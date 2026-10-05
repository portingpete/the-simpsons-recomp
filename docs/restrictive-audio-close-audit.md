# Queued audio-reader close attribution audit

The profile-wave results below retain their previous compiled scope. The later
[catalog source-extent repair](audio-catalog-source-extent-audit.md) has separate
5/5 Native and Release selections and changes only the catalog metadata guard.
Reader and close fixtures remain unchanged; their earlier execution receipts
do not transfer to that new compilation.

The cached close-profile wave is closed: independently bound Native and
Release selections each pass 18/18, in 11.51 and 10.34 seconds. Current reader
SHA256 is `a0b7bfd499af2fa231252a7e5db2a3451377110e39479f28ca3b18fb91a7918f`.
Whole original factory lifetimes now prove identical numeric profiles share
one stable close key while a different profile separates. Command ownership,
malformed/no-op outcomes and complete cleanup remain preserved.

The preceding command-ownership wave's focused passes and full 500/500 suites
are historical after the new lifecycle/producer test application and production
change. There is no new full 500-test aggregate on the current head. Source-only
review, application, selected native execution and catalog/gameplay evidence
retain separate authorities.

## Original ownership contract

Original enqueue `8233D950` writes the real eight-byte command
`{8233D980, G}` into the owned service queue. The original executor calls close
with the command pointer in `r3`, its command frame in `r30/r31/r29` and return
caller `823395BC`. At `8233D980`, the actual group argument is the command's
second DWORD, `command+4`. Inherited `r4` is not that caller's group argument.
The old logger instead resolved `groups.find(r4)`, allowing an unrelated
simultaneously live group to be reported as the close owner.

The [original source proof](../build/restrictive-audit/audio-reader-close-attribution-20261002/pinned-report.json),
SHA256 `83fdca8843057deab7205e3933c1016eff555165c6b043ea9f7ff63a88e7d69d`,
pins 18 full original function spans and 37 instruction words against image
SHA256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
This is source authority, separate from compiled inputs and runtime receipts.

The later allocator observation at `8233D5E8` precedes the original `bctrl`.
Its actual incoming LR is `8233D5B0`, established by the original linked call
at `8233D5AC` to helper `823480C0`. The helper saves/restores that LR, and the
intervening instructions do not replace it. The allocator callback receives
return LR `8233D5EC` only after the observation. The separate
[caller-distinction proof](../build/restrictive-audit/audio-close-wave-20261002/caller-distinction-source-v1.json),
SHA256 `41db3de80294ccc91580a2df68728fa268b82a3bbda86a0e1a9821b15a2d53cf`,
pins two complete spans and 21 words. The raw pre-call receipt and genuine
allocator callback ABI remain distinct evidence.

## Closed cached-profile baseline and repair

The [new Native baseline receipt](../build/restrictive-audit/close-profile-wave-20261002/baseline-native-v1-receipt.json)
has SHA256 `4e29dc9d374c7f6885c99689fe4fef0a684bbee27edce07157a441ca8162337e`.
It binds three expected deferred diagnostic failures to the unchanged
`cddc0405...ac829` reader, after genuine owner frees and original service/root
OS join. Raw lifecycle/aggregate/Case 2 counts are 42/102/47. The old logger
correctly derives command ownership but omits the profile suffix, so different
numeric profiles share a key and C's genuine encounter deduplicates.

The [test application](../build/restrictive-audit/close-profile-wave-20261002/test-application-v1.json)
has SHA256 `b2325a6694dda2087a14ff985710e7bf26a773b042ada58f492c6d2c5b68d23c`;
current lifecycle source is
`b7e90a7f340a0b0a7e40fe6fa05603f66288573c64053f91c70210b6ad806269`
and producer source is
`98ea5f638873a80f08738b25108a4488e5f7b58888638c022c5f4a08bb67acda`.
The [production application](../build/restrictive-audit/close-profile-wave-20261002/production-application-v1.json)
has SHA256 `be273a32870dc9f65bb6e6354a3311e6565737d1177f7544a53ea439e578b309`
and retains the exact old reader recovery, four-change proposal and raw
baseline authority. It applies `a0b7bfd4...7918f` without new functional guards.

The [independent binder review](../build/restrictive-audit/close-profile-wave-20261002/independent-binder-source-review-geometry-v1.json),
SHA256 `347df28ddf1daef0a618593f898c6bcb180c9d4a6c418b0c9f188f2621a9cf9c`,
includes 28 passing host-only controls, kept separate from native execution.
Root binder [v4](../build/restrictive-audit/close-profile-wave-20261002/bind-profile-wave-v4.py)
has SHA256 `334ad223076572a8cef0e4c1c9b318242c5ae76658bd6216b3f283c9f56ade2f`.
Both lifecycle and producer helpers bind the real baseline and repair data;
raw artifacts remain unchanged. Earlier source-draft and helper frontiers
remain preserved in their candidate/wave folders.

