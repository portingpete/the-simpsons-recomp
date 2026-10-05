# Modding journal

## Extended wander routes, four repairs and the completion transition (2026-10-04)

Seeded 255-command wander routes (config/audit-routes/extended-wander-v1.json seed 20261003, -v2 seed 20261004; tools/generate_audit_wander_route.py) were run on all 18 stage launchers. The first sweep (frozen stage-8 exe) completed 12/18 and exposed five defect classes the 20-command opening routes could not reach; four are repaired test-first (baselines fail with the live messages: stage9-baseline-v2.log) and mutation-checked (17 + 2 deliberate defects all fail a test): (1) the edge draw gate rejected a retained ExpandedBlend0 request (mob_rules, deterministic) -> EngineState::requireOriginalEdgeState; (2) the rigid alpha pass compared its inherited eye with the moved manager eye (bargainbin, bigsuperhappy) -> the staged register value is kept; (3) an immediate quad vertex with all-NaN position/alpha killed gamehub -> triangles using non-finite positions are dropped for non-radial, non-projected draws, everything a surviving triangle consumes is still validated; (4) the binding reset kept a private 22-entry copy of the 25-entry screen-shader table, so the first reset after a projected billboard rejected its own cached shaders (neverquest, 4 of 7 runs; the unit fixture reproduces the live addresses vs=E3E97180 ps=E3E9B1D0) -> one shared State::screenFields table. The fifth (eighty_bites frame rate falling to ~1 fps for >10 s once) was not reproduced. Final exe frozen-stage9c: seed 1 and seed 2 both 18/18 with 255/255 commands, neverquest 3/3 more. Seed 2 reaches 29 catalog passes (27 before), 704 files (368), 33,384 row receipts, 38,055 audio source receipts; across both final sweeps 45 failure requests (39 whole-party deaths, 6 from original caller 8289EB94), 43 complete death/reload chains re-derived from raw logs, all at each stage's initial checkpoint. New runner options --completion, --outro-skip-after (with a log pump: the game's stderr is a 1 MiB full buffer, so the runner's log view lagged and three early skip attempts were mistimed), --receipt-timeout (python tests); the completion route (native shortcut calls the original EpisodeComplete helper once) plays the LOC outro movie to its natural stop, the results pages, loads spr_hub, retires the LOC owner and reaches the spr_hub map ready, and with the pump a Start 0.3 s after the outro's readiness is accepted (controller-movie-start 82321114, decoder stop from the skip path 826B92C8) with the same results/spr_hub tail. Intermittent causes not found: eighty_bites collapse, three slow starts, one NativeVideoSettings aggregate failure (passes 4/4 alone; the failed aggregate is preserved). Native 527/527 (412.90 s), Release 527/527 (397.73 s) on the final rebuilt tree (receipts stage9-native{,-release}-v2; the first v1 receipts are superseded). Docs: docs/audit-wander-routes.md. Still open: pickups/destruction/dialogue/ability-hit receipts, later checkpoints, recorded and GPU lifetimes, 81 catalog passes not encountered (63 without native artifacts), skin/sky/mono row receipts, rejected-upload (Graphics::Error) containment (no deterministic producer).

## Audio claim to file-read relation (2026-10-03)

Observer-only relation between an owned audio reader claim and the recorded guest file reads that placed its bytes (kind audio-source-file, emitted right after AudioCatalog::match has content-verified the block). runtime/filesystem.cpp keeps a ring of the last 512 successful audited reads; recentFileReadCovering() resolves each byte of the claim range to the newest recorded read (covered / spanning of one open at linear offsets / ambiguous / none), the producer takes a read-sequence snapshot before copying the claim, receipts carry up to eight last-writer pieces, the true read count and up to eight catalog candidates with stream indices; auditedPathsAgree() accepts only whole-component path suffixes. OriginalAssetFilesystem covers the cases and five deliberate defects in the final source each fail it (stage7-mutation-check-v2.log; the v1 attempt stopped on a tooling error after two kills and is preserved). An independent script parses analysis/audio_catalog.bin and confirms, for 2,132 receipts from eight final-exe runs (186 files, 1,286 file/block pairs), every byte of 1,107 covered and 681 spanning claims at the catalog block offset; 344 ambiguous claims keep a confirmed suffix while a head (13 bytes to 99.7%) was not last written by a recorded read at that offset (inferred guest-side assembly of straddling blocks, not observed). Final exe frozen as frozen-stage7g-native-20261003: second full sweep 18/18 with 20/20 commands; the first sweep had one unexplained slow-start failure (meetthyplayer, first command 65 s late; preserved; three reruns pass) and one later spr_hub control ran 114 s (repeats 51-52 s on this and the stage-6 exe). Native 526/526 (427.06 s), Release 526/526 (404.46 s). Docs: docs/audit-hot-path-and-file-provenance.md. Still open: deaths beyond the Tree route, pickups, destruction, dialogue, later checkpoints and mission exits, recorded/GPU lifetimes, 63 passes without native artifacts, skin/sky/mono row receipts, rejected-upload (Graphics::Error) containment (no deterministic producer).

## Live routes, hot paths and file provenance (2026-10-03)

Took the new observers onto real stage routes with a frozen executable. Findings the fixtures could not show: the
producer-entry/row logging scanned every tracked allocation per draw (profile: ~90% in
EngineAudioOwners::allocationSpan; spr_hub hit its 180 s deadline vs 51.8 s with the 497 exe) -> exact 4 KiB paged
containment index (runtime/heap_page_index.h, unit test vs brute force, 400k lookups over 300k allocations in 0.035 s)
and a ResourceAudit epoch + cached row keys; and a deterministic misroute (eighty_bites, old exe too): a recorded mono
upload after a COMPLETED recorded rigid replay was claimed by the stale rigid run state -> only an unprepared run claims
the hooks. Added observer-only file open/read provenance (declared alias vs normalized path, extent, native identity,
offsets), baselined against the exactly reconstructed pre-edit filesystem.cpp. Result: 18/18 opening routes with 20/20
commands (final exe), 27 distinct catalog passes encountered (all admitted), 10,696 stock row receipts (max 19 rows),
368 files / 796 read groups all inside their opened extents. Full serial aggregates 526/526 Native (419.47 s) and
Release (412.22 s). LOC movie skip and a Tree death->cleanup->reload cycle repeated on the final exe (receipt final-live-lifecycle-routes-v1). Docs: docs/audit-hot-path-and-file-provenance.md. Still open: read->audio-claim attribution,
pickups/destruction/dialogue/deaths/mission exits beyond the earlier frozen routes, recorded/GPU lifetimes, 63 passes
without native artifacts.
## Rigid per-row nine-word prevalidation (2026-10-03)

Implemented the preserved row-prevalidation design: the rigid mesh entry copies each offending row's nine raw
words into the audit (kind effect_producer_row) before any offset/header/collection read; stable group holds the
source-proven consumed scalars, instance holds all nine words/ordinal/address/owner. Compiled-index and selector
controls now fail with ROW attribution; span failures keep entry attribution. Test-first: the 24 updated tests fail
against the stage-2 source and pass after; full serial aggregates Native 525/525 (428.27 s), Release 525/525
(421.99 s). Corrected a wrong expectation along the way (`variant` is always 0 because the original dispatcher
clears the alpha byte). Docs: docs/rigid-row-prevalidation-audit.md. Frozen final executable for live routes:
build/restrictive-audit/frozen-rigid-row-prevalidation-native-20261003 (SimpsonsNative.exe sha256 31c589ac...).

## Rigid first-capture repair and contained diagnostics (2026-10-03)

Skills: mod-any-game, game-recon, reverse-engineering, game-automation. Route: existing local AOT/native
port; originals read-only. The standalone first-capture target had two genuine baselines failing at 352
checks ("First rigid capture submesh extent changed"): the capture diagnostic re-imposed a 65,535-row
cap on a valid original 65,536-row table. Stage 1 applied the reviewed byte/owner extent candidate
(capture-extent-application-v1.json); both fresh opaque/alpha processes now pass in Native and Release.
Stage 2 made capture diagnostics unable to decide a primary outcome: noexcept contained capture, a
64 MiB budget that skips instead of throwing, separate attempted/completed latches, and a non-attributing
`ResourceAudit::diagnostic` event. New fresh-process cases: a valid 1,864,136-row original table
(64 MiB + 32 bytes), and real blocked output names for raw and draw metadata. They fail against stage-1
code (345/354 checks; the draw-I/O failure was misattributed to a texture_binding encounter) and pass
after the repair. Full serial aggregates: Native 525/525 (419.23 s), Release 525/525 (467.49 s).
Pitfall recorded: a first baseline attempt ran the new binary because Copy-Item kept the old mtime and
Ninja skipped the rebuild; preserved as INVALID and uncredited. Rejected-upload (Graphics::Error)
containment has no deterministic original producer and is source-reviewed only. See
docs/rigid-capture-contained-audit.md. Next: per-row nine-word diagnostics, audio file/claim attribution,
catalog support gaps, recorded/GPU lifetimes and broad gameplay routes remain open.

## Zero-row rigid regression (2026-10-03)

Reproduced two independent original zero-row failures at the zero-byte mapping
check, then repaired empty row-span and collection admission while preserving
geometry binding and closure. Native and Release each pass 18/18 selected
tests, including later ordinary draws, malformed ABI/owner/material controls
and original scoped CPU cleanup. There are 509 registered tests; no full
aggregate, new gameplay encounter or complete GPU retirement is claimed.
See docs/rigid-zero-submesh-audit.md and its bound raw/source/executable receipts.
The user initially requested wrap-up, then instructed completion of all work.
That later request supersedes the checkpoint. Unused-pointer and
collection-value logging extensions are now applied and being verified in
fresh scopes; first-capture testing follows. The broader audit remains active.

## Restrictive-check audit (2026-10-02, in progress)

User requests producer-derived bounds, complete catalogs, pre-validation combination receipts, independent stage/setup cases and original create/use/release regressions. Skills: mod-any-game, game-recon, reverse-engineering and game-automation. Continue the existing local AOT/native route with original assets read-only, verified private save/video copies and the owned native command channel.

The initial snapshot passed 48 focused native CTests, 14 independent original audio cases and 49 fresh-process FX setup cases. All 18 stage launches reached original map readiness; 15 completed the 20-command opening route, with 319 delivered commands overall. Primary-store hashes remained unchanged. Independent failures concerned Tree Hugger's distortion cache, Mob Rules' inherited skin blending, and Dolphins' presentation state. Preserve all raw failures under build/restrictive-audit/stage-sweep-20261002.

