# Native thread-exit status bridge

The bridge replaces only the six completion-field instructions at
`82433358..8243336C` in the original `82433328` exit-code helper. It resumes at
`82433370`. The original typed object reference, failure conversion, caller
output store, object dereference, Boolean return and RenderWare polling policy
remain compiled original code. No `finished` flag, emulated completion word,
invented success code or special movie-worker result participates.

This work follows the preserved checkpoint
`checkpoints/native-movie-renderer-launcher-152.zip` (SHA-256
`6536f5985e91ecb67ec7072b0a73c941f464a0490e8bfee00bbb66f4eadbef10`). Its existing
verification report records 5,300 verified payloads and 1,345 included AOT inputs.
The prerequisite audit is [native-thread-exit-status.md](native-thread-exit-status.md),
with its original evidence index in
`build/thread-exit-status/audit-evidence-index.json`.

## Ownership and result contract

`runtime/thread_exit_status.cpp` defines the global function
`void SimpsonsNativeThreadExitStatus(PPCContext&, uint8_t*)`. It requires the
active runtime/context, original reference-call LR `8243334C`, aligned stack,
thread-type token in r4, original stack-result address in r5, and r3 matching
that stack result. These checks do not replace `ObReferenceObjectByHandle`.

Under `Runtime::handleMutex`, it finds `objectReferences[r3]`, requires a
nonzero counted reference with a matching `guestObject` and Thread type, and
copies the existing shared owner. It then releases the mutex. No new native
handle or completion registry is created. An already closed guest handle is
valid at this point if the original counted reference still owns the thread.
The local shared owner prevents native-handle closure and KTHREAD reclamation
during the Windows calls.

`WaitForSingleObject(native, 0)` establishes native completion. `WAIT_TIMEOUT`
produces r11 = 259 and CR0.EQ. `WAIT_OBJECT_0` is followed by
`GetExitCodeThread`; its exact unsigned 32-bit result becomes r11 and CR0.GT
is set. CR0.LT is cleared and CR0.SO copies XER.SO. A final result of zero,
`FFFFFFFF`, or 259 is preserved. In particular, an actually terminated thread
whose result is 259 still follows the original poll's active-code branch.
Microsoft documents the zero-duration wait and the ambiguous final value:
[GetExitCodeThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodethread)
and [WaitForSingleObject](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject).

Unexpected wait results and failed native queries throw explicit diagnostics
with the guest object, wait result, operation and Windows error. They cannot
publish a successful guest result. The bridge preserves all other guest state
and host FP/last-error state, including rejection paths. It neither writes
the caller's output nor consumes its reference; the retained original code
performs those operations. If that original output store faults, its later
dereference has not executed. The test explicitly verifies this ordering and
then balances the fixture's reference.

## First-completed observation

One diagnostic is emitted per process, only after a successful native wait
and exit-code query:

```text
[THREAD EXIT STATUS] first completed original query: object=0x........ mapped_handle=0x... native_id=... wait=0x00000000 status=0x........ id_error=0; ...
```

The mapped handle is the lowest matching guest handle at the retained-owner
snapshot. Zero means the owner has no mapped guest handle (closed or pseudo).
This is diagnostic identity, not a condition for success. Native ID is obtained
from the retained handle; `id_error` explicitly records any ID-query failure.
The process-wide atomic flag bounds logging only; it is never read to decide
completion. For run 153, correlate the native ID/handle with the earlier worker
creation/exit log and inspect the original movie cleanup after its poll exits.
This bridge alone establishes neither correct front-buffer composition nor
playable progress.

## Main-owned integration

No `runtime.h` change is needed: the generator emits the global hook declaration.
Add `runtime/thread_exit_status.cpp` to `config/native_sources.json` and this
entry to `config/simpsons.toml`:

```toml
[[midasm_hook]]
address = 0x82433358
name = "SimpsonsNativeThreadExitStatus"
registers = ["ctx", "base"]
jump_address = 0x82433370
evidence_hex = "81630004556b063f4182000c816301404800000839600103"
```

The hook is not a whole-function return hook. Regenerate using the existing
AOT procedure before building. Generated source must call the bridge and jump
to the original output-store label. Never edit generated code manually.

The focused test links the production runtime and therefore the actual AOT
query, poll, close, error conversion and object imports:

```cmake
add_executable(ThreadExitStatusTests tests/test_thread_exit_status.cpp)
target_compile_options(ThreadExitStatusTests PRIVATE /fp:strict /W4 /WX)
target_link_libraries(ThreadExitStatusTests PRIVATE SimpsonsRuntime)
add_test(NAME OriginalThreadExitStatus COMMAND ThreadExitStatusTests
  "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
set_tests_properties(OriginalThreadExitStatus PROPERTIES TIMEOUT 90)
```

The project already supplies C++20, ClangCL, Windows definitions, include paths
and libraries through `SimpsonsRuntime`. No standalone fixture macro or extra
library is needed. The executable takes exactly one argument: the original
image path. `Runtime::load` also expects the existing sibling derived XEX and
player-owned game-data directory, as other original-body tests do. It does not
run game startup, open a window, output audio or change original files. The
only fixture AOT dispatch replacement is its controlled worker entry at
`PPC_CODE_BASE`; the helper, poll, close and imported services use their actual
compiled mappings. Real worker startup, cancellation, `ThreadExit`, TLS
teardown, handle ownership and reaping are production implementations.

## Verification coverage and current readiness

The source contains 109 exact original instruction/data word pins, checked
against `analysis/simpsons.pe` during development. They include the full exit
helper, full poll, full close helper, status conversion wrapper, six bridge
words, movie-loop predicate and read-only close-dispatch table. The existing
runtime image load also checks the executable hash. The source checks cover:

- Real suspended and gated running workers; original helper success with 259,
  no premature close and balanced references.
- Both ordinary worker return and production `ExTerminateThread`/`ThreadExit`
  with final codes 0, 42, `FFFFFFFF`, and 259. The first three drive original
  handle-close/state-2 behavior; final 259 retains the original active branch
  despite a signaled native handle.
- Whole guest-context preservation at the bridge except r11/CR0, both XER.SO
  values, five host FP modes, last-error preservation, unchanged KTHREAD/stack
  bytes, and no early caller-output store or dereference.
- The host TLS-exit barrier for both return paths: diagnostic `finished` true
  while the native handle is nonsignaled must remain active. Releasing the
  barrier permits the real final status.
- Pseudo-current-thread and an ordinary handle to the current native thread.
- Invalid/closed handles, wrong requested type, non-thread objects, malformed
  bridge provenance/reference metadata, output faults and runtime cancellation.
- Real reduced-access thread handles causing Windows wait/query failures.
  These are actual failing OS operations, not mocked success/failure callbacks.
- Two concurrent callers with real counted references acquired before handle
  close, each making 256 active and 256 completed bridge queries. An event
  latch keeps the worker alive until both active loops finish; no delay or
  assumed scheduler speed determines the expected result.
- Counted references and in-flight shared ownership preventing reaping,
  followed by final release, reclamation and reuse of the original thread
  area/stack slots. The original poll is invoked again after closure to verify
  its settled no-handle path.

Native waits are individually bounded to ten seconds. A 90-second CTest budget
allows diagnostic unwinding of multiple failing waits; successful runs should
finish much sooner. The bridge/test sources are ready for main's shared build.
The authoring agent has performed original-byte verification, but has not run
C++ builds, shared AOT generation, game launches, or UI actions. Execution and
actual run-153 observations remain main's validation responsibility; source
coverage is not a claim that those checks have passed.
