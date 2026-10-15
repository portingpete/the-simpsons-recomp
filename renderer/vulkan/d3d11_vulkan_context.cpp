#include "d3d11_vulkan.h"
#include "d3d11_vulkan_pipeline.h"
#include "renderer/native_backend.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

namespace Simpsons::Graphics::Vulkan {
namespace {
enum Dirty : uint32_t {
    DirtyPipeline=1u<<0,DirtyViewport=1u<<1,DirtyScissor=1u<<2,DirtyRaster=1u<<3,DirtyDepthStencil=1u<<4,
    DirtyBlendConstants=1u<<5,DirtyStencilRef=1u<<6,DirtyTopology=1u<<7,DirtyVertexBuffers=1u<<8,
    DirtyIndexBuffer=1u<<9,DirtyVertexDescriptors=1u<<10,DirtyPixelDescriptors=1u<<11};
constexpr UINT stageResourceSlots=D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
constexpr UINT stageSamplerSlots=D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;
constexpr UINT stageBufferSlots=D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;

extern "C" IMAGE_DOS_HEADER __ImageBase;
bool inThisImage(const void* address) {
    static const auto bounds=[] {
        const auto* base=reinterpret_cast<const uint8_t*>(&__ImageBase);
        const auto* headers=reinterpret_cast<const IMAGE_NT_HEADERS*>(base+__ImageBase.e_lfanew);
        return std::pair{base,base+headers->OptionalHeader.SizeOfImage};
    }();
    const auto* p=static_cast<const uint8_t*>(address);
    return p>=bounds.first && p<bounds.second;
}
// Every binding unwraps its object. QueryInterface proves the type; after
// that, the same vtable (one of this image's, never a hook's heap clone)
// identifies T without the interface query and its reference round trip.
template<class T> T* unwrap(IUnknown* object) {
    if(!object) return nullptr;
    static std::atomic<const void*> known{nullptr};
    const void* vtable=*reinterpret_cast<const void* const*>(object);
    if(vtable==known.load(std::memory_order_relaxed)) return static_cast<T*>(object);
    void* result=nullptr;
    if(FAILED(object->QueryInterface(__uuidof(T),&result)))
        throw Error("Native Vulkan context received an object that belongs to another device");
    static_cast<T*>(result)->Release();
    if(result==static_cast<void*>(object) && inThisImage(vtable)) known.store(vtable,std::memory_order_relaxed);
    return static_cast<T*>(result);
}
bool anyUavBound(const ContextState& state) {
    for(const auto* slots:{&state.outputUavs,&state.computeUavs})
        for(const auto& uav:*slots) if(uav) return true;
    return false;
}
template<class Interface,class T> void copyOut(const ComPtr<T>& value,Interface** out) {
    if(!out) return;
    *out=value.Get();
    if(*out) (*out)->AddRef();
}
// Deferred recording owns the arguments; replay passes the raw array without allocating.
template<class T> struct Captured {
    std::vector<ComPtr<T>> owned;
    std::vector<T*> raw;
    Captured(T* const* items,UINT count) {
        owned.reserve(count);raw.reserve(count);
        for(UINT i=0;i<count;++i) {owned.emplace_back(items?items[i]:nullptr);raw.push_back(items?items[i]:nullptr);}
    }
};
template<class T> std::shared_ptr<Captured<T>> capture(T* const* items,UINT count) {return std::make_shared<Captured<T>>(items,count);}
void requireNull(const void* object,const char* what) {
    if(object) {reportUnsupported(what);throw Error(std::string("Native Vulkan does not implement ")+what);}
}
template<class T> void requireNulls(T* const* items,UINT count,const char* what) {
    if(items) for(UINT i=0;i<count;++i) requireNull(items[i],what);
}
template<class T> void nullOut(T** items,UINT count) {if(items) for(UINT i=0;i<count;++i) items[i]=nullptr;}
ID3D11Resource* resourceOf(ID3D11Resource* resource) {return resource;}
VkCullModeFlags cullMode(D3D11_CULL_MODE mode) {
    return mode==D3D11_CULL_NONE?VK_CULL_MODE_NONE:mode==D3D11_CULL_FRONT?VK_CULL_MODE_FRONT_BIT:VK_CULL_MODE_BACK_BIT;
}
VkStencilOp stencilOp(D3D11_STENCIL_OP op) {
    switch(op) {
    case D3D11_STENCIL_OP_KEEP:return VK_STENCIL_OP_KEEP;
    case D3D11_STENCIL_OP_ZERO:return VK_STENCIL_OP_ZERO;
    case D3D11_STENCIL_OP_REPLACE:return VK_STENCIL_OP_REPLACE;
    case D3D11_STENCIL_OP_INCR_SAT:return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case D3D11_STENCIL_OP_DECR_SAT:return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case D3D11_STENCIL_OP_INVERT:return VK_STENCIL_OP_INVERT;
    case D3D11_STENCIL_OP_INCR:return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case D3D11_STENCIL_OP_DECR:return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default:throw Error("Native Vulkan stencil operation is invalid");
    }
}
// D3D11 defaults for null state objects.
const D3D11_RASTERIZER_DESC defaultRaster{D3D11_FILL_SOLID,D3D11_CULL_BACK,FALSE,0,0.0f,0.0f,TRUE,FALSE,FALSE,FALSE};
const D3D11_DEPTH_STENCIL_DESC defaultDepth{TRUE,D3D11_DEPTH_WRITE_MASK_ALL,D3D11_COMPARISON_LESS,FALSE,
    D3D11_DEFAULT_STENCIL_READ_MASK,D3D11_DEFAULT_STENCIL_WRITE_MASK,
    {D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS},
    {D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS}};
struct Subresource {uint32_t mip,layer;};
Subresource split(const D3D11Texture2D& texture,UINT subresource) {
    const uint32_t mips=texture.desc().MipLevels;
    if(subresource>=texture.subresources()) throw Error("Native Vulkan subresource index is out of range");
    return {subresource%mips,subresource/mips};
}
}

D3D11Context::D3D11Context(D3D11Device* device,bool deferred)
    :DeviceChild(device,deferred),deferred_(deferred) {
    activeState_.Attach(new D3D11DeviceContextState(device));
}
D3D11Context::~D3D11Context()=default;
bool D3D11Context::supports(REFIID riid) const {
    return riid==__uuidof(ID3D11DeviceContext) || riid==__uuidof(ID3D11DeviceContext1) || riid==__uuidof(D3D11Context);
}
ULONG D3D11Context::AddRef() {return deferred_?DeviceChild::AddRef():device_->AddRef();}
ULONG D3D11Context::Release() {return deferred_?DeviceChild::Release():device_->Release();}

// ---------------------------------------------------------------- binding helpers
bool D3D11Context::boundAsOutput(const D3D11ShaderResourceView& view) const {
    const auto* texture=view.texture();
    if(state_->uavsBound)
        for(const auto* slots:{&state_->outputUavs,&state_->computeUavs})
            for(const auto& uav:*slots) if(uav && uav->resource()==view.resource()) return true;
    for(const auto& target:state_->renderTargets)
        if(target && target->texture()==texture && overlaps(target->range(),view.range())) return true;
    return state_->depthStencil && state_->depthStencil->texture()==texture && overlaps(state_->depthStencil->range(),view.range());
}
// D3D11 hazard rule: binding a subresource for output unbinds its shader views.
void D3D11Context::unbindConflictingResources() {
    for(auto* stage:{&state_->vertex,&state_->pixel,&state_->auxiliary[0],&state_->auxiliary[1],&state_->auxiliary[2],&state_->auxiliary[3]}) {
        bool changed=false;
        for(uint32_t slot=0;slot<stage->resourceEnd;++slot) {
            auto& view=stage->resources[slot];
            if(view && boundAsOutput(*view.Get())) {view.Reset();changed=true;}
        }
        if(changed) dirty_|=stage==&state_->vertex?DirtyVertexDescriptors:DirtyPixelDescriptors;
    }
}
// Binding a resource for unordered access unbinds its shader views.
void D3D11Context::unbindResourcesOf(ID3D11Resource* resource) {
    for(auto* stage:{&state_->vertex,&state_->pixel,&state_->auxiliary[0],&state_->auxiliary[1],&state_->auxiliary[2],&state_->auxiliary[3]}) {
        for(uint32_t slot=0;slot<stage->resourceEnd;++slot) {
            auto& view=stage->resources[slot];
            if(view && view->resource()==resource) {view.Reset();dirty_|=DirtyVertexDescriptors|DirtyPixelDescriptors;}
        }
    }
}
void D3D11Context::setUavs(std::array<ComPtr<D3D11UnorderedAccessView>,D3D11_1_UAV_SLOT_COUNT>& slots,UINT start,UINT count,
    ID3D11UnorderedAccessView* const* views,const UINT* counts) {
    if(start>=slots.size() || count>slots.size()-start) throw Error("Native Vulkan UAV slots are out of range");
    for(UINT i=0;i<count;++i) {
        auto* view=views?unwrap<D3D11UnorderedAccessView>(views[i]):nullptr;
        slots[start+i]=view;
        if(!view) continue;
        // An initial count of -1 keeps the hidden append/consume counter.
        if(counts && counts[i]!=UINT(-1)) view->counter=counts[i];
        unbindResourcesOf(view->resource());
    }
}
void D3D11Context::setConstantBuffers(StageBindings& stage,uint32_t dirty,UINT start,UINT count,ID3D11Buffer* const* buffers,
    const UINT* first,const UINT* number) {
    if(start>=stageBufferSlots || count>stageBufferSlots-start) throw Error("Native Vulkan constant buffer slots are out of range");
    for(UINT i=0;i<count;++i) {
        auto* buffer=buffers?unwrap<D3D11Buffer>(buffers[i]):nullptr;
        if(buffer && !(buffer->desc().BindFlags&D3D11_BIND_CONSTANT_BUFFER)) buffer=nullptr;
        stage.constantBuffers[start+i]=buffer;
        stage.firstConstant[start+i]=first&&buffer?first[i]:0;
        stage.constantCount[start+i]=number&&buffer?number[i]:0;
    }
    dirty_|=dirty;
}
void D3D11Context::setResources(StageBindings& stage,uint32_t dirty,UINT start,UINT count,ID3D11ShaderResourceView* const* views) {
    if(start>=stageResourceSlots || count>stageResourceSlots-start) throw Error("Native Vulkan shader resource slots are out of range");
    for(UINT i=0;i<count;++i) {
        auto* view=views?unwrap<D3D11ShaderResourceView>(views[i]):nullptr;
        // Still bound as an output: D3D11 forces the input binding to null.
        if(view && boundAsOutput(*view)) view=nullptr;
        stage.resources[start+i]=view;
    }
    uint32_t end=std::max<uint32_t>(stage.resourceEnd,start+count);
    while(end && !stage.resources[end-1]) --end;
    stage.resourceEnd=end;
    dirty_|=dirty;
}
void D3D11Context::setSamplers(StageBindings& stage,uint32_t dirty,UINT start,UINT count,ID3D11SamplerState* const* samplers) {
    if(start>=stageSamplerSlots || count>stageSamplerSlots-start) throw Error("Native Vulkan sampler slots are out of range");
    for(UINT i=0;i<count;++i) stage.samplers[start+i]=samplers?unwrap<D3D11SamplerState>(samplers[i]):nullptr;
    dirty_|=dirty;
}
void D3D11Context::getConstantBuffers(const StageBindings& stage,UINT start,UINT count,ID3D11Buffer** buffers,UINT* first,UINT* number) const {
    for(UINT i=0;i<count;++i) {
        const UINT slot=start+i;const bool valid=slot<stageBufferSlots;
        if(buffers) copyOut(valid?stage.constantBuffers[slot]:ComPtr<D3D11Buffer>{},&buffers[i]);
        if(first) first[i]=valid?stage.firstConstant[slot]:0;
        if(number) number[i]=valid?stage.constantCount[slot]:0;
    }
}
void D3D11Context::getResources(const StageBindings& stage,UINT start,UINT count,ID3D11ShaderResourceView** views) const {
    for(UINT i=0;i<count;++i) copyOut(start+i<stageResourceSlots?stage.resources[start+i]:ComPtr<D3D11ShaderResourceView>{},&views[i]);
}
void D3D11Context::getSamplers(const StageBindings& stage,UINT start,UINT count,ID3D11SamplerState** samplers) const {
    for(UINT i=0;i<count;++i) copyOut(start+i<stageSamplerSlots?stage.samplers[start+i]:ComPtr<D3D11SamplerState>{},&samplers[i]);
}
void D3D11Context::markAllDirty() {dirty_=~0u;}
void D3D11Context::resetState() {state_->clear();markAllDirty();}
void D3D11Context::releaseState() {
    state_->clear();spareState_.reset();activeState_.Reset();recorded_.clear();deferredMaps_.clear();
}

// ---------------------------------------------------------------- shader stages
void D3D11Context::VSSetShader(ID3D11VertexShader* shader,ID3D11ClassInstance* const* instances,UINT count) {
    requireNulls(instances,count,"class instances");
    state_->vertexShader=unwrap<D3D11VertexShader>(shader);dirty_|=DirtyPipeline|DirtyVertexDescriptors;
    if(deferred_) record([object=ComPtr<ID3D11VertexShader>(shader)](D3D11Context& c){c.VSSetShader(object.Get(),nullptr,0);});
}
void D3D11Context::PSSetShader(ID3D11PixelShader* shader,ID3D11ClassInstance* const* instances,UINT count) {
    requireNulls(instances,count,"class instances");
    state_->pixelShader=unwrap<D3D11PixelShader>(shader);dirty_|=DirtyPipeline|DirtyPixelDescriptors;
    if(deferred_) record([object=ComPtr<ID3D11PixelShader>(shader)](D3D11Context& c){c.PSSetShader(object.Get(),nullptr,0);});
}
void D3D11Context::VSGetShader(ID3D11VertexShader** shader,ID3D11ClassInstance** instances,UINT* count) {
    copyOut(state_->vertexShader,shader);if(count) *count=0;(void)instances;
}
void D3D11Context::PSGetShader(ID3D11PixelShader** shader,ID3D11ClassInstance** instances,UINT* count) {
    copyOut(state_->pixelShader,shader);if(count) *count=0;(void)instances;
}
#define SIMPSONS_STAGE_SETTERS(PREFIX,STAGE,DIRTY) \
void D3D11Context::PREFIX##SetConstantBuffers(UINT start,UINT count,ID3D11Buffer* const* buffers) { \
    setConstantBuffers(state_->STAGE,DIRTY,start,count,buffers,nullptr,nullptr); \
    if(deferred_) record([start,items=capture(buffers,count)](D3D11Context& c){c.PREFIX##SetConstantBuffers(start,UINT(items->raw.size()),items->raw.data());}); \
} \
void D3D11Context::PREFIX##SetConstantBuffers1(UINT start,UINT count,ID3D11Buffer* const* buffers,const UINT* first,const UINT* number) { \
    setConstantBuffers(state_->STAGE,DIRTY,start,count,buffers,first,number); \
    if(deferred_) { \
        std::vector<UINT> firsts(first?first:nullptr,first?first+count:nullptr),numbers(number?number:nullptr,number?number+count:nullptr); \
        record([start,items=capture(buffers,count),firsts,numbers](D3D11Context& c){ \
            c.PREFIX##SetConstantBuffers1(start,UINT(items->raw.size()),items->raw.data(),firsts.empty()?nullptr:firsts.data(),numbers.empty()?nullptr:numbers.data());}); \
    } \
} \
void D3D11Context::PREFIX##SetShaderResources(UINT start,UINT count,ID3D11ShaderResourceView* const* views) { \
    setResources(state_->STAGE,DIRTY,start,count,views); \
    if(deferred_) record([start,items=capture(views,count)](D3D11Context& c){c.PREFIX##SetShaderResources(start,UINT(items->raw.size()),items->raw.data());}); \
} \
void D3D11Context::PREFIX##SetSamplers(UINT start,UINT count,ID3D11SamplerState* const* samplers) { \
    setSamplers(state_->STAGE,DIRTY,start,count,samplers); \
    if(deferred_) record([start,items=capture(samplers,count)](D3D11Context& c){c.PREFIX##SetSamplers(start,UINT(items->raw.size()),items->raw.data());}); \
} \
void D3D11Context::PREFIX##GetConstantBuffers(UINT start,UINT count,ID3D11Buffer** buffers) {getConstantBuffers(state_->STAGE,start,count,buffers,nullptr,nullptr);} \
void D3D11Context::PREFIX##GetConstantBuffers1(UINT start,UINT count,ID3D11Buffer** buffers,UINT* first,UINT* number) {getConstantBuffers(state_->STAGE,start,count,buffers,first,number);} \
void D3D11Context::PREFIX##GetShaderResources(UINT start,UINT count,ID3D11ShaderResourceView** views) {getResources(state_->STAGE,start,count,views);} \
void D3D11Context::PREFIX##GetSamplers(UINT start,UINT count,ID3D11SamplerState** samplers) {getSamplers(state_->STAGE,start,count,samplers);}
SIMPSONS_STAGE_SETTERS(VS,vertex,DirtyVertexDescriptors)
SIMPSONS_STAGE_SETTERS(PS,pixel,DirtyPixelDescriptors)
SIMPSONS_STAGE_SETTERS(GS,auxiliary[0],0)
SIMPSONS_STAGE_SETTERS(HS,auxiliary[1],0)
SIMPSONS_STAGE_SETTERS(DS,auxiliary[2],0)
SIMPSONS_STAGE_SETTERS(CS,auxiliary[3],0)
#undef SIMPSONS_STAGE_SETTERS
// Shader stages this renderer never creates: only null shaders bind; their
// resource bindings above are retained state that no shader reads.
#define SIMPSONS_UNUSED_STAGE(PREFIX,SHADER,NAME) \
void D3D11Context::PREFIX##SetShader(SHADER* shader,ID3D11ClassInstance* const* instances,UINT count) {requireNull(shader,NAME " shaders");requireNulls(instances,count,"class instances");} \
void D3D11Context::PREFIX##GetShader(SHADER** shader,ID3D11ClassInstance** instances,UINT* count) {if(shader) *shader=nullptr;if(count) *count=0;(void)instances;}
SIMPSONS_UNUSED_STAGE(GS,ID3D11GeometryShader,"geometry")
SIMPSONS_UNUSED_STAGE(HS,ID3D11HullShader,"hull")
SIMPSONS_UNUSED_STAGE(DS,ID3D11DomainShader,"domain")
SIMPSONS_UNUSED_STAGE(CS,ID3D11ComputeShader,"compute")
#undef SIMPSONS_UNUSED_STAGE
void D3D11Context::CSSetUnorderedAccessViews(UINT start,UINT count,ID3D11UnorderedAccessView* const* views,const UINT* counts) {
    setUavs(state_->computeUavs,start,count,views,counts);
    state_->uavsBound=anyUavBound(*state_);
    if(deferred_) {
        std::vector<UINT> c(counts?counts:nullptr,counts?counts+count:nullptr);
        record([start,items=capture(views,count),c](D3D11Context& context){
            context.CSSetUnorderedAccessViews(start,UINT(items->raw.size()),items->raw.data(),c.empty()?nullptr:c.data());});
    }
}
void D3D11Context::CSGetUnorderedAccessViews(UINT start,UINT count,ID3D11UnorderedAccessView** views) {
    for(UINT i=0;i<count;++i) copyOut(start+i<state_->computeUavs.size()?state_->computeUavs[start+i]:ComPtr<D3D11UnorderedAccessView>{},&views[i]);
}

// ---------------------------------------------------------------- input assembler
void D3D11Context::IASetInputLayout(ID3D11InputLayout* layout) {
    state_->inputLayout=unwrap<D3D11InputLayout>(layout);dirty_|=DirtyPipeline|DirtyVertexBuffers;
    if(deferred_) record([object=ComPtr<ID3D11InputLayout>(layout)](D3D11Context& c){c.IASetInputLayout(object.Get());});
}
void D3D11Context::IAGetInputLayout(ID3D11InputLayout** layout) {copyOut(state_->inputLayout,layout);}
void D3D11Context::IASetVertexBuffers(UINT start,UINT count,ID3D11Buffer* const* buffers,const UINT* strides,const UINT* offsets) {
    if(start>=D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT || count>D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT-start)
        throw Error("Native Vulkan vertex buffer slots are out of range");
    for(UINT i=0;i<count;++i) {
        auto* buffer=buffers?unwrap<D3D11Buffer>(buffers[i]):nullptr;
        state_->vertexBuffers[start+i]=buffer;
        state_->strides[start+i]=buffer&&strides?strides[i]:0;state_->offsets[start+i]=buffer&&offsets?offsets[i]:0;
    }
    dirty_|=DirtyVertexBuffers;
    if(deferred_) {
        std::vector<UINT> s(strides?strides:nullptr,strides?strides+count:nullptr),o(offsets?offsets:nullptr,offsets?offsets+count:nullptr);
        record([start,items=capture(buffers,count),s,o](D3D11Context& c){
            c.IASetVertexBuffers(start,UINT(items->raw.size()),items->raw.data(),s.empty()?nullptr:s.data(),o.empty()?nullptr:o.data());});
    }
}
void D3D11Context::IAGetVertexBuffers(UINT start,UINT count,ID3D11Buffer** buffers,UINT* strides,UINT* offsets) {
    for(UINT i=0;i<count;++i) {
        const UINT slot=start+i;const bool valid=slot<D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
        if(buffers) copyOut(valid?state_->vertexBuffers[slot]:ComPtr<D3D11Buffer>{},&buffers[i]);
        if(strides) strides[i]=valid?state_->strides[slot]:0;
        if(offsets) offsets[i]=valid?state_->offsets[slot]:0;
    }
}
void D3D11Context::IASetIndexBuffer(ID3D11Buffer* buffer,DXGI_FORMAT format,UINT offset) {
    state_->indexBuffer=unwrap<D3D11Buffer>(buffer);
    state_->indexFormat=buffer?format:DXGI_FORMAT_UNKNOWN;state_->indexOffset=buffer?offset:0;
    if(buffer && format!=DXGI_FORMAT_R16_UINT && format!=DXGI_FORMAT_R32_UINT) throw Error("Native Vulkan index format must be R16_UINT or R32_UINT");
    dirty_|=DirtyIndexBuffer;
    if(deferred_) record([object=ComPtr<ID3D11Buffer>(buffer),format,offset](D3D11Context& c){c.IASetIndexBuffer(object.Get(),format,offset);});
}
void D3D11Context::IAGetIndexBuffer(ID3D11Buffer** buffer,DXGI_FORMAT* format,UINT* offset) {
    copyOut(state_->indexBuffer,buffer);if(format) *format=state_->indexFormat;if(offset) *offset=state_->indexOffset;
}
void D3D11Context::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY topology) {
    if(topology!=state_->topology) {
        if(topologyClass(topology)!=topologyClass(state_->topology)) dirty_|=DirtyPipeline;
        state_->topology=topology;dirty_|=DirtyTopology;
    }
    if(deferred_) record([topology](D3D11Context& c){c.IASetPrimitiveTopology(topology);});
}
void D3D11Context::IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY* topology) {*topology=state_->topology;}

// ---------------------------------------------------------------- output merger and rasterizer
void D3D11Context::OMSetRenderTargets(UINT count,ID3D11RenderTargetView* const* views,ID3D11DepthStencilView* depth) {
    if(count>D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) throw Error("Native Vulkan render target count is out of range");
    for(UINT i=0;i<D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;++i)
        state_->renderTargets[i]=i<count&&views?unwrap<D3D11RenderTargetView>(views[i]):nullptr;
    state_->depthStencil=unwrap<D3D11DepthStencilView>(depth);
    // OMSetRenderTargets also unbinds every output-merger UAV.
    if(state_->uavsBound) {for(auto& uav:state_->outputUavs) uav.Reset();state_->uavsBound=anyUavBound(*state_);}
    unbindConflictingResources();
    dirty_|=DirtyPipeline|DirtyScissor;
    if(deferred_) record([items=capture(views,count),object=ComPtr<ID3D11DepthStencilView>(depth)](D3D11Context& c){
        c.OMSetRenderTargets(UINT(items->raw.size()),items->raw.data(),object.Get());});
}
void D3D11Context::OMSetRenderTargetsAndUnorderedAccessViews(UINT rtvs,ID3D11RenderTargetView* const* views,ID3D11DepthStencilView* depth,
    UINT uavStart,UINT uavs,ID3D11UnorderedAccessView* const* unordered,const UINT* counts) {
    const bool keepTargets=rtvs==D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,keepUavs=uavs==D3D11_KEEP_UNORDERED_ACCESS_VIEWS;
    if(deferred_) {
        std::vector<UINT> c;
        if(!keepUavs && counts) c.assign(counts,counts+uavs);
        record([rtvs,targets=capture(views,keepTargets?0:rtvs),object=ComPtr<ID3D11DepthStencilView>(depth),
            uavStart,uavs,items=capture(unordered,keepUavs?0:uavs),c](D3D11Context& context){
            context.OMSetRenderTargetsAndUnorderedAccessViews(rtvs,targets->raw.data(),object.Get(),uavStart,uavs,items->raw.data(),c.empty()?nullptr:c.data());});
    }
    if(!keepTargets) {
        if(rtvs>D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) throw Error("Native Vulkan render target count is out of range");
        for(UINT i=0;i<D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;++i)
            state_->renderTargets[i]=i<rtvs&&views?unwrap<D3D11RenderTargetView>(views[i]):nullptr;
        state_->depthStencil=unwrap<D3D11DepthStencilView>(depth);
        unbindConflictingResources();
        dirty_|=DirtyPipeline|DirtyScissor;
    }
    if(!keepUavs) {
        for(auto& uav:state_->outputUavs) uav.Reset();
        if(uavs) setUavs(state_->outputUavs,uavStart,uavs,unordered,counts);
        state_->uavsBound=anyUavBound(*state_);
    }
}
void D3D11Context::OMGetRenderTargets(UINT count,ID3D11RenderTargetView** views,ID3D11DepthStencilView** depth) {
    if(views) for(UINT i=0;i<count;++i) copyOut(i<D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT?state_->renderTargets[i]:ComPtr<D3D11RenderTargetView>{},&views[i]);
    copyOut(state_->depthStencil,depth);
}
void D3D11Context::OMGetRenderTargetsAndUnorderedAccessViews(UINT rtvs,ID3D11RenderTargetView** views,ID3D11DepthStencilView** depth,
    UINT uavStart,UINT uavs,ID3D11UnorderedAccessView** unordered) {
    OMGetRenderTargets(rtvs,views,depth);
    if(unordered) for(UINT i=0;i<uavs;++i)
        copyOut(uavStart+i<state_->outputUavs.size()?state_->outputUavs[uavStart+i]:ComPtr<D3D11UnorderedAccessView>{},&unordered[i]);
}
void D3D11Context::OMSetBlendState(ID3D11BlendState* blend,const FLOAT factor[4],UINT mask) {
    state_->blend=unwrap<D3D11BlendState>(blend);
    state_->blendFactor=factor?std::array<float,4>{factor[0],factor[1],factor[2],factor[3]}:std::array<float,4>{1,1,1,1};
    state_->sampleMask=mask;dirty_|=DirtyPipeline|DirtyBlendConstants;
    if(deferred_) record([object=ComPtr<ID3D11BlendState>(blend),f=state_->blendFactor,mask](D3D11Context& c){c.OMSetBlendState(object.Get(),f.data(),mask);});
}
void D3D11Context::OMGetBlendState(ID3D11BlendState** blend,FLOAT factor[4],UINT* mask) {
    copyOut(state_->blend,blend);
    if(factor) std::memcpy(factor,state_->blendFactor.data(),sizeof(float)*4);
    if(mask) *mask=state_->sampleMask;
}
void D3D11Context::OMSetDepthStencilState(ID3D11DepthStencilState* depth,UINT reference) {
    state_->depthStencilState=unwrap<D3D11DepthStencilState>(depth);state_->stencilRef=reference;
    dirty_|=DirtyDepthStencil|DirtyStencilRef;
    if(deferred_) record([object=ComPtr<ID3D11DepthStencilState>(depth),reference](D3D11Context& c){c.OMSetDepthStencilState(object.Get(),reference);});
}
void D3D11Context::OMGetDepthStencilState(ID3D11DepthStencilState** depth,UINT* reference) {
    copyOut(state_->depthStencilState,depth);if(reference) *reference=state_->stencilRef;
}
void D3D11Context::SOSetTargets(UINT count,ID3D11Buffer* const* buffers,const UINT* offsets) {
    if(count>D3D11_SO_BUFFER_SLOT_COUNT) throw Error("Native Vulkan stream-output target count is out of range");
    // Slots past the supplied count are unbound, as in D3D11.
    for(UINT i=0;i<D3D11_SO_BUFFER_SLOT_COUNT;++i) {
        state_->streamOutputs[i]=i<count&&buffers?unwrap<D3D11Buffer>(buffers[i]):nullptr;
        state_->streamOffsets[i]=i<count&&offsets?offsets[i]:0;
    }
    if(deferred_) {
        std::array<ComPtr<ID3D11Buffer>,D3D11_SO_BUFFER_SLOT_COUNT> objects;std::array<UINT,D3D11_SO_BUFFER_SLOT_COUNT> starts{};
        for(UINT i=0;i<count;++i) {objects[i]=buffers?buffers[i]:nullptr;starts[i]=offsets?offsets[i]:0;}
        record([objects,starts,count](D3D11Context& c) {
            std::array<ID3D11Buffer*,D3D11_SO_BUFFER_SLOT_COUNT> raw{};for(UINT i=0;i<count;++i) raw[i]=objects[i].Get();
            c.SOSetTargets(count,raw.data(),starts.data());
        });
    }
}
void D3D11Context::SOGetTargets(UINT count,ID3D11Buffer** buffers) {
    if(!buffers) return;
    for(UINT i=0;i<count;++i) copyOut(i<D3D11_SO_BUFFER_SLOT_COUNT?state_->streamOutputs[i]:ComPtr<D3D11Buffer>{},&buffers[i]);
}
void D3D11Context::RSSetState(ID3D11RasterizerState* raster) {
    auto* state=unwrap<D3D11RasterizerState>(raster);
    const auto& next=state?state->desc():defaultRaster;
    const auto& previous=state_->rasterizer?state_->rasterizer->desc():defaultRaster;
    if(next.FillMode!=previous.FillMode || next.DepthClipEnable!=previous.DepthClipEnable) dirty_|=DirtyPipeline;
    state_->rasterizer=state;dirty_|=DirtyRaster|DirtyScissor;
    if(deferred_) record([object=ComPtr<ID3D11RasterizerState>(raster)](D3D11Context& c){c.RSSetState(object.Get());});
}
void D3D11Context::RSGetState(ID3D11RasterizerState** raster) {copyOut(state_->rasterizer,raster);}
void D3D11Context::RSSetViewports(UINT count,const D3D11_VIEWPORT* viewports) {
    if(count>state_->viewports.size()) throw Error("Native Vulkan viewport count is out of range");
    for(UINT i=0;i<count;++i) state_->viewports[i]=viewports[i];
    state_->viewportCount=count;dirty_|=DirtyViewport;
    if(deferred_) {
        std::vector<D3D11_VIEWPORT> copy(viewports,viewports+count);
        record([copy](D3D11Context& c){c.RSSetViewports(UINT(copy.size()),copy.data());});
    }
}
// D3D11 reports the bound count through the in/out argument.
void D3D11Context::RSGetViewports(UINT* count,D3D11_VIEWPORT* viewports) {
    if(!count) return;
    if(viewports) for(UINT i=0;i<*count;++i) viewports[i]=i<state_->viewportCount?state_->viewports[i]:D3D11_VIEWPORT{};
    *count=state_->viewportCount;
}
void D3D11Context::RSSetScissorRects(UINT count,const D3D11_RECT* rects) {
    if(count>state_->scissors.size()) throw Error("Native Vulkan scissor count is out of range");
    for(UINT i=0;i<count;++i) state_->scissors[i]=rects[i];
    state_->scissorCount=count;dirty_|=DirtyScissor;
    if(deferred_) {
        std::vector<D3D11_RECT> copy(rects,rects+count);
        record([copy](D3D11Context& c){c.RSSetScissorRects(UINT(copy.size()),copy.data());});
    }
}
void D3D11Context::RSGetScissorRects(UINT* count,D3D11_RECT* rects) {
    if(!count) return;
    if(rects) for(UINT i=0;i<*count;++i) rects[i]=i<state_->scissorCount?state_->scissors[i]:D3D11_RECT{};
    *count=state_->scissorCount;
}
void D3D11Context::SetPredication(ID3D11Predicate* predicate,BOOL value) {
    state_->predicate=unwrap<D3D11Query>(predicate);state_->predicateValue=value; // Retained even without a predicate.
    if(deferred_) record([object=ComPtr<ID3D11Predicate>(predicate),value](D3D11Context& c){c.SetPredication(object.Get(),value);});
}
void D3D11Context::GetPredication(ID3D11Predicate** predicate,BOOL* value) {
    if(predicate) {*predicate=state_->predicate.Get();if(*predicate) (*predicate)->AddRef();}
    if(value) *value=state_->predicateValue;
}
void D3D11Context::requireUnpredicated(const char* operation) const {
    if(state_->predicate) throw Error(std::string("Native Vulkan does not implement predicated ")+operation);
}

// ---------------------------------------------------------------- state objects
void D3D11Context::ClearState() {
    resetState();
    if(deferred_) record([](D3D11Context& c){c.ClearState();});
}
void D3D11Context::SwapDeviceContextState(ID3DDeviceContextState* next,ID3DDeviceContextState** previous) {
    if(deferred_) throw Error("Native Vulkan deferred contexts cannot swap device context state");
    auto* incoming=unwrap<D3D11DeviceContextState>(next);
    if(previous) copyOut(activeState_,previous);
    if(!incoming || incoming==activeState_.Get()) return;
    // The active object receives the live state and the incoming object's
    // stored state becomes live. An active object's own storage is a cleared
    // spare: it passes to the incoming object, so swaps exchange pointers only.
    std::swap(state_,incoming->state);
    std::swap(incoming->state,activeState_->state);
    activeState_=incoming;
    markAllDirty();
}

// ---------------------------------------------------------------- Vulkan recording
VkCommandBuffer D3D11Context::commands() {return device_->vulkan().commands();}
void D3D11Context::barrier() {
    if(!barrierPending_) return;
    // GENERAL layout throughout: one full memory dependency orders every
    // transfer/attachment write before every later read or write.
    VkMemoryBarrier2 memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    memory.srcStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;memory.srcAccessMask=VK_ACCESS_2_MEMORY_WRITE_BIT;
    memory.dstStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;memory.dstAccessMask=VK_ACCESS_2_MEMORY_READ_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dependency.memoryBarrierCount=1;dependency.pMemoryBarriers=&memory;
    vkCmdPipelineBarrier2(commands(),&dependency);
    barrierPending_=false;
}
void D3D11Context::endRendering() {
    if(!rendering_) return;
    vkCmdEndRendering(commands());
    rendering_=false;barrierPending_=true;
}
void D3D11Context::prepareSubmit() {
    endRendering();
    for(const auto& query:activeQueries_) endSegment(*query.Get());
}
void D3D11Context::afterSubmit() {
    rendering_=false;barrierPending_=true;
    pipeline_=VK_NULL_HANDLE;layout_=VK_NULL_HANDLE;key_={};
    boundVertexBuffers_.fill(VK_NULL_HANDLE);vertexBindingMask_=0;boundIndexBuffer_=VK_NULL_HANDLE;
    emitted_={};pushedLayout_=VK_NULL_HANDLE;markAllDirty();
    // Queries spanning the submission continue in the next command buffer.
    for(const auto& query:activeQueries_) beginSegment(*query.Get());
}
void D3D11Context::beginRendering() {
    barrier();
    std::array<VkRenderingAttachmentInfo,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> colors{};
    uint32_t colorCount=0;
    VkExtent2D extent{~0u,~0u};bool any=false;
    for(uint32_t i=0;i<colors.size();++i) {
        const auto& target=state_->renderTargets[i];
        auto& info=colors[i];info.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        info.imageLayout=VK_IMAGE_LAYOUT_GENERAL;info.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;info.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        renderingColors_[i]=target?target->view():VK_NULL_HANDLE;
        colorFormats_[i]=target?target->format():VK_FORMAT_UNDEFINED;
        if(!target) continue;
        info.imageView=target->view();colorCount=i+1;any=true;
        extent.width=std::min(extent.width,target->extent().width);extent.height=std::min(extent.height,target->extent().height);
    }
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO},stencil{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    const auto& dsv=state_->depthStencil;
    renderingDepth_=dsv?dsv->view():VK_NULL_HANDLE;
    depthFormat_=dsv?dsv->format():VK_FORMAT_UNDEFINED;
    if(dsv) {
        for(auto* info:{&depth,&stencil}) {
            info->imageView=dsv->view();info->imageLayout=VK_IMAGE_LAYOUT_GENERAL;
            info->loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;info->storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        }
        any=true;
        extent.width=std::min(extent.width,dsv->extent().width);extent.height=std::min(extent.height,dsv->extent().height);
    }
    if(!any) extent={8192,8192};
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea={{0,0},extent};info.layerCount=1;
    info.colorAttachmentCount=colorCount;info.pColorAttachments=colors.data();
    if(dsv && (dsv->aspects()&VK_IMAGE_ASPECT_DEPTH_BIT)) info.pDepthAttachment=&depth;
    if(dsv && (dsv->aspects()&VK_IMAGE_ASPECT_STENCIL_BIT)) info.pStencilAttachment=&stencil;
    vkCmdBeginRendering(commands(),&info);
    rendering_=true;renderArea_=extent;
}
namespace {
// True (and remembered) when value differs from what the command buffer holds.
template<class T> bool changed(std::optional<T>& emitted,const T& value) {
    if(emitted && !std::memcmp(&*emitted,&value,sizeof(T))) return false;
    emitted=value;return true;
}
}
void D3D11Context::applyDynamicState() {
    const VkCommandBuffer command=commands();
    const auto& raster=state_->rasterizer?state_->rasterizer->desc():defaultRaster;
    if(dirty_&DirtyViewport) {
        const auto& v=state_->viewports[0];
        // Negative height reproduces D3D's upward NDC y with the same
        // top-left framebuffer origin and winding (Vulkan 1.1 core).
        const VkViewport viewport{v.TopLeftX,v.TopLeftY+v.Height,v.Width,-v.Height,v.MinDepth,v.MaxDepth};
        if(changed(emitted_.viewport,viewport)) vkCmdSetViewport(command,0,1,&viewport);
    }
    if(dirty_&DirtyScissor) {
        VkRect2D rect{{0,0},renderArea_};
        if(raster.ScissorEnable) {
            if(state_->scissorCount) {
                const auto& s=state_->scissors[0];
                const LONG left=std::max<LONG>(s.left,0),top=std::max<LONG>(s.top,0);
                rect.offset={int32_t(left),int32_t(top)};
                rect.extent={uint32_t(std::max<LONG>(s.right-left,0)),uint32_t(std::max<LONG>(s.bottom-top,0))};
            } else rect.extent={0,0};
        }
        if(changed(emitted_.scissor,rect)) vkCmdSetScissor(command,0,1,&rect);
    }
    if(dirty_&DirtyRaster) {
        const bool bias=raster.DepthBias!=0 || raster.SlopeScaledDepthBias!=0.0f;
        const std::array<uint32_t,3> fixed{uint32_t(cullMode(raster.CullMode)),
            uint32_t(raster.FrontCounterClockwise?VK_FRONT_FACE_COUNTER_CLOCKWISE:VK_FRONT_FACE_CLOCKWISE),uint32_t(bias)};
        if(changed(emitted_.raster,fixed)) {
            vkCmdSetCullMode(command,fixed[0]);vkCmdSetFrontFace(command,VkFrontFace(fixed[1]));vkCmdSetDepthBiasEnable(command,bias);
        }
        const std::array<float,3> values{float(raster.DepthBias),raster.DepthBiasClamp,raster.SlopeScaledDepthBias};
        if(bias && changed(emitted_.bias,values)) vkCmdSetDepthBias(command,values[0],values[1],values[2]);
    }
    if(dirty_&DirtyDepthStencil) {
        const auto& d=state_->depthStencilState?state_->depthStencilState->desc():defaultDepth;
        // Disabled tests may carry zero fields, which D3D11 ignores.
        const std::array<uint32_t,4> depth{uint32_t(d.DepthEnable!=0),uint32_t(d.DepthEnable && d.DepthWriteMask==D3D11_DEPTH_WRITE_MASK_ALL),
            uint32_t(d.DepthEnable?compareOp(d.DepthFunc):VK_COMPARE_OP_ALWAYS),0};
        if(changed(emitted_.depth,depth)) {
            vkCmdSetDepthTestEnable(command,depth[0]);vkCmdSetDepthWriteEnable(command,depth[1]);
            vkCmdSetDepthCompareOp(command,VkCompareOp(depth[2]));vkCmdSetDepthBoundsTestEnable(command,VK_FALSE);
        }
        std::array<uint32_t,11> stencil{uint32_t(d.StencilEnable!=0),VK_STENCIL_OP_KEEP,VK_STENCIL_OP_KEEP,VK_STENCIL_OP_KEEP,VK_COMPARE_OP_ALWAYS,
            VK_STENCIL_OP_KEEP,VK_STENCIL_OP_KEEP,VK_STENCIL_OP_KEEP,VK_COMPARE_OP_ALWAYS,d.StencilReadMask,d.StencilWriteMask};
        if(d.StencilEnable) {
            uint32_t at=1;
            for(const auto* face:{&d.FrontFace,&d.BackFace}) {
                stencil[at++]=stencilOp(face->StencilFailOp);stencil[at++]=stencilOp(face->StencilPassOp);
                stencil[at++]=stencilOp(face->StencilDepthFailOp);stencil[at++]=compareOp(face->StencilFunc);
            }
        }
        if(changed(emitted_.stencil,stencil)) {
            vkCmdSetStencilTestEnable(command,stencil[0]);
            vkCmdSetStencilOp(command,VK_STENCIL_FACE_FRONT_BIT,VkStencilOp(stencil[1]),VkStencilOp(stencil[2]),VkStencilOp(stencil[3]),VkCompareOp(stencil[4]));
            vkCmdSetStencilOp(command,VK_STENCIL_FACE_BACK_BIT,VkStencilOp(stencil[5]),VkStencilOp(stencil[6]),VkStencilOp(stencil[7]),VkCompareOp(stencil[8]));
            vkCmdSetStencilCompareMask(command,VK_STENCIL_FACE_FRONT_AND_BACK,stencil[9]);
            vkCmdSetStencilWriteMask(command,VK_STENCIL_FACE_FRONT_AND_BACK,stencil[10]);
        }
    }
    if((dirty_&DirtyStencilRef) && changed(emitted_.stencilRef,uint32_t(state_->stencilRef&0xFF)))
        vkCmdSetStencilReference(command,VK_STENCIL_FACE_FRONT_AND_BACK,*emitted_.stencilRef);
    if((dirty_&DirtyBlendConstants) && changed(emitted_.blendConstants,state_->blendFactor))
        vkCmdSetBlendConstants(command,state_->blendFactor.data());
    if((dirty_&DirtyTopology) && changed(emitted_.topology,uint32_t(primitiveTopology(state_->topology))))
        vkCmdSetPrimitiveTopology(command,VkPrimitiveTopology(*emitted_.topology));
    dirty_&=~(DirtyViewport|DirtyScissor|DirtyRaster|DirtyDepthStencil|DirtyStencilRef|DirtyBlendConstants|DirtyTopology);
}
// D3D excludes StartVertex/BaseVertex/StartInstance from SV_VertexID and
// SV_InstanceID; Vulkan's VertexIndex/InstanceIndex include them. Folding the
// bases into the buffer offsets gives the D3D values and the same addresses.
void D3D11Context::bindVertexBuffers(INT vertexBase,UINT instanceBase) {
    uint32_t slots=0,instanceSlots=0;
    if(state_->inputLayout) for(const auto& element:state_->inputLayout->elements()) {
        slots|=1u<<element.slot;if(element.instance) instanceSlots|=1u<<element.slot;
    }
    if(!(dirty_&DirtyVertexBuffers) && vertexBase==boundVertexBase_ && instanceBase==boundInstanceBase_) {
        // Renamed dynamic buffers can change storage without a binding change.
        bool same=true;
        for(uint32_t slot=0;slot<32 && same;++slot) if(slots&(1u<<slot)) {
            const auto& buffer=state_->vertexBuffers[slot];
            same=(buffer?buffer->range().buffer:VK_NULL_HANDLE)==boundVertexBuffers_[slot];
        }
        if(same) return;
    }
    // A base vertex reaching below a binding's storage start keeps Vulkan's
    // signed vertexOffset instead (attribute addresses are identical; only
    // SV_VertexID would then include the base).
    bool underflow=false;
    for(uint32_t slot=0;slot<32 && vertexBase<0;++slot) {
        const auto& buffer=state_->vertexBuffers[slot];
        if(!(slots&(1u<<slot)) || !buffer || (instanceSlots&(1u<<slot))) continue;
        underflow|=int64_t(state_->offsets[slot])+int64_t(vertexBase)*int64_t(state_->strides[slot])<0;
    }
    drawVertexOffset_=underflow?vertexBase:0;
    const int64_t foldedBase=underflow?0:vertexBase;
    std::array<VkBuffer,32> buffers{};std::array<VkDeviceSize,32> offsets{},strides{};
    for(uint32_t slot=0;slot<32;++slot) {
        if(!(slots&(1u<<slot))) continue;
        const auto& buffer=state_->vertexBuffers[slot];
        const VkDeviceSize stride=state_->strides[slot];
        if(!buffer) {buffers[slot]=VK_NULL_HANDLE;offsets[slot]=0;strides[slot]=stride;continue;}
        const auto range=buffer->range();
        int64_t offset=int64_t(range.offset)+state_->offsets[slot];
        offset+=(instanceSlots&(1u<<slot))?int64_t(instanceBase)*int64_t(stride):foldedBase*int64_t(stride);
        buffers[slot]=range.buffer;offsets[slot]=VkDeviceSize(offset);strides[slot]=stride;
    }
    const VkCommandBuffer command=commands();
    for(uint32_t slot=0;slot<32;) {
        if(!(slots&(1u<<slot))) {++slot;continue;}
        uint32_t end=slot;
        while(end<32 && (slots&(1u<<end))) ++end;
        vkCmdBindVertexBuffers2(command,slot,end-slot,&buffers[slot],&offsets[slot],nullptr,&strides[slot]);
        slot=end;
    }
    boundVertexBuffers_=buffers;boundVertexOffsets_=offsets;boundVertexStrides_=strides;
    vertexBindingMask_=slots;boundVertexBase_=vertexBase;boundInstanceBase_=instanceBase;
    dirty_&=~DirtyVertexBuffers;
}
void D3D11Context::pushDescriptors(VkPipelineLayout layout) {
    std::array<VkWriteDescriptorSet,64> writes{};
    std::array<VkDescriptorBufferInfo,64> buffers{};
    std::array<VkDescriptorImageInfo,64> images{};
    const VkBufferView nullTexel=VK_NULL_HANDLE;
    size_t i=0;
    for(int pass=0;pass<2;++pass) {
    const SpirvModule* module=pass?(state_->pixelShader?&state_->pixelShader->spirv():nullptr):&state_->vertexShader->spirv();
    if(!module) continue;
    StageBindings& stage=pass?state_->pixel:state_->vertex;
    const uint32_t base=pass?pixelBindingBase:0;
    for(const auto& binding:module->bindings()) {
        if(i==writes.size()) throw Error("Native Vulkan draw binds too many descriptors");
        auto& write=writes[i];
        write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstBinding=base+binding.binding;write.descriptorCount=1;write.descriptorType=binding.type;
        switch(binding.type) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
            const uint32_t slot=binding.binding-constantBufferBinding;
            const auto& buffer=stage.constantBuffers[slot];
            auto& info=buffers[i];
            if(buffer) {
                const auto range=buffer->range();
                const VkDeviceSize size=buffer->desc().ByteWidth,first=VkDeviceSize(stage.firstConstant[slot])*16;
                if(first>=size) {info={VK_NULL_HANDLE,0,VK_WHOLE_SIZE};}
                else {
                    VkDeviceSize length=stage.constantCount[slot]?VkDeviceSize(stage.constantCount[slot])*16:size-first;
                    length=std::min({length,size-first,VkDeviceSize(device_->vulkan().properties().limits.maxUniformBufferRange)});
                    info={range.buffer,range.offset+first,length};
                }
            } else info={VK_NULL_HANDLE,0,VK_WHOLE_SIZE};
            write.pBufferInfo=&info;break;
        }
        case VK_DESCRIPTOR_TYPE_SAMPLER: {
            const auto& sampler=stage.samplers[binding.binding-samplerBinding];
            images[i].sampler=sampler?sampler->sampler():device_->defaultSampler();
            write.pImageInfo=&images[i];break;
        }
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: {
            const auto& view=stage.resources[binding.binding-textureBinding];
            // Textures are single-sample; a multisampled declaration reads as unbound.
            const bool usable=view && !binding.multisampled;
            images[i].imageView=usable?view->view():VK_NULL_HANDLE;images[i].imageLayout=VK_IMAGE_LAYOUT_GENERAL;
            write.pImageInfo=&images[i];break;
        }
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:write.pTexelBufferView=&nullTexel;break;
        default:throw Error("Native Vulkan descriptor type is unsupported");
        }
        ++i;
    }
    }
    if(!i) return;
    // Skip a push identical to the one the command buffer already holds.
    pushing_.clear();
    for(size_t w=0;w<i;++w) {
        const auto& write=writes[w];
        pushing_.push_back(uint64_t(write.dstBinding)<<32|write.descriptorType);
        if(write.pBufferInfo) {
            pushing_.push_back(uint64_t(write.pBufferInfo->buffer));pushing_.push_back(write.pBufferInfo->offset);pushing_.push_back(write.pBufferInfo->range);
        } else if(write.pImageInfo) {
            pushing_.push_back(uint64_t(write.pImageInfo->sampler));pushing_.push_back(uint64_t(write.pImageInfo->imageView));
        }
    }
    if(layout==pushedLayout_ && pushing_==pushed_) return;
    vkCmdPushDescriptorSetKHR(commands(),VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,uint32_t(i),writes.data());
    pushedLayout_=layout;std::swap(pushed_,pushing_);
}
// Opt-in (SIMPSONS_VULKAN_TRACE=1) per-draw state log for fixture diagnosis.
static bool traceDraws() {
    static const bool enabled=[]{const char* v=std::getenv("SIMPSONS_VULKAN_TRACE");return v && *v=='1';}();
    return enabled;
}
static void traceDraw(const ContextState& s,bool indexed,INT vertexBase,const char* outcome) {
    const auto& v=s.viewports[0];const auto& sc=s.scissors[0];
    const auto& r=s.rasterizer?s.rasterizer->desc():defaultRaster;
    const auto& d=s.depthStencilState?s.depthStencilState->desc():defaultDepth;
    std::fprintf(stderr,"[VULKAN DRAW] %s vs=%s ps=%s indexed=%d base=%d topo=%d vp=%u:(%.1f,%.1f %.1fx%.1f z%.2f-%.2f) sc=%u:(%ld,%ld,%ld,%ld) "
        "raster(cull=%d ccw=%d clip=%d scissor=%d bias=%d) depth(en=%d wr=%d fn=%d st=%d) rt0=%p ds=%p blend=%p\n",outcome,
        s.vertexShader?s.vertexShader->spirv().name().c_str():"-",s.pixelShader?s.pixelShader->spirv().name().c_str():"-",
        int(indexed),vertexBase,int(s.topology),s.viewportCount,v.TopLeftX,v.TopLeftY,v.Width,v.Height,v.MinDepth,v.MaxDepth,
        s.scissorCount,sc.left,sc.top,sc.right,sc.bottom,int(r.CullMode),int(r.FrontCounterClockwise),int(r.DepthClipEnable),int(r.ScissorEnable),
        int(r.DepthBias),int(d.DepthEnable),int(d.DepthWriteMask),int(d.DepthFunc),int(d.StencilEnable),
        static_cast<void*>(s.renderTargets[0].Get()),static_cast<void*>(s.depthStencil.Get()),static_cast<void*>(s.blend.Get()));
    for(const auto* stage:{&s.vertex,&s.pixel}) for(uint32_t slot=0;slot<4;++slot) {
        const auto& buffer=stage->constantBuffers[slot];
        if(!buffer) continue;
        const float* data=reinterpret_cast<const float*>(buffer->hostData());
        const uint32_t first=stage->firstConstant[slot]*4;
        std::fprintf(stderr,"  %s cb%u bytes=%u first=%u count=%u data=",stage==&s.vertex?"vs":"ps",slot,buffer->desc().ByteWidth,
            stage->firstConstant[slot],stage->constantCount[slot]);
        if(data) for(uint32_t i=0;i<8;++i) std::fprintf(stderr,"%g ",data[first+i]);
        std::fprintf(stderr,"\n");
    }
}
bool D3D11Context::prepareDraw(bool indexed,INT vertexBase,UINT instanceBase) {
    if(traceDraws()) traceDraw(*state_,indexed,vertexBase,"draw");
    requireUnpredicated("draws");
    for(const auto& target:state_->streamOutputs) if(target) throw Error("Native Vulkan does not implement stream output");
    if(!state_->vertexShader) {reportUnsupported("draw without a vertex shader (skipped as D3D11 does)");return false;}
    if(!state_->viewportCount || state_->viewports[0].Width<=0 || state_->viewports[0].Height<=0) return false;
    const uint8_t topology=topologyClass(state_->topology);
    if(!topology) throw Error("Native Vulkan draw has an undefined or unsupported topology");
    if(indexed && !state_->indexBuffer) return false;
    // Attachment changes end the current dynamic rendering scope.
    if(rendering_) {
        bool same=(state_->depthStencil?state_->depthStencil->view():VK_NULL_HANDLE)==renderingDepth_;
        for(uint32_t i=0;i<renderingColors_.size() && same;++i)
            same=(state_->renderTargets[i]?state_->renderTargets[i]->view():VK_NULL_HANDLE)==renderingColors_[i];
        if(!same) endRendering();
    }
    if(!rendering_) {beginRendering();dirty_|=DirtyScissor;}
    const auto& raster=state_->rasterizer?state_->rasterizer->desc():defaultRaster;
    PipelineKey key;
    key.vertex=state_->vertexShader->id();key.pixel=state_->pixelShader?state_->pixelShader->id():0;
    key.layout=state_->inputLayout?state_->inputLayout->id():0;key.blend=state_->blend?state_->blend->id():0;
    key.sampleMask=state_->sampleMask;key.topologyClass=topology;
    key.wireframe=raster.FillMode==D3D11_FILL_WIREFRAME;key.depthClip=raster.DepthClipEnable?1:0;
    key.colors=colorFormats_;key.depth=depthFormat_;
    const VkCommandBuffer command=commands();
    if(!pipeline_ || !(key==key_)) {
        const auto entry=device_->pipelines().get(key,*state_);
        if(entry.pipeline!=pipeline_) vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,entry.pipeline);
        if(entry.layout!=layout_) dirty_|=DirtyVertexDescriptors|DirtyPixelDescriptors;
        if(key.vertex!=key_.vertex) dirty_|=DirtyVertexDescriptors;
        if(key.pixel!=key_.pixel) dirty_|=DirtyPixelDescriptors;
        if(key.layout!=key_.layout) dirty_|=DirtyVertexBuffers;
        pipeline_=entry.pipeline;layout_=entry.layout;key_=key;
    }
    dirty_&=~DirtyPipeline;
    applyDynamicState();
    const bool strip=state_->topology==D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP || state_->topology==D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
    // D3D10+ always treats the all-ones index as a strip cut; lists never cut.
    const bool restart=indexed && strip;
    if(changed(emitted_.restart,uint32_t(restart))) vkCmdSetPrimitiveRestartEnable(command,restart);
    bindVertexBuffers(vertexBase,instanceBase);
    if(indexed) {
        const auto range=state_->indexBuffer->range();
        const VkIndexType type=state_->indexFormat==DXGI_FORMAT_R32_UINT?VK_INDEX_TYPE_UINT32:VK_INDEX_TYPE_UINT16;
        const VkDeviceSize offset=range.offset+state_->indexOffset;
        if(range.buffer!=boundIndexBuffer_ || offset!=boundIndexOffset_ || type!=boundIndexType_ || (dirty_&DirtyIndexBuffer)) {
            vkCmdBindIndexBuffer(command,range.buffer,offset,type);
            boundIndexBuffer_=range.buffer;boundIndexOffset_=offset;boundIndexType_=type;
        }
        dirty_&=~DirtyIndexBuffer;
    }
    // Renamed constant buffers change storage under an unchanged binding;
    // descriptors are pushed for every draw, which is cheap with push sets.
    pushDescriptors(layout_);
    dirty_&=~(DirtyVertexDescriptors|DirtyPixelDescriptors);
    return true;
}

