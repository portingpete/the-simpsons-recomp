# Reach-game301 checkpoint — 2026-09-19

The goal remains **active**. Startup now presents five scene frames, queues the
original resident intro/loop blocks, commits their PCM through the original
mixer, and retires the short intros through the original request-release path.
It stops on the sky's unsupported cached drawing path. Sustained gameplay and
character control remain **unverified**. The captured world is visibly malformed.
No game process remains from these runs.

## Resident loop ownership

Original823425E8 reads a12-byte resident loop header: total frames plus the
loop-start frame. The reached loc sound at1829484 has55 intro frames and172690
loop frames. The block sizes are132 and34828 bytes.

Original calls establish the sequence, without native playback decisions:

- 82342C88, return82342C8C: P+8/P+44 selects the first block, P+14 starts at0.
- 823428C4, return823428C8: P+34 selects the loop body and is saved in P+38.
  P+14 equals the loop-start frame.
- 82342988, return8234298C: P+38 selects the same body; original code resets
  P+14 to the loop-start frame before enqueueing.
- All these resident calls pass r6=1, so each block requests a fresh codec
  context. Original823424D8 inverts that into queue continuation flag0.

`runtime/engine_audio_owners.cpp` now accepts these exact original transitions.
Each resident request owns its decoder and receipt, so ahead-of-playback
queueing cannot replace a still-playing intro's decoder. Decode, commit,
advance, cancellation and retirement use the request's retained decoder.
Original slot allocation, counters, queue stores and release calls remain.
The compressed source belongs to its verified bank/allocation generation.

`audio/resident_xma.h/.cpp` adds explicit loop profiles alongside ordinary
profiles. `loc.sbk` is admitted with its exact full-bank hash, metadata/audio
split, sound offsets, block hashes, selectors, complete frame quotas and PCM
hashes. There are120 ordinary sounds and12 two-block loops. Small intro sources
use the actual maximum256-frame mixer quota; decoder capacity is
still bounded by each qualified raw output extent.

## Native decoder packet-boundary fix

One loc loop and two formerly excluded story sounds exposed a shared native
decoder bug. A frame can end exactly at bit16384 of a2048-byte packet with its
continuation trailer set. Upstream leaves `packet_done` false, so the next
packet's32-bit header is interpreted as audio rather than parsed as a header.

For loc header1619990, loop block1620129:

- Frame292 occupies packet16 bits15437..16383 inclusive and ends with trailer1.
- Packet17 starts `08000000 06f7fc00...`; the actual next frame length is891.
- The separate unmodified-policy diagnostic codec reports frame293 length1024,
  used29, and994 residual bits. Its failed-frame dump begins `08000000`, proving
  that it consumed the packet header as a frame prefix.
- `build/loc-resident-xma/1620129-diagnostic.log` retains that witness.

The patch in `tools/build_native_audio_codec.py` sets `packet_done` when no bits
remain for XMA1/XMA2. Frame validation, transform arithmetic, spectral data,
PCM generation, sample skip and EOF behavior are unchanged. Production input
bytes remain untouched. The builder verifies the previous owned source/install
before migration and forces the patched translation unit to rebuild despite
preserved archive timestamps.

Qualification decodes original bytes in XMA1/XMA2 with two read schedules.
An independent stock CLI comparison uses an offline diagnostic copy that only
clears the trailer at the exact exhausted packet boundary. This does not alter
spectral frame bits and is never a production input repair:

- Loc block1620129: boundary packet16,173568 raw frames,172196 stock-comparable
  frames, maximum difference8.94e-8 after the established576-frame stock delay.
- Story header2768867: boundary packet10,60416 raw frames, difference2.38e-7.
- Story header3858558: boundary packet4,66560 raw frames, difference1.79e-7.

All429 ordinary story sounds now qualify. Single-frame loc intros are wholly
trimmed by the stock decoder's576-frame delay; their qualification explicitly
records that limitation rather than claiming stock PCM coverage. Native
variants/schedules match their complete original quotas without EOF.

`tools/probe_xma_frame_failure.py` builds a separate diagnostic decoder below
`build/loc-codec-diagnostic`; it does not change the production install.

## Validation

- `build/reach-game-301-regenerate.log`:311 generated files, zero semantic
  diagnostics; generated C++ was not hand-edited.
- `build/reach-game-301-codec-build2.log`: verified owned codec rebuild.
- `build/reach-game-301-loc-qualification3.log`:120 ordinary and12 looping
  loc sources pass; no exclusions.
- `build/reach-game-301-story-qualification2.log`:429 story sources pass;
  no exclusions. Reports and exact hashes are in each bank's build directory.
- `build/reach-game-301-build.log`: game and focused test targets build.
- `build/reach-game-301-tests.log`: all six pass: native codec ownership,
  source ownership, frontend resident sources, story resident sources,
  loc resident sources, and original bank lifecycle. Full suite not rerun.
- Run300 accepts the first55/172690 loop, then stops at the decoder-rejected
  loop's unsupported profile. Run301 gets beyond all these audio boundaries.

Run301 captures presentations1286..1290 with153,134,107,83,41 scene draws.
Files are under `build/automatic-startup/reach-game-301`; the reviewed fifth
frame is `preview/native-frame-2014840.png`. Logs show actual original-mixer
PCM commits for55-,103- and178-frame intros, and original82341884 retirement
for their requests. The run is too short to establish a complete live loop
repetition; full repeated quotas are covered offline.

## Next boundary

Failure: `Sky immediate mesh has no completed original staging`, at
`runtime/engine_effects.cpp::skyMeshOperation` (around1452).

This is the first reached **recorded** sky draw, not a missing immediate upload:
packet82D6EAA8, typedE1AAE350, metadataE6707120, source82036448,
effect00500027, context00600001, payload006000FA. The original recording call
returning to82740624 enters the deferred mesh loop82701448. The sky implementation currently
requires `rigidImmediate.active` and only implements immediate82701220.
The existing rigid backend already supports immediate and recorded paths.

Implement the sky's actual native recording/replay path, including per-payload
live inherited constants, material overlays, texture ownership and original
cache lifetimes. Do not force the original dispatcher to bypass recording.
Useful analogues are `rigidMeshOperation`, `rigidMaterialOperation`,
`prepareRigidReplay`, retained rigid payloads, and native rigid recording APIs.
Sky currently has separate `SkyMeshRun`/`SkyMaterialRun` but no retained sky
payload or replay owner. Its material commit reads only immediate staging.

After sustained rendering, investigate world viewport orientation and material
output, then verify actual character movement. The five captured scene frames
are not a completed in-game goal. Earlier low-page/null-device handling,
streamed texture skips and broader test failures remain documented in299 and
earlier checkpoints. No runtime CPU/shader interpreter, JIT or console GPU
renderer was introduced; original assets and reference trees remain unchanged.
