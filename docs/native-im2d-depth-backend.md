# Native Im2D depth backend

The Im2D backend now supports native depth comparison and writes against the
actual selected depth attachment. Main's accompanying pixel shader maps the
qualified original depth range and converts to decoded 20e4 before comparison
and writing. Native D32 storage holds that value exactly. This removes the
known excess-precision error for the reached constant; passing backend tests
still does not prove general physical-console interpolation or rounding parity.

## Packet and implementation

`Im2DDraw::depthCompare` retains original scalar values 0..7:
NEVER, LESS, EQUAL, LEQUAL, GREATER, NOTEQUAL, GEQUAL, ALWAYS. The backend maps
these explicitly to the corresponding D3D11 comparison functions. It caches all
32 combinations of depth enable, write enable, and comparison. Disabled depth
retains the requested comparison and write mask in the packet; both are inactive.
The Windows runtime may canonicalize those inactive descriptor fields: the
build186 WARP probe returned ALL/LESS for a disabled ZERO/NEVER request. The
tests therefore check active descriptor fields exactly and verify disabled
write/compare behavior from readbacks across every request combination.
In particular `depthTest=false, depthWrite=true` never writes.
Stencil remains rejected.

Enabled depth requires a real owned, single-sample D32_FLOAT_S8X24_UINT view of
R32G8X24_TYPELESS storage, matching the color target dimensions and actual OM
selection. The existing depth-resource validator checks device, resource/view
identity, formats, subresources, and writable DSV flags. An absent depth
attachment is supported only with depth disabled. Even then, a supplied depth
owner must match the selected attachment and pass validation.

The backend requires actual native viewport depth endpoints 0 and 1 when depth
is enabled. The existing vertex shader passes supplied Z unchanged with W=1.
`reverseDepth` selects `1-Z` in the pixel shader after native forward-range
interpolation; false selects Z. Main's pixel shader then converts to 20e4 using
the audited reference round-to-nearest-even function and emits the decoded value
as `SV_Depth`. The native depth state compares that output, not the unquantized
interpolated value. Disabled depth retains the previous support for other valid
native viewport ranges; its shader depth output is inactive.

The 32-byte draw constant buffer now has uint reverseDepth at byte 0, uint zero
reserved at byte 4, float alphaReference at byte 8, uint alphaTest at byte 12,
uint alphaCompare at byte 16, uint blendWord at byte 20, and two uint zero padding
words. Compile-time offsets check the shader ABI. The backend never infers
reversal from native viewport endpoints or old native bindings.

The packed color pipeline still splits strips into triangles with the original
winding parity. Each triangle sees the preceding packed color and the same live
depth attachment. There is no depth snapshot/copy or CPU comparison. The native
depth state gates the existing integer color output. Shader alpha discard and
raster culling prevent depth writes as well as color output. A zero color write
mask does not disable depth writes. Stencil remains disabled and unchanged.

Validation and all fallible allocation happen before context or target mutation.
Selected target identities, viewport restoration, texture/sampler restoration,
temporary shader/buffer cleanup, and one-count-per-submitted-packet behavior are
preserved. The selected depth/blend/raster states remain bound as before; this
service does not introduce a new state-restoration contract.

## Original integration requirements

The original-depth audit reports that the Im2D override changes the override
flag and dimensions, leaving the camera's depth scale/offset active. For actual
154, the camera endpoints 1 then 0 and VTE43F imply `1-Z`. Main owns the proof and
sets the packet flag from the retained logical camera range. The backend must not infer that transform from a native
viewport or substitute forward endpoints for original state.

The original surface is D24FS8 (20e4), as documented in
`build/im2d-depth/evidence.json`. Raw native D32 storage would retain extra
precision observable by later comparisons and depth sampling. Every
representable 20e4 value fits in D32; the shader conversion therefore uses the
existing native storage without that extra precision. The reference audit pins
Float32To20e4/Float20e4To32 and the reference accuracy path's nearest-even choice.

