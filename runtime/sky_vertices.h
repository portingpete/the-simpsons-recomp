#pragma once
#include "renderer/sky_mesh.h"
namespace Simpsons {
std::vector<Graphics::SkyVertex> decodeSkyVertices(std::span<const uint8_t> bytes,
    std::span<const uint8_t> declaration,uint32_t stride);
}
