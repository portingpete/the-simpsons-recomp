"""Replay the recorded first-mission XInput polls and capture rendered frames.

The native playback source reads every returned controller state in sequence.
The older scene-scheduled keyboard route remains available with --keyboard.
"""
from __future__ import annotations

import argparse
import csv
import ctypes
from ctypes import wintypes as W
import datetime
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "build/input-recordings/20260923-153954Z-caca9639"
SCENE_RE = re.compile(r"\[NATIVE VIEWPORT DEPTH COPY\].*count=(\d+)")
KEY_RE = re.compile(
    r"\[NATIVE INPUT\] game-window keyboard buttons=([0-9A-F]+) "
    r"rt=(\d+) left=\((-?\d+),(-?\d+)\) packet=(\d+)"
)
# 526 is the first reached mesh-particle draw; 650 uploads the bridge's Arcs
# texture.  The consecutive river samples make frame-to-frame flicker visible.
CAPTURE_SCENES = (260, 318, 319, 320, 321, 322, 400, 520, 526, 540, 620, 645, 646, 647, 650, 655,
                  678, 679, 680, 681, 682, 700, 735, 745, 760)
SPARSE_CAPTURE_SCENES = (260, 320, 520, 600, 650, 670, 680, 690, 700, 710)
# The direct first-mission route's scene counts trail presentation counts by
# about 30. This fixed interval covers movement between scene 340 and 500,
# before the first particle draw and bridge pickup.
BENCHMARK_PRESENTATIONS = (370, 530)


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def keys_for(buttons: int, rt: int, lx: int, ly: int) -> set[int]:
    if buttons & ~(0x1000 | 0x4000) or rt or lx not in (-32768, 0, 32767) or ly not in (-32768, 0, 32767):
        raise ValueError(f"Recorded route contains an unmapped control: {(buttons, rt, lx, ly)}")
    keys = set()
    if lx:
        keys.add(ord("A" if lx < 0 else "D"))
    if ly:
        keys.add(ord("S" if ly < 0 else "W"))
    if buttons & 0x1000:
        keys.add(0x20)  # XInput A: Space on the native keyboard.
    if buttons & 0x4000:
        keys.add(ord("J"))  # XInput X: J on the native keyboard.
    return keys


def route_from_recording(reference: Path, *, keyboard: bool = True) -> tuple[list[tuple[int, set[int]]], dict]:
    files = list(reference.glob("inputs-*.jsonl"))
    if len(files) != 1:
        raise ValueError(f"Expected one completed recording in {reference}, found {len(files)}")
    rows = [json.loads(row) for row in files[0].read_text(encoding="utf-8").splitlines()]
    if (len(rows) < 6 or rows[0].get("type") != "header" or rows[0].get("version") != 1 or
            rows[0].get("boundary") != "returned_controller_state" or rows[-1].get("type") != "end"):
        raise ValueError("Recording header/end is missing or unsupported")
    polls = rows[1:-1]
    if len(polls) % 4 or rows[-1].get("samples") != len(polls):
        raise ValueError("Recording does not contain complete four-slot poll cycles")
    states = []
    previous = None
    for sequence, row in enumerate(polls):
        if (row.get("type") != "input" or row.get("seq") != sequence or
                row.get("slot") != sequence % 4 or row.get("consumer") != "game" or
                row.get("status") not in (0, 1167)):
            raise ValueError(f"Recording poll order/status differs at sequence {sequence}")
        controls = (row.get("packet"), row.get("buttons"), row.get("lt"), row.get("rt"),
                    row.get("lx"), row.get("ly"), row.get("rx"), row.get("ry"))
        bounds = ((0, 0xFFFFFFFF), (0, 0xFFFF), (0, 255), (0, 255),
                  (-32768, 32767), (-32768, 32767), (-32768, 32767), (-32768, 32767))
        if any(type(value) is not int or not low <= value <= high
               for value, (low, high) in zip(controls, bounds)):
            raise ValueError(f"Recording control is out of range at sequence {sequence}")
        if row["status"] == 1167 and any(controls):
            raise ValueError(f"Disconnected poll has nonzero state at sequence {sequence}")
        if keyboard and (row.get("mouse_x", 0) or row.get("mouse_y", 0)):
            raise ValueError("Recorded raw mouse motion requires direct input playback")
        if row["slot"] == 0:
            state = (row["buttons"], row["rt"], row["lx"], row["ly"])
            if state != previous:
                states.append(state)
                previous = state

    scene = 0
    recording_started = False
    recording_active = False
    recording_saved_scene = None
    events = []
    failure = None
    for line in (reference / "game.log").read_text(errors="replace").splitlines():
        match = SCENE_RE.search(line)
        if match:
            scene = int(match[1])
        if "[INPUT RECORDING] START" in line:
            if recording_started:
                raise ValueError("Reference log contains more than one recording start")
            recording_started = True
            recording_active = True
            start_scene = scene
        if "[INPUT RECORDING] SAVED" in line and recording_active:
            recording_active = False
            recording_saved_scene = scene
        match = KEY_RE.search(line)
        if match and recording_active and keyboard:
            state = (int(match[1], 16), int(match[2]), int(match[3]), int(match[4]))
            events.append((scene, state))
        if "[FAILURE]" in line:
            failure = {"scene": scene, "message": line}
    if not recording_started or start_scene <= 0:
        raise ValueError("Reference log lacks a scene-aligned recording start")
    if keyboard:
        if not states or states[0] != (0, 0, 0, 0):
            raise ValueError("Recording does not begin from neutral keyboard input")
        if len(events) != len(states) - 1 or [state for _, state in events] != states[1:]:
            raise ValueError("Recorded controller changes disagree with scene-aligned keyboard log")
    return ([(scene, keys_for(*state)) for scene, state in events] if keyboard else []), {
        "recording": str(files[0]), "recording_sha256": sha256(files[0]),
        "recording_start_scene": start_scene, "reference_failure": failure,
        "recording_saved_scene": recording_saved_scene,
        "recorded_polls": len(polls), "recorded_cycles": len(polls) // 4,
        "state_changes": max(0, len(states) - 1),
    }


