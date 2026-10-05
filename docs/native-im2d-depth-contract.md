# Original Im2D depth contract

**The Im2D screen override does not request a forward depth viewport.** It
only selects the screen-position shader branch and its width/height constants.
With the original camera's retained depth endpoints **1,0**, the original
fragment depth is **1-Z**. The supplied actual154 history supports that reversed
mapping: outer-camera selection follows the changed-target reset, then subsequent
mode-zero resets report that the viewport is preserved. An unconditional native
0,1 viewport with unchanged Z loses the original depth value.

**Actual154 requires real depth writes.** Its committed SDK state is enable1,
write1, compare7 (ALWAYS), cull0, stencil0, alpha-test0, viewport-enable1. This
is not the earlier inactive-write case. Plain D32 storage also differs from the
original 20e4 format: actual input Z `3EFF7CEE` and reversed Z `3F004189` are
not representable in 20e4. For this uniform-depth draw, both reviewed rounding
candidates produce **20e4 `E00831`, decoded float32 `3F004188`**.

This sidecar changes only this document and `build/im2d-depth/*`. Main owns all
runtime, renderer, test, config, build and live-run work. No shared tests, AOT
generation, build, game launch, debugger attachment, UI action or reference
mutation was performed here.

## Reproducible evidence

Run `python -B build/im2d-depth/verify.py` from the workspace root.
The [verifier](../build/im2d-depth/verify.py) checks the immutable original-derived
image SHA, all **2,202 instruction words in 23 spans**, **78 independent literal
instruction pins**, six data pins, four original SDK table rows and four exact
shipped shader strings. It records reference/native source hashes and the
completed actual154 log's pertinent lines. The current result is
[verification.json](../build/im2d-depth/verification.json); the detailed bytes,
source excerpts, depth arithmetic and log extract are in
[evidence.json](../build/im2d-depth/evidence.json), with readable original
instructions in [original.txt](../build/im2d-depth/original.txt).

Image `analysis/simpsons.pe`: 15,466,496 bytes, base `82000000`, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Function extents distinguish `.pdata` from individually reviewed leaves/tails.
These checks validate the evidence's identity; they are not execution tests of
original hardware or proof of general rendering equivalence. Native source
hashes identify the audit snapshot and may change while main implements fixes.

## SDK depth state and the RenderWare distinction

The original table `82CD28B8` maps scalar IDs (hexadecimal) as follows:

| Scalar | Setter | Meaning | SDK default |
| --- | --- | --- | --- |
| `28` | `8243A6C8` | Requested depth enable | 1 |
| `2C` | `8243A738` | Depth comparison | 3 |
| `30` | `8243A708` | Retained depth-write enable | 1 |
| `38` | `82439F00` | Low three culling bits | 6 |

These SDK defaults do not identify a later draw's state. In particular the
engine startup changes comparison to6 and culling to2; actual154 instead commits
comparison7 and culling0.

`8243A6CC=90832E5C` retains the requested enable at device+2E5C. The setter
reads depth attachment+30A0; when null, `8243A6D8=38800000` makes the effective
enable zero. `8243A6E0=508B0FBC` inserts only bit1 of device+2934. It does not
clear write bit2 or stencil bit0. The write setter's
`8243A70C=508B177A` inserts bit2; comparison setter
`8243A73C=508B2676` inserts bits4..6. Depth binding `8243D598` updates+30A0,
then `8243D7E0..D808` reapplies the retained enable with the same attachment
condition. The native state must retain these independent requested values.

For canonical state, effective depth testing is `bound && enable`; effective
depth writing is `bound && enable && write` after successful fragment tests.
The local reference's `RB_DEPTHCONTROL` definition and D3D12 pipeline construction
corroborate that write is inactive when enable is zero. D3D11 also disables both
testing and writes when `DepthEnable=FALSE`.
[Microsoft depth/stencil behavior](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-depth-stencil).

