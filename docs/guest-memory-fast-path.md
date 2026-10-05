# Guest memory access model (2026-10-04)

Every original load/store used to validate the page permission table, the runtime
owner, the window/stop flags and the write barrier before touching memory. That cost
more than it looked: removing all checks (unsafe, measurement only) took the 1440p
first-mission benchmark from about 80 to about 120 FPS. The access model is now split by
who is touching guest memory.

## Recompiled original code: `PPCGuestPointerFast`

`SIMPSONS_FAST_GUEST_MEMORY` (CMake, default ON) force-includes
`runtime/aot_fast_pointer.h` into every generated chunk *before* `ppc_context.h`, which
renames `PPCGuestPointer` to the out-of-line `PPCGuestPointerFast` in `runtime/runtime.cpp`.

- Mapping and permission violations are raised by the CPU. The 4 GiB window is reserved
  `PAGE_NOACCESS` and every mapping is committed with exactly its guest protection
  (`Runtime::map`, `allocateVirtual`, physical aliases).
- A vectored handler (`guestFaultHandler`) turns a fault on guest memory into the same
  thrown `Failure` the checked path raised: it redirects the faulting thread to
  `PPCGuestFaultThunk` as if the faulting instruction had called it, so the exception
  unwinds through the guest frames to the existing catch sites (and to tests). The thunk
  realigns the stack behind an RBP frame. Faults below 0x10000 first try the null-device
  scratch demand map (`mapZeroPage`).
- Explicit checks that remain in the fast function: the unbound data-import page (0x82000,
  validated slot by slot by the checked slow path) and the exact guest-write barrier.
- `Runtime::threadObjectType` moved to 0x7FFF0400, a page that is never mapped, so a guest
  dereference of the opaque thread-object type faults in hardware instead of needing a guard.
- Do not inline the access at the ~470,000 generated sites: that grows the executable from
  85 MB to 125-160 MB and runs slower than the call (measured; `SIMPSONS_INLINE_MEMORY_ALL`
  experiment).

## Native hooks and tests: `PPCCheckedGuestPointer`

Validating hooks must throw `Failure` (never fault) for an invalid guest pointer and keep
the destructors of their frames, so they keep software checks, now cheaper:

- A 256-entry software TLB (`guestAccessTlb`, 1 KB, L1 resident) replaces the 1 MiB
  permission table on the common hit. `GuestPageAccessTable` clears the matching entry on
  every permission store, so a hit is the same fresh permission the table would give.
- The runtime-owner, base, window and stop-flag checks per access are gone.

## Cancellation

Per-access polling is replaced by polling at every original function entry
(`PPC_TRACE_ENTRY` checks `PPCStopRequested`, raised by `Runtime::requestStop`, which the
window close also calls) and in every wait (the stop event). `stopThreads` bounds the
join at 20 s and then terminates the process, so a guest thread spinning in a call-free loop
cannot hang shutdown.

## Write barrier

Stores check `guestWatchBlocks[address>>16]`, the number of armed pages in each 64 KiB
block (and, for a block's first page, in the previous block, so one load covers a store that
straddles the boundary). A nonzero count takes the exact per-page check
(`Runtime::noteGuestWrite`). Counts are maintained wherever a page flag changes state.

## Contract changes in tests

`NativeMemoryContract` and `NativeInlineMemoryContract` now assert the new contract:
function-entry cancellation instead of per-access, hardware page protection (the harness
opens the protected fixture page around its bulk copies), and fault text without the access
width. Register, memory, partial-write order and FP state still match the reference bodies.

## Measured

Interleaved A/B, main-thread CPU per frame (`tools/benchmark_first_mission.py` reports
`main_thread_cpu_ms_per_frame`, far less sensitive to desktop load than FPS):
control 12.2 ms, this model 10.9 ms (-10%). Upper bounds from unsafe experiments, same scene:
no checks anywhere 8.9 ms; fast guest pointer with unchecked hooks 10.2 ms.
