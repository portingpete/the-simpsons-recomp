# Second original registration table: 24 game-specific effects

All 24 rows of `82CD1448` are cataloged offline. The `post_effect` filename means
the second table; these effects are not all postprocessing effects. The first
row is **`simpsons_skin`, envelope `82006348`, body `82006354`**. This matches
main's reported next source boundary after build152's first-25 integration.
No second-table runtime creation or rendering is claimed here.

The complete [JSON](../analysis/native-post-effect-catalog.json) records every
row/name/source identity, envelope and body SHA256, shader record/header/code
identity, technique/pass association, literal scalar/sampler state, recursive
private/shared descriptor, serialized handle, default word, and bounded opaque
interval. Shader instructions are not decoded or executed.

The new [analyzer](../tools/analyze_post_effect_catalog.py) imports the existing
`tools/analyze_effect_catalog.py` without modifying or monkey-patching it.
Twenty-three new rows produce identical metadata with the existing parser.
Row4 requires bounded context-tail framing; its additional semantics remain
explicitly unqualified. All first-25 metadata trees are also compared for exact
equivalence with the unchanged parser.

## Source and registration authority

The pinned original flat PE is `analysis/simpsons.pe`, VA base `82000000`,
15,466,496 bytes, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
The second table contains 24 four-word rows, 384 bytes, SHA256
`94f5100ca31a244de3d1a64a203fdcf191a395a80e3a868fb288826a1b8ad324`.
Every original output word is zero and every callback is nonzero.

Original `823C7080` loads r4=24 at `823C7090`, constructs r3=`82CD1448` at
`823C708C/7094`, and calls `827019E8` at **`823C7098`**. First-table caller
`82701B70` instead loads r4=25 and r3=`82CEFD20`, then calls the same registrar
at **`82701B8C`**. Both complete caller bodies and the registrar have original
`.pdata` extents and word-checked disassembly in the evidence directory.

FX lengths come from the exact `{A3D70141,body_bytes,copy_prefix_bytes}`
envelope, not the following blob address. The source image, registration table,
each complete envelope and each decoded span are pinned. The 24 envelopes do
not overlap and total **372,880 bytes**.

Addresses, handles and unprefixed sizes in the following tables are hexadecimal;
ordinary prose counts and sizes labeled bytes are decimal. JSON offsets and
sizes are numeric byte values, relative to the body unless explicitly a VA.

| Row | Original name | Envelope VA | Envelope bytes (hex) | Callback |
|---:|---|---|---|---|
| 0 | simpsons_skin | 82006348 | 6970 | 823CA960 |
| 1 | simpsons_rigid | 8200CCB8 | 2EE0 | 823CA6B0 |
| 2 | simpsons_edge | 8202DF98 | 1AE0 | 823C8D90 |
| 3 | simpsons_aa | 8202FA78 | 1870 | 823C7FB8 |
| 4 | simpsons_edgeAA | 82034008 | 2440 | 823C96A0 |
| 5 | simpsons_aa_row | 820312E8 | 1690 | 823C89A8 |
| 6 | simpsons_aa_col | 82032978 | 1690 | 823C8698 |
| 7 | simpsons_skin_textured | 8200FB98 | 6D60 | 823CA960 |
| 8 | simpsons_rigid_textured | 820168F8 | 3090 | 823CA6B0 |
| 9 | simpsons_skin_dualtextured | 8201CD48 | 7110 | 823CA960 |
| 10 | simpsons_rigid_dualtextured | 8202AD78 | 3220 | 823CA6B0 |
| 11 | simpsons_rigid_gloss | 82019988 | 33C0 | 823CA6B0 |
| 12 | simpsons_skin_gloss | 82023E58 | 6F20 | 823CA960 |
| 13 | simpsons_sky | 82036448 | 2DC0 | 823CA6B0 |
| 14 | simpsons_flipbook | 82039208 | 2E00 | 823CA6B0 |
| 15 | simpsons_skin_flipbook | 8203C008 | 6F50 | 823CA960 |
| 16 | simpsons_uv | 82042F58 | 3690 | 823CA6B0 |
| 17 | simpsons_skin_dualtextured_uv | 8204A058 | 77B0 | 823CA960 |
| 18 | simpsons_rigid_dualtextured_uv | 820465E8 | 3A70 | 823CA6B0 |
| 19 | simpsons_projtex | 82051808 | 2FE0 | 823CA6B0 |
| 20 | simpsons_rigid_multitone | 820547E8 | 3620 | 823CA6B0 |
| 21 | simpsons_rigid_normalmap | 82057E08 | 3A40 | 823CA6B0 |
| 22 | simpsons_vfx_rigid_textured | 8205B848 | 1A90 | 823CABE8 |
| 23 | simpsons_chocolate | 8205D2D8 | 4100 | 823CA6B0 |

