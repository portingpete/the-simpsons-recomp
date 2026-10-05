# Original matrix group counts and palette ownership

The former native `groups<=64` checks confused the number of authored byte
ranges with the number of matrices selected for a shader. A bounded
original-source case uses **65 ranges to select two matrices**. Three
independent whole-original skin family regressions now reproduce that valid
input's rejection after the actual reader, relocation and geometry setup.
The source repair accepts signed-positive group counts while retaining range,
selected-palette, mapped-span and observed logical-owner bounds. **The repaired
65-group cases passed3/3 in both Native and Release, within full497/497 passing
suites in each configuration.** No stock or gameplay encounter is claimed for
this constructed original-valid input.

The independent [original CPU probe](K:/SimpsonsNativeCopy/build/restrictive-audit/matrix-group-cpu-probe-20261002/README.md)
now passes **72 checks**. It executes the immutable `826FE710` and original
memcpy with one, three and 65 groups, checks exact two-matrix results, genuine
scratch initialization, caller/TLS preservation and normal probe-owner release.
Its independently mapped composed palette is an input to the helper; it does
not execute skeleton composition, geometry setup, material traversal or a draw.
This CPU probe does not certify the source repair or the whole draw. The new
whole-original baseline below proves the restrictive native rejection.
Readable mapped pages alone cannot qualify truncated or retired observed
allocation owners.

