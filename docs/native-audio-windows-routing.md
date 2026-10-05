# Native Windows 5.1 endpoint adaptation

`NativeAudioOutput::configureWindows51()` creates a real paused 48 kHz,
six-channel float32 source and retains XAudio2's default matrix to the actual
mastering endpoint. It is an explicit native PC policy: input components
0..5 are assigned **front left, front right, front center, LFE, side left,
side right**, in that order, with source mask `0x60F`. It neither permutes
the supplied PCM nor claims those labels were recovered from the console game.
Original speaker labels and console downmix remain unresolved in
`native-dac-routing.md`.

The selected mask follows Windows' `WAVEFORMATEXTENSIBLE` speaker-bit order.
XAudio2 constructs a default route from the source mask and destination layout
when no matrix override is supplied. Those are the Windows contracts adopted
here, not an inference about the older console SDK.
[Windows channel order](https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible),
[XAudio2 default mapping](https://learn.microsoft.com/en-us/windows/win32/xaudio2/xaudio2-default-channel-mapping).

## Caller contract

The caller keeps its MTA alive until output shutdown. For the planned worker:

```cpp
NativeAudioOutput output({.capacity=4, .muted=true, .deviceId={}});
output.configureWindows51();
const auto endpoint = output.endpoint();
const auto retainedRoute = output.routing();
// Submit owned 256-frame blocks; start explicitly when the worker is ready.
output.start();
// Existing submit / poll / wait / query / retire / stop contract applies.
```

Capacity remains caller-selected: the default is two, and four is supported and
tested for the external processor. It counts all accepted receipts until
retirement, including naturally completed receipts. This backend does not model
the processor's separate logical two-buffer queue or perform its DSP.

`configure(const Routing&)` remains unchanged, including its explicit matrix
validation and exact native readback. The two configuration methods are
alternatives: either may succeed once per graph; neither may replace the other.
Bad state/duplicate calls reject before changing the graph. Native creation or
readback failure inside Windows configuration closes the graph; successful
configuration publishes its retained route only after validation. A stopped
graph cannot reopen; construct another owner with a fresh generation.

There is no forced endpoint channel configuration, OS volume change, device
selection change, or automatic migration. The existing owner chooses its actual
endpoint at creation. A null source send list targets that graph's only
mastering voice; the source is initially stopped.
[CreateSourceVoice](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2-createsourcevoice).

Mute behavior is intentionally unchanged: with `muted=true`, both source and
master voice volumes are zero, with readback checked; otherwise both start at
unity. This graph-local test mute is not the original category multiplier or
external DSP gain. All validation here used muted graphs; no audible result is
claimed.

## Actual matrix ownership and validation

The implementation never calls `SetOutputMatrix` on the Windows-policy source.
After actual voice creation it checks six input channels and 48000 Hz, then
calls `GetOutputMatrix(master, 6, endpoint.channels, ...)`. The destination
layout is bounded to 1..18 channels with exactly that many supported mask bits,
as in the existing endpoint owner. The retained vector therefore has exactly
`6 * endpoint.channels` floats, at most 108.

The readback buffer starts as NaNs; every returned element must be finite and
within XAudio2's documented amplitude bounds, `[-2^24, +2^24]`. No coefficient
is clamped, normalized, transposed, replaced or assumed to equal a console
coefficient. `routing()` returns an owned copy of this readback and both masks.
The earlier explicit-routing API deliberately retains its narrower `[-1,1]`
caller-input contract.
[GetOutputMatrix](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2voice-getoutputmatrix).

The retained flat order is the existing API order `matrix[6*destination+source]`.
Microsoft's current GetOutputMatrix page displays a different indexing formula
from its SetOutputMatrix page. The fixture's real asymmetric Set/Get round-trip
passes without transposition, and the independent default graph returns exactly
the same raw array as this owner. The implementation retains the actual API
array rather than transforming it to match the conflicting prose.
[SetOutputMatrix indexing](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2voice-setoutputmatrix).

Callbacks, receipt identity, PCM copying, wake signaling and shutdown barriers
are unchanged. The new method runs under the existing public-operation mutex
and host floating-point scope. No callback allocates or calls game code. Matrix
storage is host-owned; no console device, guest pointer or fake native handle
is introduced.

## Muted standalone validation and freeze

Run `python -B build/audio-output/build_test.py`. It compiles the production
source separately without fixture access, then builds/runs the fixture using
ClangCL C++20 `/O2 /fp:strict /W4 /WX` and system XAudio2. No full CMake build was
run by this task.

Final run: **PASS, 591 checks, 54 natural completions in fixed batches**, plus
25 accepted/retired submissions in the producer/worker/stop race. The four new
fixed completions use Windows routing and capacity four; the previous 50 use
the unchanged explicit-routing path. Race counts vary with scheduling and
include shutdown cancellation where applicable.

The actual endpoint was stereo, mask `00000003`, mastering input 48000 Hz.
The measured 6-to-2 default matrix, in raw float bits, was:

```
destination 0: 3F800000 00000000 3F353BEF 00000000 3F353BEF 00000000
destination 1: 00000000 3F800000 3F353BEF 00000000 00000000 3F353BEF
```

These values are evidence from this endpoint, not hardcoded production
coefficients or a guarantee for other endpoints. A separate real XAudio2 graph
created the same source format with no matrix setter; its readback matched
bit-for-bit, with buffer guard elements preserved. The fixture also checks
finite bounds/dimensions, immutable copied routing, both configure-once orders,
paused owned PCM after caller mutation/release, unchanged component order,
four-slot admission/retirement, reopen generations, pending stop, failed/stopped
configuration rejection, and MXCSR restoration. Existing malformed explicit
routes, real nonexistent-endpoint failure, callback-error injections and
concurrency tests continue to pass. Device removal and corrupt native readback
were not physically induced; injected callback errors remain labeled as such.

Frozen source/header/test hashes and the standalone executable hash are in
`build/audio-output/verification.json`; compile/test logs are adjacent. This task
changed only `audio/native_audio_output.h`, `audio/native_audio_output.cpp`,
`tests/test_native_audio_output.cpp`, this new document, and scratch/build outputs
under `build/audio-output`. Runtime, processor, CMake, configuration, original
and reference files were not modified.
