#pragma once
#include "material_resources.h"
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Simpsons::Graphics {

struct EffectError : std::runtime_error {using std::runtime_error::runtime_error;};

struct EffectIdentity {
    // row is the combined ordinal across the two original registration tables.
    uint32_t originalAddress,recordBytes,row;
    std::string_view name,sha256;
};
std::span<const EffectIdentity> originalEffectIdentities() noexcept;

struct EffectScalar {uint32_t sdkId,value;};
struct EffectSampler {uint32_t stage,sdkId,value;};
struct EffectTechnique {
    std::string name;
    uint32_t handle,passHandle,contextOffset,vertexShaderAddress,pixelShaderAddress;
    std::vector<EffectScalar> scalars;
    std::vector<EffectSampler> samplers;
};
struct EffectParameter {
    std::string name;
    uint32_t handle;
    std::array<uint32_t,2> descriptorWords;
};
struct EffectShader {
    uint32_t originalAddress,bodyOffset,recordBytes;
    MaterialStage stage;
};

// Immutable owned original metadata, never a relocated SDK FX or a mutable pool.
// The admitted49 effects each have exactly one pass per technique. Null shader
// sentinel associations have address zero; they are not missing metadata.
// Shared handles describe the serialized local namespace, not arbitrary live
// merged-pool order. Runtime pool association and all rendering stay separate.
class EffectRecord {
public:
    EffectRecord(uint32_t address,std::span<const uint8_t> bytes);
    EffectRecord(const EffectRecord&)=delete;
    EffectRecord& operator=(const EffectRecord&)=delete;
    EffectRecord(EffectRecord&&)=delete;
    EffectRecord& operator=(EffectRecord&&)=delete;
    const EffectIdentity& identity() const noexcept {return *identity_;}
    std::span<const uint8_t> bytes() const noexcept {return bytes_;}
    std::span<const uint8_t> body() const noexcept {return bytes().subspan(12);}
    std::span<const EffectTechnique> techniques() const noexcept {return techniques_;}
    std::span<const EffectParameter> parameters(bool shared) const noexcept {return parameters_[shared?1:0];}
    std::span<const uint8_t> privateDefaults() const noexcept {return body().subspan(defaultOffsets_[0],defaultBytes_[0]);}
    std::span<const uint8_t> sharedDefaults() const noexcept {return body().subspan(defaultOffsets_[1],defaultBytes_[1]);}
    std::span<const EffectShader> shaders() const noexcept {return shaders_;}
    uint32_t cacheBytes() const noexcept {return cacheBytes_;}

    // Cache serialization by the caller: first 24*NT bytes are headers, then
    // each technique's scalar block (12-byte rows) followed by its sampler block
    // (16-byte rows), followed by the next technique. Saved slots stay unwritten.
private:
    const EffectIdentity* identity_{};
    std::vector<uint8_t> bytes_;
    std::vector<EffectTechnique> techniques_;
    std::array<std::vector<EffectParameter>,2> parameters_;
    std::array<uint32_t,2> defaultOffsets_{},defaultBytes_{};
    std::vector<EffectShader> shaders_;
    uint32_t cacheBytes_{};
};

}
