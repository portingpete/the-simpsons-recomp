# Original Dac PCM helper fixture

The standalone `tests/test_dac_pcm.cpp` executes the real generated AOT entries
**823544D0,82354480,82345630** through Runtime.load and EngineCpuCalls. It uses
synthetic PCM, a mapped test stack and source planes. It never initializes the
application, fabricates an audio root/device, invokes the Dac0 constructor, or
creates an output voice. Original media and reference projects remain read-only.

The isolated build against build120's existing libraries passed **20 cases and
23,040 sample comparisons**, including expected protected/unmapped-memory
failures. These are bounded CPU semantics tests; they do not establish speaker
routing, an output-owner lifecycle, XMA timing, or audible/mixer equivalence.

Main integrated this unchanged fixture as CMake target `DacPcmTests` and CTest
`OriginalDacPcm`. Build121 passes all32 suites in44.16 seconds; this test passes
in0.09 seconds. Its full output is `build/native-audio-dac-pcm-121.log`.

Build122 adds `audio/dac_pcm.h`, a6144-byte BE-to-host float copy with no
arithmetic or second channel permutation. The fixture compares27,648 output
words across all successful helper cases, including NaNs preserved by the
original clamp, and releases/overwrites an unaligned source after each copy.
Partial and oversized blocks reject. All33 suites pass in46.03 seconds;
`build/native-audio-dac-pcm-122.log` records this unchanged20-case CPU coverage
plus the new transport checks. Native voice admission separately rejects
non-finite/out-of-range samples; the transport does not invent clipping.

## Exact helper ABIs and byte evidence

Authoritative image: `analysis/simpsons.pe`, base82000000, sizeEC0000, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Runtime.load verifies this image and the derived XEX before the tests. Addresses
and offsets below are hexadecimal; frame/channel counts are decimal.
The fixture additionally hashes each entire helper and checks its constants.

**823544D0**, extent1A8, SHA256
`003add46a8e145930f5eb288a39e326d9a814a1b2dc3bc8a2fc7d4ade94d98d2`:

- Input **r4=descriptor**, **r5=6** for the tested branch. Descriptor+4 is
  BE32 plane-base P; descriptor+E is BE16 plane stride N, measured in floats.
  The only descriptor reads in this branch are823544F4/823544F8. No Q link,
  device state, stream object or sample-rate field is read.
- **Incoming r3 is not the destination.** It is stored to a caller home slot,
  then overwritten with fixed global **G=82E32000** at823544E8. On successful
  six-channel return r3's low word is G. Incoming r6 is not a frame count;
  the fixture varies it among0,1,256,FFFFFFFF and obtains the same footprint.
- Always converts **256 frames**. For frame j, destination G+24*j contains
  source planes **[0,2,1,5,3,4]**, each at `P+4*(N*plane+j)`. Proof is the
  six lfs/stfs pairs at82354538..82354580, the24-byte step82354584, and the
  one-plane400-byte end bound82354514. Total output is1800 bytes.
- This is scalar lfs/stfs interleaving, **not a VMX conversion**. The tested
  finite values, signed zeros, subnormals and infinities round-trip exactly as
  BE float32 words. No clipping or fade is implicit. NaN load/store conversion
  payload rules are outside this test's conversion claim.
- For incoming SP=sp, exact stack stores are BE32[sp-8]=LR,
  BE32[sp-60]=sp, BE32[sp+14]=incoming r3. SP/LR are restored. No source or
  descriptor writes occur. These effects are checked over the entire test stack.
- Other channel branches exist (1,2,4); this fixture intentionally certifies
  only6. The 48kHz default is established by the Dac0 constructor, which loads
  literal48000.0 at821DD28C and stores S+30/Q+DC. This helper itself has no
  rate parameter and is not a resampler.

**82354480**, extent50, SHA256
`816c823a3ace87fb73a35a6267b3f54cd1d93759e9b127d382ce26ebef27039b`:

- No input pointer/count is consumed. It examines exactly1536 floats in
  `[G,G+1800)`, regardless of r3..r6 and the channel byte. r3 is unchanged.
- Constants are BE32[821DD110]=BF800000 (-1) and
  BE32[821CA0E4]=3F800000 (+1). Values below-1 are stored as-1; values
  above+1 as+1. Exact endpoints, interior values and signed zeros receive
  **no store**. Infinities therefore clamp; finite subnormals remain unchanged.
- `fcmpu` at823544A0 followed by `bge` tests LT==0; the second comparison
  and `ble` at823544B4 test GT==0. Unordered comparisons skip both stores.
  The fixture checks both signs of quiet/signalling NaNs and several payloads
  remain byte-for-byte unchanged **in memory**. It makes no claim about guest
  FPSCR exception flags or a signalling NaN retained in a live FPR.
- No stack writes. Besides full output/canary checks, an all-in-range/NaN run
  makes the output's two native pages read-only and also removes guest write
  permission. Successful execution proves the no-store branch behavior. A
  separate first-value2.0 case on those pages must fail atG before mutation.
  Both permission layers are restored by the fixture's scope.

**82345630**, extent80, SHA256
`7b848af6a3a6b6aee2f5c8aba353b86730f84c670616f563e05f0d3a129e0907`:

- Only variable input is **C=byte[82E31F9D]**. It reads/writes fixed G, uses
  literals0.0 at821DD0D8,1.0 at821CA0E4 and **1/128 at821DCC8C
  (3C000000)**. No other CPU service or SDK call occurs.
