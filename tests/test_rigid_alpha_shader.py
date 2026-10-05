"""Exact original identities, issue coverage and rejection; GPU tests are separate."""
from pathlib import Path
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import analyze_rigid_alpha_shader as alpha


class RigidAlphaInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = (ROOT / 'analysis/simpsons.pe').read_bytes()
        cls.inventory = alpha.inspect(cls.image)

    def test_complete_original_instructions(self):
        self.assertEqual(list(self.inventory['VS']['rows']), list(range(3, 20)))
        self.assertEqual(list(self.inventory['PS']['rows']), list(range(2, 15)))
        self.assertEqual([i for i, r in self.inventory['PS']['rows'].items() if r['fetch']], [2])
        self.assertEqual(self.inventory['VS']['address'], '8200D734')
        self.assertEqual(self.inventory['PS']['address'], '8200E1BC')

    def test_mutated_originals_reject(self):
        for offset in (0xCCB8, 0xD734, 0xD734 + 424, 0xE1BC, 0xE1BC + 468, 0xE1BC + 532):
            with self.subTest(offset=hex(offset)):
                changed = bytearray(self.image)
                changed[offset] ^= 1
                with self.assertRaises(Exception):
                    alpha.inspect(changed)

    def test_every_consumed_alu_emits(self):
        for slot, row in self.inventory['PS']['rows'].items():
            self.assertIn('slot' + str(slot), alpha.ps_issue(slot, row))
        source = alpha.shader_source(self.image)
        self.assertEqual(source.count('aTex0.Sample'), 1)
        self.assertNotIn('if(p0)', source)
        self.assertIn('rigidLegacyMultiply(r0.xxxx,r2.xyyz)', source)
        self.assertIn('rigidLegacyMultiply(r1.yzww,r1.xxxx)', source)


if __name__ == '__main__':
    unittest.main()
