# Exact native movie rectangle geometry

`renderer/movie_geometry.h/.cpp` implements the complete geometry selected by
original movie draw `8282E3E8`: three original 16-byte XYUV records and a complete
four-vertex native triangle strip. It depends only on the C++20 standard library.
It reads no guest memory, retains no pointers, allocates no graphics resources,
and changes no backend or render state.

```cpp
const auto geometry = Simpsons::Graphics::buildMovieGeometry(lumaWidth, presenterByte41);
// geometry.originalVertices: original primitive8 / count3 / stride16 payload.
// geometry.nativeVertices: four owned MovieVertex records, triangle-strip order.
```

Both arrays contain host float32 values with exact original component bit
patterns. They are not guest big-endian byte buffers. A native consumer can
upload `nativeVertices` with stride16, POSITION float2 at offset0 and TEXCOORD0
float2 at offset8, using a four-vertex triangle strip. An indexed triangle-list
consumer uses `0,1,2, 2,1,3`. The existing identity textured VS supplies Z=0,W=1.
No CMake, runtime, shader, or other renderer integration is included in this
ownership scope. The implementation produces the actual completed vertex data;
it does not submit a movie draw.

## Original input and literal stores

Primary program evidence is the immutable VA-mapped `analysis/simpsons.pe`,
base `82000000`, 15,466,496 bytes, SHA-256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
The existing [draw proof](native-movie-draw.md) and
[original184 JSON](../build/im2d-upload/movie-draw-original184.json) supply
independently checked disassembly and literal data. Addresses and offsets here
are hexadecimal unless stated otherwise.

At `8282E548/54C`, the draw reads presenter+8 then luma raster+C. Instruction
`8282E550=2F0B0280` is **signed** `cmpwi cr6,r11,640`. Byte+41 is read at
`8282E580` or `8282E618` and compared with zero. There are four control-flow
branches but three distinct payloads:

| Signed luma width | Byte+41 | Vertex0 (X,Y,U,V) | Vertex1 | Vertex2 | Completed vertex3 |
| --- | --- | --- | --- | --- | --- |
| <=640 | zero | (-1,-1,0,0) | (1,-1,1,0) | (-1,1,0,1) | (1,1,1,1) |
| <=640 | nonzero | (-1,-1,0,A) | (1,-1,1,A) | (-1,1,0,B) | (1,1,1,B) |
| >640 | zero | (-1,-1,0.125,0) | (1,-1,0.875,0) | (-1,1,0.125,1) | (1,1,0.875,1) |
| >640 | nonzero | (-1,-1,0,0) | (1,-1,1,0) | (-1,1,0,1) | (1,1,1,1) |

| Literal | Original address | Exact float32 word |
| --- | --- | --- |
| -1 | 821DD110 | BF800000 |
| +0 | 821DD0D8 | 00000000 |
| +1 | 82000BB0 | 3F800000 |
| A | 82001894 | 3DCCCCCD |
| B | 820036E8 | 3F666666 |
| 0.125 | 8206A014 | 3E000000 |
| 0.875 | 8215D498 | 3F600000 |

Every branch writes all 48 bytes exactly once via twelve `stfs` instructions.
The small-width paths share stores `E594/E598/E59C/E5A0` before splitting;
the large-width paths share final stores `E6B0/E6B4/E6B8/E6BC`. The new evidence
records all twelve store PCs, offsets, literal source addresses and words for
each branch. Begin calls are `8282E57C`, `8282E624`, or `8282E664`; they receive
primitive8/count3/stride16 from `E568/E570/E574`. The textured declaration at
`82151724` establishes the XYUV field offsets independently of the stores.

`buildMovieGeometry(int32_t,uint8_t)` preserves the signed comparison for every
int32 value and zero/nonzero semantics for every byte. Width 0 and negative
widths select the <=640 branch; geometry alone has no original basis to reject
them. Resource validation belongs to the caller. Height, chroma dimensions,
aspect ratio and target size do not enter this geometry computation.

## Primitive8 completion proof

The following local files in `K:/Simpsons/RexGlueCurrent` were read only.
Their full hashes and reviewed line ranges are recorded in the new evidence.

1. [xenos.h:53](K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h:53)
   defines hardware primitive `0x08` as `kRectangleList`.
   [primitive_processor.cpp:438](K:/Simpsons/RexGlueCurrent/src/graphics/primitive_processor.cpp:438)
   consumes three original vertices per rectangle; its
   [index generation at line195](K:/Simpsons/RexGlueCurrent/src/graphics/primitive_processor.cpp:195)
   supplies four consecutive host strip indices.
2. [D3D12 rectangle expansion:2505](K:/Simpsons/RexGlueCurrent/src/graphics/d3d12/pipeline_cache.cpp:2505)
   selects the longest XY edge as the shared diagonal. Our squared lengths are
   `|12|^2=8`, `|20|^2=4`, `|01|^2=4`, so the selected order is uniquely `0123`.
   The first three emitted records preserve their inputs.
