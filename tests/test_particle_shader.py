"""Original-byte and schedule regressions for the direct particle frontier."""
from pathlib import Path
import sys, unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import analyze_particle_shader as particle

class ParticleShaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=(ROOT/'analysis/simpsons.pe').read_bytes()
        cls.report=particle.inspect(cls.image)

    def test_complete_schedule_includes_conditional_work(self):
        self.assertEqual(sorted(self.report['VS']['rows']),list(range(9,81)))
        self.assertEqual(sorted(self.report['PS']['rows']),list(range(2,7)))
        blocks=self.report['VS']['blocks']
        conditional=[b for b in blocks if b['predicate_condition'] is not None]
        self.assertEqual([(b['first'],b['count'],b['predicate_condition']) for b in conditional],
                         [(55,6,True),(61,2,True),(70,6,False),(76,1,False)])
        # All 15 of these slots disappeared in an EXEC-only inventory.
        self.assertEqual(sum(b['count'] for b in conditional),15)

    def test_prefix_is_eight_registers_not_four(self):
        literals=self.report['VS']['literal_registers']
        self.assertEqual(sorted(literals),list(range(248,256)))
        self.assertEqual(literals[251],[0x3E800000,0x402DF84D,0xBC127A43,0x3E22F983])
        self.assertEqual(sorted(self.report['PS']['literal_registers']),list(range(252,256)))

    def test_vertex_fetch_index_and_packed_swizzle(self):
        rows=self.report['VS']['rows']
        for slot in (11,12,13,14,15):
            self.assertEqual(rows[slot]['fields']['source_register'],1)
            self.assertEqual(rows[slot]['fields']['source_component'],2)
        self.assertEqual(rows[13]['fields']['destination_swizzle'],[2,1,0,3])
        self.assertFalse(rows[13]['fields']['runtime_declaration_patch_verified'])

    def test_pixel_color_is_four_dot2adds_not_plain_modulation(self):
        rows=self.report['PS']['rows']
        for slot,mask in ((3,1),(4,2),(5,4),(6,8)):
            self.assertEqual(rows[slot]['fields']['vector_opcode'],17)
            self.assertEqual(rows[slot]['fields']['vector_mask'],mask)
            self.assertTrue(rows[slot]['fields']['export'])

    def test_predicate_setters_are_preserved(self):
        rows=self.report['VS']['rows']
        self.assertEqual(rows[54]['fields']['scalar_opcode'],30)
        self.assertEqual(rows[63]['fields']['scalar_opcode'],29)
        self.assertEqual(rows[68]['fields']['scalar_opcode'],27)

    def test_byte_changes_rejected(self):
        for stage,(va,size,_,_) in particle.PROFILES.items():
            for offset in (0,particle.HEADERS[stage][1],size-16):
                with self.subTest(stage=stage,offset=offset):
                    damaged=bytearray(self.image);damaged[va-0x82000000+offset]^=1
                    with self.assertRaisesRegex(ValueError,'record changed'):particle.inspect(damaged)

    def test_vertex_id_uses_half_offset_then_floor(self):
        import math
        rows=self.report['VS']['rows']
        self.assertEqual(rows[9]['fields']['sources'][1]['components'],[0,0,1,2])
        self.assertEqual(self.report['VS']['literal_registers'][254][0],0x3F000000)
        for slot in particle.VERTEX_FETCHES:
            self.assertFalse(rows[slot]['raw'][1]&(1<<15)) # floor, not nearest
        for vertex in range(4096*4):self.assertEqual(math.floor((vertex+.5)*.25),vertex//4)

    def test_transcription_literals_predicates_and_coissue(self):
        source=particle.shader_source(self.image)
        self.assertNotIn('vc[251]',source)
        self.assertIn('k251.x*r0.y',source)
        self.assertIn('bool predicate=(vc[25].x>=0)',source)
        self.assertIn('bool predicate=(vc[10].z==0)',source)
        # The standalone predicated instruction64 is outside conditional EXEC.
        body=particle.issue(64,self.report['VS']['rows'][64],'VS')
        self.assertIn('if(p0)',body)
        self.assertLess(body.index('float s='),body.index('r0.w=v.w'))
        for slot in (55,70):
            self.assertIn('if('+('p0' if slot==55 else '!p0')+')',particle.issue(slot,self.report['VS']['rows'][slot],'VS'))

    def test_declaration_is_full_float_data_and_integer_color(self):
        self.assertEqual(particle.DECLARATION[0:15:3],(0,16,32,48,60))
        self.assertEqual(particle.DECLARATION[13],0x001A2086)
        damaged=bytearray(self.image);damaged[0x1580D0+52]^=8
        with self.assertRaisesRegex(ValueError,'declaration changed'):particle.inspect(damaged)

if __name__=='__main__':unittest.main()
