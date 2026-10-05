# Native process sampling

`tools/sample_native_process.py` captures the live native stacks of exactly
`K:\SimpsonsNativeCopy\build\native\SimpsonsNative.exe`. It accepts an explicit
PID supplied by the current game run; it does not search other processes. The
output contains each thread's ID/name and each frame's native PC, SP, module,
function, source line, inline status, and original AOT function address when a
`sub_XXXXXXXX` symbol is available.

## Running a sample

From `K:\SimpsonsNativeCopy`, substitute the current game PID:

```powershell
python -B tools/sample_native_process.py --pid 12345 --output build/native-process-sampling/current-run-sample1.json
```

Use a new output name for every sample. Omitting `--output` creates a unique UTC
timestamp/PID name. JSON and its adjacent `.lldb.log` must be directly under
`build/native-process-sampling`; the wrapper exclusively creates both files.
Repeat with the same still-live authorized PID and a new filename for multiple
snapshots. No game launch or shared build is part of this tool.

Defaults are 48 frames per thread, 128 threads, and an eight-second soft
stack-reading budget. Options `--max-frames`, `--max-threads`, and `--budget`
have upper bounds of 128, 256, and 30 seconds. Counts and truncation flags are
recorded; unresolved symbols remain null rather than being guessed.

The budget is checked between LLDB calls. It cannot interrupt a blocked native
attach, symbol lookup, or detach call. The wrapper reports an overlong debugger
after 30 seconds and keeps waiting; it never forcibly terminates an attached
debugger or the game. This is an occasional diagnostic snapshot, not a
nonintrusive performance profiler: attaching briefly stops game threads.

## Dependency and execution policy

The installed `C:\Program Files\LLVM\bin\lldb.exe` is LLDB 22.1.8. Its missing
Python dependency is supplied by the existing
`C:\Program Files\Blender Foundation\Blender 5.0\python311.dll` and that
installation's `5.0\python` standard library (Python 3.11.13). The wrapper
sets `PATH`, `PYTHONHOME`, and `PYTHONDONTWRITEBYTECODE` only in the debugger
child's environment. It removes inherited `PYTHONPATH` and symbol-server paths;
the symbol search path is the native build directory. Nothing is installed or
written into LLVM, Blender, original game, or reference directories.

LLDB is launched without a window or `.lldbinit`, with Python explicitly
selected. Loading scripts from symbol files is disabled before target creation.
The game is checked through `QueryFullProcessImageNameW` before the debugger is
started, then checked again immediately before attachment. Only a query access
handle is used for identity; retaining that handle keeps the process object
alive while attachment is established. The final sampler also records the
process creation FILETIME. A wrong, nonexistent, or exited PID is refused.

The stack reader uses LLDB's frame/symbol APIs; it does not evaluate game
expressions, install breakpoints, change registers, write memory, or call game
functions. Its `finally` cleanup calls `SBProcess.Detach(False)`, including
after an attach or frame-reading exception. Detach errors are explicit failures.
An already exited target is reported without pretending that a detach occurred.
No `Kill`, `Destroy`, terminate, game shutdown, or UI action exists in the
sampling path. A debugger or operating-system crash is outside the Python
cleanup guarantee.

The supported API and command semantics are documented in the official
[LLDB command reference](https://lldb.llvm.org/man/lldb.html) and
[SBProcess API](https://lldb.llvm.org/python_api/lldb.SBProcess.html).

## Verification and actual run 152

```powershell
python -B tools/sample_native_process.py --self-test
python -B build/native-process-sampling/verify_sampler.py
```

The self-test rejects zero, invalid, nonexistent, and wrong-executable PIDs
(the last uses only the helper itself), then starts LLDB without a target to
check Python. The fixture test exercises normal cleanup, bounded thread/frame
counts, exceptions during attach/thread/frame lookup, failed attach status, an
unexpected returned PID, reported detach failure, and an already exited target.
Those fixture checks attach to no process and verify no kill or write API use.

Run 152 was sampled once with its explicitly supplied PID **31812**. This PID
has since exited; do not reuse it for another run.

- Evidence: `build/native-process-sampling/run152-sample1.json` and its adjacent
  `.lldb.log`.
- Sampling began at **2026-09-11 18:10:43.426 UTC**; attachment completed at
  **18:10:44.208 UTC**; report/cleanup finished at **18:10:44.618 UTC**.
- **70 threads** were captured. Reading their stacks took **0.406 seconds**;
  neither the time nor count limits were reached.
- LLDB returned success and printed `Process 31812 detached`. The report records
  `detached: true`, with no sample or detach error.
- A later second attempt was refused by the Windows PID check after that run
  ended. No second snapshot or debugger attachment was created.

Thread **22896** was sampled at native PC `0x00007FF77AD87C37`, SP
`0x00000043E4DFE7A0`, in `sub_82433328` (generated source
`ppc_recomp.76.cpp:15993`). Its next callers were `sub_8232B0A8`,
`sub_82375C88`, `sub_82373738`, and `sub_8282D998`. Thread **17168** was named
`RWAudioCore Dac` and was sampled in `PPCGuestPointer`, called by
`sub_82B74110`. Other captured game threads included native waits and the
window message loop. This is one observed call-stack snapshot, not proof of
which thread consumed CPU over time or what caused the stalled presentation.

The live sample predates the small addition of process creation FILETIME and
the expanded attach-exception cleanup test; its original evidence is retained
unchanged. No shared renderer/runtime source, build definition, or original
reference file was modified for this diagnostic.
