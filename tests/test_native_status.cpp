#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include <array>
#include <cstdio>
#include <cstring>

PPC_FUNC(__imp__RtlNtStatusToDosError);
namespace {
size_t checks=0;
void need(bool value,const char* reason) {++checks;if(!value) throw std::runtime_error(reason);}
template<class F> void rejects(F call,const char* reason) {
    bool failed=false;try {call();} catch(const std::exception&) {failed=true;}
    need(failed,reason);
}
using Convert=ULONG(NTAPI*)(LONG);
void abi(Simpsons::Runtime& rt) {
    const auto windows=reinterpret_cast<Convert>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlNtStatusToDosError"));
    need(windows!=nullptr,"Actual Windows status oracle is unavailable");
    struct Anchor {uint32_t status,error;};
    const std::array<Anchor,8> anchors={{{0,ERROR_SUCCESS},{0xc0000034,ERROR_FILE_NOT_FOUND},
        {0xc000003a,ERROR_PATH_NOT_FOUND},{0xc0000022,ERROR_ACCESS_DENIED},
        {0xc0000008,ERROR_INVALID_HANDLE},{0xc000000d,ERROR_INVALID_PARAMETER},
        {0xc0000043,ERROR_SHARING_VIOLATION},{0xc0de1234,ERROR_MR_MID_NOT_FOUND}}};
    for(const auto anchor:anchors)
        need(windows(LONG(anchor.status))==anchor.error,"Windows error mapping differs from qualified anchor");
    const uint32_t saved=PPCFPSCRRegister::getcsr();
    struct Restore {uint32_t fp;DWORD error;~Restore(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}} restore{saved,GetLastError()};
    const std::array<uint32_t,21> statuses={0,1,0x102,0x103,0x40000000,0x40000024,
        0x80000005,0x80000006,0x8000000d,0x8000001a,0xc0000001,0xc0000005,
        0xc0000008,0xc000000d,0xc0000022,0xc0000034,0xc000003a,0xc0000043,
        0xc00000bb,0xc0000120,0xc0de1234};
    for(uint32_t status:statuses) for(uint32_t controls:{0x1f80u,0x3fc0u,0x5f80u,0x9fc0u,0xe07fu}) {
        const uint32_t expectedError=windows(LONG(status));
        PPCContext ctx{},expected{};std::memset(&ctx,0xa5,sizeof(ctx));
        ctx.r3.u64=0xdeadbeef00000000ull|status;
        std::memcpy(&expected,&ctx,sizeof(ctx));expected.r3.u64=expectedError;
        SetLastError(0x12345678);PPCFPSCRRegister::restoreHostCSR(controls);
        __imp__RtlNtStatusToDosError(ctx,rt.base);
        const auto actualFp=PPCFPSCRRegister::getcsr();const auto actualError=GetLastError();
        PPCFPSCRRegister::restoreHostCSR(saved);
        need(!std::memcmp(&ctx,&expected,sizeof(ctx)),"Status conversion result/whole-context ABI differs");
        need(actualFp==controls && actualError==0x12345678,"Status conversion changed host FP/last-error state");
    }
}
void originalWrapper(Simpsons::Runtime& rt) {
    auto* base=rt.base;
    const std::array<uint32_t,13> words={0x7d8802a6,0x9181fff8,0x9421ffa0,0x4888edc1,
        0x816d0150,0x2b0b0000,0x409a000c,0x816d0100,0x906b0160,0x38210060,
        0x8181fff8,0x7d8803a6,0x4e800020};
    for(size_t i=0;i<words.size();++i)
        need(PPC_LOAD_U32(0x82433b98+uint32_t(i*4))==words[i],"Original status-wrapper instruction pin changed");
    constexpr uint32_t pcr=0x11000,thread=0x12000;
    PPC_STORE_U32(pcr+0x100,thread);
    PPCContext incoming{};incoming.r1.u64=0x20000;incoming.r13.u64=pcr;incoming.lr=0x12345678;
    Simpsons::EngineCpuCalls cpu(incoming,base);
    const auto stack=cpu.registers().r1.u64;
    for(uint32_t suppress:{0u,1u,0xffffffffu}) {
        PPC_STORE_U32(pcr+0x150,suppress);
        std::array<uint8_t,512> before{};before.fill(0xa5);
        std::memcpy(rt.pointer(thread,unsigned(before.size()),true),before.data(),before.size());
        need(cpu.invoke(0x82433b98,0xc0000034)==ERROR_FILE_NOT_FOUND,"Original wrapper did not return actual mapped file error");
        if(!suppress) {before[0x160]=0;before[0x161]=0;before[0x162]=0;before[0x163]=ERROR_FILE_NOT_FOUND;}
        need(!std::memcmp(rt.pointer(thread,unsigned(before.size()),false),before.data(),before.size()),
             "Original conditional BE thread-error write differs");
        need(cpu.registers().r1.u64==stack && cpu.registers().r13.u64==pcr,"Original status wrapper did not preserve frame/PCR");
    }
}
}
int main(int argc,char** argv) {
    try {
        need(argc==2,"Expected original image path");
        Simpsons::Runtime rt;rt.load(argv[1]);rt.map(0x10000,0x10000,true,"Native status fixtures");
        abi(rt);originalWrapper(rt);
        PPCContext ctx{};ctx.r3.u64=0xdeadbeefc0000034ull;
        rejects([&]{__imp__RtlNtStatusToDosError(ctx,nullptr);},"Foreign runtime base accepted");
        need(ctx.r3.u64==0xdeadbeefc0000034ull,"Rejected conversion changed output");
        rt.requestStop("native status fixture complete");
        rejects([&]{__imp__RtlNtStatusToDosError(ctx,rt.base);},"Cancelled runtime allowed conversion");
        need(ctx.r3.u64==0xdeadbeefc0000034ull,"Cancelled conversion changed output");
        std::printf("PASS native status mapping: %zu checks; actual Windows mappings and original conditional thread-error write\n",checks);
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL native status mapping: %s\n",error.what());return 1;}
}
