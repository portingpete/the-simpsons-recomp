"""Exact original sky opaque/alpha instruction and complete pass evidence."""
from pathlib import Path
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import analyze_sky_shader as sky

class SkyInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = (ROOT/'analysis/simpsons.pe').read_bytes()
        cls.inventory = sky.inspect(cls.image)

    def test_exact_pair_and_complete_slots(self):
        for stage,address in [('VS','0x82036f08'),('PS','0x820374e8')]:
            row = self.inventory[stage]
            self.assertEqual(row['address'],address)
        # VS slots 3..13 (11), PS slots 2..16 (15).
        self.assertEqual(sorted(self.inventory['VS']['rows']),list(range(3,14)))
        self.assertEqual(sorted(self.inventory['PS']['rows']),list(range(2,17)))
        self.assertEqual(sum(r['fetch'] for r in self.inventory['VS']['rows'].values()),2)
        self.assertEqual(sum(r['fetch'] for r in self.inventory['PS']['rows'].values()),4)

    def test_control_flow_exact(self):
        for stage in ('VS','PS'):
            got = tuple((int(lo,16),int(hi,16)) for lo,hi in self.inventory[stage]['control'])
            self.assertEqual(got,sky.CF[stage])

    def test_vs_fetches_exact(self):
        rows=self.inventory['VS']['rows']
        for slot,dst in [(3,2),(4,0)]:
            f=rows[slot]['fields']
            self.assertTrue(rows[slot]['fetch'])
            self.assertEqual(f['fetch_constant_index'],95)
            self.assertEqual(f['destination_register'],dst)

    def test_ps_fetches_exact(self):
        rows=self.inventory['PS']['rows']
        for slot,(const,dst) in [(6,(3,0)),(9,(0,0)),(10,(1,3)),(11,(2,1))]:
            f=rows[slot]['fields']
            self.assertTrue(rows[slot]['fetch'])
            self.assertEqual(f['fetch_constant_index'],const)
            self.assertEqual(f['destination_register'],dst)

    def test_ps_exports_single(self):
        exports=[slot for slot,row in self.inventory['PS']['rows'].items() if row['fields'].get('export')]
        self.assertEqual(exports,[16])

    def test_literals_exact(self):
        import struct
        import analyze_screen_shaders as screen
        plits=struct.unpack_from('>16I',self.image,0x820374E8-screen.BASE+0x1DC)
        self.assertEqual(tuple(plits),sky.PS_LITERALS)

    def test_shader_source_structure(self):
        src=sky.shader_source(self.image)
        self.assertIn('VSSky(SkyInput input)',src)
        self.assertIn('PSSky(SkyOutput input)',src)
        self.assertIn('skyTex3.Sample(skySampler3,r3.xy)',src)
        self.assertIn('output0.xyzw=v.xyzw;',src)

    def test_zero_write_mask_retains_pixel_kill(self):
        import copy
        row = self.inventory['PS']['rows'][8]
        self.assertEqual(row['fields']['vector_opcode'],25)
        self.assertEqual(row['fields']['vector_mask'],0)
        self.assertIn('if (any(k254.zzzz > r0.zzzz)) discard;',sky.ps_issue(8,row))
        changed = copy.deepcopy(row)
        changed['fields']['vector_opcode'] = 24
        with self.assertRaises(ValueError):
            sky.ps_issue(8,changed)

    def test_opaque_exact_records_and_complete_semantic_equivalence(self):
        opaque=sky.inspect(self.image,opaque=True)
        self.assertEqual(opaque['VS']['address'],'0x82036c2c')
        self.assertEqual(opaque['PS']['address'],'0x820371ec')
        for stage in ('VS','PS'):
            self.assertEqual(opaque[stage]['header'],self.inventory[stage]['header'])
            self.assertEqual(opaque[stage]['control'],self.inventory[stage]['control'])
            self.assertEqual(opaque[stage]['rows'],self.inventory[stage]['rows'])
            self.assertNotEqual(opaque[stage]['trailer'],self.inventory[stage]['trailer'])
        source=sky.shader_source(self.image)
        self.assertIn('VSSkyOpaque(SkyInput input)',source)
        self.assertIn('PSSkyOpaque(SkyOutput input)',source)

    def test_whole_original_selected_maps_states_and_selector(self):
        proof=sky.whole_pass_proof(self.image)
        self.assertEqual(proof['contexts'],['2480','27B0'])
        self.assertEqual(proof['textures'],[20,21,22,24])
        self.assertEqual(proof['unused_texture_leaf'],23)
        self.assertEqual(proof['material_vertex_rows'],[47,46])
        self.assertEqual((proof['depth_enable'],proof['depth_write']),(1,0))

    def test_mutated_opaque_record_map_and_selector_are_rejected(self):
        for address,inspect in ((0x82036C2C,lambda b:sky.inspect(b,opaque=True)),
                                (0x820371EC,lambda b:sky.inspect(b,opaque=True)),
                                (0x82036454+0x2560+16*24,sky.whole_pass_proof),
                                (0x823CA754,sky.whole_pass_proof)):
            changed=bytearray(self.image);changed[address-0x82000000]^=1
            with self.assertRaises(ValueError):inspect(changed)

if __name__=='__main__':unittest.main()
