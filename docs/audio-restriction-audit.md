# Audio restriction audit (2026-10-02)

The stream-reader factory still rejected a source-valid empty-ring case and
imposed an artificial 64-group quota after the earlier dynamic-size repair.
`runtime/engine_audio_reader.cpp` now admits the exact original zero result
for a positive sub-byte duration and tracks all original live allocations.
It retains
the original caller, stack, voice, allocator, entry-index, arithmetic witness,
signed-size and owned payload guards. This is a producer-derived repair; an
empty ring does not authorize a nonempty compressed claim.

## Source evidence and valid bounds

Original `82330540` constructs the stream voice first. `823305B8` bypasses
reader creation only when duration compares equal to zero. At `823305BC`,
`823305C0` and `823305C4`, only a zero bitrate becomes 48. Negative bitrates
remain negative and fail the signed extent contract. `823305C8` sign-extends
that actual bitrate before floating conversion. `82330604`, `82330608` and
`82330610` perform single-precision rounding and the two products; the scale
at `821DD434` is exactly -1000. `82330614` truncates toward zero and
`82330618` stores the integer low word. `82330620`/`82330624` calculate
`(15 - lowWord) & ~15` and `82330628` calls the reader group factory.

For positive finite durations, a product strictly between -1 and zero
legitimately truncates to zero. The stock code still constructs a reader
group. Its original containing allocation has size 0x50 with ring start at
the end of that allocation. `8238C8A4` divides the signed ring size by six;
zero is a valid dividend. The resulting threshold is zero. Empty claim,
reset, deferred group retirement, manager frees and allocator frees do not
need a compressed payload.

The structural ring bounds are 0 through 0x7FFFFFF0 bytes, aligned to 16.
The actual two single-precision products further constrain numerical
reachability. This is not a promise that every aligned integer can be
requested, or that a near-limit allocation will succeed. Signed wrap,
nonfinite duration/products, negative durations, mismatched integer
witnesses and changed original provenance remain rejected before allocation.
The native admission currently allows requested entries 0 through 253, plus
three reserve entries, so every allocated slot fits the token's eight-bit
index. This is a conservative native policy, not a recovered original
constructor cap. The [entry-range source audit](../build/restrictive-audit/reader-entry-range-source-20261003/README.md)
finds full-word parameter transport and no original 253/256 comparison.
Higher slots alias after token masking and can break claim accounting and
reset progress; the full valid original scheduling/lifetime range remains
unresolved. The existing raw254 negative also changes the count without
matching its parent voice field, so it does not isolate the count ceiling.

There is no original global 64-group quota. The factory inserts each actual
allocated group into the original list; its containing allocator and distinct
live extents govern ownership. The native map now grows with those allocations.
A separate original-path case constructs 65 groups simultaneously, uses and
resets every empty reader, then performs each real stream destructor and
waits for every original manager/group worker free.

The original-image direct-call census finds exactly two reader-group calls:
`82330628` for these dynamic stream readers and `82816804` for the stock
four-reader startup group. Other callers remain unqualified.

## Independent original setup and lifetime cases

`AudioReaderProducerTests <original-image> [case-index]` accepts indices
0 through 13. Each selected case starts the real original audio services,
constructs through `82330540`, uses the genuine group/manager/handle,
destroys the original stream through `82333EC8`, waits for actual worker
retirement, shuts down the original stream services and joins the Dac worker.
Passing an index runs the case in its own process so an earlier rejection
cannot prevent later cases from being attempted.

