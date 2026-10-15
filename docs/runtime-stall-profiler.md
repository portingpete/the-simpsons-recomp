# Runtime stall profiler

Double-click **Play The Simpsons Game - Stall Profiler.cmd** in the project
folder for a separate profiling launch. Windows may briefly show the command window. It enables
`SIMPSONS_STALL_PROFILE=1` only in the game process and writes timestamped runtime
logs to `build/stall-profiler-logs`. It uses the normal startup, save and video
settings. The helper can be invoked directly
as `build/native/SimpsonsLauncher.exe --stall-profile`.

For manual launches, set `SIMPSONS_STALL_PROFILE=1` before starting the game or
ordinary launcher. The ordinary launcher passes its environment to the game;
output goes to the existing buffered runtime log. Profiling is disabled by
default. Set the variable to `0`, or remove it, to disable profiling on the next
ordinary launch. The dedicated profiling shortcut always enables it. The game
reads the variable once at process startup.

```powershell
$env:SIMPSONS_STALL_PROFILE = '1'
& .\build\native\SimpsonsNative.exe --image .\analysis\simpsons.pe
```

The CMake option `-DSIMPSONS_STALL_PROFILER=OFF` removes the timers and profiling
wrappers at compile time. It defaults to `ON`, allowing an ordinary build to be
profiled without rebuilding. A disabled process takes the short inactive path;
it does not read the performance counter or build profiling records for calls.

Six explicitly selected original guest functions also have entry scopes:
`Guest.loader82718B70`, `Guest.byteDecode8281E9B0`,
`Guest.vmOuter828389F0`, `Guest.vmSetup82844AB0`,
`Guest.pairUpdate82B2ABD8` and `Guest.pairShift82B2A8D0`.
The generator verifies each entry, chunk and original trace anchor before adding
its scope; the AOT manifest records coverage and the unchanged body digest.
These scopes distinguish loading, byte decompression, the startup VM call/setup
subtrees and the sampled sorted-pair update path in actual frame records. The VM
roles are inferred from original instructions. Setup has other callers, so its
entire frame total cannot be added to the outer call as a single nested cost.
Growth, hooks, indirect calls and native nonlocal-jump handling remain original.
The pair
scopes preserve original growth calls and shift order. Native children keep their own section
and exclusive attribution. Each guest contributor includes remaining work in
unselected guest callees within that scope; it is not pure function self-time.
Compile-time profiling Off removes the scopes.

Frame time is wall time from entry into original execution to its first completed
native presentation, then between consecutive completed native presentations.
It includes guest work, rendering, audio on the frame thread, file I/O,
synchronization, intentional pacing, and the actual DXGI Present call. Frames
over **16.67 ms** receive `[STALL]` summaries with section totals and largest
contributors. `SIMPSONS_STALL_FRAME_MS=<1-1000>` changes that budget; 120 FPS
studies use 11-14 ms together with `SIMPSONS_PACE_LOCK=0`, because profiling
overhead otherwise makes the refresh-locked limiter step to two refreshes
(see `docs/frame-pacing.md`). A configured 30 FPS cap therefore produces slow-frame diagnostics
showing its intentional waits. The profiler does not change the cap.

Runtime imports and native HLE hooks taking over **2 ms** receive `[STALL]` call
records. Actual waits over **2 ms** receive `[WAIT]` records. Records identify
duration, Windows thread ID, function, guest function-entry address, guest caller
(LR), and waited object or fence/query identity when available. GPU query
identities are host pointers, distinct from guest presentation receipts.
`pc_kind=function_entry` means the runtime's `lastFunction`, which is not an
instruction-precise PC. A zero guest address means no guest context was available.

Slow native mutant waits also report `owner_tid_at_entry`,
`owner_caller_at_entry`, `wait_caller`, and the actual NT `status`. Owner fields
are observational snapshots taken before entering the wait; the lock holder can
change afterward. Zero means the owner or saved guest call site was unavailable.
These diagnostics do not participate in mutex ownership or scheduling decisions.

Rendering detail includes constant-bank `Map`, buffer/sampler allocation misses,
recorded draw preparation, `ExecuteCommandList`, skin draw submission, and
presentation query `CreateQuery`, `GetData`, and `Flush`. This separates driver
calls from the larger runtime hooks without adding GPU synchronization commands.

