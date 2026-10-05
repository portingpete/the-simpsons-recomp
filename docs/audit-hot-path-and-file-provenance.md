# Audit hot paths, opened-file provenance and the allocation index

Three related changes found or made while taking the row-prevalidation work live.
Fixture tests were green throughout; the live route exposed the cost, which is why
real stage runs stay part of this audit.

## The live finding

With the stage-3 executable the first live stage (`spr_hub`) did not become
playable inside the 180-second deadline and delivered **0 of 20** route actions;
the 497-test executable finishes the same stage in **51.8 s** on the same machine
(controlled re-run, same flags). The partial run is preserved in
`build/restrictive-audit/stage-sweep-rowprevalidation-20261003`. A second run after
the first fix (below) still timed out, so the fix was not the cause; an in-process
stack sample of that executable (`SIMPSONS_STACK_SAMPLE`, 15,027 raw samples,
`rigid-first-capture-wave-20261003/stage5-baseline-profile-stage4-exe-sprhub.txt`)
read about **90% self time in `EngineAudioOwners::allocationSpan`**, reached mostly
from `observeProducerEntry` (non-mesh boundaries included). The raw samples are
preserved; the symbolized readout was read at capture time and cannot be reproduced
now because the stage-4 PDB was overwritten by later builds.

`allocationSpan` ("smallest live allocation containing this address") scanned every
tracked allocation under the owner mutex. Earlier waves started calling it per draw
(row-span admission and producer-entry logging); a hub level tracks hundreds of
thousands of allocations. The cost was invisible to fixtures with a handful of them.

## Allocation containment index (`runtime/heap_page_index.h`)

4 KiB pages each list the allocations overlapping them (allocations of 16 MiB or
more sit in a short side list). Maintained at the heap's only two mutation sites;
`allocationSpan` keeps its rule (smallest containing extent) and its closing check.
`OriginalHeapPageIndex` compares the index with a brute-force scan on page
boundaries, the top of the address space, nested and large allocations, removals,
randomized overlapping layouts (equal-extent ties compared by extent, since a scan's
pick among them was only iteration order), and 300,000 allocations: 400,000 lookups
in 0.035 s. **Live: `spr_hub` 51.76 s with all 20 actions delivered, matching the
51.77 s control.**

## Row-observer hot path (`ResourceAudit` epoch)

The row observer fired for every distinct row group, which real meshes make very
frequent. `ResourceAudit::epoch()` now advances only when the mission or last-action
**value changes** (`XamInputGetState` re-announces the same string every poll while a
stick is held, so a plain counter would defeat any cache). The engine keeps a set of
already-observed stable row keys per epoch; a failing row whose key is cached is still
published by the exception handler before the original exception propagates. A
repeated compiled-index control (same action, cached keys) pins that path in the 65,536
and capture fixtures; disabling the handler makes four of them fail (preserved in
`stage4-mutation-check-v1.log`).

## Opened-file provenance (observer only)

`runtime/filesystem.cpp` records, only when an audit file is active, what the guest
actually opened and read: kind `file_open` (lifecycle) with the declared alias kept
distinct from the normalized path under the game root, access/share/options, extent and
the native file identity (volume serial and file index); kind `file_read` per
successful read with the real offset (explicit, or the position before an implicit
read), requested and completed bytes and a per-open ordinal. Offsets, handles and
ordinals are instance data; the stable group is root, status and offset mode. Failed,
EOF, zero-length and post-close reads emit nothing. `OriginalAssetFilesystem` verifies
all of this against an independent native identity query and the exact offsets. Its
baseline (the pre-edit `filesystem.cpp`, reconstructed and confirmed by SHA256 equality
with the hash recorded in the stage-3 scope) fails with "provenance receipts missing".
This is the opened-file/read edge; its relation to audio reader claims is the next
section.

## Audio claim to file-read relation (stage 7, observer only)

When the original streamed producer admits a block, the claim's bytes are already
content-verified against the catalog (`AudioCatalog::match` compares the block
SHA-256, in order, per candidate). Stage 7 adds kind `audio-source-file`, emitted right
after that match, to say **which recorded guest file reads placed those bytes**.

`runtime/filesystem.cpp` keeps a ring of the last 512 successful audited reads (open id,
ordinal, file offset, destination, completed bytes). `recentFileReadCovering(address,
length, maxSequence)` resolves every byte of the claim's range to the newest recorded read
that wrote it; the producer takes `fileReadSequence()` *before* copying the claim, so a
read completing afterwards on another thread cannot be mistaken for the source. Status:
`covered` (one read), `spanning` (several reads of the **same open** at linear file
offsets), `ambiguous` (a gap or a non-linear mix), `none`. The receipt records the opened
file (relative path and declared alias), up to eight last-writer pieces (address range,
file offset, read ordinal, open id, read sequence), the true read count, up to eight
catalog candidates (path, audio offset, bytes, stream index) and `catalog_path_match`
(a candidate path equals the opened path or is a whole-component suffix of it; `xaudio.snu`
does not match `audio.snu`). The relation is by address recency, never by content
(`relation_content_compared=0`); the claim bytes themselves are catalog-verified.

