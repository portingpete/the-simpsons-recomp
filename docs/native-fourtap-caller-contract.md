# First typed FX: caller and application contract

This is a static original-byte contract for the first registered FX. It extends
the frozen `native-first-effect-contract.md` and `fourtap-shaders.md`; neither was
edited by this task. It proves the typed sampler write, begin/apply/end association, private
weight-to-pixel-constant mapping, stage-0 texture binding, and the meaning of
viewport-enable zero. It does not grant native draw readiness.

Only this new document and `build/fourtap-caller/*` are owned by this task. No
runtime, shader, configuration, CMake, original asset, or reference source was
changed. No runtime build, original game, SDK helper, or guest instruction was
executed. The verifier is a bounded static metadata/byte checker, not a CPU or
GPU interpreter. The intended implementation remains native x64 AOT with a
native D3D11 engine boundary.

## Authority and reproduction

Addresses, offsets, sizes and raw values below are hexadecimal unless stated
otherwise. `analysis/simpsons.pe` is the existing **flat memory image**, base
`82000000`, size `EC0000`, SHA-256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Use `file offset = VA - 82000000`, not inherited PE section raw offsets.
The whole image is verified before reading any evidence. The installed offline
`build/generator-ninja/SimpsonsDisasm.exe` independently supplies disassembly;
every displayed PC and BE instruction word is compared with the image.

```powershell
python -B build/fourtap-caller/verify.py
python -B build/fourtap-caller/callers/make_evidence.py --verify
python -B build/fourtap-caller/check_caller_mutations.py
```

This writes `evidence.json` and `disassembly.txt` beside the verifier. The JSON
records exact span SHA-256s, entry byte pins, original `.pdata` records for full
functions, explicitly bounded leaves, direct branch words/targets, context
records and register associations. `probe_original.py` is a read-only inspection
utility whose decoded text output also stays in this directory. Exploration
files are supporting evidence; the verifier and explicit limits below define
the qualified scope.

Final checks passed: **18 application tests**, **27 byte-checked instruction
spans / 3,251 words / 24 direct edges**; the caller verifier checks **29 spans,
29 direct edges, 25 site pins, 14 data spans, five declaration/fetch matches,
48 quad stores and 24 index stores**. These span sets overlap; do not add them
as unique coverage. **10 additional caller checks** cover the original joins
and reject changed declaration offsets/components, usage/index metadata, mask
tables, duplicate vertex stores, index values, draw stride and vertex count.
Application mutations reject wrong pass/shader context, stage/category masks,
register/count/descriptor changes, shared namespace, truncated storage, changed
commit/constant/viewport instructions and unknown original bytes. The
asymmetric slot test preserves unused YZW lanes in all four vectors.

Application evidence/disassembly were byte-identical across consecutive final
runs. The caller's `--verify` recomputes and compares its saved evidence and
disassembly without rewriting them. `caller-mutation-results.json` records
the additional negative checks. Counts are decimal. These are static checks;
the vertex floating-point expressions are reviewed dataflow annotations, not
results of executing the original leaf or testing hardware arithmetic.

Frozen document hashes: `native-first-effect-contract.md` =
`704642bd1ada6a7e91b47cfadcbfe9d554f6f13c0ce517e41a8531b905e51f26`;
`fourtap-shaders.md` =
`409204c2891e4dfdc4b0c52ce1532e35a6f5e84075d4f03cb60bc06502699926`.
The first hash includes main's user-reported two-line historical integration
note added during this task; original evidence/semantics were unchanged.
Local `K:/Simpsons/RexGlueCurrent/include/rex/graphics/registers.h` and
`register_table.inc` supply declarative register field labels only, with hashes
in the evidence. No reference renderer/backend implementation is imported.

## Typed object and manager association

Let `T` be the typed object, `W` its borrowed wrapper, `M=BE32[W+C]`,
`E=BE32[W+10]` the original SDK effect pointer, and `D=BE32[E+2BC]` the
original SDK device pointer. Here `E+offset` means original layout evidence, never an
invitation to read that offset on a native opaque resource ID. Let `F` denote
the serialized body at `820B8AAC`; serialized offsets remain relative to F.

The existing constructor/finalizer proof is reinforced by new pins:
`8273B280` allocates `B8`, runs the base constructor and publishes vtable
`8215036C`. The eight vtable entries are `8273B220,823C7F08,823C7F20,8273B3B8,
823C7F28,823C7F30,823C7F38,823C7F40`. **The three typed rendering helpers are
not entries in this vtable.** `8273B3B8` resolves the registered wrapper by the
original `fourtapblend` name/hash path, calls wrapper virtual+8, and caches:

- `T+A8=W`, `T+AC=E`;
- `T+B0=0003FFFC`, obtained by the `Technique0` query at `8273B420`;
- `T+B4=00180008`, obtained by the `g_Sampler` query at `8273B438`.

No `g_Weights` handle is cached in the typed subclass's four trailing words.
That absence does not disconnect the weights: the serialized pass context has
four explicit child-parameter bindings described below.

## Begin, apply, and end

`8273B2F8` has complete pin `808300B0806300A84BF7AD78`. It loads technique
and wrapper and tail-calls `826B6078`, whose complete pin is
`7C6B1B787C8523787D645B78806B000C4BFFFF38`. This rearranges arguments into
`826B5FC0(M,W,technique)`.

The generic begin compares the pair `(M+4 wrapper,M+C technique)`. If both
already match, it returns without resaving or reapplying the pass. Otherwise:

1. `826B5FF4 ->826B4628` ends the previously active manager effect, if any.
2. It stores the new wrapper, retrieves its effect via wrapper virtual+8,
   and stores `M+8=E` / `M+C=technique`.
3. `826B6018..6038` decodes the technique index, resolves `E+200[index]`,
   clears `E+2B4` and `E+21C`, and stores selected technique/handle at
   `E+204/+208`.
4. `826B6048 ->826B35D8(W+14,technique,0)` selects the original CPU cache
   row, saves prior application-request values and applies the eight scalar
   and five sampler records through the existing application setters.
5. `826B6058 ->826B1F50(E,techniqueRecord+10's pass)` selects the actual
   pass context and binds its two shaders. Calls `826B2074 ->82445578` and
   `826B2084 ->82445278` consume context+48/+4C framed shader entries. Those
   links are the exact VS `820B8F08` / PS `820B90D4`, not name-based matches.

At `826B1FA4..1FEC`, a new pass marks the private parameter dirty bookkeeping
with ones; this effect's `E+120=1` causes one 16-byte all-ones store. Clearing
`E+21C` in generic begin guarantees this path after a manager selection change.
The pass's literal scalar/sampler blocks are also applied directly through SDK
method tables (`826B2238..2250`, `826B2398..23B4`). This is in addition to the
application-cache path. At `826B24AC/B0`, `E+214=pass`, `E+218=passContext`.
**Begin binds shaders/states and marks parameters dirty; the typed sampler
helper's later commit performs the constant and texture application.**

`8273B308` has complete pin `806300A84BF7980C`, tail-calling `826B4B18(W)`.
End moves the last pass to `E+21C`, clears `E+214`, restores the thirteen saved
application values through `826B37B8`, runs `826B2D60`, and clears manager
wrapper/technique. For the reviewed begin/commit path `E+2B4` remains zero;
`826B2D60` therefore skips its optional SDK default-state loops and clears
`E+204/+208`. Neither this path nor the typed end restores the preceding
texture, VS, PS, constants, declaration, stream, render target or viewport
rectangle. The viewport-enable *request* is one of the restored scalar values.
This is not a complete pipeline push/pop.

## Typed sampler writer and commit

`8273B310(T,r4)` is `A4` bytes by original `.pdata`, entry pin
`7D8802A6483010B19421FF80`. The supplied r4 is stored as a resource word;
this function does not read a raster extension, manufacture a texture, query a
name or acquire a new resource reference.

For the finalized handle `00180008`, the handle arithmetic at `8273B32C..388`
selects private descriptor `E+108 +30`. Its second word's low16 is `4`, so
`8273B38C..3A0` selects `E+128 +40`. The same arithmetic selects dirty byte
`E+0`, mask **08**, from the original table `82061428 = 80 40 20 10 08 04 02 01`.
The exact changes are:

```text
byte[E+0] |= 08
word[privateStorage+40] = supplied r4
82C1DBA0(E)                 // BL at8273B3A8, word484E27F9
```

Only the first word of the sampler's 16-byte slot is replaced. Other parameter
slots and sampler YZW are untouched by this writer. It does not write the
weights, clamp them, normalize them, set filter requests, or clear inherited
state. Handle namespace/shared-storage branches exist in the machine code,
but this contract admits only the proven private handle/profile.
Commit completion at `82C1ED00..ED0C` clears the original private and pool
dirty-bookkeeping blocks. This is separate from the writer's single dirty-bit
update; native ownership need not recreate those SDK cache blocks.

The original writer assumes a valid initialized object and active context;
it supplies no null/error checks that could legitimize running SDK commit on a
native ID. Native apply must validate owner, selected technique/pass, shader
capability, parameter storage and supplied resource association before binding.

