// Native GPU versus an independent original-microcode numerical oracle.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>
#include "flipbook_fixtures.h"
#include "VSFlipbook.h"
#include "VSFlipbookAlpha.h"
#include "PSFlipbook.h"
#include "PSFlipbookAlpha.h"
#include "PSFlipbookDraw.h"
#include "PSFlipbookAlphaDraw.h"
#include "VSFlipbookPixelProbe.h"
#include "VSFlipbookAlphaPixelProbe.h"
#include "GSFlipbookProbe.h"
#include "GSFlipbookAlphaProbe.h"
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr,const char* what) {
    if(FAILED(hr)){char message[180];sprintf_s(message,"%s: %08lX",what,ULONG(hr));throw std::runtime_error(message);}
}
void compare(float actual,float expected,unsigned sample,unsigned lane) {
    if(!std::isfinite(actual)||!std::isfinite(expected)||std::abs(actual-expected)>.00004f+.00004f*std::abs(expected)) {
        char message[200];sprintf_s(message,"Flipbook oracle case%u lane%u actual%.9g expected%.9g",sample,lane,actual,expected);
        throw std::runtime_error(message);
    }
}
struct Fixture {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11ShaderResourceView> textureView;
    ComPtr<ID3D11SamplerState> sampler;
    explicit Fixture(bool hardware) {
        D3D_FEATURE_LEVEL level{};
        check(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&device,&level,&context),"Flipbook oracle device");
        {
            D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=4;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
            d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            const D3D11_SUBRESOURCE_DATA data{kFlipbookTexture,4*16,0};ComPtr<ID3D11Texture2D> texture;
            check(device->CreateTexture2D(&d,&data,&texture),"Flipbook oracle texture");
            check(device->CreateShaderResourceView(texture.Get(),nullptr,&textureView),"Flipbook oracle SRV");
        }
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;s.MaxAnisotropy=1;
        s.ComparisonFunc=D3D11_COMPARISON_NEVER;s.MaxLOD=13;
        check(device->CreateSamplerState(&s,&sampler),"Flipbook oracle sampler");
    }
    ComPtr<ID3D11Buffer> buffer(const void* values,UINT bytes,UINT binding,D3D11_USAGE usage=D3D11_USAGE_IMMUTABLE) {
        D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.BindFlags=binding;d.Usage=usage;
        const D3D11_SUBRESOURCE_DATA data{values,0,0};ComPtr<ID3D11Buffer> result;
        check(device->CreateBuffer(&d,values?&data:nullptr,&result),"Flipbook oracle buffer");return result;
    }
    void vertices(const FlipbookCase& c,unsigned index) {
        const bool alpha=c.stage==1;const auto* vsBytes=alpha?kVSFlipbookAlpha:kVSFlipbook;
        const size_t vsSize=alpha?sizeof(kVSFlipbookAlpha):sizeof(kVSFlipbook);
        ComPtr<ID3D11VertexShader> vs;check(device->CreateVertexShader(vsBytes,vsSize,nullptr,&vs),"Flipbook oracle VS");
        const D3D11_INPUT_ELEMENT_DESC elements[]={
            {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",3,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",4,DXGI_FORMAT_R32G32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0}};
        ComPtr<ID3D11InputLayout> layout;check(device->CreateInputLayout(elements,4,vsBytes,vsSize,&layout),"Flipbook oracle layout");
        const D3D11_SO_DECLARATION_ENTRY opaqueEntries[]={
            {0,"SV_POSITION",0,0,4,0},{0,"TEXCOORD",0,0,2,0},
            {0,"TEXCOORD",1,0,3,0},{0,"TEXCOORD",2,0,4,0}};
        const D3D11_SO_DECLARATION_ENTRY alphaEntries[]={
            {0,"SV_POSITION",0,0,4,0},{0,"TEXCOORD",0,0,2,0},
            {0,"TEXCOORD",1,0,4,0},{0,"TEXCOORD",2,0,3,0},{0,"TEXCOORD",3,0,4,0}};
        const UINT lanes=alpha?17u:13u,stride=lanes*4;
        ComPtr<ID3D11GeometryShader> gs;
        check(device->CreateGeometryShaderWithStreamOutput(alpha?kGSFlipbookAlphaProbe:kGSFlipbookProbe,
            alpha?sizeof(kGSFlipbookAlphaProbe):sizeof(kGSFlipbookProbe),alpha?alphaEntries:opaqueEntries,alpha?5:4,
            &stride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&gs),"Flipbook oracle SO");
        auto vb=buffer(c.input,56,D3D11_BIND_VERTEX_BUFFER);auto cb=buffer(c.constants,48*16,D3D11_BIND_CONSTANT_BUFFER);
        auto out=buffer(nullptr,stride,D3D11_BIND_STREAM_OUTPUT,D3D11_USAGE_DEFAULT);
        D3D11_BUFFER_DESC readDesc{};out->GetDesc(&readDesc);readDesc.Usage=D3D11_USAGE_STAGING;readDesc.BindFlags=0;readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> read;check(device->CreateBuffer(&readDesc,nullptr,&read),"Flipbook oracle SO staging");
        auto* vertices=vb.Get();const UINT vbStride=56,offset=0;context->IASetVertexBuffers(0,1,&vertices,&vbStride,&offset);
        context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        context->VSSetShader(vs.Get(),nullptr,0);auto* constants=cb.Get();context->VSSetConstantBuffers(0,1,&constants);
        context->GSSetShader(gs.Get(),nullptr,0);context->PSSetShader(nullptr,nullptr,0);
        auto* output=out.Get();context->SOSetTargets(1,&output,&offset);context->Draw(1,0);context->SOSetTargets(0,nullptr,nullptr);
        context->CopyResource(read.Get(),out.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped),"Flipbook oracle SO read");
        const auto* values=static_cast<const float*>(mapped.pData);
        for(unsigned lane=0;lane<lanes;++lane)compare(values[lane],c.expected[lane],index,lane);
        context->Unmap(read.Get(),0);context->GSSetShader(nullptr,nullptr,0);
    }
    void pixels(const FlipbookCase& c,unsigned index,bool drawAdapter=false) {
        const bool alpha=c.stage==3;
        ComPtr<ID3D11VertexShader> vs;check(device->CreateVertexShader(alpha?kVSFlipbookAlphaPixelProbe:kVSFlipbookPixelProbe,
            alpha?sizeof(kVSFlipbookAlphaPixelProbe):sizeof(kVSFlipbookPixelProbe),nullptr,&vs),"Flipbook pixel transport");
        const auto* psBytes=drawAdapter?(alpha?kPSFlipbookAlphaDraw:kPSFlipbookDraw):(alpha?kPSFlipbookAlpha:kPSFlipbook);
        const size_t psSize=drawAdapter?(alpha?sizeof(kPSFlipbookAlphaDraw):sizeof(kPSFlipbookDraw)):
            (alpha?sizeof(kPSFlipbookAlpha):sizeof(kPSFlipbook));
        ComPtr<ID3D11PixelShader> ps;check(device->CreatePixelShader(psBytes,psSize,nullptr,&ps),"Flipbook original PS/draw adapter");
        float probes[4][4]{};std::memcpy(probes[0],c.input,8);
        if(alpha) {std::memcpy(probes[1],c.input+2,16);std::memcpy(probes[2],c.input+6,12);std::memcpy(probes[3],c.input+9,16);}
        else {std::memcpy(probes[1],c.input+2,12);std::memcpy(probes[2],c.input+5,16);}
        auto cb=buffer(c.constants,51*16,D3D11_BIND_CONSTANT_BUFFER);auto probe=buffer(probes,sizeof(probes),D3D11_BIND_CONSTANT_BUFFER);
        const std::array<uint32_t,4> depthValues{};auto depthConstants=buffer(depthValues.data(),16,D3D11_BIND_CONSTANT_BUFFER);
        D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
        d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> target;
        check(device->CreateTexture2D(&d,nullptr,&target),"Flipbook pixel target");ComPtr<ID3D11RenderTargetView> rtv;
        check(device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"Flipbook pixel RTV");
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> read;
        check(device->CreateTexture2D(&d,nullptr,&read),"Flipbook pixel staging");
        auto* view=rtv.Get();context->OMSetRenderTargets(1,&view,nullptr);
        const D3D11_VIEWPORT viewport{0,0,1,1,0,1};context->RSSetViewports(1,&viewport);
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster),"Flipbook pixel raster");context->RSSetState(raster.Get());
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);context->GSSetShader(nullptr,nullptr,0);
        auto* constants=cb.Get();context->PSSetConstantBuffers(0,1,&constants);auto* inputs=probe.Get();context->VSSetConstantBuffers(2,1,&inputs);
        auto* depthState=depthConstants.Get();context->PSSetConstantBuffers(1,1,&depthState);
        auto* texture=textureView.Get();context->PSSetShaderResources(0,1,&texture);
        auto* state=sampler.Get();context->PSSetSamplers(0,1,&state);
        const float clear[4]={-9,-8,-7,-6};context->ClearRenderTargetView(view,clear);
        context->Draw(3,0);context->CopyResource(read.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped),"Flipbook pixel read");
        const auto* values=static_cast<const float*>(mapped.pData);
        for(unsigned lane=0;lane<4;++lane)compare(values[lane],c.expected[lane],index,lane);
        context->Unmap(read.Get(),0);context->OMSetRenderTargets(0,nullptr,nullptr);
    }
};
}
int main(int argc,char** argv) {
    try {
        const bool hardware=argc==2&&std::string(argv[1])=="--hardware";Fixture fixture(hardware);
        static_assert(std::size(kFlipbookCases)==160);
        unsigned index=0;std::array<unsigned,4> stages{};
        for(const auto& c:kFlipbookCases){
            if(c.stage>=stages.size())throw std::runtime_error("Unknown Flipbook oracle stage");
            ++stages[c.stage];
            if(c.stage<2)fixture.vertices(c,index);
            else {fixture.pixels(c,index);fixture.pixels(c,index,true);}++index;
        }
        if(stages!=std::array<unsigned,4>{32,32,48,48})
            throw std::runtime_error("Flipbook oracle coverage differs from the original stage contract");
        std::printf("PASS original flipbook numerical GPU oracle (%s): %u atlas animation/loop/clamp and material/rim/alpha cases plus96 production depth adapter comparisons\n",hardware?"hardware":"WARP",index);
        return 0;
    } catch(const std::exception& e){std::fprintf(stderr,"FAIL Flipbook shader GPU: %s\n",e.what());return 1;}
}
