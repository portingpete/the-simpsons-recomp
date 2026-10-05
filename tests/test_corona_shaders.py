from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import analyze_corona_shaders as corona

class CoronaEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.image=(ROOT/'analysis/simpsons.pe').read_bytes()
    def test_original_inventory(self):
        report=corona.inspect(self.image)
        self.assertEqual(report['loops'],[17,17])
        self.assertEqual(report['declaration_stride'],36)
    def test_complete_record_identity(self):
        for va,size,_ in corona.PROFILES.values():
            for offset in (0,32,size//2,size-1):
                changed=bytearray(self.image);changed[va-corona.screen.BASE+offset]^=1
                with self.assertRaises(ValueError):corona.inspect(changed)
    def test_loop_defaults_and_declaration(self):
        for va in (0x82153460+0x10C,0x82153460+0x110,0x82153E0C+28):
            changed=bytearray(self.image);changed[va-corona.screen.BASE]^=1
            with self.assertRaises(ValueError):corona.inspect(changed)

if __name__=='__main__':unittest.main()
