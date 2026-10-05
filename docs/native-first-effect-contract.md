# First original FX resource: fourtapblend

Status: bounded metadata parser and evidence complete; no production changes.
This is the evidence snapshot before integration. The current native owner and
executed lifecycle/shader results are in `native-first-effect-owner.md`.
Main owns shader arithmetic/translation and runtime wrapper integration. This
pass concerns one original serialized FX, its CPU reflection cache, typed CPU
object, and paired ownership. It does not qualify the other 24 effects or a draw.

## Immediate contract and correction

`826B4828` builds a **technique/pass state cache**, not an ordinary parameter
cache. Earlier `native-graphics-startup-services.md` used “parameter metadata”
too loosely for FX `+200/+20C/+210`; this document supersedes that wording.
The first effect contains one technique, one pass, eight scalar-state records
and five sampler-state records. The original engine allocation is
`24*1 + 12*8 + 16*5 = 200` decimal bytes (`C8` hex).

Technique handle is `0003FFFC`; pass handle is `0003FFFE`. The pass-record
stride/divisor is **14 hex / 20 decimal**, not 28 hex. `826B48A8..BC` subtracts
the pass-array base, uses signed multiply-high `66666667` then arithmetic shift
3 (division by 20), and inserts the result above bit17. The original relocator
also advances pass records by `14` at `82C17A00`. Parameter handles are a
different encoding: private `g_Weights=00040000`, `g_Sampler=00180008`.

A real first-effect owner is practical: immutable original blob and two shader
records, parsed technique/pass and private parameter metadata, mutable owned
parameter storage, and the genuine original CPU cache allocation. There is no
need to reproduce a relocated D3DX SDK object in guest memory. Replacement must
cover creation **and reflection together**, plus the typed finalizer and paired
release; it cannot merely put an opaque ID into wrapper+10 and resume SDK readers.
Shader records may be owned while uncompiled, with explicit failure before use.
Their framing alone never establishes compiled shaders or successful rendering.

## Authority and reproducibility

Original flat PE `analysis/simpsons.pe`, base `82000000`, 15,466,496 bytes,
SHA256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
All addresses/offsets/sizes below are hexadecimal unless stated otherwise.
The evidence is original BE words independently checked against
`build/generator-ninja/SimpsonsDisasm.exe`, with original `.pdata` extents and
explicitly distinguished leaf/window extents. No original SDK execution occurs.

```powershell
python -B build/first-effect-contract/verify.py
```

Outputs: `build/first-effect-contract/evidence.json` and `disassembly.txt`.
The parser accepts only this exact original profile and rejects changed unknown
bytes as well as malformed known framing. Ten tests cover envelope/truncation,
technique/pass links, shader framing/stage, state categories/stages, private
parameter descriptors/handles, default finiteness, context end links and opaque
byte changes. It is a bounded metadata parser, not a GPU/state interpreter.
Initial validation: all 10 tests passed; 39 bounded instruction spans and 29 direct
calls passed original-byte checks. A second complete run produced byte-identical
JSON and disassembly outputs. The later pool extension is qualified below.
No runtime fixture, SDK creation, native shader compilation, or game draw was
executed by this evidence task.

## Exact envelope and body-relative pointers

Row0 at `82CEFD20` is `{820B8AA0,820B89D4,0,8273B280}`: blob, name,
initial output wrapper, typed callback. Name literal is `fourtapblend`.

Blob B=`820B8AA0`, header BE words `{A3D70141,CA0,AC0}`. Body F begins
at B+C=`820B8AAC`. In `82C1D0C0`, r30 is B+C at `82C1D0E8`;
the no-pool copy `82C1D274..278` copies BE32[B+4] bytes from that address.
Therefore the complete serialized copy envelope is **CAC / 3,244 decimal
bytes**, ending at `820B974C`, SHA256
`18f58d1e2c0b74fa86a73abe2dce583baf131583f328a044fe6cd2095378cee9`.
The next registered blob starts `820B9750`; its preceding four alignment bytes
are not included. B+8=`AC0` is a copy-prefix size used by shared-pool handling,
not an alternate file length. The parser additionally proves the final two
context records end exactly at body+CA0.

