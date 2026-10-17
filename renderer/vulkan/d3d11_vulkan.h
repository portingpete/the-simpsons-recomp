#pragma once
// Native Vulkan implementation of the D3D11 interface subset used by the
// renderer. The renderer keeps one code path; selecting Vulkan replaces only
// the device object behind it. Unsupported D3D11 features fail explicitly
// (E_NOTIMPL/E_INVALIDARG with a diagnostic, or Graphics::Error); nothing
// reports success without performing the operation.
#include "vulkan_device.h"
#include "vulkan_formats.h"
#include "spirv_module.h"
#include "vulkan_device_factory.h"
#include <d3d11_4.h>
#include <dxgi1_5.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Simpsons::Graphics::Vulkan {
using Microsoft::WRL::ComPtr;
class D3D11Device;
class D3D11Context;
class PipelineCache;
class DxgiSwapChain;

uint64_t nextObjectId() noexcept;
VkCompareOp compareOp(D3D11_COMPARISON_FUNC function);
void reportUnsupported(const char* what) noexcept;

class PrivateData {
public:
    HRESULT get(REFGUID guid,UINT* size,void* data);
    HRESULT set(REFGUID guid,UINT size,const void* data);
    HRESULT setInterface(REFGUID guid,const IUnknown* object);
private:
    struct Entry {GUID guid;std::vector<uint8_t> bytes;ComPtr<IUnknown> object;};
    std::mutex mutex_;
    std::vector<Entry> entries_;
};

// Common IUnknown/ID3D11DeviceChild behaviour. Children keep a private
// reference on their device (GetDevice returns it publicly), so the device
// outlives every object created from it.
template<class Base,class Derived>
class DeviceChild : public Base {
public:
    explicit DeviceChild(D3D11Device* device,bool holdDevice=true);
    virtual ~DeviceChild();
    DeviceChild(const DeviceChild&)=delete;
    DeviceChild& operator=(const DeviceChild&)=delete;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    void STDMETHODCALLTYPE GetDevice(ID3D11Device** device) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid,UINT* size,void* data) override {return privateData_.get(guid,size,data);}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid,UINT size,const void* data) override {return privateData_.set(guid,size,data);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid,const IUnknown* object) override {return privateData_.setInterface(guid,object);}
    D3D11Device* parent() const {return device_;}
    uint64_t id() const {return id_;}
protected:
    D3D11Device* const device_;
    const bool holdsDevice_;
    std::atomic<ULONG> refs_{1};
    const uint64_t id_;
    PrivateData privateData_;
};

class __declspec(uuid("338ab72b-cbdb-5b7a-8bf9-d6be7005aebf")) D3D11Buffer final : public DeviceChild<ID3D11Buffer,D3D11Buffer> {
public:
    enum class Storage {Device,Renamed,Staging};
    struct Range {VkBuffer buffer=VK_NULL_HANDLE;VkDeviceSize offset=0;};
    D3D11Buffer(D3D11Device* device,const D3D11_BUFFER_DESC& desc,const D3D11_SUBRESOURCE_DATA* initial);
    ~D3D11Buffer() override;
    void STDMETHODCALLTYPE GetType(D3D11_RESOURCE_DIMENSION* dimension) override {*dimension=D3D11_RESOURCE_DIMENSION_BUFFER;}
    void STDMETHODCALLTYPE SetEvictionPriority(UINT priority) override {eviction_=priority;}
    UINT STDMETHODCALLTYPE GetEvictionPriority() override {return eviction_;}
    void STDMETHODCALLTYPE GetDesc(D3D11_BUFFER_DESC* desc) override {*desc=desc_;}
    const D3D11_BUFFER_DESC& desc() const {return desc_;}
    Storage storage() const {return storage_;}
    // Storage that commands recorded now read or write.
    Range range() const;
    uint8_t* hostData() const;
    // Owner thread: replace host storage; earlier commands keep the old slot.
    void rename(uint64_t retireAfter);
    // Owner thread: rename while preserving current bytes (partial writes).
    void renamePreserving(uint64_t retireAfter);
    bool cpuCurrent() const {return cpuCurrent_;}
    void setCpuCurrent(bool value) {cpuCurrent_=value;}
    const HostBuffer& staging() const {return host_;}
    uint64_t gpuWrite=0,gpuRead=0; // Staging map waits.
private:
    friend DeviceChild; // Calls supports() non-virtually.
    bool supports(REFIID riid) const;
    D3D11_BUFFER_DESC desc_;
    Storage storage_;
    UINT eviction_=0;
    VkBuffer buffer_=VK_NULL_HANDLE;VmaAllocation allocation_=nullptr;
    HostSlot slot_;
    std::deque<std::pair<uint64_t,HostSlot>> spare_;
    HostBuffer host_;
    bool cpuCurrent_=true;
};

class __declspec(uuid("5d891131-348b-5fe2-9d3f-f43b9c107319")) D3D11Texture2D final : public DeviceChild<ID3D11Texture2D,D3D11Texture2D> {
public:
    struct Layout {VkDeviceSize offset=0,stencilOffset=0;uint32_t rowPitch=0,depthPitch=0,width=0,height=0;};
    D3D11Texture2D(D3D11Device* device,const D3D11_TEXTURE2D_DESC& desc,const D3D11_SUBRESOURCE_DATA* initial);
    ~D3D11Texture2D() override;
    void STDMETHODCALLTYPE GetType(D3D11_RESOURCE_DIMENSION* dimension) override {*dimension=D3D11_RESOURCE_DIMENSION_TEXTURE2D;}
    void STDMETHODCALLTYPE SetEvictionPriority(UINT priority) override {eviction_=priority;}
    UINT STDMETHODCALLTYPE GetEvictionPriority() override {return eviction_;}
    void STDMETHODCALLTYPE GetDesc(D3D11_TEXTURE2D_DESC* desc) override {*desc=desc_;}
    const D3D11_TEXTURE2D_DESC& desc() const {return desc_;}
    const FormatInfo& format() const {return format_;}
    bool isStaging() const {return desc_.Usage==D3D11_USAGE_STAGING;}
    VkImage image() const {return image_;}
    uint32_t subresources() const {return desc_.MipLevels*desc_.ArraySize;}
    uint32_t mipWidth(uint32_t mip) const {return std::max(1u,desc_.Width>>mip);}
    uint32_t mipHeight(uint32_t mip) const {return std::max(1u,desc_.Height>>mip);}
    const Layout& layout(UINT subresource) const {return layouts_[subresource];}
    const HostBuffer& staging() const {return host_;}
    // Interleaved D3D layout of combined depth/stencil staging subresources.
    std::vector<uint8_t>& shadow() {return shadow_;}
    uint64_t gpuWrite=0,gpuRead=0;
    D3D11_MAP mappedType=D3D11_MAP(0);UINT mappedSubresource=0;
private:
    friend DeviceChild; // Calls supports() non-virtually.
    bool supports(REFIID riid) const;
    void createImage();
    void createStaging();
    void upload(const D3D11_SUBRESOURCE_DATA* initial);
    void uploadDepthStencil(const D3D11_SUBRESOURCE_DATA* initial,const std::function<void(VkCommandBuffer)>& toGeneral);
    D3D11_TEXTURE2D_DESC desc_;
    FormatInfo format_;
    UINT eviction_=0;
    VkImage image_=VK_NULL_HANDLE;VmaAllocation allocation_=nullptr;
    HostBuffer host_;
    std::vector<Layout> layouts_;
    std::vector<uint8_t> shadow_;
};

struct ViewRange {uint32_t mip=0,mips=1,layer=0,layers=1;};
inline bool overlaps(const ViewRange& a,const ViewRange& b) {
    return a.mip<b.mip+b.mips && b.mip<a.mip+a.mips && a.layer<b.layer+b.layers && b.layer<a.layer+a.layers;
}

class __declspec(uuid("3ce499a0-2fcf-5ccb-864d-91169c29070d")) D3D11ShaderResourceView final : public DeviceChild<ID3D11ShaderResourceView,D3D11ShaderResourceView> {
public:
    D3D11ShaderResourceView(D3D11Device* device,ID3D11Resource* resource,const D3D11_SHADER_RESOURCE_VIEW_DESC* desc);
    ~D3D11ShaderResourceView() override;
    void STDMETHODCALLTYPE GetResource(ID3D11Resource** resource) override {resource_.CopyTo(resource);}
    void STDMETHODCALLTYPE GetDesc(D3D11_SHADER_RESOURCE_VIEW_DESC* desc) override {*desc=desc_;}
    ID3D11Resource* resource() const {return resource_.Get();}
    D3D11Texture2D* texture() const {return texture_;}
    VkImageView view() const {return view_;}
    const ViewRange& range() const {return range_;}
private:
    friend DeviceChild; // Calls supports() non-virtually.
    bool supports(REFIID riid) const;
    D3D11_SHADER_RESOURCE_VIEW_DESC desc_{};
    ComPtr<ID3D11Resource> resource_;
    D3D11Texture2D* texture_=nullptr;
    VkImageView view_=VK_NULL_HANDLE;
    ViewRange range_;
};

