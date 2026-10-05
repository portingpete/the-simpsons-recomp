#pragma once
#include "renderer/engine_state.h"
#include <memory>
struct PPCContext;

namespace Simpsons {
class EngineCpuCalls;
// Scoped engine service owner. The first-start initializer and later cache
// commits have checked CPU effects; no SDK object or command stream is exposed.
class EngineRenderState {
public:
    struct CommitCounts {uint64_t calls{},empty{},scalarEntries{},stageEntries{};};
    CommitCounts commitCounts() const noexcept;
    EngineRenderState();
    ~EngineRenderState();
    EngineRenderState(const EngineRenderState&)=delete;
    EngineRenderState& operator=(const EngineRenderState&)=delete;
    void initialize(EngineCpuCalls& cpu,uint8_t* base);
    // Replay 82400D50 using retained RW sources, never startup defaults. The
    // caller owns the enclosing binding-reset scope and delayed GPU unbinding.
    // Preflight is read-only; rebuild rolls back CPU caches/effective state and
    // its callback context on any failure before returning to the caller.
    void preflightRebuild(EngineCpuCalls& cpu,uint8_t* base) const;
    void rebuild(EngineCpuCalls& cpu,uint8_t* base);
    void commit(EngineCpuCalls& cpu,uint8_t* base);
    void setSampler(uint8_t* base,uint32_t stage,uint32_t id,uint32_t value);
    void renderWareSampler(PPCContext&,uint8_t* base,uint32_t selector,uint32_t value,bool execute);
    uint32_t applicationScalar(uint8_t* base,uint32_t application,uint32_t selector,
                               uint32_t value,bool apply);
    uint32_t applicationSampler(uint8_t* base,uint32_t application,uint32_t stage,
                                uint32_t selector,uint32_t value,bool apply);
    const Graphics::EngineState& effective() const;
    void beginRecording(uint32_t context);
    void requireRecordingSeed(uint32_t context) const;
    void endRecording(uint32_t context);
    uint32_t recordingContext() const noexcept;
    // Publish the already validated direct screen post-state; no RW/application caches.
    void publishScreenState(const Graphics::EngineState&) noexcept;
    uint32_t pipelineResetField(uint8_t* base,uint32_t index,bool apply);
    void directScalar(uint8_t* base,uint32_t id,uint32_t value);
    void directSampler(uint8_t* base,uint32_t stage,uint32_t id,uint32_t value);
private:
    struct Owner;
    std::unique_ptr<Owner> owner;
};
}
