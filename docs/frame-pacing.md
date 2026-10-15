# Frame pacing

The native limiter (`runtime/frame_pacer.h`, `SimpsonsNativeFramePace` in
`runtime/timing.cpp`) decides when each finished frame is handed to DXGI and
what time the gameplay clock uses for the update that builds it.

## What was wrong (2026-10-13)

A per-frame trace of the user's settings (VSync on, 120 FPS limit, borderless
3440 × 1440, stationary Land of Chocolate start) showed:

- The display refreshes at **119.844 Hz** (8344.2 µs; DWM reports
  `10000000/83442`, and a fit of DXGI flip times agrees to 19 µs). The limiter
  released frames at exactly 120.000 FPS. With VSync the two clocks drifted
  through every vblank phase: the time from release to flip swept from 1 ms to
  9.5 ms and back every few seconds, and about one frame per second was shown
  for two refreshes.
- Gameplay updates used the real time at which the update began. That start
  jitters with Present and post-present work, so dt differed from the
  interval the display actually advanced by ±0.5 ms (stdev), up to ±8 ms after
  a late frame.

## How it works now

**Refresh locking (VSync on).** When the limit is at or above the display
refresh (0.5% tolerance), or within 1% of a whole multiple of its period
(60 FPS on a 119.84 Hz panel is two refreshes), releases follow the display's
vblank grid from `DwmGetCompositionTimingInfo` (read every frame, about a
microsecond). Each frame aims for the vblank one locked interval after the
vblank its predecessor aimed for and is released `lead` before it. Limits
that are not a refresh multiple (90 FPS on 120 Hz) keep the free-running
limiter; so do VSync off and Unlimited.

**Lead.** A present released at least ~1.5 ms before a vblank always reached
it in the measured path (a background window on an overlay plane); one
released less than ~1 ms before always missed. The lead (a 4.5 ms minimum by default, at most 55% of a refresh and
at least 2 ms; at most 7/8 of a refresh in total) is also a jitter
buffer: a frame that finishes after its slot is released at once and still
reaches its vblank while it is inside the lead. Lateness carries over to the
next frame, so the buffer has to exist before a heavy stretch begins; a
longer lead cannot help frames already in debt. The lead is therefore the
larger of:

- the minimum plus the largest *debt* (accumulated lateness, computed from the
  frame work alone) of the last 30 seconds, kept in buckets of 120 refreshes;
- a *path* lead for the display side (GPU tail, compositor). DXGI frame
  statistics (`GetFrameStatistics`) report each frame's real flip; a frame
  that was released on its slot after a steady run of on-slot releases and
  still missed grows it by 0.5 ms, and 30 quiet seconds shrink it by 0.25 ms.

Light scenes therefore run with a short lead (low latency) and heavy levels
with a longer one (smoothness). A lower lead after the window expires only
moves releases later; every frame keeps its vblank.

**Queue drain.** A frame that misses its vblank is shown one refresh late, and
every frame after it would queue behind it (Present blocks on the two-buffer
swap chain). When DXGI reports a late flip, the next frame aims one interval
after the vblank that the newest queued frame will really reach. The display
still shows a new frame on every refresh while the queue drains.

**Whole refreshes on fixed displays.** Once at least 58 of the last 64 flips
land within 1/32 of a refresh of the DWM grid, the display is treated as fixed
refresh. A rate the machine cannot sustain then steps to the next whole number
of refreshes (an even 59.9 FPS instead of alternating one and two refreshes).
The decision replays the last 64 frame costs against the interval with a
half-refresh buffer. It steps up when more than one frame in eight would
miss, and back when at most one in 32 would miss the shorter interval. On
variable-refresh or unconfirmed displays the existing percentile hold applies
instead.

**Waiting.** A wait of at most 2.5 ms (nearly every wait in busy scenes) spins
without a timer. A longer wait sleeps on a high-resolution waitable timer until
2 ms before the release and spins the rest. That timer wakes about 0.3 ms late
(median), with a tail to ~1.9 ms, which the previous 1 ms guard did not
cover. About once a minute this machine also has a system-wide latency spike.
A separate process's timer was delayed by the same 9-18 ms at the same instant,
while a spinning thread lost under 3 ms, which the lead absorbs. A pacing timer
that expired inside such a spike was released one refresh late.