The [source verifier](K:/SimpsonsNativeCopy/tools/analyze_matrix_group_contract.py)
pins the original image SHA-256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`, complete
producer/consumer/upload/release functions, three original matrix descriptor
arrays, and instruction words. Its
[source report](K:/SimpsonsNativeCopy/build/restrictive-check-audit/matrix-group-contract-source-20261002.json)
passed **12,108 independent original span-byte mutations**, eight bounded range
models and six malformed or unqualified model rejections. These are source and
arithmetic checks, not PPC execution or native pixels.

## Original range transport and use

The 36-byte submesh row carries group count at **+28 decimal (+1C hex)** and
range pointer at **+32 decimal (+20 hex)**. Each range is two unsigned bytes:
`first matrix, length`. The group count is a full serialized word. No semantics
are assigned here to metadata's unrelated literal +18/+1C fields.

`826FE710..826FE7C8` has two paths:

- Exactly one group returns the contiguous alias `composed + 64*first` and
  returns its byte length as the selected matrix count.
- Other signed-positive group counts traverse every byte pair, copy
  `64*length` bytes to `82D63070 + 64*selected_so_far`, and sum the lengths.
  Zero-length pairs advance the table without adding matrices. A zero or
  signed-negative count returns a zero count; those empty-palette paths are
  outside this bounded shader-use proof.

There is no 64-group comparison in the original helper. The copy source and
destination for this candidate are 64-byte aligned. The original memcpy's
zero-byte path reaches `82A3CE34 -> 82A3CE54`, skips its data loads/stores and
returns the destination. Cache touch hints remain in the original routine.

The complete original immediate skin loop `82701638..82701938` composes the
authored matrices from metadata +24/+28 hex at `827016A4..AC`. When there are
more than 64 authored matrices, `827017EC/F4` loads the range pointer and count,
`82701800` invokes the helper, and `82701814/1C` uploads its **returned matrix
count** through `826FDBE0`. Material application and commit retain that selected
palette; `827018C0` submits the ordinary submesh draw arguments. Group count
does not replace the index count or affect the draw packet.

For a nonzero supplied count of two, `826FDBE0` selects the dynamic count and
uploads two matrices. A zero supplied count instead selects the reflected
array extent of 64. The helper's zero-count return therefore cannot be used to
justify empty-palette native admission.

The unmaterialed skin path follows the same contract at
`8270041C/24/2C -> 826FE710`, `82700438/40 -> 826FD060`, palette commit
`82700458`, then draw `82700470`. The character shadow path does so at
`82706410/18/20`, upload `8270642C/34`, commit `8270643C`, then draw
`82706454`. Full function spans are pinned independently.

## Bounded candidate and limits

Use an authored composed palette of 65 matrices. Store one pair `(63,2)` and
64 pairs `(65,0)` in an independently owned **130-byte table**. The result is
matrices 63 and 64, copied as **128 bytes**, with returned count **2**. A
one-pair table `(63,2)` returns the same matrices through the alias path.
Both fit the original selected shader palette and preserve the adjacent flags.

The original shadow/base/textured FX descriptor arrays have the parent
`00200102/03000041` and exactly 64 numeric matrix children. The selected matrix
limit of 64 is therefore separate from the range count. The grouping scratch
starts at `82D63070`; its once flag is at `82D64070`, 4,096 bytes later.
Composition starts at `82D64080`; its once flag is at `82D68040`, 16,320 bytes
later. Staying below the latter flag permits 255 composed matrices. This
adjacency is a conservative overwrite boundary, not a recovered authoring-tool
declaration or proof that every count above 255 is an original-valid input.

The repaired canonical `boneGroupCount` in
[engine_effects.cpp](K:/SimpsonsNativeCopy/runtime/engine_effects.cpp) requires
`1 <= groups <= 0x7FFFFFFF`, matching the original helper's signed-positive
word test. `skinBoneGroupCount` delegates to it. The selected count remains
1..64 and composition remains65..255 for grouped use. Each byte pair requires
`first <= bones`, `length <= bones-first` and `length <= 64-selected_so_far`.
Zero-length pairs therefore consume transport bytes but no shader slots.

`2*groups` fits a32-bit byte extent for this admitted count. The checked mapped
span still rejects unmapped data and address overflow. When the existing
allocator observer knows a live owner at the table start, a64-bit comparison
requires the entire span to fit that allocation's remaining logical bytes.
`allocationSpan(start)` looks up the start independently of the requested
extent, so an overlong request cannot hide its smaller owner. Static or mapped
input with no observed heap owner retains its checked mapped-span path; the
repair does not impose an unproved heap-only producer requirement.

Both snapshot producers capture the optional owner address, logical extent
and generation **before** range validation and byte copying. They revalidate
the same captured owner afterward, without refreshing its generation, then
retain the exact row words and range bytes for later traversal/upload checks.
A closing owner is rejected. A new or retired owner, changed table identity,
or changed range bytes fails before consuming the captured palette.

Independent source review of that ordering and arithmetic found no new
admission gap in this bounded contract. This is source review, not execution
of live generation retirement or the repaired draw.
The unrelated submesh count `<=65535` and raw material index `<65536` gates
also have no corresponding comparison in the inspected original transport.
Their full admission bounds remain unqualified until owner extent and original
address-wrap behavior are established independently.

## Owner setup and retirement

The packaged native reader `8282F618` reads a little-endian 12-byte prefix with
alignment and two word-sized pool byte extents. It allocates and reads those
owners independently, then calls `82831280` to relocate declared pointer
fields. A relocation target is a field in the first pool; selector zero adds
the first pool base, and a nonzero selector adds the second pool base. The
range field must have such a relocation and its complete `2*groups` byte span
must fit its selected owner. The original relocation code itself does not
validate that span; the audit requires it before regarding a model as safe.

The `3C43A23D` visitor reaches `8273B760`; setup checks metadata version
`00030002`, creates embedded geometry headers and obtains the declaration.
It does not clamp or rewrite the group count. Range tables are borrowed from
the native pool during geometry use. `8282F878` releases the first and second
pool allocations, not individual byte-pair tables. The static matrix scratch
is reused and has no per-mesh allocation to free.

The optional `groups65` case in `tests/test_skin_pass.cpp` retains real inverse
bind, pose, texture, catalog, material and camera owners. Actual
`823F9598(3,1,descriptor)` constructs its memory stream and `823F94A0` closes
it. The source-qualified initializer uses the real manager or original
singleton getter`8268E7F0`, then actual`8282F4D0(manager,0)`, following the
argument sequence at`82862068..8286208C`. It writes no fabricated allocator
global or callback. The baseline has now executed this initialization.

The first pool is256bytes. Its original relocation row at+28 declares the
submesh range-pointer field at+`0xA0`, selector1. The submesh lies at+`0x80`;
relocation adds the actual second-pool base to its serialized zero pointer.
Separate original reader calls create one group in a2-byte second pool,
65groups in a130-byte second pool and an identical independent130-byte table
for identity rejection. The table ends at its actual logical owner end.
Actual`8273B760` creates geometry headers and both cached declarations.

The valid65-group opaque draw is attempted before the new malformed cases,
so the historical64-group guard cannot be hidden by an earlier rejection.
After repair, the case must compare opaque and alpha pixels with the original
one-group alias, observe actual composition/grouping/upload/material callbacks
and preserve nonvolatile ABI and authored bytes. At the genuine mesh entry,
66pairs need132bytes and must exceed the130-byte owner; out-of-composed and
selected-total65 pairs must fail before composition/copy. At the genuine
grouped bone setter, the same-byte independent table identity and an authored
range shift producing identical matrix bits must still reject.

Normal cleanup must end the FX and camera, run paired`82700A78`, `82701118`
and `826B7600`, then retire both pools through`8282F878`. The fixture checks
retired allocation ownership and performs a fresh original reader allocation
with identical bytes and a new generation. Address reuse is recorded only if
observed; this last check is allocator-only. Live snapshot generation
rejection is explicitly **unexecuted** because the positive draw does not
manufacture an abort or native rollback after retiring its borrowed table.

Existing whole-original fixture entry points are `layout` and `run` in
[test_skin_pass.cpp](K:/SimpsonsNativeCopy/tests/test_skin_pass.cpp).
Its three-group case already includes an original empty trailing pair, and its
observer checks the actual original composition and grouping returns. The
paired cleanup helper is
[effect_draw_cleanup_helpers.h](K:/SimpsonsNativeCopy/tests/effect_draw_cleanup_helpers.h).
The matching unmaterialed and character paths are in
[test_mono_pass.cpp](K:/SimpsonsNativeCopy/tests/test_mono_pass.cpp) and
[test_shadow_camera_pass.cpp](K:/SimpsonsNativeCopy/tests/test_shadow_camera_pass.cpp).
Their prior executed cases do not grant execution credit to the new 65-group
case. Backend immutable upload retirement remains a separate evidence scope.

## Immutable native rejection baseline

The baseline ran `OriginalSkintexturedGroups65Pass`,
`OriginalSkinbaseGroups65Pass` and `OriginalSkindualGroups65Pass` independently.
Each logged all three genuine pool creations with exact2/130/130-byte logical
second owners, memory-stream close and preserved ABI, then reached the valid
65-group draw. They failed with
`Original skin submesh matrix group extent is unqualified`, after16,972,
17,050 and16,984 fixture checks respectively. Their one-group opaque and alpha
baseline draws preceded that failure. No repaired pixels, new malformed
rejections or normal cleanup credit is taken from this failed run.

The preserved
[JUnit baseline](K:/SimpsonsNativeCopy/build/restrictive-audit/controller-mission-matrix-baseline-native-20261002-v1-tests.xml)
has SHA256`890917c5490f01142f8836e4b2fc1dc17c39005ae495412d4cb30cf29bdbfae4`.
The matching
[source scope](K:/SimpsonsNativeCopy/build/restrictive-audit/controller-mission-matrix-baseline-v1-native-20261002-scope.json)
records1,051 actual configured inputs, fixture SHA256
`e54628e20f98d4811684d5c02dbf2ed8faa08894b036c7f599fb0bf71da88235`
and executed `SkinPassTests.exe` SHA256
`d3fbad4c64e40750ff54210ae97cc7b0420b5b0fffb587ee47d561ab3dd59e6b`.
The exact historical
[production source](K:/SimpsonsNativeCopy/build/restrictive-audit/controller-mission-matrix-baseline-engine-effects-20261002.cpp)
has SHA256`ceab410e32ab2cdc2214e365bb089509fd40a3ce0c7060aa4e290554bfb007ce`;
its copied AOT manifest is preserved separately. The immutable
[source-ready fixture receipt](K:/SimpsonsNativeCopy/build/restrictive-audit/groups65-skin-source-ready-20261002.json)
pins39 original setup/relocation/release instructions. Rebuilding the source
repair cannot update any of these old execution receipts.

## Repaired Native and Release execution

All three independent family cases passed in3.59seconds against the repaired
Native build. The unchanged fixture executed original skeleton composition,
one-group alias and65-group copied selection, matrix upload, material callbacks
and nonvolatile ABI checks. Both opaque and alpha pixels were equal between
the1-group and65-group forms. The malformed66-pair logical overrun,
out-of-composed pair, selected-total65, changed same-byte table identity and
changed range producing identical matrix bits all rejected in their genuine
original preflight/upload contexts.

Each case ended the original camera, released declarations, FX and caches
through their paired original helpers, and retired both table pools through
`8282F878`. Allocation ownership became stale as expected. A subsequent
original reader allocation preserved identical bytes and had a new
generation. All three report `address_reused=0`: no same-address reuse was
observed. These post-cleanup generation checks remain allocator-only. The live
snapshot generation-rejection branch remains unexecuted, and backend mesh
cache retirement remains owner-resident/full-GPU-retirement-unproved.

The repaired
[Native JUnit](K:/SimpsonsNativeCopy/build/restrictive-audit/controller-mission-matrix-repair-groups65-native-20261002-v1-tests.xml)
has SHA256`d12f02cee9e695ac3789e6deb6bcbf06586c9adc6b6599a4f8d36d16a56d2900`.
Its preserved LastTest log has SHA256
`5ec3b17874d5ea099f8d1c503e7dcad1f1133a8fcdb9a921a7cae1378c9859fc`.
The textured/base/dual cases report12,937,971/12,938,113/12,937,983 checks.
The executed Native `SkinPassTests.exe` has SHA256
`2f730197ae86f0887d0e5e3c74fb583fdbebfb591c4ee0768b1d6ae0e1445cc6`;
the repaired production source has SHA256
`e0fb9ef30057af0b0a8a7a437d0e02d569c754bb24e7f57e644d5e45d0686210`
and its generated manifest SHA256 is
`e2baf93198ff7f8adcd53a57a3b393a316aba739189f53adbc4bad79ac00094a`.
These focused Native results retain their original snapshot identity. The
later complete repaired Native and Release suites each passed497/497, in
396.90seconds and393.80seconds respectively. Each separately executed all three
65-group families and preserved the same original reader/use/cleanup contract.
Their receipts explicitly retain `address_reused=0`,
`live_generation_rejection=unexecuted` and
`backend_mesh_cache=owner_resident full_gpu_retirement=unproven`.

Both final compiled scopes contain1,219 source inputs,624 configured translation
units,641 objects with observed compiler dependencies and497 observed
non-system dependencies. The
[Native scope](K:/SimpsonsNativeCopy/build/restrictive-audit/controller-mission-matrix-repair-v2-native-20261002-scope.json)
has SHA256`6afbe8f0a5454fae84a21fb282116e208ba48596df90aaabee9f29147d267694`;
the independent
[Release scope](K:/SimpsonsNativeCopy/build/restrictive-audit/controller-mission-matrix-repair-v2-native-release-20261002-scope.json)
has SHA256`351c835074cde75ebf309ddac0f5b736c0d467ed7d464226f649c097a95ebe90`.
The final
[Native execution receipt](K:/SimpsonsNativeCopy/build/restrictive-audit/controller-mission-matrix-repair-full-native-20261002-v1-receipt.json)
has SHA256`aec4a6da6c20ecccd9f1b5d061b56bbd5ab345e31bc39b7638019920ce6bd822`;
the final
[Release execution receipt](K:/SimpsonsNativeCopy/build/restrictive-audit/controller-mission-matrix-repair-full-native-release-20261002-v1-receipt.json)
has SHA256`5038f10c18ecc4664ff656d57f3ac532bd44da80c118d71acba75c806574e3d4`.
Each binds its unchanged source and executable hashes to the preserved JUnit,
full stdout and LastTest log; prior failed snapshots are not rewritten.

The same final suites also execute four direct `allocationSpan` cases through
actual original creators`8269BD70/8269BDE0` and free`8269BEB0`. A forwarding
observer checks the closing owner before and after genuine lower free`828587F0`,
whose immutable body executes exactly once. Base/interior/last-byte queries
retain the requested logical extent; mapped padding and the logical end do not
widen it. After original free the span is absent, and a fresh original
allocation has a new generation. These independent allocator cases report
**natural_address_reuse=1**, with preserved payload/caller ABI and normal
observer release. This differs from the whole-skin cases' absent address reuse.

The direct callback/free/reuse result is allocator-only. It does not exercise
retirement of a table while an active FX snapshot still retains it, concurrent
pinning/retirement, or complete GPU upload/cache retirement. Those remain
unproved. Existing stock catalog and gameplay claims are unchanged.

## Packaged observations

The provenance-corrected authoritative census is
[packaged-mesh-vfx-source-complete-v2-20261002.json](K:/SimpsonsNativeCopy/build/restrictive-check-audit/packaged-mesh-vfx-source-complete-v2-20261002.json),
SHA-256 `13e81e770f5d96048067efcf77339fdf2fd0c56a9f20bcb7f5c7e6cfef166291`.
It records 25,990 geometry records and 29,319 submeshes; every initial raw group
count is zero. The largest observed submesh count is 19 and raw material-index
word is 23. These initial packaged values do not establish the complete valid
producer range or later runtime fixups. The new verifier's optional inventory
reread all 5,533 mesh occurrences, found authored counts **0 through 64**, and
validated every nonzero authored count's metadata +28 hex byte-map relocation
and complete owner span: **15,172 nonempty maps**, with zero palette
qualification failures. Their largest observed byte value is **246**. These
bytes select source skeleton joint matrices before composition; they are not
indices into the selected 64-matrix shader palette. The source joint-array
owner extent is a separate contract and is not inferred from that observation.
Its
[matrix field report](K:/SimpsonsNativeCopy/build/restrictive-check-audit/matrix-group-contract-20261002.json)
has SHA-256 `648879232ca461a78f5d8e0833d4a962ffa29592fade80a1c52bc15412705428`
and preserves archive, entry, payload and owner hashes. This remains an offline
field census, with no native setup, use, retirement or gameplay credit.
