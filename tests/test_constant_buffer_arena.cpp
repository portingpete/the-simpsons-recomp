#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "renderer/native_backend.h"
#include "renderer/constant_buffer_bindings.h"
#include "renderer/native_material_compiler.h"
#include "renderer/device_availability.h"
#include "renderer/immediate_draw.h"
#include "renderer/ball_effect.h"
#include "renderer/particle_draw.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace Simpsons::Graphics {
// This friend exists only in the fixture. No capability or upload test API is
// exported by the game, and observations own copies rather than mutable views.
struct NativeConstantArenaProbe {
    enum class Family { Shadow, Sky, Rigid, ImmediateVertex, ImmediatePixel };
    static_assert(NativeBackend::SkyPixelBank==8 && NativeBackend::ImmediateVertexBank==9 && NativeBackend::ImmediatePixelBank==10);
    struct Observation {
        detail::ConstantBufferSlice slice;
        UINT cursor{};
        bool valid{};
        uint64_t discardMaps{},noOverwriteMaps{};
    };
    static NativeBackend::ConstantBankId bank(Family f) {
        if(f==Family::ImmediateVertex)return NativeBackend::ImmediateVertexBank;
        if(f==Family::ImmediatePixel)return NativeBackend::ImmediatePixelBank;
        return f==Family::Shadow?NativeBackend::ShadowDepthBank:
            f==Family::Sky?NativeBackend::SkyVertexBank:NativeBackend::RigidVertexBank;
    }
    static UINT logicalBytes(Family f) {
        if(f==Family::ImmediateVertex)return 80;
        if(f==Family::ImmediatePixel)return 16;
        return f==Family::Shadow?UINT(sizeof(ShadowDepthConstants)):
            f==Family::Sky?UINT(sizeof(SkyVertexConstants)):UINT(sizeof(RigidVertexConstants));
    }
    static detail::ConstantBufferSlice publish(NativeBackend& b,Family f,std::span<const uint8_t> bytes) {
        return b.publishConstantSlice(bank(f),bytes.data(),UINT(bytes.size()));
    }
    static void require(const NativeBackend& b,Family f,const detail::ConstantBufferSlice& s) {
        b.requireConstantSlice(bank(f),s,logicalBytes(f),"independent arena fixture");
    }
    static std::vector<uint8_t> read(NativeBackend& b,Family f,const detail::ConstantBufferSlice& s) {
        std::vector<uint8_t> out(logicalBytes(f));b.readConstantSlice(bank(f),s,out);return out;
    }
    static Observation observe(const NativeBackend& b,Family f) {
        const auto& v=b.constantBanks[bank(f)];
        return {v.latestSlice,v.arenaCursor,v.publicationValid,v.arenaDiscardMaps,v.arenaNoOverwriteMaps};
    }
    static void resetImmediate(NativeBackend& b) {
        b.constantBanks[NativeBackend::ImmediateVertexBank]={};
        b.constantBanks[NativeBackend::ImmediatePixelBank]={};
    }
    static ComPtr<ID3D11DeviceContext1> context1(const NativeBackend& b){return b.constantBindingContext();}
    static ID3D11DeviceContext* context(NativeBackend& b){return b.context.Get();}
    static ID3D11Device* device(NativeBackend& b){return b.device.Get();}
    static void forceLegacy(NativeBackend& b){(void)b.constantBindingContext();b.constantArenaSupported=false;}
    static ComPtr<ID3D11DeviceContext> setContext(NativeBackend& b,ID3D11DeviceContext* value) {
        auto old=b.context;b.context=value;return old;
    }
    struct DeviceOwner {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        std::shared_ptr<DeviceAvailability> availability;
        D3D_FEATURE_LEVEL level;
    };
    static DeviceOwner replace(NativeBackend& b,const NativeBackend& other) {
        DeviceOwner old{b.device,b.context,b.availability,b.featureLevel};
        b.device=other.device;b.context=other.context;b.availability=other.availability;b.featureLevel=other.featureLevel;
        return old;
    }
    static void restore(NativeBackend& b,const DeviceOwner& old) {
        b.device=old.device;b.context=old.context;b.availability=old.availability;b.featureLevel=old.level;
    }
};
}

using namespace Simpsons::Graphics;
namespace {
using Probe=NativeConstantArenaProbe;
using Family=Probe::Family;
using Slice=detail::ConstantBufferSlice;
size_t checks{};
void need(bool value,const char* message){++checks;if(!value)throw Error(message);}
void check(HRESULT value,const char* message){need(SUCCEEDED(value),message);}
template<class F>void rejects(F&& action,const char* message) {
    bool rejected=false;try{action();}catch(const Error&){rejected=true;}need(rejected,message);
}
template<class T>std::span<const uint8_t> bytes(const T& value) {
    return {reinterpret_cast<const uint8_t*>(&value),sizeof(value)};
}
void setWord(std::vector<uint8_t>& data,size_t offset,uint32_t value) {
    std::memcpy(data.data()+offset,&value,sizeof(value));
}

struct ActualView {
    ComPtr<ID3D11Buffer> buffer;
    UINT first{},count{};
    friend bool operator==(const ActualView& a,const ActualView& b) {
        return a.buffer.Get()==b.buffer.Get()&&a.first==b.first&&a.count==b.count;
    }
};
ActualView actual(ID3D11DeviceContext1* c,bool vertex,UINT slot) {
    ActualView v;if(vertex)c->VSGetConstantBuffers1(slot,1,&v.buffer,&v.first,&v.count);
    else c->PSGetConstantBuffers1(slot,1,&v.buffer,&v.first,&v.count);return v;
}
void bindActual(ID3D11DeviceContext1* c,bool vertex,UINT slot,const ActualView& v) {
    auto* p=v.buffer.Get();
    if(!p||(v.first==0&&v.count==4096)) {
        if(vertex)c->VSSetConstantBuffers(slot,1,&p);else c->PSSetConstantBuffers(slot,1,&p);return;
    }
    if(vertex)c->VSSetConstantBuffers1(slot,1,&p,&v.first,&v.count);
    else c->PSSetConstantBuffers1(slot,1,&p,&v.first,&v.count);
}
bool supportsOffsets(ID3D11Device* device) {
    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
    return SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,&options,sizeof(options)))&&options.ConstantBufferOffsetting;
}
ComPtr<ID3D11Buffer> constantBuffer(ID3D11Device* device,UINT size) {
    std::vector<uint32_t> data(size/4);for(size_t i=0;i<data.size();++i)data[i]=0x13579BDFu+uint32_t(i);
    D3D11_BUFFER_DESC d{};
    d.ByteWidth=size;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    const D3D11_SUBRESOURCE_DATA initial{data.data(),0,0};ComPtr<ID3D11Buffer> buffer;
    check(device->CreateBuffer(&d,&initial,&buffer),"Independent immutable constants creation failed");return buffer;
}
std::vector<uint8_t> bufferBytes(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Buffer* source) {
    D3D11_BUFFER_DESC d{};source->GetDesc(&d);const UINT size=d.ByteWidth;
    d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;d.StructureByteStride=0;
    ComPtr<ID3D11Buffer> staging;check(device->CreateBuffer(&d,nullptr,&staging),"Full physical staging buffer creation failed");
    context->CopyResource(staging.Get(),source);D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Physical constant bytes Map failed");
    std::vector<uint8_t> result(size);std::memcpy(result.data(),mapped.pData,size);context->Unmap(staging.Get(),0);return result;
}
void descriptor(NativeBackend& b,const Slice& s,UINT logical) {
    need(s.binding.buffer!=nullptr,"Published slice has no actual buffer");D3D11_BUFFER_DESC d{};s.binding.buffer->GetDesc(&d);
    ComPtr<ID3D11Device> owner;s.binding.buffer->GetDevice(&owner);
    need(owner.Get()==Probe::device(b),"Published buffer belongs to another actual device");
    need(d.Usage==D3D11_USAGE_DYNAMIC&&d.BindFlags==D3D11_BIND_CONSTANT_BUFFER&&
        d.CPUAccessFlags==D3D11_CPU_ACCESS_WRITE&&d.MiscFlags==0&&d.StructureByteStride==0,
        "Published constant descriptor changed");
    need(s.logicalBytes==logical&&d.ByteWidth==(s.arena?65536u:logical),"Physical/logical byte extents changed");
    if(s.arena)need(s.binding.view==detail::ConstantBufferView::Ranged&&s.binding.firstConstant%16==0&&
        s.binding.numConstants%16==0&&s.binding.numConstants*16==((logical+255)/256)*256,
        "Arena slice has the wrong independently computed range extent");
    else need(s.binding.view==detail::ConstantBufferView::Full,"Legacy publication became a ranged arena view");
}

