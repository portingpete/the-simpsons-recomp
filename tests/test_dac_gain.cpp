// Original AOT CPU kernels with a synthetic, bounded DSP work descriptor.
// No source voice, SDK vtable, audio root, output device or application startup.
#include "runtime/engine_cpu_calls.h"
#include "audio/dac_gain.h"
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
constexpr uint32_t scalar=0x82C64960, dispatch=0x82C57E40, vectorKernel=0x82C6A0D8;
constexpr uint32_t stack=0x10000, stackSize=0x10000;
constexpr uint32_t descPage=0x40000, desc=descPage+0x100;
constexpr uint32_t sourcePage=0x50000, destPage=0x60000, regionSize=0x2000;
size_t cases{}, sampleChecks{}, oracleChecks{},nativeCalls{},nativeRejections{},nativeEmpty{};
void need(bool value,const char* message) {if(!value) throw Failure(message);}
void be(uint8_t* p,uint32_t x) {for(unsigned i=0;i<4;++i) p[i]=uint8_t(x>>(24-8*i));}
uint32_t u32(const uint8_t* p) {return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
void put(Runtime& rt,uint32_t p,uint32_t x) {be(rt.pointer(p,4,true),x);}
uint32_t get(Runtime& rt,uint32_t p) {return u32(rt.pointer(p,4,false));}
using Bytes=std::vector<uint8_t>;
Bytes read(Runtime& rt,uint32_t p,uint32_t n) {auto* q=rt.pointer(p,n,false);return {q,q+n};}
void expectedWord(Bytes& b,uint32_t offset,uint32_t x) {
    need(uint64_t(offset)+4<=b.size(),"Fixture expected bounds");be(b.data()+offset,x);
}
void equal(Runtime& rt,uint32_t p,const Bytes& expected,const char* name) {
    const auto* actual=rt.pointer(p,unsigned(expected.size()),false);
    if(!std::memcmp(actual,expected.data(),expected.size())) return;
    size_t at=0;while(actual[at]==expected[at]) ++at;
    char message[240];std::snprintf(message,sizeof(message),"%s at %08X: %02X != %02X",
        name,p+uint32_t(at),actual[at],expected[at]);throw Failure(message);
}
void pin(Runtime& rt,uint32_t p,uint32_t n,const char* expected) {
    std::array<uint8_t,32> hash{};
    need(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,rt.pointer(p,n,false),n,
        hash.data(),ULONG(hash.size()))>=0,"Function hash failed");
    std::string actual;for(auto x:hash) {actual+="0123456789abcdef"[x>>4];actual+="0123456789abcdef"[x&15];}
    need(actual==expected,"Original gain instructions changed");
}

