#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace Simpsons {

// CPU-only preparation of already-published, owned ITXD snapshots. No runtime,
// guest pointer, renderer or GPU object is retained by a job or the worker.
class ITXDRGBAPreparation {
public:
    using Levels=std::vector<std::vector<uint8_t>>;
    struct Key {
        uint32_t record{};
        uint64_t generation{};
        bool operator==(const Key&) const = default;
    };
    struct Limits {
        size_t bytes=64u*1024u*1024u;
        size_t jobs=8;
        size_t minimumOutputBytes=1024u*1024u;
    };
    class Ticket;
    using Handle=std::weak_ptr<Ticket>;
    class Ready {
        friend class ITXDRGBAPreparation;
        // Declared before levels so pixels are freed before their budget lease.
        std::shared_ptr<void> reservation_;
        Ready(std::shared_ptr<void> reservation,Levels value):
            reservation_(std::move(reservation)),levels(std::move(value)){}
    public:
        Levels levels;
        Ready(Ready&&) noexcept = default;
        Ready& operator=(Ready&&) noexcept = delete;
        Ready(const Ready&) = delete;
        Ready& operator=(const Ready&) = delete;
    };
    struct Stats {
        size_t reservedBytes{},outstandingJobs{},queued{},running{},ready{};
        uint64_t submitted{},rejected{},completed{},failed{},taken{},cancelled{};
        bool stopped{};
    };
    // The optional CPU hook is for deterministic concurrency/error tests. The
    // actual decoder is the existing qualified RGBA8 or explicit BC2 base implementation.
    using BeforeDecode=std::function<void(Key)>;
    ITXDRGBAPreparation();
    explicit ITXDRGBAPreparation(Limits,BeforeDecode={});
    ~ITXDRGBAPreparation();
    ITXDRGBAPreparation(const ITXDRGBAPreparation&)=delete;
    ITXDRGBAPreparation& operator=(const ITXDRGBAPreparation&)=delete;

    // The existing profile and this instance's immutable minimum output size.
    // No allocation, lock, job, statistics or availability/budget check: an
    // unavailable CPU worker does not change which profiles it understands.
    bool qualifiesProfile(const std::array<uint32_t,6>& descriptor,uint32_t width,
                          uint32_t height,size_t inputBytes) const noexcept;
    // Reserves input + every output mip + 4KiB bookkeeping before copying.
    // Rejection, allocation failure and unavailable worker simply return empty.
    // On budget pressure, older unused queued/ready BC2 work may be retired;
    // running work, taken Ready leases and all RGBA tickets stay protected.
    Handle submit(Key,const std::array<uint32_t,6>& descriptor,uint32_t width,
                  uint32_t height,std::span<const uint8_t> ownedInput) noexcept;
    // Additive base-only BC2 profile; shares this worker's same job/byte budget.
    // The decoder still validates 0x53, D10, zero base address and one level.
    Handle submitBC2Base(Key,const std::array<uint32_t,6>& descriptor,uint32_t width,
                         uint32_t height,std::span<const uint8_t> ownedInput) noexcept;
    // A try-lock only; demand never waits for the worker or a queued job.
    std::optional<Ready> takeReady(const Handle&,Key expected) noexcept;
    // Wrong kind, key, descriptor or dimensions never consume a ready result.
    std::optional<Ready> takeReadyBC2Base(const Handle&,Key expected,
        const std::array<uint32_t,6>& descriptor,uint32_t width,uint32_t height) noexcept;
    // Running work keeps its reservation until all of its buffers are freed.
    // Cancellation suppresses publication, including after synchronous fallback.
    void cancel(const Handle&) noexcept;
    // Abandons queued/ready work, then joins at most the current bounded decode.
    void stop() noexcept;
    Stats stats() const noexcept;
private:
    enum class Kind {RGBA8,BC2Base};
    Handle submitKind(Kind,Key,const std::array<uint32_t,6>&,uint32_t,uint32_t,
                      std::span<const uint8_t>) noexcept;
    std::optional<Ready> takeReadyKind(const Handle&,Key,Kind,
        const std::array<uint32_t,6>*,uint32_t,uint32_t) noexcept;
    struct Budget;
    struct Lease;
    struct State;
    std::shared_ptr<State> state_;
};

}