class __declspec(uuid("6769104b-1ad8-5241-bf70-1421d59e64b9")) D3D11RenderTargetView final : public DeviceChild<ID3D11RenderTargetView,D3D11RenderTargetView> {
public:
    D3D11RenderTargetView(D3D11Device* device,ID3D11Resource* resource,const D3D11_RENDER_TARGET_VIEW_DESC* desc);
    ~D3D11RenderTargetView() override;
    void STDMETHODCALLTYPE GetResource(ID3D11Resource** resource) override {resource_.CopyTo(resource);}
    void STDMETHODCALLTYPE GetDesc(D3D11_RENDER_TARGET_VIEW_DESC* desc) override {*desc=desc_;}
    ID3D11Resource* resource() const {return resource_.Get();}
    D3D11Texture2D* texture() const {return texture_;}
    VkImageView view() const {return view_;}
    VkFormat format() const {return format_.format;}
    const ViewFormat& viewFormat() const {return format_;}
    const ViewRange& range() const {return range_;}
    VkExtent2D extent() const {return extent_;}
private:
    friend DeviceChild; // Calls supports() non-virtually.
    bool supports(REFIID riid) const;
    D3D11_RENDER_TARGET_VIEW_DESC desc_{};
    ComPtr<ID3D11Resource> resource_;
    D3D11Texture2D* texture_=nullptr;
    VkImageView view_=VK_NULL_HANDLE;
    ViewFormat format_;
    ViewRange range_;
    VkExtent2D extent_{};
};

class __declspec(uuid("57ea3a67-6c02-5148-a882-9e507e6f6de0")) D3D11DepthStencilView final : public DeviceChild<ID3D11DepthStencilView,D3D11DepthStencilView> {
public:
    D3D11DepthStencilView(D3D11Device* device,ID3D11Resource* resource,const D3D11_DEPTH_STENCIL_VIEW_DESC* desc);
    ~D3D11DepthStencilView() override;
    void STDMETHODCALLTYPE GetResource(ID3D11Resource** resource) override {resource_.CopyTo(resource);}
    void STDMETHODCALLTYPE GetDesc(D3D11_DEPTH_STENCIL_VIEW_DESC* desc) override {*desc=desc_;}
    ID3D11Resource* resource() const {return resource_.Get();}
    D3D11Texture2D* texture() const {return texture_;}
    VkImageView view() const {return view_;}
    VkFormat format() const {return texture_->format().format;}
    VkImageAspectFlags aspects() const {return aspects_;}
    const ViewRange& range() const {return range_;}
    VkExtent2D extent() const {return extent_;}
private:
    friend DeviceChild; // Calls supports() non-virtually.
    bool supports(REFIID riid) const;
    D3D11_DEPTH_STENCIL_VIEW_DESC desc_{};
    ComPtr<ID3D11Resource> resource_;
    D3D11Texture2D* texture_=nullptr;
    VkImageView view_=VK_NULL_HANDLE;
    VkImageAspectFlags aspects_=0;
    ViewRange range_;
    VkExtent2D extent_{};
};

// UAVs are retained D3D11 state only: no shader stage that could access them
// is ever created, but bindings, hazards and hidden counters behave as D3D11.
class __declspec(uuid("cc78ffc9-017e-5c11-a37e-8ebeff848613")) D3D11UnorderedAccessView final : public DeviceChild<ID3D11UnorderedAccessView,D3D11UnorderedAccessView> {
public:
    D3D11UnorderedAccessView(D3D11Device* device,ID3D11Resource* resource,const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc);
    void STDMETHODCALLTYPE GetResource(ID3D11Resource** resource) override {resource_.CopyTo(resource);}
    void STDMETHODCALLTYPE GetDesc(D3D11_UNORDERED_ACCESS_VIEW_DESC* desc) override {*desc=desc_;}
    ID3D11Resource* resource() const {return resource_.Get();}
    uint32_t counter=0;
private:
    friend DeviceChild; // Calls supports() non-virtually.
    bool supports(REFIID riid) const;
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc_{};
    ComPtr<ID3D11Resource> resource_;
};

class __declspec(uuid("f6858e60-bfb7-5e96-a313-2ef990b8c902")) D3D11SamplerState final : public DeviceChild<ID3D11SamplerState,D3D11SamplerState> {
public:
    D3D11SamplerState(D3D11Device* device,const D3D11_SAMPLER_DESC& desc);
    ~D3D11SamplerState() override;
    void STDMETHODCALLTYPE GetDesc(D3D11_SAMPLER_DESC* desc) override {*desc=desc_;}
    VkSampler sampler() const {return sampler_;}
private:
    friend DeviceChild; // Calls supports() non-virtually.
    bool supports(REFIID riid) const;
    D3D11_SAMPLER_DESC desc_;
    VkSampler sampler_=VK_NULL_HANDLE;
};
// Identical descriptors share one object, as D3D11 does.
class __declspec(uuid("0936a1bb-6a5b-50fe-9282-2b40d37ccf5d")) D3D11BlendState final : public DeviceChild<ID3D11BlendState,D3D11BlendState> {
public:
    D3D11BlendState(D3D11Device* device,const D3D11_BLEND_DESC& desc):DeviceChild(device),desc_(desc){}
    ~D3D11BlendState() override;
    void STDMETHODCALLTYPE GetDesc(D3D11_BLEND_DESC* desc) override {*desc=desc_;}
    const D3D11_BLEND_DESC& desc() const {return desc_;}
private:
    friend DeviceChild; // Calls supports() non-virtually.
    bool supports(REFIID riid) const;
    D3D11_BLEND_DESC desc_;
};
class __declspec(uuid("4f258198-60b6-51b3-94dc-2458190dd7da")) D3D11DepthStencilState final : public DeviceChild<ID3D11DepthStencilState,D3D11DepthStencilState> {
public:
    D3D11DepthStencilState(D3D11Device* device,const D3D11_DEPTH_STENCIL_DESC& desc):DeviceChild(device),desc_(desc){}
    ~D3D11DepthStencilState() override;
    void STDMETHODCALLTYPE GetDesc(D3D11_DEPTH_STENCIL_DESC* desc) override {*desc=desc_;}
    const D3D11_DEPTH_STENCIL_DESC& desc() const {return desc_;}
private:
    friend DeviceChild; // Calls supports() non-virtually.
    bool supports(REFIID riid) const;
    D3D11_DEPTH_STENCIL_DESC desc_;
};
class __declspec(uuid("17edc7f7-7d23-56db-8f12-b9f42bfc8ef9")) D3D11RasterizerState final : public DeviceChild<ID3D11RasterizerState,D3D11RasterizerState> {
public:
    D3D11RasterizerState(D3D11Device* device,const D3D11_RASTERIZER_DESC& desc):DeviceChild(device),desc_(desc){}
    ~D3D11RasterizerState() override;
    void STDMETHODCALLTYPE GetDesc(D3D11_RASTERIZER_DESC* desc) override {*desc=desc_;}
    const D3D11_RASTERIZER_DESC& desc() const {return desc_;}
private:
    friend DeviceChild; // Calls supports() non-virtually.
    bool supports(REFIID riid) const;
    D3D11_RASTERIZER_DESC desc_;
};

struct InputElement {
    std::string semantic; // Upper case.
    uint32_t index=0,slot=0,offset=0,step=0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    bool instance=false;
};
class __declspec(uuid("109084b9-fe66-5730-82b8-e10e650a0d31")) D3D11InputLayout final : public DeviceChild<ID3D11InputLayout,D3D11InputLayout> {
public:
    D3D11InputLayout(D3D11Device* device,std::vector<InputElement> elements,uint64_t contentId)
        :DeviceChild(device),elements_(std::move(elements)),contentId_(contentId){}
    const std::vector<InputElement>& elements() const {return elements_;}
    // Equal for every layout of this device with the same elements; pipelines
    // are keyed by it, so per-mesh layout objects share their pipelines.
    uint64_t contentId() const {return contentId_;}
private:
    friend DeviceChild; // Calls supports() non-virtually.
    bool supports(REFIID riid) const;
    std::vector<InputElement> elements_;
    const uint64_t contentId_;
};

