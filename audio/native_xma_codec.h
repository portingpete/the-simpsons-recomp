#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>

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
    std::unique_ptr<NativeXmaCodec> create(XmaFormat) const;
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
