"""Inspect complete input recordings without launching the game."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from tools import replay_recorded_route as route


ROOT = Path(__file__).resolve().parents[1]
LATEST = ROOT / "build/input-recordings/20260923-174948Z-75fbf27f"


class RecordedRouteTests(unittest.TestCase):
    def test_private_replay_restores_video_preferences_and_relocated_menu_assets(self):
        preferences={'snapshot':b'4 4 0 1 120 6 3 2\n',
                     'recorded':b'4 3 1 0 120 5 3 1\n',
                     'configured':b'4 6 0 0 0 8 3 3\n'}
        for choice in ('snapshot','recorded','configured'):
            with self.subTest(choice=choice), tempfile.TemporaryDirectory(prefix='simpsons replay launch ') as directory:
                root=Path(directory);reference=root/'reference'
                files={'build/native/SimpsonsInputRecorder.exe':b'current recorder',
                       'build/native/audio.dll':b'audio dependency',
                       'build/native/native-assets/frontend/frontend.str':b'frontend native menu',
                       'build/native/native-assets/simpsons_chars/simpsons_chars_global.str':b'in-game native menu',
                       'profiles/player.profile':b'configured profile',
                       'profiles.video.cfg':preferences['configured'],
                       'content/save-index/slot.save':b'configured save',
                       'reference/initial-state/player.profile':b'initial profile',
                       'reference/initial-state/SIMPSONS_SLOT1.save':b'initial save',
                       'reference/inputs-fixture.jsonl':b'recorded controller polls',
                       'build/candidate/SimpsonsInputRecorder.exe':b'candidate without adjacent assets'}
                if choice!='configured':files['reference/profile.video.cfg']=preferences['recorded']
                if choice=='snapshot':files['reference/initial-state/profile.video.cfg']=preferences['snapshot']
                for name,data in files.items():
                    path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
                (root/'config').mkdir()
                (root/'config/startup_replay.json').write_text(json.dumps({'profile_store':'profiles','content_store':'content','profile_id':'player','save_index':'save-index/slot.save'}))
                (reference/'launch.json').write_text(json.dumps({'initial_save_sha256':route.sha256(reference/'initial-state/SIMPSONS_SLOT1.save')}))
                recording=reference/'inputs-fixture.jsonl'
                selected=root/('build/candidate/SimpsonsInputRecorder.exe' if choice=='configured' else 'build/native/SimpsonsInputRecorder.exe')
                with mock.patch.object(route,'ROOT',root):
                    folder,command=route.prepare_run(reference,selected,{'recording':str(recording),'recording_sha256':route.sha256(recording)},False)
                self.assertEqual((folder/'profile.video.cfg').read_bytes(),preferences[choice])
                self.assertEqual(command[0],str(folder/'SimpsonsInputRecorder.exe'))
                self.assertEqual(command[command.index('--frame-rate')+1],'60')
                self.assertEqual((folder/'native-assets/frontend/frontend.str').read_bytes(),b'frontend native menu')
                self.assertEqual((folder/'native-assets/simpsons_chars/simpsons_chars_global.str').read_bytes(),b'in-game native menu')
                if choice!='configured':self.assertEqual((folder/'audio.dll').read_bytes(),b'audio dependency')
                self.assertEqual((folder/'SimpsonsInputRecorder.exe').read_bytes(),selected.read_bytes())
                (folder/'profile.video.cfg').write_bytes(b'changed private preferences')
                for name,data in files.items():self.assertEqual((root/name).read_bytes(),data)

    def test_original_route_keeps_its_timing_and_keyboard_events(self):
        events, metadata = route.route_from_recording(route.REFERENCE, keyboard=True)
        self.assertEqual((metadata["recording_start_scene"], metadata["recording_saved_scene"],
                          metadata["reference_failure"]["scene"]), (232, 743, 743))
        self.assertEqual((metadata["recorded_polls"], metadata["recorded_cycles"],
                          metadata["state_changes"], len(events)), (2044, 511, 40, 40))
        self.assertEqual(route.playback_start_scene(route.REFERENCE, metadata, None), 233)
        self.assertEqual(route.replay_stop_scene(metadata, None), 800)
        self.assertEqual(events[0], (269, {ord("A")}))

    def test_latest_exact_playback_uses_its_recording_and_crash_scenes(self):
        events, metadata = route.route_from_recording(LATEST, keyboard=False)
        self.assertEqual(events, [])  # Exact playback consumes the JSONL polls directly.
        self.assertEqual((metadata["recording_start_scene"], metadata["recording_saved_scene"],
                          metadata["reference_failure"]["scene"]), (263, 1150, 1174))
        self.assertEqual((metadata["recorded_polls"], metadata["recorded_cycles"],
                          metadata["state_changes"]), (3548, 887, 129))
        self.assertEqual(route.playback_start_scene(LATEST, metadata, None), 263)
        self.assertEqual(route.replay_stop_scene(metadata, None), 1175)
        self.assertEqual(route.playback_start_scene(LATEST, metadata, 300), 300)
        self.assertEqual(route.replay_stop_scene(metadata, 1200), 1200)
        with self.assertRaises(ValueError):
            route.playback_start_scene(LATEST, metadata, 0)
        with self.assertRaises(ValueError):
            route.replay_stop_scene(metadata, 0)

    def test_latest_keyboard_comparison_has_no_forty_change_limit(self):
        events, metadata = route.route_from_recording(LATEST, keyboard=True)
        self.assertEqual(len(events), 129)
        self.assertEqual(metadata["state_changes"], len(events))
        self.assertEqual(events[0], (285, {ord("S")}))
        self.assertTrue(any(ord("J") in keys for _, keys in events))

    def test_inspect_cli_never_needs_an_executable(self):
        process = subprocess.run(
            [sys.executable, "-B", str(ROOT / "tools/replay_recorded_route.py"),
             "--reference", str(LATEST), "--inspect", "--executable", str(ROOT / "missing.exe")],
            capture_output=True, text=True, timeout=20)
        self.assertEqual(process.returncode, 0, process.stderr)
        result = json.loads(process.stdout)
        self.assertEqual((result["playback_start_scene"], result["planned_stop_scene"],
                          result["recorded_polls"], result["events"]), (263, 1175, 3548, []))

    def test_direct_playback_does_not_require_keyboard_mappable_controls(self):
        with tempfile.TemporaryDirectory(prefix="simpsons-replay-recording-") as directory:
            folder = Path(directory)
            rows = [{"type": "header", "version": 1, "boundary": "returned_controller_state"}]
            for slot in range(4):
                connected = slot == 0
                rows.append({"type": "input", "seq": slot, "t_us": 100 + slot,
                             "consumer": "game", "slot": slot,
                             "status": 0 if connected else 1167,
                             "packet": 1 if connected else 0,
                             "buttons": 0x2000 if connected else 0,
                             "lt": 45 if connected else 0,
                             "rt": 77 if connected else 0,
                             "lx": 1234 if connected else 0,
                             "ly": -2345 if connected else 0,
                             "rx": 100 if connected else 0,
                             "ry": -99 if connected else 0})
            rows.append({"type": "end", "samples": 4, "t_us": 200, "reason": "user"})
            recording = folder / "inputs-synthetic.jsonl"
            recording.write_text("".join(json.dumps(row, separators=(",", ":")) + "\n" for row in rows),
                                 encoding="utf-8")
            (folder / "game.log").write_text(
                "[NATIVE VIEWPORT DEPTH COPY] count=7\n"
                "[INPUT RECORDING] START file=inputs-synthetic.jsonl\n"
                "[NATIVE VIEWPORT DEPTH COPY] count=10\n"
                "[FAILURE] synthetic\n", encoding="utf-8")
            events, metadata = route.route_from_recording(folder, keyboard=False)
            self.assertEqual(events, [])
            self.assertEqual(metadata["recording_start_scene"], 7)
            self.assertEqual(metadata["reference_failure"]["scene"], 10)
            self.assertEqual(metadata["recorded_polls"], 4)
            with self.assertRaisesRegex(ValueError, "neutral keyboard"):
                route.route_from_recording(folder, keyboard=True)
            rows[2]["seq"] = 9
            recording.write_text("".join(json.dumps(row, separators=(",", ":")) + "\n" for row in rows),
                                 encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "poll order"):
                route.route_from_recording(folder, keyboard=False)


if __name__ == "__main__":
    unittest.main()
