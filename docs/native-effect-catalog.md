# Original registration-loop FX metadata catalog

The offline catalog covers **all 25 rows of `82CEFD20` in registration order**.
It verifies the original image, each row/name and exact FX envelope, resource
arrays, technique/pass links, literal states, recursive private/shared parameter
descriptors, explicit default storage and pass-to-shader associations. The table,
original image, runtime, configuration and CMake files remain unchanged.

This extends the original evidence in `native-first-effect-contract.md` and
`native-graphics-startup-services.md`; their first-effect-only scope is historical.
Main reports build146/boot089 reaching row1 after the first native FX and its two
D3D11 shaders. That integration result belongs to main. This catalog executes no
SDK code, decodes no shader instructions and establishes no rendering readiness.

## Reproduce and inspect

```powershell
python -B tools/analyze_effect_catalog.py
python -B tools/analyze_effect_catalog.py --check
python -B tools/analyze_effect_catalog.py --disassemble
python -B build/effect-catalog/verify_catalog.py
```

The first command regenerates `analysis/native-effect-catalog.json` after its
self-tests. `--check` performs the same checks and compares the complete generated
JSON without writing. `--disassemble` additionally writes original-word-checked
CPU instruction evidence to `build/effect-catalog/resource-code-evidence.json`
and `resource-code-disassembly.txt`. The optional disassembler is the existing
`build/generator-ninja/SimpsonsDisasm.exe`. Normal catalog generation uses only
Python's standard library and the original image.
The last command compares the finished catalog against the independent parameter
decoder and existing first-effect verifier, writing only
`build/effect-catalog/crosscheck.json`.

Independent parameter evidence is in `build/effect-catalog/parameter-evidence.md`,
`parameter-evidence.json`, `parameter-evidence-disassembly.txt`, and
`parameter-evidence-checks.json`. The independent collector did not import this
catalog parser. These artifacts contain decoded metadata and CPU evidence only;
no PE, FX blob, shader binary, texture or other original asset file is exported.
Default values are reported as exact decoded BE words, including padding, not
converted indiscriminately to floats.

Original source: `analysis/simpsons.pe`, flat VA base `82000000`, **15,466,496
bytes**, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
The 400-byte registration table has SHA256
`434b529f6bbfebd448fd57400833d090add3c5b868bdd1fc42b503b5bbca1063`.
Every envelope address, exact byte length and SHA256 is also pinned independently
in the analyzer's `PROFILES` list. Neither a following address nor a pattern scan
defines an FX or shader length.

Addresses and offset expressions in this document are hexadecimal. Counts and
sizes explicitly followed by “bytes”, and ordinary counts in prose, are decimal.
In JSON, offsets/sizes/counts are numeric decimal byte values; VA/handle/decoded
word strings are explicitly `0x`-prefixed. JSON `offset` is relative to the FX
body, except the top-level registration-table offset, which is image-relative.
Every `va` is an absolute original VA. End offsets are exclusive.

## Complete corpus findings

- **612,712 bytes** across 25 nonoverlapping FX envelopes.
- **68 techniques and 68 passes**. Every technique in this corpus has one pass.
  `quad` has 17 techniques; `shadows` has eight. All technique/pass names, raw
  unclassified words, handles and links are in each row's `techniques` list.
- **131 shader occurrences and 131 distinct complete shader identities:** 68
  vertex and 63 pixel. Identity means stage, complete record length and SHA256;
  records with different metadata remain different identities even if some code
  bytes match. Resource-array null sentinels are excluded from that count.
- **All 25 effects are literal-only.** Every scalar/sampler block, including
  blocks reached through multiple passes, has zero integer-parameter and
  float-parameter category counts. There are **229 scalar and 1,101 sampler
  state pass references**, not that many unique blocks. No effect needs
  default-resolved parameter state evaluation for this registration cache.
- **320 private top-level names**, **1,472 private leaves**, and **218 shared
  name/leaf occurrences**. The shared occurrences resolve to **11 distinct
  names**, with no conflicting serialized descriptor/default identities.
