#include "native_backend.h"
#include "runtime/stall_profiler.h"
#include <d3d11_1.h>
#include <cstdio>
#include "VSScenePost.h"
#include "PSWorldLook.h"

namespace Simpsons::Graphics {
namespace {
void check(HRESULT result,const char* operation) {
    if(FAILED(result)) {
        char message[180];std::snprintf(message,sizeof(message),"Native scene post %s failed: 0x%08lX",operation,ULONG(result));
        throw Error(message);
    }
}
}
// Isolated full-screen passes over a copy of the scene color.
struct ScenePostPipeline {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext1> context;
    ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> look;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11Texture2D> snapshot;
    ComPtr<ID3D11ShaderResourceView> snapshotView;
    uint32_t width{},height{};
    ScenePostPipeline(ID3D11Device* owner,ID3D11DeviceContext* immediate):device(owner) {
        ComPtr<ID3D11Device1> device1;
        check(owner->QueryInterface(IID_PPV_ARGS(&device1)),"device-state interface");
        check(immediate->QueryInterface(IID_PPV_ARGS(&context)),"context-state interface");
        const auto level=owner->GetFeatureLevel();
        const UINT flags=(owner->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
        check(device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&state),"isolated context state");
        check(owner->CreateVertexShader(kVSScenePost,sizeof(kVSScenePost),nullptr,&vertex),"vertex shader");
        check(owner->CreatePixelShader(kPSWorldLook,sizeof(kPSWorldLook),nullptr,&look),"look shader");
        D3D11_BUFFER_DESC b{};b.ByteWidth=16;b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(owner->CreateBuffer(&b,nullptr,&constants),"constants");
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
        check(owner->CreateRasterizerState(&r,&raster),"raster state");
        D3D11_BLEND_DESC d{};d.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        check(owner->CreateBlendState(&d,&blend),"replacement blend state");
        D3D11_DEPTH_STENCIL_DESC z{};z.DepthFunc=D3D11_COMPARISON_ALWAYS;
        check(owner->CreateDepthStencilState(&z,&depth),"disabled depth state");
    }
    void prepare(ID3D11Texture2D* scene,uint32_t w,uint32_t h) {
        if(w==width&&h==height&&snapshot)return;
        D3D11_TEXTURE2D_DESC desc{};scene->GetDesc(&desc);
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=0;desc.CPUAccessFlags=0;desc.Usage=D3D11_USAGE_DEFAULT;
        ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;
        check(device->CreateTexture2D(&desc,nullptr,&texture),"scene snapshot");
        check(device->CreateShaderResourceView(texture.Get(),nullptr,&view),"scene snapshot view");
        snapshot=std::move(texture);snapshotView=std::move(view);width=w;height=h;
    }
};
void NativeBackend::applyWorldLook(const std::shared_ptr<RenderTarget>& color,std::array<float,4> look) {
    flushIm2D();validateSubmissionContext();
    if(!color||!color->texture||!color->view||color->format!=TargetFormat::RGB10A2)
        throw Error("Native world look requires an RGB10A2 scene color");
    for(float value:look)if(!std::isfinite(value))throw Error("Native world look parameters are not finite");
    StallProfiler::Scope profile(StallProfiler::Section::Rendering,"ScenePost.look",nullptr);
    if(!scenePostPipeline||scenePostPipeline->device.Get()!=device.Get())
        scenePostPipeline=std::make_shared<ScenePostPipeline>(device.Get(),context.Get());
    auto& p=*scenePostPipeline;
    const uint32_t w=color->physicalWidth,h=color->physicalHeight;
    p.prepare(color->texture.Get(),w,h);
    context->CopyResource(p.snapshot.Get(),color->texture.Get());
    context->UpdateSubresource(p.constants.Get(),0,nullptr,look.data(),0,0);
    {
        struct Restore {
            ID3D11DeviceContext1* context;
            ComPtr<ID3DDeviceContextState> original;
            ~Restore(){context->ClearState();context->SwapDeviceContextState(original.Get(),nullptr);}
        } restore{p.context.Get()};
        p.context->SwapDeviceContextState(p.state.Get(),&restore.original);
        context->ClearState();
        const D3D11_VIEWPORT viewport{0,0,float(w),float(h),0,1};context->RSSetViewports(1,&viewport);
        context->RSSetState(p.raster.Get());
        context->OMSetBlendState(p.blend.Get(),nullptr,~0u);
        context->OMSetDepthStencilState(p.depth.Get(),0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(p.vertex.Get(),nullptr,0);
        context->PSSetShader(p.look.Get(),nullptr,0);
        context->PSSetConstantBuffers(0,1,p.constants.GetAddressOf());
        context->OMSetRenderTargets(1,color->view.GetAddressOf(),nullptr);
        context->PSSetShaderResources(0,1,p.snapshotView.GetAddressOf());
        context->Draw(3,0);
    }
    invalidateTemporalCopiedOrigin(color->texture.Get());
}
void NativeBackend::stashColor(const std::shared_ptr<RenderTarget>& color) {
    flushIm2D();validateSubmissionContext();
    if(!color||!color->texture)throw Error("Native color stash lacks its target");
    D3D11_TEXTURE2D_DESC desc{};color->texture->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC current{};
    if(colorStash)colorStash->GetDesc(&current);
    if(!colorStash||current.Width!=desc.Width||current.Height!=desc.Height||current.Format!=desc.Format) {
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=0;desc.CPUAccessFlags=0;desc.Usage=D3D11_USAGE_DEFAULT;
        ComPtr<ID3D11Texture2D> created;
        if(FAILED(device->CreateTexture2D(&desc,nullptr,&created)))throw Error("Native color stash allocation failed");
        colorStash=std::move(created);
    }
    context->CopyResource(colorStash.Get(),color->texture.Get());colorStashSource=color->texture;
}
bool NativeBackend::restoreStashedColor(const std::shared_ptr<RenderTarget>& color) {
    flushIm2D();validateSubmissionContext();
    // Only the same storage that was stashed (a live resolution change re-backs it).
    if(!color||!colorStash||color->texture.Get()!=colorStashSource.Get())return false;
    context->CopyResource(color->texture.Get(),colorStash.Get());colorStashSource.Reset();
    invalidateTemporalCopiedOrigin(color->texture.Get());
    return true;
}
}
