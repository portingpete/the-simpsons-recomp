# Current development status - 2026-10-05

The existing local build loads all levels. Full gameplay testing across all
levels is still incomplete; loading a level does not establish that its
progression, transitions, rendering, audio, and extended stability have all been
verified. The port remains in development.

Sound is enabled by default. Use `--mute-audio` for muted diagnostic runs.

The reports below are historical checkpoints. Their bounded verification and
limitations describe those earlier builds, rather than the current level-loading
coverage.

# Goal achieved - reach gameplay without crashing (2026-09-19)

Run315d reaches the level and visibly completes two A-triggered jumps and
landings. Two120-second observer windows finish without failure; the final
process/log audit confirms the original game still alive after331.766 seconds.
A late fresh completed capture has45226 scene draws at presentation2940.
The game is left running. This is bounded entry/control verification, not a
claim that every level or indefinite play is complete.

The native mono and four post-filter passes complete. Both Homer resident
banks (235+201 records), loc_global (479 records), and the reached streamed
dialogue are qualified through unchanged ownership/decoder guards. AOT/game
builds and focused rendering/audio/replay tests pass; the full suite was not
rerun. Bright green/pink window colors and performance remain unresolved.
See [reach-game315 evidence](docs/reach-game-315.md) and
[verification receipt](build/automatic-startup/reach-game-315d/gameplay-verification.json).

# Latest verified fix � character geometry (2026-09-19)

Homer and the rabbit now render with attached limbs and facial geometry in
live captures. The color pass now decodes packed bone indices in the same
order as the shadow pass and uploads already-transposed bone vectors without
transposing them again. Both48- and56-byte skin layouts are covered.

Seven focused checks pass, including hardware/WARP skinning and matrix tests
with distinct rotations/translations. AOT verifies311 files with zero semantic
diagnostics; the game build passes. Early and late captures confirm removal
of the stretched triangles and detached facial parts. The full suite was not
rerun. See [fix and evidence](docs/character-geometry-fix.md).

The updated character-geometry objective is achieved. The earlier
reach-in-game objective was superseded. The live run still completes98 scene
frames and2571 scene draws before the unrelated mono dispatcher guard;
sustained gameplay remains unverified. The sky occlusion fix is retained.

# Historical fix � sky foreground occlusion (2026-09-19)

Sky draws now apply the same reversed-depth conversion as the world and retain
an original mask-discard instruction previously omitted from translation.
Hardware/WARP regressions verify foreground occlusion, background fill,
depth/stencil preservation, both viewport directions, and immediate/cached
parity. All four focused checks pass. The game/AOT builds pass.

The inspected final capture shows chocolate ground, walls and railings where
the sky previously covered foreground objects. Live sky-front-b completes98
scene frames and2571 scene draws before the existing mono dispatcher stop.
Other rendering defects, sustained gameplay and character control remain
unverified. The broader goal remains paused. See [fix and evidence](docs/sky-occlusion-fix.md).

# Historical progress � reach-game313 (2026-09-19)

Original skin morph selection now supplies the six native morph attributes,
with exact stream masks, source ownership and bounds checks. Final313e
processes281 bindings across streams1 and2, completes98 scene presentations
and2571 scene draws, then stops at the existing mono dispatcher guard.
The newly inspected frame has HUD and effects but malformed pink/black world
geometry. Opening-movie completion, sustained rendering and character control
remain **unverified**.

The mono effect has pinned original shader/CPU evidence and native shader
tests on hardware/WARP. Its production material, staging and draw integration
remain pending. Nine focused tests pass; strengthened mesh upload and vertex
decoder tests pass again. Latest313f AOT verifies311 files with zero semantic
diagnostics, and the game build passes. The full suite was not rerun.

The goal tool reports **paused**. See [current checkpoint](docs/reach-game-313.md)
for captures, source hashes, validation and the next implementation step.

# Latest verified progress — reach-game312 (2026-09-19)

Projected particles now use the original four depth samples, mirrored point
sampling and ambient visibility through a native shader. Static evidence and
hardware/WARP tests pass, including original CPU constants, full draw state
restoration and existing particle parity. Two additional mono dialogue sources
and the69-source characters resident bank are qualified and integrated.

Live312d completes98 scene presentations,2571 scene draws, passing the previous
particle and audio stops. The next guarded boundary is scene flags2 selecting
the original `mono` effect8211F480 at dispatcher82740680. Its original source,
CPU path and reached static geometry are captured for native implementation.
Ten focused tests pass across graphics, dialogue and resident audio. Latest
game/AOT builds pass (311 generated files, zero semantic diagnostics); the full
suite was not rerun. The latest inspected image remains visibly malformed.
Sustained rendering, opening-movie completion and character control remain
**unverified**. Goal remains active. See [current checkpoint](docs/reach-game-312.md).

# Historical progress — reach-game311 (2026-09-19)

Cached payload00600106 now retires successfully after13 native replays. Its
native GPU resources and retained effect data are released; original CPU code
unlinks both lists, subtracts1608 bytes and returns its slot. The complete
remaining graph validates with90 retained records and1910 free slots.

Runs311/311b/311c complete36 scene presentations, up from29. Five focused tests
pass, including original driver lifecycle and hardware/WARP recording/mesh
tests. The later frame remains visibly malformed. The next stop is the
particle shadow-sampling branch (definition104 mask04), selecting original
PS82156B60 and the original1024x1024 shadow map. Its resource mapping and shader
still need qualification. Latest311c game/AOT builds pass.
Sustained rendering, opening-movie completion and character control remain
**unverified**. Goal remains active. See [current checkpoint](docs/reach-game-311.md).

# Historical progress — reach-game310 (2026-09-19)

Source8200CCB8 now completes native alpha draws while preserving its opaque
pass. The selected alpha pass uses its own base texture at stage0 and retains
the original staged camera position. Real mesh tests pass on hardware/WARP,
including blending, depth/stencil, state restoration and recorded parity.

Run310d completes29 scene presentations and six alpha packets, then stops at
nonempty cached-record destruction827374B0. The record's native payload and
original list/accounting fields are captured for retirement implementation.
Seven focused tests and game/AOT builds pass; the full suite was not rerun.
The new scene readback remains malformed. Sustained rendering and character
control remain **unverified**. Goal remains active.
See [current checkpoint](docs/reach-game-310.md).

# Historical progress — reach-game309 (2026-09-19)

Native mode-zero billboards preserve the original particle CPU math and now
submit72 quads in run309b. The first mono dialogue is independently qualified
and commits real PCM through the original mixer. The run completes23 scene
presentations and stops at a new alpha draw of material8200CCB8.

The new alpha shader pair is transcribed offline and passes independent GPU
tests on hardware and WARP. Its runtime pass/material/texture integration is
still required. Ten focused tests pass; game/AOT builds pass. The full suite
was not rerun. The captured world remains malformed, and sustained rendering
and character control remain **unverified**. Goal remains active.
See [current checkpoint](docs/reach-game-309.md).

# Historical progress — reach-game308 (2026-09-19)

Opaque dual-textured skin source8201CD48 now completes its native draw. Runtime
integration includes UV1, source-specific bone/material storage, retained base
and character-shadow textures, exact sampler checks and both submission paths.
Native immediate/recorded GPU tests pass on WARP and hardware.

Run308c stops at the next four-vertex immediate billboard caller8275F6E4 after
12 scene presentations. Its original inputs are captured for integration.
A308b scene readback still shows a malformed world. Sustained rendering and
character control remain **unverified**; goal remains active. Game/AOT builds
and nine focused tests pass. Full suite was not rerun.
See [current checkpoint](docs/reach-game-308.md).

# Historical progress — reach-game307 (2026-09-19)

Three existing skin defects are fixed: indexed bone reads, UV/weight/color
input semantics, and zero bone lanes overwriting stale stage constants.
The opaque dual-textured skin pair is transcribed offline and passes independent
GPU checks on hardware and WARP. Its runtime integration is still unfinished.
Run307 again presents12 scene frames and stops at source8201CD48; its capture
confirms three bones and a56-byte vertex with a second UV set.

Sustained rendering and character control remain **unverified**. No new scene
readback was obtained, so visual improvement is also unverified. Game build,AOT
verification and six focused shader tests pass; the full suite was not rerun.
Goal remains active. See [current checkpoint](docs/reach-game-307.md).

# Historical progress — reach-game306 (2026-09-19)

Twelve scene frames are presented. Cached sky draws now record and replay with
owned material/textures and fresh inherited constants. An inherited sampler
variant is handled, and all50 Land of Chocolate music streams qualify and feed
the original mixer. Run306 reaches an unsupported dual-textured character
material (`simpsons_skin_dualtextured`,8201CD48).

Rendering remains visibly malformed; sustained rendering and character control
are **unverified**. Goal is active. The game/build,AOT verification,six focused
renderer tests and both complete music-bank tests pass. The full suite has not
been rerun. See [current checkpoint](docs/reach-game-306.md).

# Historical progress — reach-game301 (2026-09-19)

Five malformed scene frames are presented. Resident intro/loop queue ownership
and a native XMA packet-boundary fix now admit120 ordinary and12 looping loc
sounds, plus all429 story sounds. Original mixer PCM commits and intro request
retirement execute. Run301 stops at the sky's unsupported cached drawing path.
Sustained rendering and character control remain **unverified**. Goal is active.

Build, source qualification and six focused audio/lifecycle tests pass. The
full suite has not been rerun. See [current checkpoint](docs/reach-game-301.md).

# Historical progress — reach-game299 (2026-09-19)

Four malformed scene frames are presented. Immediate buffer ownership, rigid
recording masks, native trails/radial sprites, normalized particle colors and
the L8 flare texture are implemented. The verified story bank admits427 sounds;
three previously blocked sounds now start. Run299 stops at a looping24kHz
resident sound in loc.sbk. Sustained rendering and character control remain
**unverified**. Goal remains active.

The build and focused renderer, texture, audio source and lifecycle checks pass.
The full suite has not been rerun. See [current checkpoint](docs/reach-game-299.md).

# Historical progress — reach-game290 (2026-09-19)

Particles, direct sprites and five original corona visibility queries execute.
Run290 then stops during camera reset on an unknown native buffer identity.
One malformed scene frame is presented; sustained rendering and character control
remain **unverified**. Goal remains active.

The current build and focused GPU/original-CPU/material tests pass. The full
suite has not been rerun. See [current checkpoint](docs/reach-game-290.md).

The statuses below are historical.

# Historical progress — reach-game281 (2026-09-19)

One malformed scene frame is presented. The former particle GPU-allocator hang
now executes a native particle draw, preserving the original CPU vertex update.
Run281 then stops at the next direct screen-sprite path, original8276AF78.
Sustained rendering and character control are **not verified**. The startup
checker no longer treats one frame as gameplay success.

Current build and four focused tests pass. The broader suite has eight failures
documented in [the checkpoint](docs/reach-game-281.md). Scene orientation and
material rendering remain visibly incorrect. Goal remains active.

The statuses below are historical.

# Historical progress - replay057 (2026-09-18)

Normalmap82057E08 now executes with its exact VS8205855C/PS82058CBC, 624-byte
material staging (c44/c45/c46/c47/c49/c50), owned base slot2 and normal slot3
and native depth adapter. UV1 and tangent are consumed (stride40,
6-element declaration). Replay057 completes 9 normalmap packets
(1+1+1+2+1+1+3+1+3 draws) and advances to unsupported simpsons_skin82006348
(identity0050001A, technique_AC00000000). No presented gameplay frame or
character control is verified. Full CTest passes 170/170. Details:
K:\SimpsonsNativeCopy\docs\reach-game-057.md.

The statuses below are historical.


# Latest verified progress - replay056 (2026-09-17)

Multitone820547E8 now executes with its exact VS/PS, 608-byte material staging,
owned noise slot3 and native depth adapter. RGBA8 mip decoding now covers the
original tonal-swirl and buildings textures. Replay056 advances to unsupported
simpsons_rigid_normalmap82057E08. No presented gameplay frame or character
control is verified. Dispatcher-only probes057/058 confirmed the missing
normalmap profile; both temporary allowlist changes were reverted. Details:
K:\SimpsonsNativeCopy\docs\reach-game-056.md.

The statuses below are historical.


# Latest verification - replay050 and multitone inventory (2026-09-17)

