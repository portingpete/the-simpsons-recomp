#include "runtime/runtime.h"
#include "runtime/guest_memory.h"
#include <cstdio>
#include <functional>
#include <bit>
#include "ppc_recomp_shared.h"
#include "ppc_image_metadata.h"
#include <thread>
#include <cstring>
extern "C" float roundevenf(float);
namespace Simpsons {
struct NativeMemoryContractProbe {
    static uint8_t* complete(Runtime& runtime,uint32_t address,unsigned width,bool write) {
        return runtime.pointerSlow(address,width,write);
    }
};
}
static void require(bool ok,const char* message) { if(!ok) throw Simpsons::Failure(message); }
static void rejects(std::function<void()> fn) {
    try { fn(); } catch(const Simpsons::Failure&) { return; }
    throw Simpsons::Failure("Expected invalid access to fail");
}
static std::atomic<uint32_t> workerArgument=0;
static PPC_FUNC(testAotWorker) {
    require(ctx.r13.u32!=0 && ctx.r1.u32!=0,"Worker lacks independent PCR/stack");
    uint32_t thread=PPC_LOAD_U32(ctx.r13.u32+0x100);
    require(PPC_LOAD_U32(thread+0x14c)==GetCurrentThreadId(),"Worker guest/native thread identity mismatch");
    require(PPC_LOAD_U32(PPC_LOAD_U32(ctx.r13.u32))==0x1234abcd,"Worker static TLS initialization mismatch");
    workerArgument=ctx.r3.u32;
    ctx.r3.u64=42;
}
static void memoryFastPathContracts() {
    Simpsons::Runtime rt;
    rt.map(0x10000,0x3000,true,"fast-path virtual fixture");
    for(uint32_t page:{0x20010000u,0x40010000u,0x60010000u,0x80010000u})
        rt.map(page,4096,true,"identity aperture fixture");
    rt.map(rt.threadObjectType&~4095u,4096,true,"opaque type fixture");
    rt.map(0x82000000,0x2000,true,"data import fixture");
    const auto physical=rt.allocatePhysical(0,8192,PAGE_READWRITE,0x100000,0x1FFFFF,0x10000);
    require(physical!=0,"Fast-path physical fixture allocation failed");
    const auto offset=rt.physicalAddress(physical);
    struct Result {uint8_t* pointer{};std::string failure;bool operator==(const Result&)const=default;};
    const auto outcome=[](auto&& call) {
        Result result;
        try{result.pointer=call();}catch(const Simpsons::Failure& error){result.failure=error.what();}
        return result;
    };
    const auto compare=[&](uint32_t address,unsigned width,bool write) {
        const auto nativeExpected=outcome([&]{return Simpsons::NativeMemoryContractProbe::complete(rt,address,width,write);});
        require(outcome([&]{return rt.pointer(address,width,write);})==nativeExpected,
            "Native memory path differs from complete mapping/alias/import validation");
        const auto guestExpected=outcome([&]{return PPCGuestPointerSlow(rt.base,address,width,write);});
        require(outcome([&]{return PPCGuestPointer(rt.base,address,width,write);})==guestExpected &&
            outcome([&]{return PPCCheckedGuestPointer(rt.base,address,width,write);})==guestExpected,
            "Guest memory path differs from complete demand-map/alias/import validation");
    };
    for(const auto& import:kDataImports)if(import.address&8)rt.boundImports.push_back(import.address);
    size_t cases=0;
    for(bool imports:{false,true}) {
        rt.checkingImports=imports;
        for(const auto page:{0x10000u,0x11000u,0x12000u,rt.threadObjectType&~4095u,0x82000000u,
                0x20010000u,0x40010000u,0x60010000u,0x80010000u,
                0xA0000000+offset,0xC0000000+offset,physical}) {
            const auto old=rt.pageAccess[page>>12].load();
            for(uint8_t access:{0,1,2,3}) {
                rt.pageAccess[page>>12]=access;
                for(const auto within:{0u,1u,0x3FFu,0x400u,0x43Fu,0x440u,0x600u,0x663u,0x664u,0x690u,0x7F0u,0x8FFu,0x900u,0xFEFu,0xFF0u,0xFFFu})
                    for(unsigned width:{0u,1u,2u,3u,4u,8u,15u,16u,17u,32u,64u,256u,1024u,4095u,4096u,4097u,0xFFFFFFFFu})for(bool write:{false,true}) {
                        compare(page+within,width,write);++cases;
                    }
            }
            rt.pageAccess[page>>12]=old;
        }
    }
    for(auto address:{0u,0xFFFFu,0x13000u,0xBFFFFFFFu,0xDFFFFFFFu,0xFFCFFFFFu,0xFFD00000u,0xFFFFFFFFu})
        for(unsigned width:{0u,1u,2u,4u,8u,16u,17u,32u,64u,256u,4095u,4096u,4097u,0xFFFFFFFFu})for(bool write:{false,true}){compare(address,width,write);++cases;}
    // Cancellation is polled at original function entry and in waits, not on each access:
    // the raised flag makes PPCStopNow (the PPC_TRACE_ENTRY poll) throw the stop reason.
    rt.requestStop("fast-path cancellation fixture");
    require(PPCStopRequested==1,"Stop request did not raise the function-entry flag");
    PPCContext entry{};
    require(outcome([&]{PPCStopNow(entry);return nullptr;}).failure==rt.stopReason,
        "Function-entry cancellation poll did not throw the stop reason");
    rt.stopping=false;PPCStopRequested=0;ResetEvent(rt.stopEvent);
    rt.freePhysical(physical);
    compare(physical,4,false);compare(0xC0000000+offset,4,true);
    std::printf("PASS: %zu differential memory paths and immediate cancellation/base/retirement checks\n",cases);
}
int main() {
    try {
        memoryFastPathContracts();
        Simpsons::Runtime rt;
        rt.map(0x10000,0x1000,true,"test RAM");
        auto* base=rt.base;
        PPC_STORE_U32(0x10004,0x12345678);
        require(base[0x10004]==0x12 && base[0x10007]==0x78,"Big-endian store mismatch");
        require(PPC_LOAD_U32(0x10004)==0x12345678,"Big-endian load mismatch");
        PPC_STORE_U64(0x10008,0x0123456789ABCDEFull);
        require(PPC_LOAD_U64(0x10008)==0x0123456789ABCDEFull,"64-bit endian mismatch");
        require(PPC_LOAD_U8(0x10008)==1,"Byte order mismatch");
        const auto scratchBudgetBefore=rt.memoryStatistics()[3];
        rejects([&]{(void)rt.pointer(0,4,false);});
        require(PPC_LOAD_U32(0)==0,"Guest null-device scratch did not demand-map zero-filled");
        require(rt.pointer(0,4,false)==base,"Native pointer did not observe guest demand-mapped scratch");
        PPC_STORE_U32(0,0xA1B2C3D4);
        require(PPC_LOAD_U32(0)==0xA1B2C3D4,"Guest scratch did not retain written data");
        require(rt.memoryStatistics()[3]==scratchBudgetBefore,"Scratch changed accounted guest memory budget");
        rejects([&]{(void)PPC_LOAD_U32(0x10fff);});
        rejects([&]{(void)PPC_LOAD_U64(0xfffffffc);});
        rejects([&]{PPC_MM_STORE_U32(0x7fc00000,1);});
        rt.map(0x20000,0x1000,false,"read-only");
        require(PPC_LOAD_U32(0x20000)==0,"Fresh pages should be zero");
        rejects([&]{PPC_STORE_U32(0x20000,42);});
        uint32_t vmAddress=0,vmSize=0x100000;
        require(rt.allocateVirtual(vmAddress,vmSize,0x60002000,PAGE_READWRITE)==0,"Virtual reserve failed");
        require(vmAddress==0x10000000 && vmSize==0x100000,"Reserve extent mismatch");
        rejects([&]{(void)PPC_LOAD_U32(vmAddress);});
        uint32_t commit=vmAddress,commitSize=4096;
        require(rt.allocateVirtual(commit,commitSize,0x60001000,PAGE_READWRITE)==0 && commitSize==65536,"Large-page commit failed");
        PPC_STORE_U32(commit,0x87654321);
        require(rt.allocateVirtual(commit,commitSize,0x60001000,PAGE_READWRITE)==0,"Recommit failed");
        require(PPC_LOAD_U32(commit)==0x87654321,"Recommit destroyed live contents");
        rejects([&]{(void)PPC_LOAD_U32(vmAddress+0x10000);});
        uint32_t overlap=vmAddress,overlapSize=4096;
        require(rt.allocateVirtual(overlap,overlapSize,0x2000,PAGE_READWRITE)==0xc0000018u,"Overlapping reserve should fail");
        PPCContext ctx{};
        auto memory=rt.memoryStatistics();
        require(memory[0]==104 && memory[1]==0x20000 && memory[3]==0x20000-18,"Memory budget/committed accounting mismatch");
        require(memory[5]==0x102000 && memory[11]==18,"Reserved and committed memory were confused");
        uint32_t large=0,largeSize=0x20000000,oldNext=rt.nextAllocation;
        size_t oldReservations=rt.allocations.size();
        require(rt.allocateVirtual(large,largeSize,0x3000,PAGE_READWRITE)==0xc0000017u,"Compatibility budget was not enforced");
        require(rt.allocations.size()==oldReservations && rt.nextAllocation==oldNext,"Failed reserve/commit leaked reservation");
        ctx.r3.u32=0x10300; PPC_STORE_U32(0x10300,100);
        __imp__MmQueryStatistics(ctx,base);
        require(ctx.r3.u32==0xc0000023 && PPC_LOAD_U32(0x10300)==100,"Invalid statistics size mutated buffer");
        ctx.r3.u32=0x10300; PPC_STORE_U32(0x10300,104);
        __imp__MmQueryStatistics(ctx,base);
        require(ctx.r3.u32==0 && PPC_LOAD_U32(0x1030c)==memory[3],"Guest statistics endian/layout mismatch");
        uint32_t physical=rt.allocatePhysical(0,4097,PAGE_READWRITE,0x100000,0x1fffff,0x10000);
        require(physical!=0,"Ranged physical allocation failed");
        uint32_t offset=rt.physicalAddress(physical);
        require(offset>=0x100000 && offset+8192<=0x200000 && !(offset&0xffff),"Physical range/alignment mismatch");
        PPC_STORE_U32(physical,0xa1b2c3d4);
        require(PPC_LOAD_U32(0xa0000000+offset)==0xa1b2c3d4 && PPC_LOAD_U32(0xc0000000+offset)==0xa1b2c3d4,"Physical aliases have different backing");
        auto withPhysical=rt.memoryStatistics();
        require(withPhysical[3]==memory[3]-2 && withPhysical[6]==2,"Physical memory charged more than once");
        rt.protectPhysical(physical+4095,2,PAGE_READONLY|0x400);
        require(rt.queryPhysicalProtect(physical)==0x402 && rt.queryPhysicalProtect(physical+4096)==0x402,"Straddling protection did not cover both pages");
        require(PPC_LOAD_U32(physical)==0xa1b2c3d4,"Protection change destroyed physical data");
        rejects([&]{PPC_STORE_U32(physical,1);});
        require(rt.queryPhysicalProtect(0xa0000000+offset)==PAGE_READWRITE && rt.queryPhysicalProtect(0xc0000000+offset)==PAGE_READWRITE,"Protection spread to sibling apertures");
        PPC_STORE_U32(0xc0000000+offset,0x11223344);
        require(PPC_LOAD_U32(physical)==0x11223344,"Readonly alias lost coherence with writable sibling");
        MEMORY_BASIC_INFORMATION hostProtection{};
        require(VirtualQuery(base+0xa0000000+offset,&hostProtection,sizeof(hostProtection)) && hostProtection.Protect==PAGE_READWRITE,"Shared host RAM no longer supports writable sibling alias");
        rt.protectPhysical(physical+4096,1,PAGE_NOACCESS);
        require(rt.queryPhysicalProtect(physical+4096)==PAGE_NOACCESS && rt.queryPhysicalProtect(0xa0000000+offset+4096)==PAGE_READWRITE,"Independent alias protection metadata mismatch");
        rejects([&]{(void)PPC_LOAD_U32(physical+4096);});
        rejects([&]{rt.protectPhysical(physical+4096,4097,PAGE_READWRITE);});
        require(rt.queryPhysicalProtect(physical+4096)==PAGE_NOACCESS,"Invalid protection range partially changed pages");
        rt.protectPhysical(physical,8192,PAGE_READWRITE|0x400);
        PPC_STORE_U32(physical+4096,0x10203040);
        require(PPC_LOAD_U32(0xc0000000+offset+4096)==0x10203040,"Restored physical write access failed");
        require(rt.memoryStatistics()[3]==withPhysical[3],"Protection changed physical allocation accounting");
        rt.freePhysical(physical);
        rejects([&]{(void)PPC_LOAD_U32(0xc0000000+offset);});
        require(rt.memoryStatistics()[3]==memory[3],"Physical free leaked budget");
        uint32_t readOnly=rt.allocatePhysical(0,4096,PAGE_READONLY,0x100000,0x1fffff,0);
        rejects([&]{PPC_STORE_U32(readOnly,1);});
        rt.freePhysical(readOnly);
        uint32_t largePhysical=rt.allocatePhysical(0,0x1000000,PAGE_READWRITE|0x80000000u,0x1000000,0x1ffffff,0);
        require(largePhysical!=0,"Large physical aperture fixture allocation failed");
        uint32_t largeOffset=rt.physicalAddress(largePhysical),aAlias=0xa0000000+largeOffset,eAlias=0xdffff000+largeOffset;
        rt.protectPhysical(largePhysical+1,1,PAGE_READONLY); // C aperture rounds to 16 MiB.
        require(rt.queryPhysicalProtect(largePhysical+0xffffff)==PAGE_READONLY && rt.queryPhysicalProtect(eAlias)==PAGE_READWRITE,"C aperture granularity or isolation mismatch");
        rt.protectPhysical(aAlias+1,1,PAGE_READONLY); // A aperture rounds to 64 KiB.
        require(rt.queryPhysicalProtect(aAlias+0xffff)==PAGE_READONLY && rt.queryPhysicalProtect(aAlias+0x10000)==PAGE_READWRITE,"A aperture did not use 64 KiB pages");
        rt.protectPhysical(eAlias,4096,PAGE_READONLY);
        require(VirtualQuery(base+aAlias,&hostProtection,sizeof(hostProtection)) && hostProtection.Protect==PAGE_READONLY,"Readonly union was not applied to native shared RAM");
        rt.protectPhysical(eAlias,4096,PAGE_READWRITE);
        PPC_STORE_U32(eAlias,0x88776655);
        require(PPC_LOAD_U32(aAlias)==0x88776655,"Native backing did not restore writable alias without breaking readonly siblings");
        rejects([&]{PPC_STORE_U32(aAlias,0);});
        rt.freePhysical(largePhysical);
        ctx.r3.u32=0x10400; ctx.r4.u32=0; ctx.r5.u32=1; ctx.r6.u32=2;
        __imp__NtCreateSemaphore(ctx,base);
        require(ctx.r3.u32==0,"Native semaphore creation failed");
        uint32_t semaphore=PPC_LOAD_U32(0x10400);
        PPC_STORE_U64(0x10410,0); // Poll timeout, in native 100 ns NT units.
        ctx.r3.u32=semaphore; ctx.r4.u32=1; ctx.r5.u32=0; ctx.r6.u32=0x10410;
        __imp__NtWaitForSingleObjectEx(ctx,base);
        require(ctx.r3.u32==0,"Semaphore did not consume initial signal");
        ctx.r3.u32=semaphore; __imp__NtWaitForSingleObjectEx(ctx,base);
        require(ctx.r3.u32==0x102,"Empty semaphore did not time out");
        ctx.r3.u32=semaphore; ctx.r4.u32=3; ctx.r5.u32=0;
        __imp__NtReleaseSemaphore(ctx,base);
        require(ctx.r3.u32==0xc0000047,"Semaphore allowed release beyond limit");
        ctx.r3.u32=semaphore; ctx.r4.u32=1; ctx.r5.u32=0x10418;
        __imp__NtReleaseSemaphore(ctx,base);
        require(ctx.r3.u32==0 && PPC_LOAD_U32(0x10418)==0,"Semaphore previous count mismatch");
        ctx.r3.u32=semaphore; __imp__NtClose(ctx,base);
        require(ctx.r3.u32==0,"Valid semaphore handle did not close");
        ctx.r3.u32=semaphore; __imp__NtClose(ctx,base);
        require(ctx.r3.u32==0xc0000008,"Stale semaphore handle accepted");
        ctx.r3.u32=0x10400; ctx.r4.u32=0; ctx.r5.u32=1;
        __imp__NtCreateTimer(ctx,base);
        require(ctx.r3.u32==0,"Native synchronization timer creation failed");
        uint32_t timer=PPC_LOAD_U32(0x10400);
        PPC_STORE_U64(0x10410,uint64_t(-10000ll));
        ctx.r3.u32=timer; ctx.r4.u32=0x10410; ctx.r5.u32=0; ctx.r6.u32=1;
        ctx.r7.u32=0; ctx.r8.u32=0; ctx.r9.u32=0; ctx.r10.u32=0;
        __imp__NtSetTimerEx(ctx,base);
        require(ctx.r3.u32==0,"Native timer setting failed");
        PPC_STORE_U64(0x10410,uint64_t(-10000000ll)); // One-second wait bound.
        ctx.r3.u32=timer; ctx.r4.u32=1; ctx.r5.u32=0; ctx.r6.u32=0x10410;
        __imp__NtWaitForSingleObjectEx(ctx,base);
        require(ctx.r3.u32==0,"Native timer did not signal");
        PPC_STORE_U64(0x10410,0);
        ctx.r3.u32=timer; __imp__NtWaitForSingleObjectEx(ctx,base);
        require(ctx.r3.u32==0x102,"Synchronization timer failed to consume its signal");
        ctx.r3.u32=timer; ctx.r4.u32=0x10418;
        __imp__NtCancelTimer(ctx,base);
        require(ctx.r3.u32==0 && PPC_LOAD_U32(0x10418)==0,"Timer cancellation state mismatch");
        ctx.r3.u32=timer; __imp__NtClose(ctx,base);
        rt.map(uint32_t(PPC_IMAGE_BASE+PPC_IMAGE_SIZE),(PPC_CODE_SIZE*2+0xfff)&~0xfffull,true,"test AOT dispatch",Simpsons::MemoryUse::Host);
        PPC_LOOKUP_FUNC(base,PPC_CODE_BASE)=testAotWorker;
        rt.map(kTls_raw_data_address&~0xfffu,0x1000,true,"test static TLS image",Simpsons::MemoryUse::Image);
        PPC_STORE_U32(kTls_raw_data_address,0x1234abcd);
        ctx.r3.u32=0x10400;ctx.r4.u32=0;ctx.r5.u32=0x10404;ctx.r6.u32=0;
        ctx.r7.u32=PPC_CODE_BASE;ctx.r8.u32=0x11223344;ctx.r9.u32=1;
        __imp__ExCreateThread(ctx,base);
        require(ctx.r3.u32==0,"Native suspended worker creation failed");
        uint32_t worker=PPC_LOAD_U32(0x10400);
        {
            const PPCContext saved=ctx;
            constexpr uint32_t record=0x10600,name=0x10680;
            const std::array<uint32_t,9> words={0x406D1388,0,0,0x82B76078,4,0x1000,name,PPC_LOAD_U32(0x10404),0};
            for(size_t i=0;i<words.size();++i) PPC_STORE_U32(record+uint32_t(i)*4,words[i]);
            std::memcpy(rt.pointer(name,32,true),"OriginalWorker",15);
            ctx.r3.u64=record;ctx.lr=0x82B760E8;
            __imp__RtlRaiseException(ctx,base);
            auto description=[&] {
                PWSTR text=nullptr;const HRESULT result=GetThreadDescription(rt.getHandle(worker)->native,&text);
                require(SUCCEEDED(result) && text,"Native thread name query failed");
                std::wstring out=text;LocalFree(text);return out;
            };
            require(description()==L"OriginalWorker" && workerArgument==0,"Thread naming failed or resumed the suspended worker");
            for(size_t i=0;i<words.size();++i)
                require(PPC_LOAD_U32(record+uint32_t(i)*4)==words[i],"Thread naming changed the original exception record");
            require(ctx.r3.u32==record && ctx.lr==0x82B760E8,"Void debug notification invented a return status");
            for(auto [offset,value]:std::array<std::pair<uint32_t,uint32_t>,8>{{
                {0,0xE06D7363},{4,1},{8,record},{12,0},{16,15},{20,0},{28,0},{32,1}}}) {
                PPC_STORE_U32(record+offset,value);
                rejects([&]{__imp__RtlRaiseException(ctx,base);});
                PPC_STORE_U32(record+offset,words[offset/4]);
                require(description()==L"OriginalWorker","Rejected exception changed a native thread name");
            }
            PPC_STORE_U8(name,0x80);rejects([&]{__imp__RtlRaiseException(ctx,base);});
            std::memset(rt.pointer(name,32,true),'X',32);rejects([&]{__imp__RtlRaiseException(ctx,base);});
            require(description()==L"OriginalWorker","Malformed name changed native thread ownership/name");
            ctx=saved;
        }
        ctx.r3.u32=worker;ctx.r4.u32=rt.threadObjectType+4;ctx.r5.u32=0x10420;
        __imp__ObReferenceObjectByHandle(ctx,base);
        require(ctx.r3.u32==0xc0000024 && rt.objectReferences.empty(),"Wrong object type was accepted");
        ctx.r3.u32=worker;ctx.r4.u32=rt.threadObjectType;
        __imp__ObReferenceObjectByHandle(ctx,base);
        require(ctx.r3.u32==0,"Typed native thread reference failed");
        uint32_t workerObject=PPC_LOAD_U32(0x10420);
        ctx.r3.u32=workerObject;ctx.r4.u32=2;ctx.r5.u32=0x10424;
        __imp__KeSetAffinityThread(ctx,base);
        require(ctx.r3.u32==0 && PPC_LOAD_U32(0x10424)==1,"Affinity did not return the previous logical processor");
        uint32_t workerPcr=PPC_LOAD_U32(workerObject+0xc0)-0x100;
        require(PPC_LOAD_U8(workerObject+0xbf)==1 && PPC_LOAD_U8(workerPcr+0x10c)==1 && PPC_LOAD_U32(workerPcr+0x110)==2,"PCR/KTHREAD processor fields disagree");
        ctx.r3.u32=workerObject;ctx.r4.u32=1;__imp__KeSetAffinityThread(ctx,base);
        require(ctx.r3.u32==0 && PPC_LOAD_U32(0x10424)==2,"Affinity restore did not retain prior assignment");
        ctx.r3.u32=workerObject;ctx.r4.u32=0;__imp__KeSetAffinityThread(ctx,base);
        require(ctx.r3.u32==0xc000000d && PPC_LOAD_U32(workerPcr+0x110)==1,"Invalid affinity changed the processor");
        ctx.r3.u32=workerObject;ctx.r4.u32=1;
        __imp__KeSetBasePriorityThread(ctx,base);
        require(ctx.r3.s32==0,"Native priority change lost previous increment");
        ctx.r3.u32=workerObject;__imp__KeQueryBasePriorityThread(ctx,base);
        require(ctx.r3.s32==1,"Native thread priority was not applied");
        ctx.r3.u32=workerObject;ctx.r4.u32=10;__imp__KeSetBasePriorityThread(ctx,base);
        require(ctx.r3.s32==1 && GetThreadPriority(rt.getHandle(worker)->native)==THREAD_PRIORITY_TIME_CRITICAL,"Clamped high priority was not applied to the native worker");
        ctx.r3.u32=workerObject;__imp__KeQueryBasePriorityThread(ctx,base);
        require(ctx.r3.s32==7 && PPC_LOAD_U8(workerObject+0x70)==15,"Nonsaturated high priority lost the effective increment");
        ctx.r3.u32=workerObject;ctx.r4.u32=16;__imp__KeSetBasePriorityThread(ctx,base);
        require(ctx.r3.s32==7,"High priority previous increment mismatch");
        ctx.r3.u32=workerObject;__imp__KeQueryBasePriorityThread(ctx,base);
        require(ctx.r3.s32==16,"Saturated priority query lost saturation state");
        ctx.r3.u32=workerObject;ctx.r4.u32=-10;__imp__KeSetBasePriorityThread(ctx,base);
        require(ctx.r3.s32==16 && GetThreadPriority(rt.getHandle(worker)->native)==THREAD_PRIORITY_IDLE,"Clamped low priority was not applied to native worker");
        ctx.r3.u32=workerObject;__imp__KeQueryBasePriorityThread(ctx,base);
        require(ctx.r3.s32==-7 && PPC_LOAD_U8(workerObject+0x70)==1,"Nonsaturated low priority increment mismatch");
        ctx.r3.u32=workerObject;ctx.r4.u32=0;__imp__KeSetBasePriorityThread(ctx,base);
        ctx.r3.u32=worker;ctx.r4.u32=1;ctx.r5.u32=0;ctx.r6.u32=0x10410;
        __imp__NtWaitForSingleObjectEx(ctx,base);
        require(ctx.r3.u32==0x102 && workerArgument==0,"Suspended worker executed prematurely");
        ctx.r3.u32=worker;ctx.r4.u32=0x10418;
        __imp__NtResumeThread(ctx,base);
        require(ctx.r3.u32==0 && PPC_LOAD_U32(0x10418)==1,"Worker resume count mismatch");
        PPC_STORE_U64(0x10410,uint64_t(-10000000ll));
        ctx.r3.u32=worker;ctx.r4.u32=1;ctx.r5.u32=0;ctx.r6.u32=0x10410;
        __imp__NtWaitForSingleObjectEx(ctx,base);
        require(ctx.r3.u32==0 && workerArgument==0x11223344,"AOT worker argument/execution mismatch");
        DWORD exitCode=0;
        require(GetExitCodeThread(rt.getHandle(worker)->native,&exitCode) && exitCode==42,"Native worker exit code mismatch");
        ctx.r3.u32=worker;__imp__NtClose(ctx,base);
        require(rt.objectReferences.at(workerObject).count==1,"Closing a handle dropped an explicit object reference");
        ctx.r3.u32=workerObject;__imp__ObDereferenceObject(ctx,base);
        require(rt.objectReferences.empty(),"Balanced thread reference was retained");
        rt.headerAddress=0x10200;
        PPC_STORE_U32(rt.headerAddress,0x58455832);
        PPC_STORE_U32(rt.headerAddress+8,0x100);
        PPC_STORE_U32(rt.headerAddress+20,1);
        PPC_STORE_U32(rt.headerAddress+24,0x30000);
        PPC_STORE_U32(rt.headerAddress+28,0x2200);
        ctx.r3.u32=10; __imp__XexCheckExecutablePrivilege(ctx,base);
        require(ctx.r3.u32==0,"Absent executable privilege was granted");
        ctx.r3.u32=9; __imp__XexCheckExecutablePrivilege(ctx,base);
        require(ctx.r3.u32==1,"Present executable privilege was denied");
        ctx.r3.u32=13; __imp__XexCheckExecutablePrivilege(ctx,base);
        require(ctx.r3.u32==1,"Second executable privilege was denied");
        rejects([&]{PPC_CALL_INDIRECT_FUNC(0x1234);});
        require(PPCQueryTimebase()>0,"Timebase is not advancing");
        FILETIME timeBefore{},timeAfter{};
        GetSystemTimePreciseAsFileTime(&timeBefore);
        ctx.r3.u32=0x10430;__imp__KeQuerySystemTime(ctx,base);
        GetSystemTimePreciseAsFileTime(&timeAfter);
        uint64_t wall=PPC_LOAD_U64(0x10430);
        require(wall>=((uint64_t(timeBefore.dwHighDateTime)<<32)|timeBefore.dwLowDateTime) && wall<=((uint64_t(timeAfter.dwHighDateTime)<<32)|timeAfter.dwLowDateTime),"Guest system time epoch, units or byte order disagree with native UTC time");
        ctx.r3.u32=0xfffffffcu;rejects([&]{__imp__KeQuerySystemTime(ctx,base);});
        PPC_STORE_U64(0x10410,0);
        ctx.r3.u32=1;ctx.r4.u32=1;ctx.r5.u32=0x10410;
        __imp__KeDelayExecutionThread(ctx,base);
        require(ctx.r3.u32==0,"Alertable zero-duration scheduler delay failed");
        PPC_STORE_U64(0x10410,uint64_t(-10000ll));
        ctx.r3.u32=1;ctx.r4.u32=0;ctx.r5.u32=0x10410;
        __imp__KeDelayExecutionThread(ctx,base);
        require(ctx.r3.u32==0,"Relative native delay did not report successful expiry");
        for(unsigned i=0;i<32;++i) {
            PPCContext expected;std::memcpy(&expected,&ctx,sizeof(ctx));
            const DWORD lastError=0x1234ABCD;SetLastError(lastError);
            const auto fp=PPCFPSCRRegister::getcsr();
            __imp__NtYieldExecution(ctx,base);
            require(ctx.r3.u64==0 || ctx.r3.u64==0x40000024,
                    "Native yield did not preserve a valid real NT scheduler result");
            expected.r3.u64=ctx.r3.u64;
            require(!std::memcmp(&ctx,&expected,sizeof(ctx)) && GetLastError()==lastError && PPCFPSCRRegister::getcsr()==fp,
                    "Native yield changed another register, host last-error or floating-point state");
        }
        rt.map(0x30000,0x1000,true,"test PCR records");
        PPC_STORE_U32(0x30100,0x30800);
        PPC_STORE_U32(0x30500,0x30900);
        PPCContext a{},b{}; a.r13.u32=0x30000; b.r13.u32=0x30400;
        const uint32_t cs=0x10100;
        a.r3.u32=cs; __imp__RtlInitializeCriticalSection(a,base);
        require(a.r3.u64==0,"Critical-section initialization did not return its zero status");
        // Initialization returns status in r3; reload the section argument.
        a.r3.u32=cs; __imp__RtlEnterCriticalSection(a,base);
        __imp__RtlEnterCriticalSection(a,base);
        require(PPC_LOAD_U32(cs+20)==2 && PPC_LOAD_U32(cs+24)==0x30800,"Recursive lock metadata mismatch");
        b.r3.u32=cs; __imp__RtlTryEnterCriticalSection(b,base);
        require(b.r3.u32==0,"Another thread stole an owned critical section");
        a.r3.u32=cs; __imp__RtlLeaveCriticalSection(a,base);
        std::atomic<bool> entered=false,started=false;
        std::thread lockWorker([&]{b.r3.u32=cs; started=true; __imp__RtlEnterCriticalSection(b,base); entered=true; __imp__RtlLeaveCriticalSection(b,base);});
        while(!started.load()) std::this_thread::yield();
        require(!entered.load(),"Contending thread acquired before final release");
        __imp__RtlLeaveCriticalSection(a,base);
        lockWorker.join();
        require(entered.load() && PPC_LOAD_U32(cs+16)==0xffffffffu,"Contended lock did not restore free state");
        // Odd/even ties, negative zero, non-ties and already integral large values.
        volatile float roundInputs[]={0.5f,-0.5f,1.5f,2.5f,-2.5f,3.6f,8388608.0f};
        const uint32_t expected[]={0,0x80000000u,0x40000000u,0x40000000u,0xc0000000u,0x40800000u,0x4b000000u};
        for(unsigned i=0;i<7;++i) require(std::bit_cast<uint32_t>(roundevenf(roundInputs[i]))==expected[i],"Round-to-even mismatch");
        ctx.r3.u32=0x10400;ctx.r4.u32=0;ctx.r5.u32=0;ctx.r6.u32=0;
        ctx.r7.u32=PPC_CODE_BASE;ctx.r8.u32=0x55667788;ctx.r9.u32=1;
        __imp__ExCreateThread(ctx,base);
        require(ctx.r3.u32==0,"Teardown fixture worker creation failed");
        uint32_t suspended=PPC_LOAD_U32(0x10400);
        rt.stopThreads();
        require(WaitForSingleObject(rt.getHandle(suspended)->native,1000)==WAIT_OBJECT_0,"Shutdown stranded a suspended native thread");
        require(workerArgument==0x11223344,"Cancelled suspended worker executed guest code");
        puts("PASS: endian, boundaries, overflow, unmapped MMIO, write protection, invalid indirect call");
        return 0;
    } catch(const std::exception& e) { fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
