// Standalone: production threads.cpp/thread_objects.cpp/kernel_objects.cpp,
// actual Runtime/PPC declarations, real Win32 threads and AOT fixture dispatch.
#include "runtime/threads.h"
#include "runtime/thread_placement.h"
#include "runtime/native_window.h"
#include "ppc_image_metadata.h"
#include <algorithm>
#include <thread>
#include <utility>

#ifndef SIMPSONS_THREAD_LIFECYCLE_STANDALONE
#error "Use SIMPSONS_THREAD_LIFECYCLE_STANDALONE; do not link SimpsonsRuntime"
#endif

PPC_EXTERN_FUNC(__imp__NtResumeThread);
PPC_EXTERN_FUNC(__imp__ObReferenceObjectByHandle);
PPC_EXTERN_FUNC(__imp__ObDereferenceObject);

namespace {
constexpr uint32_t output=0x10000,idOutput=0x10004,referenceOutput=0x10008;
constexpr uint32_t firstArea=0x01040000,firstStack=0x02100000,areaSize=0x4000;
constexpr uint32_t delayedExit=0xf00d0001,waitWorker=0xf00d0002;
std::atomic<unsigned> executions=0;
std::atomic<uint32_t> failPointer=0,failGuestPointer=0;
std::atomic<int> failMap=-1;
bool resumeBeforeFailure=false;
HANDLE exitReached{},exitRelease{},workerReached{},workerRelease{};
std::atomic<bool> exitWaitFailed=false;
void require(bool ok,const char* message) {if(!ok) throw Simpsons::Failure(message);}
struct HostEvent {
    HANDLE value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HostEvent() {require(value!=nullptr,"fixture event creation failed");}
    ~HostEvent() {CloseHandle(value);}
};
struct ExitBarrier {
    bool enabled=false;
    ~ExitBarrier() {
        if(enabled) {
            SetEvent(exitReached);
            if(WaitForSingleObject(exitRelease,10000)!=WAIT_OBJECT_0) exitWaitFailed=true;
        }
    }
};
thread_local ExitBarrier exitBarrier;
}

namespace Simpsons {
Runtime* active{};
thread_local PPCContext* currentContext{};
NativeWindow::~NativeWindow()=default;
Runtime::Runtime() {
    require(!active,"fixture already active");
    base=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x100000000ull,MEM_RESERVE,PAGE_NOACCESS));
    require(base!=nullptr,"fixture guest reserve failed");
    stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    require(stopEvent!=nullptr,"fixture stop event failed");active=this;
}
Runtime::~Runtime() {
    stopThreads();objectReferences.clear();handles.clear();threads.clear();mainThreadHandle.reset();
    CloseHandle(stopEvent);VirtualFree(base,0,MEM_RELEASE);currentContext=nullptr;active=nullptr;
}
uint64_t Runtime::committedPages() const {
    uint64_t pages=0;
    for(const auto& region:regions) if(region.use!=MemoryUse::Host) pages+=region.size/4096;
    for(const auto& allocation:allocations)
        pages+=std::count(allocation.committed.begin(),allocation.committed.end(),uint8_t(1));
    return pages;
}
void Runtime::map(uint32_t address,uint64_t size,bool write,const char* name,MemoryUse use) {
    require(address>=0x10000 && size && !(address&4095) && !(size&4095) && uint64_t(address)+size<=0x100000000ull,
            "fixture invalid mapping");
    for(const auto& region:regions)
        require(!(address<uint64_t(region.address)+region.size && region.address<uint64_t(address)+size),"fixture mapping overlap");
    require(VirtualAlloc(base+address,size,MEM_COMMIT,write?PAGE_READWRITE:PAGE_READONLY)==base+address,"fixture commit failed");
    // Exercise map failure AFTER host commit but BEFORE Region publication.
    int expected=int(use);
    if(failMap.compare_exchange_strong(expected,-1)) throw Failure("injected mapping publication failure");
    regions.push_back({address,size,write,name,use});
    for(uint64_t pos=address;pos<uint64_t(address)+size;pos+=4096) pageAccess[pos>>12]=write?3:1;
    invalidateWatchedPages(address,size);
}
uint8_t* Runtime::pointer(uint32_t address,unsigned width,bool write) {
    uint32_t expected=address;
    if(address && failPointer.compare_exchange_strong(expected,0)) throw Failure("injected initialization failure");
    require(width && address>=0x10000 && uint64_t(address)+width<=0x100000000ull,"fixture guest extent invalid");
    for(uint64_t page=address>>12;page<=(uint64_t(address)+width-1)>>12;++page) {
        uint8_t access=pageAccess[page].load();
        require((access&1) && (!write || (access&2)),"fixture unmapped/protected guest memory");
    }
    return base+address;
}
uint32_t runThreadEntry(PPCContext& ctx,uint8_t* base,uint32_t address) {
    PPC_CALL_INDIRECT_FUNC(address);return ctx.r3.u32;
}
}
uint8_t* PPCGuestPointer(uint8_t* base,uint32_t address,unsigned width,bool write) {
    auto& rt=*Simpsons::active;require(base==rt.base,"wrong fixture base");rt.checkRunning();
    uint32_t expected=address;
    if(address && failGuestPointer.compare_exchange_strong(expected,0)) {
        if(resumeBeforeFailure) {
            require(!rt.threads.empty(),"failure injection preceded registry publication");
            require(ResumeThread(rt.threads.back()->native)==1,"early fixture resume failed");
        }
        throw Simpsons::Failure("injected final output failure");
    }
    return rt.pointer(address,width,write);
}
[[noreturn]] void PPCRecompFailure(const PPCContext&,uint32_t,const char* reason) {throw Simpsons::Failure(reason);}

