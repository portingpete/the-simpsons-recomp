"""Pinned original record and native transcription checks for particle type 5."""
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import analyze_particle_type5_shader as type5


class ParticleType5ShaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=(ROOT/'analysis/simpsons.pe').read_bytes()
        cls.report=type5.inspect(cls.image)

    def test_shader_pair_and_full_vertex_schedule(self):
        self.assertEqual(self.report['address'],0x821578E0)
        self.assertEqual(self.report['pixel_address'],0x82156770)
        self.assertEqual(sorted(self.report['rows']),list(range(9,89)))
        self.assertEqual([i for i,r in self.report['rows'].items() if r['fetch']],
                         [11,12,13,14,15])
        self.assertEqual([(b['first'],b['count']) for b in self.report['blocks']
                          if b['predicate_condition'] is not None],[(68,6),(74,2)])

    def test_type5_geometry_instructions_and_exports(self):
        rows=self.report['rows']
        self.assertEqual(rows[64]['fields']['vector_opcode'],13)  # CNDGTEv
        self.assertEqual(rows[76]['fields']['vector_opcode'],5)   # SETGTv
        self.assertEqual(rows[82]['fields']['scalar_opcode'],7)   # SETEs
        self.assertEqual([rows[i]['fields']['vector_destination'] for i in (81,85,86,88)],
                         [62,2,1,0])
        source=type5.shader_source(self.image)
        self.assertIn('ParticleOutput VSParticleType5(ParticleInput input)',source)
        self.assertIn('r2.xyzw=input.uvTime.yxzw',source)
        self.assertIn('>=0?',source)
        self.assertIn('==0?1.0:0.0',source)

    def test_record_mutation_rejected(self):
        for offset in (0,type5.HEADER[1],type5.SIZE-16):
            with self.subTest(offset=offset):
                damaged=bytearray(self.image)
                damaged[type5.ADDRESS-0x82000000+offset]^=1
                with self.assertRaisesRegex(ValueError,'record changed'):
                    type5.inspect(damaged)


if __name__=='__main__':unittest.main()
