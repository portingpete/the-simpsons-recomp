#pragma once
#include "native_xma_codec.h"
#include <array>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace Simpsons::Audio {
struct XmaSourceError : std::runtime_error { using std::runtime_error::runtime_error; };
class FreshXmaCache;

// NativeRawF32 only: finite decoder values, unchanged logical component order.
// No guest memory, implied skip, container parsing/padding, EOF or audio device.
// Methods serialize internally. The caller must serialize guest publication +
// commit against close/destruction; this class cannot transact external writes.
class XmaSource {
    friend class FreshXmaCache;
    struct State;
    std::unique_ptr<State> state;
public:
    using Packet = std::array<uint8_t, NativeXmaCodec::packetBytes>;
    struct Limits {
        uint32_t maxSources = 20;             // Includes delivered until retire.
        uint32_t maxTickets = 2;              // Includes externally held tickets.
        uint32_t maxQuotaFrames = 4096;
        uint32_t maxBufferedFramesPerLayer = 65536;
        uint32_t maxDeclaredFrames = 262144;
        uint64_t maxCompressedBytes = 1024 * 1024;
        // Zero (the default) decodes every supplied packet in prepare(). Otherwise
        // prepare() decodes packets until each layer buffers this many frames past
        // its skip, and stage() decodes further packets of its source, in order, when
        // a quota needs them. Decoded samples are identical; only the time at which
        // the codec runs changes (a long resident sound no longer decodes seconds of
        // audio inside one original callback).
        uint32_t prepareFrames = 0;
    };
    enum class Continuity { Unspecified, FreshContext, ContinueContext };
    struct LayerInput {
        uint32_t skipFrames = 0; // Exact caller-supplied pending skip, never inferred.
        std::vector<Packet> packets;
    };
    struct Segment {
        uint64_t sequence = 0; // Strictly increasing, nonzero, never UINT64_MAX.
        uint32_t slot = 0;     // Opaque caller slot; unique among owned receipts.
        Continuity continuity = Continuity::Unspecified; // Must be supplied explicitly.
        uint32_t declaredFrames = 0;
        uint32_t initialConsumedFrames = 0; // Accounting only; does not skip PCM.
        std::vector<LayerInput> layers;
    };
    struct Receipt {
        uint64_t owner = 0, instanceGeneration = 0, sequence = 0;
        bool operator==(const Receipt&) const = default;
    };
    enum class SourceStatus { Prepared, Delivered, Failed, Cancelled };
    struct SourceView {
        Receipt receipt;
        uint32_t slot = 0, declaredFrames = 0, consumedFrames = 0;
        SourceStatus status = SourceStatus::Prepared;
        uint64_t copiedBytes = 0;
    };
    class Quota {
        friend struct State;
        friend class XmaSource;
        Receipt source_;
        uint64_t identity_ = 0, epoch_ = 0;
        uint32_t before_ = 0, frames_ = 0, channels_ = 0, layers_ = 0;
        std::vector<float> samples_; // Component-major, frames_ floats per plane.
        std::array<uint64_t, 3> rawStarts_{};
        std::array<uint32_t, 3> skips_{};
        Quota() = default;
    public:
        Quota(const Quota&) = delete;
        Quota& operator=(const Quota&) = delete;
        Receipt source() const noexcept { return source_; }
        uint64_t identity() const noexcept { return identity_; }
        uint64_t epoch() const noexcept { return epoch_; }
        uint32_t progressBefore() const noexcept { return before_; }
        uint32_t frames() const noexcept { return frames_; }
        uint32_t channels() const noexcept { return channels_; }
        std::span<const float> plane(uint32_t component) const;
        uint64_t rawStartFrame(uint32_t layer) const;
        uint32_t skippedFrames(uint32_t layer) const;
    };
    using Ticket = std::shared_ptr<const Quota>;
    enum class PrepareStatus { Accepted, Backpressure };
    struct Preparation { PrepareStatus status; Receipt receipt; };
    enum class StageStatus { Complete, NeedInput, Backpressure };
    struct Staging { StageStatus status; Ticket ticket; };
    enum class FailureStage { None, Reset, Decode, Storage, Counter };
    struct Failure {
        FailureStage stage = FailureStage::None;
        Receipt source;
        uint32_t layer = 0;
        std::exception_ptr cause; // Original exception; retained without formatting.
    };
    struct LayerView {
        uint32_t channels = 0, bufferedFrames = 0;
        uint64_t rawHeadFrame = 0, acceptedPackets = 0, decodedFrames = 0;
        uint64_t sendBackpressure = 0, discardedOnFresh = 0;
    };
    struct Snapshot {
        uint64_t owner = 0, instanceGeneration = 0, epoch = 0;
        uint64_t copiedBytes = 0, committedFrames = 0, pendingTicket = 0;
        uint32_t ticketsAlive = 0;
        bool closed = false, configured = false;
        Failure failure;
        std::vector<SourceView> sources;
        std::vector<LayerView> layers;
        // When fresh PCM is shared, counters retain their actual decoder's
        // provenance. Source receipts and consumption remain local to this owner.
        Receipt sharedFreshDecode;
    };

private:
    class FreshImage {
        friend class XmaSource;
        friend class FreshXmaCache;
        friend struct State;
        std::vector<XmaFormat> formats;
        std::vector<std::vector<float>> raw;
        std::vector<LayerView> views;
        std::vector<LayerInput> input;
        Receipt origin;
        Limits limits;
        uint32_t declaredFrames{}, initialConsumedFrames{};
        uint64_t memoryBytes{};
    };
    XmaSource(uint64_t instanceGeneration,std::shared_ptr<const FreshImage>,std::span<const XmaFormat>,const Segment&,Limits);
    uint64_t freshImageBytes() const;
    bool decodedAllInput() const; // Every supplied packet has reached the codec.
    // Sends up to `packets` more packets per layer of the oldest incompletely decoded
    // source (in order) and drains them. Returns true while packets remain. Lets another
    // thread finish an incremental decode; a failure is terminal as in prepare().
    bool decodeAhead(uint32_t packets);
    // An incrementally decoded fresh source whose codec has now seen every packet of
    // `input`: a copy of the image its eager decode would have shared at prepare().
    // Null while packets remain, after close/failure, or if its ring wrapped.
    std::shared_ptr<const FreshImage> completedFreshImage(const std::vector<LayerInput>& input,Receipt origin,
                                                          uint32_t declaredFrames,uint32_t initialConsumedFrames) const;
    std::shared_ptr<const FreshImage> shareFreshImage();
public:

