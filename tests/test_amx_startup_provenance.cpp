#include "runtime/engine_audio.h"
#include "audio/amx_audio_catalog.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <span>

namespace {
using namespace Simpsons;
void need(bool value,const char* why) {if(!value) throw Failure(why);}
struct Observed{};
constexpr uint32_t decodedResourceOffset=0xD0; // Original STR resource placement.
uint32_t be32(const uint8_t* bytes) {
    return uint32_t(bytes[0])<<24|uint32_t(bytes[1])<<16|
           uint32_t(bytes[2])<<8|bytes[3];
}

void verifyOriginalAmx(Runtime& rt,PPCContext& ctx) {
    need(rt.engineAudio && rt.gameRoot.is_absolute(),
         "Actual AMX startup lacks native audio owner or original game root");
    auto& owners=*rt.engineAudio;
    // The named SBK return follows the AMX and SBK resources in the same
    // original STR load. Its verified source address bounds this search.
    const auto sbk=owners.residentBank(ctx.r3.u32);
    const auto catalog=Audio::AmxAudioCatalog::load(
        rt.gameRoot.parent_path()/"analysis"/"amx_audio_catalog.bin");
    const auto* expected=catalog.findPayload(
        std::array<uint8_t,32>{0x52,0x43,0xA5,0x1F,0x37,0x37,0x6F,0x5B,
            0x1B,0xB8,0x96,0x9D,0xB7,0x61,0xB2,0xA6,
            0x09,0x82,0xA5,0x5D,0x79,0x2F,0xB1,0x45,
            0xA4,0x39,0xAC,0x46,0x76,0x3B,0x94,0x12},379448);
    need(expected && expected->name=="char_poles_and_ladders.amx" &&
         expected->cueCount==16 && expected->payloadBytes==379448,
         "Original AMX catalog payload identity changed");
    need(sbk.source>expected->payloadBytes+0x10000,
         "Original SBK source is too early to bound the preceding AMX resource");
    const uint32_t first=(sbk.source-expected->payloadBytes-0x10000+15)&~15u;
    const uint32_t last=sbk.source-expected->payloadBytes;
    uint32_t payloadBase=0;
    uint64_t generation=0;
    for(uint32_t at=first;at<=last;at+=16) {
        const uint8_t* header=nullptr;
        try {header=rt.pointer(at,16,false);}
        catch(const Failure&) {continue;}
        const uint32_t body=expected->payloadBytes-expected->metadataBytes-64;
        if(be32(header)!=9 || be32(header+4)!=body ||
           be32(header+8)!=expected->metadataBytes || be32(header+12)!=body) continue;
        // The original allocator is observed independently of the catalog
        // hash, before accepting any live audio bytes.
        const auto observed=owners.allocationGeneration(
            at-decodedResourceOffset,expected->payloadBytes+decodedResourceOffset);
        need(observed,"Original AMX envelope lacks an observed allocator generation");
        const uint8_t* bytes=nullptr;
        try {bytes=rt.pointer(at,expected->payloadBytes,false);}
        catch(const Failure&) {continue;}
        if(!catalog.verifyAudio(*expected,
                                std::span<const uint8_t>(bytes,expected->payloadBytes)))
            continue;
        need(!payloadBase,"Original AMX payload matched two guest addresses");
        payloadBase=at;generation=observed;
    }
    need(payloadBase && payloadBase>=decodedResourceOffset,
         "Original AMX payload was absent from the startup guest resource allocation");
    const uint32_t allocation=payloadBase-decodedResourceOffset;
    need(owners.allocationGeneration(allocation,expected->payloadBytes+decodedResourceOffset)==generation,
         "Original AMX allocation generation changed before cue verification");
    const auto* envelope=rt.pointer(payloadBase,16,false);
    need(envelope[3]==9 &&
         !std::memcmp(envelope+4,envelope+12,4),
         "Original guest AMX envelope differs from its resource profile");
    const std::array<uint8_t,8> header={0x03,0x00,0xBB,0x80,0x00,0x00,0x0B,0x45};
    const auto* cue=catalog.findCue(*expected,339952,header);
    const auto* block=cue?catalog.findBlock(*cue,0):nullptr;
    need(cue && cue->ordinal==11 && cue->frames==2885 && block &&
         block->offset==339960 && block->bytes==1267 &&
         !std::memcmp(rt.pointer(payloadBase+cue->headerOffset,8,false),header.data(),8) &&
         catalog.verifyCue(*cue,std::span<const uint8_t>(
             rt.pointer(payloadBase+cue->headerOffset,cue->endOffset-cue->headerOffset,false),
             cue->endOffset-cue->headerOffset)) &&
         catalog.verifyBlock(*block,std::span<const uint8_t>(
             rt.pointer(payloadBase+block->offset,block->bytes,false),block->bytes)),
         "Original guest AMX crash cue/block differs from the exact catalog");
    std::printf("Original AMX startup provenance PASS: payload=%08X allocation=%08X generation=%llu crash_header=%08X cue=11 bytes=%u\n",
        payloadBase,allocation,static_cast<unsigned long long>(generation),
        payloadBase+cue->headerOffset,block->bytes);
}
}

int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==2,"Original image path required");
        Runtime rt;rt.renderTestFirstMission=true;rt.load(argv[1]);
        PPCContext original{};rt.initialize(original);
        std::atomic<bool> passed=false;
        rt.audioBoundaryObserver=[&](uint32_t pc,PPCContext& ctx,uint8_t*) {
            if(pc!=0x8271191C) return;
            verifyOriginalAmx(rt,ctx);
            passed.store(true);
            throw Observed{};
        };
        try {runOriginal(original,rt.base);}catch(const Observed&) {}
        rt.stopThreads();rt.audioBoundaryObserver={};
        need(passed.load(),"Actual original startup did not reach the AMX/SBK named-resource boundary");
        return 0;
    }catch(const std::exception& error) {
        std::fprintf(stderr,"Original AMX startup provenance FAIL: %s\n",error.what());return 1;
    }
}
