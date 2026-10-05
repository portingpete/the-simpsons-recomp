# Native Im2D single-level BC2/BC3 sampling

Build191 extends the same owned, single-level path to BC3. See
[native-itxd-bc3.md](native-itxd-bc3.md) for the exact original resource,
independent block fixture, native alpha-mode tests and original lifecycle tests.
The following BC2 evidence and historical oracle corrections remain applicable.

The Im2D texture gate admits `TextureFormat::BC2` and `TextureFormat::BC3` alongside RGBA8 and BGRX8,
with exactly one level. The change reuses the existing immutable
`DXGI_FORMAT_BC2_UNORM` upload, checked resource/view ownership and native GPU
sampling in [native_backend.cpp](/K:/SimpsonsNativeCopy/renderer/native_backend.cpp).
The original BC2 change did not alter shader, packet, runtime parser, guest
binding or lifetime code. The BC3 extension adds the separately qualified ITXD
format while retaining the same original loading and association contract.

`drawIm2D` still validates the actual selected color/depth attachments, source
device and backing/view metadata, complete common draw state and the existing
point/linear, wrap/clamp sampler profile before rendering. BC1 and all mip
chains remain excluded from Im2D. Compressed dimensions must be divisible by four
under the existing backend storage contract. A BC2 upload is tightly packed
linear block rows, with 16 bytes per 4x4 block; guest tiling, pitch, endian lanes,
channel selection and authored mip interpretation must already be qualified by
the resource owner.

The unchanged textured shader evaluates the recovered `texture2D * diffuse`
expression, then alpha comparison, the selected blend equation, and explicit
RGB10A2 packing. BC2 admission does not premultiply, unpremultiply, discard
transparent RGB, expand to a CPU RGBA image or infer a different shader branch.
Depth follows the existing reverse mapping and 20e4 conversion contract.

