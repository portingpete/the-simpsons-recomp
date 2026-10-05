# First-mission gameplay FPS (2026-09-25)

Goal: raise average gameplay FPS by at least 10. Result: **+15.5 FPS** in the
controlled stationary Land of Chocolate start view (63.1 to 78.7 FPS), and
+19 to +23 FPS in the repeatable walk views below. No draws, effects, checks
or original code were skipped; the gains come from CPU/GPU synchronization
and per-draw driver allocation.

## Measurements

`tools/benchmark_first_mission.py` launches directly into Land of Chocolate
(`--render-test-first-mission`, uncapped, frame-only timing) and averages the
accepted presentation intervals in a window. The comparison baseline is the
pre-change build preserved at `build/gameplay-fps-5/baseline` (62.9 FPS,
matching the session-start build's clean runs of 62.7 and 63.8 FPS; the
"Before" start figure averages all three).

| View | Window | Before | After |
| --- | --- | ---: | ---: |
| Start position, stationary | 30-150 s | 63.1 (3 runs) | 78.7 (2 runs) |
| `--walk plaza` (building front) | 35-90 s | 89.0 | 107.9 |
| `--walk wide` (plaza, Homer behind pretzel fence, silhouette effect on) | 35-60 s | ~70 | ~93 |

Notes for reading numbers:

- These historical builds quantized gameplay time to display refreshes, so
  movement speed could increase above 60 FPS. The subsequent
  [gameplay timing fix](gameplay-speed.md) removes that coupling. Captures
  (`--capture-at`) confirmed the plaza and wide runs above ended in the same
  views; use the fixed build for future movement comparisons.
- The per-second `cpu_timeline` in each `summary.json` matters: one old-build
  wide run fell to 28 FPS only while the whole system sat at 100% CPU from an
  external load. Discard windows with high `system_busy_pct`.
- The harness keeps the game topmost but never focused, and disables input to
  its window. Earlier runs showed desktop typing reaching a focused game window
  and moving Homer mid-measurement.

```powershell
python -B tools/benchmark_first_mission.py after --seconds 150
python -B tools/benchmark_first_mission.py before --executable build/gameplay-fps-5/baseline/SimpsonsNative.exe
python -B tools/benchmark_first_mission.py wide --walk wide --seconds 90 --start 35 --capture-at 40
```

Convert captures with `python -B tools/render_frame_capture.py <run>/captures`.

## Changes

1. **No per-copy Flush** (`renderer/native_presentation.cpp`). Each depth,
   front and presentation copy used to `Flush()`, which with a threaded driver
   synchronizes with its submission thread (about 7% of main-thread time).
   Queued copies are now submitted by Present, a full command buffer, or
   `boundedWait`, which flushes once before polling with DONOTFLUSH.
   `retireCopies` stops at the first pending event: events on the one
   immediate context signal in submission order.
2. **One frame of CPU/GPU overlap at presentation.** `presentFront` waited for
   the front->swapchain transfer every frame, serializing CPU and GPU.
   `EngineDriver::present` now uses `presentFrontQueued` and completes that
   presentation's copy/transfer with a bounded wait before the next present
   consults its history receipts, before a front capture, and at idle. Native
   uploads snapshot guest bytes into GPU-ordered copies, so resetting the
   original CPU ring cursors cannot race queued GPU reads. `submissionCompleted`
   reports the real event state; the new `waitSubmission` waits for it.
3. **Constant banks instead of per-draw buffers** (`renderer/screen_pipeline.cpp`).
   Z-prepass, mono, shadow-depth, rigid, skin and sky commits created one or two
   IMMUTABLE constant buffers per commit. They now write backend-owned DYNAMIC
   banks with `Map(WRITE_DISCARD)` (the driver renames storage, so queued draws
   keep their bytes). Each publish advances a bank generation; a commit is valid
   only while it holds the current generation, so stale commits still reject.
   Static Z-prepass/mono profiles share one immutable all-zero Boolean bank.
4. **Critical-section LockCount** (`runtime/synchronization.cpp`). After the
   last release the runtime published LockCount 0 instead of the NT/Xbox free
   value -1 that its own initialization writes and its static-section check
   requires. It now publishes `recursion + waiters - 1`.

## Tests

The full suite (244 tests) passes repeatedly with `ctest -j 4`. Updated tests:
present/driver contracts for deferred completion; Z-prepass/mono tests assert
the shared zero Boolean bank and use a foreign CB1 for their stale-binding case.
Earlier-drifted tests were also fixed: DAC and thread-exit fixtures used low
addresses now served by the null-device scratch page; the critical-section
fixture reused r3 after initialization's status return; Im2D, material and
resource-bridge tests predated mip-chain admission, the VFX rigid pair and the
distortion identities (the translated set now lives in
`tests/header/native_translated_materials.h`); the resident XMA limit test
predated the 4,194,304-frame cap. Timing/focus-sensitive tests
(frame-rate pacing, controller keys, viewport page revocation, generator
compiles) were made robust to a busy or in-use desktop.

