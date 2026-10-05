# Resource crash repair and coverage audit — 2026-10-01

The rebuilt native executable completed the first-mission completion route,
including cutscene skip, mission recap and the next loading screen, then
remained playable in the Simpsons house while movement, attacks, special
actions, jumping and character switching were exercised. The final run
`20261001-181937Z-c722f3a6` ran for **538.61 seconds (8 minutes 59 seconds)**,
reached **28,489 presentations** with no unexpected failure, and ended
through `WM_CLOSE` sent to its verified game window.
[The verification receipt](../build/resource-crash-audit/final-live-verification.json)
records that the tested executable still matches the current native binary,
SHA256 `ee1a9b26eece735fa66cd74b0447300feded086a6d4502cd8a31d1d65bb62bf0`.
This establishes the repaired route and exercised house actions; it does
not establish that every later mission or effect is supported.

The run used private profile and content stores. The primary profile file,
all 253 primary content files, and the primary video settings remained
unchanged. The [final completed-front capture](../build/resource-crash-audit/runs/20261001-181937Z-c722f3a6/final-capture/native-frame-4732998.png)
shows the house fireplace after exploration. Its renderer receipt reports
891,001 scene geometry draws, including 407,720 rigid and 483,271 skin
draws, at capture presentation 27,209; the run continued to presentation
28,489. These are draw and presentation counts, not a gameplay FPS
measurement or a claim of exact console display precision.

The original live failure was a texture ownership mismatch. The diagnostic
reported texture value/header `00F00020` at `82C1DBA0`, caller `826B560C`,
for typed effect `E1AA88F0`, source `8200CCB8`. That value is the exact live
identity of the stock white builtin texture, created from `82CED9A8` and
published at `82D63004`; it is not an ITXD guest header. The original
`8270ACC0` callback can fetch that builtin identity, and passes it directly
to the texture binder at `824408E0`. Treating it as an ITXD header caused
the strict ITXD lookup to subtract `0xCC` and reject the owner.

All material texture consumers now use
[`EngineDriver::materialTexture`](../runtime/engine_driver.cpp). It first
requires the live driver and its runtime/thread owner. An exact builtin
identity is accepted only while its complete original publication bundle
is intact, and its backend texture is validated. Other values go through
the existing strict, published ITXD-header lookup. There is no acceptance
of arbitrary native IDs: foreign reflection IDs, zero, forged identities,
stale builtin identities and retired ITXD headers remain rejected. Named
palette consumers keep their separate palette-header contract, while
scene-copy and shadow resources retain their own qualified owners.

Selected-pass sampler usage is now checked before looking up a material
texture. The original effect category-7 bitmap and actual selected map
determine whether the leaf is consumed. A zero-usage leaf with no mapping
requires no texture owner; a consumed leaf must map to one pixel sampler.
Malformed, mixed-stage, missing or multi-lane mappings are rejected by
[`pixelTextureStage`](../renderer/material_texture_usage.h). Skin shadow
bindings use the selected opaque or alpha context rather than an opaque
cached reflection map. This matters because base skin's opaque pass has
no material sampler, and the supported skin alpha passes do not consume
the inherited shared shadow samplers.

The sky's independent fourth sampler was also corrected. Original source
`82036448` maps private leaves 20, 21, 22 and 24 to stages 0, 1, 2 and 3 in
both relevant maps. Leaf 23 is an unused palette; its value at word 152
must not supply the line/discard sampler. The fallback now reads the
original line value at **word 168**. When the original stage-3 callback
supplies a live scene-copy identity, that backing remains preferred and
is validated through its context, publication and owner checks.

The effect audit also exposed shader and dispatch gaps beyond the first
texture failure. The supported dual-skin alpha pass and the newly reached
textured-skin family now have their original shaders, material maps and
geometry contracts represented in the native backend:

