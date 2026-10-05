# Level transition crash and gameplay performance (2026-09-30)

## Crash and repair

The reported log ends with `Unimplemented native engine graphics boundary
0x82740680, caller 0x8273B4E0`. The rejected packet names retail effect
`820465E8`, `simpsons_rigid_dualtextured_uv`. The original scene dispatcher
reached this effect after the user's first-level completion; the native
bridge had no supported source profile or compiled artifacts for it.

The repair adds the original opaque pair `VS82046D9C / PS820477A8` and
transparent pair `VS82047318 / PS82047F44`, with their complete shader
control flow and material maps. The source animates position and UVs using
five private vertex vectors plus the original time callback. Its two
textures occupy stages 1/2 in opaque draws and 0/1 in transparent draws.
Opaque draws retain character-shadow depth at stage 0. The native bridge
now retains both textures, the selected pass, material constants, original
record/replay ownership, depth and blending, with separate validation for
the opaque and transparent paths.

`tools/analyze_rigid_dualtextured_uv_shader.py` pins the retail effect and
four shader records. Tests independently inspect original material maps,
dirty masks and both fallback flags. The incoming fallback r5 is a dead
Boolean: original instruction `82740120` overwrites it with packet.object
before the first indirect call. No shader, material or geometry is skipped
to get past the unsupported boundary.

Both native and release game/recorder executables are rebuilt. Each build
passed all **16 focused CTest groups**, covering the crashing material,
existing rigid materials, mono/depth/shadow passes, audio ownership,
viewport copies, video settings and driver lifecycle. Logs are
`build/level-transition-fix/final-native-tests.log` and
`build/level-transition-fix/final-release-tests.log`. Final AOT verification
checks 311 files with zero semantic diagnostics.

The new `OriginalRigidUvPass` regression passes 232 checks through the
original crash caller and dispatcher, all 49 effect constructors, original
ITXD texture loading and shadow-camera copies, material callbacks,
recording/cache ownership, identical cached pixels, changed TimeTicker
uploads, and the alpha fallback with incoming r4/r5 both 1. It verifies
dirty masks, nonvolatile CPU ABI, visible center pixels and unchanged
pixels outside the geometry. The synthetic fixture's main-camera color
mask is explicitly 15; its earlier depth-only mask correctly suppressed
immediate alpha color writes and was a fixture error.

The GPU shader oracle independently decodes original control flow,
arithmetic and vertex fetch swizzles. Both hardware and WARP pass 32
numerical cases plus direct/recorded color, depth, stencil, ownership and
state-restoration checks. Seven offline shader tests reject 207 semantic
mutations. Full draws exposed an alpha position/normal seed inversion;
the native seed is corrected and the oracle now follows original fetch
swizzles, with a distinct-position/normal regression. Independent image
decoding confirms both vertex input layouts.

The user's exact end-of-level route has not been replayed; gameplay
verification remains manual. Before-change sources, executable and video
preferences are preserved under `build/level-transition-fix/before`, and
the complete failure log is `build/level-transition-fix/reported-crash.log`.

## Performance evidence

The manual run `build/render-tests/20260930-193712-804870/game.log`
started with internal/output dimensions of 3440x1440 and unlimited frames.
Its in-game Video actions stepped antialiasing backward three times and
texture filtering backward once, then forward twice, before accepting.
The resulting saved preferences are `4 4 1 0 0 6 0 0`, so the run began with
SSAA 4x and 16x filtering. Both settings freeze at backend creation and
require a restart; accepting Original in Video did not change that running
backend.

`NativeBackend::configureRendering` doubles both scene dimensions for SSAA
4x. The run therefore rendered its scene at **6880x2880**, about **19.8
million pixels**, or **21.5 times** the original 1280x720 pixel count. The
current saved Original antialiasing preference will render at 3440x1440 on
the next launch, about 5.38 times the original pixel count. No preferences
were changed by this investigation.

The log has 9581 successful presentations and no frame-timing CSV. Boot,
menus, pause time and loading share the same log, so those counts cannot
establish gameplay FPS or identify a measured CPU/GPU bottleneck. Do not
compare this run directly with the stationary 1280x720 measurements in
[gameplay-fps.md](gameplay-fps.md).

## Avoided per-mesh constant snapshots

Static depth-prepass, mono-mask and static-shadow mesh inspections previously
called `EngineEffects::view` to read only the skinning Boolean at private
word 64. That call copied the effect's scalar, sampler and private-value
vectors, then loaded every live private word (roughly 1100) on each
inspection. The latest run reached 602,179 depth-prepass draws, so this
unnecessary work occurred on a frequent rendering path.

These three inspections now read that one live word directly. They retain
the same runtime/thread/context and identity checks, exact private-storage
extent and overflow checks, and validation of the **full allocation's**
read permission before the volatile endian-correct load. The fallback
before storage exists still uses the authored private default. The Boolean
check and all drawing behavior remain unchanged; public `view` still
returns its complete live snapshot. This removes allocations and unused
constant reads without reducing rendered resolution or skipping draws.

## Reduced routine diagnostics

The run produced about 14.3 MB of log output. Routine depth-copy, mono-begin
and recurring audio success diagnostics now retain the first four lines
and one in each subsequent 512 events. Audio sampling spans successive
instances, since per-instance first-PCM and source messages repeated
thousands of times during this run. Cancelled and failed audio retirement
receipts, validation failures, real copy counts, resource ownership and
all audio/rendering operations retain their previous behavior.

Applying this policy to the archived log's affected success categories
reduces **31,958 lines to 85**, from **4,165,237 bytes to 10,948 bytes**.
This is an offline diagnostic-volume comparison, not a gameplay benchmark
or a measured FPS gain. No automated game run was performed; the user
continues manual gameplay verification.

Affected sources are `runtime/engine_audio_owners.cpp`,
`runtime/engine_viewport_surfaces.cpp` and the mono-begin print in
`runtime/engine_effects.cpp`, which also contains the private-word reader.
Focused verification uses the existing
`OriginalExm0Lifecycle`, `OriginalViewportSurfaces` and
`OriginalMonoMaskPass`, `OriginalZPrepassPass` and
`OriginalShadowCameraPass` fixtures after rebuilding their executables.
The mono fixture checks static/skinned transitions and actual mask pixels;
the depth/shadow fixtures check original Boolean setters, native constants,
actual depth pixels, ownership and rejected inputs.