def playback_start_scene(reference: Path, metadata: dict, override: int | None) -> int:
    if override is not None:
        if override <= 0:
            raise ValueError("Playback start scene must be positive")
        return override
    # The original benchmark replay used scene 233, one scene after its F8
    # marker. Keep that established timing while deriving new recordings from
    # their own marker in game.log.
    return 233 if reference.resolve() == REFERENCE.resolve() else metadata["recording_start_scene"]


def replay_stop_scene(metadata: dict, override: int | None) -> int:
    if override is not None:
        if override <= 0:
            raise ValueError("Stop scene must be positive")
        return override
    failure = metadata["reference_failure"]
    return max(800, failure["scene"] + 1 if failure else 800)


def prepare_run(reference: Path, executable: Path, route_meta: dict, capture: bool,
                benchmark: bool = False, detailed: bool = False) -> tuple[Path, list[str]]:
    config = json.loads((ROOT / "config/startup_replay.json").read_text(encoding="utf-8"))
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%SZ")
    folder = ROOT / "build/recorded-route-replays" / stamp
    folder.mkdir(parents=True, exist_ok=False)
    (folder / "captures").mkdir()
    shutil.copytree(ROOT / config["profile_store"], folder / "profile")
    shutil.copytree(ROOT / config["content_store"], folder / "content")
    configured_profile = ROOT / config["profile_store"]
    # Use the recorded starting preferences when present, including older runs
    # that kept only the writable sidecar. Legacy runs inherit current prefs.
    for video_preferences in (reference / "initial-state/profile.video.cfg",
                              reference / "profile.video.cfg",
                              configured_profile.with_name(configured_profile.name + ".video.cfg")):
        if video_preferences.is_file():
            shutil.copy2(video_preferences, folder / "profile.video.cfg")
            break
    # Restore the exact initial save/profile from the human crash run.
    save_target = folder / "content" / config["save_index"]
    profile_name = config["profile_id"] + ".profile"
    shutil.copy2(reference / "initial-state/SIMPSONS_SLOT1.save", save_target)
    shutil.copy2(reference / "initial-state" / profile_name, folder / "profile" / profile_name)
    expected_save = json.loads((reference / "launch.json").read_text(encoding="utf-8"))["initial_save_sha256"]
    if sha256(save_target) != expected_save:
        raise ValueError("Private initial save differs from the recorded run")
    # Windows looks beside the copied EXE for the project's audio/MinGW DLLs.
    copied_executable = folder / executable.name
    shutil.copy2(executable, copied_executable)
    for dll in executable.parent.glob("*.dll"):
        shutil.copy2(dll, folder / dll.name)
    native_assets = executable.parent / "native-assets"
    if not native_assets.is_dir():
        native_assets = ROOT / "build/native/native-assets"
    for package in ("frontend/frontend.str", "simpsons_chars/simpsons_chars_global.str"):
        if not (native_assets / package).is_file():
            raise FileNotFoundError(f"Native Video menu assets unavailable: {native_assets / package}")
    shutil.copytree(native_assets, folder / "native-assets")
    copied_recording = folder / Path(route_meta["recording"]).name
    shutil.copy2(route_meta["recording"], copied_recording)
    if sha256(copied_recording) != route_meta["recording_sha256"]:
        raise ValueError("Private input recording differs from the original")
    command = [str(copied_executable), "--image", str(ROOT / "analysis/simpsons.pe"),
               "--frame-rate", "60", "--profile-store", str(folder / "profile"),
               "--content-store", str(folder / "content"), "--local-profile", "0:" + config["profile_id"],
               "--render-test-first-mission"]
    if capture:
        command += ["--capture-frames", str(folder / "captures"), "--capture-on-request"]
    if benchmark:
        command += ["--frame-timing", str(folder / "frames.csv")]
        if not detailed:
            command.append("--frame-timing-frames-only")
    manifest = {"command": command, "cwd": str(ROOT), "source_executable": str(executable),
                "copied_executable_sha256": sha256(copied_executable),
                "initial_save_sha256": expected_save,
                "copied_recording": str(copied_recording), **route_meta}
    (folder / "launch.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return folder, command


class NativeWindow:
    def __init__(self, pid: int):
        self.pid = pid
        self.hwnd = None
        self.user = ctypes.WinDLL("user32", use_last_error=True)
        self.user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
        self.user.PostMessageW.restype = W.BOOL
        self.user.GetWindowThreadProcessId.argtypes = [W.HWND, ctypes.POINTER(W.DWORD)]
        self.user.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, ctypes.c_int]
        self.user.ShowWindow.argtypes = [W.HWND, ctypes.c_int]
        self.user.ShowWindow.restype = W.BOOL
        self.user.IsWindowVisible.argtypes = [W.HWND]
        self.user.SetForegroundWindow.argtypes = [W.HWND]
        self.user.SetForegroundWindow.restype = W.BOOL
        self.user.SetWindowPos.argtypes = [W.HWND, W.HWND, ctypes.c_int, ctypes.c_int,
                                          ctypes.c_int, ctypes.c_int, W.UINT]
        self.user.SetWindowPos.restype = W.BOOL
        self.callback = ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        self.user.EnumWindows.argtypes = [self.callback, W.LPARAM]

    def find(self):
        found = []

        @self.callback
        def visit(hwnd, _):
            value = W.DWORD()
            self.user.GetWindowThreadProcessId(hwnd, ctypes.byref(value))
            name = ctypes.create_unicode_buffer(100)
            self.user.GetClassNameW(hwnd, name, 100)
            if value.value == self.pid and name.value == "SimpsonsNativeWindow":
                found.append(hwnd)
            return True

        self.user.EnumWindows(visit, 0)
        if len(found) == 1:
            self.hwnd = found[0]
        return self.hwnd

    def hide(self):
        if self.hwnd and self.user.IsWindowVisible(self.hwnd):
            self.user.ShowWindow(self.hwnd, 0)  # SW_HIDE

    def show_and_focus(self):
        if self.hwnd:
            self.user.ShowWindow(self.hwnd, 5)  # SW_SHOW
            # Keep the benchmark visible even if the launcher itself cannot
            # become the foreground process. The window closes after the run.
            self.user.SetWindowPos(self.hwnd, W.HWND(-1), 0, 0, 0, 0,
                                   0x0001 | 0x0002 | 0x0040)  # TOPMOST, NOMOVE, NOSIZE, SHOWWINDOW
            self.user.SetForegroundWindow(self.hwnd)

    def post(self, message: int, key: int = 0):
        if not self.hwnd or not self.user.PostMessageW(self.hwnd, message, key, 0):
            raise OSError(ctypes.get_last_error(), "Could not post to the native game window")


