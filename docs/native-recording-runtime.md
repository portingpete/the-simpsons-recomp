# Native rigid recording integration

Checkpoint: live025 reaches original rigid geometry binding after successful
material activation, recording begin, CPU record linkage, full application-state
seeding, FX context association and shadow texture transfer. It has zero recorded
scene draws. The current finish/replay source and mesh/material/staging integration
still need regeneration, build and live verification. Gameplay is unverified.

The original `82740420` recording decision and CPU body remain executable.
Native identities refer to independently owned C++ capabilities and are deliberately
unmapped in guest memory. They never provide an SDK device or command-buffer layout.

## Begin and state

Original `826F4D08` retains its limit checks, cache search, aliases, CPU allocation
and publication. The native payload is allocated at its actual `826F4E94` boundary,
then starts a real deferred recording at `826F4ED4`. It owns actual target, depth,
shader, viewport and scissor snapshots. Its identity differs from the deferred
context identity. Consecutive original seed calls validate the same native snapshot.

The original application callbacks force all82 supported scalar fields and all320
sampler fields into a separate native CPU state destination. Completion checks the
independently pinned field set, so a duplicate in the mutable registration list
cannot conceal an omitted field. Preflight does not mark a field submitted. Main
CPU effective state is restored after the original alias-restoration calls.

At `826F4F48`, the original CPU pool record must have the correct bucket backlink,
metadata and payload identity, with no completed accounting, retry result or patches.
Only then may `activePayload()` expose the recording to texture and mesh consumers.

## Textures

Original `826F39E0` retains the named `shadows` lookup, stack snapshots, three-row
loop, usage branch and 64-bit sampler-mask arithmetic. Only opaque FX metadata
traversals and the SDK binding are replaced. The selected opaque pass is exactly
`0003FFFC/0003FFFE`, metadata context `2620`, VS8200D3AC/PS8200D9E8.

The first two rows have usage80 and map to PS stages0/1. Their handles are001C000D
and0020000F, using the copied depth resources at shadow-owner F0/F4. The third
handle00240011 has usage0 and is skipped by the original branch. Before the first
binding, all resource fields, uploaded phases, formats, dimensions and identities
are validated; the sampled depth owners are distinct and do not alias the output.
Actual PSSetShaderResources calls target the deferred context.

The descriptor is D24FS8 with an identity resource swizzle. Its pinned local format
reference expands depth toRRRR; the native rigid shader explicitly adapts scalar
depth sampling before applying the original RGB selector. This reference-based
policy is not a claim of a physical-console sampling measurement.

The independent depth-binding probe consumes stages0/1 without rebinding them at
draw, distinguishing their copied .25/.75 values. It passes on WARP and hardware,
including deferred-only binding, retained resource lifetimes and unchanged
immediate state. The material accumulation, rigid mesh and driver tests also pass:
six focused tests in `build/reach-game-rigid-record-tests.log`,3.37 seconds.

## Finish and playback under development

LIVE027 completes the first original rigid submesh:3530 indices, one recorded
draw and1320 owned data bytes. Real FinishCommandList succeeds; original CPU
status/accounting/LRU publication and both context restorations complete. The
original replay callbacks compute both56-register banks, and both CPU copies of
each bank match before actual native execution. The first payload executes once.
Its1280x720 private scene target remains all zero before and after execution.
This does not prove a visible color draw or presented gameplay; geometry coverage
is under investigation. The next begin stops at the single-record ownership guard.

The shared-map union mode now admits only a provably empty shared commit. Before
any dirty-mask mutation, the original filter must leave all first64 shared bits
zero; SDK entry rechecks them. Each of the eight original category loops consumes
that same qword when F+124 is1, so no shared constant/texture work can occur,
regardless of union maps. The original mode word stays unchanged, no union pointer
is fabricated or accessed, private material accumulation remains independent,
and both128-byte dirty banks clear at commit. Nonempty union work is unsupported.
All160 tests pass in111.96s: `build/reach-game-rigid-union-full-build-tests.log`.

The first-record success profile keeps original `826F4F58`, its CPU record lookup,
status0-to2 transition, accounting, LRU stores and both alias-restoration helpers.
Native query output1 is actual owned immutable draw-data bytes; output2 is zero
because this native payload has no separately serialized second stream. These are
neither original SDK packet counts nor estimates of opaque driver storage. D3D11
command-list and resource lifetimes are owned separately.

Only successful FinishCommandList permits the original success branch. Snapshot
leases release at826F50A8/50B0. Payload seed and draw owners retain their own GPU
resource leases. At826F50C4, both aliases must already point to the previous main
context before its CPU state is restored. The subsequent original FX association
must complete before the session is released.

Original `827402F0` retains its shared/object callbacks. Per-object constants must
come from the original VS82D6C0D0 and PS82D6C450 staging arrays. The payload input
mask inherits VS c0-31 and PS c0-43; recorded material registers c46/c47/c49 remain
independent for each draw. Preparation updates distinct native draw constant
buffers immediately before ExecuteCommandList, without embedding stale uploads
in the immutable list. Actual immediate bindings are restored by native execution.

The last verified executable's guards admit one completed record, an empty initial
LRU and no retry/eviction/deletion path. A real native finish failure stops execution;
it does not manufacture a fallback or completed record. Multi-record reuse and
original cleanup need separate integration after the first real replay is verified.
Terminal host release is reported as incomplete original guest cleanup.

Work is now frozen at the user's requested
LIVE027 checkpoint (local development record). Multiple-record
source changes, six observation hooks, per-payload effect retention and captures
are saved but unbuilt. The CPU graph fixture header remains unwired.
