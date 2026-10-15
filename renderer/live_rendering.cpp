#include "native_backend.h"
#include "antialiasing.h"
#include "runtime/stall_profiler.h"
#include <d3d11_1.h>
#include <cmath>
#include <cstdio>
#include "VSAntialiasing.h"
#include "PSLiveStretchColor.h"
#include "PSLiveStretchDepth.h"
#include "PSLiveStretchStencil.h"

namespace Simpsons::Graphics {
namespace {
void check(HRESULT result,const char* operation) {
    if(FAILED(result)) {
        char message[180];std::snprintf(message,sizeof(message),"Native live rendering %s failed: 0x%08lX",operation,ULONG(result));
        throw Error(message);
    }
}
struct StretchConstants {std::array<float,2> sourceScale;uint32_t stencilBit,padding;};
static_assert(sizeof(StretchConstants)==16);
bool same(const D3D11_VIEWPORT& a,const D3D11_VIEWPORT& b) {
    return a.TopLeftX==b.TopLeftX&&a.TopLeftY==b.TopLeftY&&a.Width==b.Width&&a.Height==b.Height&&
           a.MinDepth==b.MinDepth&&a.MaxDepth==b.MaxDepth;
}
bool same(const D3D11_RECT& a,const D3D11_RECT& b) {
    return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;
}
}
// Isolated context state, like the presentation filters: the engine's complete
// immediate-context state survives every content copy untouched.
struct LiveRenderingPipeline {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext1> context;
    ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> color,depth,stencil;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11DepthStencilState> disabled,depthWrite;
    std::array<ComPtr<ID3D11DepthStencilState>,8> stencilBits;
    ComPtr<ID3D11Buffer> constants;
    LiveRenderingPipeline(ID3D11Device* owner,ID3D11DeviceContext* immediate):device(owner) {
        ComPtr<ID3D11Device1> device1;
        check(device->QueryInterface(IID_PPV_ARGS(&device1)),"device-state interface");
        check(immediate->QueryInterface(IID_PPV_ARGS(&context)),"context-state interface");
        const auto level=device->GetFeatureLevel();
        const UINT flags=(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
        check(device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&state),"isolated context state");
        check(device->CreateVertexShader(kVSAntialiasing,sizeof(kVSAntialiasing),nullptr,&vertex),"fullscreen vertex shader");
        check(device->CreatePixelShader(kPSLiveStretchColor,sizeof(kPSLiveStretchColor),nullptr,&color),"color stretch shader");
        check(device->CreatePixelShader(kPSLiveStretchDepth,sizeof(kPSLiveStretchDepth),nullptr,&depth),"depth stretch shader");
        check(device->CreatePixelShader(kPSLiveStretchStencil,sizeof(kPSLiveStretchStencil),nullptr,&stencil),"stencil stretch shader");
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        s.MaxAnisotropy=1;s.ComparisonFunc=D3D11_COMPARISON_NEVER;s.MaxLOD=D3D11_FLOAT32_MAX;
        check(device->CreateSamplerState(&s,&sampler),"clamped linear sampler");
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
        check(device->CreateRasterizerState(&r,&raster),"fullscreen raster state");
        D3D11_BLEND_DESC b{};b.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        check(device->CreateBlendState(&b,&blend),"replacement blend state");
        D3D11_DEPTH_STENCIL_DESC d{};d.DepthFunc=D3D11_COMPARISON_ALWAYS;
        d.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};d.BackFace=d.FrontFace;
        check(device->CreateDepthStencilState(&d,&disabled),"disabled depth state");
        d.DepthEnable=TRUE;d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
        check(device->CreateDepthStencilState(&d,&depthWrite),"depth-only write state");
        // Each pass sets one destination stencil bit where the source has it.
        d.DepthEnable=FALSE;d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;d.StencilEnable=TRUE;d.StencilReadMask=0xFF;
        d.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_REPLACE,D3D11_COMPARISON_ALWAYS};d.BackFace=d.FrontFace;
        for(uint32_t bit=0;bit<stencilBits.size();++bit) {
            d.StencilWriteMask=UINT8(1u<<bit);
            check(device->CreateDepthStencilState(&d,&stencilBits[bit]),"stencil-bit write state");
        }
        D3D11_BUFFER_DESC c{};c.ByteWidth=sizeof(StretchConstants);c.Usage=D3D11_USAGE_DEFAULT;c.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&c,nullptr,&constants),"stretch constants");
    }
};
namespace {
struct IsolatedState {
    ID3D11DeviceContext1* context;
    ComPtr<ID3DDeviceContextState> original;
    IsolatedState(LiveRenderingPipeline& pipeline):context(pipeline.context.Get()) {
        context->SwapDeviceContextState(pipeline.state.Get(),&original);context->ClearState();
    }
    ~IsolatedState(){context->ClearState();context->SwapDeviceContextState(original.Get(),nullptr);}
};
}