**Audio blocker resolved; replay050 verified the main menu, Continue Game and
movie skip, then rejected simpsons_rigid_multitone820547E8 at scene dispatch
(82740680, caller8273B4E0, packet flags=0).** The opaque multitone pair
VS82054F2C / PS82055710 is inventoried offline: 41 VS and 91 PS slots, a
conditional VS EXEC block, and a 608-byte private bank with noise leaves 20-22
targeting PS c47/c45 and VS c46+PS c46. Its HLSL, GPU tests and runtime
integration are unfinished; the material remains rejected. No runtime guards,
shader aliases or audio paths were changed. Focused CTest is 7/7; the older
126/166 full-suite tally is historical. See docs/reach-game-050.md.
The replay049 description below is historical.


# Current built status - LIVE034

**Latest built checkpoint: LIVE034. Gameplay remains unverified.**
The goal is paused. See [current checkpoint](docs/session-checkpoint-2026-09-13-live034.md)
for implementation, validation, the exact failure, and continuation commands.
The older frozen/unbuilt statements below describe historical LIVE027 sources.

The latest replay verifies the main menu, existing-save Continue Game, and movie
skip. It completes31 shadow draws,133 depth-prepass draws,16 original cache
recordings containing18 rigid draws, and27 immediate draws. The textured rigid
material consumes original mip chains. The next material is
simpsons_rigid_dualtextured8202AD78, stopping at82740680 from8273B4E0.
No presented gameplay frame or character control has been verified.

The sources and executable include exact textured rigid shaders, retained base
texture ownership, original parameter maps, and BC1/BC2/BC3 tiled mip storage.
Independent fixtures compare34 original/synthetic levels; native tests sample
compressed mips down to1x1 on WARP and hardware. Small UI textures retain their
prior behavior. The final complete162-test suite passes in109.15s; the checkpoint links
the validation log. The original data/reference trees are unchanged.

## Historical status below

# Current status - 2026-09-13

**Progress checkpoint saved at the user’s request. Gameplay remains unverified.**
See [session checkpoint](docs/session-checkpoint-2026-09-13-live027.md) for the
frozen sources, archive, evidence and resume commands. No implementation is running.

**Current sources are unbuilt:** multiple-record ownership and effects retention,
six original observation hooks, and the expanded GPU fixture have not been
regenerated, compiled or run. The CPU graph fixture header is unwired. The160-pass
result below belongs to the earlier LIVE027 executable, not these new edits.

Latest replay
`build/automatic-startup/reach-game-027` verifies the main menu, existing-save
Continue Game and opening-movie skip. Both shadow cameras finish31 draws
and two full depth/stencil copies; the static main-camera depth prepass
finishes133 draws. No gameplay color frame or character control is verified.

The actual flags0 simpsons_rigid8200CCB8 material now activates its opaque
VS8200D3AC/PS8200D9E8 pair. Original82740420 selects recording; original
826F4D08 allocates/links a CPU record and switches both native context aliases.
The native payload is a real deferred recording with independent ownership.
All82 scalar and320 sampler application fields are resubmitted. Original
nonnull FX context association completes. Live027 also completes the original
three-row shadow-resource loop: two copied depth owners bind at deferred PS
stages0/1 and the unused edge row is skipped. The first rigid mesh
completes its material commit and records3530 indices as one native draw. Real
FinishCommandList seals1320 owned bytes; original CPU status/accounting/LRU
publication and context restoration complete. Original replay stages both full
56-register banks and verifies both copies of each bank. The native payload
executes once. Its private1280x720 scene target remains all zero before/after;
this establishes neither visible color nor presented gameplay. The next object
hits the single-record guard. Multiple cache records and geometry coverage are
saved as unbuilt work for resumption. No original deletion success is claimed.

Independent native rigid shader, mesh and recording-payload GPU tests pass
on WARP and hardware, including deferred execution, per-draw material data,
fresh inherited replay constants, copied D24FS8 depth sampling, RGB10A2
color/depth/stencil readback, immediate-state preservation and ownership.
The original rigid vertex capture passes28010 checks over2613 vertices.
Rigid native shader preparation validates exact480/800-byte constant banks.

The complete160-test suite now passes in111.96s:
build/reach-game-rigid-union-full-build-tests.log. This includes the new
strict zero-work qualification for shared-map union mode, original material
accumulation, independent shadow depth binding on WARP/hardware, state coverage,
and the corrected shader/driver expectations from the earlier155/157 run.
LIVE027 confirms one real command-list execution; visible scene rendering remains
unverified. Regeneration verifies311 AOT files with0 semantic diagnostics.

No runtime CPU interpreter, shader translator or console GPU executor is
used. Original data and reference trees are unchanged. Physical-console
precision parity remains unproven. See `docs/native-rigid.md`,
`docs/native-zprepass.md`, and `docs/automatic-startup.md`.

The September11 entries below are historical checkpoints.

**Session stopped at the user's request on September11,2026.**
Build188 results are preserved; actual162 was not started. Resume from
docs/session-handoff-188.md. The playable-port goal is incomplete.

The goal remains a faithful, playable native Windows port of **The Simpsons Game
(US Xbox 360 retail)**. The workspace builds a native x64 AOT executable and runs
the original startup entry. **Original loading artwork and EA/Fox movie images now reach completed front
readbacks. Accepted desktop scanout, menus, world, controllable gameplay,
progression and save/load have not been verified.**

Build188 is compiled and passes all96 suites in169.05 seconds. Original primitive3 complete triangle lists and independent Im2D repeat/clamp U/V are supported. Im2D passes449,810 checks on WARP/hardware, engine state9,888, and original driver123,265 (157 Im2D draw checks). Actual161 submitted nine498-vertex font batches and completed front copy583, then rejected a newly reached original pixel-program expression. Readable text, menus and gameplay remain unverified. The final build adds exact shader-expression diagnostics and first-textured-frame capture; actual162 is pending after the requested session stop.

Session checkpoint: checkpoints/native-im2d-lists-161.zip. Results: build/im2d-upload/build188-summary.json; archive verification: build/im2d-upload/checkpoint188-verification.json. Handoff and next command: docs/session-handoff-188.md. The following build187 and older entries are historical.

Build187 / actual muted boot158: copied ITXD texture ownership, original allocation/copy/relocation/index/group lifetimes, lazy BC2 upload and checked nonnull RenderWare binding are implemented. All96 suites pass in163.30 seconds; the final focused Im2D fixtures pass359,956 checks on both WARP and hardware. Original ITXD lifecycle passes127 checks; original atlas conversion passes1,048,595 checks; original driver passes123,175 checks. Native binding/resource fixtures also pass on hardware.

Actual158 passes the previous selector1 failure and creates the native1024x512 HighlanderStdBold6060b atlas (rasterE1AC6548). It stops at the next original Im2D entry guard: primitive/count/camera/raster/buffer capability. No textured draw has been submitted, and no menu/text/gameplay success is claimed. The180-second allowance was not exhausted. Unadjusted completed-front captures preserve EA/Fox images and the later black UI background. Evidence: build/im2d-upload/build187-summary.json and build/boot-158.log. Checkpoint: checkpoints/native-itxd-font-binding-158.zip. Next: capture the rejected draw arguments and qualify its original geometry path.

Reproduce: python -B tools/recompile.py --verify; .\tools\build.ps1 -Jobs 8; .\build\native\Im2DBackendTests.exe --hardware; .\build\native\NativeGraphicsTests.exe --hardware. Actual run: python -B tools/run_native.py --timeout 180 --log build/boot-158.log --capture-frames build/captures/native-loading-158 (use a NEW log/capture number for subsequent runs).

Build186 / actual muted boot155 passes **94/94 suites in171.03 seconds**.
The Im2D renderer now performs original depth comparisons and writes using
the selected D24FS8 working attachment. Original vertex Z remains unchanged;
the pixel shader applies the retained camera reversal once, then stores the
exact decoded20e4 value. The reached UI constant produces3F004188. All eight
comparisons, write enable, alpha discard, culling and ordered triangle blending
are tested. Stencil remains disabled and unchanged.

WARP and hardware each pass337,420 native Im2D checks with the debug layer.
The original driver passes122,091 checks, including67 Im2D checks through the
real upload/setup/commit/epilogue. Actual154's exact UI vertices replay with
black pixels and depth3F004188 on rows0..718; its original bottom edge leaves
row719 uncovered under native rasterization. Source vertices remain unchanged.
Proof:2,202 original words/23 spans/78 instruction pins/6 data pins/4 SDK rows/
4 shipped shader strings; caller proof707 words/31 pins. General physical-console
rounding/interpolation and raster precision remain unverified.

Actual155 received180 seconds and exited on an explicit failure before timeout.
It passes the former depth rejection: three UI depth-enabled draws570/572/574
run with enable1/write1/ALWAYS and reversed mapping. It completes601 native
presentations, all occluded; the last contains1,164 total draws. There are574
successful Im2D draws. All three decoder threads finish0 and all27 movie planes
retire through original code. Global original cleanup remains incomplete.

The next failure is a nonnull RenderWare raster request:
selector1, rasterE1AC6548, original caller826C0268, boundary824025A8.
This path requires native texture ownership/binding and original alpha/cache
semantics. It remains an explicit failure; no texture or draw was fabricated.
Menus, world, controllable gameplay, progression and save/load are unverified.

Unadjusted completed front0150 shows the EA logo and front0630 shows upright
20th Century Fox Television. These are renderer readbacks, not accepted desktop
scanout. The existing capture allowance does not yet capture the later UI
transition; the depth fixture and executed draw logs are separate evidence.

AOT:1,349 inputs,311 outputs,zero semantic diagnostics,211 byte-matched hooks.
Game SHA256:`395bb8624d58be18c746b21b13b6a5c854e087462759c40751e2210255bfa8ce`.
Evidence:`build/im2d-upload/build186-summary.json`,`build186-tests.log`,
`aot-manifest186.json`,`im2d-depth186-hardware.log`,`build/boot-155.log`,
`build/im2d-depth/verification.json`, and `build/captures/native-loading-155`.
Details:`docs/native-im2d-depth-integration.md`,
`docs/native-im2d-depth-contract.md`,`docs/native-im2d-depth-backend.md`.
Build: `.\tools\build.ps1 -Jobs 8` (or `-SkipGenerate` with verified current AOT).
Run: `python -B tools/run_native.py --timeout 180 --log build/boot-NEXT.log
--capture-frames build/captures/native-loading-NEXT` (use new paths each run).
Next: qualify the reached nonnull raster binding, preserve the original CPU
state/cache path, and extend capture sampling to the later interface phase.

Latest verified checkpoint:`checkpoints/native-im2d-depth-155.zip`, SHA256
`6dd459d37dd75d649e2b982c57581f63db5492a2693d49115d45a1546f9079d9`. All5,738 payload hashes/exact members and1,346 included
AOT inputs match. Verification:`build/im2d-upload/checkpoint186-verification.json`.
Archived STATUS predates this final pointer.

Previous build185 / actual muted boot153 passes **94/94 suites in159.90 seconds**.
The requested native launcher is ready: **Play The Simpsons Game.cmd**, then
Play. It has no time limit and retains per-launch logs. Its self-test passes.

Full1280x720 type5 color and private type1 depth now retain the default working
storage, with distinct logical IDs, original list nodes, cache transitions and
retirement. Front/copy textures remain separate. Original clears prove both
directions of visibility; reset rollback, stale identity and lifetime checks
pass. Color proof:1,937 original words/54 pins; depth:1,163 words/82 pins.
Different-size partial surface overlap remains unqualified.

The six-word exit-status bridge82433358 to82433370 queries the retained native
thread handle. Native signaling determines completion; original typed object
reference, output store, dereference, error/poll/close paths remain AOT.
Thread tests:3,235 checks including suspended/running, final0/42/FFFFFFFF/259,
real ThreadExit, TLS teardown barrier, closed-handle races and retained owners.
Viewport pass154/reset1,440/copies6,244 also pass.

Actual153 was allowed180 seconds and exited on an explicit failure before
timeout (log timestamps span roughly47 seconds). It advances through three
VideoDecodeThread lifecycles, all native status0. The first completed query
observes object01061000/handle1B4/native39816. All27 allocated movie planes
are retired by the original raster path;9 frame allocations across3 lifecycles.
There is no remaining old thread-exit poll stall in this run.

Unadjusted completed front0152 shows the upright EA logo and front0632 shows
20th Century Fox Television. They exactly match private movie targets0060
and0300, respectively, including raw packed pixels. Private0180 shows an
earlier Fox image. These are renderer readbacks; all577 presentations are
occluded. Last completed presentation has1,114 total draws. The next attempted
Im2D draw fails after545 successful Im2D draws with:
**Original Im2D depth-write/cull state is unqualified**.
Its exact state values/caller are not included in this old diagnostic. Global
original cleanup remains incomplete. Menu/world/gameplay/saves are unverified.

