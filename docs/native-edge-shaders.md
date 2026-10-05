# Native simpsons_edge shader transcription

The standalone native shaders in `renderer/edge_shader.hlsl` implement the two
exact original records below. `tools/analyze_edge_shaders.py --verify --self-test`
checks the original image, declarative field references, complete shader records,
control flow, loop/literal constants, and the saved static field/dataflow report
`analysis/native-edge-shaders.json`. This is offline inspection, not an
instruction interpreter or a runtime translator.

The flat original image is based at `82000000`, SHA-256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
The effect is `8202DF98`, body `8202DFA4`; technique `edge` has handle `0003FFFC`
and pass `p0` has handle `0003FFFE`. Original record identities:

| Stage | Address | Record bytes | Executable offset / bytes | SHA-256 |
|---|---|---:|---|---|
| VS | `8202E6F0` | 320 | 224 / 96 | `2d87c7985ec49418be95929538748a6bab56b453b412e610dd696af66fcd72be` |
| PS | `8202E840` | 1116 | 708 / 408 | `e68d07ff5b76c143384bff779c0247be3dba0a935291db119746bfece8a6d18c` |

The VS fetches two float2 inputs and exports `(position.x,position.y,1,1)`
plus UV. The declaration stored by original `823CA338` at `82D099A0` is separately
owned by `EngineQuadDeclarations`: stream0, position offset0, UV offset8, stride16.
Allocation/declaration ownership alone does not authorize a draw.

The PS first samples color0 and classifies its alpha, blue, red and green values.
It then performs four iterations. The loop CF uses register31, start0 and step1;
the original constant metadata word is `00010004`. Each iteration sets the
current predicate from `r1.w == 0`; the remaining iteration writes occur only
while true. A detected edge therefore survives the later iterations. The blue
classification product in `r1.x` also survives between iterations. The final
export writes this edge result to RGB and literal one to alpha. There is no PS
depth export. Co-issued scalar instructions consume the previous scalar result
before writes; they are not independent assignments in arbitrary order.

Exact literal words include `3E570A3D`, `3F000000`, `3EEB851F`, `3F19999A`,
`3F800000` and `38D1B717`. Both fetches use normalized UV, inherited filtering,
computed LOD, no bias or coordinate offset, and `fetch_valid_only=0`. The neighbor
fetch is predicated. The first four kernel entries are cardinal offsets; the
four diagonal entries are uploaded but not read by this loop.

The pass context at body+`1710` has a private PS-float mask of
`0011BFC000000000` and private PS-texture mask `0040000000000000`.
Its 26 binding rows begin at body+`17F0`. Nonzero parameter associations are:

| Private handle | Value slot (bytes) | PS destination |
|---|---:|---|
| `002C0012` | 240 | texture0 |
| `00340016` | 368 | c50.x (LineWidth) |
| `0044001E` | 432 | c49.x (TargetWidth) |
| `00480020` | 448 | c48.x (TargetHeight) |
| `00540024` through `00700032` | 528 through 640, step16 | c20 through c27 |

These are current private values, not shader debug defaults. Each numeric
binding is one complete float4; the shader reads only the documented lanes.
The other effect parameters, including the depth texture and fade flags, are
still original CPU state even though this shader does not sample/use them.

Validation: the offline inspector passes 102 control-flow/identity mutation
checks. `NativeEdgeShaderWARP` and `NativeEdgeShaderHardware` both passed: each
executes 103 draws and checks every color/depth pixel on a 32x32 fixture. Source
textures use actual RGB10A2 storage; output RGBA32F avoids hiding classification
errors through output quantization. Independent expected values use boolean
color classifications rather than a register interpreter. Cases include every
two-bit alpha, blue-band boundaries, repeated loop state, altered line width and
target dimensions, reversed neighbor order, wrap and clamp. The depth fixture
verifies VS Z/W are one.

These tests establish native finite-UNORM, single-level, point-sampling behavior.
They do not establish console reciprocal/fused arithmetic, derivative,
rasterization or filtering precision. The HLSL uses fixed loop unrolling and
flattened predication for this native single-level domain. Runtime edge
activation and paired state restoration are separately implemented and tested
as described in `native-edge-parameters.md`. The added integration binds an
owned copied RGB10A2 source, immutable constant/sampler objects and the original
three-vertex rectangle; its separate backend and original-CPU fixtures cover
packed output, rejected owners/state, and disabled depth preservation. These
services do not admit the subsequent edge-AA effect or establish presentation.
