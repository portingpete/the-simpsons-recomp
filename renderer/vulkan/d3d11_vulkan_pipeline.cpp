#include "d3d11_vulkan_pipeline.h"
#include "renderer/native_backend.h"
#include "runtime/stall_profiler.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <windows.h>

namespace Simpsons::Graphics::Vulkan {
size_t PipelineKeyHash::operator()(const PipelineKey& key) const noexcept {
    // FNV-1a over the trivially comparable key bytes.
    const auto* bytes=reinterpret_cast<const uint8_t*>(&key);
    uint64_t hash=1469598103934665603ull;
    for(size_t i=0;i<sizeof(key);++i) {hash^=bytes[i];hash*=1099511628211ull;}
    return size_t(hash);
}
VkPrimitiveTopology primitiveTopology(D3D11_PRIMITIVE_TOPOLOGY topology) {
    switch(topology) {
    case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    default:throw Error("Native Vulkan primitive topology is unsupported (adjacency/patches)");
    }
}
uint8_t topologyClass(D3D11_PRIMITIVE_TOPOLOGY topology) {
    switch(topology) {
    case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:return 1;
    case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:return 2;
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:return 3;
    default:return 0;
    }
}
namespace {
VkBlendFactor blendFactor(D3D11_BLEND blend) {
    switch(blend) {
    case D3D11_BLEND_ZERO:return VK_BLEND_FACTOR_ZERO;
    case D3D11_BLEND_ONE:return VK_BLEND_FACTOR_ONE;
    case D3D11_BLEND_SRC_COLOR:return VK_BLEND_FACTOR_SRC_COLOR;
    case D3D11_BLEND_INV_SRC_COLOR:return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case D3D11_BLEND_SRC_ALPHA:return VK_BLEND_FACTOR_SRC_ALPHA;
    case D3D11_BLEND_INV_SRC_ALPHA:return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case D3D11_BLEND_DEST_ALPHA:return VK_BLEND_FACTOR_DST_ALPHA;
    case D3D11_BLEND_INV_DEST_ALPHA:return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case D3D11_BLEND_DEST_COLOR:return VK_BLEND_FACTOR_DST_COLOR;
    case D3D11_BLEND_INV_DEST_COLOR:return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case D3D11_BLEND_SRC_ALPHA_SAT:return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case D3D11_BLEND_BLEND_FACTOR:return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case D3D11_BLEND_INV_BLEND_FACTOR:return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case D3D11_BLEND_SRC1_COLOR:return VK_BLEND_FACTOR_SRC1_COLOR;
    case D3D11_BLEND_INV_SRC1_COLOR:return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
    case D3D11_BLEND_SRC1_ALPHA:return VK_BLEND_FACTOR_SRC1_ALPHA;
    case D3D11_BLEND_INV_SRC1_ALPHA:return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
    default:throw Error("Native Vulkan blend factor is invalid");
    }
}
VkBlendOp blendOp(D3D11_BLEND_OP op) {
    switch(op) {
    case D3D11_BLEND_OP_ADD:return VK_BLEND_OP_ADD;
    case D3D11_BLEND_OP_SUBTRACT:return VK_BLEND_OP_SUBTRACT;
    case D3D11_BLEND_OP_REV_SUBTRACT:return VK_BLEND_OP_REVERSE_SUBTRACT;
    case D3D11_BLEND_OP_MIN:return VK_BLEND_OP_MIN;
    case D3D11_BLEND_OP_MAX:return VK_BLEND_OP_MAX;
    default:throw Error("Native Vulkan blend operation is invalid");
    }
}
bool integerFormat(VkFormat format) {
    switch(format) {
    case VK_FORMAT_A2B10G10R10_UINT_PACK32:case VK_FORMAT_R8_UINT:case VK_FORMAT_R8_SINT:case VK_FORMAT_R8G8_UINT:case VK_FORMAT_R8G8_SINT:
    case VK_FORMAT_R8G8B8A8_UINT:case VK_FORMAT_R8G8B8A8_SINT:case VK_FORMAT_R16_UINT:case VK_FORMAT_R16_SINT:case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16_SINT:case VK_FORMAT_R16G16B16A16_UINT:case VK_FORMAT_R16G16B16A16_SINT:case VK_FORMAT_R32_UINT:case VK_FORMAT_R32_SINT:
    case VK_FORMAT_R32G32_UINT:case VK_FORMAT_R32G32_SINT:case VK_FORMAT_R32G32B32A32_UINT:case VK_FORMAT_R32G32B32A32_SINT:return true;
    default:return false;
    }
}
bool hasStencil(VkFormat format) {return format==VK_FORMAT_D32_SFLOAT_S8_UINT || format==VK_FORMAT_D24_UNORM_S8_UINT;}
// SIMPSONS_VULKAN_PIPELINE_LOG=1 logs each created pipeline: duration and
// whether the driver cache supplied it (pipeline creation feedback).
bool logPipelines() {
    static const bool enabled=[]{const char* v=std::getenv("SIMPSONS_VULKAN_PIPELINE_LOG");return v && *v=='1';}();
    return enabled;
}
std::string layoutKey(const SpirvModule* module,VkShaderStageFlags stage) {
    std::string key=std::to_string(stage)+":";
    if(module) for(const auto& binding:module->bindings()) key+=std::to_string(binding.binding)+"/"+std::to_string(int(binding.type))+",";
    return key;
}
}

namespace {
// Driver pipeline cache kept between runs beside the executable.
// SIMPSONS_VULKAN_PIPELINE_CACHE=0 disables it; another value names the file.
std::filesystem::path pipelineCachePath() {
    if(const char* text=std::getenv("SIMPSONS_VULKAN_PIPELINE_CACHE");text&&*text) {
        if(text[0]=='0'&&!text[1])return {};
        return std::filesystem::path(text);
    }
    wchar_t module[MAX_PATH]{};
    const DWORD length=GetModuleFileNameW(nullptr,module,MAX_PATH);
    if(!length||length>=MAX_PATH)return {};
    return std::filesystem::path(module).parent_path()/"vulkan-pipeline-cache.bin";
}
// The header Vulkan defines for cache data: drivers reject foreign data, but
// checking it first keeps another GPU's or driver's cache from being offered.
bool compatibleCache(const std::vector<uint8_t>& data,const VkPhysicalDeviceProperties& properties) {
    if(data.size()<32)return false;
    uint32_t length{},version{},vendor{},device{};
    std::memcpy(&length,data.data(),4);std::memcpy(&version,data.data()+4,4);
    std::memcpy(&vendor,data.data()+8,4);std::memcpy(&device,data.data()+12,4);
    return length>=32&&length<=data.size()&&version==VK_PIPELINE_CACHE_HEADER_VERSION_ONE&&
        vendor==properties.vendorID&&device==properties.deviceID&&
        !std::memcmp(data.data()+16,properties.pipelineCacheUUID,VK_UUID_SIZE);
}
}
PipelineCache::PipelineCache(D3D11Device& device):device_(device) {
    path_=pipelineCachePath();
    VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    std::vector<uint8_t> initial;
    if(!path_.empty()) {
        std::ifstream in(path_,std::ios::binary);
        if(in) {
            initial.assign(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
            if(!compatibleCache(initial,device_.vulkan().properties()))initial.clear();
        }
    }
    info.initialDataSize=initial.size();info.pInitialData=initial.empty()?nullptr:initial.data();
    if(vkCreatePipelineCache(device_.vulkan().device(),&info,nullptr,&cache_)!=VK_SUCCESS&&!initial.empty()) {
        info.initialDataSize=0;info.pInitialData=nullptr;
        check(vkCreatePipelineCache(device_.vulkan().device(),&info,nullptr,&cache_),"pipeline cache creation");
    } else if(!cache_)check(VK_ERROR_INITIALIZATION_FAILED,"pipeline cache creation");
    if(!initial.empty())std::fprintf(stderr,"[NATIVE VULKAN] pipeline cache loaded %zu bytes from %ls\n",initial.size(),path_.c_str());
}
void PipelineCache::save() noexcept {
    if(path_.empty()||!cache_)return;
    try {
        const VkDevice device=device_.vulkan().device();
        size_t size=0;
        if(vkGetPipelineCacheData(device,cache_,&size,nullptr)!=VK_SUCCESS||!size)return;
        std::vector<uint8_t> data(size);
        if(vkGetPipelineCacheData(device,cache_,&size,data.data())!=VK_SUCCESS)return;
        data.resize(size);
        // Write beside the target and replace it, so a crash never leaves a torn file.
        auto temporary=path_;temporary+=L".tmp";
        {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));if(!out)return;}
        std::error_code error;std::filesystem::rename(temporary,path_,error);
    } catch(...) {}
}
void PipelineCache::saveInBackground() {
    if(path_.empty()||!cache_)return;
    std::lock_guard lock(saveMutex_);
    savePending_=true;
    if(saving_)return;
    if(saver_.joinable())saver_.join(); // Already finished: it cleared saving_ before returning.
    saving_=true;
    saver_=std::thread([this]{
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        for(;;) {
            {std::lock_guard lock(saveMutex_);if(!savePending_){saving_=false;return;}savePending_=false;}
            // Pipeline caches are internally synchronized (no EXTERNALLY_SYNCHRONIZED
            // flag), so the render thread keeps creating pipelines meanwhile.
            save();
        }
    });
}
PipelineCache::~PipelineCache() {
    if(saver_.joinable())saver_.join();
    save();
    const VkDevice device=device_.vulkan().device();
    for(auto& [key,entry]:pipelines_) vkDestroyPipeline(device,entry.pipeline,nullptr);
    for(auto& [key,layout]:layouts_) vkDestroyPipelineLayout(device,layout,nullptr);
    for(auto& [key,layout]:setLayouts_) vkDestroyDescriptorSetLayout(device,layout,nullptr);
    if(cache_) vkDestroyPipelineCache(device,cache_,nullptr);
}
// One push-descriptor set holds both stages (Vulkan permits a single push set
// per pipeline layout); pixel bindings start at pixelBindingBase.
VkDescriptorSetLayout PipelineCache::setLayout(const SpirvModule& vertex,const SpirvModule* pixel) {
    const std::string key=layoutKey(&vertex,VK_SHADER_STAGE_VERTEX_BIT)+"|"+layoutKey(pixel,VK_SHADER_STAGE_FRAGMENT_BIT);
    if(auto found=setLayouts_.find(key);found!=setLayouts_.end()) return found->second;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for(const auto& binding:vertex.bindings())
        bindings.push_back({binding.binding,binding.type,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr});
    if(pixel) for(const auto& binding:pixel->bindings())
        bindings.push_back({pixelBindingBase+binding.binding,binding.type,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr});
    if(bindings.size()>device_.vulkan().capabilities().maxPushDescriptors)
        throw Error("Native Vulkan shaders "+vertex.name()+"/"+(pixel?pixel->name():std::string("-"))+
            " bind more descriptors than one push set allows");
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    info.bindingCount=uint32_t(bindings.size());info.pBindings=bindings.data();
    VkDescriptorSetLayout layout=VK_NULL_HANDLE;
    check(vkCreateDescriptorSetLayout(device_.vulkan().device(),&info,nullptr,&layout),"descriptor set layout creation");
    setLayouts_.emplace(key,layout);
    return layout;
}
VkPipelineLayout PipelineCache::layoutFor(const SpirvModule& vertex,const SpirvModule* pixel) {
    const std::string key=layoutKey(&vertex,VK_SHADER_STAGE_VERTEX_BIT)+"|"+layoutKey(pixel,VK_SHADER_STAGE_FRAGMENT_BIT);
    if(auto found=layouts_.find(key);found!=layouts_.end()) return found->second;
    const VkDescriptorSetLayout set=setLayout(vertex,pixel);
    VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    info.setLayoutCount=1;info.pSetLayouts=&set;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    check(vkCreatePipelineLayout(device_.vulkan().device(),&info,nullptr,&layout),"pipeline layout creation");
    layouts_.emplace(key,layout);
    return layout;
}
PipelineCache::Entry PipelineCache::get(const PipelineKey& key,const ContextState& state) {
    if(auto found=pipelines_.find(key);found!=pipelines_.end()) return found->second;
    StallProfiler::Scope profile(StallProfiler::Section::Rendering,"Vulkan.CreateGraphicsPipeline",nullptr,key.vertex);
    LARGE_INTEGER start{},end{},frequency{};
    if(logPipelines()) QueryPerformanceCounter(&start);
    const Entry entry=create(key,state);
    pipelines_.emplace(key,entry);
    if(logPipelines()) {
        QueryPerformanceCounter(&end);QueryPerformanceFrequency(&frequency);
        std::fprintf(stderr,"[NATIVE VULKAN PIPELINE] count=%zu ms=%.3f cache=%s vs=%s ps=%s\n",pipelines_.size(),
            1000.0*double(end.QuadPart-start.QuadPart)/double(frequency.QuadPart),
            entry.cacheHit<0?"unknown":entry.cacheHit?"hit":"miss",state.vertexShader->spirv().name().c_str(),
            state.pixelShader?state.pixelShader->spirv().name().c_str():"-");
    }
    // Keep the file current as new pipelines appear (doubling intervals). Reading
    // and writing the multi-megabyte cache took milliseconds on the render thread.
    if(pipelines_.size()>=savedAt_*2){savedAt_=pipelines_.size();saveInBackground();}
    return entry;
}
PipelineCache::Entry PipelineCache::create(const PipelineKey& key,const ContextState& state) {
    auto& vertex=*state.vertexShader.Get();
    D3D11PixelShader* pixel=state.pixelShader.Get();
    const auto& vs=vertex.spirv();
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_VERTEX_BIT,vertex.module(),vs.name().c_str(),nullptr};
    if(pixel) stages[1]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_FRAGMENT_BIT,
        pixel->moduleFor(vertex),pixel->spirv().name().c_str(),nullptr};

    // Vertex input: D3D matches layout elements to shader inputs by semantic.
    std::vector<VkVertexInputAttributeDescription> attributes;
    std::vector<VkVertexInputBindingDescription> bindings;
    uint32_t slots=0,instanceSlots=0;
    for(const auto& input:vs.inputs()) {
        if(!state.inputLayout) throw Error("Native Vulkan draw of "+vs.name()+" needs an input layout");
        for(uint32_t k=0;k<input.locations;++k) {
            const InputElement* element=nullptr;
            for(const auto& candidate:state.inputLayout->elements())
                if(candidate.semantic==input.semantic && candidate.index==input.index+k) {element=&candidate;break;}
            if(!element) throw Error("Native Vulkan input layout lacks "+input.semantic+std::to_string(input.index+k)+" for "+vs.name());
            const VkFormat format=vertexFormat(element->format);
            if(format==VK_FORMAT_UNDEFINED) throw Error("Native Vulkan vertex format is unsupported for "+input.semantic);
            attributes.push_back({input.location+k,element->slot,format,element->offset});
            slots|=1u<<element->slot;
            if(element->instance) instanceSlots|=1u<<element->slot;
        }
    }
    for(uint32_t slot=0;slot<32;++slot) if(slots&(1u<<slot))
        bindings.push_back({slot,0,(instanceSlots&(1u<<slot))?VK_VERTEX_INPUT_RATE_INSTANCE:VK_VERTEX_INPUT_RATE_VERTEX});
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount=uint32_t(bindings.size());vertexInput.pVertexBindingDescriptions=bindings.data();
    vertexInput.vertexAttributeDescriptionCount=uint32_t(attributes.size());vertexInput.pVertexAttributeDescriptions=attributes.data();

    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology=key.topologyClass==1?VK_PRIMITIVE_TOPOLOGY_POINT_LIST:key.topologyClass==2?VK_PRIMITIVE_TOPOLOGY_LINE_LIST:VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount=1;viewport.scissorCount=1;
    // D3D11 clamps depth to the viewport range and clips only when
    // DepthClipEnable is set; depth clamp plus explicit clip control matches.
    VkPipelineRasterizationDepthClipStateCreateInfoEXT clip{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT};
    clip.depthClipEnable=key.depthClip;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.pNext=&clip;raster.depthClampEnable=VK_TRUE;
    raster.polygonMode=key.wireframe?VK_POLYGON_MODE_LINE:VK_POLYGON_MODE_FILL;raster.lineWidth=1.0f;
    if(key.wireframe && !device_.vulkan().capabilities().fillModeNonSolid) throw Error("Native Vulkan adapter lacks wireframe rasterization");
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    const VkSampleMask sampleMask=key.sampleMask;multisample.pSampleMask=&sampleMask;
    const D3D11_BLEND_DESC* blend=state.blend?&state.blend->desc():nullptr;
    multisample.alphaToCoverageEnable=blend && blend->AlphaToCoverageEnable;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

    std::array<VkPipelineColorBlendAttachmentState,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> attachments{};
    uint32_t colorCount=0;
    for(uint32_t i=0;i<attachments.size();++i) if(key.colors[i]!=VK_FORMAT_UNDEFINED) colorCount=i+1;
    const uint32_t written=pixel?pixel->spirv().colorOutputs():0;
    for(uint32_t i=0;i<colorCount;++i) {
        auto& out=attachments[i];
        if(key.colors[i]==VK_FORMAT_UNDEFINED) continue;
        D3D11_RENDER_TARGET_BLEND_DESC target{};
        target.BlendEnable=FALSE;target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        target.SrcBlend=target.SrcBlendAlpha=D3D11_BLEND_ONE;target.DestBlend=target.DestBlendAlpha=D3D11_BLEND_ZERO;
        target.BlendOp=target.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        if(blend) target=blend->RenderTarget[blend->IndependentBlendEnable?i:0];
        // Without a shader output Vulkan writes undefined values; D3D11 leaves
        // the target unchanged. Mask those attachments.
        out.colorWriteMask=(written&(1u<<i))?VkColorComponentFlags(target.RenderTargetWriteMask&0xF):0;
        if(target.BlendEnable && !integerFormat(key.colors[i])) {
            out.blendEnable=VK_TRUE;
            out.srcColorBlendFactor=blendFactor(target.SrcBlend);out.dstColorBlendFactor=blendFactor(target.DestBlend);
            out.colorBlendOp=blendOp(target.BlendOp);
            out.srcAlphaBlendFactor=blendFactor(target.SrcBlendAlpha);out.dstAlphaBlendFactor=blendFactor(target.DestBlendAlpha);
            out.alphaBlendOp=blendOp(target.BlendOpAlpha);
        }
    }
    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount=colorCount;colorBlend.pAttachments=attachments.data();
    static constexpr VkDynamicState dynamic[]={
        VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_DEPTH_BIAS,VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        VK_DYNAMIC_STATE_CULL_MODE,VK_DYNAMIC_STATE_FRONT_FACE,VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
        VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE,VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE,VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
        VK_DYNAMIC_STATE_STENCIL_OP,VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE};
    VkPipelineDynamicStateCreateInfo dynamics{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamics.dynamicStateCount=uint32_t(std::size(dynamic));dynamics.pDynamicStates=dynamic;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount=colorCount;rendering.pColorAttachmentFormats=key.colors.data();
    if(key.depth!=VK_FORMAT_UNDEFINED) {
        rendering.depthAttachmentFormat=key.depth;
        if(hasStencil(key.depth)) rendering.stencilAttachmentFormat=key.depth;
    }
    Entry entry;
    entry.layout=layoutFor(vs,pixel?&pixel->spirv():nullptr);
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext=&rendering;info.stageCount=pixel?2:1;info.pStages=stages;
    info.pVertexInputState=&vertexInput;info.pInputAssemblyState=&assembly;info.pViewportState=&viewport;
    info.pRasterizationState=&raster;info.pMultisampleState=&multisample;info.pDepthStencilState=&depth;
    info.pColorBlendState=&colorBlend;info.pDynamicState=&dynamics;info.layout=entry.layout;
    VkPipelineCreationFeedback feedback{};
    VkPipelineCreationFeedbackCreateInfo feedbackInfo{VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO};
    if(logPipelines()) {feedbackInfo.pPipelineCreationFeedback=&feedback;feedbackInfo.pNext=info.pNext;info.pNext=&feedbackInfo;}
    const VkResult result=vkCreateGraphicsPipelines(device_.vulkan().device(),cache_,1,&info,nullptr,&entry.pipeline);
    if(feedback.flags&VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT)
        entry.cacheHit=(feedback.flags&VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT)?1:0;
    if(result<0) {
        std::fprintf(stderr,"[NATIVE VULKAN] pipeline creation failed for VS %s PS %s\n",vs.name().c_str(),pixel?pixel->spirv().name().c_str():"(none)");
        fail(result,"graphics pipeline creation");
    }
    return entry;
}
}
