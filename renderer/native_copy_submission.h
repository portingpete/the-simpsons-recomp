#pragma once
#include "native_backend.h"

namespace Simpsons::Graphics {
// Internal completion/ownership storage shared by copy and resolve submissions.
// Completion establishes GPU work only, never guest SDK/display acceptance.
class NativeCopySubmission {
    friend class NativeBackend;
    const NativeBackend* owner{};
    ComPtr<ID3D11Query> event;
    ComPtr<ID3D11Texture2D> sourceResource,destinationResource;
    std::shared_ptr<void> sourceOwner,destinationOwner;
    // Captured only by an actual same-extent CopyResource from registered Scene.
    // The source revision is a snapshot: later source writes do not change it.
    std::optional<TemporalRasterOrigin> rasterOrigin;
    uint32_t rasterComponent{};
    uint64_t rasterEpoch{};
    ComPtr<ID3D11Device> rasterDevice;
    ComPtr<ID3D11DeviceContext> rasterContext;
    std::shared_ptr<RenderTarget> rasterColor;
    std::shared_ptr<DepthTarget> rasterDepth;
    ComPtr<ID3D11RenderTargetView> rasterColorView;
    ComPtr<ID3D11DepthStencilView> rasterDepthView;
    ComPtr<ID3D11ShaderResourceView> rasterSampledView;
    bool submitted{},complete{},commandsFlushed{};
};
}
