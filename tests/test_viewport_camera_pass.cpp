#include "runtime/runtime.h"
#include "runtime/threads.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include "renderer/native_backend.h"
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace {
using namespace Simpsons;
size_t checks{};
void need(bool b,const char* why){++checks;if(!b)throw Failure(why);}
template<class F>void rejects(F&& f){bool rejected=false;try{f();}catch(const Failure&){rejected=true;}need(rejected,"Unqualified viewport operation accepted");}
// Revoking a live original raster page is global: other original threads (the
// Dac audio worker keeps a kernel event on the same heap page) would fault on
// it. Hold every other guest thread outside guest code for that brief window.
struct PausedGuestThreads {
    std::vector<HANDLE> paused;
    explicit PausedGuestThreads(Runtime& rt) {
        std::lock_guard lock(rt.threadMutex);
        try {
            for(const auto& thread:rt.threads) {
                if(!thread->native || GetThreadId(thread->native)==GetCurrentThreadId() ||
                   WaitForSingleObject(thread->native,0)==WAIT_OBJECT_0) continue;
                need(SuspendThread(thread->native)!=DWORD(-1),"Fixture could not pause an original thread");
                paused.push_back(thread->native);
                CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL; // Returns once the suspension is complete.
                need(GetThreadContext(thread->native,&context)!=FALSE,"Fixture could not confirm a paused original thread");
            }
        } catch(...) {for(auto native:paused)ResumeThread(native);throw;}
    }
    ~PausedGuestThreads() {for(auto native:paused)ResumeThread(native);}
    PausedGuestThreads(const PausedGuestThreads&)=delete;
    PausedGuestThreads& operator=(const PausedGuestThreads&)=delete;
};
struct Abi {uint64_t sp,lr;std::array<uint64_t,18> gpr,fpr;bool operator==(const Abi&)const=default;};
Abi abi(const PPCContext& c){return {c.r1.u64,c.lr,{c.r14.u64,c.r15.u64,c.r16.u64,c.r17.u64,c.r18.u64,c.r19.u64,c.r20.u64,c.r21.u64,c.r22.u64,c.r23.u64,c.r24.u64,c.r25.u64,c.r26.u64,c.r27.u64,c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64},
    {c.f14.u64,c.f15.u64,c.f16.u64,c.f17.u64,c.f18.u64,c.f19.u64,c.f20.u64,c.f21.u64,c.f22.u64,c.f23.u64,c.f24.u64,c.f25.u64,c.f26.u64,c.f27.u64,c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64}};}
void pixels(EngineDriver& d,uint32_t color,uint32_t depth,uint32_t width,uint32_t packed,uint8_t stencil){
    const auto c=d.readbackColor(color),z=d.readbackDepth(depth);need(c.size()==size_t(width)*720*4&&z.size()==size_t(width)*720*8,"Viewport readback extent differs");
    bool match=true;for(size_t i=0;i<size_t(width)*720;++i){uint32_t value{};float dz{};std::memcpy(&value,c.data()+4*i,4);std::memcpy(&dz,z.data()+8*i,4);match&=value==packed&&dz==0&&z[8*i+4]==stencil;}
    need(match,"Viewport clear altered another target or preserved wrong pixels");
}
#include "header/test_camera_pair_list.h"
struct Observed{};
void exercise(const char* image){
    Runtime rt;rt.load(image);PPCContext startup{};rt.initialize(startup);const auto entry=startup;auto* base=rt.base;
    bool observed=false;rt.audioBoundaryObserver=[](uint32_t pc,PPCContext&,uint8_t*){if(pc==0x828166FC)throw Observed{};};
    try{runOriginal(startup,base);}catch(const Observed&){observed=true;}rt.audioBoundaryObserver={};need(observed,"Missing actual original pre-FX startup boundary");
    auto& d=*rt.engineDriver;EngineCpuCalls cpu(entry,base);auto& ctx=cpu.registers();const auto saved=abi(ctx);rt.map(0x50000,0x1000,true,"viewport clear arguments");
    const uint32_t loading=PPC_LOAD_U32(0x82E07248),engine=PPC_LOAD_U32(0x82D0CA68),defaultColor=PPC_LOAD_U32(0x82D0CB00),defaultDepth=PPC_LOAD_U32(0x82D0CAFC);
    need(loading&&!PPC_LOAD_U32(0x82D08B10),"Original loading camera/viewport construction precondition differs");
    const auto baseline=d.rasterCount(),clearBaseline=d.cameraClearCount();const auto stencilBefore=PPC_LOAD_U32(0x82D0CB14);
    const uint32_t manager=cpu.invoke(0x8269D788);need(manager&&PPC_LOAD_U32(0x82D08B10)==manager,"Original viewport manager factory failed");
    std::array<uint32_t,3> camera{},color{},depth{};std::array<std::weak_ptr<Graphics::RenderTarget>,3> colorLease;std::array<std::weak_ptr<Graphics::DepthTarget>,3> depthLease;
    constexpr std::array<uint32_t,3> rgba={0x00FF00FF,0x0000FFFF,0xFFFFFFFF},packed={0xC00FFC00,0xFFF00000,0xFFFFFFFF};
    for(uint32_t slot=0;slot<3;++slot){const auto row=0x82CD12CC+20*slot;ctx.f1.f64=std::bit_cast<float>(PPC_LOAD_U32(row));ctx.f2.f64=std::bit_cast<float>(PPC_LOAD_U32(row+4));
        ctx.f3.f64=std::bit_cast<float>(PPC_LOAD_U32(row+8));ctx.f4.f64=std::bit_cast<float>(PPC_LOAD_U32(row+12));cpu.invoke(0x8269D608,manager,slot);camera[slot]=PPC_LOAD_U32(manager+0x14+4*slot);
        const auto offset=PPC_LOAD_U32(0x82E3DC94);color[slot]=PPC_LOAD_U32(PPC_LOAD_U32(camera[slot]+0x60)+offset);depth[slot]=PPC_LOAD_U32(PPC_LOAD_U32(camera[slot]+0x64)+offset);
        bool alpha{};colorLease[slot]=d.color(color[slot],alpha);depthLease[slot]=d.depth(depth[slot]);need(!alpha,"Private viewport samples as front-buffer alpha one");}
    need(abi(ctx)==saved&&d.rasterCount()==baseline+6,"Original viewport constructors changed ABI or native resource count");
    bool defaultAlpha{};const auto workingColor=d.color(defaultColor,defaultAlpha);const auto workingDepth=d.depth(defaultDepth);
    need(color[0]!=defaultColor&&depth[0]!=defaultDepth&&colorLease[0].lock()==workingColor&&depthLease[0].lock()==workingDepth,
        "Full-size views must retain distinct logical IDs and share default working storage");
    for(uint32_t slot=1;slot<3;++slot)need(colorLease[slot].lock()!=workingColor&&depthLease[slot].lock()!=workingDepth,
        "Different-size camera was incorrectly admitted to exact full-size alias profile");
    const std::array<uint32_t,3> resolvedColor={PPC_LOAD_U32(0x82D0CF90),PPC_LOAD_U32(0x82D0CF8C),PPC_LOAD_U32(0x82D0CF88)};
    std::array<std::vector<uint8_t>,3> resolvedBefore;
    for(size_t i=0;i<resolvedColor.size();++i){bool alpha{};need(d.color(resolvedColor[i],alpha)!=workingColor,"Resolve texture shares working color storage");resolvedBefore[i]=d.readbackColor(resolvedColor[i]);}
    const auto resolvedDepth=PPC_LOAD_U32(0x82D0CF84);need(d.depth(resolvedDepth)!=workingDepth,"Depth copy shares working depth storage");
    const auto depthBefore=d.readbackDepth(resolvedDepth);
    PPC_STORE_U32(0x50000,0xFF0000FF);PPC_STORE_U32(0x82D0CB14,0x11);cpu.invoke(0x823EE940,loading,0x50000,7);
    pixels(d,color[0],depth[0],1280,0xC00003FF,0x11);
    for(uint32_t slot=0;slot<3;++slot){PPC_STORE_U32(0x50000,rgba[slot]);PPC_STORE_U32(0x82D0CB14,0x22+slot);need(cpu.invoke(0x823EE940,camera[slot],0x50000,7)==1,"Original private clear returned wrong Boolean");}
    for(uint32_t cycle=0;cycle<2;++cycle){for(uint32_t slot=0;slot<3;++slot){const auto c=camera[slot];const auto clears=d.cameraClearCount();
        need(cpu.invoke(0x823F1A18,c)==c&&abi(ctx)==saved,"Original private camera begin or ABI differs");
        const auto binding=d.cameraBinding();need(binding.camera==c&&binding.colorIdentity==color[slot]&&binding.depthIdentity==depth[slot]&&
            binding.viewport==std::array<uint32_t,6>{0,0,slot?640u:1280u,720,0x3F800000,0},"Private viewport selected default/foreign attachments or wrong logical viewport");
        if(!cycle && !slot)cameraPairListContracts(rt,d,base,binding);
        if(!cycle)for(const auto raster:{binding.colorRaster,binding.depthRaster}) {
            const auto plugin=raster+PPC_LOAD_U32(0x82E3DC94);
            for(const auto field:{raster,raster+0xC,raster+0x10,raster+0x14,raster+0x1C,raster+0x20,
                    plugin,plugin+4,plugin+8,plugin+0xC,plugin+0x18}) {
                const auto old=PPC_LOAD_U32(field);PPC_STORE_U32(field,old^1);
                rejects([&]{d.cameraBinding();});PPC_STORE_U32(field,old);
                const auto restored=d.cameraBinding();
                need(restored.colorIdentity==binding.colorIdentity&&restored.depthIdentity==binding.depthIdentity&&
                    d.cameraClearCount()==clears,"Rejected live metadata mutation changed selected attachments");
            }
            const auto page=raster>>12;const auto access=rt.pageAccess[page].load();
            {PausedGuestThreads paused(rt);rt.pageAccess[page]=1;
             rejects([&]{d.cameraBinding();});rt.pageAccess[page]=access;}
            need(d.cameraBinding().camera==c,"Restored raster permission lost the live binding");
        }
        need(PPC_LOAD_U32(0x82D0CF5C)==color[slot]&&PPC_LOAD_U32(0x82D0CF58)==depth[slot]&&PPC_LOAD_U32(engine)==c&&PPC_LOAD_U32(0x82E3DD60)==c&&PPC_LOAD_U32(0x82D0CB1C)==1,"Original camera/cache ownership differs");
        for(auto [index,matrix]:std::array<std::array<uint32_t,2>,2>{{{1,0x82D0CA70},{2,0x82CD1AB0}}}){const auto address=PPC_LOAD_U32(0x82D0CB40+index*4);need(address&&!std::memcmp(rt.pointer(address,64,false),rt.pointer(matrix,64,false),64),"Original private camera matrix-pool update differs");}
        rejects([&]{d.preflightRasterDestroy(base,PPC_LOAD_U32(c+0x60));});rejects([&]{d.preflightRasterDestroy(base,PPC_LOAD_U32(c+0x64));});
        need(cpu.invoke(0x823F1A08,c)==c&&abi(ctx)==saved&&!PPC_LOAD_U32(engine)&&!PPC_LOAD_U32(0x82E3DD60)&&!PPC_LOAD_U32(0x82D0CB1C),"Original private camera end did not release CPU ownership");
        need(d.cameraClearCount()==clears,"Original begin/end unexpectedly cleared targets");
        rejects([&]{d.preflightRasterDestroy(base,PPC_LOAD_U32(c+0x60));});
        pixels(d,defaultColor,defaultDepth,1280,packed[0],0x22);
        for(uint32_t other=0;other<3;++other)pixels(d,color[other],depth[other],other?640:1280,packed[other],uint8_t(0x22+other));
    }}
    const auto c=camera[0];const auto oldSlot=PPC_LOAD_U32(manager+0x18);PPC_STORE_U32(manager+0x18,c);rejects([&]{d.selectCamera(base,c);});PPC_STORE_U32(manager+0x18,oldSlot);
    const auto oldDepth=PPC_LOAD_U32(c+0x64);PPC_STORE_U32(c+0x64,0);rejects([&]{d.selectCamera(base,c);});PPC_STORE_U32(c+0x64,oldDepth);
    rejects([&]{d.selectCamera(base,c+4);});rejects([&]{d.clearCamera(base,c,0x50000,8);});
    need(d.cameraClearCount()==clearBaseline+4,"Rejected selection/clear mutated the targets");
    cpu.invoke(0x823EE6C8,loading);need(d.cameraBinding().colorIdentity==defaultColor&&d.cameraBinding().depthIdentity==defaultDepth,"Switching back to the original loading camera did not restore default attachments");
    for(const auto c:camera){d.preflightRasterDestroy(base,PPC_LOAD_U32(c+0x60));d.preflightRasterDestroy(base,PPC_LOAD_U32(c+0x64));}
    cpu.invoke(0x8269C438,manager);need(d.rasterCount()==baseline&&abi(ctx)==saved,"Original viewport reset retained private raster ownership");
    for(uint32_t slot=0;slot<3;++slot){need(colorLease[slot].expired()==(slot!=0)&&depthLease[slot].expired()==(slot!=0),"Retirement lost shared working storage or retained an independent target");bool alpha{};rejects([&]{d.color(color[slot],alpha);});rejects([&]{d.depth(depth[slot]);});rejects([&]{d.selectCamera(base,camera[slot]);});}
    cpu.invoke(0x8269CC28);need(!PPC_LOAD_U32(0x82D08B10)&&abi(ctx)==saved,"Original viewport deleting destructor retained manager/changed ABI");PPC_STORE_U32(0x82D0CB14,stencilBefore);
    pixels(d,defaultColor,defaultDepth,1280,packed[0],0x22);
    for(size_t i=0;i<resolvedColor.size();++i)need(d.readbackColor(resolvedColor[i])==resolvedBefore[i],"Working clears or view retirement changed a resolved color texture");
    need(d.readbackDepth(resolvedDepth)==depthBefore,"Working clears or view retirement changed resolved depth");
    std::printf("Original viewport passes: three private cameras,two begin/end cycles,shared full-size working storage,distinct resolve textures,default restore and paired manager release\n");
}
}
int main(int argc,char** argv){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try{need(argc==2,"Original image required");exercise(argv[1]);std::printf("PASS native viewport camera passes:%zu checks; original CPU pass,owned target selection/clear/readback and paired release;no draw or gameplay claim,ALL MUTED\n",checks);return 0;}
    catch(const std::exception& e){std::fprintf(stderr,"FAIL viewport passes:%zu checks:%s\n",checks,e.what());return 1;}}
