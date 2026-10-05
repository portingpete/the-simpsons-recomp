# Reach-game306 — Land of Chocolate music and next character material

Goal remains active. Run306 presents12 scene frames, but the scene is malformed
and character control remains unverified. It stops at the unsupported
`simpsons_skin_dualtextured` effect. This continues
[cached sky/sampler work302..305](reach-game-305.md) and
[resident audio work301](reach-game-301.md).

## Music bank qualification

The305 startup rejection was an unknown stream certificate, not a new codec,
channel count or seek. The original asset is
`Simpsons Game, The (USA)/audiostreams/loc_mus.mus`,30467840 bytes,SHA256
`b350a5b1ce17ff5b53a2b0a6c035d9c36eafbf213252dad07b2c387f6c0743fa`.
Stream0,idB52A088E,header0314BB804002C442 has181314 frames in36 blocks. The
first2913-byte normalized claim exactly matches the original block hash
`b7aaa9c0a0928089137d1d5d520890b1aecc3aaea5d422afc52ab8f5b616d967`.

`tools/qualify_all_menu_xma.py --bank loc` independently qualifies all50 streams,
3937 blocks and20017467 frames across three stereo layers:

- Every stream is48kHz,six-channel,nonlooping.
- Native XMA1/XMA2 and both read schedules produce identical raw PCM.
- Every declared block quota is satisfiable from that block or earlier input;
  minimum surplus is zero, with no fabricated samples or raw EOF flush.
- Independent stock decoding agrees after its measured576-frame trim. Maximum
  difference across all150 layer comparisons is1.7881393432617188e-7.
- Original bytes are unchanged. Only independently certified discardedFF
  packet tails are restored in owned decoder input, as in the menu bank.

Complete evidence is in `build/loc-music-xma/report.json` and per-stream files;
the run log is `build/reach-game-306-loc-music-qualification.log`.
`audio/loc_music_xma_certificates.h` contains exact normalized-block hashes,
frame counts, layer extents, restored-tail sizes and full trimmed PCM hashes.
`audio/ea_xma_block.cpp` admits this bank through the existing ordered-block
parser. No producer, queue, mixer, loop or seek guard was bypassed.

The existing menu fixture generator/test now accepts an explicit bank. Both
banks keep their exact expected stream/block/frame totals. The test also proves
that all87 menu/loc headers are unique, so header lookup is unambiguous, and
rejects mutated blocks, unknown headers, wrong channels and wrong sequence.

## Validation

- `build/reach-game-306-regenerate.log`:311 generated files,zero AOT semantic
  diagnostics. No generated C++ was edited.
- `build/reach-game-306-build.log`:game and music tests build.
- `build/reach-game-306-tests.log`:MenuXmaSources andLocMusicXmaSources pass,
  checking complete256-frame quotas, exact all-layer PCM and retirement.
- The six focused renderer tests passed in305; renderer code is unchanged
  since that verification. The complete suite was not rerun.
- `build/automatic-startup/reach-game-306/game.log` shows the new original
  stream's native decoder admission followed by an actual256-frame,six-channel
  PCM commit from voiceE45AFA50. Full live song completion is not established;
  complete ordered quotas are covered by the offline/native tests.

The game records12 accepted scene presentations. The last is presentation1346,
750 cumulative scene draws,30 new scene draws. Sky payload006000FA executes
seven times. The strengthened startup observer reports failure, correctly:
neither ten seconds of rendering nor character control was established.
The last visually reviewed image remains the malformed303 frame documented
in305; this run does not establish repaired visual output.

## Next boundary

`Unimplemented native engine graphics boundary 0x82740680, caller0x8273B4E0`.
This is `SimpsonsNativeSceneDispatch` rejecting another skin source, before
any new shader is bound:

- Packet82D6E88C,metadataE6711008,objectE1B14FD0,cameraE4D41AB0.
- TypedE1AAC2C0,vtable82061714,wrapperE1AAC0E0,effect00500023.
- Source8201CD48,`simpsons_skin_dualtextured`,record size7110.
- Flags0,recording01000000,context00900001,property flagsC0,bucket1,
  cache offsetB0,eligible1,selected/payload0.
- Exact effect SHA256:
  `9fbe1c024716b82ecdf94cb79121f468663cd2887cc0c945896c075d5ccd9eb7`.

The existing catalog's owned metadata is copied to
`build/skin-dualtextured-inventory/effect-metadata.json` for inspection:

- Opaque `skin`0003FFFC/pass0003FFFE,context5F50,VS8201E6DC,
  PS82020B9C,two scalar states and12 sampler rows.
- `skinalpha`0007FFFC/pass0007FFFE,context66C0,VS8201F984,
  PS82021344,two scalar states and6 sampler rows.
- Opaque VS record4768 bytes/code888;PS1952 bytes/code864.
- Alpha VS record4616 bytes/code804;PS732 bytes/code192.

First qualify the actual selected shader pair and reflected material/bone
mapping. Useful existing pieces are `tools/analyze_skin_shader.py`,
`tools/analyze_rigid_dualtextured_shader.py`,native skin mesh ownership and the
original skin immediate lifecycle in`runtime/engine_effects.cpp`. Do not admit
the source in the dispatcher until its native draw path is implemented. The
existing skin path only accepts82006348 and has zero samplers, so changing the
allowlist alone is insufficient. Preserve the original cache/fallback decision.

After sustained rendering, investigate the world/material/depth defects and
verify real movement. Native keyboard/command input still supplies buttons
only; analog movement support may be needed for that verification. Earlier
broader runtime/null-device/low-page concerns remain documented. No CPU/shader
interpreter,JIT or console GPU renderer was introduced; original assets and
reference trees remain unchanged. No game process remains running.
