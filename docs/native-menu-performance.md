# Menu performance work

The active user goal now supersedes main-menu progression: get the native game
to60 FPS through at least Saved Games. The first live save created in Boot216
remains intact and was read successfully in Boot217. The later unimplemented
XamContentGetDeviceState and first3D shadow activation are outside this target;
neither has been bypassed or implemented for the performance work.

## Measurement baseline

Optional `--frame-timing <csv>` records actual QPC intervals between completed
presentations, accepted-display status and inclusive CPU time spent in present,
upload, Im2D, its program qualifier and native draw submission. It does not
change simulation clocks or frame pacing. Rendering capture work is included.
Program/draw columns are subsets of Im2D, not additional frame time.

Add `--frame-timing-frames-only` alongside `--frame-timing <csv>` to omit
per-packet diagnostic clock reads and pacing instrumentation. This writes only
presentation, elapsed_ms, frame_ms and display_accepted. Absent bucket values
are not emitted as zeros. The regular launch without timing remains unchanged;
this option improves measurement overhead, not the underlying game execution.
Both native startup and tools/run_native.py accept the explicit option.

The timing build passed OriginalDriverLifecycle in5.82s and the screen/save
bridge targets in0.83s. Regeneration has242 unsupported imports,311 files and
zero diagnostics. Full130/130 integration in239.93s predates timing-only changes.

Boot218 uses unchanged rendering, hardware D3D11 featurelevelB100, existing
Player profile/save folder, timing CSV and raw capture sampling. The harness
reached its900-second limit and terminated the child. Its same session was
closed. No game is running. It reached the autosave notice, but the final A was
queued after termination. Its folder named saved-games contains an autosave
frame, not Saved Games evidence. Boot217 previously viewed the actual populated
Saved Games menu at native-frame-1214169.

`build/boot-218-frames.csv` retains4440 complete rows (a final incomplete buffered
row, if present, must be ignored). Mean measurements:

| Interval | Frame ms | Present ms | Upload ms | Im2D ms | Program ms | Draw ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Title, frames700..780 |1169.445|54.371|65.072|986.453|838.403|97.054|
| Autosave, frames4320..4440 |167.350|47.207|5.339|98.231|85.269|8.246|

The title submits about3677 Im2D packets per frame. Original shader-source and
macro building is repeated for every packet. Frame timing confirms that this
is the largest current cost. Presentation independently costs about45..55ms;
three native GPU completion waits use Sleep(1), plus Present(1,0), and remain to
be separately investigated. CPU staging is physically committed/decommitted
per packet. The integer blend path allocates four immutable upload buffers per
packet and copies the full target before each triangle; further optimization
must preserve depth, alpha, overlap ordering and packed color quantization.

## Verified optimizations

The first optimization caches successful program validation per Runtime, keyed
by all eight40-byte original pixel-stage records, all original vertex-key input
fields, dimensions, textured mode and formatting callback identity. Live queue/
override/explicit-shader guards precede each lookup. A hit republishes the exact
168-byte original macro output. New or rejected inputs still execute the full
original builders; only successful results enter the bounded32-entry cache.
The original CPU contract tests add reuse, output restoration and changed pixel
state rejection to existing lighting/fog/texture-transform rejection coverage.
The cache build and OriginalDriverLifecycle passed (5.79s). Boot219 title
frames700..780 measured294.226ms total,40.979ms present,60.881ms upload,
132.982ms Im2D,1.101ms program and87.305ms draw. This is an actual improvement
from1169.445ms/frame. Boot219 was stopped deliberately after4385 complete
timing rows; it entered an attract movie and did not verify Saved Games.

Driver-owned256KiB staging now survives between original memcpy uploads and
is freed at driver cleanup. Completed packets retain independent byte snapshots
and real raw GPU uploads. Routine per-packet upload/draw/reset logging is sampled.
Depth-only shaders handle ZERO/ADD/ONE and zero color masks with actual alpha
rejection and quantized depth, preserving draw order without copying color.
GPU query polling uses a thread-owned high-resolution100us waitable timer,
falling back to Sleep(1) when unavailable. GetData still proves completion and
the5000ms GPU deadline remains. Present(1,0) is unchanged.

The memory/wait build passed three focused tests in2.56s, plus620940 hardware
Im2D checks and the hardware presentation contract. The latter uses a hidden
window and reports occluded, not scanout proof. Boot220 displayed the storage
warning and accepted the real folder. The final captured image was a blank TV
transition; its periodic capture budget subsequently expired. It was stopped
deliberately with15840 timing rows. Frame times on storage UI were about39ms,
with5.7ms presentation; no60 FPS claim or Saved Games capture was made.

Native Im2D now uses a655360-byte dynamic vertex ring with NO_OVERWRITE for
fresh ranges and DISCARD at wrap, immutable list/strip index buffers preserving
parity, and bounded caches of exact constant-buffer bytes. New queued depth
fixtures cross multiple ring generations and mutate caller vertices before
GPU completion. The ring build passed three focused tests in2.42s and624396
hardware Im2D checks. Regeneration remains242 unsupported imports,311 files,
zero diagnostics. Full130/130 integration still predates performance changes.

## Original pacing measurement (Boot221 ended)

`--capture-on-request` disables sampled captures. Create `capture.request` in
the opted-in capture directory to capture the next completed front; the request
is removed only after pixels and metadata close successfully. The helper
`tools/request_native_frame.py <capture-directory> --output <new-directory>`
requests, copies and renders a fresh frame, with a10-second bounded wait.
This works beyond the old7200-presentation sample limit.

Boot221 uses the existing real profile/save, optional timing, and on-request
capture. Its original wait is measured at826B7B70..826B7CC0 without changing
clocks, scheduling, registers or the original body. Live logs confirm interval2,
enabled1, refresh bits426FC28F (59.94Hz), factor37A83151 and caller826B8398.
The scheduler constructor82718D48 receives literal2 from82867A70, and stores
it at owner+144. Thus the original wait enforces roughly33.37ms before present.
Original82718788 resets owner+136 to current timebase AFTER presentation.
This run retained the original frame limit.

Boot221 late movie frames averaged37.588ms total,4.258ms present and30.498ms
original pacing. Title frames ending1604 averaged105.909ms total,8.943ms
present,8.313ms upload,39.994ms Im2D,0.793ms program,14.346ms draw and0.001ms
pacing. The title is still CPU-bound despite the renderer savings. A freshly
requested title capture (native-frame-1432228) showed Press START; START was
then sent through the normal command channel. Several later taps were delivered
but the run returned to the title/attract movie rather than verifying Saved
Games. The game was stopped deliberately (process17808, verified native EXE).
No game is currently running. On-request captures worked at draw counts beyond
9million and after the old periodic capture limit; no stale capture was treated
as Saved Games evidence.

## 60 FPS setting and longer controller press (live verified in Boot222)

`--frame-rate 60` changes the real original scheduler constructor argument from
two refresh intervals to one at82718D48, only for caller82867A78 with original
input2. Every constructor field and callback still comes from the original body.
At the original post-present timestamp store8271878C (caller826B83D4), it uses
the real time sampled at native presentation entry. Thus the next original
deadline includes native GPU presentation cost. It checks the actual scheduler
owner at82D576A0. Timebase frequency, game elapsed time, simulation, the original
scheduling body and Present(1,0) are unchanged. Default launch retains the
original behavior. The native option targets nominal60/59.94Hz; performance
through the title still needs CPU work.

`tools/send_native_input.py --file <commands> --hold START` queues START_HOLD.
The source holds the button for250ms after consumption, then releases for one
poll before the next queued command. All eight buttons support _HOLD; ordinary
single-poll taps retain their existing behavior. Discard/modal ownership clears
held input. Deterministic clock tests cover repeated polls,249/250ms expiry,
ordered queued input and discard.

The new OriginalNativeFrameRate test executes the real constructor and wait,
checks all unchanged fields,1/2 interval real deadlines, caller/owner rejection,
and native timestamp behavior. The focused build passed the frame-rate,
controller and driver tests3/3 in2.64s. `build/native-60fps-regenerate.log` and
`build/native-60fps-build.log` pass. Full integration passed131/131 in102.19s
in `build/menu-performance-integration-build.log`.

Boot222 reached the populated Saved Games menu with the existing slot intact:
`build/captures/native-loading-222-saved-games/native-frame-11905249.png`.
Light startup frames1200..2300 averaged59.281FPS (16.869ms), but Saved Games
frames51481..51720 averaged20.333FPS (49.181ms), including30.040ms waiting
for front-copy completion. Title remained about105ms/frame. The verified
process27672 was stopped deliberately at Saved Games, without loading a slot.

## Bounded UI color copies and optimized CPU inlining

Native Im2D now copies conservative transformed primitive bounds at identical
pixel coordinates, with a two-pixel rounding margin. Each destination-reading
triangle still sees all preceding color results; packed integer quantization,
channel masks, real depth and alpha rejection remain unchanged. Replace-color
packets use one ordered DrawIndexed because their shader never reads prior
color. Three focused contracts passed in2.35s and624396 hardware checks passed.

Boot223's visible Press START was matched from a fresh renderer frame by
`tools/press_native_start.py`; one250ms START_HOLD reached the storage warning.
Normal A presses selected the existing folder and acknowledged autosave.
Saved Games was visually verified at presentation7877:
`build/captures/native-loading-223-saved-games/native-frame-1526742.png`.
Frames8372..8611 averaged39.403FPS,25.379ms total,7.645ms present,
5.981ms front-copy wait,.558ms upload,3.265ms Im2D and1.397ms native draw.
The title still measured about103..105ms. Process38532 was deliberately stopped.
Both real save payload and index SHA256 remain equal to the Boot216 originals.

RelWithDebInfo now explicitly uses /Ob2 on SimpsonsPPC, SimpsonsRuntime and
SimpsonsGraphics, enabling the normal optimized inliner instead of /Ob1's
explicit-inline-only policy. PPC /fp:strict and all source/ABI guards remain.
`build/menu-inline-integration-build.log` passed131/131 in99.88s. Boot224 is
the next live measurement; no60FPS-through-Saved-Games success is claimed.
The new bounded `tools/profile_native_cpu.py` samples only a verified game
thread's instruction pointer, resumes after each read, and uses local PDBs.
Its diagnostic pause overhead must be excluded from FPS evidence.

Boot224 ended deliberately at the viewed populated Saved Games menu
(`native-loading-224-saved-games/native-frame-8575975.png`). Frames21046..21285
averaged42.425FPS/23.571ms,8.771ms present,7.115ms copy wait,2.821ms Im2D.
The first CPU sample crossed into the attract movie and is not a title profile.
The second sampled5133 instruction pointers on actual title thread27576:
1746 samples were at win32u!NtGdiDdDDIGetDeviceState+0x14, identified against
the installed DLL export table. Runtime::pointer/PPCGuestPointer/checkRunning
accounted for670/312/124 samples; heap/set operations were also prominent.
Sampling is biased by pauses and system calls, so percentages are diagnostic.

`renderer/device_availability.h` registers ID3D11Device4's one-shot removal
event with a thread-pool callback that publishes an atomic flag. Owner guards
check this flag; HRESULT checks on resource/Map/GetData/Present operations
remain. waitIdle also queries device status before and after real GPU retirement.
Unsupported older interfaces retain synchronous polling. Teardown unregisters,
cancels/drains callbacks, then closes the event. The presentation fixture injects
the actual registered event on an isolated device, verifies submission rejection
and a second device's independence; this does not claim real GPU removal.
The debug Im2D/movie fixtures reconnect their monitor when replacing the device.
Three focused contracts passed2.39s, hardware presentation and624396 Im2D
checks passed, and both movie backends plus Im2D passed3/3 in1.38s.

