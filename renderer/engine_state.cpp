#include "engine_state.h"
#include <algorithm>
#include <bit>
#include <limits>

namespace Simpsons::Graphics {
namespace {
constexpr StateFieldEvidence scalarFields[]={
    {0x28,0x8243A6C8,1,1}, {0x2C,0x8243A738,3,6}, {0x30,0x8243A708,1,1},
    {0x34,0x82439F30,0,0}, {0x38,0x82439F00,6,2}, {0x3C,0x8243A010,0,0},
    {0x40,0x8243A3A0,0,0}, {0x44,0x8243A4D0,0xFFFFFFFF,0xFFFFFFFF},
    {0x48,0x8243A130,1,6}, {0x4C,0x8243A1C0,0,7},
    {0x50,0x8243A0A0,0,0}, {0x54,0x8243A2C0,1,1}, {0x58,0x8243A330,0,0},
    {0x5C,0x8243A250,0,0}, {0x60,0x82439F60,0,0}, {0x64,0x8243A438,0,0},
    {0x68,0x8243A4A0,7,4}, {0x6C,0x8243A770,0,0}, {0x70,0x8243A7B0,0,0}, {0x74,0x8243A818,0,0},
    {0x78,0x8243A850,0,0}, {0x7C,0x8243A888,0,0}, {0x80,0x8243A7E8,7,7},
    {0x84,0x8243A988,0,0}, {0x88,0x8243A9A8,0xFFFFFFFF,0xFFFFFFFF},
    {0x8C,0x8243A9C8,0xFFFFFFFF,0xFFFFFFFF},
    {0x90,0x8243A8E8,0,0}, {0x94,0x8243A920,0,0}, {0x98,0x8243A958,0,0},
    {0x9C,0x8243A8B8,7,7}, {0xA0,0x8243A9E8,0,0},
    {0xA4,0x8243AA08,0xFFFFFFFF,0xFFFFFFFF}, {0xA8,0x8243AA28,0xFFFFFFFF,0xFFFFFFFF},
    {0xCC,0x8243AAA0,0,0},
    {0xD0,0x8243AB68,0,0}, {0xD4,0x8243AC60,0xF,0xF},
    {0xD8,0x8243ACA0,0xF,0xF}, {0xDC,0x8243ACE0,0xF,0xF},
    {0xE0,0x8243AD20,0xF,0xF}, {0x134,0x8243B3B0,0,0},
    {0x138,0x8243B460,0,0}, {0x13C,0x8243B510,0,0}, {0x140,0x8243B5C0,0,0},
    {0x150,0x8243B7A8,0,0}, {0x154,0x8243B7D8,0x87,0x87},
    // Additional application registrations retain their SDK baseline until the
    // original forced pass applies its distinct defaults. No draw is implied.
    {0xAC,0x8243AA48,0,0}, {0xB0,0x8243AD70,0x3F800000,0x3F800000},
    {0xB4,0x8243ADD0,0x3F800000,0x3F800000}, {0xB8,0x8243AD60,0,0},
    {0xBC,0x8243AE28,0x42800000,0x42800000}, {0xC0,0x8243AC10,1,1},
    {0xC4,0x8243AC40,0xFFFFFFFF,0xFFFFFFFF}, {0xC8,0x8243D0E8,0,0},
    {0xE4,0x8243B6E0,1,1}, {0xE8,0x8243B670,0x3F800000,0x3F800000},
    {0xEC,0x8243B6A8,0x3F800000,0x3F800000},
    {0xF0,0x8243AF00,0,0}, {0xF4,0x8243AF20,0,0}, {0xF8,0x8243AF48,0,0}, {0xFC,0x8243AF70,0,0},
    {0x100,0x8243AF98,0,0}, {0x104,0x8243AFC0,0,0}, {0x108,0x8243AFE8,0,0}, {0x10C,0x8243B010,0,0},
    {0x110,0x8243B030,0,0}, {0x114,0x8243B050,0,0}, {0x118,0x8243B078,0,0}, {0x11C,0x8243B0A0,0,0},
    {0x120,0x8243B0C8,0,0}, {0x124,0x8243B0F0,0,0}, {0x128,0x8243B118,0,0}, {0x12C,0x8243B140,0,0},
    {0x130,0x8243B260,1,1}, {0x144,0x8243B718,0,0}, {0x148,0x8243B750,0,0}, {0x14C,0x8243B780,0xFFFF,0xFFFF},
    {0x158,0x8243B810,0x40000000,0x40000000}, {0x15C,0x8243B840,0x40000000,0x40000000},
    {0x168,0x8243B960,0,0}, {0x16C,0x8243B990,0,0}, {0x170,0x8243B9C0,0,0}, {0x178,0x8243BA10,0,0}
};
constexpr StateFieldEvidence samplerFields[]={
    {0x00,0x8243C180,0,0}, {0x04,0x8243C1D0,0,0}, {0x08,0x8243C220,0,0},
    {0x0C,0x8243C110,0,0}, {0x10,0x8243BBD0,0,1}, {0x14,0x8243BA40,0,1},
    {0x18,0x8243BD60,2,2}, {0x1C,0x8243BF70,0,0}, {0x20,0x8243C010,0,0},
    {0x24,0x8243BE50,1,1}, {0x28,0x8243BCC8,0,0}, {0x2C,0x8243BB38,0,0},
    {0x30,0x8243BDB8,0,0}, {0x34,0x8243C090,13,13}, {0x38,0x8243C270,0,0},
    {0x3C,0x8243BEC8,0,0}, {0x40,0x8243C2C8,0,0}, {0x44,0x8243C320,0,0},
    {0x48,0x8243C378,0,0}, {0x4C,0x8243C3D0,1,1}
};
constexpr std::array<uint32_t,4> quadBlend={0x00010106,0x00010706,0x00010186,0x00010001};
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);

void need(bool condition, const char* reason) {
    if(!condition) throw EngineStateError(reason);
}
constexpr auto knownScalarOffsets=[] {
    std::array<bool,95> offsets{};
    for(const auto& field:scalarFields)offsets[field.id/4]=true;
    return offsets;
}();
bool knownScalar(uint32_t id) noexcept {
    return !(id&3) && id/4<knownScalarOffsets.size() && knownScalarOffsets[id/4];
}
bool validScalar(uint32_t id, uint32_t value) noexcept {
    switch(ScalarState(id)) {
    case ScalarState::BlendConstant: return true;
    // Original application setters retain raw IEEE-754 requests. Existing
    // drawing profiles independently require zero bias; this owns state only.
    case ScalarState::DepthBias: case ScalarState::SlopeBias:
        return (value&0x7F800000u)!=0x7F800000u;
    // Only the two observed SDK/application policies are retained for these
    // new fields. Rendering/presentation consumers have narrower contracts.
    case ScalarState::HalfPixelOffset: case ScalarState::PrimitiveResetEnable:
    case ScalarState::TessellationMode: case ScalarState::PresentInterval: return value<=1;
    case ScalarState::PointSizeMaximum: return value==0x3F800000 || value==0x42800000;
    case ScalarState::GuardBandX: case ScalarState::GuardBandY: return value==0x3F800000 || value==0x40000000;
    case ScalarState::MultisampleMask: return value==0xFFFF || value==0xFFFFFFFF;
    case ScalarState::AlphaToMaskOffsets: return value<=255;
    case ScalarState::StencilReference: case ScalarState::BackStencilReference: return value<=255;
    case ScalarState::StencilReadMask: case ScalarState::StencilWriteMask:
    case ScalarState::BackStencilReadMask: case ScalarState::BackStencilWriteMask:
        return value<=255 || value==0xFFFFFFFF; // SDK default and canonical byte masks.
    case ScalarState::DepthEnable: case ScalarState::DepthWrite:
    case ScalarState::BlendEnable: case ScalarState::SeparateAlpha:
    case ScalarState::AlphaTest: case ScalarState::StencilEnable: case ScalarState::AlphaToMask:
    case ScalarState::TwoSidedStencil: case ScalarState::ScissorEnable:
    case ScalarState::ExpandedBlend0: case ScalarState::ExpandedBlend1:
    case ScalarState::ExpandedBlend2: case ScalarState::ExpandedBlend3: return value<=1;
    case ScalarState::DepthCompare: case ScalarState::AlphaCompare:
    case ScalarState::Cull: case ScalarState::BlendOperation:
    case ScalarState::AlphaBlendOperation: return value<=7;
    case ScalarState::SourceBlend: case ScalarState::DestinationBlend:
    case ScalarState::AlphaSourceBlend: case ScalarState::AlphaDestinationBlend: return value<=31;
    case ScalarState::AlphaReference: return value<=255;
    case ScalarState::ColorMask0: case ScalarState::ColorMask1:
    case ScalarState::ColorMask2: case ScalarState::ColorMask3: return value<=15;
    // These fields are deliberately baseline-only. Their broader behavior is
    // outside this state service, even where the SDK accepts additional values.
    default:
        for(const auto& field:scalarFields) if(field.id==id) return value==field.startupValue;
        return false;
    }
}
bool validSampler(uint32_t id, uint32_t value) noexcept {
    switch(SamplerState(id)) {
    case SamplerState::AddressU: case SamplerState::AddressV: case SamplerState::AddressW:
        return value<=7; // Proven raw 3-bit fields; the draw gate supports only repeat=0.
    case SamplerState::Minification: case SamplerState::Magnification: return value<=1;
    case SamplerState::MagnificationZ: case SamplerState::MinificationZ:
    case SamplerState::SeparateZFilter: return value<=1;
    case SamplerState::MipFilter: return value<=2;
    case SamplerState::BorderSelector: case SamplerState::LodBiasBits:
    case SamplerState::MinimumMip: case SamplerState::TrilinearThreshold:
    case SamplerState::AnisotropyBiasBits: case SamplerState::HorizontalGradientBias:
    case SamplerState::VerticalGradientBias: case SamplerState::WhiteBorderW: return value==0;
    case SamplerState::MaximumAnisotropy: case SamplerState::PointBorderEnable: return value==1;
    case SamplerState::MaximumMip: return value==13;
    default: return false;
    }
}
} // namespace

