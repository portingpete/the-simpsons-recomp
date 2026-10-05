"""Check Enter and WASD through the owned native window with private stores.

Only the disabled background lab HWND receives Win32 messages. No desktop key,
mouse, cursor, foreground, or physical-controller input is used. Screenshots
are completed renderer readbacks, and the run closes through WM_CLOSE.
"""
from pathlib import Path
import ctypes
from ctypes import wintypes
import datetime
import hashlib
import json
import re
import shutil
import time

from test_graphics_settings_live import ROOT, Run, USER, window_for

USER.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
USER.SendMessageW.restype = ctypes.c_ssize_t
USER.GetForegroundWindow.restype = wintypes.HWND
KEYBOARD = re.compile(r"\[NATIVE INPUT\] game-window keyboard buttons=([0-9A-F]+).*left=\((-?\d+),(-?\d+)\)")
ACTION = re.compile(r"\[NATIVE VIDEO MENU\] action=(\d+) row=(\d+) direction=(-?\d+) render_index=(\d+)->(\d+)")
INITIAL = "5 0 0 0 120 0 3 0 0 100 1 1 1\n"


def fingerprints(config):
    result = {}
    for setting in ("profile_store", "content_store"):
        folder = ROOT / config[setting]
        for path in sorted(folder.rglob("*")):
            if path.is_file():
                with path.open("rb") as stream:
                    result[str(path.relative_to(ROOT))] = hashlib.file_digest(stream, "sha256").hexdigest()
    profile = ROOT / config["profile_store"]
    sidecar = profile.with_name(profile.name + ".video.cfg")
    result[str(sidecar.relative_to(ROOT))] = (
        hashlib.sha256(sidecar.read_bytes()).hexdigest() if sidecar.exists() else None)
    return result


class KeyboardRun(Run):
    def __init__(self, folder, config):
        super().__init__(folder, config)
        self.receipts = []

    def wait(self, seconds):
        """Keep short diagnostic holds precise without measuring game speed."""
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            if self.process.poll() is not None:
                raise RuntimeError("Owned game exited early: " + str(self.process.returncode))
            if time.monotonic() > self.deadline:
                raise TimeoutError("Bounded keyboard inspection deadline")
            if hwnd := window_for(self.process.pid):
                USER.EnableWindow(hwnd, False)
            time.sleep(min(.005, max(0, until-time.monotonic())))

    def message(self, message, key=0):
        hwnd = window_for(self.process.pid)
        if not hwnd:
            raise RuntimeError("Owned native window is absent")
        USER.EnableWindow(hwnd, False)
        USER.SendMessageW(hwnd, message, key, 0)

    def flush(self):
        # A neutral command has no button or stick override. Its existing input
        # acknowledgement flushes C stdio, making keyboard/menu receipts visible.
        self.send("PAD 0000 0 0 50")

    def hold(self, name, key, seconds, settle=.5):
        self.flush()
        before = self.log_path.read_text(errors="replace")
        foreground = int(USER.GetForegroundWindow() or 0)
        self.message(0x0007)  # Internal WM_SETFOCUS receipt; no OS focus change.
        self.message(0x0100, key)
        started = time.monotonic()
        try:
            self.wait(seconds)
        finally:
            self.message(0x0101, key)
        elapsed = time.monotonic() - started
        self.wait(settle)
        self.flush()
        after = self.log_path.read_text(errors="replace")
        if not after.startswith(before):
            raise RuntimeError("Owned game log changed unexpectedly")
        segment = after[len(before):]
        receipt = dict(name=name, key=key, requested_hold_ms=round(seconds*1000),
            actual_hold_ms=round(elapsed*1000, 2),
            target_pid=self.process.pid, target_hwnd=int(window_for(self.process.pid)),
            foreground_before=foreground, foreground_after=int(USER.GetForegroundWindow() or 0),
            keyboard=[dict(buttons=int(m[1],16), left=[int(m[2]),int(m[3])]) for m in KEYBOARD.finditer(segment)],
            video_actions=[dict(action=int(m[1]), row=int(m[2]), direction=int(m[3]),
                before=int(m[4]), after=int(m[5])) for m in ACTION.finditer(segment)],
            log=segment)
        self.receipts.append(receipt)
        (self.folder / "keyboard-receipts.json").write_text(json.dumps(self.receipts, indent=2)+"\n")
        print("KEY", name, "hold_ms="+str(receipt["actual_hold_ms"]),
            "video_actions="+str(len(receipt["video_actions"])), flush=True)
        return receipt


def require_action(receipt, row, direction, minimum=1, maximum=1):
    actions = receipt["video_actions"]
    assert minimum <= len(actions) <= maximum, receipt
    assert all(action["row"] == row and action["direction"] == direction for action in actions), receipt