    // Factory is borrowed only during construction; all layer codecs are owned.
    // 1..3 mono/stereo layers, common rate/variant, at most six components.
    XmaSource(NativeXmaFactory&, uint64_t instanceGeneration,
              std::span<const XmaFormat> formats, Limits);
    ~XmaSource();
    XmaSource(const XmaSource&) = delete;
    XmaSource& operator=(const XmaSource&) = delete;

    // Copies packets before codec use. Validated resource admission is atomic;
    // after codec mutation, any error is terminal and visible in snapshot().
    // Accepted means all supplied packets were sent/drained, NOT logical frames
    // consumed. Empty packet lists are pending input, never EOF. Continue retains
    // codecs/partial bitstreams/raw surplus. Fresh requires all receipts retired
    // and explicitly resets/discards prior raw surplus; there is no implicit reset.
    Preparation prepare(const Segment&);
    // Only the oldest not-delivered source can stage. Positive quota bounded by
    // remaining frames/limits; no short success. Repeating a pending request
    // returns the same immutable ticket. No raw/skip/logical cursor advances here.
    Staging stage(Receipt, uint32_t quotaFrames);
    void validateCommit(const Ticket&) const;
    // Successful commit is nonallocating and makes no codec calls. Callers must
    // coordinate external writes; validateCommit alone does not reserve a lock.
    void commit(const Ticket&);
    SourceView query(Receipt) const;
    void retire(Receipt); // Delivered/Failed/Cancelled only; never frees guest data.
    Snapshot snapshot() const;
    void close(); // Idempotent terminal release; retained ticket data stays valid.
};
}