std::span<const StateFieldEvidence> scalarStateEvidence() noexcept {return scalarFields;}
std::span<const StateFieldEvidence> samplerStateEvidence() noexcept {return samplerFields;}

EngineState EngineState::fromOriginalStartup() noexcept {
    EngineState result;
    for(const auto& field:scalarFields) result.scalar_[field.id/4]=field.startupValue;
    for(size_t stage=0;stage<result.samplers_.size();++stage)
        for(const auto& field:samplerFields)
            result.samplers_[stage][field.id/4]=stage<8?field.startupValue:field.sdkDefault;
    result.rebuildBlend();
    result.initialized_=true;
    return result;
}
void EngineState::requireInitialized() const {need(initialized_,"Host engine state has not been initialized");}

uint32_t EngineState::requestedBlendWord() const {
    requireInitialized();
    return raw(ScalarState::SourceBlend) | (raw(ScalarState::BlendOperation)<<5) |
        (raw(ScalarState::DestinationBlend)<<8) | (raw(ScalarState::AlphaSourceBlend)<<16) |
        (raw(ScalarState::AlphaBlendOperation)<<21) | (raw(ScalarState::AlphaDestinationBlend)<<24);
}
void EngineState::rebuildBlend() noexcept {
    uint32_t word=0x00010001;
    if(raw(ScalarState::BlendEnable)) {
        const auto low=raw(ScalarState::SourceBlend) | (raw(ScalarState::BlendOperation)<<5) |
                       (raw(ScalarState::DestinationBlend)<<8);
        if(raw(ScalarState::SeparateAlpha)) {
            word=low | (raw(ScalarState::AlphaSourceBlend)<<16) |
                (raw(ScalarState::AlphaBlendOperation)<<21) | (raw(ScalarState::AlphaDestinationBlend)<<24);
        } else {
            // Exact 8243A028..A044 rotate/mask transform, including high factor
            // bits. This is not a copy of RGB factors to alpha fields.
            word=low | (((low<<16) | ((low & 0x1010)<<12)) & 0xEFEF0000);
        }
    }
    blend_.fill(word); // Scalar publication overwrites all four target words.
}
void EngineState::setScalar(uint32_t id, uint32_t value) {
    requireInitialized();
    need(knownScalar(id),"Unsupported original scalar state ID");
    need(validScalar(id,value),"Unsupported or noncanonical scalar state value");
    scalar_[id/4]=value;
    // Do NOT elide equal values: these SDK calls still publish and may replace a
    // packed quad word. Guest cache deduplication belongs to a different layer.
    if(id==0x3C || id==0x40 ||
       ((id==0x48 || id==0x4C || id==0x50) && raw(ScalarState::BlendEnable)) ||
       ((id==0x54 || id==0x58 || id==0x5C) && raw(ScalarState::BlendEnable) && raw(ScalarState::SeparateAlpha)))
        rebuildBlend();
}
uint32_t EngineState::scalar(uint32_t id) const {
    requireInitialized();need(knownScalar(id),"Unsupported original scalar state ID");
    return scalar_[id/4];
}
std::array<float,4> EngineState::blendConstant() const {
    requireInitialized();
    const uint32_t argb=raw(ScalarState::BlendConstant);
    const float scale=std::bit_cast<float>(uint32_t{0x3B808081});
    return {float((argb>>16)&255)*scale,float((argb>>8)&255)*scale,
            float(argb&255)*scale,float(argb>>24)*scale};
}
void EngineState::setSampler(uint32_t stage, uint32_t id, uint32_t value) {
    requireInitialized();need(stage<samplers_.size(),"Sampler stage is outside the sixteen application stages");
    need(validSampler(id,value),"Unsupported sampler state ID or value");
    samplers_[stage][id/4]=value;
}
uint32_t EngineState::sampler(uint32_t stage, uint32_t id) const {
    requireInitialized();need(stage<samplers_.size(),"Sampler stage is outside the sixteen application stages");
    need(id<=0x4C && id%4==0,"Unknown sampler state ID");
    return samplers_[stage][id/4];
}
uint32_t EngineState::effectiveVolumeFilter(uint32_t stage) const {
    const bool separate=sampler(stage,SamplerState::SeparateZFilter)!=0;
    return sampler(stage,separate?SamplerState::MagnificationZ:SamplerState::Magnification) |
          (sampler(stage,separate?SamplerState::MinificationZ:SamplerState::Minification)<<1);
}
void EngineState::setPackedBlend(uint32_t target, uint32_t word) {
    requireInitialized();need(target<blend_.size(),"Blend target is outside 0..3");blend_[target]=word;
}
uint32_t EngineState::effectiveBlend(uint32_t target) const {
    requireInitialized();need(target<blend_.size(),"Blend target is outside 0..3");return blend_[target];
}
bool EngineState::effectiveDepthTest(bool depthSurfaceBound) const {
    requireInitialized();return depthSurfaceBound && raw(ScalarState::DepthEnable)!=0;
}
bool EngineState::effectiveStencilTest(bool depthSurfaceBound) const {
    requireInitialized();return depthSurfaceBound && raw(ScalarState::StencilEnable)!=0;
}
void EngineState::applyScreenQuadState(uint32_t selector, bool textured) {
    requireInitialized();need(selector<quadBlend.size(),"Screen blend selector is outside the proven 0..3 modes");
    setScalar(ScalarState::DepthEnable,0);
    setScalar(ScalarState::Cull,0);
    setScalar(ScalarState::AlphaTest,1);
    setScalar(ScalarState::AlphaCompare,4);
    setScalar(ScalarState::AlphaReference,1); // Integer request, NOT float bits.
    setPackedBlend(0,quadBlend[selector]);
    if(selector!=3) setScalar(ScalarState::ExpandedBlend0,1);
    if(textured) {
        setSampler(0,SamplerState::Minification,1);
        setSampler(0,SamplerState::Magnification,1);
        setSampler(0,SamplerState::AddressU,0);
        setSampler(0,SamplerState::AddressV,0);
    }
}
void EngineState::finishScreenQuadState(uint32_t selector) {
    requireInitialized();need(selector<quadBlend.size(),"Screen blend selector is outside the proven 0..3 modes");
    setScalar(ScalarState::BlendEnable,0);
    setScalar(ScalarState::AlphaTest,0);
    setScalar(ScalarState::DepthEnable,1);
    if(selector!=3) setScalar(ScalarState::ExpandedBlend0,0);
}
ScreenStateSnapshot EngineState::requireOriginalScreenState(bool textured) const {
    return requireScreenState(textured,ScreenPolicy::Original);
}
ScreenStateSnapshot EngineState::requireOriginalEdgeState() const {
    const auto checked=requireOriginalScreenState(false);
    need(!checked.retainedDepthWrite && !checked.alphaTest && !checked.retainedBlendEnable &&
         !checked.retainedSeparateAlpha && checked.blendSelector==3 &&
         scalar(ScalarState::MultisampleMask)==UINT32_MAX && !scalar(ScalarState::TessellationMode),
         "Native edge effective draw state is unqualified");
    return checked;
}
ScreenStateSnapshot EngineState::requireOriginalAlphaClearState() const {
    need(!scalar(ScalarState::BlendEnable) && !scalar(ScalarState::AlphaTest) &&
         !scalar(ScalarState::DepthWrite) && effectiveBlend(0)==0x00010001,
         "Original alpha clear requires an unblended, untested replacement");
    return requireScreenState(false,ScreenPolicy::AlphaClear);
}
ScreenStateSnapshot EngineState::requireOriginalIm2DScreenState(bool textured) const {
    return requireScreenState(textured,ScreenPolicy::Im2D);
}
ScreenStateSnapshot EngineState::requireOriginalIm2DDrawState(bool textured) const {
    return requireScreenState(textured,ScreenPolicy::Im2DDraw);
}
ScreenStateSnapshot EngineState::requireScreenState(bool textured) const {
    return requireScreenState(textured,ScreenPolicy::Native);
}
ScreenStateSnapshot EngineState::requireScreenState(bool textured,ScreenPolicy policy) const {
    requireInitialized();
    const bool original=policy!=ScreenPolicy::Native;
    const bool im2d=policy==ScreenPolicy::Im2D || policy==ScreenPolicy::Im2DDraw;
    const bool draw=policy==ScreenPolicy::Im2DDraw;
    if(im2d && textured) {
        const auto& s=samplers_[0];
        need((s[0]==0 || s[0]==2) && (s[1]==0 || s[1]==2),"Original Im2D sampler supports repeat/clamp U/V only");
    }
    if(original) {
        need(raw(ScalarState::HalfPixelOffset)==1,"Original screen requires half-integer pixel centers");
        need(raw(ScalarState::GuardBandX)==0x3F800000 && raw(ScalarState::GuardBandY)==0x3F800000,
             "Original screen requires guardband one");
        need(!raw(ScalarState::ClipPlaneEnable) && !raw(ScalarState::ScissorEnable) && raw(ScalarState::ViewportEnable)==1,
             "Original screen requires full viewport and no user clip planes");
        need(!raw(ScalarState::Wrap0),"Original screen texture-coordinate wrapping is unsupported");
        for(auto id:{ScalarState::ExpandedBlend1,ScalarState::ExpandedBlend2,ScalarState::ExpandedBlend3})
            need(!raw(id),"Original screen has additional expanded attachments");
    }
    if(draw) {
        const auto cull=raw(ScalarState::Cull);
        need(cull==0 || cull==2 || cull==6,"Original Im2D cull state is unqualified");
        switch(blend_[0]) {
        case 0x07060706:case 0x00010001:case 0x00010706:case 0x00010106:case 0x00010186:case 0x01000100:break;
        default:throw EngineStateError("Original Im2D blend equation is unqualified");
        }
    }
    need(draw || !raw(ScalarState::DepthEnable),"Screen state requires depth request off; target-dependent depth is unproved here");
    need(!raw(ScalarState::StencilEnable),"Screen state requires stencil request off");
    need((draw || !raw(ScalarState::Cull)) && !raw(ScalarState::Fill),"Screen state requires no culling and filled polygons");
    need(raw(ScalarState::ColorMask0)==(policy==ScreenPolicy::AlphaClear?8u:15u),
         policy==ScreenPolicy::AlphaClear?"Post alpha clear requires write mask 8":"Screen state requires target-0 RGBA write mask F");
    need(!raw(ScalarState::DepthBias) && !raw(ScalarState::SlopeBias),"Screen state requires zero depth and slope bias");
    need(original || !raw(ScalarState::ExpandedBlend0),"Expanded blending precision has no verified native implementation in this contract");
    need(!raw(ScalarState::AlphaToMask),"Alpha-to-mask coverage has no verified native screen implementation");
    // The application can retain these original requests ahead of a draw. The
    // existing screen shader fixtures only establish the earlier raster policy;
    // do not silently accept the application's new pixel-center/guardband mode.
    need(original || !raw(ScalarState::HalfPixelOffset),"Application half-pixel raster policy has no verified native screen implementation");
    need(original || (raw(ScalarState::GuardBandX)==0x40000000 && raw(ScalarState::GuardBandY)==0x40000000),
         "Application guardband raster policy has no verified native screen implementation");
    const auto sharedBlend=draw?0x00010001:blend_[0];
    const auto selected=std::find(quadBlend.begin(),quadBlend.end(),sharedBlend);
    need(selected!=quadBlend.end(),"Effective target-0 blend equation is outside the four proven screen modes");
    need(!raw(ScalarState::AlphaTest) || raw(ScalarState::AlphaCompare)==4,
         "Enabled screen alpha test supports only original GREATER comparison");
    ScreenStateSnapshot result{
        false,false,raw(ScalarState::DepthWrite)!=0,raw(ScalarState::DepthCompare),
        draw?0u:raw(ScalarState::Cull),raw(ScalarState::Fill),raw(ScalarState::ColorMask0),
        raw(ScalarState::BlendEnable)!=0,raw(ScalarState::SeparateAlpha)!=0,
        sharedBlend,uint32_t(selected-quadBlend.begin()),original && raw(ScalarState::ExpandedBlend0)!=0,
        raw(ScalarState::AlphaTest)!=0,raw(ScalarState::AlphaCompare),raw(ScalarState::AlphaReference),
        float(raw(ScalarState::AlphaReference))*std::bit_cast<float>(uint32_t{0x3B808081}),std::nullopt,
        blendConstant(),raw(ScalarState::AlphaToMaskOffsets)};
    if(textured) {
        const auto& s=samplers_[0];
        need((im2d || (s[0]==0 && s[1]==0)) && (original || s[2]==0),"Screen sampler requires repeat U/V/W");
        need(s[4]==1 && s[5]==1,"Screen sampler requires linear minification/magnification");
        need((original || s[6]==2) && s[7]==0 && s[8]==0 && s[9]==1 && s[13]==13,
             "Screen sampler requires the base-map-only, zero-bias, anisotropy-one startup policy");
        need(original || !s[12],"Separate volume filtering is outside the native screen sampler contract");
        // The six remaining auxiliary fields only accept their original SDK
        // baseline; retained Z requests are inactive while separate filtering is off.
        // Border is inactive with repeat addressing. Volume draws remain unported.
        result.sampler=ScreenSamplerState{s[5],s[4],s[6],s[0],s[1],s[2],s[9],s[8],s[13],0.0f};
    }
    return result;
}

} // namespace Simpsons::Graphics