// Observe actual Map and the post-bind pixel constant setter on the complete
// context table. No dynamic Draw dispatch hook or process-global diagnostic
// installation determines admission. All pointers and original page protections
// restore before the strong context owner releases.
// Actual SDK26100 Context1 slot123 is PSSetConstantBuffers1. This negative-only
// observer owns that queried interface/table. It never throws through the
// noexcept binding helper: one corrupted real PS2 bind is rejected afterward.
struct ImmediatePixelSetterFault {
    using Set=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,UINT,UINT,ID3D11Buffer* const*,const UINT*,const UINT*);
    static ImmediatePixelSetterFault* active;
    ComPtr<ID3D11DeviceContext1> owner;ComPtr<ID3D11DeviceContext> base;
    NativeBackend* backend;bool& armed;size_t& injections;
    void** table{};Set original{};DWORD protection{};bool installed{};
    ImmediatePixelSetterFault(NativeBackend& b,bool& flag,size_t& count):owner(Probe::context1(b)),base(Probe::context(b)),backend(&b),armed(flag),injections(count) {
        if(!owner)return; // No Set1 operation exists in the real legacy-only path.
        need(active==nullptr,"Nested actual Context1 pixel-setter observer");
        table=*reinterpret_cast<void***>(owner.Get());original=reinterpret_cast<Set>(table[123]);
        need(original!=nullptr,"Actual Context1 pixel-setter target unavailable");
        need(VirtualProtect(table+123,sizeof(void*),PAGE_READWRITE,&protection)!=0,"Context1 pixel-setter write admission failed");
        active=this;table[123]=reinterpret_cast<void*>(&set);installed=true;DWORD ignored{};
        if(!VirtualProtect(table+123,sizeof(void*),protection,&ignored)) {
            table[123]=reinterpret_cast<void*>(original);installed=false;active=nullptr;
            if(!VirtualProtect(table+123,sizeof(void*),protection,&ignored))std::abort();
            throw Error("Context1 pixel-setter protection restoration failed");
        }
    }
    bool restore() noexcept {
        if(!installed)return true;
        if(table[123]!=reinterpret_cast<void*>(&set)){installed=false;active=nullptr;return true;}
        DWORD old{};if(!VirtualProtect(table+123,sizeof(void*),PAGE_READWRITE,&old))return false;
        table[123]=reinterpret_cast<void*>(original);installed=false;active=nullptr;DWORD ignored{};
        // Fail-stop after the pointer write: a retry cannot release the
        // actual context while its table page remains writable.
        if(!VirtualProtect(table+123,sizeof(void*),protection,&ignored))std::abort();
        return table[123]==reinterpret_cast<void*>(original);
    }
    ~ImmediatePixelSetterFault(){if(!restore())std::abort();}
    ImmediatePixelSetterFault(const ImmediatePixelSetterFault&)=delete;
    ImmediatePixelSetterFault& operator=(const ImmediatePixelSetterFault&)=delete;
    static void STDMETHODCALLTYPE set(ID3D11DeviceContext1* c,UINT start,UINT count,ID3D11Buffer* const* buffers,const UINT* first,const UINT* lengths) {
        auto& s=*active;s.original(c,start,count,buffers,first,lengths);
        if(c==s.owner.Get()&&s.armed&&start==2&&count==1&&buffers&&buffers[0]&&
           buffers[0]==Probe::observe(*s.backend,Family::ImmediatePixel).slice.binding.buffer.Get()) {
            s.armed=false;++s.injections;ID3D11Buffer* empty=nullptr;
            s.base->PSSetConstantBuffers(2,1,&empty);
        }
    }
};
ImmediatePixelSetterFault* ImmediatePixelSetterFault::active{};

