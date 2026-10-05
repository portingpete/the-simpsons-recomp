# Owned native XMA codec build

Updated2026-09-19: reach-game301 (local development record) adds an XMA1/XMA2
exhausted-packet boundary correction. Strict frame validation and compressed
bytes are retained. This also corrects the corresponding stock-mode packet
transition. The builder now verifies migration from the previous owned patch
and explicitly rebuilds the patched translation unit despite archive timestamps.
Current exact artifact hashes are in `build/audio-codec/install/PROVENANCE.json`;
the frozen build details below describe the original2026-09-10 revision.

Frozen 2026-09-10 after successful isolated compilation, native factory/PCM checks, MSVC import-library linking, and offline verifier fault injection. This supplies a native software codec library; guest audio construction, queues, timing and playback remain separate integration contracts.

## Commands and exact integration paths

From `K:/SimpsonsNativeCopy`:

```powershell
python -B tools/build_native_audio_codec.py --jobs 4
python -B tools/build_native_audio_codec.py --verify
```

The builder compiles only its owned `build/audio-codec/` directory. Include `K:/SimpsonsNativeCopy/build/audio-codec/install/include/` and link these x64 MSVC import libraries:

- `K:/SimpsonsNativeCopy/build/audio-codec/install/lib/avcodec-simpsonsxma.lib`
- `K:/SimpsonsNativeCopy/build/audio-codec/install/lib/avutil-simpsonsxma.lib`

Deploy these three DLLs together using the application's controlled DLL search path:

- `K:/SimpsonsNativeCopy/build/audio-codec/install/bin/avcodec-simpsonsxma-62.dll`
- `K:/SimpsonsNativeCopy/build/audio-codec/install/bin/avutil-simpsonsxma-60.dll`
- `K:/SimpsonsNativeCopy/build/audio-codec/install/bin/libwinpthread-1.dll`

Both native smoke executables verified `avcodec_version()==LIBAVCODEC_VERSION_INT==4070502` (62.28.102) and `avutil_version()==LIBAVUTIL_VERSION_INT==3938918` (60.26.102). GNU import archives are additionally present; the named `install/lib/*.lib` files are generated with `llvm-lib /machine:x64` from the actual built exports and were linked and executed successfully with ClangCL/MSVC. No parent CMake or runtime source was edited.

## Source and patch identity

The build uses FFmpeg commit `1c2c67c0b9f7f66ab32c19dcf7f227bcd290aa4c`. The original archive was copied read-only from the explicitly authorized DarkRecomp archive, with SHA-256 `1291ae49c285f7bd55c7c059aa43f1a0fd784a1ae22d5c76297dcd11c531248a`. No reference decoder binaries or patched reference sources became build inputs. If neither the owned archive nor that seed exists, a normal build fetches only the [official pinned archive](https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/1c2c67c0b9f7f66ab32c19dcf7f227bcd290aa4c) and requires the same hash. This build needed no download or package installation.

Only `libavcodec/wmaprodec.c` is patched:

- Original SHA-256: `803547a38dea1294891c00402d6b3576a16053b0f00b395768c4983740c86553`.
- Patched SHA-256: `793553e01701b21b778189e5dd8f940ab257b35e5739efde58e03a4e3e8c50b4`.
- Unified patch SHA-256: `ec2d5a8c8b5bc9e6d2c18f88bb5c766b92b9bdc54f54590b126b9da784d2a70b`.

The public boolean AVOption `simpsons_raw_frames` defaults to zero. When enabled before open, XMA1/XMA2 accept one mono or stereo stream, route through upstream packet/reservoir/frame math, emit 512-sample FLTP frames, retain the first frame, and suppress the stock EOF overlap tail. Raw multistream contexts fail initialization. No transform, coefficient, overlap arithmetic or sample normalization was changed. Default stock decoding follows the existing stock path. The old reference option name is absent.

The caller must retain codec state across packet submissions, obey send/receive backpressure, own padded packets, preserve partially consumed output, and use flush only for explicit reset. Library creation provides no evidence for replacing original guest ctor/dtor fields or CPU queue helpers.

## Build inputs and verification

The retained `install/PROVENANCE.json` records compiler hashes, configure/import commands, selected environment, source/patch identities, validation and 188 artifact hashes. `source-manifest.json` covers all 10,135 source files; the original archive and full patched source tree remain under `build/audio-codec/`. The install retains the patch, upstream `COPYING.LGPLv2.1` and `LICENSE.md`, and installed winpthreads notices. Preserve these source/provenance materials with binary redistribution; this report does not replace their license terms.

Actual tools: MSYS2 GCC 15.2.0, binutils 2.46, make 4.4.1, existing LLVM import tools, and Git's installed `cmp`. Configure enables only XMA1/XMA2 decoding plus shared avcodec/avutil, suffix `-simpsonsxma`; programs, network, autodetection, assembly and the other FFmpeg libraries are disabled. Win32 threading is selected. GCC runtime linkage is static; the installed winpthreads DLL supplies avutil timing imports. PE imports are limited to the owned avutil DLL, that winpthreads DLL, and Windows `KERNEL32`, `msvcrt`, `bcrypt`. No emulator or shader/compiler runtime dependency was introduced.

Configure/build temporary files stay in `build/audio-codec/tmp/`. Source timestamps and `SOURCE_DATE_EPOCH` come from the pinned archive; PE linker timestamps are suppressed for owned builds. Inputs and output hashes are recorded, without claiming bit-identical rebuilding under arbitrary toolchains.

`--verify` only reads local files: it verifies archive identity, full source file set/content, exact patch, builder identity, configuration and installed/support artifact hashes. It neither builds nor repairs nor downloads. Unknown source edits also fail a normal rebuild before compilation. The verifier detects changes relative to this local provenance record; it is not a signed provenance system.

`build/audio-codec/verify-contracts.py` passed checks that disable writes, subprocesses and networking during verification, compare all checked file sizes/mtime values, and inject temporary corruption into the owned patch, DLL and unrelated source file. Each corruption was rejected; exact contents/mtime values were restored and final verification passed. Do not run this fault-injection fixture while another process uses the install. Ordinary `--verify` is read-only. Results: `build/audio-codec/verification-checks.json`.

## Measured results and limits

Native factory checks passed for XMA1/XMA2, mono/stereo, stock/raw, reset, empty-input starvation, empty raw EOF, raw multistream rejection and stock multistream acceptance. `build/audio-codec/msvc-smoke-probe.py` independently linked the shipped MSVC libraries and passed the same factory checks. Logs and command inputs remain beside the scripts.

Both original mono packet fixtures were read without modification. Owned XMA1 and XMA2 raw output exactly matched the previously recorded raw PCM hashes: short 8,704 samples; documented 55,296 samples. Default stock XMA2 produced 8,064 and 54,901 samples, respectively; maximum absolute difference from the separately installed stock FFmpeg baseline was `1.1920928955078125e-7`. Exact output hashes and packet identity are in `validation.json`. PCM passed through a pipe for hashing; this build directory contains no copied original packets or decoded media.

Stereo factory support is tested, but these original decode fixtures are mono. These results do not prove XMA hardware bit equivalence, guest 384-sample timing, original starvation/carry behavior, or game mixing/output correctness. Preserve the initial raw samples: the prior raw/stock best offset of 576 does not authorize an additional 192-sample discard. See `docs/native-xma-raw-probe.md` for that separate evidence.
