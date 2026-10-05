#pragma once
#include "renderer/shadow_mesh.h"
#include <cstdint>
#include <span>
#include <vector>

namespace Simpsons {
// Decode the original stream-zero attributes consumed by VS820C2FA0. The
// result owns values; no original pointers or console resource objects escape.
std::vector<Graphics::ShadowMeshVertex> decodeCharacterVertices(
    std::span<const uint8_t> bytes,std::span<const uint8_t> declaration,uint32_t stride,uint32_t bones);
std::vector<uint16_t> decodeCharacterIndices(std::span<const uint8_t> bytes);
}