AOT:1,349 inputs,311 outputs,zero diagnostics,211 byte-matched hooks.
Game SHA256:`a496811fa487626576010982e5b79cff3ecaf448a39ac932ddb8d37d79f62c24`.
Launcher SHA256:`54701036e48980ef976a9f6391a743ca6b449cc45c12b231794ba56bbc985b8a`.
Evidence:`build/im2d-upload/build185-summary.json`,`build185-tests.log`,
`aot-manifest185.json`,`build/boot-153.log`, and
`build/captures/native-loading-153` (five selected unadjusted PNGs).
Details:`docs/native-fullsize-working-surfaces.md`,
`docs/native-fullsize-depth-alias.md`,`docs/native-thread-exit-status-bridge.md`.
Next: capture exact reached Im2D depth-write/cull and caller state, then verify
original semantics before extending the native drawing path.

Previous verified checkpoint:`checkpoints/native-movie-continuity-launcher-153.zip`, SHA256
`8a66410db330d52c0781b7cb4c8ac6791561b586f29be100b12ae9c3b2c9a0d6`. All5,457 payload hashes/exact members and1,346 included AOT
inputs match, including compiled shader evidence. Verification:
`build/im2d-upload/checkpoint185-verification.json`. Archived STATUS predates this pointer and local formatting repair.

Previous build184 / actual muted boot152 passes **93/93 suites in153.60 seconds**.
The native launcher remains ready: **Play The Simpsons Game.cmd**, then Play.
It has no time limit and includes per-launch logs. Its self-test passes again.
The current game build is muted and still cannot complete startup.

The original movie decoder now reaches a real native three-plane draw. Private
camera captures0060 and0120 show the upright centered **EA logo**, viewed without
image adjustment. Initial target0001 exactly contains RGB10A2 codes3,0,4,0.
This is movie-target output, **not desktop scanout**: completed front0332 is black.
The full original presenter, frame uploads, lifetime and return path remain AOT.

Movie integration passes25,597 checks with five real draws and original ABI;
plane lifetime3,019; geometry covers2,304 original store traces. Both WARP and
hardware pass3,775,647 backend checks with exact qualified pixels and zero
debug-layer errors. Shader evidence passes19 offline tests, including native
VS/PS linkage. Broader console filtering/raster/precision parity is unverified.

Both actual151 and152 received the full180-second allowance and required
harness termination after progress stopped. Boot152 completes186 presentations,
all occluded:332 total draws at the final presentation,150 movie/153 Im2D/
29 screen. There is no reached explicit failure. Normal original cleanup,
complete movie playback, menu/world/gameplay/progression/saves remain unverified.

A bounded live LLDB snapshot sampled70 threads in0.406 seconds and detached.
Main thread22896 was in82433328 through8232B0A8/82375C88/82373738/8282D998.
This identifies the original thread-exit query for investigation; the sample
alone does not establish a cause. See `docs/native-process-sampling.md`.
The native thread exit does not publish the guest fields read by that original
helper. `docs/native-thread-exit-status.md` qualifies a narrow native status
query inside82433328, preserving the original reference/error/store/dereference
paths. Native handle signaling must determine completion; do not fabricate it.

Original color-placement evidence now explains the black final frame: full-size
movie/default surfaces share the same pixel memory, while native allocation
currently gives them independent backing. Original equal-size rendering skips
composition deliberately. Qualify shared backing while retaining logical
identities; revise the existing incorrect viewport-isolation expectations.
Evidence:`docs/native-movie-composition.md` (1,937 original words/54 pins).

AOT:1,348 inputs,311 outputs,zero diagnostics,210 byte-matched hooks.
Game SHA256:`2c2448f53aa1722a376719e4c88fcb73f193d7e8aeaf83f483b19c687d5b1322`.
Launcher SHA256:`54701036e48980ef976a9f6391a743ca6b449cc45c12b231794ba56bbc985b8a`.
Evidence:`build/im2d-upload/build184-summary.json`,`build184-tests.log`,
`aot-manifest184.json`,`movie-draw-original184.json` (1,206 original rows).
Details:`docs/native-movie-rendering.md`,`docs/native-movie-integration.md`,
`docs/native-movie-backend.md`,`docs/movie-shader.md`.
Next: implement qualified shared movie/default color backing and resolve
exit-status query82433328. Keep original composition conditions and presentation.

Previous verified checkpoint: `checkpoints/native-movie-renderer-launcher-152.zip`. SHA256 `6536f5985e91ecb67ec7072b0a73c941f464a0490e8bfee00bbb66f4eadbef10`. Exact members/all hashes verified in `build/im2d-upload/checkpoint184-verification.json`.

Previous build183 / actual muted boot149 passes **87/87 suites in147.26 seconds**.
The user's double-click launcher is built: **Play The Simpsons Game.cmd**
opens `build/native/SimpsonsLauncher.exe`. Play runs the game with no time limit,
keeps a separate log, and displays its exit status. Play, disabled/running state,
nonzero exit recovery, Open latest log and Open log folder were exercised in the
native UI. The game remains muted and incomplete. Computer Use was stopped on
the user's physical Escape request after these checks; no further UI input was sent.

Movie buffers now use native single-channel R8 storage and the original pooled
CPU allocator. The original provider allocates all three frame descriptors and
nine planes, and starts VideoDecodeThread. Original locks, unlocks and frame
retirement remain AOT, with native callbacks for owned pixel storage/upload.
Movie-plane lifecycle passes3,019 checks; driver122,046; crossfade244;
writable textures576 each on WARP and hardware. No full codec or movie image
correctness is established by these resource checks.

Boot149 was allowed180 seconds and exited before timeout with an explicit
read failure at000029C0 in8243B718, LR8282E408. This is movie draw8282E3E8
reading absent console-device state. All35 presentations were occluded;
3 Im2D submissions/31 total draws at the final presentation. Unadjusted
renderer capture0030 contains dim Itchy/Scratchy artwork;0031 is black.
Normal original cleanup, movie playback, menus and gameplay remain unverified.

AOT:1,348 inputs,311 outputs,zero diagnostics,209 byte-matched hooks.
Game executable SHA256:`746cd55e1bb417e95444fca8be32256b4cedb991783fb0071f13241da320f9c0`.
Launcher SHA256:`54701036e48980ef976a9f6391a743ca6b449cc45c12b231794ba56bbc985b8a`.
Evidence:`build/im2d-upload/build183-summary.json`,`build183-tests.log`,
`aot-manifest183.json`,`movie-plane-original183.json` (4,490 original rows).
Details:`docs/native-movie-plane-lifecycle.md`,`docs/launcher.md`.
Next: original movie draw's three-plane shader, rectangle geometry and effective
state; see `docs/native-movie-draw.md`. Do not fabricate console device objects.

Previous verified checkpoint:`checkpoints/native-movie-planes-launcher-149.zip`,SHA256
`e9d628f5bf086a1387166c309166eb952c12f936b5da6e2b7bc839ca99581fcc`. All4,912 payload hashes/exact members and1,345 included AOT
inputs match. Verification:`build/im2d-upload/checkpoint183-verification.json`.
Archived STATUS predates this pointer.

Previous build182 / actual muted boot146 passes **85/85 suites in150.72 seconds**.
Optional platform music-control wrappers now report Win32 unsupported while
the original game retains its intent stores. NtYieldExecution calls the real
Windows scheduler; original82B76B98 converts the returned status. Configuration
checks pass65,767; driver checks122,042 and crossfade lifecycle checks244.

The user's longer-loading request is retained: boots143�146 were each allowed
180 seconds. They exited on explicit failures after roughly8�9 seconds, before
the allowance elapsed. Final boot146 reaches original8282E940 movie-plane
creation:1280x720,format28000002,flags400,caller8282E9B8. A shared allocator's
crossfade-only check initially mislabeled this as duplicate ownership; its
diagnostic now reports the actual unimplemented caller first.

Boot146 completes35 presentations,all reported occluded,with3 Im2D draws and
31 total draws at the last presentation. Unadjusted renderer frame0030 has
dim Itchy/Scratchy artwork; frame0031 is black. No desktop scanout, complete
movie/menu, gameplay, progression or save/load is verified. Normal original
cleanup is incomplete. Next: native movie-plane allocation,locking and pixel
ownership at original engine boundaries, retaining original decoder execution.

AOT:1,348 inputs,311 outputs,zero diagnostics,206 byte-matched hooks.
Executable SHA256:`26a15c556a2ef6ee0395cae0923c6b2c3f98a440f88f48628d001419bfe8b0b1`.
Evidence:`build/im2d-upload/build182-summary.json`,`build182-tests.log`,
`aot-manifest182.json`,`platform-original182.txt`(404 original words).
Details:`docs/native-external-music.md`,`docs/native-yield.md`.

Previous verified checkpoint:`checkpoints/native-platform-loading-146.zip`,SHA256
`60c7ad187547b61b65214273283bc2a852c08147999dfe524078bb9a3f5ad1cd`. All4,692 payload hashes/exact members and1,345 included AOT
inputs match. Verification:`build/im2d-upload/checkpoint182-verification.json`.
Archived STATUS predates this pointer.

Previous build181 / actual muted boot142 passes **85/85 suites in 154.75 seconds**.
The actual game now submits3 Im2D draws using the original CPU setup,
shader selection and epilogue. Native driver lifecycle passes122,042 checks,
including15,157 original overlay state checks,25,357 immediate sampler checks
and32,771 expansion callback checks. Im2D GPU tests pass15,431 checks on both
WARP and hardware. Original game/helper bodies stay ahead-of-time compiled.

The original overlay caller now has native expansion callbacks and checked
original shade/cull/alpha queues plus immediate U/V/filter updates. Sampler
failure rolls back real CPU and native changes together. No console device,
command interpreter or draw success is fabricated by these state operations.

Actual boot142 completes35 presentations with0 display-accepted reports,
then stops at `unimplemented import __imp__XamGetSystemVersion`. Original normal cleanup remains incomplete.
Latest capture: `build/captures/native-loading-142/native-frame-0031.png`;
it is black. Prior frame0030 contains the original dim Itchy/Scratchy artwork.
These are unadjusted renderer readbacks, not desktop scanout. Metadata records actual
display acceptance and separate screen/Im2D draw counts. No completed menu,
world, controllable gameplay, progression or save/load has been verified.

AOT:1,348 inputs,311 outputs,zero diagnostics;204 byte-matched hooks.
Executable SHA256: `83b0b0715ece88c0e8079dce6ef56a701387c644ea286738c50c92b7e5b1e50d`.
Evidence: `build/im2d-upload/build181-summary.json`, `build181-tests.log`,
`aot-manifest181.json`, `integration-evidence181.json` (2,294 original words).
Next: inspect and implement the original XamGetSystemVersion compatibility query.

Previous verified checkpoint: `checkpoints/native-im2d-overlay-142.zip`, SHA256
`12418ce8015d9ed3914602d1bd1d1582cb3401c669c1121514f7ec3f963ac3f4`. All4,428 payload hashes/exact members and
1,345 included AOT inputs match, including original overlay states, scoped sampler integration and Im2D ownership.
Actual renderer capture hashes also match. Archived STATUS predates this pointer.
Verification: `build/im2d-upload/checkpoint181-verification.json`.

Previous build180 / actual muted boot139 passes **85/85 suites in 157.92 seconds**.
The full original Im2D CPU path now commits its state, performs a real native
draw on synthetic inputs, preserves exact packed rectangle pixels and depth,
and returns through its original override reset, cursor update and epilogue.
Native Im2D GPU tests pass14,825 checks on WARP and hardware; original driver
lifecycle passes48,754, including19 full-draw and188 shader-program checks.

Actual boot139 reaches the new native effective-state gate, then rejects with
`Original Im2D depth-write/expanded-blend/cull state is unqualified`.
It submits no Im2D game draw. Exact retained values need diagnosis. The launch
completes31 native presentations/25 original screen draws, but every display
reports occluded; there is no new accepted-display capture. Terminal owner
release is not proof of original normal shutdown. No menu/world/gameplay claim.

AOT:1,347 inputs,311 outputs,zero diagnostics;192 byte-matched hooks.
Executable SHA256: `e1689b5d29a6ab70699358cc04881f6b9349ab509f0a667bfec427960a6c5ad7`.
Evidence: `build/im2d-upload/build180-summary.json`, `build180-tests.log`,
`aot-manifest180.json`, `integration-evidence180.json`; docs/native-im2d-draw.md.
Next: diagnose retained state at the real draw and capture renderer readbacks
with explicit occlusion metadata. Preserve the original no-fake-success policy.

