# Native audio output integration review — build122

**Result: no confirmed ownership, callback race, error-retirement or shutdown
defect within the current documented caller contract.** No source change is
requested by this bounded review. The CMake integration builds and tests the
backend without enabling game output; Dac0 remains guarded.

Reviewed 2026-09-10: `CMakeLists.txt`, `audio/native_audio_output.h/.cpp`,
`tests/test_native_audio_output.cpp`, actual generated target/link declarations,
the production archive's defined symbols, and build122 logs. This review wrote
only this document. It did not rebuild, rerun tests, edit the approved ownership
document or inspect additional engine subsystems.

## Concrete ownership and concurrency evidence

- **Submission cannot race storage retirement or voice destruction.**
  [submit](K:/SimpsonsNativeCopy/audio/native_audio_output.cpp:195),
  [retire](K:/SimpsonsNativeCopy/audio/native_audio_output.cpp:216) and
  [stop](K:/SimpsonsNativeCopy/audio/native_audio_output.cpp:240) acquire the same
  mutex. Submission copies PCM and assigns the sequence before publishing
  `Pending` and calling the real SDK. The mutex remains held until the accepted
  receipt is returned, so even an immediate completion cannot let another API
  caller recycle that slot before the returned identity is assembled. A failed
  native submission closes the graph before freeing the unaccepted slot.

- **Publishing completion before the callback returns is safe here.**
  [OnBufferEnd](K:/SimpsonsNativeCopy/audio/native_audio_output.cpp:147) validates
  the stable slot address, performs its final slot access in the terminal-status
  compare/exchange, and then only signals the owner event. After the release
  publication, a worker can acquire that status, retire the receipt, and a
  producer can reuse the slot: the old callback never subsequently reads its
  sequence or PCM. The slot/context allocation itself remains alive until graph
  teardown. Microsoft permits reuse of the finished audio data in this callback;
  the last byte has already been consumed. There is no loop or flush path adding
  extra callbacks for a discarded buffer. [OnBufferEnd contract](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2voicecallback-onbufferend)

- **Close has no API-lock/callback-lock cycle and retains every callback object.**
  [close](K:/SimpsonsNativeCopy/audio/native_audio_output.cpp:110) holds the API
  mutex, but callbacks use atomics and event signaling only. It destroys source
  before master, unregisters the engine callback and performs final engine
  release before retiring remaining pending PCM. Neither callback storage nor
  the event is released until after that sequence. `DestroyVoice` drains voice
  callbacks/data reads; final `IXAudio2::Release` permits freeing graph and
  callback data. Unregister alone is not relied on as the final barrier.
  [DestroyVoice](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2voice-destroyvoice),
  [Release](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2-release),
  [UnregisterForCallbacks](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2-unregisterforcallbacks)

- **Error notification does not release a potentially live SDK buffer.**
  [fail](K:/SimpsonsNativeCopy/audio/native_audio_output.cpp:78) keeps the first
  HRESULT; voice/engine error callbacks publish it without changing pending
  slot ownership. `retire()` still rejects pending storage. A natural buffer end
  or completed graph destruction is required to make it retireable. Completed
  receipts and PCM remain capacity-bounded until explicit retirement. Sequence
  and graph generation checks reject retired or foreign IDs; a stopped owner
  does not start or accept PCM again.

Constructor failure also has a real owner to unwind: the `State` member exists
before `open()` runs, so its destructor closes any engine/master/callback
registration acquired before an exception. Configuration similarly closes its
partial source on failure. No success result is manufactured to hide a failed
system API.

## Precise caller constraints for the future bridge

These are limits of the current API, not defects in an existing game caller:
there is presently no production `NativeAudioOutput` caller in runtime/renderer.

1. **One worker owns event consumption.**
   [poll](K:/SimpsonsNativeCopy/audio/native_audio_output.cpp:221) resets the
   manual event before scanning atomics. A callback before reset is reflected
   in the following scan; a callback during/after the scan leaves a new wake.
   Another ordinary caller must not independently reset/consume this worker's
   event. The fixture producer's exceptional-path `poll()` at
   [line238](K:/SimpsonsNativeCopy/tests/test_native_audio_output.cpp:238) is
   followed by explicit caller cancellation during shutdown, and its worker
   also has a 100-ms timeout; it does not establish a general multiple-poller
   contract. Keep the planned Runtime stop event separate and worker-owned.

2. **A snapshot is not an atomic transaction with callbacks.** The graph error
   is loaded at cpp224 before per-slot statuses are scanned at cpp226. An error
   and failed completion can therefore arrive during the scan, producing a
   completed receipt carrying a failure even if that snapshot's earlier global
   error read was zero. Its callback wake remains signaled; the error is not
   lost. The bridge must inspect each completion's `status/error`, not convert
   every item in `completed` into a successful guest completion. The current
   [worker fixture](K:/SimpsonsNativeCopy/tests/test_native_audio_output.cpp:244)
   checks terminal statuses. Likewise, a processing-pass wake with an empty
   completion list authorizes no guest buffer retirement.