| Configuration | Selected result | Strict receipt SHA256 | Rehashed scope SHA256 |
|---|---|---|---|
| Native | 18/18, 11.51 s | [f85dfc5d…dc312](../build/restrictive-audit/close-profile-wave-20261002/repair-native-v1-receipt.json) | `b9c246a621acca07b0fb819ee987a90394fec23db88d69a2e5339c1d5c926ff7` |
| Release | 18/18, 10.34 s | [b3cf2072…e3cf0](../build/restrictive-audit/close-profile-wave-20261002/repair-native-release-v1-receipt.json) | `ebd9ea2fa092c91f054157f069e7b731d72753b26004694d593780e7b602f291` |

The full receipt hashes are
`f85dfc5dbf04f2f5f5d87dcd71d7a2318ec4d79a01a2c13d92338a3699edc312`
and `b3cf2072c2e3dd88972a7d7f55daa11e3e915324539b2076c8fa6e79cc1e3cf0`.
Each scope rehashes 1,219 inputs and three DLLs, with 219 Native and 211
Release executable identities; only selected outcomes receive execution
credit. Build logs complete 134 Native and 136 Release steps. AOT verification
reports 311 files and zero semantic diagnostics.

Both configurations bind 44 lifecycle, 114 aggregate-producer and 49 Case 2
raw rows, with 1,672/1,752/616 checks respectively. The lifecycle still completes
all eight groups/fourteen managers/64 allocations and frees plus root join.
All six malformed failures and two unknown/stale no-ops retain exact outcomes,
context/CSR/LastError preservation, cached-candidate versus qualified-owner
semantics and current failure snapshots.

The extra producer triple uses genuine whole factory `82330540`, including
three real source request/claim/releases, actual voice destruction, queued
`8233D980` closes, fifteen reader-role allocations/frees and whole service
shutdown/root/OS join. Counts exclude outer voice/service/startup allocations.
A/B have one member, 310,400-byte rings, seven effective entries and four
requested entries; C has one member, 120,000-byte rings, ten effective entries
and seven requested entries. The original source and all fourteen existing
independent producer cases remain unchanged; only diagnostics are enriched.

All three owners compare under the same mission/action/caller/Closing phase.
Six fresh genuine duplicate-rejection failure rows retain distinct raw lanes;
A/B share one key and C has a different key. Genuine Live and Closing encounter
counts each move from one in the baseline to two in repair. Natural consecutive
opaque identifiers and create caller `8233062C` appear only in `instance`.
The finite original construction role is semantic, with permitted fixture
provenance taking priority over matching stock caller/profile bytes. Candidate
fields come from the registered native cache, including unqualified command
snapshots; they do not upgrade the original queue-owner qualification or read
stale guest Group payloads. Final diagnostic assertions occur after all three
real group frees and original service/root OS join.

## Historical command-ownership baseline

The [Native baseline receipt](../build/restrictive-audit/audio-close-wave-20261002/baseline-bound-v5-native-receipt.json)
has SHA256 `baa93066b421e53d1cd91a8adc1325cc92db9c05ac80fde8f16592f68d66dabb`.
It binds `OriginalAudioReaderLifecycle` to the actual
`AudioReaderLifecycleTests.exe` command plus the immutable image, matching
JUnit/LastTest outcomes, preserved run/build logs and unique raw JSONL.
The unpatched reader SHA256 is
`6b1f9bbd88c158e9996601f8db3e24988223876bbc798f97674a509f4ea2fc6b`;
the exact applied v2 fixture SHA256 is
`645a48c94846373007fa109dfa43f018c469e16cfef740eecf4ae00ee2c6a839`.

The frozen baseline scope SHA256 is
`38c7f4520574e161ef0927b0989a50e0fbfba9267d875621852993cc73564b04`:
1,219 inputs, one executed target and three DLLs were rehashed before binding.
The compiler authority separately records 624 configured translation units,
207 implicit dependencies, 641 object dependency blocks and 497 observed
non-system dependencies. The actual selected target-link log establishes
that fixture's compilation; the other configured targets are not claimed
rebuilt by this selection.

The six retained sequential cycles and two simultaneous empty intro groups
complete eight group and fourteen manager lifetimes, with 64 actual forwarded
fixture-owned allocations and frees. Pair cycles 7 and 8 each complete five
original allocations/frees on the same worker while the root remains live,
without stale guest reads. Original root teardown and OS worker join complete
before the deferred final failure:

```text
Queued-close prevalidation ownership diagnostics differ after complete original root join
```

The baseline is an expected diagnostic failure, not a repaired test pass.
For the duplicate-first close, `r4=0` yields no cached group; changing only
inherited `r4` to the other still-live group changes the stable close group
and incorrectly reports that second Live owner. Its later original free
confirms the distinct cached generation. The first and second actual free
observations have native generations 31 and 33. These are native identities,
not fixture allocation indices or address-based epoch guesses.

The retained raw file is
`build/native/Simpsons-reader-close-84408-612101359-0.jsonl`, with 43 rows,
28,368 bytes and SHA256
`868d9ac29a6fb68f3f82a679cf349ac99634307a766595aa7ff1f4b6904b71ab`.
Groups reconstruct from identity, caller, parameters, ownership, mission and
last action. Four unattributed shutdown rows remain separate, with no close
owner or rejected-asset credit. Missing baseline full64, command-candidate
and executor-envelope fields stay explicit logging gaps.

## Preserved host-tool failures

The first binder invocation stopped before writing a receipt because
PowerShell5 `*>` produced UTF-16LE BOM build/run logs, while default-locale
reading exposed embedded NULs and missed the real target-link line. The
first binder SHA256 `6c146b8a54c59ab99c35b6bcb9f240d3b9d826202f322cfe9de49596a19016f7`
and [rejection note](../build/restrictive-audit/audio-close-wave-20261002/first-execution-log-decoding-rejection-v1.json)
are preserved; the note SHA256 is
`0f1a175bad9a20733f74be520d8145beff90c3b5ee4e8237afee9dba3329a2aa`.

The corrected binder SHA256 is
`67d68ffe5752d7a47970ae692e732138bdbafe44af44e2d23d207b2a0a66057a`.
It strictly decodes UTF-8, UTF-8 BOM and UTF-16LE/BE BOM host logs, rejects
UTF-32, malformed code units and decoded NULs, and normalizes line endings
in memory. Complete final lines remain valid without a trailing newline.
Raw JSONL remains explicit UTF-8 and XML retains its native encoding parser.
No evidence bytes were transcoded or overwritten.

The [host adapter checks](../build/restrictive-audit/audio-close-wave-20261002/host-log-adapter-checks-v2.json)
pass 18/18, with SHA256
`9cde4927e86af0402bc92808d6f32fb506275b057274914d59f34b9fa997c9ed`.
They use the actual preserved build/run/LastTest files and constructed
in-memory encoding, malformed, truncated and no-final-newline controls.
They do not invoke a native fixture or rerun a suite. Independent source
review SHA256 `a14f60f6eff82e4678e5326ee522dbfae0329325f831230090cd52f2dfef0935`
records no blocking issue. The successful baseline receipt binds the
existing execution after this host-parser correction.

The first production application helper then rejected the proposal's
`diff --git` header before writing the target. Its
[application frontier](../build/restrictive-audit/audio-close-wave-20261002/application-helper-frontier-v1.json)
has SHA256 `1e1ce88f6cc1a42501ed7e69f666ac53e2d9917d6c37413e1a092177bbf8a3e4`.
The following `repair-v2-regenerate.log` regenerated the unchanged reader:
303 chunks, 238 explicit imports and 311 verified files with zero diagnostics.
That unpatched regeneration supplies **no repaired-source or execution credit**.
The rejected helper and unmodified source identity remain preserved.

## Historical command-ownership repair and full aggregates

The corrected application applied the exact independently reviewed diagnostic
proposal, SHA256
`a8ce61acb171b51e565f24b5b0d04661b520b850261c94bc2ac3c664c4014357`.
The [production application receipt](../build/restrictive-audit/audio-close-wave-20261002/production-application-v2.json)
has SHA256 `965d48a5f1470c03229001bf4c47fc90a774a3a89d09c5b24f93c6a6dd8e5d7b`
and binds the successful baseline, exact fixture, proposal, independent
review, recovered preimage and application script. That wave's reader source SHA256 is
`cddc04059499106c8a7d4f97cc55d4ebcbec9f3c79ac47b9b4f6a43d5a3ac829`.
The two patch hunks change diagnostic handling; the functional observer
suffix remains unchanged.

