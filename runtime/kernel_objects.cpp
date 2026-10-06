#include "runtime.h"
#include "stall_profiler.h"
#include <cstdio>
#include <cstring>

namespace Simpsons {
uint32_t Runtime::addHandle(std::shared_ptr<KernelHandle> handle) {
    std::lock_guard lock(handleMutex);
    if(nextHandle>=0xfffffff0u) throw Failure("Guest handle space exhausted");
    uint32_t id=nextHandle; nextHandle+=4;
    handles.emplace(id,std::move(handle));
    return id;
}
std::shared_ptr<KernelHandle> Runtime::getHandle(uint32_t id) {
    std::lock_guard lock(handleMutex);
    auto it=handles.find(id);
    return it==handles.end()?nullptr:it->second;
}
uint32_t Runtime::closeHandle(uint32_t id) {
    std::lock_guard lock(handleMutex);
    return handles.erase(id)?0:0xc0000008u;
}
}
namespace {
template<class T> T native(const char* name) {
    HMODULE mod=GetModuleHandleW(L"ntdll.dll");
    if(!mod) throw Simpsons::Failure("ntdll.dll module handle unavailable");
    auto function=GetProcAddress(mod,name);
    if(!function) throw Simpsons::Failure(std::string("Required native NT service unavailable: ")+name);
    T fn{};std::memcpy(&fn,&function,sizeof(fn));
    return fn;
}
using NtCreateSemaphoreFn=LONG(NTAPI*)(PHANDLE,ACCESS_MASK,void*,LONG,LONG);
using NtReleaseSemaphoreFn=LONG(NTAPI*)(HANDLE,LONG,PLONG);
using NtWaitSingleFn=LONG(NTAPI*)(HANDLE,BOOLEAN,PLARGE_INTEGER);
using NtWaitMultipleFn=LONG(NTAPI*)(ULONG,const HANDLE*,ULONG,BOOLEAN,PLARGE_INTEGER);
using NtCreateTimerFn=LONG(NTAPI*)(PHANDLE,ACCESS_MASK,void*,ULONG);
using NtSetTimerFn=LONG(NTAPI*)(HANDLE,PLARGE_INTEGER,void*,void*,BOOLEAN,LONG,PBOOLEAN);
using NtCancelTimerFn=LONG(NTAPI*)(HANDLE,PBOOLEAN);
using NtCreateMutantFn=LONG(NTAPI*)(PHANDLE,ACCESS_MASK,void*,BOOLEAN);
using NtReleaseMutantFn=LONG(NTAPI*)(HANDLE,PLONG);
using NtCreateEventFn=LONG(NTAPI*)(PHANDLE,ACCESS_MASK,void*,ULONG,BOOLEAN);
using NtSetEventFn=LONG(NTAPI*)(HANDLE,PLONG);
using NtResetEventFn=LONG(NTAPI*)(HANDLE,PLONG);
struct EventHostState {
    const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
    EventHostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~EventHostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
};
bool diagnosticStackWord(const Simpsons::Runtime& rt,uint32_t address,uint32_t& value) noexcept {
    // Guest stacks use the direct virtual mapping. Two bounded reads are enough
    // to recover this ABI's saved LR; never probe aliases or throw on bad frames.
    if(!rt.base || (address&3u) || address<0x10000u || address>=0x82000000u ||
        !(rt.pageAccess.load(address>>12,std::memory_order_acquire)&1u)) return false;
    const auto* bytes=rt.base+address;
    value=(uint32_t(bytes[0])<<24)|(uint32_t(bytes[1])<<16)|(uint32_t(bytes[2])<<8)|bytes[3];
    return true;
}
uint32_t diagnosticCaller(const Simpsons::Runtime& rt,const PPCContext& ctx) noexcept {
    const uint32_t stack=ctx.r1.u32;
    uint32_t previous{},caller{};
    if((stack&15u) || !diagnosticStackWord(rt,stack,previous) || (previous&15u) ||
        previous<=stack || uint64_t(previous)-stack>0x10000u ||
        !diagnosticStackWord(rt,previous-8u,caller)) return 0;
    // An original call site must be aligned and in mapped original code. A
    // malformed or unavailable frame is recorded as unknown, never repaired.
    return !(caller&3u) && caller>=0x82000000u && caller<0xa0000000u &&
        (rt.pageAccess.load(caller>>12,std::memory_order_acquire)&1u)?caller:0;
}
uint64_t diagnosticOwner(uint32_t caller) noexcept {
    const DWORD error=GetLastError();
    const uint64_t owner=(uint64_t(GetCurrentThreadId())<<32)|caller;
    SetLastError(error);
    return owner;
}
Simpsons::Runtime& eventRuntime(uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base)throw Simpsons::Failure("Invalid native event runtime");
    Simpsons::active->checkRunning();return *Simpsons::active;
}
}

