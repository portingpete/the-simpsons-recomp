# Native video settings

Open **Pause → Options → Video** (also available from frontend Options).
Use Up/Down to select a row and Left/Right to change it. With the mouse, hover a
row and click its left or right half to decrease or increase it; the wheel also
changes the selected setting. Accept and Cancel are clickable. The screen uses the
original game's Apt display list, font, pulsing buttons and controller navigation.

The menu contains the original Brightness slider and twelve native controls:

| Setting | Choices |
| --- | --- |
| Render resolution (restart) | 1280 × 720, 1600 × 900, 1920 × 1080, 2560 × 1080 (Ultrawide), 2560 × 1440, 3440 × 1440 (Ultrawide), 3840 × 1600 (Ultrawide), 3840 × 2160, 5120 × 1440 (Ultrawide) |
| Window size | 1280 × 720, 1600 × 900, 1920 × 1080; ultrawide: 2560 × 1080, 3440 × 1440, 3840 × 1600, 5120 × 1440 |
| Display mode | Windowed, borderless fullscreen |
| VSync | Off, On |
| Frame limit | 30, 60, 90, 120, 144, 165, 240 FPS, Unlimited |
| Texture filtering (restart) | Original, 4×, 8×, 16× anisotropic |
| Antialiasing (restart) | Original, FXAA, FXAA + Original, SSAA 4× |
| FOV (16:9) | Original, 60–110 degrees in steps of 5 |
| Render scale (restart) | 50%, 67%, 75%, 100%, 125%, 150%, 200% |
| Bloom | On, Off |
| Depth of field | On, Off |
| Motion blur | On, Off |

Window size, display mode, VSync, frame limit, FOV and the three effect toggles
apply live. Render resolution, render scale, texture filtering and antialiasing
apply when the game is relaunched; these rows say `(restart)` in the native menu.
Accept saves the preferences; Cancel restores their values from when the menu
opened. The original engine continues to handle brightness and menu transitions.

The FOV value is a horizontal **16:9 reference**, with 60 degrees matching the
original camera factory's field of view. It scales the authored camera's perspective
tangents by `tan(FOV / 2) / tan(60 / 2)`, retaining the original zoom and cinematic changes;
the actual camera angle can therefore differ from the reference during those
changes. Original restores the unmodified authored view. Ultrawide expands the
horizontal view at the selected scene aspect while retaining the adjusted
vertical view. The original view-window setter rebuilds projection reciprocals
and the culling frustum. The selected FOV is published before the scene copies
its visibility planes, so objects beyond the default FOV remain visible inside
the selected view. See [original visibility verification](native-fov-visibility.md).
Menu/HUD, shadow, reflection and split-screen camera
profiles retain their original projections. Repeated camera begins do not
accumulate the adjustment, and Cancel restores the prior FOV live. Opening and
closing menus preserves the authored camera projection through the original
temporary square-camera override and restoration.

Render scale changes the number of scene pixels independently of the output
window. For example, 2560 × 1440 at 75% renders at 1920 × 1080. Scene color,
depth, post-processing and the HUD use the scaled targets; the output window
stays at its chosen size. SSAA 4× additionally doubles each scene dimension
and resolves four samples. Choices that would exceed 33,177,600 scene pixels
or D3D11's 16,384-pixel axis limit are skipped when changing resolution, AA or
scale. For example, 4K with SSAA supports 100% scale, but not 125%.
Defaults remain 100% scale and Original FOV.
Below 100%, the outline and smoothing filters retain their original texel
sampling span so downscaling keeps the game's ink edges.

Bloom, depth of field and motion blur default to On, retaining the original
effects. Off suppresses only the corresponding native draw; the original
effect preparation, validation, target copies and state restoration continue.
Motion-blur history refreshes even while disabled so re-enabling it cannot
blend with an old frame. Fog, cel shading, outlines and color treatment retain
their existing behavior. These controls only affect effects requested by the
current scene; an unrequested effect has no visible change.

The Render resolution row explicitly labels wide choices `Ultrawide`. Right
from 1920 × 1080 selects 2560 × 1080; Right from 2560 × 1440 selects
3440 × 1440. Both directions visit all nine presets. This display order keeps
the saved preference IDs unchanged. Menu logs report the native action, row,
selected extent ID and complete displayed label for manual retests.