Later producer cases cover audio ring arithmetic and 65 live reader groups; aligned geometry strides and partial tails; signed selected indices, large/odd index owners, and zero/one/two-index strips; original radial loops; configured MUS cancellation/completion; and inherited mono material matrices. The current source also preserves the original recording manager's retired graph/history, observes genuine idle context publication without guest writes, reproduces SDK triangle-strip packet splitting and admits both source-correlated pipeline capability branches. All493 native tests pass on the frozen prevalidation/capability/thread snapshot; both builds and AOT verification pass. Release passed492/493, with only the unchanged-fullscreen-window assertion failing; its isolated retry passed, but its cause remains unproved. The first observer aggregate failed because the fixture configured its audit sink twice; its failed receipts remain preserved, and the corrected35-case focused selections pass in both builds.

New effect/pipeline observations precede validation; the map request observer attributes a source-qualified original request before package publication. Independent receipt checks retain all64 malformed capability failures and their exact preceding producer snapshots. Shared production page invalidation now also links into the thread fixture, which verifies stack termination and retained PCR-owner reaping invalidate their watched pages. This fixture does not test guest write barriers. All49 fresh-process FX setup cases pass again. The full packaged geometry/VFX census recovers25,990 geometry records and29,319 primitive6 submeshes, including36 valid empty clumps; the Python cache-provenance repair rechecks each repeated occurrence's decoded and named-payload hashes. These fields do not certify native lifetimes.

Live Tree captures show Lisa's saxophone and enemy melee damage. A saved-slot Continue restored a nonzero LOC checkpoint. Accepted Exit Game retired the old LOC owner in a preserved developer-stream run, then selected an unregistered original DebugFE successor; no null-target guard was relaxed. The frozen normal frontend retry captures the actual quit query, delivered Yes, matching old LOC owner cleanup and visible Main Menu return. A second Continue reaches a new map-ready generation even though the guest reuses the same owner address. That run has closed; it proves normal exit/reentry, while death/reload remains unproved. Executable11385581244ed2e01e065cf54bf09f23975ca7e3e36b6dc21f4af99715ea06a1 and private windowed720 preferences remain frozen. The first900-second attempt and rejected timeout invocation remain preserved. Input delivery alone receives no action-completion credit.

The live route exposed two diagnostic gaps retained for the next source wave: accepted movie Start is masked before last_action capture, and mission attribution remains loc after owner retirement/frontend return. Raw delivery and lifetime rows remain untouched. The matrix-group CPU probe passes72 checks through actual original826FE710/memcpy, including65 groups selecting two matrices, but the native guard still rejects that input; full original draw and logical table-owner/generation regression remain required. VFX-capacity candidates also require whole activation/use/retirement proof. An exact-expression guard overlay reviews9 shared vertex extent predicates and4 native mono binding invariants, with2 compound guards only partly reviewed and760 whole guards still untriaged. The original773-candidate census is preserved. Renderer implementation .cpp files were absent from the earlier source snapshot, so those four invariant reviews retain source-only credit; the next build snapshot must include them.

The controller/mission/matrix source wave now reproduces three independent valid65-row rejections after actual original two-pool loading, relocation and geometry creation. Canonical admission uses the original signed-positive row-count domain while retaining composed-range, selected64-matrix and actual observed logical-owner bounds. All three native opaque/alpha cases pass pixel equivalence and original camera/declaration/FX/cache/pool cleanup; malformed logical extents, source ranges, selected totals, pointer identity and changed equivalent-result bytes still reject. Fresh allocator generations pass, but address reuse did not occur, live captured-generation rejection is unexecuted, and backend uploads remain owner-resident. Baseline3/3 failure receipts and repaired3/3 receipts remain separate. Native focused27/27 passes. The CPU movie-action fixture passes2,417 checks through the actual input wrapper; accepted Start survives diagnostic capture without changing masked returned/recorded input. Mission-retirement attribution passes guest/PPC/host preservation checks. Both complete builds now pass497/497 tests (Native396.90seconds; Release393.80seconds), bound to1219 frozen source inputs including actual Ninja-observed non-system headers. The separate1051-input baseline remains limited to its original scope. A new88-second owned LOC intro run confirms actual82321114 accepted Start, matching-owner826B92C8 decoder stop with the same accepted action, original decoder release/completion and subsequent genuine LOC readiness. Its891 raw rows and three completed accepted readbacks are bound in final-movie-action-receipt.json (SHA256d8a99beae751352bf6a8089118220e0d54581468b6054f7cc7befbdbb0205229); the primary stores and seven frozen components remain unchanged. A separate181.79-second owned Tree run now completes four original whole-party death→matching gameplay-owner cleanup→qualified reload→fresh ready cycles, including three same-address/new-generation lifetimes. Eight post-cleanup unknown mission rows verify actual retirement attribution; three additional unknown rows precede the first owner. All ready checkpoints match the original opening GUID, but reload requests briefly clear it; later reached checkpoint/world-state restoration remains unproved. Corrected receiptv2 bc80b2e73a2761140e1eb2cd8a6d052a4aa6a548db0fb4f2d6734a10dfbda9c5 binds1251 rows (the first count-label error remains preserved), four accepted captures, original source spans and unchanged primary/frozen files. Complete per-resource GPU/audio/FX retirement remains unproved. Earlier suite and live scopes receive no retroactive credit.

The next submesh source wave independently reproduces three valid65536-row rejections after original8282F618 first-pool loading/relocation, geometry/declaration creation and successful one-row opaque/alpha draws. Only immediate skin row admission is repaired: checked36-byte row arithmetic, conservative no-wrap guest span, observed containing allocation logical extent, then mapped access. Three repaired large opaque/alpha cases and four precise malformed frontiers per family pass in the Native5/5 focused selection, including pre-validation source/caller/count/bones/owner/action receipts and PPC/CSR/LastError preservation. The independent unreadable-metadata producer observation also passes. Both full builds and311-file AOT verification pass. Native500/500 passes447.12seconds and Release500/500 passes437.34seconds; both strict receipts rehash all1219 compiled source/header inputs and executable/DLL identities. Full Native receiptbcd9cc22af1a916eb72bd6202f4770b128f767fbc4eccb7d943308a63cb9b7fa and Release receipt9711a826a1af73848f1ea9c2dd50ad5d691dad3f498af4c7349184e6ea9db5a3 retain the three independent synthetic original lifetime cases and exact prevalidation failure rows. These scopes do not include every repository Python source or add gameplay coverage. The baseline1219-input scope and all three original rejection receipts remain separate; compiled current Native/Release scopes are6f31a6f7eac8cc23004eb259a3c4249261779011d1ca536e3a0d3d33a39caacb and853270c33c7f0fb3d67d1334b11351b4938dfa65ffa61e9bb0870f30e7ef550a. Other count/material/diagnostic caps, row epoch retention, physical aliases, complete allocator limits, concurrency and backend GPU retirement remain open.

The stable-grouping wave reproduces 17 independent expected diagnostic failures: one movie input case and 16 pipeline capability cases across WARP and hardware. Strict baseline receipt 844c2a140a98bf9ff98bf503ad53fef21b7611dfcebc98d2dacef6931a13558f binds 1,219 frozen inputs, 219 executable identities and retained raw JSONL; pipeline assertions follow actual original cleanup and final native lease expiry. The corrected movie baseline retains both CPU begin/end observations, while later variants, map release and decoder remain outside failed-baseline credit. The first movie attempt lost its temporary raw JSONL; its narrower failure log is preserved, and corrected v2/v4 receipts remain separate. Production now keeps movie packet counters and pipeline release-only inherited register lanes in instance data. Real input, caller, mission, action, ownership, capability and create fields remain stable keys. AOT verification reports 311 functions and zero issues; both full builds pass. Both focused 17-case runs pass (Native 6.60 seconds, Release 6.68 seconds), with exact source and executable bindings. The full suites pass 500/500 in Native (474.63 seconds) and Release (518.62 seconds). Strict receipts b1e6a58cc7fa6ad14944e7adeff22b2ba8dc14e1c14c7359c23517e3d179f3d7 and 7b578326753016a8d5614385f216a0c0d406a5e3f07f64fc9fd54e90671fb058 bind all 500 outcomes and the 17 actual grouping cases against 1,219 inputs, 219/211 executable identities and three DLLs per build. The first full binder rejected the real empty-output Homer CTest block; its parser failure and old binder remain preserved. Corrected 14/14 host parser checks include that actual block and malformed/truncated controls, with no native suite rerun or extra execution credit. Compiled scopes are f0530c17aa46969db87194da124d71c9690a1d516d35ba99f5dee279dacff395 and 377a5075d62fb9c9a333e3ad69c2596af8154e293e5fe9700acd43bdf2b04a89. Audio command+4 close ownership remains an independently source-pinned upcoming regression, with no repair or execution credit.

The audio-close wave now reproduces the logger's inherited-r4 misattribution through two simultaneously live original reader groups. The unpatched test fails only after eight fixture groups, fourteen managers, sixty-four forwarded fixture allocations/frees and original root destruction/OS worker join. Strict baseline receipt baa93066b421e53d1cd91a8adc1325cc92db9c05ac80fde8f16592f68d66dabb binds 43 raw rows, 1,219 configured inputs, the one executed target and three DLLs. The first binder rejected genuine UTF-16LE PowerShell logs; its code and rejection remain preserved. The corrected BOM reader passes 18 host parser checks without changing the logs or rerunning the native baseline. The application helper also initially rejected the diff --git prefix before writing production; the following unpatched regeneration has no repair credit. Both frontiers and recovery copies remain preserved.

Production cddc04059499106c8a7d4f97cc55d4ebcbec9f3c79ac47b9b4f6a43d5a3ac829 applies only the reviewed diagnostic patch: close resolves command+4, conservatively qualifies the original executor before queue reads, preserves complete raw lanes and host state, and keeps transient instances outside stable keys. The functional observer remains byte-for-byte unchanged after its runtime/context admission check. AOT regeneration and verification report 311 files and zero diagnostics. Native full build passes; its independent repaired lifecycle passes 1,665 checks in 1.02 seconds, including both actual frees/root join, unchanged malformed failures, unknown/stale no-ops and exact r4 collision grouping. Bound receipt 118cae2742bab0e30fc0340dc241a54019ff325875380763f424aaccd8892e6e preserves 42 raw rows against current Native scope a40807dc1d349b31718019bf78f3da821a45c75c687e3e71bb51c6ab3ae47a46 (1,219 inputs, 219 executable identities, three DLLs). Release build/tests and both complete 500-test reruns remain pending. This synthetic empty-reader lifetime excludes unrelated startup viewport owners and does not prove configured PCM/EOF/seek, filename/catalog identity, queue generations, concurrent pins or a device fence. Cached close profile fields and explicit filename associations remain open; Group.identifier is an instance serial. Detailed evidence: docs/restrictive-audio-close-audit.md.

