# Original thread exit status: bounded audit before checkpoint 184

The native runtime does not expose completed worker status to the original
`GetExitCodeThread` helper at **82433328**. Its guest KTHREAD completion fields
remain at their initialized zero values while `threadMain` returns the actual
result to Windows. The original movie shutdown loop continues while this helper
reports **259 / STILL_ACTIVE**. This is a concrete missing native service
contract and a strong explanation for run 152's sampled shutdown loop. The
sample alone does not prove the queried handle's live value or establish that
this is the only remaining blocker.

This audit changed only this document and `build/thread-exit-status/*`.
No runtime, test, generator, config, shared build, or original/reference file
was changed. A proposed patch must follow preservation of checkpoint 184.

## Original byte evidence

Disassembly comes directly from `analysis/simpsons.pe` with the existing
`SimpsonsDisasm.exe`, using image base **82000000**. Exact bytes, hashes, and
source identities are retained in `build/thread-exit-status/original-evidence.json`
and `audit-evidence-index.json`; `.txt` files contain the corresponding original
instructions. Some windows deliberately include padding or adjacent instructions.

The helper at 82433328 takes a thread handle in r3 and an exit-code output
pointer in r4:

1. **82433344–82433348:** reads the thread type from BE32[820007BC], then calls
   **82CC29E4 / ObReferenceObjectByHandle**, using its own stack for the retained
   object pointer.
2. **8243334C–82433354:** a negative NTSTATUS goes to the original error path;
   otherwise r3 becomes the retained KTHREAD address.
3. **82433358–8243336C:** reads **BE32[KTHREAD+4] & 0xFF**. If nonzero, reads
   **BE32[KTHREAD+0x140]** as the exit code. Otherwise supplies **259**.
4. **82433370–82433378:** stores that code through the original output pointer,
   calls **82CC29A4 / ObDereferenceObject**, and returns Boolean 1.
5. **82433380:** the failure path calls 82434EC0, a tail branch to 82433B98,
   then returns Boolean 0. Preserve this original error conversion path.

The completion test is the low byte of a **big-endian word**, physically at
KTHREAD+7. Writing byte 1 at KTHREAD+4 would not satisfy it. The exit code is
a full 32-bit value; zero is a valid completed result.

In **8232B0A8**, r3 points to a thread wrapper whose first word points to its
descriptor. The descriptor's first word is the handle. At **8232B0E0**, it calls
82433328. A successful result of **259** returns **1** at **8232B0F8**, leaving
the handle intact. A successful different result calls the original close
helper at **8232B108**, zeros the descriptor's handle at **8232B114**, writes
descriptor+8 = 2, and returns **2**. An absent wrapper returns 0. The existing
failure/no-handle branch also sets state 2; the native bridge must not redirect
valid queries into it to escape the loop.

In **82375C88**, the decoder owner first receives owner+0x48 = 4. At
**82375D28–82375D34**, it polls the wrapper at owner+0x24 and branches back
while the result is 1. The body may service its original queues once, then
calls **82B76B98**, the original wrapper around **NtYieldExecution**, before
polling again. Yielding does not change the exit-status predicate. Cleanup of
the decoder's subordinate objects follows only after the loop exits.

This is the same owner slot used in the original worker-start path:
**82375738** forms the `VideoDecodeThread` string address **821BAC0C**;
**8237592C** loads owner+0x24; **82375938** supplies worker function
**82375580**; **82375954** supplies the name in the creation options; and
**82375960** calls **8232A920**, the thread creation wrapper. The outer shutdown
chain is **8282D998 → 82373738 → 82375C88 → 8232B0A8 → 82433328**.

The raw direct-branch scan found six callers of 82433328:
82329F90, 8232B0E0, 827AE27C, 82B6CA70, 82B73934, and 82B7399C. A bridge
inside this SDK helper therefore needs general owned-thread semantics, not a
movie-specific success value. The scan does not claim completeness for indirect
callers.

## Current native lifetime and the gap

`Runtime::initialize` binds **820007BC → 01000400**, an opaque native thread
type token. `Runtime::initializeThread` initializes the worker's zero-backed
KTHREAD page, including its ID, TLS, stack, priority and affinity fields, but
does not publish a completion word or an exit status.

`Runtime::createThread` owns a `GuestThread` in the thread registry. It creates
the host thread behind a gate, duplicates its native handle into a shared
`KernelHandle`, sets `guestObject` to the KTHREAD address, and exposes a guest
handle through the handle map. `ObReferenceObjectByHandle` checks that handle
and the requested type, then retains the same shared object in
`objectReferences[KTHREAD]` with a counted reference. It also supports the
current-thread pseudo-handle. `ObDereferenceObject` releases that counted
reference. Neither operation updates KTHREAD completion fields.

The worker runs original AOT code. The original startup routine 82439460
preserves its return value and calls **ExTerminateThread at 824394B8**.
The native import throws `ThreadExit{code}`; `threadMain` catches it, or takes
the ordinary AOT return code, decommits the no-longer-live guest stack, prints
the exit result, sets its diagnostic `finished` flag, and returns the result
to Windows. It never writes KTHREAD+4 or +0x140.

The KTHREAD/TLS area deliberately survives completion while any guest handle,
explicit object reference, or in-flight shared owner remains. Reaping requires
both a **signaled native thread handle** and sole registry ownership. The
destructor joins the native thread before closing its handles. Existing tests
explicitly demonstrate that `finished == true` can precede native thread
termination during host TLS teardown. Therefore `finished` is not a valid
substitute for native termination in the proposed bridge.

