#pragma once
#include "native_backend.h"
#include "shadow_mesh.h"
#include <cstddef>

namespace Simpsons::Graphics {
// Original rigid attributes; UV1 is decoded only when explicitly consumed.
// The packed tangent is opt-in and trails uv1 so the first 56 bytes stay
// byte-identical for every existing consumer and GPU declaration.
struct RigidVertex {
    std::array<float,3> position{},normal{};
    std::array<float,4> color{};
    std::array<float,2> uv{},uv1{};
    std::array<float,3> tangent{};
};
static_assert(sizeof(RigidVertex)==68&&offsetof(RigidVertex,normal)==12&&
              offsetof(RigidVertex,color)==24&&offsetof(RigidVertex,uv)==40&&offsetof(RigidVertex,uv1)==48&&
              offsetof(RigidVertex,tangent)==56);

// Explicit format-reference policy. Raw depth copying does not by itself
// establish shader sampling; the integration owner also qualifies copy phase.
enum class RigidShadowSamplePolicy : uint32_t {Unqualified,ReferenceD24FS8DepthRRRR,NotUsed};
struct RigidMeshDraw : ShadowMeshDraw {
    uint32_t blendEnable{},blendWord{},expandedBlend{};
    RigidShadowSamplePolicy shadowSamplePolicy{RigidShadowSamplePolicy::Unqualified};
    std::array<std::shared_ptr<DepthTarget>,2> shadows; // world0, character1
    std::array<D3D11_SAMPLER_DESC,2> samplers{};
    std::array<std::shared_ptr<Texture>,3> materialTextures;
    std::array<D3D11_SAMPLER_DESC,3> materialSamplers{};
    // UV opaque: material t1/t2 plus character depth t0 in shadows[0].
    // UV alpha: material t0/t1 and no shadow owners.
    std::array<std::shared_ptr<Texture>,2> uvTextures;
    std::array<D3D11_SAMPLER_DESC,2> uvSamplers{};
    std::shared_ptr<Texture> baseTexture;
    D3D11_SAMPLER_DESC baseSampler{};
    std::shared_ptr<Texture> noiseTexture;
    D3D11_SAMPLER_DESC noiseSampler{};
};
class NativeRigidMesh {
public:
    ~NativeRigidMesh();
    NativeRigidMesh(const NativeRigidMesh&)=delete;
    NativeRigidMesh& operator=(const NativeRigidMesh&)=delete;
    uint32_t vertexCount() const noexcept;
    uint32_t indexCount() const noexcept;
private:
    friend class NativeBackend;
    struct State;
    explicit NativeRigidMesh(std::unique_ptr<State>);
    std::unique_ptr<State> state;
};
// Mutable owned CPU values used by preparation before every command-list
// execution. No guest address, borrowed array, or application callback retained.
// Backend release invalidates all aliases, including recorded replay owners.
class NativeRigidReplayConstants {
public:
    ~NativeRigidReplayConstants();
    NativeRigidReplayConstants(const NativeRigidReplayConstants&)=delete;
    NativeRigidReplayConstants& operator=(const NativeRigidReplayConstants&)=delete;
private:
    friend class NativeBackend;
    struct State;
    explicit NativeRigidReplayConstants(std::unique_ptr<State>);
    std::unique_ptr<State> state;
};
}
