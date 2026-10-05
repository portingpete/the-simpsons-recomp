// Real original startup, original plugin destructors, stop/close and reopen.
// The first completed native presentation is the deliberate test observation.
// The application executable continues normally beyond it.
#include "runtime/runtime.h"
#include "runtime/engine_driver.h"
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
        require(cpu.invoke(0x823EE940,c,(selector&1)?color:0xFFFFFFFC,selector)==1,"Native original clear failed");
        require(driver.cameraClearCount()==count+1,"Original clear did not reach the native operation");
        checkPixels((selector&1)?0x000FFC00:0xC00003FF,(selector&4)?0xA5:0x5A);
    }
    PPC_STORE_U32(color,0xFF0000FF);PPC_STORE_U32(0x82D0CB14,0x5A);cpu.invoke(0x823EE940,c,color,7);
    const uint32_t depthRaster=PPC_LOAD_U32(c+0x64);
    PPC_STORE_U32(c+0x64,0);PPC_STORE_U32(0x82D0CB14,0xA5);
    cpu.invoke(0x823EE940,c,0xFFFFFFFC,6);
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
int main(int argc,char** argv) {
    using namespace Simpsons;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(exceptionFilter);
    try {
        if(argc!=2) throw Failure("Original flat image path required");
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
