#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>

namespace Simpsons::Graphics {

struct EngineStateError : std::runtime_error {using std::runtime_error::runtime_error;};

// Original offset-style SDK IDs, NOT desktop D3DRS/D3DSAMP enumerations.
enum class ScalarState : uint32_t {
    DepthEnable=0x28, DepthCompare=0x2C, DepthWrite=0x30, Fill=0x34, Cull=0x38,
    BlendEnable=0x3C, SeparateAlpha=0x40, BlendConstant=0x44, SourceBlend=0x48, DestinationBlend=0x4C,
    BlendOperation=0x50, AlphaSourceBlend=0x54, AlphaDestinationBlend=0x58,
    AlphaBlendOperation=0x5C, AlphaTest=0x60, AlphaReference=0x64, AlphaCompare=0x68,
    StencilEnable=0x6C, TwoSidedStencil=0x70, StencilFail=0x74, StencilDepthFail=0x78, StencilPass=0x7C,
    StencilCompare=0x80, StencilReference=0x84, StencilReadMask=0x88, StencilWriteMask=0x8C,
    BackStencilFail=0x90, BackStencilDepthFail=0x94, BackStencilPass=0x98, BackStencilCompare=0x9C,
    BackStencilReference=0xA0, BackStencilReadMask=0xA4, BackStencilWriteMask=0xA8,
    ClipPlaneEnable=0xAC, PointSize=0xB0, PointSizeMinimum=0xB4, PointSpriteEnable=0xB8,
    PointSizeMaximum=0xBC, MultisampleAntialias=0xC0, MultisampleMask=0xC4, ScissorEnable=0xC8,
    // 8244C1B0 uploads SDK2A50..2A5C to GPU2380..2383: scale, offset,
    // scale, offset. Setter8243AAA0 (CC) scales slope by16; AB68 (D0) stores offset.
    SlopeBias=0xCC, DepthBias=0xD0,
    ColorMask0=0xD4, ColorMask1=0xD8, ColorMask2=0xDC, ColorMask3=0xE0,
    TessellationMode=0xE4, MinimumTessellationLevel=0xE8, MaximumTessellationLevel=0xEC,
    Wrap0=0xF0, Wrap15=0x12C, ViewportEnable=0x130,
    ExpandedBlend0=0x134, ExpandedBlend1=0x138, ExpandedBlend2=0x13C, ExpandedBlend3=0x140,
    HalfPixelOffset=0x144, PrimitiveResetEnable=0x148, PrimitiveResetIndex=0x14C,
    AlphaToMask=0x150, AlphaToMaskOffsets=0x154, GuardBandX=0x158, GuardBandY=0x15C,
    HiStencilEnable=0x168, HiStencilWriteEnable=0x16C, HiStencilFunction=0x170, PresentInterval=0x178
};
enum class SamplerState : uint32_t {
    AddressU=0x00, AddressV=0x04, AddressW=0x08, BorderSelector=0x0C,
    Magnification=0x10, Minification=0x14, MipFilter=0x18, LodBiasBits=0x1C,
    MinimumMip=0x20, MaximumAnisotropy=0x24, MagnificationZ=0x28, MinificationZ=0x2C,
    SeparateZFilter=0x30, MaximumMip=0x34, TrilinearThreshold=0x38, AnisotropyBiasBits=0x3C,
    HorizontalGradientBias=0x40, VerticalGradientBias=0x44, WhiteBorderW=0x48, PointBorderEnable=0x4C
};
struct StateFieldEvidence {
    uint32_t id, setterAddress, sdkDefault, startupValue;
};
// Identified original fields; presence here does not authorize arbitrary values.
std::span<const StateFieldEvidence> scalarStateEvidence() noexcept;
std::span<const StateFieldEvidence> samplerStateEvidence() noexcept;

struct ScreenSamplerState {
    uint32_t minification, magnification, mipFilter;
    uint32_t addressU, addressV, addressW;
    uint32_t maximumAnisotropy, minimumMip, maximumMip;
    float lodBias;
};
// A value snapshot of ONLY the checked host state subset. This is not a draw
// permit: target/viewport, one-level 2D texture/view, shaders and other pipeline
// state must be independently validated by the native bridge. No resource facts
// are inferred here. Later owner updates do not alter an existing snapshot.
struct ScreenStateSnapshot {
    bool depthTest, stencilTest;
    bool retainedDepthWrite;
    uint32_t retainedDepthCompare, cull, fill, colorMask;
    bool retainedBlendEnable, retainedSeparateAlpha;
    uint32_t effectiveBlendWord, blendSelector;
    bool expandedBlendRequested;
    bool alphaTest;
    uint32_t alphaCompare, alphaReferenceInteger;
    float alphaReference;
    std::optional<ScreenSamplerState> sampler; // Absent for the flat shader.
    std::array<float,4> retainedBlendConstant; // Original ARGB bytes -> RGBA floats.
    uint32_t retainedAlphaToMaskOffsets; // Inactive while alpha-to-mask is off.
};

// CPU-only effective SDK state owner. No guest pointers/caches, D3D objects,
// GPU instruction execution, bindings or pending/applied queues. Caller serializes
// access and commits its guest cache only after a successful native transaction.
class EngineState {
public:
    EngineState() noexcept = default; // Deliberately unusable before initialization.
    static EngineState fromOriginalStartup() noexcept;
    bool initialized() const noexcept {return initialized_;}