Original screen sprites reuse one device-owned packed integer scratch surface
when its complete physical texture descriptor matches. Allocation scopes record
cache misses; `CopyResource.screenSnapshot` and `CopyResource.screenCommit`
record the two original ordered full-surface copies. A different physical extent
replaces the cached surface. Historical SSAA4x runs doubled each scene
dimension: a logical 3440x1440 scene used 6880x2880 backing. SSAA menu modes
have since been removed; the independent Render scale control determines
the current internal extent.

Recorded skin, rigid, mono and sky draws skip constant-buffer updates only when
their effective inherited bytes exactly match the last upload. Changed uploads
have `D3D11.UpdateSubresource` scopes. Replay ownership, released-state and
inheritance checks still run before reuse. These optimizations reduce resource
churn; a remaining slow driver call can still reflect pressure from earlier
queued rendering.

Z-prepass meshes share their identical declaration, depth shader and fixed
render states on the owning device. Changed geometry still uploads its own
immutable buffers; device changes replace the shared objects. GPU copy waits
reuse submission evidence from a successful Present or an explicit idle Flush
to avoid a redundant Flush. Occluded presentations retain the explicit submit
fallback, and actual event-query completion remains required.

Audio producer admission uses the existing heap index for containing-allocation
lookups and copies decoded interleaved samples into the bounded ring in at most
two contiguous spans. Full-span, generation and closing-owner checks remain.
Codec admission, the original audio lock and guest wait behavior are unchanged.

Section and contributor totals use exclusive wall time: an inner GPU wait is
charged to waits rather than counted again as rendering. Call records use
inclusive elapsed time so every slow service call is still visible. Native
callbacks into original CPU code are charged to guest execution. Worker call
records carry their own thread IDs; worker elapsed time is not added to the
frame owner's wall time. Follow a frame-thread wait's object identity to the
corresponding worker records when investigating a dependency.

Contributor storage is fixed and uses bounded probing. If it fills or encounters
too many collisions, `untracked_contributor_ms` reports the unattributed portion;
section totals and individual slow-call records still include that work.

Instrumentation observes existing operations. It preserves guest registers,
Windows LastError, host floating-point state, wait results, deadlines, and game
clock/scheduler requests. Enabled profiling necessarily adds measurement and
log-writing overhead; it does not deliberately sleep, yield, flush the GPU, or
retime the game. Logging uses the existing stderr buffering.

Automatic forwarding is generated by `tools/recompile.py` from the current
import mapping and native hook inventory, and is covered by the existing AOT
hash gate. After changing native sources, regenerate using the usual build
workflow. Focused tests cover threshold boundaries, nested scopes, frame splits,
worker isolation, state preservation, inactive profiling, and forwarding
coverage.

Mesh preparation now reports separate static, rigid, skin, sky, and mono
stream scopes, with nested source lookup, capture, decode, upload, and binding
work. Bounded `[MESH SOURCE CACHE]` rows describe exact hits, watched hits,
misses, invalidated bindings, residency, and eviction. They do not change
cache validity or retention. Identical rigid input layouts and pixel adapters
are shared per device; recorded rigid draws retain references to identical
cached immutable states and depth constants while owning separate mutable
vertex and pixel constant banks.

Resident and AMX fresh-context audio can share immutable decoded float images
under a 64 MiB, 128-entry cache. Full source ownership, generation, framing,
and content qualification still precedes each admission; every voice keeps
independent receipts, quotas, progress, and cancellation. Pinned images count
toward the bounds, and optional cache pressure or identity failure falls back
to ordinary decoding. Streamed continuations keep their actual codec state.
Resident receipt samples and one shutdown `[NATIVE AUDIO FRESH CACHE]` row
report reuse and residency. Path-only catalog preparation starts at factory
startup; errors remain demand-time errors, and workers join after guest
threads have stopped.

The application prepares the Windows audio endpoint before original execution
and its first loading presentation. This moves endpoint initialization out of
the loading-frame path; it does not remove that work from total launch time.
The original Dac0 acquisition validates its CPU owner before consuming the
prepared graph. Source configuration, playback, callbacks and retirement still
follow the original graph lifecycle. Preparation-only shutdown also releases
the graph and its main-thread COM reference.

