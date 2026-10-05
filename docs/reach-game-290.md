# Reach-game290 — visibility queries execute; gameplay remains unverified

The active goal is **Reach in-game**, including sustained rendering and responsive
character control. Run290 presents one malformed scene frame, completes particle
and screen-sprite work, and executes five original corona visibility queries.
It then stops at `Unknown or stale dynamic native buffer ID` during camera reset.
This is not a successful gameplay run. No native game process remains from run290.

## Changes since reach-game281

- Direct rotated sprites retain the original CPU trigonometry and quad builder.
  The existing exact Screen_Xenon shaders render the simple branch. Unready
  queries follow the original no-draw branch without selecting a pixel shader.
- Sprite batch setup, query texture binding, expanded-blend reset and emitter
  cleanup now use native state ownership. Particles retain the original inherited
  blend-enable value and clear their texture at the original completion point.
- Viewport construction owns the original 64x8 RGB10A2 query texture and 64x64
  backup texture, with exact header, physical allocation and retirement checks.
- Corona producer VS82153278 / PS82153460 and consumer PS821536F0 have pinned
  complete records and static HLSL. The producer retains the original CPU list
  walk, 36-byte vertex construction, readiness publication and list clearing.
  Its nested 17x17 depth samples calculate the original radial visibility weight.
  Native separate resources preserve the original scene tile and both copy
  destinations without console GPU emulation.
- Skin texture-transfer setup now initializes the skipped pass-handle arithmetic
  registers. The previous guard expected a stale value tied to one heap layout.
- Material inventory includes239 records. Compiler tests now account for the
  already implemented sky/chocolate/168F8 shaders and the three corona shaders.

## Evidence

Run290: `build/automatic-startup/reach-game-290/result.json` and `game.log`.
The first scene still contains153 geometry draws (131 rigid,21 skin,one sky).
The subsequent query receipt reports five entries and original CPU vertices.

Mandatory AOT regeneration produces311 files with zero semantic diagnostics.
Run290 build succeeds; four focused CTests pass in
`build/reach-game-290-focused-tests.log`: corona GPU, original screen bridge,
material ownership and native material artifacts. The extended bridge exercises
original producer/consumer ABI, empty-list behavior, ready bytes and scene/depth
preservation. Corona GPU tests pass on WARP and hardware for visible, hidden,
equal-depth and weighted partial visibility, untouched pixels, backup coverage
and the consumer's RGBA modulation before alpha blending.

The earlier171/179 broader-suite result is historical. Its native material
artifact failure is now resolved; the entire suite has not been rerun. Permissive
low-page mapping, null-device paths and skipped streamed texture bindings remain
known defects. Scene orientation/material output and character control remain
unverified. Original assets and reference trees remain unchanged.