For the reached constant vertex Z `3EFF7CEE`, the audit reports reversed float Z
`3F004189`; both examined rounding candidates, truncation and nearest-even,
produce decoded 20e4 value `3F004188` (code `E00831`). Storing either raw
`3EFF7CEE` or raw `3F004189` is not faithful evidence for that original draw.
The original hardware rounding rule for general inputs remains a proof
requirement. Native nearest-even conversion matches the audited reference
arithmetic; it is not physical-console rounding evidence. Nor does fragment
`1-Z` after native interpolation prove bitwise equivalence to original hardware
viewport mapping before interpolation for varying-Z geometry. The reached
uniform-Z draw does not require that varying-Z claim.

Main owns `renderer/im2d_draw.hlsl`; this scoped backend patch changes no shader.
Its tests depend on that accompanying `SV_Depth` implementation.

## GPU fixtures and assumptions

`tests/test_im2d_backend.cpp` runs its existing fixtures plus:

- All 32 depth state combinations in both forward and reversed ranges against
  less/equal/greater depths, endpoints, and floats adjacent to 0.5, checking exact
  color and decoded 20e4 depth bits and retained native state descriptors.
- The exact supplied `3EFF7CEE` constant, including a slightly overscanned black
  quad. Forward output must be `3EFF7CF0`; reached reversed output must be
  `3F004188`. Original camera selection is independently owned by Main's bridge.
- Explicit nearest-even ties with even and odd mantissas, the smallest 20e4
  subnormal, values rounding to zero, the subnormal/normal boundary, and values
  rounding to one. An independent arithmetic decoder checks pinned bit results.
- Alpha comparisons on flat and textured branches, sampled-alpha discard,
  both winding parities and cull modes, all color write masks including zero,
  subviewport coverage, and unchanged stencil bytes.
- Ordered overlapping strip triangles with less/greater comparisons, writes
  on/off, and equal-depth ties, checking each triangle's effect on depth and
  explicit packed blending. Degenerate connectors preserve strip ordering.
- Missing, unselected, foreign-device, wrong-size, and absent selected depth
  rejection; invalid comparison and enabled non-0..1 viewport rejection.
  Rejections preserve color/depth bytes, counters, attachments, viewport,
  predication, and native depth/blend/raster state identities.
- Depth-off packets with retained write/compare and no depth attachment.

The depth oracle uses exact decoded 20e4 float bits, not a tolerance. Constant-Z
fixtures assume the native rasterizer preserves constant float Z (native IEEE
subnormals that flush to zero already round to zero in 20e4). The clear values are
also exactly representable by the existing 20e4-qualified clear service. Stencil
is byte 4 of each 8-byte readback element; the final three padding bytes are not
interpreted as depth/stencil. Existing whole-storage rejection comparisons are
retained for unchanged resources.

Both WARP and hardware use the same executable and fixtures. The test enables
the optional D3D debug layer before creating any fixture when available and
rejects error/corruption messages from successful submissions. Console subpixel,
interpolation, depth conversion and coverage parity are not proved by these
native tests. No existing color tolerance was relaxed. The generic raw-D32
oracle was replaced once the original format was proved; no raw-D32 write is
accepted as the final depth behavior.

## Validation

Main compiled the shared shader/backend and ran the suite on WARP and hardware.
Both pass337,420 checks with the D3D11 debug layer enabled. The original-driver
suite passes122,091 checks, including67 in the original Im2D fixture. The
focused three-suite run, including original movie integration, passes in7.57s.
Logs: `build/im2d-upload/focused186-depth-final-detail.log` and
`build/im2d-upload/im2d-depth186-hardware.log`. See the integration report for
the actual154 geometry boundary and subsequent executable evidence. The native
GetDesc check validates active fields exactly; inactive canonicalization is
documented above. No shader arithmetic, stored-depth expectation, or existing
color tolerance was weakened to make these tests pass.