Cold ITXD texture consumption reports separate `ITXD.decode` and `ITXD.upload`
scopes, keyed by the original texture record; upload logs retain its generation.
Decode converts owned tiled pixels, while upload measures CPU-side D3D resource
creation. These scopes do not wait for GPU completion.

Large qualified RGBA8 textures and the explicit base-only BC2 family may
prepare their pixels on one CPU worker after the original load has published
and verified its complete group and index. Jobs own copied input and use the
same decoder as synchronous demand. The minimum output is 1 MiB. Both kinds
share eight jobs and 64 MiB for extra input, reserved output, ready and pinned
memory. Budget pressure can retire the oldest unused queued or ready BC2 work;
running jobs, taken ready results and RGBA jobs remain protected. The
worker retains no guest pointer, runtime or GPU object. Original generation,
metadata, allocator and renderer checks still precede demand-time GPU creation.
`[NATIVE ITXD PREPARE]` reports admitted jobs; `PREPARE HIT/MISS` reports whether
first use consumed completed work. A missing, rejected or unfinished result uses
the existing synchronous decoder without waiting. Final release cancels stale
publication; teardown joins only the current bounded CPU conversion.

Static and rigid mesh upload scopes include separate decoded-cache lookup,
attribute validation, index ownership copy, cache snapshot insertion, and
first shared adapter creation. `D3D11.CreateBuffer.static/rigid.vertices` and
`.indices` measure the actual buffer creation calls. These are nested scopes:
their exclusive time moves out of the parent upload contributor, while the
parent's inclusive slow-call record still includes them. Compare all nested
contributors before drawing conclusions about an upload change.

The static decoder can transfer its private vertex allocation to the
exact-content cache after successful validation and immutable GPU upload.
This removes a second vertex snapshot allocation and copy. The cache owns the
typed vector and charges its retained capacity; indices still receive an owned
snapshot before transfer or eviction. Cache hits, skipped retention and failures
leave caller storage intact. The legacy span APIs continue to copy snapshots.
Live GPU meshes retain their immutable buffers when cache snapshots retire.

Qualified static Z-prepass uploads retain the complete 116-byte logical vertex
records and their original cache/admission bounds, while the immutable GPU
position stream stores 12 bytes per vertex. The other 104 bytes must already
be bitwise zero; one immutable zero stream is shared by the backend's device
and bound with stride zero. Each live mesh retains that stream independently
of cache reset or device replacement. Both streams have exact device, extent,
immutable-owner and binding checks. Vertex readback stages both actual GPU
buffers and reconstructs the complete logical records, including position bits.
`Mesh.static.packPositions` isolates the temporary position copy, and
`D3D11.CreateBuffer.static.zeroInputs` isolates the one shared zero allocation.
Per-mesh position and index buffer creation calls remain; fewer uploaded bytes
do not imply a proportional reduction in CPU driver time.

Static, shadow and mono depth-constant misses now use the backend's existing
bounded immutable scene cache while retaining their per-mesh fast caches.
Recorded mono, skin and sky draws also pin those exact depth buffers, and the
validated all-zero recorded mono Boolean bank uses its existing immutable owner.
Writable material banks remain private so replay updates cannot change sibling
draws. Cache reset/eviction and device retirement leave retained live/recorded
buffers valid. `D3D11.CreateBuffer.scene.depthConstants` measures actual shared
cache-miss allocation only; hits return before that scope. The former static
per-mesh allocation label therefore disappears without establishing zero cost
or a timing gain by itself. Tests inspect the actual bound/captured GPU buffers
and creation counts, rather than treating a cache lookup as upload evidence.

`Immediate.bindStorage` measures the original immediate buffer binding, with
`Immediate.prepareStorage` isolating native owner validation and queued-write
draining. Setup no longer copies the unused full 1 MiB allocation. Every
qualified trail, billboard, beam, immediate effect and decal consumer uploads
its final vertex range before drawing; the general unported consumer rejects
before drawing. Allocation/cursor checks, bindings and original guest cache
publication remain. Material setup and begin/end state publication have
separate scopes. The historical
`SimpsonsNativeDeviceEpilogue` label names immediate geometry setup, rather than
an end-of-frame operation.