class __declspec(uuid("99b8d84b-7e8d-5b79-90f5-2155b5611d59")) D3D11VertexShader final : public DeviceChild<ID3D11VertexShader,D3D11VertexShader> {
public:
    D3D11VertexShader(D3D11Device* device,std::shared_ptr<const SpirvModule> spirv);
    ~D3D11VertexShader() override;
    const SpirvModule& spirv() const {return *spirv_;}
    // SPIR-V twins are parsed once per DXBC and never freed, so the module
    // address identifies the shader code for the life of the process.
    uint64_t contentId() const {return reinterpret_cast<uintptr_t>(spirv_.get());}
    VkShaderModule module() const {return module_;}
private:
    friend DeviceChild; // Calls supports() non-virtually.
    bool supports(REFIID riid) const;
    std::shared_ptr<const SpirvModule> spirv_;
    VkShaderModule module_=VK_NULL_HANDLE;
};
class __declspec(uuid("fec29bd5-45cc-54d2-9571-6826dd852bc3")) D3D11PixelShader final : public DeviceChild<ID3D11PixelShader,D3D11PixelShader> {
public:
    D3D11PixelShader(D3D11Device* device,std::shared_ptr<const SpirvModule> spirv);
    ~D3D11PixelShader() override;
    const SpirvModule& spirv() const {return *spirv_;}
    uint64_t contentId() const {return reinterpret_cast<uintptr_t>(spirv_.get());}
    // Module whose stage inputs use the locations the vertex shader writes,
    // matched by semantic (D3D links stages by semantic, SPIR-V by location).
    VkShaderModule moduleFor(const D3D11VertexShader& vertex);
private:
    friend DeviceChild; // Calls supports() non-virtually.
    bool supports(REFIID riid) const;
    std::shared_ptr<const SpirvModule> spirv_;
    VkShaderModule module_=VK_NULL_HANDLE;
    std::mutex mutex_;
    std::unordered_map<uint64_t,VkShaderModule> linked_;
};

enum class QueryPool {Timestamp,Occlusion,Statistics};
// Event, timestamp, disjoint, occlusion, pipeline-statistics queries and
// occlusion predicates (ID3D11Predicate is ID3D11Query with its own IID).
class __declspec(uuid("f6fd83f8-e38a-5bb6-9503-c283c07286d1")) D3D11Query final : public DeviceChild<ID3D11Predicate,D3D11Query> {
public:
    D3D11Query(D3D11Device* device,const D3D11_QUERY_DESC& desc):DeviceChild(device),desc_(desc){}
    ~D3D11Query() override;
    UINT STDMETHODCALLTYPE GetDataSize() override;
    void STDMETHODCALLTYPE GetDesc(D3D11_QUERY_DESC* desc) override {*desc=desc_;}
    const D3D11_QUERY_DESC& desc() const {return desc_;}
    // Occlusion and statistics queries accumulate one pool slot per command
    // buffer they span: Vulkan queries cannot cross a submission.
    bool segmented() const {
        return desc_.Query==D3D11_QUERY_OCCLUSION || desc_.Query==D3D11_QUERY_OCCLUSION_PREDICATE || desc_.Query==D3D11_QUERY_PIPELINE_STATISTICS;
    }
    QueryPool pool() const {return desc_.Query==D3D11_QUERY_PIPELINE_STATISTICS?QueryPool::Statistics:desc_.Query==D3D11_QUERY_TIMESTAMP?QueryPool::Timestamp:QueryPool::Occlusion;}
    void releaseSlots();
    uint64_t serial=0;     // Batch that completes this query; zero until End.
    uint32_t slot=~0u;     // Timestamp pool slot.
    std::vector<uint32_t> segments;
    bool begun=false,active=false;
private:
    friend DeviceChild; // Calls supports() non-virtually.
    bool supports(REFIID riid) const;
    D3D11_QUERY_DESC desc_;
};

// Everything a graphics pipeline bakes in; the rest is dynamic state.
// Object ids (never reused) identify shaders, layouts and blend states.
struct PipelineKey {
    uint64_t vertex=0,pixel=0,layout=0,blend=0;
    uint32_t sampleMask=~0u;
    uint8_t topologyClass=0,wireframe=0,depthClip=1;
    std::array<VkFormat,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> colors{};
    VkFormat depth=VK_FORMAT_UNDEFINED;
    bool operator==(const PipelineKey&) const=default;
};
struct PipelineKeyHash {size_t operator()(const PipelineKey& key) const noexcept;};

using RecordedCommand=std::function<void(D3D11Context&)>;
class __declspec(uuid("adcd82d6-4737-51f0-87e9-ae516f5539c8")) D3D11CommandList final : public DeviceChild<ID3D11CommandList,D3D11CommandList> {
public:
    D3D11CommandList(D3D11Device* device,std::vector<RecordedCommand> commands):DeviceChild(device),commands_(std::move(commands)){}
    UINT STDMETHODCALLTYPE GetContextFlags() override {return 0;}
    const std::vector<RecordedCommand>& commands() const {return commands_;}
private:
    friend DeviceChild; // Calls supports() non-virtually.
    bool supports(REFIID riid) const;
    std::vector<RecordedCommand> commands_;
};

struct StageBindings {
    std::array<ComPtr<D3D11Buffer>,D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> constantBuffers;
    std::array<UINT,D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> firstConstant{},constantCount{};
    std::array<ComPtr<D3D11ShaderResourceView>,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> resources;
    std::array<ComPtr<D3D11SamplerState>,D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> samplers;
    uint32_t resourceEnd=0; // One past the highest bound resource slot.
    uint32_t bufferEnd=0,samplerEnd=0; // One past the highest slot set since the last clear.
    void clear() {
        for(uint32_t i=0;i<bufferEnd;++i) {constantBuffers[i].Reset();firstConstant[i]=constantCount[i]=0;}
        for(uint32_t i=0;i<resourceEnd;++i) resources[i].Reset();
        for(uint32_t i=0;i<samplerEnd;++i) samplers[i].Reset();
        bufferEnd=resourceEnd=samplerEnd=0;
    }
};
// Everything except the large binding arrays, which ContextState resets only
// up to their highest written slot.
struct ContextScalars {
    ComPtr<D3D11VertexShader> vertexShader;
    ComPtr<D3D11PixelShader> pixelShader;
    ComPtr<D3D11InputLayout> inputLayout;
    D3D11_PRIMITIVE_TOPOLOGY topology=D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ComPtr<D3D11Buffer> indexBuffer;
    DXGI_FORMAT indexFormat=DXGI_FORMAT_UNKNOWN;
    UINT indexOffset=0;
    std::array<ComPtr<D3D11RenderTargetView>,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> renderTargets;
    ComPtr<D3D11DepthStencilView> depthStencil;
    bool uavsBound=false; // Any UAV in either array: skips per-view hazard scans.
    // Stream-output targets: retained D3D11 state; draws with one bound are rejected.
    std::array<ComPtr<D3D11Buffer>,D3D11_SO_BUFFER_SLOT_COUNT> streamOutputs;
    std::array<UINT,D3D11_SO_BUFFER_SLOT_COUNT> streamOffsets{};
    ComPtr<D3D11BlendState> blend;
    std::array<float,4> blendFactor{1,1,1,1};
    UINT sampleMask=0xFFFFFFFFu;
    ComPtr<D3D11DepthStencilState> depthStencilState;
    UINT stencilRef=0;
    ComPtr<D3D11RasterizerState> rasterizer;
    ComPtr<D3D11Query> predicate;
    BOOL predicateValue=FALSE;
    std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
    UINT viewportCount=0;
    std::array<D3D11_RECT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors{};
    UINT scissorCount=0;
};
struct ContextState : ContextScalars {
    StageBindings vertex,pixel;
    // Geometry/hull/domain/compute bindings: retained D3D11 state with no
    // shader to read it (those shader types are never created here).
    std::array<StageBindings,4> auxiliary;
    std::array<ComPtr<D3D11Buffer>,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertexBuffers;
    std::array<UINT,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> strides{},offsets{};
    uint32_t vertexBufferEnd=0; // One past the highest slot set since the last clear.
    std::array<ComPtr<D3D11UnorderedAccessView>,D3D11_1_UAV_SLOT_COUNT> outputUavs,computeUavs;
    // Default state. Command lists start and end with a clear, so the ~1100
    // references are reset only where something was bound.
    void clear() {
        const bool uavs=uavsBound;
        static_cast<ContextScalars&>(*this)=ContextScalars{};
        vertex.clear();pixel.clear();for(auto& stage:auxiliary) stage.clear();
        for(uint32_t i=0;i<vertexBufferEnd;++i) {vertexBuffers[i].Reset();strides[i]=offsets[i]=0;}
        vertexBufferEnd=0;
        if(uavs) {for(auto& uav:outputUavs) uav.Reset();for(auto& uav:computeUavs) uav.Reset();}
    }
};
class __declspec(uuid("2fb11400-69e0-59fa-8b9e-210f58ed1959")) D3D11DeviceContextState final : public DeviceChild<ID3DDeviceContextState,D3D11DeviceContextState> {
public:
    explicit D3D11DeviceContextState(D3D11Device* device):DeviceChild(device),state(std::make_unique<ContextState>()){}
    std::unique_ptr<ContextState> state;
private:
    friend DeviceChild; // Calls supports() non-virtually.
    bool supports(REFIID riid) const;
};

