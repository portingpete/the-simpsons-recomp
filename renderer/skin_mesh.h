#pragma once
#include "renderer/native_backend.h"
#include "renderer/shadow_mesh.h"
#include "renderer/rigid_mesh.h"
#include <cstddef>

namespace Simpsons::Graphics {
// Decoded skin attributes in original FETCH order (stream0 + six morph rows).
// No normalization, endian conversion or weight repair occurs here.
struct SkinVertex {
    std::array<float,3> position{},normal{};
    std::array<float,2> uv{};
    std::array<float,4> indices{},weights{};
    std::array<float,4> color{};
    std::array<float,3> morph1{},morph2{},morph3{},morph4{},morph5{},morph6{};
    std::array<float,2> uv1{};
};
static_assert(sizeof(SkinVertex)==160&&offsetof(SkinVertex,normal)==12&&
              offsetof(SkinVertex,uv)==24&&offsetof(SkinVertex,indices)==32&&
              offsetof(SkinVertex,weights)==48&&offsetof(SkinVertex,color)==64&&
              offsetof(SkinVertex,morph1)==80&&offsetof(SkinVertex,morph6)==140&&offsetof(SkinVertex,uv1)==152);

struct SkinMeshDraw : ShadowMeshDraw {
    uint32_t blendEnable{},blendWord{},expandedBlend{};
    RigidShadowSamplePolicy shadowSamplePolicy{RigidShadowSamplePolicy::Unqualified};
    std::shared_ptr<DepthTarget> characterShadow;
    std::shared_ptr<Texture> baseTexture;
    std::array<D3D11_SAMPLER_DESC,2> samplers{}; // alpha: base0; opaque dual/textured: character depth0, base1
    // Original skin_dualtextured_uv consumes two material images at t0/t1
    // in both passes; this owner never aliases the character-shadow role.
    std::shared_ptr<Texture> secondTexture;
};
class NativeSkinMesh {
public:
    ~NativeSkinMesh();
    NativeSkinMesh(const NativeSkinMesh&)=delete;
    NativeSkinMesh& operator=(const NativeSkinMesh&)=delete;
    uint32_t vertexCount() const noexcept;
    uint32_t indexCount() const noexcept;
private:
    friend class NativeBackend;
    struct State;
    explicit NativeSkinMesh(std::unique_ptr<State>);
    std::unique_ptr<State> state;
};
// Mutable owned CPU values used by preparation before every command-list
// execution. No guest address, borrowed array, or application callback retained.
// Backend release invalidates all aliases, including recorded replay owners.
class NativeSkinReplayConstants {
public:
    ~NativeSkinReplayConstants();
    NativeSkinReplayConstants(const NativeSkinReplayConstants&)=delete;
    NativeSkinReplayConstants& operator=(const NativeSkinReplayConstants&)=delete;
private:
    friend class NativeBackend;
    struct State;
    explicit NativeSkinReplayConstants(std::unique_ptr<State>);
    std::unique_ptr<State> state;
};
}
