# Qualified movie pixel shader

[movie_shader.hlsl](../renderer/movie_shader.hlsl) is ready for native integration.
`PSMovie(float2 uv : TEXCOORD0) : SV_Target0` returns **float4**. Bind/cache this
entry with the existing `VSTextured`. `PSMoviePacked` returns **uint4** and applies
the requested native target helper, `round(saturate(color) * (1023,1023,1023,3))`.
That packing is separate from the original shader. Both entries compile as
`ps_5_0`; neither requires a constant buffer. No backend, runtime, config, CMake,
shared build, game, or reference files were changed.

| Native bindings | Original texture stage | Frame plane | Required view |
| --- | --- | --- | --- |
| t0 / s0 | 0, reflection `gTexture_Y` | 0, presenter+8 | R8_UNORM, sampled X |
| t1 / s1 | 1, reflection `gTexture_Cr` | 2, presenter+10 | R8_UNORM, sampled X |
| t2 / s2 | 2, reflection `gTexture_Cb` | 1, presenter+C | R8_UNORM, sampled X |

All three fetches use the same normalized `uv.xy`, computed LOD, no instruction
bias or texel offset, and inherited per-stage filters. Do not normalize the
sample again, replicate channels, rescale chroma UVs, or insert gamma conversion.
The caller supplies the effective sampler state and owns all three resources.

The user-reported boot150 probe has frame `FF614918`, presenter `E1ADC480`,
width1280, uv_mode1, VS `E3E968F0`, PS `E3E9ABD0`; plane bytes are uniformly
16/128/128. Its three samplers have clampUVW2, min/mag1, mip2, bias0,
minLOD0/maxLOD13, anisotropy1. Reported packed blend1 is replacement, expanded0,
write maskF. This is an integration fixture supplied by the user, not a live
memory observation made by this inspector. Uniform planes make its shader
color independent of UV and the reported clamped linear filtering.

Under the stated RN binary32 numeric policy, that fixture produces:

```text
RGBA float = (0.0034149044658988714, -0.0020768591202795506,
              0.004240809008479118, +0)
RGBA bits  = 3B5FCC97 BB081BEA 3B8AF67C 00000000
packed helper uint4 = (3, 0, 4, 0)
```

It is **not exact black**: the shader subtracts exactly `1/16` from normalized
Y and `1/2` from normalized chroma. Those differ from `16/255` and `128/255`.
The actual shader's output is unclamped and its alpha is **zero**, not one.
For exact sampled inputs `(0.0625,0.5,0.5)`, all output components are zero.

## Immutable record and complete instruction contract

Authority is `analysis/simpsons.pe`, base `82000000`, 15,466,496 bytes,
SHA256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Only PS record `82152B68`, 540 bytes, SHA256
`48d052096755186f10dc040a2e0718544f2dea5692f59b6e2ca5a9435351deee`
is accepted. Names are corroborated by reflection, not used to choose color
coefficients. Addresses and encoded words below are hex; register indices and
sizes in prose are decimal.

Record header+4/+8 gives payload offset164/sizeB8, **not** an instruction-only
window. Header+18 points to the program header at `82152CA8`; its first two
words are `40,78`. Thus the first64 payload bytes are literals and the actual
120-byte program is `[82152D0C,82152D84)`. The program header, metadata,
reflection, full record, full image, and all instruction fields are checked.

The four 48-bit control-flow instructions are EXEC(address2,count3,sequence15),
ALLOC(type2,size0), EXEC_END(address5,count4,sequence0), NOP. They execute exactly
slots2–8 once. All per-slot serialize bits are zero. Slot9 at `82152D78`, words
`4E4A0000 5F71F02E F5081E67`, is an unexecuted trailer.

