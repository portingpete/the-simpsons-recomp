#include "stall_profiler.h"

#if SIMPSONS_STALL_PROFILER
#include "ppc_context.h"
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstring>

namespace Simpsons::StallProfiler {
namespace {
constexpr unsigned sectionCount = static_cast<unsigned>(Section::Count);
constexpr unsigned rowCount = 512; // Fixed storage, including on guest worker threads.
constexpr unsigned waitCount = 8;
constexpr const char* sectionNames[] = {"guest", "runtime", "rendering", "audio", "file_io", "wait", "present"};
constexpr const char* guestName = "guest/recompiled";

struct HostState {
    const uint32_t csr = PPCFPSCRRegister::getcsr();
    const DWORD error = GetLastError();
    ~HostState() noexcept { PPCFPSCRRegister::restoreHostCSR(csr); SetLastError(error); }
};

bool environmentEnabled() noexcept {
    const HostState restore;
    char value[2]{};
    return GetEnvironmentVariableA("SIMPSONS_STALL_PROFILE",value,sizeof(value)) == 1 && value[0] == '1';
}

int64_t nativeFrequency() noexcept {
    const HostState restore;
    LARGE_INTEGER value{};
    return QueryPerformanceFrequency(&value) && value.QuadPart > 0 ? value.QuadPart : 1;
}
const int64_t clockFrequency = nativeFrequency();

#ifdef SIMPSONS_STALL_PROFILER_TESTING
int64_t (*testClock)() noexcept{};
int64_t testFrequency{};
void (*testSink)(const char*) noexcept{};
#endif

int64_t frequency() noexcept {
#ifdef SIMPSONS_STALL_PROFILER_TESTING
    if (testFrequency > 0) return testFrequency;
#endif
    return clockFrequency;
}

int64_t now() noexcept {
#ifdef SIMPSONS_STALL_PROFILER_TESTING
    if (testClock) return testClock();
#endif
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

struct Row {
    const char* function{};
    Section section{};
    int64_t ticks{};
    int64_t largestSlice{};
    uint64_t object{};
    uint64_t fence{};
    uint32_t pc{};
    uint32_t caller{};
};

struct State {
    Scope* top{};
    const PPCContext* context{};
    std::array<Row, rowCount> rows{};
    std::array<int64_t, sectionCount> sections{};
    std::array<Scope, waitCount> waits{};
    uint64_t generation{1};
    uint64_t frame{};
    int64_t frameStart{};
    int64_t last{};
    int64_t diagnosticTicks{};
    int64_t totalDiagnosticTicks{};
    int64_t untrackedTicks{};
    DWORD thread{};
    unsigned threadScopes{};
    unsigned waitDepth{};
    unsigned waitOverflow{};
    unsigned scopeDepth{};
    bool initialized{};
    bool frameOwner{};
#ifdef SIMPSONS_STALL_PROFILER_TESTING
    Testing::Snapshot completed{};
#endif
};
thread_local State state;

void initialize(int64_t timestamp) noexcept {
    state.thread = GetCurrentThreadId();
    state.frameStart = timestamp;
    state.last = timestamp;
    state.totalDiagnosticTicks = 0;
    state.initialized = true;
}

// No guest memory reads: these are registers already maintained by the recomp.
// lastFunction is a function-entry address, not an exact instruction PC.
uint32_t guestPC(const PPCContext* context) noexcept { return context ? context->lastFunction : 0; }
uint32_t guestCaller(const PPCContext* context) noexcept { return context ? uint32_t(context->lr) : 0; }

void charge(int64_t timestamp) noexcept {
    const int64_t ticks = timestamp > state.last ? timestamp - state.last : 0;
    state.last = timestamp;
    if (!ticks) return;
    const Scope* current = state.top;
    const Section section = current ? current->section() : Section::Guest;
    const char* function = current ? current->function() : guestName;
    state.sections[static_cast<unsigned>(section)] += ticks;
    // Function/section aggregation keeps addresses and objects from exploding
    // table cardinality. The largest exclusive slice supplies its call details.
    uintptr_t hash = reinterpret_cast<uintptr_t>(function);
    hash ^= hash >> 13;
    hash ^= uintptr_t(static_cast<unsigned>(section)) * 0x9E3779B1u;
    // Bound collision/saturation work, too: the fallback retains section totals.
    for (unsigned probe = 0; probe < 16; ++probe) {
        Row& row = state.rows[(hash + probe) & (rowCount - 1)];
        if (!row.function) { row.function = function; row.section = section; }
        if (row.function != function || row.section != section) continue;
        row.ticks += ticks;
        if (ticks > row.largestSlice) {
            row.largestSlice = ticks;
            row.pc = current ? current->pc() : guestPC(state.context);
            row.caller = current ? current->caller() : guestCaller(state.context);
            row.object = current ? current->object() : 0;
            row.fence = current ? current->fence() : 0;
        }
        return;
    }
    state.untrackedTicks += ticks;
}

double milliseconds(int64_t ticks) noexcept { return double(ticks) * 1000.0 / double(frequency()); }
bool slowCall(int64_t ticks) noexcept { return ticks > (frequency() * 2) / 1000; }
bool slowFrame(int64_t ticks) noexcept { return ticks > (frequency() * 1667) / 100000; }

void emit(const char* line) noexcept {
#ifdef SIMPSONS_STALL_PROFILER_TESTING
    if (testSink) { testSink(line); return; }
#endif
    // Main configures the existing runtime stderr stream for buffered logging.
    // One write per line keeps concurrent thread records legible; never flush here.
    std::fwrite(line, 1, std::strlen(line), stderr);
}

void callLine(const Scope& scope, int64_t duration) noexcept {
    PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);
    char line[1536]{};
    std::snprintf(line, sizeof(line),
        "%s call function=%s section=%s duration_ms=%.3f tid=%lu guest_pc=0x%08X caller=0x%08X pc_kind=function_entry object=0x%llX fence=0x%llX frame=%llu owner=%u\n",
        scope.section() == Section::Wait ? "[WAIT]" : "[STALL]",
        scope.function(), sectionNames[static_cast<unsigned>(scope.section())], milliseconds(duration),
        static_cast<unsigned long>(state.thread), scope.pc(), scope.caller(),
        static_cast<unsigned long long>(scope.object()), static_cast<unsigned long long>(scope.fence()),
        static_cast<unsigned long long>(state.frameOwner ? state.frame + 1 : 0), unsigned(state.frameOwner));
    if (const auto& details = scope.waitDetails(); details.captured) {
        const size_t used = std::strlen(line);
        if (used && used < sizeof(line))
            std::snprintf(line + used - 1, sizeof(line) - used + 1,
                " owner_tid_at_entry=%u owner_caller_at_entry=0x%08X wait_caller=0x%08X status=0x%08X\n",
                details.ownerTid, details.ownerCaller, details.waitCaller, details.status);
    }
    emit(line);
}

void frameLines(int64_t duration, const PPCContext* context) noexcept {
    PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);
    char line[2048]{};
    std::snprintf(line, sizeof(line),
        "[STALL] frame=%llu duration_ms=%.3f budget_ms=16.670 tid=%lu guest_pc=0x%08X caller=0x%08X pc_kind=function_entry sections_ms={guest:%.3f,runtime:%.3f,rendering:%.3f,audio:%.3f,file_io:%.3f,wait:%.3f,present:%.3f} diagnostic_ms=%.3f untracked_contributor_ms=%.3f accounting=owner_wall worker_calls=separate\n",
        static_cast<unsigned long long>(state.frame), milliseconds(duration), static_cast<unsigned long>(state.thread),
        guestPC(context), guestCaller(context), milliseconds(state.sections[0]), milliseconds(state.sections[1]),
        milliseconds(state.sections[2]), milliseconds(state.sections[3]), milliseconds(state.sections[4]),
        milliseconds(state.sections[5]), milliseconds(state.sections[6]), milliseconds(state.diagnosticTicks),
        milliseconds(state.untrackedTicks));
    emit(line);
    std::array<const Row*, 8> largest{};
    for (const Row& row : state.rows) {
        if (!row.ticks) continue;
        for (unsigned rank = 0; rank < largest.size(); ++rank) {
            if (largest[rank] && largest[rank]->ticks >= row.ticks) continue;
            for (unsigned move = unsigned(largest.size()) - 1; move > rank; --move) largest[move] = largest[move - 1];
            largest[rank] = &row;
            break;
        }
    }
    for (unsigned rank = 0; rank < largest.size() && largest[rank]; ++rank) {
        const Row& row = *largest[rank];
        PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);
        std::snprintf(line, sizeof(line),
            "[STALL] frame=%llu rank=%u contributor=%s section=%s exclusive_ms=%.3f largest_slice_ms=%.3f tid=%lu guest_pc=0x%08X caller=0x%08X pc_kind=function_entry object=0x%llX fence=0x%llX\n",
            static_cast<unsigned long long>(state.frame), rank + 1, row.function,
            sectionNames[static_cast<unsigned>(row.section)], milliseconds(row.ticks), milliseconds(row.largestSlice),
            static_cast<unsigned long>(state.thread), row.pc, row.caller,
            static_cast<unsigned long long>(row.object), static_cast<unsigned long long>(row.fence));
        emit(line);
    }
}