The body's offsets are relative to F. `82C17568` explicitly relocates them by
adding the copied body's address; `FFFFFFFF` maps to null. Offset zero is not
a general null sentinel. Preserve this distinction in offline bounds checking.
Some context end links legitimately equal body length; permit that only as an
end marker, never as a readable object. The native owner should use checked
indices/spans, not perform these relocations into a fabricated SDK allocation.

For this profile the relevant header fields are:

- +108=`390`: private descriptor table; +110=2 private names, +114=0 shared names.
- +118=7 descriptor-slot count; +11C=0 shared slot count. +10C=`380` and
  +12C=`384` are pointer slots used by the original shared-storage accessors;
  their original contents are `FFFFFFFF`. They are not ready host addresses.
- +128=`3D0`: private default storage; +130=5 vector slots, +134=0 shared slots,
  +138=`50` private bytes, +13C=0 shared bytes. +120=1 and +124=0 contribute
  to original context sizing; their broader flag semantics are not inferred.
- +200=`420`, +20C=1: technique-pointer array and count.
- +210=`438`, +220=1: contiguous 14-hex-byte (20-decimal-byte) pass array and count.
- +224=`AC0`, +228=2, +22C=`1E0`: context array, count and byte span.
- Resource-array triples at +230/+23C/+248/+254 are described below.

The entire header and remaining serialized bytes are retained/pinned. Fields
not consumed by the proven subset, including auxiliary name/annotation data,
are not assigned invented meanings or silently accepted as supported queries.

## Technique, pass and shader associations

F+420 contains `424`. Technique record F+424:
`{A24,1,0,2,438}`. +0 resolves to `Technique0`; +4 is one pass;
+10 points to F+438. Words +8/+C remain unclassified here.
Pass F+438 is `{A2F,0,BB0,860,950}`: `Pass0`, raw word0,
context pointer, scalar-state block, sampler-state block.

The four resource-array triples `(offset,count,totalBytes)` are:

- Vertex records: `(44C,2,1CC)`.
- Pixel records: `(618,2,21C)`.
- Scalar-state blocks: `(840,2,80)`.
- Sampler-state blocks: `(8C0,2,150)`.

Shader counts include a null sentinel. `82C19498` starts at array+8, loops
indices 1..<count, passes entry+8 to shader creation, stores its result at entry+0,
and advances by `8+BE32[entry+4]`. The first non-sentinel entries are F+454
and F+620, initially `{0,1BC}` / `{0,20C}`, followed by their original records.

- **VS `820B8F08`**: tag `102A1101`, header `114`, payload/code `A8`, total
  `1BC` (444 decimal); SHA256
  `8dcb727e02d552e6ca8d18b09ffc57d11856c8d418d962d00b24c914e11a7347`.
- **PS `820B90D4`**: tag `102A1100`, header `188`, payload/code `84`, total
  `20C` (524 decimal); SHA256
  `2c71ebc59531050284a4473619cb2b810d2854c65ad0dcf483577b107e7c7ea3`.

Stage authority is also `82C194C4 ->82448308` versus
`82C1950C ->82448178`. Both records have zero payload prefix, established by
their code metadata at header+6's offset: `(0,A8)` / `(0,84)`. There are 14/11
twelve-byte instruction slots. No exec schedule, arithmetic, texture component
order or native HLSL equivalence is asserted by this parser. Those belong to
main's independent shader proof. Existing `MaterialRecord` framing can be reused
only after explicitly qualifying these two additional identities.

