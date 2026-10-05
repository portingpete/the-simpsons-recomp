# Input replay handoff (development diagnostic)

This is a diagnostic tool for reproducing a route. It does not save or restore the current game world. For the direct Land of Chocolate test, double-click **Record First Mission Inputs.cmd**. Input recording begins automatically on the first ordinary game controller poll. Press **F9** to finish the recording. The recorder completes the current four-controller poll cycle and flushes the file. **F8** still starts or stops an ordinary recording.

To replay the latest completed F9 recording, close the current game and
double-click **Resume Last Replay.cmd** in the workspace. It selects the newest
complete first-mission recording,
copies its original starting save/profile into a private run, replays the input
prefix, and returns live keyboard control when the scene check passes. The
launcher skips recordings stopped with F8 and partial recordings. It keeps the
console open while the game runs. If the game fails, it reports the private
run's `game.log` path; closing the game window is treated as a normal exit.

The developer CLI can package the input prefix and replay it from the same initial profile/save. For an all-neutral recording, replay holds neutral input until the recorded endpoint scene, then returns live keyboard control. Recordings containing active input still require the scene count to be close to their recorded endpoint. Replay can diverge from the recorded run.

Commands:

```powershell
python -B tools/gameplay_checkpoint.py create bridge
python -B tools/gameplay_checkpoint.py list
python -B tools/gameplay_checkpoint.py resume bridge
python -B tools/gameplay_checkpoint.py resume-last
```

Checkpoint packages live under `build/gameplay-checkpoints/<name>`. Each holds a verified copy of the launch's initial game save and profile, the complete F9 input recording, and a manifest with file hashes and scene counts. Resume writes to a new private run under `build/input-recordings`; it does not overwrite the starting files.

The scene guard catches large timing drift but cannot prove the player and world are in exactly the same state. The 2026-09-23 run ending in the trail crash did not start input recording, and its on-disk game save did not change, so it cannot be restored from its crash position.
