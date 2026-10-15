#include "runtime/runtime.h"
#include "ppc_recomp_shared.h"
// External leaf helpers form the exact pre-optimization hardware-fast oracle,
// including its write-watch action before a faulting store. Checked native
// pointers intentionally validate permissions before invalidating the watch.
uint8_t* PPCGuestPointerRead(uint8_t*,uint32_t,unsigned);
uint8_t* PPCGuestPointerWrite(uint8_t*,uint32_t,unsigned);
uint8_t* PPCGuestPointerFast(uint8_t*,uint32_t,unsigned,bool);
inline uint32_t PPCExternalFastLoad32(uint8_t* base,uint32_t address) {
#if defined(SIMPSONS_FAST_GUEST_MEMORY) && SIMPSONS_FAST_GUEST_MEMORY
    return __builtin_bswap32(*(volatile uint32_t*)PPCGuestPointerRead(base,address,4));
#else
    return PPCLoadU32(base,address);
#endif
}
inline void PPCExternalFastStore32(uint8_t* base,uint32_t address,uint32_t value) {
#if defined(SIMPSONS_FAST_GUEST_MEMORY) && SIMPSONS_FAST_GUEST_MEMORY
    *(volatile uint32_t*)PPCGuestPointerWrite(base,address,4)=__builtin_bswap32(value);
#else
    PPCStoreU32(base,address,value);
#endif
}
#define PPC_EXTERNAL_FAST_LOAD_U32(x) PPCExternalFastLoad32(base,uint32_t(x))
#define PPC_EXTERNAL_FAST_STORE_U32(x,y) PPCExternalFastStore32(base,uint32_t(x),uint32_t(y))
inline uint8_t PPCExternalFastLoad8(uint8_t* base,uint32_t address) {
#if defined(SIMPSONS_FAST_GUEST_MEMORY) && SIMPSONS_FAST_GUEST_MEMORY
    return *(volatile uint8_t*)PPCGuestPointerRead(base,address,1);
#else
    return PPCLoadU8(base,address);
#endif
}
inline void PPCExternalFastStore8(uint8_t* base,uint32_t address,uint8_t value) {
#if defined(SIMPSONS_FAST_GUEST_MEMORY) && SIMPSONS_FAST_GUEST_MEMORY
    *(volatile uint8_t*)PPCGuestPointerWrite(base,address,1)=value;
#else
    *(volatile uint8_t*)PPC_GUEST_WRITE(base,address,1)=value;
#endif
}
#define PPC_EXTERNAL_FAST_LOAD_U8(x) PPCExternalFastLoad8(base,uint32_t(x))
#define PPC_EXTERNAL_FAST_STORE_U8(x,y) PPCExternalFastStore8(base,uint32_t(x),uint8_t(y))
inline uint16_t PPCExternalFastLoad16(uint8_t* base,uint32_t address) {
#if defined(SIMPSONS_FAST_GUEST_MEMORY) && SIMPSONS_FAST_GUEST_MEMORY
    return __builtin_bswap16(*(volatile uint16_t*)PPCGuestPointerRead(base,address,2));
#else
    return PPCLoadU16(base,address);
#endif
}
inline void PPCExternalFastStore16(uint8_t* base,uint32_t address,uint16_t value) {
#if defined(SIMPSONS_FAST_GUEST_MEMORY) && SIMPSONS_FAST_GUEST_MEMORY
    *(volatile uint16_t*)PPCGuestPointerWrite(base,address,2)=__builtin_bswap16(value);
#else
    *(volatile uint16_t*)PPC_GUEST_WRITE(base,address,2)=__builtin_bswap16(value);
#endif
}
#define PPC_EXTERNAL_FAST_LOAD_U16(x) PPCExternalFastLoad16(base,uint32_t(x))
#define PPC_EXTERNAL_FAST_STORE_U16(x,y) PPCExternalFastStore16(base,uint32_t(x),uint16_t(y))
#include "inline_memory_reference.h" // Parsed before the optimized access macros.
#include "runtime/aot_inline_memory.h"
#include "runtime/aot_inline_fast_memory.h"
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <span>
#include <string>
#include <type_traits>
#include <tuple>
#include <vector>

