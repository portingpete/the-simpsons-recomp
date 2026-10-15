#include "temporal_scene.h"
#include "native_copy_submission.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <limits>
namespace Simpsons::Graphics {
namespace {
void qualifyOriginalSampledColor(ID3D11ShaderResourceView* view,ID3D11Texture2D* texture,ID3D11Device* device) {
    if(!view||!texture)throw Error("Temporal output requires original sampled color storage");
    ComPtr<ID3D11Device> owner;view->GetDevice(&owner);
    ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
    if(owner.Get()!=device||resource.Get()!=texture)throw Error("Temporal sampled color is not its original device/resource owner");
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};view->GetDesc(&desc);
    if(desc.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||desc.Texture2D.MostDetailedMip||desc.Texture2D.MipLevels!=1||
        desc.Format!=DXGI_FORMAT_R10G10B10A2_UNORM)throw Error("Temporal original packed-alpha source view format/range changed");
}

}
std::array<uint32_t,2> NativeBackend::configureTemporalRendering(Antialiasing mode,uint32_t width,uint32_t height) {
    validateSubmissionContext();
    if(!validAntialiasing(mode))throw Error("Native antialiasing mode is unsupported");
    // Preserved research entry has no SDK/device initialization in normal builds.
    return {width,height};
}
void NativeBackend::registerTemporalWorldTargets(const std::shared_ptr<RenderTarget>& color,const std::shared_ptr<DepthTarget>& depth) {
    validateSubmissionContext();
    if(temporalOrigins)validateTemporalSceneOriginOwners();
    validateFrontTarget(color);validateDepthCopyTarget(depth);
    if(temporalOrigins && (temporalOrigins->color!=color || temporalOrigins->depth!=depth))
        throw Error("Temporal origin registration cannot replace its live actual Scene owners");
    if(!temporalOrigins) {
        temporalOriginPins=TemporalOriginPins{device,context,color->texture,depth->texture,color->view,depth->view};
        temporalOrigins=NativeTemporalSceneOrigins{color,depth,{}};
    }
    if(temporalScene)temporalScene->registerWorldTargets(color->texture.Get(),depth->view.Get());
}
void NativeBackend::beginTemporalScene(const std::shared_ptr<RenderTarget>& color,const std::shared_ptr<DepthTarget>& depth,uint64_t camera,uint64_t generation,bool reset) {
    validateSubmissionContext();if(!temporalScene)return;
    validateFrontTarget(color);validateDepthCopyTarget(depth);
    temporalScene->begin(color->texture.Get(),color->view.Get(),depth->view.Get(),camera,generation,reset);
}
std::shared_ptr<RenderTarget> NativeBackend::prepareTemporalUiBackground(const std::shared_ptr<RenderTarget>& color,const std::shared_ptr<DepthTarget>& depth,std::array<float,2> sourceJitter) {
    validateSubmissionContext();if(!temporalScene)return color;validateFrontTarget(color);validateDepthCopyTarget(depth);
    if(temporalScene->hasGeometry())throw Error("UI-only spatial background cannot consume a temporal world scene");
    if(!color->sampledView)throw Error("UI-only background requires original sampled single-source owners");
    qualifyOriginalSampledColor(color->sampledView.Get(),color->texture.Get(),device.Get());
    for(float value:sourceJitter)if(!std::isfinite(value)||std::abs(value)>.5f)
        throw Error("UI-only background requires finite bounded source render-pixel jitter");
    flushIm2D();
    // A captured source grid may differ from an unused planned sample. Still
    // reject unqualified/reconstructed active frames before any copy or reset.
    if(temporalScene->active())(void)temporalScene->frame();
    invalidateTemporalCopiedOrigin(temporalOutput->texture.Get());
    temporalScene->copyUiBackground(color->sampledView.Get(),temporalOutput->view.Get(),temporalOutput->physicalWidth,temporalOutput->physicalHeight,sourceJitter);
    temporalScene->reset();return temporalOutput;
}
std::shared_ptr<RenderTarget> NativeBackend::reconstructTemporalScene(const std::shared_ptr<RenderTarget>& color,const std::shared_ptr<DepthTarget>& depth) {
    validateSubmissionContext();if(!temporalScene)return color;
    validateFrontTarget(color);validateDepthCopyTarget(depth);flushIm2D();
    qualifyOriginalSampledColor(color->sampledView.Get(),color->texture.Get(),device.Get());
    if(!temporalScene->matches(color->texture.Get(),depth->view.Get()))throw Error("Temporal reconstruction does not own the selected main scene");
    if(temporalScene->reconstructed())return temporalOutput;
    const auto frame=temporalScene->frame();
    nvidiaDlss->evaluate(color->texture.Get(),depth->texture.Get(),temporalScene->motion(),temporalScene->biasCurrent(),frame);
    invalidateTemporalCopiedOrigin(temporalOutput->texture.Get());
    temporalScene->copyOutput(nvidiaDlss->outputView(),color->sampledView.Get(),temporalOutput->view.Get(),temporalOutput->physicalWidth,temporalOutput->physicalHeight,frame.jitter);
    temporalScene->markReconstructed();
    return temporalOutput;
}
std::array<float,2> NativeBackend::temporalSceneJitter() const {
    validateSubmissionContext();if(!temporalScene)throw Error("Temporal jitter requires its configured scene owner");
    return temporalScene->frame().jitter;
}
bool NativeBackend::temporalSceneActive() const {validateSubmissionContext();return temporalScene&&temporalScene->active();}
bool NativeBackend::temporalSceneHasGeometry() const {validateSubmissionContext();return temporalScene&&temporalScene->hasGeometry();}
bool NativeBackend::temporalSceneOwnsTargets(const std::shared_ptr<RenderTarget>& color,const std::shared_ptr<DepthTarget>& depth) const {
    validateSubmissionContext();return temporalScene&&color&&depth&&temporalScene->matches(color->texture.Get(),depth->view.Get());
}
void NativeBackend::rejectUntrackedTemporalWorldDraw(const std::shared_ptr<RenderTarget>& color,
    const std::shared_ptr<DepthTarget>& depth,const char* kind) {
    validateSubmissionContext();if(temporalScene&&color&&depth&&temporalScene->matches(color->texture.Get(),depth->view.Get()))
        temporalScene->rejectUntrackedWorldDraw(kind);
}
void NativeBackend::finishTemporalFrame(){validateSubmissionContext();if(temporalScene)temporalScene->finishFrame();}
void NativeBackend::resetTemporalHistory(){validateSubmissionContext();if(temporalScene)temporalScene->reset();}
std::shared_ptr<TemporalGeometryDraw> NativeBackend::temporalGeometryDraw(const std::shared_ptr<RenderTarget>& color,
    const std::shared_ptr<DepthTarget>& depth,const ShadowMeshDraw& d,uint32_t shader,uint32_t kind,bool recorded) {
    if(!temporalScene)return {};
    return temporalScene->draw(color->texture.Get(),color->view.Get(),depth->view.Get(),d,shader,kind,recorded);
}
std::vector<std::array<float,4>> NativeBackend::temporalVertexConstants() const {
    validateSubmissionContext();
    const auto ranged=constantBindingContext();if(!ranged)throw Error("Temporal original constants require their D3D11.1 owner");
    ComPtr<ID3D11Buffer> actual;UINT first{},count{};ranged->VSGetConstantBuffers1(0,1,&actual,&first,&count);
    for(const auto& bank:constantBanks) {
        if(actual.Get()!=bank.buffer.Get()||bank.device.Get()!=device.Get()||!bank.contentsValid)continue;
        if(bank.latestSlice.arena&&(!bank.publicationValid||first!=bank.latestSlice.binding.firstConstant||count!=bank.latestSlice.binding.numConstants))
            throw Error("Temporal transform snapshot has a stale original ranged binding");
        if(!bank.latestSlice.arena&&first)throw Error("Temporal transform snapshot has a shifted original bank");
        if(bank.contents.size()<64||bank.contents.size()>256*16||bank.contents.size()%16)throw Error("Temporal transform bank has an invalid register extent");
        std::vector<std::array<float,4>> result(bank.contents.size()/16);std::memcpy(result.data(),bank.contents.data(),bank.contents.size());return result;
    }
    throw Error("Temporal transform snapshot lacks its original CPU publication owner");
}
namespace {
void originGrid(std::array<float,2> jitter) {
    for(float value:jitter)if(!std::isfinite(value)||std::abs(value)>.5f)
        throw Error("Temporal attachment origin requires finite render-pixel jitter");
}
void originRevision(TemporalRasterOrigin& origin) {
    if(origin.revision==std::numeric_limits<uint64_t>::max())
        throw Error("Temporal attachment origin revision exhausted");
    ++origin.revision;
}
}
void NativeBackend::validateTemporalSceneOriginOwners() const {
    validateSubmissionContext();
    if(!temporalOrigins||!temporalOriginPins)
        throw Error("Temporal attachment origins have no frozen actual Scene storage owners");
    const auto& pins=*temporalOriginPins;const auto& owners=*temporalOrigins;
    if(device.Get()!=pins.device.Get()||context.Get()!=pins.context.Get()||
        !owners.color||!owners.depth||owners.color->texture.Get()!=pins.colorTexture.Get()||
        owners.color->view.Get()!=pins.colorView.Get()||owners.depth->texture.Get()!=pins.depthTexture.Get()||
        owners.depth->view.Get()!=pins.depthView.Get())
        throw Error("Temporal attachment origins lost their frozen device/context/resource/view identities");
    // Existing exact device/resource/view/descriptor proofs also reject public
    // metadata changes. Frozen COM pins prevent backing replacement and ABA.
    validateFrontTarget(owners.color);validateDepthCopyTarget(owners.depth);
}
NativeTemporalSceneOrigins NativeBackend::temporalSceneOrigins() {
    validateTemporalSceneOriginOwners();flushIm2D();validateTemporalSceneOriginOwners();
    return *temporalOrigins;
}
std::array<float,2> NativeBackend::temporalSceneOriginGrid(const std::shared_ptr<RenderTarget>& color,
    const std::shared_ptr<DepthTarget>& depth) {
    const auto origins=temporalSceneOrigins();
    if(origins.color!=color||origins.depth!=depth)
        throw Error("Temporal raster grid query does not own the original main Scene pair");
    std::optional<std::array<float,2>> common;
    for(const auto& component:origins.components) {
        if(component.kind==TemporalRasterOriginKind::Uniform)continue;
        if(component.kind!=TemporalRasterOriginKind::Grid)
            throw Error("Temporal raster grid contains an unknown component origin");
        originGrid(component.jitter);
        if(common&&*common!=component.jitter)
            throw Error("Temporal raster grid contains incompatible component origins");
        common=component.jitter;
    }
    return common.value_or(std::array<float,2>{});
}
void NativeBackend::requireTemporalSceneOrigin(const std::shared_ptr<RenderTarget>& color,
    const std::shared_ptr<DepthTarget>& depth,std::array<float,2> sourceJitter) {
    originGrid(sourceJitter);const auto origins=temporalSceneOrigins();
    if(origins.color!=color||origins.depth!=depth)
        throw Error("Temporal attachment origin query does not own the original main Scene pair");
    for(size_t i=0;i<origins.components.size();++i)
        if(!origins.components[i].compatible(sourceJitter)) {
            char reason[192];std::snprintf(reason,sizeof(reason),
                "Temporal component %zu has unknown or incompatible raster origin (kind=%u revision=%llu)",
                i,unsigned(origins.components[i].kind),
                static_cast<unsigned long long>(origins.components[i].revision));
            throw Error(reason);
        }
}
void NativeBackend::noteTemporalSceneClear(const std::shared_ptr<RenderTarget>& color,
    const std::shared_ptr<DepthTarget>& depth,uint32_t componentMask) {
    if((componentMask&1)&&color)invalidateTemporalCopiedOrigin(color->texture.Get());
    if((componentMask&6)&&depth)invalidateTemporalCopiedOrigin(depth->texture.Get());
    if(!temporalOrigins)return;
    validateTemporalSceneOriginOwners();
    if(!color||color->texture.Get()!=temporalOriginPins->colorTexture.Get()||
        color->view.Get()!=temporalOriginPins->colorView.Get())componentMask&=~1u;
    if(!depth||depth->texture.Get()!=temporalOriginPins->depthTexture.Get()||
        depth->view.Get()!=temporalOriginPins->depthView.Get())componentMask&=~6u;
    for(size_t i=0;i<3;++i)if(componentMask&(1u<<i)) {
        auto& component=temporalOrigins->components[i];originRevision(component);
        component.kind=TemporalRasterOriginKind::Uniform;component.jitter={};
    }
}
void NativeBackend::noteTemporalSceneDraw(const std::shared_ptr<RenderTarget>& color,
    const std::shared_ptr<DepthTarget>& depth,uint32_t colorMask,bool depthWrite,bool stencilWrite,
    std::optional<std::array<float,2>> rasterJitter) {
    if(colorMask&&color)invalidateTemporalCopiedOrigin(color->texture.Get());
    if((depthWrite||stencilWrite)&&depth)invalidateTemporalCopiedOrigin(depth->texture.Get());
    if(!temporalOrigins)return;
    validateTemporalSceneOriginOwners();
    if(rasterJitter)originGrid(*rasterJitter);
    // Im2D freezes wrapper metadata, while retaining the same real COM owners.
    // The actual draw validates these views/resources before this write receipt.
    const bool sameColor=color&&color->texture.Get()==temporalOriginPins->colorTexture.Get()&&
        color->view.Get()==temporalOriginPins->colorView.Get();
    const bool sameDepth=depth&&depth->texture.Get()==temporalOriginPins->depthTexture.Get()&&
        depth->view.Get()==temporalOriginPins->depthView.Get();
    const uint32_t mask=(sameColor&&colorMask?1u:0u)|(sameDepth&&depthWrite?2u:0u)|
        (sameDepth&&stencilWrite?4u:0u);
    for(size_t i=0;i<3;++i)if(mask&(1u<<i)) {
        auto& component=temporalOrigins->components[i];
        const bool compatible=rasterJitter&&component.compatible(*rasterJitter);
        originRevision(component);
        component.kind=compatible?TemporalRasterOriginKind::Grid:TemporalRasterOriginKind::Unknown;
        component.jitter=compatible?*rasterJitter:std::array<float,2>{};
    }
}
void NativeBackend::noteTemporalSceneCopy(ID3D11Texture2D* source,ID3D11Texture2D* destination) {
    if(source!=destination)invalidateTemporalCopiedOrigin(destination);
    if(!temporalOrigins)return;
    validateTemporalSceneOriginOwners();
    // Copies OUT of Scene leave its receipts unchanged. A full incoming copy
    // from an unregistered resource has no known grid; feedback has its own
    // qualified publication path. Self-copies preserve their prior origin.
    if(source==destination)return;
    for(size_t i=0;i<3;++i) {
        auto* owned=i?temporalOriginPins->depthTexture.Get():temporalOriginPins->colorTexture.Get();
        if(destination!=owned)continue;
        auto& component=temporalOrigins->components[i];originRevision(component);
        component.kind=TemporalRasterOriginKind::Unknown;component.jitter={};
    }
}
void NativeBackend::captureTemporalCopiedOrigin(const std::shared_ptr<NativeCopySubmission>& copy,
    const std::shared_ptr<RenderTarget>& color,const std::shared_ptr<DepthTarget>& depth) {
    if(!temporalScene||!temporalScene->active()||temporalScene->reconstructed()||!temporalOrigins||
       (!color&&!depth)||copy->sourceResource.Get()==copy->destinationResource.Get())return;
    validateTemporalSceneOriginOwners();
    const bool colorSource=color&&copy->sourceResource.Get()==temporalOriginPins->colorTexture.Get();
    const bool depthSource=depth&&copy->sourceResource.Get()==temporalOriginPins->depthTexture.Get();
    if(!colorSource&&!depthSource)return;
    if(temporalCopyEpoch==std::numeric_limits<uint64_t>::max())throw Error("Temporal sampled-copy epoch exhausted");
    if(colorSource) {
        validateFrontTarget(color);
        if(color->texture.Get()!=copy->destinationResource.Get()||
           color->pixelWidth()!=temporalOrigins->color->pixelWidth()||color->pixelHeight()!=temporalOrigins->color->pixelHeight())
            throw Error("Temporal color copy lost same-extent Single storage");
        copy->rasterColor=color;copy->rasterColorView=color->view;copy->rasterSampledView=color->sampledView;
    }else {
        validateDepthCopyTarget(depth);
        if(depth->texture.Get()!=copy->destinationResource.Get()||
           depth->storageWidth()!=temporalOrigins->depth->storageWidth()||depth->storageHeight()!=temporalOrigins->depth->storageHeight())
            throw Error("Temporal depth copy lost same-extent Single storage");
        copy->rasterDepth=depth;copy->rasterDepthView=depth->view;copy->rasterSampledView=depth->depthView;
    }
    copy->rasterComponent=colorSource?1u:2u;
    copy->rasterOrigin=temporalOrigins->components[colorSource?0:1];
    copy->rasterDevice=device;copy->rasterContext=context;copy->rasterEpoch=++temporalCopyEpoch;
}
void NativeBackend::publishTemporalCopiedOrigin(const std::shared_ptr<NativeCopySubmission>& copy) {
    if(!copy->rasterOrigin)return;
    if(copy->owner!=this||!copy->submitted||!copy->rasterEpoch)throw Error("Temporal sampled-copy publication has no actual submitted owner");
    auto& slot=temporalCopiedOrigins[temporalCopiedOriginNext];
    temporalCopiedOriginNext=(temporalCopiedOriginNext+1)%temporalCopiedOrigins.size();slot=copy;
}
void NativeBackend::invalidateTemporalCopiedOrigin(ID3D11Texture2D* destination) noexcept {
    if(!temporalCopyEpoch||!destination)return;
    for(auto& copy:temporalCopiedOrigins)if(copy&&copy->destinationResource.Get()==destination)copy.reset();
}
std::shared_ptr<NativeCopySubmission> NativeBackend::temporalCopiedOrigin(ID3D11Texture2D* resource,uint32_t component) const {
    for(const auto& copy:temporalCopiedOrigins)
        if(copy&&copy->destinationResource.Get()==resource&&copy->rasterComponent==component)return copy;
    return {};
}
TemporalRasterOrigin NativeBackend::requireTemporalCopiedOrigin(const std::shared_ptr<NativeCopySubmission>& copy,
    ID3D11Texture2D* resource,uint32_t component) const {
    validateTemporalSceneOriginOwners();
    if(!copy||copy->owner!=this||!copy->submitted||!copy->rasterOrigin||!copy->rasterEpoch||
       copy->rasterDevice.Get()!=device.Get()||copy->rasterContext.Get()!=context.Get()||
       copy->destinationResource.Get()!=resource||copy->rasterComponent!=component||temporalCopiedOrigin(resource,component)!=copy)
        throw Error("Temporal filter input lacks its current actual CopyResource snapshot");
    if(component==1) {
        const auto& color=copy->rasterColor;validateFrontTarget(color);
        if(color->texture.Get()!=resource||color->view.Get()!=copy->rasterColorView.Get()||color->sampledView.Get()!=copy->rasterSampledView.Get())
            throw Error("Temporal sampled color copy backing/views changed");
    }else if(component==2) {
        const auto& depth=copy->rasterDepth;validateDepthCopyTarget(depth);
        if(depth->texture.Get()!=resource||depth->view.Get()!=copy->rasterDepthView.Get()||depth->depthView.Get()!=copy->rasterSampledView.Get())
            throw Error("Temporal sampled depth copy backing/views changed");
    }else throw Error("Temporal filter copy component is unsupported");
    if(copy->rasterOrigin->kind==TemporalRasterOriginKind::Unknown)throw Error("Temporal filter source snapshot has unknown raster origin");
    if(copy->rasterOrigin->kind==TemporalRasterOriginKind::Grid)originGrid(copy->rasterOrigin->jitter);
    return *copy->rasterOrigin;
}
void NativeBackend::publishTemporalFilterColor(const NativeTemporalSceneOrigins& expected,const TemporalRasterOrigin& source) {
    if(source.kind==TemporalRasterOriginKind::Unknown)throw Error("Temporal full filter cannot publish unknown input provenance");
    if(source.kind==TemporalRasterOriginKind::Grid)originGrid(source.jitter);
    validateTemporalSceneFeedback(expected,1);
    invalidateTemporalCopiedOrigin(expected.color->texture.Get());
    auto& destination=temporalOrigins->components[0];originRevision(destination);
    destination.kind=source.kind;destination.jitter=source.jitter;
}
void NativeBackend::validateTemporalSceneFeedback(const NativeTemporalSceneOrigins& expected,uint32_t mask) const {
    validateTemporalSceneOriginOwners();
    if(!mask||(mask&~7u)||expected.color!=temporalOrigins->color||
        expected.depth!=temporalOrigins->depth)
        throw Error("Temporal feedback lost its registered actual Scene owner/component mask");
    for(size_t i=0;i<3;++i)if(mask&(1u<<i)) {
        const auto& current=temporalOrigins->components[i];
        if(expected.components[i].revision!=current.revision||
            current.revision==std::numeric_limits<uint64_t>::max())
            throw Error("Temporal feedback component changed after its owned snapshot");
    }
}
void NativeBackend::publishTemporalSceneFeedback(const NativeTemporalSceneOrigins& expected,uint32_t mask,
    std::array<float,2> destinationJitter) {
    originGrid(destinationJitter);validateTemporalSceneFeedback(expected,mask);
    for(size_t i=0;i<3;++i)if(mask&(1u<<i)) {
        auto& component=temporalOrigins->components[i];originRevision(component);
        component.kind=TemporalRasterOriginKind::Grid;component.jitter=destinationJitter;
    }
}

}
