#pragma once
#include "engine_state.h"
#include <array>

namespace Simpsons::Graphics {
// Prospective state for original8282E3E8. The caller publishes only after a
// successful native draw. No RenderWare queue/cache is accessed or flushed.
struct MovieState {
    EngineState after;
    std::array<ScreenSamplerState,3> samplers;
    bool retainedDepthWrite{};
    uint32_t retainedDepthCompare{};
};
MovieState prepareMovieState(const EngineState& before);
}
