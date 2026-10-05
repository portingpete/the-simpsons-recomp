# Native D3D11 movie backend

`renderer/movie_pipeline.cpp` implements `NativeBackend::drawMovie` using the
owned `MovieDraw` packet. No app, RenderWare, guest stream cache, runtime,
shader arithmetic, or viewport state is changed by this service.

The qualified input is one selected single-sample RGB10A2 color target and
same-sized selected depth/stencil target; one full-target native viewport with
depth endpoints 0/1; three independent single-level R8 planes in Y/Cr/Cb
(original frame order 0/2/1); matching half-width/half-height chroma; and the
three exact rectangle/UV profiles in `movie_geometry.cpp`. Target dimensions
may differ from plane dimensions. The shader samples common normalized UVs.
Samplers require linear min/mag, ordinary point/linear mip filtering, wrap or
clamp addressing, zero bias/minLOD, finite nonnegative maxLOD, anisotropy1,
comparisonNEVER, and finite border. Live boot150 uses clamp and 1280x720 /
640x360 planes. Depth comparison requests must be 0..7.

All packet, native backing/view/ownership, selected-target, viewport, predicate,
stream-output, and output-UAV checks precede context mutation. Resource/state
allocation and immutable vertex upload also complete before mutation and
pipeline-cache publication. A single `Draw(4,0)` renders a triangle strip into
R10G10B10A2_UINT, with disabled depth/stencil, filled unculled rasterization,
scissor disabled, replace blending, full sample mask and RGBA writes. The
compatible packed resource is copied back exactly. The counter increments
immediately after that Draw, including any later device-removal failure.

On return, actual color/depth attachments, viewport/scissor rectangles, native
IA stream0 buffer/stride/offset, other streams, index binding, all constants,
other resource/sampler slots, compute UAVs and null-predicate value survive.
PS resource slots0..2 are unbound. The chosen VS, float movie PS, declaration,
samplers, raster/blend/depth requests remain selected; the integer PS is used
only while rendering the packed target. D3D11 canonicalizes inactive sampler
anisotropy and disabled depth comparison fields in `GetDesc`; the original
engine request remains the integration owner's responsibility.

The shader is independently specified in [movie-shader.md](movie-shader.md).
It has no constant buffer, exports alpha +0, and uses the original literal
offsets/coefficient bits. Uniform bytes 16/128/128 produce native packed
components `(3,0,4,0)`, word `00400003`. Native float arithmetic, interpolation,
filtering and nearest-even integer packing do not establish console parity.

## Shader linkage requirement

Both compiled movie PS input signatures must match `VSTextured`: SV_Position
register0 followed by TEXCOORD0 register1. An entry accepting only float2 UV
places TEXCOORD in register0 and fails D3D11 shader linkage, even though uniform
planes can appear correct without the debug layer. This was detected by the
asymmetric GPU fixture and confirmed by `DEVICE_SHADER_LINKAGE_REGISTERINDEX`.
Only the shader owner changes that signature; no color arithmetic changes.

## Focused verification and integration

`tests/test_movie_backend.cpp` takes no arguments for WARP, or `--hardware`.
It checks the independent inspector's literal CPU fixtures, complete output
rectangles, spatially asymmetric planes, all UV branches, channel/plane order,
independent samplers, writable updates, queued input release/mutation, native
stream restoration followed by a real float-shader draw, and a full 1280x720
fixture. Uniform packed pixels and alpha are exact. The spatial fixture uses
a 32x32 target with 8x10 luma / 4x5 chroma: texel-space coordinates on all three
UV branches lie on a dyadic grid after fixed-point snapping, avoiding ambiguous
subtexel rounding boundaries. The asymmetric planes are separable sums with
luma deltas divisible by32 and chroma deltas divisible by64. Every filtered
sample is byte-representable, checked by the test before evaluating the color
oracle. The separate CPU sampler uses nearest-even 16.8
coordinates, as described by the [D3D11 functional specification sections
3.2.4 and 7.18.16](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).
All packed pixels, including spatial RGB and alpha, require exact equality.
The earlier 32x24 / 8x6 crop fixture exposed
adapter differences at fractional boundaries; its failed logs are preserved
under `build/movie-backend/*linkage-fixed.log` and `*native-sampler.log`. Arbitrary
between-byte filtered colors also differed on hardware with the exact grid;
those failures remain in `*exact-grid.log`. The final fixture removes that
format-precision ambiguity, and both devices matched every packed pixel with
zero error. No threshold was raised to accept earlier failures. Invalid calls compare
actual D3D state, target/depth bytes, counter and cached pipeline identity.
The optional SDK debug layer additionally rejects draw/linkage errors.

Main integration needs these entries in its existing build:

* Add `renderer/movie_pipeline.cpp` to `SimpsonsGraphics`.
* Compile `PSMovie` / `PSMoviePacked` in `renderer/movie_shader.hlsl` with FXC
  `/Ges /Gis /O3 /T ps_5_0`, output `PSMovie.h` / `PSMoviePacked.h` with variable
  names `kPSMovie` / `kPSMoviePacked`; retain existing `VSTextured.h`.
* Add executable `MovieBackendTests` from `tests/test_movie_backend.cpp`, link
  `SimpsonsGraphics`, compile `/fp:strict /W4 /WX`.
* Register `NativeMovieBackendWARP` with no arguments and
  `NativeMovieBackendHardware` with `--hardware` (60-second timeout).

After main configures its build, the focused commands from the workspace are:

```powershell
cmake --build build/native --target MovieBackendTests
ctest --test-dir build/native -R '^NativeMovieBackend(WARP|Hardware)$' --output-on-failure
```

These shared-build commands are for main to run. This worker builds only the
isolated executable using `build/movie-backend/build.cmd`, from the workspace
root, and runs `build/movie-backend/MovieBackendTests.exe` / `--hardware`.
The script compiles the two owned files with ClangCL `/fp:strict /W4 /WX`;
unchanged dependency translation units use their ordinary warning policy.
All outputs stay under `build/movie-backend`; it consumes existing shader and
effect-catalog headers without rebuilding the shared project. Local movie
headers must first be compiled from the final shader into that same directory.

Final isolated qualification (2026-09-11): both WARP and hardware passed
3,775,647 checks with the SDK debug layer enabled. Maximum spatial RGB error
was zero codes on each. Strict compilation is recorded in
`build/movie-backend/compile-final.log`; complete results are
`build/movie-backend/warp-final.log` and `hardware-final.log`. These checks do
not certify movie decoding, complete playback, presentation, or console parity.
