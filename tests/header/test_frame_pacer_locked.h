#pragma once
#include "runtime/frame_pacer.h"
#include <cstdint>
#include <vector>

// Refresh-locked pacing (VSync), display feedback and gameplay clock prediction.
namespace FramePacerLockedTests {
template<class Check> void run(Check&& need) {
    using Simpsons::FramePacer;using Simpsons::RefreshGrid;
    constexpr uint64_t frequency=50000000,ms=frequency/1000,us=frequency/1000000,ceiling=frequency/10;
    // The measured 119.844 Hz panel: 8344.2 us.
    constexpr uint64_t period=8344*us+10;
    constexpr uint64_t lead=2*ms;

    // Cap/refresh matching.
    constexpr uint64_t cap120=(frequency+119)/120,cap60=(frequency+59)/60,cap90=(frequency+89)/90;
    need(Simpsons::refreshesPerFrame(cap120,period)==1,"120 FPS did not lock to a 119.84 Hz refresh");
    need(Simpsons::refreshesPerFrame(cap60,period)==2,"60 FPS did not lock to every second 119.84 Hz refresh");
    need(Simpsons::refreshesPerFrame((frequency+29)/30,period)==4,"30 FPS did not lock to every fourth refresh");
    need(Simpsons::refreshesPerFrame(cap90,period)==0,"90 FPS claimed an even 119.84 Hz cadence");
    need(Simpsons::refreshesPerFrame((frequency+239)/240,period)==1,"A cap above the refresh did not lock to the refresh");
    need(Simpsons::refreshesPerFrame(cap120,frequency/144)==0,"120 FPS claimed an even 144 Hz cadence");
    need(Simpsons::refreshesPerFrame(cap120,frequency/240)==2,"120 FPS did not lock to every second 240 Hz refresh");
    need(Simpsons::refreshesPerFrame(cap120,0)==0 && Simpsons::refreshesPerFrame(0,period)==0,"Missing timing locked a cadence");

    // Grid arithmetic, including times before the origin and across clock wrap.
    {
        constexpr RefreshGrid grid{1000*ms,period};
        need(grid.next(1000*ms)==1000*ms && grid.next(1000*ms+1)==1000*ms+period,"Grid did not round up to the next vblank");
        need(grid.next(1000*ms-1)==1000*ms && grid.next(1000*ms-period-1)==1000*ms-period && grid.next(1000*ms-period)==1000*ms-period,"Grid failed before its origin");
        need(grid.distance(1000*ms+3*period+5)==5 && grid.distance(1000*ms+3*period-7)==7,"Grid distance is wrong");
        constexpr RefreshGrid wrapped{UINT64_MAX-period/3,period};
        need(wrapped.next(wrapped.origin+period/2)==wrapped.origin+period,"Grid failed across clock wrap");
    }

    const auto simulate=[&](FramePacer& pacer,RefreshGrid grid,uint64_t& clock,auto&& workTicks,unsigned frames,
                            std::vector<uint64_t>& releases,std::vector<uint64_t>& targets,uint64_t minimumLead=lead) {
        releases.clear();targets.clear();
        for(unsigned i=0;i<frames;++i) {
            clock+=workTicks(i);
            clock=pacer.releaseLocked(clock,grid,1,minimumLead,ceiling);
            pacer.complete(clock);
            releases.push_back(clock);targets.push_back(pacer.latestTarget());
        }
    };
    // Fast work: every release falls exactly `lead` before consecutive vblanks. The
    // display's period wins over the 120.000 FPS cap, so there is no beat at all.
    std::vector<uint64_t> releases,targets;
    {
        FramePacer pacer;const RefreshGrid grid{777*us,period};uint64_t clock=1000*ms;
        simulate(pacer,grid,clock,[](unsigned){return 4*ms;},3000,releases,targets);
        for(size_t i=1;i<releases.size();++i) {
            need(releases[i]-releases[i-1]==period,"Locked releases are not one refresh apart");
            need(targets[i]-releases[i]==lead && grid.distance(targets[i])==0,"Locked release is not lead before a vblank");
        }
        need(pacer.locked() && pacer.onSlot(),"Fast locked work was not on its slot");
    }
    // A late frame inside the lead is released at once and keeps its vblank; the next
    // frame returns to its own slot without a catch-up burst.
    {
        FramePacer pacer;const RefreshGrid grid{0,period};uint64_t clock=1000*ms;
        simulate(pacer,grid,clock,[](unsigned i){return i==40?period+ms:6*ms;},80,releases,targets);
        need(releases[40]-releases[39]==period+ms,"Late locked frame was delayed");
        need(targets[40]==targets[39]+period,"Late frame inside the lead lost its vblank");
        // The 1 ms of debt raises the lead (the buffer for the next stretch) by up to 1 ms.
        need(targets[41]==targets[40]+period && targets[41]-releases[41]>=lead && targets[41]-releases[41]<=lead+ms,
             "Frame after a late one left the grid");
        // Beyond half a refresh late: the next vblank becomes the target.
        simulate(pacer,grid,clock,[](unsigned i){return i==10?period+period*3/4:6*ms;},20,releases,targets);
        need(targets[10]==targets[9]+2*period && targets[11]==targets[10]+period,"Very late frame was not moved to the next vblank");
    }
    // A grid that re-anchors by a few microseconds every frame never drops or doubles a slot.
    {
        FramePacer pacer;uint64_t clock=1000*ms,previous=0;
        for(unsigned i=0;i<2000;++i) {
            const RefreshGrid grid{(i%2?3:0)*us+i%7,period};
            clock+=5*ms;clock=pacer.releaseLocked(clock,grid,1,lead,ceiling);pacer.complete(clock);
            if(i)need(clock-previous+10*us>=period && clock-previous<=period+10*us,"Grid jitter dropped or doubled a slot");
            previous=clock;
        }
    }
    // Two refreshes per frame (60 FPS cap at 119.84 Hz).
    {
        FramePacer pacer;const RefreshGrid grid{0,period};uint64_t clock=1000*ms,previous=0;
        for(unsigned i=0;i<500;++i) {
            clock+=6*ms;clock=pacer.releaseLocked(clock,grid,2,lead,ceiling);pacer.complete(clock);
            if(i)need(clock-previous==2*period,"60 FPS lock is not two refreshes apart");
            previous=clock;
        }
    }

    // Display feedback. Present ids start at 1; flips are reported one frame later.
    const auto run=[&](FramePacer& pacer,RefreshGrid grid,uint64_t& clock,uint32_t& present,auto&& workTicks,
                       auto&& flipDelay,unsigned frames,unsigned* repeats=nullptr,std::vector<unsigned>* blocked=nullptr) {
        uint64_t lastVblank=0;
        uint64_t pendingFlip=0;uint32_t pendingPresent=0;
        for(unsigned i=0;i<frames;++i) {
            clock+=workTicks(i);
            const uint64_t release=pacer.releaseLocked(clock,grid,1,lead,ceiling);pacer.complete(release);
            // Two swap-chain buffers: Present waits until the previous frame is on screen.
            if(blocked && lastVblank>release)blocked->push_back(i);
            clock=std::max(release,lastVblank);
            const uint32_t id=++present;pacer.submitted(id);
            if(pendingPresent)pacer.displayed(pendingPresent,pendingFlip);
            // The display shows the frame at the first vblank at least flipDelay after
            // release, and never on the vblank of the previous frame.
            uint64_t vblank=grid.next(release+flipDelay(i));
            if(lastVblank && vblank<=lastVblank){vblank=lastVblank+period;}
            if(repeats && lastVblank && vblank-lastVblank>period)*repeats+=uint32_t((vblank-lastVblank)/period-1);
            lastVblank=vblank;pendingFlip=vblank;pendingPresent=id;
        }
    };
    {
        // A pipeline that needs 1.4 ms never misses a 2 ms lead and leaves the lead alone.
        FramePacer pacer;const RefreshGrid grid{0,period};uint64_t clock=1000*ms;uint32_t present=0;unsigned repeats=0;
        run(pacer,grid,clock,present,[](unsigned){return 6*ms;},[](unsigned){return 1400*us;},4000,&repeats);
        need(repeats==0 && pacer.misses()==0 && pacer.lead()==lead,"A sufficient lead missed or changed");
        need(pacer.fixedRefresh(),"Flips on the refresh grid did not confirm a fixed refresh");
    }
    {
        // A pipeline that needs 2.6 ms: the lead grows until frames stop missing.
        FramePacer pacer;const RefreshGrid grid{0,period};uint64_t clock=1000*ms;uint32_t present=0;unsigned repeats=0;
        run(pacer,grid,clock,present,[](unsigned){return 6*ms;},[](unsigned){return 2600*us;},1200,&repeats);
        need(pacer.lead()>=2600*us && pacer.lead()<=3100*us,"Lead did not settle just above the pipeline requirement");
        // Settled: only the periodic relax probe (one lead step every kRelaxAfter on-time
        // frames) may miss, twice, before the lead returns.
        const unsigned before=repeats,missesBefore=pacer.misses();
        run(pacer,grid,clock,present,[](unsigned){return 6*ms;},[](unsigned){return 2600*us;},2000,&repeats);
        need(pacer.misses()-missesBefore<=4 && repeats-before<=4,"A settled lead kept missing vblanks");
        need(pacer.lead()>=2600*us && pacer.lead()<=3100*us,"Settled lead drifted");
        // Once the pipeline is fast again, the lead relaxes back to its minimum.
        run(pacer,grid,clock,present,[](unsigned){return 6*ms;},[](unsigned){return 1000*us;},20000);
        need(pacer.lead()==lead,"Lead did not relax after misses stopped");
    }
    {
        // Regular 12 ms frames (3.7 ms over a refresh) are absorbed once the lead covers
        // them: the cadence moves earlier and each slow frame still reaches its vblank.
        FramePacer pacer;const RefreshGrid grid{0,period};uint64_t clock=1000*ms;uint32_t present=0;unsigned repeats=0;
        const auto work=[](unsigned i){return i%40==39?12*ms:6*ms;};
        run(pacer,grid,clock,present,work,[](unsigned){return 1400*us;},2400,&repeats);
        need(pacer.lead()>=12*ms-period+1400*us && pacer.lead()<=12*ms-period+2400*us,"Lead did not grow to absorb regular slow frames");
        const unsigned before=repeats;
        run(pacer,grid,clock,present,work,[](unsigned){return 1400*us;},2000,&repeats);
        need(repeats-before<=4,"Absorbed slow frames still repeated refreshes");
        need(pacer.lead()<=period*7/8,"Lead exceeded most of a refresh");
    }
    {
        // Heavy stretches: six 9 ms frames (0.66 ms over a refresh each) every 400 frames.
        // The first stretch runs out of the 2 ms minimum lead; the recorded peak debt then
        // keeps a buffer that absorbs every later stretch for as long as stretches keep
        // coming within 30 seconds of each other; 30 calm seconds return it to the minimum.
        FramePacer pacer;const RefreshGrid grid{0,period};uint64_t clock=1000*ms;uint32_t present=0;
        const auto work=[](unsigned i){return i%400>=200 && i%400<206?9*ms:6*ms;};
        unsigned first=0;
        run(pacer,grid,clock,present,work,[](unsigned){return 1400*us;},400,&first);
        need(first>=1,"The first heavy stretch did not exceed the minimum lead");
        // 3.9 ms of debt from the work itself, plus the Present block behind the missed frame.
        need(pacer.lead()>=lead+3*ms && pacer.lead()<=period*7/8,"Lead did not follow the recorded peak debt");
        unsigned later=0;
        run(pacer,grid,clock,present,work,[](unsigned){return 1400*us;},4000,&later);
        need(later==0,"A recorded heavy stretch still repeated refreshes");
        run(pacer,grid,clock,present,[](unsigned){return 6*ms;},[](unsigned){return 1400*us;},3000);
        need(pacer.lead()>=lead+3*ms,"Debt-driven lead expired within 30 seconds of a stretch");
        run(pacer,grid,clock,present,[](unsigned){return 6*ms;},[](unsigned){return 1400*us;},1000);
        need(pacer.lead()==lead,"Debt-driven lead outlived its 30-second window");
    }
    {
        // After a spike miss, fast frames queue behind the late one and Present blocks;
        // the schedule drains that queue within two frames instead of staying behind.
        FramePacer pacer;const RefreshGrid grid{0,period};uint64_t clock=1000*ms;uint32_t present=0;
        std::vector<unsigned> blocked;unsigned repeats=0;
        run(pacer,grid,clock,present,[](unsigned){return 4*ms;},[](unsigned i){return i==500?3*ms:1400*us;},1000,&repeats,&blocked);
        need(repeats==1 && pacer.misses()==1,"A spike did not cost exactly one repeated refresh");
        need(!blocked.empty() && blocked.size()<=2 && blocked.front()==501,"The queue behind a late frame did not drain");
    }
    {
        // Each display-path spike (an on-time frame that still missed) grows the path lead
        // by a quarter of the minimum; it relaxes only after 30 quiet seconds.
        FramePacer pacer;const RefreshGrid grid{0,period};uint64_t clock=1000*ms;uint32_t present=0;
        run(pacer,grid,clock,present,[](unsigned){return 6*ms;},[](unsigned i){return i%1000==500?3*ms:1400*us;},3000);
        // (A miss also blocks the next Present, whose debt can raise the lead further.)
        // Two 3 ms spikes miss and grow the lead to 3 ms; the third then reaches its vblank.
        need(pacer.misses()==2 && pacer.lead()>=lead+2*(lead/4) && pacer.lead()<=period*7/8,"Display-path spikes did not grow the path lead");
    }
    {
        // One very slow frame (beyond its vblank) and the frame queued behind it are not
        // lead misses: no lead could have kept them.
        FramePacer pacer;const RefreshGrid grid{0,period};uint64_t clock=1000*ms;uint32_t present=0;
        run(pacer,grid,clock,present,[](unsigned i){return i%300==150?20*ms:6*ms;},[](unsigned){return 1400*us;},3000);
        need(pacer.misses()==0 && pacer.lead()==lead,"A long frame or its queued successor raised the lead");
    }
    {
        // A composed window: every frame flips one refresh after the vblank it aimed for.
        // The lead grows to its maximum first (a short lead also looks like this); once
        // no lead helps, the constant latency is not a miss: each refresh still shows a
        // new frame and no frame waits an extra refresh to "drain".
        // (The compositor queues the frame, so Present does not wait for the flip.)
        FramePacer pacer;const RefreshGrid grid{0,period};uint64_t clock=1000*ms;uint32_t present=0;
        const auto composedRun=[&](unsigned frames,uint64_t composition,unsigned spike,unsigned& repeats) {
            uint64_t lastVblank=0,pendingFlip=0;uint32_t pendingPresent=0;
            for(unsigned i=0;i<frames;++i) {
                clock+=6*ms;clock=pacer.releaseLocked(clock,grid,1,lead,ceiling);pacer.complete(clock);
                const uint32_t id=++present;pacer.submitted(id);
                if(pendingPresent)pacer.displayed(pendingPresent,pendingFlip);
                uint64_t vblank=grid.next(clock+(i==spike?8*ms:1400*us))+composition; // 8 ms: past even the longest lead.
                if(lastVblank && vblank<=lastVblank)vblank=lastVblank+period;
                if(lastVblank && vblank-lastVblank>period)repeats+=unsigned((vblank-lastVblank)/period-1);
                lastVblank=pendingFlip=vblank;pendingPresent=id;
            }
        };
        unsigned repeats=0;
        composedRun(100,period,~0u,repeats);
        need(pacer.displayLatency()==1,"A constant one-refresh display latency was not learned");
        repeats=0;const uint32_t missesBefore=pacer.misses();
        composedRun(2000,period,~0u,repeats);
        need(repeats==0,"A constant display latency repeated refreshes");
        need(pacer.misses()==missesBefore && pacer.lead()<=period*7/8,"A constant display latency counted as lead misses");
        // A real spike on top of it is still a miss, and the latency drops with the window.
        repeats=0;
        composedRun(1000,period,500,repeats);
        need(repeats==1 && pacer.misses()==missesBefore+1,"A spike past the display latency was not a single miss");
        composedRun(100,0,~0u,repeats);
        need(pacer.displayLatency()==0,"The display latency did not drop when flips became direct");
    }
    {
        // A burst of slow frames (as after a stall) steps up to four refreshes; once frames
        // fit again the extra refreshes go within about half a second, not a full history.
        FramePacer pacer;const RefreshGrid grid{0,period};uint64_t clock=1000*ms;uint32_t present=0;
        run(pacer,grid,clock,present,[](unsigned){return 6*ms;},[](unsigned){return 1400*us;},200);
        run(pacer,grid,clock,present,[](unsigned){return 30*ms;},[](unsigned){return 1400*us;},12);
        need(pacer.extraRefreshes()>=2,"A burst of 30 ms frames did not step up");
        unsigned toOne=0,toZero=0;uint32_t extra=pacer.extraRefreshes();
        for(unsigned i=1;i<=200 && !toZero;++i) {
            run(pacer,grid,clock,present,[](unsigned){return 6*ms;},[](unsigned){return 1400*us;},1);
            need(pacer.extraRefreshes()<=extra,"Recovery after a burst stepped back up");
            extra=pacer.extraRefreshes();
            if(!toOne && extra<=1)toOne=i;
            if(!extra)toZero=i;
        }
        need(toOne && toOne<=24,"Extra refreshes after a burst outlived half a second");
        need(toZero && toZero<=64,"Recovery after a burst did not reach every refresh");
    }
    {
        // A workload too heavy for every refresh from the very first frame is held (the
        // display is not confirmed yet); its flips still land on the grid, which confirms
        // the fixed refresh and replaces the uneven held cadence with two refreshes.
        FramePacer pacer;const RefreshGrid grid{0,period};uint64_t clock=1000*ms;uint32_t present=0;
        run(pacer,grid,clock,present,[](unsigned){return 10*ms;},[](unsigned){return 1400*us;},40);
        need(pacer.held(),"A heavy start on an unconfirmed display was not held");
        run(pacer,grid,clock,present,[](unsigned){return 10*ms;},[](unsigned){return 1400*us;},200);
        need(pacer.fixedRefresh() && pacer.extraRefreshes()==1 && pacer.latestInterval()==2*period,
             "Held flips on the grid did not confirm a fixed refresh");
    }
    {
        // Flips off the grid (variable refresh) never confirm a fixed refresh.
        FramePacer pacer;const RefreshGrid grid{0,period};uint64_t clock=1000*ms;uint32_t present=0;
        for(unsigned i=0;i<500;++i) {
            clock+=6*ms;const uint64_t release=pacer.releaseLocked(clock,grid,1,lead,ceiling);pacer.complete(release);clock=release;
            pacer.submitted(++present);pacer.displayed(present,release+700*us+(i%5)*100*us);
        }
        need(!pacer.fixedRefresh(),"Off-grid flips confirmed a fixed refresh");
        // Sustained 9 ms work then holds its own even cadence instead of stepping refreshes.
        for(unsigned i=0;i<200;++i){clock+=9*ms;clock=pacer.releaseLocked(clock,grid,1,lead,ceiling);pacer.complete(clock);}
        need(pacer.held() && pacer.latestInterval()==9*ms,"Variable refresh did not hold the sustained cadence");
    }
    {
        // On a fixed refresh, typical frames above one refresh step to two refreshes
        // (an even 59.9 FPS) and come back once nearly every frame fits again.
        FramePacer pacer;const RefreshGrid grid{0,period};uint64_t clock=1000*ms;uint32_t present=0;
        run(pacer,grid,clock,present,[](unsigned){return 6*ms;},[](unsigned){return 1400*us;},200);
        need(pacer.fixedRefresh(),"Fixed refresh was not confirmed");
        unsigned repeats=0;
        run(pacer,grid,clock,present,[](unsigned){return 10*ms;},[](unsigned){return 1400*us;},400,&repeats);
        need(pacer.extraRefreshes()==1 && pacer.latestInterval()==2*period,"Unsustainable fixed-refresh rate did not step to two refreshes");
        std::vector<uint64_t> held;
        for(unsigned i=0;i<200;++i){clock+=10*ms;clock=pacer.releaseLocked(clock,grid,1,lead,ceiling);pacer.complete(clock);held.push_back(clock);}
        for(size_t i=1;i<held.size();++i)need(held[i]-held[i-1]==2*period,"Two-refresh cadence was uneven");
        run(pacer,grid,clock,present,[](unsigned){return 6*ms;},[](unsigned){return 1400*us;},200);
        need(pacer.extraRefreshes()==0 && pacer.latestInterval()==period,"Fixed refresh did not return to every refresh");
    }

    // Gameplay clock prediction.
    {
        FramePacer pacer;const RefreshGrid grid{0,period};uint64_t clock=1000*ms;
        need(pacer.predict(clock)==clock,"Unpaced prediction differs from real time");
        uint64_t previousValue=0;
        for(unsigned i=0;i<300;++i) {
            const uint64_t start=clock+30*us+(i%7)*20*us; // Update start jitters after the release.
            const uint64_t value=pacer.predict(start);
            if(i>2)need(value-previousValue==period,"Locked prediction is not one refresh per update");
            previousValue=value;
            clock=start+5*ms;clock=pacer.releaseLocked(clock,grid,1,lead,ceiling);pacer.complete(clock);
            if(i>2)need(pacer.latestTarget()==value,"Locked prediction differs from the vblank the frame got");
        }
        // A second update before the next release advances by real time only.
        const uint64_t start=clock+50*us,first=pacer.predict(start);
        need(pacer.predict(start+200*us)==first+200*us,"Second update in a frame did not advance by real time");
        // Free pacing predicts the planned release.
        FramePacer free;uint64_t t=1000*ms;
        for(unsigned i=0;i<50;++i){const uint64_t value=free.predict(t+10*us);t+=4*ms;t=free.release(t,cap120,ceiling);free.complete(t);
            if(i>1)need(value==t,"Free prediction differs from the release the frame got");}
        // A reset never moves the clock backwards.
        const uint64_t before=pacer.predict(clock+2*period+70*us);
        pacer.reset();
        need(pacer.predict(clock+2*period+80*us)>before,"Reset moved the gameplay clock backwards");
    }
}
}
