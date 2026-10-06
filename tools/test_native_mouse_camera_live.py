"""Verify raw mouse camera movement in a private, disabled background game.

The native playback stream supplies relative counts without desktop input or
cursor capture. Evidence comes from completed renderer readbacks, camera hook
receipts, and unchanged fingerprints of the main profile/content stores.
The default A prelude lets the original direct-stage startup assign its player
controller before the stationary camera comparison.
"""
from __future__ import annotations

import argparse
import ctypes
import datetime as dt
import json
import math
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import saved_crash_replay as replay
from tools.jev_telemetry import capture_telemetry, movement_result
from tools.test_graphics_settings_live import Run, USER, window_for

USER.GetForegroundWindow.argtypes = []
USER.GetForegroundWindow.restype = ctypes.c_void_p
TRACE = re.compile(
    r"\[NATIVE MOUSE CAMERA\] method=(orbit|look) owner=([0-9A-F]+) "
    r"counts=(-?\d+),(-?\d+) radians=([^,\s]+),([^\s]+) dt=([^\s]+)"
)


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def safe_entries(root: Path) -> tuple[list[Path], list[Path]]:
    """Scan ancestors and descendants without following Windows reparse points."""
    root = root.absolute()
    for path in (root, *root.parents):
        info = path.lstat()
        if info.st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError(f"Input store path contains a reparse point: {path}")
    if not root.is_dir():
        raise ValueError(f"Input store is not a directory: {root}")
    directories, files, pending = [], [], [root]
    while pending:
        folder = pending.pop()
        with os.scandir(folder) as entries:
            for entry in entries:
                info = entry.stat(follow_symlinks=False)
                path = Path(entry.path)
                if info.st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                    raise ValueError(f"Input store contains a reparse point: {path}")
                if stat.S_ISDIR(info.st_mode):
                    directories.append(path)
                    pending.append(path)
                elif stat.S_ISREG(info.st_mode):
                    files.append(path)
                else:
                    raise ValueError(f"Input store contains a special file: {path}")
    return directories, files


def copy_input_store(source: Path, target: Path) -> None:
    directories, files = safe_entries(source)
    target.mkdir(exist_ok=False)
    for path in sorted(directories, key=lambda p: len(p.parts)):
        (target / path.relative_to(source)).mkdir(exist_ok=False)
    for path in files:
        if path.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError(f"Input store changed to a reparse point during copying: {path}")
        shutil.copy2(path, target / path.relative_to(source), follow_symlinks=False)
    safe_entries(target)


def fingerprint(store: Path) -> dict:
    directories, files = safe_entries(store)
    return {
        "directories": sorted(path.relative_to(store).as_posix() for path in directories),
        "files": {
            path.relative_to(store).as_posix(): {
                "size": path.stat().st_size,
                "mtime_ns": path.stat().st_mtime_ns,
                "sha256": replay.sha256(path),
            } for path in sorted(files)
        },
    }


def main_fingerprints(config: dict) -> dict:
    result = {}
    for field in ("profile_store", "content_store"):
        relative = Path(config[field])
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError(f"Startup selection has an unsafe {field}")
        result[field] = fingerprint(ROOT / relative)
    prefs = (ROOT / config["profile_store"]).with_name(Path(config["profile_store"]).name + ".video.cfg")
    if os.path.lexists(prefs):
        if prefs.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError(f"Main video preferences are a reparse point: {prefs}")
        result["video_preferences"] = dict(size=prefs.stat().st_size,
            mtime_ns=prefs.stat().st_mtime_ns, sha256=replay.sha256(prefs))
    else:
        result["video_preferences"] = None
    return result


