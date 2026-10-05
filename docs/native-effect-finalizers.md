# Original typed FX finalizers: additional guards required

**Do not let all 24 typed finalizers run with only the `823C7CA0` technique
and `823C7B20` parameter query hooks.** The 25 registration rows contain
24 nonnull typed callbacks but only **nine distinct finalizers**. Twenty-three
typed rows reach `826B5168`, which immediately dereferences the value published
as T+1C as a relocated SDK FX object. Fourtap is the sole exception.

An interim guard at **`826B7218`**, before the manager finalizer loop, is a
valid explicit stop while registration/cleanup is qualified separately.
Alternatively, a common guard at **`826B5168` entry** must reject a native ID
before its first SDK read. Do not return fabricated success or set T+20 ready
to bypass that work. Replacing this helper later still leaves shadows' inline
FX and genuine-pool setters to qualify separately.

The exact first common failure is:

```text
826B5178  817F001C  lwz r11,0x1C(r31)   ; T+1C = FX identity
826B517C  814B02AC  lwz r10,0x2AC(r11)  ; first unqualified SDK read
826B5180  824B020C  lwz r18,0x20C(r11)  ; second SDK read
```

This is static original-byte evidence, not execution of all finalizers. It
does not hold any parent build artifacts; build146's successful pool fixture
and its explicit freeze release remain in `native-effect-pool-cpu-test.md`.
Main may build and test independently.

## Original row, constructor and vtable association

Registration table `82CEFD20` contains 25 four-word rows. Its SHA256 is
`434b529f6bbfebd448fd57400833d090add3c5b868bdd1fc42b503b5bbca1063`.
The following associations come from original callback words, actual
constructor vtable stores, and **vtable+C** contents. They are not inferred
from effect names or presumed SDK interfaces. Row indices are zero-based.

- Row0 `fourtapblend`: callback `8273B280`, vtable `8215036C`, finalizer
  **`8273B3B8`**. Queries `Technique0` and `g_Sampler`; CPU wrapper lookup and
  publication only within this finalizer. It does not call `826B5168`.
- Rows1/12/16/17/18/22/23 `littextured`, `nrmmapnoskin`, `simple`, `simplelit`,
  `water`, `dualtexture`, `carnrmmap`: callback `8273B040`, vtable `8215034C`,
  finalizer **`8273B148`**. Calls `826B74C8`, then queries `TechniqueAlpha`
  and `TechniqueOpaque` if the resulting identity is nonzero.
- Row2 `particles`: callback zero. It has **no typed finalizer** in this table.
  Its wrapper/FX registration remains a real registration row.
- Row3 `quad`: callback `826B6B68`, vtable `820B7170`, finalizer
  **`826B7650`**. Calls `826B7570`, then performs 17 technique queries and
  eight parameter queries. The exact 25 names and call PCs are in the JSON.
- Row4 `shadows`: callback `82707058` invokes constructor `827064C0`, which
  stores vtable `8214E518`; finalizer **`82706A68`**. Calls `826B74C8`, then
  eight FX parameter queries, eight technique queries, two inline FX setters,
  eight pool-name queries, three inline pool setters and three pool-writing
  helpers. This finalizer is not query/publication-only even after replacing
  the common helper.
- Rows5/6/7/8/9/20 `skinned_lit`, `skinned_lit_tint_color`, `skinned_bruise`,
  `skinned_spec`, `skinned_unlit`, `skinned_bruisenospec`: callback `8273AE18`,
  vtable `8215032C`, finalizer **`8273AEF8`**. Calls `826B74C8`; then queries
  `TechniqueOpaque`, `TechniqueAlpha`, and `g_BlendMatrices`.
- Rows10/19 `unlittextured`, `mono`: callback `8273A430`, vtable `8215020C`,
  finalizer **`8273A5A8`**. Calls `826B74C8`; queries both techniques plus
  `g_Sampler`, `g_World`, `kIsSkinned`, and `kBoneMatrices`. Missing required
  handles can enter the original diagnostic path `82722150`.
