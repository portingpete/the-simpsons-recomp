#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <bit>
#include <cstdio>
#include <cstring>

void SimpsonsNativeGameplayClockAdvance(PPCContext&,uint8_t*);

using namespace Simpsons;
namespace {
size_t checks{};
void need(bool value,const char* message){++checks;if(!value)throw Failure(message);}
template<class F>void rejects(F&& f){bool rejected=false;try{f();}catch(const Failure&){rejected=true;}need(rejected,"Unqualified frame-rate boundary was accepted");}

void gameplayClockHelpers() {
    constexpr uint32_t owner=0x50500;
    constexpr uint64_t frequency=Runtime::timebaseFrequency, origin=123456789;
    constexpr double cap=5.0/60.0;
    for(uint32_t rate:{30u,60u,120u,144u,240u}) {
        Runtime::GameplayTiming clock{};clock.owner=owner;clock.previousTimebase=origin;
        double movement=0,remaining=10;
        for(uint64_t frame=1;frame<=uint64_t(rate)*10;++frame) {
            clock.sample(owner,origin+frame*frequency/rate);
            const double dt=clock.advance(cap);
            movement+=6*dt;remaining-=dt;
            need(dt>0 && dt<=1.0/rate+1.0/frequency,"Gameplay delta differs from the supplied frame interval");
        }
        need(std::abs(clock.totalSeconds-10)<1e-9,"Ten real seconds did not produce ten gameplay seconds");
        need(std::abs(movement-60)<1e-8 && std::abs(remaining)<1e-9,
             "Movement or countdown speed changed with frame rate");
        std::printf("  gameplay %u FPS: %.9f seconds, %.9f distance, %.9f countdown\n",rate,clock.totalSeconds,movement,remaining);
    }
    Runtime::GameplayTiming clock{};clock.owner=owner;clock.previousTimebase=origin;
    constexpr std::array<uint64_t,7> irregular={1,250000,1000000,416666,333333,700000,12500};
    uint64_t elapsed=0;size_t frame=0;
    while(elapsed<10*frequency) {
        elapsed+=std::min(irregular[frame++%irregular.size()],10*frequency-elapsed);
        clock.sample(owner,origin+elapsed);clock.advance(cap);
    }
    need(std::abs(clock.totalSeconds-10)<1e-9,"Irregular frame intervals changed gameplay speed");
    clock.reset(origin);
    need(clock.owner==owner && clock.previousTimebase==origin && clock.totalSeconds==0 && clock.elapsedSeconds==0,
         "Gameplay reset did not clear timing while preserving its owner");
    clock.sample(owner,origin+frequency/4000);
    need(std::abs(clock.advance(cap)-0.00025)<1e-12,"Submillisecond gameplay delta was rounded to a refresh interval");
    clock.sample(owner,clock.previousTimebase);
    need(std::abs(clock.advance(cap)-1.0/frequency)<1e-15,"Same-tick gameplay sample did not retain the one-tick positive floor");
    clock.sample(owner,clock.previousTimebase+10*frequency);
    need(std::abs(clock.advance(cap)-cap)<1e-12,"Long gameplay stall exceeded the original maximum step");
    clock.sample(owner,clock.previousTimebase+frequency/120);
    need(std::abs(clock.advance(cap)-double(frequency/120)/frequency)<1e-12,
         "Gameplay clock carried discarded stall time into the following frame");
    const auto previous=clock.previousTimebase;const auto total=clock.totalSeconds;
    rejects([&]{clock.sample(owner+4,previous+frequency);});
    need(clock.previousTimebase==previous && clock.totalSeconds==total,"Rejected gameplay owner changed its timing state");
}
void gameplayIntegerTimers(Runtime& rt,EngineCpuCalls& cpu) {
    auto* base=rt.base;auto& c=cpu.registers();constexpr uint32_t owner=0x50500;
    constexpr uint64_t frequency=Runtime::timebaseFrequency,origin=123456789;
    constexpr float refreshMs=1000.0f/60.0f;
    PPC_STORE_U32(0x82D572D0,owner);PPC_STORE_U32(0x82CED740,5);
    PPC_STORE_U32(0x82D6CA9C,std::bit_cast<uint32_t>(refreshMs));
    PPCGuestFloatingPointScope floatingPoint(c.fpscr);
    for(uint32_t rate:{60u,120u,144u,240u}) {
        rt.gameplayTiming={};rt.gameplayTiming.owner=owner;rt.gameplayTiming.previousTimebase=origin;
        uint64_t timerMilliseconds=0;
        for(uint64_t frame=1;frame<=uint64_t(rate)*10;++frame) {
            rt.gameplayTiming.sample(owner,origin+frame*frequency/rate);
            SimpsonsNativeGameplayClockAdvance(c,base);
            // The original tail publishes 82CED74C with this float multiply,
            // half-up rounding and integer conversion after the hook's handoff.
            const auto milliseconds=uint32_t(float(c.f28.f64*double(refreshMs)+c.f29.f64));
            timerMilliseconds+=milliseconds;
            need(timerMilliseconds==PPC_LOAD_U32(0x82D572B0),
                 "Integer input timers lost fractional milliseconds between frames");
            const double seconds=std::bit_cast<float>(PPC_LOAD_U32(0x82CED744));
            need(seconds>0 && seconds<=1.0/rate+1.0/frequency+1e-9,
                 "Integer timer carry rounded the accurate floating-point gameplay delta");
        }
        need(timerMilliseconds==10000,"Ten real seconds did not produce 10000 integer timer milliseconds");
        need(std::abs(std::bit_cast<float>(PPC_LOAD_U32(0x82D572B4))-10.0)<1e-6,
             "Cumulative gameplay seconds diverged from integer timers");
        std::printf("  original timer %u FPS: %llu milliseconds\n",rate,static_cast<unsigned long long>(timerMilliseconds));
    }
}
void originalGameplayClock(Runtime& rt,EngineCpuCalls& cpu) {
    auto* base=rt.base;auto& c=cpu.registers();constexpr uint32_t owner=0x50500;
    constexpr uint64_t frequency=Runtime::timebaseFrequency;
    constexpr double cap=5.0/60.0;
    const auto loadFloat=[&](uint32_t at){return std::bit_cast<float>(PPC_LOAD_U32(at));};
    const auto storeFloat=[&](uint32_t at,float value){PPC_STORE_U32(at,std::bit_cast<uint32_t>(value));};
    const auto prepare=[&] {
        std::memset(rt.pointer(owner,16,true),0,16);
        std::memset(rt.pointer(0x82D572A8,0x28,true),0,0x28);
        PPC_STORE_U32(0x82D572D0,owner);PPC_STORE_U32(0x82D572DC,0);
        PPC_STORE_U32(0x82CED760,1);PPC_STORE_U32(0x82CED740,5);
        storeFloat(0x82CED750,1);storeFloat(0x82D6CA9C,1000.0f/60.0f);storeFloat(0x82CF0394,60);
        rt.gameplayTiming={};rt.gameplayTiming.owner=owner;
    };
    const auto invoke=[&] {
        c.lr=0x82690100;const auto sp=c.r1.u32;
        const std::array<uint64_t,4> savedGpr={c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64};
        const std::array<uint64_t,4> savedFpr={c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64};
        cpu.invoke(0x82690D60,owner);
        need(c.lr==0x82690100 && c.r1.u32==sp,"Gameplay clock changed the original caller stack or return address");
        need(savedGpr==std::array<uint64_t,4>{c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64} &&
             savedFpr==std::array<uint64_t,4>{c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64},
             "Gameplay clock changed nonvolatile original registers");
    };
    bool sawHighRate=false;
    for(uint32_t rate:{0u,60u,120u,240u}) {
        prepare();rt.requestedFrameRate=rate==240?0:rate;rt.uncappedFrameRate=rate==240;
        const uint64_t before=PPCQueryTimebase(),previous=before-frequency/120;
        rt.gameplayTiming.previousTimebase=previous;invoke();const uint64_t after=PPCQueryTimebase();
        const double dt=loadFloat(0x82CED744);
        need(dt>=double(before-previous)/frequency-1e-7 && dt<=std::min(double(after-previous)/frequency,cap)+1e-7,
             "Original gameplay delta does not bracket the real elapsed time");
        sawHighRate|=dt<1.0/60.0;
        need(std::abs(loadFloat(0x82CED748)-dt)<1e-7,"Original unscaled and unit-scale gameplay deltas differ");
        need(std::abs(loadFloat(0x82D572B4)-dt)<1e-7 && std::abs(rt.gameplayTiming.totalSeconds-dt)<1e-7,
             "Original cumulative gameplay seconds differ from the real delta");
        need(std::abs(double(PPC_LOAD_U32(0x82D572B0))-1000*dt)<=1,
             "Original cumulative milliseconds differ from real elapsed time");
        need(PPC_LOAD_U32(0x82CED74C)==PPC_LOAD_U32(0x82D572B0),
             "Original integer input timer differs from its first cumulative time step");
        need(std::abs(loadFloat(0x82D572C0)-dt)<2e-6 && PPC_LOAD_U32(0x82D572B8)==1,
             "Original scaled clock or frame counter did not advance exactly once");
    }
    need(sawHighRate,"Original gameplay clock still imposed a 60 Hz minimum delta");
    rt.uncappedFrameRate=false;rt.requestedFrameRate=60;

    prepare();storeFloat(0x82CED750,0.5f);
    rt.gameplayTiming.previousTimebase=PPCQueryTimebase()-frequency/120;invoke();
    need(std::abs(loadFloat(0x82CED748)-0.5*loadFloat(0x82CED744))<1e-7,
         "Original gameplay time scale no longer applies to the real delta");
    need(std::abs(loadFloat(0x82D572C0)-loadFloat(0x82CED748))<2e-6,
         "Original scaled cumulative seconds differ from the scaled delta");

    const auto scaledMicros=PPC_LOAD_U64(0x82D572C8);const auto frameCount=PPC_LOAD_U32(0x82D572B8);
    PPC_STORE_U32(0x82D572DC,1);const auto pausedStart=PPCQueryTimebase();
    rt.gameplayTiming.previousTimebase=pausedStart-frequency;invoke();
    need(PPC_LOAD_U64(0x82D572C8)==scaledMicros && PPC_LOAD_U32(0x82D572B8)==frameCount,
         "Paused original gameplay advanced its scaled clock or frame counter");
    need(rt.gameplayTiming.previousTimebase>=pausedStart,"Pause did not consume the real clock sample");
    PPC_STORE_U32(0x82D572DC,0);const auto resumedFrom=rt.gameplayTiming.previousTimebase;
    invoke();const auto resumedAt=PPCQueryTimebase();
    need(loadFloat(0x82CED744)<=std::min(double(resumedAt-resumedFrom)/frequency,cap)+1e-7,
         "Resuming original gameplay included paused wall time");

    PPC_STORE_U32(0x82CED760,0);const auto disabledStart=PPCQueryTimebase();
    const auto unscaledSeconds=rt.gameplayTiming.totalSeconds;
    const auto disabledMicros=PPC_LOAD_U64(0x82D572C8);
    rt.gameplayTiming.previousTimebase=disabledStart-frequency;invoke();
    need(rt.gameplayTiming.totalSeconds==unscaledSeconds && PPC_LOAD_U64(0x82D572C8)==disabledMicros,
         "Disabled original gameplay advanced a clock");
    need(rt.gameplayTiming.previousTimebase>=disabledStart,"Disabled gameplay did not consume the real clock sample");
    PPC_STORE_U32(0x82CED760,1);const auto enabledFrom=rt.gameplayTiming.previousTimebase;
    invoke();const auto enabledAt=PPCQueryTimebase();
    need(loadFloat(0x82CED744)<=std::min(double(enabledAt-enabledFrom)/frequency,cap)+1e-7,
         "Reenabled original gameplay included disabled wall time");

    rt.gameplayTiming.previousTimebase=PPCQueryTimebase()-10*frequency;invoke();
    need(std::abs(loadFloat(0x82CED744)-cap)<1e-7,"Original gameplay lost its five-refresh long-stall cap");
    c.lr=0x82690100;const auto resetSp=c.r1.u32;const auto resetBefore=PPCQueryTimebase();
    cpu.invoke(0x826910A0);const auto resetAfter=PPCQueryTimebase();
    need(c.r1.u32==resetSp && c.lr==0x82690100,"Original gameplay reset changed its caller ABI");
    need(rt.gameplayTiming.owner==owner && rt.gameplayTiming.totalSeconds==0 && rt.gameplayTiming.elapsedSeconds==0 &&
         rt.gameplayTiming.previousTimebase>=resetBefore && rt.gameplayTiming.previousTimebase<=resetAfter,
         "Original reset did not reset native gameplay timing against the real clock");
    need(PPC_LOAD_U32(0x82D572B0)==0 && loadFloat(0x82D572B4)==0 && PPC_LOAD_U64(0x82D572C8)==0 &&
         PPC_LOAD_U32(0x82D572B8)==0 && loadFloat(0x82D572C0)==0 && loadFloat(0x82CED750)==1,
         "Original reset changed its zero totals or restored time scale");
}

struct WorldCollisionProbe;
WorldCollisionProbe* worldCollisionProbe{};
struct WorldCollisionProbe {
    uint8_t* base;
    PPCFunc* original;
    uint32_t steps{};
    float largestStep{};
    explicit WorldCollisionProbe(uint8_t* memory):base(memory),original(PPC_LOOKUP_FUNC(memory,0x82AB9FC0)) {
        need(original && !worldCollisionProbe,"World collision fixture has an invalid dispatch binding");
        worldCollisionProbe=this;
        PPC_LOOKUP_FUNC(base,0x82AB9FC0)=step;
    }
    ~WorldCollisionProbe() {
        PPC_LOOKUP_FUNC(base,0x82AB9FC0)=original;
        worldCollisionProbe=nullptr;
    }
    static void step(PPCContext& ctx,uint8_t* base) {
        auto* probe=worldCollisionProbe;
        need(probe && probe->base==base,"World collision callback escaped its fixture");
        const uint32_t simulation=ctx.r3.u32;
        const float dt=float(ctx.f1.f64);
        need(std::isfinite(dt) && dt>0,"World collision scheduler requested a nonpositive step");
        ++probe->steps;probe->largestStep=std::max(probe->largestStep,dt);
        // Replace only the heavyweight integrate/collide phase. Its original
        // contract advances the PSI boundary (+20) by f1, then advanceTime
        // exposes min(PSI boundary, frame target). Run that latter body itself.
        const float previous=std::bit_cast<float>(PPC_LOAD_U32(simulation+20));
        PPC_STORE_U32(simulation+20,std::bit_cast<uint32_t>(previous+dt));
        PPCSafeIndirect(ctx,base,0x82AB9EA8);
    }
};
void originalWorldCollisionScheduler(Runtime& rt,EngineCpuCalls& cpu) {
    auto* base=rt.base;auto& c=cpu.registers();
    constexpr uint32_t manager=0x50700,world=0x50800,simulation=0x50C00,vtable=0x50D00;
    const auto loadFloat=[&](uint32_t at){return std::bit_cast<float>(PPC_LOAD_U32(at));};
    WorldCollisionProbe probe(base);
    const auto prepare=[&] {
        std::memset(rt.pointer(manager,0x640,true),0,0x640);
        PPC_STORE_U32(manager+20,world);PPC_STORE_U32(world+8,simulation);
        // Original simulation constructor supplies all time fields and the
        // frame-marker tolerance. The fake world has no listeners or bodies.
        cpu.invoke(0x82AB9210,simulation,world);
        PPC_STORE_U32(simulation,vtable);
        PPC_STORE_U32(vtable+8,0x82AB9FC0);
        PPC_STORE_U32(vtable+0x14,0x82AB9EA8);
        probe.steps=0;probe.largestStep=0;
    };
    const auto invoke=[&](float dt) {
        c.lr=0x827A6790;c.f1.f64=dt;const auto sp=c.r1.u32;
        const std::array<uint64_t,2> savedGpr={c.r30.u64,c.r31.u64};
        const std::array<uint64_t,3> savedFpr={c.f29.u64,c.f30.u64,c.f31.u64};
        // Deliberately differ from the smoothed manager dt in f1. The world
        // frame target and its collision step must use the same interval.
        PPC_STORE_U32(0x82CED748,std::bit_cast<uint32_t>(dt*0.25f));
        cpu.invoke(0x827A55C0,manager);
        need(c.lr==0x827A6790 && c.r1.u32==sp,"World collision scheduler changed the caller stack or return address");
        need(savedGpr==std::array<uint64_t,2>{c.r30.u64,c.r31.u64} &&
             savedFpr==std::array<uint64_t,3>{c.f29.u64,c.f30.u64,c.f31.u64},
             "World collision scheduler changed nonvolatile original registers");
        need(loadFloat(simulation+16)==loadFloat(simulation+28),
             "World collision scheduler returned before reaching its frame target");
    };
    for(uint32_t rate:{60u,120u,144u,240u}) {
        prepare();const float dt=1.0f/float(rate);
        for(uint32_t frame=1;frame<=2*rate;++frame) {
            invoke(dt);
            if(probe.steps!=frame)
                std::fprintf(stderr,"  world %u FPS, frame %u: %u collision steps, current %.9f, PSI %.9f\n",
                             rate,frame,probe.steps,loadFloat(simulation+16),loadFloat(simulation+20));
            need(probe.steps==frame,"World collision callbacks skipped a positive high-FPS gameplay frame");
        }
        need(std::abs(loadFloat(simulation+16)-2.0f)<0.0001f,
             "High-FPS world collision scheduling changed elapsed simulation time");
        need(probe.largestStep<=dt,"High-FPS world collision steps exceed the frame target interval");
        std::printf("  original world %u FPS: %u collision steps in %.9f seconds\n",
                    rate,probe.steps,loadFloat(simulation+16));
        const auto steps=probe.steps;const float current=loadFloat(simulation+16);
        invoke(0);
        need(probe.steps==steps && loadFloat(simulation+16)==current,
             "Zero-delta world update integrated or advanced simulation time");
    }
    // The original slower-frame branch retains its two half-frame steps and
    // its bounded 16.683ms catch-up steps after a longer stall.
    for(float dt:{0.05f,0.1f}) {
        prepare();invoke(dt);
        need(probe.steps==(dt==0.05f?2u:6u),"World collision scheduling changed original large-frame substeps");
        need(probe.largestStep<=0.025f && std::abs(loadFloat(simulation+16)-dt)<0.0001f,
             "World collision catch-up exceeded its original substep size or target time");
    }
}

}
int main(int argc,char** argv){try {
    need(argc==2,"Original image required");Runtime rt;rt.load(argv[1]);PPCContext entry{};rt.initialize(entry);
    auto* base=rt.base;rt.map(0x50000,0x2000,true,"frame scheduler test records");
    EngineCpuCalls cpu(entry,base);auto& c=cpu.registers();constexpr uint32_t owner=0x50100;
    std::array<uint8_t,148> original{};
    for(uint32_t rate:{0u,30u,60u,90u,120u,144u,165u,240u}) {
        rt.requestedFrameRate=rate;std::memset(rt.pointer(owner,148,true),0xA7,148);
        c.lr=0x82867A78;const auto sp=c.r1.u32;
        need(cpu.invoke(0x82718D48,owner,2)==owner,"Original scheduler constructor return changed");
        need(c.r1.u32==sp && c.lr==0x82867A78,"Frame-rate selection changed the constructor ABI");
        need(PPC_LOAD_U32(owner+144)==(rate==60?1u:(rate?0u:2u)),"Original scheduler did not publish the selected interval");
        if(!rate)std::memcpy(original.data(),rt.pointer(owner,148,false),148);
        else for(uint32_t i=0;i<148;++i)if(i<136 || (i>=140 && i<144))
            need(rt.pointer(owner,148,false)[i]==original[i],"Frame-rate option changed another original constructor field");
    }
    rt.uncappedFrameRate=true;rt.requestedFrameRate=0;
    std::memset(rt.pointer(owner,148,true),0xA7,148);
    c.lr=0x82867A78;const auto uncappedSp=c.r1.u32;
    need(cpu.invoke(0x82718D48,owner,2)==owner,"Uncapped scheduler constructor return changed");
    need(c.r1.u32==uncappedSp && c.lr==0x82867A78,"Uncapped selection changed the constructor ABI");
    need(PPC_LOAD_U32(owner+144)==0u,"Uncapped scheduler did not publish zero intervals");
    for(uint32_t i=0;i<148;++i)if(i<136 || (i>=140 && i<144))
        need(rt.pointer(owner,148,false)[i]==original[i],"Uncapped option changed another original constructor field");
    rt.uncappedFrameRate=false;rt.requestedFrameRate=60;
    std::memset(rt.pointer(owner,148,true),0xA7,148);c.lr=0x12345678;
    rejects([&]{cpu.invoke(0x82718D48,owner,2);});
    need(PPC_LOAD_U32(owner)==0xA7A7A7A7,"Rejected scheduler constructor modified storage");
    rt.uncappedFrameRate=true;rt.requestedFrameRate=0;c.lr=0x12345678;
    rejects([&]{cpu.invoke(0x82718D48,owner,2);});
    rt.uncappedFrameRate=false;rt.requestedFrameRate=60;
    need(PPC_LOAD_U32(owner)==0xA7A7A7A7,"Rejected uncapped constructor modified storage");
    c.lr=0x82867A78;rejects([&]{cpu.invoke(0x82718D48,owner,3);});
    c.lr=0x82867A78;cpu.invoke(0x82718D48,owner,2);
    PPC_STORE_U32(0x82D576A0,owner);
    for(const auto rate:{120u,0u,60u,30u,90u,144u,165u,240u}) {
        std::array<uint8_t,148> before{};std::memcpy(before.data(),rt.pointer(owner,148,false),148);
        rt.videoSettings.frameRate=rate;applyNativeFrameRate(rt);
        need(PPC_LOAD_U32(owner+144)==(rate==60?1u:0u),"Live Video menu limit did not update the original scheduler");
        need(rt.requestedFrameRate==rate&&rt.uncappedFrameRate==(rate==0),"Live Video menu and native pacing disagree");
        need(!std::memcmp(before.data(),rt.pointer(owner,144,false),144),"Live Video limit modified another scheduler field");
    }
    const auto captured=PPCQueryTimebase();rt.nativePresentationTimebase=captured;
    c.lr=0x826B83D4;cpu.invoke(0x82718788,owner);
    need(PPC_LOAD_U32(owner+136)==uint32_t(captured),"Original stamp did not include the real native presentation entry time");
    rt.requestedFrameRate=0;rt.nativePresentationTimebase=1;c.lr=0x826B83D4;cpu.invoke(0x82718788,owner);
    need(PPC_LOAD_U32(owner+136)!=1,"Original mode inherited the native frame-boundary override");
    rt.requestedFrameRate=0;rt.uncappedFrameRate=true;rt.nativePresentationTimebase=captured;
    c.lr=0x826B83D4;cpu.invoke(0x82718788,owner);
    need(PPC_LOAD_U32(owner+136)==uint32_t(captured),"Uncapped stamp did not include the real native presentation entry time");
    rt.uncappedFrameRate=false;rt.requestedFrameRate=120;rt.nativePresentationTimebase=captured;
    c.lr=0x826B83D4;cpu.invoke(0x82718788,owner);
    need(PPC_LOAD_U32(owner+136)==uint32_t(captured),"120 FPS stamp did not include the real native presentation entry time");
    // Native 120 FPS limiter. The first present is released at once, an overdue present is
    // never delayed, and a present arriving early waits out the 8.333ms floor measured from
    // the PREVIOUS release (not from its own entry, which would add the floor to every frame).
    rt.framePacer.reset();
    {const auto start=PPCQueryTimebase();SimpsonsNativeFramePace(rt);const auto elapsed=PPCQueryTimebase()-start;
     need(elapsed<Simpsons::Runtime::timebaseFrequency/60,"First 120 FPS present was delayed");}
    {bool prompt=false;
     for(int attempt=0;attempt<5 && !prompt;++attempt) {
         rt.framePacer.reset();
         const auto first=PPCQueryTimebase();SimpsonsNativeFramePace(rt);       // released at entry
         SimpsonsNativeFramePace(rt);const auto elapsed=PPCQueryTimebase()-first; // arrives immediately: must wait the floor
         need(elapsed>=Simpsons::Runtime::timebaseFrequency/120,"120 FPS limiter released before its 8.333ms floor");
         prompt=elapsed<Simpsons::Runtime::timebaseFrequency/120+Simpsons::Runtime::timebaseFrequency*5/1000;
     }
     need(prompt,"120 FPS limiter overslept its floor in every attempt");}
    {   // Overdue: a frame that already took longer than the floor is released at once.
        rt.framePacer.reset();SimpsonsNativeFramePace(rt);
        Sleep(30);
        const auto start=PPCQueryTimebase();SimpsonsNativeFramePace(rt);const auto elapsed=PPCQueryTimebase()-start;
        need(elapsed<Simpsons::Runtime::timebaseFrequency/120,"Overdue 120 FPS present was delayed");
    }
    {   // Even cadence in simulated 50 MHz ticks (no real waiting).
        constexpr uint64_t ms=Simpsons::Runtime::timebaseFrequency/1000;
        const uint64_t floor=Simpsons::Runtime::timebaseFrequency/120+1,ceiling=Simpsons::Runtime::timebaseFrequency/10;
        const auto simulate=[&](auto&& workMs,unsigned frames,std::vector<uint64_t>& releases,unsigned percentile=85) {
            Simpsons::FramePacer pacer;uint64_t clock=1000*ms;releases.clear();
            for(unsigned i=0;i<frames;++i) {
                clock+=uint64_t(workMs(i)*double(ms));          // frame work, then the present arrives
                clock=pacer.release(clock,floor,ceiling,percentile);// the limiter returns the release time
                releases.push_back(clock);
            }
        };
        std::vector<uint64_t> releases;
        simulate([](unsigned){return 10.0;},400,releases);   // steady 100 FPS work: never delayed
        for(size_t i=64;i<releases.size();++i)need(releases[i]-releases[i-1]==10*ms,"Steady 100 FPS work was padded or sped up");
        simulate([](unsigned){return 4.0;},400,releases);    // fast work: held to the 120 FPS floor
        for(size_t i=1;i<releases.size();++i)need(releases[i]-releases[i-1]==floor,"Fast work was not held to the 120 FPS floor");
        simulate([](unsigned i){return i%2?12.0:8.0;},640,releases); // jittery 8/12 ms work settles on an even 12 ms cadence
        for(size_t i=200;i<releases.size();++i)need(releases[i]-releases[i-1]==12*ms,"Jittery work did not settle to an even cadence");
        // A stall is an outlier: it is not delayed further and does not raise the target for long.
        simulate([](unsigned i){return i==300?500.0:10.0;},1000,releases);
        need(releases[300]-releases[299]==500*ms,"A stall was shortened or lengthened by the limiter");
        for(size_t i=500;i<releases.size();++i)need(releases[i]-releases[i-1]==10*ms,"The target did not recover after a stall");
    }
    for(const auto rate:{30u,90u,144u,165u,240u}) {
        rt.requestedFrameRate=rate;rt.uncappedFrameRate=false;
        const uint64_t floor=(Runtime::timebaseFrequency+rate-1)/rate;
        rt.framePacer.reset();SimpsonsNativeFramePace(rt);
        const auto start=PPCQueryTimebase();SimpsonsNativeFramePace(rt);
        need(PPCQueryTimebase()-start>=floor-100,"Selected frame limit released before its own deadline");
        rt.nativePresentationTimebase=PPCQueryTimebase();c.lr=0x826B83D4;
        cpu.invoke(0x82718788,owner);
        need(PPC_LOAD_U32(owner+136)==uint32_t(rt.nativePresentationTimebase),"Expanded limit lost the native presentation timestamp");
        rt.videoSettings.frameRate=120;applyNativeFrameRate(rt);
        need(rt.framePacer.target(Runtime::timebaseFrequency/120)==Runtime::timebaseFrequency/120,"Live rate change did not discard the previous pacing history");
    }
    rt.requestedFrameRate=0;rejects([&]{SimpsonsNativeFramePace(rt);});
    rt.requestedFrameRate=121;rejects([&]{SimpsonsNativeFramePace(rt);});
    rt.requestedFrameRate=120;
    rt.requestedFrameRate=60;
    rt.requestedFrameRate=60;c.lr=0x826B8108;cpu.invoke(0x82718788,owner);
    need(PPC_LOAD_U32(owner+136)!=1,"Non-presentation stamp inherited a stale native frame boundary");
    c.lr=0x826B83D4;PPC_STORE_U32(owner+0x100+136,0x12345678);
    rejects([&]{cpu.invoke(0x82718788,owner+0x100);});
    need(PPC_LOAD_U32(owner+0x100+136)==0x12345678,"Rejected frame-boundary owner was written");

    // Execute the original scheduling body against a real, unscaled clock.
    PPC_STORE_U32(0x82CED760,1);PPC_STORE_U32(0x82CF0394,std::bit_cast<uint32_t>(60.0f));
    PPC_STORE_U32(0x82D61D58,std::bit_cast<uint32_t>(1000.0f/float(Runtime::timebaseFrequency)));
    for(uint32_t interval:{1u,2u}) {
        const auto start=PPCQueryTimebase();c.lr=0x826B8398;const auto sp=c.r1.u32;
        cpu.invoke(0x826B7B70,uint32_t(start),interval);const auto elapsed=PPCQueryTimebase()-start;
        need(elapsed>=uint64_t(Runtime::timebaseFrequency)*interval/60-100,"Original scheduler returned before its real deadline");
        need(c.r1.u32==sp && c.lr==0x826B8398,"Original pacing body lost its caller ABI");
    }
    {
        const auto start=PPCQueryTimebase();c.lr=0x826B8398;const auto sp=c.r1.u32;
        cpu.invoke(0x826B7B70,uint32_t(start),0u);const auto elapsed=PPCQueryTimebase()-start;
        need(elapsed<uint64_t(Runtime::timebaseFrequency)/60,"Uncapped zero-interval wait did not return immediately");
        need(c.r1.u32==sp && c.lr==0x826B8398,"Uncapped pacing body lost its caller ABI");
    }
    gameplayClockHelpers();gameplayIntegerTimers(rt,cpu);originalGameplayClock(rt,cpu);originalWorldCollisionScheduler(rt,cpu);
    std::printf("PASS native frame rate: %zu checks; original constructor fields, real deadlines, native frame-boundary ownership, 120 FPS floor, frame-independent gameplay, original pause/scale/reset and world collision cadence\n",checks);
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
