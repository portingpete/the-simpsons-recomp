#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Simpsons {
// Presentation limiter with an even cadence, in 50 MHz timebase ticks.
//
// A present arriving at `arrival` is released at max(arrival, previousRelease + target).
// `floor` is the user's cap (8.333 ms for 120 FPS). When the machine cannot sustain the
// cap, `target` rises to a high percentile of the recent natural frame times (the time
// from one release to the next arrival, which never includes this limiter's own wait),
// so most frames are released exactly `target` apart instead of jittering between fast
// and slow. A frame that really takes longer is never delayed further.
//
// The wait belongs before the present is submitted, measured from the previous release:
// sleeping after submission, measured from the present's own entry, adds the whole floor
// to every frame instead of capping the rate.
class FramePacer {
    static constexpr size_t kWindow=64,kMinimum=16;
    std::array<uint64_t,kWindow> natural_{};
    size_t count_=0,next_=0;
    uint64_t previous_=0;
public:
    void reset() noexcept {count_=next_=0;previous_=0;}
    // Current pacing interval for the recorded history (never below `floor`).
    uint64_t target(uint64_t floor,unsigned percentile=85) const {
        if(count_<kMinimum)return floor;
        std::array<uint64_t,kWindow> sorted=natural_;
        const size_t index=std::min(count_-1,count_*std::clamp(percentile,50u,100u)/100);
        std::nth_element(sorted.begin(),sorted.begin()+index,sorted.begin()+count_);
        return std::max(floor,sorted[index]);
    }
    // Returns the release time for a present arriving at `arrival` and records it.
    // Natural times are clamped to `ceiling` so a pause or stall cannot inflate the target.
    uint64_t release(uint64_t arrival,uint64_t floor,uint64_t ceiling,unsigned percentile=85) {
        if(!previous_){previous_=arrival;return arrival;}
        const uint64_t work=arrival>previous_?arrival-previous_:0;
        natural_[next_]=std::min(work,ceiling);next_=(next_+1)%kWindow;count_=std::min(count_+1,kWindow);
        const uint64_t interval=std::min(target(floor,percentile),std::max(floor,ceiling));
        const uint64_t deadline=previous_+interval;
        previous_=std::max(arrival,deadline);
        return previous_;
    }
};
}
