// Real original AOT CPU helpers, synthetic PCM only. No app startup, audio root,
// output device, SDK object, decoder or substituted helper implementation.
#include "runtime/engine_cpu_calls.h"
#include "audio/dac_pcm.h"
#include <algorithm>
#include <array>
#include <bit>
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace Simpsons;
constexpr uint32_t output=0x82E32000, outputBytes=0x1800;
constexpr uint32_t surround=output-0x100, surroundBytes=outputBytes+0x200;
constexpr uint32_t stack=0x10000, stackBytes=0x10000;
constexpr uint32_t data=0x40000, dataBytes=0x10000, descriptor=data+0x100;
constexpr uint32_t channelsAddress=0x82E31F9D;
size_t cases=0, samples=0, transportSamples=0;

void need(bool value,const char* message) { if(!value) throw Failure(message); }
std::vector<uint8_t> read(Runtime& rt,uint32_t address,uint32_t size) {
    auto* p=rt.pointer(address,size,false);return {p,p+size};
}
void writeBE(uint8_t* p,uint32_t bits) {
    for(unsigned i=0;i<4;++i) p[i]=uint8_t(bits>>(24-8*i));
}
void put(Runtime& rt,uint32_t address,uint32_t bits) {writeBE(rt.pointer(address,4,true),bits);}
void expectedWord(std::vector<uint8_t>& bytes,size_t at,uint32_t bits) {
    need(at+4<=bytes.size(),"Fixture expected-word bounds");writeBE(bytes.data()+at,bits);
}
uint32_t get(const std::vector<uint8_t>& bytes,size_t at) {
    need(at+4<=bytes.size(),"Fixture read bounds");
    return uint32_t(bytes[at])<<24 | uint32_t(bytes[at+1])<<16 |
           uint32_t(bytes[at+2])<<8 | bytes[at+3];
}
void transport(Runtime& rt) {
    // The bridge copies the exact original completed block. Test an unaligned
    // owned byte span too: neither float alignment nor little-endian source is
    // part of the original S+44 contract. Include NaNs without arithmetic;
    // acceptance by an actual native voice is a separate explicit policy.
    const auto original=read(rt,output,outputBytes);
    std::vector<uint8_t> borrowed(outputBytes+2,0xB9);
    std::copy(original.begin(),original.end(),borrowed.begin()+1);
    const auto block=Audio::DacPcmBlock::copyBigEndian(std::span(borrowed).subspan(1,outputBytes));
    std::fill(borrowed.begin(),borrowed.end(),0xDA);
    for(uint32_t i=0;i<Audio::DacPcmBlock::samples;++i) {
        need(std::bit_cast<uint32_t>(block.pcm[i])==get(original,4*i),"Dac PCM transport changed bits/channel order or borrowed input");
        ++transportSamples;
    }
    for(size_t size:{size_t(0),size_t(outputBytes-1),size_t(outputBytes+1)}) {
        bool rejected=false;
        try{Audio::DacPcmBlock::copyBigEndian(std::span(borrowed).first(size));}
        catch(const std::invalid_argument&){rejected=true;}
        need(rejected,"Dac PCM transport accepted a partial/oversized block");
    }
}
void equal(Runtime& rt,uint32_t address,const std::vector<uint8_t>& expected,const char* what) {
    auto* actual=rt.pointer(address,unsigned(expected.size()),false);
    if(!std::memcmp(actual,expected.data(),expected.size())) return;
    size_t i=0;while(i<expected.size() && actual[i]==expected[i]) ++i;
    char message[240];std::snprintf(message,sizeof(message),
        "%s at %08X: got %02X expected %02X",what,address+uint32_t(i),actual[i],expected[i]);
    throw Failure(message);
}
void pin(Runtime& rt,uint32_t address,uint32_t size,const char* expected) {
    std::array<uint8_t,32> hash{};
    need(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,rt.pointer(address,size,false),size,
                    hash.data(),ULONG(hash.size()))>=0,"Fixture function hash failed");
    std::string actual;
    for(auto c:hash) {actual+="0123456789abcdef"[c>>4];actual+="0123456789abcdef"[c&15];}
    need(actual==expected,"Original Dac PCM helper bytes changed");
}

