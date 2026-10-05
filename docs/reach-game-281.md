# Reach-game281 — native particles, gameplay still unverified

The active goal is **Reach in-game**, including sustained rendering and responsive
character control. Runs278/279 produced one malformed scene frame and then hung
inside console DrawVertices. They were not successful gameplay runs. Run280
replaced that hang with an explicit particle frontier failure. Run281 implements
the reached particle draw and advances to the next unsupported direct draw.

## Current result

`build/automatic-startup/reach-game-281/result.json` reports `success:false`.
The first scene has 153 geometry draws (131 rigid, 21 skin, one sky). The image
is visibly incorrect: vertically inverted scene/HUD, flat pink background and
black/green geometry. The first native particle submission then completes:

`[NATIVE PARTICLE DRAW] emitter=E2EEF580 particles=2 draws=1 original_cpu_upload=true`

Next failure: texture call from `8276AFD0`, within original `8276AF78`, which
draws a rotated screen sprite through original CPU helper `8276ACC0`. Do not
allowlist that SDK caller and silently retain another texture. Implement its
actual engine boundary. The simple branch uses the **existing exact original**
Screen_Xenon records VS82152880 / PS82152708; the query branch uses PS821536F0
and needs separate support. `build/native-process-sampling/after-particles281-disassembly.txt`
contains its body. No native game process remains running after run281.

## Implemented this goal turn

- Startup verification now requires three advancing scene captures spanning at
  least one second. A first scene receipt is retained separately on failure.
  Character control is explicitly not inferred from rendering.
- Chocolate shader generation builds from source again. Corrected normal/tangent
  inputs, dot3, co-issued sin/cos inputs, scalar export destination, predicate
  jumps and non-predicated texture fetches using original shader bytes.
- Direct particle inventory pins VS821570E0 and PS82156770, includes all72 VS
  issues and four predicate EXEC blocks, and correctly binds literals248..255.
- Particle HLSL is generated offline and compiled by FXC. The runtime does not
  decode or interpret shader instructions.
- EngineParticles owns real CPU staging, resumes original82773394..82773658 to
  fill64-byte particle records and update their UV animation/flags, then draws
  native quads. ITXD resolves the actual `ray` texture (64x64 BC1, three mips).
- Native draw preserves overlapping particle order, original alpha test,
  RGB10A2 blend quantization and 20e4 depth policy. Native bindings are restored.
- Reached normal variant is supported; projected/dual/oriented variants reject.

Relevant files: `runtime/engine_particles.*`, `renderer/particle_draw.*`,
`tools/analyze_particle_shader.py`, `tests/test_particle_shader.py`,
`tests/test_particle_backend.cpp`, particle hooks in `config/simpsons.toml`,
and `runtime/engine_materials.cpp` first-profile capture/dispatch.

## Validation

- Mandatory AOT regeneration:311 files, zero semantic diagnostics.
- Current build: `build/reach-game-282-build.log`, SimpsonsNative and particle test.
- Focused CTest:4/4 pass in `build/reach-game-282-focused-tests.log`.
  Includes29 startup-evidence tests,11 chocolate tests,9 particle inventory tests,
  and WARP/debug-layer particle geometry, color, depth, overlapping draws, alpha
  rejection, binding restoration and invalid-extent rejection.
- Run281 live evidence: `build/automatic-startup/reach-game-281/game.log`;
  original CPU and decoded native particle bytes under `captures/particle-native`.
- Earlier full build succeeded. Full CTest before particle addition:171/179 passed,
  recorded in `build/reach-game-280-all-tests.log`. Eight failures remain:
  NativeRigidPacketOwner, OriginalDacPcm, OriginalDacGain, NativeMemoryContract,
  OriginalThreadExitStatus, OriginalIm2DNativeDraw, NativeOriginalMaterialArtifacts,
  OriginalEngineResourceBridge. Several are caused by pre-existing permissive
  zero-page/null-device/unsupported-binding behavior; do not claim a clean suite.

No original assets or reference repositories were changed. This workspace has no
Git repository. Generated output was regenerated from source, never hand edited.
Numerical/GPU parity across every shader branch remains unverified. The particle
test uses a controlled identity-projection quad, not a console capture oracle.