// Independent integer binary32 arithmetic, nearest-even and gradual underflow.
// Arithmetic operands are finite. Multiplication may produce infinity; no NaN
// propagation or FPSCR exception oracle is invented here. No host FP arithmetic.
struct Binary {
    uint32_t sign;uint64_t significand;int power;
    explicit Binary(uint32_t bits):sign(bits&0x80000000),
        significand((bits&0x7FFFFF)|((bits&0x7F800000)?0x800000:0)),
        power((bits&0x7F800000)?int((bits>>23)&255)-150:-149) {
        need((bits&0x7F800000)!=0x7F800000,"Oracle requires finite operands");
    }
};
uint32_t rounded(uint32_t sign,uint64_t numerator,uint64_t denominator,int power) {
    need(denominator!=0,"Oracle division by zero");
    if(!numerator) return sign;
    int k=int(std::bit_width(numerator))-int(std::bit_width(denominator));
    if(k>=0) {if(numerator<(denominator<<k)) --k;}
    else {if((numerator<<(-k))<denominator) --k;}
    int exponent=k+power;
    if(exponent>127) return sign|0x7F800000;
    if(exponent<-150) return sign;
    const int unit=std::max(exponent-23,-149), shift=power-unit;
    if(shift>=0) {
        need(shift<64 && int(std::bit_width(numerator))+shift<=64,"Oracle numerator overflow");
        numerator<<=shift;
    } else {
        need(-shift<64 && int(std::bit_width(denominator))-shift<=64,"Oracle denominator overflow");
        denominator<<=-shift;
    }
    uint64_t q=numerator/denominator, rem=numerator%denominator;
    // rem versus denominator-rem avoids overflow when doubling a remainder.
    q+=(rem>denominator-rem || (rem==denominator-rem && (q&1)));
    if(exponent<-126) return sign|uint32_t(q); // may round to smallest normal
    if(q==0x1000000) {q>>=1;++exponent;}
    if(exponent>127) return sign|0x7F800000;
    return sign|(uint32_t(exponent+127)<<23)|(uint32_t(q)&0x7FFFFF);
}
uint32_t add(uint32_t x,uint32_t y) {
    Binary a(x),b(y);
    if(!a.significand && !b.significand) return a.sign&b.sign;
    if(!a.significand) return y;
    if(!b.significand) return x;
    if(a.power<b.power) {std::swap(a,b);std::swap(x,y);}
    const int gap=a.power-b.power;
    // The large operand is itself exact binary32, so a term this far below its
    // least significant bit cannot change nearest-even rounding (either sign).
    if(gap>40) return x;
    const uint64_t n=a.significand<<gap, m=b.significand;
    if(a.sign==b.sign) return rounded(a.sign,n+m,1,b.power);
    if(n==m) return 0;
    return n>m?rounded(a.sign,n-m,1,b.power):rounded(b.sign,m-n,1,b.power);
}
uint32_t subtract(uint32_t x,uint32_t y) {return add(x,y^0x80000000);}
uint32_t multiply(uint32_t x,uint32_t y) {
    Binary a(x),b(y);return rounded(a.sign^b.sign,a.significand*b.significand,1,a.power+b.power);
}
uint32_t divide(uint32_t x,uint32_t y) {
    Binary a(x),b(y);return rounded(a.sign^b.sign,a.significand,b.significand,a.power-b.power);
}
void oracleSelfTests() {
    const auto check=[](uint32_t actual,uint32_t expected) {
        need(actual==expected,"Independent integer IEEE oracle anchor failed");++oracleChecks;
    };
    check(add(0x3F800000,0x33800000),0x3F800000); // half ulp, even down
    check(add(0x3F800001,0x33800000),0x3F800002); // half ulp, odd up
    check(add(0x00800000,0x80000001),0x007FFFFF);
    check(add(0x7F7FFFFF,1),0x7F7FFFFF);
    check(add(0x3F800000,0xBF800000),0);
    check(add(0x80000000,0x80000000),0x80000000);
    check(add(0x80000000,0),0);
    check(subtract(0x3F800000,0x3F7FFFFF),0x33800000);
    check(multiply(1,0x3F000000),0);
    check(multiply(3,0x3F000000),2);
    check(multiply(0x80000003,0x3F000000),0x80000002);
    check(multiply(0x7F7FFFFF,0x40000000),0x7F800000);
    check(multiply(0x3F800001,0x3F800001),0x3F800002);
    check(multiply(0x80000000,0xBF800000),0);
    check(divide(0x3F800000,0x40400000),0x3EAAAAAB);
    check(divide(0xBF800000,0x40400000),0xBEAAAAAB);
    check(divide(1,0x40000000),0);
    check(divide(3,0x40000000),2);
    check(divide(2,0x40400000),1);
    check(rounded(0,256,1,0),0x43800000);
}

