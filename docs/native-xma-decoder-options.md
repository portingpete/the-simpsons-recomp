# Native XMA decoder capability — bounded probe

Stock native x64 FFmpeg successfully decoded **two original Simpsons EA-XMA SNU
files**, with finite, nonzero PCM and exact agreement with their declared sample
counts. This establishes a usable software-codec option. It does not implement
`XMACreateContext`, prove the original streaming/priming contract, or produce game
audio through an output device. Original ABI recovery belongs to Godel's separate
`docs/native-audio-boundary.md`; the parent's XAudio2 fixture is separate too.

## Available capability

The existing executable is
`C:/Program Files (x86)/Steam/steamapps/common/ShareX/ShareX/ffmpeg.exe`.
It reports **n8.1.1-7-g3728de467d-20260519**, x86_64 Windows, libavcodec
62.28.101 and libavutil 60.26.101. Its installed decoder list includes `xma1`,
`xma2`, and EA ADPCM R3. The actual experiments select **xma2 (native)** and
receive 48 kHz mono planar float, serialized to interleaved float32 little endian.
XMA1 availability was enumerated; a separate XMA1 decoding experiment was not run.
Version/configuration, executable SHA-256, commands and results are retained in
`K:/SimpsonsNativeCopy/build/audio-probe/`.

Upstream [wmaprodec.c](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/wmaprodec.c)
implements both XMA decoders with native transform/bitstream code. Initialization
requires channel configuration and extradata and sets 2048-byte packet alignment.
The decoder has delayed output, sample trimming and an output FIFO; successful
whole-file decoding does not establish the output availability or priming
semantics required by a guest context API. No runtime CPU translator or console
GPU component is needed for this codec.

