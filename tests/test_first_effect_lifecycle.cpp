// One original FX registration row in an isolated fixture. Production remains
// the original 25-row loop. All allocations/callbacks are real original code.
#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include "runtime/engine_effects.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace Simpsons;
size_t checks{};
const char* stage="startup";
constexpr uint32_t table=0x82CEFD20,source=0x820B8AA0,blobSize=0xCAC,poolRoot=0x82D6D2F8;
constexpr std::array<uint32_t,8> scalarIds={0x2C,0x30,0x38,0x3C,0x48,0x4C,0x6C,0x130};
constexpr std::array<uint32_t,8> scalarValues={7,0,0,1,1,1,0,0};
constexpr std::array<uint32_t,5> samplerIds={0,4,0x10,0x14,0x18};
constexpr std::array<uint32_t,5> samplerValues={2,2,1,1,2};
constexpr std::array<uint32_t,20> defaults={
    0x3E555555,0,0,0x3F800000,0x3E2AAAAB,0,0,0x3F800000,
    0x3DAAAAAB,0,0,0x3F800000,0x3D2AAAAB,0,0,0x3F800000,0,0,0,0x3F800000};
void need(bool value,const char* message) {
    ++checks;
    if(!value) {
        std::fprintf(stderr,"CHECK %zu stage=%s: %s\n",checks,stage,message);
        throw Failure(message);
    }
}
std::vector<uint8_t> bytes(Runtime& rt,uint32_t address,uint32_t size) {
    const auto* p=rt.pointer(address,size,false);return {p,p+size};
}
void same(Runtime& rt,uint32_t address,const std::vector<uint8_t>& expected,const char* message) {
    need(!std::memcmp(rt.pointer(address,uint32_t(expected.size()),false),expected.data(),expected.size()),message);
}
template<class F> std::string rejects(F&& operation,const char* message) {
    try {operation();} catch(const Failure& error) {++checks;return error.what();}
    need(false,message);return {};
}
struct StartupObserved {};
void observe(PPCContext&,uint8_t* base) {
    need(active && active->engineDriver && active->engineDriver->started(),"Missing actual native driver");
    active->engineDriver->requireContext(PPC_LOAD_U32(0x82D5DA74));
    need(!PPC_LOAD_U32(0x82D08BFC),"Graphics manager was already published");
    throw StartupObserved{};
}
struct SavedAbi {
    uint64_t sp,lr;
    std::array<uint64_t,18> gpr;
};
SavedAbi abi(const PPCContext& c) {
    return {c.r1.u64,c.lr,{c.r14.u64,c.r15.u64,c.r16.u64,c.r17.u64,c.r18.u64,c.r19.u64,
        c.r20.u64,c.r21.u64,c.r22.u64,c.r23.u64,c.r24.u64,c.r25.u64,c.r26.u64,c.r27.u64,
        c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64}};
}
void sameAbi(const PPCContext& c,const SavedAbi& before) {
    const auto after=abi(c);
    need(after.sp==before.sp && after.lr==before.lr && after.gpr==before.gpr,
         "Original lifetime changed SP/LR or nonvolatile GPRs");
}
void emptyPool(Runtime& rt,uint32_t pool) {
    auto* base=rt.base;rt.pointer(pool,0x200,false);
    need(pool && !(pool&0x7F) && PPC_LOAD_U32(poolRoot)==pool,"Original CPU pool/root alignment differs");
    for(uint32_t i=0;i<0x100;++i)
        need(PPC_LOAD_U8(pool+i)==(i<0x80?0u:0xFFu),"Original empty pool bookkeeping differs");
    for(uint32_t i=0x100;i<0x128;i+=4) need(!PPC_LOAD_U32(pool+i),"Original shared pool is nonempty");
    need(!PPC_LOAD_U32(pool+0x180) && !PPC_LOAD_U32(pool+0x184) && PPC_LOAD_U32(pool+0x188)==1,
         "Original root-only pool ownership differs");
    // Constructor leaves padding uninitialized; never inspect its value.
}
void poolRetirementRejects(Runtime& rt,EngineCpuCalls& cpu,uint32_t pool,uint32_t manager,
                           uint32_t wrapper,uint32_t cache) {
    auto* base=rt.base;auto& effects=rt.engineDriver->effects();
    rejects([&]{effects.requirePoolReleased(pool);},"A live native FX did not lease its pool");
    rejects([&]{effects.requireReleased();},"A live native FX was reported released");
    const auto poolBefore=bytes(rt,pool,0x200),managerBefore=bytes(rt,manager,0x260);
    const auto wrapperBefore=bytes(rt,wrapper,0x30),cacheBefore=bytes(rt,cache,0xC8);
    const auto tableBefore=bytes(rt,table,25*16);
    // Invoke actual entries. Hook preflight must reject before their original
    // stack stores/free. No synthetic LR, direct hook call, or fake pool exists.
    for(const auto pair:std::array<std::array<uint32_t,2>,4>{{
            {0x82CC1820,0},{0x82722568,poolRoot},{0x82C1CF00,pool},{0x82C1D0A0,pool}}}) {
        const auto before=abi(cpu.registers());
        const uint32_t stack=cpu.registers().r1.u32-0x100;
        const auto stackBefore=bytes(rt,stack,0x180);
        const auto message=rejects([&]{cpu.invoke(pair[0],pair[1]);},"Live pool retirement was accepted");
        need(message.find("pool")!=std::string::npos || message.find("Pool")!=std::string::npos,
             "Pool retirement failed at an unrelated boundary");
        sameAbi(cpu.registers(),before);
        same(rt,stack,stackBefore,"Pool preflight wrote the original entry stack");
        same(rt,pool,poolBefore,"Pool retirement rejection changed CPU metadata");
        same(rt,manager,managerBefore,"Pool retirement rejection changed manager");
        same(rt,wrapper,wrapperBefore,"Pool retirement rejection changed wrapper");
        same(rt,cache,cacheBefore,"Pool retirement rejection changed cache");
        same(rt,table,tableBefore,"Pool retirement rejection changed registration table");
        need(PPC_LOAD_U32(poolRoot)==pool && effects.count()==1,"Pool retirement rejection lost ownership");
    }
}
void typedBlocks(Runtime& rt,uint32_t typed,uint32_t manager) {
    auto* base=rt.base;rt.pointer(typed,0xB8,false);
    need(PPC_LOAD_U32(typed)==0x8215036C && PPC_LOAD_U32(typed+0x10)==manager &&
         !PPC_LOAD_U32(typed+0x14),"Original typed callback/vtable/row index differs");
    const uint32_t a=PPC_LOAD_U32(typed+0x28),b=PPC_LOAD_U32(typed+0x38),c=PPC_LOAD_U32(typed+0x30);
    need(a && b && c>=4 && a!=b && a!=c && b!=c,"Typed constructor did not own its three CPU blocks");
    rt.pointer(a,0x600,false);rt.pointer(b,0x1E0,false);rt.pointer(c-4,0x2A4,false);
    for(uint32_t i=0;i<0x600;i+=4) need(!PPC_LOAD_U32(a+i),"Original typed 64-record defaults differ");
    for(uint32_t i=0;i<0x1E0;i+=4) need(!PPC_LOAD_U32(b+i),"Original typed four-record defaults differ");
    need(PPC_LOAD_U32(c-4)==24,"Original typed array allocation header differs");
    for(uint32_t i=0;i<24;++i) need(!PPC_LOAD_U32(c+28*i+20),"Original sparse typed array defaults differ");
    // Other words of the third block and T+A8..B7 are unwritten until later use.
}
void cacheContract(Runtime& rt,const EngineEffects::View& v) {
    auto* base=rt.base;const uint32_t w=v.wrapper,c=v.cache;
    rt.pointer(w,0x30,false);rt.pointer(c,0xC8,false);
    need(v.phase==EngineEffects::Phase::Reflected && PPC_LOAD_U32(w+0x10)==v.identity &&
         PPC_LOAD_U32(w+0x14)==w && PPC_LOAD_U32(w+0x18)==v.identity &&
         PPC_LOAD_U32(w+0x1C)==c && PPC_LOAD_U32(w+0x20)==0xC8 &&
         PPC_LOAD_U32(w+0x24)==c && PPC_LOAD_U32(w+0x28)==1 && !PPC_LOAD_U32(w+0x2C),
         "Original wrapper/reflected cache metadata differs");
    const std::array<uint32_t,6> header={0x3FFFC,0,c+0x18,c+0x78,8,5};
    for(uint32_t i=0;i<header.size();++i) need(PPC_LOAD_U32(c+4*i)==header[i],"Technique/pass cache row differs");
    for(uint32_t i=0;i<scalarIds.size();++i) {
        need(v.scalars[i].sdkId==scalarIds[i] && v.scalars[i].value==scalarValues[i],"Owned scalar metadata differs");
        need(PPC_LOAD_U32(c+0x18+12*i)==PPC_LOAD_U32(0x82E06F80+scalarIds[i]) &&
             PPC_LOAD_U32(c+0x1C+12*i)==scalarValues[i],"Scalar cache ignored live remap or original value");
    }
    for(uint32_t i=0;i<samplerIds.size();++i) {
        need(!v.samplers[i].stage && v.samplers[i].sdkId==samplerIds[i] &&
             v.samplers[i].value==samplerValues[i],"Owned sampler metadata differs");
        need(!PPC_LOAD_U32(c+0x78+16*i) && PPC_LOAD_U32(c+0x7C+16*i)==PPC_LOAD_U32(0x82E07118+samplerIds[i]) &&
             PPC_LOAD_U32(c+0x80+16*i)==samplerValues[i],"Sampler cache ignored live remap or original value");
    }
    need(v.defaultVectorWords.size()==defaults.size() &&
         std::equal(v.defaultVectorWords.begin(),v.defaultVectorWords.end(),defaults.begin()),
         "Owned original parameter defaults differ");
    // Saved-value slots have no before-allocation observer. Do not poison or
    // assign expected values to those slots, or claim their write history.
}
void lifetime(Runtime& rt,EngineCpuCalls& cpu,uint32_t cycle,uint32_t& previousIdentity) {
    auto* base=rt.base;auto& effects=rt.engineDriver->effects();
    const uint32_t context=PPC_LOAD_U32(0x82D5DA74),pool=PPC_LOAD_U32(poolRoot);
    const uint32_t options=cpu.registers().r1.u32+0x60;
    const auto tableBefore=bytes(rt,table,25*16),poolBefore=bytes(rt,pool,0x200);
    const auto sourceBefore=bytes(rt,source,blobSize);
    need(!effects.count() && !PPC_LOAD_U32(0x82D08BFC) && !PPC_LOAD_U32(table+8),"Lifecycle did not start empty");
    emptyPool(rt,pool);
    stage="original manager construction";
    PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    const uint32_t manager=cpu.invoke(0x8269BF70,0x260,options);
    need(manager && !(manager&15),"Actual manager allocation failed");
    const auto managerAbi=abi(cpu.registers());
    need(cpu.invoke(0x826B6F60,manager,context)==manager,"Original manager constructor result differs");
    sameAbi(cpu.registers(),managerAbi);
    need(PPC_LOAD_U32(0x82D08BFC)==manager && PPC_LOAD_U32(manager+0x18)==pool,"Original manager lost CPU pool root");

    // Second lifetime proves reflection uses LIVE tables, via original CPU
    // table setters. No render/apply happens with these fixture-only mappings.
    const uint32_t scalarBefore=PPC_LOAD_U32(0x82E06F80+scalarIds[0]);
    const uint32_t samplerBefore=PPC_LOAD_U32(0x82E07118+samplerIds[0]);
    if(cycle) {
        cpu.invoke(0x82831360,scalarIds[0],scalarBefore==2?3u:2u);
        cpu.invoke(0x82831380,samplerIds[0],samplerBefore==1?2u:1u);
    }
    stage="original one-row registration";
    const auto registrationAbi=abi(cpu.registers());
    need(cpu.invoke(0x827019E8,table,1)==0,"Original first-row registration failed");
    sameAbi(cpu.registers(),registrationAbi);
    const uint32_t wrapper=PPC_LOAD_U32(table+8);
    need(wrapper!=0,"Original registration did not publish its wrapper");rt.pointer(wrapper,0x30,false);
    const uint32_t id=PPC_LOAD_U32(wrapper+0x10);
    need(id && id!=previousIdentity && !rt.pageAccess[id>>12].load(),"FX identity was missing, reused, or SDK-mapped");
    need(effects.count()==1,"Original registration did not create exactly one native effect");
    need(effects.compiledShaderCount(id)==2,"Original FX did not own both real compiled native shaders");
    const auto v=effects.view(id);
    need(v.identity==id && v.wrapper==wrapper && v.manager==manager && v.source==source && v.pool==pool,
         "Native FX lost original ownership/provenance");
    cacheContract(rt,v);
    const auto owned=effects.originalBytes(id);
    need(owned.size()==blobSize && owned.data()!=rt.pointer(source,blobSize,false) &&
         std::equal(owned.begin(),owned.end(),sourceBefore.begin()),"Original FX bytes are not a complete independent owned copy");
    const uint32_t name=PPC_LOAD_U32(table+4),ownedName=PPC_LOAD_U32(wrapper+4);
    constexpr char expectedName[]="fourtapblend";
    need(ownedName && ownedName!=name && PPC_LOAD_U16(wrapper+8)==12 &&
         !std::memcmp(rt.pointer(ownedName,sizeof(expectedName),false),expectedName,sizeof(expectedName)),
         "Original wrapper name allocation differs");
    same(rt,table+16,std::vector<uint8_t>(tableBefore.begin()+16,tableBefore.end()),"Registration touched another original FX row");
    need(PPC_LOAD_U32(table)==source && PPC_LOAD_U32(table+12)==0x8273B280,"Original callback/source table changed");
    const uint32_t typed=cpu.invoke(0x826B7088,manager,name);
    need(typed!=0,"Original typed resource was not inserted in the manager");
    typedBlocks(rt,typed,manager);
    need(effects.technique(id,"Technique0")==0x3FFFC && effects.parameter(id,"g_Weights")==0x40000 &&
         effects.parameter(id,"g_Sampler")==0x180008,"Native reflected handles differ");
    need(!effects.technique(id,"missing") && !effects.parameter(id,"missing") &&
         !effects.technique(id,"g_Sampler") && !effects.parameter(id,"Technique0"),"Missing/cross-namespace query was fabricated");
    stage="original typed finalizer";
    const auto typedBefore=bytes(rt,typed,0xA8);
    const auto finalizerAbi=abi(cpu.registers());
    cpu.invoke(0x8273B3B8,typed);sameAbi(cpu.registers(),finalizerAbi);
    same(rt,typed,typedBefore,"Typed finalizer changed fields outside its four outputs");
    need(PPC_LOAD_U32(typed+0xA8)==wrapper && PPC_LOAD_U32(typed+0xAC)==id &&
         PPC_LOAD_U32(typed+0xB0)==0x3FFFC && PPC_LOAD_U32(typed+0xB4)==0x180008,
         "Original typed finalizer did not store exact wrapper/effect/handles");
    same(rt,pool,poolBefore,"Native host lease changed original pool bytes/refcount");
    stage="live pool retirement rejection";
    poolRetirementRejects(rt,cpu,pool,manager,wrapper,v.cache);
    stage="original paired typed/wrapper cleanup";
    const auto cleanupAbi=abi(cpu.registers());
    cpu.invoke(0x82701118,table,1);sameAbi(cpu.registers(),cleanupAbi);
    need(!effects.count() && !PPC_LOAD_U32(table+8),"Original paired cleanup retained native ownership/publication");
    effects.requirePoolReleased(pool);effects.requireReleased();
    need(!cpu.invoke(0x826B7088,manager,name),"Original paired cleanup retained the typed registration");
    rejects([&]{(void)effects.view(id);},"Released FX identity remained valid");
    rejects([&]{(void)effects.originalBytes(id);},"Released FX source remained borrowable");
    rejects([&]{(void)effects.compiledShaderCount(id);},"Released FX shader owner remained valid");
    rejects([&]{(void)effects.technique(id,"Technique0");},"Released FX still answered technique queries");
    rejects([&]{(void)effects.parameter(id,"g_Sampler");},"Released FX still answered parameter queries");
    // W/T/cache/name blocks are now freed. Never inspect them after cleanup.
    same(rt,pool,poolBefore,"Paired cleanup freed/mutated the original root pool");
    same(rt,table,tableBefore,"Paired cleanup did not restore the original registration table");
    same(rt,source,sourceBefore,"Fixture changed original source bytes in memory");
    stage="original manager destruction";
    const auto destructionAbi=abi(cpu.registers());
    need(cpu.invoke(0x826B7600,manager,1)==manager,"Original manager deletion returned another object");
    sameAbi(cpu.registers(),destructionAbi);
    need(!PPC_LOAD_U32(0x82D08BFC) && PPC_LOAD_U32(poolRoot)==pool && PPC_LOAD_U32(pool+0x188)==1,
         "Manager destruction lost original singleton/pool lifetime");
    cpu.invoke(0x82831360,scalarIds[0],scalarBefore);cpu.invoke(0x82831380,samplerIds[0],samplerBefore);
    need(PPC_LOAD_U32(0x82D5DA74)==context && !PPC_LOAD_U32(0x82D0CAF8),"FX lifetime constructed/changed console device identity");
    rt.engineDriver->requireContext(context);
    previousIdentity=id;
    std::printf("FX cycle=%u manager=%08X wrapper=%08X typed=%08X identity=%08X pool=%08X; paired cleanup complete\n",
                cycle,manager,wrapper,typed,id,pool);std::fflush(stdout);
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==2,"Original image path required");Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);
        // Diagnostics must print before Runtime's terminal shutdown on failure.
        try {
            const auto entry=original;bool observed=false;
            rt.audioBoundaryObserver=[](uint32_t pc,PPCContext& ctx,uint8_t* base) {
                if(pc==0x828166FC) observe(ctx,base);
                else need(pc==0x82345920,"Unexpected original startup observation");
            };
            try {runOriginal(original,rt.base);} catch(const StartupObserved&) {observed=true;}
            rt.audioBoundaryObserver={};
            need(observed,"Original startup did not reach the established post-audio boundary");
            EngineCpuCalls cpu(entry,rt.base);uint32_t previousIdentity=0;
            for(uint32_t cycle=0;cycle<2;++cycle) lifetime(rt,cpu,cycle,previousIdentity);
            std::printf("PASS first effect lifecycle: %zu checks; two real manager/registration/finalizer/cleanup lifetimes; host pool leases; ALL MUTED; no draw claim\n",checks);
        } catch(const std::exception& error) {
            std::fprintf(stderr,"First FX fixture failed before Runtime teardown: stage=%s checks=%zu error=%s\n",
                         stage,checks,error.what());throw;
        } catch(...) {std::fprintf(stderr,"First FX fixture failed before Runtime teardown: stage=%s checks=%zu\n",stage,checks);throw;}
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL first FX lifecycle: %s\n",error.what());return 1;}
}