ID3D11RenderTargetView* NativeBackend::LiveViewRemap::map(ID3D11RenderTargetView* view) const {
    if(view)for(const auto& entry:colors)if(entry.from.Get()==view)return entry.to.Get();
    return nullptr;
}
ID3D11DepthStencilView* NativeBackend::LiveViewRemap::map(ID3D11DepthStencilView* view) const {
    if(view)for(const auto& entry:depths)if(entry.from.Get()==view)return entry.to.Get();
    return nullptr;
}
ID3D11ShaderResourceView* NativeBackend::LiveViewRemap::map(ID3D11ShaderResourceView* view) const {
    if(!view)return nullptr;
    for(const auto& entry:colors)if(entry.sampledFrom.Get()==view)return entry.sampledTo.Get();
    for(const auto& entry:depths) {
        if(entry.depthFrom.Get()==view)return entry.depthTo.Get();
        if(entry.stencilFrom.Get()==view)return entry.stencilTo.Get();
    }
    return nullptr;
}
const std::array<float,2>* NativeBackend::LiveViewRemap::scale(ID3D11RenderTargetView* color,ID3D11DepthStencilView* depth) const {
    if(color)for(const auto& entry:colors)if(entry.from.Get()==color)return &entry.scale;
    if(depth)for(const auto& entry:depths)if(entry.from.Get()==depth)return &entry.scale;
    return nullptr;
}

