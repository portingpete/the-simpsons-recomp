"""Independent setup receipts require completed process outcomes and identities."""
from pathlib import Path
import json
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import audit_mission_asset_support as audit
import export_effect_setup_evidence as evidence


class EffectSetupEvidenceTests(unittest.TestCase):
    def corpus(self, root):
        logs = root / 'logs';logs.mkdir()
        refs = {}
        for field in ('fixture', 'image', 'source'):
            path = root / (field + '.bin');path.write_bytes(field.encode())
            refs[field] = dict(path=path.name, sha256=audit.sha(path.read_bytes()))
        rows, cases = [], []
        for index in range(49):
            source = f'0x{0x82000000+index:08X}'
            rows.append(dict(kind='effect_pass', table=int(index>=25), row=index if index<25 else index-25,
                id='effect:'+str(index), source=source, effect='effect_'+str(index), technique_handle=1, pass_handle=2))
            raw = (f'AUDIT_EFFECT_SETUP row={index} source={source[2:]} name=effect_{index} techniques=1 bindings=3 '
                   f'create=passed metadata_use=passed release=passed malformed=passed draw=unproven '
                   f'shadow_camera_rasters_retained={4 if index==4 else 0}\n'
                   f'PASS independent original effect setup: row={index} checks=42; setup only\nRUNNER_EXIT_CODE=0\n').encode()
            path = logs / f'row-{index:02d}.log';path.write_bytes(raw)
            cases.append(dict(row=index, returncode=0, log=path.relative_to(root).as_posix(), log_sha256=audit.sha(raw)))
        runner = dict(schema=1, **refs, cases=cases, complete=True)
        (logs/'runner.json').write_text(json.dumps(runner))
        return dict(rows=rows), logs, runner

    def test_all_original_rows_export_setup_scope_without_draw_use(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder);matrix, logs, _ = self.corpus(root)
            report = evidence.export(matrix, logs, root)
            self.assertTrue(report['complete'])
            self.assertEqual((report['rows_passed'],report['passes_setup_tested']), (49,49))
            self.assertTrue(all(c['scope']=='setup' and 'metadata_use' in c['phases'] and 'use' not in c['phases']
                                for c in report['cases']))

    def test_failed_process_does_not_hide_later_passing_rows(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder);matrix, logs, runner = self.corpus(root)
            runner['cases'][0]['returncode']=1
            (logs/'runner.json').write_text(json.dumps(runner))
            report=evidence.export(matrix,logs,root)
            self.assertFalse(report['complete'])
            self.assertEqual(report['rows_passed'],48)
            self.assertEqual(report['gaps'][0]['status'],'runner_not_passing')
            self.assertEqual(report['cases'][-1]['row'],48)

    def test_windows_native_crlf_log_keeps_original_digest_and_parses(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder);matrix, logs, runner = self.corpus(root)
            path=logs/'row-00.log';raw=path.read_bytes().replace(b'\n',b'\r\n');path.write_bytes(raw)
            runner['cases'][0]['log_sha256']=audit.sha(raw)
            (logs/'runner.json').write_text(json.dumps(runner))
            report=evidence.export(matrix,logs,root)
            self.assertTrue(report['complete'])
            self.assertEqual(report['cases'][0]['proof']['sha256'],audit.sha(raw))

    def test_changed_proof_log_and_fixture_are_not_accepted(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder);matrix, logs, _ = self.corpus(root)
            (logs/'row-02.log').write_text('changed')
            report=evidence.export(matrix,logs,root)
            self.assertEqual(report['gaps'][0]['row'],2)
            (root/'fixture.bin').write_bytes(b'new build')
            with self.assertRaisesRegex(ValueError,'runner input changed'):
                evidence.export(matrix,logs,root)


if __name__ == '__main__':
    unittest.main()