Previous verified checkpoint: `checkpoints/native-im2d-draw-139.zip`, SHA256
`14f05cdfeb0c1be344861449990bbd9af0d6ba7d3078ebecfb036dd38ba434b5`. All4,204 payload hashes/exact members and
1,344 included AOT inputs match, including native Im2D source/program/ownership integration.
Actual boot139 was occluded and produced no new capture. Archived STATUS predates this pointer.
Verification: `build/im2d-upload/checkpoint180-verification.json`.

Previous build179 / actual muted boot138 passes **84/84 suites in 145.60 seconds**.
The Im2D upload remains real native storage. New owned conversion decodes the
original28-byte position/ARGB/UV format into native input, and ahead-of-time
shaders implement the original flat/textured unlit screen-position branches.
These rendering pieces are independently qualified; the game draw stays guarded.

The original CPU shader builders pass170 checks, including full generated pixel
programs and all20 vertex options. Original frontend quad construction, rotation,
RGBA packing and upload pass1,272 checks. Native input conversion passes5,956;
native GPU shaders pass3,392 on each of WARP/hardware. Driver lifecycle passes
48,717. The existing1,481 upload checks remain intact.

Actual boot138 renders23 original loading draws/30 presentations, uploads112
bytes into slot2/native buffer00D00003, then fails explicitly at fixed-function
setup82408CC0, caller8240950C. Read-only inspection of the held failure verifies
five pending original requests for blending/alpha/depth; their commit remains
essential before drawing. The bounded inspector then terminated its own child.
This launch does not establish normal cleanup. Original UI global remains zero.

Next: preserve fixed-function binding lifetimes and original state commit, then
qualify alpha/blend/cull/pixel-center/target behavior at the real Im2D draw.
See `docs/native-im2d-shaders.md`, `docs/native-im2d-first-draw-state.md`.
ReAgent/Ghidra exports remain available with independently checked original
words; generic decompiler ABI assumptions are not accepted as implementations.

No complete menu, world, controllable gameplay, progression or saves are verified.
Console precision/filtering, display gamma/pacing and broader renderer/lifecycle
work remain unfinished. Original game/reference files remain unchanged.
AOT:1,346 inputs,311 outputs,zero diagnostics;189 byte-matched hooks.
Build: `.\tools\build.ps1 -Jobs 8`.
Run: `python -B tools/run_native.py --timeout 20 --log build/boot-NEXT.log --capture-frames build/captures/native-loading-NEXT` (muted; choose unused names).
Executable SHA256: `b81e26de7ededc18e2352d6d4878779af6b39a83e14ba442edd1fedebfd845bb`.
Latest inspected capture: `build/captures/native-loading-138/native-frame-0023.png`.
Frozen evidence: `build/im2d-upload/build179-summary.json`, `build179-tests.log`,
`aot-manifest179.json`, `shader-evidence.json`, `actual138-first-state.json`.

Previous verified checkpoint: `checkpoints/native-im2d-final-138.zip`, SHA256
`05699c99581caea1322c3a72a859b49fc232b21ff0fec37a9780e794f502070f`. All4,166 payload hashes/exact members and
1,343 included AOT inputs match, including native shader/decoder sources and
the inspected raw/PNG capture. Archived STATUS predates this pointer.
Verification: `build/im2d-upload/checkpoint179-verification.json`.

Previous verified checkpoint: `checkpoints/native-im2d-shaders-137.zip`, SHA256
`7d15a1b26583bf06aa876fb9fcfbef3f15d390a440db44b7a93e959b24cc61f0`. All4,098 payload hashes/exact members and
1,343 included AOT inputs match, including native shader/decoder sources and
the inspected raw/PNG capture. Archived STATUS predates this pointer.
Verification: `build/im2d-upload/checkpoint178-verification.json`.

Previous verified checkpoint: `checkpoints/native-im2d-upload-133.zip`, SHA256
`e8179c56bdd799362d2ef1e838247a8cb3a1ea0f4b3fa1b9d9d662229a3becdb`. All3,800 payload hashes/exact members and
1,343 included AOT inputs match, including the inspected raw/PNG capture.
Archived STATUS predates this pointer.
Verification: `build/im2d-upload/checkpoint176-verification.json`.

Previous verified checkpoint: `checkpoints/native-frontend-assets-131.zip`, SHA256
`c1dc14bea51fe1cb7a340710f3734f2ada9eb25b008d53a5bc1d82f5d2d801bc`. All3,635 payload hashes/exact members and
1,343 included AOT inputs match, including the inspected raw/PNG capture.
Archived STATUS predates this pointer.
Verification: `build/unbuffered-assets/checkpoint175-verification.json`.

Previous verified checkpoint: `checkpoints/native-original-screen-127.zip`, SHA256
`0cbfff508536fcd6d7173d2d5dae3651bd4705599ca719e4ce42b3fc21b04525`. All3,402 payload hashes/exact members and
1,343 included AOT inputs match, including the inspected raw/PNG capture.
Archived STATUS predates this pointer.
Verification: `build/original-screen/checkpoint173-verification.json`.

Previous verified checkpoint: `checkpoints/native-viewport-reset-121.zip`, SHA256
`480176f468c26b216c8cd8a5072a844558b084673e8213e05f45826d79bd20ad`. All3,291 payload hashes/exact members and
1,343 included AOT inputs match. Archived STATUS predates this pointer.
Verification: `build/viewport-reset/checkpoint170-verification.json`.

Previous verified checkpoint: `checkpoints/native-viewport-copy-119.zip`, SHA256
`f4dccdf9fd74ee589a7052305e031f181f6915a56b04afe2c40ab1968ac0a92e`. All3,258 payload hashes/exact members and
1,343 included AOT inputs match. Archived STATUS predates this pointer.
Verification: `build/viewport-copy/checkpoint169-verification.json`.

Previous verified checkpoint: `checkpoints/native-viewport-camera-passes-117.zip`, SHA256
`5f6c23c261fd8a39ef7d51b78bf0b4576b030b81e1770224437e4283b659b493`. All3,236 payload hashes/exact members and
1,343 included AOT inputs match. Archived STATUS predates this pointer.
Verification: `build/camera-selection/checkpoint168-verification.json`.

Previous verified checkpoint: `checkpoints/native-controllers-113.zip`, SHA256
`8bfe09c2aa21c3690be11e67bc260a5a3acac5e18b032f8360f4615307895369`. All3,204 payload hashes/exact members and
1,343 included AOT inputs match. Archived STATUS predates this pointer.
Verification: `build/native-controllers/checkpoint167-verification.json`.

Previous verified checkpoint: `checkpoints/native-content-enumeration-111.zip`, SHA256
`14b0bab834d03500fb9866a25625ae3bfb6088d42819d434f7f39b1f5df88b8b`. All3,180 payload hashes/exact members and
1,342 included AOT inputs match. Archived STATUS predates this pointer.
Verification: `build/content-enumeration/checkpoint166-verification.json`.

Previous verified checkpoint: `checkpoints/native-viewport-cameras-109.zip`, SHA256
`b20a59885bd5c10ac1502eb41415fd398f445d9bfbc0f6a1734f555604a3ae13`. All3,152 payload hashes/exact members and
1,340 included AOT inputs match. Archived STATUS predates this pointer.
Verification: `build/rectangular-camera/checkpoint165-verification.json`.

Previous verified checkpoint: `checkpoints/native-crossfade-texture-108.zip`, SHA256
`7c320d49ee68bfb9155bb5739a90205dbe0f043e15b018a8cba2659d710bc023`. All3,118 payload hashes/exact members and
1,340 included AOT inputs match. Archived STATUS predates this pointer.
Verification: `build/crossfade-texture/checkpoint164-verification.json`.

Previous verified checkpoint: `checkpoints/native-builtin-textures-106.zip`, SHA256
`00d77a96dbdac432c19af9ae97db7c613fd8bcc2e7c6f2a3ec8807a55d48a708`. All3,075 payload hashes/exact members and
1,340 included AOT inputs match. Archived STATUS predates this pointer.
Verification: `build/builtin-textures/checkpoint163-verification.json`.

Previous verified checkpoint: `checkpoints/native-builtin-decoding-105.zip`, SHA256
`9a46a02a52602d110bbf2a0e8e87c11c72a0146cb874d06a4f498f3e1150cea5`. All3,029 payload hashes/exact members and
1,339 included AOT inputs match. Archived STATUS predates this pointer.
Verification: `build/builtin-textures/checkpoint162-verification.json`.

Previous verified checkpoint: `checkpoints/native-reflection-lifecycle-104.zip`, SHA256
`dad38c71dfd611fa3cad0eefadba8301fddf390b7807f00065b50c83f77c30bf`. All3,000 payload hashes/exact members and
1,338 included AOT inputs match. Archived STATUS predates this pointer.
Verification: `build/reflection-cubemap/checkpoint161-verification.json`.

Previous verified checkpoint: `checkpoints/native-reflection-cameras-102.zip`, SHA256
`4c59f3c967ebf4c3ba0ab8c373a40c30e4f8896c1aab88e5d7c969142120aa38`. All2,973 payload hashes/exact members and
1,337 included AOT inputs match. Archived STATUS predates this pointer.
Verification: `build/reflection-cubemap/checkpoint160-verification.json`.

Previous verified checkpoint: `checkpoints/native-cubemap-backend-101.zip`, SHA256
`155ed5d2c07707d53825c0d243b2c0dffcf0e261405d69dd268706760f25e749`. All2,950 payload hashes/exact members and
1,337 included AOT inputs match. Archived STATUS predates this pointer.
Verification: `build/reflection-cubemap/checkpoint159-verification.json`.

Previous verified checkpoint: `checkpoints/native-cubemap-layout-100.zip`, SHA256
`95c974614cfbe55fad4af0de65e1254e846b55957fbaad730c330ac69a9358d8`. All2,935 payload hashes/exact members and
1,337 included AOT inputs match. Archived STATUS predates this pointer.
Verification: `build/reflection-cubemap/checkpoint-verification.json`.

Previous verified checkpoint: `checkpoints/native-effect-finalizers-099.zip`, SHA256
`26273d50d799ac7bdf149b2ddb8ccd5a156dac927c0978607dd1f556952899d1`. All2,917 payload hashes/exact members and
1,337 included AOT inputs match. Archived STATUS predates this pointer.
Verification: `build/effect-finalizer-integration/checkpoint157-verification.json`.

Previous verified checkpoint: `checkpoints/native-effect-finalizers-098.zip`, SHA256
`4ba24ad0ffb606b7cb32a94060818a5f9644f652374988aa89f9d144c204bd62`. All2,905 payload hashes/exact members and
1,337 included AOT inputs match. Archived STATUS predates this pointer.
Verification: `build/effect-finalizer-integration/checkpoint-verification.json`.

Previous verified checkpoint: `checkpoints/native-effect-reflection-gateway-097.zip`, SHA256
`3a172026820771e18b306d4b84db014d96c84b2b2bc7e812f09df4b2fe3c8349`. All2,885 payload hashes/exact members and
1,337 included AOT inputs match. The archived STATUS predates this
archive pointer. Verification: `build/effect-reflection-gateway/checkpoint-verification.json`.

Previous fully green checkpoint: `checkpoints/native-effect-reflection-096.zip`,
SHA256 `8e1c51a2b4893973227aa6949b55e3b433dc712a7c10a32a426d3de8cac7968f`.
All2,872 payload hashes/exact members and1,337 included AOT input hashes
match. This preserves build154's60/60 suites, actual boot096 and the native
reflection decoder plus both new fixture/proof sets. Archived STATUS predates
this pointer. Verification: `build/effect-reflection-all49/checkpoint-verification.json`.

Previous fully green checkpoint: `checkpoints/native-all-effects-095.zip`, SHA256
`170948309fff548437fb368203fdfe9f62755d7688943e72f99432dbdd3e8e41`.
All2,830 payload hashes/exact members and1,337 included AOT input hashes match.
This preserves build153's58/58 suites and actual boot095, plus completed first25
reflection evidence. Pending reflection-compatibility work is outside its path
selection. No original media or profile data is included. Archived STATUS
predates this pointer. Verification: `build/post-effect-catalog/checkpoint-verification.json`.

Previous recovery points follow.

Previous fully green checkpoint: `checkpoints/native-shadow-textures-094.zip`, SHA256
`20b2e97ab5db6320abbf9be0e3c1f01834e08d58f5fd195d65c3adfd1da5ff4c`.
All2,789 payload hashes/exact members and1,337 included AOT input hashes match.
This preserves build152's57/57 suites and actual boot094. Pending second-catalog
and reflection work plus the writable-texture document update were explicitly
excluded. No original media or profile data is included. Archived STATUS predates
this pointer. Verification: `build/shadow-texture-lifecycle/checkpoint-verification.json`.

