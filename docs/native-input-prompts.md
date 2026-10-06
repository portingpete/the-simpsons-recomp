# Native mouse and keyboard prompts

Menus and gameplay tutorials use the revised hand-drawn yellow icon artwork when
the native keyboard/mouse source controls slot 0. A connected slot-0 XInput controller uses
the original gamepad artwork. Replaying inputs keeps the prompt mode selected
before playback; the recording format does not specify an input device.

During gameplay, click in the game window to activate mouse camera control. The first click also
performs its action: left click attacks, right click uses special attack / back,
and middle click sends the right-stick press. **Escape** pauses and releases the
mouse. **F6** toggles capture, and switching away releases it. The window title
shows the capture shortcut and current state.

Menus release camera capture and use the visible pointer. Hover selects a row;
left click activates it, and right click goes back. Click either half of a
setting to decrease/increase its value, or use the wheel. Authored Back, Select,
Accept and Cancel footer buttons use their visible hit bounds. Blank space and
hidden rows cannot activate the previous selection. Keyboard and controller
navigation remain available alongside the pointer.

Default keyboard controls:

- **WASD:** move / navigate menus; **arrow keys:** directional pad.
- **Enter / Space:** select / jump; **E:** interact; **K:** special attack / back.
- **Escape:** Start / pause during gameplay, Back / Cancel in menus; **Enter** also skips movies; **Tab:** Back button.
- **Q / R:** left / right shoulder; **Ctrl:** left trigger.
- **F / G:** left / right stick press.
- Existing **J** attack, **Left Shift** Homer Ball and **Backspace** Back aliases work.

WASD menu navigation uses the game's directional-pad repeat delay: a tap moves
one item, and holding starts repeating after 400 ms. Gameplay movement stays
continuous. Enter never sends pause outside movie playback; a held movie skip
must release before it can select the next menu item.

## Rebinding

Open **Options → Controls** to rebind gameplay actions and adjust mouse options
in the original native menu. Gameplay prompt icons follow the selected bindings,
including the movement keys. Menus retain their fixed navigation controls and
default prompt artwork. See [native control settings](native-control-settings.md)
for assigning either binding slot, accepting changes and restoring defaults.

Mouse side buttons and extended keys without matching artwork use the yellow
question-mark icon. This includes Mouse 4/5, F13–F24, non-ANSI OEM keys and the
virtual-key separator; the separator is not the numpad Enter key. The Controls
menu still identifies each binding by its text label. The Menu key uses its
existing dedicated artwork.

## Rendering

The original `buttons` atlas in the frontend and shared character resources is
256×256 with 64px cells. Native artwork keeps those cells and the original font's
button selection, tint, fade and draw ordering. The final Im2D draw selects the
replacement atlas and crops full-cell glyphs to the visible artwork. Footer
prompts grow away from their labels: at 720p most icons are 44px high, Space
is 36px high, and movement groups are 48px high. Inline glyphs use their original
horizontal space to avoid overlapping nearby text. The game's source data and
cached texture bindings retain their original owners when input sources change.

`tools/compile_prompt_icons.py` creates the embedded RGBA8 artwork from the
user-supplied transparent sprite sheet with hand-drawn lettering and its matching
supplementary icons.
The original source sheet is preserved. The supplement supplies arrow keys,
mouse movement, G and a question mark with revised hand-drawn strokes. Four
additional sheets complete the standard 104-key US ANSI keyboard, including
distinct left/right modifiers and numpad labels. The embedded library has 113
icons; all existing enum values remain stable. Generation requests are saved
with the assets. Rebinding selects from this embedded artwork. The executable
needs no external PNG loader.
See [asset details and regeneration](../assets/native-input-prompts/README.md).
The earlier Xelu artwork and its original license remain in `assets/xelu-light`.

## Verification

Verified October 5, 2026: the complete keyboard artwork passes all 11 Python
asset checks, including independent 104-key coverage, stable original enum
values, source/crop hashes, transparency, proportions and exact regeneration.
The game and recorder build in native and release; both configurations pass
hardware/WARP rendering and the original atlas lifecycle checks. The original
user sheet is unchanged. The [full preview](../assets/native-input-prompts/yellow-full-keyboard-preview.png)
shows the compiled artwork; build/test receipts are in
`build/native-input-prompts/full-keyboard-20261005`.