**Gameplay clock.** While the limiter paces the update thread, the gameplay
clock sample (`SimpsonsNativeGameplayClockSample`) uses `FramePacer::predict`:
the vblank (locked) or planned release (free) that the frame being built
will get. Each update then advances exactly as far as the display does. A
second update before the next release advances by real time; the value never
goes backwards. The original clamp of five refreshes per update still
applies.

## Diagnostics

| Variable | Effect |
| --- | --- |
| `SIMPSONS_PACING_TRACE=<csv>` | Per-presentation trace: update start and clock value, dt, limiter arrival/planned/actual release, target, lead, mode, DWM grid, Present return, the latest DXGI flip, and when the limiter's timer wait was due and returned |
| `SIMPSONS_PACE_LOCK=0` | Disable refresh locking (free-running limiter as before) |
| `SIMPSONS_PACE_PREDICT=0` | Time gameplay updates by their real start (as before) |
| `SIMPSONS_PACE_LEAD_US=<µs>` | Minimum lead before the targeted vblank (default 4500, at most 55% of a refresh, at least 2000) |
| `SIMPSONS_PACE_PERCENTILE=<50-100>` | Percentile for the held cadence (default 85) |

`python tools/pacing_trace_report.py <csv> --start 30` summarizes a trace:
release cadence, timer overshoot, modes, lead, display flip intervals,
repeated refreshes and the animation error (the gameplay step minus the
interval the display advanced). `tools/benchmark_first_mission.py --pacing-trace`
records and summarizes a trace for a benchmark run; `--frame-rate cfg` keeps the
profile's own limit and VSync choice.

`--presentmon` also records `presentmon.csv`. `tools/presentmon_frames.py <pid>
<csv>` reads per-present metrics for any process from the installed Intel
PresentMon service, which needs no elevation, unlike the PresentMon console. The
metrics are CPU start and present QPC stamps, GPU latency/busy/time, time until
displayed and present mode. QPC stamps match the trace (`origin_timebase` / 5 on
this machine). They show whether a frame released on time missed its vblank
because the GPU finished late. `--gpu-sample` logs NVIDIA utilization, clocks
and performance state every 100 ms and lists memory-clock/performance-state
switches in the summary. A switch at a GPU gap would point to power management
rather than another process. `tools/heavy_frame_profile.py --gap-ms` prints the
stack samples taken between a present returning and the next gameplay clock
sample.

`NativeFramePacerTests` covers the locked cadence, cap/refresh matching, grid
arithmetic, late frames, lead growth and relaxation, spike misses, queue
drain, fixed-refresh steps and clock prediction. `NativeFrameRateTests` checks
the live limiter against the machine's real DWM grid when one is available.

## Other stalls found on the way

- **Audio decoder setup under a guest lock.** Frames of 12-34 ms every ~1.87 s
  were the main thread waiting on a guest mutex while the audio thread built a
  new `XmaSource` (three FFmpeg decoders) at each loop of a six-channel stream.
  Decoders are now pooled; see `docs/native-xma-source.md`.
- **First plays of resident sounds under the same lock.** Each new sound effect
  decoded its whole sample (0.6-3.6 ms) inside the original callback. Sounds
  longer than 16384 frames now decode an 8192-frame prefix there; a background
  worker finishes them and publishes the decoded image to the fresh cache. The
  in-game sounds are mono 24 kHz, so 24 such decoders are prewarmed on a
  background thread; see `docs/native-xma-source.md`.
- **Guest `Sleep(1)` at host timer granularity.** A main-thread guest delay
  took 9.8-15.2 ms. Nonzero guest delays now use a high-resolution waitable
  timer; see `docs/native-threads.md`.
- **Main-thread CPU headroom.** At 120 FPS the main thread was 96% busy in Medal
  of Homer (7.9 ms of an 8.34 ms refresh), so ordinary heavier frames ran late.
  `EngineShadowTextures::ownsCamera` (about 700 calls per frame) re-walked the
  same guest structures: it now keeps an exact value memo of the 31 guest words
  it reads (re-read and compared; permission epoch and record version checked).
  Character and static shadow meshes now use the write-watched
  `StaticMeshSourceCache` like rigid, skin and z-prepass meshes, instead of
  copying, decoding and hashing their unchanged source on every draw.
  `EngineRasters::ownedColor/ownedDepth` use a verified surface-identity hint
  instead of scanning every raster. Medal uncapped went from ~124 to ~148 FPS
  (main thread 7.9 -> 6.6 ms per frame); at 120 FPS late frames dropped from
  ~1,000 to ~20 per minute.