namespace {
size_t checks{};
void need(bool ok,const char* why){++checks;if(!ok)throw Simpsons::Failure(why);}
uint32_t randomWord(uint32_t& state){state=state*1664525u+1013904223u;return state;}
struct Outcome {std::string failure;uint32_t csr{};bool operator==(const Outcome&)const=default;};
using Function=void(*)(PPCContext&,uint8_t*);
// The recompiled original code relies on the CPU's page protection, so its memory
// faults carry the faulting address but not the access width, and an unmapped aperture
// or device address is reported as a plain fault. The contract compared here is that
// the same accesses fail in the same order with the same register, memory and FP state.
uint8_t fixtureAccess=3;
void protectFixturePage(Simpsons::Runtime& rt,uint8_t access) {
    DWORD previous{};
    const DWORD protect=access==0?PAGE_NOACCESS:(access&2)?PAGE_READWRITE:PAGE_READONLY;
    if(!VirtualProtect(rt.base+0x14000,4096,protect,&previous))throw Simpsons::Failure("fixture page protection change failed");
}
// Page 0x14 is the fixture's protected page: the CPU enforces it for every access, so
// the harness itself opens it around its bulk copies of the 0x5000-byte window.
void setGuestPage(Simpsons::Runtime& rt,uint32_t page,uint8_t access) {
    fixtureAccess=access;protectFixturePage(rt,access);
    rt.pageAccess[page]=access;
}
template<class Body>auto withOpenFixture(Simpsons::Runtime& rt,Body&& body) {
    protectFixturePage(rt,3);
    struct Restore {Simpsons::Runtime& rt;~Restore(){protectFixturePage(rt,fixtureAccess);}} restore{rt};
    return body();
}
Outcome run(Function function,PPCContext& ctx,uint8_t* base) {
    PPCGuestFloatingPointScope fp(ctx.fpscr);
    Outcome out;
    try{function(ctx,base);}catch(const Simpsons::Failure& error){
        out.failure=error.what();
        if(out.failure.rfind("Unmapped",0)==0)out.failure="guest memory fault";
    }
    out.csr=PPCFPSCRRegister::getcsr();return out;
}
void matrixContract(Simpsons::Runtime& rt,Function referenceFunction,Function actualFunction) {
    static_assert(std::is_trivially_copyable_v<PPCContext>);
    constexpr uint32_t start=0x10000,size=0x5000;
    std::array<uint8_t,size> initialMemory{},referenceMemory{};
    uint32_t seed=0x6237A91D;
    constexpr std::array<uint32_t,12> special={0,0x80000000,1,0x007FFFFF,0x00800000,0x3F800000,
        0xBF800000,0x7F7FFFFF,0x7F800000,0xFF800000,0x7FC12345,0x7FA6789A};
    const auto compare=[&](uint32_t output,uint32_t point,uint32_t matrix,uint32_t csr,bool badBase=false) {
        PPCContext initial{};initial.r3.u32=output;initial.r4.u32=point;initial.r5.u32=matrix;
        initial.r1.u64=0x1234567800014800;initial.r6.u64=0xFEDCBA9876543210;initial.lr=0xABCDDCBA827F4B70;
        initial.ctr.u64=0xABCDEF7654321011;initial.traceIndex=61;initial.fpscr.csr=csr;
        initial.f0.u64=0x9876543210ABCDEF;initial.f14.u64=0x123456789ABCDEF0;
        PPCContext reference{},actual{};
        std::memcpy(&reference,&initial,sizeof(initial));std::memcpy(&actual,&initial,sizeof(initial));
        withOpenFixture(rt,[&]{std::memcpy(rt.base+start,initialMemory.data(),size);});
        const auto expected=run(referenceFunction,reference,rt.base+(badBase?32:0));
        withOpenFixture(rt,[&]{std::memcpy(referenceMemory.data(),rt.base+start,size);});
        withOpenFixture(rt,[&]{std::memcpy(rt.base+start,initialMemory.data(),size);});
        const auto result=run(actualFunction,actual,rt.base+(badBase?32:0));
        need(result==expected,"Selected AOT memory changed failure or native FP status");
        need(!std::memcmp(&actual,&reference,sizeof(actual)),"Selected AOT memory changed original registers/trace/FPSCR");
        need(withOpenFixture(rt,[&]{return !std::memcmp(rt.base+start,referenceMemory.data(),size);}),"Selected AOT memory changed aliasing or partial-write order");
    };
    for(uint32_t round=0;round<4;++round)for(bool flush:{false,true})for(uint32_t sample=0;sample<256;++sample) {
        for(size_t i=0;i<size;i+=4) {
            const uint32_t word=randomWord(seed);
            initialMemory[i]=uint8_t(word>>24);initialMemory[i+1]=uint8_t(word>>16);
            initialMemory[i+2]=uint8_t(word>>8);initialMemory[i+3]=uint8_t(word);
        }
        for(size_t i=0;i<16;++i) {
            const uint32_t word=sample<special.size()?special[(i+sample)%special.size()]:randomWord(seed);
            const size_t at=0x400+4*i;
            initialMemory[at]=uint8_t(word>>24);initialMemory[at+1]=uint8_t(word>>16);
            initialMemory[at+2]=uint8_t(word>>8);initialMemory[at+3]=uint8_t(word);
        }
        const uint32_t csr=PPCFPSCRRegister::DefaultCSR|(round<<PPCFPSCRRegister::RoundShift)|
            (flush?PPCFPSCRRegister::FlushMask:0);
        for(uint32_t output:{0x10800u,0x10430u,0x10434u,0x10438u,0x10200u,0x10204u})
            compare(output,0x10200,0x10400,csr);
        compare(0x10414,0x10410,0x10400,csr);
        compare(0x10420+4*(sample%8),0x10400+4*(sample%12),0x10400,csr);
    }
    const auto csr=PPCFPSCRRegister::DefaultCSR;
    for(uint8_t access:{uint8_t(0),uint8_t(1)}) {
        setGuestPage(rt,0x14,access);
        compare(0x13FF8,0x10200,0x10400,csr); // Two completed stores before a third-page failure.
        compare(0x10800,0x13FFC,0x10400,csr); // A later point load crosses the page.
        compare(0x10800,0x10200,0x13FF0,csr); // A later matrix load crosses the page.
        // Composition reaches the later rows after earlier stores. Preserve
        // that exact fault order, including when the point routine needs no
        // inaccessible row and therefore succeeds on the same addresses.
        compare(0x13FDC,0x10200,0x10400,csr);
        compare(0x10800,0x13FC8,0x10400,csr);
        compare(0x10800,0x10200,0x13FC8,csr);
    }
    setGuestPage(rt,0x14,3);
    compare(0x10800,0,0x10400,csr);compare(0xFFD00000,0x10200,0x10400,csr);
    // Cancellation is polled at every original function entry, not per memory access.
    rt.requestStop("inline memory cancellation");
    compare(0x10800,0x10200,0x10400,csr);
    rt.stopping=false;PPCStopRequested=0;ResetEvent(rt.stopEvent);
}
void runtimeRoutinesContract(Simpsons::Runtime& rt) {
    constexpr uint32_t start=0x10000,size=0x5000;
    std::array<uint8_t,size> memory{},expectedMemory{};
    for(size_t i=0;i<size;++i)memory[i]=uint8_t(i*71+19);
    const auto compare=[&](Function referenceFunction,Function actualFunction,uint32_t destination,
                           uint32_t source,uint32_t count,uint32_t stack=0x13F80) {
        PPCContext initial{};initial.r1.u32=stack;initial.r3.u32=destination;
        initial.r4.u32=source;initial.r5.u32=count;initial.r12.u64=0x12345678ABCDEF98;
        initial.r28.u64=0x0192837465ABCDEF;initial.r29.u64=0xFEDCBA9876543210;
        initial.r30.u64=0x8000000012345678;initial.r31.u64=0x7FFFABCD97531864;
        initial.lr=0x98765432ABCDCBA0;initial.traceIndex=63;
        PPCContext reference{},actual{};
        std::memcpy(&reference,&initial,sizeof(initial));std::memcpy(&actual,&initial,sizeof(initial));
        withOpenFixture(rt,[&]{std::memcpy(rt.base+start,memory.data(),size);});
        const auto expected=run(referenceFunction,reference,rt.base);
        withOpenFixture(rt,[&]{std::memcpy(expectedMemory.data(),rt.base+start,size);});
        withOpenFixture(rt,[&]{std::memcpy(rt.base+start,memory.data(),size);});
        const auto result=run(actualFunction,actual,rt.base);
        need(result==expected,"Original copy/save/restore failure changed");
        need(!std::memcmp(&reference,&actual,sizeof(actual)),"Original copy/save/restore registers changed");
        need(withOpenFixture(rt,[&]{return !std::memcmp(expectedMemory.data(),rt.base+start,size);}),"Original copy/save/restore bytes or fault order changed");
    };
    for(uint32_t count:{0u,1u,2u,3u,4u,7u,8u,9u,15u,16u,17u,28u,31u,32u,33u,63u,64u,65u,
                       84u,112u,127u,128u,129u,255u,256u,257u,1023u,1024u,4095u,4096u})
        for(uint32_t source=0;source<8;++source)for(uint32_t destination=0;destination<8;++destination)
            compare(referenceCopy,sub_82A3CD80,0x11800+destination,0x10400+source,count);
    // Compare the original routine's actual overlap behavior; do not substitute
    // a host memcpy/memmove or infer their standard-library overlap contract.
    for(int delta:{-64,-32,-8,-4,-1,0,1,4,8,32,64})
        for(uint32_t count:{0u,1u,3u,4u,7u,8u,31u,32u,63u,64u,65u,84u,112u,256u})
            compare(referenceCopy,sub_82A3CD80,uint32_t(0x11000+delta),0x11000,count);
    for(uint32_t offset=0;offset<16;++offset) {
        compare(referenceSave28,__savegprlr_28,0,0,0,0x13000+offset);
        compare(referenceRestore28,__restgprlr_28,0,0,0,0x13000+offset);
    }
    for(uint8_t access:{uint8_t(0),uint8_t(1)}) {
        setGuestPage(rt,0x14,access);
        compare(referenceCopy,sub_82A3CD80,0x13FF8,0x10400,112);
        compare(referenceCopy,sub_82A3CD80,0x10800,0x13FFC,112);
        compare(referenceSave28,__savegprlr_28,0,0,0,0x14008);
        compare(referenceRestore28,__restgprlr_28,0,0,0,0x14008);
    }
    setGuestPage(rt,0x14,3);
}
void vectorCopyContract(Simpsons::Runtime& rt) {
    constexpr uint32_t start=0x10000,size=0x5000;
    std::array<uint8_t,size> memory{},expectedMemory{};
    for(size_t i=0;i<size;++i)memory[i]=uint8_t(i*71+19);
    const auto compare=[&](uint32_t destination,uint32_t source,uint32_t count,uint32_t stack=0x13F80) {
        PPCContext initial{};initial.r1.u32=stack;initial.r3.u32=destination;
        initial.r4.u32=source;initial.r5.u32=count;initial.r12.u64=0x12345678ABCDEF98;
        initial.r26.u64=0x1122334455667788;initial.r27.u64=0x99AABBCCDDEEFF00;
        initial.r28.u64=0x0192837465ABCDEF;initial.r29.u64=0xFEDCBA9876543210;
        initial.r30.u64=0x8000000012345678;initial.r31.u64=0x7FFFABCD97531864;
        initial.lr=0x98765432ABCDCBA0;initial.traceIndex=63;
        initial.v0.u64[0]=0x0123456789ABCDEF;initial.v0.u64[1]=0xFEDCBA9876543210;
        initial.v7.u64[0]=0x1111111122222222;initial.v7.u64[1]=0x3333333344444444;
        initial.vscr_sat=0x5A;
        PPCContext reference{},actual{};
        std::memcpy(&reference,&initial,sizeof(initial));std::memcpy(&actual,&initial,sizeof(initial));
        withOpenFixture(rt,[&]{std::memcpy(rt.base+start,memory.data(),size);});
        const auto expected=run(referenceVectorCopy,reference,rt.base);
        withOpenFixture(rt,[&]{std::memcpy(expectedMemory.data(),rt.base+start,size);});
        withOpenFixture(rt,[&]{std::memcpy(rt.base+start,memory.data(),size);});
        const auto result=run(sub_82B74110,actual,rt.base);
        need(result==expected,"Vector copy failure or FPSCR changed");
        need(!std::memcmp(&reference,&actual,sizeof(actual)),"Vector copy registers changed");
        need(withOpenFixture(rt,[&]{return !std::memcmp(expectedMemory.data(),rt.base+start,size);}),"Vector copy bytes or fault order changed");
    };
    for(uint32_t count:{0u,1u,2u,3u,4u,7u,8u,9u,15u,16u,17u,31u,32u,33u,63u,64u,65u,
                       84u,112u,127u,128u,129u,143u,144u,159u,160u,175u,176u,177u,
                       255u,256u,257u,1023u,1024u,1025u,2048u,4095u,4096u})
        for(uint32_t align=0;align<16;++align)
            compare(0x11800+align,0x10400+align,count);
    for(uint32_t count:{0u,1u,8u,16u,17u,32u,64u,128u,256u,1024u,4096u}) {
        for(uint32_t align=0;align<16;++align) {
            compare(0x11800+align,0x10400,count);
            compare(0x11800,0x10400+align,count);
        }
        compare(0x11800+15,0x10400,count);compare(0x11800,0x10400+15,count);
        compare(0x11800+7,0x10400+8,count);compare(0x11800+8,0x10400+7,count);
        compare(0x11800+1,0x10400+15,count);
    }
    // Compare the original routine's actual overlap behavior; do not substitute
    // a host memcpy/memmove or infer their standard-library overlap contract.
    for(int delta:{-64,-16,-1,0,1,16,64})
        for(uint32_t count:{1u,8u,16u,32u,64u,128u,256u,1024u})
            compare(uint32_t(0x11000+delta),0x11000,count);
    for(uint8_t access:{uint8_t(0),uint8_t(1)}) {
        setGuestPage(rt,0x14,access);
        compare(0x13FF8,0x10400,112);
        compare(0x10800,0x13FFC,112);
        compare(0x13F80,0x10400,256);
    }
    setGuestPage(rt,0x14,3);
}
void fastWordLeafContract(Simpsons::Runtime& rt) {
    constexpr uint32_t start=0x10000,size=0x5000;
    std::array<uint8_t,size> memory{},expectedMemory{};
    for(size_t i=0;i<size;++i)memory[i]=uint8_t(i*71+19);
    const auto watch=rt.writeWatch();
    const auto arm=[&] {
        std::array<uint32_t,5> versions{};
        for(uint32_t page=0x10;page<=0x14;++page)versions[page-0x10]=watch.armPage(page);
        return versions;
    };
    const auto changes=[&](const std::array<uint32_t,5>& before) {
        std::array<uint32_t,5> delta{};
        for(uint32_t page=0x10;page<=0x14;++page)
            delta[page-0x10]=watch.version[page].load(std::memory_order_acquire)-before[page-0x10];
        return delta;
    };
    const auto compare=[&](Function referenceFunction,Function actualFunction,
                           uint32_t destination,uint32_t source,uint32_t count) {
        PPCContext initial{};initial.r1.u32=0x13F80;
        initial.r3.u64=0x1234567800000000ull|destination;
        initial.r4.u64=0xFEDCBA9800000000ull|source;
        initial.r5.u64=0x7654321000000000ull|count;
        initial.r8.u64=0xAABBCCDD11223344;initial.r9.u64=0x123456789ABCDEF0;
        initial.r10.u64=0x1020304050607080;initial.r11.u64=0x8070605040302010;
        initial.lr=0x98765432ABCDCBA0;initial.traceIndex=63;
        PPCContext reference{},actual{};
        std::memcpy(&reference,&initial,sizeof(initial));std::memcpy(&actual,&initial,sizeof(initial));
        withOpenFixture(rt,[&]{std::memcpy(rt.base+start,memory.data(),size);});
        const auto beforeReference=arm();
        const auto expected=run(referenceFunction,reference,rt.base);
        const auto expectedChanges=changes(beforeReference);
        withOpenFixture(rt,[&]{std::memcpy(expectedMemory.data(),rt.base+start,size);});
        withOpenFixture(rt,[&]{std::memcpy(rt.base+start,memory.data(),size);});
        const auto beforeActual=arm();
        const auto result=run(actualFunction,actual,rt.base);
        need(result==expected,"Fast inline leaf failure or FPSCR changed");
        need(!std::memcmp(&reference,&actual,sizeof(actual)),"Fast inline leaf register/fault order changed");
        need(changes(beforeActual)==expectedChanges,"Fast inline leaf exact write-watch invalidation changed");
        need(withOpenFixture(rt,[&]{return !std::memcmp(expectedMemory.data(),rt.base+start,size);}),
             "Fast inline leaf overlap or partial-write order changed");
    };
    // The original block-copy leaf subtracts64 until zero; its caller contract
    // supplies multiples of64. Compare its ordered word semantics for overlaps.
    for(uint32_t count:{0u,64u,128u,256u,1024u,4096u}) {
        for(uint32_t src=0;src<4;++src)for(uint32_t dst=0;dst<4;++dst)
            compare(referenceFastWordBlockCopy,sub_8270A590,0x11800+dst,0x10400+src,count);
        for(int offset:{-128,-64,-4,-1,0,1,4,64,128})
            compare(referenceFastWordBlockCopy,sub_8270A590,uint32_t(0x11000+offset),0x11000,count);
    }
    // Matrix composition retains all interleaved loads/stores under aliases.
    for(uint32_t destination:{0x10800u,0x10400u,0x10404u,0x10430u,0x10200u})
        compare(referenceFastMatrixCompose,sub_823F2D98,destination,0x10200,0x10400);
    for(uint8_t access:{uint8_t(0),uint8_t(1)}) {
        setGuestPage(rt,0x14,access);
        compare(referenceFastWordBlockCopy,sub_8270A590,0x13FF8,0x10400,128);
        compare(referenceFastWordBlockCopy,sub_8270A590,0x11800,0x13FFC,128);
        compare(referenceFastMatrixCompose,sub_823F2D98,0x13FF8,0x10200,0x10400);
        compare(referenceFastMatrixCompose,sub_823F2D98,0x10800,0x13FFC,0x10400);
        compare(referenceFastMatrixCompose,sub_823F2D98,0x10800,0x10200,0x13FC8);
    }
    setGuestPage(rt,0x14,3);
    // The exceptional import page and unmapped device aperture stay fail-stop.
    compare(referenceFastWordBlockCopy,sub_8270A590,0x11800,0x82000600,64);
    compare(referenceFastWordBlockCopy,sub_8270A590,0xFFD00000,0x10400,64);
    rt.requestStop("fast inline leaf cancellation");
    compare(referenceFastWordBlockCopy,sub_8270A590,0x11800,0x10400,64);
    rt.stopping=false;PPCStopRequested=0;ResetEvent(rt.stopEvent);
}
void fastWordPointerContract(Simpsons::Runtime& rt) {
    auto* base=rt.base;
    // Demand-map the null scratch page through the original external helper,
    // then both paths must observe exactly those bytes.
    const auto zero=PPCExternalFastLoad32(base,0);
    need(PPCInlineFastLoad32(base,0)==zero,"Fast inline null scratch access changed");
    const uint32_t physical=rt.allocatePhysical(0,0x20000,PAGE_READWRITE,0x100000,0x1FFFFF,0x10000);
    need(physical!=0,"Fast inline physical fixture allocation failed");
    const uint32_t offset=rt.physicalAddress(physical),canonical=0xA0000000u+offset;
    const auto watch=rt.writeWatch();
    for(uint32_t alias:{physical,canonical,0xC0000000u+offset,0xDFFFF000u+offset}) {
        PPCExternalFastStore32(base,canonical,0xA5B63719);
        need(PPCInlineFastLoad32(base,alias)==0xA5B63719,"Fast inline physical aperture load changed");
        const uint32_t page=canonical>>12,before=watch.armPage(page);
        PPCInlineFastStore32(base,alias,0xE91572A3);
        need(PPCExternalFastLoad32(base,canonical)==0xE91572A3,"Fast inline physical aperture store changed");
        need(watch.version[page].load(std::memory_order_acquire)==before+1,
             "Fast inline alias store lost canonical write invalidation");
    }
    // A word beginning in the previous64KiB block must invalidate the armed
    // first page of the next block, even when its first page is not armed.
    const uint32_t crossing=canonical+0xFFFF,nextPage=(crossing+3)>>12;
    const auto before=watch.armPage(nextPage);
    PPCInlineFastStore32(base,crossing,0x718A92D3);
    need(PPCExternalFastLoad32(base,crossing)==0x718A92D3,"Fast inline crossing word bytes changed");
    need(watch.version[nextPage].load(std::memory_order_acquire)==before+1,
         "Fast inline crossing word lost the next-block write barrier");
    rt.freePhysical(physical);
}
void fastByteDecodeContract(Simpsons::Runtime& rt) {
    // Include the largest qualified backreference distance (131072 bytes)
    // and snapshot every byte of both the ordinary and physical fixtures.
    rt.map(0x15000,0x3B000,true,"byte decoder backreference fixture");
    const uint32_t physical=rt.allocatePhysical(0,0x20000,PAGE_READWRITE,0x100000,0x1FFFFF,0x10000);
    need(physical!=0,"Byte decoder physical fixture allocation failed");
    const uint32_t physicalOffset=rt.physicalAddress(physical),physicalStart=0xA0000000u+physicalOffset;
    struct Region {uint32_t start;std::vector<uint8_t> initial,expected;};
    std::array<Region,2> regions{{{0x10000,std::vector<uint8_t>(0x40000),std::vector<uint8_t>(0x40000)},
                                {physicalStart,std::vector<uint8_t>(0x20000),std::vector<uint8_t>(0x20000)}}};
    for(auto& region:regions) {
        for(size_t i=0;i<region.initial.size();++i)region.initial[i]=uint8_t(i*71+19);
    }
    const auto put=[&](uint32_t address,std::span<const uint8_t> bytes) {
        for(auto& region:regions) {
            if(address>=region.start&&uint64_t(address)+bytes.size()<=uint64_t(region.start)+region.initial.size()) {
                std::copy(bytes.begin(),bytes.end(),region.initial.begin()+(address-region.start));return;
            }
        }
        throw Simpsons::Failure("Byte decoder fixture seed is outside owned memory");
    };
    const auto word=[&](uint32_t address,uint32_t value) {
        const std::array<uint8_t,4> bytes{uint8_t(value>>24),uint8_t(value>>16),uint8_t(value>>8),uint8_t(value)};
        put(address,bytes);
    };
    std::vector<uint32_t> pages;
    for(const auto& region:regions) {
        for(uint32_t page=region.start>>12;page<(region.start+region.initial.size())>>12;++page)pages.push_back(page);
    }
    const auto watch=rt.writeWatch();
    const auto arm=[&] {
        std::vector<uint32_t> before;before.reserve(pages.size());
        for(const uint32_t page:pages)before.push_back(watch.armPage(page));
        return before;
    };
    // The native decoder copies whole tokens only while no page among a token's
    // stores is armed; armed runs exercise its exact per-access path instead.
    const auto disarm=[&] {
        std::vector<uint32_t> before;before.reserve(pages.size());
        for(const uint32_t page:pages) {
            if(watch.armed[page].exchange(0,std::memory_order_seq_cst)==1)Simpsons::guestWatchBlockAdjust(page,-1);
            before.push_back(watch.version[page].load(std::memory_order_acquire));
        }
        return before;
    };
    const auto changes=[&](const std::vector<uint32_t>& before) {
        std::vector<uint32_t> delta;delta.reserve(pages.size());
        for(size_t i=0;i<pages.size();++i)delta.push_back(watch.version[pages[i]].load(std::memory_order_acquire)-before[i]);
        return delta;
    };
    const auto reset=[&] {
        withOpenFixture(rt,[&]{
            for(const auto& region:regions)std::memcpy(rt.base+region.start,region.initial.data(),region.initial.size());
        });
    };
    const auto compareOnce=[&](uint32_t inputCell,uint32_t outputCell,uint32_t count,
                               uint32_t csr,bool badBase,bool armed) {
        PPCContext initial{};initial.r1.u64=0x1234567800013F80;
        initial.r3.u64=0x1020304000000000ull|inputCell;initial.r4.u64=0x9080706000000000ull|outputCell;
        initial.r5.u64=0x7654321000000000ull|count;initial.r6.u64=0xAABBCCDD11223344;
        initial.r7.u64=0x1122334455667788;initial.r8.u64=0xAABBCCDD11223344;
        initial.r9.u64=0x123456789ABCDEF0;initial.r10.u64=0x1020304050607080;
        initial.r11.u64=0x8070605040302010;initial.r12.u64=0x1029384756ABCDEF;
        initial.r29.u64=0xFEDCBA9876543210;initial.r30.u64=0x8000000012345678;
        initial.r31.u64=0x7FFFABCD97531864;initial.lr=0x98765432ABCDCBA0;
        initial.ctr.u64=0xABCDEF7654321011;initial.traceIndex=63;initial.fpscr.csr=csr;
        initial.f0.u64=0x9876543210ABCDEF;initial.f14.u64=0x123456789ABCDEF0;
        initial.v0.u64[0]=0x0123456789ABCDEF;initial.v0.u64[1]=0xFEDCBA9876543210;initial.vscr_sat=0x5A;
        PPCContext reference{},actual{};std::memcpy(&reference,&initial,sizeof(initial));std::memcpy(&actual,&initial,sizeof(initial));
        reset();const auto beforeReference=armed?arm():disarm();const auto expected=run(referenceFastByteDecode,reference,rt.base+(badBase?32:0));
        const auto expectedChanges=changes(beforeReference);
        withOpenFixture(rt,[&]{for(auto& region:regions)std::memcpy(region.expected.data(),rt.base+region.start,region.expected.size());});
        reset();const auto beforeActual=armed?arm():disarm();const auto result=run(sub_8281E9B0,actual,rt.base+(badBase?32:0));
        need(result==expected,"Byte decoder external-fast failure or FPSCR changed");
        need(!std::memcmp(&reference,&actual,sizeof(actual)),"Byte decoder complete register/trace/fault order changed");
        need(changes(beforeActual)==expectedChanges,"Byte decoder exact write-watch invalidation changed");
        need(withOpenFixture(rt,[&]{
            for(const auto& region:regions) {
                if(std::memcmp(region.expected.data(),rt.base+region.start,region.expected.size()))return false;
            }
            return true;
        }),
            "Byte decoder complete fixture memory/overlap/partial writes changed");
        return std::pair{result,actual};
    };
    const auto exercise=[&](uint32_t inputCell,uint32_t outputCell,uint32_t count,
                            uint32_t csr=PPCFPSCRRegister::DefaultCSR,bool badBase=false) {
        const auto unwatched=compareOnce(inputCell,outputCell,count,csr,badBase,false);
        const auto watched=compareOnce(inputCell,outputCell,count,csr,badBase,true);
        need(unwatched.first==watched.first && !std::memcmp(&unwatched.second,&watched.second,sizeof(PPCContext)),
             "Byte decoder result depends on write-watch state");
        return watched;
    };
    constexpr uint32_t inputCell=0x10100,outputCell=0x10200,input=0x10400,output=0x11800;
    const auto setup=[&](std::span<const uint8_t> stream,uint32_t source=0x10400,uint32_t destination=0x11800,
                         uint32_t sourceCell=0x10100,uint32_t destinationCell=0x10200) {
        put(source,stream);word(sourceCell,source);word(destinationCell,destination);
    };
    const auto known=[&](std::span<const uint8_t> stream,uint32_t requested,std::span<const uint8_t> expectedOutput,
                         uint32_t consumed,uint32_t returned,uint32_t csr=PPCFPSCRRegister::DefaultCSR) {
        setup(stream);const auto [result,context]=exercise(inputCell,outputCell,requested,csr);
        need(result.failure.empty()&&context.r3.u64==returned,"Byte decoder token return semantics changed");
        need(PPCExternalFastLoad32(rt.base,inputCell)==input+consumed&&
             PPCExternalFastLoad32(rt.base,outputCell)==output+expectedOutput.size(),"Byte decoder cursor advancement changed");
        need(expectedOutput.empty()||!std::memcmp(rt.base+output,expectedOutput.data(),expectedOutput.size()),"Byte decoder known token bytes changed");
    };
    const std::vector<uint8_t> literal{0xE0,'A','B','C','D'},shortOverlap{0xE0,'A','B','C','D',0,0};
    const std::vector<uint8_t> cycle{0xE0,'A','B','C','D',0x1C,3},shortPrefix{0xE0,'A','B','C','D',3,0,'X','Y','Z'};
    const std::vector<uint8_t> medium{0xE0,'A','B','C','D',0x80,0,0};
    const std::vector<uint8_t> longPrefix{0xE0,'A','B','C','D',0xC3,0,3,0,'X','Y','Z'};
    const std::array<uint8_t,4> abcd{'A','B','C','D'};
    const std::array<uint8_t,7> shortBytes{'A','B','C','D','D','D','D'};
    const std::array<uint8_t,14> cycleBytes{'A','B','C','D','A','B','C','D','A','B','C','D','A','B'};
    const std::array<uint8_t,10> prefixBytes{'A','B','C','D','X','Y','Z','Z','Z','Z'};
    const std::array<uint8_t,8> mediumBytes{'A','B','C','D','D','D','D','D'};
    const std::array<uint8_t,12> longBytes{'A','B','C','D','X','Y','Z','D','X','Y','Z','D'};
    for(uint32_t round=0;round<4;++round)for(bool flush:{false,true}) {
        const uint32_t csr=PPCFPSCRRegister::DefaultCSR|(round<<PPCFPSCRRegister::RoundShift)|(flush?PPCFPSCRRegister::FlushMask:0);
        known(literal,4,abcd,5,0,csr);known(literal,1,abcd,5,0,csr); // A whole token overruns the requested end.
        known(shortOverlap,7,shortBytes,7,0,csr);known(cycle,14,cycleBytes,7,0,csr);
        known(shortPrefix,10,prefixBytes,10,0,csr);known(medium,8,mediumBytes,8,0,csr);
        known(longPrefix,12,longBytes,12,0,csr);
    }
    known(literal,0,{},0,0);
    for(uint32_t prefix=0;prefix<4;++prefix) {
        std::vector<uint8_t> stream=literal,bytes(abcd.begin(),abcd.end());stream.push_back(uint8_t(0xFC+prefix));
        for(uint32_t i=0;i<prefix;++i){stream.push_back(uint8_t('X'+i));bytes.push_back(uint8_t('X'+i));}
        known(stream,100,bytes,uint32_t(stream.size()),1);
    }
    for(uint32_t control=0xE0;control<=0xFB;++control) {
        const uint32_t count=((control&31)+1)*4;std::vector<uint8_t> stream{uint8_t(control)},bytes;
        for(uint32_t i=0;i<count;++i){stream.push_back(uint8_t(i*17));bytes.push_back(uint8_t(i*17));}
        known(stream,count,bytes,count+1,0);known(stream,1,bytes,count+1,0);
    }
    // All three backreference encodings, min/max lengths and distances,
    // every literal-prefix count; prehistory covers the farthest distance.
    for(uint32_t prefix=0;prefix<4;++prefix)for(uint32_t family=0;family<3;++family)
        for(uint32_t length:{family==0?3u:family==1?4u:5u,family==0?10u:family==1?67u:1028u})
            for(uint32_t distance:{1u,4u,256u,family==0?1024u:family==1?16384u:131072u}) {
                const uint32_t d=distance-1,l=length-(family==0?3:family==1?4:5);std::vector<uint8_t> stream;
                if(family==0)stream={uint8_t((d>>8)<<5|l<<2|prefix),uint8_t(d)};
                else if(family==1)stream={uint8_t(0x80|l),uint8_t(prefix<<6|d>>8),uint8_t(d)};
                else stream={uint8_t(0xC0|(d>>16)<<4|(l>>8)<<2|prefix),uint8_t(d>>8),uint8_t(d),uint8_t(l)};
                for(uint32_t i=0;i<prefix;++i)stream.push_back(uint8_t('X'+i));
                setup(stream,input,0x40000);exercise(inputCell,outputCell,length+prefix);
                exercise(inputCell,outputCell,1); // Do not truncate a long/repeated token.
            }
    // Payload and both cursor cells may be unaligned. The original accesses,
    // including final overlapping cursor stores, remain strictly ordered.
    for(uint32_t sourceOffset=0;sourceOffset<4;++sourceOffset)for(uint32_t destinationOffset=0;destinationOffset<4;++destinationOffset) {
        setup(cycle,input+sourceOffset,output+destinationOffset,inputCell+sourceOffset,outputCell+destinationOffset);
        exercise(inputCell+sourceOffset,outputCell+destinationOffset,14);
    }
    for(int delta:{-8,-4,-1,0,1,4,8}) {
        setup(literal,0x11000,uint32_t(0x11001+delta));exercise(inputCell,outputCell,1);
    }
    for(uint32_t destination:{inputCell,outputCell,inputCell+1,outputCell+1}) {
        setup(literal,input,destination);exercise(inputCell,outputCell,4);
    }
    setup(literal);word(inputCell,input);exercise(inputCell,inputCell,1);
    setup(literal);exercise(inputCell,inputCell+1,0);
    for(uint8_t access:{uint8_t(0),uint8_t(1)}) {
        setGuestPage(rt,0x14,access);
        setup(literal,0x13FFD,output);exercise(inputCell,outputCell,4); // Literal read after two completed writes.
        setup(literal,input,0x13FFE);exercise(inputCell,outputCell,4); // Two byte stores before a fault.
        const std::array<uint8_t,3> repeat{0x80,0x11,1}; // distance4354, source13FFE, accessible output15100.
        setup(repeat,input,0x15100);exercise(inputCell,outputCell,4);
        const std::array<uint8_t,4> longToken{0xC0,0,0,0};
        setup(longToken,0x13FFF,output);exercise(inputCell,outputCell,5); // Header read fault.
        setup(literal);exercise(0x14000,outputCell,4);exercise(inputCell,0x14000,4);
        if(access==1) {
            setup(literal,input,output,0x14000,outputCell);exercise(0x14000,outputCell,4); // First final cursor store.
            setup(literal,input,output,inputCell,0x14000);exercise(inputCell,0x14000,4); // Second final cursor store.
        }
    }
    setGuestPage(rt,0x14,3);
    // Canonical physical apertures and both cursor cells are qualified by
    // the same full-context/memory/watch oracle, not by a model decoder.
    for(const uint32_t alias:{physical,physicalStart,0xC0000000u+physicalOffset,0xDFFFF000u+physicalOffset}) {
        put(physicalStart+0x400,literal);word(physicalStart+0x100,alias+0x400);word(physicalStart+0x200,alias+0x1800);
        exercise(alias+0x100,alias+0x200,4);
        put(physicalStart+0xFFFD,literal);word(inputCell,alias+0xFFFD);word(outputCell,alias+0xFFFF);
        exercise(inputCell,outputCell,4); // Alias, payload overlap and64KiB boundary.
    }
    // Exceptional import page and opaque/unmapped hardware apertures must
    // still fail at the same byte/cursor access, including partial writes.
    for(uint32_t aperture:{0x82000600u,0xFFD00000u,0x70000000u}) {
        setup(literal);word(inputCell,aperture);exercise(inputCell,outputCell,4);
        setup(literal);word(outputCell,aperture);exercise(inputCell,outputCell,4);
        setup(literal);exercise(aperture,outputCell,4);exercise(inputCell,aperture,4);
        const std::array<uint8_t,2> repeat{0,0};setup(repeat);word(outputCell,aperture+1);
        exercise(inputCell,outputCell,3); // Backreference faults before its output store.
    }
    setup(literal);exercise(inputCell,outputCell,4,PPCFPSCRRegister::DefaultCSR,true);
    rt.requestStop("byte decoder entry cancellation");setup(cycle);exercise(inputCell,outputCell,14);
    rt.stopping=false;PPCStopRequested=0;ResetEvent(rt.stopEvent);
    // Seeded streams of every token kind, overlapping and far backreferences into
    // prehistory, and budgets that end inside, at and after tokens. Garbage streams
    // reach the low demand-mapped pages, wrapped cursors and unmapped memory.
    uint32_t seed=0x51A7C0DE;
    constexpr uint32_t streamAt=0x16000,decodedAt=0x38000;
    for(uint32_t sample=0;sample<160;++sample) {
        std::vector<uint8_t> stream;uint64_t produced=0;
        const uint32_t tokens=1+randomWord(seed)%(sample<16?8:160);
        for(uint32_t token=0;token<tokens;++token) {
            const uint32_t kind=randomWord(seed)%10,prefix=randomWord(seed)%4;
            const auto literalBytes=[&](uint32_t count){for(uint32_t i=0;i<count;++i)stream.push_back(uint8_t(randomWord(seed)>>13));produced+=count;};
            if(kind>=8) {
                const uint32_t runs=randomWord(seed)%28;stream.push_back(uint8_t(0xE0|runs));literalBytes((runs+1)*4);continue;
            }
            const uint32_t family=kind<4?0:kind<6?1:2;
            const uint32_t maxLength=family==0?10:family==1?67:1028,minLength=family==0?3:family==1?4:5;
            const uint32_t maxDistance=family==0?1024:family==1?16384:131072;
            const uint32_t length=minLength+randomWord(seed)%(maxLength-minLength+1);
            // Mostly near (often overlapping) copies, some far into the prehistory.
            const uint64_t reach=std::min<uint64_t>(maxDistance,produced+prefix+(decodedAt-0x10000));
            const uint32_t distance=randomWord(seed)%4?1+randomWord(seed)%std::max<uint32_t>(1,uint32_t(std::min<uint64_t>(reach,64))):
                1+uint32_t(randomWord(seed)%reach);
            const uint32_t d=distance-1,l=length-minLength;
            if(family==0)stream.insert(stream.end(),{uint8_t((d>>8)<<5|l<<2|prefix),uint8_t(d)});
            else if(family==1)stream.insert(stream.end(),{uint8_t(0x80|l),uint8_t(prefix<<6|d>>8),uint8_t(d)});
            else stream.insert(stream.end(),{uint8_t(0xC0|(d>>16)<<4|(l>>8)<<2|prefix),uint8_t(d>>8),uint8_t(d),uint8_t(l)});
            literalBytes(prefix);produced+=length;
        }
        if(sample%3) {const uint32_t tail=randomWord(seed)%4;stream.push_back(uint8_t(0xFC|tail));
            for(uint32_t i=0;i<tail;++i){stream.push_back(uint8_t(randomWord(seed)));++produced;}}
        need(stream.size()<decodedAt-streamAt && produced+0x2000<0x48000-decodedAt,"Byte decoder random fixture overflow");
        setup(stream,streamAt,decodedAt);
        const uint32_t whole=uint32_t(produced)+1+randomWord(seed)%64,partial=1+randomWord(seed)%uint32_t(produced+1);
        exercise(inputCell,outputCell,whole);exercise(inputCell,outputCell,partial);
    }
    for(uint32_t sample=0;sample<48;++sample) {
        std::vector<uint8_t> stream(512);for(auto& byte:stream)byte=uint8_t(randomWord(seed)>>11);
        const uint32_t destination=sample%4==0?0x10000+randomWord(seed)%0x400:decodedAt+randomWord(seed)%0x8000;
        setup(stream,streamAt,destination);exercise(inputCell,outputCell,1+randomWord(seed)%2048);
    }
    rt.freePhysical(physical);
}
void fastBytePointerContract(Simpsons::Runtime& rt) {
    auto* base=rt.base;
    need(PPCInlineFastLoad8(base,0)==PPCExternalFastLoad8(base,0),"Fast inline byte null scratch changed");
    const uint32_t physical=rt.allocatePhysical(0,0x20000,PAGE_READWRITE,0x100000,0x1FFFFF,0x10000);
    need(physical!=0,"Fast inline byte physical fixture allocation failed");
    const uint32_t offset=rt.physicalAddress(physical),canonical=0xA0000000u+offset;const auto watch=rt.writeWatch();
    for(uint32_t alias:{physical,canonical,0xC0000000u+offset,0xDFFFF000u+offset}) {
        for(uint32_t byteOffset:{0u,1u,2u,3u,4095u,4096u,65535u,65536u}) {
            PPCExternalFastStore8(base,canonical+byteOffset,0xA5);
            need(PPCInlineFastLoad8(base,alias+byteOffset)==0xA5,"Fast inline byte alias load changed");
            const uint32_t page=(canonical+byteOffset)>>12,before=watch.armPage(page);
            PPCInlineFastStore8(base,alias+byteOffset,0xE9);
            need(PPCExternalFastLoad8(base,canonical+byteOffset)==0xE9,"Fast inline byte alias store changed");
            need(watch.version[page].load(std::memory_order_acquire)==before+1,"Fast inline byte canonical barrier changed");
        }
    }
    const uint32_t last=canonical+0xFFFF,nextPage=(last+1)>>12;
    const auto before=watch.armPage(last>>12),nextBefore=watch.armPage(nextPage);
    PPCInlineFastStore8(base,last,0x73);
    need(watch.version[last>>12].load(std::memory_order_acquire)==before+1&&
         watch.version[nextPage].load(std::memory_order_acquire)==nextBefore,"Byte store invalidated an untouched neighboring block");
    PPCInlineFastStore8(base,last+1,0x19);
    need(watch.version[nextPage].load(std::memory_order_acquire)==nextBefore+1,"Next-block byte store lost its exact barrier");
    rt.freePhysical(physical);
}
// This executable overrides the original weak growth alias only for this
// differential fixture. It models a bounded preallocated relocation, not the
// game's allocator. Both otherwise exact bodies must reach it with identical
// complete state/memory and preserve its returns, partial failure and stop.
struct PairGrowth {
    enum class Mode {Success,Throw,Cancel};
    struct Call {PPCContext context{};std::vector<uint8_t> memory;};
    Simpsons::Runtime& rt;
    std::array<std::pair<uint32_t,size_t>,2> regions;
    uint32_t destination{};
    Mode mode{};
    std::vector<Call> calls;
    static PairGrowth* current;
    void invoke(PPCContext& ctx,uint8_t* base) {
        Call call;std::memcpy(&call.context,&ctx,sizeof(ctx));
        for(const auto& [address,size]:regions) {
            const auto at=call.memory.size();call.memory.resize(at+size);
            withOpenFixture(rt,[&]{std::memcpy(call.memory.data()+at,rt.base+address,size);});
        }
        calls.push_back(std::move(call));
        need(base==rt.base,"Pair growth received a foreign base");
        need(ctx.lr==0x82B2A92C&&ctx.r5.u64==4,"Pair growth caller/element width changed");
        const uint32_t owner=ctx.r3.u32,count=PPCExternalFastLoad32(base,owner+4);
        const uint32_t capacity=PPCExternalFastLoad32(base,owner+8)&0x3FFFFFFFu;
        need(count<=64&&ctx.r4.u64==std::max(count+2,capacity*2),"Pair growth requested capacity changed");
        const uint32_t source=PPCExternalFastLoad32(base,owner);
        PPC_TRACE_ENTRY(ctx,0x82B64600);
        // Volatile callee state deliberately changes. Original saved arguments
        // and nonvolatile registers must still govern the subsequent shifts.
        ctx.r3.u64=0xFEDCBA9876543210;ctx.r4.u64=0x1020304050607080;
        ctx.r5.u64=0xABCDEF1234567890;ctx.r6.u64=0x76543210FEDCBA98;
        ctx.r7.u64=0x1111222233334444;ctx.r8.u64=0x5555666677778888;
        ctx.r9.u64=0x9999AAAABBBBCCCC;ctx.r10.u64=0xDDDDEEEEFFFF0000;
        ctx.r11.u64=0x0123456789ABCDEF;ctx.r12.u64=0xAABBCCDDEEFF0011;
        ctx.cr0.lt=1;ctx.cr0.gt=0;ctx.cr0.eq=0;ctx.cr0.so=1;ctx.f0.u64=0x7FF8123456789ABC;
        ctx.v0.u64[0]=0x8070605040302010;ctx.lr=0x1234567887654321;
        for(uint32_t i=0;i<count*2;++i) {
            PPCExternalFastStore16(base,destination+2*i,PPCExternalFastLoad16(base,source+2*i));
            if(i==0&&mode==Mode::Throw)throw Simpsons::Failure("pair growth injected partial failure");
            if(i==0&&mode==Mode::Cancel) {
                rt.requestStop("pair growth injected cancellation");
                PPC_TRACE_ENTRY(ctx,0x82B64604);
            }
        }
        PPCExternalFastStore32(base,owner,destination);
        PPCExternalFastStore32(base,owner+8,(capacity*2>=count+2?capacity*2:count+2)|0xC0000000u);
    }
};
PairGrowth* PairGrowth::current{};
void fastPairShiftContract(Simpsons::Runtime& rt) {
    const uint32_t physical=rt.allocatePhysical(0,0x20000,PAGE_READWRITE,0x100000,0x1FFFFF,0x10000);
    need(physical!=0,"Pair shift physical fixture allocation failed");
    const uint32_t physicalOffset=rt.physicalAddress(physical),canonical=0xA0000000u+physicalOffset;
    struct Region {uint32_t start;std::vector<uint8_t> initial,expected;};
    std::array<Region,2> regions{{{0x10000,std::vector<uint8_t>(0x20000),std::vector<uint8_t>(0x20000)},
                                {canonical,std::vector<uint8_t>(0x20000),std::vector<uint8_t>(0x20000)}}};
    const auto put=[&](uint32_t address,std::span<const uint8_t> bytes) {
        for(auto& region:regions)if(address>=region.start&&uint64_t(address)+bytes.size()<=uint64_t(region.start)+region.initial.size()) {
            std::copy(bytes.begin(),bytes.end(),region.initial.begin()+(address-region.start));return;
        }
        throw Simpsons::Failure("Pair shift seed is outside owned memory");
    };
    const auto half=[&](uint32_t address,uint16_t value) {
        const std::array<uint8_t,2> bytes{uint8_t(value>>8),uint8_t(value)};put(address,bytes);
    };
    const auto word=[&](uint32_t address,uint32_t value) {
        const std::array<uint8_t,4> bytes{uint8_t(value>>24),uint8_t(value>>16),uint8_t(value>>8),uint8_t(value)};put(address,bytes);
    };
    const auto setup=[&](std::span<const uint16_t> keys,uint32_t capacity,uint32_t owner=0x10100,
                         uint32_t data=0x10800,uint32_t aliasData=0) {
        for(auto& region:regions)for(size_t i=0;i<region.initial.size();++i)region.initial[i]=uint8_t(i*71+19);
        word(owner,aliasData?aliasData:data);word(owner+4,uint32_t(keys.size()));word(owner+8,capacity);
        for(size_t i=0;i<keys.size();++i) {half(data+uint32_t(4*i),keys[i]);half(data+uint32_t(4*i+2),uint16_t(0x8100+i));}
    };
    std::vector<uint32_t> pages;
    for(const auto& region:regions)for(uint32_t page=region.start>>12;page<(region.start+region.initial.size())>>12;++page)pages.push_back(page);
    const auto watch=rt.writeWatch();
    const auto arm=[&] {std::vector<uint32_t> result;for(uint32_t page:pages)result.push_back(watch.armPage(page));return result;};
    const auto changes=[&](const std::vector<uint32_t>& before) {
        std::vector<uint32_t> result;for(size_t i=0;i<pages.size();++i)result.push_back(watch.version[pages[i]].load(std::memory_order_acquire)-before[i]);return result;
    };
    const auto reset=[&] {withOpenFixture(rt,[&]{for(const auto& region:regions)std::memcpy(rt.base+region.start,region.initial.data(),region.initial.size());});};
    const auto exercise=[&](uint32_t owner,uint32_t low,uint32_t high,uint32_t lowOutput=0x10200,uint32_t highOutput=0x10202,
                            uint32_t csr=PPCFPSCRRegister::DefaultCSR,PairGrowth::Mode growthMode=PairGrowth::Mode::Success,
                            bool entryStop=false,bool contextAlias=false,uint32_t stack=0x13F80) {
        PPCContext initial{};
        // Every byte, including padding, participates in the state comparison.
        auto* bytes=reinterpret_cast<uint8_t*>(&initial);
        for(size_t i=0;i<sizeof(initial);++i)bytes[i]=uint8_t(i*37+11);
        initial.r1.u64=0x1234567800000000ull|stack;initial.r3.u64=0x9182736400000000ull|owner;
        initial.r4.u64=0x8070605040302010;initial.r5.u64=0x76543210ABCDC39A;
        initial.r6.u64=0xABCDEFAB00000000ull|low;initial.r7.u64=0xFEDCBAFE00000000ull|high;
        initial.r8.u64=0xAABBCCDD00000000ull|lowOutput;initial.r9.u64=0x1234567800000000ull|highOutput;
        initial.lr=0x98765432ABCDCBA0;initial.ctr.u64=0xABCDEF7654321011;initial.traceIndex=63;
        initial.fpscr.csr=csr;initial.vscr_sat=0x5A;
        PairGrowth growth{rt,{{{regions[0].start,regions[0].initial.size()},{regions[1].start,regions[1].initial.size()}}},0x12001,growthMode,{}};
        struct Clear {~Clear(){PairGrowth::current=nullptr;}} clear;PairGrowth::current=&growth;
        const auto invoke=[&](Function function,PPCContext& result) {
            rt.stopping=false;PPCStopRequested=0;ResetEvent(rt.stopEvent);
            if(entryStop)rt.requestStop("pair shift entry cancellation");
            reset();PPCContext local{};
            PPCContext* context=contextAlias?reinterpret_cast<PPCContext*>(rt.base+0x18000):&local;
            std::memcpy(context,&initial,sizeof(initial));
            const auto before=arm();SetLastError(0x41ACED29);
            const auto outcome=run(function,*context,rt.base);const DWORD error=GetLastError();
            std::memcpy(&result,context,sizeof(result));
            return std::tuple{outcome,changes(before),error};
        };
        PPCContext reference{},actual{};const auto expected=invoke(referenceFastPairShift,reference);
        const auto referenceCalls=growth.calls;
        withOpenFixture(rt,[&]{for(auto& region:regions)std::memcpy(region.expected.data(),rt.base+region.start,region.expected.size());});
        growth.calls.clear();const auto result=invoke(sub_82B2A8D0,actual);
        need(result==expected,"Pair shift external-fast failure/FP/write-watch/LastError changed");
        need(!std::memcmp(&reference,&actual,sizeof(actual)),"Pair shift complete PPC register/trace/FPSCR state changed");
        if(std::get<0>(result).failure.empty()) {
            need(actual.r1.u64==initial.r1.u64&&actual.lr==uint32_t(initial.lr)&&actual.r12.u64==uint32_t(initial.lr),
                 "Pair shift original stack/32-bit saved LR restoration changed");
            need(actual.r25.u64==initial.r25.u64&&actual.r26.u64==initial.r26.u64&&actual.r27.u64==initial.r27.u64&&
                 actual.r28.u64==initial.r28.u64&&actual.r29.u64==initial.r29.u64&&actual.r30.u64==initial.r30.u64&&
                 actual.r31.u64==initial.r31.u64,"Pair shift original nonvolatile restoration changed");
        }
        need(withOpenFixture(rt,[&]{for(const auto& region:regions)if(std::memcmp(region.expected.data(),rt.base+region.start,region.expected.size()))return false;return true;}),
             "Pair shift full owned memory, save/restore or partial-write order changed");
        need(growth.calls.size()==referenceCalls.size(),"Pair shift growth call count changed");
        for(size_t i=0;i<growth.calls.size();++i) {
            need(!std::memcmp(&growth.calls[i].context,&referenceCalls[i].context,sizeof(PPCContext)),"Pair shift growth full call state changed");
            need(growth.calls[i].memory==referenceCalls[i].memory,"Pair shift growth call-boundary memory changed");
            const auto& call=growth.calls[i].context;
            need(call.r3.u64==initial.r3.u64&&call.r30.u64==initial.r3.u64&&call.r1.u64==initial.r1.u64-144,
                 "Pair shift growth owner/frame ABI changed");
            need(call.r12.u64==initial.lr,"Pair shift growth saved incoming LR changed");
            need(call.r25.u64==initial.r8.u64&&call.r26.u64==initial.r6.u64&&call.r27.u64==initial.r9.u64&&
                 call.r28.u64==initial.r5.u64&&call.r29.u64==initial.r7.u64,"Pair shift growth saved argument ABI changed");
        }
        rt.stopping=false;PPCStopRequested=0;ResetEvent(rt.stopEvent);
        return std::pair{std::get<0>(result),growth.calls.size()};
    };
    const std::array<uint16_t,7> keys{0,8,8,32,32,64,0xFFFF};
    for(uint32_t round=0;round<4;++round)for(bool flush:{false,true}) {
        const uint32_t csr=PPCFPSCRRegister::DefaultCSR|(round<<PPCFPSCRRegister::RoundShift)|(flush?PPCFPSCRRegister::FlushMask:0);
        for(const auto& [low,high]:{std::pair{0u,1u},{8u,8u},{8u,32u},{31u,65u},{0xFFFFu,0xFFFFu},{0x12340008u,0xFFFF0020u}}) {
            setup(keys,0xC0000020);const auto [outcome,calls]=exercise(0x10100,low,high,0x10200,0x10202,csr);
            need(outcome.failure.empty()&&calls==0,"Pair shift no-growth path unexpectedly failed or grew");
            need(PPCExternalFastLoad32(rt.base,0x10104)==keys.size()+2,"Pair shift pair count changed");
            // Independently check sorting, endian output indices and payload;
            // Upper endpoints precede equals; lower endpoints follow equals in
            // the preceding prefix. Equal new endpoints therefore stay adjacent.
            std::vector<std::pair<uint16_t,uint16_t>> ordered;
            for(size_t i=0;i<keys.size();++i)ordered.emplace_back(keys[i],uint16_t(0x8100+i));
            const uint16_t upper=uint16_t(high),lower=uint16_t(low),payload=0xC39A;
            const auto upperAt=std::lower_bound(ordered.begin(),ordered.end(),upper,[](const auto& pair,uint16_t key){return pair.first<key;});
            const size_t prefix=size_t(upperAt-ordered.begin()),upperIndex=prefix+1;
            ordered.insert(upperAt,{upper,payload});
            const auto lowerAt=std::upper_bound(ordered.begin(),ordered.begin()+prefix,lower,[](uint16_t key,const auto& pair){return key<pair.first;});
            const size_t lowerIndex=size_t(lowerAt-ordered.begin());ordered.insert(lowerAt,{lower,payload});
            for(size_t i=0;i<ordered.size();++i) {
                need(PPCExternalFastLoad16(rt.base,0x10800+uint32_t(4*i))==ordered[i].first&&
                     PPCExternalFastLoad16(rt.base,0x10802+uint32_t(4*i))==ordered[i].second,"Pair shift known sorted/sentinel/payload bytes changed");
            }
            need(PPCExternalFastLoad16(rt.base,0x10200)==lowerIndex&&PPCExternalFastLoad16(rt.base,0x10202)==upperIndex,
                 "Pair shift original insertion index semantics changed");
        }
    }
    // Capacity flags are masked before doubling; minimum count+2 and doubling
    // branches both reach the original call with the same full-width arguments.
    for(uint32_t count:{1u,2u,7u})for(uint32_t capacity:{count,count+1})for(auto mode:{PairGrowth::Mode::Success,PairGrowth::Mode::Throw,PairGrowth::Mode::Cancel}) {
        setup(std::span(keys).first(count),capacity|0xC0000000u);
        const auto [outcome,calls]=exercise(0x10100,8,32,0x10200,0x10202,PPCFPSCRRegister::DefaultCSR,mode);
        need(calls==1,"Pair shift growth path did not exercise the original call");
        need(outcome.failure.empty()==(mode==PairGrowth::Mode::Success),"Pair growth injected failure/cancellation was not propagated");
        if(mode==PairGrowth::Mode::Success)need(PPCExternalFastLoad32(rt.base,0x10100)==0x12001,"Pair shift did not consume relocated storage");
    }
    // Unaligned owners, cells, pair loads/stores, overlapping output stores and
    // output aliases with data/count/capacity retain the recovered access order.
    for(uint32_t alignment=0;alignment<4;++alignment) {
        setup(keys,32,0x10100+alignment,0x10800+alignment);exercise(0x10100+alignment,8,32,0x10200+alignment,0x10202+alignment);
    }
    for(uint32_t count:{16u,64u}) {
        std::vector<uint16_t> dense(count);for(uint32_t i=0;i<count;++i)dense[i]=uint16_t(3*(i/2));
        for(uint32_t upper:{1u,8u,32u,64u,0xFFFFu})for(uint32_t lower:{0u,1u,upper}) {
            setup(dense,count+2);exercise(0x10100,lower,upper);
        }
    }
    for(const auto& [lowOut,highOut]:{std::pair{0x10200u,0x10200u},{0x10200u,0x10201u},{0x10801u,0x10802u},
                                      {0x10104u,0x10106u},{0x10108u,0x1010Au}}) {
        setup(keys,32);exercise(0x10100,8,32,lowOut,highOut);
    }
    // Host context is deliberately backed by this owned guest fixture; stores
    // through output cells alter otherwise untouched FP register bytes as well.
    setup(keys,32);exercise(0x10100,8,32,0x18000+uint32_t(offsetof(PPCContext,f14)),
                           0x18000+uint32_t(offsetof(PPCContext,f15)),PPCFPSCRRegister::DefaultCSR,PairGrowth::Mode::Success,false,true);
    for(uint8_t access:{uint8_t(0),uint8_t(1)}) {
        setGuestPage(rt,0x14,access);
        for(const auto& [lowOutput,highOutput]:{std::pair{0x14000u,0x10202u},{0x10200u,0x14000u}}) {
            setup(keys,32);const auto [outcome,calls]=exercise(0x10100,8,32,lowOutput,highOutput);
            need(!outcome.failure.empty()&&calls==0,"Pair shift late protected output did not fault");
            need(PPCExternalFastLoad32(rt.base,0x10104)==keys.size()+2,"Pair shift count store before output fault was lost");
        }
        setup(std::span(keys).first(1),32,0x10100,0x13FF7);exercise(0x10100,0,1); // Halfword store straddles protection boundary.
        setup(std::span(keys).first(2),32,0x10100,0x13FF7);exercise(0x10100,1,1); // Shift store on protected page.
        setup(std::span(keys).first(2),32,0x10100,0x13FFB);exercise(0x10100,8,32); // Halfword key read straddles boundary.
        setup(keys,32,0x13FFC);exercise(0x13FFC,8,32); // Owner metadata read fault.
        setup(keys,32);exercise(0x10100,8,32,0x10200,0x10202,PPCFPSCRRegister::DefaultCSR,PairGrowth::Mode::Success,false,false,0x14040);
    }
    setGuestPage(rt,0x14,3);
    for(uint32_t alias:{physical,canonical,0xC0000000u+physicalOffset,0xDFFFF000u+physicalOffset}) {
        setup(keys,32,0x10100,canonical+0xFFED,alias+0xFFED);exercise(0x10100,8,32,alias+0x100,alias+0x102);
        setup(keys,32,canonical+0x201,canonical+0x1001,alias+0x1001);exercise(alias+0x201,8,32,alias+0x301,alias+0x303);
    }
    for(uint32_t aperture:{0x82000600u,0xFFD00000u,0x70000000u}) {
        setup(keys,32,0x10100,0x10800,aperture);exercise(0x10100,8,32);
        setup(keys,32);exercise(aperture,8,32);
        setup(keys,32);exercise(0x10100,8,32,aperture,0x10202);
        setup(keys,32);exercise(0x10100,8,32,0x10200,aperture);
    }
    setup(keys,32);const auto [stopped,stopCalls]=exercise(0x10100,8,32,0x10200,0x10202,PPCFPSCRRegister::DefaultCSR,PairGrowth::Mode::Success,true);
    need(!stopped.failure.empty()&&stopCalls==0&&PPCExternalFastLoad32(rt.base,0x10104)==keys.size(),
         "Pair shift entry cancellation touched original owner or grew");
    rt.freePhysical(physical);
}
// Test-only VM call boundaries. These bounded preallocated mutations are not
// the game's allocator, hook implementation or VM. The original save/rest28,
// indirect dispatcher and missing-live-save nonlocal rejection remain real.
struct VMSetupBoundary {
    enum class Kind:uint32_t {Coerce=0x828449E0,Frames=0x828445D8,Stack=0x828444D8,
        Varargs=0x82844838,Error=0x8283E360,Hook=0x82844738,Exit=0x82A41FA0,
        Panic=0x82230000,Closure=0x82230004};
    enum class Mode {Success,Throw,Cancel};
    using Representation=std::array<uint8_t,sizeof(PPCContext)>;
    static_assert(sizeof(Representation)==sizeof(PPCContext));
    // Typed PPCContext moves omitted 32 tail-padding bytes in the diagnostic build.
    // Store its complete captured object representation as ordinary byte-array
    // elements so every byte survives Call copies, moves and vector growth.
    struct Call {Kind kind{};Representation context{};std::vector<uint8_t> memory;std::vector<uint32_t> watch;DWORD error{};};
    Simpsons::Runtime& rt;
    std::vector<std::pair<uint32_t,size_t>> regions;
    std::vector<uint32_t> pages,versions;
    uint32_t owner{},function{},relocatedStack{0x12000},relocatedFrame{0x11800},returned{};
    Kind injected{Kind::Stack};Mode mode{Mode::Success};bool relocate{},advanceTop{};
    std::vector<Call> calls;
    size_t ordinal{};
    static VMSetupBoundary* current;
    static void diagnose(const char* phase,size_t ordinal,size_t record,Kind kind,const uint8_t* expected,const uint8_t* actual) {
        size_t first{};while(first<sizeof(PPCContext)&&expected[first]==actual[first])++first;
        if(first==sizeof(PPCContext))return;
        struct Field {size_t at,size;const char* name;};
        const std::array fields{
            Field{offsetof(PPCContext,lastFunction),sizeof(uint32_t),"lastFunction"},
            Field{offsetof(PPCContext,traceIndex),sizeof(uint32_t),"traceIndex"},
            Field{offsetof(PPCContext,trace),sizeof(PPCContext::trace),"trace"},
            Field{offsetof(PPCContext,r3),sizeof(PPCRegister),"r3"},
            Field{offsetof(PPCContext,r0),sizeof(PPCRegister),"r0"},
            Field{offsetof(PPCContext,r1),sizeof(PPCRegister),"r1"},
            Field{offsetof(PPCContext,r2),sizeof(PPCRegister),"r2"},
            Field{offsetof(PPCContext,r4),offsetof(PPCContext,r31)+sizeof(PPCRegister)-offsetof(PPCContext,r4),"r4..r31"},
            Field{offsetof(PPCContext,lr),sizeof(uint64_t),"lr"},
            Field{offsetof(PPCContext,ctr),sizeof(PPCRegister),"ctr"},
            Field{offsetof(PPCContext,xer),sizeof(PPCXERRegister),"xer"},
            Field{offsetof(PPCContext,reserved),sizeof(PPCRegister),"reserved"},
            Field{offsetof(PPCContext,msr),sizeof(uint32_t),"msr"},
            Field{offsetof(PPCContext,cr0),offsetof(PPCContext,cr7)+sizeof(PPCCRRegister)-offsetof(PPCContext,cr0),"cr0..cr7"},
            Field{offsetof(PPCContext,fpscr),sizeof(PPCFPSCRRegister),"fpscr"},
            Field{offsetof(PPCContext,vscr_sat),sizeof(uint8_t),"vscr_sat"},
            Field{offsetof(PPCContext,f0),offsetof(PPCContext,f31)+sizeof(PPCRegister)-offsetof(PPCContext,f0),"f0..f31"},
            Field{offsetof(PPCContext,v0),offsetof(PPCContext,v127)+sizeof(PPCVRegister)-offsetof(PPCContext,v0),"v0..v127"}};
        const char* name="padding";const char* previous="begin";const char* next="end";size_t relative=first,priorEnd{},nextStart=sizeof(PPCContext);
        for(const auto& field:fields) {
            if(first>=field.at&&first<field.at+field.size){name=field.name;relative=first-field.at;}
            if(field.at+field.size<=first&&field.at+field.size>=priorEnd){priorEnd=field.at+field.size;previous=field.name;}
            if(field.at>first&&field.at<nextStart){nextStart=field.at;next=field.name;}
        }
        std::fprintf(stderr,"VM_CONTEXT_DIFF phase=%s case=%zu record=%zu kind=%08X byte=%zu field=%s field_byte=%zu expected=%02X actual=%02X previous=%s previous_end=%zu next=%s next_at=%zu sizeof=%zu\n",
            phase,ordinal,record,uint32_t(kind),first,name,relative,unsigned(expected[first]),unsigned(actual[first]),previous,priorEnd,next,nextStart,sizeof(PPCContext));
    }
    void auditStorage(const char* phase,const std::vector<Call>& records,const std::vector<Representation>& captured)const {
        need(records.size()==captured.size(),"VM boundary diagnostic transport count changed");
        for(size_t i=0;i<records.size();++i) {
            const auto& call=records[i];const auto* transported=call.context.data();
            diagnose(phase,ordinal,i,call.kind,captured[i].data(),transported);
            if(std::memcmp(captured[i].data(),transported,sizeof(PPCContext)))
                std::fprintf(stderr,"VM_STORAGE_CASE owner=%08X function=%08X injected=%08X mode=%u relocate=%u advance_top=%u returned=%u\n",
                    owner,function,uint32_t(injected),unsigned(mode),unsigned(relocate),unsigned(advanceTop),returned);
            need(!std::memcmp(captured[i].data(),transported,sizeof(PPCContext)),"VM boundary snapshot transport did not preserve captured object bytes");
        }
    }
    std::vector<uint32_t> deltas()const {
        std::vector<uint32_t> out;const auto watch=rt.writeWatch();
        for(size_t i=0;i<pages.size();++i)out.push_back(watch.version[pages[i]].load(std::memory_order_acquire)-versions[i]);
        return out;
    }
    void invoke(Kind kind,PPCContext& ctx,uint8_t* base) {
        need(base==rt.base,"VM boundary received a foreign guest base");
        Call call;call.kind=kind;call.error=GetLastError();std::memcpy(call.context.data(),&ctx,sizeof(ctx));call.watch=deltas();
        withOpenFixture(rt,[&]{for(const auto& [at,size]:regions) {
            const auto offset=call.memory.size();call.memory.resize(offset+size);std::memcpy(call.memory.data()+offset,base+at,size);
        }});
        // Independent shadows audit both the new snapshot move and any existing
        // records moved by vector growth; padding remains part of the proof.
        std::vector<Representation> captured(calls.size()+1);
        for(size_t i=0;i<calls.size();++i)std::memcpy(captured[i].data(),calls[i].context.data(),sizeof(PPCContext));
        std::memcpy(captured.back().data(),&ctx,sizeof(ctx));
        calls.push_back(std::move(call));auditStorage("capture_move_or_reallocation",calls,captured);
        uint32_t expectedLR{};
        switch(kind) {
        case Kind::Coerce:expectedLR=0x82844ADC;need(ctx.r4.u32==function,"VM coercion function argument changed");break;
        case Kind::Frames:expectedLR=0x82844B4C;need(ctx.r4.u32==uint32_t(PPCExternalFastLoad16(base,owner+44))*2,"VM frame growth capacity changed");break;
        case Kind::Stack: {
            need(ctx.lr==0x82844BD8||ctx.lr==0x82844CA0,"VM stack growth continuation changed");expectedLR=uint32_t(ctx.lr);
            const uint32_t size=PPCExternalFastLoad32(base,owner+32);
            const uint32_t max=ctx.lr==0x82844BD8?PPCExternalFastLoad8(base,ctx.r30.u32+71):20;
            const uint32_t expected=ctx.lr==0x82844BD8?(int32_t(max)>int32_t(size)?size+max+5:size*2):(int32_t(size)>=20?size*2:size+25);
            need(ctx.r4.u32==expected,"VM original stack growth arithmetic changed");break;
        }
        case Kind::Varargs:expectedLR=0x82844B98;
            need(ctx.r5.u32==function+8&&ctx.r4.u32==PPCExternalFastLoad8(base,ctx.r30.u32+69),"VM vararg arguments changed");break;
        case Kind::Error:expectedLR=0x82844B68;break;
        case Kind::Hook:expectedLR=0x82844CFC;need(ctx.r4.u64==0&&ctx.r5.s64==-1,"VM hook arguments changed");break;
        case Kind::Exit:expectedLR=0x82844B40;need(ctx.r3.u64==1,"VM terminal exit argument changed");break;
        case Kind::Panic:expectedLR=0x82844B38;need(ctx.ctr.u32==uint32_t(kind),"VM panic target changed");break;
        case Kind::Closure:expectedLR=0x82844D14;need(ctx.ctr.u32==uint32_t(kind),"VM closure target changed");break;
        }
        need(ctx.lr==expectedLR,"VM boundary LR changed");
        if(kind!=Kind::Exit)need(ctx.r3.u32==owner,"VM boundary owner argument changed");
        PPC_TRACE_ENTRY(ctx,uint32_t(kind));
        const uint32_t argument=ctx.r4.u32;
        // Deliberately clobber caller-volatile state; the original nonvolatile
        // saves, original post-call reloads and exact FP propagation must win.
        const std::array registers{&ctx.r3,&ctx.r4,&ctx.r5,&ctx.r6,&ctx.r7,&ctx.r8,&ctx.r9,&ctx.r10,&ctx.r11,&ctx.r12};
        for(size_t i=0;i<registers.size();++i)registers[i]->u64=0xAABBCCDD10203040ull+i*0x10203;
        ctx.cr0.lt=1;ctx.cr0.gt=0;ctx.cr0.eq=0;ctx.cr0.so=1;
        ctx.f0.u64=0x7FF8123456789ABC;ctx.v0.u64[0]=0x8070605040302010;
        ctx.lr=0x1234567887654321;ctx.fpscr.csr^=1u<<PPCFPSCRRegister::RoundShift;ctx.fpscr.setcsr(ctx.fpscr.csr);
        if(kind==injected&&mode!=Mode::Success) {
            PPCExternalFastStore32(base,0x1D000,0x72519AC3);
            if(mode==Mode::Throw)throw Simpsons::Failure("VM boundary injected partial failure");
            rt.requestStop("VM boundary injected cancellation");PPC_TRACE_ENTRY(ctx,uint32_t(kind)+4);
        }
        switch(kind) {
        case Kind::Coerce:PPCExternalFastStore32(base,function,6);ctx.r3.u64=function;break;
        case Kind::Frames: {
            const uint32_t old=PPCExternalFastLoad32(base,owner+20);
            if(relocate) {
                for(uint32_t i=0;i<24;i+=4)PPCExternalFastStore32(base,relocatedFrame+i,PPCExternalFastLoad32(base,old+i));
                PPCExternalFastStore32(base,owner+20,relocatedFrame);
            }
            PPCExternalFastStore32(base,owner+36,relocatedFrame+0x100);
            // Test-only post-growth size crosses the original limit-error
            // branch; an initially oversized frame count instead invokes panic.
            if(injected==Kind::Error)PPCExternalFastStore16(base,owner+44,4097);
            break;
        }
        case Kind::Stack: {
            const uint32_t old=PPCExternalFastLoad32(base,owner+28),top=PPCExternalFastLoad32(base,owner+8);
            if(relocate) {
                need(top>=old&&top-old<=0x400,"VM mock relocation is outside its bounded source");
                for(uint32_t i=0;i<0x400;i+=4)PPCExternalFastStore32(base,relocatedStack+i,PPCExternalFastLoad32(base,old+i));
                PPCExternalFastStore32(base,owner+28,relocatedStack);PPCExternalFastStore32(base,owner+8,relocatedStack+top-old);
            }
            const uint32_t stack=PPCExternalFastLoad32(base,owner+28);
            PPCExternalFastStore32(base,owner+24,stack+0x800);PPCExternalFastStore32(base,owner+32,argument);break;
        }
        case Kind::Varargs:if(advanceTop)PPCExternalFastStore32(base,owner+8,PPCExternalFastLoad32(base,owner+8)+8);break;
        case Kind::Hook:break;
        case Kind::Closure:if(advanceTop)PPCExternalFastStore32(base,owner+8,PPCExternalFastLoad32(base,owner+8)+24);ctx.r3.u64=returned;break;
        case Kind::Error:throw Simpsons::Failure("VM mock original limit error boundary");
        case Kind::Exit:throw Simpsons::Failure("VM mock original terminal exit boundary");
        case Kind::Panic:break;
        }
    }
};
VMSetupBoundary* VMSetupBoundary::current{};
void vmSetupClosure(PPCContext& ctx,uint8_t* base){
    if(!VMSetupBoundary::current)throw Simpsons::Failure("Unexpected VM closure outside differential fixture");
    VMSetupBoundary::current->invoke(VMSetupBoundary::Kind::Closure,ctx,base);
}
void vmSetupPanic(PPCContext& ctx,uint8_t* base){
    if(!VMSetupBoundary::current)throw Simpsons::Failure("Unexpected VM panic outside differential fixture");
    VMSetupBoundary::current->invoke(VMSetupBoundary::Kind::Panic,ctx,base);
}
void fastVMSetupContract(Simpsons::Runtime& rt) {
    const uint32_t physical=rt.allocatePhysical(0,0x20000,PAGE_READWRITE,0x100000,0x1FFFFF,0x10000);
    need(physical!=0,"VM setup physical fixture allocation failed");
    const uint32_t offset=rt.physicalAddress(physical),canonical=0xA0000000u+offset;
    constexpr uint32_t dispatch=uint32_t(PPC_IMAGE_BASE+PPC_IMAGE_SIZE);
    rt.map(dispatch,0x1000,true,"VM test-only indirect dispatch",Simpsons::MemoryUse::Host);
    PPC_LOOKUP_FUNC(rt.base,0x82230000)=vmSetupPanic;PPC_LOOKUP_FUNC(rt.base,0x82230004)=vmSetupClosure;
    // Slots are owned private test mappings, not an installed game's dispatch.
    struct Clear {Simpsons::Runtime& rt;uint32_t physical;~Clear(){VMSetupBoundary::current=nullptr;
        PPC_LOOKUP_FUNC(rt.base,0x82230000)=nullptr;PPC_LOOKUP_FUNC(rt.base,0x82230004)=nullptr;rt.freePhysical(physical);}} clear{rt,physical};
    struct Region {uint32_t start;std::vector<uint8_t> initial,expected;};
    std::array<Region,3> regions{{{0x10000,std::vector<uint8_t>(0x40000),std::vector<uint8_t>(0x40000)},
        {canonical,std::vector<uint8_t>(0x20000),std::vector<uint8_t>(0x20000)},
        {dispatch,std::vector<uint8_t>(0x1000),std::vector<uint8_t>(0x1000)}}};
    std::memcpy(regions[2].initial.data(),rt.base+dispatch,0x1000);
    const auto put=[&](uint32_t address,std::span<const uint8_t> bytes) {
        for(auto& region:regions)if(address>=region.start&&uint64_t(address)+bytes.size()<=uint64_t(region.start)+region.initial.size()) {
            std::copy(bytes.begin(),bytes.end(),region.initial.begin()+(address-region.start));return;
        }
        throw Simpsons::Failure("VM fixture seed is outside owned memory");
    };
    const auto word=[&](uint32_t address,uint32_t value) {const std::array<uint8_t,4> bytes{uint8_t(value>>24),uint8_t(value>>16),uint8_t(value>>8),uint8_t(value)};put(address,bytes);};
    const auto half=[&](uint32_t address,uint16_t value) {const std::array<uint8_t,2> bytes{uint8_t(value>>8),uint8_t(value)};put(address,bytes);};
    const auto byte=[&](uint32_t address,uint8_t value){put(address,std::span<const uint8_t>(&value,1));};
    struct Options {
        uint32_t owner{0x10100},function{0x10800},closure{0x10900},proto{0x10A00},frame{0x11000},top{},room{0x800},size{64},
            type{6},target{0x82230004},panic{0x82230000},stack{0x13F80},returned{};
        uint16_t frames{16};uint8_t max{4},varargs{},parameters{2},hook{};
        bool c{},growFrames{},relocate{},advanceTop{},entryStop{},contextAlias{},nonlocal{},liveEmptyFrame{};
        VMSetupBoundary::Kind injected{VMSetupBoundary::Kind::Stack};VMSetupBoundary::Mode mode{VMSetupBoundary::Mode::Success};
        uint32_t csr{PPCFPSCRRegister::DefaultCSR};
        std::vector<VMSetupBoundary::Kind> expectedCalls;
    };
    std::vector<uint32_t> pages;
    for(const auto& region:regions)for(uint32_t page=region.start>>12;page<(region.start+region.initial.size())>>12;++page)pages.push_back(page);
    const auto watch=rt.writeWatch();
    const auto setup=[&](const Options& o,uint32_t functionPointer=0) {
        for(size_t n=0;n<2;++n)for(size_t i=0;i<regions[n].initial.size();++i)regions[n].initial[i]=uint8_t(i*71+19);
        const uint32_t function=functionPointer?functionPointer:o.function;
        word(o.owner+8,o.top?o.top:function+8);word(o.owner+12,function+8);word(o.owner+16,0x10B00);
        word(o.owner+20,o.frame);word(o.owner+24,(o.top?o.top:function+8)+o.room);word(o.owner+28,function-0x100);
        word(o.owner+32,o.size);word(o.owner+36,o.frame+(o.growFrames?24:0x100));half(o.owner+44,o.frames);byte(o.owner+48,o.hook);
        word(o.owner+80,o.nonlocal?0x10C00:0);word(0x10B00+40,o.panic);word(o.function,o.type);word(o.function+4,o.closure);
        byte(o.closure+6,uint8_t(o.c));word(o.closure+12,o.c?o.target:o.proto);
        word(o.proto+12,0x10E00);byte(o.proto+69,o.parameters);byte(o.proto+70,o.varargs);byte(o.proto+71,o.max);
    };
    size_t ordinal{};
    const auto exercise=[&](const Options& o,uint32_t ownerAlias=0,uint32_t functionAlias=0) {
        PPCContext initial{};auto* bytes=reinterpret_cast<uint8_t*>(&initial);
        for(size_t i=0;i<sizeof(initial);++i)bytes[i]=uint8_t(i*37+11);
        initial.r1.u64=0x1234567800000000ull|o.stack;initial.r3.u64=0x9182736400000000ull|(ownerAlias?ownerAlias:o.owner);
        initial.r4.u64=0xABCDEFAB00000000ull|(functionAlias?functionAlias:o.function);initial.lr=0x98765432ABCDCBA0;
        initial.r28.u64=0x1020304050607080;initial.traceIndex=63;initial.fpscr.csr=o.csr;initial.vscr_sat=0x5A;
        if(o.contextAlias)std::memcpy(&initial.f14,regions[0].initial.data()+o.function-0x10000,sizeof(initial.f14));
        VMSetupBoundary boundary{rt,{},pages,{},ownerAlias?ownerAlias:o.owner,functionAlias?functionAlias:o.function,
            0x12000,0x11800,o.returned,o.injected,o.mode,o.relocate,o.advanceTop,{}};
        boundary.ordinal=++ordinal;
        for(const auto& region:regions)boundary.regions.emplace_back(region.start,region.initial.size());
        VMSetupBoundary::current=&boundary;
        const auto invoke=[&](Function function,PPCContext& result) {
            rt.stopping=false;PPCStopRequested=0;ResetEvent(rt.stopEvent);
            withOpenFixture(rt,[&]{for(const auto& region:regions)std::memcpy(rt.base+region.start,region.initial.data(),region.initial.size());});
            PPCContext local{};PPCContext* context=o.contextAlias?reinterpret_cast<PPCContext*>(rt.base+0x18000):&local;
            std::memcpy(context,&initial,sizeof(initial));boundary.versions.clear();
            for(uint32_t page:pages)boundary.versions.push_back(watch.armPage(page));
            if(o.entryStop)rt.requestStop("VM setup entry cancellation");
            SetLastError(0x41ACED29);Outcome outcome;
            if(o.liveEmptyFrame) {PPCNonlocalFrame frame(*context,rt.base);outcome=run(function,*context,rt.base);}
            else outcome=run(function,*context,rt.base);
            const DWORD error=GetLastError();std::memcpy(&result,context,sizeof(result));
            return std::tuple{outcome,boundary.deltas(),error};
        };
        PPCContext reference{},actual{};const auto expected=invoke(referenceFastVMSetup,reference);
        std::vector<VMSetupBoundary::Representation> captured(boundary.calls.size());
        for(size_t i=0;i<boundary.calls.size();++i)std::memcpy(captured[i].data(),boundary.calls[i].context.data(),sizeof(PPCContext));
        const auto calls=boundary.calls;boundary.auditStorage("reference_vector_copy",calls,captured);
        withOpenFixture(rt,[&]{for(auto& region:regions)std::memcpy(region.expected.data(),rt.base+region.start,region.expected.size());});
        boundary.calls.clear();const auto result=invoke(sub_82844AB0,actual);
        need(result==expected,"VM setup failure/FP/write-watch/LastError changed");
        need(!std::memcmp(&reference,&actual,sizeof(actual)),"VM setup full PPC state/padding/trace changed");
        need(withOpenFixture(rt,[&]{for(const auto& region:regions) {
            if(std::memcmp(region.expected.data(),rt.base+region.start,region.expected.size()))return false;
        }return true;}),"VM setup full owned memory or partial-write order changed");
        need(calls.size()==boundary.calls.size(),"VM setup boundary call count changed");
        std::vector<VMSetupBoundary::Kind> actualCalls;
        for(const auto& call:boundary.calls)actualCalls.push_back(call.kind);
        need(actualCalls==o.expectedCalls,"VM setup did not reach its independently expected boundary order/count");
        for(size_t i=0;i<calls.size();++i) {
            if(calls[i].kind!=boundary.calls[i].kind||std::memcmp(calls[i].context.data(),boundary.calls[i].context.data(),sizeof(PPCContext))) {
                std::fprintf(stderr,"VM_CASE owner=%08X function=%08X c=%u frames=%u room=%u size=%u max=%u varargs=%u hook=%u stack=%08X context_alias=%u injected=%08X mode=%u\n",
                    ownerAlias?ownerAlias:o.owner,functionAlias?functionAlias:o.function,unsigned(o.c),unsigned(o.frames),o.room,o.size,unsigned(o.max),unsigned(o.varargs),unsigned(o.hook),o.stack,unsigned(o.contextAlias),uint32_t(o.injected),unsigned(o.mode));
                VMSetupBoundary::diagnose("reference_vs_candidate",boundary.ordinal,i,calls[i].kind,
                    calls[i].context.data(),boundary.calls[i].context.data());
            }
            need(calls[i].kind==boundary.calls[i].kind&&!std::memcmp(calls[i].context.data(),boundary.calls[i].context.data(),sizeof(PPCContext)),"VM setup complete boundary context/order changed");
            need(calls[i].memory==boundary.calls[i].memory&&calls[i].watch==boundary.calls[i].watch&&calls[i].error==boundary.calls[i].error,"VM setup boundary memory/watch/LastError changed");
        }
        if(o.mode!=VMSetupBoundary::Mode::Success) {
            need(std::any_of(boundary.calls.begin(),boundary.calls.end(),[&](const auto& call){return call.kind==o.injected;}),"VM injected boundary was not reached");
            need(!std::get<0>(result).failure.empty()&&PPCExternalFastLoad32(rt.base,0x1D000)==0x72519AC3,"VM boundary partial failure/cancellation was not preserved");
        }
        if(std::get<0>(result).failure.empty()) {
            need(actual.r1.u64==initial.r1.u64&&actual.lr==uint32_t(initial.lr)&&actual.r12.u64==uint32_t(initial.lr),"VM setup original stack/LR save-restore changed");
            need(actual.r28.u64==initial.r28.u64&&actual.r29.u64==initial.r29.u64&&actual.r30.u64==initial.r30.u64&&actual.r31.u64==initial.r31.u64,"VM setup original nonvolatile restore changed");
        }
        rt.stopping=false;PPCStopRequested=0;ResetEvent(rt.stopEvent);VMSetupBoundary::current=nullptr;
        return std::pair{std::get<0>(result),actual.r3.u64};
    };
    for(uint32_t round=0;round<4;++round)for(bool flush:{false,true})for(uint8_t n:{uint8_t(0),uint8_t(1),uint8_t(4)}) {
        Options o;o.max=n;o.csr=PPCFPSCRRegister::DefaultCSR|(round<<PPCFPSCRRegister::RoundShift)|(flush?PPCFPSCRRegister::FlushMask:0);
        setup(o);const auto [outcome,returned]=exercise(o);need(outcome.failure.empty()&&returned==0,"VM local branch failed");
        need(PPCExternalFastLoad32(rt.base,o.owner+8)==o.function+8+uint32_t(n)*8,"VM nil final top changed");
        for(uint32_t i=0;i<n;++i) {
            const uint32_t at=o.function+8+i*8;need(PPCExternalFastLoad32(rt.base,at)==0,"VM nil type word not zero");
            need(!std::memcmp(rt.base+at+4,regions[0].initial.data()+at+4-0x10000,4),"VM nil erased payload second word");
        }
    }
    for(bool c:{false,true})for(uint32_t size:{1u,20u,64u}) {
        Options o;o.c=c;o.type=0;o.growFrames=true;o.room=0;o.size=size;o.relocate=true;o.varargs=1;o.advanceTop=true;o.hook=1;o.returned=2;
        o.expectedCalls=c?std::vector{VMSetupBoundary::Kind::Coerce,VMSetupBoundary::Kind::Frames,VMSetupBoundary::Kind::Stack,
            VMSetupBoundary::Kind::Hook,VMSetupBoundary::Kind::Closure}:std::vector{VMSetupBoundary::Kind::Coerce,VMSetupBoundary::Kind::Frames,
            VMSetupBoundary::Kind::Varargs,VMSetupBoundary::Kind::Stack};
        setup(o);const auto [outcome,value]=exercise(o);
        need(outcome.failure.empty()&&(c?uint32_t(value)==PPCExternalFastLoad32(rt.base,o.owner+8)-o.returned*8:value==0),
             "VM bounded coercion/growth/hook branch did not produce its known successful result");
        need(PPCExternalFastLoad32(rt.base,o.owner+28)==0x12000,"VM stack relocation was not used");
    }
    for(uint32_t room:{160u,161u})for(uint32_t returned:{0u,1u,5u}) {
        Options o;o.c=true;o.room=room;o.returned=returned;o.hook=1;o.advanceTop=true;
        if(room==160)o.expectedCalls.push_back(VMSetupBoundary::Kind::Stack);
        o.expectedCalls.push_back(VMSetupBoundary::Kind::Hook);o.expectedCalls.push_back(VMSetupBoundary::Kind::Closure);setup(o);
        const auto [outcome,value]=exercise(o);need(outcome.failure.empty()&&uint32_t(value)==PPCExternalFastLoad32(rt.base,o.owner+8)-returned*8,"VM closure return count arithmetic changed");
    }
    for(auto kind:{VMSetupBoundary::Kind::Coerce,VMSetupBoundary::Kind::Frames,VMSetupBoundary::Kind::Stack,VMSetupBoundary::Kind::Varargs,
                   VMSetupBoundary::Kind::Hook,VMSetupBoundary::Kind::Closure,VMSetupBoundary::Kind::Panic,VMSetupBoundary::Kind::Exit,VMSetupBoundary::Kind::Error}) {
        for(auto mode:{VMSetupBoundary::Mode::Throw,VMSetupBoundary::Mode::Cancel}) {
            Options o;o.type=0;o.growFrames=true;o.room=0;o.varargs=1;o.hook=1;o.c=kind==VMSetupBoundary::Kind::Hook||kind==VMSetupBoundary::Kind::Closure;
            if(kind==VMSetupBoundary::Kind::Panic||kind==VMSetupBoundary::Kind::Exit)o.frames=4097;
            if(kind==VMSetupBoundary::Kind::Error)o.frames=4096;
            o.expectedCalls={VMSetupBoundary::Kind::Coerce};
            switch(kind) {
            case VMSetupBoundary::Kind::Coerce:break;
            case VMSetupBoundary::Kind::Frames:o.expectedCalls.push_back(kind);break;
            case VMSetupBoundary::Kind::Varargs:o.expectedCalls.insert(o.expectedCalls.end(),{VMSetupBoundary::Kind::Frames,kind});break;
            case VMSetupBoundary::Kind::Stack:o.expectedCalls.insert(o.expectedCalls.end(),{VMSetupBoundary::Kind::Frames,VMSetupBoundary::Kind::Varargs,kind});break;
            case VMSetupBoundary::Kind::Hook:o.expectedCalls.insert(o.expectedCalls.end(),{VMSetupBoundary::Kind::Frames,VMSetupBoundary::Kind::Stack,kind});break;
            case VMSetupBoundary::Kind::Closure:o.expectedCalls.insert(o.expectedCalls.end(),{VMSetupBoundary::Kind::Frames,VMSetupBoundary::Kind::Stack,VMSetupBoundary::Kind::Hook,kind});break;
            case VMSetupBoundary::Kind::Panic:o.expectedCalls.push_back(kind);break;
            case VMSetupBoundary::Kind::Exit:o.expectedCalls.insert(o.expectedCalls.end(),{VMSetupBoundary::Kind::Panic,kind});break;
            case VMSetupBoundary::Kind::Error:o.expectedCalls.insert(o.expectedCalls.end(),{VMSetupBoundary::Kind::Frames,kind});break;
            }
            o.injected=kind;o.mode=mode;setup(o);exercise(o);
        }
    }
    // Reload-dependent alias: first nil type store overwrites the current
    // frame's end cell, so the original reload terminates after one iteration.
    Options alias;alias.function=alias.frame+16;alias.top=alias.frame+28;alias.max=2;setup(alias);
    need(exercise(alias).first.failure.empty()&&PPCExternalFastLoad32(rt.base,alias.owner+8)==0,"VM aliased frame-end reload changed");
    need(PPCExternalFastLoad32(rt.base,alias.frame+28)==0&&PPCExternalFastLoad32(rt.base,alias.frame+32)==8,
         "VM aliased nil write erased its payload or failed to clear the frame-end word");
    Options context;context.c=true;context.contextAlias=true;context.function=0x18000+uint32_t(offsetof(PPCContext,f14));
    context.expectedCalls={VMSetupBoundary::Kind::Closure};setup(context);
    const auto [contextOutcome,contextValue]=exercise(context);
    need(contextOutcome.failure.empty()&&uint32_t(contextValue)==context.function+8,
         "VM guest-backed context alias did not reach a successful closure result");
    for(uint32_t address:{physical,canonical,0xC0000000u+offset,0xDFFFF000u+offset}) {
        Options o;o.owner=canonical+0x201;o.function=canonical+0xFFFD;o.c=true;o.hook=1;
        o.expectedCalls={VMSetupBoundary::Kind::Hook,VMSetupBoundary::Kind::Closure};setup(o,address+0xFFFD);
        const auto [closureOutcome,closureValue]=exercise(o,address+0x201,address+0xFFFD);
        need(closureOutcome.failure.empty()&&uint32_t(closureValue)==address+0xFFFD+8,"VM valid physical closure alias did not succeed");
        // All stored stack/top pointers use the same guest aperture as the
        // function argument. Numerically mixed aliases would describe a huge
        // original nil range rather than a bounded valid VM stack.
        o.c=false;o.max=4;o.expectedCalls.clear();setup(o,address+0xFFFD);
        const auto [nilOutcome,nilValue]=exercise(o,address+0x201,address+0xFFFD);
        need(nilOutcome.failure.empty()&&nilValue==0&&PPCExternalFastLoad32(rt.base,o.owner+8)==address+0xFFFD+8+32,
             "VM valid physical nil alias did not produce its bounded successful result");
    }
    for(uint8_t access:{uint8_t(0),uint8_t(1)}) {
        setGuestPage(rt,0x14,access);
        Options o;o.function=0x13FF4;o.max=2;setup(o);const auto result=exercise(o);
        need(!result.first.failure.empty()&&PPCExternalFastLoad32(rt.base,o.owner+8)==0x14004,"VM late nil fault lost previous top advance");
        need(PPCExternalFastLoad32(rt.base,0x13FFC)==0,"VM late nil fault lost previous type store");
        need(withOpenFixture(rt,[&]{return !std::memcmp(rt.base+0x14000,regions[0].initial.data()+0x4000,4);}),"VM partial nil failure erased first payload second word");
        o=Options{};o.function=0x13FF6;setup(o);
        need(!exercise(o).first.failure.empty()&&PPCExternalFastLoad32(rt.base,o.owner+8)==0x13FFE,
             "VM straddled nil store did not fault before top advancement");
        o=Options{};o.owner=0x13FD3;o.growFrames=true;
        if(access==1)o.expectedCalls={VMSetupBoundary::Kind::Frames};
        setup(o);
        need(exercise(o).first.failure.empty()==(access==1),"VM protected crossing U16 read had the wrong admission");
        o=Options{};o.proto=0x13FB9;setup(o);
        need(exercise(o).first.failure.empty()==(access==1),"VM protected U8 source had the wrong admission");
        o=Options{};o.proto=0x13FB8;o.max=1;setup(o);need(exercise(o).first.failure.empty(),"VM last-byte U8 read touched next page");
        o=Options{};o.frame=0x13FDC;setup(o);
        need(!exercise(o).first.failure.empty(),"VM later protected frame write did not fault");
        need(PPCExternalFastLoad32(rt.base,o.owner+20)==0x13FF4&&PPCExternalFastLoad32(rt.base,0x13FF4)==o.function+8&&
             PPCExternalFastLoad32(rt.base,0x13FF8)==o.function+8+uint32_t(o.max)*8,"VM frame fault lost its earlier owner/base/end stores");
        need(withOpenFixture(rt,[&]{return !std::memcmp(rt.base+0x13FFC,regions[0].initial.data()+0x3FFC,16);}),
             "VM frame fault changed untouched flags or later protected frame words");
        o=Options{};o.stack=0x14040;setup(o);
        need(!exercise(o).first.failure.empty(),"VM first original save28 store did not fault");
        o=Options{};o.stack=0x14020;setup(o);
        need(!exercise(o).first.failure.empty(),"VM second original save28 store did not fault");
        const std::array<uint8_t,8> saved28{0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80};
        need(!std::memcmp(rt.base+0x13FF8,saved28.data(),saved28.size()),"VM partial original save28 lost the completed big-endian r28 store");
        need(withOpenFixture(rt,[&]{return !std::memcmp(rt.base+0x14000,regions[0].initial.data()+0x4000,32);}),
             "VM partial original save28 changed its faulting or later stores");
    }
    setGuestPage(rt,0x14,3);
    for(uint32_t aperture:{0x82000600u,0xFFD00000u,0x70000000u}) {
        Options o;setup(o);
        need(!exercise(o,aperture).first.failure.empty(),"VM invalid owner aperture did not fault");
        need(!exercise(o,0,aperture).first.failure.empty(),"VM invalid function aperture did not fault");
        o.c=true;o.target=aperture;setup(o);
        need(!exercise(o).first.failure.empty(),"VM original indirect dispatcher accepted an aperture target");
    }
    Options missing;missing.c=true;missing.target=0x82230008;setup(missing);
    need(!exercise(missing).first.failure.empty(),"VM original indirect dispatcher accepted a missing target");
    missing.target=0x82230005;setup(missing);need(!exercise(missing).first.failure.empty(),"VM original indirect dispatcher accepted an unaligned target");
    Options jump;jump.growFrames=true;jump.frames=4097;jump.nonlocal=true;setup(jump);
    need(exercise(jump).first.failure.find("no live save")!=std::string::npos,"VM real nonlocal entry accepted no live save");
    jump.liveEmptyFrame=true;setup(jump);need(exercise(jump).first.failure.find("no live save")!=std::string::npos,"VM empty native frame accepted an unsaved jump buffer");
    Options stopped;stopped.entryStop=true;setup(stopped);need(!exercise(stopped).first.failure.empty(),"VM entry cancellation was ignored");
    std::printf("AUDIT_VM_SETUP differential=full_context_memory_watch boundary_mocks=caller_contract_only real_save_restore28=passed real_indirect_rejection=passed real_nonlocal_missing_save=passed registered_jump_restore=separate_NonlocalJumpTests\n");
}
// Whole original pair-to-bitset body: scalar sites use the external hardware-fast
// reference, while its two original vector expressions and save/rest29 remain real.
// All loops below have explicit finite owned tables; no allocator/callback model.
void fastPairBitsetContract(Simpsons::Runtime& rt) {
    const uint32_t physical=rt.allocatePhysical(0,0x20000,PAGE_READWRITE,0x100000,0x1FFFFF,0x10000);
    need(physical!=0,"Pair bitset physical fixture allocation failed");
    struct Free {Simpsons::Runtime& rt;uint32_t address;~Free(){rt.freePhysical(address);}} free{rt,physical};
    const uint32_t offset=rt.physicalAddress(physical),canonical=0xA0000000u+offset;
    struct Region {uint32_t start;std::vector<uint8_t> initial,expected;};
    std::array<Region,2> regions{{{0x10000,std::vector<uint8_t>(0x10000),std::vector<uint8_t>(0x10000)},
                                {canonical,std::vector<uint8_t>(0x20000),std::vector<uint8_t>(0x20000)}}};
    const auto put=[&](uint32_t address,std::span<const uint8_t> bytes) {
        for(auto& region:regions)if(address>=region.start&&uint64_t(address)+bytes.size()<=uint64_t(region.start)+region.initial.size()) {
            std::copy(bytes.begin(),bytes.end(),region.initial.begin()+(address-region.start));return;
        }
        throw Simpsons::Failure("Pair bitset seed is outside owned memory");
    };
    const auto half=[&](uint32_t address,uint16_t value) {
        const std::array<uint8_t,2> bytes{uint8_t(value>>8),uint8_t(value)};put(address,bytes);
    };
    const auto word=[&](uint32_t address,uint32_t value) {
        const std::array<uint8_t,4> bytes{uint8_t(value>>24),uint8_t(value>>16),uint8_t(value>>8),uint8_t(value)};put(address,bytes);
    };
    struct Options {
        uint32_t owner{0x10100},descriptor{0x10200},entries{0x11000},headers{0x11800},checkpoint{0x12000},replay{0x12200};
        uint32_t output{0x13000},stack{0x13F80},ownerInput{},descriptorInput{},outputInput{};
        uint16_t low{2},high{6},exclude{66};int32_t clearBits{};uint32_t query{1},shift{16};bool checkpoints{};
    };
    // Packed codes address three owned output words. Entry0 is deliberately skipped
    // by the original cursor; flags1 in entries2/4/8 test the upper-endpoint filter.
    constexpr std::array<uint16_t,9> codes{0,0,33,66,3,36,69,6,39};
    constexpr std::array<uint16_t,9> flags{0,0,1,0,1,0,0,0,1};
    const auto setup=[&](const Options& o) {
        for(auto& region:regions)for(size_t i=0;i<region.initial.size();++i)region.initial[i]=uint8_t(i*71+19);
        for(uint32_t i=0;i<8;++i)word(o.output+4*i,0xA55A5AA5u+i);
        word(o.owner+112,o.headers);word(o.owner+124,o.entries);word(o.owner+160,o.checkpoints?1:0);
        word(o.owner+164,o.shift);word(o.owner+168,o.checkpoint);
        half(o.descriptor+8,o.low);half(o.descriptor+10,o.high);
        for(size_t i=0;i<codes.size();++i){half(o.entries+uint32_t(4*i),flags[i]);half(o.entries+uint32_t(4*i+2),codes[i]);}
        half(o.checkpoint,0);word(o.checkpoint+4,o.replay);word(o.checkpoint+8,3);
        half(o.replay,33);half(o.replay+2,66);half(o.replay+4,36);
        half(o.headers+8,0);half(o.headers+10,2);
    };
    std::vector<uint32_t> pages;
    for(const auto& region:regions)for(uint32_t p=region.start>>12;p<(region.start+region.initial.size())>>12;++p)pages.push_back(p);
    const auto watch=rt.writeWatch();
    const auto arm=[&]{std::vector<uint32_t> out;for(uint32_t p:pages)out.push_back(watch.armPage(p));return out;};
    const auto changes=[&](const std::vector<uint32_t>& before) {
        std::vector<uint32_t> out;
        for(size_t i=0;i<pages.size();++i) {
            out.push_back(watch.version[pages[i]].load(std::memory_order_acquire)-before[i]);
        }
        return out;
    };
    const auto reset=[&]{withOpenFixture(rt,[&]{for(const auto& region:regions)std::memcpy(rt.base+region.start,region.initial.data(),region.initial.size());});};
    const auto exercise=[&](const Options& o,uint32_t csr=PPCFPSCRRegister::DefaultCSR,bool entryStop=false) {
        PPCContext initial{};auto* raw=reinterpret_cast<uint8_t*>(&initial);
        for(size_t i=0;i<sizeof(initial);++i)raw[i]=uint8_t(i*37+11);
        initial.r1.u64=0x1234567800000000ull|o.stack;initial.r3.u64=0x9182736400000000ull|(o.ownerInput?o.ownerInput:o.owner);
        initial.r4.u64=0x8765432100000000ull|uint32_t(o.clearBits);initial.r5.u64=0x7654321000000000ull|o.query;
        initial.r6.u64=0xABCDEFAB00000000ull|(o.descriptorInput?o.descriptorInput:o.descriptor);
        initial.r7.u64=0xFEDCBAFE00000000ull|o.exclude;initial.r8.u64=0xAABBCCDD00000000ull|(o.outputInput?o.outputInput:o.output);
        initial.lr=0x98765432ABCDCBA0;initial.ctr.u64=0xABCDEF7654321011;initial.traceIndex=63;initial.fpscr.csr=csr;
        const auto invoke=[&](Function function,PPCContext& result) {
            rt.stopping=false;PPCStopRequested=0;ResetEvent(rt.stopEvent);
            if(entryStop)rt.requestStop("pair bitset entry cancellation");
            reset();std::memcpy(&result,&initial,sizeof(initial));const auto before=arm();SetLastError(0x41ACED29);
            const auto outcome=run(function,result,rt.base);const auto error=GetLastError();
            return std::tuple{outcome,changes(before),error};
        };
        PPCContext reference{},actual{};const auto expected=invoke(referenceFastPairBitset,reference);
        withOpenFixture(rt,[&]{for(auto& region:regions)std::memcpy(region.expected.data(),rt.base+region.start,region.expected.size());});
        const auto result=invoke(sub_82B2A1C0,actual);
        need(result==expected,"Pair bitset external-fast failure/CSR/write-watch/LastError changed");
        need(!std::memcmp(&reference,&actual,sizeof(actual)),"Pair bitset full raw PPC registers/vector/trace/FPSCR changed");
        need(withOpenFixture(rt,[&]{for(const auto& region:regions)if(std::memcmp(region.expected.data(),rt.base+region.start,region.expected.size()))return false;return true;}),
             "Pair bitset bytes/aliasing/partial-write order changed");
        if(std::get<0>(result).failure.empty()) {
            need(actual.r1.u64==initial.r1.u64&&actual.lr==uint32_t(initial.lr)&&actual.r12.u64==uint32_t(initial.lr),"Pair bitset original stack/LR restoration changed");
            need(actual.r29.u64==initial.r29.u64&&actual.r30.u64==initial.r30.u64&&actual.r31.u64==initial.r31.u64,"Pair bitset real rest29 did not restore nonvolatile registers");
            need(actual.v0.u64[0]==0&&actual.v0.u64[1]==0,"Pair bitset original v0 zeroing changed");
        }
        return std::pair{std::get<0>(result),actual};
    };
    const auto known=[&](const Options& o,std::array<uint32_t,3> gold) {
        setup(o);need(exercise(o).first.failure.empty(),"Pair bitset bounded known case failed");
        for(size_t i=0;i<gold.size();++i)need(PPCExternalFastLoad32(rt.base,o.output+uint32_t(4*i))==gold[i],"Pair bitset original encoded endpoint/checkpoint semantics changed");
    };
    Options o;known(o,{1,16,4}); // lower toggle; upper flag1 skips; endpoints excluded.
    o.checkpoints=true;known(o,{1,2,4}); // checkpoint/replay minus excluded66; prefix clear then endpoint replay.
    o.exclude=0xFFFF;known(o,{1,2,0}); // replay66 and upper entry3 cancel exactly.
    o=Options{};o.clearBits=-1;known(o,{0xA55A5AA4u,0xA55A5AB6u,0xA55A5AA3u});
    o=Options{};o.low=0;o.high=0;setup(o);need(exercise(o).first.failure.empty(),"Pair bitset zero endpoint case failed");
    for(uint32_t i=0;i<4;++i)need(PPCExternalFastLoad32(rt.base,o.output+4*i)==0,"Original clearBits0 must still clear one16-byte vector");
    need(PPCExternalFastLoad32(rt.base,o.output+16)==0xA55A5AA9u,"Pair bitset zero count cleared beyond its original one vector");
    for(uint32_t round=0;round<4;++round)for(bool flush:{false,true}) {
        const uint32_t csr=PPCFPSCRRegister::DefaultCSR|(round<<PPCFPSCRRegister::RoundShift)|(flush?PPCFPSCRRegister::FlushMask:0);
        for(uint16_t low:{uint16_t(0),uint16_t(1),uint16_t(2),uint16_t(6)})for(uint16_t high:{uint16_t(0),uint16_t(2),uint16_t(6),uint16_t(8)}) {
            o=Options{};o.low=low;o.high=high;setup(o);need(exercise(o,csr).first.failure.empty(),"Pair bitset valid endpoint/CSR case failed");
        }
        for(uint32_t query:{0u,1u,0xFFFFFFFFu}){o=Options{};o.checkpoints=true;o.query=query;setup(o);need(exercise(o,csr).first.failure.empty(),"Pair bitset valid checkpoint query/CSR case failed");}
    }
    for(int32_t bits:{-128,-1,0,127,128,255}){o=Options{};o.clearBits=bits;setup(o);need(exercise(o).first.failure.empty(),"Pair bitset valid clear-count case failed");}
    o=Options{};o.checkpoints=true;o.query=2;o.shift=15;setup(o);need(exercise(o).first.failure.empty(),"Pair bitset valid checkpoint shift case failed"); // same checkpoint via real signed shift.
    for(uint32_t at:{0x13001u,0x13002u,0x11010u,0x11011u,0x10200u}){o=Options{};o.output=at;setup(o);need(exercise(o).first.failure.empty(),"Pair bitset valid output alias case failed");}
    for(uint32_t alias:{physical,canonical,0xC0000000u+offset,0xDFFFF000u+offset}) {
        o=Options{};o.owner=canonical+0x100;o.descriptor=canonical+0x200;o.entries=canonical+0x1000;
        o.headers=canonical+0x1800;o.checkpoint=canonical+0x2000;o.replay=canonical+0x2200;
        o.output=canonical+0xFFFC;o.ownerInput=alias+0x100;o.descriptorInput=alias+0x200;o.outputInput=alias+0xFFFC;
        o.clearBits=128;o.checkpoints=true;setup(o);need(exercise(o).first.failure.empty(),"Pair bitset valid physical alias case failed");
    }
    for(uint8_t access:{uint8_t(0),uint8_t(1)}) {
        setGuestPage(rt,0x14,access);
        for(uint32_t at:{0x13FF8u,0x14008u}){o=Options{};o.output=at;setup(o);need(!exercise(o).first.failure.empty(),"Pair bitset protected output did not fault");}
        o=Options{};o.stack=0x14010;setup(o);need(!exercise(o).first.failure.empty(),"Pair bitset partial real save29 did not fault");
        if(access==0) {
            o=Options{};o.stack=0x15020;setup(o);const auto [outcome,state]=exercise(o);
#if !defined(SIMPSONS_FAST_GUEST_MEMORY) || !SIMPSONS_FAST_GUEST_MEMORY
            need(!outcome.failure.empty(),"Checked pair-bitset stack vector pointer must remain faulting");
#endif
            // The original nonvolatile vector load is overwritten before use and may be elided.
            // Its helper and observable outcome must still match the compiled original reference.
            if(!outcome.failure.empty()) {
                PPCContext seeded{};auto* raw=reinterpret_cast<uint8_t*>(&seeded);for(size_t i=0;i<sizeof(seeded);++i)raw[i]=uint8_t(i*37+11);
                need(!std::memcmp(state.v0.u8,seeded.v0.u8,16),"Discarded stack vector fault must precede v0 clear");
                need(PPCExternalFastLoad32(rt.base,o.output)==0xA55A5AA5u,"Discarded stack-vector read fault must precede output stores");
            } else {
                need(state.v0.u64[0]==0&&state.v0.u64[1]==0,"Elided stack vector read must retain v0 clear");
                constexpr std::array<uint32_t,3> gold{1,16,4};
                for(size_t i=0;i<gold.size();++i) {
                    need(PPCExternalFastLoad32(rt.base,o.output+uint32_t(4*i))==gold[i],"Elided stack vector read changed known endpoint output");
                }
            }
            o=Options{};o.descriptorInput=0x13FF8;setup(o);need(!exercise(o).first.failure.empty(),"Pair bitset endpoint read must fault before clear");
            o=Options{};o.owner=0x13F80;setup(o);need(!exercise(o).first.failure.empty(),"Pair bitset owner read must retain earlier vector clear");
            o=Options{};o.entries=0x13FF8;setup(o);need(!exercise(o).first.failure.empty(),"Pair bitset late endpoint read did not fault");
        }
    }
    setGuestPage(rt,0x14,3);
    for(uint32_t at:{0x82000600u,0xFFD00000u}) {
        o=Options{};o.descriptorInput=at;setup(o);need(!exercise(o).first.failure.empty(),"Pair bitset exceptional endpoint route did not reject");
        o=Options{};o.outputInput=at;setup(o);need(!exercise(o).first.failure.empty(),"Pair bitset exceptional vector output did not reject");
    }
    o=Options{};setup(o);need(!exercise(o,PPCFPSCRRegister::DefaultCSR,true).first.failure.empty(),"Pair bitset entry cancellation did not reject");
    rt.stopping=false;PPCStopRequested=0;ResetEvent(rt.stopEvent);
}

void fastScalarCopyContract(Simpsons::Runtime& rt) {
    // The existing byte fixture owns page15 as well as the common page10..14
    // window; it lets reverse byte reads cross back into protected page14.
    const uint32_t physical=rt.allocatePhysical(0,0x20000,PAGE_READWRITE,0x100000,0x1FFFFF,0x10000);
    need(physical!=0,"Scalar copy physical fixture allocation failed");
    struct Free {Simpsons::Runtime& rt;uint32_t address;~Free(){rt.freePhysical(address);}} free{rt,physical};
    const uint32_t offset=rt.physicalAddress(physical),canonical=0xA0000000u+offset;
    struct Region {uint32_t start;std::vector<uint8_t> initial,expected;};
    std::array<Region,2> regions{{{0x10000,std::vector<uint8_t>(0x6000),std::vector<uint8_t>(0x6000)},
                                {canonical,std::vector<uint8_t>(0x20000),std::vector<uint8_t>(0x20000)}}};
    for(auto& region:regions) {
        for(size_t i=0;i<region.initial.size();++i)region.initial[i]=uint8_t(i*71+19);
    }
    std::vector<uint32_t> pages,blocks;
    for(const auto& region:regions) {
        for(uint32_t page=region.start>>12;page<(region.start+region.initial.size())>>12;++page) {
            pages.push_back(page);blocks.push_back(page>>4);
            if(!(page&15u)&&(page>>4))blocks.push_back((page>>4)-1);
        }
    }
    std::sort(blocks.begin(),blocks.end());blocks.erase(std::unique(blocks.begin(),blocks.end()),blocks.end());
    const auto watch=rt.writeWatch();
    const auto arm=[&] {
        std::vector<uint32_t> out;
        for(uint32_t page:pages)out.push_back(watch.armPage(page));
        return out;
    };
    const auto changes=[&](const std::vector<uint32_t>& before) {
        std::vector<std::array<uint32_t,2>> state;
        for(size_t i=0;i<pages.size();++i) {
            state.push_back({watch.version[pages[i]].load(std::memory_order_acquire)-before[i],
                             watch.armed[pages[i]].load(std::memory_order_acquire)});
        }
        std::vector<uint8_t> blockState;
        for(uint32_t block:blocks)blockState.push_back(Simpsons::guestWatchBlocks[block].load(std::memory_order_acquire));
        return std::pair{state,blockState};
    };
    const auto reset=[&] {
        withOpenFixture(rt,[&]{for(const auto& region:regions)std::memcpy(rt.base+region.start,region.initial.data(),region.initial.size());});
    };
    const auto exercise=[&](uint32_t destination,uint32_t source,uint32_t count,
                            uint32_t csr=PPCFPSCRRegister::DefaultCSR,bool badBase=false,bool entryStop=false) {
        PPCContext initial{};auto* raw=reinterpret_cast<uint8_t*>(&initial);
        for(size_t i=0;i<sizeof(initial);++i)raw[i]=uint8_t(i*37+11);
        initial.r1.u64=0x1234567800013F80;initial.r3.u64=0x1234567800000000ull|destination;
        initial.r4.u64=0xFEDCBA9800000000ull|source;initial.r5.u64=0x7654321000000000ull|count;
        initial.r7.u64=0x1122334455667788;initial.r8.u64=0xAABBCCDD11223344;
        initial.r9.u64=0x123456789ABCDEF0;initial.r10.u64=0x1020304050607080;
        initial.lr=0x98765432ABCDCBA0;initial.ctr.u64=0xABCDEF7654321011;initial.traceIndex=63;initial.fpscr.csr=csr;
        const auto invoke=[&](Function function,PPCContext& result) {
            rt.stopping=false;PPCStopRequested=0;ResetEvent(rt.stopEvent);
            if(entryStop)rt.requestStop("scalar copy entry cancellation");
            reset();std::memcpy(&result,&initial,sizeof(initial));const auto before=arm();SetLastError(0x41ACED29);
            const auto outcome=run(function,result,rt.base+(badBase?32:0));const auto error=GetLastError();
            return std::tuple{outcome,changes(before),error};
        };
        PPCContext reference{},actual{};const auto expected=invoke(referenceFastScalarCopy,reference);
        withOpenFixture(rt,[&]{for(auto& region:regions)std::memcpy(region.expected.data(),rt.base+region.start,region.expected.size());});
        const auto result=invoke(sub_82A3D1D8,actual);
        need(result==expected,"Scalar copy external-fast failure/full CSR/watch/LastError changed");
        need(!std::memcmp(&reference,&actual,sizeof(actual)),"Scalar copy full raw PPC context/read/trace/fault order changed");
        need(withOpenFixture(rt,[&]{
            for(const auto& region:regions) {
                if(std::memcmp(region.expected.data(),rt.base+region.start,region.expected.size()))return false;
            }
            return true;
        }),"Scalar copy complete owned bytes/overlap/partial writes changed");
        if(count==0&&std::get<0>(result).failure.empty()) {
            for(const auto& page:std::get<1>(result).first)
                need(page[0]==0&&page[1]==1,"Scalar copy zero count invalidated an owned page");
        }
        return std::pair{std::get<0>(result),actual};
    };
    const auto canonicalAddress=[](uint32_t address) {
        constexpr std::array<uint32_t,8> adjustment{0,0,0,0,0,0,0xE0000000u,0xC0001000u};
        return address+adjustment[address>>29];
    };
    // Independent byte gold: stage each complete four-byte group before writing,
    // retaining the original forward propagation for overlaps (not memmove).
    const auto gold=[&](uint32_t destination,uint32_t source,uint32_t count,uint32_t baseShift=0) {
        std::array<std::vector<uint8_t>,2> bytes{regions[0].initial,regions[1].initial};
        const auto cell=[&](uint32_t address)->uint8_t& {
            address=canonicalAddress(address)+baseShift;
            for(size_t i=0;i<regions.size();++i) {
                if(address>=regions[i].start&&uint64_t(address)<uint64_t(regions[i].start)+bytes[i].size())
                    return bytes[i][address-regions[i].start];
            }
            throw Simpsons::Failure("Scalar copy independent gold left owned memory");
        };
        while(count&&(destination&3u)){const uint8_t value=cell(source++);cell(destination++)=value;--count;}
        while(count>=4) {
            const std::array<uint8_t,4> value{cell(source),cell(source+1),cell(source+2),cell(source+3)};
            for(uint32_t i=0;i<4;++i)cell(destination+i)=value[i];
            source+=4;destination+=4;count-=4;
        }
        while(count){const uint8_t value=cell(source++);cell(destination++)=value;--count;}
        need(withOpenFixture(rt,[&]{
            for(size_t i=0;i<regions.size();++i) {
                if(std::memcmp(bytes[i].data(),rt.base+regions[i].start,bytes[i].size()))return false;
            }
            return true;
        }),"Scalar copy independent staged-byte or fault-prefix gold changed");
    };
    const auto known=[&](uint32_t destination,uint32_t source,uint32_t count,
                         uint32_t csr=PPCFPSCRRegister::DefaultCSR,bool badBase=false) {
        const auto [outcome,context]=exercise(destination,source,count,csr,badBase);
        need(outcome.failure.empty(),"Scalar copy intended successful case failed");
        need(outcome.csr==csr&&context.fpscr.csr==csr,"Integer scalar copy changed FP control/sticky bits");
        need(context.r3.u64==(0x1234567800000000ull|destination),"Scalar copy original destination return changed");
        gold(destination,source,count,badBase?32:0);
    };
    for(uint32_t count:{0u,1u,2u,3u,4u,5u,7u,8u,9u,15u,16u,17u,31u,32u,33u,63u,64u,65u}) {
        for(uint32_t source=0;source<4;++source)for(uint32_t destination=0;destination<4;++destination)
            known(0x11800+destination,0x10400+source,count);
    }
    for(uint32_t count:{0u,1u,3u,4u,7u,8u,12u,31u,32u,33u,64u,65u,256u}) {
        for(int delta:{-64,-8,-4,-1,0,1,4,8,64})known(uint32_t(0x11000+delta),0x11000,count);
    }
    known(0x11004,0x11000,12);
    constexpr std::array<uint8_t,12> repeat4{19,90,161,232,19,90,161,232,19,90,161,232};
    need(!std::memcmp(rt.base+0x11004,repeat4.data(),repeat4.size()),"Scalar copy reverse+4 must propagate whole staged words");
    known(0x11008,0x11000,12);
    constexpr std::array<uint8_t,12> repeat8{19,90,161,232,47,118,189,4,19,90,161,232};
    need(!std::memcmp(rt.base+0x11008,repeat8.data(),repeat8.size()),"Scalar copy reverse+8 original byte gold changed");
    known(0x11001,0x11000,8);
    constexpr std::array<uint8_t,9> leading{19,19,19,19,19,47,118,189,189};
    need(!std::memcmp(rt.base+0x11000,leading.data(),leading.size()),"Scalar copy unaligned overlap must retain prefix/word/tail staging");
    for(uint32_t count:{4095u,4096u,4097u}) {
        known(0x11800,0x10400,count);known(0x11801,0x10403,count);
    }
    for(uint32_t round=0;round<4;++round)for(bool flush:{false,true}) {
        const uint32_t csr=PPCFPSCRRegister::DefaultCSR|(round<<PPCFPSCRRegister::RoundShift)|
            (flush?PPCFPSCRRegister::FlushMask:0)|0x3Fu;
        known(0x11800,0x10401,17,csr);known(0x11001,0x11000,8,csr);
        setGuestPage(rt,0x14,0);
        need(!exercise(0x10800,0x13FFD,4,csr).first.failure.empty(),"Scalar copy guarded reverse first read did not fault");
        gold(0x10800,0x10400,0);setGuestPage(rt,0x14,3);
    }
    known(0x11800,0x10400,7,PPCFPSCRRegister::DefaultCSR,true); // Exact baseline permits an ordinary aligned shifted base.
    for(uint32_t alias:{physical,canonical,0xC0000000u+offset,0xDFFFF000u+offset}) {
        known(alias+0xFFFD,canonical+0x101,35);known(canonical+0x1000,alias+0x103,33);
        known(alias+0x1000,canonical+0x1004,64);known(alias+0x1000,canonical+0x1008,64);
        known(alias+0x1004,canonical+0x1000,12);known(alias+0x1008,canonical+0x1000,12);
    }
    for(uint32_t address:{0x82000600u,0xFFD00000u,0x70000000u}) {
        const auto [outcome,context]=exercise(address,address,0);
        need(outcome.failure.empty(),"Scalar copy zero count touched an invalid pointer");
        need(context.r4.u64==(0xFEDCBA9800000000ull|address)&&context.r5.u64==0x7654321000000000ull,
             "Scalar copy zero-count original cursors changed");
        gold(0x10800,0x10400,0);
    }
    for(uint8_t access:{uint8_t(0),uint8_t(1)}) {
        setGuestPage(rt,0x14,access);
        for(uint32_t destination:{0x13FFCu,0x13FFDu}) {
            const auto [outcome,context]=exercise(destination,0x10800,8);
            need(!outcome.failure.empty(),"Scalar copy protected destination did not fault");
            const uint32_t prefix=0x14000-destination;gold(destination,0x10800,prefix);
            need(context.r4.u32==0x10800+prefix+4&&context.r6.u32==0x14000,
                 "Scalar copy faulting store lost prior full-word read/cursor order");
        }
        if(access==0) {
            const auto [outcome,context]=exercise(0x10800,0x13FFC,8);
            need(!outcome.failure.empty(),"Scalar copy later aligned source did not fault");gold(0x10800,0x13FFC,4);
            need(context.r4.u32==0x14000&&context.r6.u32==0x10804,"Scalar copy aligned read fault cursor changed");
            const auto [first,firstContext]=exercise(0x10800,0x13FFD,4);
            need(!first.failure.empty(),"Scalar copy reverse first byte did not fault");gold(0x10800,0x10400,0);
            need(firstContext.r7.u64==0x1122334455667788&&firstContext.r8.u64==0xAABBCCDD11223344&&
                 firstContext.r9.u64==0x123456789ABCDEF0&&firstContext.r10.u64==0x1020304050607080,
                 "Scalar copy must read unaligned byte3 before bytes2/1/0");
            for(uint32_t source:{0x14FFDu,0x14FFEu,0x14FFFu}) {
                const auto [failed,prefix]=exercise(0x10800,source,4);
                need(!failed.failure.empty(),"Scalar copy reverse lower-page byte did not fault");gold(0x10800,0x10400,0);
                const uint32_t consumed=source-0x14FFCu;uint32_t packed{};
                for(uint32_t i=0;i<consumed;++i)packed|=uint32_t(regions[0].initial[source+3-i-0x10000])<<(8*i);
                need(prefix.r7.u64==packed&&prefix.r4.u64==(0xFEDCBA9800000000ull|source),
                     "Scalar copy reverse partial read order/cursor changed");
                if(consumed==1)need(prefix.r8.u64==0xAABBCCDD11223344,"Scalar copy read byte2 before byte3 completed");
                if(consumed==2)need(prefix.r9.u64==0x123456789ABCDEF0,"Scalar copy read byte1 before byte2 completed");
                if(consumed==3)need(prefix.r10.u64==0x1020304050607080,"Scalar copy read byte0 before byte1 completed");
            }
        } else known(0x10800,0x13FFD,8); // Read-only source is readable, including its boundary.
    }
    setGuestPage(rt,0x14,3);
    for(uint32_t source:{0x14FFDu,0x14FFEu,0x14FFFu})known(0x10800,source,4);
    for(uint32_t address:{0x82000600u,0xFFD00000u})for(uint32_t count:{1u,4u}) {
        need(!exercise(0x10800,address,count).first.failure.empty(),"Scalar copy exceptional source did not fault");gold(0x10800,0x10400,0);
        need(!exercise(address,0x10400,count).first.failure.empty(),"Scalar copy exceptional destination did not fault");gold(0x10800,0x10400,0);
    }
    need(exercise(0x10800,0x82000600,4,PPCFPSCRRegister::DefaultCSR,true).first.failure=="Invalid guest base",
         "Scalar copy exceptional bad base bypassed original slow validation");gold(0x10800,0x10400,0);
    // Huge counts are admitted only when the first access faults or entry cancels.
    need(!exercise(0x10800,0xFFD00000,0xFFFFFFFFu).first.failure.empty(),"Scalar copy huge-count first access did not fault");gold(0x10800,0x10400,0);
    for(uint32_t count:{0u,17u,0xFFFFFFFFu}) {
        need(!exercise(0xFFD00000,0x82000600,count,PPCFPSCRRegister::DefaultCSR,false,true).first.failure.empty(),
             "Scalar copy entry cancellation did not precede any copy access");gold(0x10800,0x10400,0);
    }
    rt.stopping=false;PPCStopRequested=0;ResetEvent(rt.stopEvent);
}

void fastHalfwordPointerContract(Simpsons::Runtime& rt) {
    auto* base=rt.base;
    need(PPCInlineFastLoad16(base,0)==PPCExternalFastLoad16(base,0),"Fast halfword null scratch changed");
    const uint32_t physical=rt.allocatePhysical(0,0x20000,PAGE_READWRITE,0x100000,0x1FFFFF,0x10000);
    need(physical!=0,"Fast halfword physical allocation failed");
    const uint32_t offset=rt.physicalAddress(physical),canonical=0xA0000000u+offset;const auto watch=rt.writeWatch();
    for(uint32_t alias:{physical,canonical,0xC0000000u+offset,0xDFFFF000u+offset})for(uint32_t at:{0u,1u,2u,3u,4095u,4096u,65535u,65536u}) {
        PPCExternalFastStore16(base,canonical+at,0xA5E9);
        need(PPCInlineFastLoad16(base,alias+at)==0xA5E9,"Fast halfword endian/alias load changed");
        const uint32_t first=(canonical+at)>>12,last=(canonical+at+1)>>12;
        const auto before=watch.armPage(first),lastBefore=last==first?before:watch.armPage(last);
        PPCInlineFastStore16(base,alias+at,0xC39A);
        need(PPCExternalFastLoad16(base,canonical+at)==0xC39A,"Fast halfword endian/alias store changed");
        need(watch.version[first].load(std::memory_order_acquire)==before+1&&
             watch.version[last].load(std::memory_order_acquire)==lastBefore+1,"Fast halfword exact/crossing write barrier changed");
    }
    // Neighbor-only watch at a64KiB crossing must still route through the exact
    // original barrier; a width-one implementation would miss this page.
    const uint32_t crossing=canonical+0xFFFF,next=(crossing+1)>>12;const auto before=watch.armPage(next);
    PPCInlineFastStore16(base,crossing,0x718A);
    need(watch.version[next].load(std::memory_order_acquire)==before+1,"Fast halfword next-block-only watch was lost");
    rt.freePhysical(physical);
}
void scalarContract(Simpsons::Runtime& rt) {
    auto* base=rt.base;
    for(uint32_t i=0;i<0x5000;++i)base[0x10000+i]=uint8_t(i*71+19);
    for(uint32_t offset=0;offset<4096;++offset) {
        const uint32_t address=0x10000+offset;
        need(PPC_LOAD_U8(address)==PPCLoadU8(base,address),"Inline byte load differs");
        need(PPC_LOAD_U16(address)==PPCLoadU16(base,address),"Inline halfword load differs");
        need(PPC_LOAD_U32(address)==PPCLoadU32(base,address),"Inline word load differs");
        need(PPC_LOAD_U64(address)==PPCLoadU64(base,address),"Inline doubleword load differs");
    }
    for(uint32_t offset:{0u,1u,2u,3u,4089u,4090u,4091u,4092u,4093u,4094u,4095u}) {
        const uint32_t address=0x11000+offset;
        PPC_STORE_U8(address,0xA7);need(PPCLoadU8(base,address)==0xA7,"Inline byte store differs");
        PPC_STORE_U16(address,0xDE31);need(PPCLoadU16(base,address)==0xDE31,"Inline halfword store differs");
        PPC_STORE_U32(address,0xA8D3075B);need(PPCLoadU32(base,address)==0xA8D3075B,"Inline word store differs");
        PPC_STORE_U64(address,0x6789ABCDEF123450ull);
        need(PPCLoadU64(base,address)==0x6789ABCDEF123450ull,"Inline doubleword store differs");
    }
}
}
PPC_FUNC(sub_82B64600) {
    if(!PairGrowth::current)throw Simpsons::Failure("Unexpected pair growth outside differential fixture");
    PairGrowth::current->invoke(ctx,base);
}
#define VM_TEST_BOUNDARY(name,kind) PPC_FUNC(name) { \
    if(!VMSetupBoundary::current)throw Simpsons::Failure("Unexpected VM boundary outside differential fixture"); \
    VMSetupBoundary::current->invoke(VMSetupBoundary::Kind::kind,ctx,base); }