void NativeBackend::stretchColor(const std::shared_ptr<RenderTarget>& source,const std::shared_ptr<RenderTarget>& destination) {
    if(!liveRenderingPipeline||liveRenderingPipeline->device.Get()!=device.Get())
        liveRenderingPipeline=std::make_shared<LiveRenderingPipeline>(device.Get(),context.Get());
    auto& p=*liveRenderingPipeline;
    IsolatedState isolated(p);
    const D3D11_VIEWPORT viewport{0,0,float(destination->physicalWidth),float(destination->physicalHeight),0,1};
    context->RSSetViewports(1,&viewport);context->RSSetState(p.raster.Get());
    context->OMSetBlendState(p.blend.Get(),nullptr,~0u);context->OMSetDepthStencilState(p.disabled.Get(),0);
    context->OMSetRenderTargets(1,destination->view.GetAddressOf(),nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(p.vertex.Get(),nullptr,0);context->PSSetShader(p.color.Get(),nullptr,0);
    context->PSSetSamplers(0,1,p.sampler.GetAddressOf());
    context->PSSetShaderResources(0,1,source->sampledView.GetAddressOf());
    context->Draw(3,0);
}
void NativeBackend::stretchDepth(const std::shared_ptr<DepthTarget>& source,const std::shared_ptr<DepthTarget>& destination) {
    if(!liveRenderingPipeline||liveRenderingPipeline->device.Get()!=device.Get())
        liveRenderingPipeline=std::make_shared<LiveRenderingPipeline>(device.Get(),context.Get());
    auto& p=*liveRenderingPipeline;
    IsolatedState isolated(p);
    const D3D11_VIEWPORT viewport{0,0,float(destination->physicalWidth),float(destination->physicalHeight),0,1};
    context->RSSetViewports(1,&viewport);context->RSSetState(p.raster.Get());
    context->ClearDepthStencilView(destination->view.Get(),D3D11_CLEAR_STENCIL,0,0);
    context->OMSetRenderTargets(0,nullptr,destination->view.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(p.vertex.Get(),nullptr,0);
    const std::array<ID3D11ShaderResourceView*,2> views{source->depthView.Get(),source->stencilView.Get()};
    context->PSSetShaderResources(1,UINT(views.size()),views.data());
    context->PSSetConstantBuffers(0,1,p.constants.GetAddressOf());
    StretchConstants values{{float(source->physicalWidth)/destination->physicalWidth,float(source->physicalHeight)/destination->physicalHeight},0,0};
    context->UpdateSubresource(p.constants.Get(),0,nullptr,&values,0,0);
    context->OMSetDepthStencilState(p.depthWrite.Get(),0);context->PSSetShader(p.depth.Get(),nullptr,0);context->Draw(3,0);
    context->PSSetShader(p.stencil.Get(),nullptr,0);
    for(uint32_t bit=0;bit<p.stencilBits.size();++bit) {
        values.stencilBit=1u<<bit;context->UpdateSubresource(p.constants.Get(),0,nullptr,&values,0,0);
        context->OMSetDepthStencilState(p.stencilBits[bit].Get(),0xFF);context->Draw(3,0);
    }
}
void NativeBackend::remapEngineBindings(const LiveViewRemap& remap,std::array<float,2> oldScale) {
    // Attachments: the same slots keep the same owners, now at new storage.
    std::array<ID3D11RenderTargetView*,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> raw{};ComPtr<ID3D11DepthStencilView> depth;
    context->OMGetRenderTargets(UINT(raw.size()),raw.data(),&depth);
    std::array<ComPtr<ID3D11RenderTargetView>,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> colors;
    for(size_t i=0;i<raw.size();++i)colors[i].Attach(raw[i]);
    bool changed=false;UINT count=0;
    for(size_t i=0;i<raw.size();++i) {
        if(auto* to=remap.map(colors[i].Get())){raw[i]=to;changed=true;}
        if(raw[i])count=UINT(i+1);
    }
    auto* depthView=depth.Get();
    if(auto* to=remap.map(depthView)){depthView=to;changed=true;}
    if(changed)context->OMSetRenderTargets(count,count?raw.data():nullptr,depthView);
    // Sampled targets in every shader stage.
    const auto remapStage=[&](auto get,auto set) {
        std::array<ID3D11ShaderResourceView*,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> views{};
        (context.Get()->*get)(0,UINT(views.size()),views.data());
        for(UINT slot=0;slot<views.size();++slot) {
            if(auto* to=remap.map(views[slot]))(context.Get()->*set)(slot,1,&to);
            if(views[slot])views[slot]->Release();
        }
    };
    remapStage(&ID3D11DeviceContext::VSGetShaderResources,&ID3D11DeviceContext::VSSetShaderResources);
    remapStage(&ID3D11DeviceContext::PSGetShaderResources,&ID3D11DeviceContext::PSSetShaderResources);
    remapStage(&ID3D11DeviceContext::GSGetShaderResources,&ID3D11DeviceContext::GSSetShaderResources);
    remapStage(&ID3D11DeviceContext::HSGetShaderResources,&ID3D11DeviceContext::HSSetShaderResources);
    remapStage(&ID3D11DeviceContext::DSGetShaderResources,&ID3D11DeviceContext::DSSetShaderResources);
    remapStage(&ID3D11DeviceContext::CSGetShaderResources,&ID3D11DeviceContext::CSSetShaderResources);
    // Physical rectangles follow the selected target's new scale. Logical
    // receipts that still describe the actual state are re-applied exactly.
    const auto newScale=selectedRenderScale();
    if(newScale==oldScale)return;
    const std::array ratio{newScale[0]/oldScale[0],newScale[1]/oldScale[1]};
    std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
    UINT viewportCount=UINT(viewports.size());context->RSGetViewports(&viewportCount,viewports.data());
    if(viewportReceipt.valid&&viewportCount==1&&viewportReceipt.scale==oldScale&&same(viewports[0],viewportReceipt.physical))
        setViewport(viewportReceipt.logical);
    else if(viewportCount) {
        for(UINT i=0;i<viewportCount;++i) {
            auto& v=viewports[i];v.TopLeftX*=ratio[0];v.Width*=ratio[0];v.TopLeftY*=ratio[1];v.Height*=ratio[1];
        }
        context->RSSetViewports(viewportCount,viewports.data());viewportReceipt.valid=false;
    }
    std::array<D3D11_RECT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors{};
    UINT scissorCount=UINT(scissors.size());context->RSGetScissorRects(&scissorCount,scissors.data());
    if(scissorReceipt.valid&&scissorCount==1&&scissorReceipt.scale==oldScale&&same(scissors[0],scissorReceipt.physical))
        setScissor(scissorReceipt.logical);
    else if(scissorCount) {
        for(UINT i=0;i<scissorCount;++i) {
            auto& r=scissors[i];
            r={LONG(std::floor(r.left*ratio[0])),LONG(std::floor(r.top*ratio[1])),LONG(std::ceil(r.right*ratio[0])),LONG(std::ceil(r.bottom*ratio[1]))};
        }
        context->RSSetScissorRects(scissorCount,scissors.data());scissorReceipt.valid=false;
    }
}
bool NativeBackend::configureCleanEyes(bool clean) {
    validateSubmissionContext();
    if(clean==cleanEyes_)return true;
    if(recordingSessionActive())return false;
    StallProfiler::Scope profile(StallProfiler::Section::Rendering,"Live.cleanEyes",nullptr);
    // Sealed skin command lists bound the previous flag's depth constants.
    flushIm2D();waitIdle();retireCopies();retireRecordingPayloads();
    cleanEyes_=clean;
    rerecordRecordingPayloads({});
    requireOwner();return true;
}
bool NativeBackend::reconfigureRendering(uint32_t width,uint32_t height,uint32_t anisotropy,Antialiasing aa,uint32_t percent,TextureFilter filter,uint32_t shadows) {
    validateSubmissionContext();
    if(renderingMatches(width,height,anisotropy,aa,percent,filter,shadows))return true;
    if(shadows>3)throw Error("Unsupported native shadow quality");
    if(temporalAntialiasing(aa)||temporalAntialiasing(antialiasingMode))throw Error("Native temporal rendering cannot change live");
    if(!supportsRendering(width,height,anisotropy,aa,percent))throw Error("Unsupported internal rendering settings or scene pixel budget exceeded");
    // A recording session owns its deferred context; retry at a later boundary.
    if(recordingSessionActive())return false;
    StallProfiler::Scope profile(StallProfiler::Section::Rendering,"Live.reconfigureRendering",nullptr);
    // Every queued use of the current storage, copy and recording retires first.
    flushIm2D();waitIdle();retireCopies();retireRecordingPayloads();
    const bool filteringChanged=anisotropy!=textureAnisotropy||filter!=textureFilterMode;
    struct Configuration {
        Antialiasing aa;uint32_t baseWidth,baseHeight,internalWidth,internalHeight,outputWidth,outputHeight,anisotropy,percent;
        TextureFilter filter;uint32_t shadows;
    };
    const Configuration previous{antialiasingMode,renderBaseWidth,renderBaseHeight,internalWidth,internalHeight,
        outputWidth,outputHeight,textureAnisotropy,renderScalePercent,textureFilterMode,shadowQualityLevel};
    const auto restore=[&] {
        antialiasingMode=previous.aa;renderBaseWidth=previous.baseWidth;renderBaseHeight=previous.baseHeight;
        internalWidth=previous.internalWidth;internalHeight=previous.internalHeight;
        outputWidth=previous.outputWidth;outputHeight=previous.outputHeight;
        textureAnisotropy=previous.anisotropy;renderScalePercent=previous.percent;textureFilterMode=previous.filter;
        shadowQualityLevel=previous.shadows;
    };
    antialiasingMode=aa;renderBaseWidth=width;renderBaseHeight=height;
    internalWidth=uint32_t((uint64_t(width)*percent+50)/100);internalHeight=uint32_t((uint64_t(height)*percent+50)/100);
    outputWidth=internalWidth;outputHeight=internalHeight;textureAnisotropy=anisotropy;renderScalePercent=percent;textureFilterMode=filter;
    shadowQualityLevel=shadows;
    struct StagedColor {std::shared_ptr<RenderTarget> owner,fresh;};
    struct StagedDepth {std::shared_ptr<DepthTarget> owner,fresh;};
    std::vector<StagedColor> colors;std::vector<StagedDepth> depths;
    try {
        // Allocate every new storage before any owner changes; snapshot the
        // registries because each allocation registers its own temporary owner.
        std::erase_if(liveColorTargets,[](const auto& entry){return entry.expired();});
        std::erase_if(liveDepthTargets,[](const auto& entry){return entry.expired();});
        const auto colorOwners=liveColorTargets;const auto depthOwners=liveDepthTargets;
        for(const auto& entry:colorOwners)if(auto target=entry.lock()) {
            const auto extent=renderExtent(target->width,target->height,target->scaleRole);
            if(extent[0]==target->physicalWidth&&extent[1]==target->physicalHeight)continue;
            colors.push_back({target,createTarget(target->width,target->height,target->format,target->scaleRole)});
        }
        for(const auto& entry:depthOwners)if(auto target=entry.lock()) {
            const auto extent=renderExtent(target->width,target->height,target->scaleRole);
            if(extent[0]==target->physicalWidth&&extent[1]==target->physicalHeight)continue;
            depths.push_back({target,createDepthTarget(target->width,target->height,target->scaleRole)});
        }
        for(const auto& staged:colors)stretchColor(staged.owner,staged.fresh);
        for(const auto& staged:depths)stretchDepth(staged.owner,staged.fresh);
        check(device->GetDeviceRemovedReason(),"content transfer");
    } catch(...) {restore();throw;}
    // Publish: owners keep their identity and logical extent; the staged
    // objects take the old storage and release it once the remap retires.
    const auto oldScale=selectedRenderScale();
    LiveViewRemap remap;
    for(auto& [owner,fresh]:colors) {
        remap.colors.push_back({owner->view,fresh->view,owner->sampledView,fresh->sampledView,
            {float(fresh->physicalWidth)/owner->physicalWidth,float(fresh->physicalHeight)/owner->physicalHeight}});
        std::swap(owner->texture,fresh->texture);std::swap(owner->view,fresh->view);std::swap(owner->sampledView,fresh->sampledView);
        std::swap(owner->physicalWidth,fresh->physicalWidth);std::swap(owner->physicalHeight,fresh->physicalHeight);
        std::swap(owner->rowBytes,fresh->rowBytes);
        owner->frontProof={};owner->descriptorProof={};
    }
    for(auto& [owner,fresh]:depths) {
        remap.depths.push_back({owner->view,fresh->view,owner->depthView,fresh->depthView,owner->stencilView,fresh->stencilView,
            {float(fresh->physicalWidth)/owner->physicalWidth,float(fresh->physicalHeight)/owner->physicalHeight}});
        std::swap(owner->texture,fresh->texture);std::swap(owner->view,fresh->view);
        std::swap(owner->depthView,fresh->depthView);std::swap(owner->stencilView,fresh->stencilView);
        std::swap(owner->physicalWidth,fresh->physicalWidth);std::swap(owner->physicalHeight,fresh->physicalHeight);
        owner->copyProof={};owner->descriptorProof={};
    }
    remapEngineBindings(remap,oldScale);
    // Temporal origin pins follow their owners' new storage. A uniform clear is
    // invariant under the stretch; a raster grid is not, so it becomes unknown.
    if(temporalOrigins&&temporalOriginPins) {
        auto& owners=*temporalOrigins;auto& pins=*temporalOriginPins;
        const bool colorMoved=owners.color&&owners.color->texture.Get()!=pins.colorTexture.Get();
        const bool depthMoved=owners.depth&&owners.depth->texture.Get()!=pins.depthTexture.Get();
        if(colorMoved){pins.colorTexture=owners.color->texture;pins.colorView=owners.color->view;}
        if(depthMoved){pins.depthTexture=owners.depth->texture;pins.depthView=owners.depth->view;}
        for(size_t i=0;i<owners.components.size();++i) {
            auto& origin=owners.components[i];
            if((i?depthMoved:colorMoved)&&origin.kind==TemporalRasterOriginKind::Grid)
                origin={TemporalRasterOriginKind::Unknown,{},origin.revision+1};
        }
    }
    // Size-keyed presentation caches would rebuild on demand; release them now.
    antialiasingOutput.reset();presentationCanvas.reset();screenScratch={};
    if(antialiasingPipeline)antialiasingPipeline->sharpened.reset();
    // The previous frame's queued swapchain copy still references the current
    // buffers, so ResizeBuffers cannot run here. The next presentation resizes
    // through configureVideoPresentation once that copy has completed.
    if(swapChain) {
        const auto extent=renderExtent(presentationLogicalWidth,presentationLogicalHeight,TargetScale::Output);
        presentationSourceWidth=extent[0];presentationSourceHeight=extent[1];
    }
    // Sealed command lists bound the old views and filtered samplers.
    const uint32_t rerecorded=!remap.empty()||filteringChanged?rerecordRecordingPayloads(remap):0;
    liveReceipt={uint32_t(colors.size()),uint32_t(depths.size()),rerecorded};
    requireOwner();return true;
}
}