void excludeDiagnostic(int64_t started) noexcept {
    const int64_t ended = now();
    if (ended > started) {
        state.diagnosticTicks += ended - started;
        state.totalDiagnosticTicks += ended - started;
    }
    state.last = ended;
}

void resetFrame(int64_t timestamp) noexcept {
    state.rows.fill({});
    state.sections.fill(0);
    state.diagnosticTicks = 0;
    state.untrackedTicks = 0;
    state.frameStart = timestamp;
    state.last = timestamp;
}

#ifdef SIMPSONS_STALL_PROFILER_TESTING
Testing::Snapshot makeSnapshot(int64_t duration) noexcept {
    Testing::Snapshot result;
    for (unsigned index = 0; index < sectionCount; ++index) result.sections[index] = state.sections[index];
    result.frameTicks = duration;
    result.diagnosticTicks = state.diagnosticTicks;
    result.frame = state.frame;
    result.scopeDepth = state.scopeDepth;
    result.registered = state.threadScopes != 0;
    result.frameOwner = state.frameOwner;
    return result;
}
#endif
}

const bool enabled = environmentEnabled();

void Scope::begin(Section section, const char* function, const PPCContext* context,
                  uint64_t object, uint64_t fence) noexcept {
    const HostState restore;
    if (!function || section >= Section::Count) return;
    const int64_t timestamp = now();
    if (!state.initialized) initialize(timestamp);
    charge(timestamp);
    previous_ = state.top;
    function_ = function;
    start_ = timestamp;
    diagnosticStart_ = state.totalDiagnosticTicks;
    generation_ = state.generation;
    object_ = object;
    fence_ = fence;
    if (!context && previous_) {
        // EngineCpuCalls can execute with a copied context. Its native wrapper
        // knows that context, whereas the guest thread's original may be stale.
        pc_ = previous_->pc();
        caller_ = previous_->caller();
    } else {
        context = context ? context : state.context;
        pc_ = guestPC(context);
        caller_ = guestCaller(context);
    }
    section_ = section;
    active_ = true;
    state.top = this;
    ++state.scopeDepth;
}