struct Calls {
    using Map=HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,D3D11_MAP,UINT,D3D11_MAPPED_SUBRESOURCE*);
    // SDK26100 ID3D11DeviceContextVtbl slot16: PSSetConstantBuffers.
    using PixelConstants=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,ID3D11Buffer* const*);
    struct Event {ID3D11Resource* resource;D3D11_MAP mode;UINT flags;HRESULT result;};
    struct Slot {size_t index;void* original;DWORD protection;bool installed;};
    static Calls* active;
    ComPtr<ID3D11DeviceContext> owner;
    NativeBackend* backend;std::unique_ptr<ImmediatePixelSetterFault> rangedPixelFault;
    void** table{};
    Map originalMap{};PixelConstants originalPixelConstants{};
    std::array<Slot,2> slots{{{14,nullptr,0,false},{16,nullptr,0,false}}};
    std::array<Event,4096> events{};size_t count{},writes{},failWrite{},injections{};bool failPostBind{};
    size_t constantMaps{}; // Uncapped; immediate draws also Map their vertex streams.
    explicit Calls(NativeBackend& b):owner(Probe::context(b)),backend(&b) {
        need(active==nullptr&&owner,"Malformed/nested typed D3D observer");
        table=*reinterpret_cast<void***>(owner.Get());
        for(auto& slot:slots){slot.original=table[slot.index];need(slot.original!=nullptr,"Actual D3D method target unavailable");}
        originalMap=reinterpret_cast<Map>(slots[0].original);
        originalPixelConstants=reinterpret_cast<PixelConstants>(slots[1].original);
        const std::array<void*,2> hooks{reinterpret_cast<void*>(&map),reinterpret_cast<void*>(&pixelConstants)};
        active=this;
        try {
            for(size_t i=0;i<slots.size();++i) {
                auto& slot=slots[i];
                need(VirtualProtect(table+slot.index,sizeof(void*),PAGE_READWRITE,&slot.protection)!=0,"D3D observer slot write admission failed");
                table[slot.index]=hooks[i];slot.installed=true;DWORD ignored{};
                // Restore immediately so the next slot captures the original
                // protection even when both entries occupy the same page.
                need(VirtualProtect(table+slot.index,sizeof(void*),slot.protection,&ignored)!=0,"D3D observer slot protection restoration failed");
            }
            rangedPixelFault=std::make_unique<ImmediatePixelSetterFault>(b,failPostBind,injections);
        } catch(...) {if(!restore())std::abort();throw;}
    }
    bool restore() noexcept {
        if(rangedPixelFault&&!rangedPixelFault->restore())return false;
        rangedPixelFault.reset();
        bool restored=true;
        for(size_t i=slots.size();i>0;--i) {
            auto& slot=slots[i-1];if(!slot.installed)continue;
            const void* hook=slot.index==14?reinterpret_cast<void*>(&map):reinterpret_cast<void*>(&pixelConstants);
            // A driver replacement already removed our hook; do not overwrite
            // that legitimate new dispatch target with a stale saved function.
            if(table[slot.index]!=hook){slot.installed=false;continue;}
            DWORD old{};
            if(!VirtualProtect(table+slot.index,sizeof(void*),PAGE_READWRITE,&old)){restored=false;continue;}
            table[slot.index]=slot.original;DWORD ignored{};
            // Our pointer write is not a driver replacement. If its page
            // protection cannot restore, fail-stop before any retry or release.
            if(!VirtualProtect(table+slot.index,sizeof(void*),slot.protection,&ignored))std::abort();
            if(table[slot.index]==slot.original)slot.installed=false;else restored=false;
        }
        if(restored)active=nullptr;return restored;
    }
    void finish(){need(restore(),"D3D observer pointers/page protections did not restore before owner release");}
    ~Calls(){if(!restore())std::abort();}
    Calls(const Calls&)=delete;Calls& operator=(const Calls&)=delete;
    static HRESULT STDMETHODCALLTYPE map(ID3D11DeviceContext* c,ID3D11Resource* r,UINT sub,D3D11_MAP mode,UINT flags,D3D11_MAPPED_SUBRESOURCE* out) {
        auto& s=*active;const bool mine=c==s.owner.Get();
        if(mine&&mode!=D3D11_MAP_READ)++s.writes;
        const HRESULT result=mine&&s.failWrite&&s.writes==s.failWrite&&mode!=D3D11_MAP_READ?
            E_OUTOFMEMORY:s.originalMap(c,r,sub,mode,flags,out);
        if(mine&&s.count<s.events.size())s.events[s.count++]={r,mode,flags,result};
        if(mine) {
            ComPtr<ID3D11Buffer> buffer;D3D11_BUFFER_DESC desc{};
            if(SUCCEEDED(r->QueryInterface(IID_PPV_ARGS(&buffer)))) {buffer->GetDesc(&desc);if(desc.BindFlags&D3D11_BIND_CONSTANT_BUFFER)++s.constantMaps;}
        }
        return result;
    }
    static void STDMETHODCALLTYPE pixelConstants(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11Buffer* const* buffers) {
        auto& s=*active;s.originalPixelConstants(c,start,count,buffers);
        // New immediate full-buffer fallback also enters a noexcept helper.
        // Keep the old throwing observer for every other original draw family.
        if(c==s.owner.Get()&&s.failPostBind&&start==2&&count==1&&buffers&&buffers[0]&&
           buffers[0]==Probe::observe(*s.backend,Family::ImmediatePixel).slice.binding.buffer.Get()) {
            s.failPostBind=false;++s.injections;ID3D11Buffer* empty=nullptr;
            s.originalPixelConstants(c,2,1,&empty);return;
        }
        if(c==s.owner.Get()&&s.failPostBind&&start==2&&count==1&&buffers&&buffers[0]) {
            // Real VS0/PS1/PS2 have changed inside Restore. Disarm before any
            // throw so a full/null legacy PS2 restore cannot inject again.
            s.failPostBind=false;++s.injections;throw Error("Injected fixture post-bind constant setter failure");
        }
    }
};
Calls* Calls::active{};

// Installed SDK26100 ID3D11DeviceContext1Vtbl has134 methods; this exact
// signature is method125 (VSGetConstantBuffers1). Patch one entry on the full
// original table; every other SDK/private method and all other contexts remain
// untouched. The injected return tuple is consumed only after a new bank bind.
struct RangeReadFailure {
    using Get=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,UINT,UINT,ID3D11Buffer**,UINT*,UINT*);
    static RangeReadFailure* active;
    ComPtr<ID3D11DeviceContext1> owner;
    ComPtr<ID3D11Buffer> retained;
    void** table{};Get original{};DWORD originalProtection{};bool installed{},armed=true;unsigned injections{};
    RangeReadFailure(ID3D11DeviceContext1* c,ID3D11Buffer* oldBuffer):owner(c),retained(oldBuffer) {
        need(active==nullptr&&owner&&retained,"Malformed/nested Context1 getter fault scope");
        table=*reinterpret_cast<void***>(owner.Get());original=reinterpret_cast<Get>(table[125]);
        need(original!=nullptr,"Actual Context1 getter target unavailable");
        need(VirtualProtect(table+125,sizeof(void*),PAGE_READWRITE,&originalProtection)!=0,"Context1 getter slot write admission failed");
        active=this;table[125]=reinterpret_cast<void*>(&get);installed=true;DWORD ignored{};
        if(!VirtualProtect(table+125,sizeof(void*),originalProtection,&ignored)) {
            // Restore the original pointer while the page is still writable.
            table[125]=reinterpret_cast<void*>(original);installed=false;active=nullptr;
            (void)VirtualProtect(table+125,sizeof(void*),originalProtection,&ignored);
            throw Error("Context1 getter slot protection restoration failed during construction");
        }
    }
    bool restore() noexcept {
        if(!installed)return true;DWORD old{};
        if(!VirtualProtect(table+125,sizeof(void*),PAGE_READWRITE,&old))return false;
        table[125]=reinterpret_cast<void*>(original);installed=false;active=nullptr;DWORD ignored{};
        return VirtualProtect(table+125,sizeof(void*),originalProtection,&ignored)!=0&&table[125]==reinterpret_cast<void*>(original);
    }
    void finish(){need(restore(),"Context1 getter pointer/page protection did not restore before owner release");}
    ~RangeReadFailure(){if(installed&&!restore())std::abort();}
    RangeReadFailure(const RangeReadFailure&)=delete;RangeReadFailure& operator=(const RangeReadFailure&)=delete;
    static void STDMETHODCALLTYPE get(ID3D11DeviceContext1* c,UINT start,UINT count,ID3D11Buffer** buffers,UINT* first,UINT* lengths) {
        auto& s=*active;s.original(c,start,count,buffers,first,lengths);
        if(c==s.owner.Get()&&s.armed&&start==0&&count==1&&buffers&&buffers[0]&&buffers[0]!=s.retained.Get()&&first&&lengths) {
            first[0]+=16;lengths[0]=16;s.armed=false;++s.injections;
        }
    }
};
RangeReadFailure* RangeReadFailure::active{};

