# Main-menu and gameplay work

Current status (2026-09-13): Main Menu is visually verified and the recorded
launcher reliably loads the existing save and selects Continue Game. The active
goal is gameplay; FPS optimization is set aside. Replay014 passed shadow-shader
activation and the real six-bone character parameter update. The native constant
upload now has a passing GPU-readback test. Character mesh binding/drawing is
the next unfinished rendering operation. Gameplay has not been verified.
The corrected transpose and constant-upload integration passes133/133 tests.
Replay016 confirms the live mesh-binding boundary. Replay018 directly verifies
the revised automatic Continue Game cue against an actionable menu, correcting
the earlier heading-only cue that could match a profile-loading overlay.

See [automatic-startup.md](automatic-startup.md) for the reusable launch sequence
and [native-shadow-depth-shader.md](native-shadow-depth-shader.md) for current
rendering evidence. The dated notes below preserve earlier milestones.

## Historical notes, 2026-09-12

The active goal has changed to 60 FPS through Saved Games. Current work and
measurements are in [native-menu-performance.md](native-menu-performance.md).
Boot217 has ended: it displayed and read the existing real save, then reached
the unimplemented XamContentGetDeviceState import after loading was selected.
The remaining main-menu and character-shadow work below is historical scope.

The main menu is not yet verified. Boot215 accepted Start, the real profile and
storage selection, controller preferences and volume name. Selecting an empty
New Game slot reached XamContentCreateEx. That native save store and file bridge
are now implemented; four affected targets pass, including original save calls
and asset read-only regressions. The full integration build passes130/130 in
239.93s (`build/native-save-integration-build.log`). Boot216 created and
published the first real game save, then stopped at the unfinished character
shadow activation. Boot217 is running to check the existing-save menu route.
The main menu remains unverified.
Earlier no-save runs reached Saved Games, the opening movie and the unfinished
first3D shadow draw. Those earlier results are historical milestones below.

## Changes and evidence

- Resident audio startup now reads the option at P+4C as a byte, matching the
  original store at 82342800 and load at 82342C7C. Boot178 showed the containing
  word was 015DCA58 with the correct first byte 01. P+28 is retained as opaque
  resident state; only streamed storage initializes it as a reader handle.
- Boot181 admits the real resident sound and reaches its original null-reader
  retirement at 82341884. The source/bank lifecycle and original audio accounting
  remain intact. This build remains muted.
- Im2D reuses its two private integer scratch textures at matching dimensions.
  It retains every existing per-triangle copy, draw, color calculation, alpha
  test and depth operation. Queued target changes and resizes have pixel tests.
- The game window supplies keyboard input to slot0 when no physical controller
  occupies that slot. Enter=Start, Space=A, Escape=B, Backspace=Back, arrows=D-pad.
  Short presses survive a game poll; losing focus releases input. Original
  823210B0 still performs the button mapping and owns connection state.

`build/native-keyboard-build.log` records a successful regenerated build and all
100 CTest tests passing (196.05 seconds). Separate software and hardware runs in
`build/im2d-scratch-warp.log` and `build/im2d-scratch-hardware.log` each passed
620,940 pixel/state checks, including queued scratch reuse and resize.

Directly inspected original renderer readbacks:

- `build/captures/native-loading-180-before-reuse/native-frame-518725.png`:
  Press START before the scratch/keyboard changes.
- `build/captures/native-loading-181-progress/native-frame-412044.png`:
  Press START with the new build. Raw SHA256:
  `a8cf2497e89ff3f07265ba15952337ea03a491858c71c0886f5df15ab66e8a6e`.

Raw RGB10A2 files and metadata sit beside each preview; the repository renderer
maps RGB codes linearly without adding or altering screen content.

## Post-Start failure and current run

Boot181 received a real game-window Enter press (`buttons=0010`, packet2).
The original game then created a new six-channel EXm0 voice and failed at
823424D8, caller823424D0, with `Unqualified EXm0 streamed format/start/seek or
source ownership`. It exited; this was progress past the title input gate,
not a main-menu success. Its last voice was E45AE940, generation12652.

Boot182 adds rejected-stream state/header/block-hash diagnostics to that guard.
`build/poststart-audio/menu-stream-index.json` indexes all37 streams in the
unchanged menu_mus.mus, ready to match the next actual rejected header/hash.
Capture sampling now also preserves every60th presentation through7200, so
later screen transitions are observable after the initial capture allowances.

The diagnostic build compiled. 99/100 tests passed; OriginalDriverLifecycle
timed out waiting5000ms for GPU completion while the game ran concurrently.
After Boot182 exited, the isolated rerun passed in5.77 seconds, recorded in
`build/poststart-audio-driver-isolated.log`. GPU contention remains an inference.

Boot182 reached a directly inspected Press START screen in
`build/captures/native-loading-182-inspect/native-frame-507639.png`.
It then received Enter and exited on the next streamed-audio guard. Its log
identifies menu_mus stream1, id3eb4b454, header0314bb8040015ae3,88803 frames,
first normalized block269 bytes, SHA256
`58c9ec1bf0c5cab428ae13eb74727075b697b97697ca4526b0e0b69930d2f798`.
The desktop tool can select its returned window, but activation and Return both
failed with `GetCursorPos failed: Access is denied. (0x80070005)`. Desktop images
can be black or occluded; use the actual renderer readbacks as visual evidence.
The user requested an alternative way to send Start. The next build adds an
explicit `--controller-input` local file channel, disabled by default. Appended
commands pass through normal native controller queries and original823210B0.
The test covers partial writes, ordered/repeated taps and releases, stale input,
malformed commands, keyboard coexistence and physical-controller priority.

`tools/qualify_menu_xma.py --stream 1` independently verified all18 blocks of
the observed source in three stereo layers and three decoder/read schedules,
without raw EOF. Each layer yields89600 raw frames, initial384 skip,88803
declared frames and413 retained tail. Per-block quota surplus is0..413 frames.
`build/menu-start-xma/report.json` records the qualification. A separate exact
header/sequence/hash certificate admits this source alongside the title track.
No main-menu completion claim is justified by tests or Press START alone.

## Direct input verification

`build/direct-input-start-audio-build.log` records the regenerated build and
all101 tests passing in177.44 seconds, including OriginalNativeControllers and
MenuStartXmaSource. Boot183 uses `build/boot-183.commands`; the explicit sender
queued START and the live log recorded `local command tap buttons=0010;
delivered to normal controller poll` at line233654. This route needs no desktop
input permission. The first inspected frame after that tap shows the EA HD
splash over animated clouds, so it does not establish a main-menu transition.

A second explicit START tap at the title was consumed at line1819964. The
newly qualified streamed cue was admitted at line1820745. Boot183 then stopped
on a mono resident sound: frontend.sbk offset51787 (audio-relative0xAF0B),
header0300bb8000023015,143381 frames,40972 block bytes. Its last completed frame
`build/captures/native-loading-183-stopped/native-frame-456200.png` shows the
title logo during the transition; no main menu is visible yet.

