# Native Im2D integration

Build186 extends the depth policy described below. The original camera's
forward/reversed full range is retained in the packet; the pixel shader maps
depth once, then converts to decoded20e4 before comparison/write. The original
UI replay and WARP/hardware tests pass; see `native-im2d-depth-integration.md`.
The build181 execution below is historical evidence.

Build181 passes85/85 suites in154.75 seconds. Actual muted boot142
submits3 original Im2D draws and completes35 native presentations.
The latest raw capture is black; the preceding frame contains the original
Itchy/Scratchy artwork. Both are saved with display-acceptance metadata.
The launch still stops at `unimplemented import __imp__XamGetSystemVersion`.
This is not a completed menu, gameplay, desktop scanout or console-parity claim.

Original82409308 still executes its prologue, primitive bookkeeping, dynamic
allocator823FC848, original memcpy, screen override leaves8240F210/8240F230,
final start-vertex increment and return epilogue. Its native unlock now retains
an immutable CPU byte snapshot and the actual native buffer/declaration owners
alongside the existing real raw GPU upload. Physical staging is freed after
upload; the later decode reads no caller-stack or freed-staging bytes.

Original82408CC0 executes in AOT. Its declaration setter82408D08 and stream
setter823EEC6C retain actual native deferred bindings, replacing SDK object
stores. Their exact register/stack/range/cache contracts are checked. The
original scalar19E/19F requests, null/textured stage schedule and state commit
remain. A nonzero cached explicit shader currently rejects before setup; its
material transition requires separate qualification. Repeated unchanged CPU
bindings must match their existing native owners.

Only the engine draw call at82409538 is replaced. It checks the mode-zero
pipeline, original start vertex/count, current camera/raster, shader selection,
committed effective state and owner identity. The native program qualifier runs
the original pixel-source builder8240EAA8, constructs the exact22-word vertex
key82410A74..82410B94 and runs original macro builder82410588. It admits only
the complete recovered flat/modulate pixel expressions and twenty vertex
options. Unsupported programs fail explicitly. Native shaders are compiled
ahead of time; no console compiler, shader COM objects, guest shader caches,
interpreter or runtime GPU-program translator is created.

Both retained expansion modes0/1 use the provisional native float equations
and explicit per-write RGB10A2 packing. Expanded input colors must be normalized.
This retains the requested mode and packed storage; it does not establish the
console's distinct expanded/normalized blend precision.

The native backend owns cached compiled shaders and input layout. It decodes
the verified28-byte declaration to owned40-byte vertices, binds actual GPU
inputs, performs ordered triangle-strip draws, and clears temporary shader and
buffer bindings. Each triangle blends against the preceding packed result,
including overlapping triangles within one strip. Original nonseparate scalar
blend07060706 retains source-alpha-squared plus destination-alpha times
one-minus-source-alpha. Integer RGB10A2 storage uses explicit float arithmetic
and round-to-even packing. The original engine function resumes only after
real submission; the backend counts a submitted packet even if the rasterizer
legitimately culls or alpha-discards every fragment.

The engine integration requires disabled stencil, full root target/viewport,
zero bias/scissor/clip planes, guardband one,
half-integer pixel centers, RGBA writes, and cull0/2/6. It activates exact X/Y
viewport dimensions with native0..1 depth while preserving the original logical
depth record. Build186 supports all eight comparisons and write enable against
the qualified D24FS8 working attachment. The PS applies the original reversal
once and stores exactly representable decoded20e4 values in native D32 backing.
The common state owner limits other retained fields to their reviewed values;
nonindexed strips do not use primitive-reset indices or tessellation patches,
and both accepted sample masks enable the one native sample. Only recovered
blend words and flat or single-level qualified texture policies can submit.

Original viewport X/Y scales are +width/2 and -height/2 at8243D074/8243D088.
Original826D54B8 emits TL,BL,TR,BR for positive unrotated extents. The native
tests check both strip parities, cull2/6, alpha comparisons, masks, overlap,
ownership, foreign resources, transient cleanup and unchanged depth. The
original driver test additionally executes the full CPU setup/commit/epilogue,
checks packed rectangle pixels and reuses cached native input bindings.

Console1/16-pixel snapping, clipping/interpolation/filtering and blend precision
are not established by native GPU tests. The implementation retains the
original shader's0.5 subtraction; it does not claim console raster equivalence.
Supporting primary references are linked in renderer/im2d_pipeline.cpp.

Presentation capture metadata now records screen_draws and im2d_draws
separately and counts both when selecting a newly rendered frame. Captures
remain unadjusted renderer readbacks, not desktop scanout evidence. The first32
changed frames are captured even when Present reports occlusion; metadata keeps
completed front-copy success separate from display acceptance.