- Only `fourtapblend` and `quad` have no shared named parameters. Five effects
  have the four-name shared profile: `particles`, `skinned_unlit`,
  `unlittextured`, `mono`, `zprepass`. The other 18 have the identical eleven-name
  profile. The four-name profile exactly matches the first four parameters of
  the eleven-name profile, including explicit storage indices and defaults.

Registration order is:

1. `fourtapblend`, `littextured`, `particles`, `quad`, `shadows`.
2. `skinned_lit`, `skinned_lit_tint_color`, `skinned_bruise`, `skinned_spec`, `skinned_unlit`.
3. `unlittextured`, `terrain`, `nrmmapnoskin`, `road`, `base_detail`.
4. `sky`, `simple`, `simplelit`, `water`, `mono`.
5. `skinned_bruisenospec`, `standardworld`, `dualtexture`, `carnrmmap`, `zprepass`.

These grouped lists describe rows0..24; they are not a replacement registration
table. Row1 is **`littextured` at `820D5730`**. The physically adjacent blob after
row0 is **row2 `particles` at `820B9750`**. The catalog preserves row/name/callback
identities and separately reports physical successors/gaps. `particles` retains
its original zero typed callback. No production loop count is shortened.

Three shadow passes (`RenderShadowDepth`, `RenderShadowSecondDepth`,
`RenderFirstDepth`) and both `zprepass` passes select the pixel-array null
sentinel. These are five explicit null shader associations, not missing parser
results or permission to substitute a pixel shader.

## Stable next-row contract: littextured

Envelope B=`820D5730`, body F=B+C=`820D573C`. Envelope length `47B0`
(18,352 bytes), body length `47A4`, copy prefix `3A20`, SHA256
`38972d41a498c52988afc803454fb6830086e3ec8a3cac966ad2a3705ee63fd9`.
There are three contexts of `390` bytes: the initial context and two pass
contexts. Their array ends at F+`44D0`; **the body continues to `47A4`** with
shared metadata. The first-effect invariant “context end equals blob end” must
not be generalized to shared profiles.

Private descriptor base F+`390`, 39 slots including sentinel, 32 leaves,
10 top-level names. Private defaults F+`4D0`, **656 bytes (`290`)**.
Top-level name → serialized handle:

- `g_AmbientColor` → `00040000`; `g_WorldI` → `00080002`; `g_World` → `000C0004`.
- `g_tintColor` → `00100006`; `g_tintColorIndex` → `0024000E`.
- `g_WorldLightParameters` → `00280010`; `g_SpecularExponent` → `008C0038`.
- `fog_color` → `0090003A`; `fog_params` → `0094003C`; `TextureSampler` → `0098003E`.

Shared descriptor base is **BE32[F+BE32[F+10C]]**, F+`4500` in the serialized
image. Shared default base is **BE32[F+BE32[F+12C]]**, F+`4560`.
There are 12 descriptor slots including sentinel, 11 leaves/names and **320
default bytes (`140`)**. Shared top-level name → local serialized handle:

- `g_ViewProjection` → `00040001`; `g_WorldEyePosition` → `00080003`.
- `g_UTransform` → `000C0005`; `g_VTransform` → `00100007`.
- `kWorldToViewPortTfmLight` → `00140009`; `kWorldToViewPortTfmCharLight` → `0018000B`.
- `kShadowDepthSampler` → `001C000D`; `kShadowCharDepthSampler` → `0020000F`.
- `kShadowEdgeSampler` → `00240011`; `kShadowAmt` → `00280013`; `kIsShadowReceiver` → `002C0015`.

`TechniqueOpaque` / `PassOpaque`: handles `0003FFFC` / `0003FFFE`;
one scalar state `(SDK ID 3C, value0)`.
`TechniqueAlpha` / `PassAlpha`: handles `0007FFFC` / `0007FFFE`;
five scalar states `(30,0),(3C,1),(48,6),(4C,7),(68,4)`.
Both passes use six sampler states, all stage0:
`(0,0),(4,0),(8,0),(10,1),(14,1),(18,1)`.
These are stored original numeric IDs/enums. Apply must use separately qualified
native state mappings; the offline default application-selector numbers are not
authority for current live remapping-table contents.