`tools/qualify_resident_xma.py --profile start` verified this second resident
source independently:20 complete original packets, selector3 /48000Hz,143872
raw frames,384 initial skip and107 retained surplus. All three decoder/read
schedules matched, with no raw EOF or restored bytes. The next build uses a
profile lookup for the two exact resident headers, retaining the same named
bank load, original audio copy, allocation generations and request ownership.

`build/direct-input-resident-start-build.log` records the regenerated build and
all101 tests passing in178.39 seconds. Boot184 is the next live attempt, using
`build/boot-184.commands`, `build/boot-184.log` and
`build/captures/native-loading-184`. Revalidate it before starting another run.

Boot184 consumed the explicit Start command and admitted both the streamed cue
and the mono resident source above. It then exited on a second simultaneous
resident cue: bank offset20559, header0300bb800000240b,9227 frames. The original
copied bank allocation was E6D49000 and the requested block was E6D4C517.

To cover the frontend transition's resident sounds together,
`tools/qualify_frontend_resident_xma.py` verified the pinned bank's28 nonlooping,
single-layer mono/stereo XMA sources. Offline discovery is pinned to28 exact
offsets and checked for disjoint spans and unique headers. Each source passed
three decoder/read schedules and provides its full declared quota plus the
original384-frame skip, using complete original packets with no raw EOF or
restoration. Production admits only emitted exact headers, offsets and hashes;
it does not scan guest memory. The certificate is
`audio/frontend_resident_xma_certificates.h`; the report is
`build/frontend-resident-xma/report.json`. The resident test now verifies all28
sources through the bounded source owner against frozen qualification hashes.

`build/direct-input-frontend-bank-build.log` records the regenerated build and
all101 tests passing in187.22 seconds. Boot185 is the subsequent live attempt,
with its own `build/boot-185.commands` channel and renderer capture directory.

Boot185 consumed Start and admitted all earlier resident/streamed cues. Its
last inspected completed frame,
`build/captures/native-loading-185-after-start/native-frame-490700.png`, shows
the purple television transition toward the menu. It exited on the next
streamed cue: menu_mus stream3, id2d2f701d, header0314bb8040027e37,
163383 frames,1035 initial normalized bytes, SHA256
`0a0646990cc1df7d3b69adbfa5cf52403f38e2af9aed1c25a527fbc8baf5672d`.

`tools/qualify_all_menu_xma.py` then verified all37 menu streams /6621 blocks /
33766564 frames. It reused and rechecked the prior title/Start evidence,
independently decoded every other source in three raw variants/read schedules,
checked per-block full quotas with the original384-frame skip, and compared
against the independent stock decoder diagnostic. No raw EOF was sent. Exact
normalized block hashes and packet-restoration extents are emitted in
`audio/all_menu_xma_certificates.h`. Production retains the original reader,
source generations, sequence ownership and no-seek startup guard; admission is
still by exact original header and block hash. `build/all-menu-xma/report.json`
records the full qualification. `MenuXmaSources` now checks every stream through
the native source owner against frozen full-output PCM hashes and rejects
corrupt headers/blocks and invalid sequence bounds.

`build/direct-input-menu-bank-build.log` records the regenerated build and
all101 tests passing in184.19 seconds. MenuXmaSources passed in5.64 seconds.
Boot186 is the next live attempt, with `build/boot-186.commands` and its own
log/capture directory. Revalidate it before another launch.

Boot186 reached the actual profile prompt after a second Start tap (the first
arrived before the title accepted input). Directly inspected
`build/captures/native-loading-186-after-start/native-frame-726347.png`: Kent
Brockman, the unsaved-profile message, Select Profile and Continue Without
Saving. A DOWN command was consumed at line3112994. It then stopped on the
navigation sound at frontend.sbk offset11580, header0300bb8000001592,
5522 frames,739 block bytes. This source contains727 payload bytes rather than
a complete2048-byte packet, so the earlier28-source scan excluded it.

The pinned bank contains two such short nonlooping XMA sources, at11580 and
24675. The resident qualifier now covers all30 mono/stereo XMA sources. The
two short sources explicitly restore1321 and1640 FF bytes in native-owned
packets, under the same adapter policy documented in native-exm0-admission.md;
no guest bytes or PCM are synthesized. Their full quotas passed all three raw
schedules and the independent stock decoder comparison (maximum difference
5.960464477539063e-08 at the measured576-frame diagnostic offset). Original
initial skipping remains384 frames. Exact payload hashes and restoration counts
are in the regenerated certificate; the test verifies unchanged payload bytes,
FF tail extent, complete PCM, original declared frames and retained surplus.

`build/direct-input-short-cues-build.log` records the regenerated build and
all101 tests passing in185.81 seconds (ResidentXmaSource:0.54 seconds).
Boot187 is the next live attempt, using `build/boot-187.commands` and its own
log/captures. No main-menu success has been recorded yet.

Boot187 exited during early presentation with `Native front presentation window
state or client extent changed`; the exact cause was not diagnosed and no
renderer guard was relaxed. Boot188 retries the same tested build with a fresh
command channel and capture directory.

Boot188 consumed START, DOWN, A, A through the direct command channel. Inspected
captures show Press START, the profile prompt, Continue Without Saving selected,
and four empty Saved Games slots. The first slot selection then stopped at
`Unknown or stale native shadow texture ID=E1AB4230 caller=82751020`.
The Saved Games preview is
`build/captures/native-loading-188-menu/native-frame-806771.png`; the stopped
frame remains Saved Games, not the main menu.

Original82751118 creates five auxiliary surfaces per viewport at82751384 and
publishes them at82751388 into the row's ten-slot array. Those allocations had
still executed the original SDK header constructor, while82441708 was already
replaced by a native-only release dispatcher. Original82750FA8's cleanup loop
therefore passed a guest SDK header to an unrelated native shadow registry.
The replacement now pairs these exact allocations with native viewport owners
and routes their releases by registered identity. Original row setup, descriptor
math, publication, partial/full cleanup and reconstruction remain AOT.
See `native-viewport-surfaces.md` for scope and validation.

`build/viewport-surface-build.log` compiled the replacement and passed all101
existing tests. The new fixture initially expected all three viewport rows at
an earlier audio checkpoint that actually has only row0. Its setup was corrected
to construct rows1/2 with the original82751118. The focused rebuild and
`build/viewport-surface-focused-test.log` then passed OriginalViewportSurfaces
in4.58 seconds. No production code changed after the full build. Boot189 is the
subsequent live retry, with a fresh command channel and frame captures.

Boot189 reached Saved Games and consumed the first-slot A command at log
line3086377. All ten auxiliary surfaces in viewport rows1/2 retired through the
original cleanup at lines3086829..3086838; the original then recreated cameras
and began loading game resources, including the Land of Chocolate global bank.
It stopped at `Native ITXD: cache key reused incompatible raster/header metadata`.
The main menu is still not verified.