// Immediate (owned by the device, reference count forwarded) or deferred.
class __declspec(uuid("b9eee9be-f3dd-524e-8928-08c009ebca45")) D3D11Context final : public DeviceChild<ID3D11DeviceContext1,D3D11Context> {
public:
    D3D11Context(D3D11Device* device,bool deferred);
    ~D3D11Context() override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    // ID3D11DeviceContext
    void STDMETHODCALLTYPE VSSetConstantBuffers(UINT StartSlot,UINT NumBuffers,ID3D11Buffer* const* ppConstantBuffers) override;
    void STDMETHODCALLTYPE PSSetShaderResources(UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView* const* ppShaderResourceViews) override;
    void STDMETHODCALLTYPE PSSetShader(ID3D11PixelShader* pPixelShader,ID3D11ClassInstance* const* ppClassInstances,UINT NumClassInstances) override;
    void STDMETHODCALLTYPE PSSetSamplers(UINT StartSlot,UINT NumSamplers,ID3D11SamplerState* const* ppSamplers) override;
    void STDMETHODCALLTYPE VSSetShader(ID3D11VertexShader* pVertexShader,ID3D11ClassInstance* const* ppClassInstances,UINT NumClassInstances) override;
    void STDMETHODCALLTYPE DrawIndexed(UINT IndexCount,UINT StartIndexLocation,INT BaseVertexLocation) override;
    void STDMETHODCALLTYPE Draw(UINT VertexCount,UINT StartVertexLocation) override;
    HRESULT STDMETHODCALLTYPE Map(ID3D11Resource* pResource,UINT Subresource,D3D11_MAP MapType,UINT MapFlags,D3D11_MAPPED_SUBRESOURCE* pMappedResource) override;
    void STDMETHODCALLTYPE Unmap(ID3D11Resource* pResource,UINT Subresource) override;
    void STDMETHODCALLTYPE PSSetConstantBuffers(UINT StartSlot,UINT NumBuffers,ID3D11Buffer* const* ppConstantBuffers) override;
    void STDMETHODCALLTYPE IASetInputLayout(ID3D11InputLayout* pInputLayout) override;
    void STDMETHODCALLTYPE IASetVertexBuffers(UINT StartSlot,UINT NumBuffers,ID3D11Buffer* const* ppVertexBuffers,const UINT* pStrides,const UINT* pOffsets) override;
    void STDMETHODCALLTYPE IASetIndexBuffer(ID3D11Buffer* pIndexBuffer,DXGI_FORMAT Format,UINT Offset) override;
    void STDMETHODCALLTYPE DrawIndexedInstanced(UINT IndexCountPerInstance,UINT InstanceCount,UINT StartIndexLocation,INT BaseVertexLocation,UINT StartInstanceLocation) override;
    void STDMETHODCALLTYPE DrawInstanced(UINT VertexCountPerInstance,UINT InstanceCount,UINT StartVertexLocation,UINT StartInstanceLocation) override;
    void STDMETHODCALLTYPE GSSetConstantBuffers(UINT StartSlot,UINT NumBuffers,ID3D11Buffer* const* ppConstantBuffers) override;
    void STDMETHODCALLTYPE GSSetShader(ID3D11GeometryShader* pShader,ID3D11ClassInstance* const* ppClassInstances,UINT NumClassInstances) override;
    void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY Topology) override;
    void STDMETHODCALLTYPE VSSetShaderResources(UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView* const* ppShaderResourceViews) override;
    void STDMETHODCALLTYPE VSSetSamplers(UINT StartSlot,UINT NumSamplers,ID3D11SamplerState* const* ppSamplers) override;
    void STDMETHODCALLTYPE Begin(ID3D11Asynchronous* pAsync) override;
    void STDMETHODCALLTYPE End(ID3D11Asynchronous* pAsync) override;
    HRESULT STDMETHODCALLTYPE GetData(ID3D11Asynchronous* pAsync,void* pData,UINT DataSize,UINT GetDataFlags) override;
    void STDMETHODCALLTYPE SetPredication(ID3D11Predicate* pPredicate,BOOL PredicateValue) override;
    void STDMETHODCALLTYPE GSSetShaderResources(UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView* const* ppShaderResourceViews) override;
    void STDMETHODCALLTYPE GSSetSamplers(UINT StartSlot,UINT NumSamplers,ID3D11SamplerState* const* ppSamplers) override;
    void STDMETHODCALLTYPE OMSetRenderTargets(UINT NumViews,ID3D11RenderTargetView* const* ppRenderTargetViews,ID3D11DepthStencilView* pDepthStencilView) override;
    void STDMETHODCALLTYPE OMSetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs,ID3D11RenderTargetView* const* ppRenderTargetViews,ID3D11DepthStencilView* pDepthStencilView,UINT UAVStartSlot,UINT NumUAVs,ID3D11UnorderedAccessView* const* ppUnorderedAccessViews,const UINT* pUAVInitialCounts) override;
    void STDMETHODCALLTYPE OMSetBlendState(ID3D11BlendState* pBlendState,const FLOAT BlendFactor[4],UINT SampleMask) override;
    void STDMETHODCALLTYPE OMSetDepthStencilState(ID3D11DepthStencilState* pDepthStencilState,UINT StencilRef) override;
    void STDMETHODCALLTYPE SOSetTargets(UINT NumBuffers,ID3D11Buffer* const* ppSOTargets,const UINT* pOffsets) override;
    void STDMETHODCALLTYPE DrawAuto() override;
    void STDMETHODCALLTYPE DrawIndexedInstancedIndirect(ID3D11Buffer* pBufferForArgs,UINT AlignedByteOffsetForArgs) override;
    void STDMETHODCALLTYPE DrawInstancedIndirect(ID3D11Buffer* pBufferForArgs,UINT AlignedByteOffsetForArgs) override;
    void STDMETHODCALLTYPE Dispatch(UINT ThreadGroupCountX,UINT ThreadGroupCountY,UINT ThreadGroupCountZ) override;
    void STDMETHODCALLTYPE DispatchIndirect(ID3D11Buffer* pBufferForArgs,UINT AlignedByteOffsetForArgs) override;
    void STDMETHODCALLTYPE RSSetState(ID3D11RasterizerState* pRasterizerState) override;
    void STDMETHODCALLTYPE RSSetViewports(UINT NumViewports,const D3D11_VIEWPORT* pViewports) override;
    void STDMETHODCALLTYPE RSSetScissorRects(UINT NumRects,const D3D11_RECT* pRects) override;
    void STDMETHODCALLTYPE CopySubresourceRegion(ID3D11Resource* pDstResource,UINT DstSubresource,UINT DstX,UINT DstY,UINT DstZ,ID3D11Resource* pSrcResource,UINT SrcSubresource,const D3D11_BOX* pSrcBox) override;
    void STDMETHODCALLTYPE CopyResource(ID3D11Resource* pDstResource,ID3D11Resource* pSrcResource) override;
    void STDMETHODCALLTYPE UpdateSubresource(ID3D11Resource* pDstResource,UINT DstSubresource,const D3D11_BOX* pDstBox,const void* pSrcData,UINT SrcRowPitch,UINT SrcDepthPitch) override;
    void STDMETHODCALLTYPE CopyStructureCount(ID3D11Buffer* pDstBuffer,UINT DstAlignedByteOffset,ID3D11UnorderedAccessView* pSrcView) override;
    void STDMETHODCALLTYPE ClearRenderTargetView(ID3D11RenderTargetView* pRenderTargetView,const FLOAT ColorRGBA[4]) override;
    void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView* pUnorderedAccessView,const UINT Values[4]) override;
    void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView* pUnorderedAccessView,const FLOAT Values[4]) override;
    void STDMETHODCALLTYPE ClearDepthStencilView(ID3D11DepthStencilView* pDepthStencilView,UINT ClearFlags,FLOAT Depth,UINT8 Stencil) override;
    void STDMETHODCALLTYPE GenerateMips(ID3D11ShaderResourceView* pShaderResourceView) override;
    void STDMETHODCALLTYPE SetResourceMinLOD(ID3D11Resource* pResource,FLOAT MinLOD) override;
    FLOAT STDMETHODCALLTYPE GetResourceMinLOD(ID3D11Resource* pResource) override;
    void STDMETHODCALLTYPE ResolveSubresource(ID3D11Resource* pDstResource,UINT DstSubresource,ID3D11Resource* pSrcResource,UINT SrcSubresource,DXGI_FORMAT Format) override;
    void STDMETHODCALLTYPE ExecuteCommandList(ID3D11CommandList* pCommandList,BOOL RestoreContextState) override;
    void STDMETHODCALLTYPE HSSetShaderResources(UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView* const* ppShaderResourceViews) override;
    void STDMETHODCALLTYPE HSSetShader(ID3D11HullShader* pHullShader,ID3D11ClassInstance* const* ppClassInstances,UINT NumClassInstances) override;
    void STDMETHODCALLTYPE HSSetSamplers(UINT StartSlot,UINT NumSamplers,ID3D11SamplerState* const* ppSamplers) override;
    void STDMETHODCALLTYPE HSSetConstantBuffers(UINT StartSlot,UINT NumBuffers,ID3D11Buffer* const* ppConstantBuffers) override;
    void STDMETHODCALLTYPE DSSetShaderResources(UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView* const* ppShaderResourceViews) override;
    void STDMETHODCALLTYPE DSSetShader(ID3D11DomainShader* pDomainShader,ID3D11ClassInstance* const* ppClassInstances,UINT NumClassInstances) override;
    void STDMETHODCALLTYPE DSSetSamplers(UINT StartSlot,UINT NumSamplers,ID3D11SamplerState* const* ppSamplers) override;
    void STDMETHODCALLTYPE DSSetConstantBuffers(UINT StartSlot,UINT NumBuffers,ID3D11Buffer* const* ppConstantBuffers) override;
    void STDMETHODCALLTYPE CSSetShaderResources(UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView* const* ppShaderResourceViews) override;
    void STDMETHODCALLTYPE CSSetUnorderedAccessViews(UINT StartSlot,UINT NumUAVs,ID3D11UnorderedAccessView* const* ppUnorderedAccessViews,const UINT* pUAVInitialCounts) override;
    void STDMETHODCALLTYPE CSSetShader(ID3D11ComputeShader* pComputeShader,ID3D11ClassInstance* const* ppClassInstances,UINT NumClassInstances) override;
    void STDMETHODCALLTYPE CSSetSamplers(UINT StartSlot,UINT NumSamplers,ID3D11SamplerState* const* ppSamplers) override;
    void STDMETHODCALLTYPE CSSetConstantBuffers(UINT StartSlot,UINT NumBuffers,ID3D11Buffer* const* ppConstantBuffers) override;
    void STDMETHODCALLTYPE VSGetConstantBuffers(UINT StartSlot,UINT NumBuffers,ID3D11Buffer** ppConstantBuffers) override;
    void STDMETHODCALLTYPE PSGetShaderResources(UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView** ppShaderResourceViews) override;
    void STDMETHODCALLTYPE PSGetShader(ID3D11PixelShader** ppPixelShader,ID3D11ClassInstance** ppClassInstances,UINT* pNumClassInstances) override;
    void STDMETHODCALLTYPE PSGetSamplers(UINT StartSlot,UINT NumSamplers,ID3D11SamplerState** ppSamplers) override;
    void STDMETHODCALLTYPE VSGetShader(ID3D11VertexShader** ppVertexShader,ID3D11ClassInstance** ppClassInstances,UINT* pNumClassInstances) override;
    void STDMETHODCALLTYPE PSGetConstantBuffers(UINT StartSlot,UINT NumBuffers,ID3D11Buffer** ppConstantBuffers) override;
    void STDMETHODCALLTYPE IAGetInputLayout(ID3D11InputLayout** ppInputLayout) override;
    void STDMETHODCALLTYPE IAGetVertexBuffers(UINT StartSlot,UINT NumBuffers,ID3D11Buffer** ppVertexBuffers,UINT* pStrides,UINT* pOffsets) override;
    void STDMETHODCALLTYPE IAGetIndexBuffer(ID3D11Buffer** pIndexBuffer,DXGI_FORMAT* Format,UINT* Offset) override;
    void STDMETHODCALLTYPE GSGetConstantBuffers(UINT StartSlot,UINT NumBuffers,ID3D11Buffer** ppConstantBuffers) override;
    void STDMETHODCALLTYPE GSGetShader(ID3D11GeometryShader** ppGeometryShader,ID3D11ClassInstance** ppClassInstances,UINT* pNumClassInstances) override;
    void STDMETHODCALLTYPE IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY* pTopology) override;
    void STDMETHODCALLTYPE VSGetShaderResources(UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView** ppShaderResourceViews) override;
    void STDMETHODCALLTYPE VSGetSamplers(UINT StartSlot,UINT NumSamplers,ID3D11SamplerState** ppSamplers) override;
    void STDMETHODCALLTYPE GetPredication(ID3D11Predicate** ppPredicate,BOOL* pPredicateValue) override;
    void STDMETHODCALLTYPE GSGetShaderResources(UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView** ppShaderResourceViews) override;
    void STDMETHODCALLTYPE GSGetSamplers(UINT StartSlot,UINT NumSamplers,ID3D11SamplerState** ppSamplers) override;
    void STDMETHODCALLTYPE OMGetRenderTargets(UINT NumViews,ID3D11RenderTargetView** ppRenderTargetViews,ID3D11DepthStencilView** ppDepthStencilView) override;
    void STDMETHODCALLTYPE OMGetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs,ID3D11RenderTargetView** ppRenderTargetViews,ID3D11DepthStencilView** ppDepthStencilView,UINT UAVStartSlot,UINT NumUAVs,ID3D11UnorderedAccessView** ppUnorderedAccessViews) override;
    void STDMETHODCALLTYPE OMGetBlendState(ID3D11BlendState** ppBlendState,FLOAT BlendFactor[4],UINT* pSampleMask) override;
    void STDMETHODCALLTYPE OMGetDepthStencilState(ID3D11DepthStencilState** ppDepthStencilState,UINT* pStencilRef) override;
    void STDMETHODCALLTYPE SOGetTargets(UINT NumBuffers,ID3D11Buffer** ppSOTargets) override;
    void STDMETHODCALLTYPE RSGetState(ID3D11RasterizerState** ppRasterizerState) override;
    void STDMETHODCALLTYPE RSGetViewports(UINT* pNumViewports,D3D11_VIEWPORT* pViewports) override;
    void STDMETHODCALLTYPE RSGetScissorRects(UINT* pNumRects,D3D11_RECT* pRects) override;
    void STDMETHODCALLTYPE HSGetShaderResources(UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView** ppShaderResourceViews) override;
    void STDMETHODCALLTYPE HSGetShader(ID3D11HullShader** ppHullShader,ID3D11ClassInstance** ppClassInstances,UINT* pNumClassInstances) override;
    void STDMETHODCALLTYPE HSGetSamplers(UINT StartSlot,UINT NumSamplers,ID3D11SamplerState** ppSamplers) override;
    void STDMETHODCALLTYPE HSGetConstantBuffers(UINT StartSlot,UINT NumBuffers,ID3D11Buffer** ppConstantBuffers) override;
    void STDMETHODCALLTYPE DSGetShaderResources(UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView** ppShaderResourceViews) override;
    void STDMETHODCALLTYPE DSGetShader(ID3D11DomainShader** ppDomainShader,ID3D11ClassInstance** ppClassInstances,UINT* pNumClassInstances) override;
    void STDMETHODCALLTYPE DSGetSamplers(UINT StartSlot,UINT NumSamplers,ID3D11SamplerState** ppSamplers) override;
    void STDMETHODCALLTYPE DSGetConstantBuffers(UINT StartSlot,UINT NumBuffers,ID3D11Buffer** ppConstantBuffers) override;
    void STDMETHODCALLTYPE CSGetShaderResources(UINT StartSlot,UINT NumViews,ID3D11ShaderResourceView** ppShaderResourceViews) override;
    void STDMETHODCALLTYPE CSGetUnorderedAccessViews(UINT StartSlot,UINT NumUAVs,ID3D11UnorderedAccessView** ppUnorderedAccessViews) override;
    void STDMETHODCALLTYPE CSGetShader(ID3D11ComputeShader** ppComputeShader,ID3D11ClassInstance** ppClassInstances,UINT* pNumClassInstances) override;
    void STDMETHODCALLTYPE CSGetSamplers(UINT StartSlot,UINT NumSamplers,ID3D11SamplerState** ppSamplers) override;
    void STDMETHODCALLTYPE CSGetConstantBuffers(UINT StartSlot,UINT NumBuffers,ID3D11Buffer** ppConstantBuffers) override;
    void STDMETHODCALLTYPE ClearState() override;
    void STDMETHODCALLTYPE Flush() override;
    D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE GetType() override {return deferred_?D3D11_DEVICE_CONTEXT_DEFERRED:D3D11_DEVICE_CONTEXT_IMMEDIATE;}
    UINT STDMETHODCALLTYPE GetContextFlags() override {return 0;}
    HRESULT STDMETHODCALLTYPE FinishCommandList(BOOL RestoreDeferredContextState,ID3D11CommandList** ppCommandList) override;
    // ID3D11DeviceContext1
    void STDMETHODCALLTYPE CopySubresourceRegion1(ID3D11Resource* pDstResource,UINT DstSubresource,UINT DstX,UINT DstY,UINT DstZ,ID3D11Resource* pSrcResource,UINT SrcSubresource,const D3D11_BOX* pSrcBox,UINT CopyFlags) override;
    void STDMETHODCALLTYPE UpdateSubresource1(ID3D11Resource* pDstResource,UINT DstSubresource,const D3D11_BOX* pDstBox,const void* pSrcData,UINT SrcRowPitch,UINT SrcDepthPitch,UINT CopyFlags) override;
    void STDMETHODCALLTYPE DiscardResource(ID3D11Resource* pResource) override;
    void STDMETHODCALLTYPE DiscardView(ID3D11View* pResourceView) override;
    void STDMETHODCALLTYPE VSSetConstantBuffers1(UINT StartSlot,UINT NumBuffers,ID3D11Buffer* const* ppConstantBuffers,const UINT* pFirstConstant,const UINT* pNumConstants) override;
    void STDMETHODCALLTYPE HSSetConstantBuffers1(UINT StartSlot,UINT NumBuffers,ID3D11Buffer* const* ppConstantBuffers,const UINT* pFirstConstant,const UINT* pNumConstants) override;
    void STDMETHODCALLTYPE DSSetConstantBuffers1(UINT StartSlot,UINT NumBuffers,ID3D11Buffer* const* ppConstantBuffers,const UINT* pFirstConstant,const UINT* pNumConstants) override;
    void STDMETHODCALLTYPE GSSetConstantBuffers1(UINT StartSlot,UINT NumBuffers,ID3D11Buffer* const* ppConstantBuffers,const UINT* pFirstConstant,const UINT* pNumConstants) override;
    void STDMETHODCALLTYPE PSSetConstantBuffers1(UINT StartSlot,UINT NumBuffers,ID3D11Buffer* const* ppConstantBuffers,const UINT* pFirstConstant,const UINT* pNumConstants) override;
    void STDMETHODCALLTYPE CSSetConstantBuffers1(UINT StartSlot,UINT NumBuffers,ID3D11Buffer* const* ppConstantBuffers,const UINT* pFirstConstant,const UINT* pNumConstants) override;
    void STDMETHODCALLTYPE VSGetConstantBuffers1(UINT StartSlot,UINT NumBuffers,ID3D11Buffer** ppConstantBuffers,UINT* pFirstConstant,UINT* pNumConstants) override;
    void STDMETHODCALLTYPE HSGetConstantBuffers1(UINT StartSlot,UINT NumBuffers,ID3D11Buffer** ppConstantBuffers,UINT* pFirstConstant,UINT* pNumConstants) override;
    void STDMETHODCALLTYPE DSGetConstantBuffers1(UINT StartSlot,UINT NumBuffers,ID3D11Buffer** ppConstantBuffers,UINT* pFirstConstant,UINT* pNumConstants) override;
    void STDMETHODCALLTYPE GSGetConstantBuffers1(UINT StartSlot,UINT NumBuffers,ID3D11Buffer** ppConstantBuffers,UINT* pFirstConstant,UINT* pNumConstants) override;
    void STDMETHODCALLTYPE PSGetConstantBuffers1(UINT StartSlot,UINT NumBuffers,ID3D11Buffer** ppConstantBuffers,UINT* pFirstConstant,UINT* pNumConstants) override;
    void STDMETHODCALLTYPE CSGetConstantBuffers1(UINT StartSlot,UINT NumBuffers,ID3D11Buffer** ppConstantBuffers,UINT* pFirstConstant,UINT* pNumConstants) override;
    void STDMETHODCALLTYPE SwapDeviceContextState(ID3DDeviceContextState* pState,ID3DDeviceContextState** ppPreviousState) override;
    void STDMETHODCALLTYPE ClearView(ID3D11View* pView,const FLOAT Color[4],const D3D11_RECT* pRect,UINT NumRects) override;
    void STDMETHODCALLTYPE DiscardView1(ID3D11View* pResourceView,const D3D11_RECT* pRects,UINT NumRects) override;

    // Owner-thread services for presentation and teardown.
    void endRendering();
    void prepareSubmit();          // Ends rendering before the batch is submitted.
    void afterSubmit();            // New batch: every Vulkan binding must be re-emitted.
    void releaseState();           // Device teardown: drop every binding reference.
    bool deferred() const {return deferred_;}
    // Owner-thread recording counts. SIMPSONS_VULKAN_STATS=1 makes Present log
    // their per-frame averages every 600 frames (then they restart).
    struct Stats {
        uint64_t frames=0,draws=0,scopes=0,attachmentBreaks=0,clears=0,transfers=0,barriers=0,pipelineBinds=0,
            pushes=0,pushSkips=0,vertexBinds=0,submits=0,lists=0,listCommands=0;
    } stats;
    void frameStats();
