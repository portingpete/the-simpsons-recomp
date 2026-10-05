#include "audio/resident_xma.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <source_location>
#include <string>

using namespace Simpsons::Audio;
namespace {
size_t checks=0;
void need(bool value,const char* why) {++checks;if(!value) throw std::runtime_error(why);}
template<class F> void rejects(F&& f,std::source_location where=std::source_location::current()) {
    bool caught=false;try{f();}catch(const std::exception&){caught=true;}
    if(!caught)std::fprintf(stderr,"Resident XMA: accepted invalid operation at test line %u\n",unsigned(where.line()));
    need(caught,"Invalid resident operation accepted");
}
std::string sha(std::span<const uint8_t> data) {
    std::array<uint8_t,32> hash{};
    need(data.size()<=ULONG_MAX && BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(data.data()),
         ULONG(data.size()),hash.data(),ULONG(hash.size()))>=0,"Resident fixture hash failed");
    std::string result;for(auto b:hash){result+="0123456789abcdef"[b>>4];result+="0123456789abcdef"[b&15];}return result;
}
std::vector<float> oracle(const EaXmaBlock& block,const ResidentXmaProfile& profile=residentXmaProfiles[0]) {
    NativeXmaCodec codec({profile.channels,profile.codecRate,XmaVariant::Xma2});
    std::vector<float> pcm;
    const std::array<uint32_t,6> widths={1,127,511,3,513,19};size_t position=0;
    auto drain=[&] {
        size_t total=0;
        for(size_t attempt=0;attempt<100000;++attempt) {
            std::vector<float> output(size_t(widths[position++%widths.size()])*profile.channels);
            const auto frames=codec.read(output);if(!frames) return total;
            pcm.insert(pcm.end(),output.begin(),output.begin()+size_t(frames)*profile.channels);total+=frames;
            need(pcm.size()<=size_t(profile.rawFrames)*profile.channels,"Resident raw frame bound exceeded");
        }
        throw std::runtime_error("Resident direct-codec drain failed to finish");
    };
    for(const auto& packet:block.layers[0].packets) {
        for(size_t tries=0;;++tries) {
            need(tries<1024,"Resident packet admission made no progress");
            if(codec.send(packet)==PacketResult::Accepted) break;
            need(drain()>0,"Resident raw backpressure produced no frames");
        }
        drain();
    }
    need(drain()==0 && pcm.size()==size_t(profile.rawFrames)*profile.channels,"Resident raw frame count changed");
    need(sha({reinterpret_cast<const uint8_t*>(pcm.data()),pcm.size()*sizeof(float)})==
         profile.rawHash,"Resident raw PCM qualification hash changed");
    return pcm;
}
void additionalProfile(std::span<const uint8_t> bank,const ResidentXmaProfile& profile,const ResidentBankProfile* group=nullptr,uint32_t blockOffset=8) {
    const auto lookup=[&](std::span<const uint8_t> header){return group?residentXmaProfile(*group,profile.headerOffset,header):residentXmaProfile(header);};
    const auto* loop=group?residentLoopProfile(*group,profile.headerOffset):nullptr;
    need(lookup(profile.header)==(loop?&loop->blocks[0]:&profile)&&std::equal(profile.header.begin(),profile.header.end(),bank.begin()+profile.headerOffset),"Resident header lookup/offset differs from original bank");
    const auto bytes=bank.subspan(profile.headerOffset+blockOffset,profile.blockBytes);
    const auto block=parseResidentXmaBlock(profile,bytes,profile.channels,profile.header);
    const auto packetBytes=profile.blockBytes-12+profile.restoredFF;
    need(block.layers.size()==1&&block.layers[0].packets.size()==packetBytes/2048&&block.declaredFrames==profile.frames,"Mono resident framing changed");
    const auto* prepared=reinterpret_cast<const uint8_t*>(block.layers[0].packets.data());
    need(!std::memcmp(prepared,bytes.data()+12,bytes.size()-12)&&
         std::all_of(prepared+bytes.size()-12,prepared+packetBytes,[](uint8_t b){return b==0xFF;}),"Resident payload/restoration differs from certificate");
    rejects([&]{parseResidentXmaBlock(profile,bytes,profile.channels==1?2:1,profile.header);});rejects([&]{parseResidentXmaBlock(profile,bytes,profile.channels,residentXmaHeader);});
    for(size_t i=0;i<8;++i){auto header=profile.header;header[i]^=1;need(!lookup(header),"Unknown resident profile accepted");rejects([&]{parseResidentXmaBlock(profile,bytes,profile.channels,header);});}
    for(size_t offset:{size_t(0),size_t(4),size_t(8),size_t(12),bytes.size()-1}){
        std::vector<uint8_t> changed(bytes.begin(),bytes.end());changed[offset]^=1;rejects([&]{parseResidentXmaBlock(profile,changed,profile.channels,profile.header);});}
    const auto raw=oracle(block,profile);NativeXmaFactory factory;
    const std::array<XmaFormat,1> formats={{{profile.channels,profile.codecRate,XmaVariant::Xma2}}};
    XmaSource::Limits limits;limits.maxBufferedFramesPerLayer=profile.rawFrames;limits.maxDeclaredFrames=profile.frames;
    limits.maxCompressedBytes=packetBytes;limits.maxQuotaFrames=256;XmaSource source(factory,profile.headerOffset,formats,limits);
    XmaSource::Segment segment;segment.sequence=1;segment.slot=0;segment.declaredFrames=profile.frames;
    segment.continuity=XmaSource::Continuity::FreshContext;segment.layers=block.layers;segment.layers[0].skipFrames=384;
    const auto accepted=source.prepare(segment);need(accepted.status==XmaSource::PrepareStatus::Accepted,"Start source not admitted");
    for(uint32_t before=0;before<profile.frames;){
        const auto count=std::min(256u,profile.frames-before);const auto staged=source.stage(accepted.receipt,count);
        need(staged.status==XmaSource::StageStatus::Complete&&staged.ticket&&staged.ticket->channels()==profile.channels&&
             staged.ticket->rawStartFrame(0)==before+384,"Mono Start quota/skip changed");
        for(uint32_t channel=0;channel<profile.channels;++channel)for(uint32_t i=0;i<count;++i)need(std::bit_cast<uint32_t>(staged.ticket->plane(channel)[i])==std::bit_cast<uint32_t>(raw[(before+384+i)*profile.channels+channel]),"Mono Start PCM differs from qualified independent schedule");
        source.validateCommit(staged.ticket);source.commit(staged.ticket);before+=count;
    }
    source.retire(accepted.receipt);const auto snapshot=source.snapshot();
    need(snapshot.sources.empty()&&snapshot.committedFrames==profile.frames&&snapshot.layers[0].bufferedFrames==profile.rawFrames-profile.frames-384&&snapshot.layers[0].acceptedPackets==packetBytes/2048,"Mono resident final accounting changed");
}
}
int main(int argc,char** argv) {
    try {
        const bool loc=argc==3&&std::string(argv[2])=="--loc";
        const bool characters=argc==3&&std::string(argv[2])=="--characters";
        const bool homer=argc==3&&std::string(argv[2])=="--homer";
        const bool homerVariant=argc==3&&std::string(argv[2])=="--homer-variant";
        const bool locGlobal=argc==3&&std::string(argv[2])=="--loc-global";
        need(argc==2||(argc==3&&(std::string(argv[2])=="--story"||loc||characters||homer||homerVariant||locGlobal)),"Resident bank fixture path required");
        const auto* bankProfile=residentBankProfile(locGlobal?2948197:homer?2030922:homerVariant?1776651:characters?1212431:loc?2551861:argc==3?4293314:residentBankBytes);
        need(bankProfile,"Resident bank profile missing");
        std::ifstream file(argv[1],std::ios::binary|std::ios::ate);
        need(bool(file) && file.tellg()==std::streamoff(bankProfile->bytes),"Missing resident SBK fixture");
        std::vector<uint8_t> bank(bankProfile->bytes);file.seekg(0);
        need(bool(file.read(reinterpret_cast<char*>(bank.data()),std::streamsize(bank.size()))),"Resident bank read failed");
        validateResidentXmaBank(*bankProfile,bank);
        if(locGlobal) {
            need(bankProfile->profiles.size()==479,"loc_global qualified source inventory changed");
            const std::array<uint8_t,8> header={3,0,0xBB,0x80,0,0,0x16,0x48};
            const auto* selected=residentXmaProfile(*bankProfile,0x1CDA89,header);
            need(selected&&selected->channels==1&&selected->playbackRate==48000&&
                 selected->codecRate==48000&&selected->codecSelector==3&&
                 selected->frames==5704&&selected->rawFrames==6144&&selected->blockBytes==1561&&selected->restoredFF==499&&
                 std::string(selected->rawHash)=="79f52d7a244c673ea999bf1bf303221b653d4a0055cbb78d0b57230f11e75210"&&
                 std::string(selected->blockHash)=="02afc69859b1855cfd62ff1750d94158f4d658775de0dc2e2a98c2bf95c41b0a",
                 "Reached loc_global source certificate changed");
            need(bankProfile->metadataBytes==0x17C30&&bankProfile->audioOffset==0x17C80&&
                 bankProfile->audioBytes==0x2B7FE5&&bankProfile->loops.empty(),"loc_global original split/loop inventory changed");
            need(!residentXmaProfile(*bankProfile,0x1CDA88,header)&&
                 !residentXmaProfile(*bankProfile,0x1CDA8A,header)&&
                 !residentXmaProfile(*bankProfile,0x1CDA89-0x17C80,header),"loc_global bank-relative header identity lost");
            for(const auto& profile:bankProfile->profiles)additionalProfile(bank,profile,bankProfile);
            for(size_t offset:{size_t(8),size_t(0x17C80),size_t(0x1CDA89),bank.size()-1}) {
                auto changed=bank;changed[offset]^=1;rejects([&]{validateResidentXmaBank(*bankProfile,changed);});
            }
            rejects([&]{validateResidentXmaBank(*bankProfile,std::span<const uint8_t>(bank).first(bank.size()-1));});
            std::printf("PASS %zu loc_global checks; %zu sources, exact bank/record identity, real PCM quotas and retirement\n",
                        checks,bankProfile->profiles.size());return 0;
        }
        if(homer||homerVariant) {
            need(bankProfile->profiles.size()==(homer?235u:201u)&&bankProfile->loops.empty(),
                 "Homer qualified source inventory changed");
            const uint32_t reached=homer?0x18D2F9:0x105C9B;
            const uint32_t other=homer?0x105C9B:0x18D2F9;
            const std::array<uint8_t,8> header={3,0,0xBB,0x80,0,0,0xC8,0};
            const auto* selected=residentXmaProfile(*bankProfile,reached,header);
            need(selected&&selected->channels==1&&selected->playbackRate==48000&&
                 selected->codecRate==48000&&selected->codecSelector==3&&
                 selected->frames==51200&&selected->rawFrames==51712&&selected->blockBytes==12300&&!selected->restoredFF&&
                 std::string(selected->rawHash)=="79065b78b8a97fa8056b4899c4c5a3ce62bd28f0b766c5e93b0a9f4ce2fcfada"&&
                 std::string(selected->blockHash)=="6967bc62eb8049b017206d814ffaf48fda67cff1bbed5a5260ff22ae3f450a69",
                 "Reached Homer source certificate changed");
            need(!residentLoopProfile(*bankProfile,reached)&&
                 !residentXmaProfile(*bankProfile,reached+1,header)&&
                 !residentXmaProfile(*bankProfile,other,header),"Homer bank-relative source identity lost");
            need(bankProfile->metadataBytes==(homer?0xB6B0u:0x9A90u)&&
                 bankProfile->audioOffset==(homer?0xB700u:0x9AC0u),"Homer original split changed");
            for(const auto& profile:bankProfile->profiles)additionalProfile(bank,profile,bankProfile);
            for(const auto& loop:bankProfile->loops) {
                uint32_t offset=12,total=0;
                need(!loop.blocks.empty()&&loop.blocks.size()==(loop.loopStart?2u:1u),"Homer loop block count changed");
                for(const auto& block:loop.blocks) {
                    additionalProfile(bank,block,bankProfile,offset);
                    offset+=block.blockBytes;total+=block.frames;
                }
                need(total==loop.totalFrames&&(!loop.loopStart||loop.blocks.front().frames==loop.loopStart),
                     "Homer loop accounting changed");
                additionalProfile(bank,loop.blocks.back(),bankProfile,offset-loop.blocks.back().blockBytes);
            }
            for(size_t offset:{size_t(8),size_t(bankProfile->audioOffset),size_t(reached),bank.size()-1}) {
                auto changed=bank;changed[offset]^=1;rejects([&]{validateResidentXmaBank(*bankProfile,changed);});
            }
            rejects([&]{validateResidentXmaBank(*bankProfile,std::span<const uint8_t>(bank).first(bank.size()-1));});
            std::printf("PASS %zu Homer checks; %zu ordinary/%zu loop sources, exact bank/record identity, real PCM quotas and retirement\n",
                        checks,bankProfile->profiles.size(),bankProfile->loops.size());return 0;
        }
        if(characters) {
            need(bankProfile->profiles.size()==66&&bankProfile->loops.size()==3,"Characters certificate count changed");
            for(const auto& profile:bankProfile->profiles)additionalProfile(bank,profile,bankProfile);
            for(const auto& loop:bankProfile->loops) {
                need(loop.blocks.size()==1&&loop.loopStart==0&&loop.blocks[0].frames==loop.totalFrames,
                     "Characters complete loop boundary changed");
                additionalProfile(bank,loop.blocks[0],bankProfile,12);
                additionalProfile(bank,loop.blocks[0],bankProfile,12);
            }
            const auto header=std::span<const uint8_t>(bank).subspan(1061146,8);
            need(residentXmaProfile(*bankProfile,1061146,header),"Reached characters source is missing");
            need(!residentXmaProfile(*bankProfile,1061147,header),"Wrong characters source offset accepted");
            auto changed=bank;changed[32]^=1;rejects([&]{validateResidentXmaBank(*bankProfile,changed);});
            std::printf("PASS %zu characters checks;66 ordinary and3 complete-loop sources, source identity, fresh quotas, hashes and retirement\n",checks);return 0;
        }
        if(loc) {
            need(bankProfile->profiles.size()==120&&bankProfile->loops.size()==12,"Loc certificate count changed");
            for(const auto& profile:bankProfile->profiles)additionalProfile(bank,profile,bankProfile);
            for(const auto& loop:bankProfile->loops) {
                uint32_t offset=12,total=0;
                need(loop.blocks.size()==2&&loop.blocks[0].frames==loop.loopStart,"Loc intro boundary changed");
                for(const auto& block:loop.blocks) {
                    additionalProfile(bank,block,bankProfile,offset);
                    offset+=block.blockBytes;total+=block.frames;
                }
                need(total==loop.totalFrames,"Loc loop complete frame count changed");
                // A second loop pass must decode from a fresh context, with its
                // own384-frame skip and exactly the same qualified full quota.
                additionalProfile(bank,loop.blocks[1],bankProfile,12+loop.blocks[0].blockBytes);
            }
            need(residentXmaProfile(*bankProfile,1619990,std::span<const uint8_t>(bank).subspan(1619990,8)),"Repaired packet-boundary loop is missing");
            auto changed=bank;changed[32]^=1;rejects([&]{validateResidentXmaBank(*bankProfile,changed);});
            std::printf("PASS %zu loc checks;120 ordinary and12 loop sources, fresh intro/loop quotas, hashes and retirement\n",checks);return 0;
        }
        if(argc==3){
            need(bankProfile->profiles.size()==429,"Story source certificate count changed");
            for(const auto& profile:bankProfile->profiles)additionalProfile(bank,profile,bankProfile);
            auto changed=bank;changed[32]^=1;rejects([&]{validateResidentXmaBank(*bankProfile,changed);});
            need(!residentXmaProfile(*bankProfile,2491362,std::array<uint8_t,8>{3,0,0xBB,0x80,0,0,0x18,0x9A}),"Wrong sound offset was admitted");
            for(uint32_t offset:{2768867u,3858558u})
                need(residentXmaProfile(*bankProfile,offset,std::span<const uint8_t>(bank).subspan(offset,8)),"Repaired packet-boundary story sound is missing");
            std::printf("PASS %zu story resident checks; bank/offset identity, all source quotas, PCM hashes and retirement\n",checks);return 0;
        }
        validateResidentXmaBank(bank);
        need(residentXmaProfiles.size()==30&&residentXmaProfiles[1].headerOffset==51787,"Qualified frontend profile set changed");
        for(size_t i=1;i<residentXmaProfiles.size();++i)additionalProfile(bank,residentXmaProfiles[i]);
        const auto bytes=std::span<const uint8_t>(bank).subspan(residentHeaderOffset+8,residentBlockBytes);
        const auto block=parseResidentXmaBlock(bytes,2,residentXmaHeader);
        need(block.declaredFrames==residentFrames && block.layers.size()==1 && block.layers[0].packets.size()==9,
             "Resident original layer framing changed");
        rejects([&]{parseResidentXmaBlock(bytes,1,residentXmaHeader);});
        rejects([&]{parseResidentXmaBlock(bytes.first(bytes.size()-1),2,residentXmaHeader);});
        auto header=residentXmaHeader;header[1]^=1;
        rejects([&]{parseResidentXmaBlock(bytes,2,header);});
        for(size_t offset:{size_t(0),size_t(4),size_t(8),size_t(12),size_t(residentBlockBytes-1)}) {
            std::vector<uint8_t> invalid(bytes.begin(),bytes.end());invalid[offset]^=1;
            rejects([&]{parseResidentXmaBlock(invalid,2,residentXmaHeader);});
        }
        auto changedBank=bank;changedBank[32]^=1;rejects([&]{validateResidentXmaBank(changedBank);});
        const auto raw=oracle(block);
        NativeXmaFactory factory;
        const std::array<XmaFormat,1> formats={{{2,residentCodecRate,XmaVariant::Xma2}}};
        XmaSource::Limits limits;limits.maxBufferedFramesPerLayer=residentRawFrames;
        limits.maxDeclaredFrames=residentFrames;limits.maxCompressedBytes=18432;limits.maxQuotaFrames=256;
        auto excessive=limits;excessive.maxBufferedFramesPerLayer=4194305; // One past XmaSource's per-layer cap.
        rejects([&]{XmaSource invalid(factory,99,formats,excessive);});
        XmaSource::Segment segment;segment.sequence=1;segment.slot=0;segment.declaredFrames=residentFrames;
        segment.continuity=XmaSource::Continuity::FreshContext;segment.layers=block.layers;segment.layers[0].skipFrames=384;
        auto small=limits;small.maxBufferedFramesPerLayer=262144;
        XmaSource insufficient(factory,100,formats,small);
        rejects([&]{insufficient.prepare(segment);});
        need(insufficient.snapshot().failure.stage!=XmaSource::FailureStage::None,"Resident overflow did not remain terminal");
        XmaSource source(factory,101,formats,limits);
        const auto accepted=source.prepare(segment);
        need(accepted.status==XmaSource::PrepareStatus::Accepted,"Resident full source admission failed");
        rejects([&]{source.retire(accepted.receipt);});
        const std::array<uint32_t,6> widths={1,127,256,3,255,19};size_t quota=0;
        for(uint32_t before=0;before<residentFrames;) {
            const uint32_t count=std::min(widths[quota++%widths.size()],residentFrames-before);
            const auto staged=source.stage(accepted.receipt,count);
            need(staged.status==XmaSource::StageStatus::Complete && staged.ticket &&
                 staged.ticket->rawStartFrame(0)==uint64_t(before)+384 && staged.ticket->frames()==count &&
                 staged.ticket->skippedFrames(0)==(before?0u:384u),"Resident complete quota/initial skip changed");
            for(uint32_t channel=0;channel<2;++channel)
                for(uint32_t frame=0;frame<count;++frame) {
                    const float actual=staged.ticket->plane(channel)[frame];
                    need(std::isfinite(actual) && std::bit_cast<uint32_t>(actual)==
                         std::bit_cast<uint32_t>(raw[(size_t(before)+384+frame)*2+channel]),"Resident quota PCM differs from pinned raw schedule");
                }
            source.validateCommit(staged.ticket);source.commit(staged.ticket);before+=count;
        }
        const auto snapshot=source.snapshot();
        need(source.query(accepted.receipt).status==XmaSource::SourceStatus::Delivered &&
             snapshot.committedFrames==residentFrames && snapshot.copiedBytes==18432 &&
             snapshot.layers[0].bufferedFrames==242 && snapshot.layers[0].rawHeadFrame==residentFrames+384 &&
             snapshot.layers[0].acceptedPackets==9,"Resident full quota/tail accounting changed");
        segment.sequence=2;
        rejects([&]{source.prepare(segment);});
        source.retire(accepted.receipt);
        const auto second=source.prepare(segment);
        need(second.status==XmaSource::PrepareStatus::Accepted && source.snapshot().layers[0].discardedOnFresh==242,
             "Explicit fresh resident context did not discard only the retained tail");
        source.close();need(source.query(second.receipt).status==XmaSource::SourceStatus::Cancelled,"Resident early close lost its receipt");
        source.retire(second.receipt);need(source.snapshot().sources.empty(),"Resident early retirement retained a source");
        std::printf("Resident XMA PASS: %zu checks, all30 exact frontend sources, 384 initial skip, verified retained tails; finite exact PCM, no EOF\n",checks);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"Resident XMA FAIL: %s\n",error.what());return 1;}
}
