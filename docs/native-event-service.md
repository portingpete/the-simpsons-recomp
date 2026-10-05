# Native handle-based event service

Boot206 accepted the real profile identity and display name, then advanced from
the title into the purple TV transition. The last viewed capture was
build/captures/native-loading-206-after-start/native-frame-586476.png. At
present788/draw587443 the original profile flow created a worker and called
82433708 through827B4E78. It stopped at NtCreateEvent with output0203F780,
no attributes,type1,initial0. The failure return address was8243375C and the
owner call returned to827B4EA0. The run exited1; no save/menu result was shown.

## Recovered original boundary

Original82433708 converts its manual-reset boolean to native event type0 or1,
narrows initial state to a byte, requests an event handle and reports the
platform error using its original thread-local state. For the reached unnamed
auto-reset event, the import arguments are handle-output,attributes0,type1,
initial0. The native service does not modify this wrapper or its caller.

Original827B4E78 stores the returned handle at owner+4, clears seven words at
owner+8 and publishes the same handle at owner+14 in its overlapped record.
Original827B4EE0 closes that owned handle and clears owner+4 and owner+EC.

Exact original ranges (the loaded image remains globally SHA256-pinned):

- 82433708/0x9C: b66e9f43d9fdcdbe5f3a05a2ea74da397efd09a2af4ad13bc842b57c347606fe
- 827B4E78/0x64: b20520aee7f226c72c8594eb02f294a2698958ca2f750fb48f50b7703a1e8494
- 827B4EE0/0x4C: f4840309e927c7a68d529b0747500b6fb0f2e91608c6af7b98bf754e12a11337

Import arguments were checked against the read-only local reference
K:/Simpsons/RexGlueCurrent/src/kernel/xboxkrnl/xboxkrnl_threading.cpp,lines568..649,
SHA25642c8a6f40ad24d64eac3d68ef34b321bd7ca0804ee215aa02a456236bfd97772.
The reference event implementation is not run or linked.

## Real Windows ownership

NtCreateEvent now uses Windows ntdll's NtCreateEvent and publishes a native
KernelHandle::Event only after success. Output is preflighted and written in
original big-endian order. Only unnamed objects and types0/1 are admitted.
Allocation failures retire the new Windows handle. The existing registry owns
successful handles, and an in-flight wait retains a shared lease after close.

NtSetEvent uses Windows NtSetEvent, including its atomic previous-state output.
NtClearEvent uses Windows NtResetEvent without a previous-state output; the
original clear import's incidental r4 is ignored. Each service validates its
runtime and cancellation, rejects wrong handle types and preflights any output
before mutating the event. CPU state beyond r3, host FP controls and LastError
are preserved. No pulse service, named object namespace, completion callback,
automatic signal or fabricated completed operation is introduced.

Windows documents the auto/manual-reset distinction and the user-mode native
entry point in [ZwCreateEvent](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwcreateevent).
The previous-state contract is documented in
[ZwSetEvent](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwsetevent).

## Tests and current limit

NativeEventOwnership uses actual Windows event objects and the actual wait
import. It tests both event types and initial states, five host FP modes,
previous-state values, repeated set/clear, auto-reset consumption, output
sentinels, invalid types/handles, unmapped/read-only/straddling outputs, rejected
named attributes, an actual wait surviving close, terminal cancellation and
original827B4E78/827B4EE0 construction/destruction with thread-local error state.
Workers have bounded waits and end normally; no forced worker termination.

build/native-event-regenerate.log and build/native-event-build.log complete.
build/native-event-tests.log passes the new event test, mutant regression and
profile query3/3 in0.34s. The complete124-test build passed in242.35s:
build/native-event-integration-build.log. Live event creation and the subsequent
profile/save flow remain to be demonstrated. Main-menu acceptance is outstanding.

Boot208 supplies live creation evidence after a normal Start: event1F8,type1,
initial0 was published by the actual original owner after present823/draw642713.
The game next called XMsgStartIORequest for appFB/messageB0008 (achievement
recording) and stopped at that still-unimplemented service. Live event set/clear
and normal teardown are not claimed from this failed run; those remain covered
by the actual native/original fixture. No save or main-menu acceptance yet.