private:
    friend DeviceChild; // Calls supports() non-virtually.
    bool supports(REFIID riid) const;
    void record(RecordedCommand command) {recorded_.push_back(std::move(command));}
    void setConstantBuffers(StageBindings& stage,uint32_t dirty,UINT start,UINT count,ID3D11Buffer* const* buffers,const UINT* first,const UINT* number);
    void setResources(StageBindings& stage,uint32_t dirty,UINT start,UINT count,ID3D11ShaderResourceView* const* views);
    void setSamplers(StageBindings& stage,uint32_t dirty,UINT start,UINT count,ID3D11SamplerState* const* samplers);
    void getConstantBuffers(const StageBindings& stage,UINT start,UINT count,ID3D11Buffer** buffers,UINT* first,UINT* number) const;
    void getResources(const StageBindings& stage,UINT start,UINT count,ID3D11ShaderResourceView** views) const;
    void getSamplers(const StageBindings& stage,UINT start,UINT count,ID3D11SamplerState** samplers) const;
    bool boundAsOutput(const D3D11ShaderResourceView& view) const;
    void unbindResourcesOf(ID3D11Resource* resource);
    void setUavs(std::array<ComPtr<D3D11UnorderedAccessView>,D3D11_1_UAV_SLOT_COUNT>& slots,UINT start,UINT count,
        ID3D11UnorderedAccessView* const* views,const UINT* counts);
    void unbindConflictingResources();
    void resetState();
    void markAllDirty();
    // Vulkan recording (immediate context only). Calls run on the device's
    // recording thread; every argument is resolved here.
    VulkanDevice& vulkan();
    template<class F> void emit(F&& call) {vulkan().record(std::forward<F>(call));}
    template<class T,class F> void emit(std::span<const T> data,F&& call) {vulkan().record(data,std::forward<F>(call));}
    void barrier();
    void beginRendering();
    bool prepareDraw(bool indexed,INT vertexBase,UINT instanceBase);
    void pushDescriptors(VkPipelineLayout layout);
    void applyDynamicState();
    void bindVertexBuffers(INT vertexBase,UINT instanceBase);
    void clearAttachment(VkImageView view,VkExtent2D extent,VkFormat format,VkImageAspectFlags aspects,
        const VkClearValue& value,VkImageAspectFlags clearAspects);
    void copyTexture(D3D11Texture2D& destination,UINT destinationSub,UINT x,UINT y,D3D11Texture2D& source,UINT sourceSub,const D3D11_BOX* box);
    void copyBuffer(D3D11Buffer& destination,UINT offset,D3D11Buffer& source,UINT sourceOffset,UINT size);
    void updateBuffer(D3D11Buffer& buffer,UINT offset,UINT size,const void* data);
    void updateTexture(D3D11Texture2D& texture,UINT subresource,const D3D11_BOX* box,const void* data,UINT rowPitch);
    HRESULT waitForStaging(uint64_t serial,UINT flags);
    void writeTimestamp(D3D11Query& query);
    void beginSegment(D3D11Query& query);
    void endSegment(D3D11Query& query);
    // Conditional rendering is not mapped: predicated work fails explicitly.
    void requireUnpredicated(const char* operation) const;
    std::vector<ComPtr<D3D11Query>> activeQueries_;
    const bool deferred_;
    // Live state. Command-list save/restore and context-state swaps exchange
    // these pointers; spareState_ is a cleared state kept for reuse.
    std::unique_ptr<ContextState> state_=std::make_unique<ContextState>(),spareState_;
    ComPtr<D3D11DeviceContextState> activeState_;
    std::vector<RecordedCommand> recorded_;
    // Deferred Map(WRITE_DISCARD) staging until Unmap records the upload.
    std::unordered_map<ID3D11Resource*,std::vector<uint8_t>> deferredMaps_;
    // Immediate Vulkan state.
    uint32_t dirty_=~0u;
    bool rendering_=false,barrierPending_=false;
    std::array<VkImageView,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> renderingColors_{};
    VkImageView renderingDepth_=VK_NULL_HANDLE;
    VkExtent2D renderArea_{};
    std::array<VkFormat,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> colorFormats_{};
    VkFormat depthFormat_=VK_FORMAT_UNDEFINED;
    VkPipeline pipeline_=VK_NULL_HANDLE;
    VkPipelineLayout layout_=VK_NULL_HANDLE;
    PipelineKey key_{};
    std::array<VkBuffer,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> boundVertexBuffers_{};
    std::array<VkDeviceSize,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> boundVertexOffsets_{},boundVertexStrides_{};
    uint32_t vertexBindingMask_=0;
    VkBuffer boundIndexBuffer_=VK_NULL_HANDLE;VkDeviceSize boundIndexOffset_=0;VkIndexType boundIndexType_=VK_INDEX_TYPE_UINT16;
    INT boundVertexBase_=0;UINT boundInstanceBase_=0;INT drawVertexOffset_=0;
    // Dynamic state last recorded into the current command buffer. Every
    // pipeline declares the same dynamic states, so it persists across
    // pipeline binds and rendering scopes; a new command buffer clears it.
    struct EmittedState {
        std::optional<VkViewport> viewport;
        std::optional<VkRect2D> scissor;
        std::optional<std::array<uint32_t,3>> raster;      // cull, front face, bias enable
        std::optional<std::array<float,3>> bias;
        std::optional<std::array<uint32_t,4>> depth;       // test, write, compare, bounds test
        std::optional<std::array<uint32_t,11>> stencil;    // test, front/back ops, read/write masks
        std::optional<uint32_t> stencilRef,topology,restart;
        std::optional<std::array<float,4>> blendConstants;
    } emitted_;
    // Descriptors last pushed into the current command buffer for pushedLayout_.
    // Handles stay unique while recording (destruction waits for completion).
    VkPipelineLayout pushedLayout_=VK_NULL_HANDLE;
    std::vector<uint64_t> pushed_,pushing_;
    friend class D3D11Device;
    friend class DxgiSwapChain;
    friend class PipelineCache;
};

