"""Offline multitone inventory regressions; no rendering qualification."""
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import analyze_rigid_multitone_shader as multitone


class MultitoneInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = (ROOT/'analysis/simpsons.pe').read_bytes()
        cls.inventory = multitone.inspect(cls.image)

    def test_complete_issue_coverage(self):
        for stage, first, stop in (('VS', 7, 48), ('PS', 13, 104)):
            with self.subTest(stage=stage):
                self.assertEqual(list(self.inventory[stage]['rows']), list(range(first, stop)))
        conditional = [slot for slot, row in self.inventory['VS']['rows'].items()
                       if row['conditional_exec']]
        self.assertEqual(conditional, [39])
        self.assertEqual(self.inventory['VS']['rows'][39]['control_index'], 7)

    def test_every_executable_issue_emits(self):
        # subTest visits every slot even if several unsupported operations exist.
        for stage in ('VS', 'PS'):
            for slot, row in self.inventory[stage]['rows'].items():
                if stage == 'VS' and row['fetch']:
                    continue
                with self.subTest(stage=stage, slot=slot):
                    text = multitone.issue(slot, row, stage)
                    self.assertIn('// slot%d' % slot, text)

    def test_predicated_vertex_coissues(self):
        for slot, condition in ((35, '!p0'), (36, 'p0'), (39, 'p0')):
            with self.subTest(slot=slot):
                text = multitone.issue(slot, self.inventory['VS']['rows'][slot], 'VS')
                self.assertTrue(text.startswith('    [branch] if(%s)\n' % condition))
        text = '\n'.join(multitone.emit_control(self.inventory['VS']['rows'], 'VS'))
        self.assertIn('} else { // CF8 -> CF10', text)
        self.assertLess(text.index('// slot39'), text.index('} else {'))
        self.assertLess(text.index('} else {'), text.index('// slot40'))
        self.assertEqual(text.count('{'), text.count('}'))

    def test_material_fetch_swizzles_and_preserved_lanes(self):
        base = multitone.issue(14, self.inventory['PS']['rows'][14], 'PS')
        self.assertIn('rigidBase.Sample(rigidBaseSampler,r0.xy)', base)
        for assignment in ('r5.x=value.y;', 'r5.y=value.x;', 'r5.w=value.z;'):
            self.assertIn(assignment, base)
        self.assertNotIn('r5.z=', base)
        noise = multitone.issue(17, self.inventory['PS']['rows'][17], 'PS')
        self.assertIn('rigidNoise.Sample(rigidNoiseSampler,r4.yx)', noise)
        self.assertNotIn('r4.w=', noise)

    def test_complete_source_interface(self):
        source = multitone.shader_source(self.image)
        self.assertIn('float4 vc[47]', source)
        self.assertIn('rigidNoise : register(t3)', source)
        self.assertIn('float2 noiseUV:TEXCOORD4', source)
        self.assertIn('r0=float4(0,input.normal)', source)
        self.assertIn('VSRigidMultitone(', source)
        self.assertIn('PSRigidMultitone(', source)
        self.assertNotIn('pc[251]', source)
        self.assertEqual(source.count('{'), source.count('}'))

    def test_private_material_registers(self):
        rows = self.inventory['maps']['private']
        self.assertEqual(len(rows), 23)
        self.assertEqual(rows[14], (0x48001C, 0, 49, 0))
        self.assertEqual(rows[20], (0x600028, 0, 47, 0))
        self.assertEqual(rows[21], (0x64002A, 46, 46, 0))
        self.assertEqual(rows[22], (0x68002C, 0, 45, 0))
        # The declared palette leaf is unused in the selected opaque context.
        self.assertEqual(rows[18], (0, 0, 0, 0))
        self.assertEqual(rows[17], (0x540022, 0, 0x800000, 0))
        self.assertEqual(rows[19], (0x5C0026, 0, 0xC00000, 0))
        self.assertEqual(len(self.inventory['maps']['shared']), 11)

    def test_every_control_word_mutation_rejected_without_hash_gate(self):
        for stage, va, _, start, length, _ in multitone.PROFILES:
            offset = va-multitone.screen.BASE+start
            code = self.image[offset:offset+length]
            for at in range(0, len(multitone.CF[stage])//2*12, 4):
                with self.subTest(stage=stage, offset=at):
                    changed = bytearray(code)
                    changed[at] ^= 1
                    with self.assertRaisesRegex(ValueError, 'control flow'):
                        multitone.decode_code(changed, stage)
            for cut in (1, 12):
                with self.subTest(stage=stage, cut=cut):
                    with self.assertRaisesRegex(ValueError, 'extent'):
                        multitone.decode_code(code[:-cut], stage)

    def test_record_and_mapping_corruption_rejected(self):
        for offset in (0x54F2C, 0x54F2C+704+39*12,
                       0x55710+1204+80*12, 0x547F4+0x2E20+21*16):
            with self.subTest(offset=offset):
                changed = bytearray(self.image)
                changed[offset] ^= 1
                with self.assertRaisesRegex(ValueError, 'effect bytes'):
                    multitone.inspect(changed)
        with self.assertRaises(ValueError):
            multitone.inspect(self.image[:0x57E07])


if __name__ == '__main__':
    unittest.main()