Mouse menu fixtures exercise the original Apt point bounds, parent transforms,
visibility, typed movie ownership, slot-zero event mapping and original event
publication/dispatch. Compiled menu actions cover ordinary rows, settings,
popups, save slots, language choices, footer buttons and hidden/blank hits.
Pointer tests cover short clicks, captured press coordinates, focus loss,
controller coexistence, movie/modal ownership and release of camera capture.
Mapping checks include resized windows, ultrawide and 4:3 output, and the bars
outside the centered menu. Derived-package checks preserve original geometry
and assets while exposing the existing footer instance names. Constant tables
follow the original Apt unload traversal, so reopening a menu retains its scripts
and mouse behavior. Stationary wheel input moves the current selection repeatedly
without snapping back to the hovered row.

Verified October 5, 2026: the focused suites pass in native and release, including
962 original Apt CPU/ABI checks and 11 compiled mouse-action tests. A private live
run verifies popup No/Back, Pause/Options/Video, repeated wheel navigation,
bidirectional setting edits, Cancel/Accept, resized-window mapping and a second
Pause after reload. All primary save/profile/video hashes remain unchanged.
See the [final verification receipt](../build/mouse-fov-fix-20261004/final-verification.json).

Focused tests cover input aliases, first-click delivery, Escape/pause separation,
focus release and source selection. Native mouse motion reaches the original
input manager after connection-only probes without being consumed early. GPU
tests on WARP and hardware check all 16 atlas cells, alpha blending, enlarged
Space geometry and original binding preservation. Geometry checks cover label
spacing, resolution scaling and malformed quads. The original atlas lifecycle
fixture checks both resource copies and font UV cells against pinned hashes.

Verified September 26, 2026: the revised hand-drawn theme builds into the game and recorder
in both `build/native` and `build/native-release`. Four focused rendering,
texture-lifecycle and launcher CTest suites pass, as do nine Python asset tests.
Regeneration reproduces the header and provenance manifest exactly, with the
source sheets unchanged. The earlier native controller suite passes 1,118 checks.
See [revised-theme test results](../build/native-input-prompts/tests-revised-icons.log).

The rebuilt recorder reached gameplay and the pause menu using private save and
profile copies. Its [revised pause-menu capture](../build/native-input-prompts/smoke-20260926-224917-623064/pause-menu.png)
shows the hand-drawn RMB mouse and Space key without overlapping their labels.
Its recording validates 3,548 polls. The test child was closed; see the
[run receipt](../build/native-input-prompts/smoke-20260926-224917-623064/result.json).

## Recording and replay shortcut verification

All eight workspace launchers use the current native game or recorder: normal
play, automatic startup, first-mission rendering, automatic enemy defeats,
gameplay recording, first-mission recording, Resume Last Replay and the saved
Ball Homer replay. The Steam entry also points to the current native game.
Icons are embedded in the runtime, so every route has the same artwork and mouse
bindings.

Building `SimpsonsLauncher` also builds `SimpsonsNative`, which builds
`SimpsonsInputRecorder`. This keeps the launchers current together. Saved-crash
playback also uses the current recorder by default; its explicit
`--archived-executable` option retains access to the historical runtime for crash
reproduction. Archived files remain intact.

The launcher integration passes seven focused CTest suites and twelve saved-replay
tests. The startup-order regression creates the controller source before the
window, then verifies keyboard/mouse input and native prompts become available.
The Steam shortcut was checked read-only and already targets the current build.
See [launcher test results](../build/native-input-prompts/tests-all-launchers.log).

Replay preserves the selected native/controller prompts without changing the
recorded inputs, and live input selects the connected source again on handoff.

## Current limits

Some tutorials contain literal controller names in their text, such as
**[Select]** for the To Do List. That label means **Tab** (or Backspace) with
native controls. This change replaces button artwork; rewriting that original
tutorial text needs a separate integration with the game's text-token system.

Input tests verify mouse motion, buttons and release state. A live run recorded
nonzero relative mouse motion and clicks reaching the native input source. Its
cursor-restoration check was inconclusive because capture was already active
when the harness saved its baseline. The isolated camera check uses synthetic
stick input, so it does not establish physical mouse sensitivity or full mission
traversal. The October 5 [native mouse repair](native-mouse-feel.md) replaces the
stick conversion with direct raw-count rotation and restores prior capture after
menus. Its tests and bounded live verification are recorded separately.
