# Original engine state bridge

`runtime/engine_state_bridge.cpp` replaces engine initializer `824008E0`, commit
`82400040`, and sampler update `82400278`. The hook evidence bytes and whole
original image identity are checked before AOT generation. These services own
CPU state; the renderer still requires separate proven target, viewport,
material, texture and draw contracts. No SDK object or GPU command decoder is
created.

Initialization retains original AOT scalar reset `823FFE78`, scalar queue helper
`82400170`, and stage queue helper `824001E0`. It ports the direct CPU stores,
eight logical sampler records, and the otherwise-inline base-map-only sampler
write. The original capability branch selects linear or point min/mag filtering.
SDK defaults remain in the host effective owner while untouched guest cache
entries retain their original invalid sentinel. These are separate layers.

Commit validates bounded, unique dirty entries, applies the supported scalar
changes to a temporary host state, and retains engine-only IDs `194..1A8` in the
guest applied cache. Stage updates execute original CPU helper `8240EBB0`, whose
real return is checked. Original read-only pipeline query `823F4670` is retained
for changed scalar entries. The original wrapper had discarded rejected stage
results; native execution makes that unsupported path an explicit failure.

The pending/applied caches, queues, logical state words, and 320-byte pipeline
record array are snapshotted before initialization/commit. Any rejected value
or callback failure restores their bytes and leaves effective host state
unchanged. An individual sampler update validates native state before publishing
its cache word. Transactions do not claim general driver restart, cross-thread
render access, or recovery from a failed allocator outside these CPU services.

`runtime/engine_cpu_calls.h` supplies a checked original ABI frame, original
stack backchain and thread-local register context, while isolating call-clobbered
registers from the intercepted function's caller. Actual guest memory and
original CPU allocator effects are retained.

Build44 passed 14 suites. `OriginalEngineStateBridge` ran 1,457 checks using the
pinned image and generated original code, including startup caches, real stage
records, original queue deduplication, unsupported scalar/stage rollback,
sampler bounds, high engine IDs, capability-selected point filtering and ABI
context restoration. `NativeEngineState` separately checks effective-state
semantics. Boot027 executed this bridge during actual startup and unwound the
completed resource/pool stages; it stopped at dynamic buffer ownership
`823FCF60`. Console device and started flag remained zero, lifecycle remained 2,
and no original pixels or game frames were produced.
