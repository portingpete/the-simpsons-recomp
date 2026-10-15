# Native video settings

Open **Pause → Options → Video**, or Video from the frontend Options menu.
The menu opens on its page row: Left/Right there switches between seven pages
(**Display**, **Quality**, **Effects**, **Color**, **Outlines**,
**Performance**, **Game**); Up/Down selects a row and Left/Right changes it.
Mouse hovering, clicking either half of a row, and the wheel also change
settings. Accept saves all pages; Cancel restores every setting from when the
menu opened.

The menu keeps the original Brightness slider and adds forty rows. Rows marked
*Restart* (or *Next launch*) are saved by Accept and take effect when the game
starts again; every other row previews immediately.

| Page | Setting | Choices | Applies |
| --- | --- | --- | --- |
| Display | Display mode | Windowed, Borderless, Fullscreen (exclusive) | Live |
| Display | Window size / Display resolution | Windowed: nine 16:9 and ultrawide sizes. Fullscreen: the monitor's 32-bit modes from 1280 × 720 up, or Desktop | Live |
| Display | Refresh rate | Fullscreen: every rate the selected mode offers; otherwise the desktop rate (shown) | Live |
| Display | VSync | Off, On | Live |
| Display | Frame limit | 30, 60, 90, 120, 144, 165, 240 FPS, Unlimited | Live |
| Display | Render resolution | **Match display** (default), or 1280 × 720 … 3840 × 2160 and four ultrawide extents | Live |
| Display | Render scale | 50%, 67%, 75%, 100%, 125%, 150%, 200% | Live |
| Quality | Antialiasing | Original; FXAA Low, FXAA, FXAA High, FXAA + Original; SMAA; **TAA** | Live |
| Quality | Texture filtering | Original, Nearest, Bilinear, Trilinear | Live |
| Quality | Anisotropic filtering | Off, 2×, 4×, 8×, 16× | Live |
| Quality | Shadow quality | Low, Original, High, Ultra | Live |
| Quality | Ambient occlusion | Off, Low, High | Live |
| Quality | Upscaling | Bilinear, Edge-adaptive | Live |
| Quality | Sharpening | 0%, 25%, 50%, 75%, 100% | Live |
| Quality | Character detail | Off, Low, Medium, High | Restart |
| Effects | FOV (16:9) | Original, 60–110 degrees in steps of 5 | Live |
| Effects | Bloom, Depth of field, Motion blur, Atmospheric fog | On, Off | Live |
| Effects | Lens flare | On, Off | Live |
| Effects | Screen shake | On, Off | Live |
| Effects | Cinematic bars | On, Off | Live |
| Color | Color grading | On, Off | Live |
| Color | Color look | Original, Vivid, Punchy, Soft, Custom | Live |
| Color | Vibrance, Saturation, Contrast, Exposure | −50% … +50% in steps of 10 (Custom look) | Live |
| Outlines | Ink outlines | Original (hard), Soft, Off | Live |
| Outlines | Soft outline strength | 25%, 50%, 75%, 100% | Live |
| Outlines | Outline color | Black, Charcoal, Brown, Navy, Forest, Plum, Crimson, Sepia | Live |
| Outlines | Character eyes | Original, Clean | Live |
| Performance | Background limit | 15, 30, 60 FPS, Unlimited | Live |
| Performance | Menu frame limit | Same as gameplay, 30, 60 FPS | Live |
| Performance | Frame queue | 1, 2, 3 frames | Live |
| Performance | FPS counter | Off, On | Live |
| Performance | Skip intro videos | Off, On | Next launch |
| Performance | Renderer | D3D11, Vulkan ([native Vulkan renderer](native-vulkan-renderer.md)) | Restart |
| Game | New game episode | Any of the 16 story episodes | Restart |

Film grain, chromatic aberration and vignette are not original effects of this
game, so they have no switches.

## Display modes

Windowed and Borderless keep the desktop mode. Fullscreen switches the game's
monitor to the selected display resolution and refresh rate
(`ChangeDisplaySettingsEx` with `CDS_FULLSCREEN`, so Windows restores the
desktop mode if the game exits). Alt-Tab or losing focus restores the desktop
mode and minimizes the game; returning re-applies the selected mode. The mode
list comes from the monitor (32-bit, progressive, at least 1280 × 720);
Desktop uses the current desktop mode. Automated and background test windows
(`SIMPSONS_BACKGROUND_WINDOW=1`) treat Fullscreen as Borderless so tests never
change the user's display mode.

