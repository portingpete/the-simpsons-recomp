#include "runtime.h"
#include "engine_cpu_calls.h"
#include "audit_stage.h"
#include <cstdio>
#include <cstring>

namespace Simpsons {
namespace {
struct AuditHostState {
    const uint32_t floatingPoint=PPCFPSCRRegister::getcsr();
    const DWORD error=GetLastError();
    ~AuditHostState(){PPCFPSCRRegister::restoreHostCSR(floatingPoint);SetLastError(error);}
};
Runtime& completionRuntime(uint8_t* base) {
    auto* rt=active;
    if(!rt||base!=rt->base)throw Failure("Invalid first-mission completion runtime");
    return *rt;
}
uint32_t auditWord(Runtime& rt,uint64_t address) {
    if(address>UINT32_MAX)throw Failure("Original word address wraps in audit snapshot");
    const auto* p=rt.pointer(uint32_t(address),4,false);
    return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
}
uint32_t auditField(Runtime& rt,uint32_t address,uint32_t offset) {return auditWord(rt,uint64_t(address)+offset);}
uint8_t auditByte(Runtime& rt,uint64_t address) {
    if(address>UINT32_MAX)throw Failure("Original byte address wraps in audit snapshot");
    return *rt.pointer(uint32_t(address),1,false);
}
std::string auditHex(uint32_t value) {
    char text[11];std::snprintf(text,sizeof(text),"0x%08X",value);return text;
}
std::string auditGuid(Runtime& rt,uint64_t address) {
    std::string result;
    for(uint32_t i=0;i<4;++i)result+=auditHex(auditWord(rt,address+4*i)).substr(2);
    return result;
}
std::string auditName(Runtime& rt,uint32_t address) {
    if(!address)throw Failure("Absent original map name in audit snapshot");
    std::string result;
    for(uint32_t i=0;i<256;++i) {
        if(uint64_t(address)+i>UINT32_MAX)throw Failure("Original map name wraps in audit snapshot");
        const auto ch=auditByte(rt,address+i);
        if(!ch)return result;
        if(ch<32||ch>126)throw Failure("Original map name is not bounded ASCII in audit snapshot");
        result+=char(ch);
    }
    throw Failure("Original map name exceeds audit snapshot bound");
}
// 82899828 dispatches the real queued map package through manager+18 before
// 8289ADF8 publishes manager+A8/AC or asks GameMainLoop to load its stream.
// Unknown diagnostics cannot change audit attribution or original admission.
void auditMapLoadRequest(Runtime& rt,const PPCContext& ctx) noexcept {
    if(!rt.resourceAudit.active())return;
    struct HostState {
        const uint32_t floatingPoint=PPCFPSCRRegister::getcsr();
        const DWORD error=GetLastError();
        ~HostState(){PPCFPSCRRegister::restoreHostCSR(floatingPoint);SetLastError(error);}
    } hostState;
    try {
        const auto manager=ctx.r3.u32,operation=ctx.r4.u32,package=ctx.r5.u32,caller=uint32_t(ctx.lr);
        std::string asset="unknown",details="operation="+std::to_string(operation)+" snapshot=unknown";
        std::string ownership="qualification=unknown originalFieldsUnchanged=true";
        std::string instance="manager="+auditHex(manager)+" requestedPackage="+auditHex(package)+
            " record="+auditHex(ctx.r31.u32)+" dispatchManager="+auditHex(ctx.r30.u32)+
            " rawArgument6="+auditHex(ctx.r6.u32)+" sp="+auditHex(ctx.r1.u32)+
            " boundaryOrdinal="+std::to_string(++rt.auditGameplayBoundaryOrdinal);
        try {
            const auto global=auditWord(rt,0x82D08BA8),type=auditWord(rt,manager);
            const auto flags=auditField(rt,manager,1200),currentPackage=auditField(rt,manager,168),currentMap=auditField(rt,manager,172);
            instance+=" globalManager="+auditHex(global)+" publishedPackage="+auditHex(currentPackage)+
                " publishedMap="+auditHex(currentMap);
            details="operation="+std::to_string(operation)+" managerType="+auditHex(type)+" flags="+auditHex(flags);
            bool qualified=false;
            if(operation==0) {
                const auto mapType=auditField(rt,package,8);
                const auto folder=auditName(rt,auditField(rt,package,20)),stream=auditName(rt,auditField(rt,package,28));
                const bool recordMatches=auditWord(rt,ctx.r31.u32)==operation&&
                    auditField(rt,ctx.r31.u32,4)==package&&auditField(rt,ctx.r31.u32,8)==ctx.r6.u32;
                const bool callbackMatches=auditField(rt,manager,24)==0x8289ADF8&&auditField(rt,manager,28)==0;
                asset=folder+"/"+stream;
                details+=" mapType="+std::to_string(mapType)+" authoredCheckpoint="+auditGuid(rt,uint64_t(package)+36)+
                    " liveCheckpoint="+auditGuid(rt,uint64_t(manager)+1184);
                qualified=manager&&manager==global&&type==0x821822E8&&package&&mapType==1&&
                    caller==0x828998C4&&ctx.r30.u32==manager&&recordMatches&&callbackMatches&&
                    !folder.empty()&&!stream.empty();
                ownership="snapshot=readable managerMatchesGlobal="+std::string(manager==global?"true":"false")+
                    " recordArgumentsMatch="+(recordMatches?"true":"false")+
                    " registeredCallbackMatches="+(callbackMatches?"true":"false")+
                    " qualification="+(qualified?"map-load-request":"unqualified")+" originalFieldsUnchanged=true";
                if(qualified)rt.resourceAudit.mission(folder);
            } else ownership="snapshot=readable qualification=other-map-operation originalFieldsUnchanged=true";
        } catch(const std::exception& error) {
            asset="unknown";
            ownership="snapshot=unreadable qualification=unknown originalFieldsUnchanged=true";
            instance+=" snapshotError="+std::string(error.what());
        } catch(...) {asset="unknown";ownership="snapshot=unreadable qualification=unknown originalFieldsUnchanged=true";}
        rt.resourceAudit.lifecycle("map-lifetime",asset,caller,
            std::string(operation==0?"phase=map-load-request ":"phase=map-callback-entry ")+details,ownership,0,instance);
    } catch(...) {std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] original map-load request snapshot failed\n");}
}
// This only records the actual original callback. Qualification controls the
// diagnostic cache, never admission, dispatch or guest memory/state.
void auditGameplayReady(Runtime& rt,const PPCContext& ctx) noexcept {
    if(!rt.resourceAudit.active())return;
    AuditHostState hostState;
    try {
        std::string asset="unknown",details="snapshot=unknown",ownership="snapshot=unknown";
        std::string instance="owner="+auditHex(ctx.r31.u32)+" sp="+auditHex(ctx.r1.u32)+
            " rawLR="+auditHex(uint32_t(ctx.lr))+" boundaryOrdinal="+std::to_string(++rt.auditGameplayBoundaryOrdinal);
        try {
            const auto owner=ctx.r31.u32,global=auditWord(rt,0x82D08C34),manager=auditWord(rt,0x82D08BA8);
            instance+=" globalOwner="+auditHex(global)+" manager="+auditHex(manager);
            const auto state=auditField(rt,owner,12);
            const auto ready=auditByte(rt,uint64_t(owner)+17);
            const auto managerType=auditWord(rt,manager),flags=auditField(rt,manager,1200);
            instance+=" rawOwnerState="+std::to_string(state)+" rawReady="+std::to_string(ready)+" rawManagerFlags="+auditHex(flags);
            const auto episode=auditField(rt,manager,160),mode=auditField(rt,manager,164),
                package=auditField(rt,manager,168),map=auditField(rt,manager,172);
            const auto root=auditField(rt,manager,192);
            instance+=" episode="+auditHex(episode)+" mode="+auditHex(mode)+" package="+auditHex(package)+" map="+auditHex(map)+" rootEpisode="+auditHex(root);
            const auto mapType=auditField(rt,map,8),episodeHash=auditField(rt,episode,32),modeHash=auditField(rt,mode,32);
            const auto episodeFlags=auditField(rt,episode,36);
            std::string rootHash=root?"unknown":"none";
            if(root)try {rootHash=auditHex(auditField(rt,root,32));}catch(...) {}
            const auto folder=auditName(rt,auditField(rt,map,20)),stream=auditName(rt,auditField(rt,map,28));
            const auto authoredCheckpoint=auditGuid(rt,uint64_t(map)+36),liveCheckpoint=auditGuid(rt,uint64_t(manager)+1184);
            asset=folder+"/"+stream;
            details="managerType="+auditHex(managerType)+" mapType="+std::to_string(mapType)+
                " episode="+auditHex(episodeHash)+" mode="+auditHex(modeHash)+" flags="+auditHex(flags)+
                " episodeFlags="+auditHex(episodeFlags)+" rootEpisode="+rootHash+
                " currentEpisodeIsRoot="+(episode==root?"true":"false")+
                " quittableByManagerFields="+((episode&&!(episodeFlags&1)&&!(flags&0x10)&&root&&episode!=root)?"true":"false")+
                " authoredCheckpoint="+authoredCheckpoint+" liveCheckpoint="+liveCheckpoint;
            const bool qualified=owner&&owner==global&&!state&&ready&&managerType==0x821822E8&&
                mapType==1&&package==map&&!folder.empty()&&!stream.empty();
            ownership="snapshot=readable ownerMatchesGlobal="+std::string(owner==global?"true":"false")+
                " ownerState="+std::to_string(state)+" ready="+std::to_string(ready)+
                " currentPackageIsMap="+(package==map?"true":"false")+" qualification="+(qualified?"ready":"unqualified");
            if(qualified) {
                if(rt.auditGameplayOwner!=owner||rt.auditGameplayAsset!=asset)++rt.auditGameplayGeneration;
                rt.auditGameplayOwner=owner;rt.auditGameplayAsset=asset;rt.auditGameplayParameters=details;
                rt.resourceAudit.mission(folder);
                instance+=" ownerGeneration="+std::to_string(rt.auditGameplayGeneration);
            }
            try {instance+=" frameCaller="+auditHex(auditWord(rt,uint64_t(ctx.r1.u32)+88));}catch(...) {instance+=" frameCaller=unknown";}
        } catch(const std::exception& error) {
            ownership="snapshot=unreadable qualification=unknown";
            instance+=" snapshotError="+std::string(error.what());
        } catch(...) {ownership="snapshot=unreadable qualification=unknown";}
        rt.resourceAudit.lifecycle("map-lifetime",asset,0x823BB5D8,"phase=map-ready "+details,ownership,0,instance);
    } catch(...) {std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] original map-ready snapshot failed\n");}
}
void auditGameplayRetired(Runtime& rt,const PPCContext& ctx) noexcept {
    if(!rt.resourceAudit.active())return;
    AuditHostState hostState;
    try {
        const auto owner=ctx.r31.u32;
        const bool cached=owner&&owner==rt.auditGameplayOwner;
        const auto asset=cached?rt.auditGameplayAsset:std::string("unknown");
        const auto details=cached?rt.auditGameplayParameters:std::string("readyIdentity=unknown");
        std::string ownership="cachedReady="+std::string(cached?"true":"false")+" globalOwnerCleared=unknown";
        std::string instance="owner="+auditHex(owner)+" sp="+auditHex(ctx.r1.u32)+
            " rawLR="+auditHex(uint32_t(ctx.lr))+" rawDeleteFlags="+auditHex(ctx.r30.u32)+
            " boundaryOrdinal="+std::to_string(++rt.auditGameplayBoundaryOrdinal);
        if(cached)instance+=" ownerGeneration="+std::to_string(rt.auditGameplayGeneration);
        try {
            const auto global=auditWord(rt,0x82D08C34);
            ownership="cachedReady="+std::string(cached?"true":"false")+" globalOwnerCleared="+(global?"false":"true");
            instance+=" globalOwner="+auditHex(global);
        } catch(...) {}
        try {instance+=" frameCaller="+auditHex(auditWord(rt,uint64_t(ctx.r1.u32)+104));}catch(...) {instance+=" frameCaller=unknown";}
        rt.resourceAudit.lifecycle("map-lifetime",asset,0x823BBCE0,
            "phase=cleanup-complete preFree=true deleteRequested="+std::string(ctx.r30.u32&1?"true":"false")+" "+details,ownership,0,instance);
        if(cached) {
            rt.auditGameplayOwner=0;rt.auditGameplayAsset.clear();rt.auditGameplayParameters.clear();
            // The cleanup receipt retains its old mission. Subsequent work
            // awaits its own qualified request/ready identity; a frontend
            // selector snapshot alone does not establish a mission owner.
            rt.resourceAudit.mission("unknown");
        }
    } catch(...) {std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] original gameplay retirement snapshot failed\n");}
}
void auditLifecycleRequest(Runtime& rt,const PPCContext& ctx,bool frontend) noexcept {
    if(!rt.resourceAudit.active())return;
    struct HostState {
        const uint32_t floatingPoint=PPCFPSCRRegister::getcsr();
        const DWORD error=GetLastError();
        ~HostState(){PPCFPSCRRegister::restoreHostCSR(floatingPoint);SetLastError(error);}
    } hostState;
    try {
        const auto caller=uint32_t(ctx.lr);
        std::string details="snapshot=unknown",ownership="original fields unchanged",asset="unknown";
        std::string instance="r3="+auditHex(ctx.r3.u32)+" sp="+auditHex(ctx.r1.u32);
        try {
            if(frontend) {
                const auto current=auditWord(rt,0x82D08C98),pending=auditWord(rt,0x82D08C9C),notification=auditWord(rt,0x82CD0EE4);
                const auto movie=auditWord(rt,0x82D09750);
                const auto movieState=movie?auditField(rt,movie,20):0;
                const auto frontendOwner=auditWord(rt,0x82D08C94);
                const auto movieIndex=frontendOwner?auditField(rt,frontendOwner,128):0;
                details="selector="+std::to_string(current)+" pending="+std::to_string(pending)+
                    " notification="+std::to_string(notification)+" movieState="+std::to_string(movieState)+
                    " movieIndex="+std::to_string(movieIndex);
                static thread_local const Runtime* lastRuntime{};
                static thread_local std::string lastState;
                if(lastRuntime!=&rt||lastState!=details) {
                    std::fprintf(stderr,"[NATIVE FRONTEND STATE] %s; original dispatcher retained\n",details.c_str());
                    lastRuntime=&rt;lastState=details;
                }
                asset="frontend/original-selector";instance+=" movieOwner="+auditHex(movie)+" frontendOwner="+auditHex(frontendOwner);
            } else {
                const auto manager=ctx.r3.u32,global=auditWord(rt,0x82D08BA8),owner=auditWord(rt,0x82D08C34);
                const auto flags=auditField(rt,manager,1200),type=auditWord(rt,manager);
                const bool cached=owner&&owner==rt.auditGameplayOwner;
                asset=cached?rt.auditGameplayAsset:"unknown";
                details="managerType="+auditHex(type)+" flags="+auditHex(flags)+
                    " liveCheckpoint="+auditGuid(rt,uint64_t(manager)+1184)+
                    " source="+(caller==0x823BBACCu?"whole-party-death":caller==0x823BC3ACu?"level-restart-message":caller==0x823BC5CCu?"episode-failed-message":"other-original-caller");
                ownership="managerMatchesGlobal="+std::string(manager==global?"true":"false")+
                    " cachedReady="+(cached?"true":"false");
                instance+=" globalManager="+auditHex(global)+" gameplayOwner="+auditHex(owner)+
                    " ownerGeneration="+std::to_string(rt.auditGameplayGeneration);
            }
        } catch(const std::exception& error) {details="snapshot=unreadable";instance+=" snapshotError="+std::string(error.what());}
          catch(...) {details="snapshot=unreadable";}
        if(frontend)rt.resourceAudit.observe("frontend-state",asset,caller,details,ownership,0,instance);
        else rt.resourceAudit.lifecycle("failure-request",asset,caller,"phase=original-request "+details,ownership,0,instance);
    } catch(...) {std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] original lifecycle request snapshot failed\n");}
}
bool originalLocReady(Runtime& rt) {
    auto* base=rt.base;
    const auto manager=PPC_LOAD_U32(0x82D08BA8);
    if(!manager)return false;
    rt.pointer(manager,0x4B8,false);
    if(PPC_LOAD_U32(manager)!=0x821822E8)return false;
    const auto episode=PPC_LOAD_U32(manager+160),mode=PPC_LOAD_U32(manager+164),map=PPC_LOAD_U32(manager+172);
    if(!episode||!mode||!map||PPC_LOAD_U32(manager+168)!=map)return false;
    rt.pointer(episode,36,false);rt.pointer(mode,36,false);rt.pointer(map,64,false);
    if(PPC_LOAD_U32(episode+32)!=0x629E5EE8||PPC_LOAD_U32(mode+8)!=3||
       PPC_LOAD_U32(mode+32)!=0x66098359||PPC_LOAD_U32(map+8)!=1||
       PPC_LOAD_U32(map+52)!=0x9FA8DE6B||PPC_LOAD_U32(map+56)!=0x51A559A2)return false;
    const auto folder=PPC_LOAD_U32(map+20),stream=PPC_LOAD_U32(map+28);
    if(!folder||!stream)return false;
    return !std::memcmp(rt.pointer(folder,4,false),"loc",4)&&
           !std::memcmp(rt.pointer(stream,8,false),"loc.str",8);
}
bool originalBartmanReady(Runtime& rt) {
    auto* base=rt.base;
    const auto manager=PPC_LOAD_U32(0x82D08BA8);
    if(!manager)return false;
    rt.pointer(manager,0x4B8,false);
    if(PPC_LOAD_U32(manager)!=0x821822E8)return false;
    const auto episode=PPC_LOAD_U32(manager+160),mode=PPC_LOAD_U32(manager+164),map=PPC_LOAD_U32(manager+172);
    if(!episode||!mode||!map||PPC_LOAD_U32(manager+168)!=map)return false;
    rt.pointer(episode,36,false);rt.pointer(mode,36,false);rt.pointer(map,64,false);
    // Retail gameflow.lua authors BARTMAN_BEGINS / MODE_STANDARD, a nil
    // checkpoint and this exact brt map/stream and completion event.
    if(PPC_LOAD_U32(episode+32)!=0xE7FEB1F0||PPC_LOAD_U32(mode+8)!=3||
       PPC_LOAD_U32(mode+32)!=0x66098359||PPC_LOAD_U32(map+8)!=1||
       PPC_LOAD_U32(map+56)!=0x16DF4FB6||
       (PPC_LOAD_U32(map+36)|PPC_LOAD_U32(map+40)|PPC_LOAD_U32(map+44)|PPC_LOAD_U32(map+48)))return false;
    const auto folder=PPC_LOAD_U32(map+20),stream=PPC_LOAD_U32(map+28);
    if(!folder||!stream)return false;
    return !std::memcmp(rt.pointer(folder,4,false),"brt",4)&&
           !std::memcmp(rt.pointer(stream,8,false),"brt.str",8);
}
bool originalAuditStageReady(Runtime& rt) {
    auto* base=rt.base;
    const auto manager=PPC_LOAD_U32(0x82D08BA8);
    if(!manager)return false;
    rt.pointer(manager,0x4B8,false);
    if(PPC_LOAD_U32(manager)!=0x821822E8)return false;
    const auto map=PPC_LOAD_U32(manager+172);
    if(!map||PPC_LOAD_U32(manager+168)!=map)return false;
    rt.pointer(map,64,false);
    const auto folder=PPC_LOAD_U32(map+20),stream=PPC_LOAD_U32(map+28);
    if(!folder||!stream||PPC_LOAD_U32(map+8)!=1)return false;
    const auto filename=rt.auditStage+".str";
    return !std::memcmp(rt.pointer(folder,unsigned(rt.auditStage.size()+1),false),rt.auditStage.c_str(),rt.auditStage.size()+1)&&
        !std::memcmp(rt.pointer(stream,unsigned(filename.size()+1),false),filename.c_str(),filename.size()+1);
}
bool retainOriginalCompletion(Runtime& rt) {
    if(rt.firstMissionCompletionTriggered)return true;
    auto* base=rt.base;
    const auto manager=PPC_LOAD_U32(0x82D08BA8);
    if(!rt.firstMissionCompletionManager||manager!=rt.firstMissionCompletionManager)return false;
    rt.pointer(manager,0x4B8,false);
    // EpisodeComplete sets success before removing the active map. Recognize
    // it even when original exit has already moved to the outro package.
    if(!(PPC_LOAD_U32(manager+1200)&0x40000))return false;
    rt.firstMissionCompletionTriggered=true;
    std::fprintf(stderr,"[FIRST MISSION COMPLETION] original LOC success already dispatched; outro retained\n");
    return true;
}
}
bool firstMissionCompletionSkipBootstrap(uint8_t* base) {
    auto& rt=completionRuntime(base);
    return rt.firstMissionCompletion&&!retainOriginalCompletion(rt);
}
bool auditStageSkipBootstrap(uint8_t* base) {
    auto& rt=completionRuntime(base);
    return !rt.auditStage.empty()&&!rt.auditStageMapStarted&&!rt.auditStagePlayIntro;
}
void firstMissionCompletionFrame(PPCContext& ctx,uint8_t* base) {
    auto& rt=completionRuntime(base);
    if(!rt.firstMissionCompletion||rt.firstMissionCompletionTriggered||!rt.firstMissionCompletionOwner)return;
    rt.checkRunning();
    if(retainOriginalCompletion(rt))return;
    const auto manager=PPC_LOAD_U32(0x82D08BA8);
    if(!manager||manager!=rt.firstMissionCompletionManager)return;
    rt.pointer(manager,0x4B8,false);
    // Busy exit and restart paths must complete before normal score publication.
    if(PPC_LOAD_U32(manager+1200)&(0x1000|0x100000))return;
    const auto owner=PPC_LOAD_U32(0x82D08C34);
    if(owner!=rt.firstMissionCompletionOwner)return;
    rt.pointer(owner,20,false);
    if(PPC_LOAD_U32(owner+12)||!PPC_LOAD_U8(owner+17)||PPC_LOAD_U8(owner+19)||!originalLocReady(rt))return;
    const auto movie=PPC_LOAD_U32(0x82D09750);
    if(!movie)return;
    rt.pointer(movie,24,false);
    // State 4 still owns a stopped movie's completion dispatch. Wait for the
    // original movie cleanup to return to idle, including decoder retirement.
    if(PPC_LOAD_U32(movie+20)||PPC_LOAD_U32(0x82E06F5C))return;
    // Stop bootstrap-only movie skipping before retail completion can queue
    // loc_igc02. All exit, score, results and next-episode work stays original.
    rt.firstMissionCompletionTriggered=true;
    std::fprintf(stderr,"[FIRST MISSION COMPLETION] original EpisodeComplete 8296FFC8 owner=%08X manager=%08X; outro/results retained\n",owner,manager);
    EngineCpuCalls cpu(ctx,base);
    cpu.invoke(0x8296FFC8);
}
}

