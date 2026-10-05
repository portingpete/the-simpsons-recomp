# Temporary first-mission rendering shortcut

Double-click **Render Test - First Mission.cmd** to load Land of Chocolate
directly for rendering work. The shortcut opts into
`SimpsonsNative.exe --render-test-first-mission`; it also automatically skips
movies through their original stop/completion path. Normal Play and automatic
startup retain their existing behavior.

Each launch records its command and PID in `build/render-tests/<timestamp>/launch.json`
and game output in `game.log` alongside it. The game stays open until you close it.
To have the Land of Chocolate melee rabbits die as they spawn, double-click
**Play Land of Chocolate - Auto Defeat.cmd**. This launcher adds
`--auto-defeat-loc-enemies`. It sends damage through the game's normal NPC
damage handler only while `loc/loc.str` is the active map. The white rabbit
used by the story is excluded. Launch without that flag for ordinary combat.
For on-request renderer captures, run:

```powershell
python -B tools/render_test_native.py --capture-frames
```

Create an empty `captures/capture.request` in that run folder to capture the next
completed frame. This shortcut loads the initial map instead of resuming a save.

The temporary hook at original startup parser `8285F928` supplies the game's
existing `-stream loc loc.str` arguments using its borrowed-argument setter
`828759B8`. Original gameflow, player setup, resource loading and rendering run
normally. No original assets or generated C++ are edited. Omit the launch flag
to disable the shortcut. Remove the hook, native source
and launch flag when rendering work no longer needs this route.

## Validation (2026-09-21)

The rebuilt game reached its first scene in 2.106 seconds without controller
commands. Three completed 1280x720 renderer readbacks span six seconds in
`build/render-tests/20260921-210229-394774/captures`; the last image visibly shows
Homer, the chocolate shop and the pretzel fence in the opening plaza. Movie draw
count is zero. `render-test-verification.json` records the observation timings.
The shortcut itself retains the original player input and gameflow. A full mission play-through has not been qualified.

The native build and AOT gate pass (311 generated files, zero semantic
diagnostics). All 18 existing automatic-startup tests, five post-filter GPU/
evidence checks, controller tests, thread lifecycle and profile CLI checks pass.
Duplicate flags and use with profile management are rejected.

The broader NativeMemoryContract test passes its 52,448 differential mapping
checks and physical allocation/thread checks, then fails in its existing
critical-section fixture (`Critical section lacks a valid original/static
initialization`). It is not reported as a passing suite.

The initial startup audio crash was caused by the unqualified white-chocolate
rabbit dialogue `d_wchr_xxx_0000b33.exa.snu`. Movement also reached Homer clip
`d_homr_xxx_0003e6c.exa.snu`. The mono dialogue bank now has 46 individually
qualified sources, including all existing `0003e6*` Homer clips. Every source
passed XMA1/XMA2 equivalence, split-input decoding, original per-block quotas,
trimmed PCM hashes, and comparison with the independent stock decoder. Original
compressed bytes and the runtime's ownership checks are retained.

The movement-triggered mesh-particle effect `simpsons_vfx_rigid_textured`
(`8205B848`) has native support for its actual shader pair, three-input mesh
layout, base texture, animated UV transform, tint, vertex/texture opacity,
alpha blending, and depth. Its shaders are generated from the checked original
instruction records by `tools/analyze_vfx_rigid_shader.py`; the native build
verifies the transcription before compiling it. The original particle
callbacks, material updates, mesh draws, and cleanup execute normally.

Movement exposed related skin-alpha blend, cleanup and effect-transition gaps,
and a target-reset rejection where two logical target IDs shared the same GPU
storage. These paths now preserve the original alpha selector, perform the
requested blend, end the previous effect, and allow the reset only when the
backing storage is identical. Independent target resets retain their guards.

Regression coverage includes hardware and WARP particle pixel/depth checks
(texture/tint RGB, UV animation, vertex alpha, zero opacity, both observed
blend settings, retained state and direct/recorded parity), skin-alpha blend
checks, particle vertex decoding, packet ownership, original viewport reset,
and the expanded dialogue bank. This qualifies the tested rendering paths;
it is not a full-game compatibility claim.

