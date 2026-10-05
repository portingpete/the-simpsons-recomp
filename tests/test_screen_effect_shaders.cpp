// Native screen-effect shaders against the independent original-microcode
// reference fixture from tools/check_screen_effect_shaders.py.
// Float outputs are compared before packing; no console precision claim.
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include "VSPostAutoTextured.h"
#include "PSDof.h"
#include "PSBlur.h"
#include "PSBloom.h"
#include "PSFogLinear.h"
#include "PSFogExp.h"
#include "PSFogExp2.h"
#include "PSSat.h"
#include "PSFlatEffect.h"
#include "PSModulatedFlat.h"
using Microsoft::WRL::ComPtr;
namespace {
size_t checks{};
void need(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void hr(HRESULT value,const char* why){if(FAILED(value)){char msg[160];sprintf_s(msg,"%s %08lX",why,ULONG(value));throw std::runtime_error(msg);}}
struct Reader {
    std::vector<uint8_t> bytes;size_t at{};
    template<class T>T get(){need(at+sizeof(T)<=bytes.size(),"Truncated fixture");T value;std::memcpy(&value,bytes.data()+at,sizeof(T));at+=sizeof(T);return value;}
};
struct FixtureTexture {uint32_t width{},height{},channels{},linear{};std::vector<float> data;};
constexpr const char* names[]={"PSDof","PSBlur","PSBloom","PSFogLinear","PSFogExp","PSFogExp2","PSSat","PSFlatEffect","PSModulatedFlat"};
void run(const char* path,bool hardware){
    std::ifstream input(path,std::ios::binary);Reader r{std::vector<uint8_t>((std::istreambuf_iterator<char>(input)),{})};
    need(r.get<uint32_t>()==0x58464553&&r.get<uint32_t>()==1,"Fixture identity differs");
    const uint32_t count=r.get<uint32_t>(),extent=r.get<uint32_t>(),width=extent&0xFFFF,height=extent>>16;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    hr(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"device");
    ComPtr<ID3D11VertexShader> vs;hr(device->CreateVertexShader(kVSPostAutoTextured,sizeof(kVSPostAutoTextured),nullptr,&vs),"VS821529C8");
    const D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
    ComPtr<ID3D11InputLayout> layout;hr(device->CreateInputLayout(&element,1,kVSPostAutoTextured,sizeof(kVSPostAutoTextured),&layout),"float2 layout");
    const void* code[]={kPSDof,kPSBlur,kPSBloom,kPSFogLinear,kPSFogExp,kPSFogExp2,kPSSat,kPSFlatEffect,kPSModulatedFlat};
    const size_t sizes[]={sizeof(kPSDof),sizeof(kPSBlur),sizeof(kPSBloom),sizeof(kPSFogLinear),sizeof(kPSFogExp),sizeof(kPSFogExp2),sizeof(kPSSat),sizeof(kPSFlatEffect),sizeof(kPSModulatedFlat)};
    std::array<ComPtr<ID3D11PixelShader>,9> ps;for(size_t i=0;i<ps.size();++i)hr(device->CreatePixelShader(code[i],sizes[i],nullptr,&ps[i]),"effect PS");
    const float strip[]={-1,-1,1,-1,-1,1,1,1};D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(strip);bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;bd.Usage=D3D11_USAGE_IMMUTABLE;
    D3D11_SUBRESOURCE_DATA sd{strip,0,0};ComPtr<ID3D11Buffer> vb;hr(device->CreateBuffer(&bd,&sd,&vb),"original strip");
    bd.ByteWidth=160;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.Usage=D3D11_USAGE_DEFAULT;ComPtr<ID3D11Buffer> cb;hr(device->CreateBuffer(&bd,nullptr,&cb),"c0..9");
    D3D11_TEXTURE2D_DESC td{};td.Width=width;td.Height=height;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target,staging;ComPtr<ID3D11RenderTargetView> rtv;hr(device->CreateTexture2D(&td,nullptr,&target),"target");hr(device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"RTV");
    td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;hr(device->CreateTexture2D(&td,nullptr,&staging),"staging");
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> raster;hr(device->CreateRasterizerState(&rd,&raster),"raster");
    std::array<double,9> worst{};std::array<uint32_t,9> seen{};
    for(uint32_t item=0;item<count;++item){
        const uint32_t shader=r.get<uint32_t>();need(shader<9,"Fixture shader index");
        std::array<float,40> constants{};for(auto& value:constants)value=r.get<float>();
        std::array<FixtureTexture,3> textures;
        for(auto& t:textures){t.width=r.get<uint32_t>();t.height=r.get<uint32_t>();t.channels=r.get<uint32_t>();t.linear=r.get<uint32_t>();
            t.data.resize(size_t(t.width)*t.height*t.channels);for(auto& value:t.data)value=r.get<float>();}
        std::vector<float> expected(size_t(width)*height*4);for(auto& value:expected)value=r.get<float>();
        context->ClearState();context->UpdateSubresource(cb.Get(),0,nullptr,constants.data(),0,0);
        // Fixture textures 0/1/depth bind to native t0/t2/t3 with samplers s0/s1/s2.
        constexpr UINT slots[]={0,2,3},samplerSlots[]={0,1,2};
        std::array<ComPtr<ID3D11ShaderResourceView>,3> views;std::array<ComPtr<ID3D11SamplerState>,3> samplers;
        for(size_t i=0;i<3;++i){const auto& t=textures[i];if(!t.width)continue;
            D3D11_TEXTURE2D_DESC desc{};desc.Width=t.width;desc.Height=t.height;desc.MipLevels=desc.ArraySize=1;desc.SampleDesc.Count=1;
            desc.Format=t.channels==4?DXGI_FORMAT_R32G32B32A32_FLOAT:DXGI_FORMAT_R32_FLOAT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.Usage=D3D11_USAGE_IMMUTABLE;
            D3D11_SUBRESOURCE_DATA data{t.data.data(),UINT(t.width*t.channels*4),0};ComPtr<ID3D11Texture2D> resource;hr(device->CreateTexture2D(&desc,&data,&resource),"fixture texture");
            hr(device->CreateShaderResourceView(resource.Get(),nullptr,&views[i]),"fixture view");
            D3D11_SAMPLER_DESC s{};s.Filter=t.linear?D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT:D3D11_FILTER_MIN_MAG_MIP_POINT;
            s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxAnisotropy=1;s.ComparisonFunc=D3D11_COMPARISON_NEVER;hr(device->CreateSamplerState(&s,&samplers[i]),"sampler");
            auto* view=views[i].Get();context->PSSetShaderResources(slots[i],1,&view);auto* sampler=samplers[i].Get();context->PSSetSamplers(samplerSlots[i],1,&sampler);}
        auto* buffer=cb.Get();context->PSSetConstantBuffers(0,1,&buffer);
        UINT stride=8,offset=0;auto* vertices=vb.Get();context->IASetVertexBuffers(0,1,&vertices,&stride,&offset);context->IASetInputLayout(layout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps[shader].Get(),nullptr,0);
        context->RSSetState(raster.Get());const D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&viewport);
        auto* view=rtv.Get();context->OMSetRenderTargets(1,&view,nullptr);const float clear[4]{-7,-7,-7,-7};context->ClearRenderTargetView(view,clear);
        context->Draw(4,0);context->CopyResource(staging.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE map{};hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"readback");
        for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x){const auto* got=reinterpret_cast<const float*>(static_cast<const uint8_t*>(map.pData)+y*map.RowPitch)+4*x;
            const float* want=expected.data()+4*(size_t(y)*width+x);
            for(uint32_t lane=0;lane<4;++lane){const double error=std::abs(double(got[lane])-want[lane]);
                if(!(error<=2e-4+2e-4*std::abs(want[lane]))){char msg[240];
                    sprintf_s(msg,"%s case%u pixel(%u,%u) lane%u native %.9g reference %.9g",names[shader],item,x,y,lane,got[lane],want[lane]);context->Unmap(staging.Get(),0);need(false,msg);}
                ++checks;if(error>worst[shader])worst[shader]=error;}}
        context->Unmap(staging.Get(),0);++seen[shader];
    }
    need(r.at==r.bytes.size(),"Fixture has trailing bytes");
    for(size_t i=0;i<9;++i){need(seen[i]>0,"Fixture omitted a shader");std::printf("  %-12s cases=%u worst=%.3g\n",names[i],seen[i],worst[i]);}
    std::printf("PASS screen effect shaders %s: %zu checks against the original microcode reference\n",hardware?"hardware":"WARP",checks);
}
}
int main(int argc,char** argv)try{
    need(argc==2||(argc==3&&std::string(argv[2])=="--hardware"),"Fixture path and optional --hardware");run(argv[1],argc==3);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL screen effect shaders after %zu checks: %s\n",checks,e.what());return 1;}
