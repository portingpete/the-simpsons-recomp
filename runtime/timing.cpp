#include "runtime.h"
#include "frame_timing.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace Simpsons {
void autoDefeatLocEnemiesFrame(PPCContext&,uint8_t*);
void firstMissionCompletionFrame(PPCContext&,uint8_t*);
}

void Simpsons::Runtime::GameplayTiming::sample(uint32_t clockOwner,uint64_t now) {
    if(!owner || owner!=clockOwner)throw Failure("Invalid native gameplay clock owner");
    // Some original movement/camera consumers divide by dt. Even two samples
    // in the same 20ns tick need a positive step, not a whole display refresh.
    elapsedSeconds=double(std::max<uint64_t>(now-previousTimebase,1))/Runtime::timebaseFrequency;
    previousTimebase=now;
}
double Simpsons::Runtime::GameplayTiming::advance(double maxSeconds) {
    const double step=std::min(elapsedSeconds,maxSeconds);
    totalSeconds+=step;
    return step;
}
void Simpsons::Runtime::GameplayTiming::reset(uint64_t now) {
    previousTimebase=now;elapsedSeconds=totalSeconds=0;
}
namespace {
Simpsons::Runtime& gameplayRuntime(uint8_t* base) {
    auto* rt=Simpsons::active;
    if(!rt || rt->base!=base)throw Simpsons::Failure("Invalid native gameplay clock context");
    return *rt;
}
}
void SimpsonsNativeGameplayClockInitialize(PPCContext& ctx,uint8_t* base) {
    auto& clock=gameplayRuntime(base).gameplayTiming;
    if(ctx.r31.u32!=PPCLoadU32(base,0x82D572D0))
        throw Simpsons::Failure("Native gameplay clock constructor owner mismatch");
    clock.owner=ctx.r31.u32;clock.previousTimebase=PPCQueryTimebase();clock.elapsedSeconds=0;
}
void SimpsonsNativeGameplayClockSample(PPCContext& ctx,uint8_t* base) {
    // This runs before the original enabled check, so disabled updates cannot
    // accumulate a backlog to replay when gameplay resumes.
    auto& clock=gameplayRuntime(base).gameplayTiming;
    if(ctx.r30.u32!=PPCLoadU32(base,0x82D572D0))
        throw Simpsons::Failure("Native gameplay clock update owner mismatch");
    clock.sample(ctx.r30.u32,PPCQueryTimebase());
    Simpsons::firstMissionCompletionFrame(ctx,base);
}
void SimpsonsNativeGameplayClockReset(PPCContext&,uint8_t* base) {
    gameplayRuntime(base).gameplayTiming.reset(PPCQueryTimebase());
}
void SimpsonsNativeGameplayClockAdvance(PPCContext& ctx,uint8_t* base) {
    auto& clock=gameplayRuntime(base).gameplayTiming;
    ctx.fpscr.disableFlushMode(); // Matches the skipped original floating-point block.
    const double refreshMs=std::bit_cast<float>(PPCLoadU32(base,0x82D6CA9C));
    if(!std::isfinite(refreshMs) || refreshMs<=0)
        throw Simpsons::Failure("Invalid original gameplay refresh duration");
    // Keep the original maximum catch-up (normally five refreshes), but remove
    // rounding and the one-refresh minimum that made 120 FPS run at 2x speed.
    const auto maxRefreshes=std::max(PPCLoadU32(base,0x82CED740),1u);
    const auto previousMs=uint64_t(clock.totalSeconds*1000.0+0.5);
    const double step=clock.advance(double(maxRefreshes)*refreshMs/1000.0);
    const auto storeFloat=[&](uint32_t address,double value) {
        PPCStoreU32(base,address,std::bit_cast<uint32_t>(float(value)));
    };
    const double totalMs=clock.totalSeconds*1000.0;
    const auto roundedMs=uint64_t(totalMs+0.5);
    // Integer compatibility clocks round the total, never each frame. Their
    // low words retain the original modulo-32-bit storage semantics.
    PPCStoreU32(base,0x82D572A8,uint32_t(uint64_t(totalMs/refreshMs+0.5)));
    storeFloat(0x82D572AC,0); // No refresh-quantization error to carry forward.
    PPCStoreU32(base,0x82D572B0,uint32_t(roundedMs));
    storeFloat(0x82D572B4,clock.totalSeconds);
    storeFloat(0x82CED744,step);
    // Resume at 82690EC0. Original code still owns pause/slow motion, scaled
    // microsecond accumulation, update count, owner timestamp and the ABI.
    ctx.r28.s64=int32_t(0x82D70000u);ctx.r31.s64=int32_t(0x82CF0000u);
    // The tail also publishes integer dt (82CED74C) for input hold timers.
    // Carry sub-ms fractions through the total: 120 FPS must alternate 8/9ms
    // instead of losing a third of a millisecond on every update.
    ctx.f28.f64=double(float(double(roundedMs-previousMs)/refreshMs));
    ctx.f29.f64=0.5;ctx.f30.f64=double(0.001f);
}

