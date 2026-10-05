// Original mesh declaration cache, CPU SDK construction, reference decrement,
// and real allocator retirement. No substitute guest resource headers.
#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include "runtime/engine_audio.h"
#include "runtime/engine_quad_declarations.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace {
using namespace Simpsons;
size_t checks{};
void need(bool value,const char* message){++checks;if(!value)throw Failure(message);}
template<class F>void rejects(F action){try{action();}catch(const Failure&){++checks;return;}throw Failure("Invalid mesh declaration lifetime accepted");}
struct Observed{};
std::array<uint64_t,20> abi(const PPCContext& c){return {c.r1.u64,c.lr,c.r14.u64,c.r15.u64,c.r16.u64,c.r17.u64,
    c.r18.u64,c.r19.u64,c.r20.u64,c.r21.u64,c.r22.u64,c.r23.u64,c.r24.u64,c.r25.u64,c.r26.u64,
    c.r27.u64,c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64};}
void exercise(const char* image){
    Runtime rt;rt.load(image);PPCContext startup{};rt.initialize(startup);const auto entry=startup;auto* base=rt.base;
    rt.audioBoundaryObserver=[](uint32_t pc,PPCContext&,uint8_t*){if(pc==0x828166FC)throw Observed{};};
    bool observed=false;try{runOriginal(startup,base);}catch(const Observed&){observed=true;}rt.audioBoundaryObserver={};
    need(observed&&rt.engineDriver&&rt.engineAudio,"Original startup checkpoint missing");
    EngineCpuCalls cpu(entry,base);auto& heap=*rt.engineAudio;
    constexpr uint32_t input=0x50000,cache=0x82D63050,vector=0x82D6305C,copies=0x82D63044;
    // Position and color, then original terminator. Color causes82701BD8 to
    // create the same secondary stream-one declaration seen in the live crash.
    constexpr std::array<uint32_t,9> elements={0,0x002A23B9,0,0x0000000C,0x00182886,0x000A0000,
        0x00FF0000,0xFFFFFFFF,0};
    rt.map(input,0x1000,true,"mesh declaration source fixture");
    for(size_t i=0;i<elements.size();++i)PPC_STORE_U32(input+uint32_t(4*i),elements[i]);
    std::array<uint64_t,2> previousGenerations{};
    uint32_t cacheStorage{},cacheCapacity{};uint64_t cacheGeneration{};
    for(unsigned cycle=0;cycle<2;++cycle){
        need(!PPC_LOAD_U32(cache+4)&&!PPC_LOAD_U32(vector+4)&&!PPC_LOAD_U32(copies+4),"Previous original declaration owners remain");
        const auto nativeCount=rt.engineDriver->quadDeclarations().count();
        const auto before=abi(cpu.registers());
        const auto row=cpu.invoke(0x82701BD8,input,3);
        need(abi(cpu.registers())==before&&row==PPC_LOAD_U32(cache)&&PPC_LOAD_U32(cache+4)==1,
             "Original declaration cache construction/ABI differs");
        const auto rowGeneration=heap.allocationGeneration(row,12);
        if(cycle)need(row==cacheStorage&&PPC_LOAD_U32(cache+8)==cacheCapacity&&rowGeneration==cacheGeneration,
                      "Original cache reconstruction replaced its retained allocation");
        else{cacheStorage=row;cacheCapacity=PPC_LOAD_U32(cache+8);cacheGeneration=rowGeneration;}
        need(PPC_LOAD_U32(vector+4)==2&&PPC_LOAD_U32(copies+4)==1,"Original primary/alternate declaration ownership differs");
        const std::array<uint32_t,2> declarations={PPC_LOAD_U32(row+4),PPC_LOAD_U32(row+8)};
        const auto storage=PPC_LOAD_U32(vector),copyStorage=PPC_LOAD_U32(copies),copiedElements=PPC_LOAD_U32(copyStorage);
        need(declarations[0]&&declarations[1]&&declarations[0]!=declarations[1],"Original declarations alias or are absent");
        std::array<uint64_t,2> generations{};
        for(unsigned i=0;i<2;++i){
            const auto declaration=declarations[i];
            need(PPC_LOAD_U32(storage+4*i)==declaration&&PPC_LOAD_U32(declaration)==0x00100005&&
                 PPC_LOAD_U32(declaration+4)==1&&PPC_LOAD_U32(declaration+0x18)==2,
                 "Original CPU declaration header/publication differs");
            need(!PPC_LOAD_U32(declaration+8)&&!PPC_LOAD_U32(declaration+0xC),"CPU declaration unexpectedly acquired device state");
            need(PPC_LOAD_U16(declaration+0x34+12)==i&&PPC_LOAD_U16(declaration+0x34+14)==(i?0:12),
                 "Original alternate color stream/offset differs");
            generations[i]=heap.allocationGeneration(declaration,0x50);
            need(generations[i]!=previousGenerations[i],"Reconstruction revived an old allocation generation");
        }
        need(cpu.invoke(0x82701BD8,input,3)==row&&PPC_LOAD_U32(vector+4)==2&&PPC_LOAD_U32(copies+4)==1,
             "Original cache hit duplicated declaration owners");
        need(rt.engineDriver->quadDeclarations().count()==nativeCount,"CPU declarations became native resource identities");
        // Probe only the admission boundary; original AOT below performs all
        // decrements/frees. Rejected members must leave both references intact.
        auto probe=cpu.registers();probe.r1.u32-=0x80;PPC_STORE_U32(probe.r1.u32,probe.r1.u32+0x80);
        probe.lr=0x82700ABC;probe.r3.u64=declarations[1];probe.r30.u64=vector;probe.r31.u64=2;probe.r29.u64=8;
        auto* previous=currentContext;currentContext=&probe;
        need(!SimpsonsNativeGraphicsResourceRelease(probe,base),"Qualified CPU declaration did not retain original release");
        const auto rejected=[&]{rejects([&]{SimpsonsNativeGraphicsResourceRelease(probe,base);});
            need(PPC_LOAD_U32(declarations[0]+4)==1&&PPC_LOAD_U32(declarations[1]+4)==1,
                 "Rejected declaration release changed its references");};
        probe.r3.u64=declarations[0];rejected();probe.r3.u64=declarations[1];
        std::memcpy(rt.pointer(input+0x100,0x50,true),rt.pointer(declarations[1],0x50,false),0x50);
        probe.r3.u64=input+0x100;rejected();probe.r3.u64=declarations[1];
        probe.r30.u64=cache;rejected();probe.r30.u64=vector;
        probe.r29.u64=4;rejected();probe.r29.u64=8;
        probe.r31.u64=0;rejected();probe.r31.u64=2;
        PPC_STORE_U32(declarations[1],0x00100003);rejected();PPC_STORE_U32(declarations[1],0x00100005);
        probe.lr=0x82700AC0;rejected();probe.lr=0x82700ABC;
        currentContext=previous;
        const auto cleanupAbi=abi(cpu.registers());cpu.invoke(0x82700A78);
        need(abi(cpu.registers())==cleanupAbi,"Original declaration teardown changed nonvolatile ABI");
        // Original827006E0(cache,0) clears used rows and count, retaining its
        // storage/capacity.82700A78 separately frees the two pointer vectors.
        need(PPC_LOAD_U32(cache)==cacheStorage&&!PPC_LOAD_U32(cache+4)&&PPC_LOAD_U32(cache+8)==cacheCapacity&&
             !PPC_LOAD_U32(row)&&!PPC_LOAD_U32(row+4)&&!PPC_LOAD_U32(row+8)&&
             heap.allocationGeneration(row,12)==cacheGeneration,
             "Original cleanup did not retain an empty reusable declaration cache");
        for(const auto owner:{vector,copies})
            need(!PPC_LOAD_U32(owner)&&!PPC_LOAD_U32(owner+4)&&!PPC_LOAD_U32(owner+8),"Original teardown did not clear its vector");
        for(const auto declaration:declarations)rejects([&]{heap.allocationGeneration(declaration,0x50);});
        rejects([&]{heap.allocationGeneration(copiedElements,36);});
        rejects([&]{heap.allocationGeneration(storage,8);});
        rejects([&]{heap.allocationGeneration(copyStorage,4);});
        need(rt.engineDriver->quadDeclarations().count()==nativeCount,"Original teardown changed native declaration ownership");
        previousGenerations=generations;
    }
}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try{need(argc==2,"Original image required");exercise(argv[1]);std::printf("PASS: %zu original mesh declaration cache/retirement checks\n",checks);return 0;}
    catch(const std::exception& error){std::fprintf(stderr,"FAIL after %zu checks: %s\n",checks,error.what());return 1;}
}
