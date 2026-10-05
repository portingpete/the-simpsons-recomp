#include "engine_pipeline_resources.h"
#include "runtime.h"
#include "renderer/native_backend.h"
#include <atomic>
#include <cstdio>

namespace {
constexpr uint32_t cursor=0x82D507EC,indexField=0x82D507F0;
enum class Phase {Allocated,Created,Released};
struct PipelineOwner {
    Simpsons::Runtime* runtime{};
    std::shared_ptr<Simpsons::Graphics::Buffer> buffer;
    uint32_t id{},thread{};
    Phase phase=Phase::Allocated;
};
thread_local PipelineOwner* currentPipeline{};
void auditPipeline(const PPCContext& ctx,uint8_t* base,bool release) noexcept {
    auto* runtime=Simpsons::active;
    if(!runtime||!runtime->resourceAudit.active())return;
    const auto fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
    try {
        char caps[32]="unreadable",r3Summary[32],parameters[256],ownership[192],instance[192];
        const auto* owner=currentPipeline;
        if(base==runtime->base)try {
            const auto* p=runtime->pointer(0x82E3DFBC,4,false);
            const auto value=(uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
            std::snprintf(caps,sizeof(caps),"%08X",value);
        }catch(...){}
        if(release)std::snprintf(r3Summary,sizeof(r3Summary),"%s",owner&&ctx.r3.u32==owner->id?"owner-match":"owner-mismatch");
        else std::snprintf(r3Summary,sizeof(r3Summary),"bytes:%08X",ctx.r3.u32);
        if(release)std::snprintf(parameters,sizeof(parameters),
            "operation=release producer=82416BC8 callsite=82416BEC r3=%s r31=%08X caps=%s",
            r3Summary,ctx.r31.u32,caps);
        else std::snprintf(parameters,sizeof(parameters),
            "operation=create producer=82416C58 callsite=82416CA0 r3=%s r4=%08X r5=%08X r6=%08X r27=%08X r31=%08X caps=%s",
            r3Summary,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,ctx.r27.u32,ctx.r31.u32,caps);
        std::snprintf(ownership,sizeof(ownership),"scope=%s runtime=%s thread=%s context=%s base=%s phase=%u backing=%u",
            owner?"present":"absent",owner&&owner->runtime==runtime?"match":"mismatch",
            owner&&owner->thread==GetCurrentThreadId()?"match":"mismatch",
            Simpsons::currentContext==&ctx?"match":"mismatch",base==runtime->base?"match":"mismatch",
            owner?unsigned(owner->phase):~0u,owner&&owner->buffer?1u:0u);
        std::snprintf(instance,sizeof(instance),"native_id=%08X r3_raw=%08X r4_raw=%08X r5_raw=%08X r6_raw=%08X r27_raw=%08X",
            owner?owner->id:0u,ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,ctx.r27.u32);
        runtime->resourceAudit.observe("pipeline_index","global:82D507F0",uint32_t(ctx.lr),parameters,ownership,0,instance);
    }catch(...){}
    PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);
}
// Axxxxx is disjoint from current declaration B, scratch C, dynamic D and
// material E identities. Never recycled; guest mapping collision is an error.
std::atomic<uint32_t> nextId{0x00A00001};
uint32_t reserveId(Simpsons::Runtime& runtime) {
    uint32_t value=nextId.load();
    while(value<0x00B00000) if(nextId.compare_exchange_weak(value,value+1)) {
        if(runtime.pageAccess[value>>12].load()) throw Simpsons::Failure("Native pipeline index ID overlaps mapped guest memory");
        return value;
    }
    throw Simpsons::Failure("Native pipeline index identity space exhausted");
}
void requireOwner(const PipelineOwner& owner) {
    if(currentPipeline!=&owner || owner.thread!=GetCurrentThreadId() || Simpsons::active!=owner.runtime)
        throw Simpsons::Failure("Native pipeline index accessed outside its runtime/thread owner scope");
    owner.runtime->checkRunning();
}
PipelineOwner& requireHook(PPCContext& ctx,uint8_t* base) {
    if(!currentPipeline) throw Simpsons::Failure("Pipeline index hook reached without a native owner");
    requireOwner(*currentPipeline);
    if(base!=currentPipeline->runtime->base || Simpsons::currentContext!=&ctx)
        throw Simpsons::Failure("Pipeline index hook has an invalid base or original callback context");
    return *currentPipeline;
}
}

