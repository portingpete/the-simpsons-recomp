# Native offline player identity

Boot204 used the real profile created by the application's existing CLI and
stopped at the missing XamUserGetXUID import. The original caller82431F08 moves
its output argument to r5, requests mask7 in r4, and calls82CC25A4. Its0x50-byte
function range has SHA256
8a2b7c482d84311ec34a77dabcd078f9f81c52da10717516afd9ac8c5b7f3ed1.
The enclosing827B2788 routine publishes the returned identity into its own
player record only after a successful query. The native adapter leaves this
original selection and association logic intact.

## Native ownership and ABI

NativeLocalPlayers::identity returns zero for an empty session slot. An active
immutable profile-file lease supplies the final64 bits of its canonical v4 GUID
as a stable native equality key. The full GUID remains the primary identity and
filename; the existing checksummed v1 record format is unchanged. The UUID
variant ensures the key cannot be zero. Listing, creation and activation check
the complete bounded store for distinct GUIDs with equal keys. A rejected
activation preserves the previous slot and lease. This key is a native offline
identifier, not an Xbox account credential or a claim of online authentication.

The import currently admits only the observed all-profile-types mask7. It
preflights the eight-byte output, writes the key in original big-endian order,
and returns HRESULT success. Empty slots return ERROR_NO_SUCH_USER as HRESULT
with zero output; invalid slots return E_INVALIDARG with zero output. Null
output returns E_INVALIDARG. Unsupported masks, unmapped outputs, cancellation
and foreign runtimes fail before publishing any result. Only r3 changes in the
CPU context; host floating-point controls and GetLastError are restored.

The actual original82431F08 wrapper converts these HRESULTs to Win32 status.
Return/output conventions were checked against the read-only local primary
reference K:/Simpsons/RexGlueCurrent/src/kernel/xam/xam_user.cpp, lines232..272,
SHA2566ca9296c6410e96cbdf5307a74f297d893abc0ba1a0bdaa6b8562205212625f7.
No reference kernel, account database, interpreter or graphics backend is run.
Unreached type-specific/online masks are not inferred from that reference.

## Validation and limits

NativeLocalPlayerOwnership exercises persistence, slot movement, independent
sessions, exact identity bytes, actual file immutability, rejected key collisions
on creation and imported-file activation, and retention of the previous lease.
OriginalLocalPlayerQuery exercises real wrapper execution, empty/active/invalid
slots, big-endian outputs with surrounding sentinels, five host FP modes, full
CPU-context preservation, LastError, unsupported masks and cancellation.
NativeLocalProfileCommandLine verifies the supported durable-profile CLI.

build/local-identity-regenerate.log reports311 AOT files with no diagnostics
and257 explicit remaining unimplemented imports. The focused build succeeded;
build/local-identity-tests.log passes all three tests in2.55s. The full build
completed successfully: build/local-identity-integration-build.log passes
123/123 tests in233.33s. Live identity handling,
settings, save creation and main-menu acceptance remain to be demonstrated.

## Boot205 and native display-name lookup

Boot205 advanced through both identity queries after a single ordinary Start.
The next missing import was XamUserGetName, reached from original827B25E8 at
LR827B263C via the tail wrapper82431878 (instruction48890C8C). Slot0 requested
16 bytes at E1A5BC54. Original827B2660 uses the same16-byte query for comparison.

The new name import admits this length and copies the real active immutable
profile's name plus NUL. Native profile names are1..15 printable ASCII bytes,
so no truncation is needed. Bytes after the terminator remain unchanged. An
empty slot writes one NUL and returns Win32 ERROR_NO_SUCH_USER. Invalid slots
and null outputs return ERROR_INVALID_PARAMETER without writing a buffer.
Unsupported lengths, unmapped buffers and cancelled/foreign runtimes fail
before outputs. Host controls and the CPU context except r3 are preserved.
The reference named above, lines345..367, supplies the bounded output/status
conventions; its automatic default account and network users are not adopted.

The bridge test covers empty/active/invalid slots, maximum-length names,
terminator/extent sentinels, host controls, rejected outputs and cancellation.
It also executes actual827B25E8: the original function performs its own identity
query, publishes the name at object+34 and clears object+44, with the remaining
record and restored call frame checked. An initial fixture assertion forgot
EngineCpuCalls'0x100-byte frame; the corrected assertion compares the actual
pre-call stack. The import implementation was unchanged by that correction.

build/local-name-regenerate.log and build/local-name-build.log complete;
256 imports remain explicit unsupported boundaries. The focused test rebuild
and build/local-name-tests-verified.log pass ownership/query/CLI3/3 in2.48s.
The preceding123/123 full suite predates this name import. Boot206 is the live
retry with the same real profile and content root. No save/menu result yet.

Boot206 subsequently accepted the name lookup and entered the TV transition.
The next boundary was the missing handle-based NtCreateEvent service, documented
in native-event-service.md. Both local-profile query imports therefore have
live original-flow evidence; save creation and main-menu acceptance do not.
The combined event/name build subsequently passed124/124 tests in242.35s,
recorded in build/native-event-integration-build.log.
