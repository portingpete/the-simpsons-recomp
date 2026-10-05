# Original debugger thread-name notification

Boot063 advances past the native recording-manager constructor, creates another
native worker in the suspended state, then reaches `RtlRaiseException` from
`82B760E8`. Original code establishes that this is the MSVC debugger thread-name
notification, not a C++ exception or a game failure.

## Original byte evidence

Original `8232B268` copies the requested name into a 32-byte CPU thread field
and explicitly terminates byte 31. At `8232B2C0..D8` it writes the four words
`1000`, name pointer, thread ID and zero. At `8232B2E4..F8` it calls original
`82B76078` with code **406D1388**, flags zero, parameter count four and that
16-byte record. The original thread metadata and wrapper remain AOT.

`82B76078` constructs a big-endian 32-bit exception record: code at +0, flags
at +4, chained record at +8, exception address at +C, parameter count at +10,
and parameters at +14. The address field is its own entry, `82B76078`.
Its import call at `82B760E4` returns to `82B760E8`.

Function identity against the verified original `analysis/simpsons.pe`:

- `8232B268`, size B8, SHA256
  `bbb4b8dcd0cdfd10d5b3ea32ae78905b29743169dcea2f362777b8f616982f84`.
- `82B76078`, size 80, SHA256
  `7d815ac0b280886d983dfa0f70e77ab375414dc6f1bc63785098fec17a99a5cd`.

The notification code and four fields agree with
[Microsoft's thread-naming description](https://learn.microsoft.com/en-us/visualstudio/debugger/tips-for-debugging-threads?view=visualstudio).

## Native adaptation

`runtime/thread_objects.cpp` recognizes only this continuable, unchained,
four-parameter notification from the verified original wrapper. It validates
type 1000, zero reserved flags, a terminated ASCII name within the original
32-byte limit, and a thread ID belonging to this runtime. ID FFFFFFFF denotes
the calling native thread; it must still have an owned runtime thread object.

The implementation retains the thread handle while applying `SetThreadDescription`
and checks the result with `GetThreadDescription`. It holds the object's state
mutex across the native operation and readback. Naming a suspended worker does
not resume it. Original exception memory and guest registers are unchanged;
this void notification does not invent a returned success status.

All other exceptions and malformed records fail with explicit diagnostics.
This implements native debug metadata only; general guest exception dispatch,
C++ unwinding and non-ASCII naming remain unsupported. A readback failure after
the native setter is a reported failure, not a claim of transactional rollback.

`tests/runtime_tests.cpp` exercises actual native naming/readback on an owned
suspended worker, unchanged exception memory and registers, invalid exception
codes/flags/chains/address/count/type/thread IDs/reserved words, and unsupported
encoding or termination. Build106 passes this fixture and all 26 CTest suites.
In actual boot066, the original helper names `VfxCullStateManagerThread`, the
native readback matches, and execution proceeds to `XamNotifyCreateListener`.
Full runtime test output is retained in `build/native-thread-naming-066.log`.
General exception handling is not claimed by that result.
