"""Original size arithmetic and source identity are not native lifetime evidence."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import analyze_particle_owner_contract as audit


class ParticleOwnerContractTests(unittest.TestCase):
    def test_shared_and_embedded_owner_are_distinct_original_branches(self):
        shared=audit.requested_owner(5001,626)
        embedded=audit.requested_owner(5001,625)
        self.assertFalse(shared['embedded'])
        self.assertEqual((shared['requested_blocks'],shared['callback_size'],shared['allocator_size']),(626,2808,2816))
        self.assertTrue(embedded['embedded'])
        self.assertEqual(embedded['allocator_size'],2816+544*626)
        self.assertTrue(embedded['fits_generic_allocation_size_branch'])
        self.assertFalse(embedded['native_create_use_release_tested'])

    def test_signed_encoding_rounding_does_not_grant_live_range(self):
        self.assertEqual(audit.requested_owner(-1,0)['requested_rounded_capacity'],8)
        self.assertEqual(audit.requested_owner(4097,0)['requested_rounded_capacity'],4104)
        largest=audit.requested_owner(32767,0)
        self.assertEqual(largest['requested_rounded_capacity'],32768)
        self.assertFalse(largest['signed_live_capacity_representable'])
        for count,available in ((32768,0),(-32769,0),(1,-1)):
            with self.assertRaises(ValueError):audit.requested_owner(count,available)

    def test_new_original_owner_helpers_reject_byte_mutation(self):
        image=(audit.ROOT/'analysis/simpsons.pe').read_bytes()
        audit.verify(image)
        for start,(end,_) in audit.EXTRA_SPANS.items():
            for address in (int(start,16),end-1):
                bad=bytearray(image);bad[address-audit.particles.fields.BASE]^=1
                with self.assertRaisesRegex(ValueError,start):audit.verify(bad,False)


if __name__=='__main__':unittest.main()
