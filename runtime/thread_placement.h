#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace Simpsons {
struct HostProcessorCore {uintptr_t mask{};uint8_t efficiency{};};

// Keep logical CPUs1..5 on their existing allowed-processor mapping. Logical
// CPU0 may use a separate performance core, preferring the last eligible core
// as measured on the live title. A sibling of CPUs1..5 is not a separate core.
inline uintptr_t selectThreadProcessor(uintptr_t allowed,unsigned cpu,std::span<const HostProcessorCore> cores) {
    if(!allowed || cpu>=6)throw std::invalid_argument("Invalid native processor selection");
    std::array<uintptr_t,sizeof(uintptr_t)*8> processors{};size_t count=0;
    for(auto remaining=allowed;remaining;remaining&=remaining-1)
        processors[count++]=remaining&(~remaining+1);
    const auto original=processors[cpu%count];
    if(cpu || cores.empty())return original;
    uintptr_t occupied=0;
    for(unsigned other=1;other<6;++other)occupied|=processors[other%count];
    uint8_t fastest=0;
    for(const auto& core:cores)if(core.mask&allowed && core.efficiency>fastest)fastest=core.efficiency;
    uintptr_t selected=0;
    for(const auto& core:cores) {
        const auto eligible=core.mask&allowed;
        if(eligible && core.efficiency==fastest && !(core.mask&occupied)) {
            const auto processor=eligible&(~eligible+1);
            if(processor>selected)selected=processor;
        }
    }
    return selected?selected:original;
}
}
