# First-mission completion diagnostic

For development testing, run the native helper from PowerShell:

```powershell
& .\build\native\SimpsonsLauncher.exe --first-mission-completion
```

This diagnostic mode loads Land of Chocolate through the
original developer stream route, waits for its map initialization to finish,
and invokes the original mission-complete handler once. Bootstrap movies are
skipped; the completion outro and results remain enabled.

This reaches the transition without replaying the level. It is not a restored
completed-world checkpoint: the last persistent campaign save still describes
the beginning of Land of Chocolate, and input recordings do not restore a world.
Completion time and scores therefore belong to this shortcut run rather than
the user's earlier play-through.

Each launch creates a unique folder under `build/mission-completion-runs` and
copies the selected profile, save/content store and saved video preferences.
Original completion autosaves and settings writes use those copies. The
launcher preserves the run and its log after exit, including partial copies
when a copy fails. Source reparse points are rejected. See
[launcher details](launcher.md) for copying and log behavior.

## Original route and guards

The opt-in native argument is `--first-mission-completion`. Startup supplies
borrowed `-stream loc loc.str` arguments through the retail setter `828759B8`
before the original parser `8285F928`. The existing normal and first-mission
rendering launch modes retain their behavior; the two first-mission options
cannot be combined.

The hook at `823BB5D8` records a ready receipt only after original `823BB578`
has completed streaming and called map-start `8289ED68`. Its retained `r31`
must match the original gameplay owner. Qualification also checks the real
gameflow manager, Land of Chocolate episode, standard mode, current gameplay
package, `loc/loc.str`, original completion score event and Springfield
successor. A nonzero map pointer alone is insufficient.

The existing gameplay clock sample retries the request while the original
application is paused, exiting/restarting, playing/stopping a movie or retaining
a decoder. Once ready, it calls the actual retail `EpisodeComplete` helper
`8296FFC8` in an isolated original CPU callback context. That helper loads the
live manager and tail-calls original `8289EA10`; original score, map exit,
outro, results and next-episode processing remain in charge. No native code
manufactures campaign completion flags or scores.

The one-shot marker is set before calling the helper so the completion outro
does not inherit bootstrap movie skipping. If the original game naturally
publishes success first, the armed manager's `0x40000` success flag is recognized
before map/readiness checks, including directly from the movie skip predicate.
This preserves the outro even when map exit has already removed the map and
another clock update does not occur. Busy exit `0x1000` alone and restart
`0x100000` are deferred rather than treated as success.

## Verification

Both native and native-release game, recorder and launcher builds pass. All six
focused CTests pass in each build (2.65 and 2.54 seconds), including 386 CPU
completion checks. Logs are `build/mission-completion-launcher/final-native-tests.log`
and `final-release-tests.log`. AOT verification covers 311 files with zero
semantic diagnostics.

`OriginalFirstMissionCompletion` is a CPU-only fixture using the unmodified
mapped retail image. It pins the startup, streaming-ready, completion helper
and completion guard instruction bytes, executes the actual borrowed-argument
setter and retail name hashing, and runs the actual three-instruction completion
helper. Only the world-dependent `8289EA10` body is replaced by an observer.
The fixture checks ready/retry gates, natural completion after map removal,
immediate outro retention, one-shot dispatch and whole caller-context/TLS/host
floating-point preservation. It does not run a mission or write a save.

`NativeGameLauncher` exercises mode parsing, process command quoting, unique
run directories, exact store/video copies, private-write isolation, missing
optional video settings, drive/UNC path normalization, source/private copies
exceeding Windows' legacy `MAX_PATH`, and nested/root junction rejection. It starts neither
the game nor the launcher GUI. Profile CLI rejection checks also exercise
invalid completion option combinations before opening a profile store.

Live outro/results display remains a manual gameplay check. No automated
gameplay was performed for this change. Primary profile, save index and video
preference hashes remained unchanged; the hashes are recorded in
`build/mission-completion-launcher/source-store-hashes.json`.

The follow-up manual run reached the completion movie, then exposed an ordinary
dual-texture fallback guard defect. See the new crash repair and direct startup (local development record).
