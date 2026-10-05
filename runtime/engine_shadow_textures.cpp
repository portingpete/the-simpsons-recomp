#include "engine_shadow_textures.h"
#include "engine_driver.h"
#include "runtime.h"
#include "renderer/native_backend.h"
#include "renderer/engine_state.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace {
using namespace Simpsons;
constexpr uint32_t stagingBytes=8192,pitch=256,pixelRowBytes=128;
void need(bool value,const char* why) {if(!value) throw Failure(why);}
bool overlaps(uint32_t a,uint32_t an,uint32_t b,uint32_t bn) {
    return uint64_t(a)<uint64_t(b)+bn && uint64_t(b)<uint64_t(a)+an;
}
struct HostState {
    const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
    HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
};
EngineShadowTextures& textures(uint8_t* base) {
    need(active && active->base==base && active->engineDriver,"Native shadow texture hook has no driver owner");
    return active->engineDriver->shadowTextures();
}
}
namespace Simpsons {
struct EngineShadowTextures::State {
    Runtime& runtime;
    Graphics::NativeBackend& backend;
    const uint32_t thread,context;
    struct Record {
        View view;
        std::shared_ptr<Graphics::DepthTarget> depth;
        std::shared_ptr<Graphics::Texture> texture;
    };
    std::unordered_map<uint32_t,Record> records;
    struct Camera {
        uint32_t owner{},camera{},frame{},color{},depth{},colorId{},depthId{};
        std::weak_ptr<Graphics::RenderTarget> colorLease;
        std::weak_ptr<Graphics::DepthTarget> depthLease;
    };
    std::array<Camera,2> cameras;
    uint64_t scissors{},copies{};
    State(Runtime& rt,Graphics::NativeBackend& graphics,uint32_t c):
        runtime(rt),backend(graphics),thread(GetCurrentThreadId()),context(c) {}
    void require(uint8_t* base) const {
        need(active==&runtime && base==runtime.base && thread==GetCurrentThreadId(),
            "Native shadow texture accessed outside its runtime/thread");
        runtime.checkRunning();
        need(bool(runtime.engineDriver),"Native shadow texture lost its driver");
        runtime.engineDriver->requireContext(context);
        need(PPC_LOAD_U32(0x82D5DA74)==context && !PPC_LOAD_U32(0x82D0CAF8),
            "Native shadow texture context/SDK device differs");
    }
    void typed(uint8_t* base,uint32_t owner) const {
        need(owner && !(owner&3) && uint64_t(owner)+0x6C0<=0x100000000ull,
            "Invalid original shadows owner address");
        runtime.pointer(owner,0x6C0,true);
        need(PPC_LOAD_U32(owner)==0x8214E518,"Native shadow texture lost its original typed owner");
    }
    Camera cameraSnapshot(uint8_t* base,uint32_t owner,uint32_t slot) const {
        typed(base,owner);const auto c=PPC_LOAD_U32(owner+0x5B4+4*slot);
        need(c && !(c&3),"Original shadows camera is absent or unaligned");
        runtime.pointer(c,0x8C,false);
        need(PPC_LOAD_U32(c)==0x04000000 && PPC_LOAD_U32(c+0x10)==0x823D2940 &&
             PPC_LOAD_U32(c+0x14)==2 && PPC_LOAD_U32(c+0x18)==0x823D1100 && PPC_LOAD_U32(c+0x1C)==0x823D1160,
             "Original shadows camera type/projection/callbacks changed");
        const auto f=PPC_LOAD_U32(c+4),color=PPC_LOAD_U32(c+0x60),depth=PPC_LOAD_U32(c+0x64);
        need(f && color && depth && color!=depth,"Original shadows camera lost its frame/attachment pair");
        runtime.pointer(f,0xA4,false);runtime.pointer(color,0x34,false);runtime.pointer(depth,0x34,false);
        need(!PPC_LOAD_U32(f+4) && !PPC_LOAD_U32(f+0x98) && !PPC_LOAD_U32(f+0x9C) && PPC_LOAD_U32(f+0xA0)==f &&
             PPC_LOAD_U32(f+0x90)==c+8 && PPC_LOAD_U32(f+0x94)==c+8 && PPC_LOAD_U32(c+8)==f+0x90 && PPC_LOAD_U32(c+0xC)==f+0x90,
             "Original shadows frame/camera attachment changed");
        const auto extension=PPC_LOAD_U32(0x82E3DC94);
        need(extension>=0x34 && !(extension&3) && uint64_t(extension)+0x20<=PPC_LOAD_U32(0x82CD1E28),
             "Original shadows raster plugin bounds changed");
        for(const auto r:{color,depth}) {
            runtime.pointer(r,extension+0x20,false);
            need(PPC_LOAD_U32(r)==r && PPC_LOAD_U32(r+0xC)==1024 && PPC_LOAD_U32(r+0x10)==1024 &&
                 PPC_LOAD_U8(r+0x20)==(r==color?5:1),"Original shadows root raster profile changed");
        }
        const auto colorId=PPC_LOAD_U32(color+extension),depthId=PPC_LOAD_U32(depth+extension);
        bool alpha{};const auto colorLease=runtime.engineDriver->color(colorId,alpha);const auto depthLease=runtime.engineDriver->depth(depthId);
        need(colorLease && !alpha && depthLease && colorLease->width==1024 && colorLease->height==1024 &&
             depthLease->pixelWidth()==1024 && depthLease->pixelHeight()==1024,"Original shadows camera native backing differs");
        return {owner,c,f,color,depth,colorId,depthId,colorLease,depthLease};
    }
    Record& find(uint8_t* base,uint32_t id) {
        const auto it=records.find(id);
        if(it==records.end()) {
            char why[192];std::snprintf(why,sizeof(why),"Unknown or stale native shadow texture ID=%08X caller=%08X",
                id,currentContext?uint32_t(currentContext->lr):0);throw Failure(why);
        }
        auto& record=it->second;typed(base,record.view.owner);
        need(PPC_LOAD_U32(record.view.owner+record.view.field)==id && !runtime.pageAccess[id>>12].load(),
            "Native shadow texture identity/publication differs");
        return record;
    }
    void unbound(uint8_t* base,uint32_t id) const {
        for(uint32_t i=0;i<4;++i) need(PPC_LOAD_U32(0x82D0CF5C+4*i)!=id,
            "Native shadow texture remains a color attachment");
        need(PPC_LOAD_U32(0x82D0CF58)!=id,"Native shadow texture remains a depth attachment");
        // Future shader binding/retention must acquire an explicit owner lease;
        // it is not enabled by this allocation-only adapter.
    }
};
EngineShadowTextures::EngineShadowTextures(Runtime& rt,Graphics::NativeBackend& backend,uint32_t context):
    state(std::make_unique<State>(rt,backend,context)) {}
EngineShadowTextures::~EngineShadowTextures() {
    for(auto& [id,record]:state->records) {
        if(record.view.staging) {
            try {state->runtime.freePhysical(record.view.staging);record.view.staging=0;}
            catch(const std::exception& error) {std::fprintf(stderr,"[NATIVE SHADOW TEXTURE] terminal staging release %08X failed: %s\n",id,error.what());}
        }
    }
    if(!state->records.empty()) std::fprintf(stderr,"[NATIVE SHADOW TEXTURE] terminal release of %zu native owners; original shadows cleanup incomplete\n",state->records.size());
}
void EngineShadowTextures::create(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.require(base);
    const uint32_t caller=uint32_t(ctx.lr),owner=ctx.r31.u32;
    const uint32_t field=caller==0x82706610?0xF0:(caller==0x8270663C?0xF4:(caller==0x82706670?0xFC:0));
    if(!field) {
        char why[256];std::snprintf(why,sizeof(why),"Unimplemented native shadow texture creation caller=%08X extent=%ux%u usage=%08X format=%08X",
            caller,ctx.r3.u32,ctx.r4.u32,ctx.r7.u32,ctx.r8.u32);throw Failure(why);
    }
    s.typed(base,owner);
    const bool border=field==0xFC;
    const uint32_t extent=border?32:1024,format=border?0x18280086:0x1A220197;
    need(ctx.r3.u32==extent && ctx.r4.u32==extent && ctx.r5.u32==1 && ctx.r6.u32==1 &&
         ctx.r7.u32==(border?0u:2u) && ctx.r8.u32==format && !ctx.r9.u32 && ctx.r10.u32==3,
         "Unsupported original shadow texture creation profile");
    need(ctx.r1.u32>=0x100 && !(ctx.r1.u32&15),"Invalid original shadow texture caller stack");
    need(!overlaps(owner,0x6C0,ctx.r1.u32-0x100,0x1B0),"Shadows owner overlaps caller stack");
    size_t previous=0;
    for(const auto& [id,record]:s.records) {
        need(record.view.owner==owner,"Concurrent typed shadows texture owners are unqualified");
        need(record.view.field!=field,"Original shadows texture field already has an owner");
        s.find(base,id);++previous;
    }
    need(previous==(field==0xF0?0u:(field==0xF4?1u:2u)),"Original shadows texture allocation order differs");
    const uint32_t first=PPC_LOAD_U32(owner+0x5B4),second=PPC_LOAD_U32(owner+0x5B8);
    need(first && second && first!=second,"Original shadows cameras are not established");
    if(field==0xF0) {
        // Both original 82704C68 calls and O+5B4/+5B8 stores have completed.
        // Capture provenance here, never by accepting a later readable camera.
        s.cameras={s.cameraSnapshot(base,owner,0),s.cameraSnapshot(base,owner,1)};
        need(s.cameras[0].colorId!=s.cameras[1].colorId && s.cameras[0].depthId!=s.cameras[1].depthId,
             "Original shadows cameras share attachment identities");
    }
    if(border) need(PPC_LOAD_U32(owner+0xF8)==PPC_LOAD_U32(0x82D0CF84),"Original shadows borrowed depth-copy role differs");
    State::Record record{{0,owner,field,extent,extent,format,0,Phase::Allocated},{},{}};
    if(border) record.texture=s.backend.createWritableTexture(extent,extent,Graphics::TextureFormat::RGBA8);
    else record.depth=s.backend.createDepthTarget(extent,extent);
    const uint32_t id=s.runtime.engineDriver->allocateTargetIdentity();record.view.identity=id;
    need(s.records.emplace(id,std::move(record)).second,"Native shadow texture identity collision");
    ctx.r3.u64=id; // The original caller publishes O+F0/F4/FC itself.
    std::fprintf(stderr,"[NATIVE SHADOW TEXTURE] allocated owner=%08X field=%X id=%08X extent=%ux%u format=%08X; pixels uninitialized\n",
        owner,field,id,extent,extent,format);
}
void EngineShadowTextures::lock(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.require(base);auto& r=s.find(base,ctx.r3.u32);
    need(uint32_t(ctx.lr)==0x8270668C && ctx.r31.u32==r.view.owner && !ctx.r4.u32 &&
         !ctx.r6.u32 && !ctx.r7.u32 && uint64_t(ctx.r1.u32)+0x50==ctx.r5.u32,
         "Unsupported original shadow texture lock ABI");
    need(r.view.field==0xFC && r.view.phase==Phase::Allocated && !r.view.staging && bool(r.texture),
         "Native shadow texture lock requires its unuploaded border resource");
    const uint32_t output=ctx.r5.u32;s.runtime.pointer(output,8,true);
    need(!overlaps(output,8,r.view.owner,0x6C0),"Shadow lock output aliases its typed owner");
    // CPU-only adapter staging uses the runtime's real mapped allocation. It
    // represents neither a console texture header nor tiled/GPU storage.
    const uint32_t staging=s.runtime.allocatePhysical(0,stagingBytes,PAGE_READWRITE,0,UINT32_MAX,4096);
    need(staging!=0,"Native shadow texture CPU staging allocation failed");
    try {
        s.runtime.pointer(staging,stagingBytes,true);
        need(!overlaps(staging,stagingBytes,output,8) && !overlaps(staging,stagingBytes,r.view.owner,0x6C0),
            "Shadow lock staging aliases original caller storage");
        PPC_STORE_U32(output,pitch);PPC_STORE_U32(output+4,staging);
        r.view.staging=staging;r.view.phase=Phase::Locked;ctx.r3.u64=staging;
    } catch(...) {s.runtime.freePhysical(staging);throw;}
    std::fprintf(stderr,"[NATIVE SHADOW TEXTURE] locked id=%08X staging=%08X bytes=%u pitch=%u; original CPU pixel writes retained\n",
        r.view.identity,staging,stagingBytes,pitch);
}
void EngineShadowTextures::unlock(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.require(base);auto& r=s.find(base,ctx.r3.u32);
    need(uint32_t(ctx.lr)==0x827066F8 && ctx.r31.u32==r.view.owner && !ctx.r4.u32 &&
         r.view.field==0xFC && r.view.phase==Phase::Locked && r.view.staging && bool(r.texture),
         "Unsupported original shadow texture unlock state/ABI");
    const auto* source=s.runtime.pointer(r.view.staging,stagingBytes,false);
    std::array<uint8_t,4096> pixels{};
    for(uint32_t y=0;y<32;++y) {
        for(uint32_t x=0;x<pitch;++x) {
            const bool visible=x<pixelRowBytes;
            const uint8_t expected=visible && (y==0 || y==31 || x<4 || x>=124)?0xFF:0;
            need(source[y*pitch+x]==expected,"Original shadow border pixels/padding differ from the qualified procedural initialization");
        }
        std::memcpy(pixels.data()+y*pixelRowBytes,source+y*pitch,pixelRowBytes);
    }
    s.backend.writeTexture(r.texture,pixels); // Owns the CPU byte snapshot on return.
    s.runtime.freePhysical(r.view.staging);r.view.staging=0;r.view.phase=Phase::Uploaded;
    // The original unlock has no stable return-value contract used by this caller.
    std::fprintf(stderr,"[NATIVE SHADOW TEXTURE] uploaded id=%08X original32x32 border:124 white/900 black pixels;8192 CPU bytes consumed,4096 GPU bytes\n",r.view.identity);
}
void EngineShadowTextures::release(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.require(base);const uint32_t id=ctx.r3.u32;auto& r=s.find(base,id);
    const uint32_t caller=r.view.field==0xF0?0x827057AC:(r.view.field==0xF4?0x827057C0:0x827057D4);
    need(uint32_t(ctx.lr)==caller && ctx.r31.u32==r.view.owner && r.view.phase!=Phase::Locked && !r.view.staging,
         "Native shadow texture release lacks its original unlocked owner");
    s.unbound(base,id);
    s.records.erase(id);ctx.r3.u64=0; // Fresh resources have one owned reference; no native retain path is enabled.
    if(s.records.empty())s.cameras={};
    std::fprintf(stderr,"[NATIVE SHADOW TEXTURE] released original owned texture id=%08X; caller clears its field\n",id);
}
size_t EngineShadowTextures::count() const {state->require(state->runtime.base);return state->records.size();}
EngineShadowTextures::View EngineShadowTextures::view(uint32_t id) const {
    state->require(state->runtime.base);return state->find(state->runtime.base,id).view;
}
std::shared_ptr<Graphics::DepthTarget> EngineShadowTextures::depth(uint32_t id) const {
    state->require(state->runtime.base);if(!state->records.contains(id)) return {};
    return state->find(state->runtime.base,id).depth;
}
std::shared_ptr<Graphics::Texture> EngineShadowTextures::texture(uint32_t id) const {
    state->require(state->runtime.base);auto& r=state->find(state->runtime.base,id);
    need(r.view.phase==Phase::Uploaded && r.texture,"Native shadow texture has no uploaded color pixels");return r.texture;
}
std::vector<uint8_t> EngineShadowTextures::readback(uint32_t id) {
    state->require(state->runtime.base);auto& r=state->find(state->runtime.base,id);
    if(r.depth) return state->backend.readbackDepthTarget(r.depth);
    return state->backend.readback(texture(id));
}
bool EngineShadowTextures::ownsCamera(uint32_t camera) const {
    auto& s=*state;auto* base=s.runtime.base;s.require(base);
    if(!camera || s.records.size()!=3)return false;
    for(uint32_t slot=0;slot<2;++slot) {
        const auto& before=s.cameras[slot];if(before.camera!=camera)continue;
        for(const auto& [id,record]:s.records) {
            s.find(base,id);need(record.view.owner==before.owner && record.view.phase!=Phase::Locked,
                "Original shadows camera has an incomplete texture owner");
        }
        const auto now=s.cameraSnapshot(base,before.owner,slot);
        need(now.camera==before.camera && now.frame==before.frame && now.color==before.color && now.depth==before.depth &&
             now.colorId==before.colorId && now.depthId==before.depthId && !before.colorLease.expired() && !before.depthLease.expired() &&
             now.colorLease.lock()==before.colorLease.lock() && now.depthLease.lock()==before.depthLease.lock(),
             "Original shadows camera or native attachment generation changed");
        return true;
    }
    return false;
}
void EngineShadowTextures::setScissor(PPCContext& ctx,uint8_t* base) {
    auto& s=*state;s.require(base);
    const uint32_t owner=ctx.r31.u32,slot=ctx.r26.u32?1:0,camera=ctx.r24.u32,sp=ctx.r1.u32;
    need((uint32_t(ctx.lr)==0x82707264 || uint32_t(ctx.lr)==0x82707280) && sp && !(sp&15) && uint64_t(sp)+0xE0<=0x100000000ull &&
         PPC_LOAD_U32(sp)==sp+0xE0 && ctx.r4.u32==sp+0x70 && ownsCamera(camera) &&
         s.cameras[slot].owner==owner && s.cameras[slot].camera==camera,
         "Native shadow scissor requires its original parent frame and camera selector");
    const auto manager=PPC_LOAD_U32(owner+0x10);s.runtime.pointer(manager,0x1C,false);
    need(manager==PPC_LOAD_U32(0x82D08BFC) && ctx.r3.u32==PPC_LOAD_U32(manager+0x14) && ctx.r3.u32==s.context,
         "Native shadow scissor context/manager differs");
    std::array<uint32_t,4> rect{};for(uint32_t i=0;i<4;++i)rect[i]=PPC_LOAD_U32(ctx.r4.u32+4*i);
    need(rect==std::array<uint32_t,4>{1,1,1023,1023},"Original shadows one-pixel border rectangle changed");
    auto& driver=*s.runtime.engineDriver;const auto binding=driver.cameraBinding();
    need(binding.camera==camera && binding.viewport==std::array<uint32_t,6>{0,0,1024,1024,0x3F800000,0} &&
         driver.effectiveState().scalar(Graphics::ScalarState::ScissorEnable)==1,"Native shadow scissor lacks selected full camera/enabled state");
    bool alpha{};const auto color=driver.color(binding.colorIdentity,alpha);const auto depth=driver.depth(binding.depthIdentity);
    need(!alpha,"Native shadow scissor has front-buffer color policy");s.backend.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);
    // The original enabled SDK path intersects these exact edges with the
    // full camera viewport. That intersection is unchanged: [1,1023)^2.
    s.backend.setScissor(rect);++s.scissors;ctx.lr=0x827072A8;
    if(s.scissors<5)std::fprintf(stderr,"[NATIVE SHADOW] scissor camera=%08X rect=1,1,1023,1023 count=%llu; native rectangle retained, no draw\n",
        camera,static_cast<unsigned long long>(s.scissors));
}
uint64_t EngineShadowTextures::scissorCount() const {state->require(state->runtime.base);return state->scissors;}
void EngineShadowTextures::copyDepth(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.require(base);const auto sp=c.r1.u32;
    need(currentContext==&c&&uint32_t(c.lr)==0x82704C00&&sp>=0x200&&!(sp&15)&&uint64_t(sp)+0x160<=0x100000000ull,
         "Native shadow depth copy has no original helper frame");
    s.runtime.pointer(sp,0x160,false);
    need(PPC_LOAD_U32(sp)==sp+0x80&&PPC_LOAD_U32(sp+0x80)==sp+0x160&&PPC_LOAD_U32(sp+0x78)==0x827073BC,
         "Native shadow depth copy parent/helper backchain differs");
    const auto owner=PPC_LOAD_U32(sp+0x74),slot=c.r26.u32,camera=c.r24.u32;
    need(slot<=1&&ownsCamera(camera)&&s.cameras[slot].owner==owner&&s.cameras[slot].camera==camera,
         "Native shadow depth copy lost its original parent camera selection");
    auto& destination=s.find(base,c.r6.u32);const auto& v=destination.view;
    need(v.owner==owner&&v.field==0xF0+4*slot&&v.width==1024&&v.height==1024&&v.format==0x1A220197&&
         v.phase!=Phase::Locked&&destination.depth&&c.r31.u32==v.identity&&c.r3.u32==s.context&&c.r4.u32==4&&
         !c.r5.u32&&!c.r7.u32&&!c.r8.u32&&!c.r9.u32&&!c.r10.u32&&c.f1.u64==0&&
         !PPC_LOAD_U32(sp+0x5C)&&!PPC_LOAD_U32(sp+0x64),
         "Native shadow depth copy flags,destination or full-region arguments differ");
    const auto manager=PPC_LOAD_U32(owner+0x10);s.runtime.pointer(manager,0x1C,false);
    need(manager==PPC_LOAD_U32(0x82D08BFC)&&PPC_LOAD_U32(manager+0x14)==s.context&&
         !PPC_LOAD_U32(manager+4)&&!PPC_LOAD_U32(manager+0xC),"Native shadow depth copy precedes original effect end");
    auto& driver=*s.runtime.engineDriver;const auto binding=driver.cameraBinding();
    if(s.copies<2)std::fprintf(stderr,"[NATIVE SHADOW COPY SOURCE] parent_camera=%08X binding_camera=%08X color=%08X/%08X/%08X depth=%08X/%08X/%08X viewport=%u,%u,%u,%u,%08X,%08X\n",
         camera,binding.camera,binding.colorIdentity,s.cameras[slot].colorId,PPC_LOAD_U32(0x82D0CF5C),
         binding.depthIdentity,s.cameras[slot].depthId,PPC_LOAD_U32(0x82D0CF58),binding.viewport[0],binding.viewport[1],
         binding.viewport[2],binding.viewport[3],binding.viewport[4],binding.viewport[5]);
    need(binding.camera==camera&&binding.colorIdentity==s.cameras[slot].colorId&&binding.depthIdentity==s.cameras[slot].depthId&&
         binding.viewport==std::array<uint32_t,6>{0,0,1024,1024,0x3F800000,0},
         "Native shadow depth copy source is not its retained original camera");
    // Original823EE8F8 calls823EDD38: the CPU attachment cache is cleared,
    // while actual SDK/native attachments stay bound. Do not republish it.
    for(uint32_t at=0x82D0CF58;at<=0x82D0CF68;at+=4)
        need(!PPC_LOAD_U32(at),"Native shadow depth copy did not retain original context cache invalidation");
    bool alpha{};const auto color=driver.color(binding.colorIdentity,alpha);const auto depth=driver.depth(binding.depthIdentity);
    need(!alpha,"Native shadow depth copy source has front color policy");
    s.backend.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);
    s.unbound(base,v.identity);
    // Flags4: full depth/stencil transfer; no source clear. The backend owns
    // both resources through its GPU event and preserves their exact native
    // depth representation. The original helper epilogue remains generated.
    s.backend.copyDepth(depth,destination.depth);
    // Ordinary SDK completion824560CC/824562E0 returns zero. Its deferred
    // console resource lists are replaced by the backend's owning receipt.
    destination.view.phase=Phase::Uploaded;++s.copies;c.r3.u64=0;c.lr=0x82704C30;
    if(s.copies<=8)std::fprintf(stderr,"[NATIVE SHADOW DEPTH COPY] owner=%08X camera=%08X source=%08X destination=%08X count=%llu; exact1024 depth/stencil transfer submitted, no clear/draw\n",
         owner,camera,binding.depthIdentity,v.identity,static_cast<unsigned long long>(s.copies));
}
uint64_t EngineShadowTextures::copyCount() const {state->require(state->runtime.base);return state->copies;}
void EngineShadowTextures::requireReleased() const {
    need(active==&state->runtime && GetCurrentThreadId()==state->thread,"Native shadow textures retired outside their owner thread");
    need(state->records.empty(),"Original shadows still own native textures at driver stop");
}
}
void SimpsonsNativeShadowTextureCreate(PPCContext& ctx,uint8_t* base) {HostState fp;textures(base).create(ctx,base);}
void SimpsonsNativeShadowTextureLock(PPCContext& ctx,uint8_t* base) {HostState fp;textures(base).lock(ctx,base);}
void SimpsonsNativeShadowTextureUnlock(PPCContext& ctx,uint8_t* base) {HostState fp;textures(base).unlock(ctx,base);}
void SimpsonsNativeShadowTextureRelease(PPCContext& ctx,uint8_t* base) {HostState fp;textures(base).release(ctx,base);}
void SimpsonsNativeShadowScissor(PPCContext& ctx,uint8_t* base) {HostState fp;textures(base).setScissor(ctx,base);}
void SimpsonsNativeShadowDepthCopy(PPCContext& ctx,uint8_t* base) {HostState fp;textures(base).copyDepth(ctx,base);}