PPC_FUNC(__imp__NtCreateEvent) {
    EventHostState host;auto& rt=eventRuntime(base);
    const uint32_t output=ctx.r3.u32,type=ctx.r5.u32;
    if(!output || type>1){ctx.r3.u64=0xc000000d;return;}
    rt.pointer(output,4,true);
    if(ctx.r4.u32)throw Simpsons::Failure("Named event attributes are not implemented");
    static auto create=native<NtCreateEventFn>("NtCreateEvent");
    HANDLE handle{};const LONG status=create(&handle,EVENT_ALL_ACCESS,nullptr,type,ctx.r6.u32!=0);
    if(status>=0) {
        if(!handle || handle==INVALID_HANDLE_VALUE) {ctx.r3.u64=0xc0000001;return;}
        std::shared_ptr<Simpsons::KernelHandle> object;
        try{object=std::make_shared<Simpsons::KernelHandle>(handle,Simpsons::KernelHandle::Type::Event);}
        catch(...){CloseHandle(handle);throw;}
        const uint32_t id=rt.addHandle(std::move(object));PPC_STORE_U32(output,id);
        std::fprintf(stderr,"[OBJECT] native event handle=0x%X type=%u initial=%u\n",id,type,ctx.r6.u32!=0);
    }
    ctx.r3.u64=uint32_t(status);
}
PPC_FUNC(__imp__NtSetEvent) {
    EventHostState host;auto& rt=eventRuntime(base);auto object=rt.getHandle(ctx.r3.u32);
    if(!object || object->type!=Simpsons::KernelHandle::Type::Event){ctx.r3.u64=0xc0000008;return;}
    const uint32_t output=ctx.r4.u32;if(output)rt.pointer(output,4,true);
    static auto set=native<NtSetEventFn>("NtSetEvent");
    LONG previous{};const LONG status=set(object->native,output?&previous:nullptr);
    if(status>=0 && output)PPC_STORE_U32(output,uint32_t(previous));
    ctx.r3.u64=uint32_t(status);
}
PPC_FUNC(__imp__NtClearEvent) {
    EventHostState host;auto& rt=eventRuntime(base);auto object=rt.getHandle(ctx.r3.u32);
    if(!object || object->type!=Simpsons::KernelHandle::Type::Event){ctx.r3.u64=0xc0000008;return;}
    // The original clear import takes only a handle. Native reset supplies the
    // same state transition; incidental r4 is never an output argument.
    static auto reset=native<NtResetEventFn>("NtResetEvent");
    ctx.r3.u64=uint32_t(reset(object->native,nullptr));
}

