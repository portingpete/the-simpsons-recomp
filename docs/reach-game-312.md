# Reach-game312 — projected particles, dialogue and characters audio

Goal remains active. The projected-shadow particle path now draws through
native shaders while retaining original particle CPU upload and matrix math.
Two additional dialogue streams and the69-source characters sound bank are
qualified. Latest run312d reaches98 scene presentations and stops at the next
`mono` material pass. Ten focused tests and game/AOT builds pass. The latest
inspected frame remains visibly malformed; opening-movie completion, sustained
rendering and character control remain unverified. This follows
[311](reach-game-311.md).

## Projected-shadow particle path

`tools/analyze_projected_particle_shader.py` pins the complete652-byte original
PS82156B60, its64-byte literal bank,240-byte code and complete17-issue schedule.
It checks the original CPU setup82772CA8..827736B0 and pinned read-only format,
sampler and instruction references. The existing full VS821570E0 is retained.
Generation is offline; no runtime shader translator or GPU emulator is added.

The PS performs four stage2 shadow fetches, a stage0 base sample, depth
comparisons and fractional interpolation of their results. Its original
Y/X/W/Z sample component selections follow explicit D24FS8 depth-to-RRRR
expansion. Signed offset fields1/31 represent+/-half a texel. The original
stage2 sampler is point/min/mag/mip with mirrored U/V, rather than linear or
clamped sampling. Constants encode1024-square sampling. Co-issued ALU lanes
read pre-issue values; depth clamp, scalar dependencies, ambient contribution,
color multiplication and alpha remain in the transcription.

The renderer has an explicit projected-shadow policy and retained1024-square
native depth owner. It rejects missing/foreign/aliased owners, wrong sampler
state and disabled projection constants. Its new pixel adapter preserves the
existing particle packed-color blending, alpha test and geometric depth.
Stage2 resource/sampler bindings are restored alongside the existing actual
pipeline state; ordinary particle draws continue to use their original pair.

Runtime definition+104 mask04 selects the new path. It validates the original
projector at82DFEB98, its+F0 shadow texture, uploaded phase, dimensions/format
and selected shader source. Live projectorE4CEA400 owns shadow00F00017. This is
the original shadows effect's1024-square texture, not viewport header82DFE840.
Original827B8328 performs frame82DFEA20 times projector+260, with the extra
emitter-local matrix when emitter flags+10 mask04 requests it. Results enter
VS21..24; definition+E4 supplies VS25.x. Original stack outputs, particle ring
iteration, animated UV/flags, staging stores and completion remain AOT.
312c additionally checks the original512-byte CPU stack frame at entry.

Requested projection with no original projector remains guarded: that original
branch inherits VS25, and its state has not been qualified. Type100=5 and
mode40=1 variants also remain guarded. No blanket variant admission was added.

## Verification

`build/reach-game-312-tests.log`: six tests pass:
OriginalParticleInventory, OriginalProjectedParticleInventory,
NativeParticleBackend, NativeParticleBackendHardware,
NativeProjectedParticleShaderWARP and NativeProjectedParticleShaderHardware.

Each projected PS GPU test runs1440 cases/92160 component checks against an
independent arithmetic/sampling oracle: all16 four-tap visibility patterns,
fractional weights, exact texel-center boundaries, mirrored coordinates,
depth saturation and comparison equality, three ambient levels, RGB and alpha.
Both WARP and hardware pass. Full particle VS/PS backend tests separately
exercise actual native depth textures, lit/shaded quantized RGB16/6, preserved
alpha and geometric depth, stage2 restoration, ordinary-path parity and
rejection without output/count mutation. Static tests reject changed shader
or CPU bytes and verify fractional offsets and simultaneous ALU operands.

Run312's first four projected batches contain8,8,6,8 particles. Subsequent
batches also render. First completed native inputs are under
`build/automatic-startup/reach-game-312/captures/particle-projected-native`:
416-byte constants,512-byte original staging and2560-byte decoded vertices.
Run312 ends after49 scene presentations/1585 scene draws at a new mono stream.

312b's capture after the first projected draw:
`build/automatic-startup/reach-game-312b/captures/native-frame-1849184.rgb10a2`
and JSON; presentation1228,1333 scene draws. Visually checked preview:
`build/reach-game-312-preview/native-frame-1849184.png`.
Raw SHA256:fdee068179fe11b8f0007fcc431b385f84da240dbf1d778a707c6d73078cd8ca.
The explosion is visible over the chocolate horizon; pink ground and large
malformed black geometry remain. No gameplay correctness claim follows.

## Dialogue qualification

The qualifier now retains a bounded list of independently checked mono SNU
profiles. Each whole original source, normalized block hash/size, packet
padding, XMA1/XMA2 and split-read output,384-frame initial skip, every block's
quota and complete trimmed PCM are checked. Stock decoding provides the
independent comparison with measured576-frame alignment. Assets are unchanged.

Besides309's first dialogue source, two newly observed sources are qualified:

- `audiostreams/mr_xxx_0/d_homr_xxx_0000b1e.exa.snu`:
  SHA25616a44fbc062159fdc1bfa19e4fd46a95115cc55b7cc67c9eb3c4cd531da01740,
  header0300BB80400179DF,96735 frames/19 blocks. First reader-owned block is
  1506 bytes, hash ecd8171f39f12b5a53b5afae32c5c9f63bda16af8bb4e971e1098859042f4782.
  Maximum stock decoder difference1.7881393432617188e-7. Report:
  `build/mono-dialogue-xma-homer/report.json`.
