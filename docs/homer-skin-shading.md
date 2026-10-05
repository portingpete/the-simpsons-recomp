# Homer hip outline and missing shading (2026-09-29)

The rear-view hip line and flat character shading exposed two errors in the
opaque skin pixel-shader generator (`tools/analyze_skin_shader.py`). Original
assets and the outline shaders are unchanged.

The final instruction, PS8200A02C slot37, exports vector XZ and scalar Y to
color0. The generator tried to replace separate X/Z assignments, while the
shared emitter produced a grouped `r0.xz=v.xz` assignment. Red and blue stayed
zero. The generator now routes both writes to output0 and rejects unexpected
write forms. The blue material flag distinguishes the adjacent pants regions
without drawing a black boundary across the hip.

The scalar MUL_CONST_0 instructions also need legacy zero-product arithmetic.
Live Homer commits use `fakeLightDir` c32=(0,0,0,0). Normalizing it generates
rsqrt(0); ordinary zero-times-infinity arithmetic then produces NaNs in the
light vector and changes the shadow-side predicate. These scalar products now
use the existing legacy multiplication helper, preserving zero components.

`tests/test_skin_shader.cpp` now executes the opaque pixel shader in addition
to the dual-textured shader. Its independent material formula checks all four
output channels, palette bands, object-ID overrides, light/shadow bands,
enable flags and the observed zero light vector. The first regression failed
with red=0 instead of 13/1023 on hardware and WARP. The second failed with
blue=.625 instead of .75 on both devices before the scalar correction.

All eight focused skin tests pass. Both game builds and the AOT gate pass
(311 generated files, zero semantic diagnostics). Temporary constant tracing
was removed before the final builds. The full suite was not rerun.

Live outline comparison:

- Before: `build/fps-benchmarks/homer-leg-seam-20260929-232427/captures/native-frame-320260.png`.
- Export correction: `build/fps-benchmarks/homer-leg-after-20260929-233123/captures/native-frame-342755.png`.
- Original lighting inputs: `build/fps-benchmarks/homer-leg-shading-20260929-233929/captures/skin-constants-*.bin`.
- Final combined fix: `build/fps-benchmarks/homer-leg-final-20260929-234754/captures/`.

The final completed rear-view renderer capture removes the extra hip line and
restores dark shading on the left side of the shirt and pants, consistent with
the supplied Xbox reference. `homer-rear-detail.png` is a direct crop of that
raw RGB10A2 capture with the same linear RGB16 conversion as the full previews;
no shading or pixels were corrected in the image. Exact console pixel parity
and a full mission playthrough were not assessed.

The final bounded run completed 120 seconds without an early exit and was
closed by the harness. Its `summary.json` records 9,604 measured presentation
intervals; the completed capture used for the detail is
`native-frame-1400240.rgb10a2`.
