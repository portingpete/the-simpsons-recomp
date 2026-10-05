#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
struct PPCContext;

namespace Simpsons {
class Runtime;
namespace Graphics {class NativeBackend;class NativeRecordingContext;class NativeRecordingPayload;}

// Retains original CPU ownership and recording-begin control flow. Native
// deferred payloads have independent host ownership; runtime finish and replay
// still require their own qualified original call paths.
class EngineRecordingOwners {
public:
    EngineRecordingOwners(Runtime&,Graphics::NativeBackend&);
    ~EngineRecordingOwners();
    EngineRecordingOwners(const EngineRecordingOwners&)=delete;
    EngineRecordingOwners& operator=(const EngineRecordingOwners&)=delete;
    void createBegin(PPCContext&,uint8_t* base);  // 826F4988, continue original body
    void createCommit(PPCContext&,uint8_t* base); // replace BL 826F4AD0, resume 4AD4
    void destroyBegin(PPCContext&,uint8_t* base); // 826F3908, continue original body
    void destroyCommit(PPCContext&,uint8_t* base);// 826F396C, continue original store
    void beginRecording(PPCContext&,uint8_t* base); // Retain original826F4D08 body.
    void beginOperation(PPCContext&,uint8_t* base,uint32_t site);
    void requireActiveContext(uint32_t identity) const;
    std::shared_ptr<Graphics::NativeRecordingPayload> activePayload(uint32_t contextIdentity) const;
    void finishRecording(PPCContext&,uint8_t* base);
    void finishOperation(PPCContext&,uint8_t* base,uint32_t site);
    void requireFinishedRecording(uint32_t typed,uint32_t previous) const;
    void completeEffectRestore(uint32_t typed,uint32_t previous);
    void requireReplay(uint32_t packet,uint32_t payloadId) const;
    void beginReplay(PPCContext&,uint8_t* base);
    void executeReplay(PPCContext&,uint8_t* base);
    void pluginDestroyBegin(PPCContext&,uint8_t* base);
    void pluginDestroyComplete(PPCContext&,uint8_t* base);
    void deleteRecord(PPCContext&,uint8_t* base,uint32_t site);
    // Observation only: retain every original instruction at these sites.
    void observeLruTouch(PPCContext&,uint8_t* base,uint32_t site);
    void observeCounterReset(PPCContext&,uint8_t* base,uint32_t site);
    void observeQuotaReturn(PPCContext&,uint8_t* base); // 826F4D50
    void observeIdleContextPublication(PPCContext&,uint8_t* base); // After original826FF6F0 store.
    void requireEmpty() const;
    size_t count() const; // Includes prepared or partially destroyed ownership.
    uint32_t identity(uint32_t owner) const;
    void validateOwner(uint32_t owner,uint32_t identity) const;
    // Once-per-frame full recording validation (called from present, after all
    // scene dispatches for the frame, before front copies). Per-mutation paths
    // retain targeted safety checks + idleHistory (cheap) and defer expensive
    // full graph validation here (1910 free slots + 90 nodes + receipts).
    // Preserves visuals (rendering uses payloads, not LRU/free/head/accounting
    // which are validation-only) and fail-stops corrupted frames before display.
    void validateFrame(uint8_t* base) const;
    // Opaque, weak diagnostic access. No recording API or SDK object is exposed.
    std::weak_ptr<Graphics::NativeRecordingContext> nativeContext(uint32_t owner,uint32_t identity) const;
private:
    struct State;
    std::unique_ptr<State> state;
};
}
