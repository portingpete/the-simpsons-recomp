#pragma once
#include "native_backend.h"
#include "shadow_mesh.h"
namespace Simpsons::Graphics {
struct ParticleVertex {
    std::array<float,4> position,velocity,uvTime;
    std::array<float,3> size;
    std::array<float,4> color;
    float originalVertexId;
};
static_assert(sizeof(ParticleVertex)==80);
using ParticleConstants=std::array<std::array<float,4>,26>;
enum class ParticleShadowSamplePolicy {None,ReferenceD24FS8DepthRRRR};
enum class ParticleSecondarySamplePolicy {None,DualTexture1LinearRepeat};
enum class ParticleVertexPolicy {Ordinary,Type5};
struct ParticleDraw : ShadowMeshDraw {
    std::vector<ParticleVertex> vertices;
    ParticleConstants constants{};
    ParticleVertexPolicy vertexPolicy=ParticleVertexPolicy::Ordinary;
    std::shared_ptr<Texture> texture;
    D3D11_SAMPLER_DESC sampler{};
    ParticleSecondarySamplePolicy secondaryPolicy=ParticleSecondarySamplePolicy::None;
    std::shared_ptr<Texture> secondaryTexture;
    D3D11_SAMPLER_DESC secondarySampler{};
    ParticleShadowSamplePolicy shadowPolicy=ParticleShadowSamplePolicy::None;
    std::shared_ptr<DepthTarget> shadow;
    D3D11_SAMPLER_DESC shadowSampler{};
    uint32_t blendWord{},alphaReference{},blendEnable=1;
};
}