    // Rejected IDs/values leave every field unchanged. Rejection must propagate;
    // it is not permission to draw using the preceding accepted state instead.
    void setScalar(uint32_t originalId, uint32_t value);
    void setScalar(ScalarState id, uint32_t value) {setScalar(uint32_t(id),value);}
    uint32_t scalar(uint32_t originalId) const;
    uint32_t scalar(ScalarState id) const {return scalar(uint32_t(id));}
    void setSampler(uint32_t stage, uint32_t originalId, uint32_t value);
    void setSampler(uint32_t stage, SamplerState id, uint32_t value) {setSampler(stage,uint32_t(id),value);}
    uint32_t sampler(uint32_t stage, uint32_t originalId) const;
    uint32_t sampler(uint32_t stage, SamplerState id) const {return sampler(stage,uint32_t(id));}
    uint32_t effectiveVolumeFilter(uint32_t stage) const;

    // Original 8243CB80 copies any uint32 verbatim to one target, independently
    // of scalar blend shadows. Unknown equations are retained but fail the gate.
    void setPackedBlend(uint32_t target, uint32_t word);
    uint32_t effectiveBlend(uint32_t target) const;
    uint32_t requestedBlendWord() const;
    std::array<float,4> blendConstant() const;
    bool effectiveDepthTest(bool depthSurfaceBound) const;
    bool effectiveStencilTest(bool depthSurfaceBound) const;

    // Only the proven scalar/sampler changes of 82756480, not a draw, shader bind,
    // texture bind, or whole state restore. Selector must be one of 0..3.
    void applyScreenQuadState(uint32_t selector, bool textured);
    void finishScreenQuadState(uint32_t selector);

    // Fail closed on state outside this bounded backend contract. Expanded
    // blending always rejects here, even on formats where it might be inactive:
    // this owner has no target format or precision-implementation proof.
    ScreenStateSnapshot requireScreenState(bool textured) const;
    // Original mode-1, in-volume screen policy. Caller must prove one-level2D
    // storage. Expanded equations are explicit; console precision is unverified.
    ScreenStateSnapshot requireOriginalScreenState(bool textured) const;
    // The original edge rectangle: the screen policy plus an unblended, untested, depth-write-free replacement
    // (blend selector 3), full sample mask and no tessellation. An expanded-precision request retained from an
    // earlier pass is accepted and reported: retail's unready sprite branch skips the reset and never sets blending
    // up for this overwrite, so the request cannot change its result. Additional expanded attachments still reject.
    ScreenStateSnapshot requireOriginalEdgeState() const;
    // Original 82773B30 clears only the scene alpha before the post filter.
    ScreenStateSnapshot requireOriginalAlphaClearState() const;
    // Same scalar/one-level Texture2D policy, with independently checked
    // repeat/clamp U/V for native Im2D. The ordinary screen gate stays strict.
    ScreenStateSnapshot requireOriginalIm2DScreenState(bool textured) const;
    // Im2D owns depth, cull and its six explicit blend equations separately.
    // Qualify the same shared screen subset without copying the whole owner
    // and overwriting those fields. The returned shared snapshot retains the
    // former depth-off/cull-none/copy-blend values; the draw uses actual state.
    ScreenStateSnapshot requireOriginalIm2DDrawState(bool textured) const;
private:
    enum class ScreenPolicy { Native, Original, Im2D, Im2DDraw, AlphaClear };
    ScreenStateSnapshot requireScreenState(bool textured,ScreenPolicy policy) const;
    void requireInitialized() const;
    void rebuildBlend() noexcept;
    uint32_t raw(ScalarState id) const noexcept {return scalar_[uint32_t(id)/4];}
    std::array<uint32_t,95> scalar_{}; // Last supported offset is 0x178.
    std::array<std::array<uint32_t,20>,16> samplers_{}; // Application stages; RenderWare uses the first eight.
    std::array<uint32_t,4> blend_{};
    bool initialized_{};
};

} // namespace Simpsons::Graphics
