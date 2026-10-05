# Reach-game313 — mono shader proof and real skin morph streams

Goal remains active. The original mono shader pair is transcribed offline and
passes hardware/WARP tests. Its runtime material/draw integration is still
required. A separate intermittent skin stop exposed a missing morph-stream
adapter: selected original vertex deltas now populate the native mesh instead
of being replaced with zero. Run313d completes98 scene presentations and
returns to the guarded mono pass. Sustained rendering and character control
are not verified. This follows [312](reach-game-312.md).

## Mono shader qualification

`tools/analyze_mono_shader.py` pins the original image, complete effect8211F480,
both VS records82120C04/82121BE8 and both PS records82122BD4/82122D38, the
literal banks, FETCH metadata, control flow, executable issue schedule, CPU
spans and both passes' upload masks/register maps. Read-only local UCODE and
Xenos enum references retain their existing pinned hashes. The generated
`build/shaders/mono_shader.hlsl` is compiled offline; no runtime shader
interpreter or translator is introduced.

The29-issue VS reads position, weights and indices. Boolean b0 controls the
four-bone path, including the original W/Z/Y/X accumulation and co-issued
address update order. It has no morph input or predicate. Shared
g_ViewProjection maps to c0..3; private kIsSkinned slot16.x maps to b0;
64 bone slots17+4*i map three rows each to c52+3*i. Both techniques have
identical executable code and literals, differing only in record trailers.

The PS has one export issue. Overlapping vector/scalar write masks produce
literal1 in YZW. X takes MAXs(c255.x,c255.x), also1. The native PS therefore
outputs white RGBA without reading any interpolator or texture.

`tests/test_mono_shader.cpp` compares GPU vertex outputs against an independent
double-precision geometry reference: static and skinned paths, all64 bones,
individual/negative/unnormalized/zero weights, nontrivial matrices and ignored
NaNs in dead inputs/registers. Each hardware/WARP run passes3149 checks over
12 draws/768 vertices plus an actual4x4 float RGBA pixel readback. Static
source, program and mapping mutation checks also pass. Report:
`build/reach-game-313-mono-evidence.json`.

These artifacts are exercised by MonoShaderTests. The production material
compiler and draw owner do not yet select them; dispatcher flags2 remain
guarded.

## Reached mono CPU/material state

Runs313b/313c and313d reach the original static mono packet. Its sole per-object
classification row is `[0,1,5,1,0,0,0]`: callback type1 at table82CF0188
selects8270A7A0 for shared handle00040001. The13 reflected parameter rows and
full192-byte typed object are captured in `mono-parameters.bin`,
`mono-classifications.bin` and `mono-typed.bin` under those runs' captures.
Typed+B4 is private g_World handle000C0004, typed+B8 is kIsSkinned00300014,
typed+BC is bone container00340016. The world setter in8273A878 is distinct
from the later combined matrix produced by the per-object callback.

The active cameraE4D41AB0 uses color00F00033/depth00F00034 and a1280x720 reverse
depth viewport. Before the pass's cull override: depth enable/write1,
compare6, cull0, color mask15, blend disabled with packed word00010001,
expanded blend0, no stencil/alpha/scissor/bias, primitive6 strip restartFFFF,
halfpixel1. These are captured states, not a claim that the native draw adapter
is implemented. The original pass itself applies cull2.

## Skin morph stream correction

Run313 stops after72 scene presentations at the third selected morph stream:
r8=2 failed a previous literal-one guard. Original826FF41C..430 computes a
64-bit fetch dirty mask;8243C624..62C ORs it into the device's dirty word.
It is not a sampler operation, stride or endian mode. Streams1..6 require
masks `[1,1,2,2,2,4]`. The exact original spans and interpretation are pinned by
`tools/analyze_skin_morph_streams.py`.

The old boundary also ignored real stream data. It is now
`SimpsonsNativeSkinMorphStreamBind`: validate the original frame, geometry,
context, loop order, source-table selection and computed mask; qualify the
source buffer's address/extent fields; copy exactly one big-endian float3 per
base vertex. Selected streams are retained as owned bytes in the current mesh.
Original unused-stream clears must finish all six bindings before upload.
The CPU loop, selection helper and resource bytes are unchanged.

`decodeSkinMorphStream` first validates every component and the complete
extent, then writes only the selected native morph attribute. Both immediate
and deferred mesh uploads apply these snapshots. During constant commit an
active coefficient must have a selected source; unbound nonzero coefficients
fail before drawing. Original CPU coefficients and bone staging are retained.

Run313d processes288 selected morph bindings (232 stream1 and56 stream2).
The first two distinct captures each contain977 vertices/11724 bytes, with
2690 and2677 nonzero float components respectively. Every component is finite.
SHA256 values:

- `skin-morph-stream-0.bin`:
  fd935873e78a38ca40c4c7c175da2f5c786602b285b84c11789226f8ae374bb2.
- `skin-morph-stream-1.bin`:
  307ac467d5c4607dbf7b6639bb03fe8bc9d00dca95cac8ac30a75ae6bc82aa54.

Stream3..6 masks and attribute order pass offline/unit/GPU tests; this run did
not exercise those live slots. No live third-stream claim is made.

## Verification and remaining work

`build/reach-game-313e-tests.log` has nine passing focused tests: mono source
evidence and hardware/WARP shaders, skin morph source evidence, vertex decoder,
skin shaders and mesh backends on hardware/WARP. Vertex decoding passes161
checks including six distinct streams, endian order, finite bounds and atomic
rejection. Existing skin shader tests each pass24580 checks, including actual
morph arithmetic. Mesh tests now retain nonzero morph attributes through
complete GPU upload/readback; their strengthened hardware/WARP checks and the
decoder pass again in `build/reach-game-313f-tests.log`.

Latest313f AOT verifies311 files with zero semantic diagnostics; game and
focused test builds pass. An intermediate diagnostic build failed because of
an unqualified C++ namespace; it was fixed and regenerated before the successful
build. Run313b used the prior successful binary, so its captures are identified
separately from the rebuilt313c/313d launches.

Run313d ends at presentation1303 with2571 cumulative scene draws,18 new draws
and98 scene presentations, at the mono dispatcher guard.

Final313e uses the313f build, including the active-morph coefficient guard. It
also completes98 scene presentations and2571 scene draws, ending at
presentation1296 with18 new scene draws. It processes281 morph bindings
(221 in stream1 and60 in stream2) before the same mono dispatcher guard;
no active coefficient/source mismatch occurs. Variations in startup timing
change the total presentation count and live binding count between runs.
The automatic observer reports gameplay_verified=false and
input_sequence_completed=false.

The requested capture is inspected at
`build/reach-game-313-preview/native-frame-1827104.png` (presentation1235,
1333 cumulative scene draws,21 new scene draws). It shows the chocolate
horizon, Homer HUD and explosion effects, but large pink and black regions
still replace proper world geometry. No visual correctness or playable
character claim is made. Raw capture SHA256:
fdee068179fe11b8f0007fcc431b385f84da240dbf1d778a707c6d73078cd8ca.
Preview SHA256:
a1c18a54c91c830efaa803a9a9d0702f5ecb831c2a06b00cd78263be61127c66.

Next work is native mono material lifetime, combined matrix staging and
static draw integration, followed by another live launch. Opening-movie
completion, sustained rendering and character control remain unverified.
The goal tool reports paused at this checkpoint; no further implementation
was started after that status was observed.
