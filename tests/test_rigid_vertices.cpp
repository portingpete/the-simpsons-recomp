#include "runtime/rigid_vertices.h"
#include "runtime/runtime.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <xmmintrin.h>

namespace {
using Bytes=std::vector<uint8_t>;
size_t checks{};
void need(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
void put(Bytes& b,size_t at,uint32_t v) {
    b.at(at)=uint8_t(v>>24);b.at(at+1)=uint8_t(v>>16);b.at(at+2)=uint8_t(v>>8);b.at(at+3)=uint8_t(v);
}
uint32_t word(const Bytes& b,size_t at){return uint32_t(b.at(at))<<24|uint32_t(b.at(at+1))<<16|uint32_t(b.at(at+2))<<8|b.at(at+3);}
Bytes declaration() {
    constexpr uint32_t rows[][3]={{0,0x002A23B9,0},{12,0x002A2187,0x00030000},
        {16,0x00182886,0x000A0000},{20,0x002C23A5,0x00050000},{0x00FF0000,UINT32_MAX,0}};
    Bytes b(60);for(size_t i=0;i<5;++i)for(size_t j=0;j<3;++j)put(b,i*12+j*4,rows[i][j]);return b;
}
template<class F>void rejects(F fn,const char* why){bool rejected=false;try{fn();}catch(const Simpsons::Failure&){rejected=true;}need(rejected,why);}
Bytes read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);need(bool(f),"Missing captured rigid data");return Bytes(std::istreambuf_iterator<char>(f),{});}

