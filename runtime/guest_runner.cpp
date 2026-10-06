#include "runtime.h"
#include "ppc_recomp_shared.h"
#include "stall_profiler.h"
namespace Simpsons {
namespace {
// Keep SEH in separate functions: the C++ wrapper owns FP restoration even
// when this layer catches a hardware exception and returns/throws a failure.
int runOriginalChecked(PPCContext& ctx,uint8_t* base) {
    __try { _xstart(ctx,base); return 4; }
    __except(exceptionFilter(GetExceptionInformation())) { return 3; }
}
uint32_t runThreadEntryChecked(PPCContext& ctx,uint8_t* base,uint32_t address) {
    __try { PPC_CALL_INDIRECT_FUNC(address); return ctx.r3.u32; }
    __except(exceptionFilter(GetExceptionInformation())) { throw Failure("Native exception in original worker thread"); }
}
}
int runOriginal(PPCContext& ctx,uint8_t* base) {
    PPCGuestFloatingPointScope floatingPoint(ctx.fpscr);
    StallProfiler::ThreadScope profiling(&ctx,true);
    try {
        const auto result=runOriginalChecked(ctx,base);
        if(result==3) unwindAudioReaderCall(ctx);
        return result;
    }catch(...) {unwindAudioReaderCall(ctx);throw;}
}
uint32_t runThreadEntry(PPCContext& ctx,uint8_t* base,uint32_t address) {
    PPCGuestFloatingPointScope floatingPoint(ctx.fpscr);
    StallProfiler::ThreadScope profiling(&ctx);
    try {return runThreadEntryChecked(ctx,base,address);}
    catch(...) {unwindAudioReaderCall(ctx);throw;}
}
}
