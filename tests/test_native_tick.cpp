// Real Windows clock tests; synthetic boundary values below test arithmetic only.
#include "runtime/runtime.h"
#ifndef SIMPSONS_NATIVE_TICK_STANDALONE
#include "runtime/engine_cpu_calls.h"
#include "runtime/frame_timing.h"
#include <sstream>
#endif
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <type_traits>

void SimpsonsNativeGetTickCount(PPCContext& ctx,uint8_t* base);
namespace {
size_t checks=0;
void need(bool ok,const char* why){++checks;if(!ok) throw std::runtime_error(why);}
void bounded(uint64_t result,uint64_t before,uint64_t after) {
    need(after>=before && after-before<0x100000000ull,"Invalid real Windows observation interval");
    need(result<=0xffffffffull,"Tick leaf must zero-extend lwz result");
    need(uint32_t(uint32_t(result)-uint32_t(before))<=after-before,"Tick outside bracketing real GetTickCount64 reads");
}
void clockAbi() {
    struct Page {
        uint8_t* pointer=static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS));
        ~Page(){if(pointer) VirtualFree(pointer,0,MEM_RELEASE);}
    } inaccessible;
    need(inaccessible.pointer!=nullptr,"Guard-page allocation failed");
    static_assert(std::is_trivially_copyable_v<PPCContext>);
    const uint32_t saved=PPCFPSCRRegister::getcsr();
    struct Restore {uint32_t value;~Restore(){PPCFPSCRRegister::restoreHostCSR(value);}} restore{saved};
    for(uint32_t controls:{0x1f80u,0x3fc0u,0x5f80u,0x9fc0u,0xe07fu}) {
        for(unsigned i=0;i<32;++i) {
            PPCContext ctx{},expected{};
            // Only r3 is read/written by the helper; all other bytes are sentinels.
            std::memset(&ctx,0xa5,sizeof(ctx));ctx.r1.u64=0;ctx.lr=0xfedcba988232deacull;
            ctx.fpscr.csr=0xe07f;std::memcpy(&expected,&ctx,sizeof(ctx));
            const uint64_t before=GetTickCount64();
            PPCFPSCRRegister::restoreHostCSR(controls);
            SimpsonsNativeGetTickCount(ctx,i&1?inaccessible.pointer:nullptr);
            const uint32_t returnedControls=PPCFPSCRRegister::getcsr();
            PPCFPSCRRegister::restoreHostCSR(saved);
            const uint64_t after=GetTickCount64();
            bounded(ctx.r3.u64,before,after);
            need(returnedControls==controls,"Tick leaked caller MXCSR controls/status");
            expected.r3.u64=ctx.r3.u64;
            need(!std::memcmp(&ctx,&expected,sizeof(ctx)),"Tick changed context beyond r3 (including guest FPSCR/SP/LR)");
        }
    }
    std::puts("PASS real low32 native clock bounds / whole-context ABI / no guest memory / five MXCSR profiles");
}
void progress() {
    PPCContext ctx{};SimpsonsNativeGetTickCount(ctx,nullptr);const uint32_t initial=ctx.r3.u32;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    uint32_t elapsed=0;
    while(elapsed<32) {
        need(std::chrono::steady_clock::now()<deadline,"Real clock did not progress before independent deadline");
        Sleep(1);
        const uint64_t before=GetTickCount64();SimpsonsNativeGetTickCount(ctx,nullptr);
        const uint64_t after=GetTickCount64();bounded(ctx.r3.u64,before,after);
        const uint32_t next=ctx.r3.u32-initial;
        need(next>=elapsed && next<0x80000000u,"Clock regressed within finite native interval");elapsed=next;
    }
    std::printf("PASS real clock progress: %u milliseconds (no forced timer resolution)\n",elapsed);
}
void arithmetic() {
    // Original addi is 64-bit; lwz/stw/cmplw consumers use the low 32 bits.
    constexpr std::array<uint64_t,10> values{0,1,9999,10000,0x7fffffffull,0xffffd8e8ull,
        0xfffffff0ull,0xffffffffull,0x100000000ull,0x1234567800000020ull};
    for(uint64_t native:values) {
        const uint32_t raw=uint32_t(native);const uint64_t added=uint64_t(raw)+10000;
        need(uint32_t(added)==uint32_t((native+10000)&0xffffffffull),"Low32 biased-clock arithmetic");
    }
    need(uint32_t(0x10u-0xfffffff0u)==32,"Modulo32 elapsed delta across wrap");
    const uint32_t now=0xffffd8e8u,biased=uint32_t(uint64_t(now)+10000),deadline=biased+32;
    need(biased==0xfffffff8u && deadline==0x18,"Biased deadline wrap fixture");
    // 8232977C compares absolute unsigned words: this IS early expiry across
    // wrap. Do not quietly fix retained original consumers or claim wrap safety.
    need(biased>=deadline && uint32_t(deadline-biased)==32,"Original unsigned deadline caveat lost");
    std::puts("PASS synthetic low32/addi/storage/wrap arithmetic (not an artificial clock advance)");
}
#ifndef SIMPSONS_NATIVE_TICK_STANDALONE
void originalConsumers(const char* image) {
    Simpsons::Runtime rt;rt.load(image);
    rt.map(0x10000,0x10000,true,"Native tick fixture stack");
    auto* base=rt.base;
    const std::array<uint32_t,4> leaf{0x3d608200,0x816b07f0,0x806b0010,0x4e800020};
    for(unsigned i=0;i<leaf.size();++i) need(PPC_LOAD_U32(0x824324a8+4*i)==leaf[i],"Original tick leaf pin");
    // load() alone does not enable import checking (initialize normally does).
    // Enable the real guard without bootstrapping unrelated game services.
    rt.checkingImports=true;
    auto guarded=[&] {
        try {rt.pointer(0x820007f0,4,false);}
        catch(const Simpsons::Failure& e) {
            need(std::string(e.what())=="Unimplemented data import: KeTimeStampBundle","Unexpected raw import rejection");return;
        }
        need(false,"Unqualified timestamp bundle import became readable");
    };
    guarded();
    // Read-only assertion view of the loaded image, not a guest service access.
    std::array<uint8_t,4> bundle{};std::memcpy(bundle.data(),base+0x820007f0,bundle.size());
    PPCContext entry{};entry.r1.u64=0x20000;entry.lr=0x11223344;
    Simpsons::EngineCpuCalls cpu(entry,base);auto& ctx=cpu.registers();
    const auto preserved=std::array{&ctx.r14,&ctx.r15,&ctx.r16,&ctx.r17,&ctx.r18,&ctx.r19,&ctx.r20,&ctx.r21,
        &ctx.r22,&ctx.r23,&ctx.r24,&ctx.r25,&ctx.r26,&ctx.r27,&ctx.r28,&ctx.r29,&ctx.r30,&ctx.r31};
    for(size_t i=0;i<preserved.size();++i) preserved[i]->u64=0xfedcba9800000000ull+i;
    for(uint32_t address:{0x8232a320u,0x8232dd38u}) {
        const uint32_t call=address==0x8232a320?0x4810817d:0x48104765;
        const std::array<uint32_t,9> words{0x7d8802a6,0x9181fff8,0x9421ffa0,call,0x38632710,
            0x38210060,0x8181fff8,0x7d8803a6,0x4e800020};
        for(unsigned i=0;i<words.size();++i) need(PPC_LOAD_U32(address+4*i)==words[i],"Original biased-clock wrapper pin");
        const uint64_t sp=ctx.r1.u64,lr=ctx.lr;const uint32_t fpscr=ctx.fpscr.csr;
        const uint64_t before=GetTickCount64();cpu.invoke(address);const uint64_t after=GetTickCount64();
        need(ctx.r3.u64>=10000 && ctx.r3.u64<=0xffffffffull+10000,"Original addi result width/bias changed");
        bounded(ctx.r3.u64-10000,before,after);
        need(ctx.r1.u64==sp && ctx.lr==lr && ctx.fpscr.csr==fpscr,"Original wrapper SP/LR/FPSCR ABI changed");
        for(size_t i=0;i<preserved.size();++i) need(preserved[i]->u64==0xfedcba9800000000ull+i,"Original wrapper nonvolatile GPR changed");
    }
    need(!std::memcmp(base+0x820007f0,bundle.data(),bundle.size()),"Tick populated/changed guarded timestamp data import");
    guarded();
    std::puts("PASS actual AOT biased-clock wrappers8232A320/8232DD38 / no timestamp-bundle publication");
}
#endif
}
#ifndef SIMPSONS_NATIVE_TICK_STANDALONE
#include "header/test_frame_timing.h"
#endif
int main(int argc,char** argv) {
    try {
        clockAbi();progress();arithmetic();
#ifndef SIMPSONS_NATIVE_TICK_STANDALONE
        need(argc==2,"Original image path required for integrated tick test");originalConsumers(argv[1]);frameTimingModes();
#else
        (void)argv;need(argc==1,"Standalone tick test takes no image argument");
        std::puts("Standalone scope: original caller words checked by evidence.py; AOT integration runs in main target");
#endif
        std::printf("PASS native millisecond tick: %zu checks\n",checks);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL native tick after %zu checks: %s\n",checks,e.what());return 1;}
}
