#include "renderer/particle_draw.h"
#include "VSParticle.h"
#include "VSParticleType5.h"
#include "GSParticleProbe.h"
#include "VSParticleProjectedProbe.h"
#include "PSParticle.h"
#include "PSParticleDual.h"
#include "PSParticleProjected.h"
#include "PSParticleDualProjected.h"
#include "particle_family_fixtures.h"
#include <d3d11sdklayers.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Simpsons::Graphics;
namespace {
void need(bool ok,const char* why){if(!ok)throw Error(why);}
void check(HRESULT hr){if(FAILED(hr))throw Error("Particle family numerical GPU fixture failed");}
ComPtr<ID3D11Buffer> buffer(ID3D11Device* device,UINT bytes,UINT bind,bool staging=false){
    D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.BindFlags=bind;
    d.Usage=staging?D3D11_USAGE_STAGING:D3D11_USAGE_DEFAULT;d.CPUAccessFlags=staging?D3D11_CPU_ACCESS_READ:0;
    ComPtr<ID3D11Buffer> result;check(device->CreateBuffer(&d,nullptr,&result));return result;
}
void compare(float actual,float expected,unsigned stage,size_t index,unsigned lane){
    if(!std::isfinite(actual)||!std::isfinite(expected)||
       std::abs(actual-expected)>4e-5f+4e-5f*std::abs(expected)){
        std::fprintf(stderr,"particle stage=%u case=%zu lane=%u actual=%.9g expected=%.9g\n",stage,index,lane,actual,expected);
        throw Error("Particle GPU output differs from independent original instruction oracle");
    }
}
struct Sampled {ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;};
Sampled sampled(ID3D11Device* device,UINT width,UINT height,DXGI_FORMAT format,const void* pixels,UINT pitch){
    D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=1;d.SampleDesc.Count=1;
    d.Format=format;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA initial{pixels,pitch,0};
    Sampled result;check(device->CreateTexture2D(&d,&initial,&result.texture));
    check(device->CreateShaderResourceView(result.texture.Get(),nullptr,&result.view));return result;
}
}

