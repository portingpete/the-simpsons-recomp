# Pipeline capability argument audit

Original `82416C58` supplies two valid capability-dependent inputs for the
same pipeline index owner. Bit16 of `82E3DFBC` selects r6=0 when set and r6=2
when clear. Both inputs request `1FFFE` bytes of R16 storage and use the same
allocation fields and paired cleanup. The earlier native r6=0 / bit16-set
guard rejected an original branch whose fourth argument has no retained
allocation or lifetime effect.

The independent [source verifier](../tools/analyze_pipeline_capability_contract.py)
checks the complete immutable flat image: base `82000000`, 15,466,496 bytes,
SHA256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
It pins complete initializer, SDK index constructor, allocator wrapper,
fallback, actual allocator methods, lazy construction, original cleanup,
SDK release/destructor/free wrapper and allocator/release tables. Its report
records each full-span hash and annotated original instruction word. This is
source evidence; native execution receipts remain separate.

The independent verifier passes 3,533 span-byte mutation rejections and 70
capability-word cases, covering each single cap bit, its inverse and mixed
patterns. It records 350 mismatched or arbitrary fourth-argument cases for
the separate native admission regression.

`82416C80` reads caps+1C; `82416C88` inverts that word and `82416C8C`
rotates17 and retains bit1. Thus the exact domain is
`r6 = (capsWord & 10000) ? 0 : 2`. Size r3=`1FFFE`, r4=8 and r5=1 remain
unchanged. The initializer publishes the returned index at `82D507F0`, builds
the same three declarations, and calls cleanup `82416BC8` on creation failure.
The capability branch does not change its publication or cleanup sequence.

Complete SDK constructor `82441A08..82441AB4` saves only r3/r4/r5. It does
not read or retain incoming r6. It allocates a `20`-byte header with flags
`64800000`, then `1FFFE` payload bytes with flags `B2800000` for this exact
producer profile. Success stores resource flags `20100002`, reference count1,
payload pointer and byte size. Both cap branches therefore have identical
allocation policy and resource fields.

The downstream allocator proof includes the original active family, rather
than relying only on the constructor's unsaved argument. Wrapper `8238E880`
captures size and flags. Its physical slot20 dispatch replaces r6 with its
own derived alignment at `8238EAA4`. Null-allocator SDK fallback `824315E0`
loads r6 from its allocation-flags table at `82431628`. The real allocator
constructor `8268E510` installs table `820B60B8`; its normal slot0 is
`8268DDA0`. That body consumes r3/r4/r5 and obtains alignment and offset from
the structured hint. It overwrites r6 with its own hint-derived offset at
`8268DEDC` or `8268DF44` before heap calls. Incoming capability mode cannot
change either allocator branch. Arbitrary substituted allocator families
remain outside this proof.

Original cleanup `82416BC8` releases the published index and declarations,
then clears their fields. SDK release `82441708` decrements the retained
reference count and invokes `82441050` at zero. Kind2 selects its index case
`82441168`; that case frees the saved payload and the header. Neither cleanup
reads a retained capword or the original fourth argument. A fresh unbound
index has header+8=0, so the optional used-resource service `824574B8` does
not occur in this profile. General SDK locking, binding and deferred release
are separate contracts.

Native admission should preserve the exact capword/r6 correlation, fixed
size/type and all existing caller, context, thread, empty-publication and
owner checks. Accepting a dead argument under this producer does not qualify
arbitrary fourth arguments or unrelated creation calls. Native backing also
replaces the SDK guest allocations and TLS accounting; this adaptation is
not evidence that those guest callbacks were executed.

Run the source verifier with:

```powershell
python -B tools/analyze_pipeline_capability_contract.py --output build/restrictive-check-audit/pipeline-capability-contract-20261002.json
```

The native regression now passes eight independent cap patterns on both WARP
and hardware in both native and Release. Each process invokes the whole
original initializer, verifies the actual R16 buffer type/size, uploads and
reads back its bytes, runs the original three-declaration and index cleanup,
rejects stale ownership and repeats zero-field cleanup. A separately retained
buffer lease remains usable after logical release; its final weak owner expires
after that lease is dropped. This proves the native backing owner lifetime,
not an indexed draw, device quiescence or an observed COM reference count.

For each pattern, four mismatched/arbitrary r6 values reject before publication
and preserve the complete PPC context, host FP state and Win32 last error. The
aggregate case also passes size/flags/type, repeated creation, caller-context,
thread and owner failures. Prevalidation receipts retain the original producer,
callsite, raw capword and incoming arguments before those checks.

Current full receipts are under `build/restrictive-audit/`:
`prevalidation-capability-thread-full-native-20261002-tests.xml` and
`prevalidation-capability-thread-full-native-release-20261002-tests.xml`,
with full `-LastTest.log` copies. Native passed493/493 in423.93seconds.
Release passed492/493 in446.93seconds; all capability cases passed, while
the unrelated unchanged-fullscreen-window test failed. Its isolated retry
passed, but the cause remains unproved. The failed suite and retry are separate
receipts, not a passing complete Release suite.

`pipeline-prevalidation-full-native-v2-20261002.json` and
`pipeline-prevalidation-full-release-v2-20261002.json` independently verify
all16 lifetime cases and64 malformed failure snapshots in each build. Every
failure retains exactly its preceding input/owner/mission/action/group, followed
by a source-valid original creation and owned release. Reports pin the compiled
source/executable scope and telemetry bytes. Raw LR remains explicit: malformed
calls use the fixture LR; the separately pinned producer and callsite do not
fabricate a gameplay caller.

The initial focused selections each failed the aggregate map-observer fixture
because it configured the same audit sink twice. That test-only correction
passed35/35 in each build under the `focused-*-20261002-v2` receipts. Preserve
the earlier failed selections rather than replacing their result.
