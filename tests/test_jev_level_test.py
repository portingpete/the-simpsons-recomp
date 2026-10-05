"""Controller/API boundary checks; these do not claim a level was completed."""
from collections import deque
import io
import json
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from jev_level_test import (ACTIONS, DEFAULT_PLAY_ACTIONS, GameSession, JevPolicy, SmokePolicy,
                            action_plan, change_label, compact_startup_captures,
                            decision_state, pad_command, parse_choice, parse_receipt,
                            parse_smoke_actions,
                            record_action_result, redact_secret, startup,
                            validate_goal_cue)


class JevLevelTestUnitTests(unittest.TestCase):
    def test_action_commands_match_native_pad_contract(self):
        self.assertEqual(pad_command('forward_jump', 600), b'PAD 1000 0 32767 600\n')
        self.assertEqual(pad_command('wait', 500), b'PAD 0000 0 0 500\n')
        self.assertEqual(pad_command('ability', 250), b'PAD 2000 0 0 250\n')
        self.assertEqual(pad_command('attack', 250), b'PAD 4000 0 0 250\n')
        self.assertEqual(pad_command('interact', 250), b'PAD 8000 0 0 250\n')
        self.assertEqual(pad_command('ball_forward', 600), b'PAD 0000 0 32767 600 255\n')
        self.assertEqual(pad_command('forward_double_jump', 600),
                         b'PAD 1000 0 32767 100\n'
                         b'PAD 0000 0 32767 180\n'
                         b'PAD 1000 0 32767 100\n')
        self.assertEqual(len(action_plan('double_jump', 600)), 3)
        with self.assertRaises(ValueError):
            pad_command('start', 250)
        with self.assertRaises(ValueError):
            pad_command('jump', 2001)

    def test_explicit_smoke_actions_are_bounded_and_known(self):
        self.assertEqual(parse_smoke_actions(None), SmokePolicy.SCRIPT)
        script = parse_smoke_actions('attack, interact,ball,forward_double_jump')
        self.assertEqual(script, ('attack', 'interact', 'ball', 'forward_double_jump'))
        self.assertEqual(SmokePolicy(script).choose({}, 3)['action'], 'forward_double_jump')
        self.assertEqual(SmokePolicy(script).choose({}, 4)['action'], 'attack')
        for invalid in ('', 'attack,', 'start', ','.join(['attack'] * 33)):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                parse_smoke_actions(invalid)

    def test_native_rt_receipts_and_macro_action_are_checked(self):
        self.assertEqual(parse_receipt('local command tap buttons=4000 hold_ms=600; '
                                       'left=(0,32767) rt=255; delivered to native controller source'),
                         (0x4000, 600, 0, 32767, 255))
        self.assertEqual(parse_receipt('local command tap buttons=1000 hold_ms=100; '
                                       'left=(0,0); delivered to native controller source'),
                         (0x1000, 100, 0, 0, 0))
        with tempfile.TemporaryDirectory() as temporary:
            run = Path(temporary)
            (run / 'controller.commands').write_bytes(b'')
            session = GameSession.__new__(GameSession)
            session.run = run
            session.receipts = []

            def deliver_written_commands():
                if session.receipts:
                    return
                for line in (run / 'controller.commands').read_text().splitlines():
                    _, buttons, lx, ly, duration, *rt = line.split()
                    receipt = (int(buttons, 16), int(duration), int(lx), int(ly),
                               int(rt[0]) if rt else 0)
                    session.receipts.append(receipt)

            session.check = deliver_written_commands
            session.wait = lambda seconds: None
            delivered = []
            session.act('forward_double_jump', 600, on_delivery=delivered.append)
            self.assertEqual(delivered, [[(0x1000, 100, 0, 32767, 0),
                                          (0, 180, 0, 32767, 0),
                                          (0x1000, 100, 0, 32767, 0)]])

    def test_unexpected_jev_answer_cannot_reach_controller(self):
        probs = {name: 1 / len(ACTIONS) for name in ACTIONS}
        response = SimpleNamespace(choices={'action': SimpleNamespace(
            choice='forward_jump', confidence=0.6, probabilities=probs)},
            model='jev-1.13.0', request_id='fixture', usage=SimpleNamespace(input_tokens=100, output_tokens=0))
        self.assertEqual(parse_choice(response)['action'], 'forward_jump')
        response.choices['action'].choice = 'START'
        with self.assertRaisesRegex(ValueError, 'outside the allowed set'):
            parse_choice(response)
        response.choices['action'].choice = 'jump'
        response.choices['action'].probabilities = {'jump': 1.0}
        with self.assertRaisesRegex(ValueError, 'incomplete action distribution'):
            parse_choice(response)
        self.assertNotIn('ability', DEFAULT_PLAY_ACTIONS)
        response.choices['action'].probabilities = {name: 1 / len(DEFAULT_PLAY_ACTIONS)
                                                    for name in DEFAULT_PLAY_ACTIONS}
        response.choices['action'].choice = 'ability'
        with self.assertRaisesRegex(ValueError, 'outside the allowed set'):
            parse_choice(response, DEFAULT_PLAY_ACTIONS)

    def test_jev_choice_excludes_b_and_stalled_movement(self):
        from unittest.mock import patch
        offered = []

        def make_choice(**kwargs):
            offered.append(tuple(kwargs['criteria']))
            return SimpleNamespace(criteria=kwargs['criteria'])

        def choose_response(*, state, questions):
            names = tuple(questions['action'].criteria)
            answer = SimpleNamespace(choice='attack', confidence=0.7,
                                     probabilities={name: 1 / len(names) for name in names})
            return SimpleNamespace(choices={'action': answer}, model='fixture',
                                   request_id='fixture',
                                   usage=SimpleNamespace(input_tokens=1, output_tokens=1))

        client = SimpleNamespace(system_one=choose_response)
        state = {'temporarily_blocked_actions': ['forward']}
        with patch('typesafe_sdk.Choice', side_effect=make_choice), \
                patch('typesafe_sdk.TypeSafeClient', return_value=client):
            self.assertEqual(JevPolicy('fixture').choose(state, 0)['action'], 'attack')
            self.assertNotIn('ability', offered[-1])
            self.assertNotIn('forward', offered[-1])
            self.assertEqual(JevPolicy('fixture', allow_b=True).choose({}, 0)['action'], 'attack')
            self.assertIn('ability', offered[-1])

    def test_state_uses_compact_observation_and_history(self):
        observation = {'color_grid': ['R' * 32] * 18, 'luminance_grid': ['@' * 32] * 18,
                       'frame_change_fraction': 0.01, 'pixel_sha256': 'not sent'}
        state = decision_state(observation, deque([{'action': 'jump'}]), 'Reach the exit')
        self.assertEqual(state['objective'], 'Reach the exit')
        self.assertEqual(state['last_visual_change'], 'some visual change')
        self.assertNotIn('pixel_sha256', state)
        self.assertEqual(SmokePolicy().choose(state, 1)['action'], 'forward')
        self.assertEqual(change_label(None), 'first observation')
        telemetry = {'player': {'position': [1, 2, 3]}, 'world': None,
                     'scene_depth_grid': {'values': [[0] * 16] * 9}}
        with_facts = decision_state(observation, deque(), 'Reach the exit', telemetry)
        self.assertEqual(with_facts['verified_game_state'], telemetry)
        self.assertLess(list(with_facts).index('verified_game_state'), list(with_facts).index('color_grid'))
        self.assertIn('gate', with_facts['route_hint'])
        stuck = deque([{'action': 'forward', 'visual_change': 'very little visual change',
                        'end_position': [7.48, 0, -32.62],
                        'observed_motion': {'player_distance': 0.0}}])
        telemetry['player']['position'] = [7.48, 0, -32.62]
        stall_state = decision_state(observation, stuck, 'Reach the exit', telemetry)
        self.assertEqual(stall_state['temporarily_blocked_actions'], ['forward'])
        self.assertIn('0.25', stall_state['navigation_warning'])

    def test_error_redaction(self):
        from unittest.mock import patch
        with patch.dict('os.environ', {'TYPESAFE_API_KEY': 'private-fixture'}):
            self.assertEqual(redact_secret('error private-fixture here'), 'error [redacted API key] here')

    def test_each_delivered_action_gets_successor_result_event(self):
        history = deque([{'step': 2, 'action': 'forward', 'visual_change': 'pending'}])
        log = io.StringIO()
        before = {'player': {'position': [1, 2, 3]}, 'world': None}
        after = {'player': {'position': [4, 6, 3]}, 'world': None}
        record_action_result(history, {'presentation': 91, 'frame_change_fraction': 0.04},
                             before, after, log, 'captures/native-frame-91.rgb10a2')
        event = json.loads(log.getvalue())
        self.assertEqual((event['event'], event['step'], event['action']),
                         ('action-result', 2, 'forward'))
        self.assertEqual(event['presentation'], 91)
        self.assertEqual(event['observed_motion']['player_distance'], 5)
        self.assertEqual(history[-1]['observed_motion']['player_distance'], 5)
        record_action_result(history, {'presentation': 92, 'frame_change_fraction': 0.04},
                             after, after, log, 'captures/native-frame-92.rgb10a2')
        self.assertEqual(len(log.getvalue().splitlines()), 1)

    def test_goal_cue_requires_real_bounded_pixel_samples(self):
        valid = {'samples': [[0, 100, 200, 300]], 'tolerance': 10, 'minimum_match': 1.0}
        self.assertIs(validate_goal_cue(valid), valid)
        for invalid in (
            {**valid, 'samples': []},
            {**valid, 'minimum_match': 0},
            {**valid, 'samples': [[1280 * 720 * 4, 1, 2, 3]]},
            {**valid, 'samples': [[1, 1, 2, 3]]},
            {**valid, 'samples': [[0, 1024, 2, 3]]},
        ):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                validate_goal_cue(invalid)

    def test_startup_does_not_pass_jev_key_to_game(self):
        from unittest.mock import patch
        import json
        with tempfile.TemporaryDirectory() as temporary:
            run = Path(temporary) / 'new-run'
            run.mkdir()
            observed = {}

            def fake_run(*args, **kwargs):
                observed.update(kwargs['env'])
                (run / 'result.json').write_text(json.dumps({'success': True, 'gameplay_verified': True}), encoding='utf-8')
                (run / 'launch.json').write_text(json.dumps({'pid': 123}), encoding='utf-8')
                return SimpleNamespace(returncode=0)

            args = SimpleNamespace(executable=Path('SimpsonsNative.exe'), startup_timeout=5)
            with patch.dict('os.environ', {'TYPESAFE_API_KEY': 'private-fixture',
                                            'TYPESAFE_LOG_LEVEL': 'off'}):
                with patch('jev_level_test.subprocess.run', side_effect=fake_run):
                    self.assertEqual(startup(run, args)[0], 123)
            self.assertNotIn('TYPESAFE_API_KEY', observed)
            self.assertNotIn('TYPESAFE_LOG_LEVEL', observed)

    def test_compaction_preserves_referenced_startup_frames(self):
        with tempfile.TemporaryDirectory() as temporary:
            run = Path(temporary)
            captures = run / 'captures'
            captures.mkdir()
            for number in range(1, 5):
                frame = captures / f'native-frame-{number}.rgb10a2'
                frame.write_bytes(b'fixture')
                frame.with_suffix('.json').write_text('{}', encoding='utf-8')
            (run / 'inputs.jsonl').write_text('{"frame":"native-frame-1.rgb10a2"}\n', encoding='utf-8')
            removed = compact_startup_captures(run, {'first_scene_frame': {'frame': 'captures/native-frame-2.rgb10a2'}})
            self.assertEqual(removed, 1)
            self.assertEqual(sorted(p.name for p in captures.glob('*.rgb10a2')),
                             ['native-frame-1.rgb10a2', 'native-frame-2.rgb10a2', 'native-frame-4.rgb10a2'])
            self.assertFalse((captures / 'native-frame-3.json').exists())


if __name__ == '__main__':
    unittest.main()
