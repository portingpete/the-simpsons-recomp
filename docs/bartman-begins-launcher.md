# Bartman Begins diagnostic

For development testing, run the native helper from PowerShell:

```powershell
& .\build\native\SimpsonsLauncher.exe --bartman-begins
```

This diagnostic mode launches Bartman Begins directly, using private stores.
Direct stage modes cannot be combined.

## Private saves and logs

Each launch copies the selected profile, content/save store and saved video
preferences into a unique `BartmanBegins-...` folder under
`build/bartman-begins-runs`. Original autosaves and settings writes use those
copies. The run's `logs` folder contains the game's combined output.

The source profile and saves remain unchanged. Each launch starts from a new
copy, so changes made in one private Bartman run are retained in that run rather
than carried into the next launch. Saved video preferences apply normally.

The launcher preserves logs and partial copies on failure, rejects source
junctions and symbolic links, and starts the game only after copying succeeds.
See [launcher details](launcher.md) for required files and copy behavior.

## Original startup route

The opt-in native flag `--bartman-begins` supplies borrowed
`-stream brt brt.str` arguments through retail setter `828759B8`, immediately
before original parser `8285F928`. The original loader initializes the authored
`BARTMAN_BEGINS` episode in `MODE_STANDARD`, using its `brt` map and stream with
no challenge checkpoint. This route does not publish Land of Chocolate
completion or manufacture campaign progress.

Bootstrap movies follow the existing original stop, decoder retirement and
completion callbacks. Automatic skipping ends only when the hook at `823BB5D8`
observes the original `823BB578` streaming/map-start completion. Its gameplay
owner must match retained `r31`; the manager, episode, standard mode, current map,
completion event, nil checkpoint and exact `brt/brt.str` names must agree.
After that boundary, later cutscenes retain normal playback and Start/Enter
skip controls.

## Validation status

Native and Release builds each pass all 138 selected tests (117.58 / 112.54
seconds), including the new launcher mode. `OriginalFirstMissionCompletion` has Bartman startup/readiness cases
covering the actual borrowed-argument setter, retail name hashes, owner/map
gates, caller preservation, and later movie policy. `NativeGameLauncher` has
file-only Bartman parsing, quoting, private-copy, isolation, long-path and
reparse-point cases. Neither fixture establishes a playable live mission.

The actual GUI launcher creates its private run, dispatches `--bartman-begins`
and reaches the original Bartman map-ready boundary with 268 positive-geometry
presentations observed before its owned window is closed. This smoke test's
receipt is `build/house-exit-fix/bartman-gui-launcher-smoke.json`.

The separate instrumented run `20261002-035032Z-020063be` uses the same current
native executable and original stage route. Personally viewed completed
captures show the opening cave, Bart and Homer, and the double-jump tutorial.
Eight acknowledged movement, jump, slingshot, interaction and character/ability
inputs complete. No unexpected failure occurs over 443.21 seconds and 12,602
presentations. Bootstrap skipping ends at the original map-ready boundary;
later-cutscene policy also passes the CPU fixture. This run does not play a
later cutscene or complete the mission.

All 254 original profile/content files and video preferences remain unchanged.
The tested executable matches the current native binary. Final live, build,
shortcut and store receipts are under `build/house-exit-fix`. Remaining
game-effect and engine qualification limits still apply across untested levels.
