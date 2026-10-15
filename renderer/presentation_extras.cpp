#include "native_backend.h"
#include "runtime/stall_profiler.h"
#include <d3d11_1.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "VSPresentationExtras.h"
#include "PSFpsOverlay.h"
#include "PSUpscaleEdge.h"

namespace Simpsons::Graphics {
namespace {
void check(HRESULT result,const char* operation) {
    if(FAILED(result)) {
        char message[180];std::snprintf(message,sizeof(message),"Native presentation extras %s failed: 0x%08lX",operation,ULONG(result));
        throw Error(message);
    }
}
struct Constants {
    std::array<uint32_t,8> text;
    std::array<float,4> textRect,sourceExtent,destination;
};
static_assert(sizeof(Constants)==80);
}
struct PresentationExtrasPipeline {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext1> context;
    ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> fps,upscale;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11BlendState> replace,alpha;
    ComPtr<ID3D11DepthStencilState> depth;
    PresentationExtrasPipeline(ID3D11Device* owner,ID3D11DeviceContext* immediate):device(owner) {
        ComPtr<ID3D11Device1> device1;
        check(owner->QueryInterface(IID_PPV_ARGS(&device1)),"device-state interface");
        check(immediate->QueryInterface(IID_PPV_ARGS(&context)),"context-state interface");
        const auto level=owner->GetFeatureLevel();
        const UINT flags=(owner->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
        check(device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&state),"isolated context state");
        check(owner->CreateVertexShader(kVSPresentationExtras,sizeof(kVSPresentationExtras),nullptr,&vertex),"vertex shader");
        check(owner->CreatePixelShader(kPSFpsOverlay,sizeof(kPSFpsOverlay),nullptr,&fps),"counter shader");
        check(owner->CreatePixelShader(kPSUpscaleEdge,sizeof(kPSUpscaleEdge),nullptr,&upscale),"upscale shader");
        D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(Constants);b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(owner->CreateBuffer(&b,nullptr,&constants),"constants");
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
        check(owner->CreateRasterizerState(&r,&raster),"raster state");
        D3D11_BLEND_DESC d{};d.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        check(owner->CreateBlendState(&d,&replace),"replacement blend state");
        auto& a=d.RenderTarget[0];a.BlendEnable=TRUE;a.SrcBlend=D3D11_BLEND_SRC_ALPHA;a.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;a.BlendOp=D3D11_BLEND_OP_ADD;
        a.SrcBlendAlpha=D3D11_BLEND_ZERO;a.DestBlendAlpha=D3D11_BLEND_ONE;a.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        check(owner->CreateBlendState(&d,&alpha),"alpha blend state");
        D3D11_DEPTH_STENCIL_DESC z{};z.DepthFunc=D3D11_COMPARISON_ALWAYS;
        check(owner->CreateDepthStencilState(&z,&depth),"disabled depth state");
    }
    template<class Body> void pass(ID3D11DeviceContext* immediate,Body&& body) {
        struct Restore {
            ID3D11DeviceContext1* context;
            ComPtr<ID3DDeviceContextState> original;
            ~Restore(){context->ClearState();context->SwapDeviceContextState(original.Get(),nullptr);}
        } restore{context.Get()};
        context->SwapDeviceContextState(state.Get(),&restore.original);
        immediate->ClearState();
        immediate->RSSetState(raster.Get());
        immediate->OMSetDepthStencilState(depth.Get(),0);
        immediate->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        immediate->VSSetShader(vertex.Get(),nullptr,0);
        immediate->PSSetConstantBuffers(0,1,constants.GetAddressOf());
        body();
    }
};
void NativeBackend::configurePresentationExtras(bool fpsCounter,uint32_t upscaling) {
    requireOwner();
    if(upscaling>1)throw Error("Unknown native presentation upscaling mode");
    fpsOverlayEnabled=fpsCounter;presentationUpscaling=upscaling;
}
void NativeBackend::notePresentationForCounter() noexcept {
    LARGE_INTEGER now{},frequency{};
    if(!QueryPerformanceCounter(&now)||!QueryPerformanceFrequency(&frequency)||!frequency.QuadPart)return;
    ++fpsFrames;
    if(!fpsWindowStart){fpsWindowStart=now.QuadPart;fpsFrames=0;return;}
    const double seconds=double(now.QuadPart-fpsWindowStart)/double(frequency.QuadPart);
    // Half-second windows keep the counter readable without lagging behind.
    if(seconds>=0.5){fpsValue=double(fpsFrames)/seconds;fpsFrames=0;fpsWindowStart=now.QuadPart;}
}
PresentationExtrasPipeline& NativeBackend::presentationExtras() {
    if(!presentationExtrasPipeline||presentationExtrasPipeline->device.Get()!=device.Get())
        presentationExtrasPipeline=std::make_shared<PresentationExtrasPipeline>(device.Get(),context.Get());
    return *presentationExtrasPipeline;
}
void NativeBackend::drawFpsOverlay(const std::shared_ptr<RenderTarget>& target) {
    auto& p=presentationExtras();
    Constants values{};
    // "123 FPS", or one decimal below 100 FPS.
    char digits[16]{};
    const double fps=std::clamp(fpsValue,0.0,9999.0);
    std::snprintf(digits,sizeof(digits),fps<99.95?"%.1f FPS":"%.0f FPS",fps);
    uint32_t count=0;
    for(const char* c=digits;*c&&count<8;++c) {
        const char ch=*c;
        values.text[count++]=ch>='0'&&ch<='9'?uint32_t(ch-'0'):ch=='.'?10u:ch=='F'?11u:ch=='P'?12u:ch=='S'?13u:14u;
    }
    const float scale=std::max(2.0f,std::floor(float(target->pixelHeight())/360.0f));
    const float x=8*scale/2,y=8*scale/2;
    values.textRect={x,y,scale,float(count)};
    context->UpdateSubresource(p.constants.Get(),0,nullptr,&values,0,0);
    const float boxWidth=(float(count)*6+1)*scale,boxHeight=9*scale;
    p.pass(context.Get(),[&] {
        const D3D11_VIEWPORT viewport{x,y,std::min(boxWidth,float(target->pixelWidth())-x),std::min(boxHeight,float(target->pixelHeight())-y),0,1};
        context->RSSetViewports(1,&viewport);
        context->OMSetBlendState(p.alpha.Get(),nullptr,~0u);
        context->PSSetShader(p.fps.Get(),nullptr,0);
        context->OMSetRenderTargets(1,target->view.GetAddressOf(),nullptr);
        context->Draw(3,0);
    });
}
void NativeBackend::drawUpscaled(const std::shared_ptr<RenderTarget>& source,const std::shared_ptr<RenderTarget>& canvas,
    std::array<float,4> rectangle) {
    auto& p=presentationExtras();
    Constants values{};
    const float w=float(source->pixelWidth()),h=float(source->pixelHeight());
    values.sourceExtent={1/w,1/h,w,h};
    values.destination={rectangle[0],rectangle[1],1/rectangle[2],1/rectangle[3]};
    context->UpdateSubresource(p.constants.Get(),0,nullptr,&values,0,0);
    StallProfiler::Scope profile(StallProfiler::Section::Rendering,"Presentation.upscale",nullptr);
    p.pass(context.Get(),[&] {
        const D3D11_VIEWPORT viewport{rectangle[0],rectangle[1],rectangle[2],rectangle[3],0,1};
        context->RSSetViewports(1,&viewport);
        context->OMSetBlendState(p.replace.Get(),nullptr,~0u);
        context->PSSetShader(p.upscale.Get(),nullptr,0);
        context->OMSetRenderTargets(1,canvas->view.GetAddressOf(),nullptr);
        context->PSSetShaderResources(0,1,source->sampledView.GetAddressOf());
        context->Draw(3,0);
    });
}
}