- r4 is saved to BE32[SP+1C] and otherwise unused. r3 is unchanged; incoming
  r5/r6 do not set length, gain, channel count or buffer address.
- End address is `G+C*512`, frame step is `C*4`: with C=6 this is exactly
  **128 frames**, not256. Frame j=0..127 is multiplied by `j/128` using
  the two original single-precision multiplies and integer-exact ramp updates.
  The final multiplier is127/128, not1. Frames128..255 stay bitwise unchanged.
- C=0 returns after its r4 home store, with no output access. The fixture tests
  only C=0 and6; it does not infer a valid arbitrary-channel/device policy.
- Finite input, signed zero, normal/subnormal extrema and inexact/tie cases are
  checked under guest round-to-nearest. Expected bits come from a separate
  integer significand/exponent calculation with nearest-even rounding, not
  the generated FP expressions. Hardcoded oracle checks include normal and
  subnormal ties. NaN/infinity fade arithmetic and other guest rounding modes
  are excluded; no guessed NaN result or exception behavior is asserted.

## Scope and meaningful boundaries

Five six-channel source layouts run in two host FP environments: contiguous
stride256, padded stride257 and263 at differing 4/128-byte alignments, and
stride2048 with each plane in its own mapped page separated by an unmapped page.
The latter starts at either the beginning of its page or exactly1024 bytes
before its end. Every source byte and descriptor byte is checked unchanged.
Distinct plane/frame samples detect incorrect lane permutations across all
256 frames, with special values at frame/16-byte/128-byte boundaries.

These functions have no vector-versus-scalar dispatch or variable-length tail;
claiming VMX-tail coverage would be incorrect. The page-boundary fixtures test
overread protection and exact scalar access footprints. All cases compare the
entire output plus256 bytes on each side, including the untouched half after
fade. An unmapped descriptor must fail at0000300E before any output change.

The common call fixture checks r2/r13, all eighteen nonvolatile GPRs and FPRs,
CR2..CR4, SP/LR, r3 results, sticky VSCR SAT preservation, and exact stack-store
footprints. Host FP state is restored exactly, both from ordinary default state
and from a caller with FTZ/DAZ and toward-zero rounding enabled. The guest cache
is independently initialized to masked-exception, nearest, non-flushing state.
This is a native-call-boundary check, not certification of general guest FPSCR.

SDK globals82E2D9F0/4 and audio root82E31BCC remain unchanged, with no runtime
handles, threads, physical/virtual allocations or audio owner acquired. The
loaded image's fixture output/global range is restored after successful tests.
No audio is submitted or heard.

## Parent integration and isolated command

Suggested parent target: `DacPcmTests`, source `tests/test_dac_pcm.cpp`, link
`SimpsonsRuntime`; CTest name `OriginalDacPcm`, argument
`${CMAKE_SOURCE_DIR}/analysis/simpsons.pe`, timeout30. No full startup stack
reservation is needed for these leaf/small-frame calls. Existing runtime/audio
dependencies and codec-DLL staging still apply to the linked executable even
though this test never creates a codec. This assignment makes no CMake changes.

The following build reuses existing static/import libraries only. Run after the
parent build has finished; it writes solely below `build/dac-pcm`. The helper
imports the existing VS/ClangCL environment function without running its tests.

```python
from pathlib import Path
import importlib.util, subprocess
root=Path.cwd(); out=root/'build/dac-pcm'; out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('host_fp_tools',root/'tests/test_host_fp.py')
m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
cc,env=m.toolchain()
env['PATH']=str(root/'build/audio-codec/install/bin')+';'+env.get('PATH','')
cmd=[str(cc),'/nologo','/std:c++20','/EHsc','/MD','/O2','/fp:strict',
 '/clang:-mssse3','/DNOMINMAX','/DWIN32_LEAN_AND_MEAN',
 f'/I{root}',f'/I{root}/build/generated',
 f'/I{root}/third_party/XenonRecomp/thirdparty/simde',
 str(root/'tests/test_dac_pcm.cpp'),'/Fo:dac_pcm.obj','/Fe:DacPcmTests.exe',
 '/link',f'/LIBPATH:{root}/build/native',f'/LIBPATH:{root}/build/audio-codec/install/lib',
 'SimpsonsRuntime.lib','SimpsonsPPC.lib','SimpsonsAudio.lib','SimpsonsGraphics.lib',
 'avcodec-simpsonsxma.lib','avutil-simpsonsxma.lib','bcrypt.lib','user32.lib',
 'gdi32.lib','d3d11.lib','dxgi.lib','ole32.lib','advapi32.lib','shell32.lib']
result=subprocess.run(cmd,cwd=out,env=env,capture_output=True,text=True,timeout=120)
(out/'compile.log').write_text(result.stdout+result.stderr)
if result.returncode: raise RuntimeError(result.stdout+result.stderr)
result=subprocess.run([str(out/'DacPcmTests.exe'),str(root/'analysis/simpsons.pe')],
 cwd=out,env=env,capture_output=True,text=True,timeout=30)
(out/'run.log').write_text(result.stdout+result.stderr)
print(result.stdout+result.stderr)
result.check_returncode()
```

Run the Python block through `python -B -` using a single-quoted PowerShell
here-string. Results are `build/dac-pcm/compile.log` and `build/dac-pcm/run.log`.
The two MEMORY FAILURE messages in the run log are deliberate rejection checks,
not swallowed unexpected failures. Production, generated code, CMake and the
previous frozen output-boundary document remain unchanged.
