# Restrictive-check audit

The audit compares original producers and packaged inputs with native admission,
keeping setup, drawing, retirement and gameplay evidence separate. Original
assets are read-only. Stage runs use verified private profile/content/video
copies; primary-store before/after hashes matched in all 18 initial runs.

## Current evidence

The [extended wander routes](audit-wander-routes.md) (255 seeded commands per stage) found what the
20-command opening routes could not: the first sweep completed 12 of 18 stages and exposed four restrictive
or incomplete checks, now repaired test-first and mutation-checked (a retained expanded-precision request
at the edge draw, a moved camera eye at the rigid alpha pass, an all-NaN immediate vertex, and the binding
reset's truncated screen-shader table). The final executable completes both seeds on all 18 stages (36 of 36
runs, 255 of 255 commands). Seed 2 reaches 29 catalog passes (27 before), 704 files (368) and 33,384 row
receipts; 43 death-and-reload chains and one completed mission-exit transition (original `EpisodeComplete`,
outro movie, results, `spr_hub` ready) are re-derived from raw logs. The outro movie's accepted-Start skip is recorded. Later checkpoints,
pickups, destruction, dialogue and ability-hit receipts are not established. The **full serial aggregates pass
527/527 in Native (412.90 s) and Release (397.73 s)**; one earlier Native aggregate failed
`NativeVideoSettings` once (it passes alone 4 of 4) and is preserved.

The [live-route wave](audit-hot-path-and-file-provenance.md) takes the work onto
real stage routes with the final executable: **all 18 opening routes complete with
20 of 20 commands delivered**, including the three stages rejected in the first sweep.
Taking the observers live exposed two defects that fixtures could not: a linear scan of
every tracked allocation per draw (a hub stage missed its 180 s deadline; now 51.8 s,
matching the 497-test executable), and a misrouted recorded mono pass after a completed
rigid replay (`eighty_bites`, deterministic in the old executable too; fixed).
New observer-only receipts record each guest file open/read (actual path, extent,
native identity, offsets) and the live join reports **27 distinct shader passes
encountered, all admitted in the original catalog**, 10,696 stock row receipts (largest
table 19 submeshes) and 368 distinct files.

