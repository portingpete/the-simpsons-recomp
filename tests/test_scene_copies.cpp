#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include "runtime/engine_scene_copies.h"
#include "renderer/native_backend.h"
#include <array>
#include <cstdio>
#include <set>

namespace {
using namespace Simpsons;
size_t checks{};
void need(bool b,const char* why){++checks;if(!b)throw Failure(why);}
template<class F>void rejects(F f){try{f();}catch(const Failure&){++checks;return;}throw Failure("Invalid scene copy accepted");}
struct Observed{};
void exercise(const char* image){
    Runtime rt;rt.load(image);PPCContext startup{};rt.initialize(startup);const auto entry=startup;auto* base=rt.base;
    rt.audioBoundaryObserver=[](uint32_t pc,PPCContext&,uint8_t*){if(pc==0x828166FC)throw Observed{};};
    bool observed=false;try{runOriginal(startup,base);}catch(const Observed&){observed=true;}rt.audioBoundaryObserver={};
    need(observed,"Original startup checkpoint missing");auto& d=*rt.engineDriver;auto& service=d.sceneCopies();
    EngineCpuCalls cpu(entry,base);auto& c=cpu.registers();const auto camera=PPC_LOAD_U32(0x82E07248);
    rt.map(0x50000,0x1000,true,"scene copy independent pixel fixture");
    const auto presents=d.presentationCount(),draws=d.screenDrawCount();std::set<uint32_t> seen;
    // The original mode flag enables these optional destinations. A test uses
    // the actual parent function and original publication/cleanup instructions.
    const auto mode=PPC_LOAD_U8(0x82CD1430);PPC_STORE_U8(0x82CD1430,1);
    for(uint32_t cycle=0;cycle<2;++cycle){
        PPC_STORE_U32(0x50000,cycle?0x0000FFFF:0xFF0000FF);
        cpu.invoke(0x823EE940,camera,0x50000,7);cpu.invoke(0x823F1A18,camera);
        const auto source=d.readbackColor(d.cameraBinding().colorIdentity);
        std::fprintf(stderr,"[SCENE COPY TEST] invoking original parent cycle=%u\n",cycle);
        cpu.invoke(0x823C7500,0,camera);
        need(service.count()==2,"Original parent did not publish the texture pair");
        const std::array<uint32_t,2> ids={PPC_LOAD_U32(0x82D09894),PPC_LOAD_U32(0x82D6C7F0)};
        std::array<std::weak_ptr<Graphics::RenderTarget>,2> weak;
        for(size_t i=0;i<2;++i){
            need(seen.insert(ids[i]).second&&!rt.pageAccess[ids[i]>>12].load(),"Scene copy identity reused/addressable");
            const auto target=service.backing(ids[i]);weak[i]=target;
            need(target->width==1280&&target->height==720&&target->format==Graphics::TargetFormat::RGB10A2,"Scene copy storage differs");
        }
        need(service.readback(ids[0])==source,"Original parent color copy changed pixel codes");
        need(service.edgeSource(ids[0],camera)==service.backing(ids[0]),"Submitted first camera copy not sampleable");
        rejects([&]{service.edgeSource(ids[0],camera+4);});
        rejects([&]{service.edgeSource(ids[1],camera);});
        rejects([&]{service.aaSource(ids[1],camera);});
        // Exercise the other authored destination through the same original
        // helper ABI; shader/effect execution is outside this copy fixture.
        c.lr=0x823C7684;c.r26.u32=camera;cpu.invoke(0x826B08B0,ids[1],0,camera);
        need(service.readback(ids[1])==source,"Second original color copy changed pixel codes");
        need(service.aaSource(ids[1],camera)==service.backing(ids[1]),"Submitted second camera copy not sampleable");
        rejects([&]{service.aaSource(ids[0],camera);});
        const auto count=d.cameraCopyCount();const auto first=service.readback(ids[0]),second=service.readback(ids[1]);
        auto rejected=[&](auto f){rejects(f);need(d.cameraCopyCount()==count&&service.readback(ids[0])==first&&service.readback(ids[1])==second,"Rejected scene copy mutated pixels or queued work");};
        c.lr=0x823C7614;rejected([&]{cpu.invoke(0x826B08B0,ids[0],0,camera);});
        c.lr=0x823C7684;rejected([&]{cpu.invoke(0x826B08B0,ids[0],0,camera);});
        c.lr=0x823C7610;rejected([&]{cpu.invoke(0x826B08B0,ids[0],PPC_LOAD_U32(0x82D0CF84),camera);});
        const auto primary=PPC_LOAD_U32(0x82D09894);PPC_STORE_U32(0x82D09894,ids[1]);
        rejects([&]{service.backing(ids[0]);});PPC_STORE_U32(0x82D09894,primary);
        c.r3.u32=ids[0];c.r31.u32=0x82D10000;c.lr=0x823C70FC;rejects([&]{service.release(c,base);});
        c.lr=0x823C70F8;const auto attachment=PPC_LOAD_U32(0x82D0CF5C);PPC_STORE_U32(0x82D0CF5C,ids[0]);
        rejects([&]{service.release(c,base);});PPC_STORE_U32(0x82D0CF5C,attachment);
        need(service.count()==2,"Rejected release changed ownership");
        cpu.invoke(0x823F1A08,camera);cpu.invoke(0x823C70C0);
        need(!service.count()&&!PPC_LOAD_U32(0x82D09894)&&!PPC_LOAD_U32(0x82D6C7F0),"Original global cleanup incomplete");service.requireReleased();
        for(const auto id:ids){need(!service.owns(id),"Retired scene copy still owned");rejects([&]{service.backing(id);});rejects([&]{service.edgeSource(id,camera);});}
        // Another valid copy retires completed GPU leases. Native event tests
        // separately verify retention when the caller releases before completion.
        cpu.invoke(0x823F1A18,camera);cpu.invoke(0x826B08B0,PPC_LOAD_U32(0x82D0CF88),0,camera);cpu.invoke(0x823F1A08,camera);
        for(const auto& w:weak)need(w.expired(),"Retired scene copy backing remains after completed GPU leases");
    }
    PPC_STORE_U8(0x82CD1430,mode);
    need(d.presentationCount()==presents&&d.screenDrawCount()==draws,"Scene copy allocation/copy introduced draw or presentation");
}
}
int main(int argc,char** argv){try{if(argc!=2)throw Failure("Original image required");exercise(argv[1]);std::printf("PASS: %zu scene copy ownership/pixel/lifetime checks; no scene shader claim\n",checks);return 0;}
catch(const std::exception& e){std::fprintf(stderr,"FAIL after %zu checks: %s\n",checks,e.what());return 1;}}
