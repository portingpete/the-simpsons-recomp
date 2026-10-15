#include "scene_feedback.h"
#include "ui_composite.h"
#include "native_copy_submission.h"
#include <d3d11_1.h>
#include <d3d11_3.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include "VSSceneFeedback.h"
#include "PSSceneFeedbackColor.h"
#include "PSSceneFeedbackDepth.h"
#include "PSSceneFeedbackDepthOnly.h"
#include "PSSceneFeedbackStencilBit.h"
namespace Simpsons::Graphics {
namespace {
void check(HRESULT value,const char* operation) {
    if(FAILED(value)) {char text[180];std::snprintf(text,sizeof(text),"Native Scene feedback %s failed: 0x%08lX",operation,ULONG(value));throw Error(text);}
}
void validJitter(std::array<float,2> value) {
    for(float v:value)if(!std::isfinite(v)||std::abs(v)>0.5f)throw Error("Scene feedback jitter must be finite within half a render pixel");
}
void validMask(uint32_t mask) {if(!mask||(mask&~7u))throw Error("Scene feedback component mask is invalid");}
void unpredicated(ID3D11DeviceContext* context) {
    ComPtr<ID3D11Predicate> predicate;BOOL value{};context->GetPredication(&predicate,&value);
    if(predicate)throw Error("Scene feedback requires unpredicated resource operations");
}
bool viewOf(ID3D11View* view,ID3D11Resource* resource) {
    if(!view)return false;ComPtr<ID3D11Resource> owner;view->GetResource(&owner);return owner.Get()==resource;
}
void rejectReads(ID3D11DeviceContext* context,ID3D11Resource* color,ID3D11Resource* depth) {
    bool alias=false;
    auto inspect=[&](auto getter) {
        std::array<ID3D11ShaderResourceView*,128> views{};(context->*getter)(0,UINT(views.size()),views.data());
        for(auto* view:views)if(view){alias=alias||viewOf(view,color)||viewOf(view,depth);view->Release();}
    };
    inspect(&ID3D11DeviceContext::PSGetShaderResources);inspect(&ID3D11DeviceContext::VSGetShaderResources);
    inspect(&ID3D11DeviceContext::GSGetShaderResources);inspect(&ID3D11DeviceContext::HSGetShaderResources);
    inspect(&ID3D11DeviceContext::DSGetShaderResources);inspect(&ID3D11DeviceContext::CSGetShaderResources);
    if(alias)throw Error("Scene feedback destination is retained as a shader read");
}
struct Constants {std::array<uint32_t,4> extent;std::array<float,2> jitter;std::array<float,2> padding{};};
static_assert(sizeof(Constants)==32);
}
struct SceneFeedbackPipeline {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> immediate;
    ComPtr<ID3D11DeviceContext1> context;
    ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> color,paired,depthOnly,stencilBit;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11SamplerState> sampler;
    std::array<ComPtr<ID3D11DepthStencilState>,4> pairedState;
    std::array<ComPtr<ID3D11DepthStencilState>,8> stencilState;
    std::array<ComPtr<ID3D11Buffer>,8> bits;
    ComPtr<ID3D11Buffer> constants;
    std::shared_ptr<RenderTarget> scratchColor;
    std::shared_ptr<DepthTarget> scratchDepth;
    std::shared_ptr<RenderTarget> capturedColor;
    std::shared_ptr<DepthTarget> capturedDepth;
    uint64_t snapshotGeneration{};
    std::shared_ptr<NativeSceneFeedbackFrame> lastFrame;
    std::optional<NativeTemporalSceneOrigins> baseline;
    uint64_t lastSerial{};
    bool shaderStencilRef{};
    SceneFeedbackPipeline(ID3D11Device* d,ID3D11DeviceContext* c):device(d),immediate(c) {
        for(auto format:{DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,DXGI_FORMAT_X32_TYPELESS_G8X24_UINT}) {
            UINT support{};check(device->CheckFormatSupport(format,&support),"typed format query");
            if(!(support&D3D11_FORMAT_SUPPORT_SHADER_LOAD))throw Error("Scene feedback typed D/S loads unsupported");
        }
        UINT support{};check(device->CheckFormatSupport(DXGI_FORMAT_R10G10B10A2_UNORM,&support),"RGB format query");
        if((support&(D3D11_FORMAT_SUPPORT_SHADER_SAMPLE|D3D11_FORMAT_SUPPORT_RENDER_TARGET))!=(D3D11_FORMAT_SUPPORT_SHADER_SAMPLE|D3D11_FORMAT_SUPPORT_RENDER_TARGET))
            throw Error("Scene feedback RGB sampling/rendering unsupported");
        check(device->CheckFormatSupport(DXGI_FORMAT_D32_FLOAT_S8X24_UINT,&support),"DS output query");
        if(!(support&D3D11_FORMAT_SUPPORT_DEPTH_STENCIL))throw Error("Scene feedback D/S output unsupported");
        D3D11_FEATURE_DATA_D3D11_OPTIONS2 options{};
        shaderStencilRef=SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2,&options,sizeof(options)))&&options.PSSpecifiedStencilRefSupported;
        ComPtr<ID3D11Device1> device1;check(device.As(&device1),"device-state interface");check(immediate.As(&context),"context-state interface");
        const auto level=device->GetFeatureLevel();const UINT flags=(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
        check(device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&state),"isolated context state");
        check(device->CreateVertexShader(kVSSceneFeedback,sizeof(kVSSceneFeedback),nullptr,&vertex),"VS");
        check(device->CreatePixelShader(kPSSceneFeedbackColor,sizeof(kPSSceneFeedbackColor),nullptr,&color),"color PS");
        if(shaderStencilRef)check(device->CreatePixelShader(kPSSceneFeedbackDepth,sizeof(kPSSceneFeedbackDepth),nullptr,&paired),"paired PS");
        check(device->CreatePixelShader(kPSSceneFeedbackDepthOnly,sizeof(kPSSceneFeedbackDepthOnly),nullptr,&depthOnly),"depth PS");
        check(device->CreatePixelShader(kPSSceneFeedbackStencilBit,sizeof(kPSSceneFeedbackStencilBit),nullptr,&stencilBit),"stencil PS");
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
        check(device->CreateRasterizerState(&r,&raster),"raster");
        D3D11_BLEND_DESC b{};b.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        check(device->CreateBlendState(&b,&blend),"blend");
        D3D11_SAMPLER_DESC filter{};filter.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        filter.AddressU=filter.AddressV=filter.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;filter.ComparisonFunc=D3D11_COMPARISON_ALWAYS;
        filter.MaxAnisotropy=1;
        filter.MaxLOD=D3D11_FLOAT32_MAX;check(device->CreateSamplerState(&filter,&sampler),"linear clamp sampler");
        for(uint32_t mask=0;mask<4;++mask) {
            D3D11_DEPTH_STENCIL_DESC s{};s.DepthEnable=BOOL(mask&1);s.DepthWriteMask=(mask&1)?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;s.DepthFunc=D3D11_COMPARISON_ALWAYS;
            s.StencilEnable=BOOL(mask&2);s.StencilReadMask=255;s.StencilWriteMask=(mask&2)?255:0;
            s.FrontFace={D3D11_STENCIL_OP_REPLACE,D3D11_STENCIL_OP_REPLACE,D3D11_STENCIL_OP_REPLACE,D3D11_COMPARISON_ALWAYS};s.BackFace=s.FrontFace;
            check(device->CreateDepthStencilState(&s,&pairedState[mask]),"selective DS state");
        }
        for(uint32_t bit=0;bit<8;++bit) {
            D3D11_DEPTH_STENCIL_DESC s{};s.DepthEnable=FALSE;s.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;s.DepthFunc=D3D11_COMPARISON_ALWAYS;
            s.StencilEnable=TRUE;s.StencilReadMask=255;s.StencilWriteMask=UINT8(1u<<bit);
            s.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_REPLACE,D3D11_COMPARISON_ALWAYS};s.BackFace=s.FrontFace;
            check(device->CreateDepthStencilState(&s,&stencilState[bit]),"stencil bit state");
            const std::array<uint32_t,4> data{1u<<bit,0,0,0};D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(data);desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            const D3D11_SUBRESOURCE_DATA initial{data.data(),0,0};check(device->CreateBuffer(&desc,&initial,&bits[bit]),"immutable bit constant");
        }
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(Constants);desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        check(device->CreateBuffer(&desc,nullptr,&constants),"cached transfer constant");
    }
};

