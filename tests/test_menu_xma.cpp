#include "audio/ea_xma_block.h"
#include "audio/all_menu_xma_certificates.h"
#include "audio/loc_music_xma_certificates.h"
#include "audio/mono_dialogue_xma_certificates.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

using namespace Simpsons::Audio;
namespace {
size_t checks=0;
void need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const XmaSourceError&){caught=true;}need(caught,"Invalid menu source accepted");}
uint32_t word(std::ifstream& file){uint32_t value=0;need(bool(file.read(reinterpret_cast<char*>(&value),4)),"Truncated menu fixture word");return value;}
std::string sha(std::span<const float> data){
    std::array<uint8_t,32> digest{};
    need(data.size_bytes()<ULONG_MAX&&BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,
        reinterpret_cast<PUCHAR>(const_cast<float*>(data.data())),ULONG(data.size_bytes()),digest.data(),ULONG(digest.size()))>=0,"Menu PCM hash failed");
    std::string result;for(auto b:digest){result+="0123456789abcdef"[b>>4];result+="0123456789abcdef"[b&15];}return result;
}
}
int main(int argc,char** argv){
    try{
        need(argc==2||(argc==3&&(std::string(argv[2])=="menu"||std::string(argv[2])=="loc"||std::string(argv[2])=="dialogue")),"Stream fixture and optional bank required");
        const bool loc=argc==3&&std::string(argv[2])=="loc";
        const bool dialogue=argc==3&&std::string(argv[2])=="dialogue";
        const uint32_t channels=dialogue?1:6,layers=(channels+1)/2;
        const std::span<const MenuXmaProfile> profiles=dialogue?std::span<const MenuXmaProfile>(monoDialogueProfiles):loc?std::span<const MenuXmaProfile>(locMusicXmaProfiles):std::span<const MenuXmaProfile>(menuXmaProfiles);
        std::ifstream file(argv[1],std::ios::binary);
        need(word(file)==profiles.size()&&profiles.size()==(dialogue?83u:loc?50u:37u),"Stream profile count changed");
        if(dialogue){
            constexpr std::array<uint8_t,8> reachedRabbit={0x03,0x00,0xBB,0x80,0x40,0x01,0x2B,0x80};
            const auto foundRabbit=std::find_if(profiles.begin(),profiles.end(),[&](const auto& profile){return profile.header==reachedRabbit;});
            need(foundRabbit!=profiles.end()&&foundRabbit->frames==76672&&foundRabbit->blocks.size()==16,
                 "Bridge replay chocolate-rabbit dialogue source missing or changed");
            need(foundRabbit->blocks.front().bytes==1393&&foundRabbit->blocks.front().frames==4736&&
                 std::string(foundRabbit->blocks.front().sha256)=="78c3233e0cb71d2c257a2e465348f6f09afa0ab70fc8a5a053bf1f346abec764",
                 "Bridge replay chocolate-rabbit dialogue differs from live crash evidence");
            constexpr std::array<uint8_t,8> reachedNelson={0x03,0x00,0xBB,0x80,0x40,0x00,0xC0,0xC4};
            const auto foundNelson=std::find_if(profiles.begin(),profiles.end(),[&](const auto& profile){return profile.header==reachedNelson;});
            need(foundNelson!=profiles.end()&&foundNelson->frames==49348&&foundNelson->blocks.size()==10,
                 "Bridge replay Nelson dialogue source missing or changed");
            need(foundNelson->blocks.front().bytes==1658&&foundNelson->blocks.front().frames==4736&&
                 std::string(foundNelson->blocks.front().sha256)=="832312dc8fc08e98f2bc1550bb1ce9d5144ca3c28d31d55defd81ffa5793cadc",
                 "Bridge replay Nelson dialogue differs from live crash evidence");
            constexpr std::array<uint8_t,8> reachedBridge={0x03,0x00,0xBB,0x80,0x40,0x01,0xF6,0x92};
            const auto foundBridge=std::find_if(profiles.begin(),profiles.end(),[&](const auto& profile){return profile.header==reachedBridge;});
            need(foundBridge!=profiles.end()&&foundBridge->frames==128658&&foundBridge->blocks.size()==26,
                 "Post-bridge dialogue source missing or changed");
            need(foundBridge->blocks.front().bytes==1573&&foundBridge->blocks.front().frames==4736&&
                 std::string(foundBridge->blocks.front().sha256)=="3673ef5c91003f3f8282a060e361b45c5fb730c93478a1712a8082936dc7be73",
                 "Post-bridge reader-owned dialogue block differs from live crash evidence");
            constexpr std::array<uint8_t,8> reached3e6c={0x03,0x00,0xBB,0x80,0x40,0x02,0x09,0x62};
            const auto found3e6c=std::find_if(profiles.begin(),profiles.end(),[&](const auto& profile){return profile.header==reached3e6c;});
            need(found3e6c!=profiles.end()&&found3e6c->frames==133474&&found3e6c->blocks.size()==27,
                 "Movement dialogue3e6c source missing or changed");
            need(std::string(found3e6c->blocks.front().sha256)=="45a6d411de76118f6ea4499bf09eb6beb1f58a2fb99547807766b4ae3d7eefe3",
                 "Movement dialogue3e6c owned block changed");
            constexpr std::array<uint8_t,8> reachedB33={0x03,0x00,0xBB,0x80,0x40,0x02,0xC0,0xD7};
            const auto foundB33=std::find_if(profiles.begin(),profiles.end(),[&](const auto& profile){return profile.header==reachedB33;});
            need(foundB33!=profiles.end()&&foundB33->frames==180439&&foundB33->blocks.size()==36,
                 "First-mission b33 dialogue source missing or changed");
            need(foundB33->blocks.front().bytes==1350&&foundB33->blocks.front().frames==4736&&
                 std::string(foundB33->blocks.front().sha256)=="18b69d3092aa57f67d92da6f96571d92322007b9ddc7c478ef6d9dc6550b88b0",
                 "First-mission b33 reader-owned dialogue block differs from live crash evidence");
            constexpr std::array<uint8_t,8> reachedFreeze={0x03,0x00,0xBB,0x80,0x40,0x02,0x30,0x29};
            const auto foundFreeze=std::find_if(profiles.begin(),profiles.end(),[&](const auto& profile){return profile.header==reachedFreeze;});
            need(foundFreeze!=profiles.end()&&foundFreeze->frames==143401&&foundFreeze->blocks.size()==29,
                 "Post-freeze Homer dialogue source missing or changed");
            need(foundFreeze->blocks.front().bytes==1787&&foundFreeze->blocks.front().frames==4736&&
                 std::string(foundFreeze->blocks.front().sha256)=="f96fd80cf55fc37afc61f469c88dc463b82f3ccfe74b7c3eedcdfeaaa75723ed",
                 "Post-freeze reader-owned dialogue block differs from live evidence");
            constexpr std::array<uint8_t,8> reachedB30={0x03,0x00,0xBB,0x80,0x40,0x02,0xB5,0xE4};
            const auto foundB30=std::find_if(profiles.begin(),profiles.end(),[&](const auto& profile){return profile.header==reachedB30;});
            need(foundB30!=profiles.end()&&foundB30->frames==177636&&foundB30->blocks.size()==35,
                 "Reached b30 dialogue source missing or changed");
            const auto& firstB30=foundB30->blocks.front();
            need(firstB30.bytes==1425&&firstB30.frames==4736&&
                 std::string(firstB30.sha256)=="54dbd898bdadb5e77a11efd94671c3792f8070d554067d677fbdaff1bb86570d",
                 "Reached b30 reader-owned first block differs from live evidence");
            constexpr std::array<uint8_t,8> reachedB32={0x03,0x00,0xBB,0x80,0x40,0x02,0x53,0x5A};
            const auto foundB32=std::find_if(profiles.begin(),profiles.end(),[&](const auto& profile){return profile.header==reachedB32;});
            need(foundB32!=profiles.end()&&foundB32->frames==152410&&foundB32->blocks.size()==30,
                 "Reached b32 dialogue source missing or changed");
            const auto& firstB32=foundB32->blocks.front();
            need(firstB32.bytes==1273&&firstB32.frames==4736&&
                 std::string(firstB32.sha256)=="a5fc8ea790438b51c858902f8c400e7b0de9c99c9e954c424d4035e1afc22621",
                 "Reached b32 reader-owned first block differs from live evidence");
            constexpr std::array<uint8_t,8> reached={0x03,0x00,0xBB,0x80,0x40,0x01,0x91,0x16};
            const auto found=std::find_if(profiles.begin(),profiles.end(),[&](const auto& profile){return profile.header==reached;});
            need(found!=profiles.end()&&found->frames==102678&&found->blocks.size()==21,
                 "Reached Homer dialogue source missing or changed");
            const auto& first=found->blocks.front();
            need(first.bytes==1668&&first.frames==4736&&
                 std::string(first.sha256)=="aaf94d73e2b552b1931a403ce7c0f9024ad51eca29059c0ee8c8b930642307bd",
                 "Reached reader-owned first block differs from live evidence");
            constexpr std::array<uint8_t,8> reached315c={0x03,0x00,0xBB,0x80,0x40,0x01,0x8E,0xC8};
            const auto found315c=std::find_if(profiles.begin(),profiles.end(),[&](const auto& profile){return profile.header==reached315c;});
            need(found315c!=profiles.end()&&found315c->frames==102088&&found315c->blocks.size()==21,
                 "Reached315c Homer dialogue source missing or changed");
            const auto& first315c=found315c->blocks.front();
            need(first315c.bytes==1704&&first315c.frames==4736&&
                 std::string(first315c.sha256)=="b141a42cee2048612ff2ef24d24a7bac89e23c40b2d7670cba084bcc89d09657",
                 "Reached315c reader-owned first block differs from live evidence");
            constexpr std::array<uint8_t,8> reached3e63={0x03,0x00,0xBB,0x80,0x40,0x01,0xAE,0x0F};
            const auto found3e63=std::find_if(profiles.begin(),profiles.end(),[&](const auto& profile){return profile.header==reached3e63;});
            need(found3e63!=profiles.end()&&found3e63->frames==110095&&found3e63->blocks.size()==22,
                 "Reached3e63 Homer dialogue source missing or changed");
            const auto& first3e63=found3e63->blocks.front();
            need(first3e63.bytes==1811&&first3e63.frames==4736&&
                 std::string(first3e63.sha256)=="20a3505b052b015a9674c6a077a9a78c93521206a3160e1d8c0a42782e3389d4",
                 "Reached3e63 reader-owned first block differs from live evidence");
            constexpr std::array<uint8_t,8> reached3e6d={0x03,0x00,0xBB,0x80,0x40,0x01,0x7C,0x45};
            const auto found3e6d=std::find_if(profiles.begin(),profiles.end(),[&](const auto& profile){return profile.header==reached3e6d;});
            need(found3e6d!=profiles.end()&&found3e6d->frames==97349&&found3e6d->blocks.size()==20,
                 "Reached3e6d Homer dialogue source missing or changed");
            const auto& first3e6d=found3e6d->blocks.front();
            need(first3e6d.bytes==1719&&first3e6d.frames==4736&&
                 std::string(first3e6d.sha256)=="7906e3d93c17012c1e3bea8ba3322a265e7d0b1dd37b7167dbf5e1af4d49ef7e",
                 "Reached3e6d reader-owned first block differs from live evidence");
            constexpr std::array<uint8_t,8> reachedAfe={0x03,0x00,0xBB,0x80,0x40,0x01,0x44,0x5B};
            const auto foundAfe=std::find_if(profiles.begin(),profiles.end(),[&](const auto& profile){return profile.header==reachedAfe;});
            need(foundAfe!=profiles.end()&&foundAfe->frames==83035&&foundAfe->blocks.size()==17,
                 "Bridge replay Homer afe dialogue source missing or changed");
            const auto& firstAfe=foundAfe->blocks.front();
            need(firstAfe.bytes==1584&&firstAfe.frames==4736&&
                 std::string(firstAfe.sha256)=="b0f708cba2edecfad57d7ac8ad90d137ad3e0184e06deae5e8ed07b61a6d9b4c",
                 "Bridge replay Homer afe first reader claim differs from live audio rejection");
            constexpr std::array<uint8_t,8> reachedAfb={0x03,0x00,0xBB,0x80,0x40,0x01,0x1C,0xCE};
            const auto foundAfb=std::find_if(profiles.begin(),profiles.end(),[&](const auto& profile){return profile.header==reachedAfb;});
            need(foundAfb!=profiles.end()&&foundAfb->frames==72910&&foundAfb->blocks.size()==15,
                 "Bridge replay Homer afb dialogue source missing or changed");
            const auto& firstAfb=foundAfb->blocks.front();
            need(firstAfb.bytes==1692&&firstAfb.frames==4736&&
                 std::string(firstAfb.sha256)=="d26f464061237f804439477011d18b12d060a8a68cca8381748f0c6bf0bcacbb",
                 "Bridge replay Homer afb first reader claim differs from live audio rejection");
            constexpr std::array<uint8_t,8> reached66eb={0x03,0x00,0xBB,0x80,0x40,0x01,0xCB,0x53};
            const auto found66eb=std::find_if(profiles.begin(),profiles.end(),[&](const auto& profile){return profile.header==reached66eb;});
            need(found66eb!=profiles.end()&&found66eb->frames==117587&&found66eb->blocks.size()==24,
                 "Bridge replay Chocolate rabbit 66eb dialogue source missing or changed");
            const auto& first66eb=found66eb->blocks.front();
            need(first66eb.bytes==1393&&first66eb.frames==4736&&
                 std::string(first66eb.sha256)=="54fff9a14581b8827bee64bad53bbfbc0450b3765787a2735be9d6ac17f0b2d2",
                 "Bridge replay Chocolate rabbit 66eb first reader claim differs from live audio rejection");
        }
        // Header lookup is the exact source key. Check uniqueness across both
        // qualified banks, including equal-duration tracks.
        std::vector<std::array<uint8_t,8>> headers;
        for(auto bank:{std::span<const MenuXmaProfile>(menuXmaProfiles),std::span<const MenuXmaProfile>(locMusicXmaProfiles),std::span<const MenuXmaProfile>(monoDialogueProfiles)})
            for(const auto& profile:bank){need(std::find(headers.begin(),headers.end(),profile.header)==headers.end(),"Ambiguous music header certificate");headers.push_back(profile.header);}
        size_t allBlocks=0;uint64_t allFrames=0;
        NativeXmaFactory factory;std::vector<XmaFormat> formats;for(uint32_t channel=0;channel<channels;channel+=2)formats.push_back({std::min(2u,channels-channel),48000,XmaVariant::Xma2});
        for(size_t stream=0;stream<profiles.size();++stream){
            const auto& profile=profiles[stream];std::array<uint8_t,8> header{};
            need(bool(file.read(reinterpret_cast<char*>(header.data()),8))&&header==profile.header,"Original menu header changed");
            const auto count=word(file);need(count==profile.blocks.size()&&qualifiedEaXmaFrames(header)==profile.frames,"Menu sequence/duration differs");
            need(qualifiedEaXmaChannels(header)==channels,"Stream channel profile differs");auto unknown=header;unknown[3]^=1;need(!qualifiedEaXmaFrames(unknown),"Unknown menu rate admitted");
            XmaSource::Limits limits;limits.maxQuotaFrames=256;limits.maxBufferedFramesPerLayer=131072;
            XmaSource source(factory,stream+1,formats,limits);std::array<std::vector<float>,3> pcm;
            for(auto& layer:pcm)layer.reserve(size_t(profile.frames)*2);
            uint32_t total=0;
            for(uint32_t i=0;i<count;++i){
                const auto length=word(file);need(length==profile.blocks[i].bytes&&length<=1024*1024,"Menu fixture block extent differs");
                std::vector<uint8_t> bytes(length);need(bool(file.read(reinterpret_cast<char*>(bytes.data()),length)),"Truncated menu block");
                auto parsed=parseEaXmaBlock(bytes,channels,i+1,header);
                if(!i){
                    rejects([&]{parseEaXmaBlock(bytes,2,1,header);});rejects([&]{parseEaXmaBlock(bytes,channels,0,header);});
                    rejects([&]{parseEaXmaBlock(bytes,channels,count+1,header);});rejects([&]{parseEaXmaBlock(bytes,channels,1,unknown);});
                }
                for(size_t offset:{size_t(0),size_t(4),size_t(12),bytes.size()-1}){
                    bytes[offset]^=1;rejects([&]{parseEaXmaBlock(bytes,channels,i+1,header);});bytes[offset]^=1;}
                XmaSource::Segment segment;segment.sequence=i+1;segment.slot=i%20;segment.declaredFrames=parsed.declaredFrames;
                segment.continuity=i?XmaSource::Continuity::ContinueContext:XmaSource::Continuity::FreshContext;
                segment.layers=std::move(parsed.layers);for(auto& layer:segment.layers)layer.skipFrames=i?0:384;
                const auto prepared=source.prepare(segment);need(prepared.status==XmaSource::PrepareStatus::Accepted,"Menu block rejected");
                for(uint32_t before=0;before<segment.declaredFrames;){
                    const auto quota=std::min(256u,segment.declaredFrames-before);const auto staged=source.stage(prepared.receipt,quota);
                    need(staged.status==XmaSource::StageStatus::Complete&&staged.ticket&&staged.ticket->channels()==channels,"Menu quota required unavailable future input");
                    for(uint32_t layer=0;layer<layers;++layer){
                        need(staged.ticket->rawStartFrame(layer)==total+384,"Menu raw origin changed");
                        for(uint32_t frame=0;frame<quota;++frame)for(uint32_t c=0;c<std::min(2u,channels-layer*2);++c){const auto sample=staged.ticket->plane(layer*2+c)[frame];need(std::isfinite(sample),"Nonfinite menu PCM");pcm[layer].push_back(sample);}
                    }
                    source.validateCommit(staged.ticket);source.commit(staged.ticket);before+=quota;total+=quota;
                }
                source.retire(prepared.receipt);
            }
            need(total==profile.frames&&source.snapshot().sources.empty(),"Menu declared extent/retirement changed");
            for(uint32_t layer=0;layer<layers;++layer)need(sha(pcm[layer])==profile.trimmedPcmHash[layer],"Menu PCM differs from independent qualification");
            allBlocks+=count;allFrames+=total;
            std::printf("Verified menu stream%zu: %u frames, %u blocks, exact full-quota PCM\n",stream,total,count);
        }
        need(file.peek()==std::char_traits<char>::eof()&&allFrames==(dialogue?8571129u:loc?20017467u:33766564u),"Music fixture total/EOF changed");
        need(allBlocks==(dialogue?1724u:loc?3937u:6621u),"Music fixture block count changed");
        std::printf("PASS %s music XMA: %zu checks; %zu streams,%zu blocks,%llu frames,%u channels,no raw EOF\n",
            dialogue?"dialogue":loc?"loc":"menu",checks,profiles.size(),allBlocks,static_cast<unsigned long long>(allFrames),channels);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL menu XMA: %s\n",e.what());return 1;}
}
