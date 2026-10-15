#include "ui_composite.h"
#include "native_copy_submission.h"
#include <d3d11_1.h>
#include <d3d11_3.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <limits>
#include "VSUiComposite.h"
#include "PSUiCompositeDepth.h"
#include "PSUiCompositeDepthOnly.h"
#include "PSUiCompositeStencilBit.h"

namespace Simpsons::Graphics {
namespace {
void check(HRESULT result,const char* operation) {
    if(FAILED(result)) {
        char message[180];std::snprintf(message,sizeof(message),"Native UI composite %s failed: 0x%08lX",operation,ULONG(result));
        throw Error(message);
    }
}
ComPtr<ID3D11Buffer> constant(ID3D11Device* device,const std::array<uint32_t,4>& value) {
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(value);desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    const D3D11_SUBRESOURCE_DATA data{value.data(),0,0};ComPtr<ID3D11Buffer> result;
    check(device->CreateBuffer(&desc,&data,&result),"immutable transfer constant");return result;
}
struct TransferConstants {
    std::array<uint32_t,4> extent;
    std::array<float,2> sourceJitter;
    std::array<float,2> padding{};
};
static_assert(sizeof(TransferConstants)==32);
void validateJitter(const std::array<float,2>& sourceJitter) {
    // temporalJitter produces signed render-pixel offsets in [-0.5,0.5].
    // Reject invalid captured frame data before any command or route mutation.
    for(float component:sourceJitter)
        if(!std::isfinite(component)||std::abs(component)>0.5f)
            throw Error("Native UI composite source jitter must be finite and within half a render pixel");
}
void requireUnpredicated(ID3D11DeviceContext* context) {
    ComPtr<ID3D11Predicate> predicate;BOOL value{};context->GetPredication(&predicate,&value);
    if(predicate)throw Error("Native UI composite requires unpredicated resource operations");
}
bool viewOf(ID3D11View* view,ID3D11Resource* resource) {
    if(!view)return false;ComPtr<ID3D11Resource> actual;view->GetResource(&actual);return actual.Get()==resource;
}
void rejectOutputAliases(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Resource* color,ID3D11Resource* depth) {
    bool alias=false;
    auto reads=[&](auto getter) {
        std::array<ID3D11ShaderResourceView*,128> views{};(context->*getter)(0,UINT(views.size()),views.data());
        for(auto* view:views)if(view){alias=alias||viewOf(view,color)||viewOf(view,depth);view->Release();}
    };
    reads(&ID3D11DeviceContext::PSGetShaderResources);reads(&ID3D11DeviceContext::VSGetShaderResources);
    reads(&ID3D11DeviceContext::GSGetShaderResources);reads(&ID3D11DeviceContext::HSGetShaderResources);
    reads(&ID3D11DeviceContext::DSGetShaderResources);reads(&ID3D11DeviceContext::CSGetShaderResources);
    std::array<ID3D11UnorderedAccessView*,64> writes{};
    const UINT count=device->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?64u:8u;
    context->CSGetUnorderedAccessViews(0,count,writes.data());
    for(auto* view:writes)if(view){alias=alias||viewOf(view,color)||viewOf(view,depth);view->Release();}
    if(alias)throw Error("Native UI composite output is retained as a shader input or unordered output");
}
}

struct UiCompositeCopyRoles {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> immediate;
    std::shared_ptr<RenderTarget> sceneColor,outputColor;
    std::shared_ptr<DepthTarget> sceneDepth,outputDepth;
    uint32_t logicalWidth{},logicalHeight{},sourceWidth{},sourceHeight{},outputWidth{},outputHeight{};
};

struct UiCompositePipeline {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> immediate;
    ComPtr<ID3D11DeviceContext1> context;
    ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> paired,depthOnly,stencilBit;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depthStencil,depthWrite;
    std::array<ComPtr<ID3D11DepthStencilState>,8> stencilWrite;
    std::array<ComPtr<ID3D11Buffer>,8> bits;
    ComPtr<ID3D11Buffer> extents;
    std::shared_ptr<DepthTarget> outputDepth;
    uint32_t logicalWidth{},logicalHeight{};
    bool shaderStencilRef{};
    UiCompositePipeline(ID3D11Device* owner,ID3D11DeviceContext* submission):device(owner),immediate(submission) {
        // Once per strong owning device/context. An absent optional stencil
        // feature selects the exact eight-bit path, never an unsupported skip.
        for(auto format:{DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,DXGI_FORMAT_X32_TYPELESS_G8X24_UINT}) {
            UINT support{};check(device->CheckFormatSupport(format,&support),"typed depth/stencil format query");
            if(!(support&D3D11_FORMAT_SUPPORT_SHADER_LOAD))throw Error("Native UI composite typed depth/stencil loads are unsupported");
        }
        UINT depthSupport{};check(device->CheckFormatSupport(DXGI_FORMAT_D32_FLOAT_S8X24_UINT,&depthSupport),"depth output format query");
        if(!(depthSupport&D3D11_FORMAT_SUPPORT_DEPTH_STENCIL))throw Error("Native UI composite depth/stencil output is unsupported");
        D3D11_FEATURE_DATA_D3D11_OPTIONS2 options{};
        shaderStencilRef=SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2,&options,sizeof(options)))&&options.PSSpecifiedStencilRefSupported;
        ComPtr<ID3D11Device1> device1;check(device.As(&device1),"device-state interface");
        check(immediate.As(&context),"context-state interface");
        const auto level=device->GetFeatureLevel();
        const UINT flags=(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
        check(device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&state),"isolated context state");
        check(device->CreateVertexShader(kVSUiComposite,sizeof(kVSUiComposite),nullptr,&vertex),"transfer vertex shader");
        if(shaderStencilRef)check(device->CreatePixelShader(kPSUiCompositeDepth,sizeof(kPSUiCompositeDepth),nullptr,&paired),"paired transfer shader");
        check(device->CreatePixelShader(kPSUiCompositeDepthOnly,sizeof(kPSUiCompositeDepthOnly),nullptr,&depthOnly),"depth transfer shader");
        check(device->CreatePixelShader(kPSUiCompositeStencilBit,sizeof(kPSUiCompositeStencilBit),nullptr,&stencilBit),"stencil-bit transfer shader");
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
        check(device->CreateRasterizerState(&r,&raster),"transfer raster state");
        D3D11_DEPTH_STENCIL_DESC d{};d.DepthEnable=TRUE;d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;d.DepthFunc=D3D11_COMPARISON_ALWAYS;
        d.StencilEnable=TRUE;d.StencilReadMask=255;d.StencilWriteMask=255;
        d.FrontFace={D3D11_STENCIL_OP_REPLACE,D3D11_STENCIL_OP_REPLACE,D3D11_STENCIL_OP_REPLACE,D3D11_COMPARISON_ALWAYS};d.BackFace=d.FrontFace;
        check(device->CreateDepthStencilState(&d,&depthStencil),"paired transfer state");
        d.StencilEnable=FALSE;d.StencilWriteMask=0;check(device->CreateDepthStencilState(&d,&depthWrite),"depth transfer state");
        d.DepthEnable=FALSE;d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;d.StencilEnable=TRUE;
        d.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_REPLACE,D3D11_COMPARISON_ALWAYS};d.BackFace=d.FrontFace;
        for(uint32_t bit=0;bit<8;++bit) {
            d.StencilWriteMask=UINT8(1u<<bit);check(device->CreateDepthStencilState(&d,&stencilWrite[bit]),"stencil-bit transfer state");
            bits[bit]=constant(device.Get(),{1u<<bit,0,0,0});
        }
        D3D11_BUFFER_DESC transfer{};transfer.ByteWidth=sizeof(TransferConstants);
        transfer.Usage=D3D11_USAGE_DYNAMIC;transfer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;transfer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        check(device->CreateBuffer(&transfer,nullptr,&extents),"cached mutable transfer constant");
    }
};