## 1440p profile and the 240 FPS goal (2026-10-02)

Earlier benchmarks ran at 1280x720 because the tool never copied
`profile.video.cfg`. `--render-resolution <index>` now writes it (3 =
2560x1440, 6 = 3440x1440). At 2560x1440, stationary start view, uncapped:
**about 70 FPS** before this session's change, **72-73 FPS** with the audit
change below (runs vary by about 3 FPS on this desktop; repeat runs).

The run is CPU-bound: the main host thread is ~100% busy, the GPU is mostly
idle, and resolution barely matters (720p was 79 FPS). 240 FPS needs 4.2 ms
per frame; the frame is currently about 14 ms.

Profiling aids added: `SIMPSONS_STACK_SAMPLE=<file>[,delay_s,duration_s,threads]`
(app/stack_sampler.h, in-process stack sampler, opt-in) and
`tools/stack_profile.py` (symbolizes it; `--within`, `--callers`, `--children`,
`--leaf`). `tools/quick_build.ps1 [-Recompile]` is the incremental build.

Main-thread self time by category (s4 capture, ~10.5k samples): guest code and
PPC helpers 20%, native bridge/validation 22%, D3D11 + driver 13%, ntdll 12%
(7% is the main thread waiting for a guest worker semaphore inside
`sub_82726888`), mesh-cache hash/compare 9%, vertex decode 4%,
`Runtime::pointer` 2.5%. The static Z-prepass pass alone (`sub_826FF4C8`,
19% of the frame) spends about 40% of itself in `StaticMeshSourceCache`
hashing and byte comparison. The rest is a long tail of per-draw validation
(camera binding, replay ownership, metadata walks), none above 3% alone.

Change: the resource audit formatted strings, took a mutex and copied nine
strings per scene dispatch even with no `--resource-audit` file (about 4-5% of
the frame). `ResourceAudit::active()` now reports true only when a file was
configured; failure and shutdown rows still go to stderr, but they are no
longer attributed to the last encounter in default launches.

