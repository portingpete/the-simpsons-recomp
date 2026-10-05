# Original lifecycle routes for the restrictive-check audit

Source evidence is read-only. Input must use the visible original menu or actual gameplay; these addresses identify observations, not permission to manufacture messages, flags or deaths. The source image is `analysis/simpsons.pe`, SHA256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.

## Authored source identities

| Source | Exact resource | Payload SHA256 |
| --- | --- | --- |
| `simpsons_chars/simpsons_chars_global.str`, entry18 | `pause.swf`,119224bytes | `85acf09b0f8117f76661d6ae2e2ae2c59f5c70adec38fcfe85ba69c0b0363044` |
| `frontend/frontend.str`, entry 30, decoded payload offset 708528 | `popup.swf`, 156724 bytes | `45e6e7113d6487d18f8ca833e86ce74252a4777a22674fb5124214980f646e2b` |
| `frontend/frontend.str`, entry 30, decoded payload offset 456896 | `frontend.swf`, 225588 bytes | `973b276f05da7ef7298a0be7f415c26fa1bdbccd52f24f11237c74add0261721` |
| `frontend/text/e172a05c.str`, entry0 | `simpsons_global.en.LH2`,148693bytes | `5a7921d5dda95e58ab145f40d05d1c306d28b2acf857c7f3d2b99af0d0161310` |
| Loose retail gameflow | `simpsons_gameflow.lua` | `11a6872ed4ec510836cdc65a263544107e102a23de8e66a09e70ed2e9d0098b7` |
| Loose retail helpers | `simpsons_gameflow_helpers.lua` | `56c8bd124ef87456a8df7b36b9b9fabc0a4e1120885f4a5c2def38d395f74dc3` |

Archive entry 30 is a compound container, with a 4224864-byte decoded extent at file offset 5904384. Both named UIX resources above occur inside it: `frontend.swf` has chunk offset 456736 and `popup.swf` has chunk offset 708380. Entry index alone does not identify a named resource; preserve its name, decoded offsets and payload hash.

The pause payload's `apti` chunk starts at payload+`0x28`; its action data starts at+`0x30`. Locate the chunk rather than assuming Options' `0x20` data offset. Initialization is APT`0x17300..0x175AD`; `MenuItemIds` is built at`0x173B9..0x173C5`. Array construction pops the pushed arguments in reverse: `Resume_game`, `Restart_challenge`, `Save_game`, `Save_menu`, `Options`, `Controller`, `Drop_player`, `Quit_episode`, `Exit_game`. Initial `currentSelection` is0 (`0x175A1..0x175AC`).

The same original action program removes entries before `initializeButtons`:

| Item | Original removal condition | Action offsets |
| --- | --- | --- |
| `Quit_episode` | `!CanQuitEpisode() || !bPlayerOneInControl` | `0x1740E..0x17465` |
| `Restart_challenge` | `!IsChallengeMode() || !bPlayerOneInControl` | `0x17466..0x174BD` |
| `Controller` | `!IsPlayingDemo()` | `0x174BE..0x17501` |
| `Save_menu`, `Save_game` | Always | `0x17502..0x17531` |
| `Drop_player` | Player one controls the pause screen | `0x17532..0x17563` |
| `Exit_game` | Player one does not control it | `0x17564..0x1757D` |

Thus a standard story run controlled by player one, outside a demo and with a quittable episode, displays **Resume Game, Options, Quit Episode, Exit Game**. The original `IsPlayingDemo` binding`823AC620` returns false, so Controller is removed in this retail build. Use a current capture to verify removals and selection; do not assume a fixed number of Down inputs. The English labels occur at LH2 payload offsets`0xE363`, `0x6F75`, `0xE227`, and`0x1FCC0` for Resume Game, Quit Episode, Exit Game, and Restart Challenge. There is no authored Restart Checkpoint item in this pause menu.

## Pause, resume and exit

Original bindings are installed by`823A64B0`: `IsPlayerOneInControl`→`823A5DF0` reads pause owner byte`+0x8C`; `CanQuitEpisode`→`823A6290`; `ExitPauseScreen`→`823A5DF8`.

`CanQuitEpisode` calls`82898500` with manager`[82D08BA8]`. It requires a current episode (`manager+0xA0`), its root flag (`episode+0x24` mask`0x1`) clear, manager`+0x4B0` mask`0x10` clear, a root episode (`manager+0xC0`), and current episode different from root. The binding repeats the manager mask`0x10` test. The APT also requires player one to control the pause screen.

