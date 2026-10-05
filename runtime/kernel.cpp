#include "runtime.h"

PPC_FUNC(__imp__KeQueryPerformanceFrequency) { ctx.r3.u64=Simpsons::Runtime::timebaseFrequency; }

namespace {
uint32_t xexHeaderField(PPCContext& ctx,uint8_t* base,uint32_t header,uint32_t key) {
    if(!header) return 0;
    if(PPC_LOAD_U32(header)!=0x58455832) PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"Invalid XEX header in RtlImageXexHeaderField");
    uint32_t size=PPC_LOAD_U32(header+8),count=PPC_LOAD_U32(header+20);
    if(size<24 || count>(size-24)/8) PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"Invalid XEX optional header bounds");
    for(uint32_t i=0;i<count;++i) {
        uint32_t entry=header+24+i*8;
        if(PPC_LOAD_U32(entry)!=key) continue;
        uint32_t value=PPC_LOAD_U32(entry+4);
        if((key&255)==0) return value;
        if((key&255)==1) return entry+4;
        if(value>=size) PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"XEX optional header offset outside header");
        return header+value;
    }
    return 0;
}
}

PPC_FUNC(__imp__XexCheckExecutablePrivilege) {
    uint32_t privilege=ctx.r3.u32;
    if(privilege>=32) PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"XEX privilege index outside system flags");
    uint32_t flags=xexHeaderField(ctx,base,Simpsons::active->headerAddress,0x30000);
    ctx.r3.u64=(flags>>privilege)&1;
}

PPC_FUNC(__imp__XamGetExecutionId) {
    struct HostState {
        const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
        HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
        ~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
    } host;
    if(!Simpsons::active||base!=Simpsons::active->base)throw Simpsons::Failure("Invalid native execution identity runtime");
    auto& rt=*Simpsons::active;rt.checkRunning();const uint32_t out=ctx.r3.u32,header=rt.headerAddress;
    if(!out||(out&3))throw Simpsons::Failure("Invalid execution identity pointer output");rt.pointer(out,4,true);
    rt.pointer(header,24,false);const auto size=PPC_LOAD_U32(header+8);
    if(size<24||size>0x10000)throw Simpsons::Failure("Execution identity header size exceeds loaded header bounds");
    rt.pointer(header,size,false);
    if(uint64_t(out)<uint64_t(header)+size&&uint64_t(header)<uint64_t(out)+4)
        throw Simpsons::Failure("Execution identity output aliases the loaded original header");
    // Runtime::load already verifies the original derived XEX hash and owns
    // these actual header bytes. Return its real24-byte optional-header field;
    // no fabricated execution record, new SDK object or replacement title ID.
    const auto identity=xexHeaderField(ctx,base,header,0x40006);
    if(identity<header||!identity||uint64_t(identity)-header+24>size)
        throw Simpsons::Failure("Execution identity is absent or outside the loaded XEX header");
    rt.pointer(identity,24,false);PPC_STORE_U32(out,identity);ctx.r3.u64=0;
    std::fprintf(stderr,"[NATIVE EXECUTION] original header=%08X identity=%08X title=%08X; loaded XEX bytes\n",header,identity,PPC_LOAD_U32(identity+12));
}

PPC_FUNC(__imp__KeGetCurrentProcessType) {
    uint32_t pcr=ctx.r13.u32;
    ctx.r3.u64=PPC_LOAD_U32(pcr+0x150)?PPC_LOAD_U8(pcr+0xc):PPC_LOAD_U8(PPC_LOAD_U32(pcr+0x100)+0x73);
}
PPC_FUNC(__imp__KeTlsAlloc) {
    auto& rt=*Simpsons::active;
    std::lock_guard lock(rt.vmMutex);
    for(uint32_t i=0;i<rt.tlsSlots.size();++i) if(!rt.tlsSlots[i]) {
        rt.tlsSlots[i]=true;
        for(uint32_t tls:rt.tlsBases) PPC_STORE_U32(tls+i*4,0);
        ctx.r3.u64=i; return;
    }
    ctx.r3.u64=0xffffffffu;
}
PPC_FUNC(__imp__KeTlsFree) {
    auto& rt=*Simpsons::active;
    std::lock_guard lock(rt.vmMutex);
    uint32_t slot=ctx.r3.u32;
    if(slot>=rt.tlsSlots.size() || !rt.tlsSlots[slot]) {ctx.r3.u64=0;return;}
    rt.tlsSlots[slot]=false;
    for(uint32_t tls:rt.tlsBases) PPC_STORE_U32(tls+slot*4,0);
    ctx.r3.u64=1;
}
PPC_FUNC(__imp__KeTlsGetValue) {
    auto& rt=*Simpsons::active; uint32_t slot=ctx.r3.u32;
    if(slot>=rt.tlsSlots.size()) PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"TLS index exceeds executable slot count");
    uint32_t thread=PPC_LOAD_U32(ctx.r13.u32+0x100), tls=PPC_LOAD_U32(thread+0x68);
    ctx.r3.u64=PPC_LOAD_U32(tls+slot*4);
}
PPC_FUNC(__imp__KeTlsSetValue) {
    auto& rt=*Simpsons::active; uint32_t slot=ctx.r3.u32,value=ctx.r4.u32;
    if(slot>=rt.tlsSlots.size()) PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"TLS index exceeds executable slot count");
    uint32_t thread=PPC_LOAD_U32(ctx.r13.u32+0x100), tls=PPC_LOAD_U32(thread+0x68);
    PPC_STORE_U32(tls+slot*4,value); ctx.r3.u64=1;
}

PPC_FUNC(__imp__NtAllocateVirtualMemory) {
    uint32_t addressPtr=ctx.r3.u32,sizePtr=ctx.r4.u32,flags=ctx.r5.u32,protect=ctx.r6.u32;
    if(!addressPtr || !sizePtr) { ctx.r3.u64=0xc000000d; return; }
    uint32_t address=PPC_LOAD_U32(addressPtr),size=PPC_LOAD_U32(sizePtr);
    PPCGuestPointer(base,addressPtr,4,true); PPCGuestPointer(base,sizePtr,4,true);
    fprintf(stderr,"[VM] allocate address=0x%08X size=0x%X flags=0x%08X protect=0x%X\n",address,size,flags,protect);
    uint32_t status=Simpsons::active->allocateVirtual(address,size,flags,protect);
    if(!status) { PPC_STORE_U32(addressPtr,address); PPC_STORE_U32(sizePtr,size); }
    ctx.r3.u64=status;
}

PPC_FUNC(__imp__MmQueryStatistics) {
    uint32_t address=ctx.r3.u32;
    if(!address) {ctx.r3.u64=0xc000000d;return;}
    if(PPC_LOAD_U32(address)!=104) {ctx.r3.u64=0xc0000023;return;}
    PPCGuestPointer(base,address,104,true);
    auto stats=Simpsons::active->memoryStatistics();
    for(uint32_t i=0;i<stats.size();++i) PPC_STORE_U32(address+i*4,stats[i]);
    ctx.r3.u64=0;
}

PPC_FUNC(__imp__RtlImageXexHeaderField) {
    ctx.r3.u64=xexHeaderField(ctx,base,ctx.r3.u32,ctx.r4.u32);
}
