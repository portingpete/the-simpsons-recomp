# Native XMA decoder ownership

Build115 passes all 30 CTest suites. The new `SimpsonsAudio` library decodes
original compressed packets with an independently built native FFmpeg codec.
It is not connected to the game callback yet: the actual game executable is
unchanged from build110 and boot069 still defines the reached EXm0 guard.
This milestone establishes packet/frame ownership, not gameplay or playback.

`audio/native_xma_codec.h/.cpp` owns one actual mono/stereo decoder per layer.
Construction requires explicit channel count, sample rate and XMA variant.
The supported factory domain is channels1/2 and rates24000/32000/44100/48000;
actual original waveform evidence currently covers two mono48000 sources.
No guessed format is assigned to an unconfigured guest instance.

The implementation checks exact header/runtime versions and requires the
`simpsons_raw_frames` option before opening a decoder. Accepted 2048-byte
packets are copied into FFmpeg-owned padded buffers. Backpressure leaves
acceptance with the caller: retry the same packet after reading available
output. A partial 512-frame output remains owned until read completely.
`read` writes interleaved floats and returns frames per channel; zero means
temporary unavailability. It never sends EOF, invents a tail, opens an output
device or applies container/engine trimming. Unexpected output shape or
nonfinite samples fail explicitly and require an explicit reset.

Each decoder serializes its operations with a mutex. Its owner must retain the
object through an operation; destroying an object concurrently with a raw
unleased caller is outside the API. Reset flushes the actual decoder and
pending frame, and is reserved for an explicit stream reset. Native SSE
floating-point controls are isolated during construction, send, read, reset
and destruction, then restored exactly. Codec arithmetic uses nearest rounding
without FTZ/DAZ, independently of the guest CPU caller's settings.

The dependency comes from `tools/build_native_audio_codec.py`, with exact
archive/source/patch/compiler and installed-file provenance. CMake invokes its
offline read-only verifier at configure and build time, imports the owned
MSVC libraries and copies the three verified DLLs beside the test executable.
The ownership test checks their actual loaded paths. The main executable does
not yet link `SimpsonsAudio`; no hardware-context layout or fake pool is added.
See [native-xma-codec-build.md](native-xma-codec-build.md) for dependency details.

`tools/prepare_audio_fixtures.py` independently reads two hash-pinned original
SNU files, validates EA metadata, strips only block/layer wrappers and restores
the previously established FF packet padding. It does not use a reference DLL,
the stock FFmpeg CLI or the earlier probe outputs. It checks original and
adapted hashes, records spans, and retains derived packets only under build.
The C++ fixture separately checks packet hashes and the previously recorded
raw output hashes:

- Short original clip: 2 packets, 8,704 raw samples,
  SHA256 `79c1b75fc3dc434099bd6b31e1dcec0da6edcfc05d7061c143ff365ae589aedd`.
- Documented original clip: 11 packets, 55,296 raw samples,
  SHA256 `c7364922a185b457206b94e9173eaa0d206ca7af5bd134586d0ba521576fa6f9`.

Both XMA variants match these exact float bytes. The test exercises immediate
caller-buffer destruction, full and varied partial reads, greedy send with
actual backpressure, empty starvation, reset after completion and after a
partial retained frame, invalid sizes/formats, all four caller rounding modes
with FTZ/DAZ/status flags, four concurrent independent owners, and use from a
thread different from construction/destruction. Output sentinels verify all
unreturned destination samples. Build115 reports **662,151 checks**.

The separate original `8233F250` converter fixture passes 664 cases and
225,264 exact signedBE16-to-float samples, including every signed16 input and
the original sparse 32-byte DCBZ writes. Builds111/112 failed because the test
oracle first omitted those extra writes, then incorrectly assumed a128-byte
DCBZ. Original instruction/reference evidence resolved the expectation;
no generator semantics were changed. Build113 and later pass the corrected
oracle. See [native-audio-trimming.md](native-audio-trimming.md).

Remaining work is the actual guest factory/instance and source lease lifecycle,
full-quota output staging, qualified frame origin and numerical conversion,
stereo/multilayer/loop/seek behavior, mixing and native device integration.
The original unbuffered caller ignores a short decode return and advances its
requested quota, so native starvation cannot be returned as false completion.
See [native-exm0-bridge-contract.md](native-exm0-bridge-contract.md). Stock
FFmpeg's measured576-sample offset is not permission to add192 to the original
384-sample skip. Software raw equality is not an Xbox hardware PCM oracle.

Reproduction, from the workspace:

```powershell
python -B tools/build_native_audio_codec.py --jobs 4
python -B tools/build_native_audio_codec.py --verify
.\tools\build.ps1 -SkipGenerate -Jobs 8
```

For a fresh source checkout, omit `-SkipGenerate` on the game build. The codec
build uses the verified local upstream archive when present; the ordinary
builder can extract that archive again after checkpoint restore. Verification
alone never extracts, repairs or downloads. Original audio and derived packets/
PCM are excluded from source checkpoints; regenerate fixtures from player data.
