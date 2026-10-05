#pragma once
#include "renderer/skin_mesh.h"
#include <cstdint>
#include <span>
#include <vector>

namespace Simpsons {
// Decode stream0 attributes consumed by VS82007C1C (position, normal, uv,
// blend indices/weights, color). Initialize morph rows to zero; apply each
// selected stream with decodeSkinMorphStream before upload. No pointers escape.
std::vector<Graphics::SkinVertex> decodeSkinVertices(
    std::span<const uint8_t> bytes,std::span<const uint8_t> declaration,uint32_t stride,uint32_t source=0x82006348,bool alpha=false);
// Stream order follows the original selected morph loop. Each stream is an
// owned big-endian float3 array with one delta per base vertex.
void decodeSkinMorphStream(std::span<Graphics::SkinVertex> vertices,uint32_t stream,std::span<const uint8_t> bytes);
uint64_t skinMorphStreamDirtyMask(uint32_t stream);
}
