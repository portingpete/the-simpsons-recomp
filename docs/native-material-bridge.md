# Native engine material creation and release

`runtime/engine_materials.cpp` implements original pixel creation/release
`823EFB78`/`823EFBD0` and vertex creation/release `823EFAA8`/`823EFB10`.
Creation takes the original record address in r3 and a writable BE32 output in
r4, then returns true only after a native CPU material resource owns an exact
immutable, hash-verified original record. Vertex creation also preserves the
original `82CD1A6C = FFFFFFFF` binding-cache invalidation. Release validates
identity and stage, releases real ownership, and returns the remaining reference
count.

The engine's 32-bit opaque tokens map to complete native generation/slot IDs.
They occupy an unmapped range disjoint from declarations, scratch indices and
dynamic buffers, are never reused, and cannot act as SDK pointers. Source,
output, stage and image identity validation precede output publication. Failed
creation releases partial ownership and leaves the output unchanged.
Original SDK creation also consults optional callbacks at `82D51544` (pixel)
and `82D51548` (vertex), which can veto creation. A nonzero callback fails
explicitly before allocation, output or VS cache mutation until that callback
contract has a native implementation. It is never silently skipped.

Creation owns a deferred shader resource; it does not report GPU compilation
or permit a draw. Native preparation creates actual D3D11 artifacts for the four
proven screen records. The sixteen remaining startup records explicitly fail
preparation until their shader semantics have native implementations. No runtime
shader interpreter, translator, or fabricated compiled object exists. The
original SDK binder/release must never consume these tokens; native engine draw
services must resolve ownership first. The still-zero console device and
unmapped identities make any unported SDK access an explicit memory failure.

`tests/test_engine_resource_bridge.cpp` uses actual generated engine entry hooks,
the original image, and real WARP resources to exercise shader creation, native
preparation/rejection, release, stale/wrong-stage IDs, immutable record checking,
read-only output failures, original scratch initialization/cleanup and declaration
deduplication. These resource tests are separate from original plugin construction
and game rendering, which have not yet been reached.
