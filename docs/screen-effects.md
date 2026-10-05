# Screen-effect passes — September 27, 2026

The subsequent charged-burp nonfinite-constant repair and its verification
limits are documented in [charged-burp-dof-crash.md](charged-burp-dof-crash.md).

The render-test run in `build/render-tests/20260927-173415-470133/game.log`
stopped after special attack was released:

```text
Unimplemented native engine graphics boundary 0x82455570, caller 0x827543C0
```

The [luma layer fix](luma-layer-crash.md) was active: eight
`[NATIVE LUMA DRAW]` passes rendered before the failure. The new failure is
the original depth-of-field pass `82754288`. It is called after every viewport
depth copy (`82751700`) and returns early until an effect activates it.

## Proactive audit

The native port keeps the console device null, so every unreplaced original
call into the SDK library fails when reached. `tools/audit_sdk_call_sites.py`
follows direct calls from the post phases (`82751778`), the post depth copy
(`82751700`) and the screen-effect command dispatcher (`8276E1E8`). It lists
every reachable function that still has unreplaced SDK calls. It does not
follow calls whose call site a hook already replaces. Before this work it found
these dormant passes, each of which would fail the first time gameplay
activated it:

| Pass | Original shader | Inputs |
| --- | --- | --- |
| `82754288` depth of field | `Dof_Xenon_PS` `821517A0` | scene, viewport depth |
| `82754C90` motion blur | `Blur_Xenon_PS` `82151C50` | viewport color slot 1 history |
| `82755508` bloom | `Bloom_Xenon_PS` `82151DB8` | scene, 64x8 corona query |
| `8276C930` fog | `Fog_Xenon_PSLinear/Exp/Exp2` | viewport depth |
| `8276FE60` saturation | `Sat_Xenon_PS` `821557C8` | scene |
| `82756268` letterbox | `Screen_Xenon_PSFlat` | none |
| `82755FD0` query overlay | `Screen_Xenon_PSModulatedFlat` `82152318` | corona query |
| `8276F268` LUT color grading | `LutColor_Xenon_PS_LutColor1..5` | scene, runtime 3D LUTs |

All of these except LUT color grading are now bridged. The audit now reports
only `8276F268` and its LUT builder `8276EAC8`.

## Bridge

`tools/generate_screen_effect_sites.py` derives `runtime/screen_effect_sites.h`
and the matching midasm hooks from the original instructions: 134 endpoints
across seven passes. Each SDK call records its operation and the argument each
of `r4..r7` is loaded with. `EngineDriver::screenEffectOperation` checks the
following at every endpoint:

- the pass frame and its saved return address;
- strict endpoint order within one scope;
- the active full-size camera;
- the exact arguments.

It then translates the call:

- Scalar, sampler and blend setters go to the effective render state.
- Resolves copy the camera color into the owned viewport textures.
- Shader, texture and declaration binds are checked against the original
  shared objects.

Pass gating, layer normalization, constant math, vertex stores and layer
resets remain original AOT code. The generator's `--check` mode is registered as
`OriginalScreenEffectSites`.

The passes also write shader constants and fetch-constant clamp fields directly
into the console device. Each `lwz r11,-13576(r31)` device load now returns a
native guest shadow block instead. The generator declares exactly which fetch
fields and constant lanes each block writes. The bridge poisons those fields
before the block runs. At the next endpoint it checks that the block wrote
them, then publishes the clamp fields as sampler state. Draws read PS c0..c9
from the shadow, as the console would from its device state.

Other changes:

- Viewport color slot 1, the 1280x720 motion-blur history at +0x398000, now has
  a native owner. Its descriptor comes from a captured header dump.
- The per-frame binding reset accepts every cached effect pixel shader.
- The gameplay overlay's flag-bit-0 variant was previously rejected. It now
  uses the bridged query prefix and draws with `PSModulatedFlat`.

## Shaders

`renderer/screen_effects.hlsl` transcribes the nine records.
`tools/analyze_screen_effect_shaders.py` pins:

- the record and executable hashes;
- the handle registrations and CPU owner spans;
- the reviewed HLSL (2472 mutation checks).

`tools/xenos_pixel_reference.py` is an independent interpreter for the
original microcode. It follows the pinned `ucode.h` conventions:

- co-issued vector and scalar operations;
- scalar a/b operand selection;
- constant-scalar operand negation applied to both operands;
- the legacy multiply rule;
- export constant masks.

It agrees with the hand-derived luma formula to 5e-8 over 300 random cases.
`NativeScreenEffectShadersWARP/Hardware` render every native shader and compare
it with the interpreter. Worst errors are 3.6e-5, or 1.3e-4 for Sat, from
bilinear subtexel quantization. The tolerance is 2e-4. A single flipped tap
offset moves the output by 6e-4 to 2.4e-3, so such a transcription error would
fail the test.

## Verification

- `OriginalScreenEffects` executes each original pass with fixture layers after
  real startup:
  - an idle frame, which draws nothing;
  - Sat, Blur priming and drawing, Bloom, Fog (all three variants) and DOF;
  - the letterbox, including a transparent case;
  - the query overlay: ready, skipped and flat-fallback cases;
  - a rejected wrong caller;
  - a binding reset.

  Pixels are compared with oracles over the constants the original CPU wrote.
  Depth, caches, post-state and layer resets are also checked. Camera clears
  leave depth at the far plane, so DOF and Fog take their far branches here.
  `NativePostFilter*` covers nonzero depth.
- `NativePostFilterWARP/Hardware` add nine effect cases with uniform inputs,
  plus stage and geometry rejections.
- All 264 development tests pass (`build/luma-fix/tests-full-suite-effects.log`).
  The 17 affected release tests pass (`build/luma-fix/tests-effects-release.log`).
  AOT verification reports zero semantic diagnostics.
- `OriginalViewportColorTextures` previously required slot 1 to be rejected. It
  now qualifies slot 1 like the other snapshot slots.

## Limits

The manual trigger has not been replayed. The reconstructed 17:34 route
completed through scene 4046 without a failure, but diverged from manual play
and activated no effect. The special attack needs a filled meter.

LUT color grading (`8276F268`) is still unported. It builds 3D lookup textures
at runtime (`8276EAC8`: create, lock and reorder a source raster, then unlock).
It samples them with one of five pixel-shader variants, so it needs
volume-texture support in the native backend. If a level activates color
grading before that work is done, it will fail at `8276F45C` or `8276F51C`.
