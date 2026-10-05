from pathlib import Path
import sys,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import analyze_projected_particle_shader as projected

class ProjectedParticleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=(ROOT/'analysis/simpsons.pe').read_bytes();cls.report=projected.inspect(cls.image)
    def test_complete_schedule_and_depth_fetches(self):
        self.assertEqual(list(self.report['rows']),list(range(2,19)))
        self.assertEqual([r['fields']['fetch_constant_index'] for r in self.report['rows'].values() if r['fetch']],[2,2,2,2,0])
        self.assertEqual(self.report['rows'][10]['fields']['vector_opcode'],6)
        self.assertTrue(self.report['rows'][8]['fields']['scalar_clamp'])
        self.assertEqual(self.report['sampler']['u'],'mirror')
        self.assertEqual(self.report['sampler']['min'],'point')
    def test_signed_fractional_offsets_and_component_selection(self):
        for slot,offset,assignment in ((2,'0.5,-0.5','r4.x=fetched.y'),(3,'-0.5,-0.5','r4.y=fetched.x'),
                                        (4,'0.5,0.5','r4.z=fetched.w'),(5,'-0.5,0.5','r4.w=fetched.z')):
            text=projected.issue(slot,self.report['rows'][slot])
            self.assertIn('float2('+offset+')/1024.0',text)
            self.assertIn(assignment,text)
            self.assertIn('.xxxx',text)
    def test_original_record_and_cpu_changes_reject(self):
        for address in (projected.ADDRESS,projected.ADDRESS+0x15C,projected.ADDRESS+projected.SIZE-1,0x82772EC4,0x82772F14):
            image=bytearray(self.image);image[address-0x82000000]^=1
            with self.subTest(address=hex(address)),self.assertRaisesRegex(ValueError,'changed'):projected.inspect(image)
    def test_simultaneous_lanes_and_scalar_dependency(self):
        source=projected.issue(10,self.report['rows'][10])
        self.assertLess(source.index('float s='),source.index('r4.xyzw=v.xyzw'))
        source=projected.issue(9,self.report['rows'][9])
        self.assertIn('k255.z-r0.x',source)
        self.assertIn('r2.w*ps',projected.issue(17,self.report['rows'][17]))
if __name__=='__main__':unittest.main()
