import importlib.util
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('audit_stage_routes', ROOT / 'tools/audit_stage_routes.py')
routes = importlib.util.module_from_spec(spec); spec.loader.exec_module(routes)


class AuditRoutesTests(unittest.TestCase):
    def test_cli_timeout_boundaries_without_launch(self):
        # Exercise the actual CLI admission and setup with an observed launcher
        # stub. No native executable or process may run in this boundary test.
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'build').mkdir(); (root / 'config').mkdir(); (root / 'analysis').mkdir()
            executable = root / 'build/fixture.exe'; executable.write_bytes(b'CLI admission fixture only')
            (root / 'config/startup_replay.json').write_text('{}', encoding='utf-8')
            (root / 'analysis/simpsons.pe').write_bytes(b'CLI admission fixture only')
            with patch.object(routes, 'ROOT', root), patch.object(routes.subprocess, 'Popen') as process:
                for value in ('20', '1800'):
                    output = root / ('build/accepted-' + value)
                    argv = ['audit_stage_routes.py', '--frontend', '--interactive', '--executable', str(executable),
                            '--output', str(output), '--timeout=' + value]
                    with self.subTest(timeout=value), patch.object(sys, 'argv', argv), \
                            patch.object(routes, 'run_stage', return_value={'stage': 'frontend', 'success': True}) as launch, \
                            contextlib.redirect_stdout(io.StringIO()):
                        self.assertEqual(routes.main(), 0)
                        self.assertEqual(launch.call_args.args[5], float(value))
                        self.assertTrue((output / 'run-manifest.json').is_file())
                for index, value in enumerate(('19', '1801', 'nan', 'inf', '-inf')):
                    output = root / ('build/rejected-' + str(index))
                    argv = ['audit_stage_routes.py', '--frontend', '--interactive', '--executable', str(executable),
                            '--output', str(output), '--timeout=' + value]
                    error = io.StringIO()
                    with self.subTest(timeout=value), patch.object(sys, 'argv', argv), \
                            patch.object(routes, 'run_stage') as launch, contextlib.redirect_stderr(error):
                        with self.assertRaises(SystemExit) as rejected:
                            routes.main()
                        self.assertEqual(rejected.exception.code, 2)
                        self.assertIn('finite bounded timeout of 20..1800', error.getvalue())
                        launch.assert_not_called()
                        self.assertFalse(output.exists())
                process.assert_not_called()

    def test_baseline_video_is_private_and_pinned(self):
        with tempfile.TemporaryDirectory() as temporary:
            parent = Path(temporary)
            original = parent / 'original.video.cfg'
            raw = b'4 4 1 0 0 6 0 0\n'
            original.write_bytes(raw)
            run = parent / 'run'; run.mkdir()
            receipt = routes.prepare_private_video(original, run, routes.digest(original), True)
            private = run / 'profile.video.cfg'
            self.assertEqual(original.read_bytes(), raw)
            self.assertEqual(private.read_bytes(), b'4 0 0 0 60 0 0 0\n')
            self.assertEqual(receipt['sha256'], routes.digest(private))
            self.assertEqual(receipt['parameters'], [4, 0, 0, 0, 60, 0, 0, 0])

    def test_video_copy_is_verified_before_override(self):
        with tempfile.TemporaryDirectory() as temporary:
            parent = Path(temporary)
            original = parent / 'original.video.cfg'; original.write_bytes(b'changed')
            run = parent / 'run'; run.mkdir()
            with self.assertRaisesRegex(ValueError, 'Private video copy'):
                routes.prepare_private_video(original, run, '0' * 64, True)
            self.assertEqual((run / 'profile.video.cfg').read_bytes(), b'changed')
            absent = parent / 'absent'; absent.mkdir()
            self.assertIsNone(routes.prepare_private_video(parent / 'missing', absent, None))
            self.assertFalse((absent / 'profile.video.cfg').exists())

    def test_packaged_stems_and_bounded_smoke(self):
        self.assertEqual(len(routes.STAGES), 18)
        self.assertEqual(len(set(routes.STAGES)), 18)
        self.assertIn('brt', routes.STAGES)
        self.assertEqual(routes.validate_route(routes.SMOKE), routes.SMOKE)

    def test_native_commands_round_trip_values(self):
        self.assertEqual(routes.command_text(dict(name='a', buttons=0x4000, lx=-32768, ly=32767, ms=1500, rt=255)),
                         'PAD 4000 -32768 32767 1500 255\n')
        self.assertEqual(routes.command_text(dict(name='skip', command='START')), 'START\n')

    def test_bad_route_values_fail_before_launch(self):
        original = next(action for action in routes.SMOKE if 'buttons' in action)
        for mutation in ({'ms': 49}, {'ms': 2001}, {'buttons': 0x400}, {'rt': 256}, {'lx': -32769}, {'ly': True},
                         {'wait_after_ms': 30001}, {'wait_after_ms': -1}, {'capture_after': 'yes'}):
            with self.assertRaises(ValueError):
                routes.validate_route([{**original, **mutation}])
        for mutation in ({'wait_for_log': ''}, {'wait_for_log': '\n'}, {'wait_for_log': True},
                         {'wait_for_log': 'x' * 257}, {'wait_for_log_ms': 0}, {'wait_for_log_ms': 120001},
                         {'wait_for_log_occurrence': 0}, {'wait_for_log_occurrence': True},
                         {'wait_for_log_occurrence': 257}, {'wait_for_log_occurrence': 2}):
            with self.assertRaises(ValueError):
                routes.validate_route([{**original, **mutation}])
        with self.assertRaises(ValueError):
            routes.validate_route([dict(name='bad', command='DELETE')])

    def test_repeated_boundary_requires_new_original_event(self):
        log = 'ready owner=first\nother event\nready owner=second\n'
        self.assertEqual(routes.log_boundary(log, 'ready owner=', 2), 'ready owner=second')
        self.assertIsNone(routes.log_boundary(log, 'ready owner=', 3))
        action = dict(name='third-movie', command='START', wait_for_log='ready owner=', wait_for_log_occurrence=3)
        self.assertEqual(routes.validate_route([action]), [action])

    def test_reparse_is_rejected_without_scan(self):
        with patch.object(Path, 'lstat', return_value=types.SimpleNamespace(st_file_attributes=0x400)), \
                patch('audit_stage_routes.os.scandir') as scan, patch.object(Path, 'is_symlink', return_value=False):
            with self.assertRaisesRegex(ValueError, 'Reparse'):
                routes.scan_plain(Path('unsafe'))
            scan.assert_not_called()

    def test_full_private_tree_hashes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); (root / 'nested').mkdir()
            (root / 'a').write_bytes(b'profile'); (root / 'nested/b').write_bytes(b'save')
            before = routes.tree_hashes(root)
            self.assertEqual(set(before), {'a', 'nested/b'})
            (root / 'nested/b').write_bytes(b'changed')
            after = routes.tree_hashes(root)
            self.assertNotEqual(before, after)
            self.assertEqual(before['a'], after['a'])

    def test_actual_input_receipt_shape(self):
        match = routes.RECEIPT.search('[NATIVE INPUT] local command tap buttons=4000 hold_ms=900; left=(0,32767) rt=0; delivered to native controller source')
        self.assertIsNotNone(match)
        self.assertEqual([int(match[1], 16), *map(int, match.groups()[1:])], [0x4000, 900, 0, 32767, 0])

    def _admit(self, *arguments):
        """Run the real CLI admission with a launcher stub; returns (exit code or 0, run_stage mock, stderr)."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'build').mkdir(); (root / 'config').mkdir(); (root / 'analysis').mkdir()
            executable = root / 'build/fixture.exe'; executable.write_bytes(b'CLI admission fixture only')
            (root / 'config/startup_replay.json').write_text('{}', encoding='utf-8')
            (root / 'analysis/simpsons.pe').write_bytes(b'CLI admission fixture only')
            argv = ['audit_stage_routes.py', '--executable', str(executable), '--output', str(root / 'build/out'), *arguments]
            error = io.StringIO()
            with patch.object(routes, 'ROOT', root), patch.object(routes.subprocess, 'Popen') as process,                     patch.object(sys, 'argv', argv), patch.object(routes, 'run_stage', return_value={'stage': 'x', 'success': True}) as launch,                     contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(error):
                try:
                    code = routes.main()
                except SystemExit as exit_request:
                    code = exit_request.code
                process.assert_not_called()
                manifest = (root / 'build/out/run-manifest.json').read_text() if (root / 'build/out/run-manifest.json').exists() else None
            return code, launch, error.getvalue(), manifest

    def test_receipt_timeout_boundaries(self):
        for value in ('5', '60', '30'):
            with self.subTest(value=value):
                code, launch, _, manifest = self._admit('--frontend', '--receipt-timeout=' + value)
                self.assertEqual(code, 0)
                self.assertEqual(launch.call_args.args[-1], float(value))
                self.assertEqual(json.loads(manifest)['receipt_timeout'], float(value))
        for value in ('4.9', '61', 'nan', 'inf', '-5'):
            with self.subTest(value=value):
                code, launch, error, manifest = self._admit('--frontend', '--receipt-timeout=' + value)
                self.assertEqual(code, 2)
                self.assertIn('Receipt timeout must be 5..60 seconds', error)
                launch.assert_not_called()
                self.assertIsNone(manifest)

    def test_completion_mode_is_exclusive_labeled_and_launches_the_shortcut_argument(self):
        code, launch, _, manifest = self._admit('--completion')
        self.assertEqual(code, 0)
        self.assertEqual(launch.call_args.args[0], 'first_mission_completion')
        self.assertIs(launch.call_args.args[-2], True)
        self.assertIs(json.loads(manifest)['completion'], True)
        for extra in (['--stages', 'loc'], ['--frontend'], ['--play-intro'], ['--play-intro', '--intro-skip-after=3']):
            with self.subTest(extra=extra):
                code, launch, error, manifest = self._admit('--completion', *extra)
                self.assertEqual(code, 2)
                self.assertIn('Completion mode is exclusive', error)
                launch.assert_not_called()
                self.assertIsNone(manifest)
        # An ordinary stage run records completion=false.
        code, launch, _, manifest = self._admit('--stages', 'loc')
        self.assertEqual(code, 0)
        self.assertIs(launch.call_args.args[-2], False)
        self.assertIs(json.loads(manifest)['completion'], False)

    def test_launch_command_selects_stage_completion_or_frontend_and_keeps_private_paths(self):
        run = Path('/fixture/run')
        config = {'profile_id': 'profile-id'}
        stage = routes.launch_command(Path('/x/game.exe'), run, config, 'loc', False, False)
        self.assertEqual(stage[stage.index('--stage') + 1], 'loc')
        self.assertNotIn('--first-mission-completion', stage)
        completion = routes.launch_command(Path('/x/game.exe'), run, config, 'first_mission_completion', True, False)
        self.assertIn('--first-mission-completion', completion)
        self.assertNotIn('--stage', completion)
        frontend = routes.launch_command(Path('/x/game.exe'), run, config, 'frontend', False, False)
        self.assertNotIn('--stage', frontend)
        self.assertNotIn('--first-mission-completion', frontend)
        self.assertIn('--play-stage-intro', routes.launch_command(Path('/x/game.exe'), run, config, 'loc', False, True))
        for command in (stage, completion, frontend):
            self.assertEqual(command[command.index('--profile-store') + 1], str(run / 'profile'))
            self.assertEqual(command[command.index('--content-store') + 1], str(run / 'content'))
            self.assertEqual(command[command.index('--controller-input') + 1], str(run / 'controller.commands'))
            self.assertEqual(command[command.index('--resource-audit') + 1], str(run / 'resources.jsonl'))
            self.assertEqual(command[command.index('--local-profile') + 1], '0:profile-id')
            self.assertIn('--capture-on-request', command)

    def test_outro_skip_option_bounds_and_due_logic(self):
        for value in ('0.2', '1', '4', '2.5'):
            with self.subTest(value=value):
                code, launch, _, manifest = self._admit('--completion', '--outro-skip-after=' + value)
                self.assertEqual(code, 0)
                self.assertEqual(launch.call_args.kwargs['outro_skip_after'], float(value))
                self.assertEqual(json.loads(manifest)['outro_skip_after'], float(value))
        for value in ('0.19', '4.1', '60', 'nan', 'inf', '-1'):
            with self.subTest(value=value):
                code, launch, error, manifest = self._admit('--completion', '--outro-skip-after=' + value)
                self.assertEqual(code, 2)
                self.assertIn('Outro skip requires --completion and a delay of 0.2..4 seconds', error)
                launch.assert_not_called()
        for extra in (['--stages', 'loc'], ['--frontend']):
            with self.subTest(extra=extra):
                code, launch, error, _ = self._admit('--outro-skip-after=2', *extra)
                self.assertEqual(code, 2)
                self.assertIn('Outro skip requires --completion', error)
                launch.assert_not_called()
        code, launch, _, manifest = self._admit('--completion')
        self.assertIsNone(launch.call_args.kwargs['outro_skip_after'])
        self.assertIsNone(json.loads(manifest)['outro_skip_after'])
        # The skip is due exactly once, only after the outro's readiness and the delay.
        due = routes.outro_skip_due
        self.assertFalse(due(100.0, None, False, 2.0))        # no outro movie yet (the completion helper has not run or no movie started)
        self.assertFalse(due(101.9, 100.0, False, 2.0))       # before the delay
        self.assertTrue(due(102.0, 100.0, False, 2.0))        # at the delay
        self.assertFalse(due(103.0, 100.0, True, 2.0))        # already requested
        self.assertFalse(due(103.0, 100.0, False, None))      # option absent
        # The log pump: a neutral receipt every interval while armed (the game's 1 MiB stderr buffer is flushed by every receipt).
        pump = routes.log_pump_due
        self.assertTrue(pump(10.0, None, True))               # first pump immediately
        self.assertFalse(pump(10.4, 10.0, True))              # too soon
        self.assertTrue(pump(10.5, 10.0, True))               # interval elapsed
        self.assertFalse(pump(99.0, 10.0, False))             # inactive (no option, or the skip already requested)
        self.assertFalse(pump(99.0, None, False))


sys.modules['audit_stage_routes'] = routes
if __name__ == '__main__':
    unittest.main()
