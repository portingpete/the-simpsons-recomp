#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace Simpsons::Audio {
struct CodecError:std::runtime_error {using std::runtime_error::runtime_error;};
enum class XmaVariant {Xma1,Xma2};
struct XmaFormat {
    uint32_t channels;
    uint32_t sampleRate;
    XmaVariant variant;
};
enum class PacketResult {Accepted,NeedDrain};
class NativeXmaCodec;
class NativeXmaCodecPool;
// Owns real unconfigured backend contexts for capability/option validation.
// A guest factory can exist before source data supplies a stream format.
class NativeXmaFactory {
    struct State;
    std::unique_ptr<State> state;
public:
    NativeXmaFactory();
    ~NativeXmaFactory();
    NativeXmaFactory(const NativeXmaFactory&)=delete;
    NativeXmaFactory& operator=(const NativeXmaFactory&)=delete;
    // A pooled (reset) decoder when one of this format is idle, else a newly opened one.
    std::unique_ptr<NativeXmaCodec> create(XmaFormat) const;
    // Shared with sources so they can return decoders after this factory is gone.
    std::shared_ptr<NativeXmaCodecPool> pool() const;
};

// Reset decoders and decoded-sample rings kept for reuse. Opening an FFmpeg XMA
// decoder costs about 1.4 ms and a fresh 1 MiB ring about 0.5 ms more. A looping
// stream builds a fresh source (one decoder per stereo layer) at every loop, on the
// audio thread inside the original callback lock that the game's main thread also
// takes: a six-channel ambience stalled 120 FPS frames by 5.5 ms every 1.9 s.
// reset() is the decoder's explicit start-of-stream state (NativeXmaCodecTests
// decode the pinned oracle after reset), so a pooled decoder behaves like a newly
// opened one. A reused ring keeps stale samples, which sources never read: they
// only read frames they decoded since taking it.
class NativeXmaCodecPool {
    struct State;
    std::unique_ptr<State> state;
public:
    explicit NativeXmaCodecPool(size_t idlePerFormat=32);
    ~NativeXmaCodecPool();
    NativeXmaCodecPool(const NativeXmaCodecPool&)=delete;
    NativeXmaCodecPool& operator=(const NativeXmaCodecPool&)=delete;
    std::unique_ptr<NativeXmaCodec> take(XmaFormat);
    // Resets and keeps the decoder; drops it when the pool is full or reset fails.
    void give(std::unique_ptr<NativeXmaCodec>) noexcept;
    // Opens decoders ahead of use (for example while a level loads).
    void reserve(XmaFormat,size_t count);
    std::vector<float> takeRing(size_t samples);
    void giveRing(std::vector<float>&&) noexcept;
    size_t idleDecoders() const;
    uint64_t reused() const;
};

// One actual software decoder for one mono/stereo layer. It owns accepted packet
// bytes and partial output frames. No guest memory, container trimming, implicit
// EOF, output device, channel-to-speaker mapping or fabricated PCM is involved.
class NativeXmaCodec {
    struct State;
    std::unique_ptr<State> state;
public:
    static constexpr uint32_t packetBytes=2048,frameSamples=512;
    explicit NativeXmaCodec(XmaFormat);
    ~NativeXmaCodec();
    NativeXmaCodec(const NativeXmaCodec&)=delete;
    NativeXmaCodec& operator=(const NativeXmaCodec&)=delete;
    XmaFormat format() const;
    PacketResult send(std::span<const uint8_t> packet);
    // Writes interleaved float samples and returns frames per channel. Zero
    // means no frame currently available; it does not identify end-of-stream.
    uint32_t read(std::span<float> destination);
    // Explicit start-of-stream reset; never used for temporary input starvation.
    void reset();
};
}
