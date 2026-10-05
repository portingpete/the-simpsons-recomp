# Recorded automatic startup

September19 replay update: Continue Game now accepts either a fresh opening
movie or direct resume into an accepted scene presentation. Route evidence is
bounded by the actual Continue command queue, after its neutral wait. A direct
resume emits route and sequence-complete events without sending a movie-skip
button. The movie route still checks fresh readiness, neutral input and skip
receipts. Capture requests drain existing observer requests and use exclusive
creation; neither helper removes another helper's pending request. Fourteen
route tests and seventeen gameplay-evidence tests pass. The rendering verifier
requires captured scene frames over ten seconds, but that result alone does
not establish character control or later crash-free operation. See
current live evidence (local development record).

Historical built replay: `reach-game-034`. The menu, existing save and movie skip
complete. Both shadow cameras finish31 draws; the static depth prepass finishes
133. The original rigid cache now supports16 records containing18 draws, and
the original immediate fallback completes27 draws across18 packets. The
textured rigid shader activates and uploads15 original mipmapped textures.
The next material, `simpsons_rigid_dualtextured`8202AD78, stops at82740680.
No gameplay frame or control was verified in that run. See
LIVE034 (local development record) for its build and tests.
The LIVE027 and earlier descriptions below are historical.

September13 rendering update: `reach-game-027` finishes both shadow cameras
(31 character/static draws and two full map copies) and133 static main-scene
depth-prepass draws. The next flags0 material is `simpsons_rigid`8200CCB8.
Its actual property flags40, bucket1 cache miss and eligibility1 select the
original recording-build path. Original rigid activation, recording begin,
CPU cache allocation, complete application-state resubmission and FX context
association now complete. Original826F39E0 also completes its three-row loop:
F0/F4 copied depth textures bind to deferred PS stages0/1, while the unused FC
edge texture is skipped. The first3530-index rigid mesh records one draw and
finishes a real native command list, retaining original CPU accounting and
context restoration. Original replay stages56+56 registers and completes both
copies per stage, then executes the native payload once. Its private scene
target remains all zero; the next object hits the single-record ownership guard.
Gameplay color and control remain unverified. Independent rigid recording GPU tests pass on WARP and
hardware, including replay with changed inherited constants and copied shadow
depth sampling. All160 tests pass in111.96s, including the independent
depth-binding probe on WARP/hardware, original material accumulation and
zero-work shared union qualification
(`build/reach-game-rigid-union-full-build-tests.log`).
The earlier two-draw main-camera capture contains62204 nonzero depth pixels.
See `native-character-mesh.md`, `native-static-shadow-mesh.md`,
`native-shadow-alpha.md`, `native-zprepass.md`, and `native-rigid.md`.
Older first-mesh and alpha-activation failures below are historical.

The current movie-skip build reached the main menu in 22.76 seconds and completed
the opening cutscene skip at 24.20 seconds in `movie-skip-002`. All four videos
skipped with real decoder cleanup. The existing character rendering failure
then occurred; entering playable gameplay remains unfinished.

For the developer diagnostic replay, run from the repository root:

```powershell
python -B tools/auto_start_native.py
```

The helper launches the existing normal native build with the recorded Player
profile and existing save folder, then sends Start and the A presses for the storage
notice, folder selection, autosave notice, save-slot selection, load confirmation,
and Continue Game. It now skips the three launch videos and the opening movie
with Start, after each movie player reports a neutral controller poll. Manual
Enter or controller Start also skips videos. Each skip retains the original
decoder shutdown and completion event; see [movie skipping](native-movie-skipping.md).

The recording uses the game's controller-command channel. Each input waits for
the corresponding menu screen, native storage dialog, or movie input readiness, and its delivery
is confirmed in the game log. It does not use fixed delays from process launch.
The only minimum delay between inputs is300ms to allow a250ms button hold and
the required neutral controller poll. Manual keyboard input in the game stops
the replay. A physical controller in slot0 also stops command playback.