// ---------------------------------------------------------------- draws
void D3D11Context::Draw(UINT count,UINT start) {
    if(deferred_) {record([count,start](D3D11Context& c){c.Draw(count,start);});return;}
    if(!count || !prepareDraw(false,INT(start),0)) return;
    vkCmdDraw(commands(),count,1,0,0);
}
void D3D11Context::DrawIndexed(UINT count,UINT start,INT base) {
    if(deferred_) {record([count,start,base](D3D11Context& c){c.DrawIndexed(count,start,base);});return;}
    if(!count || !prepareDraw(true,base,0)) return;
    vkCmdDrawIndexed(commands(),count,1,start,drawVertexOffset_,0);
}
void D3D11Context::DrawInstanced(UINT count,UINT instances,UINT start,UINT firstInstance) {
    if(deferred_) {record([=](D3D11Context& c){c.DrawInstanced(count,instances,start,firstInstance);});return;}
    if(!count || !instances || !prepareDraw(false,INT(start),firstInstance)) return;
    vkCmdDraw(commands(),count,instances,0,0);
}
void D3D11Context::DrawIndexedInstanced(UINT count,UINT instances,UINT start,INT base,UINT firstInstance) {
    if(deferred_) {record([=](D3D11Context& c){c.DrawIndexedInstanced(count,instances,start,base,firstInstance);});return;}
    if(!count || !instances || !prepareDraw(true,base,firstInstance)) return;
    vkCmdDrawIndexed(commands(),count,instances,start,drawVertexOffset_,0);
}
void D3D11Context::DrawAuto() {requireNull(this,"DrawAuto");}
void D3D11Context::DrawIndexedInstancedIndirect(ID3D11Buffer*,UINT) {requireNull(this,"indirect draws");}
void D3D11Context::DrawInstancedIndirect(ID3D11Buffer*,UINT) {requireNull(this,"indirect draws");}
void D3D11Context::Dispatch(UINT,UINT,UINT) {requireNull(this,"compute dispatch");}
void D3D11Context::DispatchIndirect(ID3D11Buffer*,UINT) {requireNull(this,"compute dispatch");}

