# Burp charge rendering fix — September 27, 2026

The next reported run confirms this animated-mask fix is active and reaches a
different, immediate beam-effect failure. The follow-up repair and wider audit
are documented in [immediate-effect-crashes.md](immediate-effect-crashes.md).

The two reported runs ended with `Original static mono scene lost its reflected
owner or geometry profile` while special attack was held:

- `build/launcher-logs/Simpsons-20260927-172053-669Z-24888-0000.log`
- `build/render-tests/20260927-132217-247395/game.log`

The native mono mask pass rejected animated geometry: dispatch required a zero
bone count, and the downstream world callback, mesh upload and Boolean commit
also supported only static meshes. The original mono vertex shader already has
a qualified branch for four weights and a palette of up to 64 bones.

The bridge now preserves the original static/skinned branch, pose calculation,
material selection and draw loop. It uploads the original transposed palette,
sets the skinning Boolean and draws the animated mesh through that shader.
The shader does not fetch morph streams. Their original selection/cleanup
continues with ownership checks. Unsupported palettes over 64 bones remain
guarded. Mesh and constant commits must agree about whether skinning is active;
static and animated meshes share a bounded cache with distinct validation.

## Verification

Both `build/native` and `build/native-release` game and input-recorder executables
were rebuilt. AOT verification reports 311 files and zero semantic diagnostics.
All 17 focused tests in the development build and all six mono tests in the
release build pass. Logs and executable identities are under
`build/burp-charge-fix`.

`OriginalMonoMaskPass` executes the original dispatcher with real catalog and
camera owners. It covers static → animated → repeated animated → static draws,
mapped bones, alpha-mask positions, material skips, unchanged RGB/depth/stencil,
register preservation and cleanup. A separate original matrix-setter check
verifies transpose and dirty-mask behavior for 2, 9 and 64 bones. Hardware and
WARP renderer tests cover all 64 bones, four weights, invalid inputs, cache
separation and switching back to static rendering. Adjacent skin, depth,
post-filter, viewport, screen-bridge and controller tests pass.

The exact manual crash has not yet been reproduced automatically. The initial
charge tests and a reconstruction of all 592 logged controller changes reached
static mono draws without failing in the old executable. The reconstructed
route preserves mouse camera input, but its timing and starting save are not an
exact recording. The new original-code regression directly exercises the
previously rejected animated branch. Additional rejection diagnostics now
record the actual owner, material and bone-count values if another failure occurs.

The rebuilt game also completed that reconstructed route and continued to scene
2674 without a gameplay failure (`fixed-log-20260927-134246/result.json`). Its
logged mono draws were static, so this is a gameplay smoke check rather than
proof of the manual charge trigger.