struct GPUQueue {
    ID3D11Device* device;ID3D11DeviceContext* context;
    ComPtr<ID3D11Texture2D> target;ComPtr<ID3D11RenderTargetView> view;
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11RasterizerState> raster;UINT width;
    GPUQueue(NativeBackend& b,UINT logical,UINT draws):device(Probe::device(b)),context(Probe::context(b)),width(draws) {
        // One output pixel per queued publication, no CPU/GPU completion query
        // between append/wrap writes. Integer output keeps this byte oracle exact.
        const std::string hlsl="cbuffer Bank:register(b0){uint4 c[4096];}; struct O{float4 p:SV_Position;nointerpolation uint4 v:TEXCOORD0;};"
            "O VS(uint id:SV_VertexID){O o;float2 p=float2((id<<1)&2,id&2);o.p=float4(p*float2(2,-2)+float2(-1,1),0,1);"
            "o.v=uint4(c[0].x,c["+std::to_string(logical/16-1)+"].w,c["+std::to_string(logical/16)+"].x,c[4095].x);return o;}"
            "uint4 PS(O o):SV_Target{return o.v;}";
        ComPtr<ID3DBlob> vs,ps,error;const UINT flags=D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS|D3DCOMPILE_OPTIMIZATION_LEVEL3;
        check(D3DCompile(hlsl.data(),hlsl.size(),"independent-arena-oracle",nullptr,nullptr,"VS","vs_5_0",flags,0,&vs,&error),"Independent arena VS compile failed");
        error.Reset();check(D3DCompile(hlsl.data(),hlsl.size(),"independent-arena-oracle",nullptr,nullptr,"PS","ps_5_0",flags,0,&ps,&error),"Independent arena PS compile failed");
        check(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex),"Independent arena VS creation failed");
        check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel),"Independent arena PS creation failed");
        D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R32G32B32A32_UINT;
        d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET;check(device->CreateTexture2D(&d,nullptr,&target),"Independent integer target creation failed");
        check(device->CreateRenderTargetView(target.Get(),nullptr,&view),"Independent integer RTV creation failed");
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        check(device->CreateRasterizerState(&rd,&raster),"Independent arena raster state creation failed");
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);
        context->GSSetShader(nullptr,nullptr,0);context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);
        context->RSSetState(raster.Get());context->OMSetBlendState(nullptr,nullptr,0xFFFFFFFF);context->OMSetDepthStencilState(nullptr,0);
        auto* rt=view.Get();context->OMSetRenderTargets(1,&rt,nullptr);
    }
    void submit(UINT index,ID3D11DeviceContext1* context1,const Slice& slice) {
        detail::bindConstantBuffer(context,context1,detail::ConstantBufferStage::Vertex,0,slice.binding);
        submitAlreadyBound(index);
    }
    void submitAlreadyBound(UINT index) {
        const D3D11_VIEWPORT vp{float(index),0,1,1,0,1};context->RSSetViewports(1,&vp);context->Draw(3,0);
    }
    std::vector<std::array<uint32_t,4>> finish() {
        context->OMSetRenderTargets(0,nullptr,nullptr);D3D11_TEXTURE2D_DESC d{};target->GetDesc(&d);
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;
        check(device->CreateTexture2D(&d,nullptr,&staging),"Queued arena staging creation failed");context->CopyResource(staging.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Queued arena GPU readback failed");
        std::vector<std::array<uint32_t,4>> result(width);std::memcpy(result.data(),mapped.pData,width*16);context->Unmap(staging.Get(),0);return result;
    }
};

void queuedWindows(bool hardware,Family family,bool forceLegacy) {
    NativeBackend b(!hardware);auto context1=Probe::context1(b);if(forceLegacy)Probe::forceLegacy(b);
    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
    const bool supported=context1&&SUCCEEDED(Probe::device(b)->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,&options,sizeof(options)))&&
        options.ConstantBufferOffsetting&&options.MapNoOverwriteOnDynamicConstantBuffer;
    Calls calls(b);const UINT logical=Probe::logicalBytes(family),window=((logical+255)/256)*256;
    const UINT beforeWrap=65536/window,count=beforeWrap*2+3;GPUQueue queue(b,logical,count);
    std::vector<std::array<uint32_t,4>> expected;expected.reserve(count);Slice first,last;
    uint64_t discard{},noOverwrite{};const auto initial=Probe::observe(b,family);
    for(UINT i=0;i<count;++i) {
        std::vector<uint8_t> payload(logical,0);setWord(payload,0,0x100u+i);setWord(payload,logical-4,0x10000u+i);
        const size_t maps=calls.count;last=Probe::publish(b,family,payload);Probe::require(b,family,last);descriptor(b,last,logical);
        need(last.arena==(supported&&!forceLegacy),"Arena selection disagrees with independent actual capability/fallback proof");
        if(i==0)first=last;
        need(calls.count==maps+1&&calls.events[maps].flags==0&&SUCCEEDED(calls.events[maps].result),"Changed constants did not make one flags-zero Map");
        if(last.arena) {
            const UINT offset=(i%beforeWrap)*window;
            need(last.binding.firstConstant*16==offset,"Append/wrap cursor differs from independent fixed bank arithmetic");
            const auto mode=i%beforeWrap==0?D3D11_MAP_WRITE_DISCARD:D3D11_MAP_WRITE_NO_OVERWRITE;
            need(calls.events[maps].mode==mode,"Arena changed constants used the wrong actual Map mode");
            if(mode==D3D11_MAP_WRITE_DISCARD)++discard;else ++noOverwrite;
        } else need(calls.events[maps].mode==D3D11_MAP_WRITE_DISCARD,"Legacy upload no longer uses the original DISCARD path");
        expected.push_back({0x100u+i,0x10000u+i,0,0});queue.submit(i,context1.Get(),last);
    }
    need(queue.finish()==expected,"Queued earlier constants were overwritten across arena append/wrap or padding/OOB changed");
    rejects([&]{Probe::require(b,family,first);},"An old publication generation survived subsequent writes");
    auto observed=Probe::observe(b,family);need(observed.valid,"Successful publication is not valid");
    if(last.arena)need(observed.discardMaps-initial.discardMaps==discard&&observed.noOverwriteMaps-initial.noOverwriteMaps==noOverwrite,
        "Arena successful Map bookkeeping differs from actual Map observations");
    std::vector<uint8_t> finalPayload(logical);setWord(finalPayload,0,0x100u+count-1);setWord(finalPayload,logical-4,0x10000u+count-1);
    need(Probe::read(b,family,last)==finalPayload,"Core readback did not extract only committed logical bytes");
    const auto physical=bufferBytes(Probe::device(b),Probe::context(b),last.binding.buffer.Get());
    if(last.arena) {
        const size_t begin=last.binding.firstConstant*16;
        need(std::equal(finalPayload.begin(),finalPayload.end(),physical.begin()+begin),"Actual arena byte window differs");
        need(std::all_of(physical.begin()+begin+logical,physical.begin()+begin+window,[](uint8_t v){return v==0;}),"Actual rounded padding was not cleared");
    }
    const size_t maps=calls.count;const auto same=Probe::publish(b,family,finalPayload);
    need(calls.count==maps&&same.generation!=last.generation&&same.binding.buffer==last.binding.buffer&&
        same.binding.firstConstant==last.binding.firstConstant&&same.binding.numConstants==last.binding.numConstants,
        "Identical-byte publication mapped storage or lost exact latest-slice reuse/stale generation protection");
    rejects([&]{Probe::require(b,family,last);},"Same-byte publication did not retire old generation");
    Probe::require(b,family,same);auto negativeZero=finalPayload;setWord(negativeZero,4,0x80000000u);
    const auto changed=Probe::publish(b,family,negativeZero);need(calls.count==maps+1,"Signed zero was treated as byte-identical");
    need(Probe::read(b,family,changed)==negativeZero,"Signed-zero bits changed during upload");
    for(unsigned kind=0;kind<5;++kind) {
        if(!changed.arena&&(kind==2||kind==3))continue;
        auto bad=changed;if(kind==0)++bad.generation;else if(kind==1)++bad.logicalBytes;
        else if(kind==2)bad.binding.firstConstant+=16;else if(kind==3)bad.binding.numConstants=0;else bad.binding.buffer=constantBuffer(Probe::device(b),logical);
        rejects([&]{Probe::require(b,family,bad);},"Tampered slice range/extent/generation/buffer proof passed");
    }
    // Actual Map failure must invalidate the latest proof, retain the cursor,
    // and require a genuine upload on retry even for formerly identical bytes.
    const auto prior=Probe::observe(b,family);calls.failWrite=calls.writes+1;auto next=negativeZero;setWord(next,0,0xABCDEFu);
    rejects([&]{(void)Probe::publish(b,family,next);},"Injected Map failure did not propagate");calls.failWrite=0;
    observed=Probe::observe(b,family);need(!observed.valid&&observed.cursor==prior.cursor,"Failed Map published validity or advanced cursor");
    rejects([&]{Probe::require(b,family,changed);},"Map failure retained an old qualified publication");
    const size_t retryMaps=calls.count;last=Probe::publish(b,family,negativeZero);
    need(calls.count==retryMaps+1,"Map failure retained an unsafe identical-byte shortcut");need(Probe::read(b,family,last)==negativeZero,"Map retry lost original bytes");
    const auto valid=Probe::observe(b,family);const size_t malformedMaps=calls.count;
    rejects([&]{(void)Probe::publish(b,family,std::span<const uint8_t>(negativeZero).first(logical-1));},"Wrong logical publication extent passed");
    rejects([&]{(void)Probe::publish(b,family,{});},"Empty publication passed");
    need(calls.count==malformedMaps&&Probe::observe(b,family).slice.generation==valid.slice.generation,
        "Rejected extent reached Map or changed the previous publication");Probe::require(b,family,last);
    std::fprintf(stderr,"[ARENA FIXTURE] family=%u forced_legacy=%u arena=%u queued=%u wrap_boundary=%u actual_discard=%llu actual_nooverwrite=%llu\n",
        unsigned(family),unsigned(forceLegacy),unsigned(last.arena),count,beforeWrap,
        static_cast<unsigned long long>(discard),static_cast<unsigned long long>(noOverwrite));
    calls.finish();
}