Read-only inspection of344 original texture records found two duplicate-key
pairs with identical names, pixels and descriptors but metadata+A8 differing
between0 and01001000: hud_target_center (frontend_global vs character-global)
and shine1 (character-global vs loc_global). Original826F25F8 goes directly to
826F2630 on a cache hit, skipping the incoming copy and relocation. Its cached
texture retains the first owner's metadata. Native admission now preserves that
behavior for the two observed word values while keeping all other metadata,
pixel, descriptor, allocation and lifetime checks. No source or cached bytes
are changed. `build/itxd-reuse-report.json` records the offline comparison.

`tools/prepare_itxd_cache_fixture.py` pins both authored hud_target_center
dictionaries and independently verifies their payload equality and sole metadata
word difference. OriginalITXDCacheVariants uses those bytes to exercise the real
original lookup, shared-group refcounts, unchanged native texture, first-owner
metadata, final release and fresh generation. Unknown word variants still fail.
The regenerated build is `build/itxd-cache-variant-build.log`.

That build completed with all103 tests passing in197.19 seconds, including
OriginalITXDCacheVariants (5.40 seconds) and OriginalViewportSurfaces (4.61).
Boot190 uses a fresh `build/boot-190.commands` channel. Before selecting an empty
slot again, check B/Back from Saved Games: Boot189's empty-slot choice started
new-game resource loading rather than directly displaying the main menu.

Boot190 confirmed that B is consumed at Saved Games but leaves that screen
open. Selecting A then passed the previous ITXD cache failure and completed
additional original resource loads. It stopped at audio reader construction:
caller8233062C, identifier50544850, one manager, ring47E00, four requested entries,
actual rootE4627DE0, no allocator override or auxiliary argument. This is the
same original streaming-reader factory as the earlier55280-byte ring.

Original82330608..624 derives the ring size from the stream request's rate and
time, scales by the pinned -1000 float at821DD434, and rounds to16 bytes. The
ownership service previously admitted only55280 for this caller. The next build
also admits the observed47E00 size; original allocation, slice provenance,
manager generation, claims, reset, queued retirement and matching free checks
are unchanged. The lifecycle fixture now runs each of three profiles twice:
six groups,12 managers,54 actual allocations and frees, followed by original
audio-root teardown and worker join. Build: `build/intro-reader-build.log`.

The regenerated build passed all103 tests in192.56 seconds, including the
expanded OriginalAudioReaderLifecycle in4.59 seconds. Boot191 launches with
a fresh command channel and renderer captures. No local save files were found
under userdata; B on Saved Games was already verified ineffective in Boot190.

Boot191 again directly verifies Start delivery (log1878611), the profile dialog
and Saved Games. First-slot A at2664269 passes the new47E00 reader allocation,
loads additional resources, constructs1280x720 movie planes and starts the
original VideoDecodeThread. It exits1 at2665641 on a separate full-size color
texture request, caller823C75C8, format182801B6, usage0. No main menu yet.

Original823C7500 owns a pair of global camera-copy textures at82D09894 and
82D6C7F0. Both construction and paired original823C70C0 cleanup are now routed
to their native owner, and the existing full-camera copy helper admits the
two exact destination/caller roles. Shader sampling remains unqualified.
See `native-scene-copies.md`; regenerated build: `build/scene-copy-build.log`.

The regenerated build passed all104 tests in199.92 seconds, including
OriginalSceneCopies in4.62 seconds. Its actual original parent function returned
success with the texture pair published; both pixel copies, original cleanup,
and two allocation generations passed. Boot192 is the live retry.

Boot192 consumed the first-slot A at3052915, allocated the two scene copies
at3054282..83, and copied the full active camera color into00F00040. It stopped
at826B5FC0, caller823CA5B4, before activating a rendering effect. The added
diagnostic retains that guard and logs the actual registered effect identity.
`build/effect-begin-diagnostic-build.log` passed all104 tests in199.22 seconds.

Boot193 confirms the exact request at3246476..78:

- managerE1A9C3C0, wrapperE1AA8FA0, technique0003FFFC;
- typed objectE1AA90E0, active cameraE4D41AB0, caller823CA5B4;
- registered native effect0050001C, original source8202DF98 (`simpsons_edge`),
  reflected phase1, cacheE1AA8FF0, poolE1A50400;
- original manager words820B71DC/0/AE986323/0/0118147C; wrapper vtable820B7140.

The uninitialized/inactive manager+8 value is not a live SDK object. The exact
source has one `edge` technique, handle0003FFFC, pass0003FFFE. Its shaders are
VS8202E6F0 and PS8202E840. `build/edge_shader_probe.py` is read-only exploratory
field inventory, with `build/edge-shader-probe.json` and `.log`; it is not a
completed shader semantic qualification. The pixel code has a real loop and
predication, beyond the earlier straight-line screen/movie/fourtap profiles.
No edge effect activation, parameter commit or draw has been admitted yet.

The exact edge VS/PS now have offline HLSL transcriptions and a reproducible
static inspector (`tools/analyze_edge_shaders.py`, 102 identity/control-flow
mutation checks). The standalone WARP and hardware fixtures each passed73
draws with exact color/depth expectations. See `native-edge-shaders.md`.

The original edge parameter setter now retains its camera queries, float
conversions and final writes into native-owned private parameter storage.
An initial fixture caught misuse of the last-entered-function trace as a call
stack; the continuation now verifies the actual original frame/backchain and
preserved mask-table register. The corrected original setter fixture passed.

The edge begin/end boundaries now own real compiled shader bindings and run
the original CPU cache save/apply/restore functions. Other effect activation
remains rejected. The native compiler now supports nine exact shader records,
including this pair, and still rejects227 others. Parameter commit and draw
remain guarded. See `native-edge-parameters.md`.

`build/edge-activation-build.log` passed107 of108 tests in200.51 seconds. The
remaining older resource test still expected these new shader records to be
unsupported; its exact capability expectation was updated. The focused rebuilt
resource test and edge begin/parameter/end test both passed in0.83 seconds in
`build/edge-commit-diagnostic-tests.log`. Actual shader stage/device/binding
checks passed on both WARP and hardware in the full run. The regenerated AOT
still verifies311 files with zero semantic diagnostics. The commit guard now
prints actual private values and effective state for the next integration.

Boot194 is running with a fresh append-only command file and frame captures.
No main-menu completion has been claimed.

Boot194 exited1 at the next boundary,826B3980/caller823C9544. The new native
edge activation completed at log3142197 and the complete original setter reached
the commit guard at3142201. Start was consumed at2320342; DOWN/A/A at2751991,
2880579 and3140822. Directly viewed captures show PressSTART(frame518669),
profile prompt(frame648007), and SavedGames(frame744874). No later game/menu
frame was verified.

