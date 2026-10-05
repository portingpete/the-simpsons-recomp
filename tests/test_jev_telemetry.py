"""Structured game state must match the selected renderer frame."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from jev_telemetry import capture_telemetry, movement_result


class JevTelemetryTests(unittest.TestCase):
    def test_exact_presentation_and_schema_are_required(self):
        raw = {'schema_version': 1, 'presentation': 41,
               'player': {'available': True, 'position': [1, 2, 3]}}
        state, status = capture_telemetry({'telemetry': raw}, 41)
        self.assertEqual(state['player']['position'], [1.0, 2.0, 3.0])
        self.assertEqual(status['status'], 'available')
        for value, expected in (({**raw, 'presentation': 40}, 'presentation_mismatch'),
                                ({**raw, 'schema_version': 2}, 'unsupported_schema')):
            with self.subTest(value=value):
                self.assertEqual(capture_telemetry({'telemetry': value}, 41),
                                 (None, {'status': expected}))
        self.assertEqual(capture_telemetry({}, 41), (None, {'status': 'missing'}))

    def test_unavailable_player_is_not_sent_as_game_fact(self):
        raw = {'schema_version': 1, 'presentation': 41,
               'player': {'available': False, 'position': [999, 999, 999]}}
        state, status = capture_telemetry({'telemetry': raw}, 41)
        self.assertIsNone(state)
        self.assertEqual(status['player'], 'player_unavailable')

    def test_nonfinite_player_data_cannot_reach_model(self):
        raw = {'schema_version': 1, 'presentation': 41,
               'player': {'available': True, 'position': [1, float('nan'), 3]}}
        state, status = capture_telemetry({'telemetry': raw}, 41)
        self.assertIsNone(state)
        self.assertEqual(status['player'], 'invalid_player_position')
        raw['player']['position'] = [1, 1_000_001, 3]
        self.assertEqual(capture_telemetry({'telemetry': raw}, 41)[1]['player'],
                         'invalid_player_position')

    def test_only_qualified_player_and_camera_fields_reach_model(self):
        raw = {'schema_version': 1, 'presentation': 41,
               'player': {'available': True, 'position': [1, 2, 3], 'grounded': True,
                          'unknown_raw_pointer': '0x1234', 'source': 'guest_character_transform'},
               'world': {'available': True, 'presentation': 41, 'camera_eye': [2, 1, 3],
                         'entities_available': False, 'objective_available': False,
                         'unverified_goal': 'exit open'}}
        state, status = capture_telemetry({'telemetry': raw}, 41)
        self.assertEqual(status['world'], 'available')
        self.assertNotIn('unknown_raw_pointer', state['player'])
        self.assertEqual(state['world'], {'camera_eye': [2.0, 1.0, 3.0]})
        raw['world']['presentation'] = 40
        state, status = capture_telemetry({'telemetry': raw}, 41)
        self.assertIsNone(state['world'])
        self.assertEqual(status['world'], 'world_presentation_mismatch')
        raw['world']['presentation'] = 41
        raw['world']['camera_eye'] = [2, float('inf'), 3]
        state, status = capture_telemetry({'telemetry': raw}, 41)
        self.assertIsNone(state['world'])
        self.assertEqual(status['world'], 'invalid_camera_eye')

    def test_observed_displacement_and_unavailable_fallback(self):
        before = {'player': {'position': [1, 2, 3]}}
        after = {'player': {'position': [4, 6, 3]}}
        self.assertEqual(movement_result(before, after), {
            'player_delta': [3, 4, 0], 'player_distance': 5, 'position_unchanged': False})
        self.assertIsNone(movement_result(before, None))
        self.assertTrue(movement_result(before, before)['position_unchanged'])
        camera_before = {'world': {'camera_eye': [1, 0, 1]}}
        camera_after = {'world': {'camera_eye': [1, 0, 4]}}
        self.assertEqual(movement_result(camera_before, camera_after),
                         {'camera_delta': [0, 0, 3], 'camera_distance': 3})

    def test_depth_grid_is_bounded_and_frame_matched(self):
        grid = {'available': True, 'source': 'original_viewport_slot0_post_depth_copy_D32',
                'presentation': 41, 'width': 16, 'height': 9,
                'encoding': 'reversed_depth_log_u8_0_far_255_near',
                'values': [[column + row for column in range(16)] for row in range(9)]}
        raw = {'schema_version': 1, 'presentation': 41,
               'player': {'available': False}, 'scene_depth_grid': grid}
        state, status = capture_telemetry({'telemetry': raw}, 41)
        self.assertEqual(status['scene_depth_grid'], 'available')
        self.assertEqual(state['scene_depth_grid']['values'][8][15], 23)
        for change, rejected in (({'presentation': 40}, 'scene_depth_grid_presentation_mismatch'),
                                 ({'encoding': 'reversed_depth_u8_0_far_255_near'}, 'invalid_scene_depth_grid_format'),
                                 ({'encoding': 'linear_meters'}, 'invalid_scene_depth_grid_format'),
                                 ({'width': 15}, 'invalid_scene_depth_grid_format'),
                                 ({'values': [[0] * 16] * 8}, 'invalid_scene_depth_grid_values'),
                                 ({'values': [[False] * 16] * 9}, 'invalid_scene_depth_grid_values'),
                                 ({'values': [[float('nan')] * 16] * 9}, 'invalid_scene_depth_grid_values'),
                                 ({'values': [[256] * 16] * 9}, 'invalid_scene_depth_grid_values')):
            with self.subTest(change=change):
                state, status = capture_telemetry({'telemetry': {**raw,
                    'scene_depth_grid': {**grid, **change}}}, 41)
                self.assertIsNone(state)
                self.assertEqual(status['scene_depth_grid'], rejected)
        unavailable = {**grid, 'available': False}
        state, status = capture_telemetry({'telemetry': {**raw, 'scene_depth_grid': unavailable}}, 41)
        self.assertIsNone(state)
        self.assertEqual(status['scene_depth_grid'], 'scene_depth_grid_unavailable')
        state, status = capture_telemetry({'telemetry': {**raw,
            'player': {'available': True, 'position': [1, 2, 3]},
            'scene_depth_grid': {**grid, 'width': 15}}}, 41)
        self.assertEqual(state['player']['position'], [1, 2, 3])
        self.assertIsNone(state['scene_depth_grid'])
        self.assertEqual(status['scene_depth_grid'], 'invalid_scene_depth_grid_format')


if __name__ == '__main__':
    unittest.main()
