#include "runtime.h"
#include "native_saves.h"
#include "native_local_players.h"
#include <cstring>

namespace Simpsons {
std::shared_ptr<Platform::NativeSaveStore> Runtime::nativeSaveSource(bool create){
    if(active!=this)throw Failure("Native save store belongs to another runtime");checkRunning();std::lock_guard lock(nativeSaveMutex);
    if(!nativeSaves&&create){if(!gameRoot.is_absolute())throw Failure("Native save store has no original image root");
        nativeSaves=std::make_shared<Platform::NativeSaveStore>(contentRoot.empty()?gameRoot.parent_path()/"saves":contentRoot);}
    return nativeSaves;
}
}
namespace {
using namespace Simpsons;
struct HostState {uint32_t fp=PPCFPSCRRegister::getcsr();DWORD error=GetLastError();
    HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}};
void need(bool value,const char* why){if(!value)throw Failure(why);}
Runtime& runtime(uint8_t* base){need(active&&base==active->base,"Invalid native save runtime");active->checkRunning();return *active;}
std::string alias(Runtime& rt,uint32_t address){need(address!=0,"Native save alias is absent");std::string result;
    for(uint32_t i=0;i<33;++i){const auto c=*rt.pointer(address+i,1,false);if(!c){need(result=="rmcsave","Unqualified original save root alias");return result;}need(c>=32&&c<127,"Invalid save alias character");result+=char(c);}
    throw Failure("Native save alias exceeds its extent");}
void output(Runtime& rt,uint32_t address,uint32_t bytes){if(address){need(!(address&3),"Unaligned native save output");rt.pointer(address,bytes,true);}}
bool overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn){return an&&bn&&uint64_t(a)<uint64_t(b)+bn&&uint64_t(b)<uint64_t(a)+an;}
Platform::NativeSaveInfo info(Runtime& rt,uint8_t* base,uint32_t slot,uint32_t address){
    need(slot<4&&address&&!(address&3),"Invalid native save owner/content record");rt.pointer(address,0x134,false);
    need(PPC_LOAD_U32(address)==1&&PPC_LOAD_U32(address+4)==1,"Unqualified native save device/type");
    const auto profile=rt.localPlayerSource()->profile(slot);if(!profile)throw Platform::NativeSaveError(ERROR_NO_SUCH_USER,"Native save owner is signed out");
    Platform::NativeSaveInfo result{profile->id,0x45410809,{},{}};
    bool terminated=false;for(uint32_t i=0;i<128;++i){const auto c=PPC_LOAD_U16(address+8+i*2);if(!c){terminated=true;break;}result.display+=wchar_t(c);}
    need(terminated&&!result.display.empty(),"Original save display name lacks a bounded terminator");
    for(uint32_t i=0;i<42;++i){const auto c=PPC_LOAD_U8(address+0x108+i);if(!c)break;result.name+=char(c);}
    need(!result.name.empty(),"Original save filename is absent");return result;
}
template<class F>void invoke(PPCContext& ctx,F action){try{action();}catch(const Platform::NativeSaveError& e){std::fprintf(stderr,"[NATIVE SAVE] native operation failed status=%u: %s\n",e.code,e.what());ctx.r3.u64=e.code;}}
}
PPC_FUNC(__imp__XamContentCreateEx){
    HostState host;auto& rt=runtime(base);rt.pointer(ctx.r1.u32,0x58,false);
    const auto slot=ctx.r3.u32,root=ctx.r4.u32,record=ctx.r5.u32,flags=ctx.r6.u32,dispositionOut=ctx.r7.u32,licenseOut=ctx.r8.u32;
    need(!ctx.r9.u32&&!ctx.r10.u64&&!PPC_LOAD_U32(ctx.r1.u32+0x54),"Unqualified cache/size/asynchronous save create request");
    need((flags&~0x1Fu)==0&&(flags&0x10)&&((flags&15)>=1)&&((flags&15)<=5),"Unqualified native save create flags");
    const auto name=alias(rt,root);output(rt,dispositionOut,4);output(rt,licenseOut,4);
    need(!overlap(dispositionOut,dispositionOut?4:0,record,0x134)&&!overlap(licenseOut,licenseOut?4:0,record,0x134)&&
        !(dispositionOut&&dispositionOut==licenseOut),"Aliased native save create outputs");
    invoke(ctx,[&]{const auto source=info(rt,base,slot,record);const auto disposition=rt.nativeSaveSource()->open(name,source,flags&15);
        if(dispositionOut)PPC_STORE_U32(dispositionOut,disposition);if(licenseOut)PPC_STORE_U32(licenseOut,0);ctx.r3.u64=0;
        std::fprintf(stderr,"[NATIVE SAVE] open alias=%s profile=%s title=%08X name=%s display=%ls flags=%X disposition=%u; real native folder session, unpublished until flush/close\n",
            name.c_str(),source.profile.c_str(),source.title,source.name.c_str(),source.display.c_str(),flags,disposition);});
}
PPC_FUNC(__imp__XamContentGetCreator){
    HostState host;auto& rt=runtime(base);const auto slot=ctx.r3.u32,record=ctx.r4.u32,isCreator=ctx.r5.u32,identity=ctx.r6.u32;
    need(!ctx.r7.u32&&isCreator,"Unqualified asynchronous/absent save creator output");output(rt,isCreator,4);output(rt,identity,8);
    need(!overlap(isCreator,4,record,0x134)&&!overlap(identity,identity?8:0,record,0x134)&&!overlap(isCreator,4,identity,identity?8:0),"Aliased native save creator outputs");
    invoke(ctx,[&]{const auto source=info(rt,base,slot,record);if(!rt.nativeSaveSource()->exists(source))throw Platform::NativeSaveError(ERROR_FILE_NOT_FOUND,"Native save has no actual owner record/session");
        const auto players=rt.localPlayerSource();const auto key=players->identity(slot);const auto still=players->profile(slot);
        if(!key||!still||still->id!=source.profile)throw Platform::NativeSaveError(ERROR_NO_SUCH_USER,"Native save owner changed during creator query");
        PPC_STORE_U32(isCreator,1);if(identity)PPC_STORE_U64(identity,key);ctx.r3.u64=0;
        std::fprintf(stderr,"[NATIVE SAVE] creator profile=%s name=%s; actual native GUID owner\n",source.profile.c_str(),source.name.c_str());});
}
PPC_FUNC(__imp__XamContentFlush){
    HostState host;auto& rt=runtime(base);need(!ctx.r4.u32,"Asynchronous native save flush is not qualified");const auto name=alias(rt,ctx.r3.u32);
    invoke(ctx,[&]{rt.nativeSaveSource()->flush(name);ctx.r3.u64=0;std::fprintf(stderr,"[NATIVE SAVE] flushed alias=%s; actual payload and published index\n",name.c_str());});
}
PPC_FUNC(__imp__XamContentClose){
    HostState host;auto& rt=runtime(base);need(!ctx.r4.u32,"Asynchronous native save close is not qualified");const auto name=alias(rt,ctx.r3.u32);
    invoke(ctx,[&]{rt.nativeSaveSource()->close(name);ctx.r3.u64=0;std::fprintf(stderr,"[NATIVE SAVE] closed alias=%s; native index publication and session release complete\n",name.c_str());});
}