The missing field update is not fixed by that retention policy: a retained,
unchanged zero completion word causes the original query to keep producing
259 even after the OS thread exits. The polling path then never reaches its
normal handle-close branch. Existing runtime/lifecycle tests verify the **host**
exit code and ownership, but do not invoke this original SDK query afterward.

The read-only RexGlue reference also names KTHREAD+0x140 `exit_status` and
sets its header `signal_state` at exit. That corroborates the field meanings;
its publication ordering is not a synchronization proof for this runtime.

## Recommended narrow bridge after checkpoint preservation

Replace only **82433358–8243336C**, resuming at **82433370**. The six exact
words are:

```text
81630004 556B063F 4182000C 81630140 48000008 39600103
```

Retain the original prologue, typed `ObReferenceObjectByHandle`, negative-status
branch, output store, `ObDereferenceObject`, and epilogue. At bridge entry r3
is the retained guest object, r31 is the caller's output pointer, and LR is
**8243334C**. Obtain a local shared owner from the existing counted
`objectReferences` entry under `handleMutex`, require a nonzero reference count,
matching guest address and Thread type, then release that mutex before querying
Windows. No new handle, thread, or completion registry is needed.

Use **WaitForSingleObject(native, 0)** to distinguish still-running from fully
terminated. On WAIT_TIMEOUT, supply r11 = 259. On WAIT_OBJECT_0, use
**GetExitCodeThread** to obtain the actual 32-bit result. Unexpected wait/query
failures should be explicit failures, never fabricated completion. Retained
shared ownership prevents native handle closure and KTHREAD reaping during the
query. These Windows APIs supply the native completion and exit-code contract;
the diagnostic flag is not consulted.

Preserve the skipped block's register contract: r11 receives a zero-extended
32-bit code; CR0 is EQ for nonsignaled and GT for signaled, with SO copied from
XER as in the original `clrlwi.`. Preserve r3, r31, LR, the stack, and all other
guest state. Preserve host last-error and floating-point state around native
calls according to the runtime's existing bridge convention. Do not write the
guest output early or balance the counted reference twice; the retained
original instructions perform those actions.

A terminated thread may legitimately return 259. Preserve that exact result
and the signaled-state CR0; the original caller's decision to continue polling
that code must not be silently changed. Windows documents both this ambiguity
and the zero-duration wait used to distinguish termination.
[GetExitCodeThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodethread),
[WaitForSingleObject](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject).

This bridge avoids introducing concurrent writes to guest completion fields.
Adding stores in `threadMain` alone would be insufficiently specified: original
loads/stores are currently volatile accesses, not a coordinated C++ atomic
publication pair, and publication before the host TLS epilogue would disagree
with the runtime's established termination boundary. A full field-publication
solution would need exit status written before a release completion store,
matching acquire access in every reader, byte-order correctness, and a defined
relationship with native handle signaling. Updating only the writer or copying
the reference implementation's store order does not provide that contract.

## Required focused verification for the later patch

- Pin all six bridge words, the retained reference/store/dereference/error
  instructions, and original poll comparisons/branches against the image.
- Execute the actual original 82433328 and 8232B0A8 AOT bodies using an owned
  suspended or gated running worker: query success with 259, no close, and
  balanced object references.
- Release and join real workers returning **0, 42, 0xFFFFFFFF, and 259**;
  check exact output bits, the helper's Boolean result, and CR0/SO. For 259,
  separately verify signaled native state and preservation of the original
  poll behavior.
- Reuse the existing host TLS-exit barrier: `finished` true but native handle
  nonsignaled must remain active; after the barrier releases, obtain the final
  status. Cover the `ThreadExit` path as well as ordinary AOT return.
- Exercise invalid/closed handles, wrong object type, pseudo-current-thread,
  output faults and native-query failures. Retain original error conversion,
  unchanged outputs on reference failure, and no invented successful result.
- Concurrent repeated queries and a handle close after a retained reference
  must not invalidate the in-flight native handle or KTHREAD. After the final
  reference is released, existing reaping and slot-reuse tests must still pass.
- Prove the poll closes a terminated non-259 worker exactly once and sets its
  original descriptor state to 2; do not bypass the movie's remaining cleanup.
- In the next muted game run, record the queried handle/native ID, native wait
  state and exit code at the first completed query, then verify the original
  loop exits and report whatever happens next. Continued gameplay or correct
  front-buffer composition is not implied by this fix.

## Run 152 evidence and the remaining uncertainty

The preserved run log creates **handle 0x1B4 / native thread 9572**, names it
`VideoDecodeThread`, and later records its exit with **status 0**. Its PCR is
01060000, implying KTHREAD **01061000** under the current creation layout.
The sampled native thread list at **18:10:44 UTC** no longer contains 9572.
Thread **22896** is sampled in 82433328 with the original shutdown call chain
listed above. Its program counter, source line and full stack are preserved in
`build/native-process-sampling/run152-sample1.json`.

The sampler did not capture guest r3/the queried handle or the KTHREAD field
bytes. Thus the audit establishes the faulty completion mechanism and the
matching original movie-worker path, but does not claim a direct observation
of `handle 0x1B4 + native signaled + guest completion zero` in one transaction.
That single diagnostic in the later bridge is the smallest remaining runtime
confirmation. No additional game launch or process inspection was performed
for this audit.
