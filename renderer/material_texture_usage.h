#pragma once
#include "effect_reflection.h"
#include <stdexcept>

namespace Simpsons::Graphics {
// The original SDK intersects dirty leaves with the selected pass category-7
// bitmap before fetching a texture value. An unused value has no owner to bind.
inline std::optional<uint32_t> pixelTextureStage(const EffectBinding& binding) {
    if(!binding.usage) {
        if(binding.lanes[0]||binding.lanes[1])
            throw std::invalid_argument("Unused material texture has an active map");
        return std::nullopt;
    }
    if(binding.usage!=0x80||binding.lanes[0]||!binding.lanes[1]||
       binding.lanes[1]->count!=1||binding.lanes[1]->start>=16)
        throw std::invalid_argument("Material texture is not a single pixel sampler");
    return binding.lanes[1]->start;
}
}