The actual edge values are LineWidth0.5 (`3F000000`), width1280, height720,
first scene copy00F00040, depth copy00F00003, paletteE1AEB2D4, DepthFade=true,
DepthFadeControl140 and DebugEdges=true. The kernel's first four XY values are
(0,1),(1,0),(0,-1),(-1,0), with Z=0,W=1. Effective state has depth/write,
stencil, cull, blend, alpha test, scissor and expanded blending disabled;
viewport and half-pixel mode enabled; guardbands1; RGBA maskF. Sampler0 is
wrap U/V/W, point min/mag, linear mip, no bias, min0/max13, anisotropy1 and a
single texture level. Commit diagnostics occupy3142202..3142316. The next
work is real constant/resource commit and original rectangle submission.

The edge commit/draw integration now owns immutable native constants, sampler
and a lease on the first original scene copy. The source must have received a
real copy from the active camera. Original private and shared dirty cache lines
clear only after actual PS bindings verify. Three pinned cuts leave all twelve
original rectangle vertex stores and the CPU UV lookup intact. The native
draw validates exact geometry, declaration, shader/commit bindings, output and
state, uploads an immutable rectangle snapshot, and preserves disabled depth.
Draw counts are included separately in capture metadata. No later edge-AA
effect or successful presented frame is implied.

`build/edge-draw-build.log` built successfully and passed109/110 tests in202.83s.
The one failure exposed that the original camera selection retains a logical
reverse viewport without creating a native viewport. The qualified edge bridge
now establishes native0->1 at draw with depth/write/stencil off, preserving the
original logical1->0 endpoints. Regeneration still verifies311 files with zero
semantic diagnostics. `build/edge-draw-viewport-tests.log` passes both affected
original edge and scene-copy tests. The original edge fixture reports2,764,859
ABI/parameter/ownership/pixel checks across three complete original draws.
Both `NativeEdgeBackendWARP` and `NativeEdgeBackendHardware` pass7,200 checks,
including six line widths, independently predicted stripe pixels, unchanged
depth/source, rejected aliases/devices/stale commits/geometry/viewport and absent
bindings. Standalone edge math passes103 draws per device. All110 tests have
passed across the full run and the focused correction; there is no claim of
one uninterrupted110/110 run after that correction.

Boot195 exited1. A first Start during the title animation was consumed at log
1407959 before its actionable prompt. The next Start at2055296 opened the
profile prompt; DOWN/A/A at2463909/2574452/2867698 followed the normal menu flow.
Directly viewed captures show title/PressSTART(frame441450), profile prompt
(frame570560), and SavedGames(frame678314). Their isolated inspection folders
are under `build/captures/native-loading-195-*`.

The real edge begin/commit/draw succeeded at2869070/2869074/2869076, using
source00F00040, cameraE4D41AB0, width1280, height720, line0.5. The next original
copy submitted edge output to00F00041. At2869078 the following **simpsons_aa**
source8202FA78, native0050001D, typedE1AA9780, caller823CA5B4 was rejected.
This is row3 AA, not row4 `simpsons_edgeAA`; the latter remains separately
unported. `docs/native-aa-shaders.md` records the subsequent qualification,
CPU setter cuts and native integration. No movie frame or main menu beyond
SavedGames has been visually verified.

`build/aa-integration-build.log` completes regeneration, build and **113/113
tests passing** in211.45s. The combined original edge/AA fixture passes4,608,066
parameter/ABI/ownership/pixel checks across three edge draws and two full AA
begin/setter/commit/draw/end sequences. Both native backend fixtures pass11,306
checks, including exact packed five-tap mean output, alpha1 and unchanged depth.
The standalone AA shaders pass111 draws and more than682,000 checks per device.
Boot196 starts with a fresh append-only input file and the normal capture path.

Boot196 exited1 after successful live edge **and AA** draws. Start consumed at
2128855 opened the profile prompt; DOWN/A/A consumed at2521276/2686984/2989436.
Viewed captures are PressSTART(frame467184), profile(frame588356), and
SavedGames(frame699641), in their separate `native-loading-196-*` folders.
Edge begin/commit/draw succeeded at2990814/2990818/2990820; AA succeeded at
2990822/2990826/2990828. AA used second-scene source00F00041, width1280,
height720 and original KernelWidth1.10000002; that value is an unused shader
input, as proven by the original binding/dataflow inspection. The original
following copy transferred the AA result to shared color-copy00F00006.

At2990830 the entry826B5FC0 rejected the next activation caller/technique.
Original parent823C7500 next invokes823CA188 with edgeAA typed lookup and the
first/second scene IDs. That function calls823CA408 (the observed shared
color copy), then virtual+24 at823CA1B8 with return823CA1BC. It would next call
setter823C9A60, rectangle823CA448 and end823CA440. This is the still-unported
row4 simpsons_edgeAA source82034008, distinct from the completed two passes.
No post-SavedGames complete movie/game frame or main menu was verified.

The separate edgeAA shader now has exact-record static evidence and a fixed
native transcription, with all five texture roles and both ten-sample loops.
The first hardware test exposed approximate native UNORM10 input conversion
crossing a palette-index floor. Canonical packed-code recovery fixes this at
the format boundary. The software and hardware fixtures each pass68 effect
draws and4 input-conversion probes, more than381,000 checks, and all four outline
branches. All1024 RGB codes and four alpha encodings are tested. Static evidence
passes188 mutations. See `docs/native-edgeaa-shaders.md` and
`build/edgeaa-input-proof-tests.log`. Runtime activation remains guarded pending
actual resource/parameter/draw integration; Boot197 has not started.

### EdgeAA integration before Boot197

The exact edgeAA shader pair is now integrated with five real native texture
roles and the original parameter setter/rectangle/state cache. Original palette
loading and decode are qualified separately through the actual ITXD loader,
copy, relocation, cache, release and reload paths. Software/hardware backend
tests and all seven palette/regression tests passed. The original three-effect
chain fixture then exposed and corrected reversed blur/edge-strength handle
labels: BlurWidth is handle003C001A from82CD1434; EdgeColorScale is00380018
from82CD1438. Original loads/conversions/stores remain in the AOT code.

`build/edgeaa-handles-tests.log` passes OriginalEdgeAAParameters in6.74s,
checking two complete edge/AA/edgeAA cycles, all188 parameter words, original
state restoration, unchanged depth, expected palette pixels and actual cleanup.
This controlled fixture feeds the original level palette through a captured
loader envelope and selects the original enabled branch; it is not a live game
or menu acceptance claim. Full build119-test run is in progress in
`build/edgeaa-integration-build.log`. The complete build passed119/119 tests in
230.98s. Boot197 now launches this verified binary with the normal append-only
controller channel and native frame captures.

### Boot197: edgeAA live success, next depth-copy boundary

