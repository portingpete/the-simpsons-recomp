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
#include "VSShadowDepth.h"
#include "GSShadowDepthProbe.h"
using Microsoft::WRL::ComPtr;
namespace {
size_t checks{};
void need(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void hr(HRESULT value,const char* why){if(FAILED(value)){char message[160];sprintf_s(message,"%s:%08lX",why,ULONG(value));throw std::runtime_error(message);}}
using Vec=std::array<float,4>;
using Constants=std::array<Vec,244>;
struct Vertex {std::array<float,3> position;std::array<float,2> uv;Vec weights,indices;};
struct Output {Vec position;std::array<float,2> uv;Vec clip;};
static_assert(sizeof(Vertex)==52 && sizeof(Output)==40 && sizeof(Constants)==3904);
// Independent geometric reference: blend affine bone matrices, transform the
// point, apply the original height rule, then world and view-projection. This
// does not reproduce the shader's registers, swizzles or issue-slot schedule.
std::array<double,4> expected(const Vertex& v,const Constants& c) {
    std::array<double,4> p={v.position[0],v.position[1],v.position[2],1};
    if(c[40][0]!=0) {
        std::array<double,3> skinned{};
        for(size_t row=0;row<3;++row)for(size_t lane=0;lane<4;++lane) {
            double matrix=0;
            for(size_t weight=0;weight<4;++weight)
                matrix+=double(v.weights[weight])*c[52+3*size_t(v.indices[weight])+row][lane];
            skinned[row]+=matrix*p[lane];
        }
        std::copy(skinned.begin(),skinned.end(),p.begin());
    }
    if(p[1]<1000)p[1]=std::min(p[1],60.0);
    std::array<double,4> world{},clip{};
    for(size_t row=0;row<4;++row)for(size_t lane=0;lane<4;++lane)world[row]+=double(c[12+row][lane])*p[lane];
    for(size_t row=0;row<4;++row)for(size_t lane=0;lane<4;++lane)clip[row]+=double(c[row][lane])*world[lane];
    return clip;
}
struct Fixture {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11GeometryShader> probe;
    ComPtr<ID3D11InputLayout> layout;ComPtr<ID3D11Buffer> constants;
    Fixture(bool hardware) {
        D3D_FEATURE_LEVEL level{};
        hr(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"device");
        std::printf("Shadow depth VS %s feature_level=%X\n",hardware?"hardware":"WARP",unsigned(level));
        hr(device->CreateVertexShader(kVSShadowDepth,sizeof(kVSShadowDepth),nullptr,&vertex),"actual vertex shader");
        const D3D11_INPUT_ELEMENT_DESC inputs[]={
            {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,20,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",3,DXGI_FORMAT_R32G32B32A32_FLOAT,0,36,D3D11_INPUT_PER_VERTEX_DATA,0}};
        hr(device->CreateInputLayout(inputs,4,kVSShadowDepth,sizeof(kVSShadowDepth),&layout),"decoded fetch payload layout");
        const D3D11_SO_DECLARATION_ENTRY outputs[]={
            {0,"SV_Position",0,0,4,0},{0,"TEXCOORD",0,0,2,0},{0,"TEXCOORD",1,0,4,0}};
        const UINT stride=sizeof(Output);
        hr(device->CreateGeometryShaderWithStreamOutput(kGSShadowDepthProbe,sizeof(kGSShadowDepthProbe),outputs,3,&stride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&probe),"output observer");
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(Constants);desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        hr(device->CreateBuffer(&desc,nullptr,&constants),"original constant bank");
        context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        context->VSSetShader(vertex.Get(),nullptr,0);context->GSSetShader(probe.Get(),nullptr,0);context->PSSetShader(nullptr,nullptr,0);
        auto* cb=constants.Get();context->VSSetConstantBuffers(0,1,&cb);
    }
    void verify(const std::vector<Vertex>& input,const Constants& c) {
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
        for(size_t i=0;i<input.size();++i) {
            const auto want=expected(input[i],c);const auto& got=observed[i];
            need(got.uv==input[i].uv && got.clip==got.position,"Original UV or duplicate clip export changed");
            for(size_t lane=0;lane<4;++lane) {
                const auto tolerance=0.0001+std::abs(want[lane])*0.000003;
                if(!std::isfinite(got.position[lane]) || std::abs(got.position[lane]-want[lane])>tolerance) {
                    char message[180];sprintf_s(message,"vertex%zu lane%zu: actual %.9g expected %.12g skin%.1f",i,lane,got.position[lane],want[lane],c[40][0]);
                    throw std::runtime_error(message);
                }
                ++checks;
            }
        }
    }
};
}
int main(int argc,char** argv)try {
    need(argc==1 || (argc==2 && std::string(argv[1])=="--hardware"),"Optional --hardware expected");
    Fixture fixture(argc==2);Constants c{};
    for(size_t i=0;i<c.size();++i)for(size_t j=0;j<4;++j)c[i][j]=float(int(i%11)-5)*0.03125f+float(j)*0.015625f;
    for(size_t row=0;row<4;++row){c[row]={};c[12+row]={};c[row][row]=c[12+row][row]=1;}
    for(size_t bone=0;bone<64;++bone)for(size_t row=0;row<3;++row) {
        c[52+bone*3+row]={};c[52+bone*3+row][row]=1;
        if(bone!=0 && bone!=63) {
            c[52+bone*3+row][row]=1+float(bone%4)*0.125f;
            c[52+bone*3+row][(row+1)%3]=float(int(bone%3)-1)*0.0625f;
            c[52+bone*3+row][3]=float(int(bone)-32)*0.25f+float(row);
        }
    }
    std::vector<Vertex> vertices;
    constexpr float heights[]={-1024,-60,-0.25f,0,59.9990234375f,60,60.0009765625f,999.9990234375f,1000,1000.0009765625f,1024};
    for(float height:heights)for(unsigned index=0;index<64;++index)
        vertices.push_back({{float(int(index)-32)*.25f,height,float(index%7)*.125f-1},
            {float(index)*.015625f-.5f,1.5f-float(index)*.03125f},
            {.125f,.25f,.375f,.25f},{float(index),float((index+17)%64),float((index+31)%64),float((index+47)%64)}});
    // Identity matrices isolate the height threshold and all decoded components.
    for(float skin:{0.0f,1.0f,-1.0f}){c[40][0]=skin;fixture.verify(vertices,c);}
    // Non-diagonal world and projective transforms expose row/column mistakes.
    c[12]={1.25f,.125f,-.25f,3};c[13]={-.25f,.75f,.0625f,-7};c[14]={.5f,-.125f,1.5f,11};c[15]={0,0,0,1};
    c[0]={.5f,.125f,-.0625f,2};c[1]={-.25f,1.5f,.125f,-1};c[2]={.125f,-.0625f,.75f,5};c[3]={.03125f,-.015625f,.0078125f,1};
    for(float skin:{0.0f,1.0f}){c[40][0]=skin;fixture.verify(vertices,c);}
    // One-hot and zero weights test each bone lane independently and confirm
    // the program does not normalize weights or blend in an identity matrix.
    for(unsigned lane=0;lane<5;++lane) {
        auto one=vertices;for(auto& v:one){v.weights={};if(lane<4)v.weights[lane]=1;}
        c[40][0]=1;fixture.verify(one,c);
    }
    std::printf("PASS shadow depth VS:%zu checks;10 actual draws,7040 vertices,skin branches,bone0..63,height thresholds,world/projection and exports; no game shadow draw\n",checks);
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL shadow depth VS:%zu checks:%s\n",checks,e.what());return 1;}