- Rows11/13/14/21 `terrain`, `road`, `base_detail`, `standardworld`: callback
  `8273ABF8`, vtable `8215030C`, finalizer **`8273ACD8`**. Calls `826B74C8`,
  then queries both techniques when the identity is nonzero.
- Row15 `sky`: callback `8273AA18`, vtable `821502EC`, finalizer
  **`8273AAB8`**. Calls `826B74C8`, then queries technique `sky`.
- Row24 `zprepass`: callback `8273A4A8`, vtable `8215022C`, finalizer
  **`8273A6C0`**. Calls `826B74C8`; queries the unlit family set plus
  `g_ViewProjection` and `kBlendWeights`, with original missing-handle
  diagnostics.

The vtable store is directly in each listed callback except shadows, whose
allocation callback calls `827064C0` at `827070A8`. Constructor bodies are
included as vtable-provenance context; this does not qualify their resource
creation side effects. In particular, shadows already performs camera/raster
work before manager finalization, detailed below.

## Common path and concrete virtual targets

`826B7218` walks the manager's existing container at M+34. At `826B7264` it
loads T from a node, reads T's vtable, loads slot+C at `826B726C`, and calls
it at `826B7274`. The registration table proves the set of nine possible
targets, not the runtime container traversal order.

After the loop, it gets the genuine pool from M+18 and calls `826B2528` at
`826B7298`, `826B72B0`, `826B72C8`, `826B72E0`. These query
`g_ViewProjection`, `g_WorldEyePosition`, `g_UTransform`, `g_VTransform` and
publish handles at M+21C/+220/+224/+228. That tail uses the real CPU pool;
it does not make the preceding typed-finalizer loop safe for native IDs.

`826B74C8` hashes the typed name with `827451C0`, looks up the CPU wrapper
using `826B5A40`, publishes T+18 and T+1C, clears T+20, and calls
**`826B7540 ->826B5168`** for a nonnull wrapper/identity. Quad's
`826B7570` uses `826B6E60` to search wrappers by technique; after publication
it reaches **`826B75C4 ->826B5168`**. Thus all eight non-fourtap families
share the same unqualified metadata reader.

The wrapper virtual calls in `8273B3B8`, `826B74C8`, `826B7570`, and
`826B6E60` resolve slot+8 of original wrapper vtable `820B7140` to
**`826B3908`**, exactly `lwz r3,0x10(r3); blr`. This returns the identity
from the genuine CPU wrapper and does not dereference that identity.

The additional virtual call at `826B546C` uses typed vtable+10. Its concrete
targets are `823C7F28` (`li r3,0; blr`) for eight families and `823CA888`
(`li r3,1; blr`) for the skinned family. These are CPU feature predicates.
The only other indirect branch in the checked finalizer closure is the
internal four-entry switch at `82830528`, table `8283052C`; it is not an
unresolved external callee.

## SDK queries, readers and writes beyond the two hooks

The two hooked query entries are `823C7CA0` and `823C7B20`. Static closure
stops at those entry replacements; their original bodies are byte-checked
for context, not declared safe to execute on native IDs. `826B2528` is a
different API on the actual CPU pool, already qualified by the pool fixture.

`826B5168` also builds typed parameter/pass metadata. It caps a parameter
count at 64, enumerates descriptor/annotation records and pass bindings,
builds CPU buffers, changes bookkeeping masks through a nested helper,
publishes T+20 ready, and processes up to two pass-cache records. Its full
`364`-byte span SHA256 is
`8acf25e88a434bdef8fa873965f54d93b6217c112dbee80006c2a374102dfd9e`.
Replacing it requires the resulting CPU metadata contract, not just its
return value. The JSON contains all direct edges and complete bodies of the
following reachable readers/consumers:

- **`826B45A0`** takes FX in r4 and initializes a CPU adapter. It directly
  reads FX+2A4/+2AC/+20C/+228/+268, then calls **`82C1D858`** at `826B45FC`.
  `826F3F90` is another caller of `82C1D858` at `826F409C`.
- **`82C1D858`** reads FX+2B8, increments nonnull P+188, and writes the pool
  pointer to its output. This is an SDK FX-layout getter with an actual
  retain, not the real-pool name API and not a no-op host lease.
- **`828301F8`, `82831038`, `82830B18`, `826F3F90`** obtain FX through
  adapter+20 and directly read parameter/technique/pass fields including
  +2A8/+2AC/+200/+210. The adapter is a CPU object; the contained native ID
  still cannot satisfy these SDK reads.
- **`826B2A28`** decodes parameter descriptors and names through
  FX+29C/+10C/+108/+298, and annotation count/table fields +264/+284.
- **`826F3148`, `82721C98`, `82721DE8`** traverse parameter/annotation child
  handles through FX+260 or private/shared descriptor/name pointers. They
  do not reduce to the name-query entry hooks.
- **`826F3258`, `826F3328`** directly consume FX+210 pass records, usage
  masks and binding fields. **`826F3C00`, `826F3DC0`** combine those reads
  with descriptor queries and call the mask writer **`82C1F9F0`**.
- **`82C1F9F0`** reads FX+10C/+108/+104 and selects the private FX+80 or
  shared bookkeeping mask; **`82C1ED18`** recursively writes selected bits,
  including `stdx` at `82C1ED98`. These are writes, not pure descriptor
  getters. Both original entries are included in the byte-checked closure.
- **`82722068`, `82721F20`, `82830390`** are further descriptor/child/pass
  metadata consumers, reaching the above APIs and readers.
- **`8282FDD0`** reads FX+260 and annotation-index tables +274/+278/+27C/+280.
  **`8282FED0`** reads annotation descriptors/names/default metadata through
  +2A0/+260/+26C. **`82830C28`, `82830618`, `82830888`, `82830960`** form
  the adapter-side annotation query/classification chain.
- **`82C21408`** reads FX+260/+26C to obtain an annotation descriptor and
  value storage. It calls **`82C20FC0`**, which converts/copies descriptor
  values into an output buffer. Guarding only public technique/parameter
  name lookup leaves these accesses reachable.

CPU hash/tree helpers, array storage, string classification, ABI save/restore,
and diagnostics are distinguished in `evidence.json`. No unknown external
SDK query is hidden behind a generic “CPU-only” label: the fixed SDK-layout
readers and mask writers above remain unqualified on native IDs. The proof
is bounded at explicit CRT/allocator/diagnostic dependencies, not a review
of the whole allocator or formatting library.

## Shadows has inline setters after the common helper

Even a future complete replacement of `826B5168` cannot make
`82706A68` query/publication-only. Its full `41C`-byte span SHA256 is
`a3add400e6747bf9fb07177e5c82dbbc618006d8f530736a8d38e2c6c0776f9b`.

After query publication, `82706BD8` reloads FX from W+10. At
`82706BEC..82706BFC` it reads FX+10C/+100/+108/+128/+12C. It decodes
`kShadowBackDepthSampler`, dirties a byte at **`82706C4C`** and writes the
value from T+F0 at **`82706C50`**. The second inline block uses
`kFirstDepthSampler`, with stores at **`82706CD4/CD8`** and value T+F8.
These stores are inline in the finalizer: no generic query/descriptor hook
intercepts them.

`827225B0` then supplies the real pool. Eight original pool queries find
shadow/shared handles. Three inline blocks update that pool's dirty bytes
and default storage:

- `82706DD0/DD4`: `kShadowDepthSampler`, value T+F0.
- `82706E10/E14`: `kShadowCharDepthSampler`, value T+F4.
- `82706E50/E54`: `kShadowEdgeSampler`, value T+FC.

