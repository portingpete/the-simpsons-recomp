#include "header/test_frame_pacer.h"
#include "header/test_frame_pacer_locked.h"
#include <cstdio>
#include <stdexcept>

int main() {try {
    size_t checks=0;
    const auto need=[&](bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);};
    FramePacerTests::run(need);
    FramePacerLockedTests::run(need);
    std::printf("PASS native frame pacer: %zu checks; cap drift, actual wake cadence, stalls, presentation blocking, reset, clock wrap, "
                "foreground/background policy, refresh locking, lead feedback, display latency, fixed-refresh steps, burst recovery and gameplay clock prediction\n",checks);
    return 0;
} catch(const std::exception& error) {std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}}
