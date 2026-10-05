# Bridge mono-pass blend compatibility

The 2026-09-27 normal-launch log stopped at scene 1053 with
`Native mono mesh blend state is unqualified`:
`build/launcher-logs/Simpsons-20260927-041513-161Z-41964-0000.log`.
The log did not include the rejected blend values. This was a renderer
exception; the gameplay timing and bounce-pad changes are unchanged.

The mono pass previously admitted only disabled replacement blending or
enabled source-alpha blending, both with expanded blending disabled.
It now supports the following original states:

- Disabled replacement (`00010001`).
- Enabled replacement (`00010001`), with explicit ONE/ZERO factors.
- Source-alpha/inverse-source-alpha for RGB and alpha (`07060706`).
- Source-alpha/inverse-source-alpha for RGB with alpha replacement (`00010706`).
- Either canonical expanded-blend flag for these states.

The original mono pixel shader produces exact RGBA one. The supported
equations therefore produce an exact endpoint with either expanded flag;
this does not extend expanded-blend support to other shaders or equations.
The backend retains the requested blend descriptors and color masks.
Rejected combinations now report enable, packed word, expanded flag and mask.

## Verification

The new regression failed against the previous renderer on both hardware
and WARP (software D3D11). With the correction, both pass 59,446 checks,
including pixel readback for all supported modes, expanded flags and masks
0/8/15, depth/stencil preservation, and restoration of native render state.
Invalid flag values and unsupported equations remain rejected.

Both native and release game/recorder builds succeeded. Release checks for
the original gameplay clock, collision cadence and millisecond clock also
pass. Build receipts are in `build/bridge-blend-fix/`.

Direct-mission traversal reached and crossed the bridge on the diagnostic
build without reproducing the exception. Reconstructing the user's input
from the launcher log did not preserve the route, because the log lacks
input timestamps and the original starting save snapshot. Those runs do
not establish the exact rejected combination from the user's crash.

The final corrected executable also completed a bounded private smoke run at
uncapped FPS in `build/bounce-pad-fix/drive-uncapped-20260927-043632Z-757e91/`.
It bounced from both yellow marshmallows, reached the upper landing at
`[23.969, 4.309, -33.054]`, and rendered the bridge area. The log records mono
draws through count 8 without a rendering exception. The only failure marker
was `Native window closed` when the diagnostic session ended.

The final rendered position was `[28.869, 4.313, -42.450]`, presentation 23179,
preserved in `final-bridge/native-frame-3188398.png` and its raw capture under
`captures/`. Controller timing overshot the bridge entrance during this run;
a complete crossing was not verified. `smoke-summary.json` records that limit.
The user's exact rejected blend tuple was not recovered.