struct Materials {
    NativeMaterialCompiler compiler;MaterialRegistry registry;
    const CompiledMaterial *shadow{},*rigidVS{},*rigidPS{},*skyVS{},*skyPS{};
    Materials(NativeBackend& b,const std::vector<uint8_t>& image):compiler(b) {
        for(const auto& row:originalMaterialIdentities()) {
            const auto address=row.originalAddress;
            if(address!=0x820C2FA0&&address!=0x8200D3AC&&address!=0x8200D9E8&&address!=0x82036F08&&address!=0x820374E8)continue;
            const auto id=registry.create(address,std::span<const uint8_t>(image).subspan(address-0x82000000,row.recordBytes));
            const auto* artifact=&registry.prepareForBind(id,compiler);
            if(address==0x820C2FA0)shadow=artifact;else if(address==0x8200D3AC)rigidVS=artifact;
            else if(address==0x8200D9E8)rigidPS=artifact;else if(address==0x82036F08)skyVS=artifact;else skyPS=artifact;
        }
        need(shadow&&rigidVS&&rigidPS&&skyVS&&skyPS,"Exact original public commit shaders unavailable");
    }
};

void publicCommits(const std::vector<uint8_t>& image,bool hardware,bool forceLegacy) {
    NativeBackend b(!hardware);auto c1=Probe::context1(b);if(forceLegacy)Probe::forceLegacy(b);Calls calls(b);Materials m(b,image);
    ShadowDepthConstants shadow{};RigidVertexConstants rigid{};RigidPixelConstants pixel{};SkyVertexConstants sky{};SkyPixelConstants skyPixel{};
    shadow[0][0]=rigid[0][0]=sky[0][0]=1;pixel[50][3]=skyPixel[50][3]=2;
    b.bindShadowDepthShader(*m.shadow);auto sc=b.commitShadowDepth(*m.shadow,shadow);b.requireShadowDepthCommit(sc);
    const auto actualShadow=b.readbackShadowDepthConstants(sc);
    need(std::memcmp(&shadow,&actualShadow,sizeof(shadow))==0,"Public shadow readback differs");
    b.bindRigidShaders(*m.rigidVS,*m.rigidPS);auto rc=b.commitRigid(*m.rigidVS,*m.rigidPS,rigid,pixel);b.requireRigidCommit(rc);
    const auto actualRigid=b.readbackRigidVertexConstants(rc);
    const auto actualPixel=b.readbackRigidPixelConstants(rc);
    need(std::memcmp(&rigid,&actualRigid,sizeof(rigid))==0&&std::memcmp(&pixel,&actualPixel,sizeof(pixel))==0,"Public rigid logical readback differs");
    ComPtr<ID3D11Buffer> pixelBuffer;Probe::context(b)->PSGetConstantBuffers(0,1,&pixelBuffer);D3D11_BUFFER_DESC pixelDesc{};pixelBuffer->GetDesc(&pixelDesc);
    need(pixelDesc.ByteWidth==sizeof(RigidPixelConstants)&&pixelDesc.Usage==D3D11_USAGE_DYNAMIC&&pixelDesc.CPUAccessFlags==D3D11_CPU_ACCESS_WRITE,
        "Unselected pixel bank changed its original physical descriptor");
    b.bindRigidShaders(*m.skyVS,*m.skyPS);auto kc=b.commitSky(*m.skyVS,*m.skyPS,sky,skyPixel);b.requireSkyCommit(kc);
    // Private observation supplies the same actual GPU readback for Sky, whose
    // public API deliberately has no standalone constant-readback operation.
    const auto skySlice=Probe::observe(b,Family::Sky).slice;need(Probe::read(b,Family::Sky,skySlice)==std::vector<uint8_t>(bytes(sky).begin(),bytes(sky).end()),"Public sky commit GPU bytes differ");
    shadow[0][0]=2;const auto oldSC=sc;b.bindShadowDepthShader(*m.shadow);sc=b.commitShadowDepth(*m.shadow,shadow);rejects([&]{b.requireShadowDepthCommit(oldSC);},"Old public shadow generation passed");
    rigid[0][0]=3;const auto oldRC=rc;b.bindRigidShaders(*m.rigidVS,*m.rigidPS);rc=b.commitRigid(*m.rigidVS,*m.rigidPS,rigid,pixel);rejects([&]{b.requireRigidCommit(oldRC);},"Old public rigid generation passed");
    sky[0][0]=4;const auto oldKC=kc;b.bindRigidShaders(*m.skyVS,*m.skyPS);kc=b.commitSky(*m.skyVS,*m.skyPS,sky,skyPixel);rejects([&]{b.requireSkyCommit(oldKC);},"Old public sky generation passed");
    if(c1) {
        // Actual pointer remains correct while only Context1 range metadata is
        // changed. The opaque public commit guard must reject that tampering.
        const bool offsets=supportsOffsets(Probe::device(b));
        if(offsets) {
            const auto good=actual(c1.Get(),true,0);auto bad=good;bad.first+=16;bad.count=16;bindActual(c1.Get(),true,0,bad);
            rejects([&]{b.requireSkyCommit(kc);},"Public sky guard ignored actual bound range offset");bindActual(c1.Get(),true,0,good);
            bad=good;bad.count=0;bindActual(c1.Get(),true,0,bad);rejects([&]{b.requireSkyCommit(kc);},"Public sky guard accepted an explicit zero-length range");bindActual(c1.Get(),true,0,good);
        }
        auto sentinel=constantBuffer(Probe::device(b),65536);
        const ActualView vs{sentinel,offsets?16u:0u,offsets?16u:4096u},ps{sentinel,offsets?32u:0u,offsets?16u:4096u};
        b.bindRigidShaders(*m.rigidVS,*m.rigidPS);
        bindActual(c1.Get(),true,0,vs);bindActual(c1.Get(),false,0,ps);
        rigid[1][0]=5;pixel[1][0]=6;calls.failWrite=calls.writes+2;
        rejects([&]{(void)b.commitRigid(*m.rigidVS,*m.rigidPS,rigid,pixel);},"Public commit partial upload did not fail");calls.failWrite=0;
        need(actual(c1.Get(),true,0)==vs&&actual(c1.Get(),false,0)==ps,"Failed public commit did not restore real VS0/PS0 ranges");
        rc=b.commitRigid(*m.rigidVS,*m.rigidPS,rigid,pixel);b.requireRigidCommit(rc);
        for(Family family:{Family::Shadow,Family::Rigid,Family::Sky}) {
            if(family==Family::Shadow)b.bindShadowDepthShader(*m.shadow);
            else if(family==Family::Rigid)b.bindRigidShaders(*m.rigidVS,*m.rigidPS);
            else b.bindRigidShaders(*m.skyVS,*m.skyPS);
            bindActual(c1.Get(),true,0,vs);bindActual(c1.Get(),false,0,ps);
            RangeReadFailure fault(c1.Get(),sentinel.Get());
            rejects([&]{if(family==Family::Shadow)(void)b.commitShadowDepth(*m.shadow,shadow);
                else if(family==Family::Rigid)(void)b.commitRigid(*m.rigidVS,*m.rigidPS,rigid,pixel);
                else (void)b.commitSky(*m.skyVS,*m.skyPS,sky,skyPixel);},"Post-bind public commit proof did not reject one corrupted getter tuple");
            need(fault.injections==1&&!fault.armed,"Post-bind public commit fault was not exactly once");fault.finish();
            need(actual(c1.Get(),true,0)==vs&&actual(c1.Get(),false,0)==ps,"Public commit catch path lost actual original ranges");
        }
    }
    b.bindRigidShaders(*m.rigidVS,*m.rigidPS);rc=b.commitRigid(*m.rigidVS,*m.rigidPS,rigid,pixel);
    auto invalid=rigid;invalid[0][0]=std::numeric_limits<float>::quiet_NaN();const size_t maps=calls.count;
    rejects([&]{(void)b.commitRigid(*m.rigidVS,*m.rigidPS,invalid,pixel);},"Public nonfinite constants passed");
    rejects([&]{(void)b.commitRigid(*m.shadow,*m.rigidPS,rigid,pixel);},"Public wrong material identity passed");
    need(calls.count==maps,"Rejected public input reached a Map");
    NativeBackend other(!hardware);const auto before=Probe::observe(b,Family::Rigid).slice;
    bool threadRejected=false;std::thread wrongOwner([&]{try{b.requireRigidCommit(rc);}catch(const Error&){threadRejected=true;}});wrongOwner.join();
    need(threadRejected,"Public commit bypassed backend thread ownership");
    auto foreign=constantBuffer(Probe::device(other),65536);auto bad=before;bad.binding.buffer=foreign;
    rejects([&]{Probe::require(b,Family::Rigid,bad);},"Foreign actual-device buffer passed slice proof");
    auto oldContext=Probe::setContext(b,Probe::context(other));rejects([&]{b.requireRigidCommit(rc);},"Foreign context passed current commit proof");Probe::setContext(b,oldContext.Get());
    ComPtr<ID3D11DeviceContext> deferred;check(Probe::device(b)->CreateDeferredContext(0,&deferred),"Independent deferred context creation failed");
    oldContext=Probe::setContext(b,deferred.Get());rejects([&]{b.requireRigidCommit(rc);},"Deferred context passed immediate commit proof");Probe::setContext(b,oldContext.Get());
    const auto oldOwner=Probe::replace(b,other);rejects([&]{b.requireRigidCommit(rc);},"Replaced actual device retained old commit qualification");
    const auto replacement=Probe::publish(b,Family::Rigid,bytes(rigid));descriptor(b,replacement,UINT(sizeof(rigid)));
    need(replacement.binding.buffer.Get()!=before.binding.buffer.Get(),"Replacement device reused old actual constant storage");Probe::restore(b,oldOwner);
    rejects([&]{Probe::require(b,Family::Rigid,before);},"Replaced bank retained obsolete device publication");
    calls.finish();
}

