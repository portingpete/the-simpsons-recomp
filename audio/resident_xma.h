#pragma once
#include "ea_xma_block.h"
#include "frontend_resident_xma_certificates.h"

namespace Simpsons::Audio {
// Exact original frontend.sbk source qualified by qualify_resident_xma.py.
// The EA playback rate and the XMA context selector describe different clocks.
inline constexpr std::array<uint8_t,8> residentXmaHeader={0x03,0x04,0x1F,0x40,0x00,0x06,0x67,0x8E};
inline constexpr uint32_t residentBankBytes=515055, residentHeaderOffset=33335;
// Original82807B60 separates the metadata prefix from this audio section.
inline constexpr uint32_t residentAudioOffset=0x1B40, residentAudioBytes=0x7C0AF;
inline constexpr uint32_t residentAudioHeaderOffset=residentHeaderOffset-residentAudioOffset;
static_assert(residentAudioOffset+residentAudioBytes==residentBankBytes);
inline constexpr uint32_t residentBlockBytes=18444, residentFrames=419726, residentRawFrames=420352;
inline constexpr uint32_t residentCodecSelector=0, residentCodecRate=24000;
struct ResidentLoopProfile {
    uint32_t headerOffset,totalFrames,loopStart;
    // Each original resident block requests a fresh decoder context.
    std::span<const ResidentXmaProfile> blocks;
};
struct ResidentBankProfile {
    const char* name;
    uint32_t bytes,metadataBytes,audioOffset,audioBytes;
    const char* hash;
    std::span<const ResidentXmaProfile> profiles;
    std::span<const ResidentLoopProfile> loops={};
};
const ResidentBankProfile* residentBankProfile(uint32_t bytes);
const ResidentLoopProfile* residentLoopProfile(const ResidentBankProfile&,uint32_t headerOffset);
const ResidentXmaProfile* residentXmaProfile(const ResidentBankProfile&,uint32_t headerOffset,std::span<const uint8_t> header);
void validateResidentXmaBank(const ResidentBankProfile&,std::span<const uint8_t>);
EaXmaBlock parseResidentXmaBlock(const ResidentXmaProfile&,std::span<const uint8_t>,uint32_t channels,std::span<const uint8_t> header);
const ResidentXmaProfile* residentXmaProfile(std::span<const uint8_t> header);
void validateResidentXmaBank(std::span<const uint8_t>);
EaXmaBlock parseResidentXmaBlock(std::span<const uint8_t>,uint32_t channels,std::span<const uint8_t> header);
}
