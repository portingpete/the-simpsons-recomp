#include "d3d11_vulkan.h"
#include "d3d11_vulkan_pipeline.h"
#include "dxgi_vulkan.h"
#include "renderer/native_backend.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>

namespace Simpsons::Graphics::Vulkan {
namespace {
constexpr std::array<uint32_t,3> querySlotCounts{4096,1024,256};
template<class F> HRESULT guarded(const char* operation,F&& body) {
    try {return body();}
    catch(const std::exception& error) {
        std::fprintf(stderr,"[NATIVE VULKAN] %s failed: %s\n",operation,error.what());
        return E_INVALIDARG;
    }
}
template<class Interface,class T> HRESULT hand(T* object,Interface** out) {
    *out=object;return S_OK;
}
}

D3D11Device::D3D11Device() {
    immediate_=std::make_unique<D3D11Context>(this,false);
    pipelines_=std::make_unique<PipelineCache>(*this);
    dxgi_=std::make_unique<DxgiDevice>(*this);
    // D3D11's sampler for unbound slots: trilinear, clamp, unbounded LOD.
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod=VK_LOD_CLAMP_NONE;sampler.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    check(vkCreateSampler(vulkan_.device(),&sampler,nullptr,&defaultSampler_),"default sampler creation");
    // D3D11 statistics without geometry, hull, domain or compute stages.
    statisticsFlags_=VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT|VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT|
        VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT|VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT|
        VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT|VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
    statisticsCount_=6;
    for(size_t kind=0;kind<querySlots_.size();++kind) {
        if(kind==size_t(QueryPool::Statistics) && !vulkan_.capabilities().pipelineStatistics) continue;
        VkQueryPoolCreateInfo pool{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        pool.queryType=kind==size_t(QueryPool::Timestamp)?VK_QUERY_TYPE_TIMESTAMP:kind==size_t(QueryPool::Occlusion)?VK_QUERY_TYPE_OCCLUSION:VK_QUERY_TYPE_PIPELINE_STATISTICS;
        pool.queryCount=querySlotCounts[kind];
        if(kind==size_t(QueryPool::Statistics)) pool.pipelineStatistics=statisticsFlags_;
        auto& slots=querySlots_[kind];
        check(vkCreateQueryPool(vulkan_.device(),&pool,nullptr,&slots.pool),"query pool creation");
        vkResetQueryPool(vulkan_.device(),slots.pool,0,pool.queryCount);
        for(uint32_t slot=pool.queryCount;slot>0;--slot) slots.free.push_back(slot-1);
    }
}
D3D11Device::~D3D11Device() {
    vkDeviceWaitIdle(vulkan_.device());
    dxgi_.reset();
    pipelines_.reset();
    immediate_.reset();
    if(defaultSampler_) vkDestroySampler(vulkan_.device(),defaultSampler_,nullptr);
    for(auto& slots:querySlots_) if(slots.pool) vkDestroyQueryPool(vulkan_.device(),slots.pool,nullptr);
}
HRESULT D3D11Device::QueryInterface(REFIID riid,void** object) {
    if(!object) return E_POINTER;
    *object=nullptr;
    if(riid==__uuidof(IUnknown) || riid==__uuidof(ID3D11Device) || riid==__uuidof(ID3D11Device1) || riid==__uuidof(ID3D11Device2) ||
       riid==__uuidof(ID3D11Device3) || riid==__uuidof(ID3D11Device4) || riid==__uuidof(D3D11Device)) {
        *object=static_cast<ID3D11Device4*>(this);AddRef();return S_OK;
    }
    if(riid==__uuidof(IDXGIObject) || riid==__uuidof(IDXGIDevice) || riid==__uuidof(IDXGIDevice1)) {
        *object=static_cast<IDXGIDevice1*>(dxgi_.get());AddRef();return S_OK;
    }
    return E_NOINTERFACE;
}
ULONG D3D11Device::AddRef() {return refs_.fetch_add(1,std::memory_order_relaxed)+1;}
ULONG D3D11Device::Release() {
    const ULONG left=refs_.fetch_sub(1,std::memory_order_acq_rel)-1;
    if(!left) destroy();
    return left;
}
// The last public reference drops every binding and cache that pins child
// objects; the device itself goes once no child keeps its private reference.
void D3D11Device::destroy() {
    if(shutDown_) {if(!children_.load(std::memory_order_acquire)) delete this;return;}
    shutDown_=true;
    try {immediate_->Flush();vulkan_.wait(vulkan_.submittedSerial(),5000000000ull);} catch(const std::exception& error) {
        std::fprintf(stderr,"[NATIVE VULKAN] device teardown flush failed: %s\n",error.what());
    }
    children_.fetch_add(1,std::memory_order_relaxed); // Hold deletion until the caches are gone.
    immediate_->releaseState();
    {
        std::vector<IUnknown*> cached;
        {
            std::lock_guard lock(stateMutex_);
            for(auto& [desc,state]:blendStates_) cached.push_back(state);
            for(auto& [desc,state]:depthStates_) cached.push_back(state);
            for(auto& [desc,state]:rasterStates_) cached.push_back(state);
            for(auto& [desc,state]:samplerStates_) cached.push_back(state);
            blendStates_.clear();depthStates_.clear();rasterStates_.clear();samplerStates_.clear();
        }
        for(auto* state:cached) state->Release();
    }
    if(children_.fetch_sub(1,std::memory_order_acq_rel)==1 && !refs_.load(std::memory_order_acquire)) delete this;
}
void D3D11Device::releaseChildReference() {
    if(children_.fetch_sub(1,std::memory_order_acq_rel)==1 && shutDown_ && !refs_.load(std::memory_order_acquire)) delete this;
}
uint32_t D3D11Device::acquireQuerySlot(QueryPool kind) {
    std::lock_guard lock(queryMutex_);
    auto& slots=querySlots_[size_t(kind)];
    if(!slots.pool) throw Error("Native Vulkan adapter lacks pipeline statistics queries");
    while(!slots.retiring.empty() && vulkan_.complete(slots.retiring.front().first)) {
        slots.free.push_back(slots.retiring.front().second);slots.retiring.pop_front();
    }
    if(slots.free.empty()) throw Error("Native Vulkan query slots are exhausted");
    const uint32_t slot=slots.free.back();slots.free.pop_back();
    // The slot's previous use has completed: reset on the host.
    vkResetQueryPool(vulkan_.device(),slots.pool,slot,1);
    return slot;
}
void D3D11Device::releaseQuerySlot(QueryPool kind,uint32_t slot) {
    std::lock_guard lock(queryMutex_);
    querySlots_[size_t(kind)].retiring.emplace_back(vulkan_.recordingSerial(),slot);
}
void D3D11Device::forgetBlend(const D3D11BlendState*) {}
void D3D11Device::forgetDepthStencil(const D3D11DepthStencilState*) {}
void D3D11Device::forgetRasterizer(const D3D11RasterizerState*) {}
void D3D11Device::forgetSampler(const D3D11SamplerState*) {}
// D3D11 returns one object per distinct descriptor. The cache keeps them
// until teardown, so an identical later request returns the same pointer.
template<class T,class Desc> HRESULT D3D11Device::dedupState(std::vector<std::pair<Desc,T*>>& cache,const Desc& desc,T** out) {
    std::lock_guard lock(stateMutex_);
    for(auto& [key,state]:cache) if(!std::memcmp(&key,&desc,sizeof(Desc))) {state->AddRef();*out=state;return S_OK;}
    T* state=new T(this,desc);
    state->AddRef(); // Cache reference.
    cache.emplace_back(desc,state);
    *out=state;
    return S_OK;
}

// ---------------------------------------------------------------- resources
HRESULT D3D11Device::CreateBuffer(const D3D11_BUFFER_DESC* desc,const D3D11_SUBRESOURCE_DATA* initial,ID3D11Buffer** buffer) {
    if(!desc || !desc->ByteWidth) return E_INVALIDARG;
    if(!buffer) return S_FALSE;
    return guarded("CreateBuffer",[&]{return hand(new D3D11Buffer(this,*desc,initial),buffer);});
}
HRESULT D3D11Device::CreateTexture2D(const D3D11_TEXTURE2D_DESC* desc,const D3D11_SUBRESOURCE_DATA* initial,ID3D11Texture2D** texture) {
    if(!desc) return E_INVALIDARG;
    if(!texture) return S_FALSE;
    return guarded("CreateTexture2D",[&]{return hand(new D3D11Texture2D(this,*desc,initial),texture);});
}
HRESULT D3D11Device::CreateTexture1D(const D3D11_TEXTURE1D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture1D**) {
    reportUnsupported("1D textures");return E_NOTIMPL;
}
HRESULT D3D11Device::CreateTexture3D(const D3D11_TEXTURE3D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture3D**) {
    reportUnsupported("3D textures");return E_NOTIMPL;
}
HRESULT D3D11Device::CreateShaderResourceView(ID3D11Resource* resource,const D3D11_SHADER_RESOURCE_VIEW_DESC* desc,ID3D11ShaderResourceView** view) {
    if(!resource) return E_INVALIDARG;
    if(!view) return S_FALSE;
    return guarded("CreateShaderResourceView",[&]{return hand(new D3D11ShaderResourceView(this,resource,desc),view);});
}
HRESULT D3D11Device::CreateUnorderedAccessView(ID3D11Resource* resource,const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc,ID3D11UnorderedAccessView** view) {
    if(!resource) return E_INVALIDARG;
    if(!view) return S_FALSE;
    return guarded("CreateUnorderedAccessView",[&]{return hand(new D3D11UnorderedAccessView(this,resource,desc),view);});
}
HRESULT D3D11Device::CreateRenderTargetView(ID3D11Resource* resource,const D3D11_RENDER_TARGET_VIEW_DESC* desc,ID3D11RenderTargetView** view) {
    if(!resource) return E_INVALIDARG;
    if(!view) return S_FALSE;
    return guarded("CreateRenderTargetView",[&]{return hand(new D3D11RenderTargetView(this,resource,desc),view);});
}
HRESULT D3D11Device::CreateDepthStencilView(ID3D11Resource* resource,const D3D11_DEPTH_STENCIL_VIEW_DESC* desc,ID3D11DepthStencilView** view) {
    if(!resource) return E_INVALIDARG;
    if(!view) return S_FALSE;
    return guarded("CreateDepthStencilView",[&]{return hand(new D3D11DepthStencilView(this,resource,desc),view);});
}
HRESULT D3D11Device::CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* descs,UINT count,const void* bytecode,SIZE_T length,ID3D11InputLayout** layout) {
    if((!descs && count) || count>D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT) return E_INVALIDARG;
    return guarded("CreateInputLayout",[&]()->HRESULT {
        const auto spirv=SpirvModule::fromDxbc(bytecode,length,VK_SHADER_STAGE_VERTEX_BIT);
        std::vector<InputElement> elements;
        std::array<uint32_t,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> next{};
        for(UINT i=0;i<count;++i) {
            const auto& d=descs[i];
            if(!d.SemanticName || d.InputSlot>=D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT) return E_INVALIDARG;
            InputElement element;
            element.semantic=d.SemanticName;
            for(auto& c:element.semantic) c=char(std::toupper(static_cast<unsigned char>(c)));
            element.index=d.SemanticIndex;element.format=d.Format;element.slot=d.InputSlot;
            element.instance=d.InputSlotClass==D3D11_INPUT_PER_INSTANCE_DATA;element.step=d.InstanceDataStepRate;
            if(element.instance && element.step!=1) throw Error("Native Vulkan instance step rates other than one are unsupported");
            const uint32_t bytes=vertexFormatBytes(d.Format);
            if(!bytes || vertexFormat(d.Format)==VK_FORMAT_UNDEFINED) throw Error("Native Vulkan vertex element format is unsupported");
            element.offset=d.AlignedByteOffset==D3D11_APPEND_ALIGNED_ELEMENT?(next[d.InputSlot]+3u)&~3u:d.AlignedByteOffset;
            next[d.InputSlot]=element.offset+bytes;
            elements.push_back(std::move(element));
        }
        // D3D11 rejects a layout that does not supply every shader input.
        for(const auto& input:spirv->inputs()) for(uint32_t k=0;k<input.locations;++k) {
            bool found=false;
            for(const auto& element:elements) found|=element.semantic==input.semantic && element.index==input.index+k;
            if(!found) throw Error("Native Vulkan input layout lacks shader input "+input.semantic+std::to_string(input.index+k)+" of "+spirv->name());
        }
        if(!layout) return S_FALSE;
        std::string content;
        for(const auto& element:elements)
            content+=element.semantic+"/"+std::to_string(element.index)+"/"+std::to_string(element.slot)+"/"+std::to_string(element.offset)+"/"+
                std::to_string(element.step)+"/"+std::to_string(int(element.format))+"/"+(element.instance?"i":"v")+";";
        uint64_t contentId;
        {
            std::lock_guard lock(stateMutex_);
            contentId=layoutContentIds_.try_emplace(std::move(content),layoutContentIds_.size()+1).first->second;
        }
        *layout=new D3D11InputLayout(this,std::move(elements),contentId);
        return S_OK;
    });
}
HRESULT D3D11Device::CreateVertexShader(const void* bytecode,SIZE_T length,ID3D11ClassLinkage* linkage,ID3D11VertexShader** shader) {
    if(linkage) {reportUnsupported("class linkage");return E_NOTIMPL;}
    return guarded("CreateVertexShader",[&]()->HRESULT {
        auto spirv=SpirvModule::fromDxbc(bytecode,length,VK_SHADER_STAGE_VERTEX_BIT);
        if(!shader) return S_FALSE;
        *shader=new D3D11VertexShader(this,std::move(spirv));return S_OK;
    });
}
HRESULT D3D11Device::CreatePixelShader(const void* bytecode,SIZE_T length,ID3D11ClassLinkage* linkage,ID3D11PixelShader** shader) {
    if(linkage) {reportUnsupported("class linkage");return E_NOTIMPL;}
    return guarded("CreatePixelShader",[&]()->HRESULT {
        auto spirv=SpirvModule::fromDxbc(bytecode,length,VK_SHADER_STAGE_FRAGMENT_BIT);
        if(!shader) return S_FALSE;
        *shader=new D3D11PixelShader(this,std::move(spirv));return S_OK;
    });
}
HRESULT D3D11Device::CreateGeometryShader(const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11GeometryShader**) {reportUnsupported("geometry shaders");return E_NOTIMPL;}
HRESULT D3D11Device::CreateGeometryShaderWithStreamOutput(const void*,SIZE_T,const D3D11_SO_DECLARATION_ENTRY*,UINT,const UINT*,UINT,UINT,ID3D11ClassLinkage*,ID3D11GeometryShader**) {
    reportUnsupported("stream output");return E_NOTIMPL;
}
HRESULT D3D11Device::CreateHullShader(const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11HullShader**) {reportUnsupported("hull shaders");return E_NOTIMPL;}
HRESULT D3D11Device::CreateDomainShader(const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11DomainShader**) {reportUnsupported("domain shaders");return E_NOTIMPL;}
HRESULT D3D11Device::CreateComputeShader(const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11ComputeShader**) {reportUnsupported("compute shaders");return E_NOTIMPL;}
HRESULT D3D11Device::CreateClassLinkage(ID3D11ClassLinkage**) {reportUnsupported("class linkage");return E_NOTIMPL;}
HRESULT D3D11Device::CreateBlendState(const D3D11_BLEND_DESC* desc,ID3D11BlendState** state) {
    if(!desc) return E_INVALIDARG;
    if(!state) return S_FALSE;
    return guarded("CreateBlendState",[&]{D3D11BlendState* out=nullptr;const HRESULT r=dedupState(blendStates_,*desc,&out);*state=out;return r;});
}
HRESULT D3D11Device::CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC* desc,ID3D11DepthStencilState** state) {
    if(!desc) return E_INVALIDARG;
    if(!state) return S_FALSE;
    return guarded("CreateDepthStencilState",[&]{D3D11DepthStencilState* out=nullptr;const HRESULT r=dedupState(depthStates_,*desc,&out);*state=out;return r;});
}
HRESULT D3D11Device::CreateRasterizerState(const D3D11_RASTERIZER_DESC* desc,ID3D11RasterizerState** state) {
    if(!desc) return E_INVALIDARG;
    if(!state) return S_FALSE;
    if(desc->FillMode!=D3D11_FILL_SOLID && desc->FillMode!=D3D11_FILL_WIREFRAME) return E_INVALIDARG;
    return guarded("CreateRasterizerState",[&]{D3D11RasterizerState* out=nullptr;const HRESULT r=dedupState(rasterStates_,*desc,&out);*state=out;return r;});
}
HRESULT D3D11Device::CreateSamplerState(const D3D11_SAMPLER_DESC* desc,ID3D11SamplerState** state) {
    if(!desc) return E_INVALIDARG;
    if(!state) return S_FALSE;
    return guarded("CreateSamplerState",[&]{D3D11SamplerState* out=nullptr;const HRESULT r=dedupState(samplerStates_,*desc,&out);*state=out;return r;});
}
HRESULT D3D11Device::CreateQuery(const D3D11_QUERY_DESC* desc,ID3D11Query** query) {
    if(!desc) return E_INVALIDARG;
    if(desc->MiscFlags&~UINT(D3D11_QUERY_MISC_PREDICATEHINT)) return E_INVALIDARG;
    switch(desc->Query) {
    case D3D11_QUERY_EVENT:case D3D11_QUERY_TIMESTAMP:case D3D11_QUERY_TIMESTAMP_DISJOINT:case D3D11_QUERY_OCCLUSION:break;
    case D3D11_QUERY_PIPELINE_STATISTICS:
        if(!vulkan_.capabilities().pipelineStatistics) {reportUnsupported("pipeline statistics queries");return E_NOTIMPL;}
        break;
    default:reportUnsupported("query types other than event, timestamp, occlusion and pipeline statistics");return E_NOTIMPL;
    }
    if(!query) return S_FALSE;
    *query=new D3D11Query(this,*desc);
    return S_OK;
}
HRESULT D3D11Device::CreatePredicate(const D3D11_QUERY_DESC* desc,ID3D11Predicate** predicate) {
    if(!desc) return E_INVALIDARG;
    if(desc->Query!=D3D11_QUERY_OCCLUSION_PREDICATE) {reportUnsupported("stream-output predicates");return E_NOTIMPL;}
    if(!predicate) return S_FALSE;
    *predicate=new D3D11Query(this,*desc);
    return S_OK;
}
HRESULT D3D11Device::CreateCounter(const D3D11_COUNTER_DESC*,ID3D11Counter**) {return DXGI_ERROR_UNSUPPORTED;}
HRESULT D3D11Device::CreateDeferredContext(UINT flags,ID3D11DeviceContext** context) {
    if(flags) return E_INVALIDARG;
    if(!context) return S_FALSE;
    *context=new D3D11Context(this,true);
    return S_OK;
}
HRESULT D3D11Device::CreateDeferredContext1(UINT flags,ID3D11DeviceContext1** context) {
    if(flags) return E_INVALIDARG;
    if(!context) return S_FALSE;
    *context=new D3D11Context(this,true);
    return S_OK;
}
HRESULT D3D11Device::OpenSharedResource(HANDLE,REFIID,void**) {reportUnsupported("shared resources");return E_NOTIMPL;}
HRESULT D3D11Device::OpenSharedResource1(HANDLE,REFIID,void**) {reportUnsupported("shared resources");return E_NOTIMPL;}
HRESULT D3D11Device::OpenSharedResourceByName(LPCWSTR,DWORD,REFIID,void**) {reportUnsupported("shared resources");return E_NOTIMPL;}
HRESULT D3D11Device::CreateBlendState1(const D3D11_BLEND_DESC1*,ID3D11BlendState1**) {reportUnsupported("logic-op blend states");return E_NOTIMPL;}
HRESULT D3D11Device::CreateRasterizerState1(const D3D11_RASTERIZER_DESC1*,ID3D11RasterizerState1**) {reportUnsupported("forced sample count rasterizer states");return E_NOTIMPL;}
HRESULT D3D11Device::CreateDeviceContextState(UINT,const D3D_FEATURE_LEVEL* levels,UINT count,UINT,REFIID emulated,
    D3D_FEATURE_LEVEL* chosen,ID3DDeviceContextState** state) {
    if(emulated!=__uuidof(ID3D11Device) && emulated!=__uuidof(ID3D11Device1)) return E_INVALIDARG;
    D3D_FEATURE_LEVEL selected{};bool found=false;
    for(UINT i=0;i<count && !found;++i) if(levels[i]<=D3D_FEATURE_LEVEL_11_1) {selected=levels[i];found=true;}
    if(!found) return E_INVALIDARG;
    if(chosen) *chosen=selected;
    if(!state) return S_FALSE;
    *state=new D3D11DeviceContextState(this);
    return S_OK;
}
HRESULT D3D11Device::RegisterDeviceRemovedEvent(HANDLE event,DWORD* cookie) {
    if(!event || !cookie) return E_INVALIDARG;
    *cookie=vulkan_.registerLostEvent(event);
    return S_OK;
}
void D3D11Device::UnregisterDeviceRemoved(DWORD cookie) {vulkan_.unregisterLostEvent(cookie);}
void D3D11Device::GetImmediateContext(ID3D11DeviceContext** context) {*context=immediate_.get();AddRef();}
void D3D11Device::GetImmediateContext1(ID3D11DeviceContext1** context) {*context=immediate_.get();AddRef();}

