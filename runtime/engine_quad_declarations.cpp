#include "engine_quad_declarations.h"
#include "engine_viewport_surfaces.h"
#include "engine_builtin_textures.h"
#include "engine_driver.h"
#include "engine_scene_copies.h"
#include "engine_shadow_textures.h"
#include "engine_reflection_textures.h"
#include "runtime.h"
#include <array>
#include <cstdio>
#include <unordered_map>

namespace {
using namespace Simpsons;
void need(bool value,const char* why) {if(!value) throw Failure(why);}
struct HostState {
    const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
    HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
};
EngineDriver& driver(uint8_t* base) {
    need(active && active->base==base && active->engineDriver,"Native graphics resource has no driver owner");
    return *active->engineDriver;
}
}
namespace Simpsons {
struct EngineQuadDeclarations::State {
    Runtime& runtime;const uint32_t thread,context;
    Graphics::DeclarationRegistry registry;
    struct Record {uint32_t owner,field;Graphics::DeclarationId declaration;};
    std::unordered_map<uint32_t,Record> records;
    uint32_t cachedId{};
    Graphics::DeclarationId cachedDeclaration{};
    State(Runtime& rt,uint32_t c):runtime(rt),thread(GetCurrentThreadId()),context(c) {}
    void require() const {
        need(active==&runtime && thread==GetCurrentThreadId() && runtime.engineDriver,
             "Native quad declaration accessed outside its runtime/thread");
        runtime.checkRunning();runtime.engineDriver->requireContext(context);
    }
    void typed(uint8_t* base,uint32_t o) const {
        need(o && !(o&3) && uint64_t(o)+0x118<=0x100000000ull,"Invalid original quad owner");
        runtime.pointer(o,0x118,true);
        need(PPC_LOAD_U32(o)==0x820B7170,"Native quad declaration lost its typed owner");
    }
    const Record& find(uint32_t id) const {
        const auto it=records.find(id);need(it!=records.end(),"Unknown or stale native quad declaration ID");
        auto* base=runtime.base;typed(base,it->second.owner);
        need(!runtime.pageAccess[id>>12].load() && PPC_LOAD_U32(it->second.owner+it->second.field)==id,
             "Native quad declaration publication differs");
        return it->second;
    }
    void cached() const {
        auto* base=runtime.base;
        need(cachedId && !runtime.pageAccess[cachedId>>12].load() && PPC_LOAD_U32(0x82D099A0)==cachedId,
             "Native post-effect declaration publication differs");
    }
    void postBase(uint8_t* base,uint32_t owner) const {
        need(owner && !(owner&3) && uint64_t(owner)+0xA8<=0x100000000ull,"Invalid original post-effect base owner");
        runtime.pointer(owner,0xA8,true);
        // Original823CA338/3A8 publish this base vtable before the resource call.
        need(PPC_LOAD_U32(owner)==0x82061698,"Native post-effect declaration lost its base owner");
    }
    void references(Graphics::DeclarationId declaration) const {
        if(cachedId) cached();
        uint32_t expected=cachedId && cachedDeclaration==declaration?1u:0u;
        for(const auto& [id,record]:records) {
            find(id);
            if(record.declaration==declaration) ++expected;
        }
        need(expected && registry.referenceCount(declaration)==expected,
             "Native CPU declaration has an unqualified retained alias");
    }
};
EngineQuadDeclarations::EngineQuadDeclarations(Runtime& rt,uint32_t context):state(std::make_unique<State>(rt,context)) {}
EngineQuadDeclarations::~EngineQuadDeclarations() {
    const auto count=state->records.size()+(state->cachedId?1u:0u);
    if(count) std::fprintf(stderr,"[NATIVE QUAD DECLARATION] terminal release of %zu CPU owners; original effect cleanup incomplete\n",count);
}
void EngineQuadDeclarations::create(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.require();need(base==s.runtime.base,"Native quad declaration memory differs");
    const uint32_t caller=uint32_t(ctx.lr),o=ctx.r31.u32;
    // Two verified BL call sites, before the original instruction updates LR.
    // Do not intercept other users of the SDK's pure CPU declaration builder.
    const uint32_t field=caller==0x826B6BB8?0xA8:(caller==0x826B6BD0?0xAC:0);
    need(field!=0,"Unsupported original native quad declaration creation caller");s.typed(base,o);
    const uint32_t source=field==0xA8?0x820B71C4:0x820B71A0;
    need(ctx.r3.u32==source,"Unqualified original quad declaration source");
    // These exact original stream0/method0 records include every opaque and
    // terminator byte. No broad packed-type or semantic substitution is used.
    constexpr std::array<uint32_t,6> position={0,0x002C23A5,0,0x00FF0000,0xFFFFFFFF,0};
    constexpr std::array<uint32_t,9> textured={0,0x002C23A5,0,8,0x002C23A5,0x00050000,0x00FF0000,0xFFFFFFFF,0};
    const std::span<const uint32_t> words=field==0xA8?std::span<const uint32_t>(position):std::span<const uint32_t>(textured);
    const uint32_t size=uint32_t(words.size()*4);const auto* bytes=s.runtime.pointer(source,size,false);
    for(size_t i=0;i<words.size();++i) need(PPC_LOAD_U32(source+uint32_t(i*4))==words[i],"Original quad declaration bytes differ");
    for(const auto& [id,record]:s.records) {
        need(record.owner==o && record.field!=field,"Concurrent/duplicate quad declaration owner");s.find(id);
    }
    need(s.records.size()==(field==0xA8?0u:1u),"Original quad declaration creation order differs");
    const auto declaration=s.registry.create({bytes,size});
    uint32_t id=0;
    try {
        id=s.runtime.engineDriver->allocateTargetIdentity();
        need(s.records.emplace(id,State::Record{o,field,declaration}).second,"Native quad declaration identity collision");
    } catch(...) {s.registry.release(declaration);throw;}
    ctx.r3.u64=id;ctx.lr=field==0xA8?0x826B6BD0:0x826B6BE4;
    std::fprintf(stderr,"[NATIVE QUAD DECLARATION] owned source=%08X bytes=%u owner=%08X field=%X id=%08X; CPU metadata only, binding guarded\n",source,size,o,field,id);
}
void EngineQuadDeclarations::createPost(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.require();need(base==s.runtime.base,"Native post-effect declaration memory differs");
    need(uint32_t(ctx.lr)==0x823CA354 && ctx.r30.u32==0x82D10000 && ctx.r3.u32==0x82061674,
         "Unsupported original post-effect declaration creation caller/source");
    s.postBase(base,ctx.r31.u32);
    need(!s.cachedId && !PPC_LOAD_U32(0x82D099A0),"Original post-effect declaration cache is already populated");
    constexpr std::array<uint32_t,9> words={0,0x002C23A5,0,8,0x002C23A5,0x00050000,0x00FF0000,0xFFFFFFFF,0};
    const auto* bytes=s.runtime.pointer(0x82061674,36,false);
    for(size_t i=0;i<words.size();++i)
        need(PPC_LOAD_U32(0x82061674+uint32_t(i*4))==words[i],"Original post-effect declaration bytes differ");
    const auto declaration=s.registry.create({bytes,36});
    uint32_t id=0;
    try {id=s.runtime.engineDriver->allocateTargetIdentity();}
    catch(...) {s.registry.release(declaration);throw;}
    s.cachedDeclaration=declaration;s.cachedId=id;
    ctx.r3.u64=id;ctx.lr=0x823CA37C;
    std::fprintf(stderr,"[NATIVE POST DECLARATION] owned source=82061674 bytes=36 global=82D099A0 id=%08X; CPU metadata only, binding guarded\n",id);
}
void EngineQuadDeclarations::release(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.require();need(base==s.runtime.base,"Native quad declaration memory differs");
    if(s.cachedId && ctx.r3.u32==s.cachedId) {
        s.cached();s.postBase(base,ctx.r30.u32);
        need(uint32_t(ctx.lr)==0x823CA3E0 && ctx.r31.u32==0x82D10000,
             "Native post-effect declaration release lacks its original caller");
        s.references(s.cachedDeclaration);
        s.registry.release(s.cachedDeclaration);const auto id=s.cachedId;s.cachedId=0;s.cachedDeclaration={};
        ctx.r3.u64=0;
        std::fprintf(stderr,"[NATIVE POST DECLARATION] released id=%08X; original caller clears its global\n",id);
        return;
    }
    const uint32_t id=ctx.r3.u32;const auto r=s.find(id);
    need(ctx.r31.u32==r.owner && uint32_t(ctx.lr)==(r.field==0xA8?0x826B4D10u:0x826B4D24u),
         "Native quad declaration release lacks its original owner/caller");
    s.references(r.declaration);
    s.registry.release(r.declaration);s.records.erase(id);ctx.r3.u64=0;
    std::fprintf(stderr,"[NATIVE QUAD DECLARATION] released id=%08X; original caller clears its field\n",id);
}
bool EngineQuadDeclarations::owns(uint32_t id) const {state->require();return state->records.contains(id) || (id && id==state->cachedId);}
size_t EngineQuadDeclarations::count() const {state->require();return state->records.size()+(state->cachedId?1u:0u);}
std::shared_ptr<const Graphics::DeclarationRecord> EngineQuadDeclarations::record(uint32_t id) const {
    state->require();
    if(id && id==state->cachedId) {state->cached();return state->registry.record(state->cachedDeclaration);}
    return state->registry.record(state->find(id).declaration);
}
void EngineQuadDeclarations::requireReleased() const {
    need(active==&state->runtime && GetCurrentThreadId()==state->thread,"Native quad declaration retirement has wrong thread/runtime");
    need(state->records.empty() && !state->cachedId && !state->registry.liveCount(),"Original effects retain native CPU declarations");
}
}
void SimpsonsNativeQuadDeclarationCreate(PPCContext& ctx,uint8_t* base) {HostState fp;driver(base).quadDeclarations().create(ctx,base);}
void SimpsonsNativePostDeclarationCreate(PPCContext& ctx,uint8_t* base) {HostState fp;driver(base).quadDeclarations().createPost(ctx,base);}
bool SimpsonsOriginalMeshDeclarationDestroy(PPCContext& ctx,uint8_t* base) {
    constexpr uint32_t owner=0x82D6305C;
    if(uint32_t(ctx.lr)!=0x82441058 || ctx.r30.u32!=owner)return false;
    HostState fp;driver(base).requireContext(PPC_LOAD_U32(0x82D5DA74));
    const auto sp=ctx.r1.u32;
    need(currentContext==&ctx && sp>=0xD0 && !(sp&15),"Original mesh declaration destruction context differs");
    active->pointer(sp,0xD0,false);
    need(PPC_LOAD_U32(sp)==sp+0x70 && PPC_LOAD_U32(sp+0x70)==sp+0xD0 &&
         PPC_LOAD_U32(sp+0x68)==0x82441770 && PPC_LOAD_U32(sp+0xC8)==0x82700ABC,
         "Original mesh declaration destruction lacks its nested release frames");
    const auto count=PPC_LOAD_U32(owner+4),storage=PPC_LOAD_U32(owner);
    const auto remaining=PPC_LOAD_U64(sp+0xC0);
    need(count && count<=PPC_LOAD_U32(owner+8) && count<=UINT32_MAX/4 &&
         remaining && remaining<=count && ctx.r29.u32==4*remaining,
         "Original mesh declaration destruction vector frontier differs");
    active->pointer(storage,4*count,false);
    const auto declaration=ctx.r31.u32;
    need(declaration && !(declaration&3) && ctx.r3.u32==declaration &&
         PPC_LOAD_U32(storage+4*(uint32_t(remaining)-1))==declaration,
         "Original mesh declaration destruction is not the current vector member");
    active->pointer(declaration,0x38,true);
    need(PPC_LOAD_U32(declaration)==0x00100005 && !PPC_LOAD_U32(declaration+4),
         "Original mesh declaration destruction type/reference differs");
    const auto elements=PPC_LOAD_U32(declaration+0x18);
    need(elements<=(UINT32_MAX-0x38)/12,"Original mesh declaration destruction extent overflows");
    active->pointer(declaration,0x38+12*elements,true);
    // Type five's original switch destination is82441220. It only frees the
    // CPU header; skip its unused VdGlobalDevice lookup, retaining the original
    // already-executed prologue, allocator call, and complete epilogue.
    return true;
}
bool SimpsonsNativeGraphicsResourceRelease(PPCContext& ctx,uint8_t* base) {
    HostState fp;auto& d=driver(base);
    if(d.quadDeclarations().owns(ctx.r3.u32)) d.quadDeclarations().release(ctx,base);
    else if(d.viewportSurfaces().owns(ctx.r3.u32)) d.viewportSurfaces().release(ctx,base);
    else if(d.sceneCopies().owns(ctx.r3.u32)) d.sceneCopies().release(ctx,base);
    else if(d.reflectionTextures().owns(ctx.r3.u32)) d.reflectionTextures().release(ctx,base);
    else if(d.builtinTextures().owns(ctx.r3.u32)) throw Simpsons::Failure("Unqualified original built-in image release caller");
    else if(uint32_t(ctx.lr)==0x82700ABC) {
        // Original82701BD8 creates CPU declarations with824458E0 and owns
        // them in this vector. They never became native resource identities.
        // Keep82441708's original atomic reference decrement and82441050's
        // type-five header free; the latter has no device/backing operation.
        constexpr uint32_t owner=0x82D6305C;
        need(currentContext==&ctx && ctx.r30.u32==owner && ctx.r1.u32>=0x80 && !(ctx.r1.u32&15),
             "Original mesh declaration release lacks its vector/frame");
        active->pointer(ctx.r1.u32,0x80,false);
        need(PPC_LOAD_U32(ctx.r1.u32)==ctx.r1.u32+0x80,"Original mesh declaration release stack differs");
        const auto count=PPC_LOAD_U32(owner+4),remaining=ctx.r31.u32,storage=PPC_LOAD_U32(owner);
        need(count && count<=PPC_LOAD_U32(owner+8) && count<=UINT32_MAX/4 &&
             remaining && remaining<=count && ctx.r29.u32==4*remaining,
             "Original mesh declaration release vector frontier differs");
        active->pointer(storage,4*count,false);
        const auto declaration=ctx.r3.u32;
        need(declaration && !(declaration&3) && PPC_LOAD_U32(storage+4*(remaining-1))==declaration,
             "Original mesh declaration release is not the current vector member");
        active->pointer(declaration,0x38,true);
        need(PPC_LOAD_U32(declaration)==0x00100005 && PPC_LOAD_U32(declaration+4),
             "Original mesh declaration type/reference differs");
        const auto elements=PPC_LOAD_U32(declaration+0x18);
        need(elements<=(UINT32_MAX-0x38)/12,"Original mesh declaration extent overflows");
        active->pointer(declaration,0x38+12*elements,true);
        return false;
    }
    else {
        // Native IDs are deliberately unmapped. A mapped guest pointer here
        // identifies another original SDK resource class; log its header so
        // the missing owner can be qualified without treating it as a shadow.
        if(ctx.r3.u32>=0x10000000 && !(ctx.r3.u32&3)) {
            try {
                active->pointer(ctx.r3.u32,64,false);
                std::fprintf(stderr,"[NATIVE GRAPHICS RELEASE FRONTIER] id=%08X caller=%08X vector=%08X/%u r31=%08X r29=%08X words=",
                    ctx.r3.u32,uint32_t(ctx.lr),PPC_LOAD_U32(0x82D6305C),PPC_LOAD_U32(0x82D63060),ctx.r31.u32,ctx.r29.u32);
                for(uint32_t i=0;i<16;++i)std::fprintf(stderr," %08X",PPC_LOAD_U32(ctx.r3.u32+4*i));
                std::fprintf(stderr,"\n");
            } catch(const std::exception&) {}
        }
        d.shadowTextures().release(ctx,base);
    }
    return true;
}
