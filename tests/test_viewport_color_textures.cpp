// Original constructors/destructors own all guest headers and physical memory.
// Only copy-call inputs are a bounded fixture; no game-rendering claim is made.
#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include "runtime/engine_viewport_surfaces.h"
#include "renderer/native_backend.h"
#include "renderer/engine_state.h"
#include <array>
#include <cstdio>
#include <thread>
#include <string_view>
#include <vector>

void SimpsonsRejectUnportedGraphics(PPCContext&,uint8_t*);

namespace {
using namespace Simpsons;
size_t checks{};
void need(bool value,const char* message){++checks;if(!value)throw Failure(message);}
template<class F>void rejects(F action){try{action();}catch(const std::exception&){++checks;return;}throw Failure("Invalid viewport color contract accepted");}
struct Observed{};
std::vector<uint32_t> effective(EngineDriver& driver) {
    std::vector<uint32_t> result;const auto& state=driver.effectiveState();
    for(const auto& row:Graphics::scalarStateEvidence())result.push_back(state.scalar(row.id));
    for(uint32_t stage=0;stage<8;++stage)for(const auto& row:Graphics::samplerStateEvidence())result.push_back(state.sampler(stage,row.id));
    return result;
}
void exercise(const char* image) {
    Runtime rt;rt.load(image);PPCContext startup{};rt.initialize(startup);const auto entry=startup;auto* base=rt.base;
    rt.audioBoundaryObserver=[](uint32_t pc,PPCContext&,uint8_t*){if(pc==0x828166FC)throw Observed{};};
    bool observed=false;try{runOriginal(startup,base);}catch(const Observed&){observed=true;}rt.audioBoundaryObserver={};
    need(observed,"Original startup checkpoint missing");auto& driver=*rt.engineDriver;auto& service=driver.viewportSurfaces();
    EngineCpuCalls cpu(entry,base);
    constexpr std::array<uint32_t,3> headers{0x82DFE360,0x82DFE534,0x82DFE5D0};
    constexpr std::array<uint32_t,3> slots{0,3,4};
    std::array<std::weak_ptr<Graphics::RenderTarget>,3> leases;
    for(size_t i=0;i<headers.size();++i) {
        const auto texture=service.colorTexture(headers[i]);leases[i]=texture;
        need(texture->width==(1280u>>i)&&texture->height==(720u>>i)&&texture->format==Graphics::TargetFormat::RGB10A2,
             "Original color texture dimensions/format differ");
        if(i)need(texture!=service.backing(PPC_LOAD_U32(0x82DFEF38+4*slots[i])),"Resolve texture aliases its auxiliary render attachment");
        for(size_t j=0;j<i;++j)need(texture!=leases[j].lock(),"Distinct resolve snapshots share native storage");
        rejects([&]{service.readbackColorTexture(headers[i]);});
        for(uint32_t word=0;word<13;++word) {
            const auto address=headers[i]+4*word,saved=PPC_LOAD_U32(address);PPC_STORE_U32(address,saved^1);
            rejects([&]{service.colorTexture(headers[i]);});PPC_STORE_U32(address,saved);
        }
        const auto publication=0x82DFEF10+4*slots[i],saved=PPC_LOAD_U32(publication);PPC_STORE_U32(publication,saved+52);
        rejects([&]{service.colorTexture(headers[i]);});PPC_STORE_U32(publication,saved);
    }
    for(const auto header:{0u,0x82DFE361u,0x82DFE394u,0x82DFE400u,0x82DFE49Cu,0x82DFE840u})rejects([&]{service.colorTexture(header);});
    // Slot1 is the full-size history original Blur 82754C90 samples and then
    // resolves into; slot2/6/7 are the distortion snapshots.
    const std::array<uint32_t,4> distortionHeaders{0x82DFE498,0x82DFE708,0x82DFE7A4,0x82DFE3FC};
    const std::array<uint32_t,4> distortionWidths{1280,256,64,1280},distortionHeights{720,256,64,720};
    const std::array<uint32_t,4> distortionSlots{2,6,7,1};
    std::array<std::weak_ptr<Graphics::RenderTarget>,4> distortionLeases;
    for(unsigned i=0;i<distortionHeaders.size();++i){
        const auto header=distortionHeaders[i];const auto target=service.colorTexture(header);
        distortionLeases[i]=target;
        need(target->width==distortionWidths[i]&&target->height==distortionHeights[i],"Distortion resolve extent differs");
        rejects([&]{service.readbackColorTexture(header);});
        for(const auto& lease:leases)need(target!=lease.lock(),"Distortion snapshot aliases a post-filter snapshot");
        for(unsigned j=0;j<i;++j)need(target!=distortionLeases[j].lock(),"Distinct distortion snapshots share backing");
        for(uint32_t word=0;word<13;++word){const auto saved=PPC_LOAD_U32(header+4*word);PPC_STORE_U32(header+4*word,saved^1);
            rejects([&]{service.colorTexture(header);});PPC_STORE_U32(header+4*word,saved);}
        const auto publication=0x82DFEF10+4*distortionSlots[i],saved=PPC_LOAD_U32(publication);
        PPC_STORE_U32(publication,saved+4);rejects([&]{service.colorTexture(header);});PPC_STORE_U32(publication,saved);
    }
    need(service.colorTexture(0x82DFE7A4)==service.queryBackupTexture(0x82DFE7A4),"Distortion and query backup lost shared snapshot ownership");
    const auto root=PPC_LOAD_U32(0x82DFEB44);PPC_STORE_U32(0x82DFEB44,root+4096);
    rejects([&]{service.colorTexture(headers[0]);});PPC_STORE_U32(0x82DFEB44,root);
    need((PPC_LOAD_U32(headers[1]+32)&0xFFFFF000)==(PPC_LOAD_U32(headers[2]+32)&0xFFFFF000),
         "Original constructor did not preserve slot3/4 physical alias");
    need(!service.colorCopyCount(),"Allocation fabricated a color copy");
    bool wrongThreadRejected=false;std::thread other([&]{try{service.colorTexture(headers[0]);}catch(const std::exception&){wrongThreadRejected=true;}});other.join();
    need(wrongThreadRejected,"Color texture owner accepted a foreign thread");
    // Original split-screen constructors and preserve-first cleanup must leave
    // the full-size color owners intact despite the original shared placement.
    need(cpu.invoke(0x82751118,1,0,640,720)==1&&cpu.invoke(0x82751118,2,0,640,720)==2,"Original split viewport construction failed");
    cpu.invoke(0x82750FA8,1);for(const auto& lease:leases)need(!lease.expired(),"Partial cleanup retired row-zero color texture");
    for(const auto& lease:distortionLeases)need(!lease.expired(),"Partial cleanup retired row-zero distortion texture");
    cpu.invoke(0x82751510,0);
    rt.map(0x50000,0x1000,true,"viewport color copy clear fixture");PPC_STORE_U32(0x50000,0x00FFFFFF); // Exact endpoint RGBA: opaque cyan.
    const auto camera=PPC_LOAD_U32(0x82E07248);cpu.invoke(0x823EE940,camera,0x50000,7);cpu.invoke(0x823F1A18,camera);
    const auto binding=driver.cameraBinding();const auto source=driver.readbackColor(binding.colorIdentity),depth=driver.readbackDepth(binding.depthIdentity);
    const auto scalars=effective(driver);const auto clears=driver.cameraClearCount(),presents=driver.presentationCount(),frontCopies=driver.frontCopyCount();
    auto call=cpu.registers();call.r1.u32-=0x120;call.lastFunction=0x82439F60;call.lr=0x82773DF4;call.r3.u64=call.r4.u64=call.r5.u64=0;
    call.r6.u64=headers[0];call.r7.u64=call.r8.u64=call.r9.u64=call.r10.u64=0;call.f1.f64=0;
    call.r25.u64=2;call.r29.u64=0;call.r30.u64=0x82DFEA20;call.r31.u64=0x82D10000;
    PPC_STORE_U32(call.r1.u32,call.r1.u32+0x120);PPC_STORE_U32(call.r1.u32+0x118,0x8276E1C4);
    PPC_STORE_U32(call.r1.u32+0x5C,0);PPC_STORE_U32(call.r1.u32+0x64,0);
    auto* previous=currentContext;currentContext=&call;
    service.copyColor(call,base,0x82773E34);
    need(service.colorCopyCount()==1&&uint32_t(call.lr)==0x82773E38,"Exact first color copy did not publish its count/LR");
    need(service.readbackColorTexture(headers[0])==source,"Packed color copy changed source codes or alpha storage");
    const auto rejected=[&](auto action){rejects(action);need(service.colorCopyCount()==1&&service.readbackColorTexture(headers[0])==source,"Rejected copy mutated destination/count");};
    rejected([&]{service.copyColor(call,base,0x82773E38);});
    call.lr=0x82773DF4;
    rejected([&]{service.copyColor(call,base,0x82773E34);}); // Cannot restart an incomplete graph.
    call.r6.u64=headers[1];call.lr=0x82773F18;
    // Later slots have real source ownership, but no native aux attachment was
    // bound by this fixture. Mutating the CPU cache cannot forge an OM binding.
    const auto sourceField=PPC_LOAD_U32(0x82D0CF5C);PPC_STORE_U32(0x82D0CF5C,PPC_LOAD_U32(0x82DFEB7C));
    bool rejectedActualOM=false;
    try{service.copyColor(call,base,0x82773F44);}
    catch(const Graphics::Error& error){
        need(std::string_view(error.what())=="Native front copy source is not actual OM color zero",
             "Auxiliary copy rejected for a backend reason other than actual OM mismatch");
        rejectedActualOM=true;
    }
    need(rejectedActualOM,"Forged auxiliary CPU binding did not reach the actual backend OM guard");
    need(service.colorCopyCount()==1&&service.readbackColorTexture(headers[0])==source,
         "Actual OM rejection changed the copy count or completed snapshot");
    PPC_STORE_U32(0x82D0CF5C,sourceField);
    call.r4.u64=0x100;rejected([&]{service.copyColor(call,base,0x82773F44);});call.r4.u64=0;
    call.r5.u64=0x50000;rejected([&]{service.copyColor(call,base,0x82773F44);});call.r5.u64=0;
    call.r6.u64=headers[2];rejected([&]{service.copyColor(call,base,0x82773F44);});call.r6.u64=headers[1];
    call.lr=0x82773E38;rejected([&]{service.copyColor(call,base,0x82773F44);});call.lr=0x82773F18;
    call.f1.f64=1;rejected([&]{service.copyColor(call,base,0x82773F44);});call.f1.f64=0;
    PPC_STORE_U32(call.r1.u32+0x118,0);rejected([&]{service.copyColor(call,base,0x82773F44);});PPC_STORE_U32(call.r1.u32+0x118,0x8276E1C4);
    currentContext=previous;
    const auto after=driver.cameraBinding();
    need(after.camera==binding.camera&&after.colorIdentity==binding.colorIdentity&&after.depthIdentity==binding.depthIdentity&&after.viewport==binding.viewport&&
         driver.readbackColor(binding.colorIdentity)==source&&driver.readbackDepth(binding.depthIdentity)==depth&&effective(driver)==scalars&&
         driver.cameraClearCount()==clears&&driver.presentationCount()==presents&&driver.frontCopyCount()==frontCopies,
         "Color resolve changed source/depth, bindings, effective state, clear or presentation counters");

    // The phase-one effect resolves slot zero, then clears its source color
    // attachment. Give source and destination different endpoint colors so a
    // skipped copy, reversed copy, or premature clear cannot pass by accident.
    PPC_STORE_U32(0x50000,0xFF00FFFF); // Opaque magenta, distinct from the earlier cyan snapshot.
    cpu.invoke(0x823EE940,camera,0x50000,1);
    const auto effectSource=driver.readbackColor(binding.colorIdentity);
    const auto effectDestination=service.readbackColorTexture(headers[0]);
    const auto effectDepth=driver.readbackDepth(binding.depthIdentity);
    need(effectSource!=effectDestination&&effectDestination==source&&effectDepth==depth,
         "Phase-one fixture did not establish distinct color targets and preserved depth");
    const auto effectScalars=effective(driver);
    const auto effectClears=driver.cameraClearCount();
    const auto effectPresents=driver.presentationCount(),effectFrontCopies=driver.frontCopyCount();
    auto effectCall=cpu.registers();effectCall.r1.u32-=0xA0;
    effectCall.lastFunction=0x82455570;effectCall.lr=0x827724E0;
    effectCall.r3.u64=0;effectCall.r4.u64=0x100;effectCall.r5.u64=0;effectCall.r6.u64=headers[0];
    effectCall.r7.u64=effectCall.r8.u64=effectCall.r9.u64=0;effectCall.r10.u64=effectCall.r1.u32+0x70;
    effectCall.r30.u64=0x82D10000;effectCall.f1.f64=0;
    PPC_STORE_U32(effectCall.r1.u32,effectCall.r1.u32+0xA0);
    PPC_STORE_U32(effectCall.r1.u32+0x98,0x827517A8);
    PPC_STORE_U32(effectCall.r1.u32+0x5C,0);PPC_STORE_U32(effectCall.r1.u32+0x64,0);
    for(uint32_t offset=0x70;offset<0x80;offset+=4)PPC_STORE_U32(effectCall.r1.u32+offset,0);
    const auto effectGate=PPC_LOAD_U32(0x82DFF580);PPC_STORE_U32(0x82DFF580,1);
    currentContext=&effectCall;
    const auto rejectedEffect=[&](auto action) {
        rejects(action);
        need(service.colorCopyCount()==1&&service.readbackColorTexture(headers[0])==effectDestination&&
             driver.readbackColor(binding.colorIdentity)==effectSource&&
             driver.readbackDepth(binding.depthIdentity)==effectDepth,
             "Rejected effect resolve changed a color/depth target or copy count");
    };
    effectCall.r4.u64=0;rejectedEffect([&]{SimpsonsRejectUnportedGraphics(effectCall,base);});effectCall.r4.u64=0x100;
    effectCall.r6.u64=headers[1];rejectedEffect([&]{SimpsonsRejectUnportedGraphics(effectCall,base);});effectCall.r6.u64=headers[0];
    PPC_STORE_U32(effectCall.r1.u32+0x74,0x3F800000);
    rejectedEffect([&]{SimpsonsRejectUnportedGraphics(effectCall,base);});PPC_STORE_U32(effectCall.r1.u32+0x74,0);
    SimpsonsRejectUnportedGraphics(effectCall,base);
    need(service.colorCopyCount()==2&&service.readbackColorTexture(headers[0])==effectSource&&
         driver.readbackColor(binding.colorIdentity)==std::vector<uint8_t>(effectSource.size(),0)&&
         driver.readbackDepth(binding.depthIdentity)==effectDepth,
         "Phase-one resolve did not copy exact packed color before clearing only its source");
    const auto effectAfter=driver.cameraBinding();
    need(effectAfter.camera==binding.camera&&effectAfter.colorIdentity==binding.colorIdentity&&
         effectAfter.depthIdentity==binding.depthIdentity&&effectAfter.viewport==binding.viewport&&
         effective(driver)==effectScalars&&driver.cameraClearCount()==effectClears&&
         driver.presentationCount()==effectPresents&&driver.frontCopyCount()==effectFrontCopies,
         "Phase-one resolve changed camera binding, effective state or unrelated counters");
    currentContext=previous;PPC_STORE_U32(0x82DFF580,effectGate);
    // A real slot-two resolve creates a receipt on this generation. Keep an
    // external GPU lease across original row retirement/reconstruction: the
    // reused guest header must resolve the new owner with no old copy receipt.
    PPC_STORE_U32(0x50000,0x00FFFFFF);cpu.invoke(0x823EE940,camera,0x50000,1);
    bool alphaOne=false;const auto sceneTarget=driver.color(binding.colorIdentity,alphaOne);
    service.resolveColor(distortionHeaders[0],sceneTarget,false);
    need(service.readbackColorTexture(distortionHeaders[0])==driver.readbackColor(binding.colorIdentity),
         "Distortion resolve failed to preserve its exact source codes");
    auto retiredDistortion=service.colorTexture(distortionHeaders[0]);
    cpu.invoke(0x823F1A08,camera);
    cpu.invoke(0x82750FA8,0);service.requireReleased();for(const auto& lease:leases)need(lease.expired(),"Retired color owner retained native backing after completed copy");
    for(const auto header:headers)rejects([&]{service.colorTexture(header);});
    for(size_t i=0;i<distortionHeaders.size();++i){
        rejects([&]{service.colorTexture(distortionHeaders[i]);});
        need(distortionLeases[i].expired()==(i!=0),"Retired distortion texture lease ownership differs");}
    need(cpu.invoke(0x82751118,0,0,1280,720)==0,"Original row-zero reconstruction failed");
    for(size_t i=0;i<headers.size();++i){need(leases[i].expired(),"Reconstruction revived stale backing");leases[i]=service.colorTexture(headers[i]);
        rejects([&]{service.readbackColorTexture(headers[i]);});}
    for(size_t i=0;i<distortionHeaders.size();++i){
        const auto target=service.colorTexture(distortionHeaders[i]);
        need(target!=distortionLeases[i].lock(),"Reconstruction reused an externally retained distortion generation");
        rejects([&]{service.readbackColorTexture(distortionHeaders[i]);});
        distortionLeases[i]=target;}
    retiredDistortion.reset();
    need(service.colorTexture(distortionHeaders[2])==service.queryBackupTexture(distortionHeaders[2]),
         "Reconstruction split the shared distortion/corona texture owner");
    cpu.invoke(0x82750FA8,0);service.requireReleased();for(const auto& lease:leases)need(lease.expired(),"Recreated color texture did not retire");
    for(const auto& lease:distortionLeases)need(lease.expired(),"Recreated distortion texture did not retire");
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try{need(argc==2,"Original image required");exercise(argv[1]);std::printf("PASS: %zu viewport color texture lifetime/copy contracts; no post-filter draw claim\n",checks);return 0;}
    catch(const std::exception& error){std::fprintf(stderr,"FAIL after %zu checks: %s\n",checks,error.what());return 1;}
}
