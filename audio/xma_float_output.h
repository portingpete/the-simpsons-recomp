#pragma once
#include "xma_source.h"
#include <bit>
#include <cstring>

namespace Simpsons::Audio {
// The NativeRawF32 bridge retains the original converter's sparse 32-byte
// DCBZ footprint. Address alignment is guest alignment, not host alignment.
class XmaFloatOutput {
    uint32_t address_,stride_,frames_,channels_;
    std::array<std::span<const float>,6> planes_{};
public:
    XmaFloatOutput(uint32_t address,uint32_t stride,const XmaSource::Quota& quota)
        :address_(address),stride_(stride),frames_(quota.frames()),channels_(quota.channels()) {
        if(!address || (address&3) || !stride || stride>65535 || !frames_ || frames_>stride ||
           !channels_ || channels_>6 || uint64_t(address)+4ull*stride*channels_>0x100000000ull)
            throw XmaSourceError("Invalid native XMA output footprint");
        for(uint32_t i=0;i<channels_;++i) {
            planes_[i]=quota.plane(i);
            if(planes_[i].size()!=frames_) throw XmaSourceError("Incomplete native XMA output plane");
        }
    }
    uint32_t bytes() const noexcept {return 4*stride_*channels_;}
    void write(std::span<uint8_t> destination) const {
        if(destination.size()!=bytes()) throw XmaSourceError("Native XMA output destination extent changed");
        // Everything which can reject is checked before the first store.
        for(uint32_t component=0;component<channels_;component+=2) {
            const uint32_t offset=4*stride_*component;
            const bool stereo=component+1<channels_;
            if(!((address_+offset)&127))
                for(uint32_t k=0;k<frames_/(stereo?16u:32u);++k)
                    std::memset(destination.data()+offset+128*k,0,32);
        }
        for(uint32_t component=0;component<channels_;++component)
            for(uint32_t sample=0;sample<frames_;++sample) {
                const uint32_t word=std::bit_cast<uint32_t>(planes_[component][sample]);
                auto* target=destination.data()+4*(stride_*component+sample);
                target[0]=uint8_t(word>>24);target[1]=uint8_t(word>>16);
                target[2]=uint8_t(word>>8);target[3]=uint8_t(word);
            }
    }
};
}
