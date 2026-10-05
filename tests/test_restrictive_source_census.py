"""Lexical matches must retain source evidence without claiming valid bounds."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import audit_restrictive_checks as audit


class RestrictiveSourceCensusTests(unittest.TestCase):
    def test_audio_catalog_and_other_handwritten_domains_are_included(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            inputs = {
                'runtime/reader.cpp': 'need(entries <= 253, "unqualified reader count");',
                'renderer/mesh.cpp': 'need(stride == 24, "unsupported stride");',
                'audio/catalog.cpp': 'need(fileBytes <= 536870912, "unqualified source extent");',
                'common/packet.h': 'require(flags == 0, "unsupported flags");',
                'app/startup.cpp': 'need(width == 1280, "unqualified fixed size");',
            }
            for relative, source in inputs.items():
                path = root/relative;path.parent.mkdir(parents=True, exist_ok=True);path.write_text(source+'\n')
            excluded = root/'build/generated/guest.cpp';excluded.parent.mkdir(parents=True);excluded.write_text('need(size == 4, "unsupported");\n')
            manifest, guards, _ = audit.census_sources(root)
            self.assertEqual({row['path'] for row in manifest}, set(inputs))
            self.assertEqual({row['file'] for row in guards}, set(inputs))
            self.assertTrue(all(row['producer_validity']=='unestablished_by_this_scan' for row in guards))

    def test_multiline_guard_keeps_exact_expression_and_original_line(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder);(root/'runtime').mkdir();path = root/'runtime/skin_vertices.cpp'
            expression = 'need(stride > 0 &&\n     stride <= 1020, "Unqualified stride");'
            path.write_bytes(('// unsupported example, not a control-flow proof\n' + expression + '\n').encode())
            manifest, guards, lines = audit.scan_file(path, root)
            self.assertEqual((guards[0]['line'], guards[0]['end_line']), (2, 3))
            self.assertEqual(guards[0]['expression'], expression[:-1])
            self.assertEqual(guards[0]['producer_validity'], 'unestablished_by_this_scan')
            self.assertEqual(guards[0]['disposition'], 'mechanical_candidate_untriaged')
            self.assertEqual(len(guards), 1)
            self.assertEqual(lines[0]['line'], 1)
            self.assertIsNone(lines[0]['related_guard'])
            self.assertEqual(manifest['path'], 'runtime/skin_vertices.cpp')

    def test_check_text_inside_comments_strings_and_raw_literals_is_not_a_guard(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder);path = root/'state.cpp'
            path.write_text('// need(flags == 0, "unsupported");\n'
                'const char* label = "need(size == 4, unsupported)";\n'
                'const char* shader = R"hlsl(need(stride == 4, "unqualified"))hlsl";\n'
                'need(flags == 0, "unsupported flags");\n')
            _, guards, lines = audit.scan_file(path, root)
            self.assertEqual(len(guards), 1)
            self.assertEqual(guards[0]['line'], 4)
            self.assertEqual(len(lines), 4)
            self.assertTrue(all(line['status']=='mechanical_candidate' for line in lines))
            self.assertTrue(all(line['disposition']=='mechanical_candidate_untriaged' for line in lines))

    def test_shader_numeric_profiles_and_base_vertex_limits_remain_candidates(self):
        self.assertIn('geometry_index_range', audit.categories('need(baseVertex == 0, "base differs")'))
        self.assertIn('shader_or_vertex_profile', audit.categories('need(vertexShader == 0x8200CCB8, "source differs")'))
        self.assertNotIn('fixed_size_or_numeric_bound', audit.categories('need(owner, "owned input")'))


if __name__ == '__main__':
    unittest.main()
