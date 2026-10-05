# Native Dac0 source and original worker integration

Build130 passes all36 CTest suites. Actual muted boot075 completes original
Dac0 construction, configures and activates real Windows output, and starts
the original `RWAudioCore Dac` worker. The next reached failure is the
`KeTimeStampBundle` read in824324A8, caller8232DEA8. This is startup progress;
no decoded game waveform, audible fidelity, screen or gameplay is established.

## Retained CPU construction and native ownership

`runtime/engine_audio_output.cpp` and byte-pinned `config/simpsons.toml` hooks
retain the original constructor, mixer allocation, PCM buffers, two source
descriptors, synchronization event, worker creation and graph registration.
The native boundary replaces SDK engine/voice operations. It owns a real MTA,
XAudio2 engine/master/source and `Audio::DacProcessor`; it creates no console
SDK singleton, hardware voice layout or success-only source object.

The nonzero word S+40 is a typed native source identity, generation-unique in
its namespace and rejected as guest storage. Source operations validate both
that identity and the actual original S/root/descriptor/vtable. Original
SDK globals82E2D9F0/82E2D9F4 stay zero. The constructor validates its entire
0x120-byte stack frame before retained saves or native owner publication.

Boot075 records S=E4624C30, Q=E4627DE0, mixer=E4048000, source00700001,
eventE1A66720 and native worker43352. The real Windows endpoint reports two
channels/mask3. All automatic launches and tests use muted source/master gain.

## Explicit Windows output policy

The source is six-channel float48kHz, with components interpreted as Windows
FL/FR/FC/LFE/SL/SR. Windows supplies its real default endpoint matrix, which is
read back and validated. This is the native speaker policy: original physical
labels and console downmix are still unresolved. Windows session/endpoint
volume supplies the PC volume policy; console category globals are not copied
into fabricated SDK structures. See `native-audio-windows-routing.md` and
`native-dac-routing.md` for the evidence and limits.

Two logical source slots preserve the original admission bound. A separate
four-buffer Windows stage absorbs the 256-frame original quantum versus the
Windows processing quantum. Its buffering/latency is a native adaptation,
not established console device timing equivalence. Raw source storage is
copied, scalar gain is applied at processing time, and source consumption is
reported only after full DSP and accepted downstream submission. Actual
XAudio2 completion remains a separate receipt. See `native-dac-processing.md`.

## Original worker and callback discipline

The actual original worker823460D0 performs graph commands and PCM production.
Native wait/capacity/submit hooks service its real processing demand. The
private host DSP thread and XAudio2 callbacks never execute AOT code or access
guest memory. Only the original Dac worker delivers original callbacks.

Completion823463D0 receives the original12-byte record containing S, the
original completion-word address and a result. It clears S+3100 or S+3104
itself. Processing callback823463B0 receives the same record shape. The
original tail event calls signal the retained CPU synchronization-event
header. Native event handling validates the actual type/state/list links;
no Windows HANDLE is inserted into that CPU header. Processor wake and runtime
cancellation are actual native wait objects. Coalesced processing hints are
demand notifications, not fabricated PCM completion.

The original slot becomes free only after its actual callback clears the word
and the matching logical receipt is retired. Generation/sequence provenance
continues through independent downstream completion. Queued data cancelled
during final release invokes the original cancellation callback; failures stay
explicit and retain operation, stage, error and receipt diagnostics.

## Teardown and fixture observation

The original root destructor82338FA0 queues Dac deletion on the Dac worker.
The source-release hook stops/joins only the private host DSP thread and drains
downstream callbacks before the original CPU event can be freed. It does not
join the calling Dac worker. After the root destructor releases Q+48, the main
thread hook82339088 joins that actual worker before original root storage is
freed. Hook823391E8 retires retained native ownership after the original global
root becomes zero. No freed S/root fields are reread after their deallocation.

The optional `Runtime::audioBoundaryObserver` is empty in production. Tests
throw a diagnostic observation exception after real source configuration or
after original startup releases Q+4C at828166F8. They do not skip source/worker
operations or report an unexecuted constructor successful. The EXm0 allocator
fixture observes before concurrent worker allocation; the separate Dac
lifecycle fixture lets that real worker run and invokes the original root
destructor through `EngineCpuCalls`.

Terminal runtime failure uses a separate lifetime guarantee: original workers
are cancelled/joined, then host DSP and all backend callbacks drain, then COM
and guest memory are released. That path does not claim original graph cleanup.
Normal complete application shutdown remains unverified.

## Verification

Build129 passed35 suites. Build130 adds the independent processor suite and
passes36/36 in52.24 seconds (`build/hundred-thirtieth-build.log` and
`build/native-audio-verification-130.log`).

- OriginalDacLifecycle:13 checks, actual source/worker/DSP/callback/downstream
  completion, original graph release and real OS worker join before root free.
- OriginalExm0Lifecycle:78,619 checks,133 real allocation/free pairs and null
  allocation fault; source observation retains SDK-global/layout checks.
- NativeDacProcessor:12,439 checks,12,288 exact component samples and9 actual
  natural OnBufferEnd receipts, plus ownership, target timing and stop races.
- OriginalDacGain:41 original calls,31,176 exact samples,21 integer anchors.
- NativeAudioOutputOwnership:594 checks and54 fixed-batch natural completions.
  Concurrent scheduling counts are not fixed acceptance requirements.

A bounded independent integration review found no concrete blocker in callback
threading, event release ordering or main/root joining; the actual lifecycle
test supplies execution evidence for those paths. The latest executable SHA256
for this integration is
`514c90cfc903c8683869c0a9cd1206781a3877a33a8543f98ad81c7ae689f133`.
`build/boot-075.log` records the actual muted application run. The EXm0 input,
decoder/context and feeder guards remain: startup output does not establish
asset decode, trimming, looping, audible content or normal whole-app teardown.
