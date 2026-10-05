# Original simpsons_edgeAA shader qualification

Boot196 reached this effect after the real edge and AA draws and the following
shared color copy. Activation remains guarded while resource/CPU integration is
being implemented. The main menu has not been reached.

The source is82034008, body82034014/9268 bytes, SHA256
`d8e15a8b641d4a0765209c671ff643ccc69683effe013f414712f1f537d53075`.
`tools/analyze_edgeaa_shaders.py` checks original bytes, instruction fields,
control flow, literals, bindings and the original integer default. Its report is
`analysis/native-edgeaa-shaders.json`;188 identity/control-flow mutation checks
pass. The fixed HLSL is `renderer/edgeaa_shader.hlsl`. No runtime translator,
interpreter or console GPU backend is involved.

| Stage | Record/bytes | Executable offset/bytes | SHA256 |
|---|---|---|---|
|VS|820347B0/320|224/96|`f9c7b041b09d31e0ba7e11cb4c432ae679f2facf730840f9e8e35325eb9456fe`|
|PS|82034900/2688|1224/1464|`66a97e173078b799410a9a063daca781c426efbc2c0ac8dd9e5b509f5167196f`|

There are113 static issue slots,109 in the PS. The VS consumes float2 position
and UV, exporting clip Z/W1. Five PS texture roles are color0, depth1, palette2,
base color3, original line4. Float inputs map c20..27 to depth fade, shadow fade,
depth-AA threshold, width, height, rim-shadow light, rim light and cast shadow;
c48..50 are NSamples, BlurWidth and EdgeColorScale. KernelWidth is unused.

The shader decodes packed blue/alpha material flags, chooses direct or palette
RGB, applies rim and cast-shadow terms, and selects four outline branches:
no edge, low-depth attenuation, filtered lines, or solid-line blending. Two
ten-sample horizontal/vertical loops use hardware integer16. Both have a
predicate-false break, but the qualified active body does not change that
predicate. Original shader CTAB default integer0 and effect context tail8992
both contain `{10,0,1,0}`. Admission is restricted to NSamples10; changed integer
constant application is not qualified. The original count, offset and division
remain distinct inputs. Alpha participates in the outline and final lighting;
it is not forced to one. There is no pixel depth export.

Special scalar opcodes42..47 encode a constant and a temporary using split
fields. The temporary index uses the opcode low bit, src3 selector and middle
swizzle bits; the constant reads W and the temporary X. Source negation applies
to both operands. Only the operand-field parser at1335..1390 of the pinned
reference `translator.cpp` was consulted, alongside declarative `ucode.h`;
no renderer implementation was used. Each special scalar's decoded operands is
explicitly asserted. Ordinary scalar operations use the W component and may
update the previous scalar even with an empty destination mask.

## Native texture conversion and tests

The first software run passed, while hardware chose an adjacent palette cell
at pixel14,28. A separate raw-source shader measured packed green1022 as
0.9990234375 on hardware versus float32(1022/1023)=0.999022483826. That difference
crossed an authored `floor`; increasing the output tolerance would hide a real
color change. The native RGB10A2 input conversion now recovers the uniquely
nearest packed integer, then normalizes it before the effect's math. This
applies to base, color and line samples only. Palette and depth retain their
separate native formats. It is an explicit native precision policy, not a
claim about the console texture unit.

The standalone fixture checks all1024 RGB codes and all4 alpha codes on each
device, verifies raw-source conversion stays within half a packed code, and
checks the recovered integer exactly. Independent color-domain expected values
use double precision, not temporary-register execution. Sixty-eight effect
draws exercise all four branches, asymmetric palettes/colors, blue/alpha
categories, mixed predicates within pixel quads, neighbor-only depth changes,
wrap/clamp borders, four blur widths and lighting/color parameters. Output is
RGBA32F before quantization; tolerance3e-6 covers native arithmetic. Depth checks
verify the VS's exact Z/W1 with a separate D32 target.

