"""A crash replay preserves the failed run and replays it from private stores."""
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location(
    "saved_crash_replay", Path(__file__).resolve().parents[1] / "tools/saved_crash_replay.py")
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)

FAILURE = "[FAILURE] Native texture callback has an invalid owner or ABI"


class SavedCrashReplayTests(unittest.TestCase):
    def fixture(self, root: Path) -> Path:
        config = {"profile_id": "player", "profile_store": "profiles",
                  "content_store": "content", "save_index": "save-index/slot.save"}
        (root / "config").mkdir()
        replay.write_json(root / "config/startup_replay.json", config)
        files = {"build/native/SimpsonsInputRecorder.exe": b"recording runtime",
                 "build/native/audio.dll": b"runtime dependency",
                 "build/native/native-assets/frontend/frontend.str": b"frontend native menu",
                 "build/native/native-assets/simpsons_chars/simpsons_chars_global.str": b"in-game native menu",
                 "profiles.video.cfg": b"4 6 0 0 0 8 3 3\n",
                 "analysis/simpsons.pe": b"game image"}
        for relative, data in files.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (root / "Simpsons Game, The (USA)").mkdir()
        source = root / "build/input-recordings/20260923-224246Z-fixture"
        for relative, data in {
                "initial-state/SIMPSONS_SLOT1.save": b"initial save",
                "initial-state/player.profile": b"initial profile",
                "initial-state/profile.video.cfg": b"4 4 0 1 120 6 3 2\n",
                "profile.video.cfg": b"4 3 1 0 120 5 3 1\n",
                "content/save-index/slot.save": b"later writable save",
                "content/static.bin": b"game content",
                "profile/player.profile": b"later writable profile",
                "game.log": (FAILURE + "\n").encode()}.items():
            path = source / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        rows = [{"type": "header", "version": 1, "boundary": "returned_controller_state",
                 "start_scene": 0, "save_state_captured": False}]
        for seq in range(8):
            slot = seq % 4
            rows.append({"type": "input", "seq": seq, "consumer": "game", "slot": slot,
                         "status": 0 if slot == 0 else 1167,
                         "packet": 2 if seq == 4 else (1 if slot == 0 else 0),
                         "buttons": 0x4000 if seq == 4 else 0,
                         "lt": 0, "rt": 0, "lx": 0, "ly": 0, "rx": 0, "ry": 0})
        rows.append({"type": "end", "samples": 8, "reason": "shutdown", "end_scene": 5148})
        (source / "inputs-fixture.jsonl").write_text(
            "".join(json.dumps(row, separators=(",", ":")) + "\n" for row in rows),
            encoding="utf-8")
        replay.write_json(source / "launch.json", {
            "first_mission": True, "recording_auto_start": True, "profile_id": "player",
            "initial_save_sha256": replay.sha256(source / "initial-state/SIMPSONS_SLOT1.save"),
            "executable_sha256": replay.sha256(root / "build/native/SimpsonsInputRecorder.exe"),
            "image_sha256": replay.sha256(root / "analysis/simpsons.pe")})
        replay.write_json(source / "result.json", {
            "exit_code": 1, "recordings": ["inputs-fixture.jsonl"]})
        return source

    def test_archive_verifies_hashes_and_does_not_overwrite(self):
        with tempfile.TemporaryDirectory(prefix="saved crash replay ") as directory:
            root = Path(directory)
            source = self.fixture(root)
            bundle = replay.archive(root, "ball-homer-j", source)
            manifest = replay.verify(bundle)
            self.assertEqual(manifest["recording_info"], {
                "polls": 8, "start_scene": 0, "end_scene": 5148, "final_x_sequence": 4})
            self.assertEqual(manifest["source_failure"], FAILURE)
            self.assertEqual((bundle / "run/inputs-fixture.jsonl").read_bytes(),
                             (source / "inputs-fixture.jsonl").read_bytes())
            self.assertEqual((bundle / "binary/audio.dll").read_bytes(), b"runtime dependency")
            self.assertEqual((bundle / "binary/native-assets/frontend/frontend.str").read_bytes(),b"frontend native menu")
            self.assertEqual((bundle / "binary/native-assets/simpsons_chars/simpsons_chars_global.str").read_bytes(),b"in-game native menu")
            for relative, entry in manifest["files"].items():
                path = bundle / relative
                self.assertEqual(path.stat().st_size, entry["bytes"])
                self.assertEqual(replay.sha256(path), entry["sha256"])
            with self.assertRaises(FileExistsError):
                replay.archive(root, "ball-homer-j", source)

    def test_rejects_incomplete_or_unordered_polls_and_missing_final_j(self):
        with tempfile.TemporaryDirectory(prefix="saved crash replay ") as directory:
            root = Path(directory)
            source = self.fixture(root)
            recording = source / "inputs-fixture.jsonl"
            original = recording.read_text(encoding="utf-8").splitlines()
            for label, change in (
                    ("missing end", lambda rows: rows.pop()),
                    ("wrong sequence", lambda rows: rows[2].update(seq=42)),
                    ("wrong slot", lambda rows: rows[2].update(slot=3)),
                    ("missing J", lambda rows: rows[5].update(buttons=0)),
                    ("disconnected input", lambda rows: rows[6].update(buttons=1))):
                with self.subTest(label=label):
                    rows = [json.loads(line) for line in original]
                    change(rows)
                    recording.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")
                    with self.assertRaises(ValueError):
                        replay.archive(root, "invalid", source)
                    self.assertFalse((root / "saved-replays/invalid").exists())

    def test_archive_survives_source_changes_and_detects_package_tampering(self):
        with tempfile.TemporaryDirectory(prefix="saved crash replay ") as directory:
            root = Path(directory)
            source = self.fixture(root)
            bundle = replay.archive(root, "ball-homer-j", source)
            shutil.rmtree(source)
            self.assertEqual(replay.verify(bundle)["recording_info"]["final_x_sequence"], 4)
            for relative in ("run/inputs-fixture.jsonl", "run/initial-state/SIMPSONS_SLOT1.save",
                             "run/initial-state/player.profile", "run/game.log",
                             "run/initial-state/profile.video.cfg", "binary/native-assets/frontend/frontend.str"):
                with self.subTest(relative=relative):
                    path = bundle / relative
                    original = path.read_bytes()
                    path.write_bytes(original + b"changed")
                    with self.assertRaisesRegex(ValueError, "changed"):
                        replay.verify(bundle)
                    path.write_bytes(original)
            replay.verify(bundle)

    def test_play_defaults_to_current_binary_with_archived_input_and_private_stores(self):
        with tempfile.TemporaryDirectory(prefix="saved crash replay ") as directory:
            root = Path(directory)
            source = self.fixture(root)
            bundle = replay.archive(root, "ball-homer-j", source)
            archive_hashes = {name: replay.sha256(path) for name, path in replay.regular_files(bundle).items()}
            current = root / "build/native/SimpsonsInputRecorder.exe"
            current.write_bytes(b"updated runtime with native input prompts")
            shutil.rmtree(source)
            process = mock.Mock(pid=1234, returncode=0)
            process.wait.return_value = 0
            with mock.patch.object(replay.subprocess, "Popen", return_value=process) as popen:
                run = replay.play(root, "ball-homer-j", timeout=5)
            command = popen.call_args.args[0]
            self.assertEqual(command[0], str(current))
            self.assertEqual(command[command.index("--image") + 1],
                             str(root / "analysis/simpsons.pe"))
            self.assertEqual(command[command.index("--input-playback") + 1],
                             str(run / "playback-inputs.jsonl"))
            self.assertIn("--input-playback-from-first-poll", command)
            self.assertNotIn("--input-playback-continue-live", command)
            self.assertNotIn("--input-playback-expected-end-scene", command)
            self.assertNotIn("--input-recording-directory", command)
            self.assertNotIn("--input-recording-auto-start", command)
            self.assertEqual(replay.read_json(run / "launch.json")["selected_executable_sha256"],
                             replay.sha256(current))
            self.assertEqual(replay.read_json(run / "launch.json")["executable_source"], "current")
            self.assertEqual((run / "content/save-index/slot.save").read_bytes(), b"initial save")
            self.assertEqual((run / "profile/player.profile").read_bytes(), b"initial profile")
            self.assertEqual((run / "profile.video.cfg").read_bytes(), b"4 4 0 1 120 6 3 2\n")
            self.assertEqual(command[command.index('--frame-rate')+1],'60')
            (run / "profile.video.cfg").write_bytes(b"changed private preferences")
            self.assertEqual((run / "playback-inputs.jsonl").read_bytes(),
                             (bundle / "run/inputs-fixture.jsonl").read_bytes())
            self.assertEqual((bundle / "run/content/save-index/slot.save").read_bytes(),
                             b"later writable save")
            self.assertFalse(replay.read_json(run / "result.json")["reproduced_source_failure"])
            self.assertEqual({name: replay.sha256(path) for name, path in replay.regular_files(bundle).items()},
                             archive_hashes)
            replay.verify(bundle)

    def test_explicit_archived_executable_requires_the_original_failure(self):
        with tempfile.TemporaryDirectory(prefix="saved crash replay ") as directory:
            root = Path(directory)
            source = self.fixture(root)
            bundle = replay.archive(root, "ball-homer-j", source)
            archive_hashes = {name: replay.sha256(path) for name, path in replay.regular_files(bundle).items()}
            (root / "build/native/SimpsonsInputRecorder.exe").write_bytes(b"updated runtime")
            process = mock.Mock(pid=1234, returncode=1)
            process.wait.return_value = 1

            def crashed(command, **kwargs):
                kwargs["stdout"].write((FAILURE + "\n").encode())
                return process

            with mock.patch.object(replay.subprocess, "Popen", side_effect=crashed) as popen:
                run = replay.play(root, "ball-homer-j", timeout=5, archived_executable=True)
            self.assertEqual(popen.call_args.args[0][0], str(bundle / "binary/SimpsonsInputRecorder.exe"))
            self.assertEqual(replay.read_json(run / "launch.json")["executable_source"], "archived")
            self.assertTrue(replay.read_json(run / "result.json")["reproduced_source_failure"])
            with mock.patch.object(replay.subprocess, "Popen", return_value=process):
                with self.assertRaisesRegex(ValueError, "did not reproduce"):
                    replay.play(root, "ball-homer-j", timeout=5, archived_executable=True)
            self.assertEqual({name: replay.sha256(path) for name, path in replay.regular_files(bundle).items()},
                             archive_hashes)

    def test_old_archive_uses_private_binary_with_current_menu_assets(self):
        with tempfile.TemporaryDirectory(prefix="saved crash replay ") as directory:
            root=Path(directory);source=self.fixture(root)
            bundle=replay.archive(root,"old-archive",source)
            # Model a valid archive created before native menu assets were copied.
            shutil.rmtree(bundle/"binary/native-assets")
            manifest=replay.read_json(bundle/"manifest.json")
            manifest['files']={name:entry for name,entry in manifest['files'].items() if not name.startswith('binary/native-assets/')}
            replay.write_json(bundle/"manifest.json",manifest)
            replay.verify(bundle)
            archive_hashes={name:replay.sha256(path) for name,path in replay.regular_files(bundle).items()}
            process=mock.Mock(pid=1234,returncode=1);process.wait.return_value=1
            def crashed(command,**kwargs):
                private_binary=Path(command[0]).parent
                self.assertNotEqual(private_binary,bundle/'binary')
                self.assertEqual(Path(command[0]).read_bytes(),b'recording runtime')
                self.assertEqual((private_binary/'audio.dll').read_bytes(),b'runtime dependency')
                self.assertEqual((private_binary/'native-assets/frontend/frontend.str').read_bytes(),b'frontend native menu')
                self.assertEqual((private_binary/'native-assets/simpsons_chars/simpsons_chars_global.str').read_bytes(),b'in-game native menu')
                kwargs['stdout'].write((FAILURE+'\n').encode())
                return process
            with mock.patch.object(replay.subprocess,'Popen',side_effect=crashed) as popen:
                run=replay.play(root,'old-archive',timeout=5,archived_executable=True)
            self.assertEqual(popen.call_args.args[0][0],str(run/'binary/SimpsonsInputRecorder.exe'))
            self.assertEqual(replay.read_json(run/'launch.json')['executable_source'],'archived')
            self.assertTrue(replay.read_json(run/'result.json')['reproduced_source_failure'])
            self.assertEqual({name:replay.sha256(path) for name,path in replay.regular_files(bundle).items()},archive_hashes)
            replay.verify(bundle)
            # If both the old archive and current install lack packages, fail
            # before process creation rather than launching a broken window.
            shutil.rmtree(root/'build/native/native-assets')
            with mock.patch.object(replay.subprocess,'Popen') as popen:
                with self.assertRaisesRegex(FileNotFoundError,'Native Video menu assets unavailable'):
                    replay.play(root,'old-archive',timeout=5,archived_executable=True)
            popen.assert_not_called()

    def test_missing_current_executable_does_not_fall_back_to_archive(self):
        with tempfile.TemporaryDirectory(prefix="saved crash replay ") as directory:
            root = Path(directory)
            source = self.fixture(root)
            bundle = replay.archive(root, "ball-homer-j", source)
            (root / "build/native/SimpsonsInputRecorder.exe").unlink()
            with mock.patch.object(replay.subprocess, "Popen") as popen:
                with self.assertRaises(FileNotFoundError):
                    replay.play(root, "ball-homer-j", timeout=5)
            popen.assert_not_called()
            self.assertFalse((root / "build/crash-replay-runs").exists())
            replay.verify(bundle)

    def test_executable_selection_options_are_mutually_exclusive(self):
        with self.assertRaisesRegex(ValueError, "cannot be selected together"):
            replay.play(Path("unused"), "ball-homer-j", executable=Path("candidate.exe"),
                        archived_executable=True)
        with mock.patch("sys.argv", ["saved_crash_replay.py", "play", "ball-homer-j",
                                     "--executable", "candidate.exe", "--archived-executable"]):
            with mock.patch("sys.stderr"), mock.patch.object(replay, "play") as play:
                with self.assertRaises(SystemExit) as raised:
                    replay.main()
        self.assertEqual(raised.exception.code, 2)
        play.assert_not_called()

    def test_cli_passes_explicit_archive_selection(self):
        with mock.patch("sys.argv", ["saved_crash_replay.py", "play", "ball-homer-j", "--archived-executable"]):
            with mock.patch("sys.stdout"), mock.patch.object(replay, "play", return_value=Path("run")) as play:
                replay.main()
        play.assert_called_once_with(replay.ROOT, "ball-homer-j", 600, None, 0, 0, archived_executable=True)

    def test_play_rejects_missing_or_changed_workspace_game_files(self):
        with tempfile.TemporaryDirectory(prefix="saved crash replay ") as directory:
            root = Path(directory)
            source = self.fixture(root)
            bundle = replay.archive(root, "ball-homer-j", source)
            (root / "Simpsons Game, The (USA)").rmdir()
            with self.assertRaisesRegex(ValueError, "original game files"):
                replay.play(root, "ball-homer-j", timeout=5)
            (root / "Simpsons Game, The (USA)").mkdir()
            (root / "analysis/simpsons.pe").write_bytes(b"changed game image")
            with self.assertRaisesRegex(ValueError, "archived game image"):
                replay.play(root, "ball-homer-j", timeout=5)

    def test_play_can_skip_neutral_prefix_and_align_first_changed_poll(self):
        with tempfile.TemporaryDirectory(prefix="saved crash replay ") as directory:
            root = Path(directory)
            source = self.fixture(root)
            bundle = replay.archive(root, "ball-homer-j", source)
            process = mock.Mock(pid=1234, returncode=1)
            process.wait.return_value = 1

            def crashed(command, **kwargs):
                kwargs["stdout"].write((FAILURE + "\n").encode())
                return process

            with mock.patch.object(replay.subprocess, "Popen", side_effect=crashed) as popen:
                run = replay.play(root, "ball-homer-j", timeout=5, start_scene=374, skip_polls=4)
            command = popen.call_args.args[0]
            self.assertNotIn("--input-playback-from-first-poll", command)
            self.assertEqual(command[command.index("--input-playback-start-scene") + 1], "374")
            launch = replay.read_json(run / "launch.json")
            self.assertEqual(launch["playback_start_scene"], 374)
            self.assertEqual(launch["skipped_neutral_polls"], 4)
            self.assertEqual(launch["source_recording_sha256"],
                             replay.sha256(bundle / "run/inputs-fixture.jsonl"))
            rows = [json.loads(line) for line in
                    (run / "playback-inputs.jsonl").read_text(encoding="utf-8").splitlines()]
            self.assertEqual(rows[1]["seq"], 0)
            self.assertEqual(rows[1]["packet"], 2)
            self.assertEqual(rows[1]["buttons"], 0x4000)
            self.assertEqual(rows[-1]["samples"], 4)
            self.assertEqual(replay.recording_info(bundle / "run/inputs-fixture.jsonl")["polls"], 8)
            with self.assertRaisesRegex(ValueError, "multiple of four"):
                replay.play(root, "ball-homer-j", timeout=5, skip_polls=3)
            with self.assertRaisesRegex(ValueError, "leave at least one"):
                replay.play(root, "ball-homer-j", timeout=5, skip_polls=8)

    def test_play_candidate_executable_uses_same_archived_input_and_stores(self):
        with tempfile.TemporaryDirectory(prefix="saved crash replay ") as directory:
            root = Path(directory)
            source = self.fixture(root)
            bundle = replay.archive(root, "ball-homer-j", source)
            candidate = root / "build/native/CandidateRecorder.exe"
            candidate.write_bytes(b"new candidate runtime")
            process = mock.Mock(pid=1234, returncode=0)
            process.wait.return_value = 0

            def finished(command, **kwargs):
                return process

            with mock.patch.object(replay.subprocess, "Popen", side_effect=finished) as popen:
                run = replay.play(root, "ball-homer-j", timeout=5, executable=candidate)
            command = popen.call_args.args[0]
            self.assertEqual(command[0], str(candidate))
            self.assertEqual(command[command.index("--input-playback") + 1],
                             str(run / "playback-inputs.jsonl"))
            self.assertNotIn("--input-recording-directory", command)
            self.assertNotIn("--input-recording-auto-start", command)
            self.assertEqual(replay.read_json(run / "launch.json")["selected_executable_sha256"],
                             replay.sha256(candidate))
            self.assertEqual((run / "content/save-index/slot.save").read_bytes(), b"initial save")
            self.assertEqual((run / "playback-inputs.jsonl").read_bytes(),
                             (bundle / "run/inputs-fixture.jsonl").read_bytes())
            self.assertFalse(replay.read_json(run / "result.json")["reproduced_source_failure"])
            self.assertEqual(replay.read_json(run / "result.json")["failure"], "")

    def test_candidate_timeout_stops_process_and_records_timeout(self):
        with tempfile.TemporaryDirectory(prefix="saved crash replay ") as directory:
            root = Path(directory)
            source = self.fixture(root)
            replay.archive(root, "ball-homer-j", source)
            candidate = root / "build/native/CandidateRecorder.exe"
            candidate.write_bytes(b"new candidate runtime")
            process = mock.Mock(pid=1234, returncode=1)
            process.wait.side_effect = [replay.subprocess.TimeoutExpired("replay", 5), 1]
            with mock.patch.object(replay.subprocess, "Popen", return_value=process):
                run = replay.play(root, "ball-homer-j", timeout=5, executable=candidate)
            process.terminate.assert_called_once_with()
            self.assertTrue(replay.read_json(run / "result.json")["timed_out"])
            self.assertFalse(replay.read_json(run / "result.json")["reproduced_source_failure"])


if __name__ == "__main__":
    unittest.main()
