# Replay056: multitone runtime integration; normalmap remains blocked

Gameplay presentation and character control are NOT verified.

## Implemented and exercised

The opaque multitone source820547E8 now has exact native artifact creation and
pair validation (VS82054F2C / PS82055710), a 608-byte private profile, dirty
PS c45/c46/c47/c49 projection and dual-stage VS c46 projection. Immediate
commits override inherited c46 with its retained material value. The runtime
resolves g_NoiseSampler leaf19 from storage word124 and owns its slot3 texture.
Immediate and recorded draws select the multitone color/depth adapter; deferred
draws retain noise resources and sampler state. Immediate sampler restoration
covers slots0..3. UV1 is consumed for multitone.

GPU mesh fixtures on WARP and hardware exercise the actual multitone pair and
adapter, distinct noise owners with A/B/A replay, immediate color/depth/stencil,
state restoration, missing-noise rejection and lifetime release. Constant tests
cover independent dirty accumulation and dual-stage c46 projection. Artifact
population is now28 supported /208 unsupported records.

Replay051 reproduced the old source rejection. Replay052 advanced into
multitone and rejected loc_tonal_swirl texture storage. Replay053 uploaded that
texture and rejected another name with the same layout. Replay054/055 advanced
to loc_buildings2_dualtone (64-square, three levels). Descriptor-driven RGBA8
support now handles tiled8888/8-in-32/ZYXW, power-of-two32..1024, base-only or
mips through the first packed16-texel level; other controls and deeper tails
remain rejected. Independent inverse-address fixtures pin original candy-wall,
tonal-swirl and buildings2 storage, compare every decoded mip, mutate descriptor
bits and byte lanes, and read back actual GPU mip uploads. Tonal storage was
verified identical in two separate original resources.

Replay056 reached the next source82057E08 after multitone immediate submeshes
completed and its RGBA8 textures uploaded. Do not infer that every multitone
runtime recording branch was exercised: A/B/A recorded coverage is the GPU
fixture; the live material draws observed here are immediate.

Evidence:
- K:\SimpsonsNativeCopy\build\automatic-startup\reach-game-056\game.log
- K:\SimpsonsNativeCopy\build\automatic-startup\reach-game-056\result.json

## Normalmap blocker and diagnostic probes

Source82057E08 is simpsons_rigid_normalmap, identity0050002F, packet82D6ED0C.
The opaque pair is VS8205855C (1160 bytes, code572+588) and PS82058CBC
(3004 bytes, code1336+1668), context3140, private bank624 bytes.
820589EC is the alternate VERTEX shader, not the pixel shader.

The inventory in K:\SimpsonsNativeCopy\tools\analyze_rigid_normalmap_shader.py
pins both full record hashes, headers/trailers and control tables. Five inventory
unit tests in K:\SimpsonsNativeCopy\tests\test_rigid_normalmap_shader.py pass,
including control mutations with the digest check mocked out and all-ALU emission.
They are registered as OriginalRigidNormalmapInventory in CMake. VS has43 slots
and six vertex fetches; PS has122 slots,20 texture fetches, five forward jumps
and instruction predication. The shared emitter now emits VS37/37 and PS102/102
ALU slots after adding scalar8/25/40/43/44/47 and vector8. Scalar bank selection
is stage-aware; split forms decode the constant and temporary from raw fields,
rejecting unqualified modifiers/addressing. Opcode45 remains unsupported.
Existing opcode42 output is unchanged; broader legacy arithmetic parity still
requires GPU qualification. Reference definitions are in
K:\Simpsons\RexGlueCurrent\include\rex\graphics\format\ucode.h.
No normalmap HLSL, GPU oracle, tangent-bearing mesh, runtime profile or admission
is implemented. Trial ALU emission is NOT semantic qualification.

Dispatcher-only admission probes057 and058 both stopped at `Unqualified rigid
source profile`, before normalmap rendering. BOTH temporary source allowlist
changes were reverted. `rigidProfile` now includes the actual source in rejection
text; a direct CPU test confirms source82057E08 rejects without changing constants.
There is no existing normalmap rendering-comparison fixture to run.

## Validation and next work

Checked AOT regeneration passes311 files, zero semantic diagnostics; mandatory
after runtime edits. Native executable builds successfully. Ten focused tests
passed together; separate shader WARP/hardware and artifact checks passed3/3;
RGBA asset tests passed3/3. Latest direct material test passed1609 checks.
Final restored-guard executable rebuilt successfully; final focused CTest is9/9,
normalmap standalone inventory4/4, and AOT verification311 files/zero diagnostics.
The final binary was not replayed after restoring the guards; replay056 is the
last live evidence for that admission configuration. A pre-existing printf
thread-ID format warning remains in engine_itxd_textures.cpp:183.
Full suite was not rerun. Audio startup succeeded in these replays; this does
not supersede historical full-suite audio failures.

Next: qualify normalmap tangent semantics, vertex exports, scalar split operand
forms and predication/branches; build independent GPU VS/PS oracles; integrate
624-byte staging, normalmap slot3 ownership and depth adapter; only then admit
source82057E08 and run a fresh replay. A presented gameplay frame and responsive
character control must be verified separately. Original56-register staging
extent is unchanged and contains multitone's highest material row46.
