"""Original caller validity must not be inflated into native alpha coverage."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import audit_shader_producer_ranges as producer


class ShaderProducerRangesTests(unittest.TestCase):
    def test_real_original_low_byte_partitions_and_burp_literal(self):
        report=producer.inspect()
        finding=report['findings'][0]
        self.assertEqual(finding['source'],'0x8211F480')
        self.assertFalse(finding['current_route']['alpha_reachable'])
        self.assertFalse(finding['draw_lifecycle_tested'])
        self.assertEqual(finding['representative_inputs'],[0,1,255,256,257,0xffffffff])
        self.assertEqual([s['typed_offset'] for s in finding['selections']],['0xAC','0xA8'])
        self.assertEqual([s['callback_lr'] for s in finding['original_callers']],['82740134','8274047C'])
        whole=finding['qualified_whole_public_producer']
        self.assertEqual(whole['immediate_flags'],[0,1])
        self.assertEqual(whole['recording_flags'],[0])
        self.assertEqual(whole['recording_metadata'],[0,4])
        self.assertEqual(whole['recording_original_caller'],'82740A60')

    def test_each_selector_instruction_rejected_independently_of_outer_image_hash(self):
        original=(ROOT/'analysis/simpsons.pe').read_bytes()
        # Span/image digests are already proved in the positive case. Bypass
        # only these outer digests to exercise each exact instruction guard.
        for address, word, meaning in producer.INSTRUCTIONS:
            changed=bytearray(original)
            changed[address-0x82000000]^=1
            with patch.object(producer.audit,'sha',side_effect=lambda b: producer.effects.IMAGE_SHA if len(b)==len(changed)
                else next((digest for a,n,digest,_ in producer.SPANS if len(b)==n), 'unknown')):
                with self.assertRaisesRegex(ValueError,'Original producer instruction changed'):
                    producer.pin_producers(changed)

    def test_burp_literal_mutation_does_not_turn_the_existing_route_alpha(self):
        original=(ROOT/'analysis/simpsons.pe').read_bytes()
        changed=bytearray(original)
        changed[0x740B47]=1
        with patch.object(producer.audit,'sha',side_effect=lambda b: producer.effects.IMAGE_SHA if len(b)==len(changed)
            else next((digest for a,n,digest,_ in producer.SPANS if len(b)==n), 'unknown')):
            with self.assertRaisesRegex(ValueError,'Burp callback literal flag changed'):
                producer.pin_producers(changed)


if __name__=='__main__':
    unittest.main()
