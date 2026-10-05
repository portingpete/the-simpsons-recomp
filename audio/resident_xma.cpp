#include "resident_xma.h"
#include "story_resident_xma_certificates.h"
#include "loc_resident_xma_certificates.h"
#include "characters_resident_xma_certificates.h"
#include "homer_resident_xma_certificates.h"
#include "loc_global_resident_xma_certificates.h"
#include <algorithm>
#include <windows.h>
#include <bcrypt.h>

namespace Simpsons::Audio {
namespace {
void need(bool value,const char* why) {if(!value) throw XmaSourceError(why);}
void identity(std::span<const uint8_t> bytes,const char* expected) {
    need(bytes.size()<=ULONG_MAX,"Resident XMA hash extent overflow");
    std::array<uint8_t,32> digest{};
    need(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(bytes.data()),
         ULONG(bytes.size()),digest.data(),ULONG(digest.size()))>=0,"Resident XMA hash failed");
    for(size_t i=0;i<digest.size();++i)
        need(expected[2*i]=="0123456789abcdef"[digest[i]>>4] && expected[2*i+1]=="0123456789abcdef"[digest[i]&15],
             "Resident XMA bytes differ from the qualified original asset");
}
}
void validateResidentXmaBank(std::span<const uint8_t> bytes) {
    need(bytes.size()==residentBankBytes,"Resident SBK extent changed");
    identity(bytes,"6605ab30a96453c9108af5866b3670ddda719cafe36301f857ae6279bc136482");
}
const ResidentBankProfile* residentBankProfile(uint32_t bytes) {
    static const std::array<ResidentBankProfile,7> banks={{
        {"frontend.sbk",residentBankBytes,0x1B28,residentAudioOffset,residentAudioBytes,
         "6605ab30a96453c9108af5866b3670ddda719cafe36301f857ae6279bc136482",residentXmaProfiles},
        {"Story_Mode_Design.sbk",4293314,0x1526C,0x152C0,4293314-0x152C0,
         "0392a01388db435d73cd81a565c9bc94273a13c53245cb30d596283594bdeca1",storyResidentXmaProfiles},
        {"loc.sbk",2551861,0x7700,0x7740,0x2678F5,
         "702aebda44b3a0f36866b35b89d00c0fd7d5cbfa8dfd7409df4cc562122e71eb",locResidentXmaProfiles,locResidentLoopProfiles},
        {"simpsons_chars_global.sbk",1212431,0x43A8,0x43C0,1195087,
         "4cedafc16ca0c681a0e4284ca8ae65e38ef19197ce29387a3ac8cac2ed50f078",charactersResidentXmaProfiles,charactersResidentLoopProfiles},
        {"homer.sbk",2030922,0xB6B0,0xB700,2030922-0xB700,
         "a667c0b91b2f8a92c2254c334c5ab9fa13f9fb111f83d0370c12921c2bee9688",homerResidentXmaProfiles,homerResidentLoopProfiles},
        {"homer_fh0_gh0_hh0_h0.sbk",1776651,0x9A90,0x9AC0,1776651-0x9AC0,
         "f14720a2436677a4f5d09a6008bb5bee346ecbdfc6567086687bb8c41e7d7a76",homerVariantResidentXmaProfiles,homerVariantResidentLoopProfiles},
        {"loc_global.sbk",2948197,0x17C30,0x17C80,0x2B7FE5,
         "145197be5688def500325f86d2c4896ac3ddbf751be95c388393e22bbec2e6df",locGlobalResidentXmaProfiles,locGlobalResidentLoopProfiles}
    }};
    for(const auto& bank:banks)if(bank.bytes==bytes)return &bank;
    return nullptr;
}
void validateResidentXmaBank(const ResidentBankProfile& bank,std::span<const uint8_t> bytes) {
    need(bytes.size()==bank.bytes && bank.audioOffset+bank.audioBytes==bank.bytes &&
         bank.metadataBytes+24<=bank.audioOffset,"Resident bank extent/split differs");
    identity(bytes,bank.hash);
}
const ResidentXmaProfile* residentXmaProfile(const ResidentBankProfile& bank,uint32_t offset,std::span<const uint8_t> header) {
    if(header.size()!=8)return nullptr;
    for(const auto& profile:bank.profiles)
        if(offset==profile.headerOffset && std::equal(header.begin(),header.end(),profile.header.begin()))return &profile;
    if(const auto* loop=residentLoopProfile(bank,offset))
        if(std::equal(header.begin(),header.end(),loop->blocks[0].header.begin()))return &loop->blocks[0];
    return nullptr;
}
const ResidentLoopProfile* residentLoopProfile(const ResidentBankProfile& bank,uint32_t offset) {
    for(const auto& loop:bank.loops)if(loop.headerOffset==offset)return &loop;
    return nullptr;
}
const ResidentXmaProfile* residentXmaProfile(std::span<const uint8_t> header) {
    if(header.size()!=8)return nullptr;
    for(const auto& profile:residentXmaProfiles)
        if(std::equal(header.begin(),header.end(),profile.header.begin()))return &profile;
    return nullptr;
}
EaXmaBlock parseResidentXmaBlock(std::span<const uint8_t> bytes,uint32_t channels,std::span<const uint8_t> header) {
    const auto* profile=residentXmaProfile(header);
    need(profile,"Unqualified resident XMA header");
    return parseResidentXmaBlock(*profile,bytes,channels,header);
}
EaXmaBlock parseResidentXmaBlock(const ResidentXmaProfile& selected,std::span<const uint8_t> bytes,uint32_t channels,std::span<const uint8_t> header) {
    const auto* profile=&selected;
    need(channels==profile->channels && header.size()==8 && std::equal(header.begin(),header.end(),profile->header.begin()),"Resident XMA header/channels differ from selected source");
    need(bytes.size()==profile->blockBytes,"Resident XMA block extent changed");
    identity(bytes,profile->blockHash);
    auto word=[&](size_t at) {return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|
                                  uint32_t(bytes[at+2])<<8|uint32_t(bytes[at+3]);};
    need(word(0)==bytes.size() && word(4)==profile->frames && (word(8)&3)==profile->codecSelector &&
         (word(8)>>2)==bytes.size()-8 &&
         profile->restoredFF==(NativeXmaCodec::packetBytes-(bytes.size()-12)%NativeXmaCodec::packetBytes)%NativeXmaCodec::packetBytes,
         "Resident XMA block/layer framing changed");
    EaXmaBlock result;result.rawHeader=word(0);result.declaredFrames=word(4);
    XmaSource::LayerInput layer;
    layer.packets.resize((bytes.size()-12+profile->restoredFF)/NativeXmaCodec::packetBytes);
    for(size_t i=0;i<layer.packets.size();++i) {
        auto& packet=layer.packets[i];packet.fill(0xFF);
        const auto at=12+i*NativeXmaCodec::packetBytes;
        std::copy_n(bytes.begin()+at,std::min(size_t(NativeXmaCodec::packetBytes),bytes.size()-at),packet.begin());
    }
    // Only the exact hashed/profiled payload may restore its recorded FF tail.
    // All original payload bits remain unchanged; guest memory is never padded.
    result.layers.push_back(std::move(layer));
    return result;
}
}
