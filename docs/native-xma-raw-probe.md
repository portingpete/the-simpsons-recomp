# Bounded native raw XMA comparison

**Measured result:** both pinned original mono files align with the recorded
stock FFmpeg PCM at **raw sample offset +576**, not +384. The existing patched
DLL's raw XMA1 and raw XMA2 paths produce **byte-identical complete raw outputs**
for these two inputs. This is a software-decoder comparison, not an original
hardware priming rule. All raw initial frames remain in the output files.

## Actual native capability used

`K:/SimpsonsNativeCopy/tools/probe_xma_raw_codec.py` compiles one small native
x64 ClangCL executable under `build/audio-probe/raw-*`. It includes the unchanged
`K:/DarkRecomp/runtime/native/xma_raw_decoder.cpp` and uses the existing headers,
import libraries and DLLs under
`K:/DarkRecomp/build_native/deps/ffmpeg-darkxma/`.
There is no codec rebuild, dependency installation, reference-project mutation,
parent CMake/runtime change, or output-device use.

The reference wrapper opens XMA1 with 28-byte extradata and requires the custom
`darkrecomp_raw_frames=1` option. A second probe path opens the same patched
library's XMA2 entry with its 34-byte extradata, one mono stream, 48 kHz and the
same required raw option. Neither path changes the original compressed bits.
Actual loaded DLL paths are logged; versions are avcodec 62.28.102 and avutil
60.26.102. `avcodec-darkxma-62.dll` SHA-256 is
`56a3901e9a9882baa31f873b2318499ea5a85a9cd4b77778e2f3d6022039accf`.

The reference's `PROVENANCE.json` records FFmpeg commit
`1c2c67c0b9f7f66ab32c19dcf7f227bcd290aa4c` and patched decoder SHA-256
`ea644dffdfa65dde0d223ebac93ba235ebc4d9f61e251a2aa3ccc56881671e9e`.
The probe verifies that source hash and records source/wrapper/import-library/DLL
hashes before and after execution. This verifies the inspected local capability;
it is not an independent reproducible-build attestation of those DLLs.

The patch bypasses the stock container FIFO/trim/tail path, clears first-frame
skipping, and returns 512-sample frames. The harness drains complete frames after
each accepted 2048-byte packet. Reads before input and repeated reads after input
exhaustion return no frame. **No null/EOF packet is submitted.** Reference packet
ownership is preserved; explicit bounds stop decoder errors or nonprogress.

## Inputs and results

Inputs are the same hash-pinned SNU originals and stock float32 outputs described
in `K:/SimpsonsNativeCopy/docs/native-xma-decoder-options.md`. The probe checks
both source and stock PCM hashes, independently validates the SNU structure, and
requires extracted packets to match the previous diagnostic adapter exactly.
Original packet headers including `08 00 00 00` are retained, with the previously
documented restored `FF` packet tails. Compressed payloads are not re-encoded,
and no audio is played.

- **short**, `audiostreams/ri_xxx_0/d_shri_xxx_0005795.exa.snu`: two packets;
  **17 raw frames = 8,704 samples**, versus 8,064 stock samples. At +576, all
  8,064 stock samples overlap, leaving 64 raw tail samples. Maximum absolute
  difference **1.1920928955078125e-7**, RMS difference **7.951671040476917e-9**.
  Full raw SHA-256:
  `79c1b75fc3dc434099bd6b31e1dcec0da6edcfc05d7061c143ff365ae589aedd`.
- **documented**, `audiostreams/01_xxx_0/d_as01_xxx_0003bb5.exa.snu`: eleven
  packets; **108 raw frames = 55,296 samples**, versus 54,901 stock samples.
  At +576, **54,720 samples** overlap. Maximum absolute difference
  **1.1920928955078125e-7**, RMS difference **1.0459273518635449e-8**. The remaining
  **181 stock tail samples are absent from this no-EOF raw output**; 64 of those
  samples are nonzero, with peak 7.32575572328642e-5. They cannot be replaced by
  assumed silence. Full raw SHA-256:
  `c7364922a185b457206b94e9173eaa0d206ca7af5bd134586d0ba521576fa6f9`.

The first retained 512-sample frame has **448 leading positive-zero float values,
then 64 nonzero samples**, in each case and both raw codec modes. Subsequent
frames are also retained. The report hashes the first three frames individually
and records the first nonzero raw sample index. The decoder carries an IMDCT
overlap buffer between frames, but this experiment does not independently derive
why these inputs have that exact 448-zero prefix or map it to a hardware first
subframe. Those zeros are observed native decoder output, not probe padding or
an instruction to trim 448 samples.
Both XMA2 runs match their corresponding raw XMA1 file in count and every byte.
This bounded result does not establish interchangeability for other packet
layouts or prove whether these EA streams were originally encoded as XMA1/XMA2.

## Alignment candidates and the 384 question

Offset means `stock[i]` is compared to `raw[i + offset]`. The probe searches every
integer offset from -1024 through +2048 on stock samples 2048..3071, a nonzero
interior window. It then measures the full available overlap for the five best
candidates and explicit offsets 0, 128, 256, 384, 512, 576, 640, 896 and 1024.
No shifted/trimmed replacement output is written.

+576 wins decisively: its window RMS errors are 1.51e-9 and 1.82e-8; neighboring
+575/+577 offsets are approximately 0.008 and 0.041 respectively. At **+384**,
full-overlap RMS errors are **0.112119** and **0.207171**; maximum errors are
**0.941851** and **1.109110**. Thus a 384-sample raw prefix discard does **not**
reproduce either stock waveform, even though the longer raw output minus 384
happens to leave enough samples for the stock length (11 extra).

The local patched source provides a software explanation to inspect: its raw
path keeps the first 512-sample frame, while the stock path skips that frame and
sets an additional 64-sample skip. The measured +576 relationship is consistent
with those codec policies. It must **not** be substituted for Godel's original
384/512 ABI evidence or used to invent an additional 192-sample guest discard.
The unmatched final tail also prevents claiming complete stock reproduction for
the longer file under the no-EOF contract.

## Reproduction, retained evidence and limits

From `K:/SimpsonsNativeCopy`:

```powershell
python -B tools/probe_xma_raw_codec.py
```

Prerequisites are the existing reference DLLs/headers, ClangCL/VS tools and
previous stock probe artifacts. No downloads occur. The compile bound is 60
seconds; each native decoding process is bounded to 20 seconds. The analysis
uses Python's standard library, not another model/API or codec implementation.

`build/audio-probe/raw-report.json` records hashes, provenance, commands, counts,
initial frames, candidate metrics, compared-span hashes and unmatched tail
statistics. `raw-*.frames.csv` records each returned frame's index, accepted
packet count and raw output offset; this is return timing, not a claim about
which source packet begins each encoded frame. Logs and generated harness
`raw-codec-probe.cpp` are also retained.

For checkpointing, include the durable script, this document, JSON/CSV reports
and logs. **Exclude derived `.packets` and `.f32le` media**, including both raw
decoder variants. There is no hardware PCM oracle, guest integration, loop/seek,
stereo/multilayer, mixer, output capture or game-audio fidelity proof here.
The parent owns the eventual independently pinned codec builder; this experiment
does not make the reference project or its project-specific raw option a
production dependency.
