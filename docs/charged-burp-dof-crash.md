# Charged burp: reciprocal DOF constants — September 30, 2026

The latest reported first-mission run is
`build/render-tests/20260930-175618-692932/game.log`. It stopped near
presentation 5326 / depth-copy scene 5295 with `Screen-effect constant is
nonfinite`, after five luma draws. The user identified the action as releasing
a charged burp. This run used 1080p internal rendering, 4x SSAA and uncapped
pacing. Earlier nearby logs that ended with `Native window closed` were normal
window closes.

The original DOF CPU pass `82754288` calculates its focus depth at `82754564`
and focus slope at `827545A8`. A zero focus distance produces infinity in
PS `821517A0` constant c1.z and zero in c1.w. A zero focus range produces an
infinite c1.w. With both distance and range zero, c1.z is infinite and c1.w is
NaN from 0/0. These are legitimate outputs of the original reciprocal math.
The native renderer rejected every nonfinite live lane before submitting a
draw. The original shader uses the older multiplication rule in which a zero
operand annihilates infinity, so these inputs can still produce finite pixels.

`OriginalScreenEffectTests` now invokes the actual CPU pass with zero focus
distance and zero focus range. The unrepaired renderer failed the first case
after 6,451,349 checks with:

```text
Screen-effect constant is nonfinite: shader=821517A0 c1.z bits=7F800000
```

The renderer now accepts infinity only in the DOF focus coefficients c1.z/w.
It also permits NaN c1.w only when c1.z is infinite, matching the combined-zero
case. The original shader's saturated focus maps that NaN to zero and retains
finite center taps. NaNs in other lanes or paired with a finite focus depth,
infinite tap radii, and infinite live constants in other effects remain
rejected. `dofLegacyProduct` in `screen_effects.hlsl` preserves the original
zero/denormal multiplication rule when applying the focus slope. Original CPU
requests, coefficients, pass gating, layer resets, taps and blending remain
intact. Invalid-constant diagnostics retain the shader identity, lane and raw
bits for any subsequent user failure.

The independent original-microcode shader fixture now includes infinite focus
depth with a zero slope, both signs of infinite slope, combined infinite depth
and NaN slope, exact focus-depth pixels, fully blurred surrounding pixels and
the sharp far-plane band. The
three focused shader checks passed on WARP and hardware, including the pinned
shader/CPU evidence and 2,473 mutation checks. Results are recorded in
`build/latest-crash/shader-tests.log`; combined-zero checks are recorded in
`build/latest-crash/combined-zero-shader-tests.log`. `NativePostFilterWARP/Hardware`
also cover accepting these coefficients and rejecting unrelated NaNs or infinite tap radii.
The original CPU test preserves the derived coefficients and checks the actual
draw, pixels, state and request reset. Its effect counter snapshot now covers
all seven passes; previously its letterbox/overlay assertions indexed beyond
a five-element snapshot.

The exact manual crash has not been reproduced. A reconstructed input trace
retained all 1,292 keyboard/mouse changes but did not activate the luma/DOF
passes. Six scripted B charge/release cycles also did not activate them; the
captured HUD showed a nearly empty special-attack meter. These are inconclusive
gameplay attempts, not verification of the manual trigger. The user will test
the real charged-burp release in the rebuilt game. All test processes are
closed, and the automated runs used copied executables and private save/config
stores.
