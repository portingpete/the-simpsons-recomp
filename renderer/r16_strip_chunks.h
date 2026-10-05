#pragma once
#include <cstdint>

namespace Simpsons::Graphics {
// Original8244D5D8..D614 caps a packet at65535 indices. Primitive6's
// factor1 and even quotient produce chunks65534; table821D3D30's overlap2
// advances by65532. Each packet has not_eop0 and begins a fresh strip.
// Call only after validating the complete selected range against its owned
// nonempty buffers. That native owner bound also prevents start arithmetic
// from wrapping. Zero count still emits the original no-fetch packet.
template<class Emit>
void forEachOriginalR16StripChunk(uint32_t start,uint32_t count,Emit&& emit) {
    while(count>65535) {
        emit(65534u,start);
        start+=65532;
        count-=65532;
    }
    emit(count,start);
}
}