| Slot/address | Exact 96-bit instruction | Decoded operation |
| --- | --- | --- |
| 2 / 82152D24 | `10081001 1F1FFFF8 00004000` | fetch stage0 X into r1.x; preserve YZW |
| 3 / 82152D30 | `10181001 1F1FFFC7 00004000` | fetch stage1 X into r1.y; preserve XZW |
| 4 / 82152D3C | `10281001 1F1FFE3F 00004000` | fetch stage2 X into r1.z; preserve XYW |
| 5 / 82152D48 | `C8070000 00C01A00 8001FD00` | ADD r0.xyz, r1.xyz, c253.zww |
| 6 / 82152D54 | `C801C000 00B0B06C 9100FDFF` | DP2ADD color0.x000, r0.xy, c253.xy, c255.x |
| 7 / 82152D60 | `C8028000 0065C000 9000FE00` | DP3 color0._y__, r0.yzx, c254.xyz |
| 8 / 82152D6C | `C8048000 00C41A6C 9100FEFF` | DP2ADD color0.__z_, r0.xz, c254.zw, c255.x |

ALU swizzles are component-relative. Constants use the full unsigned eight-bit
index; the high bits mean source modifiers only for temporary registers. The
ADD's unused encoded source3 index0 is **not a c0 read**. All four scalar
opcodes are50 (RETAIN_PREV), scalar masks0: no scalar result is written. There
is no predication, relative addressing, negate, absolute, ALU clamp, discard,
alpha test, depth export, or other executed instruction.

Alpha follows the export mask rule, not a movie convention. Slot6 has export1,
vector mask1, scalar mask0, scalar-destination-relative1. On an export that
last flag writes zero to the other three components (constant-zero maskE).
Slots7/8 replace G/B and preserve A. No undefined register W is consumed.
See pinned `ucode.h` lines1834–2064 for fields and export mask semantics, and
lines1469–1494 for DP3/DP2ADD. The [Microsoft DP2ADD definition](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dp2add---ps)
corroborates two products plus the replicated third-source scalar.

## Literal upload provenance; no dynamic constants

| Register | Original literal address | XYZW float32 bits |
| --- | --- | --- |
| c252 | 82152CCC | `00000000 00000000 00000000 00000000` (unread) |
| c253 | 82152CDC | `3F950A81 3FCC4A9D BD800000 BF000000` |
| c254 | 82152CEC | `BF501EAC BEC89507 3F950A81 40011A54` |
| c255 | 82152CFC | `00000000 00000000 00000000 00000000` (X read) |

Consequently, with `t=(Y,Cr,Cb)+c253.zww`, the arithmetic is:

```text
R = t.x*1.1643830537796021 + t.y*1.5960270166397095 + 0
G = (t.y*(-0.81296801567077637) + t.z*(-0.39176198840141296))
    + t.x*1.1643830537796021
B = t.x*1.1643830537796021 + t.z*2.0172319412231445 + 0
A = +0
```

Shader creation `82448178..82448288` copies the header to PS object+28 and
establishes its payload pointer at+18. Binding and immediate draw dirty-state
dispatch reach `8245FA00`. At `8245FB74/78`, r4=PS+28 and r5=[PS+18];
`8245FB80=4BFFFC91` calls literal uploader `8245F810`.

The uploader reads header+14=`118`, locates metadata `82152C80`, then advances
14 to its first group at `82152C94`. The pair `01FC0010,00000000` means
destination01FC, count10 DWORDs, payload byte offset0. `8245F88C=5748103A`
shifts the destination by2. Stores `8245F898/A0/A4/A8` emit:

```text
C0022F00  GPU_address(payload)  000007F0  00000010
```

The LOAD_ALU_CONSTANT command loads hardware registers47F0–47FF. Pixel constant
base4400 makes these c252–c255. A zero-count terminator ends this upload group.
The earlier binder metadata mask/descriptor walk alone does not describe this
separate constant-upload path. Pinned reference `src/graphics/command_processor.cpp`
lines702 and1713, and `src/graphics/d3d12/command_processor.cpp` line4378, establish
the packet and pixel-bank mapping. The HLSL embeds these proved bits with
`asfloat`; it requires no runtime constant defaults or constant buffer.

## R8 conversion and precision boundary

The movie factory request is `28000002`. Original builder `8243F928` packs
hardware format2, endian0, tiled0, all signs0 (unsigned), number-format0
(fractional), swizzleA00=`XXX1`. Factory `824405D8=93C1005C` passes exponent0;
builder `8243FA04=82A1015C` reads it. The resulting resource word+28 satisfies
`word & 0007FFFF == 00001400`. Binder `82440988=509B0058` imports inherited
filter bits19–30 without changing these low19 bits. Thus there is no integer,
signed, biased, gamma, or exponent conversion of the sampled scalar.