| Original effect | Source | Opaque VS / PS | Alpha VS / PS | Consumed geometry |
| --- | --- | --- | --- | --- |
| Base skin | `82006348` | `82007C1C` / `8200A02C` | `82008E20` / `8200A4A4` | 48-byte stream, UV0 |
| Dual-textured skin | `8201CD48` | `8201E6DC` / `82020B9C` | `8201F984` / `82021344` | 56-byte stream, UV0 and UV1 |
| Textured skin | `8200FB98` | `8201146C` / `82013900` | `820126EC` / `82013F8C` | 48-byte stream, UV0 |

The next live unsupported-boundary failure identified typed `E1AAB350`,
vtable `82061714`, identity `00500021`, source `8200FB98`, at original
dispatcher `82740680`, caller `8273B4E0`. The original catalog identifies
this source as **simpsons_skin_textured**. Textured skin uses the standard
single-UV bone layout; it does not acquire dual-skin UV1 geometry merely
because its opaque pass uses two textures. Its opaque character depth is
at stage 0 and material base at stage 1; alpha consumes the base at stage
0. Both passes retain AlphaTestEnable at pixel constant 48. The final live
log confirms **51 FB98 opaque pass selections** with
`VS8201146C / PS82013900`, and an original immediate fallback completion
receipt with `draws=1`, past the original failing boundary. Pass selections
are sampled diagnostics rather than a count of every FB98 draw.

The skin fallback now accepts both original Boolean values of incoming
r5. This is grounded in original instruction `82740120 = 80BF0004`, which
overwrites r5 with `packet.object` before its first use. The dispatcher
can supply either metadata bit-1 value. Caller, source, vtable, r4/r6
Boolean, frame, active owner, context and recording-lease checks remain
in place; this change does not authorize an arbitrary fallback caller.

The packaged texture census found one additional metadata admission gap:
`moh_palette_dark_w_s`, a 16×16 RGBA8 base image in
`medal_of_homer/medal_of_homer/story_mode/zone01.str`, entry 15,
`zone01_split11.itxd`. Its allocation is a single 4096-byte 32×32 tile,
with no extra mip levels, and descriptor words
`80400002 00000086 0001E00F 00000C14 00000000 00000200`.
The existing RGBA decoder now accepts that exact small-image profile;
descriptor, allocation and other dimension guards remain strict.
[`prepare_itxd_small_rgba_fixture.py`](../tools/prepare_itxd_small_rgba_fixture.py)
pins the original payload and independently derives all 256 logical
texels from physical inverse addresses and original lane selection.
The WARP and hardware regression checks decoding, channel mutations,
padding isolation, upload/readback, point sampling, and rejected profile
mutations. This was a census finding, not a second observed house crash.

Verification includes original CPU traversal, independent metadata and
pixel oracles, malformed-input checks, and native GPU execution. The
earlier expanded native group passed 24/24 tests in 30.94 seconds, and the
three original skin fixtures then passed in 1.88 seconds. The final native
and release suites supersede those preliminary totals:

| Verification | Result | Evidence |
| --- | --- | --- |
| Final native focused CTest suite | 55/55 passed, 25.52 seconds | [final-native-tests.log](../build/resource-crash-audit/final-native-tests.log) |
| AOT verification | 311 files, zero semantic diagnostics | [final-aot-verify.log](../build/resource-crash-audit/final-aot-verify.log) |
| Final release focused CTest suite | 55/55 passed, 26.30 seconds | [final-release-tests.log](../build/resource-crash-audit/final-release-tests.log) |
| Completion shortcut | Current native launcher, `--first-mission-completion` | [final-shortcut-validation.json](../build/resource-crash-audit/final-shortcut-validation.json) |
| Live native route | No unexpected failure; owned window close | [final-live-verification.json](../build/resource-crash-audit/final-live-verification.json) |

[`test_skin_pass.cpp`](../tests/test_skin_pass.cpp) executes the complete
original `8273B4D0 → 82740680 → 827400F8 → 82701638` draw path for base,
dual and textured skin, with all four r4/r5 Boolean pairs. Its **1,506
checks** comprise textured 496, base 502 and dual 508. Twelve real draws
perform twelve original `826FE7C8` bone compositions using dynamic
skeleton plugins, two joints, bind matrices and byte bone map `[1,0]`.
The independently checked palette is returned at `82D64080`. Original
material callbacks retain every authored vector lane and texture header,
then clear all 128 private dirty bytes. The fixtures preserve SP/LR and
nonvolatile GPR14–31/FPR14–31, require a changed center pixel and unchanged
outside pixel, and compare complete readbacks across both values of dead
incoming r5. Source, dictionary, geometry and skeleton bytes remain intact.