Input handler`823A6C78` handles original Select event6 in main selector1. The current selector is`[82D08E34]`, pending selector`[82D08E38]`. It compares `Resume_game` at`823A6E40` and selects2; `Restart_challenge` at`823A6E60` selects3; `Quit_episode` at`823A6E80` selects4; `Exit_game` selects12. Dispatcher`823A6580` copies pending to current and invokes the corresponding original screen program. Resume calls APT`ExitPause` at`823A6668`; native binding`823A5DF8` requests screen closure with owner byte`+0x94=1`, word`+0x98=3`. This is pause closure, not an episode exit or map reload.

Quit selector4 creates the original `$FE_Quit_Episode_query` popup at`823A68CC`. Its authored English question at LH2`0x23665` is: “Are you sure you want to quit this episode? If you exit now you will need to restart the level.” Popup callback`823A6380` forwards the actual selection to`823A5F48`. Current selector4 and selection low byte0 cause`8289BDA0` at`823A6038`; a nonzero selection restores main selector1. Original Yes/No localization strings are `[RDDown] Yes` and `[RDRight] No`; obtain a capture of the live popup before selecting, since an input hint is not a navigation index.

`8289BDA0` requests return to the eligible root episode through flags`0x4|0x100`, helper`828981F8`, and original GameMainLoop activation`8289A6B8`. Its alternate path selects `GameMainLoop` with its original stored return key. `8289A6B8` finds and activates the original loop, then calls`82860490`, which resets checkpoint state through`82953960`, calls the active gameplay owner's exit helper`823BB740`, and marks loop state`+0x10/+0x14=1`. The exit helper changes owner`+0x0C` to9 unless already9, disables/changes ticks and starts the original exit work. A return from any request helper is not completion.

Restart Challenge selector3 also activates this original GameMainLoop before closing pause. It is hidden for the ordinary `MODE_STANDARD` stage launches; do not describe a story route as having executed it.

Exit Game is a separate original choice, available even when Quit Episode is
hidden. Main-menu selection `Exit_game` requests pause selector12. Its actual
query callback `823A5F48` branch `823A6078` checks selection `r4` low byte0,
then calls `8289DB28` at `823A608C`, returned caller `823A6090`. The helper
resolves the authored `GameMainLoop` name at `82001F00`, updates original
save/score/mode work and calls `828602B0` on that actual loop. Loop activation
invokes its vtable method`+0x14`, then calls the active gameplay owner's exit
helper `823BB740` when `[82D08C34]` is present. A Yes selection therefore needs
the genuine popup capture/input, cached old-owner retirement at `823BBCE0`
with global owner cleared, and an original frontend successor capture. The
game-window close request is Runtime shutdown and cannot prove this route.

Direct `-stream` startup has a distinct return path. Original `828604E0`
converts the preceding InitOnceMainLoop key to DebugFEMainLoop (`F200280F`)
at GameMainLoop`+8`. Startup `82861F48` registers only InitOnce, Game and
Frontend loops. The genuine Exit Game query can retire the map, then request
this unregistered debug successor and fail the original indirect call at
`82860D00` (LR `82860D04`). The observed `A5A03BAF` in r4 identifies the old
Game loop; r8 retains the missing debug successor. Ordinary frontend-owned
Game entry preserves the registered frontend key `B92397EF`.

The frozen LOC run `loc-exit-owned-frozen-20261002/loc-1ca501e9` independently
viewed the real quit prompt and delivered Yes. Its cached ready owner and
generation match the `823BBCE0` cleanup receipt with the gameplay global
cleared. It then failed the null successor call; frontend return is unproved.
All five raw captures are completed renderer readbacks with
`display_accepted=false` because the window was occluded. The primary stores
were unchanged. These results belong to the frozen historical executable.

`tests/test_original_loop_routes.cpp` passed the Native and Release aggregates
and two independent cases (`loop-route-tools-native-tests.xml` and
`loop-route-tools-release-tests.xml`). It qualifies name hashing,
registration/removal, the complete borrowed frontend constructor/destructor,
and Game return methods in an explicit leaf ABI fixture. The Game creator's
three allocated strings, world tick, popup, map exit and frontend presentation
are outside that CPU fixture. Missing debug and malformed keys retain the
original lookup miss; no debug owner or null fallback is fabricated.

