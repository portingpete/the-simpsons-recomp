// Execute the original CPU image filters, independently of native uploading.
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include <array>
#include <cstdio>
#include <cstring>
using namespace Simpsons;
namespace {
size_t checks{},levels{},calls{};
void need(bool v,const char* why){++checks;if(!v)throw Failure(why);}
struct Observed{};
void observe(uint32_t pc,PPCContext&,uint8_t* base){need(pc==0x823458C0&&active&&active->engineDriver&&active->engineDriver->started()&&!PPC_LOAD_U32(0x82D08BFC),"Unexpected startup boundary");throw Observed{};}
void descriptor(uint8_t* base,uint32_t at,uint32_t data,uint32_t n,uint32_t pitch,uint32_t format,uint32_t poison){
    for(uint32_t i=0;i<21;++i)PPC_STORE_U32(at+4*i,0);
    PPC_STORE_U32(at,data);PPC_STORE_U32(at+4,format);PPC_STORE_U32(at+8,pitch);
    for(uint32_t offset:{0x10u,0x28u}){PPC_STORE_U32(at+offset+8,n);PPC_STORE_U32(at+offset+12,n);PPC_STORE_U32(at+offset+20,1);}
    for(uint32_t offset:{0x40u,0x44u,0x48u})PPC_STORE_U32(at+offset,poison);
}
struct Input{uint32_t source,bytes,n,format,pixel,mips;};
constexpr std::array inputs={Input{0x82CED9A8,0x102C,32,0x18280086,0xFFFFFFFF,6},Input{0x82CEECF0,0x102C,32,0x18280086,0xFF000000,6},Input{0x82CEE9D8,0x312,16,0x28280086,0xFF808080,5}};
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(exceptionFilter);
    try{
        need(argc==2,"Original image required");Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        rt.graphicsStartupObserver=observe;bool stopped=false;try{runOriginal(startup,rt.base);}catch(const Observed&){stopped=true;}
        rt.graphicsStartupObserver={};need(stopped,"Boundary missing");auto* base=rt.base;rt.map(0x50000,0x10000,true,"original filter fixture");
        constexpr uint32_t image=0x50010,info=0x50100,filter=0x50200,src=0x50300,dst=0x50400,header=0x50510,out=0x50600;
        for(const auto& input:inputs)for(uint32_t poison:{0u,0xCDCDCDCDu,0xFFFFFFFFu,0x5A5A5A5Au})for(uint32_t padding:{0u,256u}){
            EngineCpuCalls cpu(entry,base);auto& c=cpu.registers();const auto sp=c.r1.u64,lr=c.lr;
            std::vector<uint8_t> embedded(rt.pointer(input.source,input.bytes,false),rt.pointer(input.source,input.bytes,false)+input.bytes);
            // Original CPU header builder establishes format/endian/swizzle;
            // its backing addresses are never dereferenced or shipped.
            std::memset(rt.pointer(header-16,0x54,true),0x79,0x54);std::memset(rt.pointer(header,0x34,true),int(poison&255),0x34);
            c.r3.u64=3;c.r4.u64=input.n;c.r5.u64=input.n;c.r6.u64=1;c.r7.u64=input.mips;c.r8.u64=0;c.r9.u64=input.format;c.r10.u64=1;
            PPC_STORE_U32(c.r1.u32+0x54,0);PPC_STORE_U32(c.r1.u32+0x5C,0);PPC_STORE_U32(c.r1.u32+0x64,0);
            PPC_STORE_U32(c.r1.u32+0x6C,header);PPC_STORE_U32(c.r1.u32+0x74,out);PPC_STORE_U32(c.r1.u32+0x7C,out+4);
            cpu.invoke(0x8243F928);++calls;
            const uint32_t fetch1=PPC_LOAD_U32(header+0x20),fetch3=PPC_LOAD_U32(header+0x28);
            need((fetch1&63)==6 && ((fetch1>>6)&3)==2 && ((fetch3>>1)&0xFFF)==(input.format==0x18280086?0x60Au:0xA0Au),"Original 8:8:8:8 endian or ZYXW/ZYX1 swizzle differs");
            need(!(PPC_LOAD_U32(header+0x1C)&0x80000000u),"Built-in original texture is tiled");
            for(uint32_t i=0;i<16;++i)need(*rt.pointer(header-16+i,1,false)==0x79 && *rt.pointer(header+0x34+i,1,false)==0x79,"Header bounds changed");
            cpu.invoke(0x82BBD720,image);++calls;need(!cpu.invoke(0x82BC2FF8,image,input.source,input.bytes,info,1),"Decode failed");++calls;
            auto data=PPC_LOAD_U32(image+4),sourceSize=input.n,sourcePitch=input.n*4;uint32_t destination=0x51010;
            for(uint32_t n=input.n,level=0;n;n>>=1,++level){
                const uint32_t pitch=n*4+padding,bytes=pitch*n,sourceBytes=sourcePitch*sourceSize;
                std::vector<uint8_t> source(rt.pointer(data,sourceBytes,false),rt.pointer(data,sourceBytes,false)+sourceBytes);
                std::memset(rt.pointer(destination-16,bytes+32,true),0xCD,bytes+32);
                std::memset(rt.pointer(filter-16,44,true),0xA7,44);
                std::memset(rt.pointer(src-16,0x74,true),0xA7,0x74);std::memset(rt.pointer(dst-16,0x74,true),0xA7,0x74);
                descriptor(base,src,data,sourceSize,sourcePitch,input.format,poison);descriptor(base,dst,destination,n,pitch,input.format,poison);
                cpu.invoke(0x82BC52E0,filter);++calls;need(!cpu.invoke(0x82BC8EB8,filter,dst,src,level?5:0x80004),"CPU filter failed");++calls;++levels;
                const uint32_t expected=input.format==0x28280086&&level?0x00808080:input.pixel;
                for(uint32_t y=0;y<n;++y){
                    for(uint32_t x=0;x<n;++x)need(PPC_LOAD_U32(destination+y*pitch+4*x)==expected,"Original filtered pixel differs");
                    for(uint32_t i=n*4;i<pitch;++i)need(*rt.pointer(destination+y*pitch+i,1,false)==0xCD,"Filter wrote row padding");
                }
                need(!std::memcmp(source.data(),rt.pointer(data,sourceBytes,false),sourceBytes),"Original filter changed source pixels/padding");
                need(!PPC_LOAD_U32(filter)&&!PPC_LOAD_U32(filter+4)&&PPC_LOAD_U32(filter+8)==(level?5u:0x80004u),"Filter retained CPU helper ownership");
                for(uint32_t i=0;i<16;++i)need(*rt.pointer(destination-16+i,1,false)==0xCD && *rt.pointer(destination+bytes+i,1,false)==0xCD &&
                    *rt.pointer(filter-16+i,1,false)==0xA7 && *rt.pointer(filter+12+i,1,false)==0xA7 &&
                    *rt.pointer(src-16+i,1,false)==0xA7 && *rt.pointer(src+0x54+i,1,false)==0xA7 &&
                    *rt.pointer(dst-16+i,1,false)==0xA7 && *rt.pointer(dst+0x54+i,1,false)==0xA7,"Filter metadata/pixel canary changed");
                cpu.invoke(0x82BC61D0,filter);++calls;
                need(c.r1.u64==sp && c.lr==lr,"Original filter stack/LR changed");
                data=destination;sourceSize=n;sourcePitch=pitch;destination=destination==0x51010?0x55010:0x51010;
            }
            cpu.invoke(0x82BBE1F8,image);++calls;
            need(!std::memcmp(embedded.data(),rt.pointer(input.source,input.bytes,false),input.bytes),"Embedded image changed");
        }
        need(levels==136,"Filter level coverage differs");
        std::printf("PASS original built-in filtering:%zu checks,%zu AOT calls,%zu levels; original swizzles,all texels,four source-flag poisons,tight/padded pitch,CPU cleanup;ALL MUTED\n",checks,calls,levels);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL original filter:%zu checks %s\n",checks,e.what());return 1;}
}
