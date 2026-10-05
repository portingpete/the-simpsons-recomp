# Gameplay reached: resident audio and direct resume qualification

Run315d reaches the level, visibly responds to two A inputs with complete jumps
and landings, and remains alive after331.766 seconds with no failure marker.
Two120-second observer windows pass. The final inspected run is left running.
The bounded verification is saved in
build/automatic-startup/reach-game-315d/gameplay-verification.json.
This proves level entry and jumping; full level traversal, long-term stability,
visual fidelity and performance remain separate work. Bright green/pink window
colors remain visible. Earlier failed iterations below are historical evidence.

Run315 integrates both original Homer resident banks through the existing
named-loader, full-bank hash, split-allocation and generation ownership path.
The native producer and its guards are unchanged. The catalog contains235
Homer profiles and201 variant profiles, with no loops or excluded records.
Each profile has real native decode evidence; the reached shared51200-frame
record produces51712 raw frames, consumes the original384-frame initial skip,
and retains128 surplus frames. XMA1, XMA2 and split packet reads agree exactly.

Qualification is recorded in build/homer-resident-xma/report.json. Regeneration
and the SimpsonsNative, ResidentXmaTests and ResidentBankLifecycleTests builds
pass. CharactersResidentXmaSource, HomerResidentXmaSource and
OriginalResidentBankLifecycle all pass in build/reach-game-315-tests.log.

The startup replay now accepts either a fresh opening movie or an accepted
scene presentation after Continue Game. Direct resume publishes its route and
sequence-complete event without inventing a movie skip or sending START.
Capture requests use exclusive creation and wait for an observer's existing
request to drain. Fourteen route tests and seventeen gameplay-evidence tests
pass. The scene verifier still proves rendering only, not character control.

## Run315a

Continue takes the direct-resume route at25.2888 seconds. The replay captures
51 scene frames across10.2499 seconds and exits successfully at35.7971 seconds.
Its gameplay_verified field describes that bounded rendering threshold; it
does not establish the overall goal. The independent observer subsequently
detects a worker failure after213 scene presentations across33.513 seconds,
with5830 cumulative scene draws. The game process exits.

The original variant bank loads with its exact1776651-byte full-bank identity.
The newly reached failure is streamed dialogue, not resident admission:
original function823424D8, caller823424D0, header0300BB8040019116,
one channel at48000Hz and102678 declared frames. The first owned reader block
is1668 bytes, declares4736 frames and hashes to
aaf94d73e2b552b1931a403ce7c0f9024ad51eca29059c0ee8c8b930642307bd.
That header lacks a streamed-source certificate. The runtime retains its
format, original caller, seek, request ownership and accounting guards.

Evidence: build/automatic-startup/reach-game-315a/{game.log,inputs.jsonl,
result.json,scene-observation-20260919-201532-121842.json}. Gameplay without
crashing and responsive controls remain unverified. Streamed dialogue
qualification is the next boundary.

The streamed record matches exactly
Simpsons Game, The (USA)/audiostreams/mr_xxx_0/d_homr_xxx_0003e65.exa.snu,
48144 bytes with SHA-256
ee6bea761488b1452d4e9153e56f882cab120432d01ef7bbfed83f538a34a0a0.
Its21 ordered blocks sum to102678 frames. The first normalized block matches
the live receipt exactly; the final block includes41 zero bytes in its owned
hash and terminal-padding certificate. Qualification extends the existing
mono-dialogue catalog, with no producer or reader changes. All three native
decode schedules produce103424 raw frames with identical hash
84d67f5d553c22a35b453e0c4227ee1664fdd208ff610b48e9266dcc6b94f2be.
After the384-frame initial skip, every block meets its quota with0..362 surplus
frames. No EOF is sent. The independent decoder comparison has maximum error
1.7881393432617188e-7. Evidence is in
build/mono-dialogue-xma-homer-3e65/report.json and its accompanying logs.
The focused test now pins4 dialogue profiles,85 blocks and424921 frames,
while retaining every-block mutation, quota, PCM-hash and retirement checks.
The315b regeneration and game/MenuXmaTests build pass. MonoDialogueXmaSources
passes in0.24 seconds; logs are build/reach-game-315b-{regenerate,build,tests}.log.

## Run315b

The reached streamed dialogue now passes. A completed front capture at
presentation1611 shows Homer standing in the level with the objective overlay;
its exact RGB16 preview is build/reach-game-315b-preview/native-frame-2323923.png.
The run remains alive beyond both earlier audio failures.

A single A_HOLD command was then appended to the existing controller channel
after replay completion. The game logs actual delivery at presentation1764.
Before a visible jump can be verified, a new resident record rejects at
823424D8, caller82342C8C: headerE7B02E09, bytes0300BB8000001648,
blockE7B02E11, first sequence, bank/profile/loop all0. The observer catches
the failure after368 scene presentations spanning58.55 seconds and10170
cumulative scene draws. Input delivery is established, character control is
not. The goal remains open; the new resident record needs qualification.

Evidence: build/automatic-startup/reach-game-315b/game.log,
control-observation-20260919-202355-191784.json and
scene-observation-20260919-202400-130452.json in the same run directory.

