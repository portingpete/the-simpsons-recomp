#include "runtime.h"
#include "native_local_players.h"
#include <cstdio>

namespace {
void need(bool ok,const char* why){if(!ok)throw Simpsons::Failure(std::string("Native achievement request: ")+why);}
bool overlap(uint32_t a,uint32_t n,uint32_t b,uint32_t m){return uint64_t(a)<uint64_t(b)+m && uint64_t(b)<uint64_t(a)+n;}
struct HostState {
    const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
    HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
};
}
PPC_FUNC(__imp__XMsgStartIORequest) {
    HostState host;need(Simpsons::active && base==Simpsons::active->base,"foreign runtime");
    auto& rt=*Simpsons::active;rt.checkRunning();
    need(ctx.r3.u32==0xFB && ctx.r4.u32==0xB0008 && ctx.r7.u32==8,"unsupported app/message/payload size");
    const auto ov=ctx.r5.u32,request=ctx.r6.u32;
    need(ov && request && !(ov&3) && !(request&3),"absent or unaligned request/completion");
    rt.pointer(request,8,false);rt.pointer(ov,0x1C,true);
    const auto count=PPC_LOAD_U32(request),records=PPC_LOAD_U32(request+4);
    need(count && count<=25 && records && !(records&3),"unqualified achievement array");rt.pointer(records,count*8,false);
    need(!PPC_LOAD_U32(ov+0x10),"completion callbacks require an APC service");
    auto event=rt.getHandle(PPC_LOAD_U32(ov+0xC));
    need(event && event->type==Simpsons::KernelHandle::Type::Event,"completion has no live event");
    need(!overlap(ov,0x1C,request,8) && !overlap(ov,0x1C,records,count*8),"completion aliases request inputs");
    rt.pointer(ctx.r13.u32,0x154,false);const auto threadAddress=PPC_LOAD_U32(ctx.r13.u32+0x100);
    auto thread=rt.threadObject(threadAddress);
    need(thread && thread->type==Simpsons::KernelHandle::Type::Thread && GetThreadId(thread->native)==GetCurrentThreadId(),"caller is not its owned native thread");
    need(!PPC_LOAD_U32(ctx.r13.u32+0x150),"interrupt-context thread error handling is unqualified");
    const uint32_t errorOutput=threadAddress+0x160;rt.pointer(errorOutput,4,true);
    need(!overlap(errorOutput,4,ov,0x1C) && !overlap(errorOutput,4,request,8) && !overlap(errorOutput,4,records,count*8),"thread error output aliases request");
    std::vector<Simpsons::Platform::LocalAchievement> snapshot;snapshot.reserve(count);
    for(uint32_t i=0;i<count;++i)snapshot.push_back({PPC_LOAD_U32(records+i*8),PPC_LOAD_U32(records+i*8+4)});
    std::lock_guard eventLock(event->stateMutex);
    // The loaded original image's title is45410809. Full durable native GUIDs
    // resolve all slots, and actual files are flushed before completion.
    const auto added=rt.localPlayerSource()->writeAchievements(0x45410809,snapshot);
    uint32_t threadId=0;
    {std::lock_guard lock(rt.handleMutex);for(const auto& [id,object]:rt.handles)if(object==thread){threadId=id;break;}}
    if(!threadId)threadId=rt.addHandle(thread);
    need(ResetEvent(event->native)!=FALSE,"completion reset failed after persistence");
    PPC_STORE_U32(ov+4,0);PPC_STORE_U32(ov+8,threadId);PPC_STORE_U32(ov+0x18,0);
    PPC_STORE_U32(errorOutput,0);PPC_STORE_U32(ov,0);
    need(SetEvent(event->native)!=FALSE,"completion signal failed after persistence");
    ctx.r3.u64=ERROR_IO_PENDING;
    std::fprintf(stderr,"[NATIVE ACHIEVEMENT] request records=%u newly_persisted=%zu event=%08X caller_thread=%08X; durable native files, completed inline\n",
        count,added,PPC_LOAD_U32(ov+0xC),threadId);
    for(const auto& record:snapshot)std::fprintf(stderr,"[NATIVE ACHIEVEMENT] slot=%u id=%08X\n",record.slot,record.id);
}
