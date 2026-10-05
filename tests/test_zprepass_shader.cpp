#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <string>
#include "VSZPrepass.h"
#include "../renderer/zprepass_mesh.h"
#include <limits>
#include "GSZPrepassProbe.h"
using Microsoft::WRL::ComPtr;
namespace {
size_t checks{},draws{},observedVertices{};
void need(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void hr(HRESULT value,const char* why){if(FAILED(value)){char message[160];sprintf_s(message,"%s:%08lX",why,ULONG(value));throw std::runtime_error(message);}}
using Vec=std::array<float,4>;
using Constants=std::array<Vec,244>;
using Vertex=Simpsons::Graphics::ZPrepassVertex;
struct Output {Vec position;};
static_assert(sizeof(Vertex)==116 && sizeof(Output)==16 && sizeof(Constants)==3904);
// Independent geometry reference: apply six additive morph deltas, blend four
// affine bones without normalizing weights, then the one combined CPU matrix.
std::array<double,4> expected(const Vertex& v,const Constants& c,uint32_t skin) {
    std::array<double,4> p={v.position[0],v.position[1],v.position[2],1};
    if(skin) {
        if(c[37][3]>=0.5f)for(size_t morph=0;morph<6;++morph)for(size_t xyz=0;xyz<3;++xyz)
            p[xyz]+=double(v.morph[morph][xyz])*c[36+morph/4][morph%4];
        std::array<double,3> skinned{};
        for(size_t row=0;row<3;++row)for(size_t lane=0;lane<4;++lane) {
            double matrix=0;
            for(size_t weight=0;weight<4;++weight)
                matrix+=double(v.weights[weight])*c[52+3*size_t(v.indices[weight])+row][lane];
            skinned[row]+=matrix*p[lane];
        }
        std::copy(skinned.begin(),skinned.end(),p.begin());
    }
    std::array<double,4> clip{};
    for(size_t row=0;row<4;++row)for(size_t lane=0;lane<4;++lane)clip[row]+=double(c[row][lane])*p[lane];
    return clip;
}
struct Fixture {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11GeometryShader> probe;
    ComPtr<ID3D11InputLayout> layout;ComPtr<ID3D11Buffer> constants,booleans;
    Fixture(bool hardware) {
        D3D_FEATURE_LEVEL level{};
        hr(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"device");
        std::printf("Z-prepass VS %s feature_level=%X\n",hardware?"hardware":"WARP",unsigned(level));
        hr(device->CreateVertexShader(kVSZPrepass,sizeof(kVSZPrepass),nullptr,&vertex),"actual vertex shader");
        std::array<D3D11_INPUT_ELEMENT_DESC,9> inputs{};
        inputs[0]={"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        inputs[1]={"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0};
        inputs[2]={"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,28,D3D11_INPUT_PER_VERTEX_DATA,0};
        for(UINT i=0;i<6;++i)inputs[i+3]={"TEXCOORD",i+3,DXGI_FORMAT_R32G32B32_FLOAT,0,44+12*i,D3D11_INPUT_PER_VERTEX_DATA,0};
        hr(device->CreateInputLayout(inputs.data(),UINT(inputs.size()),kVSZPrepass,sizeof(kVSZPrepass),&layout),"decoded fetch payload layout");
        const D3D11_SO_DECLARATION_ENTRY outputs[]={{0,"SV_Position",0,0,4,0}};
        const UINT stride=sizeof(Output);
        hr(device->CreateGeometryShaderWithStreamOutput(kGSZPrepassProbe,sizeof(kGSZPrepassProbe),outputs,1,&stride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&probe),"output observer");
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(Constants);desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        hr(device->CreateBuffer(&desc,nullptr,&constants),"original constant bank");
        desc.ByteWidth=16;hr(device->CreateBuffer(&desc,nullptr,&booleans),"independent Boolean bank");
        context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        context->VSSetShader(vertex.Get(),nullptr,0);context->GSSetShader(probe.Get(),nullptr,0);context->PSSetShader(nullptr,nullptr,0);
        ID3D11Buffer* cb[]={constants.Get(),booleans.Get()};context->VSSetConstantBuffers(0,2,cb);
    }
    void verify(const std::vector<Vertex>& input,const Constants& c,uint32_t skin) {
        const std::array<uint32_t,4> flags={skin,0,0,0};context->UpdateSubresource(booleans.Get(),0,nullptr,flags.data(),0,0);
        context->UpdateSubresource(constants.Get(),0,nullptr,c.data(),0,0);
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(input.size()*sizeof(Vertex));desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        desc.Usage=D3D11_USAGE_IMMUTABLE;const D3D11_SUBRESOURCE_DATA data{input.data(),0,0};ComPtr<ID3D11Buffer> vb,output,readback;
        hr(device->CreateBuffer(&desc,&data,&vb),"vertex payloads");
        desc.ByteWidth=UINT(input.size()*sizeof(Output));desc.BindFlags=D3D11_BIND_STREAM_OUTPUT;desc.Usage=D3D11_USAGE_DEFAULT;
        hr(device->CreateBuffer(&desc,nullptr,&output),"output allocation");
        desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        hr(device->CreateBuffer(&desc,nullptr,&readback),"readback allocation");
        ComPtr<ID3D11Query> query;const D3D11_QUERY_DESC q{D3D11_QUERY_SO_STATISTICS,0};hr(device->CreateQuery(&q,&query),"output count query");
        auto* buffer=vb.Get();UINT stride=sizeof(Vertex),zero=0;context->IASetVertexBuffers(0,1,&buffer,&stride,&zero);
        auto* target=output.Get();context->SOSetTargets(1,&target,&zero);context->Begin(query.Get());context->Draw(UINT(input.size()),0);context->End(query.Get());
        context->SOSetTargets(0,nullptr,nullptr);context->CopyResource(readback.Get(),output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};hr(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"read actual vertex outputs");
        std::vector<Output> observed(input.size());std::memcpy(observed.data(),mapped.pData,observed.size()*sizeof(Output));context->Unmap(readback.Get(),0);
        D3D11_QUERY_DATA_SO_STATISTICS statistics{};
        need(context->GetData(query.Get(),&statistics,sizeof(statistics),0)==S_OK && statistics.NumPrimitivesWritten==input.size() &&
             statistics.PrimitivesStorageNeeded==input.size(),"Actual VS output count differs");
        ++draws;observedVertices+=input.size();
        for(size_t i=0;i<input.size();++i) {
            const auto want=expected(input[i],c,skin);const auto& got=observed[i];
            for(size_t lane=0;lane<4;++lane) {
                const auto tolerance=0.0001+std::abs(want[lane])*0.000003;
                if(!std::isfinite(got.position[lane]) || std::abs(got.position[lane]-want[lane])>tolerance) {
                    char message[180];sprintf_s(message,"vertex%zu lane%zu: actual %.9g expected %.12g Boolean%u",i,lane,got.position[lane],want[lane],skin);
                    throw std::runtime_error(message);
                }
                ++checks;
            }
        }
    }
};
}
int main(int argc,char** argv)try {
    need(argc==1||(argc==2&&std::string(argv[1])=="--hardware"),"Optional --hardware expected");
    Fixture f(argc==2);Constants c{};
    for(size_t row=0;row<4;++row)c[row][row]=1;
    for(size_t bone=0;bone<64;++bone)for(size_t row=0;row<3;++row) {
        auto& v=c[52+3*bone+row];v[row]=1+float(bone%4)*0.125f;
        v[(row+1)%3]=float(int(bone%3)-1)*0.0625f;v[3]=float(int(bone)-32)*0.25f+float(row);
    }
    std::vector<Vertex> vertices(64);
    for(size_t i=0;i<vertices.size();++i) {
        auto& v=vertices[i];v.position={float(int(i)-32)*0.25f,float(i%5)*0.5f-1,float(i%7)*0.125f-0.25f};
        v.weights={0.125f,0.25f,0.375f,0.25f};v.indices={float(i),float((i+17)%64),float((i+31)%64),float((i+47)%64)};
        for(size_t j=0;j<6;++j)v.morph[j]={float(j+1)*0.125f,float(int(i%3)-1)*float(j+1)*0.0625f,-float(j+1)*0.03125f};
    }
    // The static branch is direct combined-matrix positioning, with no shadow height cap.
    auto tall=vertices;tall[0].position[1]=60.25f;tall[1].position[1]=999.5f;tall[2].position[1]=1000.25f;
    f.verify(tall,c,0);
    c[0]={0.5f,0.125f,-0.0625f,2};c[1]={-0.25f,1.5f,0.125f,-1};
    c[2]={0.125f,-0.0625f,0.75f,5};c[3]={0.03125f,-0.015625f,0.0078125f,1};
    c[36]={0.125f,-0.25f,0.375f,0.5f};c[37]={-0.625f,0.75f,99,0};
    f.verify(vertices,c,0);f.verify(vertices,c,1);
    for(float threshold:{std::nextafter(0.5f,0.0f),0.5f,std::nextafter(0.5f,1.0f)}) {
        c[37][3]=threshold;f.verify(vertices,c,1);
    }
    // Isolate every morph input, including position5/6 sourced from c37.xy.
    for(size_t j=0;j<6;++j) {
        auto one=c;one[36]={};one[37]={0,0,0,1};one[36+j/4][j%4]=1;f.verify(vertices,one,1);
    }
    // Four bone lanes and unnormalized/negative/zero weights remain original arithmetic.
    c[37][3]=1;
    for(size_t lane=0;lane<6;++lane) {
        auto one=vertices;for(auto& v:one){v.weights={};if(lane<4)v.weights[lane]=1;else if(lane==5)v.weights={2,-0.5f,0.25f,0.5f};}
        f.verify(one,c,1);
    }
    f.verify(vertices,c,0xFFFFFFFFu);
    // When the Boolean bank is false, even NaNs in every unused FETCH and
    // float register must be ignored. c40.x cannot turn on shadow-style skinning.
    const float nan=std::numeric_limits<float>::quiet_NaN();auto dead=vertices;auto unused=c;
    for(size_t i=4;i<unused.size();++i)unused[i].fill(nan);
    unused[40][0]=1;
    for(auto& v:dead){v.weights.fill(nan);v.indices.fill(nan);for(auto& delta:v.morph)delta.fill(nan);}
    f.verify(dead,unused,0);
    // With skinning active but the morph predicate false, morph inputs alone are dead.
    dead=vertices;for(auto& v:dead)for(auto& delta:v.morph)delta.fill(nan);
    c[37][3]=std::nextafter(0.5f,0.0f);c[40][0]=0;f.verify(dead,c,1);
    std::printf("PASS Z-prepass VS: %zu checks, %zu actual draws/%zu vertices; all nine inputs, combined matrix, Boolean bank, six morph lanes and four bone lanes\n",
        checks,draws,observedVertices);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL Z-prepass VS after %zu checks: %s\n",checks,e.what());return 1;}
