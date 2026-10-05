# Original FX reflection gateway contract

This is an offline contract for replacing `826B5168`, reached by 23 of the 25 registered profiles, and for retaining the original quad lookup `826B7570`. It covers the CPU reflection outputs and the callees needed for the common gateway to return. It does **not** close the manager finalizer `826B7218`, quad/shadow setters, shader compilation, declaration binding, pass application, or drawing. Keep those guards in place.

The original flat image is `analysis/simpsons.pe`, base `82000000`, 15,466,496 bytes, SHA-256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`. The registration/serialized metadata authority is [the complete catalog](../analysis/native-effect-catalog.json), SHA-256 `031241d1bff10c771433ce0c5154c05a93628a4b83b114c0d0bfcfdadea2a43b`. The earlier [finalizer contract](native-effect-finalizers.md) remains the authority for dispatch outside this gateway.

## Reproducible evidence

All new proof files are in `build/effect-reflection/`. They read the original image and use the existing host disassembler; they do not build the port or modify the original image. The evidence contains instruction words, span hashes and derived metadata, not copied FX/shader assets.

- `verify.py --check`: common gateway, base CPU storage, wrapper lookup, quad selection, feature predicates and bounded shared-handle remap window. `gateway-evidence.json` pins 16 spans / 629 words; `gateway-original.txt` has the checked listing; `gateway-plan.json` contains ordered local handles and first-two-pass selection for all 25 rows.
- `parameters_verify.py`: annotation, descriptor, parameter list and classification proof. `parameters-evidence.json` contains all 25 rows' annotations and query outputs; its checked code is in `parameters-disassembly.txt`.
- `passes_verify.py --test`: pass usage/binding, output masks and mutable-mask side effects. `passes-evidence.json` and `passes-disassembly.txt` identify the checked spans. Synthetic instruction tests are bounded verification of the recovered algorithm, not a runtime emulation design.
- `corpus_verify.py`: checks the association between the three proofs, then compares `gateway-corpus.json`. This compact handoff contains the 23 gateways' scalar fields, all 525 parameter rows, 194 classification rows, 15 selected light arrays and 45 pass records. It preserves the unknown classification word explicitly. Run it after the component verifiers.

The detailed [parameter contract](../build/effect-reflection/parameters-review.md) gives descriptor/value formulas, exact annotation associations and individual incidental reads. The [pass contract](../build/effect-reflection/passes-proof.md) gives the complete native mask algorithm and all 25 profiles' projected masks. These are parts of this contract, with their stated bounds; the compact JSON is a serialized-local projection, not an execution capture of a relocated SDK FX.

The common gateway span is `826B5168..826B54CC`, 868 bytes, SHA-256 `8acf25e88a434bdef8fa873965f54d93b6217c112dbee80006c2a374102dfd9e`. Evidence spans use an exclusive end address. Byte/word verification is independent of the prose and checks semantic mutations without relying only on a changed whole-image hash.

## Entry and existing CPU storage

`826B5168` receives `r3 = T`, the original typed effect object. `T+1C` is its effect identity, `T+18` its original wrapper, and `T+10` its original manager. It preserves the nonvolatile ABI and returns `r3 = 0`. The first unsafe operations for an opaque native FX ID are `826B517C: lwz r10,0x2AC(F)` and `826B5180: lwz r18,0x20C(F)`, after loading `F = [T+1C]` at `5178`. Replace the whole gateway before those loads; substituting a few SDK query entry points is insufficient.

The original base constructor `826B4F60` already owns the CPU buffers:

| Object field | Allocation and initialization | Gateway purpose |
|---|---|---|
| `T+28` | 1,536 bytes = 64 × 24; entirely zeroed | Ordered top-level parameter rows |
| `T+30` | Allocation 676 bytes; prefix count 24, public pointer is allocation + 4; 24 × 28-byte rows | Scope/type classification rows |
| `T+38` | 480 bytes = 4 × 120; entirely zeroed | Selected light-array child rows |
| `T+48` | 96 embedded bytes, entirely zeroed | Two 48-byte pass records |

For `T+30`, the constructor initializes **only each row's word at `+14` hex**; it does not clear the other six words. `T+34`, `T+3C` and `T+40` start at zero. Do not replace the buffers, alter their allocation headers, or assume the entire classification allocation is zero. The original destructor `826B3E50` frees `T+30 - 4`, `T+28` and `T+38`, then releases the owned name. The embedded pass records have no separate allocation.

`T+20` is a **byte**. The gateway writes one to it before constructing pass records; a native adapter must not clear the neighboring three bytes with a word store. `T+24` receives `min(local named count, 64)`; `T+2C` starts at `FFFFFFFF`; `T+44` starts at zero.

The string hash call `827451C0` and `826B2FD8` occur before adapter creation. The latter is exactly `blr` in this image and adds no observable output. The transient SDK adapter `826B45A0` reads several fields of F, calls GetPool, and builds an annotation cache. A native replacement can decode immutable metadata directly and does not need to allocate an SDK-shaped adapter/cache. The original gateway frees the adapter's `+0` allocation at `826B54BC`.

## Enumeration identity and shared pool

Read the exact serialized `body+2A8` handle array and its `body+2AC` count. All 25 arrays contain precisely the private and local shared top-level handles, but 23 arrays interleave the namespaces. Concatenating `parameters(false)` and `parameters(true)` loses the original indices. `gateway-plan.json` records every handle, name, descriptor index and row association. The 23 gateway rows contain 525 named entries in total; the maximum per FX is 32 (`sky`), below the original cap of 64. Rows 0 (`fourtapblend`) and 2 (`particles`) do not call this gateway.

The four-name shared schema remains a four-name local enumeration even when global pool lookup can find all 11 shared names. In `82C190C8..82C19134`, the original merge reads the local count and rewrites only shared entries of the local handle array using its mapping rows; private handles stay unchanged. That window does not append names or change the count. This is a bounded statement about the checked window, not an assertion that no other code ever writes the count.

Use an explicit mapping between immutable local handles and the actual pooled storage/descriptor identity. Keep local order and top-level indices for the reflection outputs. Name lookup through the genuine CPU pool is a separate operation. Rebuilding the reflection list from a globally expanded query namespace would change `T+24`, every subsequent row index, the selected light-array handle and mask associations.

The current catalog preserves all descriptor/default bytes and the immutable body, but `EffectParameter` exposes only top-level name/handle/two descriptor words. Reflection additionally needs the serialized order, annotations, recursive descriptors and usage/binding tables. These can be decoded from the owned, SHA-pinned body; no shader instructions or fake SDK object fields are required.

## Query and classification sequence

The gateway calls `828301F8(adapter, T+28, 64, 0, 0)` and ignores its returned count. It then calls `82831038(adapter, 0, T+30, 24, 1, 0)` and stores that count in `T+34`. Classification selects scope 0 entries used by **technique 0, pass 0**, in the original top-level enumeration order. It is not a filter against whichever technique is selected later by a derived finalizer.

There are 1,173 annotation descriptors across all 25 blobs. Their actual shapes are scalar integer-word or string annotations. `F+268`, the effect-level annotation count consumed by adapter construction, is zero for every row. Parameter annotations still exist and must be decoded; `F+268 == 0` does not mean annotation-free metadata.

Specifically, 774 annotations contain integer words and 399 contain strings. Of these, 1,147 annotations are linked from named top-level parameters; the other 26 (22 `shortdesc`, four `vertexDecl`) are preserved without inventing a parameter association. Values use four-byte offset units from `body+26C`, unlike ordinary parameter defaults. A native decoder must validate this annotation grammar separately from merely owning the raw body.

Each 24-byte parameter row is `{handle, usage, evenStart, oddStart, evenCount, oddCount}`. Usage comes from technique 0/pass 0. A kind-2 array overrides both counts with its element count; a matrix leaf does not become multiple descriptor leaves. These rows contain binding metadata, not a copy of numeric defaults or texture objects.

Both parameter-row query variants call the binding helper even for zero usage subsets. The six preceding lookup-table words are pinned at `82000ECC/EEC/F0C/F2C/F4C/F6C`. For `B = binding-vector-base + 16*leaf`, the zero case reads `B-4` and `B+88`, then computes `start = word(B-4)&2`, `count = (word(B+88)&1)+1`. This is bounded original machine behavior; a C++ decoder must use checked explicit offsets, never a negative array index or an invalid host shift.

The 23 gateway rows make 1,088 such zero-subset queries in their T28/T38 projections. All source reads stay within their serialized bodies. Starts are zero; counts before array overrides are 1 in 875 cases and 2 in 213. Some count reads cross into following context headers; ten read later sky metadata. **These fields are not universally `(0,0)` or `(0,1)`.** Two hypothetical fourtap count reads fall outside its body; fourtap does not invoke this gateway, and no adjacent asset bytes are substituted in the proof.

These incidental reads need a final live-layout correspondence check before asserting equality to every relocated/merged SDK output. Owning the serialized body and proving every read bounded does not prove that moved shared vectors retain the same neighboring words. The JSON distinguishes the serialized addresses from runtime heap addresses. It contains no unresolved numeric parameter word in the actual 23-row serialized projection.

`paramScope`, `paramScopeType` and `defaultname` comparisons fold ASCII A–Z. Scope and scope-type scanning retain the **last** matching annotation. Default-name lookup selects the **first** match. String values index the pinned original name tables; integer scope values are preserved as words. Unknown scope/type strings use 4 / 41 respectively. The scope table is ENGINE=0, PER_MATERIAL=1, PER_MATERIAL_ENGINE=2, ENGINE_ONCE_PER_FRAME=3, INVALID_SCOPE=4. The complete scope-type/default-name tables are recorded in `parameters-evidence.json`.

The classification row is seven words / 28 bytes:

| Offset | Meaning |
|---|---|
| `+00` | Scope, default 4 |
| `+04` | Engine scope type, default 41; type 7 is LIGHTING, type 5 is REFLECTION_CUBEMAP |
| `+08` | Descriptor-derived shape code |
| `+0C` | Default texture-name index; initial 1, unmatched explicit string 0 |
| `+10` | Index in the **local** top-level enumeration |
| `+14` | Zero |
| `+18` | Copied caller-stack word, not written by the row classifier |

Do not manufacture a deterministic value for the last word based on zero-initialized C++ structs. The original seven-word copy includes a word not initialized by `82830C28`; the query proof distinguishes it from defined metadata. A native ABI policy must address that word explicitly before claiming complete byte equality for this buffer.

The copied source is `82831038`'s frame SP+88 hex, or query-entry SP-A8. In the common gateway it corresponds to gateway-entry SP-198 **at query execution time**. Reading that address at gateway-hook entry does not reproduce it: the original adapter and earlier queries have not run yet. The mask helper consumes the defined top-level index at row+10, so this reserved word cannot change its projected masks. Later consumers outside this closure are not qualified here.

For each selected type-7 row, the gateway indexes `T+28` with the classification row's `+10`, loads that parameter handle, and calls `82722068(F, handle, T+38)`. The actual selected profile is a four-element light array. Each 120-byte output block contains five 24-byte parameter rows in storage order **type, position, color, direction, property**. Do not replace it with a matrix/array stride heuristic. The catalog's recursive descriptor identities are required.

The returned child count becomes `T+3C`. Each child's five usage words are tested with `55` first; any such bit increments the selected count and ORs `T+44` with 2. Only if all five fail that test does any `AA` bit increment the count and OR with 1. Thus mixed flags take the `55` branch. `T+40` becomes the count of selected child blocks. With no selected type-7 row, constructor `T+3C/T+40` remain intact. The general original loop overwrites the child buffer/counts for each type-7 row and accumulates `T+44`; the actual corpus has at most one selected light array per row.

A type-5 row assigns its local top-level index to `T+2C`; the last match wins. The corpus has such matches in terrain, nrmmapnoskin, road, base_detail, simplelit, water and carnrmmap. All other gateway rows leave `FFFFFFFF`.

## Pass selection and native boundary

The gateway walks techniques and their passes in serialized order, taking **two passes total**, not two per technique. An empty technique consumes no slot. Each output record at `T+48+48*n` contains technique handle at `+0`, pass handle at `+4`, and the helper's output begins at `+8`. The helper `826F3F90` only ORs two 64-bit words at helper offsets `+0/+8`; helper offsets `+10..+27` hex (outer record `+18..+2F`) remain untouched. Preserve prior bits on re-entry.

The technique handle is `(techniqueIndex << 18) | 3FFFC`; a pass handle is `(globalPassIndex << 18) | 3FFFE`, with the pass index derived from the 20-byte pass array. The selected 23 rows produce 45 records: sky has one pass; the other 22 rows supply two.

The feature predicate is the virtual entry at `T.vtable+10`. Only the skinned typed callback `8273AE18` uses the verified leaf returning one; the other typed families return zero. The helper receives the inverted low-byte predicate. Derive this from the qualified original type, not the profile's spelling. `gateway-plan.json` records the six actual skinned rows and all helper flags.

The pass helper performs scope-0 then scope-2 queries with capacity 64 and arguments `r7=1,r8=0`; row `+10` maps back to the local top-level handle. It separately checks each local parameter's name in the genuine pool and excludes scope 2 from that pool-found step. The masks depend on usage/binding metadata and this lookup result, not only literal render states or the two shader identities.

These queries always filter with technique 0/pass 0; the subsequent recursive mask walks use the **current selected pass**. Actual scope-0 count is at most 17 and all used scope-2 lists are empty. Do not change query filtering to the current pass. Repeated scope/pool-found visits OR the same bits but still perform the documented mutable clears.

The recursive walk skips a descriptor whose usage is zero, including an entire container whose first leaf is unused. A used container clears its subtree's mutable bits, then recurses over the correctly decoded children. A used leaf's nonzero even/odd subset chooses its highest usage bit and the original binding table entry. The output walk rounds component ranges outward to four-component registers, ORs MSB-first u64 masks, and clears the leaf's mutable bit. The non-skinned occupancy walk marks those component ranges but does not clear leaf bits. It still clears used container subtrees.

The two occupancy scans visit components **0..251**, never flush a trailing unused run, and therefore are not a complement of 64 occupancy bits. Preserve this edge: an entirely unused array contributes zero. The detailed pass proof includes the exact scalar algorithm and checks it against bounded execution of the pinned loop instructions.

Usage bitmaps and mutable masks are different resources. Usage reads the eight per-namespace context bitmap pointers. The original setter selects private mutable bits inline at `F+80` or shared mutable bits via `[F+104]`; it never modifies those context usage arrays. In native code, clear the corresponding qualified per-effect/pool bookkeeping state. Do not modify immutable catalog data or make a dirty clear change later usage-query results. The evidence records every clear's namespace, first leaf and leaf count, including containers and one-leaf matrices.

`82C1D858` GetPool increments the nonnull pool's `+188` count. Adapter construction calls it once; each selected pass calls it again. Neither the adapter teardown in `826B5168` nor the whole pass-helper body performs a matching pool release. Native ownership must account for these observed retains explicitly; do not invoke the SDK getter on an opaque native FX ID or silently describe these calls as read-only queries. These facts alone do not establish a balanced full-manager lifetime.

For one call of every gateway row with a nonnull pool, this closure adds **68** original retains (23 adapter + 45 pass), with zero releases in the checked closure. The pass helpers themselves have no heap allocation/free; temporary classification rows and occupancy arrays live on their stack. A native implementation can use bounded host temporaries while preserving the already-owned original output buffers.

## Concrete corpus checks

The complete values are in `gateway-corpus.json`; selected useful expectations from fresh original constructor storage are:

| Profile | T24 parameters | T34 classified | T3C light children | T40 used children | T44 flags | First two mask pairs, R+08 / R+10 |
|---|---:|---:|---:|---:|---:|---|
| littextured | 21 | 10 | 4 | 4 | 1 | `F000000000000000 / 7FFC000000000000`, both records |
| quad | 8 | 0 | 0 | 0 | 0 | zero / zero, both records |
| shadows | 26 | 4 | 0 | 0 | 0 | `FFF8000000000000 / 7FE0000000000000`; then `FFF8000000000000 / 0` |
| terrain | 29 | 17 | 4 | 2 | 1 | `FFFC000000000000 / FFF7000000000000`, both records |
| water | 29 | 7 | 4 | 1 | 1 | `FFFC000000000000 / 7FFC000000000000`, both records |
| sky | 32 | 12 | 0 | 0 | 0 | `FFFE000000000000 / FC00000000000000`; second record untouched/zero |

These mask projections assume the genuine pool's known 11-name set and validated local-to-pool mapping. They are metadata results; even an all-zero pair does not imply that a shader, declaration or draw is ready.

## Quad lookup can remain original

`826B7570(T, techniqueName)` calls `826B6E60(T+10, techniqueName)`, publishes the returned wrapper at `T+18`, calls its `vtable+8` getter, publishes the effect identity at `T+1C`, clears byte `T+20`, and calls the common gateway at `826B75C4` (return LR `826B75C8`). It returns zero on its successful path. The null checks occur after the wrapper dereference; do not claim an ordinary safe missing-wrapper result for unchanged original code.

`826B6E60` walks the existing manager wrapper container in its actual iteration order. The wrapper getter `826B3908` only reads `W+10` and returns it, so an opaque native FX ID is compatible. The technique-name query is `823C7CA0`, already owned by native metadata. The first wrapper whose query returns a nonzero handle is returned. Keeping this CPU container traversal avoids replacing the manager's map or its allocation shape.

The original quad call at `826B766C` uses the pinned string `CopyColor` at `820B73F4`. Only catalog row 3 (`quad`) has that technique, handle `0013FFFC`, technique index 4. **This selects the wrapper, not the reflection pass pair.** Its common gateway records `BlurHorizontal5Sample` and `BlurVertical5Sample`, the first two serialized techniques/passes. Remaining quad-finalizer queries/setters are outside this return closure.

The named variant `826B74C8` similarly preserves the existing wrapper map and reaches the common gateway at `826B7540` (LR `826B7544`). Its lookup dereference also precedes its later null checks.

## Implementation handoff and limits

The narrow native owner is the entire common gateway with a bounded metadata decoder and native per-effect/pool mask state. Keep the original typed objects, manager/wrapper containers, their buffers and subsequent original CPU traversal. Validate the original type and wrapper/native identity association, qualify all buffer capacities and local-to-pool handle mappings, decode into temporary host values, and publish only the original defined fields in the original byte order. Return the original success ABI. No new SDK-layout object, D3D device substitute, shader instruction decoder or GPU operation is needed for this gateway.

Three details remain bounded compatibility work before exact whole-gateway equivalence: live relocated/merged correspondence for incidental zero-subset reads; the classifier's copied but unwritten seventh word; and the unmatched SDK pool retains. Do not hide these with zeroed structs, a fake SDK allocation or an assertion of complete teardown. The next useful probe is the shared context/binding relocation and the consumers of unused-lane/reserved fields, with the exact source offsets already listed in the evidence. The original manager guard remains until the larger finalizer closure is implemented and independently exercised.

Verification at handoff: common proof 16 spans / 629 words / 19 negative cases; parameter proof 28 spans / 1,837 words / 18 checks; pass proof nine spans / 1,117 words / 101 checks, including 4,227 descriptor/pass pairs over all 68 passes; integration proof four association mutations. Counts overlap across component code spans and are not summed as unique instructions. No production builds or source modifications were performed for this task.
