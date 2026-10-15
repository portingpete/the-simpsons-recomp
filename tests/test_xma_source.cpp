#include "audio/xma_source.h"
#include "audio/fresh_xma_cache.h"
#include "audio/xma_float_output.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <thread>
#include <vector>
#include <xmmintrin.h>

// A successful commit must not allocate. This switch is local to the caller;
// actual codec preparation and fixture setup still use their normal allocators.
static thread_local bool denyAllocation = false;
static thread_local size_t failAfterAllocations = std::numeric_limits<size_t>::max();
static thread_local bool allocationFailureInjected = false;
void* operator new(std::size_t size) {
    if (denyAllocation) throw std::bad_alloc();
    if (failAfterAllocations != std::numeric_limits<size_t>::max()) {
        if (!failAfterAllocations) {
            failAfterAllocations = std::numeric_limits<size_t>::max();
            allocationFailureInjected = true;
            throw std::bad_alloc();
        }
        --failAfterAllocations;
    }
    if (auto* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

using namespace Simpsons::Audio;
using namespace std::chrono_literals;
using Source = XmaSource;
static std::atomic_uint64_t checks{}, comparedSamples{};
static void need(bool ok, const char* why) {
    ++checks;
    if (!ok) throw std::runtime_error(why);
}
template<class F> static void rejects(F&& fn) {
    bool rejected = false;
    try { fn(); } catch (const XmaSourceError&) { rejected = true; }
    need(rejected, "Invalid source operation was not rejected");
}
static std::string sha(std::span<const uint8_t> bytes) {
    std::array<uint8_t, 32> result{};
    need(bytes.size() <= std::numeric_limits<ULONG>::max(), "SHA input bound");
    need(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(bytes.data()), ULONG(bytes.size()),
                    result.data(), ULONG(result.size())) == 0, "SHA256 failed");
    std::string hex;
    for (auto b : result) { hex += "0123456789abcdef"[b >> 4]; hex += "0123456789abcdef"[b & 15]; }
    return hex;
}
struct Fixture {
    const char* file;
    uint32_t channels, frames;
    const char* packetHash;
    const char* rawHash;
    std::vector<Source::Packet> packets;
    std::vector<float> oracle;
    std::vector<uint64_t> packetEndFrames;
};
static Fixture mono[] = {
    {"short.packets", 1, 8704, "312a9c2a63c7744f84df913da544e3fce047289cd42aa5ae76808eb2117a9996",
     "79c1b75fc3dc434099bd6b31e1dcec0da6edcfc05d7061c143ff365ae589aedd", {}, {}, {}},
    {"documented.packets", 1, 55296, "9a837e71c7b71887a182202ada01c003a99515308967f55bf115e9ba8230014f",
     "c7364922a185b457206b94e9173eaa0d206ca7af5bd134586d0ba521576fa6f9", {}, {}, {}}
};
static Fixture stereo = {
    "stereo-layer0.packets", 2, 90624, "15b2401b70ad812144a9cc856a0018e6498fb61e32a41a178b6c1c3c837a8dca",
    "6fd267cb6df20d4814e6552405bc359e5570ea0498b12c6fbe4ca3258dc753ff", {}, {}, {}
};
static Fixture six[] = {
    {"six-mus52-layer0.packets", 2, 9728, "9bbda700f82471b36310d3994942091a82b0ae9923e872f93435738cb0861c4b",
     "5e82b247db32e9bbdc79e0cd463dcc2b5645c89b7cf24753d485d080bbb69689", {}, {}, {}},
    {"six-mus52-layer1.packets", 2, 9728, "ce0778603fc90ceabb60faec1f16ac3ac9f65d2b5667b66cfdedfc0e79a7fd65",
     "a04fcfea9dd0f8ef451d9e8baa3d23100077827981fbbc8fe44258efcec12b35", {}, {}, {}},
    {"six-mus52-layer2.packets", 2, 9728, "414858dac1137e78c8c731fdf3c0cece7ba9f91a792c63c1f0579b7c5ccfe431",
     "13dbbecf15f6f13072d62e4cc024e4173c14fe20cf2f8f1138cb4e98f44fd1d3", {}, {}, {}}
};
static void load(Fixture& fixture, const std::filesystem::path& directory) {
    std::ifstream in(directory / fixture.file, std::ios::binary | std::ios::ate);
    need(bool(in), "Missing prepared original packet fixture");
    const auto bytes = in.tellg();
    need(bytes > 0 && bytes <= 262144 && bytes % 2048 == 0, "Invalid prepared packet extent");
    fixture.packets.resize(size_t(bytes) / 2048);
    in.seekg(0);
    need(bool(in.read(reinterpret_cast<char*>(fixture.packets.data()), bytes)), "Packet read failed");
    need(sha({reinterpret_cast<const uint8_t*>(fixture.packets.data()), size_t(bytes)}) == fixture.packetHash,
         "Prepared packet identity changed");
    // Independent direct-codec schedule, checked against frozen whole-output
    // hashes. No assembler, skip equation, guest metadata or output device here.
    NativeXmaCodec codec({fixture.channels, 48000, XmaVariant::Xma2});
    const std::array<uint32_t, 6> widths{1, 127, 511, 3, 513, 19};
    size_t position = 0;
    auto drain = [&] {
        uint64_t count = 0;
        for (uint32_t attempt = 0; attempt < 32768; ++attempt) {
            const auto frames = widths[position++ % widths.size()];
            std::vector<float> output(size_t(frames) * fixture.channels);
            const auto n = codec.read(output);
            if (!n) return count;
            count += n;
            fixture.oracle.insert(fixture.oracle.end(), output.begin(), output.begin() + size_t(n) * fixture.channels);
        }
        throw std::runtime_error("Oracle drain did not terminate");
    };
    for (const auto& packet : fixture.packets) {
        for (uint32_t attempt = 0;; ++attempt) {
            need(attempt < 1024, "Oracle packet progress bound");
            if (codec.send(packet) == PacketResult::Accepted) break;
            need(drain() != 0, "Oracle send/drain made no progress");
        }
        drain();
        fixture.packetEndFrames.push_back(fixture.oracle.size() / fixture.channels);
    }
    need(drain() == 0 && fixture.oracle.size() == size_t(fixture.frames) * fixture.channels,
         "Oracle raw frame count changed");
    need(sha({reinterpret_cast<const uint8_t*>(fixture.oracle.data()), fixture.oracle.size() * sizeof(float)}) == fixture.rawHash,
         "Direct-codec schedule differs from frozen raw oracle");
}
static Source::Limits limits() {
    Source::Limits value;
    value.maxBufferedFramesPerLayer = 131072;
    return value;
}
static std::vector<XmaFormat> formats(std::span<Fixture* const> fixtures) {
    std::vector<XmaFormat> values;
    for (auto* f : fixtures) values.push_back({f->channels, 48000, XmaVariant::Xma2});
    return values;
}
static Source::Segment segment(std::span<Fixture* const> fixtures, uint64_t sequence, uint32_t frames,
                              Source::Continuity continuity = Source::Continuity::FreshContext) {
    Source::Segment value;
    value.sequence = sequence;
    value.slot = uint32_t(sequence % 20);
    value.continuity = continuity;
    value.declaredFrames = frames;
    for (auto* f : fixtures) value.layers.push_back({0, f->packets});
    return value;
}
static Source::Receipt prepare(Source& owner, const Source::Segment& value) {
    const auto csr = _mm_getcsr();
    const auto result = owner.prepare(value);
    need(_mm_getcsr() == csr, "Prepare changed caller MXCSR");
    need(result.status == Source::PrepareStatus::Accepted && result.receipt.sequence == value.sequence,
         "Prepared source was not accepted");
    return result.receipt;
}
static void compare(const Source::Ticket& ticket, std::span<Fixture* const> fixtures,
                    std::span<const uint64_t> starts) {
    need(bool(ticket) && fixtures.size() == starts.size(), "Missing complete quota ticket");
    uint32_t component = 0;
    for (size_t layer = 0; layer < fixtures.size(); ++layer) {
        const auto& fixture = *fixtures[layer];
        need(ticket->rawStartFrame(uint32_t(layer)) == starts[layer] && starts[layer] + ticket->frames() <= fixture.frames,
             "Quota raw origin/extent changed");
        for (uint32_t channel = 0; channel < fixture.channels; ++channel, ++component) {
            const auto plane = ticket->plane(component);
            need(plane.size() == ticket->frames(), "Quota plane extent differs");
            for (uint32_t frame = 0; frame < ticket->frames(); ++frame) {
                const auto expected = fixture.oracle[(starts[layer] + frame) * fixture.channels + channel];
                need(std::bit_cast<uint32_t>(plane[frame]) == std::bit_cast<uint32_t>(expected), "Quota PCM differs from pinned raw schedule");
                ++comparedSamples;
            }
        }
    }
    need(ticket->channels() == component, "Logical component count/order changed");
}
static void commit(Source& owner, const Source::Ticket& ticket) {
    const auto csr = _mm_getcsr();
    owner.validateCommit(ticket);
    denyAllocation = true;
    try { owner.commit(ticket); } catch (...) { denyAllocation = false; throw; }
    denyAllocation = false;
    need(_mm_getcsr() == csr, "Commit changed caller MXCSR");
}
static void whole(NativeXmaFactory& factory, std::span<Fixture* const> fixtures, uint32_t hostileCSR,
                  XmaVariant variant = XmaVariant::Xma2) {
    const auto previous = _mm_getcsr();
    _mm_setcsr(hostileCSR);
    try {
        auto layerFormats = formats(fixtures);
        for (auto& format : layerFormats) format.variant = variant;
        Source owner(factory, 7, layerFormats, limits());
        auto input = segment(fixtures, 1, fixtures[0]->frames);
        auto receipt = prepare(owner, input);
        const auto before = owner.snapshot();
        need(before.layers[0].acceptedPackets == fixtures[0]->packets.size() && before.committedFrames == 0,
             "Acceptance was confused with consumption");
        for (auto& layer : input.layers) for (auto& packet : layer.packets) packet.fill(0xA5);
        rejects([&] { owner.retire(receipt); });
        const std::array<uint32_t, 8> chunks{1, 3, 127, 511, 512, 513, 997, 4096};
        uint32_t delivered = 0;
        size_t cursor = 0;
        Source::Ticket retained;
        while (delivered < input.declaredFrames) {
            const auto n = std::min(chunks[cursor++ % chunks.size()], input.declaredFrames - delivered);
            auto result = owner.stage(receipt, n);
            need(result.status == Source::StageStatus::Complete && result.ticket->progressBefore() == delivered,
                 "Full raw quota was not staged");
            const std::vector<uint64_t> starts(fixtures.size(), delivered);
            compare(result.ticket, fixtures, starts);
            need(owner.query(receipt).consumedFrames == delivered, "Staging mutated logical progress");
            if (!retained) {
                retained = result.ticket;
                need(owner.stage(receipt, n).ticket == retained, "Staged retry allocated or decoded again");
                rejects([&] { owner.stage(receipt, n + 1); });
                rejects([&] { retained->plane(retained->channels()); });
                rejects([&] { retained->rawStartFrame(uint32_t(fixtures.size())); });
            }
            commit(owner, result.ticket);
            rejects([&] { owner.commit(result.ticket); });
            delivered += n;
            need(owner.query(receipt).consumedFrames == delivered, "Commit did not advance exact logical frames");
        }
        const auto end = owner.snapshot();
        need(end.layers[0].acceptedPackets == before.layers[0].acceptedPackets && end.layers[0].decodedFrames == before.layers[0].decodedFrames,
             "Stage/commit decoded accepted packets again");
        if (fixtures[0]->packets.size() > 2) need(end.layers[0].sendBackpressure > 0, "Real packet backpressure was not exercised");
        need(owner.query(receipt).status == Source::SourceStatus::Delivered && end.copiedBytes != 0,
             "Source ownership ended before retirement");
        owner.retire(receipt);
        rejects([&] { owner.query(receipt); });
        need(owner.snapshot().copiedBytes == 0, "Source copies retained after retirement");
        owner.close();
        compare(retained, fixtures, std::vector<uint64_t>(fixtures.size(), 0));
        need(_mm_getcsr() == hostileCSR, "Source lifetime changed hostile caller MXCSR");
    } catch (...) { _mm_setcsr(previous); throw; }
    _mm_setcsr(previous);
}
static void progressAndContinuity(NativeXmaFactory& factory) {
    const std::array<Fixture*, 1> fixtures{&mono[0]};
    Source owner(factory, 8, formats(fixtures), limits());
    auto input = segment(fixtures, 1, 333);
    input.initialConsumedFrames = 17;
    input.layers[0].skipFrames = 9;
    auto first = prepare(owner, input);
    auto staged = owner.stage(first, 316);
    const std::array<uint64_t, 1> start{9};
    compare(staged.ticket, fixtures, start);
    need(staged.ticket->skippedFrames(0) == 9 && staged.ticket->progressBefore() == 17, "Explicit skip/progress was inferred or merged");
    commit(owner, staged.ticket);
    need(owner.snapshot().layers[0].rawHeadFrame == 325, "Initial progress caused an implicit PCM trim");
    owner.retire(first);
    auto next = segment(fixtures, 2, 129, Source::Continuity::ContinueContext);
    next.layers[0].packets.clear();
    next.layers[0].skipFrames = 5;
    auto second = prepare(owner, next);
    auto continued = owner.stage(second, 129);
    compare(continued.ticket, fixtures, std::array<uint64_t, 1>{330});
    commit(owner, continued.ticket);
    owner.retire(second);
    need(owner.snapshot().layers[0].acceptedPackets == 2 && owner.snapshot().layers[0].bufferedFrames == 8245,
         "Continuation reset the codec or discarded raw surplus");
    staged.ticket.reset(); continued.ticket.reset();
    auto fresh = segment(fixtures, 3, 20);
    fresh.layers[0].packets.resize(1);
    auto third = prepare(owner, fresh);
    auto restarted = owner.stage(third, 20);
    compare(restarted.ticket, fixtures, std::array<uint64_t, 1>{0});
    need(restarted.ticket->epoch() == 2 && owner.snapshot().layers[0].discardedOnFresh == 8245,
         "Explicit fresh context did not account for discarded surplus");
    commit(owner, restarted.ticket); owner.retire(third);
}
static void originalSegments(NativeXmaFactory& factory) {
    // Exact documented mono EA extents, with the explicit fixture profile from
    // frozen evidence. These constants are test inputs, never source defaults.
    const std::array<Fixture*, 1> fixtures{&mono[1]};
    auto bounds = limits(); bounds.maxBufferedFramesPerLayer = 8192;
    Source owner(factory, 9, formats(fixtures), bounds);
    uint64_t origin = 384;
    for (size_t i = 0; i < mono[1].packets.size(); ++i) {
        const uint32_t frames = i == 0 ? 4736 : i == 10 ? 4085 : 5120;
        auto input = segment(fixtures, i + 1, frames, i ? Source::Continuity::ContinueContext : Source::Continuity::FreshContext);
        input.layers[0].packets = {mono[1].packets[i]};
        input.layers[0].skipFrames = i ? 0u : 384u;
        auto receipt = prepare(owner, input);
        uint32_t completed = 0;
        while (completed < frames) {
            const auto n = std::min(257u, frames - completed);
            auto result = owner.stage(receipt, n);
            need(result.status == Source::StageStatus::Complete, "Recorded current-block quota required future input");
            compare(result.ticket, fixtures, std::array<uint64_t, 1>{origin});
            commit(owner, result.ticket);
            origin += n; completed += n;
        }
        owner.retire(receipt);
    }
    const auto end = owner.snapshot();
    need(end.committedFrames == 54901 && end.layers[0].bufferedFrames == 11 && end.epoch == 1,
         "Original supplied extents/skip/continuation changed");
}
static void wrappedInterleavedRead(NativeXmaFactory& factory, std::span<Fixture* const> fixtures) {
    // Observe packet boundaries through the independently scheduled, frozen
    // direct-codec oracle. Bound the ring to one packet burst plus its actual
    // layer surplus; an odd extent forces decoded 512-frame reads to straddle
    // the ring end for both stereo and multiple independent stereo layers.
    const auto packets = fixtures[0]->packets.size();
    uint32_t capacity = 0;
    uint64_t origin = 0;
    for (size_t packet = 0; packet < packets; ++packet) {
        uint64_t commonEnd = std::numeric_limits<uint64_t>::max();
        for (const auto* fixture : fixtures) {
            need(fixture->packets.size() == packets && fixture->packetEndFrames.size() == packets,
                 "Ring-wrap fixture packet layers differ");
            const auto end = fixture->packetEndFrames[packet];
            need(end >= origin && end - origin <= 4194304 - 257,
                 "Ring-wrap fixture packet burst exceeds bounded storage");
            capacity = std::max(capacity, uint32_t(end - origin) + 257);
            commonEnd = std::min(commonEnd, end);
        }
        need(commonEnd > origin, "Ring-wrap fixture packet has no all-layer quota");
        origin = commonEnd;
    }
    need(capacity < origin && capacity % 512 != 0, "Ring-wrap fixture did not require an interior wrap");
    auto bounds = limits();
    bounds.maxBufferedFramesPerLayer = capacity;
    bounds.maxQuotaFrames = std::min(bounds.maxQuotaFrames, capacity);
    Source owner(factory, 16, formats(fixtures), bounds);
    std::vector<uint32_t> wraps(fixtures.size());
    origin = 0;
    for (size_t packet = 0; packet < packets; ++packet) {
        uint64_t commonEnd = std::numeric_limits<uint64_t>::max();
        for (const auto* fixture : fixtures) commonEnd = std::min(commonEnd, fixture->packetEndFrames[packet]);
        const auto frames = uint32_t(commonEnd - origin);
        auto input = segment(fixtures, packet + 1, frames,
                             packet ? Source::Continuity::ContinueContext : Source::Continuity::FreshContext);
        for (size_t layer = 0; layer < fixtures.size(); ++layer)
            input.layers[layer].packets = {fixtures[layer]->packets[packet]};
        const auto before = owner.snapshot();
        const auto receipt = prepare(owner, input);
        const auto decoded = owner.snapshot();
        for (size_t layer = 0; layer < fixtures.size(); ++layer) {
            need(decoded.layers[layer].decodedFrames == fixtures[layer]->packetEndFrames[packet],
                 "Ring-wrap preparation changed decoded packet progress");
            wraps[layer] += uint32_t(decoded.layers[layer].decodedFrames / capacity -
                                     before.layers[layer].decodedFrames / capacity);
        }
        uint32_t delivered = 0;
        while (delivered < frames) {
            const auto take = std::min(257u, frames - delivered);
            const auto staged = owner.stage(receipt, take);
            need(staged.status == Source::StageStatus::Complete, "Ring-wrap quota lost an available layer");
            compare(staged.ticket, fixtures, std::vector<uint64_t>(fixtures.size(), origin + delivered));
            commit(owner, staged.ticket);
            delivered += take;
        }
        owner.retire(receipt);
        origin = commonEnd;
    }
    for (const auto count : wraps) need(count != 0, "Ring-wrap test never crossed a layer ring end");
    const auto final = owner.snapshot();
    need(final.committedFrames == origin && final.epoch == 1 && final.copiedBytes == 0,
         "Ring-wrap continuation changed source accounting or lifetime");
}
static void pendingAllLayers(NativeXmaFactory& factory) {
    // Synthetic host metadata deliberately separates two layers' packet arrival.
    // Compressed bytes and sample oracles are original; this is not an EA caller proof.
    const std::array<Fixture*, 2> fixtures{&six[0], &six[1]};
    Source owner(factory, 10, formats(fixtures), limits());
    auto first = segment(fixtures, 1, 100);
    first.layers[0].packets.resize(1);
    first.layers[1].packets.clear();
    auto a = prepare(owner, first);
    const auto before = owner.snapshot();
    for (unsigned i = 0; i < 3; ++i) {
        const auto pending = owner.stage(a, 100);
        need(pending.status == Source::StageStatus::NeedInput && !pending.ticket, "Partial layer became a successful quota");
    }
    need(owner.snapshot().layers[0].bufferedFrames == before.layers[0].bufferedFrames && owner.query(a).consumedFrames == 0,
         "NeedInput consumed the available layer");
    auto second = segment(fixtures, 2, 100, Source::Continuity::ContinueContext);
    second.layers[0].packets.clear(); second.layers[1].packets.resize(1);
    auto b = prepare(owner, second);
    rejects([&] { owner.stage(b, 100); });
    auto completed = owner.stage(a, 100);
    need(completed.status == Source::StageStatus::Complete, "Pending quota did not resume with real input");
    compare(completed.ticket, fixtures, std::array<uint64_t, 2>{0, 0});
    commit(owner, completed.ticket); owner.retire(a);
    auto later = owner.stage(b, 100);
    compare(later.ticket, fixtures, std::array<uint64_t, 2>{100, 100});
    commit(owner, later.ticket); owner.retire(b);
    const auto end = owner.snapshot();
    need(end.layers[0].acceptedPackets == 1 && end.layers[1].acceptedPackets == 1 && end.epoch == 1,
         "Pending retry duplicated input or reset a layer");
}
static void independentLayerSkips(NativeXmaFactory& factory) {
    const std::array<Fixture*, 3> fixtures{&six[0], &six[1], &six[2]};
    Source owner(factory, 15, formats(fixtures), limits());
    auto input = segment(fixtures, 1, 513);
    const std::array<uint64_t, 3> skips{3, 19, 511};
    for (size_t i = 0; i < skips.size(); ++i) input.layers[i].skipFrames = uint32_t(skips[i]);
    auto receipt = prepare(owner, input);
    auto first = owner.stage(receipt, 129);
    compare(first.ticket, fixtures, skips);
    commit(owner, first.ticket);
    auto second = owner.stage(receipt, 384);
    compare(second.ticket, fixtures, std::array<uint64_t, 3>{132, 148, 640});
    for (uint32_t layer = 0; layer < 3; ++layer)
        need(second.ticket->skippedFrames(layer) == 0, "Explicit skip was applied again at a partial quota");
    commit(owner, second.ticket); owner.retire(receipt);
}
static void boundsAndLifetime(NativeXmaFactory& factory) {
    const std::array<Fixture*, 1> fixtures{&mono[0]};
    auto bounds = limits(); bounds.maxSources = 1; bounds.maxTickets = 1; bounds.maxCompressedBytes = 4096;
    Source owner(factory, 11, formats(fixtures), bounds);
    auto value = segment(fixtures, 1, 6);
    auto invalid = value; invalid.sequence = 0;
    rejects([&] { owner.prepare(invalid); });
    invalid = value; invalid.sequence = std::numeric_limits<uint64_t>::max();
    rejects([&] { owner.prepare(invalid); });
    invalid = value; invalid.initialConsumedFrames = invalid.declaredFrames;
    rejects([&] { owner.prepare(invalid); });
    invalid = value; invalid.layers.clear();
    rejects([&] { owner.prepare(invalid); });
    invalid = value; invalid.layers[0].packets.push_back(mono[0].packets[0]);
    rejects([&] { owner.prepare(invalid); });
    invalid = value; invalid.continuity = Source::Continuity::ContinueContext;
    rejects([&] { owner.prepare(invalid); });
    invalid = value; invalid.continuity = Source::Continuity::Unspecified;
    rejects([&] { owner.prepare(invalid); });
    invalid = value; invalid.layers[0].skipFrames = bounds.maxBufferedFramesPerLayer;
    rejects([&] { owner.prepare(invalid); });
    invalid = value; invalid.continuity = Source::Continuity(42);
    rejects([&] { owner.prepare(invalid); });
    need(owner.snapshot().sources.empty() && !owner.snapshot().configured, "Rejected input mutated the owner");
    const auto receipt = prepare(owner, value);
    rejects([&] { owner.prepare(value); });
    invalid = value; invalid.sequence = 2; invalid.continuity = Source::Continuity::ContinueContext;
    rejects([&] { owner.prepare(invalid); }); // Slot 1 is still owned.
    invalid.slot = 2;
    need(owner.prepare(invalid).status == Source::PrepareStatus::Backpressure, "Source receipt limit did not apply backpressure");
    rejects([&] { owner.stage(receipt, 0); }); rejects([&] { owner.stage(receipt, 7); });
    auto a = owner.stage(receipt, 3);
    commit(owner, a.ticket);
    need(owner.stage(receipt, 3).status == Source::StageStatus::Backpressure, "Externally retained ticket escaped storage limit");
    a.ticket.reset();
    auto b = owner.stage(receipt, 3);
    commit(owner, b.ticket);
    need(owner.prepare(invalid).status == Source::PrepareStatus::Backpressure, "Delivered receipt freed capacity before retire");
    owner.retire(receipt);
    invalid.sequence = 3; invalid.continuity = Source::Continuity::FreshContext;
    auto next = prepare(owner, invalid);
    b.ticket.reset();
    auto pending = owner.stage(next, 3);
    const auto expected = std::bit_cast<uint32_t>(pending.ticket->plane(0)[0]);
    auto foreign = next; ++foreign.instanceGeneration;
    rejects([&] { owner.query(foreign); });
    Source other(factory, 11, formats(fixtures), limits());
    rejects([&] { other.commit(pending.ticket); });
    owner.close(); owner.close();
    need(owner.query(next).status == Source::SourceStatus::Cancelled && owner.snapshot().copiedBytes == 0 &&
         owner.snapshot().layers[0].bufferedFrames == 0, "Close did not cancel/release pending source ownership");
    need(std::bit_cast<uint32_t>(pending.ticket->plane(0)[0]) == expected, "Close invalidated retained ticket data");
    rejects([&] { owner.commit(pending.ticket); }); rejects([&] { owner.prepare(invalid); });
    owner.retire(next); rejects([&] { owner.retire(next); });

    Source::Ticket survives;
    {
        auto localFactory = std::make_unique<NativeXmaFactory>();
        auto local = std::make_unique<Source>(*localFactory, 12, formats(fixtures), limits());
        localFactory.reset(); // No hidden factory lifetime dependency.
        auto input = segment(fixtures, 1, 3); input.layers[0].skipFrames = 448;
        auto r = prepare(*local, input);
        survives = local->stage(r, 3).ticket;
    }
    compare(survives, fixtures, std::array<uint64_t, 1>{448});
}
static void terminalFailure(NativeXmaFactory& factory) {
    const std::array<Fixture*, 2> fixtures{&six[0], &six[1]};
    auto bounds = limits(); bounds.maxBufferedFramesPerLayer = 6000; bounds.maxQuotaFrames = 100;
    Source owner(factory, 13, formats(fixtures), bounds);
    auto input = segment(fixtures, 1, 100);
    input.layers[0].packets.resize(1); // This layer completes preparation first.
    bool failed = false;
    try { owner.prepare(input); } catch (const XmaSourceError&) { failed = true; }
    need(failed, "Decoded raw overflow did not become a terminal failure");
    const auto snapshot = owner.snapshot();
    need(snapshot.failure.stage == Source::FailureStage::Storage && snapshot.failure.layer == 1 && snapshot.failure.cause &&
         snapshot.layers[0].decodedFrames != 0 && snapshot.layers[1].decodedFrames > 6000 &&
         snapshot.sources.size() == 1 && snapshot.sources[0].status == Source::SourceStatus::Failed,
         "Partial-layer failure lost stage/receipt/real-decoder provenance");
    rejects([&] { owner.stage(snapshot.failure.source, 100); });
    input.sequence = 2; rejects([&] { owner.prepare(input); });
    owner.close(); owner.retire(snapshot.failure.source);
    need(owner.snapshot().sources.empty() && owner.snapshot().failure.cause, "Failure cleanup lost terminal provenance");
}
static void floatOutput(NativeXmaFactory& factory, std::span<Fixture* const> fixtures) {
    // Use actual decoder tickets and the independently pinned codec oracle.
    // Check every byte, including sparse clear-only bytes and exterior guards.
    Source owner(factory, 15, formats(fixtures), limits());
    auto input = segment(fixtures, 1, 256);
    for (auto& layer : input.layers) layer.skipFrames = 448;
    const auto receipt = prepare(owner, input);
    const std::array<uint32_t, 7> quotas{1, 16, 17, 31, 32, 33, 126};
    uint32_t consumed = 0;
    for (const auto frames : quotas) {
        const auto staged = owner.stage(receipt, frames);
        need(staged.status == Source::StageStatus::Complete && staged.ticket, "Output test lacks a real complete ticket");
        const auto channels = staged.ticket->channels();
        for (const auto stride : {frames, 257u}) for (const auto address : {0x1000u, 0x1004u, 0x1080u}) {
            XmaFloatOutput output(address, stride, *staged.ticket);
            const size_t bytes = 4 * size_t(stride) * channels;
            need(output.bytes() == bytes, "Output footprint size differs");
            std::vector<uint8_t> actual(bytes + 128, 0xA5), expected(actual);
            for (size_t offset = 0; offset < bytes; ++offset) {
                const auto component = uint32_t(offset / (4 * stride));
                const auto withinPlane = uint32_t(offset % (4 * stride));
                const auto pair = component / 2;
                const auto withinPair = uint32_t(offset - size_t(pair) * 8 * stride);
                const bool stereoLayer = pair * 2 + 1 < channels;
                uint8_t byte = 0xA5;
                if ((address + pair * 8 * stride) % 128 == 0 && withinPair % 128 < 32 &&
                    withinPair / 128 < frames / (stereoLayer ? 16 : 32)) byte = 0;
                if (withinPlane < 4 * frames) {
                    const auto& fixture = *fixtures[pair];
                    const auto channel = component - pair * 2;
                    const auto sample = fixture.oracle[size_t(448 + consumed + withinPlane / 4) * fixture.channels + channel];
                    const auto word = std::bit_cast<uint32_t>(sample);
                    byte = uint8_t(word >> (24 - 8 * (withinPlane % 4)));
                }
                expected[64 + offset] = byte;
            }
            denyAllocation = true;
            try { output.write(std::span<uint8_t>(actual).subspan(64, bytes)); }
            catch (...) { denyAllocation = false; throw; }
            denyAllocation = false;
            need(actual == expected, "NativeRawF32 output differs in PCM, sparse clears, gaps or exterior guards");
            std::fill(actual.begin(), actual.end(), 0xA5);
            rejects([&] { output.write(std::span<uint8_t>(actual).subspan(64, bytes - 1)); });
            need(std::all_of(actual.begin(), actual.end(), [](uint8_t b) { return b == 0xA5; }),
                 "Rejected output wrote a partial footprint");
        }
        rejects([&] { XmaFloatOutput output(0, 256, *staged.ticket); });
        rejects([&] { XmaFloatOutput output(0x1001, 256, *staged.ticket); });
        rejects([&] { XmaFloatOutput output(0x1000, frames - 1, *staged.ticket); });
        rejects([&] { XmaFloatOutput output(0xFFFFFFFC, 256, *staged.ticket); });
        need(owner.query(receipt).consumedFrames == consumed, "Writing PCM committed source progress");
        commit(owner, staged.ticket); consumed += frames;
    }
    need(consumed == 256 && owner.query(receipt).status == Source::SourceStatus::Delivered,
         "Output test did not consume its real source");
    owner.retire(receipt);
}
static void concurrentCommit(NativeXmaFactory& factory) {
    const std::array<Fixture*, 1> fixtures{&mono[0]};
    Source owner(factory, 14, formats(fixtures), limits());
    auto receipt = prepare(owner, segment(fixtures, 1, 100));
    auto ticket = owner.stage(receipt, 100).ticket;
    std::promise<void> start; auto gate = start.get_future().share();
    auto run = [&] { gate.wait(); try { owner.commit(ticket); return true; } catch (const XmaSourceError&) { return false; } };
    auto a = std::async(std::launch::async, run), b = std::async(std::launch::async, run);
    start.set_value();
    need(a.wait_for(2s) == std::future_status::ready && b.wait_for(2s) == std::future_status::ready, "Concurrent commit did not finish");
    need(a.get() != b.get() && owner.query(receipt).consumedFrames == 100, "Concurrent commit duplicated logical consumption");
    owner.retire(receipt);
}
static void freshReuse(NativeXmaFactory& factory, std::span<Fixture* const> fixtures) {
    // Shared backing comes from an actual decoded first owner. Each subsequent
    // voice keeps its own receipt, skip/progress, quota limit and cancellation.
    FreshXmaCache cache(8ull * 1024 * 1024, 2);
    auto bounds = limits(); bounds.maxQuotaFrames = 257; bounds.maxTickets = 1;
    const auto layerFormats = formats(fixtures);
    auto input = segment(fixtures, 1, fixtures[0]->frames - 384);
    for (auto& layer : input.layers) layer.skipFrames = 384;
    const auto csr = _mm_getcsr();
    auto first = cache.prepare(factory, 101, layerFormats, input, bounds);
    input.sequence = 42; input.slot = 13;
    auto second = cache.prepare(factory, 102, layerFormats, input, bounds);
    need(_mm_getcsr() == csr && !first.reused && second.reused, "Fresh repeated content was not reused faithfully");
    const auto a = first.source->snapshot(), b = second.source->snapshot();
    need(a.sharedFreshDecode == first.receipt && b.sharedFreshDecode == first.receipt &&
         first.receipt.owner != second.receipt.owner && second.receipt.instanceGeneration == 102 &&
         second.receipt.sequence == 42 && b.sources[0].slot == 13 && b.committedFrames == 0 && b.epoch == 1,
         "Shared PCM replaced independent owner identity or actual decode provenance");
    need(a.copiedBytes == b.copiedBytes && a.copiedBytes != 0 && cache.snapshot().pinned == 1,
         "Shared PCM bypassed per-voice compressed ownership or cache pin accounting");
    for (size_t i = 0; i < fixtures.size(); ++i)
        need(a.layers[i].acceptedPackets == b.layers[i].acceptedPackets &&
             a.layers[i].decodedFrames == b.layers[i].decodedFrames &&
             a.layers[i].sendBackpressure == b.layers[i].sendBackpressure,
             "Shared PCM lost actual decoder progress counters");
    rejects([&] { second.source->stage(second.receipt, 258); });
    const auto retained = first.source->stage(first.receipt, 257).ticket;
    compare(retained, fixtures, std::vector<uint64_t>(fixtures.size(), 384));
    rejects([&] { second.source->commit(retained); });
    rejects([&] { second.source->query(first.receipt); });
    commit(*first.source, retained);
    need(second.source->query(second.receipt).consumedFrames == 0 &&
         second.source->snapshot().layers[0].rawHeadFrame == 0,
         "Committing one shared voice advanced another voice");
    first.source->close();
    need(first.source->query(first.receipt).status == Source::SourceStatus::Cancelled,
         "Closing shared voice lost original cancellation state");
    compare(retained, fixtures, std::vector<uint64_t>(fixtures.size(), 384));
    auto continuation = input; ++continuation.sequence; continuation.continuity = Source::Continuity::ContinueContext;
    rejects([&] { second.source->prepare(continuation); });
    uint32_t delivered = 0;
    while (delivered < input.declaredFrames) {
        const auto count = std::min(257u, input.declaredFrames - delivered);
        auto staged = second.source->stage(second.receipt, count);
        need(staged.status == Source::StageStatus::Complete && staged.ticket->progressBefore() == delivered,
             "Shared fresh voice did not stage a complete original quota");
        compare(staged.ticket, fixtures, std::vector<uint64_t>(fixtures.size(), 384 + delivered));
        commit(*second.source, staged.ticket); delivered += count;
        if (delivered < input.declaredFrames)
            need(second.source->stage(second.receipt, std::min(257u, input.declaredFrames - delivered)).status ==
                 Source::StageStatus::Backpressure, "Shared voice escaped externally retained ticket bound");
    }
    need(second.source->query(second.receipt).status == Source::SourceStatus::Delivered &&
         second.source->snapshot().copiedBytes == b.copiedBytes,
         "Shared source released copies before original retirement");
    second.source->retire(second.receipt); second.source->close();
    need(cache.snapshot().pinned == 0 && cache.snapshot().entries == 1 && cache.snapshot().bytes <= 8ull * 1024 * 1024,
         "Shared close/retirement lost bounded retained image accounting");
    input.sequence = 59; input.slot = 18;
    auto third = cache.prepare(factory, 103, layerFormats, input, bounds);
    need(third.reused && third.source->snapshot().sharedFreshDecode == first.receipt,
         "Retired original decode lost reusable immutable PCM");
    // Factory/cache lifetime is irrelevant to an already qualified voice.
    std::shared_ptr<Source> survives;
    Source::Receipt survivesReceipt;
    {
        FreshXmaCache local(8ull * 1024 * 1024, 1);
        auto localFirst = local.prepare(factory, 104, layerFormats, input, bounds);
        auto localSecond = local.prepare(factory, 105, layerFormats, input, bounds);
        need(localSecond.reused, "Local cache did not exercise retained image lifetime");
        survives = localSecond.source; survivesReceipt = localSecond.receipt;
    }
    compare(survives->stage(survivesReceipt, 129).ticket, fixtures, std::vector<uint64_t>(fixtures.size(), 384));
    survives->close(); third.source->close();
}
static void freshRecipeAndPressure(NativeXmaFactory& factory) {
    const std::array<Fixture*, 1> fixtures{&mono[0]};
    const auto layerFormats = formats(fixtures);
    auto bounds = limits(); bounds.maxQuotaFrames = 129;
    auto input = segment(fixtures, 1, 333); input.initialConsumedFrames = 17; input.layers[0].skipFrames = 9;
    FreshXmaCache cache(8ull * 1024 * 1024, 8);
    auto first = cache.prepare(factory, 111, layerFormats, input, bounds);
    const auto origin = first.receipt;
    first.source->close();
    const auto retained = cache.snapshot();
    for (const auto sequence : {uint64_t(0), std::numeric_limits<uint64_t>::max()}) {
        auto invalid = input; invalid.sequence = sequence;
        rejects([&] { cache.prepare(factory, 112, layerFormats, invalid, bounds); });
    }
    for (const auto generation : {uint64_t(0), std::numeric_limits<uint64_t>::max()})
        rejects([&] { cache.prepare(factory, generation, layerFormats, input, bounds); });
    for (const auto continuity : {Source::Continuity::Unspecified, Source::Continuity::ContinueContext, Source::Continuity(42)}) {
        auto invalid = input; invalid.continuity = continuity;
        rejects([&] { cache.prepare(factory, 112, layerFormats, invalid, bounds); });
    }
    need(cache.snapshot().entries == retained.entries && cache.snapshot().bytes == retained.bytes && cache.snapshot().pinned == 0,
         "Invalid shared admission mutated image ownership or storage");
    for (uint32_t change = 0; change < 6; ++change) {
        auto changed = input; auto changedBounds = bounds; auto changedFormats = layerFormats;
        if (change == 0) ++changed.layers[0].skipFrames;
        if (change == 1) --changed.declaredFrames;
        if (change == 2) ++changed.initialConsumedFrames;
        if (change == 3) --changedBounds.maxQuotaFrames;
        if (change == 4) changedFormats[0].variant = XmaVariant::Xma1;
        if (change == 5) changed.layers[0].packets.resize(1);
        auto actual = cache.prepare(factory, 113 + change, changedFormats, changed, changedBounds);
        need(!actual.reused && actual.source->snapshot().sharedFreshDecode == actual.receipt && actual.receipt != origin,
             "Different fresh recipe inherited an incompatible decoded image");
        changed.sequence = 40 + change; changed.slot = 11;
        auto repeated = cache.prepare(factory, 123 + change, changedFormats, changed, changedBounds);
        need(repeated.reused && repeated.source->snapshot().sharedFreshDecode == actual.receipt &&
             repeated.source->query(repeated.receipt).consumedFrames == changed.initialConsumedFrames,
             "Equivalent changed recipe lost actual fresh decode/local progress");
        const auto quota = std::min(100u, changed.declaredFrames - changed.initialConsumedFrames);
        auto ticket = repeated.source->stage(repeated.receipt, quota).ticket;
        compare(ticket, fixtures, std::array<uint64_t, 1>{changed.layers[0].skipFrames});
        commit(*repeated.source, ticket);
        actual.source->close(); repeated.source->close();
    }
    auto invalid = input; invalid.layers[0].skipFrames = bounds.maxBufferedFramesPerLayer;
    rejects([&] { cache.prepare(factory, 141, layerFormats, invalid, bounds); });
    invalid = input; invalid.initialConsumedFrames = invalid.declaredFrames;
    rejects([&] { cache.prepare(factory, 141, layerFormats, invalid, bounds); });
    invalid = input; invalid.layers.clear();
    rejects([&] { cache.prepare(factory, 141, layerFormats, invalid, bounds); });
    FreshXmaCache pinned(4ull * 1024 * 1024, 1);
    auto held = pinned.prepare(factory, 151, layerFormats, input, bounds);
    auto different = input; ++different.layers[0].skipFrames;
    auto fallback = pinned.prepare(factory, 152, layerFormats, different, bounds);
    need(!fallback.reused && !fallback.source->snapshot().sharedFreshDecode.owner &&
         pinned.snapshot().pinned == 1 && pinned.snapshot().entries == 1 && pinned.snapshot().evictions == 0 &&
         pinned.snapshot().bypasses == 1 && pinned.snapshot().bytes <= 4ull * 1024 * 1024,
         "Pinned cache pressure changed actual decoder fallback or bounded image storage");
    compare(fallback.source->stage(fallback.receipt, 100).ticket, fixtures, std::array<uint64_t, 1>{10});
    held.source->close(); fallback.source->close();
    auto replacement = pinned.prepare(factory, 153, layerFormats, different, bounds);
    need(!replacement.reused && replacement.source->snapshot().sharedFreshDecode == replacement.receipt &&
         pinned.snapshot().evictions == 1 && pinned.snapshot().entries == 1,
         "Unpinned LRU entry did not free bounded cache capacity");
    for (const auto budget : {uint64_t(0), uint64_t(1)}) {
        FreshXmaCache unavailable(budget, 1);
        auto actual = unavailable.prepare(factory, 154, layerFormats, input, bounds);
        need(!actual.reused && !actual.source->snapshot().sharedFreshDecode.owner && unavailable.snapshot().bytes == 0 &&
             unavailable.snapshot().entries == 0 && unavailable.snapshot().bypasses == 1,
             "Optional cache storage pressure rejected or replaced normal actual decoding");
        auto ticket = actual.source->stage(actual.receipt, 100).ticket;
        compare(ticket, fixtures, std::array<uint64_t, 1>{9}); commit(*actual.source, ticket); actual.source->close();
    }
    // Fault each successive host allocation until one reaches optional image
    // publication. Required source allocations may fail normally; an optional
    // failure must leave the already-prepared real owner and exact PCM usable.
    bool observedOptionalFailure = false;
    for (size_t position = 0; position < 64 && !observedOptionalFailure; ++position) {
        FreshXmaCache faulted(4ull * 1024 * 1024, 1);
        allocationFailureInjected = false; failAfterAllocations = position;
        FreshXmaCache::Result actual;
        try { actual = faulted.prepare(factory, 161, layerFormats, input, bounds); }
        catch (const std::bad_alloc&) {}
        failAfterAllocations = std::numeric_limits<size_t>::max();
        if (!actual.source || !allocationFailureInjected) continue;
        need(!actual.reused && faulted.snapshot().bypasses == 1 && faulted.snapshot().entries == 0 &&
             faulted.snapshot().bytes == 0 && actual.source->snapshot().failure.stage == Source::FailureStage::None,
             "Optional fresh image allocation failure damaged actual source admission");
        auto ticket = actual.source->stage(actual.receipt, 100).ticket;
        compare(ticket, fixtures, std::array<uint64_t, 1>{9}); commit(*actual.source, ticket); actual.source->close();
        observedOptionalFailure = true;
    }
    need(observedOptionalFailure, "Allocation fault sweep did not reach optional fresh image publication");
}

// Limits::prepareFrames: prepare() decodes a prefix, stage() decodes further packets in
// order as quotas need them. Every quota matches the pinned oracle bit for bit; packets
// reach the codec only as required; an incomplete source is never shared by the fresh
// cache; and retiring a delivered source first gives the codec its remaining packets.
static void incremental(NativeXmaFactory& factory, std::span<Fixture* const> fixtures, uint32_t prefix) {
    auto bounded = limits();bounded.prepareFrames = prefix;
    {
        Source owner(factory, 31, formats(fixtures), bounded);
        auto input = segment(fixtures, 1, fixtures[0]->frames);
        auto receipt = prepare(owner, input);
        const auto early = owner.snapshot();
        for (size_t layer = 0; layer < fixtures.size(); ++layer)
            need(early.layers[layer].bufferedFrames >= prefix && early.layers[layer].acceptedPackets < fixtures[layer]->packets.size(),
                 "Incremental prepare did not stop after its prefix");
        const std::array<uint32_t, 6> chunks{256, 1, 997, 4096, 513, 127};
        uint32_t delivered = 0;size_t cursor = 0;
        while (delivered < input.declaredFrames) {
            const auto n = std::min(chunks[cursor++ % chunks.size()], input.declaredFrames - delivered);
            auto result = owner.stage(receipt, n);
            need(result.status == Source::StageStatus::Complete, "Incremental stage did not decode its quota");
            compare(result.ticket, fixtures, std::vector<uint64_t>(fixtures.size(), delivered));
            const auto view = owner.snapshot();
            // No packet beyond need: the frames decoded before the last packet sent were
            // still short of the largest target so far (prefix or this quota's end).
            for (size_t layer = 0; layer < fixtures.size(); ++layer) {
                const auto accepted = size_t(view.layers[layer].acceptedPackets);
                need(accepted < 2 || fixtures[layer]->packetEndFrames[accepted - 2] < std::max<uint64_t>(prefix, uint64_t(delivered) + n),
                     "Incremental stage decoded a packet it did not need");
            }
            commit(owner, result.ticket);delivered += n;
        }
        const auto end = owner.snapshot();
        for (size_t layer = 0; layer < fixtures.size(); ++layer)
            need(end.layers[layer].acceptedPackets == fixtures[layer]->packets.size(), "Incremental source left packets undecoded");
        owner.retire(receipt);owner.close();
    }
    {
        // Retiring a delivered source that still holds unsent packets sends them first.
        Source owner(factory, 32, formats(fixtures), bounded);
        auto receipt = prepare(owner, segment(fixtures, 1, 1024));
        auto result = owner.stage(receipt, 1024);
        need(result.status == Source::StageStatus::Complete, "Short incremental quota failed");
        commit(owner, result.ticket);
        need(owner.snapshot().layers[0].acceptedPackets < fixtures[0]->packets.size(), "Short source decoded every packet early");
        owner.retire(receipt);
        need(owner.snapshot().layers[0].acceptedPackets == fixtures[0]->packets.size(), "Retire dropped unsent packets");
    }
    {
        // The cache never shares an incomplete source; once one has decoded every packet
        // its image is published and a later play reuses the same exact floats.
        FreshXmaCache cache;
        auto input = segment(fixtures, 1, fixtures[0]->frames);
        const auto first = cache.prepare(factory, 33, formats(fixtures), input, bounded);
        need(!first.reused, "An incremental miss reported reuse");
        // The background worker finishes the decode without any stage() call.
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (cache.snapshot().entries == 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);
        const auto decoded = first.source->snapshot();
        bool complete = true;
        for (size_t layer = 0; layer < fixtures.size(); ++layer)
            complete &= decoded.layers[layer].acceptedPackets == fixtures[layer]->packets.size();
        need(cache.snapshot().entries == 1 && complete && decoded.committedFrames == 0,
             "The background worker did not complete and publish the incremental decode");
        // The incremental source itself still plays every exact frame.
        uint32_t delivered = 0;
        while (delivered < input.declaredFrames) {
            const auto n = std::min(4096u, input.declaredFrames - delivered);
            auto result = first.source->stage(first.receipt, n);
            need(result.status == Source::StageStatus::Complete, "Incremental cached source failed to stage");
            compare(result.ticket, fixtures, std::vector<uint64_t>(fixtures.size(), delivered));
            commit(*first.source, result.ticket);delivered += n;
        }
        first.source->retire(first.receipt);
        const auto third = cache.prepare(factory, 35, formats(fixtures), input, bounded);
        need(third.reused && cache.snapshot().entries == 1, "A completed incremental decode was not reused");
        delivered = 0;
        while (delivered < input.declaredFrames) {
            const auto n = std::min(997u, input.declaredFrames - delivered);
            auto result = third.source->stage(third.receipt, n);
            need(result.status == Source::StageStatus::Complete, "Published incremental image failed to stage");
            compare(result.ticket, fixtures, std::vector<uint64_t>(fixtures.size(), delivered));
            commit(*third.source, result.ticket);delivered += n;
        }
    }
}
int main(int argc, char** argv) {
    try {
        static_assert(sizeof(float) == 4 && std::endian::native == std::endian::little);
        need(argc == 3, "Usage: NativeXmaSourceTests MONO_PACKET_DIR MULTILAYER_PACKET_DIR");
        for (auto& fixture : mono) load(fixture, argv[1]);
        load(stereo, argv[2]); for (auto& fixture : six) load(fixture, argv[2]);
        NativeXmaFactory factory;
        const std::array<Fixture*, 1> shortCase{&mono[0]}, longCase{&mono[1]}, stereoCase{&stereo};
        const std::array<Fixture*, 3> sixCase{&six[0], &six[1], &six[2]};
        rejects([&] { Source owner(factory, 0, formats(shortCase), limits()); });
        auto invalidBounds = limits(); invalidBounds.maxTickets = 0;
        rejects([&] { Source owner(factory, 1, formats(shortCase), invalidBounds); });
        std::array<XmaFormat, 2> mixed{{{1, 48000, XmaVariant::Xma2}, {2, 44100, XmaVariant::Xma2}}};
        rejects([&] { Source owner(factory, 1, mixed, limits()); });
        // Masked hostile round/FTZ/DAZ/status controls; every result is bit checked.
        for (uint32_t csr : {0x1F80u, 0xBFC5u, 0xDFE1u, 0xFFE5u}) whole(factory, shortCase, csr);
        whole(factory, shortCase, 0x1F80u, XmaVariant::Xma1);
        whole(factory, longCase, 0x1F80); whole(factory, stereoCase, 0x1F80); whole(factory, sixCase, 0x1F80);
        progressAndContinuity(factory); originalSegments(factory); pendingAllLayers(factory);
        wrappedInterleavedRead(factory, stereoCase); wrappedInterleavedRead(factory, sixCase);
        independentLayerSkips(factory);
        boundsAndLifetime(factory); terminalFailure(factory); concurrentCommit(factory);
        freshReuse(factory, shortCase); freshReuse(factory, stereoCase); freshReuse(factory, sixCase);
        freshRecipeAndPressure(factory);
        incremental(factory, longCase, 4096); incremental(factory, stereoCase, 2048); incremental(factory, sixCase, 1024);
        const std::array<Fixture*, 2> threeCase{&six[0], &mono[0]}, fourCase{&six[0], &six[1]};
        const std::array<Fixture*, 3> fiveCase{&six[0], &six[1], &mono[0]};
        floatOutput(factory, shortCase); floatOutput(factory, stereoCase);
        floatOutput(factory, threeCase); floatOutput(factory, fourCase);
        floatOutput(factory, fiveCase); floatOutput(factory, sixCase);
        std::cout << "Native XMA source PASS: " << checks << " checks; " << comparedSamples
                  << " exact raw samples; six pinned original layer streams; independent immutable fresh PCM reuse; NativeRawF32 byte footprints; no guest writes/output device/EOF\n";
        return 0;
    } catch (const std::exception& error) {
        denyAllocation = false; failAfterAllocations = std::numeric_limits<size_t>::max();
        std::cerr << "Native XMA source FAIL: " << error.what() << '\n';
        return 1;
    }
}