namespace Simpsons {
struct EnginePipelineResources::Owner : PipelineOwner {};
EnginePipelineResources::EnginePipelineResources(Graphics::NativeBackend& backend):owner_(std::make_unique<Owner>()) {
    if(currentPipeline) throw Failure("Nested native pipeline index owner is unsupported");
    if(!active) throw Failure("Native pipeline index owner requires an active runtime");
    active->checkRunning();
    owner_->runtime=active;owner_->thread=GetCurrentThreadId();
    // Backend enforces its owner thread and native allocation failure. Publish
    // the TLS scope only after real resource creation succeeds.
    owner_->buffer=backend.createBuffer(0x1FFFE,Graphics::BufferKind::Index16);
    if(!owner_->buffer || owner_->buffer->type()!=Graphics::BufferKind::Index16 || owner_->buffer->byteSize()!=0x1FFFE)
        throw Failure("Native pipeline index allocation does not match original backing");
    active->checkRunning();
    currentPipeline=owner_.get();
}
EnginePipelineResources::~EnginePipelineResources() {
    if(owner_->phase==Phase::Created)
        std::fprintf(stderr,"[NATIVE ENGINE] pipeline index scope ended before original cleanup; native backing released, guest owner cleanup incomplete\n");
    if(currentPipeline==owner_.get()) currentPipeline=nullptr;
}
void EnginePipelineResources::validateCreated(uint32_t originalIndexField) const {
    requireOwner(*owner_);
    if(owner_->phase!=Phase::Created || !owner_->buffer || originalIndexField!=owner_->id ||
       PPCLoadU32(owner_->runtime->base,indexField)!=owner_->id)
        throw Failure("Original pipeline index field does not identify its native buffer");
}
void EnginePipelineResources::requireReleased() const {
    requireOwner(*owner_);
    if(owner_->phase!=Phase::Released || owner_->buffer || owner_->id || PPCLoadU32(owner_->runtime->base,indexField))
        throw Failure("Original pipeline index cleanup has not completed");
}
void EnginePipelineResources::requireUnowned() const {
    requireOwner(*owner_);
    if(owner_->phase==Phase::Created || owner_->id ||
       (owner_->phase==Phase::Released && owner_->buffer) || PPCLoadU32(owner_->runtime->base,indexField))
        throw Failure("Pipeline index still has published ownership");
}
std::shared_ptr<Graphics::Buffer> EnginePipelineResources::index(uint32_t nativeId) const {
    validateCreated(nativeId);
    return owner_->buffer;
}
}

void SimpsonsNativePipelineIndexCreate(PPCContext& ctx,uint8_t* base) {
    auditPipeline(ctx,base,false);
    auto& owner=requireHook(ctx,base);
    // Original82416C80..8C derives this unused SDK argument from cap bit16.
    // The SDK82441A08 retains only size/flags/usage, and its allocation and
    // paired cleanup are identical in both branches. Keep the caller's exact
    // correlation rather than admitting arbitrary fourth-argument values.
    const auto originalMode=(PPC_LOAD_U32(0x82E3DFBC)&0x10000u)?0u:2u;
    if(owner.phase!=Phase::Allocated || !owner.buffer || ctx.r3.u32!=0x1FFFE || ctx.r4.u32!=8 ||
       ctx.r5.u32!=1 || ctx.r6.u32!=originalMode || ctx.r27.u32!=0x82D507FC)
        throw Simpsons::Failure("Unsupported pipeline index allocation arguments or repeated initialization");
    owner.runtime->pointer(cursor,20,true); // Original following stores must be valid.
    for(uint32_t at=cursor;at<cursor+20;at+=4)
        if(PPC_LOAD_U32(at)) throw Simpsons::Failure("Pipeline initializer encountered existing cursor/resource ownership");
    const auto id=reserveId(*owner.runtime);
    owner.id=id;owner.phase=Phase::Created;
    ctx.r3.u32=id;ctx.lr=0x82416CA4;
}
void SimpsonsNativePipelineIndexRelease(PPCContext& ctx,uint8_t* base) {
    auditPipeline(ctx,base,true);
    auto& owner=requireHook(ctx,base);
    if(owner.phase!=Phase::Created || !owner.buffer || ctx.r3.u32!=owner.id || ctx.r31.u32!=0x82D507F4 ||
       PPC_LOAD_U32(indexField)!=owner.id)
        throw Simpsons::Failure("Unknown, stale or mismatched pipeline index release");
    owner.runtime->pointer(indexField,4,true); // Original clear follows the hook.
    owner.buffer.reset();owner.id=0;owner.phase=Phase::Released;
    ctx.r3.u32=0;ctx.lr=0x82416BF0;
}