Current audio-close verification closes both full builds and the Release focused regression: 1/1 passes in 0.61 seconds with 1,665 checks and 42 raw rows. Strict receipt eb0772e86aa40f5cf617a7f36cd0666f755a6c32158ab9217bb7be6351a5755b binds it to Release scope a934e0fb7a002ff464dce66cb0b1c563742de1cf2be2e64c9611aabffe668aa4. Native passes all 500 tests in 415.97 seconds; strict full receipt c1273bb3b1504303a071fd13c8858e8f2eb41f8ffcd26ab27cfac861d2804a04 retains the actual lifecycle observations and full-suite outcomes. Release passes all 500 tests in 448.35 seconds; strict full receipt fd9075168261c493b407b068fe3b6bd4b7a6c5cc2933a49f93302b0f218e18fd likewise binds its actual outcomes, unique 42-row fixture log, all 1,219 inputs, 211 executable identities and three DLLs. These results supersede the pending statuses above, without expanding catalog or gameplay coverage. No source or executable changed between each compiled scope and final strict binding.

The refreshed source-only census uses the audio-close source reports under build/restrictive-check-audit/audio-close-source-*-v1-20261002.json: 769 mechanical guard candidates across 274 files, 1,112 keyword lines, 49 effects and 110 passes, with 63 missing-artifact passes. No runtime log was joined. Independent close-profile/catalog design review confirms that future implementation must retain construction origin, catalog source hashes and offsets, explicit relation publication, and bounded filenames. Profile-only candidates are being drafted separately; source review does not qualify their execution.

The close-profile wave (2026-10-03) now closes a separate diagnostic regression. Test-first baseline3/3 expected failures occur only after complete original lifetimes; strict receipt4e29dc9d374c7f6885c99689fe4fef0a684bbee27edce07157a441ca8162337e preserves42/102/47 lifecycle/aggregate/Case2 raw rows and two rebuilt targets. Readera0b7bfd499af2fa231252a7e5db2a3451377110e39479f28ca3b18fb91a7918f adds immutable qualified construction origin/caller and cached numeric close profiles. Two whole-factory readers with310400-byte rings/effective7/raw4 entries now share a key, while120000/effective10/raw7 remains distinct. Natural serials, native generations and inherited-r4 collisions remain per-instance; the asset is still generic original-reader. Three actual requests/claims/copied checks/releases, fifteen forwarded reader-role allocations/frees, whole voice/queued worker closes, service shutdown and original root/OS join precede the diagnostic result in both aggregate and independentCase2. The empty-reader regression retains eight malformed/no-op controls and sixty-four forwarded allocations/frees for eight groups/fourteen managers.

Both complete builds and AOT311/0 verification pass (134 Native and136 Release build steps). The affected eighteen cases pass11.51seconds Native and10.34seconds Release, with lifecycle1672/aggregate1752/Case2616 checks and44/114/49 retained raw rows per configuration. Strict receiptsf85dfc5dbf04f2f5f5d87dcd71d7a2318ec4d79a01a2c13d92338a3699edc312 andb3cf2072c2e3dd88972a7d7f55daa11e3e915324539b2076c8fa6e79cc1e3cf0 bind current1219-input scopesb9c246a621acca07b0fb819ee987a90394fec23db88d69a2e5339c1d5c926ff7 andebd9ea2fa092c91f054157f069e7b731d72753b26004694d593780e7b602f291,219/211 executable identities and three DLLs. Previous full500 executions above remain historical after these new source/test edits; no current full500 rerun is claimed. The final adapters pass28 independent host checks. Unscoped-startup and aggregate-action adapter frontiers remain preserved, with no telemetry rewrite or native baseline rerun. Applicationv1's broad nested-scope freeze is the preceding binder proof; fresh repair scopes separately verify actual compiled inputs. Detailed source, execution, cleanup and remaining limits: build/restrictive-audit/close-profile-wave-20261002/README.md.

The refreshed close-profile source-only census (2026-10-03) still reports769 mechanical guard candidates/1112 keyword lines/274 files,49 effects/110 passes/63 missing artifacts, and includes readera0b7. CensusSHA30ef26a7e2fc172e316e5f029d43283b8e784c13a673f2ca3f2e8004764ae748 joins no runtime logs. The game-specific AA row/column source review pins26 whole functions and1102 words, but original setup reachability remains unqualified: completed known setters couple parent enable and static mode, so no producer-valid row/column rejection is reproduced and no guard is widened. Catalog certificate retention, actual opened/read file lineage, bounded filenames, queue epochs/pins, device fences, configured PCM/EOF/seek and complete mission action/resource lifetimes remain open. The catalog metadata candidate and independent fixture draft are unapplied/source-only; no gameplay credit is transferred from the older frozen runs.

The current audit request authorizes these private, owned-process gameplay routes and supersedes the older no-automation plan below. Main report: docs/restrictive-check-audit.md. No worktree cleanup or PR merge.

The source-extent wave (2026-10-03) reproduces a new catalog rejection with an actual producer input. An authenticated copy of chocolate-rabbit dialogue keeps all original encoded audio, moves its payload to aligned 0x20000000 and changes only the copied wrapper offset word. The unchanged generator accepts the sparse 536,891,968-byte source and emits a 2,145-byte catalog. All eight independent Native baseline cases run: original full 14-block/67,328-frame use and retirement plus five structural negatives pass; relocated use and reopening reject at the native per-source 512 MiB metadata cap. Strict baseline receipt 26893e6a77d73654cd93586a611846586f85fb8e10b3a13c4b0974d8e0b599ab and independent source/file review preserve actual hashes, parameters, caller, action, mission and ownership before validation. Two identical failure instances group as one policy bug. Original assets remain untouched.

Production source 93515f1b2eeae206491867fba7259c263a794fa543f9b1fa5655e645796df80c removes only that Source64 metadata ceiling; real catalog allocation/table/path/framing/ordering and subtraction bounds remain. The production applier rehashes every nested compiled source/executable/DLL and raw fixture input immediately before writing. Both affected selections pass five CTests (Native 9.50 s; Release 9.28 s), including all eight independent extent cases. Each run completes original, relocated and reopened 14-block lifetimes, retires every receipt, rejects 14 stale queries and 14 stale stages per lifetime, closes codecs/files, and rejects a valid new segment at the exact closed-owner guard. Full PCM matches SHA256 1924b6830424185a1ef1d4ef233791cf2dc590dcd6efc2de5798e72a53e2154c. Five malformed catalogs still reach their exact own guards independently. Strict Native a7e4cf42c15fd90cd1cc0c7523f7f8245b26b77f554ace415e5d23d940d90d69 and Release 17bebff2a8843ad8ed9afd22f8436efd1369513f3a1019ed4bfcf3549f561931 bind separate 1,220-input/four-executable/three-DLL scopes and 642 object dependency records. Actual 11/12 build steps compile the catalog and relink the application; AOT verifies 311/0. The new target raises registration to 501 CTests; no full 501 rerun or new gameplay run is claimed. The binder's 121 host controls and all independent read-only reviews stay separate from actual native executions. Evidence: docs/audio-catalog-source-extent-audit.md and build/restrictive-audit/audio-catalog-extent-wave-20261003/README.md.

Source review also corrects the earlier claim that 253 is an original reader constructor limit: transport and allocation keep the full word, while later token masking aliases higher slots and can break claim accounting/reset progress. Production remains conservative; coherent raw254/255 low-slot designs do not prove a safe full-capacity domain. docs/audio-restriction-audit.md is corrected, with its earlier copy preserved. The fresh source-only census still has 769 mechanical candidates/1,112 keyword lines/274 files and 49 effects/110 passes/63 missing artifacts, without runtime joins. The certificate identity API is rebased onto 93515f but remains unapplied, as does its v5 fixture. Full original source ownership, guest large-file setup, reader lifetime, device/sample-seek qualification and complete mission routes remain open. The overall goal remains active.

The catalog identity wave (2026-10-03) now implements the owning optional CandidateIdentity getter. Exact source/header d8d4ac5f1b2f776df4670ddaffd5d9e2ebcfba60f674e7c2f8b43c1e38946a24/464c30621bae6c4d8be5cc9983abfc8a238746b77090efc5418ff9b3644d4f41 retain framed source path/kind/extent/digest and stream ordinal/header/audio offsets without changing admission, matching or ordered duplicate aliases. Test-first Native baseline runs all47 independent cases before the one delayed missing-accessor feature failure; it is not a valid-original-asset rejection. Its142 raw rows contain48 attempts,46 before-load identities and48 outcomes. Independent source/baseline reviews preserve actual read intervals. API application a28323349a168e79f35615fc0cc3955f13e395e8efa60abd13a67dd18ffd0460 rehashes all nested baseline compilation/execution inputs immediately before writing.

Both selected repairs pass8/8 (Native14.69s; Release14.08s). Each runs55 independent metadata cases/163 raw rows, checks all9470 shipped copies, owning values after destruction/replacement/move, preserved aliases and exact independently observed structural/current-policy diagnostics. Existing original matching loop controls remain bounded256-frame quota checks. Full original/relocated/reopened decoder lifetimes and original MUS cancellation/completion through file/read/request/mixer/codec/metadata/deferred reader/service/worker release pass again. Strict current receipts6249e0a615cdef7cad61dbc044b1668a3cb1f374fba63ae5b31f31765d1ad6a8 and5214d146336d2bb88df49c8ec5f0d8b702f125a3c2d29bd23288747737cbc40c bind1220 project inputs/five executable identities/three DLLs/642 dependency records. Both actual17-step builds recompile the changed layout's consumers and link the app. AOT311/0 verification passes; no full501 or new gameplay run is claimed. The current lifetime supplement edbffe23e832e97aab2c7472250d18a86db701e81610f0af651393576b9783e4 separately binds all actual source/fixture bytes and complete controls. CTest fixture/XML ordering and field padding are handled in parsed views without telemetry rewriting. Prior binder drafts and preparation frontiers remain preserved.

