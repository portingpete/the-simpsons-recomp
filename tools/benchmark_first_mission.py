"""Measure average FPS in Land of Chocolate, launched directly into the first mission.

Each run copies the configured profile/content stores into a new folder under
build/fps-benchmarks, starts the game with --render-test-first-mission and
frame-only timing and SIMPSONS_BACKGROUND_WINDOW=1: the game window opens without
activation, outside the taskbar, at the bottom of the z-order (never topmost, never
focused, never over the user's windows), and the tool disables input to it so desktop
typing cannot steer Homer. SIMPSONS_IGNORE_PHYSICAL_CONTROLLERS=1 makes physical XInput
pads read as disconnected (XInput is not focus-bound, so a controller used elsewhere on
the machine would otherwise move Homer or rumble mid-run); walks still use commands. Frames whose presentation was occluded are counted in
the summary (accepted_pct and window_accepted_pct); an occluded run can differ from a visible one, so compare runs with similar acceptance.
Optional preset or file walks are sent through the game's ordinary controller
command channel. The summary reports the mean FPS of the accepted presentation
intervals inside the measurement window, including rejected presentations, plus a per-second process/system CPU
timeline: external load shows up there and invalidates a window.

Movement is simulated with real frame times, so walks can end in slightly
different places on different builds; compare captures (--capture-at) before
comparing their numbers. The stationary start view is the controlled case.
Capture mode also enables continuing per-draw diagnostic dumps; runs with
--capture-at are diagnostic evidence and must not be used as clean FPS trials.
"""
from __future__ import annotations

import argparse
import csv
import ctypes
from ctypes import wintypes as W
import datetime
import json
import os
import re
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
# Two-segment walks that land in repeatable views from the first-mission start.
WALKS = {
    "plaza": ["PAD 0000 0 32767 2000", "PAD 0000 0 32767 1500", "PAD 0000 0 -32768 700"],
    "wide": ["PAD 0000 0 -32768 2000", "PAD 0000 0 -32768 1200", "PAD 0000 0 32767 600"],
}

kernel = ctypes.WinDLL("kernel32", use_last_error=True)
user = ctypes.WinDLL("user32", use_last_error=True)


class FileTime(ctypes.Structure):
    _fields_ = [("low", W.DWORD), ("high", W.DWORD)]

    def seconds(self) -> float:
        return ((self.high << 32) | self.low) / 10_000_000


kernel.GetProcessTimes.argtypes = [W.HANDLE] + [ctypes.POINTER(FileTime)] * 4
kernel.GetProcessTimes.restype = W.BOOL
kernel.GetSystemTimes.argtypes = [ctypes.POINTER(FileTime)] * 3
kernel.GetSystemTimes.restype = W.BOOL
ENUM = ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
user.EnumWindows.argtypes = [ENUM, W.LPARAM]
user.GetWindowThreadProcessId.argtypes = [W.HWND, ctypes.POINTER(W.DWORD)]
user.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, ctypes.c_int]
user.SetWindowPos.argtypes = [W.HWND, W.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, W.UINT]
user.EnableWindow.argtypes = [W.HWND, W.BOOL]
user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]


def process_cpu(handle) -> float:
    times = [FileTime() for _ in range(4)]
    if not kernel.GetProcessTimes(int(handle), *(ctypes.byref(t) for t in times)):
        return float("nan")
    return times[2].seconds() + times[3].seconds()


class ThreadEntry(ctypes.Structure):
    _fields_ = [("size", W.DWORD), ("usage", W.DWORD), ("tid", W.DWORD), ("owner", W.DWORD), ("base_pri", W.LONG),
                ("delta_pri", W.LONG), ("flags", W.DWORD)]


kernel.CreateToolhelp32Snapshot.restype = W.HANDLE
kernel.OpenThread.restype = W.HANDLE