Capture-enabled runs include first-use recording color evidence: up to sixteen
payloads perform full-target before/after readbacks, comparisons, and file
writes. Those outer recording execution times include diagnostic work. Use
capture-free runs for normal timing, separate bounded captures to verify the
scene, and inspect nested execution scopes before attributing the cost to the
driver. Stack sampling also adds pauses and is evidence of attribution rather
than a clean timing benchmark.

When both `SIMPSONS_STALL_PROFILE=1` and a nonempty
`SIMPSONS_STACK_SAMPLE` specification are set at process launch, every owner
frame also emits `[STACK FRAME CLOCK]`. The record identifies its process,
thread, profiler generation, frame number, QPC frequency and complete interval.
It reuses the profiler's existing boundary timestamp. Clock logging is charged
to diagnostic overhead in the following frame. Ordinary sampler-off runs add
no frame-clock reads or records; disabled profiling still returns immediately.

Raw stack captures keep their existing `M` module and `S` sample lines. Separate
`Q` metadata establishes the owned process and QPC domain, while each `T` record
links an `S` ordinal to timestamps before suspension, after context capture and
after resume. `tools/stack_profile_clock.py` correlates only samples whose entire
conservative suspension-to-context interval lies strictly inside a matching
owner frame. Boundary intersections remain ambiguous. Worker, legacy and
outside-window samples remain unknown and are retained with their raw stacks.
The resume timestamp records suspension cost; it does not assign the captured
context to a later frame. Sampling perturbs execution, so these counts diagnose
work within a frame and do not establish clean frame-time improvements.

The selected original byte decoder `8281E9B0` uses inline scalar byte and word
access helpers qualified by its complete original body hash and access counts.
The decoder's instruction body, sequential overlapping copies, token decisions,
cursor-store order and volatile accesses remain unchanged. Import-page fallback,
canonical guest aliases and exact-width write-watch barriers retain the ordinary
memory contract. Differential tests execute the original external-helper body
against the selected inline body, including complete context, memory, cursor,
fault and watched-write outcomes. This targets recurring loading-stage decoder
overhead; it does not attribute unrelated cold scene or movie cleanup stalls.

The selected sorted-pair shift `82B2A8D0` likewise inlines only its scalar
halfword and word accesses. A complete original-body hash, exact access counts
and reversible identifier substitutions preserve every shift, comparison,
save/restore and the original `82B64600` growth call. Its original growth and
sorting algorithm remain in charge. Halfword accesses retain big-endian
conversion, ordered volatile width, import fallback, canonical aliases and
exact-width watched-write behavior. The independent reference uses external
access helpers; a bounded test growth callback verifies call-boundary state
and partial failure behavior rather than replacing or proving the real allocator.
The observed pair-shift subtree includes unselected callees and is not an
estimate of achievable access-helper savings.

VM frame setup `82844AB0` is qualified separately by its complete original
body hash and 61 scalar sites: 36 word loads, 17 word stores, two halfword loads
and six byte loads. Only those identifiers change. Frame/stack growth, hooks,
coercion, indirect callbacks, save/restore and native nonlocal transfer remain
original. The nil loop still writes one word per eight-byte cell with its
intervening top/end reloads; it is not bulk-cleared or preflighted. The outer
`828389F0` uses unsupported U64 accesses and remains unselected. Scope coverage
hashes the emitted body; separate reversible qualification pins the original
body before identifier substitutions. The measured VM setup residual includes
unselected callees and does not predict access-helper savings.

Thread-exit queries continue to observe actual Windows thread completion with
a retained, counted guest thread owner and a nonblocking native handle poll.
The full handle-table scan used only to label the first completed diagnostic
runs once in that diagnostic path, rather than on every active poll. Its handle
identity is a current diagnostic snapshot; concurrent guest close may report
zero while the retained native owner remains valid. The original guest poll,
output, dereference and status-code policy are unchanged. Movie shutdown also
delivers queued work while polling, so repeated yield time does not by itself
justify replacing the entire loop with a blocking wait.

Static source-cache misses transfer the three already captured raw planes into
the cache after decoder, index, source-header, upload and bind validation. The
legacy span API still copies its snapshots. Owned entries charge retained vector
capacities to the existing 512-entry/64 MiB limits. List/hint allocations occur
before transfer, and the original guest write-watch arm is checked after staging;
stale arms leave an exact-content entry without a trusted binding. Reused guest
hint keys are published before old-entry eviction.