| Index | Input | Ring | Use before original release |
| --- | --- | --- | --- |
| 0 | Default bitrate, four entries | 0x47E00 | Request, claim, copy, release |
| 1 | Bitrate 48, four entries | 0x55280 | Request, claim, copy, release |
| 2 | Bitrate 48, four entries | 0x4BC80 | Request, claim, copy, release |
| 3 | 1.25 seconds, bitrate 96, seven entries | 120000 | Request, claim, copy, release |
| 4 | 2 seconds, bitrate 17, zero entries | 34000 | Request, claim, copy, release |
| 5 | 0.125 seconds, bitrate 80, 253 entries | 10000 | Request, claim, copy, release |
| 6 | 1.25 seconds, zero/default bitrate | 60000 | Request, claim, copy, release |
| 7 | 0.128 seconds, bitrate 1 | 128 | Deferred request, empty claim, reset cancellation |
| 8 | 0.001 seconds, bitrate 1 | 16 | Deferred request, empty claim, reset cancellation |
| 9 | 0.0005 seconds, bitrate 1 | 0 | Empty claim, reset |
| 10 | Smallest positive float, bitrate 1 | 0 | Empty claim, reset |
| 11 | 65 simultaneously live streams, 0.0005 seconds, bitrate 1 | 0 each | Empty claim and reset for each; retire all groups |
| 12 | 2.064 seconds, bitrate 1 | 2064 | Request, claim, copy, release at the initial watermark |
| 13 | 2.048 seconds, bitrate 1 | 2048 | Deferred request, empty claim, reset cancellation below the watermark |

The fresh-ring request scheduler reserves 16 bytes (`8238C570`) and compares
the remainder with the rounded watermark (`8238C604`). For these small rings,
the watermark is 2048 regardless of the requested 64-byte payload. A valid
ring of 128, 16 or 2048 bytes defers the request and enters status 2; an actual
reset cancels its token and clears that status. A 2064-byte ring reaches the
initial threshold and returns the real claim. Small-ring allocation support
does not imply that any requested payload becomes immediately available.

Every independent process also checks the separate zero-duration bypass,
which creates a stream voice without a reader or consumed identifier. Real
producer-frame mutations test caller, stack, owner, allocator, identifiers,
argument flags, entry bounds, float witnesses, nonfinite/negative durations,
signed overflow and source instruction changes. Small/empty readers reject
an unregistered payload before reading its address. Nonempty cases retain
the 64 copied bytes after the actual containing storage is freed and check
all nonvolatile GPR/FPR, SP and LR preservation.

## Encounter logging

The original reader create, claim, reset, release, manager close, group
retirement and final group free boundaries submit resource audit receipts
before admission or guest reads. Receipts include the raw original caller,
duration, actual bitrate, ring and entry parameters; native ownership phases;
and separate instance addresses/generations/tokens. Logging uses native
provenance when available and labels unregistered identities explicitly.
It does not dereference an invalid owner to construct a diagnostic, and does
not change admission or authorize a resource. The shared resource audit
provides the mission, last action and failure grouping.

The encoded producer also records its cached EAAC header before frame or
member validation. Resident receipts include the catalog bank path/name and
header offset; AMX receipts include the payload name and cue offset. It keeps
member/group/state/bank generations in the instance fields. A streamed header
can identify multiple source files, so that initial receipt deliberately
retains the header identity rather than inventing a unique source filename.
The existing ordered owned-block catalog match resolves playback candidates.

## Catalog comparison and remaining restrictive paths

The encoded census already covers 7,430 SNU/MUS files, 9,470 streams,
377,643 blocks and 677,048 layers. All catalog streams use 1, 2, 4 or 6
channels; the production 1-through-6 EXm0 allocation bound covers them.
The complete pure streamed decoder census covers 9,256 unique chains and
rechecks duplicate streams/blocks. Resident SBK coverage has 45,382 cues
and 45,498 blocks; the decoder census covers 9,024 unique encoded block
keys. AMX coverage has 254 cues and 247 unique encoded keys. Existing
source/codec provenance checks remain required when consuming these reports.

The one-MiB compressed claim/block and four-Mi-frame decoded resident
storage limits cover all catalog data. Those are host storage contracts,
not inferred complete original limits. Exact 256-frame Dac processing,
20 request/codec slots and three mono/stereo XMA layers come from original
layouts or their established complete setup regressions. No observed
catalog mismatch justifies widening those contracts here.

