"""Native captures and command delivery never substitute for gameplay outcomes."""
from pathlib import Path
import json
import sys
import tempfile
import unittest

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import export_stage_route_evidence as evidence


class StageRouteEvidenceTests(unittest.TestCase):
    def capture(self,folder,**changes):
        row=dict(width=2,height=3,format='R10G10B10A2_UNORM_LE',capture_source='completed_front_renderer_readback',
            front_copy_completed=True,display_accepted=True,skin=3,telemetry=dict(player=dict(available=True,
                source='original_frame',position=[1,2,3])))
        row.update(changes)
        path=folder/'native-frame-5.json';path.write_text(json.dumps(row));path.with_suffix('.rgb10a2').write_bytes(b'\0'*24)
        return path

    def test_native_readback_pins_exact_pixels_and_keeps_semantic_scope_unproved(self):
        with tempfile.TemporaryDirectory() as folder:
            path=self.capture(Path(folder));report=evidence.read_capture(path)
            self.assertTrue(report['renderer_front_completed'])
            self.assertEqual(report['cumulative_draws'],dict(skin=3))
            self.assertEqual(report['player_position'],[1,2,3])
            self.assertEqual(report['pixels']['bytes'],24)

    def test_unrelated_diagnostic_json_is_not_a_capture(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'recording.json';path.write_text('{}')
            with self.assertRaisesRegex(ValueError,'Not a native frame'):
                evidence.read_capture(path)

    def test_truncated_raw_or_wrong_format_fails_capture_verification(self):
        with tempfile.TemporaryDirectory() as folder:
            path=self.capture(Path(folder));path.with_suffix('.rgb10a2').write_bytes(b'\0')
            with self.assertRaisesRegex(ValueError,'raw renderer readback'):
                evidence.read_capture(path)
            path=self.capture(Path(folder),format='unknown')
            with self.assertRaisesRegex(ValueError,'raw renderer readback'):
                evidence.read_capture(path)

    def test_delivery_receipt_cannot_be_reused_and_map_boundary_has_no_asset_credit(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);run=root/'loc-1';run.mkdir();(run/'captures').mkdir()
            (run/'game.log').write_text('[STAGE AUDIT] original map initialized stage=loc ready\n'
                'local command tap buttons=1000 hold_ms=350; left=(0,0) rt=0; delivered\n')
            (run/'launch.json').write_text('{}')
            (run/'resources.jsonl').write_text(json.dumps(dict(schema=1,event='lifecycle',kind='map',asset='loc',
                caller=0,parameters='ready',ownership='live',mission='loc',last_action='launch',sequence=1)))
            action=dict(action=dict(name='jump'),delivered=True,receipt=[4096,350,0,0,0])
            summary=root/'summary.json';summary.write_text(json.dumps(dict(schema=1,results=[dict(stage='loc',run=str(run),
                actions=[action,action],success=True)])))
            report=evidence.export(summary,['loc','brt'])
            self.assertEqual(report['summary']['verified_input_deliveries'],1)
            self.assertEqual(report['summary']['missing_stages'],['brt'])
            self.assertEqual(report['stages'][0]['resources']['event_types'],dict(lifecycle=1))
            self.assertIn('ability_hit',report['stages'][0]['semantic_outcomes_unproved'])


if __name__=='__main__':
    unittest.main()