std::shared_ptr<NativeCopySubmission> NativeBackend::submitSceneFeedback(
    const std::shared_ptr<RenderTarget>& sourceColor,const std::shared_ptr<DepthTarget>& sourceDepth,
    const std::shared_ptr<RenderTarget>& destinationColor,const std::shared_ptr<DepthTarget>& destinationDepth,
    std::array<float,2> jitter,uint32_t mask,bool forceStencilFallback,std::shared_ptr<void> extraLease) {
    validJitter(jitter);validMask(mask);flushIm2D();validateSubmissionContext();retireCopies();
    validateFrontTarget(sourceColor);validateFrontTarget(destinationColor);validateDepthCopyTarget(sourceDepth);validateDepthCopyTarget(destinationDepth);
    unpredicated(context.Get());requireNoOutputUavs();
    if(sourceColor->format!=TargetFormat::RGB10A2||destinationColor->format!=TargetFormat::RGB10A2||
       sourceColor->width!=sourceDepth->width||sourceColor->height!=sourceDepth->height||
       sourceColor->width!=destinationColor->width||sourceColor->height!=destinationColor->height||
       sourceColor->width!=destinationDepth->width||sourceColor->height!=destinationDepth->height||
       sourceColor->physicalWidth!=sourceDepth->physicalWidth||sourceColor->physicalHeight!=sourceDepth->physicalHeight||
       destinationColor->physicalWidth!=destinationDepth->physicalWidth||destinationColor->physicalHeight!=destinationDepth->physicalHeight)
        throw Error("Scene feedback requires paired Single RGB10/D32 logical/physical owners");
    const std::array<ID3D11Resource*,4> owners{sourceColor->texture.Get(),sourceDepth->texture.Get(),destinationColor->texture.Get(),destinationDepth->texture.Get()};
    for(size_t i=0;i<owners.size();++i)for(size_t k=0;k<i;++k)if(owners[i]==owners[k])throw Error("Scene feedback source/destination resources alias");
    rejectReads(context.Get(),(mask&1)?destinationColor->texture.Get():nullptr,(mask&6)?destinationDepth->texture.Get():nullptr);
    if(!sceneFeedbackPipeline||sceneFeedbackPipeline->device.Get()!=device.Get()||sceneFeedbackPipeline->immediate.Get()!=context.Get())
        sceneFeedbackPipeline=std::make_shared<SceneFeedbackPipeline>(device.Get(),context.Get());
    auto pipeline=sceneFeedbackPipeline;
    const bool exact=jitter==std::array<float,2>{}&&sourceColor->physicalWidth==destinationColor->physicalWidth&&sourceColor->physicalHeight==destinationColor->physicalHeight;
    const bool drawColor=(mask&1)&&!exact;const uint32_t ds=(exact&&(mask&6)==6)?0:(mask&6);
    const bool shaderWork=drawColor||ds;
    const Constants data{{sourceColor->physicalWidth,sourceColor->physicalHeight,destinationColor->physicalWidth,destinationColor->physicalHeight},jitter,{}};
    if(shaderWork) {
        D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(pipeline->constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"update cached constant");
        std::memcpy(mapped.pData,&data,sizeof(data));context->Unmap(pipeline->constants.Get(),0);
    }
    struct Lease {std::shared_ptr<RenderTarget> sourceColor,destinationColor;std::shared_ptr<DepthTarget> sourceDepth,destinationDepth;std::shared_ptr<SceneFeedbackPipeline> pipeline;std::shared_ptr<void> extra;};
    // Exact resource copies need resource lifetime only. In particular a
    // canonical snapshot completion must not retain pipeline -> lastFrame ->
    // completion -> pipeline recursively. Shader draws retain their state.
    auto lease=std::make_shared<Lease>(Lease{sourceColor,destinationColor,sourceDepth,destinationDepth,
        shaderWork?pipeline:std::shared_ptr<SceneFeedbackPipeline>{},std::move(extraLease)});
    auto submitted=std::make_shared<NativeCopySubmission>();submitted->owner=this;submitted->sourceResource=sourceColor->texture;submitted->destinationResource=destinationColor->texture;
    submitted->sourceOwner=lease;submitted->destinationOwner=lease;
    D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};check(device->CreateQuery(&query,&submitted->event),"completion query");
    pendingCopies.push_back(submitted); // Retain every strong lease before the first resource command.
    if(mask&1)invalidateTemporalCopiedOrigin(destinationColor->texture.Get());
    if(mask&6)invalidateTemporalCopiedOrigin(destinationDepth->texture.Get());
    struct Restore {ID3D11DeviceContext1* context;ComPtr<ID3DDeviceContextState> original;~Restore(){context->ClearState();context->SwapDeviceContextState(original.Get(),nullptr);}} restore{pipeline->context.Get(),{}};
    pipeline->context->SwapDeviceContextState(pipeline->state.Get(),&restore.original);context->ClearState();
    if(exact&&(mask&1))context->CopyResource(destinationColor->texture.Get(),sourceColor->texture.Get());
    if(exact&&(mask&6)==6)context->CopyResource(destinationDepth->texture.Get(),sourceDepth->texture.Get());
    if(drawColor||ds) {
        const D3D11_VIEWPORT viewport{0,0,float(destinationColor->physicalWidth),float(destinationColor->physicalHeight),0,1};
        context->RSSetViewports(1,&viewport);context->RSSetState(pipeline->raster.Get());context->OMSetBlendState(pipeline->blend.Get(),nullptr,0xFFFFFFFFu);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(pipeline->vertex.Get(),nullptr,0);
        const std::array<ID3D11ShaderResourceView*,3> views{sourceColor->sampledView.Get(),sourceDepth->depthView.Get(),sourceDepth->stencilView.Get()};
        context->PSSetShaderResources(0,UINT(views.size()),views.data());context->PSSetConstantBuffers(0,1,pipeline->constants.GetAddressOf());
        context->PSSetSamplers(0,1,pipeline->sampler.GetAddressOf());
        if(drawColor) {
            context->OMSetRenderTargets(1,destinationColor->view.GetAddressOf(),nullptr);context->OMSetDepthStencilState(pipeline->pairedState[0].Get(),0);
            context->PSSetShader(pipeline->color.Get(),nullptr,0);context->Draw(3,0);
        }
        if(ds) {
            context->OMSetRenderTargets(0,nullptr,destinationDepth->view.Get());
            if(pipeline->shaderStencilRef&&!forceStencilFallback&&(ds&4)) {
                context->OMSetDepthStencilState(pipeline->pairedState[ds>>1].Get(),0);context->PSSetShader(pipeline->paired.Get(),nullptr,0);context->Draw(3,0);
            }else {
                if(ds&2) {context->OMSetDepthStencilState(pipeline->pairedState[1].Get(),0);context->PSSetShader(pipeline->depthOnly.Get(),nullptr,0);context->Draw(3,0);}
                if(ds&4) {
                    context->ClearDepthStencilView(destinationDepth->view.Get(),D3D11_CLEAR_STENCIL,0,0);
                    context->PSSetShader(pipeline->stencilBit.Get(),nullptr,0);
                    for(uint32_t bit=0;bit<8;++bit) {context->OMSetDepthStencilState(pipeline->stencilState[bit].Get(),1u<<bit);context->PSSetConstantBuffers(1,1,pipeline->bits[bit].GetAddressOf());context->Draw(3,0);}
                }
            }
        }
    }
    context->End(submitted->event.Get());submitted->submitted=true;
    // Specialized transfer deliberately does not call generic submitCopy:
    // arbitrary incoming Scene copies invalidate origins, this qualified writer
    // instead publishes its captured component revisions after submission.
    requireOwner();return submitted;
}

