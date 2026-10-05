# Gloss runtime integration — replays 048/049 (2026-09-17)

## Implemented and built

Source `82019988` selects real offline artifacts VS `8201A07C` / PS `8201A6EC`.
The opaque `rigid` pass uses selected context `2B00`, 18 sampler rows, and a
544-byte private bank. Admission remains restricted to exact shader pairs,
original packet/pass metadata, owned resources and qualified draw state.
No shader alias, graphics stub, audio stub or successful guard bypass was added.

Private material ownership is exponent leaf13/word76 -> c50, custom lines
leaf14/word80 -> c49, scale leaf19/word124 -> c47, shadow leaf20/word128 -> c46,
and rim leaf21/word132 -> c45. Original private/shared usage masks, register
maps and storage offsets are checked against the original image by
`OriginalRigidMaterialConstants`. Existing rigid variants retain their maps.

The native PS bank is now 51 float4s (816 bytes); native recording snapshots
are 1336 bytes. Immediate commits copy only the selected material registers
into original per-object staging; recorded draws retain independent material
snapshots and apply the existing inherited-register mask on replay. UV1 is
consumed for gloss. The separate `PSRigidGlossDraw` adapter uses the gloss
interpolator layout and the existing qualified native depth conversion.

## Verification

- Full project build succeeded, including `SimpsonsNative.exe`.
- Focused final suite: **7/7 passed** (material maps/accumulation, shader evidence,
  material artifacts, shader GPU tests and mesh GPU tests on WARP/hardware).
- Gloss VS stream-output tests compare all 27 exported floats against an
  independent matrix oracle with identity, nonaffine and separately varied
  matrix banks, using the shared native mesh input-layout signature.
- Existing 1296 gloss PS draws per device still pass final-RGBA comparisons.
- New native gloss mesh tests run two real immediate draws and A/B/A recorded
  replay with different c50 exponents, checking exact RGB10A2, depth and stencil,
  material snapshot retention, state restoration and D3D11 error messages.
- The full suite ran **126/166 passed, 40 failed** in the current environment.
  Runtime/audio tests report mastering-voice creation failures; this is NOT a
  claim that the prior 166/166 baseline still passes.

Logs under `K:\SimpsonsNativeCopy\build`:
`gloss-full-build.log`, `gloss-focused-final.log`, `gloss-integration-ctest.log`,
`gloss-audio-diagnostic.log`.

## Live validation blocked before graphics

Both `K:\SimpsonsNativeCopy\build\automatic-startup\reach-game-048\result.json`
and `K:\SimpsonsNativeCopy\build\automatic-startup\reach-game-049\result.json`
report `Create output mastering voice (HRESULT 0x80070490)` before the menu.
The standalone audio test reproduces the default-endpoint failure. Failure
logging now records channels=0 (XAudio2 default), rate=48000, flags=00010000
(NO_VIRTUAL_AUDIO_CLIENT), device=<default>, effects=null, category=6, and the
actual HRESULT. The invalid-device negative test separately returns 80070057.

A standalone probe (`build/audio-probe/probe.cpp`, compiled with ole32/oleaut32/
xaudio2) measured the environment directly: `XAudio2Create` succeeds (S_OK);
`CreateMasteringVoice` on the default endpoint returns 80070490 both with
`XAUDIO2_NO_VIRTUAL_AUDIO_CLIENT` and with default flags; a masterless source
voice creation returns `88960001` (XAUDIO2_E_DEVICE_INVALIDATED);
`GetDefaultAudioEndpoint(eRender,eMultimedia)` returns 80070490; and
`EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE)` reports **zero active render
endpoints**. XAudio2 is healthy; this Windows session has no active render
endpoint. The negative test with an explicit invalid device separately returns
80070057, so 80070490 is not an argument-shape rejection. Audio services are
running; devices exist but none is currently an active render endpoint. No
system endpoint or driver was changed and no audio stub was added.

**No live gloss draw, presented gameplay frame, or character control is verified.**
After a real audio output endpoint is enabled, run a fresh replay directory
(050 next):
`python -B K:\SimpsonsNativeCopy\tools\auto_start_native.py --run-directory K:\SimpsonsNativeCopy\build\automatic-startup\reach-game-050`.
Keep rendered gameplay and character-control verification separate. Nonuniform
gloss shadow-tap weighting is still not independently qualified by these probes.