PPC_FUNC(__imp__NtCreateMutant) {
    if(!Simpsons::active || base!=Simpsons::active->base) throw Simpsons::Failure("Invalid native mutant runtime");
    auto& rt=*Simpsons::active;rt.checkRunning();
    const uint32_t output=ctx.r3.u32;
    if(!output) {ctx.r3.u64=0xc000000d;return;}
    rt.pointer(output,4,true);
    if(ctx.r4.u32) throw Simpsons::Failure("Named mutant attributes are not implemented");
    static auto create=native<NtCreateMutantFn>("NtCreateMutant");
    HANDLE handle{};
    const LONG status=create(&handle,MUTEX_ALL_ACCESS,nullptr,ctx.r5.u32!=0);
    if(status>=0) {
        if(!handle || handle==INVALID_HANDLE_VALUE) {ctx.r3.u64=0xc0000001;return;}
        std::shared_ptr<Simpsons::KernelHandle> object;
        try {object=std::make_shared<Simpsons::KernelHandle>(handle,Simpsons::KernelHandle::Type::Mutant);}
        catch(...) {CloseHandle(handle);throw;}
        if(Simpsons::StallProfiler::enabled && ctx.r5.u32)
            object->stallMutantOwner.store(diagnosticOwner(diagnosticCaller(rt,ctx)),std::memory_order_relaxed);
        const uint32_t id=rt.addHandle(std::move(object));
        PPC_STORE_U32(output,id);
        std::fprintf(stderr,"[OBJECT] native mutant handle=0x%X initial_owner=%u\n",id,ctx.r5.u32!=0);
    }
    ctx.r3.u64=uint32_t(status);
}
PPC_FUNC(__imp__NtReleaseMutant) {
    if(!Simpsons::active || base!=Simpsons::active->base) throw Simpsons::Failure("Invalid native mutant runtime");
    auto& rt=*Simpsons::active;rt.checkRunning();
    // Original wrapper 82B76660 passes zero. The console's second argument is
    // not established as the Windows previous-count pointer; never interpret it
    // as guest output or silently accept another control mode.
    if(ctx.r4.u32) throw Simpsons::Failure("Unsupported native mutant release control");
    auto object=rt.getHandle(ctx.r3.u32);
    if(!object || object->type!=Simpsons::KernelHandle::Type::Mutant) {ctx.r3.u64=0xc0000008;return;}
    static auto release=native<NtReleaseMutantFn>("NtReleaseMutant");
    // Guest threads have dedicated native threads. Windows supplies recursive
    // ownership, non-owner rejection and abandonment on actual thread exit.
    const bool profile=Simpsons::StallProfiler::enabled;
    uint64_t observed=profile?object->stallMutantOwner.load(std::memory_order_relaxed):0;
    LONG previous{};
    const LONG status=release(object->native,profile?&previous:nullptr);
    // A native mutant's count is zero at its final owned level and negative
    // while recursively owned. Only observe the optional host previous-count
    // output; guest r4 remains reserved, with no guest memory output.
    if(profile && status>=0 && previous==0)
        object->stallMutantOwner.compare_exchange_strong(observed,0,std::memory_order_relaxed);
    ctx.r3.u64=uint32_t(status);
}

PPC_FUNC(__imp__NtCreateSemaphore) {
    uint32_t output=ctx.r3.u32;
    if(!output) {ctx.r3.u64=0xc000000d;return;}
    PPCGuestPointer(base,output,4,true);
    if(ctx.r4.u32) PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"Named semaphore attributes are not implemented");
    static auto create=native<NtCreateSemaphoreFn>("NtCreateSemaphore");
    if(ctx.r5.s32<0 || ctx.r6.s32<=0 || ctx.r5.s32>ctx.r6.s32) {ctx.r3.u64=0xc000000d;return;}
    HANDLE handle{};
    LONG status=create(&handle,SEMAPHORE_ALL_ACCESS,nullptr,ctx.r5.s32,ctx.r6.s32);
    if(status>=0) {
        if(!handle) {ctx.r3.u64=0xc0000001;return;}
        auto object=std::make_shared<Simpsons::KernelHandle>(handle,Simpsons::KernelHandle::Type::Semaphore);
        uint32_t id=Simpsons::active->addHandle(std::move(object));
        PPC_STORE_U32(output,id);
        fprintf(stderr,"[OBJECT] semaphore handle=0x%X initial=%d limit=%d\n",id,ctx.r5.s32,ctx.r6.s32);
    }
    ctx.r3.u64=uint32_t(status);
}
PPC_FUNC(__imp__NtReleaseSemaphore) {
    auto object=Simpsons::active->getHandle(ctx.r3.u32);
    if(!object || !object->native || object->type!=Simpsons::KernelHandle::Type::Semaphore) {ctx.r3.u64=0xc0000008;return;}
    if(ctx.r4.s32<=0) {ctx.r3.u64=0xc000000d;return;}
    uint32_t output=ctx.r5.u32;
    if(output) PPCGuestPointer(base,output,4,true);
    static auto release=native<NtReleaseSemaphoreFn>("NtReleaseSemaphore");
    LONG previous=0,status=release(object->native,ctx.r4.s32,output?&previous:nullptr);
    if(status>=0 && output) PPC_STORE_U32(output,uint32_t(previous));
    ctx.r3.u64=uint32_t(status);
}
PPC_FUNC(__imp__NtWaitForSingleObjectEx) {
    auto object=Simpsons::active->getHandle(ctx.r3.u32);
    if(!object) {ctx.r3.u64=0xc0000008;return;}
    if(ctx.r4.u32>1) {ctx.r3.u64=0xc000000d;return;}
    LARGE_INTEGER timeout{};
    if(ctx.r6.u32) timeout.QuadPart=int64_t(PPC_LOAD_U64(ctx.r6.u32));
    static auto wait=native<NtWaitMultipleFn>("NtWaitForMultipleObjects");
    HANDLE waited[]={object->native,Simpsons::active->stopEvent};
    const bool profileMutant=Simpsons::StallProfiler::enabled && object->type==Simpsons::KernelHandle::Type::Mutant;
    const uint64_t ownerAtEntry=profileMutant?object->stallMutantOwner.load(std::memory_order_relaxed):0;
    const uint32_t waitCaller=profileMutant?diagnosticCaller(*Simpsons::active,ctx):0;
    Simpsons::StallProfiler::Scope waitProfile(Simpsons::StallProfiler::Section::Wait,"NtWaitForSingleObjectEx",&ctx,ctx.r3.u32);
    LONG status=wait(2,waited,1,ctx.r5.u32!=0,ctx.r6.u32?&timeout:nullptr);
    if(profileMutant) {
        if(status==0 || status==0x80) {
            const uint64_t acquired=diagnosticOwner(waitCaller);
            // Keep the original acquisition call site during recursive entry.
            // Abandonment always replaces the departed native owner's record.
            if(status==0x80 || uint32_t(object->stallMutantOwner.load(std::memory_order_relaxed)>>32)!=uint32_t(acquired>>32))
                object->stallMutantOwner.store(acquired,std::memory_order_relaxed);
        }
        waitProfile.setWaitDetails(uint32_t(ownerAtEntry>>32),uint32_t(ownerAtEntry),waitCaller,uint32_t(status));
    }
    waitProfile.finish();
    // Guest APC enqueue/completion services currently fail explicitly; the
    // modeled guest queue is empty. Never misreport an unknown host APC as a
    // delivered guest callback if another component introduces one.
    if(uint32_t(status)==0xc0) PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"Native APC woke a wait without a guest APC delivery bridge");
    if(status==1) Simpsons::active->checkRunning();
    ctx.r3.u64=uint32_t(status);
}
PPC_FUNC(__imp__NtClose) { ctx.r3.u64=Simpsons::active->closeHandle(ctx.r3.u32); }

