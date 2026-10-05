"""Regression checks for offline ALU emission; not gameplay or GPU qualification."""
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import analyze_edge_shaders as edge
import analyze_rigid_shader as rigid


def row(raw):
    return dict(raw=raw, fields=edge.alu(*raw), fetch=False)


class RigidAluEmissionTests(unittest.TestCase):
    def test_gloss_dot2add(self):
        first = rigid.issue(21, row((0xC8020000, 0x00B2B26C, 0xD10B0BFF)), 'PS')
        self.assertIn('rigidLegacyMultiply(r11.zyyy,r11.zyyy)', first)
        self.assertIn('precise float sum=products.x+products.y;', first)
        self.assertIn('precise float4 v=(sum+k255.x).xxxx;', first)
        self.assertIn('r0.y=v.y;', first)
        second = rigid.issue(42, row((0xC8040004, 0x00196F6C, 0xB107FD04)), 'PS')
        self.assertIn('rigidLegacyMultiply(r7.ywww,k253.wxxx)', second)
        self.assertIn('precise float4 v=(sum+r4.x).xxxx;', second)
        self.assertIn('r4.z=v.z;', second)

    def test_scalar_multiply_coissue(self):
        emitted = rigid.issue(23, row((0x09140501, 0x00BEBEB1, 0xB0092401)), 'PS')
        self.assertIn('rigidLegacyProduct(r1.y,r1.y)', emitted)
        # Both right-hand sides must precede either register write.
        self.assertLess(emitted.index('precise float s='), emitted.index('r1.z=v.z;'))
        self.assertIn('ps=s;', emitted)

    def test_ieee_log_and_exp(self):
        log = rigid.issue(36, row((0x40240000, 0x04B1B16C, 0xA000FD80)), 'PS')
        self.assertIn('precise float s=log2(abs(r0.x));', log)
        self.assertLess(log.index('precise float s='), log.index('r0.z=v.z;'))
        self.assertIn('r0.y=s.x;', log)
        exp = rigid.issue(38, row((0x38280005, 0x00B1C66C, 0xA105FE00)), 'PS')
        self.assertIn('precise float s=exp2(r0.x);', exp)
        self.assertIn('r0.y=s.x;', exp)

    def test_multitone_scalar_add_coissue(self):
        emitted = rigid.issue(32, row((0x00180000, 0x006CB16C, 0xA305FC00)), 'PS')
        # Hand-transcribed reference for this one retail co-issue, not a
        # snapshot of the generator or a claim of whole-shader equivalence.
        self.assertEqual(emitted, '\n'.join((
            '    { // slot32',
            '        precise float4 v=min(r5.xxxx,k252.yyyy);',
            '        precise float s=r0.x+r0.x;',
            '        r0.w=v.w;',
            '        r0.x=s.x;',
            '        ps=s;',
            '    }')))

    def test_multitone_shadow_position_cnde(self):
        # Retail VS slot24: CNDE (opcode12), not DST (opcode28).
        # Zero in literal X chooses world W; the other lanes choose world ZXY.
        instruction = row((0xC80F0007, 0x00B06CA6, 0x6CFF0004))
        self.assertEqual(instruction['fields']['vector_opcode'], 12)
        emitted = rigid.issue(24, instruction, 'VS')
        self.assertEqual(emitted, '\n'.join((
            '    { // slot24',
            '        precise float4 v=float4((k255.xyyy).x==0.0?(r0.xxxx).x:(r4.zzxy).x,(k255.xyyy).y==0.0?(r0.xxxx).y:(r4.zzxy).y,(k255.xyyy).z==0.0?(r0.xxxx).z:(r4.zzxy).z,(k255.xyyy).w==0.0?(r0.xxxx).w:(r4.zzxy).w);',
            '        r7.xyzw=v.xyzw;',
            '    }')))
        # Do not silently treat the actual DST opcode as conditional move.
        instruction['fields']['vector_opcode'] = 28
        with self.assertRaisesRegex(ValueError, 'Unqualified vector opcode'):
            rigid.issue(24, instruction, 'VS')

    def test_normalmap_split_multiply_coissue(self):
        instruction = row((0xAC270006,0x00B1C042,0xC101062E))
        self.assertEqual(rigid.issue(33,instruction,'PS'), '\n'.join((
            '    { // slot33',
            '        precise float4 v=r1.yyyy*r6.xyzz;',
            '        precise float s=rigidLegacyProduct(pc[46].x,r1.z);',
            '        r6.xyz=v.xyz;',
            '        r0.y=s.x;',
            '        ps=s;',
            '    }')))

    def test_normalmap_scalar_expressions(self):
        # Hand-decoded W/X operands, not generated snapshots. SETGT compares
        # against zero; SUB_CONST's odd opcode selects temporary r1.
        cases = (
            ('VS',(0x64280000,0x02B1C611,0xE0000101),'r1.w-r1.y'),
            ('PS',(0xA0200100,0x000000B1,0xE2000081),'sqrt(abs(r1.y))'),
            ('PS',(0x20200000,0x1900006C,0xE2000080),'(-abs(r0.x)>0?1.0:0.0)'),
            ('PS',(0xBD820401,0x00C3BE00,0xD00109FF),'k255.w-r1.x'),
            ('PS',(0xB0100000,0x18000000,0xC200002D),'pc[45].w+r0.x'),
            ('PS',(0xB0430005,0x00B0B041,0x8107FE28),'pc[40].x+r0.y'),
        )
        for stage,raw,expected in cases:
            with self.subTest(raw=raw):
                self.assertEqual(rigid.scalar(row(raw),stage),expected)
        self.assertEqual(rigid.scalar(row(cases[-1][1]),'VS'),'vc[40].x+r0.y')
        frac = rigid.issue(67,row((0xA8160007,0x00160001,0xC800002E)),'PS')
        # Relative swizzle 0x16: lane + offset modulo four = [2,2,3,3].
        self.assertIn('precise float4 v=frac(r0.zzww);',frac)

    def test_normalmap_unqualified_modifiers(self):
        for raw in ((0xAC270006,0x00B1C042,0xC101062E),
                    (0xB0100000,0x18000000,0xC200002D),
                    (0xBD820401,0x00C3BE00,0xD00109FF)):
            for word,bit in ((0,7),(1,24),(1,29),(1,30),(1,31)):
                bad=list(raw);bad[word]^=1<<bit
                with self.subTest(raw=raw,word=word,bit=bit):
                    with self.assertRaisesRegex(ValueError,'Unqualified CONST scalar'):
                        rigid.scalar(row(tuple(bad)))
        for raw in ((0x64280000,0x02B1C611,0xE0000101),
                    (0xA0200100,0x000000B1,0xE2000081),
                    (0x20200000,0x1900006C,0xE2000080)):
            bad=list(raw);bad[2]^=64
            with self.assertRaisesRegex(ValueError,'Unqualified scalar operand'):
                rigid.scalar(row(tuple(bad)))
        bad=row((0xA8160087,0x00160001,0xC800002E))
        with self.assertRaisesRegex(ValueError,'Unqualified FRAC'):
            rigid.issue(67,bad,'PS')

    def test_unknown_opcode_still_rejected(self):
        instruction = row((0xC8020000, 0x00B2B26C, 0xD10B0BFF))
        instruction['fields']['vector_opcode'] = 31
        with self.assertRaisesRegex(ValueError, 'Unqualified vector opcode'):
            rigid.issue(21, instruction, 'PS')
        instruction = row((0x40240000, 0x04B1B16C, 0xA000FD80))
        instruction['fields']['scalar_opcode'] = 51
        with self.assertRaisesRegex(ValueError, 'Unqualified scalar opcode'):
            rigid.issue(36, instruction, 'PS')


if __name__ == '__main__':
    unittest.main()
