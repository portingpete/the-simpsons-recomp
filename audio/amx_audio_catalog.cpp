#include "amx_audio_catalog.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <windows.h>
#include <bcrypt.h>

namespace Simpsons::Audio {
namespace {
void need(bool ok,const char* reason) {
    if(!ok) throw std::runtime_error(reason);
}
uint32_t u32(std::span<const uint8_t> data,size_t at) {
    need(at<=data.size() && data.size()-at>=4,"Truncated AMX catalog word");
    return uint32_t(data[at]) | uint32_t(data[at+1])<<8 |
           uint32_t(data[at+2])<<16 | uint32_t(data[at+3])<<24;
}
uint64_t u64(std::span<const uint8_t> data,size_t at) {
    return uint64_t(u32(data,at)) | uint64_t(u32(data,at+4))<<32;
}
uint32_t be32(std::span<const uint8_t> data,size_t at) {
    need(at<=data.size() && data.size()-at>=4,"Truncated AMX EAAC header");
    return uint32_t(data[at])<<24 | uint32_t(data[at+1])<<16 |
           uint32_t(data[at+2])<<8 | data[at+3];
}
std::array<uint8_t,32> sha256(std::span<const uint8_t> bytes) {
    need(bytes.size()<=ULONG_MAX,"AMX hash input exceeds native extent");
    std::array<uint8_t,32> digest{};
    need(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,
                   const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),
                   digest.data(),ULONG(digest.size()))>=0,
         "AMX SHA-256 failed");
    return digest;
}
}

