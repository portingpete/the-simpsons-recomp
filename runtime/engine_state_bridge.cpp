#include "engine_state_bridge.h"
#include "engine_cpu_calls.h"
#include "engine_cache_transaction.h"
#include "engine_driver.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <vector>
#include <optional>
// Keep this last: shared header bodies retain their normal definitions.
#include "aot_inline_memory.h"

namespace {
thread_local Simpsons::EngineRenderState* stateScope{};
constexpr uint32_t scalarPending=0x82D0F3B0,scalarApplied=0x82E3D580;
constexpr uint32_t scalarQueue=0x82D0ED08,scalarCount=0x82D10114,scalarSize=425;
constexpr uint32_t stagePending=0x82D0DB70,stageApplied=0x82E3D160;
constexpr uint32_t stageQueue=0x82D0E4C8,stageCount=0x82D10118,stageSize=264;
constexpr uint32_t samplerCache=0x82D0D170,stageRecords=0x82D501E0;
struct Word {uint32_t address,value;};
// Direct engine CPU stores in 824008E0, including exact original float bits.
constexpr Word startupWords[]={
    {0x82D0E3B0,1},{0x82D0E3B4,1},{0x82D0E3B8,0},{0x82D0E3BC,1},
    {0x82D0E3C0,1},{0x82D0E3C4,1},{0x82D0E3C8,8},{0x82D0E3CC,0},
    {0x82D0E3D0,0xFFFFFFFF},{0x82D0E3D4,0xFFFFFFFF},{0x82D0E3D8,0},{0x82D0E3DC,0},
    {0x82D0E3E0,2},{0x82D0E3E4,0},{0x82D0E3E8,1},{0x82D0E3EC,0x3F800000},
    {0x82D0E3F0,0},{0x82D0E3F4,2},{0x82D0E4B8,5},{0x82D0E4BC,6},
    {0x82D0E4C0,5},{0x82D0E4C4,1},{0x82D10100,0},{0x82D10104,0},
    {0x82D10108,0},{0x82D1011C,0},{0x82D10120,0},{0x82D10124,0},{0x82D10128,0}
};
// Calls to the original scalar queue helper, in original order.
constexpr Word startupScalars[]={
    {0x196,0},{0x1A1,3},{0x198,0},{0x195,2},{0x2C,6},{0x30,1},{0x28,1},
    {0x6C,0},{0x74,0},{0x78,0},{0x7C,0},{0x80,7},{0x84,0},{0x88,0xFFFFFFFF},
    {0x8C,0xFFFFFFFF},{0x48,6},{0x4C,7},{0x68,4},{0x64,0},{0x3C,0},{0x60,0},
    {0x19F,0},{0x38,2},{0x1A5,0},{0x1A6,0},{0x1A7,0},{0x197,0},{0x1A3,0},
    {0x1A0,0xFFFFFFFF},{0x1A4,0}
};
// The known CPU callees below only mutate these engine cache/record windows.
// Capturing them also preflights every publication, so failure cannot make a
// guest cache claim that an unsupported native state was applied.
using CacheTransaction=Simpsons::EngineCacheTransaction;
Simpsons::EngineRenderState& scope(uint8_t* base) {
    if(!stateScope || !Simpsons::active || base!=Simpsons::active->base)
        throw Simpsons::Failure("Native engine state reached outside its owner scope");
    return *stateScope;
}

constexpr uint32_t rebuildStackBytes=0x1C0; // C0 original frame + 100 commit callback frame.
constexpr std::array<uint32_t,7> rebuildSamplerIds={0x14,0x10,0x18,0,4,0xC,0x24};
struct RebuildPlan {
    std::array<std::array<uint32_t,7>,8> samplers{};
};

template<size_t N>
void requireTable(uint8_t* base,uint32_t address,const std::array<uint32_t,N>& expected) {
    Simpsons::active->pointer(address,4*N,false);
    for(size_t i=0;i<N;++i) if(PPC_LOAD_U32(address+uint32_t(4*i))!=expected[i])
        throw Simpsons::Failure("Original RW rebuild conversion table changed");
}
// Contiguous, byte-verified arrays at 82062CA0..82062DC7. Bounds describe
// original table storage; the effective owner separately rejects unsupported
// values (notably anisotropic filters and non-baseline stencil operations).
RebuildPlan planRebuild(uint8_t* base,const Simpsons::Graphics::EngineState& effective) {
    constexpr std::array<uint32_t,3> shade={0,1,2};
    constexpr std::array<uint32_t,4> fog={0,3,1,2},cull={0,0,2,6};
    constexpr std::array<uint32_t,12> blend={0,0,1,4,5,6,7,10,11,8,9,16};
    constexpr std::array<uint32_t,5> address={0,0,1,2,6};
    constexpr std::array<uint32_t,28> filter={2,2,0,2,1,2,0,0,1,0,0,1,1,1,4,2,4,2,4,2,4,0,4,0,4,1,4,1};
    constexpr std::array<uint32_t,9> stencil={0,0,1,2,3,4,5,6,7},compare={0,0,1,2,3,4,5,6,7};
    requireTable(base,0x82062CA0,shade);requireTable(base,0x82062CAC,fog);
    requireTable(base,0x82062CBC,blend);requireTable(base,0x82062CEC,address);
    requireTable(base,0x82062D00,filter);requireTable(base,0x82062D70,cull);
    requireTable(base,0x82062D80,stencil);requireTable(base,0x82062DA4,compare);
    auto lookup=[&](const auto& table,uint32_t source) {
        const uint32_t index=PPC_LOAD_U32(source);
        if(index>=table.size()) throw Simpsons::Failure("Original RW rebuild source index is out of bounds");
        return table[index];
    };
    // These high engine IDs are CPU-only; validate their indexed sources but
    // preserve all original raw color/float words, including 199/19A snapshots.
    lookup(fog,0x82D0E3E8);lookup(shade,0x82D0E3F4);
    auto next=effective;
    const uint32_t depthWrite=PPC_LOAD_U32(0x82D0E3B0),depthTest=PPC_LOAD_U32(0x82D0E3B4);
    next.setScalar(0x30,depthWrite);next.setScalar(0x2C,depthTest?6:7);
    next.setScalar(0x28,(depthTest || depthWrite)?1:0);
    next.setScalar(0x6C,PPC_LOAD_U32(0x82D0E3B8));
    for(auto pair:std::array<Word,3>{{{0x74,0x82D0E3BC},{0x78,0x82D0E3C0},{0x7C,0x82D0E3C4}}})
        next.setScalar(pair.address,lookup(stencil,pair.value));
    next.setScalar(0x80,lookup(compare,0x82D0E3C8));
    for(auto pair:std::array<Word,3>{{{0x84,0x82D0E3CC},{0x88,0x82D0E3D0},{0x8C,0x82D0E3D4}}})
        next.setScalar(pair.address,PPC_LOAD_U32(pair.value));
    next.setScalar(0x48,lookup(blend,0x82D0E4B8));next.setScalar(0x4C,lookup(blend,0x82D0E4BC));
    next.setScalar(0x68,lookup(compare,0x82D0E4C0));
    next.setScalar(0x64,PPC_LOAD_U32(scalarPending+8*0x64)); // r21 before reset, NOT an applied/default value.
    const bool vertexAlpha=PPC_LOAD_U32(0x82D0E3D8)!=0;
    next.setScalar(0x3C,vertexAlpha?1:0);
    next.setScalar(0x60,vertexAlpha?PPC_LOAD_U32(0x82D0E4C4):0);
    next.setScalar(0x38,lookup(cull,0x82D0E3E0));
    RebuildPlan plan;
    for(uint32_t stage=0;stage<8;++stage) {
        const uint32_t source=0x82D0E3F8+24*stage,index=PPC_LOAD_U32(source+12);
        if(index>=filter.size()/2) throw Simpsons::Failure("Original RW rebuild filter index is out of bounds");
        plan.samplers[stage]={filter[2*index],filter[2*index],filter[2*index+1],
            lookup(address,source+4),lookup(address,source+8),PPC_LOAD_U32(source+16),PPC_LOAD_U32(source+20)};
        for(size_t i=0;i<rebuildSamplerIds.size();++i) next.setSampler(stage,rebuildSamplerIds[i],plan.samplers[stage][i]);
    }
    return plan;
}

struct RebuildExecution {
    Simpsons::EngineRenderState* owner;
    PPCContext* context;
    uint8_t* base;
    Simpsons::Graphics::EngineState& effective;
    const RebuildPlan& plan;
    uint32_t originalSp;
    uint32_t samplerCalls{};
    bool entered{};
};
thread_local RebuildExecution* rebuildExecution{};
struct RebuildScope {
    explicit RebuildScope(RebuildExecution& execution) {rebuildExecution=&execution;}
    ~RebuildScope() {rebuildExecution=nullptr;}
};
RebuildExecution& requireRebuild(PPCContext& ctx,uint8_t* base) {
    if(!rebuildExecution || rebuildExecution->owner!=&scope(base) || rebuildExecution->context!=&ctx ||
       Simpsons::currentContext!=&ctx || rebuildExecution->base!=base || PPC_LOAD_U32(0x82D0CAF8))
        throw Simpsons::Failure("RW rebuild callback is outside its exact native CPU scope");
    return *rebuildExecution;
}
void rebuildSampler(PPCContext& ctx,uint8_t* base,uint32_t id,bool filterCall,bool engineCall=false) {
    auto& run=requireRebuild(ctx,base);
    const uint32_t stage=ctx.r27.u32,k=run.samplerCalls%7;
    const uint32_t value=filterCall || engineCall?ctx.r5.u32:ctx.r10.u32;
    if(!run.entered || stage>=8 || stage!=run.samplerCalls/7 || id!=rebuildSamplerIds[k] ||
       value!=run.plan.samplers[stage][k] || ctx.r1.u32!=run.originalSp-0xC0 ||
       ctx.r31.u32!=samplerCache || ctx.r30.u32!=0x82062CA0 || ctx.r24.u32!=24*stage ||
       ctx.r28.u32!=0x82D0E404+24*stage || ctx.r29.u32!=samplerCache+0x40+320*stage ||
       ctx.r25.u64!=(uint64_t(1)<<63)>>(stage+32))
        throw Simpsons::Failure("RW rebuild sampler callback has an invalid order, source or ABI");
    if((filterCall && (ctx.r3.u32 || ctx.r4.u32!=stage)) ||
       (engineCall && (ctx.r3.u32!=stage || ctx.r4.u32!=id ||
                     ctx.lr!=(id==0xC?0x824010DCu:0x824010ECu))))
        throw Simpsons::Failure("RW rebuild sampler callback arguments are invalid");
    const uint32_t cache=samplerCache+320*stage+4*id;
    // The two BL sites already stored the cache. Inline replacements must own
    // the original CPU stw. 82400278 remains the existing native engine entry.
    if(PPC_LOAD_U32(cache)!=(filterCall?value:0xFFFFFFFFu))
        throw Simpsons::Failure("RW rebuild sampler cache disagrees with the original CPU schedule");
    auto next=run.effective;next.setSampler(stage,id,value);
    if(!filterCall) PPC_STORE_U32(cache,value);
    run.effective=next;
    if(filterCall) ctx.lr=id==0x14?0x82400FDC:0x82401008; // Replaced original BL.
    ++run.samplerCalls;
}

struct RwSamplerPlan {std::vector<Word> writes;};
RwSamplerPlan planRwSampler(uint8_t* base,const Simpsons::Graphics::EngineState& effective,uint32_t selector,uint32_t value) {
    if(selector!=2 && selector!=9)throw Simpsons::Failure("Unqualified RenderWare sampler selector");
    if(PPC_LOAD_U8(0x82062DE8+selector-1)!=(selector==2?0x47:0x50))
        throw Simpsons::Failure("Original RenderWare sampler dispatch table changed");
    if(PPC_LOAD_U32(0x82D0CAF8))throw Simpsons::Failure("RenderWare sampler requires native-only device ownership");
    Simpsons::active->pointer(samplerCache,0x12A0,true);
    auto next=effective;
    RwSamplerPlan plan;
    auto add=[&](uint32_t id,uint32_t v) {
        next.setSampler(0,id,v); // Validate the entire schedule before its first CPU store.
        plan.writes.push_back({id,v});
    };
    if(selector==2) {
        constexpr std::array<uint32_t,5> address={0,0,1,2,6};
        requireTable(base,0x82062CEC,address);
        if(value>=address.size())throw Simpsons::Failure("Original RenderWare address index is out of bounds");
        if(PPC_LOAD_U32(0x82D0E3FC)!=value)add(0,address[value]);
        if(PPC_LOAD_U32(0x82D0E400)!=value)add(4,address[value]);
    } else {
        constexpr std::array<uint32_t,28> filter={2,2,0,2,1,2,0,0,1,0,0,1,1,1,4,2,4,2,4,2,4,0,4,0,4,1,4,1};
        requireTable(base,0x82062D00,filter);
        if(value>=filter.size()/2)throw Simpsons::Failure("Original RenderWare filter index is out of bounds");
        if(int32_t(PPC_LOAD_U32(0x82D0E40C))>1)add(0x24,1);
        if(PPC_LOAD_U32(0x82D0E404)!=value) {
            add(0x14,filter[2*value]);add(0x10,filter[2*value]);
            if(PPC_LOAD_U32(samplerCache+0x60)!=filter[2*value+1])add(0x18,filter[2*value+1]);
        }
    }
    return plan;
}
struct RwSamplerExecution {
    Simpsons::EngineRenderState* owner;
    PPCContext* context;
    Simpsons::Graphics::EngineState& effective;
    const RwSamplerPlan& plan;
    uint32_t selector,request,sp;
    size_t cursor{};
    bool entered{};
};
thread_local RwSamplerExecution* rwSamplerExecution{};
RwSamplerExecution& requireRwSampler(PPCContext& ctx,uint8_t* base) {
    if(!rwSamplerExecution || rwSamplerExecution->owner!=&scope(base) ||
       rwSamplerExecution->context!=&ctx || Simpsons::currentContext!=&ctx ||
       !Simpsons::active->engineDriver || PPC_LOAD_U32(0x82D0CAF8))
        throw Simpsons::Failure("RenderWare sampler callback is outside its native CPU scope");
    Simpsons::active->engineDriver->effectiveState(); // Recheck thread/lifetime ownership.
    return *rwSamplerExecution;
}
void rwSamplerEntry(PPCContext& ctx,uint8_t* base,uint32_t selector) {
    auto& run=requireRwSampler(ctx,base);
    if(run.entered || run.selector!=selector || ctx.r3.u32!=run.request || ctx.r1.u32!=run.sp)
        throw Simpsons::Failure("Original RenderWare sampler helper entry ABI changed");
    run.entered=true;
}
void rwSamplerApply(PPCContext& ctx,uint8_t* base,uint32_t id,uint32_t resume) {
    auto& run=requireRwSampler(ctx,base);
    if(!run.entered || run.cursor>=run.plan.writes.size())
        throw Simpsons::Failure("Unexpected RenderWare sampler callback");
    const auto write=run.plan.writes[run.cursor];
    if(write.address!=id || PPC_LOAD_U32(samplerCache+4*id)!=write.value)
        throw Simpsons::Failure("Original RenderWare sampler CPU store/order changed");
    if(run.selector==2) {
        if(ctx.r1.u32!=run.sp || ctx.r3.u32!=run.request || ctx.r9.u32!=write.value ||
           ctx.r10.u32 || ctx.r11.u32!=samplerCache || ctx.r7.u32!=0x82D10000 ||
           PPC_LOAD_U32(id?0x82D0E400:0x82D0E3FC)!=run.request)
            throw Simpsons::Failure("Original RenderWare address callback ABI changed");
    } else {
        if(ctx.r1.u32!=run.sp-0x80 || ctx.r31.u32!=samplerCache || ctx.r29.u32!=0x82D10000 ||
           (id==0x18?(ctx.r11.u32 || ctx.r28.u32!=write.value):
                        (ctx.r3.u32 || ctx.r4.u32 || ctx.r5.u32!=write.value)))
            throw Simpsons::Failure("Original RenderWare filter callback ABI changed");
    }
    auto next=run.effective;next.setSampler(0,id,write.value);run.effective=next;
    if(resume)ctx.lr=resume; // Only replaced BL instructions change LR.
    ++run.cursor;
}
}

