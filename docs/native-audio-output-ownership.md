# Native audio output owner

`audio/native_audio_output.h/.cpp` implements a real Windows XAudio2 graph in
`Simpsons::Audio::NativeAudioOutput`. It owns the engine, mastering voice, source
voice, callbacks, wake event, PCM slots and receipt metadata. There is no guest
pointer, guest callback, Dac0 hook, SDK object emulation, decoder or game thread in
this component. No CMake/runtime/config/generated/original/reference file was
changed for this task.

## Integration API

1. Keep an initialized COM MTA alive through graph shutdown. Construct
   `NativeAudioOutput({.capacity=2, .muted=true, .deviceId={}})` on an MTA thread.
   The owner does not initialize or uninitialize the caller's apartment.
2. Read `endpoint()`. Its channel count/mask describe the actual mastering voice;
   its 48000 Hz rate describes that voice's input, not proof of the hardware DAC
   rate. Empty device ID selects the current default. An explicit invalid ID
   fails; it does not silently choose another endpoint.
3. Supply `Routing`: exactly six Windows source-speaker bits, the exact returned
   destination mask, and `6 * endpoint.channels` coefficients. The matrix index
   is `6 * destination + source`. Coefficients are finite within `[-1,1]`.
   `configure()` creates the source and verifies the matrix with the real
   `GetOutputMatrix`. The source remains paused. `routing()` returns this
   immutable configuration.
4. `submit(span<const float>)` accepts exactly 256 interleaved six-channel frames
   (1536 float32 samples, 6144 bytes), finite and within `[-1,1]`. It copies the
   input before returning `Accepted` with a receipt. The caller can immediately
   overwrite or release its input. `Backpressure` copies/accepts nothing and
   returns an empty receipt. `start()` explicitly starts the voice; repeated
   starts are harmless. Submissions may precede start.
5. The engine worker calls `poll()`, handles `error`/`stopped`, processes completed
   receipts, then calls `retire(receipt)`. Completion inspection never invokes
   guest code. `query(receipt)` provides the same receipt state individually.
6. `wakeHandle()` is a borrowed Windows HANDLE, suitable for
   `WaitForMultipleObjects({Runtime.stopEvent, wakeHandle()})`. Never close or
   reset it. `poll()` resets it before scanning atomic state to avoid losing a
   callback between scan and reset. `wait(ms)` is an optional single-handle wait,
   bounded to 60000 ms. Keep the C++ owner alive throughout every operation/wait.
7. On normal engine teardown, signal the separate worker cancellation, join the
   original/native worker, call `stop()`, then destroy the owner. Public operations
   can also race with `stop()` while the C++ object remains alive. `stop()` is
   irreversible and idempotent; reopen by constructing a new owner.

The manual event coalesces hints. It signals on real engine processing passes,
including passes with zero PCM or a paused source, as well as completion, errors
and shutdown. A wake is not a completed buffer, an elapsed sample count, or an
original Xbox scheduling quantum. A worker must inspect state on every wake and
must not wait again after observing terminal stop/error. Microsoft defines the
engine callback at the end of each processing pass; the real zero-PCM behavior
was also tested locally. [Processing-pass callback](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2enginecallback-onprocessingpassend)

This API deliberately has no speculative enable/disable, pause/resume, gain or
original speaker mapping. `start()` is a native operation, not a claim about
original `82C3F7A8`. The Dac0 bridge must establish that meaning independently.

## Ownership, receipts and failure

Each graph receives a monotonically allocated, nonzero 64-bit generation. Each
accepted submission receives a monotonically increasing, nonzero sequence in
that generation. Neither counter wraps; exhaustion rejects. These are host
metadata identities, never guest SDK pointers. A generation from another owner,
an unknown/retired sequence and retirement of pending storage all reject.

Capacity defaults to two; explicitly configurable bounds are 1..64. **All
unretired receipts count against capacity**, even after completion. Thus both
PCM and completion metadata remain bounded if the worker stops polling. Each
slot is preallocated and has a stable address throughout its submission. The
receipt sequence prevents slot reuse from making stale IDs valid.

`Pending` retains storage. `Consumed` means a natural `OnBufferEnd` observed before
shutdown/error, which permits PCM reuse; it does not mean audible playback or
hardware/display-clock synchronization. `Cancelled` means shutdown retired an
unconfirmed submission; some of that block may already have been processed.
`Failed` retains the first failing HRESULT after safe retirement. Successfully
consumed receipts remain consumed if a later, unrelated graph error occurs.

The source buffer has no loop or end-of-stream flag. Temporary starvation is not
EOF. XAudio2 can queue buffers while the source is stopped and consumes them on
start. Its data lifetime requirement is honored by the owned slot until its
completion or synchronous voice destruction. [SubmitSourceBuffer](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2sourcevoice-submitsourcebuffer)