Render resolution **Match display** follows the presented size: the window
client, the desktop, or the exclusive mode. It picks the largest supported
16:9 … 32:9 internal extent that fits (1280 × 720 to 5120 × 2160); Render scale
then applies on top. When the internal image is smaller than the window,
Upscaling selects DXGI's bilinear stretch or an edge-adaptive filter that keeps
outlines sharp.

## Temporal antialiasing

TAA jitters the world projection by an eight-sample Halton(2,3) sequence
(applied where the original camera begin stores its projection, `0x823F0210`;
HUD, shadow and reflection cameras are not jittered), then resolves
the scene color after the post depth copy and before the original post
effects. History is reprojected with the previous view-projection matrix,
clamped to the current neighbourhood, and reset on camera cuts. The packed
material flags in scene-color alpha are kept, so the original cel-shading and
outline pass still reads them. Like the other modes it can be switched live;
the first TAA frame starts a fresh history.

## Textures, shadows and occlusion

Texture filtering replaces the linear material samplers: Nearest keeps texels
square, Bilinear disables mip blending, Trilinear blends mips; Original keeps
the game's own filter. Anisotropic filtering applies on top of any linear mode.
Point-sampled lookup maps, palettes and comparison samplers are never changed.

Shadow quality scales the storage of the 1024 × 1024 shadow-map surfaces (the
character and world shadow depths) to 512, 1024, 2048 or 4096 texels per side;
their logical size and the original shadow matrices stay unchanged.

Ambient occlusion is a depth-only screen-space pass over the finished world
image (8 samples for Low, 16 with a wider radius for High), blurred at half
resolution and multiplied into color before the post effects. HUD and menus are
not affected.

## Color look

Color look adjusts the finished world image before the HUD: Vivid, Punchy and
Soft are presets; Custom uses the four sliders. Original leaves the image
unchanged. Color grading is the separate original saturation treatment.

## Outlines and eyes

The game draws its ink outlines inside the original `simpsons_edgeAA`
composite. Original keeps it exact (the extra parameters are zero). Soft fades
the lines to the chosen strength, Off removes them, and Outline color tints
them (black is the original).

Character eyes **Clean** affects the base character shader family, whose
vertex color carries the artists' "no rim shadow" flag (eye whites). The
original rim test only accepts a fully set flag; Clean accepts it from 0.5, so
triangles where the flag is partly set no longer show speckled rim shadow on
eye whites. The native port already interpolates this flag without
perspective, which removes most of the console's speckles even in Original.
Changing it re-records sealed skin draws.

## Character detail

Character detail rounds skinned character meshes with curved PN triangles:
Low, Medium and High split every triangle edge into 2, 3 or 4 segments (4×, 9×
or 16× the triangles). Corners keep the original vertices; new vertices lie on
cubic patches built from normals averaged over identical positions, so
neighbouring triangles, UV seams and material boundaries share one edge curve
and the surface stays closed. Bone weights, UVs, colours and facial morph
deltas are interpolated, so animation and expressions are unchanged.

Each draw range is smoothed once, on a low-priority worker thread, the first
time it is drawn; until the result is ready (usually a few frames) the original
mesh draws. Uploading a finished range costs the render thread about 1–2 ms,
at most one per frame. Recorded (replayed) draws keep what they record, so
they smooth their range when recorded, on the render thread. Results stay with
the cached mesh. On the first-mission
start view High measured 147.3 and 146.6 FPS against 146.6 and 143.1 FPS with
detail Off (D3D11, 720p, uncapped). Shadow maps and the depth-only passes use
the original meshes; eyeballs become rounder too, which can make them show a
little more from the side.

## Presentation extras and menus

The FPS counter draws the presented frame rate in the corner of the final
image, on a copy made at presentation, so scene captures do not include it. Menu frame limit
caps presentation while a menu or the pause screen is open. Skip intro videos
skips the startup logo movies on the next launch.

## New game episode

New game episode chooses the episode that **New Game** starts in. At startup
the game's `simpsons_gameflow.lua` is copied to `<exe>/native-gameflow/`, with
its `SetDefault()` moved to the chosen episode, and the game loads that copy
instead (`[NATIVE GAMEFLOW]` in the log). The retail script is not modified;
if its layout is not the expected retail one, New Game keeps its original
episode. Existing saves are not affected.

## Live rendering changes

