#pragma once
#include "xma_source.h"

namespace Simpsons::Audio {
// Only independently qualified, ordered source blocks may restore packet tails.
// Input is the normalized, owned reader claim; guest memory is never padded.
struct EaXmaBlock {
    uint32_t rawHeader = 0, declaredFrames = 0;
    std::vector<XmaSource::LayerInput> layers;
};
struct EaXmaCertificate;
// Zero means the exact source header has no independent admission certificate.
uint32_t qualifiedEaXmaFrames(std::span<const uint8_t> streamHeader);
uint32_t qualifiedEaXmaChannels(std::span<const uint8_t> streamHeader);
EaXmaBlock parseEaXmaBlock(std::span<const uint8_t> bytes, uint32_t channels,
                          uint64_t sequence, std::span<const uint8_t> streamHeader);
EaXmaBlock parseEaXmaBlockCertified(std::span<const uint8_t> bytes, uint32_t channels,
                                    const EaXmaCertificate& certificate);
}