Previous progress checkpoint: `checkpoints/native-shadow-cameras-092.zip`, SHA256
`50f2f8fb8726343bb72734a56f0dea280c9af747261d01741ecc53d60ef9f90a`.
All2,734 payload hashes/exact members and1,335 included AOT input hashes match.
This preserves build149's54/55 result and the known texture boundary. The two
pending camera-lifecycle/texture-contract documents were explicitly excluded;
the frozen tests, original evidence and run logs are included. Archived STATUS
predates this pointer. Verification: `build/shadow-camera-lifecycle/checkpoint-verification.json`.

Previous progress checkpoint: `checkpoints/native-effect-catalog-090.zip`, SHA256
`64fee7d47edd5e7f2285b880dcd9b383576c2beab9958bc7c42b85c591e3790f`.
All2,703 payload hashes/exact members and1,335 included AOT input hashes match.
This preserves the known53/54 result; it is not a green-build checkpoint.
Pending shadow-raster contract is excluded. Archived STATUS predates this pointer.
Verification: `build/effect-catalog-lifecycle/checkpoint-verification.json`.

Earlier fully green build146 (51/51 suites) recovery checkpoint:
`checkpoints/native-first-effect-089.zip`, SHA256
`0fdfa18eef458907aa06afa2125b9bd5d8e5d50454241920b9fa3a90a4e8f8b5`.
All2,577 payload hashes/exact members and1,335 included AOT input hashes match.
Original PE/XEX/switch-table inputs, game media and profile data are excluded.
The pending effect-catalog and first-effect caller artifacts are excluded;
completed build146/boot089 evidence is included. Archived STATUS predates this
checkpoint pointer. Verification: `build/first-effect-lifecycle/checkpoint-verification.json`.

Build100 / desktop boot060 completes two real RGB10A2 front copies, two accepted
native presentations, front-role/history rotation, and original buffer-ring
advancement. The unedited `build/captures/native-present-060.jpg` shows black
clear only; original geometry draws remain zero. Sandbox boots058/059 correctly
report occlusion rather than accepted presentation. WARP and hardware tests
verify all 1,024 RGB component codes and all four alpha codes through real GPU
and swapchain copies, completion queries, lifetime and binding preservation.
Original display gamma/pacing and scanout equivalence remain unverified.

All 51 CTest suites pass in build146: effective state 9,822 checks, original state
bridge 8,019, and original driver lifecycle 29,948. The first presentation and four
additional fixture presentations retain the original list splice and CPU tail;
history metadata survives driver restart while physical front IDs remain unique.
No original loading artwork, screen geometry, world or gameplay has run.
Boot066 completes original recording-manager constructor `826F4988`, caller
`823B748C`, with owner `E1A5B7C0` and native identity `00600001`. Its original
CPU pool/registration/prewarm remain AOT; only the SDK allocation call is replaced
by real D3D11 deferred-context ownership. The 2,000-item CPU pool is present,
both context aliases remain unchanged and main CAF8 stays zero. No SDK device
layout or console commands are constructed. Recording begin/end/playback remain
guarded; an allocated context does not claim executable recordings.

Three original constructor/destructor lifetimes pass, including both O and O+4
deletion routes, same-address stale-ID rejection, untouched O+60, CPU pool
cleanup, native release and rejection of stop while a manager is live. The
original deletion notification leaves the pre-render subscription unchanged;
production retains that CPU effect. The repeated-lifetime fixture explicitly
uses the original unsubscribe helper between cycles. Full application/global
event teardown remains unverified. Backend ownership tests pass 350 checks on
WARP and hardware. All 156 hook sites match the original executable bytes.
See `docs/native-recording-owner-design.md` and `docs/native-recording-backend.md`.

The executable also processes the original MSVC thread-name notification and
reads back `VfxCullStateManagerThread` from its actual native worker. Other
guest exceptions remain explicit failures. Native naming/readback and malformed
record rejection pass in the runtime suite. Boot068 creates two native system
notification listeners (handles140/144, mask1/version2), retains the original
profile constructor and starts original worker827AEFB0. Each listener has a
real queue and waitable event. No initial UI/sign-in events or accounts are
fabricated. Native delivery, filter, close, concurrent ownership and checked
output tests pass; actual UI/profile/input event producers remain unimplemented.
The original profile defaults mean no user selected, not a successful sign-in.
Its storage worker waits on its listener; normal manager stop/join remains
unverified, while terminal runtime cancellation quiesces native workers.
See `docs/native-notification-service.md` and `docs/native-profile-startup.md`.

Boot068 also creates the actual native mutex (handle14C, initially unowned).
The mutex suite verifies recursive ownership, thread handoff, abandonment,
non-owner rejection and close during the real native wait import. Original
application mutex teardown has not run. Boot068 reached `XMACreateContext`,
caller `8233E744`, output pointer `E4626120`, in original audio initialization.
Original decoding directly reads hardware-context fields. Build110 therefore
guards the verified EXm0 provider `8233E598`, caller `82816470`, before its early
initialized flag, physical pool allocations and 256-context loop. Actual boot069
confirms the flag, both pools, owner and free count are still zero at that stop.
No audio behavior is skipped or reported successful. See
`docs/native-audio-boundary.md` and `docs/native-mutant-service.md`.

Offline native FFmpeg decodes two original mono EA-XMA clips to finite nonzero
PCM with exact declared sample totals: 8,064 and 54,901. Both pass a separate
muted Windows XAudio2 output probe with ordered buffer completion. This verifies
available decoder/output capabilities; game codec integration, priming, signed16
quantization, stereo/layers, looping, seeking, mixing and audible fidelity remain
unverified. The complete EXm0 callback lifecycle must preserve original queues,
sample trimming and planar float output. Automated runs remain muted. See
`docs/native-xma-decoder-options.md` and `docs/native-audio-output-probe.md`.

Build115 adds the independently built native `SimpsonsAudio` decoder library
and passes 662,151 ownership/PCM checks. Both original mono clips match the
recorded raw hashes for XMA1 and XMA2, retaining 8,704 and 55,296 samples without
an EOF tail. Packet ownership, partial reads, actual backpressure, explicit
reset, concurrent independent owners and caller SSE floating-point controls
are verified. The owned codec build verifies its pinned source/patch and all
installed artifacts; the test checks the actual loaded DLL paths. Stereo and
other rates have factory tests, not original waveform proof yet. The original
BE16 converter separately passes 664 cases / 225,264 exact output samples,
including sparse DCBZ writes. See `docs/native-xma-codec-ownership.md`,
`docs/native-xma-codec-build.md` and `docs/native-audio-trimming.md`.

Build120 connects a real native EXm0 factory and unconfigured instance ownership
to the original provider, allocation and destruction paths. The lifecycle suite
now passes 78,610 checks and 133 actual allocation/free pairs in build121, plus explicit null
allocation fault/retry. It verifies exact CPU layouts, same-address generations,
retained native leases, 256-layer capacity failure cleanup, nonvolatile GPRs,
and real concurrent VM mapping changes. Review found an unlocked region-vector
scan; it is now protected by vmMutex. Input enqueue fails before publication,
and the complete reviewed hardware-context/decode family stays guarded.
No configured game decoder or audio playback is claimed. See
`docs/native-exm0-ownership.md` and `docs/native-exm0-ownership-review.md`.
Build121 also verifies that the actual registered P6B0 descriptor passes through
the generic constructor/destructor with one real allocation/free, its full CPU
byte footprint intact, and no change to native EXm0 ownership.

Actual muted boot075 completes original Dac0 construction and starts the real
`RWAudioCore Dac` worker. Its real Windows source is configured and active:
S=E4624C30, Q=E4627DE0, mixer=E4048000, native source00700001, eventE1A66720,
worker43352, endpoint2 channels/mask3, muted. Original CPU buffers, descriptors,
event, graph registration and worker remain AOT. No SDK singleton/voice layout
is fabricated. Its next explicit failure is the KeTimeStampBundle data read
in824324A8, caller8232DEA8. Original geometry draws remain zero.

The OriginalDacLifecycle suite passes13 checks using the actual original worker,
native DSP, original callbacks and real downstream completion. It then invokes
the original root destructor, executes graph-driven Dac deletion on its worker,
and joins that actual OS worker from the main root destructor before root free.
The EXm0 suite passes78,619 checks/133 allocation-free pairs, including early
source observation and full0x120-byte entry-stack preflight. Optional diagnostic
observers are empty in production. Terminal failure cleanup also joins workers
and drains native callbacks before releasing COM and guest memory, but does
not claim original graph cleanup. Whole-app normal teardown and decoded game
audio remain unverified. See `docs/native-dac-integration.md`; the earlier
prefix-only checkpoint is described in `docs/native-dac-construction.md`.
The new OriginalDacPcm suite executes the original interleave, clamp and fade
helpers in 20 cases / 23,040 exact sample comparisons, including page guards,
IEEE corner cases, complete store footprints, ABI and host FP restoration.
Only the six-channel interleave and finite fade are qualified; this is no output
device or speaker-routing proof. See `docs/native-dac-pcm.md`.
Build122 also verifies the new owned BE-float transport over27,648 samples,
including signed zeros, subnormals and preserved NaN bits. It changes only byte
representation, with no extra channel permutation or clipping.

The independent `SimpsonsAudioOutput` library now owns a real Windows XAudio2
engine, mastering/source voices, wake event, copied256-frame PCM slots and
generation/sequence receipts. Its CTest passes594 scheduling-dependent checks
and54 natural completions in fixed batches. Tests cover stale receipts, bounded
admission, callback errors, concurrent shutdown and reopened lifetimes; all are
muted. The game uses its real configured source through a separate bounded DSP
owner. Logical source capacity is two, with four downstream Windows buffers;
source consumption follows complete DSP and real downstream acceptance, while
natural output completion has a separate receipt. NativeDacProcessor passes
12,439 checks,12,288 exact component samples and9 actual OnBufferEnd receipts.
The extra Windows buffering is an explicit latency adaptation. See
`docs/native-audio-output-ownership.md` and `docs/native-dac-processing.md`.

SDK analysis proves activation, two-descriptor capacity, borrowed PCM submission,
processing/completion callbacks and cancellation on final release. It also finds
category gain and a separate gain ramp; immediate host SetVolume alone is not
that contract. Dac destruction can run on its own audio worker while processing
the original graph-command queue. It must not blindly join itself. Startup
holds Q4C until828166F8, so constructor interception is too early for a normal
worker-join fixture. See `docs/native-dac-sdk-contract.md` for the later safe
observation proposal, original numeric route and unresolved speaker/category policy.

The OriginalDacGain suite now executes41 original calls and compares31,176
samples exactly with21 independent integer-oracle anchors. It establishes the
scalar ramp denominator, rounding, partial progress and actual dispatcher path.
Original Dac PCM alignment selects the scalar fallback. First activation seeds
current gain from requested gain; later changes ramp without forcing the last
sample to the target. The native scalar helper is now integrated into the output
library and matches the complete original output/progress footprints. It passes
53 processed calls,29 rejection cases and3 no-work cases, including arbitrary
caller rounding/FTZ/DAZ with exception masks clear. Rejections leave output/state
unchanged. The integrated source applies this helper during DSP. The original
source uses256-frame processing passes; starvation separately snaps current to
target and zeros the remaining planes. Baking gain at packet acceptance would
not establish the verified processing-time behavior. See `docs/native-dac-gain.md`.

Additional routing evidence verifies535 words across9 original spans. The sole
direct speaker-query caller selects hardware-summary text, not a Dac matrix;
the default-send helper has no six-to-two branch. The original platform supplies
later category multipliers, applied to requested gain. Physical labels and final
console stereo downmix remain unresolved. The native source explicitly maps
components to Windows FL/FR/FC/LFE/SL/SR and uses the actual default endpoint
matrix read back from XAudio2. Windows session/endpoint volume is the PC volume
policy. These choices do not claim recovered console speaker/category behavior.
See `docs/native-dac-routing.md` and `docs/native-audio-windows-routing.md`.

The owned codec also passed six original stereo-layer probes, including a
complete stereo SNU, two layers from a bounded four-channel loop-body excerpt,
and three layers from a complete six-channel MUS stream. All18 native runs
matched between raw variants and partial-read schedules. Speaker routing,
loop replay and hardware numerical equivalence remain unproved. See
`docs/native-xma-multilayer-probe.md`. The EXm0 unbuffered caller ignores short
decode returns: full-quota staging, source leases, trimming and numerical
conversion must be implemented before its decode/input guards can be removed.

