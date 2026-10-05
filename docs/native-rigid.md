Current integration checkpoint: **LIVE034**, built and162/162 tests passing.
The original cache now executes16 independent payloads containing18 rigid draws;
the immediate fallback completes27 draws across18 packets. The exact
`simpsons_rigid_textured`820168F8 opaque VS8201700C/PS82017658 pair is qualified
with original base texture ownership and sampler state. The offline auditor
`tools/analyze_rigid_textured_shader.py` pins both complete records and checks
the full static source; WARP/hardware fixtures independently check packing,
sampling, shadow taps, dirty maps and direct/recorded resource lifetime.
Original BC1/BC2/BC3 mips retain every authored level. LIVE034 uploads15
mipmapped textures and reaches `simpsons_rigid_dualtextured`8202AD78, which remains
unimplemented. No presented gameplay color or character control is verified.
See LIVE034 evidence (local development record).

Historical integration checkpoint: live027 completes original rigid activation,
native recording begin with the original CPU record/cache linkage, all82 scalar
and320 sampler resubmissions, nonnull FX context association, and the original
826F39E0 shadow-resource loop. That loop binds the two copied depth owners to
deferred stages0/1 and skips the unused third row. The first material commit,
3530-index recorded draw, real finish, original CPU restoration and56+56-register
replay uploads complete. The native payload executes once, but changes no pixels
in the all-zero private scene target. The next object hits the single-record
guard; multiple records and geometry visibility remain under investigation.
No visible scene color draw or gameplay is claimed.
Native rigid mesh/shader and recording-payload tests pass on both WARP and
hardware. All160 tests pass in111.96s in
`build/reach-game-rigid-union-full-build-tests.log`.

The live018 selected technique is `simpsons_rigid` source `8200CCB8`,
`rigid` handle `0003FFFC`, pass `0003FFFE`: VS `8200D3AC` and PS
`8200D9E8`. The distinct `rigidalpha` pair is not an alias and is outside
this transcription. Original cached-recording construction/application and
material ownership remain separate native integration work.

The VS record is 896 bytes with code at offset `218`, 360 code bytes
including a 12-byte trailer: 348 executable bytes, no literal bank.
The PS record is 1,996 bytes, with 64 literal bytes at `390`, code at
`3D0`, and 1,020 code bytes including a 12-byte trailer: 1,008 executable
bytes. Both use straight-line issue blocks; PS has three uniform conditional
jumps, no loop, no bone addressing and no discard/depth export.

The native entry names are `VSRigid`, `PSRigid`, test-only `GSRigidProbe`
and `VSRigidPixelProbe`. Original VS/PS float banks are separate native
stage bindings at `b0`: VS `float4 vc[30]` (480 bytes), PS `float4 pc[50]`
(800 bytes). The PS literal bank remains authored constants. No Boolean
buffer is used: shadow enables/receiver are float parameters.

| Owner / parameter | Handle | Original registers |
| --- | --- | --- |
| shared g_ViewProjection | `00040001` | VS c0..3 |
| private g_World | `000C0004` | VS c12..15 |
| shared kWorldToViewPortTfmLight | `00140009` | VS c22..25 |
| shared kWorldToViewPortTfmCharLight | `0018000B` | VS c26..29 |
| shared kShadowAmt | `00280013` | PS c30 |
| shared kIsShadowReceiver | `002C0015` | PS c31 |
| private light type/position/color/direction | `00300010/00340012/00380014/003C0016` | PS c33/34/35/36 |
| private g_ObjectId | `004C001E` | PS c40 |
| private g_RimShadowEnable / g_ShadowEnable | `005C0026 / 00580024` | PS c46 / c47 |
| private g_customLinesParams | `0048001C` | PS c49 |
| shared kShadowDepthSampler | `001C000D` | PS texture/sampler0 |
| shared kShadowCharDepthSampler | `0020000F` | PS texture/sampler1 |

The shader itself reads PS c30.y, c31.x, c36.xyz, c40.x, c46.x,
c47.x and c49.z. PS c33..35 are mapped but shader-dead. The base sampler,
ambient, inverse world, tint colors, specular exponent and projected texture
are absent from this pass's read set. Shared handles resolve against the
actual attached pool, whose layout can differ from the source-local layout.
Private defaults occupy 544 bytes at body offset `460`, with 22 leaves;
source shared defaults occupy 320 bytes, with 11 leaves. Context `2620`
holds this pass's exact maps and masks.

Both original techniques set SDK depth enable `0x28=1` and write `0x30=1`.
For `rigid`, stages0/1 have sampler offsets `0x0,0x4,0x8,0x10,0x14,0x18` set to
`2,2,2,0,0,2`. All other states are inherited. In particular, technique
selection alone does not qualify attachments, blending, depth comparison,
texture format/swizzle or color conversion.

Original vertex FETCH semantics are POSITION0.xyz (homogeneous W=1),
NORMAL0.xyz, COLOR0.rgba and TEXCOORD0.xy. Native inputs are TEXCOORD0..3
in that order. No weights, indices, UV1 or morph data is consumed.
The VS emits UV0, both projected shadow positions, the world-transformed
normal and color. The PS reads only the incoming color's blue component;
it does not sample a base-color texture.