Applying settings skips window style/size changes when the current output and
display mode already match. Staged render/AA/filtering changes and live pacing
changes therefore keep mouse capture; changing Window size while fullscreen
also leaves the desktop-sized window alone.

Render resolution controls actual D3D11 scene color, depth, front buffers,
history/resolve copies, and proportional post-processing targets. The renderer
rasterizes geometry into the selected extent, rather than enlarging a completed
720p image. Original guest camera/raster metadata and menu coordinates stay
1280 × 720 and are mapped to physical GPU viewport/scissor coverage. Fixed-size
shadow, reflection, distortion and corona query resources keep their original
dimensions. Texture filtering changes linear material samplers in immediate and
cached rigid, skin and sky geometry; point lookup maps and shadow samplers keep
their original sampling. These settings are frozen before GPU resource and
cached draw-command creation, hence the relaunch requirement.

Ultrawide render resolutions use the selected scene aspect ratio. Choose an
ultrawide **Render resolution** to render the wider scene; Window size controls
the output client independently. The full-size main perspective camera keeps
its vertical field of view and expands its horizontal view through the original
camera setter and frustum updates. Split-screen and other camera profiles retain
their existing behavior. Apt menus/HUD and movie content keep their 16:9
proportions; loading artwork outside the Apt draw path retains its existing policy.
For example, a 1920 × 1080 internal image displayed on a 3440 × 1440 desktop
has side bars. To fill that desktop with the wider scene, select 3440 × 1440
as **Render resolution**, Accept, and relaunch. Window size applies immediately
and controls only the output; fullscreen always uses the monitor's desktop size.

Original antialiasing preserves the game's existing smoothing. FXAA keeps the
original compositing effects, cel shading and outlines, removes their neighboring
pixel smoothing offsets, then applies a native FXAA filter to the final image.
FXAA + Original combines both smoothing methods. SSAA 4× renders the scene at
twice the selected resolution in each dimension, retains the original effects,
and averages each 2 × 2 block into one output pixel. For example, 1280 × 720
with SSAA 4× renders scene geometry at 2560 × 1440 and produces a 1280 × 720
internal image. SSAA requires more GPU work and memory than the other modes.

Native presentation fits the selected internal image to the window or desktop
while preserving its aspect ratio, adding bars when the output aspect differs.
Selecting a higher internal extent than the window provides supersampling.
Fullscreen uses the current monitor's
desktop dimensions. VSync uses native DXGI presentation.
The frame limit updates both the original frame-wait owner and native pacing;
it is a limit, not a promise that every scene achieves that frame rate.
60 FPS retains the original one-refresh scheduler; other capped choices use
the native presentation limiter. Changing the limit clears its previous pacing
history. Gameplay clocks retain real elapsed time at every supported rate.

Preferences are stored next to the profile-store directory, with `.video.cfg`
appended to its name. For example, `userdata/local-profiles.video.cfg` belongs
to `userdata/local-profiles`. Keeping this small file outside the profile
directory preserves the engine's strict profile-file validation. Missing or
invalid preferences use 720p internally and windowed, original filtering and
antialiasing, VSync off, a 120 FPS limit, Original FOV, 100% render scale and
enabled original effects. Version 5 adds FOV, scale and the three effect flags
after the original eight fields. Versions 1–4 retain their existing fields and
use the original defaults for the new controls. Version 4 adds the ultrawide extents
without changing existing resolution indices or the preference-field order.
Version 3 preferences retain all rendering, display and antialiasing values.
Version 2 preferences retain all their
rendering and display values and select Original antialiasing.
Existing version 1 preferences migrate the selected resolution to both internal
rendering and window size, preserving the user's resolution choice.

Explicit `--frame-rate`, `--uncapped-frame-rate`, and `--vsync` launch flags
override the saved initial settings. The normal launcher and automatic-start
helper use saved preferences.
`--frame-rate` accepts 30, 60, 90, 120, 144, 165 and 240.

Recording and isolated-startup helpers copy the profile's sibling `.video.cfg`
into their private run. Recordings also preserve the starting preferences beside
the initial save/profile snapshot. Replays restore that snapshot when available,
then recorded preferences or the configured profile's preferences for older
runs. Their existing explicit frame-rate flags still apply. Relocated replay
executables receive adjacent `native-assets`, and new crash archives bundle
those packages with the original executable and DLLs. Older archives without
the packages use a private binary copy with validated current menu assets;
the archive stays unchanged. All 42 helper checks passed using temporary data
and mocked launches, without running gameplay.

