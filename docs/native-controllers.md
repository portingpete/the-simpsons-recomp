# Native Windows controller queries

The original four-slot input poll now reads Windows XInput1.4 state and
capabilities. Each request queries the operating system. The bridge preserves
the returned packet number, digital buttons, trigger magnitudes and signed
stick positions, converting only the original record's byte order. It applies
no deadzone or gameplay mapping. A missing controller returns1167 and clears
the output record unless slot0 has the owned keyboard or explicitly enabled
command source below.

## Game-window keyboard and mouse

The native game window supplies keyboard and mouse input for slot 0 when its
physical XInput controller is disconnected. The original input manager consumes
the resulting button, trigger, and stick values.

Open **Options > Controls** for the native keyboard, mouse, and original
controller preferences. The first two pages expose two bindings for every
gameplay action. Left/right chooses the primary or secondary slot; Enter or
select starts capture. Press and release the desired key or mouse button.
Escape cancels capture; Delete or Backspace clears the selected slot. A key
already assigned elsewhere exchanges its binding with the selected slot.
All five mouse buttons are supported. F6, F8, F9, and the Windows keys are
reserved. Accept changes saves the native settings; backing out restores the
keyboard and mouse settings from when Controls opened.

The third page includes mouse sensitivity (25–300%, in 25% steps), horizontal
and vertical mouse inversion, and reset keyboard/mouse defaults. The original
gamepad inversion and vibration options remain in the same native menu.
These preferences are stored in a separate, validated version-one
`.controls.cfg` file alongside the existing profile preferences.

The table below describes the defaults. Gameplay bindings can change, while
native menus retain WASD/arrows, Enter/Space confirm, Escape/K/right-click back,
and the original Tab/Backspace input. During gameplay, Escape remains a
pause/release recovery key even when the optional Pause binding is cleared.
Enter retains movie skipping.

| Default native input | Gamepad control |
| --- | --- |
| WASD | Left stick / movement; directional-pad repeat in menus |
| Mouse movement | Direct camera rotation from raw relative movement |
| Enter or Space | A / select / jump; Enter also skips movies |
| Left mouse button or J | X / attack |
| E | Y / interact |
| Right mouse button or K | B / special attack; back in menus |
| Left Shift | Right trigger / Homer Ball |
| Ctrl (either side) | Left trigger |
| Q / R | Left / right shoulder button |
| F | Left stick click |
| Middle mouse button or G | Right stick click |
| Escape | Start / pause in gameplay; B / back in native menus; releases mouse capture |
| Tab or Backspace | Back |
| Arrow keys | Directional pad |

Click inside the game window to use the mouse. The first click captures the
cursor and delivers its action immediately; focused mouse buttons also work
when capture is unavailable. **Escape** pauses during gameplay, backs out of
native menus, and releases the cursor. F6 is
an optional capture/release toggle. Focus loss or closing the window releases
capture too. Capture hides the cursor and confines it to the client area;
release restores its previous visibility, position, and clipping rectangle.
Resuming a menu restores prior gameplay capture. F6, focus loss, moving the
window, or changing its output extent cancels that automatic restoration.
The window title explains the click and release controls. Right click (or K)
performs the game's B action: special attack during gameplay, back in menus.

Foreground raw mouse motion retains device counts until the ordinary slot-zero
poll. The selected original orbit/look camera consumes the displacement once,
at its final angular input step. At 100% sensitivity each count rotates by
0.0025 radians. Native mouse inversion combines with the player's original
inversion settings. The camera retains its original pitch limits, yaw wrapping,
collision and follow behavior. Mouse input avoids stick deadzones,
speed limits, diagonal normalization, angle thresholds and frame-time scaling.
The fallback's right stick stays neutral; physical controllers and old recordings
retain their original stick path. Absolute raw mouse devices are ignored.
Capture generations discard stale motion across focus/capture/menu changes.
Integer accumulation preserves the thread's floating-point state. The original input
manager has one state query per slot per input update (`82321110` calls
`82B766A8`, returning at `82321114`). Connection-only queries with a null output
buffer bypass sampling, so they cannot consume pending clicks or mouse motion
before the manager reads them.

Only focused-window input is accepted. Short key or mouse presses survive until
the next game poll; key repeat does not create extra presses. Aliases are tracked
independently, so releasing J does not cancel a held left mouse button. Focus
loss clears held and pending input; the next focused click captures it again.
Fallback capabilities advertise all mapped buttons, both triggers, and both
sticks. Physical slot 0 takes priority, discarding pending native input while
connected. Closing the window retires the native input source.

`NativeControllers::usesKeyboardMouse()` exposes the slot-0 source for prompt
rendering without an extra device poll. Attaching the keyboard selects native
prompts by default; an ordinary or modal slot-0 poll switches to controller
prompts when a physical device is connected, and back when it disconnects.
Recorded playback preserves the selected live prompt set because its original
input-device identity is not recorded. A keyboard/mouse session therefore keeps
native prompts during replay; the next live slot-0 poll updates the prompt set
when a checkpoint hands control back to the player.