## `g_Weights` to PS c0–c3: complete static association

The metadata parser's private name/descriptor proof gives parent handle
`00040000`, followed by four scalar child descriptors. The pass at `F+438`
points to context `F+BB0`; context+40 points to five 10-byte binding records
at `F+C50`. The record words are:

```text
offset   parameter   VS word   PS word   auxiliary
C50     00080000    00000000  00000000  00000000
C60     000C0002    00000000  00000001  00000000
C70     00100004    00000000  00000002  00000000
C80     00140006    00000000  00000003  00000000
C90     00180008    00000000  00000000  00000000
```

Their meaning is selected by context masks, not inferred from zero/nonzero
register words. Context+4 points to `F+C10`, whose BE64 is
`F000000000000000`: the first four records are private **PS float** bindings.
Context+1C points to `F+C40`, whose BE64 is `0800000000000000`: the fifth is
a private **PS texture** binding. The VS, integer/conversion and shared masks
are zero. Context+44 is `FFFFFFFF`, the serialized null shared-record pointer.
`82C17A20..AE0` explicitly relocates these context pointers during SDK creation.

Commit's PS-float loop is `82C1DF78..E088`. It intersects the private dirty
bits with context+4's mask and uses the selected record's handle to resolve
the descriptor. That descriptor's second word selects private slot
`0,10,20,30`. Record+8's low10 selects PS register `0,1,2,3`; bits10–11
give count-minus-one, zero for every row. At `82C1DFFC`, literal `178` is
added to the register and shifted left four, giving SDK offsets
**D+1780,1790,17A0,17B0**.

Each iteration's vector load/store at `82C1E00C/E014` copies **all 16 bytes**,
including YZW. `82C1E02C..E064` marks the corresponding four-register group
dirty at BE64 `D+8`. All four here select its high bit
`8000000000000000`. Thus the native constant payload is four float4 values,
64 bytes, not four packed floats. The shader proof separately establishes
that only their X lanes affect color.
The report and slot-projection fixture retain canonical BE original words;
native upload must decode them to host float32/word representation. Copying BE
byte order directly into a D3D11 constant buffer would be incorrect.

Original default X words are `3E555555,3E2AAAAB,3DAAAAAB,3D2AAAAB`; Y/Z are
zero and W is `3F800000`. The commit consumes **current private storage**,
not shader debug defaults. Parameter edits before commit must remain visible.
No constant-write caller is assumed merely from the name `g_Weights`.

The original indexed draw `8244CEC0` enters setup `8244C970` at `8244CF04`.
At `8244CA20..CA3C` setup tests the pixel dirty mask, passes
`(D,mask,4400,D+1780)` to `8245EC68` at `8244CA38`, then clears D+8.
The uploader operates in groups of
four vectors, reads source offsets `0,10,20,30`, writes 40 bytes per selected
group and constructs the register write header. With only this group's dirty
bit set, it selects register `4400`, sixteen DWORDs, header `000F4400`.
Local declarative labels identify `4000` as `SHADER_CONSTANT_000_X` and `4400`
as `SHADER_CONSTANT_256_X`, the separately selected pixel bank. The slow space
path delegates to `8245E278`; allocator/submission correctness is not newly
qualified. No console packet execution is needed for native implementation.

## Texture and sampler association

The fifth context binding has handle `00180008` and PS word zero. Commit's
private-PS-texture loop `82C1E4DC..E59C` intersects dirty bits with context+1C,
uses record+8 bits22–29 as the stage, reads the resource DWORD from private
slot+40, and calls **824408E0(D,0,resource,0000000080000000)** at
`82C1E56C` (word `4B822375`). This is the exact stage-0 association consumed
by all four PS texture fetches in the frozen shader proof.

`824408E0` imports the original resource descriptor into D's fetch descriptor,
stores the stage resource at `D+30F8`, preserves sampler fields, and recomputes
the effective mip interval from resource limits and retained request bytes.
It also tracks prior resource use; this is not an ordinary COM AddRef proof.
Native code needs a validated sampled-resource/view owner and GPU lifetime,
not a texture pointer copied into fabricated SDK storage. A null resource
takes the original null-binding branch; it is not authorization for a fallback
texture or a successful sample.

The five explicit stage-0 sampler requests are U/V address `2`, mag/min `1`,
mip `2`. W address, border, LOD/bias, anisotropy and other fields stay inherited.
Texture association alone does not establish the current resource's format,
dimensions, mip chain, view conversion or content.

## Inherited state and viewport-enable zero