PPC_FUNC(__imp__NtCreateTimer) {
    uint32_t output=ctx.r3.u32;
    if(!output || ctx.r5.u32>1) {ctx.r3.u64=0xc000000d;return;}
    PPCGuestPointer(base,output,4,true);
    if(ctx.r4.u32) PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"Named timer attributes are not implemented");
    static auto create=native<NtCreateTimerFn>("NtCreateTimer");
    HANDLE handle{};
    LONG status=create(&handle,TIMER_ALL_ACCESS,nullptr,ctx.r5.u32);
    if(status>=0) {
        if(!handle || handle==INVALID_HANDLE_VALUE) {ctx.r3.u64=0xc0000001;return;}
        uint32_t id=Simpsons::active->addHandle(std::make_shared<Simpsons::KernelHandle>(handle,Simpsons::KernelHandle::Type::Timer));
        PPC_STORE_U32(output,id);
    }
    ctx.r3.u64=uint32_t(status);
}
PPC_FUNC(__imp__NtSetTimerEx) {
    auto object=Simpsons::active->getHandle(ctx.r3.u32);
    if(!object || object->type!=Simpsons::KernelHandle::Type::Timer) {ctx.r3.u64=0xc0000008;return;}
    if(ctx.r5.u32) PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"Guest timer callback requires APC delivery");
    if(ctx.r6.u32!=1 || ctx.r10.u32!=0) PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"Unsupported NtSetTimerEx control arguments");
    if(!ctx.r4.u32 || ctx.r9.s32<0) {ctx.r3.u64=0xc000000d;return;}
    LARGE_INTEGER due{}; due.QuadPart=int64_t(PPC_LOAD_U64(ctx.r4.u32));
    static auto set=native<NtSetTimerFn>("NtSetTimer");
    ctx.r3.u64=uint32_t(set(object->native,&due,nullptr,nullptr,ctx.r8.u32!=0,ctx.r9.s32,nullptr));
}
PPC_FUNC(__imp__NtCancelTimer) {
    auto object=Simpsons::active->getHandle(ctx.r3.u32);
    if(!object || object->type!=Simpsons::KernelHandle::Type::Timer) {ctx.r3.u64=0xc0000008;return;}
    uint32_t output=ctx.r4.u32;
    if(output) PPCGuestPointer(base,output,4,true);
    BOOLEAN state=FALSE;
    static auto cancel=native<NtCancelTimerFn>("NtCancelTimer");
    LONG status=cancel(object->native,&state);
    if(status>=0 && output) PPC_STORE_U32(output,state?1:0);
    ctx.r3.u64=uint32_t(status);
}
