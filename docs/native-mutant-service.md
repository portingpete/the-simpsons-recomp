# Native mutex ownership at the original mutant imports

Boot067 reaches NtCreateMutant at **82B76614**, returning to82B76618, with
r3=0203F530 (handle output), r4=0 (unnamed attributes) and r5=0 (not initially
owned). The retained original wrapper82B765D8 selects null attributes when its
name argument is null, masks its ownership byte and passes stack+50 as output.
Its success path loads that handle; failure translates the NT status and returns
zero. The reached caller8231E9F0 requests an unnamed, initially unowned mutex and
stores its handle at **82E31BC8**. Original allocation and use remain AOT.

The companion release wrapper82B76660 writes **r4=0** before calling the import
at82B76670. It tests the returned NT status and reports success or translates
the error. The second console argument is not established as a previous-count
pointer. The native bridge therefore supports zero only and explicitly rejects
other values before changing ownership. [Xenia's threading entry points](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/kernel/xboxkrnl/xboxkrnl_threading.cc)
corroborate this three-argument create ABI and zero release control, while leaving
nonzero release semantics unresolved. They are not a shipped runtime dependency.

## Native behavior

`runtime/kernel_objects.cpp` creates an actual Windows mutant using dynamically
resolved NtCreateMutant and stores it in the existing KernelHandle registry with
type Mutant. The native NT signatures, including Windows' optional previous-count
output, are recorded in [PHNT's native declarations](https://raw.githubusercontent.com/winsiderss/phnt/master/ntexapi.h).
The release bridge passes nullptr for that host output. It returns the actual
native NT status, including non-owner rejection, rather than reporting a no-op
as success. Null create output returns invalid-parameter status; other invalid
or read-only outputs are rejected before OS allocation. Named attributes remain
an explicit unsupported boundary. Failed host-owner allocation closes the raw
handle; the checked registry owns successful allocations.

The existing NtWaitForSingleObjectEx and NtClose hold shared native handle leases.
Guest workers have dedicated native threads, so Windows supplies recursive
ownership, contention and abandonment when an owning thread exits. Those are
documented [Windows mutex behaviors](https://learn.microsoft.com/en-us/windows/win32/sync/mutex-objects).
Scheduler equivalence, named console namespaces and unknown release modes are
not inferred from them.

Build109 passes all 28 CTest suites (38.68 seconds). NativeMutantOwnership uses
the actual imports and OS objects to check recursive ownership, non-owner
rejection, bounded cross-thread handoff, abandonment on real thread exit, stale
handles, rejection before allocation/ownership changes, and guest close during
the real native wait import. No forced thread termination or mocked mutex is
used. See tests/test_mutants.cpp and build/native-mutant-109.log.

Actual boot068 creates native mutant handle14C, initially unowned, and advances
through the following original physical allocations. The next explicit failure
is XMACreateContext at LR8233E744. This proves the reached mutex creation path;
original application mutex release/teardown remains unverified. No game artwork
or geometry was drawn. Logs: build/hundred-ninth-build.log and build/boot-068.log.
Build109 executable SHA256:
`8fc3d7f2f7b6e65e239b0ac91282dc50ad6697ff5d1d8e683325d6aefa29eb2a`.

## Original byte evidence

The extracted image identity is SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Read-only checks using tools/analyze_poststart_integration.py's identity,
layout, span, sha and word helpers recovered:

- 82B765D8: .pdata size84, SHA256
  `3dbc1a6aaaf03c0d7b17e03ade67502f7cf1ac8373c4d751b30224b71b3a211d`.
- 82B76660: size3C, SHA256
  `75901d957d53ff63152e6f84d34eaaac8598942e7fa0f7c535c6bbff3cdc23ed`.
- 8231E9F0: size50, SHA256
  `18adf778651ecca8932d79edab75fc290be49dc0ebc4db4de12603e9356f8aeb`.

Instruction words: 82B7660C=57E5063E, 82B76610=38610050,
82B76614=4814D0F1, 82B7666C=38800000, 82B76670=4814D0A5,
8231EA08=48857BD1, 8231EA14=906B1BC8. These pin the wrappers and reached
request; they do not claim the console kernel itself has been disassembled.
