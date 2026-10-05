#include "resident_audio_catalog.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <windows.h>
#include <bcrypt.h>

namespace Simpsons::Audio {
namespace {
void need(bool ok, const char* reason) {
    if(!ok) throw std::runtime_error(reason);
}
uint32_t u32(std::span<const uint8_t> data, size_t at) {
    need(at<=data.size() && data.size()-at>=4,"Truncated resident catalog word");
    return uint32_t(data[at]) | uint32_t(data[at+1])<<8 |
           uint32_t(data[at+2])<<16 | uint32_t(data[at+3])<<24;
}
uint64_t u64(std::span<const uint8_t> data, size_t at) {
    return uint64_t(u32(data,at)) | uint64_t(u32(data,at+4))<<32;
}
uint32_t be32(std::span<const uint8_t> data, size_t at) {
    need(at<=data.size() && data.size()-at>=4,"Truncated resident EAAC header");
    return uint32_t(data[at])<<24 | uint32_t(data[at+1])<<16 |
           uint32_t(data[at+2])<<8 | data[at+3];
}
std::array<uint8_t,32> sha256(std::span<const uint8_t> bytes) {
    need(bytes.size()<=ULONG_MAX,"Resident hash input exceeds native extent");
    std::array<uint8_t,32> digest{};
    need(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,
                   const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),
                   digest.data(),ULONG(digest.size()))>=0,
         "Resident SHA-256 failed");
    return digest;
}
std::string stringAt(std::span<const uint8_t> data, uint64_t base, uint64_t bytes,
                     uint64_t offset, uint32_t count) {
    need(count && count<=4096 && offset<=bytes && count<=bytes-offset,
         "Resident catalog string extent is invalid");
    std::string value(reinterpret_cast<const char*>(data.data()+size_t(base+offset)),count);
    need(value.find('\0')==std::string::npos,"Resident catalog string contains NUL");
    return value;
}
}