The pinned reference maps format2 to R8_UNORM and its format swizzle to RRRR;
composed with XXX1 this yields RRR1. Only X is read. Source-byte normalization
is therefore byte/255, followed by effective filtering. The R8 format and
sampling interpretation do not derive from the shader name. See
`src/graphics/d3d12/texture_cache.cpp:200`, `pipeline/texture/cache.cpp:382`,
`pipeline/shader/dxbc_translator_fetch.cpp:1919`, and `xenos.h:120` beneath the
reference graphics directory.

HLSL uses 32-bit `float`, explicit products, ordered sums, and `precise` output.
This matches the pinned translator's separate multiply/add policy for DP3 and
DP2ADD (`dxbc_translator_alu.cpp:178`; also the SPIR-V implementation), avoiding
an unjustified fused dot-product replacement. [HLSL precise](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-variable-syntax)
preserves operation grouping and prevents multiplication/addition contraction.
The compiled listing has precise mul/add, no MAD or DP instruction; harmless
commutation and removal of adding literal+0 are verified over the fixture domain.

**No console/host floating-point bit-identity claim is made.** The independent
numeric oracle rounds each operation to nearest-even binary32; physical GPU
rounding, filter/LOD precision, interpolation and subnormal/exceptional behavior
have not been compared with Xenos. [Direct3D floating-point rules](https://learn.microsoft.com/en-us/windows/win32/direct3d11/floating-point-rules)
permit implementation tolerances. Fetch sample-location0 is decoded as centroid;
the native entry uses ordinary interpolated UV, with single-sample integration
as its initial rasterization scope. Multisample coverage/interpolation equivalence
is unqualified. Target clamp, gamma, blend, write masks, rectangle expansion and
presentation remain outside this PS. Alpha zero must survive any target helper.

## Reproduce and evidence

```powershell
python -B tools/analyze_movie_shader.py --verify --verify-references
python -B -m unittest discover -s tests -p test_movie_shader_evidence.py -v
```

`--write` regenerates only `build/movie-shader/movie-shader.json`; default output
is stdout. The inspector is narrowly pinned and fails closed on changed image,
record, metadata, opcode, flags, constants, schedule or trailer. No modified
original or runtime memory is accepted. Reference verification is read-only.

The evidence directory contains the deterministic decoded report, 684
image-matched CPU disassembly rows across 11 pinned provenance spans, separate
FXC binaries and hexadecimal listings for both entries, a compiler/source/output
hash manifest, and test results. The tests cover all 4,320 single-bit record
corruptions; direct field/schedule/metadata rejection independent of the record
hash; R8 scalar channel and preserve semantics; export alpha; clipping boundaries;
and 2,048 independent exact-rational oracle cases. Those include all 256 values
of each plane separately, seeded asymmetric triples and between-byte samples.
Both compiled listings are also evaluated against those oracles, including
the packed helper. This checks emitted dataflow, not execution on a GPU.

The integration check additionally pins the native stage signatures:
`VSTextured` outputs `SV_Position` in register0 and `TEXCOORD0` in register1.
Both movie pixel entries retain that complete input signature and read UV from
register1. The initial scalar-only pixel input assigned UV to register0 and
failed D3D stage linkage; the cropped GPU fixtures exposed that error even
though uniform-color arithmetic fixtures passed. The pre-fix listings are
retained as rejected regression inputs. There are now19 offline tests.

Rebuild the standalone shader/linkage evidence with
`python -B build/movie-shader/compile.py`, using the pinned FXC executable.

Files: [inspector](../tools/analyze_movie_shader.py),
[tests](../tests/test_movie_shader_evidence.py),
[decoded evidence](../build/movie-shader/movie-shader.json),
[original provenance](../build/movie-shader/original-provenance.txt),
[compile manifest](../build/movie-shader/compile.json).
All 9 reference file SHA256 pins are in the inspector/report. The original
[draw](native-movie-draw.md) and [plane](native-movie-planes.md) evidence remains
unchanged.
