#pragma once
#include "runtime.h"
#include <optional>

namespace Simpsons {
// EXm0 ownership and qualified streamed NativeRawF32 decoding. No native
// identity is stored where original code expects an SDK hardware record.
class EngineAudioOwners {
    struct State;
    std::unique_ptr<State> state;
public:
    struct View {uint64_t generation;uint32_t channels,layers;bool enabled;};
    struct ResidentBankView {
        uint32_t resource,handle,allocation,extent,header,source;
        uint64_t generation,sourceGeneration;
    };
    struct AllocationSpan {
        uint32_t address,extent;
        uint64_t generation;
        bool operator==(const AllocationSpan&) const=default;
    };
    explicit EngineAudioOwners(Runtime&);
    ~EngineAudioOwners();
    void start(PPCContext&,uint8_t*);
    void stop(PPCContext&,uint8_t*);
    void enable(PPCContext&,uint8_t*,bool);
    void createBegin(PPCContext&,uint8_t*);
    void createAllocated(PPCContext&,uint8_t*);
    void construct(PPCContext&,uint8_t*);
    void createEnd(PPCContext&,uint8_t*);
    void destroyBegin(PPCContext&,uint8_t*);
    void release(PPCContext&,uint8_t*);
    void destroyEnd(PPCContext&,uint8_t*);
    void observeStream(uint32_t,PPCContext&,uint8_t*);
    void observeHeap(uint32_t,PPCContext&,uint8_t*);
    void observeBank(uint32_t,PPCContext&,uint8_t*);
    void producerBegin(PPCContext&,uint8_t*);
    void producerEnd(PPCContext&,uint8_t*);
    void inputPreflight(PPCContext&,uint8_t*);
    void input(PPCContext&,uint8_t*);
    void decode(PPCContext&,uint8_t*);
    void advanceBegin(PPCContext&,uint8_t*);
    void advanceEnd(PPCContext&,uint8_t*,bool completion);
    void sourceFreeBegin(uint32_t,PPCContext&,uint8_t*);
    void sourceFreeEnd(uint32_t,PPCContext&,uint8_t*);
    void readerRelease(uint32_t,PPCContext&,uint8_t*);
    size_t count() const;
    uint32_t reservedLayers() const;
    bool ready() const;
    View view(uint32_t owner) const;
    uint64_t allocationGeneration(uint32_t address,uint32_t bytes) const;
    // Locate an observed owner by the start byte before validating a borrowed
    // interior span. Absence never means a requested span overflow is valid.
    std::optional<AllocationSpan> allocationSpan(uint32_t address) const;
    ResidentBankView residentBank(uint32_t resource) const;
    std::weak_ptr<const void> lease(uint32_t owner,uint64_t generation) const;
};
}
