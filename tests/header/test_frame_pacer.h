#pragma once
#include "runtime/frame_pacer.h"
#include <array>
#include <cstdint>
#include <limits>

namespace FramePacerTests {
template<class Check> void run(Check&& need) {
    constexpr uint64_t frequency=50000000,ms=frequency/1000,ceiling=frequency/10;
    constexpr uint64_t floor=(frequency+119)/120;
    Simpsons::FramePacer pacer;
    // Zero is a valid timestamp. It must not act as an uninitialized sentinel.
    need(pacer.release(0,floor,ceiling)==0,"First zero-time frame was delayed");
    pacer.complete(0);
    need(pacer.release(1,floor,ceiling)==floor,"Zero-time initialization lost the next frame deadline");

    // Ten-minute traces at every supported cap retain exact ceiling-rounded
    // deadlines. No per-frame floating-point rounding or missed work is added.
    for(const uint32_t rate:{15u,30u,60u,90u,120u,144u,165u,240u}) {
        const uint64_t interval=(frequency+rate-1)/rate;
        const uint64_t frames=uint64_t(rate)*600,origin=1000*ms;
        pacer.reset();uint64_t actual=pacer.release(origin,interval,ceiling);pacer.complete(actual);
        for(uint64_t frame=1;frame<=frames;++frame) {
            const uint64_t deadline=pacer.release(actual+1,interval,ceiling);
            need(deadline==origin+frame*interval,"Steady cap accumulated work or deadline drift");
            actual=deadline;pacer.complete(actual);
        }
        need(actual-origin>=600*frequency && actual-origin-600*frequency<frames,
             "Cap rounding exceeded one clock tick per frame over ten minutes");
    }

    // Alternate scheduler overshoots after an8ms workload. Actual release
    // intervals never burst below the cap and wake latency never becomes work.
    pacer.reset();uint64_t actual=pacer.release(1000*ms,floor,ceiling);pacer.complete(actual);
    for(unsigned frame=0;frame<2000;++frame) {
        const uint64_t deadline=pacer.release(actual+8*ms,floor,ceiling);
        need(deadline-actual==floor,"Timer overshoot changed the cap or produced a catch-up burst");
        const uint64_t wake=deadline+(frame%3)*2*ms;
        pacer.complete(wake);actual=wake;
        need(pacer.target(floor)==floor,"Timer overshoot polluted natural frame history");
    }

    // Sustainable uneven work can still converge to an even cadence.
    pacer.reset();actual=pacer.release(1000*ms,floor,ceiling);pacer.complete(actual);
    for(unsigned frame=0;frame<256;++frame) {
        const uint64_t deadline=pacer.release(actual+(frame%2?12:8)*ms,floor,ceiling);
        if(frame>=100)need(deadline-actual==12*ms,"Adaptive pacing lost the sustainable uneven-work cadence");
        actual=deadline;pacer.complete(actual);
    }

    // Slightly uneven sustained work is held to its 85th percentile.
    pacer.reset();actual=pacer.release(1000*ms,floor,ceiling);pacer.complete(actual);
    constexpr uint64_t steady[]{12*ms,12*ms+ms/2,13*ms,13*ms+ms/2};
    for(unsigned frame=0;frame<256;++frame) {
        const uint64_t deadline=pacer.release(actual+steady[frame%4],floor,ceiling);
        if(frame>=100)need(deadline-actual==13*ms+ms/2,"Adaptive pacing lost a tight sustained cadence");
        actual=deadline;pacer.complete(actual);
    }

    // Bursty work (a loading screen) is never held to its slowest frames:
    // every release is the later of its arrival and the cap.
    pacer.reset();actual=pacer.release(1000*ms,floor,ceiling);pacer.complete(actual);
    constexpr uint64_t bursty[]{5*ms,30*ms,10*ms,35*ms,6*ms,20*ms,9*ms,25*ms};
    for(unsigned frame=0;frame<256;++frame) {
        const uint64_t work=bursty[frame%8],deadline=pacer.release(actual+work,floor,ceiling);
        need(deadline-actual==std::max(work,floor),"Bursty work was held beyond the cap");
        actual=deadline;pacer.complete(actual);
    }
    need(pacer.target(floor)==floor,"Bursty history raised the pacing interval");

    // Repeated500ms stalls used to occupy the percentile window after their
    // values were clamped to100ms. Every stall now releases immediately and the
    // next fast frame begins a fresh cap without a delayed recovery backlog.
    for(unsigned stall=0;stall<24;++stall) {
        const uint64_t arrival=actual+500*ms;
        need(pacer.release(arrival,floor,ceiling)==arrival,"A stall was delayed beyond its arrival");
        pacer.complete(arrival);
        need(pacer.target(floor)==floor,"A long stall retained adaptive throttle history");
        actual=pacer.release(arrival+4*ms,floor,ceiling);
        need(actual-arrival==floor,"A recovered frame inherited the stall duration");
        pacer.complete(actual);
    }
    pacer.reset();
    need(pacer.target(floor)==floor && pacer.release(0,floor,ceiling)==0,
         "Reset retained cadence, samples or initialization");

    // Delayed Present/VSync/GPU work already pays the cap. It must not pay a
    // fresh full interval on top of that work.
    actual=pacer.release(25*ms,floor,ceiling);pacer.complete(actual);
    for(unsigned frame=0;frame<256;++frame) {
        const uint64_t arrival=actual+17*ms;
        need(pacer.release(arrival,floor,ceiling)==arrival,"Presentation blocking received an additional limiter interval");
        pacer.complete(arrival);actual=arrival;
    }

    // Unsigned deadline arithmetic keeps a monotonic clock valid through wrap.
    pacer.reset();actual=std::numeric_limits<uint64_t>::max()-floor/2;
    need(pacer.release(actual,floor,ceiling)==actual,"Wrap trace delayed its first frame");pacer.complete(actual);
    for(unsigned frame=0;frame<100;++frame) {
        const uint64_t deadline=pacer.release(actual+1,floor,ceiling);
        need(deadline-actual==floor,"Clock wrap broke the frame deadline");
        actual=deadline;pacer.complete(actual);
    }
    const uint64_t backwards=actual-floor;
    need(pacer.release(backwards,floor,ceiling)==backwards,"Clock discontinuity created an unbounded wait");
    need(pacer.target(floor)==floor,"Clock discontinuity retained stale pacing history");

    for(uint32_t foreground:{0u,30u,60u,120u,240u})for(uint32_t background:{0u,15u,30u,60u}) {
        need(Simpsons::effectiveFrameRate(foreground,background,true)==foreground,"Foreground frame cap changed with background policy");
        const uint32_t expected=!background?foreground:!foreground?background:std::min(foreground,background);
        need(Simpsons::effectiveFrameRate(foreground,background,false)==expected,"Background policy raised a cap or failed to limit uncapped gameplay");
    }
}
}
