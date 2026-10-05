# Reach-game311 — cached record retirement

Goal remains active. Runs311,311b and311c complete36 scene presentations, up from
29 in310d. Both retire the previously blocking cached record and continue
rendering until a depth-sampling particle variant rejects. The later frame
still shows a visibly malformed world. Sustained rendering, opening-movie
completion and character control remain unverified. This follows
[310](reach-game-310.md).

## Original CPU cleanup retained

The nonempty plugin callback827374B0 now validates its complete native/CPU
ownership before entering the original loop. Current admission is the
observed caller823FB3DC, offsetB0, flags10 and bucket1. Empty callbacks retain
their previous original behavior. Foreign buckets, records, active operations
and incomplete effect/payload owners reject before resource/list changes.

Original82737400 runs its prologue and CPU cleanup. Only BL8273743C to the
resource release82441708 is adapted: wait for submitted GPU work, invalidate
the matching rigid/sky replay constants, remove its retained effect owner,
and release the native payload. The payload is an opaque native identity,
never a guest COM pointer. Other record deletion callers remain guarded.

Original instructions still clear the payload field, repair the object's
links, call826F4BE8 to unlink the LRU and subtract its byte charge, and call
826F39B0 to return the record to its original pool. The completion hook at
827374A4 verifies the returned node's fields, head advancement, context aliases
and manager history. It updates the retained ownership model and validates
every remaining object/LRU link, byte total and all2000 pool slots. The
plugin-loop epilogue827374F4 verifies all four heads are empty. Original
stack, nonvolatile registers, loop and return ABI remain AOT.

The live record00600106/CPU E2ED1E0C had previously completed13 native replays.
Retirement releases1608 bytes and leaves90 records retained and1910 pool
slots free.311b receipt: `game.log` line60918 under its automatic-startup
directory. Later records continue replaying. Retirement does not decrement
the original per-frame successful-build counter; the original deletion
helper does not modify that field.

## Verification

`build/reach-game-311-tests.log`: five tests pass:
NativeRecordingPayloadWARP/Hardware, NativeRigidMeshWARP/Hardware and
OriginalDriverLifecycle. The driver fixture additionally checks rejection
of unscoped deletion entry/release/completion and plugin destruction without
changing CPU pool/manager state or callback ABI. Native payload tests cover
actual submission, GPU retirement, owner release, stale reuse and reentrancy.
The live run supplies the positive original CPU deletion evidence.

Latest311c AOT regeneration verifies311 files with zero semantic diagnostics;
`build/reach-game-311c-build.log` builds the game successfully.311b/c add bounded
particle diagnostics to the tested311 cleanup implementation. No generated
C++ was edited. The full test suite was not run.

311b presents1261..1296:36 scene frames, ending with1312 cumulative scene draws
and21 new scene draws. Capture after three presentations beyond retirement:
`build/automatic-startup/reach-game-311b/captures/native-frame-1990735.rgb10a2`
and its JSON. Presentation1293 has1249 scene draws. Preview:
`build/reach-game-311-preview/native-frame-1990735.png`.
Raw SHA256:8dcaa85901479f5bb00486d351aa0eb51ca1fde154ae2260efa1293742872ece.
Visually checked: HUD, chocolate horizon and explosion particles; pink ground
and large malformed black geometry remain. This is not gameplay verification.

## Next observed boundary

311b rejects at particle preflight82772D98 before the skipped SDK setup:
emitterE2EF5F30, definitionE60E0E10, parametersE60E0F40, count/capacity8,
type100=00, flags104=06, mode40=00, flagsD0=2804006D, flagsD4=80000000,
r29=0, textureE1B5B120, SP0203F3B0, LR82772D80. The new branch is definition
byte104 mask04. Definition+E4 is0.4. Bounded original emitter/definition/parameter
captures are in311b's `captures/particle-variant`.

311c repeats the same failure at emitterE2EF5F30, definitionE5F8CE10,
parametersE5F8CF40 and textureE1B5B180. It completes36 scene frames, ending at
presentation1199 with1312 scene draws. Its `captures/particle-variant` also
contains the original frame, shader registry and672-byte projector object.
All launcher/capture sessions ended; no game process remains running.

The current projector at82DFEB98 is the original shadows effect E4CEA400;
its+F0 texture is native00F00017, the1024x1024 shadow texture allocated by the
original shadows constructor. This identifies a projected **shadow** input,
not the separate viewport depth-copy header82DFE840. Emitter flags+10 are0,
so the original branch multiplies frame82DFEA20 by projector+260 without the
additional emitter-local matrix. The four projector rows are captured; the
existing EngineShadowTextures owner can validate this resource. Its mapping,
copy generation, sampler state and exact shader semantics still need checking.

Original disassembly: `build/reach-game-311-particle-disasm.txt`.
82772DF4..F44 checks frame82DFEA20+178, computes the projector matrix with
original827B8328, uploads VS21..24 and VS25.x from definition+E4, and selects
the projector's+F0 depth texture at stage2. If that frame owner is absent,
the original branch leaves the ordinary pixel shader selected. With an owner,
mode40=0 selects registry82CF25DC, whose adjacent source is82156B60. The VS
remains821570E0. Do not enable this by merely removing the variant guard.

Read-only preliminary inventory:
`build/reach-game-311-projected-particle-inventory.json`. Pixel record82156B60 is
652 bytes, SHA2566b4a47e0cc4cc014e9c5226ee70dc1ec413ba9e7d020d4b3d2b20b197a287ccb.
Its64-byte literal bank precedes240 bytes of code. Two control pairs cover
slots2..18: four stage2 depth samples with offset fields1/31 and distinct
Y/X/W/Z channels, one stage0 base sample, then arithmetic and color export.
The normal VS already exports three interpolants. The new pixel shader,
depth resource mapping and original projector constants still require
qualification and tests; this inventory is not a shader implementation or
runtime admission. A native depth texture adapter must preserve those sample
and channel semantics rather than assuming ordinary RGBA color sampling.