def make_recording(path: Path, neutral_polls: int, prelude_a: bool = False) -> None:
    # One slot-zero poll plus the three disconnected slots forms one cycle.
    # The long final neutral tail keeps replay active until normal WM_CLOSE.
    prelude_delay = 10 if prelude_a else 0
    prelude_polls = 8 if prelude_a else 0
    prelude_end = prelude_delay + prelude_polls
    motion_start = prelude_end + neutral_polls
    cycles = motion_start + 8 + 12000
    with path.open("w", encoding="utf-8", newline="\n") as output:
        output.write(json.dumps(dict(type="header", version=1,
            boundary="returned_controller_state", start_scene=0,
            save_state_captured=False, mouse_camera="raw_counts_per_slot0_poll"),
            separators=(",", ":")) + "\n")
        for cycle in range(cycles):
            moving = motion_start <= cycle < motion_start + 8
            active = cycle >= motion_start
            packet = 1 if cycle < motion_start else 2 if moving else 3
            if prelude_a:
                packet = (1 if cycle < prelude_delay else 2 if cycle < prelude_end else
                          3 if cycle < motion_start else 4 if moving else 5)
            for slot in range(4):
                row = dict(type="input", seq=cycle*4+slot, t_us=cycle*16667+slot,
                    consumer="game", slot=slot, status=0 if slot == 0 else 1167,
                    packet=packet if slot == 0 else 0,
                    buttons=0x1000 if prelude_delay <= cycle < prelude_end and slot == 0 else 0,
                    lt=0, rt=0,
                    lx=0, ly=0, rx=0, ry=0, mouse_native=int(active and slot == 0),
                    mouse_x=40 if moving and slot == 0 else 0, mouse_y=0)
                output.write(json.dumps(row, separators=(",", ":")) + "\n")
        output.write(json.dumps(dict(type="end", samples=cycles*4,
            t_us=cycles*16667, reason="user", end_scene=0), separators=(",", ":")) + "\n")


class MouseRun(Run):
    def __init__(self, folder: Path, config: dict, executable: Path,
                 recording: Path, scene: int, deadline: float, foreground: int | None):
        self.folder = folder
        self.captures = folder / "captures"
        self.captures.mkdir()
        self.log_path = folder / "game.log"
        self.log = self.log_path.open("wb", buffering=0)
        self.deadline = deadline
        self.foreground = foreground
        self.foreground_observations = [dict(hwnd=foreground, elapsed_seconds=0)]
        self.last_foreground = foreground
        self.observed_owned_windows = set()
        self.started = time.monotonic()
        self.last_capture = None
        self.observations = {}
        self.command = [str(executable), "--image", str(ROOT / "analysis/simpsons.pe"),
            "--render-test-first-mission", "--frame-rate", "60", "--mute-audio",
            "--profile-store", str(folder.parent / "profile"),
            "--content-store", str(folder.parent / "content"),
            "--local-profile", "0:" + config["profile_id"],
            "--input-playback", str(recording), "--input-playback-start-scene", str(scene),
            "--capture-frames", str(self.captures), "--capture-on-request"]
        try:
            self.process = subprocess.Popen(self.command, cwd=ROOT, stdout=self.log,
                stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW,
                env=dict(os.environ, SIMPSONS_BACKGROUND_WINDOW="1",
                    SIMPSONS_IGNORE_PHYSICAL_CONTROLLERS="1", SIMPSONS_MOUSE_CAMERA_TRACE="1"))
        except Exception:
            self.log.close()
            raise

    def wait(self, seconds: float) -> None:
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            super().wait(min(.1, until-time.monotonic()))
            current = USER.GetForegroundWindow()
            owned = window_for(self.process.pid)
            if owned:
                self.observed_owned_windows.add(owned)
            if owned and current == owned:
                raise RuntimeError("Owned background game took desktop foreground focus")
            if current != self.last_foreground:
                self.foreground_observations.append(dict(hwnd=current,
                    elapsed_seconds=round(time.monotonic()-self.started, 3)))
                self.last_foreground = current

    def capture_scene(self, name: str) -> dict:
        super().capture(name, [1280, 720], scene=True)
        metadata = json.loads(self.last_capture.with_suffix(".json").read_text(encoding="utf-8"))
        telemetry, status = capture_telemetry(metadata, metadata["presentation"])
        if telemetry is None or telemetry["player"] is None or telemetry["world"] is None:
            raise RuntimeError("Completed capture lacks qualified player/camera telemetry: " + str(status))
        self.observations[name]["telemetry"] = telemetry
        self.observations[name]["telemetry_status"] = status
        write_json(self.folder / "captures.json", self.observations)
        return telemetry