Microsoft documents BC2's explicit four-bit alpha per texel and four-color RGB
mode regardless of endpoint order. The fixture bytes follow that layout.
[Explicit-alpha format](https://learn.microsoft.com/en-us/windows/uwp/graphics-concepts/textures-with-alpha-channels).
Native block-compressed textures are sampled through a shader resource view.
[Direct3D block compression](https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression).

## Added GPU fixtures

[test_im2d_backend.cpp](/K:/SimpsonsNativeCopy/tests/test_im2d_backend.cpp) uploads
literal DXT3 bytes and observes real GPU color, depth and compressed-source
readbacks. Expected texels are authored separately from the bytes; no production
compression decoder supplies the pixel oracle. Endpoint RGB, packed alpha/color,
depth/stencil, storage and debug-layer checks remain exact. Only point-sampled
derived RGB uses the specification interval described below. Filtered-alpha
comparisons use separated references; exact ties use point samples.

- A 12x8 image contains six distinct blocks in three columns and two rows. Its
  first block has ascending black/white endpoints, all sixteen alpha nibbles and
  four different selector rows. The remaining blocks are transparent red,
  translucent green/blue, opaque yellow and translucent magenta. This exercises
  block pitch, row orientation, selector order, RGB channel order and explicit
  alpha. Every target pixel, including uncovered margins, is checked.
- White and nontrivial RGBA diffuse values exercise component multiplication.
  Literal packed pins include transparent red `000003FF`, black `00000000`,
  white with low alpha `3FFFFFFF`, green `400FFC00` and blue `BFF00000`.
  Replace blending retains RGB at alpha zero. The two derived grays must fall
  within distinct per-channel intervals; an endpoint, wrong selector or BC1
  three-color result cannot satisfy the corresponding interval.
- The same image checks sampled-alpha rejection with reversed depth. Accepted
  pixels write exactly 0.75, rejected/uncovered pixels retain 0.5, and stencil
  remains unchanged. With depth disabled, the entire depth surface survives.
- An additional all-white palette with all sixteen explicit alpha nibbles
  exercises all four RGB selectors. SRC_ALPHA blending maps each nibble to a
  distinct 10-bit RGB result, checked exactly, including nonseparate alpha
  squaring. Alpha gating and depth/stencil are checked again. This strengthens
  alpha observation beyond the two-bit stored-alpha channel.
- A separate 4x4 block has red/alpha-zero and blue/alpha-one quadrants. Point
  samples, exact texel centers and binary-coordinate bilinear samples have
  explicit packed expectations for ideal weights 1/4, 3/8, 1/2 and 3/4.
  Alpha testing uses references 1/8 and 7/8: every mixed sample must pass the
  lower reference and fail the higher, updating/preserving depth accordingly.
  Both horizontal and vertical filtering matter. Separate point-sampled
  alpha1 multiplied by diffuse alpha1/2 supplies an exact tie for all eight
  comparison functions, with exact packed color and depth/stencil checks.
- Mutable caller upload bytes are overwritten after texture creation. Readback
  must retain the original compressed blocks before and after drawing. A final
  draw releases the caller's texture and vertices before output readback.
- Rejected BC1 draws, foreign BC2/BC3 ownership, spoofed format metadata and
  unsupported BC2 sampler requests use the existing no-mutation checks for
  color/depth, attachments, viewport, counters and native pipeline state.
  Compressed source bytes must also survive rejection. BC2 mip-chain creation
  is rejected; the existing RGBA mip-chain draw rejection still checks the
  Im2D one-level gate.

## BC2 interpolant oracle correction

Main's first build187 executions stopped at texel (2,0). WARP returned
`15154551` (RGB codes 337/337/337); hardware returned `15550555`
(341/321/341). The first oracle required `15555555` (341/341/341), incorrectly
treating the derived black/white palette entry as universally bit-exact.
Logs: `build/im2d-upload/focused187-bc2-tests.log` and
`build/im2d-upload/im2d-bc2187-hardware-first.log`.

The primary [D3D11.3 Functional Specification, section19.5.2, Error Tolerance,
and section19.5.7, BC2](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)
allows component decode error strictly below `1/255 + 0.03 * endpointRange` for
this format, using the larger pre/post-promotion endpoint span. Reference
values zero and one must remain exact. Section19.5.7 defines explicit alpha;
section7.18.16.3 guarantees point sampling selects one texel exactly.

For this fixture, both endpoint spans are one. The two reference grays are
`k/3` (k=1 or2), with decode error below `173/5100`. The oracle propagates that
open interval through positive diffuse `t/8` and the existing explicit
round-to-nearest-even UINT packing. It does not choose either observed device
as the reference. The resulting inclusive code bounds are:

| Diffuse | Reference1/3 | Reference2/3 |
| --- | --- | --- |
| 1 | 306..376 | 647..717 |
| 5/8 | 191..235 | 405..448 |
| 3/8 | 115..141 | 243..269 |
| 1/8 | 38..47 | 81..90 |

These are calculated with integer rational arithmetic and pinned at compile
time. For these fixed combinations, the interval endpoints are separated from
rounding thresholds sufficiently that float32 multiplication cannot change the
outer accepted bins. Only derived RGB texels in the first block use them;
each channel is checked independently. Packed alpha, discard decisions,
endpoints, margins and depth remain exact. The binary-coordinate bilinear
fixtures retain exact packed expectations for the selected native test profile;
that does not establish equality of their internal float values to ideal values.

## Filtered-alpha comparison correction

Main reports WARP passes, including the white alpha ramp. The subsequent hardware
run reached `im2d-bc2187-hardware-final.log` check24437 and returned `A0000200`
where the test expected a discarded fragment. This matches the nominal half
blend's packed color, but does not establish that the sampled float alpha was
exactly0.5. The log does not identify its precise value or the rounding stage.

[D3D11.3 sections7.18.16.1-.3 and19.5.7](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)
distinguish coordinate precision from filter arithmetic. BC2 filtering requires
at least UNORM8 precision; representable weights do not promise an exact
float32 half result. Point sampling and constant-input filtering have stronger
invariants. Accordingly, GREATER against the ideal mixed alpha is not a portable
tie oracle.

The revised filtered cases retain the exact packed-color oracle and the disabled
alpha case. References0.125/0.875 put every ideal mixed sample at least0.125 from
either threshold, far beyond the UNORM8 step1/255. Each mixed sample must both
pass and fail in separate draws; rejected pixels must preserve depth. These
references are fixture choices, not inferred hardware values or changes to the
production shader. Exact equality behavior is separately checked on point alpha
1 multiplied by diffuse0.5, for all eight comparisons. Existing exact flat,
RGBA and white-BC2 alpha tests remain in place. Arbitrary fractional-filter
float or packed-output parity across all devices is not established.

## Original-console evidence limits

The intervals specify bounded native behavior; they do not establish original
console palette reconstruction, filtering, coverage, blend or display parity.
The BC2 format gate remains general, so a passing bounded test must not be
reported as universal compressed-color faithfulness. The new all-white palette
fixture is a stronger exact case relevant to white glyph storage, but does not
identify or qualify any original texture by itself.

[The existing decoder investigation](/K:/SimpsonsNativeCopy/docs/texture-decode.md)
documents original BC2 layout and a deterministic reference-backed CPU RGB8
decoder. Its truncating interpolation policy is not used as a general GPU
oracle here. Original DXT2_3 descriptors do not alone establish alpha association;
the existing recovered shader/blend expression determines use of the sampled
components. This patch does not prove the identity, dimensions, layout or
ownership of the texture reported at actual156, or establish correct game text.

## Main validation handoff

Main built the preceding revisions and supplied the results above. This worker
corrected only `tests/test_im2d_backend.cpp` and this report in the filtered-alpha
revision; it ran no build, AOT generation, GPU tests, game or UI actions. The
revised fixtures still require compilation and both executions by Main.
From `K:\SimpsonsNativeCopy`, after coordinating shared build ownership:

```powershell
.\build\original-screen\build-focused.ps1 -Targets Im2DBackendTests
ctest --test-dir build/native -R '^OriginalIm2DNativeDraw$' --output-on-failure
.\build\native\Im2DBackendTests.exe --hardware
```

The same executable includes the existing depth/color regression matrix and
the new BC2 fixtures. Main should retain both execution logs and investigate
any exact-oracle mismatch before integrating the runtime texture owner. This
renderer/test-only patch introduces no AOT input change; Main's separate runtime
changes may require regeneration.