Stage 7 adds the observer-only [relation between audio reader claims and the guest file
reads that placed their bytes](audit-hot-path-and-file-provenance.md#audio-claim-to-file-read-relation-stage-7-observer-only).
An independent check against the serialized catalog confirms, for **2,132 receipts** from the
final executable (186 opened files, 1,286 distinct file/block pairs), every byte of the 1,788
covered or spanning claims at the catalog's own file offsets; in the other 344 (`ambiguous`)
claims a confirmed suffix remains while a head (13 bytes to 99.7% of the claim) was not last
written by a recorded read at that offset, and why is inferred, not observed. Five mutations
of the final source each fail the filesystem test. The final executable completes a second
full 18-stage sweep 18/18 with 20/20 commands each; the first sweep had one unexplained
slow-start failure (`meetthyplayer`, preserved; three reruns pass). The **full serial
aggregates pass 526/526 in Native (427.06 s) and Release (404.46 s)**. Deaths beyond
the earlier frozen Tree route, pickups, destruction, dialogue, later checkpoints and mission
exits remain open.

The [per-row prevalidation receipts](rigid-row-prevalidation-audit.md) copy each
rigid row's nine raw words into the audit before any offset/header/collection
read, so malformed compiled-index and selector rows fail with row attribution
(span failures keep entry attribution). All 24 affected tests fail against the
preserved stage-2 source and pass after the change; the **full serial aggregate
passes 525/525 in Native (428.27 s) and Release (421.99 s)**. This supersedes the
open "per-row nine-word diagnostics" item below for the rigid entry only.

The preceding [capture repair](rigid-capture-contained-audit.md) passes the **full serial
aggregate of 525/525 registered tests in Native and Release**, in 419.23 and
467.49 seconds. Stage 1 removes the 65,535-row capture whitelist that aborted a
valid 65,536-row first draw (genuine 352-check baselines, two fresh processes).
Stage 2 contains capture diagnostics: a valid 1,864,136-row original table above
the 64 MiB diagnostic budget now renders and reports `skipped`; blocked output
names are reported `failed` instead of aborting draws. The six new cases fail
against stage-1 code (345/354 checks) and pass after the repair; the host
`ResourceAudit` regression proves diagnostics never become the latest primary
encounter. Rejected-upload capture has no deterministic original producer and is
source-reviewed only. No gameplay encounter or complete GPU retirement is
claimed.

The preceding [zero-row rigid repair](rigid-zero-submesh-audit.md) and its
unused-pointer and collection-value logging extensions pass **26/26 selected
tests in Native and Release**, in 24.81 and 43.50 seconds (the earlier 18/18
checkpoint remains historical). Ten independent zero processes plus the six
65,536-row families retain 34 malformed controls per configuration and twelve
rendered catalog pass joins; zero loops add no rendered shader combination.

The preceding [rigid submesh range wave](rigid-submesh-range-audit.md) passed **16/16
selected tests in Native and Release**, in 13.25 and 12.87 seconds. The actual
baseline reproduces a valid original 65,536-row rejection. Checked byte spans
and observed logical allocation bounds replace the 65,535-row ceiling. Six
fresh family processes complete original setup, opaque/alpha pixel comparisons,
four independent malformed controls, later valid use and scoped CPU release.
Production entry observations preserve failure groups before validation; each
configuration joins twelve actual shader selections to original catalog rows.
Separate scopes bind 1,221 project inputs, five executable identities, three
DLLs and 643 dependency records. Both initial repairs build in 13 steps; AOT
verifies 311/0. There are 507 registered CTests, with no full 507 aggregate or
new gameplay route claimed. Recorded large rows, capture-enabled and recorded zero-row
inputs, per-row rejection attribution and complete GPU retirement remain open.

The preceding [catalog identity wave](audio-catalog-identity-audit.md) passed **8/8 selected
tests in Native and Release**, in 14.69 and 14.08 seconds. Every shipped stream's
copied certificate is checked, ordered aliases remain intact and owning values
survive catalog destruction/replacement/move. Both selections also rerun full
extent decoder lifetimes and original MUS cancellation/completion through
metadata/reader/service/worker retirement. Actual 17-step builds recompile the
changed layout's consumers and relink the application. Separate scopes bind
1,220 inputs, five selected executables, three DLLs and 642 dependency records.
The getter does not establish the actual opened-file/read/claim relation.
Prevalidation receipts cover all 55 independent metadata cases; policy probes
remain distinguished from structural corruption and authenticated source inputs.
That wave registered 501 CTests; its scopes precede the rigid source changes.
No full 501-test aggregate or new gameplay route was claimed. The overall audit
remains active.

The [current source census](../build/restrictive-check-audit/rigid-row-expanded-census-v1-20261003.json)
includes handwritten audio, common and app code as well as runtime/renderer:
823 mechanical candidates across 313 files, with 1,168 keyword lines. All are
mechanically untriaged. The preceding expanded824 report and runtime/renderer
769 reports remain preserved; the scanner's four host checks belong to its
earlier scope repair. No native/gameplay join follows from this lexical census.
Its stale-producer gate rejected the first refresh attempt; fresh admission
and producer reports retain 49 effects/110 passes/63 missing artifact passes.

The earlier [source extent regression](audio-catalog-source-extent-audit.md) passes
**5/5 selected tests in Native and Release**, in 9.50 and 9.28 seconds.
The unchanged catalog producer accepts an actual 536,891,968-byte isolated
SNU containing authenticated original dialogue. The former loader rejects it
solely at the per-source 512 MiB metadata ceiling; that ceiling is now removed.
All eight independent cases pass, including complete original/relocated/reopened
14-block, 67,328-frame decoder lifetimes with identical PCM and five exact
structural rejections. Prevalidation tuples and actual source hashes are
preserved. Strict receipts are `a7e4cf42c15fd90cd1cc0c7523f7f8245b26b77f554ace415e5d23d940d90d69`
and `17bebff2a8843ad8ed9afd22f8436efd1369513f3a1019ed4bfcf3549f561931`.
Their separate scopes rehash 1,220 compiled project inputs, four selected
executables, three DLLs and 642 object dependency records. Application links
and AOT 311/0 verification pass. The project registers 501 CTests; no full
501-test aggregate or new gameplay run is claimed.

The [reader entry source audit](../build/restrictive-audit/reader-entry-range-source-20261003/README.md)
corrects the former claim of an original 253-entry constructor cap. The
original count transport is full-word; later token masking aliases higher
slots and can prevent claim/reset progress. Admission remains conservative
while the full original scheduling/lifetime domain is unresolved. Coherent
raw254/255 low-slot cases are [designed but unapplied](../build/restrictive-audit/reader-entry-range-cases-candidate-20261003/DESIGN.md).
The metadata identity accessor is now implemented and tested. The
[actual source ownership relation](../build/restrictive-audit/audio-source-identity-implementation-source-20261003/README-v1.md)
and broad mission action/resource lifetimes remain open. The
[rigid 65,536-row source design](../build/restrictive-audit/remaining-geometry-rejection-source-20261003/README.md)
now has the separately bound original setup/draw/scoped-release regression
above; capture, zero-row, recording and broader ownership domains remain open.

The earlier cached-reader-close profile wave is closed with independently bound
**18/18 Native and 18/18 Release** selections, in 11.51 and 10.34 seconds.
The [Native receipt](../build/restrictive-audit/close-profile-wave-20261002/repair-native-v1-receipt.json)
has SHA256 `f85dfc5dbf04f2f5f5d87dcd71d7a2318ec4d79a01a2c13d92338a3699edc312`;
the [Release receipt](../build/restrictive-audit/close-profile-wave-20261002/repair-native-release-v1-receipt.json)
has SHA256 `b3cf2072c2e3dd88972a7d7f55daa11e3e915324539b2076c8fa6e79cc1e3cf0`.
Each rehashes 1,219 inputs and three DLLs, with separate scopes
`b9c246a621acca07b0fb819ee987a90394fec23db88d69a2e5339c1d5c926ff7`
and `ebd9ea2fa092c91f054157f069e7b731d72753b26004694d593780e7b602f291`,
containing 219 Native and 211 Release executable identities. Those inventories
do not grant execution credit to unselected binaries. The full build logs
complete 134 Native and 136 Release steps; AOT verifies 311 files with zero
semantic diagnostics. No full 500-test aggregate was run for that profile wave.

The [test-first baseline](../build/restrictive-audit/close-profile-wave-20261002/baseline-native-v1-receipt.json),
SHA256 `4e29dc9d374c7f6885c99689fe4fef0a684bbee27edce07157a441ca8162337e`,
binds three expected deferred diagnostic failures after complete original
cleanup. Its old `cddc0405...ac829` logger merges the different numeric profile.
Current reader SHA256 is
`a0b7bfd499af2fa231252a7e5db2a3451377110e39479f28ca3b18fb91a7918f`;
the current lifecycle/producer fixtures are `b7e90a7f...806269` and
`98ea5f63...67acda`. Baseline lifecycle/aggregate/Case 2 raw counts are
42/102/47; repaired counts are 44/114/49 in both configurations, with
1,672/1,752/616 checks respectively. The strict binder and independent 28
host checks retain separate source and native authorities.

Whole original factory `82330540` creates three concurrent owners. A/B use
310,400-byte rings and seven effective entries from four requested entries;
C uses 120,000 bytes and ten effective entries from seven requested entries.
Three real request/claim/releases, fifteen reader-role allocations/frees,
original owner closes, service shutdown and root/OS join complete before final
diagnostic assertions. Same-profile A/B keys match; C separates. Finite
construction origin is semantic; naturally distinct reader serials and actual
create caller remain in `instance`. All eight lifecycle malformed/no-op
controls retain their original outcomes. The asset stays generic
`original-reader`: no catalog, filename, PCM/seek, gameplay, queue epoch/pin
or device-fence credit follows. [The audio-close audit](restrictive-audio-close-audit.md)
pins the applications, raw receipts and remaining limits.

The following 500-test receipts are historical after this wave's fixture and
production applications; their passing outcomes remain attached to their
original source/executable scopes.

## Historical command-ownership repair

The preceding queued-reader-close wave has a strictly bound Native baseline:
the original two-owner lifecycle completes both real group frees, 64 total
fixture frees and original root OS join, then fails the deferred ownership
diagnostic assertion. Its [baseline receipt](../build/restrictive-audit/audio-close-wave-20261002/baseline-bound-v5-native-receipt.json)
has SHA256 `baa93066b421e53d1cd91a8adc1325cc92db9c05ac80fde8f16592f68d66dabb`
and binds 43 raw rows, 1,219 inputs, one executed target and three DLLs to
the unpatched source. Its Native focused repair passed 1,665 checks in
1.02 seconds, bound by [receipt](../build/restrictive-audit/audio-close-wave-20261002/focused-v2-native-receipt.json)
SHA256 `118cae2742bab0e30fc0340dc241a54019ff325875380763f424aaccd8892e6e`:
42 raw rows, original eight-group/fourteen-manager/64-free/root-join lifetime,
stable duplicate/inherited-r4 grouping, six malformed controls and two no-ops.
Its reader SHA256 is `cddc04059499106c8a7d4f97cc55d4ebcbec9f3c79ac47b9b4f6a43d5a3ac829`,
against a fresh scope of 1,219 inputs, 219 executable identities and three DLLs.
The separate [Release focus receipt](../build/restrictive-audit/audio-close-wave-20261002/focused-v2-native-release-receipt.json)
SHA256 `eb0772e86aa40f5cf617a7f36cd0666f755a6c32158ab9217bb7be6351a5755b`
binds 1/1 pass, the same 1,665 checks and 42 raw rows in 0.61 seconds, against
its 1,219-input/211-executable/three-DLL scope. That historical Native full execution passed
500/500 in 415.97 seconds and is strictly bound by [full receipt](../build/restrictive-audit/audio-close-wave-20261002/full-v2-native-receipt.json)
SHA256 `c1273bb3b1504303a071fd13c8858e8f2eb41f8ffcd26ab27cfac861d2804a04`.
Its historical Release full execution passed 500/500 in 448.35 seconds, strictly bound by
[full receipt](../build/restrictive-audit/audio-close-wave-20261002/full-v2-native-release-receipt.json)
SHA256 `fd9075168261c493b407b068fe3b6bd4b7a6c5cc2933a49f93302b0f218e18fd`.
Both full suites have zero failures or skips. Source application,
focused execution and full-suite outcomes remain separate; this adds no
new gameplay/catalog join or wider audio-source lifetime credit.
[The audio-close audit](restrictive-audio-close-audit.md)
preserves the BOM/parser and application-helper failures, 18/18 host-only
adapter checks, exact source ownership proof and open profile/catalog limits.
No native rerun was needed for the BOM parser correction, and the failed
application's unpatched regeneration grants no repair credit.

The preceding stable-grouping snapshot has bound **500/500 Native and 500/500
Release** aggregates in 474.63 and 518.62 seconds. Its [Native full receipt](../build/restrictive-audit/stable-group-wave-20261002/full-native-v2-receipt.json)
has SHA256 `b1e6a58cc7fa6ad14944e7adeff22b2ba8dc14e1c14c7359c23517e3d179f3d7`,
with 1,219 frozen inputs, 219 executables and three DLLs rehashed against
scope `f0530c17...ff395`. Both independent focused selections pass 17/17,
Native in 6.60 seconds and Release in 6.68 seconds, with receipt hashes
`b9c0e4e6...adcf4` and `4a377f54...d97f72`. The [Release full receipt](../build/restrictive-audit/stable-group-wave-20261002/full-native-release-v2-receipt.json)
has SHA256 `7b578326753016a8d5614385f216a0c0d406a5e3f07f64fc9fd54e90671fb058`,
with 1,219 inputs, 211 executables and three DLLs rehashed against its separate
scope `377a5075...04a89`.

Two diagnostic producer fixes now move accepted movie sample packet counters
and release-only pipeline `r4/r5/r6/r27` lanes into `instance`. The preserved
baseline has all 17 expected grouping failures. Repaired failure snapshots
retain the latest raw instances while true global/caller/mission/action/caps,
owner-match, context and phase controls remain distinct. All 16 pipeline cases
complete original initialization, native upload/readback, declaration/index
release and final backing expiry. The CPU movie fixture completes original
wrapper acceptance, fixture-owned Begin/End and map release, with decoder
playback unproved. The ordinary controller aggregate does not run that movie
grouping fixture. No new gameplay or frozen-catalog credit follows from these
diagnostic changes; [the grouping audit](resource-audit-grouping-review.md)
pins exact baseline, repair and scope identities.

The first full-run artifact bind stopped at a valid empty-stdout CTest block.
Its old parser and failed-attempt note are preserved. The corrected binder
checks an actual closing sentinel and authoritative result metadata; 14/14
host parser regressions pass, including all 500 real JUnit/LastTest pairs.
The Native suite was not rerun to repair this parser error.

The preceding immediate-skin submesh repair has separate, independently bound **500/500
Native and 500/500 Release** aggregates, in 447.12 and 437.34 seconds.
The [Native receipt](../build/restrictive-audit/submesh65536-wave-20261002/full-native-v1-receipt.json)
has SHA256 `bcd9cc22af1a916eb72bd6202f4770b128f767fbc4eccb7d943308a63cb9b7fa`;
the [Release receipt](../build/restrictive-audit/submesh65536-wave-20261002/full-native-release-v1-receipt.json)
has SHA256 `9711a826a1af73848f1ea9c2dd50ad5d691dad3f498af4c7349184e6ea9db5a3`.
The binder rehashes each configuration's 1,219 frozen inputs, executable
identities and DLLs. The separate scope hashes are `6f31a6f7...caacb` and
`853270c3...f550a`, with 219 and 211 executable identities respectively.

All three synthetic original skin families now complete 65,536-row
opaque/alpha use with equivalent pixels and actual original reader,
relocation, camera/declaration/FX/cache/pool/stream-source retirement.
Four independently attributed malformed frontiers per family still reject,
then a later valid draw succeeds. Prevalidation receipts preserve the exact
caller, count, logical owner span and instance generation without changing
PPC/CSR/LastError. This repairs the immediate skin count rejection only;
other family/material caps, row epoch/pinning, physical aliases and full
backend GPU retirement remain open. No 500-snapshot gameplay or catalog
matrix join is claimed; the following 497 matrices remain unchanged.

The preceding controller/mission/matrix wave has separate Native source scope
`build/restrictive-audit/controller-mission-matrix-repair-v2-native-20261002-scope.json`
(SHA256 `6afbe8f0a5454fae84a21fb282116e208ba48596df90aaabee9f29147d267694`)
and Release scope
`controller-mission-matrix-repair-v2-native-release-20261002-scope.json`
(SHA256 `351c835074cde75ebf309ddac0f5b736c0d467ed7d464226f649c097a95ebe90`).
Both full Native and Release aggregates pass 497/497, in 396.90 and 393.80
seconds respectively. Independent whole-original skin cases now pass
with 65 signed-positive matrix ranges selecting two matrices, for base,
textured and dual materials, including opaque/alpha use and original pool
cleanup. The three earlier guard failures are preserved; the repaired 3/3
JUnit is `controller-mission-matrix-repair-groups65-native-20261002-v1-tests.xml`.
The focused Native selection passes 27/27. The separate borrowed-owner case
passes closing/free/natural-address-reuse checks through the real owner.
Controller/movie and mission-attribution CPU cases also pass. These are
constructed original-path receipts, with no packaged-mesh or gameplay credit.
The final aggregate receipts retain the configured translation units,
compiler dependencies and inputs as a new scope. The new v5 join uses that
497 authority; the preserved v4 matrix retains its older build authority. The historical
493/493 Native and 492/493 Release failure/retry scope remains preserved.

The separate [Native v5 catalog](../build/restrictive-check-audit/controller-mission-matrix497-live-support-v5-native-bound-v2-20261002.json)
and [Release v5 catalog](../build/restrictive-check-audit/controller-mission-matrix497-live-support-v5-native-release-bound-v2-20261002.json)
join fresh passing JUnit receipts: all 49 independent setup processes,
padded and extended draws, three original 65-group cases, and the two exact
menu MUS stream lifetimes. Each has 138 tested rows, 110 setup-tested rows,
49 rendering-tested rows and 51 lifecycle-tested rows. The 26 synthetic
geometry cases remain separate from the packaged mesh population. Only the
two closed Native 497 live logs are imported: 2,142 events, 301 exact runtime
and owner-qualified gameplay rows, 15 effect sources, and zero telemetry
errors or resource rejection groups. The Release tests and Native live
execution retain separate executable/source authorities. No older catalog
test or gameplay credit is transferred into this scope.

The frozen Native application
`4813bebf4a0e741a3596f8268cb746fa55ca7dcf87012c25554c4091e0cdbba0`
now has an actual LOC movie skip receipt: original caller `82321114` accepts
Start, the same movie owner reaches original decoder stop at `826B92C8`,
the decoder releases and dispatches original completion, then LOC reaches
map readiness. The bounded original filename is `movies\\en\\loc_igc01.vp6`.
This 88-second owned route proves the control/decoder endpoint; it does not
identify a packaged movie occurrence or prove all mission resources retire.

The same frozen application completes four original whole-party-death reload
cycles in Tree Hugger during a 181.79-second closed route. Each `823BBACC`
request with nonbusy flags `2001` matches original `823BBCE0` owner cleanup,
a fresh qualified map request and readiness. Owner generations advance
1Ã¢â€ â€™2Ã¢â€ â€™3Ã¢â€ â€™4Ã¢â€ â€™5, with three reused-address transitions. Ready republishes the same
opening/checkpoint GUID `A58E700D46285D71E8DB0F9790DDA768`; reload requests
briefly have a zero live GUID. This proves death-triggered opening/checkpoint
reload, without later-checkpoint world-state restoration. The raw run has
11 unknown-mission rows: eight after cleanup before a new request and three
before the initial owner. All remain unqualified for gameplay. Terminal
shutdown still reports incomplete original resource releases. Both new
routes preserve primary stores and all seven frozen components.

The historical prevalidation/capability/thread Native aggregate passes
**493/493 tests**. Its complete JUnit and stdout are retained at
`build/restrictive-audit/prevalidation-capability-thread-full-native-20261002-tests.xml`
and the matching `-LastTest.log`. Both focused Native/Release v2 selections
pass 35/35. The complete Release aggregate passes 492/493: the unchanged
`NativeVideoSettings` fullscreen-window assertion failed, then its isolated
retry passed 1/1. Its cause remains unproved and the failed aggregate/retry
receipts are preserved separately. All new draw/lifetime cases passed in
Release; selected receipts do not convert the aggregate into 493/493.

The preserved 65,536-submesh baseline is a separate failing scope. All three
skin families complete original pool allocation/relocation and one-row
opaque/alpha use, then reproduce the valid `Skin submesh extent is unqualified`
rejection. The [submesh audit](restrictive-submesh-material-audit.md#independent-original-valid-rejection)
pins those source/executable/JUnit identities. The immediate-skin byte/owner
repair now has the separate passing 500-suite receipts above. No new pass or catalog credit is added to the
frozen 497 matrices, and other submesh/material caps remain unresolved.

All 49 fresh FX setup processes also pass on that earlier snapshot. Its
catalog matrix joins 110 setup receipts, 12 padded draw receipts and 52 extended
draw receipts against source/executable/input hashes. It records 23 FX
selections with original create/use/release plus 23 synthetic regression
inputs, without attributing those fixtures to gameplay or packaged geometry.
The preserved v3 matrix additionally joins two exact menu MUS stream lifetimes through
original PCM/mixer/service use; `draw_tested` remains false for audio. It has
135 tested rows, 46 rendering-tested rows and 48 lifecycle-tested rows. Its 39
historical logs retain 1,960 runtime-encountered rows but only 305 with qualified
active map-owner/generation gameplay attribution. Legacy mission labels,
pre-ready requests and stale labels after owner cleanup grant no gameplay
credit; previous v2 labels/counts remain preserved.
The separate frozen normal-exit v4 join adds the completed frontend run:
40 logs / 81,611 events, 1,979 runtime rows and 342 owner-qualified gameplay
rows. Its executable is `11385581...06a1`, with the copied AOT manifest
`6c3b8a2f...d11db2`; the subsequent controller/attribution source wave is a
different scope. Original Exit Game retires owner generation 1, returns to
visible Main Menu and later Continue publishes generation 2. That v4 route
does not exercise death or checkpoint reload. The same reused owner address does not
transfer attribution between these generations.
The source-pinned offline inventory independently checks all 5,533 mesh and
8,770 VFX occurrences and recovers 25,990 geometry records / 29,319 submeshes,
while actual material/shader selection and asset lifetimes remain separate.
See [the current catalog matrix and scope notes](catalog-support-audit.md).

The separate [particle-capacity source audit](restrictive-particle-capacity-audit.md)
recovers9,129 `.prt` module rows and requested capacities8..256. Zero stock
requests exceed4,096. This does not prove the full original maximum or the
serialized module's create/use/release lifetime; actual pool activation can
publish a capacity smaller than the authored request.

All 18 stages reached their original `-stream <folder> <folder>.str` map-ready
boundary. Fifteen completed the 20-command opening route; three stopped at new
rejections. There are 319 delivered command receipts. This establishes opening
loading/rendering and input delivery, not complete missions or every named action.

| Stage | Rejection | Original context |
|---|---|---|
| tree_hugger | Screen shader/declaration cache changed | B action; all five original distortion draws/resolves completed before reset |
| mob_rules | Skin required replacement or alpha blend | B action; opaque textured-skin source `8200FB98` selected after alpha rendering |
| dayofthedolphins | Front window state/client extent changed | First confirmation; preceding presentations were occluded; actual failing window state was not yet logged |

Initial logs remain in `build/restrictive-audit/stage-sweep-20261002`. The grouped
report is `build/restrictive-check-audit/stage-sweep-encounter-groups.json`:
51,070 written events, full raw signatures, no malformed telemetry. Sequence
numbers count observations before deduplication. Exact window-close/shutdown
messages are distinct from the three substantive rejections.

The first corrected native snapshot passed 48 focused CTests, including 14
independent audio producer cases. All 49 FX setup rows passed in fresh processes.
Runner receipts pin each exit code, output and executable/source/image hash.
Registration and metadata use do not prove a rendered lifetime. Padded skin/sky
tests additionally execute original drawing, declaration retirement, both FX-table
cleanup calls and manager destruction. GPU upload caches remain backend-owned;
complete GPU cache retirement is explicitly unproven.

The expanded snapshot repaired the distortion cache transition and inherited
opaque skin blending. Fresh Mob Rules and Dolphins opening routes passed; a
later Tree route reached a radial-state rejection, and its next rerun passed.
The original failing window state remains unproved. Preserve each failed run;
successful reruns alone do not establish the cause or full valid range.

The lifetimes snapshot passed 204 of 206 selected native tests. The two failures
were new rigid pixel fixtures whose shader constants disabled the intended
shadow branches; their test-only correction passed both subsequent native GPU
suites. All 49 independent
FX setup processes passed on that snapshot. GPU tests separately verify that
backend destruction releases its immutable vertex/index buffers; this does not
prove original game asset retirement. Receipts preserve both scopes.

LOC intro playback completed naturally and reached gameplay in an earlier live run.
Other earlier runs captured playback and delivered Start without the newer
same-owner accepted-action/completion proof recorded above. Pause and resume were captured for
Tree and Mob Rules. Direct stage loading did not publish the original root
episode required for Quit Episode, so its absence in those pause menus is
explained by the producer. Later-checkpoint world restoration remains untested. Genuine frontend Continue
additionally reached LOC with a nonzero restored checkpoint in the private
saved slot; the source-bound ready receipt distinguishes that path from direct
stage loading. Earlier confirmation-only runs did not establish accepted exit.
The later frozen ordinary frontend route now matches delivered Yes with original
owner cleanup and visible Main Menu return; saved-game reentry publishes a
new owner generation. Its1800-second route deadline and deliberate close
remain separate from those completed semantic endpoints.

Later independent producer cases now pass original radial owner-loop cleanup,
65,536-vertex owners, aligned partial vertex tails, index owners above 16 MiB,
an unused odd index tail, and native index capacity above the former 64 MiB
cap. Original selected index counts 0, 1 and 2 retain nonempty owners and submit
no triangles; all eight original Boolean passes leave color/depth unchanged
and complete their original declaration, payload and FX cleanup. These cases
do not qualify missing buffers, wrapped source addresses or large draw-count
chunk equivalence.

Whole original mono alpha/opaque callers now pass material matrix inheritance,
rendered pixels, malformed/stale input rejection and original declaration/FX
retirement. Deferred native mono GPU lifetimes also pass WARP and hardware.
Both whole original mono recording variants now pass 716 checks in native and
Release. They execute the real public dispatcher, recording, retained replay,
matrix inheritance, payload/plugin retirement, declaration/FX cleanup and
recording-context destruction. Malformed ownership and alias changes still
reject. The original finish counter and last-record history survive payload
retirement, and the original idle setter can publish the live application
context without resetting guest aliases. The completed Native evidence join
now includes both whole recording variants; separate Release draw exports
also pass their exact source/executable/marker checks.

Two independent configured MUS cases pass actual original metadata loading,
PCM mixing, cancellation/completion, codec lease retirement, metadata free,
deferred reader destruction, service shutdown and worker join. The authored
`menu_mus.msx` descriptor requests 1,872 metadata bytes; earlier attempts to
load the entire MUS bank were invalid fixture inputs. No audio guard was
relaxed for those fixture defects. Nonzero authored seek remains unproved.

The accepted LOC Exit Game popup now reached genuine original owner cleanup:
the cached owner retired at `823BBCE0` and the global owner cleared. That frozen
direct-stream run then failed at a null successor call. Source tracing explains
the route limitation: original developer entry selects `DebugFEMainLoop`, which
the retail startup does not register; ordinary frontend entry retains the
registered `FrontendMainLoop`. This is not evidence for relaxing null-target
validation. The failure, occluded completed readbacks and old executable scope
remain preserved. The later frozen ordinary frontend run now proves accepted
exit and visible Main Menu return without that developer-loop limitation.
Independent loop cases now pass in both native and Release through the original
frontend constructor, hash producer, registry, return selection, removal and
borrowed destructor. Missing and malformed keys retain their original lookup
failure, and null indirect targets still reject. These CPU cases do not claim
the world-dependent enter/tick/cleanup; the separate live endpoint receipt
establishes the normal exit route above.

The preceding submesh repair completed both frozen 500-test aggregates.
The preceding grouping snapshot has separately bound full Native and Release
500-suite passes, plus both focused 17-case selections.
Live routes and additional source
domains remain separate work; an earlier pass does not certify a later binary.

## Producer contracts

- Audio ring allocation follows exact original single-precision arithmetic and
  sixteen-byte alignment. Zero and aligned extents through `0x7FFFFFF0` are
  producer-valid subject to original arithmetic/count/mapped allocation guards.
  Only zero bitrate selects the default. Tests cover claims at2,064bytes,
  deferred cancellation at2,048bytes, sub-byte/denormal zero rings and65
  concurrent original groups, with reset, worker retirement and destruction.
- The declaration SDK stores DWORD stride in one byte. Skin/sky semantic/index
  associations permit aligned padding through1,020bytes when typed attributes
  fit and complete records do not overlap. Captured48/56-byte skin strides and
  one28-byte sky layout were not the complete producer contract.
- Submesh base vertex reaches both immediate and recorded SDK submissions.
  Restart is tested before the offset; only selected effective indices must fit
  owned vertices. Whole original cases now exercise65,536-vertex owners,
  partial vertex tails, large/odd index owners, short counts and paired CPU
  retirement. Consumed fetch/owner bounds retain validation; unqualified
  declaration/family selection and complete asset GPU retirement remain gaps.
- Original alpha skin cleanup resets expanded blend while retaining enabled
  factors. An opaque pass can inherit enabled `0x07060706`, expanded zero.
  Exact native equation admission must be independent of shader alpha selection.
- Original distortion `82772468` leaves its final composite in the guest cache;
  native scoped draws restore preceding physical bindings. A complete original
  five-draw/five-resolve receipt and unchanged logical FX owner authorize that
  transition. Incomplete phases and corrupted owner/cache/receipts still fail.
- Original triangle-strip SDK submission splits counts above 65,535 into
  65,534-index packets with two-index overlap. Each packet starts a fresh strip,
  so restart placement can change winding at a boundary. All six mesh backends
  now follow that recurrence after validating the complete selected range.
  The original sky fixture passes in Native and Release and distinguishes
  requested cull2/6 from the actual material cull0/2 for both logical shader
  pairs. The public original dispatcher clears packet alpha, and the original
  draw restores the requested cull state. All eight Boolean selections,
  independent winding/SDK packet pixels and paired CPU retirement pass.
- Original pipeline initialization selects r6=0 or2 from capability bit16.
  Both branches request the same R16 index allocation; the constructor and
  actual allocator discard that incoming argument, and cleanup has no
  capability branch. Native handling now requires the source-derived
  correlation. All16 independent WARP/hardware cap-pattern lifetimes pass
  in each build, including upload/readback, original cleanup, retained lease
  release and stale use.64 exact failure-context snapshots per build preserve
  each malformed producer tuple without claiming an indexed draw.

Primary-source pins and regressions: [audio](audio-restriction-audit.md),
[geometry](restrictive-geometry-audit.md), [catalog](catalog-support-audit.md),
[decal pool](restrictive-decal-pool-audit.md) and
[copied ITXD ranges](restrictive-copied-itxd-range-audit.md).
The separate [submesh/material table audit](restrictive-submesh-material-audit.md)
now recovers all compiled collection and serialized RenderWare list counts.
Its shipped maxima stay below the current caps, while original DWORD
loops and owned table extents support the now-executed immediate skin
65,536-row regression. Compiled-material and RenderWare selector candidates
still require independent original create/use/release regressions.
The [pipeline capability audit](restrictive-pipeline-capability-audit.md)
contains the independent allocator and cleanup proof.
The current [source-extent census](../build/restrictive-check-audit/source-extent-census-v1-20261003.json)
has SHA256 `3216d89667930615eabdc58585009dd3040031c39ea68119321e318cf5994f43`:
769 mechanical guard candidates/1,112 keyword lines across 274 source files.
A match is not proof of a bug; each candidate still requires a pinned
producer-range or original-path disposition. The current source-only
[effects](../build/restrictive-check-audit/source-extent-effects-v1-20261003.json)
and [shader producer](../build/restrictive-check-audit/source-extent-shader-producers-v1-20261003.json)
reports have hashes `ed9a6f2917ca8236287d764aa0d7c880abcd22bb53b82265eb49014ad62a0e99`
and `b402516754a35b497d7fed420f7de9b9bc7c245b421430dfb0e838c60ca1729c`.
They retain 49 effects/110 registered passes, 47 artifact-admitted passes,
45 declared selections/63 missing-artifact passes, the same asset catalog,
two shader source findings and no texture scope. They import no runtime logs,
native receipts or new catalog/lifecycle credit. All older `close-profile-source-*`, `audio-close-source-*`
and `stable-group-source-*` reports remain preserved.

The separate [AA source review](../build/restrictive-audit/aa-fx-source-review-20261002/completion-receipt-v1.json)
is complete as source-only evidence, with receipt SHA256
`de002aef798702b47bfed7e025a28cf2eccab0fba87ed8b90b0252dfc256d2cb`.
Alternate original setup reachability remains unqualified; no valid native
rejection, repair or draw/lifetime execution is claimed.

The separate [stable-grouping review](resource-audit-grouping-review.md)
preserves the original source-only inventory of 17 observe, six lifecycle
and two action callsites. Two now-applied producer changes move movie sample packet counters and release-only
pipeline register lanes into `instance`, retaining real flags/counts,
shader/material identity, caller, mission and last action. Their independent
baseline and repair receipts are separate from that historical inventory.
The audio close ownership snapshot gap at `8233D980` has the preserved two-group
baseline: inherited `r4` incorrectly selects the other still-live owner and
splits one close signature. Its repair derives the candidate from the bounded
original command's `+4` slot. The later closed profile wave retains that exact
qualification and adds cached semantic profile fields; its current 18/18
Native and Release selections are bound above. The earlier full 500/500 suites
remain historical. The [profile proposal authority](../build/restrictive-audit/close-profile-candidate-20261002/source-candidate-receipt.json)
now identifies applied and tested reader `a0b7bfd4...7918f`. Exact catalog and
filename/SourceRelation publication remain unimplemented; the separate
[catalog metadata candidate](../build/restrictive-audit/audio-catalog-identity-candidate-20261002/source-candidate-receipt.json)
remains unapplied. Queue/row epochs and pins, physical aliases, concurrent owner
lifetime, audio-device fences and whole-original GPU retirement remain open.

## Provenance and routes

The complete catalog matrix covers490archives in20packaging groups,49registered
effects/110passes, eight direct particle SDK selections,7,318textures,
5,533meshes,8,770VFX resources and three sound catalogs. Path groups are
packaging, not proved runtime mission dependencies. Opaque references and
missing shaders remain visible gaps.

`--resource-audit <jsonl>` logs each new combination at its instrumented
boundaries before local validation. Receipts
preserve identity, original caller, parameters, mission, last nonneutral input,
ownership, thread and scene. Group keys exclude transient addresses/generations;
`instance` retains them and `scene_context` retains the latest same-thread scene
owner. Deduplicated observations refresh the latest failure snapshot. Logging
never authorizes an input; unknown provenance remains explicit. Shutdown events
retain context without rejected-asset credit.
The early effect snapshot now precedes owner/camera/requested-technique
checks in seven effect families. Its independent mono regression passes four
early malformed cases and a later original draw/retirement. Uninstrumented
registrations, decoders and backend guards can still retain older context;
the catalog report lists those logging gaps explicitly.

ITXD bindings use cached original name/descriptor/extent/phase. Audio uses cached
EAAC/catalog provenance and established payload hashes. Shared names, headers or
pointers cannot prove one packaged occurrence. Reused shader pairs require exact
original technique/pass handles.

```powershell
python -B tools/audit_stage_routes.py --timeout 180
python -B tools/audit_stage_routes.py --stages tree_hugger mob_rules --timeout 180
python -B tools/audit_stage_routes.py --stages loc --play-intro --intro-skip-after 10 --timeout 300
python -B tools/run_independent_effect_setup.py --fixture build/native/IndependentEffectSetupTests.exe --output build/restrictive-check-audit/effect-setup-new
```

`--route` accepts a JSON list of named commands or bounded PAD holds. Optional
`wait_after_ms` and `capture_after` create observation points. New runs snapshot
runner source, route and launch hashes. Captures must be completed native front
readbacks; diagnostic JSON is not a frame. Input uses the owned command channel
and shutdown targets only the owned process/window.

`--baseline-video` verifies the original video copy and then applies 720p,
windowed, 60 FPS preferences only to that run's private store. The exact override
bytes and parameters are recorded. Original profile/content/video hashes are
still checked after the run. Without that flag the copied preferences are used.

The default opening route sends confirmations, jump, attack, B, interaction,
right trigger, movement/attack and shoulder input. The separate frozen routes
above prove one LOC skip, normal frontend exit and four Tree death reloads.
Later checkpoints, other cutscenes and mission exits need separate capture-guided
routes and original lifetime receipts. They cannot be inferred from closing a
test window, an input replay checkpoint or successful asset loading. These
routes remain part of the active audit.
