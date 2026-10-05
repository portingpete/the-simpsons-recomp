#include "runtime.h"

namespace Simpsons {
void Runtime::invalidateWatchedPages(uint32_t address,uint64_t size) noexcept {
    // Mapping, commit, decommit and protection changes can change guest bytes
    // without a store; every alias of each page maps to one canonical page.
    static constexpr uint32_t adjustment[8]={0,0,0,0,0,0,0xE0000000,0xC0001000};
    guestPermissionEpoch.fetch_add(1,std::memory_order_seq_cst);
    for(uint64_t pos=address&~uint64_t(0xFFF);pos<uint64_t(address)+size&&pos<0xFFD00000ull;pos+=0x1000) {
        const uint32_t canonical=uint32_t(pos)+adjustment[uint32_t(pos)>>29];
        if(writeWatchArmed[canonical>>12].exchange(0,std::memory_order_relaxed)==1)
            guestWatchBlockAdjust(canonical>>12,-1);
        writeWatchVersion[canonical>>12].fetch_add(1,std::memory_order_release);
    }
}
}