def thread_cpu(pid: int) -> dict[int, float]:
    """Cumulative CPU seconds (user+kernel) of every thread of the process."""
    out: dict[int, float] = {}
    snap = kernel.CreateToolhelp32Snapshot(0x4, 0)  # TH32CS_SNAPTHREAD
    if not snap or snap == W.HANDLE(-1).value:
        return out
    entry = ThreadEntry()
    entry.size = ctypes.sizeof(entry)
    ok = kernel.Thread32First(snap, ctypes.byref(entry))
    while ok:
        if entry.owner == pid:
            handle = kernel.OpenThread(0x0800, False, entry.tid)  # THREAD_QUERY_LIMITED_INFORMATION
            if handle:
                times = [FileTime() for _ in range(4)]
                if kernel.GetThreadTimes(handle, *(ctypes.byref(t) for t in times)):
                    out[entry.tid] = times[2].seconds() + times[3].seconds()
                kernel.CloseHandle(handle)
        ok = kernel.Thread32Next(snap, ctypes.byref(entry))
    kernel.CloseHandle(snap)
    return out


def system_times() -> tuple[float, float]:
    idle, kernel_time, user_time = FileTime(), FileTime(), FileTime()
    kernel.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel_time), ctypes.byref(user_time))
    return idle.seconds(), kernel_time.seconds() + user_time.seconds()  # Kernel time includes idle.


def game_window(pid: int):
    found = []

    @ENUM
    def visit(hwnd, _):
        owner = W.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(64)
        user.GetClassNameW(hwnd, name, 64)
        if owner.value == pid and name.value == "SimpsonsNativeWindow":
            found.append(hwnd)
        return True

    user.EnumWindows(visit, 0)
    return found[0] if len(found) == 1 else None


