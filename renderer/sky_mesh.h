#pragma once
#include "renderer/native_backend.h"
#include "renderer/shadow_mesh.h"
#include <cstddef>

namespace Simpsons::Graphics {
// Decoded sky attributes in original FETCH order (position, uv, uv1). Only
// position and uv feed the rigidalpha VS; uv1 is retained decoded but
// unbound (no sky fetch consumes the second uv set).
struct SkyVertex {
    std::array<float,3> position{};
    std::array<float,2> uv{},uv1{};
};
static_assert(sizeof(SkyVertex)==28&&offsetof(SkyVertex,uv)==12&&
              offsetof(SkyVertex,uv1)==20);

struct SkyMeshDraw : ShadowMeshDraw {
    uint32_t blendEnable{},blendWord{},expandedBlend{};
    // Material rows own stages 0-2. The original stage-3 bind can instead
    // reference the live full-resolution scene-copy target used as the sky
    // line/discard layer; textures[3] remains the qualified fallback used by
    // standalone tests and material-only packets.
    std::array<std::shared_ptr<Texture>,4> textures{};
    std::shared_ptr<RenderTarget> lineTarget;
    std::array<D3D11_SAMPLER_DESC,4> samplers{};
};
class NativeSkyMesh {
public:
    ~NativeSkyMesh();
    NativeSkyMesh(const NativeSkyMesh&)=delete;
    NativeSkyMesh& operator=(const NativeSkyMesh&)=delete;
    uint32_t vertexCount() const noexcept;
    uint32_t indexCount() const noexcept;
private:
    friend class NativeBackend;
    struct State;
    explicit NativeSkyMesh(std::unique_ptr<State>);
    std::unique_ptr<State> state;
};
class NativeSkyReplayConstants {
public:
    ~NativeSkyReplayConstants();
    NativeSkyReplayConstants(const NativeSkyReplayConstants&)=delete;
    NativeSkyReplayConstants& operator=(const NativeSkyReplayConstants&)=delete;
private:
    friend class NativeBackend;
    struct State;
    explicit NativeSkyReplayConstants(std::unique_ptr<State>);
    std::unique_ptr<State> state;
};
}