| Original comparison | D3D11 comparison enum | Predicate |
| --- | --- | --- |
| 0 | 1 NEVER | false |
| 1 | 2 LESS | incoming < stored |
| 2 | 3 EQUAL | incoming == stored |
| 3 | 4 LESS_EQUAL | incoming <= stored |
| 4 | 5 GREATER | incoming > stored |
| 5 | 6 NOT_EQUAL | incoming != stored |
| 6 | 7 GREATER_EQUAL | incoming >= stored |
| 7 | 8 ALWAYS | true |

The original values follow the pinned three-bit SDK field and local reference
`xenos.h:677`; D3D11 values are raw+1, not a direct enum cast.
[Microsoft comparison enum](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_comparison_func).

Do not equate **RenderWare Z-test off** with **SDK depth enable off**. The
original RenderWare write helper `82401E00` stages SDK scalar28 as well as30
when the cached RenderWare test Boolean is zero. Thus a write-only RW request
can legitimately require enabled SDK depth with comparison ALWAYS. Main traced
actual154's caller requests independently; the captured effective values prove
the native draw needs that behavior without requiring this sidecar to guess
the caller's state.

## Position, VTE and viewport depth

Original `82409308` uploads the vertices, calls setup `82408CC0`, calls
`8240F210` at `82409514`, draws through `823F4B60` at `82409538`, then calls
`8240F230` at `82409540`. Setup clears the explicit shader bindings, arranges
fixed-function stages and commits pending states. It does not unconditionally
turn depth off or set depth comparison.

`8240F210` consists of seven instructions ending in BLR. Its only stores are
screen flag1 at `82D503D4`, width at D8, height at DC. The four-instruction
`8240F230` clears only D4. **Neither issues a viewport call or writes depth
endpoints.** Original `82410A68` reads those fields into its key, selects the
shipped source at `82063228`, and `8240FE00` uploads the screen XY scales using
the dimensions. The screen branch's pinned expressions are:

```text
X = (inputX - 0.5) * scaleX - 1
Y = -((inputY - 0.5) * scaleY - 1)
Z = inputZ
W = 1
```

The original source has 21,677 bytes, SHA256
`9fbcd5e2391752e25a6e7ddf22704e0f271fb562747b157b1cc6f5421f55b78b`.
RHW is ignored, including actual154's RHW `41200000` (10). No reciprocal or
division of Z by that input is justified. Root-raster upload copies bytes;
the offset-raster branch adjusts only XY and preserves the Z/RHW fields.

Original SDK scalar130 calls `8243B260`. Nonzero requests write **0000043F**
at device+294C (`8243B264=3960043F`, `B270=9163294C`). The pinned local
`PA_CL_VTE_CNTL` definition identifies bits0..5 as viewport XY/Z scale and
offset enables. Thus Zscale and Zoffset are enabled. Bits8/9 are zero and bit10
is one; with the qualified shader's W=1 there is no alternative input-RHW depth
interpretation. Scalar130 also controls the corresponding clip-disable bit;
the normal enabled branch leaves clipping enabled. Actual154 records scalar130=1
at log line11977.

Camera selection `823EE6C8` writes endpoint1 at SP+60 and endpoint0 at SP+64,
then calls `8243D0F8` at `823EE7E0`. The SDK wrapper converts the first four
integer rectangle words to floats and passes the two depth floats through.
Within `8243CE80`:

```text
8243D060 ED9AF828  fsubs f12,f26,f31   # end - start
8243D064 D3FF291C  stfs f31,291C(r31) # start / Z offset
8243D068 D19F2918  stfs f12,2918(r31) # Z scale
```

Consequently start1/end0 means scale−1, offset1, hence **1−Z**. XY scales are
positive width/2 and negative height/2 (`D074=D1BF2908`, `D088=D1BF2910`).