void Scope::finish() noexcept {
    if (!active_) return;
    const HostState restore;
    active_ = false;
    if (generation_ != state.generation) return; // A detached/reused guest thread.
    const int64_t timestamp = now();
    charge(timestamp);
    // Normal RAII is LIFO; tolerate an unmatched pacing hook without changing
    // game control flow or leaving a dangling pointer in the TLS scope chain.
    if (state.top == this) state.top = previous_;
    else {
        for (Scope* current = state.top; current; current = current->previous_) {
            if (current->previous_ == this) { current->previous_ = previous_; break; }
        }
    }
    if (state.scopeDepth) --state.scopeDepth;
    const int64_t excluded = state.totalDiagnosticTicks - diagnosticStart_;
    const int64_t duration = timestamp > start_ + excluded ? timestamp - start_ - excluded : 0;
    if (slowCall(duration)) { callLine(*this, duration); excludeDiagnostic(timestamp); }
}

void ThreadScope::begin(const PPCContext* context, bool frameOwner) noexcept {
    const HostState restore;
    previousContext_ = state.context;
    previousOwner_ = state.frameOwner;
    const int64_t timestamp = now();
    if (!state.threadScopes) {
        ++state.generation;
        state.top = nullptr;
        state.scopeDepth = 0;
        state.waitDepth = 0;
        state.waitOverflow = 0;
        state.frame = 0;
        resetFrame(timestamp);
        initialize(timestamp);
    } else charge(timestamp);
    ++state.threadScopes;
    state.context = context;
    state.frameOwner = frameOwner;
    active_ = true;
}

