// Native GPU transport versus independent original-instruction numerical data.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <iterator>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include "skin_dualtextured_uv_fixtures.h"
#include "VSSkinDualUV.h"
#include "GSSkinDualUVProbe.h"
#include "VSSkinDualUVAlpha.h"
#include "GSSkinDualUVAlphaProbe.h"
#include "PSSkinDualUV.h"
#include "PSSkinDualUVAlpha.h"
#include "VSSkinDualUVPixelProbe.h"
#include "VSSkinDualUVAlphaPixelProbe.h"
#include "PSSkinDualUVDraw.h"
#include "PSSkinDualUVAlphaDraw.h"
#include "renderer/skin_input.h"
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr,const char* what){
    if(FAILED(hr)){char message[180];sprintf_s(message,"%s: %08lX",what,ULONG(hr));throw std::runtime_error(message);}
}
void compare(float actual,float expected,unsigned sample,unsigned stage,unsigned lane){
    if(!std::isfinite(actual)||!std::isfinite(expected)||std::abs(actual-expected)>.00004f+.00004f*std::abs(expected)){
        char message[200];sprintf_s(message,"Skin dual UV oracle case%u stage%u lane%u actual%.9g expected%.9g",sample,stage,lane,actual,expected);
        throw std::runtime_error(message);
    }
}
struct Fixture {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11ShaderResourceView> material,secondary;
    ComPtr<ID3D11SamplerState> linearWrap;
    explicit Fixture(bool hardware){
        D3D_FEATURE_LEVEL level{};
        check(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&device,&level,&context),"Skin dual UV oracle device");
        auto texture=[&](const float values[16][4]){
            D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=4;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
            d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            const D3D11_SUBRESOURCE_DATA data{values,4*16,0};ComPtr<ID3D11Texture2D> resource;
            check(device->CreateTexture2D(&d,&data,&resource),"Skin dual UV original fixture texture");
            ComPtr<ID3D11ShaderResourceView> view;
            check(device->CreateShaderResourceView(resource.Get(),nullptr,&view),"Skin dual UV fixture SRV");return view;
        };
        material=texture(kSkinDualUVTextures[0]);secondary=texture(kSkinDualUVTextures[1]);
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;s.MaxAnisotropy=1;
        s.ComparisonFunc=D3D11_COMPARISON_NEVER;s.MaxLOD=13;
        check(device->CreateSamplerState(&s,&linearWrap),"Skin dual UV original linear/wrap sampler");

    }
    ComPtr<ID3D11Buffer> buffer(const void* values,UINT bytes,UINT binding,D3D11_USAGE usage=D3D11_USAGE_IMMUTABLE){
        D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.BindFlags=binding;d.Usage=usage;
        const D3D11_SUBRESOURCE_DATA data{values,0,0};ComPtr<ID3D11Buffer> result;
        check(device->CreateBuffer(&d,values?&data:nullptr,&result),"Skin dual UV oracle buffer");return result;
    }
    void vertices(const SkinDualUVCase& c,unsigned index){
        const bool alpha=c.stage==1;
        const auto* vertex=alpha?kVSSkinDualUVAlpha:kVSSkinDualUV;
        const auto vertexBytes=alpha?sizeof(kVSSkinDualUVAlpha):sizeof(kVSSkinDualUV);
        const auto* geometry=alpha?kGSSkinDualUVAlphaProbe:kGSSkinDualUVProbe;
        const auto geometryBytes=alpha?sizeof(kGSSkinDualUVAlphaProbe):sizeof(kGSSkinDualUVProbe);
        ComPtr<ID3D11VertexShader> vs;check(device->CreateVertexShader(vertex,vertexBytes,nullptr,&vs),"Skin dual UV original VS");
        std::array<D3D11_INPUT_ELEMENT_DESC,13> elements{};
        std::copy(Simpsons::Graphics::SkinInputElements.begin(),Simpsons::Graphics::SkinInputElements.end(),elements.begin());
        elements[12]={"TEXCOORD",12,DXGI_FORMAT_R32G32_FLOAT,0,152,D3D11_INPUT_PER_VERTEX_DATA,0};
        ComPtr<ID3D11InputLayout> layout;
        check(device->CreateInputLayout(elements.data(),UINT(elements.size()),vertex,vertexBytes,&layout),"Skin dual UV thirteen-input semantic layout");
        const D3D11_SO_DECLARATION_ENTRY entries[]={
            {0,"SV_POSITION",0,0,4,0},{0,"TEXCOORD",0,0,2,0},{0,"TEXCOORD",1,0,2,0},
            {0,"TEXCOORD",2,0,2,0},{0,"TEXCOORD",3,0,3,0},{0,"TEXCOORD",4,0,3,0},{0,"TEXCOORD",5,0,4,0}};
        const UINT lanes=20u,stride=lanes*4;
        ComPtr<ID3D11GeometryShader> gs;
        check(device->CreateGeometryShaderWithStreamOutput(geometry,geometryBytes,entries,7,&stride,1,
            D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&gs),"Skin dual UV original VS stream output");
        auto vb=buffer(c.input,sizeof(c.input),D3D11_BIND_VERTEX_BUFFER);
        auto cb=buffer(c.constants,sizeof(c.constants),D3D11_BIND_CONSTANT_BUFFER);
        auto out=buffer(nullptr,stride,D3D11_BIND_STREAM_OUTPUT,D3D11_USAGE_DEFAULT);
        D3D11_BUFFER_DESC readDesc{};out->GetDesc(&readDesc);readDesc.Usage=D3D11_USAGE_STAGING;
        readDesc.BindFlags=0;readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> read;check(device->CreateBuffer(&readDesc,nullptr,&read),"Skin dual UV stream output staging");
        auto* input=vb.Get();const UINT inputStride=sizeof(c.input),offset=0;
        context->IASetVertexBuffers(0,1,&input,&inputStride,&offset);context->IASetInputLayout(layout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);context->VSSetShader(vs.Get(),nullptr,0);
        auto* constants=cb.Get();context->VSSetConstantBuffers(0,1,&constants);
        context->GSSetShader(gs.Get(),nullptr,0);context->PSSetShader(nullptr,nullptr,0);
        auto* output=out.Get();context->SOSetTargets(1,&output,&offset);context->Draw(1,0);context->SOSetTargets(0,nullptr,nullptr);
        context->CopyResource(read.Get(),out.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped),"Skin dual UV stream output read");
        const auto* values=static_cast<const float*>(mapped.pData);
        for(unsigned lane=0;lane<lanes;++lane)compare(values[lane],c.expected[lane],index,c.stage,lane);
        context->Unmap(read.Get(),0);context->GSSetShader(nullptr,nullptr,0);
    }
    void pixels(const SkinDualUVCase& c,unsigned index,bool production=false){
        const bool alpha=c.stage==3;
        const auto* vertex=alpha?kVSSkinDualUVAlphaPixelProbe:kVSSkinDualUVPixelProbe;
        const auto vertexBytes=alpha?sizeof(kVSSkinDualUVAlphaPixelProbe):sizeof(kVSSkinDualUVPixelProbe);
        const auto* pixel=production?(alpha?kPSSkinDualUVAlphaDraw:kPSSkinDualUVDraw):(alpha?kPSSkinDualUVAlpha:kPSSkinDualUV);
        const auto pixelBytes=production?(alpha?sizeof(kPSSkinDualUVAlphaDraw):sizeof(kPSSkinDualUVDraw)):(alpha?sizeof(kPSSkinDualUVAlpha):sizeof(kPSSkinDualUV));
        ComPtr<ID3D11VertexShader> vs;check(device->CreateVertexShader(vertex,vertexBytes,nullptr,&vs),"Skin dual UV pixel transport");
        ComPtr<ID3D11PixelShader> ps;check(device->CreatePixelShader(pixel,pixelBytes,nullptr,&ps),"Skin dual UV original PS");
        float probes[6][4]{};std::memcpy(probes,c.input,sizeof(probes));
        auto cb=buffer(c.constants,sizeof(c.constants),D3D11_BIND_CONSTANT_BUFFER);
        auto probe=buffer(probes,sizeof(probes),D3D11_BIND_CONSTANT_BUFFER);
        const float depthParameters[4]={0,0,0,0};auto depth=buffer(depthParameters,sizeof(depthParameters),D3D11_BIND_CONSTANT_BUFFER);
        D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
        d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target;check(device->CreateTexture2D(&d,nullptr,&target),"Skin dual UV pixel target");
        ComPtr<ID3D11RenderTargetView> rtv;check(device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"Skin dual UV pixel RTV");
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> read;check(device->CreateTexture2D(&d,nullptr,&read),"Skin dual UV pixel staging");
        auto* view=rtv.Get();context->OMSetRenderTargets(1,&view,nullptr);
        const std::array<float,4> sentinel{.03125f,.0625f,.09375f,.125f};context->ClearRenderTargetView(view,sentinel.data());
        const D3D11_VIEWPORT viewport{0,0,1,1,0,1};context->RSSetViewports(1,&viewport);
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster),"Skin dual UV pixel raster");context->RSSetState(raster.Get());
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);context->GSSetShader(nullptr,nullptr,0);
        auto* constants=cb.Get();context->PSSetConstantBuffers(0,1,&constants);auto* depthInput=depth.Get();context->PSSetConstantBuffers(1,1,&depthInput);auto* inputs=probe.Get();context->VSSetConstantBuffers(2,1,&inputs);
        ID3D11ShaderResourceView* sampled[2]={material.Get(),secondary.Get()};
        ID3D11SamplerState* samplers[2]={linearWrap.Get(),linearWrap.Get()};
        context->PSSetShaderResources(0,2,sampled);context->PSSetSamplers(0,2,samplers);
        context->Draw(3,0);context->CopyResource(read.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped),"Skin dual UV pixel read");
        const auto* values=static_cast<const float*>(mapped.pData);
        for(unsigned lane=0;lane<4;++lane)compare(values[lane],c.expected[lane],index,c.stage,lane);
        context->Unmap(read.Get(),0);context->OMSetRenderTargets(0,nullptr,nullptr);
    }
};
}
int main(int argc,char** argv){try{
    if(argc>2||(argc==2&&std::string(argv[1])!="--hardware"))throw std::runtime_error("Usage: SkinDualUVShaderTests [--hardware]");
    static_assert(std::size(kSkinDualUVCases)==192&&sizeof(kSkinDualUVCases[0].input)==160);
    const bool hardware=argc==2;Fixture fixture(hardware);std::array<unsigned,4> counts{};
    unsigned index=0;
    for(const auto& c:kSkinDualUVCases){
        if(c.stage>=counts.size())throw std::runtime_error("Unknown skin dual UV numerical oracle stage");
        ++counts[c.stage];
        if(c.stage<2)fixture.vertices(c,index);else {fixture.pixels(c,index);fixture.pixels(c,index,true);}
        ++index;
    }
    if(counts!=std::array<unsigned,4>{32,32,64,64})throw std::runtime_error("Missing skin dual UV numerical oracle stage cases");
    std::printf("PASS original skin dual UV numerical GPU oracle (%s): %u bone/morph/animated UV/rim/two-material cases\n",
        hardware?"hardware":"WARP",index);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL skin dual UV shader GPU: %s\n",e.what());return 1;}}
