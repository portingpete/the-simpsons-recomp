#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace Simpsons {
// Opt-in per-presentation pacing trace (SIMPSONS_PACING_TRACE=<csv path>).
// Records, in 50 MHz timebase ticks converted to microseconds, the gameplay
// clock sample that built each frame (real start and the clock value it used),
// the limiter's planned and actual release, its target, lead and mode, the
// display refresh grid, the time Present returned and DXGI's statistics for the
// latest displayed flip. Owner thread only; it changes nothing.
class PacingTrace {
    std::FILE* file=nullptr;
    uint64_t origin=0,sample=0,clock=0,dt=0,arrival=0,planned=0,wake=0,interval=0,target=0,lead=0,step=0;
    uint64_t gridPeriod=0,gridOrigin=0,timerDue=0,timerReturn=0;
    unsigned samples=0,mode=0;
    uint32_t statsPresent=0,lastPresent=0;uint64_t statsSync=0;bool stats=false;
    uint64_t rows=0;
    static double us(uint64_t ticks) {return double(ticks)/50.0;}
    double at(uint64_t ticks) const {return ticks?double(int64_t(ticks-origin))/50.0:-1.0;}
public:
    static PacingTrace& instance() {static PacingTrace trace;return trace;}
    PacingTrace() {
        const char* path=std::getenv("SIMPSONS_PACING_TRACE");
        if(!path || !*path)return;
        file=std::fopen(path,"w");
        if(file)std::fputs("index,sample_us,clock_us,dt_us,step_us,samples,arrival_us,planned_us,wake_us,present_end_us,"
            "interval_us,target_us,lead_us,mode,grid_period_us,grid_vblank_us,last_present,stat_present,stat_sync_us,"
            "timer_due_us,timer_return_us\n",file);
    }
    ~PacingTrace(){if(file)std::fclose(file);}
    bool enabled() const {return file!=nullptr;}
    // The absolute origin (50 MHz timebase) lets other captures, such as the stack
    // sampler's QPC stamps, be aligned with trace rows.
    void setOrigin(uint64_t value) {
        if(origin)return;origin=value;
        std::fprintf(stderr,"[PACING TRACE] origin_timebase=%llu\n",static_cast<unsigned long long>(origin));
    }
    void clockSample(uint64_t real,uint64_t value,uint64_t delta) {
        if(!file)return;setOrigin(real);sample=real;clock=value;dt=delta;++samples;
    }
    void clockStep(double seconds){if(file)step=uint64_t(seconds*50e6+0.5);}
    // mode: 0 free, 1 locked on its slot, 2 locked but late, 3 locked config holding its own cadence.
    void paced(uint64_t arrive,uint64_t plan,uint64_t woke,uint64_t pacing,uint64_t aim,uint64_t ahead,unsigned how,
               uint64_t period,uint64_t vblank) {
        if(!file)return;setOrigin(arrive);
        arrival=arrive;planned=plan;wake=woke;interval=pacing;target=aim;lead=ahead;mode=how;gridPeriod=period;gridOrigin=vblank;
    }
    // The limiter's last timer wait of this frame: when it was due and when it returned.
    void timerWait(uint64_t due,uint64_t returned) {if(file){timerDue=due;timerReturn=returned;}}
    // `sync` is DXGI's SyncQPCTime in the 50 MHz timebase.
    void displayStatistics(uint32_t last,bool valid,uint32_t presentCount,uint64_t sync) {
        if(!file)return;lastPresent=last;stats=valid;statsPresent=presentCount;statsSync=sync;
    }
    void presented(uint64_t now) {
        if(!file)return;
        setOrigin(now);
        std::fprintf(file,"%llu,%.1f,%.1f,%.1f,%.1f,%u,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%u,%.3f,%.1f,%u,%u,%.1f,%.1f,%.1f\n",
            static_cast<unsigned long long>(rows++),at(sample),at(clock),us(dt),us(step),samples,at(arrival),at(planned),
            at(wake),at(now),us(interval),at(target),us(lead),mode,us(gridPeriod),at(gridOrigin),
            lastPresent,stats?statsPresent:0u,stats?at(statsSync):-1.0,at(timerDue),at(timerReturn));
        samples=0;arrival=planned=wake=interval=target=lead=gridPeriod=gridOrigin=timerDue=timerReturn=0;mode=0;stats=false;lastPresent=0;
        if(rows%240==0)std::fflush(file);
    }
};
}
