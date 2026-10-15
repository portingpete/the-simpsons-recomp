#include "spirv_module.h"
#include "spirv_registration.h"
#include "renderer/native_backend.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>

namespace Simpsons::Graphics::Vulkan {
namespace {
// https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html
enum : uint32_t {
    OpName=5,OpEntryPoint=15,OpTypeBool=20,OpTypeInt=21,OpTypeFloat=22,OpTypeVector=23,OpTypeMatrix=24,
    OpTypeImage=25,OpTypeSampler=26,OpTypeSampledImage=27,OpTypeArray=28,OpTypeRuntimeArray=29,OpTypeStruct=30,
    OpTypePointer=32,OpConstant=43,OpVariable=59,OpDecorate=71,OpDecorateString=5632};
enum : uint32_t {DecBlock=2,DecBufferBlock=3,DecBuiltIn=11,DecLocation=30,DecBinding=33,DecDescriptorSet=34,DecSemantic=5635};
enum : uint32_t {UniformConstant=0,Input=1,Uniform=2,Output=3,StorageBuffer=12};
enum : uint32_t {BuiltInFragDepth=22,BuiltInVertexIndex=42,BuiltInInstanceIndex=43,BuiltInFragStencilRef=5014};
struct Key {
    std::array<uint32_t,4> value;
    bool operator==(const Key& other) const {return value==other.value;}
};
struct KeyHash {size_t operator()(const Key& key) const noexcept {return size_t(key.value[0])^(size_t(key.value[1])<<32)^key.value[2]^(size_t(key.value[3])<<16);}};
std::string literal(const uint32_t* words,size_t count) {
    std::string text;
    for(size_t i=0;i<count;++i) for(int b=0;b<4;++b) {
        const char c=char((words[i]>>(8*b))&0xFF);
        if(!c) return text;
        text.push_back(c);
    }
    return text;
}
[[noreturn]] void invalid(const std::string& name,const char* why) {
    throw Error("Native Vulkan shader "+name+": "+why);
}
}

std::shared_ptr<const SpirvModule> SpirvModule::fromDxbc(const void* bytecode,size_t size,VkShaderStageFlagBits stage) {
    if(!bytecode || size<32 || std::memcmp(bytecode,"DXBC",4))
        throw Error("Native Vulkan shader creation requires DXBC bytecode");
    Key key{};std::memcpy(key.value.data(),static_cast<const uint8_t*>(bytecode)+4,16);
    static std::mutex mutex;
    static std::unordered_map<Key,const SpirvShader*,KeyHash> registry;
    static std::unordered_map<Key,std::shared_ptr<const SpirvModule>,KeyHash> parsed;
    std::lock_guard lock(mutex);
    if(auto found=parsed.find(key);found!=parsed.end()) {
        if(found->second->stage()!=stage) throw Error("Native Vulkan shader bytecode is for another pipeline stage");
        return found->second;
    }
    if(registry.empty())
        for(auto* entry=spirvShaderList().load(std::memory_order_acquire);entry;entry=entry->next)
            registry.emplace(Key{entry->dxbcChecksum},entry);
    const auto entry=registry.find(key);
    if(entry==registry.end()) {
        char message[200];
        std::snprintf(message,sizeof(message),"Native Vulkan has no checked SPIR-V twin for DXBC %08X%08X%08X%08X",
            key.value[0],key.value[1],key.value[2],key.value[3]);
        throw Error(message);
    }
    auto module=std::make_shared<const SpirvModule>(std::vector<uint32_t>(entry->second->words,entry->second->words+entry->second->wordCount),
        stage,entry->second->name);
    parsed.emplace(key,module);
    return module;
}

SpirvModule::SpirvModule(std::vector<uint32_t> words,VkShaderStageFlagBits stage,std::string name)
    :words_(std::move(words)),stage_(stage),name_(std::move(name)) {
    reflect();
}

void SpirvModule::reflect() {
    if(words_.size()<5 || words_[0]!=0x07230203u) invalid(name_,"SPIR-V header is invalid");
    struct Type {uint32_t op=0;std::vector<uint32_t> ops;};
    std::unordered_map<uint32_t,Type> types;
    std::unordered_map<uint32_t,uint32_t> constants,builtins,locations,bindings,variableStorage,variableType;
    std::unordered_map<uint32_t,std::string> semantics;
    std::unordered_map<uint32_t,bool> blocks;
    std::vector<uint32_t> variables;
    uint32_t model=~0u;
    for(size_t at=5;at<words_.size();) {
        const uint32_t count=words_[at]>>16,op=words_[at]&0xFFFF;
        if(!count || at+count>words_.size()) invalid(name_,"SPIR-V instruction stream is malformed");
        const uint32_t* ops=&words_[at+1];const size_t n=count-1;
        switch(op) {
        case OpEntryPoint:if(model==~0u) model=ops[0];break;
        case OpTypeBool:case OpTypeInt:case OpTypeFloat:case OpTypeVector:case OpTypeMatrix:case OpTypeImage:case OpTypeSampler:
        case OpTypeSampledImage:case OpTypeArray:case OpTypeRuntimeArray:case OpTypeStruct:case OpTypePointer:
            types[ops[0]]={op,std::vector<uint32_t>(ops,ops+n)};break;
        case OpConstant:if(n>=3) constants[ops[1]]=ops[2];break;
        case OpVariable:variables.push_back(ops[1]);variableType[ops[1]]=ops[0];variableStorage[ops[1]]=ops[2];break;
        case OpDecorate:
            if(n<2) break;
            if(ops[1]==DecBuiltIn && n>=3) builtins[ops[0]]=ops[2];
            else if(ops[1]==DecLocation && n>=3) {locations[ops[0]]=ops[2];locationDecorations_.emplace_back(ops[0],at+3);}
            else if(ops[1]==DecBinding && n>=3) {bindings[ops[0]]=ops[2];bindingDecorations_.push_back(at+3);}
            else if(ops[1]==DecDescriptorSet && n>=3) setDecorations_.push_back(at+3);
            else if(ops[1]==DecBlock) blocks[ops[0]]=true;
            else if(ops[1]==DecBufferBlock) invalid(name_,"storage buffers are not mapped");
            break;
        case OpDecorateString:if(n>=3 && ops[1]==DecSemantic) semantics[ops[0]]=literal(ops+2,n-2);break;
        default:break;
        }
        at+=count;
    }
    const uint32_t expected=stage_==VK_SHADER_STAGE_VERTEX_BIT?0u:stage_==VK_SHADER_STAGE_FRAGMENT_BIT?4u:~1u;
    if(model!=expected) invalid(name_,"SPIR-V execution model differs from the requested D3D stage");
    auto pointee=[&](uint32_t pointer)->uint32_t {
        const auto found=types.find(pointer);
        if(found==types.end() || found->second.op!=OpTypePointer) invalid(name_,"variable type is not a pointer");
        return found->second.ops[2];
    };
    auto shape=[&](uint32_t type,SpirvVariable& variable) {
        uint32_t locations=1;
        for(int depth=0;depth<8;++depth) {
            const auto found=types.find(type);
            if(found==types.end()) invalid(name_,"stage variable type is undeclared");
            const auto& t=found->second;
            switch(t.op) {
            case OpTypeFloat:variable.base=SpirvBase::Float;variable.locations=locations;return;
            case OpTypeInt:variable.base=t.ops[2]?SpirvBase::Int:SpirvBase::Uint;variable.locations=locations;return;
            case OpTypeBool:variable.base=SpirvBase::Other;variable.locations=locations;return;
            case OpTypeVector:variable.components=t.ops[2];type=t.ops[1];
                if(types[type].op==OpTypeFloat) {variable.base=SpirvBase::Float;variable.locations=locations;return;}
                if(types[type].op==OpTypeInt) {variable.base=types[type].ops[2]?SpirvBase::Int:SpirvBase::Uint;variable.locations=locations;return;}
                variable.base=SpirvBase::Other;variable.locations=locations;return;
            case OpTypeMatrix:locations*=t.ops[2];type=t.ops[1];break;
            case OpTypeArray:locations*=constants.count(t.ops[2])?constants[t.ops[2]]:1;type=t.ops[1];break;
            default:variable.base=SpirvBase::Other;variable.locations=locations;return;
            }
        }
        invalid(name_,"stage variable type nests too deeply");
    };
    for(uint32_t id:variables) {
        const uint32_t storage=variableStorage[id];
        const uint32_t type=pointee(variableType[id]);
        if(storage==StorageBuffer) invalid(name_,"storage buffers are not mapped");
        if(storage==UniformConstant || storage==Uniform) {
            if(!bindings.count(id)) continue;
            SpirvBinding binding;binding.binding=bindings[id];
            const auto& t=types[type];
            if(storage==Uniform) {
                if(t.op!=OpTypeStruct || !blocks.count(type)) invalid(name_,"uniform variable is not a Block");
                binding.type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            } else if(t.op==OpTypeSampler) binding.type=VK_DESCRIPTOR_TYPE_SAMPLER;
            else if(t.op==OpTypeImage) {
                // OpTypeImage: result, sampled type, Dim, Depth, Arrayed, MS, Sampled, format.
                if(t.ops[6]!=1) invalid(name_,"storage images (UAVs) are not mapped");
                binding.dimension=t.ops[2];binding.arrayed=t.ops[4]!=0;binding.multisampled=t.ops[5]!=0;
                binding.type=t.ops[2]==5?VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            } else invalid(name_,"descriptor arrays and combined samplers are not mapped");
            const uint32_t b=binding.binding;
            const bool valid=binding.type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER?b<constantBufferBinding+constantBufferSlots:
                binding.type==VK_DESCRIPTOR_TYPE_SAMPLER?(b>=samplerBinding&&b<samplerBinding+samplerSlots):
                (b>=textureBinding&&b<textureBinding+textureSlots);
            if(!valid) invalid(name_,"descriptor binding lies outside its D3D register space");
            bindings_.push_back(binding);
            continue;
        }
        if(storage!=Input && storage!=Output) continue;
        if(builtins.count(id)) {
            const uint32_t builtin=builtins[id];
            if(storage==Output && builtin==BuiltInFragDepth) writesDepth_=true;
            if(storage==Output && builtin==BuiltInFragStencilRef) writesStencil_=true;
            if(storage==Input && builtin==BuiltInVertexIndex) usesVertexIndex_=true;
            if(storage==Input && builtin==BuiltInInstanceIndex) usesInstanceIndex_=true;
            continue;
        }
        if(!locations.count(id)) invalid(name_,"stage variable has neither a builtin nor a location");
        SpirvVariable variable;variable.id=id;variable.location=locations[id];
        std::string semantic=semantics.count(id)?semantics[id]:std::string();
        for(auto& c:semantic) c=char(std::toupper(static_cast<unsigned char>(c)));
        size_t digits=semantic.size();
        while(digits && std::isdigit(static_cast<unsigned char>(semantic[digits-1]))) --digits;
        variable.index=digits<semantic.size()?uint32_t(std::stoul(semantic.substr(digits))):0;
        variable.semantic=semantic.substr(0,digits);
        shape(type,variable);
        if(storage==Output && stage_==VK_SHADER_STAGE_FRAGMENT_BIT) {
            if(variable.semantic!="SV_TARGET") invalid(name_,"pixel output is not SV_Target");
            for(uint32_t i=0;i<variable.locations;++i) colorOutputs_|=1u<<(variable.location+i);
        }
        (storage==Input?inputs_:outputs_).push_back(variable);
    }
}
std::vector<uint32_t> SpirvModule::withBindingOffset(uint32_t offset) const {
    auto out=words_;
    for(size_t at:setDecorations_) out[at]=0;
    for(size_t at:bindingDecorations_) out[at]+=offset;
    return out;
}
std::vector<uint32_t> SpirvModule::withInputLocations(const std::vector<std::pair<const SpirvVariable*,uint32_t>>& remap,uint32_t offset) const {
    auto out=withBindingOffset(offset);
    for(const auto& [variable,location]:remap)
        for(const auto& [id,at]:locationDecorations_) if(id==variable->id) out[at]=location;
    return out;
}
}