Boot197 used normal START, DOWN, A, A commands. Delivery lines are2158181,
2769223,2812928,3027263. Viewed captures: PressSTART at452477, profile warning
at630545, SavedGames at706615. EdgeAA begin/commit/draw completed at
3028656/3028661/3028663: id0050001E, typedE1AA9FB0, cameraE4D41AB0,
color00F00006, depth00F00003, original paletteE1AEB2D4, first/second scene
00F00040/41,1280x720,NSamples10,BlurWidth1,DepthFade140,threshold0.1.

The next failure at3028664 is SDK resolve82455570 from82751754. Caller82751700
passes source selector0x14 (depth, single sample), null rectangle/point/level,
destination from82DFEA20+148. This is the viewport row's original texture8
header copied by82751510, not the separate shared depth copy00F00003.
Original82751118 builds those genuine headers and publishes them in the
100-byte viewport row; a native ownership/copy bridge remains to be added.
The global SDK resolve stays guarded. The latest viewed complete capture
`build/captures/native-loading-197-transition/native-frame-751876.png` is the
black loading screen with the cat/loading icon. Boot197 exited1. No opening
game frame or main menu has been verified. No game remains running.

The full-size viewport texture8 now has native depth/stencil ownership observed
at original constructor completion and row retirement. Exact original82751700
copy arguments and publications map to the existing real depth-copy backend;
the unrelated SDK resolve stays guarded. Two original caller cycles preserve
ABI, exact source/destination depth/stencil, color, and native ownership. Tests
also cover full/preserve cleanup, recreation and invalid caller/clear/header
requests. `docs/native-viewport-depth-texture.md` records the original evidence
and the initial containing-pool correction. Full build
`build/viewport-depth-integration-build.log` passed119/119 tests in227.59s.
Boot198 is running this verified build; no menu acceptance yet.

### Boot198: opening movie completed, shadows camera selection next

Normal START/DOWN/A/A reached the opening Land of Chocolate movie. Additional
START/A were delivered during the movie without showing a pause/menu. The movie
finished; present4006 was accepted by the display. The following clear failed
at cameraE2CA7550, selector6, caller8270725C: original shadows pass82707220.
The two original 1024x1024 shadow camera surface pairs already have native
owners. Selection only admitted loading/viewport cameras. Boot198 exited1;
no game remains running and no main menu has been verified. The last viewed
capture native-loading-198-after-movie/native-frame-715595.png is still a movie
close-up despite that inspection folder's name.

The native shadows owner now qualifies its two original camera generations,
selection/clear/begin/end and original one-pixel scissor call. Application
scissor and finite bias requests are retained as raw state; existing drawing
profiles still reject enabled scissor/nonzero bias. OriginalShadowCameraPass
executes both real parent branches through the next shadows FX activation,
source820C0550,826B5FC0,LR8270614C, with no casters in the fixture. Focused
tests pass3/3; the border also passes actual hardware set/query checks.
See native-shadow-camera-pass.md. Full build120-test suite is running in
build/shadow-camera-integration-build.log; Boot199 is not yet launched.
The complete build passed120/120 tests in230.09s. Boot199 has now launched
that verified binary with a fresh append-only controller file and native
frame capture directory. No menu acceptance yet.

### Boot199: live shadows clear/border success, matrix setter exposed

Normal START/DOWN/A/A delivery lines2202261,2654437,2799291,3061632.
Viewed PressSTART481870, profile prompt624697, SavedGames733038, and opening
movie762956. The movie completed and original shadows selector1 cleared
cameraE2CA7550,1024x1024,selector6, then set the native border at3173479.
The next live path has real character casters, unlike the empty test fixture.
It reads a native FX ID as an SDK object in82704600: first unsafe load82704614,
address00500111 (=00500005+10C), reported latest helper82A3C3C4/LR82704608.
Caller827055E0 builds g_World from the original frame and calls the matrix setter
at8270568C,LR82705690. Boot199 exited1. No game is running and no main menu or
first3D frame has been verified. A bounded native matrix setter is now being
implemented and tested; its original outer frame assembly is retained.
The bounded setter now preserves the complete g_World matrix and dirty bit
in native parameter storage. Both original caller cycles and original effect
finalizer regression pass,2/2 in1.36s. The complete build is running in
build/shadow-world-integration-build.log; Boot200 has not yet launched.
The complete build passed120/120 tests in230.75s. Boot200 is now running
the verified matrix fix, with a fresh local controller file and native captures.

### Boot200: live world matrix success; Boot201/202: audio device lost

Boot200 delivered normal START/DOWN/A/A at lines2114181,2613853,2751406,
3060263 and completed the opening movie. Final edgeAA draws2359..2361 preceded
the first shadow camera border at3170323. The original world matrix setter
passed live at3170325. The next line rejected an unqualified effect activation
technique before shader binding. No first3D frame or main menu was verified.
The activation guard has since gained register-only diagnostics for the actual
entry, return address, manager, wrapper, technique and typed owner. Its accepted
requests and failure behavior are unchanged; no new shadow shader is enabled.

Boot201 again reached the visible PressSTART and SavedGames screens using
normal START/DOWN/A. Viewed captures are
build/captures/native-loading-201-ready/native-frame-868060.png and
build/captures/native-loading-201-saved/native-frame-1152561.png. A one-shot
local watcher was prepared to append START at the final observed movie draw,
but the audio backend failed first, stage2/error88880001, while still on
SavedGames. The watcher exited without sending its timed Start. Boot201 exited1.
Thus precisely timed end-of-movie Start remains untested.

Boot202 immediately exited1 at CreateMasteringVoice,HRESULT80070490. Windows
AudioSrv and AudioEndpointBuilder are running, but an unsandboxed read-only
Get-PnpDevice query found no present AudioEndpoint devices. The user was asked
to reconnect or enable an audio output; no reply has yet arrived. No game or
input watcher remains running. No fake audio endpoint or silent-consumption
fallback was introduced.

The diagnostic-only code regenerated and built successfully:
build/shadow-activation-diagnostic-regenerate.log and
build/shadow-activation-diagnostic-build.log. Its OriginalShadowCameraPass
regression cannot reach the test body because the same actual mastering-voice
creation fails during startup; see build/shadow-activation-diagnostic-tests.log.
The most recent complete suite remains the preceding120/120 matrix build.
Restore a real audio output before the next live run, use a fresh command file,
and repeat normal START/DOWN/A/A. Main-menu acceptance remains outstanding.

### Boot203: timed Start delivered; character shadow technique identified

Audio endpoints reappeared on the next authoritative check. The targeted
OriginalShadowCameraPass then passed in0.79s; see
build/shadow-activation-diagnostic-restored-tests.log. Boot203 launched the
verified diagnostic build. An initial Start at1275853 arrived before the title
was ready; the later Start at2173203 opened the profile prompt. Normal DOWN/A/A
were delivered at2648871,2872808,3065902. Viewed PressSTART478263,
profile631320 and SavedGames743555 are retained in the203 capture folders.