`82C16E50` computes a context stride from header counts: start
`58 + 40*(F120+F124)`, align16/add `10*F130` when nonzero, then similarly for
F134, align16, and add context+54. Here the result is F0 for each context.
F+AC0 is the initial context, F+BB0 the pass context. Their +48/+4C pointers
are `(44C,618)` and `(454,620)`, respectively. Their +50 end links are BB0
and CA0. Thus the pass's two shader associations are proven through actual
serialized links, not just discovered by a byte-pattern scan.

`826B2958(F,passHandle,out32)` returns these fields for the first pass:
+0 name `Pass0`, +4 raw0, +8 scalar count8, +C sampler count5,
+10 VS record pointer/+14 bytes1BC, +18 PS record pointer/+1C bytes20C.
It follows the context's +48/+4C framed entries to obtain the two record spans.

## Literal state records and original engine cache

`826B2958` sums block+10/+14/+18 for scalar states and block+80/+84/+88
for sampler states. Both profiles are **literal-only**: `{0,0,8}` and
`{0,0,5}`. `826B2B88` and `826B2C48` therefore return each stored value
directly; their integer-parameter/float-conversion branches are not taken here.

Scalar `(original SDK offset ID,value)` records, in original order:
`(2C,7),(30,0),(38,0),(3C,1),(48,1),(4C,1),(6C,0),(130,0)`.
The already-qualified native names are depth compare, depth write, cull,
blend enable, source blend, destination blend, stencil enable, viewport enable.
The state values are original numeric enums, not desktop D3DRS enum values.
No depth-enable, blend-operation, color-mask or other inherited state is supplied
by this list. In particular viewport-enable0 must not be silently replaced by
an arbitrary native viewport or used as world-draw authorization.

All sampler records address stage0: `(ID,value)` is
`(0,2),(4,2),(10,1),(14,1),(18,2)` for U/V address, magnification,
minification and mip filter. Other sampler fields and stages remain inherited.
Texture binding is separate from these five states.

Mapping tables are **82E06F80** and **82E07118** (`lis82E0` plus positive
6F80/7118). Their entries are application selector numbers, not function pointers.
Original `828313A0` installs default scalar selector `ID/4-9` and sampler
selector `ID/4+1`. First-effect scalar selectors are `2,3,5,6,9,A,12,43`;
sampler selectors are `1,2,5,6,7`. Table setters `82831360/80` can change them.
The original file has zeros before initialization, so a native construction
preflight must read/validate the current guest mapping rather than assume the
file contents or hardcode a purported always-current mapping.

Let W be the original 30-byte wrapper, H=W+14, C the original C8-byte allocation.
`826B4828` produces W+18=effect reference, +1C=C, +20=C8, +24=C, +28=1.
W+14 stays W; W+2C (H+18, active cache row) remains its constructor-initial zero.
It frees the preceding cache if the required byte count changes, then allocates
through original `8269BE40`; it does not silently replace the original allocator.

The one cache row at C has:
`+0=0003FFFC, +4=0(pass index), +8=C+18, +C=C+78, +10=8, +14=5`.
Eight C-byte scalar rows follow at C+18:
`{currentMappedApplicationSelector,requestedValue,savedPreviousValue}`.
Five 10-byte sampler rows follow at C+78:
`{stage,currentMappedApplicationSelector,requestedValue,savedPreviousValue}`.
**Saved previous values are unwritten during reflection.** They are filled by
actual apply; do not zero them and claim original write-footprint equivalence.
The exact untouched slots are C+20+0C*i for scalar i=0..7 and C+84+10*i
for sampler i=0..4. This first reflection writes C+00..17 completely, then only
the first two scalar words / first three sampler words. For equal old/new cache
sizes, it reuses the allocation and overwrites those same fields; saved values
and the active-row pointer remain untouched. The bounded constructor profile
starts with active-row zero; this does not qualify rebuilding an active effect.