def replay(folder: Path, command: list[str], events: list[tuple[int, set[int]]], timeout: float,
           stop_scene: int, keyboard: bool, capture: bool, capture_scenes: tuple[int, ...],
           particle_diagnostic: bool, visible: bool = False) -> dict:
    log_path = folder / "game.log"
    with log_path.open("wb", buffering=0) as output:
        process = subprocess.Popen(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
    window = NativeWindow(process.pid)
    manifest = json.loads((folder / "launch.json").read_text(encoding="utf-8"))
    manifest["pid"] = process.pid
    (folder / "launch.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    started = time.monotonic()
    scene = 0
    cursor = 0
    held = set()
    offset = 0
    partial = ""
    captures = []
    failures = []
    features = []
    playback_states = []
    playback_end = False
    particle_marker = folder / "captures/bridge-particle-diagnostic"
    last_scene_at = started
    foreground_attempted = False
    try:
        while time.monotonic() - started < timeout:
            if window.hwnd is None:
                window.find()
            if visible:
                if window.hwnd and not foreground_attempted:
                    window.show_and_focus()
                    foreground_attempted = True
            else:
                window.hide()
            with log_path.open("rb") as stream:
                stream.seek(offset)
                data = stream.read().decode("utf-8", errors="replace")
                offset = stream.tell()
            lines = (partial + data).split("\n")
            partial = lines.pop()
            for line in lines:
                match = SCENE_RE.search(line)
                if match:
                    scene = int(match[1])
                    last_scene_at = time.monotonic()
                if "[FAILURE]" in line or "[THREAD FAILURE]" in line:
                    failures.append({"scene": scene, "message": line})
                if ("[NATIVE VFX RIGID DRAW]" in line or
                    "[NATIVE ITXD L8]" in line and "name=Arcs" in line or
                    "[NATIVE PARTICLE TYPE5]" in line):
                    features.append({"scene": scene, "message": line})
                if "[INPUT PLAYBACK] state" in line:
                    playback_states.append({"scene": scene, "message": line})
                if "[INPUT PLAYBACK] END" in line:
                    playback_end = True
            if failures or process.poll() is not None:
                break
            while keyboard and window.hwnd and cursor < len(events) and scene >= events[cursor][0]:
                desired = events[cursor][1]
                window.post(0x0007)  # WM_SETFOCUS: diagnostic keyboard source.
                for key in sorted(held - desired):
                    window.post(0x0101, key)  # WM_KEYUP
                for key in sorted(desired - held):
                    window.post(0x0100, key)  # WM_KEYDOWN
                held = desired
                cursor += 1
            if capture:
                request = folder / "captures/capture.request"
                for marker in capture_scenes:
                    if scene >= marker and marker not in captures and not request.exists():
                        request.touch()
                        captures.append(marker)
                        break
            if particle_diagnostic:
                if 610 <= scene <= 715 and not particle_marker.exists():
                    particle_marker.touch()
                elif scene > 715 and particle_marker.exists():
                    particle_marker.unlink()
            if scene > 100 and time.monotonic() - last_scene_at > 5:
                failures.append({"scene": scene, "message": "Scene stopped advancing for five seconds"})
                break
            if scene >= stop_scene:
                break
            time.sleep(0.003)
    finally:
        particle_marker.unlink(missing_ok=True)
        if window.hwnd and process.poll() is None:
            if keyboard:
                for key in sorted(held):
                    window.post(0x0101, key)
            window.post(0x0010)  # WM_CLOSE
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=10)
    reached_bridge = (any("[NATIVE VFX RIGID DRAW]" in feature["message"] for feature in features)
                      and any("[NATIVE ITXD L8]" in feature["message"] for feature in features))
    reached_type5 = any("[NATIVE PARTICLE TYPE5]" in feature["message"] for feature in features)
    return {"pid": process.pid, "scene": scene,
            "delivered_changes": cursor if keyboard else max(0, len(playback_states) - 1),
            "playback_states": playback_states, "playback_end": playback_end,
            "requested_capture_scenes": captures, "features": features, "failures": failures,
            "reached_bridge": reached_bridge, "reached_type5": reached_type5,
            "exit_code": process.returncode, "elapsed_seconds": time.monotonic() - started}


def benchmark_report(folder: Path, scene: int) -> dict:
    first, last = BENCHMARK_PRESENTATIONS
    report = {"first_presentation": first, "last_presentation": last,
              "count": last - first + 1, "scene_reached": scene, "valid": False}
    if scene < 500:
        report["reason"] = "The replay did not reach the end of the fixed gameplay scene window"
        return report
    try:
        with (folder / "frames.csv").open(newline="", encoding="utf-8") as stream:
            rows = [row for row in csv.DictReader(stream)
                    if row.get("presentation", "").isdigit() and
                    first <= int(row["presentation"]) <= last]
    except (OSError, csv.Error) as error:
        report["reason"] = f"Frame timing CSV is unavailable: {error}"
        return report
    if [int(row["presentation"]) for row in rows] != list(range(first, last + 1)):
        report["reason"] = "Gameplay presentation window is incomplete or duplicated"
        return report
    if any(row.get("display_accepted") != "1" for row in rows):
        report["reason"] = "Gameplay presentation window contains occluded frames"
        return report
    try:
        durations = [float(row["frame_ms"]) for row in rows]
    except (KeyError, TypeError, ValueError) as error:
        report["reason"] = f"Gameplay frame duration is missing or invalid: {error}"
        return report
    if any(not math.isfinite(value) or value <= 0 for value in durations):
        report["reason"] = "Gameplay frame duration is nonpositive or nonfinite"
        return report
    report.update(valid=True, fps=1000 / statistics.fmean(durations),
                  mean_ms=statistics.fmean(durations), median_ms=statistics.median(durations),
                  max_ms=max(durations), over_25_ms=sum(value > 25 for value in durations))
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, default=REFERENCE)
    parser.add_argument("--executable", type=Path, default=ROOT / "build/native/SimpsonsInputRecorder.exe")
    parser.add_argument("--timeout", type=float, default=90)
    parser.add_argument("--stop-scene", type=int,
                        help="Close after this scene; default is at least one scene beyond the reference failure")
    parser.add_argument("--inspect", action="store_true", help="Validate and print the route without launching")
    parser.add_argument("--keyboard", action="store_true", help="Use the approximate scene-scheduled keyboard route")
    parser.add_argument("--no-capture", action="store_true", help="Replay without GPU readback stalls")
    parser.add_argument("--benchmark", action="store_true",
                        help="Show the game and measure accepted first-mission frames 370 through 530")
    parser.add_argument("--benchmark-detailed", action="store_true",
                        help="Include native timing buckets in the first-mission benchmark")
    parser.add_argument("--vsync", action="store_true",
                        help="Use the previous display-synchronized presentation mode")
    parser.add_argument("--sparse-capture", action="store_true", help="Request only ten route/bridge frames")
    parser.add_argument("--particle-diagnostic", action="store_true",
                        help="Enable the bridge particle marker during scenes 610–715")
    parser.add_argument("--require-type5", action="store_true",
                        help="Fail unless the qualified type-5 particle path is observed")
    parser.add_argument("--playback-start-scene", type=int,
                        help="First scene with recorded slot-zero polls; default derives from the reference log")
    args = parser.parse_args()
    if args.benchmark and args.keyboard:
        parser.error("Benchmark requires the exact native input playback route")
    if args.benchmark_detailed and not args.benchmark:
        parser.error("--benchmark-detailed requires --benchmark")
    events, metadata = route_from_recording(args.reference, keyboard=args.keyboard)
    start_scene = playback_start_scene(args.reference, metadata, args.playback_start_scene)
    stop_scene = replay_stop_scene(metadata, args.stop_scene)
    if args.inspect:
        print(json.dumps({**metadata, "playback_start_scene": start_scene,
                          "planned_stop_scene": stop_scene,
                          "benchmark_presentations": BENCHMARK_PRESENTATIONS if args.benchmark else None,
                          "events": [(scene, sorted(keys)) for scene, keys in events]}, indent=2))
        return 0
    capture = not args.no_capture and not args.benchmark
    folder, command = prepare_run(args.reference, args.executable, metadata, capture,
                                  args.benchmark, args.benchmark_detailed)
    if args.vsync:
        command.append("--vsync")
        manifest = json.loads((folder / "launch.json").read_text(encoding="utf-8"))
        manifest["command"] = command
        (folder / "launch.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if not args.keyboard:
        command += ["--input-playback", str(folder / Path(metadata["recording"]).name),
                    "--input-playback-start-scene", str(start_scene)]
        manifest = json.loads((folder / "launch.json").read_text(encoding="utf-8"))
        manifest["command"] = command
        (folder / "launch.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"REPLAY {folder}", flush=True)
    capture_scenes = SPARSE_CAPTURE_SCENES if args.sparse_capture else CAPTURE_SCENES
    result = replay(folder, command, events, args.timeout, stop_scene, args.keyboard,
                    capture, capture_scenes, args.particle_diagnostic, args.benchmark)
    if args.benchmark:
        result["benchmark"] = benchmark_report(folder, result["scene"])
    (folder / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    if list((folder / "captures").glob("*.rgb10a2")):
        subprocess.run([sys.executable, "-B", str(ROOT / "tools/render_frame_capture.py"),
                        str(folder / "captures")], check=True, timeout=180)
    print(json.dumps({"folder": str(folder), **result}, indent=2), flush=True)
    return 0 if ((not args.benchmark or result["benchmark"]["valid"]) and
                 not result["failures"] and
                 (result["delivered_changes"] == len(events) if args.keyboard else result["playback_end"])
                 and result["reached_bridge"] and (not args.require_type5 or result["reached_type5"])) else 1


if __name__ == "__main__":
    raise SystemExit(main())