// Restrict only pages in this fixture's loaded copy; exercise actual native
// protection AND guest checks, restoring both before any other fixture writes.
struct ReadOnlyPages {
    Runtime& rt;uint32_t address,size;DWORD previous{};std::vector<uint8_t> access;
    ReadOnlyPages(Runtime& r,uint32_t a,uint32_t n):rt(r),address(a),size(n) {
        need(!(a&4095) && !(n&4095),"Fixture protection alignment");
        auto* p=rt.pointer(a,n,false);
        for(uint32_t off=0;off<n;off+=4096) access.push_back(rt.pageAccess[(a+off)>>12].load());
        need(VirtualProtect(p,n,PAGE_READONLY,&previous)!=0,"Fixture read-only protection failed");
        for(uint32_t off=0;off<n;off+=4096) rt.pageAccess[(a+off)>>12].store(1);
    }
    ~ReadOnlyPages() {
        DWORD ignored{};
        if(!VirtualProtect(rt.base+address,size,previous,&ignored)) std::terminate();
        for(uint32_t off=0;off<size;off+=4096) rt.pageAccess[(address+off)>>12].store(access[off/4096]);
    }
};
struct HostFP {
    uint32_t saved=PPCFPSCRRegister::getcsr();
    explicit HostFP(uint32_t bits) {PPCFPSCRRegister::restoreHostCSR(bits);}
    ~HostFP() {PPCFPSCRRegister::restoreHostCSR(saved);}
};

void call(Runtime& rt,uint32_t address,uint32_t a,uint32_t b,uint32_t c,uint32_t d,
          bool hostileHost=false,const char* expectedFailure=nullptr) {
    PPCContext entry{};entry.r1.u64=stack+stackBytes;entry.lr=0x11223344;
    entry.r2.u64=0x1020304050607080ull;entry.r13.u64=0x8877665544332211ull;
    entry.cr2={1,0,0,{1}};entry.cr3={0,1,0,{0}};entry.cr4={0,0,1,{1}};
    entry.vscr_sat=1;
    auto* previous=currentContext;
    bool failed=false;
    {
        EngineCpuCalls cpu(entry,rt.base);auto& ctx=cpu.registers();
        const std::array gprs={&ctx.r14,&ctx.r15,&ctx.r16,&ctx.r17,&ctx.r18,&ctx.r19,
            &ctx.r20,&ctx.r21,&ctx.r22,&ctx.r23,&ctx.r24,&ctx.r25,&ctx.r26,&ctx.r27,
            &ctx.r28,&ctx.r29,&ctx.r30,&ctx.r31};
        const std::array fprs={&ctx.f14,&ctx.f15,&ctx.f16,&ctx.f17,&ctx.f18,&ctx.f19,
            &ctx.f20,&ctx.f21,&ctx.f22,&ctx.f23,&ctx.f24,&ctx.f25,&ctx.f26,&ctx.f27,
            &ctx.f28,&ctx.f29,&ctx.f30,&ctx.f31};
        for(size_t i=0;i<gprs.size();++i) {
            gprs[i]->u64=0xFEDCBA9800000000ull+i;
            fprs[i]->u64=0x4008000000000000ull+i;
        }
        const uint32_t sp=ctx.r1.u32;auto expectedStack=read(rt,stack,stackBytes);
        // Exact instruction stores outside the PCM output: converter's frame
        // backchain/saved LR/r3 home, or fade's r4 home. Clamp makes no stores.
        if(address==0x823544D0) {
            expectedWord(expectedStack,sp-8-stack,uint32_t(entry.lr));
            expectedWord(expectedStack,sp-0x60-stack,sp);
            expectedWord(expectedStack,sp+0x14-stack,a);
        } else if(address==0x82345630) expectedWord(expectedStack,sp+0x1C-stack,b);
        const uint32_t host=PPCFPSCRRegister::DefaultCSR |
            (hostileHost ? uint32_t(PPCFPSCRRegister::FlushMask|SIMDE_MM_ROUND_TOWARD_ZERO) : 0u);
        HostFP hostScope(host);
        try {cpu.invoke(address,a,b,c,d);}
        catch(const Failure& e) {
            if(!expectedFailure || std::string(e.what()).find(expectedFailure)!=0) throw;
            failed=true;
        }
        need(PPCFPSCRRegister::getcsr()==host,"Original Dac helper leaked host FP controls/status");
        if(!failed) {
            need(ctx.r1.u64==sp && ctx.lr==entry.lr,"Dac helper damaged SP/LR");
            need(ctx.r3.u32==(address==0x823544D0?output:a),"Dac helper r3 result changed");
            need(ctx.r2.u64==entry.r2.u64 && ctx.r13.u64==entry.r13.u64,"Dac helper changed TOC/TLS");
            for(size_t i=0;i<gprs.size();++i) {
                need(gprs[i]->u64==0xFEDCBA9800000000ull+i,"Dac helper damaged nonvolatile GPR");
                need(fprs[i]->u64==0x4008000000000000ull+i,"Dac helper damaged nonvolatile FPR");
            }
            need(!std::memcmp(&ctx.cr2,&entry.cr2,sizeof(PPCCRRegister)) &&
                 !std::memcmp(&ctx.cr3,&entry.cr3,sizeof(PPCCRRegister)) &&
                 !std::memcmp(&ctx.cr4,&entry.cr4,sizeof(PPCCRRegister)),"Dac helper damaged saved CR fields");
            need(ctx.vscr_sat==1,"Scalar Dac helper changed VSCR SAT");
            equal(rt,stack,expectedStack,"Dac stack store footprint");
        }
    }
    need(currentContext==previous,"EngineCpuCalls failed to restore current context");
    need(failed==bool(expectedFailure),"Dac helper failed to reject protected/unmapped memory");
    ++cases;
}

