#include "ea_xma_block.h"
#include "menu_xma_certificates.h"
#include "menu_start_xma_certificates.h"
#include "all_menu_xma_certificates.h"
#include "loc_music_xma_certificates.h"
#include "mono_dialogue_xma_certificates.h"
#include <algorithm>
#include <windows.h>
#include <bcrypt.h>

namespace Simpsons::Audio {
namespace {
std::span<const EaXmaCertificate> certificates(std::span<const uint8_t> header) {
    if(header.size()!=8)return {};
    for(const auto& profile:menuXmaProfiles)
        if(std::equal(header.begin(),header.end(),profile.header.begin()))return profile.blocks;
    for(const auto& profile:locMusicXmaProfiles)
        if(std::equal(header.begin(),header.end(),profile.header.begin()))return profile.blocks;
    for(const auto& profile:monoDialogueProfiles)
        if(std::equal(header.begin(),header.end(),profile.header.begin()))return profile.blocks;
    return {};
}
}
uint32_t qualifiedEaXmaFrames(std::span<const uint8_t> header) {
    if(certificates(header).empty())return 0;
    return (uint32_t(header[4]&0x3f)<<24)|(uint32_t(header[5])<<16)|(uint32_t(header[6])<<8)|header[7];
}
uint32_t qualifiedEaXmaChannels(std::span<const uint8_t> header) {
    if(certificates(header).empty())return 0;
    return (header[1]>>2)+1;
}
EaXmaBlock parseEaXmaBlock(std::span<const uint8_t> bytes, uint32_t channels,
                          uint64_t sequence, std::span<const uint8_t> streamHeader) {
    auto require = [](bool ok, const char* reason) { if (!ok) throw XmaSourceError(reason); };
    const auto source=certificates(streamHeader);
    require(channels&&channels==qualifiedEaXmaChannels(streamHeader) && sequence && sequence<=source.size(), "Unqualified EA XMA source identity/sequence");
    return parseEaXmaBlockCertified(bytes,channels,source[size_t(sequence-1)]);
}
EaXmaBlock parseEaXmaBlockCertified(std::span<const uint8_t> bytes,uint32_t channels,
                                   const EaXmaCertificate& certificate) {
    auto require = [](bool ok, const char* reason) { if (!ok) throw XmaSourceError(reason); };
    require(channels>=1 && channels<=6,"Unsupported EA XMA channel count");
    require(bytes.size()==certificate.bytes, "Qualified EA XMA block extent changed");
    std::array<uint8_t,32> digest{};
    require(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(bytes.data()),
            ULONG(bytes.size()),digest.data(),ULONG(digest.size()))>=0, "EA XMA source hash failed");
    for(size_t i=0;i<digest.size();++i)
        require(certificate.sha256[2*i]=="0123456789abcdef"[digest[i]>>4] &&
                certificate.sha256[2*i+1]=="0123456789abcdef"[digest[i]&15],
                "EA XMA owned source does not match its qualified sequence");
    auto word = [&](size_t at) {
        require(at <= bytes.size() && bytes.size() - at >= 4, "Truncated EA XMA word");
        return uint32_t(bytes[at]) << 24 | uint32_t(bytes[at+1]) << 16 |
               uint32_t(bytes[at+2]) << 8 | uint32_t(bytes[at+3]);
    };
    require(bytes.size() >= 8 && bytes.size() <= 1024 * 1024, "Unsupported EA XMA block extent");
    EaXmaBlock result;
    result.rawHeader = word(0);
    require(result.rawHeader == bytes.size(), "EA XMA reader-normalized header/owned extent disagree");
    result.declaredFrames = word(4);
    require(result.declaredFrames == certificate.frames && result.declaredFrames &&
            result.declaredFrames <= 65536, "Unsupported EA XMA declared frame extent");
    size_t at = 8;
    for (uint32_t i = 0; i < (channels + 1) / 2; ++i) {
        const auto header = word(at), length = header >> 2;
        require((header & 3) == 3 && length >= 8 && length <= bytes.size() - at,
                "Unqualified EA XMA layer extent/rate selector");
        XmaSource::LayerInput layer;
        const size_t size = length - 4;
        require(size==certificate.payloadBytes[i] &&
                certificate.restoredFF[i]==(NativeXmaCodec::packetBytes-size%NativeXmaCodec::packetBytes)%NativeXmaCodec::packetBytes,
                "EA XMA payload extent/restoration certificate differs");
        layer.packets.resize((size + NativeXmaCodec::packetBytes - 1) / NativeXmaCodec::packetBytes);
        for (size_t offset = 0, packet = 0; offset < size; offset += NativeXmaCodec::packetBytes, ++packet) {
            auto& owned = layer.packets[packet];
            owned.fill(0xFF);
            const auto count = std::min(size - offset, size_t(NativeXmaCodec::packetBytes));
            std::copy_n(bytes.begin() + at + 4 + offset, count, owned.begin());
        }
        result.layers.push_back(std::move(layer));
        at += length;
    }
    require(at<=bytes.size() && bytes.size()-at==certificate.terminalPadding &&
            std::all_of(bytes.begin()+at,bytes.end(),[](uint8_t value){return value==0;}),
            "Unqualified EA XMA trailing bytes");
    return result;
}
}