## Metadata findings

There are **42 techniques and 42 passes**, each technique with one pass, and
**84 shader occurrences / 84 distinct complete shader identities**: 42 vertex
and 42 pixel. Every pass references non-sentinel shaders of both stages.
None of these complete stage/length/SHA256 identities occurs in the first 25.
This does not assert that every shader's instruction code differs; complete
records include metadata as well as code. Code-only hashes are retained too.

All scalar and sampler resource blocks have zero parameter-driven category
counts. There are **93 scalar and 486 sampler state pass references**. These
literal blocks do not qualify the opaque `edgeAA` context tail or establish
native state mappings. The JSON keeps original numeric state IDs/values and
first-pass cache geometry separately from any future native application.

The private namespaces contain **399 top-level names and 954 leaves**. All
descriptor words, recursive children, storage indices, logical dimensions,
annotations counts, exact default intervals and words are present in each
row's `parameters.private`. Auxiliary annotation/reflection payload semantics
remain outside the existing decoder's qualification; known targets and opaque
intervals are bounded and hashed, not silently discarded.

### Shared pool: reuse of the established profile, no new shared names

There are **229 shared name/leaf occurrences**. Rows2–6 have the four-name
profile; the other 19 rows have the eleven-name profile. Each named descriptor,
serialized handle, storage index and default-byte identity matches the
`littextured` reference, including the exact first-four prefix for rows2–6.
No new shared name or descriptor/default conflict was found.

The eleven names remain `g_ViewProjection`, `g_WorldEyePosition`, `g_UTransform`,
`g_VTransform`, `kWorldToViewPortTfmLight`, `kWorldToViewPortTfmCharLight`,
`kShadowDepthSampler`, `kShadowCharDepthSampler`, `kShadowEdgeSampler`,
`kShadowAmt`, and `kIsShadowReceiver`. The first four serialized handles are
`00040001`, `00080003`, `000C0005`, `00100007`.

The eleven-name profile has 12 descriptor slots including sentinel, 96
descriptor bytes and 320 default bytes. Its serialized packed-name block is
209 bytes: the eleven terminated names consume 208 bytes followed by one
additional zero. The four-name profile has five slots, 40 descriptor bytes,
112 default bytes, and 63 packed-name bytes, of which the names consume 62.
These match the first table; the 208-byte owned-name count from the original
pool initializer is not a contradictory 209-byte serialized allocation.

This source comparison supports **reusing a validated, already populated
common-11 CPU pool** without introducing new namespace/storage or inventing a
host pool merger. Runtime reuse must preserve actual live parameter values,
handle identity, ownership and pool counts. This sidecar did not invoke or
qualify the general `82C18530` merge against a nonempty pool. **Never rerun
`82C181E8` on that nonempty pool**: the earlier initializer qualification is
for initial empty metadata and would replace backing.

## Exact next row: simpsons_skin

Envelope `82006348`, body `82006354`, 26,992 envelope bytes (`6970`), body
length `6964`, copy prefix `5100`. Envelope SHA256:
`7bdd4625fb9e0cb3348d90935e98fc8576e2395eb49e65f050f7fb764bb9d9a9`.
Body SHA256:
`2c9760125b80242f1609a156da04c9c8d25d80cbd5527830c63073d2c96f5793`.
Private metadata has 16 names, 91 descriptor slots, 86 leaves, 4,592 default
bytes; the shared metadata uses the common eleven-name profile.

Techniques `skin` / `skinalpha` have handles `0003FFFC` / `0007FFFC`, each
with pass `p0` and pass handles `0003FFFE` / `0007FFFE`. Both reference scalar
states `(SDK ID28,value1)` and `(ID30,value1)`. `skin` references an empty
sampler block; `skinalpha` references stage0 states `(0,0),(4,0),(8,0)`,
`(10,1),(14,1),(18,1)`. Empty original state lists are not invented defaults.

