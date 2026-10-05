# Native binding reset and null raster binding

Later extension: `native-viewport-reset.md` qualifies the private-to-default
target transition and its conditional viewport/effective-clipping reset. The
default-only implementation described below is the historical initial scope.

Boot057 reaches the original presentation callback `823EE820`, caller
`8240806C`, after two successful mode-zero resets and the original camera end.
Presentation remains guarded; this is not evidence of a rendered game frame.
The native reset uses the byte evidence in `native-screen-bridge-boundary.md`.

## Mode-zero reset

The engine entry `823EFDA0` owns one checked operation. It validates native
resource identities and the selected default target pair before changing CPU
binding caches. It preserves stream-record word +0C, independent application
caches, texture ownership, logical reversed viewport and attachment contents.

The null path of original `82401AF0` runs for all eight engine texture stages.
Its alpha flag and pending scalar changes stay in AOT. Original `82400D50`
then reconstructs the retained RW state: original conversion tables, scalar
and stage helpers, sampler-cache stores, CPU stage callbacks and final commit
remain executed. Six byte-pinned hooks replace its direct SDK accesses.
Stages 8–15 and effective fields outside that rebuild remain inherited.
The mask-zero equality behavior of the original pending/applied reset is
retained; rebuilding is not equivalent to applying startup defaults.

The backend finally clears precisely PS textures 0–7, vertex streams 0–3,
index, vertex/pixel shader and input layout, then selects the owned default
color/depth attachments. It preserves other shader-resource slots, samplers,
constants, shader stages, topology, viewport/scissor and fixed-function state.
Actual D3D11 context queries verify these effects. COM references retain
outstanding GPU use; guest logical resource counts are not decremented.

This implementation accepts the observed cache-hit default-target profile.
A changed target that would run the original conditional viewport/scissor
reset is rejected. Preserved resource aliases of the new attachments and
unowned extra output slots are rejected before native setters.

Before GPU publication, failure restores CPU cache windows and the rebuild's
effective state, callback registers and stack bytes. Once GPU publication can
have begun, failure stops the driver. No restoration of guest words is claimed
to undo GPU work.

## RenderWare selector 1 with a null raster

The original selector table byte `82062DE8=53` branches to `82402744`.
It calls `82401940(value,0)` at `8240274C`; both original frames remain AOT.
The nonnull route is unsupported and rejected before its first mutation.
The original helper's null path is also available directly for stages 0–7.

For stage zero with texture alpha flag `82D0E3DC` set, it clears that flag.
If vertex alpha `82D0E3D8` is zero, it queues BlendEnable (ID `3C`) = 0 and
calls original scalar helper `82400170(60,0)`. Dirty membership, capacity and
all selected writes are preflighted before either update. Already-dirty
entries retain their queue positions. Effective scalars wait for real commit.

When the selected raster cache `82D0E3F8+24*stage` is already zero, the helper
does not issue an SDK bind. Otherwise it stores zero and calls `824408E0` at
`82401AB8`, returning to `82401ABC`. The null-only adapter checks that exact
caller, the zero device/texture arguments, stage and original 64-bit mask,
then clears one actual D3D11 PS resource slot. Other bindings and contents
survive. The existing reset-only callbacks remain limited to their own scope.

## Verification

Build098 passes all 24 CTest suites; the real original driver lifecycle suite
passes 28,637 checks. Boot057 reaches the presentation guard described above.

`test_rw_rebuild_contract.h` uses the actual AOT rebuild with twelve retained
state variants. It checks all stage-cache words, unchanged application state,
stages 8–15, original ABI frames and invalid-source rejection. A test dispatch
wrapper injects failure only after two real CPU stage callbacks and all 56
sampler updates, then checks rollback and a successful retry.

`test_binding_reset_contract.h` exercises live owned resources and validates
binding-cache words, preserved texture references, viewport and GPU contents.
`test_null_raster_contract.h` covers all eight stages, cached/uncached bindings,
both alpha flags, queue deduplication and rejected inputs. Backend tests query
real WARP/hardware state, including unrelated resources and UAV counters.
These checks support the implementation; they do not establish game rendering.
