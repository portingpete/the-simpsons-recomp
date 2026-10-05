#pragma once
#include <cstdint>

namespace Simpsons {
// Brent's cycle detector for a read-only, singly linked walk. Unlike a set of
// visited nodes, this retains constant local state and allocates no heap nodes.
// The independent visit limit also bounds malformed/mutating guest chains.
// The caller still validates every node, link, owner and terminating sentinel.
template<uint32_t Limit> class GuestChainWalk {
    static_assert(Limit>0 && Limit<=0x10000000);
    uint32_t count{},anchor{},power=1,distance{};
public:
    bool visit(uint32_t address) noexcept {
        if(!address || count>=Limit)return false;
        if(count++==0){anchor=address;return true;}
        if(address==anchor)return false;
        if(++distance==power){anchor=address;power*=2;distance=0;}
        return true;
    }
};
}
