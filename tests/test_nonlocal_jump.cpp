#include "runtime/runtime.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <thread>
using namespace Simpsons;
namespace {
size_t checks{},restores{};
void need(bool v,const char* why){++checks;if(!v)throw Failure(why);}
template<class F> void rejects(F&& f,const char* why){try{f();}catch(const Failure&){++checks;return;}need(false,why);}
auto gprs(PPCContext& c){return std::array{&c.r13,&c.r14,&c.r15,&c.r16,&c.r17,&c.r18,&c.r19,&c.r20,&c.r21,&c.r22,&c.r23,&c.r24,&c.r25,&c.r26,&c.r27,&c.r28,&c.r29,&c.r30,&c.r31};}
auto fprs(PPCContext& c){return std::array{&c.f14,&c.f15,&c.f16,&c.f17,&c.f18,&c.f19,&c.f20,&c.f21,&c.f22,&c.f23,&c.f24,&c.f25,&c.f26,&c.f27,&c.f28,&c.f29,&c.f30,&c.f31};}
constexpr uint32_t buffer=0x30010,other=0x30210,continuation=0x82BC04C4;
struct Unwind {int& count;~Unwind(){++count;}};
void originalJump(PPCContext& c,uint8_t* base,uint32_t address,uint64_t value) {
    c.r3.u64=address;c.r4.u64=value;c.lr=0x82BBD7EC;
    PPCSafeIndirect(c,base,0x82A43D30);
    need(false,"Original longjmp returned to its invoking caller");
}
}
int main(int argc,char** argv) {
    try {
        need(argc==2,"Original image required");Runtime rt;rt.load(argv[1]);
        rt.map(0x10000,0x10000,true,"nonlocal CPU test stack");rt.map(0x30000,0x1000,true,"nonlocal CPU test buffers");
        auto* base=rt.base;PPCContext c{};c.r1.u64=0x1F000;currentContext=&c;
        for(uint32_t i=0;i<19;++i)gprs(c)[i]->u64=0x1122334455660000ull+i*0x137;
        for(uint32_t i=0;i<18;++i)fprs(c)[i]->u64=0x3FF0000000000000ull+i*0x1123456789ull;
        const auto initial=c;
        for(uint32_t poison:{0u,0xCDu,0xFFu}) {
            c=initial;std::memset(rt.pointer(buffer-16,0x170,true),int(poison),0x170);
            PPCNonlocalFrame frame(c,base);c.r3.u64=buffer;c.lr=continuation;frame.save(continuation);
            need(!c.r3.u64 && c.r1.u64==initial.r1.u64,"Initial original save result/SP differ");
            for(uint32_t i=0;i<19;++i)need(PPC_LOAD_U64(buffer+0x98+i*8)==gprs(c)[i]->u64,"Original saved GPR image differs");
            for(uint32_t i=0;i<18;++i)need(PPC_LOAD_U64(buffer+i*8)==fprs(c)[i]->u64,"Original saved FPR image differs");
            need(PPC_LOAD_U64(buffer+0x90)==c.r1.u64 && PPC_LOAD_U32(buffer+0x134)==continuation && !PPC_LOAD_U32(buffer+0x138),"Original jump buffer tail differs");
            for(uint32_t i=0;i<16;++i)need(*rt.pointer(buffer-16+i,1,false)==poison && *rt.pointer(buffer+0x13C+i,1,false)==poison,"Original save exceeded its buffer");
            auto saved=c;
            for(uint64_t value:{0ull,1ull,0xFFFFFFFF80000000ull,0xFFFFFFFFFFFFFFFFull}) {
                for(auto* r:gprs(c))r->u64^=0xFEEDBAADFACE1234ull;
                for(auto* f:fprs(c))f->u64=0x400921FB54442D18ull;
                c.v20.u64[0]=0x123456789ABCDEF0ull;c.v20.u64[1]=0x0FEDCBA987654321ull;
                c.ctr.u64=0xABCDEF00;c.xer={1,1,1};c.vscr_sat=1;
                c.r1.u64=saved.r1.u64-0x400;
                int unwound=0;
                try {Unwind native{unwound};originalJump(c,base,buffer,value);}
                catch(const PPCNonlocalTransfer& t){need(frame.resume(t)==continuation,"Wrong compiled resume target");++restores;}
                need(unwound==1 && c.r3.u64==(value?value:1),"Native destructor or original zero/value normalization differs");
                need(c.r1.u64==saved.r1.u64 && c.lr==continuation,"Original stack/LR restore differs");
                for(uint32_t i=0;i<19;++i)need(gprs(c)[i]->u64==gprs(saved)[i]->u64,"Original GPR restore differs");
                for(uint32_t i=0;i<18;++i)need(fprs(c)[i]->u64==fprs(saved)[i]->u64,"Original FPR restore differs");
                need(c.ctr.u64==0xABCDEF00 && c.xer.so && c.xer.ov && c.xer.ca && c.vscr_sat &&
                     c.v20.u64[0]==0x123456789ABCDEF0ull && c.v20.u64[1]==0x0FEDCBA987654321ull,"Non-saved machine state was incorrectly restored");
                // Original lwz reads the high word of the std-saved stack.
                need(PPC_LOAD_U32(0x82CFAE94)==continuation && PPC_LOAD_U32(0x82CFAE9C)==uint32_t(saved.r1.u64>>32) &&
                     !PPC_LOAD_U32(0x82CFAE98),"Original unwind bookkeeping was omitted");
            }
            c.r3.u64=buffer;
            auto* byte=rt.pointer(buffer+0x20,1,true);*byte^=1;
            const auto sp=c.r1.u64;
            rejects([&]{PPCNonlocalFrame::validateJump(c,base);},"Changed original jump buffer accepted");
            need(c.r1.u64==sp,"Rejected jump mutated stack");*byte^=1;
            auto foreign=c;rejects([&]{PPCNonlocalFrame::validateJump(foreign,base);},"Foreign CPU context accepted");
            bool threadRejected=false;std::thread thread([&]{try{PPCNonlocalFrame::validateJump(c,base);}catch(const Failure&){threadRejected=true;}});thread.join();
            need(threadRejected,"Foreign thread accepted the original buffer");
            int nestedUnwound=0;
            try {
                PPCNonlocalFrame nested(c,base);Unwind native{nestedUnwound};
                c.r1.u64=sp-0x200;c.r3.u64=other;c.lr=continuation;nested.save(continuation);
                try {originalJump(c,base,buffer,7);}catch(const PPCNonlocalTransfer& t){nested.resume(t);need(false,"Wrong frame swallowed transfer");}
            } catch(const PPCNonlocalTransfer& t){need(frame.resume(t)==continuation,"Outer resume failed");++restores;}
            need(nestedUnwound==1 && c.r3.u64==7 && c.r1.u64==sp,"Nested native scope was not unwound");
            c.r3.u64=other;rejects([&]{PPCNonlocalFrame::validateJump(c,base);},"Expired nested buffer remained live");
            // A later save in the same live frame replaces the earlier capture.
            c.r3.u64=buffer;c.lr=continuation;c.r14.u64=0xFEEDBEEF;frame.save(continuation);
            c.r14.u64=0;try{originalJump(c,base,buffer,9);}catch(const PPCNonlocalTransfer& t){frame.resume(t);++restores;}
            need(c.r14.u64==0xFEEDBEEF,"Repeated save did not replace prior register state");
        }
        c.r3.u64=buffer;rejects([&]{PPCNonlocalFrame::validateJump(c,base);},"Expired outer frame remained live");
        {
            PPCNonlocalFrame frame(c,base);c.lr=continuation;
            c.r3.u64=buffer+1;rejects([&]{frame.save(continuation);},"Misaligned jump buffer accepted");
            c.r3.u64=buffer;
            PPC_STORE_U32(0x82E3E26C,1);rejects([&]{frame.save(continuation);},"Extended CRT save silently accepted");PPC_STORE_U32(0x82E3E26C,0);
            c.lr=continuation+4;rejects([&]{frame.save(continuation);},"Mismatched continuation accepted");
        }
        need(restores==18,"Unexpected nonlocal restore coverage");
        std::printf("PASS original nonlocal control flow:%zu checks,%zu original restores; repeated/nested saves, native destructors, stale/foreign/changed buffers; CPU-only,ALL MUTED\n",checks,restores);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL nonlocal control flow:%zu checks %s\n",checks,e.what());return 1;}
    catch(...){std::fprintf(stderr,"FAIL unhandled nonlocal transfer\n");return 1;}
}