// Independent scalar hardware oracle: sign is determined by magnitude, and
// the two clamped codes are selected by integer identity rather than the
// decoder's std::max expression. A volatile result keeps the real division
// observable even when its eventual output is the clamped -1 bit pattern.
__declspec(noinline) uint32_t scalarNormalBits(uint32_t raw) {
#ifdef __clang__
#pragma clang fp exceptions(strict)
#endif
    const auto signedMagnitude=raw<512?int32_t(raw):-int32_t(1024u-raw);
    volatile float divided=_mm_cvtss_f32(_mm_div_ss(
        _mm_cvtsi32_ss(_mm_setzero_ps(),signedMagnitude),_mm_set_ss(511.0f)));
    return raw==512||raw==513?0xBF800000u:std::bit_cast<uint32_t>(float(divided));
}
void ownedLastRecordBoundaries() {
    using Simpsons::decodeRigidVertices;
    // Offsets deliberately leave large padding and put optional inputs later.
    // These literal ends are independent owned-span boundaries for each mode.
    constexpr uint32_t stride=96;
    constexpr std::array<uint32_t,8> ends={48,80,76,80,64,80,76,80};
    constexpr uint32_t rows[][3]={{0,0x002A23B9,0},{60,0x002A2187,0x00030000},
        {16,0x00182886,0x000A0000},{40,0x002C23A5,0x00050000},
        {68,0x002C23A5,0x00050100},{76,0x002A2187,0x00060000},
        {0x00FF0000,UINT32_MAX,0}};
    Bytes decl(sizeof(rows));
    for(size_t i=0;i<std::size(rows);++i)for(size_t lane=0;lane<3;++lane)put(decl,12*i+4*lane,rows[i][lane]);
    const auto sameBits=[&](const auto& got,const auto& expected) {
        need(got.size()==expected.size(),"Owned rigid record count changed");
        for(size_t i=0;i<got.size();++i) {
            const auto arrayBits=[&](const auto& a,const auto& b) {
                for(size_t lane=0;lane<a.size();++lane)
                    need(std::bit_cast<uint32_t>(a[lane])==std::bit_cast<uint32_t>(b[lane]),
                         "Unused final rigid padding changed a consumed or zero attribute");
            };
            arrayBits(got[i].position,expected[i].position);arrayBits(got[i].normal,expected[i].normal);
            arrayBits(got[i].color,expected[i].color);arrayBits(got[i].uv,expected[i].uv);
            arrayBits(got[i].uv1,expected[i].uv1);arrayBits(got[i].tangent,expected[i].tangent);
        }
    };
    for(uint32_t mode=0;mode<ends.size();++mode) {
        const bool normal=(mode&4)!=0,uv1=(mode&2)!=0,tangent=(mode&1)!=0;
        Bytes full(2*stride,0xCD);
        for(size_t i=0;i<2;++i) {
            const size_t at=i*stride;
            put(full,at,0x80000000);put(full,at+4,0x3F800000+uint32_t(i));put(full,at+8,0xBF800000);
            put(full,at+16,0x80402010+uint32_t(i));
            put(full,at+40,0x3E800001+uint32_t(i));put(full,at+44,0x80000000);
            put(full,at+60,512u|(513u<<10)|(511u<<20));
            put(full,at+68,uv1?0xBF000001:0x7FC12345);put(full,at+72,0x3F000001+uint32_t(i));
            put(full,at+76,0xC0100801);
        }
        const auto original=full;
        const auto complete=decodeRigidVertices(full,decl,stride,uv1,tangent,normal);
        need(complete.size()==2&&full==original,"Complete rigid owner changed source or count");
        Bytes owned(full.begin(),full.begin()+stride+ends[mode]);const auto ownedOriginal=owned;
        sameBits(decodeRigidVertices(owned,decl,stride,uv1,tangent,normal),complete);
        need(owned==ownedOriginal,"Padding-truncated rigid owner was mutated");
        auto missing=owned;missing.resize(missing.size()-4);
        const auto firstOnly=decodeRigidVertices(missing,decl,stride,uv1,tangent,normal);
        need(firstOnly.size()==1,"Missing final consumed word acquired another rigid record");
        sameBits(firstOnly,std::vector<Simpsons::Graphics::RigidVertex>{complete[0]});
        auto bad=owned;const auto floatOffset=uv1?72u:44u;
        put(bad,stride+floatOffset,0x7FC12345);
        rejects([&]{decodeRigidVertices(bad,decl,stride,uv1,tangent,normal);},
                "Nonfinite owned final rigid floating word was ignored");
        // A short extra record owns a NaN position but lacks its final required
        // word: it must remain wholly unfetched rather than cause a rejection.
        auto shortTail=full;shortTail.resize(2*stride+ends[mode]-4,0xCD);
        put(shortTail,2*stride,0x7FC12345);const auto tailOriginal=shortTail;
        sameBits(decodeRigidVertices(shortTail,decl,stride,uv1,tangent,normal),complete);
        need(shortTail==tailOriginal,"Unused short rigid record was mutated");
        Bytes noComplete(full.begin(),full.begin()+ends[mode]-4);
        rejects([&]{decodeRigidVertices(noComplete,decl,stride,uv1,tangent,normal);},
                "Rigid owner without one complete consumed record was accepted");
    }
}
void normalOnlyFloatingPointStatus() {
    struct RestoreCSR {
        const uint32_t saved=_mm_getcsr();
        ~RestoreCSR(){_mm_setcsr(saved);}
    } restore;
    constexpr uint32_t sticky=0x3F,masks=0x1F80,roundingMask=0x6000,flush=0x8040;
    const auto initial=(restore.saved|masks)&~(sticky|roundingMask|flush);
    const auto decl=declaration();
    // Every other consumed attribute is exact zero. Its arithmetic cannot
    // legitimately set precision or invalid, and no tangent/UV1 is consumed.
    Bytes source(28,0);
    for(uint32_t mode:{0u,0x2000u,0x4000u,0x6000u})for(uint32_t raw=0;raw<1024;++raw) {
        const uint32_t packed=raw|(raw<<10)|(raw<<20)|((raw&3)<<30);
        put(source,12,packed);
        const uint32_t control=initial|mode;
        _mm_setcsr(control);
        const auto inactive=Simpsons::decodeRigidVertices(source,decl,28,false,false,false);
        const auto inactiveCSR=_mm_getcsr();
        need(inactive.size()==1&&inactiveCSR==control,"Dead normal or zero attributes changed FP status/control");
        for(float value:inactive[0].normal)
            need(std::bit_cast<uint32_t>(value)==0,"Unconsumed normal was not exact zero");

        _mm_setcsr(control);
        std::array<uint32_t,3> expected;
        for(auto& bits:expected)bits=scalarNormalBits(raw);
        const auto oracleCSR=_mm_getcsr();
        // n/511 is binary-exact only for n=0,+511,-511. Raw512 still
        // performs an inexact -512/511 before clamping, so its flag is kept.
        const uint32_t expectedFlags=raw==0||raw==511||raw==513?0u:0x20u;
        need((oracleCSR&sticky)==expectedFlags&&(oracleCSR&~sticky)==control,
             "Scalar normal oracle did not execute exact/inexact division under requested rounding");

        _mm_setcsr(control);
        const auto decoded=Simpsons::decodeRigidVertices(source,decl,28,false,false,true);
        const auto actualCSR=_mm_getcsr();
        need(decoded.size()==1,"Normal FP fixture acquired another vertex");
        need((actualCSR&~sticky)==control,"Normal decoder changed caller FP exception masks, rounding or flush mode");
        need((actualCSR&sticky)==expectedFlags,"Normal decoder added invalid or changed scalar division sticky flags");
        for(size_t lane=0;lane<3;++lane)
            need(std::bit_cast<uint32_t>(decoded[0].normal[lane])==expected[lane],
                 "Normal decoder output bits differ from scalar division/clamp under requested rounding");
    }
}
struct NormalRestoreCSR {
    const uint32_t saved=_mm_getcsr();
    ~NormalRestoreCSR(){_mm_setcsr(saved);}
};
constexpr uint32_t normalSticky=0x3F,normalMasks=0x1F80,normalPrecisionMask=0x1000;
constexpr uint32_t normalRounding=0x6000,normalFlush=0x8040;
uint32_t packedNormal(const std::array<uint32_t,3>& raw,uint32_t high=0) {
    return raw[0]|(raw[1]<<10)|(raw[2]<<20)|(high&0xC0000000u);
}
void normalOutput(const std::vector<Simpsons::Graphics::RigidVertex>& decoded,
                  const std::array<uint32_t,3>& expected) {
    need(decoded.size()==1,"Masked/fallback normal fixture acquired another vertex");
    const auto& v=decoded[0];
    for(size_t lane=0;lane<3;++lane)
        need(std::bit_cast<uint32_t>(v.normal[lane])==expected[lane],
             "Mixed normal lane differs from independent scalar bits");
    const auto zeros=[&](const auto& values) {
        for(float value:values)need(std::bit_cast<uint32_t>(value)==0,
            "Normal-only decode overwrote a separate or unconsumed attribute");
    };
    zeros(v.position);zeros(v.color);zeros(v.uv);zeros(v.uv1);zeros(v.tangent);
}
void normalMaskedAndFallbackStatus() {
    NormalRestoreCSR restore;
    const uint32_t base=(restore.saved|normalMasks)&~(normalSticky|normalRounding|normalFlush);
    const auto decl=declaration(),savedDecl=decl;
    Bytes source(28,0);
    const auto check=[&](const std::array<uint32_t,3>& raw,uint32_t control,uint32_t high) {
        put(source,12,packedNormal(raw,high));const auto saved=source;
        // Gold uses the same rounding/flush mode, with all exceptions masked.
        _mm_setcsr(control|normalMasks);
        std::array<uint32_t,3> expected{};uint32_t flags=control&normalSticky;
        for(size_t lane=0;lane<3;++lane) {
            expected[lane]=scalarNormalBits(raw[lane]);
            if(raw[lane]!=0&&raw[lane]!=511&&raw[lane]!=513)flags|=0x20;
        }
        const auto oracleCSR=_mm_getcsr();
        need(oracleCSR==((control|normalMasks)|flags),"Mixed scalar normal oracle status differs");
        _mm_setcsr(control);
        const auto decoded=Simpsons::decodeRigidVertices(source,decl,28,false,false,true);
        const auto actualCSR=_mm_getcsr();
        need(actualCSR==(control|flags),"Masked/fallback normal changed seeded flags or caller control");
        normalOutput(decoded,expected);
        need(source==saved&&decl==savedDecl,"Normal status fixture mutated its owned inputs");
    };
    constexpr std::array<std::array<uint32_t,3>,6> patterns={{{0,511,513},{512,0,511},
        {511,512,513},{1,1023,0},{513,511,512},{512,512,512}}};
    for(uint32_t round:{0u,0x2000u,0x4000u,0x6000u})for(uint32_t flush:{0u,0x40u,0x8000u,0x8040u}) {
        const auto control=base|round|flush;
        // Each encoding occurs in each lane with different neighbors.
        for(uint32_t raw=0;raw<1024;++raw)
            check({raw,(raw+341)&1023,(raw+683)&1023},control,(raw&3)<<30);
        for(uint32_t seed:{1u,0x15u,0x3Fu})for(const auto& pattern:patterns)
            check(pattern,control|seed,0xC0000000u);
        // One clear mask at a time: exact 0/+511/-511 avoids a trap.
        for(uint32_t clear:{0x80u,0x100u,0x200u,0x400u,0x800u,0x1000u})
            check({0,511,513},control&~clear,0x80000000u);
    }
    put(source,12,packedNormal({1,512,1023}));
    const auto failure=[&](Bytes data,Bytes declarationBytes,const char* expected,uint32_t flags) {
        const auto saved=data,savedDeclaration=declarationBytes;
        _mm_setcsr(base);bool rejected=false;uint32_t actualCSR=0;
        try {Simpsons::decodeRigidVertices(data,declarationBytes,28,false,false,true);}
        catch(const Simpsons::Failure& error) {
            actualCSR=_mm_getcsr();
            need(std::strcmp(error.what(),expected)==0,"Normal error type/message/order changed");
            rejected=true;
        }
        need(rejected&&actualCSR==(base|flags),"Normal failure frontier changed FP status");
        need(data==saved&&declarationBytes==savedDeclaration,"Rejected normal input was mutated");
    };
    auto badDecl=decl;put(badDecl,48,1);
    failure(source,badDecl,"Rigid declaration terminator differs",0);
    auto early=source;put(early,0,0x7FC12345);
    failure(early,decl,"Nonfinite rigid vertex attribute",0);
    auto late=source;put(late,20,0x7FC12345);
    failure(late,decl,"Nonfinite rigid vertex attribute",0x20);
}
struct PrecisionTrap {
    uint32_t count{},code{},csr{},fxcsr{};
    uintptr_t rip{};
};
// Local precision-only retry: no unwinding, allocation, FP arithmetic,
// global handler or RIP advancement. Both x64 saved CSR fields are distinct.
LONG precisionFilter(EXCEPTION_POINTERS* exception,PrecisionTrap* trap) noexcept {
    if(!exception||!exception->ExceptionRecord||!exception->ContextRecord||!trap||trap->count)
        return EXCEPTION_CONTINUE_SEARCH;
    const auto* record=exception->ExceptionRecord;auto* context=exception->ContextRecord;
    const DWORD required=CONTEXT_CONTROL|CONTEXT_FLOATING_POINT;
    const auto code=record->ExceptionCode;
    if((record->ExceptionFlags&EXCEPTION_NONCONTINUABLE)||(context->ContextFlags&required)!=required||
       (code!=EXCEPTION_FLT_INEXACT_RESULT&&code!=STATUS_FLOAT_MULTIPLE_FAULTS&&code!=STATUS_FLOAT_MULTIPLE_TRAPS)||
       !context->Rip||record->ExceptionAddress!=reinterpret_cast<void*>(context->Rip))
        return EXCEPTION_CONTINUE_SEARCH;
    const uint32_t csr=context->MxCsr,fxcsr=context->FltSave.MxCsr;
    if((csr&normalMasks)!=(normalMasks&~normalPrecisionMask)||
       (csr&normalSticky)!=0x20||(csr&0xFFFF0000u))
        return EXCEPTION_CONTINUE_SEARCH;
    trap->count=1;trap->code=code;trap->csr=csr;trap->fxcsr=fxcsr;trap->rip=uintptr_t(context->Rip);
    const uint32_t repaired=(csr|normalPrecisionMask)&~normalSticky;
    context->MxCsr=repaired;
    context->FltSave.MxCsr=repaired;
    return EXCEPTION_CONTINUE_EXECUTION;
}
__declspec(noinline) void precisionDecode(const Bytes* source,const Bytes* decl,bool normal,
                                        std::vector<Simpsons::Graphics::RigidVertex>* output) {
    *output=Simpsons::decodeRigidVertices(*source,*decl,28,false,false,normal);
}
// Only trivial parameters/locals cross __try. The separate C++ decoder's
// vector completes and destructs normally after retrying the same instruction.
__declspec(noinline) void precisionRetry(const Bytes* source,const Bytes* decl,bool normal,
    std::vector<Simpsons::Graphics::RigidVertex>* output,PrecisionTrap* trap) {
    __try {precisionDecode(source,decl,normal,output);}
    __except(precisionFilter(GetExceptionInformation(),trap)) {}
}
void normalPrecisionRetry() {
    NormalRestoreCSR restore;
    const uint32_t base=(restore.saved|normalMasks)&~(normalSticky|normalRounding|normalFlush);
    const auto decl=declaration(),savedDecl=decl;
    constexpr std::array<std::array<uint32_t,3>,3> patterns={{{1,511,513},{0,512,511},{0,511,1023}}};
    size_t traps=0;PrecisionTrap lastTrap{};
    for(uint32_t round:{0u,0x2000u,0x4000u,0x6000u})for(uint32_t flush:{0u,0x40u,0x8000u,0x8040u}) {
        const auto control=(base|round|flush)&~normalPrecisionMask;
        for(const auto& raw:patterns) {
            Bytes source(28,0);put(source,12,packedNormal(raw,0xC0000000u));const auto saved=source;
            _mm_setcsr(control|normalMasks);
            std::array<uint32_t,3> expected{};
            for(size_t lane=0;lane<3;++lane)expected[lane]=scalarNormalBits(raw[lane]);
            std::vector<Simpsons::Graphics::RigidVertex> output;PrecisionTrap trap{};
            _mm_setcsr(control);
            precisionRetry(&source,&decl,true,&output,&trap);
            const auto actualCSR=_mm_getcsr();
            need(trap.count==1&&trap.rip&&trap.csr==(control|0x20),
                 "Precision-only fallback did not reach exactly one resumable frontier");
            need(trap.code==EXCEPTION_FLT_INEXACT_RESULT||trap.code==STATUS_FLOAT_MULTIPLE_FAULTS||
                 trap.code==STATUS_FLOAT_MULTIPLE_TRAPS,"Precision retry accepted an unrelated exception code");
            need(actualCSR==(control|normalMasks|0x20),"Precision retry changed masks/rounding/flush or final flags");
            normalOutput(output,expected);
            need(source==saved&&decl==savedDecl,"Precision retry mutated its owned inputs");
            lastTrap=trap;++traps;
        }
        Bytes source(28,0);put(source,12,packedNormal({1,512,1023}));const auto saved=source;
        std::vector<Simpsons::Graphics::RigidVertex> output;PrecisionTrap inactive{};
        _mm_setcsr(control);
        precisionRetry(&source,&decl,false,&output,&inactive);
        const auto inactiveCSR=_mm_getcsr();
        need(!inactive.count&&inactiveCSR==control,"Dead normal created a precision trap or sticky flag");
        normalOutput(output,{0,0,0});
        need(source==saved,"Inactive precision control mutated its source");
        const auto failed=[&](size_t at,uint32_t expectedTraps,uint32_t expectedCSR) {
            auto bad=source;put(bad,at,0x7FC12345);const auto savedBad=bad;
            PrecisionTrap trap{};std::vector<Simpsons::Graphics::RigidVertex> rejectedOutput;
            bool rejected=false;uint32_t actualCSR=0;
            _mm_setcsr(control);
            try {precisionRetry(&bad,&decl,true,&rejectedOutput,&trap);}
            catch(const Simpsons::Failure& error) {
                actualCSR=_mm_getcsr();
                need(std::strcmp(error.what(),"Nonfinite rigid vertex attribute")==0,
                     "Precision fallback failure type/message changed");
                rejected=true;
            }
            need(rejected&&trap.count==expectedTraps&&actualCSR==expectedCSR,
                 "Precision fallback error ordering or exception frontier changed");
            need(rejectedOutput.empty()&&bad==savedBad&&decl==savedDecl,
                 "Precision fallback rejection published output or mutated input");
            if(expectedTraps)need(trap.rip&&trap.csr==(control|0x20),
                "Late UV failure lost the precision trap receipt");
        };
        failed(0,0,control);
        failed(20,1,control|normalMasks|0x20);
    }
    std::printf("Verified normal precision retry: %zu single-trap controls, last code=%08X csr=%08X fxcsr=%08X rip=%llX. ",
        traps,lastTrap.code,lastTrap.csr,lastTrap.fxcsr,static_cast<unsigned long long>(lastTrap.rip));
}

}
int main(int argc,char** argv)try {
    using Simpsons::decodeRigidVertices;
    normalOnlyFloatingPointStatus();
    const auto callerNormalCSR=_mm_getcsr();
    normalMaskedAndFallbackStatus();
    need(_mm_getcsr()==callerNormalCSR,"Masked/fallback fixture did not restore caller CSR");
    normalPrecisionRetry();
    need(_mm_getcsr()==callerNormalCSR,"Precision fixture did not restore caller CSR");
    ownedLastRecordBoundaries();
    auto decl=declaration();Bytes source(1024*28);
    for(uint32_t i=0;i<1024;++i) {
        const auto at=size_t(i)*28;
        put(source,at,0x80000000);put(source,at+4,0x3E000001);put(source,at+8,0xC47A0000);
        put(source,at+12,i|((1023-i)<<10)|(((i+513)&1023)<<20)|((i&3)<<30));
        put(source,at+16,((i&255)<<24)|(((i+17)&255)<<16)|(((i+83)&255)<<8)|((i+211)&255));
        put(source,at+20,0x80000000);put(source,at+24,0x3F000001);
    }
    const auto original=source;auto vertices=decodeRigidVertices(source,decl,28);
    need(vertices.size()==1024&&source==original,"Rigid decoder changed source or vertex extent");
    for(size_t i=0;i<vertices.size();++i) {
        const auto& v=vertices[i];
        for(size_t j=0;j<3;++j)need(std::bit_cast<uint32_t>(v.position[j])==word(source,28*i+4*j),"Rigid position bits changed");
        for(size_t j=0;j<2;++j)need(std::bit_cast<uint32_t>(v.uv[j])==word(source,28*i+20+4*j),"Rigid UV bits changed");
        const auto packed=word(source,28*i+12);
        for(size_t j=0;j<3;++j) {
            const auto raw=(packed>>(10*j))&1023;
            // Independent magnitude/sign oracle covers every encoding per lane.
            const double expected=raw<512?double(raw)/511.0:-std::min(1.0,double(1024-raw)/511.0);
            need(v.normal[j]==float(expected),"Rigid signed normal decoding differs");
        }
        const auto color=word(source,28*i+16);
        const uint32_t channels[]={(color>>16)&255,(color>>8)&255,color&255,color>>24};
        for(size_t j=0;j<4;++j)need(v.color[j]==float(double(channels[j])/255.0),"Rigid color normalization/channel differs");
    }
    Bytes one(source.begin(),source.begin()+28);
    // LIVE029 contains referenced 00000000 packed normals. Dead high bits
    // cannot turn these into fallback directions or modify source attributes.
    for(uint32_t high:{0u,0x40000000u,0x80000000u,0xC0000000u}) {
        auto zero=one;put(zero,12,high);const auto saved=zero;
        const auto decoded=decodeRigidVertices(zero,decl,28);
        need(zero==saved&&decoded.size()==1,"Zero-normal decoding changed original storage");
        for(float n:decoded[0].normal)need(std::bit_cast<uint32_t>(n)==0,"Original zero normal was replaced");
        need(decoded[0].position==vertices[0].position&&decoded[0].color==vertices[0].color&&
             decoded[0].uv==vertices[0].uv,"Zero-normal decoding changed other attributes");
    }
    for(size_t at:{size_t(0),size_t(4),size_t(8),size_t(20),size_t(24)})for(uint32_t invalid:{0x7F800000u,0xFF800000u,0x7FC12345u}) {
        auto bad=one;put(bad,at,invalid);rejects([&]{decodeRigidVertices(bad,decl,28);},"Nonfinite consumed rigid input accepted");
    }
    for(uint32_t stride:{0u,27u,29u,1024u})rejects([&]{decodeRigidVertices(one,decl,stride);},"Invalid rigid stride accepted");
    rejects([&]{decodeRigidVertices({},decl,28);},"Empty rigid stream accepted");
    for(size_t row=0;row<4;++row)for(size_t field=0;field<3;++field) {
        auto bad=decl;put(bad,12*row+4*field,UINT32_MAX);
        rejects([&]{decodeRigidVertices(one,bad,28);},"Invalid rigid declaration row accepted");
    }
    {auto bad=decl;put(bad,12+8,0);rejects([&]{decodeRigidVertices(one,bad,28);},"Duplicate rigid input accepted");}
    {auto bad=decl;put(bad,12,0x0001000C);rejects([&]{decodeRigidVertices(one,bad,28);},"Alternate rigid stream accepted");}
    {auto bad=decl;put(bad,12,27);rejects([&]{decodeRigidVertices(one,bad,28);},"Unaligned rigid input accepted");}
    for(size_t at=48;at<60;at+=4){auto bad=decl;put(bad,at,1);rejects([&]{decodeRigidVertices(one,bad,28);},"Invalid rigid terminator accepted");}
    // The selected VS ignores a known tangent and UV1 even when payload is NaN.
    auto extra=decl;extra.resize(84);std::copy(decl.begin()+48,decl.end(),extra.begin()+72);
    put(extra,48,28);put(extra,52,0x002C23A5);put(extra,56,0x00050100);
    put(extra,60,36);put(extra,64,0x002A2187);put(extra,68,0x00060000);
    auto padded=one;padded.resize(40,255);auto extended=decodeRigidVertices(padded,extra,40);
    need(extended.size()==1&&extended[0].normal==vertices[0].normal&&extended[0].uv==vertices[0].uv,"Dead rigid inputs affected decoded attributes");
    need(extended[0].uv1==std::array<float,2>{},"Unconsumed UV1 was not zero initialized");
    need(extended[0].tangent==std::array<float,3>{},"Unconsumed tangent was not zero initialized");
    rejects([&]{decodeRigidVertices(one,decl,28,true);},"Missing consumed UV1 accepted");
    rejects([&]{decodeRigidVertices(padded,extra,40,true);},"Nonfinite consumed UV1 accepted");
    auto dualDecl=decl;dualDecl.resize(72);std::copy(decl.begin()+48,decl.end(),dualDecl.begin()+60);
    put(dualDecl,48,28);put(dualDecl,52,0x002C23A5);put(dualDecl,56,0x00050100);
    auto dualSource=one;dualSource.resize(36);put(dualSource,28,0x80000000);put(dualSource,32,0x3F000001);
    const auto savedDual=dualSource;const auto dual=decodeRigidVertices(dualSource,dualDecl,36,true);
    need(dualSource==savedDual&&dual.size()==1&&dual[0].uv==vertices[0].uv,
         "Dual UV decode changed source or UV0");
    for(size_t lane=0;lane<2;++lane)need(std::bit_cast<uint32_t>(dual[0].uv1[lane])==word(dualSource,28+4*lane),
         "Dual UV1 bits changed");
    for(size_t at:{size_t(28),size_t(32)})for(uint32_t invalid:{0x7F800000u,0xFF800000u,0x7FC12345u}) {
        auto bad=dualSource;put(bad,at,invalid);
        rejects([&]{decodeRigidVertices(bad,dualDecl,36,true);},"Nonfinite dual UV1 accepted");
        need(decodeRigidVertices(bad,dualDecl,36)[0].uv1==std::array<float,2>{},"Dead UV1 affected decode");
    }
    // Tangent is opt-in packed 002A2187 XYZ, same as normals. High two bits
    // are dead; handedness is not inferred. Uses usage6/index0 semantic.
    rejects([&]{decodeRigidVertices(one,decl,28,false,true);},"Missing consumed tangent accepted");
    rejects([&]{decodeRigidVertices(dualSource,dualDecl,36,false,true);},"Missing tangent with UV1 accepted");
    need(decodeRigidVertices(padded,extra,40)[0].tangent==std::array<float,3>{},"Dead tangent affected decode");
    {
        // Stride40 layout mirrors the observed extra declaration: UV1 at28,
        // tangent at36. Verify opt-in decode preserves source and other attrs.
        auto tangentSource=padded;const auto savedTangent=tangentSource;
        const auto withTangent=decodeRigidVertices(tangentSource,extra,40,false,true);
        need(tangentSource==savedTangent&&withTangent.size()==1,"Tangent decode changed source or extent");
        need(withTangent[0].position==vertices[0].position&&withTangent[0].normal==vertices[0].normal&&
             withTangent[0].color==vertices[0].color&&withTangent[0].uv==vertices[0].uv,
             "Tangent decode changed other attributes");
        need(withTangent[0].uv1==std::array<float,2>{},"Unconsumed UV1 changed during tangent decode");
        // 0xFFFFFFFF payload gives per-lane raw1023; high bits must not leak.
        for(float t:withTangent[0].tangent)need(t==float(double(-1)/511.0),"All-ones tangent lane differs");
        // Zero packed tangent decodes to zero, not a fallback direction.
        for(uint32_t high:{0u,0x40000000u,0x80000000u,0xC0000000u}) {
            auto zero=tangentSource;put(zero,36,high);const auto savedZero=zero;
            const auto decoded=decodeRigidVertices(zero,extra,40,false,true);
            need(zero==savedZero&&decoded.size()==1,"Zero-tangent decoding changed storage");
            for(float t:decoded[0].tangent)need(std::bit_cast<uint32_t>(t)==0,"Original zero tangent was replaced");
            need(decoded[0].position==vertices[0].position,"Zero-tangent decode changed position");
        }
        // Exhaustive per-lane 10-bit encodings with high-bit variations.
        for(size_t lane=0;lane<3;++lane)for(uint32_t raw=0;raw<1024;++raw) {
            for(uint32_t high:{0u,0x40000000u,0x80000000u,0xC0000000u}) {
                auto sample=tangentSource;
                uint32_t packed=0;
                for(size_t other=0;other<3;++other) {
                    uint32_t value=(other==lane)?raw:0;
                    packed|=value<<(10*other);
                }
                packed|=high;
                put(sample,36,packed);
                const auto decoded=decodeRigidVertices(sample,extra,40,false,true);
                const double expected=raw<512?double(raw)/511.0:-std::min(1.0,double(1024-raw)/511.0);
                need(decoded[0].tangent[lane]==float(expected),"Tangent packed decoding differs");
                for(size_t other=0;other<3;++other)if(other!=lane)
                    need(decoded[0].tangent[other]==0.0f,"Tangent lane crosstalk");
            }
        }
        // UV1 and tangent together remain independent.
        auto both=tangentSource;put(both,28,0x3F800000);put(both,32,0x40000000);
        put(both,36,0x00000000);
        const auto decodedBoth=decodeRigidVertices(both,extra,40,true,true);
        need(decodedBoth[0].uv1==std::array<float,2>{1.0f,2.0f},"UV1+tangent UV1 bits changed");
        for(float t:decodedBoth[0].tangent)need(t==0.0f,"UV1+tangent tangent changed");
    }
    {
        // Mesh particles fetch position, packed color and UV only. No normal
        // may be read from the position bytes when that element is absent.
        Bytes particleDecl(48);const uint32_t rows[][3]={{0,0x002A23B9,0},{12,0x00182886,0x000A0000},
            {16,0x002C23A5,0x00050000},{0x00FF0000,UINT32_MAX,0}};
        for(size_t i=0;i<4;++i)for(size_t j=0;j<3;++j)put(particleDecl,12*i+4*j,rows[i][j]);
        Bytes data(24);std::copy_n(one.begin(),12,data.begin());std::copy(one.begin()+16,one.end(),data.begin()+12);
        const auto decoded=decodeRigidVertices(data,particleDecl,24,false,false,false);
        need(decoded.size()==1&&decoded[0].position==vertices[0].position&&decoded[0].color==vertices[0].color&&
             decoded[0].uv==vertices[0].uv&&decoded[0].normal==std::array<float,3>{},"Particle input decoding differs");
        rejects([&]{decodeRigidVertices(data,particleDecl,24);},"Lit mesh accepted a missing normal");
        auto missingUv=particleDecl;put(missingUv,24+8,0x00050001);
        rejects([&]{decodeRigidVertices(data,missingUv,24,false,false,false);},"Particle accepted missing UV0");
    }
    if(argc==2) {
        const std::filesystem::path dir=argv[1];auto geometry=read(dir/"scene-geometry.bin");
        auto elements=read(dir/"scene-elements.bin"),data=read(dir/"scene-vertices.bin");
        const auto stride=word(geometry,4);const auto captured=decodeRigidVertices(data,elements,stride);
        need(captured.size()==2613&&stride==28,"LIVE017 rigid geometry extent differs");
        for(size_t i=0;i<captured.size();++i) {
            for(size_t lane=0;lane<3;++lane)need(std::bit_cast<uint32_t>(captured[i].position[lane])==word(data,i*stride+4*lane),"Live rigid position bits changed");
            for(float n:captured[i].normal)need(std::isfinite(n)&&n>=-1&&n<=1,"Live rigid normal outside normalized range");
        }
        std::printf("Verified LIVE017's %zu rigid vertices. ",captured.size());
    } else need(argc==1,"Supply optional capture directory");
    std::printf("PASS rigid vertex decoder: %zu checks, all 1024 normal and 256 color encodings per lane\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL rigid vertex decoder after %zu checks: %s\n",checks,e.what());return 1;}