The controller fixture covers the mappings, raw-count accumulation and direction,
one-time consumption, alias release, focus loss, short presses, packet changes,
physical-device priority, prompt-source transitions, and source retirement.
It also exercises actual window click, Escape, and focus messages, and passes
native mouse snapshots beside the original input manager after repeated
null-output probes. A live foreground window is required to verify cursor
capture itself; the test reports when desktop focus is unavailable. Separate
original camera fixtures verify proportional displacement, batching/frame-time
independence, clamp/wrap, controller behavior and ABI. See
[native mouse repair](native-mouse-feel.md).
The focused `--controls-only` fixture additionally verifies rebound keyboard
and extra mouse buttons, fixed menu navigation, consumed capture until release,
conflict exchanges, clear/cancel, and focus-loss cancellation. A separate
settings fixture checks persistence, malformed-file rejection, defaults, and
mouse option cycles.

## Physical controller evidence

Original `823210B0`, manager singleton `82E36B84`, polls four slots through
`82B766A8`, which inserts flags0. State records are16 bytes, matching the
original byte/halfword reads at stack offsets54..5E. On connection the original
code calls capability wrapper `82B766A0` with flags1 and builds14 digital,
two analog and four axis entries. It compacts button bits0..9/12..15 into
original bits0..13, preserves trigger magnitudes and converts each signed
stick using its original integer expression
`((axis+32768)*2000/65535)-1000`. Endpoint values therefore remain-1000/+1000.
Original code also owns descriptor allocation, connection flags, disconnect
resizing, release and subsequent reconnection.

The native query source is owned by the Runtime. There are no emulated console
input objects, cached fake devices or synthetic production input. Tests can
substitute only the native OS query through a private friend seam. The498-check
fixture verifies independent expected record bytes, nonvolatile integer and
floating-point ABI, host last-error state, output bounds, invalid queries and
the actual original manager after its normal startup. Four slots undergo
three connected/released/disconnected cycles, including original allocation
changes, digital compaction, analog values and signed stick normalization.
Full manager destructor teardown is outside this milestone.

Capability output is20 bytes and preserves real device type, subtype, flags,
control resolutions and advertised motor resolutions. This advertises what
the physical controller reports; native vibration submission remains to be
implemented. Windows XInput1.4 is used because the older9.1 API returns fixed
capabilities. This machine currently reports all four slots disconnected.
Connected-state tests therefore prove ABI and original CPU consumption, not
physical controller interaction or controllable gameplay.

Qualified requests are slots0..3, state flags0, and capability flags0/1.
Unknown device queries and any-user indices fail explicitly. These tests verify
input conversion and original input-manager consumption; they do not establish
the camera feel or all gameplay actions in a live level.

Actual muted startup advances through the four controller queries and the
existing original loading-camera clear. It then selects another camera and
reaches the explicit native guard requiring the qualified loading camera.
Next trace that caller, camera ownership, target binding and paired end path
before extending the native renderer.

Primary API sources:
[XInputGetState](https://learn.microsoft.com/en-us/windows/win32/api/xinput/nf-xinput-xinputgetstate),
[state record](https://learn.microsoft.com/en-us/windows/win32/api/xinput/ns-xinput-xinput_state),
[gamepad fields](https://learn.microsoft.com/en-us/windows/win32/api/xinput/ns-xinput-xinput_gamepad),
[XInputGetCapabilities](https://learn.microsoft.com/en-us/windows/win32/api/xinput/nf-xinput-xinputgetcapabilities),
and [capability record](https://learn.microsoft.com/en-us/windows/win32/api/xinput/ns-xinput-xinput_capabilities).
Original bytes and read-only reference layouts are pinned by
`build/native-controllers/evidence.py`; original game/reference files are unchanged.

## Direct diagnostic input

Launch with `--controller-input <existing-file>` to enable a local command
channel. The normal launcher leaves it disabled. Existing file contents are
skipped when the process opens the channel. Append ASCII `START`, `A`, `B`,
`BACK`, `UP`, `DOWN`, `LEFT`, or `RIGHT`, each followed by a newline, after the
log reports that the channel is ready. Keep the same file and append to it;
do not truncate or replace it during the run.

Each command supplies one tap to slot 0 through the ordinary controller query,
with a release poll between taps. Commands work without window focus, combine
with the window keyboard, and yield to a physical controller. While a physical
controller is connected, queued file commands are discarded. Unknown commands
are rejected and logged. No game state or original input logic is altered.

For a run already launched with that file, send a tap using:

```powershell
python -B tools/send_native_input.py --file build/current-run.commands START
```

The sender reports that the command was queued. The game's log then reports
`local command tap buttons=0010` when its ordinary controller query consumes it.

Timed gameplay segments use `PAD <four_hex_buttons> <lx> <ly> <duration_ms>
[rt]`. Face buttons (`F000`) and DPad directions (`000F`) may be combined;
stick axes are signed 16-bit values, duration is 50–2000 ms, and optional right
trigger magnitude is 0–255. A changed button segment supplies a release poll.
The 123 ms house-exit Up press is covered at 122 ms and the exact 123 ms
release, including consumption by the original input manager. Physical input
still takes priority.