## Direct stage entry and genuine frontend Continue

The captured direct Tree Hugger pause menu contains Resume Game, Options and Exit Game. That narrower menu is consistent with the original root-episode gate. Standard-mode initializer `8289B610` clears manager `+0xC0` at `8289B6C0`; the developer stream caller reaches it at `8289D5E0`. Episode selection `8289BA10` republishes this root pointer at `8289BA54` only when episode `+0x24` has mask `0x2`. This is the authored `SetRestorePlayerPositions` flag used by SPR_HUB, absent from the initial Tree/LOC episodes. Standard mode clears challenge bit `0x10` at `8289B660`, so that bit alone does not explain the missing Quit Episode item. The actual ready receipt must retain `rootEpisode`, `episodeFlags`, `flags` and `currentEpisodeIsRoot` to establish the live reason.

`frontend.swf` main-menu initialization is APT `0x1F534..0x1F73A`. Its original array order is `Continue`, `Replay_episode`, `Debug_replay`, `Cliches`, `FMVBrowser`, `Options`, `Extras`. It removes Continue iff `!IsSavedGameAvailable()` (`0x1F5FA..0x1F63D`), Debug replay iff `!IsDebugEnabled()` and Extras iff `!AreExtrasAvailable()`. The script then runs `initializeButtons`, stores initial selection 0, and requests Continue if the saved initial selection is empty. Later native availability and the visible capture remain authoritative; this array is not a fixed navigation count. This program does not remove Replay episode using the Continue gate.

The `IsSavedGameAvailable` binding `8239C0B8`, registered at `823A0640..823A0664`, returns the negation of `828A7490([82D08D70])`. The latter returns true exactly when that original saved-game owner has word `+0x10 == 0` and flags word `+0x4` mask `0x1` clear. Preserve unreadable state as unknown; file presence alone is not this gate.

Original main-menu input handler `8239F6A8` handles Select event 6 and asks APT `getCurrentMenuSelection`. It compares the returned ID with `Continue` at `8239F6FC`, selects pending state 9 at `8239F70C`, writes `[82D08C9C]` at `8239FFD0` and calls `8239E140`. That dispatcher publishes current selector `[82D08C98]`; its selector-9 slot leads to `8239E4A4`, which invokes `8289DA90` at `8239E4AC`.

`8289DA90` checks the original saved-game owner again. When a game is available, it sets manager flag `0x8`, invokes genuine GameMainLoop helper `8289A6B8` at `8289DB08`, and returns true. The frontend calls `stopKeyListener` at `8239E4DC` and sets selector 8. If no game is available, it creates the original popup and installs callback `8239CD28`; acceptance low byte 0 re-enters the original dispatcher. The separate selector-7 profile/startup handler at `8239CA5C` can initialize standard mode (`8239CB04`) and queue the current game (`8239CB0C`), but is not a proven prerequisite of visible Continue. Do not manufacture either selector or infer that a request helper's return means loading finished.

The serialized gameflow listener `8289C860` receives an adjusted owner, `this = manager + 8`. For original record key `0xD911147C`, it restores checkpoint words from record `+0x120..+0x12C` to actual manager `+0x4A0..+0x4AC` at `8289CBC0..8289CBDC`. It resolves record `+0x4` through `828A50C8` and stores the resulting package at `8289CBF4`, adjusted-owner `+0xB8`, which is actual manager `+0xC0`. The store at `8289CC10` is adjusted-owner `+0xC0` (actual manager `+0xC8`) and must not be mistaken for the root pointer. This source establishes how a genuine restored game can recover a root; it does not prove a particular private profile contains one. Count Continue completion only after a genuine map-ready receipt with actual restored identity/checkpoint, then use its current visible pause menu to decide whether Quit Episode is available.

The native frontend input handler calls `823AC258`, which invokes authored APT method `isOkayToAcceptUserInput` (literal `82003398`) and requires returned text `true` (`821D5C9C`). The original Start selector is 6 and its screen label is `Start` (`820021E0`); attract selector 5 uses `Attract` (`820021E8`). A delivered controller command therefore does not establish that a frontend selection was accepted.