The later freeze was separate from those exceptions. A live LLDB snapshot
(`build/native-process-sampling/sample-20260922T021521.252418Z-44988.json`)
captured the game thread in console allocator `82457B40`, reached through
`8244C450` from `82773B30`. The window and audio threads remained active;
closing that frozen window produced the misleading final `Native window closed`
message. The frame counter had stopped at 998.

Original `82773B30` now submits its full-screen alpha clear through the native
screen renderer. RGB, depth and stencil remain bit-for-bit intact. Its original
CPU flag store and register-restoring epilogue still execute, and the five
following SDK state setters publish their native equivalents. The subsequent
mono mesh pass now implements the alpha-only mask, including its original
depth test/bias, before the existing post filter runs.

The final movement run `build/render-tests/vfx-20260921-222300-161941/result.json`
advanced through 3,000 scene frames in 73.54 seconds with 47 replayed keyboard
changes, live mesh-particle draws and no gameplay/thread failure. Seven completed
front-buffer readbacks show continuing presentation; the last contains 16 post
filter draws and 10 mono mesh draws. The harness deliberately closed the window
after reaching frame 3,000. Replay timing is synchronized to scene counts, but
this is a bounded diagnostic run, not deterministic playback or a full mission
play-through.

All 19 focused checks in `build/render-freeze-final-tests.log` pass. Added
regressions run the original alpha-clear routine, check every RGB/alpha/depth
pixel, verify its ABI, caches, flags and subsequent cleanup, and exercise the
mono alpha-only pass on hardware and WARP. The screen backend also passes its
8,252 checks on hardware (`OriginalScreenBackendTests --hardware`).

The bridge pickup crash in run `20260921-222542-064177` (frame 109,544)
was an L8 texture rejection: the decoder only admitted 64x64 textures, while
the reached `Arcs` effect is 256x256. The decoder now uses the descriptor's
actual tile pitch for power-of-two base textures from 64 to 2048 texels in
each dimension, including rectangular textures. Format, swizzle, mip and
allocation guards remain in place. Ten fixtures, including four original
effect textures, match an independent inverse-address decoder byte-for-byte;
GPU upload/readback checks pass on hardware and WARP.

The subsequent manual traversal in run `bridge-20260921-231647-000704`
successfully rendered `Arcs` and collected bridge items, then stopped at
frame 3,419 on dialogue `d_chcb_xxx_0000b23.exa.snu` beyond the bridge.
Its exact original 1,573-byte first block matches the rejected source hash.
The newly qualified source includes all 26 blocks and 128,658 frames;
independent decoder error is at most 1.78814e-7. A following replay also
reached Nelson clip `d_nels_xxx_000683f.exa.snu`; its 10 blocks and 49,348
frames passed the same qualification. A different run selected chocolate-rabbit
clip `d_chcb_xxx_0005bd4.exa.snu`; all 27 clips in the related `0000b1f..0000b2f`
and `0005bd0..0005bd9` sets are now qualified together to cover those variants.
The dialogue bank regression decodes all 46 sources (1,000 blocks, 4,978,349
frames), validates complete PCM hashes and rejects altered source blocks.
All five focused audio/texture
tests pass in `build/bridge-final-tests.log`.

The rebuilt run `bridge-dialogue-20260921-233011-478249/result.json` completed
4,300 scene frames in 87.06 seconds with 151 input changes and no failure,
then left the game open. This replay did not reach the bridge texture or
post-bridge dialogue trigger; it does not establish a successful crossing
in the latest build. Those sources are covered by the byte-exact regressions
above, and the earlier manual traversal establishes the pickup texture fix.

Build repairs needed for this workspace: correct generator relative-path
construction, rebuild the stale native audio dependency, refresh the reviewed
post-filter shader hash (GPU/evidence checks pass, shader source unchanged), and
remove a nested acquisition of `vmMutex` in `Runtime::map`. Physical and thread
allocators already hold that mutex throughout mapping; bootstrap is single-threaded.
