#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace Simpsons::Audio {
class OutputError : public std::runtime_error {
    int32_t status_;
public:
    OutputError(const char* operation,int32_t status);
    int32_t status() const noexcept { return status_; }
};

// A real XAudio2 graph. Creation requires an initialized MTA; the caller keeps
// that apartment alive through stop(). This class never owns/uninitializes COM.
// Public operations may run concurrently with stop(), while the C++ owner stays
// alive. No operation may be called from an XAudio2 callback.
class NativeAudioOutput {
    struct State;
    std::unique_ptr<State> state;
    friend struct NativeAudioOutputTestAccess;
public:
    static constexpr uint32_t channels=6, sampleRate=48000, blockFrames=256;
    static constexpr uint32_t blockSamples=channels*blockFrames, maxBuffers=64;
    struct Options {
        uint32_t capacity=2; // Includes completed receipts until explicitly retired.
        bool muted=true;    // Immutable for this graph; every automated test uses true.
        // Empty uses the Windows virtual client and follows the default device.
        // An explicit ID pins that endpoint; failures remain terminal.
        std::wstring deviceId;
    };
    struct Endpoint {uint32_t channels,sampleRate,channelMask;bool muted;};
    struct Routing {
        uint32_t sourceChannelMask=0; // Exactly six Windows speaker bits.
        uint32_t destinationChannelMask=0; // Must equal endpoint().channelMask.
        // XAudio2 indexing: matrix[6*destinationChannel + sourceChannel].
        std::vector<float> matrix;
    };
    struct Receipt {
        uint64_t generation=0, sequence=0;
        bool operator==(const Receipt&) const = default;
    };
    enum class SubmitStatus {Accepted,Backpressure};
    struct Submission {SubmitStatus status;Receipt receipt;};
    // Consumed means a natural OnBufferEnd, not audible/device-clock completion.
    enum class BufferStatus {Pending,Consumed,Cancelled,Failed};
    struct Completion {Receipt receipt;BufferStatus status;int32_t error;};
    enum class ErrorOrigin:uint32_t {None,WakeEvent,BackendCall,VoiceCallback,EngineCallback,CallbackContract};
    struct Snapshot {
        uint64_t generation;
        bool configured,running,stopped;
        int32_t error; // First asynchronous/synchronous backend error; zero if none.
        uint32_t capacity,owned,pending;
        std::vector<Completion> completed; // Safe to retire; retained until retire().
        ErrorOrigin errorOrigin=ErrorOrigin::None; // Atomically paired with the first HRESULT.
    };
    explicit NativeAudioOutput(const Options&);
    ~NativeAudioOutput();
    NativeAudioOutput(const NativeAudioOutput&)=delete;
    NativeAudioOutput& operator=(const NativeAudioOutput&)=delete;
    Endpoint endpoint() const;
    uint64_t generation() const noexcept;
    void configure(const Routing&); // Once; leaves the source stopped, no implicit routing.
    // Alternative once-only native Windows policy: component indices 0..5 are
    // FL, FR, FC, LFE, SL, SR (mask 0x60F). Uses XAudio2's default endpoint
    // matrix, retained by routing(); no source permutation or SetOutputMatrix.
    // This is an explicit PC adaptation, not recovered console speaker labels.
    // Source remains stopped. Any native creation/readback failure closes graph.
    void configureWindows51();
    Routing routing() const; // Configuration was read back from the actual source voice.
    void start();
    // Exactly 256 interleaved frames, finite normalized float32 [-1,1]. Copies
    // input before acceptance. Backpressure consumes nothing and returns no ID.
    Submission submit(std::span<const float>);
    Completion query(Receipt) const;
    void retire(Receipt); // Pending/stale/foreign receipts reject. Safe after stop().
    // One worker consumes wake hints via poll()/wait(); producers and stop may
    // be concurrent. poll resets the event BEFORE scanning atomics, avoiding a
    // lost completion. Always inspect error/stopped as well as completed.
    Snapshot poll();
    // Borrowed Windows HANDLE for WaitForMultipleObjects with caller cancellation.
    // Signaled on engine processing passes even with no PCM, completions/errors,
    // and stop. Do not close/reset it; keep this owner alive through every wait.
    void* wakeHandle() const noexcept;
    bool wait(uint32_t timeoutMs) const; // 0..60000, true=signaled, false=timeout.
    // Irreversible, idempotent. DestroyVoice drains callbacks/data reads before
    // pending storage becomes Cancelled/Failed. Reopen with a new owner/generation.
    void stop() noexcept;
};

#ifdef SIMPSONS_AUDIO_OUTPUT_TESTS
// Standalone fixture only; absent from production compilation. Error delivery
// calls the SAME callback handlers and is explicitly injection, not device loss.
struct NativeAudioOutputTestAccess {
    static std::vector<float> copied(const NativeAudioOutput&,NativeAudioOutput::Receipt);
    static void voiceError(NativeAudioOutput&,int32_t);
    static void criticalError(NativeAudioOutput&,int32_t);
    static uint32_t masteringFlags(const NativeAudioOutput&);
};
#endif
}
