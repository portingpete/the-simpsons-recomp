# Reach-game314 — mono draws and the post-filter boundary

The reach-gameplay goal is active. Runs314a and314b complete the original
static mono pass and submit two native mono meshes. They then stop at the
unsupported color resolve82455570 called from82773E34 in post-filter82773D40.
Neither sustained gameplay nor responsive character controls is verified.

The mono adapter retains original CPU world/shared transforms, material
commit, Boolean selection and all four stage upload copies. The typed mono
upload masks are A8=7FFFC and AC=3FFFC. Static mono mesh submission uses the
original CPU mesh loop and the separately qualified white-output shader.
Native mesh tests cover depth comparisons, reversed depth, color mask, cull,
immutable uploads, binding ownership and state restoration on hardware/WARP.

Nine focused mono shader, mono mesh, z-prepass mesh and skin mesh checks pass
in build/reach-game-314-tests.log. Regeneration and the game build pass in
the corresponding314 and314b logs. The full suite was not rerun.

Run314b still has98 presented scene frames and2571 rigid/skin/sky draws: the
two mono draws occur in the next, unfinished frame. Its post-frame, texture
headers, viewport rows and private color readback are diagnostic inputs,
not completed front-buffer gameplay evidence. Slots0/3/4 are RGB10A2;
slots3/4 share original pixel storage but have different pitches. The native
implementation under development uses separate resolve snapshots for the
bounded ordered post-filter calls, without claiming general memory aliasing.

The original target setter8243DED0 branches to8243D230. Its stage-zero path
calls8243D198, resetting the viewport from82069FA4 (0,0,65535,65535,0,1).
8243CE80 then clamps it to the selected target extent. The original push/pop
helpers subsequently set depth endpoints to1,0. Disassembly is saved in
build/reach-game-314-post-viewport-sdk.txt and the post-targets log.

The independent tools/observe_native_scene.py observer can request captures
without sending input. Its sustained-scene classification requires30 live
presents spanning10 seconds and never establishes gameplay or control.
Run314b reports rendering followed by failure; the fatal result takes
precedence. The replay script is still waiting for a later movie event.

Post-filter shader qualification, viewport copy tests and driver integration
are in progress. They have not yet passed an integrated live run.

The post-filter analyzer now passes550 mutation checks against the exact
shader records,30 issue slots, original CPU spans and reviewed HLSL. This
does not replace backend or live validation. The two inline sampler writes
are CLAMP, not MIRROR: rotate11/value1 sets0x800 in bits10..12 (enum2),
and rotate14/value1 sets enum2 in bits13..15. Min/mag enum1 is LINEAR.
The pending native tests cover the production clamp/linear combination.

## Integrated validation

The314c build passes. All seven focused post-filter/backend/shader/material
and viewport-color tests pass after correcting the viewport fixture to use
an already-qualified endpoint clear color. The backend tests run on both
hardware and WARP. Shader tests each cover181291 checks/499 draws; the
material catalog covers248 records and5098 ownership checks. Logs are in
build/post-filter-qualification and build/reach-game-314c-*.log.

Run314c caught a runtime guard mistake before the first copy: lastFunction
records the most recent callee, not the enclosing function. The owner now
checks the exact retained pre-call LR (DF4/F18/FD4/74098), together with the
original frame, saved caller, nonvolatile registers and arguments. Its
updated test passes. Run314d completes the full-size first copy, then rejects
the first post draw because native effective state still contains mono
depth/cull state. The original SDK setters write only console shadow memory.

Fourteen additional exact setter adapters now publish those original scalar,
sampler and packed-blend operations to the native owner. The packed blend
word is authoritative: CB80 replaces it independently of the scalar enable
shadow, and A010(false) disables blending by writing10001. The post adapter
therefore applies the packed equation while retaining the original scalar
shadow. There are43 exact post hooks in total.

Run314e submits the first640x360 post draw, then exposes a second owner
mistake: later copies called the general camera validator while an auxiliary
target was intentionally bound. They now use the camera identity retained
by the first copy, with exact scope/phase/global-camera and auxiliary-source
checks. General camera validation is unchanged. The regression now requires
the precise backend actual-OM-mismatch error instead of accepting any error.

Run314f completes all four copies and four draws (alpha mask, resize, filter,
and final1010B glow composite). It presents216 scene frames and5914 scene
draws across30.789 seconds before a newly reached audio guard stops the
worker. The completed front capture2014577 includes4 post draws and2 mono
draws, shows the objective overlay and intact Homer geometry, and is previewed
without color correction in build/reach-game-314f-preview.

The new failure is in engine_audio_owners.cpp beginResident: original
function823424D8, caller82342C8C, resident headerE76131DB with bytes
0300BB800000C800, blockE76131E3, sequence1, bank/profile/loop all0.
Run314f exits at55.98 seconds. The observer reports rendering followed by
failure, never verified gameplay/control. Audio qualification remains open.