The bounded `[MESH SOURCE CACHE]` record also reports `owned_insertions`,
`owned_logical_bytes` and `owned_capacity_bytes`. These cumulative observations
prove transferred volume, not time saved. Output frequency and the profiler-off
early return remain unchanged. `Mesh.static.rawCapture` and
`Mesh.static.cacheInsert` continue to measure their respective owner work;
absence from the top eight does not mean zero cost.

The immutable ITXD factory and bounded GPU worker remain covered by standalone
fixtures, but Runtime GPU admission is disabled. The broad BC/RGBA experiment
reduced final transition frames while increasing marker-defined house-loading
sums in both clean pairs. The narrow existing-large-RGBA policy improved LOC
frames and later steady-window means/tails in both pairs, but first-house and
loading results were mixed. Neither experiment establishes repeatable overall
transition-stall relief; their frozen binaries, successful tests and full traces
remain in build/pacing-video-20261007. This selection does not imply that every
GPU preparation policy regresses performance.

The game retains CPU-only large-RGBA and qualified base-only BC2 preparation
and original synchronous demand-time texture creation. The RGBA profile query
shares that CPU helper's original qualifier and immutable minimum output threshold without
submitting work, locking or changing statistics. GPU-only preparation counters
and HIT/MISS/SKIP/OFF fields belong to the preserved experimental binaries;
their absence from current Runtime logs is not a measured zero.

The experimental decoded rigid-vertex ownership transfer remains in preserved
snapshots and frozen binaries, rather than the default decoded-cache caller. It demonstrably transferred
248 snapshots/16,575,162 bytes without duplicate cache copies. At saved primary
dimensions, however, both paired first-house frames and early-window means
were higher. That conservative selection does not assign the change in time
to ownership transfer; the binary comparison also includes source/PGO/factory
differences and materially different system load. Current decoded rigid-vertex
cache misses use the prior span insertion. Separately, capture-off rigid raw
source snapshots transfer their vertex, index and declaration planes to the
owned source cache after complete decoder, index and renderer validation;
diagnostic capture retains the copying path. The accepted static owned-source
path is retained.

Startup attribution adds five existing profiler scopes:
Startup.D3D11CreateDevice.initial, Startup.D3D11CreateDevice.fallback11_0,
Startup.presentationFilters.create, Startup.resources.initialize and
Startup.attachWindow. Device scopes enclose actual API calls separately while
retaining the exact E_INVALIDARG-only retry. The filter scope measures only
an actual pipeline cache miss after submission validation, including creation
triggered by nonzero sharpening. Resource and window scopes wrap only their
original calls. Null context inherits the active original CPU wrapper metadata.
Disabled profiling adds no QPC reads or logging; existing guards preserve
incoming CSR and LastError. Rows/calls retain the profiler's eight-contributor
and two-millisecond emission limits, so missing values remain unknown.

### Immediate and particle destination history

Ordinary single-sample immediate draws and single-sample particles snapshot
only their rasterized coverage into an integer destination texture. They then
write directly to the original color target. Each immediate triangle reads the
preceding triangle's result; both triangles of a particle quad share the same
snapshot, and successive quads retain their original order. This removes the
initial and final scene copies from these paths.

The original shader still computes blending, alpha rejection, packed RGB10A2
codes and depth. The direct output normalizes those already-quantized codes
for the target's UNORM view. Snapshot shaders recover integer codes from that
typed view. Coverage snapshots use the original vertex shader, rasterizer and
scissor, with depth and alpha rejection disabled. Color masks and depth state
apply to the real draw. All resource aliases are unbound between the two passes.

Radial, projected and active temporal immediate draws retain their previous
paths. The
conservative immediate coverage calculation can skip proven invisible work;
uncertain transforms retain the conservative fallback. Raster, blend and
sampler objects reuse the existing bounded device-owned caches with identical
descriptors. This changes neither the guest draw order nor the saved settings.

The backend fixtures compare the direct paths with the retained integer-copy
implementation, including packed pixels, defined depth/stencil bytes and
retained context state. Separate GPU checks verify all 1,024 codes in each RGB
channel and all four alpha codes against independent packed values. These
checks establish rendering equivalence; clean gameplay measurements establish
performance. Neither establishes removal of cold asset-loading stalls.

