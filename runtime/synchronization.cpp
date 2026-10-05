#include "runtime.h"
#include <algorithm>
#include <chrono>

namespace {
std::shared_ptr<Simpsons::CriticalSection> getSection(uint8_t* base,uint32_t address,bool initialize,uint32_t spin=0) {
    if(address&3) throw Simpsons::Failure("Unaligned critical section");
    PPCGuestPointer(base,address,28,true);
    auto& rt=*Simpsons::active;
    std::lock_guard guard(rt.criticalMutex);
    auto found=rt.criticalSections.find(address);
    if(found!=rt.criticalSections.end()) {
        if(initialize) {
            std::lock_guard lock(found->second->mutex);
            if(found->second->owner || found->second->waiters) throw Simpsons::Failure("Reinitializing an active critical section");
            memset(PPCGuestPointer(base,address,28,true),0,28);
            PPC_STORE_U8(address,1);
            PPC_STORE_U8(address+1,std::min<uint64_t>((uint64_t(spin)+255)/256,255));
            PPC_STORE_U32(address+16,0xffffffffu);
        }
        return found->second;
    }
    if(!initialize && (PPC_LOAD_U8(address)!=1 || PPC_LOAD_U32(address+16)!=0xffffffffu ||
                      PPC_LOAD_U32(address+20)!=0 || PPC_LOAD_U32(address+24)!=0))
        throw Simpsons::Failure("Critical section lacks a valid original/static initialization");
    if(initialize) {
        memset(PPCGuestPointer(base,address,28,true),0,28);
        PPC_STORE_U8(address,1);
        PPC_STORE_U8(address+1,std::min<uint64_t>((uint64_t(spin)+255)/256,255));
        PPC_STORE_U32(address+16,0xffffffffu);
    }
    auto section=std::make_shared<Simpsons::CriticalSection>();
    rt.criticalSections.emplace(address,section);
    return section;
}
uint32_t currentThread(PPCContext& ctx,uint8_t* base) {
    uint32_t thread=PPC_LOAD_U32(ctx.r13.u32+0x100);
    if(!thread) throw Simpsons::Failure("Critical section has no current guest thread");
    return thread;
}
void publish(uint8_t* base,uint32_t address,const Simpsons::CriticalSection& section) {
    // NT/Xbox RTL_CRITICAL_SECTION: LockCount counts outstanding entries minus
    // one, so a free section with no waiters is -1, as initialization writes.
    // An unowned section always has zero recursion.
    const uint32_t lockCount=section.recursion+section.waiters-1;
    PPC_STORE_U32(address+16,lockCount);
    PPC_STORE_U32(address+20,section.recursion);
    PPC_STORE_U32(address+24,section.owner);
}
bool acquire(PPCContext& ctx,uint8_t* base,bool onlyTry) {
    uint32_t address=ctx.r3.u32,thread=currentThread(ctx,base);
    auto section=getSection(base,address,false);
    std::unique_lock lock(section->mutex);
    if(PPC_LOAD_U32(address+20)!=section->recursion || PPC_LOAD_U32(address+24)!=section->owner)
        throw Simpsons::Failure("Guest modified native critical-section ownership outside its service boundary");
    if(section->owner && section->owner!=thread) {
        if(onlyTry) return false;
        ++section->waiters;
        publish(base,address,*section);
        try {
            while(section->owner!=0) {
                (void)section->changed.wait_for(lock,std::chrono::milliseconds(100),[&]{return section->owner==0 || !Simpsons::active || Simpsons::active->stopping;});
                if(!Simpsons::active) throw Simpsons::Failure("Runtime shutdown during critical-section wait");
                Simpsons::active->checkRunning();
            }
        } catch(...) {--section->waiters;publish(base,address,*section);throw;}
        --section->waiters;
    }
    section->owner=thread;
    ++section->recursion;
    publish(base,address,*section);
    return true;
}
}
PPC_FUNC(__imp__RtlInitializeCriticalSection) { getSection(base,ctx.r3.u32,true); ctx.r3.u64=0; }
PPC_FUNC(__imp__RtlInitializeCriticalSectionAndSpinCount) { getSection(base,ctx.r3.u32,true,ctx.r4.u32); ctx.r3.u64=0; }
PPC_FUNC(__imp__RtlEnterCriticalSection) { acquire(ctx,base,false); }
PPC_FUNC(__imp__RtlTryEnterCriticalSection) { ctx.r3.u64=acquire(ctx,base,true); }
PPC_FUNC(__imp__RtlLeaveCriticalSection) {
    uint32_t address=ctx.r3.u32,thread=currentThread(ctx,base);
    auto section=getSection(base,address,false);
    std::lock_guard lock(section->mutex);
    if(section->owner!=thread || !section->recursion) throw Simpsons::Failure("Critical section released by a non-owner");
    if(--section->recursion==0) section->owner=0;
    publish(base,address,*section);
    if(!section->owner && section->waiters) section->changed.notify_one();
}
