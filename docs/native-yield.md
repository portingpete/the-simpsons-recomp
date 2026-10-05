# Native scheduler yield

Boot143 was given a180-second loading allowance but exited after about8.5
seconds at the missing NtYieldExecution import. Original wrapper82B76B98
calls this no-argument service at82B76BA4, compares its result with
STATUS_NO_YIELD_PERFORMED(40000024), and returns the Boolean of inequality.
Its stack restoration and result conversion remain original AOT code.

runtime/timing.cpp resolves and calls the real Windows ntdll NtYieldExecution.
It retains the actual32-bit NT status in the original return register,
including40000024 when the scheduler performs no yield. Host last-error and
floating-point state are preserved. The runtime stop condition is checked
before and after the nonblocking service. A missing native export fails
explicitly. This does not introduce a fixed sleep or synthesize success.

The existing zero-interval KeDelayExecutionThread implementation is separate:
that delay has elapsed even when no peer thread ran. Its normalization does
not apply to the original NtYieldExecution caller's explicit status test.

Focused RuntimeTests checks32 real host calls and complete context/host state
preservation. NativeConfigurationTests pins all14 original wrapper words and
executes16 real AOT wrapper calls, checking the Boolean result and preserved
stack/LR/nonvolatile registers. Combined configuration checks pass65,767.
Logs: build/im2d-upload/runtime182-focused2.log and
configuration182-focused2.log. Full build182 passes85/85 suites in150.72
seconds; build/im2d-upload/build182-tests.log preserves the complete run.

Boot144, also allowed180 seconds, advanced past the yield and exited after
about8.4 seconds at a misleading duplicate-live-crossfade rejection. The
shared allocator had been reached by a different caller,8282E9B8. Build182
reports that unimplemented caller before crossfade-only checks. Final boot146
requests1280x720 format28000002 flags400 plane0 and exits after about8.7
seconds. It does not exhaust its180-second allowance. Native movie-plane
ownership is the next implementation; loading/gameplay remain unverified.

Primary native ABI reference:
[phnt NtYieldExecution declaration](https://github.com/winsiderss/systeminformer/blob/master/phnt/include/ntkeapi.h).
