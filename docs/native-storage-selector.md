# Native storage selector

Boot210 reached82432E40/XamShowDeviceSelectorUI at LR8285CF64 after the real
Player save catalog returned zero records. Original8285CED8 supplies slot0,
saved-game type1, flags300, requested bytes0, owner+31C device output and
owner+33C overlapped output. It zeros the28-byte completion, including the
event and callback, and expects997. The audit traced the complete producer:
driver byte+4 independently supplies bit200, and the request's second boolean
independently supplies bit100. Original8285CC78 initializes driver byte+4 to0;
original82CA3A78 sets it. Original82CA2880 stores the second boolean at request+38,
and the public request82CA3968 explicitly supplies0. Whole processor82CA3D28
calls8285CED8 at82CA4000/LR82CA4004. The native import now admits exactly
flags0/100/200/300 for active slots0..3 and type1. Unsupported bits and nonzero
event/callback completions still reject. All admitted flags retain this native
platform's real modal folder choice; no console automatic-device policy is claimed.

The selector is an actual owned Win32 window with the real profile name,
configured folder, actual caller-available capacity, and Use this folder/A and
Continue without saving/B buttons. Enter accepts the focused/default choice;
Escape cancels. The owned game window is disabled during the dialog and its
client extent/rendering remain unchanged. Native controls use DPI-scaled layout
and a real Segoe UI font. No console UI, dummy HDD or selected-device object is
fabricated. The native installed-folder identifier is1, as in content enumeration.

The configured folder must already exist on a fixed local volume. Its ancestors
remain pinned. inspectNativeStorage reads real64-bit volume capacity and verifies
exclusive temporary-file write/flush/delete access, as documented in
native-save-catalog.md. Insufficient capacity disables acceptance. Acceptance
rechecks the actual folder/capacity and the same active full-GUID profile before
returning a choice. A failed access or changed profile is an explicit failure.
Available bytes are a snapshot, not a reservation or a claim that game payload
writing is implemented. No save or profile is created by this selector.

NativeControllers grants one host token exclusive UI consumption of the selected
controller. Ordinary game input queries report neutral input while preserving
actual connection availability. The token alone reads physical/keyboard/file
input; pre-dialog commands are discarded and held buttons must be released before
they can choose an option. Physical controllers keep their existing priority.
After close, queued UI commands are discarded and held buttons are suppressed
until release so a choice cannot also activate the game behind the dialog.

The current native scheduling policy is a modal call on the requesting native
thread. It keeps the original output pointers on that call's stack; no pointer
or callback is retained after return and no additional console dispatcher is
introduced. The request publishes997/pending and the actual caller-thread
handle, then waits for the real window choice. It returns997 with an already
completed record, length0, result/extended error0 for acceptance or1223 for
cancellation. The caller's event/callback/application context fields are
preserved. Window shutdown/Runtime stop retires UI/input ownership and throws,
without reporting a successful selection. Callback exceptions stay inside the
native window callback boundary and are rethrown on the requesting thread.

Notification9/true is published only after the actual window is visible.
After its window is destroyed and completion is published, notification9/false
is queued. Original8285C868 consumes those transitions: while owner+328 says
UI is open it returns15; after closing it reads the actual extended error and
updates owner+360 from owner+31C only on success. It maps cancellation1223
to return14 and clears its device fields itself. Those state transitions remain
original CPU code. There is no artificial notification delay or fabricated UI
event on a request that never opened a window.

ResourceAudit captures the original request before strict admission: raw caller,
user/type/flags, full64-bit requested bytes, actual configured folder identity,
owned-window and readable completion state. Output/thread/profile identities stay
in instance context. After the existing profile lookup, it updates the snapshot
before the active-profile guard. Unreadable completion fields remain unknown;
diagnostic reads never admit them, change CPU/output bytes or construct profiles.

Evidence: original image SHA256
6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0.
Original .pdata spans8285CED8/B0 bytes
eee54bd45a32197ebac9bf2efd32786e2c38c018ae2242c5f3e8e58357afb22a
and8285C868/F8 bytes
13f98980cab814d5b1d3e1baebd90bbd17aa1a1868e6e86883e9098e7121f3c5.
The local primary xam_ui.cpp reference (notification9, selector arguments)
has SHA2565c6a5f3c487e996863204b30dfb8d31d6260df961ff63d631ff7542722cf614a;
kernel_state.cpp (deferred result/extended-error/length/context contract)
has SHA256cb08be1aa8bf8da6c3afb325892e4f14e2f02bc7ea69170ee0998e7b4df848f8.
Both are under K:/Simpsons/RexGlueCurrent/src. Its headless dummy-device
selection, emulated objects and dispatch backend are not used or linked.

NativeStorageSelectorTests executes the original
constructor/request/poll with a real visible native window and file commands,
then checks cancellation, capacity refusal, control bounds/text, notifications,
CPU/host state and Runtime shutdown. NativeControllerTests adds exclusive UI
ownership, stale/held input and post-close release checks. The first build found
a missing noexcept on the fixture's substituted OS query; production UI code
compiled. The fixture signature is corrected. The next check correctly refused
an enormous request caused by the fixture retaining r4's upper32 bits. Original
8285CED8 uses a64-bit value before shifting by12, so the fixture now initializes
the full register instead of the generic32-bit invoke overload. Production size
checks were not weakened. OriginalNativeStorageSelector passes in0.47s.

The existing content and controller regressions pass in5.10s and4.68s. The new
test passes305 checks in the visual run, including a real native PrintWindow
capture whose control text and bounds are also inspected. The viewed image is
build/native-storage-selector-ui.png, converted losslessly from the native BMP
capture using System.Drawing (the default Python runtime has no Pillow).
All labels, full folder text, actual capacity and both buttons are visible.
Logs:build/native-storage-selector-tests.log (initial fixture failure and two
passing regressions), build/native-storage-selector-tests-verified.log and
build/native-storage-selector-visual.log. The complete126-test build passed
in234.34s:build/native-storage-selector-integration-build.log. Boot211 has
launched with the same real Player profile/content folder and a fresh input
channel. Boot211 live acceptance is now verified: its native selector displayed
the real Player folder and527509946368 available bytes for requested0. An actual
file-channel A selected it; result0/device1 and the paired UI-close notification
advanced the original code to XamContentGetDeviceData at8285CE58. The selector
itself succeeded; the game did not reach the main menu. The failed process later
required a scoped stop after its fatal unsupported import stalled shutdown.

The restrictive-check audit's first four-flag lifecycle run reached the actual
original request, real modal, paired notifications and both original polls for
each independent case. Its aggregate then failed an incorrect fixture assertion:
8285CED8 clears pending device+31C and its OVERLAPPED, while it preserves the
previous selected-device+360 until8285C868 publishes the new result. The fixture
now compares+360 against its pre-request value rather than expecting zero again.
All four independent cases also exposed an incorrect aggregate handle baseline:
the real completion publishes a Runtime-owned caller-thread handle in
OVERLAPPED+8. Original8285CDA0→8285B430 closes its two notification listeners,
writes bothFFFFFFFF sentinels, and leaves that completion thread owned by Runtime.
The repaired fixture checks those exact listener IDs are absent, exactly two
handles were removed, and the only introduced remaining handle is the actual
main thread. The initial failures remain in
build/restrictive-audit/storage-mono-audit-tests.log; passing rerun is pending.