struct HostFP {
    uint32_t saved=PPCFPSCRRegister::getcsr();
    explicit HostFP(bool hostile) {PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR |
        (hostile?uint32_t(PPCFPSCRRegister::FlushMask|SIMDE_MM_ROUND_TOWARD_ZERO):0u));}
    ~HostFP() {PPCFPSCRRegister::restoreHostCSR(saved);}
};
struct ReadOnly {
    Runtime& rt;uint32_t address,size;DWORD old{};Bytes access;
    ReadOnly(Runtime& r,uint32_t a,uint32_t n):rt(r),address(a),size(n) {
        need(!(a&4095) && !(n&4095),"Fixture protection alignment");
        for(uint32_t off=0;off<n;off+=4096) access.push_back(rt.pageAccess[(a+off)>>12].load());
        need(VirtualProtect(rt.pointer(a,n,false),n,PAGE_READONLY,&old)!=0,"Fixture VirtualProtect");
        for(uint32_t off=0;off<n;off+=4096) rt.pageAccess[(a+off)>>12].store(1);
    }
    ~ReadOnly() {
        DWORD ignored{};if(!VirtualProtect(rt.base+address,size,old,&ignored)) std::terminate();
        for(uint32_t off=0;off<size;off+=4096) rt.pageAccess[(address+off)>>12].store(access[off/4096]);
    }
};
bool traced(const PPCContext& c,uint32_t pc) {
    return std::find(std::begin(c.trace),std::end(c.trace),pc)!=std::end(c.trace);
}
void invoke(Runtime& rt,uint32_t pc,uint32_t argument,uint32_t result,bool hostile,
            int selected=-1,const char* failure=nullptr) {
    PPCContext entry{};entry.r1.u64=stack+stackSize;entry.lr=0x11223344;
    entry.r2.u64=0x876543219ABCDEF0;entry.r13.u64=0xABCDEF0123456789;
    entry.cr2={1,0,0,{1}};entry.cr3={0,1,0,{0}};entry.cr4={0,0,1,{1}};entry.vscr_sat=1;
    auto* previous=currentContext;bool rejected=false;
    {
        EngineCpuCalls cpu(entry,rt.base);auto& c=cpu.registers();
        const std::array gprs={&c.r14,&c.r15,&c.r16,&c.r17,&c.r18,&c.r19,&c.r20,&c.r21,&c.r22,
            &c.r23,&c.r24,&c.r25,&c.r26,&c.r27,&c.r28,&c.r29,&c.r30,&c.r31};
        const std::array fprs={&c.f14,&c.f15,&c.f16,&c.f17,&c.f18,&c.f19,&c.f20,&c.f21,&c.f22,
            &c.f23,&c.f24,&c.f25,&c.f26,&c.f27,&c.f28,&c.f29,&c.f30,&c.f31};
        for(size_t i=0;i<gprs.size();++i) {gprs[i]->u64=0xF123456700000000ull+i;fprs[i]->u64=0x4008000000000000ull+i;}
        const auto sp=c.r1.u32;auto expectedStack=read(rt,stack,stackSize);
        if(pc==scalar && !failure) {
            const auto remaining=get(rt,desc+0x18)-get(rt,desc+0x1C);
            expectedWord(expectedStack,sp-0x10-stack,0);
            expectedWord(expectedStack,sp-0xC-stack,remaining);
        }
        HostFP scope(hostile);const auto host=PPCFPSCRRegister::getcsr();
        try {cpu.invoke(pc,argument,0xDEAD1000,0xDEAD2000,0xDEAD3000,0xDEAD4000);}
        catch(const Failure& e) {
            if(!failure || std::string(e.what()).find(failure)!=0) throw;
            rejected=true;
        }
        need(PPCFPSCRRegister::getcsr()==host,"Gain call leaked host FP state");
        if(!rejected) {
            need(c.r3.u32==result,"Gain return ABI");
            need(c.r1.u64==sp && c.lr==entry.lr,"Gain SP/LR preservation");
            need(c.r2.u64==entry.r2.u64 && c.r13.u64==entry.r13.u64,"Gain TOC/TLS preservation");
            for(size_t i=0;i<gprs.size();++i) {
                need(gprs[i]->u64==0xF123456700000000ull+i,"Gain nonvolatile GPR");
                need(fprs[i]->u64==0x4008000000000000ull+i,"Gain nonvolatile FPR");
            }
            need(!std::memcmp(&c.cr2,&entry.cr2,sizeof(PPCCRRegister)) &&
                !std::memcmp(&c.cr3,&entry.cr3,sizeof(PPCCRRegister)) &&
                !std::memcmp(&c.cr4,&entry.cr4,sizeof(PPCCRRegister)),"Gain saved CR fields");
            need(c.vscr_sat==1,"Gain VSCR SAT preservation");
            if(pc==scalar) equal(rt,stack,expectedStack,"Scalar gain exact stack footprint");
            if(selected>=0) {
                need(traced(c,vectorKernel),"Original dispatcher did not select six-channel optimized entry");
                need(traced(c,scalar)==bool(selected),"Original scalar/vector alignment selection differs");
            }
        }
    }
    need(currentContext==previous,"EngineCpuCalls TLS context restoration");
    need(rejected==bool(failure),"Gain call failed expected rejection");++cases;
}

