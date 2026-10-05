"""Chocolate rigidalpha inventory only; no GPU execution or runtime admission."""
from pathlib import Path
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import analyze_chocolate_shader as choc

class ChocolateInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = (ROOT/'analysis/simpsons.pe').read_bytes()
        cls.inventory = choc.inspect(cls.image)

    def test_exact_pair_and_complete_slots(self):
        for stage,address in [('VS','0x8205e0b8'),('PS','0x8205eed4')]:
            row = self.inventory[stage]
            self.assertEqual(row['address'],address)
        # VS slots 5..46 (42), PS slots 10..58 (49).
        self.assertEqual(sorted(self.inventory['VS']['rows']),list(range(5,47)))
        self.assertEqual(sorted(self.inventory['PS']['rows']),list(range(10,59)))
        self.assertEqual(sum(r['fetch'] for r in self.inventory['VS']['rows'].values()),6)
        self.assertEqual(sum(r['fetch'] for r in self.inventory['PS']['rows'].values()),3)

    def test_control_flow_exact(self):
        for stage in ('VS','PS'):
            got = tuple((int(lo,16),int(hi,16)) for lo,hi in self.inventory[stage]['control'])
            self.assertEqual(got,choc.CF[stage])

    def test_vs_fetches_exact(self):
        rows=self.inventory['VS']['rows']
        for slot,dst in [(5,1),(6,3),(7,5),(8,4),(9,2),(10,2)]:
            f=rows[slot]['fields']
            self.assertTrue(rows[slot]['fetch'])
            self.assertEqual(f['fetch_constant_index'],95)
            self.assertEqual(f['destination_register'],dst)

    def test_ps_fetches_exact(self):
        rows=self.inventory['PS']['rows']
        for slot,(const,dst) in [(10,(1,6)),(11,(0,7)),(34,(2,6))]:
            f=rows[slot]['fields']
            self.assertTrue(rows[slot]['fetch'])
            self.assertEqual(f['fetch_constant_index'],const)
            self.assertEqual(f['destination_register'],dst)

    def test_ps_jumps_exact(self):
        jumps=[(e[0],e[1]) for e in self.inventory['PS']['control'] if int(e[1],16)>>12==11]
        self.assertEqual(len(jumps),5)

    def test_ps_exports_single(self):
        exports=[slot for slot,row in self.inventory['PS']['rows'].items() if row['fields'].get('export')]
        self.assertEqual(exports,[58])

    def test_literals_exact(self):
        import struct
        import analyze_screen_shaders as screen
        vlits=struct.unpack_from('>16I',self.image,0x8205E0B8-screen.BASE+0x298)
        self.assertEqual(tuple(vlits),choc.VS_LITERALS)
        plits=struct.unpack_from('>16I',self.image,0x8205EED4-screen.BASE+0x378)
        self.assertEqual(tuple(plits),choc.PS_LITERALS)

    def test_shader_source_structure(self):
        src=choc.shader_source(self.image)
        self.assertIn('VSChocolate(ChocInput input)',src)
        self.assertIn('PSChocolate(ChocOutput input)',src)
        self.assertNotIn('input.morph',src)
        self.assertIn('r3=float4(input.normal,0)',src)
        self.assertIn('r5=float4(input.tangent,0)',src)
        self.assertIn('if(!p0)',src)
        self.assertNotIn('bool b0',src)
        self.assertIn('sin(',src)

    def test_runtime_consumes_chocolate_uv1_and_tangent(self):
        runtime=(ROOT/'runtime/engine_effects.cpp').read_text(encoding="utf-8")
        renderer=(ROOT/'renderer/rigid_mesh.cpp').read_text(encoding="utf-8")
        self.assertIn('const bool chocolate=record.view.source==0x8205D2D8;',runtime)
        self.assertIn('const bool tangentInput=(rigidProfile(record.view.source).normalmap&&!s.alphaRigid())||chocolate;',runtime)
        self.assertIn('rigidProfile(record.view.source).multitone||rigidProfile(record.view.source).normalmap||chocolate;',runtime)
        self.assertIn('const bool tangent=rigidProfile(record.view.source).normalmap&&!s.alphaRigid();',runtime)
        self.assertIn('kVSChocolate,sizeof(kVSChocolate),&result->chocolateLayout',renderer)
        self.assertIn('if(normalmap||choc)m.requireTangentBindings(context.Get(),choc,chocAlpha)',renderer)
        self.assertIn('draw->layout=vfx?m.vfxLayout:chocOpaque?m.chocolateOpaqueLayout:chocAlpha?m.chocolateLayout:normalmap?m.tangentLayout:m.layout',renderer)
        self.assertIn('bindChocolateMeshDeclaration(mesh.native,s.alphaRigid())',runtime)
        self.assertIn('bindChocolateMeshDeclaration(payload,mesh.native,s.alphaRigid())',runtime)

    def test_export_scalar_uses_vector_export_destination(self):
        row=self.inventory['VS']['rows'][41]
        self.assertEqual(row['raw'],(0x14878004,0x00BA6C1B,0xE1030302))
        self.assertTrue(row['fields']['export'])
        self.assertEqual(row['fields']['vector_destination'],4)
        self.assertEqual(row['fields']['scalar_mask'],8)
        src=choc.issue(41,row,'VS',False)
        self.assertIn('output4.w=s;',src)
        self.assertNotIn('r0.w=',src)

    def test_zero_frame_export_keeps_ieee_rsq_and_legacy_multiply(self):
        import hashlib
        import analyze_fourtap_shaders as four
        # The installed opcode definition separates clamp-to-maximum (20)
        # from the actual IEEE reciprocal-square-root opcode (22).
        self.assertEqual(hashlib.sha256(four.UCODE.read_bytes()).hexdigest(),four.UCODE_SHA)
        reference=four.UCODE.read_text()
        self.assertIn('kRsqc = 20',reference)
        self.assertIn('kRsq = 22',reference)
        self.assertIn('multiplication (+-0 or denormal * anything = +0)',reference)
        rows=self.inventory['VS']['rows']
        self.assertEqual(rows[37]['raw'],(0x58180305,0x001B1B6C,0xE2050583))
        self.assertEqual(rows[37]['fields']['scalar_opcode'],22)
        self.assertIn('rsqrt(abs(r3.x))',choc.issue(37,rows[37],'VS',False))
        self.assertIn('rigidLegacyMultiply(r3.zwyy,r3.xxxx)',choc.issue(41,rows[41],'VS',False))

    def test_all_issued_chocolate_multiply_normalizations_keep_sm3_products(self):
        for stage in ('VS','PS'):
            for slot,row in self.inventory[stage]['rows'].items():
                if row['fetch']:continue
                f=row['fields'];src=choc.issue(slot,row,stage,False)
                if f['vector_opcode']==1 and f['vector_mask']:
                    self.assertIn('rigidLegacyMultiply(',src)
                if f['scalar_opcode'] in (3,42):
                    self.assertIn('rigidLegacyProduct(',src)

    def test_predicate_jump_is_not_a_boolean_constant_test(self):
        for lo,hi in choc.CF['PS']:
            if hi>>12==11 and not lo&0x2000:
                self.assertTrue(lo&0x4000)
                self.assertFalse(hi&0x400)
        for slot in (10,11,34):
            self.assertFalse(self.inventory['PS']['rows'][slot]['raw'][1]&0x80000000)
            self.assertNotIn('if(p0)',choc.issue(slot,self.inventory['PS']['rows'][slot],'PS',True))

    def test_sincos_coissue_reads_before_writes_and_preserves_previous_scalar(self):
        for slot in (17,18):
            src=choc.issue(slot,self.inventory['VS']['rows'][slot],'VS',False)
            self.assertLess(src.index('precise float s='),src.index('=v.'))
            self.assertIn('ps=s;',src)
        src=choc.issue(17,self.inventory['VS']['rows'][17],'VS',False)
        self.assertIn('dot(r3.zxy,vc[12].zxy)',src)

if __name__=='__main__':unittest.main()