3. **Stop is a graph barrier, not C++ object-lifetime synchronization.**
   [The header](K:/SimpsonsNativeCopy/audio/native_audio_output.h:16) requires the
   owner to outlive all operations and borrowed-handle waits, with its caller
   MTA alive through shutdown. Keep cancellation, worker join, then owner
   destruction as planned. Do not call blocking graph teardown from XAudio2
   callbacks. No receipt or wake proves original speaker mapping, original gain,
   full audible playback or Dac0 readiness.

## Integration and evidence limits

[CMake41](K:/SimpsonsNativeCopy/CMakeLists.txt:41) builds the production archive
with C++20, `/fp:strict /W4 /WX` and public system libraries
`xaudio2 ole32 ksuser uuid`.
[CMake117](K:/SimpsonsNativeCopy/CMakeLists.txt:117) compiles the same source in a
separate test executable with `SIMPSONS_AUDIO_OUTPUT_TESTS`; it does not link a
second production copy of the implementation into that executable. `llvm-nm`
inspection found the production `submit/poll/stop` definitions and no defined
`NativeAudioOutputTestAccess` symbol in `build/native/SimpsonsAudioOutput.lib`.

The runtime link at [CMake72](K:/SimpsonsNativeCopy/CMakeLists.txt:72) does not yet
add this library. The current [Dac0 entry guard](K:/SimpsonsNativeCopy/runtime/engine_audio.cpp:35)
still fails before device initialization. These match the bounded backend-only
checkpoint; they are not an accidental claim of game output integration.

Read directly from `build/hundred-twenty-second-build.log`: **33/33 tests pass,
46.03 seconds**. `build/native-audio-output-122.log` records **501 checks**, 50
fixed-batch natural completions, and 26 accepted/retired concurrent submissions,
all muted, in 2.05 seconds. Concurrent retirement may include shutdown
cancellation. The existing tests exercise caller `VirtualFree` before start,
pending stop, repeated owner lifetimes, callback error delivery and concurrent
submission/poll/retirement/stop. They do not reproduce a physical endpoint removal
or a real `SubmitSourceBuffer` failure after successful queueing: callback-error
injections are labeled, and the actual system rejection exercised is invalid
endpoint creation. This review's latter failure-path assessment is source-level.

Recomputed frozen source hashes match the original standalone manifest:

- Header: `7582440aebeab54ec6cfe736ccbec647055d69692108c13f670b3d7a3eae05ff`.
- Implementation: `af02880a6d57d628037bcace430a23b52cd73be8fb7c39cd468b32318a0824cc`.
- Test: `86f1c65ca366cf621eb2741512634f559b14337c525958f4076804baec741bc1`.

Review frozen at build122. No backend or CMake correction is required by the
examined evidence. The game Dac0 guard remains appropriate pending its separate
fidelity and worker/ownership integration contracts.

## Addendum: Dac0 construction prefix, build123

This later review covers the new `runtime/engine_audio_output.h/.cpp`, its Runtime
lifetime and hook integration, and the changed EXm0 observer. It supersedes the
build122 statement that there is no production caller: the runtime now links
the native output library and acquires a real engine/master graph at the pinned
constructor callsite. It still cannot configure a source or complete Dac0
construction. No implementation file was edited by this review.

### Findings for main

**1. Build123's observed failure is an incorrect fixture trace expectation.**
[tests/test_engine_audio.cpp:30](K:/SimpsonsNativeCopy/tests/test_engine_audio.cpp:30)
requires `ctx.lastFunction == 8234591C` when it catches the intended source-guard
failure. `lastFunction` is updated by `PPC_TRACE_ENTRY`; the failure macro's PC
argument is separate. The original CPU mixer initializer was the last function
traced before the mid-function guard. Read directly from
`build/hundred-twenty-third-build.log:379` onward:

```
Dac0 engine acquired: S=E4624C30 Q=E4627DE0 mixer=E4048000 generation=1
source guarded before SDK voice/buffer/event/worker creation
failure PC=8234591C lastFunction=82353BC8 LR=823458C4
SP=0203F370 r3=0203F400 r4=E4624C70
terminal Dac0 construction cleanup logged
```

The observer rethrows because its trace comparison is false; its later full-S
footprint assertions therefore have **not passed in build123**. Update the
observer to respect function-trace versus failure-PC semantics, while retaining
the exact guard message/ABI and CPU-footprint assertions. Do not infer that the
source guard or native engine acquisition failed. Build123 is **32/33**, not a
completed green checkpoint; this is the sole failed suite in that log.

