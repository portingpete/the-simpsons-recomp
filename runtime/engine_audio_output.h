#pragma once
#include "runtime.h"
#include <span>

namespace Simpsons {
// Native source ownership at the Dac0 boundary. Original CPU graph construction,
// mixing, event storage and worker execution remain AOT.
class EngineAudioOutput {
    struct State;
    std::unique_ptr<State> state;
public:
    struct View {
        uint32_t owner,root,mixer;
        uint64_t generation;
        uint32_t endpointChannels,endpointMask;
        bool nativeEngine,muted;
        uint32_t identity,event,workerId;
        bool configured,active,released,workerReturned,joined;
        uint64_t submitted,consumed,cancelled,passCallbacks,backendRetired;
    };
    explicit EngineAudioOutput(Runtime&);
    ~EngineAudioOutput();
    void begin(PPCContext&,uint8_t*);
    void acquire(PPCContext&,uint8_t*);
    void source(PPCContext&,uint8_t*);
    void activate(PPCContext&,uint8_t*);
    void workerBegin(PPCContext&,uint8_t*);
    void workerEnd(PPCContext&,uint8_t*);
    void wait(PPCContext&,uint8_t*);
    void capacity(PPCContext&,uint8_t*);
    void submit(PPCContext&,uint8_t*);
    void gain(PPCContext&,uint8_t*,bool);
    void signal(PPCContext&,uint8_t*);
    void release(PPCContext&,uint8_t*);
    void engineRelease(PPCContext&,uint8_t*);
    void startup(PPCContext&,uint8_t*);
    void rootJoin(PPCContext&,uint8_t*);
    void rootFinish(PPCContext&,uint8_t*);
    void mixerReady(PPCContext&,uint8_t*);
    void mixerEnd(PPCContext&,uint8_t*);
    struct PcmLease {
        std::unique_lock<std::mutex> lock;
        std::span<uint8_t> bytes;
    };
    // Held only across native publication/commit, never across original calls.
    PcmLease pcm(PPCContext&,uint8_t*,uint32_t root,uint32_t descriptor,uint32_t channels);
    View view() const;
};
}
