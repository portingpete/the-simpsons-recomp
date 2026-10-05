#include "threads.h"
#include <algorithm>
#include <cstdio>

namespace Simpsons {
std::shared_ptr<KernelHandle> Runtime::threadObject(uint32_t address) {
    std::lock_guard lock(threadMutex);
    if(mainThreadHandle && mainThreadHandle->guestObject==address) return mainThreadHandle;
    for(const auto& thread:threads) if(thread->thread==address) return thread->object;
    return nullptr;
}
}
PPC_FUNC(__imp__ObReferenceObjectByHandle) {
    auto& rt=*Simpsons::active;
    uint32_t handle=ctx.r3.u32,type=ctx.r4.u32,output=ctx.r5.u32;
    if(!output) {ctx.r3.u64=0xc000000d;return;}
    PPCGuestPointer(base,output,4,true);
    uint32_t currentThreadObject=0;
    if(handle==0xfffffffeu){
        if(UINT32_MAX-ctx.r13.u32<0x100) {ctx.r3.u64=0xc000000d;return;}
        currentThreadObject=PPC_LOAD_U32(ctx.r13.u32+0x100);
    }
    auto object=handle==0xfffffffeu?rt.threadObject(currentThreadObject):rt.getHandle(handle);
    if(!object) {ctx.r3.u64=0xc0000008;return;}
    if(object->type!=Simpsons::KernelHandle::Type::Thread)
        PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"Guest object references for this native handle type are not implemented");
    if(type && type!=rt.threadObjectType) {ctx.r3.u64=0xc0000024;return;}
    {
        std::lock_guard lock(rt.handleMutex);
        auto& reference=rt.objectReferences[object->guestObject];
        reference.object=object;
        if(reference.count==0xffffffffu) throw Simpsons::Failure("Guest object reference count overflow");
        ++reference.count;
    }
    PPC_STORE_U32(output,object->guestObject);
    ctx.r3.u64=0;
}
PPC_FUNC(__imp__ObDereferenceObject) {
    auto& rt=*Simpsons::active;
    std::lock_guard lock(rt.handleMutex);
    auto reference=rt.objectReferences.find(ctx.r3.u32);
    if(reference==rt.objectReferences.end() || !reference->second.count)
        PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"Object dereference has no matching retained reference");
    if(--reference->second.count==0) rt.objectReferences.erase(reference);
    ctx.r3.u64=0;
}
namespace {
int nativePriority(int increment) {
    return increment>=7?THREAD_PRIORITY_TIME_CRITICAL:(increment<=-7?THREAD_PRIORITY_IDLE:increment);
}
int queryPriority(const Simpsons::KernelHandle& thread) {
    if(!thread.native || thread.native==INVALID_HANDLE_VALUE)
        throw Simpsons::Failure("Priority query has no native thread");
    if(GetPriorityClass(GetCurrentProcess())!=NORMAL_PRIORITY_CLASS)
        throw Simpsons::Failure("Guest priority contract requires the native normal process priority class");
    int priority=GetThreadPriority(thread.native);
    if(priority==THREAD_PRIORITY_ERROR_RETURN || priority!=nativePriority(thread.priorityIncrement))
        throw Simpsons::Failure("Native thread priority differs from the assigned guest priority");
    return thread.priorityIncrement;
}
}
PPC_FUNC(__imp__KeQueryBasePriorityThread) {
    auto object=Simpsons::active->threadObject(ctx.r3.u32);
    if(!object) PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"Priority query references an unknown guest thread");
    std::lock_guard lock(object->stateMutex);
    ctx.r3.s64=queryPriority(*object);
}
PPC_FUNC(__imp__KeSetBasePriorityThread) {
    uint32_t address=ctx.r3.u32;
    auto object=Simpsons::active->threadObject(address);
    if(!object) PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"Priority change references an unknown guest thread");
    std::lock_guard lock(object->stateMutex);
    int increment=ctx.r4.s32,previous=queryPriority(*object);
    int effective=increment>=16?16:(increment<=-16?-16:std::clamp(increment,-7,7));
    if((effective>2 && effective<7) || (effective<-2 && effective>-7))
        PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"Intermediate native thread priority requires an exact scheduler mapping");
    if(!object->native || object->native==INVALID_HANDLE_VALUE) throw Simpsons::Failure("Priority change has no native thread");
    if(!SetThreadPriority(object->native,nativePriority(effective))) throw Simpsons::Failure("Native thread priority update failed");
    object->priorityIncrement=effective;
    PPC_STORE_U8(address+0x70,uint32_t(std::clamp<int64_t>(8ll+increment,1,15)));
    ctx.r3.s64=previous;
}
PPC_FUNC(__imp__KeSetAffinityThread) {
    auto& rt=*Simpsons::active;
    uint32_t address=ctx.r3.u32,mask=ctx.r4.u32,output=ctx.r5.u32;
    if(!mask || (mask&~0x3fu)) {ctx.r3.u64=0xc000000d;return;}
    if(output) PPCGuestPointer(base,output,4,true);
    std::lock_guard lock(rt.threadMutex);
    std::shared_ptr<Simpsons::KernelHandle> object;
    uint32_t pcr=0;
    if(rt.mainThreadHandle && address==rt.threadAddress) {object=rt.mainThreadHandle;pcr=rt.pcrAddress;}
    else for(const auto& thread:rt.threads) if(thread->thread==address) {object=thread->object;pcr=thread->pcr;break;}
    if(!object) PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"Affinity change references an unknown guest thread");
    uint32_t previous=PPC_LOAD_U32(pcr+0x110);
    Simpsons::assignThreadAffinity(rt,object->native,pcr,address,mask);
    if(output) PPC_STORE_U32(output,previous);
    ctx.r3.u64=0;
}

