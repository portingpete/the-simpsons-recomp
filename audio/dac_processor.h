#pragma once
#include "native_audio_output.h"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Simpsons::Audio {
// One bounded source DSP owner, not an SDK voice. The caller retains the
// configured, stopped, empty capacity4 backend and its MTA until stop/destruction.
// This owner exclusively submits/polls/retires it; external stop is detected as
// failure. All public methods are thread-safe while this C++ owner remains alive.
// No method/destruction may run on the private processing thread or XA callbacks.
class DacProcessor {
    struct State;
    std::unique_ptr<State> state;
    friend struct DacProcessorTestAccess;
public:
    static constexpr uint32_t sourceCapacity=2,downstreamCapacity=4;
    struct Receipt {
        uint64_t generation=0,sequence=0;
        bool operator==(const Receipt&) const = default;
    };
    enum class SubmitStatus {Accepted,Backpressure};
    struct Submission {SubmitStatus status;Receipt receipt;};
    enum class SourceStatus {Queued,Processing,Consumed,Cancelled,Failed};
    enum class FailureStage {None,WorkerStart,BackendState,Dsp,Submit,Retire,Wait,Wake,Stop};
    struct Failure {
        FailureStage stage=FailureStage::None;
        int32_t error=0;
        Receipt source;
        NativeAudioOutput::Receipt downstream;
        std::string operation;
        NativeAudioOutput::ErrorOrigin backendOrigin=NativeAudioOutput::ErrorOrigin::None;
    };
    struct SourceCompletion {
        Receipt receipt;
        SourceStatus status=SourceStatus::Queued;
        int32_t error=0;
        uint32_t processedFrames=0; // DSP alone is insufficient for Consumed.
        NativeAudioOutput::Receipt downstream; // Nonzero only after real acceptance.
        float initialGain=0,targetGain=0,finalGain=0;
    };
    struct DownstreamCompletion {
        Receipt source;
        NativeAudioOutput::Completion completion;
        bool retired=false; // True only after the backend's real terminal receipt is retired.
    };
    struct Snapshot {
        uint64_t generation=0,backendGeneration=0;
        bool active=false,stopping=false,stopped=false,workerExited=false;
        uint32_t owned=0,queued=0,processing=0;
        float current=0,target=1;
        uint64_t processingHints=0,starvationPasses=0,backendRetired=0;
        Failure failure; // First terminal failure, with operation and receipt provenance.
        std::vector<SourceCompletion> sources; // All owned, including terminal until retire.
        std::vector<DownstreamCompletion> downstream;
    };
    explicit DacProcessor(NativeAudioOutput&);
    ~DacProcessor();
    DacProcessor(const DacProcessor&)=delete;
    DacProcessor& operator=(const DacProcessor&)=delete;
    uint64_t generation() const noexcept;
    // Exactly256 interleaved six-component frames, finite normalized [-1,1].
    // Copies raw values; no gain is baked at admission. Capacity includes terminal
    // source receipts until retire. Rejected/backpressured requests consume nothing.
    Submission submit(std::span<const float>);
    void setTargetGain(float); // Finite [0,1], including signed zero; no current snap.
    void activate(); // Once: seeds current=target and actually calls backend.start().
    SourceCompletion query(Receipt) const;
    void retire(Receipt); // Queued/processing/stale/foreign receipts reject.
    // Sole observer resets its manual event before taking a coherent snapshot.
    // Terminal downstream records are acknowledged by poll, freeing one of four
    // bounded provenance slots. Unobserved completions apply backpressure; actual
    // backend retirement happens on real completion, independently of this ack.
    Snapshot poll();
    void* wakeHandle() const noexcept; // Borrowed manual-reset HANDLE, never close/reset.
    bool wait(uint32_t timeoutMs) const; //0..60000; always inspect poll's failure/stopped.
    // Irreversible, idempotent; cancels pending raw source work, drains downstream
    // callbacks/storage via backend.stop(), and joins. Never call from the private
    // processing thread. Safe from a separate original/native Dac worker.
    void stop();
};

#ifdef SIMPSONS_DAC_PROCESSOR_TESTS
// Isolated test build only. Gates pause CPU work, never manufacture acceptance,
// processing completion or XAudio2 callbacks. Stop interrupts either gate.
struct DacProcessorTestAccess {
    enum class Gate {BeforeDsp,BeforeSubmit};
    static void hold(DacProcessor&,Gate,bool);
    static std::vector<float> raw(const DacProcessor&,DacProcessor::Receipt);
    static std::vector<float> processed(const DacProcessor&,DacProcessor::Receipt);
    static std::vector<float> submitted(const DacProcessor&,DacProcessor::Receipt);
    static bool workerFPRestored(const DacProcessor&);
};
#endif
}
