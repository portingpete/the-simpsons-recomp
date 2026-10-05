"""Startup data isolation tests; no native process or controller is launched."""

from pathlib import Path
import contextlib
import io
import json
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import auto_start_native


class IsolationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / "build/native").mkdir(parents=True)
        (self.root / "build/native/SimpsonsNative.exe").write_bytes(b"test executable")
        (self.root / "config").mkdir()
        self.profile = self.root / "build/profile-source"
        self.content = self.root / "build/content-source"
        self.profile.mkdir()
        self.save_index = Path("save-index/player/45410809/SIMPSONS_SLOT1.save")
        (self.content / self.save_index.parent).mkdir(parents=True)
        (self.profile / "player.profile").write_bytes(b"original profile")
        self.video_preferences=self.profile.with_name(self.profile.name+".video.cfg")
        self.video_preferences.write_bytes(b"4 4 0 1 120 6 3 2\n")
        (self.content / self.save_index).write_bytes(b"original save")
        (self.root / "config/startup_replay.json").write_text(json.dumps({
            "profile_id": "player",
            "profile_store": "build/profile-source",
            "content_store": "build/content-source",
            "save_index": self.save_index.as_posix(),
            "screens": {},
        }), encoding="utf-8")

    def run_startup(self, name: str, *, isolate: bool, launch_check=None):
        run_dir = self.root / "build/automatic-startup" / name
        process = MagicMock(pid=4321)
        replay = MagicMock()
        replay.main_menu_verified = True
        replay.input_sequence_completed = False
        replay.gameplay_verified = False
        replay.gameplay_evidence = None
        command = ["--run-directory", str(run_dir), "--until", "main-menu"]
        if isolate:
            command.append("--isolate-data")
        with patch.object(auto_start_native, "ROOT", self.root), \
             patch.object(auto_start_native.subprocess, "Popen", return_value=process) as launch, \
             patch.object(auto_start_native, "Replay", return_value=replay):
            if launch_check is not None:
                launch.side_effect = lambda *args, **kwargs: (launch_check(), process)[1]
            self.assertEqual(auto_start_native.main(command), 0)
        self.assertEqual(launch.call_count, 1)
        return run_dir, launch.call_args.args[0]

    def test_isolated_launch_uses_private_copies_created_before_process(self):
        expected_run = self.root / "build/automatic-startup/isolated"
        def copies_exist():
            self.assertEqual((expected_run / "profile/player.profile").read_bytes(), b"original profile")
            self.assertEqual((expected_run / "content" / self.save_index).read_bytes(), b"original save")
            self.assertEqual((expected_run / "profile.video.cfg").read_bytes(), self.video_preferences.read_bytes())
        run_dir, command = self.run_startup("isolated", isolate=True, launch_check=copies_exist)
        self.assertEqual(command[command.index("--profile-store") + 1], str(run_dir / "profile"))
        self.assertEqual(command[command.index("--content-store") + 1], str(run_dir / "content"))
        (run_dir / "content" / self.save_index).write_bytes(b"changed by game")
        (run_dir / "profile.video.cfg").write_bytes(b"changed by game")
        self.assertEqual((self.content / self.save_index).read_bytes(), b"original save")
        self.assertEqual(self.video_preferences.read_bytes(),b"4 4 0 1 120 6 3 2\n")
        recorded = json.loads((run_dir / "launch.json").read_text(encoding="utf-8"))
        self.assertEqual(recorded["command"], command)

    def test_default_launch_keeps_configured_stores(self):
        run_dir, command = self.run_startup("shared", isolate=False)
        self.assertEqual(command[command.index("--profile-store") + 1], str(self.profile))
        self.assertEqual(command[command.index("--content-store") + 1], str(self.content))
        self.assertFalse((run_dir / "profile").exists())
        self.assertFalse((run_dir / "content").exists())
        self.assertFalse((run_dir / "profile.video.cfg").exists())

    def test_incomplete_copy_does_not_launch(self):
        real_copytree = auto_start_native.shutil.copytree
        def incomplete_copy(source, destination):
            if source == self.content:
                destination.mkdir()
                return destination
            return real_copytree(source, destination)
        with patch.object(auto_start_native, "ROOT", self.root), \
             patch.object(auto_start_native.shutil, "copytree", side_effect=incomplete_copy), \
             patch.object(auto_start_native.subprocess, "Popen") as launch, \
             contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                auto_start_native.main(["--run-directory", str(self.root / "build/automatic-startup/incomplete"),
                                        "--until", "main-menu", "--isolate-data"])
        launch.assert_not_called()


if __name__ == "__main__":
    unittest.main()