uint32_t conversionSample(uint32_t plane,uint32_t frame) {
    // Unique normal values establish order across every frame/channel. Selected
    // boundary frames cover nonarithmetic IEEE bit patterns, including subnormals.
    constexpr std::array special={0u,0x80000000u,0x3F800000u,0xBF800000u,
        0x00000001u,0x80000001u,0x007FFFFFu,0x807FFFFFu,0x00800000u,
        0x80800000u,0x7F7FFFFFu,0xFF7FFFFFu,0x7F800000u,0xFF800000u};
    constexpr std::array boundary={0u,1u,3u,4u,15u,16u,31u,32u,127u,128u,254u,255u};
    const auto found=std::find(boundary.begin(),boundary.end(),frame);
    if(found!=boundary.end()) return special[(plane+size_t(found-boundary.begin()))%special.size()];
    return ((plane&1u)<<31) | 0x3E800000u | ((plane+1)*0x10000+frame*37);
}
void resetOutput(Runtime& rt) {std::memset(rt.pointer(surround,surroundBytes,true),0xA5,surroundBytes);}
void conversion(Runtime& rt,uint32_t first,uint16_t stride,bool guarded,bool hostile,uint32_t ignoredCount) {
    resetOutput(rt);std::memset(rt.pointer(data,dataBytes,true),0xB6,dataBytes);
    if(guarded) for(uint32_t lane=0;lane<6;++lane)
        std::memset(rt.pointer(0x60000+lane*0x2000,0x1000,true),0xC7,0x1000);
    put(rt,descriptor+4,first);
    auto* n=rt.pointer(descriptor+0xE,2,true);n[0]=uint8_t(stride>>8);n[1]=uint8_t(stride);
    for(uint32_t lane=0;lane<6;++lane) for(uint32_t j=0;j<256;++j)
        put(rt,first+4*(uint32_t(stride)*lane+j),conversionSample(lane,j));
    auto expected=read(rt,surround,surroundBytes);const auto inputs=read(rt,data,dataBytes);
    std::array<std::vector<uint8_t>,6> guardedInputs;
    if(guarded) for(uint32_t lane=0;lane<6;++lane) guardedInputs[lane]=read(rt,0x60000+lane*0x2000,0x1000);
    // Independent frame/plane indexing, not the original register/store order.
    constexpr std::array order={0u,2u,1u,5u,3u,4u};
    for(uint32_t j=0;j<256;++j) for(uint32_t lane=0;lane<6;++lane)
        expectedWord(expected,0x100+4*(j*6+lane),conversionSample(order[lane],j));
    call(rt,0x823544D0,0x3000,descriptor,6,ignoredCount,hostile);
    equal(rt,surround,expected,"Six-channel interleave/output guards");
    transport(rt);
    equal(rt,data,inputs,"Interleave descriptor/source mutation");
    if(guarded) for(uint32_t lane=0;lane<6;++lane)
        equal(rt,0x60000+lane*0x2000,guardedInputs[lane],"Guarded source mutation");
    samples+=256*6;
}

