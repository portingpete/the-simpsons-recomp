# Optional external-music availability on native PC

Actual muted boot142 reached XamGetSystemVersion while original828099A8
requested optional platform music control. The preceding graphics milestone
and its exact evidence are frozen in build181/checkpoint native-im2d-overlay-142.
The user then requested a longer loading run. The executable had exited on an
explicit missing import after about eight seconds; it had not used its timeout.

Original828099A8 reads the game music owner at82E06E30, retains its original
no-owner return, stores the request's low byte at owner+4D, and tail-calls
82B794B8 when that byte is nonzero or82B79590 when zero. The object's direct
method82808E50 makes the same low-byte intent store and two tail calls.
Both game methods remain original AOT code.

The two platform wrappers choose legacy or newer console service sequences
using the system-version build field. They ultimately request applicationFA,
message7001A through82B790E8 and XMsgStartIORequestEx, with a three-word
record. The legacy sequence also queries7001B through82B79238 and
XMsgInProcessCall. Existing listener evidence describes this optional music
control path in docs/native-notification-audio.md. The original82B790E8
wrapper converts HRESULT failures to Win32-style errors before returning.

The native PC port currently has no integrated external-music controller.
Both specific platform service boundaries now return ERROR_NOT_SUPPORTED
(50), preserving the original game caller's intent stores, return interpretation
and subsequent execution. This error supplies no claim of successful playback
control, platform music ownership, notification publication, or desktop-media
state. Native game audio remains a separate implemented service. The adapter
does not call the Windows audio session APIs or change other applications.

Generic XamGetSystemVersion and message imports remain explicit failures for
unqualified callers. The adapter does not invent a console version to select
one of the legacy/newer branches. Existing Xenia and UnleashedRecomp sources
also identify a no-argument system-version ABI, but their constant-zero stubs
do not establish a faithful version policy for this game. DarkRecomp was read
as a reference only and remains unchanged.

Verification in NativeConfigurationTests executes both original game methods
for zero/nonzero and high-byte-only requests, checks the exact single-byte
object publication and Win32 unsupported result, verifies the absent-owner
branch, preserved nonvolatile/stack/LR state, unchanged legacy claim/restore
fields, no new resources, and continued rejection of the generic version import.
Original source evidence: build/im2d-upload/external-music-evidence182.json
and external-music-original182.txt. Build182 passes85/85 suites in150.72
seconds. The final configuration fixture, including original yield checks,
passes65,767 checks. Actual muted boot146 was allowed180 seconds and advanced
past music control and scheduler yield; it exited after about8.7 seconds at
the unimplemented movie-plane allocation caller8282E9B8. It did not exhaust
its loading allowance. These results do not establish complete loading or
gameplay.

Primary research references:

- [Xenia system-version ABI and explicit stub status](https://github.com/xenia-project/xenia/blob/master/src/xenia/kernel/xam/xam_info.cc)
- [Xenia XMP application dispatch](https://github.com/xenia-project/xenia/blob/master/src/xenia/kernel/xam/apps/xmp_app.cc)
- [UnleashedRecomp import implementation](https://github.com/hedge-dev/UnleashedRecomp/blob/main/UnleashedRecomp/kernel/imports.cpp)