Immediate vertex uploads use a bounded, per-pipeline dynamic buffer. Each
submission maps with WRITE_DISCARD before any draw, preserving queued storage;
the buffer grows only for a larger accepted packet. The exact filtered quad
index sequence reuses an immutable index buffer. Other index sequences retain
their dedicated uploads. Particle vertices and vertex/pixel constants use the
same DISCARD lifetime rule, and unchanged particle depth constants use the
existing immutable cache. Both paths retain a separate 32-entry depth-state
cache with their original zero disabled-stencil fields. These descriptors are
kept distinct from the scene depth cache.

The private reference paths retain dedicated immutable uploads and depth-state
creation. Queued-draw checks vary geometry and constants without intervening
readback, then compare complete color and defined depth/stencil values against
that reference. This tests storage lifetime as well as single-draw pixels.

### Recording retirement and repeated state setup

The original recording release hook waits for the payload's most recent
submitted event, rather than inserting a whole-backend idle event. The wait
validates the device, owner thread, sealed payload, pending entry and immutable
submitted-event identity before refreshing completion evidence. Only an actual
successful event result proves completion. The new wait's never-executed and
already-retired fast paths perform no query polling or Flush. Existing release
bookkeeping retains nonblocking retirement of already-completed recordings.
The submitted case uses the
existing bounded completion wait and retires only that payload; it does not
create/end another query, drain unrelated recordings or submit queued Im2D
work. Genuine shutdown and resize waits retain their full idle behavior.

Original screen draws retain up to 128 blend states keyed by the complete
original selector and color-mask descriptor on their owning device. Their
samplers use the existing exact 16-entry scene cache; matching post-depth
states use the existing scene depth cache. Movie screen scratch reuse compares
all physical texture descriptor fields and device identity. Replacement is
published only after successful creation and validation. Ordered snapshots,
movie planes, depth, shaders and packed color arithmetic remain unchanged.
The screen GPU fixture counts actual creation calls: after its cold state
population, 12 queued warm passes perform zero blend, depth or sampler
creations on WARP and hardware. Movie fixtures compare queued mixed
movie/screen submissions with fresh independent GPU references.

Immediate and particle draws save and restore pixel-shader resource slots
0 through 3 and sampler slots 0 through 2 in batches. Every getter reference
is retained, including duplicate and null bindings; restoration keeps the
original resource-hazard order. This removes ten context calls from each
nonempty direct draw. Successful billboard BEGIN/END trace pairs sample the
first four admissions and every 512th thereafter, using the same decision for
both rows. Failure reporting remains unsampled. Sampled rows cannot estimate
total draw or emitter counts.

The 2026-10-12 90-second diagnostic run passed gameplay, source, preference
and owned-close checks with STALL profiling, 32 contributors and D3D call
counters enabled. One guarded wholly stationary 600-presentation device
counter window reported 24.5 sampler creation attempts per frame, versus
151.85 averaged across the two guarded windows of the preserved baseline
diagnostic. Blend and depth creation attempts were 1.0 each, versus 44.75.
Buffer creation attempts increased from 70.3 to 86.9. Texture2D/RTV requests
remained 1.0 each and query requests remained 9.0. These are rounded observed
API attempts for those populations, rather than guaranteed success counts
or matched-world-state causal measurements. Context1 coverage remains
incomplete; unprinted GetData, Flush or End counters do not prove zero calls.

The new run retained 109 paired billboard traces, including 69 in the stationary
phase, with no orphan or cross-frame pair. It logged no recording-release call
above the two-millisecond threshold. One route contributor reported 0.191 ms
of exclusive recording-release time; absent ranked entries elsewhere remain
unknown. Inclusive slow-call durations must not be added to their nested
exclusive GPU-wait durations. All 1,825 positive CSV intervals and outliers
remain available, including the 162.418 ms early frame and 127.661 ms first
stage frame. The pre-CSV startup owner record is retained separately.