uint32_t clampExpected(uint32_t bits) {
    const uint32_t mag=bits&0x7FFFFFFF;
    // fcmpu leaves LT/GT clear on unordered; both original branches skip stores.
    if(mag>0x7F800000u || mag<=0x3F800000u) return bits;
    return (bits&0x80000000u)|0x3F800000u;
}
void clamp(Runtime& rt,bool readOnly,bool hostile) {
    constexpr std::array values={0u,0x80000000u,1u,0x80000001u,0x007FFFFFu,0x807FFFFFu,
        0x00800000u,0x80800000u,0x3F7FFFFFu,0xBF7FFFFFu,0x3F800000u,0xBF800000u,
        0x3F800001u,0xBF800001u,0x40000000u,0xC0000000u,0x7F7FFFFFu,0xFF7FFFFFu,
        0x7F800000u,0xFF800000u,0x7FC00000u,0xFFC00000u,0x7FC12345u,0xFFC54321u,
        0x7F800001u,0xFF800001u,0x7FBFFFFFu,0xFFBFFFFFu};
    resetOutput(rt);
    for(uint32_t i=0;i<outputBytes/4;++i) {
        uint32_t bits=values[i%values.size()];
        if(readOnly) bits=clampExpected(bits);
        put(rt,output+4*i,bits);
    }
    auto expected=read(rt,surround,surroundBytes);
    for(uint32_t i=0;i<outputBytes/4;++i)
        expectedWord(expected,0x100+4*i,clampExpected(get(expected,0x100+4*i)));
    if(readOnly) {
        ReadOnlyPages protectedOutput(rt,output,0x2000);
        call(rt,0x82354480,0x3000,0x3000,0xFFFFFFFF,0,hostile);
    } else call(rt,0x82354480,0x3000,0x3000,0xFFFFFFFF,0,hostile);
    equal(rt,surround,expected,"Clamp bounds/NaN/zero/no-store contract");transport(rt);samples+=outputBytes/4;
}

// Integer IEEE-754 oracle: multiply a finite binary32 by j/128, nearest-even.
// It does not use the native FP expressions or FPR helpers under test.
uint64_t roundedShift(uint64_t value,int shift) {
    if(shift<=0) return value<<(-shift);
    need(shift<64,"Fixture rounding shift out of range");
    const uint64_t whole=value>>shift, remainder=value&((uint64_t(1)<<shift)-1), half=uint64_t(1)<<(shift-1);
    return whole+(remainder>half || (remainder==half && (whole&1)));
}
uint32_t faded(uint32_t bits,uint32_t frame) {
    const uint32_t sign=bits&0x80000000, mag=bits&0x7FFFFFFF, exponent=mag>>23;
    need(exponent<255 && frame<128,"Fixture fade only covers finite input/128 frames");
    if(!mag || !frame) return sign;
    const uint64_t significand=(mag&0x7FFFFF)|(exponent?0x800000:0);
    const uint64_t product=significand*frame;
    const int power=exponent ? int(exponent)-127-23-7 : -149-7;
    const int highest=int(std::bit_width(product))-1;
    int e=highest+power;
    if(e < -126) return sign|uint32_t(roundedShift(product,-(power+149)));
    uint64_t mantissa=roundedShift(product,highest-23);
    if(mantissa==0x1000000) {mantissa>>=1;++e;}
    need(e<128,"Fixture finite fade overflow");
    return sign|(uint32_t(e+127)<<23)|(uint32_t(mantissa)&0x7FFFFF);
}
void fade(Runtime& rt,uint8_t channels,bool hostile) {
    resetOutput(rt);*rt.pointer(channelsAddress,1,true)=channels;
    constexpr std::array finite={0u,0x80000000u,1u,0x80000001u,0x00000003u,0x80000003u,
        0x007FFFFFu,0x807FFFFFu,0x00800000u,0x80800000u,0x3F800000u,0xBF800000u,
        0x3F800001u,0xBF800003u,0x7F7FFFFFu,0xFF7FFFFFu,0x3EAAAAABu,0xBEAAAAABu};
    for(uint32_t i=0;i<outputBytes/4;++i) put(rt,output+4*i,finite[(i+i/6)%finite.size()]);
    auto expected=read(rt,surround,surroundBytes);
    if(channels) for(uint32_t frame=0;frame<128;++frame) for(uint32_t lane=0;lane<6;++lane) {
        const uint32_t at=0x100+4*(frame*6+lane);
        expectedWord(expected,at,faded(get(expected,at),frame));
    }
    call(rt,0x82345630,0x3000,0xDEADBEEF,0xFFFFFFFF,0,hostile);
    equal(rt,surround,expected,"Fade ramp/first128 footprint/unchanged second128");
    transport(rt);
    samples+=channels?128*6:0;
}