ResidentAudioCatalog ResidentAudioCatalog::load(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input) throw std::runtime_error("Resident catalog missing or unreadable: "+path.string());
    const auto length=input.tellg();
    need(length>=64 && length<=std::streamoff(64*1024*1024),
         "Resident catalog file extent is invalid");
    std::vector<uint8_t> bytes(size_t(length),uint8_t{});
    input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),length);
    need(bool(input),"Resident catalog read was incomplete");
    const std::span<const uint8_t> data(bytes);
    need(std::memcmp(data.data(),"SIMRES01",8)==0 && u32(data,8)==1,
         "Resident catalog magic/version mismatch");
    const uint32_t banks=u32(data,12),cues=u32(data,16),blocks=u32(data,20);
    const uint64_t bankOffset=u64(data,24),cueOffset=u64(data,32),
                   blockOffset=u64(data,40),stringOffset=u64(data,48),stringBytes=u64(data,56);
    need(banks && banks<=4096 && cues && cues<=1000000 && blocks && blocks<=1000000 &&
         stringBytes<=8*1024*1024 && bankOffset==64 &&
         cueOffset==bankOffset+uint64_t(banks)*132 &&
         blockOffset==cueOffset+uint64_t(cues)*60 &&
         stringOffset==blockOffset+uint64_t(blocks)*72 &&
         stringOffset<=data.size() && stringBytes==data.size()-stringOffset,
         "Resident catalog table offsets/counts are invalid");
    ResidentAudioCatalog result;
    result.banks_.reserve(banks);result.cues_.reserve(cues);result.blocks_.reserve(blocks);
    uint32_t nextCue=0;
    for(uint32_t i=0;i<banks;++i) {
        const size_t at=size_t(bankOffset)+size_t(i)*132;
        const uint64_t pathOffset=u64(data,at),nameOffset=u64(data,at+12);
        const uint32_t pathBytes=u32(data,at+8),nameBytes=u32(data,at+20);
        Bank bank;
        bank.path=stringAt(data,stringOffset,stringBytes,pathOffset,pathBytes);
        bank.name=stringAt(data,stringOffset,stringBytes,nameOffset,nameBytes);
        bank.payloadBytes=u64(data,at+24);bank.metadataBytes=u64(data,at+32);
        bank.audioOffset=u64(data,at+40);bank.audioBytes=u64(data,at+48);
        bank.firstCue=u32(data,at+56);bank.cueCount=u32(data,at+60);
        bank.entryIndex=u32(data,at+64);
        std::copy_n(data.begin()+at+68,32,bank.payloadHash.begin());
        std::copy_n(data.begin()+at+100,32,bank.containerHash.begin());
        need(bank.path.front()!='/' && bank.path.find('\\')==std::string::npos &&
             bank.path.find("../")==std::string::npos && bank.path!=".." &&
             bank.name.find('/')==std::string::npos && bank.name.find('\\')==std::string::npos &&
             bank.payloadBytes && bank.payloadBytes<=64ull*1024*1024 &&
             bank.metadataBytes<=bank.audioOffset &&
             bank.audioOffset-bank.metadataBytes>=24 &&
             bank.audioOffset<=bank.payloadBytes &&
             bank.audioBytes==bank.payloadBytes-bank.audioOffset &&
             bank.firstCue==nextCue && bank.cueCount && bank.cueCount<=cues-nextCue,
             "Resident catalog bank identity/span is invalid");
        result.banks_.push_back(std::move(bank));
        nextCue+=result.banks_.back().cueCount;
    }
    need(nextCue==cues,"Resident catalog banks do not cover every cue");
    uint32_t nextBlock=0;
    for(uint32_t i=0;i<cues;++i) {
        const size_t at=size_t(cueOffset)+size_t(i)*60;
        Cue cue;
        cue.bankIndex=u32(data,at);cue.ordinal=u32(data,at+4);
        cue.headerOffset=u64(data,at+8);cue.firstBlock=u32(data,at+16);
        cue.blockCount=u32(data,at+20);cue.frames=u32(data,at+24);
        cue.playbackRate=u32(data,at+28);cue.channels=u32(data,at+32);
        cue.loopStartSample=u32(data,at+36);
        const uint32_t flags=u32(data,at+40);
        std::copy_n(data.begin()+at+44,8,cue.header.begin());
        cue.endOffset=u64(data,at+52);cue.loop=(flags&1)!=0;
        need(cue.bankIndex<banks,"Resident cue bank index exceeds table");
        const auto& bank=result.banks_[cue.bankIndex];
        const uint32_t header0=be32(cue.header,0),header1=be32(cue.header,4);
        need(i>=bank.firstCue && i-bank.firstCue<bank.cueCount &&
             cue.ordinal==i-bank.firstCue && cue.firstBlock==nextBlock &&
             cue.blockCount==(cue.loop && cue.loopStartSample?2u:1u) &&
             cue.blockCount<=blocks-nextBlock &&
             cue.headerOffset>=bank.audioOffset && cue.headerOffset<bank.payloadBytes &&
             cue.endOffset>cue.headerOffset && cue.endOffset<=bank.payloadBytes &&
             (!cue.ordinal || result.cues_.back().endOffset==cue.headerOffset) &&
             (!(flags&~1u)) && cue.frames && cue.frames<=0x1FFFFFFF &&
             cue.playbackRate>=8000 && cue.playbackRate<=48000 &&
             (cue.channels==1 || cue.channels==2) &&
             (!cue.loop || cue.loopStartSample<cue.frames) &&
             (cue.loop || !cue.loopStartSample) &&
             header0>>28==0 && ((header0>>24)&15)==3 &&
             ((header0>>18)&63)+1==cue.channels &&
             (header0&0x3FFFF)==cue.playbackRate &&
             header1>>30==0 && ((header1>>29)&1)==uint32_t(cue.loop) &&
             (header1&0x1FFFFFFF)==cue.frames,
             "Resident cue identity/span/header is invalid");
        result.cues_.push_back(cue);nextBlock+=cue.blockCount;
        if(cue.ordinal+1==bank.cueCount)
            need(cue.endOffset==bank.payloadBytes,
                 "Resident bank cue sequence does not end at EOF");
    }
    need(nextBlock==blocks,"Resident cues do not cover every block");
    uint64_t expectedOffset=0;
    uint32_t frameSum=0;
    for(uint32_t i=0;i<blocks;++i) {
        const size_t at=size_t(blockOffset)+size_t(i)*72;
        Block block;block.cueIndex=u32(data,at);block.ordinal=u32(data,at+4);
        block.offset=u64(data,at+8);block.bytes=u32(data,at+16);
        block.frames=u32(data,at+20);block.selector=u32(data,at+24);
        block.codecRate=u32(data,at+28);block.restoredFF=u32(data,at+32);
        block.payloadBytes=u32(data,at+36);
        std::copy_n(data.begin()+at+40,32,block.hash.begin());
        need(block.cueIndex<cues,"Resident block cue index exceeds table");
        const auto& cue=result.cues_[block.cueIndex];
        need(i>=cue.firstBlock && i-cue.firstBlock<cue.blockCount &&
             block.ordinal==i-cue.firstBlock && block.bytes>12 &&
             block.bytes<=16*1024*1024 && block.payloadBytes==block.bytes-12 &&
             block.selector<4 &&
             block.codecRate==std::array<uint32_t,4>{24000,32000,44100,48000}[block.selector] &&
             block.restoredFF==(2048u-block.payloadBytes%2048u)%2048u &&
             block.frames && block.offset==
                 (block.ordinal?expectedOffset:cue.headerOffset+8+(cue.loop?4:0)) &&
             block.offset<=cue.endOffset && block.bytes<=cue.endOffset-block.offset,
             "Resident block extent/rate/packet metadata is invalid");
        if(block.ordinal==0) frameSum=0;
        need(block.frames<=cue.frames && frameSum<=cue.frames-block.frames,
             "Resident block sample sum exceeds cue");
        frameSum+=block.frames;expectedOffset=block.offset+block.bytes;
        result.blocks_.push_back(block);
        if(block.ordinal+1==cue.blockCount)
            need(expectedOffset==cue.endOffset && frameSum==cue.frames &&
                 (!cue.loop || !cue.loopStartSample ||
                  result.blocks_[cue.firstBlock].frames==cue.loopStartSample),
                 "Resident block sequence does not cover cue");
    }
    return result;
}

