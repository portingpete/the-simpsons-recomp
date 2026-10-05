#pragma once
#include "native_backend.h"
#include "shadow_mesh.h"
namespace Simpsons::Graphics {
struct ImmediateVertex {std::array<float,3> position;float alpha;std::array<float,4> uv;};
static_assert(sizeof(ImmediateVertex)==32);
struct ImmediateDraw : ShadowMeshDraw {
    std::vector<ImmediateVertex> vertices;
    std::array<std::array<float,4>,5> constants{};
    std::shared_ptr<Texture> texture;
    std::shared_ptr<Texture> texture2;
    D3D11_SAMPLER_DESC sampler{};
    uint32_t primitive=6; // Original type 6: strip; type 4: independent triangles.
    uint32_t blendWord{},alphaReference{},blendEnable=1,alphaTest=1;
    bool radial=false;
    bool nullTexture=false; // Qualified original single-texture effect with no stage-zero fetch.
    bool projected=false;
    std::array<std::array<float,4>,5> projectionConstants{}; // Original VS c21..25.
    std::shared_ptr<DepthTarget> projectionDepth;
    D3D11_SAMPLER_DESC projectionSampler{};
    std::shared_ptr<RenderTarget> query;
};
}
