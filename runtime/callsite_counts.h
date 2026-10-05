#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Simpsons {
// Opt-in diagnostic (SIMPSONS_CALLSITE_COUNTS=1): counts calls of selected per-draw validators
// by caller return address and prints the busiest call sites every 600 presented frames as
// module RVAs (symbolize with llvm-symbolizer --relative-address against the same PDB).
// Owner (render) thread only; never enabled by default.
struct CallSiteCounts {
    const char* name;
    std::unordered_map<uintptr_t,uint64_t> counts;
    static bool enabled() {
        static const bool value=[]{const char* text=std::getenv("SIMPSONS_CALLSITE_COUNTS");return text&&*text&&*text!='0';}();
        return value;
    }
    void note(void* returnAddress) {if(enabled())++counts[reinterpret_cast<uintptr_t>(returnAddress)];}
    void dump(uint64_t frames) {
        if(!enabled()||counts.empty()||!frames)return;
        std::vector<std::pair<uint64_t,uintptr_t>> rows;uint64_t total=0;
        for(const auto& [address,count]:counts){rows.push_back({count,address});total+=count;}
        std::sort(rows.rbegin(),rows.rend());
        const auto module=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        std::fprintf(stderr,"[CALLSITE COUNTS] %s calls/frame=%.1f sites=%zu\n",name,double(total)/double(frames),rows.size());
        for(size_t i=0;i<rows.size()&&i<20;++i)
            std::fprintf(stderr,"[CALLSITE COUNTS]   %s rva=0x%llx %.1f/frame\n",name,
                static_cast<unsigned long long>(rows[i].second-module),double(rows[i].first)/double(frames));
        counts.clear();
    }
};
inline CallSiteCounts cameraBindingCalls{"cameraBinding",{}},validatePoolCalls{"validatePool",{}};
}
