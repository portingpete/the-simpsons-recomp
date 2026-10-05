from pathlib import Path
import sys,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import analyze_dual_particle_shader as dual

class DualParticleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=(ROOT/'analysis/simpsons.pe').read_bytes();cls.report=dual.inspect(cls.image)
    def test_complete_exact_schedule(self):
        self.assertEqual(list(self.report['rows']),list(range(2,9)))
        self.assertEqual(self.report['rows'][2]['fields']['fetch_constant_index'],1)
        self.assertEqual(self.report['rows'][2]['fields']['source_components'],[2,3,2])
        self.assertEqual(self.report['rows'][3]['fields']['fetch_constant_index'],0)
        self.assertEqual(self.report['rows'][4]['fields']['vector_opcode'],1)
        for slot,mask in ((5,1),(6,2),(7,4),(8,8)):
            self.assertEqual(self.report['rows'][slot]['fields']['vector_opcode'],17)
            self.assertEqual(self.report['rows'][slot]['fields']['vector_mask'],mask)
    def test_host_slot_mapping_avoids_packed_destination(self):
        source=dual.shader_source(self.image)
        self.assertIn('particleDualTexture:register(t3)',source)
        self.assertIn('particleDualSampler:register(s1)',source)
        self.assertIn('r1=particleDualTexture.Sample(particleDualSampler,r0.zw)',source)
        self.assertEqual(self.report['host_texture_slot'],3)
        self.assertEqual(self.report['sampler']['mip'],'point')
    def test_record_and_cpu_changes_reject(self):
        for address in (dual.ADDRESS,dual.ADDRESS+0x15C,dual.ADDRESS+dual.SIZE-1,0x82773028,0x827730B8):
            image=bytearray(self.image);image[address-0x82000000]^=1
            with self.subTest(address=hex(address)),self.assertRaisesRegex(ValueError,'changed'):dual.inspect(image)
if __name__=='__main__':unittest.main()