One substantive production restriction remains: seek/config state
`W+20`, `W+24`, `P+3C`, `P+40` and `P+44` is guarded zero. The original
`82341F78` derives a requested start sample from the command's double time
at offset 24 and the voice playback rate. When the request is inside the
sound, it passes command offset 40 to `82342748` at `82342058`.
`82342748` calls the metadata parser `8234EB50` when both start sample and
command metadata pointer are nonzero, then populates those five fields,
`P+48`, `P+4C` and `P+14`. The metadata parser has separate record encodings
at `8234EBB8`, `8234EC70` and `8234EDF0`; it is not encoded in the existing
EAAC sound catalogs. This is an unsupported original API path; an authored
nonzero command-metadata input has not been established. It needs a
command-metadata census and a complete real seek/decode/stop/restart/release
fixture before repair. The following source trace narrows that next work.

## Optional seek origin and semantics

The full configuration selector is **5**, pinned by the original
`82341B3C: 2F040005` and `82341B40: 419A02E0`. Selector 4 creates a separate
time command and is not this configuration payload. Selector 5 accepts:

| Control input offset | Original meaning or destination |
| --- | --- |
| 00 | Double copied to command+08, then the voice time origin |
| 08 | Double copied to command+10, then the source file base |
| 10 | Double requested start time; command+18 |
| 18 | Source filename pointer; copied into the command string |
| 1C | EAAC header pointer; command+24 |
| 20 | Optional seek metadata pointer; command+28 |
| 24 | Reader-group identifier; command+20 |
| 28 | Float converted to the command byte at+2E |
| 2C | The original producer's sequence/tag float |

Selector 0 adapts the smaller ordinary configuration into the same command.
It synthesizes start time 0 from `821DD1E8` and a zero seek metadata pointer.
The real MUS producer `82334468` obtains a 28-byte authored table record via
`823343F0`, derives header=`bankBase+16*record[2]` and
fileBase=`128*record[3]`, then invokes selector 0. The resident bank producer
`823337D8` likewise constructs the smaller ordinary configuration and invokes
selector 0. These source paths establish why ordinary asset playback has no
optional seek metadata. They do not establish that every future caller does.

The catalog inspector requires SNU word+0C and MUS record word+18 to be zero,
but their identities as seek metadata are **not proved**; that field check is
not a command-metadata census. The SNU wrapper-to-audio region remains opaque.
No real nonzero metadata object or selector-5 asset caller was recovered in
this bounded trace. A fabricated row table would only test parser arithmetic,
so it was not substituted for an authored source lifecycle regression.

When a genuine full command requests an inside-source start, `82341F78`
truncates `playbackRate * startTime` to a signed sample count and passes that
count to `82342748`. Its metadata parser reads an eight-byte header:
byte0=0, byte1=flags, BE16+2=priming/lookback frames, BE32+4=relative auxiliary
data offset (zero means null). The signed high nibble of flags selects
uncompressed four-word rows (0) or signed delta rows (1). For an uncompressed
row, the four big-endian words are compressed-file byte increment, auxiliary
data byte increment, declared sample-frame increment and terminal flag.
`8234EC70` selects the row containing `max(start-priming,0)` and derives:

| Destination | Derived value |
| --- | --- |
| W+24, P+14 | Declared frame prefix before selected row |
| P+44 | Compressed file-byte prefix before selected row |
| P+3C | Auxiliary base plus auxiliary byte prefix |
| P+40 | Minimum of priming frames and start minus frame prefix |
| W+20 | Start minus priming contribution minus frame prefix |
| P+48 | Metadata flags |
| P.byte4C | Whether selected row's terminal flag equals 1 |

The initial original reader request is issued at fileBase+P44. The original
codec allocation sets the seek argument when W20, W24 or P40 is nonzero
(`82342BA8..82342BE0`); `823424D8` forwards W20 as initial consumed samples,
P3C as auxiliary data and P48 as auxiliary flags. Native support must establish
the exact selected block/catalog sequence, preserved preroll/packet state and
PCM skip semantics. Advancing a catalog ordinal or removing the zero-field
guard alone cannot supply that proof. Original completion must still retire
the declared block and release its real claim; early stop destroys the codec
before releasing outstanding claims at `82342CC8`.

