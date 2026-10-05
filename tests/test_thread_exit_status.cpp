// Links the production runtime and original AOT image. Only the worker entry
// is a fixture callback; 82433328, 8232B0A8 and 82432CC8 run their actual bodies.
#include "runtime/threads.h"
#include "runtime/engine_cpu_calls.h"
#include "ppc_recomp_shared.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <thread>

void SimpsonsNativeThreadExitStatus(PPCContext&,uint8_t*);

namespace {
using namespace Simpsons;
std::atomic<size_t> checks=0;
constexpr uint32_t buffer=0x30000,output=buffer+0x10,wrapper=buffer+0x20,descriptor=buffer+0x40;
constexpr uint32_t originalQuery=0x82433328,originalPoll=0x8232B0A8;
constexpr uint32_t marker=0xA17EBEEF;
void need(bool value,const char* why){++checks;if(!value)throw Failure(why);}
template<class F>void rejects(F&& f,const char* reason){
    try{f();}catch(const Failure& e){need(std::string(e.what()).find(reason)!=std::string::npos,"Unexpected rejection reason");return;}
    need(false,"Expected explicit failure did not occur");
}
struct Event {
    HANDLE native=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Event(){need(native!=nullptr,"Fixture event creation failed");}
    ~Event(){CloseHandle(native);}
    void signal(){need(SetEvent(native)!=FALSE,"Fixture event signal failed");}
    void wait(){need(WaitForSingleObject(native,10000)==WAIT_OBJECT_0,"Fixture event timed out");}
};
struct Plan {
    uint32_t status{};
    bool terminate{},tlsBarrier{},gate{};
    Event entered,release,tlsEntered,tlsRelease;
    std::atomic<bool> tlsFailed=false;
};
std::array<std::unique_ptr<Plan>,64> plans;
uint32_t nextPlan=0;
struct ExitBarrier {
    Plan* plan{};
    ~ExitBarrier(){if(plan){SetEvent(plan->tlsEntered.native);if(WaitForSingleObject(plan->tlsRelease.native,10000)!=WAIT_OBJECT_0)plan->tlsFailed=true;}}
};
thread_local ExitBarrier exitBarrier;

PPC_FUNC(workerEntry){
    need(ctx.r3.u32<plans.size() && plans[ctx.r3.u32]!=nullptr,"Unknown fixture worker plan");
    auto& plan=*plans[ctx.r3.u32];
    need(currentContext==&ctx && base==active->base,"Production worker did not install its context");
    if(plan.tlsBarrier)exitBarrier.plan=&plan;
    plan.entered.signal();
    if(plan.gate){
        HANDLE waits[]={plan.release.native,active->stopEvent};
        need(WaitForMultipleObjects(2,waits,FALSE,10000)==WAIT_OBJECT_0,"Gated worker was not released");
    }
    ctx.r3.u64=plan.status;
    if(plan.terminate){__imp__ExTerminateThread(ctx,base);need(false,"ExTerminateThread returned instead of unwinding");}
}

struct Worker {
    Runtime& rt;
    Plan& plan;
    uint32_t handle{},object{},pcr{},stack{};
    std::shared_ptr<KernelHandle> owner;
    GuestThread* record{};
    Worker(Runtime& runtime,uint32_t status,bool suspended=false,bool gated=false,bool terminate=false,bool tls=false)
        :rt(runtime),plan(*newPlan(status,gated,terminate,tls)){
        PPCContext c{};c.r3.u64=buffer+0x100;c.r5.u64=buffer+0x104;
        c.r7.u64=PPC_CODE_BASE;c.r8.u64=nextPlan-1;c.r9.u64=suspended?1:0;
        need(rt.createThread(c)==0,"Production thread creation failed");
        auto* base=rt.base;handle=PPC_LOAD_U32(buffer+0x100);owner=rt.getHandle(handle);
        need(bool(owner),"Created thread handle is missing");object=owner->guestObject;
        std::lock_guard lock(rt.threadMutex);
        for(const auto& candidate:rt.threads)if(candidate->object==owner){record=candidate.get();pcr=record->pcr;stack=record->stack;}
        need(record!=nullptr,"Created thread registry owner missing");
    }
    static Plan* newPlan(uint32_t status,bool gate,bool terminate,bool tls){
        need(nextPlan<plans.size(),"Too many fixture plans");
        auto p=std::make_unique<Plan>();p->status=status;p->gate=gate;p->terminate=terminate;p->tlsBarrier=tls;
        auto* result=p.get();plans[nextPlan++]=std::move(p);return result;
    }
    Worker(const Worker&)=delete;
    ~Worker(){
        SetEvent(plan.release.native);SetEvent(plan.tlsRelease.native);
        if(owner && WaitForSingleObject(owner->native,0)!=WAIT_OBJECT_0){
            DWORD previous;do{previous=ResumeThread(owner->native);}while(previous!=DWORD(-1)&&previous>1);
            if(WaitForSingleObject(owner->native,10000)!=WAIT_OBJECT_0)std::terminate();
        }
        if(rt.getHandle(handle))rt.closeHandle(handle);
    }
    void resume(){PPCContext c{};c.r3.u64=handle;__imp__NtResumeThread(c,rt.base);need(c.r3.u32==0,"Original native resume failed");}
    void finish(){
        plan.release.signal();plan.tlsRelease.signal();
        need(WaitForSingleObject(owner->native,10000)==WAIT_OBJECT_0,"Worker did not fully terminate");
        DWORD code=0;need(GetExitCodeThread(owner->native,&code)!=FALSE&&code==plan.status,"Actual native worker status differs");
        need(!rt.stopping && !plan.tlsFailed,"Worker failure or TLS barrier timeout");
    }
};

void pinWords(Runtime& rt,uint32_t address,const char* hex){
    auto* base=rt.base;const size_t length=std::strlen(hex);need(length%8==0,"Invalid fixture instruction pin");
    auto nibble=[](char c)->uint32_t{return c>='0'&&c<='9'?uint32_t(c-'0'):uint32_t(c-'a'+10);};
    for(size_t off=0;off<length;off+=8){uint32_t word=0;for(size_t j=0;j<8;++j)word=(word<<4)|nibble(hex[off+j]);
        need(PPC_LOAD_U32(address+uint32_t(off/2))==word,"Original thread instruction/data pin changed");}
}
void pins(Runtime& rt){
    // Whole original helper, including exactly six replaced words.
    pinWords(rt,0x82433328,"7d8802a69181fff8fbe1fff09421ff903d60820038a100507c9f2378808b07bc4888f69d2c0300004180003080610050"
        "81630004556b063f4182000c816301404800000839600103917f00004888f631386000014800000c48001b4138600000"
        "382100708181fff87d8803a6ebe1fff04e800020");
    pinWords(rt,0x8232B0A8,"7d8802a69181fff8fbe1fff09421ff907c7f1b787c2004ac817f0000280b000041820064814b00002b0a0000419a0044"
        "388100505543003e481082492c03000041820030816100502b0b0103409a000c3860000148000034817f0000806b0000"
        "48107bc1817f000039400000914b0000817f00003940000238600002914b00084800000838600000382100708181fff8"
        "7d8803a6ebe1fff04e800020");
    pinWords(rt,0x82432CC8,"7d8802a69181fff89421ffa03d6082cd816b2760816b00047d6903a64e8004212c0300004180000c386000014800000c"
        "48000ea138600000382100608181fff87d8803a64e800020");
    pinWords(rt,0x82433B98,"7d8802a69181fff89421ffa04888edc1816d01502b0b0000409a000c816d0100906b0160382100608181fff8"
        "7d8803a64e800020");
    pinWords(rt,0x82434EC0,"4bffecd8");
    // Read-only original table selects the real NtClose import; no fake close.
    pinWords(rt,0x82CD2760,"82cd2734");pinWords(rt,0x82CD2734,"0000000082cc28c4");
    pinWords(rt,0x82375D28,"807f00244bfb537d2f030001419aff7c");
    pinWords(rt,0x824394B4,"807f00504888979d");
}

uint32_t refCount(Runtime& rt,uint32_t object){
    std::lock_guard lock(rt.handleMutex);const auto it=rt.objectReferences.find(object);
    return it==rt.objectReferences.end()?0:it->second.count;
}
void dereference(Runtime& rt,uint32_t object){PPCContext c{};c.r3.u64=object;__imp__ObDereferenceObject(c,rt.base);need(c.r3.u64==0,"Production dereference failed");}
PPCContext retainedContext(Runtime& rt,const PPCContext& initial,uint32_t handle,uint32_t sp=0x17000){
    PPCContext c{};std::memset(&c,0xA5,sizeof(c));
    c.r1.u64=sp;c.r13=initial.r13;c.r3.u64=handle;c.r4.u64=Runtime::threadObjectType;c.r5.u64=uint64_t(sp)+0x50;c.lr=0x8243334C;
    __imp__ObReferenceObjectByHandle(c,rt.base);need(c.r3.u64==0,"Typed original reference failed");
    auto* base=rt.base;c.r3.u64=PPC_LOAD_U32(sp+0x50);return c;
}
void directQuery(Runtime& rt,PPCContext c,uint32_t status,bool signaled){
    auto* base=rt.base;PPCContext* previous=currentContext;currentContext=&c;
    struct Restore{PPCContext* previous;uint32_t fp;DWORD error;~Restore(){currentContext=previous;PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}}
        restore{previous,PPCFPSCRRegister::getcsr(),GetLastError()};
    const uint32_t object=c.r3.u32,count=refCount(rt,object);
    std::array<uint8_t,0x200> objectBefore{};std::memcpy(objectBefore.data(),rt.pointer(object,0x200,false),objectBefore.size());
    std::array<uint8_t,0x100> stackBefore{};std::memcpy(stackBefore.data(),rt.pointer(c.r1.u32,0x100,false),stackBefore.size());
    PPC_STORE_U32(output,marker);
    for(uint8_t so:{uint8_t(0),uint8_t(1)})for(uint32_t fp:{0x1F80u,0x3FC0u,0x5F80u,0x9FC0u,0xE07Fu}){
        c.xer={so,1,1};PPCContext expected;std::memcpy(&expected,&c,sizeof(c));
        expected.r11.u64=status;expected.cr0.lt=0;expected.cr0.gt=uint8_t(signaled);expected.cr0.eq=uint8_t(!signaled);expected.cr0.so=so;
        SetLastError(0xC0FFEE42);PPCFPSCRRegister::restoreHostCSR(fp);
        SimpsonsNativeThreadExitStatus(c,base);
        const auto resultFp=PPCFPSCRRegister::getcsr();const auto resultError=GetLastError();
        PPCFPSCRRegister::restoreHostCSR(restore.fp);
        need(!std::memcmp(&c,&expected,sizeof(c)),"Bridge changed guest state beyond r11/CR0 or lost XER.SO");
        need(resultFp==fp && resultError==0xC0FFEE42,"Bridge changed host floating-point/last-error state");
        need(refCount(rt,object)==count,"Bridge consumed a counted reference");
        need(PPC_LOAD_U32(output)==marker,"Bridge stored caller output before the original instruction");
        need(!std::memcmp(objectBefore.data(),rt.pointer(object,0x200,false),objectBefore.size()),"Bridge wrote guest KTHREAD fields");
        need(!std::memcmp(stackBefore.data(),rt.pointer(c.r1.u32,0x100,false),stackBefore.size()),"Bridge changed original stack image");
    }
}
void query(Runtime& rt,const PPCContext& initial,uint32_t handle,uint32_t status,bool signaled,uint32_t out=output){
    auto* base=rt.base;PPC_STORE_U32(out,marker);
    for(uint8_t so:{uint8_t(0),uint8_t(1)}){
        PPCContext c=initial;c.r31.u64=0xDEADBEEFF00DFACE;c.xer={so,1,1};c.lr=0x12345678;
        EngineCpuCalls cpu(c,base);const auto saved=cpu.registers();
        need(cpu.invoke(originalQuery,handle,out)==1,"Original exit-status helper did not return Boolean success");
        const auto& after=cpu.registers();need(PPC_LOAD_U32(out)==status,"Original helper stored wrong exit-code bits");
        need(after.r1.u64==saved.r1.u64&&after.r31.u64==saved.r31.u64&&after.r13.u64==saved.r13.u64&&after.lr==saved.lr,
            "Original exit-status helper changed nonvolatile frame/LR/PCR");
        need(after.cr0.lt==0&&after.cr0.gt==uint8_t(signaled)&&after.cr0.eq==uint8_t(!signaled)&&after.cr0.so==so,
            "Original helper lost completion-state CR0/SO");
        need(after.xer.so==so&&after.xer.ov==1&&after.xer.ca==1,"Original query changed XER");
    }
}
void poll(Runtime& rt,const PPCContext& initial,Worker& worker,bool final){
    auto* base=rt.base;PPC_STORE_U32(wrapper,descriptor);PPC_STORE_U32(descriptor,worker.handle);PPC_STORE_U32(descriptor+8,0x31415926);
    EngineCpuCalls cpu(initial,base);
    need(cpu.invoke(originalPoll,wrapper)==(final?2u:1u),"Original poll result differs");
    need(PPC_LOAD_U32(descriptor)==(final?0u:worker.handle)&&PPC_LOAD_U32(descriptor+8)==(final?2u:0x31415926u),
        "Original poll handle/state stores differ");
    need(bool(rt.getHandle(worker.handle))!=final,"Original poll closed a running thread or failed to close a completed thread");
    need(refCount(rt,worker.object)==0,"Original poll leaked a reference");
    if(final){
        need(cpu.invoke(originalPoll,wrapper)==2&&PPC_LOAD_U32(descriptor)==0,"Second original poll changed settled state");
        need(rt.closeHandle(worker.handle)==0xC0000008,"Completed handle remained closable after original poll");
    }
}

void ordinaryCases(Runtime& rt,const PPCContext& initial){
    for(uint32_t status:{0u,42u,0xFFFFFFFFu,259u})for(bool terminate:{false,true}){
        Worker w(rt,status,true,true,terminate);
        auto direct=retainedContext(rt,initial,w.handle);directQuery(rt,direct,259,false);dereference(rt,w.object);
        query(rt,initial,w.handle,259,false);poll(rt,initial,w,false);
        need(WaitForSingleObject(w.plan.entered.native,0)==WAIT_TIMEOUT,"Status query resumed a suspended worker");
        w.resume();w.plan.entered.wait();query(rt,initial,w.handle,259,false);poll(rt,initial,w,false);
        w.finish();
        direct=retainedContext(rt,initial,w.handle);directQuery(rt,direct,status,true);dereference(rt,w.object);
        query(rt,initial,w.handle,status,true);need(refCount(rt,w.object)==0,"Original complete query reference unbalanced");
        poll(rt,initial,w,status!=259);
        if(status==259)need(WaitForSingleObject(w.owner->native,0)==WAIT_OBJECT_0,"Real exit 259 was not signaled");
    }
}
void tlsCases(Runtime& rt,const PPCContext& initial){
    for(bool terminate:{false,true}){
        Worker w(rt,42,false,false,terminate,true);w.plan.tlsEntered.wait();
        need(w.record->finished.load()&&WaitForSingleObject(w.owner->native,0)==WAIT_TIMEOUT,
            "TLS fixture did not expose finished-before-native-termination interval");
        query(rt,initial,w.handle,259,false);poll(rt,initial,w,false);
        auto c=retainedContext(rt,initial,w.handle);directQuery(rt,c,259,false);dereference(rt,w.object);
        w.finish();query(rt,initial,w.handle,42,true);poll(rt,initial,w,true);
    }
}
void selfCases(Runtime& rt,const PPCContext& initial){
    query(rt,initial,0xFFFFFFFE,259,false);
    auto c=retainedContext(rt,initial,0xFFFFFFFE);directQuery(rt,c,259,false);dereference(rt,rt.threadAddress);
    const uint32_t self=rt.addHandle(rt.mainThreadHandle);
    query(rt,initial,self,259,false);need(rt.closeHandle(self)==0,"Real main-thread handle close failed");
    need(WaitForSingleObject(rt.mainThreadHandle->native,0)==WAIT_TIMEOUT&&refCount(rt,rt.threadAddress)==0,"Self query changed native ownership");
}
void referenceErrors(Runtime& rt,const PPCContext& initial){
    auto* base=rt.base;Worker w(rt,0);w.finish();
    const uint32_t closed=rt.addHandle(w.owner);need(rt.closeHandle(closed)==0,"Closed-handle fixture failed");
    for(uint32_t handle:{0u,0x12345678u,closed}){
        PPC_STORE_U32(output,marker);PPC_STORE_U32(rt.threadAddress+0x160,marker);
        EngineCpuCalls cpu(initial,base);
        need(cpu.invoke(originalQuery,handle,output)==0,"Invalid/closed handle was reported successful");
        need(PPC_LOAD_U32(output)==marker&&PPC_LOAD_U32(rt.threadAddress+0x160)==ERROR_INVALID_HANDLE,"Original reference error/output behavior differs");
        need(refCount(rt,w.object)==0,"Failed original reference retained an object");
    }
    // Wrong requested thread type follows the preserved NTSTATUS conversion.
    rt.bindData(0x820007BC,Runtime::threadObjectType+4);
    {EngineCpuCalls cpu(initial,base);PPC_STORE_U32(output,marker);need(cpu.invoke(originalQuery,w.handle,output)==0,"Wrong requested object type succeeded");}
    rt.bindData(0x820007BC,Runtime::threadObjectType);
    need(PPC_LOAD_U32(output)==marker&&refCount(rt,w.object)==0,"Wrong-type reference changed output/ownership");
    auto event=std::make_shared<KernelHandle>(CreateEventW(nullptr,TRUE,FALSE,nullptr),KernelHandle::Type::Event);
    need(event->native!=nullptr,"Wrong-object event creation failed");const auto eventHandle=rt.addHandle(event);
    {EngineCpuCalls cpu(initial,base);rejects([&]{cpu.invoke(originalQuery,eventHandle,output);},"Guest object references for this native handle type");}
    need(PPC_LOAD_U32(output)==marker&&refCount(rt,w.object)==0,"Non-thread reference produced a success output");rt.closeHandle(eventHandle);
    // Output-store faults happen AFTER a successful original ObReference and
    // BEFORE its dereference. Keep that real fault order; fixture balances it.
    rt.map(0x50000,0x1000,false,"thread-status readonly output");
    // Guest pages below 0x10000 demand-map as zero-filled null-device scratch,
    // so a null output no longer faults; 0x20000 lies between the fixture maps.
    for(uint32_t out:{0x20000u,0x40000u,0x50000u,0x33FFFu}){
        EngineCpuCalls cpu(initial,base);
        rejects([&]{cpu.invoke(originalQuery,w.handle,out);},"Unmapped/protected guest write");
        need(refCount(rt,w.object)==1,"Original output fault skipped reference or performed dereference early");dereference(rt,w.object);
    }
}

void rejectsUnchanged(Runtime& rt,PPCContext c,const char* reason,uint8_t* base){
    PPCContext* previous=currentContext;currentContext=&c;
    const auto savedFp=PPCFPSCRRegister::getcsr();const auto savedError=GetLastError();
    PPCContext before;std::memcpy(&before,&c,sizeof(c));
    SetLastError(marker);PPCFPSCRRegister::restoreHostCSR(0x9FC0);
    bool rejected=false,expectedReason=false;
    try{SimpsonsNativeThreadExitStatus(c,base);}catch(const Failure& e){rejected=true;expectedReason=std::string(e.what()).find(reason)!=std::string::npos;}
    const auto error=GetLastError();const auto fp=PPCFPSCRRegister::getcsr();
    currentContext=previous;PPCFPSCRRegister::restoreHostCSR(savedFp);SetLastError(savedError);
    need(rejected&&expectedReason,"Missing/wrong bridge failure");
    need(!std::memcmp(&before,&c,sizeof(c))&&error==marker&&fp==0x9FC0,"Rejected bridge changed guest or host state");
    need(refCount(rt,c.r3.u32)<=1,"Bridge failure added references");
}
void bridgeErrors(Runtime& rt,const PPCContext& initial){
    Worker w(rt,42);w.finish();auto c=retainedContext(rt,initial,w.handle);
    auto bad=c;bad.lr=0;rejectsUnchanged(rt,bad,"original typed reference",rt.base);
    bad=c;bad.r1.u64|=1;rejectsUnchanged(rt,bad,"original typed reference",rt.base);
    bad=c;bad.r4.u64=0;rejectsUnchanged(rt,bad,"original typed reference",rt.base);
    bad=c;bad.r5.u64+=4;rejectsUnchanged(rt,bad,"original typed reference",rt.base);
    bad=c;bad.r3.u64|=0x100000000ull;rejectsUnchanged(rt,bad,"original typed reference",rt.base);
    rejectsUnchanged(rt,c,"runtime/context",nullptr);
    {auto* saved=currentContext;currentContext=nullptr;rejects([&]{SimpsonsNativeThreadExitStatus(c,rt.base);},"runtime/context");currentContext=saved;}
    dereference(rt,w.object);rejectsUnchanged(rt,c,"counted, matching Thread",rt.base);
    // Invalid counted-reference metadata must fail without querying an event
    // or inventing a terminal thread status. No such owners enter production.
    const auto invalid=std::make_shared<KernelHandle>(nullptr,KernelHandle::Type::Thread);
    invalid->guestObject=w.object;
    for(unsigned scenario=0;scenario<5;++scenario){
        invalid->guestObject=scenario==2?w.object+4:w.object;
        invalid->type=scenario==3?KernelHandle::Type::Event:KernelHandle::Type::Thread;
        {std::lock_guard lock(rt.handleMutex);rt.objectReferences[w.object]={scenario==0?nullptr:invalid,scenario==1?0u:1u};}
        rejectsUnchanged(rt,c,scenario==4?"no native thread handle":"counted, matching Thread",rt.base);
        {std::lock_guard lock(rt.handleMutex);rt.objectReferences.erase(w.object);}
    }
    // Real reduced-access duplicated THREAD handles make Windows fail: first
    // the wait lacks SYNCHRONIZE, then a signaled wait lacks query permission.
    for(DWORD access:{DWORD(THREAD_QUERY_LIMITED_INFORMATION),DWORD(SYNCHRONIZE)}){
        HANDLE native{};need(DuplicateHandle(GetCurrentProcess(),w.owner->native,GetCurrentProcess(),&native,access,FALSE,0)!=FALSE,
            "Restricted native thread duplicate failed");
        auto restricted=std::make_shared<KernelHandle>(native,KernelHandle::Type::Thread);restricted->guestObject=w.object;
        const auto handle=rt.addHandle(restricted);auto queryContext=retainedContext(rt,initial,handle);
        rejectsUnchanged(rt,queryContext,access==SYNCHRONIZE?"GetExitCodeThread":"WaitForSingleObject",rt.base);
        need(refCount(rt,w.object)==1,"Native-query failure consumed original reference");dereference(rt,w.object);rt.closeHandle(handle);
    }
}

void closeRace(Runtime& rt,const PPCContext& initial){
    Worker w(rt,42,false,true);w.plan.entered.wait();
    // Each caller acquires its real counted reference before the close. Their
    // queries proceed concurrently after the guest map entry has disappeared.
    Event ready,closed,activeDone;std::atomic<unsigned> arrived=0,activeFinished=0;std::array<std::exception_ptr,2> errors{};
    auto queryThread=[&](unsigned index){
        uint32_t object=0;
        try{
            auto c=retainedContext(rt,initial,w.handle,0x18000+index*0x2000);object=c.r3.u32;
            currentContext=&c;if(arrived.fetch_add(1)==1)ready.signal();closed.wait();
            for(unsigned iteration=0;iteration<256;++iteration){
                c.xer.so=uint8_t(iteration&1);SimpsonsNativeThreadExitStatus(c,rt.base);
                need(c.r11.u64==259&&c.cr0.eq&&!c.cr0.gt&&c.cr0.so==(iteration&1),"Concurrent active query changed result/CR0");
            }
            if(activeFinished.fetch_add(1)==1)activeDone.signal();
            need(WaitForSingleObject(w.owner->native,10000)==WAIT_OBJECT_0,"Concurrent caller did not observe final native termination");
            for(unsigned iteration=0;iteration<256;++iteration){
                SimpsonsNativeThreadExitStatus(c,rt.base);need(c.r11.u64==42&&c.cr0.gt&&!c.cr0.eq,"Concurrent completed query changed result/CR0");
            }
        }catch(...){errors[index]=std::current_exception();ready.signal();activeDone.signal();}
        if(object)try{dereference(rt,object);}catch(...){errors[index]=std::current_exception();}
        currentContext=nullptr;
    };
    std::jthread a(queryThread,0),b(queryThread,1);
    ready.wait();need(arrived==2,"Concurrent callers failed before retained references");
    need(rt.closeHandle(w.handle)==0&&!rt.getHandle(w.handle)&&refCount(rt,w.object)==2,"Guest close invalidated counted owners");
    closed.signal();
    // The worker cannot exit before both active-query loops finish. No delay
    // or assumed scheduler speed determines the expected status.
    activeDone.wait();need(activeFinished==2,"Concurrent active-query loop failed");
    w.finish();a.join();b.join();
    for(const auto& error:errors)if(error)std::rethrow_exception(error);
    need(refCount(rt,w.object)==0,"Concurrent queries retained extra references");
}

void collect(Runtime& rt){PPCContext c{};need(rt.createThread(c)==0xC000000D,"Collection probe accepted an absent worker entry");}
void reaping(Runtime& rt,const PPCContext& initial){
    collect(rt);need(rt.threads.empty(),"Earlier completed test workers were retained");
    uint32_t firstPcr=0,firstStack=0,object=0;std::shared_ptr<KernelHandle> lease;
    {
        Worker w(rt,0);w.finish();firstPcr=w.pcr;firstStack=w.stack;object=w.object;
        auto c=retainedContext(rt,initial,w.handle);need(rt.closeHandle(w.handle)==0,"Retained reaping fixture close failed");
        directQuery(rt,c,0,true);lease=w.owner;
    }
    collect(rt);need(rt.threads.size()==1&&refCount(rt,object)==1,"Counted reference did not retain KTHREAD");
    dereference(rt,object);collect(rt);need(rt.threads.size()==1,"In-flight shared owner did not retain KTHREAD");
    lease.reset();collect(rt);need(rt.threads.empty()&&refCount(rt,object)==0,"Final shared release did not allow reaping");
    {Worker reused(rt,42,true);need(reused.pcr==firstPcr&&reused.stack==firstStack,"Reaped thread slots were not reused");
        reused.resume();reused.finish();query(rt,initial,reused.handle,42,true);poll(rt,initial,reused,true);}
    collect(rt);need(rt.threads.empty()&&rt.handles.empty()&&rt.objectReferences.empty()&&rt.tlsBases.size()==1,"Thread-status tests leaked owned resources");
}
}

