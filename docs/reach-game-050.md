# Replay 050 and multitone inventory

## Live retest

Reconnection restored the Remote Audio render endpoint. Default mastering-voice
creation succeeds and NativeAudioOutputOwnership passes. Replay 050 verified the
main menu, delivered Continue Game and skipped the opening movie. Native rigid
draws ran before scene admission rejected source 820547E8, identified by the
original catalog as simpsons_rigid_multitone.

Evidence: K:\SimpsonsNativeCopy\build\automatic-startup\reach-game-050\result.json
and K:\SimpsonsNativeCopy\build\automatic-startup\reach-game-050\game.log.
The rejected packet has flags=0, caller8273B4E0 and boundary82740680. The logged
technique_AC=0007FFFC is a stored alpha-technique field, not evidence that the
opaque packet selected alpha. The source allowlist is the rejecting condition.
Gameplay presentation and character control remain unverified.

## Offline implementation foundation

K:\SimpsonsNativeCopy\tools\analyze_rigid_multitone_shader.py pins the effect
hash, opaque VS82054F2C / PS82055710 record hashes and complete control flow.
It inventories 41 VS and 91 PS issue slots. VS slot39 belongs to conditional
EXEC CF7 (1027/5600), which the gloss control-flow emitter does not support.
This is an inventory, not an execution oracle or shader translation.

Opaque context2D40 has 24 sampler-state rows and a 608-byte private bank.
Private material mappings include:

- leaf14 -> PS c49 (custom lines)
- leaf20, byte560 -> PS c47 (noise parameters)
- leaf21, byte576 -> VS c46 AND PS c46 (noise parameters2)
- leaf22, byte592 -> PS c45 (noise parameters3)
- base sampler leaf17 -> texture slot2; noise sampler leaf19 -> slot3
- palette sampler leaf18 is unmapped in this selected context

The current rigid VS bank contains only 30 float4 registers, so admission alone
cannot implement this material. The native backend has no multitone artifact
creation cases or HLSL source. VS comparison/conditional operations and control
flow need transcription; texture fetch semantics and cross-stage material
ownership need independent qualification. Do not alias the shader to gloss.

## Tests and remaining work

K:\SimpsonsNativeCopy\tests\test_rigid_multitone_shader.py adds four tests:
complete slot coverage including conditional EXEC, pinned material maps,
every control-word mutation rejection without the record-hash gate, and
record/map corruption plus truncation rejection. Registered as
OriginalRigidMultitoneInventory in K:\SimpsonsNativeCopy\CMakeLists.txt.

NativeMaterialCompilerTests builds. Focused CTest: 7/7 pass (inventory, gloss
evidence, material artifacts, rigid shader and mesh tests on WARP/hardware).
The full suite was not rerun after reconnecting; the older 126/166 tally is
historical, not a current full-suite result.

Multitone HLSL, GPU oracles, artifact creation and runtime integration remain
unfinished. No runtime guards, shader aliases or audio paths were changed.
Replay051 was not run because this analysis-only change cannot advance it.