The old fixed movie count was unsuitable: runs198/199/200 ended at edgeAA
counts2368/2400/2361. The one-shot watcher instead observed the newly created
VideoDecodeThread45564, its genuine successful exit, and the next edgeAA draw.
It appended exactly one START after draw2432. The actual controller poll received
that Start at3179236, immediately before the shadow camera clear. The world
matrix setter passed at3179244; at3179245 the game rejected RenderShadowDepth,
technique0007FFFC,caller8270715C. Boot203 exited1 without a visible pause or menu.
No game or input watcher remains running. This timed-input route was tested and
does not avoid the first shadow-rendering requirement.

The exact missing vertex program820C2FA0 now has a static native translation and
independent actual GPU tests, passing WARP/hardware/static evidence3/3. See
native-shadow-depth-shader.md. It is admitted as a native material artifact;
original effect activation, mesh/constant binding and drawing still need work.
The complete build compiled and passed122/123 tests. The remaining resource
bridge test had an outdated unsupported-shader allowlist; after correction,
its focused rebuild and test passed. See native-shadow-depth-shader.md.

### Boot204: real local-profile route reaches missing identity service

The supported profile-management CLI created a durable native Player profile,
575cf79a-3815-45f7-a6f7-e8d709d16298, in build/mainmenu-profile-204. The separate
build/mainmenu-content-204 directory started empty. Boot204 explicitly activated
that profile in slot0. The viewed title capture is
build/captures/native-loading-204-ready/native-frame-467243.png.

One normal Start reached original82431F08, which requested XamUserGetXUID with
slot0,mask7 and writable output0203F860. The missing import stopped the run at
82431F20; the caller was827B27C8. Boot204 exited1. No save was created and no
main menu was verified. This tests the game's own profile/save route; whether
that route can save before the first3D draw remains unknown.

The native local-profile owner now exposes a persistent64-bit equality key
derived from its real GUID, with collision checks. The reached mask7 import
returns that key for active slots, preserving original selection handling.
See native-local-identity.md. Focused ownership, original query and CLI tests
passed3/3; the complete build subsequently passed123/123 in233.33s, recorded
in build/local-identity-integration-build.log. Boot205 uses the same real profile
and content root with a fresh command file; live acceptance remains pending.

### Boot205: identity accepted; display-name service reached

The viewed PressSTART capture is
build/captures/native-loading-205-start/native-frame-430478.png. One normal
Start was delivered after present766/draw526080. Original827B2788 accepted
the native identity and entered827B25E8. Its second identity query succeeded;
the next call,82431878 at LR827B263C, stopped at XamUserGetName with
slot0,outputE1A5BC54,length16. Boot205 exited1 without a save or main menu.

The name import now reads the actual active profile's printable ASCII display
name, including its terminator, within the original16-byte output. The targeted
ownership/query/CLI tests pass3/3 in2.48s after correcting the new fixture's
expected stack to include EngineCpuCalls'0x100-byte bridge frame. The original
827B25E8 refresh itself is executed and its exact record changes are checked.
See build/local-name-tests-verified.log and native-local-identity.md. Boot206
uses the same durable profile/content root; live name acceptance is pending.

### Boot206: name accepted; profile-flow event service reached

Normal Start from the viewed title463517 advanced to the purple TV transition,
capture586476. Both identity and name queries passed. The run stopped after
present788/draw587443 at NtCreateEvent in82433708,LR8243375C,called by827B4E78.
The request was for a real unnamed unsignaled auto-reset event. Boot206 exited1.
No save or main menu was created or verified.

The native handle event family now supplies create/set/clear through actual
Windows NT services. NativeEventOwnership passes real state transitions,
wait/close ownership, cancellation and the original owner constructor/destructor.
Mutant and player-query regressions also pass,3/3 in0.34s. See
native-event-service.md and build/native-event-tests.log. The complete124-test
integration run passed in242.35s: build/native-event-integration-build.log.
Boot207 uses the same real profile/content root with a fresh command file.

Boot207 stopped during startup after present522/draw1005 with
"Native front presentation window state or client extent changed". Its command
file is empty: no Start was sent, and it supplies no live event-service evidence.
The existing guard checks window ownership, fullscreen state and exact client
extent; this log does not isolate which condition changed. The verified build
is being retried as Boot208 with fresh capture/command files. No renderer guard
was relaxed and no new input was delivered to the stopped run.

### Boot208: live event creation succeeds; achievement write request reached

The same124/124-verified binary passed startup on retry. PressSTART was viewed
at build/captures/native-loading-208-ready/native-frame-518773.png. A single
normal START was delivered at log line2409354. After present823/draw642713,
the original owner created native event1F8,type1,initial0. The next failure was
XMsgStartIORequest,appFB,message000B0008,length8,overlappedE2950D9C and
request0203F800,from824316A8 at824316E4; its caller was827B4FAC. Boot208 exited1.

The original wrapper encodes the request as two32-bit words: achievement count
and record pointer. Each achievement record is two32-bit fields: local slot
and achievement ID. The local primary reference identifies appFB/messageB0008
as XGIUserWriteAchievements (xam/apps/xgi_app.cpp,lines170 onward). Its default
account behavior is not adopted. Actual request payload values have not been
dumped, persisted or acknowledged, and no achievement store has been created.
The next required work is a real local achievement persistence owner and the
qualified asynchronous completion bridge for this original request.

The last sampled complete front is native-frame-537159,presentation780,still
the title screen. The current capture policy samples later frames every60
presents; the subsequent transition failed before the next periodic sample.
Do not describe that title capture as a main-menu or final transition frame.
The retained profile remains its original141-byte record and the content root
is still empty. No game is running. No main menu or save has been verified.

### Native achievement persistence and completion implementation

The appFB/messageB0008 request now persists exact original achievement IDs for
active native GUID profiles into immutable checksummed files in a separate
profile-store.achievements sibling directory. It completes the original
overlapped record and signals the actual event only after durable writes or
verified duplicate records. Original wrapper/poll/owner cleanup are retained.
See native-achievements.md for ownership, ABI evidence and limits.

The targeted profile ownership/query/CLI and new original achievement request
tests pass4/4 in2.80s after correcting two fixtures. The full125-test integration
build passed in238.28s: build/native-achievement-integration-build.log. Boot209
launched with a fresh empty command file; its result is recorded below.

### Boot209: durable achievement and storage prompt; saved-game scan reached

Normal START was delivered after present759/draw544407. The native achievement
service persisted original ID00000020 for active slot0, using event1F8 and
actual calling-thread handle1FC. The real156-byte record is
build/mainmenu-profile-204.achievements/575cf79a-3815-45f7-a6f7-e8d709d16298-45410809-00000020.achievement.
Its checksummed content was read back. The original profile remains unchanged.

