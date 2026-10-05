#include "audio_catalog.h"
#include "menu_xma_certificates.h"
#include <algorithm>
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>

namespace Simpsons::Audio {
namespace {
void need(bool ok,const char* why) {if(!ok)throw XmaSourceError(why);}
uint32_t u32(std::span<const uint8_t> bytes,size_t at) {
    need(at<=bytes.size() && bytes.size()-at>=4,"Truncated audio catalog word");
    return uint32_t(bytes[at]) | uint32_t(bytes[at+1])<<8 | uint32_t(bytes[at+2])<<16 | uint32_t(bytes[at+3])<<24;
}
uint64_t u64(std::span<const uint8_t> bytes,size_t at) {
    return uint64_t(u32(bytes,at)) | uint64_t(u32(bytes,at+4))<<32;
}
uint16_t u16(std::span<const uint8_t> bytes,size_t at) {
    need(at<=bytes.size() && bytes.size()-at>=2,"Truncated audio catalog halfword");
    return uint16_t(bytes[at] | uint16_t(bytes[at+1])<<8);
}
uint32_t be32(std::span<const uint8_t> bytes,size_t at) {
    need(at<=bytes.size() && bytes.size()-at>=4,"Truncated EA-XMA stream header");
    return uint32_t(bytes[at])<<24 | uint32_t(bytes[at+1])<<16 |
           uint32_t(bytes[at+2])<<8 | bytes[at+3];
}
uint64_t headerKey(std::span<const uint8_t> header) {
    need(header.size()==8,"EA-XMA catalog lookup requires an exact 8-byte header");
    uint64_t key=0;for(uint8_t byte:header)key=(key<<8)|byte;return key;
}
std::array<uint8_t,32> sha256(std::span<const uint8_t> bytes) {
    need(bytes.size()<=ULONG_MAX,"Audio catalog block exceeds native hash extent");
    std::array<uint8_t,32> digest{};
    need(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(bytes.data()),
         ULONG(bytes.size()),digest.data(),ULONG(digest.size()))>=0,"Audio catalog block hash failed");
    return digest;
}
bool sameBlock(const AudioCatalog::Block& a,const AudioCatalog::Block& b) {
    return a.bytes==b.bytes && a.frames==b.frames && a.terminalPadding==b.terminalPadding &&
           a.payloadBytes==b.payloadBytes && a.restoredFF==b.restoredFF &&
           a.normalizedHash==b.normalizedHash;
}
std::string hex(std::span<const uint8_t> bytes) {
    std::string result;result.reserve(bytes.size()*2);
    for(uint8_t byte:bytes){result.push_back("0123456789abcdef"[byte>>4]);result.push_back("0123456789abcdef"[byte&15]);}
    return result;
}
}
AudioCatalog AudioCatalog::load(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input)throw XmaSourceError("Audio catalog missing or unreadable: "+path.string());
    const auto length=input.tellg();
    need(length>=76 && length<=std::streamoff(512*1024*1024),"Audio catalog file extent is invalid");
    std::vector<uint8_t> bytes(size_t(length),uint8_t{});
    input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),length);
    need(bool(input),"Audio catalog file read was incomplete");
    const std::span<const uint8_t> data(bytes);
    need(std::memcmp(data.data(),"SIMAUD01",8)==0 && u32(data,8)==1,"Audio catalog magic/version mismatch");
    const uint32_t sources=u32(data,12),streams=u32(data,16),blocks=u32(data,20),layers=u32(data,24);
    const uint64_t sourceOffset=u64(data,28),streamOffset=u64(data,36),blockOffset=u64(data,44),
                   layerOffset=u64(data,52),stringOffset=u64(data,60),stringBytes=u64(data,68);
    need(sources && sources<=100000 && streams && streams<=100000 && blocks && blocks<=1000000 &&
         layers && layers<=3000000 && stringBytes<=32*1024*1024,
         "Audio catalog table counts exceed bounded profile");
    need(sourceOffset==76 && streamOffset==sourceOffset+uint64_t(sources)*64 &&
         blockOffset==streamOffset+uint64_t(streams)*72 &&
         layerOffset==blockOffset+uint64_t(blocks)*100 &&
         stringOffset==layerOffset+uint64_t(layers)*36 &&
         stringOffset<=data.size() && stringBytes==data.size()-stringOffset,
         "Audio catalog table offsets are inconsistent");
    AudioCatalog catalog;catalog.sources_.reserve(sources);catalog.streams_.reserve(streams);
    catalog.blocks_.reserve(blocks);
    uint32_t nextStream=0;
    for(uint32_t i=0;i<sources;++i) {
        const size_t at=size_t(sourceOffset)+size_t(i)*64;
        const uint64_t pathOffset=u64(data,at),fileBytes=u64(data,at+24);
        const uint32_t pathLength=u32(data,at+8),first=u32(data,at+12),count=u32(data,at+16),kind=u32(data,at+20);
        need(pathLength && pathLength<=4096 && pathOffset<=stringBytes && pathLength<=stringBytes-pathOffset &&
             fileBytes && (kind==1 || kind==2) &&
             count && first==nextStream && count<=streams-first &&
             (kind!=1 || count==1),"Audio catalog source span/identity is invalid");
        std::string name(reinterpret_cast<const char*>(data.data()+size_t(stringOffset+pathOffset)),pathLength);
        need(name.find('\0')==std::string::npos && name.find('\\')==std::string::npos &&
             name.front()!='/' && name.find("../")==std::string::npos && name!="..",
             "Audio catalog source path is invalid");
        catalog.sources_.push_back({std::move(name),first,count,kind,fileBytes});
        std::copy_n(data.begin()+at+32,32,catalog.sources_.back().fileHash.begin());
        nextStream+=count;
    }
    need(nextStream==streams,"Audio catalog source table does not cover all streams");
    uint32_t nextBlock=0;
    for(uint32_t i=0;i<streams;++i) {
        const size_t at=size_t(streamOffset)+size_t(i)*72;
        Stream s;s.source=u32(data,at);s.ordinal=u32(data,at+4);
        const uint64_t headerOffset=u64(data,at+8),audioOffset=u64(data,at+16),audioBytes=u64(data,at+24);
        s.firstBlock=u32(data,at+32);s.blockCount=u32(data,at+36);
        s.sampleRate=u32(data,at+40);s.channels=u32(data,at+44);s.frames=u32(data,at+48);
        s.loopStartSample=u32(data,at+52);s.loopOffsetRelative=u32(data,at+56);
        const uint32_t flags=u32(data,at+60);
        std::copy_n(data.begin()+at+64,8,s.header.begin());s.loop=(flags&1)!=0;
        const uint32_t h0=be32(s.header,0),h1=be32(s.header,4);
        need(s.source<sources && i>=catalog.sources_[s.source].firstStream &&
             i-catalog.sources_[s.source].firstStream<catalog.sources_[s.source].streamCount &&
             s.ordinal==(catalog.sources_[s.source].kind==1?0u:i-catalog.sources_[s.source].firstStream) &&
             headerOffset<=catalog.sources_[s.source].fileBytes &&
             8<=catalog.sources_[s.source].fileBytes-headerOffset &&
             audioOffset<=catalog.sources_[s.source].fileBytes &&
             audioBytes<=catalog.sources_[s.source].fileBytes-audioOffset &&
             audioBytes && s.firstBlock==nextBlock && s.blockCount && s.blockCount<=blocks-nextBlock &&
             s.sampleRate==48000 && (s.channels==1 || s.channels==2 || s.channels==4 || s.channels==6) &&
             s.frames && flags<2 && ((h0>>28)==0) && ((h0>>24)&15)==3 &&
             ((h0>>18)&63)+1==s.channels && (h0&0x3ffff)==s.sampleRate &&
             h1>>30==1 && ((h1>>29)&1)==unsigned(s.loop) &&
             (h1&0x1fffffff)==s.frames &&
             (s.loop ? (s.loopStartSample>0 && s.loopStartSample<s.frames &&
                        s.loopOffsetRelative>0 && s.loopOffsetRelative<audioBytes)
                     : (!s.loopStartSample && !s.loopOffsetRelative)),
             "Audio catalog stream header/span is invalid");
        s.headerOffset=headerOffset;s.audioOffset=audioOffset;s.audioBytes=audioBytes;
        catalog.byHeader_[headerKey(s.header)].push_back(i);
        catalog.streams_.push_back(s);nextBlock+=s.blockCount;
    }
    need(nextBlock==blocks,"Audio catalog stream table does not cover all blocks");
    // The live producer has a header and reader-owned blocks, not a source
    // path. An unresolved same-header alias must agree on loop control.
    for(const auto& [header,ids]:catalog.byHeader_) {
        (void)header;
        const auto& first=catalog.streams_[ids.front()];
        for(uint32_t id:ids) {
            const auto& other=catalog.streams_[id];
            need(other.sampleRate==first.sampleRate && other.channels==first.channels &&
                 other.frames==first.frames && other.loop==first.loop &&
                 other.loopStartSample==first.loopStartSample &&
                 other.loopOffsetRelative==first.loopOffsetRelative,
                 "Audio catalog same-header aliases disagree on stream/loop control");
        }
    }
    uint32_t nextLayer=0;
    for(uint32_t i=0;i<blocks;++i) {
        const size_t at=size_t(blockOffset)+size_t(i)*100;
        const uint32_t streamIndex=u32(data,at),ordinal=u32(data,at+4);
        const uint64_t rawOffset=u64(data,at+8);
        Block b;b.bytes=u32(data,at+16);b.frames=u32(data,at+20);
        const uint32_t firstLayer=u32(data,at+24),layerCount=u16(data,at+28),segmentFlag=u16(data,at+30);
        b.terminalPadding=u32(data,at+32);
        std::copy_n(data.begin()+at+68,32,b.normalizedHash.begin());
        need(streamIndex<streams && i>=catalog.streams_[streamIndex].firstBlock &&
             ordinal==i-catalog.streams_[streamIndex].firstBlock &&
             ordinal<catalog.streams_[streamIndex].blockCount &&
             b.bytes>=8 && b.bytes<=1024*1024 && b.frames && b.frames<=65536 &&
             firstLayer==nextLayer && layerCount==(catalog.streams_[streamIndex].channels+1)/2 &&
             layerCount<=layers-nextLayer && (segmentFlag==0 || segmentFlag==0x80) &&
             b.terminalPadding<=63,
             "Audio catalog block span/framing is invalid");
        const auto& s=catalog.streams_[streamIndex];
        const uint64_t audioOffset=u64(data,size_t(streamOffset)+size_t(streamIndex)*72+16),
                       audioBytes=u64(data,size_t(streamOffset)+size_t(streamIndex)*72+24);
        need(rawOffset>=audioOffset && rawOffset<=audioOffset+audioBytes &&
             b.bytes<=audioOffset+audioBytes-rawOffset &&
             rawOffset==(ordinal?u64(data,size_t(blockOffset)+size_t(i-1)*100+8)+catalog.blocks_.back().bytes:audioOffset),
             "Audio catalog block ordering/extent is invalid");
        catalog.blocks_.push_back(b);nextLayer+=layerCount;
    }
    need(nextLayer==layers,"Audio catalog block table does not cover all layers");
    for(uint32_t i=0;i<layers;++i) {
        const size_t at=size_t(layerOffset)+size_t(i)*36;
        const uint32_t blockIndex=u32(data,at),ordinal=u32(data,at+4);
        const uint64_t layerHeader=u64(data,at+8),payloadOffset=u64(data,at+20);
        const uint32_t layerBytes=u32(data,at+16),payloadBytes=u32(data,at+28),restoredFF=u32(data,at+32);
        need(blockIndex<blocks && ordinal<3 && layerBytes>=8 && payloadBytes==layerBytes-4 &&
             payloadOffset==layerHeader+4 &&
             restoredFF==(2048-payloadBytes%2048)%2048,
             "Audio catalog layer framing is invalid");
        // The layer row's block index and ordinal must match the block range.
        const size_t blockAt=size_t(blockOffset)+size_t(blockIndex)*100;
        const uint32_t first=u32(data,blockAt+24),count=u16(data,blockAt+28);
        const uint64_t rawOffset=u64(data,blockAt+8),rawEnd=rawOffset+catalog.blocks_[blockIndex].bytes;
        need(i>=first && i-first==ordinal && ordinal<count &&
             layerHeader==(ordinal?u64(data,size_t(layerOffset)+size_t(i-1)*36+8)+u32(data,size_t(layerOffset)+size_t(i-1)*36+16):rawOffset+8) &&
             layerHeader<=rawEnd && layerBytes<=rawEnd-layerHeader,
             "Audio catalog layer ordering/extent is invalid");
        auto& b=catalog.blocks_[blockIndex];b.payloadBytes[ordinal]=payloadBytes;b.restoredFF[ordinal]=restoredFF;
        if(ordinal+1==count)need(layerHeader+layerBytes+b.terminalPadding==rawEnd,
                                 "Audio catalog terminal padding differs from layer spans");
    }
    for(uint32_t i=0;i<streams;++i) {
        const auto& s=catalog.streams_[i];uint64_t frames=0;unsigned ends=0;uint64_t introEnd=0,introFrames=0;
        const uint64_t audioOffset=u64(data,size_t(streamOffset)+size_t(i)*72+16),
                       audioBytes=u64(data,size_t(streamOffset)+size_t(i)*72+24);
        for(uint32_t j=0;j<s.blockCount;++j) {
            const size_t at=size_t(blockOffset)+size_t(s.firstBlock+j)*100;
            frames+=catalog.blocks_[s.firstBlock+j].frames;
            if(u16(data,at+30)==0x80) {
                ++ends;
                if(ends==1) {
                    introEnd=u64(data,at+8)+catalog.blocks_[s.firstBlock+j].bytes-audioOffset;
                    introFrames=frames;
                    if(s.loop)need(j==0,"Looped SNU introduction exceeds one EAAC block");
                }
            }
        }
        const size_t last=size_t(blockOffset)+size_t(s.firstBlock+s.blockCount-1)*100;
        need(frames==s.frames && ends==(s.loop?2u:1u) && u16(data,last+30)==0x80 &&
             u64(data,last+8)+catalog.blocks_.at(s.firstBlock+s.blockCount-1).bytes==audioOffset+audioBytes &&
              (!s.loop || (s.loopStartSample==1 && introFrames==s.loopStartSample &&
                           introEnd==s.loopOffsetRelative)),
             "Audio catalog stream samples/segment ends are inconsistent");
    }
    return catalog;
}
std::optional<AudioCatalog::CandidateIdentity> AudioCatalog::candidateIdentity(uint32_t streamIndex) const {
    if(streamIndex>=streams_.size())return std::nullopt;
    const auto& stream=streams_[streamIndex];
    if(stream.source>=sources_.size())return std::nullopt;
    const auto& source=sources_[stream.source];
    return CandidateIdentity{streamIndex,stream.source,source.kind,stream.ordinal,
        source.path,source.fileHash,source.fileBytes,stream.headerOffset,
        stream.audioOffset,stream.audioBytes,stream.header};
}
const AudioCatalog::Stream* AudioCatalog::findHeader(std::span<const uint8_t> header) const {
    if(header.size()!=8)return nullptr;
    auto it=byHeader_.find(headerKey(header));return it==byHeader_.end()?nullptr:&streams_[it->second.front()];
}
AudioCatalog::Match AudioCatalog::match(std::span<const uint8_t> header,std::span<const uint32_t> prior,
                                       uint64_t sequence,std::span<const uint8_t> ownedBytes) const {
    need(sequence && sequence<=std::numeric_limits<uint32_t>::max() && !ownedBytes.empty(),
         "Audio catalog source sequence/owned block is invalid");
    need((sequence==1)==prior.empty(),"Audio catalog candidate continuity is invalid");
    const auto it=byHeader_.find(headerKey(header));
    need(it!=byHeader_.end(),"Audio catalog has no source for the streamed EA-XMA header");
    const auto& search=prior.empty()?std::span<const uint32_t>(it->second):prior;
    const auto digest=sha256(ownedBytes);
    Match result;
    for(uint32_t id:search) {
        need(id<streams_.size() && streams_[id].header==std::array<uint8_t,8>{header[0],header[1],header[2],header[3],header[4],header[5],header[6],header[7]},
             "Audio catalog candidate/header association changed");
        const auto& s=streams_[id];
        if(sequence>s.blockCount)continue;
        const auto& block=blocks_[s.firstBlock+size_t(sequence-1)];
        if(block.bytes!=ownedBytes.size() || block.normalizedHash!=digest)continue;
        need(result.candidates.empty() || sameBlock(result.block,block),
             "Audio catalog candidates disagree on an identical owned block");
        if(result.candidates.empty())result.block=block;
        result.candidates.push_back(id);
    }
    if(result.candidates.empty()) {
        char message[220];const auto head=hex(header),hash=hex(digest);
        std::snprintf(message,sizeof(message),"Audio catalog has no ordered owned block: header=%s sequence=%llu sha256=%s",
                      head.c_str(),static_cast<unsigned long long>(sequence),hash.c_str());
        throw XmaSourceError(message);
    }
    return result;
}
bool AudioCatalog::complete(std::span<const uint32_t> candidates,uint64_t sequence) const {
    if(candidates.empty())return false;
    for(uint32_t id:candidates)if(id>=streams_.size() || sequence!=streams_[id].blockCount)return false;
    return true;
}
EaXmaBlock parseCatalogEaXmaBlock(std::span<const uint8_t> ownedBytes,uint32_t channels,
                                  const AudioCatalog::Block& block) {
    const auto hash=hex(block.normalizedHash);
    const EaXmaCertificate certificate{hash,block.bytes,block.frames,block.payloadBytes,
                                       block.restoredFF,block.terminalPadding};
    return parseEaXmaBlockCertified(ownedBytes,channels,certificate);
}
}