The original no-storage notice is `$TCR_NO_DEVICE_SELECTED` (`8200209C`). Dispatcher `8239DA58`, notification state `[82CD0EE4] == 1001`, builds that popup at `8239DB8C..8239DBE4` and installs callback `8239CD28`. Acceptance low byte 0 releases the popup and invokes `8239C968` at `8239CD70`. With current Start selector 6 and notification state still 1001, `8239C9E4..8239CA10` disables authored input and calls `823AAB28([82D08D78],8239B550)` to request the real storage selector; completion `8239B550` forwards to frontend dispatcher `8239CD90`.

In live `frontend-lifetimes-interactive-2/frontend-101f9ce0`, Done opened the separate native **Save storage** window: `game.log` line 46862 records a visible selector with the actual private content folder, `accept=1`, and `A=use folder B=continue without saving`. The game-front capture consequently showed only the blue TV background while this modal owner held input; it did not show the storage window. The actual `runtime/storage_selector.cpp` UI handles A as IDOK, rechecks the active profile and folder capacity, then publishes the original completion and UI-close notification. Require its `completed selected=1 device=1 result=0` receipt before counting folder acceptance, then require the next actual menu capture. Neither the notice dismissal nor a blank front frame establishes Main Menu or map readiness.

## Death and checkpoint state

Original `823BB888` examines the actual party actors, actor flags and health through their original vtable methods. A single character knockout can use the original player recovery path while another character survives. Whole-party failure reaches`8289DEE0` at`823BBAC8`; it is suppressed while owner byte`+0x13` or manager busy bit`0x1000` is set. Preserve available actor and action observations before this boundary; numeric actor-health telemetry is not currently implemented.

The triggering actor must have flags word`+0x350` bit13 set and health from its original vtable method`+0xB0` at or below the original literal at`82004454`, word`34000000` (FLT_EPSILON, approximately1.1920928955078125e-7). Every other present party actor must have flags word`+0x350` bit14 or word`+0x354` bit29; this loop does not read companion health. A surviving companion takes a separate score-update branch through`828A3100`/`828A31A8` and`82A0E4F0`, with predicate`8288DBE0` checking actor`+0x334 != FFFFFFFF`; these helpers do not establish direct actor revival. The current failure observer records owner/manager/checkpoint state but not individual actor health/flags. Exact immutable spans and route limits are preserved in`build/restrictive-audit/tree-death-route-proposal-20261002-source.json`. Genuine failure request receipt caller`823BBACC` identifies the whole-party producer. The ordinary death display schedules5.0seconds from the actual scene clock; the alternate challenge path uses0.5seconds. Literal words`821822B4=40A00000` and`821822B0=3F000000` establish those delays. The tick gate and later original reload remain required; a timed input route alone cannot establish death.

Gameplay message handler`823BC360` ignores owner states9/10. Actual registered `iMsgLevelRestart` (`[82D09854]`, literal`820044E8`, registration`823BBC70`) reaches`8289DEE0` at`823BC3A8`. Actual `iMsgEpisodeFailed` (`[82D09870]`, literal`820044D4`, registration`823BBC84`) first calls UI helper`823AF8B8`, then`8289DEE0` at`823BC5C8`. No audit route should send either message artificially.

`8289DEE0` ignores an already-busy manager, otherwise sets bit`0x1000`. It branches on the original episode's BusStopFail flag and current mode; ordinary story failure sets bit`0x10000` and enters`8289DDA0`. The latter schedules original tick work and a delay using manager`+0x4D4`. The original running-tick handler`8289EC04..8289ECA4` waits for the delay and global`[82E03390]` to clear. With bit`0x10000` it clears the bit and calls`8289BEA8(manager,1)`, which requests bit8, updates restart score events via`8289A730`, and activates GameMainLoop. The other path calls`8289DC10`, which enqueues the original map removal and mode/map restart work and sets bit`0x4000`. Do not assume every death takes the same map path.

`simpsons_gameflow_helpers.lua:91` only invokes `MapPkg:SetCheckpoint` for a nonnil authored argument. The standard LOC, Bartman and Tree Hugger initial maps use nil (`simpsons_gameflow.lua:70,84,108`); timed challenges have exact nonzero GUIDs (`:76,88,112`). Medal of Homer also has later standard map entries with authored GUIDs (`:234,238`). The original `-stream` branch at`8285FB34..8285FB50` passes `MODE_STANDARD`, empty checkpoint text and a null GUID. This establishes initial selection, not absence of dynamically reached world checkpoints.