std::shared_ptr<NativeSceneFeedbackFrame> NativeBackend::captureSceneFeedback(uint64_t serial,const std::shared_ptr<NativeCopySubmission>& rawFrontCopy) {
    flushIm2D();validateUiCompositeOwners();
    const auto& r=*uiComposite->resources;
    if(!serial||!rawFrontCopy||rawFrontCopy->owner!=this||!rawFrontCopy->submitted||!rawFrontCopy->event||
       rawFrontCopy->sourceResource.Get()!=r.outputColor->texture.Get()||pendingCopies.empty()||pendingCopies.back()!=rawFrontCopy)
        throw Error("Scene feedback capture requires this frame's latest actual submitted raw front copy");
    auto expected=temporalSceneOrigins();
    if(expected.color!=r.sceneColor||expected.depth!=r.sceneDepth)throw Error("Scene feedback capture does not own the registered default Scene pair");
    validJitter(r.sourceJitter);for(const auto& component:expected.components)if(!component.compatible(r.sourceJitter))throw Error("Scene feedback capture has unknown or mismatched source origins");
    if(sceneFeedbackPipeline&&sceneFeedbackPipeline->device.Get()==device.Get()&&sceneFeedbackPipeline->immediate.Get()==context.Get()&&
       (serial<=sceneFeedbackPipeline->lastSerial||(sceneFeedbackPipeline->lastFrame&&sceneFeedbackPipeline->lastFrame->rawFrontCopy==rawFrontCopy)))
        throw Error("Scene feedback frame serial was reused");
    auto frame=std::make_shared<NativeSceneFeedbackFrame>();frame->owner=this;frame->device=device;frame->context=context;
    frame->expected=expected;frame->rawColor=r.outputColor;frame->rawDepth=r.outputDepth;frame->sourceJitter=r.sourceJitter;frame->serial=serial;frame->rawFrontCopy=rawFrontCopy;
    frame->rawColorResource=r.outputColor->texture;frame->rawColorView=r.outputColor->view;frame->rawColorSampledView=r.outputColor->sampledView;
    frame->rawDepthResource=r.outputDepth->texture;frame->rawDepthView=r.outputDepth->view;
    frame->rawDepthSampledView=r.outputDepth->depthView;frame->rawStencilSampledView=r.outputDepth->stencilView;
    if(!sceneFeedbackPipeline||sceneFeedbackPipeline->device.Get()!=device.Get()||sceneFeedbackPipeline->immediate.Get()!=context.Get())
        sceneFeedbackPipeline=std::make_shared<SceneFeedbackPipeline>(device.Get(),context.Get());
    auto pipeline=sceneFeedbackPipeline;
    if(pipeline->snapshotGeneration==std::numeric_limits<uint64_t>::max())throw Error("Scene feedback canonical snapshot generation exhausted");
    if(!pipeline->capturedColor||pipeline->capturedColor->width!=r.logicalWidth||pipeline->capturedColor->height!=r.logicalHeight||
       pipeline->capturedColor->physicalWidth!=r.outputWidth||pipeline->capturedColor->physicalHeight!=r.outputHeight) {
        auto color=createTarget(r.outputWidth,r.outputHeight,TargetFormat::RGB10A2,TargetScale::Fixed);
        auto depth=createDepthTarget(r.outputWidth,r.outputHeight,TargetScale::Fixed);
        color->width=r.logicalWidth;color->height=r.logicalHeight;depth->width=r.logicalWidth;depth->height=r.logicalHeight;
        tagRenderExtent(color->texture.Get(),color->width,color->height);tagRenderExtent(depth->texture.Get(),depth->width,depth->height);
        pipeline->capturedColor=std::move(color);pipeline->capturedDepth=std::move(depth);
    }
    frame->capturedColor=pipeline->capturedColor;frame->capturedDepth=pipeline->capturedDepth;
    frame->capturedColorResource=frame->capturedColor->texture;frame->capturedColorView=frame->capturedColor->view;frame->capturedColorSampledView=frame->capturedColor->sampledView;
    frame->capturedDepthResource=frame->capturedDepth->texture;frame->capturedDepthView=frame->capturedDepth->view;
    frame->capturedDepthSampledView=frame->capturedDepth->depthView;frame->capturedStencilSampledView=frame->capturedDepth->stencilView;
    // Reusing a cached texture changes its contents even while older strong
    // owners remain live. Invalidate replay authority before the queued writes.
    frame->snapshotGeneration=++pipeline->snapshotGeneration;
    frame->snapshotCompletion=submitSceneFeedback(frame->rawColor,frame->rawDepth,frame->capturedColor,frame->capturedDepth,{},7,false,{});
    return frame;
}
std::shared_ptr<NativeCopySubmission> NativeBackend::commitSceneFeedback(const std::shared_ptr<NativeSceneFeedbackFrame>& frame) {
    flushIm2D();validateSubmissionContext();
    if(!frame||frame->owner!=this||frame->device.Get()!=device.Get()||frame->context.Get()!=context.Get()||!frame->serial||
       !frame->rawFrontCopy||frame->rawFrontCopy->owner!=this||!frame->rawFrontCopy->submitted||!frame->rawFrontCopy->event||
       frame->rawFrontCopy->sourceResource.Get()!=frame->rawColor->texture.Get())throw Error("Scene feedback commit lost its captured raw owner/submission");
    validateUiCompositeOwners();const auto& r=*uiComposite->resources;
    if(r.sceneColor!=frame->expected.color||r.sceneDepth!=frame->expected.depth||r.outputColor!=frame->rawColor||r.outputDepth!=frame->rawDepth||r.sourceJitter!=frame->sourceJitter)
        throw Error("Scene feedback commit active owner bundle changed after capture");
    if(!sceneFeedbackViewsUnchanged(frame))throw Error("Scene feedback commit captured actual resources/views changed within a live wrapper");
    if(sceneFeedbackPipeline&&sceneFeedbackPipeline->device.Get()==device.Get()&&sceneFeedbackPipeline->immediate.Get()==context.Get()&&
       (frame->serial<=sceneFeedbackPipeline->lastSerial||(sceneFeedbackPipeline->lastFrame&&sceneFeedbackPipeline->lastFrame->rawFrontCopy==frame->rawFrontCopy)))
        throw Error("Scene feedback commit frame serial was reused");
    if(!sceneFeedbackPipeline||frame->snapshotGeneration!=sceneFeedbackPipeline->snapshotGeneration||
       frame->capturedColor!=sceneFeedbackPipeline->capturedColor||frame->capturedDepth!=sceneFeedbackPipeline->capturedDepth||
       !frame->snapshotCompletion||frame->snapshotCompletion->owner!=this||!frame->snapshotCompletion->submitted||!frame->snapshotCompletion->event||
       frame->snapshotCompletion->sourceResource.Get()!=frame->rawColor->texture.Get()||
       frame->snapshotCompletion->destinationResource.Get()!=frame->capturedColor->texture.Get())
        throw Error("Scene feedback commit canonical snapshot contents were replaced");
    validateTemporalSceneFeedback(frame->expected,7);
    auto result=submitSceneFeedback(frame->capturedColor,frame->capturedDepth,frame->expected.color,frame->expected.depth,{},7,false,frame);
    publishTemporalSceneFeedback(frame->expected,7,{});
    sceneFeedbackPipeline->lastFrame=frame;sceneFeedbackPipeline->baseline=temporalSceneOrigins();sceneFeedbackPipeline->lastSerial=frame->serial;
    return result;
}
std::shared_ptr<NativeCopySubmission> NativeBackend::alignSceneFeedback(const std::shared_ptr<RenderTarget>& color,
    const std::shared_ptr<DepthTarget>& depth,std::array<float,2> nextJitter,uint32_t preserveMask) {
    validJitter(nextJitter);if(preserveMask&~7u)throw Error("Scene feedback preserve mask is invalid");
    flushIm2D();validateSubmissionContext();if(!preserveMask)return {};
    if(uiComposite)throw Error("Next Scene feedback alignment requires original target routing");
    auto expected=temporalSceneOrigins();
    if(expected.color!=color||expected.depth!=depth)throw Error("Next Scene feedback alignment lost registered owners");
    for(size_t i=0;i<3;++i)if((preserveMask&(1u<<i))&&!expected.components[i].compatible({}))
        throw Error("Next Scene feedback alignment requires qualified selected Grid0/Uniform components");
    validateTemporalSceneFeedback(expected,preserveMask);
    if(!sceneFeedbackPipeline||sceneFeedbackPipeline->device.Get()!=device.Get()||sceneFeedbackPipeline->immediate.Get()!=context.Get())
        sceneFeedbackPipeline=std::make_shared<SceneFeedbackPipeline>(device.Get(),context.Get());
    auto pipeline=sceneFeedbackPipeline;std::shared_ptr<RenderTarget> sourceColor;std::shared_ptr<DepthTarget> sourceDepth;std::shared_ptr<void> lease;
    bool unchanged=pipeline->lastFrame&&pipeline->baseline&&pipeline->baseline->color==color&&pipeline->baseline->depth==depth&&
        pipeline->lastFrame->snapshotGeneration==pipeline->snapshotGeneration&&
        pipeline->lastFrame->capturedColor==pipeline->capturedColor&&pipeline->lastFrame->capturedDepth==pipeline->capturedDepth&&
        sceneFeedbackViewsUnchanged(pipeline->lastFrame);
    if(unchanged)for(size_t i=0;i<3;++i)if((preserveMask&(1u<<i))&&expected.components[i].revision!=pipeline->baseline->components[i].revision)unchanged=false;
    if(unchanged) {sourceColor=pipeline->lastFrame->capturedColor;sourceDepth=pipeline->lastFrame->capturedDepth;lease=pipeline->lastFrame;}
    else {
        // An intervening write invalidates replay authority. Snapshot ALL selected
        // components onto one Grid0 source so alpha/D/S retain a paired texel.
        validateFrontTarget(color);validateDepthCopyTarget(depth);
        if(!pipeline->scratchColor||pipeline->scratchColor->width!=color->width||pipeline->scratchColor->height!=color->height||
           pipeline->scratchColor->physicalWidth!=color->physicalWidth||pipeline->scratchColor->physicalHeight!=color->physicalHeight) {
            auto c=createTarget(color->physicalWidth,color->physicalHeight,TargetFormat::RGB10A2,TargetScale::Fixed);
            auto d=createDepthTarget(color->physicalWidth,color->physicalHeight,TargetScale::Fixed);
            c->width=color->width;c->height=color->height;d->width=color->width;d->height=color->height;
            tagRenderExtent(c->texture.Get(),c->width,c->height);tagRenderExtent(d->texture.Get(),d->width,d->height);
            pipeline->scratchColor=std::move(c);pipeline->scratchDepth=std::move(d);
        }
        sourceColor=pipeline->scratchColor;sourceDepth=pipeline->scratchDepth;
        // Direct specialized copy keeps registered Scene revisions unchanged.
        lease=submitSceneFeedback(color,depth,sourceColor,sourceDepth,{},preserveMask,false,{});
    }
    auto result=submitSceneFeedback(sourceColor,sourceDepth,color,depth,nextJitter,preserveMask,false,lease);
    publishTemporalSceneFeedback(expected,preserveMask,nextJitter);return result;
}
bool NativeBackend::sceneFeedbackViewsUnchanged(const std::shared_ptr<NativeSceneFeedbackFrame>& frame) const {
    validateSubmissionContext();
    return frame&&frame->rawColor&&frame->rawDepth&&frame->capturedColor&&frame->capturedDepth&&
        frame->rawColor->texture.Get()==frame->rawColorResource.Get()&&frame->rawColor->view.Get()==frame->rawColorView.Get()&&
        frame->rawColor->sampledView.Get()==frame->rawColorSampledView.Get()&&frame->rawDepth->texture.Get()==frame->rawDepthResource.Get()&&
        frame->rawDepth->view.Get()==frame->rawDepthView.Get()&&frame->rawDepth->depthView.Get()==frame->rawDepthSampledView.Get()&&
        frame->rawDepth->stencilView.Get()==frame->rawStencilSampledView.Get()&&
        frame->capturedColor->texture.Get()==frame->capturedColorResource.Get()&&frame->capturedColor->view.Get()==frame->capturedColorView.Get()&&
        frame->capturedColor->sampledView.Get()==frame->capturedColorSampledView.Get()&&frame->capturedDepth->texture.Get()==frame->capturedDepthResource.Get()&&
        frame->capturedDepth->view.Get()==frame->capturedDepthView.Get()&&frame->capturedDepth->depthView.Get()==frame->capturedDepthSampledView.Get()&&
        frame->capturedDepth->stencilView.Get()==frame->capturedStencilSampledView.Get();
}
ComPtr<ID3D11Buffer> NativeBackend::sceneFeedbackTransferConstants() const {
    validateSubmissionContext();
    if(!sceneFeedbackPipeline||sceneFeedbackPipeline->device.Get()!=device.Get()||sceneFeedbackPipeline->immediate.Get()!=context.Get())
        throw Error("Scene feedback constants lack a matching actual cached device/context");
    return sceneFeedbackPipeline->constants;
}
std::array<ComPtr<ID3D11Texture2D>,2> NativeBackend::sceneFeedbackScratchResources() const {
    validateSubmissionContext();
    if(!sceneFeedbackPipeline||sceneFeedbackPipeline->device.Get()!=device.Get()||sceneFeedbackPipeline->immediate.Get()!=context.Get())
        throw Error("Scene feedback scratch cache lacks a matching actual device/context");
    const auto& p=*sceneFeedbackPipeline;
    return {p.scratchColor?p.scratchColor->texture:ComPtr<ID3D11Texture2D>{},p.scratchDepth?p.scratchDepth->texture:ComPtr<ID3D11Texture2D>{}};
}
std::array<ComPtr<ID3D11Texture2D>,2> NativeBackend::sceneFeedbackCapturedResources() const {
    validateSubmissionContext();
    if(!sceneFeedbackPipeline||sceneFeedbackPipeline->device.Get()!=device.Get()||sceneFeedbackPipeline->immediate.Get()!=context.Get())
        throw Error("Scene feedback canonical cache lacks a matching actual device/context");
    const auto& p=*sceneFeedbackPipeline;
    return {p.capturedColor?p.capturedColor->texture:ComPtr<ID3D11Texture2D>{},p.capturedDepth?p.capturedDepth->texture:ComPtr<ID3D11Texture2D>{}};
}
}
