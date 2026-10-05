The native static zprepass now completes133 draws in `reach-game-018`, after
both shadow maps. Its six focused shader/mesh/decoder checks pass on WARP and
hardware where applicable; the whole original CPU fixture passes570 checks,
including real catalog/camera creation, matrix/Boolean uploads, dirty state,
material filtering, depth/stencil/color, cleanup, ABI and rejection. The
actual tangent-layout capture passes2322 decoder checks. A completed gameplay
color frame and player control are still unverified.

The earlier `reach-game-013` boundary identified the original zprepass branch of
`82740680`, selected by `*(82D6CCA8) & 40`. Its typed owner is `E1AA7BE0`,
vtable `8215022C`, wrapper `E1AA7B28`, identity `00500019`, source effect
`821490E0`. `Z+AC` selects `TechniqueOpaque`, handle `0003FFFC`.
This document records the original shader and CPU boundaries supporting that
native milestone. Skinned/morph prepass submissions remain unqualified.
Addresses, offsets and handles are hexadecimal. Sizes and counts are decimal.

The original pair is **VS `8214A8A4`, null PS**, pass handle `0003FFFE`.
The other technique, `TechniqueAlpha` (`0007FFFC`, stored at `Z+A8`), uses
VS `8214B9B8` and also a null PS. Each VS record is 4,364 bytes: literal
bank at record `E68`, 64 bytes; code at `EA8`, 612 bytes including a
12-byte unexecuted trailer. The 600 executable bytes and literal banks
are identical. Only the first trailer word differs (`4E4A0001` versus
`4E4A0000`). These names do not imply an alpha-sampling pixel shader.

Both passes have exactly one literal scalar row, **SDK state `38` = `2`
(cull)**, and no sampler rows. Depth enable/write/comparison, color-write
masks, attachments, viewport and depth bias are inherited effective state.
The shader exports only clip position. It has no texture read, alpha
reference, discard, explicit depth export, UV output or shadow-height clamp.
Native selection must bind the real VS and clear the pixel-shader stage.

The authored [HLSL](K:/SimpsonsNativeCopy/renderer/zprepass_shader.hlsl)
provides `VSZPrepass` (`vs_5_0`) and test-only `GSZPrepassProbe` (`gs_5_0`).
Native constant buffer `b0` is `float4 c[244]`, 3,904 bytes; native `b1`
is one `uint4`, 16 bytes, with the normalized original Boolean register
zero in X. Native buffer binding `b0` and original Boolean register `b0`
are separate concepts. The geometry probe only observes the VS position
through stream output and is not a game-stage replacement.

The original nine FETCHes match declaration usage **and index**, as
shown by `8245EE64..84`. They map to decoded native inputs as follows:

| Original semantic | Native semantic | Components | Original destination |
| --- | --- | --- | --- |
| POSITION0 | TEXCOORD0 | xyz | r5.xyz |
| BLENDWEIGHT0 | TEXCOORD1 | xyzw | r6.xyzw |
| BLENDINDICES0 | TEXCOORD2 | xyzw | r4.wzyx |
| POSITION1 | TEXCOORD3 | xyz | r7.xyz |
| POSITION2 | TEXCOORD4 | xyz | r3.xyz |
| POSITION3 | TEXCOORD5 | xyz | r2.xyz |
| POSITION4 | TEXCOORD6 | xyz | r1.xyz |
| POSITION5 | TEXCOORD7 | xyz | r8.xyz |
| POSITION6 | TEXCOORD8 | xyz | r0.xyz |