There is a real **separate forward reset**: changed color-target binding can
call `8243D198`, which loads default viewport data at `82069FA4` and tail-calls
`8243D0F8`. The pinned data are `(0,0,65535,65535,0.0,1.0)`; dimensions are
then bounded to the target. Cache-hit target binding preserves the viewport.
See [the existing reset contract](native-viewport-reset.md). Camera reselection
after that reset requests reversed1,0 again. Im2D's screen flag does not undo it.

Actual154's completed log, immediately before the rejected packet, shows this
sequence: private camera; changed target reset to default0,1; outer camera
E2CA7010 selected; viewport-preserving reset; successful Im2D567; another
viewport-preserving reset; depth-only clear; rejected next Im2D packet. This is
evidence for **reversed depth at the draw**, not proof of forward depth. The
old backend's depth-off0,1 convenience must not be treated as original authority.
The log does not directly sample original SDK device registers, which the native
port intentionally does not execute; the conclusion combines the recorded
engine transitions with their independently pinned original contracts.

For this uniform-Z, W=1, zero-bias, depth-clipped packet, preserve original clipZ
and use ordered native viewport0,1, then compute1−Z in the depth-export shader
under an explicit reversed-depth flag. The original0..1 clipping interval is
unchanged by exchanging its two endpoints. Keep XY, W, culling and pixel centers
unchanged. Do not sort original endpoint state, overwrite guest input bytes or
also reverse vertexZ. General varying-Z interpolation and rounding need separate
verification; do not prequantize arbitrary vertices in place of per-fragment
depth quantization.

## Original depth representation and numerical result

`1A220197 & 3F = 17` hex, surface index23 decimal. Surface construction
`8243FC4C` extracts that index; `FC6C/FC74` recognize22/23 as depth. The format
table load at `FDCC` uses `8206A028 + 2*index`, giving `8206A056=1120`.
`FDD0` extracts its format nibble; the depth branch places its low bit at
header+1C bit16 (`FDD8`, `FDE8`). That bit is1, which the pinned local reference
names **D24FS8**, floating20e4 plus stencil8; selector0 would be D24S8 UNORM.
The SDK number-format bit is not permission to reinterpret this as UINT depth.

Let e be the high four bits of the24-bit code and m the low twenty. Decode is
`m*2^-34` for e=0, otherwise `(1+m/2^20)*2^(e-15)`. Every decoded value is
exactly representable in float32. This makes D32FS8 usable as **backing for
quantized decoded values**, not permission to retain its excess precision.

| Value | Float32 bits | 20e4 truncation | 20e4 nearest-even |
| --- | --- | --- | --- |
| Original input Z, 0.49900001287460327 | `3EFF7CEE` | decoded `3EFF7CE8` | decoded `3EFF7CF0` |
| Reversed1−Z, 0.5009999871253967 | `3F004189` | code `E00831`, decoded `3F004188` | code `E00831`, decoded `3F004188` |

The common decoded stored result is **0.500999927520752**. The original and
reversed floats both have nonzero low three mantissa bits and are not already
20e4-normal values. A raw D32 write creates an observable difference for later
depth comparisons, copies and sampling, even though this packet's ALWAYS test
produces the same immediate color.

