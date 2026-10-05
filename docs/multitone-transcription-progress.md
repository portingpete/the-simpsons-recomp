# Multitone transcription and GPU checkpoint — 2026-09-17

## Latest continuation: multitone executes in replay056

Runtime shader creation, 608-byte staging, owned noise slot3 and depth adapter
are implemented and tested. Replay056 executes multitone immediate draws and
advances to unsupported simpsons_rigid_normalmap82057E08. Gameplay presentation
and character control remain unverified. See
K:\SimpsonsNativeCopy\docs\reach-game-056.md for evidence, scope and continuation.
Normalmap dispatcher-only probes057/058 reached the missing profile; those
allowlist changes were reverted. Normalmap remains unadmitted.

Normalmap continuation: shared ALU emission now covers VS37/37 and PS102/102
slots. Exact-expression, co-issue and modifier-rejection tests cover the new
forms; the five-test inventory is registered in CTest. Native build/AOT pass,
and the focused rigid suite passes12/12; dual-textured source verification and
107 mutation checks also pass. This is emission coverage, not shader execution:
tangent inputs, eight interpolators, predicates/branches, GPU oracles and runtime
integration remain outstanding. No new gameplay replay was run.

## Historical continuation: runtime VS bank widened to 47 rows (unadmitted)

First runtime-backend step only: `Graphics::RigidVertexConstants` in
`renderer/native_backend.h` grew from 30 to 47 rows (752 bytes) to include
multitone's c46; existing 30-register shaders accept the larger bound buffer,
matching the PS-bank precedent. Dependents updated: `RecordedConstants`
static_assert 1336 -> 1608 in `renderer/rigid_mesh.cpp`, the payload
receipt expectation 2*1336 -> 2*1608 in `tests/test_rigid_mesh.cpp`, and
`EngineEffects::prepareRigidReplay` in `runtime/engine_effects.cpp` now fills
all `vertex.size()` rows from the 0x82D6C0D0 staging copy (was 30, which would
have left c30-c46 uninitialized). The adjacent PS loop also changed from a
hardcoded 50 to `pixel.size()`, fixing c50 (gloss exponent staging) having been
left uninitialized in the replay bank since the 51-row PS bank was introduced.

Validation, scoped: AOT regenerated through the checked path
(`tools/recompile.py` with the pinned generator/analyser; 303 chunks, 0
semantic diagnostics; the VerifyAOT gate had correctly refused to compile the
edited runtime source before regeneration). The full native build then linked
all 186 targets, and ctest `NativeRigidMeshWARP`, `NativeRigidMeshHardware`,
`NativeOriginalMaterialArtifacts` and `OriginalRigidMaterialConstants` pass,
covering 30-register-shader immediate/recorded/A-B-A behavior and bitwise
GPU bank round-trips on both backends. NOT covered: no replay or gameplay run;
`prepareRigidReplay` itself is compile-verified only; the runtime staging
copies still stop at the original 896-byte (56-register) extent, which the
comment already documents. `tools/build.ps1 -SkipGenerate` aborts on a CMake
deprecation warning under stderr redirection before reaching compilation —
tooling only, worked around by invoking cmake directly with the same vcvars
environment; the script itself is unmodified.

Not done, unchanged: factory/pair multitone creation, 608-byte profile
staging, owned noise slot3 (immediate+recorded, A/B/A), native depth
adapter/format, and admission. Source820547E8 remains excluded from both
dispatcher gates and `rigidProfile`; `isRigidSource` still rejects it and
`rigidProfile` still throws. The audio-startup failures stand; full suite not
rerun. Next: multitone creation cases in the factory/pair validator, then
608-byte staging, then slot-3 ownership.

## Earlier continuation: real VS/rasterizer/PS linkage

Added `K:\SimpsonsNativeCopy\tests\header\test_multitone_linkage.h`, included
inside `K:\SimpsonsNativeCopy\tests\header\test_multitone_pixels.h` after its
506 isolated PS cases. It reuses the qualified pixel oracle and resources,
but replaces the probe VS with VSRigidMultitone and a real shared-layout mesh.
Probe constant buffer b2 is explicitly unbound. PSRigidMultitone remains bound;
no geometry shader or interpolator probe substitutes for either stage.

