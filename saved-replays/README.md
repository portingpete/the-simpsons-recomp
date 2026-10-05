# Saved crash replays

`ball-homer-j-20260923-224246Z` preserves the complete September 23 Ball Homer
J-press crash run: 21,308 ordered controller polls, the starting save/profile,
private content store, original executable and DLLs, game image, and crash log.
The final controller cycle contains J (`XINPUT_GAMEPAD_X`, `0x4000`) after scene
5147; the run ends at scene 5148. The observed failure was `Native texture callback has an invalid owner
or ABI`.

Verify the archive before using it:

```powershell
python -B tools/saved_crash_replay.py verify ball-homer-j-20260923-224246Z
```

To replay, double-click **Replay Ball Homer J Crash.cmd** in the workspace, or
run:

```powershell
python -B tools/saved_crash_replay.py play ball-homer-j-20260923-224246Z
```

Playback uses a fresh private directory under `build/crash-replay-runs`. It
uses the current `build/native/SimpsonsInputRecorder.exe`, including its native
mouse controls and yellow keyboard/mouse prompts. It feeds the recorded controller
states from the first game poll and records whether the same failure occurs.
The archived executable, inputs and starting data remain unchanged. A missing
current build produces an error; it does not select the older binary.

For historical crash reproduction with the exact archived executable, run:

```powershell
python -B tools/saved_crash_replay.py play ball-homer-j-20260923-224246Z --archived-executable
```

This explicit mode requires the original failure to recur. To test another
build, use `--executable <path>`; it cannot be combined with
`--archived-executable`. Current and candidate builds can complete without
reproducing the old failure.

Playback reconstructs a route from the original disk save and recorded input;
timing or game state can diverge from the saved run.
