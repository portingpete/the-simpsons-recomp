"""Summarize a SIMPSONS_PACING_TRACE csv: presentation cadence, timer accuracy,
gameplay dt versus the displayed interval, and DXGI display-side timing.

Usage: python tools/pacing_trace_report.py pacing.csv [--start S] [--end S]

Columns (microseconds, relative to the first traced event):
  sample_us        real start of the gameplay update that built the frame
  clock_us/dt_us   the gameplay clock value it used and its delta
  step_us          the gameplay step actually advanced (dt after the original clamp)
  arrival_us       present entry (limiter arrival); planned_us its planned release
  wake_us          actual release after waiting; present_end_us after Present returned
  target_us        locked: the vblank the frame aimed for; free: its planned release
  lead_us, mode    locked release lead; 0 free, 1 locked on slot, 2 locked late, 3 held
  grid_*           DWM refresh period and latest vblank used for the release
  last_present     DXGI count of this present; stat_present/stat_sync_us: latest flip shown

"Animation error" compares the step that built frame N with the time the display
advanced between frames N-1 and N (DXGI flip times when available, else releases).
When it is zero, motion advances exactly as far as the display did.
"""
from __future__ import annotations

import argparse
import collections
import csv
import json
import statistics
from pathlib import Path


def pct(values, q):
    if not values:
        return None
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(q * len(ordered)))]


def describe(values, digits=3):
    if not values:
        return None
    return {"n": len(values), "mean": round(statistics.fmean(values), digits),
            "stdev": round(statistics.pstdev(values), digits), "p1": round(pct(values, 0.01), digits),
            "p50": round(pct(values, 0.5), digits), "p99": round(pct(values, 0.99), digits),
            "min": round(min(values), digits), "max": round(max(values), digits)}


def load(path: Path):
    rows = []
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            try:
                rows.append({key: float(value) for key, value in row.items()})
            except (TypeError, ValueError):
                pass  # The final buffered row can be incomplete.
    return rows


def report(path: Path, start: float = 0.0, end: float | None = None) -> dict:
    rows = load(path)
    paced = [r for r in rows if r["wake_us"] >= 0]
    clock = "wake_us" if paced else "present_end_us"
    selected = [r for r in rows if r[clock] >= start * 1e6 and (end is None or r[clock] <= end * 1e6)]
    result: dict = {"trace": str(path), "rows": len(rows), "window_rows": len(selected), "clock": clock}
    if len(selected) < 3:
        return result
    ms = lambda us: us / 1000.0
    # Display flip time of each present, from any later row's statistics.
    flip = {}
    for r in rows:
        if r["stat_present"] > 0 and r["stat_sync_us"] >= 0:
            flip[int(r["stat_present"])] = r["stat_sync_us"]
    intervals, release_errors, display_errors, display, periods = [], [], [], [], []
    multi = 0
    for previous, current in zip(selected, selected[1:]):
        if current["index"] != previous["index"] + 1:
            continue
        interval = current[clock] - previous[clock]
        intervals.append(ms(interval))
        single = current["samples"] == 1 and current["step_us"] > 0
        if current["samples"] != 1:
            multi += 1
        if single:
            release_errors.append(ms(current["step_us"] - interval))
        a, b = int(previous["last_present"]), int(current["last_present"])
        if a and b == a + 1 and a in flip and b in flip:
            shown = flip[b] - flip[a]
            display.append(ms(shown))
            periods.append(current["grid_period_us"])
            if single:
                display_errors.append(ms(current["step_us"] - shown))
    latency, present_cost, timer, targets, leads, modes, flip_latency, misses = [], [], [], [], [], [], [], 0
    for r in selected:
        if r["sample_us"] >= 0:
            latency.append(ms(r[clock] - r["sample_us"]))
        present_cost.append(ms(r["present_end_us"] - r[clock]))
        if r["wake_us"] >= 0:
            timer.append(ms(r["wake_us"] - r["planned_us"]))
            targets.append(ms(r["interval_us"]))
            modes.append(int(r["mode"]))
            if r["mode"] in (1, 2):
                leads.append(ms(r["lead_us"]))
        p = int(r["last_present"])
        if p in flip and r["wake_us"] >= 0:
            flip_latency.append(ms(flip[p] - r["wake_us"]))
            if r["mode"] == 1 and flip[p] > r["target_us"] + r["grid_period_us"] / 2:
                misses += 1
    median = statistics.median(intervals)
    result.update(
        fps=round(1000 * len(intervals) / sum(intervals), 2),
        release_interval_ms=describe(intervals),
        release_within_0p25ms_of_median_pct=round(100 * sum(abs(v - median) <= 0.25 for v in intervals) / len(intervals), 2),
        release_over_1p5x_median=sum(v > 1.5 * median for v in intervals),
        animation_error_vs_release_ms=describe(release_errors),
        frames_with_multiple_or_no_updates=multi,
        sample_to_release_ms=describe(latency),
        present_call_ms=describe(present_cost),
    )
    # Refresh period: the recorded DWM grid, or (free pacing records none) the typical flip.
    recorded = [v for v in periods if v > 0]
    period_ms = ms(statistics.median(recorded)) if recorded else (statistics.median(display) if display else 0)
    refresh_counts = [round(v / period_ms) for v in display] if period_ms else []
    if display:
        result["display"] = {
            "interval_ms": describe(display),
            "refreshes_per_frame": {str(k): v for k, v in sorted(collections.Counter(refresh_counts).items())},
            "repeated_refreshes": sum(max(0, k - 1) for k in refresh_counts),
            "animation_error_ms": describe(display_errors),
            "animation_error_abs_p99_ms": round(pct([abs(e) for e in display_errors], 0.99), 3) if display_errors else None,
            "animation_error_abs_over_0p5ms": sum(abs(e) > 0.5 for e in display_errors),
            "release_to_flip_ms": describe(flip_latency),
        }
    if timer:
        counts = collections.Counter(modes)
        result.update(timer_overshoot_ms=describe(timer), pacing_interval_ms=describe(targets),
                      modes={"free": counts[0], "locked_on_slot": counts[1], "locked_late": counts[2], "held": counts[3]},
                      locked_lead_ms=describe(leads), locked_on_slot_misses=misses)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--start", type=float, default=0.0, help="Window start, seconds of trace time")
    parser.add_argument("--end", type=float, default=None)
    args = parser.parse_args()
    print(json.dumps(report(args.trace, args.start, args.end), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
