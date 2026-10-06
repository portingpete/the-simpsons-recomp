# Direct native mouse camera

The previous camera input multiplied each relative mouse count by 1024, clipped
each packet to a signed stick range, and added an 8689-unit deadzone boost. This
lost fast movement, made opposing packets order-dependent, and passed displacement
through the original controller speed, time and minimum-angle processing.

Raw Win32 counts now remain independent of the XInput right stick. The selected
slot-zero camera consumes them once at its final angular input step, using 0.0025
radians per count. This removes the thumbstick deadzone, velocity limit, diagonal
normalization and frame-time scaling from mouse input. Original inversion options,
pitch limits, yaw wrapping, activity timers, collision and target-follow behavior
remain in the original code. Other player cameras, physical controllers and old
recordings retain their original paths. Camera blending admits the selected camera
so a fading-out camera cannot consume its movement.

The byte-pinned hooks are `8295CB18` inside orbit input `8295C9C0`, and `8296099C`
inside alternate look input `82960870`. Orbit writes pitch/yaw at stack +68/+6C;
look supplies f28/f29. Original `82A2A480` and `82960328` apply the angular limits.
Admission requires the selected camera and original character controller ID zero.
The original getter `823BE620` reads the character's controller assignment at
`+0x81C`. Unassigned ID six and other player IDs retain original behavior. Optional
`SIMPSONS_MOUSE_CAMERA_TRACE=1` diagnostics observe the original dispatch and
controller lookup without changing their results.
Original-image evidence is retained in
`build/native-mouse-feel-20261005/camera-source-evidence.json` and
`camera-original-chain.txt`. No original game files are modified.

Capture now resumes automatically after menus if gameplay had previously captured
the cursor. F6, focus loss, window movement and actual output changes cancel that
intent. A capture generation invalidates polled motion if focus/capture/menu
ownership changes before camera consumption. Disabled background windows never
capture the desktop cursor.

New input recordings add optional raw mouse fields to version one; old files remain
valid. Direct playback preserves the native snapshot independently of live focus.
Neutral-prefix trimming checks raw movement, and approximate keyboard replay refuses
to discard mouse motion.
The recording stores polled counts; it does not record a later capture-generation
invalidation before the camera consumes them. A replay can therefore apply movement
that the original run discarded during a focus or capture transition.

The original CPU fixture passes 135 checks through the real character getter,
input normalization, profile inversion and angular consumers. It covers small
counts, batching, frame/speed independence, opposing movement, pitch limits, yaw
wrapping, blending, controller priority and host/guest ABI preservation. In
particular, original `826BD964` negates normalized stick Y: positive Win32 raw Y
therefore retains the previous camera direction without another negation.

Both native and release builds pass all 12 focused controller, camera, capture,
recording and menu regressions; the standalone capture policy checks pass 46
assertions. The Python replay/checkpoint tests pass 27 cases, and AOT verifies
311 generated files with zero semantic diagnostics. Final source/binary hashes
and log references are retained in
`build/native-mouse-feel-20261005/final-verification.json`.

The private live run `20261005-130729Z-19408b96` verifies eight 40-count movements
at 0.1 radians each. Completed renderer readbacks show the camera moving 8.56 world
units while the player's position remains unchanged. The disabled background HWND
never acquired foreground or real cursor capture, closed normally, and left the
primary save/profile/content fingerprints unchanged. An original A-button prelude
assigns control to Homer before the neutral settling period; the direct stage
launch otherwise starts with controller ID six and correctly ignores camera input.
The reusable check defaults to this passing recipe:

```powershell
python -B tools/test_native_mouse_camera_live.py
```

The live receipt is under `build/native-mouse-camera-live/<run>/verification.json`.
Physical mouse feel and sensitivity remain subjective; this check supplies
synthetic raw counts without taking over the desktop. Full mission traversal is
outside the focused verification.