def summarize(frames: Path, start: float, end: float | None) -> dict:
    rows = []
    accepted = []
    with frames.open(newline="") as stream:
        for row in csv.DictReader(stream):
            try:
                rows.append((float(row["elapsed_ms"]) / 1000, float(row["frame_ms"])))
                accepted.append(row["display_accepted"] == "1")
            except (KeyError, ValueError):
                pass  # The final buffered row can be incomplete.
    if not rows:
        return {"frames": 0}
    stop = min(end, rows[-1][0]) if end else rows[-1][0]
    selected = [i for i, (at, _) in enumerate(rows) if start <= at <= stop]
    window = [rows[i][1] for i in selected]
    result = {"window_s": [start, round(stop, 2)], "frames": len(window)}
    if window:
        ordered = sorted(window)
        pick = lambda q: ordered[min(len(ordered) - 1, int(q * len(ordered)))]
        median = statistics.median(window)
        # Steadiness: tail percentiles, spread, and hitch counts relative to the median frame.
        result.update(avg_fps=round(len(window) / (sum(window) / 1000), 2),
                      median_ms=round(median, 3),
                      p95_ms=round(pick(0.95), 3), p99_ms=round(pick(0.99), 3), p999_ms=round(pick(0.999), 3),
                      max_ms=round(ordered[-1], 3),
                      stdev_ms=round(statistics.pstdev(window), 3),
                      low1pct_fps=round(1000 / pick(0.99), 1),
                      hitches_over_1p5x=sum(1 for ms in window if ms > 1.5 * median),
                      frames_over_50ms=sum(1 for ms in window if ms > 50),
                      frame_budget_120_ms=1000 / 120,
                      frames_over_120_budget=sum(1 for ms in window if ms > 1000 / 120),
                      pct_over_120_budget=round(100 * sum(ms > 1000 / 120 for ms in window) / len(window), 2),
                      pct_over_120_budget_plus_1ms=round(100 * sum(ms > 1000 / 120 + 1 for ms in window) / len(window), 2))
    buckets: dict[int, list[float]] = {}
    for at, ms in rows:
        buckets.setdefault(int(at // 10) * 10, []).append(ms)
    if accepted:
        result["accepted_pct"] = round(100 * sum(accepted) / len(accepted), 1)
    if selected:
        result["window_accepted_pct"] = round(100 * sum(accepted[i] for i in selected) / len(selected), 1)
    result["fps_per_10s"] = {str(k): round(len(v) / (sum(v) / 1000), 1) for k, v in sorted(buckets.items()) if sum(v)}
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("name", help="Run label used in the output folder name")
    parser.add_argument("--executable", type=Path, default=ROOT / "build/native/SimpsonsNative.exe")
    parser.add_argument("--seconds", type=float, default=150, help="Run length before the game is closed")
    parser.add_argument("--start", type=float, default=30, help="Measurement window start (seconds of frame timing)")
    parser.add_argument("--end", type=float, default=0, help="Measurement window end; 0 means the end of the run")
    parser.add_argument("--frame-rate", choices=["uncapped", "60", "120"], default="uncapped")
    parser.add_argument("--render-resolution", type=int, default=-1,
                        help="Render resolution index (3 = 2560x1440, 6 = 3440x1440); -1 keeps the profile default")
    parser.add_argument("--graceful", action="store_true", help="Close the window and wait for a normal exit instead of terminating")
    parser.add_argument("--detailed", action="store_true",
                        help="Record the per-frame phase buckets (present, upload, Im2D, draw, pacing...) in frames.csv")
    parser.add_argument("--walk", help="Preset (" + ", ".join(WALKS) + ") or a file of controller commands")
    parser.add_argument("--walk-at", type=float, default=20, help="Seconds after launch to send the walk")
    parser.add_argument("--capture-at", default="", help="Comma list of seconds after launch to capture a frame")
    args = parser.parse_args()
    if not 10 <= args.seconds <= 3600:
        parser.error("--seconds must be between 10 and 3600")
    captures = [float(value) for value in args.capture_at.split(",") if value]
    walk = None
    if args.walk:
        walk = "\n".join(WALKS[args.walk]) + "\n" if args.walk in WALKS else Path(args.walk).read_text(encoding="utf-8")
    executable = args.executable.resolve()
    if executable.name != "SimpsonsNative.exe" or not executable.is_file():
        parser.error("--executable must name an existing SimpsonsNative.exe")

    config = json.loads((ROOT / "config/startup_replay.json").read_text(encoding="utf-8"))
    folder = ROOT / "build/fps-benchmarks" / f"{args.name}-{datetime.datetime.now():%Y%m%d-%H%M%S}"
    folder.mkdir(parents=True)
    shutil.copytree(ROOT / config["profile_store"], folder / "profile")
    shutil.copytree(ROOT / config["content_store"], folder / "content")
    if args.render_resolution >= 0:
        # version resolution fullscreen vsync rate render filtering antialiasing
        (folder / "profile.video.cfg").write_text(f"4 0 0 0 0 {args.render_resolution} 0 0\n")
    command = [str(executable), "--image", str(ROOT / "analysis/simpsons.pe"), "--render-test-first-mission",
               "--profile-store", str(folder / "profile"), "--content-store", str(folder / "content"),
               "--local-profile", "0:" + config["profile_id"],
               "--frame-timing", str(folder / "frames.csv")]
    if not args.detailed:
        command += ["--frame-timing-frames-only"]
    command += ["--uncapped-frame-rate"] if args.frame_rate == "uncapped" else ["--frame-rate", args.frame_rate]
    if walk:
        (folder / "controller.commands").write_bytes(b"")
        command += ["--controller-input", str(folder / "controller.commands")]
    if captures:
        command += ["--capture-frames", str(folder / "captures"), "--capture-on-request"]
        (folder / "captures").mkdir()
    (folder / "launch.json").write_text(json.dumps({"command": command}, indent=2) + "\n", encoding="utf-8")

    environment = dict(os.environ, SIMPSONS_BACKGROUND_WINDOW="1", SIMPSONS_IGNORE_PHYSICAL_CONTROLLERS="1")
    with (folder / "game.log").open("wb") as log:
        process = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, env=environment)
    started = time.monotonic()
    placed = False
    samples = [(0.0, process_cpu(process._handle), system_times())]
    thread_samples = [(0.0, thread_cpu(process.pid))]
    graceful_shutdown = False
    forced_termination = False
    try:
        while time.monotonic() - started < args.seconds and process.poll() is None:
            now = time.monotonic() - started
            if not placed and (hwnd := game_window(process.pid)):
                # Already created non-activating at the bottom of the z-order by the game;
                # disabled so it receives no keyboard/mouse input.
                user.EnableWindow(hwnd, False)
                placed = True
            if walk and now >= args.walk_at:
                with (folder / "controller.commands").open("a", encoding="utf-8") as out:
                    out.write(walk)
                walk = None
            if captures and now >= captures[0]:
                captures.pop(0)
                request = folder / "captures/capture.request"
                if not request.exists():
                    request.write_bytes(b"")
            time.sleep(1)
            samples.append((time.monotonic() - started, process_cpu(process._handle), system_times()))
            thread_samples.append((samples[-1][0], thread_cpu(process.pid)))
    finally:
        exit_code = process.poll()
        exited_early = exit_code is not None
        if exit_code is None and args.graceful and (hwnd := game_window(process.pid)):
            # Close the window like a user would so the game runs its normal shutdown
            # (profile-guided optimization training runs write their profile at exit).
            user.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
            try:
                process.wait(60)
            except subprocess.TimeoutExpired:
                pass
            exit_code = process.poll()
            # The runtime currently reports its requested WM_CLOSE through the
            # exception path and exits with 1. Accept that exact terminal reason,
            # while keeping a crash or a timed-out forced close distinguishable.
            terminal_lines = (folder / "game.log").read_text(encoding="utf-8", errors="replace").splitlines()
            failures = [line for line in terminal_lines if line.startswith("[FAILURE]")]
            unexpected = any(marker in line for line in terminal_lines
                             for marker in ("[HOST EXCEPTION]", "[TERMINATE]", "[READ MEMO MISMATCH]"))
            graceful_shutdown = exit_code is not None and not unexpected and (
                (exit_code == 0 and not failures) or
                (exit_code == 1 and failures == ["[FAILURE] Native window closed"]))
        if exit_code is None:
            forced_termination = True
            process.terminate()
            try:
                process.wait(10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(10)
            exit_code = process.returncode

    timeline = []
    for (t0, c0, s0), (t1, c1, s1) in zip(samples, samples[1:]):
        total = s1[1] - s0[1]
        timeline.append({"t": round(t1, 1), "game_cores": round((c1 - c0) / (t1 - t0), 2),
                         "system_busy_pct": round(100 * (1 - (s1[0] - s0[0]) / total), 1) if total else None})
    summary = summarize(folder / "frames.csv", args.start, args.end or None)
    # The busiest guest thread's CPU time per presented frame is far less sensitive to
    # desktop load than wall-clock FPS: it only counts time the thread actually ran.
    first = next((s for s in thread_samples if s[0] >= args.start), None)
    last = thread_samples[-1]
    if first and last[0] > first[0] and summary.get("frames"):
        deltas = {tid: last[1][tid] - first[1].get(tid, 0.0) for tid in last[1]}
        main_tid = max(deltas, key=deltas.get)
        window_s = last[0] - first[0]
        frames = summary["frames"] * min(1.0, window_s / max(1e-9, summary["window_s"][1] - summary["window_s"][0]))
        summary["main_thread_cpu_ms_per_frame"] = round(1000 * deltas[main_tid] / max(1.0, frames), 3)
        summary["main_thread_busy_pct"] = round(100 * deltas[main_tid] / window_s, 1)
    busy = [row["system_busy_pct"] for row in timeline if row["system_busy_pct"] is not None]
    video = re.findall(r"\[NATIVE VIDEO\] window=(\d+)x(\d+).*?render=(\d+)x(\d+) aa=(\d+)",
                       (folder / "game.log").read_text(encoding="utf-8", errors="replace"))
    summary.update(name=args.name, folder=str(folder), executable=str(executable),
                   game_exited_early=exited_early, peak_system_busy_pct=max(busy) if busy else None,
                   exit_code=exit_code, graceful_shutdown=graceful_shutdown,
                   forced_termination=forced_termination,
                   frame_rate=args.frame_rate, detailed=args.detailed, walk=args.walk,
                   capture_diagnostics_enabled=bool(args.capture_at),
                   timing_purpose="diagnostic capture" if args.capture_at else "performance measurement",
                   video={"window": [int(x) for x in video[-1][:2]],
                          "render": [int(x) for x in video[-1][2:4]], "antialiasing": int(video[-1][4])} if video else None,
                   cpu_window_note="Thread samples use launch time; frame intervals use first-presentation time. CPU/frame is approximate.",
                   logical_processors=os.cpu_count(), cpu_timeline=timeline)
    (folder / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in summary.items() if key != "cpu_timeline"}, indent=2))
    return 1 if exited_early or (args.graceful and not graceful_shutdown) else 0


if __name__ == "__main__":
    sys.exit(main())