void NativeBackend::validateUiCompositeCopyRoles() const {
    validateSubmissionContext();if(!uiCompositeCopyRoles)return;
    const auto& r=*uiCompositeCopyRoles;
    if(r.device.Get()!=device.Get()||r.immediate.Get()!=context.Get())
        throw Error("Native UI shared copy cache belongs to another actual device/context");
    validateFrontTarget(r.sceneColor);validateFrontTarget(r.outputColor);validateDepthCopyTarget(r.sceneDepth);validateDepthCopyTarget(r.outputDepth);
    if(r.sceneColor->width!=r.logicalWidth||r.sceneColor->height!=r.logicalHeight||r.sceneDepth->width!=r.logicalWidth||r.sceneDepth->height!=r.logicalHeight||
       r.outputColor->width!=r.logicalWidth||r.outputColor->height!=r.logicalHeight||r.outputDepth->width!=r.logicalWidth||r.outputDepth->height!=r.logicalHeight||
       r.sceneColor->physicalWidth!=r.sourceWidth||r.sceneColor->physicalHeight!=r.sourceHeight||r.sceneDepth->physicalWidth!=r.sourceWidth||r.sceneDepth->physicalHeight!=r.sourceHeight||
       r.outputColor->physicalWidth!=r.outputWidth||r.outputColor->physicalHeight!=r.outputHeight||r.outputDepth->physicalWidth!=r.outputWidth||r.outputDepth->physicalHeight!=r.outputHeight||
       renderExtent(r.logicalWidth,r.logicalHeight,TargetScale::Scene)!=std::array<uint32_t,2>{r.sourceWidth,r.sourceHeight}||
       renderExtent(r.logicalWidth,r.logicalHeight,TargetScale::Output)!=std::array<uint32_t,2>{r.outputWidth,r.outputHeight})
        throw Error("Native UI shared copy immutable owner/extent profile changed");
}
void NativeBackend::registerUiCompositeCopyRoles(const std::shared_ptr<RenderTarget>& sceneColorCopy,
    const std::shared_ptr<DepthTarget>& sceneDepthCopy) {
    validateSubmissionContext();
    if(uiComposite)throw Error("Native UI shared copy registration cannot change an active composite");
    if(!temporalAntialiasing(antialiasingMode))throw Error("Native UI shared copy roles require a configured temporal mode");
    validateFrontTarget(sceneColorCopy);validateDepthCopyTarget(sceneDepthCopy);
    if(sceneColorCopy->width!=sceneDepthCopy->width||sceneColorCopy->height!=sceneDepthCopy->height||
       sceneColorCopy->physicalWidth!=sceneDepthCopy->physicalWidth||sceneColorCopy->physicalHeight!=sceneDepthCopy->physicalHeight||
       renderExtent(sceneColorCopy->width,sceneColorCopy->height,TargetScale::Scene)!=std::array<uint32_t,2>{sceneColorCopy->physicalWidth,sceneColorCopy->physicalHeight})
        throw Error("Native UI shared copies require an exact Single Scene pair");
    const auto output=renderExtent(sceneColorCopy->width,sceneColorCopy->height,TargetScale::Output);
    if(uiCompositeCopyRoles&&uiCompositeCopyRoles->device.Get()==device.Get()&&uiCompositeCopyRoles->immediate.Get()==context.Get()&&
       uiCompositeCopyRoles->sceneColor==sceneColorCopy&&uiCompositeCopyRoles->sceneDepth==sceneDepthCopy&&
       uiCompositeCopyRoles->outputWidth==output[0]&&uiCompositeCopyRoles->outputHeight==output[1]) {
        validateUiCompositeCopyRoles();return; // No allocation/epoch change for the identical qualified cache.
    }
    if(uiCompositeCopyEpoch==std::numeric_limits<uint64_t>::max())throw Error("Native UI shared copy generation exhausted");
    auto pending=std::make_shared<UiCompositeCopyRoles>();pending->device=device;pending->immediate=context;
    pending->sceneColor=sceneColorCopy;pending->sceneDepth=sceneDepthCopy;
    pending->logicalWidth=sceneColorCopy->width;pending->logicalHeight=sceneColorCopy->height;
    pending->sourceWidth=sceneColorCopy->physicalWidth;pending->sourceHeight=sceneColorCopy->physicalHeight;
    pending->outputWidth=output[0];pending->outputHeight=output[1];
    pending->outputColor=createTarget(pending->logicalWidth,pending->logicalHeight,TargetFormat::RGB10A2,TargetScale::Output);
    pending->outputDepth=createDepthTarget(pending->logicalWidth,pending->logicalHeight,TargetScale::Output);
    validateFrontTarget(pending->outputColor);validateDepthCopyTarget(pending->outputDepth);
    if(pending->outputColor->physicalWidth!=output[0]||pending->outputColor->physicalHeight!=output[1]||
       pending->outputDepth->physicalWidth!=output[0]||pending->outputDepth->physicalHeight!=output[1]||
       pending->outputColor->width!=pending->logicalWidth||pending->outputColor->height!=pending->logicalHeight||
       pending->outputDepth->width!=pending->logicalWidth||pending->outputDepth->height!=pending->logicalHeight||
       pending->outputColor->texture.Get()==sceneColorCopy->texture.Get()||pending->outputDepth->texture.Get()==sceneDepthCopy->texture.Get())
        throw Error("Native UI shared Output cache has invalid or aliased physical backings");
    // Both allocations and actual views qualify before replacing any strong role.
    uiCompositeCopyRoles=std::move(pending);++uiCompositeCopyEpoch;
}
uint64_t NativeBackend::uiCompositeCopyGeneration() const {
    validateSubmissionContext();
    if(uiComposite)validateUiCompositeOwners();else validateUiCompositeCopyRoles();
    return temporalAntialiasing(antialiasingMode)&&uiCompositeCopyRoles?uiCompositeCopyEpoch:0;
}

