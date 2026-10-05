# Reach-game307 — skin shader corrections and dual-textured shader qualification

Goal remains active. Run307 still stops at the unsupported dual-textured skin
source8201CD48 after12 accepted scene presentations. Sustained rendering and
character control are unverified. This follows [306](reach-game-306.md).

## Three existing skin rendering defects corrected

1. The opaque skin VS only indexed one of its twelve bone constant reads.
   Eleven reads silently used fixed c52/c53/c54. The shared offline emitter in
   `tools/skin_shader_emit.py` now emits all twelve as c[a0+52..54]. Co-issued
   MaxAs/vector instructions read the old address before updating a0. Indices
   are still the original values; no vertex/weight repair is applied.
2. `renderer/skin_mesh.cpp` mapped the physical UV/weight/color byte offsets to
   the wrong semantic numbers. Its shared `renderer/skin_input.h` now maps
   byte24 UV to TEXCOORD5, byte48 weights to TEXCOORD2, byte64 color to
   TEXCOORD4. Position, normal, indices and six morph streams keep their
   correct associations. SkinVertex remains152 bytes.
3. The material commit treated a zero bone lane as an unset value, allowing
   stale staging values to survive. `applySkinBonePalette` in
   `runtime/skin_material_constants.h` copies every lane in c52..c243,
   including zero. Rows outside the palette remain inherited. Runtime uses
   this function after the original stage uploads and private dirty filtering.

## New opaque dual skin shader pair

`tools/analyze_skin_dualtextured_shader.py` pins the whole original effect,
both complete shader records, headers, trailers, literal banks, input semantic
associations and complete control flow. It emits native HLSL offline under
build/shaders. No runtime translation or source admission was added.

- VS8201E6DC:4768 bytes, code starts3880,888 bytes including trailer,
  eight CF pairs,65 executable slots8..72. Thirteen fetches include two UVs,
  original indices/weights and six morph streams. There are twelve indexed
  bone reads and a single forward morph branch.
- PS82020B9C:1952 bytes, code starts1088,864 bytes including trailer,
  eight CF pairs,63 executable slots8..70. Base texture is t1/s1 with an
  RGB-to-xzy fetch; nine character-shadow taps use t0/s0 and projected zw.
- The scalar ADD_CONST_1 at slot23 reads c40.z+r1.x. Slot34 is individually
  predicated. Both require explicit handling beyond the shared rigid emitter.
- Generated entries: VSSkinDual,PSSkinDual,GSSkinDualProbe,
  VSSkinDualPixelProbe. CMake compiles these offline with FXC strict flags.
  They remain test artifacts; native material compiler/mesh/runtime bindings
  for this source are still unimplemented. Alpha pair is not transcribed.

## Validation

- AOT regeneration:311 files,zero semantic diagnostics;
  `build/reach-game-307-regenerate.log`. Generated C++ was not edited.
- Game and GPU tests build: `build/reach-game-307-build.log`.
- Six focused tests pass in `build/reach-game-307-tests.log`:
  NativeSkinShaderWARP/Hardware,NativeRigidShaderWARP/Hardware,
  OriginalSkinInventory,OriginalSkinDualInventory. Full suite was not rerun.
- New `tests/test_skin_shader.cpp` executes the actual production skin input
  layout on WARP and hardware. Each run checks24580 values/conditions in eight
  vertex draws (1024 vertices) and192 pixel draws. Independent geometric
  formulae cover all64 distinct bones, one-hot and blended weights, zero
  normals, six morph offsets, the morph threshold, clip/world/shadow transforms,
  colors and both UVs. Independent material formulae cover linear wrapped
  base sampling, normal/view lighting blend, object-ID and color packing,
  rim predicates, zero-length inputs and channel-distinct point shadow taps.
  These are native/reference arithmetic checks, not physical Xbox GPU proof.
- Offline tests reject every four-byte-word mutation of both records and
  truncations. Regression checks require all twelve bone reads to be indexed
  and the old a0 value to survive co-issue. Palette checks catch stale values
  under zero lanes and unintended writes outside its192 rows.

Run307 launches, replays menus and completes the existing skin draws with the
corrected layout/shader/commit. Last accepted presentation1286 has750 cumulative
scene draws and30 new scene draws. It then rejects the same unimplemented
graphics boundary82740680,caller8273B4E0,source8201CD48. The observer correctly
reports failure. It was waiting for the level movie and did not request a scene
readback before the stop; the last307 capture is the main menu. No visual
improvement is claimed. The last visually reviewed scene remains303's malformed
frame. No game process remains running.

## Concrete next integration data

Run307's `build/automatic-startup/reach-game-307/captures/scene-*.bin` captures
the actual rejected packet, metadata, geometry, declaration and vertices:

- Packet82D6E88C,metadataE6C26008,geometryE6C26040,typedE1AAC2C0,
  wrapperE1AAC0E0,effect00500023.
- Three bones; typed+48=0003FFFC,+4C=0003FFFE: the opaque pair is confirmed.
  Typed+A8 holds bone-array handle005C0026; first child handle00600026.
- Vertex bytes51D0, stride56,374 vertices;14 declaration rows including
  terminator. Stream0: position offset0,float3; packed normal offset12;
  UV0 offset16,float2;UV1 offset24,float2;indices offset32,byte4;
  weights offset36,float4;color offset52,packed color. Six zero-morph stream
  declarations follow. The current decoder only accepts stride48/13 rows.
- Opaque context5F50. Private bank4752 bytes/1188 words,90 leaves;
  bone leaves19..82 begin at private word100,16 words per matrix; native
  VS palette still starts c52,three rows per bone. Do not reuse old source's
  leaf18/word96 or bone-array handle00580024.
- Private PS mappings: leaf14/word80 -> c49; leaf15/word84 -> c40;
  leaf86/word1172 -> c47; leaf87/word1176 -> c46. Other mapped lighting
  leaves8..11 target c33..c36. VS morph leaf17/word92 -> c39,
  leaf18/word96 -> c38. Inheritance/filtering must follow original uploads.
- Private base sampler leaf83/handle016000A6 maps pixel stage1. Shared
  character shadow handle0020000F maps stage0; world shadow is unused.
  Shared view position maps pixel c4, character transform maps VS c26,
  shadow amount c30 and receiver c31. Preserve attached-pool remapping.
- Stage0 original sampler rows: addressXYZ2,mag/min0,mip2 (point clamp,
  base level). Stage1: addressXYZ0,mag/min1,mip1 (linear wrap).

Remaining work: decode/own UV1, qualify native compiled pair and input layout,
add skin dual color/depth adapter plus owned texture/sampler binding and state
restoration, extend source-specific bone/material/texture lifecycle and packet
owner checks. Preserve the original nonzero-bone immediate fallback and all
cache decisions. Only then admit8201CD48 in scene dispatch and launch again.
Existing skin texture hooks are zero-sampler-only and cannot simply be reused.
Earlier renderer orientation/depth/material defects and broader test concerns
remain open. Original assets and reference trees remain unchanged.
