"""Reconstruct a four-slot XInput playback file from a keyboard game log.

The game logs keyboard state changes, not every poll. The first-mission route
has one four-slot game poll cycle per viewport depth-copy scene; compare the
completed 20260923-174948Z recording for the calibration. This tool fills the
unchanged cycles between logged changes and writes a playback-compatible JSONL.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re


SCENE = re.compile(r"\[NATIVE VIEWPORT DEPTH COPY\].*count=(\d+)")
INPUT = re.compile(
    r"\[NATIVE INPUT\] game-window keyboard buttons=([0-9A-F]+) "
    r"(?:rt=(\d+) )?left=\((-?\d+),(-?\d+)\) packet=(\d+)"
)


def scan(log: Path) -> tuple[list[tuple[int, dict[str, int]]], int, int]:
    scene = 0
    present = 0
    changes: list[tuple[int, dict[str, int]]] = []
    for line in log.open(encoding="utf-8", errors="replace"):
        scene_match = SCENE.search(line)
        if scene_match:
            next_scene = int(scene_match[1])
            if next_scene != scene + 1:
                raise ValueError(f"Depth-copy scenes have a gap: {scene} to {next_scene}")
            scene = next_scene
        if line.startswith("[NATIVE PRESENT]"):
            present += 1
        input_match = INPUT.search(line)
        if input_match:
            buttons, rt, lx, ly, packet = input_match.groups()
            state = {"packet": int(packet), "buttons": int(buttons, 16),
                     "lt": 0, "rt": int(rt or 0), "lx": int(lx), "ly": int(ly),
                     "rx": 0, "ry": 0}
            if not scene or (changes and (scene <= changes[-1][0] or
                                          state["packet"] != changes[-1][1]["packet"] + 1)):
                raise ValueError(f"Keyboard changes are not ordered at scene {scene}")
            changes.append((scene, state))
    if not changes or changes[0][1]["packet"] != 2:
        raise ValueError("Expected the first logged keyboard change after neutral packet 1")
    return changes, scene, present


def reconstruct(log: Path, output: Path, start_scene: int, end_scene: int) -> dict:
    changes, last_scene, present = scan(log)
    if not 1 <= start_scene < changes[0][0] or end_scene < last_scene:
        raise ValueError("Start must precede the first change; end must cover the complete log")
    output.parent.mkdir(parents=True, exist_ok=True)
    header = {"type": "header", "version": 1, "pid": 0,
              "started_utc": "reconstructed-from-game-log", "clock": "synthetic_scene_microseconds",
              "boundary": "returned_controller_state", "save_state_captured": False}
    state = {"packet": 1, "buttons": 0, "lt": 0, "rt": 0,
             "lx": 0, "ly": 0, "rx": 0, "ry": 0}
    cursor = 0
    sequence = 0
    with output.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(header, separators=(",", ":")) + "\n")
        for scene in range(start_scene, end_scene + 1):
            if cursor < len(changes) and changes[cursor][0] == scene:
                state = changes[cursor][1]
                cursor += 1
            for slot in range(4):
                row = {"type": "input", "seq": sequence,
                       "t_us": (scene - start_scene) * 16667 + slot * 25,
                       "consumer": "game", "slot": slot, "status": 0 if slot == 0 else 1167}
                row.update(state if slot == 0 else {key: 0 for key in state})
                stream.write(json.dumps(row, separators=(",", ":")) + "\n")
                sequence += 1
        stream.write(json.dumps({"type": "end", "samples": sequence,
                                 "t_us": (end_scene - start_scene + 1) * 16667,
                                 "reason": "reconstructed"}, separators=(",", ":")) + "\n")
    if cursor != len(changes):
        raise AssertionError("Unwritten keyboard changes")
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    return {"source_log": str(log.resolve()), "input_playback": str(output.resolve()),
            "input_playback_sha256": digest, "start_scene": start_scene,
            "end_scene": end_scene, "last_original_scene": last_scene,
            "original_presents": present, "original_changes": len(changes),
            "polls": sequence, "cycles": sequence // 4,
            "assumption": "one four-slot game poll cycle per depth-copy scene"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--start-scene", type=int, default=200)
    parser.add_argument("--end-scene", type=int, default=7400)
    args = parser.parse_args()
    result = reconstruct(args.log, args.output, args.start_scene, args.end_scene)
    manifest = args.output.with_suffix(".manifest.json")
    manifest.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
