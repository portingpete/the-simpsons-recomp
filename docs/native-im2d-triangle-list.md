# Native Im2D complete triangle lists

Main built this patch and verified449,410 Im2D checks on WARP and hardware.
Logs are `build/im2d-upload/focused188-list-detail.log` and
`build/im2d-upload/im2d-list188-hardware.log`. The original driver fixture also
passes with157 Im2D draw checks. Actual game verification remains separate.

`Im2DDraw::primitiveType` now admits original primitive3 as a triangle list:
3 through9360 vertices, divisible by3. Original primitive4 remains a triangle
strip with its existing 3 through9362 bound. Other topologies, incomplete list
triplets and out-of-range counts fail before upload or context mutation.

The packet field and layout are unchanged. The only rendering change is index
construction: a list emits consecutive indices, grouping `(0,1,2)`, `(3,4,5)`
and so on. It never applies the odd-triangle swap required for a strip. Both
paths use the existing native triangle-list input assembly and one ordered
three-index draw per triangle. The previous packed color is copied before each
triangle, and the selected depth attachment remains attached throughout.
Alpha discard, depth comparison/write, channel masks, explicit RGB10A2 packing,
BC2 sampling, shader constants and cleanup retain their existing behavior.
Native list/strip topology meanings are documented by
[Microsoft](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d11-primitive-topology).

## Original evidence boundary

The existing [original disassembly](/K:/SimpsonsNativeCopy/build/im2d-upload/original-chain.txt)
shows `82409358=2F1A0003` and `8240935C=419A0024` selecting primitive3.
`82409380=39600003` and `82409384=7FBE5BD6` compute signed vertex count/3.
Main's [list188 evidence](/K:/SimpsonsNativeCopy/build/im2d-upload/list188-evidence.json)
records19 instruction pins,7 topology-table words and a reused154-word upload
span. In particular, the table lookup at82409520/24/30 maps primitive3 through
`82062E08 + 4*3`, then calls823F4B60. The original upload/cursor accounting stays
in Main's AOT/runtime work.

[Actual159](/K:/SimpsonsNativeCopy/build/boot-159.log:12061) reaches primitive3,
498 vertices (166 complete triangles), source0203A7C0, caller826C0A5C and
rasterE1AC6548. Main owns the original font resource, state qualification and
real-AOT integration fixture. This backend patch does not reinterpret those
guest resources or claim that the original glyphs have rendered correctly.

Although the original division has behavior for nonmultiples of3, that behavior
and associated cursor/upload semantics are outside this bounded backend profile.
Incomplete lists are rejected explicitly instead of silently dropping vertices.
The upper bound is the complete-triplet subset of the existing upload bound.

## Added native GPU fixtures

[test_im2d_backend.cpp](/K:/SimpsonsNativeCopy/tests/test_im2d_backend.cpp) adds:

- Two separated triangles with different colors, depths and opposite winding.
  Both winding reversals and cull0/2/6 are exercised. Every color/depth/stencil
  pixel is checked, including the gap where erroneous strip bridges would draw.
  The triangles have4x3 extents; a half-space oracle uses integer doubled sample
  coordinates, with no sample lying on a sloping edge.
- Two overlapping list triplets with different RGBA colors, reversed depth,
  LESS/LEQUAL/ALWAYS comparisons, write on/off, alpha rejection, and masks0/5/15.
  The oracle applies each masked packed write before the next blend and each
  enabled depth write before the next comparison. Equal depths and float inputs
  that tie only after20e4 conversion are included. Cull6 catches an erroneous
  list parity swap. Mask0 still permits depth writes.
- Valid counts3,498 and9360. The long buffers have a degenerate prefix and two
  final triangles covering the target, detecting truncation or lost tail indices.
  The minimum count renders one independent triangle. These draw an exact opaque
  BC2 blue texel multiplied by nontrivial diffuse RGBA; caller upload bytes are
  overwritten and packet/texture references released before output observation.
- Rejection of empty, short, incomplete and oversized lists, including complete
  oversized counts and a nonfinite vertex at the end of a9360-vertex list.
  Valid lists with foreign textures or unselected attachments also reject.
  Existing rejection checks require unchanged pixels, depth/stencil, counters,
  attachments, viewport and native state. The old primitive3/four-vertex test
  remains as an incomplete-list rejection.

Every new submission uses the existing shared debug-layer, resource-selection,
viewport, packet-counter and transient-buffer cleanup checks. The preceding
359956-check strip/BC2 contract reported by Main has not been rewritten or
relaxed. These are additional tests, pending execution; no new total is claimed.

## Scope and Main validation

Only `renderer/im2d_draw.h`, `renderer/im2d_pipeline.cpp`,
`tests/test_im2d_backend.cpp` and this report changed in this worker task. No
HLSL, CMake, runtime, AOT, original bytes or other tests/docs were edited.
The worker ran no build, tests, AOT generation, game or UI actions.

After shared build coordination, Main should build `Im2DBackendTests` and run
the same full native draw suite on WARP and hardware:

```powershell
.\build\original-screen\build-focused.ps1 -Targets Im2DBackendTests
ctest --test-dir build/native -R '^OriginalIm2DNativeDraw$' --output-on-failure
.\build\native\Im2DBackendTests.exe --hardware
```

Main's original-AOT fixture and actual160 run remain separate integration
evidence. GPU fixtures verify the declared native topology/order policy, not
physical-console subpixel coverage, arbitrary BC2 interpolation or display parity.