The first pass supplies only these scalar `(SDK offset ID,value)` pairs:
`(2C,7),(30,0),(38,0),(3C,1),(48,1),(4C,1),(6C,0),(130,0)`.
They map through the current application selector tables; preflight must not
assume the initial file's zero mapping arrays are initialized mappings.
Save/restore uses current application-request values through `826B7940` and
`826B79B0`, not independent hardware-state queries. Direct SDK changes can
make those shadows insufficient as an effective-state snapshot.

No depth-enable, blend operation, separate-alpha policy, alpha test/reference,
color-write mask, fill/scissor, pixel-center mode, multisample target/control,
clip plane, target binding or viewport rectangle is supplied by those eight
records. Such state must be obtained from the tracked effective engine state
and the actual caller. Startup defaults or the shader fixture cannot fill gaps.

The original viewport setter **8243B260** is fully pinned. Value zero writes
**D+294C=00000400**, and inserts **1 into D+2944 bit16**. It marks dirty64
D+10 with `A0`. Value nonzero writes `43F` and clears that clip-disable bit.
Local register labels establish that zero disables all six viewport X/Y/Z
scale/offset enable bits, retains `vtx_w0_fmt=1`, and sets `clip_disable=1`.
This is an active transform/clipping mode. It is not a zero-area viewport,
rendering disable, default full-target viewport, or permission to drop position
Z/W. The native shader artifact passes the fetched position through; a D3D11
draw must implement the caller's actual position/viewport/pixel-center
convention. This static proof alone does not specify an equivalent host matrix
or clipping policy for arbitrary geometry.

## Original caller and its reachability limit

**827219B8**, extent `1A4`, is an original caller containing this entire typed
sequence. It preserves incoming r3 as r29, obtains descriptor data through
`82440178`, builds the CPU vertex/index arrays through `82721610`, binds the
declaration from global `82D6CCF0`, then:

```text
82721A54 -> 8273B2F8  begin on [82D6CCEC]
82721A60 -> 8273B310  supply preserved input r29; commit
82721A8C -> 8244CEC0  primitive4, minVertex0, vertices8, indicesC, stride30
82721AC4 -> 82455570  original resolve-side operation, r6=r29
82721B10 -> 8244CEC0  primitive4, minVertex8, vertices8, indicesC, stride30
82721B40 -> 82455570  original resolve-side operation, r6=r29
82721B48 -> 8273B308  end on [82D6CCEC]
```

The body also calls clear-side helper `82453C30` at `82721A48` and
`82721AE8`. Neither clear/resolve semantics nor target aliasing/lifetime is
qualified by identifying these edges. In particular the same preserved
resource is passed to commit and both resolve-side calls; do not replace this
sequence with a single isolated sample/draw or assume unchanged contents.

The known initialization path `826B0DF8` calls manager registration/finalization
through `82701B70`, then `82721948`. That initializer creates a declaration from
`82CF0428` through `824458E0` and caches it at `82D6CCF0`. It looks up
`fourtapblend` in the initialized typed manager via `826B7088` and caches the
result at `82D6CCEC`. This connects the actual typed row0 to the consumer's
global, subject to successful initialization and valid borrowed lifetimes.

**Normal-play reachability is not established.** The original `.text` scan
`82230000..82CC3CE4` finds one direct call to each typed begin/write/end, all
inside `827219B8`. A broader opcode16/opcode18 branch scan finds no external
edge to any instruction in that consumer's extent. Its entry-address byte
literal occurs only in `.pdata` at `82201690`, which is not a caller. This is
an original implementation, not proof it is called in a normal frame or boot.
Indirect/computed aliases, optional startup callback `82D6302C` and dynamically
supplied addresses remain open. Native first-effect construction/compilation
does not close that gap.

## Original declaration and fetch component expansion

The actual declaration at **82CF0428** has five `C`-byte entries and one
terminal entry, total `48` / 72 decimal bytes. This is the declaration table's extent,
not the vertex stride. Its entries are all stream0:

```text
element address  vertex byte offset  packed type  usage/index
82CF0428        00                  001A23A6     0/0
82CF0434        10                  002C23A5     5/0
82CF0440        18                  002C23A5     5/1
82CF044C        20                  002C23A5     5/2
82CF0458        28                  002C23A5     5/3
82CF0464        terminal streamFF, packed typeFFFFFFFF
```

The VS's five fetch metadata words at `820B8FE8..8FF8` are
`00100003,00005004,00015005,00025006,00335007`. The SDK fetch patcher
`8245EDC8` matches their usage/index fields against declaration bytes+9/+A;
the low instruction indices select VS slots3,4,5,6,7. This establishes the
five shader/declaration joins using metadata and patcher instructions.

