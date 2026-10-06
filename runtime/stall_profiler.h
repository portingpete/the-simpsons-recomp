#pragma once

#include <cstdint>

#ifndef SIMPSONS_STALL_PROFILER
#define SIMPSONS_STALL_PROFILER 1
#endif

struct PPCContext;

namespace Simpsons::StallProfiler {

enum class Section : uint8_t { Guest, Runtime, Rendering, Audio, FileIO, Wait, Present, Count };

struct WaitDetails {
    uint32_t ownerTid{}, ownerCaller{}, waitCaller{}, status{};
    bool captured{};
};

#if SIMPSONS_STALL_PROFILER
// Read once at process startup. The disabled hot path is one predictable branch;
// -DSIMPSONS_STALL_PROFILER=0 removes even that branch.
extern const bool enabled;

class Scope {
    friend void beginWait(const PPCContext*, const char*, uint64_t, uint64_t) noexcept;
    friend void endWait() noexcept;
    Scope* previous_{};
    const char* function_{};
    int64_t start_{};
    int64_t diagnosticStart_{};
    uint64_t generation_{};
    uint64_t object_{};
    uint64_t fence_{};
    uint32_t pc_{};
    uint32_t caller_{};
    WaitDetails waitDetails_{};
    Section section_{Section::Runtime};
    bool active_{};
    void begin(Section, const char*, const PPCContext*, uint64_t, uint64_t) noexcept;
public:
    Scope() noexcept = default;
    Scope(Section section, const char* function, const PPCContext* ctx = nullptr,
          uint64_t object = 0, uint64_t fence = 0) noexcept {
        if (enabled) begin(section, function, ctx, object, fence);
    }
    ~Scope() noexcept { if (active_) finish(); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    void finish() noexcept;
    void setWaitDetails(uint32_t ownerTid, uint32_t ownerCaller, uint32_t waitCaller, uint32_t status) noexcept {
        if (active_) waitDetails_ = {ownerTid, ownerCaller, waitCaller, status, true};
    }

    // Read-only access for the fixed TLS accounting table.
    Scope* previous() const noexcept { return previous_; }
    const char* function() const noexcept { return function_; }
    Section section() const noexcept { return section_; }
    uint32_t pc() const noexcept { return pc_; }
    uint32_t caller() const noexcept { return caller_; }
    uint64_t object() const noexcept { return object_; }
    uint64_t fence() const noexcept { return fence_; }
    const WaitDetails& waitDetails() const noexcept { return waitDetails_; }
};

class ThreadScope {
    const PPCContext* previousContext_{};
    bool previousOwner_{};
    bool active_{};
    void begin(const PPCContext*, bool) noexcept;
public:
    explicit ThreadScope(const PPCContext* ctx, bool frameOwner = false) noexcept {
        if (enabled) begin(ctx, frameOwner);
    }
    ~ThreadScope() noexcept;
    ThreadScope(const ThreadScope&) = delete;
    ThreadScope& operator=(const ThreadScope&) = delete;
};

void frameBoundary(const PPCContext* ctx = nullptr) noexcept;
void beginWait(const PPCContext* ctx, const char* function,
               uint64_t object = 0, uint64_t fence = 0) noexcept;
void endWait() noexcept;

#ifdef SIMPSONS_STALL_PROFILER_TESTING
namespace Testing {
struct Snapshot {
    int64_t sections[static_cast<unsigned>(Section::Count)]{};
    int64_t frameTicks{};
    int64_t diagnosticTicks{};
    uint64_t frame{};
    uint32_t scopeDepth{};
    bool registered{};
    bool frameOwner{};
};
void setClock(int64_t (*clock)() noexcept, int64_t frequency) noexcept;
void setLogSink(void (*sink)(const char*) noexcept) noexcept;
Snapshot snapshot() noexcept;
Snapshot completedFrame() noexcept;
}
#endif

#else
inline constexpr bool enabled = false;
class Scope {
public:
    Scope() noexcept = default;
    Scope(Section, const char*, const PPCContext* = nullptr, uint64_t = 0, uint64_t = 0) noexcept {}
    void finish() noexcept {}
    void setWaitDetails(uint32_t, uint32_t, uint32_t, uint32_t) noexcept {}
};
class ThreadScope {
public:
    explicit ThreadScope(const PPCContext*, bool = false) noexcept {}
};
inline void frameBoundary(const PPCContext* = nullptr) noexcept {}
inline void beginWait(const PPCContext*, const char*, uint64_t = 0, uint64_t = 0) noexcept {}
inline void endWait() noexcept {}
#endif

}
