# Native reflection decoder — build154

The pure C++ decoder in `renderer/effect_reflection.cpp` derives the common
reflection metadata from the owned, SHA-qualified effect body. It does not yet
publish into the original typed object's buffers or replace826B5168. The manager
guard826B7218 remains in place. Build154 passes60/60 suites in89.92 seconds;
actual muted boot096 registers49 effects then reaches that guard at caller82701BBC.
No new original game pixels, shader application or gameplay are established.

`NativeEffectReflectionMetadata` passes49,537 checks over all47 common-gateway
profiles,1,153 local parameters,365 selected classification records,27 light
arrays,87 selected pass records and1,877 ordered mutable-clear events. The
fixture checks every defined classification word, used binding ranges, local
order, array-count overrides, light flags, pass handles/masks and the complete
ordered dirty-clear list. Decoded output survives destruction of its source
EffectRecord; input bytes remain unchanged. The test fixture comes from the
independent offline original-instruction model, not the C++ implementation.

`OriginalEffectBindingConsumer` passes12,801 checks across2,560 genuine AOT calls
to8270A370 and8270A438. Each writer sees all256 usage masks and five adversarial
inactive-field values, including7FFFFFFF/FFFFFFFF. Active vector and memcpy
paths produce the exact entire CPU cache contents. Null source is admitted only
when no consumed lane is active. ABI, source and parameter bytes remain intact.
These are CPU cache writes; they do not upload native GPU constants.

## Extension beyond the frozen first25 proof

The prior `docs/native-effect-reflection-contract.md` and its four component
proofs remain unchanged. New reproduction and evidence live in
`build/effect-reflection-all49/`. Its verifier reuses the frozen query helpers,
copies the parameter orchestration with two explicit profile extensions and
compares the first25 output against the frozen reports. It checks6,562
descriptor/pass pairs using the pinned original usage/binding instruction
evaluator. That evaluator is an offline verification tool, never shipped code.

The49 effects contain2,808 annotations. The second table adds19 scalar class2
entries named exactly`expand`, each descriptor word00000008 with one stored word
00000001. They are not any of the three classifier query names. The decoder
retains/validates those bytes and does not cast the value to a guessed scalar
type. Queried scope/default annotations still obey the existing integer-word
and string contracts. Thirteen extension checks reject changed shapes, values,
queried names, invalid storage and typed-predicate associations, and pin the
new single-light and inactive-count cases.

Twelve second-table profiles have one light child; the remaining selected light
arrays have four. Original82722068 stops on the first missing child or after
four successful children. Its member order remains type,position,color,
direction,property. Combined single-child ordinals are26,33,34,35,36,39,41,43,44,
45,46,48. All actual scope2 filtered lists remain empty.

The second-table feature predicate is still the actual typed vtable+10 entry:
823CA888 returns1 for callback823CA960;823C7F28 returns0 for the other seven
families. The offline plan pins their original words and maps the catalog's
callback/vtable/finalizer associations. A future runtime caller must validate
the real typed object and feed this predicate; it must not infer skinning from
the effect name. The pure decoder takes the predicate as an explicit argument.

## Inactive fields and native publication policy

`EffectBinding` represents a range only when its usage subset is nonzero.
Kind2 array element counts are represented separately because the original
parameter writer overwrites both returned counts, even for inactive lanes.
Pass masks use the encoded binding count, before that override. Classification
contains only the six words written by82830C28; the unwritten seventh stack
word is absent. These host values are metadata, not a byte-for-byte claim about
undefined original allocation contents.

The expanded original context-origin model reports the following incidental
zero-subset reads across47 gateway callers:

| Pool scenario | Defined | Unwritten alignment bytes | Outside requested FX payload |
|---|---:|---:|---:|
| First registration sequence |4,132|76|8|
| Already populated |4,131|77|8|

One field differs between the serialized and merged projections in both
scenarios: simpsons_edgeAA, combined row29, local top index26, handle006C0032,
inactive55 count. Its serialized count2 becomes1 after the merged context's
shared-binding memset. It is not an active binding difference. Do not copy the
serialized incidental count and claim exact live SDK memory correspondence.

The consumer evidence pins eight whole/reviewed functions and523 instruction
words. The inspected826B5770 dispatcher consumes classification+04/+10 and copies
six binding words; it does not consume the unknown classification+18 word.
826B3E50 clears classification+14 and frees the buffers. Other inspected material,
lighting and cubemap consumers are recorded with their boundaries. Only the two
constant writers above have new runtime consumer tests. This does not certify
all33 active engine dispatch types or their transitive rendering callees.

A future gateway adapter must make its guest publication policy explicit,
preserve existing bytes where the native representation has no defined field,
and validate the supported subsequent consumers. It must also own private and
shared dirty masks separately from immutable usage bits, preserve pass-mask OR
semantics and untouched pass tails on re-entry, and retain the original buffer
allocations/lifetimes. No guest buffer publication is implemented in this change.

## Ownership and next boundary

With a nonnull pool, the bounded original closure contains134 SDK retains:
47 adapter calls plus87 pass calls, with no matching release in that closure.
This count is evidence of original behavior, not a native lifetime policy.
Native pool leases, escaped aliases and finalizer/manager teardown still need
an explicit reconciliation. The native decoder performs neither retain nor
release and cannot hide that outstanding work.

Next implement the common gateway's publication and native mask ownership under
the guarded manager, exercise it with the actual typed objects and both cleanup
orders, then qualify the derived quad/shadow finalizer setters before releasing
the manager guard. The normal shadows raster/state-extension cleanup gap remains
unchanged; SDK depth resolve82455570 is still guarded.

Reproduce with:

```powershell
python -B build/effect-reflection-all49/verify.py
python -B build/effect-reflection-all49/checks.py
python -B build/effect-reflection-consumers/verify.py
python -B tools/generate_effect_reflection_fixture.py --check
.\tools\build.ps1 -Jobs 8
python -B tools/run_native.py --timeout 20 --log build/boot-096.log
```

Frozen results: `build/effect-reflection-all49/build154-tests.log`,
`build154-summary.json`, `aot-manifest154.json`, `native-test154.log`,
`boot096-output.log`, and `build/boot-096.log`. Existing58 suites remain green;
the two new suites are the decoder and original constant-consumer tests.
