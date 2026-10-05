# Live repair of the first-mission recap transition

The user authorized driving this exact route until the loading crash was fixed:
launch the direct completion shortcut, skip the outro, continue through the
mission recap, and enter the following area. Every diagnostic run uses its own
copied profile, save, and video settings. Controller commands go only through
the opt-in native input file; screenshots come from completed renderer
readbacks. The preferred shortcut remains `Play First Mission - Completion.lnk`
and opens the game directly.

## Texture mip allocation

The original manual failure in `Completion-20261001-120603-900Z-12972-0000`
was reproduced in `build/recap-live-fix/runs/20261001-121259Z-871cb1a7`.
The rejected resource was `beam1`, generation 446, a 64x64 L8 texture with
three levels and 12,288 bytes of tiled storage. Its descriptor was:

```
80800002 00000002 0007E03F 00001400 00000080 00001A00
```

The record is in `spr_hub.str`, split entry 13 (`spr_hub_split9.itxd`),
record offset `0xB28`, payload offset `0x2C000`. The original metadata
SHA256 is `624146a511e5009b3246e4abc5afa16cdcc542f662b18591c57c2dc3d85c0e4c`.
It matches the live record byte for byte. Original `8243FE90..8243FE98`
extracts `(descriptor[4] >> 6) & 15` and returns that value plus one.

The old loader admitted only a base level. The new chain decoder preserves
that exact base qualification and separately validates authored mip metadata,
4 KB level allocations, packed-tail origins, every pixel address, and the
complete allocation length. It converts luminance to RRR1 and uploads every
authored level into immutable, owned RGBA8 native storage. Packed-layout
reference evidence is the pinned Xenia-derived ReXGlue texture implementation;
the authored asset and original instructions are independent retail evidence.

`tools/prepare_itxd_luminance_mip_fixture.py` produces an independent
inverse-address oracle for eight textures and 70 levels, including the exact
three-level `beam1`, rectangular tails, deep tails, and 2048-pixel boundaries.
CPU and WARP/hardware readbacks compare every level, reject malformed
descriptors/allocations, and check cross-level mutation isolation. The five
base/family/mip CTests and four Python checks passed in the native build.

## Streamed audio token

The live skip/recap route then passed `beam1` but exposed an EXm0 reader
identity rejection in `20261001-122259Z-ed9f4891`. A second diagnostic run,
`20261001-122852Z-9e8b020f`, showed that all owner/handle/node fields matched,
while the producer passed initial token `300` for a correctly claimed block
whose original token was `401`.

Original `82342194` stores the first request token at stream state `P+2C`.
Preload requests at `8234220C..8234221C` preserve it when it is already set;
later refills at `823429D4` and `82342AB8` discard new return tokens.
Original `82342474` claims the next node by its reader handle, then publishes
that actual node into the request slot at `823424C4`. Therefore `P+2C` is
not the identity of every subsequent block.

The producer now passes the claimed node's own token after checking its mapped
extent. The existing copy guard still verifies the registered claim, reader,
owner, token, epoch, node length/address/state, callback, request table,
containing ring, and release gate. Original cancellation state is preserved.
Header and exact block catalog matching, sequence, sample progress, and
fresh/continuation checks remain intact. The failing body matches Springfield
outdoor ambience `spr_amb_ext_qd_01.exa.snu` (four channels, a one-sample
intro, then a 3,914-byte/4,736-sample first body block).

## Recording data capacity

Run `20261001-123335Z-960d6686` passed both fixes, then exposed an eighth-draw
capacity rejection while recording ordinary dual-textured geometry. Each
native draw owns a 1,624-byte expanded constant/mask snapshot. The old backend
counted these copies against the original `0x3000` command-buffer allocation.

Original `82459FB0` allocates GPU command backing and saves its byte capacity.
Query `8245A7E8` measures command cursor byte distances. Those bytes are not
the native snapshot representation. The native payload now has a distinct
1 MiB owned-data budget while retaining the original allocation metadata. Its
initial reserve remains 12/36 KiB; storage grows geometrically within the host
bound before a recording callback.
Its original CPU cache accounting continues to report the actual owned native
bytes, preserving eviction and replay checks. Vector growth must complete
before invoking any callback that can issue GPU work. The original cache
quota at `82CED950` is 15,000,000 bytes.

## Completed verification

`build/recap-live-fix/runs/20261001-124311Z-0df438be` ran the patched native
build on the visible desktop with copied stores and unchanged saved graphics
settings (3440x1440, original antialiasing). Its controller receipts confirm
the outro accepted Start, released its original decoder, and continued through
both recap pages into the next load. `beam1` uploaded all three levels. The
audio and recording paths completed and the game rendered Homer and Bart in
the Simpsons' living room.

Completed front-buffer captures at presentation 9106 and later show the
loaded room; `frame_scene_geometry_draws` is nonzero, the display accepted
the completed copies, and the frame timing CSV reached 20,497 presentations
and 350.389 seconds. The run ended only when the test deliberately posted
`WM_CLOSE` to its verified PID 70112 game window. Its final `Native window
closed` failure receipt denotes that explicit stop; there was no crash before
it. The movement probe is not credited as controller-response evidence.

Useful evidence:

- `after-recording-fix/native-frame-1301975.png` and its raw/JSON siblings.
- `stable-gameplay/native-frame-2599830.png` and its raw/JSON siblings.
- `game.log`, `frames.csv`, `route-inputs.json`, `launch.json`, and `closed.json`.

Both `build/native` and `build/native-release` game, input recorder, and direct
launcher builds are current. All 18 focused CTests pass in each build (7.54
and 7.10 seconds), including original completion/launcher isolation, EXm0 and
reader lifetimes, the new 250-check original reader claim regression, base
L8 and all 70 mip readbacks on WARP/hardware, original ITXD/stream ownership,
and five recording groups. Each recording-payload backend passes 7,308 checks:
24 expanded snapshots exceed both original command capacities, retain exact
pixels/depth after replay and caller mutation, and reject the host limit plus
one byte before issuing draw work. The reader fixture tests the original
reader contract; the live run provides the producer integration evidence.

Logs are `build/recap-live-fix/final-native-tests.log` and
`final-release-tests.log`. AOT verification passes 311 files with zero semantic
diagnostics. Original profile, save-index, and video hashes match the preserved
baseline in `final-primary-store-hashes.json`. The existing `.lnk` still targets
the GUI direct-launch helper with only `--first-mission-completion`; no startup
window or extra click was introduced. This test verifies the reported transition;
it does not establish an improvement in first-level gameplay performance.