Build133 replaces only the verified millisecond-clock leaf824324A8 with real
Windows uptime narrowed to32 bits. Its906-check suite executes both original
biased-clock wrappers and retains the raw KeTimeStampBundle import guard.
Actual muted boot076 passes that clock read and creates a further original
worker; it then fails on notification subscription mask20/version2, caller
8280A17C. Original deadline arithmetic and its wrap limitation are retained.
See `docs/native-millisecond-clock.md`.

Build134 adds the exact audio-category5 subscription, mask20/version2, to the
real notification queue. Original consumer8280A780 filters0A000003; an empty
poll preserves the original O+4C state. No external-music event or controller
state is invented. Its existing original constructor initializes O+4C=0.
The extended notification suite passes, and actual boot077 creates listener158
before two more original workers. Optional console music-control requests and
complete original owner cleanup remain unverified. See
`docs/native-notification-audio.md`.

Build135 implements RtlInitAnsiString's real borrowed descriptor contract.
Its85-check suite compares counted lengths and saturation with actual Windows
ntdll and verifies the original BE layout, bounds and void ABI. Actual muted
boot078 passes that call, then reaches ExGetXConfigSetting(category3,setting9)
at82432D3C, caller8282AF00. Successful external asset reads are still unverified.
See `docs/native-ansi-string.md`.

Build136 replaces the verified no-argument language wrapper82432D10 with a
real Windows user-UI-language query mapped to the original twelve language IDs.
Original name and locale lookup remain AOT; this USA locale table falls back
to its English entry for unavailable translations. The65,681-check suite covers
the original name table, actual AOT lookup, Windows mappings, ABI/FP and retained
failure for the raw ExGetXConfigSetting import. Actual muted boot079 passes
language selection and reaches XamUserGetSigninState for slot0, caller823A1254.
See `docs/native-configuration.md`.

Build136 executable SHA256, actual muted boot079:
`7db75bf6088c70f664307f5fa54cf70454b9a7b8f55e7902b58a115cc2076e9f`.
Logs: `build/hundred-thirty-sixth-build.log`, `build/boot-079.log` and
`build/native-configuration-136.log`. Total CTest time53.09 seconds.

Build137 adds the native XMA source assembler to the normal audio library. Its
402,079-check suite compares397,693 exact raw samples from six pinned original
layer streams, including XMA1/XMA2 and mono/stereo/six-component output. Owned
packet copies, complete immutable planar quotas, explicit skip accounting,
continued input, bounded backpressure, stale receipts, cancellation and a
nonallocating commit preserve source and output lifetimes. This is native
RawF32 preparation; live original source admission, guest output publication
and configured EXm0 teardown still require their verified bridges. Runtime
input/decode guards remain. See `docs/native-xma-source.md` and
`docs/native-exm0-integration-next.md`. Full build137 passes40/40 suites in53.19s;
logs are `build/hundred-thirty-seventh-build.log` and
`build/native-audio-verification-137.log`.
Actual muted boot080 of build137 reaches the same slot0 sign-in query at
823A1254 and terminates its workers on the explicit unsupported import. Its
executable SHA256 is
`884f51d634f890781684b0604d6b6c481fa93f8059826b9144255946f5755702`.
This verifies the rebuilt executable's startup state; no new pixels or live
game audio have been demonstrated. See `build/boot-080.log`.

Build139 implements real durable native local profiles and four session slots,
the qualified SigninState import, explicit command-line create/list/activation,
and notifications after actual activation/sign-out. Normal sessions start empty;
stored GUID identities are native profile IDs, not Xbox credentials. Original
game-player association and selection remain AOT. Build138 exposed a real
rename sharing violation; exclusive final creation, complete write/flush and
close now publish readable records while keeping the directory lifetime pins.
No overwrite or silent repair is allowed. The owner passes931 checks, the
actual original query-loop/ABI/notification bridge199, and real cross-process
CLI persistence passes. All43 suites pass in55.31s. See
`docs/native-local-players.md`, `docs/native-local-player-bridge.md`,
`build/hundred-thirty-ninth-build.log` and `build/native-local-player-139.log`.
Build139 executable SHA256:
`e13cf2adebdaa12273ade06772f41c7adbfddcd4420c4e2a0bf12fc48e83c110`.
Both actual muted boots081/082 reach8284BD30 with r3=FFFFFFFF, caller8284C0C8;
the active-profile boot uses an isolated temporary stored fixture, removed after
process exit. No further profile, external game-file read or rendered-art claim.

Build140 changes recovered switch dispatch to the low32 selector used by the
original CMPLWI/table address sequence. It preserves full GPR arithmetic and
the actual table load/CTR. Dense/sparse context/local-register regressions pass,
and the actual original8284BCF8 terminal case passes18 checks, retaining
r11=100000000 while returning its original287 token. All44 suites pass in54.15s.
Actual muted boot083 passes8284BD30, then reaches unsupported RtlNtStatusToDosError
in original82433B98 with r3=C0000034 on the file worker. Its original wrapper
conditionally stores the resulting DOS error in the original thread object;
that CPU/TLS behavior remains AOT. See `docs/native-word-switch.md`,
`build/hundred-fortieth-build.log`, `build/native-word-switch-140.log` and
`build/boot-083.log`. Executable SHA256:
`d3b89075d7b3affe8dbf4918dfc32d894a566f420444d0d4b79327584d3173a5`.

Build141 maps NTSTATUS through the actual Windows RtlNtStatusToDosError routine,
preserving import context/host FP and leaving original conditional thread-error
writes in82433B98. The246-check suite covers native mappings, actual AOT write
and suppression branches, cancellation and ABI. File-open diagnostics retain
the real request/result. All45 suites pass in54.51s; boot084 records
`D:\cmd.txt`, access80100080, share1, options60, statusC0000034 and continues
past its error handler to guarded graphics initialization826B0DF8. This is not
a successful game-file read or a rendered-frame claim. See
`docs/native-status-mapping.md`, `build/hundred-forty-first-build.log`,
`build/native-status-141.log` and `build/boot-084.log`. Executable SHA256:
`e861c833b4901535c4dd3db789a9fc71942b794d8519f4c1b97ad99456cba77f`.
That generation had311 files, zero semantic diagnostics and263 explicit
unsupported imports; all142 existing hook pins still matched.

Build142 retains original826B6F60/8271BD10 CPU construction and moves the guard
to82701B70 before effect registration. The preflight validates caller/driver
identity; original allocations, arrays, VMX matrices, null branches and globals
remain AOT. Two paired empty lifetimes pass605 checks. Independent evidence
checks1,154 original words plus54 later-service spans/45 calls/25 effect rows.
Actual boot085 publishes managerE1A9C600, secondaryE1A9C910, childE1A9C998,
native context00900001; first_effect is zero. No SDK device layout is created.
See `docs/native-graphics-cpu-startup.md` and
`docs/native-graphics-startup-services.md`.

The integrated original audio reader lifecycle passes556 checks: two groups,
eight managers,34 real allocations/matching frees, actual address reuse,
command enqueue, deferred cleanup on the real Dac worker and normal root/OS
join. It exposed and corrected a fixture lock prescription: Q48 is held for
the worker lifetime; use original Q40/Q44 mutant callbacks through the root
wrappers. This is an empty-reader lifetime, not production source/copy admission.
See `docs/native-audio-reader-lifecycle-test.md`.

All47 suites pass in63.22s. Build log `build/native-graphics-cpu-142.log`,
actual muted run `build/boot-085.log`. Executable SHA256:
`caa574b0421a9bcb7d88a930a1c2fa6924981053a92a5f46d52886a9f3a7c827`.
That generation had311 files, zero semantic diagnostics,263 explicit missing
imports and143 byte-pinned hooks.

Build143/boot086 retained the real empty FX wrapper/name construction and
proved its shared pool is nonnull. Build144 introduced native metadata/cache
ownership and exact typed queries. A temporary absence of Windows audio
endpoints caused seven real XAudio2 startup failures; desktop access did not
remove that environment failure. After endpoints returned, six passed and
the driver fixture exposed a partial-start rollback regression. Build145 fixed
the empty FX cleanup precondition and added real native shader artifacts.
One resource-bridge test still expected the two newly supported shaders to
fail preparation; build146 updates that contract and passes the full suite.

The native owner copies/pins the complete3,244-byte first blob, parses its
original names/handles/defaults, and retains both independently owned shader
records/artifacts. Original wrapper/typed/cache allocations, registrations,
finalizer stores and paired destruction remain AOT. Native pool leases protect
the real original CPU root; SDK reference count stays1 under this explicit
native ownership policy. Pool construction provenance and all four verified
retirement routes are checked. Normal first-effect release invalidates native
IDs before the original cache/name/object frees. Full CRT shutdown is unverified.

The shader proof covers18 scheduled original instructions and32 semantic
checks. WARP and RTX3080Ti each pass14 draws/4,160 RGBA/depth comparisons
with independent fixture inputs; this does not establish a game draw. The
material registry owns22 pinned identities; six native shader bodies compile,
while16 startup records still reject preparation. Declaration/stream association,
texture/constants and inherited effect state still gate application.

Build146 passes51/51 suites in75.73s. Original graphics CPU lifetimes now pass
635 checks; the first-effect fixture passes1,831. Actual boot089 publishes
W=E1A9C9F8, nativeFX00500001, cacheE1A9CA30 and two native shaders, then
rejects row1 source820D5730/W=E1A9CBF8 at826B4B88, caller82701A94.
Logs: `build/native-first-effect-146.log`, `build/boot-089.log`.
Executable SHA256:
`3cfbf5765773542e533e990f48504ef645b705fcf5825c3655100e8baba70876`.
Current generation:311 files, zero semantic diagnostics,263 explicit missing
imports,156 byte-pinned hooks and1,338 input hashes. Next build147; next boot090.
See `docs/native-first-effect-owner.md`, `docs/native-first-effect-contract.md`,
`docs/native-first-effect-lifecycle-test.md` and `docs/fourtap-shaders.md`.

The complete application state passes still cover 82 registered scalar fields
and twenty sampler categories at sixteen stages, retaining cache suppression,
forced updates and all eight saved-state frames. The later pipeline reset and
direct updates correctly change native effective state independently of those
CPU caches. See `docs/native-application-state.md` and
`docs/native-application-sampler-bridge.md`.

The requested **ReAgent/Ghidra trial** now works for offline evidence export and
planning: six original functions, 763 byte-checked instructions and 75 checked
direct calls. A verified inline save-helper definition fixes Ghidra's incorrect
state-owner inference at `82723D80`; explicit annotations preserve missing-call
and ABI limitations in ReAgent packets. See `docs/reagent-ghidra-probe.md`.
No model-generated candidate has been accepted or executed. ReAgent's full
reversal gate remains closed until a candidate-specific validation harness is
configured; this does not block ordinary port work. That offline trial preceded
the native application-state changes above.

## Commands

Run from `K:\SimpsonsNativeCopy` in PowerShell:

```powershell
python -B tools\build_native_audio_codec.py --jobs 4
.\tools\build.ps1 -Jobs 8
python tools\run_native.py --timeout 20 --log build\boot-next.log
```

`build.ps1` selects the installed Visual Studio x64 environment, ClangCL and SDK
resource compiler, builds the offline generator, derives and checks the exact
game image, regenerates translation, verifies input/output hashes, builds and
runs CTest. The default tool source snapshot is already in `third_party`.
XexTool is currently expected at `K:\XexTool_v6.3\xextool.exe`; its hash is recorded.
Use `-SkipGenerate` only when the AOT manifest still verifies. The ordinary build
without `-Diagnostic` rejects reported unresolved translation semantics.

The actual executable is `build\native\SimpsonsNative.exe`; direct invocation:

```powershell
.\build\native\SimpsonsNative.exe --image .\analysis\simpsons.pe
```

All current runs are muted. The bounded Python launcher terminates only its own
child on timeout and records that outcome as a failure, not a gameplay pass.

## Verified progress

- XEX identity and 34,176 `.pdata` entries, import descriptors, helper signatures,
  PE section layout and PDB identity recorded in `analysis/executable.json`.
- Normalized generator input reproduces the independently extracted 15,466,496
  byte image exactly; input game files are untouched.
- Identified and excluded the **metadata-declared export table** from code
  discovery; no instruction skipping was used to hide those decode errors.
- First native executable passed memory/endian/bounds/MMIO/indirect-call and
  host rounding checks, then executed original `0x82432280`.
- `build/boot-001.log`: explicit failure reading unimplemented
  `XexExecutableModuleHandle` from original startup `0x824340C0`.
- `build/boot-002.log`: native loader/header lookup passes; next reached failure
  is `NtAllocateVirtualMemory`, caller return `0x82436F74`.
