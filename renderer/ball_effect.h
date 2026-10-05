#pragma once
#include "shadow_mesh.h"
#include "post_filter.h"

namespace Simpsons::Graphics {
// Original 82771A18 quad vertices, in perimeter order: x, y, u, v.
// Constants retain VS c0..c8: world-to-view, view-to-screen, world center/alpha.
struct BallEffectDraw : ShadowMeshDraw {
    std::array<std::array<float,4>,4> vertices{};
    std::array<std::array<float,4>,9> constants{};
    std::shared_ptr<Texture> texture;
    D3D11_SAMPLER_DESC sampler{};
    uint32_t blendWord{},blendEnable=1,alphaReference{};
};
// Original explicit-UV screen rectangles used by the full distortion phase.
struct DistortionDraw : PostFilterDraw {
    std::array<std::array<float,4>,3> quad{};
    std::array<std::shared_ptr<RenderTarget>,2> inputs{};
    std::array<D3D11_SAMPLER_DESC,2> samplers{};
};
}