A complete regression should capture an actual owned metadata allocation,
call the original selector-5 command path under the real Q lock, allow the
original request/claim and codec setup, compare consumed PCM and remaining
sample accounting with an independently decoded source, then exercise both
normal completion and stop while a claim is pending. It must retain the bank
or stream metadata owner until the queued setup/parser has consumed it.
Malformed metadata requires a pre-parser bounded ownership check: the original
parser has no row-count parameter, returns partially initialized output for
unknown encodings, and clears only selected fields on a version/error result.
Those original behaviors are not permission to read arbitrary guest memory.

### Authored menu MUS member positioning: October 2 source census

The later read-only census traced the authored menu music scheduler, rather
than treating a nonzero MUS member selector as a sample seek. The immutable
`menu_mus.msx` extraction contains 142 nodes, seven events and 130 nodes
with positive segment numbers. Those nodes cover every segment from 1 through
37. Original bank registration `8232E240` stores the PFD body word `+0x34`
as active-bank `+0x3C`; for this descriptor it points to `PFD+0xDD4`.
Its 37 eight-byte timing rows exactly match the 37 MUS record `+0xC`
member units. The second timing word is preserved as authored data; it is
not optional seek metadata.

Whole segment dispatcher `82337C80` uses its existing track state's node
identity at `+8`. At `82337F90` it calls `82337010`, which resolves the
node through the registered node table, reads its signed first halfword,
and returns the first word of timing row `segment-1`. It forwards that
value as `r5` to virtual voice slot `+0x28` at `82337FB4`.
The actual MUS vtable is `821DC850`, whose `+0x28` entry is `82334468`.
That function's `823345FC: 38800000` fixes the stream configuration
selector to zero before its actual virtual call at `82334670`.
`82341E0C..82341E18` then supplies the verified zero double at
`821DD1E8` and a null optional metadata pointer. Thus all authored menu
segment selections use the zero-start branch of `82342748`.

A concrete nonzero authored member input is node 5, `PFD+0x1C0`
(MSX payload `+0x210`), whose first halfword is segment 2. Its timing row
at `PFD+0xDDC` yields selector `0x31C6`. Original `823343F0` resolves it
to MUS record 1, metadata header `+0x510`, and file base `+0x18E300`.
The existing configured completion fixture used selector 81, which reaches
that same original record; it did not execute this authored scheduler or
the exact selector `0x31C6`. Its already recorded member-1 lifecycle remains
valid, with no new scheduler or seek execution credit from this census.

For the immutable menu bank, nonnegative helper selectors `0..215184`
resolve within the 37 actual records. Record 0 receives `0..80`; later
record `i` receives values above the preceding record's maximum of its
header/start units, through the current record's maximum. The receipt
records every interval, exact selected header and file extent. Negative
signed selectors also resolve to record 0 in the original helper, but no
authored negative selector is established. Values above the last interval
have no valid-input credit: `823343F0` has no record-count bound and can
walk outside the owned table. These are member-selection intervals, not
time or decoded-frame bounds.