void SimpsonsNativeWorldCollisionStep(PPCContext& ctx,uint8_t*) {
    // The small-frame path otherwise integrates 33.3667ms ahead, then skips
    // collision passes until gameplay catches up. Use the same smoothed dt
    // that 827A55C0 supplied to Havok's frame target so contacts update with
    // the character. The original large-frame substep loop stays in charge.
    ctx.f1.f64=std::min(ctx.f1.f64,ctx.f31.f64);
}

// Measurement surrounds the original wait body; it changes no clock,
// deadline, interval, register or scheduler request.
void SimpsonsNativeFrameWaitBegin(PPCContext& ctx,uint8_t* base) {
    const DWORD error=GetLastError();Simpsons::FrameTiming::beginPacing(ctx,base);SetLastError(error);
}
void SimpsonsNativeFrameWaitEnd(PPCContext&,uint8_t*) {Simpsons::FrameTiming::endPacing();}

// Configure the original scheduler through its real constructor argument.
// The original body still creates every field, callback and timestamp.
void SimpsonsNativeFrameSchedulerConfigure(PPCContext& ctx,uint8_t* base) {
    auto* rt=Simpsons::active;
    if(!rt || rt->base!=base)throw Simpsons::Failure("Invalid native frame scheduler context");
    if(rt->uncappedFrameRate) {
        if(uint32_t(ctx.lr)!=0x82867A78 || ctx.r4.u32!=2)
            throw Simpsons::Failure("Native uncapped frame rate requires the original two-interval scheduler constructor");
        ctx.r4.u64=0;
        std::fprintf(stderr,"[NATIVE FRAME RATE] original scheduler configured for zero intervals (uncapped 120+ FPS testing; real dt preserved)\n");
        return;
    }
    if(!rt->requestedFrameRate)return;
    if(!Simpsons::NativeVideoSettings::validFrameRate(rt->requestedFrameRate))throw Simpsons::Failure("Invalid native frame rate");
    if(uint32_t(ctx.lr)!=0x82867A78 || ctx.r4.u32!=2)
        throw Simpsons::Failure("Native frame rate requires the original two-interval scheduler constructor");
    if(rt->requestedFrameRate!=60) {
        // Native presentation pacing expresses these limits independently of
        // the original integer-refresh scheduler and the display refresh rate.
        ctx.r4.u64=0;
        std::fprintf(stderr,"[NATIVE FRAME RATE] original scheduler bypassed with native %.6gms floor (%u FPS target; real dt preserved)\n",1000.0/rt->requestedFrameRate,rt->requestedFrameRate);
        return;
    }
    ctx.r4.u64=1;
    std::fprintf(stderr,"[NATIVE FRAME RATE] original scheduler configured for one refresh interval (60 FPS target)\n");
}
// Native limiter with an even cadence (see frame_pacer.h). Called at present
// entry, before the present is submitted: planned releases are separated by the
// selected interval, or a high percentile of recent natural frame times when the
// machine cannot sustain the limit. A timer overshoot can shorten the next actual
// interval as the planned cadence catches up. (The old
// version slept after submission, measured from this present's own entry, which added
// the whole floor to every frame.) Guest clocks, scheduler requests and simulation dt
// are untouched; only the owner thread waits, on a high-resolution timer with a
// processor-yield tail. SIMPSONS_PACE_PERCENTILE (50-100, default 85) tunes the target.
void Simpsons::SimpsonsNativeFramePace(Simpsons::Runtime& runtime) {
    const uint32_t rate=runtime.requestedFrameRate;
    if(!rate||!Simpsons::NativeVideoSettings::validFrameRate(rate))throw Simpsons::Failure("Invalid native presentation limit");
    // Ceiling division keeps the actual limit at or below the selected rate.
    const uint64_t floor=Simpsons::Runtime::timebaseFrequency/rate+
        (Simpsons::Runtime::timebaseFrequency%rate?1:0);
    const uint64_t ceiling=Simpsons::Runtime::timebaseFrequency/10; // A pause or stall never raises the target.
    static const unsigned percentile=[]{const char* text=std::getenv("SIMPSONS_PACE_PERCENTILE");return text?unsigned(std::atoi(text)):85u;}();
    struct PaceTimer {
        HANDLE handle=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
        ~PaceTimer(){if(handle)CloseHandle(handle);}
        void sleep100ns(long long units) const {
            if(units<=0)return;
            if(!handle){Sleep(1);return;} // Older Windows retains a coarser but correct wait.
            LARGE_INTEGER due{};due.QuadPart=-units;
            if(!SetWaitableTimerEx(handle,&due,0,nullptr,nullptr,nullptr,0))return;
            if(WaitForSingleObject(handle,1000)!=WAIT_OBJECT_0)return;
        }
    };
    thread_local PaceTimer timer;
    const uint64_t release=runtime.framePacer.release(PPCQueryTimebase(),floor,ceiling,percentile);
    for(;;) {
        const uint64_t now=PPCQueryTimebase();
        if(now-release>(~uint64_t(0)>>1))  // Unsigned: now < release (50MHz wrap is still ordered).
        {
            // 50MHz ticks are 20ns; five ticks make one 100ns timer unit.
            const uint64_t remain100ns=(release-now)/5;
            if(remain100ns>10000)timer.sleep100ns(static_cast<long long>(remain100ns-10000)); // Keep the last 1ms for the yield tail.
            else if(remain100ns>500)timer.sleep100ns(static_cast<long long>(remain100ns-500));
            else YieldProcessor();
        } else return;
    }
}
// Original82718788 resets the scheduler after Present. Include the preceding
// native presentation cost in the next deadline by using its real entry time.
// This is a frame-boundary choice, never a scaled or fabricated clock value.
void SimpsonsNativeFrameSchedulerStamp(PPCContext& ctx,uint8_t* base) {
    auto* rt=Simpsons::active;
    if(!rt || rt->base!=base)throw Simpsons::Failure("Invalid native frame boundary context");
    if(uint32_t(ctx.lr)!=0x826B83D4)return;
    const bool stampTimebase=(rt->requestedFrameRate || rt->uncappedFrameRate) && rt->nativePresentationTimebase;
    const bool defeatEnemies=rt->autoDefeatLocEnemies &&
        rt->landOfChocolateStreamSeen.load(std::memory_order_acquire);
    if(!stampTimebase && !defeatEnemies)return;
    if(ctx.r3.u32!=PPCLoadU32(base,0x82D576A0))
        throw Simpsons::Failure("Native frame boundary differs from the original scheduler owner");
    if(stampTimebase)ctx.r11.u64=rt->nativePresentationTimebase;
    if(defeatEnemies)Simpsons::autoDefeatLocEnemiesFrame(ctx,base);
}