`826B35D8(H,techniqueHandle,passIndex)` selects the row, saves current values
through `826B7940/826B79B0`, then applies through `826B7968/826B79E8` with
force0. `826B37B8(H)` restores those saved values through the same original
setters and clears H+18. This is a useful CPU state path to retain, provided
the surrounding effect application still rejects unported shader/SDK work.

## Private parameters, defaults, and the shared-pool boundary

The first effect has two **private** named parameters and no shared named
parameters. Original `823C7B20` searches private names first and then shared.
Private name block F+A10 is `g_Weights\0g_Sampler\0`.
The descriptor table at F+390 has a sentinel 8-byte entry, then:

- F+398 `{00200012,00040005}`: array container spanning five descriptor entries.
- F+3A0/3A8/3B0/3B8: `{0,00010000}`, `{0,00010001}`, `{0,00010002}`,
  `{0,00010003}`.
- F+3C0 `{0020000C,00000004}`: sampler parameter entry.

`823C7B20` advances an entry by 8 if its first word's low two bits are zero,
otherwise by `8*BE16[entry+6]`. The handle incorporates the descriptor index
and count of preceding scalar entries. This independently yields
`g_Weights=00040000`, `g_Sampler=00180008`. Unknown names return zero in the
original. These are parameter handles, not Xbox GPU pointers or texture handles.

F+3D0 contains five exact 16-byte default slots. The first four have X words
`3E555555,3E2AAAAB,3DAAAAAB,3D2AAAAB`; each has Y/Z=0 and W=`3F800000`.
The fifth is `{0,0,0,3F800000}`. Preserve all 50 bytes, including the W lanes.
The initial sampler resource word is zero. Shader debug/constant metadata is
not authority to substitute its differently padded defaults for these FX slots.
The parser reports original words, without inventing later GPU packing.

The r5 argument to `82C1D0C0` is the original shared-pool object, not a second
effect or a copied native SDK context. With r5=0 the original copies the whole
CA0 body and adds two aligned 80-byte private bookkeeping regions. With a
nonnull pool and this effect's +11C=0, it takes the no-merge path: copies
`AC0+1E0=CA0`, borrows pool bookkeeping bases at pool/pool+80, and later
increments pool+188. This effect contributes no shared named parameters.
Boot086 subsequently proved the pool nonnull; see the bounded extension below.

`826B7218` first finalizes every typed resource, then queries M+18 for
`g_ViewProjection`, `g_WorldEyePosition`, `g_UTransform`, `g_VTransform`
through `826B2528`, storing handles in M+21C/+220/+224/+228. Those names
are absent from this effect's private list and are **not** made available by
successfully loading row0. A first-effect-only fixture must not fake the entire
manager finalization or invent zero-valued global parameters. General shared
pool merge/lifetime and the other effects remain outside this milestone.

### Boot086: real original CPU pool and native lease policy

The actual `build/boot-086.log` reports W=`E1A9C9F8`, M=`E1A9C600`,
P=`E1A50400` at the pre-SDK first-effect guard. Pool contents and reference count
are not in that log. The proposed empty-profile checks below must inspect live
memory; they are not a claim that this task captured those contents.

**A native first-effect owner can keep this original pool as CPU metadata.**
It is a genuine D3DX CPU allocation already constructed by original code, not a
device, command buffer, or invented SDK-shaped native object. A counted host
lease is legitimate for a native FX resource, provided it protects that exact
root's lifetime and all SDK FX/pool consumers outside this subset remain gated.
This is native ownership substitution, not preservation of SDK FX refcount writes.
The no-shared-parameter profile does not eliminate the lifetime dependency.

Original pool creation is `82CBA370 ->82C1CDD8`, with output address
`82D6D2F8` (`lis82D7; addi -2D08`). `827225B0` reads that exact global.
The manager borrows this pointer; first FX receives it as r5. `82C1CDD8`:

- Requests 200 bytes aligned80 through `82C16D90`; this helper calls the
  existing CPU allocator `8238E880` with size280 and flags `24870000`.
  The returned aligned P has the real raw allocation pointer stored at P-4.