int main(int argc,char** argv){try{
    const bool hardware=argc==2&&std::strcmp(argv[1],"--hardware")==0;
    need(argc==1||hardware,"Usage: ParticleFamilyShaderTests [--hardware]");
    static_assert(std::size(kParticleVertexCases)==64&&std::size(kParticlePixelCases)==1536);
    static_assert(sizeof(ParticleVertex)==sizeof(kParticleVertexCases[0].input));
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL feature;
    const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};
    auto hr=D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,
        D3D11_CREATE_DEVICE_DEBUG,levels,1,D3D11_SDK_VERSION,&device,&feature,&context);
    if(FAILED(hr))check(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,
        0,levels,1,D3D11_SDK_VERSION,&device,&feature,&context));
    ComPtr<ID3D11VertexShader> vertices[2];ComPtr<ID3D11PixelShader> pixels[4];
    check(device->CreateVertexShader(kVSParticle,sizeof(kVSParticle),nullptr,&vertices[0]));
    check(device->CreateVertexShader(kVSParticleType5,sizeof(kVSParticleType5),nullptr,&vertices[1]));
    check(device->CreatePixelShader(kPSParticle,sizeof(kPSParticle),nullptr,&pixels[0]));
    check(device->CreatePixelShader(kPSParticleDual,sizeof(kPSParticleDual),nullptr,&pixels[1]));
    check(device->CreatePixelShader(kPSParticleProjected,sizeof(kPSParticleProjected),nullptr,&pixels[2]));
    check(device->CreatePixelShader(kPSParticleDualProjected,sizeof(kPSParticleDualProjected),nullptr,&pixels[3]));
    const D3D11_INPUT_ELEMENT_DESC elements[]={
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",3,DXGI_FORMAT_R32G32B32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",4,DXGI_FORMAT_R32G32B32A32_FLOAT,0,60,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",5,DXGI_FORMAT_R32_FLOAT,0,76,D3D11_INPUT_PER_VERTEX_DATA,0}};
    ComPtr<ID3D11InputLayout> layout;check(device->CreateInputLayout(elements,6,kVSParticle,sizeof(kVSParticle),&layout));
    const D3D11_SO_DECLARATION_ENTRY entries[]={{0,"SV_POSITION",0,0,4,0},{0,"TEXCOORD",0,0,4,0},
        {0,"TEXCOORD",1,0,4,0},{0,"TEXCOORD",2,0,4,0}};
    const UINT streamStride=64;ComPtr<ID3D11GeometryShader> stream;
    check(device->CreateGeometryShaderWithStreamOutput(kGSParticleProbe,sizeof(kGSParticleProbe),entries,4,
        &streamStride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&stream));
    auto input=buffer(device.Get(),sizeof(ParticleVertex),D3D11_BIND_VERTEX_BUFFER);
    auto constants=buffer(device.Get(),sizeof(ParticleConstants),D3D11_BIND_CONSTANT_BUFFER);
    auto output=buffer(device.Get(),64,D3D11_BIND_STREAM_OUTPUT);auto readback=buffer(device.Get(),64,0,true);
    context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    auto* vb=input.Get();UINT stride=sizeof(ParticleVertex),offset=0;context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
    auto* cb=constants.Get();context->VSSetConstantBuffers(0,1,&cb);context->GSSetShader(stream.Get(),nullptr,0);
    size_t checks=0;unsigned vcount[2]{};
    for(size_t i=0;i<std::size(kParticleVertexCases);++i){const auto& c=kParticleVertexCases[i];
        need(c.stage<2,"Particle vertex fixture stage is invalid");++vcount[c.stage];
        context->UpdateSubresource(input.Get(),0,nullptr,c.input,0,0);
        context->UpdateSubresource(constants.Get(),0,nullptr,c.constants,0,0);
        context->VSSetShader(vertices[c.stage].Get(),nullptr,0);auto* so=output.Get();const UINT zero=0;
        context->SOSetTargets(1,&so,&zero);context->Draw(1,0);context->SOSetTargets(0,nullptr,nullptr);
        context->CopyResource(readback.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE map{};
        check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&map));
        for(unsigned lane=0;lane<16;++lane){compare(static_cast<const float*>(map.pData)[lane],c.expected[lane],c.stage,i,lane);++checks;}
        context->Unmap(readback.Get(),0);
    }
    need(vcount[0]==32&&vcount[1]==32,"Particle vertex coverage changed");
    context->GSSetShader(nullptr,nullptr,0);context->IASetInputLayout(nullptr);
    vb=nullptr;stride=offset=0;context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
    ComPtr<ID3D11VertexShader> probe;check(device->CreateVertexShader(kVSParticleProjectedProbe,sizeof(kVSParticleProjectedProbe),nullptr,&probe));
    context->VSSetShader(probe.Get(),nullptr,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto probeConstants=buffer(device.Get(),48,D3D11_BIND_CONSTANT_BUFFER);cb=probeConstants.Get();context->VSSetConstantBuffers(3,1,&cb);
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;
    td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> color,colorReadback;ComPtr<ID3D11RenderTargetView> target;
    check(device->CreateTexture2D(&td,nullptr,&color));check(device->CreateRenderTargetView(color.Get(),nullptr,&target));
    td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    check(device->CreateTexture2D(&td,nullptr,&colorReadback));
    auto base=sampled(device.Get(),4,4,DXGI_FORMAT_R32G32B32A32_FLOAT,kParticleBaseTexture,64);
    auto secondary=sampled(device.Get(),4,4,DXGI_FORMAT_R32G32B32A32_FLOAT,kParticleSecondaryTexture,64);
    std::vector<float> depths(1024*1024,.25f);auto shadow=sampled(device.Get(),1024,1024,DXGI_FORMAT_R32_FLOAT,depths.data(),4096);
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sd.MaxLOD=13;sd.MaxAnisotropy=1;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;
    ComPtr<ID3D11SamplerState> samples[3];check(device->CreateSamplerState(&sd,&samples[0]));
    sd.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;check(device->CreateSamplerState(&sd,&samples[1]));
    sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=D3D11_TEXTURE_ADDRESS_MIRROR;
    check(device->CreateSamplerState(&sd,&samples[2]));
    for(unsigned i=0;i<3;++i){auto* raw=samples[i].Get();context->PSSetSamplers(i,1,&raw);}
    auto* resource=base.view.Get();context->PSSetShaderResources(0,1,&resource);
    resource=secondary.view.Get();context->PSSetShaderResources(3,1,&resource);
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
    ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster));context->RSSetState(raster.Get());
    const D3D11_VIEWPORT viewport{0,0,1,1,0,1};context->RSSetViewports(1,&viewport);
    unsigned previousMask=~0u,pcount[4]{};
    for(size_t i=0;i<std::size(kParticlePixelCases);++i){const auto& c=kParticlePixelCases[i];
        need(c.stage<4&&c.shadowMask<16,"Particle pixel fixture stage/mask is invalid");++pcount[c.stage];
        if(previousMask!=c.shadowMask){
            for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)
                depths[(511+y)*1024+511+x]=(c.shadowMask&(1u<<(y*2+x)))?.75f:.25f;
            ID3D11ShaderResourceView* none=nullptr;context->PSSetShaderResources(2,1,&none);
            context->UpdateSubresource(shadow.texture.Get(),0,nullptr,depths.data(),4096,0);
            resource=shadow.view.Get();context->PSSetShaderResources(2,1,&resource);previousMask=c.shadowMask;
        }
        context->UpdateSubresource(probeConstants.Get(),0,nullptr,c.input,0,0);
        context->PSSetShader(pixels[c.stage].Get(),nullptr,0);auto* rt=target.Get();context->OMSetRenderTargets(1,&rt,nullptr);
        context->Draw(3,0);context->OMSetRenderTargets(0,nullptr,nullptr);context->CopyResource(colorReadback.Get(),color.Get());
        D3D11_MAPPED_SUBRESOURCE map{};check(context->Map(colorReadback.Get(),0,D3D11_MAP_READ,0,&map));
        for(unsigned lane=0;lane<4;++lane){compare(static_cast<const float*>(map.pData)[lane],c.expected[lane],2+c.stage,i,lane);++checks;}
        context->Unmap(colorReadback.Get(),0);
    }
    for(unsigned n:pcount)need(n==384,"Particle pixel coverage changed");
    ComPtr<ID3D11InfoQueue> queue;device->QueryInterface(IID_PPV_ARGS(&queue));
    if(queue)for(UINT64 i=0;i<queue->GetNumStoredMessages();++i){SIZE_T n=0;queue->GetMessage(i,nullptr,&n);
        std::vector<uint8_t> bytes(n);auto* message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());queue->GetMessage(i,message,&n);
        if(message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"D3D11: %s\n",message->pDescription);throw Error("Particle numerical D3D11 validation error");}}
    std::printf("PASS particle family %s:64 original VS cases,1536 original PS cases,%zu checks;both VS,4 PS,dual UV flag,RRRR shadows and two textures\n",hardware?"hardware":"WARP",checks);
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Particle family GPU failure: %s\n",e.what());return 1;}}
