"""Checkpoint packages must replay from an intact initial save and full input prefix."""
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location(
    "gameplay_checkpoint", Path(__file__).resolve().parents[1] / "tools/gameplay_checkpoint.py")
checkpoint = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checkpoint)


class CheckpointTests(unittest.TestCase):
    def fixture(self, root: Path) -> Path:
        config = {"profile_id": "player", "profile_store": "profiles", "content_store": "content",
                  "save_index": "save-index/slot.save"}
        (root / "config").mkdir()
        (root / "config/startup_replay.json").write_text(json.dumps(config), encoding="utf-8")
        files = {"build/native/SimpsonsInputRecorder.exe": b"new runtime",
                 "analysis/simpsons.pe": b"original image",
                 "profiles/player.profile": b"current profile",
                 "content/save-index/slot.save": b"current save",
                 "content/static.bin": b"unchanged content"}
        for relative, data in files.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        run = root / "build/input-recordings/20260923-000000Z-fixture"
        (run / "initial-state").mkdir(parents=True)
        (run / "initial-state/SIMPSONS_SLOT1.save").write_bytes(b"recorded initial save")
        (run / "initial-state/player.profile").write_bytes(b"recorded initial profile")
        rows = [{"type": "header", "version": 1, "boundary": "returned_controller_state",
                 "start_scene": 0, "save_state_captured": False}]
        for seq in range(8):
            rows.append({"type": "input", "seq": seq, "consumer": "game", "slot": seq % 4,
                         "status": 0 if seq % 4 == 0 else 1167,
                         "packet": 1 if seq % 4 == 0 else 0,
                         "buttons": 0, "lt": 0, "rt": 0, "lx": 0, "ly": 0, "rx": 0, "ry": 0})
        rows.append({"type": "end", "samples": 8, "reason": "checkpoint", "end_scene": 200})
        (run / "inputs-fixture.jsonl").write_text(
            "".join(json.dumps(row, separators=(",", ":")) + "\n" for row in rows), encoding="utf-8")
        checkpoint.write_json(run / "launch.json", {
            "first_mission": True, "recording_auto_start": True, "profile_id": "player",
            "initial_save_sha256": checkpoint.sha256(run / "initial-state/SIMPSONS_SLOT1.save"),
            "image_sha256": checkpoint.sha256(root / "analysis/simpsons.pe"),
            "executable_sha256": "previous-runtime-hash",
        })
        return run

    def test_package_and_private_resume(self):
        with tempfile.TemporaryDirectory(prefix="simpsons checkpoint ") as directory:
            root = Path(directory)
            source = self.fixture(root)
            package = checkpoint.create_checkpoint(root, "after-river", source)
            self.assertEqual(checkpoint.read_json(package / "checkpoint.json")["end_scene"], 200)
            self.assertEqual((package / "initial-state/SIMPSONS_SLOT1.save").read_bytes(), b"recorded initial save")
            self.assertEqual(checkpoint.recording_info(package / "inputs.jsonl")["polls"], 8)
            run, command, manifest = checkpoint.prepare_resume(root, package)
            self.assertEqual((run / "content/save-index/slot.save").read_bytes(), b"recorded initial save")
            self.assertEqual((run / "profile/player.profile").read_bytes(), b"recorded initial profile")
            self.assertEqual((root / "content/save-index/slot.save").read_bytes(), b"current save")
            self.assertIn("--input-playback-from-first-poll", command)
            self.assertIn("--input-playback-continue-live", command)
            self.assertEqual(command[command.index("--input-playback-expected-end-scene") + 1], "200")
            self.assertIn("--input-recording-auto-start", command)
            self.assertTrue(manifest["executable_differs_from_recording"])
            self.assertEqual(manifest["checkpoint_recording_sha256"],
                             checkpoint.sha256(run / "checkpoint-inputs.jsonl"))
            with self.assertRaises(FileExistsError):
                checkpoint.create_checkpoint(root, "after-river", source)
            (package / "inputs.jsonl").write_text("changed", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "changed"):
                checkpoint.prepare_resume(root, package)

    def test_reject_partial_or_non_checkpoint_recording(self):
        with tempfile.TemporaryDirectory(prefix="simpsons checkpoint ") as directory:
            root = Path(directory)
            source = self.fixture(root)
            recording = source / "inputs-fixture.jsonl"
            rows = recording.read_text(encoding="utf-8").splitlines()
            rows[-1] = rows[-1].replace('"checkpoint"', '"user"')
            recording.write_text("\n".join(rows) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "No complete F9"):
                checkpoint.create_checkpoint(root, "bad", source)

    def test_latest_checkpoint_skips_newer_unfinished_recordings(self):
        with tempfile.TemporaryDirectory(prefix="simpsons checkpoint ") as directory:
            root = Path(directory)
            source = self.fixture(root)
            for suffix, reason in (("000001Z-user", "user"), ("000002Z-partial", None)):
                newer = source.parent / ("20260923-" + suffix)
                shutil.copytree(source, newer)
                recording = newer / "inputs-fixture.jsonl"
                rows = recording.read_text(encoding="utf-8").splitlines()
                if reason is None:
                    rows.pop()
                else:
                    end = json.loads(rows[-1])
                    end["reason"] = reason
                    rows[-1] = json.dumps(end)
                recording.write_text("\n".join(rows) + "\n", encoding="utf-8")

            package = checkpoint.latest_checkpoint(root)
            manifest = checkpoint.read_json(package / "checkpoint.json")
            self.assertEqual(manifest["source_run"], str(source))
            self.assertEqual(manifest["source_recording"], "inputs-fixture.jsonl")
            self.assertEqual(checkpoint.recording_info(package / "inputs.jsonl")["end_scene"], 200)

    def test_latest_checkpoint_reuses_package_but_rejects_changed_source(self):
        with tempfile.TemporaryDirectory(prefix="simpsons checkpoint ") as directory:
            root = Path(directory)
            source = self.fixture(root)
            package = checkpoint.latest_checkpoint(root)
            manifest = checkpoint.read_json(package / "checkpoint.json")

            self.assertEqual(checkpoint.latest_checkpoint(root), package)
            self.assertEqual(checkpoint.read_json(package / "checkpoint.json"), manifest)
            self.assertEqual(len(checkpoint.available(root)), 1)

            recording = source / "inputs-fixture.jsonl"
            rows = recording.read_text(encoding="utf-8").splitlines()
            first_poll = json.loads(rows[1])
            first_poll["buttons"] = 1
            rows[1] = json.dumps(first_poll)
            recording.write_text("\n".join(rows) + "\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                checkpoint.latest_checkpoint(root)
            self.assertEqual(checkpoint.read_json(package / "checkpoint.json"), manifest)

    def test_launcher_reports_game_failure_and_preserves_log_path(self):
        with tempfile.TemporaryDirectory(prefix="simpsons checkpoint ") as directory:
            root = Path(directory)
            source = self.fixture(root)
            package = checkpoint.create_checkpoint(root, "failed-replay", source)
            process = mock.Mock(pid=1234)
            process.wait.return_value = 1
            with mock.patch.object(checkpoint.subprocess, "Popen", return_value=process):
                with self.assertRaisesRegex(ValueError, r"Replay game exited with code 1; see .*game\.log"):
                    checkpoint.launch_resume(root, package)
            runs = list((root / "build/input-recordings").glob("*-resume-*"))
            self.assertEqual(len(runs), 1)
            self.assertEqual(checkpoint.read_json(runs[0] / "result.json")["exit_code"], 1)
            self.assertFalse(checkpoint.read_json(runs[0] / "result.json")["normal_window_close"])

    def test_launcher_accepts_window_close_after_replay(self):
        with tempfile.TemporaryDirectory(prefix="simpsons checkpoint ") as directory:
            root = Path(directory)
            source = self.fixture(root)
            package = checkpoint.create_checkpoint(root, "closed-replay", source)
            process = mock.Mock(pid=1234)
            process.wait.return_value = 1

            def close_window(*_args, **kwargs):
                kwargs["stdout"].write(b"[FAILURE] Native window closed\n")
                return process

            with mock.patch.object(checkpoint.subprocess, "Popen", side_effect=close_window):
                run = checkpoint.launch_resume(root, package)
            result = checkpoint.read_json(run / "result.json")
            self.assertEqual(result["exit_code"], 1)
            self.assertTrue(result["normal_window_close"])


if __name__ == "__main__":
    unittest.main()