Eight draws compare every RGBA lane of all four pixels against CPU matrix
composition, per-vertex noise-UV selection, affine barycentric interpolation,
and the existing pixel channel oracle. Cases vary VS c46.w, PS c49.w (with
noise slot3 unbound when disabled), shadow enable, nonreceiver state, shadow
projection W rows, custom ID, and opposing shadow-map contents. Vertex UV0,
UV1, normals and colors vary across the triangle. World/shadow W varies;
clip W is fixed at one, so this does NOT qualify nonuniform clip-W perspective
interpolation. Opposing map cases are not an exhaustive bank-swap mutation test.

Validation: RigidShaderTests rebuilt and passed on WARP and hardware, each with
95,774 checks, 44 VS draws/2,292 vertices and 2,437 PS draws. All six focused
CTest targets passed (1.98 seconds). No production shader or runtime admission
changes were made in this continuation. The full suite was not rerun; its last
result remains 127/167 with audio-startup failures, as recorded below.

Audio clarification: a read-only registry check now finds eight active render
endpoints, and audio devices report OK. This does not establish a usable default
XAudio2 endpoint or explain HRESULT0x80070490. The previous blanket advice to
restore a missing endpoint was unsupported; the audio-startup cause remains
unresolved. No audio code, device setting or service was changed.

Next: runtime factory/pair validation, 47-register VS bank, 608-byte profile
staging, owned noise slot3 through immediate/recorded paths and A/B/A retention,
then native depth adapter/format integration before admission. Source820547E8
remains excluded. No replay or gameplay verification was performed.

## Earlier continuation: shadow-export bug reproduced and fixed

The missing VS shadow assertions exposed an actual transcription bug in
`K:\SimpsonsNativeCopy\tools\analyze_rigid_shader.py`: vector opcode12 was
incorrectly emitted as DST. Xenos opcode12 is per-component CNDE
(src0 == 0 ? src1 : src2); DST is opcode28. This is confirmed by the existing
hash-pinned reference `K:\Simpsons\RexGlueCurrent\include\rex\graphics\format\ucode.h`
(SHA256 e820997c448f0fe4734738cac1526b841dde947e2dac568f56c022d1a55a59fb).
Earlier speculation that the unusual DST output was original behavior was wrong.

Retail VS slot24 words C80F0007/00B06CA6/6CFF0004 select homogeneous world W
from r0.x and world ZXY from r4 using literal (0,1,1,1). The assembled vector
feeds both shadow matrices. The incorrect DST discarded world XYZ.

- Added a retail-slot emission regression in
  `K:\SimpsonsNativeCopy\tests\test_rigid_alu_emission.py`.
- Added all eight shadow-export lane assertions in
  `K:\SimpsonsNativeCopy\tests\header\test_multitone_vertices.h`, using independent
  world-to-shadow matrix composition. Cases include identity and distinct dense
  shadow banks, nonconstant/nonaffine world W, and every existing c46 threshold
  and dominant-axis case (1,500 vertices per backend).
- Before the fix, the emission regression failed and BOTH WARP and hardware
  failed: character shadow sample0 lane0 actual1 expected-3.
- Corrected opcode12 to conditional selection using all three operands and
  removed duplicate erroneous opcode cases. Generated HLSL is rebuilt through
  CMake, not edited manually. Opcode28 remains unsupported rather than aliased.
- After an explicit RigidShaderTests rebuild, both GPU backends pass 95,614
  checks: 36 VS draws/2,268 vertices and 2,429 PS draws (including 506 multitone
  PS draws). All six focused CTest targets pass; ALU regressions pass 6/6.

Full-suite result is NOT green: the final rerun passes 127/167, with 40 failures
reporting Create output mastering voice, HRESULT0x80070490, at audio startup.
Audio services were running; no audio code/system-setting workaround was made.
Receipt: `K:\SimpsonsNativeCopy\build\multitone-shadow-final-ctest.log`.
An earlier broad run still used the stale GPU test binary and reported 42
failures; explicitly rebuilding the focused target resolved the two GPU failures.

Remaining: end-to-end VS/PS linkage, native depth adapter/format integration,
shader factory/pair validation, 47-register runtime VS bank, 608-byte profile
staging, owned slot3 through immediate/recorded paths, and A/B/A retention.
Source820547E8 is still excluded. No replay or gameplay success is claimed.
Restore a working audio output endpoint before treating a full-suite rerun as
complete validation; do not bypass audio initialization to hide these failures.