Original Boolean `b0=false` jumps from CF2 to CF10, bypassing both morph
and skin arithmetic. The static result consumes only POSITION0 and
`c0..3`. With `b0=true`, slots17/18 set a predicate from **`c37.w >= 0.5`**;
false skips only six morph MADs. Those MADs add POSITION1–6 deltas in
order, weighted by `c36.xyzw`, then `c37.xy`; `c37.z` is unused.
Skinning then blends four bones in W,Z,Y,X order. MOVA uses
`clamp(floor(index*3+0.5), -256, 255)`; slot30's vector read uses the old
address before its co-issued address update. The HLSL preserves that
ordering, swizzles and unnormalized weights. Both paths finally transform
homogeneous position using the four original matrix rows. CF13 is an
empty EXEC_END and does not re-execute FETCH slot7.

The live013 first mesh has 245 vertices, stride 36, 452 index entries,
one submesh and zero bones. Its six declaration rows include the sentinel:
POSITION0 at `0` (type `002A23B9`), NORMAL0 at `C` (`002A2187`), COLOR0
at `10` (`00182886`), TEXCOORD0 at `14` and TEXCOORD1 at `1C`
(`002C23A5`), then stream `FF` / type `FFFFFFFF`. Normal, color and both
UVs are shader-dead here. Missing skin and morph attributes may have
zero native placeholders only while the original Boolean is false;
those placeholders are not inferred weights or a qualified skinned layout.

Constant ownership remains with the original typed effect, wrapper and
attached shared pool. Let `Z` be the typed owner, `W=*(Z+18)`,
`F=*(W+10)` the live effect body, and `P` its attached shared pool.

| Parameter | Handle / typed field | Storage and leaf | Original register mapping |
| --- | --- | --- | --- |
| g_ViewProjection | `00040001`, `Z+C0` | shared slot0, leaf0 | float `c0..3` |
| kIsSkinned | `00300014`, `Z+B8` | private slot16.x, leaf10 | Boolean register0, category4 |
| kBlendWeights | container `00340016`, `Z+C4` | private slots17–18, leaves11–12 | float `c36..37` |
| kBoneMatrices | container `0040001A`, `Z+BC` | 64 matrices at slot19+4i, leaves13–76 | three float4s at `c52+3i` |
| g_World | `000C0004` | private slot5, leaf2 | absent from this pass's upload map |

Source-body `+108=390` locates 81 private descriptors; `+128=620`
locates **4,400 default bytes**, 1,100 words. There are 77 private leaves
and four shared leaves (`+130/+134`); source shared storage is 112 bytes
(`+13C`). The attached pool can contain a larger union of parameters.
Copy the original private defaults: they are not all zero. In particular,
slot16.w is one, while the Boolean setter writes only slot16.x.

The two technique contexts, source-body `45C0` and `4BF0`, have identical
constant mappings. Each private map has 77 rows of 16 bytes, each source
shared map four rows of 16 bytes. Private category-zero mask is
`001FFFFFFFFFFFFFFFF8000000000000` (leaves11–76); category four is
`00200000000000000000000000000000` (leaf10). Shared category-zero mask
is `8000000000000000` (leaf0). Other categories are empty. There are
198 mapped float4 registers in the 244-register native bank; holes have
no shader read. Bone rows map three vectors, even though CPU storage and
the original setter retain all four vectors per matrix.

`kIsSkinned` has descriptor `00000008/00010010`. Despite its Boolean
upload category, original `823C8EB0` converts the low-byte truth value to
**float 0/1**: `823C8F2C vcfux`, then `823C8F5C stvewx`. Its dirty
write at `823C8F4C` ORs private byte1 with `20`. Blend leaves set byte1
bits `10/08`; the first bone leaf uses `04`. Shared matrix leaf0 sets
shared byte0 bit `80`. Preserve untouched lanes and dirty bits.

Original `826B1FB0` clears the 128-byte private dirty line and seeds
`16 * ceil(F[120]/2)` bytes with `FF`; zprepass source count is `2`,
so this is 16 bytes, with a zero tail. Shared reseeding at
`826B1FF0..204C` occurs when `*(82D00F80)==0 || *(F+2B8)==0`, using
the live `F+124` count. Writer `82C1D3BC` stores the actual pool at
`F+2B8`; `82C1D334..338` copies `P+114` into `F+124`. Source-body zero
at `2B8` does not prove an unattached live effect. Commit consumes owned
values through these masks, and clears dirty storage only after actual
native constant publication succeeds.

