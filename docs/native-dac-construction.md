# Native Dac0 construction prefix

Historical build125/126 boundary. Build130 supersedes this source guard with
the actual native source, original worker and graph teardown integration;
see `native-dac-integration.md`. The prefix evidence below remains applicable.

The original Dac0 constructor now executes its CPU setup and acquires a real,
muted Windows XAudio2 engine/mastering voice. It stops at the next source-creation
call, before accepting any PCM, allocating the original event or starting the
original Dac0 worker. This is partial construction, not functioning game audio.
The output library's own host wake event and host callbacks already exist.

## Original boundary and implementation

`runtime/engine_audio_output.cpp` owns the native graph and its COM apartment.
The three byte-pinned hooks in `config/simpsons.toml` retain the original
constructor body:

- `823456D0`: entry preflight, then fall through. It requires the actual generic
  graph-wrapper caller (`LR=8233DC44`), descriptor `82D069B4`, initialized CPU
  header and real audio root `Q=82E31BCC`. S must be 16-byte aligned and mapped
  for 0x3108 bytes, outside image, stack and kernel regions. The VM region scan
  holds `Runtime.vmMutex`. The complete immediate 0x120-byte original stack frame
  is checked before publishing construction or running the original save helper.
- `823458C0`, original word `488FABA1`: replace SDK initialization call
  `82C40460`, then resume `823458C4`. Original mixer allocation and setup have
  already executed. The bridge requires its real 128-byte-aligned 0x30080-byte
  mixer, matching root, six-channel/48kHz default CPU profile, and zero output
  SDK globals. Successful MTA initialization and actual native engine/master
  acquisition precede the return. Native capacity is two, and mute is mandatory
  for this automated-launch profile. No source exists yet.
- `8234591C`, original word `488FAB4D`: validate the actual stack source record
  and stop explicitly with `Native Dac0 source routing/gain/worker lifecycle is
  not implemented`. The record specifies float PCM, six components, 48kHz, two
  packet descriptors and original processing/completion callbacks. Validation
  also requires an acquired native engine with no configured source, running
  playback or owned submission. The original SDK source call never executes.

No console singleton, voice layout, MMIO address, native handle or SDK identity
is written into guest storage. Retained original CPU code writes exactly six
S words before this guard: S+0 vtable `821DCB90`, S+C pointer S+28, S+24 real mixer,
S+28 float3, S+30 float48000 and S+38 zero. The SDK output globals `82E2D9F0/9F4`
remain unchanged. r3=0 at the replaced SDK initialization call is not consumed as
an SDK object; retained code overwrites it with the source record address.

## Lifetime and failure handling

Construction is restricted to the original main thread and active Runtime/CPU
context. Each later hook verifies the retained frame, S, Q and descriptor.
The state mutex protects native ownership; it is never acquired by host audio
callbacks. Runtime's audio lookup mutex is released before state-method entry.

COM `S_OK` and `S_FALSE` both acquire one apartment reference. A failing HRESULT
throws without one. If native graph construction fails, partial native ownership
unwinds and the successful COM reference is balanced. COM initialization,
exception cleanup and terminal cleanup run with private default host FP controls,
restoring the caller's exact MXCSR afterward. The backend separately isolates
its own native calls. COM failure at this integration seam has not been injected.

At terminal Runtime destruction, existing guest workers are cancelled and joined
before output ownership is reset. Native source/master/engine destruction drains
host callbacks before `CoUninitialize`, on the same main thread that initialized
it, and before guest memory is released. Wrong-thread apartment destruction is
an explicit fatal invariant. No Dac0 worker exists at this guard, so no Dac0
self-join or Q4C/Q48 teardown is performed. Original partial graph/mixer rollback
is explicitly not claimed; terminal cleanup reports that state.

## Verification

The original EXm0 lifecycle fixture intercepts the actual indirect Dac0
constructor and invokes its real AOT body. It catches only the exact new guard
message with retained caller/owner ABI, checks the complete S footprint against
the six-word expectation, validates original mixer/root storage, native endpoint,
generation and mute, and compares both SDK globals. It throws a test-only
observation exception instead of returning constructor success. Its later EXm0
tests use a fresh CPU call context, never the exception-unwound constructor frame.

Build123 compiled and reached actual native acquisition, but its lifecycle test
incorrectly expected `lastFunction=8234591C`. That field names the last entered
CPU helper (`82353BC8`); the failure PC is separate. Build124 corrects this
fixture assertion and passes all 34 suites, including the original gain fixture.
The independent ownership review found the incomplete entry-stack preflight.
Build125 adds the complete immediate-frame check and a negative actual-entry
fixture with SP only 0x100 bytes above the mapped stack bottom. It requires no
stack/owner writes, no caller SP/LR change, and no published construction. This
does not promise to preflight every nested CPU helper's deeper stack usage.

Build125 passes all 34 suites in46.28 seconds. The lifecycle reports78,618 checks
and133 actual allocation/free pairs. Actual muted boot073 acquires owner
`E4624C30`, root `E4627DE0`, mixer `E4048000`, native generation1 and a two-channel
endpoint with mask3, then exits1 at the intended source guard. Existing workers
exit before terminal output cleanup. There is no new visible game frame or audio
playback. The executable SHA256 is
`0cfcafbfa833824d2bed2bc2d50ac4b9f12a2537c451ff45a01c5aa00ffba612`.
See `build/hundred-twenty-fifth-build.log`, `build/boot-073.log` and
`build/native-audio-verification-125.log`.

Build126 adds the bounded native gain helper to the output library and passes
all34 suites in45.99 seconds. Muted boot074 verifies the newly linked executable,
with the same real native owner/profile and explicit source guard. Its SHA256 is
`e7df276b5a9308824ef6376df08c48f063eb81183549408186803e9f59842902`.
Logs are `build/hundred-twenty-sixth-build.log`, `build/boot-074.log` and
`build/native-audio-verification-126.log`. The gain helper is not yet invoked by
the guarded source; this build does not claim new playback behavior.

The full source/worker gate requires verified gain processing, physical channel
and category-volume policy, submission retirement, CPU callback delivery and
normal graph-driven destruction. Dac0 destruction can execute on its own worker;
native callbacks/data must be retired there and any real OS join performed from
an outer owner. Startup observation must occur after the original Q4C release.
See `native-dac-sdk-contract.md`, `native-dac-gain.md`,
`native-audio-output-ownership.md` and `native-audio-output-review.md`.