Render resolution, Render scale, Antialiasing, Texture and Anisotropic
filtering and Shadow quality apply at the next presentation after the row
changes. That frame waits for the GPU to
go idle, then (`renderer/live_rendering.cpp`):

- allocates new storage for every Scene- and Output-sized engine target whose
  extent changes, before any owner is modified, so a failed allocation keeps
  the active settings;
- carries the current contents across: filtered color, nearest depth, and
  stencil one bit per pass (no shader stencil export needed);
- swaps the new storage into the existing owners, which keep their identity and
  logical size, and moves the engine's bound attachments, sampled views,
  viewports and scissors to it;
- re-records sealed recorded draws (rigid, skin, sky, mono) so they reference
  the new views, physical rectangles and filtered samplers;
- moves the temporal origin pins to the new storage (raster grids become
  unknown; clears survive), and lets the next presentation resize the swap
  chain after its queued copy has completed.

If a recording session is open the change waits for the next presentation.
A change rejected before publication (unsupported settings or budget) keeps
the active settings and the menu shows them again; a failure after publication
stops the game instead of rendering partly updated state. The log line
`[NATIVE VIDEO] live rendering ...` reports the old and new extents, the number
of re-backed targets and re-recorded lists, and how long the change took. The
frame limiter discards that one long frame instead of carrying it as pacing
debt. `tools/test_live_rendering_settings.py` drives these changes through the
real paused-game menu and checks each one with a renderer readback.

After MSAA was removed (2026-10-14, D3D11, first mission, background window)
that run applied ten changes in one process: 1280 × 720 → 1600 × 900,
Original → 4× filtering, every AA step from Original through SMAA,
100% → 75% scale, a 1280 × 720 preview and its Cancel. Re-backing 22 color and
5 depth targets and re-recording 126 lists took 11.9 ms for the resolution
change and 4.2–6.0 ms for scale changes; filtering took 5.7 ms; AA steps took
1.1–3.7 ms. The Video menu showed `Antialiasing: SMAA` as the last choice.
Accept saved the choices, Cancel restored the accepted storage, and the game
closed normally
([verification](K:/SimpsonsNativeCopy/build/live-rendering-settings-20261014/live-20261014-223901-804566/verification.json)).
The same run on the Vulkan renderer passed with 5.0–14.0 ms changes
([verification](K:/SimpsonsNativeCopy/build/live-rendering-settings-20261014/live-20261014-224108-281155/verification.json)).

## Image size and antialiasing

Render resolution and Render scale determine the internal output image;
Window size determines the client separately. For example, 2560 × 1440 at 75%
produces a 1920 × 1080 internal output. Presentation preserves its aspect ratio
and adds bars when it differs from the window. Borderless fullscreen uses the
monitor's desktop dimensions.

Original retains the game's smoothing. FXAA offers three edge-search qualities,
and FXAA + Original combines both smoothing methods. SMAA applies spatial edge
detection and neighborhood blending. For higher quality, combine SMAA with a
render scale above 100%, which supersamples the whole pipeline.

MSAA 2×/4×/8× has been removed. The game draws its cel shading, palette colors,
rim light, shadows and ink outlines in a full-screen pass (`simpsons_edgeAA`)
that reads single-sample copies of the scene. MSAA could not smooth those
outlines, averaged the material flags packed into scene color at model edges
(wrong palette colors on exactly those pixels), and multiplied the cost of every
later post-effect for samples that were already identical. All render targets
are single-sample; the Vulkan renderer rejects multisampled textures.

SSAA choices have also been removed. Saved retired SSAA IDs 3 and 6 and excluded
temporal IDs 11–15 migrate to Original antialiasing, and removed MSAA IDs 8–10
migrate to SMAA, while retaining independent display, effects, and pacing
preferences. Legacy versions retain their original
schema checks; unknown AA IDs or malformed fields still fall back to defaults.
Render scale remains an independent image-size control. DLAA and DLSS are
excluded from this build; the normal game needs no NGX SDK or runtime.

The settings enforce a maximum of 33,177,600 output pixels, D3D11's texture axis
limit, and a 1 GiB budget for scene color plus depth storage (12 bytes per
pixel). Resolution, scale, and AA cycles skip combinations exceeding these limits.
This budget excludes other textures and driver allocations.

Sharpening is applied after antialiasing. Its 0% default preserves the image;
higher values use a contrast-clamped filter and affect the presented scene and
HUD.

## Camera and effects

