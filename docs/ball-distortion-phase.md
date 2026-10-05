# Ball Homer distortion continuation

The original `82772468` phase is retained, including its sprite mask, both filtered reductions, emboss pass, scene restore, and displacement composite. Runtime supplies the original three XYUV RECTLIST vertices and effective state to `NativeBackend::drawDistortion`. The backend uses the existing post target ownership and packed RGB10A2 adapter, and restores all native bindings after every draw.

The screen vertex stage is original `82152880`. Filters use the previously qualified `82155F28`; the original scene restore uses `82152708` and PS c0. The two newly qualified records are:

- `82156150`, `Distort_Xenon_PSEmboss`: sample green at `v + 1/64` and `v - 1/64`, form their difference, output the two original ordered expressions for biased RG, the square root of twice the squared difference in B, and constant one in A. Original overlapping scalar/vector export masks establish the constant alpha.
- `82156340`, `Distort_Xenon_PSDisplace`: sample the distortion image, offset scene UV by `-(RG - 0.5)/48`, read scene RGB, and output `saturate(B * 5)` as alpha. Original packed blend word `0x10706` blends the displaced RGB with the restored scene using source alpha. A zero distortion field preserves scene RGB while writing source alpha zero.

`tools/analyze_ball_composite_shaders.py --verify --self-test` pins both complete records, executed schedules, their handle associations, the reviewed HLSL, and the local declarative scalar/export reference. It rejects 244 executable/source mutations. It is a build dependency of the native shader artifacts.

`BallEffectBackendTests` exercises positive and negative emboss edges, the displacement direction and scale, separate red and green coordinate channels, partial-strength blending, empty/full distortion, explicit UV filtering, and native state preservation. Its existing sprite tests cover transforms, texture green/alpha multiplication, all four blend modes, depth/stencil, clipping, and invalid state rejection. Native floating-point/sampler/raster behavior is qualified by these local tests; physical-console rounding parity is not asserted.