Boot225 reached the viewed populated Saved Games menu at
`native-loading-225-saved-games/native-frame-2966192.png`, then process33440
was stopped deliberately. Frames16170..16409 averaged43.862FPS/22.799ms,
9.662ms present,7.997ms copy wait,2.058ms Im2D and.800ms native draw.
Heavy title frames2330..2524 averaged79.407ms,23.886ms Im2D,4.886ms native
draw and6.815ms upload. The60FPS-through-menu target is still incomplete.

The next change replaces per-node unordered-set allocations in the two hot
camera raster registry/list walks with bounded constant-space Brent cycle
detection. Every node/link/owner check still runs; limits stay256/65536 nodes.
New fixtures cover1024 cycle shapes, exact valid limits, and actual cyclic/
duplicate guest lists rejecting without changing raster ownership. Regeneration
passed311files/zero diagnostics. Full integration passed132/132 in99.32s in
`build/menu-chain-integration-build.log`.

Boot226 reached viewed populated Saved Games at
`native-loading-226-saved-games/native-frame-5759560.png`, then process36808
was deliberately stopped. Frames27361..27600 averaged45.950FPS/21.763ms,
10.399ms present,8.814ms copy wait,1.448ms Im2D and.720ms draw. Heavy title
samples excluding the CPU-profiling interval averaged62.175ms total,3.889ms
present,6.994ms upload,16.091ms Im2D and4.846ms draw (119frames in2331..2590,
excluding2370..2510). The clean5112-point CPU sample on thread9248 attributed
1223/541/222 samples to Runtime::pointer/PPCGuestPointer/checkRunning; the
former GetDeviceState syscall hotspot disappeared. The fresh prompt watcher
sent START automatically after sampling; ordinary A presses reached Saved Games.

The pending memory change inlines the existing cancellation and Runtime::pointer
bodies only at PPCGuestPointer's two call sites. The public cancellation API and
standalone lifecycle fixture use the same shared body in checked_running.h.
Mapping, aperture, import, page permission and shutdown checks are retained.
The raster registry/list walk now validates each complete node's existing range
once, then reads its live words through volatile native pointers, retaining
the existing endianness, link, cycle and ownership validation. Regeneration
passed311files/zero diagnostics and full integration passed132/132 in97.68s in
`build/menu-memory-inline-integration-build.log`. No compiler warnings/errors
were reported. Boot227's initial198 heavy title frames1752..1949 averaged
49.166ms total,3.585ms present,6.948ms upload,12.699ms Im2D and4.753ms
native draw. The fresh title watcher sent START once and the actual storage
warning was viewed. Normal A presses selected the real folder and acknowledged
autosave. Populated Saved Games was viewed at
`native-loading-227-saved-games/native-frame-5723281.png`.
Frames22365..22604 averaged47.443FPS/21.078ms,11.911ms present,10.176ms copy
wait,.473ms upload,1.386ms Im2D,.040ms program and.786ms native draw.
The existing save and index hashes remain unchanged. Boot227 was deliberately
stopped at Saved Games (verified process29276, harness session92853 closed).

The Im2D renderer now groups destination-reading triangles into ordered layers.
Conservative screen tiles impose strict order on all overlapping bounds; only
disjoint triangles share a color snapshot. Stable ordering within each layer
allows adjacent original triangle indices to share a DrawIndexed call. Every
triangle still executes. A bounded planning budget falls back to sequential
layers. The actual copy counter increments at CopySubresourceRegion only.
Tests compare every packed color/depth/stencil pixel against original triangle
order for 32 grid cases spanning blend, texture, alpha, cull and depth states.
The grid uses at most8 copies instead of38. Current hardware Im2D validation
passed1,804,268 checks; three focused contracts passed2.38s.

Boot228 reached the viewed populated Saved Games menu at
`native-loading-228-saved-games/native-frame-8175474.png` using the fresh title
watcher and normal held A inputs through storage selection and autosave.
Frames33046..33285 averaged58.083FPS/17.217ms,6.868ms present,5.197ms copy
wait,.444ms upload,1.822ms Im2D and1.247ms native draw. Heavy title frames
1756..2030 averaged49.140ms total,3.451ms present,6.818ms upload,13.264ms
Im2D and5.216ms native draw. Process32168 was deliberately stopped at the
populated menu; harness59452 and title watcher62675 completed.

The next measurement adds optional preflight/setup/reset CPU timing columns.
They cover native validation previously outside the Im2D bucket; existing game
behavior is unchanged. Regeneration passed311files/zero semantic diagnostics.
Full integration passed132/132 in99.39s in
`build/menu-validation-timing-integration-build.log`.
Boot229's360 heavy title frames1754..2304 excluding profiling1910..2100
averaged49.241ms total,5.640ms preflight,1.596ms setup,.249ms reset,
6.961ms upload and12.967ms Im2D. The5132-sample CPU profile confirms
Runtime::pointer/PPCGuestPointer still dominate native CPU samples. Inline
PDB frames explain standard-library helper labels; they do not imply those
helpers are separate uninlined calls. No profiling pauses are FPS evidence.
Start was automatically delivered once; Boot229 was deliberately stopped at
the viewed storage warning (process42472, harness48187, watcher50307 closed).

The pending camera validation change returns each root raster's qualified
identity and native attachments together, avoiding duplicate metadata/list
walks inside the same camera check. Existing ownedColor/ownedDepth APIs still
validate their live records independently. Ordinary raster metadata and dynamic
vertex ownership now read live volatile fields through a freshly checked whole
allocation/node range. There is no cache across original CPU execution.
New fixtures mutate every relevant dynamic record field and both viewport
raster roles, revoke page write access after successful validation, and require
immediate rejection without changing ownership or target bindings. Regeneration
passed311files/zero diagnostics. Full integration passed132/132 in97.37s in
`build/menu-camera-validation-integration-build.log`.
Boot230's204 heavy title frames1764..1967 averaged47.692ms total,
4.781ms preflight,.917ms setup,7.048ms upload,12.512ms Im2D and5.388ms
draw. Populated Saved Games was viewed at
`native-loading-230-saved-games/native-frame-3439633.png`; frames13154..13393
averaged57.658FPS/17.344ms,7.774ms present,6.145ms copy wait and1.738ms
Im2D. Process38720 was deliberately stopped there; harness42965 and title
watcher42706 are closed.

The pending native-buffer upload change uses a bounded1MiB dynamic source ring
with fresh-byte NO_OVERWRITE and DISCARD on wrap, followed by an ordered GPU
CopySubresourceRegion into the original DEFAULT buffer. Uploads over1MiB keep
UpdateSubresource. Resources, untouched bytes and immediate caller snapshot
semantics remain. A queued fixture interleaves500 overlapping updates across
four buffers, mutates each caller snapshot, crosses multiple ring generations,
and compares all output bytes after the queue. Oversized fallback is also tested.
Reference contracts: [buffer region copies](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copysubresourceregion)
and [dynamic mapping lifetime](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_map).
Focused build passed in `build/menu-upload-ring-build.log`. Hardware graphics
checks passed; three focused graphics/Im2D/presentation contracts passed.75s.
Boot231's496 heavy title frames1758..2253 averaged42.929ms total,
2.317ms upload,12.079ms Im2D,5.440ms native draw,4.732ms preflight and.845ms
setup. Normal Start/A inputs reached the viewed populated Saved Games at
`native-loading-231-saved-games/native-frame-3632238.png`; frames11197..11436
averaged57.806FPS/17.299ms,8.027ms present,6.463ms copy wait,.161ms upload
and1.706ms Im2D. Process24064 was deliberately stopped there; harness63617
and title watcher51860 are closed. Full normal integration passed132/132 in
98.06s in `build/menu-upload-ring-integration-build.log`. Both existing save
payload and index SHA256 still match the Boot216 originals.

