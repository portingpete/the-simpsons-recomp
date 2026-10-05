# First FourTapBlend shader qualification

Shader-body qualification only. The two exact original records implement **four samples of one 2D texture at four supplied UV pairs**, weighted by **PS c0.x, c1.x, c2.x, c3.x**. They do not calculate blur offsets, normalize the weights, clamp color, replace alpha, apply a matrix, or construct position Z/W. This follows the complete executed instructions, independently of the effect name/reflection.

Native source: [fourtapblend.hlsl](K:/SimpsonsNativeCopy/renderer/fourtapblend.hlsl), entries **VSFourTap / PSFourTap**. Offline FXC compilation and independent headless D3D11 qualification passed on **WARP** and **NVIDIA GeForce RTX 3080 Ti**: **14 draws and 4,160 RGBA/depth comparisons per device**, plus the explicit native MAD rounding probe. This does not establish original effect association, engine readiness, an original scene, or presentation.

## Exact inputs and offline authority

The pinned flat original `analysis/simpsons.pe` has base82000000, sizeEC0000, SHA-256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`. Ranges below are half-open; addresses and instruction sizes are hexadecimal except where marked decimal.

- VS record **820B8F08**, header/instruction offset **114**, payload **A8**, total **444 decimal bytes**, ending820B90C4. SHA-256 `8dcb727e02d552e6ca8d18b09ffc57d11856c8d418d962d00b24c914e11a7347`.
- PS record **820B90D4**, header/instruction offset **188**, payload **84**, total **524 decimal bytes**, ending820B92E0. SHA-256 `2c71ebc59531050284a4473619cb2b810d2854c65ad0dcf483577b107e7c7ea3`.

Every word is read big-endian. The analyzer validates the full image and exact whole-record hashes before decoding. Bytes between records are excluded. Record reflection/typed caller and declaration association remain Popper/main's separate work.

Read-only declarative authority is `K:/Simpsons/RexGlueCurrent/include/rex/graphics/format/ucode.h`, SHA-256 `e820997c448f0fe4734738cac1526b841dde947e2dac568f56c022d1a55a59fb`: EXEC fields218..255, CF unpack515..522, fetch layouts689..857, MUL/MAX/MULADD1336..1437, export registers1767..1816, relative swizzles1967..1970 and ALU layout2005..2057. Referenced enum file `include/rex/graphics/xenos.h` is also pinned: SHA-256 `7857b51de405a2414d1b3f4c6b86854c4471077b0596acd35c78c26295093227`. No reference translator/backend code is copied or linked. This reuses the extraction conventions of the existing screen shader inspector; the old inspector and its supported profile remain unchanged.

## Complete control flow

Each control-flow slot contains two 48-bit instructions; executable addresses index 12-byte slots from the payload start. Every non-CF/non-trailer slot executes exactly once; there are no conditional branches, loops, predicates, scalar side operations or extra executable slots.

VS CF at820B901C/9028/9034:

```text
F1555003 00001201 C2000000  EXEC slot3 count5 sequence155; ALLOC position
00001008 00001200 C4000000  EXEC slot8 count1 sequence0; ALLOC interpolators
00004009 00002200 00000000  EXEC_END slot9 count4 sequence0; NOP
```

PS CF at820B925C/9268:

```text
00554002 00001200 C4000000  EXEC slot2 count4 sequence55; ALLOC colors
00004006 00002200 00000000  EXEC_END slot6 count4 sequence0; NOP
```

Sequence pairs select fetch=1/ALU=0; no scheduled instruction has the serialize bit. Predicate-clean is set, yield and addressing-mode fields are zero. The first VS EXEC's cache-hint fields are **vc_hi=F, vc_lo=1**; all other cache hints are zero. Their bit positions are separate from address/count and preserved in the report. They are not vertex arithmetic or native resource allocations. ALLOC size fields are reported raw as0; native DXBC declares its actual five VS outputs independently, without reproducing a console allocation interface.

The complete unexecuted trailers are **4E4A0001 2F199EE6 ED5862DD** for VS and **4E4A0000 2F199EE6 ED5862DD** for PS. Neither may be decoded/executed as an ALU. The prior screen inspector's trailer/EXEC assumptions alone would reject this VS; the new inspector explicitly handles its verified cache-hint bits and exact trailer.

## Vertex fetches and exports

Five fetches precede five MAX(v,v) exports:

```text
slot VA        word0    word1    word2
 3   820B9040  05F82000 00000688 00000000  fetched position -> r2.xyzw
 4   820B904C  05F81000 0000023F 00000000  fetched tap0.xy -> r1.zw, keep r1.xy
 5   820B9058  05F81000 00000FC8 00000000  fetched tap1.xy -> r1.xy, keep r1.zw
 6   820B9064  05F80000 0000023F 00000000  fetched tap2.xy -> r0.zw, keep r0.xy
 7   820B9070  05F80000 00000FC8 00000000  fetched tap3.xy -> r0.xy, keep r0.zw
 8   820B907C  C80F803E 00000000 C2020200  position62.xyzw = MAX(r2,r2)
 9   820B9088  C8038000 001A1A00 C2010100  interpolator0.xy = MAX(r1.zw,r1.zw)
