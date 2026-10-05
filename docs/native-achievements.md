# Native earned-achievement persistence and completion

Boot208 reached XMsgStartIORequest(appFB,message000B0008,length8) after the
requested ordinary Start and successful native profile/event setup. The missing
request was called from824316A8 at824316E4, with overlappedE2950D9C and
payload0203F800. No achievement was acknowledged by that run. The new service
records only IDs submitted by the original game for actual active native
profiles; it does not invent an unlock, save slot, campaign progress or menu.

## Original ABI and retained control flow

Original824316A8 packs two32-bit fields, count and record pointer, into its own
stack and calls the import with an eight-byte payload. Its caller827B4F48 uses
owner+24 for an array of eight-byte {slot,achievementID} records and owner+EC for
count: at most25 records fit. The owner's existing event is at overlapped+C.
The native adapter admits this app/message,1..25 records, aligned mapped
payload/overlapped/array, a real Event handle and no completion callback. It
rejects unsupported requests before publishing completion.

The adapter verifies the caller's actual Runtime-owned thread against the native
Windows thread ID, validates its error output and rejects interrupt-context
error handling. It snapshots all array entries before persistence and resolves
all session slots through immutable active profile-file leases. The loaded
image's title ID is45410809. A thread handle already in the native registry is
reused; the main thread receives one registry alias to its existing owned native
thread object when needed. No console thread/kernel object is fabricated.

After every requested record has been durably created or verified, the adapter
resets the real completion event, writes length0,actual caller-thread handle,
extended-error0,thread-last-error0 and result0, then signals the event. The
event, callback and application completion-context fields are preserved. The
import returns997, representing an immediately completed asynchronous request.
The original wrapper returns997; original82433670 sees completed result0 and
the original owner closes its event normally. CPU context except r3 and host
FP/LastError are preserved. No artificial delay or retained guest pointer is used.

Original function range SHA256 pins:

- 824316A8/0x80:7772e2b590a69e21d2eb3598897b4c7335021d9aacf3751dfa0586c6fab4e3d3
- 82433670/0x98:c911b5013e2cf8275c1b368c7e3a9843c526424e06c69466f5a5373b968ff52a
- 827B4DF8/0xC:fb89f8c6e54a6197b460711dd1452981ac22356cb24b5503b4d823425fc298cf

The read-only local reference supplies the message and completion ABI, not a
runtime backend or account source. Paths under K:/Simpsons/RexGlueCurrent:

- src/kernel/xam/apps/xgi_app.cpp,caseB0008:
  037db48250c383f05348f1792d9377c5950d657ea948cf74f7ee57fe565315cd
- src/kernel/xam/xam_msg.cpp,lines54..84:
  8b54a9eb94a7ea5947f66b55ee0eddc3939bc83e0026f72587f50508b79fb3b5
- src/system/kernel_state.cpp,lines1043..1084:
  cb08be1aa8bf8da6c3afb325892e4f14e2f02bc7ea69170ee0998e7b4df848f8
- include/rex/system/xio.h,overlapped layout:
  f743e1c96e86bc471661bbef67fd19ac5af1220083a503f4bfe0964cbd8b4431

## Durable native records

NativeLocalPlayers now owns achievement operations alongside its active profile
leases. Its existing directory-pinning implementation was factored into a
private PinnedDirectory helper shared by the two stores. Profile files and their
v1 format are unchanged. Achievements use a separate sibling directory formed
by appending .achievements to the absolute profile-store path. Thus the live
task profile uses build/mainmenu-profile-204.achievements when first written.

Each immutable file is named
<full-guid>-<eight-hex-title>-<eight-hex-achievement>.achievement. Its bounded ASCII
record contains the version header, full GUID, title,achievement ID and SHA256
of the preceding fields. The GUID remains the primary native identity; session
slot changes do not affect ownership. No timestamp or online-service claim is
invented. The actual filesystem records first publication time.

All batch slots resolve before the achievement directory is created. Existing
target records are validated before any new file is written and their read
leases are retained for the batch. Creation uses CREATE_NEW with exclusive
sharing, validates file identity and its single hard link, writes exact bytes
and calls FlushFileBuffers before success. Duplicate requests validate the
existing exact checksummed record. Actual concurrent final-name collisions are
reopened and verified; a still-exclusive writer can cause an explicit sharing
failure, which is not silently acknowledged.

An I/O failure can leave earlier records in a multi-record batch committed; the
batch then throws and is not acknowledged complete. Retry is idempotent. A
crash-partial final record fails validation rather than being repaired or called
earned. Failure cleanup targets only the newly created file through its handle.
Pinned NTFS directories reject reparse roots/ancestors and file hard links.
Only exact canonical requested filenames are accessed; this is not a directory
enumerator, achievement catalog or leaderboard implementation.

## Verification and remaining work

NativeLocalPlayerOwnership now checks actual persistence/reopen, duplicate
requests, profile/title/ID isolation, slot moves, independent sessions, concurrent
native writes, unchanged profile bytes, rejected inactive batches, corruption,
hard links and a real directory junction. The hard-link fixture uses an unpinned
destination directory; targeting the intentionally pinned fixture root was
rejected by Windows before the test could exercise the record reader.

OriginalNativeAchievementRequest runs the actual original event constructor,
824316A8 writer,827B4DF8/82433670 poll and827B4EE0 destructor. It checks five
host FP modes, CPU/host state, complete overlapped byte extents, real caller
identity, invalid buffers/requests/profiles, corrupted records and cancellation.
A separate native thread waits for the real event and reopens every newly
written record before reporting success. Cancellation snapshots use the direct
validated memory view, since ordinary PPC loads correctly reject a stopped
runtime. These are fixture corrections, not relaxed production checks.

The initial build caught a test auto-declaration type mismatch and omitted source
registration. Both are corrected. The new bridge is listed in
config/native_sources.json; AOT regeneration reports252 explicit unsupported
imports and311 generated files with zero semantic diagnostics.
build/native-achievement-registered-build.log and
build/native-achievement-fixture-build.log complete. The four targeted tests
pass in2.80s in build/native-achievement-tests-verified.log. The complete125-test
build passed in238.28s in build/native-achievement-integration-build.log.

Boot209 live acceptance: the original request contained slot0/ID00000020.
The bridge persisted one new156-byte record in the production profile store's
.achievements sibling, then completed event1F8 with actual caller-thread1FC.
The record was read back and the original game advanced to its visible
"No storage device selected" prompt. After normal A, the background saved-game
enumeration reached an unsupported type1 request at8285C470. Boot209 exited1.
This verifies achievement persistence and original completion in the live flow;
it does not establish saved-game support or arrival at the main menu.