- Initializes P+000..07F to zero and P+080..0FF to FF. It explicitly zeroes
  ten words +100..124, +180 and +184, and initializes +188 to 1.
  P+128..17F and +18C..1FF are uninitialized by this constructor; do not require
  them to be zero or overwrite them as part of a claimed original footprint.
- Enters existing CPU critical section `82D00F84`, increments the SDK pool
  population word **82E2D968**, and may clear the CPU bookkeeping mode word
  `82D00F80` when more than one pool exists. It publishes P only on success.
  No device argument, shader creation, console command, or MMIO call occurs.

For this exact first blob, +11C=0 makes `82C1D120` branch to `82C1D208`
before reading P+100. `82C1D2D0` then takes the no-merge copy path. The copied
effect receives F+100=P and F+104=P+80. Original relocation changes the copied
effect and its embedded private slots, not those two pool bookkeeping regions.
The private shader/state resources add no shared pool parameters. The direct
pool mutation of successful original first-FX creation is the unconditional
nonnull-pool retain at **82C1D3C4..CC**: `P+188 = P+188 + 1`.
Optional SDK effect observers and subsequent apply/clone/merge are not included
in a native constructor-ready capability.

There are two coherent ownership implementations; do not mix their counts:

1. **Main's selected host-lease policy.** Verify genuine root/allocation
   provenance, P==BE32[M+18]==BE32[82D6D2F8], empty initialized metadata, and
   P+188==1. Acquire a real host lifetime association between native FX and P;
   keep +188 at the root's 1 because no original SDK FX object exists. Release
   the host association on paired native FX destruction. Driver stop and root
   retirement require zero native leases. Keep native/SDK counts separate and
   never call SDK FX release on the native effect ID.
2. **Original CPU count option.** `82C1CEE8(P)` and identical leaf `82C1D088(P)`
   read +188, add one, store it, and return the new u32 count in r3. The full pin
   is `7C6B1B78814B0188386A0001906B01884E800020`. Reject zero/overflow and
   serialize on the owner thread; this is not an atomic/interlocked increment.
   `82C1CF00(P)` decrements/stores +188; when nonzero it immediately returns
   that count. Preflight old count>1 for the native FX's release so it cannot
   take the final-free branch. This gives an actual 1->2->1 for one effect.
   It preserves original count semantics without any SDK effect layout.

For either policy, unchanged empty metadata means P+000..07F all zero,
P+080..0FF all FF, P+100..124/+180/+184 zero; only explicitly known fields
are checked. No host policy needs to dereference private FX offsets on P.
The offline validator in `verify.py` checks this proposed profile, count bounds,
and untouched padding using synthetic validation fixtures; it is not evidence
of actual runtime pool contents or a replacement pool constructor.

**Other pool release and count consumers:**

- **82C1CF00**, pin `7D8802A64BE1F4C99421FF90`, is the actual pool release.
  **82C1D0A0**, pin `4BFFFE60`, tail-calls it and must receive equivalent guard
  coverage. A host-lease policy must reject these operations on P while leases
  exist; returning success without release would not be faithful.
- On final count zero, CF00 frees an optional CPU allocation rooted at P+180
  using its -4 raw backpointer, then frees P's raw backing. It decrements
  82E2D968 under the CPU critical section. If that and the SDK FX population
  82E2D964 are both zero, it frees a CPU scratch allocation at 82E2D96C, clears
  82E2D970 and calls `82C16C28` to clear CPU lookup/cache arrays. This is CPU
  cleanup, but it is not the nonzero release path used by the count option.
- Shared-pool merge `82C18530` reads P+188 at `82C185FC` to preserve the count
  while rebuilding pool metadata. FX clone `82C1D668` retains at `82C1D7F4`.
  `82C1D858(FX,outPool)` reads FX+2B8, retains its nonnull pool at `82C1D864`,
  and writes the pointer to r4. These are SDK FX consumers, not permission to
  query or clone a native effect ID. Main's SDK FX gates must retain this limit.