- `build/boot-003.log` through `boot-005.log`: the actual original CRT now passes
  native virtual reserve/commit, PCR/process-type and critical-section setup.
  `build/boot-006.log` passes the privilege check and original CRT TLS setup.
  Logs 007â€“010 progress through actual memory statistics, the engine's
  `0x1E2B0000` physical arena, its first semaphore and performance-frequency
  setup. Logs 011â€“012 pass native window/video-mode queries and timer creation.
  Logs 013â€“016 progress through native thread creation, typed thread references,
  priority, resume and delays. The game creates two actual native workers.
  Logs 017â€“020 pass worker affinity/priority, native UTC time and physical page
  protection. Through boot023 the actual failure was console graphics initialization:
  `VdInitializeEngines` from `0x82466E80`, function `0x82466E48`.
- The ninety-first native build passes twenty-four CTest suites: native memory,
  actual instruction emitter, original-asset filesystem, native thread lifecycle,
  D3D11 resource upload/readback, native screen shader rendering, and original
  native texture struct decoding/upload, transactional startup resources,
  material ownership, original screen shader evidence, and real native material
  artifact creation, original vertex-declaration ownership, effective engine
  state, original state/resource ABI bridges, dynamic buffer ownership, original
  pipeline resource ownership, the actual original driver lifecycle, and bounded
  expanded-blend evidence, native viewport set/query, and post-start integration
  evidence, camera-raster evidence, loading-texture evidence and host FP control
  at native-to-AOT boundaries. The state bridge
  passes 8,019 checks, including actual original AOT setter comparisons and
  every intermediate normal/Z filter update across all Boolean combinations.
  The driver fixture passes 4,357 checks, including all eight original saved state
  frames, sixteen sampler stages, forced updates, invalid-owner rejection and two original engine
  start/stop/close cycles, partial-start rollback/retry and native submission
  registration, all eight real GPU clear selectors, original camera begin/end
  reuse, matrix-pool copies, active-camera destruction rejection, 67-field
  reset, unchanged-mode wrapper push/pop, and original CPU fog/depth queue
  order, capacity and deferred-application behavior. Full application
  teardown/restart remains unverified.
  Generation reports zero semantic diagnostics across 303 translation units and
  77,663 mapped native functions, including callable import entries.
- Three byte/halfword-offset switches recovered from original instructions and
  table bytes. Explicit evidence is checked in `config/switch_overrides.toml`.
- Asset inventory: 7,968 files / 4,385,078,597 bytes. Validated outer stream,
  compressed-resource and media structures are documented in `docs/assets.md`.
- All 20 StreamTOCs parse; 470 references cover all 490 stream files, and 2,656
  block descriptors match. Exact frontend StreamTOC and ITXD payloads extract
  reproducibly without modifying source files.
- Frontend ITXD inspection identifies 15 textures (DXT1, DXT2/3, L8). All base
  levels now decode reproducibly to PNGs and have been visually inspected.
  Twenty-seven inspector tests and 22 decoder tests pass; original GPU rounding,
  alpha association and non-base mips remain unverified. See texture-decode.md.
- Recovered original loading quad ABI, camera/driver callback slots, and the
  actual entry-to-console-GPU-init call chain; see `docs/render-boundary.md`.
- Decoded the distinct embedded `frame1/frame2` Itchy/Scratchy loading textures.
  Five focused tests and exact artifact reproduction pass. Original stream-loader
  instructions independently prove the linear source rows and separate tiled upload.
- Fixed independent physical alias protections and selected-aperture page sizes.
  Fixed thread slot exhaustion with first-fit reuse, ownership-aware reaping and
  transactional creation rollback. Lifecycle tests run 512 sequential and 128
  concurrent AOT workers on real native threads.
- D3D11 resource backend tests pass with WARP and the hardware adapter at feature
  level 11.1. Native allocation is now connected to engine request 2; no game
  frames have rendered. Latest actual game run is `build/boot-038.log`.
- The four original screen shaders have complete bounded instruction proof and
  16 byte/field tests. Offline-compiled native equivalents pass offscreen pixel
  checks on WARP and hardware: RGBA arithmetic, quad orientation, four blend
  equations, alpha threshold, masks, repeat and bilinear filtering. Original
  effective startup state is now tracked; target/viewport integration and expanded
  blending precision still gate game rendering. See native-graphics.md.
- Verified full-context generator hooks pass nine emitter/configuration tests.
  Actual boot023 reaches the engine request hook, records all 35 live plugins
  in an original `0x5B4` engine allocation, and preserves the original GPU
  initialization failure. This observer returns no fabricated success.
- Boot024 replaces the console start path at the verified engine boundary with
  actual native allocation of six target roles and five buffers, plus HWND
  swap-chain attachment. Boot025 also executes the original binding/cache/raster
  CPU initializers and their teardown. Boot026 additionally executes
  the original scratch initializer and cleanup, creating native index and
  declaration resources through verified callsite/engine hooks. Native backing
  rolls back; console device and started flag remain zero, engine state remains 2.
- Boot027 ports mixed state initialization `824008E0` and commits the original
  scalar/stage queues through validated native state and original CPU pipeline
  helpers. Boot028 additionally creates all four dynamic pools through the real
  original allocator, publishes four native VB owners in real original linked
  records, then releases them through the original callbacks. At that milestone
  persistent driver ownership and target publication still gated plugin execution.
  See native-state-bridge.md and
  native-dynamic-buffers.md.
- Boot029 also binds the actual default native color/depth targets before the
  original CPU initialization and clears native bindings during unwind. Target
  binding queries verify D3D11 retained the requested references; WARP and hardware
  reject incompatible dimensions, duplicate backing and foreign-device resources.
- Boot030/031 retain the native driver, publish six checked native target IDs,
  preserve the original presentation fields, and complete the original 35-plugin
  construction path, gamma tables and request 17. The actual engine reaches
  lifecycle 3. The next explicit failure is unported post-start integration
  `823EE8F8`, called from `82875D64`. Console device `CAF8` remains zero and no
  game frame renders. See `docs/native-driver-lifecycle.md`.
- Boot032 retains the original post-start binding reset, returns a checked native
  backend identity, and executes both original CPU allocations. Boot033 connects
  native submission registration at `82875E20`, preserving original allocation
  effects, identity publications and application initialization. It reaches the
  first camera raster creation: `823F7070`, flags 2, 1280x720, depth field 0,
  extension offset 34. That unsupported callback now stops execution. Caller
  storage is retained until runtime teardown; its original early release remains
  unproved. See `docs/native-submission-bridge.md`.
- Boot034/035 create both original 1280x720 startup camera rasters through the
  original wrapper allocations and plugin lifecycle. Color type 2 retains a zero
  extension handle; depth type 1 borrows the native default-depth identity and
  consumes the original shared flag. Real original list nodes are retained.
  The 277-check driver fixture now covers actual camera destruction, untouched
  metadata, dynamic extension offsets, failed CPU allocation rollback, all eight
  bound-stage rejection paths and live-raster protection against early driver
  stop. Preflight precedes the original reverse plugin-destructor traversal.
  Actual execution stops at loading texture raster flags 384; no game frame
  has rendered. See `docs/native-camera-raster-bridge.md`.
- Boot037/038 load both original embedded frame1/frame2 textures through the
  original memory-stream, raster/texture constructors, EA2F plugin and dictionary
  insertion path. Each owns an actual immutable D3D11 BC3 texture. Native GPU
  readback matches every decoded source block in the driver fixture. Original
  dictionary destruction releases guest wrappers while separately owned native
  references retain GPU resources until their own release. Exact source/chunk/
  extension preflight, failed foreign-device attachment rollback, detached
  texture cleanup and immutable lock/unlock guards are verified. No game pixels
  have rendered. Boot038 stops explicitly at application state dispatcher
  `82723D80`, caller `8272470C`, selector 6, value 0, force 1.
  See `docs/native-loading-textures.md`.
- The native viewport service passes 150 checks on WARP and hardware, using
  actual D3D11 set/query state. Original reversed depth endpoints remain outside
  this host API until a verified draw transform handles them.
- The material owner preserves all 20 verified original shader records with
  explicit uncompiled/compiled/unsupported states, reference counts and stale-ID
  protection. The four screen records create real D3D11 shader objects from
  offline bytecode; the 16 untranslated startup records fail explicitly on bind.
  Original shader create/release engine entries are now native hooks with checked
  ABI/output/stage/lifetime behavior. Optional creation-veto callbacks are guarded.
  Their 210-check integration fixture also executes original scratch/declaration
  ownership and checks explicit SDK-binding rejection. Actual plugin creation
  now runs; shader binding/rendering remains unimplemented.
- Captured the actual desktop window using Windows Graphics Capture and tested
  its close button. Its 1280Ã—720 client area is blank; no original pixels have
  rendered. `boot-011-desktop-retry.log` records the failed execution held for
  inspection. This is window-service verification only.
- The installed IDA 9.3/Hex-Rays PPC works headlessly on a bounded, correctly
  mapped derived-image database. Four functions exported pseudocode; 646
  instruction words and 73 direct-call targets match the independent decoder.
  Raw pseudocode has documented guest-ABI/type/stack errors and is navigation
  evidence only. ReAgent currently requires Ghidra; this direct IDA probe needs
  no ReAgent installation or additional model calls. See `docs/ida-analysis.md`.

## Current implementation and remaining limits

Native reserve/commit, initial PCR/static and dynamic TLS, executable-module
header lookup, recursive/contended critical sections and executable privilege
lookup are implemented. Privileges come from the original XEX system flags.
Native memory accounting enforces a 512 MiB compatibility budget. Physical CPU
aliases share checked backing, with real allocate/free accounting. Native
semaphores support counts, waits, release and handle lifetime. Callable import
cells resolve to their verified AOT thunks. See `docs/native-memory.md`.
Native timer objects support set/wait/cancel; guest timer APC callbacks remain
explicitly unsupported. The native window has its own responsive message pump,
fixed initial client extent, and a 60 Hz presentation policy for the US profile.
Generator memory/VMX/FP/branch/trap/barrier corrections have focused emitted-code
tests. All 21 implemented integer saturating vector add/sub/pack forms now update
SAT; modulo pack aliasing is fixed. Reservation-granule and FP conversion/general
FP/exception semantics remain incomplete; see `docs/existing-work.md`.
Zero reported translation diagnostics does not certify instruction fidelity.

Native workers now have separate PCR/TLS/stacks, execute original AOT startup,
and propagate failures into cooperative cancellation and joined shutdown.
Native handle references, priority and delay services have focused execution tests.
Read-only original asset I/O is integrated and has standalone ABI/path/lifetime
tests. Verified volume queries return native size/free-space data. Original
startup has not yet reached successful external game-file reads in this executable;
the embedded loading dictionary now passes through original memory-stream reads.
Remaining imports, additional synchronization, asynchronous asset I/O, audio,
video, renderer and user input still require implementation. The application
now links the native D3D11 engine backend and no runtime instruction decoder.
Existing `K:\Simpsons` graphics code processes console GPU commands and is not
part of this application. Genuine engine mesh/material draw contracts remain
to be recovered. Full gameplay, saves, stability and performance are unverified.

`ealogo.vp6` has a 512-byte unparsed trailer in the original data; the asset
inspector reports it explicitly. This is not evidence of a damaged file.