The first recorded run reached Press Start32.25s after launch, Saved Games35.54s
after launch, and selected the slot35.73s after launch. Thus the recorded menu
sequence took about3.3s from the visible title prompt to Saved Games. Loading
also requires a separate confirmation; this was observed and added to the replay.

Each run creates its own folder under build/automatic-startup with inputs.jsonl,
controller.commands, game.log, launch.json, result.json and captured screen evidence.
Inputs.jsonl records screen observations, queued buttons and actual game receipts.
The opening movie's completed skip is also recorded. A later rendering failure
still stops the run and is reported as a failure; completing the input sequence
does not mean gameplay is working.
The stored screen cues are in config/startup_replay.json and need no external
image package. They distinguish the recorded screens and reject empty or
incomplete frames. Saved Games explicitly excludes its load-confirmation overlay.
The damaged-save notice is also recognized and stops playback. Recognition uses
a completed renderer readback even when Windows marks the game window occluded;
the native controller channel does not require desktop focus.

Existing-save loading and the main menu are now verified. The earlier damaged-save notice
came from a missing directory lookup: the file itself is114800 bytes and its
three checksums match. Native folder lookup now returns that actual size.
Subsequent live runs exposed the missing time-zone and calendar queries used
to display the save date; both now have native Windows implementations and
focused tests. Replay012 automatically reached the title at31.20s, Main Menu
at38.23s, pressed Continue Game at38.25s, observed the level movie at39.28s,
and delivered its movie Start at43.65s. At114.47s the first character-shadow
activation hit an unfinished native rendering operation (826B5FC0,
RenderShadowDepth). The recording reports that failure; gameplay has not yet
been verified. See native-storage-data.md and native-configuration.md for the
save fixes and native-shadow-depth-shader.md for the rendering boundary.
Replay013 subsequently passed shadow-shader activation and stopped at its next
missing character-parameter operation. Its result records main_menu_verified
and input_sequence_completed as true, and gameplay_verified as false.
Replay014 then passed the real six-bone character parameter update and reached
the next constant-upload operation at116.86s. Native constant upload now has
actual GPU readback coverage. Character mesh binding/drawing remains unfinished;
the automatic launcher does not report that boundary as successful gameplay.

The earlier main-menu cue recognized only the heading. Inspection of replay016's
39.004s frame found the profile-loading overlay still open, so those older cue
timestamps do not prove that Continue Game was actionable. The revised cue uses
the actual yellow Continue Game text. Its regression check rejects the captured
profile-loading overlay as well as the other recorded screens, black frames and
truncated frames. The original visually inspected Main Menu in replay011 remains
valid evidence; the earlier heading-only automatic cue was too early.
Replay018 verifies the corrected cue: the captured Main Menu at46.8756s shows
all five menu choices with Continue Game available
(build/automatic-startup/replay-018-main-menu/native-frame-1756535.png, directly
viewed). A was consumed at46.8977s, the movie began47.9926s, and Start was
consumed48.7788s. These input receipts follow the actual ready menu.

Replay017's recorder stopped because its first readback took longer than the
old10-second limit while the game continued starting. Capture now allows30
seconds and returns immediately on renderer acknowledgement; this adds no fixed
delay. The verified old test process was terminated before replay018 reached its
recorded menus. The native graphics implementation still stops at the first
unported character mesh stream binding after its tested bone/constant upload.
Replay018's game later exited at that same binding after the helper's observation
period; its result.json describes successful input delivery, with gameplay_verified
explicitly false. Its game.log records the later native failure. Profile, save
payload, save index and achievement hashes remain unchanged.

The requested Computer plugin was attempted first. Its JavaScript kernel failed
twice, including after reset, with “failed to write kernel assets”/Windows error3.
The native controller channel was used for the actual recorded inputs instead.

For diagnostics, tools/auto_start_native.py --until saved-games stops the inputs
at the populated Saved Games menu and leaves the game running there.
Use --until main-menu to stop at the verified Main Menu instead.