The viewed capture build/captures/native-loading-209-after-start/native-frame-616853.png
shows the original "No storage device selected" prompt with Done/A. Normal A
was delivered after present1139/draw678905. The background save scan then failed
at82432560,LR8285C470,with unsupported user/device/type/flags. Boot209 exited1.
There is no save or main-menu acceptance.

Original8285C418 requests selected user, device from owner+360, saved-game type1,
flags0,page1. Original8285CC78 initializes that device field to0; device-selection
completion can later replace it. This is a distinct profile-owned scan from
startup's common marketplace enumeration. Native save catalog support and its
original consumers are being qualified; see native-save-catalog.md.

### Boot210: real saved-game scan accepted; storage selector reached

The125/125-verified build displayed PressSTART in the viewed capture
build/captures/native-loading-210-prompt2/native-frame-518689.png. A single
normal START was delivered at log line2320485. Achievement ID32 was verified
as an existing durable record (newly_persisted=0), completing event1F8 with
actual caller-thread1FC. The viewed storage-warning capture is
build/captures/native-loading-210-after-start/native-frame-661506.png.

Normal A was delivered at log line2793420. The actual active Player catalog
under mainmenu-content-204/saves/<full GUID>/45410809 contained zero saves.
Original8285C418 accepted device0/type1/flags0, created real enumerator200,
read synchronous status18/count0 and closed it. No save data was manufactured.

The next original request is XamShowDeviceSelectorUI through82432E40 at
LR8285CF64. The live arguments are slot0,type1,flags300,total_requested0,
deviceOutE1ADC614,overlappedE1ADC634. Original8285CED8 zeroes the28-byte
completion record and selected-device output before this call. Its caller
expects997; original8285C8B8 later polls completion and updates owner+360
from owner+31C only on success. Boot210 exited1 at this unimplemented import.
No storage selection, saved game, completed movie/world or main menu is claimed.

The reference xam_ui.cpp headless selector writes a dummy HDD ID. That behavior
is not used. Next work requires a real native storage choice, actual directory
write/capacity qualification and original completion semantics. The native
catalog implementation and eligibility work are documented in native-save-catalog.md.

The native storage eligibility helper is built and tested: it pins the actual
configured folder, reads native available/total/free capacity and performs a
write+flush through a random exclusive temporary file that deletes on close.
It creates no profile or save and never selects a device. Focused catalog,
storage and original-consumer regression tests pass in4.97s. The last full suite
is125/125 before this backend-only addition; no second live launch was made
because XamShowDeviceSelectorUI still has no implementation or live caller for
the helper. No game is running. Main-menu work remains active.

### Boot211: live storage selection accepted; volume metadata reached

The126/126-verified build displayed PressSTART in the viewed capture
build/captures/native-loading-211-start/native-frame-522381.png. START was
delivered at log line2423789; the existing achievement ID32 was verified and
completed with no new record. The viewed storage warning is
build/captures/native-loading-211-after-start/native-frame-665877.png.
Normal A dismissed it and the real empty save catalog again completed.

At log line2883949 the actual native selector became visible for Player and
K:/SimpsonsNativeCopy/build/mainmenu-content-204. Real available capacity was
527509946368 bytes, requested bytes0, acceptance enabled. A second A went through
the exclusive native UI input source. At2971940 the selector completed with
selected1/device1/result0, actual caller-thread1F4, and the paired UI-close
notification. Original code then called82432580 atLR8285CE58 with device1 and
80-byte output02303B60. XamContentGetDeviceData was still unimplemented.

Boot211 reported the fatal missing import but its process12632 remained alive
in shutdown. The same process/session was rechecked. A scoped process query
outside the sandbox verified its exact executable and Boot211 command line,
then stopped only that failed process. The harness closed with exit1. This is
not a claim of clean original game teardown; no save had been created.

XamContentGetDeviceData now returns real fixed-folder volume metadata through
the original80-byte record. The read-only query is separate from the selector's
write probe. Original8285CDF8's64-bit byte-to4KiB conversion is tested directly,
including optional outputs and exact stack-snapshot comparison. Content and
selector tests pass2/2 in5.54s; see native-storage-data.md. Boot212 uses the same
real profile/folder and a fresh input channel. Its live result follows.

### Boot212: real storage details accepted; autosave notice and execution identity

PressSTART was viewed at build/captures/native-loading-212-ready/native-frame-408398.png.
The normal command sequence START,A,A,A opened the storage warning, accepted the
real native folder selector, and dismissed the original autosave notice. The
existing achievement record was verified with newly_persisted0. Native selector
completion at log line2501394 reported selected1/device1/result0/caller-thread1F4.
Storage details at2501795 reported actual total1706177851392 bytes, available
526175166464 bytes and volume label Codex. The original autosave notice is visible
in build/captures/native-loading-212-after-storage/native-frame-690545.png.

After A, the original selected-device save scan created enumerator204 for user0,
device1,type1, zero entries and synchronous status18/count0. The actual content
folder remains empty. After present1780/draw753253, original82433BF0 called
XamGetExecutionId at LR82433C1C with output0203F110 and title45410809. That import
was not implemented. Boot212 exited1 naturally with all nine worker exit logs;
no forced termination was required. The last sampled capture still shows the
autosave notice and predates the fatal call. No main menu has been verified.

The execution query now returns the actual loaded original XEX optional-header
record. Original publisher validation, byte identity, bounded outputs and
CPU/host state checks pass along with memory/configuration regressions,3/3 in
0.25s. See native-execution-identity.md. Boot213 uses the same profile/folder and
a fresh command stream; its live result follows.

### Boot213: execution identity accepted; controller preference query reached

The original PressSTART prompt was viewed in
build/captures/native-loading-213-prompt/native-frame-470882.png. Normal START
was delivered at log line2055280. Achievement ID32 was verified without another
record. Normal A dismissed the viewed storage warning at native-frame-587411.
The actual folder selector completed at2542387 with device1/result0, and the
real capacity query reported total1706177851392, available524401311744, labelCodex.
The autosave notice was viewed at
build/captures/native-loading-213-after-storage/native-frame-670730.png.

Normal A dismissed that notice. The selected-folder catalog was empty. After
present1596/draw715134, XamGetExecutionId returned actual pointer010119C0 to the
loaded original metadata, title45410809. Original82C71CB8 then reached
XamUserReadProfileSettings at LR82C71D14 with title45410809,user0,XUIDcount0,
XUIDpointer0,settingcount2,IDs0203F228,sizeOut0203F220,result0. Original827B2CE0
requested IDs10040002 (camera Y inversion) and10040003 (controller vibration)
with a synchronous size-first query. The run exited1 naturally at this missing
import, with all nine worker exit logs and terminal native resource release.
No save was created and no main menu was verified.

The native preference query now reports the actual v1 profile's absence of
controller overrides. The original size/read/allocate/free path and active
player option loop are tested; game defaults remain untouched. The new target
passes in4.55s with identity/player regressions also passing. See
native-profile-preferences.md. Boot214 uses the same real profile/folder with
a fresh command stream; its live result follows.