The new resident header matches loc_global.sbk, entry2 of loc/loc_global.str.
The2948197-byte bank hashes to
145197be5688def500325f86d2c4896ac3ddbf751be95c388393e22bbec2e6df;
metadata is0x17C30 bytes and audio begins at0x17C80, extent0x2B7FE5.
The reached header lies at0x1CDA89, followed by a1561-byte block declaring5704
frames, selector3, with1549 payload bytes and499 candidate FF tail bytes.
Its block hash is
02afc69859b1855cfd62ff1750d94158f4d658775de0dc2e2a98c2bf95c41b0a.
The inferred live audio baseE794D000 matches the logged0x2B8000 allocation,
but actual admission still requires the original named loader, full-bank hash,
split, allocator and generation checks. Offline inventory finds479 records,
no loops, at24000/48000Hz. All479 records now pass real decode qualification,
with zero exclusions. The reached record produces6144 raw frames identically
in all three native runs, with no EOF; skip384 and quota5704 leave56 frames.
Its raw PCM hash is
79f52d7a244c673ea999bf1bf303221b653d4a0055cbb78d0b57230f11e75210.
The independent decoder comparison differs by at most2.98e-8 across5056
samples. Reports and decoder/module logs are in build/loc-global-resident-xma.
The catalog appends this seventh bank through the unchanged runtime owner.
LocGlobalResidentXmaSource verifies the complete bank, all479 decoded profiles,
exact reached record and offset, mutation rejection, quotas and retirement.

The315c AOT regeneration and game/test build pass. All five focused CTests
pass: MonoDialogueXmaSources, CharactersResidentXmaSource,
HomerResidentXmaSource, LocGlobalResidentXmaSource and
OriginalResidentBankLifecycle (10.51 seconds total). The new bank test takes
4.16 seconds. Logs: build/reach-game-315c-{regenerate,build,tests}.log.

## Run315c: visible control, later dialogue failure

The original loc_global bank loads through the named-loader/full-hash path.
The A_HOLD test now succeeds: completed front captures show Homer standing at
presentation1623, airborne at1628, and landed at1642. All20 consecutive captures
validate against the scene metadata authority. Exact previews are in
build/reach-game-315c-control-preview (draws2159152,2160189,2163099).
The controller receipt and capture order are recorded in
build/automatic-startup/reach-game-315c/control-observation-20260919-203225-139326.json.
That helper deliberately leaves character_control_verified false; the control
conclusion here comes from visual inspection of the before/airborne/landed
captures, not input delivery or pixel changes alone.

The longer observation still catches a later streamed dialogue failure:
823424D8, caller823424D0, header0300BB8040018EC8, first owned block1704 bytes,
4736 frames, SHA-256
b141a42cee2048612ff2ef24d24a7bac89e23c40b2d7670cba084bcc89d09657.
The run presents621 scene frames across99.975 seconds and17254 scene draws
before failing. The goal remains open despite the successful jump.
The observer report is scene-observation-20260919-203312-434878.json in that run.

The late record is d_homr_xxx_0003e62.exa.snu in audiostreams/mr_xxx_0,
full-source SHA-256
88b36c4bc9b09c28785761a9e330c9ff7b3231f68f18ae5045f6fe27c5570099.
All21 blocks total102088 declared frames. Qualification produces102912 raw
frames identically in three schedules, hash
bfc6dba836c6318a306d71e61ab7864e90967696d30f1b23192cdce90d83bff6,
without EOF. After the original384-frame skip, each block meets its quota;
surplus ranges0..440 frames. The independent comparison has maximum error
1.7881393432617188e-7. See build/mono-dialogue-xma-homer-3e62/report.json.
The focused catalog test now pins5 profiles,106 blocks and527009 frames.
The315d regeneration and game/MenuXmaTests build pass; MonoDialogueXmaSources
passes in0.23 seconds. Logs: build/reach-game-315d-{regenerate,build,tests}.log.

## Run315d control evidence

The same A_HOLD test succeeds in the rebuilt run. Capture2418141 precedes the
input,2419178 shows Homer airborne, and2422088 shows him landed. Presentations
1769 through1788 are20 consecutive completed scene captures. Their raw receipts
and input delivery are recorded in control-observation-20260919-203840-889342.json
under build/automatic-startup/reach-game-315d; inspected exact previews are in
build/reach-game-315d-control-preview.

The first120-second observer completes without a failure or process exit:
650 live scene presentations over103.192 seconds,18066 cumulative scene draws.
Report: scene-observation-20260919-203919-797180.json. The later dialogue header
0300BB8040018EC8 is actually reached and accepted by the original stream owner
(game.log927911 onward). A subsequent completed capture at presentation2243
contains25710 scene draws and the same active level. A second observation
window also finishes without a failure or process exit:702 new live scene
presentations over116.961 seconds, ending at presentation2889 and43798 scene
draws. Its report is scene-observation-20260919-204153-603776.json.

A second A_HOLD test at presentations2553..2572 again shows Homer airborne
and then landed (captures2582245 and2585155). The game remains alive afterward.
The final fresh capture at presentation2940 has45226 scene draws. The final
process/log-prefix audit at331.766 seconds finds none of the four failure
markers and verifies the original process is still alive. These observations
satisfy the bounded goal of reaching responsive gameplay without crashing.
The full suite was not rerun; the relevant focused tests above passed.