The tail calls `827058B0` at `82706E60`, `827059B0` at `82706E6C`, and
`82705AA0` at `82706E78`. The first two obtain P, read P+100/+108, dirty
the selected byte, and write the `kShadowAmt` vector at `82705990` and
`82705A84`. The third writes `kIsShadowReceiver`, including dirty-byte
store `82705B18` and floating-point store `82705B24`, with r4=0 at this
finalizer call site. This is real pool mutation requiring its own setter
contract; the first-initialization/name-lookup fixture alone does not
qualify it or the meaning of native texture/raster identities stored there.

## Earlier shadows constructor boundary observed by main

Main reported that build147's actual registration reaches the shadows
callback and rejects a native camera raster with flags5 before finalizers.
The original call path is independently pinned here:

```text
827070A8 ->827064C0          ; typed shadows constructor
82706590 ->82704C68         ; r3=400, r4=400 (1024 by 1024)
82704C7C ->823F1DB0         ; camera creation, retained in r31
82704C84  li r6,5
82704C88  li r5,0
82704C94 ->82408130         ; width, height, depth=0, flags=5
82704C98  mr r11,r3         ; preserve first returned raster
82704C9C  li r6,1
82704CA0  li r5,20
82704CAC  stw r11,60(r31)   ; first returned raster into camera+60
82704CB0 ->82408130         ; same width/height, depth=32, flags=1
82704CB4  stw r3,64(r31)    ; second raster into camera+64
```

The constructor invokes `82704C68` a second time at `827065A4` with the
same dimensions. Returned camera pointers go into T+5B4 and T+5B8.
The full helper span SHA256 is
`e67b6fddf4617864233d1aaef068ac38164901bc0782cc1f30ffa62329510fa2`.
This pins call arguments and ownership publication; it does not choose a
native format or claim support for flags5. The callback must not be skipped
to reach a later boundary. Other shadows constructor resource calls remain
constructor context, outside this finalizer capability assessment.

## Reproduction, checks and limits

Run from `K:\SimpsonsNativeCopy`:

```powershell
python -B build/effect-finalizers/verify.py --test
```

The verifier reads only `analysis/simpsons.pe` plus the existing decoder and
evidence utility. Original PE SHA256 is
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
It independently reads the registration table, verifies constructor immediate
vtable construction/store, reads vtable+C/+10, follows the bounded direct
call closure, records concrete indirect targets, and checks every disassembled
word against original bytes. Original `.pdata` defines nonleaf extents;
reviewed leaf extents end at their actual return or complete local switch.

**73 spans / 4,889 original words / 43 semantic pins / 16 tests pass.**
The tests include original baseline and in-memory mutations of vtable slots,
constructor stores/constants, the common raw read, gateway calls, wrapper
identity getter, pool retain, FX/pool writes, typed feature target, callback
inventory, and camera raster flags/depth. Mutation checks bypass whole-image
identity so they exercise the semantic checks themselves. Default operation
compares saved evidence; `--write --test` refreshes only this evidence folder.

`build/effect-finalizers/evidence.json` contains all 25 rows, nine types,
complete function hashes/edges, exact query names/pointers/call sites, and
the reader/write inventory. `disassembly.txt` is the byte-checked original
listing; `checks.log` records the tests. Static immediate-name slices are
explicitly bounded; `826B6E60`'s caller-supplied technique is not guessed.

This work changes only this new document and `build/effect-finalizers/*`.
It neither edits nor executes production, original binaries, shaders, CMake,
configuration, the frozen pool fixture/runner/doc, or prior caller evidence.
It does not validate build147 execution, all registration callbacks, native
setter semantics, draw integration, or normal cleanup. The actionable boundary
is settled: retain the manager guard now; replacing the common reader and
shadows setters is separate implementation work with explicit contracts.
