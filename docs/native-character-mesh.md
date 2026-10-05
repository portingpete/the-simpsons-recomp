# First character shadow mesh

Native implementation and original evidence, 2026-09-13. The flat
image SHA256 is `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
All offsets/addresses below are hexadecimal unless identified as decimal.

## Original calls and ownership

| Engine BL site | SDK target | Resume/LR | Meaning |
| --- | --- | --- | --- |
| 827063B8 | 8243C5C0 | 827063BC | Stream0, resource G+38, offset0, stride G+4 |
| 827063C8 | 82445798 | 827063CC | Declaration from BE32(BE32(G+30)+4) |
| 827063D4 | 8243C768 | 827063D8 | Index resource G+58 |
| 82706454 | 8244D360 | 82706458 | Indexed submesh draw |

Original8273B760 initializes vertex bytes/data from G+0/G+10 and index
bytes/format/data from G+14/G+18/G+1C. At8273B7EC..F8 it calls82701BD8
with elements G+C and count G+8, then stores the cache-entry pointer at G+30.
Each cache entry is12 bytes: `{hash, originalDeclaration, alternateDeclaration}`.
The observed first entry is `BC06D35F E1B2A840 E1B2A920`; the next three
logged words belong to the next entry. Original82745390 hashes every byte with
`h=h*1003F+byte`, modulo2^32. All156 captured declaration bytes yield BC06D35F.
82701D48 directly creates the original declaration through824458E0; this path
bypasses the screen/scratch DeclarationRegistry. The alternate changes color
to stream1/offset0 (82701D24..40); this draw uses the original at cache+4.

## Declaration, fetch and endian mapping

The13 records are12 data elements plus terminator, each12 bytes:
BE16 stream, BE16 byte offset, BE32 type, method/usage/index/opaque bytes.
SDK82445900..18 terminates on stream00FF; engine823EF84C..86C uses typeFFFFFFFF.
Preserve the opaque byte. The first stream has48 bytes/vertex:

| Usage/index | Offset | Type | Shadow input |
| --- | --- | --- | --- |
| Position0/0 | 00 | 002A23B9 float3 | TEXCOORD0 |
| Normal3/0 | 0C | 002A2187 signed normalized packed10/10/10/2 | Unused |
| UV5/0 | 10 | 002C23A5 float2 | TEXCOORD1 |
| Bone indices2/0 | 18 | 001A2286 unsigned unnormalized byte4 | TEXCOORD3 |
| Weights1/0 | 1C | 001A23A6 float4 | TEXCOORD2 |
| Color10/0 | 2C | 00182886 unsigned normalized byte4, ZYXW selectors | Unused |

Six further rows are position usages0/index1..6, streams1..6, offset0,
type002A23B9. They are unused by VS820C2FA0. Metadata820C3DA8..3DB4 contains
`00100007 00005008 00001009 0020200A`: fetch slots7/8/9/10 match usages0/5/1/2,
all index0. Original8245EE64..84 compares these against declaration bytes+9/+A.
8245EED0..EF0C derives component selectors;8245EF94..EFC0 inserts format/sign/
integer fields. For001A2286: low6=6, sign bit8=0, integer bit9=1, selectors
at bits10,13,16,19 are XYZW. Original fetch10 itself then selects WZYX.

Vertex header G+54=`10002C72` uses endian2,8-in-32; construction is
82C1FB10..38. G+50=`E822793F` includes low resource/type bits: mask low2 when
comparing with data pointer G+10=`E822793C`. For the bone field, read
`q=BE32(vertex+18)` and extract unsigned lanes at shifts0,8,16,24, then convert
to float without normalization. `00 00 00 04` means `(4,0,0,0)` and weightX=1
selects bone4. Keep the existing HLSL `.wzyx`; do not pre-reverse the input.
Preserve float position/UV/weight bits, including signed zero; do not renormalize.
The native decoded layout is52 bytes: float3 position@0, float2 UV@12,
float4 weights@20, float4 indices@36 (these four offsets are decimal).

Original8243C6A8/B4 stores `stride>>2` in one byte. Supported byte strides must
be divisible by4 and at most1020 decimal. Runtime owner corrected that guard;
the regression tests retain49 and1024 as rejection cases.

## Index draw

82706444..50 loads r7/r6/r5/r4 from submesh+18/+14/+10/+C. Their meanings:
r4 primitive type, r5 base vertex, r6 start index in elements, r7 index count.
8244D5D8..EC writes r5 to VGT_INDX_OFFSET2102.8244D620/63C computes
`IB.data+2*r6`; the32-bit branch uses4*r6.8244D618/624 inserts r7 directly into
the draw index count. No triangle-count multiplication is performed.

Observed arguments `(6,0,0,241)` mean triangle strip, base0, start0,577 indices
(decimal). Index header20000002 gives16-bit indices and endian1,8-in-16.
Decode BE16 individually and preserve FFFF. Live scalar148=1/14C=FFFF enables
cuts; setters are8243B750/8243B780. The capture has237 vertices,577 indices,
56 cuts and non-cut indices0..236. Native mapping is R16_UINT triangle strip,
`DrawIndexed(577,0,0)`. Renderer ownership, depth adaptation and gameplay
verification remain separate from this decoder evidence.

## Native implementation and live result

The original82706378 submesh loop now executes with native operations at its
four recorded BL sites. Each entry captures fresh immutable vertex/index data,
validates its declaration and buffer endian/width fields, uploads real D3D11
buffers, binds its input layout/index buffer, and submits DrawIndexed. Native
buffers own their data through GPU completion; reused original addresses never
reuse a stale mesh snapshot. Original mesh storage and nonvolatile ABI remain.

The depth adapter retains original VS820C2FA0 and its actual constant-buffer
owner. It temporarily supplies a depth-only PS, preserves color/stencil, applies
logical depth reversal once and rounds once to decoded20e4. Bias follows the
original SDK slope-times16 operation and the reference's conversion of that
subpixel slope back to pixel units. Constant offset is added directly. The
September27 immediate effect audit corrected previously reversed slope/offset
labels using the original GPU register upload; see `immediate-effect-crashes.md`.
This is a qualified native policy, not proof of
physical-console subpixel/interpolation/rounding parity.

The complete integration build passes137/137 suites in191.29s:
`build/reach-game-mesh-full-build-tests.log`. Separate hardware/WARP fixtures
verify GPU vertex/index bytes, exact depth pixels, comparisons after rounding,
nonzero bias, clipping, restart/scissor/cull behavior and binding restoration.
The actual original mesh-loop fixture submits two submeshes and rejects an
invalid range before drawing; it passes1056 checks. The decoder passes642
checks when additionally run against the captured first live mesh.

Live `build/automatic-startup/reach-game-003` submits seven real meshes with
237,928,743,2144,977,1529 and792 vertices. It then fails at the separate
RenderShadowDepthAlpha activation, caller8270614C. This is advancement past the
previous first stream-binding failure, not a completed gameplay color frame.
Subsequent diagnostic capture reads that private depth target before the same
explicit alpha boundary; its focused original-loop fixture passes again.

## Checks and integration

`tests/test_character_mesh.cpp` defaults to self-contained synthetic13-row
declarations and48-byte vertices. It checks bone lane order, non-normalization,
weighted and zero-weight bounds, signed-zero bits, ownership, missing/duplicate
consumed semantics, malformed terminators/formats, truncation/stride, all consumed
NaN/infinity lanes, and BE16 restart preservation. One optional positional
argument is a capture directory containing the three shadow-mesh-*.bin files;
the default CTest must not depend on that directory. C++ compilation/execution
is left to the runtime owner's combined build.

`python -B tools/analyze_character_mesh.py --verify` checks the exact original
image, four BL words/targets, relevant CPU instructions, literal declaration
hash and shader metadata/fetch association. Optional `--reference-root
K:/Simpsons/RexGlueCurrent` additionally hashes five audited local source files.
The verifier passed with all five references checked. It writes no files and
does not execute a reference renderer, shader translator or command processor.

Suggested CMake registration (runtime owner supplies character_mesh.cpp):

```cmake
add_executable(CharacterMeshTests tests/test_character_mesh.cpp)
target_link_libraries(CharacterMeshTests PRIVATE SimpsonsRuntime)
target_compile_options(CharacterMeshTests PRIVATE /fp:strict /W4 /WX)
add_test(NAME NativeCharacterMeshDecoder COMMAND CharacterMeshTests)
add_test(NAME OriginalCharacterMeshEvidence COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_SOURCE_DIR}/tools/analyze_character_mesh.py" --verify)
```
