#include "engine_audio.h"
#include <cstdio>

namespace {
Simpsons::Runtime& audioRuntime(uint8_t* base) {
    if(!Simpsons::active || base!=Simpsons::active->base) throw Simpsons::Failure("Invalid native audio bridge runtime");
    Simpsons::active->checkRunning();return *Simpsons::active;
}
std::shared_ptr<Simpsons::EngineAudioOwners> owners(uint8_t* base,bool create=false) {
    auto& rt=audioRuntime(base);std::lock_guard lock(rt.audioMutex);
    if(create && !rt.engineAudio) rt.engineAudio=std::make_shared<Simpsons::EngineAudioOwners>(rt);
    return rt.engineAudio;
}
auto requiredOwners(uint8_t* base) {
    auto value=owners(base);if(!value) throw Simpsons::Failure("Native EXm0 callback has no factory ownership");return value;
}
}
void SimpsonsNativeAudioProvider(PPCContext& ctx,uint8_t* base) {owners(base,true)->start(ctx,base);}
void SimpsonsNativeAudioStop(PPCContext& ctx,uint8_t* base) {owners(base,true)->stop(ctx,base);}
void SimpsonsNativeAudioDisable(PPCContext& ctx,uint8_t* base) {requiredOwners(base)->enable(ctx,base,false);}
void SimpsonsNativeAudioEnable(PPCContext& ctx,uint8_t* base) {requiredOwners(base)->enable(ctx,base,true);}
void SimpsonsNativeAudioCreateBegin(PPCContext& ctx,uint8_t* base) {
    if(ctx.r4.u32==0x82D073AC) requiredOwners(base)->createBegin(ctx,base);
}
void SimpsonsNativeAudioCreateAllocated(PPCContext& ctx,uint8_t* base) {
    if(auto value=owners(base)) value->createAllocated(ctx,base);
    else if(ctx.r30.u32==0x82D073AC) throw Simpsons::Failure("Native EXm0 allocation lacks factory");
}
void SimpsonsNativeAudioConstruct(PPCContext& ctx,uint8_t* base) {requiredOwners(base)->construct(ctx,base);}
void SimpsonsNativeAudioCreateEnd(PPCContext& ctx,uint8_t* base) {if(auto value=owners(base)) value->createEnd(ctx,base);}
void SimpsonsNativeAudioDestroyBegin(PPCContext& ctx,uint8_t* base) {owners(base,true)->destroyBegin(ctx,base);}
void SimpsonsNativeAudioRelease(PPCContext& ctx,uint8_t* base) {requiredOwners(base)->release(ctx,base);}
void SimpsonsNativeAudioDestroyEnd(PPCContext& ctx,uint8_t* base) {if(auto value=owners(base)) value->destroyEnd(ctx,base);}
void SimpsonsNativeAudioInputPreflight(PPCContext& ctx,uint8_t* base) {owners(base,true)->inputPreflight(ctx,base);}
void SimpsonsNativeAudioProducerBegin(PPCContext& ctx,uint8_t* base) {requiredOwners(base)->producerBegin(ctx,base);}
void SimpsonsNativeAudioProducerEnd(PPCContext& ctx,uint8_t* base) {if(auto value=owners(base)) value->producerEnd(ctx,base);}
void SimpsonsNativeAudioInput(PPCContext& ctx,uint8_t* base) {requiredOwners(base)->input(ctx,base);}
void SimpsonsNativeAudioDecode(PPCContext& ctx,uint8_t* base) {requiredOwners(base)->decode(ctx,base);}
void SimpsonsNativeAudioAdvanceBegin(PPCContext& ctx,uint8_t* base) {if(auto value=owners(base)) value->advanceBegin(ctx,base);}
void SimpsonsNativeAudioAdvancePartial(PPCContext& ctx,uint8_t* base) {if(auto value=owners(base)) value->advanceEnd(ctx,base,false);}
void SimpsonsNativeAudioAdvanceComplete(PPCContext& ctx,uint8_t* base) {if(auto value=owners(base)) value->advanceEnd(ctx,base,true);}
void SimpsonsNativeAudioSourceFreeBegin(PPCContext& ctx,uint8_t* base) {if(auto value=owners(base)) value->sourceFreeBegin(0x82341840,ctx,base);}
void SimpsonsNativeAudioSourceFreeEnd(PPCContext& ctx,uint8_t* base) {if(auto value=owners(base)) value->sourceFreeEnd(0x82341880,ctx,base);}
void SimpsonsNativeAudioSourceCancelBegin(PPCContext& ctx,uint8_t* base) {if(auto value=owners(base)) value->sourceFreeBegin(0x82342D28,ctx,base);}
void SimpsonsNativeAudioSourceCancelEnd(PPCContext& ctx,uint8_t* base) {if(auto value=owners(base)) value->sourceFreeEnd(0x82342D60,ctx,base);}
void SimpsonsNativeAudioResidentFreeEnd(PPCContext& ctx,uint8_t* base) {if(auto value=owners(base)) value->sourceFreeEnd(0x82341884,ctx,base);}
void SimpsonsNativeAudioResidentCancelEnd(PPCContext& ctx,uint8_t* base) {if(auto value=owners(base)) value->sourceFreeEnd(0x82342D64,ctx,base);}
#define AUDIO_HEAP(pc) void SimpsonsAudioHeap##pc(PPCContext& ctx,uint8_t* base) {owners(base,true)->observeHeap(0x##pc,ctx,base);}
AUDIO_HEAP(8268DDA0)
AUDIO_HEAP(8268DED0)
AUDIO_HEAP(8268DEF0)
AUDIO_HEAP(8268DF34)
AUDIO_HEAP(8268DF50)
AUDIO_HEAP(8268DF84)
AUDIO_HEAP(8268DF90)
AUDIO_HEAP(8268DFD4)
AUDIO_HEAP(8268E014)
AUDIO_HEAP(8268E018)
#undef AUDIO_HEAP
#define AUDIO_BANK(pc) void SimpsonsAudioBank##pc(PPCContext& ctx,uint8_t* base) {owners(base,true)->observeBank(0x##pc,ctx,base);}
AUDIO_BANK(82711918)
AUDIO_BANK(8271191C)
AUDIO_BANK(826F1DCC)
AUDIO_BANK(826F1E04)
AUDIO_BANK(8272E9DC)
AUDIO_BANK(8272E9E0)
AUDIO_BANK(82807C4C)
AUDIO_BANK(82811568)
AUDIO_BANK(82811580)
AUDIO_BANK(82811584)
AUDIO_BANK(828115A8)
AUDIO_BANK(828115D4)
AUDIO_BANK(82807C6C)
AUDIO_BANK(82812A00)
#undef AUDIO_BANK
#define AUDIO_STREAM(pc) void SimpsonsAudioStream##pc(PPCContext& ctx,uint8_t* base) {owners(base,true)->observeStream(0x##pc,ctx,base);}
AUDIO_STREAM(8233C654)
AUDIO_STREAM(8233CD44)
AUDIO_STREAM(8233CD48)
AUDIO_STREAM(8233C658)
AUDIO_STREAM(8233C77C)
AUDIO_STREAM(8233C780)
AUDIO_STREAM(823418E8)
AUDIO_STREAM(823419AC)
AUDIO_STREAM(823419B0)
AUDIO_STREAM(82341AB0)
AUDIO_STREAM(823425E8)
AUDIO_STREAM(82341AB8)
AUDIO_STREAM(82341B04)
AUDIO_STREAM(82341B08)
AUDIO_STREAM(8233C838)
AUDIO_STREAM(8233C924)
AUDIO_STREAM(8233C928)
#undef AUDIO_STREAM
#define AUDIO_GUARD(name,pc,reason) void name(PPCContext& ctx,uint8_t* base) {audioRuntime(base);PPC_RECOMP_FAILURE(ctx,pc,reason);}
AUDIO_GUARD(SimpsonsRejectAudioPoolInit,0x8233E5C0,"Original XMA hardware pool initialization is replaced by native EXm0 factory ownership")
AUDIO_GUARD(SimpsonsRejectAudioContextQuery,0x8233E568,"Original XMA hardware-context reads are not a native codec operation")
AUDIO_GUARD(SimpsonsRejectAudioContextInit,0x8233ED78,"Native EXm0 source format/configuration is not implemented")
AUDIO_GUARD(SimpsonsRejectAudioReset,0x8233FF80,"Native EXm0 stream-error reset is not implemented")
AUDIO_GUARD(SimpsonsRejectAudioFeeder,0x8233F000,"Native EXm0 source lease/packet feeding is not implemented")
AUDIO_GUARD(SimpsonsRejectAudioDeletingDestructor,0x8233EC00,"EXm0 deleting destructor bypasses required native owner retirement")
AUDIO_GUARD(SimpsonsRejectAudioBaseDestructor,0x8233EC48,"EXm0 base destructor bypasses required native owner retirement")
AUDIO_GUARD(SimpsonsRejectAudioBaseVtable,0x8233EBF0,"EXm0 base vtable mutation bypasses required native owner retirement")
#undef AUDIO_GUARD
