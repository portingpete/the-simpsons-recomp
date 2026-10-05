#include "renderer/particle_draw.h"
#include "PSParticleProjected.h"
#include "VSParticleProjectedProbe.h"
#include <d3d11sdklayers.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Simpsons::Graphics;
namespace {
void need(bool value,const char* why){if(!value)throw Error(why);}
void check(HRESULT value){if(FAILED(value))throw Error("Projected particle GPU fixture failed");}
float fraction(float x){return x-std::floor(x);}
unsigned mirror(float x) {
    float q=std::fmod(x,2.0f);if(q<0)q+=2;if(q>1)q=2-q;
    return std::min(1023u,unsigned(std::floor(q*1024)));
}
}
int main(int argc,char** argv) {try {
    const bool hardware=argc==2&&std::strcmp(argv[1],"--hardware")==0;
    need(argc==1||hardware,"Usage: ParticleProjectedShaderTests [--hardware]");
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};
    auto hr=D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,
        D3D11_CREATE_DEVICE_DEBUG,levels,1,D3D11_SDK_VERSION,&device,&level,&context);
    if(FAILED(hr))check(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,
        0,levels,1,D3D11_SDK_VERSION,&device,&level,&context));
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    check(device->CreateVertexShader(kVSParticleProjectedProbe,sizeof(kVSParticleProjectedProbe),nullptr,&vs));
    check(device->CreatePixelShader(kPSParticleProjected,sizeof(kPSParticleProjected),nullptr,&ps));
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=4;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;
    td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> output,readback;ComPtr<ID3D11RenderTargetView> rt;
    check(device->CreateTexture2D(&td,nullptr,&output));check(device->CreateRenderTargetView(output.Get(),nullptr,&rt));
    td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    check(device->CreateTexture2D(&td,nullptr,&readback));
    td.Width=td.Height=1024;td.Format=DXGI_FORMAT_R32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;td.Usage=D3D11_USAGE_DEFAULT;td.CPUAccessFlags=0;
    std::vector<float> depth(1024*1024,.25f);D3D11_SUBRESOURCE_DATA initial{depth.data(),1024*4,0};
    ComPtr<ID3D11Texture2D> shadow;ComPtr<ID3D11ShaderResourceView> shadowView;
    check(device->CreateTexture2D(&td,&initial,&shadow));check(device->CreateShaderResourceView(shadow.Get(),nullptr,&shadowView));
    td.Width=td.Height=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    const std::array<float,4> base={.3f,.6f,.2f,.75f};initial={base.data(),16,0};
    ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> textureView;
    check(device->CreateTexture2D(&td,&initial,&texture));check(device->CreateShaderResourceView(texture.Get(),nullptr,&textureView));
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU=sd.AddressV=D3D11_TEXTURE_ADDRESS_MIRROR;sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD=13;sd.MaxAnisotropy=1;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;
    ComPtr<ID3D11SamplerState> sampler;check(device->CreateSamplerState(&sd,&sampler));
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=48;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> constants;check(device->CreateBuffer(&bd,nullptr,&constants));
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
    ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster));context->RSSetState(raster.Get());
    const D3D11_VIEWPORT vp{0,0,4,4,0,1};context->RSSetViewports(1,&vp);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
    auto* cb=constants.Get();context->VSSetConstantBuffers(3,1,&cb);
    auto* v=textureView.Get();context->PSSetShaderResources(0,1,&v);v=shadowView.Get();context->PSSetShaderResources(2,1,&v);
    auto* sm=sampler.Get();context->PSSetSamplers(0,1,&sm);context->PSSetSamplers(2,1,&sm);
    const std::array<std::array<float,2>,6> coords={{{511.7f/1024,511.8f/1024},{512.2f/1024,512.3f/1024},
        {511.5f/1024,511.5f/1024},{-511.7f/1024,511.8f/1024},{1+511.7f/1024,1+511.8f/1024},{-.2f,2.4f}}};
    const std::array<float,4> tint={.3f,.4f,.5f,.6f};size_t cases=0,checks=0;
    for(unsigned mask=0;mask<16;++mask) {
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)depth[(511+y)*1024+511+x]=(mask&(1u<<(2*y+x)))?.75f:.25f;
        ID3D11ShaderResourceView* nullView=nullptr;context->PSSetShaderResources(2,1,&nullView);
        context->UpdateSubresource(shadow.Get(),0,nullptr,depth.data(),1024*4,0);v=shadowView.Get();context->PSSetShaderResources(2,1,&v);
        for(auto uv:coords)for(float z:{-.1f,.25f,.5f,.9f,1.1f})for(float ambient:{0.0f,.4f,1.0f}) {
            const std::array<std::array<float,4>,3> inputs={{{.2f,.3f,0,0},{uv[0],uv[1],z,ambient},tint}};
            context->UpdateSubresource(constants.Get(),0,nullptr,inputs.data(),0,0);auto* target=rt.Get();context->OMSetRenderTargets(1,&target,nullptr);
            context->Draw(3,0);context->OMSetRenderTargets(0,nullptr,nullptr);context->CopyResource(readback.Get(),output.Get());
            const float threshold=1-std::clamp(z,0.0f,1.0f);
            const auto visible=[&](float dx,float dy){return threshold>=depth[mirror(uv[1]+dy/1024)*1024+mirror(uv[0]+dx/1024)]?1.0f:0.0f;};
            const float fx=fraction(uv[0]*1024-.5f),fy=fraction(uv[1]*1024-.5f);
            const float left=visible(-.5f,-.5f)*(1-fy)+visible(-.5f,.5f)*fy;
            const float right=visible(.5f,-.5f)*(1-fy)+visible(.5f,.5f)*fy;
            const float light=ambient+(1-ambient)*(left*(1-fx)+right*fx);
            std::array<float,4> expected{};for(unsigned i=0;i<4;++i)expected[i]=2*base[i]*tint[i]*(i<3?light:1);
            D3D11_MAPPED_SUBRESOURCE map{};check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&map));
            for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)for(unsigned lane=0;lane<4;++lane) {
                const auto actual=reinterpret_cast<const float*>(static_cast<const uint8_t*>(map.pData)+y*map.RowPitch)[4*x+lane];++checks;
                if(!std::isfinite(actual)||std::abs(actual-expected[lane])>2e-5f) {
                    std::fprintf(stderr,"projected PS mismatch mask=%u uv=%g,%g z=%g ambient=%g lane=%u actual=%g expected=%g\n",mask,uv[0],uv[1],z,ambient,lane,actual,expected[lane]);
                    throw Error("Original projected particle arithmetic/sampling differs");
                }
            }
            context->Unmap(readback.Get(),0);++cases;
        }
    }
    ComPtr<ID3D11InfoQueue> queue;device->QueryInterface(IID_PPV_ARGS(&queue));
    if(queue)for(UINT64 i=0;i<queue->GetNumStoredMessages();++i){SIZE_T n=0;queue->GetMessage(i,nullptr,&n);std::vector<uint8_t> bytes(n);auto* m=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());queue->GetMessage(i,m,&n);
        if(m->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"D3D11: %s\n",m->pDescription);throw Error("Projected particle D3D11 validation error");}}
    std::printf("PASS projected particle %s: %zu cases, %zu checks; four depth comparisons, mirrored sampling, fractional weights, depth clamp, ambient and alpha\n",hardware?"hardware":"WARP",cases,checks);
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Projected particle shader failure: %s\n",e.what());return 1;}}