3. [Fourth vertex construction:2595](K:/Simpsons/RexGlueCurrent/src/graphics/d3d12/pipeline_cache.cpp:2595)
   applies subtraction then addition to position and each interpolator:
   `d = f32(f32(b-a)+c)`. The
   [Vulkan implementation:2701](K:/Simpsons/RexGlueCurrent/src/graphics/vulkan/pipeline_cache.cpp:2701)
   explicitly emits `NoContraction` subtraction then addition. The
   [vertex-shader expansion:994](K:/Simpsons/RexGlueCurrent/src/graphics/pipeline/shader/spirv_translator.cpp:994)
   corroborates that order. The
   [primary Xenia implementation](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/d3d12/pipeline_cache.cc#L2427)
   also contains the longest-edge selection and fourth-vertex expansion.

For the movie's coordinates, `(b-a)+c` is `(1,1)`; Z=0 and W=1 also survive.
For UV input `(u0,v0),(u1,v0),(u0,v1)`, the fourth is `(u1,v1)`.
This is an exact binary32 result for **all accepted profiles**, not just an
algebraic approximation: U uses 0/1 or exact binary fractions 0.125/0.875;
V subtracts identical endpoints first (`A-A=0`), then adds B. Thus directly
copying `{b.x,c.y,b.u,c.v}` matches the reference operation order bit for bit.
The code never reconstructs A as `1-B` (which rounds to `3DCCCCD0`, not
`3DCCCCCD`), performs decimal parsing, or inherits a guest rounding mode.

The proof qualifies this full, axis-aligned movie rectangle with culling
disabled. It is not a silicon measurement or a general proof of Xenos internal
precision and decomposition. The reference's
[draw utility caveat:83](K:/Simpsons/RexGlueCurrent/include/rex/graphics/util/draw.h:83)
explicitly leaves general rectangle culling/decomposition under investigation.
No such broader behavior is implemented or inferred here.

## Orientation, raster policy and observed boot150

The original numeric correspondence is preserved: vertices0/1 have Y=-1 and
the first V endpoint; vertex2 has Y=+1 and the second V endpoint. Vertex3 uses
Y=+1 and that same second endpoint. Primitive8 does not independently identify
screen top, decoded row order, or texture orientation. No Y flip, V flip,
endpoint swap, half-pixel shift, or viewport-dependent scaling is applied.

The [original textured VS proof](screen-shaders.md) gives unchanged XY/UV and
Z=0,W=1. The movie explicitly requests HALFPIXELOFFSET=1 at `8282E3F8/E404`.
The [screen raster policy](native-screen-raster-policy.md) therefore uses
unchanged clip-space XY/UV and an unshifted host viewport. Read-only
[draw.cpp:325](K:/Simpsons/RexGlueCurrent/src/graphics/util/draw.cpp:325)
applies the half-pixel translation only to the separate integer-center mode.
Exact full-target clip corners with the integral 1280x720 viewport meet the
policy's common-grid position subset. This does not establish shader output,
texture sampling precision, or arbitrary fractional raster equivalence.

The existing general screen bridge expects a different ordered Y convention;
passing these vertices through that geometry-order gate requires a separate
movie consumer. It must preserve this output, not reorder or flip it to satisfy
the screen helper. Existing target/viewport, scissor, guard/clip, shader,
depth/stencil, blend and sampling qualifications still belong to that consumer.

The user reports actual boot150 inputs: lumaWidth1280, presenter+41=1, camera
1280x720, color RGB10A2, depth disabled, halfPixel1, cull0 and scissor off.
That snapshot selects the **>640/nonzero full-UV branch**, Begin at `8282E624`.
The C++ regression pins its complete 64-byte native rectangle. The first decoded
luma plane is reported uniformly16 and both chroma planes128. These are
user-supplied probe observations; this task did not launch the probe or observe
a rendered movie picture, and draws no color-conversion conclusion from them.

## Validation and verification

`expandMovieRectangle(span<const MovieVertex>)` validates exactly three records,
the original full-rectangle XY bit patterns, matching UV edge bits, and one of
the three original UV profiles before returning an owned four-record array.
It rejects NaN/infinity, signed zero in place of literal +0, perturbed endpoints,
wrong order, fractional/degenerate positions and combined U/V cropping absent
from the original branches. This is intentionally a bounded movie operation,
not a generic rectangle utility. Success and rejection leave input untouched.

`tests/test_movie_geometry.cpp` is a standalone standard-library C++20 test with
an explicit read-only original-image argument. It pins every instruction in
`8282E548..8282E6C8` (96 words), all seven constants, declaration records and
the half-pixel request. Its bounded PPC interpreter follows original branch
instructions, reads literal words, and records each original `stfs` assignment;
expected vertices are not taken from the native builder or a duplicate UV table.
The mocked SDK Begin verifies 8/3/16 and invalidates volatile registers before
returning a synthetic writable span. No SDK rendering is emulated.

The tests compare both all 48 big-endian original payload bytes and all 48 host
layout bytes, preserve the first three vertices, check the fourth against the
reference arithmetic, exercise both triangles and affine UV interpolation, and
pin the observed boot150 rectangle. They cover nine signed widths including
INT32_MIN/INT32_MAX and 639/640/641 with every possible presenter byte (2,304
traces), all single-bit mutations of every input word in each distinct profile,
nonfinite values, invalid counts, caller ownership and all four rounding modes.

**No C++ test was compiled or executed, and no build or game launch was run, as
requested.** Executed verification was the standard-library Python evidence
audit, which independently traces the original image and checks the source
pins/literal initializers; it does not execute the native C++ implementation:

```powershell
python -B build/movie-geometry/verify_evidence.py
```

It passed 96 code pins, seven literals, all four original184 payload/store
comparisons, 2,304 signed-width/byte traces, exact reference float32 completion,
and 252 exact-rational interior samples across both triangles. It also checked
that all read-only input hashes remained unchanged during the audit. The
result is [movie-geometry-evidence.json](../build/movie-geometry/movie-geometry-evidence.json).
The C++ test needs only its own source plus `renderer/movie_geometry.cpp` and
the workspace include root when the integration owner next authorizes a build.

Changed files: `renderer/movie_geometry.h`, `renderer/movie_geometry.cpp`,
`tests/test_movie_geometry.cpp`, this document,
`build/movie-geometry/verify_evidence.py`, and
`build/movie-geometry/movie-geometry-evidence.json`.
