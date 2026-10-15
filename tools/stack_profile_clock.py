"""Correlate owned stack samples to QPC owner-frame intervals without symbolizing.

M/S capture records remain unchanged. Q declares the owned process/frequency
and sampling window; T links a conservative capture interval to its S ordinal.
Every sample survives correlation, including legacy/worker/unmatched samples.
"""

from __future__ import annotations

import argparse
import bisect
import json
from collections import Counter, defaultdict
from pathlib import Path

MAX_BYTES = 128 * 1024 * 1024
MAX_LINES = 1_000_000
FRAME_PREFIX = "[STACK FRAME CLOCK] "


def integers(text: str, required: tuple[str, ...]) -> dict:
    result = {}
    for token in text.split():
        if "=" not in token:
            raise ValueError(f"Malformed clock token: {token}")
        key, value = token.split("=", 1)
        if key in result:
            raise ValueError(f"Duplicate clock field: {key}")
        try:
            result[key] = int(value)
        except ValueError as error:
            raise ValueError(f"Noninteger clock field: {key}") from error
    if not set(required).issubset(result):
        raise ValueError(f"Clock record lacks fields: {sorted(set(required) - result.keys())}")
    return result


def bounded_lines(text: str):
    for number, line in enumerate(text.splitlines(), 1):
        if number > MAX_LINES:
            raise ValueError("Clock input exceeds line bound")
        yield number, line


def parse_capture(text: str) -> dict:
    modules, samples, clocks = {}, [], {}
    metadata = None
    for number, line in bounded_lines(text):
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "M":
            if len(parts) < 3:
                raise ValueError(f"Malformed module at line {number}")
            index = int(parts[1])
            if index < 0 or index in modules:
                raise ValueError("Invalid/duplicate module index")
            modules[index] = " ".join(parts[2:])
        elif parts[0] == "S":
            if len(parts) < 3 or int(parts[1]) <= 0:
                raise ValueError(f"Malformed stack sample at line {number}")
            stack = []
            for value in parts[2:]:
                module, rva = value.split(":")
                pair = [int(module), int(rva, 16)]
                if pair[0] < -1 or pair[1] < 0:
                    raise ValueError("Invalid stack module/RVA")
                stack.append(pair)
            samples.append({"sample": len(samples), "tid": int(parts[1]), "stack": stack,
                            "line": number, "raw": line, "clock": None})
        elif parts[0] == "Q":
            if metadata is not None:
                raise ValueError("Duplicate capture QPC metadata")
            metadata = integers(" ".join(parts[1:]),
                ("schema", "pid", "frequency", "capture_begin_qpc", "capture_end_qpc", "sample_count"))
            if metadata["schema"] != 1 or metadata["pid"] <= 0 or metadata["frequency"] <= 0:
                raise ValueError("Unsupported/invalid capture QPC domain")
            if not 0 < metadata["capture_begin_qpc"] <= metadata["capture_end_qpc"] or metadata["sample_count"] < 0:
                raise ValueError("Non-monotonic capture QPC window/count")
        elif parts[0] == "T":
            record = integers(" ".join(parts[1:]), ("sample", "tid", "begin_qpc", "context_qpc", "end_qpc"))
            ordinal = record["sample"]
            if ordinal < 0 or ordinal in clocks:
                raise ValueError("Invalid/duplicate sample clock ordinal")
            if record["tid"] <= 0 or not 0 < record["begin_qpc"] <= record["context_qpc"] <= record["end_qpc"]:
                raise ValueError("Non-monotonic sample QPC interval")
            clocks[ordinal] = record
    if metadata is None:
        if clocks:
            raise ValueError("Sample clocks lack capture QPC domain metadata")
        return {"modules": modules, "metadata": None, "samples": samples}
    if metadata["sample_count"] != len(samples) or set(clocks) != set(range(len(samples))):
        raise ValueError("Missing/out-of-range clock ordinal or sample-count mismatch")
    previous_end = metadata["capture_begin_qpc"]
    for sample in samples:
        record = clocks[sample["sample"]]
        if record["tid"] != sample["tid"]:
            raise ValueError("Sample clock ordinal points to another thread")
        if record["begin_qpc"] < previous_end or record["end_qpc"] > metadata["capture_end_qpc"]:
            raise ValueError("Sample QPC intervals are non-monotonic or outside capture window")
        sample["clock"] = record
        previous_end = record["end_qpc"]
    return {"modules": modules, "metadata": metadata, "samples": samples}


