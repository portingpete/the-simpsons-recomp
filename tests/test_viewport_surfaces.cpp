#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include "runtime/engine_viewport_surfaces.h"
#include "renderer/native_backend.h"
#include <array>
#include <cstdio>
#include <set>
#include <vector>

namespace {
using namespace Simpsons;
size_t checks{};
void need(bool b,const char* why){++checks;if(!b)throw Failure(why);}
template<class F>void rejects(F f){try{f();}catch(const Failure&){++checks;return;}throw Failure("Invalid viewport surface ownership accepted");}
struct Observed{};
void exercise(const char* image){
    Runtime rt;rt.load(image);PPCContext startup{};rt.initialize(startup);const auto entry=startup;auto* base=rt.base;
    rt.audioBoundaryObserver=[](uint32_t pc,PPCContext&,uint8_t*){if(pc==0x828166FC)throw Observed{};};
    bool observed=false;try{runOriginal(startup,base);}catch(const Observed&){observed=true;}
    rt.audioBoundaryObserver={};
    need(observed,"Original startup checkpoint missing");
    auto& service=rt.engineDriver->viewportSurfaces();EngineCpuCalls cpu(entry,base);
    const std::weak_ptr<Graphics::DepthTarget> depthLease=service.depthTexture(0x82DFE840);
    const std::weak_ptr<Graphics::RenderTarget> queryLease=service.queryTexture(0x82DFE8DC);
    need(!queryLease.expired()&&queryLease.lock()->width==64&&queryLease.lock()->height==8&&
         queryLease.lock()->format==Graphics::TargetFormat::RGB10A2,"Original query texture native extent/format differs");
    rejects([&]{service.queryTexture(0x82DFE8DD);});
    const auto queryWord=PPC_LOAD_U32(0x82DFE900);PPC_STORE_U32(0x82DFE900,queryWord^1);
    rejects([&]{service.queryTexture(0x82DFE8DC);});PPC_STORE_U32(0x82DFE900,queryWord);
    need(!depthLease.expired() && !service.depthCopyCount(),"Original full-size depth texture owner missing or fabricated copy");
    rejects([&]{service.depthTexture(0x82DFE841);});
    rejects([&]{service.depthTexture(0x82DFE874);});
    const auto headerWord=PPC_LOAD_U32(0x82DFE864);
    PPC_STORE_U32(0x82DFE864,headerWord^1);rejects([&]{service.depthTexture(0x82DFE840);});PPC_STORE_U32(0x82DFE864,headerWord);
    need(PPC_LOAD_U32(0x82DFEBA0)==1&&service.count()==5,"Original audio-start checkpoint did not own the first five surfaces");
    need(cpu.invoke(0x82751118,1,0,640,720)==1&&cpu.invoke(0x82751118,2,0,640,720)==2,"Original additional viewport construction failed");
    need(PPC_LOAD_U32(0x82DFEBA0)==3&&service.count()==15,"Original constructors did not own three sets of five surfaces");
    std::set<uint32_t> ids;std::vector<std::weak_ptr<Graphics::RenderTarget>> weak;
    for(uint32_t row=0;row<3;++row)for(uint32_t slot=0;slot<10;++slot){
        const auto id=PPC_LOAD_U32(0x82DFEF38+100*row+4*slot);
        if(slot<3||slot>7){need(!id,"Original non-surface slot unexpectedly published");continue;}
        need(ids.insert(id).second&&service.owns(id)&&!rt.pageAccess[id>>12].load(),"Native surface identity lacks unique ownership");
        const auto target=service.backing(id);weak.push_back(target);
        const auto w=slot<=5?(row?640u:1280u)/(1u<<(slot-2)):(slot==6?256u:64u);
        const auto h=slot<=5?720u/(1u<<(slot-2)):(slot==6?256u:64u);
        need(target->width==w&&target->height==h&&target->format==Graphics::TargetFormat::RGB10A2,"Native surface dimensions/format differ");
        const auto field=0x82DFEF38+100*row+4*slot;
        PPC_STORE_U32(field,id+1);rejects([&]{service.backing(id);});PPC_STORE_U32(field,id);
    }
    rejects([&]{service.requireReleased();});
    {
        auto c=cpu.registers();auto* previous=currentContext;currentContext=&c;
        const auto id=PPC_LOAD_U32(0x82DFEF44);const auto before=service.count();
        c.lr=0x82751024;c.r3.u32=id;c.r30.u32=0x82DFEF00;c.r31.u32=0x82DFEF44;c.r28.u32=7;c.r29.u32=0;
        rejects([&]{service.release(c,base);});
        c.lr=0x82751020;c.r31.u32+=4;rejects([&]{service.release(c,base);});c.r31.u32-=4;
        const auto bound=PPC_LOAD_U32(0x82D0CF5C);PPC_STORE_U32(0x82D0CF5C,id);
        rejects([&]{service.release(c,base);});PPC_STORE_U32(0x82D0CF5C,bound);
        need(service.count()==before&&PPC_LOAD_U32(0x82DFEF44)==id,"Rejected release changed ownership/publication");
        currentContext=previous;
    }
    // Exercise the actual original destructor's preserve-first-row branch.
    cpu.invoke(0x82750FA8,1);
    need(PPC_LOAD_U32(0x82DFEBA0)==1&&service.count()==5,"Original partial cleanup did not preserve just row zero");
    need(!depthLease.expired(),"Preserve-first-row cleanup retired its depth texture");
    for(uint32_t i=0;i<weak.size();++i)need(weak[i].expired()==(i>=5),"Partial cleanup native resource lifetime differs");
    // Real original row constructors recreate the two retired viewport rows.
    need(cpu.invoke(0x82751118,1,0,640,720)==1&&cpu.invoke(0x82751118,2,0,640,720)==2,"Original viewport row recreation failed");
    need(service.count()==15,"Recreated rows lack native resources");
    for(uint32_t row=1;row<3;++row)for(uint32_t slot=3;slot<=7;++slot)
        need(!ids.contains(PPC_LOAD_U32(0x82DFEF38+100*row+4*slot)),"Recreation reused a stale identity");
    cpu.invoke(0x82750FA8,0);need(!PPC_LOAD_U32(0x82DFEBA0)&&!service.count(),"Original complete cleanup retained ownership");service.requireReleased();
    need(depthLease.expired(),"Original full cleanup retained depth texture ownership");
    need(queryLease.expired(),"Original full cleanup retained query texture ownership");
    rejects([&]{service.depthTexture(0x82DFE840);});
    for(const auto& w:weak)need(w.expired(),"Original cleanup retained native backing");
    for(const auto id:ids){need(!service.owns(id),"Retired identity remains owned");rejects([&]{service.backing(id);});}
    for(uint32_t row=0;row<3;++row){
        need(!PPC_LOAD_U32(0x82DFEF00+100*row)&&!PPC_LOAD_U8(0x82DFEF60+100*row),"Original row reset missing");
        for(uint32_t slot=0;slot<10;++slot)need(!PPC_LOAD_U32(0x82DFEF38+100*row+4*slot),"Original slot reset missing");
    }
    need(cpu.invoke(0x82751118,0,0,1280,720)==0,"Original full-size row recreation failed");
    const std::weak_ptr<Graphics::DepthTarget> newDepth=service.depthTexture(0x82DFE840);
    need(!newDepth.expired() && depthLease.expired(),"Recreated header reused retired native depth ownership");
    cpu.invoke(0x82750FA8,0);service.requireReleased();need(newDepth.expired(),"Recreated depth texture did not retire");
}
}
int main(int argc,char** argv){try{if(argc!=2)throw Failure("Original image required");exercise(argv[1]);std::printf("PASS: %zu original viewport surface allocation/release/recreation checks; no rendering claim\n",checks);return 0;}
catch(const std::exception& e){std::fprintf(stderr,"FAIL after %zu checks: %s\n",checks,e.what());return 1;}}