**2. Entry stack preflight misses the bottom 0x20 bytes of the immediate original
frame.** [caller():49](K:/SimpsonsNativeCopy/runtime/engine_audio_output.cpp:49)
checks `[incomingSP-100,incomingSP)`, then
[begin():95](K:/SimpsonsNativeCopy/runtime/engine_audio_output.cpp:95) records
construction and returns into the original prologue. Independently read from
`analysis/simpsons.pe`, `823456DC = 9421FEE0`, or `stwu r1,-120(r1)`.
For a mapped main stack beginning at `02000000`, incoming `SP=02000100` passes
that current range check, but the original backchain store targets unmapped
`01FFFFE0`. The original save helper has already written its valid upper-frame
saves by then. Checked guest memory still rejects the bad store; this is not an
unchecked host access, and it did not cause build123's normal-stack failure.

Before publishing construction, preflight the complete immediate `120`-byte
entry frame with non-wrapping subtraction. Keep entry-frame validation distinct
from any additional nested-call scratch check at the later hooks. A focused
negative fixture should place SP `100` bytes above the mapped stack bottom and
require rejection before construction publication/original stack saves. This
does not claim to preflight every retained CPU helper's deeper stack usage.

### Ownership paths that remain sound at this guarded seam

- [acquire():109](K:/SimpsonsNativeCopy/runtime/engine_audio_output.cpp:109)
  balances successful `CoInitializeEx`, including `S_FALSE`, with exactly one
  `CoUninitialize`. A failing HRESULT acquires no apartment reference. If native
  graph creation throws, the backend's partial graph unwinds first, followed by
  that apartment reference. Original mixer allocation/CPU metadata remain
  explicitly partial construction; no original rollback or retry success is
  claimed. COM/apartment and real endpoint failures at this engine seam have not
  been fault-injected by the current integration fixture.
  [COM initialization contract](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-coinitializeex)

- [Runtime destruction](K:/SimpsonsNativeCopy/runtime/runtime.cpp:57) joins guest
  workers before resetting output ownership, and does so before guest memory is
  released. The [output State destructor](K:/SimpsonsNativeCopy/runtime/engine_audio_output.cpp:29)
  destroys the native graph before uninitializing COM. The current app and EXm0
  fixture initialize, execute and destroy Runtime on that same main thread.
  The explicit wrong-thread termination is a lifetime invariant, not support
  for moving this owner between threads. No external retained output owner was
  found on the current paths. Its saved CPU pointer is compared during the live
  hooks but is not dereferenced by terminal destruction.

- Wrapper lookup releases `Runtime.audioMutex` before invoking a state method.
  The state mutex serializes acquisition/view/guard inspection. Its VM metadata
  scan takes `vmMutex` only for the region traversal and calls no original CPU
  helper under that lock. The host audio callbacks never touch this state,
  Runtime, the guest or either mutex. No reverse lock acquisition was found in
  this bounded prefix. This does not authorize freeing live S/Q/mixer storage
  from another thread; the graph remains in main-thread construction.

- The explicit extent checks cover `S[3108]`, `Q[100]`, the original aligned
  `mixer[30080]`, and the source-stack descriptor `[SP+90,SP+EC)`; the inspected
  field reads fit them. Guest reads still go through checked PPC accessors.
  Owner storage is screened against image/stack/kernel regions. Root/descriptor,
  constructor caller/frame, default CPU profile and zero SDK globals are checked
  before accepting native acquisition. No guest SDK pointer or native identity
  is written. Native r3=0 at the replaced call is not consumed as an SDK object:
  retained code overwrites r3 with `SP+90` before the next, guarded source call.

- Config pins and generated control flow retain the constructor body, replace
  only `823458C0:488FABA1` and resume `823458C4`, then throw before executing
  `8234591C:488FAB4D`. Those words were checked against the original image.
  The native backend already owns its **host** wake event and processing
  callbacks; “no event/worker yet” applies to the original Dac0 event and worker,
  not that internal host event. No guest callback, PCM source, buffer submission,
  guest event or Dac0 worker is enabled by the prefix.

No additional COM or host graph lifetime defect was found. The fixture trace
correction and immediate-frame preflight are the two bounded follow-ups;
full Dac0 shutdown, gain/routing and worker completion remain outside this review.

### Build125 resolution, reviewed 2026-09-10

Both findings are resolved in the current source. `begin()` now checks a
non-wrapping incoming SP and the full `120`-byte immediate frame before recording
construction. The actual-entry negative fixture uses `SP=02000100`, verifies the
preceding page is unmapped, then checks original stack/S bytes and SP/LR unchanged
and `view()` rejected before restoring the context for normal execution. The
normal observer uses the exact source-guard message and retained LR/owner ABI,
then reaches the full-S footprint assertions. I also verified the new private
`HostFloatingPoint` scope surrounds COM acquisition and State cleanup, with
restoration on exception unwind. No backend source changes were made.

Read directly from `build/hundred-twenty-fifth-build.log`: **34/34 pass, 46.28
seconds**. Main reports the EXm0 fixture at 78,618 checks/133 actual allocation
and free pairs, and muted boot073 stopping at the intended source guard. This
review did not rerun tests. No open prefix-review correction remains; source,
routing, gain and worker integration are still separately gated.