The source handler snapshots `command+4` only after actual runtime/base/PPC
context and the complete eight-byte command range are qualified. Cached
candidate lookup does not dereference unknown group ownership. Original
LR, root and command frame must correlate before bounded queue fields are
read; only the complete queue/function/window envelope qualifies the owner.
Unqualified or unreadable states remain explicit. Raw pointers, generations
and low/full64 register lanes remain in `instance`; stable source semantics,
finite qualification flags/phases, caller, mission and action remain keys.
The source review and actual native preservation checks retain separate
authorities; applying this code alone did not supply the focused result.

The [Native focused repair receipt](../build/restrictive-audit/audio-close-wave-20261002/focused-v2-native-receipt.json)
has SHA256 `118cae2742bab0e30fc0340dc241a54019ff325875380763f424aaccd8892e6e`.
`OriginalAudioReaderLifecycle` passes 1,665 checks in 1.02 seconds on the exact
`cddc0405...ac829` reader and `645a48c9...a839` fixture. The fresh configured
scope SHA256 is
`a40807dc1d349b31718019bf78f3da821a45c75c687e3e71bb51c6ab3ae47a46`,
with 1,219 inputs, 219 executable identities and three DLLs rehashed. This
selected test grants no new execution credit to the other recorded binaries.

The independent [Release focused repair receipt](../build/restrictive-audit/audio-close-wave-20261002/focused-v2-native-release-receipt.json)
has SHA256 `eb0772e86aa40f5cf617a7f36cd0666f755a6c32158ab9217bb7be6351a5755b`.
It binds 1/1 pass with the same 1,665 checks and 42 raw rows in 0.61 seconds,
using the same exact reader/fixture source and its distinct Release executable.
Release scope SHA256 is
`a934e0fb7a002ff464dce66cb0b1c563742de1cf2be2e64c9611aabffe668aa4`,
with 1,219 inputs, 211 executable identities and three DLLs rehashed.
Its raw file is
`build/native-release/Simpsons-reader-close-24392-614758609-0.jsonl`,
37,330 bytes, SHA256
`a3589ca29355291c7e90218372f3673f9021c768baae572678b28bd7dd80878a`.
Native and Release receipts retain independent command, compiled scope,
executable, JUnit/LastTest, run-log and raw-file authority.

The bound focused executions establish fresh actual first/second close receipts,
correct cached generations from the later original free observations and
complete original eight-group/fourteen-manager/64-allocation/free/root-join
lifetime. The Closing duplicate and inherited-r4 collision now share one
stable group and one encounter, with two genuine functional failure snapshots
retaining their latest distinct raw instances. The other-live `r4` value
remains instance-only, including its deliberate `76543210` upper lane.

Six malformed controls independently preserve the exact functional failures:
unreadable and wrapping command, function mismatch, uncorrelated root register,
wrong base and copied context. Readable unknown-group and retired-first-group
controls remain two genuine no-ops, without fabricated failure or qualified
owner fields. Every failure has its fresh prevalidation encounter, and each
control lies in the actual first or second owner window. Complete PPC,
host CSR/LastError, bounded owned command/group/root bytes and native counters
remain preserved; both real pair frees and original root join complete.

The repaired raw file is
`build/native/Simpsons-reader-close-58760-614483437-0.jsonl`: 42 rows,
37,330 bytes, SHA256
`e313db9a0c8e598d4259077c03ee1ee29d78b801e8f0a7e33e3e9d8c61314a11`.
The receipt binds its exact bytes to JUnit, LastTest, run/build logs and the
compiled/executable scope. Both focused receipts have `full_suite` null;
their successful selected tests do not establish a complete aggregate.

The subsequent [Native full receipt](../build/restrictive-audit/audio-close-wave-20261002/full-v2-native-receipt.json),
SHA256 `c1273bb3b1504303a071fd13c8858e8f2eb41f8ffcd26ab27cfac861d2804a04`,
strictly binds the completed 500/500 Native suite in 415.97 seconds, with
zero failures or skips and the same fresh Native source/executable scope.
It preserves all actual JUnit/LastTest outcomes and separately extracts the
current 1,665-check audio fixture. That execution has its own 42-row raw file,
`build/native/Simpsons-reader-close-59716-615099406-0.jsonl`, 37,297 bytes,
SHA256 `94620ba7b87f9268f5ca82cd1c92db6054b997102e3284f2c267f2708afa3ef8`.