The separate textured-skin material test checks both original contexts,
all 86 private leaf usages, selected material rows, the first and last
bone leaves, and the 48-byte consumed vertex layout. Partial-dirty updates
preserve clean, inherited and unmapped lanes bit for bit, including
negative zero and a subnormal value; short banks and invalid layouts are
rejected without modifying retained data. The texture-map regression
checks **83 selected rows across 13 families: 38 consumed and 45 unused**,
including all three shared skin shadow rows for both passes of all three
skin families. The real builtin lifecycle plus stock ITXD publication and
original group-retirement fixture passes **3,205 checks in both builds**.

The broader inventories expose remaining work rather than proving every
catalog entry playable. The current
[effect admission report](../build/resource-crash-audit/effect-admission.json)
covers **49 effects, 110 technique/pass combinations, 215 registered
shaders and 256 immutable material identities**. There are 80 native
material artifacts, of which 63 correspond to registered-effect shader
identities. **33 passes have all required artifacts**, **30 passes have
an explicit declared runtime selection**, and the sampled final live log
observes **four pass pairs**. These categories are distinct. **77 original
passes still lack required artifacts.** Those passes are substantive
remaining coverage gaps and could cause guarded failures if later
gameplay reaches an unsupported path; their presence in the catalog does
not itself prove that any particular mission will reach them. Instruction
equivalence candidates are review leads, not authorized shader aliases.
Asset VFX references and emitter combinations are not fully parsed, and
the independently implemented direct particle path has its own bounded
contracts separate from registered particle-effect shaders.

The [texture metadata audit](../build/texture-runtime-audit/report.json)
verifies the original SHA256 of 362 packaged STR files and examines 863
ITXD dictionary occurrences, 709 unique dictionaries, and **7,318 texture
records / 5,882 unique metadata records**. Format counts are BC1 3,050,
BC2 1,542, BC3 1,126, RGBA8 1,301 and L8 299. After the exact 16×16 repair,
all metadata fits the current strict admission rules; the
[baseline report](../build/texture-runtime-audit/report-before-small-rgba.json)
retains the one prior gap. This bounded prefix audit expands 1,873,474
bytes of dictionary headers from 547,646 compressed prefix bytes, while
the dictionaries declare 825,647,104 bytes. It does not decode every tiled
payload or check every texture on a GPU. Archived family fixtures cover
13 authored cases / 43 mip levels and 15 synthetic cases / 90 levels;
the new actual small RGBA fixture supplies additional focused coverage.

The [audio audit](../build/audio-runtime-audit/report.json) verifies
**7,430 SNU/MUS containers, 9,470 streams, 377,643 original blocks and
677,048 layers**, with no discovered format/storage metadata violations.
Existing resident decoder coverage includes 9,024 unique encoded blocks;
AMX coverage includes 254 cues and 247 unique encoded keys. Structural
block coverage is not a full streamed-decoder census. Caller-derived
reader extents, optional seek/config commands, cancellation, stop/restart,
ring lifetime and worker scheduling remain subject to their explicit
runtime qualifications and cannot be proved from asset framing alone.

The original skin fixtures are deliberately bounded to one submesh,
fixed geometry, two joints and one influence per vertex. They do not
exercise morph streams, arbitrary four-influence skinning, large live
bone palettes, skin recording replay or every CPU status register.
Their material sampler uses `simpsons_palette`; `fire64bw3` is genuinely
loaded but not drawn. Unused-map tests prove map admission rather than
instrumenting a poisoned unused live texture value. Terminal fixture and
application host cleanup also does not claim complete paired original
global teardown. GPU shader and mesh tests provide additional numerical,
depth, state and ownership coverage, but the live result remains a
specific completed route and house exploration, not an all-game or
performance guarantee.