## Measurements (2026-10-14)

User settings: borderless 3440 × 1440, render 125%, SMAA, VSync on, 120 FPS
limit, one queued frame, on a 119.844 Hz panel. Land of Chocolate stationary
start, 40-second windows (about 4,750 frames), background benchmark window
(PresentMon reports hardware-composed independent flip, an overlay plane),
`--pacing-trace`. "Repeats" are refreshes that showed the
previous frame again according to DXGI flip times. "Animation error" is the
gameplay step minus the interval the display advanced.

| Build / path | Repeats per run | Frames with animation error > 0.5 ms | Animation error p99 | Release to flip p50 |
| --- | --- | --- | --- | --- |
| Old limiter and clock (`SIMPSONS_PACE_LOCK=0 SIMPSONS_PACE_PREDICT=0`), 7 runs | 14, 18, 25, 35, 35, 45, 47 | 89-267 | 0.9-6.7 ms | 5.1-6.8 ms |
| Refresh lock, lead ≤ ½ refresh, 2 runs | 13, 30 | 26, 58 | 0.07 ms (one run 8.3) | 4.2 ms |
| Refresh lock, adaptive lead ≤ ⅞ refresh, 3 runs | 4, 4, 11 | 6-16 | 0.06-0.08 ms | 6.8 ms |
| Plus decoder pool (2 runs), then also the delay timer (2 runs) | 0, 0; 1, 0 | 0-2 | < 0.07 ms | 6.3-6.6 ms |
| Final build: CPU work, debt/path leads, incremental audio (2 first-mission + 2 Medal runs) | 2, 1; 1, 0 | 0-4 | < 0.1 ms | 2.8-6.6 ms |
| 4.5 ms minimum lead, 1 ms timer guard: 2 first-mission + 9 Medal runs (80,500 frames) | 1, 0; 0, 2, 0, 1, 2, 1, 2, 1, 0 | 0-4 | < 0.07 ms | 5.3-6.9 ms |
| Spin-only short waits, 2 ms guard: 2 first-mission + 6 Medal runs (60,700 frames) | 0, 0; 1, 0, 0, 0, 0, 0 | 0-2 | < 0.07 ms | 5.3-7.3 ms |

A third decoder-pool run showed 129 repeats during a 0.7-second window in
which other processes raised total CPU use from about 15% to 54%; the benchmark's
CPU timeline flags such windows, and that run is excluded above.

Walking (plaza route) with the decoder pool: 0 repeats, release-interval stdev
0.053 ms. Medal of Homer start, final build (main thread ~88% busy): 0, 1, 0, 1, 1
and 0 repeats in runs of ~7,200 frames (one per 60 s at most). The remaining
repeats come from single game frames of 11-15 ms (guest logic events) or runs of
frames just over a refresh that exceed the largest buffer (7/8 of a refresh);
absorbing those would need a second queued frame (another refresh of latency).

The 11 runs with the 1 ms timer guard had three causes of repeats. In the first,
a pacing timer expired inside a system-wide latency spike and returned 11-18 ms
late. A timer canary in a separate process recorded the same delay at the same
QPC time, and the spike also occurs with the game closed. The second was a burst
of 10-12 ms guest frames. In the third, a frame was released on time and its
Present returned in 0.14 ms, but PresentMon shows its GPU work spanning 13.8 ms
with only 2.1 ms busy (the GPU waited ~11 ms inside the frame; normally ~4 ms
while it waits for the CPU's commands) and finishing after the vblank. Spinning
short waits removes the first cause. The other two caused 4 repeats in the
141,000 frames of both builds, about one per five minutes at 120 Hz. The GPU gaps did not line up with Wallpaper Engine's
presents (10 per second, up to 15 ms GPU busy while the game window is in the
background). VSync off keeps the free-running limiter; the
predicted clock halves the step error against releases (p1/p99 ±0.11 ms vs
±0.23 ms).

Not measured here: a foreground fullscreen window with independent flip or
variable refresh (G-SYNC), and real tearing with VSync off. All runs used a
background window so the benchmark never took focus. That window is already
scanned out on an overlay plane (hardware-composed independent flip), not
composed by DWM. `tools/benchmark_first_mission.py --foreground --pacing-trace
--presentmon` measures the foreground case in an unattended run. On a variable-refresh
display, locked releases still pace at the panel's maximum rate; frames whose
flips do not land on the DWM grid keep the percentile hold instead of
whole-refresh steps.
