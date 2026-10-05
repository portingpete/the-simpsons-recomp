# Native thread initialization

Actual boot 013 reaches `ExCreateThread` from `0x82434EC8`, with default stack,
startup `0x82439460`, entry `0x82B734C8`, parent-stack argument `0x0203F770` and
flags 1 (initially suspended). Boot 014 creates that native thread, then reaches
the thread-type import used by `0x824330A8` before setting priority/resuming it.
No game worker execution was established by boot 014; failure teardown resumes
and cancels the still-suspended host thread.

Boot 016 executes the first original worker startup handshake and creates a
second worker (entry `0x82CAC720`), then stops at `KeSetAffinityThread` from
`0x8243353C`: KTHREAD `0x01045000`, logical mask 2, previous-mask output
`0x0203F6A4`. Both workers exit on cooperative cancellation.

`0x82439460` receives entry/argument in r3/r4, runs original thread attach
callbacks, invokes entry(argument), runs original detach callbacks, and calls
`ExTerminateThread`. `0x82B734C8` consumes the parent-stack startup packet and
publishes its handshake before calling the requested worker. These original
functions remain AOT code; the native runtime does not replace that handshake.

Each native worker has an independent PPC context, guest stack, KPCR, KTHREAD
and static/dynamic TLS. Stack defaults come from this executable's verified
0x40000-byte configuration. Host stack reservation is 64 MiB to support native
call depth. New static TLS copies the original template. Shared dynamic TLS
indices are zeroed across registered thread slots on allocation/free.

The native thread is created suspended while its guest records, handle, ID and
entry are installed. `NtResumeThread` reports the real previous suspend count.
Original termination unwinds with a dedicated exception, preserving the exit
code. Unimplemented imports and host exceptions in workers remain failures;
the first worker failure cancels the runtime and wakes native object waits.
Stacks are decommitted when their worker exits. Teardown resumes suspended
workers for cancellation and joins them before freeing guest memory.

Typed references check the actual native handle type and retain that object
independently of the guest handle. Thread-type identity is opaque: attempts to
read an unimplemented type descriptor fail rather than exposing invented fields.
Balanced dereferences release retained references; an unmatched dereference
fails. Priority changes apply to the real Windows thread in the normal process
priority class. Current mappings cover -2 through +2, increments clamped to the
variable-priority limits 1/15, and saturated +/-16 increments. Nonsaturated
clamping retains an effective +/-7 increment; saturation retains +/-16. Both
use native idle/time-critical thread priorities, without changing the process
to realtime. Intermediate effective increments +/-3 through +/-6 still fail
pending an exact mapping. Semantics were checked against Microsoft's
[KeSetBasePriorityThread documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/nf-ntddk-kesetbaseprioritythread).

Original wrapper `0x824330A8` maps Win32 +/-15 to kernel +/-16; query wrapper
`0x82433128` reverses that mapping. Boot 017 requests increment 10. A suspended
native worker probe confirmed NtSetInformationThread class 3 rejects increments
3,6,7,10 and their negatives, accepts +/-16, and GetThreadPriority reports +/-15
for those extremes. The guest increment is therefore retained separately from
the native API's compressed priority representation. Queries validate the actual
native priority before returning that retained value. Tests distinguish an
increment 10 clamped to +7 from true +16 saturation, and exercise the low limit.

Native delay requests preserve absolute/relative 100 ns intervals and scheduler
yielding for zero duration. A direct local `NtDelayExecution(TRUE, &zero)` probe
returned `0x40000024` (`STATUS_NO_YIELD_PERFORMED`) when no peer was ready. This
informational yield outcome maps to successful completion of the requested zero
delay, following the completion distinction in
[KeDelayExecutionThread](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kedelayexecutionthread).
APC enqueue/delivery is still unsupported: alertable calls use the native empty
queue; an unknown native APC wake fails explicitly. Nonzero waits also listen
for the runtime cancellation event, so worker failure does not strand a sleeper.

