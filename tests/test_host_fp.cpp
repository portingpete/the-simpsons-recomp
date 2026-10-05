// Compiled by test_host_fp.py against verbatim production headers/wrappers.
#include "engine_cpu_calls.h"
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>

namespace Simpsons {
Runtime* active{};
thread_local PPCContext* currentContext{};
LONG exceptionFilter(EXCEPTION_POINTERS* info) {
    return info->ExceptionRecord->ExceptionCode==0xE0424242u
        ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
}
uint8_t* PPCGuestPointer(uint8_t*,uint32_t address,unsigned width,bool write) {
    return Simpsons::active->pointer(address,width,write);
}
[[noreturn]] void PPCRecompFailure(const PPCContext&,uint32_t,const char* reason) {
    throw Simpsons::Failure(reason);
}
namespace {
constexpr uint32_t masks=0x1F80,flush=0x8040,roundMask=0x6000;
constexpr uint32_t host=masks|0x2000|0x20; // Downward, sticky precision flag.
unsigned checks{};
int behavior{};
bool hostDestructorRan{};
void require(bool value,const char* reason) {
    ++checks;
    if(!value) {
        // Safe diagnostic/unwind even when demonstrating the old zero-cache bug.
        _mm_setcsr(masks);
        throw Simpsons::Failure(reason);
    }
}
void masked() {require((_mm_getcsr()&masks)==masks,"PPC FPSCR update unmasked host FP exceptions");}
__declspec(noinline) float divide(float a,float b) {
    return _mm_cvtss_f32(_mm_div_ss(_mm_set_ss(a),_mm_set_ss(b)));
}
__declspec(noinline) float add(float a,float b) {
    return _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(a),_mm_set_ss(b)));
}
void arithmetic() {
    masked();
    volatile float value=divide(1.0f,10.0f);
    require(value>0 && (_mm_getcsr()&0x20),"Inexact arithmetic did not execute/set sticky precision");
}
struct Restore {uint32_t saved=_mm_getcsr();~Restore(){_mm_setcsr(saved);}};
struct HostDestructor {
    ~HostDestructor() {
        if(_mm_getcsr()!=host) std::abort();
        volatile float value=divide(1.0f,10.0f);
        hostDestructorRan=value>0;
    }
};
void guest(PPCContext& ctx,uint8_t*) {
    masked();
    require((_mm_getcsr()&(roundMask|flush))==(ctx.fpscr.csr&(roundMask|flush)),
            "Native entry did not activate the callee guest FP controls");
    ctx.fpscr.storeFromGuest(PPC_ROUND_UP);
    ctx.fpscr.enableFlushMode();
    arithmetic();
    ctx.r3.u32=73;
    if(behavior==1) throw Simpsons::Failure("intentional guest failure");
    if(behavior==2) RaiseException(0xE0424242u,0,0,nullptr);
}
void nested(PPCContext& ctx,uint8_t* base) {
    ctx.fpscr.storeFromGuest(PPC_ROUND_TOWARD_ZERO);
    ctx.fpscr.disableFlushModeUnconditional();
    const uint32_t outer=_mm_getcsr();
    {
        Simpsons::EngineCpuCalls cpu(ctx,base);
        require(cpu.invoke(0x104)==73,"Nested guest result lost");
        require(_mm_getcsr()==outer,"Nested callback lost caller guest FP environment");
        require((cpu.registers().fpscr.csr&roundMask)==0x4000,"Callback lost its own guest rounding state");
    }
    require((ctx.fpscr.csr&roundMask)==0x6000,"Callback overwrote caller guest FPSCR cache");
    ctx.r3.u32=73;
}
alignas(64) std::array<uint8_t,0x1000> memory{};
void setup(PPCContext& ctx) {
    ctx.r1.u32=0xF00;
    PPC_LOOKUP_FUNC(memory.data(),0x100)=guest;
    PPC_LOOKUP_FUNC(memory.data(),0x104)=guest;
    PPC_LOOKUP_FUNC(memory.data(),0x108)=nested;
}
void boundaries(const char* which) {
    for(int mode=0;mode<2;++mode) {
        PPCContext ctx{};setup(ctx);behavior=mode;
        ctx.fpscr.storeFromGuest(PPC_ROUND_NEAREST);
        ctx.fpscr.disableFlushModeUnconditional();
        _mm_setcsr(host);
        bool caught=false;
        try {
            if(!strcmp(which,"main")) require(Simpsons::runOriginal(ctx,memory.data())==4,"Main result changed");
            else if(!strcmp(which,"worker")) require(Simpsons::runThreadEntry(ctx,memory.data(),0x100)==73,"Worker result changed");
            else {
                Simpsons::EngineCpuCalls cpu(ctx,memory.data());
                require(cpu.invoke(0x100)==73,"Callback result changed");
                require(_mm_getcsr()==host,"Callback restored too late (at frame destruction)");
                require((cpu.registers().fpscr.csr&roundMask)==0x4000,"Callback did not preserve guest state");
                require(cpu.invoke(0x100)==73 && _mm_getcsr()==host,
                        "Repeated callback did not reactivate/restore its guest environment");
            }
        } catch(const Simpsons::Failure& e) {
            if(strcmp(e.what(),"intentional guest failure")) throw;
            caught=true;
        }
        require(caught==bool(mode),"Guest exception was swallowed/changed");
        require(_mm_getcsr()==host,"Native boundary leaked guest MXCSR on return/throw");
        require(Simpsons::currentContext==nullptr,"Callback context not restored");
    }
}
}
PPC_FUNC(_xstart) {guest(ctx,base);}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    Restore restore;
    Simpsons::Runtime runtime{memory.data()};Simpsons::active=&runtime;
    try {
        require(argc==2,"Expected one test mode");
        const char* mode=argv[1];
        if(!strcmp(mode,"helpers")) {
            _mm_setcsr(masks);
            PPCContext ctx{};
            ctx.fpscr.enableFlushMode();masked();
            require((_mm_getcsr()&flush)==flush,"VMX did not enable FTZ/DAZ");
            ctx.fpscr.disableFlushMode();masked();
            require(!(_mm_getcsr()&flush),"Scalar FP did not disable FTZ/DAZ");
            for(unsigned rounding=0;rounding<4;++rounding) {
                ctx.fpscr.csr=0;
                ctx.fpscr.storeFromGuest(rounding);masked();
                ctx.fpscr.csr=0;ctx.fpscr.enableFlushModeUnconditional();masked();
                ctx.fpscr.csr=flush;ctx.fpscr.disableFlushModeUnconditional();masked();
            }
        } else if(!strcmp(mode,"arithmetic")) {
            PPCContext ctx{};
            ctx.fpscr.enableFlushMode();arithmetic();
            ctx.fpscr.disableFlushModeUnconditional();arithmetic();
        } else if(!strcmp(mode,"rounding")) {
            // Tie above 1.0: nearest/zero/down -> 1, up -> next float.
            constexpr uint32_t expected[]={0x3F800000,0x3F800000,0x3F800001,0x3F800000};
            PPCContext ctx{};
            for(unsigned rounding=0;rounding<4;++rounding) {
                ctx.fpscr.storeFromGuest(rounding);masked();
                require(ctx.fpscr.loadFromHost()==rounding,"Guest rounding encoding changed");
                require(std::bit_cast<uint32_t>(add(1.0f,0x1p-24f))==expected[rounding],"Guest rounding arithmetic changed");
            }
            ctx.fpscr.storeFromGuest(0);
            ctx.fpscr.enableFlushMode();
            require(std::bit_cast<uint32_t>(divide(0x1p-126f,2))==0,"VMX FTZ not active");
            ctx.fpscr.disableFlushMode();
            require(std::bit_cast<uint32_t>(divide(0x1p-126f,2))==0x00400000,"Scalar gradual underflow lost");
        } else if(!strcmp(mode,"main") || !strcmp(mode,"worker") || !strcmp(mode,"callback")) {
            boundaries(mode);
        } else if(!strcmp(mode,"nested")) {
            PPCContext ctx{};setup(ctx);_mm_setcsr(host);
            require(Simpsons::runThreadEntry(ctx,memory.data(),0x108)==73,"Nested result changed");
            require(_mm_getcsr()==host,"Outer host environment not restored");
        } else if(!strcmp(mode,"seh")) {
            PPCContext ctx{};setup(ctx);behavior=2;_mm_setcsr(host);
            require(Simpsons::runOriginal(ctx,memory.data())==3,"Main SEH contract changed");
            require(_mm_getcsr()==host,"SEH exit leaked MXCSR");
            bool caught=false;
            try {Simpsons::runThreadEntry(ctx,memory.data(),0x100);}
            catch(const Simpsons::Failure&) {caught=true;}
            require(caught && _mm_getcsr()==host,"Worker SEH exit leaked MXCSR");
        } else if(!strcmp(mode,"unwind")) {
            PPCContext ctx{};setup(ctx);behavior=1;_mm_setcsr(host);
            bool caught=false;
            try {HostDestructor destructor;Simpsons::runOriginal(ctx,memory.data());}
            catch(const Simpsons::Failure&) {caught=true;}
            require(caught && hostDestructorRan,"Host cleanup did not complete before outer catch");
        } else require(false,"Unknown mode");
        std::printf("PASS %s / %u checks\n",mode,checks);
        return 0;
    } catch(const std::exception& e) {
        _mm_setcsr(masks);
        std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;
    }
}