FOV is a horizontal reference at 16:9. The original reference is 60 degrees;
the chosen value scales the authored perspective tangents while retaining zoom
and cinematic changes. Ultrawide expands the horizontal view while retaining
the adjusted vertical view. The original camera setter updates the culling
frustum. Original restores the authored view, and repeated camera begins do not
accumulate the adjustment. HUD, menus, shadow, reflection, and split-screen
camera profiles retain their original projections. Apt UI and movies retain
their 16:9 proportions.

The effect switches default to On. Each switch controls the corresponding
original effect when a scene requests it. Lens flare is the original
sun-visibility-modulated glare overlay (pixel shader `0x82152318`); Screen
shake removes the original camera shake offset (`0x8295A3DC`) without changing
gameplay. Neither appeared in the automated opening views, so their
suppression has been checked in code and logs only. Motion-blur history still refreshes
when the effect is disabled, so re-enabling it does not blend an old frame.
Fog controls the depth-based atmosphere pass; Color grading controls the
original saturation treatment; Cinematic bars controls letterboxing.

## Pacing and preferences

The frame limiter accounts for VSync blocking and replaces the original refresh
wait for every explicit limit. With VSync on, a limit at or above the display's
refresh rate (or a whole multiple of its period, such as 60 FPS on a 120 Hz
display) follows the display's own vblanks instead of a separate clock, so every
frame reaches its own refresh; a 120 FPS limit on a 119.84 Hz display runs at
119.84. Gameplay updates are timed for the refresh or release they will be shown
at. Changing the limit or VSync resets its timing history. A selected limit caps
presentation; actual scene performance can be lower. See `docs/frame-pacing.md`.

Background limit reduces presentation work when the game is inactive or
minimized and never raises a lower foreground limit. Unlimited removes the
additional background cap. Frame queue sets DXGI's maximum queued presentation
frames (on Vulkan, the frames in flight before Present waits): one is the
default, with two and three available. It does not specify a measured input
latency.

The 2026-10-12 performance build passed separate 90-second Medal and house
checks at the settings below, including audio, controller acknowledgments,
world geometry, all 970 source inputs, unchanged primary preferences and
unforced owned closure. It reuses exact screen state and movie scratch
descriptors, batches immediate/particle state restoration, samples paired
billboard traces, and retires a recording against its own last GPU event.
All 21 automated tests passed, as did the separate hardware screen fixture.
Fresh private training of nine changed translation units was merged into the
accepted profile; the complete normal USE build had no profile-mismatch or
discarded-count diagnostics.

A fresh archived-baseline Medal run used the same route, private settings and
helper protocol. The observed application intervals were:

| Run and phase | Count | Mean | p99 | Maximum | Above 50 ms |
| --- | --- | --- | --- | --- | --- |
| Archived baseline Medal route | 767 | 36.551 ms | 67.423 ms | 91.545 ms | 75 |
| Current Medal route | 633 | 44.500 ms | 74.984 ms | 80.663 ms | 216 |
| Archived baseline Medal settled | 1166 | 43.814 ms | 61.329 ms | 76.904 ms | 647 |
| Current Medal settled | 1139 | 44.774 ms | 61.221 ms | 72.649 ms | 480 |
| Current house settled | 985 | 17.527 ms | 20.363 ms | 23.455 ms | 0 |
| Current house movement | 246 | 17.605 ms | 20.266 ms | 26.149 ms | 0 |

This comparison **does not establish a frame-rate improvement**. Frame times
changed substantially within both Medal runs, and the route did not hold
world state fixed. A post-run read-only host check found DarkRecomp using
about 35% of the GPU while Simpsons was closed; total GPU utilization was
about 44%. That observation establishes a competing workload at the time of
the check, rather than its complete history or a causal explanation of the
timing difference. The full current Medal run retains 767 intervals above
50 ms and two above 100 ms; the house run retains eight and three,
respectively. First real Medal rendering took 108.994 ms and first house
rendering took 166.037 ms. Cold loading and the whole-game 60 FPS target remain
unresolved. No other application was stopped.

The [clean comparison](K:/SimpsonsNativeCopy/build/performance-20261012/clean-comparison-final-a.json),
[host observation](K:/SimpsonsNativeCopy/build/performance-20261012/host-gpu-observation.json)
and [build qualification](K:/SimpsonsNativeCopy/build/performance-20261012/final-normal-build-b/qualification.json)
retain the current evidence. Every positive interval and outlier is included;
the only excluded interval is each CSV's initial zero sentinel. A separate
diagnostic run observed fewer sampler and blend/depth creation requests, as
documented in `docs/runtime-stall-profiler.md`; instrumented timings are not
clean benchmark evidence.