namespace {
PPC_FUNC(fixtureWorker) {
    auto& rt=*Simpsons::active;
    require(Simpsons::currentContext==&ctx,"worker context not installed");
    uint32_t object=PPC_LOAD_U32(ctx.r13.u32+0x100);
    require(PPC_LOAD_U32(object+0x14c)==GetCurrentThreadId(),"worker ID not native ID");
    uint32_t tls=PPC_LOAD_U32(ctx.r13.u32),dynamic=PPC_LOAD_U32(object+0x68);
    require(PPC_LOAD_U32(tls)==0x1234abcd,"static TLS not initialized");
    for(uint32_t slot=0;slot<kTls_slot_count;++slot)
        require(PPC_LOAD_U32(dynamic+slot*4)==0,"reused dynamic TLS was not zero");
    PPC_STORE_U32(dynamic,0xfeedbeef);
    PPC_STORE_U32(ctx.r1.u32,ctx.r3.u32); // Real guest stack backed by native committed pages.
    ++executions;
    if(ctx.r3.u32==delayedExit) exitBarrier.enabled=true;
    if(ctx.r3.u32==waitWorker) {
        SetEvent(workerReached);
        HANDLE handles[]={workerRelease,rt.stopEvent};
        require(WaitForMultipleObjects(2,handles,FALSE,10000)==WAIT_OBJECT_0,"fixture worker wait failed");
    }
    ctx.r3.u32=42;
}
void prepare(Simpsons::Runtime& rt) {
    auto* base=rt.base;
    rt.map(0x10000,0x1000,true,"fixture output");
    uint32_t dispatch=uint32_t(PPC_IMAGE_BASE+PPC_IMAGE_SIZE);
    // Only the first fixture dispatch slot is needed, not the original game.
    rt.map(dispatch,0x1000,true,"fixture AOT dispatch",Simpsons::MemoryUse::Host);
    PPC_LOOKUP_FUNC(base,PPC_CODE_BASE)=fixtureWorker;
    uint32_t tlsPage=kTls_raw_data_address&~0xfffu;
    uint64_t tlsExtent=(uint64_t(kTls_raw_data_address-tlsPage)+kTls_raw_data_size+4095)&~4095ull;
    rt.map(tlsPage,tlsExtent,true,"fixture static TLS");
    require(kTls_raw_data_size>=4 && kTls_slot_count>0,"fixture requires target TLS metadata");
    PPC_STORE_U32(kTls_raw_data_address,0x1234abcd);
}
struct Worker {uint32_t handle,object,pcr,tls,stack;HANDLE native,gate,guestNative;};
Worker create(Simpsons::Runtime& rt,uint32_t flags=0,uint32_t argument=0,uint32_t out=output,uint32_t requestedStack=0) {
    PPCContext ctx{};ctx.r3.u32=out;ctx.r4.u32=requestedStack;ctx.r5.u32=out+4;
    ctx.r7.u32=uint32_t(PPC_CODE_BASE);ctx.r8.u32=argument;ctx.r9.u32=flags;
    require(rt.createThread(ctx)==0,"native fixture create failed");
    uint8_t* base=rt.base;uint32_t handle=PPC_LOAD_U32(out);
    auto object=rt.getHandle(handle);require(bool(object),"created guest handle missing");
    std::lock_guard lock(rt.threadMutex);
    for(const auto& thread:rt.threads) if(thread->object==object) {
        require(PPC_LOAD_U32(out+4)==thread->id,"created ID output mismatch");
        return {handle,thread->thread,thread->pcr,thread->tls,thread->stack,thread->native,thread->startEvent,object->native};
    }
    throw Simpsons::Failure("created thread not in registry");
}
void finish(Simpsons::Runtime& rt,const Worker& worker,bool resume=false) {
    auto object=rt.getHandle(worker.handle);require(bool(object),"wait handle missing");
    if(resume) {
        PPCContext ctx{};ctx.r3.u32=worker.handle;__imp__NtResumeThread(ctx,rt.base);
        require(ctx.r3.u32==0,"fixture resume failed");
    }
    require(WaitForSingleObject(object->native,10000)==WAIT_OBJECT_0,"native worker did not terminate");
    DWORD code=0;require(GetExitCodeThread(object->native,&code) && code==42,"native worker exit code mismatch");
    require(!rt.stopping,"fixture worker requested shutdown");
}
void collect(Simpsons::Runtime& rt) {
    PPCContext ctx{}; // Reaping precedes rejecting the intentionally absent entry.
    require(rt.createThread(ctx)==0xc000000d,"invalid-entry collection probe accepted");
}
size_t regionCount(Simpsons::Runtime& rt) {std::lock_guard lock(rt.vmMutex);return rt.regions.size();}
void emptyRegistry(Simpsons::Runtime& rt,size_t baseRegions) {
    collect(rt);
    require(rt.threads.empty() && rt.tlsBases.empty() && rt.handles.empty() && rt.objectReferences.empty(),"thread ownership/TLS registry leaked");
    require(regionCount(rt)==baseRegions,"thread guest mappings leaked");
}
void requireDecommitted(Simpsons::Runtime& rt,uint32_t address) {
    MEMORY_BASIC_INFORMATION info{};
    require(VirtualQuery(rt.base+address,&info,sizeof(info)) && info.State==MEM_RESERVE,"rollback left native committed pages");
    require(rt.pageAccess[address>>12]==0,"rollback retained guest permissions");
}
void watchedLifetime(Simpsons::Runtime& rt,size_t baseRegions) {
    ResetEvent(workerReached);ResetEvent(workerRelease);
    auto worker=create(rt,0,waitWorker);
    require(WaitForSingleObject(workerReached,10000)==WAIT_OBJECT_0,"watched worker did not reach its stable wait");
    uint64_t stackSize=0;
    {std::lock_guard lock(rt.vmMutex);for(const auto& region:rt.regions)if(region.address==worker.stack)stackSize=region.size;}
    auto watch=rt.writeWatch();uint32_t first=0,last=0;
    require(stackSize&&watch.pages(rt.base+worker.stack,size_t(stackSize),first,last),"live stack has no watched extent");
    std::vector<uint32_t> stackVersions;watch.arm(first,last,stackVersions);
    require(watch.unchanged(first,last,stackVersions.data()),"stable waiting stack changed before cleanup");
    SetEvent(workerRelease);finish(rt,worker);
    require(!watch.unchanged(first,last,stackVersions.data()),"terminated stack retained its cached page versions");
    for(uint32_t page=first;page<=last;++page)require(!watch.armed[page].load()&&
        watch.version[page].load()==stackVersions[page-first]+1,"stack cleanup did not invalidate every owned page exactly once");
    requireDecommitted(rt,worker.stack);
    require(watch.pages(rt.base+worker.pcr,areaSize,first,last),"retained thread owner has no watched extent");
    std::vector<uint32_t> areaVersions;watch.arm(first,last,areaVersions);
    collect(rt);require(watch.unchanged(first,last,areaVersions.data()),"live guest handle retired its thread-owner pages");
    rt.closeHandle(worker.handle);emptyRegistry(rt,baseRegions);
    require(!watch.unchanged(first,last,areaVersions.data()),"reaped owner retained its cached page versions");
    for(uint32_t page=first;page<=last;++page)require(!watch.armed[page].load()&&
        watch.version[page].load()==areaVersions[page-first]+1,"owner cleanup did not invalidate every owned page exactly once");
    requireDecommitted(rt,worker.pcr);ResetEvent(workerReached);ResetEvent(workerRelease);
    puts("[THREAD WATCH LIFETIME] waiting stack/use/termination/retained-owner/reaping: production page invalidation passed");
}
void retentionTests(Simpsons::Runtime& rt,size_t baseRegions) {
    uint8_t* base=rt.base;
    auto retained=create(rt);finish(rt,retained);
    auto other=create(rt,1);
    require(other.pcr!=retained.pcr && other.stack==retained.stack,"handle retention blocked stack reuse or released KTHREAD");
    require(PPC_LOAD_U32(retained.tls+kTls_data_size)==0xfeedbeef,"retained TLS was overwritten");
    finish(rt,other,true);rt.closeHandle(other.handle);collect(rt);
    require(rt.threads.size()==1 && rt.tlsBases.size()==1,"live guest handle was reaped");
    PPCContext ctx{};ctx.r3.u32=retained.handle;ctx.r4.u32=rt.threadObjectType;ctx.r5.u32=referenceOutput;
    __imp__ObReferenceObjectByHandle(ctx,base);
    require(ctx.r3.u32==0 && PPC_LOAD_U32(referenceOutput)==retained.object,"real typed reference failed");
    rt.closeHandle(retained.handle);
    other=create(rt,1);require(other.pcr!=retained.pcr,"explicit object reference was reaped");
    finish(rt,other,true);rt.closeHandle(other.handle);collect(rt);
    require(rt.threads.size()==1,"explicit reference did not retain KTHREAD");
    // In-flight service shared_ptrs must also keep the record alive after the
    // last explicit guest reference has gone away.
    auto inFlight=rt.threadObject(retained.object);
    ctx.r3.u32=retained.object;__imp__ObDereferenceObject(ctx,base);collect(rt);
    require(rt.threads.size()==1 && rt.objectReferences.empty(),"in-flight shared ownership was ignored");
    inFlight.reset();emptyRegistry(rt,baseRegions);
    auto reused=create(rt,1);require(reused.pcr==firstArea && reused.stack==firstStack,"reaped intervals not reused");
    finish(rt,reused,true);rt.closeHandle(reused.handle);emptyRegistry(rt,baseRegions);
}
void osTerminationTest(Simpsons::Runtime& rt,size_t baseRegions) {
    auto delayed=create(rt,0,delayedExit);
    require(WaitForSingleObject(exitReached,10000)==WAIT_OBJECT_0,"native TLS exit barrier not reached");
    {
        std::lock_guard lock(rt.threadMutex);
        require(rt.threads.front()->finished && WaitForSingleObject(delayed.native,0)==WAIT_TIMEOUT,
                "fixture did not separate finished flag from native termination");
    }
    rt.closeHandle(delayed.handle); // Registry is now the sole KernelHandle owner.
    auto other=create(rt,1);
    require(other.pcr!=delayed.pcr && other.stack==delayed.stack,"reaper used finished instead of native termination");
    require(SetEvent(exitRelease)!=0 && WaitForSingleObject(delayed.native,10000)==WAIT_OBJECT_0,"native TLS exit release failed");
    require(!exitWaitFailed,"native TLS teardown timed out");
    finish(rt,other,true);rt.closeHandle(other.handle);emptyRegistry(rt,baseRegions);
}
void activeAndGapTests(Simpsons::Runtime& rt,size_t baseRegions) {
    auto live=create(rt,0,waitWorker);
    require(WaitForSingleObject(workerReached,10000)==WAIT_OBJECT_0,"running worker not reached");
    rt.closeHandle(live.handle);
    auto other=create(rt,1);
    require(other.pcr!=live.pcr && other.stack!=live.stack,"running worker mapping was reused");
    require(SetEvent(workerRelease)!=0 && WaitForSingleObject(live.native,10000)==WAIT_OBJECT_0,"running worker did not exit");
    finish(rt,other,true);rt.closeHandle(other.handle);emptyRegistry(rt,baseRegions);
    // Mixed live Region and reserved-but-uncommitted Allocation obstacles.
    {
        std::lock_guard lock(rt.vmMutex);
        rt.map(firstArea,areaSize,true,"occupied area",Simpsons::MemoryUse::Kernel);
        rt.map(firstStack,0x10000,true,"occupied stack",Simpsons::MemoryUse::Stack);
        rt.allocations.push_back({firstStack+0x10000,0x20000,0x1000,std::vector<uint8_t>(32)});
    }
    other=create(rt,1,0,output,0x5000);
    require(other.pcr==firstArea+areaSize && other.stack==firstStack+0x30000,"first-fit ignored an occupied/reserved interval");
    finish(rt,other,true);rt.closeHandle(other.handle);collect(rt);
    {std::lock_guard lock(rt.vmMutex);rt.allocations.clear();}
    rt.unmap(firstArea);rt.unmap(firstStack);emptyRegistry(rt,baseRegions);
}
void rollbackTests(Simpsons::Runtime& rt,size_t baseRegions) {
    uint8_t* base=rt.base;
    for(unsigned scenario=0;scenario<6;++scenario) {
        unsigned before=executions;uint32_t savedNext=rt.nextHandle;
        PPC_STORE_U32(output,0xdeadbeef);PPC_STORE_U32(idOutput,0x12345678);
        if(scenario==0) failMap=int(Simpsons::MemoryUse::Kernel);
        if(scenario==1) failMap=int(Simpsons::MemoryUse::Stack);
        if(scenario==2) failPointer=firstArea+0x100; // Native thread exists, initialization incomplete.
        if(scenario==3) failPointer=firstArea+0x10c; // TLS registered, affinity publication fails.
        if(scenario==4) rt.nextHandle=0xfffffff0; // Production addHandle rejects after DuplicateHandle.
        if(scenario==5) {failGuestPointer=output;resumeBeforeFailure=true;} // Published, resumed, still gated.
        bool rejected=false;
        try {create(rt);} catch(const Simpsons::Failure&) {rejected=true;}
        failMap=-1;failPointer=0;failGuestPointer=0;resumeBeforeFailure=false;
        if(scenario==4) rt.nextHandle=savedNext;
        require(rejected && executions==before && !rt.stopping,"rollback ran guest code or failed to reject");
        require(PPC_LOAD_U32(output)==0xdeadbeef && PPC_LOAD_U32(idOutput)==0x12345678,"failed creation published guest outputs");
        emptyRegistry(rt,baseRegions);requireDecommitted(rt,firstArea);requireDecommitted(rt,firstStack);
        auto recovery=create(rt);finish(rt,recovery);rt.closeHandle(recovery.handle);emptyRegistry(rt,baseRegions);
    }
}
}

