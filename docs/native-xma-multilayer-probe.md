# Original XMA stereo and multilayer probe

Frozen 2026-09-10. Six original stereo layers passed through the owned native codec. For every layer, raw XMA1, raw XMA2, and XMA2 with partial reads/backpressure produced **byte-identical PCM**. Independently searching the stock CLI comparison found **raw offset +576** for all six layers. This is a software comparison, not a guest trim or hardware quantization rule.

The bounded deliverables are [the probe](K:/SimpsonsNativeCopy/tools/probe_xma_multilayer.py), this document, and [the report and local artifacts](K:/SimpsonsNativeCopy/build/audio-multilayer/report.json). The builder/install, originals, core codec, runtime and CMake were not changed. No audio device was opened.

## Original selection and framing

Paths below are relative to `K:/SimpsonsNativeCopy/Simpsons Game, The (USA)/`. Selected files were fully hashed before parsing and again after decoding. Hashes must match both the pinned case and existing asset inventory. The existing inspector validates EA headers, index entries, block/layer bounds and declared sample sums; the probe independently walks the selected byte framing.

- **Stereo, complete nonloop SNU:** `audiostreams/fe_xxx_0/d_mvfe_xxx_000091c.exa.snu`, 41,248 bytes, SHA-256 `01b6d13b00b790e0e3d76a957e96186ab5baec59269d7af537d8a889d66e937d`. Two channels, 48,000 Hz, 89,999 declared sample frames, 18 EA blocks starting at `0x20`. One stereo layer becomes 28 adapted 2,048-byte packets.
- **Four-channel bounded excerpt:** `audiostreams/80b_crow/amb_80b_crowd_qd_01.exa.snu`, 1,031,904 bytes, SHA-256 `33b002d487566dd1fe52116028925fafbc75dd17db5df9a79befb9195c66745f`. All **62 four-channel SNU headers in the inventory were independently read and are looping**. This smallest four-channel file is therefore explicitly a loop-body prefix, not a nonloop file. Its full extent is 997,219 samples in 196 blocks. The probe excludes the complete one-sample introduction and selects blocks 1..4 beginning at `0xA0`, exactly the original loop-body offset. These four blocks declare `4736 + 3*5120 = 20096` samples; each of two stereo layers becomes eight packets. No loop replay or original end-of-stream is exercised.
- **Six-channel complete nonloop MUS stream:** `audiostreams/bin.mus`, 14,631,808-byte container, SHA-256 `df2a94000d13059fca163d17eeaf4892a3bc0a4d3bf2c920016e953625f3d965`. Select index **52**, ID `5b64e1bd`, header at `0xB40`, audio at `0xB32880`, audio length `0x48C0`. Six channels, 48,000 Hz, `4736 + 4569 = 9305` samples in two blocks. Each of three stereo layers becomes four packets. Only this small stream's derived media is written.

For each block, the probe reads the big-endian block flag/24-bit extent and sample word, then the stereo-layer records in file order. The layer word's `word >> 2` extent includes its four-byte header; the qualified low selector is 3. Each selected payload starts `08 00 00 00`. Every original payload bit, including internal packet headers, is copied unchanged. Only the documented stripped packet tail is restored with `FF` bytes to the next 2,048-byte boundary. This does not insert PCM silence, merge layers, reset between EA blocks, or re-encode packets.

`source_spans` records each original block/layer offset, header word, payload length/hash, adapted offset and exact FF count. Malformed extents, unsupported flags/selectors/prefixes, unexpected padding and sample-sum mismatches reject. This is a narrow EA adapter; the actual codec performs compressed-bitstream decoding.

## Native and stock execution

The probe compiles its small harness plus the unchanged `audio/native_xma_codec.cpp` directly using ClangCL and the frozen MSVC import libraries. It opens a separate real `NativeXmaCodec` for each layer. All selected contexts are **stereo**; the earlier two mono fixtures remain separately qualified by the frozen builder/core tests.

The exact libraries are `build/audio-codec/install/lib/avcodec-simpsonsxma.lib` and `avutil-simpsonsxma.lib`. Every run verifies the actual loaded module paths for `avcodec-simpsonsxma-62.dll`, `avutil-simpsonsxma-60.dll`, and `libwinpthread-1.dll` against that owned install. No reference DLL is copied or loaded. The [frozen build contract](K:/SimpsonsNativeCopy/docs/native-xma-codec-build.md) specifies the source patch and raw option. Its read-only verifier passed before and after the probe: **10,135 source files and 188 artifacts**.

The independent stock executable is `C:/Program Files (x86)/Steam/steamapps/common/ShareX/ShareX/ffmpeg.exe`, version `n8.1.1-7-g3728de467d-20260519`, SHA-256 `958aeaa0ea3e74e5e273157e6f045c94804bb72913261d8ef6c3d772743d13df`. It decodes a newly generated diagnostic RIFF containing exactly the same layer packets. The envelope declares one stereo XMA2 stream at 48 kHz, with encoded/play/loop sample limits zero. There is no remix, requested output duration or imposed output count. Stock decoding reaches this finite diagnostic file's EOF; the raw native contexts receive **no EOF**.

The report pins the script, inspector, inventory, compiler, core sources, builder, install provenance, DLLs, import libraries and stock CLI. Those inputs and all three original files were unchanged across the run. Commands, loaded module paths, per-read CSVs, finite float PCM, packet spans and output hashes remain under `build/audio-multilayer/` (about 5.5 MiB).

## Measured frames, alignment and channel order

Here a **sample frame** contains one sample per channel; a **codec frame** contains 512 sample frames. The report's `raw_frames` uses the former unit and `codec_frames_512` uses the latter.