void memoryFailures(Runtime& rt) {
    resetOutput(rt);const auto original=read(rt,surround,surroundBytes);
    // Guest pages below 0x10000 demand-map as zero-filled null-device scratch;
    // 0x30000 lies between this fixture's stack and data maps and stays unmapped.
    call(rt,0x823544D0,0x30000,0x30000,6,256,true,"Unmapped/protected guest read address=0x0003000E");
    equal(rt,surround,original,"Invalid descriptor wrote output");
    put(rt,output,0x40000000);const auto unclamped=read(rt,surround,surroundBytes);
    {
        ReadOnlyPages protectedOutput(rt,output,0x2000);
        call(rt,0x82354480,0,0,0,0,true,"Unmapped/protected guest write address=0x82E32000");
    }
    equal(rt,surround,unclamped,"Rejected clamp store changed memory");
}
}

int main(int argc,char** argv) {
    try {
        if(argc!=2) throw Simpsons::Failure("Usage: DacPcmTests analysis/simpsons.pe");
        Simpsons::Runtime rt;rt.load(argv[1]);
        rt.map(stack,stackBytes,true,"Dac PCM fixture stack");
        rt.map(data,dataBytes,true,"Dac PCM fixture descriptor/planes");
        for(uint32_t lane=0;lane<6;++lane)
            rt.map(0x60000+lane*0x2000,0x1000,true,"Dac PCM fixture guarded plane");
        const auto savedOutput=read(rt,surround,surroundBytes);
        const auto sdk=read(rt,0x82E2D9F0,8),root=read(rt,0x82E31BCC,4);
        pin(rt,0x823544D0,0x1A8,"003add46a8e145930f5eb288a39e326d9a814a1b2dc3bc8a2fc7d4ade94d98d2");
        pin(rt,0x82354480,0x50,"816c823a3ace87fb73a35a6267b3f54cd1d93759e9b127d382ce26ebef27039b");
        pin(rt,0x82345630,0x80,"7b848af6a3a6b6aee2f5c8aba353b86730f84c670616f563e05f0d3a129e0907");
        for(auto [pc,bits]:std::array<std::array<uint32_t,2>,4>{{
            {0x821DD0D8,0},{0x821CA0E4,0x3F800000},{0x821DD110,0xBF800000},{0x821DCC8C,0x3C000000}}})
            need(get(read(rt,pc,4),0)==bits,"Original Dac helper literal changed");
        // Known exact cases exercise the oracle independently of original calls.
        need(faded(0x3F800000,1)==0x3C000000 && faded(0xBF800000,127)==0xBF7E0000 &&
             faded(1,64)==0 && faded(3,64)==2 && faded(0x80000003,64)==0x80000002 &&
             faded(0x3F800001,3)==0x3CC00002 && faded(0x3F800003,3)==0x3CC00004 &&
             faded(0x80000000,7)==0x80000000,"Fixture integer fade oracle failed");
        for(bool hostile:{false,true}) {
            conversion(rt,data+0x1000,256,false,hostile,256);
            conversion(rt,data+0x1004,257,false,hostile,0);
            conversion(rt,data+0x107C,263,false,hostile,1);
            // Per-plane unmapped gaps, starts at page start or exactly1024 bytes
            // before page end. Original scalar loads must not overread a plane.
            conversion(rt,0x60000,2048,true,hostile,0xFFFFFFFF);
            conversion(rt,0x60C00,2048,true,hostile,256);
            clamp(rt,false,hostile);clamp(rt,true,hostile);
            fade(rt,6,hostile);fade(rt,0,hostile);
        }
        memoryFailures(rt);
        equal(rt,0x82E2D9F0,sdk,"Dac CPU fixture changed SDK globals");
        equal(rt,0x82E31BCC,root,"Dac CPU fixture fabricated an audio root");
        need(!rt.engineAudio && rt.threads.empty() && rt.handles.empty() &&
             rt.allocations.empty() && rt.physicalAllocations.empty(),"Dac CPU helpers acquired runtime resources");
        std::memcpy(rt.pointer(surround,surroundBytes,true),savedOutput.data(),savedOutput.size());
        std::printf("PASS original Dac PCM: %zu cases, %zu sample checks, %zu owned transport samples; 6ch interleave, clamp IEEE/no-store, finite fade128, exact BE footprints, page guards, ABI and host FP restoration; no output device\n",cases,samples,transportSamples);
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"FAIL original Dac PCM: %s\n",e.what());return 1;
    }
}
