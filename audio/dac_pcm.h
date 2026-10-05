#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace Simpsons::Audio {
// Original six-channel Dac0 output at82E32000 and each S-owned slot is
// 256 interleaved frames of BE float32. This transport changes representation
// only: original interleave/fade/clamp remain AOT, including their NaN policy.
struct DacPcmBlock {
    static constexpr uint32_t channels=6,frames=256,samples=channels*frames,bytes=samples*4;
    std::array<float,samples> pcm;
    static DacPcmBlock copyBigEndian(std::span<const uint8_t> source) {
        if(source.size()!=bytes) throw std::invalid_argument("Dac0 PCM block must contain exactly 6144 bytes");
        DacPcmBlock out;
        for(uint32_t i=0;i<samples;++i) {
            const auto* p=source.data()+4*i;
            const uint32_t bits=uint32_t(p[0])<<24 | uint32_t(p[1])<<16 | uint32_t(p[2])<<8 | p[3];
            out.pcm[i]=std::bit_cast<float>(bits);
        }
        return out;
    }
};
static_assert(sizeof(float)==4 && DacPcmBlock::bytes==0x1800);
}