The actual original file-provider chain is source-pinned, including the provider-file object containing the guest NT handle. Runtime open/read identity, IO-to-reader claim relation and safe same-object full-file fingerprint remain absent; copied catalog digests are assertions. Source diagnostics must copy metadata without retaining kernel handles or extending original file lifetimes, and distinguish reader/resource generations. A separate rigid65536-row original reader/setup/draw/release regression is designed and remains unexecuted. Reader capacity and complete mission actions/resource lifetimes remain open; the goal stays active. Details: docs/audio-catalog-identity-audit.md.

The scanner's runtime/renderer-only scope omitted handwritten audio catalog checks. Its Python-only scope repair now includes audio/common/app; the meaningful omission fixture plus existing lexical checks pass4 host tests. Expanded report audio-identity-expanded-census-v2-20261003.json contains313sources/824mechanical guards/1169keyword lines and actual catalogd8/header464 hashes. Earlier769 reports remain preserved with their narrower scope. The55 additional candidates have no automatic producer/lifetime/gameplay credit. This change touches no native compiled input or executable and grants no additional native execution. Immutable matched-source certificate publication remains a separate source-only design under build/restrictive-audit/audio-matched-certificate-design-20261003.

The rigid row-count wave (2026-10-03) reproduces the actual original65,536 rejection at351checks after one-row opaque/alpha references. Earlier two fixture stops were an alpha-eligibility assertion, corrected against original8274090C clearing the packet byte before8274029C loads it. Baseline2e874684d8790145da11f043b149566bf429446e0674dbaadea99d0a21d9bfa4 pins oldade767 and54rawrows; that aborted large call receives no cleanup credit. Finalenginea2632975f4f1021310072af84123e33f0fe5841bf0ee9ca027ce6d92a31ba090 computes36ull*count, bounds the complete span against the observed start-byte allocator owner, preserves mapped fallback and separate material/capture caps, and logs original rigid entries before validation. Original line endings are preserved; the first normalized candidate is retained as a preparation frontier.

Six independent fresh rigid family processes now pass create/use/opaque+alpha pixel equivalence, four malformed owner/material controls, later valid calls and original camera/declaration/FX/cache/pool/serialized-source cleanup in both builds. Native and Release each pass16/16 selected CTests in13.25/12.87seconds. Strict9ad32eead6ab7791d2c6a3713e766726af385df0587be7849dc0c281524a3f94/e473ff534e6edc69762d23f3d6f0a35bcb9bc4c8082030c63bb5a4563ba8bfde join12original shader pairs per configuration and bind1221projectinputs/fiveexes/threeDLLs/643dependencyrecords. Production has9entries+4failure rows per family, with failure group/instance preserved; local10encounters are separate.24malformed controls per configuration retain full PPC/host/owned-byte restoration. Stale CPU owner/query checks are not retired rigid-handler or concurrent-lease proof. Both initial repairs build13steps; final Native family registration performs4steps including3verifiers. AOT311/0 passes. Registration is507; no full507 or new gameplay run. Full GPU retirement, borrowed authored-material/driver lifetimes, recorded larger rows, valid zero rows, capture-enabled first/rejected uploads, offending row-word attribution, actual audio source relation and broad mission resource routes remain open. The overall goal is active. Report: docs/rigid-submesh-range-audit.md.

The first census refresh stopped on a stale native shader-producer hash; fresh effect and producer reports passed that gate without bypass. Current rigid-row-expanded-census-v1-20261003.json has313sources/823mechanical candidates/1168keywordlines, all lexically untriaged. Its49effects/110passes/63missing artifacts are unchanged;12fixture shader joins live in separate execution receipts and are not gameplay observations. Binder preparation frontiers for CRLF/XML, deduplicated sequences, CMake bracket grammar and earlier alpha pass selection versus later mesh continuation actions remain preserved; no raw telemetry was rewritten or native tests replayed to repair those formats.

## Prior ultrawide task

Add ultrawide support, then fix the latest crash. User requested the installed universal-modder skills; their instructions and native/retro playbooks were read before continuing after the instruction update.

## Confirmed facts

- Existing rendering keeps guest metadata at 1280x720 while D3D11 Scene resources scale to the configured internal dimensions. AA modes are frozen at backend creation.
- Add internal indices 5..8: 2560x1080, 3440x1440, 3840x1600, 5120x1440. Add window indices 3..6 with the same four extents. Version 4 preserves the old index meanings and migrates versions 1..3.
- Camera begin `823F1870` precedes dirty frame sync `8240D5C0`. Setter `823F1C98` owns view window at camera +0x68/+0x6C, reciprocals +0x70/+0x74 and dirty-frame notification. Use it to set horizontal view window to vertical view window times frozen render aspect; repeated calls must be idempotent.
- Main scene camera identity comes from graphics manager at `82D08B10`, camera at manager +0x14. Scene plugin callbacks are +0x10 `823D2940`, +0x18 `823D1100`, +0x1C `823D1160`. Perspective mode is camera +0x14 == 1. Leave fixed shadow/reflection/UI cameras alone.
- Native Im2D coverage and movie draws need centered 16:9 content on wider Scene targets. Scene geometry, depth, post effects and AA keep the full width.
- Latest actual failure: `build/render-tests/20260930-175618-692932/game.log`, "Screen-effect constant is nonfinite", near frame 5326. Human settings were 1080p internal, SSAA4x, uncapped. Prior launcher/test exits labeled "Native window closed" are normal closes.
- Screen-effect diagnostics now identify shader, constant row/lane and raw bits; replay reconstruction preserves 1292 keyboard/mouse state changes, including right-stick mouse values the old reconstruction omitted.
- Original DOF CPU divisions at `82754564`/`827545A8` can produce infinity when focus distance/range is zero. The original CPU test reproduced the rejected DOF c1.z infinity, and the original shader oracle confirms zero-annihilating legacy multiplication. Permit the original DOF c1.z/w infinities and paired c1.w NaN when c1.z is infinite (both requests zero); reproduce original multiplication/saturation behavior. Other live NaNs/infinities remain rejected. The user's charged-burp gameplay trigger remains for the user to verify.
- UI fitting is restricted to the existing Apt expanded-overlay enter/exit scope. Wider world geometry and effects use the full scene target. Movies keep their original aspect, and display presentation pads an exact internal pixel copy before window scaling.
- Original camera/frustum, video preference migration and all prior focused checks passed in both builds. The new ultrawide GPU fixture initially required an exact Float32 height; 1600/720 scaling differs by 0.00012 pixels. After correcting the assertion tolerance, all four real allocations, Im2D coverage, movie bars and exact padded presentation copies pass in the native build.

## Lab and restore

Profile/content/config backup: `build/ultrawide-lab/backups/20260930-180733/backup.json`. Restore from these copies if requested. Actual user saves/preferences stay untouched by automated runs; each run copies private stores. The installed skills are available, but the `um` executable was not found; use existing game-specific replay/capture tools and native filesystem copies.

## Next

Both native and release game/recorder builds are complete. AOT verification passed for 311 files with zero semantic diagnostics. All 21 focused CPU/GPU/menu/launcher checks passed in each configuration; logs are `build/ultrawide-final-native-tests.log` and `build/ultrawide-final-native-release-tests.log`. The launcher and replay helper updates passed 42 mock/temp-data checks without launching the game.

Normal, automatic, render-test and installed Steam entry points use the rebuilt native game. Recorder/replay/isolation helpers now carry saved video preferences and required derived menu assets. The installed Steam shortcut retains its existing explicit 60 FPS startup argument; Steam is open, so its shortcuts file was not changed. The generator uses saved frame-rate preferences for new entries.

The user explicitly will perform charged-burp and ultrawide gameplay testing; do not run further game automation. Select a wide Render resolution in native Video, Accept and relaunch to apply the internal resolution and camera change.

## Settings return / pause interaction report

The user changed resolution, returned to gameplay with pillarboxing, then reported an unresponsive pause/window interaction. Keyboard/mouse was used; Alt+Tab did not restore it. The run `build/render-tests/20260930-184847-822282/game.log` ended with a normal window close after 3150 successful presents. Saved preferences are `4 4 1 0 0 2 3 3`: desktop/output 3440x1440, internal 1920x1080 with SSAA4x. The different aspects explain the bars. A copied preference snapshot is `build/window-resize-lab/reported-video.cfg`; user settings are untouched.

Every staged Video change previously reapplied the window style/size and released mouse capture, including unchanged output and fullscreen size preferences. The handler now skips those mutations when actual bounds/mode already match. Isolated tests observe real window mutation messages and verify pending keyboard pulses across no-op and actual transitions.

Three late Escape/Start pulses still reached game polling, ruling out a lost native focus/message pump as the sole explanation. Original Apt `823AC258` gates menu events using `isOkayToAcceptUserInput`; original transitions disable and then re-enable it. No callback register/stack corruption or specific stuck gate is confirmed. Added a read-only byte-pinned gate-return trace at `823AC2E4` that logs per-caller result changes while retaining the original gate and epilogue. No speculative Apt behavior override. Gameplay verification remains with the user.

Both final native/release games and recorders are rebuilt with the window fix and gate trace. Eight focused checks passed in each configuration, including real unchanged-window mutation checks, original controller/keyboard handling, presentation, wide GPU/camera fixtures and host floating-point preservation. Logs: `build/window-resize-native-tests.log` and `build/window-resize-native-release-tests.log`. One parallel release GUI run lost a queued pulse while competing test windows were active; its standalone check and the full serialized release suite passed unchanged. Run GUI fixtures serially across build directories. User video preferences remain unchanged. The reported gameplay freeze still requires the user's retest.

## Missing ultrawide menu choices

The user reports Right from 3840x2160 returns to 1280x720. Their two latest runs, `20260930-191530-022144` and `20260930-192142-312785`, load the current native game and derived character Options package; the latter cancels the menu. Neither old log records which native row/action or internal preset was selected. Linked-code inspection confirms the active Save callback calls modulo-nine stepping and its active label getters accept all nine correct width/height table entries in both native and release games. Both packaged Options resources exactly match the current generator. The visible five-choice wrap remains unexplained by this static evidence; do not claim gameplay verification.