The next experiment builds the same source in `build/native-thinlto`, with
CMake IPO/Clang ThinLTO enabled,8 link workers and a2GiB cache. PPC strict FP
and all existing source guards remain; the normal build configuration is
unchanged. `build/menu-thinlto-build.ps1` builds the game and real memory,
driver-lifecycle and frame-rate contracts after normal integration finishes.
Its log is `build/menu-thinlto-build.log`; combined session44613 completed
successfully. All424 build steps and3/3 selected real contracts passed1.85s.
Boot232 ran this separate executable with the same normal inputs, capture and
timing options. Its327 heavy title frames1734..2060 averaged42.633ms total,
2.340ms upload,11.926ms Im2D,5.603ms native draw,4.570ms preflight and.827ms
setup. This differs little from Boot231; IPO alone is not a demonstrated major
improvement. The fresh watcher delivered Start once. Process42260 was deliberately
stopped at the viewed storage warning `native-loading-232-storage/native-frame-2383310.png`;
harness41683 and watcher38078 are closed.
`tools/run_native.py --executable`
accepts only an existing SimpsonsNative.exe beneath the workspace build folder,
retaining the usual native path by default. Reference: [Clang ThinLTO](https://clang.llvm.org/docs/ThinLTO.html).

The pending memory change separates PPCGuestPointer's single-page accesses
of1..16 bytes into a small path with the same runtime/base, window closure,
cancellation, opaque-type, import, physical-alias and per-aperture page permission
checks. All large/cross-page/special/invalid cases use the previous implementation
and diagnostic catch, now named PPCGuestPointerSlow. No checks are bypassed for
successful accesses. New real Runtime fixtures compare20,608 combinations
against Runtime::pointer, including boundaries, independent alias permissions,
partially bound imports, opaque types, invalid widths and retired physical RAM.
Base mismatch and immediate cancellation are also checked. Regeneration passed
311files/zero diagnostics. Normal full integration passed132/132 in96.35s in
`build/menu-memory-fast-path-integration-build.log`; the real memory test reports
all20,608 differential cases passing. Combined session54120 completed the separate
IPO build and3/3 selected real contracts in1.98s in
`build/menu-memory-fast-path-thinlto-build.log`.
Boot233 ran that build. Its311 heavy title frames1459..1769 averaged38.084ms
total,2.251ms upload,11.091ms Im2D,5.622ms native draw,3.727ms preflight and
.664ms setup. Normal Start/A inputs reached the viewed populated Saved Games at
`native-loading-233-saved-games/native-frame-6174368.png`. Frames25321..25560
averaged58.995FPS/16.950ms,8.285ms present,6.732ms copy wait,.684ms idle,
.169ms upload and1.679ms Im2D. Process36716 was deliberately stopped at the
menu; harness29051 and watcher41711 are closed.

The pending presentation change removes the extra waitIdle after presentFront.
The latter already waits for the real transfer event following all preceding
immediate-context draw/buffer commands. No engine GPU work intervenes before
the original cursor reset. Explicit waitIdle remains available for other paths.
A new presentation fixture queues four real buffer uploads and a real event,
calls presentFront, then requires GetData(DONOTFLUSH) to prove that earlier work
is complete before any readback or additional wait. The idle CSV column remains
present and will be zero on this path. Regeneration passed311files/zero diagnostics.
Session59556 completed normal full integration132/132 in95.27s in
`build/menu-present-retirement-integration-build.log`, followed by passing hardware
presentation checks in `build/menu-present-retirement-hardware.log`. The hardware
fixture proves earlier buffer retirement using GetData(DONOTFLUSH); its hidden
window is occluded and makes no visible-display claim.
Boot234 runs the normal build, verified process9460, harness30539 with1800-second
limit. Session48509 completed its8-second CPU profile and fresh title watcher;
Start was delivered once. The profile has5169 samples on thread21724 and.847ms
maximum pause. Outermost PDB attribution includes1260 PPCGuestPointer,363
Runtime::pointer,877 ntdll,520 D3D11,308 Nvidia driver and264 VCRUNTIME samples.
The65 clean heavy title frames1661..1725, excluding1420..1660 around profiling,
averaged38.980ms total,3.496ms present,2.288ms upload,11.621ms Im2D,5.404ms
native draw,4.327ms preflight and.777ms setup. Profiling pauses are excluded from
FPS evidence; those measurements remain diagnostic rather than an exact CPU census.

Normal held A inputs selected the real storage folder and acknowledged autosave.
Populated Saved Games was viewed at
`native-loading-234-saved-games/native-frame-6783436.png`. Frames26114..26353
averaged59.885FPS/16.699ms,4.021ms present,3.139ms copy wait,.164ms upload,
1.675ms Im2D,1.208ms native draw,4.663ms original pacing and zero extra idle wait.
The save payload and index SHA256 still match their original values. Boot234 was
deliberately stopped at Saved Games (process9460, harness30539 closed) for the
next performance build. No slot was loaded.

Remaining measured bottleneck: the title. Candidates not implemented include
testing stronger compiler inlining of the now-small memory path, reusing owned
CPU input/decoded-vertex vector capacity to reduce per-packet allocations, and
reducing redundant validation while retaining all live ownership/permission
checks. Original PPC weak-function noinline attributes are unchanged. No draw
batching, omitted rendering, synthetic save/menu state or extra clock scaling
has been added. Saved Games now runs near60; the title does not.
The title is still CPU limited. No draw omission, fake counter, forced menu/save
state or clock scaling is allowed; the60FPS-through-menu goal remains incomplete.

The next experiment marks the small PPCGuestPointer definition always_inline,
retaining its noinline slow implementation and every successful-path check.
The previous IPO executable's actual matrix routine823EBD00 still calls
PPCGuestPointer for each scalar memory read (public offset11946096 in .text,
bridge offset143856; `build/native-process-sampling/boot-233-matrix-disassembly.txt`).
Normal regeneration passed311files/zero diagnostics, and full integration passed
132/132 in95.99s in `build/menu-memory-force-inline-integration-build.log`.
Combined session30440 is now building the separate IPO version and selected
real contracts in `build/menu-memory-force-inline-thinlto-build.log`.
No game is running; performance benefit from forced inlining is unproven.
The optimized game's link finished: the executable is248,007,680 bytes.
Its newly read PDB places matrix823EBD00 at .text offset41,115,920. The actual
machine code in `build/native-process-sampling/boot-235-matrix-disassembly.txt`
now contains the runtime/base, bounds and cancellation checks directly at its
scalar accesses, with a call only on the slow path. This verifies compiler
inlining, not a speed gain. The remaining optimized fixture links are pending.
The bounded CPU sampler now accepts an explicit workspace native executable,
verifies the live process against that exact path while holding its query
handle, and uses that executable/PDB for symbolization. Its default is unchanged.
Both Python files parse; direct checks reject a non-game executable and a Python
process supplied with the alternate game path before any thread suspension.
Combined session30440 finished successfully. The separate optimized build's
three selected real contracts passed2.07s. Boot235 runs that executable as
verified process13856, harness47114, with1800-second limit and the same original
profile/content stores. Its clean heavy title frames1994..2233 average27.425FPS,
36.463ms total,3.943ms present,2.187ms upload,10.867ms Im2D,5.544ms native draw,
3.455ms preflight and.671ms setup. This is a modest improvement over Boot234;
it does not justify claiming60FPS or changing the normal launch/build path.

The file `build/native-process-sampling/boot-235-title-cpu.json` is MISNAMED:
the profile started after the title entered its attract movie. Phase frames
3636..4200 show movie rendering, not the heavy title. Its5259 samples and1.083ms
maximum pause must not be used as title CPU evidence. The initial sandboxed
attempt could not open the thread and produced no sample file; the authorized
bounded attempt succeeded. Future title sampling should start immediately upon
the fresh prompt in one bounded command, before the idle attract transition.
Session58076 is closed. Start sent during the viewed attract movie returned to
the title; the fresh prompt watcher then queued one Start. Both250ms presses
were logged as delivered. Three held A presses acknowledged storage, selected
the existing native folder and acknowledged autosave. No slot was loaded.

Populated Saved Games was directly viewed at
`native-loading-235-saved-games/native-frame-7442206.png`. Frames13081..13320
average59.769FPS/16.731ms,4.249ms present,3.375ms copy wait,.160ms upload,
1.677ms Im2D,4.203ms original pacing and zero extra idle wait. Both original
save payload and index SHA256 remain unchanged. The game is left at Saved Games.
The always_inline experiment remains in the source and the separate IPO build;
the normal build remains non-IPO. Its248MB footprint and long links are material
costs for a modest title gain. The active60FPS-through-Saved-Games goal remains
incomplete. No further input should be sent at the populated save menu.

Boot235 was deliberately stopped at Saved Games (process13856, harness47114
closed). Boot236 repeated the same optimized executable solely to obtain valid
title CPU evidence. The fresh prompt/profile/eight-second clean interval/Start
sequence ran in one bounded command, session82589, now closed. The profile
`build/native-process-sampling/boot-236-title-cpu.json` contains5160 samples,
2.024ms maximum pause, on the real heavy title during frames1920..2160.
Outermost attribution includes1137 ntdll,477 D3D11,393 Runtime::pointer,330
Nvidia,297 VCRUNTIME,224 original827F4B70,168 matrix823EBD00 and134 native
drawIm2D. Innermost attribution includes811 inlined PPCGuestPointer. The hot
VCRUNTIME offset1DAFB is actual REP MOVSB; the local DLL machine code confirms
that this is copying, not memcmp. Clean title frames2181..2450 average36.520ms,
4.002ms present,2.261ms upload,10.752ms Im2D,5.485ms native draw,3.536ms
preflight and.675ms setup. Start reached the viewed storage warning
`native-loading-236-storage/native-frame-4557251.png`. Process41596 was stopped
there; harness70003 is closed. No game is currently running.

The current change removes up to three full EngineState copies per textured
Im2D draw. Original/Im2D qualifiers now read the retained fields directly using
private policy distinctions. The new draw qualifier checks the existing six
native blend equations and cull0/2/6, while preserving the former shared-screen
snapshot's depth-off, cull-none and copy-blend values. The original actual depth,
cull and blend requests still populate the real native draw. Every original
sampler/raster/alpha/stencil gate remains, including inactive W/mip retention.
Scalar ID recognition now uses a constexpr95-entry table from the same recovered
registration evidence, with bounds/alignment checks before indexing.
The old copy-and-override method is preserved only as a test oracle:7884
differential accepted/rejected state cases compare all retained snapshot fields.
All scalar offsets0..1FF, holes, unaligned and extreme IDs retain rejection.
The standalone fixture passes25,841 checks. Regeneration passed311files/zero
diagnostics. Three focused contracts pass2.33s. Normal full integration is
running in session19426, log `build/menu-direct-state-integration-build.log`.
The separate IPO executable has not been rebuilt for this state change.
Session19426 finished successfully: all132 integration tests passed95.38s.
Hardware Im2D passed1,804,268 checks in `build/menu-direct-state-hardware.log`.
Boot237 runs the updated normal/non-IPO build as verified process40272,
harness43927, with1800-second limit. Its title profile/session97758 completed
before attract playback:5141 samples,2.078ms maximum pause, phase1716..1920.
Outermost samples include1185 PPCGuestPointer,982 ntdll,494 D3D11,394
Runtime::pointer,297 Nvidia,224 VCRUNTIME,114 native drawIm2D,87 original
827F4B70 and77 matrix823EBD00. Copy offset1DAFB has113 samples. The remaining
memory/draw costs dominate; copy removal is not a large frame-rate win.

Use clean title frames1941..2157 (217frames), excluding the profile margin and
the later capture/Start transition:26.247FPS/38.099ms,3.638ms present,2.312ms
upload,11.371ms Im2D,5.636ms native draw,4.123ms preflight,.824ms setup.
The earlier quick250-frame summary1941..2190 included the Start transition;
the217-frame window is the corrected title comparison. The same normal build
before this change averaged38.980ms in Boot234, so this is a small improvement.
It should not be compared directly with the old separate IPO executable.

Fresh prompt captures at presentations1729 and2177 show3,677.42 original Im2D
packets per presentation,3,676.42 with depth enabled and134.42 textured.
Depth-enabled counts do NOT prove color writes are absent; a separate actual
depth-only packet counter would be needed before choosing that batching path.
Reducing repeated submission of compatible packets may warrant investigation,
but no batching is implemented, and no original geometry may be omitted.
A smaller remaining candidate is simplifying the checked scalar memory path:
route whole opaque/import pages through the existing slow validator and test a
constant aperture-offset table, retaining every live owner/cancellation/page
permission check. This is only a candidate, not an implemented optimization.

Normal held Start/A inputs again reached directly viewed populated Saved Games
at `native-loading-237-saved-games/native-frame-6264676.png`. Frames21830..22069
average59.567FPS/16.788ms,5.243ms present,4.369ms copy wait,.170ms upload,
1.725ms Im2D and3.133ms original pacing. Original save payload and index hashes
remain unchanged. The game is left there; no further A/slot-loading input is
authorized for this performance endpoint. The title still falls far short of60,
so the active full goal remains incomplete.

### Native depth packet batching and Boot238

Boot237 was subsequently stopped at Saved Games (verified process40272,
harness43927 closed). The native renderer now accepts an explicit ordered queue
of validated, owned, untextured color-preserving Im2D packets. Compatible packets
share actual color/depth resource identities, all active scalar state and exact
viewport. Original strip winding is expanded into ordered list triangles; the
9,360-vertex limit splits batches without dropping any geometry. Other packets
remain immediate. Every packet still passes the original live ownership,
attachment, scalar, coordinate, viewport, predication, stream-output and UAV
checks before queue acceptance. Caller vertices and resource metadata are copied.

Queued submissions execute through the same immediate D3D11 context, with a
private context-state object that restores every caller binding. Copies, clears,
readbacks, other draws, presentation and explicit waits flush preceding work.
The private state is cleared before restoration so it does not retain old
attachments. This uses native DrawIndexed, with no console command processor,
interpreter, skipped geometry or substitute menu. Relevant platform contracts:
[SwapDeviceContextState](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-swapdevicecontextstate)
and [CreateDeviceContextState](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11device1-createdevicecontextstate).
Unsupported native context-state interfaces fall back to individual submissions.

Logical Im2D counters advance only after real DrawIndexed; a separate
`im2d_native_draw_calls` capture field counts actual native calls. Engine logs
distinguish accepted packets from submitted packets. Neither count measures FPS.
The real GPU fixture compares packed color and every defined depth/stencil bit
against serial submissions across 64 state combinations, mixed list/strip
packets, overlapping depths and alpha values, mutated caller inputs and retained
graphics state. A hardware pipeline-statistics query proves all 8,000 triangles
and 24,000 vertices from 4,000 quads reach the GPU in three bounded batches.
Additional checks cover copy/clear barriers, a rejected following packet,
incompatible depth parameters, target switching and depth-before-color order.

Regeneration passed 311 files/zero diagnostics. Full normal integration passed
132/132 in95.42s (`menu-depth-batch-integration-build.log`). Final transition
fixtures passed1,916,022 checks on both WARP and hardware in
`menu-depth-batch-transition-{warp,hardware}.log`. The production renderer was
unchanged between the full suite and those added transition tests.

Boot238 measured the normal build, process40888/harness24120. The title profile
has5,164 samples with0.549ms maximum suspension, phase1679..1920. Clean verified
title frames1941..2223 (283frames) averaged28.340FPS/35.286ms:5.314ms presentation,
2.090ms upload,8.214ms Im2D,2.715ms draw,3.872ms preflight and0.757ms setup.
Compared with Boot237's38.099ms, actual frame time improved2.813ms (7.4%).
Submission timing can move between draw and presentation barriers; the actual
accepted-frame interval is the performance measure. Fresh matching title
captures at presentations1694/2244 retain3,677.384 original packets per frame
and134.384 textured packets, with578.767 actual native draw calls per frame.

The remaining title profile includes1,301 samples in PPCGuestPointer,401 in
Runtime::pointer,601 in NtWaitForSingleObject and154 in the VCRUNTIME REP MOVSB
copy. Original memory checking remains a substantial CPU cost.

Held Start through the project command channel, followed by the verified
storage/autosave prompts, reached directly viewed populated Saved Games at
`native-loading-238-saved-games/native-frame-5092795.png` (presentation11279).
Frames11300..11539 averaged59.498FPS/16.807ms,5.098ms presentation,1.755ms Im2D,
and3.585ms original pacing. Original save payload and index hashes remain
unchanged. Process40888 was verified and stopped there; harness24120 is closed.
The full60FPS goal remains incomplete.

The next change routes the entire opaque-type and data-import pages through the
existing complete memory validator, simplifies the per-aperture permission mask,
and computes physical aliases with an eight-entry constant offset table.
Single-page/width bounds, current runtime/base identity, window cancellation,
acquire loads of stop/permission state and the original aperture's independent
permissions remain checked for every access. Tests additionally map the four
otherwise unrepresented identity apertures and compare all outcomes with the
complete Runtime::pointer validator. The expanded test passes30,848 differential
cases, plus the existing base/cancellation/retirement checks. Regeneration passed
311files/zero diagnostics; full normal integration passed132/132 in95.12s
(`menu-aperture-table-integration-build.log`).

Boot239 measured this memory change on normal process34092/harness6243. The
verified title profile has5,199 samples,2.537ms maximum suspension, phase1597..1838
(the second row of each phase snapshot was still being written; bounds are
conservatively inclusive). Fresh title captures were presentations1609/2161.
Clean frames1859..2140 average29.059FPS/34.412ms over282frames:5.356ms presentation,
2.282ms upload,8.460ms Im2D,2.821ms draw,3.825ms preflight and0.750ms setup.
This is another0.874ms frame-time improvement versus Boot238. Original packet
rate remains3,677.449/frame,134.449 textured, with578.899 actual native draws.
Outermost samples include915 PPCGuestPointer,452 Runtime::pointer,677
NtWaitForSingleObject,150 native drawIm2DImpl and120 VCRUNTIME REP MOVSB.

Held Start/A reached directly viewed populated Saved Games at
`native-loading-239-saved-games/native-frame-5726189.png`, presentation13251.
Frames13272..13511 average59.440FPS/16.824ms,4.148ms presentation,1.727ms Im2D,
and5.551ms original pacing. Save payload and index hashes remain unchanged.
Process34092 was verified/stopped at this endpoint; harness6243 is closed.

The next allocation change retains one bounded spare native batch and its
vertex capacities after successful GPU submission. Submitted target/depth/texture
owners are released; only CPU storage is recycled. Engine upload snapshots now
alternate two owned byte buffers, committing the replacement only after the
real GPU write succeeds. Decoded vertices reuse a separate per-driver capacity,
returned by a scope guard after the backend has consumed/copied each packet.
Every packet still gets freshly decoded data and freshly initialized scalar
state. The decoder's existing returned-vector API delegates to a reusable-output
overload; source/output overlap is rejected before resize. Added fixtures verify
all40 bytes of every vertex across growing/shrinking packets, caller mutation,
inactive NaN/signed-zero preservation, overlap rejection and recovery after a
late invalid vertex. Regeneration passed311files/zero diagnostics. The decoder
fixture passes25,090 checks; three focused contracts pass2.47s. Hardware Im2D
passes1,916,022 checks. Full normal integration passes132/132 in93.93s, log
`menu-reuse-vertices-integration-build.log`.

The regular Play launcher now supplies `--frame-rate 120` (raised from 60), so
its normal launches use the same native scheduler setting as the performance
runs. Its existing argument-round-trip test was updated and passes
(`menu-60fps-launcher-tests.log`). The standalone game's command-line default
is likewise 120; `--frame-rate 60` remains available and historical run
instructions requesting it explicitly still select one-refresh pacing.

Boot240 runs the current normal build as process35716/harness30110. The real title
profile contains5,205 samples,1.230ms maximum suspension, phase1717..1958.
Verified title captures are presentations1734/2354. Clean frames1979..2333
(355frames) average30.136FPS/33.182ms:5.461ms presentation,1.932ms upload,
7.801ms Im2D,2.628ms draw,3.707ms preflight and0.742ms setup. Original packets
remain3,677.448/frame,134.448 textured, with578.897 actual native draw calls.
Reusing buffers reduced actual frame time another1.230ms versus Boot239. Relative
to Boot237, this turn's measured normal build improved from26.247 to30.136FPS,
with4.917ms lower frame time. CPU profile still includes929 PPCGuestPointer,
413 Runtime::pointer,685 NtWaitForSingleObject and123 VCRUNTIME REP MOVSB samples.
Full60FPS through the title remains unachieved.

Boot240's held Start/A sequence reached directly viewed populated Saved Games
at `native-loading-240-saved-games/native-frame-6457484.png`, presentation13415.
Clean frames13436..13675 (240frames) average59.745FPS/16.738ms:3.372ms presentation,
0.151ms upload,1.694ms Im2D,2.544ms copy wait and6.380ms original pacing.
Save payload/index hashes remain exactly4cb60f94.../939bb381..., with original
114800/224-byte sizes. The141-byte local profile hashes to
94b6db1fbac38a13af36ea64fd7ce4faa0670102426f40b4313ac993451bb077.
No slot-loading input was sent. The current normal game is responding and left
at Saved Games (verified process35716, harness30110; launcher timeout1800seconds).
No builds or test pipelines remain running. The active full60FPS goal is still
incomplete; this turn made measured progress, not a completion or blocker.

### Selected inline memory checks and Boot241

Boot240 was subsequently verified/stopped at Saved Games; harness30110 closed.
The previous goal turn made measured progress. The active objective remains
60FPS through the real menu route, including the heavy title.

The complete common scalar memory path now lives in `runtime/guest_memory.h`.
The existing public PPCGuestPointer delegates to that same body; its exceptional
diagnostic path remains an external noinline PPCGuestPointerSlow. The code keeps
every runtime/base, width, page-boundary, address-range, window/stop, exceptional
page, aperture alias and acquire-loaded permission check. Likely-branch hints
describe the measured successful path; they change no guard or check order.

`runtime/aot_inline_memory.h` supplies distinct typed volatile/byte-swapped
helpers and overrides only the generated scalar access macros after the normal
context header has been parsed. Thus shared inline function definitions remain
identical across translation units. CMake initially force-includes this in the
original chunks70/170/171 containing823EBD00/827F4B70/827F51E0. The configuration
is gated in `config/inline_memory.json`; CMake rejects a chunk that no longer
contains the expected original entry. The two headers and selection file are
included in the AOT input hash gate. No original instruction body was edited.

The new NativeInlineMemoryContract compiles an exact generated matrix body as
a separate reference using external memory calls. Only its test symbol/linkage
changes. The reference needs C++ linkage: /EHsc otherwise assumes its extern-C
call cannot throw and removes the catch around deliberate memory faults. An
explicit object dependency ensures regenerated reference headers rebuild the
test immediately. The initial fixture issue was corrected and the comparison
passes65,619 checks: all scalar widths/alignments,16,384 matrix scenarios across
all rounding/flush modes, unusual float bit patterns, overlapping point/matrix/
output storage, every register/trace/FPSCR byte and actual partial-write failures.
The existing30,848 memory cases also compare the direct inline path with the
complete Runtime::pointer validator. Both builds preserve exact diagnostics.

The initial full suite passed133/133 in97.52s. After branch-layout hints,
regeneration again passed311files/zero diagnostics and the final full suite
passed133/133 in95.53s (`menu-selected-inline-layout-integration-build.log`).
Object disassembly proves successful accesses no longer call PPCGuestPointer;
the exceptional branches retain real calls to PPCGuestPointerSlow. Native game
size is75,156,992 bytes, versus73,827,840 before this selection.

The CPU sampler now optionally reads a caller return address only while RIP is
inside an explicitly reviewed leaf function. The exact live code hash must match
the supplied RVA/size contract before sampling; no target memory/register writes
are added. For this build, PPCGuestPointer is212 bytes atRVA155184 with no calls,
pushes or stack adjustment (only ret/tail jump). Its reviewed disassembly is
`selected-inline-pointer-disassembly.txt`, code hash66ce3e5e...b27663. The sampler
reads eight bytes at CONTEXT.Rsp only for this verified leaf, resumes in finally,
and symbolizes return-address-minus-one. This is not a general stack unwind.

Boot241 runs that build as process33536/harness1006. Its title profile has5,150
samples with1.742ms maximum suspension, phase1559..1800. Clean verified title
frames1821..2159 (339frames) average30.730FPS/32.542ms:5.266ms presentation,
1.954ms upload,7.867ms Im2D,2.651ms draw,3.796ms preflight and0.724ms setup.
Title captures1563/2180 retain3,677.436 original packets/frame,134.436 textured,
and578.872 actual native draw calls. The measured improvement versus Boot240 is
0.641ms; the title still falls well short of60FPS.

The leaf profile obtained674 caller samples with zero read failures. Leading
callers include original memcpy82A3CD80(55), native requireCaller(41),
validateCamera(41), raster requireOwner(38), Im2D preflight(32), savegprlr28(29),
native Im2D draw(28),82409308(27),823FC848(25), raster root validation(24),
savegprlr23(24), target validation(21) andrestgprlr28(19).
This identifies the next expansion: original chunks72/73/239 and the native
engine driver/raster/state bridge implementations. Native files include the
macro overrides LAST, after all shared header definitions, preserving their
one-definition contract. The extended test reference copies original memcpy
and register save/restore bodies unchanged and adds overlap, alignment, all
register bytes and partial-fault comparisons. This expansion is not yet built
or measured; Boot241 still uses the initial three original chunks.

Boot241 reached directly viewed populated Saved Games at
`native-loading-241-saved-games/native-frame-8702165.png`, presentation23785.
Frames23806..24045 average59.988FPS/16.670ms,3.338ms presentation,1.652ms Im2D,
and6.195ms original pacing. Save payload/index hashes remain unchanged. Verified
process33536 was stopped there; harness1006 is closed. The expanded selection
is currently building in session39733, logs `menu-inline-callers-{regenerate,build,tests}.log`.

The expanded selection completed in session39733. The extended memory fixture
passes 71,973 checks. Full integration session53407 passed all133 tests in94.87s
(menu-inline-callers-integration-build.log).

Boot242 measured that build: title frames1896..2256 (361 frames), excluding
sampling1559..1875 and capture2277, average32.699FPS/30.582ms. Mean costs:
5.397ms presentation,1.874ms upload,7.236ms Im2D,2.705ms draw,3.200ms preflight,
0.707ms setup,4.425ms copy wait. The title is still below60FPS.
Start was delivered through the enabled command channel. Populated Saved Games
was directly viewed at native-loading-242-saved-games/native-frame-5279263.png,
presentation9041. Clean frames9062..9301 average59.964FPS. Profile/save/index
hashes remain unchanged. Verified process28676 was stopped at that menu;
harness46573 is closed.

The next implementation batches only adjacent raw Im2D vertex uploads into
owned, bounded256KiB storage, retaining actual COM buffer identity. The existing
Im2D flush submits those bytes before its DrawIndexed; that same barrier covers
clears, copies, readbacks, presentation and idle. General writeBuffer drains the
queue first, while every new packet still checks its resource, thread, device
and range. Im2D renders separately decoded40-byte vertices in its private ring;
no original raw28-byte buffer is consumed before this barrier. Native recording
contexts expose no recording or replay operation. Original memcpy and every
byte still reach actual native GPU storage. Capture metadata now distinguishes
actual native buffer upload calls. This implementation is not yet measured.

Raw upload batching passed all133 integration tests in94.94s, plus explicit
hardware resource, Im2D and presentation tests. The hardware Im2D fixture
passes1,916,025 checks and preserves8,000 triangles/24,000 vertices in3 draws,
with4,000 original-sized raw writes combined into at most5 actual GPU copies.
Separate fixtures compare exact bytes after overlapping/noncontiguous writes,
foreign/thread/range rejections, caller wrapper replacement and multiple ring
wraps. Direct GPU readback bypasses the public drain to prove idle/clear ordering.

Boot243: clean title frames2016..2432 (417 frames), after sampling1679..1995
and before capture2453, average35.076FPS/28.510ms. Upload cost is0.845ms,
presentation3.638ms (copy wait2.654ms), Im2D7.504ms, draw2.830ms,
preflight3.308ms, setup0.674ms. Captures1713..2453 preserve3,677.409 original
packets and578.819 physical draws per frame, while raw uploads are148.409/frame.
The title remains below60FPS. Its8s CPU profile has5,186 samples and1.4145ms
maximum suspension; full Runtime::pointer validation remains a leading cost.

A following implementation specializes only a complete single-page native
Runtime::pointer request. It loads current permissions and preserves the same
alias calculation; exceptional pages/ranges/failures fall through to the
original validator body, now pointerSlow. A private test probe calls that
unchanged body for differential comparison with both fast paths. No native
cancellation/owner checks are removed or cached. This follow-up is regenerated
but not yet built or measured.

Boot243 reached directly viewed populated Saved Games at
native-loading-243-saved-games/native-frame-6393383.png, presentation11892.
Frames11913..12152 average59.857FPS. Save payload/index/profile SHA256 values
remain unchanged. Verified process32300 was stopped there; harness97806 is
closed. The native pointer follow-up is building/testing in session39972,
menu-native-pointer-integration-build.log; regeneration passed311 files with
zero semantic diagnostics.

The native pointer follow-up passed all133 tests in95.40s (session39972 closed,
menu-native-pointer-integration-build.log). Differential tests now compare the
unchanged full validator against native Runtime::pointer and both PPC paths,
including widths0..4097 andUINT_MAX, permission0..3, imports, opaque pages and
all eight apertures. The new native fast function is129 bytes with no stack
changes/calls. A reviewed live-code hash contract allows its caller sampling
in Boot244; it is not a general unwind. Boot244 is the next measurement.

Measurement correction for Boot242/243: the original windows above included
some repeated captures from the blinking Press START detector. Those title FPS
figures are superseded by the uninterrupted windows below. All capture frame
ordinals were enumerated and excluded with a20-frame margin; these windows also
start after the next120-frame CSV flush boundary following the profiler, plus
at least21 frames, accounting for buffered timing output. Future measurements
should request a fresh capture after the profiler to establish that boundary.

Authoritative title measurements (menu-242-244-clean-measurements.json):
- Boot242, expanded memory inlining:1956..2130,175 frames,32.827FPS/30.463ms.
- Boot243, added raw GPU upload batching:2076..2294,219 frames,35.286FPS/28.339ms.
- Boot244, added single-page Runtime::pointer:2436..2673,238 frames,
  38.084FPS/26.258ms. Present3.335ms, upload0.847ms, Im2D6.768ms,
  preflight2.626ms, setup0.572ms, draw2.817ms, copy wait2.505ms.
All windows precede the first capture of the final Start detector. No profiling,
capture or prompt transition frames are included. The full60FPS goal remains
unfinished; input reaches Saved Games without loading a slot.

Boot244 profile:5,198 samples,0.7374ms maximum suspension, zero caller read
failures. The verified Runtime::pointer leaf callers are raster listNode147,
dynamic-buffer validateOwned30, camera validation18, program qualification13,
target validation12, raster extension11 and metadata validation9. The normal
PC summary still has PPCCheckedGuestPointer660, native Runtime::pointer251,
D3D11375, VCRUNTIME354 and ntdll1197. This identifies further CPU work; no further
optimization from that profile has been implemented in this build.

Boot244 reached directly viewed populated Saved Games at
native-loading-244-saved-games/native-frame-8194525.png, presentation14881.
Frames14902..15141 (240 frames) average59.887FPS/16.698ms, with6.417ms original
pacing. Profile, save payload and save-index SHA256 values all match the retained
originals. Input log shows one delivered held Start followed by exactly three
held A commands for the storage notice, existing-folder selection and autosave
notice. No slot was loaded. Title capture counters retain3,677.405 original
packets,578.811 actual native draws and148.405 actual buffer uploads per frame.

At this checkpoint the fully tested build is still running as verified
process15076, harness21393, at Saved Games. No build or test remains running.
The title is38.084FPS; the full60FPS objective is not complete. All changes in
this checkpoint are compiled and tested; there are no pending source edits.

Camera-pair checkpoint: engine_rasters now performs one complete list walk for
both attachment roots. Every node is read/validated live; color-first metadata
and duplicate failure order are retained, and depth metadata/duplicate checks
complete before returning either owner. There is no cross-call validation cache.
The single-root API retains the single-key walk. The private viewport fixture
now tests both duplicate/missing roots, late cycles, unrelated-node read/write
permissions, and the accepted65,536/rejected65,537 node bounds. It passes327
checks. Full integration passed133/133 in95.65s; session63784 is closed.

Boot245 title: actual post-profile capture2384 anchors the clean interval
2405..2755 (351 frames), ending before the first new Start-detector capture.
Average38.522FPS/25.959ms, preflight2.226ms, Im2D6.772ms, draw3.173ms,
presentation3.293ms (copy wait2.415ms), upload0.839ms. The preflight gain is
partly offset by higher native draw cost in this sample; title remains below60.

Read-only scheduling inspection: the i9-14900K exposes8 performance cores
(two logical processors each, efficiency class1) and16 efficiency cores
(class0), all in group0. Original host mapping uses the first allowed logical
processors. Main thread37796 is bound to mask1; the busiest worker5332 is bound
to mask10hex, a different physical performance core. Process priority is Normal.
No scheduling or global power setting was changed; evidence does not identify
same-core contention from the busy original worker as the main bottleneck.
The topology interpretation follows Microsoft's PROCESSOR_RELATIONSHIP docs:
https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-processor_relationship

Boot245 directly viewed Saved Games at native-loading-245-saved-games/
native-frame-10101342.png, presentation22756. Frames22777..23016 average
59.518FPS. Profile/save/index hashes are unchanged. Verified process42572
was stopped at that menu; harness24550 is closed.

The next renderer change reuses color-layer preparation vectors and replaces
the14,400-cell clear per packet with generation-tagged touched cells. Every
packet still computes the original conservative dependencies, same stable
triangle order/bounds, and same million-cell sequential fallback. A full clear
on generation wrap prevents stale epoch-one cells. The preparation lease rejects
reentrancy and resets on failure. No GPU/resource state is retained in these
CPU-only buffers. Differential fixtures retain the old dense-grid algorithm
and cover changing extents, empty/growing/shrinking packets,9,360 triangles,
forced generation wrap, the work cap and recovery after invalid bounds.
Build/tests currently run in session21877, menu-layer-storage-integration-build.log.

That build completed133/133 checks in95.72s. Explicit hardware Im2D passed
2,123,745 checks. Boot246 title frames2358..2720 (363 frames after actual
post-profile capture2337 and before new detector captures) average39.841FPS,
25.100ms; preflight2.146ms, draw3.162ms, presentation3.254ms. Saved Games was
directly viewed at native-loading-246-saved-games/native-frame-12193889.png,
presentation26747. Frames26768..27007 average59.676FPS. The existing slot,
profile, payload and index are unchanged. Start and three A holds were delivered
through the direct channel. Process19164 was stopped at Saved Games for the next
renderer build; harness51981 is closed. The full60FPS objective is unfinished.

The next candidate extends ordered native batching to flat color and immutable
textures. Every packet still validates all live inputs. Compatibility additionally
requires identical actual texture and SRV identities plus sampler bits, and the
texture wrapper is copied into an owned snapshot. Writable textures remain
immediate. Cross-packet blend dependencies use the existing conservative layers.
This candidate is not yet built or validated at this checkpoint.

The color/immutable-texture batching candidate now passes3,783,249 Im2D checks
on both WARP and hardware. The new differential fixtures compare immediate
original packets with queued packets across all six blend words, flat/RGBA8/
BGRX8/BC2/BC3 sources, masks, depth and alpha policies, culling, pixel centers,
overlap, strips/lists and caller storage reuse. Separate fixtures check sampler,
source, viewport and effective-state transitions; rejected texture/sampler
recovery; wrapper reassignment; and writable-texture upload ordering. A real
pipeline query confirms six disjoint triangles reach one actual DrawIndexed
with three actual color copies. Full integration passed133/133 in96.35s
(menu-color-batch-integration-build.log, session2093 closed).

Boot247 was launched on the restricted desktop and reported display=occluded.
The detector rejected that capture and no Start was sent. It supplies no valid
visible FPS evidence. Verified process42888 was stopped; harness9403 closed.
Boot248 uses the interactive desktop with the same tested binary and stores.
Its launch harness is47355; profile/Start command session78986 is pending.

Boot248 completed that run: verified process39636. Title frames2236..2599
(364 frames, actual post-profile capture2215) average39.542FPS/25.289ms,
presentation2.640ms, upload0.835ms, Im2D6.895ms, draw3.290ms. The visible
title did not improve versus Boot246's39.841FPS in this sample, despite
actual raw uploads falling148.493 to42.378/frame and present-copy cost falling
2.459 to1.774ms. Actual native draws only fell578.985 to573.319/frame; original
packets remain3,677.378/frame. The packed glyph triangles still used separate
draw calls when their indices were noncontiguous within a dependency layer.

Saved Games was directly viewed at native-loading-248-saved-games/
native-frame-7599031.png, presentation10405; frames10426..10665 average59.806FPS.
One Start and three A holds were delivered. Profile, save and index hashes match
the originals. Process39636 stopped at that menu; sessions47355/78986 are closed.

The next renderer candidate copies complete list triplets into the separate
decoded GPU stream in the already-established stable layer order, then submits
one contiguous DrawIndexed per layer. Identity permutations retain the existing
bulk upload; strips outside the normalized queue retain the existing path. No
vertex bits, winding, layer order, original raw GPU storage, coverage, shader or
blend equations change. Im2D shaders do not consume vertex/primitive IDs. The
new real GPU fixture compares six independent textured strips with immediate
submission, checks all12 triangles/36 indices and requires two draws/four copies.
Full build/test plus hardware checking run in session63142;
menu-layer-compaction-integration-build.log is the authoritative build log.

Layer compaction completed133/133 tests in95.47s, followed by3,795,541 passing
hardware Im2D checks. Session63142 is closed. No test or build is running, and
all source changes are compiled. Boot249 runs the tested binary on the visible
desktop with unchanged stores; launch harness49609, profile/Start session64340.

Boot249 measured title frames2017..2370 (354 frames after actual post-profile
capture1996, before the first following detector capture2391). Average38.959FPS,
25.668ms, presentation2.555ms (copy1.704ms), upload0.855ms, Im2D7.033ms,
draw3.347ms, preflight2.239ms and setup0.596ms. Native draw calls fell to
236.344/frame, uploads42.420/frame, while all3,677.420 original packets/frame
remain. These changes reduce actual GPU submissions but have NOT demonstrated
a title FPS improvement over Boot246's39.841FPS. The full60FPS goal is unfinished.

The8s title profile has5,185 samples and1.5983ms maximum suspension. Outermost
symbol counts include ntdll1005, D3D11440, VCRUNTIME322, Runtime::pointer222,
original827F4B70206,823EBD00171,827F51E0153, drawIm2DImpl123,
PPCGuestPointer117, originalmemcpy82A3CD80117, layer preparation90,
preflight76 and paired listNodes76. Profile pauses/captures are excluded from
the measured title window. CPU execution and live validation still need work;
no further optimization from this latest profile has been implemented.

Saved Games was directly viewed at native-loading-249-saved-games/
native-frame-6044480.png, presentation9010. Frames9031..9270 average59.854FPS.
The original existing slot is visible. Profile, save payload and save index
SHA256 values match the originals. The log records exactly one Start and three
A holds for the storage notice, existing-folder selection and autosave notice;
no slot was loaded. Process25024 remains running at Saved Games, exact path
K:/SimpsonsNativeCopy/build/native/SimpsonsNative.exe, launch harness49609.
Profile/Start session64340 is closed. All changes are built and tested; no build,
test, capture request or unverified source edit is pending at this checkpoint.

The next goal turn classifies Boot249 as progress: actual GPU submissions were
reduced and verified, but the lack of FPS gain redirected investigation to CPU
work. Read-only sampling still identifies the original vertex/matrix routines
and live checked accesses. The title's detailed mode also starts/finishes many
QPC scopes per packet, so a new optional frame-only mode will measure intervals
without those scopes before making further CPU changes. It retains the same
actual presentation timestamps and emits no unmeasured bucket columns. Tests
bracket two frame timestamps with independent QPC reads, check actual index/
acceptance fields, retain detailed scope timing and preserve exact MXCSR and
GetLastError values. Four malformed CLI combinations reject before launch.
Regeneration verified311 files with zero diagnostics. Verified process25024
was stopped at Saved Games; harness49609 closed. Full integration is currently
running in session22754, menu-frame-only-integration-build.log.

That build passed133/133 checks in96.49s; session22754 is closed. LastTest.log
contains the new independent-clock/CSV/state-preservation checks. Boot250's
frame-only title window2008..2384 (377 frames) averages41.283FPS, compared with
Boot249's38.959FPS under detailed instrumentation. This is a measurement-mode
difference, not demonstrated faster underlying game execution. Actual original
packets remain3,677.373/frame, native draws237.029 and uploads42.373. The same
8s diagnostic profile is excluded from the timing window, as are all captures.
The title remains below60FPS with the lighter mode.

tools/measure_native_affinity.py adds a bounded, reversible host-core comparison:
verified exact process/TID, fresh group-zero performance-core topology, original
single-CPU affinity, two other performance cores, then the original again.
Only that native thread's affinity changes, with checked restoration in finally.
There are no game-register/memory writes, priority changes or global settings.
Raw completed-front capture pairs bracket real CSV windows; no PNG conversion
runs during the comparison. This experiment has not yet run at this checkpoint.

Boot250 directly viewed Saved Games at native-loading-250-saved-games/
native-frame-8342883.png, presentation22058. Frames22079..22318 average59.918FPS.
The profile/save/index hashes match. Process29448 stopped at Saved Games;
launch32069 and profile/Start45607 are closed.

Boot251 ran the reversible comparison on verified process17460/thread38436.
Fresh topology identifies8 performance cores (SMT, efficiency1) plus16 efficiency
cores (efficiency0), all group0. Original mask1 measured41.314FPS; mask100hex
measured42.047FPS; mask4000hex measured44.143FPS; original mask1 again measured
41.539FPS. Every window excludes21 frames at both raw capture boundaries and
uses accepted presentation intervals. The checked finally restored mask1; JSON
boot-251-affinity.json records restoration=true. Start was then sent once.

The next candidate applies that evidence only to original logical CPU0: choose
the last allowed performance core that shares no hardware core with the existing
logical CPU1..5 mapping. Those five mappings and original CPU indices/masks stay
unchanged. Fresh bounded Windows topology and process affinity are queried at
assignment; missing/malformed/multigroup topology or no separate performance
core retains the existing mapping. It changes no native priority or system-wide
setting. The pure selector fixtures cover hybrid/uniform, restricted/sparse/high
bit masks, sibling exclusion and fallback. A real suspended native thread tests
all six original PCR/KTHREAD CPU identities and masks after production assignment.
Regeneration has311 files, zero diagnostics; session78809 closed. The placement
candidate is not yet built at this checkpoint.

Boot251 directly viewed Saved Games at native-loading-251-saved-games/
native-frame-15004670.png, presentation29279. Frames29300..29539 average59.720FPS
on the restored original host mask. Profile/save/index hashes still match.
Process17460 stopped at that menu; launch16218 and comparison/Start3848 closed.
The placement candidate has compiled; its full test run is active in session46457
(menu-thread-placement-integration-build.log). No game is running during tests.

The placement build passed133/133 in95.66s; session46457 closed. LastTest.log
contains the selector/real-thread CPU identity tests. Boot252 uses this compiled
placement automatically, without the comparison script changing any affinity.
An independent read of its sampled main thread29688 in verified process32080
confirmed group0/mask4000hex (boot-252-placement.json). The tested binary is
K:/SimpsonsNativeCopy/build/native/SimpsonsNative.exe, launch harness75800;
profile/Start session94550 has completed and closed.

Boot252 frame-only title frames2048..2446 (399 frames) average43.419FPS, versus
Boot250's41.283FPS with the same lighter mode and old placement. Post-profile
capture2027 and the first following detector capture2467 anchor the exclusion
window. Original packets3,677.420/frame, native draw calls237.516 and original
raw native uploads42.420/frame remain real. This is a modest measured gain;
the full60FPS through Saved Games objective is not complete.

The8s title profile has5,168 samples and2.9932ms maximum suspension, excluded
from measured FPS. Outermost samples: ntdll922, D3D11440, VCRUNTIME377,
Runtime::pointer242, original827F4B70214,823EBD00176,827F51E0163,
originalmemcpy82A3CD80131, drawIm2DImpl122, PPCGuestPointer119,
layer preparation105 and dynamic buffer lookup92. Original vertex/matrix work,
memory checking and native validation remain CPU candidates; no further change
from this profile is implemented yet. No FP/rounding, aliasing or guard shortcut
was taken during this turn. All source changes are built and tested.

Boot252 directly viewed the existing slot at native-loading-252-saved-games/
native-frame-7397408.png, presentation12141. The initial240-frame interval
12162..12401 averaged59.210FPS. To check that short sample, the fixed following
1200-frame interval12402..13601 averaged59.914FPS (median16.66055ms,
maximum28.6984ms, two intervals above25ms), with no new capture/profile/input.
Both results are retained in boot-252-saved-timing.json and
boot-252-saved-steady-timing.json; the longer sample does not erase the short one.
Profile, payload and index SHA256 values match the originals. Exactly one Start
and three A holds were delivered; no slot was loaded. Process32080 remains live
at Saved Games, exact tested executable path, launch harness75800. No build,
test, capture request, comparison or unverified source edit is pending.
The title is43.419FPS with lighter measurement; full60FPS is still unfinished.

Achievement verification clarification: the retained8e7b01082e5033a4ca77e28b386d92b55f06d8f64b13efd24ff09e06ec5f6f82
value is the achievement file's embedded PAYLOAD checksum, not the checksum of
the whole156-byte file. Recomputing the bytes before SHA256: matches that original
payload checksum. The whole-file SHA256 is
aecde104cdd5366746de62fee660ac3ccad6d83868c2397359f36f027d3ff321;
its write time remains2026-09-12 22:50:32UTC, before these runs. No achievement
record was changed or restored during this check. Preserve both checksum roles
to avoid another false mismatch in a later goal turn.

Continuation checkpoint: the isolated AVX2/no-FMA matrix experiment under
build/native-cpu-variants generated its source but has not compiled or run.
The build script now specifies the observed Clang and LLD paths; configuration
still fails because the scratch link invokes rc without the SDK bin on PATH
(menu-cpu-variants-benchmark.log). There is no AVX2 correctness or speed result,
and the default game source/binary has not changed. Boot252 had been stopped.

The user's clarification to send Start was handled with the existing append-only
controller channel. Boot253 is the same tested binary, verified process34252,
launch harness39183. The visible prompt detector matched frame1543940 and sent
START_HOLD exactly once; the native log confirms buttons0010 delivered for250ms.
Three subsequent A holds acknowledged the storage notice, selected the existing
folder, and acknowledged autosave. The captured and directly viewed Saved Games
screen is native-loading-253-saved-games/native-frame-2733714.png with the
existing slot A,0:00.00(0%),The Land of Chocolate. No slot was loaded. All four
retained whole-file profile/save/index/achievement hashes match. The game remains
live there, and no additional controller commands are pending. This boot has no
new controlled FPS comparison; the broader full60FPS objective remains unfinished.

The isolated CPU benchmark now configures with the explicit LLVM compiler/linker,
SDK bin on PATH, and the same optimized strict-FP/exception flags. It passed the
71,973 existing behavior checks plus3 benchmark setup checks. Eight alternating
rounds measured44.1365ns/call SSSE3 versus44.670375ns/call AVX2/no-FMA; the newer
instruction variant is slightly slower and is not adopted in the game.

Boot254 used unchanged game code to locate large native copies. The new optional
--copy-callers diagnostic reads only at the byte-verified VCRUNTIME140 helper's
rep movsb instruction(1DAFB); reviewed two pushes put the return at RSP+16 and
R8 retains the size. It changes no registers/memory, resumes in finally, and
rejects a different live helper. The12s profile has7,757 samples,4.0349ms maximum
pause and zero copy-read failures. Of235 captured copy callers,233 were the
12,220-byte CacheTransaction backup,1 resetBindings backup and1 Im2D vertex copy.
The capture made later after this profile visibly shows the idle demo, so the
47.071FPS interval in boot-254-title-timing.json is explicitly INVALID as title
evidence. tools/measure_native_menu.py now rejects movie-draw changes inside a
title interval. This boot's legitimate Saved Games240-frame interval18567..18806
averaged59.561FPS. Process43484 stopped at the populated Saved Games screen,
native-loading-254-saved-games/native-frame-18606221.png; launch36140 closed.

The next implemented candidate reuses only allocated backup storage for the same
three complete CacheTransaction windows. Every construction repeats the same
fresh permission checks and byte copies; every failed transaction restores the
same full windows. Thread-local leased buffers keep nested snapshots independent
and return storage even if construction fails. Tests cover nested commit/rollback,
all bytes and surrounding page canaries, injected exceptions, all6 page checks,
cancellation during an operation, and reuse across two runtime lifetimes.
Regeneration passed311 files/zero diagnostics. The first full suite passed132/133;
the added test mistakenly expected Runtime::pointer to check cancellation at
construction. The test now triggers the existing checkRunning inside a live
transaction and verifies rollback, preserving original behavior. Its focused
rerun passed. A clean full recheck is now running; no live game or live FPS result
for this candidate yet.

The snapshot candidate passed the clean full133/133 recheck in94.29s. Boot255
verified the actual title capture before measuring: native-loading-255-after-profile/
native-frame-3771948.png,presentation2256. Frames2277..2730(454) measured48.588FPS;
the end boundary is2751, no movie draws occurred, and original packets remain
3,677.434/frame. This is faster than the last valid43.419FPS title measurement;
Boot254's mixed demo interval remains excluded. Native draws235.396/frame and
raw uploads42.434/frame remain real. The8s CPU sample has5,150 observations,
2.8082ms max pause and zero copy-read errors. No CacheTransaction rep-movsb caller
was observed; the sole captured large-copy caller was resetBindings.

Boot255 Saved Games capture native-frame-8012122.png,presentation11732 directly
shows the existing slot. Initial frames11753..11992 average57.782FPS(10 intervals
above25ms); the fixed following1200 frames11993..13192 average59.823FPS(5 above25ms).
Retain both results rather than ignoring initial stutter. Process30696 stopped at
Saved Games after one Start/three A holds; launch10164 and profile/Start10336 closed.
The tested48.588FPS binary,PDB,and DLLs are preserved in build/native-snapshot-baseline
for a future same-session comparison. No slot was loaded.

A second candidate separates EngineState's existing scalar/sampler validation
from publication. Application and pipeline preflight paths validate directly;
apply paths and direct setters use the already-atomic setters without cloning
the whole owner. Complete snapshots remain wherever a later guest store/callback
can fail. Setter validation, equal-value blend broadcasts and all owner/table/
permission checks are unchanged. A new CPU fixture verifies that qualifying all
known defaults and rejecting bad/uninitialized requests changes no scalar,sampler,
or independently packed target blend. Its regeneration passed311 files/zero
diagnostics, and build/menu-state-validation-integration-build.log is now active.

The state-validation candidate passed133/133 in95.58s, including26,259 pure state
checks. Boot256's verified title frames2444..2888(445) measured47.803FPS, median
20.5454ms,6 intervals above25ms. No movie work occurred in the window; original
packets3,677.391/frame remain present. This does not demonstrate an improvement
over the snapshot-only48.588FPS build. The standalone validation API, the five
owner-clone removals, and their dedicated pure-state fixture were reverted.
Complete snapshot-buffer reuse and its tests remain. No rejected optimization
is left enabled. Boot256 reached Saved Games at native-frame-8620366.png; process
11012 stopped there, with launch54147 and profile/Start80563 closed. All four
profile/save/index/achievement whole-file checksums match after boots255/256.
The restored source regenerated311 files/zero diagnostics and is building/testing
in menu-snapshot-retained-integration-build.log for one more live measurement.

Final retained-source check passed133/133 in96.08s; session95238 is closed.
Boot257 verifies the repeat with the default tested executable: process42652,
main thread32308, launch40405. Profile/Start55800 is complete and closed. The
title anchor native-loading-257-after-profile/native-frame-3834343.png visibly
shows Press START at presentation2280. Clean frames2301..2762(462) average
49.0596FPS, median20.08815ms; end capture2783. Movie draws remain unchanged;
original packets3,677.437/frame, native draws235.398/frame and raw uploads42.437/frame
remain real. Together with boot255's48.588FPS, this supports a repeatable title
improvement over the prior43.419FPS, but it does not meet the full60FPS objective.

The8s profile has5,140 samples and4.434ms maximum pause, excluded from FPS.
Outermost counts: ntdll618,D3D11442,VCRUNTIME415,original827F4B70233,
Runtime::pointer231,823EBD00190,82A3CD80144,827F51E0140,PPCGuestPointer139,
drawIm2DImpl121,EngineDriver draw108,upload preflight107,layer preparation102.
There are zero copy-caller read errors. Small native copies and original CPU
geometry/validation remain candidates. No new instruction/FP change is enabled.
One potential next diagnostic is to count empty versus nonempty dirty-state
commits before considering snapshot work for commits with no changed state.
No such counter or fast path is implemented, and all complete snapshots/checks
are currently retained. LLVM profdata and the x64 instrumentation runtime also
exist locally, but no profile-guided compilation has been configured or run.

Boot257 directly viewed Saved Games in native-loading-257-saved-games/
native-frame-8745933.png,presentation14706. First240 frames14727..14966 average
59.1965FPS,4 intervals above25ms. The fixed following1200 frames14967..16166
average60.00825FPS,median16.65945ms,max22.2396ms,zero above25ms. Both samples are
retained; the steady result does not erase initial jitter. All profile/save/index/
achievement checksums match after this boot. Exactly one Start and three A holds
were delivered; no slot was loaded. The game remains live at Saved Games on the
verified default executable, launch40405. No build, test, capture request, input,
or unverified main-source change is pending. The active goal remains full60FPS
through the menu path, not merely60FPS at Saved Games.

Next goal turn: boot257 was stopped at Saved Games; launch40405 closed. A small
capture diagnostic now records commit attempts, empty commits, and scalar/stage
entry counts. It changes no commit behavior and adds no per-commit clock reads.
The diagnostic built and passed OriginalEngineStateBridge plus NativeMillisecondClock.
Boot258 measures a visibly verified title interval2107..2556(450 frames), no movie
draw changes:47.992FPS. Per frame:3,681.460 commit attempts,3,652.460 empty commits,
176 scalar entries and104 stage entries. Thus about99.2% of title commits have
both dirty counts zero. These data motivated the next candidate; no CPU profiler
ran in this diagnostic boot (the anchor folder retains the helper's historical
after-profile naming). Boot258 reached Saved Games at native-frame-11852843.png;
verified process28904 stopped there, launch40228 and title/Start6197 closed.

The empty-commit candidate keeps both original count loads and bounds/owner
checks, validates all three complete writable cache windows via
EngineCacheTransaction::preflight, and executes both original zero stores in
their original order. With zero loop iterations there are no callbacks, cache
entry changes or effective-state changes to roll back, so it omits only the
snapshot copies and unchanged host copy for that case. Nonempty transactions
retain the entire existing snapshot and rollback path. Differential fixtures
compare against the previous full-snapshot empty operation, including poisoned
unreferenced entries, all cache bytes, whole PPC context, all rounding/flush modes,
all6 pages with0/1/2 permissions and cancellation. Regeneration passed311 files,
zero diagnostics. Build/test menu-empty-commit-integration-build.log passed133/133
in93.51s; session62657 is closed. The engine-state fixture reports30,492 checks
and the empty-commit differential cases passed. No live FPS result for this
candidate exists at this checkpoint; boot259 is measuring it now.

Boot259 completed with the retained empty-commit candidate. Its directly viewed
title anchor native-loading-259-after-profile/native-frame-3238269.png is
presentation2103; no CPU profiler ran. Clean frames2124..2599(476), ending before
capture2620, average50.6236FPS, median19.47015ms, maximum26.9497ms,2 above25ms.
No movie draw changes occurred. Original draws3,677.501/frame, native draws
238.352/frame and raw uploads42.501/frame remain present. Empty commits average
3,652.501 of3,681.501 attempts/frame; scalar176 and stage104 entries/frame.
This improves on the matching diagnostic-only boot258's47.992FPS but remains
below the full60FPS objective.

Saved Games was directly viewed at native-loading-259-saved-games/
native-frame-13682867.png,presentation31919. Initial240 frames31940..32179 average
58.2806FPS,10 above25ms. Fixed following1200 frames32180..33379 average59.8178FPS,
median16.65025ms,maximum33.4022ms,3 above25ms. Retain both intervals. One Start and
three A holds were sent; no slot was loaded. Verified default game process42004
has stopped. All four profile/save/index/achievement whole-file SHA256 values
still match after boots258/259.

The existing build/native-thinlto experiment is being refreshed against current
source using build/native-thinlto.ps1. It retains strict FP and original CPU bodies,
uses the existing IPO configuration, eight LTO workers, one link job and a2GB
cache. This is a repeat of an older experiment, not a first attempt; older title
results and the invalid boot235 attract-movie CPU profile remain recorded above.
The current focused build log is menu-empty-commit-thinlto-build.log. It has linked
the game and is still linking/checking focused test targets at this checkpoint.
The default build/native executable remains the fully tested50.624FPS candidate.
No current-source ThinLTO FPS result exists yet and no default IPO setting changed.

The user's delayed clarification requested sending Start to the game. Boot260
launched the fully tested default executable while the isolated ThinLTO CPU link
continued. The fresh title detector matched native-frame-2796864.rgb10a2 and
queued exactly one START_HOLD; the game log confirms buttons0010/250ms delivery.
Three subsequent A holds acknowledged storage, selected the existing folder,
and acknowledged autosave. Directly viewed native-loading-260-saved-games/
native-frame-3795395.png shows the populated Saved Games screen. No slot was
loaded and all four saved-data checksums still match. Process35568 remains live
at Saved Games, default executable, launch session94494; Start session59170 is
closed. This boot is an input/navigation confirmation, not an FPS comparison,
because the compiler experiment was running concurrently. The active full60FPS
goal remains unfinished. ThinLTO log most recently completed step83/86 linking
RuntimeTests.exe; remaining focused tests/build outcome are pending. Do not
restart that build merely because its original session handle is unavailable.

Next continuation classified the previous turn as progress (verified Start/menu
delivery and saved-data checks) plus a verified live compiler wait. The same
ninja1728 remained live; the last link is EngineStateBridgeTests after completed
game, RuntimeTests, InlineMemoryTests and NativeTickTests links. No duplicate
build was started. Boot260 process35568 was stopped at Saved Games and launch
session94494 closed before preparing the isolated live comparison.

The old boot257 profile's largest NT instruction sample is the current installed
ntdll NtWaitForSingleObject syscall return atRVA160404 (470/5140 observations),
while the actual WriteFile syscall has7. Nearest export names are only diagnostic
for other offsets. A new optional --nt-wait-callers profiler flag validates the
entire32-byte stack-preserving NT stub in the live process before reading RSP.
It also validates all343 bytes of the locally disassembled KERNELBASE
WaitForSingleObjectEx body (RVA1C070, SHA256
5b6928fab736b23f8ef6e81239e28eb848236c186e1dae159ff0e49e711e49ad).
Only when the immediate NT return address is exactly wrapper+1C11F does it read
the wrapper caller at NT-RSP+A0 (three pushes,0x80-byte allocation,8-byte call).
No other frame is guessed/unwound; every sample resumes the thread in finally.
Changed DLL code rejects profiling; game instructions/registers are not edited.

Boot260 Saved Games diagnostic boot-260-saved-wait-owners.json collected2292
observations in4s. Both caller-read error counts are zero. Of480 wrapper-owner
samples,434 resolve through the correct default PDB to boundedWait::PollTimer::pause,
43 are NVIDIA driver waits,2 D3D11 and1 NVIDIA capture. This is Saved Games,
not a title profile. The compile was running, and maximum diagnostic pause was
15.9768ms; none of these samples or concurrent frame intervals are FPS evidence.
The next title profile will determine whether GPU query polling also matters
there before altering any wait behavior. No GPU wait change has been made.

The current-source ThinLTO build completed all86 steps and4/4 focused contracts
in1.20s test time (build itself ended10:22 Eastern). Boot261 verified the title
at native-loading-261-after-profile/native-frame-3697525.png,presentation2218.
Clean frames2239..2760(522), bounded by capture2781, average54.6939FPS,
median18.03485ms,max28.5802ms,2 above25ms. No movie draw changes occurred.
Original packets3,677.368/frame,native draws237.139/frame,raw uploads42.368/frame
remain present. Commit attempts3,681.368/empty3,652.368, scalar176/stage104.
This improves on default boot259's50.624FPS but still misses60FPS.

The8s title profile boot-261-title-waits.json used the correct ThinLTO PDB,
5,131 observations,8.4281ms maximum pause,zero NT/direct-wrapper read failures.
432 verified wrapper-owner samples resolve to boundedWait::PollTimer::pause,
48 NVIDIA,4 D3D11,2 capture driver. Sampling occurs before the excluded title
anchor interval. Thus the polling timer is relevant on the actual title too.
Saved Games native-frame-8660839.png,presentation12409 was directly viewed:
initial240 frames12430..12669 average57.5646FPS,11 above25ms; fixed following1200
frames12670..13869 average58.4778FPS,28 above25ms. Keep both lower results.
Exactly one Start and three A holds; no slot loaded, all four saved-data hashes
match. Process22696 stopped there; launch94296 and profile/Start54423 closed.
The54.694FPS executable,PDB,DLLs are preserved in native-thinlto-empty-baseline.
Default build settings remain unchanged; the full133 have not yet all been
rebuilt/tested under ThinLTO, so the experiment is not a fully qualified default.

Next candidate changes only boundedWait polling cadence. For at most250us once
at the beginning of each GPU event wait, it checks the real query with a processor
pause instead of immediately arming the100us timer. Every owner/device check,
GetData result, five-second timeout and timer fallback remain. QPC failure or
regression ends the short spin and falls back; no new completion claim is made.
No fence/Flush1/event implementation was added. Default full integration is
running in menu-gpu-short-poll-integration-build.log, session34417. No candidate
FPS evidence exists yet. The ThinLTO builder gained -Render for game plus the
affected presentation/Im2D software and hardware fixtures. Its cache ceiling is
now16GB because the observed existing cache already held9.34GB across2380 files;
the previous2GB pruning policy would discard most of those reusable objects.

The short-poll candidate passed the full default133/133 in93.08s; session34417
closed. The first ThinLTO renderer link failed while writing its existing root
SimpsonsNative.pdb. An exclusive read/write open proved that another process
held that PDB; directory write/remove succeeded and disk free space was469GB.
No unrelated process was killed. A read-only Restart Manager query could not
start (error29); this was not treated as a build blocker. The builder now writes
new PDBs to native-thinlto/symbols. Relink session11251 completed successfully;
menu-gpu-short-poll-thinlto-relink.log records2/2 graphics contracts in1.28s,
hardware presentation with exact packed codes/bindings, and3,795,541 hardware
Im2D checks. Failed attempt52287 is closed and its failure log retained.

Boot262 used the new short-poll ThinLTO executable. The directly viewed title
anchor native-loading-262-after-profile/native-frame-3940347.png is presentation2355.
No CPU profiler ran. Clean frames2376..2910(535), bounded by capture2931,
average56.1627FPS,median17.498ms,max28.0447ms,2 above25ms. No movie draw changes.
Original draws3,677.410/frame,native draws236.922/frame,raw uploads42.410/frame;
commit attempts3,681.410/empty3,652.410,scalar176/stage104. This improves on
boot261's54.694FPS, but full60 remains unproved and unmet on the title.

Saved Games native-loading-262-saved-games/native-frame-9747565.png,presentation16001
was directly viewed. Initial240 frames16022..16261 average58.0829FPS,8 above25ms;
fixed following1200 frames16262..17461 average59.7284FPS,median16.6716ms,
max30.2114ms,6 above25ms. Keep both intervals. Exactly one Start and three A holds
were delivered; no slot was loaded. All four profile/save/index/achievement
hashes matched after this boot. Verified process41808 was stopped there;
launch36688 and title/Start56579 are closed.

Important symbolizer correction: LLVM prefers a same-named adjacent PDB even
when it does not match the executable's embedded GUID. Merely moving the new
PDB to symbols did NOT make default lookup safe: the old root file yielded
the prior boundedWait line65 for the new binary, whose matching symbols yield
line79. No boot262 CPU profile used those stale symbols, and FPS is unaffected.
Profiler validated_symbol_image now checks the executable GUID/age against
actual PDB metadata. A matching adjacent file is used normally. For the explicit
native-thinlto/symbols arrangement, it copies and SHA256-verifies only the EXE
beside the matching PDB and uses that image for symbolization, while process
identity verification still uses the live executable path. Missing/mismatched
PDBs reject profiling. Normal, preserved baseline and new ThinLTO selections
were all verified; the new image resolves the correct line79. Current EXE SHA256
2b59a079d4e18f440c228438bf3782b8a2586cb27e241891d7d1d6aba560e7cf,
PDB GUID D9223FBB-7150-45C4-4C4C-44205044422E,age1. The earlier boot261 profile
used its then-matching root PDB and remains valid.

Full ThinLTO qualification is now running via native-thinlto.ps1 -All in
menu-gpu-short-poll-thinlto-all.log, unified session6984. It builds all targets
with one LTO link at a time before running all133 tests. No game is live, no
input/capture is pending, and no source edit should race this build. Default
build/native also contains the tested short-poll source, with IPO still off;
the56.163FPS result belongs specifically to the isolated ThinLTO executable.
Next steps are to finish the full qualification, repeat the title comparison
with matching non-profiled timing if needed, and remove the remaining frame
cost. A clean low-capture startup/transition run is also still needed to verify
the full requested path. Do not claim completion from the near60 Saved result.

The full qualification was deliberately restarted with two simultaneous links
after measuring31.77GiB total RAM,12.06GiB available and5.627GB peak for one LLD.
The exact old ninja43472 tree was verified by path/start time before stopping;
session6984 closed with the intentional interruption. The builder now accepts
-LinkJobs1 or2, default1. The replacement is native-thinlto.ps1 -All -LinkJobs2,
ninja24520, session38712, log menu-gpu-short-poll-thinlto-all-parallel.log. It
reuses completed objects/cache. No source or FP behavior changed in that restart.
At the latest observation it has linked22/104 remaining targets and still has
two live LLD processes; all133 ThinLTO tests have not yet run.

An isolated resource-lookup candidate lives under build/lookup-variant. It adds
private checkedSlot to EngineDynamicBuffers, retaining the exact old owner
thread/busy/initialized/runtime/nonzero-ID and live slot guards and messages.
The owning buffer() API still returns a shared_ptr. Synchronous requireBuffer
and ownsBuffer reuse those guards without making a temporary owning copy.
The standalone fixture passed original ABI/byte evidence, four real WARP VBs,
rollback, retry, cancellation, retained backing and stale IDs. Added differential
queries cover valid/invalid IDs, all four buffers/null, wrong threads, inactive
and retired owners, and unchanged strong references. Alternating million-query
rounds measured roughly54..66 thread cycles for discarded owning lookups versus
14..19 for validation-only queries. A compiler was running; these are isolated
CPU cost results, not game FPS evidence. Log menu-lookup-variant-benchmark.log.

The candidate now also adds DeclarationRegistry::matchesRecord through the
existing get(id) guard, and EngineScratchResources::ownsDeclaration through
the existing requireDeclaration/token lookup. Declaration differential tests
cover live/stale/foreign/invalid IDs, null and distinct records, logical/strong
references, deduplicated cache reuse, reset, and1000 sequential lifetimes. All
original six-layout, allocation-failure and image-byte proofs still pass:
menu-lookup-declaration-variant.log, completed session46103.

Boot263 was a separate user-requested Start delivery using the tested normal
build, not a timing run. One START_HOLD and three separate A_HOLD commands were
confirmed by the log. The viewed native-frame-2585298.png shows the existing
slot at Saved Games. Four persisted-data hashes matched. Process41944 was later
verified by path/start time and stopped there before continuing performance work.
See build/boot-263-input-result.md. No slot was loaded.

Canonical runtime/renderer sources remain unchanged while full qualification
links. build/lookup-variant/game-source is an actual source copy with the lookup
changes and EngineDriver callsite substitutions for discarded lookups/immediate
identity comparisons. Real input snapshots, bindings, raw GPU writes and readbacks
keep their existing owning references. This copy independently regenerated303
chunks/242 explicit unsupported imports and passed the311-file gate with zero
diagnostics. All309 generated C++/header outputs exactly match the compiled
original graph (game-original-code-equality.json); no PPC body or hook changed.

An isolated complete runtime/renderer and six affected-test build is running
through build/lookup-variant/build_game.ps1, session59151, log
menu-lookup-game-build.log. It mirrors the qualified normal compiler/linker
arguments from the actual compilation database, recompiles all runtime/renderer
translation units against copied headers, and reuses the original PPC/audio
libraries only after checking their dependency records do not include changed
class definitions. It reruns the copied generation gate and adds scratch-owner
query tests, including a foreign thread and original CPU cleanup. Passing all
candidate tests and measuring a clean live FPS comparison are still pending.

### Optional ThinLTO and IR-based PGO

Clang ThinLTO is opt-in with `-DSIMPSONS_THINLTO=ON`. It keeps the existing strict
floating-point and `/Ob2` settings; it does not enable `/GL`, fast math,
architecture-specific code, or guest-clock changes. Ninja link concurrency is
limited to one, and `lld-link` uses an 8-worker ThinLTO job pool plus a bounded
2GiB cache. The default is `OFF` because the accepted stationary gameplay trial
measured 43.973 FPS with ThinLTO versus 46.985 FPS in the preceding ordinary
build. External CPU load affected the ThinLTO run, so this is a warning rather
than a clean regression result. The title-screen gain below does not establish
a gameplay gain.

The last clean title comparison measured50.6236FPS in the ordinary build
(boot259) and56.1627FPS in the ThinLTO build (boot262), a gain of5.5391FPS.
Those runs used the same title route and renderer workload. The current source
was rebuilt in both the isolated ThinLTO directory and the standard
`build/native` directory before the gameplay-focused default was restored to
`OFF`. The unattended launches reached the main-menu route,
but Windows reported every presentation as occluded while the game window was
behind Codex. Those launches therefore provide no valid current visible-FPS
comparison; the gain above is the prior measured result, not a fresh claim for
the current source.

`SIMPSONS_PGO_MODE` is `OFF` (default), `GENERATE`, or `USE`. Only
`Release`/`RelWithDebInfo` receive PGO flags, applied `PRIVATE` to
`SimpsonsPPC`, `SimpsonsRuntime`, `SimpsonsGraphics`, `SimpsonsAudio`,
`SimpsonsAudioOutput`, and `SimpsonsNative`. Test drivers stay ordinary
native code and link the instrumented/optimized production libraries.
`OFF` leaves ThinLTO controlled by the separate build option. Existing
`/fp:strict` and `/Ob2` flags are preserved. No `/GL`, fast-math,
architecture, semantic, check, or clock changes. References: [Profile Guided Optimization](https://clang.llvm.org/docs/UsersManual.html#profile-guided-optimization)
and [Finding clang runtime libraries](https://clang.llvm.org/docs/UsersManual.html#finding-clang-runtime-libraries).

1. From an initialized MSVC x64 shell, configure and build the training
   build. A fresh Ninja dir needs the explicit Clang compiler, otherwise it
   picks MSVC `cl` from the shell and fails the Clang requirement:

   ```powershell
   cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DSIMPSONS_PGO_MODE=GENERATE
   cmake --build build/native
   ```

   The default raw-profile directory is `build/native/pgo`
   (`-DSIMPSONS_PGO_DIR=<dir>` overrides it). `GENERATE` also adds the
   config-gated `/LIBPATH:<compiler resource dir>/lib/windows` needed
   because CMake invokes `lld-link` directly; the resource directory comes
   from the configured compiler `-print-resource-dir`, not a hardcoded path.
2. Run the game normally through main-menu/resume/gameplay, then
   close it normally. Each instrumented binary writes `default_%m.profraw`
   under the `GENERATE` directory (`LLVM_PROFILE_FILE` can override the path).
   Training runs use atomic profile updates and are intentionally slower;
   they are never FPS evidence.
3. Merge with the installed `llvm-profdata` 22.1.8:

   ```powershell
   llvm-profdata merge -o build/native/pgo/game.profdata build/native/pgo
   ```

4. Configure and build the optimized build with the merged profile, then
   re-run tests/benchmark:

   ```powershell
   cmake -S . -B build/native-pgo -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DSIMPSONS_PGO_MODE=USE -DSIMPSONS_PGO_PROFILE=build/native/pgo/game.profdata
   cmake --build build/native-pgo
   ```

   `USE` requires the file to exist and be nonempty, and adds
   `-Werror=profile-instr-out-of-date` without disabling other warnings or
   mismatch detection. Revert with `-DSIMPSONS_PGO_MODE=OFF`.

Generated `.profraw`/`.profdata` files are local build artifacts. No speed
claim follows from enabling PGO until a qualified rebuild and measurement
are compared.