VM_TEST_BOUNDARY(sub_828449E0,Coerce)
VM_TEST_BOUNDARY(sub_828445D8,Frames)
VM_TEST_BOUNDARY(sub_828444D8,Stack)
VM_TEST_BOUNDARY(sub_82844838,Varargs)
VM_TEST_BOUNDARY(sub_8283E360,Error)
VM_TEST_BOUNDARY(sub_82844738,Hook)
VM_TEST_BOUNDARY(sub_82A41FA0,Exit)
#undef VM_TEST_BOUNDARY
int main()try {
    Simpsons::Runtime rt;rt.map(0x10000,0x5000,true,"selected inline memory fixture");
    scalarContract(rt);
    matrixContract(rt,referenceMatrix,sub_823EBD00);
    matrixContract(rt,referenceMatrixCompose,sub_823F2D98);
    runtimeRoutinesContract(rt);vectorCopyContract(rt);fastWordLeafContract(rt);fastWordPointerContract(rt);
    fastByteDecodeContract(rt);fastBytePointerContract(rt);fastPairShiftContract(rt);fastVMSetupContract(rt);fastHalfwordPointerContract(rt);fastPairBitsetContract(rt);fastScalarCopyContract(rt);
    std::printf("PASS: %zu selected inline memory checks; original point/matrix/copy/register/byte-decoder/pair-shift bodies, growth boundary, aliasing, partial failures and FP modes\n",checks);
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL after %zu: %s\n",checks,error.what());return 1;}
