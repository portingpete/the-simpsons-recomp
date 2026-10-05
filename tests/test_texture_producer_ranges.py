"""Original caps, serialized fields and native qualification are distinct."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import audit_texture_producer_ranges as producer


class TextureProducerRangesTests(unittest.TestCase):
    def test_real_caps_do_not_grant_itxd_lifecycle_or_all_format_credit(self):
        report=producer.inspect();caps=report['authority']['original_producers']['caps']
        self.assertEqual((caps['width'],caps['height']),(8192,8192))
        self.assertFalse(caps['ordinary_2d_power_of_two_required'])
        self.assertFalse(report['native_lifecycle_tested'])
        self.assertEqual(len(report['actionable_queue']),5)
        ordinary,volume,encoding,copied=report['established_scopes']
        self.assertEqual(ordinary['normalized_dimensions'],[1,8192])
        self.assertTrue(ordinary['compatible_format_required'])
        self.assertEqual((volume['width_height_max'],volume['depth_max']),(2048,1024))
        self.assertEqual(encoding['nonborder_representable'],[1,8192])
        self.assertIn('not proof',encoding['qualification'])
        self.assertIn('No format or dimension whitelist',copied['qualification'])
        for finding in report['actionable_queue']:
            self.assertNotIn('native_lifecycle_tested',finding)
            self.assertTrue(finding['copied_itxd_validity'])

    def test_each_original_instruction_is_pinned_after_outer_hashes(self):
        original=(ROOT/'analysis/simpsons.pe').read_bytes()
        digests={n:h for _,n,h,_ in producer.SPANS}
        for address,word,meaning in producer.INSTRUCTIONS:
            changed=bytearray(original);changed[address-producer.effects.BASE]^=1
            with patch.object(producer.audit,'sha',side_effect=lambda b:producer.effects.IMAGE_SHA
                    if len(b)==len(changed) else digests[len(b)]):
                with self.assertRaisesRegex(ValueError,'Original texture instruction changed'):
                    producer.pin_original(changed)

    def test_changed_device_cap_is_not_accepted_as_the_pinned_bound(self):
        original=bytearray((ROOT/'analysis/simpsons.pe').read_bytes())
        original[0x8206AA30+0x58-producer.effects.BASE+2]=0x10
        digests={n:h for _,n,h,_ in producer.SPANS}
        with patch.object(producer.audit,'sha',side_effect=lambda b:producer.effects.IMAGE_SHA
                if len(b)==len(original) else digests[len(b)]):
            with self.assertRaisesRegex(ValueError,'caps fields changed'):producer.pin_original(original)

    def test_stock_profile_counts_remain_observation_only(self):
        data=dict(dictionaries=[{}],textures=[dict(format='28000102',admission='admitted_metadata',width=64,height=64)])
        with patch.object(producer.audit,'load',return_value=(data,dict(path='fixture',sha256='abc'))):
            report=producer.inspect(texture_report=Path('fixture'))
        self.assertEqual(report['shipped_metadata_scope']['occurrences'],1)
        self.assertEqual(report['shipped_metadata_scope']['max_width'],64)
        self.assertEqual(report['authority']['original_producers']['caps']['width'],8192)
        self.assertFalse(report['native_lifecycle_tested'])


if __name__=='__main__':unittest.main()