Packed type `001A23A6` has float4 format low6=`26`, selectors `[X,Y,Z,W]`.
`002C23A5` has float2 format low6=`25`, expansion selectors `[X,Y,0,1]`.
The format labels come from the hashed local enum declarations. Patcher
component composition preserves the original fetch keep selector7. Therefore
the position fetch receives **all four stored position floats**, while the
UV fetches consume just each declared XY and preserve the other temporary
lanes, as the frozen shader proof requires. Missing UV components do not
supply position Z/W or overwrite the keep lanes.

The actual stream stride is **30 bytes / 48 decimal**, independently proved
by both draw callsites' stack argument and CPU stores. Fetch offsets in DWORDs
are `0,4,6,8,A`, stride `C`. The SDK dirty-declaration path reaches patcher
`8245EDC8` through `8245FA00 ->8245F8C0/8245F6B0`; this analysis qualifies the
matching, formats and component composition. It does not reconstruct the
complete emitted shader, fetch sorting, mini-fetch/prefetch optimization or
other SDK shader machinery. Native D3D11 needs the proven input association,
not a console fetch patcher.

This layout happens to agree with the native shader fixture's 48-byte layout;
the original table, metadata, patcher fields and caller stores establish it
independently. No inference used the fixture.

## CPU vertex/index streams

`827219B8` provides stack storage at `SP+D0` for **10 vertices** (16 decimal),
`300` bytes total, and indices at `SP+A0`. `82721610` calls the bounded leaf
**827213E8** four times at `82721854/64/74/84`, constructing four quads. The
leaf has 48 decimal separately inventoried float stores per quad, returns the next
free record address, and explicitly fills every vertex field.

For dimensions W/H converted from unsigned input to float32, each quad's
vertex order and position are:

```text
v0 = (-0.5,    H-0.5, 0, 1)
v1 = (-0.5,    -0.5,  0, 1)
v2 = (W-0.5,   H-0.5, 0, 1)
v3 = (W-0.5,   -0.5,  0, 1)
```

The subtractions and UV additions use the original single-precision stores/
operations recorded in the ledger. For each of the four taps, base UVs are
`(0,1),(0,0),(1,1),(1,0)` in that vertex order, plus the quad's per-tap offset.
The four quad offset lists before reciprocal-dimension multiplication are:

- U `[-0.5,-2.5,-4.5,-6.5]`, V all zero;
- U `[1.5,3.5,5.5,7.5]`, V all zero;
- U all zero, V `[-0.5,-2.5,-4.5,-6.5]`;
- U all zero, V `[1.5,3.5,5.5,7.5]`.

U offsets multiply `f32(1/f32(W))`, V offsets multiply `f32(1/f32(H))`.
The shader itself computes none of these offsets. Zero/exceptional dimensions
and general floating-point equivalence are not newly supported by this leaf.
The CPU supplies Z=0/W=1 explicitly in this caller; the VS remains a genuine
float4 pass-through, without an inferred Z/W replacement for other callers.

The generated 16-bit index words are
`0,1,2,1,3,2,4,5,6,5,7,6,8,9,A,9,B,A,C,D,E,D,F,E`.
Primitive raw4 is a triangle list. The first draw uses the first C indices,
8 vertices, minVertex0. The second uses indices at `SP+B8`, 8 vertices,
minVertex8. Both pass `SP+D0` as vertex base and stride30. `8244CEC0` copies
`vertexCount*stride=180` bytes from `base+minVertex*stride`, through
`82A3CD80`, and uses `-minVertex` for rebasing. Do not interpret the second
call as reading the first eight records or silently rewrite the shared index
array. Native vertex uploads must translate original BE float/index storage
to host representation and own their GPU lifetime.

## Remaining native qualification

The proven application association is suitable input to a native engine
service that owns the exact metadata and compiled shader identities, commits
four current float4 weight slots, and binds one validated stage-0 sampled
resource with tracked effective state. It must preserve manager selection,
the original thirteen-value save/restore behavior and borrowed-object lifetime.
It must not resume `82C1DBA0`, the original shader binders, or the SDK uploader
with native opaque IDs.

Outstanding independently from the application proof: a real reachable entry
into `827219B8`; current resource/target ownership, aliasing and contents;
clear/resolve behavior; effective inherited state; fully qualified native
viewport-zero rasterization and pixel-center convention; complete SDK fetch
emission if ever needed as an oracle. Declaration matching and component
expansion for these five inputs are closed by the static evidence above.
No game frame or draw occurred in this task.