#include "header/test_thread_placement.h"
int main() {
    try {
        HostEvent reached,release,running,continueWorker;
        exitReached=reached.value;exitRelease=release.value;workerReached=running.value;workerRelease=continueWorker.value;
        Simpsons::Runtime rt;prepare(rt);size_t baseRegions=regionCount(rt);
        threadPlacementContracts(rt,baseRegions);
        watchedLifetime(rt,baseRegions);
        // Warm the CRT's native/std::thread bookkeeping at the same concurrency
        // as the later test before measuring process-wide handle growth.
        std::atomic<unsigned> warmReady=0;std::atomic<bool> warmFailed=false;
        auto warm=[&](uint32_t out) {
            try {
                auto worker=create(rt,0,waitWorker,out);++warmReady;
                finish(rt,worker);rt.closeHandle(worker.handle);
            } catch(const std::exception&) {warmFailed=true;warmReady=2;}
        };
        std::thread warmA(warm,0x10040),warmB(warm,0x10050);
        for(unsigned attempt=0;attempt<10000 && warmReady<2;++attempt) Sleep(1);
        SetEvent(workerRelease);warmA.join();warmB.join();
        require(warmReady==2 && !warmFailed,"native/CRT warmup failed");
        ResetEvent(workerReached);ResetEvent(workerRelease);emptyRegistry(rt,baseRegions);
        DWORD initialHandles=0;require(GetProcessHandleCount(GetCurrentProcess(),&initialHandles)!=0,"native handle count failed");
        unsigned before=executions;
        for(unsigned i=0;i<512;++i) {
            auto worker=create(rt,i&1,i);
            require(worker.pcr==firstArea && worker.stack==firstStack,"sequential slots were not reused");
            finish(rt,worker,(i&1)!=0);rt.closeHandle(worker.handle);
            // Sample actual handles before another allocation can reuse their
            // numeric values. This is independent of CRT process-wide caches.
            if(i==511) {
                collect(rt);
                for(HANDLE native:{worker.native,worker.gate,worker.guestNative}) {
                    DWORD flags=0;
                    require(!GetHandleInformation(native,&flags) && GetLastError()==ERROR_INVALID_HANDLE,
                            "reaping did not close a worker/gate/guest native handle");
                }
            }
        }
        require(executions-before==512,"not all 512 real AOT workers ran");emptyRegistry(rt,baseRegions);
        DWORD afterSequential=0;GetProcessHandleCount(GetCurrentProcess(),&afterSequential);
        retentionTests(rt,baseRegions);osTerminationTest(rt,baseRegions);activeAndGapTests(rt,baseRegions);rollbackTests(rt,baseRegions);
        // Two independent callers churn concurrently through the production locks.
        std::atomic<bool> callerFailed=false;
        auto churn=[&](uint32_t out) {
            try {for(unsigned i=0;i<64;++i) {auto worker=create(rt,0,i,out);finish(rt,worker);rt.closeHandle(worker.handle);}}
            catch(const std::exception& error) {fprintf(stderr,"concurrent caller: %s\n",error.what());callerFailed=true;}
        };
        std::thread a(churn,0x10040),b(churn,0x10050);a.join();b.join();
        require(!callerFailed,"concurrent creation/reaping failed");emptyRegistry(rt,baseRegions);
        // Winpthreads can finish process-internal cleanup just after join.
        // Give its fixed cache/cleanup a bounded settling interval; a per-worker
        // handle leak still exceeds this allowance by hundreds of handles.
        DWORD finalHandles=0;
        for(unsigned attempt=0;attempt<100;++attempt) {
            require(GetProcessHandleCount(GetCurrentProcess(),&finalHandles)!=0,"final native handle count failed");
            if(finalHandles<=initialHandles+4) break;
            Sleep(5);
        }
        fprintf(stderr,"[TEST] native handles initial=%lu after512=%lu final=%lu\n",initialHandles,afterSequential,finalHandles);
        // CRT thread-local machinery may retain a small process-global cache.
        require(finalHandles<=initialHandles+4,"native handles leaked across lifecycle churn");
        auto suspended=create(rt,1);before=executions;rt.stopThreads();
        require(WaitForSingleObject(suspended.native,0)==WAIT_OBJECT_0 && executions==before,"shutdown stranded or ran a suspended worker");
        puts("PASS: 512 sequential + 128 concurrent native AOT workers, retained handles/references, OS termination gate, first-fit reuse, rollback, cancellation");
        return 0;
    } catch(const std::exception& error) {fprintf(stderr,"FAIL: %s (Win32=%lu)\n",error.what(),GetLastError());return 1;}
}
