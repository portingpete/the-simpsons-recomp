# Reach-game308 — dual-textured skin runtime integration

Goal remains active. Opaque dual-textured skin source8201CD48 now completes
its native draw, advancing past307's rejected source. Latest run308c stops at
an unimplemented four-vertex immediate billboard reservation, caller8275F6E4.
There are still only12 completed scene presentations; sustained rendering and
character control are unverified. This follows [307](reach-game-307.md).

## Implemented and exercised

- Source-specific profiles retain the original opaque technique, shader pair,
  private bank, bone array, declaration and stride. Alpha remains unqualified.
  Dispatch and packet ownership now accept8201CD48 only through those checks.
- The original56-byte vertex layout decodes both UV sets; native SkinVertex is
  now160 bytes, with UV1 at152. Old attribute offsets remain unchanged. Both
  declarations reject duplicate/missing morph semantics, overlapping fields,
  wrong types/strides and nonfinite consumed floats.
- The native material compiler binds VS8201E6DC/PS82020B9C, with a matching
  input layout and a color/depth pixel adapter. Mixed skin pairs reject.
- The original three-row shadow lookup/usage/mapping/binding walk retains the
  character depth owner and binds it at stage0. World/edge unused rows still
  execute their original usage queries. The original stack, context, pass,
  handles and resource lifetimes are checked.
- Private base texture leaf83/word1124 commits a retained native ITXD texture
  at stage1. Private PS leaves14/86/87 project to c49/c47/c46. Bone leaves19..82
  start at word100; all64 three-row palettes retain zero lanes. Reflected
  private exclusions2,8,9,10,11,12,15,17,18 remain inherited from original
  stage uploads, including lighting, object ID and morph values.
- Both skin submission paths share texture preparation. The actual hook at
  827018C0 is distinct from the older generic827013B0 path; the first308 run
  exposed this duplicate path before any dual draw was accepted.
- Original stage0 point/clamp/base-level and stage1 linear/wrap samplers are
  qualified against all20 state fields. Native character depth samples use
  the explicit D24FS8 RRRR policy. Immediate submission restores both sampler
  slots and other changed GPU state. Recorded native draws retain texture,
  depth, shader-resource-view and sampler ownership and pass replay checks.
  Runtime still preserves the original nonzero-bone immediate fallback; no
  original cache decision was bypassed or replaced with native replay.

## Validation and run evidence

Latest AOT regeneration:311 files,zero semantic diagnostics in
`build/reach-game-308c-regenerate.log`. Game and tests build in308c-build and
308c-test-build logs. Generated C++ was not edited.

Nine focused tests pass in `build/reach-game-308c-tests.log`: skin decoder,
skin shader WARP/hardware, skin mesh WARP/hardware, both skin inventories,
native material artifacts and original engine resource bridge. The latter
test's stale support list was brought into agreement with already-qualified
native shader identities. Full suite was not rerun; earlier broader defects
remain documented.

New mesh checks cover actual color/depth/stencil output, character-shadow RRRR
sampling, UV1 packing, immediate/recorded parity, sampler/state restoration,
unchanged sibling output, missing/mismatched resources, layout mismatch, and
released live replay state. Pixel fixtures avoid a half-code red quantization
tie where WARP/hardware legitimately differed by one code. Existing shader
tests independently exercise the full arithmetic and input layout. The307
captured374-vertex dual mesh passes4550 decoder checks including synthetic
cases for both layouts. Compiler population is48 compiled/196 unsupported.

- Run308 initially reached the dual material's base upload and shadow binding,
  then rejected the alternate skin submission path's missing texture fields.
- Run308b completes273 old-skin draws and one dual-skin draw before the new
  immediate billboard blocker. Last accepted scene presentation1302 has750
  cumulative scene draws and30 new draws. Twelve scene frames total.
- Run308c reproduces the same blocker and preserves its CPU inputs under
  `build/automatic-startup/reach-game-308c/captures/immediate-billboard`.
  Last accepted scene presentation1254 again has750 cumulative/30 new draws.
  The diagnostic makes no reservation and submits no fabricated geometry.
- No game process remains running.

An extra request in308b captured an early scene at presentation1292, before
the new dual draw: `captures/native-frame-2059997.rgb10a2`. Its unmodified
RGB10-to-RGB16 preview is `build/reach-game-308-preview/native-frame-2059997.png`.
Visual inspection shows the HUD and autosave overlay but an incorrectly
rendered world with a large pink region and malformed/black geometry. It does
not establish visual correctness of the new dual draw or playable gameplay.

## Next immediate billboard boundary

Original function8275F168 builds four32-byte vertices per particle and calls
reservation82751E38 at8275F6E0, returning to8275F6E4. The current native service
supports only the separate trail caller8277E640, so the new caller rejects.

Captured308c: ownerE2EEBFA0,vtable821530EC,stack0203F350,entry0203F3C0,
particle/blockE5317F70,index0. DefinitionE6232D20 has wordD0=04000003,
wordD4=080C0008,byte104=0,byte40=0; secondary texture owner+D4=0. This selects
mode zero. TextureE1B5BF20 points to rasterE1B5BF98, sampling word1102.
The ITXD lifecycle identifies this asset as `star_glow`, headerE1B5BFEC,
format18280186,generation385; its CPU copy/relocation was verified and GPU
upload deferred. Resolve that retained owner rather than the skipped binding.
Stack, owner, definition, material, particle, texture, raster, ring,
view-projection and selected shader tokens were captured before reservation.
`state.txt` includes the effective scalar/sampler state, which is diagnostic
only: some original console state writes currently fall into null-device
scratch and therefore do not establish correct native state.

Concrete original sites for integration:

- Setup82751B68 at8275F218 currently reaches the existing mode-zero native
  setup. It selects original VS821511D8/PS821509D8 through the existing
  shader globals; their native shader implementation already exists.
- Texture setup82751988 at8275F228 and state block8275F22C..8275F2F4 need
  owned native texture/sampler/scalar handling. Do not use the current skipped
  texture bind or null-device writes as proof of a complete draw contract.
- Per-particle transform827518D0 at8275F490 retains original matrix kernel
  827B8328. At82751934 its r5 points to four completed constant rows. Preserve
  this CPU math while replacing the console constant upload.
- Color stores8275F494..8275F4C4 use original stack+80..8C. Soft-particle
  helper8275CBD0 at8275F4E8 writes an extra bank unused by the qualified
  mode-zero shader; mode variants need separate qualification.
- Reservation82751E38 at8275F6E0 precedes the original CPU quad construction
  8275F6EC..8275F7E8. A native submission belongs after those stores, before
  8275F7EC advances the original particle loop. End82751DA0 at8275F814 must
  close the same owned lifetime. Preserve original loop/skip/count behavior.

Original assets and reference trees remain unchanged. The next work is a
qualified native billboard lifecycle, followed by another live run. Earlier
orientation/depth/material defects still need investigation.
