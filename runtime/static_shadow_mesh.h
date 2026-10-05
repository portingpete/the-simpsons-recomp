#pragma once
#include "renderer/shadow_mesh.h"
#include <cstdint>
#include <span>
#include <vector>

namespace Simpsons {
// Decode stream-zero position0/UV0 for the captured unskinned shadow path.
// The caller must qualify metadata bones==0 and committed VS c40.x==0.
// Absent blend attributes become positive-zero lanes in the native 52-byte
// layout, not fabricated weights: their shader branch is inactive. Normal0,
// color0 and UV1 are unused; only their observed formats are accepted.
// The result owns exact finite position/UV values and borrows no input memory.
std::vector<Graphics::ShadowMeshVertex> decodeStaticShadowVertices(
    std::span<const uint8_t> bytes,std::span<const uint8_t> declaration,uint32_t stride);
}