namespace Simpsons {
struct EngineRenderState::Owner {
    Graphics::EngineState effective;CommitCounts commits;
    std::optional<Graphics::EngineState> beforeRecording;
    uint32_t recordingContext{};
    std::array<bool,0x57> seededScalars{};
    std::array<bool,16*20> seededSamplers{};
};
EngineRenderState::CommitCounts EngineRenderState::commitCounts() const noexcept {return owner->commits;}
EngineRenderState::EngineRenderState():owner(std::make_unique<Owner>()) {
    if(stateScope) throw Failure("Nested native engine state scope");
    stateScope=this;
}
EngineRenderState::~EngineRenderState() {stateScope=nullptr;}
const Graphics::EngineState& EngineRenderState::effective() const {return owner->effective;}
uint32_t EngineRenderState::recordingContext() const noexcept {return owner->recordingContext;}
void EngineRenderState::beginRecording(uint32_t context) {
    if(!context||owner->beforeRecording||!owner->effective.initialized())throw Failure("Invalid native recording state scope");
    owner->beforeRecording=owner->effective;owner->recordingContext=context;
    owner->seededScalars.fill(false);owner->seededSamplers.fill(false);
    // This is a separate CPU state destination. Original827246C8/82724728
    // must force every registered application row before any recorded draw.
}
void EngineRenderState::requireRecordingSeed(uint32_t context) const {
    if(!owner->beforeRecording||owner->recordingContext!=context)throw Failure("Missing native recording state scope");
    auto* base=active->base;
    const auto count=PPC_LOAD_U32(0x82D6D7E8);
    if(count!=82)throw Failure("Original recording scalar seed extent differs");
    // The registration list is mutable guest storage; duplicates must not
    // conceal a missing field. Require the complete independently pinned set.
    for(const auto& field:Graphics::scalarStateEvidence()) {
        const auto selector=field.id/4-9;
        if(selector>=owner->seededScalars.size()||!owner->seededScalars[selector])throw Failure("Original recording scalar seed is incomplete");
    }
    for(bool seeded:owner->seededSamplers)if(!seeded)throw Failure("Original recording sampler seed is incomplete");
}
void EngineRenderState::endRecording(uint32_t context) {
    if(!owner->beforeRecording||owner->recordingContext!=context)throw Failure("Native recording state context differs at restore");
    owner->effective=*owner->beforeRecording;owner->beforeRecording.reset();owner->recordingContext=0;
}
void EngineRenderState::publishScreenState(const Graphics::EngineState& state) noexcept {owner->effective=state;}

void EngineRenderState::preflightRebuild(EngineCpuCalls& cpu,uint8_t* base) const {
    if(&scope(base)!=this || !owner->effective.initialized() || rebuildExecution ||
       currentContext!=&cpu.registers()) throw Failure("Invalid or nested RW state rebuild scope");
    if(PPC_LOAD_U32(0x82D0CAF8) || PPC_LOAD_U32(0x821DD0D8)!=0)
        throw Failure("RW state rebuild requires native-only context and the original zero constant");
    for(auto [address,size]:std::array<std::pair<uint32_t,uint32_t>,3>{{
        {samplerCache,0x2FBC},{stageApplied,0xB24},{stageRecords,0x140}}}) active->pointer(address,size,true);
    const auto sp=cpu.registers().r1.u32;
    if(sp<rebuildStackBytes || (sp&15)) throw Failure("Invalid RW state rebuild callback stack");
    active->pointer(sp-rebuildStackBytes,rebuildStackBytes,true);
    const uint32_t n=PPC_LOAD_U32(scalarCount),m=PPC_LOAD_U32(stageCount);
    if(n>scalarSize || m>stageSize) throw Failure("Original RW rebuild dirty count is out of bounds");
    std::array<bool,scalarSize> scalars{};
    std::array<bool,stageSize> stages{};
    for(uint32_t i=0;i<n;++i) {
        const uint32_t id=PPC_LOAD_U32(scalarQueue+4*i);
        if(id>=scalarSize || scalars[id] || PPC_LOAD_U32(scalarPending+8*id+4)!=1)
            throw Failure("Original RW rebuild scalar dirty membership is invalid");
        scalars[id]=true;
    }
    for(uint32_t i=0;i<m;++i) {
        const uint32_t stage=PPC_LOAD_U32(stageQueue+8*i),id=PPC_LOAD_U32(stageQueue+8*i+4);
        if(stage>=8 || id>=33) throw Failure("Original RW rebuild stage dirty index is invalid");
        const uint32_t index=stage*33+id;
        if(stages[index] || PPC_LOAD_U32(stagePending+8*index+4)!=1)
            throw Failure("Original RW rebuild stage dirty membership is invalid");
        stages[index]=true;
    }
    (void)planRebuild(base,owner->effective);
}

void EngineRenderState::rebuild(EngineCpuCalls& cpu,uint8_t* base) {
    preflightRebuild(cpu,base);
    const auto plan=planRebuild(base,owner->effective);
    CacheTransaction transaction;
    const auto previous=owner->effective;
    const auto incoming=cpu.registers();
    auto* stack=active->pointer(incoming.r1.u32-rebuildStackBytes,rebuildStackBytes,true);
    const std::vector<uint8_t> savedStack(stack,stack+rebuildStackBytes);
    RebuildExecution execution{this,&cpu.registers(),base,owner->effective,plan,incoming.r1.u32};
    RebuildScope guard(execution);
    try {
        cpu.invoke(0x82400D50); // Original reset, retained-source reads, queue helpers and final commit.
        if(!execution.entered || execution.samplerCalls!=56 || PPC_LOAD_U32(scalarCount) || PPC_LOAD_U32(stageCount))
            throw Failure("Original RW state rebuild did not complete its checked CPU schedule");
        transaction.publish();
    } catch(...) {
        owner->effective=previous;
        std::memcpy(stack,savedStack.data(),savedStack.size());
        cpu.registers()=incoming;
        throw;
    }
}

uint32_t EngineRenderState::applicationScalar(uint8_t* base,uint32_t application,uint32_t selector,
                                            uint32_t value,bool apply) {
    if(&scope(base)!=this || !owner->effective.initialized() || !application || (application&3) ||
       selector==0 || selector>=0x57)
        throw Failure("Invalid native application scalar owner or selector");
    const uint32_t id=(selector+9)*4;
    if(PPC_LOAD_U32(0x82150580+4*selector)!=id)
        throw Failure("Original application scalar selector table changed");
    const auto fields=Graphics::scalarStateEvidence();
    const auto field=std::find_if(fields.begin(),fields.end(),[id](const auto& f){return f.id==id;});
    if(field==fields.end()) {
        char message[160];
        std::snprintf(message,sizeof(message),"Unsupported native application scalar selector 0x%X (SDK offset 0x%X)",selector,id);
        throw Failure(message);
    }
    // 82466800 installs this row's setter at context+40+id. Native execution
    // uses the checked effective owner; it never creates a guest SDK object.
    if(PPC_LOAD_U32(0x82CD28B8+3*id+4)!=field->setterAddress)
        throw Failure("Original application scalar SDK setter table changed");
    const uint32_t count=PPC_LOAD_U32(0x82D6D7E8),index=PPC_LOAD_U32(0x82D6D498+4*selector);
    if(!count || count>82 || index>=count || PPC_LOAD_U32(0x82D6D6A0+4*index)!=selector)
        throw Failure("Original application scalar cache mapping is invalid");
    active->pointer(application,0xD2C,false);
    active->pointer(application+0x694+4*index,4,true);
    const int32_t depth=int32_t(PPC_LOAD_U32(application+0xD28));
    // Eight saved 0x694-byte frames precede the constructor's flags at +41CC.
    if(depth<0 || depth>8) throw Failure("Original application state stack depth is out of bounds");
    if(depth) {
        const uint64_t frame=uint64_t(application)+uint32_t(depth)*0x694+0x698;
        if(frame+0x694>0x100000000ull) throw Failure("Original application state stack address overflow");
        active->pointer(uint32_t(frame)+4*index,4,false);
        active->pointer(uint32_t(frame)+4*(0x192+(index>>5)),4,true);
    }
    auto next=owner->effective;
    next.setScalar(id,value); // Includes forced equal-value blend broadcasts.
    if(apply) {owner->effective=next;if(owner->beforeRecording)owner->seededScalars[selector]=true;}
    return field->setterAddress;
}

uint32_t EngineRenderState::applicationSampler(uint8_t* base,uint32_t application,uint32_t stage,
                                             uint32_t selector,uint32_t value,bool apply) {
    if(&scope(base)!=this || !owner->effective.initialized() || !application || (application&3) ||
       stage>=16 || selector==0 || selector>20)
        throw Failure("Invalid native application sampler owner, stage or selector");
    const uint32_t id=(selector-1)*4;
    if(PPC_LOAD_U32(0x821506E0+4*selector)!=id)
        throw Failure("Original application sampler selector table changed");
    const auto field=Graphics::samplerStateEvidence()[selector-1];
    if(field.id!=id || PPC_LOAD_U32(0x82CD2D78+3*id+4)!=field.setterAddress)
        throw Failure("Original application sampler SDK setter table changed");
    // The first initialization registers exactly twenty categories, in an order
    // independent of selectors and SDK offsets. Appended registrations reject.
    constexpr std::array<uint32_t,21> categories={0x7FFFFFFF,0,1,2,8,3,4,5,9,10,13,6,7,15,11,12,14,16,17,18,19};
    if(PPC_LOAD_U32(0x82D6D7EC)!=20) throw Failure("Original application sampler count is invalid");
    for(uint32_t t=0;t<categories.size();++t) {
        if(PPC_LOAD_U32(0x82D6D648+4*t)!=categories[t] ||
           (t && PPC_LOAD_U32(0x82D6D5F8+4*categories[t])!=t))
            throw Failure("Original application sampler category mapping is invalid");
    }
    const uint32_t index=categories[selector];
    const uint64_t end=uint64_t(application)+0x41D4;
    if(end>0x100000000ull) throw Failure("Original application sampler owner address overflow");
    active->pointer(application,0x41D4,false);
    active->pointer(application+0x7DC+0x50*stage+4*index,4,true);
    const int32_t depth=int32_t(PPC_LOAD_U32(application+0xD28));
    if(depth<0 || depth>8) throw Failure("Original application state stack depth is out of bounds");
    if(depth) {
        const uint32_t frame=application+uint32_t(depth)*0x694+0x698;
        active->pointer(frame+0x148+0x50*stage+4*index,4,false);
        active->pointer(frame+0x654+4*(stage+(index>>5)),4,true);
    }
    auto next=owner->effective;
    next.setSampler(stage,id,value); // No RenderWare sampler-cache lookup or write.
    if(apply) {owner->effective=next;if(owner->beforeRecording)owner->seededSamplers[20*stage+selector-1]=true;}
    return field.setterAddress;
}

void EngineRenderState::directScalar(uint8_t* base,uint32_t id,uint32_t value) {
    if(&scope(base)!=this || !owner->effective.initialized()) throw Failure("Native direct scalar has no initialized owner");
    const auto fields=Graphics::scalarStateEvidence();
    const auto field=std::find_if(fields.begin(),fields.end(),[id](const auto& f){return f.id==id;});
    if(field==fields.end() || PPC_LOAD_U32(0x82CD28B8+3*id+4)!=field->setterAddress)
        throw Failure("Native direct scalar has no verified original setter");
    auto next=owner->effective;next.setScalar(id,value);owner->effective=next;
}
void EngineRenderState::directSampler(uint8_t* base,uint32_t stage,uint32_t id,uint32_t value) {
    if(&scope(base)!=this || !owner->effective.initialized())throw Failure("Native direct sampler has no initialized owner");
    const auto fields=Graphics::samplerStateEvidence();
    const auto field=std::find_if(fields.begin(),fields.end(),[id](const auto& f){return f.id==id;});
    if(field==fields.end() || PPC_LOAD_U32(0x82CD2D78+3*id+4)!=field->setterAddress)
        throw Failure("Native direct sampler has no verified original setter");
    auto next=owner->effective;next.setSampler(stage,id,value);owner->effective=next;
    // Direct SDK pass state does not modify the RenderWare/application caches.
}
uint32_t EngineRenderState::pipelineResetField(uint8_t* base,uint32_t index,bool apply) {
    if(&scope(base)!=this || !owner->effective.initialized() || index>=67)
        throw Failure("Invalid native pipeline state reset scope or index");
    // Pinned 823F4618 walks exactly these 67 pairs at 82CD1B70. Its callers
    // retain the original pipeline-mode stack; neither guest state cache changes.
    const uint32_t id=index<34?0x28+4*index:index==34?0xB8:index<45?0xC0+4*(index-35):0xF0+4*(index-45);
    const auto fields=Graphics::scalarStateEvidence();
    const auto field=std::find_if(fields.begin(),fields.end(),[id](const auto& f){return f.id==id;});
    if(field==fields.end()) throw Failure("Native pipeline reset field has no verified scalar implementation");
    const uint32_t value=id==0x2C?6:id==0xE4?0:field->sdkDefault;
    if(PPC_LOAD_U32(0x82CD1B70+8*index)!=id || PPC_LOAD_U32(0x82CD1B74+8*index)!=value ||
       PPC_LOAD_U32(0x82CD28B8+3*id+4)!=field->setterAddress)
        throw Failure("Original pipeline scalar reset table changed");
    auto next=owner->effective;next.setScalar(id,value);
    if(apply) owner->effective=next;
    return field->setterAddress;
}

void EngineRenderState::initialize(EngineCpuCalls& cpu,uint8_t* base) {
    if(&scope(base)!=this || owner->effective.initialized()) throw Failure("Native engine state requires first-start initialization");
    if(PPC_LOAD_U32(0x82D0CAF8)) throw Failure("Console device cannot be used by native state initialization");
    if(PPC_LOAD_U32(0x821DD0D8)!=0 || PPC_LOAD_U32(0x82000BB0)!=0x3F800000)
        throw Failure("Original state initializer float constants changed");
    CacheTransaction transaction;
    auto previous=owner->effective;
    try {
        owner->effective=Graphics::EngineState::fromOriginalStartup();
        cpu.invoke(0x823FFE78); // Original scalar pending/applied/queue reset.
        std::memset(active->pointer(stageApplied,0x420,true),0xFF,0x420);
        for(uint32_t i=0;i<stageSize;++i) {PPC_STORE_U32(stagePending+8*i,0xFFFFFFFF);PPC_STORE_U32(stagePending+8*i+4,0);}
        PPC_STORE_U32(stageCount,0);
        std::memset(active->pointer(samplerCache,0xA00,true),0xFF,0xA00);
        std::memset(active->pointer(0x82E3DC40,0x44,true),0,0x44);
        for(auto word:startupWords) PPC_STORE_U32(word.address,word.value);
        for(auto word:startupScalars) cpu.invoke(0x82400170,word.address,word.value);
        const uint32_t filter=(PPC_LOAD_U32(0x82E3DFE0)&0x200)?1:0;
        for(uint32_t stage=0;stage<8;++stage) {
            // Original texture=null clears are represented by this first-start
            // owner's empty bindings; no console descriptor or reference exists.
            for(auto field:std::array<Word,7>{{{0x14,filter},{0x10,filter},{0x18,2},{0,0},{4,0},{0xC,0},{0x24,1}}})
                setSampler(base,stage,field.address,field.value);
            const std::array<uint32_t,6> values={0,1,1,filter?2u:1u,0,1};
            for(uint32_t i=0;i<6;++i) PPC_STORE_U32(0x82D0E3F8+24*stage+4*i,values[i]);
            if(stage) {cpu.invoke(0x824001E0,stage,1,1);cpu.invoke(0x824001E0,stage,4,1);}
        }
        cpu.invoke(0x824001E0,0,1,3);cpu.invoke(0x824001E0,0,3,0);
        cpu.invoke(0x824001E0,0,4,3);cpu.invoke(0x824001E0,0,6,0);
        commit(cpu,base);
        transaction.publish();
    } catch(...) {owner->effective=previous;throw;}
}

void EngineRenderState::setSampler(uint8_t* base,uint32_t stage,uint32_t id,uint32_t value) {
    if(&scope(base)!=this || !owner->effective.initialized() || stage>=8 || id>0x4C || (id&3))
        throw Failure("Uninitialized native sampler or invalid original cache index");
    const uint32_t address=samplerCache+320*stage+4*id;
    active->pointer(address,4,true);
    if(PPC_LOAD_U32(address)==value) return;
    auto next=owner->effective;
    next.setSampler(stage,id,value); // Validate before guest/host publication.
    PPC_STORE_U32(address,value);
    owner->effective=next;
}

void EngineRenderState::renderWareSampler(PPCContext& ctx,uint8_t* base,uint32_t selector,uint32_t value,bool execute) {
    if(&scope(base)!=this || !owner->effective.initialized() || rwSamplerExecution || currentContext!=&ctx)
        throw Failure("Invalid or nested native RenderWare sampler scope");
    const auto plan=planRwSampler(base,owner->effective,selector,value);
    const uint32_t frame=execute?0x180:0x1F0,sp=ctx.r1.u32;
    if(sp<frame || (sp&15))throw Failure("Invalid original RenderWare sampler stack");
    auto* stack=active->pointer(sp-frame,frame,true);
    if(!execute)return;
    const std::vector<uint8_t> savedStack(stack,stack+frame);
    CacheTransaction transaction;
    const auto previous=owner->effective;
    try {
        EngineCpuCalls cpu(ctx,base);
        RwSamplerExecution run{this,&cpu.registers(),owner->effective,plan,selector,value,cpu.registers().r1.u32};
        struct Guard {
            explicit Guard(RwSamplerExecution& r){rwSamplerExecution=&r;}
            ~Guard(){rwSamplerExecution=nullptr;}
        } guard(run);
        const auto result=cpu.invoke(selector==2?0x82401660:0x824017A0,value);
        if(result!=1 || !run.entered || run.cursor!=plan.writes.size() || cpu.registers().r1.u32!=run.sp)
            throw Failure("Original RenderWare sampler did not complete its CPU schedule");
        ctx.r3.u64=result;
        transaction.publish();
        static thread_local uint32_t logged{};
        if(logged++<8)std::fprintf(stderr,"[NATIVE RW SAMPLER] selector=%u request=%u original stores/native updates=%zu; CPU helper completed\n",
                                  selector,value,run.cursor);
    } catch(...) {
        owner->effective=previous;
        std::memcpy(stack,savedStack.data(),savedStack.size());
        throw;
    }
}

void EngineRenderState::commit(EngineCpuCalls& cpu,uint8_t* base) {
    if(&scope(base)!=this || !owner->effective.initialized()) throw Failure("Native engine state commit precedes initialization");
    uint32_t n=PPC_LOAD_U32(scalarCount),m=PPC_LOAD_U32(stageCount);
    if(n>scalarSize || m>stageSize) throw Failure("Original state dirty queue exceeds its bounded capacity");
    ++owner->commits.calls;owner->commits.empty+=!n&&!m;
    owner->commits.scalarEntries+=n;owner->commits.stageEntries+=m;
    if(!n&&!m) {
        // There are no CPU callbacks, cache-entry writes or host changes. The
        // two original stores still run in order and write the zeros just read.
        // Keep every full-window permission gate; no rollback bytes can differ.
        CacheTransaction::preflight();
        PPC_STORE_U32(scalarCount,0);PPC_STORE_U32(stageCount,0);
        return;
    }
    CacheTransaction transaction;
    auto next=owner->effective;
    std::array<bool,scalarSize> scalars{};
    std::array<bool,stageSize> stages{};
    // Validate the complete scalar transaction before original CPU callbacks.
    for(uint32_t i=0;i<n;++i) {
        uint32_t id=PPC_LOAD_U32(scalarQueue+4*i);
        if(id>=scalarSize || scalars[id] || PPC_LOAD_U32(scalarPending+8*id+4)!=1)
            throw Failure("Invalid or duplicate original scalar dirty entry");
        scalars[id]=true;
        uint32_t value=PPC_LOAD_U32(scalarPending+8*id);
        if(value!=PPC_LOAD_U32(scalarApplied+4*id) && id<0x194) next.setScalar(id,value);
    }
    for(uint32_t i=0;i<m;++i) {
        uint32_t stage=PPC_LOAD_U32(stageQueue+8*i),id=PPC_LOAD_U32(stageQueue+8*i+4);
        if(stage>=8 || id>=33) throw Failure("Invalid original stage dirty index");
        uint32_t index=33*stage+id;
        if(stages[index] || PPC_LOAD_U32(stagePending+8*index+4)!=1) throw Failure("Invalid or duplicate original stage dirty entry");
        stages[index]=true;
        uint32_t value=PPC_LOAD_U32(stagePending+8*index);
        if(value!=PPC_LOAD_U32(stageApplied+4*index) && cpu.invoke(0x8240EBB0,stage,id,value)!=0)
            throw Failure("Original CPU pipeline stage rejected the pending state");
    }
    for(uint32_t i=0;i<n;++i) {
        uint32_t id=PPC_LOAD_U32(scalarQueue+4*i),value=PPC_LOAD_U32(scalarPending+8*id);
        if(value!=PPC_LOAD_U32(scalarApplied+4*id)) {
            PPC_STORE_U32(scalarApplied+4*id,value);
            cpu.invoke(0x823F4670); // Original read-only current pipeline query.
        }
        PPC_STORE_U32(scalarPending+8*id+4,0);
    }
    for(uint32_t i=0;i<m;++i) {
        uint32_t index=33*PPC_LOAD_U32(stageQueue+8*i)+PPC_LOAD_U32(stageQueue+8*i+4);
        PPC_STORE_U32(stageApplied+4*index,PPC_LOAD_U32(stagePending+8*index));
        PPC_STORE_U32(stagePending+8*index+4,0);
    }
    PPC_STORE_U32(scalarCount,0);PPC_STORE_U32(stageCount,0);
    owner->effective=next;
    transaction.publish();
}
}