PPC_FUNC(__imp__RtlRaiseException) {
    if(!Simpsons::active || base!=Simpsons::active->base) throw Simpsons::Failure("Invalid native exception runtime");
    auto& rt=*Simpsons::active;rt.checkRunning();
    const uint32_t record=ctx.r3.u32;
    rt.pointer(record,20,false);
    const uint32_t code=PPC_LOAD_U32(record),flags=PPC_LOAD_U32(record+4),
        chained=PPC_LOAD_U32(record+8),address=PPC_LOAD_U32(record+12),count=PPC_LOAD_U32(record+16);
    // Original 8232B268 emits the MSVC thread-name notification through the
    // original 82B76078 RaiseException wrapper. Other guest exceptions still
    // require a real dispatcher/unwinder and must never be swallowed.
    if(code!=0x406D1388 || flags || chained || count!=4 || address!=0x82B76078 || ctx.lr!=0x82B760E8) {
        char message[256];
        int n=std::snprintf(message,sizeof(message),"Unsupported guest exception code=%08X flags=%08X chained=%08X address=%08X parameters=%u caller=%08X",
            code,flags,chained,address,count,uint32_t(ctx.lr));
        if(n<0||size_t(n)>=sizeof(message)) throw Simpsons::Failure("Unsupported guest exception: diagnostic truncated");
        throw Simpsons::Failure(message);
    }
    rt.pointer(record,36,false);
    const uint32_t type=PPC_LOAD_U32(record+20),name=PPC_LOAD_U32(record+24),
        requestedThread=PPC_LOAD_U32(record+28),reserved=PPC_LOAD_U32(record+32);
    if(type!=0x1000 || !name || reserved)
        throw Simpsons::Failure("Malformed original debugger thread-name notification");
    // Original helper copies 32 bytes and explicitly terminates byte 31.
    // The verified ASCII profile has an exact Unicode conversion.
    rt.pointer(name,32,false);std::wstring description;std::string diagnostic;
    for(uint32_t i=0;i<32;++i) {
        const uint8_t c=PPC_LOAD_U8(name+i);
        if(!c) break;
        if(c<0x20 || c>0x7E || i==31)
            throw Simpsons::Failure("Unsupported encoding or termination in original thread name");
        description.push_back(wchar_t(c));diagnostic.push_back(char(c));
    }
    const uint32_t target=requestedThread==0xFFFFFFFF?GetCurrentThreadId():requestedThread;
    std::shared_ptr<Simpsons::KernelHandle> object;
    {
        std::lock_guard lock(rt.threadMutex);
        if(rt.mainThreadHandle && GetThreadId(rt.mainThreadHandle->native)==target) object=rt.mainThreadHandle;
        else for(const auto& thread:rt.threads) if(thread->id==target) {object=thread->object;break;}
    }
    if(!target || !object || object->type!=Simpsons::KernelHandle::Type::Thread || GetThreadId(object->native)!=target)
        throw Simpsons::Failure("Thread-name notification does not identify an owned native thread");
    std::lock_guard lock(object->stateMutex);
    const HRESULT result=SetThreadDescription(object->native,description.c_str());
    if(FAILED(result)) {
        char message[128];std::snprintf(message,sizeof(message),"Native thread description failed: id=%u HRESULT=%08lX",target,ULONG(result));
        throw Simpsons::Failure(message);
    }
    PWSTR actual=nullptr;const HRESULT query=GetThreadDescription(object->native,&actual);
    const bool matches=SUCCEEDED(query) && actual && description==actual;
    if(actual) LocalFree(actual);
    if(!matches) throw Simpsons::Failure("Native thread description readback disagrees with the original name");
    std::fprintf(stderr,"[THREAD] original debug name applied: id=%u name=%s; native description read back\n",target,diagnostic.c_str());
    // RtlRaiseException returns void. Preserve the caller's guest registers and
    // exception record; only this recognized debugger notification completes.
}