Live checkpoint identity is manager`+0x4A0..+0x4AC`: `82898158` copies16bytes there. Genuine checkpoint code`82953558` compares/copies a world object's GUID with global`82E07D94`, then calls this setter at`829535F0`. `82953960` obtains the original zero GUID from`82745BF8`, writes it through`82898158`, then clears the four words at`82E07D94`. Package checkpoint is separately at map`+0x24..+0x30`. Log both; neither a pointer nor a nil initial package checkpoint proves the player's restored location.

## Eighteen-stage opening sweep with the final executable

`stage-sweep-stage6-20261003` (frozen `SimpsonsNative.exe` `79b519a5…a1504e`, windowed
720p private preferences, owned command channel, private profile/content copies,
primary stores unchanged) completes all 18 stages: each reaches the original
`-stream` map-ready boundary and delivers all 20 commands (A confirmations, jump,
attack, ability, interact, special trigger, forward attack, return, character change),
with zero failure rows and 42 to 56 seconds per stage. `tree_hugger`, `mob_rules` and
`dayofthedolphins`, which stopped at new rejections in the first sweep, pass. Earlier
attempts of this sweep are preserved: the stage-3 executable missed its 180 s deadline
on `spr_hub` (linear allocation scan), and the stage-5 executable stopped `eighty_bites`
at 4.5 s (recorded mono pass misrouted after a completed rigid replay). See the
[hot-path and provenance audit](audit-hot-path-and-file-provenance.md). Command delivery
does not prove a hit, pickup or destruction; checkpoints, deaths, later cutscenes and
mission exits keep their separate frozen-route receipts above.


## Lifecycle routes repeated on the final executable

The accepted-movie-Start and whole-party-death routes were repeated with the frozen
stage-6 executable (`79b519a5…a1504e`); receipt `final-live-lifecycle-routes-v1-receipt.json`
(SHA256 `929076606a02dc456d511591e16497201f70e5ed0d0a18ac617e3f9643f42eae`)
re-derives both ordered chains from the raw logs.

- **LOC movie skip** (`movie-skip-stage6-live-20261003`): original input-manager
  caller `82321114` accepts Start, the same movie owner (`E1A68A68`) reaches decoder
  stop at `826B92C8`, the decoder releases and completion dispatches, then LOC map-ready.
- **Tree Hugger death and reload** (`tree-death-stage6-live-20261003`, interactive
  queue: one neutral observation, a capture, close): map-ready (owner `E1AC2D08`,
  generation 1), the original whole-party request at `823BBACC` with that owner,
  original cleanup at `823BBCE0` with the global owner cleared, a fresh map-load
  request, and a fresh ready with a different owner (`E1AC3080`) at the same live
  checkpoint `A58E700D46285D71E8DB0F9790DDA768`. This run saw one death in 630 s (the
  earlier 497-executable run saw four in 182 s; neutral input varies), no failure rows,
  primary stores unchanged. It exercises the allocation index across an original
  map retirement and reload.

Terminal shutdown still reports incomplete original cleanup, so no complete
resource-lifetime credit follows, and a later reached checkpoint's world restoration
remains untested.

## Extended wander routes, later deaths and the completion transition

The [extended wander routes](audit-wander-routes.md) (255 seeded commands per stage, two seeds) run on the
final stage-9c executable on all 18 stages with no failure. They found and forced four repairs on earlier
executables. Across the two final sweeps the raw logs hold **45 failure requests: 39 whole-party deaths
(original `823BBACC`) and 6 from another original caller (`8289EB94`, bargainbin and eighty_bites)**, each
followed by original gameplay-owner cleanup (`823BBCE0`, global owner cleared), a reload request and a fresh
ready at the next generation: **43 complete chains**, two interrupted by the end of the run. All
complete chains restore the stage's initial checkpoint; none reached a later one, so later checkpoint
restoration is still untested.

