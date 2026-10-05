"""Exact original lifetimes cannot be inferred from generic passes or GPU teardown."""
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
import export_extended_draw_evidence as evidence


class ExtendedDrawEvidenceTests(unittest.TestCase):
    def corpus(self, root, include_mono=False, include_mono_recording=False):
        specs = dict(evidence.SPECS)
        if include_mono:
            specs.update(evidence.MONO_SPECS)
        if include_mono_recording:
            specs.update(evidence.MONO_RECORDING_SPECS)
        config = root / 'CTestTestfile.cmake';junit = root / 'results.xml'
        files = {'image.pe', 'dictionary.itxd', 'tests/effect_catalog_lifecycle_helpers.h',
                 'tests/effect_draw_cleanup_helpers.h', 'runtime/engine_effects.cpp', 'runtime/engine_driver.cpp',
                 'runtime/rigid_profile.h', 'runtime/skin_profile.h'}
        for spec in specs.values():
            files.update((spec.fixture, *spec.dependencies, 'bin/' + spec.executable + '.exe'))
        for name in files:
            path = root / name;path.parent.mkdir(parents=True, exist_ok=True);path.write_bytes(name.encode())
            os.utime(path, ns=(1_000_000_000, 1_000_000_000))
        rows, commands = {}, []
        tree = ET.Element('testsuite')
        for test, spec in specs.items():
            command = [str(root / 'bin' / (spec.executable + '.exe')), str(root / 'image.pe')]
            command += [str(root / 'dictionary.itxd') if value == '{dictionary}' else value for value in spec.arguments]
            commands.append('add_test([=[' + test + ']=] ' + ' '.join('"' + value + '"' for value in command) + ')')
            fields = dict(evidence.COMMON + spec.required + (('source', spec.source[2:]),))
            if spec.marker in ('AUDIT_RIGID_BLEND_LIFECYCLE', 'AUDIT_SKY_BLEND_LIFECYCLE', 'AUDIT_GEOMETRY_STRIP_BOUNDARY'):
                fields.update(opaque_vertex='11111111', opaque_pixel='22222222', alpha_vertex='33333333', alpha_pixel='44444444')
            techniques = (spec.selected_technique,) if spec.selected_technique else ('0x0003FFFC', '0x0007FFFC')
            for index, technique in enumerate(techniques):
                if spec.source == '0x820C0550':
                    vertex, pixel = '0x820C2FA0', '0x00000000'
                elif spec.selected_technique:
                    vertex, pixel = '0x' + fields['vertex'], '0x' + fields['pixel']
                else:
                    vertex, pixel = (('0x11111111', '0x22222222') if index == 0 else ('0x33333333', '0x44444444'))
                rows[(spec.source, technique)] = dict(id=f'fx:{spec.source}:{technique}', kind='effect_pass',
                    source=spec.source, technique_handle=technique, vertex=vertex, pixel=pixel,
                    pass_handle='0x' + fields.get('pass', '0003FFFE' if index == 0 else '0007FFFE'),
                    coverage=audit.coverage())
            node = ET.SubElement(tree, 'testcase', name=test)
            ET.SubElement(node, 'system-out').text = (spec.marker + ' ' + ' '.join(f'{k}={v}' for k, v in fields.items()) +
                                                   '\n' + spec.native_pass + ' 100 checks\n')
        config.write_text('\n'.join(commands), encoding='utf-8');os.utime(config, ns=(1_000_000_000, 1_000_000_000))
        for spec in specs.values():
            os.utime(root / 'bin' / (spec.executable + '.exe'), ns=(2_000_000_000, 2_000_000_000))
        self.write_report(tree, junit)
        return dict(rows=list(rows.values())), junit, config, tree

    def write_report(self, tree, junit):
        ET.ElementTree(tree).write(junit, encoding='utf-8');os.utime(junit, ns=(3_000_000_000, 3_000_000_000))

    def test_independent_original_lifetimes_credit_only_exact_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve();matrix, junit, config, _ = self.corpus(root)
            report = evidence.export(matrix, junit, config, root)
            self.assertTrue(report['complete'])
            self.assertEqual((len(report['cases']), len(report['synthetic_rows'])), (44, 15))
            shadow = [row for row in report['cases'] if row['test'] == 'OriginalShadowTangentPass']
            self.assertEqual(len(shadow), 2)
            self.assertIn('0x0007FFFC', shadow[0]['catalog_id'])
            self.assertTrue(all(('malformed' in row['phases']) == (row['test']=='OriginalSkyStripBoundaryPass')
                                for row in report['cases']))
            self.assertTrue(all('full GPU retirement unproven' in row['qualification'] for row in report['cases']))

    def test_backend_destructor_marker_does_not_grant_original_asset_lifetime(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve();matrix, junit, config, tree = self.corpus(root)
            tree[0][0].text = ('AUDIT_GPU_MESH_RETIREMENT family=skin create=passed draw_use=passed backend_destructor=passed\n' +
                              evidence.SPECS[tree[0].get('name')].native_pass + ' 100 checks\n')
            self.write_report(tree, junit)
            report = evidence.export(matrix, junit, config, root)
            self.assertFalse(report['complete']);self.assertEqual(len(report['synthetic_rows']), 14)

    def test_bad_pair_and_missing_release_are_not_lifecycle_receipts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve();matrix, junit, config, tree = self.corpus(root)
            tree[0][0].text = tree[0][0].text.replace('fx_release=passed', 'fx_release=unproven')
            self.write_report(tree, junit)
            report = evidence.export(matrix, junit, config, root)
            self.assertEqual(report['gaps'][0]['status'], 'missing_or_mismatched_lifecycle_marker')
            rigid = next(node for node in tree if node.get('name') == 'OriginalRigidInherited_base')
            rigid[0].text = rigid[0].text.replace('opaque_vertex=11111111', 'opaque_vertex=55555555')
            self.write_report(tree, junit)
            with self.assertRaisesRegex(ValueError, 'shader pairs'):
                evidence.export(matrix, junit, config, root)

    def test_failed_case_keeps_later_independent_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve();matrix, junit, config, tree = self.corpus(root)
            ET.SubElement(tree[0], 'failure', message='frontier')
            self.write_report(tree, junit)
            report = evidence.export(matrix, junit, config, root)
            self.assertEqual(report['gaps'], [dict(test=tree[0].get('name'), status='not_passing')])
            self.assertEqual(len(report['synthetic_rows']), 14)

    def test_source_edit_or_config_rewrite_invalidates_historical_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve();matrix, junit, config, _ = self.corpus(root)
            os.utime(config, ns=(4_000_000_000, 4_000_000_000))
            with self.assertRaisesRegex(ValueError, 'config changed'):
                evidence.export(matrix, junit, config, root)
            os.utime(config, ns=(1_000_000_000, 1_000_000_000))
            os.utime(root / 'runtime/engine_effects.cpp', ns=(4_000_000_000, 4_000_000_000))
            with self.assertRaisesRegex(ValueError, 'source/helper changed'):
                evidence.export(matrix, junit, config, root)

    def test_recording_variants_require_original_complete_lifetime_and_preserve_independence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            matrix, junit, config, tree = self.corpus(root, include_mono=True, include_mono_recording=True)
            report = evidence.export(matrix, junit, config, root, include_mono=True, include_mono_recording=True)
            self.assertTrue(report['complete'])
            self.assertEqual((len(report['cases']), len(report['synthetic_rows'])), (52, 19))
            failed = next(node for node in tree if node.get('name') == 'OriginalMonoRecording_default')
            ET.SubElement(failed, 'failure', message='later valid original frontier')
            self.write_report(tree, junit)
            report = evidence.export(matrix, junit, config, root, include_mono=True, include_mono_recording=True)
            self.assertEqual(report['gaps'], [dict(test='OriginalMonoRecording_default', status='not_passing')])
            self.assertEqual(len(report['synthetic_rows']), 18)
            self.assertEqual(len([case for case in report['cases'] if case['test'] == 'OriginalMonoRecording_bit2']), 2)

    def test_recording_entry_and_backend_only_release_are_not_original_lifetimes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            matrix, junit, config, tree = self.corpus(root, include_mono_recording=True)
            default = next(node for node in tree if node.get('name') == 'OriginalMonoRecording_default')
            default[0].text = ('AUDIT_MONO_RECORDING_FRONTIER source=8211F480 create=passed '
                               'recording_entry=encountered use=unproven release=unproven\n' +
                               evidence.MONO_RECORDING_SPECS[default.get('name')].native_pass)
            bit2 = next(node for node in tree if node.get('name') == 'OriginalMonoRecording_bit2')
            bit2[0].text = bit2[0].text.replace('payload_release=passed', 'payload_release=unproven')
            self.write_report(tree, junit)
            report = evidence.export(matrix, junit, config, root, include_mono_recording=True)
            self.assertEqual([gap['status'] for gap in report['gaps']],
                             ['missing_or_mismatched_lifecycle_marker'] * 2)
            self.assertEqual(len(report['synthetic_rows']), 15)

    def test_chunk_boundary_requires_original_packet_and_independent_winding_proof(self):
        for changed in (('selected_count=65536', 'selected_count=65535'),
                        ('original_packet_pixels=passed', 'original_packet_pixels=unproven'),
                        ('independent_winding_pixels=passed', 'independent_winding_pixels=unproven'),
                        ('actual_opaque_cull=0_2', 'actual_opaque_cull=2_6'),
                        ('actual_alpha_cull=0_2', 'actual_alpha_cull=2_6'),
                        ('original_material_cull=passed', 'original_material_cull=unproven'),
                        ('original_packet_alpha=cleared', 'original_packet_alpha=retained')):
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory).resolve();matrix, junit, config, tree = self.corpus(root)
                chunk = next(node for node in tree if node.get('name') == 'OriginalSkyStripBoundaryPass')
                chunk[0].text = chunk[0].text.replace(*changed);self.write_report(tree, junit)
                report = evidence.export(matrix, junit, config, root)
                self.assertEqual(report['gaps'], [dict(test='OriginalSkyStripBoundaryPass',status='missing_or_mismatched_lifecycle_marker')])
                self.assertEqual(len(report['synthetic_rows']), 14)


if __name__ == '__main__':
    unittest.main()