`build/edgeaa-input-proof-tests.log` passes all three focused tests in3.91s.
WARP reports68 effect draws,4 raw-input probes and381,002 checks; hardware
reports68/4 and381,077 checks. Counts include bounded GPU waits. Each device's
branch coverage is23,200/14,260/20,766/11,406 pixels. The production game still
uses the last fully tested edge/AA binary; edgeAA engine admission is pending.

Console reciprocal/fused arithmetic, filtering, rasterization, depth precision
and output rounding parity remain unproven. A shader fixture proves neither
resource provenance, engine submission, a complete frame, presentation nor the
main menu.

## Native resource and original CPU integration

The native material registry admits thirteen exact shader records; 223 remain
unsupported. The edge/AA/edgeAA pairs cannot be mixed. EdgeAA binds a real
176-byte constant buffer, five point/wrap samplers and five owned native SRVs.
Its three RGB10A2 color inputs must be distinct, equally sized single-level
textures. Depth uses the existing D32/S8 copy with a depth-only float SRV.
Palette is a single-level 64x64 RGBA8 texture. Output aliases, cross-device
objects, missing/stale bindings and nonfinite or unsupported constants reject.
The backend fixture verifies all four effect branches and unchanged inputs on
software and hardware. `build/edgeaa-backend-tests.log` passes all three tests.

The original palette is `simpsons_palette` in `loc/loc.str`, entry6,
`loc_split4.itxd` (405504 bytes), record0x728, payload0x27000/16384 bytes.
The descriptor is `80800002 00000086 0007E03F 00000C14 00000000 00000200`:
tiled 64x64 k8_8_8_8, endian8-in-32 and ZYXW swizzle. Its native decode uses the
qualified four-byte tiled address and raw byte order1,2,3,0. An independent
inverse-address fixture verifies every RGBA byte. Source dictionary SHA256 is
`663277a5e672bcb4410e7ef4b6368db1edd6b988e8cbb4ae4c26e8e64c6b71cc`;
tiled SHA256 is
`d48830b178130fbcc9bcd6a92e1c91c687065b262f1fb2ba251339ff48064e62`, and RGBA is
`dd5b29268a38aebf4a8d34b8e8332e929b30e38a150d163dbe1175358239d867`.
The ITXD service resolves only the actual published header to its owned record,
checking exact name/metadata/generation. Original copy, promotion, cache,
release and reload tests pass; interior and stale headers reject.
`build/edgeaa-palette-tests.log` passes seven tests in26.45s.

Runtime integration keeps parent823C7500, pass823CA188, setter823C9A60,
rectangle823CA448, and their original prologues/epilogues. Twenty-two byte-pinned
continuations replace SDK descriptor/address resolution only; original float
loads, vector integer conversion, video-mode conversions and stores remain.
The five scalar helpers retain both original save/restore helpers and stfsx;
the palette helper retains its original stwx. Begin/commit/end require exact
caller, frame, typed owner, cache, manager, camera and shader identities.
Commit acquires actual first/second scene copies, copied shared color/depth,
and the original palette header; dirty masks clear only after real bindings.
Draw rechecks all five leases. Capture metadata includes `edgeaa_draws`.

The original finalizer publishes **BlurWidth** at82D09970 (handle003C001A,
slot400, sourcefloat82CD1434) and **EdgeColorScale** at82D09974
(00380018, slot384, sourcefloat82CD1438). The first integration test caught an
incorrect reversal of these adjacent settings. The final corrected original
chain fixture runs both cycles successfully: all three effects draw, all188
private words and untouched lanes match, original scalar/five-sampler caches
restore, dirty masks clear, black palette pixels and unchanged depth/stencil
match, and original cleanup retires the palette/copies.
`build/edgeaa-handles-tests.log`: OriginalEdgeAAParameters PASS6.74s.
The fixture supplies the unchanged level dictionary through the observed
original font-loader envelope and enables the original post-processing branch;
these are fixture inputs, not changes to game startup or flow. Full build/live
acceptance remains separate.
