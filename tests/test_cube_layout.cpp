// Original CPU texture header/layout helpers only. No SDK device, GPU resource,
// lock synchronization or shipped implementation. Includes the small cube's
// partial clear coverage, using the retained reference address equations.
#include "runtime/engine_cpu_calls.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <set>
using namespace Simpsons;
namespace {
size_t checks=0,calls=0;
void need(bool ok,const char* why) {++checks;if(!ok) throw Failure(why);}
uint32_t tiled(uint32_t x,uint32_t y,uint32_t pitch) {
    const uint32_t macro=((x>>5)+(y>>5)*(pitch>>5))<<9;
    const uint32_t micro=((x&7)+((y&14)<<2))<<2;
    const uint32_t mixed=macro+((micro&~15u)<<1)+(micro&15)+((y&1)<<4);
    return ((mixed&~511u)<<3)+((y&16)<<7)+((mixed&448)<<2)+(((((y&8)>>2)+(x>>3))&3)<<6)+(mixed&63);
}
uint32_t rowColumn(uint32_t x,uint32_t y,uint32_t pitch) {
    const uint32_t rowMacro=((y/32)*(pitch/32))<<9,rowMicro=(y&6)<<4;
    const uint32_t row=rowMacro+((rowMicro&~15u)<<1)+(rowMicro&15)+((y&8)<<5)+((y&1)<<4);
    const uint32_t columnMacro=(x/32)<<9,columnMicro=(x&7)<<2;
    const uint32_t at=row+columnMacro+((columnMicro&~15u)<<1)+(columnMicro&15);
    return ((at&~511u)<<3)+((at&448)<<2)+(at&63)+((y&16)<<7)+(((((y&8)>>2)+(x>>3))&3)<<6);
}
void clearCoverage(uint32_t n) {
    const uint32_t pitch=(n+31)&~31u,clearBytes=n*n*4,faceBytes=pitch*((n+31)&~31u)*4;
    std::set<uint32_t> offsets;uint32_t initialized=0;
    for(uint32_t y=0;y<n;++y) for(uint32_t x=0;x<n;++x) {
        const auto at=tiled(x,y,pitch);
        need(at==rowColumn(x,y,pitch),"Reference tiled-address formulations disagree");
        need(!(at&3) && at+4<=faceBytes && offsets.insert(at).second,"Tiled texel address exceeds/aliases original face");
        const bool cleared=at+4<=clearBytes;initialized+=cleared;
        need(cleared==(n==256 || y<8),"Original contiguous clear logical coverage differs");
    }
    need(initialized==(n==16?128u:65536u),"Wrong initialized texel count");
    std::printf("Cube N=%u: original memset=%u bytes; initialized=%u/%u texels per face; remaining texels retain unspecified prior backing\n",n,clearBytes,initialized,n*n);
}
}
int main(int argc,char** argv) {
    try {
        need(argc==2,"Original image required");
        Runtime rt;rt.load(argv[1]);rt.map(0x10000,0x10000,true,"cube layout probe stack");
        rt.map(0x30000,0x10000,true,"cube layout probe CPU data");
        PPCContext entry{};entry.r1.u64=0x20000;entry.lr=0x12345678;currentContext=&entry;
        auto* base=rt.base;EngineCpuCalls cpu(entry,base);
        constexpr uint32_t header=0x30010,output=0x30100;
        for(uint32_t cubeSize:{16u,256u}) for(uint32_t profile=0;profile<3;++profile) for(uint32_t poison:{0u,0xA5u,0xFFu,0x5Au}) {
            const bool cube=profile==0;const uint32_t n=profile==2?cubeSize/2:cubeSize,faces=cube?6:1;
            const uint32_t pitch=((n+31)&~31u)*4,faceBytes=pitch*((n+31)&~31u);
            std::memset(rt.pointer(header-16,0x54,true),0x79,0x54);
            std::memset(rt.pointer(header,0x34,true),int(poison),0x34);
            std::memset(rt.pointer(output,0x80,true),0xCC,0x80);
            auto& c=cpu.registers();const auto sp=c.r1.u32;
            c.r3.u64=cube?0x12:3;c.r4.u64=n;c.r5.u64=n;c.r6.u64=faces;c.r7.u64=1;c.r8.u64=0;c.r9.u64=0x282801B6;c.r10.u64=2;
            PPC_STORE_U32(sp+0x54,0);PPC_STORE_U32(sp+0x5C,0);PPC_STORE_U32(sp+0x64,0);
            PPC_STORE_U32(sp+0x6C,header);PPC_STORE_U32(sp+0x74,output);PPC_STORE_U32(sp+0x7C,output+4);
            cpu.invoke(0x8243F928);++calls;
            need(c.r1.u32==sp && c.lr==entry.lr,"Original header builder changed stack/LR");
            need(PPC_LOAD_U32(output)==faces*faceBytes && !PPC_LOAD_U32(output+4),"Original primary/secondary allocation sizes differ");
            need(PPC_LOAD_U32(header+4)==1 && (PPC_LOAD_U32(header+0x1C)&0x80000000u) &&
                 ((PPC_LOAD_U32(header+0x30)>>9)&3)==(cube?3u:1u),"Original header refcount/tiled/dimension differs");
            for(uint32_t i=0;i<16;++i) need(*rt.pointer(header-16+i,1,false)==0x79 &&
                 *rt.pointer(header+0x34+i,1,false)==0x79,"Original header builder exceeded its52-byte allocation");
            // Complete the same two CPU base-address publications as82440680/88.
            // Layout reads addresses but never accesses this unallocated backing.
            PPC_STORE_U32(header+0x20,(PPC_LOAD_U32(header+0x20)&0xFFF)|0xA1000000);
            PPC_STORE_U32(header+0x30,PPC_LOAD_U32(header+0x30)&0xFFF);
            for(uint32_t face=0;face<faces;++face) {
                std::memset(rt.pointer(output,0x80,true),0xCC,0x80);
                c.r3.u64=header;c.r4.u64=face;c.r5.u64=0;
                c.r6.u64=output;c.r7.u64=output+4;c.r8.u64=output+8;c.r9.u64=output+12;c.r10.u64=output+16;
                PPC_STORE_U32(sp+0x54,output+20);PPC_STORE_U32(sp+0x5C,output+24);PPC_STORE_U32(sp+0x64,output+28);
                cpu.invoke(0x8243F268);++calls;
                need(c.r1.u32==sp && c.lr==entry.lr,"Original layout helper changed stack/LR");
                const std::array<uint32_t,8> expected={pitch,faceBytes,faceBytes,0,n,n,1,0xA1000000+face*faceBytes};
                for(uint32_t i=0;i<8;++i) need(PPC_LOAD_U32(output+4*i)==expected[i],"Original face/pitch/level layout differs");
            }
        }
        for(uint32_t n:{16u,256u}) clearCoverage(n);
        need(calls==88,"Original CPU layout call coverage differs");
        std::printf("PASS original cubemap layout:%zu checks,%zu AOT calls;cube/full2D/half2D profiles;four header poisons;no GPU or SDK device,ALL MUTED\n",checks,calls);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL original cube layout: checks=%zu calls=%zu %s\n",checks,calls,e.what());return 1;}
}
