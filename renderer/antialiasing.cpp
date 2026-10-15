#include "native_backend.h"
#include "antialiasing.h"
#include "dlss.h"
#include "runtime/stall_profiler.h"
#include <d3d11_1.h>
#include <cstdio>
#include "VSAntialiasing.h"
#include "PSFXAA.h"
#include "PSSharpen.h"

namespace Simpsons::Graphics {
namespace {
void check(HRESULT result,const char* operation) {
    if(FAILED(result)) {
        char message[180];std::snprintf(message,sizeof(message),"Native antialiasing %s failed: 0x%08lX",operation,ULONG(result));
        throw Error(message);
    }
}
struct FilterConstants {
    std::array<float,4> fxaa{};
    std::array<float,4> detail{};
};
static_assert(sizeof(FilterConstants)==32);
ComPtr<ID3D11Buffer> createFilterConstants(ID3D11Device* device,const FilterConstants& values) {
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(values);
    desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    const D3D11_SUBRESOURCE_DATA data{&values,0,0};
    ComPtr<ID3D11Buffer> result;
    check(device->CreateBuffer(&desc,&data,&result),"immutable filter constants");
    return result;
}
}
AntialiasingPipeline::AntialiasingPipeline(ID3D11Device* device,ID3D11DeviceContext* immediate) {
        ComPtr<ID3D11Device1> device1;
        check(device->QueryInterface(IID_PPV_ARGS(&device1)),"device-state interface");
        check(immediate->QueryInterface(IID_PPV_ARGS(&context)),"context-state interface");
        const auto level=device->GetFeatureLevel();
        const UINT flags=(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
        check(device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&state),"isolated context state");
        check(device->CreateVertexShader(kVSAntialiasing,sizeof(kVSAntialiasing),nullptr,&vertex),"fullscreen vertex shader");
        check(device->CreatePixelShader(kPSFXAA,sizeof(kPSFXAA),nullptr,&fxaa),"FXAA shader");
        check(device->CreatePixelShader(kPSSharpen,sizeof(kPSSharpen),nullptr,&sharpen),"sharpen shader");
        // The original FXAA preset retains exactly its existing four-tap
        // thresholds/span. Low skips weaker edges and uses two taps. High
        // detects weaker contrast and takes six taps across a longer span.
        fxaaQuality[0]=createFilterConstants(device,{{1.0f/32,1.0f/8,8,1.0f/128},{0,0,0,0}});
        fxaaQuality[1]=createFilterConstants(device,{{1.0f/16,1.0f/6,4,1.0f/64},{-1,0,0,0}});
        fxaaQuality[2]=createFilterConstants(device,{{1.0f/64,1.0f/12,12,1.0f/256},{1,0,0,0}});
        for(const uint32_t percent:{25u,50u,75u,100u})
            sharpeningStrength[percent]=createFilterConstants(device,{{},{0,float(percent)/100,0,0}});
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
void NativeBackend::warmPresentationFilters() {
    validateSubmissionContext();
    if(!antialiasingPipeline) {
        StallProfiler::Scope filterProfile(StallProfiler::Section::Rendering,"Startup.presentationFilters.create",nullptr);
        antialiasingPipeline=std::make_shared<AntialiasingPipeline>(device.Get(),context.Get());
    }
}
void NativeBackend::configureSharpening(uint32_t percent) {
    requireOwner();
    if(percent>100)throw Error("Native sharpening strength must be between 0 and 100 percent");
    if(percent) {
        warmPresentationFilters();
        auto& constant=antialiasingPipeline->sharpeningStrength[percent];
        if(!constant)constant=createFilterConstants(device.Get(),{{},{0,float(percent)/100,0,0}});
    }
    presentationSharpening=percent;
}
std::shared_ptr<RenderTarget> NativeBackend::resolveAntialiasing(const std::shared_ptr<RenderTarget>& front) {
    flushIm2D();validateSubmissionContext();validateFrontTarget(front);
    bool spatial=false;
    switch(antialiasingMode) {
        case Antialiasing::Original:
        // Native TAA already resolved the world before post effects and the HUD.
        case Antialiasing::TAA:break;
        case Antialiasing::FXAA:
        case Antialiasing::FXAAOriginal:
        case Antialiasing::FXAALow:
        case Antialiasing::FXAAHigh:
        case Antialiasing::SMAA:spatial=true;break;
        case Antialiasing::DLAA:
        case Antialiasing::DLSSQuality:
        case Antialiasing::DLSSBalanced:
        case Antialiasing::DLSSPerformance:
        case Antialiasing::DLSSUltraPerformance:
            // NGX reconstruction and original post processing already produced
            // the output-sized frame. Presentation may only add sharpening.
            if(!nvidiaDlss||!nvidiaDlss->capability().available||!temporalScene||!temporalOutput||
               front->pixelWidth()!=outputWidth||front->pixelHeight()!=outputHeight||
               temporalOutput->pixelWidth()!=outputWidth||temporalOutput->pixelHeight()!=outputHeight||
               nvidiaDlss->extent().outputWidth!=outputWidth||nvidiaDlss->extent().outputHeight!=outputHeight)
                throw Error("Native temporal presentation requires an available reconstruction feature and full output extent");
            break;
        default:throw Error("Native antialiasing mode is unsupported");
    }
    if(!spatial&&!presentationSharpening)return front;
    const auto w=front->pixelWidth(),h=front->pixelHeight();
    warmPresentationFilters();
    auto& pipeline=*antialiasingPipeline;
    const auto prepareTarget=[&](std::shared_ptr<RenderTarget>& target) {
        if(!target||target->pixelWidth()!=w||target->pixelHeight()!=h||
           target->width!=front->width||target->height!=front->height) {
            auto created=createTarget(w,h,TargetFormat::RGB10A2);
            created->width=front->width;created->height=front->height;
            tagRenderExtent(created->texture.Get(),front->width,front->height);
            target=std::move(created);
        }
        if(front->texture.Get()==target->texture.Get())throw Error("Native presentation filter source aliases its output");
    };
    if(spatial)prepareTarget(antialiasingOutput);
    if(antialiasingMode==Antialiasing::SMAA) {
        if(!pipeline.smaa)pipeline.smaa=std::make_unique<detail::SMAAPipeline>(device.Get());
        pipeline.smaa->prepare(w,h);
    }
    if(presentationSharpening) {
        prepareTarget(pipeline.sharpened);
        auto& constant=pipeline.sharpeningStrength[presentationSharpening];
        if(!constant)constant=createFilterConstants(device.Get(),{{},{0,float(presentationSharpening)/100,0,0}});
    }
    struct Restore {
        ID3D11DeviceContext1* context;
        ComPtr<ID3DDeviceContextState> original;
        ~Restore(){context->ClearState();context->SwapDeviceContextState(original.Get(),nullptr);}
    } restore{pipeline.context.Get()};
    pipeline.context->SwapDeviceContextState(pipeline.state.Get(),&restore.original);
    context->ClearState();
    const D3D11_VIEWPORT viewport{0,0,float(w),float(h),0,1};context->RSSetViewports(1,&viewport);
    context->RSSetState(pipeline.raster.Get());
    context->OMSetBlendState(pipeline.blend.Get(),nullptr,~0u);
    context->OMSetDepthStencilState(pipeline.depth.Get(),0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(pipeline.vertex.Get(),nullptr,0);
    context->PSSetSamplers(0,1,pipeline.sampler.GetAddressOf());
    const auto drawFilter=[&](const std::shared_ptr<RenderTarget>& source,const std::shared_ptr<RenderTarget>& output,
                              ID3D11PixelShader* shader,ID3D11Buffer* constant) {
        // Clear every presentation source slot before the next output bind.
        const std::array<ID3D11ShaderResourceView*,3> empty{};context->PSSetShaderResources(0,UINT(empty.size()),empty.data());
        context->OMSetRenderTargets(1,output->view.GetAddressOf(),nullptr);
        context->VSSetShader(pipeline.vertex.Get(),nullptr,0);
        context->PSSetSamplers(0,1,pipeline.sampler.GetAddressOf());
        context->PSSetShader(shader,nullptr,0);context->PSSetConstantBuffers(0,1,&constant);
        context->PSSetShaderResources(0,1,source->sampledView.GetAddressOf());
        invalidateTemporalCopiedOrigin(output->texture.Get());
        context->Draw(3,0);
    };
    std::shared_ptr<RenderTarget> result=front;
    if(spatial) {
        if(antialiasingMode==Antialiasing::SMAA) {
            invalidateTemporalCopiedOrigin(antialiasingOutput->texture.Get());
            pipeline.smaa->draw(context.Get(),front->sampledView.Get(),antialiasingOutput->view.Get());
        }else {
            const size_t quality=antialiasingMode==Antialiasing::FXAALow?1:antialiasingMode==Antialiasing::FXAAHigh?2:0;
            drawFilter(front,antialiasingOutput,pipeline.fxaa.Get(),pipeline.fxaaQuality[quality].Get());
        }
        result=antialiasingOutput;
    }
    if(presentationSharpening) {
        drawFilter(result,pipeline.sharpened,pipeline.sharpen.Get(),pipeline.sharpeningStrength[presentationSharpening].Get());
        result=pipeline.sharpened;
    }
    return result;
}
}
