#pragma once
#include "renderer/zprepass_mesh.h"
#include <cstdint>
#include <span>
#include <vector>

namespace Simpsons {
// Caller must qualify original metadata bones==0 and committed Boolean b0==0.
// VS8214A8A4 then consumes only position0. Decode its finite FLOAT3 bits without
// coordinate changes; initialize the eight unused native attributes to +0.
// Dead declaration rows still require individually qualified formats, streams,
// methods, bounds and unique semantics. Their payload bytes are never decoded.
std::vector<Graphics::ZPrepassVertex> decodeStaticZPrepassVertices(
    std::span<const uint8_t> bytes,std::span<const uint8_t> declaration,uint32_t stride);
}