Changed the native Render resolution cycle to persisted IDs `0,1,2,5,3,6,7,4,8`, placing 2560x1080 directly after 1920x1080 and 3440x1440 after 2560x1440. Wide choices explicitly say `Ultrawide (restart)`. Saved IDs and their dimensions retain their previous meanings. Enlarged the separate Apt value slot to 64 bytes and moved method names to +0xA0 (80 bytes), with bounds checks; both buffers avoid variadic argument spills and the saved LR. Logs now include menu action, row, direction, old/new internal index and the full selected label, plus startup/apply internal extents. The user will test the native menu; no game automation.

Both game/recorder pairs and launcher dependencies are rebuilt (native 19:32, release 19:31). AOT verifies 311 files with zero semantic diagnostics. All three focused CTest groups pass in each build: NativeVideoSettings, NativeVideoMenuAssets, NativeGameLauncher; logs `build/ultrawide-menu-native-tests.log` and `build/ultrawide-menu-native-release-tests.log`. The package group now has three Python tests, including an independent evaluator of actual appended Apt bytes: all 16 row/direction exports, getter reset/selection retention, parameter register binding and full wide-label setters. It mocks method receivers and does not execute the original AVM scope or gameplay. Independent review confirms the callback-buffer bounds and stable saved IDs. The actual preference hash remains `06167B1D84048E19068494A269FCAC64CA6A33BD26D4BA1EA4DEBBD4C616F033`.

## New post-level crash and poor performance

Reported run: `build/render-tests/20260930-193712-804870/game.log` ends at unimplemented graphics boundary 82740680 / caller8273B4E0, source820465E8 `simpsons_rigid_dualtextured_uv`, flags0 and technique_AC7FFFC. New source requires native opaque and alpha animated-UV shader pairs, two material textures, and opaque character shadow at stage0. Repair source/executable/crash/preference snapshots are under `build/level-transition-fix/before` and `reported-crash.log`.

Parallel work: root integrates runtime/profiles/material/texture guards; shader agent transcribes original UV shaders and backend/CMake; original-code agent pins maps and builds CPU regression fixture; performance agent reduces private Boolean inspections and routine success logging. Current user preference hash AE4573241B7EC9C43E0554FEC30A0764DBEFCD93DDE56DFFEB4DD404D3169140. No preferences/saves changed and no gameplay automation.

Performance evidence: latest menu actions plus saved settings prove the run started with SSAA4x +16x filtering at3440x1440, frozen until restart, yielding6880x2880 actual scene pixels. Saved Original/Original applies at next launch. CPU inspections previously copied/read ~1100 private words for one Boolean; new helper retains full provenance/access validation but reads selected word only. No gameplay FPS gain claimed without measurement. Build and regression checks pending.


### Rigid dualtextured UV shader evidence

Retail source `820465E8` SHA256 `aca225b7e054662efed2e63c5052e2aad8bc4199dd327e2a7c13db8d7a30abd4` is distinct from ordinary dualtextured `8202AD78`. Opaque records are VS `82046D9C` (1396 bytes, code +856/540, CF5, SHA `e4c391bd252f9b4656eddf902fcf9833e78d062990ba85d973aa97e0197a9fa9`) and PS `820477A8` (1940 bytes, code +1088/852, CF10, SHA `91f1ed5d3c5d6c266e68acbe12c02d3273d7ee5f6ecc318565428bc4c3b1b1d3`). Alpha records are VS `82047318` (1152 bytes, code +732/420, CF4, SHA `a0c773625e06cd2a962b93842e68079f1cbf371689cbf0aba49e9de334ba0ec2`) and PS `82047F44` (868 bytes, code +484/384, CF7, SHA `e46ae17e3a92fd19a4021987405ee4821f2fccc21f40de9907ecbb239faf25fd`). Full executable coverage is opaque39/60 and alpha30/24 VS/PS slots.

Dedicated transcription `tools/analyze_rigid_dualtextured_uv_shader.py` preserves original sine/cosine motion, UV frequency/amplitude/scale/velocity leaves VS c43..47, ticker c22, texture flags PS c42, split scalar ADD_CONST_1 opcode45, complete forward predicate/unconditional CF, all literal banks, and coissued old-register RHS reads. PS literal c248..255 requires a local operand mapping, unlike older c252..255-only shaders. Opaque t0 is shared CHARACTER shadow depth with matrix c26..29, material textures t1/t2; alpha materials t0/t1 and no depth samples. Opaque adapter alone expands D24FS8 depth RRRR. New native entry headers are VSRigidDualTexturedUV, PSRigidDualTexturedUV, VSRigidDualTexturedUVAlpha, PSRigidDualTexturedUVAlpha and both PS*Draw adapters. FXC input signatures exactly match existing ordinary5-element mesh layout (alpha first4), verified by inspecting generated DXBC headers.

Backend direct/record support uses RigidMeshDraw.uvTextures[2]/uvSamplers[2], retains both replay owners and COM views, and validates only shadows[0] for opaque while requiring absent shadows[1]. Original family registrations retain distinct record identities. No native constants bank expansion needed (existing VS48/PS51 covers all reads).

Verification: all6 source/inventory/original-control regression tests passed, including207 header/literal/CF/semantic-association mutations rejected independently of record hash. Native numerical fixture independently decodes and executes original retail CF/ALU; it never evaluates HLSL for expected outputs. WARP and hardware each passed32 cases:16 moving vertex+UV transformations and16 opaque/alpha material modes with receiver samples both enabled/disabled. Material fixture sampling positions vary on binary fractional points so D3D subtexel filtering precision does not contaminate the4e-5 arithmetic tolerance (initial arbitrary fractions differed by ~6e-5 in texture interpolation). CTests: OriginalRigidDualTexturedUVInventory, NativeRigidDualTexturedUVShaderWARP, NativeRigidDualTexturedUVShaderHardware. No gameplay automation performed by shader work.

Alpha UV mesh integration exposed an initial input-seeding error despite the first numerical fixtures: the alpha VS semantic00100004 is position fetchslot4 -> r0.yzw, while normal fetchslot5 -> r3.xyz. Corrected native alpha seed and changed the independent oracle to execute original fetch destination swizzles against pinned declaration order, rather than duplicate native seed assignments. Added explicit distinct-position/normal zero-motion coverage; all7 offline tests now pass. Rebuilt native UV and mesh targets; NativeRigidMeshWARP/Hardware plus NativeRigidDualTexturedUVShaderWARP/Hardware all passed after correction (5 CTests including original inventory, 7.52seconds). Opaque first-pixel visibility assertion was too narrow under halfpixel viewport; parent now compares the complete target to its clear readback. Stale common prologue comments naming ordinary-rigid records were removed.

### Original UV CPU dispatcher regression

Independently pinned both original context tables: opaque at body+30F0, alpha at body+3440, private bank172 words/28 leaves. Material leaves17..21 map words92/96/100/104/108 to VS c47/c46/c45/c44/c43; leaf22 word112 maps PS c42 in both passes. Opaque leaf14 word80 maps PS c49 and leaf27 word168 maps PS c48. Inherited leaf23 word116 maps TimeTicker to VS c22 and is excluded from material masks. Base texture leaves25/26 store original headers at words136/152, binding opaque stages1/2 and alpha stages0/1. Opaque shared leaf5 maps the CHARACTER light matrix to VS c26..29 and shared leaf7 maps CHARACTER depth to t0; world-shadow leaf6 is unused. Added exact original mapping-table, dirty-partial projection and profile checks to `tests/test_rigid_material_constants.cpp`, plus source/owner/Boolean fallback admission checks to `tests/test_rigid_packet_owner.cpp`.

Original827400F8 overwrites incoming r5 with packet+4 at82740120 before its first use; original82740680 derives incoming r4/r5 as Boolean metadata/property selectors and moves them at82740B1C/82740B18. Thus UV's qualified r4<=1/r5<=1 includes the genuine alpha1/1 route without dropping any live argument. Whole-original fixture exercises that exact route.

New `tests/test_rigid_uv_pass.cpp`/CTest `OriginalRigidUvPass` passed232 checks in native (direct run0.98seconds; log `build/rigid-uv-pass.log`). It runs original startup to the pre-FX boundary, registers/reflection-finalizes all49 effects, reuses the genuine untouched original recording singleton, loads unchanged `build/itxd-palette/loc_split4.itxd` through original826F26B0 and the verified8271191C request frame, and selects two distinct constructor-owned texture headers (RGBA Simpsons palette and BC3 fire texture). Both complete original82707220 shadow parents establish scissor and world/character depth uploads. Original8273B4D0 ->82740680 then builds and executes one opaque UV recording, replays identical pixels without rerunning material rows, changes the authored timer and verifies original8270C2A8 updates VS c22 from0.125 to0.25 in the same retained payload, then completes original827400F8 alpha fallback with both texture/vector callbacks and real center/bounds output. It checks original CPU pool borrow, LRU, owned-byte accounting, retained cache/payload association, private dirty clearing, shader/source immutability, geometry immutability, both original56-register upload banks, and nonvolatile GPR/FPR/SP/LR preservation. Test input uses SDK D4=15 for RGBA writes; an initial depth-fixture mask0 correctly made immediate alpha invisible and was corrected in the test. Completed cached records intentionally remain resident for terminal runtime teardown, as in the existing recording graph fixture; original cleanup success is not asserted. No game automation or user save/preference changes were performed. Root owns the final serialized native/release regression results.

### Final post-level repair verification

Both native and native-release game/recorder executables are rebuilt with the new animated UV material and performance changes. All 16 focused CTest groups pass in each build; logs are build/level-transition-fix/final-native-tests.log and final-release-tests.log. This includes whole original UV dispatcher/cached replay/alpha fallback (232 checks), real hardware/WARP numerical and direct/recorded GPU paths, material maps/owner guards, original mono/zprepass/static-shadow, audio lifecycle, viewport copies, video settings, original artifact inventory and driver lifecycle. Final AOT verification: 311 files, zero semantic diagnostics, log final-aot-verify.log. An independent image-byte review verifies the corrected alpha input mapping; the GPU oracle now executes original fetch swizzles rather than reusing the native register seed.

Final source eligibility guard validates shared handles unconditionally and requires inherited exclusion only for bindings consumed by the selected original pass. The original union SDK zero-work proof remains unchanged; real reflection of all 49 effects provides the exclusions. Material compiler inventory is 68 compiled / 188 unsupported, matching four added exact original shader identities with no ownership leaks. GPU mesh comparisons use the whole clear target, since the original half-pixel viewport can leave pixel 0 untouched.