// Original 823BB578 reaches this epilogue only after streaming finishes and
// 8289ED68 has initialized the active map, score events and player state.
void SimpsonsNativeFirstMissionCompletionReady(PPCContext& ctx,uint8_t* base) {
    auto& rt=Simpsons::completionRuntime(base);
    Simpsons::auditGameplayReady(rt,ctx);
    if(!rt.auditStage.empty()&&!rt.auditStageMapStarted) {
        rt.checkRunning();
        const auto owner=PPC_LOAD_U32(0x82D08C34);
        if(!owner||ctx.r31.u32!=owner)throw Simpsons::Failure("Original audit map-start owner differs");
        rt.pointer(owner,20,false);
        if(PPC_LOAD_U32(owner+12)||!PPC_LOAD_U8(owner+17))throw Simpsons::Failure("Original audit map-start completion is not ready");
        if(!Simpsons::originalAuditStageReady(rt))return;
        rt.auditStageMapStarted=true;
        rt.resourceAudit.mission(rt.auditStage);
        std::fprintf(stderr,"[STAGE AUDIT] original map initialized stage=%s owner=%08X; normal movie controls restored\n",rt.auditStage.c_str(),owner);
        return;
    }
    if(rt.bartmanBegins&&!rt.bartmanBeginsMapStarted) {
        rt.checkRunning();
        const auto owner=PPC_LOAD_U32(0x82D08C34);
        if(ctx.r31.u32!=owner||!owner)throw Simpsons::Failure("Original Bartman map-start owner differs");
        rt.pointer(owner,20,false);
        if(PPC_LOAD_U32(owner+12)||!PPC_LOAD_U8(owner+17))
            throw Simpsons::Failure("Original Bartman map-start completion is not ready");
        if(!Simpsons::originalBartmanReady(rt))return;
        rt.bartmanBeginsMapStarted=true;
        std::fprintf(stderr,"[BARTMAN BEGINS] original brt map initialized owner=%08X; normal movie controls restored\n",owner);
        return;
    }
    if(!rt.firstMissionCompletion||rt.firstMissionCompletionTriggered)return;
    rt.checkRunning();
    const auto owner=PPC_LOAD_U32(0x82D08C34);
    if(ctx.r31.u32!=owner||!owner)throw Simpsons::Failure("Original completed map-start owner differs");
    rt.pointer(owner,20,false);
    if(PPC_LOAD_U32(owner+12)||!PPC_LOAD_U8(owner+17))
        throw Simpsons::Failure("Original map-start completion is not ready");
    if(!Simpsons::originalLocReady(rt))return;
    rt.firstMissionCompletionOwner=owner;
    rt.firstMissionCompletionManager=PPC_LOAD_U32(0x82D08BA8);
    std::fprintf(stderr,"[FIRST MISSION COMPLETION] original LOC map initialized; waiting for bootstrap movie cleanup\n");
}

