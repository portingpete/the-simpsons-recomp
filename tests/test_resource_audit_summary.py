"""Exact provenance grouping preserves failures and exposes noisy inputs."""
from pathlib import Path
import json
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import summarize_resource_audit as summary


def event(sequence=1, **values):
    row = dict(schema=1, event='encounter', sequence=sequence, kind='texture_binding',
        asset='guest-header:00100000', caller=0x8270BD8C, parameters='header=00100000',
        ownership='ITXD-owner-unvalidated', mission='loc', last_action='attack', instance='object=00001000')
    row.update(values)
    return row


class ResourceAuditSummaryTests(unittest.TestCase):
    def report(self, root, rows):
        path = root/'resources.jsonl';path.write_text('\n'.join(json.dumps(row) if not isinstance(row,str) else row for row in rows))
        return summary.summarize([path])

    def test_full_context_groups_repeat_and_instance_is_retained_without_becoming_signature(self):
        with tempfile.TemporaryDirectory() as folder:
            report = self.report(Path(folder), [event(), event(2, instance='object=00002000'),
                event(3, parameters='header=00100001'), event(4, last_action='checkpoint_reload')])
            self.assertEqual(report['summary']['signature_groups'], 3)
            group = next(g for g in report['groups'].values() if g['count']==2)
            self.assertEqual(group['first']['instance'], 'object=00001000')
            self.assertEqual(next(iter(group['log_line_spans'].values()))['count'], 2)
            self.assertEqual(report['kinds']['texture_binding']['field_cardinality'][0]['distinct'], 2)

    def test_window_close_remains_raw_context_but_is_not_unsupported_asset_evidence(self):
        with tempfile.TemporaryDirectory() as folder:
            report = self.report(Path(folder), [event(event='failure', reason='Native window closed'),
                event(2, event='failure', reason='Native runtime shutdown'),
                event(3, event='failure', reason='Original shader mismatch')])
            self.assertEqual(report['summary']['failure_groups'], 1)
            self.assertEqual(report['summary']['failed_occurrences'], 3)
            self.assertEqual(report['summary']['terminal_context_occurrences'], 2)
            self.assertEqual(report['failure_reasons']['Original shader mismatch'], 1)
            self.assertEqual(len(next(iter(report['failures'].values()))['reasons']), 3)

    def test_bad_line_does_not_hide_later_context_and_digest_covers_snapshot(self):
        with tempfile.TemporaryDirectory() as folder:
            report = self.report(Path(folder), ['bad json', event()])
            self.assertEqual(report['summary']['events'], 1)
            self.assertEqual(report['rejected'][0]['line'], 1)
            self.assertEqual(len(report['inputs'][0]['sha256_prefix']), 64)
            self.assertGreater(report['inputs'][0]['bytes_read'], 0)

    def test_explicit_shutdown_event_keeps_context_without_failure_credit(self):
        with tempfile.TemporaryDirectory() as folder:
            report = self.report(Path(folder), [event(event='shutdown', reason='Native runtime shutdown')])
            self.assertEqual(report['event_types']['shutdown'], 1)
            self.assertEqual(report['summary']['failed_occurrences'], 0)
            self.assertFalse(report['failures'])

    def test_movie_lifecycle_keeps_each_original_boundary_without_failure_credit(self):
        with tempfile.TemporaryDirectory() as folder:
            report = self.report(Path(folder), [event(event='lifecycle', kind='movie-lifetime',
                asset='movies/intro.bik', caller=0x828291E0, parameters='phase=input-ready'),
                event(2,event='lifecycle', kind='movie-lifetime', asset='movies/intro.bik',
                    caller=0x8282D998, parameters='phase=decoder-stop-request')])
            self.assertEqual(report['event_types']['lifecycle'], 2)
            self.assertEqual(report['kinds']['movie-lifetime']['unique_signatures'], 2)
            self.assertEqual(report['summary']['failed_occurrences'], 0)
            self.assertFalse(report['failures'])


if __name__ == '__main__':
    unittest.main()