D3D11_SAMPLER_DESC sampler() {
    D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    s.MaxLOD=13;s.MaxAnisotropy=1;s.ComparisonFunc=D3D11_COMPARISON_NEVER;return s;
}
template<class T>void drawState(T& d) {
    d.viewport={0,0,16,16,0,0x3F800000};d.depthCompare=7;d.viewportEnable=d.halfPixelOffset=1;
    d.colorMask=15;d.multisampleMask=0xFFFFFFFF;d.blendWord=0x10106;d.blendEnable=0;
}
void directRangeRestore(bool hardware,bool forceLegacy) {
    NativeBackend b(!hardware);auto c1=Probe::context1(b);if(!c1){std::fprintf(stderr,"[ARENA FIXTURE] Context1 absent; real ranged restore unavailable\n");return;}
    if(forceLegacy)Probe::forceLegacy(b);Calls calls(b);auto* context=Probe::context(b);
    auto color=b.createTarget(16,16,TargetFormat::RGB10A2);auto depth=b.createDepthTarget(16,16);
    b.bindTargets({color,nullptr,nullptr,nullptr},depth);b.setViewport({0,0,16,16,0,1});
    const std::array<uint8_t,4> texel{255,255,255,255};auto texture=b.createTexture(1,1,TextureFormat::RGBA8,texel);
    ImmediateDraw immediate{};drawState(immediate);immediate.texture=texture;immediate.sampler=sampler();
    for(size_t row=0;row<4;++row)immediate.constants[row][row]=1;immediate.constants[4]={1,1,1,1};
    for(auto xy:std::array<std::array<float,2>,4>{{{-.5f,.5f},{-.5f,-.5f},{.5f,.5f},{.5f,-.5f}}})immediate.vertices.push_back({{xy[0],xy[1],.5f},1,{0,0,0,0}});
    BallEffectDraw ball{};drawState(ball);ball.texture=texture;ball.sampler=sampler();
    for(size_t row=0;row<4;++row){ball.constants[row][row]=1;ball.constants[4+row][row]=1;}
    ball.constants[8]={0,0,.5f,1};ball.vertices={{{-.5f,-.5f,0,0},{.5f,-.5f,1,0},{.5f,.5f,1,1},{-.5f,.5f,0,1}}};
    ParticleDraw particle{};drawState(particle);particle.texture=texture;particle.sampler=sampler();
    for(size_t row=0;row<4;++row){particle.constants[row][row]=1;particle.constants[4+row][row]=1;}
    particle.constants[8]={1,1,1,1};particle.constants[9][0]=1;particle.constants[15]={255,255,255,255};
    particle.constants[16]={1,1,1,63.75f};particle.constants[17]={1,1,1000,0};particle.constants[19][3]=255;particle.constants[25][0]=-1;
    for(UINT i=0;i<4;++i){ParticleVertex v{};v.position={0,0,.5f,0};v.size={.5f,0,1};v.color={16.0f/255,16.0f/255,16.0f/255,0};v.originalVertexId=float(i);particle.vertices.push_back(v);}
    auto sentinel=constantBuffer(Probe::device(b),65536);const std::array<UINT,3> vsSlots{0,1,3};const std::array<UINT,3> psSlots{0,1,2};
    const auto seed=[&](UINT count) {
        for(UINT slot:vsSlots)bindActual(c1.Get(),true,slot,{sentinel,16+slot*16,count});
        for(UINT slot:psSlots)bindActual(c1.Get(),false,slot,{sentinel,80+slot*16,count});
    };
    const auto snapshot=[&] {
        std::array<ActualView,6> result;for(size_t i=0;i<3;++i){result[i]=actual(c1.Get(),true,vsSlots[i]);result[i+3]=actual(c1.Get(),false,psSlots[i]);}return result;
    };
    // Pin the device's true plain/full Get1 tuple before helper normalization.
    auto* raw=sentinel.Get();context->VSSetConstantBuffers(0,1,&raw);const auto full=actual(c1.Get(),true,0);
    need(full.buffer==sentinel&&full.first==0&&full.count==4096,"Actual legacy full GetConstantBuffers1 tuple changed");
    const auto captured=detail::captureConstantBuffer(context,c1.Get(),detail::ConstantBufferStage::Vertex,0);
    need(captured.buffer==full.buffer&&captured.view==detail::ConstantBufferView::Ranged&&
        captured.firstConstant==full.first&&captured.numConstants==full.count,"Actual legacy full tuple was not preserved exactly");
    b.clearTarget(color,{0,0,0,0});b.clearDepthTarget(depth,0,0);b.drawImmediate(color,depth,immediate);
    need(actual(c1.Get(),true,0)==full,"Real immediate draw lost the canonical legacy full tuple");
    const bool offsets=supportsOffsets(Probe::device(b));
    if(!offsets)std::fprintf(stderr,"[ARENA FIXTURE] actual device lacks range offsetting; full/null restore still checked\n");
    for(UINT count:{16u,0u})for(unsigned family=0;family<3&&offsets;++family) {
        seed(count);const auto before=snapshot();
        const auto zero=detail::captureConstantBuffer(context,c1.Get(),detail::ConstantBufferStage::Vertex,0);
        need(zero.view==detail::ConstantBufferView::Ranged&&zero.firstConstant==16&&zero.numConstants==count,
            "Explicit nonzero-offset/zero-length range was normalized to a full binding");
        const auto submit=[&]{if(family==0)b.drawImmediate(color,depth,immediate);else if(family==1)b.drawBallEffect(color,depth,ball);else b.drawParticles(color,depth,particle);};
        b.clearTarget(color,{0,0,0,0});b.clearDepthTarget(depth,0,0);submit();need(snapshot()==before,"Successful direct draw lost actual VS/PS range tuple");
        const auto pixels=b.readbackTarget(color);need(std::any_of(pixels.begin(),pixels.end(),[](uint8_t v){return v!=0;}),"Qualified direct draw produced no actual GPU pixels");
        const size_t injectedBefore=calls.injections;
        calls.failPostBind=true;rejects(submit,"Injected post-bind constant setter error did not unwind");calls.failPostBind=false;
        need(!calls.failPostBind&&calls.injections==injectedBefore+1,"Rejected direct submit did not execute exactly one post-bind constant setter fault");
        need(snapshot()==before,"Direct draw error lost actual retained ranges");need(b.readbackTarget(color)==pixels,"Rejected direct draw modified prior actual pixels");
        // Do not rebind the saved view for this oracle: sample the actual
        // restored VS0 directly. Distinct source words expose both offset and
        // zero-length extent errors that pointer-only getters would miss.
        GPUQueue restoredGPU(b,16,1);restoredGPU.submitAlreadyBound(0);const auto restoredPixels=restoredGPU.finish();
        const std::array<uint32_t,4> expected=count?std::array<uint32_t,4>{0x13579BDFu+64,0x13579BDFu+67,0x13579BDFu+68,0}:
            std::array<uint32_t,4>{0,0,0,0};
        need(restoredPixels.size()==1&&restoredPixels.front()==expected,"Actual restored constant range produced the wrong independent GPU bytes");
        b.bindTargets({color,nullptr,nullptr,nullptr},depth);
    }
    // Null bindings must remain null, separately from nonnull zero-length views.
    ID3D11Buffer* empty=nullptr;context->VSSetConstantBuffers(0,1,&empty);
    const auto nullActual=actual(c1.Get(),true,0);
    const auto null=detail::captureConstantBuffer(context,c1.Get(),detail::ConstantBufferStage::Vertex,0);
    need(!null.buffer&&null.view==detail::ConstantBufferView::Ranged&&null.firstConstant==nullActual.first&&null.numConstants==nullActual.count,
        "Null actual binding tuple was confused with a nonnull zero-length range");
    b.drawImmediate(color,depth,immediate);need(!actual(c1.Get(),true,0).buffer,"Direct restoration fabricated a prior null buffer");
    calls.finish();
}
// Exercise the production draw, not only the publisher: distinct real VS/PS
// payloads, unchanged input snapshot, repeat reuse and queued packed color/D/S
// across an actual arena wrap, then drop ALL test/backend bank owners before
// completing readback. The generic queuedWindows oracle covers both new banks
// independently (including all defined rounded tail and out-of-window reads).
void immediateQueuedDraws(bool hardware,bool forceLegacy) {
    NativeBackend b(!hardware);auto c1=Probe::context1(b);if(forceLegacy)Probe::forceLegacy(b);Calls calls(b);
    constexpr UINT count=259;auto color=b.createTarget(count,1,TargetFormat::RGB10A2);auto depth=b.createDepthTarget(count,1);
    b.bindTargets({color,nullptr,nullptr,nullptr},depth);b.setViewport({0,0,float(count),1,0,1});
    b.clearTarget(color,{0,0,0,0});b.clearDepthTarget(depth,0,0x5A);
    ImmediateDraw d{};d.viewport={0,0,count,1,0,0x3F800000};d.depthEnable=d.depthWrite=1;d.depthCompare=7;
    d.viewportEnable=d.halfPixelOffset=1;d.colorMask=15;d.multisampleMask=0xFFFFFFFF;d.blendWord=0x10106;d.blendEnable=d.alphaTest=0;
    const std::array<uint8_t,4> texel{255,255,255,255};d.texture=b.createTexture(1,1,TextureFormat::RGBA8,texel);d.sampler=sampler();
    for(size_t row=0;row<4;++row)d.constants[row][row]=1;d.constants[4]={.25f,.5f,.75f,1};d.alphaReference=128;
    const auto geometry=[&](UINT column) {
        const float left=2.0f*float(column)/float(count)-1.0f,right=2.0f*float(column+1)/float(count)-1.0f;
        d.vertices={{{left,1,.5f},1,{0,0,0,0}},{{left,-1,.5f},1,{0,0,0,0}},
                    {{right,1,.5f},1,{0,0,0,0}},{{right,-1,.5f},1,{0,0,0,0}}};
    };
    geometry(0);const auto originalConstants=d.constants;b.drawImmediate(color,depth,d);
    {
        const auto vs=Probe::observe(b,Family::ImmediateVertex).slice,ps=Probe::observe(b,Family::ImmediatePixel).slice;
        descriptor(b,vs,80);descriptor(b,ps,16);Probe::require(b,Family::ImmediateVertex,vs);Probe::require(b,Family::ImmediatePixel,ps);
        const auto expectedVS=bytes(originalConstants);const std::array<uint32_t,4> expectedPS{0x10106,0x3F008081,0,0};
        const auto expectedPSBytes=bytes(expectedPS);
        const auto actualVS=Probe::read(b,Family::ImmediateVertex,vs),actualPS=Probe::read(b,Family::ImmediatePixel,ps);
        need(std::equal(actualVS.begin(),actualVS.end(),expectedVS.begin(),expectedVS.end())&&
             std::equal(actualPS.begin(),actualPS.end(),expectedPSBytes.begin(),expectedPSBytes.end()),"Actual immediate VS80/PS16 bytes differ from independent inputs");
        need(d.constants==originalConstants,"Native publication changed the caller's immediate constants");
        for(const auto& slice:{vs,ps})if(slice.arena) {
            const auto physical=bufferBytes(Probe::device(b),Probe::context(b),slice.binding.buffer.Get());
            const size_t begin=slice.binding.firstConstant*16,window=slice.binding.numConstants*16;
            need(std::all_of(physical.begin()+begin+slice.logicalBytes,physical.begin()+begin+window,[](uint8_t v){return v==0;}),"Immediate defined rounded tail contains stale bytes");
        }
    }
    b.clearTarget(color,{0,0,0,0});b.clearDepthTarget(depth,0,0x5A);
    const uint64_t startVSDiscard=Probe::observe(b,Family::ImmediateVertex).discardMaps;
    const uint64_t startPSDiscard=Probe::observe(b,Family::ImmediatePixel).discardMaps;
    const uint64_t startVSAppend=Probe::observe(b,Family::ImmediateVertex).noOverwriteMaps;
    const uint64_t startPSAppend=Probe::observe(b,Family::ImmediatePixel).noOverwriteMaps;
    for(UINT i=0;i<count;++i) {
        geometry(i);d.constants[4][0]=float(i%4)*.25f;d.alphaReference=i%256;b.drawImmediate(color,depth,d);
        if(i%32==0) {
            const auto beforeVS=Probe::observe(b,Family::ImmediateVertex),beforePS=Probe::observe(b,Family::ImmediatePixel);const size_t maps=calls.constantMaps;
            b.drawImmediate(color,depth,d);const auto afterVS=Probe::observe(b,Family::ImmediateVertex),afterPS=Probe::observe(b,Family::ImmediatePixel);
            need(calls.constantMaps==maps&&detail::constantBufferBindingMatches(beforeVS.slice.binding,afterVS.slice.binding)&&
                 detail::constantBufferBindingMatches(beforePS.slice.binding,afterPS.slice.binding)&&
                 beforeVS.slice.generation!=afterVS.slice.generation&&beforePS.slice.generation!=afterPS.slice.generation,
                 "Repeated real immediate draw mapped/replaced byte-identical constants or retained an old generation");
        }
    }
    {
        const auto vs=Probe::observe(b,Family::ImmediateVertex),ps=Probe::observe(b,Family::ImmediatePixel);
        if(vs.slice.arena)need(vs.discardMaps>=startVSDiscard+1&&ps.discardMaps>=startPSDiscard+1&&
            vs.noOverwriteMaps>startVSAppend&&ps.noOverwriteMaps>startPSAppend,"Actual immediate draw queue did not cross append/wrap for both banks");
    }
    // Counters above hold no resource owners. Release these last diagnostic
    // slices too, so only the driver retains buffers across reset/readback.
    auto firstVS=Probe::observe(b,Family::ImmediateVertex).slice,firstPS=Probe::observe(b,Family::ImmediatePixel).slice;
    Probe::resetImmediate(b);
    rejects([&]{Probe::require(b,Family::ImmediateVertex,firstVS);},"Reset retained an old immediate vertex publication");
    rejects([&]{Probe::require(b,Family::ImmediatePixel,firstPS);},"Reset retained an old immediate pixel publication");
    firstVS={};firstPS={};
    const auto pixels=b.readbackTarget(color),depths=b.readbackDepthTarget(depth);
    need(pixels.size()==count*4&&depths.size()==count*8,"Queued immediate readback extent changed");
    constexpr std::array<uint32_t,4> red{0,256,512,767};
    for(UINT i=0;i<count;++i) {
        uint32_t packed{},z{};std::memcpy(&packed,pixels.data()+i*4,4);std::memcpy(&z,depths.data()+i*8,4);
        need(packed==(red[i%4]|(512u<<10)|(767u<<20)|(3u<<30)),"Queued append/wrap/reset changed original immediate packed pixels");
        need(z==0x3F000000&&depths[i*8+4]==0x5A,"Queued append/wrap/reset changed defined immediate depth/stencil");
    }
    std::fprintf(stderr,"[IMMEDIATE ARENA FIXTURE] hardware=%u forced_legacy=%u queued_draws=%u consumed=80/16 queued_pixels=%u\n",unsigned(hardware),unsigned(forceLegacy),count+9,count);
    calls.finish();
}