| Association | Shader VA | Bytes | Complete record SHA256 |
|---|---|---:|---|
| skin VS | 82007C1C | 4604 | 13a58b272067edcd276667f284ad4582c9d3f40089798decb67ba3dbeefcc649 |
| skin PS | 8200A02C | 1136 | b84c92c790a8dffa2a8c051dee9bb2b39238d6e5a270cfcb06f707f240bfa91c |
| skinalpha VS | 82008E20 | 4604 | 11f2f256c463e01a385b05e91a97b8d393680e62154e17f4abcb082753f8a17c |
| skinalpha PS | 8200A4A4 | 720 | a8bcfe5c5bea6cbbb2e1f3cd3b0d98827241b86b448dbf390e4fa1e12c4aeca4 |

## Row4 context extension: bounded framing, unknown application

`simpsons_edgeAA` is envelope `82034008`, body F=`82034014`, length `2440`,
body length `2434`, copy prefix `1DA0`. It has one `aa` technique and one `p0`
pass. Its context array starts F+`1DA0`, count2, total `590` bytes.

Original `82C16E50` computes the aligned base from F+120/+124 and leaf counts
F+130/+134, then adds **BE32[context+54]** at `82C16EA0/EA8`. In this row the
base is `2C0`. Original relocator `82C17568` separately relocates context+50 at
`82C17AC0..ADC`, calls the size helper at `82C17AE0`, and advances by its
return at `82C17AEC`.

- Context0: F+`1DA0`, base and total `2C0`, +50=`2060`, +54=0.
- Context1: F+`2060`, base `2C0`, total `2D0`, +50=`2320`, +54=`10`.
- Context1's tail is F+`2320` / VA **`82036334`**, 16 bytes, words
  `0000000A 00000000 00000001 00000000`, SHA256
  `3d8db406c401b3963b808987aa74e8e6bff17b2bfb6011c4815e4468d6fcbe7c`.

Thus +50 points to the base end/tail start, not the next context at F+`2330`.
The unchanged parser expects +50 to equal the full stride end and then rejects
all nonzero extensions; it correctly remains unsupported there. The new parser
checks the proven base geometry and array bounds, admits only observed lengths
0 or16, and records the tail separately. **It does not treat the four words as
states, constants, descriptors, shader instructions or no-ops.** Application,
copy/merge behavior and rendering involving the tail require more original
consumer evidence. A tail-content mutation changes its identity without
inventing a semantic rejection rule.

## Typed callbacks, finalizers and declaration lifetime

There are eight distinct callback/finalizer pairs. Associations are proven by
each constructor's actual `stw r11,0(r31)` constant and vtable+C; destructor
addresses come from vtable+0. No association is inferred from effect names.

| Rows | Callback | Vtable | Finalizer | Queries after common helper |
|---|---|---|---|---|
| 0,7,9,12,15,17 | 823CA960 | 82061714 | 823CAA98 | skin, skinalpha, g_BlendMatrices |
| 1,8,10,11,13,14,16,18,19,20,21,23 | 823CA6B0 | 820616C0 | 823CA7C0 | rigid, rigidalpha |
| 2 | 823C8D90 | 820614E4 | 823C8F68 | edge plus 11 parameter queries |
| 3 | 823C7FB8 | 82061430 | 823C8290 | aa plus four parameter queries |
| 4 | 823C96A0 | 82061598 | 823C9868 | aa plus 17 parameter queries |
| 5 | 823C89A8 | 820614BC | 823C8A30 | aa plus two parameter queries |
| 6 | 823C8698 | 82061494 | 823C8720 | aa plus two parameter queries |
| 22 | 823CABE8 | 82061758 | 823CACB8 | rigid |

All eight finalizers call **`826B74C8`**, which reaches **`826B5168` at
`826B7540`**. The first common raw SDK read is `826B517C`, FX+2AC, followed
by FX+20C at `826B5180`. Therefore the two query-entry hooks alone are
insufficient for every one of these rows, just as for the non-fourtap families
in the earlier [finalizer proof](native-effect-finalizers.md).

