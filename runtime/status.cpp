#include "runtime.h"

PPC_FUNC(__imp__RtlNtStatusToDosError) {
    if(!Simpsons::active || base!=Simpsons::active->base)
        throw Simpsons::Failure("Invalid native status-conversion runtime");
    Simpsons::active->checkRunning();
    struct HostState {
        const uint32_t fp=PPCFPSCRRegister::getcsr();
        const DWORD error=GetLastError();
        HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
        ~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
    } hostState;
    using Convert=ULONG(NTAPI*)(LONG);
    static const auto convert=[] {
        const auto proc=GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlNtStatusToDosError");
        if(!proc) throw Simpsons::Failure("Windows RtlNtStatusToDosError is unavailable");
        return reinterpret_cast<Convert>(proc);
    }();
    // Real Windows mapping for the native platform's NTSTATUS results. The
    // original 82433B98 wrapper still owns its conditional guest thread-error
    // write. No guest TLS, pointer output, or invented error table here.
    ctx.r3.u64=convert(ctx.r3.s32);
}
