#include "native_backend.h"
#include <cmath>
#include <algorithm>

namespace Simpsons::Graphics {
namespace {
constexpr GUID logicalExtentKey={0x7b39c8e1,0x2da8,0x4a03,{0x99,0xd3,0x51,0xc7,0x35,0x9e,0x84,0x10}};
// Bound scene allocations to the previous largest supported preset, 4K SSAA.
// This is a settings budget, not a guarantee that a GPU has available memory.
constexpr uint64_t maxScenePixels=uint64_t(3840)*2160*4;
void validate(const D3D11_VIEWPORT& value) {
    for(float field:{value.TopLeftX,value.TopLeftY,value.Width,value.Height,value.MinDepth,value.MaxDepth})
        if(!std::isfinite(field)) throw Error("Native viewport requires finite coordinates, dimensions and depth endpoints");
    constexpr double lower=D3D11_VIEWPORT_BOUNDS_MIN,upper=D3D11_VIEWPORT_BOUNDS_MAX;
    if(value.TopLeftX<lower || value.TopLeftX>upper || value.TopLeftY<lower || value.TopLeftY>upper ||
       value.Width<0 || value.Height<0 || double(value.TopLeftX)+value.Width>upper ||
       double(value.TopLeftY)+value.Height>upper)
        throw Error("Native viewport exceeds D3D11 feature-level-11 coordinate/dimension bounds");
    if(value.MinDepth<0 || value.MinDepth>1 || value.MaxDepth<0 || value.MaxDepth>1)
        throw Error("Native viewport depth endpoints must be in [0,1]");
    if(value.MinDepth>value.MaxDepth)
        throw Error("Native viewport reversed depth mapping requires a verified vertex transform; endpoint sorting is unsupported");
}

} // namespace

bool NativeBackend::supportsRendering(uint32_t width,uint32_t height,uint32_t anisotropy,Antialiasing aa,uint32_t percent) noexcept {
    if(width<1280||width>5120||height<720||height>2160||uint64_t(width)*9<uint64_t(height)*16||uint64_t(width)*9>uint64_t(height)*32||
       (anisotropy!=1&&anisotropy!=4&&anisotropy!=8&&anisotropy!=16)||uint32_t(aa)>3||
       (percent!=50&&percent!=67&&percent!=75&&percent!=100&&percent!=125&&percent!=150&&percent!=200))return false;
    const uint32_t samples=aa==Antialiasing::SSAA4x?2:1;
    const uint64_t w=((uint64_t(width)*percent+50)/100)*samples,h=((uint64_t(height)*percent+50)/100)*samples;
    return w&&h&&w<=D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION&&h<=D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION&&w*h<=maxScenePixels;
}
void NativeBackend::configureRendering(uint32_t width,uint32_t height,uint32_t anisotropy,Antialiasing aa,uint32_t percent) {
    requireOwner();
    if(renderingAllocated||swapChain)throw Error("Internal resolution must be configured before target allocation");
    if(!supportsRendering(width,height,anisotropy,aa,percent))throw Error("Unsupported internal rendering settings or scene pixel budget exceeded");
    antialiasingMode=aa;
    const uint32_t samples=aa==Antialiasing::SSAA4x?2:1;
    renderBaseWidth=width;renderBaseHeight=height;
    internalWidth=uint32_t((uint64_t(width)*percent+50)/100)*samples;
    internalHeight=uint32_t((uint64_t(height)*percent+50)/100)*samples;textureAnisotropy=anisotropy;
}
std::array<uint32_t,2> NativeBackend::renderExtent(uint32_t w,uint32_t h,TargetScale scale) const {
    if(scale==TargetScale::Fixed)return {w,h};
    const uint32_t samples=antialiasingMode==Antialiasing::SSAA4x?2:1;
    const uint64_t width=std::max(uint64_t(1),(uint64_t(w)*(internalWidth/samples)+640)/1280)*samples;
    const uint64_t height=std::max(uint64_t(1),(uint64_t(h)*(internalHeight/samples)+360)/720)*samples;
    if(width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION||height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION||width*height>maxScenePixels)
        throw Error("Scaled native scene target exceeds dimension or scene pixel budget");
    return {uint32_t(width),uint32_t(height)};
}
void NativeBackend::tagRenderExtent(ID3D11Texture2D* texture,uint32_t w,uint32_t h) const {
    const std::array extent{w,h};
    if(FAILED(texture->SetPrivateData(logicalExtentKey,sizeof(extent),extent.data())))throw Error("Unable to tag logical render extent");
}
std::array<float,2> NativeBackend::selectedRenderScale() const {
    ComPtr<ID3D11RenderTargetView> color;ComPtr<ID3D11DepthStencilView> depth;
    context->OMGetRenderTargets(1,&color,&depth);
    ComPtr<ID3D11Resource> resource;
    if(color)color->GetResource(&resource);else if(depth)depth->GetResource(&resource);else return {1,1};
    std::array<uint32_t,2> logical{};UINT bytes=sizeof(logical);
    if(FAILED(resource->GetPrivateData(logicalExtentKey,&bytes,logical.data())))return {1,1};
    if(bytes!=sizeof(logical)||!logical[0]||!logical[1])throw Error("Invalid logical render extent tag");
    ComPtr<ID3D11Texture2D> texture;
    if(FAILED(resource.As(&texture)))throw Error("Render extent belongs to a non-texture output");
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    return {float(desc.Width)/logical[0],float(desc.Height)/logical[1]};
}
D3D11_VIEWPORT NativeBackend::renderViewport(const std::shared_ptr<RenderTarget>& target,D3D11_VIEWPORT v) const {
    const float x=float(target->pixelWidth())/target->width,y=float(target->pixelHeight())/target->height;
    v.TopLeftX*=x;v.Width*=x;v.TopLeftY*=y;v.Height*=y;return v;
}
D3D11_VIEWPORT NativeBackend::contentViewport(const std::shared_ptr<RenderTarget>& target,D3D11_VIEWPORT logical) const {
    auto v=renderViewport(target,logical);
    if(logical.Width<=0||logical.Height<=0)return v;
    const double aspect=double(logical.Width)/logical.Height;
    const float width=float(std::min(double(v.Width),double(v.Height)*aspect));
    const float height=float(std::min(double(v.Height),double(v.Width)/aspect));
    v.TopLeftX+=(v.Width-width)*0.5f;v.TopLeftY+=(v.Height-height)*0.5f;
    v.Width=width;v.Height=height;return v;
}
std::array<uint32_t,2> NativeBackend::presentationExtent(uint32_t w,uint32_t h,uint32_t clientW,uint32_t clientH,
    uint32_t logicalW,uint32_t logicalH) const {
    if(!w||!h||!clientW||!clientH||!logicalW||!logicalH||w>16384||h>16384||clientW>16384||clientH>16384||logicalW>16384||logicalH>16384)
        throw Error("Invalid native presentation aspect dimensions");
    // Keep every internal pixel; add centered bars before DXGI display scaling.
    // Use the selected base aspect, including the caller's logical surface,
    // so rounding a 67% scene to whole texels cannot add a one-pixel bar.
    const uint64_t aspectW=uint64_t(logicalW)*renderBaseWidth*720,aspectH=uint64_t(logicalH)*renderBaseHeight*1280;
    uint64_t width=w,height=h;
    if(aspectW*clientH<aspectH*clientW)width=(uint64_t(w)*aspectH*clientW+aspectW*clientH-1)/(aspectW*clientH);
    else if(aspectW*clientH>aspectH*clientW)height=(uint64_t(h)*aspectW*clientH+aspectH*clientW-1)/(aspectH*clientW);
    if(width>16384||height>16384)throw Error("Native aspect-preserving presentation exceeds D3D11 bounds");
    return {uint32_t(width),uint32_t(height)};
}
D3D11_RECT NativeBackend::renderScissor(const std::shared_ptr<RenderTarget>& target,D3D11_RECT r) const {
    const double x=double(target->pixelWidth())/target->width,y=double(target->pixelHeight())/target->height;
    return {LONG(std::floor(r.left*x)),LONG(std::floor(r.top*y)),LONG(std::ceil(r.right*x)),LONG(std::ceil(r.bottom*y))};
}
D3D11_SAMPLER_DESC NativeBackend::materialSampling(D3D11_SAMPLER_DESC desc) const {
    requireOwner();
    // Preserve point-sampled lookup/noise maps and all comparison samplers.
    if(textureAnisotropy>1&&(desc.Filter==D3D11_FILTER_MIN_MAG_MIP_LINEAR||desc.Filter==D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT)) {
        desc.Filter=D3D11_FILTER_ANISOTROPIC;desc.MaxAnisotropy=textureAnisotropy;
    }
    return desc;
}
namespace {
bool equal(const D3D11_VIEWPORT& a,const D3D11_VIEWPORT& b) {
    return a.TopLeftX==b.TopLeftX && a.TopLeftY==b.TopLeftY && a.Width==b.Width && a.Height==b.Height &&
           a.MinDepth==b.MinDepth && a.MaxDepth==b.MaxDepth;
}
bool equal(const D3D11_RECT& a,const D3D11_RECT& b) {
    return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;
}
}

D3D11_VIEWPORT NativeBackend::setViewport(const D3D11_VIEWPORT& value) {
    requireOwner();
    if(featureLevel<D3D_FEATURE_LEVEL_11_0) throw Error("Native viewport requires D3D feature level 11");
    validate(value); // Every argument check precedes mutation of native state.
    auto physical=value;const auto scale=selectedRenderScale();
    physical.TopLeftX*=scale[0];physical.Width*=scale[0];physical.TopLeftY*=scale[1];physical.Height*=scale[1];
    validate(physical);context->RSSetViewports(1,&physical);
    // Verify actual native state before retaining logical coordinates. A
    // fractional scale can lose one Float32 ULP on multiplication/division;
    // the receipt is usable only while actual state and target scale match.
    D3D11_VIEWPORT actual{};UINT count=1;context->RSGetViewports(&count,&actual);requireOwner();
    if(count!=1||!equal(actual,physical))throw Error("D3D11 did not retain the requested native viewport");
    viewportReceipt={actual,value,scale,true};return value;
}

std::optional<D3D11_VIEWPORT> NativeBackend::viewport() const {
    requireOwner();
    std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> actual{};
    UINT count=UINT(actual.size());
    context->RSGetViewports(&count,actual.data());
    requireOwner();
    if(!count) return std::nullopt;
    if(count!=1) throw Error("Native viewport query encountered an unsupported multiple-viewport state");
    const auto scale=selectedRenderScale();
    if(viewportReceipt.valid&&viewportReceipt.scale==scale&&equal(viewportReceipt.physical,actual[0]))return viewportReceipt.logical;
    actual[0].TopLeftX/=scale[0];actual[0].Width/=scale[0];actual[0].TopLeftY/=scale[1];actual[0].Height/=scale[1];
    validate(actual[0]);
    return actual[0];
}
void NativeBackend::setScissor(const std::array<uint32_t,4>& r) {
    requireOwner();
    if(r[0]>=r[2] || r[1]>=r[3] || r[2]>32767 || r[3]>32767)
        throw Error("Native scissor requires a nonempty positive rectangle within coordinate bounds");
    const auto scale=selectedRenderScale();
    const D3D11_RECT rect{LONG(std::floor(r[0]*scale[0])),LONG(std::floor(r[1]*scale[1])),LONG(std::ceil(r[2]*scale[0])),LONG(std::ceil(r[3]*scale[1]))};
    if(rect.left<0||rect.top<0||rect.right<=rect.left||rect.bottom<=rect.top||rect.right>32767||rect.bottom>32767)
        throw Error("Scaled native scissor exceeds coordinate bounds");
    context->RSSetScissorRects(1,&rect);
    D3D11_RECT actual{};UINT count=1;context->RSGetScissorRects(&count,&actual);requireOwner();
    if(count!=1||!equal(actual,rect))throw Error("D3D11 did not retain the native scissor rectangle");
    // Integer pixel coverage cannot round-trip odd logical edges below 100%.
    // Retain the input only under this actual native rectangle/scale proof.
    scissorReceipt={actual,r,scale,true};
}
std::optional<std::array<uint32_t,4>> NativeBackend::scissor() const {
    requireOwner();std::array<D3D11_RECT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> r{};
    UINT count=UINT(r.size());context->RSGetScissorRects(&count,r.data());requireOwner();
    if(!count)return std::nullopt;
    if(count!=1 || r[0].left<0 || r[0].top<0 || r[0].right<=r[0].left || r[0].bottom<=r[0].top ||
       r[0].right>32767 || r[0].bottom>32767)throw Error("Native scissor query has an unsupported rectangle/count");
    const auto scale=selectedRenderScale();
    if(scissorReceipt.valid&&scissorReceipt.scale==scale&&equal(scissorReceipt.physical,r[0]))return scissorReceipt.logical;
    return std::array<uint32_t,4>{uint32_t(std::lround(r[0].left/scale[0])),uint32_t(std::lround(r[0].top/scale[1])),uint32_t(std::lround(r[0].right/scale[0])),uint32_t(std::lround(r[0].bottom/scale[1]))};
}
}
