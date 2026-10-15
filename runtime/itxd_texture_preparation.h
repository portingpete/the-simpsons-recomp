#pragma once

#include "renderer/native_backend.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Simpsons {

// Speculative immutable textures from already-published CPU-owned snapshots.
// The worker retains its device-only factory, never a Runtime, guest pointer,
// backend, immediate/deferred context or DXGI object. Demand never waits for it.
class ITXDTexturePreparation {
public:
    using Levels=std::vector<std::vector<uint8_t>>;
    struct Key {
        uint32_t record{};
        uint64_t generation{};
        bool operator==(const Key&) const = default;
    };
    struct Request {
        Key key;
        std::array<uint32_t,6> descriptor{};
        uint32_t width{},height{};
        Graphics::TextureFormat format=Graphics::TextureFormat::RGBA8;
        bool operator==(const Request&) const = default;
    };
    struct Limits {
        size_t cpuBytes=64u*1024u*1024u;
        size_t gpuBytes=64u*1024u*1024u;
        size_t pendingJobs=128;
        size_t readyResources=1024;
    };
    class Ticket;
    using Handle=std::weak_ptr<Ticket>;
    using BeforeWork=std::function<void(const Request&)>;
private:
    struct State;
    struct Result;
public:
    class Ready {
        friend class ITXDTexturePreparation;
        std::shared_ptr<State> state_;
        std::unique_ptr<Result> result_;
        Ready(std::shared_ptr<State>,std::unique_ptr<Result>) noexcept;
    public:
        ~Ready();
        Ready(Ready&&) noexcept;
        Ready& operator=(Ready&&) noexcept = delete;
        Ready(const Ready&) = delete;
        Ready& operator=(const Ready&) = delete;
        const Request& request() const noexcept;
        const std::shared_ptr<Graphics::Texture>& texture() const noexcept;
    };
    struct Stats {
        size_t cpuReservedBytes{},gpuReservedBytes{},pendingJobs{},gpuReservedResources{};
        size_t queued{},running{},ready{};
        size_t highWaterCpuBytes{},highWaterGpuBytes{},highWaterPendingJobs{},highWaterGpuResources{};
        uint64_t submitted{},rejected{},completed{},failed{},taken{},cancelled{},evicted{};
        uint64_t takeAbsent{},takeBusy{},takeMismatch{},takeQueued{},takeRunning{},takeCancelled{};
        uint64_t rejectedProfile{},rejectedCpuBudget{},rejectedDuplicate{},rejectedUnavailable{},rejectedGpuBudget{};
        bool available{},stopped{};
    };
    explicit ITXDTexturePreparation(std::shared_ptr<const Graphics::NativeImmutableMipFactory>);
    ITXDTexturePreparation(std::shared_ptr<const Graphics::NativeImmutableMipFactory>,Limits,
                           BeforeWork beforeDecode={},BeforeWork beforeUpload={});
    ~ITXDTexturePreparation();
    ITXDTexturePreparation(const ITXDTexturePreparation&)=delete;
    ITXDTexturePreparation& operator=(const ITXDTexturePreparation&)=delete;

    // Reserve owned input + a conservative full-mip CPU output bound + 4KiB
    // bookkeeping before copying. Existing decoders alone qualify layouts.
    // Completed GPU results release this pending reservation. GPU charges count
    // authored mip bytes, not the driver's opaque physical allocation footprint.
    Handle submit(const Request&,std::span<const uint8_t> ownedInput) noexcept;
    std::optional<Ready> takeReady(const Handle&,const Request& expected) noexcept;
    // Only marks/notifies; buffers and speculative GPU objects retire on worker.
    void cancel(const Handle&) noexcept;
    // Joins the one current bounded-size job. Driver calls cannot be interrupted
    // and have no wall-clock bound. Pinned Ready leases remain charged through
    // main validation/publication; post-stop Ready cleanup is terminal teardown.
    void stop() noexcept;
    Stats stats() const noexcept;
#ifdef SIMPSONS_ITXD_TEXTURE_PREPARATION_TESTING
    using TestUpload=std::function<std::shared_ptr<Graphics::Texture>(const Request&,const Levels&)>;
    ITXDTexturePreparation(Limits,TestUpload,BeforeWork beforeDecode={},BeforeWork beforeUpload={});
    void testingWithStateLock(const std::function<void()>&);
    void testingBeforeIdleWait(std::function<void()>);
#endif
private:
    std::shared_ptr<State> state_;
};

}