PPC_FUNC(__imp__KeQuerySystemTime) {
    PPCGuestPointer(base,ctx.r3.u32,8,true);
    FILETIME now{};
    GetSystemTimePreciseAsFileTime(&now);
    PPC_STORE_U64(ctx.r3.u32,(uint64_t(now.dwHighDateTime)<<32)|now.dwLowDateTime);
}

namespace {
using NtDelayFn=LONG(NTAPI*)(BOOLEAN,PLARGE_INTEGER);
using NtWaitFn=LONG(NTAPI*)(HANDLE,BOOLEAN,PLARGE_INTEGER);
using NtYieldFn=LONG(NTAPI*)();
template<class T> T service(const char* name) {
    HMODULE mod=GetModuleHandleW(L"ntdll.dll");
    if(!mod) throw Simpsons::Failure("ntdll.dll module handle unavailable");
    auto address=GetProcAddress(mod,name);
    if(!address) throw Simpsons::Failure(std::string("Required native timing service is missing: ")+name);
    T fn{};std::memcpy(&fn,&address,sizeof(fn));
    return fn;
}
}
PPC_FUNC(__imp__NtYieldExecution) {
    (void)base;
    if(!Simpsons::active) throw Simpsons::Failure("Invalid yield runtime");
    Simpsons::active->checkRunning();
    // Original82B76B98 compares the exact NT status against40000024 before
    // producing its Boolean. Preserve the real host scheduler result, including
    // STATUS_NO_YIELD_PERFORMED; do not turn a failed yield into success.
    // Native ABI: NTSTATUS NTAPI NtYieldExecution(void), phnt/ntkeapi.h.
    const auto error=GetLastError();const auto fp=PPCFPSCRRegister::getcsr();
    struct Restore {
        DWORD error;uint32_t fp;
        ~Restore(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
    } restore{error,fp};
    PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);
    static auto yield=service<NtYieldFn>("NtYieldExecution");
    const uint32_t status=uint32_t(yield());
    Simpsons::active->checkRunning();
    ctx.r3.u64=status;
}
PPC_FUNC(__imp__RtlTimeToTimeFields) {
    struct HostState {uint32_t fp=PPCFPSCRRegister::getcsr();DWORD error=GetLastError();
        HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}} host;
    auto* rt=Simpsons::active;
    if(!rt||rt->base!=base)throw Simpsons::Failure("Invalid native time conversion runtime");rt->checkRunning();
    if(!ctx.r3.u32||!ctx.r4.u32)throw Simpsons::Failure("Absent native time conversion buffer");
    LARGE_INTEGER time{};time.QuadPart=int64_t(PPC_LOAD_U64(ctx.r3.u32));
    auto* output=PPCGuestPointer(base,ctx.r4.u32,16,true);
    // TIME_FIELDS is eight signed 16-bit values in this order: year, month,
    // day, hour, minute, second, milliseconds, weekday. The real NT conversion
    // consumes the same absolute100ns epoch; this is not a scaled game clock.
    struct Fields {SHORT value[8];};static_assert(sizeof(Fields)==16);
    using Convert=void(NTAPI*)(PLARGE_INTEGER,Fields*);
    static auto convert=service<Convert>("RtlTimeToTimeFields");Fields fields{};convert(&time,&fields);
    rt->checkRunning();
    for(unsigned i=0;i<8;++i){const auto value=uint16_t(fields.value[i]);output[i*2]=uint8_t(value>>8);output[i*2+1]=uint8_t(value);}
    // The original import has no return value. Preserve the complete context.
}
PPC_FUNC(__imp__KeDelayExecutionThread) {
    if(!Simpsons::active) throw Simpsons::Failure("Invalid delay runtime");
    if(ctx.r3.u32>1 || !ctx.r5.u32) {ctx.r3.u64=0xc000000d;return;}
    Simpsons::active->checkRunning();
    LARGE_INTEGER interval{};interval.QuadPart=int64_t(PPC_LOAD_U64(ctx.r5.u32));
    BOOLEAN alertable=ctx.r4.u32!=0;
    LONG status;
    if(interval.QuadPart==0) {
        // Preserve the real scheduler yield behavior of a zero-duration delay.
        static auto delay=service<NtDelayFn>("NtDelayExecution");
        status=delay(alertable,&interval);
        // This host reports STATUS_NO_YIELD_PERFORMED when no peer was ready.
        // The requested zero interval still elapsed; KeDelay reports completion.
        if(uint32_t(status)==0x40000024) status=0;
    } else {
        static auto wait=service<NtWaitFn>("NtWaitForSingleObject");
        status=wait(Simpsons::active->stopEvent,alertable,&interval);
        if(status==0) Simpsons::active->checkRunning();
        if(status==0x102) status=0; // Normal delay expiry, not an object wait timeout.
    }
    if(uint32_t(status)==0xc0) PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"Native APC woke a delay without a guest APC delivery bridge");
    ctx.r3.u64=uint32_t(status);
}

// Original 824324A8: no arguments, zero-extended 32-bit millisecond result.
// Callers retain their own +10000 bias and 32-bit deadline/wrap arithmetic.
// This replaces the leaf, never the guarded KeTimeStampBundle data import.
void SimpsonsNativeGetTickCount(PPCContext& ctx,uint8_t* base) {
    (void)base; // No guest storage or timestamp-bundle layout is accessed.
    struct HostFloatingPoint {
        const uint32_t previous=PPCFPSCRRegister::getcsr();
        HostFloatingPoint(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
        ~HostFloatingPoint(){PPCFPSCRRegister::restoreHostCSR(previous);}
    } floatingPoint;
    ctx.r3.u64=uint32_t(GetTickCount64());
}