Beyond that shared helper, the eight bodies have 47 query call sites and 47
CPU publication stores. A bounded instruction-shape audit admits only loads
of typed CPU object +8/+1C, stack saves/restores, constant query arguments,
calls to the common helper/query APIs, and stores to typed +A8/+AC/+B0/+B4 or
CPU globals `82D098FC..82D0999C`. There are no additional inline FX-memory
reads/setters or indirect calls in these bodies. This conclusion explicitly
excludes the unsafe common helper and the later begin/apply/draw methods.

All queried names are listed with original string SHA256 and call PC.
`simpsons_edge` queries **`FakeLighting`** and **`FakeLightingThreshold`**, but
neither is present in that row's serialized private/shared namespace. Preserve
the original missing-name query result; do not invent descriptors or handles.
All other finalizer queries resolve to serialized names in their corresponding
rows. This does not prove that any later use of a missing handle is harmless.

The six skin rows, twelve rigid rows and one VFX row allocate their typed CPU
objects and call the existing base `826B4F60`. Rows2–6 additionally use base
`823CA338`, which conditionally creates a shared declaration:

- `823CA364/36C` checks global **`82D099A0`**; if empty, `823CA378` calls
  **`824458E0`** with source **`82061674`**, storing the result at `823CA37C`.
- The original source is 36 bytes: two 12-byte entries and a terminator.
  Decoded words are `00000000 002C23A5 00000000`,
  `00000008 002C23A5 00050000`, and `00FF0000 FFFFFFFF 00000000`.
  SHA256 `0433df1ef161551bfd46cb5edf8e33da819182bb79db90bd1ee610ffe757f473`.
- Those five typed destructors call base **`823CA3A8`**, which loads the
  shared global, calls **`82441708` at `823CA3DC`** if nonzero, clears it at
  `823CA3E4`, then calls existing typed CPU cleanup `826B3E50`.

This is one cached declaration shared by that family, with an explicit
create/release pair. Native identity, retained CPU source, concurrent owner
behavior and cleanup must be tested at those actual call sites. No new D3D
input layout, stream expansion, draw binding or safe global SDK interception
is implied by these source bytes. The new catalog does not qualify those
constructors solely because their allocations are mostly CPU work.

## Reproduction, checks and remaining limits

```powershell
python -B tools/analyze_post_effect_catalog.py --disassemble
python -B tools/analyze_post_effect_catalog.py --check --disassemble
```

The first command writes only `analysis/native-post-effect-catalog.json` and
`build/post-effect-catalog/*`. The second is read-only: it regenerates and
compares the complete catalog, saved checks and decoded CPU evidence. The
disassembler is the existing `build/generator-ninja/SimpsonsDisasm.exe` and
every emitted PC/word is compared with the original image. No build or
original-game execution occurs. Without `--disassemble`, Python's standard
library and the two unchanged local analysis libraries are sufficient.

**21 offline checks passed**, including 18 rejection mutations/negative
cases, a shared-default conflict, a changed opaque-tail identity, and exact
first-25 parser equivalence. Negative cases exercise image/envelope identity,
resource counts, shader stage and entry links, parameter-driven state category,
descriptor sentinel, shared pointer bounds, tail lengths/base-end links,
unchanged-parser rejection, caller count, vtable/callback association,
declaration bytes and an injected inline read through an FX identity.
The CPU evidence covers **34 spans, 1,859 original words and 26 explicit
semantic word pins**, with 47 query-name/publication associations.

Remaining work is explicit: qualify the nonzero context tail's consumers;
implement or guard the existing common reflection reader; own the new cached
declaration at both actual call sites; integrate and test all 24 rows with the
real populated pool and later cleanup; translate/compile shaders and prove
actual streams, constants, state, draw and viewport behavior separately.
Auxiliary payload semantics and full global declaration lifetime are not
settled by this static catalog.

Changed paths for this task are only the new analyzer, JSON, this document and
`build/post-effect-catalog/*`. Existing analyzer/runtime/renderer/tests/CMake,
original assets and earlier frozen evidence were not edited. Main's build152
checkpoint excludes these pending catalog artifacts. The separately authorized
writable-texture document update records its 306+306 checks and the 57/57
build152 suite without changing backend or fixture source.