// Original 823BBCC0 returns from full derived cleanup before this boundary;
// its optional guest free and complete original CPU epilogue remain intact.
void SimpsonsNativeGameplayOwnerRetired(PPCContext& ctx,uint8_t* base) {
    auto* rt=Simpsons::active;
    if(rt&&base==rt->base)Simpsons::auditGameplayRetired(*rt,ctx);
}

void SimpsonsNativeMapLoadRequestAudit(PPCContext& ctx,uint8_t* base) {
    auto* rt=Simpsons::active;
    if(rt&&base==rt->base)Simpsons::auditMapLoadRequest(*rt,ctx);
}

void SimpsonsNativeFrontendStateAudit(PPCContext& ctx,uint8_t* base) {
    auto* rt=Simpsons::active;
    if(rt&&base==rt->base)Simpsons::auditLifecycleRequest(*rt,ctx,true);
}

void SimpsonsNativeEpisodeFailureAudit(PPCContext& ctx,uint8_t* base) {
    auto* rt=Simpsons::active;
    if(rt&&base==rt->base)Simpsons::auditLifecycleRequest(*rt,ctx,false);
}

// Temporary rendering shortcut. Original8285F928 already implements
// -stream <folder> <stream>, including gameflow/player/map initialization.
// Install borrowed arguments immediately before that parser; retain all of
// its normal CPU loading code and leave ordinary launches untouched.
void SimpsonsNativeRenderTestStartup(PPCContext& ctx,uint8_t* base) {
    auto* rt=Simpsons::active;
    if(!rt || base!=rt->base)throw Simpsons::Failure("Invalid render-test startup runtime");
    if(!rt->renderTestFirstMission&&!rt->firstMissionCompletion&&!rt->bartmanBegins&&rt->auditStage.empty())return;
    if(unsigned(rt->renderTestFirstMission)+unsigned(rt->firstMissionCompletion)+unsigned(rt->bartmanBegins)+unsigned(!rt->auditStage.empty())>1)
        throw Simpsons::Failure("Conflicting direct stage startup modes");
    if(!rt->auditStage.empty()&&!Simpsons::isAuditStage(rt->auditStage))throw Simpsons::Failure("Unknown original audit stage");
    rt->checkRunning();
    uint32_t address=0,size=0x1000;
    if(rt->allocateVirtual(address,size,0x3000,PAGE_READWRITE)!=0)
        throw Simpsons::Failure("Unable to allocate render-test launch arguments");
    const auto folder=!rt->auditStage.empty()?rt->auditStage:(rt->bartmanBegins?std::string("brt"):std::string("loc"));
    const auto filename=folder+".str";
    const char* arguments[]={"-stream",folder.c_str(),filename.c_str()};
    uint32_t offset=16;
    for(uint32_t i=0;i<3;++i) {
        const auto length=uint32_t(std::strlen(arguments[i])+1);
        std::memcpy(rt->pointer(address+offset,length,true),arguments[i],length);
        PPC_STORE_U32(address+4*i,address+offset);
        offset+=length;
    }
    PPC_STORE_U32(address+12,0);
    Simpsons::EngineCpuCalls cpu(ctx,base);
    // Original setter clears previous arguments and marks these as borrowed;
    // the native Runtime owns their storage until process teardown.
    cpu.invoke(0x828759B8,3,address);
    if(!rt->auditStage.empty())
        std::fprintf(stderr,"[STAGE AUDIT] original startup: -stream %s %s; parser and map setup retained\n",folder.c_str(),filename.c_str());
    else if(rt->bartmanBegins)
        std::fprintf(stderr,"[BARTMAN BEGINS] original stage startup: -stream brt brt.str; completion transition bypassed\n");
    else if(rt->firstMissionCompletion)
        std::fprintf(stderr,"[FIRST MISSION COMPLETION] original LOC startup: -stream loc loc.str; completion pending\n");
    else std::fprintf(stderr,"[RENDER TEST] temporary first-mission shortcut: -stream loc loc.str\n");
}