Video preferences remain unchanged: SHA256 AE4573241B7EC9C43E0554FEC30A0764DBEFCD93DDE56DFFEB4DD404D3169140. Startup and apply logs now disclose actual scene dimensions, AA and filtering, and the restart requirement. Saved Original AA/filter applies at next launch; no measured gameplay FPS improvement or exact end-of-level route verification is claimed. No automated gameplay performed. See docs/level-transition-crash.md and README.md for the repair and evidence.

## First-mission completion launcher request (2026-09-30)

User requests a launcher immediately after completing Land of Chocolate and explicitly chooses the mission-complete transition itself. Skills read: mod-any-game, game-recon, reverse-engineering plus native/retro-decomp playbooks. Continue original recompilation/hooks; no loader or gameplay automation. Source snapshots are under build/mission-completion-launcher/before. Primary profile/save/video preferences will remain unchanged: the new launcher uses a private copy per run so original completion autosaves remain isolated. Read-only evidence confirms the primary slot still displays 0:35.98 (0%) The Land of Chocolate; no completed mission save was published by the crash run. Existing input checkpoints replay from the start and do not restore the world. Route investigation and launcher implementation in progress. No worktree cleanup or recursive deletion is requested or needed.

Implemented opt-in `--first-mission-completion` with borrowed original `-stream loc loc.str`, a ready receipt at retail823BB5D8 after original map-start8289ED68, and one original EpisodeComplete8296FFC8 dispatch through EngineCpuCalls. Exact episode/mode/map/source/score/successor/manager/owner checks retain original startup semantics; original movie state must be idle with no decoder and pause/exit/restart retry safely. Natural success0x40000 is consumed before map/owner gates and from the movie skip predicate, preserving the original outro even after map removal. Only bootstrap movies are automatically skipped in this mode. The original handler still owns completion flags, scores, outro/results and next episode.

Native build and six focused CTests passed in2.29seconds, including386 CPU completion checks, launcher copying/quoting, original controllers, checkpoint controls, frame-rate behavior and CLI parse-before-write. Release completion/controller/timing/CLI tests also passed; release launcher self-test exposed a long Win32 copy path and is being repaired before final verification. AOT verification passes311 files with zero semantic diagnostics. No game process or automated gameplay was launched. See docs/first-mission-completion.md; final verification follows.

Final verification: both game/recorder/launcher builds are current; all six focused CTests pass in each build (native 2.65 seconds, release 2.54 seconds). The launcher now uses normalized absolute extended paths only for filesystem operations, retaining ordinary command/display paths. Added drive/UNC normalization and actual source/private save paths exceeding MAX_PATH, including read/write/source-isolation checks. Source junction rejection and non-following reparse scans before owned self-test cleanup remain intact. Logs: build/mission-completion-launcher/final-native-tests.log and final-release-tests.log. CPU fixture passed 386 checks, running the real borrowed argv setter, name hashes and EpisodeComplete helper with only the world-dependent completion body observed. Live outro/results display remains untested; user performs the manual run. Main profile/save-index/video hashes match the unchanged originals recorded in source-store-hashes.json. Dedicated shortcut: Play First Mission - Completion.cmd.

## Completion follow-up crash and direct launch (2026-09-30)

User reports a new crash and requests launch without a startup window or Play click. Latest manual run is build/mission-completion-runs/Completion-20261001-015123-814Z-70244-0000; its log reaches original EpisodeComplete and later fails at827400F8 with "Unqualified original opaque rigid fallback entry", typedE1AACAF0 packet82D6EE08 r4/r5=1/1. Original live reflection/owner diagnostics in the same log resolve that typed owner to ordinary simpsons_rigid_dualtextured source8202AD78 identity00500024, rather than animated UV. The source admission helper allows r4=1 but omits the incoming r5 Boolean flag for that source. Original dispatcher/body evidence and whole-draw regression are being checked before final qualification. Route remains the existing AOT/native hooks; no loader, game asset changes or gameplay automation. Installed mod-any-game, game-recon and reverse-engineering instructions read again. Before-source snapshots and primary store hashes are under build/mission-completion-crash. Launcher direct dispatch preserves isolated completion copies and logs.

Original bytes pin metadata+8 bit1 to r21 at827408B8 and its move to incoming r5 at82740B18; F8 overwrites incoming r5 with packet.object at82740120 (80BF0004) before any use. Source8202AD78 now admits both original Boolean flags, retaining all packet/typed/context/material/draw guards. Its existing alpha shader executable matches the qualified168F8 alpha pair; WARP, hardware and original inventory checks pass. Native direct-launch/game build and six existing regressions pass. New OriginalRigidDualPass executes original8273B4D0→82740680→827400F8: alpha1/0 and crashing1/1 both complete with identical pixels; the added opaque comparison exposed fixture blend state left over from alpha and is being corrected without changing runtime blend qualification. New Windows .lnk shortcuts point straight at the GUI launch helper with no command-window flash; their properties and hashes are in shortcut-validation-f6db57feeca044f9a175816961e12974.json. No shortcuts or gameplay were launched.

Final follow-up verification: native and native-release game/recorder/direct-launcher builds are current. All 11 focused CTest groups pass in both builds; native logs are native-existing-tests.log (six groups, 8.30 seconds), shared-alpha-tests.log (three groups, 0.99 seconds), final-native-dual-tests.log (two groups, 1.02 seconds). Release final-release-tests.log passes all 11 in 10.57 seconds. Two missing release numerical-test binaries were built before the final clean run; the initial Not Run log is preserved as release-tests-before-alpha-build.log. OriginalRigidDualPass passes 230 checks with all four Boolean pairs supplied by real original metadata-driven dispatch, equal alpha1/0 versus1/1 and opaque0/1 versus0/0 pixels, real source/texture/register associations, immutable source/geometry, dirty clearing and full nonvolatile ABI. Fixture neutral outer-pass state is reset per variant; no native blend qualification was changed. NativeRigidPacketOwner passes 210 ownership/profile/Boolean rejection checks. Final AOT verifies 311 files with zero semantic diagnostics. Primary profile/save-index/video hashes match source-store-hashes.json. Headless helper creates no startup UI; preferred Play First Mission - Completion.lnk and Play The Simpsons Game.lnk also avoid shell-console flash. Their properties were read back without launching. No automated gameplay; live manual retest remains the limit. Source evidence and build/test logs are under build/mission-completion-crash, details docs/dualtextured-completion-crash.md.

## Loading screen after mission recap (2026-09-30)

Latest manual log: build/mission-completion-runs/Completion-20261001-025225-143Z-24408-0000/logs/Simpsons-20261001-025225-143Z-24408-0000.log. Failure at827400F8 has flags1/1, typedE1AAD2D0, packet82D6E9D0; the same run's real reflection resolves it to simpsons_rigid_gloss source82019988 identity00500025. Previous ordinary dual alpha draw now succeeds. Gloss's alpha shader pair is distinct and was missing, so argument admission alone is insufficient. Original maps and shaders also expose related multitone820547E8 and normalmap82057E08 alpha gaps; all three use a single stage0 base sample without shadow/noise/normal textures. Source/log backups and unchanged primary store hashes are under build/post-recap-crash/before. Relevant universal-modder skills remain in use. Parallel original metadata, numerical shader oracle and whole-original dispatcher regressions accompany selected-pass runtime/backend integration. No gameplay automation or launcher UI is introduced. Build/validation pending.

Final post-recap verification: both native and native-release game/recorder/direct-launcher builds are current. All22 focused CTests pass in each build (14.81/13.87 seconds), logs final-native-tests.log and final-release-tests.log under build/post-recap-crash. Whole-original dispatcher fixtures pass328gloss/331multitone/414normalmap checks, all4Boolean pairs plus normalalpha without unusedtangent; olddual295/UV232 and owner257checks also pass. Independent original-instruction numerical oracle passes72cases each on WARP/hardware; seven offline tests cover source identity, CF/fetch/linkage mutations, scalar45 and zero-input behavior. Original gloss scalar45 means pc40.z + old r1.x and preserves old co-issued operands; the source was pinned against installed original-format headers. Six exact original shader identities raise compiler population68→74 (unsupported188→182), with no ownership leaks. Opaque front materials stay mapped as before; alpha uses selected PSrows, base t0 and no extra textures/shadows, normalalpha ordinary declaration, glossalpha no UV1 requirement. Additional failure diagnostics now include source/identity. FinalAOT311files/zero diagnostics. Primary profile/save-index/video hashes match before/primary-store-hashes.json; final-shortcut-validation.json confirms unchanged directlauncher targets/arguments. No gameplay, shortcut launch, save mutation or worktree cleanup. Full manual transition and gameplay FPS remain unverified. Details docs/post-recap-gloss-crash.md.

## Authorized live post-recap repair (2026-10-01)

User authorizes testing this exact issue until fixed: skip completion cutscene, continue through recap, reproduce subsequent crash. Latest manual log Completion-20261001-120603-900Z-12972-0000 reaches next-area sky/rigid/particle draws and fails ITXD L8 base descriptor/allocation validation. Original alpha fixes are passed. Continue existing AOT/native route; use opted-in native controller commands and completed renderer readbacks so inputs never target other desktop apps. Primary stores are backed up/scanned for reparse points and each live launch uses a new private copy. New failure diagnostics expose exact L8 owner, name, descriptor, allocation and caller. Lab/evidence build/recap-live-fix; user authorization supersedes prior no-gameplay-testing preference for this issue. No loader/registry/primary-preference changes or cleanup requested.

Live repair completed: exact authored beam1 metadata/payload proved a three-level L8 chain (64/32/16 pixels, 12,288-byte tiled allocation). Added strict chain decoding, immutable RRR1 uploads, and independent inverse-address fixtures for eight cases/70 levels. The live route then exposed a second failure: original stream P+2C retains initial request token300 while the next actual B carries token401. Original preload/refill/claim instructions prove this is legal; producer now uses mapping-checked B[0], preserving every reader/owner/sequence/catalog/continuity guard. Next live run exposed native snapshots incorrectly bounded by original GPU command capacity: eighth 1,624-byte snapshot exceeds0x3000. Added a separate1MiB host policy, geometric storage growth before GPU callbacks, original allocation metadata unchanged, actual native-byte cache accounting retained. Original 15,000,000-byte cache quota/eviction remains in force. Source recoveries and all failed logs remain preserved.

