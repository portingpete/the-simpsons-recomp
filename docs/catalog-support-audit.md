# Mission asset support audit

`tools/audit_mission_asset_support.py` joins the complete original asset census,
global effect/pass admission report, direct particle SDK selections, texture
metadata audit, and three sound catalogs. Every record has independent implementation, test, lifecycle-test,
malformed-input-test, and gameplay-encounter fields. All source reports are
hashed; a changed shader selector, decoder, or texture owner requires a fresh
audit before the join accepts it.

The baseline census contains **490 STR archives in 20 asset groups**. Those
groups are the original first directory in each archive path. They describe
packaging, rather than a proved dependency list for a runtime mission. Shared
character assets remain `simpsons_chars`; global effects and streamed audio
remain `unattributed_global` unless explicit runtime evidence supplies more
information.

The newer frozen 497-test joins are separate from the preserved v3/v4 scopes
documented below. Their exact build and closed-route authorities are described
in the [497 scope addendum](#frozen-497-tests-and-closed-native-routes).
The later [500-test submesh repair](#separate-500-test-submesh-repair-scope)
passes in both configurations, with three new synthetic original lifetimes.
It has no new catalog join or gameplay scope; the frozen 497 matrices and
their exact counts remain unchanged.

| Population | Complete inventory | Current static support evidence | Remaining gap |
|---|---:|---|---|
| Registered effects | 49 effects, 110 technique/pass combinations | 47 passes have both required native artifacts; 45 have a source-declared selector, including mono alpha immediate handling | 63 passes lack one or more artifacts; source declarations do not grant native lifecycle credit |
| Direct particle SDK passes | Eight selections: two VS by four PS | All original selections have explicit native programs/contracts | Kept distinct from registered `particles` FX; no gameplay or lifetime credit inferred |
| Immutable material identities | 256 | 108 identities have native shader artifacts | A shader artifact alone does not establish its input, state, or lifetime contract |
| VFX resources | 8,770 occurrences, 3,447 unique payloads | Exact occurrence plus original row counts,16 module keys, revisions and flags recovered | Module-specific emitter layouts and effect/material associations remain unqualified |
| Mesh resources | 5,533 occurrences | Exact occurrence plus25,990 native geometry records / 29,319 submeshes; declaration, stride, index and raw material fields recovered | Actual material/pass selection, consumed fetch validity and native resource lifetimes remain unproved per packaged occurrence |
| Texture dictionaries | 863 occurrences, 709 unique payload labels | All original compressed metadata prefixes parse independently | Pixel decoding, GPU use, and release are separate tests |
| Textures | 7,318 occurrences, 5,882 unique metadata records | All five shipped formats pass current metadata admission | Admission does not prove that every texture is decoded, drawn, or released |
| Resident sound | 156 SBK occurrences, 45,382 cue occurrences | Every cue retains bank identity, header offset, channel/rate/loop and ordered block identities; zero current format/storage violations | Factory/ring/cancel/seek/reload lifetimes require original caller tests |
| Ambient AMX sound | 8 unique payloads, 254 unique cues; 58 resource and 1,999 cue occurrences | Every occurrence reconciles to its original archive and payload; zero current format/storage violations | A deduplicated pure decoder test is separate from each live owner |
| Streamed sound | 7,430 files, 9,470 streams | Every source reconciles to the original manifest; stream offsets and framing retained | SNU/MUS filenames do not prove mission use; native scheduling is separate |

BNK chunks are not counted as sound banks solely because their type name
contains “bank.” The original audio catalog identifies resident SBK and AMX
resources explicitly. This prevents the 1,213 unrelated BNK chunks from
inflating the sound population.

## Live opening-route encounters (final executable)

The [live join](../build/restrictive-check-audit/stage6-live-encounters-v1-20261003.json)
hashes the 18 stage logs of `build/restrictive-audit/stage-sweep-stage6-20261003`
(frozen executable `79b519a5…a1504e`, 20 delivered commands per stage) and joins them to
the 110-pass catalog. **27 passes were reflected-selected live, every one admitted**
(skin and skin alpha, skin textured/dual/gloss, rigid, textured, gloss, dual, UV,
multitone, normalmap, sky, flipbook, chocolate, each with its alpha pair where reached,
and mono `TechniqueOpaque`). The previous catalog recorded zero observed passes, so
this is the first encountered-during-gameplay credit; it proves reachability on the
opening route only. The other 83 passes were not reached, and most still lack native
artifacts, so absence here is not evidence about gameplay support.


## Original producer evidence

The effect inventory is anchored to the original flat image SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`
and the two original registration tables. Original `82701B70` passes the 25
rows at `82CEFD20` to registrar `827019E8` at callsite `82701B8C`.
Original `823C7080` passes the 24 rows at `82CD1448` to that registrar at
callsite `823C7098`. The complete envelopes, shader records and pass
associations are pinned by `audit_effect_admission.py`; they establish the
original population independently of the implemented source whitelist.

Only two remaining missing-artifact passes belong to the game-specific second
table: `simpsons_aa_row` (`82031980` / `82031AD0`) and `simpsons_aa_col`
(`82033010` / `82033160`). The other 61 belong to the first table, including
17 `quad` passes and six `shadows` passes. A rejection in any one pass must
not be treated as a reason to skip the remaining original combinations.

The direct particle SDK producer is a distinct original **two-VS by four-PS**
selection matrix, fully recorded by the effect audit. Its implemented support
does not establish support for the separate registered `particles` effect or
for opaque VFX emitters that may reference it.

The texture audit opens original STR files read-only, checks every complete
packaged file SHA256 against `analysis/assets.json`, and expands only through
the linked dictionary metadata. Metadata fields are compared with current
`renderer/itxd_blocks.cpp` admission. The original runtime copied-header,
allocator, relocation, group lookup, and reference ownership path is separate
in `runtime/engine_itxd_textures.cpp`; this offline audit does not call it.

The accompanying geometry producer audit replaces skin's captured 48/56-byte
stride restriction and sky's captured 28-byte layout with nonzero aligned
strides through 1,020 bytes and semantic/index-driven offset association.
Original `8243C6A8..B4` encodes DWORD stride in one byte; original
`8245EE64..94` associates declaration semantics. Exact declaration families,
format lists remain implemented restrictions in the geometry consumers. The
separate base-vertex audit establishes that the original producer transports
the signed base independently and splits index counts; native source now
checks selected effective indices before a draw. Fresh native receipts are
required to credit these cases. These changes do **not** decode the
5,533 packaged mesh resources. The separate packaged audit now recovers their
serialized declaration/stride fields; native material selection and complete
resource lifetime coverage remain independent.
See `docs/restrictive-geometry-audit.md` for original producer pins and whole
original setup/draw/retire regression inputs.

## Generate and consume the matrix

```powershell
python -B tools/audit_effect_admission.py --json build/restrictive-check-audit/effect-admission.json
python -B tools/audit_texture_runtime_coverage.py --output build/restrictive-check-audit/texture-metadata.json
python -B tools/audit_mission_asset_support.py --effect-report build/restrictive-check-audit/effect-admission.json --texture-report build/restrictive-check-audit/texture-metadata.json
python -B -m unittest discover -s tests -p test_mission_asset_support_audit.py
```

The output is `build/restrictive-check-audit/mission-support.json`. It contains
79,667 original occurrence/selection records and per-group counts: 79,659
original resource/cue/registered-pass records plus eight direct SDK selections.
Synthesized geometry regression records are counted separately. Use optional repeated
`--encounter-log <path>` and `--test-evidence <path>` arguments to attach
explicit runtime evidence. A report with no supplied evidence has no proved
gameplay encounters or lifecycle-tested combinations, even when source tests
or fixtures exist elsewhere.

Encounter JSONL accepts schema 1 encounter, failure, release, shutdown and lifecycle events
with `kind`, `asset`, `caller`,
`parameters`, `ownership`, `mission`, `last_action`, and `sequence`. Parameters
may be a JSON object or an escaped `key=value` string. Caller retains the raw
uint32 original return address or a source-supplied string. Logs retain malformed
lines with their line number and continue processing later events; the tool
returns a failing status when such evidence is incomplete.

`encountered_runtime` retains an exact runtime match independently of mission
attribution. An empty, unknown, unattributed, or fixture mission does not set
`encountered_gameplay`; this keeps unassigned setup/test events separate.

`kind=effect_pass`, `asset=source:82006348` proves an encountered source only.
An exact shader pair proves a pass only when it selects one unique registered
pass. A pair reused by multiple techniques also needs original technique/pass
handles. A pointer or a scene source cannot prove a packaged mesh/VFX payload.
Payload hashes shared across occurrences need archive and entry provenance;
duplicates within one entry additionally need the original decoded offset.
The stock census includes one such duplicate VFX payload, so name plus hash
is insufficient even inside a single archive entry.

Native `audio-source` receipts preserve cached resident bank path, embedded
SBK name, original cue offset and exact EAAC header. A unique match proves that
resident occurrence; optional cached payload hash/archive/entry further constrain
it. AMX payload name/offset/header identifies the actual native cue, but duplicate
packaged occurrences remain a candidate set without archive/entry. A streamed
EAAC header always remains a candidate set: even one matching header cannot
prove its complete encoded chain or original source file. Unregistered owner
receipts do not receive catalog credit.

Explicit shutdown receipts preserve the last thread context without granting
an asset encounter or rejection. Historic `Native window closed` and `Native
runtime shutdown` failure receipts remain raw evidence with separate reason
counts; their retained resource context does not establish asset admission
causality. Screen, blend and other failures also retain that qualification when
the failing boundary has no independently captured asset identity.

A test-evidence file has `schema: 1` and a `cases` array. Each passing case
names its exact `catalog_id`, `result: "passed"`, executed `phases`, and a
`proof` object with workspace-relative `path` and complete file `sha256`.
Only a single explicit `scope: "draw"` case containing **create, use, release**
marks the combination lifecycle-tested; `malformed` additionally marks malformed-input
rejection evidence. Separate partial tests are not silently combined into
one complete lifetime. A changed proof, pinned build input or unknown catalog identity fails
the join.

`scope: "setup"` cases use `create`, `query`, `metadata_use`, and `release` to
set `setup_tested`. They preserve the fact that registration, selected-pass
metadata use and retirement succeeded without claiming a rendered lifetime.

`tests/test_independent_effect_setup.cpp` accepts the original image and one
combined row ordinal 0..48. Each invocation reaches the original graphics
startup boundary, constructs the original manager, populates the real shared
pool through the original first producer (`littextured`) and retires its seed
wrapper, then registers the canonical target row. The original null-callback
`particles` row requires its canonical `littextured` predecessor within the
same two-row invocation: original `827019E8` initializes `r29` to zero, the
null-callback branch at `82701AD8` skips replacement of that register, and
`82701B20..34` unconditionally retains its previous typed owner. Starting at
`particles` alone is invalid original producer input. Its case preserves this
real adjacency and the matching original two-row cleanup.

The fixture exercises original
queries, copied caches, the typed virtual finalizer when present, every
technique's private/shared metadata bindings, malformed copied input and stale
ID rejection, and original target/manager retirement. The shadow case keeps
the already known four retained camera rasters explicit.

Run all rows independently and attach setup receipts after the native target
has been built:

```powershell
python -B tools/run_independent_effect_setup.py --fixture build/native/IndependentEffectSetupTests.exe --output build/restrictive-check-audit/effect-setup
python -B tools/export_effect_setup_evidence.py --matrix build/restrictive-check-audit/mission-support.json --logs build/restrictive-check-audit/effect-setup --output build/restrictive-check-audit/effect-setup-evidence.json
python -B tools/audit_mission_asset_support.py --effect-report build/restrictive-check-audit/effect-admission.json --texture-report build/restrictive-check-audit/texture-metadata.json --test-evidence build/restrictive-check-audit/effect-setup-evidence.json
```

The runner starts the setup test executable once per row, records complete
stdout/stderr plus the exit result, and continues after failures or bounded
timeouts. Its manifest pins the fixture executable, original image, test
source, and every per-case log. The exporter requires a verified zero exit
result and exact row/source/name/technique-count markers; missing or rejected
rows remain gaps and do not hide later successes.

The corrected run at `build/restrictive-check-audit/effect-setup-corrected`
passes all **49 independent processes**, producing **110 setup-only pass
receipts**. The earlier `effect-setup` logs retain its 48 successes and the
invalid standalone particles invocation. The corrected original adjacency
does not widen a runtime guard.

The padded draw receipt exporter consumes native CTest/JUnit output for base,
textured and dual skin plus sky. Use an untruncated passed-output limit so the
native lifetime and PASS markers survive capture:

```powershell
ctest --test-dir build/native -R "OriginalSkin(textured|base|dual)PaddedPass|OriginalSkyPaddedPass" --test-output-size-passed 200000 --output-junit K:/SimpsonsNativeCopy/build/restrictive-check-audit/geometry-native-tests-complete.xml --output-on-failure
python -B tools/export_geometry_draw_evidence.py --matrix build/restrictive-check-audit/mission-support.json --junit build/restrictive-check-audit/geometry-native-tests-complete.xml --output build/restrictive-check-audit/geometry-draw-evidence.json
```

All four completed cases provide **eight exact registered-pass CPU-owner
lifecycle receipts** and **four synthesized declaration-owner lifecycles**.
They construct original FX/declarations, check opaque/alpha output pixels,
release through original calls, and reject stale owners. Their explicit native
marker preserves `backend_mesh_cache=owner_resident` and
`full_gpu_retirement=unproven`. This qualifies their original logical/CPU owner
lifetime evidence; it does not claim complete GPU teardown or link synthesized
vertices to any packaged mesh. The receipt exporter pins fixture/helper,
executable, image, dictionary and CTest configuration; rebuilding requires
fresh native receipts.

Repeated failure groups use kind, exact asset identity, original caller,
parameters, mission, last action, and ownership state. Sequence and log line
are retained as occurrences but excluded from the grouping key. This keeps
repeated occurrences together while distinguishing failures introduced by
checkpoint reload, death, cutscene skip, or mission exit.

## Independent admission and regression coverage

The texture audit now records each rejected dictionary and original source
failure, including archive, entry, name and payload identity, then proceeds to
later dictionaries/files. Failed payloads never enter the successful parse
cache. The CLI returns failure after writing the complete report if any case
was rejected; a partial result cannot look complete.

Fifty-nine Python regressions cover independent dictionary rejection, failed-cache
retry, duplicate original resource identity, malformed telemetry recovery,
source-versus-pass encounters, shared shader/payload ambiguity, failure
grouping, cached audio identities, terminal context, synthesized geometry,
source-expression recovery, setup-versus-draw scope, completed independent process outcomes,
and complete-lifecycle/build-input proof identity. The nine effect
admission regressions also pass. These verify the audit machinery; they do
not replace original native create/use/release regressions or gameplay routes.

## Shader producer and texture restriction triage

`tools/audit_shader_producer_ranges.py` pins the complete original mono world,
immediate-loop, recording-build, scene-dispatch and public-wrapper spans,
checks each selector instruction, and reuses the existing shader program,
literal and constant-map verifier. The mono callback saves incoming `r7` at
`8273A894`, masks its low byte at `8273A8A0`, and chooses typed `+A8` alpha at
`8273A92C` or `+AC` opaque at `8273A934` before original `826B6078` activation.
The valid selection predicate is the low byte, so 255/257/FFFFFFFF select
alpha while 256 selects opaque. The earlier native `beginMono` required the
entire saved flag to be zero and always selected the first technique.

This is an original-valid **public-path support gap**, with an important
caller limit. The current Burp route `8273B4D0 ->82740680` mode2 supplies
literal zero at `82740B44`; alpha is unreachable through that route. The
original immediate caller `827400F8` and recording caller `82740420` preserve
their incoming flag and call the world callback at `82740130/82740478`.
The independent `MonoImmediatePassTests` alpha and opaque cases reproduced
the earlier rejection through the whole public wrapper and dispatcher:
`8273B4D0 ->82740680`, caller `8273B4E0`, flags zero, live mono typed owner
in packet `+18`. Both stopped at the dispatcher whitelist before the world
callback; raw independent reports remain under
`build/restrictive-check-audit/mono-immediate-frontier`. The fixture uses an
original position-only declaration whose allocator extent derives from
`0x38 +12 *element_count`, rather than a two-element size assumption.

The next independent alpha and opaque executions advanced through actual
world callback and material commit, then both rejected `reflection: descriptor
handle` after 152 checks. Their raw JUnit remains
`build/restrictive-audit/producer-ranges-frontier-tests.xml`. The original
shared-pool query `826F3258` tests eight bitmap lanes and does not look up a
local parameter descriptor. The three actual shared handles `001C000D`,
`0020000F` and `00240011` address leaves outside this effect's four local
descriptors, with valid zero usage. The source repair bounds every bitmap
word by the serialized owner and preserves the general descriptor API's
validation. It awaits another independent whole-original alpha/opaque
native execution.

The next native run reached both immediate draws and then failed the
independent white-export pixels. Logs showed the original callback's correct
identity matrix staged first, followed by a native material commit of zero
shared defaults. Original `8270A7A0` writes the combined matrix directly to
the staging bank at `8270AB14/8270AB44`. Empty material `826B54D0` skips all
setters and tail-calls `826B2F20`; the SDK commit uploads shared constants
only where dirty bits and selected-pass usage intersect (`82C1E5EC..5FC`).
The native repair preserves that verified staging during the empty material
commit. The independent recordings stopped earlier because original startup
already owned the genuine singleton; their fixture now adopts and validates
that owner, constructing through the original allocator/caller only when it
is absent. Raw failed JUnit remains
`build/restrictive-audit/index-mono-frontier-tests.xml`. These latest repairs
are followed by fresh native evidence below.

Both immediate alpha and opaque cases subsequently passed **530 checks each**
in `build/restrictive-audit/mono-matrix-configured-tests.xml`, including real
white-export pixels, original declaration and all 49 FX/CPU-cache retirement,
malformed handles/cache and stale-owner rejection. Exact pass and synthetic
case receipts are preserved in
`build/restrictive-check-audit/mono-immediate-matrix-evidence.json`. That focused
export remains `complete=false` because its JUnit omits 14 unrelated default
cases; those omitted tests receive no credit. This historical source/executable
receipt does not grant proof for a later rebuild. Full GPU upload-cache
retirement remains separate and unproved for these original CPU owners.

The same run reached the genuine recording builder for both variants and
stopped at its required scissor snapshot. The independent recording fixture
now runs both complete original empty shadow parents `82707220`, whose actual
camera path sets the rectangle through `8243C430` at `827072A4` and retains it
after restoring camera/state. No native rectangle is fabricated and no
recording validation guard is relaxed. This genuine path passed the scissor
frontier in the following independent runs. Both recording variants then
rejected at 155 checks in
`build/restrictive-audit/recording-state-tests.xml`: the real deferred caller
`82740624` supplied halfpixel 1, MSAA request 1 and mask `0000FFFF`, with
single-sample backing, scissor disabled, primitive reset enabled and color
mask 15. The native guard required `FFFFFFFF`. The narrow source repair
admits the original equivalent full low-16 mask; other partial masks,
halfpixel values and MSAA requests remain guarded. Its independent native
whole-recording and backend pixel/lifetime rerun followed in later runs. The
failed receipts grant no recording use or release credit.

Source continuations now cover immediate and deferred world/cache, original
callback/staging, dead shared-shadow usage, static submesh material, draw or
recording, and original cleanup. Complete recording fixtures execute those whole
callers with a real registered owner, selected-row cache, pixels, cache reuse,
reset and original release. Changing Burp's literal zero in an observer would
fabricate an input. Shader instruction equivalence and the existing 49 setup
cases do not establish this draw lifecycle. The immediate receipts above
remain bound to their historical source and executable. Recording draw
and original recording lifecycle credit require a successful native receipt.

The new recording frontier fixtures enter the genuine public wrapper and
dispatcher with metadata `+8` equal to 0 or 4, recording enabled at the actual
byte `82CF0BE8`, and the real packet eligibility byte. They retain the original
`82740420` caller `82740A60`; no test invokes a fabricated world callback or
changes its flag. In this qualified static producer, metadata alpha bits 0/1
make original `8274090C` clear the packet's eligibility byte and choose the
immediate route. Thus the callback's full-DWORD selection domain does not
prove this caller produces an alpha recording. Default and bit2 recording
cases first produced separate failing frontiers: default reached the
recording-build packet guard, while bit2 rejected the original recording
entry qualification. Their preserved JUnit is
`build/restrictive-audit/mono-recording-frontier-tests.xml`; neither provides
use or release evidence.

The next complete default and bit2 fixtures retain that genuine dispatcher
branch, then require original `82701448` deferred material/mesh use, published
cache and repeated `827402F0` replay with independent moved-matrix pixels.
Retirement enters the actual registered atomic plugin walker `823FB3A8`
with registry `82CD1678`, reaches original `827374B0`, and exercises the
original deleting recording-manager wrapper, declaration and paired FX/CPU
cache releases. Malformed selected handles/cache pointers and stale payloads
must still fail. Original manager destructor `826F3908` does not clear its
dead recording-history fields; that producer fact is recorded separately
from active retained payload ownership. Original successful finish
`826F501C` increments owner `+50`, while completed-payload deletion
`826F4BE8` updates byte accounting and LRU links without resetting that
count or the saved history. The producer analyzer now pins these complete
original paths, payload unlink/pool return and destruction with 17 hashed
spans and 37 exact instructions. Its source-only report is
`build/restrictive-check-audit/shader-retirement-proof-source-pending.json`.
Both variants then reproduced the actual manager-retirement rejection at
194 checks in `build/restrictive-audit/storage-mono-audit-tests.xml`, after
use, repeated replay and actual original cache/payload release. The original
state had 2,000 free slots, zero allocated slots, zero LRU links/bytes and
success count 1; the exact saved last-record history remained nonzero. The
native destructor's `emptyFields` check rejected that valid retired state.
The source repair now validates the complete empty ownership graph,
accounting/count, absence of active operations and unchanged saved history,
then runs the same original destructor without clearing those guest fields.
Its fixture independently rejects deletion during the real active session
and with a live cached payload, each corrupted history/count/accounting/pool
field, and stale payload/manager identities. It verifies unchanged CPU
owner/pool bytes and retained native ownership for every rejection. These
malformed history and pool ownership checks passed in the next independent
run. Both variants then reached a later alias rejection at 262 checks in
`build/restrictive-audit/retired-graph-storage-observer-tests.xml`: the live
application context remained `00900001`, while the mesh alias had legitimately
advanced from the constructor's null snapshot to that same device context.
Whole original dispatcher `82740680` invokes setter `826FF6D8` at `8274069C`;
the original nonzero store `826FF6F0` publishes `82D63028` independently of a
recording session. The source repair observes the completed store at
`826FF6F4`, validates the exact caller/frame, live device identity and idle
manager, and updates only the host's expected aliases. The fixture relies on
that actual original setter, validates its argument/result, and adds invalid
caller/frame/flags/foreign-space and foreign/reverted-alias negatives.
The final shader producer report pins 18 whole spans and 39 instructions at
`build/restrictive-check-audit/final-source-alias-chunk-shader-producers-20261002.json`.
Both `OriginalMonoRecording_default` and `OriginalMonoRecording_bit2` then
passed in `build/restrictive-audit/idle-alias-strip-focused-native-tests.xml`.
Both Release variants also passed **716 checks each**, with their complete
markers preserved in
`build/restrictive-audit/idle-alias-strip-focused-release-LastTest.log`
(SHA256 `9acd61129a1ff90e5ff6902781b8ed09685e78dbd2c4c9e83e14d91346cea03b`).
Their fixture completes the original public dispatcher, material/mesh use,
cached repeated replay with inherited-matrix pixels, original registered
plugin payload retirement, the same original manager destructor, context,
declaration and all 49 FX/CPU-cache releases. The pass includes active-session,
live-payload, retained-history, corrupt graph, invalid idle publication,
foreign/reverted aliases and stale-owner negatives. This focused JUnit
truncates successful output at 1,024 bytes, so its lifecycle markers cannot
yet be exported into the matrix; a fresh untruncated receipt is required.
The focused suite has three unrelated geometry/backend failures still under
investigation. Prepared source or a frontier marker cannot stand in for any
completed native step, and full original GPU upload-cache retirement remains
unproved.

`tools/export_extended_draw_evidence.py` exports exact pass and CPU owner
receipts for the independent skin/rigid/sky inherited blend, sky signed or
selected base-offset, and static tangent cases. Native PASS, full lifetime
markers and unchanged source/build/input identities are all required. The
tangent fixture credits only source `820C0550`, technique `0007FFFC`,
VS `820C2FA0` with no pixel shader. Backend VB/IB destructor proofs are a
separate lifetime scope. Its optional `--include-mono` and
`--include-mono-recording` require the independent immediate and recording
success markers respectively, including the exact selected shader pair.
Recording receipts additionally require replay, inherited-matrix pixels,
payload and recording-context retirement. Rejected historical cases cannot
receive receipts. The exporter pins mono owner/backend/HLSL sources along with
fixture, executable, CTest configuration and JUnit identity, and rejects
historical receipts after newer source or build-input changes.

The current original Z-prepass route loads `+AC` unconditionally at
`827406BC`; its unused alpha artifact is a catalog candidate, not evidence
that this caller rejects an originally requested alpha pass. Four-tap blend
artifacts likewise do not establish an executed original draw selector.
The 63 missing-artifact registered passes remain a queue of independent
original combinations, grouped by their exact effect and shader identities.

Every one of the 863 packaged texture dictionaries ran independently through
metadata admission: all 7,318 records and five original formats were accepted.
This rules out a rejection in that shipped metadata census, while complete
pixel decoding and create/use/release remain unproved catalog-wide. L8, BC
and RGBA decoder dimension/mip limits are qualified implementation profiles;
neither shipped observations nor the native hardware's larger dimension cap
establishes the original producer's full valid format/size range. Cached
name/size/six-descriptor telemetry is joined as a profile candidate set only;
it cannot identify a packaged occurrence without full metadata/payload
identity and original container provenance.

`tools/audit_texture_producer_ranges.py` now pins ten separate original spans
and 23 instructions for requirements, descriptor construction/readback and
the copied ITXD loader. The device caps copier `8244E5E0` copies the immutable
`8206AA30` record; fields `+58/+5C` contain 8192. Ordinary compatible type 3
2D requirements at `82B7F950` clamp dimensions to those caps. The actual
texture caps word `0001EC45` does not require a power-of-two size. Accepted
BC formats undergo a separate multiple of 4 normalization. Type 17 volume textures
instead clamp width/height to 2048 and depth to 1024. These are original SDK
normalization scopes, not evidence that every encoded format is valid.

The SDK constructor `8243F928` and reader `8243DED8` transport thirteen-bit
2D dimensions; level-count reader `8243FE90` transports a four-bit count
minus one. Field representability is separate from compatibility, pixel
storage and lifetimes. Normal copied ITXD setup does not run the SDK
requirements helper: `826F26B0 ->826F24D8 ->82736F58` copies the serialized
N-byte extent, then `82C20E80` relocates address fields. Consequently that
walker accepting metadata does not establish malformed descriptor validity.

Five pinned texture queue entries preserve the current L8/BC 2048 and
RGBA 1024 caps, minima/power-of-two constraints, BC pitch limit, packed-tail
constraints, palette-name 64x64 branch, and five-format/zero-auxiliary
restriction. No original offline ITXD serializer was located in the pinned
executable. A genuine larger/non-power-of-two descriptor, added format,
padded pitch or auxiliary-header output must still be traced to that producer
and tested through original copy/bind/sample/unbind/final allocator release
before these guards are relaxed. All 7318 shipped metadata records currently
fit the admitted profiles; their observed 4..2048 height and 16..2048 width
envelopes are not full producer bounds.

```powershell
python -B tools/audit_shader_producer_ranges.py --effect-report build/restrictive-check-audit/effect-admission.json --texture-report build/restrictive-check-audit/texture-metadata.json
python -B tools/audit_texture_producer_ranges.py --texture-report build/restrictive-check-audit/texture-metadata.json
python -B tools/export_stage_route_evidence.py --summary build/restrictive-audit/stage-sweep-20261002/summary.json --output build/restrictive-check-audit/stage-sweep-route-evidence.json
```

The first 18-stage route sweep proves all 18 original maps initialized and
preserves 33 authoritative native frame/raw readback pairs, 319 independently
verified input deliveries and three substantive failures. Fifteen runs
reported success. Captured player positions differ in two stages. These are
bounded opening routes: they do not prove ability hits, enemy attacks,
consumed pickups, destroyed objects, dialogue audibility, checkpoint reload,
death/respawn, later cutscene playback/skipping or mission exits. Only strict
`native-frame-<digits>.json` metadata with the exact raw renderer readback is
accepted; derived preview JSON and recording diagnostics are excluded.
Historical launch hashes identify the tested binary. Later rebuilds or route
repairs do not retroactively turn this sweep into evidence for a newer build.
Map lifecycle and terminal shutdown events preserve context separately and
never grant asset encounter or rejection coverage.

## Persisted restrictive source census and queue

`tools/audit_restrictive_checks.py` scans every C/C++ and HLSL source under
runtime and renderer, records complete balanced named-check expressions and
exact keyword-hit lines, and hashes every source. Comments and quoted/raw text
remain keyword evidence but cannot masquerade as executed check calls. Every
found candidate explicitly remains `unestablished_by_this_scan`; a numeric
guard may be required malformed-input validation.

```powershell
python -B tools/audit_restrictive_checks.py --effect-report build/restrictive-check-audit/effect-admission.json --producer-report build/restrictive-check-audit/shader-producer-ranges.json --texture-producer-report build/restrictive-check-audit/texture-producer-ranges.json
python -B tools/summarize_resource_audit.py --run-directory build/restrictive-audit/stage-sweep-20261002 --output build/restrictive-check-audit/stage-sweep-encounter-groups.json
```

The initial census records **757 balanced guard candidates and 1,048 keyword
lines in 272 source files**. Counts are snapshots and must be regenerated after
source changes. Its separate producer findings link exact hashed document
lines for the original dynamic ring range, representable geometry stride,
and producer-literal reflection/shadow sizes. Its actionable queue retains
the original nonzero audio seek/config path, native base-vertex/vertex-count
limits, all 63 missing shader combinations and opaque mesh/VFX semantics.
Related lexical candidates are associations, not per-guard validity proofs.

The later mono recording source snapshot
`build/restrictive-check-audit/source-census-mono-recording-source-pending.json`
contains 773 balanced candidates and 1,080 keyword lines in the same 272 files.
Its producer report adds shared-pool usage, inherited matrix staging, genuine
recording selection and registered plugin/manager retirement pins. This is a
source snapshot with outstanding native frontiers, not additional passed
asset or draw coverage.

The frozen alias/chunk source snapshot is
`build/restrictive-check-audit/final-source-alias-chunk-census-20261002.json`:
**773 guard candidates, 1,089 keyword lines and 273 source files**. Its paired
`final-source-alias-chunk-effects-20261002.json` and
`final-source-alias-chunk-textures-20261002.json` retain 49 effects/110 passes,
47 passes with native artifacts, 63 missing pass artifacts, and 863 texture
dictionaries/7,318 metadata-admitted textures. The source-only base matrix
`final-source-alias-chunk-base-matrix-20261002.json` contains 79,667 original
records across 490 archives and 20 packaging groups, with zero test or
encounter credit until independent receipts and actual logs are joined.
These source report names identify snapshots; later native results are
recorded separately.

The frozen prevalidation/capability/thread source snapshot uses the prefix
`build/restrictive-check-audit/final-source-prevalidation-capability-thread-`.
Its `effects-20261002.json`, `textures-20261002.json`,
`shader-producers-20261002.json`, `texture-producers-20261002.json` and
`census-v2-20261002.json` retain the current source hashes. The census records
**773 balanced guard candidates, 1,114 keyword lines and 274 source files**;
all mechanical candidates remain untriaged. The source-only
`base-matrix-v2-20261002.json` contains the same **79,667 original records**
across 490 archives/20 packaging groups, now with source-proven packaged
fields joined for every 5,533 mesh and8,770 VFX occurrence. It grants zero test
or encounter credit. The accompanying `audio-20261002.json` checks current
catalog format/storage bounds; this invocation does not rehash original
streamed files or prove decoder/playback execution.

The decal count candidate illustrates required producer triage. Original
singleton `827502A0 ->82767340` owns fixed `0xF2560` storage with 800 quad and
200 cached nodes; original `82767708/827677D8` acquire or reuse those nodes,
and gameplay insertion `82769490` does not allocate/grow them. The native
65,536 cap cannot reject this proven singleton range. Generic caller-owned
insertion `827649E0/82764A10` has no count cutoff, but its broader owner and
lifetime range remains unproved. No widening or new rejection is claimed.

The encounter summarizer streams logs into exact raw asset/caller/parameters/
ownership/mission/action signature groups, keeping the full first receipt,
per-log line spans, occurrence counts and every failure reason. Source logs
remain the authority for all individual instances and sequences. Per-field
cardinality exposes pointer/state noise for producer-side review; it never
silently deletes fields from failure grouping. A logger sequence such as
416,993 counts observed boundaries, whereas that Tree Hugger failure occurred
at written log line 1,563 after native deduplication. Sequence is not a written
row or distinct-combination count.

## Rejection dispositions and observation limits

The final census gives every mechanically found guard and keyword line the
disposition `mechanical_candidate_untriaged`. Domain proximity to a producer
finding never upgrades that guard to a proved cap or valid-input rejection.
Its separately pinned findings and queue distinguish these dispositions:

| Disposition | Established scope | Required follow-up |
|---|---|---|
| Proven valid producer rejection | Historical ring whitelist, represented skin/sky strides, independent signed base/count transport, whole mono public paths, capability-dependent pipeline initialization, 65 signed-positive matrix groups and 65,536 immediate skin rows rejected original-valid inputs | Keep each repair's independent create/use/release and malformed receipts under its own scope; historical failures remain preserved |
| Proven producer cap | Original ring arithmetic/alignment, encoded stride ceiling, named reflection/shadow literals; ordinary SDK type-3 texture cap is a separate scope | Retain validation outside that explicit producer scope; a SDK cap does not qualify copied ITXD serialization |
| Unqualified offline domain | Additional ITXD formats, dimensions, pitches, mip tails, borrowed backing and auxiliary controls; mesh material/pass associations and VFX module layouts beyond recovered prefixes | Locate the original serializer/consumer or genuine authored output before widening or joining those fields |
| Untested create/use/release path | Missing native shader contracts, packaged resource occurrences without complete owner/use evidence, authored nonzero audio seek metadata | Execute each independent original path with actual use, original retirement and malformed/stale cases; setup or decoder success alone gives no lifecycle credit |

For each emitted schema-1 encounter, `ResourceAudit` preserves kind, asset,
original caller, parameters, ownership, mission and last non-neutral observed
controller action before deduplication. Owner pointers, generations and frames
are represented in `instance`; the exact stable fields above form the group.
The source-only grouping review below identifies two remaining producer
counter/register exceptions rather than silently normalizing them. A failure
reuses the latest observation on that thread and preserves `scene_context`.
That is retained context rather than proof that the latest texture or sound
caused the exception. Lifecycle and shutdown rows remain separate observations.

| Boundary | Position relative to validation | Identity/coverage limitation |
|---|---|---|
| Scene dispatch (`engine_driver.cpp`, `observeSceneInput`) | Before dispatcher frame/source/flag admission; fields are read independently with unreadable counts | Cached FX source plus typed handles/stride is source evidence; it does not identify a packaged mesh/VFX or exact selected shader pair |
| Effect producer entry (`engine_effects.cpp`, `observeProducerEntry`) | First operation in edge, mono, rigid, skin, VFX rigid, Z-prepass and shadow begin, before their frame/owner/camera/requested-technique checks | Cached source and selected shader metadata are labeled unvalidated candidates in kind `effect_producer_entry`; unreadable fields carry an explicit mask. They grant no source/pass/catalog encounter credit |
| Mono/rigid/skin selected pass (`engine_effects.cpp`) | Before selected-pass metadata/cache handling, after earlier frame, owner, camera and requested-technique checks | Exact VS/PS/technique/pass may join a pass; the separate entry observation retains earlier rejected parameters |
| ITXD source and material binding (`engine_itxd_textures.cpp`, `engine_driver.cpp`) | Source snapshot precedes local metadata/span checks; binding snapshot precedes caller/driver admission | Cached name/bytes/descriptor is a profile, not complete metadata/payload hash or packaged occurrence; some load-envelope checks precede the source snapshot |
| Reader create/claim/reset/release (`engine_audio_reader.cpp`, `auditBoundary`) | Before guest reads and local admission for the explicit reader boundary list | Raw requested ring/count/entries and cached ownership are retained; unlisted reader/API entries lack their own boundary snapshot |
| Audio source (`engine_audio_owners.cpp`, `auditProducer`) | Before frame/member/source admission | Resident cached archive/entry/payload hash can identify an occurrence; AMX hash may identify several packaged copies, and stream header alone is a candidate set |
| Skin geometry use (`engine_effects.cpp`, `observeSkinDraw`) | Before texture/state/backend draw checks, after geometry/material decoding | Exact draw state and FX shaders are retained; this hook covers skin and does not supply original serialized geometry identity or snapshots for every other family |
| Screen replacement and radial (`engine_effects.cpp`, `engine_driver.cpp`) | Before their local cache/phase/declaration/state guards | These explain the observed Tree frontiers, but source/declaration identity alone is not a named packaged resource |
| Map/movie owner boundaries | Actual original ready/cleanup or input-ready/stop-request boundaries | Preserve owner-route evidence; movie stop request precedes decoder completion, so these grant no asset use/release credit |

Logging is enabled only for audited runs. Source inspection found no universal
pre-validation hook at every effect registration, geometry decoder, particle
or VFX setup/release, shader selector, or render backend guard. Early failures
there can retain an older thread snapshot or remain unattributed. The central
JSON failure hook is in `threads.cpp`; exceptions outside that worker catch
must also be checked in the preserved runtime/runner log. Capture errors and
file-write fallback announce `RESOURCE AUDIT IO FAILURE`; fallback JSON is
prefixed in stderr and requires separate extraction before a JSONL join.
These are explicit logging gaps, not evidence that an unobserved resource
combination was accepted or released.

The new entry observation preserves the complete PPC context, host floating
point control/status and LastError, performs no guest stores and never throws.
Incoming owner, packet, camera and ABI addresses stay in `instance`, while
`flags_low8` comes from the actual published flags word. Candidate metadata
does not imply a validated selection. `tests/test_effect_producer_entry.cpp`
derives four rejected inputs at the genuine original mono activation: bad
owner, camera, requested technique and unreadable wrapper. It then forwards
the unchanged original invocation and requires actual pixels plus original
retirement. Its native execution belongs to the new prevalidation/capability
scope. `OriginalEffectProducerEntry` passed 568 checks in the complete
493/493 Native suite: four early failures retain owner/camera/technique/
unreadable inputs, PPC/CSR/LastError preservation and a later whole original
mono draw/retirement. This fixture qualifies those mono failures and the
shared snapshot behavior; it does not fabricate native malformed cases for
all seven hooked effect families.

The read-only packaged field audit is documented in
[`packaged-mesh-vfx-field-audit.md`](packaged-mesh-vfx-field-audit.md). It pins
the original chunk reader, native geometry pools/relocations/record visitor,
declaration/submesh consumers and VFX prepublication loader. The optional
matrix `--packaged-fields` join adds only recovered parameters by exact
archive/entry/decoded-offset/name/payload identity. Native support and every
setup/use/release/gameplay coverage bit remain independent.

The complete packaged report recovers25,990 native geometry records and
29,319 submeshes, with strides24/28/36/40/48/56, raw primitive6 and exact
declaration/owner/index/material fields. It retains 36 original-valid empty
clumps. Every selected R16 span contains literal FFFF; restart semantics
still require the original selected draw/state contract. The8,770 VFX
occurrences expose row counts,16 module keys, revisions and flags, while
module-specific stride/material/shader parameters remain null. All 14,303
occurrences were independently hash-checked with zero qualification failures.
The report SHA256 is
`13e81e770f5d96048067efcf77339fdf2fd0c56a9f20bcb7f5c7e6cfef166291`
for `packaged-mesh-vfx-source-complete-v2-20261002.json`. The v2 Python cache
checks each occurrence's decoded/payload identity even when parsed fields
are reused; a conflicting occurrence is reported without hiding later valid
ones. Its ten parser regressions and 31 matrix/three census regressions passed
on that v2 Python snapshot. The exact before/after Python-only source hashes
are retained in `packaged-cache-provenance-python-delta-20261002.json`.
Recovered serialized fields do not associate a live pointer with that named
asset, certify every selected vertex fetch, or execute an original resource
lifetime.

A separate [`particle capacity field audit`](restrictive-particle-capacity-audit.md)
recovers 9,129 `.prt` rows in 3,191 named VFX occurrences. Original signed
counts clamp and round to requested capacities8..256, with zero stock requests
above the native4,096 cap. Actual activated capacity can be smaller than the
request. This expanded field scope remains source-only and leaves the stable
v2 packaged report/base unchanged; authentic module allocation, use and
release are still required to qualify larger requests.

The preserved v3 Native matrix with exact audio lifetimes and owner-qualified
mission attribution is
[`prevalidation-capability-thread-mission-support-v3-20261002.json`](../build/restrictive-check-audit/prevalidation-capability-thread-mission-support-v3-20261002.json).
All 49 fresh setup processes passed, producing110 setup-only pass receipts.
The padded and extended exporters accept 12 and 52 receipts respectively,
including 23 synthetic regression rows. Together they prove original
create/use/release for 23 global FX selections and23 synthetic inputs; they
do not identify packaged mesh/VFX lifetimes. The complete Native JUnit has
493 passing tests. Separate Release padded/extended exports also accept the
same12/52 independently passing draw receipts. The complete Release
aggregate passes492/493; the unchanged `NativeVideoSettings` fullscreen-window
assertion failed, then its isolated retry passed1/1. The cause remains
unproved and the failed aggregate/retry scopes stay preserved. No fresh
Release49-process setup run or Release gameplay route is inferred.

Two additional lifecycle receipts identify only `audiostreams/menu_mus.mus`
ordinal0/header `0x500` and ordinal1/header `0x510`, with immutable source hash,
original record/audio offsets and matching full Native PASS outputs. Ordinal0
mixes1,794 samples before genuine live cancellation; ordinal1 reaches all
88,803 authored samples and natural completion. Their coverage uses
`use_kind=pcm_mixer_service`, `lifecycle_tested=true`, `draw_tested=false`.
They grant no malformed exact-stream, whole-bank, nonzero seek or gameplay
credit. The v3 join retains the stable v2 packaged field report/base and the
previous v2 matrix. Together the current receipts yield135 tested rows,
46 rendering-tested rows and48 lifecycle-tested rows.

The matrix retains 39 explicitly selected owned resource logs with 77,828
events and zero malformed telemetry lines. They prove 19 encountered effect
sources,28 exact selected FX passes and1,932 uniquely identified sound cues.
The v2 report retained1,953 rows with explicit mission labels; v3 qualifies
gameplay only while a source-qualified map-ready owner/generation is active.
It records305 owner-qualified original rows. Pre-ready requests, legacy labels
and resources after cleanup retain raw mission text without gameplay credit.
Those logs cover seven
historical executable hashes, retained with launch/result receipts in
[`prevalidation-capability-thread-final-evidence-scopes-v2-20261002.json`](../build/restrictive-check-audit/prevalidation-capability-thread-final-evidence-scopes-v2-20261002.json).
They are not gameplay evidence for the current rebuilt executable. The 48
failure-context groups retain 182 legacy terminal rows and six nonterminal
rows, including missing frozen-copy assets and an unregistered/null exit
successor. They do not establish48 unsupported assets. Texture profiles,
stream header candidates and ambiguous packaged copies retain candidate
sets; lifecycle/shutdown observations and synthetic fixtures grant no
gameplay or named asset release credit.

The live normal frontend Exit route also exposed stale diagnostic attribution:
the raw `mission` field can remain `loc` after the original owner retires and
Main Menu returns. The parser keeps source-qualified prepublication map-load
requests as request scope, then matches map-ready manager/package/owner and
generation. Cleanup clears gameplay ownership; a later matching ready boundary
is required to restore it. Rows lacking these boundaries are explicitly
`legacy_label_only`, and repeated owner addresses cannot transfer coverage
across generations or logs. The preserved39-log v3 inputs exclude the later
completed frontend route; its separate v4 join follows below.

The v3 Python changes pass35 matrix tests and four strict audio-adapter tests,
plus the affected setup/padded/extended/census exporter tests. The Native
493-test source snapshot retains the earlier31-test matrix fixture source;
the exact inverse patch matches its recorded hash, while the newer Python
hashes and explicit reruns are pinned separately. That snapshot also omitted
renderer `.cpp` files: whole-source renderer conclusions require a later
complete build snapshot or explicit receipt inputs. Source-only guard reviews
do not retroactively establish compiled renderer coverage.

The completed frozen normal-exit join is
[`prevalidation-capability-thread-normal-exit-mission-support-v4-20261002.json`](../build/restrictive-check-audit/prevalidation-capability-thread-normal-exit-mission-support-v4-20261002.json).
It reuses the complete immutable v3 implementation/test rows and reprocesses
40 explicitly hashed logs, including the completed ordinary frontend run.
It records81,611 events,1,979 runtime rows and342 owner-qualified gameplay
rows;135 tested /46 draw /48 lifecycle rows remain unchanged. No current-tree
implementation census is rerun after the next C++ wave begins.

The new run has25 native captures,22 verified deliveries,3,783 resource rows
and no substantive failures. The runner reached its1800-second deadline;
the game then closed under its owned-window policy and primary stores were
unchanged. A separately preserved endpoint proves ordinary Continue to LOC,
accepted Exit Game, original owner retirement at sequence5,590,173 and actual
visible Main Menu return. A later Continue publishes the same address with
generation2 at sequence5,596,578. This is saved-game reentry, without death
or checkpoint-reload credit. Map lifecycle completion does not prove complete
lifetime for the named assets loaded by that map.

Frozen executable SHA256 is
`11385581244ed2e01e065cf54bf09f23975ca7e3e36b6dc21f4af99715ea06a1`,
with copied AOT manifest
`6c3b8a2f3aabab501761bf2582b6432e874d0bde1e1edc29ba1775a381d11db2`.
All seven copied runtime/content/manifest files and endpoint capture hashes
were independently rechecked before the v4 join. Its code/build authority
remains the old Native493 scope, including that scope's explicit renderer
source omission; source files subsequently edited for the next wave are not
credited by this run.

The old logger retains raw `mission=loc` for365 records after actual owner
retirement. V4 marks those records `post_retirement_unqualified` and gives
them no gameplay credit before a new qualified map request and ready boundary.
Source-qualified requests preserve mission-request attribution; ambiguous
textures, streamed header candidates, raw geometry pointers and cached VFX
identities remain unlinked or candidate sets. Native `last_action` can mask
movie-control inputs; raw verified PAD deliveries are retained separately.
The v5 source census and v3 scope retain their earlier document hashes;
this v4 documentation addendum does not rewrite those historical receipts.

## Frozen 497 tests and closed Native routes

The new [Native matrix](../build/restrictive-check-audit/controller-mission-matrix497-live-support-v5-native-bound-v2-20261002.json)
and [Release matrix](../build/restrictive-check-audit/controller-mission-matrix497-live-support-v5-native-release-bound-v2-20261002.json)
use freshly exported independent receipts from the complete 497/497 passing
JUnit for each configuration. Both scopes record 1,219 inputs, configured
translation units and compiler dependencies, including renderer sources.
The Native full receipt is SHA256
`aec4a6da6c20ecccd9f1b5d061b56bbd5ab345e31bc39b7638019920ce6bd822`;
Release is
`5038f10c18ecc4664ff656d57f3ac532bd44da80c118d71acba75c806574e3d4`.
These establish their own execution scope. The earlier Native 493 and
Release 492/493 failure/retry authorities and v4 matrix remain unchanged.

All 49 setup cases are independently passing CTest processes in the new
JUnit. The build-local adapter checks each exact command, row/source/name,
technique count and create/query/metadata-use/release marker against the
current catalog; no runner outcome is fabricated. The original row-2 adjacent
setup prerequisite remains part of the fixture. Setup gives no rendered use
or complete shadow-camera teardown credit. Padded and extended exporters
also recheck their exact successful markers and arguments, then bind every
referenced source and executable to the corresponding 497 scope. Three new
65-group cases add six repeated FX pass receipts and three synthetic geometry
lifetimes, through real original stream/relocation/composition/use/pool cleanup.
They do not identify packaged meshes or validate active captured-generation
reuse. The separate exact menu MUS adapter accepts only the two authored
stream records with complete original PCM/mixer/service lifetimes;
`draw_tested` stays false for audio.

Each configuration has 138 tested rows: 110 setup-tested FX selections,
26 synthetic geometry regressions and two exact sound streams. There are
49 rendering-tested rows and 51 lifecycle-tested rows. These figures are
row coverage, not the number of native test invocations. The exporter rejects
nonpassing/ambiguous markers, wrong catalog identity, changed compiled
inputs and malformed raw route endpoints. Three setup attribution controls
and two ambiguous/zero-generation route controls reject independently.
The [final join receipt](../build/restrictive-check-audit/controller-mission-matrix497-live-support-v5-receipt-v2-20261002.json)
pins the adapters and outputs. Earlier build-local adapter failures and the
initial independently corrected count join remain preserved.

Only two closed resource logs are imported into these new matrices: actual
LOC movie skip and Tree Hugger whole-party death/reload. They contain 2,142
events, zero malformed telemetry lines and zero resource rejection groups.
There are 301 exact catalog rows encountered while a qualified map owner is
active, plus 15 encountered FX sources. The 1,675 unlinked events retain raw
identity, caller, parameters, mission, last action and ownership. Unknown
headers, ambiguous texture profiles, raw geometry/VFX pointers and producer
entry candidates receive no invented catalog identity. Lifecycle records
remain route observations, without named-asset use/release credit. Both
matrices' live events execute the separately frozen **Native** app; Release
test passes do not create a Release gameplay claim.

Frozen application SHA256 is
`4813bebf4a0e741a3596f8268cb746fa55ca7dcf87012c25554c4091e0cdbba0`.
Its seven-file copy receipt is
`6654de229cc1342debe773387f11345bdf88c21126375cca6f40422d91bc75bf`.
The [movie endpoint](../build/restrictive-check-audit/controller-mission-matrix497-movie-route-evidence-20261002.json)
checks original `82321114` Start acceptance, the same owner at decoder stop
`826B92C8`, original decoder release/completion, and subsequent LOC readiness.
It is bound to the actual bounded filename `movies\\en\\loc_igc01.vp6`; an
archive/payload occurrence association remains unqualified. Closing its owned
window is separate from the completed movie endpoint and grants no mission
exit or complete map-resource retirement.

The [Tree route endpoint](../build/restrictive-check-audit/controller-mission-matrix497-tree-route-evidence-v2-20261002.json)
requires all four exact original whole-party-death requests (`823BBACC`,
flags `2001`), same-generation cleanup (`823BBCE0`), qualified fresh requests
and new readiness. Generations 1→2→3→4→5 include three reused-address
transitions. The same opening/checkpoint GUID is restored at readiness;
requests temporarily report zero live GUID, so later-checkpoint world-state
restoration is unproved. The corrected root receipt preserves its v1 and raw
hashes: there are 11 unknown mission rows total, eight after cleanup before a
new request and three before the initial owner. None receives gameplay credit.
The neutral input and viewed captures do not establish an ability hit, pickup,
destruction or per-actor numeric death state. Terminal Runtime teardown still
has incomplete original audio/texture/geometry/FX/worker/GPU releases; these
routes do not close those individual resource lifetimes.

## Separate 500-test submesh repair scope

The later immediate-skin repair passes **500/500 Native** in 447.12 seconds
and **500/500 Release** in 437.34 seconds. Strict full-suite receipts are
[Native](../build/restrictive-audit/submesh65536-wave-20261002/full-native-v1-receipt.json)
(`bcd9cc22af1a916eb72bd6202f4770b128f767fbc4eccb7d943308a63cb9b7fa`) and
[Release](../build/restrictive-audit/submesh65536-wave-20261002/full-native-release-v1-receipt.json)
(`9711a826a1af73848f1ea9c2dd50ad5d691dad3f498af4c7349184e6ea9db5a3`).
Each rehashes its frozen 1,219 inputs, executables and DLLs against separate
compiled scopes, Native `6f31a6f7...caacb` and Release `853270c3...f550a`.
They preserve the earlier three independently executed valid cap rejections.

Three constructed original base/textured/dual skin cases now allocate and
relocate 65,536 rows through the actual original first-pool reader, use
opaque/alpha selections with equivalent pixels, and release the original
camera/declaration/FX/cache/pool/stream-source owners. Four malformed
frontiers per family retain exact early failure receipts and a later valid
draw: count65,537 and shifted row views exceed their actual containing owner,
compiled indices fail their range, and a wrapped RenderWare selector fails
offset arithmetic. Prevalidation preserves PPC/CSR/LastError and keeps the
row generation in the excluded instance, while true counts/flags remain keys.
The [submesh audit](restrictive-submesh-material-audit.md) records the full
baseline/repair identities and remaining alias, row-epoch, concurrent-pinning
and full GPU retirement limits. Other material/family/diagnostic caps remain
independent. No packaged 65,536-row occurrence is identified: the shipped
maximum remains19. These tests add neither gameplay evidence nor a new
catalog support join. All 497 v5 matrix rows and counts above remain unchanged.

Fresh **source-only** reports are
`build/restrictive-check-audit/submesh65536-source-census-v1-20261002.json`
(SHA256 `407e122631426449a6ade75ab9f821d62e5e9f9061e5af12b9ca11f21fcb156b`),
`submesh65536-source-effects-v2-20261002.json`
(`ed9a6f2917ca8236287d764aa0d7c880abcd22bb53b82265eb49014ad62a0e99`) and
`submesh65536-source-shader-producers-v1-20261002.json`
(`b8601be1e6239fe6e94d779d6be7c030bffa200d66c1d2dbf5112e543521c2b9`).
The census contains **769 mechanical guard candidates, 1,112 keyword lines
and 274 source files**; each candidate stays mechanically untriaged unless
independently reviewed. The effects snapshot retains49 effects/110 passes,
47 artifact-admitted passes,45 source-declared selections and63 missing
artifact passes, with zero observed passes in that source-only invocation.
The shader producer report retains the 63 independent missing-pass queue
and its explicit selector/original-code-versus-lifetime limits. None of these
source files grants new native or catalog lifetime credit.

The [stable-grouping source review](resource-audit-grouping-review.md) and
`resource-audit-stable-group-review-source500-20261002.json`
(`fde09fd0770f4cc452b93c3894e13f50534b0144d8a515c4826a2202fb7b4e48`)
inventory17 observe, six lifecycle and two action callsites. The two unopened
producer candidates move accepted movie sample packet counters and
release-only pipeline register lanes from stable parameters into `instance`.
They preserve raw data, real variant flags/counts, shader/material identity,
original caller, mission and last action; generic ResourceAudit performs no
automatic normalization. The audio reader close at `8233D980` also has an
early ownership attribution gap: its snapshot uses rawr4 whereas functional
handling derives the actual group from checkedr3+4. These proposals remain
unapplied and untested, and historical JSONL groups are unchanged.