Affinity uses the verified Xbox three-argument ABI: thread object, new mask,
optional previous-mask pointer; the result is NTSTATUS. Single-bit masks for
six logical CPUs map deterministically onto CPUs allowed to this host process.
The OS affinity is changed and PCR current_cpu (+0x10C), processor_mask (+0x110),
and KTHREAD current_cpu (+0xBF) remain consistent. A newly created thread starts
on logical CPU 0 unless an explicit creation mask selects another CPU. Hosts
with fewer allowed CPUs share them modulo the allowed count. Multiple-bit masks
fail until a guest CPU migration policy is implemented. Unknown thread objects
fail rather than returning a fabricated success. Local reference evidence:
`xboxkrnl_threading.cpp:294`, `xthread.h` KPRCB/KTHREAD fields, and actual boot 016.

Meaningful native tests execute an AOT fixture on a real worker: initial
suspension, independent stack/TLS/identity, resume count, argument delivery,
return status, object references, real priority changes and cancellation of a
still-suspended worker. These support the platform service and do not prove
original gameplay or all concurrent PPC semantics.

Remaining limitations include running-thread suspension, APC delivery,
additional scheduler APIs, and inherited CPU reservation-granule/ABA semantics.
The bounded lifecycle fixtures below verify native thread churn, not gameplay.
Unsupported creation flags and entry addresses fail explicitly.

Boot 018 passes affinity and the original high-priority worker setup, then
reaches `KeQuerySystemTime` from `0x82328260`. The native service writes one
checked BE64 UTC timestamp from GetSystemTimePreciseAsFileTime: 100 ns intervals
since 1601-01-01, matching the
[kernel time format](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kequerysystemtime-r1).
This wall clock remains separate from the monotonic 50 MHz simulation timebase.
Tests bracket the value with native UTC queries to check epoch/units/endianness.

## Bounded worker lifetime and allocation reuse

`runtime/threads.cpp` now reaps completed workers at the next `createThread`
attempt, under `threadMutex`. Eligibility requires BOTH a signalled native thread
HANDLE (zero-timeout wait) and `object.use_count()==1`. The sole remaining owner
must be the thread registry; guest handles, explicit `ObReferenceObjectByHandle`
references and in-flight service shared_ptrs all prevent reaping. A `finished`
flag is diagnostic only: C++ thread-local destructors and native thread teardown
may still be running after it is set. Destruction always joins the native HANDLE.

Reaping removes the PCR/KTHREAD/TLS mapping and its dynamic TLS-base registration
together under `vmMutex`, then erases the thread entry and closes its native
thread, creation-event and duplicate guest-object handles. A retained exited
thread keeps its identity/TLS records; its already-decommitted stack is reusable
independently. The reaper never decommits an old stack address, because another
worker may already occupy it. Reaping is deferred until another creation attempt
or runtime destruction, so a final unreferenced record may remain until then.

Allocation uses first-fit scans while holding `vmMutex`: 16 KiB thread records
in `[0x01040000,0x02000000)`, and stacks aligned to 64 KiB in
`[0x02100000,0x08000000)`. Both mapped Regions and reserved virtual Allocation
intervals block reuse. Stack sizes retain the existing 4 KiB rounding and
16 KiB minimum. The legacy Runtime nextThreadArea/nextThreadStack fields are no
longer consumed or advanced; no Runtime header/state additions were required.
Memory accounting continues to use actual mapped extents.

Creation is transactional until guest entry is enabled. A new native worker
starts suspended and also waits behind a per-worker event. All records, TLS,
affinity and native/guest handles are prepared first. Output pointers are checked
again under `vmMutex`; native resume for a nonsuspended request still leaves the
worker behind the event until creation commits. Thus an early native resume
cannot execute guest code during rollback. Only nonthrowing endian output stores
follow release of the gate, while the same memory lock protects their mappings.
The registry lock covers publication and serializes reaping/lookup.

