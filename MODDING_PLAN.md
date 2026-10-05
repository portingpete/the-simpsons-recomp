# The Simpsons Game: ultrawide and latest crash

## Active restrictive-check audit (2026-10-02)

Audit the original producer ranges, all mission catalogs and current native admission. Record new combinations before validation, group by asset/caller/parameters/mission/action/ownership, run independent original setup cases, and reproduce each valid rejection through complete create/use/release before broadening support. Original files stay read-only; stage launches use hashed private copies and the native command/capture bridge. This user-authorized audit includes live routes, checkpoint/death/cutscene/mission-exit testing. Preserve failed logs and recovery files; PR merges always need permission, and no worktree cleanup is part of this task.

Initial results: all18stages initialize independently;15complete the opening route, three new rejections retained for source-derived repairs. Corrected snapshot passes48focused CTests and49independent FX setup rows. Catalog joins distinguish implemented/setup-tested/draw-lifecycle-tested/gameplay-encountered; opaque geometry/VFX references and missing shaders remain explicit. Continue repair retests and the remaining original lifetime routes. Details: docs/restrictive-check-audit.md.

- Workspace: `K:/SimpsonsNativeCopy`; native Windows Xbox 360 recompilation using the user's local retail assets and `analysis/simpsons.pe`.
- Route: continue the existing XenonRecomp C++ runtime and byte-verified TOML hooks. This reaches the original camera/culling path and D3D11 renderer directly; a new loader is unnecessary.
- Scope: offline first-mission rendering, native Video menu, independent internal/display resolutions, 21:9 and 32:9 camera support, and the latest screen-effect failure.
- Profile/content: `build/mainmenu-profile-204`, `build/mainmenu-content-204`; preferences are the sibling `build/mainmenu-profile-204.video.cfg`.
- Logs: `build/launcher-logs`, `build/render-tests`; latest actual crash is `build/render-tests/20260930-175618-692932/game.log`.
- Lab: source profile/content/config snapshot at `build/ultrawide-lab/backups/20260930-180733`. Replays use private copies, controller command files and owned test processes.
- Skills: installed `mod-any-game`, `game-recon`, `reverse-engineering`, plus native/retro-decomp playbooks read. Their `um` CLI was absent from PATH and the installed skill/plugin locations checked; native filesystem backup and existing game-specific test/capture tooling provide the same lab isolation.
- Community research: [XenonRecomp hook documentation](https://github.com/hedge-dev/XenonRecomp/blob/main/README.md) supports the existing byte-pinned hook approach. [The Simpsons Game Recomp](https://github.com/YesterMester/TheSimpsonsGameRecomp) is another active native port; its different runtime is not being installed into this established workspace.
- Verification: original CPU camera setter/frustum fixtures, actual WARP target dimensions and UI/movie coverage, settings migration/window hit testing, exact padded presentation copies and original DOF CPU/shader regressions. The user will perform charged-burp and ultrawide gameplay testing; no further automated game runs.
- Remaining uncertainty: the original CPU fixture reproduces rejected DOF infinity and its shader has verified legacy multiplication, but the user's precise charged-burp crash needs their gameplay confirmation. Full-size main-camera support excludes split-screen/other profiles; fitting covers Apt menus/HUD and movies.
- No game assets or generated decompiled code will be published. PR merges require explicit user permission.
- Missing-choice follow-up: make wide internal presets visible alongside 1080p/1440p, explicitly label Ultrawide, retain saved IDs, and record native row/action/extent diagnostics. Both current linked callbacks and label getters already contain nine choices; the reported five-choice visible wrap needs the user's manual retest after rebuilding. Isolated preference and packaged-bytecode tests cover the revised cycle and full label export without running gameplay.

## First-mission completion shortcut (2026-09-30)

- User-selected destination: the mission-complete transition after Land of Chocolate.
- Route: original `-stream loc loc.str` startup, byte-pinned post-load receipt at `823BB5D8`, then original `EpisodeComplete` helper `8296FFC8` once the gameflow and movie owners are ready. Original outro/results processing remains enabled.
- Isolation: each launch copies the selected profile/content/video stores into `build/mission-completion-runs`; originals are read-only sources. Failed copies and logs remain available. Source reparse points are rejected; no worktree cleanup is needed.
- Evidence and verification: [completion route](docs/first-mission-completion.md), 386 CPU fixture checks, embedded launcher copy/quoting/long-path tests, original controls/timing and profile CLI checks. Both builds and all six focused CTests pass; AOT verification covers 311 files with zero diagnostics. Main profile/save/video hashes remain unchanged.
- Manual limit: no gameplay launched or driven. The user will check the actual outro/results display using `Play First Mission - Completion.lnk`. The shortcut does not restore the earlier play-through's scores or world checkpoint.

## Completion follow-up and direct startup (2026-09-30)

- Latest manual log: `build/mission-completion-runs/Completion-20261001-015123-814Z-70244-0000/logs`; the outro ran before ordinary dual-textured source `8202AD78` failed the native fallback argument guard with `r4/r5=1/1`.
- Repair: accept the original Boolean flags for this already-qualified source, retaining downstream ownership/material/draw guards and existing shaders. Original instruction evidence and verification are in [the crash note](docs/dualtextured-completion-crash.md).
- Direct startup: both launch modes dispatch immediately and exit without a launcher UI. Preferred `.lnk` shortcuts also avoid the `.cmd` console flash; private completion copies and output logs remain.
- Verification: both builds and all 11 focused CTest groups pass, including 230 original dual dispatcher/draw checks and 210 packet-owner checks. AOT: 311 files, zero semantic diagnostics. Main profile/save/video hashes are unchanged; no automated gameplay or worktree cleanup.

## Crash after mission recap loading screen (2026-09-30)

- Reproduce from the latest manual log and original CPU/GPU fixtures; no gameplay automation.
- Add original gloss, multitone and normalmap alpha shaders and selected-pass material maps, mesh inputs, stage0 base sampler and no-shadow/no-noise texture guards. Keep original ownership/ABI/argument checks.
- Extend original four-Boolean dispatcher tests for all three sources, independent instruction-derived numerical shader checks, original map/dirty-bank tests and artifact inventory.
- Build native and release game/recorder/direct launcher, verify AOT and primary store hashes, leave existing direct completion shortcut in place.

Completed: all three original alpha material paths and selected resource/declaration guards are integrated. Native and release builds each pass all 22 focused tests, with 72 original-instruction numerical cases on each GPU backend and original dispatcher coverage of all Boolean flags. AOT verifies 311 files with zero diagnostics. Primary stores and direct-launch shortcut targets are unchanged. Verification logs and source snapshots: build/post-recap-crash; details docs/post-recap-gloss-crash.md. No automated gameplay; full post-recap loading transition requires the user's manual retest.

## Authorized live recap repair (2026-10-01)

The user authorizes driving the completion outro/recap/loading route until this issue is fixed. Use isolated profile/content/video copies and the opt-in native controller bridge on the visible game desktop; preserve recovery copies and logs.

Completed: reproduced original beam1 L8 mip-chain rejection, then the additional streamed-audio token mismatch and recording snapshot capacity mismatch exposed by live retests. Added strict authored L8 mip uploads, actual claimed-node token selection with original guards, and a separate bounded host recording budget with pre-emission allocation. The complete skip/recap route now loads the Simpsons' house and renders for over five minutes without an unexpected failure. Both builds pass all18focusedtests and AOT verifies311files/zero diagnostics. All254profile/content files and video settings match their original backups. The existing direct completion shortcut remains in place. Evidence and limits: docs/live-recap-crash.md and build/recap-live-fix/final-live-verification.json. First-level gameplay performance remains outside this transition verification.

## Resource crash sweep (2026-10-01)

Completed the new movement-triggered built-in image repair, central material ownership resolver, selected-pass sampler/shadow guards, authored sky fallback line binding, dual-skin alpha and simpsons_skin_textured opaque/alpha ports. The shipped-texture census also identified and fixed the exact16x16RGBA8 tiled palette gap. All7,318 shipped texture metadata occurrences now pass admission; audio catalog/block census found no additional format or storage mismatch. The49-effect audit explicitly separates33artifact-complete passes,30runtime selections and77missing-artifact passes; catalog presence alone does not prove gameplay reachability.

Final build passed the completion skip/recap/loading route and exercised movement, attacks, special action, jumping and character switching for538.61seconds/28,489presentations without an unexpected failure. It executed the repaired8200FB98 textured-skin fallback. Both builds pass55focusedtests; actual built-in/ITXD owner fixture passes3,205checks and whole original skin fixtures pass1,506checks. AOT311files/zero diagnostics. Primary stores/preferences and the direct no-window launcher are unchanged. Remaining audit limits include unported effect rows, opaque VFX emitter semantics, streamed decode census and live reader scheduling/seek/restart. Details and reproducible receipts: docs/resource-crash-audit.md and build/resource-crash-audit/final-live-verification.json. Performance improvement is not established by this crash repair.
## House exit and Bartman Begins test route (2026-10-01)

Completed source-qualified auxiliary skin strides, dynamic audio producers/default-device handling and nine missing game-material passes with original dispatcher, material, numerical GPU and ownership regressions. Full streamed decode has zero errors across the catalog; game-specific table1 coverage is 40/42, while alternate AA, legacy table0 paths and opaque emitter references remain explicit limits.

Both final builds pass 138 selected tests and the 311-file AOT gate. Live completion/recap/house exit succeeds without an unexpected failure for 371.24 seconds; current direct Bartman Begins startup and eight opening-area inputs succeed for 443.21 seconds. New Play Bartman Begins.lnk opens that stage without launcher UI, using separate private saves/logs; original shortcuts/stores/preferences are preserved. Final receipts and captures: build/house-exit-fix; route usage: docs/bartman-begins-launcher.md.

The checked pointer/matrix optimization has one matched house observation of 34.69→41.12 FPS. Performance remains uneven, and a complete Bartman mission, later cutscene playback and all-level execution remain outside these verified runs. Keep these limits separate from the completed crash/launcher repairs.

## Enter and keyboard menu repair (2026-10-04)

- Request: Enter must not pause; WASD menu navigation currently advances too quickly.
- Route: update existing native keyboard mapping and byte-pinned original Apt input-dispatcher consumers, preserving original digital repeat timing and continuous gameplay movement.
- Recovery: build/keyboard-menu-fix-20261004/before; source/config/native and release binaries are preserved. Primary store/preference hashes recorded for 255 files.
- Validation: keyboard/window/original-input-manager fixtures, original Apt direction/repeat/ABI fixtures, and a bounded private background game run with messages addressed only to its owned HWND; no desktop input takeover.
- Status: build and verification in progress; no PR merge or worktree cleanup.

Completed: both native/release game and recorder builds pass all eight focused
CTest groups; original menu fixture has 798 checks and AOT has 311 files/zero
diagnostics. The private live keyboard check passes Enter/no pause, Escape,
one-row W/S, one-action 200ms A/D holds and bounded delayed repeats. Cancel and
normal WM_CLOSE succeed; all 255 primary file hashes remain unchanged. Details
and completed captures: build/keyboard-menu-fix-20261004/final-verification.json
and live-20261005-025143Z-934018. Existing shortcuts use the updated native game.
