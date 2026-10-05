#pragma once
#include "native_backend.h"
#include <cstddef>

namespace Simpsons::Graphics {
// Already decoded CPU attributes, in original FETCH order. No normalization,
// endian conversion, coordinate permutation or weight repair occurs here.
struct ShadowMeshVertex {
    std::array<float,3> position;
    std::array<float,2> uv;
    std::array<float,4> weights,indices;
};
static_assert(sizeof(ShadowMeshVertex)==52 && offsetof(ShadowMeshVertex,uv)==12 &&
              offsetof(ShadowMeshVertex,weights)==20 && offsetof(ShadowMeshVertex,indices)==36);

// Explicit opt-in to the audited local reference arithmetic, not a claim of
// physical-console raster/interpolation/rounding equivalence.
enum class ShadowMeshDepthPolicy : uint32_t { Unqualified, Reference20e4Rne };
struct ShadowMeshDraw {
    uint32_t primitiveType{},indexCount{},startIndex{};
    int32_t baseVertex{};
    // Original integer XYWH followed by raw float32 depth endpoint bits.
    std::array<uint32_t,6> viewport{};
    // Original L,T,R,B, with exclusive right/bottom edges.
    std::array<uint32_t,4> scissor{};
    // Original SDK scalar values; these are NOT D3D11 enum values.
    uint32_t depthEnable{},depthWrite{},depthCompare{},cull{},fill{},colorMask{};
    uint32_t stencilEnable{},alphaTest{},scissorEnable{},halfPixelOffset{};
    uint32_t primitiveReset{},primitiveResetIndex{},viewportEnable{},clipPlaneEnable{};
    uint32_t multisampleAntialias{},multisampleMask{},alphaToMask{};
    uint32_t depthBiasBits{},slopeBiasBits{};
    ShadowMeshDepthPolicy depthPolicy{ShadowMeshDepthPolicy::Unqualified};
};

class NativeShadowMesh {
public:
    ~NativeShadowMesh();
    NativeShadowMesh(const NativeShadowMesh&)=delete;
    NativeShadowMesh& operator=(const NativeShadowMesh&)=delete;
    uint32_t vertexCount() const noexcept;
    uint32_t indexCount() const noexcept;
private:
    friend class NativeBackend;
    struct State;
    explicit NativeShadowMesh(std::unique_ptr<State>);
    std::unique_ptr<State> state;
};
}
