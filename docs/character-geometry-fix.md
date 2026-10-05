# Character geometry fix — 2026-09-19

Homer and the rabbit now render with properly attached limbs and facial
geometry. Early and late live captures confirm that the stretched triangles,
detached eyes and displaced rabbit parts are gone. The preceding
[sky occlusion fix](sky-occlusion-fix.md) is retained.

## Two independent decoding errors

The color-pass skin decoder read packed bone indices from the high byte first.
Declaration001A2286 is unsigned byte4 with XYZW selectors after the stream's
8-in-32 endian conversion. The low byte is X, as already established by the
[character shadow evidence](native-character-mesh.md). Captured vertices with
bytes `00 00 00 09` and weights `(1,0,0,0)` must use bone9; the incorrect color
decoder attached them to bone0. `runtime/skin_vertices.cpp` now decodes the
same index/weight pairing as the shadow pass for both48- and56-byte layouts.
The shader's original WZYX fetch swizzle is retained.

The color material upload also transposed each bone matrix a second time.
Original setter826FDBE0 already transposes its source into four private
vectors. The upload must copy the first three vectors directly to c52..243,
as the existing character shadow upload does. A second transpose loses
translation components and changes the articulated transforms.
`projectSkinBoneMatrices` now projects those stored vectors directly, updating
only modified bone leaves and retaining the rest of the palette.

## Verification

The bone-byte regression failed on the old decoder. It now checks asymmetric
indices, color/shadow parity and the observed single-influence bone9 case in
both layouts. Matrix tests use64 distinct asymmetric transforms for both skin
profiles, poison the unused fourth vectors with NaNs, and verify that
unmodified bones remain retained. Existing GPU tests then exercise the
projected palette with skinning and morphs on hardware and WARP.

All seven focused checks pass: NativeCharacterMeshDecode,
OriginalCharacterMeshEvidence, NativeSkinVertexDecode, NativeSkinShaderWARP,
NativeSkinShaderHardware, NativeSkinMeshWARP and NativeSkinMeshHardware.
Latest AOT regeneration verifies311 files with zero semantic diagnostics;
the game and test builds pass. A final build dry run has no remaining
generation, compilation or linking. The full suite was not rerun.

Logs are `build/character-geometry-matrices-regenerate.log`,
`build/character-geometry-matrices-build.log` and
`build/character-geometry-matrices-tests.log`.

## Live comparison

Both baseline and final runs captured392,1837 and2481 cumulative scene draws.
The intermediate index-only correction changed but did not resolve the
deformation; it is preserved separately under character-geometry-after.

Baseline early frame:
`build/character-geometry-before-preview/native-frame-2063268.png`.
Final early frame (Homer and rabbit):
`build/character-geometry-matrices-preview/native-frame-1909560.png`.
Final late frame (Homer turning his head):
`build/character-geometry-matrices-preview/native-frame-1927116.png`.
All previews are direct RGB10-to-RGB16 conversions without visual correction.

![Corrected Homer and rabbit geometry](../build/character-geometry-matrices-preview/native-frame-1909560.png)

Final early raw SHA256:
`9d065e2f734a87a4d4f6e64c110e59b6dd007fc65b34d5be1f97f09f664d51eb`.
Final late raw SHA256:
`03e379c7f01d167bdfd6d989fb998886bb9d08ea5f52e54c568b2e15732593eb`.

Final run `build/automatic-startup/character-geometry-matrices` completes98
scene presentations and2571 scene draws, then reaches the existing unrelated
mono dispatcher guard82740680 (caller8273B4E0). Sustained gameplay remains
unverified. The updated objective was character geometry, and that visible
deformation is fixed; the earlier reach-in-game objective was superseded.