On partial failure, any published registry/handle entry is removed, the gated
worker is cooperatively cancelled, resumed and JOINED, then its stack, record and
TLS registration are removed. Mapping failures after host commit but before
Region publication decommit the unpublished interval as well. Unsupported
multi-CPU creation affinity is rejected before allocation. No TerminateThread is
used. An impossible native join failure is fatal rather than freeing a context
that a host thread could still access. Stop still blocks new creation, wakes
waiters, resumes suspended workers and joins before Runtime releases memory.

## Standalone lifecycle fixture and parent integration

`tests/test_thread_lifecycle.cpp` links production threads.cpp,
thread_objects.cpp and kernel_objects.cpp against the actual Runtime/PPCContext
declarations. Its small harness supplies real VirtualAlloc-backed guest memory,
checked pointers and a fixture AOT dispatch slot, without linking the original
game or runtime.cpp. Native CreateThread, affinity, duplicate handles, resume,
wait, object references, reaping and teardown execute production code.

Coverage:

- 512 sequential native AOT workers, alternating immediate/suspended start,
  verify exit code, identity, static/dynamic TLS and reuse of the first slots.
- 128 workers from two concurrent creators exercise publication/reaping locks.
- A guest handle, then an explicit object reference, then an in-flight shared_ptr
  each retain the KTHREAD/TLS while its stack is reused.
- A C++ thread-local destructor waits on a native event after `finished=true`.
  With registry-only ownership, creation must still preserve that record until
  the native thread HANDLE signals. A running worker with a closed guest handle
  also retains both its record and live stack.
- Mixed mapped/reserved obstacles test first-fit selection and smaller stacks.
- Six injected failures cover PCR/stack commit before bookkeeping publication,
  incomplete initialization, registered TLS followed by affinity publication
  failure, exhausted guest-handle IDs after duplication, and a final output fault
  after an early native resume. Each verifies no guest execution/output change,
  no residual mappings/handles/TLS entry, and successful creation afterwards.
- Actual worker/event/duplicate handles are checked closed after reaping. Native
  process handle counts must remain bounded across churn (a small fixed CRT
  cache allowance and a bounded cleanup-settling interval are permitted).
- Cooperative shutdown joins a still-suspended worker without running its AOT
  entry. No original assets, code generation, or parent build is involved.

Standalone fixtures passed GCC and ClangCL/MSVC. OS allocation/duplication failures
are handled transactionally; their corresponding real resource exhaustion was
not forced. General guest pointer misuse after releasing all references remains
invalid; completed record addresses may be reused.

Parent integration (this task does not edit CMake):

```cmake
add_executable(ThreadLifecycleTests tests/test_thread_lifecycle.cpp
  runtime/threads.cpp runtime/thread_objects.cpp runtime/kernel_objects.cpp)
target_compile_features(ThreadLifecycleTests PRIVATE cxx_std_20)
target_compile_definitions(ThreadLifecycleTests PRIVATE
  SIMPSONS_THREAD_LIFECYCLE_STANDALONE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(ThreadLifecycleTests PRIVATE
  "${CMAKE_SOURCE_DIR}" "${SIMPSONS_GENERATED_DIR}"
  "${CMAKE_SOURCE_DIR}/third_party/XenonRecomp/thirdparty/simde")
add_test(NAME NativeThreadLifecycle COMMAND ThreadLifecycleTests)
set_tests_properties(NativeThreadLifecycle PROPERTIES TIMEOUT 60)
```

Do not link SimpsonsRuntime to this standalone target (its harness supplies a
subset of Runtime methods). Existing runtime source registration is unchanged.
After the parent's normal regeneration/checkpoint, run:

```powershell
cmake --build <parent-build-directory> --target ThreadLifecycleTests
ctest --test-dir <parent-build-directory> -R '^NativeThreadLifecycle$' --output-on-failure
```
