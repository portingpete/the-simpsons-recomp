# Extended wander routes and the defects they found

The 20-command opening routes ([lifecycle routes](audit-lifecycle-routes.md)) reach the first
seconds of a stage. This wave runs much longer, repeatable routes on all 18 stage launchers
and treats every live failure as a possible restrictive check that rejects a valid original
input: reproduce it, find the original behavior, repair the handling, keep the malformed
rejections, and verify with a test that fails first.

## The routes

`tools/generate_audit_wander_route.py` writes a seeded route: four A confirmations, then 40
segments of (walk in one of eight stick directions, walk plus attack, attack, jump, and by
segment index the ability, interact, the special trigger, a character switch) each closed by an
A confirmation. **255 commands**, bounded PAD values only, no START or BACK. Delivery proves
neither a hit, a pickup, a destruction nor a defeat; those need their own receipts.

| Route | Seed |
| --- | --- |
| `config/audit-routes/extended-wander-v1.json` | 20261003 |
| `config/audit-routes/extended-wander-v2.json` | 20261004 |

Command: `python -B tools/audit_stage_routes.py --route <json> --timeout 900 --receipt-timeout 30
--baseline-video --executable <frozen exe> --output <new dir>` (about 340 s per stage, owned
process, private profile/content copies, primary stores unchanged). Log markers (`wait_for_log`, the movie and completion triggers) lag the game: its stderr is a
1 MiB full buffer that only a delivered command receipt flushes, so a time-critical action must not wait on a
marker alone (see the outro skip below). Manual play through the
interactive queue was tried first (Tree Hugger, the Simpsons' house) and abandoned: the stick is
camera-relative and each action costs about a second.

## What the first sweep found (frozen stage-8 executable)

`build/restrictive-audit/wander-sweep-stage8a-20261003`: **12 of 18 stages complete**; six did not.

| Stage | Failure | Reproduction |
| --- | --- | --- |
| `mob_rules` | `Native edge effective draw state is unqualified` (about 40 s) | every run (3 of 3) |
| `bargainbin`, `bigsuperhappy` | `Rigid alpha inherited eye differs from original WORLD_EYE_POS source` | bargainbin 2 of 3 runs, bigsuperhappy 1 of 3 |
| `gamehub` | `Immediate vertex is nonfinite` | 2 of 3 runs |
| `neverquest` | `Unknown or stale native material identity` | 4 of 7 runs |
| `eighty_bites` | route stopped: frame rate fell to about 1 fps for over 10 s (an unacknowledged command) | once; a rerun with in-process sampling was clean |

Intermittent ones were first given diagnostics: a temporary instrumented build printed the
values at three sites (`wander-diag-sweep-20261004`, `wander-diag-sweep2-20261004`), and the
`neverquest` message and its binding-reset annotation became permanent so the next occurrence
named its cause.

## The four repairs

Each repair has a baseline in which the new test fails against the old behavior with the live
message (`stage9-baseline-v2.log`), a mutation check in which every deliberate defect fails
a test (`stage9-mutation-check-v1.log`, 17 of 17; `stage9b-mutation-check-v1.log`, 2 of 2),
and a live rerun.

1. **Edge draw state (`mob_rules`).** The captured state was the screen policy with an
   unblended replacement, selector 3, full sample mask and **ExpandedBlend0 set**. Retail's
   unready sprite branch skips the expanded-blend reset (existing fixture comment), every other
   pass family already accepts `expandedBlend<=1`, and an unblended overwrite cannot depend on
   blend precision. `EngineState::requireOriginalEdgeState` accepts and reports the request;
   depth write, alpha test, blending, separate alpha, selector, sample mask, tessellation and
   additional expanded attachments still reject (`NativeEngineState` group 12;
   `OriginalEdgeParameters` cycle 3 retains the request through the real edge draw).
2. **Rigid alpha eye (`bargainbin`, `bigsuperhappy`).** Captured: staged (79.9158, 58.1824,
   34.5013) against the manager's moved eye (77.7036, 52.3428, 27.0014). Retail never rewrites
   an excluded PC4, so the register keeps the completed stage upload's value; the equality
   against `manager+0x230` is removed (the lanes must still be finite). `OriginalRigidBasePass`
   moves the manager eye after the stage upload and requires the same pixels.
3. **Immediate vertices (`gamehub`).** Captured: vertex 0 of a four-vertex dual-texture strip
   with every position and alpha lane `FFC00000` (quiet NaN). The original GPU discards a
   primitive that uses a non-finite position, so such triangles are now dropped (non-radial,
   non-projected draws); everything a surviving triangle consumes must still be finite, and
   radial and projected draws keep their all-vertex validation. That the producer emits such a
   vertex is inferred from the live capture; the producer itself was not examined.
   `NativeImmediateWARP/Hardware` cover strips, lists, each lane, infinities, an all-culled draw
   and the rejection controls.
4. **Binding reset (`neverquest`).** The failure named `vs=E3E97180 ps=E3E9B1D0` (shared shader
   object addresses, words `00000006 00000001 ...`). The reset kept a private 22-entry copy of
   the 25-entry screen-shader table, so the first reset after a projected billboard (slots 22 to
   24) rejected its own cached shaders. One shared table now serves qualification and reset.
   `OriginalScreenBridge` reproduces the identical addresses under the old table (mutation) and
   keeps rejecting a foreign cached shader.

The slow starts and the `eighty_bites` collapse were not reproduced and have no known cause.
The runner now has `--receipt-timeout` (5 to 60 s) so a stalled game is recorded rather than
ending the run at 10 s.

## Final executable

`frozen-stage9c-native-20261003`: **seed 1 completes 18 of 18 and seed 2 completes 18 of 18**
(255 of 255 commands each, no failure rows, primary stores unchanged); `neverquest` completes
three more times. Against the 18 opening routes, seed 2 reaches (`stage9-wander2-live-encounters`
join, every pass joined to the original catalog):

| | Opening routes | Seed 2 |
| --- | --- | --- |
| Distinct shader passes | 27 | 29 (adds `simpsons_skin_gloss` and `simpsons_skin_flipbook`) |
| Distinct files opened | 368 | 704 |
| Rigid row receipts | 10,696 | 33,384 (largest table still 19) |
| Audio source receipts | 4,684 | 38,055 |
| Geometry uses | 9,848 | 47,712 |
| Character voice-line streams opened (`d_<speaker>_xxx_<n>.exa.snu`) | 27 in 9 stages | 86 in 11 stages (87 for seed 1) |

The voice-line row is dialogue **audio** only: the stream was opened and its blocks admitted against the catalog
(`stage9-voice-lines-v1.json`, from `count-voice-lines-20261004.py`, input hashes inside); that a line was audible or
that a dialogue event happened on screen is not shown.

Seed 2 also logs **24 failure requests: 21 whole-party deaths and 3 from another original
caller (8289EB94)**. `verify-lifecycle-chains-20261004.py` re-derives every chain from the raw
logs: 23 are complete (ready, request, original cleanup with the global owner cleared,
reload request, fresh ready at the next generation) and one run ended during its reload. All
23 are at the stage's initial checkpoint; no later checkpoint was reached.

## Mission exit through the completion transition

`--completion` launches the native first-mission completion shortcut (`--first-mission-completion`),
which calls the original `EpisodeComplete` helper once after Land of Chocolate is ready: the
transition is real, the completion is **not naturally played** and the run is labeled so. The
played route (`completion-played-stage9c-20261004`) records the original sequence: LOC ready,
`EpisodeComplete`, outro movie `loc_igc02.vp6` to its natural decoder stop, the Springfield Shopper
results ("Great Job, Simpson", time 0:00.00) and collectibles pages with A confirmations, the
`spr_hub` load request, original cleanup of the LOC gameplay owner, and the `spr_hub` map ready
(the Simpsons' living room).

**Skipping the outro works through the original input manager.** Three early attempts
(`completion-skipped`, `completion-autoskip` at 2 s, `completion-autoskip-b` at 0.3 s) delivered Start only
after the movie's natural decoder stop; that was the runner, not the game. The game's stderr is a 1 MiB full
buffer (`app/main.cpp`), so the runner's view of the log trailed the movie by many seconds and its trigger fired
late. Every delivered command receipt flushes that buffer, so `--outro-skip-after` now also sends a neutral
50 ms PAD every 0.5 s (a log pump; neutral input also arms the original movie skip). With it
(`completion-autoskip-c-stage9c-20261004`, Start 0.3 s after readiness) the logs hold the accepted
`controller-movie-start` row (caller `82321114`, raw buttons `0010`, returned `0000`), the original
decoder-stop request for `loc_igc02.vp6` from the skip path (`826B92C8`, not the natural-end `826B93E0`),
the results pages, the `spr_hub` load request, LOC owner cleanup and `spr_hub` ready, in 78.6 s against 206 s
played through. The three late attempts are preserved as mistimed runs; they say nothing about the game.

## Limits

- Opening plus wander routes only: no pickup, destruction, dialogue, ability-hit or enemy-defeat
  receipts exist; command delivery and encounter counts do not prove them.
- Deaths and reloads were all at a stage's initial checkpoint.
- 29 of the 110 catalog passes have been encountered; the other 81 (63 of them without native
  artifacts) are untouched.
- Terminal shutdown still reports incomplete original cleanup, so no complete resource lifetime
  is claimed.

## Evidence

| Configuration | Receipt SHA256 | Scope SHA256 | Aggregate |
| --- | --- | --- | --- |
| **Native, stage 9 (final)** | `24de4b4ac9756dece793d8c622deb4cfdaac613cdb273a717c54167a2165c89c` | `6b30ed83…3105c9f9` | **527/527, 412.90 s** |
| **Release, stage 9 (final)** | `243dfefdc5d39b87415bb9aaa270051bbc050d389795b4a373fc5130b7d8ce11` | `ae5b9bfc…d8366df1` | **527/527, 397.73 s** |
| Native, first stage-9 receipt (superseded) | `55e9f1e95a58a874f486d498824585ff148b25721090f1bb94c069cf55c7bd61` | `86133842…9b4a4900` | 527/527, 400.46 s |
| Release, first stage-9 receipt (superseded) | `7daa330951be31999c625238c8e5b9afd6fdc07c7b8fda619aeb0662d09c59ea` | `38b8215a…85ae9e7c` | 527/527, 413.72 s |

The receipts re-derive the 24 rigid row cases of the earlier waves from this aggregate's raw logs, the
failing first sweep and its exact failure messages, the diagnostic values (eye, vertex, edge state, the
cached reset bindings), the baseline and the 32 mutation results (17 + 2 + the 13 of stage 8 on the
audio receipt), the generated routes against the generator, both clean final sweeps, the 43 death/reload
chains recomputed from the raw logs, the encounter join, the audio-relation verification (7,972
receipts: 3,971 covered and 2,238 spanning fully confirmed, 1,748 ambiguous with a confirmed suffix, 15
with more than eight last-writer pieces left unevaluated) and the five completion runs. The first stage-9 receipts were superseded after the outro skip was resolved (a runner
log pump, its tests, and a removed polling instrumentation of `controllers.cpp`, restored to its hash): the final
receipts re-run both full aggregates on the rebuilt tree and bind that the live evidence's executable
(`stage9c`, `6ef51d4d…f4dc12b0`) and the tested rebuild (`stage9d`, `c581a603…8552c3`) differ in four link-stamp
bytes and that every C++ and shader source hash of the two scopes is identical. Final identities:
frozen executables `build/restrictive-audit/frozen-stage9c-native-20261003` and `-stage9d-`;
`engine_effects.cpp` `ddeed78d…2343b9b6`, `immediate_draw.cpp` `057f0c10…1470a08b`,
`engine_state.cpp` `d71ce22c…03c9e77d`, `engine_driver.cpp` `208986e9…57b51c3e`,
`engine_materials.cpp` `49480333…f6d7bed8`, `audit_stage_routes.py` `f722a397…d9ce4646`.

One earlier Native aggregate failed `NativeVideoSettings` once ("Unchanged fullscreen output rewrote
the native window"; it passes alone 4 of 4, cause not determined); it is preserved with a note and
carries no credit. Frozen executables of the intermediate builds (`stage8a`, `diag`, `stage9a`, `stage9b`)
and every failing or slow run are preserved under `build/restrictive-audit`.
