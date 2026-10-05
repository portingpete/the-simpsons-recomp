"""The recorder launch must preserve the starting save and isolate autosaves."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('record_gameplay_inputs', Path(__file__).resolve().parents[1] / 'tools/record_gameplay_inputs.py')
recorder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recorder)


class RecorderLaunchTests(unittest.TestCase):
    def test_private_run_and_initial_snapshot(self):
        with tempfile.TemporaryDirectory(prefix='simpsons recorder launch ') as directory:
            root = Path(directory)
            files = {'build/native/SimpsonsInputRecorder.exe': b'fixture executable',
                     'analysis/simpsons.pe': b'fixture image', 'profiles/player.profile': b'profile',
                     'profiles.video.cfg': b'4 4 0 1 120 6 3 2\n',
                     'content/save-index/slot.save': b'initial save', 'content/other.bin': b'other content'}
            for relative, data in files.items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            (root / 'config').mkdir()
            (root / 'config/startup_replay.json').write_text(json.dumps({
                'profile_id': 'player', 'profile_store': 'profiles', 'content_store': 'content',
                'save_index': 'save-index/slot.save'}))
            folder, command, manifest = recorder.prepare_run(root)
            self.assertIn(str(folder / 'content'), command)
            self.assertIn(str(folder / 'profile'), command)
            self.assertEqual(command[-2:], ['--input-recording-directory', str(folder)])
            self.assertEqual((folder / 'content/other.bin').read_bytes(), b'other content')
            self.assertEqual((folder / 'profile.video.cfg').read_bytes(), files['profiles.video.cfg'])
            self.assertEqual((folder / 'initial-state/profile.video.cfg').read_bytes(), files['profiles.video.cfg'])
            (folder / 'profile.video.cfg').write_bytes(b'changed private preferences')
            self.assertEqual((root / 'profiles.video.cfg').read_bytes(), files['profiles.video.cfg'])
            self.assertEqual((folder / 'initial-state/profile.video.cfg').read_bytes(), files['profiles.video.cfg'])
            (folder / 'content/save-index/slot.save').write_bytes(b'new autosave')
            self.assertEqual((root / 'content/save-index/slot.save').read_bytes(), b'initial save')
            self.assertEqual((folder / 'initial-state/SIMPSONS_SLOT1.save').read_bytes(), b'initial save')
            self.assertEqual(manifest['initial_save_sha256'], recorder.digest(root / 'content/save-index/slot.save'))
            self.assertEqual(manifest['initial_video_settings_sha256'], recorder.digest(root / 'profiles.video.cfg'))
            self.assertEqual(command[command.index('--frame-rate') + 1], '60')
            self.assertEqual(json.loads((folder / 'launch.json').read_text(encoding="utf-8")), manifest)
            self.assertEqual(list(folder.glob('inputs-*.jsonl')), [])
            second, _, _ = recorder.prepare_run(root)
            self.assertNotEqual(second, folder)
            self.assertEqual((folder / 'content/save-index/slot.save').read_bytes(), b'new autosave')
            (root / 'profiles.video.cfg').unlink()
            first_mission, command, manifest = recorder.prepare_run(root, first_mission=True)
            self.assertEqual(command[0], str(root / 'build/native/SimpsonsInputRecorder.exe'))
            self.assertIn('--render-test-first-mission', command)
            self.assertIn('--input-recording-auto-start', command)
            self.assertEqual(command[-2:], ['--input-recording-directory', str(first_mission)])
            self.assertTrue(manifest['first_mission'])
            self.assertTrue(manifest['recording_auto_start'])
            self.assertFalse((first_mission / 'profile.video.cfg').exists())
            self.assertIsNone(manifest['initial_video_settings_sha256'])


if __name__ == '__main__':
    unittest.main()