## Earlier continuation: pixel GPU evidence (historical)

Added `K:\SimpsonsNativeCopy\tests\header\test_multitone_pixels.h`, included
and invoked by `K:\SimpsonsNativeCopy\tests\test_rigid_shader.cpp`. It uses the
previously compiled test-only VSRigidMultitonePixelProbe to feed the actual
unadapted PSRigidMultitone and reads all four lanes of a 2x2 float target.
No production shader logic or runtime admission was changed.

- 506 multitone PS draws pass on each of WARP and hardware.
- Independent 2x2 base/noise textures exercise slot and channel separation,
  point/clamp coordinates, independent noise-UV scaling, disabled noise with
  slot3 unbound, positive gains, signed distance, center/slope controls and
  saturation. Expected noise modulation uses channel arithmetic, not register
  emulation; packing and shadows reuse the existing spatial/dual-color oracle
  with multitone-specific control thresholds.
- Coverage includes secondary UV wrap/packing, custom IDs, color-blue equality,
  zero/signed-zero normals, nonreceiver maps unbound, and independently varied
  shadow/rim threshold neighbors against dark maps. VS c46.w selects noise UV;
  PS c49.w controls noise enable/gain. These are distinct controls.
- Each of 18 selected RGB shadow taps independently flips final alpha, with
  disagreeing nonselected channels. Depth equality/adjacent floats, distinct
  bank projections, nonunit W, fractional weights and shadow amount are tested.
- The suspected PS coordinate reuse was ruled out by dataflow: slots72-75
  replace r1.xy from world-shadow r2; slots76-77 apply the same half-scale and
  offset as the character path. No shader correction was warranted.

Validation: RigidShaderTests rebuilt successfully; WARP and hardware each report
83,614 checks, 36 VS draws/2,268 vertices and 2,429 PS draws across all variants.
The full native CTest suite passes 167/167 in 97.56 seconds. The earlier
NativeHostFloatingPoint setup failure (vcvars64 exit255, before assertions)
was resolved by clearing __VSCMD_PREINIT_PATH and trimming process PATH; no
floating-point implementation or test changes were needed.

Remaining: VS shadow-coordinate exports and end-to-end VS/PS linkage, native
depth adapter/format integration, native shader factory/pair validation,
47-register runtime VS bank, 608-byte profile staging, slot3 ownership through
immediate/recorded paths and A/B/A retention. These PS tests do not establish
console-wide equivalence or gameplay success. Source820547E8 remains excluded;
no replay or gameplay verification was performed. Next is resolving and
qualifying VS shadow exports, then backend integration before admission.

## Earlier continuation: vertex GPU evidence (historical)

The current continuation adds a real D3D11 stream-output test, not scene
admission. Source820547E8 remains excluded by both dispatcher gates and
rigidProfile; no gameplay success is claimed.

- `K:\SimpsonsNativeCopy\renderer\rigid_multitone_probe.hlsl` is a test-only
  pass-through geometry shader observing the actual multitone VS.
- `K:\SimpsonsNativeCopy\tests\header\test_multitone_vertices.h` checks 1,500
  vertices against an independent geometric reference on each of WARP and
  hardware. Coverage includes c46.w below/equal/above 0.5 (including adjacent
  floats), all signed axis combinations in {-2,-1,0,1,2}, dominant-axis ties,
  zero normals, a rotated/scaled/translated world matrix, UV0 passthrough,
  clip position, transformed normals, both UV pairs, and color preservation.
- The test uses the same dualtextured input-layout signature as the native
  mesh implementation, with a separate 47-register test constant buffer.
  It does not enlarge the runtime constant bank.
- `K:\SimpsonsNativeCopy\CMakeLists.txt` generates multitone HLSL from the
  pinned emitter and compiles VS, PS, depth adapter, and test GS headers with
  `/Ges /Gis /WX /O3`. The previously started VS/PS/depth targets and includes
  were still present on disk despite the previous conversation's revert
  summary; this continuation retains and builds them.
