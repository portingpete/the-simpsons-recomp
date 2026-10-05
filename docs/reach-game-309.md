# Reach-game309 — native billboards, mono dialogue, rigidalpha qualification

Goal remains active. Run309b completes23 scene presentations, up from12 in
[308](reach-game-308.md), and stops at an unsupported alpha draw of material
source8200CCB8. Rendering remains visibly malformed. Sustained rendering,
opening-movie completion and character control remain unverified.

## Implemented and exercised

The mode-zero billboard path now preserves the original8275F168 particle
loop, matrix multiplication and quad vertex construction. Native services
own the retained ITXD texture, state, ring reservation and completed draw.
Original entry, stack, counts, context, shader/declaration identities and
resource lifetimes are checked at each boundary. Run309b submits72 billboard
quads through this path and completes their original batch endings.

- Setup8275F228 replaces the texture/state device work through8275F2F4.
  Original mode-zero setup82751B68 remains. Source definition flags determine
  depth and packed blend; byte106 supplies alpha reference. Stage0 has the
  retained texture, linear min/mag/mip and original U/V addressing. Stage1 is
  cleared. Unknown modes and secondary owners reject.
- Matrix helper827518D0 keeps the original827B8328 CPU product. Hook82751934
  transfers its four completed rows.8275F494 transfers the original particle
  color. The mode-zero soft-particle bank at8275F4E8 is unused by the exact
  selected shaders, so no fabricated constant bank is supplied.
- Reservation82751E38 recognizes caller8275F6E4 and claims exactly128 bytes
  for four32-byte vertices. Original CPU stores8275F6EC..8275F7E8 remain.
  Hook8275F7EC decodes those completed vertices, submits and preserves the
  original loop. End82751DA0 at caller8275F818 closes the same owner/count.
- Five replacement windows in config/simpsons.toml pin their original bytes.
  Generated C++ was not edited. The previously qualified immediate VS/PS and
  renderer are reused without a new runtime shader translator.
- First-quad native input captures are retained under each run's
  `captures/billboard-native`, including finite vertex and transform/color
  values. These captures alone do not prove final scene correctness.

The first mono streamed dialogue is now independently qualified and feeds
the original mixer. Source:
`audiostreams/hr_xxx_0/d_wchr_xxx_0000b31.exa.snu` under the original asset root.
Its SHA256 is202927e7eb530e077db5e1543bfde9d654cc18356964c9a0d67e822c6218aa62.
The48kHz,141793-frame stream has28 blocks and header0300BB80400229E1.

`tools/qualify_mono_dialogue_xma.py` checks the complete original source,
reader-normalized first live block, every block length/hash, packet padding,
XMA1/XMA2 and split reads, initial384-frame skip, per-block output quotas and
full trimmed PCM. The independent stock decoder comparison measures a
576-frame offset and maximum sample error2.384185791015625e-7. Report:
`build/mono-dialogue-xma/report.json`; certificate:
`audio/mono_dialogue_xma_certificates.h`.

The audio owner now derives one or three decoder layers from the qualified
channel count, including a final mono layer. It still rejects uncertified
headers, unexpected channels, mutated blocks and invalid producer ownership.
No general dialogue-bank admission is implied. The original music fixtures
and tests still pass alongside the new mono fixture.

Live evidence in309b:

- Line42979: qualified stream V=E45A98E0, generation38575,4736 initial frames,
  one channel, explicit384-frame skip.
- Line44608: the same original mixer voice commits256 frames of real PCM.
- Last accepted scene presentation1245 has1039 cumulative scene draws and20
  new draws. Presentations1223..1245 contain23 scene frames.

## New shader implementation, not yet integrated

The next fallback packet is82D6EBEC, typedE1AA88F0, identity0050001B,
source8200CCB8, wrapperE1AA8710, context00900001. Entry827400F8 returns to
82740B28 with r4=1,r5=1,r6=0,SP0203F590. Current runtime rejects the new
arguments before changing that packet's native draw state. All earlier
owner/phase guards pass. The failure message still says "opaque rigid".