AmxAudioCatalog AmxAudioCatalog::load(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input) throw std::runtime_error("AMX catalog missing or unreadable: "+path.string());
    const auto length=input.tellg();
    need(length>=64 && length<=std::streamoff(2*1024*1024),
         "AMX catalog file extent is invalid");
    std::vector<uint8_t> bytes(size_t(length),uint8_t{});
    input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),length);
    need(bool(input),"AMX catalog read was incomplete");
    const std::span<const uint8_t> data(bytes);
    need(std::memcmp(data.data(),"SIMAMX01",8)==0 && u32(data,8)==2,
         "AMX catalog magic/version mismatch");
    const uint32_t payloads=u32(data,12),cues=u32(data,16),blocks=u32(data,20);
    const uint64_t payloadOffset=u64(data,24),cueOffset=u64(data,32),
                   blockOffset=u64(data,40),stringOffset=u64(data,48),
                   stringBytes=u64(data,56);
    need(payloads && payloads<=64 && cues && cues<=10000 && blocks==cues &&
         stringBytes<=8192 && payloadOffset==64 &&
         cueOffset==payloadOffset+uint64_t(payloads)*100 &&
         blockOffset==cueOffset+uint64_t(cues)*84 &&
         stringOffset==blockOffset+uint64_t(blocks)*68 &&
         stringOffset<=data.size() && stringBytes==data.size()-stringOffset,
         "AMX catalog table offsets/counts are invalid");
    AmxAudioCatalog result;
    result.payloads_.reserve(payloads);result.cues_.reserve(cues);result.blocks_.reserve(blocks);
    uint32_t nextCue=0;
    for(uint32_t i=0;i<payloads;++i) {
        const size_t at=size_t(payloadOffset)+size_t(i)*100;
        const uint64_t nameOffset=u64(data,at);
        const uint32_t nameBytes=u32(data,at+8);
        Payload payload;
        payload.payloadBytes=u32(data,at+12);payload.metadataBytes=u32(data,at+16);
        payload.prefixBytes=u32(data,at+20);payload.audioEndOffset=u32(data,at+24);
        payload.firstCue=u32(data,at+28);payload.cueCount=u32(data,at+32);
        std::copy_n(data.begin()+at+36,32,payload.payloadHash.begin());
        std::copy_n(data.begin()+at+68,32,payload.audioHash.begin());
        need(nameBytes && nameBytes<=255 && nameOffset<=stringBytes &&
             nameBytes<=stringBytes-nameOffset &&
             payload.payloadBytes>=64 && payload.payloadBytes<=16*1024*1024 &&
             payload.metadataBytes<=payload.payloadBytes-64 &&
             payload.prefixBytes>=payload.metadataBytes+64 &&
             payload.prefixBytes<payload.audioEndOffset &&
             payload.audioEndOffset<=payload.payloadBytes &&
             payload.firstCue==nextCue && payload.cueCount &&
             payload.cueCount<=cues-nextCue,
             "AMX catalog payload identity/span is invalid");
        payload.name.assign(reinterpret_cast<const char*>(data.data()+
                            size_t(stringOffset+nameOffset)),nameBytes);
        need(payload.name.find('\0')==std::string::npos &&
             payload.name.find('/')==std::string::npos &&
             payload.name.find('\\')==std::string::npos,
             "AMX catalog payload name is invalid");
        result.payloads_.push_back(std::move(payload));
        nextCue+=result.payloads_.back().cueCount;
    }
    need(nextCue==cues,"AMX catalog payloads do not cover every cue");
    for(uint32_t i=0;i<cues;++i) {
        const size_t at=size_t(cueOffset)+size_t(i)*84;
        Cue cue;
        cue.payloadIndex=u32(data,at);cue.ordinal=u32(data,at+4);
        cue.headerOffset=u32(data,at+8);cue.endOffset=u32(data,at+12);
        cue.frames=u32(data,at+16);cue.playbackRate=u32(data,at+20);
        cue.channels=u32(data,at+24);cue.loopStartSample=u32(data,at+28);
        const uint32_t flags=u32(data,at+32);
        cue.firstBlock=u32(data,at+36);cue.blockCount=u32(data,at+40);
        std::copy_n(data.begin()+at+44,8,cue.header.begin());
        std::copy_n(data.begin()+at+52,32,cue.cueHash.begin());
        cue.loop=(flags&1)!=0;
        need(cue.payloadIndex<payloads,"AMX cue payload index exceeds table");
        const auto& payload=result.payloads_[cue.payloadIndex];
        const uint32_t first=be32(cue.header,0),second=be32(cue.header,4);
        need(i>=payload.firstCue && i-payload.firstCue<payload.cueCount &&
             cue.ordinal==i-payload.firstCue && cue.firstBlock==i &&
             cue.blockCount==1 && cue.headerOffset>=payload.prefixBytes &&
             cue.headerOffset<cue.endOffset && cue.endOffset<=payload.audioEndOffset &&
             (cue.ordinal?result.cues_.back().endOffset==cue.headerOffset:
                          cue.headerOffset==payload.prefixBytes) &&
             !(flags&~1u) && cue.frames && cue.frames<=4000000 &&
             cue.playbackRate>=8000 && cue.playbackRate<=48000 &&
             (cue.channels==1 || cue.channels==2) &&
             (!cue.loop || cue.loopStartSample<cue.frames) &&
             (cue.loop || !cue.loopStartSample) &&
             first>>28==0 && ((first>>24)&15)==3 &&
             ((first>>18)&63)+1==cue.channels &&
             (first&0x3FFFF)==cue.playbackRate &&
             second>>30==0 && ((second>>29)&1)==uint32_t(cue.loop) &&
             (second&0x1FFFFFFF)==cue.frames,
             "AMX cue extent/header/sample identity is invalid");
        result.cues_.push_back(cue);
        if(cue.ordinal+1==payload.cueCount)
            need(cue.endOffset==payload.audioEndOffset,
                 "AMX cue sequence does not cover audio section");
    }
    for(uint32_t i=0;i<blocks;++i) {
        const size_t at=size_t(blockOffset)+size_t(i)*68;
        Block block;
        block.cueIndex=u32(data,at);block.ordinal=u32(data,at+4);
        block.offset=u32(data,at+8);block.bytes=u32(data,at+12);
        block.frames=u32(data,at+16);block.selector=u32(data,at+20);
        block.codecRate=u32(data,at+24);block.restoredFF=u32(data,at+28);
        block.payloadBytes=u32(data,at+32);
        std::copy_n(data.begin()+at+36,32,block.hash.begin());
        need(block.cueIndex==i && block.ordinal==0,
             "AMX block does not belong to its cue");
        const auto& cue=result.cues_[i];
        need(block.offset==cue.headerOffset+(cue.loop?12u:8u) &&
             block.offset<cue.endOffset && block.bytes==cue.endOffset-block.offset &&
             block.bytes>12 && block.bytes<=16*1024*1024 &&
             block.payloadBytes==block.bytes-12 && block.frames==cue.frames &&
             block.selector<4 &&
             block.codecRate==std::array<uint32_t,4>{24000,32000,44100,48000}[block.selector] &&
             block.restoredFF==(2048u-block.payloadBytes%2048u)%2048u,
             "AMX block extent/rate/packet identity is invalid");
        result.blocks_.push_back(block);
    }
    return result;
}

