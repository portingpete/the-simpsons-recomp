# Native NTSTATUS conversion and original thread error

Actual boot083 reached `RtlNtStatusToDosError` through original82433B98,
LR82433BA8, on a native worker with statusC0000034. This followed a real
native file-open failure. The file path was not previously logged, so that
boot alone does not establish whether the missing file is optional or required.

`runtime/status.cpp` calls the actual Windows `ntdll.dll` implementation for
the input low word and returns its zero-extended ULONG in r3. Windows defines
this operation as conversion to the corresponding system error, with
ERROR_MR_MID_NOT_FOUND when no mapping exists; the native implementation follows
that service instead of inventing a local table. See Microsoft's
[RtlNtStatusToDosError documentation](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-rtlntstatustodoserror).
This is the native platform mapping policy, not a claim that every private
console status has a Windows equivalent. Existing unsupported services remain
explicit failures.

The import validates runtime ownership/cancellation, preserves other guest
context, and restores the host floating-point controls and Win32 last-error
value. It does not access guest memory or implement guest TLS. Original82433B98
still reads PCR+150 and, when zero, obtains the original thread pointer from
PCR+100 and stores the mapped BE32 error at thread+160. Its nonzero suppression
branch skips that write. Native Windows TEB layout is not copied into the game.

`tests/test_native_status.cpp` checks eight fixed error anchors and21 actual
Windows mappings across five host FP profiles, the whole import context,
cancelled/foreign-runtime rejection, and the actual original wrapper's write
and suppression branches. The wrapper's13 instructions are pinned to original
bytes. No successful guest file operation is fabricated by these tests.

File-open diagnostics now record the checked original ASCII request, native
status, access/share/options and caller before propagating the same failure.
They neither rewrite the path nor change file-open results. The runtime's
existing read-only asset containment and create/overwrite rejection remain.
Build, test and actual-boot results are recorded in `STATUS.md` after execution.
