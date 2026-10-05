# Native millisecond clock leaf

Frozen implementation: `SimpsonsNativeGetTickCount(PPCContext&, uint8_t*)`,
appended to `runtime/timing.cpp`, replaces only original leaf `824324A8`.
It returns `uint32_t(GetTickCount64())` in zero-extended r3. It reads no guest
memory and constructs no `KeTimeStampBundle` object. The data import remains
guarded for all other unqualified accesses.

This implements the recovered millisecond/32-bit leaf contract using real
Windows system uptime. The original kernel's private bundle implementation,
other fields, exact timer granularity and suspend behavior are not recovered
from the supplied game image. No static clock, artificial progress, game-start
epoch or deadline adjustment is introduced.

## Original byte and unit proof

Evidence input: `analysis/simpsons.pe`, base `82000000`, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Addresses and offsets below are hexadecimal; time quantities such as10000 are
decimal unless prefixed. The complete leaf is16 bytes:

```
824324A8  3d608200  lis r11,0x8200
824324AC  816b07f0  lwz r11,0x7f0(r11)
824324B0  806b0010  lwz r3,0x10(r11)
824324B4  4e800020  blr
```

It consumes no arguments, creates no stack frame and returns an `lwz` value,
therefore zero-extended32 bits. LR, condition registers and floating-point state
are unchanged by these instructions. Original r11 is volatile scratch; the
native leaf preserves it instead of manufacturing a bundle pointer. Native code
changes only r3, with exact host MXCSR restoration around the Windows call.

The reached caller is inside `8232DE30` (.pdata extent `10C`), not a separate
function at `8232DE40`. Its call at `8232DEA8:48104601` returns to `8232DEAC`.
It computes `r31 = tick + 10000` at `8232DEB0`; a nonzero signed r29 less
than10000 is converted to a deadline by adding that biased tick at `8232DEC0`.
It stores r29 into SP+50 and passes its address to original lock helper
`82329720`. The initializer/lock/count/owner-stack effects remain original AOT.
The tick leaf must not absorb this bias.

The unit connection is stronger than merely observing a10000 constant:

- `82329B70` calls the clock. `82329B78..94` takes a stored non-sentinel
  deadline minus `tick+10000` when the deadline is later, otherwise zero.
  That remaining duration goes directly to `824337A8` at `82329BD0` or
  `82329C10`.
- `824337A8` sets non-alertable r5=0 and tails to `82434F90`. That wrapper
  passes the same r4 to `824394E0`, whose `824394F4:1D6BD8F0` multiplies the
  zero-extended duration by **-10000**, stores a signed64-bit timeout, then
  returns its address. `FFFFFFFF` instead returns a null timeout pointer.
  `82434FC4` passes it to original `NtWaitForSingleObjectEx` (`82CC2BB4`).
- The associated sleep path `82432A08 -> 82434658` independently converts
  its duration with the same -10000 scale before `KeDelayExecutionThread`.
  `82329770..98` polls this clock with one-unit sleeps through that path.

NT relative wait intervals use100-nanosecond units, so the recovered duration
unit is a millisecond. Windows `GetTickCount64` supplies milliseconds since
system startup. The narrowing deliberately retains the original32-bit result;
it does not upgrade existing guest consumers to64-bit deadlines.
[NT timeout units](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kewaitforsingleobject),
[GetTickCount64](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-gettickcount64).

Two particularly small retained consumers are `8232A320` and `8232DD38`, each
36 bytes: save LR/frame, call the leaf, add10000, restore frame/LR, return.
The addi itself operates on the64-bit register. Consumers using stw/lwz/cmplw
see its low32 bits. The integrated fixture executes both actual AOT wrappers.

The static scan found **20 direct branches/calls** to this leaf, including four
unlinked tail branches at `8225D6B8`, `8241FCF0`, `826797C0`, `8270CE10`.
All addresses/words and available .pdata owners are in
`build/native-tick/evidence.json`. No aligned literal pointer to the leaf was
found. This is not complete computed/indirect-call discovery, nor a semantic
qualification of every containing subsystem. `8267A4D8..500` also subtracts
successive32-bit readings and stores the resulting remaining duration, which
corroborates elapsed-clock use without proving its higher-level purpose.

## Width, resolution and wrap limitation

The native result is milliseconds modulo2^32; the period is approximately49.7
days. Windows documents a system-timer-limited resolution, typically10–16ms,
and does not make this a high-resolution performance clock. This implementation
does not change the machine's timer resolution or any existing timing import.
[GetTickCount width/resolution](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-gettickcount).

