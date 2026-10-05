// Transport only: expected values come from the independent retail word interpreter.
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
#include <iterator>
#include <stdexcept>
#include <string>
#include "rigid_remaining_fixtures.h"
#include "VSChocolateOpaque.h"
#include "PSChocolateOpaque.h"
#include "PSChocolateOpaqueDraw.h"
#include "GSChocolateOpaqueProbe.h"
#include "VSChocolateOpaquePixelProbe.h"
#include "VSChocolate.h"
#include "PSChocolate.h"
#include "PSChocolateDraw.h"
#include "GSChocolateAlphaFullProbe.h"
#include "VSChocolateAlphaPixelProbe.h"
#include "VSProjtex.h"
#include "PSProjtex.h"
#include "PSProjtexDraw.h"
#include "GSProjtexProbe.h"
#include "VSProjtexPixelProbe.h"
#include "VSProjtexAlpha.h"
#include "PSProjtexAlpha.h"
#include "PSProjtexAlphaDraw.h"
#include "GSProjtexAlphaProbe.h"
#include "VSProjtexAlphaPixelProbe.h"
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr,const char* what) {
    if(FAILED(hr)){char m[180];sprintf_s(m,"%s: %08lX",what,ULONG(hr));throw std::runtime_error(m);}
}
void compare(float actual,float expected,unsigned sample,unsigned lane) {
    if(!std::isfinite(actual)||!std::isfinite(expected)||std::abs(actual-expected)>.00004f+.00004f*std::abs(expected)) {
        char m[200];sprintf_s(m,"Remaining shader oracle case%u lane%u actual%.9g expected%.9g",sample,lane,actual,expected);
        throw std::runtime_error(m);
    }
}
struct Bytes {const void* data;size_t size;};
const Bytes vertexBytes[]={{kVSChocolateOpaque,sizeof(kVSChocolateOpaque)},{kVSChocolate,sizeof(kVSChocolate)},
    {kVSProjtex,sizeof(kVSProjtex)},{kVSProjtexAlpha,sizeof(kVSProjtexAlpha)}};
const Bytes geometryBytes[]={{kGSChocolateOpaqueProbe,sizeof(kGSChocolateOpaqueProbe)},{kGSChocolateAlphaFullProbe,sizeof(kGSChocolateAlphaFullProbe)},
    {kGSProjtexProbe,sizeof(kGSProjtexProbe)},{kGSProjtexAlphaProbe,sizeof(kGSProjtexAlphaProbe)}};
const Bytes probeBytes[]={{kVSChocolateOpaquePixelProbe,sizeof(kVSChocolateOpaquePixelProbe)},{kVSChocolateAlphaPixelProbe,sizeof(kVSChocolateAlphaPixelProbe)},
    {kVSProjtexPixelProbe,sizeof(kVSProjtexPixelProbe)},{kVSProjtexAlphaPixelProbe,sizeof(kVSProjtexAlphaPixelProbe)}};
const Bytes pixelBytes[]={{kPSChocolateOpaque,sizeof(kPSChocolateOpaque)},{kPSChocolate,sizeof(kPSChocolate)},
    {kPSProjtex,sizeof(kPSProjtex)},{kPSProjtexAlpha,sizeof(kPSProjtexAlpha)}};
const Bytes adapterBytes[]={{kPSChocolateOpaqueDraw,sizeof(kPSChocolateOpaqueDraw)},{kPSChocolateDraw,sizeof(kPSChocolateDraw)},
    {kPSProjtexDraw,sizeof(kPSProjtexDraw)},{kPSProjtexAlphaDraw,sizeof(kPSProjtexAlphaDraw)}};
