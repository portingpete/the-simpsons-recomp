#include "audio/resident_audio_catalog.h"
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
    need(hex.size()==64,"Invalid fixture digest length");
    std::array<uint8_t,32> result{};
    auto digit=[](char c)->uint8_t {
        if(c>='0'&&c<='9')return uint8_t(c-'0');
        if(c>='a'&&c<='f')return uint8_t(c-'a'+10);
        throw std::runtime_error("Invalid fixture digest digit");
    };
    for(size_t i=0;i<32;++i)result[i]=uint8_t((digit(hex[i*2])<<4)|digit(hex[i*2+1]));
    return result;
}
std::string hex(std::span<const uint8_t> bytes) {
    std::string out;out.reserve(bytes.size()*2);
    for(const auto byte:bytes) {
        out.push_back("0123456789abcdef"[byte>>4]);
        out.push_back("0123456789abcdef"[byte&15]);
    }
    return out;
}
}
int main(int argc,char** argv) {
    try {
        need(argc==3,"Provide resident binary catalog and extracted SBK paths");
        using Simpsons::Audio::ResidentAudioCatalog;
        auto catalog=ResidentAudioCatalog::load(argv[1]);
        need(catalog.bankCount()==156 && catalog.cueCount()==45382 &&
             catalog.blockCount()==45498,"Resident catalog corpus counts changed");
        const auto* bank=catalog.findBank(hash("702aebda44b3a0f36866b35b89d00c0fd7d5cbfa8dfd7409df4cc562122e71eb"),2551861);
        need(bank && bank->path=="loc/loc.str" && bank->name=="loc.sbk" &&
             bank->entryIndex==2 && bank->audioOffset==30528 &&
             bank->audioBytes==2521333 && bank->cueCount==132,
             "Known loc resident bank identity/split changed");
        need(catalog.cue(*bank,0) && catalog.cue(*bank,0)->headerOffset==32648 &&
             !catalog.cue(*bank,132),"Resident bank cue ordinal lookup changed");
        const std::array<uint8_t,8> header={0x03,0x00,0x5D,0xC0,0x00,0x00,0x3B,0x40};
        const auto* cue=catalog.findCue(*bank,32648,header);
        need(cue && cue->channels==1 && cue->playbackRate==24000 &&
             cue->frames==15168 && !cue->loop && cue->blockCount==1,
             "Known loc resident cue changed");
        const auto* block=catalog.findBlock(*cue,0);
        need(block && block->offset==32656 && block->bytes==6156 &&
             block->frames==15168 && block->selector==0 &&
             block->codecRate==24000 && block->payloadBytes==6144 &&
             block->restoredFF==0 &&
             block->hash==hash("8b5ba2ebce58af42d9a778ebe0b58bfe213b03d6e74020b684859b459eede426"),
             "Known loc resident block certificate changed");
        need(!catalog.findBlock(*cue,1),"Out-of-range resident block was admitted");
        auto wrong=header;wrong[7]^=1;
        need(!catalog.findCue(*bank,32648,wrong) &&
             !catalog.findCue(*bank,32649,header),
             "Unknown resident cue header/offset was admitted");
        std::vector<uint8_t> altered(block->bytes,0);
        need(!catalog.verifyBlock(*block,altered),
             "Altered resident block passed exact byte verification");
        const auto* story=catalog.findBank(hash("0392a01388db435d73cd81a565c9bc94273a13c53245cb30d596283594bdeca1"),4293314);
        need(story && story->cueCount==430,"Flagged story cue missing from complete bank");
        const std::array<uint8_t,8> loopHeader={0x03,0x00,0xBB,0x80,0x20,0x01,0x9D,0x29};
        const auto* loop=catalog.findCue(*story,3694586,loopHeader);
        const auto* intro=loop?catalog.findBlock(*loop,0):nullptr;
        const auto* body=loop?catalog.findBlock(*loop,1):nullptr;
        need(loop && loop->loop && loop->loopStartSample==1 && loop->blockCount==2 &&
             intro && intro->frames==1 && body && body->frames==105768,
             "Two-block resident loop certificate changed");
        const auto* global=catalog.findBank(hash("145197be5688def500325f86d2c4896ac3ddbf751be95c388393e22bbec2e6df"),2948197);
        const std::array<uint8_t,8> lowHeader={0x03,0x00,0x8C,0xA0,0x00,0x00,0x0B,0x70};
        const auto* low=global?catalog.findCue(*global,1830094,lowHeader):nullptr;
        need(global && global->cueCount==485 && low && low->playbackRate==36000,
             "36-kHz resident playback cue missing");
        const auto* bargain=catalog.findBank(hash("6a1430b0594ddb67f762b8e82963856250b1681d6c3dc246c5d4efc0f2867f77"),1330580);
        need(bargain && bargain->path=="bargainbin/bargainbin.str" &&
             bargain->name=="bargainbin.sbk" && bargain->cueCount==33,
             "Previously uncertified Bargain Bin bank missing");
        std::ifstream file(argv[2],std::ios::binary|std::ios::ate);
        need(bool(file) && file.tellg()==std::streamoff(bargain->payloadBytes),
             "Exact Bargain Bin SBK fixture missing");
        std::vector<uint8_t> payload(size_t(bargain->payloadBytes));
        file.seekg(0);
        need(bool(file.read(reinterpret_cast<char*>(payload.data()),std::streamsize(payload.size()))) &&
             catalog.verifyBank(*bargain,payload),
             "Extracted Bargain Bin SBK differs from catalog identity");
        const auto* fresh=catalog.cue(*bargain,0);
        const auto* freshBlock=fresh?catalog.findBlock(*fresh,0):nullptr;
        need(fresh && fresh->headerOffset==8920 && fresh->frames==15897 &&
             fresh->playbackRate==48000 && !fresh->loop && freshBlock &&
             freshBlock->offset==8928 && freshBlock->bytes==4108 &&
             freshBlock->frames==15897 && freshBlock->selector==3 &&
             freshBlock->codecRate==48000 && freshBlock->restoredFF==0 &&
             freshBlock->hash==hash("b88306e0b2ac239d410b4730881f6f5d6a056a9c944a4b8eb6018910c0b29f82"),
             "Previously uncertified Bargain Bin cue/block changed");
        const auto original=std::span<const uint8_t>(payload).subspan(
            size_t(freshBlock->offset),freshBlock->bytes);
        need(catalog.verifyBlock(*freshBlock,original),
             "Exact previously uncertified block failed catalog hash");
        const auto blockHash=hex(freshBlock->hash);
        Simpsons::Audio::ResidentXmaProfile profile{};
        profile.header=fresh->header;profile.headerOffset=uint32_t(fresh->headerOffset);
        profile.blockBytes=freshBlock->bytes;profile.frames=freshBlock->frames;
        profile.channels=fresh->channels;profile.codecSelector=freshBlock->selector;
        profile.codecRate=freshBlock->codecRate;profile.playbackRate=fresh->playbackRate;
        profile.restoredFF=freshBlock->restoredFF;profile.blockHash=blockHash.c_str();
        const auto parsed=Simpsons::Audio::parseResidentXmaBlock(
            profile,original,fresh->channels,fresh->header);
        need(parsed.declaredFrames==fresh->frames && parsed.layers.size()==1 &&
             parsed.layers[0].packets.size()==2,
             "Previously uncertified cue cannot pass exact resident framing parser");
        std::printf("Resident catalog PASS: %zu banks, %zu cues, %zu blocks\n",
                    catalog.bankCount(),catalog.cueCount(),catalog.blockCount());
        return 0;
    }catch(const std::exception& error) {
        std::fprintf(stderr,"Resident catalog FAIL: %s\n",error.what());return 1;
    }
}