The 2026-10-10 archived baseline build was checked in two separate 90-second runs with a
60 FPS limit, VSync off, 4300 × 1800 internal rendering, a 3440 × 1440 window,
FXAA High, 125% render scale and 16× filtering. Primary preferences stayed
unchanged; these overrides applied only to the private test copies.

| Application interval | Count | Mean | p99 | Maximum | Above 50 ms |
| --- | --- | --- | --- | --- | --- |
| Medal complete run | 3244 | 27.600 ms | 37.621 ms | 150.841 ms | 5 |
| Medal route | 936 | 29.774 ms | 36.848 ms | 55.426 ms | 2 |
| Medal settled stationary | 1973 | 26.072 ms | 28.488 ms | 45.059 ms | 0 |
| House complete run | 5129 | 17.474 ms | 35.939 ms | 162.607 ms | 7 |
| House settled stationary | 1032 | 16.670 ms | 16.787 ms | 19.269 ms | 0 |
| House movement | 257 | 16.739 ms | 18.920 ms | 20.636 ms | 0 |

Medal route and settled averages correspond to 33.59 and 38.36 application FPS;
this still **does not meet the 60 FPS target**. The earlier same-settings
observation recorded 12.54 and 16.38 FPS, respectively. The first Medal frame
was 100.2047 ms. House settled and movement averages were 59.99 and 59.74 FPS,
but the first-room entry frame still took **162.6069 ms**. Cold-loading stalls
remain unresolved. All startup frames and outliers remain in the complete
results, including two Medal and three house intervals over 100 ms.

The current single-sample immediate and particle paths avoid whole-target
copies while preserving quantized color, depth and draw order. Proven-empty
immediate submissions skip invisible uploads; bounded reusable upload buffers
and exact cached render-state descriptors reduce repeated setup. The retained
copy paths provide comparison controls.
That baseline's twelve focused WARP/hardware tests passed with zero skips in 8.42 seconds.
Fresh training of the two changed rendering units was merged into the existing
profile, followed by a complete normal USE rebuild with no profile-mismatch
or discarded-count warnings. Build and gameplay verification are complete.

The final [Medal verification](K:/Anything/simpsons-pacing-inspection-20261010/medal-copy-region-validation/two-tu-pgo-training-a/final-normal-helper/stall-reuse-clean-60-medal-final-use1/verification.json),
[house verification](K:/Anything/simpsons-pacing-inspection-20261010/medal-copy-region-validation/artifacts-e/final-house-helper/stall-reuse-clean-60-final-house1/verification.json)
and [independent raw-interval audit](K:/Anything/simpsons-pacing-inspection-20261010/medal-copy-region-validation/final-verification.json)
retain the gates, phase boundaries and all outliers. Each run's adjacent
`frames.csv` contains its raw intervals. The final
[GPU test log](K:/Anything/simpsons-pacing-inspection-20261010/medal-copy-region-validation/two-tu-pgo-training-a/final-normal-build/final-twelve-serial-CTests/LastTest.log)
records the twelve passing tests. Audio, route input, world geometry, all 970
source inputs, primary preferences and normal owned closure passed; both game
processes are gone.

These are application intervals observed through Remote Desktop. The runs did
not hold world state fixed and do not establish a matched causal gain,
physical-display scanout, natural campaign progression or performance across
every level. Earlier trials, including the prior 175.3868 ms first-house hitch,
remain recorded in `MODLOG.md`.

Preferences are stored beside the profile-store directory with `.video.cfg`
appended, such as `userdata/local-profiles.video.cfg`. Version 10 appends 24
fields to version 9 (display mode, refresh, anisotropy and every row above);
loading version 9 or older keeps those settings and maps the old 4×/8×/16×
filtering choice to Anisotropic filtering. Removed-mode migration changes only
antialiasing. Defaults are a 1280 × 720 window, Match display rendering,
Original AA and filtering, 100% scale, Original FOV, enabled original effects,
Original shadows, no AO, VSync off, a 120 FPS foreground limit, 30 FPS
background limit, no sharpening, one queued frame, and original outlines,
eyes, character detail and New Game episode.

Explicit `--frame-rate`, `--uncapped-frame-rate`, and `--vsync` launch flags
override saved startup choices. Recording and replay helpers preserve the
profile's sibling video preferences in their private runs. The derived native
menu packages are generated from the user's retail assets; retail files remain
unchanged.
