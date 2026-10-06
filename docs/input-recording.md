# Record gameplay inputs

For a direct start in Land of Chocolate, double-click **Record First Mission
Inputs.cmd**. Recording starts automatically at the first ordinary game input
poll. Press **F9** to finish the input recording at a complete controller poll
boundary. This does not save or restore the game world.
This uses the recorder build and a private save copy. The ordinary **Record
Gameplay Inputs.cmd** launch still uses F8 to start recording.

The recording and resume shortcuts use `build/native/SimpsonsInputRecorder.exe`.
Building `SimpsonsNative` also rebuilds this executable so their controls and
native input icons stay current. Reopen a shortcut after updating the build.

1. Close your current game when you are ready to switch builds.
2. Double-click **Record Gameplay Inputs.cmd** in the workspace.
3. Load Continue Game and enter the level normally. The title says **F8 record inputs**.
4. With the game window focused, press **F8** before moving toward the crash.
   The title changes to **REC | F8 stop**. Keyboard and controller input both work.
5. Play until the crash, or press **F8** again to finish. A second recording starts
   a new file. Holding F8 does not repeatedly start/stop recording.

Each launch creates a folder under `build/input-recordings`. It contains:

- `inputs-*.jsonl`: input recordings, created automatically for the direct
  first-mission shortcut or when you press F8 in an ordinary recorder launch.
- `game.log`: the game's output, including recording start/stop and failures.
- `launch.json`: command, process ID, executable/image/save hashes and profile.
- `result.json`: exit code and recording filenames, written after the game exits.
- `initial-state`: a preserved copy of the starting save and profile.
- `content` and `profile`: private writable copies used by this test launch.

No menu automation is required. F8 is handled by the game window; it is never
sent as a gameplay button. The recorder captures every returned controller poll
for all four slots, including neutral/repeated states, releases, connection
status, packet number, buttons, both triggers and both analog sticks. Values are
recorded after keyboard/controller selection and movie/modal filtering; native
dialog input is marked separately. Timestamps are elapsed monotonic microseconds,
and sequence numbers count polls, not simulation frames.

New version-one recordings also include `mouse_native`, `mouse_x` and `mouse_y`
on each input row, identified by `mouse_camera` in the header. These capture raw
relative mouse counts separately from the neutral fallback right stick, allowing
the original camera's direct mouse path to replay. Only successful slot-zero game
polls can carry native mouse data. Old recordings without these fields continue
through the original controller path. Trimming a neutral prefix now checks mouse
counts too; the approximate `--keyboard` replay requires direct input playback
when raw mouse movement is present.

Raw mouse fields describe the snapshot at the input poll. If focus, capture or
menu ownership changes before the camera consumes that snapshot, live input
discards it. That later invalidation is not recorded, so playback can apply the
polled movement across such a transition. These fields do not promise an exact
camera trajectory through focus or capture changes.

Each complete line is written directly to Windows before the input call returns.
A process crash does not require stopping the recorder to retain earlier lines.
Power loss is not covered by that guarantee. Abrupt death may leave no `end`
record or an incomplete final line; future readers should retain complete lines
and ignore an incomplete tail. A normal stop or shutdown writes an end record
and flushes the file. A disk error stops recording, logs the error and changes
the title to **REC ERROR** while gameplay input continues unchanged.

This captures inputs, not video or an in-memory save state. For ordinary F8
recordings, start as soon as the same loaded level becomes controllable, before
moving. The native
`--input-playback <inputs.jsonl> --input-playback-start-scene <count>` option
replays the exact returned four-slot XInput polls, including packet numbers and
one-poll button edges. The start scene is the viewport depth-copy count at the
first recorded slot-zero poll; the recording itself does not contain that scene
number unless it was made by the new automatic first-mission recorder. Playback
rejects an unexpected poll order or malformed recording and ordinarily returns
neutral input after its final poll. The developer-only input replay handoff
explicitly opts into live controls after a scene-count check. It still needs
the same starting save/profile and direct mission launch. It is not a quicksave.

For the September 23 crash recording, `tools/replay_recorded_route.py` copies
the original initial save/profile and recording into a private run, starts the
playback at scene 233, hides that diagnostic window, and captures frames along
the route. Its `--keyboard` option retains the approximate scene-scheduled
keyboard replay for comparison. A replay reports success only after the rigid
VFX and Arcs bridge paths are reached; `--require-type5` also requires a live
type-5 particle draw. `--sparse-capture` reduces frame-readback stalls when
testing that route.

Build the recorder with the project's x64 ClangCL environment:

```powershell
cmake --build build/native --target SimpsonsInputRecorder --parallel 6
```

The separate executable shares the normal game runtime, allowing it to be built
without replacing an executable that is currently running. Any rebuilt native
game also accepts `--input-recording-directory <directory>` to enable F8.

Focused checks: `OriginalNativeControllers` exercises the actual window hotkey,
title indicator, keyboard/physical input and modal/movie filtering;
`NativeInputRecordingCrashPersistence` parses the file after forced process
termination; `NativeInputRecordingLaunch` verifies save isolation and snapshot
retention. `OriginalNativeStorageSelector` checks the native dialog regression.
