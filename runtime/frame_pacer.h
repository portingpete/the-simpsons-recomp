#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace Simpsons {
// A background cap may lower the foreground cap; zero means no additional cap.
constexpr uint32_t effectiveFrameRate(uint32_t foregroundRate,uint32_t backgroundRate,bool foreground) noexcept {
    if(foreground || !backgroundRate)return foregroundRate;
    return foregroundRate?std::min(foregroundRate,backgroundRate):backgroundRate;
}
// Display refresh timing in timebase ticks: vblanks occur at origin + k*period.
struct RefreshGrid {
    uint64_t origin=0,period=0;
    constexpr bool valid() const noexcept {return period!=0;}
    // First vblank at or after `time`. Offsets within half the clock range keep wrap order.
    constexpr uint64_t next(uint64_t time) const noexcept {
        const uint64_t offset=time-origin;
        if(offset<(uint64_t(1)<<63))return origin+(offset+period-1)/period*period;
        return origin-(origin-time)/period*period;
    }
    // Distance from `time` to the nearest vblank.
    constexpr uint64_t distance(uint64_t time) const noexcept {
        const uint64_t after=next(time)-time;
        return std::min(after,period-after);
    }
};
// Whole refreshes per capped frame when VSync can lock a cap to the display: one when
// the cap is at or above the refresh rate (0.5% tolerance, so a 120 FPS cap locks to a
// 119.84 Hz display), otherwise a multiple within 1% of the cap (60 FPS at 119.84 Hz is
// two refreshes). Zero means the cap cannot be shown evenly and paces on its own clock.
constexpr uint32_t refreshesPerFrame(uint64_t capInterval,uint64_t refresh) noexcept {
    if(!capInterval || !refresh)return 0;
    if(capInterval<=refresh+refresh/200)return 1;
    const uint64_t multiple=(capInterval+refresh/2)/refresh;
    const uint64_t product=multiple*refresh;
    const uint64_t difference=product>capInterval?product-capInterval:capInterval-product;
    return multiple<=8 && difference*100<=capInterval?uint32_t(multiple):0;
}
// Presentation limiter with an even cadence, in 50 MHz timebase ticks.
//
// release(): a present arriving at `arrival` is released at max(arrival, previousRelease
// + target). `floor` is the user's cap (8.333 ms for 120 FPS). When the machine cannot
// sustain the cap, `target` rises to a high percentile of the recent natural frame times
// (the time from one release to the next arrival, which never includes this limiter's own
// wait), so most frames are released exactly `target` apart instead of jittering between
// fast and slow, provided that percentile is within 25% of the typical frame. A frame
// that really takes longer is never delayed further.
//
// releaseLocked(): with VSync, a free-running cap beats against the display. 120.000 FPS
// on a 119.844 Hz display drifted through every vblank phase and showed about one frame
// per second for two refreshes. Locked releases instead fall on the display's own refresh
// grid, `lead` before the vblank each frame targets, one frame per `refreshes` refreshes,
// so every frame reaches the display on its own refresh with a constant latency.
// The lead is also a jitter buffer: a frame that arrives after its slot is released at
// once and still reaches its vblank while it is inside the lead. Lateness carries over
// (the next frame starts late), so the buffer must exist before a heavy stretch begins:
// extra lead cannot help frames already in debt. The lead therefore follows the recent
// largest debt (accumulated lateness) of the last 30 seconds on top of the minimum,
// and separately grows when DXGI reports repeated misses of the display path itself
// (GPU or compositor time). It stays below 7/8 of a refresh. Light scenes keep a short
// lead (low latency); a workload close to the refresh budget buys smoothness with a
// longer one.
// On a display whose flips land on the refresh grid (a fixed refresh rate), a rate the
// machine cannot sustain steps to the next whole number of refreshes instead of holding
// its own cadence, which would beat against the display exactly like an unlocked cap.
// Other displays (variable refresh, or no statistics yet) keep the percentile hold.
//
// The wait belongs before the present is submitted, measured from the previous release:
// sleeping after submission, measured from the present's own entry, adds the whole floor
// to every frame instead of capping the rate.
// Call complete() with the actual release after waiting. Timer overshoot then
// belongs to the previous wait, not the next frame's measured work or a catch-up
// burst. Long stalls discard history and begin a fresh cadence immediately.
//
// predict() supplies the gameplay clock: the release (free) or vblank (locked) that the
// frame being built will most likely get, so each update advances exactly as far as the
// display does instead of by the jitter of the code that runs before the sample.
class FramePacer {
    static constexpr size_t kWindow=64,kMinimum=16,kPending=16;
    static constexpr uint32_t kRelaxAfter=3600; // On-time locked frames before the path lead shrinks one step (30 s at 120 Hz).
    static constexpr uint32_t kGridWindow=64,kGridFixed=58; // Flips on the refresh grid that confirm a fixed rate.
    static constexpr uint32_t kMaximumExtraRefreshes=3;
    // The largest debt of the last 30 buckets of 120 refreshes (30 s at 120 Hz) sets the
    // lead. Buckets advance with time, so slow loading frames cannot stretch the window.
    static constexpr uint32_t kDebtBuckets=30,kDebtBucketRefreshes=120;
    std::array<uint64_t,kWindow> natural_{};
    size_t count_=0,next_=0;
    uint64_t previous_=0;
    bool initialized_=false;
    // State of the latest release, for predict() and display feedback.
    enum class Mode:uint8_t {Free,Locked,Held};
    Mode mode_=Mode::Free;
    uint64_t interval_=0,target_=0,intended_=0;
    RefreshGrid grid_{};
    uint32_t refreshes_=0,stepped_=0;
    bool onSlot_=false;
    uint64_t arrival_=0;
    uint32_t steadySlots_=0; // Consecutive locked releases on their slot.
    uint64_t releases_=0;
    // Display feedback. It survives reset() because it describes the display path,
    // not the workload.
    uint64_t lead_=0,minimumLead_=0,pathLead_=0; // pathLead_: miss-driven display-path lead.
    uint64_t debt_=0,peakDebt_=0; // Lateness carried by the current frame and its recent peak.
    std::array<uint64_t,kDebtBuckets> debtBuckets_{};
    uint32_t debtBucket_=0;uint64_t debtBucketStart_=0;bool debtClock_=false;
    uint32_t misses_=0,onTimeSinceMiss_=0;
    // Whole refreshes between the vblank an on-slot frame aimed for and its flip, for the
    // most recent frames. Their minimum is the display path's constant latency: a
    // composed window flips every frame one refresh after its vblank without missing.
    // It is trusted only once the path lead has reached its maximum (no lead could have
    // kept those vblanks), and stays trusted while every recent frame still shows it.
    static constexpr uint32_t kLatencyWindow=32,kLatencyMinimum=8;
    std::array<uint8_t,kLatencyWindow> delays_{};
    uint32_t delayNext_=0,delayCount_=0,latency_=0;
    bool latencyTrusted_=false;
    uint64_t onGrid_=0;uint32_t gridSamples_=0;
    uint32_t lastFlipPresent_=0,latestPresent_=0;uint64_t lastFlipSync_=0;
    struct Pending {uint32_t present=0;uint64_t vblank=0,arrival=0;RefreshGrid grid{};bool onSlot=false,steady=false,locked=false;};
    std::array<Pending,kPending> pending_{};
    size_t pendingNext_=0;
    // Gameplay clock prediction.
    uint64_t prediction_=0,sampleReal_=0,sampledRelease_=0;
    bool predicted_=false,sampled_=false;
    static constexpr bool before(uint64_t a,uint64_t b) noexcept {return b-a-1<(uint64_t(1)<<63);} // a < b
    static constexpr uint64_t later(uint64_t a,uint64_t b) noexcept {return before(a,b)?b:a;}
    // The lead may grow to most of a refresh: beyond that, releases would land before the
    // previous frame's flip and Present would block instead.
    uint64_t maximumLead(uint64_t period) const noexcept {return std::max(minimumLead_,period*7/8);}
    void record(uint64_t work) noexcept {
        natural_[next_]=work;next_=(next_+1)%kWindow;count_=std::min(count_+1,kWindow);
    }
    // Replays the newest `frames` (default all) recorded frames, in order, against a locked
    // `interval`: a frame may run late by up to `slack` (the lead's buffer) and the next
    // frame inherits that lateness. Returns how many would have missed their vblank; a
    // miss re-anchors the schedule.
    size_t missesAt(uint64_t interval,uint64_t slack,size_t frames=kWindow) const noexcept {
        frames=std::min(frames,count_);
        const size_t oldest=(count_<kWindow?0:next_)+(count_-frames);
        uint64_t debt=0;size_t misses=0;
        for(size_t i=0;i<frames;++i) {
            const uint64_t work=natural_[(oldest+i)%kWindow];
            debt=debt+work>interval?debt+work-interval:0;
            if(debt>slack){++misses;debt=0;}
        }
        return misses;
    }
    // How many of the newest frames fit in `span` of time (at least kMinimum), each taking
    // its own work or the current `interval`, whichever is longer.
    static constexpr uint32_t kRecentRefreshes=64;
    size_t recentFrames(uint64_t span,uint64_t interval) const noexcept {
        size_t frames=0;uint64_t total=0;
        while(frames<count_) {
            const uint64_t time=std::max(natural_[(next_+kWindow-1-frames)%kWindow],interval);
            if(frames>=kMinimum && total+time>span)break;
            total+=time;++frames;
        }
        return frames;
    }
    // Drops all but the newest `frames` from the history, oldest first at index 0.
    void keepRecent(size_t frames) noexcept {
        frames=std::min(frames,count_);
        std::array<uint64_t,kWindow> kept{};
        for(size_t i=0;i<frames;++i)kept[i]=natural_[(next_+kWindow-frames+i)%kWindow];
        natural_=kept;count_=frames;next_=frames%kWindow;
    }
public:
    void reset() noexcept {
        count_=next_=0;previous_=0;initialized_=false;mode_=Mode::Free;interval_=target_=intended_=0;
        refreshes_=stepped_=0;onSlot_=false;pending_={};pendingNext_=0;sampled_=false;
    }
    // Current pacing interval for the recorded history (never below `floor`).
    // Holding every frame to the high percentile only pays off while the history
    // is tight: it then costs a few percent of throughput and removes jitter.
    // Bursty phases (loading screens, streaming) would instead idle most frames
    // up to their slowest neighbours; a spr_hub load measured 20-35 ms of limiter
    // wait inside 37 ms frames. Those pace to the cap alone.
    uint64_t target(uint64_t floor,unsigned percentile=85) const {
        if(count_<kMinimum)return floor;
        std::array<uint64_t,kWindow> sorted=natural_;
        const size_t index=std::min(count_-1,count_*std::clamp(percentile,50u,100u)/100);
        std::nth_element(sorted.begin(),sorted.begin()+index,sorted.begin()+count_);
        const uint64_t high=sorted[index];
        if(high<=floor)return floor;
        // Everything before `index` is at most `high`, so the median lies there.
        const size_t middle=count_/2;
        uint64_t median=high;
        if(middle<index) {
            std::nth_element(sorted.begin(),sorted.begin()+middle,sorted.begin()+index);
            median=sorted[middle];
        }
        const uint64_t typical=std::max(floor,median); // No frame is released faster than the cap.
        return high-typical>typical/4?floor:high;
    }
    // Returns the release time for a present arriving at `arrival` and records it.
    // Intervals shorter than half the uint64_t clock range retain wrap ordering.
    // `ceiling` separates normal frame work from stalls, which are never samples.
    uint64_t release(uint64_t arrival,uint64_t floor,uint64_t ceiling,unsigned percentile=85) {
        ++releases_;mode_=Mode::Free;refreshes_=stepped_=0;onSlot_=false;interval_=floor;
        if(!initialized_){previous_=arrival;initialized_=true;return target_=arrival;}
        const uint64_t work=arrival-previous_;
        if(work>=std::max(floor,ceiling)) {
            // Includes a clock discontinuity. Clamping several stalls to100ms
            // would otherwise let them become the85th percentile and throttle
            // every following frame even after the machine has recovered.
            count_=next_=0;previous_=arrival;return target_=arrival;
        }
        record(work);
        interval_=std::min(target(floor,percentile),std::max(floor,ceiling));
        previous_=work>=interval_?arrival:previous_+interval_;
        return target_=previous_;
    }
    // Refresh-locked release (see the class comment). `minimumLead` is the smallest lead
    // before the targeted vblank; the lead adapts upward from it on reported misses.
    uint64_t releaseLocked(uint64_t arrival,const RefreshGrid& grid,uint32_t refreshes,uint64_t minimumLead,
                           uint64_t ceiling,unsigned percentile=85) {
        if(!grid.valid() || !refreshes)return release(arrival,grid.period*refreshes,ceiling,percentile);
        ++releases_;grid_=grid;onSlot_=false;arrival_=arrival;
        const Mode was=mode_;
        if(minimumLead!=minimumLead_ || !pathLead_){minimumLead_=minimumLead;pathLead_=minimumLead;}
        const auto updateLead=[&]{lead_=std::min(std::max(pathLead_,minimumLead_+peakDebt_),maximumLead(grid.period));};
        updateLead();
        if(refreshes_<refreshes || refreshes_>refreshes+kMaximumExtraRefreshes || was==Mode::Free)refreshes_=refreshes;
        const auto lock=[&](uint64_t vblank) {
            // Release `lead` before the targeted vblank, or at once when already past that slot.
            const uint64_t slot=vblank-lead_;
            mode_=Mode::Locked;intended_=vblank;interval_=grid.period*refreshes_;
            if(!before(slot,arrival)) {onSlot_=true;steadySlots_=std::min(steadySlots_+1,1u<<20);target_=vblank;return previous_=slot;}
            steadySlots_=0;
            // Late: release at once. A small overrun still reaches this vblank inside the lead;
            // past the vblank, it aims for the first one it can reach after the minimum lead.
            target_=before(arrival,vblank)?vblank:grid.next(arrival+minimumLead_);
            return previous_=arrival;
        };
        if(!initialized_){initialized_=true;previous_=arrival;debt_=0;return lock(grid.next(arrival+lead_));}
        const uint64_t frame=grid.period*refreshes;
        const uint64_t work=arrival-previous_;
        if(work>=std::max(frame,ceiling)){count_=next_=0;previous_=arrival;debt_=0;return lock(grid.next(arrival+lead_));}
        record(work);
        if(fixedRefresh()) {
            // Even cadences on a fixed display exist only in whole refreshes. The lead
            // absorbs occasional slow frames, so step up only when more than one recent
            // frame in eight would still miss its vblank, and step back once at most one
            // in thirty-two would miss the shorter interval. The replay assumes half a
            // refresh of buffer, which the adaptive lead provides when it is needed.
            if(count_>=kMinimum) {
                const uint64_t slack=grid.period/2;
                if(refreshes_<refreshes+kMaximumExtraRefreshes && missesAt(grid.period*refreshes_,slack)*8>count_)++refreshes_;
                else if(refreshes_>refreshes+1) {
                    // Above one extra refresh (a burst, typically right after a stall), judge
                    // only the last ~half second: a 64-frame history at four refreshes held
                    // 22 FPS for 2.3 s after the burst had ended. The step drops the burst
                    // from the history, so it cannot step straight back up.
                    const size_t recent=recentFrames(grid.period*kRecentRefreshes,grid.period*refreshes_);
                    if(missesAt(grid.period*(refreshes_-1),slack,recent)*32<=recent){--refreshes_;keepRecent(recent);}
                }
                else if(refreshes_>refreshes && count_>=kWindow/2 && missesAt(grid.period*(refreshes_-1),slack)*32<=count_)--refreshes_;
            }
        } else {
            refreshes_=refreshes;
            const uint64_t held=std::min(target(frame,percentile),std::max(frame,ceiling));
            if(held>frame) {
                // The machine cannot sustain this refresh multiple: hold its own even cadence.
                mode_=Mode::Held;interval_=held;
                previous_=work>=held?arrival:previous_+held;
                return target_=previous_;
            }
        }
        stepped_=refreshes_-refreshes;
        {
            // Debt follows the work alone (independent of the lead): a frame inherits its
            // predecessor's lateness. Debt no lead could absorb repeats a refresh anyway and
            // the schedule re-anchors to that late frame, so it neither counts nor carries.
            const uint64_t interval=grid.period*refreshes_;
            debt_=debt_+work>interval?debt_+work-interval:0;
            // Heavy stretches recur seconds apart: keep the largest debt of the recent
            // window. A lower lead after a bucket expires only releases later; every frame
            // still aims for the same vblank.
            if(debt_>maximumLead(grid.period)-minimumLead_)debt_=0;
            const uint64_t span=grid.period*kDebtBucketRefreshes;
            if(!debtClock_){debtClock_=true;debtBucketStart_=arrival;}
            for(uint32_t step=0;step<kDebtBuckets && arrival-debtBucketStart_>=span && arrival-debtBucketStart_<(uint64_t(1)<<63);++step) {
                debtBucketStart_+=span;debtBucket_=(debtBucket_+1)%kDebtBuckets;debtBuckets_[debtBucket_]=0;
            }
            if(arrival-debtBucketStart_>=span && arrival-debtBucketStart_<(uint64_t(1)<<63))debtBucketStart_=arrival; // A long gap cleared every bucket.
            debtBuckets_[debtBucket_]=std::max(debtBuckets_[debtBucket_],debt_);
            peakDebt_=*std::max_element(debtBuckets_.begin(),debtBuckets_.end());
            updateLead();
        }
        // One locked interval after the vblank the previous frame aimed for (not after its
        // release: a frame released after its vblank is shown one refresh later, and the
        // next frame must not aim for that same vblank). Half a refresh of tolerance
        // absorbs grid re-anchoring and timer phase noise.
        const uint64_t locked=grid.period*refreshes_;
        return lock(was==Mode::Locked?grid.next(target_+locked-grid.period/2):grid.next(previous_+locked-grid.period/2+lead_));
    }
    void complete(uint64_t actual) noexcept {
        if(initialized_ && actual-previous_<(uint64_t(1)<<63))previous_=actual;
    }
    // After Present: DXGI's count for the present just submitted.
    // Held frames (refresh locking on a display not yet confirmed fixed) are recorded too:
    // only their flips can confirm the grid, and a workload that started heavy otherwise
    // stayed held at an uneven cadence (one and two refreshes) on a fixed display.
    void submitted(uint32_t present) noexcept {
        if(mode_==Mode::Free)return;
        latestPresent_=present;
        pending_[pendingNext_]={present,intended_,arrival_,grid_,onSlot_,steadySlots_>16,mode_==Mode::Locked};
        pendingNext_=(pendingNext_+1)%kPending;
    }
    // DXGI frame statistics: `present` reached the display at the vblank `sync`.
    // Returns true when a locked frame missed a vblank that a longer lead could have kept.
    bool displayed(uint32_t present,uint64_t sync) noexcept {
        bool missed=false;
        for(auto& entry:pending_) {
            if(!entry.present || entry.present!=present)continue;
            const Pending found=entry;entry={};
            onGrid_=(onGrid_<<1)|(found.grid.distance(sync)<=found.grid.period/32?1u:0u);
            gridSamples_=std::min(gridSamples_+1,kGridWindow);
            if(!found.locked)break; // A held frame aimed for no vblank: grid evidence only.
            if(found.onSlot && found.grid.period) {
                const uint64_t delay=before(found.vblank,sync)?(sync-found.vblank+found.grid.period/2)/found.grid.period:0;
                delays_[delayNext_]=uint8_t(std::min<uint64_t>(delay,8));delayNext_=(delayNext_+1)%kLatencyWindow;
                delayCount_=std::min(delayCount_+1,kLatencyWindow);
                latency_=delayCount_<kLatencyMinimum?0:*std::min_element(delays_.begin(),delays_.begin()+delayCount_);
                latencyTrusted_=latency_ && (latencyTrusted_ || pathLead_>=maximumLead(found.grid.period));
            }
            // Lateness is measured past the path's constant latency. Treating that latency
            // as a miss on every frame made the drain below add a refresh to every other
            // frame (two- and three-refresh alternation at 60 FPS, one and two at 120).
            const uint64_t shift=uint64_t(displayLatency())*found.grid.period;
            const bool late=before(found.vblank+shift+found.grid.period/2,sync);
            if(late && mode_==Mode::Locked) {
                // Every frame submitted after a late one queues behind it and Present
                // blocks for each. Aim the next frame one interval after the vblank the
                // newest queued frame will really reach; the display still shows a new
                // frame on every refresh while the queue drains.
                const uint32_t queued=latestPresent_-present;
                const uint64_t latency=uint64_t(displayLatency())*grid_.period;
                if(queued<kPending) {
                    const uint64_t expected=sync+uint64_t(queued)*grid_.period*refreshes_;
                    if(before(target_+latency+grid_.period/2,expected))target_=grid_.next(expected-latency-grid_.period/2);
                }
            }
            // A frame queued behind a previous frame that was itself shown late could
            // not have reached its vblank with any lead; neither could one that arrived
            // after the vblank had already passed.
            const bool clear=lastFlipPresent_+1==present && before(lastFlipSync_,found.vblank+shift-found.grid.period/2);
            // A longer lead moves the whole cadence earlier, so it also saves a frame that
            // finished up to the remaining lead headroom after its vblank.
            const bool savable=before(found.arrival,found.vblank+(maximumLead(found.grid.period)-lead_));
            // Debt-driven lead already covers workload lateness; misses beyond it point at
            // the display path, whose lead grows separately.
            if(late && clear && savable) {
                missed=true;++misses_;onTimeSinceMiss_=0;
                // A frame released on its slot after a steady run that still missed points at
                // the display path (GPU tail or compositor; late frames are the workload's
                // debt, and loading screens are not steady). Each such miss grows the path
                // lead one step from its own value, so a miss while the debt lead happens to
                // be high cannot lock that lead in; it relaxes one step per 30 quiet seconds.
                if(found.onSlot && found.steady)
                    pathLead_=std::min(pathLead_+std::max<uint64_t>(minimumLead_/4,1),maximumLead(found.grid.period));
            } else if(!late && ++onTimeSinceMiss_>=kRelaxAfter) {
                onTimeSinceMiss_=0;
                const uint64_t step=std::max<uint64_t>(minimumLead_/8,1);
                pathLead_=pathLead_>minimumLead_+step?pathLead_-step:minimumLead_;
            }
            break;
        }
        lastFlipPresent_=present;lastFlipSync_=sync;
        return missed;
    }
    // Gameplay clock value for an update starting at `now` (see the class comment).
    // A second update before the next release advances by real time from the first.
    uint64_t predict(uint64_t now) noexcept {
        uint64_t value=now;
        if(sampled_ && sampledRelease_==releases_)value=prediction_+std::max<uint64_t>(now-sampleReal_,1);
        else if(initialized_ && mode_==Mode::Locked) {
            const uint64_t frame=grid_.period*refreshes_;
            value=later(grid_.next(target_+frame-grid_.period/2),grid_.next(now+lead_));
        } else if(initialized_)value=later(previous_+interval_,now);
        if(predicted_ && !before(prediction_,value))value=prediction_+1;
        prediction_=value;predicted_=true;sampled_=true;sampleReal_=now;sampledRelease_=releases_;
        return value;
    }
    // True once nearly all recent locked flips landed on the refresh grid.
    bool fixedRefresh() const noexcept {
        return gridSamples_>=kGridWindow && uint32_t(std::popcount(onGrid_))>=kGridFixed;
    }
    bool locked() const noexcept {return mode_==Mode::Locked;}
    bool held() const noexcept {return mode_==Mode::Held;}
    bool onSlot() const noexcept {return onSlot_;}
    uint64_t lead() const noexcept {return lead_;}
    uint32_t misses() const noexcept {return misses_;}
    // Refreshes every on-slot frame recently took past its vblank (the path's constant latency).
    uint32_t displayLatency() const noexcept {return latencyTrusted_?latency_:0;}
    uint32_t extraRefreshes() const noexcept {return stepped_;}
    // Target display time (locked) or release (free) of the latest release.
    uint64_t latestTarget() const noexcept {return target_;}
    uint64_t latestInterval() const noexcept {return interval_;}
};
}