The `--completion` runner mode launches the native first-mission completion shortcut, which calls the
original `EpisodeComplete` helper once after Land of Chocolate is ready. The recorded sequence is LOC
ready, `loc_igc02.vp6` (the outro) from input-ready to its natural decoder stop, the results pages
(A confirmations), the `spr_hub` load request, cleanup of the LOC owner and `spr_hub` ready. That is a
real original mission-exit transition, not a naturally played completion (the results show 0:00.00).
A Start delivered 0.3 s after the outro's input-ready is **accepted by the original input manager route**
(`controller-movie-start` from `82321114`, then the original decoder-stop request from `826B92C8` instead of the
natural-end `826B93E0`), after which the results pages and the `spr_hub` ready follow. Three earlier attempts
were mistimed by the runner's lagging view of the 1 MiB-buffered game log and prove nothing; see
[the wander routes](audit-wander-routes.md).

## Receipts that establish completion

Mission attribution can be observed before ready without advancing the original
load. Queued request dispatcher `82899828` reads operation/package/argument from
the original 12-byte record in r31, selects the callback pair at
`manager + 8*(packageType+2)`, and calls it at `828998C0` (LR `828998C4`). Whole
manager constructor `8289DF78` installs `(8289ADF8,0)` at manager`+0x18` for map
type1 (`8289E264`, `8289E27C`, `8289E2AC`). At callback entry the unadjusted
manager is r3, operation is r4, package is r5 and the third record field is r6;
the dispatcher retains manager in r30 and record in r31.

Operation0 publishes that actual package to manager`+0xAC` at `8289AED4` and
`+0xA8` at `8289AEDC`, then reads its stream`+0x1C` at `8289AEFC` and folder
`+0x14` at `8289AF14`. It requests the original GameMainLoop stream via
`8289A2C8` at `8289AF2C`. The entry observer records `phase=map-load-request`
before those writes or stream work. Only the source-matching caller, global
manager/type, callback pair, record arguments and readable type1 package with
bounded nonempty folder/stream can change audit mission attribution. It never
publishes a gameplay ready owner, sets a checkpoint or changes admission.
Other map operations and unknown/malformed snapshots retain the prior audit
mission. Full PPC state, host FP control/status and LastError are preserved.
The existing filesystem observer separately attributes a successfully opened
exact top-level map stream; it already precedes ready and remains unchanged.

`tests/test_first_mission_completion.cpp` includes metadata-only good,
unpublished/repeated request, wrong caller/manager/callback/record/package,
bounded unknown and foreign-runtime observer cases. They do not execute the
world callback or prove a real load. The exact source extents, hashes and all
127 fixture pin words are in
`build/restrictive-audit/early-map-load-source-proof-20261002.json`; the new
observer and fixture passed the coordinated Native and Release focused runs.
The complete Native suite passed 493/493. These metadata cases still claim no
world-load completion.

* The genuine post-streaming helper`823BB578` publishes owner state0 and ready byte`+0x11=1`, then calls map-start`8289ED68` at`823BB5D4`. Its epilogue`823BB5D8` is the map-ready observation point. Capture owner`r31`, live global`[82D08C34]`, manager, current package`+0xA8`, map`+0xAC`, type, actual folder/stream, episode/mode identities, manager flags and both checkpoint GUIDs. An unqualified/unreadable snapshot is unknown.
* Genuine deleting wrapper`823BBCC0` calls derived cleanup`823BA590` at`823BBCDC` (the mapped-image direct-branch census has no other caller). Cleanup releases/unsubscribes its original resources, clears global`[82D08C34]` at`823BA7AC`, and calls base event cleanup`82690790` before returning. Observe at`823BBCE0`, after cleanup and before optional guest free`8269BEB0` at`823BBCF0`. This proves the gameplay owner retired; it does not by itself prove every native resource family balanced.
* Match retirement to a cached identity from that owner's genuine map-ready receipt. The manager may already point to the successor, so resolving the old map from current manager state would misattribute it. Addresses/generations belong in instance context, not combination groups.
* A reload requires the observed original request/death path, old-owner retirement, and a later genuine ready receipt for the actual loaded package, plus a visible restored location. A quit requires the original accepted popup, old-owner retirement, and original successor/frontend readiness. Resume requires actual pause closure and returned gameplay input, without inventing a map reload.
* Record audio codec stop, pending cancellation/join and source-owner release alongside these map receipts. Existing focused audio create/use/release tests cover reader/claim/worker/reset contracts; they do not certify death or mission-exit world teardown.

