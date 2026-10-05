"""Opaque skin inventory only; no GPU execution or runtime admission."""
from pathlib import Path
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import analyze_skin_shader as skin

class SkinInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = (ROOT/'analysis/simpsons.pe').read_bytes()
        cls.inventory = skin.inspect(cls.image)

    def test_exact_pair_and_complete_slots(self):
        for stage,address in [('VS','0x82007c1c'),('PS','0x8200a02c')]:
            row = self.inventory[stage]
            self.assertEqual(row['address'],address)
        # VS slots 7..65 (59), PS slots 4..37 (34).
        self.assertEqual(sorted(self.inventory['VS']['rows']),list(range(7,66)))
        self.assertEqual(sorted(self.inventory['PS']['rows']),list(range(4,38)))
        self.assertEqual(sum(r['fetch'] for r in self.inventory['VS']['rows'].values()),12)
        self.assertEqual(sum(r['fetch'] for r in self.inventory['PS']['rows'].values()),0)

    def test_control_flow_exact(self):
        self.assertEqual(tuple(self.inventory['VS']['control']),skin.CF['VS'])
        self.assertEqual(tuple(self.inventory['PS']['control']),skin.CF['PS'])

    def test_vs_fetches_exact(self):
        rows=self.inventory['VS']['rows']
        expected={7:(95,5),8:(95,8),9:(95,9),10:(95,4),11:(95,1),12:(95,11),
                  13:(95,10),14:(95,3),15:(95,2),16:(95,0),17:(95,6),18:(95,7)}
        for slot,(const,dst) in expected.items():
            f=rows[slot]['fields']
            self.assertTrue(rows[slot]['fetch'])
            self.assertEqual(f['fetch_constant_index'],const)
            self.assertEqual(f['destination_register'],dst)

    def test_ps_has_no_fetches(self):
        for slot,row in self.inventory['PS']['rows'].items():
            self.assertFalse(row['fetch'])

    def test_vs_jump_single(self):
        jumps=[e for e in self.inventory['VS']['control'] if (e[1]>>12)==11]
        self.assertEqual(len(jumps),1)

    def test_ps_is_straight_line(self):
        ops=[hi>>12 for lo,hi in self.inventory['PS']['control']]
        self.assertNotIn(11,ops)

    def test_literals_exact(self):
        import struct
        import analyze_screen_shaders as screen
        vlits=struct.unpack_from('>16I',self.image,0x82007C1C-screen.BASE+3736)
        self.assertEqual(tuple(vlits[12:]),(0x00000000,0x3F800000,0x3F000000,0x40400000))
        plits=struct.unpack_from('>16I',self.image,0x8200A02C-screen.BASE+604)
        self.assertEqual(tuple(plits),skin.PS_LITERALS)

    def test_shader_source_structure(self):
        src=skin.shader_source(self.image)
        self.assertIn('VSSkin(SkinInput input)',src)
        self.assertIn('PSSkin(SkinOutput input)',src)
        self.assertIn('int a0=0;',src)
        self.assertIn('vc[a0+53]',src)

if __name__=='__main__':unittest.main()
