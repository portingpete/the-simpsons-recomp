// Native GPU versus an independent original-microcode numerical oracle.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include "rigid_dualtextured_uv_fixtures.h"
#include "VSRigidDualTexturedUV.h"
#include "VSRigidDualTexturedUVAlpha.h"
#include "PSRigidDualTexturedUV.h"
#include "PSRigidDualTexturedUVAlpha.h"
#include "VSRigidDualTexturedUVPixelProbe.h"
#include "VSRigidDualTexturedUVAlphaPixelProbe.h"
#include "GSRigidDualTexturedUVProbe.h"
#include "GSRigidDualTexturedUVAlphaProbe.h"
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr,const char* what) {
    if(FAILED(hr)){char message[180];sprintf_s(message,"%s: %08lX",what,ULONG(hr));throw std::runtime_error(message);}
}
void compare(float actual,float expected,unsigned sample,unsigned lane) {
    if(!std::isfinite(actual)||std::abs(actual-expected)>.00004f+.00004f*std::abs(expected)) {
        char message[200];sprintf_s(message,"UV oracle case%u lane%u actual%.9g expected%.9g",sample,lane,actual,expected);
        throw std::runtime_error(message);
    }
}
struct Fixture {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    std::array<ComPtr<ID3D11ShaderResourceView>,3> textures;
    ComPtr<ID3D11SamplerState> sampler;
    explicit Fixture(bool hardware) {
        D3D_FEATURE_LEVEL level{};
        check(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&device,&level,&context),"UV oracle device");
        for(unsigned bank=0;bank<3;++bank) {
            D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=4;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
            d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            const D3D11_SUBRESOURCE_DATA data{kUVTextures[bank],4*16,0};ComPtr<ID3D11Texture2D> texture;
            check(device->CreateTexture2D(&d,&data,&texture),"UV oracle texture");
            check(device->CreateShaderResourceView(texture.Get(),nullptr,&textures[bank]),"UV oracle SRV");
        }
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;s.MaxAnisotropy=1;
        s.ComparisonFunc=D3D11_COMPARISON_NEVER;s.MaxLOD=13;
        check(device->CreateSamplerState(&s,&sampler),"UV oracle sampler");
    }
    ComPtr<ID3D11Buffer> buffer(const void* values,UINT bytes,UINT binding,D3D11_USAGE usage=D3D11_USAGE_IMMUTABLE) {
        D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.BindFlags=binding;d.Usage=usage;
        const D3D11_SUBRESOURCE_DATA data{values,0,0};ComPtr<ID3D11Buffer> result;
        check(device->CreateBuffer(&d,values?&data:nullptr,&result),"UV oracle buffer");return result;
    }
    void vertices(const UVCase& c,unsigned index) {
        const bool alpha=c.stage==1;const auto* vsBytes=alpha?kVSRigidDualTexturedUVAlpha:kVSRigidDualTexturedUV;
        const size_t vsSize=alpha?sizeof(kVSRigidDualTexturedUVAlpha):sizeof(kVSRigidDualTexturedUV);
        ComPtr<ID3D11VertexShader> vs;check(device->CreateVertexShader(vsBytes,vsSize,nullptr,&vs),"UV oracle VS");
        const D3D11_INPUT_ELEMENT_DESC elements[]={
            {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",3,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",4,DXGI_FORMAT_R32G32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0}};
        ComPtr<ID3D11InputLayout> layout;check(device->CreateInputLayout(elements,alpha?4:5,vsBytes,vsSize,&layout),"UV oracle layout");
        const D3D11_SO_DECLARATION_ENTRY entries[]={
            {0,"SV_POSITION",0,0,4,0},{0,"TEXCOORD",0,0,4,0},{0,"TEXCOORD",1,0,BYTE(alpha?4:2),0},
            {0,"TEXCOORD",2,0,4,0},{0,"TEXCOORD",3,0,3,0},{0,"TEXCOORD",4,0,4,0}};
        const UINT lanes=alpha?12u:21u,stride=lanes*4;
        ComPtr<ID3D11GeometryShader> gs;
        check(device->CreateGeometryShaderWithStreamOutput(alpha?kGSRigidDualTexturedUVAlphaProbe:kGSRigidDualTexturedUVProbe,
            alpha?sizeof(kGSRigidDualTexturedUVAlphaProbe):sizeof(kGSRigidDualTexturedUVProbe),entries,alpha?3:6,
            &stride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&gs),"UV oracle SO");
        auto vb=buffer(c.input,56,D3D11_BIND_VERTEX_BUFFER);auto cb=buffer(c.constants,48*16,D3D11_BIND_CONSTANT_BUFFER);
        auto out=buffer(nullptr,stride,D3D11_BIND_STREAM_OUTPUT,D3D11_USAGE_DEFAULT);
        D3D11_BUFFER_DESC readDesc{};out->GetDesc(&readDesc);readDesc.Usage=D3D11_USAGE_STAGING;readDesc.BindFlags=0;readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> read;check(device->CreateBuffer(&readDesc,nullptr,&read),"UV oracle SO staging");
        auto* vertices=vb.Get();const UINT vbStride=56,offset=0;context->IASetVertexBuffers(0,1,&vertices,&vbStride,&offset);
        context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        context->VSSetShader(vs.Get(),nullptr,0);auto* constants=cb.Get();context->VSSetConstantBuffers(0,1,&constants);
        context->GSSetShader(gs.Get(),nullptr,0);context->PSSetShader(nullptr,nullptr,0);
        auto* output=out.Get();context->SOSetTargets(1,&output,&offset);context->Draw(1,0);context->SOSetTargets(0,nullptr,nullptr);
        context->CopyResource(read.Get(),out.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped),"UV oracle SO read");
        const auto* values=static_cast<const float*>(mapped.pData);
        for(unsigned lane=0;lane<lanes;++lane)compare(values[lane],c.expected[lane],index,lane);
        context->Unmap(read.Get(),0);context->GSSetShader(nullptr,nullptr,0);
    }
    void pixels(const UVCase& c,unsigned index) {
        const bool alpha=c.stage==3;
        ComPtr<ID3D11VertexShader> vs;check(device->CreateVertexShader(alpha?kVSRigidDualTexturedUVAlphaPixelProbe:kVSRigidDualTexturedUVPixelProbe,
            alpha?sizeof(kVSRigidDualTexturedUVAlphaPixelProbe):sizeof(kVSRigidDualTexturedUVPixelProbe),nullptr,&vs),"UV pixel transport");
        ComPtr<ID3D11PixelShader> ps;check(device->CreatePixelShader(alpha?kPSRigidDualTexturedUVAlpha:kPSRigidDualTexturedUV,
            alpha?sizeof(kPSRigidDualTexturedUVAlpha):sizeof(kPSRigidDualTexturedUV),nullptr,&ps),"UV original PS");
        float probes[5][4]{};std::memcpy(probes[0],c.input,16);
        if(alpha)std::memcpy(probes[1],c.input+4,16);
        else {std::memcpy(probes[1],c.input+4,8);std::memcpy(probes[2],c.input+6,16);
            std::memcpy(probes[3],c.input+10,12);std::memcpy(probes[4],c.input+13,16);}
        auto cb=buffer(c.constants,51*16,D3D11_BIND_CONSTANT_BUFFER);auto probe=buffer(probes,sizeof(probes),D3D11_BIND_CONSTANT_BUFFER);
        D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
        d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> target;
        check(device->CreateTexture2D(&d,nullptr,&target),"UV pixel target");ComPtr<ID3D11RenderTargetView> rtv;
        check(device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"UV pixel RTV");
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> read;
        check(device->CreateTexture2D(&d,nullptr,&read),"UV pixel staging");
        auto* view=rtv.Get();context->OMSetRenderTargets(1,&view,nullptr);
        const D3D11_VIEWPORT viewport{0,0,1,1,0,1};context->RSSetViewports(1,&viewport);
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster),"UV pixel raster");context->RSSetState(raster.Get());
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);context->GSSetShader(nullptr,nullptr,0);
        auto* constants=cb.Get();context->PSSetConstantBuffers(0,1,&constants);auto* inputs=probe.Get();context->VSSetConstantBuffers(2,1,&inputs);
        ID3D11ShaderResourceView* views[]={textures[0].Get(),textures[1].Get(),textures[2].Get()};context->PSSetShaderResources(0,3,views);
        ID3D11SamplerState* states[]={sampler.Get(),sampler.Get(),sampler.Get()};context->PSSetSamplers(0,3,states);
        context->Draw(3,0);context->CopyResource(read.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped),"UV pixel read");
        const auto* values=static_cast<const float*>(mapped.pData);
        for(unsigned lane=0;lane<4;++lane)compare(values[lane],c.expected[lane],index,lane);
        context->Unmap(read.Get(),0);context->OMSetRenderTargets(0,nullptr,nullptr);
    }
};
}
int main(int argc,char** argv) {
    try {
        const bool hardware=argc==2&&std::string(argv[1])=="--hardware";Fixture fixture(hardware);
        unsigned index=0;for(const auto& c:kUVCases){if(c.stage<2)fixture.vertices(c,index);else fixture.pixels(c,index);++index;}
        std::printf("PASS original UV numerical GPU oracle (%s): %u wave/UV and material/shadow branch cases\n",hardware?"hardware":"WARP",index);
        return 0;
    } catch(const std::exception& e){std::fprintf(stderr,"FAIL UV shader GPU: %s\n",e.what());return 1;}
}