Tests: `OriginalAssetFilesystem` covers covering/partial/adjacent/degenerate ranges,
newest-read precedence, failed reads never recorded, two- and ten-read spans (pieces
sorted, offsets, open and sequence kept), non-linear and cross-open adjacency staying
ambiguous, the pre-copy snapshot bound, eviction after 512 reads and the path-agreement
helper. The new API did not exist before this change, so no behavioral baseline of the
old source can run; instead five deliberate defects in the final source (snapshot bound
ignored, cross-open spans merged, partial-component suffix accepted, pieces unsorted,
piece identity dropped) each fail the test with a distinct message and the restored file's
hash equals the pristine one (`stage7-mutation-check-v2.log`). The first attempt stopped
after two kills on a tooling error and is preserved with a note. There is no host test of
the receipt emitter itself; it is covered by the live runs below only.

Independent check: `build/restrictive-audit/verify-audio-relation-20261003.py` parses
`analysis/audio_catalog.bin` itself and, for every receipt, takes each candidate stream's
block row (`rawOffset`, byte length) to compute where the block begins in the file. A
piece is **confirmed** when the file offset the recorded read reports at a guest address
equals that offset plus the address delta inside the claim. Over **2,132 receipts** from
eight runs of the final executable (two complete 18-stage sweeps, three reruns, three
controls), covering 186 opened files and 1,286 distinct (file, block) pairs (blocks 1 to
745):

- **1,107 covered and 681 spanning** claims: every byte confirmed, in a catalog row whose
  length equals the claim and whose source path agrees with the opened file. All 382
  first-block claims are covered. No `none`, no path disagreement.
- **344 ambiguous** claims, in every one a suffix is confirmed and the head is not: 13
  bytes up to 99.7% of the claim (median 3,911) was not last written by a recorded read at
  the expected offset, although the bytes are catalog-verified. So something other than a
  recorded file read at that offset placed them; guest code assembling a block that
  straddles a read-buffer boundary is the inference, **not an observation**. Suffix
  starts share a fixed spacing in some files (348,160 bytes in eleven `.snu` files,
  296,960 in `spr_mus`) and not in others (128 bytes for several `.mus`), so no uniform
  model is claimed.

Receipts are per stable group (file, relation, last action, mission): a sample of the
claims in a run, not every block.

## Live finding: a recorded mono pass after a completed rigid replay

Once the live sweep could finish, `eighty_bites` failed deterministically 4.5 s after
map-ready with `Rigid replay staging has no original replay association` (three runs,
two executables; the old 497-test executable fails identically; preserved as
`repro-eighty-stage5-a`, `repro-eighty-old497` and `repro-eighty-stage5-b`, and in
`stage-sweep-stage5-20261003`). The original hook `826B3270` serves both recorded rigid
and recorded mono constant uploads. `rigidReplayOperation` routed to the mono handler
only when `rigidReplay.cpu` was null, but a *completed* recorded rigid replay keeps its
run state (`cpu` set, `prepared` true) until the next replay starts or its payload
retires, so any mono pass after one was misrouted into the rigid check and rejected.
The route to the stage is not an artificial input: it is the opening route's own
recorded draws, a skin pass, then a mono post pass.

Fix: only an *unprepared* rigid run is staging and claims the hooks. No fixture drives
a recorded rigid replay, so the live stage route is the regression; the fixed
executable completes the stage (51.6 s, 20 of 20 actions).

## Live coverage (final executable)

`build/restrictive-audit/stage-sweep-stage6-20261003`: **18 of 18 opening routes
complete with all 20 commands delivered**, owned process and private store copies,
primary stores unchanged, zero failure rows (the three stages rejected in the original
sweep, `tree_hugger`, `mob_rules` and `dayofthedolphins`, all pass). The immutable
join `build/restrictive-check-audit/stage6-live-encounters-v1-20261003.json` (SHA256
`5e4f5f35e05b0652b29fb1277064b3804bb4222e1533d0fe328578fbb6586f85`) hashes all 18
logs and reports what was actually encountered:

- **27 distinct shader passes** reflected-selected live, **all 27 in the original
  110-pass catalog and admitted** (skin, rigid, textured, gloss, dual, UV, multitone,
  normalmap, sky, flipbook, chocolate, mono). 83 catalog passes were not reached by
  these routes; that is not evidence they are unsupported.