- Stereo layer 0: **90,624 raw sample frames / 177 codec frames**, versus 89,999 stock frames. At +576, all 89,999 stock frames overlap; 49 raw frames remain after that comparison. Maximum absolute float difference is `1.7881393432617188e-7`.
- Four-channel layers 0 and 1: **20,480 raw / 40 codec frames each**, versus 20,096 stock frames each. At +576, 19,904 stock frames overlap, leaving **192 stock tail frames per layer** outside the available raw extent. Maximum differences over the overlap are `9.313225746154785e-9` and `7.450580596923828e-9`.
- Six-channel layers 0, 1 and 2: **9,728 raw / 19 codec frames each**, versus 9,305 stock frames each. At +576, 9,152 stock frames overlap, leaving **153 stock tail frames per layer** outside the raw extent. Maximum differences are `2.9802322387695312e-8`, `3.725290298461914e-8`, and `5.960464477539063e-8`.

The alignment is measured over every integer offset from **-1024 through +2048**, using a nontrivial 256-frame interior window chosen by maximum energy, then checked over the full overlap. Offset +576 wins independently on every layer. No output was trimmed to force that result. Cross-build float differences remain reported rather than described as bit equivalence with stock.

The same-order comparison's channel RMS errors range from approximately `1.3e-9` to `1.24e-8`. Swapping the two native channels increases RMS to approximately `0.07694` for the stereo SNU, `3.97e-5` / `2.11e-5` for the four-channel layers, and `0.06489` / `0.07213` / `0.05841` for the MUS layers. Thus even the nearly matching four-channel pairs provide asymmetric evidence for **within-layer channel 0/1 order**. All six layer PCM hashes are distinct.

Layer `i` retains file order and logical channel slots `2*i, 2*i+1`. This agrees with the original plane-address arithmetic at `8233FD24..8233FD58`, separately pinned in [the trimming report](K:/SimpsonsNativeCopy/docs/native-audio-trimming.md). This probe does not repeat that original-CPU proof or assign speakers, a six-channel speaker mask, or mixer routing. The diagnostic stereo mask is solely a CLI envelope field.

Raw PCM SHA-256, identical across all three schedules per layer:

```text
stereo/0      6fd267cb6df20d4814e6552405bc359e5570ea0498b12c6fbe4ca3258dc753ff
four-prefix/0 43bc3592c23f4b962ba984214a143fc8cadefaf0b71efec56588dc7a33b05a59
four-prefix/1 8292b4bbe783ff4d74a93838c31fcca329948ca99a5d82a0648b7532243bd33d
six-mus52/0   5e82b247db32e9bbdc79e0cd463dcc2b5645c89b7cf24753d485d080bbb69689
six-mus52/1   a04fcfea9dd0f8ef451d9e8baa3d23100077827981fbbc8fe44258efcec12b35
six-mus52/2   13dbbecf15f6f13072d62e4cc024e4173c14fe20cf2f8f1138cb4e98f44fd1d3
```

## Starvation, backpressure and the unmatched tails

Eighteen native runs accepted **168 packet submissions**, produced **482,304 sample frames** in aggregate, returned **494 empty reads**, and exercised **16 `NeedDrain` responses**. These totals include the three repetitions of each layer. The alternative schedule sends ahead, retries the same packet only after productive draining, and reads capacities `1,7,31,511,3,127,513,17`. Unwritten output elements retain their sentinel; partial read tails remain owned by the codec. Its final PCM is byte-identical to the ordinary schedule.

Three reads before any input, empty reads between packets, and three reads after all available packets return zero without EOF or invented PCM. Later packets successfully resume output after intermediate starvation. This qualifies packet-boundary starvation and partial output consumption for these inputs; it does not qualify arbitrary byte fragments, guest queue publication, or loop/reset behavior.

The unmatched stock tails are **not silence**. Both four-channel layers have all 384 float values nonzero in their 192-frame tails, with peaks approximately `0.04176` and `0.05564`. Each six-channel layer has all 306 float values nonzero in its 153-frame tail, with peaks `0.04476`, `0.12552`, and `0.23134`. The finite stock diagnostic drains its EOF path; the raw contexts intentionally do not. In particular, the four-channel diagnostic EOF is the end of an excerpt, not the original source's EOF.

These observations cannot authorize padding missing PCM, treating temporary starvation as EOF, or subtracting another 192 guest samples. Original 384-sample start skip, 512-sample carry, source leases, complete guest quotas and error/reset behavior remain the separate [EXm0 bridge contract](K:/SimpsonsNativeCopy/docs/native-exm0-bridge-contract.md). The relation to original hardware signed-16 quantization and PCM origin is still unproved. This task establishes the six software layer streams and their stock relationship only.

## Reproduction and frozen validation

From `K:/SimpsonsNativeCopy`:

```powershell
python -B tools/build_native_audio_codec.py --verify
python -B tools/probe_xma_multilayer.py --self-test
python -B tools/probe_xma_multilayer.py
```

The first command is read-only. The probe writes only its fixed `build/audio-multilayer/` directory, including compiler temporary files. It performs no whole-game build. It requires the installed toolchain, pinned originals, inventory, unchanged core, owned codec and stock CLI; missing or changed inputs fail explicitly.

Final run: **four self-test groups passed** (bit preservation/FF restoration; asymmetric layer split and new RIFF; malformed framing with six rejection cases; output bounds/nonfinite PCM), **18 native decode runs passed**, all six raw variant/schedule hashes matched, all six stock comparisons measured +576, and the final codec verifier passed. `report.json` records the actual metrics rather than hard-coding +576 as a decoder rule. Builder/install and all non-owned production files remain frozen and unchanged by this probe.
