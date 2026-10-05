#include "audio/amx_audio_catalog.h"
#include "audio/resident_xma.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void need(bool ok,const char* reason) {if(!ok)throw std::runtime_error(reason);}
std::array<uint8_t,32> hash(std::string_view hex) {
    need(hex.size()==64,"Invalid AMX fixture digest length");
    std::array<uint8_t,32> result{};
    auto digit=[](char c)->uint8_t {
        if(c>='0'&&c<='9')return uint8_t(c-'0');
        if(c>='a'&&c<='f')return uint8_t(c-'a'+10);
        throw std::runtime_error("Invalid AMX fixture digest digit");
    };
    for(size_t i=0;i<32;++i)result[i]=uint8_t((digit(hex[i*2])<<4)|digit(hex[i*2+1]));
    return result;
}
std::string hex(std::span<const uint8_t> bytes) {
    std::string out;out.reserve(bytes.size()*2);
    for(uint8_t byte:bytes) {
        out.push_back("0123456789abcdef"[byte>>4]);
        out.push_back("0123456789abcdef"[byte&15]);
    }
    return out;
}
}
int main(int argc,char** argv) {
    try {
        need(argc==3,"Provide AMX binary catalog and original payload paths");
        using Simpsons::Audio::AmxAudioCatalog;
        const auto catalog=AmxAudioCatalog::load(argv[1]);
        need(catalog.payloadCount()==8 && catalog.cueCount()==254 &&
             catalog.blockCount()==254,"AMX catalog corpus counts changed");
        const auto* item=catalog.findPayload(hash(
            "5243a51f37376f5b1bb8969db761b2a60982a55d792fb145a439ac46763b9412"),379448);
        need(item && item->name=="char_poles_and_ladders.amx" &&
             item->prefixBytes==2700 && item->audioEndOffset==379129 &&
             item->cueCount==16 && item->audioHash==hash(
                 "09321fe20432468e5f49ccb1e3ec99c4783c6e2be6eac3968151fa8c39ae7446"),
             "Original AMX payload/audio identity changed");
        const std::array<uint8_t,8> header={0x03,0x00,0xBB,0x80,0x00,0x00,0x0B,0x45};
        const auto* cue=catalog.findCue(*item,339952,header);
        const auto* block=cue?catalog.findBlock(*cue,0):nullptr;
        need(cue && cue->ordinal==11 && cue->frames==2885 &&
             cue->channels==1 && cue->playbackRate==48000 && !cue->loop &&
             cue->endOffset==341227 && block && block->offset==339960 &&
             block->bytes==1267 && block->frames==2885 &&
             block->selector==3 && block->codecRate==48000 &&
             block->restoredFF==793 && block->payloadBytes==1255 &&
             block->hash==hash("484a5856112f4003308ccc312b29e93c5894b14d01a234d057a9aedea4babec6"),
             "Original crash cue/block certificate changed");
        need(!catalog.findBlock(*cue,1) && !catalog.cue(*item,16),
             "Out-of-range AMX cue/block was admitted");
        auto wrong=header;wrong[7]^=1;
        need(!catalog.findCue(*item,339952,wrong) &&
             !catalog.findCue(*item,339953,header),
             "Unknown AMX header/offset was admitted");
        std::ifstream input(argv[2],std::ios::binary|std::ios::ate);
        need(bool(input) && input.tellg()==std::streamoff(item->payloadBytes),
             "Exact original AMX fixture is unavailable");
        std::vector<uint8_t> payload(item->payloadBytes);
        input.seekg(0);
        need(bool(input.read(reinterpret_cast<char*>(payload.data()),
                             std::streamsize(payload.size()))) &&
             catalog.findPayload(payload)==item && catalog.verifyPayload(*item,payload) &&
             catalog.verifyAudio(*item,payload),
             "Original AMX payload fails exact catalog identity");
        const auto originalCue=std::span<const uint8_t>(payload).subspan(
            cue->headerOffset,cue->endOffset-cue->headerOffset);
        const auto originalBlock=std::span<const uint8_t>(payload).subspan(
            block->offset,block->bytes);
        need(catalog.verifyCue(*cue,originalCue) &&
             catalog.verifyBlock(*block,originalBlock),
             "Original crash cue/block fails exact catalog hash");
        const auto blockHash=hex(block->hash);
        Simpsons::Audio::ResidentXmaProfile profile{};
        profile.header=cue->header;profile.headerOffset=cue->headerOffset;
        profile.blockBytes=block->bytes;profile.frames=block->frames;
        profile.channels=cue->channels;profile.codecSelector=block->selector;
        profile.codecRate=block->codecRate;profile.playbackRate=cue->playbackRate;
        profile.restoredFF=block->restoredFF;profile.blockHash=blockHash.c_str();
        const auto parsed=Simpsons::Audio::parseResidentXmaBlock(
            profile,originalBlock,cue->channels,cue->header);
        need(parsed.declaredFrames==cue->frames && parsed.layers.size()==1,
             "Original crash cue fails resident framing parser");
        const auto* loop=catalog.cue(*item,0);
        const auto* loopBlock=loop?catalog.findBlock(*loop,0):nullptr;
        need(loop && loop->loop && loop->loopStartSample==0 &&
             loop->headerOffset==2700 && loopBlock &&
             loopBlock->offset==2712 && loopBlock->frames==130256,
             "Original AMX one-block loop certificate changed");
        payload[127]^=1;
        need(!catalog.verifyPayload(*item,payload) && catalog.verifyAudio(*item,payload),
             "AMX metadata mutation altered the immutable audio certificate");
        payload[block->offset+12]^=1;
        need(!catalog.verifyPayload(*item,payload) &&
             !catalog.verifyAudio(*item,payload) &&
             !catalog.verifyBlock(*block,std::span<const uint8_t>(payload).subspan(
                 block->offset,block->bytes)),
             "Mutated AMX source passed exact hash validation");
        std::printf("AMX catalog PASS: %zu payloads, %zu cues, %zu blocks\n",
                    catalog.payloadCount(),catalog.cueCount(),catalog.blockCount());
        return 0;
    }catch(const std::exception& error) {
        std::fprintf(stderr,"AMX catalog FAIL: %s\n",error.what());return 1;
    }
}