int main(int argc,char** argv){
    try{
        need(argc==2,"Original image path required");Runtime rt;rt.load(argv[1]);PPCContext initial{};rt.initialize(initial);
        rt.map(0x10000,0x10000,true,"thread-exit direct/concurrent stacks");rt.map(buffer,0x4000,true,"thread-exit fixture outputs");
        pins(rt);
        // Dispatch for the controlled native worker only. Original query/poll/
        // close and their imports retain the image's actual compiled mappings.
        auto* base=rt.base;PPC_LOOKUP_FUNC(base,PPC_CODE_BASE)=workerEntry;
        selfCases(rt,initial);ordinaryCases(rt,initial);tlsCases(rt,initial);
        referenceErrors(rt,initial);bridgeErrors(rt,initial);closeRace(rt,initial);reaping(rt,initial);
        auto self=retainedContext(rt,initial,0xFFFFFFFE);rt.requestStop("thread-status tests complete");
        rejectsUnchanged(rt,self,"thread-status tests complete",base);dereference(rt,rt.threadAddress);
        std::printf("PASS native thread-exit status: %zu checks; original query/poll/close/error bodies, real workers, TLS barrier, races and ownership\n",checks.load());
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL native thread-exit status: %s (%zu checks)\n",error.what(),checks.load());return 1;}
}