The local reference's `xenos.cpp:124..176` supplies conversion/decode formulas,
attributed there to the Direct3D9 reference implementation and Microsoft format
conversion code. Its accuracy-oriented ROV path explicitly enables nearest-even
conversion (`d3d12/render_target_cache.cpp:874..876`). Its host-DSV path also has
a truncating optimization. These are implementation research and strong format
authority; neither the configuration default nor this byte audit independently
proves physical-console raster rounding. The Xenia authors explicitly discuss
that distinction and excess-precision problems.
[Xenia's original depth-conversion analysis](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html).
For the actual reversed constant above both candidates agree, so that uncertainty
does not justify retaining the wrong D32 value or leaving this packet unported.

## Bounded native implementation and validation needs

1. Main's bridge must preserve the original logical viewport and derive its
   explicit reversed-depth flag from the current endpoint state. The generic
   backend should consume its supplied positions/viewport/flag, not guess whether
   a caller is reversed.
   Retain effective enable/write/comparison individually and keep actual attachment
   ownership/dimension validation. Inactive writes must remain valid when depth is
   disabled; enabled writes with comparison ALWAYS must actually update depth.
2. Use a native pixel shader depth export (`SV_Depth`) to quantize **after the
   original viewport mapping/interpolation**, then decode20e4 back to float32 for
   D32FS8 storage. For the current normal range, rebias float exponent by−112,
   add `3 + ((bits >> 3) & 1)` for nearest-even, shift right3 to encode; reverse
   exponent rebias/mantissa shift to decode. Zero, the denormal grid2^-34,
   normalized boundary2^-14, exponent carry and saturation need explicit handling
   before widening the profile. The reference code is pinned in the evidence.
3. With the current finite0..1 input, W=1, zero bias and enabled depth clipping,
   clip geometry at the original clip planes and saturate the interpolated mapped
   depth to the qualified interval before conversion. A shader `SV_Depth` value
   is already viewport-space depth; do not apply the viewport transform twice.
   This profile requires no half-range storage remap. Supporting the full20e4
   range up to2−2^-20 later would require its own storage/readback contract.
4. Run the original alpha decision before publishing color/depth. A discarded or
   culled fragment changes neither; a zero color write mask still permits depth
   writes. Avoid forced early-depth attributes with shader depth export/discard.
   Preserve stencil with stencil disabled. Depth testing/writing must accompany
   every triangle in the existing ordered packed-color blend loop, so subsequent
   triangles test the updated real depth attachment.
   [Microsoft explicit early-depth attribute](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sm5-attributes-earlydepthstencil),
   [HLSL depth semantics](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-semantics).
5. Meaningful GPU pixel/readback fixtures: actual four-vertex packet through the
   real bridge; stored depth exactly `3F004188` and stencil unchanged; separate
   all-eight compare truth tables with incoming lower/equal/higher depth; writes
   disabled vs enabled; disabled depth with retained write1; alpha rejection;
   both cull orientations and both strip parities; RGBA mask0 still writing depth;
   overlapping strip triangles with different depths; and draw→depthCopy→draw
   equality to detect retained excess precision. Include near-quantization-boundary
   values that distinguish unquantizedD32, truncation and nearest-even. Assertions
   must inspect depth bytes and affected color pixels, not only draw counts or
   state descriptors. Rejections must precede context/resource/count mutation.

### Integration ABI relayed by the backend owner

The backend owner subsequently reports `Im2DDraw::reverseDepth` with a default
of false. Its32-byte draw constants are: offset0 uint depthReversed, offset4
uint reserved0, offset8 float alphaReference, offset12 uint alphaTest, offset16
uint alphaCompare, offset20 uint blendWord, offsets24/28 uint padding. Main owns
the matching HLSL. With native viewport0,1 the depth output computes
`reverseDepth ? 1-input.position.z : input.position.z`, then quantizes to20e4.
It must not also compensate vertexZ. This paragraph records the communicated
ABI, not a build/test result or verification of the final edited implementation.

## Cull0/2/6

`82439F04=51640038` preserves all device+2948 bits except0..2 and inserts the
SDK value unchanged. The reference defines bit0=cull-front, bit1=cull-back,
bit2=front-face orientation (0CCW,1CW). With the original negative viewport-Y
scale and the native XY transform preserving winding, the existing mapping is
qualified:0→NONE;2→BACK with FrontCounterClockwise=TRUE;6→BACK with
FrontCounterClockwise=FALSE. Preserve triangle-strip odd/even ordering when
submitting individual triangles. Actual154 requests0, so no winding rejection
is requested there. Other low-bit combinations remain outside this bounded
implementation, even though the scalar owner can retain values0..7.

This report does not claim a completed menu, gameplay, general depth-rendering
parity, hardware rounding proof or a successful post-fix actual run. Main's
implementation and actual155-or-later validation remain required.