Next concrete step: qualify the remaining25-row effect catalog and implement
the next `littextured` profile, blob820D5730, including shared pool/parameter
ownership. Keep the production loop intact and unsupported profiles explicit.
For the first effect, finish the original caller, declaration/stream, texture,
constant and inherited-state contracts before enabling application or drawing.
Later environment/cube/declaration/crossfade dependencies are recorded in
`docs/native-graphics-startup-services.md`. Keep native
local slots distinct from original selection/association; identity/name/settings,
SigninInfo, native chooser and save/content adapters remain unsupported.
EXm0 instances are now explicitly unconfigured native owners. Qualify original
384/512 trimming, source leases, explicit native PCM policy and full-quota staging
before enabling input/decode; stock whole-file trimming must not be applied
again. NativeRawF32 is a valid native decoder policy; exact console BE16
quantization is an optional fidelity comparison, not a prerequisite for that
policy. The new45-prefix analysis supports bounded quota preparation and
identifies the live source/transaction/teardown work still needed. See
`docs/native-exm0-integration-next.md`.
The source admission and actual reader-group ring allocation/free are now
pinned in `docs/native-exm0-admission.md` and
`docs/native-audio-reader-lifetime.md`: four0x64000-byte rings reside in one
0x1900B0-byte original allocation; individual release and bulk reset both require
native copy exclusion. The actual empty reader-group lifecycle now passes556
checks; production claim/copy/configured-owner bridges remain.
Native UI, local-profile and
input event sources also remain;
do not inject unverified startup events from other games. Original recording
ownership is now present, while recording
begin/end/playback and screen drawing remain explicitly guarded. Presentation
now uses immutable physical target IDs with independent rotating front roles,
real completion and native SDR output. Broader gamma/output equivalence,
scaling and original pacing remain unverified.
Then connect the screen material pass using `docs/native-screen-bridge-boundary.md`
and `docs/native-screen-raster-policy.md`. The recovered mode 1/guard 1 mapping requires
no position or UV shift, but arbitrary fractional quads still need a precision
gate. The original alpha early-out and AOT clip-coordinate arithmetic must
remain executed. Screen drawing remains guarded; native presentation now runs. Startup
color is proven 10:10:10:2 UNORM;
depth is 20e4 float plus stencil, with exact-value clears supported and general
depth-write quantization still guarded. Expanded blending preserves the same
packed storage, but its exact arithmetic is unproved; selectors 0â€“2 remain
gated. See `docs/native-expanded-blend.md`.
The console GPU engine import remains unsupported and is no longer reached
by native request 2. Driver start now reports success only after its required
native backing and CPU services are present; original engine readiness still
depends on the original plugin/gamma path. Unported graphics callbacks fail
explicitly, and terminal fatal cleanup is distinguished from normal paired stop.

Recoverable native loading-texture checkpoint (build066, boot038):
`checkpoints/native-loading-textures-038.zip`, SHA256
`e3d407614afd6a49ca023f7792530554d8c99c2209232a63ac4c239876d6cf4c`.

Subsequent offline ReAgent/Ghidra evidence checkpoint:
`checkpoints/reagent-ghidra-probe-039.zip`, SHA256
`e88dee447de30036025765d3762b6af22c19c01ad1dac78d36e8fd6c2a701c2a`.
This is analysis checkpoint 039, not a new native boot milestone.

Earlier native application-state checkpoint (build073, boot044):
`checkpoints/native-application-state-044.zip`, SHA256
`e1affb78849043627cd134fdb7d2f39fcb59fa62e31ead18af3b190db24215fb`.

Earlier complete application-state checkpoint (build079, boot047):
`checkpoints/native-application-state-047.zip`, SHA256
`9b80de861f807bdb37d6d17468d35eec32e4be9b6fac41f957de4252974194b2`.

Earlier native camera/state checkpoint (build091, boot055):
`checkpoints/native-camera-state-055.zip`. It contains the authored changes,
evidence, generation manifest, full build logs and extracted passing lifecycle,
state-bridge and host-FP test logs. SHA256:
`3882b1bf0fa41664bb33894bfbc57ec864d04331bffa2c6573d5ddcc04b0508b`.

Earlier native binding-reset checkpoint (build098, boot057):
`checkpoints/native-binding-reset-057.zip`. Includes authored sources, evidence,
generation manifest, full build logs, and extracted passing lifecycle/state/
host-FP/graphics logs. All 1,782 archived entries were hash-verified. SHA256:
`5ad59c8462850c778b1b253f0a0fa0465b7a55445186ed93f2643029a169f0ef`.

Earlier native presentation checkpoint (build100, desktop boot060):
`checkpoints/native-presentation-060.zip`, including the unedited window capture,
build/boot logs, presentation evidence and passing test logs. All 1,797 archived
entries were hash-verified. SHA256:
`a1b170c0d83d3a63374b0fb62c903bb0baf72764f01642ae7e89eedb708452f9`.

Earlier native recording-boundary checkpoint (build102, boot062):
`checkpoints/native-recording-boundary-062.zip`, including the prior unedited
desktop capture, verified recording ownership backend, constructor guards,
original consumer/lifetime evidence, and full build/test/boot logs. All 1,812
archived entries were hash-verified. SHA256:
`526f92e9152a8d0000a05c0d02dc54614354c2ef0a34880501027e54a44513f2`.

Earlier native recording-owner checkpoint (build106, boot066):
`checkpoints/native-recording-owner-066.zip`, containing the native manager
lifecycle, original CPU callback evidence, native debug naming, full passing
build/test logs, actual boot and the prior unedited desktop capture. All 1,831
archived entries were hash-verified. SHA256:
`735bb27add9257b016f96864b8a465213887a21f941e9c85aaa4cb72047b07f5`.

Earlier native platform-services checkpoint (build109, boot068):
`checkpoints/native-platform-services-068.zip`, including native notification
queues/handles, mutex ownership, original UI/profile evidence, full passing
build/test logs, actual boot and the prior unedited black-clear capture. All
1,851 archived entries were hash-verified. SHA256:
`cf4416db765169f125fd1ea87e003551282b20ca1261c1dc88052cf21d119ece`.

Earlier native audio-boundary checkpoint (build110, boot069):
`checkpoints/native-audio-boundary-069.zip`, including the early EXm0 guard,
original codec/pool evidence, software-decoder and muted-output probe sources,
logs/reports, and full passing build/test/boot logs. All 1,875 archived entries
were hash-verified. Original/derived audio media and probe binaries are excluded.
SHA256 `c6f21bfee13d76c46a3509bfad5617d38916db8ebe51d151422e90c4d6400522`.

For desktop inspection (the sandbox's isolated desktop is not visible to the
capture tool), launch in the desktop session:
`SimpsonsNative.exe --image analysis/simpsons.pe --hold-on-failure`.
This optional switch retains an already failed window for up to 120 seconds;
it never resumes failed game execution. Normal automated runs omit it.

Earlier native audio-codec checkpoint (build115; game boot remains069):
`checkpoints/native-audio-codec-115.zip`, including the native decoder core,
original PCM conversion and codec ownership tests, frozen EXm0 queue/lifecycle
evidence, independently built codec binaries, licenses, provenance and pinned
upstream source archive. All 2,104 payload hashes and the exact archive member
set were verified. Original/derived game audio media are excluded.
SHA256 `53be9bedeb36c9eb2b2a3694fc298f291d80044be06d8b35f8dc8ce57ebf87b0`.
The full extracted dependency source tree can be reconstructed from its included
archive and exact patch using the normal codec builder; `--verify` stays read-only.

Earlier verified audio-ownership checkpoint (build121, actual game boot072):
`checkpoints/native-audio-ownership-072.zip`, SHA256
`89808bdd59f8326bc9ba0402b36a01abf48d900c03d5121a4b9595ee06d54fc4`.
All2,188 payload hashes and the exact member set were verified. It preserves the
native EXm0 service/factory, review fixes, generic P6B0 passthrough and Dac PCM
fixture, stereo/multilayer reports, and the owned codec dependency. Original or
derived audio media are excluded. Its explicit exclusion manifest also names
the independent output-backend and SDK-contract files still being authored;
those are not part of build121's verified production state.

Earlier verified output-backend checkpoint (build122, game executable still boot072):
`checkpoints/native-audio-output-122.zip`, SHA256
`fe0e4ebba4d04095e09662ba1234c884640eb82376baa772211596a207da35ea`.
All2,300 payload hashes and the exact member set were verified; no original or
derived audio media are included. It adds the native output library/tests,
owned BE-float transport, SDK operation/teardown evidence and current build logs.
The explicit exclusion manifest names the still-independent gain fixture and
output review files, which are not part of this tested snapshot.

Earlier verified construction/gain checkpoint (build126, actual muted boot074):
`checkpoints/native-audio-construction-074.zip`, SHA256
`b680ccd87c692f32b197c17cb2da0c767cf548768a3f26b8ccf1c6c33a9e164e`.
All2,355 payload hashes and the exact member set were verified, with no original
or derived audio media and no pending-file exclusions. It includes the real Dac0
engine/master prefix, review fixes, native scalar gain processor and original
differential fixture, source-pass/starvation pins, routing/category evidence,
build126 logs and actual boot074. Playback remains guarded. The archived STATUS
precedes this final archive-hash pointer; all implementation and evidence files
are included.

Earlier verified source/worker/clock checkpoint (build133, actual muted boot076):
`checkpoints/native-audio-worker-clock-076.zip`, SHA256
`d77a7461e53757c26c14eb093d1378606461c1181ea4d2c1aba5125d821836e6`.
All2,402 payload hashes and the exact member set were verified, with no original
or derived audio media and no pending-file exclusions. It includes the native
source/DSP pipeline, original worker/callback/root teardown integration, Windows
routing policy, real millisecond clock leaf,37-suite build133 verification and
actual boot076. The new mask20 notification investigation and EXm0 next-step
analysis began after the verified production state; their results are not
claimed by this checkpoint. Its STATUS precedes this final archive-hash pointer.

Earlier verified notification/ANSI checkpoint (build135, actual muted boot078):
`checkpoints/native-audio-notifications-ansi-078.zip`, SHA256
`ba4cba21f974eef1081cd2ccaa670cb89be50ca0b63bbd0faccab5ad95d5faf0`.
All2,425 payload hashes, exact archive members and1,330 included AOT input
hashes were verified. It adds the actual audio-category listener extension,
ANSI descriptor import, original consumer evidence,38-suite build135 results,
actual boot078 and the frozen EXm0 preparation analysis. Original/derived audio
media are excluded. The exclusion manifest names the pending source assembler
and native configuration files, which are outside this tested production state.
Its STATUS precedes this final archive-hash pointer.

Earlier verified language checkpoint (build136, actual muted boot079):
`checkpoints/native-language-079.zip`, SHA256
`3c5f203815bad0568c4a1218cb1031866dd9580543ae86233849de931a5b4b6d`.
All2,436 payload hashes, exact archive members and1,331 included AOT input
hashes were verified. It adds the native Windows language wrapper, original
language/locale evidence,39-suite build136 results and actual boot079. No
original/derived audio media are included. The exclusion manifest names the
source assembler and local-player investigation still being authored; they are
not part of this tested production state. Its STATUS precedes this archive hash.

Earlier verified source-assembler checkpoint (build137, actual muted boot080):
`checkpoints/native-audio-source-137.zip`, SHA256
`2b1545065a07e99e8b73f90ff19cad1c6abbe20553f7af13424609370994bf57`.
All2,454 payload hashes, exact archive members and1,331 included AOT input
hashes were verified. It includes the source assembler, packet-preparation
helper and input manifest, frozen player-entry evidence,40-suite build137
verification and actual boot080. Original/derived audio media are excluded.
The exclusion manifest names the next native player implementation and live
audio admission investigation, which are outside this tested state. The
archived STATUS precedes this final archive-hash pointer.

Earlier verified local-player checkpoint (build139, actual muted boots081/082):
`checkpoints/native-local-players-082.zip`, SHA256
`dfcb84f90e9d935d5f45881c6743e74a789a9cd979aa3f5b537e32e1ae7da9b9`.
All2,481 payload hashes, exact archive members and1,333 included AOT input
hashes were verified. It includes the real local profile service/import/CLI,
43-suite verification, both actual boots and frozen EXm0 admission evidence.
Game media and local profile records are excluded. The exclusion manifest
names the pending reader-ring lifetime investigation. This snapshot precedes
the switch-width generator fix; its STATUS precedes this final archive pointer.

Earlier verified word-switch checkpoint (build140, actual muted boot083):
`checkpoints/native-word-switch-083.zip`, SHA256
`27116fdc2a1ca4e2e164312c26315b587b170cb6111b739534b10000c47623cd`.
All2,489 payload hashes, exact archive members and1,333 included AOT input
hashes were verified. It includes the corrected generator, emitted and original
word-switch regressions,44-suite verification and actual boot083. Game media
and profile records are excluded. The pending reader-ring investigation is
explicitly excluded. The native status-conversion work began afterward; this
snapshot does not claim it. Its STATUS precedes this final archive pointer.

Latest verified status-mapping checkpoint (build141, actual muted boot084):
`checkpoints/native-status-084.zip`, SHA256
`e18ea572173406c939e55cea9a7d41f6f791c1db440c65987fb76f7c4bffe5b4`.
All2,500 payload hashes, exact archive members and1,334 included AOT input
hashes were verified. It includes native Windows error conversion, the actual
original thread-error regression, request diagnostics,45-suite verification,
boot084 and frozen audio reader-ring lifetime evidence. Game media and profile
records are excluded. The exclusion manifest names the pending reader-group
test and graphics-helper investigation, which are outside this tested state.
Its archived STATUS precedes this final archive-hash pointer.

Use `python tools\checkpoint.py <name>` for source/provenance checkpoints under
`checkpoints`. An empty Git initialization triggered a sandbox refresh error;
its metadata was preserved there and normal sandbox execution was restored.