What would move the number materially needs a structural change, not more
trimming: a guest-write barrier (page watch bit cleared by the first store)
so unchanged static meshes, camera and owner structures are validated once
instead of re-read on every draw (this relaxes the current "validate every
byte every draw" policy); moving D3D11 submission off the main thread; and
overlapping the main thread with the guest worker it currently waits for.

## 200 FPS goal, 2026-10-04 findings

Baseline at 2560x1440, stationary start view, uncapped (`--render-resolution 3`): 75-76 FPS
(12.7 ms median; runs vary by about 3 FPS and more while the desktop is busy). About 200 original
geometry draws and 28 scene draws per frame, so the cost is per-draw overhead, not draw count.

Main-thread split (stack sampler, 10k samples): about 50% inside native hooks (Z-prepass, rigid, skin,
replay, shadow, Im2D), 43% in recompiled guest code and its memory checks, 6.7% blocked on a
semaphore for a guest worker thread. D3D11 plus the NVIDIA user-mode driver is about 12% of the
thread. The renderer chain is `sub_826B8068 -> sub_8269E980 -> sub_8273B4D0 -> sub_82740680` (55%)
with the rigid pass `sub_827400F8` (19%), replay `sub_827402F0` (18%) and static Z-prepass
`sub_826FF4C8` (11%) below it.

Tried and rejected (no gain beyond run noise):

- Inline checked memory path in every AOT chunk (`-DSIMPSONS_INLINE_MEMORY_ALL=ON`): 77.6 vs 75-76 FPS
  but the executable grows from 85 MB to 245 MB. The option stays in CMakeLists.txt, default OFF.
- Bounded user-mode spin before guest semaphore waits: 69.5 vs 69.6 FPS. The worker's phase work
  (about 0.8 ms per frame) is on the critical path; this is real waiting, not wake latency.
- `ID3D11Multithread::SetMultithreadProtected(FALSE)`: no measurable change. (`D3D11_CREATE_DEVICE_SINGLETHREADED`
  cannot be used: deferred-context creation fails with DXGI_ERROR_INVALID_CALL.)

Tooling: `tools/stack_tree.py` (top-down call tree), `tools/stack_lines.py` (inline-aware line-level self
time), `tools/quick_build.ps1 -Configure ...`. The benchmark now opens the game window without
activation, outside the taskbar and at the bottom of the z-order (`SIMPSONS_BACKGROUND_WINDOW=1`); it no
longer uses a topmost window. `summary.json` reports `accepted_pct` (share of presentations that were not
occluded).

Landed (2026-10-04): guest-write-watched exact source caches for the rigid and skin mesh streams
(`rigidSourceCache`, `skinSourceCaches` in runtime/engine_effects.cpp, same `StaticMeshSourceCache`
contract as the static Z-prepass cache: exact content key plus the exact guest-write barrier, bounded
re-verification, `SIMPSONS_WATCH_VERIFY=1` repeats the full comparison on every hit). A hit skips the
guest copies, vertex decode, index decode and content hash. Skin meshes with bound morph streams, and the
first raw capture, always take the full path. The cache variant key now includes everything the decoder
and upload consume (stride, UV1/tangent/VFX flags; skin source, stride, alpha, and one cache per original
skin vertex shader). LRU promotion is O(1) (it scanned up to 512 entries per hit).
Interleaved A/B at 2560x1440: baseline 76.0 / 75.9 FPS, new 82.1 / 80.0 FPS. Full ctest 527/527; a
verify-mode walk through the plaza found zero stale hits.

Remaining native cost is flat: `validateCamera` 7.9%, `executeReplay` 6.8%, `requireRigidPayload` 4.6%,
`requireReplay` 4.4%, `activeZPrepass` 4.5% (mostly validateCamera and `validatePool`), `drawZPrepassMesh`
6.3% (about 60-80 D3D state read-backs per draw). All of it re-validates unchanged guest/host state on
every hook; `EngineRasters::cameraSurfaces` documents that no validation result is retained. Removing
that cost needs either exact write-watch memoization of those validators or dropping redundant D3D
self-checks, both changes of the validate-every-call policy.

## Hardware-assisted guest memory (2026-10-04)

The per-access software checks of recompiled code were the single largest cost: an unsafe
no-check build ran the benchmark at about 120 FPS against 80. The recompiled code now uses
CPU page protection (`PPCGuestPointerFast`), validating hooks use a software TLB, cancellation
is polled at function entry, and the store barrier is a one-load 64 KiB block count. Details,
contract changes and measurements: [guest-memory-fast-path.md](guest-memory-fast-path.md).
Main-thread CPU per frame at 1440p: 12.2 ms before, 10.9 ms after (interleaved rounds).

The user's direction on 2026-10-04 is a steady ~120 FPS rather than a raw maximum: judge
changes by `main_thread_cpu_ms_per_frame` and the new frame-time spread fields (`p95_ms`,
`p99_ms`, `stdev_ms`, `hitches_over_1p5x`) in `summary.json`; discard runs with
`main_thread_busy_pct` under about 85 (the desktop shares the machine with other heavy jobs).

## Steadiness fixes (2026-10-04)

Three causes of uneven frames found while measuring under desktop load:

1. **Hard CPU pinning.** Every guest thread was pinned to one logical processor
   (`SetThreadAffinityMask`), so whenever another process occupied that CPU the guest
   thread waited a whole scheduler quantum: with another heavy job running, the same build
   went from 89-96 FPS / p99 15 ms to 43-67 FPS / p99 53-74 ms (main thread busy 45-65%
   instead of 90%). Threads now keep the chosen CPU as their *ideal* processor but may migrate
   among the performance cores (`SetThreadIdealProcessor` plus a performance-core mask).
   `SIMPSONS_HARD_PIN=1` restores the old behavior for A/B. The process also opts out of OS
   power/timer throttling (`ProcessPowerThrottling`), which Windows applies to unfocused windows.
2. **The 120 FPS limiter added a delay instead of capping.** It slept until 8.333 ms after
   the *current* present's own entry, after submitting it, so every frame cost the guest
   work plus about 8 ms (about 52 FPS where the work alone allows 90). `runtime/frame_pacer.h`
   now releases a present no earlier than the previous release plus the interval, before
   submitting it. When the machine cannot sustain 120, the interval rises to the 85th percentile
   of recent natural frame times (`SIMPSONS_PACE_PERCENTILE`, 50-100), so frames are released an
   even distance apart instead of jittering. Covered by simulated-tick tests in
   `tests/test_native_frame_rate.cpp`.
3. **XInput polling of empty controller slots** cost about 4% of the frame in bursts (device
   enumeration, 0.1 ms to several ms per call, three slots per frame). A slot that just reported
   "not connected" is re-queried at most every 250 ms (`NativeControllers::disconnectedPollMs`).

Cache re-verification (`StaticMeshSourceCache`) is now staggered per binding so the periodic full
byte comparisons never land in one frame.

Measured in the game's default capped-120 mode at 2560x1440 with another heavy job running on the
machine (same conditions, 3 runs each, 45 s):

| Build | FPS | median ms | p95 ms | p99 ms | stdev ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| before (hard pinning, post-submission limiter) | 23-24 | 36-37 | 67-71 | 84-92 | 14-16 |
| after (soft placement, pre-submission pacer) | 82-92 | 10.9-11.9 | 11.6-14.1 | 13.3-15.7 | 0.7-1.1 |

`SIMPSONS_D3D_CALL_STATS=1` prints per-method D3D11 call counts every 600 frames. The immediate
context's draw/state methods bypass a patched vtable (the runtime swaps dispatch), so only device
and a few context methods are counted: useful for object creation per frame (about 90 queries, 23
buffers, 14 sampler states per frame; `Device::AddRef`/`Release` about 8,600 each from
`GetDevice` in per-draw owner validation), not for the Set/Draw mix.
