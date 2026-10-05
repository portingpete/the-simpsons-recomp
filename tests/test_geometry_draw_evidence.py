"""Native lifetime evidence cannot be inferred from a loading/draw PASS alone."""
from pathlib import Path
import os
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import audit_mission_asset_support as audit
import export_geometry_draw_evidence as evidence


class GeometryDrawEvidenceTests(unittest.TestCase):
    def corpus(self, root):
        root = root.resolve()
        (root / 'tests').mkdir()
        (root / 'bin').mkdir()
        paths = {}
        for name in ('image.pe', 'dictionary.itxd', 'tests/test_skin_pass.cpp', 'tests/test_sky_pass.cpp',
                     'tests/effect_catalog_lifecycle_helpers.h', 'bin/SkinPassTests.exe', 'bin/OriginalSkyPassTests.exe'):
            path = root / name;path.write_bytes(name.encode());paths[name] = path
            os.utime(path, ns=(1_000_000_000, 1_000_000_000))
        config = root / 'CTestTestfile.cmake'
        junit = root / 'results.xml'
        tree = ET.Element('testsuite')
        commands, rows = [], []
        for test, (source, family, stride, delta, fixture, executable, _) in evidence.SPECS.items():
            argv = [str(root / 'bin' / (executable + '.exe')), str(paths['image.pe'])]
            if family != 'sky':
                argv += [str(paths['dictionary.itxd']), family]
            argv += ['padded']
            commands.append('add_test([=[' + test + ']=] ' + ' '.join('"' + a + '"' for a in argv) + ')')
            node = ET.SubElement(tree, 'testcase', name=test)
            output = ET.SubElement(node, 'system-out')
            scope = 'original_screen_cache' if family == 'sky' else 'original_bones_streams_screen_cache'
            marker = f'PASS original skin {family}:' if family != 'sky' else 'PASS original sky:'
            output.text = (f'AUDIT_GEOMETRY_LIFECYCLE source={source[2:]} stride={stride} delta={delta} '
                'create=passed use=passed declaration_release=passed fx_release=passed cpu_cache_release=passed '
                f'malformed=passed malformed_scope={scope} stale_use=passed '
                'backend_mesh_cache=owner_resident full_gpu_retirement=unproven\n' + marker + ' 42 checks\n')
            for i in range(2):
                rows.append(dict(id=f'effect:{source}:{i}', kind='effect_pass', source=source,
                                 asset_group=None, coverage=audit.coverage()))
        config.write_text('\n'.join(commands), encoding='utf-8')
        os.utime(config, ns=(1_000_000_000, 1_000_000_000))
        for name in ('bin/SkinPassTests.exe', 'bin/OriginalSkyPassTests.exe'):
            os.utime(paths[name], ns=(2_000_000_000, 2_000_000_000))
        ET.ElementTree(tree).write(junit, encoding='utf-8')
        os.utime(junit, ns=(3_000_000_000, 3_000_000_000))
        return dict(rows=rows), junit, config, tree

    def rewrite_report(self, tree, junit):
        ET.ElementTree(tree).write(junit, encoding='utf-8')
        os.utime(junit, ns=(3_000_000_000, 3_000_000_000))

    def test_passing_native_lifetime_marker_exports_scoped_original_owners(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve();matrix, junit, config, _ = self.corpus(root)
            report = evidence.export(matrix, junit, config, root)
            self.assertTrue(report['complete'])
            self.assertEqual((len(report['cases']), len(report['synthetic_rows'])), (12, 4))
            audit.add_synthetic_rows(matrix['rows'], report['synthetic_rows'])
            audit.join_test_evidence(matrix['rows'], report['cases'], root)
            self.assertTrue(all(r['coverage']['lifecycle_tested'] for r in matrix['rows']))
            self.assertTrue(all(not r['coverage']['encountered_gameplay'] for r in matrix['rows']))
            self.assertTrue(all(r['synthetic'] and r['asset_group'] is None for r in report['synthetic_rows']))
            self.assertTrue(all('full GPU retirement unproven' in c['qualification'] for c in report['cases']))

    def test_pass_without_release_marker_is_not_lifecycle_evidence(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve();matrix, junit, config, tree = self.corpus(root)
            tree[0][0].text = 'PASS original skin base: 42 checks\n'
            self.rewrite_report(tree, junit)
            report = evidence.export(matrix, junit, config, root)
            self.assertFalse(report['complete'])
            self.assertEqual(len(report['synthetic_rows']), 3)
            self.assertEqual(report['gaps'][0]['status'], 'missing_or_mismatched_lifecycle_marker')

    def test_failed_case_and_wrong_command_keep_independent_later_results(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve();matrix, junit, config, tree = self.corpus(root)
            ET.SubElement(tree[0], 'failure', message='native failure')
            self.rewrite_report(tree, junit)
            raw = config.read_text();line = raw.splitlines()[1]
            config.write_text(raw.replace(line, line.replace('"padded"', '"canonical"')))
            os.utime(config, ns=(1_000_000_000, 1_000_000_000))
            report = evidence.export(matrix, junit, config, root)
            self.assertEqual([g['status'] for g in report['gaps']], ['not_passing', 'command_mismatch'])
            self.assertEqual(len(report['synthetic_rows']), 2)

    def test_mismatched_source_and_stride_marker_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve();matrix, junit, config, tree = self.corpus(root)
            tree[0][0].text = tree[0][0].text.replace('stride=80', 'stride=48')
            tree[1][0].text = tree[1][0].text.replace('source=8200FB98', 'source=82006348')
            self.rewrite_report(tree, junit)
            report = evidence.export(matrix, junit, config, root)
            self.assertEqual(len(report['gaps']), 2)
            self.assertEqual(len(report['synthetic_rows']), 2)

    def test_source_or_helper_changed_since_build_is_not_accepted(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve();matrix, junit, config, _ = self.corpus(root)
            path = root / 'tests/test_skin_pass.cpp'
            os.utime(path, ns=(4_000_000_000, 4_000_000_000))
            with self.assertRaisesRegex(ValueError, 'fixture changed'):
                evidence.export(matrix, junit, config, root)
            os.utime(path, ns=(1_000_000_000, 1_000_000_000))
            os.utime(root / 'tests/effect_catalog_lifecycle_helpers.h', ns=(4_000_000_000, 4_000_000_000))
            with self.assertRaisesRegex(ValueError, 'helper changed'):
                evidence.export(matrix, junit, config, root)


if __name__ == '__main__':
    unittest.main()