def main():
    config = json.loads((ROOT / "config/startup_replay.json").read_text())
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%SZ-%f")
    folder = ROOT / "build/keyboard-menu-fix-20261004" / ("live-"+stamp)
    folder.mkdir(parents=True)
    before = fingerprints(config)
    (folder / "primary-before.json").write_text(json.dumps(before, indent=2)+"\n")
    shutil.copytree(ROOT / config["profile_store"], folder / "profile")
    shutil.copytree(ROOT / config["content_store"], folder / "content")
    prefs = folder / "profile.video.cfg"
    prefs.write_text(INITIAL)
    lab = folder / "lab"
    lab.mkdir()
    run = KeyboardRun(lab, config)
    print("RUN", folder, flush=True)
    passed = False
    failure = None
    try:
        run.start()
        run.capture("scene-before-enter", [1280,720], True)
        enter = run.hold("enter-in-game", 0x0D, .1, 1)
        assert any(state["buttons"] == 0x1000 for state in enter["keyboard"]), enter
        assert not any(state["buttons"] & 0x0010 for state in enter["keyboard"]), enter
        run.capture("scene-after-enter", [1280,720], True)

        escape = run.hold("escape-pause", 0x1B, .1, 2.5)
        assert any(state["buttons"] == 0x0010 for state in escape["keyboard"]), escape
        run.capture("pause-after-escape", [1280,720])
        run.hold("s-one-pause-row", ord("S"), .1)
        run.capture("pause-options-selected", [1280,720])
        run.hold("enter-options", 0x0D, .1, 1)
        run.capture("options-after-enter", [1280,720])
        run.hold("enter-video", 0x0D, .1, 2)
        assert "[NATIVE VIDEO MENU] opened" in run.log_path.read_text(errors="replace"), "Keyboard did not enter Video from Options"
        run.capture("video-brightness-selected", [1280,720])

        run.hold("s-one-video-row", ord("S"), .1)
        right = run.hold("d-200ms-on-render-row", ord("D"), .2)
        require_action(right, 1, 1)
        assert right["video_actions"][0]["before"] == 0 and right["video_actions"][0]["after"] == 1, right
        left = run.hold("a-200ms-on-render-row", ord("A"), .2)
        require_action(left, 1, -1)
        assert left["video_actions"][0]["after"] == 0, left
        run.capture("video-short-ad-restored", [1280,720])

        run.hold("w-one-video-row", ord("W"), .1)
        brightness = run.hold("d-after-w-is-brightness", ord("D"), .2)
        assert not brightness["video_actions"], brightness
        run.hold("s-back-to-render-row", ord("S"), .1)
        boundary = run.hold("d-400ms-repeat-boundary", ord("D"), .4)
        require_action(boundary, 1, 1, 1, 2)
        repeat = run.hold("d-650ms-bounded-repeat", ord("D"), .65)
        # The original digital dispatcher waits 400ms before 80ms repeats.
        # Allow one-frame scheduling variation; per-frame analog polling would
        # produce dozens of actions in this interval and fails this bound.
        require_action(repeat, 1, 1, 3, 6)
        run.capture("video-held-d-repeat", [1280,720])

        run.hold("k-cancel-video", ord("K"), .1, 1)
        assert prefs.read_text() == INITIAL, "Cancelled Video preview changed private preferences"
        assert "[NATIVE VIDEO MENU] cancelled and restored" in run.log_path.read_text(errors="replace")
        run.hold("k-back-to-pause", ord("K"), .1, 1)
        run.hold("k-resume", ord("K"), .1, 2)
        run.capture("scene-after-cancel-resume", [1280,720], True)
        passed = True
    except Exception as error:
        failure = repr(error)
        raise
    finally:
        try:
            run.close()
        finally:
            after = fingerprints(config)
            (folder / "primary-after.json").write_text(json.dumps(after, indent=2)+"\n")
            unchanged = before == after
            shutdown_path = lab / "shutdown.json"
            shutdown = json.loads(shutdown_path.read_text()) if shutdown_path.exists() else {}
            (folder / "verification.json").write_text(json.dumps(dict(passed=passed and unchanged and shutdown.get("normal",False),
                failure=failure, primary_stores_unchanged=unchanged, primary_stores_used=False,
                desktop_input_injected=False, foreground_activation_api_called=False, cursor_api_called=False,
                normal_shutdown=shutdown.get("normal",False),
                input_route="owned-disabled-HWND SendMessageW",
                timing_purpose="menu correctness and bounded action counts"), indent=2)+"\n")
            assert unchanged, "Primary save/profile/video files changed during private keyboard run"
    print("PASS", folder, flush=True)


if __name__ == "__main__":
    main()
