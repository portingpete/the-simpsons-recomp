"""168F8 rigidalpha inventory only; no GPU execution or runtime admission."""
from pathlib import Path
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import analyze_168f8alpha_shader as a168

class A168InventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = (ROOT/'analysis/simpsons.pe').read_bytes()
        cls.inventory = a168.inspect(cls.image)

    def test_exact_pair_and_complete_slots(self):
        for stage,address in [('VS','0x8201739c'),('PS','0x82017e4c')]:
            row = self.inventory[stage]
            self.assertEqual(row['address'],address)
        # VS slots 3..19 (17), PS slots 3..19 (17).
        self.assertEqual(sorted(self.inventory['VS']['rows']),list(range(3,20)))
        self.assertEqual(sorted(self.inventory['PS']['rows']),list(range(3,20)))
        self.assertEqual(sum(r['fetch'] for r in self.inventory['VS']['rows'].values()),4)
        self.assertEqual(sum(r['fetch'] for r in self.inventory['PS']['rows'].values()),1)

    def test_control_flow_exact(self):
        for stage in ('VS','PS'):
            got = tuple((int(lo,16),int(hi,16)) for lo,hi in self.inventory[stage]['control'])
            self.assertEqual(got,a168.CF[stage])

    def test_vs_fetches_exact(self):
        rows=self.inventory['VS']['rows']
        for slot,dst in [(3,1),(4,0),(5,2),(6,3)]:
            f=rows[slot]['fields']
            self.assertTrue(rows[slot]['fetch'])
            self.assertEqual(f['fetch_constant_index'],95)
            self.assertEqual(f['destination_register'],dst)

    def test_ps_fetch_exact(self):
        rows=self.inventory['PS']['rows']
        f=rows[4]['fields']
        self.assertTrue(rows[4]['fetch'])
        self.assertEqual(f['fetch_constant_index'],0)
        self.assertEqual(f['destination_register'],4)
        self.assertEqual(f['destination_swizzle'],[3,0,1,2])

    def test_dual_alpha_reuses_identical_executable(self):
        import struct
        def instructions(address):
            at=address-0x82000000
            header=struct.unpack_from('>9I',self.image,at)
            prefix,size=struct.unpack_from('>2I',self.image,at+header[6])
            self.assertEqual(size,0xFC)
            return self.image[at+header[1]+prefix:at+header[1]+prefix+size-12]
        self.assertEqual(instructions(0x8201739C),instructions(0x8202B884))
        self.assertEqual(instructions(0x82017E4C),instructions(0x8202C3BC))

    def test_ps_jump_exact(self):
        jumps=[(e[0],e[1]) for e in self.inventory['PS']['control'] if int(e[1],16)>>12==11]
        self.assertEqual(len(jumps),1)

    def test_ps_exports_pair(self):
        exports=[slot for slot,row in self.inventory['PS']['rows'].items() if row['fields'].get('export')]
        self.assertEqual(exports,[18,19])

    def test_vs_exports_all(self):
        exports=[slot for slot,row in self.inventory['VS']['rows'].items() if row['fields'].get('export')]
        self.assertEqual(exports,list(range(7,20)))

    def test_literals_exact(self):
        import struct
        import analyze_screen_shaders as screen
        plits=struct.unpack_from('>16I',self.image,0x82017E4C-screen.BASE+0x214)
        self.assertEqual(tuple(plits),a168.PS_LITERALS)

    def test_shader_source_structure(self):
        src=a168.shader_source(self.image)
        self.assertIn('VS168F8(A168Input input)',src)
        self.assertIn('PS168F8(A168Output input)',src)
        self.assertNotIn('bool b0',src)
        self.assertIn('[flatten] if(p0)',src)
        self.assertIn('-k255.z-(-r4.x)',src)
        self.assertIn('if(any(k255.yyyy>r0.xxxx)) discard;',src)

    def test_fetch_serialize_does_not_predicate_sample(self):
        # Decode the actual fetch bits and CF jump mode independently of the
        # emission path: serialize is not a per-instruction predicate.
        a,b,c=self.inventory['PS']['rows'][4]['raw']
        self.assertEqual((b>>31,c>>31),(0,0))
        lo,hi=a168.CF['PS'][1]
        self.assertEqual(((lo>>14)&1,(hi>>10)&1),(1,0))
        source=a168.shader_source(self.image)
        sample=source.split('{ // slot4',1)[1].split('{ // slot5',1)[0]
        self.assertNotIn('if(',sample)
        self.assertIn('aTex0.Sample',sample)

if __name__=='__main__':unittest.main()