Successful visible live run: build/recap-live-fix/runs/20261001-124311Z-0df438be, ownedPID70112. Original outro accepts Start and releases decoder; two recap pages continue into Simpsons' living room. Completed displayed captures show Homer/Bart, current scene geometry and live animation. Ran350.389seconds/20,497presentations, no failure until deliberately posting WM_CLOSE to that exact verified game window. Movement response is not claimed. Current native executable SHA matches the tested executable; final-live-verification.json records completed captures and zero unexpected failures. Main profile(1file), content(253files) and video all match before copies, including primary save/index hashes. No worktree/archive/recursive cleanup.

Final builds: native and native-release game/input-recorder/direct-launcher current. All18focusedCTests pass in each build (7.54/7.10seconds), including250original reader-claim checks with two genuine requests/distinct tokens and strict guard mutations; 70mip readbacks per GPU backend; original texture/reader/EXm0 lifecycle; launcher/completion; and recording payload/depth/owner checks. Recording payloads pass7,308checks per backend, 24full-size snapshots crossing both guest capacities, exact replay pixels/depth, caller mutation/retirement and host-limit+1 rejection before emission. AOT verifies311files/zero semantic diagnostics. Existing direct completion .lnk remains unchanged, target build/native/SimpsonsLauncher.exe --first-mission-completion; no startup UI. This closes the reported loading-transition crash; first-level performance improvement is not established by this test. Details docs/live-recap-crash.md; receipts build/recap-live-fix/final-native-tests.log, final-release-tests.log, final-primary-store-hashes.json and final-shortcut-validation.json.

## New material crash and resource audit (2026-10-01)

Reproduced the latest post-recap crash during movement: material header 00F00020 was a live built-in white image, incorrectly routed through copied ITXD ownership. All production material texture calls now use one resolver that accepts only exact, fully published built-in owners or strict copied ITXD headers. Rejected aliases, foreign callers, incomplete publications and retired owners remain failures. Selected-pass maps now distinguish an unused sampler from used stage0, avoid inactive skin words and shadow lanes, and correct sky fallback slot3 to its authored line texture.

The next live test exposed missing simpsons_skin_textured source8200FB98 at boundary82740680. Added its exact opaque/alpha shaders, original constant/sampler maps, nondual geometry and separate skin mesh cache; opaque depth sampling retains native RRRR behavior. Also completed the already-selected dual-skin alpha shaders. Original instruction evidence proves incoming r5 is overwritten before use in the skin fallback; both original Boolean values are now accepted while source, caller, context, ownership and frame guards remain. Native shader population74→80. Three whole original skin fixtures pass1,506checks covering12draws/all four Boolean pairs, real two-joint bone composition and original material callbacks; geometry/morph coverage limits are documented.

The asset-wide texture census found one additional gap: an authored16x16RGBA8 tiled palette in Medal of Homer. Added its exact4096-byte layout with original descriptor controls retained. All7,318 shipped texture metadata occurrences now satisfy current admission; this is not an all-texture pixel/GPU census. Audio audit checks9,470streams/377,643blocks against7,430original files, plus45,382SBK and254AMX cues, with no format/storage violations. Full streamed decode, unseen reader extents and live seek/restart/cancellation remain unproved. The effect census covers49effects/110passes/256immutable identities:33artifact-complete passes,30runtime-declared selections and77missing-artifact passes. Missing rows are tracked as coverage gaps, not claimed as observed crashes; VFX emitter semantics remain opaque.

Final native live run20261001-181937Z-c722f3a6 completed skip/recap/loading, then movement/attacks/special/jump/character switching. It selected the repaired textured-skin pass51times and completed original fallback draws. No unexpected failure over538.61seconds/28,489presentations; deliberate close targeted only the verified game window. Completed renderer capture shows Homer/Bart moved by the fireplace. Tested executable SHA matches current native binary; all254primary profile/content files and video settings match backups. Native and native-release each pass55focusedCTests (25.52/26.30seconds), including hardware/WARP, old texture layouts, original shader/dispatcher and audio/recording regressions. Optional built-in plus actual ITXD retirement fixture passes3,205checks in each build. AOT verifies311files/zero semantic diagnostics. Direct completion shortcut stays unchanged with no startup UI. No gameplay FPS improvement is claimed. Report: docs/resource-crash-audit.md; receipts: build/resource-crash-audit.
## House-exit crash sweep and direct Bartman Begins (2026-10-01)

Fixed the original auxiliary skin stream guard to validate the actual owned geometry's 48- or 56-byte stride and descriptor. Whole original dispatcher fixtures cover the real auxiliary lookup and strict malformed-call rejection. Qualified dynamic audio-ring production from the original factory/clock/allocator instead of two observed sizes; retained stream/buffer ownership and default-device routing. Full streamed decode covers 9,470 streams, 9,256 unique chains and 377,643 blocks with zero failures.

Added nine exact original game-material passes: skin gloss/flipbook/dual-UV opaque and alpha, rigid projected texture opaque and alpha, and Chocolate opaque. Exact selected banks, samplers, input signatures and source identities remain checked. GPU regressions caught unused input signatures, legacy zero multiplication and the flipbook frame-33 reciprocal boundary; WARP and hardware now pass unchanged cases/tolerances. Compiler inventory is 108 native artifacts; game-specific table1 admits 40/42 passes, with alternate AA and legacy catalog/emitter limits documented.

Native and Release each pass all 138 selected tests after the new direct-stage mode (117.58/112.54 seconds); AOT verifies 311 files with zero semantic diagnostics. The completion-route live retest reaches Springfield outside the house over 371.24 seconds/17,959 presentations without an unexpected failure. The new Play Bartman Begins.lnk dispatches the original brt/brt.str stage directly through the GUI helper, with no startup window and separate private copies. Actual GUI dispatch and a captured 443.21-second/12,602-presentation opening-area run pass; eight movement/weapon/interaction/character inputs are acknowledged. Both owned windows close normally. All 254 primary store files/video preferences remain unchanged; existing shortcuts retain their routes.

Bounded recording pointer reuse and exact checked matrix inlining retain all guards and differential behavior. One matched stationary-house sample changes 34.6861 to 41.1224 FPS; this combined-build observation does not isolate the optimization or establish first-level/whole-game performance. Evidence and remaining limits: docs/house-exit-crash.md, docs/bartman-begins-launcher.md and build/house-exit-fix. No worktree cleanup or merge.
## Claude performance handoff continued (2026-10-04)

Recovered the 120 FPS/1440p session from the local transcript and `build/perf120` artifacts. Continue the established offline XenonRecomp/native D3D11 route; no loader is needed. Read installed mod-any-game, game-recon and reverse-engineering skills and native/retro-decomp playbooks. The workspace is a source folder without a root Git repository. Recovery sources and 255 primary profile/content/video hashes are preserved in `build/perf120/codex-continuation`.

Claude's e13 snapshot passed all 528 CTests. Completed e11/e13 A/B pairs support lower CPU/frame, but variable desktop load and remaining frames over 8.333 ms do not establish smooth 120 FPS. Independent review found no new correctness regression in camera memo dependency generations, raster record invalidation, immutable COM proofs or pooled recording events. Added and passed focused memo regressions for watched/unwatched permissions, remapping, nested generations and 256/257-page limits.

The benchmark now records normal versus forced shutdown, actual video settings, window acceptance and the 120 FPS budget. Exact requested WM_CLOSE is expected to return native status 1 with the single `Native window closed` reason; other failures remain rejected. A completed renderer readback proves 2560x1440 scene/front rendering at the stationary first-mission view. Its capture/timing run had high external load and is diagnostic rather than speed evidence. CPU samples and frame intervals retain different time origins, so CPU/frame is approximate; output window remains 1280x720.

PGO refresh completed under `build/perf120/pgo-runs/codex-20261004-1953`: three successful training workloads merged into `game-8ca14762.profdata`, now active in `build/native`. Complete production/test build and all 528 CTests pass (187.78 s). New build SHA256 `DAEC804AFC1AEE2844F7B37E18FA55F335E41AB1B4BC5DD36BFB113A8958B234` matches frozen `build/perf120/codex-pgo`; existing GUI shortcuts still target native. Original frame-worker signal/wait ordering is retained. Closely matched stationary pair improves 136.67→139.30 FPS (+1.92%); continuous uncapped movement improves 175.33→179.36 (+2.30%) in one same-route pair. Higher-load pairs cannot establish an isolated gain. The clean continuous 120 cap run averages 119.11 FPS, P99 9.818 ms/1% low 101.9, with 40 acknowledged movement segments, 100% accepted displays and no unexpected failure; occasional dips and variable external load remain. Scope is original 1440p internal rendering/Original AA in a 1280x720 output window on opening LOC routes; later missions and full-size desktop presentation are unproved.

Cache re-proof mode passes a real plaza walk with no mismatch and a completed viewed 2560x1440 capture. Found and separated a diagnostic contamination: capture-on-request limits front screenshots but a nonempty capture directory still rewrites billboard files every batch and checks recording request files per replay. Its 67.4 FPS continuous result is diagnostic-only; the same no-capture route exceeds 175 FPS. Benchmark summaries now disclose that mode, normal/forced shutdown, actual settings, acceptance and deadline/tolerance metrics. Final receipts and detailed measured limits: `build/perf120/NOTES.md` and `build/perf120/codex-continuation/final-verification.json`. All 255 primary store/preference hashes remain identical. Original binary/cache/assets/profiles, failed/diagnostic logs and recovery files are preserved. No primary settings changes, recursive deletion, worktree cleanup or PR merge.

## FOV and expanded graphics settings (2026-10-04)

User requests an FOV setting and normal additional graphics controls. Continue
the established offline native/XenonRecomp route, editing the existing original
APT Video menu and native D3D11 renderer. Read installed mod-any-game,
game-recon, reverse-engineering, native/retro playbooks and game-automation.
Community check confirms the existing compiler/runtime separation in
https://github.com/hedge-dev/XenonRecomp/blob/main/README.md; no loader is needed.
Recovery source, binary, cache and derived menu copies are under
`build/graphics-settings-20261004/before`; primary profile/content/video hashes
are preserved separately in `primary-store-hashes.before.json`. All live runs
use private copies and native controller commands, with desktop input excluded.

Added v5 preferences retaining versions 1–4 and original defaults. New controls:
FOV Original or 60–110 horizontal 16:9 reference degrees in 5-degree steps;
render scale 50/67/75/100/125/150/200%; live Bloom/DOF/Motion Blur On/Off; frame
limits 30/60/90/120/144/165/240/Unlimited. FOV applies through original setter
823F1C98, preserving authored zoom and original dirty-frame/frustum sync.
Runtime-owned source/publication provenance prevents accumulation and restores
authored bits when selecting Original. The reference is the original factory's
60-degree horizontal tangent, pinned at 827142D8 and observed in the live main
scene camera. Authored zoom and cinematic changes remain relative to that baseline.