def parse_frame_clocks(text: str) -> list[dict]:
    records, identities, previous = [], set(), {}
    for number, line in bounded_lines(text):
        if not line.startswith(FRAME_PREFIX):
            continue
        row = integers(line[len(FRAME_PREFIX):],
            ("schema", "pid", "tid", "generation", "frame", "owner", "begin_qpc", "end_qpc", "frequency"))
        if row["schema"] != 1 or row["owner"] != 1 or any(row[key] <= 0 for key in
                ("pid", "tid", "generation", "frame", "frequency")):
            raise ValueError("Unsupported/invalid owner frame clock identity")
        if not 0 < row["begin_qpc"] <= row["end_qpc"]:
            raise ValueError("Non-monotonic owner frame QPC interval")
        identity = (row["pid"], row["tid"], row["generation"], row["frame"])
        if identity in identities:
            raise ValueError("Duplicate owner frame clock identity")
        identities.add(identity)
        thread = (row["pid"], row["tid"])
        before = previous.get(thread)
        if before is not None and (row["begin_qpc"] < before["end_qpc"] or
            row["frequency"] != before["frequency"] or
            (row["generation"] == before["generation"] and row["frame"] <= before["frame"])):
            raise ValueError("Owner frame clocks regress or change frequency")
        row.update(line=number, raw=line)
        records.append(row)
        previous[thread] = row
    return records


def correlate(capture: dict, frames: list[dict], expected_pid: int | None = None) -> dict:
    metadata = capture["metadata"]
    if metadata and expected_pid is not None and metadata["pid"] != expected_pid:
        raise ValueError("Capture belongs to another owned PID")
    by_thread = defaultdict(list)
    if metadata:
        for frame in frames:
            if frame["pid"] != metadata["pid"]:
                continue
            if frame["frequency"] != metadata["frequency"]:
                raise ValueError("Sample and owned frame QPC frequencies differ")
            by_thread[frame["tid"]].append(frame)
    # Frame clocks are ordered/non-overlapping per owner by the parser. Binary
    # lookup makes bounded 1 ms captures avoid a samples-times-frames scan.
    ends = {tid: [row["end_qpc"] for row in rows] for tid, rows in by_thread.items()}
    results = []
    for sample in capture["samples"]:
        result = {**sample, "status": "unknown", "frame": None, "candidate_frames": []}
        clock = sample["clock"]
        rows = by_thread.get(sample["tid"])
        if metadata is None or clock is None:
            result["reason"] = "legacy sample lacks QPC metadata"
        elif not rows:
            result["reason"] = "sampled thread lacks an owned process frame clock"
        else:
            lo, hi = clock["begin_qpc"], clock["context_qpc"]
            index = bisect.bisect_left(ends[sample["tid"]], lo)
            overlapping = []
            while index < len(rows) and rows[index]["begin_qpc"] <= hi:
                overlapping.append(rows[index])
                index += 1
            if len(overlapping) == 1 and overlapping[0]["begin_qpc"] < lo and hi < overlapping[0]["end_qpc"]:
                result.update(status="matched", reason="complete conservative capture interval is inside owner frame",
                              frame=overlapping[0])
            elif overlapping:
                result.update(status="boundary_ambiguous", reason="capture interval crosses or touches an owner boundary",
                              candidate_frames=overlapping)
            else:
                result["reason"] = "capture interval lies outside known owner frame clocks"
        results.append(result)
    return {"metadata": metadata, "modules": capture["modules"], "samples": results,
            "all_frame_clocks": frames, "status_counts": dict(Counter(row["status"] for row in results)),
            "limitations": ["Sampling suspensions perturb runtime; these are diagnostic stack counts, not clean FPS.",
                "Stop instant is only known within before-SuspendThread..after-GetThreadContext. Boundary intersections remain ambiguous.",
                "Resume/unwind end QPC records suspension cost but does not move the captured stack into a later frame.",
                "PID, owner TID and QPC frequency must match. Worker/legacy/outside-window samples remain unknown and are retained.",
                "Clock records require sampler and runtime-profiler opt-in; sampler-off traces add no timing/logging."]}


def read_bounded(path: Path) -> str:
    before = path.stat()
    if before.st_size > MAX_BYTES:
        raise ValueError(f"Input exceeds byte bound: {path}")
    with path.open("rb") as stream:
        raw = stream.read(MAX_BYTES + 1)
    after = path.stat()
    if len(raw) > MAX_BYTES or before.st_size != len(raw) or before.st_mtime_ns != after.st_mtime_ns:
        raise ValueError(f"Input changed or exceeded bound: {path}")
    return raw.decode("utf-8", errors="replace")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--log", required=True, type=Path, help="Matching owned process game.log")
    parser.add_argument("--pid", type=int, help="Independently known owned launch PID")
    parser.add_argument("--frame", type=int, help="Frame to count in compact stdout; full output retains every sample")
    parser.add_argument("--output", type=Path, help="Optional new JSON report; never replaces inputs")
    args = parser.parse_args()
    try:
        result = correlate(parse_capture(read_bounded(args.capture)), parse_frame_clocks(read_bounded(args.log)), args.pid)
        if args.output:
            with args.output.open("x", encoding="utf-8") as stream:
                json.dump(result, stream, indent=2)
                stream.write("\n")
    except (ValueError, OSError) as error:
        parser.error(str(error))
    print(json.dumps({"metadata": result["metadata"], "status_counts": result["status_counts"],
        "requested_frame": args.frame,
        "requested_frame_matched_samples": sum(row["status"] == "matched" and
            row["frame"]["frame"] == args.frame for row in result["samples"]) if args.frame else None,
        "sample_count": len(result["samples"]), "limitations": result["limitations"]}, indent=2))


if __name__ == "__main__":
    main()