const AmxAudioCatalog::Payload* AmxAudioCatalog::payload(uint32_t index) const noexcept {
    return index<payloads_.size()?&payloads_[index]:nullptr;
}
const AmxAudioCatalog::Payload* AmxAudioCatalog::findPayload(
    const std::array<uint8_t,32>& hash,uint64_t bytes) const noexcept {
    for(const auto& payload:payloads_)
        if(payload.payloadBytes==bytes && payload.payloadHash==hash) return &payload;
    return nullptr;
}
const AmxAudioCatalog::Payload* AmxAudioCatalog::findPayload(
    std::span<const uint8_t> bytes) const {
    return findPayload(sha256(bytes),bytes.size());
}
const AmxAudioCatalog::Cue* AmxAudioCatalog::cue(
    const Payload& payload,uint32_t ordinal) const noexcept {
    return ordinal<payload.cueCount && payload.firstCue<cues_.size() &&
           ordinal<cues_.size()-payload.firstCue?&cues_[payload.firstCue+ordinal]:nullptr;
}
const AmxAudioCatalog::Cue* AmxAudioCatalog::findCue(
    const Payload& payload,uint32_t headerOffset,std::span<const uint8_t> header) const noexcept {
    if(header.size()!=8 || payload.firstCue>cues_.size() ||
       payload.cueCount>cues_.size()-payload.firstCue) return nullptr;
    const auto first=cues_.begin()+payload.firstCue;
    const auto last=first+payload.cueCount;
    const auto it=std::lower_bound(first,last,headerOffset,
        [](const Cue& cue,uint32_t offset){return cue.headerOffset<offset;});
    return it!=last && it->headerOffset==headerOffset &&
           std::equal(header.begin(),header.end(),it->header.begin())?&*it:nullptr;
}
const AmxAudioCatalog::Block* AmxAudioCatalog::findBlock(
    const Cue& cue,uint32_t ordinal) const noexcept {
    return ordinal<cue.blockCount && cue.firstBlock<blocks_.size() &&
           ordinal<blocks_.size()-cue.firstBlock?&blocks_[cue.firstBlock+ordinal]:nullptr;
}
bool AmxAudioCatalog::verifyPayload(const Payload& payload,
                                    std::span<const uint8_t> bytes) const {
    return bytes.size()==payload.payloadBytes && sha256(bytes)==payload.payloadHash;
}
bool AmxAudioCatalog::verifyAudio(const Payload& payload,
                                  std::span<const uint8_t> bytes) const {
    return bytes.size()==payload.payloadBytes &&
           payload.prefixBytes<payload.audioEndOffset &&
           payload.audioEndOffset<=bytes.size() &&
           sha256(bytes.subspan(payload.prefixBytes,
                                payload.audioEndOffset-payload.prefixBytes))==payload.audioHash;
}
bool AmxAudioCatalog::verifyCue(const Cue& cue,std::span<const uint8_t> bytes) const {
    return bytes.size()==cue.endOffset-cue.headerOffset && sha256(bytes)==cue.cueHash;
}
bool AmxAudioCatalog::verifyBlock(const Block& block,std::span<const uint8_t> bytes) const {
    return bytes.size()==block.bytes && sha256(bytes)==block.hash;
}
}