Original deadline comparisons are **not generally wrap-safe**. For example,
`8232977C` compares unsigned low32 words `tick+10000` and an absolute deadline;
it does not compare a signed modular difference. With tick=`FFFFD8E8`, the
biased value is `FFFFFFF8`; a deadline32ms later wraps to `00000018`, and that
unsigned comparison expires early. The fixture records this behavior explicitly.
No consumer rewrite, clamp, fresh per-Runtime epoch or fake long-uptime test is
part of this bounded leaf implementation.

## Main integration

The exact hook is:

```toml
[[midasm_hook]]
address = 0x824324A8
name = "SimpsonsNativeGetTickCount"
registers = ["ctx", "base"]
return = true
evidence_hex = "3d608200816b07f0806b00104e800020"
```

Main owns configuration and CMake. The test target is `NativeTickTests`, source
`tests/test_native_tick.cpp`, linked to `SimpsonsRuntime`, compiled with
`/fp:strict`. Register `NativeMillisecondClock` with
`analysis/simpsons.pe` as its sole argument; main's15-second timeout is sufficient
for the bounded3-second progress deadline plus image loading. Do not define
`SIMPSONS_NATIVE_TICK_STANDALONE` for this integrated target. Kernel32 is the only
native API library needed, already present through normal Windows linking.

## Verification and freeze

`python -B build/native-tick/evidence.py` passed: **11 byte-checked spans,
318 original instruction words, 20 direct references, 2 integrity rejections**.
It validates the original image hash and PE/.pdata framing, compares every
disassembler word to original bytes, and rejects a changed image or forged
disassembly. Explicit bounded leaf spans are distinguished from .pdata extents.

`python -B build/native-tick/build_test.py` passed: **830 standalone checks**.
The last run observed46ms of real clock progress, with no forced resolution.
Tests bracket each native return between actual `GetTickCount64` reads, check
zero extension, compare the complete PPCContext except r3, pass null/inaccessible
guest bases, preserve guest FPSCR and five host MXCSR profiles (including
unmasked/sticky flags), and verify synthetic arithmetic around32-bit wrap.
Synthetic values are used only for arithmetic; the production clock is never
replaced or advanced by the fixture.

The complete production timing.cpp compiles unchanged apart from the appended
leaf. The standalone executable resolves unrelated timing-import dependencies
to explicit failure-only fixture traps; no substitute clock or successful guest
memory service is supplied. Both standalone and integrated test branches
compile with ClangCL C++20 `/O2 /fp:strict /W4 /WX`.

**Actual AOT caller execution remains for main's regenerated build133.** Its
fixture loads the original image, maps only a stack, enables the real raw-import
guard, invokes both pinned biased-clock wrappers, checks return bounds and
SP/LR/nonvolatile GPR/FPSCR preservation, and requires the bundle import to stay
unreadable and its loaded slot bytes unchanged. No integrated-pass claim is made
by the standalone result.

Build131 stopped at linkage: `PPC_FUNC` decorated its reference parameter as
`__restrict`, producing `YAXAEIAU` where the generated midasm declaration expected
`YAXAEAU`. `llvm-nm` confirmed this mismatch. The definition and test declaration
now both use plain `void SimpsonsNativeGetTickCount(PPCContext&, uint8_t*)`.
The isolated test was rerun successfully with830 checks. Main reports build132
correctly rejected stale generated-input hashes before compilation; full
regeneration in build133 passed all37 CTest suites in52.43 seconds. Its native
clock suite passed906 checks, including both actual AOT wrappers with the raw
import guard enabled. No integrated pass is attributed to131/132.

Actual muted boot076 passes the previously reached clock read and creates a
further original worker. The next failure is notification subscription mask20,
version2, caller8280A17C (`build/boot-076.log`). The tested executable SHA256 is
`15b6f0c3dc048b2315646f4d821aa1648943963931689d9eafd086f008c15252`.

The current integrated fixture explicitly sets `rt.checkingImports=true` before
checking the timestamp slot. `Runtime::load()` alone does not enable that guard;
normal `initialize()` does. It requires the exact guarded `rt.pointer()` failure
both before and after the AOT wrappers. Its separate raw `memcpy`/`memcmp` of four
loaded image bytes is a read-only assertion view, not a guest-readable bundle or
an import binding. There is no unguarded `PPC_LOAD_U32` of the timestamp slot in
the current fixture.

Runtime helper and test are frozen. Hashes and executable provenance are in
`build/native-tick/verification.json`; compilation/test logs are adjacent.
Only `runtime/timing.cpp`, `tests/test_native_tick.cpp`, this document and
`build/native-tick/*` changed. No runtime header, audio, configuration, CMake,
generated, original or reference files were modified.
