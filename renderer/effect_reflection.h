#pragma once
#include "effect_resources.h"
#include <optional>

namespace Simpsons::Graphics {

struct EffectConstantRange {uint32_t start,count;};
struct EffectBinding {
    uint32_t handle,usage;
    std::array<std::optional<EffectConstantRange>,2> lanes;
    // Kind2 arrays override the counts written by the original row query,
    // including unused lanes. Pass masks use the encoded binding counts.
    std::optional<uint32_t> arrayElements;
};
struct EffectClassification {
    // Only the six words actually written by82830C28. Its unwritten seventh
    // caller-stack word is deliberately not represented as metadata.
    std::array<uint32_t,6> words;
};
struct EffectReflectedParameter {
    std::string name;
    EffectBinding binding;
    EffectClassification classification;
};
struct EffectLightBlock {std::array<EffectBinding,5> members;};
struct EffectMutableClear {
    uint32_t namespaceIndex,firstLeaf,leafCount;
};
struct EffectReflectedPass {
    uint32_t techniqueHandle,passHandle;
    std::array<uint64_t,2> masks;
    std::vector<EffectMutableClear> clears;
};

// Original pass-specific usage query. Typed reflection still uses the front
// pass; draw-time texture/constant queries must use the selected technique.
EffectBinding effectPassBinding(const EffectRecord& record,uint32_t technique,uint32_t handle);

// Pure owned CPU metadata derived from a SHA-qualified EffectRecord. No guest
// writes, FX-shaped SDK object, pool retain, shader binding or rendering.
// Inactive ranges are absent: original queries incidentally read neighboring
// allocation bytes for them. This decoder does not pretend those are constants.
class EffectReflection {
public:
    EffectReflection(const EffectRecord& record,bool notSkinned,
                     std::span<const std::string_view> poolNames);
    std::vector<EffectReflectedParameter> parameters;
    std::vector<EffectClassification> classified;
    std::vector<EffectLightBlock> lights;
    std::vector<EffectReflectedPass> passes;
    uint32_t reflectionCubeIndex=UINT32_MAX,activeLights=0,lightFlags=0;
};

}
