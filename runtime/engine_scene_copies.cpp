#include "engine_scene_copies.h"
#include "engine_driver.h"
#include "runtime.h"
#include "renderer/native_backend.h"
#include <array>
#include <cstdio>

namespace {
using namespace Simpsons;
constexpr std::array<uint32_t,2> fields={0x82D09894,0x82D6C7F0};
void need(bool b,const char* why){if(!b)throw Failure(std::string("Native scene copies: ")+why);}
}
namespace Simpsons {
struct EngineSceneCopies::State {
    Runtime& runtime;Graphics::NativeBackend& backend;const uint32_t context,thread;
    struct Record {uint32_t id{};std::shared_ptr<Graphics::RenderTarget> target;uint32_t copiedCamera{};};
    std::array<Record,2> records{};uint32_t constructionSp{},constructionCamera{};
    State(Runtime& r,Graphics::NativeBackend& b,uint32_t c):runtime(r),backend(b),context(c),thread(GetCurrentThreadId()){}
    void require() const {
        auto* base=runtime.base;
        need(active==&runtime&&GetCurrentThreadId()==thread,"wrong runtime/thread");runtime.checkRunning();
        runtime.engineDriver->requireContext(context);backend.validateSubmissionContext();
        need(PPC_LOAD_U32(0x82D5DA74)==context&&!PPC_LOAD_U32(0x82D0CAF8),"context publication differs");
    }
    void frame(const PPCContext& c,uint8_t* base) const {
        require();need(base==runtime.base&&currentContext==&c&&c.r1.u32>=0x200&&!(c.r1.u32&15),"caller context/frame differs");
    }
    void publications() const {
        auto* base=runtime.base;
        for(size_t i=0;i<2;++i){const auto& r=records[i];need(PPC_LOAD_U32(fields[i])==r.id,"original global publication differs");
            if(r.id)need(r.target&&!runtime.pageAccess[r.id>>12].load(),"native identity/backing differs");}
    }
    size_t find(uint32_t id) const {
        require();publications();
        for(size_t i=0;i<2;++i)if(id&&records[i].id==id)return i;
        throw Failure("Native scene copies: unknown or stale identity");
    }
};
EngineSceneCopies::EngineSceneCopies(Runtime& r,Graphics::NativeBackend& b,uint32_t c):state(std::make_unique<State>(r,b,c)){}
EngineSceneCopies::~EngineSceneCopies(){
    const auto n=(state->records[0].id?1:0)+(state->records[1].id?1:0);
    if(n)std::fprintf(stderr,"[NATIVE SCENE COPY] terminal release of %d owners; original global cleanup incomplete\n",n);
}
void EngineSceneCopies::create(PPCContext& c,uint8_t* base){
    auto& s=*state;s.frame(c,base);s.publications();const uint32_t lr=uint32_t(c.lr),sp=c.r1.u32;
    need(lr==0x823C75C8||lr==0x823C75F4,"unsupported creation caller");const size_t slot=lr==0x823C75C8?0:1;
    need(c.r3.u32==1280&&c.r4.u32==720&&c.r5.u32==1&&c.r6.u32==1&&!c.r7.u32&&
         c.r8.u32==0x182801B6&&!c.r9.u32&&c.r10.u32==3,"creation extent/format/flags differ");
    s.runtime.pointer(sp+0x50,8,false);
    need(PPC_LOAD_U32(sp+0x50)==1280&&PPC_LOAD_U32(sp+0x54)==720&&PPC_LOAD_U8(0x82CD1430)&&
         c.r27.u32==0x82D10000&&c.r28.u32==0x82D70000,"original parent mode/dimension state differs");
    const auto camera=s.runtime.engineDriver->cameraBinding();
    need(camera.camera==c.r26.u32&&camera.viewport[2]==1280&&camera.viewport[3]==720&&
         PPC_LOAD_U32(0x82E3DD60)==camera.camera&&PPC_LOAD_U32(0x82D0CB1C)==1,"original active camera differs");
    need(!s.records[slot].id&&(slot?bool(s.records[0].id):!s.records[1].id),"duplicate or reordered construction");
    if(slot)need(s.constructionSp==sp&&s.constructionCamera==camera.camera,"creation pair parent changed");
    auto target=s.backend.createTarget(1280,720,Graphics::TargetFormat::RGB10A2,Graphics::TargetScale::Scene);
    const auto id=s.runtime.engineDriver->allocateTargetIdentity();s.records[slot]={id,std::move(target)};
    s.constructionSp=sp;s.constructionCamera=camera.camera;c.r3.u64=id;
    std::fprintf(stderr,"[NATIVE SCENE COPY] allocated slot=%zu id=%08X RGB10A2 1280x720; original global publication follows, pixels unspecified\n",slot,id);
}
std::shared_ptr<Graphics::RenderTarget> EngineSceneCopies::destination(const PPCContext& c,uint32_t id,uint32_t camera) const {
    auto& s=*state;s.frame(c,s.runtime.base);const auto slot=s.find(id);
    need(s.records[0].id&&s.records[1].id&&uint32_t(c.lr)==(slot?0x823C7684u:0x823C7610u)&&
         c.r3.u32==id&&!c.r4.u32&&c.r5.u32==camera&&c.r26.u32==camera,"copy lacks original destination/camera caller");
    return s.records[slot].target;
}
void EngineSceneCopies::copied(uint32_t id,uint32_t camera) {
    auto& s=*state;auto& record=s.records[s.find(id)];
    need(camera==s.runtime.engineDriver->cameraBinding().camera,"copy camera changed before publication");
    record.copiedCamera=camera;
}
std::shared_ptr<Graphics::RenderTarget> EngineSceneCopies::edgeSource(uint32_t id,uint32_t camera) const {
    auto& s=*state;const auto slot=s.find(id);const auto& record=s.records[slot];
    need(slot==0 && camera && record.copiedCamera==camera &&
         camera==s.runtime.engineDriver->cameraBinding().camera,"edge source lacks its original first camera copy");
    return record.target;
}
std::shared_ptr<Graphics::RenderTarget> EngineSceneCopies::aaSource(uint32_t id,uint32_t camera) const {
    auto& s=*state;const auto slot=s.find(id);const auto& record=s.records[slot];
    need(slot==1 && camera && record.copiedCamera==camera &&
         camera==s.runtime.engineDriver->cameraBinding().camera,"AA source lacks its original second camera copy");
    return record.target;
}
void EngineSceneCopies::release(PPCContext& c,uint8_t* base){
    auto& s=*state;s.frame(c,base);const auto slot=s.find(c.r3.u32);const auto id=c.r3.u32;
    need(uint32_t(c.lr)==(slot?0x823C7114u:0x823C70F8u)&&c.r31.u32==(slot?0x82D70000u:0x82D10000u),"release lacks original global owner/caller");
    for(uint32_t i=0;i<4;++i)need(PPC_LOAD_U32(0x82D0CF5C+4*i)!=id,"copy remains a color attachment");
    need(PPC_LOAD_U32(0x82D0CF58)!=id,"copy remains a depth attachment");
    s.records[slot]={};c.r3.u64=0; // Original823C70C0 clears the global; queued GPU copies retain backing.
    std::fprintf(stderr,"[NATIVE SCENE COPY] released id=%08X through original global cleanup\n",id);
}
bool EngineSceneCopies::owns(uint32_t id) const {state->require();return id&&(state->records[0].id==id||state->records[1].id==id);}
size_t EngineSceneCopies::count() const {state->require();return (state->records[0].id?1u:0u)+(state->records[1].id?1u:0u);}
std::shared_ptr<Graphics::RenderTarget> EngineSceneCopies::backing(uint32_t id) const {return state->records[state->find(id)].target;}
std::vector<uint8_t> EngineSceneCopies::readback(uint32_t id){return state->backend.readbackTarget(backing(id));}
void EngineSceneCopies::requireReleased() const {
    need(active==&state->runtime&&GetCurrentThreadId()==state->thread,"retirement runtime/thread differs");
    need(!state->records[0].id&&!state->records[1].id,"original global copy textures remain owned");
}
}