The narrow CPU path keeps the complete original dispatcher and geometry
walkers. At dispatcher entry, `P` below denotes the packet, rather than
the shared pool: `P+0=metadata`, `P+4=object`, `P+8=camera`.

| BL site → callee | Arguments / required behavior |
| --- | --- |
| `827406C4 → 826B6078` | `W, *(Z+AC)`; activation resume `827406C8` |
| `827406D4 → 8270BF08` | `object, Z, camera`; retain original frame/camera-relative matrix arithmetic |
| `8270C26C → 82704600` | effect identity, `*(Z+C0)`, helper `SP+90`; writes the combined matrix into shared storage |
| `82740814 → 823C8EB0` | static: `W, *(Z+B8), 0` |
| `8274081C → 826B3980` | static wrapper commit; resume `82740820` |
| `826FF340 → 8243C5C0` | ordinary stream0: context, `0, G+38, 0, *(G+4), 1` |
| `826FF498 → 82445798` | declaration from ordinary cache `+4` |
| `826FF4A4 → 8243C768` | indices at `G+58` |
| `826FF584 → 8244D360` | static draw: primitive, base vertex, start index, count from submesh `+C/+10/+14/+18` |
| `826FF5B0 → 8243C5C0` | static cleanup: context, `1, 0, 0, 0, 1`; then original object cleanup |

Here `G=*(metadata+C)`. Original `8270BF08` uses object/camera frames
and globals `82CD1AB0`, `82D0CA70`; its output already combines the
per-object transform and camera matrix. Uploading an independent world
matrix instead would change the shader's inputs. Dispatcher context
adaptation at `8274069C` must retain the original setter and flags `40`.

For later skinned captures, `827407C0 → 8273FB68` writes two morph
vectors, `827407D0` sets the Boolean true, and `827407D8` commits before
entering `82700318`. The helper computes bones via `826FE7C8`, writes
them at `827003A0 → 826FD060`, sets the Boolean again at `827003B0`,
then performs a distinct direct SDK commit at **`827003B8 → 82C1DBA0`**.
Its draw is `82700470 → 8244D360`, with the same submesh argument layout.
More than 64 bones uses per-submesh palettes and separate commit sites;
the ordinary static capture does not qualify those native bindings.
Both walkers retain the original material skip bit `20`. Morph streams,
alternate color streams and auxiliary geometry branches also remain
separate geometry qualifications; a complete shader transcription alone
does not establish those native stream contracts.

The offline [analyzer](K:/SimpsonsNativeCopy/tools/analyze_zprepass_shader.py)
pins the original PE, both full records, effect metadata, UCODE field
references, constants, literal state rows and original CPU spans/calls.
It decodes the nine FETCHes, both conditional jumps, address updates and
position exports. A separate source check constructs the full VS body
statically from decoded fields and compares it with the authored HLSL,
including co-issued address ordering; this does not execute instructions.
Whole-source pinning also covers interfaces and the test-only probe.

`python -B tools/analyze_zprepass_shader.py --verify --self-test` passed
**21,606 mutation checks** on September 13, 2026, plus the positive
original/source checks. Mutations cover every record byte, every code bit,
control flow, masks, register mappings and consequential HLSL changes.
The executable SHA-256 is
`b05ca8f1b48c659d0fefa13ecd414f4c36538e922edac30100c68ac3f5df4c1b`.
Native GPU tests and runtime integration are now exercised by
`ZPrepassMeshTests`, `ZPrepassShaderTests`, `ZPrepassVertexTests`, and
`ZPrepassPassTests`. Finite inputs and integer bone indices
0–63 bound the authored arithmetic; console/native floating-point and
raster precision equivalence is not established by this static proof.