std::shared_ptr<NativeUiCompositeBundle> NativeBackend::promoteUiComposite(const std::shared_ptr<RenderTarget>& sceneColor,
    const std::shared_ptr<DepthTarget>& sceneDepth,const std::shared_ptr<RenderTarget>& reconstructedColor,std::array<float,2> sourceJitter) {
    return promoteUiCompositeImpl(sceneColor,sceneDepth,reconstructedColor,false,sourceJitter);
}
std::shared_ptr<NativeUiCompositeBundle> NativeBackend::promoteUiCompositeImpl(const std::shared_ptr<RenderTarget>& sceneColor,
    const std::shared_ptr<DepthTarget>& sceneDepth,const std::shared_ptr<RenderTarget>& reconstructedColor,bool forceStencilFallback,
    std::array<float,2> sourceJitter) {
    validateJitter(sourceJitter);
    flushIm2D();validateSubmissionContext();retireCopies();
    if(uiComposite)throw Error("Native UI composite is already active");
    validateFrontTarget(sceneColor);validateFrontTarget(reconstructedColor);validateDepthCopyTarget(sceneDepth);
    requireUnpredicated(context.Get());requireNoOutputUavs();
    if(sceneColor->width!=sceneDepth->width||sceneColor->height!=sceneDepth->height||
       sceneColor->width!=reconstructedColor->width||sceneColor->height!=reconstructedColor->height||
       sceneColor->physicalWidth!=sceneDepth->physicalWidth||sceneColor->physicalHeight!=sceneDepth->physicalHeight||
       sceneColor->texture.Get()==reconstructedColor->texture.Get())
        throw Error("Native UI composite requires distinct single-sample output and paired logical/physical scene owners");
    requireSelectedTargets({sceneColor,nullptr,nullptr,nullptr},sceneDepth);
    validateUiCompositeCopyRoles();
    if(uiCompositeCopyRoles) {
        const auto& copies=*uiCompositeCopyRoles;
        if(uiCompositeCopyEpoch==std::numeric_limits<uint64_t>::max())throw Error("Native UI shared copy generation exhausted");
        if(copies.logicalWidth!=sceneColor->width||copies.logicalHeight!=sceneColor->height||
           copies.sourceWidth!=sceneColor->physicalWidth||copies.sourceHeight!=sceneColor->physicalHeight||
           copies.outputWidth!=reconstructedColor->physicalWidth||copies.outputHeight!=reconstructedColor->physicalHeight||
           copies.sceneColor->texture.Get()==sceneColor->texture.Get()||copies.sceneColor->texture.Get()==reconstructedColor->texture.Get()||
           copies.sceneDepth->texture.Get()==sceneDepth->texture.Get()||copies.outputColor->texture.Get()==reconstructedColor->texture.Get())
            throw Error("Native UI shared copy roles do not match distinct immutable scene/output owners");
    }
    if(!uiCompositePipeline||uiCompositePipeline->device.Get()!=device.Get()||uiCompositePipeline->immediate.Get()!=context.Get())
        uiCompositePipeline=std::make_shared<UiCompositePipeline>(device.Get(),context.Get());
    auto& pipeline=*uiCompositePipeline;
    if(!pipeline.outputDepth||pipeline.outputDepth->physicalWidth!=reconstructedColor->physicalWidth||
       pipeline.outputDepth->physicalHeight!=reconstructedColor->physicalHeight||pipeline.logicalWidth!=sceneColor->width||pipeline.logicalHeight!=sceneColor->height) {
        auto depth=createDepthTarget(reconstructedColor->physicalWidth,reconstructedColor->physicalHeight,TargetScale::Fixed);
        depth->width=sceneColor->width;depth->height=sceneColor->height;tagRenderExtent(depth->texture.Get(),depth->width,depth->height);
        pipeline.outputDepth=std::move(depth);pipeline.logicalWidth=sceneColor->width;pipeline.logicalHeight=sceneColor->height;
    }
    const auto outputDepth=pipeline.outputDepth;validateDepthCopyTarget(outputDepth);
    rejectOutputAliases(device.Get(),context.Get(),reconstructedColor->texture.Get(),outputDepth->texture.Get());
    if(uiCompositeCopyRoles) {
        const auto& copies=*uiCompositeCopyRoles;
        const std::array<ID3D11Resource*,8> owners{sceneColor->texture.Get(),sceneDepth->texture.Get(),reconstructedColor->texture.Get(),outputDepth->texture.Get(),
            copies.sceneColor->texture.Get(),copies.sceneDepth->texture.Get(),copies.outputColor->texture.Get(),copies.outputDepth->texture.Get()};
        for(size_t i=0;i<owners.size();++i)for(size_t j=0;j<i;++j)
            if(owners[i]==owners[j])throw Error("Native UI shared copy and active owners alias an immutable resource");
    }
    const std::array<uint32_t,4> extent{sceneDepth->physicalWidth,sceneDepth->physicalHeight,outputDepth->physicalWidth,outputDepth->physicalHeight};
    const TransferConstants transfer{extent,sourceJitter,{}};
    D3D11_MAPPED_SUBRESOURCE mapped{};
    // Immediate-queue ordering and WRITE_DISCARD renaming preserve prior draws
    // while reusing the same buffer object for changing frame jitter/extents.
    check(context->Map(pipeline.extents.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"update cached transfer constant");
    std::memcpy(mapped.pData,&transfer,sizeof(transfer));context->Unmap(pipeline.extents.Get(),0);
    auto bundle=std::make_shared<NativeUiCompositeBundle>();auto resources=std::make_shared<NativeUiCompositeBundle::Resources>();
    *resources={sceneColor,reconstructedColor,sceneDepth,outputDepth,sceneColor->width,sceneColor->height,extent[0],extent[1],extent[2],extent[3],sourceJitter,{},{},{},{},0};
    if(uiCompositeCopyRoles) {
        resources->sceneColorCopy=uiCompositeCopyRoles->sceneColor;resources->outputColorCopy=uiCompositeCopyRoles->outputColor;
        resources->sceneDepthCopy=uiCompositeCopyRoles->sceneDepth;resources->outputDepthCopy=uiCompositeCopyRoles->outputDepth;
        resources->copyGeneration=uiCompositeCopyEpoch+1;
    }
    bundle->resources=resources;
    auto submission=std::make_shared<NativeCopySubmission>();submission->owner=this;
    D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};check(device->CreateQuery(&query,&submission->event),"completion query");
    // The separate lease avoids a bundle->completion->bundle cycle and keeps
    // all captured original/output owners alive until the actual event retires.
    submission->sourceOwner=resources;submission->destinationOwner=outputDepth;
    submission->sourceResource=sceneDepth->texture;submission->destinationResource=outputDepth->texture;
    bundle->completion=submission;pendingCopies.push_back(submission);
    invalidateTemporalCopiedOrigin(outputDepth->texture.Get());
    if(extent[0]==extent[2]&&extent[1]==extent[3]&&!forceStencilFallback&&sourceJitter[0]==0.0f&&sourceJitter[1]==0.0f) {
        // Only zero-jitter DLAA has equal scene/output pixel centers. Preserve original
        // world ownership with an exact cached Single-depth resource copy;
        // neither a resize draw nor any depth/stencil conversion is needed.
        context->CopyResource(outputDepth->texture.Get(),sceneDepth->texture.Get());
        context->End(submission->event.Get());submission->submitted=true;
    }else {
        struct Restore {
            ID3D11DeviceContext1* context;ComPtr<ID3DDeviceContextState> original;
            ~Restore(){context->ClearState();context->SwapDeviceContextState(original.Get(),nullptr);}
        } restore{pipeline.context.Get()};
        pipeline.context->SwapDeviceContextState(pipeline.state.Get(),&restore.original);
        context->ClearState();context->OMSetRenderTargets(0,nullptr,outputDepth->view.Get());
        const D3D11_VIEWPORT viewport{0,0,float(extent[2]),float(extent[3]),0,1};context->RSSetViewports(1,&viewport);context->RSSetState(pipeline.raster.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(pipeline.vertex.Get(),nullptr,0);
        const std::array<ID3D11ShaderResourceView*,2> views{sceneDepth->depthView.Get(),sceneDepth->stencilView.Get()};
        context->PSSetShaderResources(0,UINT(views.size()),views.data());context->PSSetConstantBuffers(0,1,pipeline.extents.GetAddressOf());
        if(pipeline.shaderStencilRef&&!forceStencilFallback) {
            context->OMSetDepthStencilState(pipeline.depthStencil.Get(),0);context->PSSetShader(pipeline.paired.Get(),nullptr,0);context->Draw(3,0);
        }else {
            context->ClearDepthStencilView(outputDepth->view.Get(),D3D11_CLEAR_STENCIL,0,0);
            context->OMSetDepthStencilState(pipeline.depthWrite.Get(),0);context->PSSetShader(pipeline.depthOnly.Get(),nullptr,0);context->Draw(3,0);
            context->PSSetShader(pipeline.stencilBit.Get(),nullptr,0);
            for(uint32_t bit=0;bit<8;++bit) {
                context->OMSetDepthStencilState(pipeline.stencilWrite[bit].Get(),1u<<bit);
                context->PSSetConstantBuffers(1,1,pipeline.bits[bit].GetAddressOf());context->Draw(3,0);
            }
        }
        context->End(submission->event.Get());submission->submitted=true;
    }
    // Only after complete validation and isolated submission does routing become
    // visible. Failure retains GPU leases but never publishes a partial route.
    bindTargets({reconstructedColor,nullptr,nullptr,nullptr},outputDepth);
    if(uiCompositeCopyRoles)uiCompositeCopyEpoch=resources->copyGeneration;
    uiComposite=bundle;return bundle;
}
ComPtr<ID3D11Buffer> NativeBackend::uiCompositeTransferConstants() const {
    validateSubmissionContext();
    if(!uiCompositePipeline||uiCompositePipeline->device.Get()!=device.Get()||uiCompositePipeline->immediate.Get()!=context.Get())
        throw Error("Native UI composite transfer constants have no matching cached owner");
    return uiCompositePipeline->extents;
}
void NativeBackend::validateUiCompositeOwners() const {
    validateSubmissionContext();if(!uiComposite||!uiComposite->resources)throw Error("Native UI composite has no active owner bundle");
    const auto& r=*uiComposite->resources;
    validateUiCompositeCopyRoles();
    if(uiCompositeCopyRoles) {
        const auto& copies=*uiCompositeCopyRoles;
        if(r.sceneColorCopy!=copies.sceneColor||r.outputColorCopy!=copies.outputColor||r.sceneDepthCopy!=copies.sceneDepth||r.outputDepthCopy!=copies.outputDepth||
           r.copyGeneration!=uiCompositeCopyEpoch||copies.logicalWidth!=r.logicalWidth||copies.logicalHeight!=r.logicalHeight||
           copies.sourceWidth!=r.sourceWidth||copies.sourceHeight!=r.sourceHeight||copies.outputWidth!=r.outputWidth||copies.outputHeight!=r.outputHeight)
            throw Error("Native UI active shared copies lost their captured role/generation");
    }else if(r.sceneColorCopy||r.outputColorCopy||r.sceneDepthCopy||r.outputDepthCopy||r.copyGeneration)
        throw Error("Native UI composite retained copies without a registered role");
    validateFrontTarget(r.sceneColor);validateFrontTarget(r.outputColor);validateDepthCopyTarget(r.sceneDepth);validateDepthCopyTarget(r.outputDepth);
    if(r.sceneColor->width!=r.logicalWidth||r.sceneColor->height!=r.logicalHeight||r.outputColor->width!=r.logicalWidth||r.outputColor->height!=r.logicalHeight||
       r.sceneDepth->width!=r.logicalWidth||r.sceneDepth->height!=r.logicalHeight||r.outputDepth->width!=r.logicalWidth||r.outputDepth->height!=r.logicalHeight||
       r.sceneColor->physicalWidth!=r.sourceWidth||r.sceneColor->physicalHeight!=r.sourceHeight||r.sceneDepth->physicalWidth!=r.sourceWidth||r.sceneDepth->physicalHeight!=r.sourceHeight||
       r.outputColor->physicalWidth!=r.outputWidth||r.outputColor->physicalHeight!=r.outputHeight||r.outputDepth->physicalWidth!=r.outputWidth||r.outputDepth->physicalHeight!=r.outputHeight)
        throw Error("Native UI composite immutable owner metadata changed");
}
std::shared_ptr<RenderTarget> NativeBackend::routeColor(const std::shared_ptr<RenderTarget>& owner) const {
    validateSubmissionContext();if(!uiComposite||!owner)return owner;validateUiCompositeOwners();
    const auto& r=*uiComposite->resources;
    if(owner.get()==r.sceneColor.get())return r.outputColor;
    return owner.get()==r.sceneColorCopy.get()?r.outputColorCopy:owner;
}
std::shared_ptr<DepthTarget> NativeBackend::routeDepth(const std::shared_ptr<DepthTarget>& owner) const {
    validateSubmissionContext();if(!uiComposite||!owner)return owner;validateUiCompositeOwners();
    const auto& r=*uiComposite->resources;
    if(owner.get()==r.sceneDepth.get())return r.outputDepth;
    return owner.get()==r.sceneDepthCopy.get()?r.outputDepthCopy:owner;
}
void NativeBackend::clearUiComposite() {
    flushIm2D();validateSubmissionContext();if(!uiComposite)return;validateUiCompositeOwners();requireNoOutputUavs();
    const auto& r=*uiComposite->resources;
    if(uiCompositeCopyRoles&&uiCompositeCopyEpoch==std::numeric_limits<uint64_t>::max())throw Error("Native UI shared copy generation exhausted");
    std::array<ID3D11RenderTargetView*,8> colors{};ComPtr<ID3D11DepthStencilView> depth;context->OMGetRenderTargets(8,colors.data(),&depth);
    bool output=colors[0]==r.outputColor->view.Get()&&depth.Get()==r.outputDepth->view.Get();
    bool original=colors[0]==r.sceneColor->view.Get()&&depth.Get()==r.sceneDepth->view.Get();
    for(size_t i=0;i<colors.size();++i){if(i&&colors[i]){output=false;original=false;}if(colors[i])colors[i]->Release();}
    if(!output&&!original)throw Error("Native UI composite cannot restore unrelated actual target owners");
    if(output) {
        rejectOutputAliases(device.Get(),context.Get(),r.sceneColor->texture.Get(),r.sceneDepth->texture.Get());
        bindTargets({r.sceneColor,nullptr,nullptr,nullptr},r.sceneDepth);
    }
    uiComposite.reset();if(uiCompositeCopyRoles)++uiCompositeCopyEpoch;requireOwner();
}
}
