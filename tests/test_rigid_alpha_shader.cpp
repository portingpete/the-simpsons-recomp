// Real GPU execution of offline shader artifacts with independent geometric
// expectations. No original instruction executor or runtime shader compiler.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include "VSRigidAlpha.h"
#include "PSRigidAlpha.h"
#include "PS168F8.h"
#include "VSRigidAlphaPixelProbe.h"
#include "GSRigidAlphaProbe.h"
using Microsoft::WRL::ComPtr;
namespace {
using Vec=std::array<float,4>;
using Input=std::array<Vec,4>;
using Output=std::array<Vec,5>;
using VBank=std::array<Vec,64>;
using PBank=std::array<Vec,51>;
struct Vertex { std::array<float,3> position,normal; Vec color; std::array<float,2> uv; };
static_assert(sizeof(Vertex)==48 && sizeof(Output)==80);
size_t checks{},vsDraws{},psDraws{};
void need(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
void hr(HRESULT r,const char* what){if(FAILED(r)){char s[160];sprintf_s(s,"%s: %08lX",what,ULONG(r));throw std::runtime_error(s);}}
void close(float actual,double expected,const char* name){
    ++checks;
    if(!std::isfinite(actual)||std::abs(actual-expected)>0.000008*(1+std::abs(expected))){
        char s[200];sprintf_s(s,"%s: %.9g expected %.12g",name,actual,expected);throw std::runtime_error(s);
    }
}
struct Fixture {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vs,probeVs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11GeometryShader> gs;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vb,vc,pc,probe,stream,readStream;
    ComPtr<ID3D11Texture2D> target,readTarget,texture;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> raster;
    std::array<Vec,4> texels={Vec{.13f,.71f,.27f,-9},Vec{.83f,.19f,.47f,7},
                                     Vec{.31f,.97f,.59f,3},Vec{.67f,.41f,.89f,-5}};
    ComPtr<ID3D11Buffer> buffer(UINT size,UINT bind,bool staging=false){
        D3D11_BUFFER_DESC d{};d.ByteWidth=size;d.BindFlags=bind;
        d.Usage=staging?D3D11_USAGE_STAGING:D3D11_USAGE_DEFAULT;
        d.CPUAccessFlags=staging?D3D11_CPU_ACCESS_READ:0;
        ComPtr<ID3D11Buffer> b;hr(device->CreateBuffer(&d,nullptr,&b),"buffer");return b;
    }
    explicit Fixture(bool hardware,bool texturedAlpha=false){
        D3D_FEATURE_LEVEL level{};const D3D_FEATURE_LEVEL requested=D3D_FEATURE_LEVEL_11_0;
        hr(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,
            nullptr,0,&requested,1,D3D11_SDK_VERSION,&device,&level,&context),"device");
        need(level==requested,"SM5 required");
        hr(device->CreateVertexShader(kVSRigidAlpha,sizeof(kVSRigidAlpha),nullptr,&vs),"VS");
        hr(device->CreateVertexShader(kVSRigidAlphaPixelProbe,sizeof(kVSRigidAlphaPixelProbe),nullptr,&probeVs),"probe VS");
        hr(device->CreatePixelShader(texturedAlpha?kPS168F8:kPSRigidAlpha,
            texturedAlpha?sizeof(kPS168F8):sizeof(kPSRigidAlpha),nullptr,&ps),"PS");
        const D3D11_SO_DECLARATION_ENTRY elements[]={{0,"SV_Position",0,0,4,0},{0,"TEXCOORD",0,0,4,0},
            {0,"TEXCOORD",1,0,4,0},{0,"TEXCOORD",2,0,4,0},{0,"TEXCOORD",3,0,4,0}};
        const UINT streamStride=sizeof(Output);
        hr(device->CreateGeometryShaderWithStreamOutput(kGSRigidAlphaProbe,sizeof(kGSRigidAlphaProbe),
            elements,5,&streamStride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&gs),"probe GS");
        const D3D11_INPUT_ELEMENT_DESC inputs[]={{"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",3,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0}};
        hr(device->CreateInputLayout(inputs,4,kVSRigidAlpha,sizeof(kVSRigidAlpha),&layout),"layout");
        vb=buffer(sizeof(Vertex),D3D11_BIND_VERTEX_BUFFER);vc=buffer(sizeof(VBank),D3D11_BIND_CONSTANT_BUFFER);
        pc=buffer(sizeof(PBank),D3D11_BIND_CONSTANT_BUFFER);probe=buffer(sizeof(Input),D3D11_BIND_CONSTANT_BUFFER);
        stream=buffer(sizeof(Output),D3D11_BIND_STREAM_OUTPUT);readStream=buffer(sizeof(Output),0,true);
        D3D11_TEXTURE2D_DESC t{};t.Width=t.Height=1;t.MipLevels=t.ArraySize=t.SampleDesc.Count=1;
        t.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;t.BindFlags=D3D11_BIND_RENDER_TARGET;
        hr(device->CreateTexture2D(&t,nullptr,&target),"target");hr(device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"RTV");
        t.BindFlags=0;t.Usage=D3D11_USAGE_STAGING;t.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        hr(device->CreateTexture2D(&t,nullptr,&readTarget),"readback");
        t.Width=t.Height=2;t.BindFlags=D3D11_BIND_SHADER_RESOURCE;t.Usage=D3D11_USAGE_DEFAULT;t.CPUAccessFlags=0;
        const D3D11_SUBRESOURCE_DATA init{texels.data(),2*sizeof(Vec),0};
        hr(device->CreateTexture2D(&t,&init,&texture),"sample texture");
        hr(device->CreateShaderResourceView(texture.Get(),nullptr,&srv),"SRV");
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc=D3D11_COMPARISON_NEVER;sd.MaxAnisotropy=1;
        hr(device->CreateSamplerState(&sd,&sampler),"sampler");
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        hr(device->CreateRasterizerState(&rd,&raster),"raster");
    }
    Output vertex(const Vertex& v,const VBank& c){
        context->OMSetRenderTargets(0,nullptr,nullptr);context->PSSetShader(nullptr,nullptr,0);
        context->UpdateSubresource(vb.Get(),0,nullptr,&v,0,0);context->UpdateSubresource(vc.Get(),0,nullptr,&c,0,0);
        auto* b=vb.Get();const UINT stride=sizeof(Vertex),offset=0;
        context->IASetVertexBuffers(0,1,&b,&stride,&offset);context->IASetInputLayout(layout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        context->VSSetShader(vs.Get(),nullptr,0);b=vc.Get();context->VSSetConstantBuffers(0,1,&b);
        context->GSSetShader(gs.Get(),nullptr,0);b=stream.Get();context->SOSetTargets(1,&b,&offset);
        context->Draw(1,0);++vsDraws;context->SOSetTargets(0,nullptr,nullptr);
        context->CopyResource(readStream.Get(),stream.Get());D3D11_MAPPED_SUBRESOURCE m{};
        hr(context->Map(readStream.Get(),0,D3D11_MAP_READ,0,&m),"VS map");
        Output out;std::memcpy(&out,m.pData,sizeof(out));context->Unmap(readStream.Get(),0);return out;
    }
    Vec pixel(const Input& input,const PBank& c){
        context->GSSetShader(nullptr,nullptr,0);context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->UpdateSubresource(probe.Get(),0,nullptr,&input,0,0);context->UpdateSubresource(pc.Get(),0,nullptr,&c,0,0);
        context->VSSetShader(probeVs.Get(),nullptr,0);auto* b=probe.Get();context->VSSetConstantBuffers(1,1,&b);
        context->PSSetShader(ps.Get(),nullptr,0);b=pc.Get();context->PSSetConstantBuffers(0,1,&b);
        auto* resource=srv.Get();context->PSSetShaderResources(0,1,&resource);
        auto* sample=sampler.Get();context->PSSetSamplers(0,1,&sample);
        auto* color=rtv.Get();context->OMSetRenderTargets(1,&color,nullptr);
        const Vec clear{-9,-8,-7,-6};context->ClearRenderTargetView(color,clear.data());
        context->OMSetDepthStencilState(nullptr,0);context->OMSetBlendState(nullptr,nullptr,0xFFFFFFFF);
        const D3D11_VIEWPORT viewport{0,0,1,1,0,1};context->RSSetViewports(1,&viewport);context->RSSetState(raster.Get());
        context->Draw(3,0);++psDraws;context->CopyResource(readTarget.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE m{};hr(context->Map(readTarget.Get(),0,D3D11_MAP_READ,0,&m),"PS map");
        Vec out;std::memcpy(&out,m.pData,sizeof(out));context->Unmap(readTarget.Get(),0);return out;
    }
};
void vertices(Fixture& f){
    for(int sample=0;sample<40;++sample){
        VBank bank{};for(size_t i=0;i<bank.size();++i)for(size_t j=0;j<4;++j)
            bank[i][j]=float((int(i)*3+int(j)*7+sample*11)%37-18)*.0625f;
        Vertex v{{float(sample-13)*.125f,.375f,-.875f},{.25f,-.75f,float(sample-21)*.0625f},
                 {.2f,.4f,.7f,.3f},{.31f,.67f}};
        if(sample==0)v.normal={0,0,0};
        const auto actual=f.vertex(v,bank);
        for(size_t row=0;row<4;++row){
            double clip=bank[row][3],world=bank[12+row][3];
            for(size_t lane=0;lane<3;++lane){clip+=double(bank[row][lane])*v.position[lane];world+=double(bank[12+row][lane])*v.position[lane];}
            close(actual[0][row],clip,"clip");close(actual[2][row],world,"world");
            close(actual[4][row],v.color[row],"color");
        }
        for(size_t row=0;row<3;++row){double n=0;for(size_t lane=0;lane<3;++lane)n+=double(bank[12+row][lane])*v.normal[lane];close(actual[3][row],n,"normal");}
        close(actual[3][3],0,"normal padding");
        for(size_t lane=0;lane<4;++lane)close(actual[1][lane],lane<2?v.uv[lane]:0,"UV");
    }
}
void pixels(Fixture& f,bool texturedAlpha=false){
    // These are geometric view/normal vectors, not a shader-register emulator.
    const std::array<Vec,6> normals={Vec{0,0,1,17},Vec{0,0,-2,17},Vec{1,0,0,17},
                                    Vec{.25f,-.75f,1.5f,17},Vec{0,0,0,17},Vec{-2,1,-.25f,17}};
    for(size_t n=0;n<normals.size();++n)for(int viewCase=0;viewCase<3;++viewCase)
    for(float threshold:{-.125f,0.f,.25f,.875f,1.f,1.25f})for(size_t texel=0;texel<4;++texel)
    for(int alphaTest=0;alphaTest<(texturedAlpha?2:1);++alphaTest){
        PBank bank{};for(size_t i=0;i<bank.size();++i)bank[i]={float(i+3),-float(i+5),float(i+7),-float(i+11)};
        Input in{Vec{(texel%2)*.5f+.25f,(texel/2)*.5f+.25f,11,19},Vec{.5f,-.25f,.75f,23},normals[n],Vec{.9f,.2f,.7f,.625f}};
        const Vec view=viewCase==0?Vec{0,0,2,0}:viewCase==1?Vec{.75f,-.5f,1.25f,0}:Vec{0,0,0,0};
        for(size_t i=0;i<3;++i)bank[4][i]=in[1][i]+view[i];
        bank[40][3]=texel==0?0:texel==1?1:texel==2?-.5f:2;
        bank[49]={threshold,texel==2?-.5f:1.75f,37,-29};
        bank[48][0]=float(alphaTest);
        double nl=0,vl=0,dot=0;for(size_t i=0;i<3;++i){nl+=double(in[2][i])*in[2][i];vl+=double(view[i])*view[i];dot+=double(in[2][i])*view[i];}
        const double cosine=nl==0||vl==0?0:std::clamp(std::abs(dot/std::sqrt(nl*vl)),0.0,1.0);
        const bool silhouette=threshold>=cosine;
        const double intensity=std::clamp(bank[49][1]*(1-cosine),0.0,1.0);
        const auto actual=f.pixel(in,bank);
        if(texturedAlpha&&alphaTest&&f.texels[texel][3]<.0001f){
            for(size_t lane=0;lane<4;++lane)close(actual[lane],-9.0+double(lane),"discard preserves target");
            continue;
        }
        for(size_t lane=0;lane<3;++lane)close(actual[lane],silhouette?intensity:f.texels[texel][lane],"RGB silhouette/sample");
        close(actual[3],silhouette?1:double(in[3][3])*bank[40][3]*(texturedAlpha?f.texels[texel][3]:1),"opacity");
    }
}
}
int main(int argc,char** argv)try{
    const bool hardware=argc==2&&std::string(argv[1])=="--hardware";
    Fixture f(hardware);vertices(f);pixels(f);
    Fixture textured(hardware,true);pixels(textured,true);
    // Check exact cutoff, transparent alpha and partial opacity in addition
    // to negative/over-one samples. RGB must be sampled even with c48.x=0.
    const float cutoff=.0001f;
    textured.texels[0][3]=0;
    textured.texels[1][3]=std::nextafter(cutoff,0.f);
    textured.texels[2][3]=cutoff;
    textured.texels[3][3]=.5f;
    textured.context->UpdateSubresource(textured.texture.Get(),0,nullptr,textured.texels.data(),2*sizeof(Vec),0);
    pixels(textured,true);
    std::printf("PASS rigidalpha shader %s: %zu checks, %zu VS draws, %zu PS draws\n",hardware?"hardware":"WARP",checks,vsDraws,psDraws);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL rigidalpha after %zu checks: %s\n",checks,e.what());return 1;}