The original cache therefore has two 24-byte technique rows, six 12-byte scalar
rows and twelve 16-byte sampler rows: **312 bytes (`138`)**. Reflection leaves
saved-previous-value words unwritten. The original engine selects the first pass
of each technique when constructing this cache (`826B4828`); the parser separately
lists every declared pass rather than confusing techniques with passes.

Four exact associated shader identities:

- Opaque VS `820D5F04`, 1,008 bytes, SHA256
  `dfd6a6f5e6dac19f3ea667da6a3bf3c80cec6b16a769d7673480bb96cccc70c2`.
- Alpha VS `820D62FC`, 1,008 bytes, SHA256
  `3079378a828929d8cc2352f80c1e25c185f2e0cb0722100454d6e0a664ae2af7`.
- Opaque PS `820D66FC`, 4,352 bytes, SHA256
  `02630c1cb7f1f6f73a2324d3a953db5cade5c27b066c31945f62efefa59fdc0e`.
- Alpha PS `820D7804`, 4,352 bytes, SHA256
  `db01932ab6d1b33c0dba9aae6df26d8ad42f09b72bc940a85073ad1de896dfa9`.

Both VS headers are 656 bytes with a 64-byte payload prefix and a 288-byte code
window. Both PS headers are 1,096 bytes with a 64-byte prefix and a 3,192-byte
code window. These numbers describe framing only. Metadata-ready ownership
with deferred compilation is a valid capability; none of these four shaders has
been translated or compiled by this sidecar.

## Decoder rules and original instruction authority

The envelope is `{A3D70141,body_bytes,copy_prefix_bytes}` followed by exactly
`body_bytes`. The original no-pool copy in `82C1D274..278` copies from B+C using
BE32[B+4]. Pointers are body-relative. `FFFFFFFF` is the serialized null sentinel;
zero is not a general null. End links may equal the end of their specific span,
but that does not make them readable objects.

`82C17568` proves the relocated root fields, technique pass-pointer lists,
20-byte pass stride, context links and the separately indirect shared bases.
Technique records are **16 bytes plus four bytes per pass pointer**, not an
assumed array of 20-byte technique records. Passes themselves are contiguous
20-byte records. Technique and pass handles are `(index<<18)|3FFFC` and
`(index<<18)|3FFFE`, respectively.

Shader arrays start with one 8-byte zero sentinel. `82C19498` iterates the other
entries by `8+BE32[entry+4]`. The record begins at entry+8; stage is established
both by `102A1101/102A1100` and the original VS/PS creation paths. A record's
header length plus payload length must equal its exact entry length. Header
metadata offsets are checked within the header; code metadata defines payload
prefix plus code length, with no shader instruction interpretation. Pass context
+48/+4C must reference actual entry starts in the corresponding stage array.

Scalar blocks have category counts at +10/+14/+18 and 8-byte state rows at +1C.
Sampler blocks have counts at +80/+84/+88 and 8-byte rows at +8C, each encoded as
BE16 stage, BE16 SDK ID, BE32 value. Original `82C18F38..64` and
`82C1908C..B8` advance to aligned next blocks using header sizes20/90 plus eight
bytes per summed count. Block0 is the full zero sentinel. Every block must fit
its resource-array byte count, and the walk must consume the array exactly.
Unknown parameter categories are rejected distinctly; defaults are not guessed
into those paths. Original `826B2B88`/`826B2C48` are the literal-query authority.

`82C16E50` sizes contexts from `58 + 40*(F120+F124)`, followed by the aligned
private and shared caches of `10*F130` / `10*F134` bytes, a final align16, and
context+54 extension bytes. Every observed extension is zero. The parser checks
all sixteen bookkeeping links, both vector-cache spans, both shader links and
the end link against this geometry. These context cache sizes are **not** the
parameter-default block sizes.