uint32_t sample(uint32_t frame,uint32_t lane,bool normalOnly) {
    if(normalOnly) return ((lane&1)<<31)|rounded(0,256+lane*64+frame,1,-9);
    constexpr std::array edge={0u,0x80000000u,1u,0x80000001u,3u,0x80000003u,
        0x007FFFFFu,0x807FFFFFu,0x00800000u,0x80800000u,0x3F800000u,0xBF800000u,
        0x3F800001u,0xBF800003u,0x3EAAAAABu,0xBEAAAAABu,0x7F7FFFFFu,0xFF7FFFFFu};
    return edge[(frame*7+lane*3)%edge.size()];
}
void setup(Runtime& rt,uint32_t current,uint32_t target,uint32_t sourceTotal=256,
           uint32_t sourceProgress=0,uint32_t destTotal=256,uint32_t destProgress=0,
           uint32_t source=sourcePage+0x404,uint32_t dest=destPage+0x80,bool normalOnly=false) {
    need(sourceTotal<=256 && destTotal<=256 && sourceProgress<=sourceTotal && destProgress<=destTotal,
        "Fixture bounded descriptor counters");
    std::memset(rt.pointer(descPage,0x1000,true),0xA7,0x1000);
    std::memset(rt.pointer(sourcePage,regionSize,true),0xB8,regionSize);
    std::memset(rt.pointer(destPage,regionSize,true),0xC9,regionSize);
    put(rt,desc,source);put(rt,desc+4,sourceTotal);put(rt,desc+8,sourceProgress);
    *rt.pointer(desc+0xC,1,true)=0;*rt.pointer(desc+0xD,1,true)=6;
    put(rt,desc+0x10,48000);put(rt,desc+0x14,dest);put(rt,desc+0x18,destTotal);
    put(rt,desc+0x1C,destProgress);put(rt,desc+0x20,48000);
    put(rt,desc+0x24,current);put(rt,desc+0x28,target);
    put(rt,desc+0x4C,0x3000);put(rt,desc+0x50,3);put(rt,desc+0x54,1);
    for(uint32_t j=0;j<sourceTotal;++j) for(uint32_t lane=0;lane<6;++lane)
        put(rt,source+4*(j*6+lane),sample(j,lane,normalOnly));
}
void checkRun(Runtime& rt,bool hostile,bool viaDispatch=false,int selected=-1,
              Audio::DacGainState* retainedNative=nullptr) {
    const auto s=get(rt,desc),d=get(rt,desc+0x14),sp=get(rt,desc+8),dp=get(rt,desc+0x1C);
    const auto sr=get(rt,desc+4)-sp,dr=get(rt,desc+0x18)-dp,n=std::min(sr,dr);
    need(n!=0 && sr<=256 && dr<=256,"Do not directly invoke unsafe scalar zero-work loop");
    auto expectedDesc=read(rt,descPage,0x1000),expectedDest=read(rt,destPage,regionSize);
    const auto source=read(rt,sourcePage,regionSize);
    auto current=get(rt,desc+0x24);const auto target=get(rt,desc+0x28);
    const auto step=divide(subtract(target,current),rounded(0,dr,1,0));
    for(uint32_t j=0;j<n;++j) {
        for(uint32_t lane=0;lane<6;++lane) {
            expectedWord(expectedDest,d-destPage+lane*0x400+4*(dp+j),multiply(get(rt,s+4*((sp+j)*6+lane)),current));
            ++sampleChecks;
        }
        current=add(current,step);
    }
    expectedWord(expectedDesc,desc-descPage+8,sp+n);
    expectedWord(expectedDesc,desc-descPage+0x1C,dp+n);
    expectedWord(expectedDesc,desc-descPage+0x24,current);
    if(viaDispatch) {
        expectedWord(expectedDesc,desc-descPage+0x2C,0x3F800000); // rate ratio
        expectedWord(expectedDesc,desc-descPage+0x4C,vectorKernel);
        expectedWord(expectedDesc,desc-descPage+0x50,0);
    }
    // Owned native float arrays, translated from the same original BE bits.
    // Prefix/suffix guards and inactive plane tails must remain byte-identical.
    Audio::DacGainState native{get(rt,desc+4),sp,get(rt,desc+0x18),dp,
        std::bit_cast<float>(get(rt,desc+0x24)),std::bit_cast<float>(target)};
    if(retainedNative) {
        retainedNative->sourceFrames=native.sourceFrames; // caller exposes next source fragment
        need(retainedNative->sourceProgress==sp && retainedNative->destinationProgress==dp &&
            retainedNative->destinationFrames==native.destinationFrames &&
            std::bit_cast<uint32_t>(retainedNative->current)==get(rt,desc+0x24) &&
            std::bit_cast<uint32_t>(retainedNative->target)==target,"Native partial-call state diverged");
        native=*retainedNative;
    }
    const float canary=std::bit_cast<float>(0xC1234567u);
    std::vector<float> nativeInput(native.sourceFrames*6+2,canary),nativeOutput(1536+2,canary);
    for(uint32_t i=0;i<native.sourceFrames*6;++i) nativeInput[i+1]=std::bit_cast<float>(get(rt,s+i*4));
    for(uint32_t i=0;i<1536;++i) nativeOutput[i+1]=std::bit_cast<float>(get(rt,d+i*4));
    const auto inputCopy=nativeInput,outputCopy=nativeOutput;
    {
        HostFP fp(hostile);const auto csr=PPCFPSCRRegister::getcsr();
        const auto result=Audio::processDacGain(std::span(nativeInput).subspan(1,native.sourceFrames*6),
            std::span(nativeOutput).subspan(1,1536),native);
        need(result.work==Audio::DacGainWork::Processed && result.frames==n,"Native gain result");
        need(PPCFPSCRRegister::getcsr()==csr,"Native gain leaked host FP state");
    }
    ++nativeCalls;
    invoke(rt,viaDispatch?dispatch:scalar,desc,viaDispatch?n:desc,hostile,selected);
    equal(rt,descPage,expectedDesc,"Gain descriptor footprint/current/target/counters");
    equal(rt,destPage,expectedDest,"Gain exact samples/plane stride/output guards");
    equal(rt,sourcePage,source,"Gain input changed");
    need(native.sourceProgress==get(rt,desc+8) && native.destinationProgress==get(rt,desc+0x1C) &&
        std::bit_cast<uint32_t>(native.current)==get(rt,desc+0x24) &&
        std::bit_cast<uint32_t>(native.target)==get(rt,desc+0x28) &&
        native.sourceFrames==get(rt,desc+4) && native.destinationFrames==get(rt,desc+0x18),
        "Native/original gain state bit comparison");
    for(uint32_t i=0;i<1536;++i)
        need(std::bit_cast<uint32_t>(nativeOutput[i+1])==get(rt,d+i*4),"Native/original output bit comparison");
    need(!std::memcmp(nativeInput.data(),inputCopy.data(),nativeInput.size()*sizeof(float)),"Native source changed");
    need(!std::memcmp(nativeOutput.data(),outputCopy.data(),sizeof(float)) &&
         !std::memcmp(&nativeOutput.back(),&outputCopy.back(),sizeof(float)),"Native output guard changed");
    if(retainedNative) *retainedNative=native;
}
void emptyDispatcher(Runtime& rt,bool outputFull) {
    setup(rt,0x3EAAAAAB,0x3F800000,256,outputFull?0:256,256,outputFull?256:0);
    put(rt,desc,0x3000);put(rt,desc+0x14,0x3000);
    const auto descriptor=read(rt,descPage,0x1000),out=read(rt,destPage,regionSize);
    invoke(rt,dispatch,desc,0,true);
    equal(rt,descPage,descriptor,"Empty dispatcher touched descriptor or kernel cache");
    equal(rt,destPage,out,"Empty dispatcher touched PCM");
    Audio::DacGainState state{256,outputFull?0u:256u,256,outputFull?256u:0u,
        std::bit_cast<float>(0x3EAAAAABu),1.0f};const auto before=state;
    HostFP fp(true);const auto csr=PPCFPSCRRegister::getcsr();
    const auto result=Audio::processDacGain({},{},state);
    need(result.work==Audio::DacGainWork::NoWork && result.frames==0 &&
        !std::memcmp(&state,&before,sizeof(state)),"Native no-work mutated state");
    need(PPCFPSCRRegister::getcsr()==csr,"Native no-work leaked host FP state");++nativeEmpty;
}
void rejectionCases(Runtime& rt) {
    setup(rt,0,0x3F800000,1,0,1);
    const auto descriptor=read(rt,descPage,0x1000),out=read(rt,destPage,regionSize);
    // Guest pages below 0x10000 demand-map as zero-filled null-device scratch;
    // 0x30000 lies between this fixture's stack and descriptor maps and stays unmapped.
    invoke(rt,scalar,0x30000,0,true,-1,"Unmapped/protected guest read address=0x00030008");
    equal(rt,descPage,descriptor,"Invalid descriptor changed valid descriptor");
    equal(rt,destPage,out,"Invalid descriptor changed output");
    {
        ReadOnly guard(rt,destPage,regionSize);
        invoke(rt,scalar,desc,0,true,-1,"Unmapped/protected guest write address=0x00061480");
    }
    equal(rt,descPage,descriptor,"Rejected first output store advanced gain/counters");
    equal(rt,destPage,out,"Rejected first output store changed output");
    // The first load in its reverse-lane store sequence is source+20. Leave
    // exactly the first five floats readable; the sixth must fail before stores.
    put(rt,desc,sourcePage+regionSize-20);
    const auto badInputDesc=read(rt,descPage,0x1000);
    invoke(rt,scalar,desc,0,true,-1,"Unmapped/protected guest read address=0x00052000");
    equal(rt,descPage,badInputDesc,"Rejected first input load advanced descriptor");
    equal(rt,destPage,out,"Rejected first input load changed output");
    // No rollback is promised for a fault after earlier lane/frame stores.
}
void nativeReject(std::span<const float> input,std::span<float> output,
                  Audio::DacGainState& state,const char* message) {
    const std::vector<float> inputBefore(input.begin(),input.end()),outputBefore(output.begin(),output.end());
    const auto stateBefore=state;
    HostFP restore(false);
    // All exception masks clear, pending flags set, nondefault rounding and
    // FTZ/DAZ: even signaling-NaN validation must not perform floating arithmetic.
    const uint32_t csr=0x3F|0x6000|PPCFPSCRRegister::FlushMask;
    PPCFPSCRRegister::restoreHostCSR(csr);
    bool rejected=false;
    try {Audio::processDacGain(input,output,state);}
    catch(const Audio::DacGainError& e) {need(std::string(e.what())==message,"Unexpected native gain rejection");rejected=true;}
    need(PPCFPSCRRegister::getcsr()==csr,"Native rejection leaked host FP state");
    need(rejected,"Native gain accepted invalid work");
    need(!std::memcmp(&state,&stateBefore,sizeof(state)),"Native rejection changed state");
    if(!input.empty()) need(!std::memcmp(input.data(),inputBefore.data(),input.size_bytes()),"Native rejection changed source");
    if(!output.empty()) need(!std::memcmp(output.data(),outputBefore.data(),output.size_bytes()),"Native rejection changed output");
    ++nativeRejections;
}
void nativeBoundaryCases() {
    using Audio::DacGainState;
    std::array<float,Audio::dacGainSamples+1> input,output;
    input.fill(1.0f);output.fill(std::bit_cast<float>(0xC9C9C9C9u));
    const auto src=std::span(input).first(6),dst=std::span(output).first(1536);
    const DacGainState initial{1,0,1,0,0.5f,1.0f};
    for(unsigned field=0;field<4;++field) {
        auto s=initial;
        if(field==0) s.sourceFrames=257;
        if(field==1) s.destinationFrames=257;
        if(field==2) s.sourceProgress=2;
        if(field==3) s.destinationProgress=2;
        nativeReject(src,dst,s,"Dac gain invalid frame counters");
    }
    auto s=initial;
    nativeReject(src.first(5),dst,s,"Dac gain insufficient span capacity");
    nativeReject(src,dst.first(1280),s,"Dac gain insufficient span capacity");
    nativeReject(input,dst,s,"Dac gain span exceeds bounded block");
    nativeReject(src,output,s,"Dac gain span exceeds bounded block");
    nativeReject({},dst,s,"Dac gain insufficient span capacity");
    nativeReject(src,{},s,"Dac gain insufficient span capacity");
    // Exact and partial overlap, including overlap confined to an otherwise
    // inactive destination tail. Full supplied spans intentionally cannot alias.
    nativeReject(dst.first(6),dst,s,"Dac gain overlapping storage");
    nativeReject(std::span(output).subspan(1531,6),dst,s,"Dac gain overlapping storage");
    nativeReject(std::span<const float>(&s.current,1),dst,s,"Dac gain overlapping storage");
    nativeReject(src,std::span<float>(&s.target,1),s,"Dac gain overlapping storage");
    for(uint32_t bits:{0x7FC12345u,0x7F800001u,0x7F800000u,0xFF800000u}) {
        s=initial;s.current=std::bit_cast<float>(bits);
        nativeReject(src,dst,s,"Dac gain nonfinite current/target");
        s=initial;s.target=std::bit_cast<float>(bits);
        nativeReject(src,dst,s,"Dac gain nonfinite current/target");
        s=initial;input[5]=std::bit_cast<float>(bits);
        nativeReject(src,dst,s,"Dac gain nonfinite source sample");input[5]=1.0f;
    }
    // Invalid gain state is rejected even on an otherwise empty request.
    s=initial;s.sourceProgress=1;s.target=std::bit_cast<float>(0x7FC12345u);
    nativeReject({},{},s,"Dac gain nonfinite current/target");
    s=initial;s.current=std::bit_cast<float>(0xFF7FFFFFu);s.target=std::bit_cast<float>(0x7F7FFFFFu);
    nativeReject(src,dst,s,"Dac gain nonfinite gain difference");
    // Finite difference/step, but the tenth repeated addition overflows. The
    // integer oracle proves the late rejection case; no staged output escapes.
    const auto step=divide(0x7F7FFFff,rounded(0,10,1,0));uint32_t accumulated=0;
    for(unsigned i=0;i<10;++i) accumulated=add(accumulated,step);
    need(accumulated==0x7F800000,"Late-overflow integer oracle anchor");++oracleChecks;
    s={10,0,10,0,0.0f,std::bit_cast<float>(0x7F7FFFFFu)};
    nativeReject(std::span(input).first(60),dst,s,"Dac gain nonfinite advanced current");

    // Exact minimal destination extent, invalid values only outside consumed
    // source positions, and plane tails prove preflight is scoped to actual work.
    input[0]=std::bit_cast<float>(0x7F800001u);
    input[12]=std::bit_cast<float>(0x7FC12345u);
    s={3,1,1,0,1.0f,1.0f};
    const auto result=Audio::processDacGain(std::span(input).first(18),dst.first(1281),s);
    need(result.work==Audio::DacGainWork::Processed && result.frames==1 &&
         s.sourceProgress==2 && s.destinationProgress==1,"Native minimum extent/consumed input domain");
    for(unsigned lane=0;lane<6;++lane)
        need(std::bit_cast<uint32_t>(output[lane*256])==0x3F800000,"Native minimum extent output");
    ++nativeCalls;
    // Legitimate zero-size state, no spans and no gain mutation.
    s={0,0,0,0,-0.0f,-0.0f};const auto before=s;
    const auto empty=Audio::processDacGain({},{},s);
    need(empty.work==Audio::DacGainWork::NoWork && empty.frames==0 &&
         !std::memcmp(&s,&before,sizeof(s)),"Native zero-size no-work");++nativeEmpty;
}
void nativeFPEnvironments() {
    for(uint32_t gain:{0x3F000000u,0x40000000u}) {
        std::array<float,18> input;
        for(unsigned i=0;i<input.size();++i) input[i]=std::bit_cast<float>(sample(i/6,i%6,false));
        std::array<uint32_t,18> expected;
        for(unsigned i=0;i<input.size();++i) expected[i]=multiply(std::bit_cast<uint32_t>(input[i]),gain);
        for(uint32_t rounding:{0u,0x2000u,0x4000u,0x6000u}) for(uint32_t flush:{0u,0x8040u}) {
            std::array<float,1536> output;output.fill(std::bit_cast<float>(0xC1234567u));
            Audio::DacGainState s{3,0,3,0,std::bit_cast<float>(gain),std::bit_cast<float>(gain)};
            HostFP restore(false);const uint32_t csr=rounding|flush|0x3F; // exception masks clear
            PPCFPSCRRegister::restoreHostCSR(csr);
            const auto result=Audio::processDacGain(input,output,s);
            need(PPCFPSCRRegister::getcsr()==csr,"Native gain arbitrary caller MXCSR restoration");
            need(result.work==Audio::DacGainWork::Processed && result.frames==3 &&
                 s.sourceProgress==3 && s.destinationProgress==3 &&
                 std::bit_cast<uint32_t>(s.current)==gain && std::bit_cast<uint32_t>(s.target)==gain,
                 "Native hostile FP result/state");
            for(unsigned lane=0;lane<6;++lane) for(unsigned j=0;j<256;++j)
                need(std::bit_cast<uint32_t>(output[lane*256+j])==(j<3?expected[j*6+lane]:0xC1234567u),
                     "Native arbitrary caller FP exact sample/guard");
            ++nativeCalls;
        }
    }
}
}

