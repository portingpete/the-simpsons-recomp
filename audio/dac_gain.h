#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>

namespace Simpsons::Audio {
struct DacGainError:std::runtime_error {using std::runtime_error::runtime_error;};
struct DacGainState {
    uint32_t sourceFrames{},sourceProgress{};
    uint32_t destinationFrames{},destinationProgress{};
    float current{},target{};
};
enum class DacGainWork {Processed,NoWork};
struct DacGainResult {DacGainWork work;uint32_t frames;};
inline constexpr uint32_t dacGainChannels=6,dacGainPlaneFrames=256;
inline constexpr uint32_t dacGainSamples=dacGainChannels*dacGainPlaneFrames;

// Synchronous CPU-only scalar82C64960 profile. Caller owns valid native float
// storage for the duration; no concurrent access to output/state or input writes.
// Source is interleaved; output is six planes with fixed256-float stride. Both
// spans are bounded to1536 elements and must not overlap each other or state.
// Positive work requires sourceFrames*6 readable elements and at least
// 5*256+destinationFrames writable elements. No channel reorder/clamp is applied.
// Totals/progress are0..256, progress<=total. Finite current/target and consumed
// source values are required. Gain subtraction, step and all updated currents
// must stay finite; finite sample products may overflow to signed infinity.
// Violations throw DacGainError without output/state mutation. Valid exhausted
// work returns {NoWork,0} without accessing spans, after count/gain validation.
// Success changes only consumed output positions, progresses and current.
// Host MXCSR is restored exactly; arithmetic uses nearest-even gradual underflow.
// Build with /fp:strict. No NaN, guest exception flags, resampling, SDK voice,
// activation/category policy or general vector-kernel equivalence is claimed.
DacGainResult processDacGain(std::span<const float> source,
                            std::span<float> destination,DacGainState& state);
}