- The checked direct root path has no application branch querying P+188. A
  bounded load/store scan in SDK FX range 82C16C28..82C20000 found the above
  merge, constructor, two retain leaves, release, FX-create, clone and get-pool
  users. This does not prove absence of all indirect aliases elsewhere.

**Root teardown is a separate hazard:** static constructor registers
`82CC1820`; it calls `8269BEB0(P)` directly at **82CC1840**, then clears
82D6D2F8. It does not inspect +188 or call CF00. Its entry pin is
`7D8802A69181FFF8FBE1FFF0`. The generic wrapper destructor **82722568**
has the same entry pin and loads/frees/clears its r3[0] pointer similarly.
Both direct-free routes must reject the owned root while native leases exist.
After all effects retire, preserving the original CRT body preserves its exact
CPU cleanup; it does not establish that CRT free performs CF00's SDK population
cleanup. The raw-versus-aligned allocator compatibility of that separate CRT
route and normal complete CRT shutdown are not requalified by this extension.
Do not silently replace it with CF00 and claim identical teardown.

Pool-extension validation: **13 tests passed**, including exact original retain/
release pins, malformed pool/count/extent rejection and acceptance of untouched
padding. The report now contains **56 byte-checked instruction spans, 39 checked
direct calls**, and the reproducible bounded +188 load/store scan (14 words).
Two complete runs produced identical report/disassembly bytes. This extension
did not execute a guest pool lifecycle or change runtime/configuration files.

## Typed callback, finalization and destruction

`8273B280(r3=name,r4=M)` allocates B8 through original `8269BF70`, descriptor
`{2,10,0}`, calls `826B4F60(T,name,M)`, then sets vtable `8215036C`.
The base constructor retains original name allocation and three real CPU blocks:

- T+28 owns 600 bytes: 64 decimal records of 18 bytes, all six words per record zeroed.
- T+38 owns 1E0 bytes: four records of 78 bytes, zeroed by original stores.
- T+30 points four bytes into a 2A4-byte allocation: leading count18, then
  24 decimal records of 1C bytes; only each record's +14 word is initialized to zero.

T+10=M; +18/+1C and byte+20 are zeroed; +34/+3C/+40 are zeroed;
T+48..A7 is cleared. Unlisted fields, including T+A8..B7, are not initialized
by this constructor. The registration loop separately writes T+14=row index0
and inserts T into M+34. Null/failure handling must not be converted into
successful ready objects; several original callers do not check their results.

Vtable+C is **8273B3B8**. It finds W via M+1C using hash of `fourtapblend`,
stores T+A8=W, gets W+10 through its virtual+8 and stores T+AC=effect reference.
At `8273B420` it calls `823C7CA0(effect,"Technique0")`, stores result at T+B0;
at `8273B438` it calls `823C7B20(effect,"g_Sampler")`, stores result at T+B4.
Thus expected final fields are `{W,ownedEffectReference,0003FFFC,00180008}`.
A native replacement of this finalizer must retain the genuine lookup and those
engine fields while implementing the two queries from parsed metadata. It must
not invoke either SDK-layout getter on an unmapped native ID.

Typed deleting destructor **8273B220 ->826B3E50** frees the three CPU blocks
and name and optionally T. It does not release T+A8's wrapper or T+AC's effect;
they are borrowed. Normal `82701118` deletes the typed object before its wrapper.
Wrapper `826B4AC8 ->826B3910` releases W+10 through `82C1D560`, clears it,
frees W+1C and the name, then optionally frees W. Native lifetime needs the paired
W+10 release interception, with genuine owned shader/pool references retired.

The exact paired callsite slice proposed by main is supported by these bytes:

