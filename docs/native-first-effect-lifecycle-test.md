# Original first-effect lifetime fixture

Status: build144 passed 1,827 checks; build145 adds two real native shader owners
and stale shader-owner checks, passing 1,831 checks. CMake now runs this fixture
as OriginalFirstEffectLifecycle. No aggregate heap allocation/free totals are
claimed. The original shader/caller proof limits remain separate.

`tests/test_first_effect_lifecycle.cpp` accepts one argument: the original flat
image path. It loads/initializes the real Runtime and runs the actual original
entry until the established diagnostic observation at `828166FC`, following
real muted Dac startup. This is the same observation used by the existing
graphics CPU startup fixture. No allocator, original callback, or SDK operation
is replaced by the test. It never assigns a synthetic LR to enter a hook.

Each of two isolated manager lifetimes performs:

1. Original `8269BF70(260, real CPU descriptor)` allocation, followed by
   `826B6F60(manager, nativeContext)`. The original singleton and existing CPU
   pool root must agree. Both managers borrow the same live original pool.
2. Actual **`827019E8(r3=82CEFD20,r4=1)`** registration. This fixture-only count
   does not edit the original row table, production loop, or 25-row count.
   The original body allocates the wrapper/name, calls native engine creation
   and reflection through main's configured boundaries, inserts the wrapper,
   calls original row0 callback `8273B280`, and inserts its typed CPU object.
3. Validate one native `EngineEffects::View`, unmapped/unique identity, complete
   independently owned CAC-byte original source, exact 20 default words, all
   eight scalar/five sampler metadata records, and C8 CPU cache's defined fields.
   Expected cache selectors come from live `82E06F80`/`82E07118` tables. The
   second cycle changes one valid scalar/sampler mapping through original
   `82831360/82831380`, then restores it after cleanup, proving reflection does
   not hardcode startup mappings. No state is applied or rendered.
4. Find T through **`826B7088(manager, originalName)`**, not a fabricated tree
   node. Validate its vtable, original manager/row fields, three real CPU blocks,
   and only fields initialized by the original constructor. Query actual native
   technique/parameter handles and missing/cross-namespace names.
5. Invoke original **`8273B3B8(T)`**. Main's two query hooks handle its actual
   calls at `8273B420/438`; original code performs lookup and the four stores.
   Verify T+A8/B8 boundary: outputs are wrapper, native identity, `3FFFC`,
   `180008`, and T+0..A7 is unchanged. SP/LR/nonvolatile GPRs are preserved.
6. Reject premature pool retirement through the actual entries `82CC1820`,
   `82722568(&root)`, `82C1CF00(pool)` and release alias `82C1D0A0(pool)`.
   Compare the entry stack, entire live pool, manager, wrapper, cache and row
   table before/after rejection. Native pool/FX lifetime checks must reject too.
   The root stays nonnull and P+188 remains 1 under main's host-lease policy.
7. Actual **`82701118(r3=82CEFD20,r4=1)`** paired typed/wrapper cleanup.
   Verify empty native ownership, cleared table publication, absent typed lookup,
   stale-ID/source/query rejection and surviving unchanged pool. Never read the
   freed wrapper/typed/cache/name buffers or use the expired source span.
8. Original **`826B7600(manager,1)`** deleting destructor clears the singleton
   and frees its CPU containers. Verify the original root remains live, the
   console device field CAF8 remains zero and native context identity is valid.

The following limits are intentional:

- There is no allocation-entry observer for saved-value slots. The test checks
  only the cache's defined words; it does not poison returned storage or claim
  that an unknown prior byte pattern proves an unwritten slot.
- The source is compared byte-for-byte and its host-owned span has independent
  backing. No original image file or mapped source bytes are mutated. A source
  mutation/rejection test is deferred rather than changing source protections.
- Real original cleanup executes, but no aggregate heap allocation/free counter
  is installed. Successful two-cycle cleanup is not a complete heap-leak proof.
- The first FX owns two real offline-compiled native shaders. No begin/commit/end,
  inherited-state closure, original draw, shared-parameter merge, CRT root free,
  full graphics initialization, or complete application shutdown is claimed.
  Runtime subsequently performs its existing terminal worker/backend retirement.

## Build and integration

Syntax only (safe while main builds; records exact header/source hashes):

```powershell
python -B build/first-effect-lifecycle/run_probe.py
```

Passed with real ClangCL, `/std:c++20 /W4 /WX /fp:strict`, using current main
headers and generated context. No production source is compiled by this command.
Outputs are `syntax.log`, `syntax-command.json`, and `syntax-result.json`.

After main signals the new FX build is complete/frozen:

```powershell
python -B build/first-effect-lifecycle/run_probe.py --run
```

The runner compiles just this fixture, links existing parent libraries, runs
muted with a 60-second timeout and records input/output hashes and the real
exit code. Parent artifact changes during compile/run invalidate the result.
It performs no CMake generation or parent build. All runner outputs stay in its
owned directory. Use `run.log` and `result.json` for the actual check count.

Main's CMake integration adds `FirstEffectLifecycleTests` from this single
source, links `SimpsonsRuntime` (existing transitive platform/audio/graphics
libraries), applies `/fp:strict /W4 /WX`, and registers a test passing
`analysis/simpsons.pe`, timeout60. The isolated runner uses a 64-MiB stack reserve
like other original-entry fixtures. No test-specific production callback is
required beyond the existing audio observation and main's normal FX hooks.