`tools/build_native_video_menu.py` produces separate derived packages for both
copies of `options.swf` under the executable's `native-assets` directory.
Retail assets remain unchanged. Only Options is replaced; other resources,
Audio, Controls and Credits retain their original data and behavior.

GPU verification checks actual D3D11 color/depth descriptors, physical scene
viewports, proportional post targets, packed draw/readback extents at all five
resolutions, fixed query extents, and actual native anisotropic sampler state.
Captures report their actual internal dimensions and the depth telemetry samples
the matching depth-buffer extent.

Antialiasing GPU fixtures verify all four modes on actual WARP textures:
Original preserves source bytes, FXAA smooths diagonal edges while leaving flat
regions unchanged, and SSAA resolves the exact average of four independent
scene samples. Full native context snapshots verify that presentation filtering
restores engine bindings, viewport/scissor state and predication.

Focused verification covers preference validation and persistence, forward
and reverse control cycles including all four antialiasing modes, all nine
render extents and seven window extents, version 1/2/3
preference migration, actual window/fullscreen changes, original frame
pacing, presentation contracts, launcher arguments, and preservation of all
other packaged resources. Live gameplay captures verify menu navigation,
setting previews, Accept/Cancel and returning to gameplay.

Window-style changes preserve visibility and enabled state. Regression checks
also verify native client mouse hit testing after every resolution change and
fullscreen transition. Explicit window sizes account for the caption and borders
without Windows shortening the requested client height to its default monitor
tracking limit.

The September 30 internal-resolution verification passed GPU fixtures at all
five resolutions and full-game captures at 900p/1080p in the native build and
1440p/2160p in the release build, each with an independent 1280 × 720 client.
The 1080p native-menu run verified all seven rows, a live 1600 × 900 window,
fullscreen, staged internal changes, cancellation, preference saving, and
returning to scene geometry with fresh depth telemetry. Both game builds and
their input-recorder executables include the rendering changes and derived menus.

The antialiasing update passed 14 focused native checks and five release checks.
Live captures verified every mode with fresh geometry/depth, enabled visible
windows and completed presentation copies. The native eight-row menu run
verified each AA choice, cancellation, saving and return to gameplay. The SSAA
release run rendered scene color/depth at 2560 × 1440 and captured the resolved
1280 × 720 internal output. Capture metadata records `scene_width`,
`scene_height` and `antialiasing` separately from output `width`/`height`.

The ultrawide update passed 21 focused checks in each final native/release
configuration, including all four actual wide color/depth extents, the original
camera setter/frustum, centered Apt/movie rendering and exact internal pixel
copies across display aspect changes. The depth-of-field regressions cover
zero focus distance, zero range and both zero through the original CPU and
independent shader microcode fixtures. The user will verify ultrawide gameplay
and the charged-burp release; those manual results are not established by these
automated checks.

The settings-return update passes isolated window/controller regressions in
both builds. Menu input-gate diagnostics log result changes from the original
Apt gate without changing its behavior, for the user's next pause-menu retest.

The ultrawide-menu follow-up rebuild passes preference/navigation, native
launcher and menu-asset checks in both builds. A bounded independent evaluator
of the appended Apt bytes verifies all row actions, native-action export/reset
and full ultrawide label assignment. It mocks method receivers and does not
replace manual verification of the original AVM menu in-game. New row/action
logs support the user's reported wrap-at-4K retest.

The October 4 graphics-settings update passes all 528 tests in the final native
build, including original camera/frustum and ABI checks, preference migrations,
all menu actions, scaled rendering, frame pacing and effect disable/re-enable.
WARP and hardware fixtures compare 50/67/75% outline, smoothing and cel
compositing pixels against matching physical-size reference targets.

A private first-mission run verifies all thirteen menu rows, live 110-degree
FOV, previews, Cancel, Accept and saved preferences. Relaunch renders the selected
75% scale at 960 × 540 with a 1280 × 720 output window; viewed captures retain
ink outlines and show Original restoring the stock 60-degree view. Both owned
runs close normally. These captures verify behavior, not a frame-rate gain or
all-mission coverage. All 255 primary profile/content/preference hashes remain
unchanged. Receipts are in `build/graphics-settings-20261004`.