ThreadScope::~ThreadScope() noexcept {
    if (!active_) return;
    const HostState restore;
    if (state.threadScopes) --state.threadScopes;
    if (!state.threadScopes) {
        ++state.generation;
        state.top = nullptr;
        state.context = nullptr;
        state.scopeDepth = 0;
        state.waitDepth = 0;
        state.waitOverflow = 0;
        state.frameOwner = false;
        state.initialized = false;
    } else {
        charge(now());
        state.context = previousContext_;
        state.frameOwner = previousOwner_;
    }
}

void frameBoundary(const PPCContext* context) noexcept {
    if (!enabled || !state.frameOwner || !state.threadScopes) return;
    const HostState restore;
    const int64_t timestamp = now();
    charge(timestamp);
    const int64_t duration = timestamp > state.frameStart ? timestamp - state.frameStart : 0;
    ++state.frame;
#ifdef SIMPSONS_STALL_PROFILER_TESTING
    state.completed = makeSnapshot(duration);
#endif
    const bool report = slowFrame(duration);
    if (report) frameLines(duration, context ? context : state.context);
    // Open scopes retain their inclusive start and remain on the stack. Their
    // exclusive accounting resumes in the next frame, including outer scopes.
    resetFrame(timestamp);
    if (report) excludeDiagnostic(timestamp);
}

void beginWait(const PPCContext* context, const char* function, uint64_t object, uint64_t fence) noexcept {
    if (!enabled) return;
    if (state.waitDepth == waitCount || state.waitOverflow) { ++state.waitOverflow; return; }
    Scope& wait = state.waits[state.waitDepth++];
    wait.begin(Section::Wait, function, context, object, fence);
}

void endWait() noexcept {
    if (!enabled) return;
    if (state.waitOverflow) { --state.waitOverflow; return; }
    if (state.waitDepth) state.waits[--state.waitDepth].finish();
}

#ifdef SIMPSONS_STALL_PROFILER_TESTING
namespace Testing {
void setClock(int64_t (*clock)() noexcept, int64_t suppliedFrequency) noexcept {
    testClock = clock;
    testFrequency = suppliedFrequency;
}
void setLogSink(void (*sink)(const char*) noexcept) noexcept { testSink = sink; }
Snapshot snapshot() noexcept { return makeSnapshot(state.last - state.frameStart); }
Snapshot completedFrame() noexcept { return state.completed; }
}
#endif
}
#endif