Original823CA730 uses the low byte of r7 (fallback r4) to choose typed+AC
instead of typed+A8. Fallback827400F8 retains that argument in r30. It also
applies selectors44=1,6=1,9=6,10=7 and optionally disables depth writes for
nonzero r6; cleanup resets selector44 and conditionally restores depth write.
Entry r5 is overwritten from the packet before use. These facts do not
authorize substituting the front material pass for the alpha pass.

The original source has a distinct rigidalpha technique0007FFFC/pass0007FFFE,
context2910, VS8200D734 and PS8200E1BC. Both depth scalar rows are1; its six
stage0 sampler rows have values0,0,0,1,1,1 for SDK IDs0,4,8,16,20,24.

`tools/analyze_rigid_alpha_shader.py` now pins the complete image/effect,
records, headers, literals, complete instruction schedule, consumed resource
maps and exports, then emits HLSL offline. There is no runtime admission yet.

- All executable VS instructions match the existing168F8 alpha VS exactly.
  Native input semantics are position, normal, color and UV. Tests verify
  clip/world transforms, transformed normals, UV and color independently.
- The new PS samples base RGB, computes the absolute normalized view/normal
  cosine, and chooses either sampled color with scaled vertex opacity or an
  opaque silhouette whose intensity uses custom-line constants. The native
  transcription preserves co-issued old-register reads and SM3 zero-product
  behavior at both normalizations, including zero-length directions.
- Exact map: VS view-projection c0..3 and object c12..15; PS world-eye c4.xyz,
  object-ID c40.w and custom-line c49.xy. Private leaf16 g_BaseSampler maps to
  stage0. This pass does **not** use either shadow sampler. Its sampler must
  come from that retained base-texture owner, not a previous shadow binding.
- Test-only build artifacts are generated in build/shaders. The production
  material compiler, pass selection, material commit and draw backend remain
  unchanged for this new pair. There is no alpha-pair alias to opaque shaders.

## Validation and limitations

Game/AOT309b:311 generated files,zero semantic diagnostics;
`build/reach-game-309b-regenerate.log` and309b-build.log. No runtime edits were
made after that build. Subsequent changes add offline shader tooling and a
standalone test target only.

Ten focused CTest cases pass across three logs:

- `build/reach-game-309-tests.log`: immediate WARP/hardware and original
  immediate/radial shader evidence, four cases.
- `build/reach-game-309b-audio-tests.log`: full menu music, Land of Chocolate
  music and mono dialogue source tests, three cases.
- `build/reach-game-309-alpha-tests.log`: new alpha WARP/hardware and original
  inventory tests, three cases. Each GPU runs40 VS probes and432 PS draws,
 2529 checks. PS cases include forward/back/oblique/zero normals, zero eye
  direction, distinct texture channels/texels, exact0/1 threshold equality,
  negative/zero/high opacity scales and saturated silhouette intensities.

The full test suite was not rerun. Existing unrelated failures remain open.
The alpha GPU tests establish shader arithmetic/input behavior, not runtime
resource ownership, draw state or scene correctness.

Run309's extra scene readback after initial billboard draws is
`build/automatic-startup/reach-game-309/captures/native-frame-1869998.rgb10a2`;
its preview is `build/reach-game-309-preview/native-frame-1869998.png`.
Visual inspection still shows HUD/autosave over pink and malformed black
world geometry.309b did not obtain a newer scene readback. No game process
remains running. Failure shutdown releases native backing; it does not prove
successful original resource cleanup.

Next: integrate the new rigidalpha pass while preserving8200CCB8's existing
opaque draws. Pass selection must track the actual requested technique,
including cache transitions, texture transfer, material constants, base
texture stage0 and packed alpha blend. Do not globally classify every CCB8
draw as alpha or merely widen the argument guard. Capture/check the live
typed+A8/+AC and material/declaration inputs before admitting the new draw.
Afterward regenerate AOT before building and repeat the live run. Existing
world orientation/depth/material defects and sustained control still require
resolution. Original assets and reference trees remain unchanged.
