#include "engine_viewport_surfaces.h"
#include "engine_driver.h"
#include "runtime.h"
#include "renderer/native_backend.h"
#include <array>
#include <cstring>
#include <cstdio>
#include <unordered_map>

namespace {
using namespace Simpsons;
constexpr uint32_t table=0x82DFEF00,countField=0x82DFEBA0;
constexpr std::array<uint64_t,10> profiles={
    0xFFFFFFFF00000000ull,0xFFFFFFFF00000000ull,0xFFFFFFFF00000000ull,
    0xFFFEFFFE00010100ull,0xFFFCFFFC00010100ull,0xFFF8FFF800010100ull,
    0x0100010000000100ull,0x0040004000000100ull,0xFFFFFFFF01000000ull,0x0040000800000001ull};
void need(bool ok,const char* why){if(!ok)throw Failure(std::string("Native viewport surfaces: ")+why);}
}
namespace Simpsons {
struct EngineViewportSurfaces::State {
    Runtime& runtime;Graphics::NativeBackend& backend;const uint32_t context,thread;
    struct Record {uint32_t owner,slot,width,height;std::shared_ptr<Graphics::RenderTarget> target;};
    std::unordered_map<uint32_t,Record> records;
    struct DepthRecord {
        uint32_t allocation{},pixels{},end{};
        std::array<uint32_t,13> header{};
        std::shared_ptr<Graphics::DepthTarget> target;
    } depth;
    struct QueryRecord {
        std::array<uint32_t,13> header{};
        std::shared_ptr<Graphics::RenderTarget> target;
        std::shared_ptr<Graphics::NativeCopySubmission> submission;
    } query,queryBackup;
    struct ColorRecord {
        uint32_t slot{},address{},pixels{},width{},height{},footprint{};
        std::array<uint32_t,13> header{};
        std::shared_ptr<Graphics::RenderTarget> target;
        std::shared_ptr<Graphics::NativeCopySubmission> submission;
    };
    std::array<ColorRecord,6> colors{};
    struct ColorRun {PPCContext* cpu{};uint32_t sp{},camera{},phase{},flags{};} colorRun;
    uint64_t depthCopies{},colorCopies{};
    uint32_t lastDepthCopyCamera{};
    const ColorRecord& colorOwner(uint8_t* base,uint32_t address) const {
        depthOwner(base);
        for(const auto& color:colors)if(color.address==address&&color.target) {
            need(PPC_LOAD_U32(table+0x10+4*color.slot)==address&&
                 color.pixels>=depth.allocation&&uint64_t(color.pixels)+color.footprint<=depth.end,
                 "color texture lost its original publication/allocation");
            for(size_t i=0;i<color.header.size();++i)
                need(PPC_LOAD_U32(address+uint32_t(4*i))==color.header[i],"original color texture header changed");
            return color;
        }
        throw Failure("Native viewport surfaces: unqualified color texture header");
    }
    State(Runtime& r,Graphics::NativeBackend& b,uint32_t c):runtime(r),backend(b),context(c),thread(GetCurrentThreadId()){}
    void require(uint8_t* base) const {
        need(active==&runtime&&base==runtime.base&&GetCurrentThreadId()==thread,"wrong runtime/thread");
        runtime.checkRunning();runtime.engineDriver->requireContext(context);backend.validateSubmissionContext();
        need(PPC_LOAD_U32(0x82D5DA74)==context&&!PPC_LOAD_U32(0x82D0CAF8),"context publication differs");
    }
    const Record& find(uint8_t* base,uint32_t id) const {
        const auto i=records.find(id);need(i!=records.end(),"unknown or stale identity");
        const auto& r=i->second;
        need(!runtime.pageAccess[id>>12].load()&&PPC_LOAD_U32(r.owner+0x38+4*r.slot)==id,"original publication differs");
        return r;
    }
    void depthOwner(uint8_t* base) const {
        need(depth.target && PPC_LOAD_U16(table)==1280 && PPC_LOAD_U16(table+2)==720 && PPC_LOAD_U8(table+0x60)==1 &&
             PPC_LOAD_U32(table+0x30)==0x82DFE840 && PPC_LOAD_U32(table+0xC)==depth.end &&
             PPC_LOAD_U32(0x82DFEB44)==depth.allocation && PPC_LOAD_U32(countField)>0,"full-size depth texture lost its original row/allocation");
        for(size_t i=0;i<depth.header.size();++i)need(PPC_LOAD_U32(0x82DFE840+uint32_t(4*i))==depth.header[i],"original depth texture header changed");
        std::lock_guard lock(runtime.vmMutex);bool owned=false;
        for(const auto& allocation:runtime.physicalAllocations)
            // The original allocator suballocates this row from its large
            // physical pool. Constructor/row retirement establish ownership;
            // this additional check establishes the containing pool is live.
            if(depth.allocation>=allocation.address && depth.pixels>=depth.allocation &&
               uint64_t(depth.pixels)+1280*720*4<=uint64_t(allocation.address)+allocation.size &&
               depth.end<=uint64_t(allocation.address)+allocation.size)owned=true;
        need(owned,"original depth texture physical allocation was retired");
    }
};
EngineViewportSurfaces::EngineViewportSurfaces(Runtime& r,Graphics::NativeBackend& b,uint32_t c):state(std::make_unique<State>(r,b,c)){}
EngineViewportSurfaces::~EngineViewportSurfaces(){if(!state->records.empty())std::fprintf(stderr,"[NATIVE VIEWPORT SURFACE] terminal release of %zu owners; original viewport cleanup incomplete\n",state->records.size());}
void EngineViewportSurfaces::create(PPCContext& c,uint8_t* base){
    auto& s=*state;s.require(base);
    const uint32_t owner=c.r31.u32,slotAddress=c.r21.u32,sp=c.r1.u32;
    need(uint32_t(c.lr)==0x82751388&&currentContext==&c&&sp>=0x200&&!(sp&15),"unsupported creation caller/frame");
    need(owner>=table&&owner<=table+200&&(owner-table)%100==0,"unqualified viewport row");
    const uint32_t row=(owner-table)/100;
    need(slotAddress>=owner+0x44&&slotAddress<=owner+0x54&&(slotAddress-owner-0x38)%4==0,"unqualified auxiliary slot");
    const uint32_t slot=(slotAddress-owner-0x38)/4;
    s.runtime.pointer(owner,100,true);s.runtime.pointer(sp+0x88,12,false);
    for(uint32_t i=0;i<profiles.size();++i)need(PPC_LOAD_U64(0x82150988+8*i)==profiles[i],"original profile table changed");
    const uint32_t width=PPC_LOAD_U16(owner),height=PPC_LOAD_U16(owner+2);
    need(width==(row?640u:1280u)&&height==720&&PPC_LOAD_U32(countField)==row+1&&PPC_LOAD_U8(owner+0x60)==1,
        "original viewport construction state differs");
    const uint32_t w=slot<=5?width/(1u<<(slot-2)):(slot==6?256:64);
    const uint32_t h=slot<=5?height/(1u<<(slot-2)):(slot==6?256:64);
    need(c.r3.u32==w&&c.r4.u32==h&&c.r5.u32==0x182801B6&&!c.r6.u32&&c.r7.u32==sp+0x88&&
         PPC_LOAD_U32(sp+0x88)<2048&&!PPC_LOAD_U32(sp+0x8C)&&!PPC_LOAD_U32(sp+0x90),"surface extent/format/placement differs");
    need(!PPC_LOAD_U32(slotAddress),"original slot already published");
    size_t previous=0;
    for(const auto& [id,r]:s.records){
        s.find(base,id);
        if(r.owner==owner){need(r.slot<slot,"duplicate or reordered creation");++previous;}
    }
    need(previous==slot-3,"auxiliary creation order differs");
    // Placement is original EDRAM metadata only. These native allocations have
    // uninitialized pixels and cannot bind/sample/resolve through this adapter.
    auto target=s.backend.createTarget(w,h,Graphics::TargetFormat::RGB10A2,slot<=5?Graphics::TargetScale::Scene:Graphics::TargetScale::Fixed);
    const uint32_t id=s.runtime.engineDriver->allocateTargetIdentity();
    need(s.records.emplace(id,State::Record{owner,slot,w,h,std::move(target)}).second,"identity collision");
    c.r3.u64=id; // Original82751388 publishes the slot after this returns.
    std::fprintf(stderr,"[NATIVE VIEWPORT SURFACE] allocated row=%u slot=%u id=%08X extent=%ux%u; original publication follows, pixels uninitialized\n",row,slot,id,w,h);
}
void EngineViewportSurfaces::release(PPCContext& c,uint8_t* base){
    auto& s=*state;s.require(base);const uint32_t id=c.r3.u32;const auto& r=s.find(base,id);
    need(uint32_t(c.lr)==0x82751020&&currentContext==&c&&c.r30.u32==r.owner&&c.r31.u32==r.owner+0x38+4*r.slot&&
         c.r28.u32==10-r.slot&&!c.r29.u32&&PPC_LOAD_U32(countField)>(r.owner-table)/100,"release lacks original row/slot owner");
    for(uint32_t i=0;i<4;++i)need(PPC_LOAD_U32(0x82D0CF5C+4*i)!=id,"surface remains a color attachment");
    need(PPC_LOAD_U32(0x82D0CF58)!=id,"surface remains a depth attachment");
    // Active viewport copies are borrowed metadata, not additional references.
    // Original82750FA8 clears source slots/row fields itself; no GPU use exists.
    s.records.erase(id);c.r3.u64=0;
    std::fprintf(stderr,"[NATIVE VIEWPORT SURFACE] released id=%08X through original viewport cleanup\n",id);
}
void EngineViewportSurfaces::publishDepth(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.require(base);
    need(currentContext==&c && c.r1.u32>=0x200 && !(c.r1.u32&15) && PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0x1B0 &&
         c.r24.u32<3 && c.r31.u32==table+100*c.r24.u32 && PPC_LOAD_U32(countField)==c.r24.u32+1,"depth publication lacks original constructor frame");
    if(c.r24.u32)return; // Split-screen depth texture/alias semantics remain unported.
    need(!s.depth.target,"duplicate full-size depth texture owner");
    State::DepthRecord candidate;
    candidate.allocation=PPC_LOAD_U32(0x82DFEB44);candidate.end=PPC_LOAD_U32(table+0xC);
    for(size_t i=0;i<candidate.header.size();++i)candidate.header[i]=PPC_LOAD_U32(0x82DFE840+uint32_t(4*i));
    constexpr std::array<uint32_t,13> expected={3,1,0,0,0,0xFFFF0000,0xFFFF0000,0x8A000002,0x97,0x0059E4FF,0xD11,0,0x200};
    for(size_t i=0;i<expected.size();++i)
        need((i==8?candidate.header[i]&0xFFF:candidate.header[i])==expected[i],"unqualified full-size original depth texture descriptor");
    candidate.pixels=candidate.header[8]&0xFFFFF000;
    need(candidate.allocation && candidate.end>candidate.allocation && candidate.end-candidate.allocation==0xEA6000 &&
         candidate.pixels>=candidate.allocation && uint64_t(candidate.pixels)+1280*720*4<=candidate.end,
         "original depth texture placement exceeds its constructor allocation");
    candidate.target=s.backend.createDepthTarget(1280,720,Graphics::TargetScale::Scene);
    State::QueryRecord query;
    need(PPC_LOAD_U32(table+0x34)==0x82DFE8DC,"query texture lost its original viewport row");
    for(size_t i=0;i<query.header.size();++i)query.header[i]=PPC_LOAD_U32(0x82DFE8DC+uint32_t(4*i));
    constexpr std::array<uint32_t,13> queryExpected={3,1,0,0,0,0xFFFF0000,0xFFFF0000,0x80800002,0xB6,0xE03F,0xC14,0,0x200};
    for(size_t i=0;i<queryExpected.size();++i)
        need((i==8?query.header[i]&0xFFF:query.header[i])==queryExpected[i],"original64x8 query texture descriptor differs");
    const uint32_t queryPixels=query.header[8]&0xFFFFF000;
    need(queryPixels>=candidate.allocation&&uint64_t(queryPixels)+0x2000<=candidate.end,"query texture exceeds original physical allocation");
    // Original construction allocates storage without initializing pixels.
    // The query-ready byte prevents consumers from sampling until the producer.
    query.target=s.backend.createTarget(64,8,Graphics::TargetFormat::RGB10A2);
    State::QueryRecord backup;
    need(PPC_LOAD_U32(table+0x2C)==0x82DFE7A4,"query backup lost its original viewport row");
    for(size_t i=0;i<backup.header.size();++i){backup.header[i]=PPC_LOAD_U32(0x82DFE7A4+uint32_t(i*4));
        const auto expectedWord=i==9?0x7E03Fu:queryExpected[i];
        if((i==8?backup.header[i]&0xFFF:backup.header[i])!=expectedWord){
            char reason[160];std::snprintf(reason,sizeof(reason),"query backup descriptor word%zu=%08X expected=%08X",i,backup.header[i],expectedWord);throw Failure(reason);}}
    const uint32_t backupPixels=backup.header[8]&0xFFFFF000;
    need(backupPixels>=candidate.allocation&&uint64_t(backupPixels)+64*64*4<=candidate.end,"query backup exceeds original physical allocation");
    backup.target=s.backend.createTarget(64,64,Graphics::TargetFormat::RGB10A2);
    // Original827512A8 constructs these headers;82751458 places them in
    // the same physical allocation as depth. Slots2/3/4 share original pixels,
    // but the checked post/distortion phases consume distinct resolve snapshots.
    // Slot6 owns the square distortion intermediate; slot7 shares queryBackup.
    // Slot1 is the full-size history that original Blur 82754C90 samples and
    // then resolves into, placed directly after slot0's 1280x736 footprint.
    std::array<State::ColorRecord,6> colors{};
    constexpr std::array<uint32_t,6> slots{0,3,4,2,6,1};
    constexpr std::array<uint32_t,6> pitchWords{0x8A000002,0x85000002,0x82800002,0x8A000002,0x82000002,0x8A000002};
    constexpr std::array<uint32_t,6> sizeWords{0x0059E4FF,0x002CE27F,0x0016613F,0x0059E4FF,0x001FE0FF,0x0059E4FF};
    constexpr std::array<uint32_t,6> widths{1280,640,320,1280,256,1280},heights{720,360,180,720,256,720};
    constexpr std::array<uint32_t,6> placements{0,0x730000,0x730000,0x730000,0xAC8000,0x398000};
    for(size_t index=0;index<colors.size();++index) {
        auto& color=colors[index];color.slot=slots[index];color.address=0x82DFE360+156*color.slot;
        color.width=widths[index];color.height=heights[index];
        color.footprint=color.width*((color.height+31)&~31u)*4;
        need(PPC_LOAD_U32(table+0x10+4*color.slot)==color.address,"color constructor header publication differs");
        for(size_t i=0;i<color.header.size();++i)color.header[i]=PPC_LOAD_U32(color.address+uint32_t(4*i));
        const std::array<uint32_t,13> expectedColor{3,1,0,0,0,0xFFFF0000,0xFFFF0000,
            pitchWords[index],0xB6,sizeWords[index],0xC14,0,0x200};
        for(size_t i=0;i<expectedColor.size();++i)
            need((i==8?color.header[i]&0xFFF:color.header[i])==expectedColor[i],"unqualified original viewport color descriptor");
        color.pixels=color.header[8]&0xFFFFF000;
        need(uint64_t(candidate.allocation)+placements[index]==color.pixels&&
             uint64_t(color.pixels)+color.footprint<=candidate.end,"original viewport color placement differs");
        color.target=s.backend.createTarget(color.width,color.height,Graphics::TargetFormat::RGB10A2,color.slot==6?Graphics::TargetScale::Fixed:Graphics::TargetScale::Scene);
    }
    s.depth=std::move(candidate);
    try{s.depthOwner(base);}catch(...){s.depth={};throw;}
    s.query=std::move(query);
    s.queryBackup=std::move(backup);s.colors=std::move(colors);s.colorRun={};
    std::fprintf(stderr,"[NATIVE VIEWPORT DEPTH] original texture8 header=82DFE840 allocation=%08X pixels=%08X end=%08X; native1280x720 depth/stencil owner, contents unspecified\n",s.depth.allocation,s.depth.pixels,s.depth.end);
}
void EngineViewportSurfaces::retireDepth(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.require(base);
    need(currentContext==&c && c.r1.u32>=0x200 && !(c.r1.u32&15) && PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0xA0 &&
         c.r26.u32<3 && c.r30.u32==table+100*c.r26.u32,"depth retirement lacks original row destructor frame");
    if(c.r26.u32)return;
    s.depthOwner(base);
    for(const auto& color:s.colors)(void)s.colorOwner(base,color.address);
    s.depth={};s.query={};s.queryBackup={};s.colors={};s.colorRun={};s.lastDepthCopyCamera=0;
    // Native queued copies retain real resource leases until GPU completion.
    std::fprintf(stderr,"[NATIVE VIEWPORT DEPTH] retired original full-size row before CPU header/publication clears\n");
}
void EngineViewportSurfaces::copyDepth(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.require(base);s.depthOwner(base);
    need(currentContext==&c && c.lastFunction==0x82751700 && c.r1.u32>=0x200 && !(c.r1.u32&15) && PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0x80 &&
         !c.r3.u32 && c.r4.u32==0x14 && !c.r5.u32 && c.r6.u32==0x82DFE840 && !c.r7.u32 && !c.r8.u32 && !c.r9.u32 && !c.r10.u32 &&
         c.f1.f64==0 && !PPC_LOAD_U32(c.r1.u32+0x5C) && !PPC_LOAD_U32(c.r1.u32+0x64),"unqualified original post depth-copy arguments/frame");
    need(PPC_LOAD_U32(0x82DFEB68)==c.r6.u32 && PPC_LOAD_U16(0x82DFEB38)==1280 && PPC_LOAD_U16(0x82DFEB3A)==720,
         "post depth-copy destination differs from original viewport selection");
    auto& driver=*s.runtime.engineDriver;const auto camera=driver.cameraBinding();
    need(camera.camera==PPC_LOAD_U32(0x82E3DD60) && PPC_LOAD_U32(0x82D0CB1C)==1 &&
         camera.depthIdentity==PPC_LOAD_U32(0x82D0CF58) && camera.colorIdentity==PPC_LOAD_U32(0x82D0CF5C) &&
         camera.viewport[2]==1280 && camera.viewport[3]==720,"post depth copy lost its active full-size camera");
    s.backend.copyDepth(driver.depth(camera.depthIdentity),s.depth.target);++s.depthCopies;
    s.runtime.nativeDepthCopyCount.store(s.depthCopies,std::memory_order_release);
    s.lastDepthCopyCamera=camera.camera;c.lr=0x82751754;
    // Retain the real copy count/receipt every frame; sample its success print.
    if(s.depthCopies<=4 || ((s.depthCopies-1)%512)==0)
        std::fprintf(stderr,"[NATIVE VIEWPORT DEPTH COPY] camera=%08X source=%08X destination=82DFE840 count=%llu; exact native depth/stencil copy, no clear/draw/state change\n",
        camera.camera,camera.depthIdentity,static_cast<unsigned long long>(s.depthCopies));
}
std::shared_ptr<Graphics::DepthTarget> EngineViewportSurfaces::depthTexture(uint32_t header) const {
    auto& s=*state;s.require(s.runtime.base);need(header==0x82DFE840,"unqualified viewport depth header");s.depthOwner(s.runtime.base);return s.depth.target;
}
std::shared_ptr<Graphics::RenderTarget> EngineViewportSurfaces::queryTexture(uint32_t header) const {
    auto& s=*state;auto* base=s.runtime.base;s.require(base);s.depthOwner(base);
    need(header==0x82DFE8DC&&s.query.target&&PPC_LOAD_U32(table+0x34)==header,"unqualified viewport query texture owner");
    for(size_t i=0;i<s.query.header.size();++i)
        need(PPC_LOAD_U32(header+uint32_t(4*i))==s.query.header[i],"original query texture header changed");
    return s.query.target;
}
uint64_t EngineViewportSurfaces::depthCopyCount() const {state->require(state->runtime.base);return state->depthCopies;}
uint32_t EngineViewportSurfaces::depthCopyCamera() const {state->require(state->runtime.base);return state->lastDepthCopyCamera;}
std::shared_ptr<Graphics::RenderTarget> EngineViewportSurfaces::queryBackupTexture(uint32_t header) const {
    auto& s=*state;auto* base=s.runtime.base;s.require(base);s.depthOwner(base);
    need(header==0x82DFE7A4&&s.queryBackup.target&&PPC_LOAD_U32(table+0x2C)==header,"unqualified query backup owner");
    for(size_t i=0;i<s.queryBackup.header.size();++i)need(PPC_LOAD_U32(header+uint32_t(i*4))==s.queryBackup.header[i],"query backup header changed");
    return s.queryBackup.target;
}
std::vector<uint8_t> EngineViewportSurfaces::readbackDepthTexture(uint32_t header) const {return state->backend.readbackDepthTarget(depthTexture(header));}
std::shared_ptr<Graphics::RenderTarget> EngineViewportSurfaces::colorTexture(uint32_t header) const {
    if(header==0x82DFE7A4)return queryBackupTexture(header);
    auto& s=*state;s.require(s.runtime.base);return s.colorOwner(s.runtime.base,header).target;
}
std::vector<uint8_t> EngineViewportSurfaces::readbackColorTexture(uint32_t header) const {
    auto& s=*state;s.require(s.runtime.base);
    if(header==0x82DFE7A4){const auto target=queryBackupTexture(header);need(bool(s.queryBackup.submission),"color readback requires a submitted resolve");s.backend.waitCopy(s.queryBackup.submission);return s.backend.readbackTarget(target);}
    // The 64x8 corona query texture is rendered, not resolved: read it directly.
    if(header==0x82DFE8DC)return s.backend.readbackTarget(queryTexture(header));
    const auto& color=s.colorOwner(s.runtime.base,header);
    need(bool(color.submission),"color readback requires a submitted resolve");
    s.backend.waitCopy(color.submission);return s.backend.readbackTarget(color.target);
}
uint64_t EngineViewportSurfaces::colorCopyCount() const {state->require(state->runtime.base);return state->colorCopies;}
void EngineViewportSurfaces::resolveColor(uint32_t header,const std::shared_ptr<Graphics::RenderTarget>& source,bool clear) {
    auto& s=*state;s.require(s.runtime.base);const auto destination=colorTexture(header);
    // copyFront validates the actual selected source attachment, native device,
    // format, dimensions and predication before changing either resource.
    s.backend.validateFrontCopy(source,destination);
    auto submitted=s.backend.copyFront(source,destination);
    if(clear)s.backend.clearTarget(source,{0,0,0,0});
    if(header==0x82DFE7A4)s.queryBackup.submission=std::move(submitted);
    else for(auto& color:s.colors)if(color.address==header){color.submission=std::move(submitted);break;}
    ++s.colorCopies;
}
void EngineViewportSurfaces::copyColor(PPCContext& c,uint8_t* base,uint32_t site) {
    auto& s=*state;s.require(base);
    const uint32_t stage=site==0x82773E34?0u:site==0x82773F44?1u:site==0x82774000?2u:site==0x827740C4?3u:UINT32_MAX;
    // lastFunction is tracing history: nested AOT calls overwrite it and do
    // not restore it on return. The pre-BL boundary retains the preceding
    // original call's LR; frame/caller/nonvolatile guards identify this scope.
    constexpr uint32_t previousReturns[]={0x82773DF4,0x82773F18,0x82773FD4,0x82774098};
    const bool qualified=stage!=UINT32_MAX&&currentContext==&c&&uint32_t(c.lr)==previousReturns[stage]&&
         c.r1.u32>=0x300&&!(c.r1.u32&15)&&PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0x120&&
         PPC_LOAD_U32(c.r1.u32+0x118)==0x8276E1C4&&c.r30.u32==0x82DFEA20&&c.r31.u32==0x82D10000&&
         !c.r3.u32&&!PPC_LOAD_U32(0x82D0CAF8)&&!c.r4.u32&&!c.r5.u32&&!c.r7.u32&&!c.r8.u32&&!c.r9.u32&&!c.r10.u32&&
         c.f1.f64==0&&!PPC_LOAD_U32(c.r1.u32+0x5C)&&!PPC_LOAD_U32(c.r1.u32+0x64)&&c.r25.u32<=3;
    if(!qualified)std::fprintf(stderr,"[NATIVE POST COPY REJECT] site=%08X trace_last=%08X lr=%08X sp=%08X current=%u r3..10=%08X/%08X/%08X/%08X/%08X/%08X/%08X/%08X r25=%08X r30=%08X r31=%08X f1=%.17g\n",
        site,c.lastFunction,uint32_t(c.lr),c.r1.u32,unsigned(currentContext==&c),c.r3.u32,c.r4.u32,c.r5.u32,c.r6.u32,c.r7.u32,c.r8.u32,c.r9.u32,c.r10.u32,
        c.r25.u32,c.r30.u32,c.r31.u32,c.f1.f64);
    need(qualified,"unqualified original post color-copy site/frame/arguments");
    const uint32_t index=stage<2?stage:2;auto& color=s.colors[index];
    (void)s.colorOwner(base,c.r6.u32);
    need(color.address==c.r6.u32&&PPC_LOAD_U32(0x82DFEB48+4*color.slot)==color.address&&
         PPC_LOAD_U16(0x82DFEB38)==1280&&PPC_LOAD_U16(0x82DFEB3A)==720&&
         !(PPC_LOAD_U32(0x82D6CCA8)&2),"post color-copy destination/viewport selection differs");
    auto& driver=*s.runtime.engineDriver;
    // The first resolve selects the logical main camera. Subsequent resolves
    // intentionally have an auxiliary OM target, so retain the exact camera
    // scope rather than invoking the general main-attachment validator.
    NativeCameraBinding camera{};
    if(!stage)camera=driver.cameraBinding();
    const uint32_t cameraId=stage?s.colorRun.camera:camera.camera;
    need(cameraId&&cameraId==PPC_LOAD_U32(0x82E3DD60)&&PPC_LOAD_U32(0x82D0CB1C)==1,
         "post color-copy lost its active camera");
    if(!stage)need(!s.colorRun.phase||s.colorRun.phase==4||(s.colorRun.phase==3&&(s.colorRun.flags&1)),"post color-copy restarted an incomplete resolve sequence");
    else need(s.colorRun.cpu==&c&&s.colorRun.sp==c.r1.u32&&s.colorRun.camera==cameraId&&
              s.colorRun.flags==c.r25.u32&&s.colorRun.phase==stage&&(stage!=3||!(c.r25.u32&1)),
              "post color-copy lost its original resolve sequence");
    std::shared_ptr<Graphics::RenderTarget> source;uint32_t sourceId{};
    if(!stage) {
        sourceId=camera.colorIdentity;
        need(sourceId==PPC_LOAD_U32(0x82D0CF5C)&&camera.viewport[2]==1280&&camera.viewport[3]==720,
             "first post copy lost its full-size camera attachment");
        bool alphaOne=false;source=driver.color(sourceId,alphaOne);
    } else {
        sourceId=PPC_LOAD_U32(0x82DFEB70+4*color.slot);
        const auto& owner=s.find(base,sourceId);
        need(owner.owner==table&&owner.slot==color.slot&&owner.width==color.width&&owner.height==color.height&&
             PPC_LOAD_U32(0x82D0CF5C)==sourceId,"post color-copy lost its exact auxiliary attachment");
        source=owner.target;
    }
    // copyFront is the existing exact packed-color resolve primitive. It
    // checks actual native OM slot0, dimensions/device/format and no predicate,
    // copies storage without clear/state/present, and retains completion leases.
    auto submitted=s.backend.copyFront(source,color.target);
    color.submission=std::move(submitted);++s.colorCopies;
    s.colorRun={&c,c.r1.u32,cameraId,stage+1,c.r25.u32};c.lr=site+4;
    if(s.colorCopies<=16)std::fprintf(stderr,"[NATIVE VIEWPORT COLOR COPY] site=%08X camera=%08X source=%08X texture=%08X slot=%u extent=%ux%u count=%llu\n",
        site,cameraId,sourceId,color.address,color.slot,color.width,color.height,static_cast<unsigned long long>(s.colorCopies));
}
void EngineViewportSurfaces::resolveAndClearColor(PPCContext& c,uint8_t* base) {
    auto& s=*state;s.require(base);
    const uint32_t sp=c.r1.u32;
    need(currentContext==&c&&c.lastFunction==0x82455570&&uint32_t(c.lr)==0x827724E0&&
         sp>=0x200&&!(sp&15),"unqualified effect color resolve caller/frame");
    s.runtime.pointer(sp,0xA0,false);
    need(PPC_LOAD_U32(sp)==sp+0xA0&&PPC_LOAD_U32(sp+0x98)==0x827517A8&&
         c.r10.u32==sp+0x70&&
         !c.r3.u32&&c.r4.u32==0x100&&!c.r5.u32&&c.r6.u32==0x82DFE360&&
         !c.r7.u32&&!c.r8.u32&&!c.r9.u32&&c.f1.f64==0&&
         !PPC_LOAD_U32(sp+0x5C)&&!PPC_LOAD_U32(sp+0x64)&&
         !PPC_LOAD_U32(sp+0x70)&&!PPC_LOAD_U32(sp+0x74)&&
         !PPC_LOAD_U32(sp+0x78)&&!PPC_LOAD_U32(sp+0x7C)&&
         c.r30.u32==0x82D10000&&PPC_LOAD_U32(0x82DFF580),
         "unqualified effect color resolve arguments/clear vector");
    const auto& color=s.colorOwner(base,c.r6.u32);
    need(color.slot==0&&PPC_LOAD_U32(0x82DFEB48)==color.address&&
         PPC_LOAD_U16(0x82DFEB38)==1280&&PPC_LOAD_U16(0x82DFEB3A)==720,
         "effect color resolve lost its full-size destination");
    auto& driver=*s.runtime.engineDriver;
    const auto camera=driver.cameraBinding();
    need(camera.camera==PPC_LOAD_U32(0x82E3DD60)&&PPC_LOAD_U32(0x82D0CB1C)==1&&
         camera.colorIdentity==PPC_LOAD_U32(0x82D0CF5C)&&
         camera.viewport[2]==1280&&camera.viewport[3]==720,
         "effect color resolve lost its active full-size camera");
    bool alphaOne=false;
    auto source=driver.color(camera.colorIdentity,alphaOne);
    s.backend.validateFrontCopy(source,color.target);
    // Selector 0x100 resolves color zero, then clears that attachment with the
    // caller's zero vector. The native commands use the same immediate queue.
    auto submitted=s.backend.copyFront(source,color.target);
    s.backend.clearTarget(source,{0,0,0,0});
    s.colors[0].submission=std::move(submitted);++s.colorCopies;
    std::fprintf(stderr,"[NATIVE VIEWPORT RESOLVE CLEAR] camera=%08X source=%08X texture=%08X count=%llu\n",
        camera.camera,camera.colorIdentity,color.address,static_cast<unsigned long long>(s.colorCopies));
}
bool EngineViewportSurfaces::owns(uint32_t id) const {state->require(state->runtime.base);return state->records.contains(id);}
size_t EngineViewportSurfaces::count() const {state->require(state->runtime.base);return state->records.size();}
std::shared_ptr<Graphics::RenderTarget> EngineViewportSurfaces::backing(uint32_t id) const {state->require(state->runtime.base);return state->find(state->runtime.base,id).target;}
void EngineViewportSurfaces::requireReleased() const {
    need(active==&state->runtime&&GetCurrentThreadId()==state->thread,"retirement runtime/thread differs");need(state->records.empty()&&!state->depth.target&&!state->query.target&&!state->queryBackup.target&&!state->colors[0].target&&!state->colors[1].target&&!state->colors[2].target&&!state->colors[3].target&&!state->colors[4].target&&!state->colors[5].target,"original viewport surfaces/textures remain owned");
}
}
void SimpsonsNativeViewportSurfaceCreate(PPCContext& c,uint8_t* b){
    const auto fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
    struct Restore {uint32_t fp;DWORD error;~Restore(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}} restore{fp,error};
    PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);
    need(active&&active->base==b&&active->engineDriver,"no driver owner");active->engineDriver->viewportSurfaces().create(c,b);
}
namespace {
template<class F>void viewportDepthBoundary(PPCContext& c,uint8_t* b,F action){
    const auto fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
    struct Restore{uint32_t fp;DWORD error;~Restore(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}} restore{fp,error};
    PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);
    need(active&&active->base==b&&active->engineDriver,"no viewport depth driver owner");action(active->engineDriver->viewportSurfaces(),c,b);
}
}
void SimpsonsNativeViewportDepthPublish(PPCContext& c,uint8_t* b){viewportDepthBoundary(c,b,[](auto& s,auto& c,auto* b){s.publishDepth(c,b);});}
void SimpsonsNativeViewportDepthRetire(PPCContext& c,uint8_t* b){viewportDepthBoundary(c,b,[](auto& s,auto& c,auto* b){s.retireDepth(c,b);});}
void SimpsonsNativeViewportDepthCopy(PPCContext& c,uint8_t* b){viewportDepthBoundary(c,b,[](auto& s,auto& c,auto* b){s.copyDepth(c,b);});}
void SimpsonsNativeViewportResolveAndClearColor(PPCContext& c,uint8_t* b){viewportDepthBoundary(c,b,[](auto& s,auto& c,auto* b){s.resolveAndClearColor(c,b);});}