The source-only receipt is
`build/restrictive-audit/menu-mus-seek-origin-20261002.json`, SHA256
`7ce0d99ca10f24cdf674538d91a2d7e89bc408b84e1c2cb90c526208283a9ca9`.
It pins the unchanged original image SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`,
archive/extracted descriptor/MUS identities, all member intervals and
16 original function byte hashes. Its companion script only reads bytes
and writes this audit artifact.

This narrows the remaining seek restriction without broadening it.
The selector-5 parser branch requires a converted start count inside the
source, `1..frames-1`, and a nonnull metadata pointer. A valid complete
input also needs the corresponding authored metadata owner and encoded
block/preroll state. Neither this descriptor nor its recovered scheduler
supplies that input. A future original-path fixture can retain the existing
configured MUS construction/read/mix/stop/metadata-free/deferred-reader/
worker-join recipe, but must first establish a genuine selector-5 producer
and keep its metadata live through queued parsing. No nonzero sample-seek
execution, repair, lifetime or malformed-metadata credit is claimed here.

## Encounter provenance and teardown coverage

The October 2 stage sweep writes pre-validation `audio-source` receipts using
cached member headers and native owners. A resident receipt's bank path,
embedded SBK name and bank-relative header offset come from the exact decoded
payload match at `engine_audio_owners.cpp`'s `82711918` load observation; the
resident catalog's 156 payload hashes are all distinct. AMX receipts similarly
retain the exact payload name and cue offset. Their eight payload hashes are
distinct, but one payload can have several packaged STR occurrences. The
receipt proves that payload/cue, not which occurrence supplied it. A bare
stream EAAC header may match several authored streams and remains a candidate
set unless stronger source provenance exists. Guest pointer proximity is not
an asset identity.

The diagnostic revision adds cached `payload_sha256` for resident and AMX
sources and cached resident `archive`/`entry`, with no additional guest reads.
Reader-create receipts label bitrate, duration and voice only for the real
`8233062C` streamed caller. Startup and unknown callers retain those registers
as raw instance fields. Group counts, in-flight operation counts, allocation
addresses and generations belong to the instance snapshot; stable size,
format, caller and ownership phase determine the combination group. The
ResourceAudit sequence counts all observations before deduplication, so a
sequence near 417,000 is not 417,000 JSONL rows.

Closing a configured EXm0 voice at `82342CC8` destroys its codec before
`82342D28` releases outstanding source requests. Native `State::cancel`
closes the decoder and marks retained source receipts cancelled, but keeps
their original reader claims and bank/AMX owners until the real release join.
Stream retirement at `82342D60` requires the reader callback and exact charge
subtraction; resident/AMX retirement at `82342D64` requires the original
null-reader branch and unchanged zero charge. Normal completion uses
`82341840` and joins at `82341880`/`82341884`. Both branches retire only delivered,
cancelled or failed decoder receipts. An SBK release marks it retiring at
`82812A00`; actual `8268DF90` allocation free must follow source retirement.
Member/state/group generation checks keep a reused allocation from reviving
an old request.

The 14 focused reader tests cover real request/claim/release or deferred-token
reset, original voice/group destruction, worker frees and root/service
shutdown. `test_engine_audio` separately covers unconfigured EXm0 ownership;
the named SBK lifecycle test covers original splitting/disposal and stale
generation rejection. These do not yet exercise a configured original voice
stopped with a resident or streamed decoder receipt pending, nor checkpoint,
death or mission-exit resource replacement. A targeted next fixture should
use an actually encountered cue, drive its original configuration/worker,
stop it at `82342CC8` while its real receipt is pending, allow the actual
release joins, then dispose its resource and verify zero outstanding claims
and rejection of the old generation. The sweep reviewed so far has no new
audio admission rejection to justify widening a guard for that fixture.

Terminal window closure or a runtime shutdown following a rendering rejection
does not prove that the audio asset stored in a worker's last receipt failed
admission. Preserve those failure snapshots as terminal context and keep the
first substantive rejection as the root cause. Host-only release after an
aborted sweep is not a successful original teardown.

## Configured original MUS owner lifecycle

`tests/test_audio_stream_configured.cpp` now adds two independent configured
MUS cases. Both passed independently on the native target in
`build/restrictive-audit/configured-mus-tests-8.xml`; complete output is
preserved in `configured-mus-tests-8-full.log` and input/library/executable
hashes in `configured-mus-pass-receipt.json`. No production audio guard was
broadened to make these tests pass.

Both cases load the immutable `audiostreams/menu_mus.mus`, 32,950,016 bytes,
SHA256 `3154127815784c0de9331e1b69748ff47b2e5e70de012296fedb1aace9a31dda`.
Original filename format `821B5F30` is `d:\audiostreams\%s`; the fixture uses
that drive and separator form. Original file-module selector `82323748`
treats an unprefixed pathname as a different default-provider request.
The unchanged authored descriptor is `frontend/frontend.str` entry 30,
`menu_mus.msx`, 4,766 bytes, payload SHA256
`3fa1abe6b4a8eac02d44191e42900fc672b85cbfe1619e8f128d05c8c13944e9`.
`tools/prepare_configured_mus_fixture.py` performs bounded extraction and
records the full original archive and payload identity. Its PFDx body begins
at payload offset `0x50`; original bank registration `8232E240` stores
PFDx word `+0x2C` as its track descriptor table. The sole authored table
index at `PFDx+0xDBC` is `0x370`, selecting descriptor `PFDx+0xDC0`.
Its word `+0xC` is `0x750`: 1,872 metadata bytes including all 37 EAAC headers.
The original standalone voice factory leaves its descriptor `+0x64` null.
It must receive the unchanged authored descriptor through original attachment
method `8232ED88` before whole `82334468` can request metadata through
`82334258` at `82334514`. The requested extent comes from that descriptor,
then `82335170` allocates/reads it and leaves the voice's ownership byte
`+0x158` set. Actual poll `82334328` waits for its request and restores magic
`0xBEDFACED`; readback is compared with the exact original metadata prefix.

Earlier fixture runs remain failures in `configured-mus-tests-1` through
`configured-mus-tests-6`. The first wrong source pin and later wrong filename
or missing descriptor/full-bank request are fixture defects, not valid audio
rejections. The read-only original file provider measured 32,950,016 bytes;
the oversized metadata allocation returned zero, so the actual read received
buffer zero and status `C0000005`. Its dirty-disc UI and loading-worker
render failures were downstream effects. No support was broadened from them.
`configured-mus-tests-7` ran an old executable while its replacement was still
linking and has no current-source validation credit; the corrected build and
both completed cases are recorded separately in run 8.

Original record lookup `823343F0` compares a signed numerical range against
each record's header/start units. The first record's hash word is not that
selector. Query 0 selects record 0; query 81, one above record 0's header
unit 80, advances to actual record 1. Original whole producer `82334468`
constructs selector 0 at `82334670`, with time origin 0.001 from `821DD410`,
the real file base and the selected owned header. Selector 0 synthesizes
requested start time 0 from `821DD1E8` at `82341E14`; it is not a -1 start.
The fixture observes both the original virtual call and actual queued
`82341F78` command, requiring start 0 and null optional seek metadata before
forwarding the existing original entries without changing results.

Case 0 stops long record 0 after actual `823424D8` owned-block production
and original mixer `8233FAF8` PCM use. It requires a real pending claim to
reach cancellation/release. Case 1 lets short record 1 reach all actual
block releases and frame consumption before stopping. Original voice stop
`82334778` dispatches stream selector 1 at `823347DC`, queuing worker
`82342268`, which calls `82342CC8` for active voices. The test requires
actual codec retirement and expiration of its native generation lease.

Original destructor `82333EC8` then frees the owned metadata through
`82333F90`, enqueues reader-group retirement `8233D980` at `82333FC0`, and
runs the base owner's group cleanup. Passing requires zero reader groups,
managers, claims and operations, an observed metadata free, actual original
stream-service shutdown `8232E068`, and root `82338FA0` joining the real
Dac worker with exit code zero. A malformed producer caller is rejected
before ownership changes, then the same real call proceeds unchanged.
The fixture never supplies replacement compressed bytes, EOF, thread state,
messages, allocations or successful return values.

The early-stop process consumed 1,794 PCM frames and observed 20 actual
producer blocks and 20 cancelled-claim releases. The natural-completion
process consumed all 88,803 authored frames and observed all 18 block releases
with zero cancellations. Each required exactly one owned metadata free and
one deferred reader retirement, an expired native codec generation lease,
empty reader ownership, cleared stream services, and original Dac worker exit
code zero before root storage release. This proves the scoped audio lifetime;
the graphics owners and unrelated startup workers still end at runtime
shutdown and are not credited as original gameplay cleanup successes.

## Recorded verification

The final coordinated native run passed all 48 focused tests, including the
aggregate producer lifecycle and all 14 independent reader cases. See
`build/restrictive-audit/focused-final-tests.log`. The threshold cases verify
actual original behavior, including token cancellation and worker frees.

No game or launcher is started by this audit. Audio CPU tests start the
existing muted audio fixture and its worker. Independent producer cases do
not prove gameplay reachability, every authored duration, seek, cancellation,
deaths/checkpoints, or all mission routes.
