#pragma once
#include "native_backend.h"

namespace Simpsons::Graphics {
// Original 82773CD0 rectangle-list input: three float2 corners, stride eight.
// The backend expands its implicit fourth corner; no vertex/UV adjustment.
struct PostFilterDraw {
    std::shared_ptr<RenderTarget> input;
    // Explicit-UV original Screen VSTextured path used by distortion passes.
    std::optional<std::array<float,12>> texturedVertices;
    std::shared_ptr<RenderTarget> secondaryInput;
    D3D11_SAMPLER_DESC secondarySampler{};
    uint32_t pixelShader{}; // VS821529C8, or VS82152880 with texturedVertices.
    std::array<float,6> vertices{};
    // Original 82770AF0 TRIANGLESTRIP: four float2 corners, drawn as stored.
    std::optional<std::array<float,8>> strip;
    // Screen-effect depth stage (Dof stage 1, Fog stage 0): native t3/s2.
    std::shared_ptr<DepthTarget> depthInput;
    D3D11_SAMPLER_DESC depthSampler{};
    std::array<std::array<float,4>,10> pixelConstants{}; // Original PS c0..9, native b0.
    D3D11_SAMPLER_DESC sampler{};
    // Original integer XYWH followed by float32 depth endpoint bits.
    std::array<uint32_t,6> viewport{};
    std::array<uint32_t,4> scissor{};
    uint32_t blendEnable{},blendWord=0x00010001,expandedBlend{},colorMask=15;
    uint32_t depthEnable{},depthWrite{},depthCompare{},stencilEnable{},alphaTest{};
    uint32_t cull{},fill{},scissorEnable{},halfPixelOffset=1,viewportEnable=1;
    uint32_t clipPlaneEnable{},multisampleAntialias{},multisampleMask=0xFFFFFFFF,alphaToMask{};
    uint32_t depthBiasBits{},slopeBiasBits{};
};
static_assert(sizeof(PostFilterDraw::pixelConstants)==160);
}
