# Luma layer crash — September 27, 2026

The render-test run in `build/render-tests/20260927-164236-827066/game.log`
stopped at viewport scene 2086 with:

```text
Unimplemented native engine graphics boundary 0x82455570, caller 0x82770C2C
```

Special attack (B) was held from scene 1960 and released at scene 2085, one
frame before the failure.

## Cause

Phase one of `82751778` calls `82770AF0` every frame. It keeps three screen
layers at `82CF24F0`, `82CF2520` and `82CF2550`: add, subtract and lerp. Each
layer has a color and a two-value curve. Screen-effect command case 8
(`8276E428` → `8276CE78` → `827709A0`) accumulates them. When no layer is
active, the pass returns early. That is why it had not appeared before.

When a layer is active, the original code:

1. resolves the scene color to viewport texture `82DFE360` (selector 0, no clear);
2. sets half-pixel offset, cull, depth, alpha test and the `0x10001` copy blend;
3. binds VS `821529C8` and PS `821559D8` (`Luma_Xenon_PS`), the resolved texture,
   linear clamp sampling and the float2 screen declaration;
4. uploads each layer as PS c0..c5, or stores zero color for an inactive layer;
5. draws a four-vertex float2 triangle strip, then restores blend and depth;
6. clears the layers.

No native bridge existed for these calls or for the four direct stores to the
console device. The first call to reach a guard was the resolve.

`Luma_Xenon_PS` computes Rec.601 luma. It evaluates the same curve for each
layer, using that layer's `gb.x/gb.y` values. It adds the add color, subtracts
the subtract color and lerps toward the lerp color. Output alpha is the
constant one.

## Repair

- `renderer/luma_shader.hlsl` transcribes PS `821559D8` issue by issue, reusing
  the shared post-filter packed output adapter. `tools/analyze_luma_shader.py`
  pins the record. It also pins the 30 issue-slot signatures, the literal bank
  and the handle registration. The CPU spans of `82770AF0`, `82770870`,
  `82770720`, `82444DF8` and the phase-one dispatcher are pinned as well, along
  with the reviewed HLSL. Its self-test rejects 449 mutations.
- `drawPostFilter` accepts the original four-corner strip only with this
  shader. It validates live constant lanes: every color lane and each curve's
  `.xy`.
- 23 midasm hooks in `config/simpsons.toml` replace the 19 SDK calls and the
  four device-shadow blocks. `EngineDriver::lumaOperation` enforces their exact
  order, frame and arguments. It recomputes each layer's original activity test
  and checks the branch the CPU took. Layer tests, normalization, strip stores
  and resets remain original AOT code.
- An inactive layer rewrites only its color register; its previous curve stays
  in c1/c3/c5. That matches the original. A zero color cancels a saturated weight
  exactly.
- The shared shader object at `82CF24E4` must match its source record, byte for
  byte. The per-frame binding reset now accepts that object when it is the last
  cached pixel shader. Otherwise the next frame would reject it.

## Verification

- `OriginalScreenBridge` executes original `82770AF0` with real startup
  shader, declaration and viewport owners. All-inactive layers do no work. A
  wrong caller is rejected before any work. Three active layers resolve the
  scene exactly and draw once. The output matches an independent reference
  (512,256,767 → 675,280,487 in 10-bit codes). Depth, caches, post-state and
  the original layer reset are checked. A subtract-only pass cancels retained
  curves. A binding reset then accepts the cached luma shader.
- `NativePostFilterWARP/Hardware` draw the strip on WARP and hardware. They use
  three constant banks, including saturating curves, point and linear
  sampling, and two blend words. Output is compared to a CPU oracle within one
  10-bit code. Six invalid strip, shader and constant cases are rejected.
- All 259 development tests pass (`build/luma-fix/tests-full-suite.log`). The 11
  affected tests pass in development and release. AOT verification reports 311
  files and zero semantic diagnostics.

The full suite also exposed three stale tests from the earlier immediate-effect
work. They are updated. The pinned translated-material list now includes the
three projected immediate records; the population is 64 compiled and 192
untranslated. The shadow camera depth probes now expect the corrected constant
depth offset: 0.5 and 0.75 plus `D0`, which is −0.0004. The old windows
assumed the swapped slope offset.

## Limits

The manual trigger has not been replayed. Both the pre-fix and fixed builds
completed the reconstructed route of 695 input changes without a failure
(through scenes 4619 and 3667). Neither activated a layer. Scripted charge and
release cycles at level start did not activate one either: the burp meter was
nearly empty. Artifacts and hashes are in `build/luma-fix/verification.json`. To check in play, fill the
special-attack meter, hold B/right click/K and release. The pass logs
`[NATIVE LUMA DRAW]` for its first eight draws.