// ---------------------------------------------------------------- clears
void D3D11Context::clearAttachment(VkImageView view,VkExtent2D extent,VkFormat format,VkImageAspectFlags aspects,
    const VkClearValue& value,VkImageAspectFlags clearAspects) {
    requireUnpredicated("clears");
    endRendering();barrier();
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO},depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO},
        stencil{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea={{0,0},extent};info.layerCount=1;
    (void)format;
    if(aspects&VK_IMAGE_ASPECT_COLOR_BIT) {
        color.imageView=view;color.imageLayout=VK_IMAGE_LAYOUT_GENERAL;color.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;color.clearValue=value;
        info.colorAttachmentCount=1;info.pColorAttachments=&color;
    }
    if(aspects&VK_IMAGE_ASPECT_DEPTH_BIT) {
        depth.imageView=view;depth.imageLayout=VK_IMAGE_LAYOUT_GENERAL;depth.storeOp=VK_ATTACHMENT_STORE_OP_STORE;depth.clearValue=value;
        depth.loadOp=(clearAspects&VK_IMAGE_ASPECT_DEPTH_BIT)?VK_ATTACHMENT_LOAD_OP_CLEAR:VK_ATTACHMENT_LOAD_OP_LOAD;
        info.pDepthAttachment=&depth;
    }
    if(aspects&VK_IMAGE_ASPECT_STENCIL_BIT) {
        stencil.imageView=view;stencil.imageLayout=VK_IMAGE_LAYOUT_GENERAL;stencil.storeOp=VK_ATTACHMENT_STORE_OP_STORE;stencil.clearValue=value;
        stencil.loadOp=(clearAspects&VK_IMAGE_ASPECT_STENCIL_BIT)?VK_ATTACHMENT_LOAD_OP_CLEAR:VK_ATTACHMENT_LOAD_OP_LOAD;
        info.pStencilAttachment=&stencil;
    }
    const VkCommandBuffer command=commands();
    vkCmdBeginRendering(command,&info);vkCmdEndRendering(command);
    barrierPending_=true;
}
void D3D11Context::ClearRenderTargetView(ID3D11RenderTargetView* target,const FLOAT color[4]) {
    if(deferred_) {
        record([object=ComPtr<ID3D11RenderTargetView>(target),c=std::array<float,4>{color[0],color[1],color[2],color[3]}](D3D11Context& context){
            context.ClearRenderTargetView(object.Get(),c.data());});
        return;
    }
    auto* view=unwrap<D3D11RenderTargetView>(target);
    if(!view) return;
    VkClearValue value{};
    if(view->viewFormat().integer) {
        // D3D11 converts the float clear value to the integer view format.
        for(int i=0;i<4;++i) {
            if(view->viewFormat().signedInteger) value.color.int32[i]=int32_t(color[i]);
            else value.color.uint32[i]=color[i]>0?uint32_t(color[i]):0u;
        }
    } else std::memcpy(value.color.float32,color,sizeof(float)*4);
    clearAttachment(view->view(),view->extent(),view->format(),VK_IMAGE_ASPECT_COLOR_BIT,value,VK_IMAGE_ASPECT_COLOR_BIT);
}
void D3D11Context::ClearDepthStencilView(ID3D11DepthStencilView* target,UINT flags,FLOAT depth,UINT8 stencil) {
    if(deferred_) {
        record([object=ComPtr<ID3D11DepthStencilView>(target),flags,depth,stencil](D3D11Context& c){c.ClearDepthStencilView(object.Get(),flags,depth,stencil);});
        return;
    }
    auto* view=unwrap<D3D11DepthStencilView>(target);
    if(!view || !flags) return;
    VkImageAspectFlags clear=0;
    if(flags&D3D11_CLEAR_DEPTH) clear|=VK_IMAGE_ASPECT_DEPTH_BIT;
    if(flags&D3D11_CLEAR_STENCIL) clear|=VK_IMAGE_ASPECT_STENCIL_BIT;
    clear&=view->aspects();
    if(!clear) return;
    VkClearValue value{};value.depthStencil={depth,stencil};
    clearAttachment(view->view(),view->extent(),view->format(),view->aspects(),value,clear);
}
void D3D11Context::ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView*,const UINT[4]) {requireNull(this,"unordered access view clears");}
void D3D11Context::ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView*,const FLOAT[4]) {requireNull(this,"unordered access view clears");}
void D3D11Context::ClearView(ID3D11View*,const FLOAT[4],const D3D11_RECT*,UINT) {requireNull(this,"ClearView");}
void D3D11Context::DiscardResource(ID3D11Resource*) {}
void D3D11Context::DiscardView(ID3D11View*) {}
void D3D11Context::DiscardView1(ID3D11View*,const D3D11_RECT*,UINT) {}
void D3D11Context::GenerateMips(ID3D11ShaderResourceView*) {requireNull(this,"GenerateMips");}
void D3D11Context::SetResourceMinLOD(ID3D11Resource*,FLOAT lod) {if(lod!=0.0f) requireNull(this,"resource minimum LOD");}
FLOAT D3D11Context::GetResourceMinLOD(ID3D11Resource*) {return 0.0f;}
// No shader appends or consumes, so the hidden counter is the last initial
// count supplied when the view was bound.
void D3D11Context::CopyStructureCount(ID3D11Buffer* destination,UINT offset,ID3D11UnorderedAccessView* source) {
    if(deferred_) {
        record([to=ComPtr<ID3D11Buffer>(destination),offset,from=ComPtr<ID3D11UnorderedAccessView>(source)](D3D11Context& c){
            c.CopyStructureCount(to.Get(),offset,from.Get());});
        return;
    }
    auto* buffer=unwrap<D3D11Buffer>(destination);auto* view=unwrap<D3D11UnorderedAccessView>(source);
    if(!buffer || !view || (offset&3)) throw Error("Native Vulkan CopyStructureCount arguments are invalid");
    const uint32_t value=view->counter;
    updateBuffer(*buffer,offset,sizeof(value),&value);
}

