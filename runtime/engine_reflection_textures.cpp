#include "engine_reflection_textures.h"
#include "engine_driver.h"
#include "engine_scene_copies.h"
#include "engine_effects.h"
#include "engine_shadow_textures.h"
#include "runtime.h"
#include "renderer/native_backend.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace {
using namespace Simpsons;
constexpr std::array<uint32_t,6> faceOrder{0,1,4,5,2,3};
constexpr std::array<uint32_t,3> fields{8,12,16};
constexpr uint8_t untouched=0xD7;
void need(bool ok,const char* why) {if(!ok) throw Failure(why);}
bool overlaps(uint32_t a,uint32_t n,uint32_t b,uint32_t m) {
    return uint64_t(a)<uint64_t(b)+m && uint64_t(b)<uint64_t(a)+n;
}
struct HostState {
    const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
    HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
};
EngineDriver& driver(uint8_t* base) {
    need(active && active->base==base && active->engineDriver,"Native reflection texture has no driver owner");
    return *active->engineDriver;
}
}
namespace Simpsons {
struct EngineReflectionTextures::State {
    Runtime& runtime;Graphics::NativeBackend& backend;const uint32_t context,thread;
    struct Record {
        View view{};
        uint32_t entrySp{};
        std::shared_ptr<Graphics::CubeTexture> cube;
        std::array<std::shared_ptr<Graphics::RenderTarget>,2> companions;
    };
    std::unordered_map<uint32_t,Record> records;
    State(Runtime& rt,Graphics::NativeBackend& b,uint32_t c):runtime(rt),backend(b),context(c),thread(GetCurrentThreadId()) {}
    void require(uint8_t* base) const {
        need(active==&runtime && base==runtime.base && GetCurrentThreadId()==thread,"Native reflection ownership has wrong runtime/thread");
        runtime.checkRunning();runtime.engineDriver->requireContext(context);
        need(PPC_LOAD_U32(0x82D5DA74)==context && !PPC_LOAD_U32(0x82D0CAF8),"Native reflection context publication differs");
        backend.validateSubmissionContext();
    }
    void frame(PPCContext& ctx,uint8_t* base) const {
        require(base);need(currentContext==&ctx && ctx.r1.u32>=0x200 && !(ctx.r1.u32&15),"Native reflection caller stack/context differs");
        runtime.pointer(ctx.r1.u32-0x100,0x200,true);
    }
    void region(uint32_t address,uint32_t size,bool write=true) const {
        need(address && !(address&3) && uint64_t(address)+size<=0x100000000ull,"Invalid original reflection owner bounds");
        runtime.pointer(address,size,write);
    }
    Record& find(uint32_t owner) {
        const auto it=records.find(owner);need(it!=records.end(),"Unknown or stale native reflection owner");return it->second;
    }
    void published(const Record& r,uint8_t* base) const {
        const auto& v=r.view;region(v.owner,0x90);
        need(PPC_LOAD_U32(v.owner)==v.size && !PPC_LOAD_U32(v.owner+4),"Native reflection size/face state changed");
        need(PPC_LOAD_U32(v.owner+0x14)==(v.contextRetained?context:0),"Native reflection retained context field differs");
        for(uint32_t i=0;i<3;++i) if(v.identities[i])
            need(PPC_LOAD_U32(v.owner+fields[i])==v.identities[i] && !runtime.pageAccess[v.identities[i]>>12].load(),
                 "Native reflection texture publication differs");
        if(v.phase!=Phase::Destroying) {
            need(PPC_LOAD_U32(v.owner+0x64)==0x8214E72C && PPC_LOAD_U32(v.owner+0x68)==v.container &&
                 !PPC_LOAD_U32(v.owner+0x6C) && PPC_LOAD_U32(v.owner+0x70)==1024 &&
                 PPC_LOAD_U32(v.owner+0x74)==1024 && !PPC_LOAD_U32(v.owner+0x78),"Original reflection CPU container differs");
            region(v.container,4096);
        }
    }
    void unbound(uint8_t* base,const Record& r) const {
        for(const uint32_t id:r.view.identities) if(id) {
            for(uint32_t i=0;i<4;++i) need(PPC_LOAD_U32(0x82D0CF5C+4*i)!=id,"Native reflection texture remains a color attachment");
            need(PPC_LOAD_U32(0x82D0CF58)!=id,"Native reflection texture remains a depth attachment");
        }
    }
    Record& texture(uint32_t id) {
        for(auto& [owner,r]:records) for(const uint32_t known:r.view.identities) if(id && known==id) return r;
        throw Failure("Unknown or stale native reflection texture identity");
    }
};
EngineReflectionTextures::EngineReflectionTextures(Runtime& rt,Graphics::NativeBackend& b,uint32_t c):state(std::make_unique<State>(rt,b,c)) {}
EngineReflectionTextures::~EngineReflectionTextures() {
    for(auto& [owner,r]:state->records) if(r.view.staging) {
        try {state->runtime.freePhysical(r.view.staging);}
        catch(const std::exception& e) {std::fprintf(stderr,"[NATIVE REFLECTION CUBE] terminal staging cleanup failed: %s\n",e.what());}
    }
    if(!state->records.empty()) std::fprintf(stderr,"[NATIVE REFLECTION CUBE] terminal release of %zu owners; original paired/global cleanup incomplete\n",state->records.size());
}
void EngineReflectionTextures::begin(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.frame(ctx,base);const uint32_t o=ctx.r3.u32,n=ctx.r5.u32;
    need((uint32_t(ctx.lr)==0x826FF198 && n==16) || (uint32_t(ctx.lr)==0x826FF14C && n==256),
         "Unsupported original reflection constructor caller/size");
    need(ctx.r4.u32==s.context && !s.records.contains(o),"Native reflection constructor context/owner differs");
    s.region(o,0x90);need(!(o&15) && !overlaps(o,0x90,ctx.r1.u32-0x100,0x200) &&
         !overlaps(o,0x90,0x82000000,0xEC0000),"Reflection owner aliases original image/stack");
    State::Record r{};r.view.owner=o;r.view.size=n;r.view.context=s.context;r.view.phase=Phase::Prepared;r.entrySp=ctx.r1.u32;
    need(s.records.emplace(o,std::move(r)).second,"Duplicate native reflection owner");
}
void EngineReflectionTextures::retainContext(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.frame(ctx,base);auto& r=s.find(ctx.r31.u32);auto& v=r.view;
    need(v.phase==Phase::Prepared && !v.contextRetained && ctx.r3.u32==s.context &&
         ctx.r29.u32==s.context && ctx.r30.u32==v.size && ctx.r1.u32+0x90==r.entrySp &&
         uint32_t(ctx.lr)==0x8273C2E4,"Original reflection context retain state differs");
    v.container=PPC_LOAD_U32(v.owner+0x68);s.region(v.container,4096);
    need(!overlaps(v.container,4096,v.owner,0x90) && !overlaps(v.container,4096,ctx.r1.u32-0x100,0x200),"Reflection CPU container aliases owner/stack");
    // A native driver dependency lease: stop rejects every live record. Terminal
    // teardown destroys these children before its D3D11 backend. No SDK+3C count.
    v.contextRetained=true;v.phase=Phase::Constructing;s.published(r,base);
    need(PPC_LOAD_U8(v.owner+0x84)==1,"Original reflection enable byte differs");
    ctx.lr=0x8273C2FC;
}
void EngineReflectionTextures::create(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.frame(ctx,base);auto& r=s.find(ctx.r31.u32);auto& v=r.view;s.published(r,base);
    const uint32_t caller=uint32_t(ctx.lr),index=caller==0x8273C328?0:(caller==0x8273C350?1:(caller==0x8273C378?2:3));
    need(index<3 && v.phase==Phase::Constructing && v.contextRetained && !v.identities[index] &&
         (index==0 || v.identities[index-1]),"Original reflection texture allocation order/caller differs");
    const uint32_t n=index==2?v.size/2:v.size;
    need(ctx.r3.u32==n && ctx.r4.u32==n && ctx.r5.u32==(index?1u:6u) && ctx.r6.u32==1 &&
         !ctx.r7.u32 && ctx.r8.u32==0x282801B6 && !ctx.r9.u32 && ctx.r10.u32==(index?3u:0x12u),
         "Unsupported original reflection texture creation profile");
    if(index==0) r.cube=s.backend.createCubeTexture(n);
    else r.companions[index-1]=s.backend.createTarget(n,n,Graphics::TargetFormat::RGB10A2);
    const uint32_t id=s.runtime.engineDriver->allocateTargetIdentity();v.identities[index]=id;ctx.r3.u64=id;
    std::fprintf(stderr,"[NATIVE REFLECTION CUBE] allocated owner=%08X field=%X id=%08X extent=%u faces=%u RGB10A2; initial pixels undefined\n",v.owner,fields[index],id,n,index?1:6);
}
void EngineReflectionTextures::lock(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.frame(ctx,base);auto& r=s.texture(ctx.r3.u32);auto& v=r.view;s.published(r,base);
    need(uint32_t(ctx.lr)==0x8273C3C4 && ctx.r31.u32==v.owner && v.phase==Phase::Constructing &&
         v.completedFaces<6 && !v.staging && v.identities[2] && ctx.r3.u32==v.identities[0] &&
         ctx.r4.u32==faceOrder[v.completedFaces] && !ctx.r5.u32 && ctx.r6.u32==ctx.r1.u32+0x50 &&
         !ctx.r7.u32 && !ctx.r8.u32 && ctx.r27.u32==v.size*v.size*4 && ctx.r28.u32==6-v.completedFaces &&
         ctx.r29.u32==0x821503A0+40*v.completedFaces,"Unsupported original reflection face-lock profile");
    const uint32_t pitch=v.size==16?128:1024,bytes=v.size==16?4096:262144,output=ctx.r6.u32;
    s.region(output,8);need(!overlaps(output,8,v.owner,0x90),"Reflection lock output aliases owner");
    const uint32_t staging=s.runtime.allocatePhysical(0,bytes,PAGE_READWRITE,0,UINT32_MAX,4096);
    need(staging!=0,"Native reflection lock staging allocation failed");
    try {
        need(!overlaps(staging,bytes,v.owner,0x90) && !overlaps(staging,bytes,output,8) &&
             !overlaps(staging,bytes,v.container,4096),"Reflection staging aliases CPU ownership");
        std::memset(s.runtime.pointer(staging,bytes,true),untouched,bytes);
        PPC_STORE_U32(output,pitch);PPC_STORE_U32(output+4,staging);v.staging=staging;
    } catch(...) {s.runtime.freePhysical(staging);throw;}
    // This caller consumes only the two original output words, not a status.
}
void EngineReflectionTextures::unlock(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.frame(ctx,base);auto& r=s.texture(ctx.r3.u32);auto& v=r.view;s.published(r,base);
    need(uint32_t(ctx.lr)==0x8273C3E4 && ctx.r31.u32==v.owner && v.phase==Phase::Constructing &&
         v.completedFaces<6 && v.staging && ctx.r3.u32==v.identities[0] &&
         ctx.r4.u32==faceOrder[v.completedFaces] && !ctx.r5.u32,"Unsupported original reflection face-unlock profile");
    const uint32_t bytes=v.size==16?4096:262144,cleared=v.size*v.size*4,rows=v.size==16?8:256;
    const auto* p=s.runtime.pointer(v.staging,bytes,false);
    for(uint32_t i=0;i<bytes;++i) need(p[i]==(i<cleared?0:untouched),"Original reflection contiguous clear exceeded/differs from qualified coverage");
    // The qualified tiled clear is uniform zero in these logical rows. Gather
    // that owned value snapshot only; undefined lower N16 rows are not uploaded.
    const std::vector<uint8_t> pixels(size_t(v.size)*rows*4,0);
    s.backend.writeCubeRows(r.cube,faceOrder[v.completedFaces],0,rows,pixels);
    s.runtime.freePhysical(v.staging);v.staging=0;++v.completedFaces;
    std::fprintf(stderr,"[NATIVE REFLECTION CUBE] original face=%u clear consumed=%u CPU bytes; uploaded rows=%u/%u\n",faceOrder[v.completedFaces-1],cleared,rows,v.size);
}
void EngineReflectionTextures::commit(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.frame(ctx,base);auto& r=s.find(ctx.r31.u32);auto& v=r.view;s.published(r,base);
    need(v.phase==Phase::Constructing && v.completedFaces==6 && !v.staging &&
         uint32_t(ctx.lr)==0x8273C3FC && ctx.r3.u32==v.owner,"Original reflection construction is incomplete");
    v.quad=PPC_LOAD_U32(v.owner+0x80);v.camera=PPC_LOAD_U32(v.owner+0x60);
    s.region(v.quad,0xA8);s.region(v.camera,PPC_LOAD_U32(0x82CD1B30));
    const uint32_t manager=PPC_LOAD_U32(0x82D08BFC),wrapper=PPC_LOAD_U32(v.quad+0x18);
    need(PPC_LOAD_U32(v.quad)==0x820B7170 && manager && PPC_LOAD_U32(v.quad+0x10)==manager && wrapper==PPC_LOAD_U32(0x82CEFD58),
         "Original reflection borrowed quad differs");
    s.region(wrapper,0x14);const auto effect=s.runtime.engineDriver->effects().viewHeader(PPC_LOAD_U32(wrapper+0x10));
    need(effect.wrapper==wrapper && effect.manager==manager,"Native reflection borrowed quad has no live effect");
    const uint32_t cr=PPC_LOAD_U32(v.camera+0x60),dr=PPC_LOAD_U32(v.camera+0x64),offset=PPC_LOAD_U32(0x82E3DC94);
    s.region(cr,PPC_LOAD_U32(0x82CD1E28));s.region(dr,PPC_LOAD_U32(0x82CD1E28));
    bool alphaOne=false;const auto color=s.runtime.engineDriver->color(PPC_LOAD_U32(cr+offset),alphaOne);
    const auto depth=s.runtime.engineDriver->depth(PPC_LOAD_U32(dr+offset));
    need(color && depth && color->width==v.size && color->height==v.size && depth->pixelWidth()==v.size &&
         depth->pixelHeight()==v.size && !alphaOne && PPC_LOAD_U32(v.camera+0x14)==1 &&
         PPC_LOAD_U32(v.camera+0x80)==0x3F800000 && PPC_LOAD_U32(v.camera+0x84)==0x43C80000 &&
         !PPC_LOAD_U32(v.camera+PPC_LOAD_U32(0x82CED790)),"Original reflection camera ownership/profile differs");
    v.phase=Phase::Ready;
    std::fprintf(stderr,"[NATIVE REFLECTION CUBE] constructor complete owner=%08X size=%u camera=%08X quad=%08X; context lease and three native resources owned\n",v.owner,v.size,v.camera,v.quad);
}
void EngineReflectionTextures::destroyBegin(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.frame(ctx,base);auto& r=s.find(ctx.r3.u32);s.published(r,base);s.unbound(base,r);
    need(r.view.phase==Phase::Ready && r.view.contextRetained && !r.view.staging,"Reflection destruction requires a completed unlocked owner");
    need(PPC_LOAD_U32(r.view.owner+0x60)==r.view.camera && PPC_LOAD_U32(r.view.owner+0x80)==r.view.quad,"Reflection destructor CPU ownership differs");
    r.view.phase=Phase::Destroying;
}
void EngineReflectionTextures::releaseContext(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.frame(ctx,base);auto& r=s.find(ctx.r31.u32);s.published(r,base);
    need(r.view.phase==Phase::Destroying && r.view.contextRetained && ctx.r3.u32==s.context,"Reflection context release lacks its retained owner");
    r.view.contextRetained=false;ctx.lr=0x8273C02C; // Original following store clearsO+14.
}
void EngineReflectionTextures::release(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.frame(ctx,base);auto& r=s.texture(ctx.r3.u32);auto& v=r.view;s.published(r,base);s.unbound(base,r);
    const uint32_t caller=uint32_t(ctx.lr),index=caller==0x8273C040?0:(caller==0x8273C054?1:(caller==0x8273C068?2:3));
    need(index<3 && v.phase==Phase::Destroying && !v.contextRetained && ctx.r31.u32==v.owner &&
         v.identities[index]==ctx.r3.u32 && (index==0 || !v.identities[index-1]),"Original reflection texture release order differs");
    if(index==0) r.cube.reset();else r.companions[index-1].reset();
    v.identities[index]=0;ctx.r3.u64=0; // One owned resource reference; caller clears its field.
}
void EngineReflectionTextures::destroyCommit(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.frame(ctx,base);auto& r=s.find(ctx.r31.u32);auto& v=r.view;s.published(r,base);
    need(v.phase==Phase::Destroying && !v.contextRetained && !v.staging &&
         v.identities==std::array<uint32_t,3>{} && uint32_t(ctx.lr)==0x8273C07C,"Original reflection destruction is incomplete");
    for(uint32_t field:{8u,12u,16u,20u}) need(!PPC_LOAD_U32(v.owner+field),"Original reflection destructor did not clear resource fields");
    std::fprintf(stderr,"[NATIVE REFLECTION CUBE] paired owner=%08X released context/cube/two2D; original direct camera/container cleanup retained; attached raster/frame cleanup remains separate\n",v.owner);
    s.records.erase(v.owner);
}
bool EngineReflectionTextures::owns(uint32_t id) const {
    state->require(state->runtime.base);if(!id) return false;
    for(const auto& [o,r]:state->records) if(std::find(r.view.identities.begin(),r.view.identities.end(),id)!=r.view.identities.end()) return true;
    return false;
}
size_t EngineReflectionTextures::count() const {state->require(state->runtime.base);return state->records.size();}
size_t EngineReflectionTextures::leaseCount() const {
    state->require(state->runtime.base);size_t n=0;for(const auto& [o,r]:state->records) n+=r.view.contextRetained;return n;
}
EngineReflectionTextures::View EngineReflectionTextures::view(uint32_t o) const {
    state->require(state->runtime.base);auto& r=state->find(o);state->published(r,state->runtime.base);return r.view;
}
std::shared_ptr<Graphics::CubeTexture> EngineReflectionTextures::cube(uint32_t o) const {
    need(view(o).phase==Phase::Ready,"Reflection cube is not ready");return state->find(o).cube;
}
std::shared_ptr<Graphics::RenderTarget> EngineReflectionTextures::companion(uint32_t o,uint32_t index) const {
    need(index<2 && view(o).phase==Phase::Ready,"Reflection companion is not ready");return state->find(o).companions[index];
}
std::vector<uint8_t> EngineReflectionTextures::readbackFace(uint32_t o,uint32_t face) {return state->backend.readbackCubeFace(cube(o),face);}
void EngineReflectionTextures::requireReleased() const {
    need(active==&state->runtime && GetCurrentThreadId()==state->thread,"Reflection retirement has wrong runtime/thread");
    need(state->records.empty(),"Original reflection owners retain native context/resources at driver stop");
}
}
#define REFLECTION_HOOK(name,method) void name(PPCContext& c,uint8_t* b) {HostState fp;driver(b).reflectionTextures().method(c,b);}
REFLECTION_HOOK(SimpsonsNativeReflectionBegin,begin)
REFLECTION_HOOK(SimpsonsNativeReflectionRetain,retainContext)
REFLECTION_HOOK(SimpsonsNativeReflectionLock,lock)
REFLECTION_HOOK(SimpsonsNativeReflectionUnlock,unlock)
REFLECTION_HOOK(SimpsonsNativeReflectionCommit,commit)
REFLECTION_HOOK(SimpsonsNativeReflectionDestroyBegin,destroyBegin)
REFLECTION_HOOK(SimpsonsNativeReflectionReleaseContext,releaseContext)
REFLECTION_HOOK(SimpsonsNativeReflectionDestroyCommit,destroyCommit)
#undef REFLECTION_HOOK
void SimpsonsNativeEngineTextureCreate(PPCContext& c,uint8_t* b) {
    HostState fp;auto& d=driver(b);const auto lr=uint32_t(c.lr);
    if(lr==0x8273C328 || lr==0x8273C350 || lr==0x8273C378) d.reflectionTextures().create(c,b);
    else if(lr==0x823F7354) d.createSnapshotTexture(c,b);
    else if(lr==0x823C75C8 || lr==0x823C75F4) d.sceneCopies().create(c,b);
    else d.shadowTextures().create(c,b);
}
