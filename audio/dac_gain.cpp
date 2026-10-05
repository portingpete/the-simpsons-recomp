#include "dac_gain.h"
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <xmmintrin.h>

#if defined(__FAST_MATH__)
#error Dac gain requires strict floating-point compilation
#endif
#pragma float_control(precise,on,push)
#pragma fenv_access(on)

namespace Simpsons::Audio {
namespace {
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);
bool finite(float value) {return (std::bit_cast<uint32_t>(value)&0x7F800000)!=0x7F800000;}
struct FloatScope {
    unsigned saved=_mm_getcsr();
    FloatScope() {_mm_setcsr(0x1F80);}
    ~FloatScope() {_mm_setcsr(saved);}
};
struct Range {uintptr_t begin,end;};
Range range(const void* p,size_t bytes) {
    const auto begin=reinterpret_cast<uintptr_t>(p);
    if((bytes && !p) || bytes>std::numeric_limits<uintptr_t>::max()-begin)
        throw DacGainError("Dac gain invalid native range");
    return {begin,begin+bytes};
}
bool overlap(Range a,Range b) {return a.begin<a.end && b.begin<b.end && a.begin<b.end && b.begin<a.end;}
// Explicit single-precision operations: no reassociation, fusion, double
// accumulation or implicit dependence on the caller's rounding/FTZ/DAZ state.
float sub(float a,float b) {return _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(a),_mm_set_ss(b)));}
float div(float a,float b) {return _mm_cvtss_f32(_mm_div_ss(_mm_set_ss(a),_mm_set_ss(b)));}
float mul(float a,float b) {return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(a),_mm_set_ss(b)));}
float add(float a,float b) {return _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(a),_mm_set_ss(b)));}
}

DacGainResult processDacGain(std::span<const float> source,
                            std::span<float> destination,DacGainState& state) {
    if(state.sourceFrames>dacGainPlaneFrames || state.destinationFrames>dacGainPlaneFrames ||
       state.sourceProgress>state.sourceFrames || state.destinationProgress>state.destinationFrames)
        throw DacGainError("Dac gain invalid frame counters");
    if(!finite(state.current) || !finite(state.target))
        throw DacGainError("Dac gain nonfinite current/target");
    const auto sourceRemaining=state.sourceFrames-state.sourceProgress;
    const auto remaining=state.destinationFrames-state.destinationProgress;
    if(!sourceRemaining || !remaining) return {DacGainWork::NoWork,0};
    if(source.size()>dacGainSamples || destination.size()>dacGainSamples)
        throw DacGainError("Dac gain span exceeds bounded block");
    const auto input=range(source.data(),source.size_bytes());
    const auto output=range(destination.data(),destination.size_bytes());
    const auto control=range(&state,sizeof(state));
    if(overlap(input,output) || overlap(input,control) || overlap(output,control))
        throw DacGainError("Dac gain overlapping storage");
    if(source.size()<state.sourceFrames*dacGainChannels ||
       destination.size()<5*dacGainPlaneFrames+state.destinationFrames)
        throw DacGainError("Dac gain insufficient span capacity");
    const auto n=std::min(sourceRemaining,remaining);
    const auto first=state.sourceProgress*dacGainChannels;
    for(uint32_t i=first;i<first+n*dacGainChannels;++i)
        if(!finite(source[i])) throw DacGainError("Dac gain nonfinite source sample");

    // Stage the whole bounded result before the first externally visible write.
    // All arithmetic rejection paths, including late current overflow, leave
    // caller-owned samples and state unchanged. No dynamic allocation on success.
    std::array<float,dacGainSamples> staged;
    FloatScope fp;
    const float difference=sub(state.target,state.current);
    if(!finite(difference)) throw DacGainError("Dac gain nonfinite gain difference");
    const float step=div(difference,static_cast<float>(remaining));
    if(!finite(step)) throw DacGainError("Dac gain nonfinite gain step");
    float current=state.current;
    for(uint32_t j=0;j<n;++j) {
        for(uint32_t lane=0;lane<dacGainChannels;++lane)
            staged[lane*dacGainPlaneFrames+j]=mul(source[first+j*dacGainChannels+lane],current);
        current=add(step,current);
        if(!finite(current)) throw DacGainError("Dac gain nonfinite advanced current");
    }
    for(uint32_t lane=0;lane<dacGainChannels;++lane)
        for(uint32_t j=0;j<n;++j)
            destination[lane*dacGainPlaneFrames+state.destinationProgress+j]=staged[lane*dacGainPlaneFrames+j];
    state.sourceProgress+=n;
    state.destinationProgress+=n;
    state.current=current;
    return {DacGainWork::Processed,n};
}
}
#pragma float_control(pop)