10   820B9094  C8038001 00B0B000 C2010100  interpolator1.xy = MAX(r1.xy,r1.xy)
11   820B90A0  C8038002 001A1A00 C2000000  interpolator2.xy = MAX(r0.zw,r0.zw)
12   820B90AC  C8038003 00B0B000 C2000000  interpolator3.xy = MAX(r0.xy,r0.xy)
```

Fetch destination selectors688=[X,Y,Z,W],23F=[keep,keep,X,Y],FC8=[X,Y,keep,keep]. Relative ALU swizzles1A andB0 decode to[Z,W,W,W] and[X,Y,Y,Y]; only XY is exported for each UV. Position exports all four fetched components unchanged for the qualified finite domain. Unlike Screen_Xenon, this VS does **not** supply Z=0/W=1. There are no VS constants.

All embedded vertex fetches use source r0.x, fetch-constant index95 and placeholder format/stride/offset0. They have no relative registers, predicate, mini-fetch, exponent adjustment, signed conversion or index rounding; normalized-format bit is reported as encoded, not claimed as a final vertex format. **These are not a verified runnable declaration.** Native input labels `POSITION` float4 and `TEXCOORD0..3` float2 denote the five decoded fetch results in order. The fixture's 48-byte float layout is owned test data, not a claim that the original caller has that stride/format. Main must associate actual declaration patching, offsets, component expansion and stream ownership before an engine draw can use it.

## Pixel fetches and exact accumulation order

```text
slot VA        word0    word1    word2
 2   820B9274  10084001 1F1FF688 00004000  texture0(r0.xy) -> r4
 3   820B9280  10081021 1F1FF688 00004000  texture0(r1.xy) -> r1
 4   820B928C  10082041 1F1FF688 00004000  texture0(r2.xy) -> r2
 5   820B9298  10080061 1F1FF688 00004000  texture0(r3.xy) -> r0
 6   820B92A4  C80F0000 00006C00 81000300  r0 = r0 * c3.x
 7   820B92B0  C80F0000 00776C77 AB020200  r0 = MULADD(r2.wzyx,c2.x,r0.wzyx)
 8   820B92BC  C80F0000 00006C77 AB010100  r0 = MULADD(r1,c1.x,r0.wzyx)
 9   820B92C8  C80F8000 00006C00 AB040000  color0 = MULADD(r4,c0.x,r0)
