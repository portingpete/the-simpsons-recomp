# Recorded gameplay freeze — September 21, 2026

The freeze captured in `build/input-recordings/20260921-165726Z-91b72b59`
is fixed in both `SimpsonsNative.exe` and `SimpsonsInputRecorder.exe`.
The original recording and its starting save/profile remain intact.

## Cause and rendering fix

Original routine `82755FD0` draws a full-screen color fade. Its reached flat
shader path still entered Xbox command-buffer submission (`8244C450` and
`8244C8F0`) with a null console device. Reproducing the recorded keyboard route
with the old executable stalled at scene depth copy 529; the user's capture
stalled at 530. Input polling and presentation stopped while the window thread
and audio remained active.

The hook at `827560A4`, resuming at `8275625C`, now submits this fade through the
existing native screen renderer. It preserves the original alpha early-out,
query/fallback prefix, and register-restoring epilogue. Primitive 8 is a
three-corner RECTLIST: `(-1,-1)`, `(1,-1)`, `(-1,1)`. Expanding its fourth corner
to `(1,1)` covers the screen with a rectangle.

The bridge validates the reached camera, targets, flat declaration, shaders,
coordinates, and color. It implements the original blend/depth state and
publishes the corresponding engine caches while clearing retained immediate
geometry bindings. This change covers the observed flat fade path; the
query-dependent shader variant remains guarded.

Implementation: `runtime/engine_driver.cpp`, `runtime/engine_driver.h`, and
`config/simpsons.toml`. Generated AOT files were regenerated from configuration.

## Dialogue reached after the fade

Further movement exposed an independent qualification failure for Homer clip
`audiostreams/mr_xxx_0/d_homr_xxx_0003e6a.exa.snu`, header
`0300BB8040023029`. The existing qualification tool now includes this exact
source and regenerated its certificate: 143,401 trimmed frames and 29 blocks.
The source SHA-256 is
`7eb1c62b82ad4cb12d67a6180de43f8f5e3c246f7a57cfa0a2f72a42fd88ccec`.
All ten dialogue profiles were requalified. The new clip's maximum difference
from the independent decoder was `2.384185791015625e-07`; every block had enough
decoded samples. Parser and source ownership checks remain enforced.

## Verification

- The original-AOT screen bridge regression failed before the rendering fix.
  It now verifies the invisible early-out, all pixels of a half-opacity red
  fade, unchanged depth, ABI/cache/state preservation, invalid-color rejection,
  and a subsequent ordinary screen draw.
- Eight focused recording, controller, storage, screen bridge/backend and
  shader tests passed. The dialogue source test also passed. The hardware
  screen backend passed 7,732 checks.
- The final normal executable reproduced the recorded keyboard sequence,
  completed 61 fade draws, accepted additional movement and jump input, and
  advanced through scene depth copy 900. The newly qualified dialogue header
  was reached in this run. No gameplay/thread failure occurred; the harness
  then deliberately closed its own game window.
- Readbacks before and after the added controls show the changed camera and
  character view. Input polling and presentation continued through the run.

Evidence is under `build/input-freeze`: `baseline-1/result.json`,
`fixed-3-movement/result.json`, `fixed-3-movement/game.log`, `final-tests.log`,
`audio-tests.log`, `hardware-tests.log`, and `qualify-dialogue.log`.
Final image previews are in `build/input-freeze/final-previews`.

`build/input-freeze/reproduce.py` is a bounded diagnostic harness that restores
private starting-state copies and synchronizes this recording's keyboard
changes against scene depth-copy counts. It is not a general deterministic
playback implementation. Verification covers this recorded freeze and the
subsequent tested controls, not all level traversal.
