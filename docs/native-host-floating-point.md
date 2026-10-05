# Native floating-point control at AOT boundaries

Build083 exposed a host `C000008F` inexact floating-point exception during
driver-fixture failure cleanup. Build086 reproduced the same exception during
an original camera frame callback. The original game had not requested these
host traps: a fresh `PPCContext` contained a zero FPSCR host-control cache.
The first cached flush/rounding update wrote that zero-based value to MXCSR,
clearing every SSE exception-mask bit. Ordinary inexact arithmetic then
trapped in original AOT code or a host library.

`runtime/ppc_context.template.h` now initializes the x64 cache to `1F80` and
preserves those exception masks on every cached write. The cache still carries
the original guest rounding and scalar/VMX flush policy. It is host control
state, not a complete emulation of guest FPSCR status or exception delivery.
General guest floating-point exception semantics remain incomplete.

Native entry to original code now saves the caller's exact control/status word,
loads the callee's guest control state, and restores the caller on return or
C++ exception unwind. This covers main startup, worker entry and each
`EngineCpuCalls::invoke`. Nested CPU callbacks preserve the outer guest's
environment and maintain a separate copied register context. Ordinary
guest-to-guest calls continue sharing the guest environment. Main/worker SEH
handling remains in an inner function so the C++ scope also restores state
after that checked layer returns or throws.

The isolated fixture compiles verbatim production headers and entry wrappers;
only memory services, original entry functions and the SEH diagnostic callback
are substituted. Its nine cases test default and explicitly zeroed caches,
actual inexact arithmetic, all four rounding modes, flush versus gradual
underflow, main/worker/callback return and throw, nested callbacks, SEH exit,
and host arithmetic during destructor unwind. All nine cases failed before
the fix and passed after it. The fixture is registered as
`NativeHostFloatingPoint` in CTest. The regenerated full lifecycle run remains
the integration check; consult `STATUS.md` for its current result.

No generated function was edited. Regeneration is required because every AOT
translation unit includes the generated copy of the production template.