For later native integration, a separately pinned `libavcodec`/`libavutil` build
with XMA decoders and their selected dependencies is the narrow candidate.
[FFmpeg configure](https://github.com/FFmpeg/FFmpeg/blob/master/configure)
supports selective decoders and disabling programs, network, demuxers and other
libraries. A supplied-packet codec service does not need the CLI/RIFF adapter.
The installed CLI's configuration enables GPL/version3; it is a probe tool,
not a proposed binary dependency. No dependency was installed or rebuilt here.

The [send/receive API](https://ffmpeg.org/doxygen/trunk/group__lavc__encdec.html)
requires draining available frames on backpressure and preserving packet/frame
ownership. Temporary lack of source input is not EOF: a null packet enters
draining mode and resumption then needs reset. The future bridge must establish
which side owns priming, final trimming, loops and decoder reset before using
those operations.

## Existing original assets and the transport adapter

`K:/SimpsonsNativeCopy/docs/assets.md` and `analysis/assets.json` already establish
7,413 SNU files (7,204 mono, 147 stereo, 62 four-channel) and 17 MUS containers
holding 2,057 six-channel streams. Their inspected EAAC headers are version 0,
codec 3, streamed type 1, 48 kHz. Sixty-three SNU files loop. These are container
facts, not an assertion that every asset is already decodable. Movie audio is
separately identified as EA ADPCM R3, not XMA.

[Vgmstream's EAAC reader](https://github.com/vgmstream/vgmstream/blob/master/src/meta/ea_eaac.c)
maps codec 3 to EA-XMA and separates channel pairs into layers. Its current
decoder selection uses XMA2 because EAAC version alone does not reliably identify
XMA1 versus XMA2. Successful decoding here therefore does not independently
classify the original encoder generation. Six channels must not automatically
be assigned a Windows 5.1 speaker order; layered dynamic music is also possible.

For each validated mono block, the probe retains the original packet header and
compressed payload after the four-byte layer-size word. It restores the removed
packet tail with `FF` bytes to a 2048-byte boundary, following
[ea_eaac_streamfile.h](https://github.com/vgmstream/vgmstream/blob/master/src/meta/ea_eaac_streamfile.h).
It then adds a **new diagnostic XMA2 RIFF envelope**, using the layout in
[ffmpeg_decoder_utils.c](https://github.com/vgmstream/vgmstream/blob/master/src/coding/ffmpeg_decoder_utils.c).
Encoded/play/loop sample fields remain zero; the diagnostic transport block size
is 65536. These wrapper values and restored padding are explicitly not original
asset bytes. No compressed audio bits were re-encoded, and no sample-count output
limit, resampling, gain adjustment or channel remix was applied.

## Actual results and reproduction

Read-only originals came from
`K:/SimpsonsNativeCopy/Simpsons Game, The (USA)/`:

- `audiostreams/ri_xxx_0/d_shri_xxx_0005795.exa.snu`: **5,888 bytes**, two EAAC
  blocks, 4,096 adapted packet bytes. FFmpeg exit 0; **8,064 / 8,064** decoded /
  declared samples, peak absolute amplitude 0.861420, RMS 0.095464.
  Source SHA-256 `4007581a5527caaab34b130f0c603a694f7273b1188abde36b3f5da97c6855e4`.
- `audiostreams/01_xxx_0/d_as01_xxx_0003bb5.exa.snu`: **19,328 bytes**, eleven
  EAAC blocks, 22,528 adapted packet bytes. FFmpeg exit 0; **54,901 / 54,901**
  samples, peak 0.771767, RMS 0.139538.
  Source SHA-256 `bfd2acf74af905f1521fbe1502c1f140a82a73f003fc1cbe2dec866e92b9f7a9`.

Both hashes matched the existing inventory before extraction and remained
unchanged afterward. All output samples were finite; both outputs contained
nonzero samples. Commands use `-xerror` and a 20-second subprocess bound.
Neither source was fabricated. Exact sample counts and clean decode are useful
checks, **not a hardware PCM equivalence oracle or a listening/scanout claim**.

Run from `K:/SimpsonsNativeCopy`:

```powershell
python -B tools/probe_xma_codec.py
```

The frozen probe is preserved under tools/probe_xma_codec.py with workspace paths
resolved from that authored location. Reproduction passed from that path. Its
original copy remains in build/audio-probe/probe_xma.py. The script imports the
existing read-only asset parser, requires the two pinned
source hashes, and writes only its own probe files. `report.json` records every
source payload span/hash and restored padding span, native command, PCM hash,
sample statistics and unchanged-source checks. `short.decode.log` and
`documented.decode.log` show the actual native decoder selection.

The parent subsequently submitted both exact PCM files to a separate muted
Windows XAudio2 probe. Both produced ordered native buffer completion and empty
queues; see `native-audio-output-probe.md`. This extends output capability
evidence without establishing game streaming, speaker routing or a hardware
PCM equivalence reference. No media playback was audible.

## Read-only local reuse evidence and remaining limits

`K:/DarkRecomp/runtime/native/xma_raw_decoder.cpp` provides a useful ownership
pattern: owned padded packet copies, retained partial frames, explicit errors,
and no EOF on temporary input exhaustion. Its companion
`K:/DarkRecomp/tools/build_xma_codec.py` deliberately patches a pinned FFmpeg to
expose raw 512-sample frames, bypass stock FIFO/priming/tail behavior, and restrict
that mode to one mono/stereo stream. Its `darkrecomp_raw_frames` option and
`avcodec-darkxma-62.dll` are **project-specific**, not stock FFmpeg APIs.
`K:/DarkRecomp/runtime/native/XMA.md` records that project's comparisons; those
results were not rerun or adopted as Simpsons timing evidence.

`K:/Simpsons/RexGlueCurrent/thirdparty/FFmpeg/libavcodec/wmaprodec.c` similarly
contains a custom `AV_CODEC_ID_XMAFRAMES` / `ff_xmaframes_decoder` returning planar
float. `src/audio/xma_context.cpp:PreparePacket` supplies an extra frame-padding
byte; `PrepareDecoder` configures that custom codec. This is an alternative raw
frame contract to examine only if original ABI evidence needs it. The surrounding
context/register runtime is not a required dependency of stock XMA decoding.

The bounded probe does not cover stereo/multilayer interleaving, channel routing,
loop/seek/subframe restart, starvation/backpressure, streaming output quotas,
guest buffer ownership, BE16 quantization, mixer timing, cancellation or device
playback. No production code, runtime hooks, CMake, original assets or reference
projects changed. The parent's `xaudio2_probe.*` and output-probe document were
not touched. Codec availability is established; choosing whole-packet versus
raw-frame integration remains dependent on the original stream contract.