// ---------------------------------------------------------------- copies and updates
void D3D11Context::copyTexture(D3D11Texture2D& destination,UINT destinationSub,UINT x,UINT y,D3D11Texture2D& source,UINT sourceSub,const D3D11_BOX* box) {
    const auto to=split(destination,destinationSub),from=split(source,sourceSub);
    const uint32_t sourceWidth=source.mipWidth(from.mip),sourceHeight=source.mipHeight(from.mip);
    VkOffset3D sourceOffset{0,0,0};VkExtent3D extent{sourceWidth,sourceHeight,1};
    if(box) {
        if(box->right<=box->left || box->bottom<=box->top || box->back<=box->front) return;
        if(box->right>sourceWidth || box->bottom>sourceHeight || box->front || box->back!=1) throw Error("Native Vulkan copy box is invalid");
        sourceOffset={int32_t(box->left),int32_t(box->top),0};extent={box->right-box->left,box->bottom-box->top,1};
    }
    requireUnpredicated("copies");
    const uint32_t destinationWidth=destination.mipWidth(to.mip),destinationHeight=destination.mipHeight(to.mip);
    // Block-compressed edges may cover the physical block beyond the extent.
    const auto& sf=source.format();
    if(x+extent.width>destinationWidth || y+extent.height>destinationHeight) {
        if(!sf.compressed) throw Error("Native Vulkan copy exceeds its destination");
        extent.width=std::min(extent.width,destinationWidth-x);extent.height=std::min(extent.height,destinationHeight-y);
    }
    endRendering();barrier();
    const VkCommandBuffer command=commands();
    const VkImageAspectFlags aspects=sf.aspects;
    const uint64_t serial=device_->vulkan().recordingSerial();
    if(!destination.isStaging() && !source.isStaging()) {
        VkImageCopy region{};
        region.srcSubresource={aspects,from.mip,from.layer,1};region.srcOffset=sourceOffset;
        region.dstSubresource={destination.format().aspects,to.mip,to.layer,1};region.dstOffset={int32_t(x),int32_t(y),0};
        region.extent=extent;
        vkCmdCopyImage(command,source.image(),VK_IMAGE_LAYOUT_GENERAL,destination.image(),VK_IMAGE_LAYOUT_GENERAL,1,&region);
    } else if(destination.isStaging() && !source.isStaging()) {
        const auto& layout=destination.layout(destinationSub);
        const bool combined=sf.depth() && sf.stencil();
        std::array<VkBufferImageCopy,2> regions{};uint32_t count=0;
        if(combined) {
            const VkDeviceSize texel=VkDeviceSize(y)*layout.width+x;
            regions[count++]={layout.offset+texel*4,layout.width,0,{VK_IMAGE_ASPECT_DEPTH_BIT,from.mip,from.layer,1},sourceOffset,extent};
            regions[count++]={layout.stencilOffset+texel,layout.width,0,{VK_IMAGE_ASPECT_STENCIL_BIT,from.mip,from.layer,1},sourceOffset,extent};
        } else {
            const uint32_t bx=x/sf.blockWidth,by=y/sf.blockHeight;
            const uint32_t rowTexels=layout.rowPitch/sf.blockBytes*sf.blockWidth;
            regions[count++]={layout.offset+VkDeviceSize(by)*layout.rowPitch+VkDeviceSize(bx)*sf.blockBytes,rowTexels,0,
                {aspects,from.mip,from.layer,1},sourceOffset,extent};
        }
        vkCmdCopyImageToBuffer(command,source.image(),VK_IMAGE_LAYOUT_GENERAL,destination.staging().buffer,count,regions.data());
        destination.gpuWrite=serial;
    } else if(!destination.isStaging() && source.isStaging()) {
        const auto& layout=source.layout(sourceSub);
        std::array<VkBufferImageCopy,2> regions{};uint32_t count=0;
        if(sf.depth() && sf.stencil()) {
            const VkDeviceSize texel=VkDeviceSize(sourceOffset.y)*layout.width+uint32_t(sourceOffset.x);
            regions[count++]={layout.offset+texel*4,layout.width,0,{VK_IMAGE_ASPECT_DEPTH_BIT,to.mip,to.layer,1},{int32_t(x),int32_t(y),0},extent};
            regions[count++]={layout.stencilOffset+texel,layout.width,0,{VK_IMAGE_ASPECT_STENCIL_BIT,to.mip,to.layer,1},{int32_t(x),int32_t(y),0},extent};
        } else {
            const uint32_t bx=uint32_t(sourceOffset.x)/sf.blockWidth,by=uint32_t(sourceOffset.y)/sf.blockHeight;
            const uint32_t rowTexels=layout.rowPitch/sf.blockBytes*sf.blockWidth;
            regions[count++]={layout.offset+VkDeviceSize(by)*layout.rowPitch+VkDeviceSize(bx)*sf.blockBytes,rowTexels,0,
                {destination.format().aspects,to.mip,to.layer,1},{int32_t(x),int32_t(y),0},extent};
        }
        vkCmdCopyBufferToImage(command,source.staging().buffer,destination.image(),VK_IMAGE_LAYOUT_GENERAL,count,regions.data());
        source.gpuRead=serial;
    } else throw Error("Native Vulkan staging-to-staging texture copies are unsupported");
    barrierPending_=true;
}
void D3D11Context::copyBuffer(D3D11Buffer& destination,UINT offset,D3D11Buffer& source,UINT sourceOffset,UINT size) {
    if(!size) return;
    requireUnpredicated("copies");
    if(offset+uint64_t(size)>destination.desc().ByteWidth || sourceOffset+uint64_t(size)>source.desc().ByteWidth)
        throw Error("Native Vulkan buffer copy is out of range");
    auto& vulkan=device_->vulkan();
    if(destination.storage()==D3D11Buffer::Storage::Renamed) {
        if(source.storage()==D3D11Buffer::Storage::Renamed && source.cpuCurrent()) {
            // Both sides hold host bytes current at this point of the timeline.
            const bool whole=offset==0 && size==destination.desc().ByteWidth;
            if(whole) destination.rename(vulkan.recordingSerial()); else destination.renamePreserving(vulkan.recordingSerial());
            std::memmove(destination.hostData()+offset,source.hostData()+sourceOffset,size);
            return;
        }
        destination.renamePreserving(vulkan.recordingSerial());
        destination.setCpuCurrent(false);
    }
    endRendering();barrier();
    const auto to=destination.range(),from=source.range();
    const VkBufferCopy region{from.offset+sourceOffset,to.offset+offset,size};
    vkCmdCopyBuffer(commands(),from.buffer,to.buffer,1,&region);
    if(destination.storage()==D3D11Buffer::Storage::Staging) destination.gpuWrite=vulkan.recordingSerial();
    if(source.storage()==D3D11Buffer::Storage::Staging) source.gpuRead=vulkan.recordingSerial();
    barrierPending_=true;
}
void D3D11Context::CopyResource(ID3D11Resource* destination,ID3D11Resource* source) {
    if(deferred_) {
        record([to=ComPtr<ID3D11Resource>(destination),from=ComPtr<ID3D11Resource>(source)](D3D11Context& c){c.CopyResource(to.Get(),from.Get());});
        return;
    }
    if(!destination || !source || destination==source) throw Error("Native Vulkan CopyResource needs two distinct resources");
    D3D11_RESOURCE_DIMENSION a{},b{};destination->GetType(&a);source->GetType(&b);
    if(a!=b) throw Error("Native Vulkan CopyResource dimensions differ");
    if(a==D3D11_RESOURCE_DIMENSION_BUFFER) {
        auto* to=unwrap<D3D11Buffer>(destination);auto* from=unwrap<D3D11Buffer>(source);
        if(to->desc().ByteWidth!=from->desc().ByteWidth) throw Error("Native Vulkan CopyResource buffer sizes differ");
        copyBuffer(*to,0,*from,0,to->desc().ByteWidth);
        return;
    }
    auto* to=unwrap<D3D11Texture2D>(destination);auto* from=unwrap<D3D11Texture2D>(source);
    if(!to || !from) throw Error("Native Vulkan CopyResource supports buffers and 2D textures only");
    const auto& td=to->desc();const auto& fd=from->desc();
    if(td.Width!=fd.Width || td.Height!=fd.Height || td.MipLevels!=fd.MipLevels || td.ArraySize!=fd.ArraySize ||
       !sameFamily(td.Format,fd.Format))
        throw Error("Native Vulkan CopyResource descriptors are incompatible");
    for(UINT sub=0;sub<to->subresources();++sub) copyTexture(*to,sub,0,0,*from,sub,nullptr);
}
void D3D11Context::CopySubresourceRegion(ID3D11Resource* destination,UINT destinationSub,UINT x,UINT y,UINT z,
    ID3D11Resource* source,UINT sourceSub,const D3D11_BOX* box) {
    if(deferred_) {
        std::optional<D3D11_BOX> copy;if(box) copy=*box;
        record([to=ComPtr<ID3D11Resource>(destination),from=ComPtr<ID3D11Resource>(source),destinationSub,x,y,z,sourceSub,copy](D3D11Context& c){
            c.CopySubresourceRegion(to.Get(),destinationSub,x,y,z,from.Get(),sourceSub,copy?&*copy:nullptr);});
        return;
    }
    if(!destination || !source) throw Error("Native Vulkan CopySubresourceRegion needs resources");
    D3D11_RESOURCE_DIMENSION a{},b{};destination->GetType(&a);source->GetType(&b);
    if(a!=b) throw Error("Native Vulkan CopySubresourceRegion dimensions differ");
    if(a==D3D11_RESOURCE_DIMENSION_BUFFER) {
        auto* to=unwrap<D3D11Buffer>(destination);auto* from=unwrap<D3D11Buffer>(source);
        const UINT begin=box?box->left:0,end=box?box->right:from->desc().ByteWidth;
        if(destinationSub || sourceSub || y || z || end<begin) throw Error("Native Vulkan buffer region copy is invalid");
        copyBuffer(*to,x,*from,begin,end-begin);
        return;
    }
    if(z) throw Error("Native Vulkan 2D copies have no depth offset");
    auto* to=unwrap<D3D11Texture2D>(destination);auto* from=unwrap<D3D11Texture2D>(source);
    if(!to || !from) throw Error("Native Vulkan CopySubresourceRegion supports buffers and 2D textures only");
    if(!sameFamily(to->desc().Format,from->desc().Format)) throw Error("Native Vulkan region copy formats are incompatible");
    copyTexture(*to,destinationSub,x,y,*from,sourceSub,box);
}
void D3D11Context::CopySubresourceRegion1(ID3D11Resource* destination,UINT destinationSub,UINT x,UINT y,UINT z,
    ID3D11Resource* source,UINT sourceSub,const D3D11_BOX* box,UINT) {
    CopySubresourceRegion(destination,destinationSub,x,y,z,source,sourceSub,box);
}
void D3D11Context::updateBuffer(D3D11Buffer& buffer,UINT offset,UINT size,const void* data) {
    if(offset+uint64_t(size)>buffer.desc().ByteWidth) throw Error("Native Vulkan buffer update is out of range");
    if(!size) return;
    auto& vulkan=device_->vulkan();
    if(buffer.storage()==D3D11Buffer::Storage::Renamed) {
        // Orphan the storage: earlier commands keep their bytes, no GPU work.
        if(offset==0 && size==buffer.desc().ByteWidth) buffer.rename(vulkan.recordingSerial());
        else buffer.renamePreserving(vulkan.recordingSerial());
        std::memcpy(buffer.hostData()+offset,data,size);
        return;
    }
    if(buffer.storage()!=D3D11Buffer::Storage::Device) throw Error("Native Vulkan UpdateSubresource targets DEFAULT resources only");
    const HostSlice staging=vulkan.allocateUpload(size,16);
    std::memcpy(staging.data,data,size);
    endRendering();barrier();
    const VkBufferCopy region{staging.offset,offset,size};
    vkCmdCopyBuffer(commands(),staging.buffer,buffer.range().buffer,1,&region);
    barrierPending_=true;
}
void D3D11Context::updateTexture(D3D11Texture2D& texture,UINT subresource,const D3D11_BOX* box,const void* data,UINT rowPitch) {
    if(texture.isStaging()) throw Error("Native Vulkan UpdateSubresource cannot target staging textures");
    const auto& format=texture.format();
    if(format.depth()||format.stencil()) throw Error("Native Vulkan depth/stencil UpdateSubresource is unsupported");
    const auto sub=split(texture,subresource);
    const uint32_t width=texture.mipWidth(sub.mip),height=texture.mipHeight(sub.mip);
    VkOffset3D offset{0,0,0};VkExtent3D extent{width,height,1};
    if(box) {
        if(box->right<=box->left || box->bottom<=box->top) return;
        if(box->right>width || box->bottom>height || box->front || box->back!=1) throw Error("Native Vulkan update box is invalid");
        offset={int32_t(box->left),int32_t(box->top),0};extent={box->right-box->left,box->bottom-box->top,1};
    }
    const uint32_t blocksWide=(extent.width+format.blockWidth-1)/format.blockWidth;
    const uint32_t blocksHigh=(extent.height+format.blockHeight-1)/format.blockHeight;
    const uint32_t rowBytes=blocksWide*format.blockBytes;
    auto& vulkan=device_->vulkan();
    const HostSlice staging=vulkan.allocateUpload(VkDeviceSize(rowBytes)*blocksHigh,16);
    for(uint32_t row=0;row<blocksHigh;++row)
        std::memcpy(staging.data+size_t(row)*rowBytes,static_cast<const uint8_t*>(data)+size_t(row)*rowPitch,rowBytes);
    endRendering();barrier();
    const VkBufferImageCopy region{staging.offset,0,0,{format.aspects,sub.mip,sub.layer,1},offset,extent};
    vkCmdCopyBufferToImage(commands(),staging.buffer,texture.image(),VK_IMAGE_LAYOUT_GENERAL,1,&region);
    barrierPending_=true;
}
void D3D11Context::UpdateSubresource(ID3D11Resource* destination,UINT subresource,const D3D11_BOX* box,const void* data,UINT rowPitch,UINT depthPitch) {
    if(!destination || !data) return;
    D3D11_RESOURCE_DIMENSION dimension{};destination->GetType(&dimension);
    if(deferred_) {
        // Snapshot the caller's bytes now; D3D11 copies them before returning.
        std::vector<uint8_t> bytes;std::optional<D3D11_BOX> copy;if(box) copy=*box;
        UINT pitch=rowPitch;
        if(dimension==D3D11_RESOURCE_DIMENSION_BUFFER) {
            auto* buffer=unwrap<D3D11Buffer>(destination);
            const UINT size=box?box->right-box->left:buffer->desc().ByteWidth;
            bytes.assign(static_cast<const uint8_t*>(data),static_cast<const uint8_t*>(data)+size);
        } else {
            auto* texture=unwrap<D3D11Texture2D>(destination);
            const auto sub=split(*texture,subresource);
            const uint32_t height=box?box->bottom-box->top:texture->mipHeight(sub.mip);
            const uint32_t rows=(height+texture->format().blockHeight-1)/texture->format().blockHeight;
            bytes.assign(static_cast<const uint8_t*>(data),static_cast<const uint8_t*>(data)+size_t(rows)*rowPitch);
        }
        record([object=ComPtr<ID3D11Resource>(destination),subresource,copy,bytes=std::move(bytes),pitch,depthPitch](D3D11Context& c){
            c.UpdateSubresource(object.Get(),subresource,copy?&*copy:nullptr,bytes.data(),pitch,depthPitch);});
        return;
    }
    if(dimension==D3D11_RESOURCE_DIMENSION_BUFFER) {
        auto* buffer=unwrap<D3D11Buffer>(destination);
        if(subresource) throw Error("Native Vulkan buffer subresource must be zero");
        const UINT offset=box?box->left:0,size=box?box->right-box->left:buffer->desc().ByteWidth;
        updateBuffer(*buffer,offset,size,data);
        return;
    }
    auto* texture=unwrap<D3D11Texture2D>(destination);
    if(!texture) throw Error("Native Vulkan UpdateSubresource supports buffers and 2D textures only");
    updateTexture(*texture,subresource,box,data,rowPitch);
}
void D3D11Context::UpdateSubresource1(ID3D11Resource* destination,UINT subresource,const D3D11_BOX* box,const void* data,UINT rowPitch,UINT depthPitch,UINT) {
    UpdateSubresource(destination,subresource,box,data,rowPitch,depthPitch);
}
// Every Vulkan texture is single-sample, so there is never anything to resolve.
void D3D11Context::ResolveSubresource(ID3D11Resource*,UINT,ID3D11Resource*,UINT,DXGI_FORMAT) {requireNull(this,"multisample resolves");}

