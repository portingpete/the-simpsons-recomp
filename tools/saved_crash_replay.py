"""Archive a first-mission crash recording and replay it from its starting save.

The archive contains the complete source run, the exact recorder executable and
its adjacent DLLs/native menu assets, and the game image. A replay gets private
writable stores and the recorded video preferences.
Playback uses the current recorder unless an archived or candidate executable
is explicitly selected.
It is an input replay, not a snapshot of the game world at the crash.
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
NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9_-]{0,63}\Z")
PROFILE_ID = re.compile(r"[A-Za-z0-9_-]{1,80}\Z")
RECORDING_NAME = re.compile(r"inputs-[A-Za-z0-9_-]+\.jsonl\Z")
PAD_RANGES = {"packet": (0, 0xFFFFFFFF), "buttons": (0, 0xFFFF),
              "lt": (0, 255), "rt": (0, 255),
              "lx": (-32768, 32767), "ly": (-32768, 32767),
              "rx": (-32768, 32767), "ry": (-32768, 32767)}
X_BUTTON = 0x4000  # Native keyboard J maps to XINPUT_GAMEPAD_X.


def read_json(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def integer(value: object, low: int, high: int) -> bool:
    return type(value) is int and low <= value <= high


def recording_info(path: Path) -> dict:
    """Require a complete shutdown recording with J pressed on its final cycle."""
    count = 0
    header = end = None
    previous_slot0 = last_slot0 = None
    with path.open("rb") as stream:
        for raw in stream:
            if not raw.endswith(b"\n") or len(raw.rstrip(b"\r\n")) > 1024:
                raise ValueError("Recording has an incomplete or oversized row")
            row = json.loads(raw)
            if not isinstance(row, dict):
                raise ValueError("Recording row is not an object")
            kind = row.get("type")
            if kind == "header":
                if (header is not None or count or end is not None or
                        row.get("version") != 1 or
                        row.get("boundary") != "returned_controller_state" or
                        not integer(row.get("start_scene"), 0, 0xFFFFFFFFFFFFFFFF)):
                    raise ValueError("Unsupported recording header")
                header = row
            elif kind == "input":
                if header is None or end is not None:
                    raise ValueError("Input row outside recording")
                if (not integer(row.get("seq"), count, count) or
                        not integer(row.get("slot"), count % 4, count % 4) or
                        row.get("consumer") != "game" or
                        not integer(row.get("status"), 0, 1167) or
                        row["status"] not in (0, 1167)):
                    raise ValueError(f"Invalid game poll at sequence {count}")
                for field, (low, high) in PAD_RANGES.items():
                    if not integer(row.get(field), low, high):
                        raise ValueError(f"Invalid {field} at sequence {count}")
                if row["status"] == 1167 and any(row[field] for field in PAD_RANGES):
                    raise ValueError(f"Disconnected poll {count} has controller data")
                if count % 4 == 0:
                    previous_slot0, last_slot0 = last_slot0, row
                count += 1
            elif kind == "end":
                if header is None or end is not None or count != row.get("samples"):
                    raise ValueError("Invalid recording end row")
                end = row
            else:
                raise ValueError("Unknown recording row")
    if (header is None or end is None or count == 0 or count % 4 or
            end.get("reason") != "shutdown" or
            not integer(end.get("end_scene"), 1, 0xFFFFFFFFFFFFFFFF)):
        raise ValueError("Crash recording must end with complete four-slot shutdown polls")
    if (last_slot0 is None or last_slot0["status"] != 0 or
            not (last_slot0["buttons"] & X_BUTTON) or
            (previous_slot0 is not None and previous_slot0["buttons"] & X_BUTTON)):
        raise ValueError("Final slot-0 poll must contain a fresh J/X press")
    return {"polls": count, "start_scene": header["start_scene"],
            "end_scene": end["end_scene"], "final_x_sequence": last_slot0["seq"]}


def regular_files(folder: Path) -> dict[str, Path]:
    """Enumerate files without following symlinks or directory junctions."""
    found = {}
    for path in folder.rglob("*"):
        if path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction()):
            raise ValueError(f"Archive tree contains a link: {path}")
        if path.is_file():
            found[path.relative_to(folder).as_posix()] = path
        elif not path.is_dir():
            raise ValueError(f"Archive tree contains a special file: {path}")
    return found


def failure_line(path: Path) -> str:
    failures = []
    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if "[FAILURE]" in line or "[THREAD FAILURE]" in line:
                failures.append(line.strip())
    return failures[-1] if failures else ""


def copy_trimmed_recording(source: Path, target: Path, skip_polls: int) -> None:
    """Remove only complete, neutral controller cycles before a later scene gate."""
    if skip_polls < 0 or skip_polls % 4:
        raise ValueError("Skipped polls must be a nonnegative multiple of four")
    if not skip_polls:
        shutil.copy2(source, target)
        return
    rows = [json.loads(line) for line in source.read_text(encoding="utf-8").splitlines()]
    header, polls, end = rows[0], rows[1:-1], rows[-1]
    if skip_polls >= len(polls):
        raise ValueError("Skipped polls must leave at least one complete controller cycle")
    for seq, row in enumerate(polls[:skip_polls]):
        if (row["slot"] != seq % 4 or
                row["status"] != (0 if seq % 4 == 0 else 1167) or
                any(row[field] for field in ("buttons", "lt", "rt", "lx", "ly", "rx", "ry"))):
            raise ValueError(f"Skipped poll {seq} is not a neutral four-slot input")
    kept = polls[skip_polls:]
    for seq, row in enumerate(kept):
        row["seq"] = seq
    end["samples"] = len(kept)
    with target.open("w", encoding="utf-8", newline="\n") as output:
        for row in (header, *kept, end):
            output.write(json.dumps(row, separators=(",", ":")) + "\n")


def archive(root: Path, name: str, source: Path) -> Path:
    if not NAME.fullmatch(name):
        raise ValueError("Replay name must be 1–64 letters, digits, underscores or hyphens")
    source = source.resolve(strict=True)
    if source.parent != (root / "build/input-recordings").resolve():
        raise ValueError("Source must be a direct child of build/input-recordings")
    regular_files(source)
    launch = read_json(source / "launch.json")
    result = read_json(source / "result.json")
    recordings = sorted(source.glob("inputs-*.jsonl"))
    if (len(recordings) != 1 or result.get("recordings") != [recordings[0].name] or
            not launch.get("first_mission") or not launch.get("recording_auto_start") or
            not integer(result.get("exit_code"), 1, 0xFFFFFFFF)):
        raise ValueError("Source is not one failed automatic first-mission recording")
    info = recording_info(recordings[0])
    if info["start_scene"] != 0:
        raise ValueError("First-mission replay must start at scene zero")
    source_failure = failure_line(source / "game.log")
    if not source_failure or source_failure == "[FAILURE] Native window closed":
        raise ValueError("Source log does not contain a game crash")
    config = read_json(root / "config/startup_replay.json")
    profile_id = launch.get("profile_id")
    save_index = config.get("save_index")
    if (not isinstance(profile_id, str) or not PROFILE_ID.fullmatch(profile_id) or
            profile_id != config.get("profile_id") or
            not isinstance(save_index, str) or not save_index or
            Path(save_index).is_absolute() or ".." in Path(save_index).parts):
        raise ValueError("Source profile or save selection is invalid")
    original_save = source / "initial-state/SIMPSONS_SLOT1.save"
    original_profile = source / "initial-state" / (profile_id + ".profile")
    if (not original_save.is_file() or not original_profile.is_file() or
            sha256(original_save) != launch.get("initial_save_sha256")):
        raise ValueError("Initial save/profile is missing or does not match the launch")
    executable = root / "build/native/SimpsonsInputRecorder.exe"
    image = root / "analysis/simpsons.pe"
    if (not executable.is_file() or not image.is_file() or
            sha256(executable) != launch.get("executable_sha256") or
            sha256(image) != launch.get("image_sha256")):
        raise ValueError("The original executable or game image changed since recording")

    output_root = root / "saved-replays"
    output_root.mkdir(parents=True, exist_ok=True)
    target = output_root / name
    if target.exists():
        raise FileExistsError(f"Named replay already exists: {target}")
    staging = output_root / ("." + name + "-" + uuid.uuid4().hex)
    staging.mkdir()
    try:
        shutil.copytree(source, staging / "run")
        (staging / "binary").mkdir()
        shutil.copy2(executable, staging / "binary/SimpsonsInputRecorder.exe")
        for dll in sorted(executable.parent.glob("*.dll")):
            if dll.is_symlink():
                raise ValueError(f"Linked runtime DLL is unsupported: {dll}")
            shutil.copy2(dll, staging / "binary" / dll.name)
        native_assets = executable.parent / "native-assets"
        if native_assets.is_dir():
            regular_files(native_assets)
            shutil.copytree(native_assets, staging / "binary/native-assets")
        (staging / "game-image").mkdir()
        shutil.copy2(image, staging / "game-image/simpsons.pe")
        copied_info = recording_info(staging / "run" / recordings[0].name)
        if copied_info != info:
            raise ValueError("Copied recording changed during archive creation")
        files = {name: {"sha256": sha256(path), "bytes": path.stat().st_size}
                 for name, path in sorted(regular_files(staging).items())}
        manifest = {"version": 1, "name": name,
                    "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
                    "source_run": str(source), "recording": recordings[0].name,
                    "profile_id": profile_id, "save_index": save_index,
                    "source_failure": source_failure, "recording_info": info,
                    "restore_method": "initial_disk_save_and_input_replay_to_crash",
                    "in_memory_save_state_captured": False, "files": files}
        write_json(staging / "manifest.json", manifest)
        if target.exists():
            raise FileExistsError(f"Named replay already exists: {target}")
        staging.rename(target)
    except Exception:
        if staging.exists() and staging.resolve().parent == output_root.resolve():
            shutil.rmtree(staging)
        raise
    return target


def verify(bundle: Path) -> dict:
    if bundle.is_symlink() or (hasattr(bundle, "is_junction") and bundle.is_junction()):
        raise ValueError("Replay archive cannot be a link")
    manifest = read_json(bundle / "manifest.json")
    if (manifest.get("version") != 1 or manifest.get("name") != bundle.name or
            manifest.get("restore_method") != "initial_disk_save_and_input_replay_to_crash" or
            not isinstance(manifest.get("files"), dict)):
        raise ValueError("Unsupported replay archive manifest")
    recording_name = manifest.get("recording")
    profile_id = manifest.get("profile_id")
    save_index = manifest.get("save_index")
    if (not isinstance(recording_name, str) or
            not RECORDING_NAME.fullmatch(recording_name) or
            not isinstance(profile_id, str) or not PROFILE_ID.fullmatch(profile_id) or
            not isinstance(save_index, str) or not save_index or
            Path(save_index).is_absolute() or ".." in Path(save_index).parts):
        raise ValueError("Replay archive has unsafe recording/profile/save paths")
    files = regular_files(bundle)
    files.pop("manifest.json", None)
    if set(files) != set(manifest["files"]):
        raise ValueError("Replay archive has missing or unexpected files")
    for name, path in files.items():
        expected = manifest["files"][name]
        if (not isinstance(expected, dict) or path.stat().st_size != expected.get("bytes") or
                sha256(path) != expected.get("sha256")):
            raise ValueError(f"Replay archive file changed: {path}")
    recording = bundle / "run" / recording_name
    if recording_info(recording) != manifest.get("recording_info"):
        raise ValueError("Replay recording metadata changed")
    source_launch = read_json(bundle / "run/launch.json")
    if (sha256(bundle / "binary/SimpsonsInputRecorder.exe") != source_launch.get("executable_sha256") or
            sha256(bundle / "game-image/simpsons.pe") != source_launch.get("image_sha256") or
            sha256(bundle / "run/initial-state/SIMPSONS_SLOT1.save") != source_launch.get("initial_save_sha256")):
        raise ValueError("Replay archive differs from its source launch")
    if (source_launch.get("profile_id") != profile_id or
            failure_line(bundle / "run/game.log") != manifest.get("source_failure")):
        raise ValueError("Replay archive metadata differs from its source run")
    return manifest


def play(root: Path, name: str, timeout: int = 600, executable: Path | None = None,
         start_scene: int = 0, skip_polls: int = 0, *, archived_executable: bool = False) -> Path:
    if not NAME.fullmatch(name):
        raise ValueError("Invalid replay name")
    if archived_executable and executable is not None:
        raise ValueError("Archived and candidate executables cannot be selected together")
    bundle = root / "saved-replays" / name
    manifest = verify(bundle)
    if timeout <= 0:
        raise ValueError("Timeout must be positive")
    if start_scene < 0:
        raise ValueError("Playback start scene cannot be negative")
    if skip_polls < 0 or skip_polls % 4:
        raise ValueError("Skipped polls must be a nonnegative multiple of four")
    default_executable = (bundle / "binary/SimpsonsInputRecorder.exe" if archived_executable else
                          root / "build/native/SimpsonsInputRecorder.exe")
    selected_executable = (executable or default_executable).resolve(strict=True)
    if not selected_executable.is_file():
        raise ValueError(f"Replay executable is not a file: {selected_executable}")
    relocate_for_assets = not (selected_executable.parent / "native-assets").is_dir()
    fallback_assets = root / "build/native/native-assets"
    if relocate_for_assets:
        for package in ("frontend/frontend.str", "simpsons_chars/simpsons_chars_global.str"):
            if not (fallback_assets / package).is_file():
                raise FileNotFoundError(f"Native Video menu assets unavailable: {fallback_assets / package}")
        regular_files(fallback_assets)
    # Runtime::load locates the original game files relative to the image's
    # grandparent. The archived image is retained for verification, but an image
    # under the archive would make it search for game files inside the archive.
    installed_image = root / "analysis/simpsons.pe"
    if (not (root / "Simpsons Game, The (USA)").is_dir() or
            not installed_image.is_file() or
            sha256(installed_image) != sha256(bundle / "game-image/simpsons.pe")):
        raise ValueError("Replay requires the archived game image and original game files at the workspace root")
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d-%H%M%SZ")
    run = root / "build/crash-replay-runs" / (stamp + "-" + name + "-" + uuid.uuid4().hex[:8])
    run.mkdir(parents=True, exist_ok=False)
    if relocate_for_assets:
        # Older archives predate the adjacent native menu packages. Supply
        # current packages in a private binary directory, never in the archive.
        private_binary = run / "binary"
        private_binary.mkdir()
        shutil.copy2(selected_executable, private_binary / selected_executable.name)
        for dll in selected_executable.parent.glob("*.dll"):
            shutil.copy2(dll, private_binary / dll.name)
        shutil.copytree(fallback_assets, private_binary / "native-assets")
        selected_executable = private_binary / selected_executable.name
    shutil.copytree(bundle / "run/content", run / "content")
    shutil.copytree(bundle / "run/profile", run / "profile")
    shutil.copytree(bundle / "run/initial-state", run / "initial-state")
    configured_profile = root / read_json(root / "config/startup_replay.json")["profile_store"]
    for video_preferences in (bundle / "run/initial-state/profile.video.cfg",
                              bundle / "run/profile.video.cfg",
                              configured_profile.with_name(configured_profile.name + ".video.cfg")):
        if video_preferences.is_file():
            shutil.copy2(video_preferences, run / "profile.video.cfg")
            break
    save_index = Path(manifest["save_index"])
    profile_name = manifest["profile_id"] + ".profile"
    save_target = run / "content" / save_index
    save_target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(run / "initial-state/SIMPSONS_SLOT1.save", save_target)
    shutil.copy2(run / "initial-state" / profile_name, run / "profile" / profile_name)
    playback = run / "playback-inputs.jsonl"
    copy_trimmed_recording(bundle / "run" / manifest["recording"], playback, skip_polls)
    command = [str(selected_executable), "--image",
               str(installed_image), "--frame-rate", "60",
               "--profile-store", str(run / "profile"), "--content-store", str(run / "content"),
               "--local-profile", "0:" + manifest["profile_id"],
               "--render-test-first-mission", "--input-playback", str(playback)]
    command += (["--input-playback-start-scene", str(start_scene)] if start_scene else
                ["--input-playback-from-first-poll"])
    launch = {"bundle": str(bundle), "command": command, "working_directory": str(root),
              "recording_sha256": sha256(playback),
              "source_recording_sha256": manifest["files"]["run/" + manifest["recording"]]["sha256"],
              "selected_executable_sha256": sha256(selected_executable),
              "executable_source": "archived" if archived_executable else "override" if executable else "current",
              "playback_start_scene": start_scene,
              "skipped_neutral_polls": skip_polls,
              "source_failure": manifest["source_failure"]}
    write_json(run / "launch.json", launch)
    timed_out = False
    with (run / "game.log").open("wb", buffering=0) as output:
        process = subprocess.Popen(command, cwd=root, stdout=output, stderr=subprocess.STDOUT,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        launch["pid"] = process.pid
        write_json(run / "launch.json", launch)
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        except BaseException:
            if process.poll() is None:
                process.terminate()
                process.wait()
            raise
    reproduced = not timed_out and failure_line(run / "game.log") == manifest["source_failure"]
    write_json(run / "result.json", {"pid": process.pid, "exit_code": process.returncode,
               "timed_out": timed_out, "reproduced_source_failure": reproduced,
               "failure": failure_line(run / "game.log")})
    if not reproduced and archived_executable:
        raise ValueError(f"Replay did not reproduce the saved failure; see {run / 'game.log'}")
    return run


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    save = commands.add_parser("save", help="Archive one failed recording without overwriting it")
    save.add_argument("name")
    save.add_argument("--run", type=Path, required=True, help="Source build/input-recordings run")
    check = commands.add_parser("verify", help="Verify every archived file against its hash")
    check.add_argument("name")
    replay = commands.add_parser("play", help="Launch a private replay from the saved archive")
    replay.add_argument("name")
    replay.add_argument("--timeout", type=int, default=600, help="Maximum replay time in seconds")
    executable = replay.add_mutually_exclusive_group()
    executable.add_argument("--executable", type=Path,
                            help="Candidate executable to test instead of the current recorder build")
    executable.add_argument("--archived-executable", action="store_true",
                            help="Use the exact archived recorder and require the original failure to recur")
    replay.add_argument("--start-scene", type=int, default=0,
                        help="Delay the first recorded poll until this native scene count")
    replay.add_argument("--skip-polls", type=int, default=0,
                        help="Skip this many initial neutral polls (a multiple of four)")
    args = parser.parse_args()
    if args.action == "save":
        print(f"Saved crash replay: {archive(ROOT, args.name, args.run)}")
    elif args.action == "verify":
        if not NAME.fullmatch(args.name):
            raise ValueError("Invalid replay name")
        bundle = ROOT / "saved-replays" / args.name
        manifest = verify(bundle)
        print(f"Verified crash replay: {bundle} ({manifest['recording_info']['polls']} polls)")
    else:
        print(f"Crash replay run: {play(ROOT, args.name, args.timeout, args.executable,
                                         args.start_scene, args.skip_polls,
                                         archived_executable=args.archived_executable)}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        raise SystemExit(f"Crash replay error: {error}") from error
