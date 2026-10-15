#include "native_backend.h"
#include "runtime/stall_profiler.h"
#include <d3d11_1.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "VSAmbientOcclusion.h"
#include "PSAmbientOcclusion.h"
#include "PSAmbientOcclusionBlur.h"
#include "PSAmbientOcclusionApply.h"

namespace Simpsons::Graphics {
namespace {
void check(HRESULT result,const char* operation) {
    if(FAILED(result)) {
        char message[180];std::snprintf(message,sizeof(message),"Native ambient occlusion %s failed: 0x%08lX",operation,ULONG(result));
        throw Error(message);
    }
}
struct Constants {std::array<float,4> projection,offsets,params,extent,blur;};
static_assert(sizeof(Constants)==80);
}
struct AmbientOcclusionPipeline {
    struct Surface {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> target;
        ComPtr<ID3D11ShaderResourceView> sampled;
    };
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext1> context;
    ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> occlusion,blur,apply;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11BlendState> replace,multiply;
    ComPtr<ID3D11DepthStencilState> depth;
    std::array<Surface,2> surfaces;
    uint32_t width{},height{};
    AmbientOcclusionPipeline(ID3D11Device* owner,ID3D11DeviceContext* immediate):device(owner) {
        ComPtr<ID3D11Device1> device1;
        check(owner->QueryInterface(IID_PPV_ARGS(&device1)),"device-state interface");
        check(immediate->QueryInterface(IID_PPV_ARGS(&context)),"context-state interface");
        const auto level=owner->GetFeatureLevel();
        const UINT flags=(owner->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
        check(device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&state),"isolated context state");
        check(owner->CreateVertexShader(kVSAmbientOcclusion,sizeof(kVSAmbientOcclusion),nullptr,&vertex),"vertex shader");
        check(owner->CreatePixelShader(kPSAmbientOcclusion,sizeof(kPSAmbientOcclusion),nullptr,&occlusion),"occlusion shader");
        check(owner->CreatePixelShader(kPSAmbientOcclusionBlur,sizeof(kPSAmbientOcclusionBlur),nullptr,&blur),"blur shader");
        check(owner->CreatePixelShader(kPSAmbientOcclusionApply,sizeof(kPSAmbientOcclusionApply),nullptr,&apply),"apply shader");
        D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(Constants);b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(owner->CreateBuffer(&b,nullptr,&constants),"constants");
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        s.MaxAnisotropy=1;s.ComparisonFunc=D3D11_COMPARISON_NEVER;s.MaxLOD=D3D11_FLOAT32_MAX;
        check(owner->CreateSamplerState(&s,&sampler),"sampler");
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
        check(owner->CreateRasterizerState(&r,&raster),"raster state");
        D3D11_BLEND_DESC d{};d.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        check(owner->CreateBlendState(&d,&replace),"replacement blend state");
        // color = destination * occlusion; alpha (packed scene flags) is kept.
        auto& m=d.RenderTarget[0];m.BlendEnable=TRUE;m.SrcBlend=D3D11_BLEND_ZERO;m.DestBlend=D3D11_BLEND_SRC_COLOR;m.BlendOp=D3D11_BLEND_OP_ADD;
        m.SrcBlendAlpha=D3D11_BLEND_ZERO;m.DestBlendAlpha=D3D11_BLEND_ONE;m.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        check(owner->CreateBlendState(&d,&multiply),"multiply blend state");
        D3D11_DEPTH_STENCIL_DESC z{};z.DepthFunc=D3D11_COMPARISON_ALWAYS;
        check(owner->CreateDepthStencilState(&z,&depth),"disabled depth state");
    }
    void prepare(uint32_t w,uint32_t h) {
        if(w==width&&h==height&&surfaces[0].texture)return;
        std::array<Surface,2> fresh;
        for(auto& surface:fresh) {
            D3D11_TEXTURE2D_DESC desc{};desc.Width=std::max(1u,w/2);desc.Height=std::max(1u,h/2);desc.MipLevels=desc.ArraySize=1;
            desc.Format=DXGI_FORMAT_R16_FLOAT;desc.SampleDesc={1,0};desc.Usage=D3D11_USAGE_DEFAULT;
            desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            check(device->CreateTexture2D(&desc,nullptr,&surface.texture),"occlusion surface");
            check(device->CreateRenderTargetView(surface.texture.Get(),nullptr,&surface.target),"occlusion target");
            check(device->CreateShaderResourceView(surface.texture.Get(),nullptr,&surface.sampled),"occlusion view");
        }
        surfaces=std::move(fresh);width=w;height=h;
    }
};
void NativeBackend::applyAmbientOcclusion(const std::shared_ptr<RenderTarget>& color,const std::shared_ptr<DepthTarget>& depthTarget,
    const AmbientOcclusionFrame& frame) {
    flushIm2D();validateSubmissionContext();
    if(!color||!depthTarget||!color->view||!depthTarget->depthView||color->format!=TargetFormat::RGB10A2)
        throw Error("Native ambient occlusion lacks its scene color or depth owner");
    if(frame.quality<1||frame.quality>2)throw Error("Unknown native ambient occlusion quality");
    for(float value:frame.projection)if(!std::isfinite(value))throw Error("Native ambient occlusion projection is not finite");
    if(!(frame.projection[0]>0)||!(frame.projection[1]>0)||frame.projection[3]==0)return; // Not a perspective world view.
    const uint32_t w=depthTarget->physicalWidth,h=depthTarget->physicalHeight;
    if(color->physicalWidth!=w||color->physicalHeight!=h)throw Error("Native ambient occlusion color/depth storage differs");
    StallProfiler::Scope profile(StallProfiler::Section::Rendering,"AmbientOcclusion.apply",nullptr);
    if(!ambientOcclusionPipeline||ambientOcclusionPipeline->device.Get()!=device.Get())
        ambientOcclusionPipeline=std::make_shared<AmbientOcclusionPipeline>(device.Get(),context.Get());
    auto& p=*ambientOcclusionPipeline;
    p.prepare(w,h);
    Constants values{};
    values.projection=frame.projection;
    values.offsets={frame.offsets[0],frame.offsets[1],float(frame.frame%64)*0.618034f,0};
    // Low: shorter reach and eight samples; High: a wider, denser estimate.
    values.params=frame.quality==1?std::array<float,4>{0.6f,0.8f,0.0015f,8}:std::array<float,4>{1.0f,1.0f,0.0015f,16};
    values.extent={float(w),float(h),float(std::max(1u,w/2)),float(std::max(1u,h/2))};
    const auto pass=[&](ID3D11PixelShader* shader,ID3D11RenderTargetView* target,ID3D11ShaderResourceView* input,
                        ID3D11BlendState* blend,const D3D11_VIEWPORT& viewport,std::array<float,2> direction) {
        values.blur={direction[0],direction[1],0.1f,0};
        context->UpdateSubresource(p.constants.Get(),0,nullptr,&values,0,0);
        const std::array<ID3D11ShaderResourceView*,2> none{};context->PSSetShaderResources(0,2,none.data());
        context->RSSetViewports(1,&viewport);
        context->OMSetBlendState(blend,nullptr,~0u);
        context->OMSetRenderTargets(1,&target,nullptr);
        const std::array<ID3D11ShaderResourceView*,2> inputs{depthTarget->depthView.Get(),input};
        context->PSSetShaderResources(0,2,inputs.data());
        context->PSSetShader(shader,nullptr,0);
        context->Draw(3,0);
    };
    {
        struct Restore {
            ID3D11DeviceContext1* context;
            ComPtr<ID3DDeviceContextState> original;
            ~Restore(){context->ClearState();context->SwapDeviceContextState(original.Get(),nullptr);}
        } restore{p.context.Get()};
        p.context->SwapDeviceContextState(p.state.Get(),&restore.original);
        context->ClearState();
        context->RSSetState(p.raster.Get());
        context->OMSetDepthStencilState(p.depth.Get(),0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(p.vertex.Get(),nullptr,0);
        context->PSSetConstantBuffers(0,1,p.constants.GetAddressOf());
        context->PSSetSamplers(0,1,p.sampler.GetAddressOf());
        const D3D11_VIEWPORT half{0,0,values.extent[2],values.extent[3],0,1},full{0,0,float(w),float(h),0,1};
        pass(p.occlusion.Get(),p.surfaces[0].target.Get(),nullptr,p.replace.Get(),half,{0,0});
        pass(p.blur.Get(),p.surfaces[1].target.Get(),p.surfaces[0].sampled.Get(),p.replace.Get(),half,{1,0});
        pass(p.blur.Get(),p.surfaces[0].target.Get(),p.surfaces[1].sampled.Get(),p.replace.Get(),half,{0,1});
        pass(p.apply.Get(),color->view.Get(),p.surfaces[0].sampled.Get(),p.multiply.Get(),full,{0,0});
    }
    invalidateTemporalCopiedOrigin(color->texture.Get());
}
}
