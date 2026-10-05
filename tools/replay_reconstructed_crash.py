"""Replay a reconstructed crash-log XInput fixture from its original save state."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from replay_recorded_route import ROOT, prepare_run, replay, sha256


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, default=ROOT / "build/input-recordings/20260923-191003Z-f64bc1fc")
    parser.add_argument("--fixture", type=Path, default=ROOT / "build/reconstructed-input/20260923-191003Z-f64bc1fc.jsonl")
    parser.add_argument("--executable", type=Path, default=ROOT / "build/native/SimpsonsInputRecorder.exe")
    parser.add_argument("--timeout", type=float, default=600)
    parser.add_argument("--stop-scene", type=int, default=7350)
    parser.add_argument("--auto-defeat-loc-enemies", action="store_true",
                        help="Enable automatic melee rabbit defeats during the replay")
    args = parser.parse_args()
    reference = args.reference.resolve()
    fixture = args.fixture.resolve()
    manifest = json.loads(fixture.with_suffix(".manifest.json").read_text(encoding="utf-8"))
    if sha256(fixture) != manifest["input_playback_sha256"]:
        raise ValueError("Reconstructed input fixture changed after its manifest was written")
    metadata = {"recording": str(fixture), "recording_sha256": manifest["input_playback_sha256"],
                "reconstructed_from": manifest["source_log"], "playback_start_scene": manifest["start_scene"],
                "original_failure_scene": manifest["last_original_scene"],
                "original_changes": manifest["original_changes"], "reconstructed_cycles": manifest["cycles"]}
    folder, command = prepare_run(reference, args.executable.resolve(), metadata, False)
    command += ["--input-playback", str(folder / fixture.name),
                "--input-playback-start-scene", str(manifest["start_scene"])]
    if args.auto_defeat_loc_enemies:
        command.append("--auto-defeat-loc-enemies")
    launch = json.loads((folder / "launch.json").read_text(encoding="utf-8"))
    launch["command"] = command
    (folder / "launch.json").write_text(json.dumps(launch, indent=2) + "\n", encoding="utf-8")
    print(f"REPLAY {folder}", flush=True)
    result = replay(folder, command, [], args.timeout, args.stop_scene, False, False, (), False)
    (folder / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    compact = {key: value for key, value in result.items()
               if key not in ("playback_states", "features")}
    compact["playback_state_count"] = len(result["playback_states"])
    compact["feature_count"] = len(result["features"])
    print(json.dumps({"folder": str(folder), **compact}, indent=2), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