- At **826B4BAC**, word `48568515` calls `82C1D0C0`. Let M=BE32[W+C].
  Live r31=W, r3=BE32[M+14] (manager's device identity), r4=serialized blob,
  r5=BE32[M+18] (shared pool), r6=W+10 (output reference). These are values loaded from the
  manager, not an assertion that either identity has an SDK layout. Resume at
  **826B4BB0**, which sets r3=W+14; **826B4BB4** (`4BFFFC75`) calls the
  replacement for `826B4828`. Do not run the original reflection body on a
  native ID. The wrapper ignores the SDK-create return value and continues;
  failure must therefore throw/stop before a false resource is published.
- At **826B393C**, word `48569C25` calls `82C1D560`, with r31=W and
  r3=W+10's effect reference. Release the checked native owner, then resume at
  **826B3940**. Original `li r11,0; stw r11,10(r31)` clears W+10, followed
  by the actual cache free at **826B394C** and name cleanup. The original body
  does not clear the borrowed copy at W+18 before freeing W; do not add such a
  write while claiming identical CPU effects. A separate entry preflight can
  reject a live/active association before the destructor's vtable publication.

Keeping the typed finalizer `8273B3B8` guarded is valid for the current
constructor-only milestone. Its T+A8..B7 fields remain uninitialized, and the
original typed destructor does not read them. Permitting that destructor does
not permit any typed begin, parameter update, commit, or end operation.

Original FX release decrements effect+304; on zero it destroys the two shader
array resources, releases its device and pool, handles an optional linked effect,
and frees its own allocation. `82C19318` also checks whether the shaders are
currently bound and can unbind before release. A constructor-only native
milestone must enforce **never bound** and retire its real owner references;
it cannot claim the complete bound-resource release contract is implemented.

## Minimal native implementation slice and remaining gates

1. Parse and own this exact blob plus two original shader records. Keep private
   parameter storage as native-owned values and sampled-resource references;
   maintain explicit metadata-ready versus compiled/unsupported capability.
2. At the W engine boundary, preserve original wrapper/name/tree allocation and
   build the original C8 CPU cache using the current checked selector mappings.
   W+10/W+18 may identify a validated native owner, never an SDK object layout.
   Keep the original cache allocation/reallocation/free and unwritten bytes.
3. Qualify `8273B3B8` for the exact row0 typed object and its two native queries;
   preserve the original typed base constructor and destructor. Pair wrapper
   release with native owner release before permitting a full first lifetime.
4. Keep row1/all other effects unsupported. A test can invoke the original row
   registration with count1 and then the real typed finalizer, but production's
   count25 must not be shortened or reported as complete graphics initialization.

Entry cuts/pins available in the JSON include `826B4B88` creation/reflection,
`8273B3B8` typed finalizer (`7D8802A69181FFF8FBC1FFE8`), `826B3910` wrapper
destruction (`7D8802A69181FFF8FBE1FFF0`), and the first typed render consumers:
`8273B2F8` begin (`808300B0806300A84BF7AD78`), `8273B310` sampler/write/commit
(`7D8802A6483010B19421FF80`), `8273B308` end (`806300A84BF7980C`).
They and generic engine apply `826B5FC0/826B4B18` require gates until ported;
they read SDK FX fields or call SDK commit. This is coverage for the reviewed
first typed consumer, not an exhaustive scan of every FX use in the executable.

Meaningful next fixture: compare native parsing/query/cache outputs with original
CPU metadata helpers in isolated fixture memory containing a copied original
body, using its actual relocation helper `82C17568` and proven private storage
setup. That is a test oracle only, never a production SDK-shaped owner. Do not
invoke device-dependent FX/shader creation. Check name misses, exact handles,
one-pass shader links, all state rows, C8 size, untouched saved-value slots, full
typed allocations/frees, nonnull-cache replacement, source-copy ownership and
failure before publication. Native compilation/binding remains main's separate
shader and renderer work; no original draw or inherited-state closure is claimed.
