// Native GPU transport versus independent original-instruction numerical data.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include "skin_dualalpha_fixtures.h"
#include "VSSkinDualAlpha.h"
#include "PSSkinDualAlpha.h"
#include "VSSkinDualAlphaPixelProbe.h"
#include "GSSkinDualAlphaProbe.h"
#include "renderer/skin_input.h"
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr,const char* what) {
    if(FAILED(hr)){char message[180];sprintf_s(message,"%s: %08lX",what,ULONG(hr));throw std::runtime_error(message);}
}
void compare(float actual,float expected,unsigned sample,unsigned stage,unsigned lane) {
    if(!std::isfinite(actual)||!std::isfinite(expected)||std::abs(actual-expected)>.00004f+.00004f*std::abs(expected)) {
        char message[200];sprintf_s(message,"Dual skin alpha oracle case%u stage%u lane%u actual%.9g expected%.9g",sample,stage,lane,actual,expected);
        throw std::runtime_error(message);
    }
}
struct Fixture {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11ShaderResourceView> texture;
    ComPtr<ID3D11SamplerState> sampler;
    explicit Fixture(bool hardware) {
        D3D_FEATURE_LEVEL level{};
        check(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&device,&level,&context),"Dual skin alpha oracle device");
        D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=4;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
        d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA data{kSkinDualAlphaTexture,4*16,0};ComPtr<ID3D11Texture2D> resource;
        check(device->CreateTexture2D(&d,&data,&resource),"Dual skin alpha original texture");
        check(device->CreateShaderResourceView(resource.Get(),nullptr,&texture),"Dual skin alpha texture SRV");
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;s.MaxAnisotropy=1;
        s.ComparisonFunc=D3D11_COMPARISON_NEVER;s.MaxLOD=13;
        check(device->CreateSamplerState(&s,&sampler),"Dual skin alpha original linear/wrap sampler");
    }
    ComPtr<ID3D11Buffer> buffer(const void* values,UINT bytes,UINT binding,D3D11_USAGE usage=D3D11_USAGE_IMMUTABLE) {
        D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.BindFlags=binding;d.Usage=usage;
        const D3D11_SUBRESOURCE_DATA data{values,0,0};ComPtr<ID3D11Buffer> result;
        check(device->CreateBuffer(&d,values?&data:nullptr,&result),"Dual skin alpha oracle buffer");return result;
    }
    void vertices(const SkinDualAlphaCase& c,unsigned index) {
        ComPtr<ID3D11VertexShader> vs;
        check(device->CreateVertexShader(kVSSkinDualAlpha,sizeof(kVSSkinDualAlpha),nullptr,&vs),"Dual skin alpha original VS");
        const auto& elements=Simpsons::Graphics::SkinInputElements;
        ComPtr<ID3D11InputLayout> layout;
        check(device->CreateInputLayout(elements.data(),UINT(elements.size()),kVSSkinDualAlpha,sizeof(kVSSkinDualAlpha),&layout),"Dual skin alpha twelve-input semantic layout");
        const D3D11_SO_DECLARATION_ENTRY entries[]={
            {0,"SV_POSITION",0,0,4,0},{0,"TEXCOORD",0,0,2,0},{0,"TEXCOORD",1,0,3,0},
            {0,"TEXCOORD",2,0,4,0},{0,"TEXCOORD",3,0,4,0}};
        const UINT stride=17*4;
        ComPtr<ID3D11GeometryShader> gs;
        check(device->CreateGeometryShaderWithStreamOutput(kGSSkinDualAlphaProbe,sizeof(kGSSkinDualAlphaProbe),
            entries,5,&stride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&gs),"Dual skin alpha original VS stream output");
        auto vb=buffer(c.input,sizeof(c.input),D3D11_BIND_VERTEX_BUFFER);auto cb=buffer(c.constants,sizeof(c.constants),D3D11_BIND_CONSTANT_BUFFER);
        auto out=buffer(nullptr,stride,D3D11_BIND_STREAM_OUTPUT,D3D11_USAGE_DEFAULT);
        D3D11_BUFFER_DESC readDesc{};out->GetDesc(&readDesc);readDesc.Usage=D3D11_USAGE_STAGING;readDesc.BindFlags=0;readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> read;check(device->CreateBuffer(&readDesc,nullptr,&read),"Dual skin alpha stream output staging");
        auto* input=vb.Get();const UINT inputStride=sizeof(c.input),offset=0;context->IASetVertexBuffers(0,1,&input,&inputStride,&offset);
        context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        context->VSSetShader(vs.Get(),nullptr,0);auto* constants=cb.Get();context->VSSetConstantBuffers(0,1,&constants);
        context->GSSetShader(gs.Get(),nullptr,0);context->PSSetShader(nullptr,nullptr,0);
        auto* output=out.Get();context->SOSetTargets(1,&output,&offset);context->Draw(1,0);context->SOSetTargets(0,nullptr,nullptr);
        context->CopyResource(read.Get(),out.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped),"Dual skin alpha stream output read");
        const auto* values=static_cast<const float*>(mapped.pData);
        for(unsigned lane=0;lane<17;++lane)compare(values[lane],c.expected[lane],index,c.stage,lane);
        context->Unmap(read.Get(),0);context->GSSetShader(nullptr,nullptr,0);
    }
    void pixels(const SkinDualAlphaCase& c,unsigned index) {
        if(c.stage!=1)throw std::runtime_error("Unknown dual skin alpha pixel oracle stage");
        ComPtr<ID3D11VertexShader> vs;
        check(device->CreateVertexShader(kVSSkinDualAlphaPixelProbe,sizeof(kVSSkinDualAlphaPixelProbe),nullptr,&vs),"Dual skin alpha pixel transport");
        ComPtr<ID3D11PixelShader> ps;check(device->CreatePixelShader(kPSSkinDualAlpha,sizeof(kPSSkinDualAlpha),nullptr,&ps),"Dual skin alpha original PS");
        float probes[4][4]{};std::memcpy(probes,c.input,sizeof(probes));
        auto cb=buffer(c.constants,sizeof(c.constants),D3D11_BIND_CONSTANT_BUFFER);auto probe=buffer(probes,sizeof(probes),D3D11_BIND_CONSTANT_BUFFER);
        D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
        d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> target;
        check(device->CreateTexture2D(&d,nullptr,&target),"Dual skin alpha pixel target");ComPtr<ID3D11RenderTargetView> rtv;
        check(device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"Dual skin alpha pixel RTV");
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> read;
        check(device->CreateTexture2D(&d,nullptr,&read),"Dual skin alpha pixel staging");
        auto* view=rtv.Get();context->OMSetRenderTargets(1,&view,nullptr);
        const D3D11_VIEWPORT viewport{0,0,1,1,0,1};context->RSSetViewports(1,&viewport);
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster),"Dual skin alpha pixel raster");context->RSSetState(raster.Get());
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);context->GSSetShader(nullptr,nullptr,0);
        auto* constants=cb.Get();context->PSSetConstantBuffers(0,1,&constants);auto* inputs=probe.Get();context->VSSetConstantBuffers(2,1,&inputs);
        auto* sampled=texture.Get();context->PSSetShaderResources(0,1,&sampled);auto* state=sampler.Get();context->PSSetSamplers(0,1,&state);
        context->Draw(3,0);context->CopyResource(read.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped),"Dual skin alpha pixel read");
        const auto* values=static_cast<const float*>(mapped.pData);
        for(unsigned lane=0;lane<4;++lane)compare(values[lane],c.expected[lane],index,c.stage,lane);
        context->Unmap(read.Get(),0);context->OMSetRenderTargets(0,nullptr,nullptr);
    }
};
}
int main(int argc,char** argv) {
    try {
        if(argc>2||(argc==2&&std::string(argv[1])!="--hardware"))throw std::runtime_error("Usage: SkinDualAlphaShaderTests [--hardware]");
        const bool hardware=argc==2;Fixture fixture(hardware);
        static_assert(std::size(kSkinDualAlphaCases)==64);std::array<unsigned,2> counts{};
        unsigned index=0;
        for(const auto& c:kSkinDualAlphaCases) {
            if(c.stage>=counts.size())throw std::runtime_error("Unknown dual skin alpha numerical oracle stage");
            ++counts[c.stage];if(c.stage==0)fixture.vertices(c,index);else fixture.pixels(c,index);++index;
        }
        for(const auto count:counts)if(count!=32)throw std::runtime_error("Missing dual skin alpha numerical oracle case");
        std::printf("PASS original dual skin alpha numerical GPU oracle (%s): %u bone/morph vertex and base-texture pixel cases\n",hardware?"hardware":"WARP",index);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL dual skin alpha shader GPU: %s\n",e.what());return 1;}
}