### Boot214: controller preferences accepted; Saved Games and storage name

The viewed PressSTART prompt is
build/captures/native-loading-214-prompt/native-frame-478300.png. Normal START
was delivered at2188394, and the durable achievement was verified unchanged.
Normal A dismissed the storage warning at native-frame-610253. The next A
accepted the real folder selector at2692513 (device1/result0/caller-thread1F4).
Actual metadata reported total1706177851392, available522706378752 and labelCodex.
The autosave notice was viewed at native-frame-953831 and dismissed with A.

At4662390 the native preference read returned the actual v1 profile's absence
of controller overrides. The game continued and displayed Saved Games with
four New Game entries, viewed at
build/captures/native-loading-214-after-preferences/native-frame-1328689.png.
Normal A at6403358 selected a new-game entry. The preference read passed again
at6403361. Then original82C9ECB8 requested XamContentGetDeviceName atLR82C9ED0C,
device1/output82D01BB4/capacity28 UTF16 units. That import was unimplemented.
Boot214 exited1 naturally, all nine workers exited and terminal native owners
were released. The actual content folder remains empty; no save was fabricated.
Saved Games is an intermediate screen and main-menu arrival is still unverified.

The native storage name now comes from the actual volume metadata. The affected
content target passes in5.09s, including exact string extent and original tail/
UTF16-length consumers. See native-storage-data.md. Boot215 uses the same real
profile/folder and a fresh input channel; its live result follows.

### Boot215: real storage name accepted; first native save-container request

Normal START was delivered after the viewed PressSTART prompt in
build/captures/native-loading-215-prompt/native-frame-706332.png. The existing
achievement was verified, and normal A inputs dismissed the storage warning,
accepted the real folder selector and dismissed the autosave notice. Viewed
frames1191462 and1660225 show the two original notices. Execution identity and
the actual v1 profile's absent controller preferences passed again.

Saved Games was viewed in
build/captures/native-loading-215-saved-games/native-frame-2023338.png. Normal A
selected an empty New Game entry. At9335093 the name query returned the real
volume label Codex with capacity28 UTF16 units. Original82C9ECB8 continued past
that query. After present7523/draw2307566, worker41348 reached the unimplemented
XamContentCreateEx at LR82432508 in824324B8. The request was slot0,
root8215F5E4 (original literal rmcsave), contentDataE1ADC4E0, flags12,
dispositionOut0,licenseOut0,cacheSize0,contentSize0. The original9th stack
argument was set from caller r9=0 in824327D0, so the request is synchronous.

The worker stack continues via824327FC,8285B7B8,82CA8220,82C9E964,8232A8C4,
82CAC6CC and82CAC794. The complete live308-byte content record was not dumped;
its display name and filename remain to be observed or reconstructed, not
guessed. Original824324B8 already checked device nonzero/type1 and applied
flag10. The actual content folder is still empty. Boot215 exited1 naturally;
the same session was closed and no game process remained. No forced termination.

The last sampled complete frame,
build/captures/native-loading-215-new-game/native-frame-2249528.png, still shows
Saved Games and predates the fatal save transition. It is not main-menu
evidence. The next work is native save-container ownership, file writing and
publication using the actual original payload, retaining the game's flow.
Current asset filesystem access deliberately denies writes and create/overwrite
dispositions; it must stay read-only for original assets when save I/O is added.

The full integration build passes128/128 in235.51s after the newly qualified
storage metadata, execution identity, profile preferences and device name:
build/native-profile-storage-integration-build.log. No game is running and no
main menu or completed saved game has been verified. The goal remains active.

### Boot216: real save published; original first character-shadow boundary

After viewed PressSTART frame485652, normal START was delivered at2335748.
Viewed frames657605 and844940 show the storage and autosave notices. Normal A
at3207285 opened the native folder selector; A at3388633 accepted the actual
configured folder. Normal A at3811777 dismissed the autosave notice. Saved Games
was viewed in native-loading-216-saved-games/native-frame-956783.png.

A at4278140 selected the first New Game slot. The native CreateEx and creator
queries succeeded at4278895/96. The actual original content name is
SIMPSONS_SLOT1, display `0:00.00 (0%) The Land of Chocolate`, flags12. Original
code opened rmcsave:\SIMPSONS_SLOT1 and wrote28,8 and114764 bytes through20C.
Close at4283020 published the native index and released the session. The actual
114800-byte save and checksummed index persist under the existing Player GUID;
see native-save-store.md for hashes. A later original scan returned that one
real save. No progress was fabricated.

The opening level movie was viewed at1067980 and1078335. A normal START tap at
4384488 was delivered during the movie. Before a visible main menu, the game
reached the same first character shadow as Boot203:826B5FC0,caller8270715C,
technique0007FFFC,typedE4CEA400,wrapperE1A9E3C0,managerE1A9C3C0. It failed at
4421939 on the existing activation guard. This is the exact RenderShadowDepth
program whose shader is tested but whose mesh/bone binding remains unfinished.
The last complete sampled frame predates this failure and still shows the movie.
Boot216 exited1 naturally; its same harness session was closed. The completed
save remains intact. Boot217 reuses that real save to inspect the normal
existing-save menu route; it does not overwrite progress or force a menu state.

The current complete integration suite is130/130,239.93s, in
build/native-save-integration-build.log. Four affected save/catalog/filesystem
targets also passed in5.56s. Main-menu arrival remained unverified at Boot216.

### Recorded startup: main menu verified (2026-09-13)

Replay011 loaded the original existing114800-byte save, reached Main Menu, and
the native renderer capture was viewed at
build/automatic-startup/replay-011-review/native-frame-2026877.png. The earlier
damaged-save error was missing file-size metadata, fixed with real native save
directory opening/enumeration; timestamp display also needed native timezone
and calendar conversion. Profile, save payload/index and achievement hashes are
unchanged. Full integration including these fixes passes133/133.

Replay012 independently automated title→storage notice/selector→autosave notice→
existing slot→load confirmation→Main Menu→Continue Game→opening movie Start.
Main Menu appeared38.23 seconds after launch (7.02 seconds after Press Start).
The movie began39.28 seconds after launch. At114.47 seconds the original first
character shadow reached826B5FC0/8270715C/0007FFFC and stopped at its remaining
rendering activation guard. Gameplay is not yet verified.

The launcher is Play The Simpsons Game Automatically.cmd. The reusable helper
also supports --until main-menu. Inputs, receipts and screen evidence are in
build/automatic-startup/replay-012; config/startup_replay.json contains the
verified cues. The requested Computer plugin failed to initialize twice;
the opted-in native command channel supplied these actual recorded inputs.

Native character-shadow activation now passes its focused original-entry/cache/
GPU-binding/cleanup test and a full133/133 suite in93.90 seconds. Replay013 is
checking the live result; shadow constants, mesh binding and draws remain work.