// ---------------------------------------------------------------- capability queries
HRESULT D3D11Device::CheckFormatSupport(DXGI_FORMAT format,UINT* support) {
    if(!support) return E_INVALIDARG;
    *support=0;
    if(format==DXGI_FORMAT_R16_UINT || format==DXGI_FORMAT_R32_UINT) *support|=D3D11_FORMAT_SUPPORT_IA_INDEX_BUFFER|D3D11_FORMAT_SUPPORT_BUFFER;
    if(vertexFormat(format)!=VK_FORMAT_UNDEFINED) *support|=D3D11_FORMAT_SUPPORT_IA_VERTEX_BUFFER|D3D11_FORMAT_SUPPORT_BUFFER;
    FormatInfo info;
    try {info=resourceFormat(format,0);}
    catch(const Error&) {
        // View-only depth/stencil formats sample their combined storage.
        if(format==DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS || format==DXGI_FORMAT_X32_TYPELESS_G8X24_UINT)
            info=resourceFormat(DXGI_FORMAT_R32G8X24_TYPELESS,0);
        else if(format==DXGI_FORMAT_R24_UNORM_X8_TYPELESS || format==DXGI_FORMAT_X24_TYPELESS_G8_UINT)
            info=resourceFormat(DXGI_FORMAT_R24G8_TYPELESS,0);
        else return *support?S_OK:E_FAIL;
    }
    VkFormat vk=info.format;
    try {
        if(!info.depth() && !info.stencil() && !info.typeless) vk=viewFormat(format,info).format;
    } catch(const Error&) {}
    VkFormatProperties properties{};vkGetPhysicalDeviceFormatProperties(vulkan_.physical(),vk,&properties);
    const auto features=properties.optimalTilingFeatures;
    if(features&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) {
        *support|=D3D11_FORMAT_SUPPORT_TEXTURE2D|D3D11_FORMAT_SUPPORT_TEXTURECUBE|D3D11_FORMAT_SUPPORT_SHADER_LOAD|
            D3D11_FORMAT_SUPPORT_SHADER_SAMPLE|D3D11_FORMAT_SUPPORT_MIP|D3D11_FORMAT_SUPPORT_CPU_LOCKABLE|
            D3D11_FORMAT_SUPPORT_CAST_WITHIN_BIT_LAYOUT|D3D11_FORMAT_SUPPORT_SHADER_GATHER;
        if(info.depth()) *support|=D3D11_FORMAT_SUPPORT_SHADER_SAMPLE_COMPARISON|D3D11_FORMAT_SUPPORT_SHADER_GATHER_COMPARISON;
    }
    if(features&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) *support|=D3D11_FORMAT_SUPPORT_RENDER_TARGET|D3D11_FORMAT_SUPPORT_MIP_AUTOGEN;
    if(features&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) *support|=D3D11_FORMAT_SUPPORT_BLENDABLE;
    if(features&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) *support|=D3D11_FORMAT_SUPPORT_DEPTH_STENCIL|D3D11_FORMAT_SUPPORT_TEXTURE2D;
    if(format==DXGI_FORMAT_R8G8B8A8_UNORM || format==DXGI_FORMAT_B8G8R8A8_UNORM || format==DXGI_FORMAT_R10G10B10A2_UNORM ||
       format==DXGI_FORMAT_R16G16B16A16_FLOAT || format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
        *support|=D3D11_FORMAT_SUPPORT_DISPLAY;
    return *support?S_OK:E_FAIL;
}
// Native Vulkan textures are single-sample only; no format reports multisample levels.
HRESULT D3D11Device::CheckMultisampleQualityLevels(DXGI_FORMAT,UINT samples,UINT* levels) {
    if(!levels) return E_INVALIDARG;
    *levels=samples==1?1u:0u;
    return S_OK;
}
void D3D11Device::CheckCounterInfo(D3D11_COUNTER_INFO* info) {if(info) *info={};}
HRESULT D3D11Device::CheckCounter(const D3D11_COUNTER_DESC*,D3D11_COUNTER_TYPE*,UINT*,LPSTR,UINT*,LPSTR,UINT*,LPSTR,UINT*) {return DXGI_ERROR_UNSUPPORTED;}
HRESULT D3D11Device::CheckFeatureSupport(D3D11_FEATURE feature,void* data,UINT size) {
    if(!data) return E_INVALIDARG;
    switch(feature) {
    case D3D11_FEATURE_THREADING: {
        if(size!=sizeof(D3D11_FEATURE_DATA_THREADING)) return E_INVALIDARG;
        // Creation is thread-safe (initialization uploads are serialized
        // ahead of the next submission); command lists replay natively.
        *static_cast<D3D11_FEATURE_DATA_THREADING*>(data)={TRUE,TRUE};return S_OK;
    }
    case D3D11_FEATURE_DOUBLES:
        if(size!=sizeof(D3D11_FEATURE_DATA_DOUBLES)) return E_INVALIDARG;
        *static_cast<D3D11_FEATURE_DATA_DOUBLES*>(data)={FALSE};return S_OK;
    case D3D11_FEATURE_FORMAT_SUPPORT: {
        if(size!=sizeof(D3D11_FEATURE_DATA_FORMAT_SUPPORT)) return E_INVALIDARG;
        auto* out=static_cast<D3D11_FEATURE_DATA_FORMAT_SUPPORT*>(data);
        return CheckFormatSupport(out->InFormat,&out->OutFormatSupport);
    }
    case D3D11_FEATURE_FORMAT_SUPPORT2:
        if(size!=sizeof(D3D11_FEATURE_DATA_FORMAT_SUPPORT2)) return E_INVALIDARG;
        static_cast<D3D11_FEATURE_DATA_FORMAT_SUPPORT2*>(data)->OutFormatSupport2=0;return S_OK;
    case D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS:
        if(size!=sizeof(D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS)) return E_INVALIDARG;
        *static_cast<D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS*>(data)={TRUE};return S_OK;
    case D3D11_FEATURE_D3D11_OPTIONS: {
        if(size!=sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS)) return E_INVALIDARG;
        D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
        options.DiscardAPIsSeenByDriver=TRUE;options.FlagsForUpdateAndCopySeenByDriver=TRUE;options.CopyWithOverlap=TRUE;
        options.ConstantBufferPartialUpdate=TRUE;options.ConstantBufferOffsetting=TRUE;options.MapNoOverwriteOnDynamicConstantBuffer=TRUE;
        *static_cast<D3D11_FEATURE_DATA_D3D11_OPTIONS*>(data)=options;return S_OK;
    }
    case D3D11_FEATURE_D3D11_OPTIONS2: {
        if(size!=sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS2)) return E_INVALIDARG;
        D3D11_FEATURE_DATA_D3D11_OPTIONS2 options{};
        options.PSSpecifiedStencilRefSupported=vulkan_.capabilities().stencilExport;
        *static_cast<D3D11_FEATURE_DATA_D3D11_OPTIONS2*>(data)=options;return S_OK;
    }
    default:return E_INVALIDARG;
    }
}

HRESULT createDevice(ID3D11Device** device,D3D_FEATURE_LEVEL* level,ID3D11DeviceContext** context) {
    try {
        auto* created=new D3D11Device();
        if(level) *level=D3D_FEATURE_LEVEL_11_1;
        if(context) created->GetImmediateContext(context);
        if(device) *device=created; else created->Release();
        return S_OK;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"[NATIVE VULKAN] device creation failed: %s\n",error.what());
        return E_FAIL;
    }
}
bool isVulkanDevice(ID3D11Device* device) {
    if(!device) return false;
    void* object=nullptr;
    if(FAILED(device->QueryInterface(__uuidof(D3D11Device),&object))) return false;
    static_cast<D3D11Device*>(object)->Release();
    return true;
}
std::string deviceDescription(ID3D11Device* device) {
    if(!isVulkanDevice(device)) return {};
    void* object=nullptr;device->QueryInterface(__uuidof(D3D11Device),&object);
    auto* vulkan=static_cast<D3D11Device*>(object);
    std::string text=vulkan->vulkan().description();
    vulkan->Release();
    return text;
}
}
