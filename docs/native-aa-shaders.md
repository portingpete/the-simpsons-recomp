# Original simpsons_aa pass

This is `simpsons_aa` at8202FA78, distinct from `simpsons_edgeAA` at82034008.
Boot195 consumed Start, reached SavedGames, selected an empty slot, completed
the edge effect's real parameter commit and rectangle, and copied that output
to the second scene texture. It then stopped at this AA activation, caller
823CA5B4, typedE1AA9780, nativeid0050001D. No main menu was reached.

The immutable6256-byte effect SHA256 is
`1e43e441085ff58eb8f74f2224350e538f082184fb5b9f57250f325b4db7831b`;
its6244-byte body at8202FA84 hashes to
`d43160e340c2ca690a7c21d99d43ccabf63de624430091b3f6ffd4c76385fada`.
The original flat image is the same pinned image as the edge evidence.
`tools/analyze_aa_shaders.py` reads only original bytes and pinned declarative
instruction fields. Its report is `analysis/native-aa-shaders.json`;79
identity/control-flow mutation checks pass. It is not an instruction interpreter.

| Record | Bytes | Executable offset/bytes | SHA256 |
|---|---:|---|---|
| VS820301A0 |316|220/96|`cb28bef1f4b0d9ab5603bdbf9c66bef12012731a5589bb3b22a2e004809a4314`|
| PS820302EC |800|656/144|`aa1610b0e2b4ae380482229f0a7e97c11c724d5c3813e5bae56352605eb259c3`|

The vertex program matches the qualified float2 position/float2 UV declaration:
clip Z/W are literal1. The pixel program first samples RGB in BGR register
order. Its integer31=`00010004` repeats four axis-neighbor fetches, start0/step1,
then exports the sum times float32 `3E4CCCCD` (0.2), reversing BGR back to RGB.
Overlapping export W masks supply alpha1. Alpha is not fetched or averaged.
The harmless slot4 has no result masks. No depth export or depth read exists.
All original CF/ALU/fetch fields, loop metadata, literal block and26 parameter
binding rows are recorded and checked. Only12 static issue slots are executed.

The pass's private PS float inputs are width384→c50, height400→c49, and eight
kernel float4s416..528→c20..27. Only the first four kernels are read. Color
texture slot240 binds PS stage0. KernelWidth368 remains mutable original CPU
data but this shader does not read it. TexelKernel4 and other original private
values also remain intact. One technique/pass has four scalar requests, six
sampler requests and168 bytes of original CPU cache.

## Native engine closure

The shared edge integration now admits this second exact profile with its own
typed vtable82061430, shader pair, technique global82D098FC, private152-word
storage, original cache extent, parameter globals and commit frame. Unknown
sources still fail before activation. Real begin/end retain the original
826B35D8/826B37B8 save/apply/restore calls. Compilation does not substitute an
SDK object or make a native ID guest-addressable.

The original setter823C8350 retains its160-byte frame, numeric conversions,
video-mode queries, source-global load and final stores. Pinned cuts replace
only SDK descriptor/storage resolution:

| Cut | Continuation | Retained result |
|---|---|---|
|823C8380|823C83FC|KernelWidth float store, private368|
|823C8404|823C8410|skip SDK loads; retain color-handle load|
|823C8414|823C8450|retain original second-scene source load|
|823C8454|823C8484|source word store, private240|
|823C84C4|823C8530|original width conversion/store, private384|
|823C8548|823C854C|retain height sign extension|
|823C8550|823C855C|retain height-handle load|
|823C8560|823C8564|retain height integer store|
|823C8568|823C85CC|original height conversion/store, private400|

The common826B3980 commit requires AA return address823C85E0, not the edge
setter's different return address. It validates the immutable default kernels,
original second-scene publication, actual copied resource/camera provenance,
original pool and active shader ownership. A real immutable native commit is
tagged edge or AA; it cannot authorize the other shader pair's draw. Actual
constant/sampler/SRV bindings are verified. Original private/shared128-byte
dirty cache lines clear only after this succeeds.

The exact original823CA448 rectangle and full native viewport use the already
qualified edge geometry closure. AA requires replacement output, depth/write/
stencil/alpha-test off, solid/no-cull, no scissor or expanded blending, and a
one-level point/wrap sampler. The second scene image must have received its
real original copy; allocation alone and the first-copy role cannot authorize
AA sampling. Native draw accounting separates `aa_draws` from `edge_draws`.

## Validation scope

Before engine admission, both WARP and hardware passed111 standalone AA draws
with more than682,000 checks per device. Input RGB10A2, RGBA32F output and D32
depth expose errors before output quantization. Independent expected values
compute a five-sample mean in double precision, with a2.4e-7 native float
tolerance, exact alpha1 and exact VS depth1. Cases include asymmetric RGB,
impulses, ramps, binary edges, all two-bit alpha values, wrap/clamp, varied
dimensions, reversed neighbor order, unused kernel entries and unused width.
The existing103-draw edge fixtures passed in the same test run.

The integrated backend fixture additionally compares every packed RGB10A2
output code against the independently rounded integer sum of five input codes.
It tests cross-profile shaders/commits and preserves source and disabled depth.
The original-CPU fixture runs two complete AA begin/setter/draw/end sequences
after an original edge draw and second camera copy, checking ABI, all private
values, immutable assets, output pixels and final inactive ownership.

Validation record: `build/aa-integration-build.log` builds and passes all113
tests in211.45 seconds. The original edge/AA fixture passes4,608,066 checks;
each native backend fixture passes11,306, including packed AA pixel values.
Boot196 then verified actual AA begin/commit/draw through the original game
flow, followed by the original shared color copy. It stopped at the next,
distinct edgeAA activation; AA's actual KernelWidth1.10000002 remained unused.

These establish bounded native behavior. Console reciprocal/fused arithmetic,
filtering, rasterization and output rounding parity remain unproven. The later
`simpsons_edgeAA` effect remains guarded. A submitted effect draw is not proof
of a completed original frame, presentation or the main menu.