int main(int argc,char** argv) {
    try {
        need(argc==2,"Usage: DacGainTests analysis/simpsons.pe");oracleSelfTests();
        Runtime rt;rt.load(argv[1]);
        rt.map(stack,stackSize,true,"Dac gain fixture stack");
        rt.map(descPage,0x1000,true,"Dac gain CPU descriptor");
        rt.map(sourcePage,regionSize,true,"Dac gain synthetic interleaved source");
        rt.map(destPage,regionSize,true,"Dac gain synthetic planar destination");
        const auto sdk=read(rt,0x82E2D9F0,8),root=read(rt,0x82E31BCC,4);
        pin(rt,scalar,0x150,"1ca0a94c553d1c67e6a37b8688f5e3f94886438c2c7709da2e9222fa613cd718");
        pin(rt,dispatch,0x1BC,"7f564327c4955401b299e362a74d742a06e281c748246ea5c74601f3362a3da8");
        pin(rt,vectorKernel,0x350,"8054ed90040b68c5125cc26e0a396a437892f6e7cd48970ac36917b9d0dab68e");
        need(get(rt,0x821B2D30)==vectorKernel && get(rt,0x82C6A144)==0x4BFFA81D,
            "Observed equal-rate six-channel scalar dispatch pins");
        need(get(rt,0x82C474B8)==0xC01A0070 && get(rt,0x82C474C0)==0xD01A00E0 &&
             get(rt,0x82C474C8)==0xC01E0028 && get(rt,0x82C474D0)==0xD01E0024,
             "First activation current/target initialization pins");
        for(bool hostile:{false,true}) {
            // The first-activation CPU precondition is equal current and target.
            // Compare it with an actual ramp; do not construct a fake SDK source.
            for(uint32_t gain:{0x3F800000u,0x3F000000u,0xBF800000u,0u,0x80000000u}) {
                setup(rt,gain,gain);checkRun(rt,hostile);
            }
            setup(rt,0,0x3F800000);checkRun(rt,hostile);
            setup(rt,0x3F800000,0);checkRun(rt,hostile);
            setup(rt,0x40000000,0x40000000,7,0,7);checkRun(rt,hostile); // finite overflow samples
            setup(rt,0x3E000000,0x3F500000,9,3,9,2);checkRun(rt,hostile); // denominator7, consume6
            setup(rt,0x3EAAAAAB,0x3F800000,32,2,12,9);checkRun(rt,hostile); // dest-limited3
            setup(rt,1,3,3,0,3);checkRun(rt,hostile);
            need(get(rt,desc+0x24)==4,"Rounded subnormal ramp must overshoot, not force target");
            setup(rt,0x00800000,0x00800001,3,0,3);checkRun(rt,hostile);
            need(get(rt,desc+0x24)==0x00800000,"Subnormal step rounds to zero, not target");
            // Same destination block, successively exposed source packets. Each
            // invocation recomputes step from remaining destination and current.
            setup(rt,0x3EAAAAAB,0x3F555555,17,0,17);
            Audio::DacGainState retained{17,0,17,0,std::bit_cast<float>(0x3EAAAAABu),std::bit_cast<float>(0x3F555555u)};
            for(uint32_t available:{3u,8u,17u}) {put(rt,desc+4,available);checkRun(rt,hostile,false,-1,&retained);}
            // Observed source S+44 is four bytes off16 alignment. Execute the
            // real dispatcher and optimized entry, and prove scalar fallback.
            setup(rt,0,0x3F800000);checkRun(rt,hostile,true,1);
            // Aligned endpoints at mapped-page ends, odd count still falls back.
            setup(rt,0x3EAAAAAB,0x3F800000,255,0,256,0,sourcePage+0x800,destPage+0x800);
            checkRun(rt,hostile,true,1);
            // Exact normal steady-gain case only: identify vector alternative;
            // this does not compare arbitrary vector rounding/NaN/subnormals.
            setup(rt,0x3F800000,0x3F800000,256,0,256,0,sourcePage+0x800,destPage+0x800,true);
            checkRun(rt,hostile,true,0);
        }
        emptyDispatcher(rt,false);emptyDispatcher(rt,true);rejectionCases(rt);
        nativeBoundaryCases();nativeFPEnvironments();
        equal(rt,0x82E2D9F0,sdk,"CPU fixture changed SDK singleton globals");
        equal(rt,0x82E31BCC,root,"CPU fixture changed/fabricated audio root");
        need(!rt.engineAudio && rt.threads.empty() && rt.handles.empty() && rt.allocations.empty() &&
            rt.physicalAllocations.empty(),"CPU gain fixture acquired runtime/device resources");
        std::printf("PASS original/native Dac gain: %zu AOT calls, %zu exact sample comparisons, %zu integer-oracle anchors; %zu native processed, %zu native rejections, %zu native no-work; guards/ABI/MXCSR; no output device\n",
            cases,sampleChecks,oracleChecks,nativeCalls,nativeRejections,nativeEmpty);return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL original Dac gain: %s\n",e.what());return 1;}
}