Private parameters use F+108 descriptors, F+128 default bytes, F+288 packed
name area and F+298 per-descriptor name pointers. Shared equivalents use an
extra indirection through F+10C/+12C/+290/+29C. The full private name area also
contains technique/pass/compiler/annotation strings; only the first F+110 strings
are top-level parameter names. Shared top-level count is F+114. The per-descriptor
name map includes descendants and is checked independently against the packed
root names. Root lookup `823C7B20` searches private first, then shared, returns
zero on miss and does not invent dotted or bracketed member-name lookup.

For descriptor words w0/w1:

- `kind=w0&3`; kind0 is a leaf, kind1/2 are the observed structure/array
  containers. Nonleaves span `low16(w1)` descriptor slots **including themselves**.
  Their immediate child count is `(w0>>2)&3FFF`. Children advance by each child's
  own subtree span, not a fixed leaf stride. Kind3 is unrecognized.
- Handle is `(descriptor_index<<18)|(preceding_leaf_count<<1)|shared_bit`.
  Count preceding leaves only; a matrix and sampler each count as one leaf.
- A leaf's storage address is `default_base + 10*low16(w1)`. A container has no
  storage-slot field; its low halfword is its subtree size. Logical bytes are
  `4*high16(w1)`, which need not equal its allocation footprint.
- F+130/+134 count leaves. Preserve complete storage lengths F+138/+13C.
  Numeric rows occupy 16 bytes each. A four-row/three-column matrix therefore
  has 48 logical bytes and 64 stored bytes. The corpus contains 64-matrix arrays.
- Some sampler/object defaults occupy 64-byte observed intervals while reporting
  zero logical bytes. The catalog reports their actual interval up to the next
  explicit leaf offset or block end. It does not claim a universal object-size
  rule from that observation. No original default lane/padding is discarded.

`823C7BCC..7C58`, `823C7968`, `823C7908`, `826B2A28`, `823C7D18`, and
`823C7E38` establish those query/storage fields. Recursive child walking is
corroborated by `82C17CD4..17DAC`; row-sized storage accounting/copying is at
`82C18058..68` and `82C18790..CC`. Full instruction hashes and labelled
leaf/window/.pdata extents are in the evidence artifacts.

The parser also reports exact `826B2A28` annotation counts: if F+264 is zero,
zero; otherwise private `w0>>21`, shared BE16[F+BE32[F+284]+2*descriptor_index].
Annotation payload query semantics remain unimplemented. A checked count must
not be presented as a working general annotation API.

Do not demand eight-byte alignment merely because descriptors have an eight-byte
stride: some shared tables are only four-byte aligned. The F+2A0 relocated
auxiliary pointer table can be only **two-byte aligned**, including F+AAE in
fourtapblend. `82C17BF4..C14` still walks four-byte entries. Its decoder therefore
uses bounded packed BE reads instead of imposing an invented aligned-word rule.

## Smallest faithful native metadata/reflection slice

1. Admit these exact table rows/envelopes by original identity. Own immutable
   metadata and shader records, with checked indices instead of relocated SDK
   pointers. The existing native shader registry can acquire real uncompiled
   ownership; the FX owner must preserve the exact stage/record/pass association
   and the null-sentinel cases. Compilation capability stays separate.
2. Parse one common variable-length technique/pass model, all literal state
   blocks, and recursive parameter trees. Own full explicit private default
   blocks. Implement the reviewed name/descriptor/technique/pass/state queries
   together with creation; do not send a native owner ID to SDK-layout readers.
   Parameter child handles must be derived from descriptor and leaf ordinals.
3. Keep a native shared store associated with the genuine original CPU pool
   root. This corpus needs eleven compatible shared names and a four-name subset.
   Row1 is the first nonempty shared profile. Validate/adopt existing native
   entries by descriptor compatibility, and preserve already modified values
   when another effect joins. Do not reinitialize all pool values on each effect
   creation or keep independent per-effect copies pretending to share.
