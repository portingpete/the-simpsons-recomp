#pragma once
#include "native_backend.h"
#include "temporal_origins.h"
namespace Simpsons::Graphics {
// Captured before queued presentation. Immutable source/owner metadata; this
// token proves the ordered raw front copy, not physical display acceptance.
class NativeSceneFeedbackFrame {
    friend class NativeBackend;
    friend struct NativeUiCompositeProbe;
    const NativeBackend* owner{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    NativeTemporalSceneOrigins expected;
    std::shared_ptr<RenderTarget> rawColor;
    std::shared_ptr<DepthTarget> rawDepth;
    std::shared_ptr<RenderTarget> capturedColor;
    std::shared_ptr<DepthTarget> capturedDepth;
    // Wrapper lifetime does not freeze its private COM resource/view fields.
    // Pin each actual resource and every view participating in this capture.
    ComPtr<ID3D11Texture2D> rawColorResource,rawDepthResource,capturedColorResource,capturedDepthResource;
    ComPtr<ID3D11RenderTargetView> rawColorView,capturedColorView;
    ComPtr<ID3D11ShaderResourceView> rawColorSampledView,capturedColorSampledView;
    ComPtr<ID3D11DepthStencilView> rawDepthView,capturedDepthView;
    ComPtr<ID3D11ShaderResourceView> rawDepthSampledView,rawStencilSampledView,capturedDepthSampledView,capturedStencilSampledView;
    std::shared_ptr<NativeCopySubmission> rawFrontCopy;
    std::shared_ptr<NativeCopySubmission> snapshotCompletion;
    std::array<float,2> sourceJitter{};
    uint64_t serial{};
    uint64_t snapshotGeneration{};
};
}
