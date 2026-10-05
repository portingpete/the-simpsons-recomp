"""Source-qualified .prt field parsing; never native lifetime credit."""
import copy
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import audit_packaged_particle_fields as audit


def payload(count=5001):
    data=bytearray(800)
    data[28]=2
    struct.pack_into('>I',data,48,108)
    struct.pack_into('>I',data,76,132)
    for at in (108,132):struct.pack_into('>3I',data,at,0x2E707274,15,0)
    struct.pack_into('>3I',data,120,0,1000,0) # Bad original owner reference first.
    struct.pack_into('>3I',data,144,2,56,456)
    definition=200
    struct.pack_into('>h',data,definition+0xDA,count)
    struct.pack_into('>I',data,definition+0xD0,0x18001080)
    data[definition+0x100]=5
    data[definition+0x104]=4
    data[definition+0x40]=1
    return data


class ParticleFieldTests(unittest.TestCase):
    def test_bad_reference_preserves_later_owned_requested_fields(self):
        data=payload()
        before=bytes(data)
        result=audit.particle_fields(data)
        self.assertEqual(result['modules'][0]['status'],'offline_qualification_failure')
        row=result['modules'][1]
        self.assertEqual((row['definition_offset'],row['parameter_offset']),(200,600))
        self.assertEqual((row['authored_count_signed'],row['requested_blocks_of8'],row['requested_rounded_capacity']),(5001,626,5008))
        self.assertEqual(row['flagsD0_after_original_loader'],'18000080')
        self.assertTrue(row['exceeds_native4096_requested_domain'])
        self.assertIsNone(row['live_capacity'])
        self.assertIsNone(row['shader_selection'])
        self.assertFalse(row['native_lifecycle_tested'])
        self.assertEqual(bytes(data),before)

    def test_original_signed_clamp_is_distinct_from_live_capacity(self):
        for value in (-32768,-1,0,1,7,8,9,32767):
            row=audit.particle_fields(payload(value))['modules'][1]
            self.assertEqual(row['loader_normalized_requested_count'],max(value,1))
            self.assertEqual(row['requested_rounded_capacity'],((max(value,1)+7)//8)*8)
            self.assertIsNone(row['live_capacity'])
        data=payload()
        struct.pack_into('>I',data,136,16)
        self.assertEqual(audit.particle_fields(data)['modules'][1]['reason'],'Unqualified .prt module revision')

    def test_cached_named_metadata_failure_does_not_poison_valid_fields(self):
        data=payload()
        entry=dict(encoding='raw',file_offset=0,stored_size=len(data),decoded_size=len(data))
        chunk=dict(payload_decoded_offset=0,payload_size=len(data),payload_sha256=audit.fields.sha(data))
        bad=copy.deepcopy(chunk);bad['payload_sha256']='0'*64
        cache=dict(chunks={})
        with self.assertRaises(ValueError):audit.named_fields(data,entry,bad,cache,None)
        _,parsed,_=audit.named_fields(data,entry,chunk,cache,None)
        self.assertEqual(parsed['modules'][1]['requested_rounded_capacity'],5008)

    def test_full_original_span_and_module_table_mutations_reject(self):
        image=(audit.ROOT/'analysis/simpsons.pe').read_bytes()
        audit.verify(image)
        for start,(end,_) in audit.SPANS.items():
            for address in (int(start,16),end-1):
                data=bytearray(image);data[address-audit.fields.BASE]^=1
                with self.assertRaisesRegex(ValueError,start):audit.verify(data,False)
        data=bytearray(image);data[0x82152FBC-audit.fields.BASE]^=1
        with self.assertRaisesRegex(ValueError,'module table'):audit.verify(data,False)


if __name__=='__main__':unittest.main()