The prior stage-ready logging was one-shot and matched only the requested stage; it could not prove a second ready event or a successor map. Observation-only lifecycle receipts must run before that latch and must emit each boundary even when its stable combination matches an earlier receipt. They do not change readiness admission, dispatch an event, set a checkpoint or turn shutdown cascades into successful releases. Exact extracted-resource provenance, all 125 decoded original pause initialization actions, decoded frontend main-menu initialization and boundary instruction pins are preserved in `build/restrictive-audit/lifecycle-source-evidence.json`.

## Movie identity and startup progression

Shared movie setup`826B8B70` copies its descriptor RwString, appends`.vp6` and normalizes through a128-byte local buffer before original`82743600` copies it back. Owner`[82D09750]` has filename buffer pointer`+0x1C`, uint16 length`+0x20` and uint16 allocation capacity`+0x22`. Original`82743600` copies the byte length excluding NUL, writes the terminator at`buffer[length]`, and stores the length. The audit reads only a nonempty printable byte string of length at most127 with larger capacity and a readable terminating NUL. Unreadable, oversized or malformed diagnostic identities remain unknown without changing movie admission. Flags are owner`+0x2C`, callback object`+0x30`, state`+0x14`; addresses and allocation capacity are instance context.

Successful-start boundary`826B926C` occurs after original`826B91E0` publishes state2, before it releases its112-byte stack frame. Its saved original caller is at currentSP+104, as established by original`__savegprlr29`; the usual creator return address is`826B95A4`, with the original repeat path a separate caller. Raw hook LR is internal and is retained separately. End boundary`8282D998` is entry to decoder stop, before its actual worker wait and release. Its receipt is **decoder-stop-request**, not decoder retirement or movie completion. Existing native skip completes real`826B9290` stop and`826B8AD8` completion, then logs the separate actual completion receipt. Natural EOF follows original`826B92F0`; loop flag0 can repeat the movie, while completion frees both filename strings and emits the actual MovieEnded event.

Frontend owner`[82D08C94]` word`+0x80` is the movie index, not an asset pointer. The registered `GetNextMovieToPlay` binding`8239B140` reads that exact word;`823A0208` increments it and requests Movies selector3 until the index exceeds2. Initializer`8239BA58` resets it when current selector`[82D08C98]` differs from3. Authored frontend arrays select EA, Fox and Gracie in original array construction order, with widescreen/SD variants. Filename, frontend current/pending selectors and movie index must be observed together before attributing a replay. Repeated heap pointers in the earlier skip route do not establish which file played or why startup repeated. The new mapped observer regression exercises the original RwString copy and bounded/unknown snapshot behavior; it claims no actual movie playback or decoder lifecycle completion.

## Observed normal Continue and accepted Exit

The owned natural-boot run
`build/restrictive-audit/frontend-owned-493-lifetime-retry-20261002/frontend-4985b5e6`
executed the frozen seven-file Native scope, executable SHA256
`11385581244ed2e01e065cf54bf09f23975ca7e3e36b6dc21f4af99715ea06a1`.
The actual saved-game selection and Main Menu Continue reached LOC ready
sequence16216, owner`E1AC2C80`, generation1, live checkpoint
`3A7C2CC747D73EF90D70EF8F30129413`. Original bootstrap movies completed
naturally; no skip was delivered during them.

Independent captures establish the pause menu, selected Exit Game and its
confirmation (`native-frame-16602717`). A separate delivered A accepted Yes.
Original cleanup sequence5590173 at`823BBCE0` retained the same owner and
generation, `cachedReady=true` and `globalOwnerCleared=true`. The later
`native-frame-17212716` shows the actual Main Menu. The source-pinned normal
Game return key selects the registered `FrontendMainLoop=B92397EF`; its class
identity is source-qualified, while the live evidence directly proves the
retirement and visible successor. The earlier direct DebugFE null-successor
failure remains a distinct historical result.

The second actual Continue reused address`E1AC2C80` as generation2, ready
sequence5596578. It restored the same saved checkpoint; it is not a death or
checkpoint reload. The rendered Things To Do list says “Follow the White
Rabbit through the village.” Subsequent movement and jump controls reached
the original Jump tutorial and a platform, but no genuine failure request,
generation2 retirement or later ready occurred. Enemy contact, pickup,
destruction, death and respawn remain unproved by this run.