void immediateDeviceProof(bool hardware,bool forceLegacy) {
    NativeBackend b(!hardware),other(!hardware);if(forceLegacy)Probe::forceLegacy(b);
    for(Family f:{Family::ImmediateVertex,Family::ImmediatePixel}) {
        if(forceLegacy)Probe::forceLegacy(b);
        std::vector<uint8_t> input(Probe::logicalBytes(f));setWord(input,0,0x12345678);
        const auto before=Probe::publish(b,f,input);descriptor(b,before,UINT(input.size()));
        auto foreign=constantBuffer(Probe::device(other),UINT(input.size()));auto bad=before;bad.binding.buffer=foreign;
        rejects([&]{Probe::require(b,f,bad);},"Immediate slice admitted another actual device");
        const auto old=Probe::replace(b,other);rejects([&]{Probe::require(b,f,before);},"Device replacement retained old immediate publication");
        if(forceLegacy)Probe::forceLegacy(b);const auto replacement=Probe::publish(b,f,input);descriptor(b,replacement,UINT(input.size()));
        need(replacement.binding.buffer.Get()!=before.binding.buffer.Get(),"Replacement device reused the old immediate constant owner");
        Probe::restore(b,old);rejects([&]{Probe::require(b,f,replacement);},"Restored old device admitted the foreign immediate bank");
    }
}

}

int main(int argc,char** argv)try {
    need(argc==2||(argc==3&&std::strcmp(argv[2],"--hardware")==0),"Supply analysis/simpsons.pe and optional --hardware");
    std::ifstream input(argv[1],std::ios::binary);const std::vector<uint8_t> image((std::istreambuf_iterator<char>(input)),{});
    need(image.size()==15466496,"Original flat shader image extent differs");const bool hardware=argc==3;
    for(bool legacy:{false,true}) {
        for(Family f:{Family::Shadow,Family::Sky,Family::Rigid})queuedWindows(hardware,f,legacy);
        for(Family f:{Family::ImmediateVertex,Family::ImmediatePixel})queuedWindows(hardware,f,legacy);
        immediateQueuedDraws(hardware,legacy);immediateDeviceProof(hardware,legacy);
        publicCommits(image,hardware,legacy);directRangeRestore(hardware,legacy);
    }
    std::printf("PASS constant buffer arena %s: %zu checks; actual queued wrap/append bytes, guards, Map failure, legacy fallback and ranged restoration\n",hardware?"hardware":"WARP",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL constant buffer arena after %zu checks: %s\n",checks,error.what());return 1;}
