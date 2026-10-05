// Original decoder and CPU resource requirements. No SDK texture is created.
#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <set>
using namespace Simpsons;
namespace {
size_t checks{},calls{},images{};
void need(bool ok,const char* why) { ++checks;if(!ok)throw Failure(why); }
struct Observed {};
void observe(uint32_t pc,PPCContext&,uint8_t* base) {
    
    need(pc==0x823458C0&&active&&active->engineDriver&&active->engineDriver->started()&&!PPC_LOAD_U32(0x82D08BFC),"Unexpected original startup boundary");
    throw Observed{};
}
struct Input {uint32_t source,bytes,n,format,pixel,mips;};
constexpr std::array inputs={Input{0x82CED9A8,0x102C,32,0x18280086,0xFFFFFFFF,6},
    Input{0x82CEECF0,0x102C,32,0x18280086,0xFF000000,6},Input{0x82CEE9D8,0x312,16,0x28280086,0xFF808080,5}};
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==2,"Original image required");Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);const auto entry=original;
        bool observed=false;rt.graphicsStartupObserver=observe;
        try{runOriginal(original,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original startup boundary absent");
        auto* base=rt.base;rt.map(0x50000,0x1000,true,"original built-in decoder test data");
        constexpr uint32_t image=0x50010,info=0x50100,requirements=0x50140;
        const auto context=rt.engineDriver->refreshContext(entry,base);rt.engineDriver->requireContext(context);
        std::set<uint32_t> allocations;bool reused=false;
        for(uint32_t poison:{0u,0xCDu,0xFFu,0x5Au})for(const auto& input:inputs) {
            EngineCpuCalls cpu(entry,base);const auto sp=cpu.registers().r1.u64;
            std::vector<uint8_t> source(rt.pointer(input.source,input.bytes,false),rt.pointer(input.source,input.bytes,false)+input.bytes);
            std::memset(rt.pointer(image-16,0x200,true),int(poison),0x200);
            cpu.invoke(0x82BBD720,image);++calls;
            for(uint32_t i=0;i<21;++i) {
                const bool initialized=i==0||i==1||i==2||i==14||i==15||i==19||i==20;
                const uint32_t expected=i==0?0xFFFFFFFFu:initialized?0u:poison*0x01010101;
                need(PPC_LOAD_U32(image+4*i)==expected,"Original image constructor initialized a different field set");
            }
            const auto hr=cpu.invoke(0x82BC2FF8,image,input.source,input.bytes,info,1);++calls;++images;
            need(hr==0 && cpu.registers().r1.u64==sp && cpu.registers().lr==entry.lr,"Original decoder result or ABI differs");
            const auto data=PPC_LOAD_U32(image+4);need(data && data!=input.source && data!=input.source+18,"Original decoder did not own converted pixels");
            reused|=!allocations.insert(data).second;
            const std::array<uint32_t,21> expected={input.format,data,0,input.n,input.n,1,0,0,input.n,input.n,0,1,input.n*4,0,1,0,1,3,2,0,0};
            for(uint32_t i=0;i<expected.size();++i)need(PPC_LOAD_U32(image+4*i)==expected[i],"Original decoded CPU image metadata differs");
            const std::array<uint32_t,7> infoExpected={input.n,input.n,1,1,input.format,3,2};
            for(uint32_t i=0;i<infoExpected.size();++i)need(PPC_LOAD_U32(info+4*i)==infoExpected[i],"Original file-info metadata differs");
            for(uint32_t i=0;i<input.n*input.n;++i)need(PPC_LOAD_U32(data+4*i)==input.pixel,"Original decoded texel differs");
            for(uint32_t i=0;i<16;++i)need(*rt.pointer(image-16+i,1,false)==poison && *rt.pointer(image+0x54+i,1,false)==poison &&
                *rt.pointer(info-16+i,1,false)==poison && *rt.pointer(info+0x1C+i,1,false)==poison,"Original decoder exceeded metadata bounds");
            for(uint32_t i=0;i<3;++i)PPC_STORE_U32(requirements+4*i,i==2?1:input.n);
            PPC_STORE_U32(requirements+12,0xFFFFFFFF);PPC_STORE_U32(requirements+16,input.format);
            auto& c=cpu.registers();c.r3.u64=context;c.r4.u64=requirements;c.r5.u64=requirements+4;c.r6.u64=requirements+8;
            c.r7.u64=requirements+12;c.r8.u64=0;c.r9.u64=requirements+16;c.r10.u64=1;PPC_STORE_U32(c.r1.u32+0x54,3);
            const auto normalized=cpu.invoke(0x82B7F950);++calls;
            need(normalized==0 && c.r1.u64==sp && c.lr==entry.lr,"Original CPU requirements helper failed");
            const std::array<uint32_t,5> reqExpected={input.n,input.n,1,input.mips,input.format};
            for(uint32_t i=0;i<reqExpected.size();++i)need(PPC_LOAD_U32(requirements+4*i)==reqExpected[i],"Original mip count, extent or selected format differs");
            cpu.invoke(0x82BBE1F8,image);++calls;
            // Original destructor releases owned data, but retains every field,
            // including its now retired pixel pointer. Never dereference it here.
            for(uint32_t i=0;i<expected.size();++i)need(PPC_LOAD_U32(image+4*i)==expected[i],"Original destructor changed CPU image fields");
            need(!std::memcmp(rt.pointer(input.source,input.bytes,false),source.data(),source.size()),"Original embedded image changed");
        }
        need(reused,"Original image allocator never reused a released pixel allocation");
        for(const auto invalid:std::array<std::array<uint32_t,2>,2>{{{0,0x102C},{0x82CED9A8,0}}}) {
            EngineCpuCalls cpu(entry,base);std::memset(rt.pointer(image,0x54,true),0xA5,0x54);cpu.invoke(0x82BBD720,image);++calls;
            std::array<uint8_t,0x54> before{};std::memcpy(before.data(),rt.pointer(image,0x54,false),before.size());
            need(cpu.invoke(0x82BC2FF8,image,invalid[0],invalid[1],info,1)==0x8876086C,"Original invalid input HRESULT differs");++calls;
            need(!std::memcmp(before.data(),rt.pointer(image,0x54,false),before.size()),"Rejected original decode mutated image object");
            cpu.invoke(0x82BBE1F8,image);++calls;
        }
        need(images==12 && calls==54,"Original image call coverage differs");
        std::printf("PASS original built-in image decoding:%zu checks,%zu AOT calls,%zu decoded images; all texels, metadata, six/six/five mip levels, four poisons and original destruction; no native texture/upload/sampling claim,ALL MUTED\n",checks,calls,images);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL original built-in decoder:%zu checks %s\n",checks,e.what());return 1;}
    catch(...){std::fprintf(stderr,"FAIL unexpected nonlocal transfer\n");return 1;}
}