def receipts(text: str) -> list[dict]:
    result = []
    for match in TRACE.finditer(text):
        method, owner, x, y, pitch, yaw, delta_time = match.groups()
        result.append(dict(method=method, owner=owner, x=int(x), y=int(y),
            pitch=float(pitch), yaw=float(yaw), delta_time=float(delta_time)))
    return result


def verify_receipts(text: str) -> list[dict]:
    observed = receipts(text)
    if len(observed) != 8:
        raise RuntimeError(f"Expected eight original camera hook receipts, got {len(observed)}")
    for row in observed:
        if (row["x"] != 40 or row["y"] != 0 or not math.isfinite(row["delta_time"]) or
                not math.isclose(row["pitch"], 0, abs_tol=1e-6) or
                not math.isclose(abs(row["yaw"]), .1, abs_tol=1e-6)):
            raise RuntimeError("Raw counts were not consumed as .1-radian yaw: " + str(row))
    if not math.isclose(abs(sum(row["yaw"] for row in observed)), .8, abs_tol=1e-5):
        raise RuntimeError("The eight mouse steps did not produce a consistent .8-radian total")
    return observed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=ROOT / "build/native/SimpsonsNative.exe")
    parser.add_argument("--scene", type=int, default=233)
    parser.add_argument("--neutral-polls", type=int, default=500,
                        help="Neutral slot-zero polls after the A prelude; default 500 gives 8.3 seconds")
    parser.add_argument("--prelude-a", action=argparse.BooleanOptionalAction, default=True,
                        help="After 10 neutral polls, hold A for 8 polls; settle 6 seconds before baseline")
    parser.add_argument("--timeout", type=int, default=150, help="Total bounded run time, at most 150 seconds")
    options = parser.parse_args()
    if not 60 <= options.timeout <= 150 or options.scene < 1 or not 120 <= options.neutral_polls <= 6000:
        parser.error("--timeout must be 60..150, --neutral-polls 120..6000, and --scene must be positive")
    if options.prelude_a and options.neutral_polls < 450:
        parser.error("--prelude-a requires at least 450 neutral polls for its settling baseline")
    executable = options.executable.resolve(strict=True)
    if executable.name != "SimpsonsNative.exe" or not executable.is_file():
        parser.error("--executable must name an existing SimpsonsNative.exe")
    started = time.monotonic()
    deadline = started + options.timeout - 30
    foreground = USER.GetForegroundWindow()
    config = json.loads((ROOT / "config/startup_replay.json").read_text(encoding="utf-8"))
    original = main_fingerprints(config)
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d-%H%M%SZ")
    folder = ROOT / "build/native-mouse-camera-live" / (stamp + "-" + uuid.uuid4().hex[:8])
    folder.mkdir(parents=True, exist_ok=False)
    write_json(folder / "main-stores-before.json", original)
    copy_input_store(ROOT / config["profile_store"], folder / "profile")
    copy_input_store(ROOT / config["content_store"], folder / "content")
    # Private video preferences give stable readback dimensions and light effects.
    (folder / "profile.video.cfg").write_text("5 0 0 0 60 0 3 0 0 100 0 0 0\n", encoding="ascii")
    recording = folder / "inputs-raw-mouse.jsonl"
    make_recording(recording, options.neutral_polls, options.prelude_a)
    evidence = dict(passed=False, run=str(folder), foreground_before=foreground,
        desktop_input_injected=False, real_mouse_captured=False, primary_stores_used=False,
        recording_sha256=replay.sha256(recording), scene_gate=options.scene,
        neutral_slot0_polls=options.neutral_polls, prelude_a=options.prelude_a,
        prelude_delay_slot0_polls=10 if options.prelude_a else 0,
        prelude_slot0_polls=8 if options.prelude_a else 0,
        prelude_settle_seconds=6 if options.prelude_a else 0, expected_mouse_yaw_radians=.8)
    run = None
    failure = None
    print("RUN", folder, flush=True)
    try:
        child = folder / "game"
        child.mkdir()
        run = MouseRun(child, config, executable, recording, options.scene, deadline, foreground)
        write_json(child / "launch.json", dict(command=run.command, pid=run.process.pid,
            foreground=foreground, desktop_input_injected=False, real_mouse_captured=False,
            private_profile_store=True, private_content_store=True))
        while "[INPUT PLAYBACK] START" not in run.log_path.read_text(errors="replace"):
            run.wait(.1)
        if options.prelude_a:
            run.wait(6)
        before = run.capture_scene("before-mouse")
        while len(receipts(run.log_path.read_text(errors="replace"))) < 8:
            run.wait(.1)
        run.wait(.35)
        after = run.capture_scene("after-mouse")
        movement = movement_result(before, after)
        if movement is None or movement.get("camera_distance", 0) <= 1 or movement.get("player_distance", math.inf) >= .1:
            raise RuntimeError("Mouse replay did not move the camera with a stationary player: " + str(movement))
        text = run.log_path.read_text(errors="replace")
        observed = verify_receipts(text)
        if text.find("[NATIVE CAPTURE]") > text.find("[NATIVE MOUSE CAMERA]"):
            raise RuntimeError("Baseline readback occurred after the mouse movement began")
        evidence.update(movement=movement, hook_receipts=observed, captures=run.observations)
    except Exception as error:
        failure = error
        evidence["error"] = str(error)
    finally:
        if run is not None:
            try:
                run.close()
            except Exception as error:
                failure = failure or error
                evidence["shutdown_error"] = str(error)
            text = run.log_path.read_text(errors="replace")
            bad = [line for line in text.splitlines() if
                (line.startswith("[FAILURE]") and line != "[FAILURE] Native window closed") or
                any(marker in line for marker in ("[THREAD FAILURE]", "[HOST EXCEPTION]", "[TERMINATE]", "[READ MEMO MISMATCH]"))]
            if bad:
                failure = failure or RuntimeError("Game logged a failure during mouse inspection")
            evidence["failures"] = bad
            evidence["hook_receipts"] = receipts(text)
            evidence["hook_receipt_count"] = len(evidence["hook_receipts"])
            evidence["captures"] = run.observations
            evidence["foreground_observations"] = run.foreground_observations
            evidence["owned_window_handles"] = sorted(run.observed_owned_windows)
        try:
            final = main_fingerprints(config)
            write_json(folder / "main-stores-after.json", final)
            evidence["main_stores_unchanged"] = final == original
            if final != original:
                failure = failure or RuntimeError("Main profile/content stores changed during private inspection")
        except Exception as error:
            failure = failure or error
            evidence.update(main_stores_unchanged=False, fingerprint_error=str(error))
        evidence.update(foreground_after=USER.GetForegroundWindow(), duration_seconds=time.monotonic()-started)
        if run is not None and evidence["foreground_after"] in run.observed_owned_windows:
            failure = failure or RuntimeError("Owned background game took desktop foreground focus")
        if evidence["duration_seconds"] > options.timeout:
            failure = failure or RuntimeError("Mouse inspection exceeded its total deadline")
        evidence["passed"] = failure is None
        if failure is not None:
            evidence["error"] = str(failure)
        write_json(folder / "verification.json", evidence)
    if failure is not None:
        raise RuntimeError(f"Mouse camera inspection failed; evidence preserved at {folder}: {failure}")
    print("PASS", folder, flush=True)


if __name__ == "__main__":
    main()