The independent [Release full receipt](../build/restrictive-audit/audio-close-wave-20261002/full-v2-native-release-receipt.json),
SHA256 `fd9075168261c493b407b068fe3b6bd4b7a6c5cc2933a49f93302b0f218e18fd`,
strictly binds its completed 500/500 suite in 448.35 seconds, with zero
failures or skips and the separate Release scope `a934e0fb...68aa4` above.
Its selected audio fixture again passes 1,665 checks with 42 raw rows:
`build/native-release/Simpsons-reader-close-64312-615741531-0.jsonl`,
37,329 bytes, SHA256
`ab90eec6cd7b5d83c083a8e3deb9ee98257afe7257a1e5cd10f5e6e1e01ec15a`.
Both full receipts preserve actual JUnit/LastTest outcomes and their distinct
current raw files; the reader and fixture identities remain unchanged.
Earlier 500-suite passes are not retroactively attributed to this repair,
and these aggregates supply no new catalog/gameplay join.

## Current source-only inventory

Fresh [effects](../build/restrictive-check-audit/close-profile-source-effects-v1-20261003.json)
has SHA256 `ed9a6f2917ca8236287d764aa0d7c880abcd22bb53b82265eb49014ad62a0e99`:
49 effects, 110 registered passes, 47 artifact-admitted passes, 45 declared
selections and 63 missing-artifact passes. It retains the same 490-archive,
8,770-VFX catalog and 3,447 unique payload hashes.
[Shader producers](../build/restrictive-check-audit/close-profile-source-shader-producers-v1-20261003.json)
has SHA256 `da733f9ed69c6d3a982cc9978c93a3e67b7a09a23897530b45b0f79514afa651`,
two source findings and no texture scope.

The [mechanical source census](../build/restrictive-check-audit/close-profile-source-census-v1-20261003.json)
has SHA256 `30ef26a7e2fc172e316e5f029d43283b8e784c13a673f2ca3f2e8004764ae748`:
769 candidates and 1,112 keyword lines across 274 files, with categories
unchanged. It imports no runtime logs, native receipts, matrix join or new
catalog/gameplay/lifetime credit. Mechanical candidates and model pending
labels do not override separately bound original lifetime results. All old
`audio-close-source-*` reports remain historical and preserved.

## Profile, catalog and lifetime limits

The close asset remains generic `original-reader`. The current logger now
retains qualified finite construction origin and cached member/ring/raw-effective
entry/allocator-override profile fields. Its opaque `Group.identifier` is an
original incrementing lookup serial, preserved in `instance` alongside actual
create caller. The whole stock-factory triple tests serial exclusion with
identical action, mission, caller and phase, plus distinct genuine numeric
profiles through complete original lifetimes. The prior
[open-profile note](../build/restrictive-audit/audio-reader-close-attribution-20261002/open-close-profile-linkage-audio_checks.md),
SHA256 `557f36b5023c66208da0c760cd3650d645417874b5ef051196a135d7ad606ed4`,
remains historical missing-field evidence. This wave resolves that numeric
profile gap, without assigning serials or numeric profiles any packaged-source
meaning. Profile admission and auxiliary seek fields are unchanged.

The [profile proposal authority](../build/restrictive-audit/close-profile-candidate-20261002/source-candidate-receipt.json),
SHA256 `2cbec64e86b190b22040d760a2e68a69a8f1297e221dc9533af8a949770324d2`,
now identifies the exact applied/tested source. The separate
[catalog certificate candidate](../build/restrictive-audit/audio-catalog-identity-candidate-20261002/source-candidate-receipt.json)
remains source-only and unapplied. Serialized source SHA and header/audio
extent retention would be a catalog certificate, not proof of an original
opened/read source or a published reader relation. No filename, AMX cue,
EAAC header, stream ordinal or unique archive occurrence is linked to close.

The [future linkage source review](../build/restrictive-audit/close-profile-linkage-source-20261002/independent-review-geometry-v1.json),
SHA256 `5f7c70b11d88a24ac08a4d2560ad62927048d2967ce684a334c353fe0b6a1cd9`,
independently rechecks 26 original spans and 62 instruction/data words. Its
construction-origin and common-key serial prerequisites are now addressed by
this closed profile wave; the source-lineage design remains unimplemented.
It still requires exact original opened/read file and selected-record proof,
claim/manager epoch/reader-group generation revalidation at SourceRelation
publication, and the existing lock direction. No filesystem/catalog work or
owner lookup may be introduced under the reader mutex. Resource-owner
generation remains distinct from reader-group generation.

A declared filename requires the complete bounded original command, used and
capacity bounds, and a bounded NUL after `+38`; the original 128-byte copy does
not universally guarantee termination. Declared paths and candidate header
matches cannot substitute for owned file/read provenance or resolve aliases.
Queue-row epochs, physical aliases, concurrent owner pins, whole guest-memory
preservation and an audio-device fence remain unproved. This wave grants no
new PCM/EOF, codec, seek, gameplay or catalog lifetime credit. Older 500/497
receipts and matrices retain their original frozen authorities.