The runner closed only its owned window at1800.058seconds. All22 inputs and
25 native readbacks are preserved. Its deadline result remains unsuccessful,
without invalidating the separately recorded accepted Exit endpoint. The
original profile/content/video snapshots and all seven frozen files were
reverified unchanged. Terminal native releases explicitly report incomplete
original cleanup and receive no complete resource-lifetime credit. The audit
mission string also remains `loc` after the visible Main Menu return in this
frozen build; those later frontend rows cannot be credited as LOC gameplay
solely from that string.

Exact commands, hashes, captures and all eight map boundaries are retained in
the run's `normal-continue-exit-endpoint.json`, the parent
`route-evidence.json` and `final-owned-route-receipt.json`. The earlier
900-second partial run and rejected1800-second attempt remain preserved;
the retry used the separately hashed finite20..1800 runner revision and its
10 passing boundary/unit regressions.


## Accepted movie Start in the 497-test frozen runtime

The separate owned run`build/restrictive-audit/controller-mission-matrix-movie-skip-live-20261002/loc-901ec490` used executable SHA256`4813bebf4a0e741a3596f8268cb746fa55ca7dcf87012c25554c4091e0cdbba0`. A completed accepted intro readback shows actual movie playback (`movie_draws=3370`). Real input-manager caller`82321114` produced fresh Start sequence28112, with raw buttons0010 and returned buttons0000. Decoder-stop sequence28116 at original caller`826B92C8` retained the same owner`E1A68A68`, filename`movies\en\loc_igc01.vp6` and accepted Start last action. The original decoder released and completion dispatched before LOC map-ready sequence28120. This independently verifies the new diagnostic action through the actual movie manager and stop/completion path.

The88-second run closed only its owned PID13924 window. Primary profile/content/video and all seven frozen files remained unchanged. Its891 raw rows, launch/result, three accepted readbacks and derived previews are hash-bound in`final-movie-action-receipt.json`, SHA256`d8a99beae751352bf6a8089118220e0d54581468b6054f7cc7befbdbb0205229`. No original mission exit, gameplay-owner cleanup or death/reload occurred; those lifetimes remain separate. The new matching-owner mission-retirement attribution has CPU preservation coverage in the497/497 Native and Release suites, while its actual frontend-return logging is still unexecuted.


## Observed Tree whole-party death and reload

The owned497-scope run`build/restrictive-audit/tree-neutral-death-497-live-20261002/tree_hugger-86424185` closed successfully after181.79seconds, PID73908. It used the same frozen executable as the movie run and unchanged private-store isolation. Four genuine whole-party producer requests at`823BBACC` had cached matching owners/generations and flags2001 with busy1000 clear. Each was followed by matching`823BBCE0` cached cleanup with the gameplay global cleared, a source-qualified operation0 request and fresh Tree ready. Generations1→2→3→4→5 establish retirement/reload even where the owner address repeats; three cycles reuse the same address.

The cleanup receipt retains the retiring Tree identity. All intervening receipts before each new qualified load use mission`unknown`; the new request restores`tree_hugger`. This gives actual live coverage for the new retirement-attribution observer. Four completed accepted captures show the closed Auntie Nature gate encounter, worker melee and the same authored location after the observed reloads. Only one neutral PAD0 observation was delivered; actual combat and the original party checker produced the requests without health/checkpoint writes. The diagnostic last action remains`startup` because neutral input does not supply a new action. All four failures share one stable combination group while owner/generation remain instance data.

Every ready and pre-failure receipt has live checkpoint`A58E700D46285D71E8DB0F9790DDA768`, while each operation0 request briefly has zero live checkpoint. These are original death-triggered map reloads returning to the same ready checkpoint/opening, not proof that a later reached checkpoint restores its world state. No per-actor numeric health, dying-frame capture, or generation timestamp in renderer metadata is claimed. Gameplay-owner retirement also does not establish full audio/FX/texture/geometry/GPU teardown; terminal incomplete cleanup lines remain preserved.

The1251 raw rows, four complete create/failure/cleanup/request/ready chains,8 post-retirement mission-unknown rows (plus3 pre-owner startup unknown rows), captures, original11-span verification, owned closure and unchanged primary/frozen hashes are bound in`final-death-reload-receipt-v2.json`, SHA256`bc80b2e73a2761140e1eb2cd8a6d052a4aa6a548db0fb4f2d6734a10dfbda9c5`. The preserved first receipt mislabeled11 total unknown rows as post-cleanup; v2 corrects that count while preserving every original raw row and cycle. The earlier no-death Tree run and source-only proposal stay unchanged.