- `audiostreams/hr_xxx_0/d_wchr_xxx_0000b34.exa.snu`:
  SHA256dbea569e4596a98a4b4181eaca06e076c41bff469992b233243300af5c5a5189,
  header0300BB8040014703,83715 frames/17 blocks. First reader-owned block is
  1064 bytes, hash29650d602787e2b55f7ffd4d6f3f8ffc89834951930cf840e0efd70fe247d0ff.
  Maximum stock decoder difference1.7881393432617188e-7. Report:
  `build/mono-dialogue-xma-wchr-b34/report.json`.

The first new source feeds live voiceE45A9620/generation38412 in312b. That run
continues to68 scene presentations, ending with1984 scene draws and21 new
draws at presentation1259. It then rejects the second newly observed source
before decode; that observation led to its additional312c qualification.

312b audio tests pass240890 checks across two sources/47 blocks/238528 frames.
The312c certificate and test totals now cover three sources/64 blocks/322243
frames. Qualification log: `build/reach-game-312c-mono-qualification.log`.
312c AOT verifies311 files with zero semantic diagnostics. Game and audio-test
builds pass; audio tests pass325410 checks across all three sources. Live312c
reaches75 scene presentations and2140 scene draws, then stops at an unknown
resident bank (header0300BB800000850E). Sustained rendering remains unverified.

## Characters resident bank

The newly reached sound belongs to original SToc entry4 of
`simpsons_chars/simpsons_chars_global.str`, resource `simpsons_chars_global.sbk`.
The1212431-byte bank has SHA256
`4cedafc16ca0c681a0e4284ca8ae65e38ef19197ce29387a3ac8cac2ed50f078`.
Metadata occupies0x43A8 bytes and audio starts at0x43C0 with1195087 bytes.
Reached sound1061146 contains34062 mono48k frames; its block starts1061154
and contains four complete XMA packets.

`tools/qualify_characters_resident_xma.py` verifies all66 ordinary sources
and three complete loops (loop start0):69 blocks/4547399 declared frames.
XMA1/XMA2 and split-read schedules match bitwise, with complete quotas,
384-frame initial skips, exact input/PCM hashes and no raw EOF. Four sources
requiring restored packet padding also match independent stock decoding with
maximum error1.1920928955078125e-7. The other65 use the native schedule and
variant comparisons; no independent-stock claim is made for those sources.
Report: `build/characters-resident-xma/report.json`; no sources excluded.

The generated certificate is admitted through the existing owned bank split,
hash, allocation-generation and source-accounting path. Runtime ownership
checks are unchanged. The new fixture exercises every source, fresh playback
of all three loops twice, full PCM quotas, retirement and mutated bank/header/
block rejection. Loc discovery retains its original default counts132/12.
312d AOT verifies311 files with zero semantic diagnostics; game,
ResidentXmaTests and ResidentBankLifecycleTests builds pass.
`build/reach-game-312d-resident-tests.log` records three passing tests:
CharactersResidentXmaSource (4755756 checks), LocResidentXmaSource (12955874
checks) and OriginalResidentBankLifecycle (13 checks). The original borrowed
bank lifecycle still validates copying, retirement and stale generations.

## Latest live result and next boundary

Live312d completes98 scene presentations, ending at presentation1292 with
2571 cumulative scene draws and18 new draws. The characters-bank stop is
passed. The next rejected original dispatcher call is82740680 from8273B4E0,
now with scene flags2. This selects packet+1C, the original `mono` typed
effectE1AA57E0 (vtable8215020C, wrapperE1AA5728, identity00500014,
source8211F480), instead of packet+18's normal material. No guard was relaxed
to admit this pass.

Original82740B38..82740BD8 calls mono virtual8273A878 with alpha selector0.
It selects typed+AC TechniqueOpaque0003FFFC, copies the object's original
frame matrix via823F2540 into its stack, then calls82704600 for the typed+B4
matrix parameter. The dispatcher sets kIsSkinned false through typed+B8,
commits and calls826B5770. Metadata+24 is zero for this reached static mesh,
so its eventual draw helper is826FF4C8. The existing static-mesh machinery
may be reusable, but mono's material, constants and pass still need native
qualification and integration.

The exact effect contains VS82120C04/82121BE8 (4060 bytes each,420 code
bytes) and PS82122BD4/82122D38 (348 bytes each,36 code bytes), with three
vertex FETCHes and a Boolean-controlled bone path; no morph FETCHes. This
is preliminary offline inventory, not a shader equivalence or GPU proof.
The full shader hashes, technique states and captured geometry hashes are
saved in `build/reach-game-312d-mono-evidence.json`. Original disassembly:
`build/reach-game-312d-scene-disassembly.txt` and
`build/reach-game-312d-mono-apply-disassembly.txt`.

Captured packet/metadata/geometry/elements/vertices/indices/typed data are
under `build/automatic-startup/reach-game-312d/captures/scene-*.bin`.
The vertices contain5796 bytes (stride28), indices988 bytes. The latest
visually inspected frame remains312b's malformed world.312d did not produce
a later inspected frame. Its automatic observer still reports
`input_sequence_completed=false` and `gameplay_verified=false`.
Opening-movie completion, sustained rendering and character control remain
unverified; the goal stays active. No game process remains running.
