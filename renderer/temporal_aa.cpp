#include "native_backend.h"
#include "runtime/stall_profiler.h"
#include <d3d11_1.h>
#include <cstdio>
#include "VSTemporalAA.h"
#include "PSTemporalAAResolve.h"

namespace Simpsons::Graphics {
namespace {
void check(HRESULT result,const char* operation) {
    if(FAILED(result)) {
        char message[180];std::snprintf(message,sizeof(message),"Native temporal AA %s failed: 0x%08lX",operation,ULONG(result));
        throw Error(message);
    }
}
struct Constants {
    std::array<std::array<float,4>,4> reprojection;
    std::array<float,4> extent,control,motion;
};
static_assert(sizeof(Constants)==112);
}
struct NativeTemporalPipeline {
    struct Surface {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> target;
        ComPtr<ID3D11ShaderResourceView> sampled;
    };
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext1> context;
    ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11DepthStencilState> depth;
    // Snapshot of the scene being resolved, and the two history buffers.
    Surface snapshot;
    std::array<Surface,2> history;
    uint32_t width{},height{},current{};
    bool historyValid=false;
    NativeTemporalPipeline(ID3D11Device* owner,ID3D11DeviceContext* immediate):device(owner) {
        ComPtr<ID3D11Device1> device1;
        check(owner->QueryInterface(IID_PPV_ARGS(&device1)),"device-state interface");
        check(immediate->QueryInterface(IID_PPV_ARGS(&context)),"context-state interface");
        const auto level=owner->GetFeatureLevel();
        const UINT flags=(owner->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
        check(device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&state),"isolated context state");
        check(owner->CreateVertexShader(kVSTemporalAA,sizeof(kVSTemporalAA),nullptr,&vertex),"vertex shader");
        check(owner->CreatePixelShader(kPSTemporalAAResolve,sizeof(kPSTemporalAAResolve),nullptr,&pixel),"resolve shader");
        D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(Constants);b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(owner->CreateBuffer(&b,nullptr,&constants),"constants");
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        s.MaxAnisotropy=1;s.ComparisonFunc=D3D11_COMPARISON_NEVER;s.MaxLOD=D3D11_FLOAT32_MAX;
        check(owner->CreateSamplerState(&s,&sampler),"history sampler");
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
        check(owner->CreateRasterizerState(&r,&raster),"raster state");
        D3D11_BLEND_DESC d{};
        d.IndependentBlendEnable=FALSE;d.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        check(owner->CreateBlendState(&d,&blend),"replacement blend state");
        D3D11_DEPTH_STENCIL_DESC z{};z.DepthFunc=D3D11_COMPARISON_ALWAYS;
        check(owner->CreateDepthStencilState(&z,&depth),"disabled depth state");
    }
    void prepare(ID3D11Texture2D* scene,uint32_t w,uint32_t h) {
        if(w==width&&h==height&&snapshot.texture)return;
        D3D11_TEXTURE2D_DESC desc{};scene->GetDesc(&desc);
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=0;desc.CPUAccessFlags=0;desc.Usage=D3D11_USAGE_DEFAULT;
        Surface copy;
        check(device->CreateTexture2D(&desc,nullptr,&copy.texture),"scene snapshot");
        check(device->CreateShaderResourceView(copy.texture.Get(),nullptr,&copy.sampled),"scene snapshot view");
        std::array<Surface,2> fresh;
        for(auto& surface:fresh) {
            D3D11_TEXTURE2D_DESC h16{};h16.Width=w;h16.Height=h;h16.MipLevels=h16.ArraySize=1;
            h16.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;h16.SampleDesc={1,0};h16.Usage=D3D11_USAGE_DEFAULT;
            h16.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            check(device->CreateTexture2D(&h16,nullptr,&surface.texture),"history");
            check(device->CreateRenderTargetView(surface.texture.Get(),nullptr,&surface.target),"history target");
            check(device->CreateShaderResourceView(surface.texture.Get(),nullptr,&surface.sampled),"history view");
        }
        snapshot=std::move(copy);history=std::move(fresh);width=w;height=h;current=0;historyValid=false;
    }
};
void NativeBackend::resetNativeTemporal() noexcept {
    if(nativeTemporalPipeline)nativeTemporalPipeline->historyValid=false;
}
bool NativeBackend::nativeTemporalHistory() const noexcept {
    return nativeTemporalPipeline&&nativeTemporalPipeline->historyValid;
}
void NativeBackend::resolveNativeTemporal(const std::shared_ptr<RenderTarget>& color,const std::shared_ptr<DepthTarget>& depthTarget,
    const NativeTemporalFrame& frame) {
    flushIm2D();validateSubmissionContext();
    if(!color||!depthTarget||!color->texture||!color->view||!depthTarget->depthView)
        throw Error("Native temporal AA lacks its scene color or depth owner");
    const uint32_t w=color->physicalWidth,h=color->physicalHeight;
    if(color->format!=TargetFormat::RGB10A2||depthTarget->physicalWidth!=w||depthTarget->physicalHeight!=h)
        throw Error("Native temporal AA requires matching RGB10A2 scene color and depth storage");
    StallProfiler::Scope profile(StallProfiler::Section::Rendering,"TemporalAA.resolve",nullptr);
    if(!nativeTemporalPipeline||nativeTemporalPipeline->device.Get()!=device.Get())
        nativeTemporalPipeline=std::make_shared<NativeTemporalPipeline>(device.Get(),context.Get());
    auto& p=*nativeTemporalPipeline;
    p.prepare(color->texture.Get(),w,h);
    context->CopyResource(p.snapshot.texture.Get(),color->texture.Get());
    Constants values{};
    values.reprojection=frame.reprojection;
    values.extent={1.0f/float(w),1.0f/float(h),float(w),float(h)};
    // A tenth of each new sample converges in a few frames while hiding the
    // eight-sample jitter; fast motion trusts the new sample more.
    values.control={frame.historyJitter[0],frame.historyJitter[1],frame.historyValid&&p.historyValid?1.0f:0.0f,0.1f};
    values.motion={0.35f,48.0f,1.25f,0};
    context->UpdateSubresource(p.constants.Get(),0,nullptr,&values,0,0);
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
        context->PSSetShader(p.pixel.Get(),nullptr,0);
        context->PSSetConstantBuffers(0,1,p.constants.GetAddressOf());
        context->PSSetSamplers(0,1,p.sampler.GetAddressOf());
        const auto next=p.current^1u;
        const std::array<ID3D11RenderTargetView*,2> targets{color->view.Get(),p.history[next].target.Get()};
        context->OMSetRenderTargets(UINT(targets.size()),targets.data(),nullptr);
        const std::array<ID3D11ShaderResourceView*,3> inputs{p.snapshot.sampled.Get(),depthTarget->depthView.Get(),p.history[p.current].sampled.Get()};
        context->PSSetShaderResources(0,UINT(inputs.size()),inputs.data());
        context->Draw(3,0);
        p.current=next;
    }
    p.historyValid=true;++nativeTemporalResolves;
    invalidateTemporalCopiedOrigin(color->texture.Get());
}
}