// ---------------------------------------------------------------- map/unmap
HRESULT D3D11Context::waitForStaging(uint64_t serial,UINT flags) {
    auto& vulkan=device_->vulkan();
    if(!serial || vulkan.complete(serial)) return S_OK;
    if(serial>vulkan.submittedSerial()) Flush();
    if(flags&D3D11_MAP_FLAG_DO_NOT_WAIT) return vulkan.complete(serial)?S_OK:DXGI_ERROR_WAS_STILL_DRAWING;
    if(!vulkan.wait(serial,5000000000ull)) throw Error("Native Vulkan staging map waited more than 5000 ms for the GPU");
    return S_OK;
}
HRESULT D3D11Context::Map(ID3D11Resource* resource,UINT subresource,D3D11_MAP type,UINT flags,D3D11_MAPPED_SUBRESOURCE* mapped) {
    if(!resource || !mapped) return E_INVALIDARG;
    *mapped={};
    D3D11_RESOURCE_DIMENSION dimension{};resource->GetType(&dimension);
    if(dimension==D3D11_RESOURCE_DIMENSION_BUFFER) {
        auto* buffer=unwrap<D3D11Buffer>(resource);
        if(subresource) return E_INVALIDARG;
        const UINT size=buffer->desc().ByteWidth;
        if(buffer->desc().Usage==D3D11_USAGE_DYNAMIC) {
            if(deferred_) {
                if(type!=D3D11_MAP_WRITE_DISCARD) return E_INVALIDARG;
                auto& scratch=deferredMaps_[resource];scratch.assign(size,0);
                mapped->pData=scratch.data();mapped->RowPitch=mapped->DepthPitch=size;return S_OK;
            }
            if(type==D3D11_MAP_WRITE_DISCARD) buffer->rename(device_->vulkan().recordingSerial());
            else if(type!=D3D11_MAP_WRITE_NO_OVERWRITE) return E_INVALIDARG;
            mapped->pData=buffer->hostData();mapped->RowPitch=mapped->DepthPitch=size;return S_OK;
        }
        if(buffer->storage()!=D3D11Buffer::Storage::Staging || deferred_) return E_INVALIDARG;
        const bool reads=type==D3D11_MAP_READ || type==D3D11_MAP_READ_WRITE;
        const HRESULT wait=waitForStaging(reads?std::max(buffer->gpuWrite,buffer->gpuRead):buffer->gpuRead,flags);
        if(wait!=S_OK) return wait;
        device_->vulkan().invalidate(buffer->staging());
        mapped->pData=buffer->hostData();mapped->RowPitch=mapped->DepthPitch=size;return S_OK;
    }
    auto* texture=unwrap<D3D11Texture2D>(resource);
    if(!texture || !texture->isStaging() || deferred_) return E_INVALIDARG;
    if(subresource>=texture->subresources()) return E_INVALIDARG;
    const bool reads=type==D3D11_MAP_READ || type==D3D11_MAP_READ_WRITE;
    const HRESULT wait=waitForStaging(reads?std::max(texture->gpuWrite,texture->gpuRead):texture->gpuRead,flags);
    if(wait!=S_OK) return wait;
    device_->vulkan().invalidate(texture->staging());
    const auto& layout=texture->layout(subresource);
    const auto& format=texture->format();
    if(format.depth() && format.stencil()) {
        // Interleave the depth and stencil planes into D3D's texel layout.
        size_t base=0;for(UINT s=0;s<subresource;++s) base+=texture->layout(s).depthPitch;
        uint8_t* out=texture->shadow().data()+base;
        const uint8_t* depth=texture->staging().data+layout.offset;const uint8_t* stencil=texture->staging().data+layout.stencilOffset;
        const uint32_t texelBytes=format.blockBytes;
        for(uint32_t row=0;row<layout.height;++row) for(uint32_t column=0;column<layout.width;++column) {
            const size_t texel=size_t(row)*layout.width+column;uint8_t* destination=out+size_t(row)*layout.rowPitch+size_t(column)*texelBytes;
            std::memset(destination,0,texelBytes);
            if(texelBytes==8) {std::memcpy(destination,depth+texel*4,4);destination[4]=stencil[texel];}
            else {uint32_t value=0;std::memcpy(&value,depth+texel*4,4);value=(value&0xFFFFFFu)|(uint32_t(stencil[texel])<<24);std::memcpy(destination,&value,4);}
        }
        mapped->pData=out;
    } else mapped->pData=texture->staging().data+layout.offset;
    mapped->RowPitch=layout.rowPitch;mapped->DepthPitch=layout.depthPitch;
    texture->mappedType=type;texture->mappedSubresource=subresource;
    return S_OK;
}
void D3D11Context::Unmap(ID3D11Resource* resource,UINT subresource) {
    if(!resource) return;
    D3D11_RESOURCE_DIMENSION dimension{};resource->GetType(&dimension);
    if(deferred_) {
        auto found=deferredMaps_.find(resource);
        if(found==deferredMaps_.end()) return;
        record([object=ComPtr<ID3D11Resource>(resource),bytes=std::move(found->second)](D3D11Context& c){
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if(FAILED(c.Map(object.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) throw Error("Native Vulkan deferred map replay failed");
            std::memcpy(mapped.pData,bytes.data(),bytes.size());c.Unmap(object.Get(),0);
        });
        deferredMaps_.erase(found);
        return;
    }
    if(dimension==D3D11_RESOURCE_DIMENSION_BUFFER) {
        auto* buffer=unwrap<D3D11Buffer>(resource);
        if(buffer->storage()==D3D11Buffer::Storage::Staging) device_->vulkan().flush(buffer->staging());
        return;
    }
    auto* texture=unwrap<D3D11Texture2D>(resource);
    if(!texture || !texture->isStaging()) return;
    const bool wrote=texture->mappedType==D3D11_MAP_WRITE || texture->mappedType==D3D11_MAP_READ_WRITE;
    const auto& format=texture->format();
    if(wrote && format.depth() && format.stencil()) {
        // Split the interleaved D3D texels back into the copy planes.
        const UINT sub=texture->mappedSubresource;const auto& layout=texture->layout(sub);
        size_t base=0;for(UINT s=0;s<sub;++s) base+=texture->layout(s).depthPitch;
        const uint8_t* in=texture->shadow().data()+base;
        uint8_t* depth=texture->staging().data+layout.offset;uint8_t* stencil=texture->staging().data+layout.stencilOffset;
        for(uint32_t row=0;row<layout.height;++row) for(uint32_t column=0;column<layout.width;++column) {
            const uint8_t* texel=in+size_t(row)*layout.rowPitch+size_t(column)*format.blockBytes;const size_t index=size_t(row)*layout.width+column;
            if(format.blockBytes==8) {std::memcpy(depth+index*4,texel,4);stencil[index]=texel[4];}
            else {uint32_t word=0;std::memcpy(&word,texel,4);const uint32_t d=word&0xFFFFFFu;std::memcpy(depth+index*4,&d,4);stencil[index]=uint8_t(word>>24);}
        }
    }
    if(wrote) device_->vulkan().flush(texture->staging());
    texture->mappedType=D3D11_MAP(0);(void)subresource;
}

// ---------------------------------------------------------------- queries
void D3D11Context::writeTimestamp(D3D11Query& query) {
    query.releaseSlots();
    query.slot=device_->acquireQuerySlot(QueryPool::Timestamp);
    vkCmdWriteTimestamp2(commands(),VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,device_->queryPool(QueryPool::Timestamp),query.slot);
}
// Occlusion/statistics queries begin and end outside dynamic rendering, so
// they stay valid across the render scopes and copies in between.
void D3D11Context::beginSegment(D3D11Query& query) {
    endRendering();
    const uint32_t slot=device_->acquireQuerySlot(query.pool());
    query.segments.push_back(slot);
    const VkQueryControlFlags flags=query.pool()==QueryPool::Occlusion && device_->vulkan().capabilities().preciseOcclusion?VK_QUERY_CONTROL_PRECISE_BIT:0;
    vkCmdBeginQuery(commands(),device_->queryPool(query.pool()),slot,flags);
}
void D3D11Context::endSegment(D3D11Query& query) {
    endRendering();
    vkCmdEndQuery(commands(),device_->queryPool(query.pool()),query.segments.back());
}
void D3D11Context::Begin(ID3D11Asynchronous* async) {
    if(deferred_) {record([object=ComPtr<ID3D11Asynchronous>(async)](D3D11Context& c){c.Begin(object.Get());});return;}
    auto* query=unwrap<D3D11Query>(async);
    if(!query) return;
    if(query->desc().Query==D3D11_QUERY_TIMESTAMP_DISJOINT) {query->begun=true;query->serial=0;}
    if(query->segmented()) {
        if(query->active) {endSegment(*query);std::erase_if(activeQueries_,[&](const auto& q){return q.Get()==query;});}
        query->releaseSlots();query->serial=0;
        beginSegment(*query);query->active=true;activeQueries_.emplace_back(query);
    }
}
void D3D11Context::End(ID3D11Asynchronous* async) {
    if(deferred_) {record([object=ComPtr<ID3D11Asynchronous>(async)](D3D11Context& c){c.End(object.Get());});return;}
    auto* query=unwrap<D3D11Query>(async);
    if(!query) return;
    if(query->desc().Query==D3D11_QUERY_TIMESTAMP) writeTimestamp(*query);
    if(query->segmented()) {
        if(!query->active) {query->releaseSlots();beginSegment(*query);} // D3D11: End without Begin counts nothing.
        endSegment(*query);query->active=false;
        std::erase_if(activeQueries_,[&](const auto& q){return q.Get()==query;});
    }
    query->serial=device_->vulkan().recordingSerial();query->begun=false;
}
HRESULT D3D11Context::GetData(ID3D11Asynchronous* async,void* data,UINT size,UINT flags) {
    if(deferred_) return DXGI_ERROR_INVALID_CALL;
    auto* query=unwrap<D3D11Query>(async);
    if(!query) return E_INVALIDARG;
    if(data && size!=query->GetDataSize()) return E_INVALIDARG;
    if(!query->serial) return S_FALSE;
    auto& vulkan=device_->vulkan();
    if(query->serial>vulkan.submittedSerial()) {
        if(flags&D3D11_ASYNC_GETDATA_DONOTFLUSH) return S_FALSE;
        Flush();
    }
    if(!vulkan.complete(query->serial)) return S_FALSE;
    switch(query->desc().Query) {
    case D3D11_QUERY_EVENT:if(data) *static_cast<BOOL*>(data)=TRUE;return S_OK;
    case D3D11_QUERY_TIMESTAMP: {
        uint64_t value=0;
        const VkResult result=vkGetQueryPoolResults(vulkan.device(),device_->queryPool(QueryPool::Timestamp),query->slot,1,sizeof(value),&value,sizeof(value),VK_QUERY_RESULT_64_BIT);
        if(result==VK_NOT_READY) return S_FALSE;
        check(result,"timestamp query result");
        if(data) *static_cast<UINT64*>(data)=value;
        return S_OK;
    }
    case D3D11_QUERY_OCCLUSION:case D3D11_QUERY_OCCLUSION_PREDICATE:case D3D11_QUERY_PIPELINE_STATISTICS: {
        const bool statistics=query->desc().Query==D3D11_QUERY_PIPELINE_STATISTICS;
        const uint32_t count=statistics?device_->statisticsCount():1;
        std::array<uint64_t,8> sum{},value{};
        for(uint32_t segment:query->segments) {
            const VkResult result=vkGetQueryPoolResults(vulkan.device(),device_->queryPool(query->pool()),segment,1,
                sizeof(uint64_t)*count,value.data(),sizeof(uint64_t)*count,VK_QUERY_RESULT_64_BIT);
            if(result==VK_NOT_READY) return S_FALSE;
            check(result,"query result");
            for(uint32_t i=0;i<count;++i) sum[i]+=value[i];
        }
        if(!data) return S_OK;
        if(query->desc().Query==D3D11_QUERY_OCCLUSION) *static_cast<UINT64*>(data)=sum[0];
        else if(query->desc().Query==D3D11_QUERY_OCCLUSION_PREDICATE) *static_cast<BOOL*>(data)=sum[0]!=0;
        else {
            // Vulkan writes the enabled counters in bit order.
            auto* out=static_cast<D3D11_QUERY_DATA_PIPELINE_STATISTICS*>(data);*out={};
            out->IAVertices=sum[0];out->IAPrimitives=sum[1];out->VSInvocations=sum[2];
            out->CInvocations=sum[3];out->CPrimitives=sum[4];out->PSInvocations=sum[5];
        }
        return S_OK;
    }
    case D3D11_QUERY_TIMESTAMP_DISJOINT:
        if(data) {
            auto* out=static_cast<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT*>(data);
            out->Frequency=UINT64(std::llround(1e9/double(vulkan.properties().limits.timestampPeriod)));out->Disjoint=FALSE;
        }
        return S_OK;
    default:return E_INVALIDARG;
    }
}

// ---------------------------------------------------------------- submission and command lists
void D3D11Context::Flush() {
    if(deferred_) return;
    prepareSubmit();
    device_->vulkan().submit();
    afterSubmit();
}
void D3D11Context::ExecuteCommandList(ID3D11CommandList* list,BOOL restore) {
    if(deferred_) {
        record([object=ComPtr<ID3D11CommandList>(list),restore](D3D11Context& c){c.ExecuteCommandList(object.Get(),restore);});
        return;
    }
    auto* commands=unwrap<D3D11CommandList>(list);
    if(!commands) return;
    // Command lists start from default state and never inherit the caller's.
    std::unique_ptr<ContextState> saved;
    if(restore) {saved=std::move(state_);state_=spareState_?std::move(spareState_):std::make_unique<ContextState>();}
    else state_->clear();
    markAllDirty();
    for(const auto& command:commands->commands()) command(*this);
    state_->clear();
    if(saved) {spareState_=std::move(state_);state_=std::move(saved);}
    markAllDirty();
}
HRESULT D3D11Context::FinishCommandList(BOOL restore,ID3D11CommandList** list) {
    if(!deferred_) return DXGI_ERROR_INVALID_CALL;
    if(!list) return E_INVALIDARG;
    *list=new D3D11CommandList(device_,std::move(recorded_));
    recorded_.clear();deferredMaps_.clear();
    if(!restore) resetState();
    return S_OK;
}
}