- Built only RigidShaderTests (AOT verification succeeded). Both WARP and
  hardware runs pass: 74,922 total checks, 36 VS draws/2,268 vertices and
  1,923 PS draws per run. These totals include existing rigid/gloss/dual tests;
  the multitone contribution is vertex-only. All six focused CTest targets
  pass: NativeRigidShaderWARP, NativeRigidShaderHardware,
  OriginalRigidAluEmission, OriginalRigidGlossShaderEvidence,
  OriginalRigidMultitoneInventory, and OriginalRigidShaderEvidence. The entire
  native suite has not been rerun after this continuation's edits.

Still unqualified: multitone shadow-coordinate exports, pixel base/noise
sampling and arithmetic, nonuniform shadow taps, native material staging and
resource retention. Compilation of the pixel/depth artifacts alone is not
rendering qualification. The native shader factory still has no multitone
creation cases and the game executable was not rebuilt in this continuation.

The earlier live diagnostic receipt is
`K:\SimpsonsNativeCopy\build\automatic-startup\multitone-marker-20260917-135127\game.log`:
`lr=8273B4E0 flags=00000000 source=820547E8`. It identifies the first source
allowlist rejection, not the only work needed for runtime support. No further
startup replay was needed for the vertex GPU milestone.

## Original offline checkpoint (historical)


Gameplay remains blocked. No runtime admission, shader alias, guard bypass,
executable rebuild, or replay051 was performed in this checkpoint.

## Implemented

`K:\SimpsonsNativeCopy\tools\analyze_rigid_multitone_shader.py` now emits a
standalone candidate HLSL pair with `--emit-hlsl <path>`:

- Original opaque VS82054F2C and PS82055710, pinned by existing record hashes.
- Forward predicate branches, the vertex if/else and conditional EXEC.
- Whole-coissue predication for VS slots35,36,39.
- Separate base and noise sampling, original destination masks and swizzles.
- Separate multitone interpolators and a 47-register vertex bank.
- Original literal banks and five vertex semantic input assignments.

The shared ALU emitter now handles scalar ADD opcode0. A hand-transcribed
exact-output regression checks PS slot32, including evaluation of both
right-hand sides before destination writes. This is not a full shader oracle.

Generated candidates and compile outputs are under:

- `K:\SimpsonsNativeCopy\build\multitone-candidate.hlsl`
- `K:\SimpsonsNativeCopy\build\multitone-vs.cso`
- `K:\SimpsonsNativeCopy\build\multitone-ps.cso`

These files are not linked into the native game or registered as renderer
artifacts. Regenerate from the tool rather than editing the candidate.

## Verified

- Multitone Python suite: 8/8 tests pass. Every executable ALU/PS fetch emits;
  the five VS fetches are represented by the input interface.
- Shared ALU Python suite: 5/5 tests pass.
- Existing rigid evidence: 849 mutation checks and exact HLSL verification pass.
- Existing gloss evidence: HLSL verification passes.
- Windows SDK 10.0.26100.0 x64 FXC compiles both entry points with
  `/Ges /WX`, targets `vs_5_0` and `ps_5_0`.

Compilation and textual tests do not establish GPU arithmetic equivalence,
rendered output, native resource lifetimes, or gameplay correctness. The full
native test suite was not run.

## Remaining implementation

1. Add independent GPU oracles for vertex dominant-axis/noise-UV selection,
   both c46.w branches, ties, coissue predicates, and pixel noise/base sampling.
   Qualify literal-bank associations, arithmetic and nonuniform shadow taps.
2. Add renderer shader artifacts and the native depth-output adapter, with
   multitone-specific interface and pairing checks.
3. Extend vertex/material staging and recording snapshots for VS c46 plus PS
   c45/c46/c47/c49. Keep old variants' register ownership unchanged.
4. Add the 608-byte private profile, 24 sampler rows, and owned noise texture
   slot3 through immediate and recorded paths. Test A/B/A material retention.
5. Only then admit source820547E8 in both dispatcher gates and rigidProfile,
   rebuild the executable, and run a new replay051 directory.
6. Verify a presented gameplay color frame and character control separately;
   replay input completion alone is not gameplay success.

The attempted startup at
`K:\SimpsonsNativeCopy\build\automatic-startup\20260917-115653\result.json`
verified menus and movie skip, then failed at82740680/caller8273B4E0 with
source820547E8. A second launch was left at the main menu; process42108 was
running at the last process check, not a continuing guarantee of liveness.