```

Every fetch is opcode1, dimension1=2D, normalized coordinates, source components[X,Y,X] with only XY used, identity RGBA destination swizzle688. All use fetch constant0. Computed LOD=1; register LOD/gradients=0; instruction LOD bias=0; X/Y/Z half-texel offset fields=0. Min/mag/mip and volume-filter fields are3 (fetch-constant state); anisotropy is7 (fetch-constant state). Fetch-valid-only=1, no predicate or relative addressing. Arbitrary-filter raw0 names `k2x4Sym`; the reference marks that field deprecated, so it must not be mislabeled as an enabled native four-tap filter or as an enum called “none.” The four texture operations come from four explicit instructions.

Sample-location field0 names centroid. Effective original interpolation still depends on original sampling controls. The native artifact currently qualifies **single-sample** rasterization only, where center/centroid coincide for covered pixels. No multisample/centroid or general original interpolation policy is declared implemented.

All nine ALUs in both stages have scalar opcode50=retain_previous, scalar mask0, no clamping, negation, absolute values, predicate or relative addressing. Pixel masks areF. Only the final PS operation exports color0; no depth export or discard occurs. Source2 swizzle6C broadcasts X; temporary swizzle77 is WZYX. Sources are captured before writing r0. The paired reversals cancel, leaving each final color component in original RGBA order:

```text
a = tap3 * weight3
a = MULADD(tap2, weight2, a)
a = MULADD(tap1, weight1, a)
out = MULADD(tap0, weight0, a)
```

This is an ordered weighted sum, including alpha, not a dot product with one float4 color, not four textures, and not an average with an implicit1/4. The HLSL retains the literal original intermediate swizzles; offline FXC removes the cancelling permutations and interleaves samples with arithmetic while preserving the accumulation chain.

## Native contract and numerical qualification

- **VSFourTap**, `vs_5_0`: decoded POSITION.xyzw plus four separately supplied decoded UV pairs. No matrix, half-pixel shift, generated offsets or automatic depth replacement.
- **PSFourTap**, `ps_5_0`: `t0=Texture2D<float4>`, `s0=SamplerState`, implicit-LOD `Sample` at each UV. No hardcoded LOD or sampling offset. Caller supplies a compatible view and effective sampler; no absent-resource or failed-effect fallback.
- **b0** contains four float4 registers, **64 bytes**, with weights at byte offsets **0,16,32,48**, X only. YZW are unused. Packing four weights into one float4 without an explicit native remap would change the original register contract.
- Ordered **precise MUL + three precise mad** operations. The offline DXBC check requires exactly four `sample`, one `mul`, three `mad`, no discard, and no `sample_l`. VS DXBC passes the position and all four UVs through.

The local MULADD definition cites a fused single-rounding implementation. D3D11 `precise mad` preserves the authored operation/order but permits a consistent fused or unfused implementation; it does not promise console bit identity. [Microsoft HLSL mad](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/mad), [precise variable rules](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-variable-syntax).

The deliberate cancellation probe uses `(1+2^-23)*(1-2^-23)-1`: WARP produced **+0, bits00000000**; RTX3080Ti produced the fused result **-2^-46, bitsA8800000**. The test checks every pixel/lane for consistency and reports the outcome; it never treats WARP's result as original hardware proof. This native rounding policy is a concrete qualification limit, not a claim that shader-body translation is impossible. General exceptional/subnormal behavior, overflow and exact Xenos/native filtering/LOD precision are unproved. D3D11 has its own accuracy and denormal rules. [Microsoft floating-point rules](https://learn.microsoft.com/en-us/windows/win32/direct3d11/floating-point-rules).

## Executed tests and reproduction

[test_fourtap_shaders.cpp](K:/SimpsonsNativeCopy/tests/test_fourtap_shaders.cpp) creates its own real D3D11 device/context, immutable asymmetric RGBA32F mip texture, float target/depth, buffers, sampler and shaders. No NativeBackend private access, original shader interpreter, runtime shader compiler, game startup, output device, window or presentation is used. CPU expected values independently calculate sampling/addressing and the ordered weighted result with `std::fma`. Finite dyadic fixtures keep arithmetic results representable; RGBA/depth comparisons allow4e-6 for native interpolation/filtering. Rounding-policy discrimination is separate and exact.

Coverage includes four basis weights; unequal signed/non-normalized weights; negative/HDR components and independent alpha; distinct rotated/mirrored/shifted UV pairs; wrap versus clamp; point versus quarter-texel linear filtering; four independently computed power-of-two LODs; sampler-forced LOD1; and actual W=2 partial coverage with fetched Z=.5 yielding depth.25. Unused constant YZW components contain asymmetric sentinels. Real GPU event-query completion precedes staging readback.

```powershell
python -B tools/analyze_fourtap_shaders.py --verify --self-test
python -B build/fourtap-shaders/run_probe.py
```

The analyzer has **32 semantic/mutation checks**, pins both records/reference headers, decodes every executed slot, rejects altered records and unsupported fetch/ALU/CF forms, and independently reduces the temporary/swizzle dataflow to the ordered component expressions. Deterministic report: [native-fourtap-shaders.json](K:/SimpsonsNativeCopy/analysis/native-fourtap-shaders.json). Use `--write` only to regenerate that owned report; `--verify` is read-only.

The standalone runner uses installed Windows SDK **10.0.26100.0 FXC**, `/Ges /Gis /O3`, and clang-cl **/std:c++20 /fp:strict /W4 /WX**. All headers/CSO/DXBC listings/executable/logs/hashes remain under `build/fourtap-shaders`. It links only **d3d11.lib dxgi.lib**, and runs WARP then hardware without fallback. There is no parent build or regeneration.

For main integration, compile `VSFourTap`/`PSFourTap` offline into headers `VSFourTap.h`/`PSFourTap.h`, arrays `kVSFourTap`/`kPSFourTap`; add that header directory to the test include path. The standalone executable accepts optional `--hardware`; suggested CTest timeout45. Native MaterialRegistry/NativeBackend registration, caller/reflection/declaration validation, constant association, texture/view/sampler state, blend/depth/alpha/raster policy and original lifetime remain main/Popper ownership. This artifact must not by itself mark the effect ready.

Only the assigned new HLSL, analyzer, test, this document, analysis JSON and `build/fourtap-shaders/*` were written. Original assets and all references remained read-only; no existing backend/runtime/config/CMake/generated files were changed.