- 10,696 row receipts from stock rigid meshes: every row is triangle-strip (6), base
  vertex 0, authored group count 0, word2/word8 zero; start index is nonzero in
  2,723; the largest table has **19** submeshes, far below any former cap.
- 761 file opens over 368 distinct files (212 `.str`, 120 `.snu`, 16 `.mus`, 16
  `.vp6`, 3 `.lua`, 1 `.txt`) and 796 read groups; every read lies inside the extent of
  the file the guest opened.

Limits: opening routes only. No pickup, destruction, dialogue, checkpoint, death or
mission-exit coverage is claimed here (the earlier frozen LOC skip, normal exit and Tree
death-reload receipts remain separate). A reflected-selected pass proves reachability,
not a complete lifetime.

### Final executable (stage 7)

`build/restrictive-audit/frozen-stage7g-native-20261003` (SHA256
`0da700df2598b96f9182c923888e5ba9fc6a42d9f7a663820372f919159f8e48`; the frozen copy equals the built
executable and holds no reparse points). The first complete sweep
(`stage-sweep-stage7-20261003`) passed 17 of 18 and **failed `meetthyplayer`**: its first
command went out 65 s after the start (about 12 s in the other runs) and the route stopped
at an unacknowledged `interact` after 11 actions (111.8 s, no failure rows). Three immediate
reruns pass (42.0, 42.2 and 42.0 s), and a second complete sweep
(`stage-sweep-stage7b-20261003`) passes **18 of 18 with 20 of 20 commands each** (43 to 57 s,
primary stores unchanged). Later one `spr_hub` control started late as well (114.0 s,
success) while two repeats on the same executable (51.6 s, 52.1 s) and one on the frozen
stage-6 executable (51.6 s) are normal. The causes of both slow starts were not determined
(background load on the machine is suspected, not shown); the failed and slow runs are
preserved and bound in the receipt, so no regression is excluded beyond those repeats.

## Evidence

| Configuration | Receipt SHA256 | Scope SHA256 | Aggregate |
| --- | --- | --- | --- |
| Native, stage 4 | `f16aff38d648af74a9173220565d79a1469493392749744c8a0cf6c624f10875` | `c8d4fef4…62d40` | 525/525, 429.51 s |
| Native, stage 5 | `7c6c76529f23ebabf1fadf0b04eac76382c91fc875621d911a80a35066beadaa` | `5ce34d1a…b40a8` | 526/526, 432.35 s |
| Native, stage 6 | `aa94b1dc685d8bddee01c7c75c36ac12d59c3cb27dbb651920b0b2a76c68a53d` | `ce6f7655…174ad2c` | 526/526, 419.47 s |
| Release, stage 6 | `362ca7e02dc3fe6475114c402ac702a130bf761978afc6f3ead0b58a0ac743b6` | `43cd1a57…cace29b` | 526/526, 412.22 s |
| **Native, stage 7 (final)** | `20814111feb717219487778fb77b6057baf918c1f51e2ce4574e1206ab098279` | `8cbdfb1b…764de9a5` | **526/526, 427.06 s** |
| **Release, stage 7 (final)** | `2412c2e0773ff57ef9c79e7a41a16fff468e1621d0c31a08c54590c11013c253` | `281cba90…50d0736d` | **526/526, 404.46 s** |

The stage-6 receipts re-derive the 24 row cases from raw logs, the filesystem and
index joins, the `spr_hub` timings (51.77 s control, deadline-reached slow run, 51.76 s
fixed), the `eighty_bites` baseline/fixed runs and all 18 live results. Final
identities: `engine_effects.cpp` `292f82da…d9ecb`; frozen executable
`79b519a5…a1504e` (`build/restrictive-audit/frozen-stage6-native-20261003`).

The stage-7 receipts re-derive the same 24 row cases from this aggregate's raw logs and bind
the five-mutation check, the preserved pre- and post-edit sources (`stage7-before-*`,
`stage7-after-*`), the frozen executable, the failed first sweep with its reruns, the
second sweep, the three `spr_hub` controls (one slow) with the stage-6 repeat, and the
independent catalog check (`audio-relation-stage7g-verify-v2-20261003.json`, 2,132
receipts). Final identities: `filesystem.cpp` `965b1da7…f93a3362`, `filesystem.h`
`1f57323f…b6641fe3`, `engine_audio_owners.cpp` `6215c646…25bb4cd6`, `test_filesystem.cpp`
`d3bce86c…2e4a2ada` (`engine_effects.cpp` unchanged, `292f82da…`).

Fresh `stage7-effects`, `stage7-shader-producers` and `stage7-expanded-census` reports
(`build/restrictive-check-audit`, `*-v1-20261003.json`) are summary-identical to stage 6:
49 effects and 110 passes (47 admitted, 63 without native artifacts), 314 source files, 821
mechanical guard candidates and 1,168 keyword lines, all candidates still untriaged.