void SimpsonsNativeStateInitialize(PPCContext& ctx,uint8_t* base) {
    auto& state=scope(base);Simpsons::EngineCpuCalls cpu(ctx,base);state.initialize(cpu,base);
}
void SimpsonsNativeStateCommit(PPCContext& ctx,uint8_t* base) {
    auto& state=scope(base);Simpsons::EngineCpuCalls cpu(ctx,base);state.commit(cpu,base);
}
void SimpsonsNativeSamplerSet(PPCContext& ctx,uint8_t* base) {
    if(rebuildExecution) {rebuildSampler(ctx,base,ctx.r4.u32,false,true);return;}
    scope(base).setSampler(base,ctx.r3.u32,ctx.r4.u32,ctx.r5.u32);
}
void SimpsonsNativeRwAddressEntry(PPCContext& ctx,uint8_t* base) {rwSamplerEntry(ctx,base,2);}
void SimpsonsNativeRwFilterEntry(PPCContext& ctx,uint8_t* base) {rwSamplerEntry(ctx,base,9);}
void SimpsonsNativeRwAddressU(PPCContext& ctx,uint8_t* base) {rwSamplerApply(ctx,base,0,0);}
void SimpsonsNativeRwAddressV(PPCContext& ctx,uint8_t* base) {rwSamplerApply(ctx,base,4,0);}
void SimpsonsNativeRwFilterAnisotropy(PPCContext& ctx,uint8_t* base) {rwSamplerApply(ctx,base,0x24,0x824017E4);}
void SimpsonsNativeRwFilterMin(PPCContext& ctx,uint8_t* base) {rwSamplerApply(ctx,base,0x14,0x82401820);}
void SimpsonsNativeRwFilterMag(PPCContext& ctx,uint8_t* base) {rwSamplerApply(ctx,base,0x10,0x82401834);}
void SimpsonsNativeRwFilterMip(PPCContext& ctx,uint8_t* base) {rwSamplerApply(ctx,base,0x18,0);}
void SimpsonsNativeStateRebuildPreflight(PPCContext& ctx,uint8_t* base) {
    auto& run=requireRebuild(ctx,base);
    if(run.entered || ctx.r1.u32!=run.originalSp) throw Simpsons::Failure("Repeated or malformed RW rebuild entry");
    run.entered=true;
}
void SimpsonsNativeStateRebuildMin(PPCContext& ctx,uint8_t* base) {rebuildSampler(ctx,base,0x14,true);}
void SimpsonsNativeStateRebuildMag(PPCContext& ctx,uint8_t* base) {rebuildSampler(ctx,base,0x10,true);}
void SimpsonsNativeStateRebuildMip(PPCContext& ctx,uint8_t* base) {rebuildSampler(ctx,base,0x18,false);}
void SimpsonsNativeStateRebuildAddressU(PPCContext& ctx,uint8_t* base) {rebuildSampler(ctx,base,0,false);}
void SimpsonsNativeStateRebuildAddressV(PPCContext& ctx,uint8_t* base) {rebuildSampler(ctx,base,4,false);}
