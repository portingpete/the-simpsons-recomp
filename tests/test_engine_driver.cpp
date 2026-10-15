// Real original startup, original plugin destructors, stop/close and reopen.
// The first completed native presentation is the deliberate test observation.
// The application executable continues normally beyond it.
#include "runtime/runtime.h"
#include "runtime/engine_driver.h"
#include "runtime/engine_audio.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_im2d_program.h"
#include "runtime/engine_loading_textures.h"
#include "runtime/engine_recording.h"
#include "renderer/native_backend.h"
#include "renderer/native_texture_stream.h"
#include "renderer/engine_state.h"
#include "renderer/im2d_vertices.h"
#include <bit>
#include <cmath>
#include <cstdio>
#include <functional>
#include <thread>
#include <algorithm>
#include <cstring>
#include <limits>

extern void SimpsonsNativeOverlayExpandedEnter(PPCContext&,uint8_t*);
extern void SimpsonsNativeOverlayExpandedExit(PPCContext&,uint8_t*);

namespace {
size_t checks{};
void require(bool value,const char* message) {
    ++checks;
    if(!value) {
        std::fprintf(stderr,"[DRIVER TEST] failed check %zu: %s\n",checks,message);
        throw Simpsons::Failure(message);
    }
}
void rejects(const std::function<void()>& fn) {
    ++checks;try {fn();} catch(const std::exception&) {return;}
    throw Simpsons::Failure("Expected driver ownership operation to fail");
}
constexpr std::array<uint32_t,6> roles={0x82D0CB00,0x82D0CAFC,0x82D0CF84,0x82D0CF90,0x82D0CF8C,0x82D0CF88};
struct FirstPresentObserved {};
PPCFunc* originalPresentBody{};
void observeFirstPresent(PPCContext& ctx,uint8_t* base) {
    originalPresentBody(ctx,base);
    // Present queues its copy/transfer without a CPU wait; the receipt must be
    // known immediately and its real GPU event must complete within the bound.
    require(ctx.r3.u32==1 && Simpsons::active->engineDriver->frontCopyCount()==1 &&
            Simpsons::active->engineDriver->waitSubmission(PPC_LOAD_U32(0x82D0CF94)),
            "First native presentation did not queue a front copy that really completes");
    throw FirstPresentObserved{}; // Only this fixture replaces a dispatch slot.
}
struct PresentObservation {
    uint8_t* base;
    explicit PresentObservation(uint8_t* memory):base(memory) {
        originalPresentBody=PPC_LOOKUP_FUNC(base,0x823EE820);
        require(originalPresentBody!=nullptr,"Original presentation mapping is absent");
        PPC_LOOKUP_FUNC(base,0x823EE820)=observeFirstPresent;
    }
    ~PresentObservation() {PPC_LOOKUP_FUNC(base,0x823EE820)=originalPresentBody;originalPresentBody=nullptr;}
};
// Dedicated hardware DLAA regression: pause after the real first loading clear,
// before present/UI transport. No generation or temporal success is fabricated.
struct FirstTemporalLoadingClearObserved {};
PPCFunc* originalTemporalLoadingClearBody{};
void observeTemporalLoadingClear(PPCContext& ctx,uint8_t* base) {
    const uint32_t camera=ctx.r3.u32;
    auto& runtime=*Simpsons::active;
    require(runtime.videoSettings.antialiasing==uint32_t(Simpsons::Graphics::Antialiasing::DLAA),
            "Temporal loading fixture did not configure actual DLAA before driver creation");
    require(camera&&camera==PPC_LOAD_U32(0x82E07248)&&ctx.r5.u32==7&&ctx.lr==0x823F1BA8,
            "Temporal loading fixture did not reach the actual original startup clear");
    require(!PPC_LOAD_U32(0x82D08B10),"Temporal loading fixture already has a main viewport manager");
    const auto allocation=runtime.engineAudio?runtime.engineAudio->allocationSpan(camera):std::nullopt;
    std::fprintf(stderr,"[DRIVER TEST TEMPORAL LOADING SPAN] camera=%08X present=%u address=%08X extent=%08X generation=%llu\n",
        camera,unsigned(bool(allocation)),allocation?allocation->address:0u,allocation?allocation->extent:0u,
        static_cast<unsigned long long>(allocation?allocation->generation:0u));
    require(!allocation||!allocation->generation||camera<allocation->address||
            uint64_t(camera)+0x8C>uint64_t(allocation->address)+allocation->extent,
            "Loading fixture did not reproduce the exact unqualified original main allocation span predicate");
    originalTemporalLoadingClearBody(ctx,base);
    auto& driver=*runtime.engineDriver;
    require(ctx.r3.u32==1&&driver.cameraClearCount()==1&&driver.frontCopyCount()==0&&
            driver.presentationAttemptCount()==0,"Temporal loading clear did not complete alone before presentation");
    const auto binding=driver.cameraBinding();
    require(binding.camera==camera&&binding.colorIdentity==PPC_LOAD_U32(0x82D0CB00)&&
            binding.depthIdentity==PPC_LOAD_U32(0x82D0CAFC),"Temporal loading clear lost its original default owner pair");
    const auto origins=driver.temporalSceneOrigins();
    for(const auto& origin:origins.components)
        require(origin.kind==Simpsons::Graphics::TemporalRasterOriginKind::Uniform&&origin.revision>0,
                "Temporal loading clear did not publish the exact invariant clear components");
    throw FirstTemporalLoadingClearObserved{};
}
struct TemporalLoadingClearObservation {
    uint8_t* base;
    explicit TemporalLoadingClearObservation(uint8_t* memory):base(memory) {
        originalTemporalLoadingClearBody=PPC_LOOKUP_FUNC(base,0x823EE940);
        require(originalTemporalLoadingClearBody!=nullptr,"Original temporal loading clear mapping is absent");
        PPC_LOOKUP_FUNC(base,0x823EE940)=observeTemporalLoadingClear;
    }
    ~TemporalLoadingClearObservation() {
        PPC_LOOKUP_FUNC(base,0x823EE940)=originalTemporalLoadingClearBody;
        originalTemporalLoadingClearBody=nullptr;
    }
};
#include "header/test_rw_fog_contract.h"
#include "header/test_rw_frontend_contract.h"
#include "header/test_rw_overlay_state_contract.h"
#include "header/test_im2d_upload_contract.h"
#include "header/test_im2d_shader_cpu_contract.h"
#include "header/test_im2d_quad_contract.h"
#include "header/test_im2d_draw_contract.h"
#include "header/test_overlay_expanded_contract.h"
#include "header/test_binding_reset_contract.h"
#include "header/test_null_raster_contract.h"
#include "header/test_rw_rebuild_contract.h"
#include "header/test_rw_sampler_contract.h"
#include "header/test_present_contract.h"
#include "header/test_recording_owner_contract.h"
void applicationStateContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    using namespace Simpsons;
    const uint32_t app=0x82D5DB78,selector=6,index=PPC_LOAD_U32(0x82D6D498+4*selector);
    const uint32_t cache=app+0x694+4*index;
    auto& driver=*runtime.engineDriver;
    const auto queueBefore=PPC_LOAD_U32(0x82D10114),appliedBefore=PPC_LOAD_U32(0x82E3D580+4*0x3C);
    const uint32_t initialDepth=PPC_LOAD_U32(app+0xD28);
    require(initialDepth<=8,"Application fixture found an invalid original state stack");
    const uint32_t initialValue=PPC_LOAD_U32(cache);
    auto* stack=runtime.pointer(app+0xD28,0x34AC,true);
    const std::vector<uint8_t> savedStack(stack,stack+0x34AC);
    PPC_STORE_U32(app+0xD28,0); // Exercise full capacity independently of live depth.
    const uint32_t sp=cpu.registers().r1.u32;
    cpu.registers().r28.u64=0x1122334455667788ull;
    cpu.registers().r29.u64=0xAABBCCDDEEFF0011ull;
    cpu.registers().lr=0x11223344;
    cpu.invoke(0x82723D80,app,selector,1,1);
    require(PPC_LOAD_U32(cache)==1 && driver.effectiveState().scalar(0x3C)==1,"Original application cache and native state diverged");
    require(cpu.registers().r1.u32==sp && cpu.registers().lr==0x11223344 &&
            cpu.registers().r28.u64==0x1122334455667788ull && cpu.registers().r29.u64==0xAABBCCDDEEFF0011ull,
            "Application native setter damaged original save/restore ABI");
    // Desynchronize only the host value to expose whether the original cache
    // fast path suppresses a call and its low-byte force path reapplies it.
    driver.applicationScalar(base,app,selector,0,true);
    cpu.invoke(0x82723D80,app,selector,1,0x100);
    require(driver.effectiveState().scalar(0x3C)==0 && PPC_LOAD_U32(cache)==1,"Original force argument did not use only its low byte");
    cpu.invoke(0x82723D80,app,selector,1,0x101);
    require(driver.effectiveState().scalar(0x3C)==1,"Forced equal application value did not reach the native owner");
    for(uint32_t depth=1;depth<=8;++depth) {
        cpu.invoke(0x82723978,app); // Original push copies applied cache and clears dirty words.
        require(PPC_LOAD_U32(app+0xD28)==depth,"Original application state push lost its depth");
        const uint32_t frame=app+depth*0x694+0x698,dirty=frame+4*(0x192+(index>>5)),bit=1u<<(index&31);
        const uint32_t prior=PPC_LOAD_U32(frame+4*index),untouched=0xA5A5A5A5u&~bit;
        PPC_STORE_U32(dirty,untouched);
        cpu.invoke(0x82723D80,app,selector,prior^1,1);
        require(PPC_LOAD_U32(dirty)==(untouched|bit) && PPC_LOAD_U32(cache)==(prior^1),"Original state divergence did not set exactly its dirty bit");
        cpu.invoke(0x82723D80,app,selector,prior,1);
        require(PPC_LOAD_U32(dirty)==untouched && PPC_LOAD_U32(cache)==prior,"Original saved-value restore did not clear exactly its dirty bit");
    }
    PPC_STORE_U32(app+0xD28,0);
    const auto hostBefore=driver.effectiveState().scalar(0x3C);
    const auto cacheBefore=PPC_LOAD_U32(cache);
    for(uint32_t depth:{9u,0xFFFFFFFFu}) {
        PPC_STORE_U32(app+0xD28,depth);
        rejects([&]{cpu.invoke(0x82723D80,app,selector,0,1);});
    }
    PPC_STORE_U32(app+0xD28,0);
    const uint32_t map=0x82D6D498+4*selector;
    PPC_STORE_U32(map,0x7FFFFFFF);
    rejects([&]{cpu.invoke(0x82723D80,app,selector,0,1);});PPC_STORE_U32(map,index);
    rejects([&]{cpu.invoke(0x82723D80,app,selector,2,1);});
    rejects([&]{cpu.invoke(0x82723D80,app+4,selector,0,1);});
    require(PPC_LOAD_U32(cache)==cacheBefore && driver.effectiveState().scalar(0x3C)==hostBefore,
            "Rejected application scalar changed CPU or host state");
    require(PPC_LOAD_U32(0x82D10114)==queueBefore && PPC_LOAD_U32(0x82E3D580+4*0x3C)==appliedBefore,
            "Direct application state incorrectly changed the separate RenderWare pending/applied caches");
    cpu.invoke(0x82723D80,app,selector,initialValue,1);
    std::copy(savedStack.begin(),savedStack.end(),stack);
}
void applicationSamplerContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    auto& driver=*runtime.engineDriver;
    const uint32_t app=0x82D5DB78,selector=5,index=PPC_LOAD_U32(0x82D6D648+4*selector);
    const uint32_t initialDepth=PPC_LOAD_U32(app+0xD28),sp=cpu.registers().r1.u32;
    auto* rw=runtime.pointer(0x82D0D170,0xA00,false);
    const std::vector<uint8_t> rwBefore(rw,rw+0xA00);
    require(initialDepth<=8,"Sampler fixture found an invalid original state stack");
    auto* stack=runtime.pointer(app+0xD28,0x34AC,true);
    const std::vector<uint8_t> savedStack(stack,stack+0x34AC);
    PPC_STORE_U32(app+0xD28,0);
    cpu.registers().r26.u64=0x12345678AABBCCDDull;
    cpu.registers().r31.u64=0xFFEEDDCC87654321ull;
    cpu.registers().lr=0x11223344;
    for(uint32_t stage=0;stage<16;++stage) {
        const uint32_t cache=app+0x7DC+0x50*stage+4*index;
        cpu.invoke(0x82723C80,app,stage,selector,1,1);
        require(PPC_LOAD_U32(cache)==1 && driver.effectiveState().sampler(stage,0x10)==1,
                "Application sampler cache/native state diverged");
        driver.applicationSampler(base,app,stage,selector,0,true);
        cpu.invoke(0x82723C80,app,stage,selector,1,0x100);
        require(driver.effectiveState().sampler(stage,0x10)==0,"Sampler force flag did not use low byte");
        cpu.invoke(0x82723C80,app,stage,selector,1,0x101);
        require(driver.effectiveState().sampler(stage,0x10)==1,"Forced equal sampler was elided");
    }
    require(cpu.registers().r1.u32==sp && cpu.registers().lr==0x11223344 &&
            cpu.registers().r26.u64==0x12345678AABBCCDDull && cpu.registers().r31.u64==0xFFEEDDCC87654321ull,
            "Sampler bridge damaged original save/restore ABI");
    for(uint32_t depth=1;depth<=8;++depth) {
        cpu.invoke(0x82723978,app);
        const uint32_t frame=app+depth*0x694+0x698;
        for(uint32_t stage:{0u,7u,8u,15u}) {
            const uint32_t cache=app+0x7DC+0x50*stage+4*index;
            const uint32_t dirty=frame+0x654+4*stage,bit=1u<<index;
            const uint32_t saved=PPC_LOAD_U32(frame+0x148+0x50*stage+4*index),other=0xA5A5A5A5u&~bit;
            PPC_STORE_U32(dirty,other);
            cpu.invoke(0x82723C80,app,stage,selector,saved^1,1);
            require(PPC_LOAD_U32(dirty)==(other|bit) && PPC_LOAD_U32(cache)==(saved^1),"Sampler divergence changed wrong frame/stage/dirty bit");
            cpu.invoke(0x82723C80,app,stage,selector,saved,1);
            require(PPC_LOAD_U32(dirty)==other && PPC_LOAD_U32(cache)==saved,"Sampler restore failed to clear exact dirty bit");
        }
    }
    PPC_STORE_U32(app+0xD28,0);
    auto* applied=runtime.pointer(app+0x694,0x694,false);
    const std::vector<uint8_t> before(applied,applied+0x694);
    const auto host=driver.effectiveState();
    for(uint32_t depth:{9u,0xFFFFFFFFu}) {
        PPC_STORE_U32(app+0xD28,depth);
        rejects([&]{cpu.invoke(0x82723C80,app,15,selector,0,1);});
    }
    PPC_STORE_U32(app+0xD28,0);
    for(uint32_t badStage:{16u,0xFFFFFFFFu}) rejects([&]{cpu.invoke(0x82723C80,app,badStage,selector,0,1);});
    for(uint32_t badSelector:{0u,21u}) rejects([&]{cpu.invoke(0x82723C80,app,0,badSelector,0,1);});
    rejects([&]{cpu.invoke(0x82723C80,app+4,0,selector,0,1);});
    rejects([&]{cpu.invoke(0x82723C80,app,0,selector,2,1);});
    rejects([&]{cpu.invoke(0x82723C80,app,0,11,2,0);}); // Noncanonical Z-filter flag.
    const uint32_t map=0x82D6D648+4*selector;
    PPC_STORE_U32(map,0x7FFFFFFF);
    rejects([&]{cpu.invoke(0x82723C80,app,0,selector,0,1);});PPC_STORE_U32(map,index);
    PPC_STORE_U32(0x82D6D7EC,40);
    rejects([&]{cpu.invoke(0x82723C80,app,0,selector,0,1);});PPC_STORE_U32(0x82D6D7EC,20);
    for(uint32_t entry:{0x827238B8u,0x82723858u,0x82723AB0u,0x82723B40u,0x82724038u,0x82724230u})
        rejects([&]{cpu.invoke(entry,app,0,selector,0,0);});
    require(std::vector<uint8_t>(applied,applied+0x694)==before,"Rejected sampler path changed application cache");
    for(uint32_t stage=0;stage<16;++stage)
        for(const auto& f:Simpsons::Graphics::samplerStateEvidence())
            require(driver.effectiveState().sampler(stage,f.id)==host.sampler(stage,f.id),"Rejected sampler path changed native effective state");
    require(std::vector<uint8_t>(rw,rw+0xA00)==rwBefore,"Application sampler changed the separate RenderWare cache");
    std::copy(savedStack.begin(),savedStack.end(),stack);
}
void cameraClearContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    auto& driver=*runtime.engineDriver;
    const uint32_t c=PPC_LOAD_U32(0x82E07248),colorId=PPC_LOAD_U32(0x82D0CB00),depthId=PPC_LOAD_U32(0x82D0CAFC);
    const uint32_t stencilBefore=PPC_LOAD_U32(0x82D0CB14),color=cpu.registers().r1.u32+0x60;
    const uint32_t activeBefore=PPC_LOAD_U32(0x82D0CB1C),currentBefore=PPC_LOAD_U32(0x82E3DD60);
    const auto binding=driver.cameraBinding();
    require(binding.camera==c && binding.colorIdentity==colorId && binding.depthIdentity==depthId &&
            binding.viewport==std::array<uint32_t,6>{0,0,1280,720,0x3F800000,0},"Native camera selection lost original roles or reversed logical viewport");
    auto checkPixels=[&](uint32_t packed,uint8_t stencil) {
        const auto colors=driver.readbackColor(colorId),depths=driver.readbackDepth(depthId);
        require(colors.size()==1280*720*4 && depths.size()==1280*720*8,"Camera readback extent differs from original targets");
        bool matched=true;
        for(size_t i=0;i<1280*720;++i) {
            uint32_t word;float z;
            memcpy(&word,colors.data()+4*i,4);memcpy(&z,depths.data()+8*i,4);
            matched=matched && word==packed && z==0 && depths[8*i+4]==stencil;
        }
        require(matched,"Native camera clear/readback did not preserve the requested attachment components");
    };
    checkPixels(0,0); // Actual original startup's clear, before diagnostic seeds.
    for(uint32_t selector=0;selector<8;++selector) {
        PPC_STORE_U32(color,0xFF0000FF);PPC_STORE_U32(0x82D0CB14,0x5A);
        require(cpu.invoke(0x823EE940,c,color,7)==1,"Native camera seed clear did not preserve Boolean ABI");
        PPC_STORE_U32(color,0x00FF0000);PPC_STORE_U32(0x82D0CB14,0xA5);
        const auto count=driver.cameraClearCount();
        const auto originsBefore=driver.temporalSceneOrigins();
        require(cpu.invoke(0x823EE940,c,(selector&1)?color:0xFFFFFFFC,selector)==1,"Native original clear failed");
        const auto originsAfter=driver.temporalSceneOrigins();
        require(originsBefore.color==originsAfter.color&&originsBefore.depth==originsAfter.depth,
            "Original partial clear changed the canonical Scene origin owners");
        for(size_t component=0;component<3;++component)
            require(originsAfter.components[component].revision==originsBefore.components[component].revision+
                bool(selector&(1u<<component))&&
                originsAfter.components[component].kind==Simpsons::Graphics::TemporalRasterOriginKind::Uniform,
                "Original selector updated an uncleared component or lost its invariant clear origin");
        require(driver.cameraClearCount()==count+1,"Original clear did not reach the native operation");
        checkPixels((selector&1)?0x000FFC00:0xC00003FF,(selector&4)?0xA5:0x5A);
    }
    PPC_STORE_U32(color,0xFF0000FF);PPC_STORE_U32(0x82D0CB14,0x5A);cpu.invoke(0x823EE940,c,color,7);
    const uint32_t depthRaster=PPC_LOAD_U32(c+0x64);
    PPC_STORE_U32(c+0x64,0);PPC_STORE_U32(0x82D0CB14,0xA5);
    const auto suppressedOrigins=driver.temporalSceneOrigins();
    cpu.invoke(0x823EE940,c,0xFFFFFFFC,6);
    require(driver.temporalSceneOrigins().components==suppressedOrigins.components,
        "Missing original depth raster changed suppressed depth/stencil origins");
    checkPixels(0xC00003FF,0x5A); // Missing camera depth suppresses both flags, but default depth remains selected.
    PPC_STORE_U32(c+0x64,depthRaster);cpu.invoke(0x823EE6C8,c);
    const auto count=driver.cameraClearCount();
    rejects([&]{cpu.invoke(0x823EE940,c,color,8);});
    rejects([&]{cpu.invoke(0x823EE940,c+4,color,7);});
    rejects([&]{cpu.invoke(0x823EE940,c,0xFFFFFFFC,1);});
    PPC_STORE_U32(color,0x800000FF);rejects([&]{cpu.invoke(0x823EE940,c,color,1);});PPC_STORE_U32(color,0xFF0000FF);
    PPC_STORE_U32(0x82D0CB14,256);rejects([&]{cpu.invoke(0x823EE940,c,color,4);});PPC_STORE_U32(0x82D0CB14,0xA5);
    const uint32_t r=PPC_LOAD_U32(c+0x60);
    PPC_STORE_U16(r+0x1C,1);rejects([&]{cpu.invoke(0x823EE940,c,color,7);});PPC_STORE_U16(r+0x1C,0);
    const uint32_t oldWidth=PPC_LOAD_U32(r+0xC);
    PPC_STORE_U32(r+0xC,oldWidth-1);rejects([&]{cpu.invoke(0x823EE940,c,color,7);});PPC_STORE_U32(r+0xC,oldWidth);
    PPC_STORE_U32(0x82D0CF60,colorId);rejects([&]{cpu.invoke(0x823EE940,c,color,7);});PPC_STORE_U32(0x82D0CF60,0);
    require(driver.cameraClearCount()==count,"Rejected clear executed a native operation");checkPixels(0xC00003FF,0x5A);
    require(PPC_LOAD_U32(0x82D0CB1C)==activeBefore && PPC_LOAD_U32(0x82E3DD60)==currentBefore,
            "Camera clear changed original begin/end/current-camera bookkeeping");
    PPC_STORE_U32(0x82D0CB14,stencilBefore);
}
void temporalLoadingCameraContracts(const char* image) {
    using namespace Simpsons;
    Runtime runtime;runtime.videoSettings.antialiasing=uint32_t(Graphics::Antialiasing::DLAA);
    runtime.load(image);PPCContext startup{};runtime.initialize(startup);
    const PPCContext entry=startup;auto* base=runtime.base;bool observed=false;
    {
        TemporalLoadingClearObservation observation(base);
        try {runOriginal(startup,base);}catch(const FirstTemporalLoadingClearObserved&) {observed=true;}
    }
    require(observed,"Actual hardware DLAA startup did not reach its bounded loading clear");
    require(PPC_LOOKUP_FUNC(base,0x823EE940)!=observeTemporalLoadingClear,
            "Temporal loading clear observer did not restore the original dispatch");
    EngineCpuCalls cpu(entry,base);
    cameraClearContracts(runtime,cpu,base); // Unchanged eight-selector pixels, origins, ABI/ownership rejection gold.
    require(runtime.engineDriver->presentationAttemptCount()==0&&runtime.engineDriver->frontCopyCount()==0,
            "Bounded temporal loading clear fixture submitted a presentation");
    std::printf("PASS: actual hardware DLAA loading startup clear plus unchanged eight-selector clear/readback/origin contracts / %zu checks; no world, NGX evaluation or presentation claim\n",checks);
}
// This separate opt-in process runs through actual first loading UI transport and
// first real GPU-completed presentation. The clear-only fixture stays exact.
PPCFunc* originalCameraPluginDestroyBody{};
uint32_t closingCamera{};
bool observedCameraClosing{};
struct CameraPluginClosingTarget {
    uint32_t tail{},callback{},offset{},size{},id{},context{};
    uint64_t generation{};
    uint8_t* base{};
};
CameraPluginClosingTarget closingPlugin{};
void observeCameraPluginClosing(PPCContext& ctx,uint8_t* base) {
    // The walker is a direct AOT BL. Its first registered destructor is the
    // actual indirect callback, reached before any original plugin body runs.
    if(ctx.r3.u32==closingCamera&&ctx.r31.u32==closingPlugin.tail) {
        require(Simpsons::active&&Simpsons::active->base==base&&base==closingPlugin.base&&
                Simpsons::currentContext==&ctx&&uint32_t(ctx.lr)==0x823FB3DC&&
                ctx.r30.u32==0x82CD1B30&&ctx.ctr.u32==closingPlugin.callback&&
                ctx.r4.u32==closingPlugin.offset&&ctx.r5.u32==closingPlugin.size,
                "Actual first camera plugin destructor callback changed its context or original ABI");
        auto& driver=*Simpsons::active->engineDriver;
        require(PPC_LOAD_U32(0x82CD1B30+0x14)==closingPlugin.tail&&
                PPC_LOAD_U32(closingPlugin.tail+0x38)==0x82CD1B30&&
                PPC_LOAD_U32(closingPlugin.tail)==closingPlugin.offset&&
                PPC_LOAD_U32(closingPlugin.tail+4)==closingPlugin.size&&
                PPC_LOAD_U32(closingPlugin.tail+8)==closingPlugin.id&&
                PPC_LOAD_U32(closingPlugin.tail+0x24)==closingPlugin.callback,
                "Actual first camera destructor lost its captured live registry owner or callback");
        const auto lifetime=driver.observedCameraLifetime(closingCamera);
        require(lifetime&&lifetime->phase==Simpsons::NativeCameraLifetimePhase::Closing&&
                lifetime->context==closingPlugin.context&&lifetime->generation==closingPlugin.generation,
                "Actual camera plugin destruction did not observe Closing before callbacks");
        driver.requireContext(closingPlugin.context);
        rejects([&]{driver.setUiDrawing(true);});
        observedCameraClosing=true;
    }
    // Unrelated calls and the observed call retain the exact original callback.
    originalCameraPluginDestroyBody(ctx,base);
}
struct CameraPluginClosingObservation {
    uint8_t* base;
    CameraPluginClosingObservation(uint8_t* memory,uint32_t camera):base(memory) {
        require(!originalCameraPluginDestroyBody,"Nested camera Closing observation");
        require(Simpsons::active&&Simpsons::active->base==base&&Simpsons::active->engineDriver,
                "Camera plugin observation lost its actual Runtime/driver");
        auto& runtime=*Simpsons::active;auto& driver=*runtime.engineDriver;
        runtime.pointer(0x82CD1B30,0x18,false);
        const uint32_t tail=PPC_LOAD_U32(0x82CD1B30+0x14);
        require(tail&&!(tail&3),"Actual camera plugin registry has no aligned destructor tail");
        runtime.pointer(tail,0x3C,false);
        const CameraPluginClosingTarget target{tail,PPC_LOAD_U32(tail+0x24),PPC_LOAD_U32(tail),
            PPC_LOAD_U32(tail+4),PPC_LOAD_U32(tail+8),PPC_LOAD_U32(0x82D6D890),0,base};
        require(PPC_LOAD_U32(tail+0x38)==0x82CD1B30&&
                uint64_t(target.offset)+target.size<=PPC_LOAD_U32(0x82CD1B30),
                "Actual first camera destructor registry owner or original object extent changed");
        require(!(target.callback&3)&&target.callback>=PPC_CODE_BASE&&
                target.callback<PPC_CODE_BASE+PPC_CODE_SIZE,
                "Actual first camera destructor callback is outside original code");
        PPCFunc* const body=PPC_LOOKUP_FUNC(base,target.callback);
        require(body&&body!=observeCameraPluginClosing,"Actual first camera plugin destructor is absent or already observed");
        const auto birth=driver.observedCameraLifetime(camera);
        require(birth&&birth->phase==Simpsons::NativeCameraLifetimePhase::Constructed&&
                birth->generation&&birth->context==target.context,
                "Camera plugin observation lacks its genuine current driver/birth receipt");
        driver.requireContext(target.context);
        // Every fallible capture/qualification precedes lookup replacement.
        closingPlugin=target;closingPlugin.generation=birth->generation;
        originalCameraPluginDestroyBody=body;closingCamera=camera;observedCameraClosing=false;
        PPC_LOOKUP_FUNC(base,target.callback)=observeCameraPluginClosing;
    }
    ~CameraPluginClosingObservation() {
        PPC_LOOKUP_FUNC(base,closingPlugin.callback)=originalCameraPluginDestroyBody;
        originalCameraPluginDestroyBody=nullptr;closingCamera=0;closingPlugin={};
    }
};
// Read-only qualification of the real original camera free-list. Keeping a
// genuine second slot alive prevents the target block's empty retirement.
struct CameraPoolBlock {
    uint32_t address{},next{},previous{},firstSlot{};
    std::vector<uint8_t> bitmap;
};
struct CameraPoolState {
    uint32_t engine{},pool{},offset{},stride{},count{},bitmapBytes{},alignment{},flags{};
    std::vector<CameraPoolBlock> blocks;
    bool occupied(uint32_t block,uint32_t index) const {
        return (blocks[block].bitmap[index/8]&(0x80u>>(index%8)))!=0;
    }
    std::array<uint32_t,2> slot(uint32_t camera) const {
        std::array<uint32_t,2> result{};bool found=false;
        for(uint32_t b=0;b<blocks.size();++b) {
            const uint64_t end=uint64_t(blocks[b].firstSlot)+uint64_t(count)*stride;
            if(camera<blocks[b].firstSlot||uint64_t(camera)>=end)continue;
            require(!found&&(camera-blocks[b].firstSlot)%stride==0,
                    "Original camera is not one unique aligned pool slot");
            const uint32_t index=(camera-blocks[b].firstSlot)/stride;
            // The original free routine uses the unaligned bitmap-end range.
            const uint32_t freeIndex=(camera-(blocks[b].address+8+bitmapBytes))/stride;
            require(freeIndex==index,"Original allocated/free bitmap slot indices differ");
            result={b,index};found=true;
        }
        require(found,"Original camera is absent from its actual linked pool blocks");return result;
    }
    bool sameShape(const CameraPoolState& other) const {
        if(engine!=other.engine||pool!=other.pool||offset!=other.offset||stride!=other.stride||
           count!=other.count||bitmapBytes!=other.bitmapBytes||alignment!=other.alignment||
           flags!=other.flags||blocks.size()!=other.blocks.size())return false;
        for(size_t i=0;i<blocks.size();++i)
            if(blocks[i].address!=other.blocks[i].address||blocks[i].next!=other.blocks[i].next||
               blocks[i].previous!=other.blocks[i].previous||blocks[i].firstSlot!=other.blocks[i].firstSlot)return false;
        return true;
    }
    std::vector<uint32_t> freeSlotsThrough(uint32_t target) const {
        std::vector<uint32_t> addresses;
        for(uint32_t b=0;b<blocks.size();++b)for(uint32_t i=0;i<count;++i) {
            const uint32_t address=blocks[b].firstSlot+i*stride;
            if(!occupied(b,i))addresses.push_back(address);
            if(address==target) {
                require(!occupied(b,i)&&!addresses.empty()&&addresses.size()<=128,
                        "Original target reuse bound is absent or exceeds the finite constructor limit");
                return addresses;
            }
        }
        throw Simpsons::Failure("Original target is missing from its qualified reuse scan");
    }
};
CameraPoolState captureCameraPool(Simpsons::Runtime& runtime,uint8_t* base) {
    require(Simpsons::active==&runtime&&runtime.base==base,"Camera pool capture lost its owning Runtime");
    CameraPoolState result;result.engine=PPC_LOAD_U32(0x82D0CA68);result.offset=PPC_LOAD_U32(0x82D0CFC0);
    require(result.engine&&!(result.engine&3)&&result.offset>=0x100&&result.offset<=0x1000&&
            uint64_t(result.engine)+result.offset+4<=0x100000000ull,"Original camera free-list owner/offset is invalid");
    runtime.pointer(result.engine,0x140,false);
    require(PPC_LOAD_U32(result.engine+312)==0x823FB768&&PPC_LOAD_U32(result.engine+316)==0x823FB920,
            "Original camera allocation/release callbacks are not the qualified pooled pair");
    runtime.pointer(result.engine+result.offset,4,false);result.pool=PPC_LOAD_U32(result.engine+result.offset);
    require(result.pool==0x82D0CF9C,"Original camera free-list lost its supplied static owner header");
    runtime.pointer(result.pool,36,false);
    result.stride=PPC_LOAD_U32(result.pool);result.count=PPC_LOAD_U32(result.pool+4);
    result.bitmapBytes=PPC_LOAD_U32(result.pool+8);result.alignment=PPC_LOAD_U32(result.pool+12);
    result.flags=PPC_LOAD_U32(result.pool+24);
    const uint32_t cameraBytes=PPC_LOAD_U32(0x82CD1B30);
    require(result.count>=2&&result.count<=128&&result.bitmapBytes==(result.count+7)/8&&
            result.alignment&&result.alignment<=256&&!(result.alignment&(result.alignment-1))&&
            cameraBytes>=0x8C&&cameraBytes<=0x10000&&result.stride>=0x8C&&result.stride<=0x10000&&
            result.stride==((uint64_t(cameraBytes)+result.alignment-1)&~uint64_t(result.alignment-1))&&
            result.flags==3,"Original camera pool count/stride/alignment/retirement profile is unqualified");
    const uint64_t extent=uint64_t(result.count)*result.stride+result.alignment+result.bitmapBytes+7;
    require(extent<=0x100000,"Original camera pool block exceeds the bounded read extent");
    const uint32_t sentinel=result.pool+16;uint32_t previous=sentinel;
    for(uint32_t node=PPC_LOAD_U32(sentinel);node!=sentinel;node=PPC_LOAD_U32(node)) {
        require(node&&!(node&3)&&result.blocks.size()<32&&
                (result.blocks.size()+1)*result.count<=128&&uint64_t(node)+extent<=0x100000000ull,
                "Original camera pool block list exceeds its finite address/count bounds");
        require(std::none_of(result.blocks.begin(),result.blocks.end(),[&](const auto& block){return block.address==node;}),
                "Original camera pool list repeats a non-sentinel block");
        runtime.pointer(node,uint32_t(extent),false);
        const uint64_t first=(uint64_t(node)+result.alignment+result.bitmapBytes+7)&~uint64_t(result.alignment-1);
        require(first>=uint64_t(node)+8+result.bitmapBytes&&first-(uint64_t(node)+8+result.bitmapBytes)<result.stride&&
                first+uint64_t(result.count)*result.stride<=uint64_t(node)+extent,
                "Original aligned camera slots exceed their actual block range");
        CameraPoolBlock block{node,PPC_LOAD_U32(node),PPC_LOAD_U32(node+4),uint32_t(first),{}};
        require(block.previous==previous,"Original camera pool reciprocal previous link differs");
        for(uint32_t i=0;i<result.bitmapBytes;++i)block.bitmap.push_back(PPC_LOAD_U8(node+8+i));
        for(uint32_t i=result.count;i<result.bitmapBytes*8;++i)
            require(!(block.bitmap[i/8]&(0x80u>>(i%8))),"Original camera pool has occupied padding bitmap bits");
        result.blocks.push_back(std::move(block));previous=node;
    }
    require(!result.blocks.empty()&&PPC_LOAD_U32(result.pool+20)==previous,
            "Original camera pool tail/sentinel ownership differs");return result;
}
// Diagnostic only: faithful original LOC bootstrap stops BEFORE forwarding
// the first real row-zero depth clear. No camera clear/history is substituted.
struct MainCameraOwnerObserved {};
PPCFunc* originalMainOwnerClearBody{};
PPCFunc* originalMainOwnerPresentBody{};
Simpsons::Runtime* mainOwnerRuntime{};
uint32_t mainOwnerForwardedClears{},mainOwnerPresents{};
void observeMainCameraOwnerClear(PPCContext& ctx,uint8_t* base) {
    using namespace Simpsons;
    require(mainOwnerRuntime&&active==mainOwnerRuntime&&base==mainOwnerRuntime->base,
            "Main camera diagnostic lost its owning Runtime");
    auto& runtime=*mainOwnerRuntime;
    const uint32_t camera=ctx.r3.u32,selector=ctx.r5.u32,manager=PPC_LOAD_U32(0x82D08B10);
    bool target=false;
    if(manager&&camera&&camera!=PPC_LOAD_U32(0x82E07248)&&(selector&2)&&selector<=7) {
        runtime.pointer(manager,0x140,false);
        require(PPC_LOAD_U32(manager)==0x820B6DEC&&PPC_LOAD_U32(manager+8)==0x820B6DE8&&
                PPC_LOAD_U32(manager+0x128)==4,"Main camera diagnostic encountered an invalid viewport manager");
        target=PPC_LOAD_U32(manager+0x14)==camera;
    }
    if(!target) {
        require(++mainOwnerForwardedClears<=2048,"Main camera diagnostic exceeded its ordinary clear bound");
        originalMainOwnerClearBody(ctx,base);return;
    }
    std::array<uint8_t,sizeof(PPCContext)> originalContext{};
    std::memcpy(originalContext.data(),&ctx,sizeof(ctx));
    require(ctx.lr==0x823F1BA8&&currentContext==&ctx&&runtime.firstMissionCompletion&&
            runtime.videoSettings.antialiasing==uint32_t(Graphics::Antialiasing::DLAA),
            "Main camera diagnostic did not reach the genuine AA11 LOC camera-clear callback");
    require(runtime.engineDriver&&runtime.engineAudio,"Main camera diagnostic lacks original driver/allocation observers");
    auto& driver=*runtime.engineDriver;
    const auto birth=driver.observedCameraLifetime(camera);
    const uint32_t context=PPC_LOAD_U32(0x82D6D890);
    std::optional<EngineAudioOwners::AllocationSpan> span;
    std::string spanError;
    try {span=runtime.engineAudio->allocationSpan(camera);}catch(const std::exception& error){spanError=error.what();}
    const uint64_t cameraEnd=uint64_t(camera)+0x8C;
    const uint64_t spanEnd=span?uint64_t(span->address)+span->extent:0;
    const bool contained=span&&span->generation&&camera>=span->address&&cameraEnd<=spanEnd;
    // Preserve the primary ownership tuple before incidental routed-view checks.
    std::fprintf(stderr,"[DRIVER TEST MAIN CAMERA BIRTH] camera=%08X present=%u phase=%u generation=%llu context=%08X\n",
        camera,unsigned(bool(birth)),birth?unsigned(birth->phase):~0u,
        static_cast<unsigned long long>(birth?birth->generation:0),birth?birth->context:0);
    std::fprintf(stderr,"[DRIVER TEST MAIN CAMERA SPAN] camera=%08X present=%u address=%08X requestedExtent=%08X bankGeneration=%llu cameraOffset=%llu cameraEnd=%llu spanEnd=%llu full8C=%u queryError=%s; private clear-span/frame receipt has no public getter\n",
        camera,unsigned(bool(span)),span?span->address:0,span?span->extent:0,
        static_cast<unsigned long long>(span?span->generation:0),
        static_cast<unsigned long long>(span&&camera>=span->address?uint64_t(camera)-span->address:~uint64_t(0)),
        static_cast<unsigned long long>(cameraEnd),static_cast<unsigned long long>(spanEnd),unsigned(contained),spanError.c_str());
    std::fflush(stderr);
    driver.requireContext(context);
    const auto binding=driver.cameraBinding();
    require(binding.camera==camera&&binding.viewport==std::array<uint32_t,6>{0,0,1280,720,0x3F800000,0},
            "Main camera diagnostic did not retain the actual selected row-zero full-size pair");
    const uint32_t colorRaster=PPC_LOAD_U32(camera+0x60),depthRaster=PPC_LOAD_U32(camera+0x64);
    require(binding.colorRaster==colorRaster&&binding.depthRaster==depthRaster&&depthRaster&&
            PPC_LOAD_U8(colorRaster+0x20)==5&&PPC_LOAD_U8(depthRaster+0x20)==1,
            "Main camera diagnostic did not retain the actual type5/type1 original raster pair");
    bool alphaOne=false;const auto color=driver.color(binding.colorIdentity,alphaOne);
    const auto depth=driver.depth(binding.depthIdentity);
    require(color&&depth,"Main camera diagnostic lacks nonnull actual public routed attachment lookup owners");
    // These are routed public lookups, not private original C/D getters.
    // Depth pixelWidth/Height is logical; storageWidth/Height is physical.
    std::fprintf(stderr,"[DRIVER TEST MAIN CAMERA OWNER] camera=%08X loading=%08X manager=%08X row0=%08X engine=%08X current=%08X inPass=%u selector=%u caller=%08X sp=%08X context=%08X routedColor=%p routedDepth=%p C=%08X/%08X D=%08X/%08X colorLogical=%ux%u colorPhysical=%ux%u depthLogical=%ux%u depthPhysical=%ux%u alphaOne=%u copies=%llu attempts=%llu clears=%llu forwardedClears=%u; public routed lookups, not private original C/D getters\n",
        camera,PPC_LOAD_U32(0x82E07248),manager,PPC_LOAD_U32(manager+0x14),PPC_LOAD_U32(0x82D0CA68),
        PPC_LOAD_U32(0x82E3DD60),PPC_LOAD_U32(0x82D0CB1C),selector,uint32_t(ctx.lr),ctx.r1.u32,context,
        static_cast<void*>(color.get()),static_cast<void*>(depth.get()),colorRaster,binding.colorIdentity,
        depthRaster,binding.depthIdentity,color->width,color->height,color->pixelWidth(),color->pixelHeight(),
        depth->pixelWidth(),depth->pixelHeight(),depth->storageWidth(),depth->storageHeight(),unsigned(alphaOne),
        static_cast<unsigned long long>(driver.frontCopyCount()),static_cast<unsigned long long>(driver.presentationAttemptCount()),
        static_cast<unsigned long long>(driver.cameraClearCount()),mainOwnerForwardedClears);
    require(birth&&birth->camera==camera&&birth->phase==NativeCameraLifetimePhase::Constructed&&
            birth->generation&&birth->context==context,"Main camera diagnostic lacks a genuine current constructor/context receipt");
    const auto pool=captureCameraPool(runtime,base);const auto slot=pool.slot(camera);
    const auto& block=pool.blocks[slot[0]];
    const uint64_t blockExtent=uint64_t(pool.count)*pool.stride+pool.alignment+pool.bitmapBytes+7;
    std::fprintf(stderr,"[DRIVER TEST MAIN CAMERA POOL] E=%08X allocate=%08X release=%08X backingAllocate=%08X backingRelease=%08X offset=%08X F=%08X stride=%u count=%u bitmapBytes=%u alignment=%u flags=%08X block=%08X blockExtent=%llu firstSlot=%08X blockIndex=%u slotIndex=%u occupied=%u next=%08X previous=%08X linkedBlocks=%zu\n",
        pool.engine,PPC_LOAD_U32(pool.engine+312),PPC_LOAD_U32(pool.engine+316),PPC_LOAD_U32(pool.engine+264),
        PPC_LOAD_U32(pool.engine+268),pool.offset,pool.pool,pool.stride,pool.count,pool.bitmapBytes,pool.alignment,
        pool.flags,block.address,static_cast<unsigned long long>(blockExtent),block.firstSlot,slot[0],slot[1],
        unsigned(pool.occupied(slot[0],slot[1])),block.next,block.previous,pool.blocks.size());
    // Current callback/manager identity distinguishes observer coverage routes.
    const uint32_t heapManager=PPC_LOAD_U32(0x82D57244);
    if(heapManager) {
        runtime.pointer(heapManager,4,false);const uint32_t vtable=PPC_LOAD_U32(heapManager);
        runtime.pointer(vtable,24,false);
        std::fprintf(stderr,"[DRIVER TEST MAIN CAMERA HEAP] manager=%08X vtable=%08X allocate=%08X directAllocate=%08X\n",
            heapManager,vtable,PPC_LOAD_U32(vtable),PPC_LOAD_U32(vtable+20));
    } else std::fprintf(stderr,"[DRIVER TEST MAIN CAMERA HEAP] manager=00000000; no manager identity inferred\n");
    require(pool.occupied(slot[0],slot[1])&&cameraEnd<=uint64_t(block.firstSlot)+uint64_t(slot[1]+1)*pool.stride,
            "Main camera diagnostic lacks an occupied full8C original pool slot");
    require(spanError.empty(),"Main camera diagnostic encountered a Closing/failed bank-span query; tuple retained");
    require(std::memcmp(originalContext.data(),&ctx,sizeof(ctx))==0,
            "Read-only main camera diagnostic changed guest registers/context padding");
    std::fflush(stderr);
    throw MainCameraOwnerObserved{}; // Target clear is deliberately NOT forwarded.
}
void observeMainCameraOwnerPresent(PPCContext& ctx,uint8_t* base) {
    require(mainOwnerRuntime&&Simpsons::active==mainOwnerRuntime&&base==mainOwnerRuntime->base,
            "Main camera diagnostic presentation lost its owning Runtime");
    originalMainOwnerPresentBody(ctx,base);
    require(++mainOwnerPresents<=512,"Main camera diagnostic exceeded its real presentation bound without row-zero clear");
}
struct MainCameraOwnerObservation {
    uint8_t* base;
    explicit MainCameraOwnerObservation(Simpsons::Runtime& runtime):base(runtime.base) {
        require(!mainOwnerRuntime&&!originalMainOwnerClearBody&&!originalMainOwnerPresentBody,
                "Main camera diagnostic observer is already active");
        const auto clear=PPC_LOOKUP_FUNC(base,0x823EE940),present=PPC_LOOKUP_FUNC(base,0x823EE820);
        require(clear&&present,"Main camera diagnostic lacks original clear/presentation dispatch");
        mainOwnerRuntime=&runtime;originalMainOwnerClearBody=clear;originalMainOwnerPresentBody=present;
        mainOwnerForwardedClears=mainOwnerPresents=0;
        PPC_LOOKUP_FUNC(base,0x823EE940)=observeMainCameraOwnerClear;
        PPC_LOOKUP_FUNC(base,0x823EE820)=observeMainCameraOwnerPresent;
    }
    ~MainCameraOwnerObservation() {
        PPC_LOOKUP_FUNC(base,0x823EE940)=originalMainOwnerClearBody;
        PPC_LOOKUP_FUNC(base,0x823EE820)=originalMainOwnerPresentBody;
        originalMainOwnerClearBody=originalMainOwnerPresentBody=nullptr;mainOwnerRuntime=nullptr;
    }
};
void temporalMainCameraOwnerContracts(const char* image,const char* privateProfile,const char* privateContent,const char* profileId) {
    using namespace Simpsons;
    Runtime runtime;runtime.firstMissionCompletion=true;
    const auto profile=std::filesystem::absolute(privateProfile).lexically_normal();
    const auto content=std::filesystem::absolute(privateContent).lexically_normal();
    require(std::filesystem::is_directory(profile)&&std::filesystem::is_directory(content)&&profile!=content&&profileId&&*profileId,
            "Main camera diagnostic requires existing separate private profile/content clones and profile ID");
    runtime.videoSettingsPath=profile;runtime.videoSettingsPath+=L".video.cfg";
    runtime.videoSettings=NativeVideoSettings::load(runtime.videoSettingsPath);
    require(runtime.videoSettings.antialiasing==uint32_t(Graphics::Antialiasing::DLAA),
            "Main camera diagnostic private v8 settings must select AA11 before driver creation");
    runtime.requestedFrameRate=runtime.videoSettings.frameRate;
    runtime.uncappedFrameRate=runtime.videoSettings.frameRate==0;runtime.vsyncEnabled=runtime.videoSettings.vsync;
    runtime.configureLocalPlayers(profile);runtime.load(image);runtime.contentRoot=content;
    runtime.activateLocalPlayer(0,profileId);
    PPCContext startup{};runtime.initialize(startup);
    bool observed=false;std::atomic<bool> expired=false;
    {
        MainCameraOwnerObservation observation(runtime);
        // Cancellation is the existing Runtime stop path, never a success marker.
        // This owned thread joins before Runtime or dispatch-owner destruction.
        std::jthread watchdog([&](std::stop_token stop) {
            for(unsigned i=0;i<600&&!stop.stop_requested();++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if(!stop.stop_requested()) {expired=true;runtime.requestStop("Main camera owner diagnostic exceeded60seconds without its real row-zero clear");}
        });
        try {runOriginal(startup,runtime.base);}catch(const MainCameraOwnerObserved&){observed=true;}
    }
    require(observed&&!expired,"Actual AA11 LOC startup did not reach the bounded original row-zero depth clear");
    require(!mainOwnerRuntime&&!originalMainOwnerClearBody&&!originalMainOwnerPresentBody&&
            PPC_LOOKUP_FUNC(runtime.base,0x823EE940)!=observeMainCameraOwnerClear&&
            PPC_LOOKUP_FUNC(runtime.base,0x823EE820)!=observeMainCameraOwnerPresent,
            "Main camera diagnostic failed to restore original dispatch mappings");
    std::printf("PASS: actual AA11 original LOC row-zero camera constructor/pair/pool and allocation-span tuple captured BEFORE target clear; no main clear/frame/history/NGX evaluation success claim / %zu checks\n",checks);
}
// Separate main-camera lifetime regression. Reuse the read-only owner diagnostic
// and its real loading presentations; only its genuine target clear is advanced.
struct MainCameraLifetimeClearObserved {};
struct MainCameraLifetimeClearCapture {
    uint32_t camera{},manager{},context{},targetForwards{},ordinaryPresents{};
    uint32_t currentCamera{},inPass{},engineCamera{};
    uint64_t clears{},copies{},attempts{};
    Simpsons::NativeCameraBinding binding{};
    Simpsons::NativeCameraLifetime birth{};
    Simpsons::Graphics::NativeTemporalSceneOrigins before{},cleared{};
};
MainCameraLifetimeClearCapture* mainLifetimeClearCapture{};
void observeMainCameraLifetimeClear(PPCContext& ctx,uint8_t* base) {
    // The existing exact diagnostic validates the original Runtime, LOC/AA11,
    // manager, row0, base birth, raster pair and occupied camera pool slot.
    try {observeMainCameraOwnerClear(ctx,base);return;}
    catch(const MainCameraOwnerObserved&) {}
    using namespace Simpsons;
    require(mainLifetimeClearCapture&&mainOwnerRuntime&&active==mainOwnerRuntime,
            "Main camera lifetime clear lost its composed owner observation");
    auto& capture=*mainLifetimeClearCapture;auto& runtime=*mainOwnerRuntime;
    auto& driver=*runtime.engineDriver;
    require(!capture.targetForwards&&ctx.r5.u32==2&&ctx.lr==0x823F1BA8&&currentContext==&ctx,
            "Main camera lifetime did not stop at its first genuine row-zero selector2 clear");
    capture.camera=ctx.r3.u32;capture.manager=PPC_LOAD_U32(0x82D08B10);
    capture.context=PPC_LOAD_U32(0x82D6D890);capture.binding=driver.cameraBinding();
    const auto birth=driver.observedCameraLifetime(capture.camera);
    require(birth&&birth->phase==NativeCameraLifetimePhase::Constructed&&birth->generation&&
            birth->context==capture.context,"Main camera lifetime clear lacks its genuine base construction");
    capture.birth=*birth;
    require(!runtime.engineAudio->allocationSpan(capture.camera),
            "Main camera lifetime fixture did not reproduce actual U absent bank-span coverage");
    capture.before=driver.temporalSceneOrigins();
    capture.clears=driver.cameraClearCount();capture.copies=driver.frontCopyCount();
    capture.attempts=driver.presentationAttemptCount();capture.ordinaryPresents=mainOwnerPresents;
    capture.currentCamera=PPC_LOAD_U32(0x82E3DD60);capture.inPass=PPC_LOAD_U32(0x82D0CB1C);
    capture.engineCamera=PPC_LOAD_U32(PPC_LOAD_U32(0x82D0CA68));
    ++capture.targetForwards;
    originalMainOwnerClearBody(ctx,base); // Exactly one real target dispatch.
    require(ctx.r3.u32==1&&capture.targetForwards==1&&driver.cameraClearCount()==capture.clears+1&&
            driver.frontCopyCount()==capture.copies&&driver.presentationAttemptCount()==capture.attempts&&
            mainOwnerPresents==capture.ordinaryPresents,
            "Genuine main selector2 clear failed or submitted a new presentation");
    const auto binding=driver.cameraBinding();bool alphaOne=false;
    capture.cleared=driver.temporalSceneOrigins();
    require(binding.camera==capture.camera&&binding.colorRaster==capture.binding.colorRaster&&
            binding.depthRaster==capture.binding.depthRaster&&binding.colorIdentity==capture.binding.colorIdentity&&
            binding.depthIdentity==capture.binding.depthIdentity&&binding.viewport==capture.binding.viewport&&
            capture.cleared.color==capture.before.color&&capture.cleared.depth==capture.before.depth&&
            driver.color(binding.colorIdentity,alphaOne)==capture.cleared.color&&
            driver.depth(binding.depthIdentity)==capture.cleared.depth,
            "Genuine main depth clear lost its actual selected original Scene owners");
    require(capture.cleared.components[1].kind==Graphics::TemporalRasterOriginKind::Uniform&&
            capture.cleared.components[1].jitter==std::array<float,2>{}&&
            capture.cleared.components[1].revision>capture.before.components[1].revision,
            "Genuine selector2 clear did not publish a new invariant actual depth origin");
    for(const auto& origin:capture.cleared.components)
        require(origin.kind!=Graphics::TemporalRasterOriginKind::Unknown&&origin.revision,
                "Main clear retained an unknown or unwritten actual component origin");
    const auto clearedDepth=driver.readbackDepth(binding.depthIdentity);
    require(clearedDepth.size()==size_t(capture.cleared.depth->storageWidth())*
            capture.cleared.depth->storageHeight()*8,"Main selector2 depth readback has the wrong physical extent");
    bool allCleared=true;
    for(size_t i=0;i<clearedDepth.size();i+=8) {
        float depth{};std::memcpy(&depth,clearedDepth.data()+i,4);allCleared=allCleared&&depth==0.0f;
    }
    require(allCleared,"Genuine selector2 clear did not clear every actual D32 depth value");
    throw MainCameraLifetimeClearObserved{}; // No original geometry follows.
}
struct MainCameraLifetimeClearObservation {
    MainCameraOwnerObservation owner;
    MainCameraLifetimeClearObservation(Simpsons::Runtime& runtime,MainCameraLifetimeClearCapture& capture):owner(runtime) {
        require(!mainLifetimeClearCapture,"Nested main camera lifetime clear observation");
        mainLifetimeClearCapture=&capture;
        PPC_LOOKUP_FUNC(owner.base,0x823EE940)=observeMainCameraLifetimeClear;
    }
    ~MainCameraLifetimeClearObservation() {
        PPC_LOOKUP_FUNC(owner.base,0x823EE940)=observeMainCameraOwnerClear;
        mainLifetimeClearCapture=nullptr; // owner then restores both real dispatches.
    }
};
void rejectsMainCameraLifetime(const std::function<void()>& operation) {
    bool rejected=false;std::string actual;
    try {operation();}catch(const Simpsons::Failure& error) {rejected=true;actual=error.what();}
    if(rejected&&actual!="Temporal selected main camera lost its original construction lifetime")
        std::fprintf(stderr,"[DRIVER TEST MAIN LIFETIME] unexpected rejection: %s\n",actual.c_str());
    require(rejected&&actual=="Temporal selected main camera lost its original construction lifetime",
            "Main camera stale selection was not rejected at its exact construction lifetime guard");
}
struct MainCameraPluginClosingCapture {
    CameraPluginClosingTarget target{};
    uint32_t camera{},observations{},forwards{},completions{};
};
PPCFunc* originalMainCameraPluginDestroyBody{};
MainCameraPluginClosingCapture* mainCameraPluginClosingCapture{};
void observeMainCameraPluginClosing(PPCContext& ctx,uint8_t* base) {
    require(mainCameraPluginClosingCapture&&originalMainCameraPluginDestroyBody,
            "Main camera plugin callback lost its actual observation owner");
    auto& capture=*mainCameraPluginClosingCapture;const auto& target=capture.target;
    const bool observed=ctx.r3.u32==capture.camera&&ctx.r31.u32==target.tail;
    if(observed) {
        require(++capture.observations==1&&Simpsons::active&&Simpsons::active->base==base&&base==target.base&&
                Simpsons::currentContext==&ctx&&uint32_t(ctx.lr)==0x823FB3DC&&
                ctx.r30.u32==0x82CD1B30&&ctx.ctr.u32==target.callback&&
                ctx.r4.u32==target.offset&&ctx.r5.u32==target.size,
                "Main first plugin destructor changed its original callback context or ABI");
        require(PPC_LOAD_U32(0x82CD1B30+0x14)==target.tail&&
                PPC_LOAD_U32(target.tail+0x38)==0x82CD1B30&&
                PPC_LOAD_U32(target.tail)==target.offset&&PPC_LOAD_U32(target.tail+4)==target.size&&
                PPC_LOAD_U32(target.tail+8)==target.id&&PPC_LOAD_U32(target.tail+0x24)==target.callback,
                "Main first plugin destructor lost its live original registry/record owner");
        auto& driver=*Simpsons::active->engineDriver;
        const auto lifetime=driver.observedCameraLifetime(capture.camera);
        require(lifetime&&lifetime->phase==Simpsons::NativeCameraLifetimePhase::Closing&&
                lifetime->generation==target.generation&&lifetime->context==target.context,
                "Main camera was not genuinely Closing before its first original plugin callback");
        driver.requireContext(target.context);
        rejectsMainCameraLifetime([&]{(void)driver.cameraBinding();});
        rejectsMainCameraLifetime([&]{driver.setUiDrawing(true);});
        ++capture.forwards;
    }
    originalMainCameraPluginDestroyBody(ctx,base); // Every real callback forwards.
    if(observed)++capture.completions;
}
struct MainCameraPluginClosingObservation {
    uint8_t* base;
    MainCameraPluginClosingCapture& capture;
    MainCameraPluginClosingObservation(uint8_t* memory,uint32_t camera,MainCameraPluginClosingCapture& value):base(memory),capture(value) {
        require(!mainCameraPluginClosingCapture&&!originalMainCameraPluginDestroyBody,
                "Nested main camera plugin Closing observation");
        require(Simpsons::active&&Simpsons::active->base==base&&Simpsons::active->engineDriver,
                "Main plugin Closing observation lacks its actual Runtime/driver");
        auto& runtime=*Simpsons::active;auto& driver=*runtime.engineDriver;
        runtime.pointer(0x82CD1B30,0x18,false);
        const uint32_t tail=PPC_LOAD_U32(0x82CD1B30+0x14);
        require(tail&&!(tail&3),"Main first plugin destructor has no aligned actual registry tail");
        runtime.pointer(tail,0x3C,false);
        CameraPluginClosingTarget target{tail,PPC_LOAD_U32(tail+0x24),PPC_LOAD_U32(tail),
            PPC_LOAD_U32(tail+4),PPC_LOAD_U32(tail+8),PPC_LOAD_U32(0x82D6D890),0,base};
        require(PPC_LOAD_U32(tail+0x38)==0x82CD1B30&&
                uint64_t(target.offset)+target.size<=PPC_LOAD_U32(0x82CD1B30)&&
                !(target.callback&3)&&target.callback>=PPC_CODE_BASE&&target.callback<PPC_CODE_BASE+PPC_CODE_SIZE,
                "Main first plugin destructor registry extent or callback is unqualified");
        const auto body=PPC_LOOKUP_FUNC(base,target.callback);
        require(body&&body!=observeMainCameraPluginClosing&&body!=observeCameraPluginClosing,
                "Main first plugin destructor lacks its exact original indirect dispatch");
        const auto birth=driver.observedCameraLifetime(camera);
        require(birth&&birth->phase==Simpsons::NativeCameraLifetimePhase::Constructed&&birth->generation&&
                birth->context==target.context,"Main Closing callback capture lacks its genuine current birth");
        driver.requireContext(target.context);target.generation=birth->generation;
        capture.target=target;capture.camera=camera;capture.observations=capture.forwards=capture.completions=0;
        originalMainCameraPluginDestroyBody=body;mainCameraPluginClosingCapture=&capture;
        PPC_LOOKUP_FUNC(base,target.callback)=observeMainCameraPluginClosing;
    }
    ~MainCameraPluginClosingObservation() {
        PPC_LOOKUP_FUNC(base,capture.target.callback)=originalMainCameraPluginDestroyBody;
        originalMainCameraPluginDestroyBody=nullptr;mainCameraPluginClosingCapture=nullptr;
    }
};
// Independent bounded source-grid transport gold from the existing native UI
// fixture. This accepts the proven FP nearest interval, never identical jittered RGB.
std::array<uint32_t,2> mainLifetimeBackgroundCandidates(uint32_t pixel,uint32_t source,uint32_t output,float jitter) {
    require(source&&output&&std::isfinite(jitter)&&std::abs(jitter)<=0.5f,
            "Main UI source-grid gold has an invalid physical extent or jitter");
    if(jitter==0.0f) {
        const uint32_t selected=uint32_t(std::min((uint64_t(2)*pixel+1)*source/(uint64_t(2)*output),uint64_t(source-1)));
        return {selected,selected};
    }
    const long double coordinate=(static_cast<long double>(pixel)+0.5L)*source/output+static_cast<long double>(jitter);
    const long double error=8*std::numeric_limits<float>::epsilon()*std::max(1.0L,std::abs(coordinate));
    const int64_t low=int64_t(std::floor(coordinate-error)),high=int64_t(std::floor(coordinate+error));
    require(high-low<=1,"Main UI source-grid nearest interval exceeded its proven FP bound");
    return {uint32_t(std::clamp(low,int64_t(0),int64_t(source-1))),uint32_t(std::clamp(high,int64_t(0),int64_t(source-1)))};
}
void mainLifetimeDefinedTransportGold(const std::vector<uint8_t>& sourceColor,const std::vector<uint8_t>& sourceDepth,
        uint32_t sw,uint32_t sh,const std::vector<uint8_t>& outputColor,const std::vector<uint8_t>& outputDepth,
        uint32_t ow,uint32_t oh,std::array<float,2> sourceJitter) {
    require(sw&&sh&&ow&&oh&&sourceColor.size()==size_t(sw)*sh*4&&sourceDepth.size()==size_t(sw)*sh*8&&
            outputColor.size()==size_t(ow)*oh*4&&outputDepth.size()==size_t(ow)*oh*8,
            "Main UI transport lost its actual paired physical readback extents");
    // A bounded real alpha/D32/S8 sample complements the complete depth-clear
    // scan. Renderer-wide transport and RGB oracles remain in their own fixtures.
    auto samples=[](uint32_t extent) {
        return std::array<uint32_t,7>{0,extent/8,extent/4,extent/2,
            uint32_t(uint64_t(extent)*3/4),uint32_t(uint64_t(extent)*7/8),extent-1};
    };
    bool matched=true;
    for(uint32_t y:samples(oh))for(uint32_t x:samples(ow)) {
        const size_t at=size_t(y)*ow+x;uint32_t outputPacked{};
        std::memcpy(&outputPacked,outputColor.data()+at*4,4);bool paired=false;
        const auto xs=mainLifetimeBackgroundCandidates(x,sw,ow,sourceJitter[0]);
        const auto ys=mainLifetimeBackgroundCandidates(y,sh,oh,sourceJitter[1]);
        for(uint32_t sy:ys)for(uint32_t sx:xs) {
            const size_t sourceAt=size_t(sy)*sw+sx;uint32_t sourcePacked{};
            std::memcpy(&sourcePacked,sourceColor.data()+sourceAt*4,4);
            paired=paired||((outputPacked>>30)==(sourcePacked>>30)&&
                std::memcmp(outputDepth.data()+at*8,sourceDepth.data()+sourceAt*8,5)==0);
        }
        matched=matched&&paired;
    }
    require(matched,"Main UI packed alpha and defined D32/stencil do not share the captured source-grid texel (X24 excluded)");
}
void temporalMainCameraLifetimeContracts(const char* image,const char* privateProfile,const char* privateContent,const char* profileId) {
    using namespace Simpsons;
    Runtime runtime;runtime.firstMissionCompletion=true;
    const auto profile=std::filesystem::absolute(privateProfile).lexically_normal();
    const auto content=std::filesystem::absolute(privateContent).lexically_normal();
    require(std::filesystem::is_directory(profile)&&std::filesystem::is_directory(content)&&profile!=content&&profileId&&*profileId,
            "Main camera lifetime requires existing separate private profile/content clones and profile ID");
    runtime.videoSettingsPath=profile;runtime.videoSettingsPath+=L".video.cfg";
    runtime.videoSettings=NativeVideoSettings::load(runtime.videoSettingsPath);
    require(runtime.videoSettings.antialiasing==uint32_t(Graphics::Antialiasing::DLAA),
            "Main camera lifetime private v8 settings must select AA11 before driver creation");
    runtime.requestedFrameRate=runtime.videoSettings.frameRate;
    runtime.uncappedFrameRate=runtime.videoSettings.frameRate==0;runtime.vsyncEnabled=runtime.videoSettings.vsync;
    runtime.configureLocalPlayers(profile);runtime.load(image);runtime.contentRoot=content;
    runtime.activateLocalPlayer(0,profileId);
    PPCContext startup{};runtime.initialize(startup);const PPCContext entry=startup;auto* base=runtime.base;
    MainCameraLifetimeClearCapture capture;bool observed=false;std::atomic<bool> expired=false;
    {
        MainCameraLifetimeClearObservation observation(runtime,capture);
        std::jthread watchdog([&](std::stop_token stop) {
            for(unsigned i=0;i<600&&!stop.stop_requested();++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if(!stop.stop_requested()) {expired=true;runtime.requestStop("Main camera lifetime exceeded60seconds without its real row-zero clear");}
        });
        try {runOriginal(startup,base);}catch(const MainCameraLifetimeClearObserved&){observed=true;}
    }
    require(observed&&!expired&&capture.targetForwards==1&&!mainLifetimeClearCapture&&
            !mainOwnerRuntime&&!originalMainOwnerClearBody&&!originalMainOwnerPresentBody&&
            PPC_LOOKUP_FUNC(base,0x823EE940)!=observeMainCameraLifetimeClear&&
            PPC_LOOKUP_FUNC(base,0x823EE820)!=observeMainCameraOwnerPresent,
            "Main camera lifetime did not stop after its real clear and restore both original dispatches");
    auto& driver=*runtime.engineDriver;EngineCpuCalls cpu(entry,base);
    const uint32_t camera=capture.camera;const auto& binding=capture.binding;const auto& birth=capture.birth;
    const auto sourceColorOwner=capture.cleared.color;const auto sourceDepthOwner=capture.cleared.depth;
    require(sourceColorOwner&&sourceDepthOwner&&sourceColorOwner->width==1280&&sourceColorOwner->height==720&&
            sourceDepthOwner->pixelWidth()==1280&&sourceDepthOwner->pixelHeight()==720&&
            sourceColorOwner->pixelWidth()==sourceDepthOwner->storageWidth()&&
            sourceColorOwner->pixelHeight()==sourceDepthOwner->storageHeight(),
            "Main lifetime source owners lost their distinct logical/physical extents");
    const auto sourceColor=driver.readbackColor(binding.colorIdentity),sourceDepth=driver.readbackDepth(binding.depthIdentity);
    std::optional<std::array<float,2>> sourceGrid;
    for(const auto& component:capture.cleared.components)if(component.kind==Graphics::TemporalRasterOriginKind::Grid) {
        require(!sourceGrid||*sourceGrid==component.jitter,"Main clear retained incompatible actual component grids");
        sourceGrid=component.jitter;
    }
    const auto sourceJitter=sourceGrid.value_or(std::array<float,2>{});
    driver.setUiDrawing(false);driver.setUiDrawing(true);
    bool alphaOne=false;const auto promotedColor=driver.color(binding.colorIdentity,alphaOne);
    const auto promotedDepth=driver.depth(binding.depthIdentity);
    require(driver.cameraBinding().camera==camera&&PPC_LOAD_U32(capture.manager+0x14)==camera&&
            camera!=PPC_LOAD_U32(0x82E07248)&&promotedColor&&promotedDepth&&
            promotedColor!=sourceColorOwner&&promotedDepth!=sourceDepthOwner&&
            promotedColor->width==1280&&promotedColor->height==720&&
            promotedDepth->pixelWidth()==1280&&promotedDepth->pixelHeight()==720&&
            promotedColor->pixelWidth()==promotedDepth->storageWidth()&&
            promotedColor->pixelHeight()==promotedDepth->storageHeight(),
            "Real zero-geometry main UI begin did not retain its selected row-zero camera and promote both actual owners");
    const auto uiOrigins=driver.temporalSceneOrigins();
    require(uiOrigins.color==sourceColorOwner&&uiOrigins.depth==sourceDepthOwner&&
            uiOrigins.components==capture.cleared.components,
            "Main UI background transport changed its retained original Scene origins");
    mainLifetimeDefinedTransportGold(sourceColor,sourceDepth,sourceColorOwner->pixelWidth(),sourceColorOwner->pixelHeight(),
        driver.readbackColor(binding.colorIdentity),driver.readbackDepth(binding.depthIdentity),
        promotedColor->pixelWidth(),promotedColor->pixelHeight(),sourceJitter);
    driver.setUiDrawing(true); // Exercise the already-promoted positive shortcut.
    require(driver.color(binding.colorIdentity,alphaOne)==promotedColor&&driver.depth(binding.depthIdentity)==promotedDepth,
            "Repeated genuine main UI begin changed the captured promoted owner pair");
    uint64_t expectedClears=capture.clears+1;
    auto expectedOrigins=uiOrigins.components;
    auto unchanged=[&] {
        require(PPC_LOAD_U32(capture.manager+0x14)==camera&&PPC_LOAD_U32(0x82E3DD60)==capture.currentCamera&&
                PPC_LOAD_U32(0x82D0CB1C)==capture.inPass&&PPC_LOAD_U32(PPC_LOAD_U32(0x82D0CA68))==capture.engineCamera&&
                driver.color(binding.colorIdentity,alphaOne)==promotedColor&&driver.depth(binding.depthIdentity)==promotedDepth&&
                driver.cameraClearCount()==expectedClears&&driver.frontCopyCount()==capture.copies&&
                driver.presentationAttemptCount()==capture.attempts,
                "Main lifetime negative changed the stale row-zero/guest selection, promoted owners, clear or presentation counts");
        const auto origins=driver.temporalSceneOrigins();
        require(origins.color==sourceColorOwner&&origins.depth==sourceDepthOwner&&origins.components==expectedOrigins,
                "Main lifetime rejection changed the still-live original Scene owner/origin receipt");
    };
    unchanged();
    // Original main clear does not select: preserve its explicit admission while
    // a genuine loading camera is bound, then return through the real RW clear.
    const uint32_t loading=PPC_LOAD_U32(0x82E07248);
    require(loading&&loading!=camera,"Main roundtrip has no distinct actual loading camera");
    const auto loadingBirth=driver.observedCameraLifetime(loading);
    require(loadingBirth&&loadingBirth->phase==NativeCameraLifetimePhase::Constructed&&
            loadingBirth->generation&&loadingBirth->context==birth.context,
            "Main roundtrip loading camera lacks its genuine live context/birth");
    cpu.invoke(0x823EE6C8,loading); // Existing original selection callback.
    require(driver.cameraBinding().camera==loading,"Genuine loading selection did not bind its actual original camera");
    driver.setUiDrawing(true); // A retained main admission must not poison a valid nonmain binding.
    unchanged();
    driver.setUiDrawing(false);
    require(cpu.invoke(0x823F1B80,camera,0x82D6C09C,7)==camera,
            "Original main selector7 clear failed after loading selection without an explicit main reselect");
    require(driver.cameraBinding().camera==camera&&driver.cameraClearCount()==capture.clears+2&&
            driver.frontCopyCount()==capture.copies&&driver.presentationAttemptCount()==capture.attempts,
            "Main roundtrip did not submit exactly its real clear before binding the remembered original camera");
    const auto roundtripOrigins=driver.temporalSceneOrigins();
    require(roundtripOrigins.color==sourceColorOwner&&roundtripOrigins.depth==sourceDepthOwner,
            "Main roundtrip clear changed the strong original Scene owners");
    for(const auto& origin:roundtripOrigins.components)
        require(origin.kind==Graphics::TemporalRasterOriginKind::Uniform,
                "Original main selector7 did not publish its actual full color/depth/stencil clear origins");
    driver.setUiDrawing(true);
    require(driver.color(binding.colorIdentity,alphaOne)==promotedColor&&driver.depth(binding.depthIdentity)==promotedDepth,
            "Main roundtrip UI promotion changed its cached full-output owner pair");
    const auto roundtripUiOrigins=driver.temporalSceneOrigins();
    require(roundtripUiOrigins.color==sourceColorOwner&&roundtripUiOrigins.depth==sourceDepthOwner&&
            roundtripUiOrigins.components==roundtripOrigins.components,
            "Main roundtrip UI transport changed the real successful clear origins");
    expectedClears=capture.clears+2;expectedOrigins=roundtripUiOrigins.components;
    unchanged();
    std::fprintf(stderr,"[DRIVER TEST MAIN NONMAIN ROUNDTRIP] main=%08X birth=%llu loading=%08X loadingBirth=%llu selector=7 originalWrapper=823F1B80 explicitMainReselect=0 clears=%llu\n",
        camera,static_cast<unsigned long long>(birth.generation),loading,static_cast<unsigned long long>(loadingBirth->generation),
        static_cast<unsigned long long>(driver.cameraClearCount()));
    std::vector<uint32_t> constructed;
    const auto poolBeforeAnchor=captureCameraPool(runtime,base);const auto targetBeforeAnchor=poolBeforeAnchor.slot(camera);
    require(poolBeforeAnchor.occupied(targetBeforeAnchor[0],targetBeforeAnchor[1]),
            "Genuine main camera lost its original occupied pool slot before retention setup");
    auto printTargetBitmap=[&](const char* stage,const CameraPoolState& pool,const std::array<uint32_t,2>& target) {
        const auto& block=pool.blocks[target[0]];
        std::fprintf(stderr,"[DRIVER TEST MAIN RETENTION POOL] stage=%s target=%08X block=%08X blockIndex=%u slotIndex=%u count=%u bitmap=",
            stage,camera,block.address,target[0],target[1],pool.count);
        for(uint8_t byte:block.bitmap)std::fprintf(stderr,"%02X",unsigned(byte));
        std::fprintf(stderr," occupied=%u linkedBlocks=%zu\n",unsigned(pool.occupied(target[0],target[1])),pool.blocks.size());
    };
    printTargetBitmap("before-setup",poolBeforeAnchor,targetBeforeAnchor);
    uint32_t firstSibling=0,freeSiblingCount=0;
    for(uint32_t slot=0;slot<poolBeforeAnchor.count;++slot)if(!poolBeforeAnchor.occupied(targetBeforeAnchor[0],slot)) {
        const uint32_t address=poolBeforeAnchor.blocks[targetBeforeAnchor[0]].firstSlot+slot*poolBeforeAnchor.stride;
        if(!firstSibling)firstSibling=address;
        ++freeSiblingCount;
    }
    uint32_t anchor=0;std::array<uint32_t,2> anchorSlot{};NativeCameraLifetime anchorBirth{};bool anchorOwned=false;
    std::fprintf(stderr,"[DRIVER TEST MAIN RETENTION] freeSiblings=%u firstSibling=%08X branch=%s\n",
        freeSiblingCount,firstSibling,firstSibling?"owned-original-constructors":"existing-authenticated-peer");
    if(firstSibling) {
        // Fill only the actual free prefix in the original list/MSB-first scan.
        // Every result stays live and is owned until normal original cleanup.
        const auto anchorCandidates=poolBeforeAnchor.freeSlotsThrough(firstSibling);
        require(!anchorCandidates.empty()&&anchorCandidates.back()==firstSibling,
                "Main retention has no qualified constructor prefix through its free target-block sibling");
        std::fprintf(stderr,"[DRIVER TEST MAIN RETENTION] originalConstructorPrefix=%zu\n",anchorCandidates.size());
        for(size_t attempt=0;attempt<anchorCandidates.size();++attempt) {
            const uint32_t next=cpu.invoke(0x823F1DB0);
            std::fprintf(stderr,"[DRIVER TEST MAIN RETENTION CONSTRUCTOR] attempt=%zu expected=%08X actual=%08X\n",
                attempt,anchorCandidates[attempt],next);
            require(next!=0,"Original main retention constructor failed within its derived finite prefix");
            constructed.push_back(next);
            require(next==anchorCandidates[attempt],"Original main retention constructor changed its qualified pool scan order");
            const auto nextBirth=driver.observedCameraLifetime(next);
            require(nextBirth&&nextBirth->camera==next&&nextBirth->phase==NativeCameraLifetimePhase::Constructed&&
                    nextBirth->generation>birth.generation&&nextBirth->context==birth.context,
                    "Owned main retention candidate lacks a genuine new constructor/context receipt");
            const auto currentPool=captureCameraPool(runtime,base);
            require(poolBeforeAnchor.sameShape(currentPool),"Owned main retention constructor changed the qualified pool shape");
            const auto currentTarget=currentPool.slot(camera),currentSlot=currentPool.slot(next);
            printTargetBitmap("owned-constructor",currentPool,currentTarget);
            std::fprintf(stderr,"[DRIVER TEST MAIN RETENTION CONSTRUCTOR] camera=%08X block=%08X slot=%u generation=%llu context=%08X\n",
                next,currentPool.blocks[currentSlot[0]].address,currentSlot[1],
                static_cast<unsigned long long>(nextBirth->generation),nextBirth->context);
            require(currentTarget==targetBeforeAnchor,"Owned main retention constructor changed the target's qualified slot");
            require(currentPool.occupied(currentTarget[0],currentTarget[1]),"Owned main retention constructor cleared the target occupied bit");
            require(currentPool.occupied(currentSlot[0],currentSlot[1]),"Owned main retention candidate lacks its actual occupied bit");
            if(next==firstSibling) {anchor=next;anchorSlot=currentSlot;anchorBirth=*nextBirth;anchorOwned=true;}
        }
        require(anchor==firstSibling&&anchorOwned,"Original constructor prefix did not reach its actual target-block sibling");
    } else {
        // A full target block cannot accept a new owned sibling. Keep an already
        // live, typed peer untouched; this is no ownership or destructor claim.
        for(uint32_t slot=0;slot<poolBeforeAnchor.count&&!anchor;++slot) {
            if(slot==targetBeforeAnchor[1])continue;
            require(poolBeforeAnchor.occupied(targetBeforeAnchor[0],slot),"Full main target block unexpectedly contains a free sibling");
            const uint32_t peer=poolBeforeAnchor.blocks[targetBeforeAnchor[0]].firstSlot+slot*poolBeforeAnchor.stride;
            const auto peerBirth=driver.observedCameraLifetime(peer);
            std::fprintf(stderr,"[DRIVER TEST MAIN RETENTION PEER] camera=%08X slot=%u receipt=%u phase=%u generation=%llu context=%08X\n",
                peer,slot,unsigned(bool(peerBirth)),peerBirth?uint32_t(peerBirth->phase):~0u,
                peerBirth?static_cast<unsigned long long>(peerBirth->generation):0ull,peerBirth?peerBirth->context:0u);
            if(peerBirth&&peerBirth->camera==peer&&peerBirth->phase==NativeCameraLifetimePhase::Constructed&&
               peerBirth->generation&&peerBirth->context==birth.context) {
                require(poolBeforeAnchor.slot(peer)==std::array<uint32_t,2>{targetBeforeAnchor[0],slot},
                        "Existing main retention peer is not its full aligned original pool slot");
                anchor=peer;anchorSlot={targetBeforeAnchor[0],slot};anchorBirth=*peerBirth;
            }
        }
        require(anchor&&!anchorOwned,"Full main target block lacks an authenticated live same-context retention peer");
    }
    const auto poolAnchored=captureCameraPool(runtime,base);
    require(poolBeforeAnchor.sameShape(poolAnchored),"Main retention setup changed the qualified linked pool shape");
    const auto targetAnchored=poolAnchored.slot(camera),actualAnchorSlot=poolAnchored.slot(anchor);
    require(targetAnchored==targetBeforeAnchor,"Main retention setup changed the target's exact original slot");
    require(actualAnchorSlot==anchorSlot,"Main retention witness changed its captured original slot");
    require(anchorSlot[0]==targetAnchored[0],"Main retention witness is not in the exact target block");
    require(anchorSlot[1]!=targetAnchored[1],"Main retention witness aliases the target slot");
    require(poolAnchored.occupied(targetAnchored[0],targetAnchored[1]),"Main target lost its occupied bit before genuine destruction");
    auto requireRetention=[&](const CameraPoolState& pool,const char* stage) {
        const auto currentBirth=driver.observedCameraLifetime(anchor);
        std::fprintf(stderr,"[DRIVER TEST MAIN RETENTION WITNESS] stage=%s camera=%08X owned=%u receipt=%u phase=%u generation=%llu context=%08X\n",
            stage,anchor,unsigned(anchorOwned),unsigned(bool(currentBirth)),currentBirth?uint32_t(currentBirth->phase):~0u,
            currentBirth?static_cast<unsigned long long>(currentBirth->generation):0ull,currentBirth?currentBirth->context:0u);
        require(currentBirth&&currentBirth->camera==anchorBirth.camera&&currentBirth->phase==NativeCameraLifetimePhase::Constructed&&
                currentBirth->generation==anchorBirth.generation&&currentBirth->context==anchorBirth.context,
                "Main retention witness lost its exact genuine captured birth/context");
        require(poolAnchored.sameShape(pool),"Main retention witness lost its qualified linked pool shape");
        const auto target=pool.slot(camera),actual=pool.slot(anchor);printTargetBitmap(stage,pool,target);
        require(target==targetAnchored,"Main retention target changed its exact original slot");
        require(actual==anchorSlot,"Main retention witness changed its exact full original slot");
        require(pool.occupied(actual[0],actual[1]),"Main retention witness lost its actual occupied bit");
        require((std::find(constructed.begin(),constructed.end(),anchor)!=constructed.end())==anchorOwned,
                "Main retention peer ownership differs from the actual constructed cleanup vector");
    };
    requireRetention(poolAnchored,"before-target-free");
    MainCameraPluginClosingCapture closing;
    {
        MainCameraPluginClosingObservation observation(base,camera,closing);
        require(cpu.invoke(0x823F1D48,camera)==1,"Actual generic main camera destruction failed");
        require(closing.observations==1&&closing.forwards==1&&closing.completions==1,
                "Actual first main plugin callback did not observe Closing and forward exactly once");
    }
    require(!mainCameraPluginClosingCapture&&!originalMainCameraPluginDestroyBody&&
            PPC_LOOKUP_FUNC(base,closing.target.callback)!=observeMainCameraPluginClosing,
            "Main plugin Closing observation did not restore its original indirect dispatch");
    const auto freed=driver.observedCameraLifetime(camera);
    require(freed&&freed->phase==NativeCameraLifetimePhase::Freed&&freed->generation==birth.generation&&
            freed->context==birth.context,"Actual generic main free did not revoke its genuine original construction");
    rejectsMainCameraLifetime([&]{(void)driver.cameraBinding();});
    rejectsMainCameraLifetime([&]{driver.setUiDrawing(true);});
    unchanged(); // No freed camera/raster-field read is used to reject this interval.
    const auto poolAfterFree=captureCameraPool(runtime,base);requireRetention(poolAfterFree,"after-target-free");
    const auto freedSlot=poolAfterFree.slot(camera);
    require(freedSlot==targetAnchored,"Actual main free changed its original target slot");
    require(!poolAfterFree.occupied(freedSlot[0],freedSlot[1]),"Actual main free did not clear its target occupied bit");
    const auto reuseCandidates=poolAfterFree.freeSlotsThrough(camera);uint32_t reused=0;
    for(size_t attempt=0;attempt<reuseCandidates.size()&&!reused;++attempt) {
        const uint32_t next=cpu.invoke(0x823F1DB0);
        require(next!=0,"Original main reuse constructor failed within its derived finite bound");constructed.push_back(next);
        require(next==reuseCandidates[attempt],"Original main reuse constructor changed its qualified pool scan order");
        const auto nextBirth=driver.observedCameraLifetime(next);
        require(nextBirth&&nextBirth->phase==NativeCameraLifetimePhase::Constructed&&
                nextBirth->generation>birth.generation&&nextBirth->context==birth.context,
                "Main reuse candidate lacks its genuine new constructor/context receipt");
        const auto currentPool=captureCameraPool(runtime,base);requireRetention(currentPool,"reuse-constructor");
        const auto currentSlot=currentPool.slot(next);
        require(poolAfterFree.sameShape(currentPool)&&currentPool.occupied(currentSlot[0],currentSlot[1]),
                "Main reuse constructor changed the retained original pool/candidate ownership");
        if(next==camera)reused=next;
    }
    require(reused==camera,"Original main pool did not exercise actual same-address reuse within its qualified bound");
    const auto secondBirth=driver.observedCameraLifetime(camera);
    require(secondBirth&&secondBirth->phase==NativeCameraLifetimePhase::Constructed&&
            secondBirth->generation>birth.generation&&secondBirth->context==birth.context&&
            !PPC_LOAD_U32(camera+0x60)&&!PPC_LOAD_U32(camera+0x64),
            "Same-address main base constructor did not leave a new genuine lifetime with unassigned rasters");
    // Selection still carries the old generation. Do not assign rasters or select
    // the new object: the exact lifetime error must precede its zero C60/C64 reads.
    rejectsMainCameraLifetime([&]{(void)driver.cameraBinding();});
    rejectsMainCameraLifetime([&]{driver.setUiDrawing(true);});
    unchanged();
    // The remembered old main birth must reject a same-address new base object
    // BEFORE its zero C60/C64 reads, even after another legitimate selection.
    cpu.invoke(0x823EE6C8,loading);
    require(driver.cameraBinding().camera==loading,
            "Remembered stale main lifetime poisoned an unrelated valid loading binding");
    rejectsMainCameraLifetime([&]{cpu.invoke(0x823F1B80,camera,0,0);});
    rejectsMainCameraLifetime([&]{driver.preflightCameraPass(base,camera,true);});
    rejectsMainCameraLifetime([&]{driver.setUiDrawing(true);}); // Real main clear/post owner remains independently stale.
    require(driver.cameraBinding().camera==loading,
            "Remembered main rejection changed its still-live unrelated loading binding");
    unchanged();
    std::fprintf(stderr,"[DRIVER TEST MAIN NONMAIN STALE REUSE] main=%08X oldBirth=%llu newBirth=%llu loading=%08X rememberedClearRejected=1 unassignedRasters=1 unrelatedBindingLive=1\n",
        camera,static_cast<unsigned long long>(birth.generation),static_cast<unsigned long long>(secondBirth->generation),loading);
    for(auto object=constructed.rbegin();object!=constructed.rend();++object)
        require(cpu.invoke(0x823F1D48,*object)==1,"Main lifetime anchor/reuse camera release failed");
    require(driver.cameraClearCount()==expectedClears&&driver.frontCopyCount()==capture.copies&&
            driver.presentationAttemptCount()==capture.attempts,
            "Main lifetime fixture submitted a main presentation or advanced another clear");
    std::printf("PASS: actual AA11 LOC absent-bankspan main row-zero selector2 clear, real zero-geometry UI promotion, original first plugin Closing/free and same-address new-birth stale-selection rejection / %zu checks; no world, NGX evaluation, post phase or main presentation claim\n",checks);
}
void temporalLoadingUiLifetimeContracts(const char* image) {
    using namespace Simpsons;
    Runtime runtime;runtime.videoSettings.antialiasing=uint32_t(Graphics::Antialiasing::DLAA);
    runtime.load(image);PPCContext startup{};runtime.initialize(startup);
    const PPCContext entry=startup;auto* base=runtime.base;bool reached=false;
    {
        PresentObservation observation(base);
        try {runOriginal(startup,base);}catch(const FirstPresentObserved&) {reached=true;}
    }
    require(reached,"Hardware DLAA startup did not complete its actual loading UI/first present");
    auto& driver=*runtime.engineDriver;EngineCpuCalls cpu(entry,base);
    const uint32_t camera=PPC_LOAD_U32(0x82E07248);
    const auto birth=driver.observedCameraLifetime(camera);
    require(birth&&birth->camera==camera&&birth->generation&&
            birth->phase==NativeCameraLifetimePhase::Constructed&&birth->context==PPC_LOAD_U32(0x82D6D890),
            "Loading UI did not retain a genuine successful original constructor/context receipt");
    const auto binding=driver.cameraBinding();
    require(binding.camera==camera&&binding.colorIdentity==PPC_LOAD_U32(0x82D0CB00)&&
            binding.depthIdentity==PPC_LOAD_U32(0x82D0CAFC),"Actual first UI lost its original loading default pair");
    const auto origins=driver.temporalSceneOrigins();
    for(const auto& component:origins.components)
        require(component.kind==Graphics::TemporalRasterOriginKind::Grid&&component.jitter==std::array<float,2>{},
                "Actual loading UI/present did not publish the retained unjittered component origins");
    cameraClearContracts(runtime,cpu,base); // The original eight-selector/pixel/rejection oracle remains unchanged.
    const auto sourceColor=driver.readbackColor(binding.colorIdentity);
    const auto sourceDepth=driver.readbackDepth(binding.depthIdentity);
    const auto clearedOrigins=driver.temporalSceneOrigins();
    driver.setUiDrawing(false);driver.setUiDrawing(true);
    bool sampledAlphaOne=false;
    require(driver.color(binding.colorIdentity,sampledAlphaOne)!=clearedOrigins.color&&
            driver.depth(binding.depthIdentity),"Real loading UI did not route to its owned reconstructed-output bundle");
    require(driver.readbackColor(binding.colorIdentity)==sourceColor,
            "Zero-grid DLAA loading UI changed actual packed color pixels");
    const auto outputDepth=driver.readbackDepth(binding.depthIdentity);
    require(outputDepth.size()==sourceDepth.size(),"Loading UI changed the expected physical depth extent");
    for(size_t i=0;i<sourceDepth.size();i+=8)
        require(std::memcmp(sourceDepth.data()+i,outputDepth.data()+i,5)==0,
                "Loading UI changed defined D32/stencil bytes (undefined X24 excluded)");
    // Keep actual promoted Output active across every lifetime negative.
    // Repeated UI begins and direct present cannot bypass the loading receipt.
    const auto promoted=driver.color(binding.colorIdentity,sampledAlphaOne);
    PPCContext call=cpu.registers();call.r3.u32=binding.colorRaster;call.lr=0x8240806C;
    PPC_STORE_U32(0x82E07248,0);rejects([&]{driver.setUiDrawing(true);});PPC_STORE_U32(0x82E07248,camera);
    require(driver.color(binding.colorIdentity,sampledAlphaOne)==promoted,
            "A changed loading global bypassed or reset its captured promoted admission");
    std::vector<uint32_t> constructed;
    const auto poolBeforeAnchor=captureCameraPool(runtime,base);
    const auto targetBeforeAnchor=poolBeforeAnchor.slot(camera);
    require(poolBeforeAnchor.occupied(targetBeforeAnchor[0],targetBeforeAnchor[1]),
            "Genuine selected camera does not occupy its original pool bit");
    const uint32_t anchor=cpu.invoke(0x823F1DB0);
    require(anchor&&anchor!=camera,"Genuine anchor constructor did not produce a separate original camera");
    constructed.push_back(anchor);
    const auto anchorBirth=driver.observedCameraLifetime(anchor);
    require(anchorBirth&&anchorBirth->phase==NativeCameraLifetimePhase::Constructed&&
            anchorBirth->generation>birth->generation&&anchorBirth->context==birth->context,
            "Pool anchor lacks its genuine successful original constructor/context receipt");
    const auto poolAnchored=captureCameraPool(runtime,base);
    const auto targetAnchored=poolAnchored.slot(camera),anchorSlot=poolAnchored.slot(anchor);
    require(poolBeforeAnchor.sameShape(poolAnchored)&&targetAnchored==targetBeforeAnchor&&
            anchorSlot[0]==targetAnchored[0]&&anchorSlot[1]!=targetAnchored[1]&&
            poolAnchored.occupied(targetAnchored[0],targetAnchored[1])&&
            poolAnchored.occupied(anchorSlot[0],anchorSlot[1]),
            "Genuine anchor did not retain a different occupied slot in the exact target pool block");
    {
        CameraPluginClosingObservation observation(base,camera);
        require(cpu.invoke(0x823F1D48,camera)==1,"Actual generic camera destruction failed");
        require(observedCameraClosing,"Actual camera Closing boundary was not observed before plugin teardown");
    }
    const auto freed=driver.observedCameraLifetime(camera);
    require(freed&&freed->phase==NativeCameraLifetimePhase::Freed&&freed->generation==birth->generation,
            "Actual camera free completion did not revoke its original birth receipt");
    require(PPC_LOAD_U32(0x82E07248)==camera,"Fixture did not retain the original stale loading-global interval");
    rejects([&]{driver.setUiDrawing(true);});
    rejects([&]{driver.present(call,base,binding.colorRaster);});
    require(driver.color(binding.colorIdentity,sampledAlphaOne)==promoted,
            "Lifetime rejection reset the promoted route instead of checking its admission");
    // The live anchor prevents empty-block retirement. Derive the exact finite
    // constructor bound from valid free slots before target in original scan order.
    const auto poolAfterFree=captureCameraPool(runtime,base);
    const auto freedSlot=poolAfterFree.slot(camera),retainedAnchor=poolAfterFree.slot(anchor);
    require(poolAnchored.sameShape(poolAfterFree)&&freedSlot==targetAnchored&&retainedAnchor==anchorSlot&&
            !poolAfterFree.occupied(freedSlot[0],freedSlot[1])&&
            poolAfterFree.occupied(retainedAnchor[0],retainedAnchor[1]),
            "Actual target free did not retain the linked anchor block and clear only its target slot");
    const auto reuseCandidates=poolAfterFree.freeSlotsThrough(camera);uint32_t reused=0;
    for(size_t attempt=0;attempt<reuseCandidates.size()&&!reused;++attempt) {
        const uint32_t next=cpu.invoke(0x823F1DB0);
        require(next!=0,"Real camera constructor failed during the bounded reuse fixture");
        constructed.push_back(next);
        require(next==reuseCandidates[attempt],"Real camera constructor deviated from the qualified original pool scan order");
        const auto currentPool=captureCameraPool(runtime,base);
        const auto currentSlot=currentPool.slot(next);
        require(poolAfterFree.sameShape(currentPool)&&currentPool.occupied(currentSlot[0],currentSlot[1])&&
                currentPool.occupied(anchorSlot[0],anchorSlot[1]),
                "Real reuse constructor changed the retained original pool/anchor ownership");
        if(next==camera)reused=next;
    }
    require(reused==camera,"Real camera allocator did not exercise same-address reuse within the fixed bound");
    const auto secondBirth=driver.observedCameraLifetime(camera);
    require(secondBirth&&secondBirth->phase==NativeCameraLifetimePhase::Constructed&&
            secondBirth->generation>birth->generation&&secondBirth->context==birth->context,
            "Same-address constructor did not publish a new genuine object lifetime");
    // Restore only guest association words to the still-live original type2/1
    // rasters. This negative fixture deliberately makes every other owner check
    // match, so a current-Live map lookup alone cannot accidentally pass.
    PPC_STORE_U32(reused+0x60,binding.colorRaster);PPC_STORE_U32(reused+0x64,binding.depthRaster);
    rejects([&]{driver.setUiDrawing(true);});
    rejects([&]{driver.present(call,base,binding.colorRaster);});
    require(driver.color(binding.colorIdentity,sampledAlphaOne)==promoted,
            "Same-address rejection changed the already-promoted Output route");
    // A real new selection/clear may capture the new generation and same live pair.
    cpu.invoke(0x823EE6C8,reused);
    driver.setUiDrawing(true);
    require(driver.observedCameraLifetime(camera)->generation==secondBirth->generation,
            "Legitimate re-selection lost its observed constructor generation");
    for(uint32_t object:constructed)require(cpu.invoke(0x823F1D48,object)==1,"Reuse fixture camera release failed");
    std::printf("PASS: actual hardware DLAA loading UI/real first present, typed constructor Closing/free, stale-global and same-address selection-generation rejection / %zu checks; no temporal world or NGX evaluation claim\n",checks);
}
void cameraPassContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    auto& driver=*runtime.engineDriver;
    const uint32_t c=PPC_LOAD_U32(0x82E07248),engine=PPC_LOAD_U32(0x82D0CA68),r=PPC_LOAD_U32(c+0x60);
    require(!PPC_LOAD_U32(engine) && !PPC_LOAD_U32(0x82E3DD60) && !PPC_LOAD_U32(0x82D0CB1C),
            "Actual original camera end did not clear CPU ownership before presentation");
    require(cpu.invoke(0x823F1A18,c)==c,"Original camera begin wrapper failed after observed end");
    require(PPC_LOAD_U32(engine)==c && PPC_LOAD_U32(0x82E3DD60)==c && PPC_LOAD_U32(0x82D0CB1C)==1,
            "Actual original camera begin did not publish its CPU ownership");
    for(uint32_t index:{1u,2u})
        require(PPC_LOAD_U32(0x82D0CB40+4*index)!=0,"Original camera matrix-pool allocation was omitted");
    rejects([&]{cpu.invoke(0x82407DC0,r);});
    rejects([&]{cpu.invoke(0x823F62A0,0,r,0);});
    require(driver.rasterCount()==4,"Rejected active-camera destruction changed native ownership");
    const auto clears=driver.cameraClearCount();
    require(cpu.invoke(0x823F1A08,c)==c,"Original camera end wrapper returned the wrong camera");
    require(!PPC_LOAD_U32(engine) && !PPC_LOAD_U32(0x82E3DD60) && !PPC_LOAD_U32(0x82D0CB1C),
            "Original camera end failed to clear its CPU current-camera fields");
    require(driver.cameraBinding().camera==c,"Original camera end unexpectedly unselected its target cache");
    require(cpu.invoke(0x823F1A18,c)==c,"Original camera begin wrapper failed on reuse");
    require(PPC_LOAD_U32(engine)==c && PPC_LOAD_U32(0x82E3DD60)==c && PPC_LOAD_U32(0x82D0CB1C)==1,
            "Original camera begin did not restore its CPU ownership");
    // Compare immediately after begin: later original rendering preparation
    // can change the source matrices independently of the cached pool copies.
    for(auto [index,source]:std::array<std::pair<uint32_t,uint32_t>,2>{{{1,0x82D0CA70},{2,0x82CD1AB0}}}) {
        const uint32_t matrix=PPC_LOAD_U32(0x82D0CB40+4*index);
        require(std::memcmp(runtime.pointer(matrix,64,false),runtime.pointer(source,64,false),64)==0,
                "Original camera begin lost its view/projection matrix copy");
    }
    require(cpu.invoke(0x823F1A08,c)==c && driver.cameraClearCount()==clears,"Camera begin/end incorrectly performed a clear");
}
void pipelineResetContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    auto& driver=*runtime.engineDriver;
    auto* cache=runtime.pointer(0x82D5DB78+0x694,0x694,false);
    const std::vector<uint8_t> appBefore(cache,cache+0x694);
    auto* rw=runtime.pointer(0x82D0D170,0x2FBC,false);
    const std::vector<uint8_t> rwBefore(rw,rw+0x2FBC);
    const auto sp=cpu.registers().r1.u32;
    cpu.registers().r29.u64=0x8877665544332211ull;cpu.registers().lr=0x12345678;
    cpu.invoke(0x823F4618);
    for(uint32_t i=0;i<67;++i)
        require(driver.effectiveState().scalar(PPC_LOAD_U32(0x82CD1B70+8*i))==PPC_LOAD_U32(0x82CD1B74+8*i),
                "Original pipeline reset loop did not apply its ordered native scalar values");
    require(cpu.registers().r1.u32==sp && cpu.registers().lr==0x12345678 && cpu.registers().r29.u64==0x8877665544332211ull,
            "Native pipeline reset changed saved-register/stack ABI");
    require(std::vector<uint8_t>(cache,cache+0x694)==appBefore && std::vector<uint8_t>(rw,rw+0x2FBC)==rwBefore,
            "Pipeline direct reset incorrectly rewrote application/RenderWare CPU caches");
    // Original wrapper pushes a second identical mode and restores half-pixel
    // mode even when its unchanged-mode path elides the 67-state reset.
    const uint32_t depth=PPC_LOAD_U32(0x82D0CFFC),record=cpu.registers().r1.u32+0x60;
    require(depth==0,"Actual rendering pipeline mode stack has an unexpected depth");
    const uint32_t mode=cpu.invoke(0x823F4670);
    require(mode==0,"Actual rendering preparation did not return to mode zero");
    driver.directScalar(base,0x38,0);
    require(cpu.invoke(0x826B09A0,record,mode)==record && PPC_LOAD_U32(record)==depth+1 && PPC_LOAD_U32(0x82D0CFFC)==depth+1,
            "Original pipeline scope push did not retain its CPU depth record");
    require(driver.effectiveState().scalar(0x38)==0 && driver.effectiveState().scalar(0x144)==1,
            "Unchanged pipeline mode reset state or lost its direct pixel-center update");
    cpu.invoke(0x826B09F0,record);
    require(PPC_LOAD_U32(0x82D0CFFC)==depth && driver.effectiveState().scalar(0x144)==1,
            "Original pipeline scope pop lost its CPU depth or pixel-center policy");
    driver.directScalar(base,0x38,6);
}
void loadingContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    using namespace Simpsons;
    auto& driver=*runtime.engineDriver;
    const uint32_t dictionary=PPC_LOAD_U32(0x82E071EC),extension=PPC_LOAD_U32(0x82E3DC94),plugin=PPC_LOAD_U32(0x82CF0600);
    require(dictionary && driver.rasterCount()==4,"Original loading dictionary did not acquire two native textures alongside the camera");
    std::array<std::shared_ptr<Graphics::Texture>,2> held;
    std::array<std::weak_ptr<Graphics::Texture>,2> weak;
    std::array<uint32_t,2> rasters{},identities{};
    size_t index=0;
    for(const auto [field,position]:std::array<std::pair<uint32_t,uint32_t>,2>{{{0x82E071E8,0x28},{0x82E071E4,0x100BC}}}) {
        const uint32_t texture=PPC_LOAD_U32(field),raster=PPC_LOAD_U32(texture),x=raster+extension;
        rasters[index]=raster;identities[index]=PPC_LOAD_U32(x);
        require(texture && raster && PPC_LOAD_U32(texture+4)==dictionary && PPC_LOAD_U32(texture+0x54)==1,
            "Original loading texture reference/dictionary ownership mismatch");
        const auto decoded=Graphics::decodeNativeTextureStruct({runtime.pointer(0x8215F820+position+12,0x1005C,false),0x1005C});
        require(driver.readbackTextureRaster(raster)==decoded.blocks,"Actual uploaded loading BC3 bytes differ from original serialized source");
        require(std::string(reinterpret_cast<const char*>(runtime.pointer(texture+0x10,32,false)))==decoded.name &&
            PPC_LOAD_U32(texture+0x50)==0x1102,"Original loading texture name/sampler mismatch");
        require(cpu.invoke(0x823FE0A8,dictionary,texture+0x10)==texture && PPC_LOAD_U32(texture+0x54)==1,
            "Original dictionary lookup failed or incorrectly added a reference");
        require(!std::memcmp(runtime.pointer(texture+plugin,8,false),runtime.pointer(0x8215F820+position+12+0x1005C+24,8,false),8),
            "Original EA2F extension callback did not retain its raw two-word payload");
        require(identities[index] && !runtime.pageAccess[identities[index]>>12].load(),"Native texture identity is usable as an SDK pointer");
        require(PPC_LOAD_U32(x+8)==0x01000100 && PPC_LOAD_U32(raster+0x18)==0 && PPC_LOAD_U32(raster+0x28)==256 &&
            PPC_LOAD_U32(raster+0x2C)==256 && !PPC_LOAD_U8(raster+0x22),"Loading texture post-unlock metadata was omitted");
        held[index]=driver.textureRaster(raster);weak[index]=held[index];
        for(uint32_t entry:{0x82408208u,0x82407BA8u}) {
            const auto total=PPC_LOAD_U32(0x82CD1E28);
            const auto* bytes=runtime.pointer(raster,total,false);
            const std::vector<uint8_t> before(bytes,bytes+total);
            bool guarded=false;
            // Mode5 is the admitted movie access mode. Immutable loading
            // textures must still fail the owner check before the SDK callback.
            try {cpu.invoke(entry,raster,0,5);} catch(const Failure& error) {
                guarded=std::string(error.what()).ends_with(entry==0x82408208u?
                    "raster lock is not an owned movie plane":"raster unlock is not an owned movie plane");
            }
            require(guarded && cpu.registers().lastFunction==entry,"Native immutable texture lock/unlock reached an SDK consumer");
            require(std::vector<uint8_t>(bytes,bytes+total)==before && driver.textureRaster(raster)==held[index],
                "Rejected immutable texture access changed raster metadata or native ownership");
        }
        ++index;
    }
    require(identities[0]!=identities[1],"Loading textures share a native identity");
    // Retain native references across genuine original dictionary/texture/raster
    // destruction. The original borrowed globals must still be cleared.
    cpu.invoke(0x82862B18);
    require(!PPC_LOAD_U32(0x82E071EC) && !PPC_LOAD_U32(0x82E071E4) && !PPC_LOAD_U32(0x82E071E8) && driver.rasterCount()==2,
        "Original loading dictionary teardown retained guest texture ownership");
    for(size_t i=0;i<held.size();++i) {
        rejects([&]{driver.textureRaster(rasters[i]);});
        require(!weak[i].expired(),"Original texture teardown invalidated separately owned native work");
        held[i].reset();require(weak[i].expired(),"Native loading texture resource leaked after its last owner released it");
    }
    // Malformed second-texture data and enclosing lengths fail before original
    // allocation/publication. Open/close a real original memory stream and alter
    // only this fixture's cursor/image byte for fault injection.
    const uint32_t params=cpu.registers().r1.u32+0x80,output=cpu.registers().r1.u32+0x60;
    PPC_STORE_U32(params,0x8215F820);PPC_STORE_U32(params+4,0x20150);
    const uint32_t stream=cpu.invoke(0x823F9598,3,1,params);
    require(stream!=0,"Original memory stream fixture allocation failed");
    PPC_STORE_U32(stream+0xC,0x100BC);PPC_STORE_U32(output,0xAABBCCDD);
    auto invoke=[&](uint32_t length) {cpu.registers().lr=0x823FF8F4;cpu.invoke(0x8240A278,stream,output,length);};
    rejects([&]{invoke(0x10087);});
    const uint32_t extensionByte=0x8215F820+0x100BC+12+0x1005C+24;
    const uint8_t original=PPC_LOAD_U8(extensionByte);PPC_STORE_U8(extensionByte,original^1);
    rejects([&]{invoke(0x10088);});PPC_STORE_U8(extensionByte,original);
    require(PPC_LOAD_U32(output)==0xAABBCCDD && PPC_LOAD_U32(stream+0xC)==0x100BC && driver.rasterCount()==2,
        "Malformed loading input changed cursor/output/native ownership before validation");
    {
        Graphics::NativeBackend foreign(true);
        cpu.registers().lr=0x823FF8F4;cpu.registers().r3.u32=stream;cpu.registers().r4.u32=output;cpu.registers().r5.u32=0x10088;
        bool rejected=false;
        try {readLoadingTexture(cpu.registers(),base,foreign,driver,stream,output,0x10088);}
        catch(const Graphics::Error& error) {rejected=std::string(error.what())=="Native resource belongs to another graphics device";}
        require(rejected,"Foreign-device loading upload did not reach the checked native attachment rejection");
        require(PPC_LOAD_U32(output)==0xAABBCCDD && PPC_LOAD_U32(stream+0xC)==0x100BC+12+0x1005C && driver.rasterCount()==2,
            "Failed native attachment did not preserve stream effects and roll back original raster ownership");
    }
    PPC_STORE_U32(stream+0xC,0x100BC);
    require((invoke(0x10088),cpu.registers().r3.u32)==1,"Direct original loading callback failed");
    const uint32_t detached=PPC_LOAD_U32(output),detachedRaster=PPC_LOAD_U32(detached);
    require(PPC_LOAD_U32(detached+4)==0 && PPC_LOAD_U32(detached+0x54)==1 && PPC_LOAD_U32(detached+plugin+4)==detached &&
        PPC_LOAD_U32(stream+0xC)==0x100BC+12+0x1005C,"Native callback consumed an extension or transferred dictionary ownership too early");
    require(PPC_LOAD_U32(detachedRaster+extension)>identities[1],"Released native texture identity was reused");
    require(cpu.invoke(0x823FDEC8,detached)==1 && driver.rasterCount()==2,"Original detached texture final release failed");
    require(cpu.invoke(0x823F94A0,stream,0)==1,"Original memory stream fixture close failed");
}
void rasterContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    using namespace Simpsons;
    auto& driver=*runtime.engineDriver;
    const uint32_t camera=PPC_LOAD_U32(0x82E07248),offset=PPC_LOAD_U32(0x82E3DC94);
    require(camera && driver.rasterCount()==2,"Original camera did not retain its two native raster associations");
    const uint32_t color=PPC_LOAD_U32(camera+0x60),depth=PPC_LOAD_U32(camera+0x64);
    require(color && depth && color!=depth,"Original camera raster fields were not published");
    require(PPC_LOAD_U32(color+offset)==0 && PPC_LOAD_U32(depth+offset)==PPC_LOAD_U32(roles[1]),"Original shared-raster target representation changed");
    require(PPC_LOAD_U8(color+0x20)==2 && PPC_LOAD_U8(depth+0x20)==1 && !PPC_LOAD_U32(0x82CD1D88),"Actual camera creation did not consume the original shared-depth flag");
    bool retained=false;
    try {driver.stop(cpu.registers(),base);} catch(const Failure& error) {
        retained=std::string(error.what())=="Native driver still owns live camera raster associations";
    }
    require(retained && driver.rasterCount()==2 && driver.started(),"Premature driver stop failed to protect live camera rasters");
    // Exercise the real frame/camera/raster destructor path for this isolated
    // fixture. This is not a claim of complete application shutdown.
    cpu.invoke(0x82714220,camera);PPC_STORE_U32(0x82E07248,0);
    require(driver.rasterCount()==0 && !PPC_LOAD_U32(0x82D0D01C),"Original camera destruction retained its raster list nodes");
    require(!PPC_LOAD_U32(0x82CD1D88) && bool(driver.depth(PPC_LOAD_U32(roles[1]))),"Shared depth destruction rearmed allocation or released driver backing");
    rejects([&]{cpu.invoke(0x823F62A0,0,color,0);});

    runtime.map(0x10000,0x10000,true,"bounded camera raster contract fixture");
    const uint32_t r=0x10000,x=r+offset,total=PPC_LOAD_U32(0x82CD1E28);
    auto prepare=[&] {
        memset(runtime.pointer(r,total,true),0xA5,total);
        PPC_STORE_U32(r,r);PPC_STORE_U32(r+0xC,1280);PPC_STORE_U32(r+0x10,720);PPC_STORE_U32(r+0x14,0);
        PPC_STORE_U16(r+0x1C,0);PPC_STORE_U16(r+0x1E,0);PPC_STORE_U8(r+0x21,0);PPC_STORE_U8(r+0x22,0);
    };
    auto bytes=[&] {auto* p=runtime.pointer(r,total,false);return std::vector<uint8_t>(p,p+total);};
    auto untouchedExtension=[&] {
        require(PPC_LOAD_U32(x+0x10)==0xA5A5A5A5 && PPC_LOAD_U32(x+0x14)==0xA5A5A5A5 && PPC_LOAD_U32(x+0x1C)==0xA5A5A5A5,
            "Native raster creation invented writes to untouched extension fields");
        require(PPC_LOAD_U32(r+0x24)==0xA5A5A5A5 && PPC_LOAD_U32(r+0x30)==0xA5A5A5A5 && PPC_LOAD_U8(r+0x22)==0,
            "Native raster creation overwrote untouched wrapper fields");
    };
    prepare();auto before=bytes();
    const uint32_t registryHead=PPC_LOAD_U32(0x82CD1E38),registryTail=PPC_LOAD_U32(0x82CD1E3C);
    for(uint32_t field:{registryHead+0x34,registryHead+0x38,0x82CD1E3Cu}) {
        const uint32_t old=PPC_LOAD_U32(field);PPC_STORE_U32(field,old^4);
        rejects([&]{cpu.invoke(0x823F7070,0,r,2);});PPC_STORE_U32(field,old);
        require(bytes()==before && driver.rasterCount()==0,"Malformed plugin chain changed raster ownership");
    }
    for(uint32_t flags:{0u,0x82u,0x384u}) {
        rejects([&]{cpu.invoke(0x823F7070,0,r,flags);});
        require(bytes()==before && driver.rasterCount()==0 && !PPC_LOAD_U32(0x82D0D01C),"Unsupported raster changed original metadata/list ownership");
    }
    // The viewport family now qualifies1280x720 and640x720 private surfaces.
    // Keep negative coverage on nearby, transposed and other unverified sizes;
    // both color/depth must reject before changing metadata or list ownership.
    for(const auto size:std::array<std::array<uint32_t,2>,8>{{{1280,719},{640,719},{720,1280},{16,720},{256,720},{1024,720},{1024,512},{1920,1080}}}) {
        PPC_STORE_U32(r+0xC,size[0]);PPC_STORE_U32(r+0x10,size[1]);const auto unsupported=bytes();
        for(uint32_t flags:{1u,5u}) {
            rejects([&]{cpu.invoke(0x823F7070,0,r,flags);});
            require(bytes()==unsupported && driver.rasterCount()==0 && !PPC_LOAD_U32(0x82D0D01C),
                "Unsupported private dimensions changed metadata/list ownership");
        }
    }
    prepare();require(bytes()==before,"Private dimension rejection changed the original fixture baseline");
    // Failure after CPU format normalization must restore precisely this
    // transaction's raster writes while leaving unrelated original CPU effects.
    const uint32_t engine=PPC_LOAD_U32(0x82D0CA68),allocate=PPC_LOAD_U32(engine+0x138);
    PPC_STORE_U32(engine+0x138,0x12345678);
    rejects([&]{cpu.invoke(0x823F7070,0,r,2);});PPC_STORE_U32(engine+0x138,allocate);
    require(bytes()==before && driver.rasterCount()==0 && !PPC_LOAD_U32(0x82D0D01C),"Failed original list allocation did not roll back the raster transaction");
    require(PPC_LOAD_U32(0x82D0D000)==0x01200B00,"Rollback erased the original format helper scratch effect");
    require(cpu.invoke(0x823F7070,0,r,2)==1,"Native root camera raster create failed");untouchedExtension();
    require(PPC_LOAD_U32(r+0x18)==0 && PPC_LOAD_U32(r+0x28)==1280 && PPC_LOAD_U32(r+0x2C)==720,"Original type-2 extent metadata missing");
    rejects([&]{cpu.invoke(0x823F7070,0,r,2);});
    const uint32_t node=PPC_LOAD_U32(0x82D0D01C);
    const uint32_t oldNext=PPC_LOAD_U32(node+4),extra=0x11000;
    const auto ownedRaster=bytes();
    for(uint32_t shape=0;shape<3;++shape) {
        PPC_STORE_U32(extra,shape==2?r:r+0x100);
        PPC_STORE_U32(extra+4,shape==1?extra:(shape==2?0:node));
        PPC_STORE_U32(node+4,extra);
        rejects([&]{cpu.invoke(0x823F62A0,0,r,0);});
        require(bytes()==ownedRaster && driver.rasterCount()==1 && PPC_LOAD_U32(0x82D0D01C)==node,
                "Cyclic/duplicate raster chain changed its live owner before rejection");
        PPC_STORE_U32(node+4,oldNext);
    }
    for(uint32_t stage=0;stage<8;++stage) {
        const uint32_t field=0x82D0E3F8+stage*0x18,old=PPC_LOAD_U32(field);PPC_STORE_U32(field,r);
        rejects([&]{cpu.invoke(0x82407DC0,r);});PPC_STORE_U32(field,old);
        require(cpu.registers().lastFunction==0x82407DC0,"Bound raster destruction entered original plugin callbacks before validation");
        require(PPC_LOAD_U32(0x82D0D01C)==node && driver.rasterCount()==1,"Rejected stage-unbind destruction lost raster ownership");
    }
    PPC_STORE_U32(r,r+0x100);rejects([&]{cpu.invoke(0x823F62A0,0,r,0);});PPC_STORE_U32(r,r);
    require(cpu.invoke(0x823F62A0,0,r,0)==1 && driver.rasterCount()==0,"Native camera association release failed");
    prepare();PPC_STORE_U32(0x82CD1D88,1); // Fixture-only first-depth initial condition.
    require(cpu.invoke(0x823F7070,0,r,1)==1,"Native shared depth raster create failed");untouchedExtension();
    for(uint32_t field:{0x18u,0x28u,0x2Cu}) require(PPC_LOAD_U32(r+field)==0xA5A5A5A5,"Shared depth invented color-only raster writes");
    require(cpu.invoke(0x823F62A0,0,r,0)==1 && !PPC_LOAD_U32(0x82CD1D88),"Shared-depth destruction changed original one-time allocation policy");
    // Also retain the real wrapper allocator/plugin constructor/destructor pair
    // for another color association; no fixture-written metadata on this path.
    const uint32_t wrapped=cpu.invoke(0x82408130,1280,720,0,2);
    require(wrapped && driver.rasterCount()==1,"Original raster wrapper did not acquire a native association");
    require(cpu.invoke(0x82407DC0,wrapped)==1 && driver.rasterCount()==0 && !PPC_LOAD_U32(0x82D0D01C),"Original raster wrapper did not release its association/node");
    // A consistent alternate plugin location must work: 34 is an observation,
    // not the engine extension ABI. Only this fixture's registry is changed.
    uint32_t plugin=registryHead;
    while(PPC_LOAD_U32(plugin+8)!=0x40C) plugin=PPC_LOAD_U32(plugin+0x30);
    const uint32_t movedOffset=offset+0x20,movedTotal=total+0x20;
    PPC_STORE_U32(plugin,movedOffset);PPC_STORE_U32(0x82E3DC94,movedOffset);PPC_STORE_U32(0x82CD1E28,movedTotal);
    memset(runtime.pointer(r,movedTotal,true),0xA5,movedTotal);prepare();
    require(cpu.invoke(0x823F7070,0,r,2)==1 && PPC_LOAD_U32(r+movedOffset)==0 &&
        PPC_LOAD_U32(r+movedOffset+0x18)==0x182801B6,"Native raster hardcoded the observed extension offset");
    require(cpu.invoke(0x823F62A0,0,r,0)==1,"Moved raster extension failed paired destruction");
    PPC_STORE_U32(plugin,offset);PPC_STORE_U32(0x82E3DC94,offset);PPC_STORE_U32(0x82CD1E28,total);
    require(PPC_LOAD_U32(0x82CD1E3C)==registryTail && !PPC_LOAD_U32(0x82D0D01C),"Raster fixture changed unrelated registry/list ownership");
}
}
#include "beam_lifetime_contracts.h"
int main(int argc,char** argv) {
    using namespace Simpsons;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(exceptionFilter);
    try {
        if(argc==3&&std::strcmp(argv[2],"--temporal-beam-lifetime")==0) {
            temporalBeamLifetimeContracts(argv[1]);return 0;
        }
        if(argc==3&&std::strcmp(argv[2],"--temporal-loading")==0) {
            temporalLoadingCameraContracts(argv[1]);return 0;
        }
        if(argc==3&&std::strcmp(argv[2],"--temporal-loading-ui")==0) {
            temporalLoadingUiLifetimeContracts(argv[1]);return 0;
        }
        if(argc==6&&std::strcmp(argv[2],"--temporal-main-camera-owner")==0) {
            temporalMainCameraOwnerContracts(argv[1],argv[3],argv[4],argv[5]);return 0;
        }
        if(argc==6&&std::strcmp(argv[2],"--temporal-main-camera-lifetime")==0) {
            temporalMainCameraLifetimeContracts(argv[1],argv[3],argv[4],argv[5]);return 0;
        }
        if(argc!=2) throw Failure("Original flat image path required");
        overlayExpandedNoRuntimeContracts();
        Runtime runtime;runtime.load(argv[1]);PPCContext original{};runtime.initialize(original);
        const PPCContext entry=original;auto* base=runtime.base;
        bool reached=false;
        {
            PresentObservation observation(base);
            try {runOriginal(original,base);} catch(const FirstPresentObserved&) {reached=true;}
        }
        require(reached,"Original startup did not reach the bounded post-start observation point");
        const uint32_t integrationContext=PPC_LOAD_U32(0x82D5DA74);
        runtime.engineDriver->requireContext(integrationContext);
        require(integrationContext && !runtime.pageAccess[integrationContext>>12].load(),"Original setup did not retain the opaque native context");
        require(PPC_LOAD_U32(0x82D6D890)==integrationContext,"Original pure-store integration helper did not publish its native identity");
        const uint32_t app=0x82D5DB78;
        require(PPC_LOAD_U32(0x82D6D7E8)==82 && PPC_LOAD_U32(0x82D6D7EC)==20,"Actual startup did not register all application states");
        for(uint32_t index=0;index<82;++index) {
            const uint32_t selector=PPC_LOAD_U32(0x82D6D6A0+4*index),id=PPC_LOAD_U32(0x82150580+4*selector);
            uint32_t expected=PPC_LOAD_U32(app+0x694+4*index);
            for(uint32_t i=0;i<67;++i) if(PPC_LOAD_U32(0x82CD1B70+8*i)==id) expected=PPC_LOAD_U32(0x82CD1B74+8*i);
            if(id==0x144) expected=1; // Original pipeline scope's direct setter follows reset.
            constexpr std::array<uint32_t,18> rebuilt={0x30,0x2C,0x28,0x6C,0x74,0x78,0x7C,0x80,0x84,
                0x88,0x8C,0x48,0x4C,0x68,0x64,0x3C,0x60,0x38};
            if(std::find(rebuilt.begin(),rebuilt.end(),id)!=rebuilt.end()) expected=PPC_LOAD_U32(0x82E3D580+4*id);
            require(runtime.engineDriver->effectiveState().scalar(id)==expected,
                    "Actual pipeline state reset/default inheritance differs from native effective state");
        }
        for(uint32_t stage=0;stage<16;++stage) for(uint32_t index=0;index<20;++index) {
            const uint32_t selector=PPC_LOAD_U32(0x82D6D5F8+4*index),id=PPC_LOAD_U32(0x821506E0+4*selector);
            uint32_t expected=PPC_LOAD_U32(app+0x7DC+0x50*stage+4*index);
            constexpr std::array<uint32_t,7> rebuilt={0x14,0x10,0x18,0,4,0xC,0x24};
            if(stage<8 && std::find(rebuilt.begin(),rebuilt.end(),id)!=rebuilt.end()) expected=PPC_LOAD_U32(0x82D0D170+320*stage+4*id);
            require(runtime.engineDriver->effectiveState().sampler(stage,id)==expected,
                    "Actual startup sampler pass did not synchronize native and application state");
        }
        require(runtime.engineDriver->submissionConfigured() && runtime.graphicsStorage.size()==1,"Actual original setup did not register native submission");
        require(runtime.engineDriver->cameraClearCount()==1,"Actual startup did not execute exactly its original first camera clear");
        const uint32_t reservation0=PPC_LOAD_U32(0x82E0759C),reservation1=PPC_LOAD_U32(0x82E075A0);
        require(reservation0 && reservation1,"Original post-start CPU allocations did not execute");
        const auto& retained=runtime.graphicsStorage.front();
        require(retained.context==integrationContext && retained.address[0]==reservation0 && retained.address[1]==reservation1 &&
                retained.size[0]==0x20000 && retained.size[1]==0x600000,"Native registration retained the wrong original allocations");
        require(PPC_LOAD_U32(0x82D576A0)!=0,"Original application CPU object initialization was skipped after native integration");
        EngineCpuCalls cpu(entry,base);
        applicationStateContracts(runtime,cpu,base);
        applicationSamplerContracts(runtime,cpu,base);
        rwFogContracts(runtime,cpu,base);
        rwDepthWriteContracts(runtime,cpu,base);
        rwFrontendContracts(runtime,cpu,base);
        rwOverlayStateContracts(runtime,cpu,base);
        rwSamplerContracts(runtime,cpu,base);
        rwRebuildContracts(runtime,cpu,base);
        bindingResetContracts(runtime,cpu,base);
        nullRasterContracts(runtime,cpu,base);
        cameraClearContracts(runtime,cpu,base);
        cameraPassContracts(runtime,cpu,base);
        im2dUploadContracts(runtime,cpu,base);
        im2dShaderCpuContracts(runtime,cpu,base);
        im2dQuadContracts(runtime,cpu,base);
        im2dDrawContracts(runtime,cpu,base);
        overlayExpandedContracts(runtime,cpu,base);
        const auto presentationCheckpoint=presentContracts(runtime,cpu,base);
        pipelineResetContracts(runtime,cpu,base);
        loadingContracts(runtime,cpu,base);
        rasterContracts(runtime,cpu,base);
        recordingOwnerContracts(runtime,cpu,base);
        for(uint32_t callbackEntry:{0x82461500u,0x82460D38u,0x82460DC0u,0x82454BE8u,0x82454BF0u,
            0x82740680u,
            0x826F3690u,0x826F2F40u,0x826F2FA0u,0x826F2FC8u,0x826F2E40u,0x8273FB38u,
            0x826F3560u,0x826F3628u,0x826F37B8u,0x826F37D0u,0x826F3808u,
            0x82459FB0u,0x8245A368u,0x8245C5F8u,0x8245A7E8u,0x8245C7A0u}) {
            char expected[96];
            std::snprintf(expected,sizeof(expected),"Unimplemented native engine graphics boundary 0x%08X,",callbackEntry);
            bool guarded=false;
            try {cpu.invoke(callbackEntry,0x22,0x12345678);}
            catch(const Failure& error) {guarded=std::string(error.what()).starts_with(expected);}
            require(guarded,"SDK instrumentation did not fail at its explicit native guard");
            require(cpu.registers().lastFunction==callbackEntry,"SDK instrumentation executed beyond its guarded entry");
            require(runtime.engineDriver->submissionConfigured() && runtime.graphicsStorage.size()==1,"Unsupported SDK instrumentation changed native submission ownership");
        }
        for(const auto& [callbackEntry,expected]:std::array<std::pair<uint32_t,const char*>,6>{{
            {0x82737400u,"Unqualified original cached record deletion entry"},
            {0x82740420u,"Unqualified original rigid recording-build entry"},
            {0x826F4D08u,"Unqualified original recording begin entry"},
            {0x826F39E0u,"Unqualified original rigid texture-transfer entry"},
            {0x826F4F58u,"Unqualified original recording finish entry"},
            {0x827402F0u,"Unqualified original recording replay entry"}}}) {
            bool guarded=false;
            try {cpu.invoke(callbackEntry,0x22,0x12345678);}
            catch(const Failure& error) {guarded=std::string(error.what())==expected;}
            require(guarded&&cpu.registers().lastFunction==callbackEntry,"Qualified recording entry accepted an unrelated caller");
            require(runtime.engineDriver->submissionConfigured()&&runtime.graphicsStorage.size()==1,
                    "Rejected recording entry changed native submission ownership");
        }
        {
            // These entry guards must run before the original constructor's
            // singleton/pool writes or the SDK creator's output clear. A real
            // writable owner/output fixture catches accidental fallthrough.
            const uint32_t memory=cpu.registers().r1.u32+0x20,output=memory+0x80;
            std::array<uint8_t,0x84> saved{};
            std::memcpy(saved.data(),runtime.pointer(memory,saved.size(),false),saved.size());
            std::memset(runtime.pointer(memory,saved.size(),true),0xA5,saved.size());
            const std::array<uint32_t,4> fields={0x82D09784,0x82DFE10C,0x82D6D890,0x82D0CAF8};
            std::array<uint32_t,4> values{};
            for(size_t i=0;i<fields.size();++i) values[i]=PPC_LOAD_U32(fields[i]);
            for(uint32_t guardEntry:{0x826F4988u,0x82452540u}) {
                cpu.registers().r8.u64=output;
                char expected[96];
                std::snprintf(expected,sizeof(expected),"Unimplemented native engine graphics boundary 0x%08X,",guardEntry);
                bool guarded=false;
                try {cpu.invoke(guardEntry,guardEntry==0x826F4988?memory:0,2,0,0,0);}
                catch(const Failure& error) {guarded=guardEntry==0x826F4988 || std::string(error.what()).starts_with(expected);}
                require(guarded && cpu.registers().lastFunction==guardEntry,"Recording creation escaped its pre-SDK entry guard");
                for(size_t i=0;i<saved.size();++i)
                    require(runtime.pointer(memory,saved.size(),false)[i]==0xA5,"Rejected recording creation changed owner/output memory");
                for(size_t i=0;i<fields.size();++i)
                    require(PPC_LOAD_U32(fields[i])==values[i],"Rejected recording creation published a singleton, pool or context");
                require(runtime.engineDriver->submissionConfigured() && runtime.graphicsStorage.size()==1,
                    "Rejected recording creation changed native submission ownership");
            }
            std::memcpy(runtime.pointer(memory,saved.size(),true),saved.data(),saved.size());
        }
        const uint32_t descriptor=cpu.registers().r1.u32+0x60;
        auto descriptorWords=std::array<uint32_t,6>{0,0x20000,reservation0,0x600000,reservation1,0};
        auto writeDescriptor=[&] {for(size_t i=0;i<descriptorWords.size();++i) PPC_STORE_U32(descriptor+uint32_t(i)*4,descriptorWords[i]);};
        writeDescriptor();
        rejects([&]{runtime.engineDriver->configureSubmission(base,integrationContext,descriptor);});
        require(runtime.graphicsStorage.size()==1,"Duplicate registration added another reservation record");
        fprintf(stderr,"[DRIVER TEST] allocator=%08X registered=%08X flags=%08X\n",PPC_LOAD_U32(0x82D57244),PPC_LOAD_U32(0x82DFD8C4),PPC_LOAD_U32(0x82D5726C));
        uint32_t oldColor{};
        uint32_t oldContext{};
        std::weak_ptr<Graphics::RenderTarget> releasedColor;
        for(unsigned cycle=0;cycle<2;++cycle) {
            require(bool(runtime.engineDriver) && runtime.engineDriver->started(),"Original startup has no persistent native owner");
            const uint32_t engine=PPC_LOAD_U32(0x82D0CA68);
            const uint32_t bindingPool=PPC_LOAD_U32(0x82D0CB3C);
            const uint32_t context=cpu.invoke(0x823EE8F8);
            runtime.engineDriver->requireContext(context);
            require(context>oldContext && !runtime.pageAccess[context>>12].load(),"Context identity was reused or overlaps guest memory");
            if(oldContext) rejects([&]{runtime.engineDriver->requireContext(oldContext);});
            oldContext=context;
            if(cycle==1) {
                presentRestartContracts(runtime,base,presentationCheckpoint);
                require(!runtime.engineDriver->submissionConfigured(),"A fresh driver inherited submission readiness");
                writeDescriptor();
                rejects([&]{runtime.engineDriver->configureSubmission(base,context+1,descriptor);});
                PPC_STORE_U32(descriptor+4,0x20004);
                rejects([&]{runtime.engineDriver->configureSubmission(base,context,descriptor);});
                writeDescriptor();
                // These process-lifetime reservations still belong to the first
                // integration receipt. A new driver cannot silently reuse them.
                rejects([&]{runtime.engineDriver->configureSubmission(base,context,descriptor);});
                require(!runtime.engineDriver->submissionConfigured() && runtime.graphicsStorage.size()==1,"Rejected registration changed native ownership");
            }
            require(PPC_LOAD_U32(0x82D0CB3C)==bindingPool,"Context cache reset replaced the original live pool");
            for(uint32_t field:{0x82D0CF58u,0x82D0CF5Cu,0x82D0CF60u,0x82D0CF64u,0x82D0CF68u})
                require(PPC_LOAD_U32(field)==0,"Original context binding cache reset missing");
            require(PPC_LOAD_U32(engine+0x144)==3,"Original plugin/gamma/start walk did not publish lifecycle 3");
            require(PPC_LOAD_U32(0x82D0CB08)==1 && !PPC_LOAD_U32(0x82D0CAF8),"Native started/console-device invariants failed");
            require(cpu.invoke(0x823F0630,8,0,0,0)==1,"Native device-exists request failed");
            require(cpu.invoke(0x823F0630,7,0,0,3)==0,"Mode selection incorrectly succeeded while started");
            require(PPC_LOAD_U32(0x82D10118)==0,"Unexpected pending texture-stage state");
            if(cycle==0) {
                // Rebuild has already committed the actual original camera
                // state. A later fixture begin can either match applied values
                // or queue changed near/far values; validate both memberships.
                const uint32_t n=PPC_LOAD_U32(0x82D10114);
                require(n<=425,"Original camera pending-state queue is out of bounds");
                std::array<bool,425> seen{};
                for(uint32_t i=0;i<n;++i) {
                    const uint32_t id=PPC_LOAD_U32(0x82D0ED08+4*i);
                    require(id<seen.size() && !seen[id] && PPC_LOAD_U32(0x82D0F3B0+8*id+4)==1,
                            "Original camera/RW dirty queue has invalid membership");
                    seen[id]=true;
                }
                for(uint32_t id:{0x199u,0x19Au})
                    require(seen[id] || PPC_LOAD_U32(0x82D0F3B0+8*id)==PPC_LOAD_U32(0x82E3D580+4*id),
                            "Original camera near/far value is neither committed nor queued");
            } else require(PPC_LOAD_U32(0x82D10114)==0,"Fresh driver startup left pending scalar states");
            require(PPC_LOAD_U32(0x82D507F0)!=0,"Original plugin pipeline index was not constructed");
            const uint32_t colorId=PPC_LOAD_U32(roles[0]);
            require(colorId>oldColor,"Target identity was reused across driver lifetimes");
            if(oldColor) {bool alpha{};rejects([&]{runtime.engineDriver->color(oldColor,alpha);});}
            oldColor=colorId;
            bool alpha=true;
            auto color=runtime.engineDriver->color(colorId,alpha);releasedColor=color;
            require(!alpha && color->format==Graphics::TargetFormat::RGB10A2,"Default target format/sampling role mismatch");
            require(color->width==PPC_LOAD_U32(0x82E3DF84) && color->height==PPC_LOAD_U32(0x82E3DF88),"Target extent mismatch");
            for(size_t i=0;i<roles.size();++i) {
                const uint32_t id=PPC_LOAD_U32(roles[i]);
                require(id && !runtime.pageAccess[id>>12].load(),"Published target is usable as an SDK guest pointer");
                for(size_t j=0;j<i;++j) require(id!=PPC_LOAD_U32(roles[j]),"Native target identities alias");
                if(i==1 || i==2) {
                    require(bool(runtime.engineDriver->depth(id)),"Missing real depth backing");
                    rejects([&]{runtime.engineDriver->color(id,alpha);});
                } else {
                    auto value=runtime.engineDriver->color(id,alpha);
                    require(alpha==(i==3 || i==4),"Front-color sampled-alpha policy mismatch");
                    rejects([&]{runtime.engineDriver->depth(id);});
                }
            }
            color.reset();
            bool foreignThreadRejected=false;
            std::thread worker([&]{try {runtime.engineDriver->started();} catch(const Failure&) {foreignThreadRejected=true;}});worker.join();
            require(foreignThreadRejected,"Driver accepted access from another thread");
            rejects([&]{runtime.engineDriver->stop(cpu.registers(),base);});
            require(runtime.engineDriver->started() && PPC_LOAD_U32(0x82D0CB08)==1,"Premature stop damaged live plugin ownership");
            require(cpu.invoke(0x823EC950)==1,"Original plugin destructors/native driver stop failed");
            require(!runtime.engineDriver && releasedColor.expired(),"Driver stop retained native backing");
            require(!PPC_LOAD_U32(0x82D5DA74) && !PPC_LOAD_U32(0x82D6D890),"Driver retirement left a stale native integration alias");
            require(runtime.graphicsStorage.size()==1 && runtime.queryPhysicalProtect(reservation0)==0x404 && runtime.queryPhysicalProtect(reservation1)==0x404,
                "Driver stop fabricated an unproved early free of caller-supplied submission reservations");
            require(PPC_LOAD_U32(engine+0x144)==2 && PPC_LOAD_U32(0x82D0CB08)==0,"Original stopped lifecycle mismatch");
            require(cpu.invoke(0x823F0630,8,0,0,0)==0,"Device-exists remained true after stop");
            for(uint32_t field:roles) require(PPC_LOAD_U32(field)==0,"Stop left a native target ID");
            for(uint32_t field:{0x82D0CB18u,0x82D0CB10u,0x82D0CB3Cu,0x82D0D020u,0x82D101D4u,0x82D101D8u,
                0x82D0D0C8u,0x82D0D0CCu,0x82D0D0D0u,0x82D0D0D8u,0x82D507F0u,0x82D507F4u,0x82D507F8u,0x82D507FCu})
                require(PPC_LOAD_U32(field)==0,"Original cleanup left a resource/pool/mode field");
            require(cpu.invoke(0x823ECAA8)==1,"Original close failed after native stop");
            require(PPC_LOAD_U32(PPC_LOAD_U32(0x82D0CA68)+0x144)==1 && !PPC_LOAD_U32(0x82D0CAF4),"Original close lifecycle mismatch");
            if(cycle==0) {
                // Original platform-open ignores this nonnull parameter record;
                // the higher-level open API requires the pointer itself.
                const uint32_t params=cpu.registers().r1.u32+0x60;PPC_STORE_U32(params,0);
                require(cpu.invoke(0x823ECF58,params)==1,"Original reopen failed");
                const uint32_t reopened=PPC_LOAD_U32(0x82D0CA68),modes=PPC_LOAD_U32(0x82D0CB18);
                const PPCContext retryContext=cpu.registers();
                // Force a checked failure after the real target publication,
                // original pools and original scratch owner have initialized.
                // Only this fixture's mapped image bytes are changed.
                const uint32_t constant=PPC_LOAD_U32(0x821DD0D8);
                PPC_STORE_U32(0x821DD0D8,1);
                rejects([&]{cpu.invoke(0x823EC9E0);});
                PPC_STORE_U32(0x821DD0D8,constant);cpu.registers()=retryContext;
                require(!runtime.engineDriver,"Failed start published a persistent driver");
                require(PPC_LOAD_U32(reopened+0x144)==2 && !PPC_LOAD_U32(0x82D0CB08),"Failed start advanced original readiness");
                require(PPC_LOAD_U32(0x82D0CB18)==modes && modes,"Partial-start rollback freed the original open-mode table");
                for(uint32_t field:roles) require(PPC_LOAD_U32(field)==0,"Partial-start rollback left a target ID");
                for(uint32_t field:{0x82D0CB3Cu,0x82D0D020u,0x82D101D4u,0x82D101D8u})
                    require(PPC_LOAD_U32(field)==0,"Partial-start rollback left an original pool/scratch resource");
                const uint32_t caps=PPC_LOAD_U32(0x82E3DFBC);
                PPC_STORE_U32(0x82E3DFBC,caps&~0x10000u);
                rejects([&]{cpu.invoke(0x823EC9E0);});
                PPC_STORE_U32(0x82E3DFBC,caps);cpu.registers()=retryContext;
                require(!runtime.engineDriver && !PPC_LOAD_U32(0x82D0CB08),"Unsupported capability created a driver owner");
                const uint32_t allocator=PPC_LOAD_U32(0x82D57244);
                PPC_STORE_U32(0x82D57244,0);
                rejects([&]{cpu.invoke(0x823EC9E0);});
                PPC_STORE_U32(0x82D57244,allocator);cpu.registers()=retryContext;
                require(!runtime.engineDriver && !PPC_LOAD_U32(0x82D0CB08),"Missing allocator initialization was silently skipped");
                require(cpu.invoke(0x823EC9E0)==1,"Original second plugin/start cycle failed");
            }
        }
        std::printf("PASS: actual original engine driver startup/stop/close/reopen / %zu checks; no frame claim\n",checks);
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL after %zu checks: %s\n",checks,error.what());return 1;}
}
