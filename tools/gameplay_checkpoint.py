"""Name a completed F9 input checkpoint, then replay it into live gameplay.

The package keeps the launch's initial on-disk save/profile and a complete input
prefix. It does not capture process memory. Replay may diverge if the runtime
changes; the native handoff checks its scene count before restoring live input.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import uuid


ROOT = Path(__file__).resolve().parents[1]
NAMES = re.compile(r"[A-Za-z0-9][A-Za-z0-9_-]{0,47}\Z")
FIELDS = {"packet": (0, 0xFFFFFFFF), "buttons": (0, 0xFFFF),
          "lt": (0, 255), "rt": (0, 255),
          "lx": (-32768, 32767), "ly": (-32768, 32767),
          "rx": (-32768, 32767), "ry": (-32768, 32767)}


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def selection(root: Path) -> dict:
    data = read_json(root / "config/startup_replay.json")
    for key in ("profile_id", "profile_store", "content_store", "save_index"):
        if not isinstance(data.get(key), str) or not data[key]:
            raise ValueError(f"Startup selection lacks {key}")
    for key in ("profile_store", "content_store", "save_index"):
        path = Path(data[key])
        if path.is_absolute() or ".." in path.parts:
            raise ValueError(f"Startup selection has an unsafe {key}")
    return data


def recording_info(path: Path) -> dict:
    """Validate exactly the poll prefix accepted by the native playback path."""
    with path.open("r", encoding="utf-8") as stream:
        first = next(stream)
        if len(first) > 1025:
            raise ValueError("Recording header exceeds the native line limit")
        header = json.loads(first)
        if (header.get("type") != "header" or header.get("version") != 1 or
                header.get("boundary") != "returned_controller_state" or
                type(header.get("start_scene")) is not int or header["start_scene"] < 0):
            raise ValueError(f"Unsupported recording header: {path}")
        count = 0
        end = None
        for line in stream:
            if len(line) > 1025:
                raise ValueError("Recording row exceeds the native line limit")
            row = json.loads(line)
            if row.get("type") == "end":
                if end is not None:
                    raise ValueError("Recording has two end rows")
                end = row
                continue
            if end is not None or row.get("type") != "input":
                raise ValueError("Recording has input outside its poll stream")
            if (type(row.get("seq")) is not int or row["seq"] != count or
                    type(row.get("slot")) is not int or row["slot"] != count % 4 or
                    row.get("consumer") != "game" or type(row.get("status")) is not int or
                    row["status"] not in (0, 1167)):
                raise ValueError(f"Recording has a non-game or out-of-order poll at {count}")
            for key, (low, high) in FIELDS.items():
                value = row.get(key)
                if type(value) is not int or not low <= value <= high:
                    raise ValueError(f"Recording has an invalid {key} at poll {count}")
            if row["status"] == 1167 and any(row[key] for key in FIELDS):
                raise ValueError(f"Disconnected poll {count} contains input")
            count += 1
    if (end is None or end.get("reason") != "checkpoint" or
            type(end.get("samples")) is not int or end["samples"] != count or
            count == 0 or count % 4 or type(end.get("end_scene")) is not int or end["end_scene"] <= 0):
        raise ValueError("F9 checkpoint must have complete four-slot polls and a positive end scene")
    return {"polls": count, "start_scene": header.get("start_scene"),
            "end_scene": end["end_scene"]}


def find_source(root: Path, requested: Path | None = None) -> tuple[Path, Path, dict]:
    runs = [requested] if requested else sorted((root / "build/input-recordings").glob("*"), reverse=True)
    for run in runs:
        if not run or not run.is_dir():
            continue
        manifest_path = run / "launch.json"
        if not manifest_path.is_file():
            continue
        manifest = read_json(manifest_path)
        if not manifest.get("first_mission") or not manifest.get("recording_auto_start"):
            continue
        for file in sorted(run.glob("inputs-*.jsonl"), reverse=True):
            try:
                info = recording_info(file)
            except (ValueError, UnicodeError, json.JSONDecodeError, StopIteration):
                continue
            return run, file, info
    raise ValueError("No complete F9 checkpoint from an automatic first-mission recording was found")


def create_checkpoint(root: Path, name: str, requested_run: Path | None = None) -> Path:
    if not NAMES.fullmatch(name):
        raise ValueError("Checkpoint name must be 1–48 letters, digits, underscores or hyphens")
    config = selection(root)
    run, recording, info = find_source(root, requested_run)
    launch = read_json(run / "launch.json")
    initial = run / "initial-state"
    save = initial / "SIMPSONS_SLOT1.save"
    profile_name = config["profile_id"] + ".profile"
    profile = initial / profile_name
    if launch.get("profile_id") != config["profile_id"] or not save.is_file() or not profile.is_file():
        raise ValueError("Recording is missing its matching initial save/profile")
    if sha256(save) != launch.get("initial_save_sha256"):
        raise ValueError("The recording's initial save changed after launch")
    output_root = root / "build/gameplay-checkpoints"
    output_root.mkdir(parents=True, exist_ok=True)
    target = output_root / name
    if target.exists():
        raise FileExistsError(f"Checkpoint already exists: {target}")
    staging = output_root / ("." + name + "-" + uuid.uuid4().hex)
    staging.mkdir()
    try:
        (staging / "initial-state").mkdir()
        shutil.copy2(save, staging / "initial-state/SIMPSONS_SLOT1.save")
        shutil.copy2(profile, staging / "initial-state" / profile_name)
        shutil.copy2(recording, staging / "inputs.jsonl")
        package = {
            "version": 1, "name": name,
            "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            "source_run": str(run), "source_recording": recording.name,
            "profile_id": config["profile_id"], "save_index": config["save_index"],
            "image_sha256": launch["image_sha256"],
            "recording_executable_sha256": launch["executable_sha256"],
            "initial_save_sha256": sha256(staging / "initial-state/SIMPSONS_SLOT1.save"),
            "initial_profile_sha256": sha256(staging / "initial-state" / profile_name),
            "recording_sha256": sha256(staging / "inputs.jsonl"),
            **info,
            "restore_method": "initial_disk_save_and_input_replay_to_live",
            "in_memory_save_state_captured": False,
        }
        write_json(staging / "checkpoint.json", package)
        staging.rename(target)
    except Exception:
        # This directory was created exclusively above and is a direct child.
        if staging.exists() and staging.resolve().parent == output_root.resolve():
            shutil.rmtree(staging)
        raise
    return target


def available(root: Path) -> list[Path]:
    return sorted((root / "build/gameplay-checkpoints").glob("*/checkpoint.json"))


def latest_checkpoint(root: Path) -> Path:
    """Package the newest complete F9 run once, then reuse that package."""
    run, recording, _ = find_source(root)
    name = "auto-" + run.name
    if not NAMES.fullmatch(name):
        raise ValueError(f"Latest recording folder cannot be named as a checkpoint: {run.name}")
    checkpoint = root / "build/gameplay-checkpoints" / name
    if not checkpoint.exists():
        return create_checkpoint(root, name, run)
    package = read_json(checkpoint / "checkpoint.json")
    source_launch = read_json(run / "launch.json")
    if (package.get("source_run") != str(run) or
            package.get("source_recording") != recording.name or
            package.get("recording_sha256") != sha256(recording) or
            package.get("initial_save_sha256") != source_launch.get("initial_save_sha256")):
        raise ValueError(f"Existing automatic checkpoint differs from its source: {checkpoint}")
    return checkpoint


def prepare_resume(root: Path, checkpoint: Path) -> tuple[Path, list[str], dict]:
    config = selection(root)
    package = read_json(checkpoint / "checkpoint.json")
    if package.get("version") != 1 or package.get("restore_method") != "initial_disk_save_and_input_replay_to_live":
        raise ValueError("Unsupported checkpoint package")
    if package.get("profile_id") != config["profile_id"] or package.get("save_index") != config["save_index"]:
        raise ValueError("Checkpoint profile/save selection differs from the current launch")
    profile_name = config["profile_id"] + ".profile"
    source_save = checkpoint / "initial-state/SIMPSONS_SLOT1.save"
    source_profile = checkpoint / "initial-state" / profile_name
    recording = checkpoint / "inputs.jsonl"
    for path, key in ((source_save, "initial_save_sha256"),
                      (source_profile, "initial_profile_sha256"),
                      (recording, "recording_sha256")):
        if not path.is_file() or sha256(path) != package.get(key):
            raise ValueError(f"Checkpoint file is missing or changed: {path}")
    info = recording_info(recording)
    if any(info[key] != package.get(key) for key in ("polls", "start_scene", "end_scene")):
        raise ValueError("Checkpoint recording metadata changed")
    image = root / "analysis/simpsons.pe"
    executable = root / "build/native/SimpsonsInputRecorder.exe"
    source_profile_store = root / config["profile_store"]
    source_content_store = root / config["content_store"]
    for path in (image, executable, source_profile_store / profile_name,
                 source_content_store / config["save_index"]):
        if not path.is_file():
            raise FileNotFoundError(f"Required resume file is missing: {path}")
    if sha256(image) != package.get("image_sha256"):
        raise ValueError("Original game image differs from the checkpoint")
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d-%H%M%SZ")
    run = root / "build/input-recordings" / (stamp + "-resume-" + uuid.uuid4().hex[:8])
    run.mkdir(parents=True, exist_ok=False)
    shutil.copytree(source_profile_store, run / "profile")
    shutil.copytree(source_content_store, run / "content")
    (run / "initial-state").mkdir()
    shutil.copy2(source_save, run / "initial-state/SIMPSONS_SLOT1.save")
    shutil.copy2(source_profile, run / "initial-state" / profile_name)
    save_target = run / "content" / config["save_index"]
    shutil.copy2(source_save, save_target)
    shutil.copy2(source_profile, run / "profile" / profile_name)
    copied_recording = run / "checkpoint-inputs.jsonl"
    shutil.copy2(recording, copied_recording)
    command = [str(executable), "--image", str(image), "--frame-rate", "60",
               "--profile-store", str(run / "profile"), "--content-store", str(run / "content"),
               "--local-profile", "0:" + config["profile_id"], "--render-test-first-mission",
               "--input-playback", str(copied_recording), "--input-playback-from-first-poll",
               "--input-playback-continue-live", "--input-playback-expected-end-scene", str(info["end_scene"]),
               "--input-recording-directory", str(run), "--input-recording-auto-start"]
    manifest = {
        "started_utc": stamp, "command": command, "working_directory": str(root),
        "executable_sha256": sha256(executable), "image_sha256": sha256(image),
        "initial_save_sha256": sha256(run / "initial-state/SIMPSONS_SLOT1.save"),
        "profile_id": config["profile_id"], "recording_hotkey": "F9",
        "recording_starts": "automatic_first_game_poll", "recording_auto_start": True,
        "first_mission": True, "in_memory_save_state_captured": False,
        "resumed_checkpoint": str(checkpoint),
        "checkpoint_recording_sha256": sha256(copied_recording),
        "checkpoint_end_scene": info["end_scene"],
        "executable_differs_from_recording": sha256(executable) != package["recording_executable_sha256"],
    }
    write_json(run / "launch.json", manifest)
    return run, command, manifest


def launch_resume(root: Path, checkpoint: Path) -> Path:
    run, command, manifest = prepare_resume(root, checkpoint)
    with (run / "game.log").open("wb", buffering=0) as output:
        process = subprocess.Popen(command, cwd=root, stdout=output, stderr=subprocess.STDOUT,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        manifest["pid"] = process.pid
        write_json(run / "launch.json", manifest)
        exit_code = process.wait()
    normal_window_close = False
    if exit_code:
        with (run / "game.log").open(encoding="utf-8", errors="replace") as stream:
            failures = [line.strip() for line in stream
                        if "[FAILURE]" in line or "[THREAD FAILURE]" in line]
        normal_window_close = failures == ["[FAILURE] Native window closed"]
    write_json(run / "result.json", {"pid": process.pid, "exit_code": exit_code,
               "normal_window_close": normal_window_close,
               "recordings": [path.name for path in sorted(run.glob("inputs-*.jsonl"))]})
    if exit_code and not normal_window_close:
        raise ValueError(f"Replay game exited with code {exit_code}; see {run / 'game.log'}")
    return run


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    create = commands.add_parser("create", help="Name the latest completed F9 checkpoint")
    create.add_argument("name", nargs="?")
    create.add_argument("--run", type=Path, help="Specific input-recordings run folder")
    resume = commands.add_parser("resume", help="Resume a named checkpoint")
    resume.add_argument("name", nargs="?")
    commands.add_parser("resume-last", help="Replay the latest completed F9 recording into live control")
    commands.add_parser("list", help="List named checkpoints")
    args = parser.parse_args()
    if args.action == "create":
        name = args.name or input("Checkpoint name (letters, digits, _ or -): ").strip()
        print(f"Saved checkpoint: {create_checkpoint(ROOT, name, args.run)}")
    elif args.action == "list":
        for file in available(ROOT):
            info = read_json(file)
            print(f"{info['name']}  scene {info['end_scene']}  {file.parent}")
    elif args.action == "resume-last":
        checkpoint = latest_checkpoint(ROOT)
        package = read_json(checkpoint / "checkpoint.json")
        print(f"Replaying {package['source_run']} to scene {package['end_scene']}. Control returns after the scene check passes.", flush=True)
        print(f"Game run: {launch_resume(ROOT, checkpoint)}")
    else:
        files = available(ROOT)
        if not files:
            raise ValueError("No named checkpoints exist; press F9 in a first-mission recording, then run create")
        if args.name:
            if not NAMES.fullmatch(args.name):
                raise ValueError("Invalid checkpoint name")
            checkpoint = ROOT / "build/gameplay-checkpoints" / args.name
        else:
            for index, file in enumerate(files, 1):
                info = read_json(file)
                print(f"{index}. {info['name']} (scene {info['end_scene']})")
            choice = int(input("Resume which checkpoint number? "))
            if not 1 <= choice <= len(files):
                raise ValueError("Checkpoint number is outside the list")
            checkpoint = files[choice - 1].parent
        if not (checkpoint / "checkpoint.json").is_file():
            raise FileNotFoundError(f"Checkpoint does not exist: {checkpoint}")
        print("Replaying to checkpoint. Control returns after the input prefix and scene check pass.", flush=True)
        print(f"Game run: {launch_resume(ROOT, checkpoint)}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        raise SystemExit(f"Checkpoint error: {error}") from error
