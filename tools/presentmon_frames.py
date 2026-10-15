"""Per-present GPU and display timing of a running process from the PresentMon service.

python -B tools/presentmon_frames.py <pid> <output.csv> [--seconds N]

Uses the installed Intel PresentMon shared service (PresentMonAPI2Loader.dll), which
needs no elevation, unlike the PresentMon console capture. Each row is one present:
its CPU start and present-call QPC stamps, PresentMon's frame metrics in milliseconds
(GPU busy/latency, time until displayed, display change interval) and the present
mode. Rows are written until the process exits or the time limit is reached. QPC
values share the game's QueryPerformanceCounter domain (pacing trace origin / 5).
"""
from __future__ import annotations

import argparse
import ctypes
import csv
import sys
import time
from pathlib import Path

LOADER = Path(r"C:\Program Files\Intel\PresentMon\SDK\PresentMonAPI2Loader.dll")

# PM_METRIC values of PresentMonAPI.h (API 3.3) and how each frame value is stored.
METRICS = [
    ("cpu_start_qpc", 7, "Q"),
    ("present_start_qpc", 77, "Q"),
    ("between_presents_ms", 78, "d"),
    ("in_present_api_ms", 79, "d"),
    ("cpu_busy_ms", 9, "d"),
    ("gpu_latency_ms", 23, "d"),
    ("gpu_busy_ms", 14, "d"),
    ("gpu_wait_ms", 15, "d"),
    ("gpu_time_ms", 13, "d"),
    ("until_displayed_ms", 81, "d"),
    ("between_display_change_ms", 80, "d"),
    ("display_latency_ms", 24, "d"),
    ("dropped", 16, "?"),
    ("present_mode", 20, "i"),
    ("sync_interval", 18, "i"),
    ("allows_tearing", 22, "?"),
]
MODES = {0: "unknown", 1: "hardware_legacy_flip", 2: "hardware_legacy_copy", 3: "hardware_independent_flip",
         4: "composed_flip", 5: "composed_copy_gpu_gdi", 6: "composed_copy_cpu_gdi", 8: "hardware_composed_independent_flip"}


class QueryElement(ctypes.Structure):
    _fields_ = [("metric", ctypes.c_int), ("stat", ctypes.c_int), ("deviceId", ctypes.c_uint32),
                ("arrayIndex", ctypes.c_uint32), ("dataOffset", ctypes.c_uint64), ("dataSize", ctypes.c_uint64)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("pid", type=int)
    parser.add_argument("output", type=Path)
    parser.add_argument("--seconds", type=float, default=0, help="Stop after this long (0: when the process exits)")
    args = parser.parse_args()
    api = ctypes.CDLL(str(LOADER))
    session = ctypes.c_void_p()
    if status := api.pmOpenSession(ctypes.byref(session)):
        print(f"pmOpenSession failed: {status}", file=sys.stderr)
        return 1
    try:
        if status := api.pmStartTrackingProcess(session, ctypes.c_uint32(args.pid)):
            print(f"pmStartTrackingProcess failed: {status}", file=sys.stderr)
            return 1
        elements = (QueryElement * len(METRICS))(*[QueryElement(metric, 0, 0, 0, 0, 0) for _, metric, _ in METRICS])
        query, blob_size = ctypes.c_void_p(), ctypes.c_uint32()
        if status := api.pmRegisterFrameQuery(session, ctypes.byref(query), elements, ctypes.c_uint64(len(METRICS)),
                                              ctypes.byref(blob_size)):
            print(f"pmRegisterFrameQuery failed: {status}", file=sys.stderr)
            return 1
        capacity = 512
        blobs = (ctypes.c_uint8 * (blob_size.value * capacity))()
        types = {"Q": ctypes.c_uint64, "d": ctypes.c_double, "i": ctypes.c_int32, "?": ctypes.c_bool}
        deadline = time.monotonic() + args.seconds if args.seconds else None
        rows = 0
        with args.output.open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow([name for name, _, _ in METRICS])
            idle = 0
            while deadline is None or time.monotonic() < deadline:
                count = ctypes.c_uint32(capacity)
                status = api.pmConsumeFrames(query, ctypes.c_uint32(args.pid), blobs, ctypes.byref(count))
                if status:
                    break  # The process exited (or the service stopped tracking it).
                for frame in range(count.value):
                    base = ctypes.addressof(blobs) + frame * blob_size.value
                    row = []
                    for (name, _, kind), element in zip(METRICS, elements):
                        value = types[kind].from_address(base + element.dataOffset).value
                        row.append(MODES.get(value, value) if name == "present_mode" else
                                   f"{value:.4f}" if kind == "d" else int(value))
                    writer.writerow(row)
                rows += count.value
                if count.value:
                    idle = 0
                else:
                    idle += 1
                    if idle > 200:  # 10 s without presents.
                        break
                    time.sleep(0.05)
        api.pmFreeFrameQuery(query)
        api.pmStopTrackingProcess(session, ctypes.c_uint32(args.pid))
        print(f"{rows} presents -> {args.output}")
    finally:
        api.pmCloseSession(session)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
