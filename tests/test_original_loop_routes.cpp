// Original loop names, registry, borrowed frontend owner and return-key ABI.
// This deliberately does not run a world, fake a tick, dispatch a popup, or
// claim that the GameMainLoop creator's three allocated strings were created.
#include "runtime/engine_cpu_calls.h"
#include <array>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

using namespace Simpsons;
namespace {
size_t checks{};
const char* phase="source";
void need(bool value,const char* why){++checks;if(!value)throw Failure(why);}
template<class F>void rejects(F&& call,const char* why){
    bool failed=false;try{call();}catch(const Failure&){failed=true;}need(failed,why);
}
void word(uint32_t actual,uint32_t expected){need(actual==expected,"Original loop instruction/source word differs");}
template<size_t N>void pin(uint8_t* base,uint32_t pc,const std::array<uint32_t,N>& expected){
    for(size_t i=0;i<N;++i)word(PPC_LOAD_U32(pc+uint32_t(i)*4),expected[i]);
}
struct Abi {
    uint64_t sp,lr;std::array<uint64_t,18> g{},f{};
    explicit Abi(const PPCContext& c):sp(c.r1.u64),lr(c.lr){
        const auto gs=std::array{c.r14,c.r15,c.r16,c.r17,c.r18,c.r19,c.r20,c.r21,c.r22,c.r23,c.r24,c.r25,c.r26,c.r27,c.r28,c.r29,c.r30,c.r31};
        const auto fs=std::array{c.f14,c.f15,c.f16,c.f17,c.f18,c.f19,c.f20,c.f21,c.f22,c.f23,c.f24,c.f25,c.f26,c.f27,c.f28,c.f29,c.f30,c.f31};
        for(size_t i=0;i<18;++i){g[i]=gs[i].u64;f[i]=fs[i].u64;}
    }
    void verify(const PPCContext& c)const{const Abi after(c);need(after.sp==sp&&after.lr==lr&&after.g==g&&after.f==f,"Original loop leaf/helper changed nonvolatile ABI");}
};
constexpr uint32_t area=0x50000,manager=area,initLeaf=area+0x200,gameLeaf=area+0x300,
    frontend=area+0x400,nameBuffer=area+0x500;
constexpr uint32_t gameKey=0xA5A03BAF,frontendKey=0xB92397EF,initKey=0xC956DCCE,debugKey=0xF200280F;
void sources(Runtime& rt){
    auto* base=rt.base;
    pin(base,0x828621C0,std::array{0x3B610090u,0x4BFFE805u,0x7C641B78u,0x386100F0u,0x7F65DB78u,0x4BFFE8CDu});
    pin(base,0x828621DC,std::array{0x807E8D60u,0x4BFFE7E9u,0x7C641B78u,0x386100D0u,0x4BFFD455u,0x3D608200u,0x807E8D60u,0x3BA100D0u,0x388B1F10u,0x4BFFE7C9u,0x7C641B78u,0x386100F0u,0x7FA5EB78u,0x4BFFE891u});
    pin(base,0x82860510,std::array{0x3D608216u,0x807E8D60u,0x388BF748u,0x480004ADu,0x7F1D1840u,0x409A001Cu,0x3D608216u,0x807E8D60u,0x388BF738u,0x48000495u,0x907F0008u});
    pin(base,0x82860220,std::array{0x81630010u,0x2F0B0000u,0x419A0018u,0x3D608200u,0x388B1F00u,0x3D6082D1u,0x806B8D60u,0x4800078Cu,0x80630004u,0x4E800020u});
    pin(base,0x828602B0,std::array{0x7D8802A6u,0x9181FFF8u,0x9421FFA0u,0x81630000u,0x80830008u,0x816B0014u,0x7D6903A6u,0x4E800421u,0x3D6082D1u,0x806B8C34u,0x2B030000u,0x419A0008u,0x4BB5B461u,0x38600001u,0x38210060u,0x8181FFF8u,0x7D8803A6u,0x4E800020u});
    pin(base,0x82860120,std::array{0x90830004u,0x4E800020u});
    pin(base,0x82860128,std::array{0x90830008u,0x4E800020u});
    pin(base,0x82860AA0,std::array{0x39630008u,0x9081FFF0u,0x90A1FFF4u,0x814B0080u,0x2B0A0010u,0x4C980020u,0xE921FFF0u,0x554A1838u,0x7D2A592Au,0x814B0080u,0x394A0001u,0x914B0080u,0x4E800020u});
    pin(base,0x82860CB0,std::array{0x38800000u,0x907F0004u,0x39600000u});
    pin(base,0x82860CF4,std::array{0x81630000u,0x816B0004u,0x7D6903A6u,0x4E800421u});
    word(PPC_LOAD_U32(0x8215F710+0x10),0x82860220);word(PPC_LOAD_U32(0x8215F710+0x14),0x82860120);
    // Mutation probes exercise this source gate on copied words only; no
    // executable instruction or original asset is modified.
    rejects([&]{word(PPC_LOAD_U32(0x82860538)^1,0x907F0008);},"Changed debug-return producer word was admitted");
    rejects([&]{word(PPC_LOAD_U32(0x82860D00)^1,0x4E800421);},"Changed successor dispatch word was admitted");
}
void exercise(Runtime& rt,unsigned route){
    auto* base=rt.base;phase=route?"direct-stream debug return":"normal frontend return";
    std::memset(rt.pointer(area,0x1000,true),0,0x1000);
    // Original startup's stack registry initializes sixteen zero pairs and
    // count0. These are isolated CPU fixture addresses, not a live manager.
    PPC_STORE_U32(manager,0x8215F810);PPC_STORE_U32(0x82D08D60,manager);
    need(PPC_LOAD_U32(0x82D08C34)==0,"CPU route fixture inherited a real gameplay owner");
    PPCContext c{};std::memset(&c,0xA5,sizeof(c));c.r1.u64=0x20000;c.r13.u64=0;c.lr=0x82862234;c.fpscr.csr=0x1F80;
    currentContext=&c;EngineCpuCalls cpu(c,base);auto& call=cpu.registers();const Abi abi(call);
    const auto invoke=[&](uint32_t pc,auto... args){const auto result=cpu.invoke(pc,args...);abi.verify(call);return result;};
    const auto name=[&](uint32_t pc,std::string_view expected,uint32_t key){
        need(!std::memcmp(rt.pointer(pc,unsigned(expected.size()+1),false),expected.data(),expected.size()+1),"Original loop literal identity differs");
        need(invoke(0x828609C8,manager,pc)==key,"Actual original name producer returned a different key");
    };
    name(0x82001F00,"GameMainLoop",gameKey);name(0x82001F10,"FrontendMainLoop",frontendKey);
    name(0x8215F748,"InitOnceMainLoop",initKey);name(0x8215F738,"DebugFEMainLoop",debugKey);
    const auto allocations=rt.allocations.size();
    invoke(0x8285F640,frontend,gameKey);
    need(PPC_LOAD_U32(frontend)==0x8215F698&&PPC_LOAD_U32(frontend+4)==gameKey&&PPC_LOAD_U32(frontend+12)==1,
         "Whole original borrowed frontend constructor differs");
    for(uint32_t at:{8u,16u,20u,24u,28u})need(PPC_LOAD_U32(frontend+at)==0,"Original frontend constructor retained unexpected state");
    need(rt.allocations.size()==allocations,"Borrowed frontend constructor allocated fixture ownership");
    for(const auto pair:std::array{std::pair{initKey,initLeaf},std::pair{gameKey,gameLeaf},std::pair{frontendKey,frontend}})
        invoke(0x82860AA0,manager,pair.first,pair.second);
    need(PPC_LOAD_U32(manager+136)==3,"Actual registry did not retain all three original startup entries");
    need(invoke(0x82860B70,manager,initKey)==initLeaf&&invoke(0x82860B70,manager,gameKey)==gameLeaf&&
         invoke(0x82860B70,manager,frontendKey)==frontend,"Original registry owner lookup differs");
    // Game return-helper ABI fixture only. Its creator allocates three strings
    // and needs the actual original heap/pool bootstrap; this does not claim
    // those allocations or the world-dependent original enter/tick/cleanup.
    PPC_STORE_U32(gameLeaf,0x8215F710);
    const auto returnKey=route?debugKey:frontendKey;
    invoke(0x82860128,gameLeaf,returnKey); // genuine public stored-return setter
    need(PPC_LOAD_U32(gameLeaf+8)==returnKey,"Original stored return setter differs");
    need(invoke(0x828602B0,gameLeaf)==1&&PPC_LOAD_U32(gameLeaf+4)==returnKey,
         "Whole original loop-exit request did not invoke its real vtable setter");
    const auto registryBefore=std::array{PPC_LOAD_U64(manager+8),PPC_LOAD_U64(manager+16),PPC_LOAD_U64(manager+24)};
    const auto selected=invoke(0x82860220,gameLeaf);
    need(selected==returnKey,"Original post-cleanup successor getter differs");
    const auto successor=invoke(0x82860B70,manager,selected);
    need(successor==(route?0u:frontend),"Original frontend/debug successor availability differs");
    for(const auto missing:{0u,1u,0xFFFFFFFFu,debugKey})need(invoke(0x82860B70,manager,missing)==0,"Unregistered/malformed loop key acquired an owner");
    const char mutated[]="FrontendMainLooo";std::memcpy(rt.pointer(nameBuffer,sizeof(mutated),true),mutated,sizeof(mutated));
    const auto wrongKey=invoke(0x828609C8,manager,nameBuffer);
    need(wrongKey!=frontendKey&&invoke(0x82860B70,manager,wrongKey)==0,"Changed authored name byte acquired the genuine frontend owner");
    need(registryBefore==std::array{PPC_LOAD_U64(manager+8),PPC_LOAD_U64(manager+16),PPC_LOAD_U64(manager+24)}&&
         PPC_LOAD_U32(manager+136)==3,"Rejected key/hash lookup mutated the original registry");
    // Independently retain the actual native indirect-target rejection. This
    // is a malformed target probe, not a manufactured original manager tick.
    const auto beforeBadTarget=call;
    rejects([&]{cpu.invoke(0);},"Absent loop indirect target acquired a native fallback");
    need(!std::memcmp(&call,&beforeBadTarget,sizeof(call)),"Rejected null indirect target changed CPU context");
    std::printf("[ORIGINAL LOOP ROUTE] case=%u old=%08X return=%08X successor=%08X registrations=3; no tick/world/popup completion\n",route,gameKey,selected,successor);
    phase="original registry removal and borrowed frontend retirement";
    invoke(0x82860AD8,manager,frontendKey);need(invoke(0x82860B70,manager,frontendKey)==0&&PPC_LOAD_U32(manager+136)==2,"Original frontend registry removal failed");
    invoke(0x8285FBE0,frontend,0);need(PPC_LOAD_U32(frontend)==0x8215F674,"Original borrowed frontend destructor did not retire derived type");
    invoke(0x82860AD8,manager,gameKey);invoke(0x82860AD8,manager,initKey);
    need(PPC_LOAD_U32(manager+136)==0&&invoke(0x82860B70,manager,gameKey)==0&&invoke(0x82860B70,manager,initKey)==0,
         "Original registration retirement retained stale owner mappings");
    need(!PPC_LOAD_U32(0x82D08C34)&&rt.allocations.size()==allocations,"CPU route retirement changed gameplay or heap ownership");
    PPC_STORE_U32(0x82D08D60,0);
}
}
int main(int argc,char** argv){try{
    need(argc==2||argc==3,"Original image and optional independent loop case required");unsigned selected=2;
    if(argc==3){const auto end=argv[2]+std::strlen(argv[2]);auto result=std::from_chars(argv[2],end,selected);need(result.ec==std::errc{}&&result.ptr==end&&selected<2,"Invalid original loop case");}
    Runtime rt;rt.load(argv[1]);rt.map(0x10000,0x10000,true,"original loop CPU fixture stack");rt.map(area,0x1000,true,"borrowed loop registry and leaf ABI fixtures");
    sources(rt);if(selected<2)exercise(rt,selected);else{exercise(rt,0);exercise(rt,1);}
    std::printf("PASS original loop CPU routes: %zu checks; frontend create/register/use/remove/borrowed retirement; missing debug return retained; no world transition credit\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL original loop routes phase=%s checks=%zu: %s\n",phase,checks,error.what());return 1;}}