const ResidentAudioCatalog::Bank* ResidentAudioCatalog::findBank(
    const std::array<uint8_t,32>& hash,uint64_t bytes) const noexcept {
    for(const auto& bank:banks_)
        if(bank.payloadBytes==bytes && bank.payloadHash==hash) return &bank;
    return nullptr;
}
const ResidentAudioCatalog::Bank* ResidentAudioCatalog::findBank(
    std::span<const uint8_t> payload) const {
    return findBank(sha256(payload),payload.size());
}
const ResidentAudioCatalog::Cue* ResidentAudioCatalog::cue(
    const Bank& bank,uint32_t ordinal) const noexcept {
    return ordinal<bank.cueCount && bank.firstCue<cues_.size() &&
           ordinal<cues_.size()-bank.firstCue?&cues_[bank.firstCue+ordinal]:nullptr;
}
const ResidentAudioCatalog::Cue* ResidentAudioCatalog::findCue(
    const Bank& bank,uint64_t headerOffset,std::span<const uint8_t> header) const noexcept {
    if(header.size()!=8 || bank.firstCue>cues_.size() ||
       bank.cueCount>cues_.size()-bank.firstCue) return nullptr;
    const auto first=cues_.begin()+bank.firstCue;
    const auto last=first+bank.cueCount;
    const auto it=std::lower_bound(first,last,headerOffset,
        [](const Cue& cue,uint64_t offset){return cue.headerOffset<offset;});
    return it!=last && it->headerOffset==headerOffset &&
           std::equal(header.begin(),header.end(),it->header.begin())?&*it:nullptr;
}
const ResidentAudioCatalog::Block* ResidentAudioCatalog::findBlock(
    const Cue& cue,uint32_t ordinal) const noexcept {
    return ordinal<cue.blockCount && cue.firstBlock<blocks_.size() &&
           ordinal<blocks_.size()-cue.firstBlock?&blocks_[cue.firstBlock+ordinal]:nullptr;
}
bool ResidentAudioCatalog::verifyBank(const Bank& bank,std::span<const uint8_t> payload) const {
    return payload.size()==bank.payloadBytes && sha256(payload)==bank.payloadHash;
}
bool ResidentAudioCatalog::verifyBlock(const Block& block,std::span<const uint8_t> bytes) const {
    return bytes.size()==block.bytes && sha256(bytes)==block.hash;
}
}