4. Treat serialized local shared handles as evidence for local namespaces, not
   proof of arbitrary live merged-pool order. Native root lookups, FX shared
   lookups, writes, lifetime and aliases must resolve to the same native store.
   Main's full merge/global lookup probe covers `82C17DC0`, `82C18530`,
   `82C1D0C0`, `826B2528`; this sidecar does not substitute its local equality
   checks for that integration proof. Retain the real CPU root and its genuine
   provenance. Native leases and original SDK counts remain distinct under
   main's selected policy; all 25 effects require a lease when the root is nonnull.
5. Preserve the original wrapper/name/tree/typed-object allocations, row callbacks,
   publication order and full 25-row loop. Construct the actual original CPU
   cache from current checked remapping tables, preserving allocator behavior
   and untouched saved-value words. Pair native acquisition and cache creation
   with rollback on failure and the real ordered typed/wrapper destruction.
6. Qualify subsequent typed finalizers and query callers separately. Keep apply,
   commit, shader bind, draw and unported declaration paths guarded until their
   actual contracts are implemented. Loading/reflection of these 25 resources
   must never be advertised as successful whole graphics startup or renderability.

Because every state block is literal-only, adding generic parameter-state
evaluation is unnecessary for this milestone. In particular, the inspected
original parameter-state query branches do not justify an invented general
parameter-index-to-default-slot conversion. The analyzer rejects those branches
as `unrecognized_framing`, even if supplied with otherwise bounded test data.

## Validation and explicit limits

**17 test cases pass**, including 125 resource-boundary truncations (five arrays
for each original effect), changed envelope/version/identity, technique/pass and
shader links into record interiors, count/stride overrun, malformed nested
descriptor children, matrix/default storage, shared-cell/name bounds, packed
unaligned auxiliary links, unknown descriptor kinds and state categories, and
valid-looking default/opaque-byte changes rejected by the pinned profile check.
The independent parameter evidence adds 87 checks, 1,572 numeric-size checks and
122 container-size checks. The optional CPU evidence export verifies eight
bounded spans word-for-word against the original image; parameter evidence
verifies eighteen more. Repeated `--check` confirms deterministic catalog output.
Cross-checking both independently generated reports passes for all 25 rows,
50 namespaces, 1,860 descriptor words/names, 1,812 handles and 1,690 leaf-default
hashes/extents. Four first-effect identity/cache/query/shader comparisons agree
with the existing first-effect verifier. The JSON field `has_shared_parameters`
describes names only; `pool_lifetime_requirement` separately preserves the
nonnull-root lease requirement for rows0/3 as well as the other 23 rows.

Failures are differentiated: `bounds` for invalid reads/spans, `malformed` for
contradictory known structure, `unrecognized_framing` for unsupported shape, and
`identity` for bytes differing from the admitted original. The production CLI
requires the exact original image before any catalog generation. Structural
mutation tests exercise `inspect_blob` before identity checking so a SHA failure
cannot conceal a missing bounds check. `inspect_profile` adds exact per-row
length/hash admission. No nonoriginal input is silently cataloged as supported.

`checked_spans` records bounds and SHA256 for every consumed resource/table/default
span. Shader debug/reflection/constants payload internals, auxiliary annotations
and lookup grammars, state bitmap interpretation and unclassified header words
are **opaque**, with explicit known target/span checks and complete envelope
identity. `opaque_uninterpreted_body_intervals` hashes the remaining byte gaps;
it is not a list of corrupt framing. `unrecognized_framing` is empty for these
25 admitted originals. This does not claim every byte has a semantic decoder.

Every requested FX/shader/resource envelope and consumed descriptor/link/default
access is bounds checked. Recursive internal grammars of opaque auxiliary/shader
metadata are not certified safe for later interpretation; native code must gate
those queries or add a bounded decoder before consuming them. Object/sampler
default padding is retained and bounded but not assigned invented semantics.

No shader arithmetic, GPU packing equivalence, native compile result, live pool
contents, arbitrary pool merge order, full typed-caller closure, device binding,
draw readiness or original rendered frame is proved here. Those remain explicit
integration work, with main owning the native AOT/D3D11 implementation.
