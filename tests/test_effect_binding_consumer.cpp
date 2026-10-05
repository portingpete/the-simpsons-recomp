// Run the genuine AOT CPU constant writer to qualify unused binding lanes.
// These are original CPU caches, not GPU uploads or rendered-frame evidence.
#include "runtime/engine_cpu_calls.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
using namespace Simpsons;
size_t checks{};
void need(bool ok,const char* why) {++checks;if(!ok) throw Failure(why);}
constexpr uint32_t lane55=0x82D6C0D0,laneAA=0x82D6C450,extent=0x380;
constexpr uint32_t record=0x30000,source=0x30100;
std::vector<uint8_t> bytes(Runtime& rt,uint32_t at,uint32_t n) {
    const auto* p=rt.pointer(at,n,false);return {p,p+n};
}
void run(Runtime& rt,EngineCpuCalls& cpu,uint32_t usage,uint32_t poison,bool onlyAA) {
    auto* base=rt.base;
    std::array<uint8_t,extent> initial55{},initialAA{};
    for(uint32_t i=0;i<extent;++i) {initial55[i]=uint8_t(i*11+37);initialAA[i]=uint8_t(i*7+23);}
    std::memcpy(rt.pointer(lane55,extent,true),initial55.data(),extent);
    std::memcpy(rt.pointer(laneAA,extent,true),initialAA.data(),extent);
    constexpr uint32_t start55=3,startAA=7;
    // Exercise both the vector store and original memcpy paths.
    const uint32_t count55=(usage&1)?1:2,countAA=(usage&2)?2:1;
    PPC_STORE_U32(record,0xDEADBEEF);PPC_STORE_U32(record+4,usage);
    PPC_STORE_U32(record+8,(!onlyAA && (usage&0x55))?start55:poison);
    PPC_STORE_U32(record+12,(usage&0xAA)?startAA:poison);
    PPC_STORE_U32(record+16,(!onlyAA && (usage&0x55))?count55:poison);
    PPC_STORE_U32(record+20,(usage&0xAA)?countAA:poison);
    const auto before=bytes(rt,record,24),sourceBytes=bytes(rt,source,32);
    const auto saved=cpu.registers();
    const auto* oldContext=currentContext;
    // A null source is intentional only for the original no-used-lanes path.
    cpu.invoke(onlyAA?0x8270A438:0x8270A370,record,(usage&(onlyAA?0xAA:0xFF))?source:0);
    need(currentContext==oldContext,"Original constant writer changed current context");
    const auto& after=cpu.registers();
    need(after.r1.u64==saved.r1.u64 && after.lr==saved.lr && after.r30.u64==saved.r30.u64 &&
         after.r31.u64==saved.r31.u64,"Original constant writer changed nonvolatile ABI");
    auto expected55=initial55,expectedAA=initialAA;
    if(!onlyAA && (usage&0x55)) std::memcpy(expected55.data()+16*start55,sourceBytes.data(),16*count55);
    if(usage&0xAA) std::memcpy(expectedAA.data()+16*startAA,sourceBytes.data(),16*countAA);
    need(!std::memcmp(rt.pointer(lane55,extent,false),expected55.data(),extent),"Original55 cache used an inactive range or wrote wrong bytes");
    need(!std::memcmp(rt.pointer(laneAA,extent,false),expectedAA.data(),extent),"OriginalAA cache used an inactive range or wrote wrong bytes");
    need(bytes(rt,record,24)==before && bytes(rt,source,32)==sourceBytes,"Original constant writer changed inputs");
}
}

int main(int argc,char** argv) {
    try {
        need(argc==2,"Original image required");Runtime rt;rt.load(argv[1]);
        rt.map(0x10000,0x10000,true,"binding consumer test stack");
        rt.map(record,0x1000,true,"binding consumer test data");
        PPCContext entry{};entry.r1.u64=0x20000;entry.lr=0x12345678;
        currentContext=&entry;
        for(uint32_t i=0;i<32;++i) rt.pointer(source,32,true)[i]=uint8_t(i*17+3);
        {
            EngineCpuCalls cpu(entry,rt.base);
            // The array covers every usage-bit combination and adversarial
            // values that would escape a cache if an inactive lane were read.
            for(bool onlyAA:{false,true})
                for(uint32_t poison:{0u,1u,2u,0x7FFFFFFFu,0xFFFFFFFFu})
                    for(uint32_t usage=0;usage<256;++usage) run(rt,cpu,usage,poison,onlyAA);
        }
        currentContext=nullptr;
        std::printf("PASS original FX binding consumers: %zu checks,2560 genuine AOT calls; inactive ranges ignored, active vector/memcpy bytes exact; no SDK execution, GPU upload or draw\n",checks);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL original FX binding consumer: %s\n",e.what());return 1;}
}
