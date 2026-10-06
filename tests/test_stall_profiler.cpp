#include "runtime/stall_profiler.h"
#include "ppc_context.h"
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace SP = Simpsons::StallProfiler;
namespace {
thread_local int64_t timestamp{};
thread_local unsigned clockReads{};
thread_local std::array<char, 262144> logBytes{};
thread_local size_t logSize{};
thread_local int64_t logCost{};
constexpr DWORD errorCookie = 0x1234ABCD;
constexpr uint32_t csrCookie = PPCFPSCRRegister::DefaultCSR | 0x2000 | 0x8000 | 0x04;
constexpr uint32_t hostileCSR = PPCFPSCRRegister::DefaultCSR | 0x6000 | 0x8040 | 0x20;
size_t checks{};

void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

int64_t fakeClock() noexcept {
    ++clockReads;
    SetLastError(0xBADF00D);
    PPCFPSCRRegister::restoreHostCSR(hostileCSR);
    return timestamp;
}

void capture(const char* line) noexcept {
    const size_t length = std::strlen(line);
    if (length < logBytes.size() - logSize) {
        std::memcpy(logBytes.data() + logSize, line, length + 1);
        logSize += length;
    }
    timestamp += logCost;
    SetLastError(0xDEADBEEF);
    PPCFPSCRRegister::restoreHostCSR(hostileCSR);
}

void clearLog() noexcept { logSize = 0; logBytes[0] = 0; logCost = 0; }
bool contains(const char* text) noexcept { return std::strstr(logBytes.data(), text) != nullptr; }
size_t occurrences(const char* text) noexcept {
    size_t result = 0;
    const char* cursor = logBytes.data();
    while ((cursor = std::strstr(cursor, text))) { ++result; cursor += std::strlen(text); }
    return result;
}

void setHost() noexcept { SetLastError(errorCookie); PPCFPSCRRegister::restoreHostCSR(csrCookie); }
void preserved(const char* message) {
    const DWORD error = GetLastError();
    const uint32_t csr = PPCFPSCRRegister::getcsr();
    check(error == errorCookie && csr == csrCookie, message);
}

int64_t ticks(const SP::Testing::Snapshot& snapshot, SP::Section section) noexcept {
    return snapshot.sections[static_cast<unsigned>(section)];
}
int64_t total(const SP::Testing::Snapshot& snapshot) noexcept {
    int64_t result = snapshot.diagnosticTicks;
    for (int64_t value : snapshot.sections) result += value;
    return result;
}

PPCContext context(uint32_t pc, uint32_t caller) {
    PPCContext result{};
    result.lastFunction = pc;
    result.lr = caller;
    return result;
}

void thresholdsAndPreservation() {
    clearLog(); timestamp = 0;
    PPCContext ctx = context(0x82001000, 0x82002004);
    const PPCContext original = ctx;
    setHost();
    {
        SP::ThreadScope owner(&ctx, true);
        preserved("Thread registration altered host state");
        SP::Scope exact(SP::Section::Runtime, "Exact2ms", &ctx);
        preserved("Scope constructor altered host state");
        timestamp = 2000; exact.finish();
        preserved("Exact-threshold Scope finish altered host state");
        check(logSize == 0, "A call exactly 2 ms must stay quiet");
        {
            SP::Scope slow(SP::Section::FileIO, "SlowRead", &ctx, 0x7788);
            timestamp = 4001;
        }
        preserved("Slow-call logging altered host state");
        check(contains("[STALL] call function=SlowRead section=file_io duration_ms=2.001"), "Slow call threshold/log missing");
        check(contains("guest_pc=0x82001000 caller=0x82002004") && contains("object=0x7788"), "Slow call guest/object detail missing");
        clearLog();
        timestamp = 16670; SP::frameBoundary(&ctx);
        preserved("Exact-threshold frame altered host state");
        check(logSize == 0, "A frame exactly 16.67 ms must stay quiet");
        auto first = SP::Testing::completedFrame();
        check(first.frameTicks == 16670 && total(first) == first.frameTicks, "First frame interval/accounting wrong");
        timestamp = 33341; SP::frameBoundary(&ctx);
        preserved("Slow-frame logging altered host state");
        check(contains("[STALL] frame=2 duration_ms=16.671"), "Slow frame threshold/log missing");
        check(contains("rank=1 contributor=guest/recompiled") && contains("exclusive_ms=16.671"), "Guest residual contributor missing");
        check(std::memcmp(&ctx, &original, sizeof(ctx)) == 0, "Profiler changed guest context");
    }
    preserved("Thread teardown altered host state");
    check(!SP::Testing::snapshot().registered && !SP::Testing::snapshot().scopeDepth, "Thread teardown retained state");
}

void nestedSectionsAndFrameSplitting() {
    clearLog(); timestamp = 0;
    PPCContext outerCtx = context(0x82010000, 0x82010104);
    PPCContext copiedCtx = context(0x82020000, 0x82020104);
    SP::ThreadScope owner(&outerCtx, true);
    timestamp = 1000;
    SP::Scope rendering(SP::Section::Rendering, "DrawFrame", &copiedCtx);
    timestamp = 4000;
    SP::Scope wait(SP::Section::Wait, "GpuFence", nullptr, 0xAA, 0x55);
    timestamp = 20000;
    setHost(); SP::frameBoundary(); preserved("Cross-frame boundary altered host state");
    auto first = SP::Testing::completedFrame();
    check(first.frameTicks == 20000 && first.scopeDepth == 2, "Boundary discarded open scopes");
    check(ticks(first, SP::Section::Guest) == 1000 && ticks(first, SP::Section::Rendering) == 3000 &&
          ticks(first, SP::Section::Wait) == 16000 && total(first) == 20000, "Nested exclusive sections overlap or split incorrectly");
    check(contains("rank=1 contributor=GpuFence section=wait exclusive_ms=16.000"), "Largest contributor sorting wrong");
    check(contains("guest_pc=0x82020000 caller=0x82020104 pc_kind=function_entry object=0xAA fence=0x55"), "Backend null context did not inherit native wrapper context");
    clearLog();
    wait.setWaitDetails(91, 0x82022004, 0x82023008, 0x80);
    preserved("Wait metadata changed host state");
    timestamp = 23000; wait.finish();
    preserved("Cross-frame wait finish altered host state");
    check(contains("[WAIT] call function=GpuFence section=wait duration_ms=19.000"), "Wait inclusive duration lost across frame boundary");
    check(contains("frame=2 owner=1"), "Owner call frame association wrong");
    check(contains("owner_tid_at_entry=91 owner_caller_at_entry=0x82022004 wait_caller=0x82023008 status=0x00000080"),
          "Wait owner/caller/native status details missing");
    timestamp = 25000; rendering.finish();
    check(contains("[STALL] call function=DrawFrame section=rendering duration_ms=24.000"), "Outer inclusive duration lost across frame boundary");
    timestamp = 26000;
    {
        SP::Scope present(SP::Section::Present, "Present", &outerCtx);
        timestamp = 28000;
    }
    timestamp = 40000; SP::frameBoundary();
    auto second = SP::Testing::completedFrame();
    check(second.frameTicks == 20000 && second.scopeDepth == 0, "Second frame interval or nesting depth wrong");
    check(ticks(second, SP::Section::Wait) == 3000 && ticks(second, SP::Section::Rendering) == 2000 &&
          ticks(second, SP::Section::Present) == 2000 && ticks(second, SP::Section::Guest) == 13000 &&
          total(second) == 20000, "Cross-frame sections double-counted or omitted time");
    check(occurrences("[WAIT] call function=GpuFence") == 1 && occurrences("[STALL] call function=DrawFrame") == 1, "Inclusive call logged more than once");
}

void pacingAndDiagnosticAccounting() {
    clearLog(); timestamp = 0;
    PPCContext ctx = context(0x82030000, 0x82030104);
    SP::ThreadScope owner(&ctx, true);
    timestamp = 1000;
    setHost(); SP::beginWait(&ctx, "OriginalPacing", 0x33); preserved("beginWait altered host state");
    timestamp = 2000;
    {
        SP::Scope inner(SP::Section::Runtime, "ShortInner", &ctx);
        timestamp = 2500;
    }
    timestamp = 6000; logCost = 100;
    SP::endWait(); preserved("endWait/logging altered host state");
    check(contains("[WAIT] call function=OriginalPacing section=wait duration_ms=5.000"), "Split guest pacing hooks missing");
    timestamp = 20000; SP::frameBoundary();
    auto completed = SP::Testing::completedFrame();
    check(ticks(completed, SP::Section::Wait) == 4500 && ticks(completed, SP::Section::Runtime) == 500,
          "Pacing nested runtime time was counted twice");
    check(completed.diagnosticTicks == 100 && total(completed) == completed.frameTicks,
          "Logger overhead attributed to game work or omitted from wall interval");
    check(contains("diagnostic_ms=0.100"), "Logger overhead missing from frame summary");
    logCost = 0;
    SP::endWait(); // Unmatched optional hooks remain observational.
    check(SP::Testing::snapshot().scopeDepth == 0, "Unmatched wait hook corrupted stack");
}

void nestedLoggingExcludedFromCallDuration() {
    clearLog(); timestamp = 0;
    PPCContext ctx = context(0x82035000, 0x82035104);
    SP::ThreadScope owner(&ctx, true);
    SP::Scope outer(SP::Section::Runtime, "OuterAcrossLogging", &ctx);
    {
        SP::Scope inner(SP::Section::Wait, "InnerLoggingWait", &ctx);
        timestamp = 3000; logCost = 10000;
    }
    check(timestamp == 13000, "Fake sink did not model nested logging cost");
    timestamp = 20000;
    SP::frameBoundary(); // Slow frame summary adds several log writes.
    const int64_t afterBoundary = timestamp;
    check(afterBoundary > 20000, "Slow frame summary did not model logging cost");
    timestamp = afterBoundary + 1000;
    logCost = 0;
    outer.finish();
    check(contains("[STALL] call function=OuterAcrossLogging section=runtime duration_ms=11.000"),
          "Outer inclusive call duration retained nested logging across a frame reset");
    check(contains("[WAIT] call function=InnerLoggingWait section=wait duration_ms=3.000"),
          "Inner wait duration included its own logger overhead");
    auto frame = SP::Testing::completedFrame();
    check(frame.diagnosticTicks == 10000 && total(frame) == 20000,
          "Nested logging exclusion changed actual owner wall interval");
}

void threadIsolationAndReuse() {
    clearLog(); timestamp = 0;
    PPCContext ctx = context(0x82040000, 0x82040104);
    {
        SP::ThreadScope owner(&ctx, true);
        timestamp = 1000;
        SP::Scope rendering(SP::Section::Rendering, "OwnerDraw", &ctx);
        bool workerOK = false;
        std::thread worker([&] {
            clearLog(); timestamp = 0;
            PPCContext workerCtx = context(0x82050000, 0x82050104);
            SP::ThreadScope registration(&workerCtx);
            SP::Scope io(SP::Section::FileIO, "WorkerRead", &workerCtx, 0x42);
            timestamp = 8000; setHost(); io.finish();
            const auto workerState = SP::Testing::snapshot();
            SP::frameBoundary(); // A worker cannot complete an owner frame.
            workerOK = GetLastError() == errorCookie && PPCFPSCRRegister::getcsr() == csrCookie &&
                workerState.registered && !workerState.frameOwner && workerState.frame == 0 &&
                ticks(workerState, SP::Section::FileIO) == 8000 && contains("frame=0 owner=0") &&
                contains("guest_pc=0x82050000 caller=0x82050104") && !contains("budget_ms=");
        });
        worker.join();
        check(workerOK, "Worker timing/metadata/host preservation or frame association wrong");
        check(logSize == 0, "Worker logs or state leaked into owner TLS");
        timestamp = 20000; rendering.finish(); SP::frameBoundary();
        const auto ownerState = SP::Testing::completedFrame();
        check(ticks(ownerState, SP::Section::FileIO) == 0 && ticks(ownerState, SP::Section::Rendering) == 19000 &&
              total(ownerState) == 20000, "Concurrent worker wall time inflated owner frame");
    }
    clearLog(); timestamp = 100000;
    {
        SP::ThreadScope reused(&ctx, true);
        check(SP::Testing::snapshot().frame == 0 && SP::Testing::snapshot().scopeDepth == 0, "Reused thread retained prior frame state");
        timestamp = 101000; SP::frameBoundary();
        const auto reusedState = SP::Testing::completedFrame();
        check(reusedState.frame == 1 && reusedState.frameTicks == 1000 && total(reusedState) == 1000,
              "Reused thread counted idle lifetime as a frame");
    }
    check(logSize == 0, "Reused thread produced a stale slow-frame report");
}

void exceptionsAndUnregisteredCalls() {
    clearLog(); timestamp = 0;
    PPCContext ctx = context(0x82060000, 0x82060104);
    bool caught = false;
    try {
        SP::ThreadScope owner(&ctx, true);
        SP::Scope io(SP::Section::FileIO, "ThrowingRead", &ctx);
        timestamp = 3000; setHost();
        throw 42;
    } catch (int) { caught = true; }
    preserved("Exception unwinding altered host state");
    check(caught && !SP::Testing::snapshot().registered && SP::Testing::snapshot().scopeDepth == 0,
          "Exception unwinding retained scopes/context");
    check(contains("[STALL] call function=ThrowingRead"), "Exception unwinding failed to log a slow runtime call");
    clearLog();
    {
        SP::Scope standalone(SP::Section::Runtime, "UnregisteredCall", &ctx);
        timestamp = 6001;
    }
    preserved("Unregistered scope altered host state");
    check(contains("function=UnregisteredCall") && contains("frame=0 owner=0"), "Unregistered runtime call was omitted or associated with an owner frame");
    clearLog();
    {
        SP::Scope noContext(SP::Section::Audio, "NoContext");
        timestamp = 9002;
    }
    check(contains("guest_pc=0x00000000 caller=0x00000000"), "Detached thread reused stale guest context");
}

void tableSaturation() {
    clearLog(); timestamp = 0;
    PPCContext ctx = context(0x82070000, 0x82070104);
    SP::ThreadScope owner(&ctx, true);
    std::array<std::array<char, 24>, 600> names{};
    for (unsigned index = 0; index < names.size(); ++index) {
        std::snprintf(names[index].data(), names[index].size(), "SmallCall%u", index);
        SP::Scope call(SP::Section::Runtime, names[index].data(), &ctx);
        timestamp += 100;
    }
    SP::frameBoundary();
    const auto completed = SP::Testing::completedFrame();
    check(ticks(completed, SP::Section::Runtime) == 60000 && total(completed) == 60000, "Table saturation lost section accounting");
    check(contains("untracked_contributor_ms=") && !contains("untracked_contributor_ms=0.000"), "Table saturation was silent");
}

void disabledFastPath() {
    check(!SP::enabled, "Disabled fixture requires SIMPSONS_STALL_PROFILE=0");
    clearLog(); timestamp = 0; clockReads = 0;
    PPCContext ctx = context(0x82080000, 0x82080104);
    const PPCContext original = ctx;
    setHost();
    {
        SP::ThreadScope owner(&ctx, true);
        SP::Scope scope(SP::Section::Wait, "DisabledWait", &ctx, 1, 2);
        scope.setWaitDetails(3, 4, 5, 6);
        timestamp = 1000000;
        scope.finish(); SP::beginWait(&ctx, "DisabledPacing"); SP::endWait(); SP::frameBoundary(&ctx);
    }
    preserved("Disabled profiler altered host state");
    check(clockReads == 0 && logSize == 0 && !SP::Testing::snapshot().registered,
          "Disabled profiler read clock, logged, or created TLS registration");
    check(std::memcmp(&ctx, &original, sizeof(ctx)) == 0, "Disabled profiler changed guest context");
}

void benchmark() {
    SP::Testing::setClock(nullptr, 0);
    SP::Testing::setLogSink(nullptr);
    constexpr unsigned count = 200000;
    LARGE_INTEGER frequency{}, begin{}, baselineEnd{}, measuredEnd{};
    QueryPerformanceFrequency(&frequency);
    volatile unsigned loopCounter = 0;
    QueryPerformanceCounter(&begin);
    for (unsigned index = 0; index < count; ++index) loopCounter = index;
    QueryPerformanceCounter(&baselineEnd);
    for (unsigned index = 0; index < count; ++index) {
        SP::Scope call(SP::Section::Runtime, "BenchmarkCall");
        loopCounter = index;
    }
    QueryPerformanceCounter(&measuredEnd);
    const auto saved = PPCFPSCRRegister::getcsr();
    PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);
    const double nanoseconds = double((measuredEnd.QuadPart - baselineEnd.QuadPart) -
        (baselineEnd.QuadPart - begin.QuadPart)) * 1.0e9 / double(frequency.QuadPart) / double(count);
    std::printf("Stall profiler benchmark enabled=%u calls=%u baseline_subtracted_ns_per_call=%.2f counter=%u\n",
        unsigned(SP::enabled), count, nanoseconds, unsigned(loopCounter));
    PPCFPSCRRegister::restoreHostCSR(saved);
}
}

int main(int argc, char** argv) {
    try {
        SP::Testing::setClock(fakeClock, 1000000);
        SP::Testing::setLogSink(capture);
        if (argc > 1 && std::strcmp(argv[1], "--benchmark") == 0) { benchmark(); return 0; }
        if (argc > 1 && std::strcmp(argv[1], "--disabled") == 0) disabledFastPath();
        else {
            check(SP::enabled, "Enabled fixture requires SIMPSONS_STALL_PROFILE=1");
            thresholdsAndPreservation();
            nestedSectionsAndFrameSplitting();
            pacingAndDiagnosticAccounting();
            nestedLoggingExcludedFromCallDuration();
            threadIsolationAndReuse();
            exceptionsAndUnregisteredCalls();
            tableSaturation();
        }
        PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);
        std::printf("Stall profiler tests: %zu checks passed\n", checks);
        return 0;
    } catch (const std::exception& error) {
        PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);
        std::fprintf(stderr, "Stall profiler test failure: %s\n", error.what());
        return 1;
    }
}
