# Submesh and material table limits

The reviewed original loaders and consumers do not establish the native
65,535-submesh/compiled-material caps or the 65,536 RenderWare material-selector
cap. Counts and selectors are DWORD fields, with different signedness rules
and different owners. The immediate skin path now completes an independent
65,536-row original create/use/release regression for three material families
in both Native and Release. Compiled-material and RenderWare selector
beyond-cap paths remain source-only candidates. The preserved source/offline
report below is unchanged; new native receipts have their own scope.

The independent verifier is
[analyze_submesh_material_contract.py](../tools/analyze_submesh_material_contract.py).
Its preserved report is
`build/restrictive-check-audit/submesh-material-contract-source-v2-20261002.json`,
SHA256 `ce16216c1a07081632889f33510c54be1b64b4baf3cafb3ab5c14204f878b215`.
It pins 32 complete original function spans and 38 instruction words against
the immutable image SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`,
rejects 8,788 independent changed span bytes and exercises 15 conservative
owner/arithmetic models plus 11 malformed extent/index controls. These checks
do not execute the PPC functions, allocator, shaders or destructors.
The earlier v1 report and its exact tool copy remain preserved; v2 corrects
the largest model's last-row selection, adds index==count/truncated-slot
controls and explicitly includes the 4,096-row static-shadow diagnostic cap.

## Distinct fields and owners

| Field | Original interpretation | Owner and required bound |
|---|---|---|
| Metadata+10hex | Full DWORD submesh count | 36 bytes per row in a relocated EA33 pool; no-wrap complete span |
| Metadata+14hex | Relocated submesh row pointer | First or second declared EA33 byte owner |
| Submesh+0 | Full DWORD RenderWare material-list selector | Geometry+24hex points to material pointers; selector must fit the actual pointer-table count/owner |
| Submesh+4 | Unsigned compiled collection index | Metadata+34hex collection DWORD0 is count; pointer array starts at+Chex; original lookup requires index<count |
| Compiled material+4 | Unsigned parameter count | Material+14hex points to12-byte parameter rows; separately relocated and bounded |
| RwGeometry+28hex | Signed DWORD material-list count | Independently allocated four-byte pointer array, with original material references |
| RwMaterial+18hex | 16-bit per-object reference count | This limits safe repeated aliases to one object; it is not a table-count declaration |

The frozen packaged reader calls the raw submesh+0 field `material_index`
and the raw+4 field `material_selector`. This new report preserves that
reader and uses explicit names `rw_material_list_selector` and
`compiled_collection_index` in its additional rows. Confusing these two
fields would apply a range or ownership check to the wrong table.

Global 82D6D814 is also easy to misread. Original 82727AF8 calls
823DCE60 to register a 40-byte EA13 plugin on RenderWare materials and stores
the returned plugin offset in 82D6D814. It is not a global header-allocation
base. Original draw callers load a material pointer from the geometry's list,
then add the plugin offset before reading flags. The native local names
`offsets` and `headers` describe that arithmetic poorly. A new qualification
must validate the actual list slot and live material/plugin span.

## Producer and consumer evidence

Reader 8282F618 reads the two EA33 pool byte sizes as swapped DWORDs and
allocates them through 8282F4E8/8282F558. Relocator 82831280 publishes pointers
according to the declared first/second owner selector. Visitor 828311D0 passes
type 3C43A23D records to geometry setup 8273B760. None of these paths truncates
submesh or material counts to 16 bits. The concrete allocator maximum and
offline authoring-tool maximum remain unresolved; the DWORD interface alone
does not qualify all 32-bit extents. Original 8282F878 frees the containing
pools. Individual submesh and compiled-table pointers are borrowed views of
those owners.

Five complete draw consumers—826FF4C8, 82700318, 82701220, 82701448 and
82701638—take signed subset arguments. A negative start selects zero; a
negative length selects the metadata count. The end clamp and start/end
comparison use `cmplw`, so the metadata count is unsigned in those loops.
They advance 36 bytes per row and decrement the full remaining DWORD.
Character-shadow 82706378 instead tests the metadata count using `cmpwi` and
only enters for a signed-positive value. Those distinctions must survive
any wider admission. Wrapped subset sums or row-address arithmetic remain
outside the conservative model.

Compiled collection lookup 82831C18 compares the supplied index with the
collection count using `cmplw`, returns null for index>=count and otherwise
loads slot 4*(index+3). Count accessor 82831C40 returns DWORD 0 unchanged.
Setup 8273B760 optionally calls 8273B668 on metadata+34hex; setup walks full
unsigned collection and parameter counts. Relocator 82831EF0, inverse
serializer 82831FE0 and parameter cleanup 82831E18 also use unsigned DWORD
loops. No reviewed compiled-table path provides a 16-bit cap. The original
callback start/count fields extracted by 82700498 are 5 and 3 bits; those real
field limits are separate and must stay qualified.

RenderWare material-list stream reader 823D38D8 reads a signed DWORD count
and a 4*count index map. Reserve 823D34A0 allocates 4*requested count; append
823D3558 compares signed capacity/count and grows by 20 entries. The list
publishes its count as a DWORD. A negative index-map item loads a new
material; a nonnegative item selects a previously published pointer and
retains it. Repeated aliases affect each material's 16-bit reference count,
so a large list cannot inherit lifetime safety from its byte extent alone.
List cleanup 823D32C0 retires every positive-count reference and frees the
pointer array; geometry destruction 823D0568 invokes that cleanup.

## Complete shipped field inventory

The new report independently reopened and hash-verified all 5,533 mesh
occurrences using the unchanged v2 packaged reader
SHA256 `176bd013bc070bec59c3be5a5a8fd63bf9926fa1d763d4045c477957ba61b1d9`
and census SHA256
`13e81e770f5d96048067efcf77339fdf2fd0c56a9f20bcb7f5c7e6cfef166291`.
Each compiled collection, material header and nonempty parameter table was
resolved only through its actual declared relocation and checked against
the selected original pool. Complete table/header hashes and raw asset
archive/entry/payload identity remain in the rows. A local failure would be
retained independently, without preventing later materials or archives from
being inspected. There were zero archive or local qualification failures.

| Recovered shipped field | Population | Maximum |
|---|---:|---:|
| Submesh counts |25,990 geometry records |19 |
| RenderWare material-list selectors |29,319 submeshes |23 |
| Compiled collection indices |29,319 submeshes |18 |
| Compiled collection counts |25,990 collections /29,319 slots |19 |
| Compiled parameter counts |29,319 material headers |22 |
| Serialized RenderWare material-list counts |20,490 lists |24 |

Every stock compiled count equals its associated submesh count in this
inventory. That is an observation, not an original producer invariant.
Every recovered stock value lies below the reviewed native 16-bit caps.
The maximum submesh/compiled count 19 is in `lodmodel1.rws`,
`gamehub/gamehub/zone13.str` entry 5, metadata offset 797488, named payload
SHA256 `9b791dbc1dcadc2a1650a63abe9ebcae073d041c98cca99e795ef57e8cb09e7a`.
The selector 23 is in the same payload at metadata 994384; its serialized
material list has 24 entries. The duplicate `spr_hub` packaging occurrence
remains independently identified. Offline nesting and count observations
do not grant a runtime object/record association, rendered material
selection, native lifetime or mission gameplay credit.

## Actionable candidates and remaining qualifications

The preserved source report pins 15 expressions in `runtime/engine_effects.cpp`
at source SHA256
`e0fb9ef30057af0b0a8a7a437d0e02d569c754bb24e7f57e644d5e45d0686210`.
Nine are runtime 16-bit cap candidates, two are only the count component of
compound callback guards and four occur in capture diagnostics. This is
a read-time source receipt; it does not assert equivalence to a compiled
executable or extend the frozen v4 matrix.

The candidate repair is to replace each arbitrary cap with checked complete
owner arithmetic, while retaining original signedness and selected-row,
caller, shader, cache and phase associations:

- Submesh tables require checked 36*count and row addresses in their actual
  declared byte owner. The conservative no-wrap arithmetic ceiling
  floor(FFFFFFFF/36)=119,304,647 is not an allocator or authoring maximum.
- Compiled collections require checked 12+4*count, index<count and live
  relocated material/parameter spans. A larger count does not qualify
  missing pointers or a malformed parameter callback range.
- Submesh+0 requires a checked 4*selector lookup inside the actual RenderWare
  list, followed by a valid material/plugin span. Preserve the separate
  compiled index in submesh+4.
- Compound callbacks retain row alignment, selected offset, original caller
  frame and cache/owner comparisons. Only their count cap is under review.
- Capture policy must stay distinct from validity. The static shadow
  diagnostic additionally caps submesh count at 4,096. First rigid capture
  can introduce a fresh rejection after upload; rejected rigid capture can
  replace the original renderer failure with a diagnostic assertion.
  These are diagnostic failure-attribution risks, not producer bounds.

Three independent whole-original paths are identified. The first reproduced
a valid native rejection and now completes large use and original retirement
under the separate 500-suite repair scope below. The other two remain
source-only candidates:

| Candidate | Complete table extent | Original use to reproduce |
|---|---:|---|
|65,536 submeshes |00240000 bytes | Original last-row subset and independent default full loop, with genuine skip flags isolating the selected draw |
|65,536 compiled materials |0004000C bytes | Original setup8273B760→8273B668, then valid final slot through82831C18 and complete material continuation |
| Row+0 selector65,536 into65,537 RenderWare pointers |00040004 bytes | Original stream/reserve/append and last pointer/plugin lookup, with row+4 kept independent |

The two remaining candidates need authentic allocator/reader publication,
complete selected use and original cleanup. Large RenderWare lists need distinct objects or bounded
alias counts to avoid reference-count overflow. Malformed truncated owners,
wrapped row/slot addresses, index==count, corrupted relocation provenance,
stale owners and retained callback associations must still reject. A later
valid independent case must run after the malformed cases. No admission or
validated lifecycle is claimed for those two paths until they execute.

The earlier build-local candidate for the first row is preserved at
`build/restrictive-audit/skin-submesh65536-candidate-20261002/test_skin_pass-submeshes65536.patch`.
That exact artifact remains unapplied and unexecuted. Its receipt and complete
candidate source are alongside it; an adapted fixture was subsequently
applied and run under the separate baseline scope below.
The candidate allocates, reads and relocates both 1-row and 65,536-row tables
through the original stream path. Its large first pool declares 00240000 row
bytes plus an 80hex-byte prefix, with 65,535 skipped material rows and one
draw row; the fixture composes only two matrices. It is designed to verify
original declaration setup and opaque/alpha equivalence, then malformed
count/shifted-owner/index controls, a later valid case and paired
declaration/FX/cache/pool/source retirement.
The existing metadata borrows the reader's relocated row pointer, matching
the source-qualified group fixture's ownership pattern. Its source-only
design did not grant lifecycle credit, and its receipts are unchanged.

## Independent original valid rejection

Three independent Native cases now reproduce the cap rejection for base
`82006348`, textured `8200FB98` and dual `8201CD48`. Each passes original
`8282F618` pool allocation, `82831280` relocation and stream close, with a
complete 2,359,296-byte row span inside its 2,359,424-byte first-pool owner.
Each then completes the one-row opaque/alpha baseline before the first
65,536-row call fails with `Skin submesh extent is unqualified`. The three
cases take 1.85 seconds in aggregate. This is an executed valid rejection,
without large-use, malformed-case or original-retirement success.

The [baseline frontier report](../build/restrictive-audit/submesh65536-wave-20261002/baseline-frontier-evidence-v2-20261002.json)
has SHA256
`9e03c1e2b15a3c16780510a2dfb05e9d08013b22e0c7f4b62f4235fc30232483`.
It independently checks all three failed JUnit cases and exact positive
setup/baseline markers. Its compiled scope is
`controller-mission-matrix-submesh65536-baseline-v1-native-20261002-scope.json`,
SHA256 `d3df437c86c2876498926e18e0e07f9f9c0cfd8d524df0c3a1f262c729498f9f`,
with 1,219 inputs, 219 executable identities and compiler dependencies.
The complete baseline JUnit SHA256 is
`9680932d3d524f600546bac8d0deac02ef2871e8412a0e0b63c23ec8d3366569`.
The executable is `SkinPassTests.exe`, SHA256
`da1db81dd0446eb7674c155fecf3eff35cf561c08ed22bcbe5f4623fa34a488e`;
the exact preserved fixture is `test_skin_pass.after.cpp`, SHA256
`cbeeb68f7776035b72026d64ee4a2e3ee31f28bfb9df0600bef6667bdc6d30fb`.
Preserved production code still matches the source report's `e0fb9ef...6210`
identity. Later fixture changes are explicitly distinct from this baseline.

## Repaired original lifetime and malformed frontiers

The executed repair is limited to the immediate skin table path. It
checks `36ull*count` and end-address arithmetic before narrowing, requires
the complete row span to fit any observed allocation containing its first
byte, then checks the mapped span. Existing static or unobserved owners
retain mapped-range qualification. Material counts, other geometry families
and diagnostic caps remain separate unresolved checks. The malformed count
case updates both metadata and the original `r7` argument, so the owner-bound
failure is not hidden by an earlier ABI association rejection.

The [Native full receipt](../build/restrictive-audit/submesh65536-wave-20261002/full-native-v1-receipt.json)
has SHA256 `bcd9cc22af1a916eb72bd6202f4770b128f767fbc4eccb7d943308a63cb9b7fa`;
the [Release full receipt](../build/restrictive-audit/submesh65536-wave-20261002/full-native-release-v1-receipt.json)
has SHA256 `9711a826a1af73848f1ea9c2dd50ad5d691dad3f498af4c7349184e6ea9db5a3`.
Both complete aggregates pass 500/500, in 447.12 and 437.34 seconds. Their
strict binder confirms all frozen source, executable and DLL hashes remain
unchanged against each configuration's 1,219-input scope. Native scope is
`controller-mission-matrix-submesh65536-repair-v1-native-20261002-scope.json`
(`6f31a6f7eac8cc23004eb259a3c4249261779011d1ca536e3a0d3d33a39caacb`);
Release scope is the corresponding `-native-release-` file
(`853270c33c7f0fb3d67d1334b11351b4938dfa65ffa61e9bb0870f30e7ef550a`).

Each base/textured/dual case uses original first-pool allocation/reader and
`82831280` relocation, with 65,535 genuinely skipped rows and one draw row.
The composed palette stays at two matrices. Both opaque and alpha render
equivalently to the one-row baseline, then complete original camera end,
declaration retirement, both FX/cache cleanup paths, pool release `8282F878`
and stream-source release `8269BF10`. Released allocations are stale, and a
later valid case succeeds after the four malformed probes.

| Malformed original input | Exact rejected frontier |
|---|---|
| Count 65,537 with matching `r7` | Complete rows exceed the observed allocation owner |
| Shifted row view with count 65,536 | Complete rows exceed the observed allocation owner |
| Compiled collection index out of range | Skin submesh material index is out of range |
| Wrapped RenderWare material selector | Skin material offset table overflows |

Each family has four exact prevalidation failure receipts at original caller
`82740294`, retaining asset source, parameters, mission/action, ownership,
logical owner span and raw instance generation. PPC state, host CSR and
LastError remain preserved; generations are excluded from the stable key.
These synthetic original-path cases receive no packaged-mesh or gameplay
credit, and they do not update the frozen 497 v5 catalog matrices.

The original full DWORD loop establishes the count representation, not all
address aliases or an allocator-wide maximum. The conservative end-address
rule remains narrower than a general 4-GiB address-space theorem. This
repair does not prove that a different in-range allocation is an equivalent
row owner, retain a row generation across use, pin concurrent ownership, or
retire backend GPU caches. Count zero and an exclusive end address of
`0x100000000` also remain outside this conservative admission. Those
qualifications remain explicit after the new 65,536-row regression passes;
backend mesh caches are owner-resident and full GPU retirement is unproved.
The earlier independent failing scope and source-only candidate remain
unchanged, without borrowing credit from the repaired executable.

```powershell
python -B tools/analyze_submesh_material_contract.py --inventory --output build/restrictive-check-audit/submesh-material-contract-source-new.json
```

Related source scopes: [packaged fields](packaged-mesh-vfx-field-audit.md),
[matrix groups](restrictive-matrix-group-audit.md),
[primitive transport](restrictive-mesh-primitive-audit.md) and
[particle owner plan](original-particle-owner-regression-plan.md).
