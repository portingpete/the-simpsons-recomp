#pragma once
#include "renderer/rigid_mesh.h"
namespace Simpsons {
std::vector<Graphics::RigidVertex> decodeRigidVertices(std::span<const uint8_t> bytes,
    std::span<const uint8_t> declaration,uint32_t stride,bool consumeUv1=false,bool consumeTangent=false,bool consumeNormal=true);
}