Concurrent GPU activity made timing comparisons inconclusive. The current
stationary instrumented means were 8.781 ms guest, 17.569 ms rendering and
21.398 ms waiting, including 10.405 ms pacing and 10.385 ms exclusive
completion-query work; these do not establish a wait or frame-rate gain.
Every one of the 1,098 positive stationary intervals had an owner header.
Other runs or phases with sparse headers support means only over their
logged subset, while contributor totals divided by all intervals are lower
bounds.

The [diagnostic audit](K:/SimpsonsNativeCopy/build/performance-20261012/helper-rebase/normal-profiler-final-b/medal-profiler/stall-reuse-clean-60-final-normal-medal-profiler-b/profile-analysis/profile-summary.json)
preserves raw rows, query identities, outliers, coverage and guarded device
windows. The [phase-boundary correction](K:/SimpsonsNativeCopy/build/performance-20261012/profile-analysis-correction.md)
records why the historical route endpoint was corrected to the final neutral
ACK. Current and baseline diagnostics use the same corrected parser; original
historical files remain unchanged. No cold-loading or whole-game 60 FPS claim
follows from these counters.

## GPU attribution and pixel history (opt-in)

With `SIMPSONS_STALL_PROFILE=1`, adding `SIMPSONS_GPU_PROFILE=1` ends a D3D11
timestamp at every rendering/presentation scope boundary on the graphics owner
thread. Every 120 resolved frames `[GPU PROFILE]` lists GPU milliseconds per frame
by scope, and frames over 33 ms print `[GPU SLOW FRAME]` with their top scopes.
GPU time spent idle waiting for the CPU is charged to the scope in which it idled,
and thousands of timestamps per frame add GPU cost, so treat the numbers as
attribution, not clean timing.

`SIMPSONS_GPU_COLUMN_PROBE=<frame>[,<x>[,<y0>,<y1>]]` additionally records a pixel
history for one frame: at each outermost rendering scope boundary it copies rows
`[y0,y1)` of column `x` (default: the centre column, rows 900-1300) of the bound
single-sample RGB10A2 color target, then prints, per run of rows, every scope that
changed those pixels with the value it left (`[GPU COLUMN PROBE]`). Rows merge
into one run only when both the writers and the values they left match, so a value
that alternates from row to row shows up as one line per row. Draws inside
recorded command lists are attributed to the scope that executes the list.

The D3D11 runtime keeps the immediate context's dispatch table inside the object
and rewrites its draw entries on the fly, so vtable hooks never see draw calls.
These tools therefore never hook draws; they only end queries and issue
GPU-ordered copies.

`SIMPSONS_DRAW_CALLERS=1` (independent of the profiler) prints each distinct
guest return-address chain (up to ten frames, at most 256 chains) that reaches
the 2D draw paths, tagged UI or world. It attributed the HUD's 2D pass to
`826D5CB0` for photo mode's HUD removal.

## 2026-10-13 stall fixes

- **Radial immediate effects** (radial blur/speed fans) now use the direct
  per-triangle destination snapshot like other immediates (`PSRadialUNormDraw`),
  instead of copying the whole scene target once per triangle plus twice per draw.
  At 4300x1800 those copies made Eighty Bites GPU-bound for seconds at a time
  (~80 ms GPU frames). The retained full-copy path stays the test oracle
  (`ImmediateBackendTests` compares both, including radial blends, culling,
  query visibility and depth).
- **Native RefPack decoder.** `runtime/native_byte_decode.cpp` replaces the
  generated `sub_8281E9B0` (the loader's byte decoder) with an implementation that
  copies whole tokens when their memory is validated plain guest memory and
  executes an exact transcription of the original instructions otherwise, so the
  register file, trace, faults, partial writes and write-watch invalidations match.
  `NativeInlineMemoryContract` compares it with the original body with and without
  armed watches, over known tokens and seeded random/garbage streams.
- **Frame limiter during bursty phases.** The adaptive cadence only holds frames to
  the 85th percentile when that is within 25% of the typical frame; loading screens
  pace to the cap alone. The Springfield hub reaches its first stage frame in
  ~1.6 s instead of 3.1-3.6 s at the 120 FPS cap.
- **No audio device** no longer stops startup: `NativeAudioOutput` falls back to a
  silent real-time clock when the default endpoint is missing.
- **Display scaling above 100%**: the process is per-monitor DPI aware and window
  extents are measured in the window's own DPI context.