class DxgiDevice;
class __declspec(uuid("a5409dfe-5e26-5d85-98e0-9372711c421f")) D3D11Device final : public ID3D11Device4 {
public:
    D3D11Device();
    ~D3D11Device();
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE CreateBuffer(const D3D11_BUFFER_DESC* pDesc,const D3D11_SUBRESOURCE_DATA* pInitialData,ID3D11Buffer** ppBuffer) override;
    HRESULT STDMETHODCALLTYPE CreateTexture1D(const D3D11_TEXTURE1D_DESC* pDesc,const D3D11_SUBRESOURCE_DATA* pInitialData,ID3D11Texture1D** ppTexture1D) override;
    HRESULT STDMETHODCALLTYPE CreateTexture2D(const D3D11_TEXTURE2D_DESC* pDesc,const D3D11_SUBRESOURCE_DATA* pInitialData,ID3D11Texture2D** ppTexture2D) override;
    HRESULT STDMETHODCALLTYPE CreateTexture3D(const D3D11_TEXTURE3D_DESC* pDesc,const D3D11_SUBRESOURCE_DATA* pInitialData,ID3D11Texture3D** ppTexture3D) override;
    HRESULT STDMETHODCALLTYPE CreateShaderResourceView(ID3D11Resource* pResource,const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc,ID3D11ShaderResourceView** ppSRView) override;
    HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D11Resource* pResource,const D3D11_UNORDERED_ACCESS_VIEW_DESC* pDesc,ID3D11UnorderedAccessView** ppUAView) override;
    HRESULT STDMETHODCALLTYPE CreateRenderTargetView(ID3D11Resource* pResource,const D3D11_RENDER_TARGET_VIEW_DESC* pDesc,ID3D11RenderTargetView** ppRTView) override;
    HRESULT STDMETHODCALLTYPE CreateDepthStencilView(ID3D11Resource* pResource,const D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc,ID3D11DepthStencilView** ppDepthStencilView) override;
    HRESULT STDMETHODCALLTYPE CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* pInputElementDescs,UINT NumElements,const void* pShaderBytecodeWithInputSignature,SIZE_T BytecodeLength,ID3D11InputLayout** ppInputLayout) override;
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const void* pShaderBytecode,SIZE_T BytecodeLength,ID3D11ClassLinkage* pClassLinkage,ID3D11VertexShader** ppVertexShader) override;
    HRESULT STDMETHODCALLTYPE CreateGeometryShader(const void* pShaderBytecode,SIZE_T BytecodeLength,ID3D11ClassLinkage* pClassLinkage,ID3D11GeometryShader** ppGeometryShader) override;
    HRESULT STDMETHODCALLTYPE CreateGeometryShaderWithStreamOutput(const void* pShaderBytecode,SIZE_T BytecodeLength,const D3D11_SO_DECLARATION_ENTRY* pSODeclaration,UINT NumEntries,const UINT* pBufferStrides,UINT NumStrides,UINT RasterizedStream,ID3D11ClassLinkage* pClassLinkage,ID3D11GeometryShader** ppGeometryShader) override;
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const void* pShaderBytecode,SIZE_T BytecodeLength,ID3D11ClassLinkage* pClassLinkage,ID3D11PixelShader** ppPixelShader) override;
    HRESULT STDMETHODCALLTYPE CreateHullShader(const void* pShaderBytecode,SIZE_T BytecodeLength,ID3D11ClassLinkage* pClassLinkage,ID3D11HullShader** ppHullShader) override;
    HRESULT STDMETHODCALLTYPE CreateDomainShader(const void* pShaderBytecode,SIZE_T BytecodeLength,ID3D11ClassLinkage* pClassLinkage,ID3D11DomainShader** ppDomainShader) override;
    HRESULT STDMETHODCALLTYPE CreateComputeShader(const void* pShaderBytecode,SIZE_T BytecodeLength,ID3D11ClassLinkage* pClassLinkage,ID3D11ComputeShader** ppComputeShader) override;
    HRESULT STDMETHODCALLTYPE CreateClassLinkage(ID3D11ClassLinkage** ppLinkage) override;
    HRESULT STDMETHODCALLTYPE CreateBlendState(const D3D11_BLEND_DESC* pBlendStateDesc,ID3D11BlendState** ppBlendState) override;
    HRESULT STDMETHODCALLTYPE CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC* pDepthStencilDesc,ID3D11DepthStencilState** ppDepthStencilState) override;
    HRESULT STDMETHODCALLTYPE CreateRasterizerState(const D3D11_RASTERIZER_DESC* pRasterizerDesc,ID3D11RasterizerState** ppRasterizerState) override;
    HRESULT STDMETHODCALLTYPE CreateSamplerState(const D3D11_SAMPLER_DESC* pSamplerDesc,ID3D11SamplerState** ppSamplerState) override;
    HRESULT STDMETHODCALLTYPE CreateQuery(const D3D11_QUERY_DESC* pQueryDesc,ID3D11Query** ppQuery) override;
    HRESULT STDMETHODCALLTYPE CreatePredicate(const D3D11_QUERY_DESC* pPredicateDesc,ID3D11Predicate** ppPredicate) override;
    HRESULT STDMETHODCALLTYPE CreateCounter(const D3D11_COUNTER_DESC* pCounterDesc,ID3D11Counter** ppCounter) override;
    HRESULT STDMETHODCALLTYPE CreateDeferredContext(UINT ContextFlags,ID3D11DeviceContext** ppDeferredContext) override;
    HRESULT STDMETHODCALLTYPE OpenSharedResource(HANDLE hResource,REFIID ReturnedInterface,void** ppResource) override;
    HRESULT STDMETHODCALLTYPE CheckFormatSupport(DXGI_FORMAT Format,UINT* pFormatSupport) override;
    HRESULT STDMETHODCALLTYPE CheckMultisampleQualityLevels(DXGI_FORMAT Format,UINT SampleCount,UINT* pNumQualityLevels) override;
    void STDMETHODCALLTYPE CheckCounterInfo(D3D11_COUNTER_INFO* pCounterInfo) override;
    HRESULT STDMETHODCALLTYPE CheckCounter(const D3D11_COUNTER_DESC* pDesc,D3D11_COUNTER_TYPE* pType,UINT* pActiveCounters,LPSTR szName,UINT* pNameLength,LPSTR szUnits,UINT* pUnitsLength,LPSTR szDescription,UINT* pDescriptionLength) override;
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D11_FEATURE Feature,void* pFeatureSupportData,UINT FeatureSupportDataSize) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid,UINT* pDataSize,void* pData) override {return privateData_.get(guid,pDataSize,pData);}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid,UINT DataSize,const void* pData) override {return privateData_.set(guid,DataSize,pData);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid,const IUnknown* pData) override {return privateData_.setInterface(guid,pData);}
    D3D_FEATURE_LEVEL STDMETHODCALLTYPE GetFeatureLevel() override {return D3D_FEATURE_LEVEL_11_1;}
    UINT STDMETHODCALLTYPE GetCreationFlags() override {return D3D11_CREATE_DEVICE_BGRA_SUPPORT;}
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override {return vulkan_.lost()?DXGI_ERROR_DEVICE_REMOVED:S_OK;}
    void STDMETHODCALLTYPE GetImmediateContext(ID3D11DeviceContext** ppImmediateContext) override;
    HRESULT STDMETHODCALLTYPE SetExceptionMode(UINT RaiseFlags) override {exceptionMode_=RaiseFlags;return S_OK;}
    UINT STDMETHODCALLTYPE GetExceptionMode() override {return exceptionMode_;}
    void STDMETHODCALLTYPE GetImmediateContext1(ID3D11DeviceContext1** ppImmediateContext) override;
    HRESULT STDMETHODCALLTYPE CreateDeferredContext1(UINT ContextFlags,ID3D11DeviceContext1** ppDeferredContext) override;
    HRESULT STDMETHODCALLTYPE CreateBlendState1(const D3D11_BLEND_DESC1* pBlendStateDesc,ID3D11BlendState1** ppBlendState) override;
    HRESULT STDMETHODCALLTYPE CreateRasterizerState1(const D3D11_RASTERIZER_DESC1* pRasterizerDesc,ID3D11RasterizerState1** ppRasterizerState) override;
    HRESULT STDMETHODCALLTYPE CreateDeviceContextState(UINT Flags,const D3D_FEATURE_LEVEL* pFeatureLevels,UINT FeatureLevels,UINT SDKVersion,REFIID EmulatedInterface,D3D_FEATURE_LEVEL* pChosenFeatureLevel,ID3DDeviceContextState** ppContextState) override;
    HRESULT STDMETHODCALLTYPE OpenSharedResource1(HANDLE hResource,REFIID returnedInterface,void** ppResource) override;
    HRESULT STDMETHODCALLTYPE OpenSharedResourceByName(LPCWSTR lpName,DWORD dwDesiredAccess,REFIID returnedInterface,void** ppResource) override;
    // ID3D11Device2..4: device-removed notification is implemented (the
    // renderer's availability guard uses it); the rest reports unsupported.
    void STDMETHODCALLTYPE GetImmediateContext2(ID3D11DeviceContext2** context) override {if(context) *context=nullptr;}
    HRESULT STDMETHODCALLTYPE CreateDeferredContext2(UINT,ID3D11DeviceContext2** context) override {if(context) *context=nullptr;return E_NOTIMPL;}
    void STDMETHODCALLTYPE GetResourceTiling(ID3D11Resource*,UINT* tiles,D3D11_PACKED_MIP_DESC*,D3D11_TILE_SHAPE*,UINT* count,UINT,D3D11_SUBRESOURCE_TILING*) override {if(tiles) *tiles=0;if(count) *count=0;}
    HRESULT STDMETHODCALLTYPE CheckMultisampleQualityLevels1(DXGI_FORMAT Format,UINT SampleCount,UINT Flags,UINT* pNumQualityLevels) override {
        if(Flags) {if(pNumQualityLevels) *pNumQualityLevels=0;return E_NOTIMPL;}
        return CheckMultisampleQualityLevels(Format,SampleCount,pNumQualityLevels);
    }
    HRESULT STDMETHODCALLTYPE CreateTexture2D1(const D3D11_TEXTURE2D_DESC1*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture2D1**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateTexture3D1(const D3D11_TEXTURE3D_DESC1*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture3D1**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateRasterizerState2(const D3D11_RASTERIZER_DESC2*,ID3D11RasterizerState2**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateShaderResourceView1(ID3D11Resource*,const D3D11_SHADER_RESOURCE_VIEW_DESC1*,ID3D11ShaderResourceView1**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView1(ID3D11Resource*,const D3D11_UNORDERED_ACCESS_VIEW_DESC1*,ID3D11UnorderedAccessView1**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateRenderTargetView1(ID3D11Resource*,const D3D11_RENDER_TARGET_VIEW_DESC1*,ID3D11RenderTargetView1**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateQuery1(const D3D11_QUERY_DESC1*,ID3D11Query1**) override {return E_NOTIMPL;}
    void STDMETHODCALLTYPE GetImmediateContext3(ID3D11DeviceContext3** context) override {if(context) *context=nullptr;}
    HRESULT STDMETHODCALLTYPE CreateDeferredContext3(UINT,ID3D11DeviceContext3** context) override {if(context) *context=nullptr;return E_NOTIMPL;}
    void STDMETHODCALLTYPE WriteToSubresource(ID3D11Resource*,UINT,const D3D11_BOX*,const void*,UINT,UINT) override {reportUnsupported("WriteToSubresource");}
    void STDMETHODCALLTYPE ReadFromSubresource(void*,UINT,UINT,ID3D11Resource*,UINT,const D3D11_BOX*) override {reportUnsupported("ReadFromSubresource");}
    HRESULT STDMETHODCALLTYPE RegisterDeviceRemovedEvent(HANDLE hEvent,DWORD* pdwCookie) override;
    void STDMETHODCALLTYPE UnregisterDeviceRemoved(DWORD dwCookie) override;

    VulkanDevice& vulkan() {return vulkan_;}
    D3D11Context& immediate() {return *immediate_;}
    PipelineCache& pipelines() {return *pipelines_;}
    DxgiDevice& dxgi() {return *dxgi_;}
    void addChildReference() {children_.fetch_add(1,std::memory_order_relaxed);}
    void releaseChildReference();
    // Query pool slots (owner thread), host-reset before reuse.
    VkQueryPool queryPool(QueryPool kind) const {return querySlots_[size_t(kind)].pool;}
    uint32_t acquireQuerySlot(QueryPool kind);
    void releaseQuerySlot(QueryPool kind,uint32_t slot);
    uint32_t statisticsCount() const {return statisticsCount_;}
    VkQueryPipelineStatisticFlags statisticsFlags() const {return statisticsFlags_;}
    VkSampler defaultSampler() const {return defaultSampler_;}
    uint32_t maximumFrameLatency() const {return frameLatency_.load(std::memory_order_relaxed);}
    void setMaximumFrameLatency(uint32_t frames) {frameLatency_.store(frames,std::memory_order_relaxed);}
    // State object de-duplication (D3D11 returns the same object for an
    // identical descriptor). Destructors unregister.
    void forgetBlend(const D3D11BlendState* state);
    void forgetDepthStencil(const D3D11DepthStencilState* state);
    void forgetRasterizer(const D3D11RasterizerState* state);
    void forgetSampler(const D3D11SamplerState* state);
    DxgiSwapChain* swapChain=nullptr; // Single attached chain (owner thread).
private:
    void destroy();
    template<class T,class Desc> HRESULT dedupState(std::vector<std::pair<Desc,T*>>& cache,const Desc& desc,T** out);
    std::atomic<ULONG> refs_{1},children_{0};
    bool shutDown_=false;
    VulkanDevice vulkan_;
    std::unique_ptr<D3D11Context> immediate_;
    std::unique_ptr<PipelineCache> pipelines_;
    std::unique_ptr<DxgiDevice> dxgi_;
    PrivateData privateData_;
    UINT exceptionMode_=0;
    std::atomic<uint32_t> frameLatency_{3};
    VkSampler defaultSampler_=VK_NULL_HANDLE;
    struct QuerySlots {VkQueryPool pool=VK_NULL_HANDLE;std::vector<uint32_t> free;std::deque<std::pair<uint64_t,uint32_t>> retiring;};
    std::array<QuerySlots,3> querySlots_;
    std::mutex queryMutex_;
    VkQueryPipelineStatisticFlags statisticsFlags_=0;
    uint32_t statisticsCount_=0;
    std::mutex stateMutex_;
    std::vector<std::pair<D3D11_BLEND_DESC,D3D11BlendState*>> blendStates_;
    std::vector<std::pair<D3D11_DEPTH_STENCIL_DESC,D3D11DepthStencilState*>> depthStates_;
    std::vector<std::pair<D3D11_RASTERIZER_DESC,D3D11RasterizerState*>> rasterStates_;
    std::vector<std::pair<D3D11_SAMPLER_DESC,D3D11SamplerState*>> samplerStates_;
    std::unordered_map<std::string,uint64_t> layoutContentIds_; // Guarded by stateMutex_.
};


// ---- DeviceChild implementation
// The COM methods reach the derived class without virtual calls beyond the COM
// interface: hooks (and test observers) may clone a vtable to its SDK slot count.
template<class Base,class Derived>
DeviceChild<Base,Derived>::DeviceChild(D3D11Device* device,bool holdDevice):device_(device),holdsDevice_(holdDevice),id_(nextObjectId()) {
    if(holdsDevice_) device_->addChildReference();
}
template<class Base,class Derived>
DeviceChild<Base,Derived>::~DeviceChild() {
    if(holdsDevice_) device_->releaseChildReference();
}
template<class Base,class Derived>
HRESULT DeviceChild<Base,Derived>::QueryInterface(REFIID riid,void** object) {
    if(!object) return E_POINTER;
    *object=nullptr;
    if(riid==__uuidof(IUnknown) || riid==__uuidof(ID3D11DeviceChild) || static_cast<const Derived*>(this)->Derived::supports(riid)) {
        *object=static_cast<Base*>(this);AddRef();return S_OK;
    }
    return E_NOINTERFACE;
}
template<class Base,class Derived>
ULONG DeviceChild<Base,Derived>::AddRef() {return refs_.fetch_add(1,std::memory_order_relaxed)+1;}
template<class Base,class Derived>
ULONG DeviceChild<Base,Derived>::Release() {
    const ULONG left=refs_.fetch_sub(1,std::memory_order_acq_rel)-1;
    if(!left) delete static_cast<Derived*>(this);
    return left;
}
template<class Base,class Derived>
void DeviceChild<Base,Derived>::GetDevice(ID3D11Device** device) {
    *device=device_;device_->AddRef();
}
}
