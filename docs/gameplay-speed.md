# Gameplay speed above 60 FPS

The original update at `82690D60` rounded elapsed time to whole display
refreshes and required at least one refresh per update. Removing the rendering
wait therefore made gameplay run faster above 60 FPS.

The native hooks in `runtime/timing.cpp` now sample the real 50 MHz timebase
once per gameplay update. They replace only the refresh-quantization block,
then resume the original pause, time-scale and scaled-clock code. Rendering
can still use 60 FPS, 120 FPS or uncapped pacing.

- Fractional frame durations drive movement and animation.
- Integer input timers carry fractions across updates instead of rounding
  each frame independently.
- Disabled updates consume their clock sample, preventing a catch-up on resume.
- The original maximum step (normally five refreshes) still limits stalls.
- Clock construction and reset follow the original lifecycle. A one-tick
  minimum protects existing code that divides by the gameplay delta.

`NativeFrameRateTests` checks ten seconds of scripted elapsed time at
30, 60, 120, 144 and 240 FPS, irregular frame intervals, integer timer carry,
stalls and reset. It also executes the translated original update and verifies
published clocks, pause, slow motion and register preservation.

Rebuilds require regeneration through `tools/recompile.py`; generated C++ is
not edited directly. Restart the game after rebuilding to use the fix.

## Verification (2026-09-26)

- Both `build/native` and `build/native-release` game/recorder builds passed.
- `OriginalNativeFrameRate` (17,760 checks) and `NativeMillisecondClock` passed
  in both builds. AOT verification reports zero semantic diagnostics.
- A read-only probe of the live game's original clock fields measured
  1.001 gameplay seconds per wall second at 100.7 uncapped updates/second,
  and 1.000 at 59.9 updates/second with the 60 FPS cap. Each measurement covers
  eleven seconds within a bounded 24-second Land of Chocolate run.
  The preserved old executable advanced 1.341 gameplay seconds per wall second
  at 79.4 uncapped updates/second, reproducing the speedup.
- The separate 32-second uncapped movement smoke completed without a failure
  marker. Enemy interactions made position-only before/after comparisons
  unsuitable as a speed measurement; the clock probe above measures time
  directly.

Probe script and individual receipts: `build/game-speed-fix/`.
The full regression suite and long level playthroughs were not run.

## Bounce-pad collision follow-up (2026-09-27)

The first yellow pad could miss Homer when he jumped onto it. Character
movement used fractional frame durations, but the world scheduler at
`827A55C0` still integrated collisions 33.3667ms ahead and skipped collision
passes while gameplay caught up.

`SimpsonsNativeWorldCollisionStep` now supplies the scheduler's smoothed frame
duration to its small-frame collision step. This uses the same elapsed time
as Havok's frame target. The original large-frame substep loop remains intact.

The scheduler regression executes the translated original code and checks
collision cadence and elapsed time at 60, 120, 144 and 240 FPS, pause, large
steps, and register preservation. It failed on the previous build and passes
with the correction. The fixture replaces heavy collision processing with
a simulation-time recorder; the live test below covers the pad itself.

A private uncapped session reproduced the fall through the first pad with
the previous executable: Homer descended below the pad without a trampoline
contact. With the correction, the same approach and jump triggered Homer's
launch and landed him on the pink platform. Temporary contact tracing was
removed after verification.

- Before: `build/bounce-pad-fix/drive-uncapped-20260927-035300Z-ba2223/`.
- After: `build/bounce-pad-fix/drive-uncapped-20260927-035139Z-224317/`.
- Both folders contain controller commands, position samples, raw captures,
  frame times and contact logs. Test sessions use private profile copies.

Final trace-free builds of the game and input recorder succeeded in both
`build/native` and `build/native-release`. `OriginalNativeFrameRate` (24,576
checks) and `NativeMillisecondClock` passed in both. The final native build
also completed the pad jump at 60 FPS, landing on the pink platform; receipt:
`build/bounce-pad-fix/drive-60-20260927-035608Z-01beaf/`.