// Read-only observation of the original save-load state machine. Every original
// instruction still executes; no status, register or save byte is changed.
void SimpsonsNativeSaveLoadEvent(PPCContext& ctx,uint8_t* base){
    HostState host;auto& rt=runtime(base);const auto object=ctx.r3.u32,event=ctx.r4.u32;
    rt.pointer(object,0x17C,false);rt.pointer(event,4,false);
    std::fprintf(stderr,"[NATIVE SAVE LOAD] object=%08X event=%u state=%u size=%u header=%08X/%u/%u/%u/%08X/%08X/%08X status=%u; original callback observed\n",
        object,PPC_LOAD_U32(event),PPC_LOAD_U32(object+0x8C),PPC_LOAD_U32(object+0xFC),
        PPC_LOAD_U32(object+0x14C),PPC_LOAD_U32(object+0x150),PPC_LOAD_U32(object+0x154),PPC_LOAD_U32(object+0x158),
        PPC_LOAD_U32(object+0x15C),PPC_LOAD_U32(object+0x160),PPC_LOAD_U32(object+0x164),PPC_LOAD_U32(object+0x178));
    if(PPC_LOAD_U32(event)==28){rt.pointer(event,0x5C,false);
        std::fprintf(stderr,"[NATIVE SAVE LOAD SIZE] event=%08X bytes=%u caller=%08X\n",event,PPC_LOAD_U32(event+0x58),uint32_t(ctx.lr));
    }
}
void SimpsonsNativeSaveMetadataBegin(PPCContext& ctx,uint8_t* base){
    HostState host;(void)runtime(base);
    std::fprintf(stderr,"[NATIVE SAVE METADATA BEGIN] r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X caller=%08X\n",ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,ctx.r7.u32,uint32_t(ctx.lr));
}
void SimpsonsNativeSaveMetadataResult(PPCContext& ctx,uint8_t* base){
    HostState host;(void)runtime(base);
    std::fprintf(stderr,"[NATIVE SAVE METADATA RESULT] result=%u first=%08X filter=%08X name=%08X output=%08X\n",ctx.r3.u32,ctx.r27.u32,ctx.r28.u32,ctx.r30.u32,ctx.r31.u32);
}
