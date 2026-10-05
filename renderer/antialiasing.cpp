#include "native_backend.h"
#include <d3d11_1.h>
#include <cstdio>
#include "VSAntialiasing.h"
#include "PSFXAA.h"
#include "PSSSAA4x.h"

namespace Simpsons::Graphics {
namespace {
void check(HRESULT result,const char* operation) {
    if(FAILED(result)) {
        char message[180];std::snprintf(message,sizeof(message),"Native antialiasing %s failed: 0x%08lX",operation,ULONG(result));
        throw Error(message);
    }
}
}
struct AntialiasingPipeline {
    ComPtr<ID3D11DeviceContext1> context;
    ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> fxaa,ssaa;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11DepthStencilState> depth;
    AntialiasingPipeline(ID3D11Device* device,ID3D11DeviceContext* immediate) {
        ComPtr<ID3D11Device1> device1;
        check(device->QueryInterface(IID_PPV_ARGS(&device1)),"device-state interface");
        check(immediate->QueryInterface(IID_PPV_ARGS(&context)),"context-state interface");
        const auto level=device->GetFeatureLevel();
        const UINT flags=(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
        check(device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&state),"isolated context state");
        check(device->CreateVertexShader(kVSAntialiasing,sizeof(kVSAntialiasing),nullptr,&vertex),"fullscreen vertex shader");
        check(device->CreatePixelShader(kPSFXAA,sizeof(kPSFXAA),nullptr,&fxaa),"FXAA shader");
        check(device->CreatePixelShader(kPSSSAA4x,sizeof(kPSSSAA4x),nullptr,&ssaa),"SSAA resolve shader");
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        s.MaxAnisotropy=1;s.ComparisonFunc=D3D11_COMPARISON_NEVER;s.MaxLOD=D3D11_FLOAT32_MAX;
        check(device->CreateSamplerState(&s,&sampler),"clamped linear sampler");
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
        check(device->CreateRasterizerState(&r,&raster),"fullscreen raster state");
        D3D11_BLEND_DESC b{};b.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        check(device->CreateBlendState(&b,&blend),"replacement blend state");
        D3D11_DEPTH_STENCIL_DESC d{};d.DepthFunc=D3D11_COMPARISON_ALWAYS;
        check(device->CreateDepthStencilState(&d,&depth),"disabled depth state");
    }
};
std::shared_ptr<RenderTarget> NativeBackend::resolveAntialiasing(const std::shared_ptr<RenderTarget>& front) {
    flushIm2D();validateSubmissionContext();validateFrontTarget(front);
    if(antialiasingMode==Antialiasing::Original)return front;
    const uint32_t scale=antialiasingMode==Antialiasing::SSAA4x?2:1;
    if(front->pixelWidth()%scale||front->pixelHeight()%scale)throw Error("SSAA source must contain four samples per output pixel");
    const auto w=front->pixelWidth()/scale,h=front->pixelHeight()/scale;
    if(!antialiasingPipeline)antialiasingPipeline=std::make_shared<AntialiasingPipeline>(device.Get(),context.Get());
    if(!antialiasingOutput||antialiasingOutput->pixelWidth()!=w||antialiasingOutput->pixelHeight()!=h||
       antialiasingOutput->width!=front->width||antialiasingOutput->height!=front->height) {
        antialiasingOutput=createTarget(w,h,TargetFormat::RGB10A2);
        antialiasingOutput->width=front->width;antialiasingOutput->height=front->height;
        tagRenderExtent(antialiasingOutput->texture.Get(),front->width,front->height);
    }
    if(front==antialiasingOutput)throw Error("Antialiasing source aliases its presentation output");
    auto& pipeline=*antialiasingPipeline;
    struct Restore {
        ID3D11DeviceContext1* context;
        ComPtr<ID3DDeviceContextState> original;
        ~Restore(){context->ClearState();context->SwapDeviceContextState(original.Get(),nullptr);}
    } restore{pipeline.context.Get()};
    pipeline.context->SwapDeviceContextState(pipeline.state.Get(),&restore.original);
    context->ClearState();
    context->OMSetRenderTargets(1,antialiasingOutput->view.GetAddressOf(),nullptr);
    const D3D11_VIEWPORT viewport{0,0,float(w),float(h),0,1};context->RSSetViewports(1,&viewport);
    context->RSSetState(pipeline.raster.Get());
    context->OMSetBlendState(pipeline.blend.Get(),nullptr,~0u);
    context->OMSetDepthStencilState(pipeline.depth.Get(),0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(pipeline.vertex.Get(),nullptr,0);
    context->PSSetShader(scale==2?pipeline.ssaa.Get():pipeline.fxaa.Get(),nullptr,0);
    context->PSSetSamplers(0,1,pipeline.sampler.GetAddressOf());
    context->PSSetShaderResources(0,1,front->sampledView.GetAddressOf());
    context->Draw(3,0);
    return antialiasingOutput;
}
}
