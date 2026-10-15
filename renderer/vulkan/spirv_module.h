#pragma once
#include "vulkan_api.h"
#include <memory>
#include <string>
#include <vector>

namespace Simpsons::Graphics::Vulkan {
// Register spaces used by tools/vulkan_shader_compile.py.
constexpr uint32_t constantBufferBinding=0,samplerBinding=16,textureBinding=32,uavBinding=160;
constexpr uint32_t constantBufferSlots=14,samplerSlots=16,textureSlots=128;
// Vulkan allows one push-descriptor set per pipeline layout, so both stages
// share set 0 and pixel-stage bindings are offset by this amount.
constexpr uint32_t pixelBindingBase=256;

struct SpirvBinding {
    uint32_t binding=0;
    VkDescriptorType type=VK_DESCRIPTOR_TYPE_MAX_ENUM;
    // Sampled images: SPIR-V Dim (1=2D, 3=Cube, 5=Buffer), arrayed/multisampled.
    uint32_t dimension=0;bool arrayed=false,multisampled=false;
};
enum class SpirvBase {Float,Int,Uint,Other};
struct SpirvVariable {
    std::string semantic; // Upper-case name without index, e.g. TEXCOORD.
    uint32_t index=0,location=0,locations=1,components=4;
    SpirvBase base=SpirvBase::Float;
    uint32_t id=0;
};
// Reflection of one registered SPIR-V twin. Immutable after creation.
class SpirvModule {
public:
    // Resolves D3D bytecode through its DXBC checksum. Throws when no twin is
    // registered: a shader without a checked SPIR-V twin is unsupported.
    static std::shared_ptr<const SpirvModule> fromDxbc(const void* bytecode,size_t size,VkShaderStageFlagBits stage);
    explicit SpirvModule(std::vector<uint32_t> words,VkShaderStageFlagBits stage,std::string name);
    const std::string& name() const {return name_;}
    VkShaderStageFlagBits stage() const {return stage_;}
    const std::vector<uint32_t>& words() const {return words_;}
    const std::vector<SpirvBinding>& bindings() const {return bindings_;}
    const std::vector<SpirvVariable>& inputs() const {return inputs_;}
    const std::vector<SpirvVariable>& outputs() const {return outputs_;}
    // Pixel shaders: bit n set when SV_Target n is written.
    uint32_t colorOutputs() const {return colorOutputs_;}
    bool writesDepth() const {return writesDepth_;}
    bool writesStencil() const {return writesStencil_;}
    bool usesVertexIndex() const {return usesVertexIndex_;}
    bool usesInstanceIndex() const {return usesInstanceIndex_;}
    // Copy in descriptor set 0 with every binding number offset (pixel stage).
    std::vector<uint32_t> withBindingOffset(uint32_t offset) const;
    // Same, with stage inputs moved to the given locations (matched by semantic).
    std::vector<uint32_t> withInputLocations(const std::vector<std::pair<const SpirvVariable*,uint32_t>>& remap,uint32_t offset) const;
private:
    void reflect();
    std::vector<uint32_t> words_;
    VkShaderStageFlagBits stage_;
    std::string name_;
    std::vector<SpirvBinding> bindings_;
    std::vector<SpirvVariable> inputs_,outputs_;
    std::vector<size_t> setDecorations_; // word index of each DescriptorSet operand
    std::vector<size_t> bindingDecorations_; // word index of each Binding operand
    std::vector<std::pair<uint32_t,size_t>> locationDecorations_; // variable id, word index of Location operand
    uint32_t colorOutputs_=0;
    bool writesDepth_=false,writesStencil_=false,usesVertexIndex_=false,usesInstanceIndex_=false;
};
}
