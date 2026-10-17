#pragma once
#include <filesystem>
#include "d3d11_vulkan.h"
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace Simpsons::Graphics::Vulkan {
VkPrimitiveTopology primitiveTopology(D3D11_PRIMITIVE_TOPOLOGY topology);
uint8_t topologyClass(D3D11_PRIMITIVE_TOPOLOGY topology);

// Owner-thread cache of graphics pipelines and push-descriptor layouts.
// Viewport, scissor, cull, front face, depth bias, depth/stencil tests,
// stencil reference, blend constants, topology and vertex strides are
// dynamic, so D3D11 state changes of those kinds never create pipelines.
class PipelineCache {
public:
    explicit PipelineCache(D3D11Device& device);
    ~PipelineCache();
    PipelineCache(const PipelineCache&)=delete;
    PipelineCache& operator=(const PipelineCache&)=delete;
    struct Entry {VkPipeline pipeline=VK_NULL_HANDLE;VkPipelineLayout layout=VK_NULL_HANDLE;int8_t cacheHit=-1;};
    Entry get(const PipelineKey& key,const ContextState& state);
    size_t size() const {return pipelines_.size();}
    // Writes the driver cache to its file (also done on destruction).
    void save() noexcept;
    // The same on a worker thread; a request during a save runs it again after.
    void saveInBackground();
private:
    VkDescriptorSetLayout setLayout(const SpirvModule& vertex,const SpirvModule* pixel);
    VkPipelineLayout layoutFor(const SpirvModule& vertex,const SpirvModule* pixel);
    Entry create(const PipelineKey& key,const ContextState& state);
    D3D11Device& device_;
    VkPipelineCache cache_=VK_NULL_HANDLE;
    std::unordered_map<PipelineKey,Entry,PipelineKeyHash> pipelines_;
    std::unordered_map<std::string,VkDescriptorSetLayout> setLayouts_;
    std::unordered_map<std::string,VkPipelineLayout> layouts_;
    std::filesystem::path path_;
    size_t savedAt_=16;
    std::mutex saveMutex_;
    std::thread saver_;
    bool saving_=false,savePending_=false; // Guarded by saveMutex_.
};
}
