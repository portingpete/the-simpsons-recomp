#include "runtime.h"
#include <cstdio>

namespace {
struct HostState {
    const uint32_t fp=PPCFPSCRRegister::getcsr();
    const DWORD error=GetLastError();
    HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
};
[[noreturn]] void nativeFailure(const char* operation,DWORD error,uint32_t object,DWORD wait) {
    char message[192];
    std::snprintf(message,sizeof(message),
        "Native thread-exit status %s failed: object=%08X wait=%08lX Win32=%lu",
        operation,object,wait,error);
    throw Simpsons::Failure(message);
}
// Diagnostic only, once per process. Never participates in completion or status.
std::atomic<bool> completedDiagnostic=false;
}

// Original 82433358..8243336C only; resume at 82433370. The original typed
// reference, output store, dereference, error conversion and return all remain.
void SimpsonsNativeThreadExitStatus(PPCContext& ctx,uint8_t* base) {
    HostState hostState;
    if(!Simpsons::active || Simpsons::active->base!=base || Simpsons::currentContext!=&ctx)
        throw Simpsons::Failure("Invalid native thread-exit status runtime/context");
    auto& rt=*Simpsons::active;
    rt.checkRunning();
    if(ctx.lr!=0x8243334C || ctx.r1.u32<0x100 || (ctx.r1.u32&15) ||
       ctx.r4.u64!=Simpsons::Runtime::threadObjectType ||
       ctx.r5.u64!=uint64_t(ctx.r1.u32)+0x50 || ctx.r3.u64!=ctx.r3.u32 ||
       PPC_LOAD_U32(ctx.r1.u32+0x50)!=ctx.r3.u32)
        throw Simpsons::Failure("Native thread-exit status did not follow the original typed reference");

    const uint32_t address=ctx.r3.u32;
    std::shared_ptr<Simpsons::KernelHandle> owner;
    uint32_t mappedHandle=0;
    {
        std::lock_guard lock(rt.handleMutex);
        const auto found=rt.objectReferences.find(address);
        if(found==rt.objectReferences.end() || !found->second.count || !found->second.object ||
           found->second.object->guestObject!=address ||
           found->second.object->type!=Simpsons::KernelHandle::Type::Thread)
            throw Simpsons::Failure("Native thread-exit status requires a counted, matching Thread reference");
        owner=found->second.object;
        if(!owner->native || owner->native==INVALID_HANDLE_VALUE)
            throw Simpsons::Failure("Native thread-exit status reference has no native thread handle");
        // Best available handle identity at this reference snapshot. A closed
        // guest handle or the self pseudo-handle need not have a map entry.
        for(const auto& [handle,object]:rt.handles)
            if(object==owner && (!mappedHandle || handle<mappedHandle)) mappedHandle=handle;
    }
    // No registry lock spans Windows calls. The local shared owner keeps its
    // native handle and KTHREAD alive, even if the guest handle closes now.
    const DWORD wait=WaitForSingleObject(owner->native,0);
    DWORD code=STILL_ACTIVE;
    if(wait==WAIT_OBJECT_0) {
        if(!GetExitCodeThread(owner->native,&code)) nativeFailure("GetExitCodeThread",GetLastError(),address,wait);
    } else if(wait!=WAIT_TIMEOUT) {
        const DWORD error=wait==WAIT_FAILED?GetLastError():ERROR_INVALID_DATA;
        nativeFailure("WaitForSingleObject",error,address,wait);
    }
    if(wait==WAIT_OBJECT_0 && !completedDiagnostic.exchange(true,std::memory_order_relaxed)) {
        const DWORD id=GetThreadId(owner->native);
        const DWORD idError=id?ERROR_SUCCESS:GetLastError();
        std::fprintf(stderr,
            "[THREAD EXIT STATUS] first completed original query: object=0x%08X mapped_handle=0x%X "
            "native_id=%lu wait=0x%08lX status=0x%08lX id_error=%lu; "
            "mapped_handle=0 means closed/pseudo; original output/dereference/poll retained\n",
            address,mappedHandle,id,wait,code,idError);
    }
    // clrlwi. compares the nonnegative completion byte, not the exit code.
    // Even an actual final status 259 or FFFFFFFF keeps CR0.GT, XER.SO and
    // the original caller's status-code policy. No guest completion stores.
    ctx.r11.u64=uint32_t(code);
    ctx.cr0.compare<uint32_t>(wait==WAIT_OBJECT_0?1u:0u,0u,ctx.xer);
}