constexpr unsigned widths[4][7]={{4,4,4,3,4,4,4},{4,4,3,4,4,4,0},{2,4,4,3,2,4,0},{2,4,3,4,0,0,0}};
constexpr unsigned outputs[4]={31,27,23,17};
struct Fixture {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    std::array<ComPtr<ID3D11ShaderResourceView>,4> textures;ComPtr<ID3D11SamplerState> sampler;
    explicit Fixture(bool hardware) {
        D3D_FEATURE_LEVEL level{};
        check(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&device,&level,&context),"Remaining oracle device");
        for(unsigned bank=0;bank<4;++bank) {
            D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=4;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
            d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            const D3D11_SUBRESOURCE_DATA data{kRemainingTextures[bank],4*16,0};ComPtr<ID3D11Texture2D> texture;
            check(device->CreateTexture2D(&d,&data,&texture),"Remaining oracle texture");
            check(device->CreateShaderResourceView(texture.Get(),nullptr,&textures[bank]),"Remaining oracle SRV");
        }
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;s.MaxAnisotropy=1;
        s.ComparisonFunc=D3D11_COMPARISON_NEVER;s.MaxLOD=13;
        check(device->CreateSamplerState(&s,&sampler),"Remaining oracle sampler");
    }
    ComPtr<ID3D11Buffer> buffer(const void* values,UINT bytes,UINT binding,D3D11_USAGE usage=D3D11_USAGE_IMMUTABLE) {
        D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.BindFlags=binding;d.Usage=usage;
        const D3D11_SUBRESOURCE_DATA data{values,0,0};ComPtr<ID3D11Buffer> result;
        check(device->CreateBuffer(&d,values?&data:nullptr,&result),"Remaining oracle buffer");return result;
    }
    void vertices(const RemainingCase& c,unsigned index) {
        const unsigned family=c.stage;const auto bytes=vertexBytes[family];
        ComPtr<ID3D11VertexShader> vs;check(device->CreateVertexShader(bytes.data,bytes.size,nullptr,&vs),"Remaining oracle VS");
        const D3D11_INPUT_ELEMENT_DESC elements[]={
            {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",3,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",4,DXGI_FORMAT_R32G32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",5,DXGI_FORMAT_R32G32B32_FLOAT,0,56,D3D11_INPUT_PER_VERTEX_DATA,0}};
        constexpr unsigned originalVS[]={0x8205DABC,0x8205E0B8,0x82051EEC,0x82052358};
        char layoutDiagnostic[160];sprintf_s(layoutDiagnostic,
            "Remaining oracle layout case%u stage%u originalVS%08X attributes%u",index,family,originalVS[family],family<2?6:4);
        ComPtr<ID3D11InputLayout> layout;check(device->CreateInputLayout(elements,family<2?6:4,bytes.data,bytes.size,&layout),layoutDiagnostic);
        std::array<D3D11_SO_DECLARATION_ENTRY,8> entries{};entries[0]={0,"SV_POSITION",0,0,4,0};unsigned count=1;
        for(unsigned i=0;i<7&&widths[family][i];++i)entries[count++]={0,"TEXCOORD",i,0,BYTE(widths[family][i]),0};
        const UINT stride=outputs[family]*4;ComPtr<ID3D11GeometryShader> gs;const auto gb=geometryBytes[family];
        check(device->CreateGeometryShaderWithStreamOutput(gb.data,gb.size,entries.data(),count,&stride,1,
            D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&gs),"Remaining oracle SO");
        auto vb=buffer(c.input,68,D3D11_BIND_VERTEX_BUFFER);auto cb=buffer(c.constants,48*16,D3D11_BIND_CONSTANT_BUFFER);
        auto out=buffer(nullptr,stride,D3D11_BIND_STREAM_OUTPUT,D3D11_USAGE_DEFAULT);
        D3D11_BUFFER_DESC d{};out->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> read;check(device->CreateBuffer(&d,nullptr,&read),"Remaining oracle SO staging");
        auto* vertices=vb.Get();const UINT vbStride=68,offset=0;context->IASetVertexBuffers(0,1,&vertices,&vbStride,&offset);
        context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        context->VSSetShader(vs.Get(),nullptr,0);auto* constants=cb.Get();context->VSSetConstantBuffers(0,1,&constants);
        context->GSSetShader(gs.Get(),nullptr,0);context->PSSetShader(nullptr,nullptr,0);
        auto* output=out.Get();context->SOSetTargets(1,&output,&offset);context->Draw(1,0);context->SOSetTargets(0,nullptr,nullptr);
        context->CopyResource(read.Get(),out.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped),"Remaining oracle SO read");
        const auto* values=static_cast<const float*>(mapped.pData);
        for(unsigned lane=0;lane<outputs[family];++lane)compare(values[lane],c.expected[lane],index,lane);
        context->Unmap(read.Get(),0);context->GSSetShader(nullptr,nullptr,0);
    }
    void pixels(const RemainingCase& c,unsigned index,bool adapter) {
        const unsigned family=c.stage-4;const auto pb=probeBytes[family],sb=adapter?adapterBytes[family]:pixelBytes[family];
        ComPtr<ID3D11VertexShader> vs;check(device->CreateVertexShader(pb.data,pb.size,nullptr,&vs),"Remaining pixel transport");
        ComPtr<ID3D11PixelShader> ps;check(device->CreatePixelShader(sb.data,sb.size,nullptr,&ps),"Remaining original PS/adapter");
        float probes[7][4]{};unsigned at=0;
        for(unsigned i=0;i<7&&widths[family][i];++i){std::memcpy(probes[i],c.input+at,widths[family][i]*4);at+=widths[family][i];}
        auto cb=buffer(c.constants,51*16,D3D11_BIND_CONSTANT_BUFFER),probe=buffer(probes,sizeof(probes),D3D11_BIND_CONSTANT_BUFFER);
        const std::array<unsigned,4> depth{};auto dc=buffer(depth.data(),16,D3D11_BIND_CONSTANT_BUFFER);
        D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
        d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target;check(device->CreateTexture2D(&d,nullptr,&target),"Remaining pixel target");
        ComPtr<ID3D11RenderTargetView> rtv;check(device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"Remaining pixel RTV");
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> read;
        check(device->CreateTexture2D(&d,nullptr,&read),"Remaining pixel staging");
        auto* view=rtv.Get();context->OMSetRenderTargets(1,&view,nullptr);const D3D11_VIEWPORT viewport{0,0,1,1,0,1};context->RSSetViewports(1,&viewport);
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster),"Remaining pixel raster");context->RSSetState(raster.Get());
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);context->GSSetShader(nullptr,nullptr,0);
        auto* constants=cb.Get();context->PSSetConstantBuffers(0,1,&constants);auto* inputs=probe.Get();context->VSSetConstantBuffers(2,1,&inputs);
        auto* depthState=dc.Get();context->PSSetConstantBuffers(1,1,&depthState);
        ID3D11ShaderResourceView* views[]={textures[0].Get(),textures[1].Get(),textures[2].Get(),textures[3].Get()};context->PSSetShaderResources(0,4,views);
        ID3D11SamplerState* states[]={sampler.Get(),sampler.Get(),sampler.Get(),sampler.Get()};context->PSSetSamplers(0,4,states);
        const float clear[4]={-9,-8,-7,-6};context->ClearRenderTargetView(view,clear);context->Draw(3,0);context->CopyResource(read.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped),"Remaining pixel read");
        const auto* values=static_cast<const float*>(mapped.pData);
        for(unsigned lane=0;lane<4;++lane)compare(values[lane],adapter?c.depthExpected[lane]:c.expected[lane],index,lane);
        context->Unmap(read.Get(),0);context->OMSetRenderTargets(0,nullptr,nullptr);
    }
};
}
int main(int argc,char** argv) {
    try {
        const bool hardware=argc==2&&std::string(argv[1])=="--hardware";Fixture fixture(hardware);
        static_assert(std::size(kRemainingCases)==384);unsigned index=0;std::array<unsigned,8> stages{};
        for(const auto& c:kRemainingCases){if(c.stage>=8)throw std::runtime_error("Unknown remaining shader stage");++stages[c.stage];
            if(c.stage<4)fixture.vertices(c,index);else{fixture.pixels(c,index,false);fixture.pixels(c,index,true);}++index;}
        if(stages!=std::array<unsigned,8>{32,32,32,32,64,64,64,64})throw std::runtime_error("Remaining original stage coverage differs");
        std::printf("PASS chocolate/projtex original instruction GPU oracle (%s):384 cases and256 production adapter comparisons\n",hardware?"hardware":"WARP");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL remaining shader GPU: %s\n",e.what());return 1;}
}
