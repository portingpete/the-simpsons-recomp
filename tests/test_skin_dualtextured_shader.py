"""Exact offline shader coverage, address/predicate handling and mutation rejection."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import analyze_skin_dualtextured_shader as dual
import analyze_skin_shader as skin
from skin_shader_emit import vs_issue


class DualSkinTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = (ROOT/'analysis/simpsons.pe').read_bytes()
        cls.rows = dual.inspect(cls.image)

    def test_coverage(self):
        self.assertEqual(list(self.rows['VS']), list(range(8, 73)))
        self.assertEqual(list(self.rows['PS']), list(range(8, 71)))
        self.assertEqual([s for s, r in self.rows['VS'].items() if r['fetch']], list(range(8, 21)))
        self.assertEqual([s for s, r in self.rows['PS'].items() if r['fetch']], [8]+list(range(45, 54)))

    def test_all_twelve_bone_reads_are_indexed(self):
        # Regression for the prior transcription which indexed only one read.
        for rows in (skin.inspect(self.image)['VS']['rows'], self.rows['VS']):
            indexed = 0
            for slot, row in rows.items():
                if row['fetch']:
                    continue
                f = row['fields']
                if f['constant_0_relative']:
                    text = vs_issue(slot, row)
                    reg = f['sources'][1]['register']
                    self.assertIn(f'vc[a0+{reg}]', text)
                    self.assertNotIn(f'vc[{reg}]', text)
                    indexed += 1
            self.assertEqual(indexed, 12)

    def test_old_address_before_coissued_maxas(self):
        text = vs_issue(34, self.rows['VS'][34])
        self.assertLess(text.index('vc[a0+53]'), text.index('a0=nextAddress'))
        self.assertIn('r9.y', text)

    def test_predicate_and_split_scalar(self):
        self.assertIn('if(p0)', dual.ps_issue(34, self.rows['PS'][34]))
        self.assertIn('pc[40].z+r1.x', dual.ps_issue(23, self.rows['PS'][23]))

    def test_complete_source_emits(self):
        text = dual.shader_source(self.image)
        self.assertIn('float4 vc[256]', text)
        self.assertIn('skinBase.Sample(skinBaseSampler,r0.xy)', text)
        self.assertEqual(text.count('shadow0.Sample'), 9)
        self.assertIn('float4(input.uv,input.uv1)', text)

    def test_all_record_words_and_truncations_reject(self):
        for profile in dual.PROFILES:
            _, va, size, *_ = profile
            original = self.image[va-0x82000000:va-0x82000000+size]
            for i in range(0, size, 4):
                bad = bytearray(original)
                bad[i] ^= 1
                with self.assertRaises(ValueError):
                    dual.decode_record(bad, profile)
            with self.assertRaises(ValueError):
                dual.decode_record(original[:-1], profile)


if __name__ == '__main__':
    unittest.main()