Callbacks perform only bounded, lock-free atomic bookkeeping and `SetEvent`.
They never allocate, free, log, acquire the public API mutex, call game/CPU code,
or touch guest memory. `OnVoiceError` and `OnCriticalError` preserve the first
HRESULT and wake the worker. An error callback alone **does not release queued
PCM**: the worker must stop the graph before pending failed buffers become
retireable. [Callback restrictions](https://learn.microsoft.com/en-us/windows/win32/xaudio2/xaudio2-callbacks)

Stop serializes against submission/configuration. It marks closing, destroys the
source first, then the mastering voice, unregisters engine callbacks and releases
the engine. Callback/event/PCM storage stays alive throughout. Only then does it
retire any remaining pending slots and signal the terminal wake. `DestroyVoice`
is the voice callback/data-read barrier; final engine release drains engine
callbacks. No callback can call this blocking operation. The production code
does not use `FlushSourceBuffers`, whose discard notifications could otherwise
be mistaken for natural consumption. [DestroyVoice](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2voice-destroyvoice),
[Release](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2-release),
[FlushSourceBuffers](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2sourcevoice-flushsourcebuffers)

Malformed input rejects before queue publication and does not poison an otherwise
valid graph. A real failed submission closes the graph, returns no accepted
receipt for that submission, and preserves earlier accepted receipt outcomes.
Backend errors never turn into successful submission/completion. Source creation
failure also closes partial native ownership. An explicit device/no-virtual-client
policy makes device loss terminal rather than silently migrating a caller-pinned
route. Callers must stop and construct a new graph with freshly checked endpoint
information. [Critical error](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2enginecallback-oncriticalerror)

Native creation/configuration/submission/start/destruction isolate MXCSR and
restore the caller's exact value. This does not promise bit-identical hardware
mixing or resampling. The component accepts normalized PCM only; the separate
BE transport's preservation of arbitrary bits, including NaNs, is not permission
to play those values through this API.

## Routing and remaining qualification limits

The source is `WAVEFORMATEXTENSIBLE`, IEEE float32, six channels, 48000 Hz,
24-byte block alignment and 1152000 bytes/sec. The caller assigns the source mask
and explicit matrix. There is no default game-plane permutation or implicit
downmix. Microsoft's mask ordering and output-matrix indexing determine only
the native routing syntax. [WAVEFORMATEXTENSIBLE](https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible),
[SetOutputMatrix](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2voice-setoutputmatrix)

Master and source volume are immutable for the graph: both zero by default, both
one only if a caller explicitly constructs an unmuted graph. Each is read back
before accepting PCM. **Every automated run is muted.** The tests use an explicit
six-speaker fixture mask and asymmetric matrix to the discovered endpoint; that
mask is test input, not evidence for original game speaker meanings.

Not yet qualified: original routing/volume/enable semantics, clipping of mixed
values outside this API's normalized domain, Dac0 timing and scheduling equivalence,
audible output, speaker position correctness, endpoint latency, device removal
recovery, or production graph integration. No guest code or original assets are
needed by this host-ownership test.

## Standalone validation

Run `python -B build/audio-output/build_test.py` from the workspace. It imports
the existing compiler-discovery function without writing bytecode, compiles the
production source separately without fixture access, then compiles/runs the
standalone test executable. Compilation is C++20, ClangCL, `/O2 /fp:strict /W4 /WX`
against the installed Windows SDK and system XAudio2. Logs, objects, executable,
temporary files and source/executable hashes stay under `build/audio-output`.
There is no full CMake build or modification to parent targets.

For later CMake integration, compile `tests/test_native_audio_output.cpp` and
`audio/native_audio_output.cpp` together in a separate test executable with
`SIMPSONS_AUDIO_OUTPUT_TESTS`, linking `Xaudio2.lib Ole32.lib Ksuser.lib Uuid.lib`.
The production source compiles without that define. Test-only access reads the
owned submitted PCM and invokes the actual error callback handlers. Those two
error-delivery tests are explicit injections, not fabricated device-loss events
or a claim that a physical endpoint was removed.

The fixture verifies malformed framing/masks/matrices/samples; an actual failing
`CreateMasteringVoice` for an invalid endpoint; paused admission; zero-PCM wakes
while paused and running; caller cancellation in a multi-handle wait; exact owned
PCM after the caller mutates and `VirtualFree`s its source; real native completion;
two-buffer backpressure including completed/unretired slots; stale/foreign IDs;
pending cancellation, eight reopen lifetimes, concurrent stop, destructor while
running; first-error propagation without early PCM retirement; real
producer/worker/stop races; and exact MXCSR restoration. It does not substitute
an XAudio2 implementation or claim output pixels/audio content by callback count.

Final standalone validation (2026-09-10): **PASS, 516 checks**, 50 natural buffer
completions in fixed batches, plus 25 accepted/retired submissions in the real
producer/worker/stop race. Race retirement includes any pending buffer cancelled
by shutdown; it is not an assertion that all 25 played completely. Processing
wakes were also verified to cease after `stop()` returned. The actual system
endpoint was two channels, mask `00000003`, mastering input 48000 Hz, muted.
The deliberately invalid endpoint returned the real `CreateMasteringVoice`
failure `80070057`. Both production-only and fixture-enabled builds passed
`/W4 /WX`. Scheduling-dependent check/race counts may vary on another run.

Evidence is frozen in `build/audio-output/production-compile.log`, `compile.log`,
`test.log` and `verification.json`. The last file pins the compiled header,
implementation, test source and executable SHA-256 values; the executable hash
for this run is
`bf3c4bb6f8ebfc31e468d9c23c5e077831f760b8b502750bb46fe9aac6f9f1bc`.
No parent build was run. Production header/source and test are frozen pending
main integration feedback; the original routing/gain and Dac0 bridge remain
separate work.

Main integrated the frozen source as production CMake library
`SimpsonsAudioOutput` and the separate fixture executable `NativeAudioOutputTests`.
Build122 passes all33 CTest suites in46.03 seconds. `NativeAudioOutputOwnership`
passes501 scheduling-dependent checks,50 fixed-batch natural completions, and
26 accepted/retired submissions in the concurrent race, all muted. The complete
test log is `build/native-audio-output-122.log`. The guarded game executable is
unchanged from boot072; no game Dac0 output or speaker equivalence is claimed.
