#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "ppc_recomp_shared.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

PPC_FUNC(__imp__NtCreateEvent);
PPC_FUNC(__imp__NtSetEvent);
PPC_FUNC(__imp__NtClearEvent);

namespace {
using Simpsons::Runtime;using Simpsons::KernelHandle;
constexpr uint32_t out=0x10040,poll=0x10080,bounded=0x10088;
constexpr uint32_t invalid=0xC0000008,parameter=0xC000000D,timeout=0x102;
size_t checks=0;
void need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F> void rejects(F call,const char* why){bool caught=false;try{call();}catch(const Simpsons::Failure&){caught=true;}need(caught,why);}
using Import=void(*)(PPCContext&,uint8_t*);
uint32_t checked(Runtime& rt,Import function,PPCContext ctx,uint32_t fp=0x9FC0) {
    PPCContext expected;std::memcpy(&expected,&ctx,sizeof(ctx));
    const uint32_t saved=PPCFPSCRRegister::getcsr();
    struct Restore{uint32_t fp;~Restore(){PPCFPSCRRegister::restoreHostCSR(fp);}} restore{saved};
    PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(0x321654);
    function(ctx,rt.base);const auto actualFP=PPCFPSCRRegister::getcsr();const auto error=GetLastError();
    PPCFPSCRRegister::restoreHostCSR(saved);expected.r3.u64=ctx.r3.u32;
    need(!std::memcmp(&ctx,&expected,sizeof(ctx)) && actualFP==fp && error==0x321654,"Event import changed CPU context or host controls");
    return ctx.r3.u32;
}
uint32_t create(Runtime& rt,uint32_t type,uint32_t initial,uint32_t output=out,uint32_t attributes=0,uint32_t fp=0x9FC0) {
    PPCContext ctx{};std::memset(&ctx,0xA5,sizeof(ctx));ctx.r3.u32=output;ctx.r4.u32=attributes;ctx.r5.u32=type;ctx.r6.u32=initial;
    return checked(rt,__imp__NtCreateEvent,ctx,fp);
}
uint32_t make(Runtime& rt,uint32_t type=1,uint32_t initial=0,uint32_t fp=0x9FC0) {
    auto* base=rt.base;PPC_STORE_U64(out-4,0xA7A7A7A7DEADBEEFull);PPC_STORE_U32(out+4,0xA7A7A7A7);
    need(create(rt,type,initial,out,0,fp)==0,"Event creation failed");const auto id=PPC_LOAD_U32(out);auto object=rt.getHandle(id);
    need(id && object && object->native && object->type==KernelHandle::Type::Event,"Event lacks real owned native backing");
    need(PPC_LOAD_U32(out-4)==0xA7A7A7A7 && PPC_LOAD_U32(out+4)==0xA7A7A7A7,"Event creation changed adjacent output bytes");return id;
}
uint32_t set(Runtime& rt,uint32_t id,uint32_t previous=0) {
    PPCContext ctx{};std::memset(&ctx,0xA5,sizeof(ctx));ctx.r3.u32=id;ctx.r4.u32=previous;return checked(rt,__imp__NtSetEvent,ctx);
}
uint32_t clear(Runtime& rt,uint32_t id) {
    PPCContext ctx{};std::memset(&ctx,0xA5,sizeof(ctx));ctx.r3.u32=id;
    ctx.r4.u64=0xFEDCBA9800050000ull; // Incidental unmapped value, never a pointer.
    return checked(rt,__imp__NtClearEvent,ctx);
}
uint32_t wait(Runtime& rt,uint32_t id,bool block=false) {
    PPCContext ctx{};ctx.r3.u32=id;ctx.r4.u32=1;ctx.r6.u32=block?bounded:poll;
    __imp__NtWaitForSingleObjectEx(ctx,rt.base);return ctx.r3.u32;
}
void close(Runtime& rt,uint32_t id){PPCContext ctx{};ctx.r3.u32=id;__imp__NtClose(ctx,rt.base);need(ctx.r3.u32==0,"Event close failed");}
void states(Runtime& rt) {
    auto* base=rt.base;
    for(uint32_t fp:{0x1F80u,0x3FC0u,0x5F80u,0x9FC0u,0xE07Fu})for(uint32_t type:{0u,1u})for(uint32_t initial:{0u,1u}) {
        const auto id=make(rt,type,initial,fp);
        need(wait(rt,id)==(initial?0:timeout),"Event initial state differs");
        need(wait(rt,id)==(!type && initial?0:timeout),"Auto/manual event consumption differs");
        need(set(rt,id,out+8)==0 && PPC_LOAD_U32(out+8)==(!type && initial?1u:0u),"First set previous-state differs");
        need(set(rt,id,out+8)==0 && PPC_LOAD_U32(out+8)==1,"Repeated set did not report actual previous state");
        need(wait(rt,id)==0 && wait(rt,id)==(!type?0:timeout),"Set auto/manual event wait behavior differs");
        need(clear(rt,id)==0 && wait(rt,id)==timeout,"Clear did not reset event");
        close(rt,id);need(set(rt,id,out+8)==invalid && clear(rt,id)==invalid && wait(rt,id)==invalid,"Closed event remained usable");
    }
}
void rejection(Runtime& rt) {
    auto* base=rt.base;PPC_STORE_U32(out,0xDEADBEEF);const auto count=rt.handles.size();const auto next=rt.nextHandle;
    need(create(rt,1,0,0)==parameter && create(rt,2,0)==parameter,"Null output/invalid event type accepted");
    for(uint32_t address:{0x40000u,0x50000u,0x2FFFEu})rejects([&]{create(rt,1,0,address);},"Readonly/unmapped/straddling event output accepted");
    rejects([&]{create(rt,1,0,out,0x50000);},"Named event attributes accepted");
    PPCContext bad{};bad.r3.u32=out;bad.r5.u32=1;
    rejects([&]{__imp__NtCreateEvent(bad,base+1);},"Foreign event runtime accepted");
    need(rt.handles.size()==count && rt.nextHandle==next && PPC_LOAD_U32(out)==0xDEADBEEF,"Rejected creation changed outputs/ownership");
    const auto id=make(rt);PPC_STORE_U32(out+8,0x12345678);
    for(uint32_t address:{0x40000u,0x50000u,0x2FFFEu})rejects([&]{set(rt,id,address);},"Invalid previous-state pointer accepted");
    need(wait(rt,id)==timeout && PPC_LOAD_U32(out+8)==0x12345678,"Rejected set changed event or previous-state output");
    close(rt,id);
    HANDLE raw=CreateSemaphoreW(nullptr,0,1,nullptr);need(raw!=nullptr,"Wrong-type semaphore fixture failed");
    const auto wrong=rt.addHandle(std::make_shared<KernelHandle>(raw,KernelHandle::Type::Semaphore));
    need(set(rt,wrong,out+8)==invalid && clear(rt,wrong)==invalid && wait(rt,wrong)==timeout,"Event import changed wrong-type object");
    close(rt,wrong);
}
struct Worker {
    std::exception_ptr error;std::jthread thread;
    template<class F> explicit Worker(F body):thread([this,body]{try{body();}catch(...){error=std::current_exception();}}){}
    void finish(){thread.join();if(error)std::rethrow_exception(error);}
};
void lease(const std::shared_ptr<KernelHandle>& object) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    while(object.use_count()<3 && std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    need(object.use_count()>=3,"Wait import did not retain its object lease");
}
void concurrentClose(Runtime& rt) {
    const auto id=make(rt);auto object=rt.getHandle(id);std::weak_ptr<KernelHandle> weak=object;
    HANDLE duplicate{};need(DuplicateHandle(GetCurrentProcess(),object->native,GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS)!=FALSE,"Native event duplication failed");
    struct Owned{HANDLE h;~Owned(){CloseHandle(h);}} owned{duplicate};
    Worker worker([&]{if(wait(rt,id,true)!=0)throw std::runtime_error("Closed in-flight event wait failed");});
    lease(object);close(rt,id);object.reset();need(!weak.expired(),"Close destroyed an in-flight event");
    need(SetEvent(duplicate)!=FALSE,"Could not signal actual surviving native event");worker.finish();
    need(weak.expired() && WaitForSingleObject(duplicate,0)==WAIT_TIMEOUT,"Completed wait leaked lease or failed auto-reset");
}
void originalOwner(Runtime& rt) {
    auto* base=rt.base;constexpr uint32_t object=0x18000,pcr=0x20000,thread=0x23000;
    std::array<uint8_t,0x100> expected;expected.fill(0xA7);std::memcpy(rt.pointer(object,0x100,true),expected.data(),expected.size());
    PPC_STORE_U32(pcr+0x100,thread);PPC_STORE_U32(pcr+0x150,0);PPC_STORE_U32(thread+0x160,0xDEADBEEF);
    PPCContext entry{};entry.r1.u64=0x30000;entry.r13.u32=pcr;Simpsons::EngineCpuCalls cpu(entry,base);const auto stack=cpu.registers().r1.u64;
    cpu.invoke(0x827B4E78,object);const auto id=PPC_LOAD_U32(object+4);auto event=rt.getHandle(id);
    need(event && event->type==KernelHandle::Type::Event && wait(rt,id)==timeout,"Original event constructor lacks unsignaled native event");
    std::memset(expected.data()+8,0,28);
    for(size_t i=0;i<4;++i)expected[4+i]=expected[0x14+i]=uint8_t(id>>(24-8*i));
    need(!std::memcmp(rt.pointer(object,0x100,false),expected.data(),expected.size()) && PPC_LOAD_U32(thread+0x160)==0,"Original event constructor ownership/publication differs");
    cpu.invoke(0x827B4EE0,object);
    need(!rt.getHandle(id) && PPC_LOAD_U32(object+4)==0 && cpu.registers().r1.u64==stack,"Original event destructor did not close ownership/restore frame");
}
void cancellation(Runtime& rt) {
    const auto id=make(rt);auto object=rt.getHandle(id);
    Worker worker([&]{bool stopped=false;try{wait(rt,id,true);}catch(const Simpsons::Failure&){stopped=true;}if(!stopped)throw std::runtime_error("Runtime cancellation failed to wake event wait");});
    lease(object);rt.requestStop("event fixture complete");worker.finish();
    rejects([&]{create(rt,1,0);},"Cancelled runtime created event");rejects([&]{set(rt,id);},"Cancelled runtime signalled event");rejects([&]{clear(rt,id);},"Cancelled runtime cleared event");
    need(WaitForSingleObject(object->native,0)==WAIT_TIMEOUT,"Cancellation fabricated an event signal");close(rt,id);
}
}
int main(int argc,char** argv) {
    try {
        need(argc==2,"Expected original image path");Runtime rt;rt.load(argv[1]);rt.map(0x10000,0x20000,true,"native event fixture");
        rt.map(0x40000,0x1000,false,"native event readonly fixture");auto* base=rt.base;
        PPC_STORE_U64(poll,0);PPC_STORE_U64(bounded,uint64_t(-20000000ll));const auto initial=rt.handles.size();
        states(rt);rejection(rt);concurrentClose(rt);originalOwner(rt);cancellation(rt);
        need(rt.handles.size()==initial,"Native event fixtures leaked registry handles");
        std::printf("PASS native events: %zu checks; actual auto/manual state, prior state, close/wait leases, cancellation and original owner lifecycle\n",checks);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL native events: %s\n",error.what());return 1;}
}
