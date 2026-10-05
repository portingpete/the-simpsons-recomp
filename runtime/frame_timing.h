#pragma once
#include "runtime.h"
#include <array>
#include <fstream>
#include <iomanip>

namespace Simpsons {
// Optional, owner-thread-only measurements. Nested columns are inclusive;
// frame intervals include game CPU work, GPU waits, display and captures.
class FrameTiming {
    std::ofstream output;
    std::array<int64_t,12> ticks{};
    inline static thread_local FrameTiming* current{};
    int64_t pacingStart{};
    int64_t frequency{},previous{},origin{};
    bool detailed=true;
    uint64_t windowFrames{};
    int64_t windowStart{};
    static int64_t now(){const DWORD saved=GetLastError();LARGE_INTEGER value{};QueryPerformanceCounter(&value);SetLastError(saved);return value.QuadPart;}
public:
    enum Bucket {Present,Upload,Im2D,Program,Draw,PresentCopy,PresentDisplay,PresentIdle,OriginalPacing,Im2DPreflight,Im2DSetup,BindingReset};
    class Scope {
        int64_t* total{};int64_t start{};
    public:
        explicit Scope(int64_t* value):total(value),start(value?now():0){}
        Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
        ~Scope(){finish();}
        void finish(){if(total){*total+=now()-start;total=nullptr;}}
    };
    explicit FrameTiming(const std::filesystem::path& path,bool buckets=true):detailed(buckets){if(path.empty())return;
        output.open(path,std::ios::out|std::ios::trunc);if(!output)throw Failure("Frame timing output could not be opened");
        LARGE_INTEGER value{};if(!QueryPerformanceFrequency(&value)||value.QuadPart<=0)throw Failure("Frame timing clock unavailable");frequency=value.QuadPart;
        output<<"presentation,elapsed_ms,frame_ms";
        if(detailed)output<<",present_ms,upload_ms,im2d_ms,program_ms,draw_ms,present_copy_ms,present_display_ms,present_idle_ms,original_pacing_ms,im2d_preflight_ms,im2d_setup_ms,binding_reset_ms";
        output<<",display_accepted\n";output.flush();
        if(current)throw Failure("Nested frame timing owner");current=this;
    }
    ~FrameTiming(){if(current==this)current=nullptr;}
    static void beginPacing(const PPCContext& ctx,uint8_t* base){if(!current || !current->detailed)return;
        if(current->pacingStart)throw Failure("Nested original frame pacing measurement");
        if(current->windowFrames==0)std::fprintf(stderr,"[ORIGINAL PACING] start=%08X intervals=%u enabled=%u rate_bits=%08X factor_bits=%08X caller=%08X\n",
            ctx.r3.u32,ctx.r4.u32,PPCLoadU32(base,0x82CED760),PPCLoadU32(base,0x82CF0394),PPCLoadU32(base,0x82D61D58),uint32_t(ctx.lr));
        current->pacingStart=now();
    }
    static void endPacing(){if(!current || !current->detailed)return;
        if(!current->pacingStart)throw Failure("Original frame pacing measurement has no start");
        current->ticks[OriginalPacing]+=now()-current->pacingStart;current->pacingStart=0;
    }
    Scope measure(Bucket bucket){return Scope(detailed && output.is_open()?&ticks[bucket]:nullptr);}
    void frame(uint64_t index,bool accepted){if(!output.is_open())return;
        const auto timestamp=now();const auto delta=previous?timestamp-previous:0;
        if(!origin){origin=timestamp;windowStart=timestamp;}previous=timestamp;
        const auto fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
        struct Restore{uint32_t fp;DWORD error;~Restore(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}} restore{fp,error};
        PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);
        const double factor=1000.0/double(frequency);
        output<<index<<','<<std::fixed<<std::setprecision(4)<<double(timestamp-origin)*factor<<','<<double(delta)*factor;
        if(detailed)for(auto value:ticks)output<<','<<double(value)*factor;
        output<<','<<(accepted?1:0)<<'\n';ticks.fill(0);
        if(delta)++windowFrames;
        if(index%120==0){output.flush();const double seconds=double(timestamp-windowStart)/double(frequency);
            std::fprintf(stderr,"[FRAME TIMING] presentation=%llu frames=%llu seconds=%.3f FPS=%.2f; actual presentation intervals\n",
                static_cast<unsigned long long>(index),static_cast<unsigned long long>(windowFrames),seconds,seconds>0?double(windowFrames)/seconds:0);
            std::fflush(stderr);
            windowFrames=0;windowStart=timestamp;}
        if(!output)throw Failure("Frame timing output write failed");
    }
};
}
