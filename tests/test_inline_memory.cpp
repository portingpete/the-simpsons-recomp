#include "runtime/runtime.h"
#include "ppc_recomp_shared.h"
#include "inline_memory_reference.h" // Parsed before the optimized access macros.
#include "runtime/aot_inline_memory.h"
#include <array>
#include <cstring>
#include <cstdio>
#include <string>
#include <type_traits>

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
int main()try {
    Simpsons::Runtime rt;rt.map(0x10000,0x5000,true,"selected inline memory fixture");
    scalarContract(rt);
    matrixContract(rt,referenceMatrix,sub_823EBD00);
    matrixContract(rt,referenceMatrixCompose,sub_823F2D98);
    runtimeRoutinesContract(rt);vectorCopyContract(rt);
    std::printf("PASS: %zu selected inline memory checks; original point/matrix composition/copy/register bodies, aliasing, partial failures and FP modes\n",checks);
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL after %zu: %s\n",checks,error.what());return 1;}