Each enabled shadow bank uses nine normalized 2D implicit-LOD samples,
with signed integer offsets in a 3-by-3 footprint. Original five-bit
offsets `2/30` mean `+1/-1` texels. Samples deliberately select different
R/G/B components. Native shadow texture views must preserve the original
texture-format/swizzle meaning; assuming every sample uses red would
change this shader. Sampler settings remain externally owned.

The resource swizzle and depth-format expansion are separate facts. Original
`1A220197` encodes format `23` (`k_24_8_FLOAT` / D24FS8), endian `2`, tiled `1`,
unsigned component signs, number-format bit `1`, and **identity XYZW**:
`(format >> {18,21,24,27}) & 7 = {0,1,2,3}`. It does not encode XXXX.
The original builder `8243F928` retains its format argument `r9` in `r31`
at `8243F954`. Instructions `8243FB60`, `FB84`, `FBA0`, `FBBC` extract
the W/Z/Y/X selectors; `FBD8` extracts the number-format bit, `FBE0`
inserts the packed selectors, and `8243FC0C` stores `resource+0x28`.
With zero exponent adjustment its low19 bits are **`00000D11`**. The
binder loads this word at `82440924`, replaces only bits19..30 with
cached sampler fields at `82440988` (`rlwimi r27,r4,0,1,12`), and stores
fetch dword3 at `82440998`; it preserves the low19 format/swizzle bits.
These spans and key instructions are pinned in the analyzer.

The local read-only format reference explicitly supplies **RRRR** for
`k_24_8_FLOAT`, using an R32 float depth view and depth-float unpacking:
[texture_cache.cpp](K:/Simpsons/RexGlueCurrent/src/graphics/d3d12/texture_cache.cpp:302).
Its format-swizzle composition occurs at
[texture_cache.cpp:1502](K:/Simpsons/RexGlueCurrent/src/graphics/d3d12/texture_cache.cpp:1502).
The reference file SHA256 is
`ebd1eb6fdcf509e7f7f9b476c24caeb8efaa1d7b80a05f51932bdb6963675d09`.
[xenos.h](K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h:1238)
defines the independent descriptor swizzle bits. Thus the qualified
representation is depth-format expansion RRRR followed by resource XYZW;
it is **not** DXGI's default missing G/B components. This is original CPU
descriptor proof plus format-reference evidence, not an original-hardware
sampling experiment.

Native integration must qualify the owned source/destination texture,
copy phase, dimensions, format and effective sampler state before supplying
that representation. An adapter for the qualified depth view may expand
its sampled R into RGBA before the original FETCH component selection.
The original texture handle/identity and sampler fields must remain intact.
`PSRigid` itself samples arbitrary float4 values and retains all original
channel selections. Prior raw depth/stencil copy tests establish neither
shader sample expansion nor filtering equivalence. Stencil sampling is
not consumed by this pair.

The PS encodes object ID (c49.z overrides c40.x when nonzero), wrapped UV
bins, a COLOR0.blue threshold and the rim flag into RGB. Shadow work is
uniformly gated by c47.x != 0; the two sample blocks require c31.x > 0.
Depth comparisons use `1-saturate(projectedZ/projectedW) >= sample`.
The 3-by-3 filter combines column-selected channels with fractional
`projectedUV*1024` weights. c30.y, the normalized normal and c36.xyz
control the final shadow gates, followed by the original floor and
`.39*(1-floor(visibility))+.01` alpha packing. These comparisons are
**SETGTEv (opcode6)**, including equality; scalar predicate29 is strict
greater-than. Co-issued vector/scalar expressions read the old register
values, and the scalar previous-result value is preserved across issues.

`python -B tools/analyze_rigid_shader.py --verify --self-test` passes:
**841 rejected mutations**, covering shader headers/instruction words,
defaults, complete category masks and register maps, state rows, authored
source, and the original depth descriptor. The inspector pins the image,
effect, both full shader records, enum references and depth-format reference.
It compares the complete static HLSL transcription, including structured
branches and literal bit patterns; it does not execute original shaders.

`tests/test_rigid_shader.cpp` passes on WARP and hardware. It consumes
offline `VSRigid`, `PSRigid`, `GSRigidProbe` and `VSRigidPixelProbe` headers,
with WARP by default and optional `--hardware`. The VS probe reads all
21 output floats through stream output and compares them with independent
matrix geometry, including nonaffine W and nonunit normals. The PS probe
reads every pixel/lane of a float target and checks packing, equality
boundaries, conditional unbound-texture paths, separate constant banks,
projection and shadow gates. Two 1024-square RGBA float fixtures have
independent channels; **each of the 18 taps independently changes final
alpha**, exposing channel, offset, bank and comparison mistakes. These
fixtures intentionally do not stand in for a native D24FS8 view test.

`tests/test_rigid_mesh.cpp` also passes on both renderers with actual deferred
recording, finish and execution; independent material snapshots, fresh inherited
constants, immediate-state preservation, RGB10A2 color and depth/stencil
readback, and the explicit D24FS8 RRRR sampling adapter are checked.
Arithmetic qualification covers finite consumed inputs and nonzero used
projection W. Zero-length normals use the audited legacy zero-product rule,
with independent normalization and final-output probes. Unused constant lanes
are poisoned in GPU fixtures. Other original NaN/denormal handling and exact physical-console rounding are not established
by these native GPU tests or the offline transcription.
