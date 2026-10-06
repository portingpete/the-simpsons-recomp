# Runtime stall profiler

Double-click **Play The Simpsons Game - Stall Profiler.cmd** in the project
folder for a separate profiling launch. Windows may briefly show the command window. It enables
`SIMPSONS_STALL_PROFILE=1` only in the game process and writes timestamped runtime
logs to `build/stall-profiler-logs`. It uses the normal startup, save and video
settings. The helper can be invoked directly
as `build/native/SimpsonsLauncher.exe --stall-profile`.

For manual launches, set `SIMPSONS_STALL_PROFILE=1` before starting the game or
ordinary launcher. The ordinary launcher passes its environment to the game;
output goes to the existing buffered runtime log. Profiling is disabled by
default. Set the variable to `0`, or remove it, to disable profiling on the next
ordinary launch. The dedicated profiling shortcut always enables it. The game
reads the variable once at process startup.

```powershell
$env:SIMPSONS_STALL_PROFILE = '1'
& .\build\native\SimpsonsNative.exe --image .\analysis\simpsons.pe
```

The CMake option `-DSIMPSONS_STALL_PROFILER=OFF` removes the timers and profiling
wrappers at compile time. It defaults to `ON`, allowing an ordinary build to be
profiled without rebuilding. A disabled process takes the short inactive path;
it does not read the performance counter or build profiling records for calls.

Frame time is wall time from entry into original execution to its first completed
native presentation, then between consecutive completed native presentations.
It includes guest work, rendering, audio on the frame thread, file I/O,
synchronization, intentional pacing, and the actual DXGI Present call. Frames
over **16.67 ms** receive `[STALL]` summaries with section totals and largest
contributors. A configured 30 FPS cap therefore produces slow-frame diagnostics
showing its intentional waits. The profiler does not change the cap.

Runtime imports and native HLE hooks taking over **2 ms** receive `[STALL]` call
records. Actual waits over **2 ms** receive `[WAIT]` records. Records identify
duration, Windows thread ID, function, guest function-entry address, guest caller
(LR), and waited object or fence/query identity when available. GPU query
identities are host pointers, distinct from guest presentation receipts.
`pc_kind=function_entry` means the runtime's `lastFunction`, which is not an
instruction-precise PC. A zero guest address means no guest context was available.

Slow native mutant waits also report `owner_tid_at_entry`,
`owner_caller_at_entry`, `wait_caller`, and the actual NT `status`. Owner fields
are observational snapshots taken before entering the wait; the lock holder can
change afterward. Zero means the owner or saved guest call site was unavailable.
These diagnostics do not participate in mutex ownership or scheduling decisions.

Rendering detail includes constant-bank `Map`, buffer/sampler allocation misses,
recorded draw preparation, `ExecuteCommandList`, skin draw submission, and
presentation query `CreateQuery`, `GetData`, and `Flush`. This separates driver
calls from the larger runtime hooks without adding GPU synchronization commands.

Original screen sprites reuse one device-owned packed integer scratch surface
when its complete physical texture descriptor matches. Allocation scopes record
cache misses; `CopyResource.screenSnapshot` and `CopyResource.screenCommit`
record the two original ordered full-surface copies. A different physical extent
replaces the cached surface. SSAA4x doubles each scene dimension, so a logical
3440x1440 scene uses 6880x2880 backing for these operations.

Recorded skin, rigid, mono and sky draws skip constant-buffer updates only when
their effective inherited bytes exactly match the last upload. Changed uploads
have `D3D11.UpdateSubresource` scopes. Replay ownership, released-state and
inheritance checks still run before reuse. These optimizations reduce resource
churn; a remaining slow driver call can still reflect pressure from earlier
queued rendering.

Section and contributor totals use exclusive wall time: an inner GPU wait is
charged to waits rather than counted again as rendering. Call records use
inclusive elapsed time so every slow service call is still visible. Native
callbacks into original CPU code are charged to guest execution. Worker call
records carry their own thread IDs; worker elapsed time is not added to the
frame owner's wall time. Follow a frame-thread wait's object identity to the
corresponding worker records when investigating a dependency.

Contributor storage is fixed and uses bounded probing. If it fills or encounters
too many collisions, `untracked_contributor_ms` reports the unattributed portion;
section totals and individual slow-call records still include that work.

Instrumentation observes existing operations. It preserves guest registers,
Windows LastError, host floating-point state, wait results, deadlines, and game
clock/scheduler requests. Enabled profiling necessarily adds measurement and
log-writing overhead; it does not deliberately sleep, yield, flush the GPU, or
retime the game. Logging uses the existing stderr buffering.

Automatic forwarding is generated by `tools/recompile.py` from the current
import mapping and native hook inventory, and is covered by the existing AOT
hash gate. After changing native sources, regenerate using the usual build
workflow. Focused tests cover threshold boundaries, nested scopes, frame splits,
worker isolation, state preservation, inactive profiling, and forwarding
coverage.
