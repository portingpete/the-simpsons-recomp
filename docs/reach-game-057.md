# Replay057: normalmap runtime integration; skin remains blocked

Gameplay presentation and character control are NOT verified.

## Implemented and exercised

The opaque normalmap source82057E08 now has exact native artifact creation and
pair validation (VS8205855C / PS82058CBC), a 624-byte private profile
(context0x3140, 24 samplers, 156 words), dirty PS c44/c45/c46/c47/c49/c50
projection (leaves 23/22/21/20/14/13, words 152/148/144/140/80/76) and
dual-texture base (leaf17 word92, g_BaseSampler) plus normal (leaf19 word124,
g_NormalMapSampler) ownership at slot3. Immediate commits override inherited
banks with all six material registers; VS has no private projection (all 30
rows inherited). Immediate and recorded draws select the normalmap color/depth
adapter; deferred draws retain base/normal resources and sampler state.
Immediate sampler restoration covers slots0..3. UV1 and tangent are consumed
(stride40, 6-element TEXCOORD0..5 declaration at byte56); the first 56 bytes
stay byte-identical for existing consumers.

GPU fixtures: VS oracle (existing 33-float stream-output, WARP/hardware) plus
new full VS/PS transcription (37 VS ALU, 102 PS ALU, 20 fetches, 5 predicate
jumps, 4 predicated slots, 9 predicate sources, k251..k255 literals) compiling
under FXC vs_5_0/ps_5_0. Constant tests cover independent dirty accumulation
for all six leaves and profile extent (624 bytes, 24 samplers, 156 words).
Artifact tests cover 30 compiled / 206 unsupported records (was 28/208).

Replay057 (build/automatic-startup/reach-game-057) verifies the main menu,
Continue Game and movie skip, then executes normalmap immediate draws:
BEGIN id0050002F VS8205855C/PS82058CBC, 9 packets (1+1+1+2+1+1+3+1+3 draws)
with base/normal uploads and shadow/depth binding, all completing with
original fallback and stream cleanup. It advances to the next scene dispatch
(packet82D6D9A4, source82006348 simpsons_skin, identity0050001A).

Evidence:
- build/automatic-startup/reach-game-057/game.log
- build/automatic-startup/reach-game-057/result.json

## Skin blocker

Source82006348 is simpsons_skin, vtable82061714 (vs rigid 820616C0),
technique_AC00000000 (vs rigid 0003FFFC). Skinned character rendering
(bones, skin VS/PS, vertex blending) has no native transcription, GPU oracle,
tangent/bone-bearing mesh, runtime profile or admission. The dispatcher
rejects at 82740680 from 8273B4E0, same boundary as previous rigid blockers
but for a skinned source.

## Validation and next work

AOT regeneration passes 311 files, zero diagnostics. Native executable builds
(223 targets). Full CTest passes 170/170. Focused rigid/material tests pass.
No presented gameplay frame or character control is verified.

Next: qualify skin tangent/bone semantics, vertex exports and skinning;
build independent GPU VS/PS oracles; integrate skin staging, bone/slot
ownership and depth adapter; only then admit source82006348 and run a fresh
replay. A presented gameplay frame and responsive character control must be
verified separately.
