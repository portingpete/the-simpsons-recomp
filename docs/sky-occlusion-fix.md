# Sky foreground occlusion fix — 2026-09-19

The sky no longer covers foreground world geometry. The final live capture
shows the chocolate ground, walls and railings where the previous capture
showed sky and a large pink region. Other rendering defects and the existing
mono pass stop remain outside this fix; gameplay is not verified.

## Causes and changes

The original sky VS82036F08 exports clip `(x,y,w,w)`, placing it at the far
plane. The native sky path uploaded reversed-depth constants but used the
color-only PS820374E8, leaving raster depth at 1. The actual game uses depth
enable1, write0, comparison6 (greater-or-equal), viewport1..0 and zero biases.
The incorrect depth therefore let the sky overwrite foreground samples.

`renderer/sky_draw.hlsl` now retains the original sky color shader and uses
the existing world depth adapter for viewport mapping, bias and 20e4 rounding.
Both immediate and recorded sky draws select it. Original geometry, depth
comparison and write policy are retained; immediate bindings are restored.

The offline sky transcription also omitted PS slot8's KILLGT instruction
because its destination write mask is zero. This instruction still discards
pixels: slot7 computes `.99 - mask.red`, and slot8 kills when zero is greater
than that result. The transcription now emits the strict comparison and
discard, with its operand/opcode contract and pinned local UCODE reference
checked. Original shader record hashes remain unchanged.

Explicit CMake object dependencies ensure regenerated sky shader headers are
compiled into their consumers in the same build. A dry run after the final
build has no remaining shader generation, compilation or link work.

## Verification

- The new foreground-depth regression failed on hardware and WARP before
  the fix. It now passes for normal and reversed viewports, depth writes on
  and off, and both immediate and recorded draws. Alternating foreground and
  background samples verify color, depth and stencil preservation.
- A separate mask regression also failed before the transcription fix. It
  now checks mask values252..255/255 around the .99 threshold, using an ALWAYS
  depth comparison to isolate discard. Discarded samples preserve both color
  and depth; both draw paths pass on hardware and WARP.
- All four focused CTest checks pass: NativeSkyMeshWARP,
  NativeSkyMeshHardware, NativeSkyVertexDecode and OriginalSkyInventory
  (eight Python cases, including rejecting a changed kill opcode).
- AOT regeneration verifies311 files with zero semantic diagnostics.
  The final game and test builds pass. The full suite was not rerun.

Logs: `build/sky-front-regenerate.log`, `build/sky-front-before-tests.log`,
`build/sky-mask-before-tests.log`, `build/sky-front-final2-build.log` and
`build/sky-front-final2-tests.log`.

Final run `build/automatic-startup/sky-front-b` completes98 scene
presentations and2571 scene draws. It stops at the same unrelated mono
dispatcher82740680, caller8273B4E0; the startup observer reports gameplay
unverified. The inspected frame is presentation1306, with1354 cumulative
scene draws and21 new scene draws:

![Corrected foreground occlusion](../build/sky-front-b-preview/native-frame-1991453.png)

Raw RGB10A2 SHA256:
`db7036d5f8e77328fca355c089fa015ba17fd929d2f65ef0f9db5b9d41778a45`.
Preview SHA256:
`810f3259bdd7ba78c32bf64cd76c09b69172fcdd5e979259b255b741d1491c7d`.
The preview is a direct RGB10-to-RGB16 conversion with no visual corrections.
