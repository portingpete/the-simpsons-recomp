#include "threads.h"
#include "native_window.h"
#include "checked_running.h"
#include "thread_placement.h"
#include "stall_profiler.h"
#include "ppc_image_metadata.h"
#include <algorithm>
#include <bit>

namespace Simpsons {
namespace {
// Caller holds vmMutex. Shared with exit, reaping and creation rollback.
void unmapLocked(Runtime& rt,uint32_t address) {
    auto found=std::find_if(rt.regions.begin(),rt.regions.end(),[&](const auto& region){return region.address==address;});
    if(found==rt.regions.end()) throw Failure("Native unmap requires an exact mapped extent");
    if(!VirtualFree(rt.base+address,found->size,MEM_DECOMMIT)) throw Failure("Native page decommit failed");
    for(uint64_t pos=address;pos<uint64_t(address)+found->size;pos+=4096) rt.pageAccess[pos>>12].store(0,std::memory_order_release);
    rt.invalidateWatchedPages(address,found->size);
    rt.regions.erase(found);
}
void mapThreadLocked(Runtime& rt,uint32_t address,uint32_t size,const char* name,MemoryUse use) {
    // firstFit verified this interval is unused while the same vmMutex is held.
    // map may fail after committing host pages but before publishing its Region.
    try {rt.map(address,size,true,name,use);}
    catch(...) {
        if(!VirtualFree(rt.base+address,size,MEM_DECOMMIT)) {
            rt.requestStop("Thread mapping rollback decommit failed");
            throw Failure("Thread mapping rollback decommit failed");
        }
        std::erase_if(rt.regions,[&](const auto& region){return region.address==address;});
        for(uint64_t pos=address;pos<uint64_t(address)+size;pos+=4096) rt.pageAccess[pos>>12].store(0,std::memory_order_release);
        rt.invalidateWatchedPages(address,size);
        throw;
    }
}
uint32_t firstFit(const Runtime& rt,uint32_t low,uint32_t high,uint32_t size,uint32_t alignment) {
    if(!alignment || !std::has_single_bit(alignment) || !size || low>=high) return 0;
    uint64_t candidate=low;
    while(candidate+size<=high) {
        uint64_t next=candidate;
        auto skip=[&](uint32_t address,uint64_t length) {
            if(candidate<uint64_t(address)+length && address<candidate+size)
                next=std::max(next,(uint64_t(address)+length+alignment-1)&~uint64_t(alignment-1));
        };
        for(const auto& region:rt.regions) skip(region.address,region.size);
        // Reserved-but-uncommitted virtual allocations also own their interval.
        for(const auto& allocation:rt.allocations) skip(allocation.address,allocation.size);
        if(next==candidate) return uint32_t(candidate);
        candidate=next;
    }
    return 0;
}
void reapThreadsLocked(Runtime& rt) {
    // threadMutex excludes lookup/publication; a handle, object reference, or
    // in-flight native service holds another shared_ptr and prevents reaping.
    for(auto it=rt.threads.begin();it!=rt.threads.end();) {
        auto& thread=**it;
        DWORD state=WaitForSingleObject(thread.native,0);
        if(state==WAIT_FAILED) throw Failure("Native thread termination query failed");
        if(state!=WAIT_OBJECT_0 || thread.object.use_count()!=1) {++it;continue;}
        {
            std::lock_guard memoryLock(rt.vmMutex);
            unmapLocked(rt,thread.pcr);
            std::erase(rt.tlsBases,thread.tls+kTls_data_size);
        }
        it=rt.threads.erase(it); // OS has returned; destructor joins/closes native handles.
    }
}
struct ThreadCreateNoMemory {};
std::vector<HostProcessorCore> processorCores() {
    // This optional placement policy is bounded to the existing group-zero
    // mapping. Missing/malformed topology retains the previous host mapping.
    const DWORD saved=GetLastError();
    struct Restore {DWORD error;~Restore(){SetLastError(error);}} restore{saved};
    if(GetActiveProcessorGroupCount()!=1)return {};
    DWORD bytes=0;
    if(GetLogicalProcessorInformationEx(RelationProcessorCore,nullptr,&bytes) ||
       GetLastError()!=ERROR_INSUFFICIENT_BUFFER || !bytes || bytes>1024*1024)return {};
    std::vector<uint64_t> storage((bytes+7)/8);
    auto* data=reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(storage.data());
    if(uintptr_t(data)%alignof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)!=0) return {};
    if(!GetLogicalProcessorInformationEx(RelationProcessorCore,data,&bytes) || bytes>storage.size()*8)return {};
    constexpr size_t masksOffset=offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX,Processor)+offsetof(PROCESSOR_RELATIONSHIP,GroupMask);
    std::vector<HostProcessorCore> cores;uintptr_t seen=0;
    for(size_t offset=0;offset<bytes;) {
        if(bytes-offset<masksOffset+sizeof(GROUP_AFFINITY))return {};
        if(offset%alignof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)!=0) return {};
        const auto* entry=reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(reinterpret_cast<const uint8_t*>(data)+offset);
        if(entry->Relationship!=RelationProcessorCore || entry->Size<masksOffset+sizeof(GROUP_AFFINITY) || entry->Size>bytes-offset ||
           entry->Processor.GroupCount!=1 || entry->Processor.GroupMask[0].Group!=0)return {};
        const auto mask=entry->Processor.GroupMask[0].Mask;
        if(!mask || (mask&seen))return {};
        seen|=mask;cores.push_back({mask,entry->Processor.EfficiencyClass});offset+=entry->Size;
    }
    return cores;
}
}
void assignThreadAffinity(Runtime& rt,HANDLE native,uint32_t pcr,uint32_t thread,uint32_t logicalMask) {
    if(!logicalMask || (logicalMask&~0x3fu)) throw Failure("Invalid six-CPU guest affinity mask");
    if(!std::has_single_bit(logicalMask)) throw Failure("Multi-CPU affinity needs a guest CPU migration contract");
    DWORD_PTR processMask=0,systemMask=0;
    if(!GetProcessAffinityMask(GetCurrentProcess(),&processMask,&systemMask) || !processMask)
        throw Failure("Native process affinity query failed");
    unsigned cpu=std::countr_zero(logicalMask);
    // Guest logical processors map consistently onto the process's allowed CPUs.
    const auto allCores=processorCores();
    const auto cores=cpu==0?allCores:std::vector<HostProcessorCore>{};
    const auto chosen=selectThreadProcessor(processMask,cpu,cores);
    static const bool hardPin=[]{const char* text=std::getenv("SIMPSONS_HARD_PIN");return text&&*text&&*text!='0';}();
    if(hardPin) {
        if(!SetThreadAffinityMask(native,chosen)) throw Failure("Native thread affinity assignment failed");
    } else {
        // A thread hard-pinned to one logical CPU waits a whole scheduler quantum whenever a
        // neighbour occupies that CPU, which shows up as multi-millisecond frame hitches under
        // desktop load. Prefer the chosen CPU (ideal processor) but let the scheduler migrate the
        // thread among the performance cores (the fastest efficiency class) when it is busy.
        uintptr_t allowed=0;
        uint8_t fastest=0;
        for(const auto& core:allCores)if((core.mask&processMask)&&core.efficiency>fastest)fastest=core.efficiency;
        for(const auto& core:allCores)if((core.mask&processMask)&&core.efficiency==fastest)allowed|=core.mask&processMask;
        if(!allowed)allowed=processMask;
        if(!(allowed&chosen))allowed|=chosen;
        if(!SetThreadAffinityMask(native,allowed)) throw Failure("Native thread affinity assignment failed");
        SetThreadIdealProcessor(native,DWORD(std::countr_zero(uint64_t(chosen))));
    }
    *rt.pointer(pcr+0x10c,1,true)=uint8_t(cpu);
    *rt.pointer(thread+0xbf,1,true)=uint8_t(cpu);
    PPCStoreU32(rt.base,pcr+0x110,logicalMask);
}
GuestThread::~GuestThread() {
    if(native) {
        if(native==INVALID_HANDLE_VALUE) {fprintf(stderr,"[THREAD FAILURE] Invalid native worker handle\n");std::terminate();}
        DWORD probe=WaitForSingleObject(native,0);
        if(probe==WAIT_FAILED) {fprintf(stderr,"[THREAD FAILURE] Worker state query failed\n");std::terminate();}
        if(probe!=WAIT_OBJECT_0) {
            cancelled=true;
            if(startEvent) SetEvent(startEvent);
            DWORD previous;
            do {previous=ResumeThread(native);} while(previous!=DWORD(-1) && previous>1);
            if(previous==DWORD(-1)) {fprintf(stderr,"[THREAD FAILURE] Worker resume failed\n");std::terminate();}
        }
        // finished is published before the OS thread epilogue/TLS teardown.
        // Never destroy its context or close its gate until the HANDLE signals.
        StallProfiler::Scope joinProfile(StallProfiler::Section::Wait,"GuestThread::~GuestThread.join",nullptr,reinterpret_cast<uintptr_t>(native));
        const DWORD joined=WaitForSingleObject(native,INFINITE);
        joinProfile.finish();
        if(joined!=WAIT_OBJECT_0) {
            fprintf(stderr,"[THREAD FAILURE] Cannot safely destroy an unjoined native worker\n");
            std::terminate();
        }
        CloseHandle(native);
    }
    if(startEvent) CloseHandle(startEvent);
}
}
// Cancellation flag polled at every original function entry (PPC_TRACE_ENTRY).
volatile uint8_t PPCStopRequested=0;
namespace Simpsons {
void Runtime::requestStop(const std::string& reason) {
    std::lock_guard lock(stopMutex);
    if(!stopping) {stopReason=reason;stopping.store(true,std::memory_order_release);PPCStopRequested=1;}
    SetEvent(stopEvent);
}
void Runtime::checkRunning() {checkRuntimeRunning(*this);}
void Runtime::stopThreads() {
    requestStop("Native runtime shutdown");
    std::vector<GuestThread*> pending;
    {
        std::lock_guard lock(threadMutex);
        for(auto& thread:threads) pending.push_back(thread.get());
    }
    for(auto* thread:pending) {
        // Only initial suspension/resumption is implemented. Never leave a
        // suspended guest holding the runtime alive during failure teardown.
        if(!thread || !thread->native || thread->native==INVALID_HANDLE_VALUE) continue;
        DWORD previous;
        do {previous=ResumeThread(thread->native);} while(previous!=DWORD(-1) && previous>1);
    }
    for(auto* thread:pending) {
        if(!thread || !thread->native || thread->native==INVALID_HANDLE_VALUE) continue;
        // Cancellation is polled at original function entry and in waits, so a guest
        // thread spinning in a call-free loop cannot observe it: bound the join.
        StallProfiler::Scope joinProfile(StallProfiler::Section::Wait,"Runtime::stopThreads.join",nullptr,reinterpret_cast<uintptr_t>(thread->native));
        DWORD result=WaitForSingleObject(thread->native,20000);
        joinProfile.finish();
        if(result==WAIT_TIMEOUT) {
            fprintf(stderr,"[THREAD FAILURE] A guest thread did not observe cancellation within 20 s; terminating the process"); fputc(10,stderr);
            fflush(stderr);
            TerminateProcess(GetCurrentProcess(),3);
        }
        if(result==WAIT_FAILED) fprintf(stderr,"[THREAD FAILURE] Shutdown join failed\n");
    }
}
void Runtime::unmap(uint32_t address) {
    std::lock_guard lock(vmMutex);
    unmapLocked(*this,address);
}
void Runtime::initializeThread(PPCContext& ctx,uint32_t pcr,uint32_t thread,uint32_t tls,uint32_t stack,uint32_t stackSize,uint32_t id) {
    if(uint64_t(kTls_data_size)+uint64_t(kTls_slot_count)*4>0x1000) throw Failure("Thread TLS exceeds bootstrap page");
    if(uint64_t(stack)+stackSize>0x100000000ull || stackSize<0x100) throw Failure("Thread stack range invalid");
    memcpy(pointer(tls,kTls_raw_data_size,true),pointer(kTls_raw_data_address,kTls_raw_data_size,false),kTls_raw_data_size);
    uint32_t dynamic=tls+kTls_data_size;
    ctx.r13.u64=pcr; ctx.r1.u64=uint64_t(stack)+stackSize-0x100; ctx.lr=0;
    PPCStoreU32(base,pcr,tls); PPCStoreU64(base,pcr+0x30,pcr);
    PPCStoreU32(base,pcr+0x70,stack+stackSize); PPCStoreU32(base,pcr+0x74,stack);
    PPCStoreU32(base,pcr+0x100,thread); PPCStoreU32(base,pcr+0x110,1);
    PPCStoreU32(base,pcr+0x2a8,pcr+0x100);
    PPCStoreU32(base,thread+0x5c,stack+stackSize); PPCStoreU32(base,thread+0x60,stack);
    PPCStoreU32(base,thread+0x68,dynamic);
    PPCStoreU32(base,thread+0xc0,pcr+0x100);
    PPCStoreU32(base,thread+0xc4,pcr+0x100);
    *pointer(thread,1,true)=6;*pointer(thread+0x70,1,true)=8;
    *pointer(thread+0x72,1,true)=1; *pointer(thread+0x73,1,true)=1;
    PPCStoreU32(base,thread+0x14c,id);
    tlsBases.push_back(dynamic);
}
static DWORD WINAPI threadMain(void* argument) {
    auto& thread=*static_cast<GuestThread*>(argument);
    auto& rt=*thread.runtime;
    StallProfiler::Scope gateProfile(StallProfiler::Section::Wait,"GuestThread.startGate",&thread.context,reinterpret_cast<uintptr_t>(thread.startEvent));
    const DWORD started=WaitForSingleObject(thread.startEvent,INFINITE);
    gateProfile.finish();
    if(started!=WAIT_OBJECT_0) {
        rt.requestStop("Native worker creation gate wait failed");thread.finished=true;return 1;
    }
    if(thread.cancelled) {thread.finished=true;return 0;}
    currentContext=&thread.context;
    uint32_t result=0;
    try {
        rt.checkRunning();
        if(thread.startup) {thread.context.r3.u64=thread.entry;thread.context.r4.u64=thread.argument;}
        else thread.context.r3.u64=thread.argument;
        result=runThreadEntry(thread.context,rt.base,thread.startup?thread.startup:thread.entry);
    } catch(const ThreadExit& exit) {result=exit.code;}
        catch(const std::exception& failure) {
            rt.resourceAudit.failure(failure.what());
        result=1;
        if(!rt.stopping) {
            fprintf(stderr,"[THREAD FAILURE] id=%u function=0x%08X lr=0x%08X: %s\n",thread.id,thread.context.lastFunction,uint32_t(thread.context.lr),failure.what());
            rt.requestStop(failure.what());
        }
    }
    currentContext=nullptr;
    // KTHREAD/TLS records remain while handles may still refer to this thread.
    // Its no-longer-live stack is decommitted and removed from the memory budget.
    try {rt.unmap(thread.stack);} catch(const std::exception& failure) {rt.requestStop(failure.what());result=1;}
    fprintf(stderr,"[THREAD] exited id=%u status=0x%X\n",thread.id,result);
    thread.finished=true;
    return result;
}
uint32_t Runtime::createThread(PPCContext& caller) {
    std::lock_guard registryLock(threadMutex);
    checkRunning();
    reapThreadsLocked(*this);
    uint32_t output=caller.r3.u32,requestedStack=caller.r4.u32,idOutput=caller.r5.u32;
    uint32_t startup=caller.r6.u32,entry=caller.r7.u32,argument=caller.r8.u32,flags=caller.r9.u32;
    fprintf(stderr,"[THREAD] create stack=0x%X startup=0x%08X entry=0x%08X arg=0x%08X flags=0x%X\n",requestedStack,startup,entry,argument,flags);
    if(flags&~0x3f000001u) throw Failure("Unsupported ExCreateThread flags");
    uint32_t affinity=(flags>>24)?(flags>>24):1;
    if(!std::has_single_bit(affinity)) throw Failure("Multi-CPU affinity needs a guest CPU migration contract");
    if(output) pointer(output,4,true);
    if(idOutput) pointer(idOutput,4,true);
    for(uint32_t address:{entry,startup}) if(address && (address<PPC_CODE_BASE || address>=PPC_CODE_BASE+PPC_CODE_SIZE || (address&3) || !PPC_LOOKUP_FUNC(base,address)))
        throw Failure("Native thread entry has no AOT function mapping");
    if(!entry) return 0xc000000d;
    uint64_t stackSize=(uint64_t(requestedStack?requestedStack:0x40000)+4095)&~4095ull;
    stackSize=std::max<uint64_t>(stackSize,0x4000);
    if(stackSize>0x4000000) return 0xc0000017;
    threads.reserve(threads.size()+1); // Publication cannot allocate after native entry is enabled.
    auto thread=std::make_unique<GuestThread>();
    thread->runtime=this;thread->stackSize=uint32_t(stackSize);
    thread->startup=startup;thread->entry=entry;thread->argument=argument;thread->flags=flags;
    bool areaMapped=false,stackMapped=false,published=false;
    uint32_t handle=0,area=0,stack=0,tls=0;
    auto rollback=[&] {
        if(published) {thread=std::move(threads.back());threads.pop_back();}
        if(handle) closeHandle(handle);
        thread.reset(); // Cancel behind the gate, resume and JOIN before decommit.
        std::lock_guard memoryLock(vmMutex);
        if(tls) std::erase(tlsBases,tls+kTls_data_size);
        if(stackMapped) unmapLocked(*this,stack);
        if(areaMapped) unmapLocked(*this,area);
    };
    try {
        {
            std::lock_guard memoryLock(vmMutex);
            if(committedPages()+stackSize/4096+4>memoryBudgetPages) throw ThreadCreateNoMemory{};
            area=firstFit(*this,0x01040000,0x02000000,0x4000,0x4000);
            stack=firstFit(*this,0x02100000,0x08000000,uint32_t(stackSize),0x10000);
            if(!area || !stack) throw ThreadCreateNoMemory{};
            thread->pcr=area;thread->thread=area+0x1000;thread->tls=tls=area+0x2000;thread->stack=stack;
            regions.reserve(regions.size()+2);tlsBases.reserve(tlsBases.size()+1);
            mapThreadLocked(*this,area,0x4000,"worker PCR/TLS",MemoryUse::Kernel);areaMapped=true;
            mapThreadLocked(*this,stack,uint32_t(stackSize),"worker stack",MemoryUse::Stack);stackMapped=true;
        }
        thread->startEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!thread->startEvent) throw ThreadCreateNoMemory{};
        DWORD id=0;
        thread->native=CreateThread(nullptr,0x4000000,threadMain,thread.get(),CREATE_SUSPENDED|STACK_SIZE_PARAM_IS_A_RESERVATION,&id);
        if(!thread->native) throw ThreadCreateNoMemory{};
        thread->id=id;
        {
            std::lock_guard memoryLock(vmMutex);
            initializeThread(thread->context,area,thread->thread,tls,stack,thread->stackSize,id);
        }
        assignThreadAffinity(*this,thread->native,area,thread->thread,affinity);
        thread->object=std::make_shared<KernelHandle>(nullptr,KernelHandle::Type::Thread);
        if(!DuplicateHandle(GetCurrentProcess(),thread->native,GetCurrentProcess(),&thread->object->native,0,FALSE,DUPLICATE_SAME_ACCESS))
            throw Failure("Native thread handle duplication failed");
        thread->object->guestObject=thread->thread;
        handle=addHandle(thread->object);
        auto* live=thread.get();threads.push_back(std::move(thread));published=true;
        {
            // Prevent unmap/protect between validation and the final stores.
            // All operations that can throw precede opening the creation gate.
            std::lock_guard memoryLock(vmMutex);
            uint8_t* out=output?PPCGuestPointer(base,output,4,true):nullptr;
            uint8_t* idOut=idOutput?PPCGuestPointer(base,idOutput,4,true):nullptr;
            if(!(flags&1) && ResumeThread(live->native)==DWORD(-1)) throw Failure("Native initial thread resume failed");
            if(!SetEvent(live->startEvent)) throw Failure("Native worker creation gate release failed");
            uint32_t beHandle=__builtin_bswap32(handle),beId=__builtin_bswap32(id);
            if(out) memcpy(out,&beHandle,4);
            if(idOut) memcpy(idOut,&beId,4);
        }
        fprintf(stderr,"[THREAD] created handle=0x%X id=%u PCR=0x%08X stack=0x%08X..0x%08X suspended=%d\n",handle,uint32_t(id),area,stack,stack+uint32_t(stackSize),int((flags&1)!=0));
        return 0;
    } catch(const ThreadCreateNoMemory&) {rollback();return 0xc0000017;}
      catch(...) {rollback();throw;}
}
}
PPC_FUNC(__imp__ExCreateThread) {ctx.r3.u64=Simpsons::active->createThread(ctx);}
PPC_FUNC(__imp__ExTerminateThread) {throw Simpsons::ThreadExit{ctx.r3.u32};}
PPC_FUNC(__imp__NtResumeThread) {
    auto object=Simpsons::active->getHandle(ctx.r3.u32);
    if(!object || object->type!=Simpsons::KernelHandle::Type::Thread) {ctx.r3.u64=0xc0000008;return;}
    uint32_t output=ctx.r4.u32;
    if(output) PPCGuestPointer(base,output,4,true);
    DWORD previous=ResumeThread(object->native);
    if(previous==DWORD(-1)) {ctx.r3.u64=0xc0000001;return;}
    if(output) PPC_STORE_U32(output,previous);
    ctx.r3.u64=0;
}