Effects suppress only their validated native GPU submissions at the original
screen pass End: DOF82754288, Blur82754C90, Bloom82755508. Preparation,
request resets, binding restoration, depth copies and per-frame blur-history
refresh continue. Render scale changes scene/depth/post/HUD targets before
SSAA samples while preserving the output client and selected camera aspect.
Unsupported resolution/AA/scale combinations are skipped before publication;
scene budget is the existing 4K SSAA maximum of 33,177,600 pixels. Fractional
viewports and odd scissor rectangles retain their original logical values only
with matching actual native-state/target-scale receipts. Game clocks remain
real time; expanded caps change presentation pacing and clear old cadence history.

APT has thirteen rows using the existing font/controller navigation. Original
brightness remains first, followed by the existing controls then FOV8, scale9,
Bloom10, DOF11 and Motion Blur12. Relevant camera, effect on/off/re-enable,
scaled resource/presentation, persistence/migration, frame timing and AVM action
fixtures were extended. The live main camera uses original end forwarder
82A77EE0, admitted only when saved callback82E28170 is registered823D1160.
All18original view-window setter calls were audited. Source tracking excludes
the two exact saved-window restorers827F5D24 (APT) and826D48F4 (temporary effect),
while still observing an authored write identical to the previous override.
Real writer/forwarder/setter bytes, frustum/ABI, both menu/effect restorations,
authored zoom, custom/Original and foreign callback rejection are fixture-covered.

Capture review found that below100% the original half-texel point kernel lost
ink edges when still divided by larger logical dimensions. Effective native
edge/AA/cel-compositor sampling dimensions now clamp per axis to
min(logical,physical), after exact original logical-constant validation.
Existing>=100% behavior remains. WARP/hardware differential fixtures compare
complete packed outputs at50/67/75% against equal-physical-size Fixed targets,
including both boundary axes, half-integer radii and strict malformed constants.

Final native production/test build and all528CTests pass (194.38seconds).
AOT verifies311files with zero semantic diagnostics. Private run
`live-20261004-221905-562904` validates thirteen readable menu rows, live stock
60→110degree FOV, Cancel restoring60degrees, Accept/save, restart with75% scale
(960x540 scene/front, independent1280x720 output), restored ink outlines, and
Original returning exact authored stock projection. Completed scene captures
have fresh geometry/depth telemetry. Both owned windows close normally through
WM_CLOSE with no unexpected failure; no desktop input was injected.
All255primary profile/content/video hashes remain identical. Final receipts:
`build/graphics-settings-20261004/final-verification.json`, `ctest-complete.log`
and `live-complete.log`; user guide:`docs/native-video-settings.md`.

Capture-enabled runs are correctness evidence only. PGO USE configuration retains
the prior profile; changed-function profile mismatches fall back to compiler
code generation. No new FPS gain or all-mission coverage is claimed for this
feature update. Original assets and recovery files are preserved. No worktree
cleanup or PR merge.

## Enter and WASD menu navigation (2026-10-04)

User reports Enter pauses gameplay and WASD menu navigation advances too fast.
Continue the existing offline XenonRecomp/native runtime route; installed
mod-any-game, game-recon, reverse-engineering, native/retro playbooks and
game-automation instructions were read. The um CLI remains unavailable; use
existing source-pinned CPU fixtures and private game-local window/capture tools.
Source and native/release executable recovery copies are preserved under
build/keyboard-menu-fix-20261004/before. Primary profile/content/preferences
hashes are recorded in primary-store-hashes.before.json (255 files).

Source of truth: original Apt dispatcher 823A1EB0 uses digital fresh/held query
returns at 823A206C/823A2090 and repeat delays 400/80/40 ms. Its analog path
823A21E0 accumulates full-strength WASD per frame, causing rapid menu stepping.
Four byte-pinned hooks route the last ordinary keyboard poll's WASD directions
through existing digital queries, and clear only the two left-stick vector
outputs at the menu consumers 823A21E0/823A2298. Original input-manager state and
continuous gameplay movement remain available to gameplay consumers. Physical,
command-stick, playback, modal, movie and retired sources invalidate this
keyboard-only navigation snapshot; right-stick mouse output is retained.

Enter supplies A/select outside movie ownership; Escape supplies Start/pause.
Movie-owned Enter still supplies the existing fresh Start skip, and a held skip
releases before confirming another menu. Fresh key transitions clear that latch.
Implementation/build/CPU and private live verification are in progress.
Community route reference checked:
https://github.com/hedge-dev/XenonRecomp/blob/main/README.md

Completed: native and release SimpsonsNative / SimpsonsInputRecorder builds are
updated. All eight focused CTest groups pass in each build: controllers (1,288
checks), checkpoint controls (95), movie action audit (3,458), original menu
navigation (798), native storage selector, WARP/hardware prompt rendering and
prompt assets. The full original menu fixture verifies all four direction rows,
strict initial >400 ms, subsequent >80 ms and accelerated >40 ms boundaries,
source/slot/binding gates, output bounds and host/PPC ABI. Diagonal navigation
retains the original table's first-eligible-event priority; an initial fixture
incorrectly expected two events and was corrected to the actual dispatcher.
The movie-held Enter fixture was also aligned with the existing unarmed Start
policy during a second movie; no fresh skip or pause leaks outside movie ownership.
AOT verifies 311 files with zero semantic diagnostics. Changed PGO functions use
ordinary code generation when their saved profile control-flow hash mismatches.

Private live run live-20261005-025143Z-934018 validates Enter A/select while the
Homer scene keeps rendering, Escape Start/pause, S -> Options -> Enter -> Video,
W/S one-row movement, D/A 200 ms holds producing exactly one row1 action each,
D400 ms producing one action and D650 ms producing four. Cancel restores exact
private preferences; the owned background window closes normally by WM_CLOSE.
Input addressed only its disabled HWND; no desktop/global input, foreground or
cursor takeover. Completed scene/menu captures were viewed. All 255 primary
profile/content/video hashes remain identical. Recovery files, captures and
receipts remain under build/keyboard-menu-fix-20261004; final-verification.json
records current source/binary hashes and test results. Title-specific Enter/A
acceptance is not separately live-verified; normal Apt ACCEPT dispatch is present.
No PR merge, worktree cleanup or game asset changes.

## Mouse menus and widened FOV visibility (2026-10-04)

User requests mouse-operable menus and repair for objects disappearing beyond
stock FOV. Continue existing offline XenonRecomp/native renderer route. Read
mod-any-game, game-recon, reverse-engineering and game-automation skills plus
native/retro-decomp playbooks; um remains unavailable. Original image/assets
stay read-only. Source/binary recovery copies retained under
build/mouse-fov-fix-20261004/before (additional agent-specific backups nearby).
Use source-pinned CPU fixtures and private profile/content/video copies for
owned background-window input and completed renderer captures. No desktop
mouse/keyboard takeover, worktree cleanup or PR merge is part of this request.

Mouse source: NativeWindow currently captures every click and NativeKeyboard
maps left to X/attack. Add an independent absolute pointer/menu route preserving
short clicks until Apt consumption and suppress gameplay mouse while menus own
input. Research original Apt display-list hitTest and event publisher 827F1B28.
FOV source: 827258B8 rebuilds/copies camera planes through 823F1790 and 826AE9D0
before late render begin 823F1870. Publish user camera window before visibility
planes are built, retaining original culling and exact Original projection.
XenonRecomp hook reference checked:
https://github.com/hedge-dev/XenonRecomp/blob/main/README.md

Completed October 5: native and release game, recorder and launcher builds are
updated. Menus use an independent absolute pointer, original transformed Apt
bounds and the original slot-zero event publisher. Hover/click, right-click
Back, repeated wheel navigation, setting adjustment and existing footer buttons
retain original screen ownership and controller/keyboard navigation. The Xbox
point hitTest stub now returns an original Apt Boolean for the supported bounds
overload. Blocked screens discard pending clicks and retain the cursor without
calling partially initialized AVM methods. Retail image/assets remain read-only;
13 menu resources are extended in the separate derived frontend packages.

Original Apt unload rewrites Push/ConstantPool indices into traversal ordinals
(827D5BD4). Appending constants while inserting earlier actions therefore broke
the second menu load. Generated packages now order their constants by the real
character/frame/control traversal and preserve every referenced typed value.
The compiled regression reproduces the former corruption and verifies repeated
loads. Popup instance names are reversed in retail: select_yes imports NoButton
with FE_Back_button, and select_no imports YesButton with FE_Select_button;
their mouse events follow those actual imports. Video hit overlap follows the
original visible button display depths. Stationary wheel queries preserve the
ordinary selection while still selecting the hovered editable setting.

FOV is now published before 823F1790 rebuilds the camera frustum and 826AE9D0
copies scene visibility planes. The original classifier continues near/far and
outside-view rejection; private cameras and authored zoom retain their existing
qualification and restoration. Original CPU fixtures verify admission beyond
all four stock edges at widened FOV before render begin, plus dirty-list and ABI
preservation.

Validation: the full native suite passed 532/532 before the final lifecycle/wheel
refinements; final focused checks pass 11/11 in both native and release. These
include 962 original Apt CPU/ABI checks, 11 compiled mouse-action tests and three
package-preservation tests. Actual original constant unload/reload is exercised,
including named parameters, pool scope and a shared running ordinal. AOT verifies
311 generated files with zero semantic diagnostics.

Private live run live-20261005-051254Z-863686 verifies Pause/Options/Video mouse
selection, quit popup right-click and clickable No, repeated stationary wheel
navigation, bidirectional setting wheel edits, clickable Cancel restoring exact
preferences, resized 1600x900 client mapping, FOV110, clickable Accept saving,
Resume and a second Pause/Back after menu resource reload. Completed menu and
wider-FOV Chocolate Land scene captures were viewed. The owned disabled HWND
received targeted messages only and closed normally by WM_CLOSE. All 255 primary
profile/content/video hashes remain unchanged. Sources, binary/package hashes,
tests and the live receipt are recorded in
build/mouse-fov-fix-20261004/final-verification.json. Recovery copies and failed
diagnostic logs are retained. Full mission traversal is not part of this test.
No PR merge or worktree cleanup was performed.
