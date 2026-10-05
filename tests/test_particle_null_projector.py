"""Retail instruction proof for requested projection with a null projector.

82772CA8 retains r10==0 at its null-projector branch and selects the ordinary
or dual PS. The inherited c21..25 affect only the unconsumed TEX1 export.
This executes hash-pinned original instructions, never the native HLSL or
runtime classifier. The C++ original-dispatch fixture separately tests owners,
selected records and the complete retail CPU particle upload.
"""
from pathlib import Path
import math
import struct
import unittest

import test_dual_projected_particle_shader as original

ROOT = Path(__file__).resolve().parents[1]
# Literal PPC evidence: initial r10=0, projection-request bit, live projector
# load/null branch, and separate ordinary/dual fallback shader choices.
PINS = {
    0x82772D18: 0x3BA00000,
    0x82772DD0: 0x7FAAEB78,
    0x82772DD8: 0x896B0104,
    0x82772DDC: 0x556B077A,
    0x82772DF8: 0x817B0178,
    0x82772DFC: 0x2B0B0000,
    0x82772E00: 0x419A0194,
    0x82772F94: 0xD3E10060,
    0x82772FB0: 0x2B0B0001,
    0x82772FCC: 0x808B25DC,
    0x82772FE8: 0x808B25C4,
    0x82773008: 0x808B25E8,
    0x82773014: 0x808B25D0,
}


def qualify_branch(image):
    for address, expected in PINS.items():
        if struct.unpack_from('>I', image, address - 0x82000000)[0] != expected:
            raise ValueError('Original null-projector branch changed')
    return original.reports(image)


def packed(values):
    return struct.pack('<' + 'f' * len(values), *values)


def consumed_vertex(values):
    # SV_POSITION, TEX0 (both texture coordinates), TEX2 (particle tint).
    return packed(values[:8] + values[12:16])


class OriginalNullProjectorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = (ROOT / 'analysis/simpsons.pe').read_bytes()
        cls.vs, cls.ps = qualify_branch(cls.image)

    def test_original_branch_and_negative_pins(self):
        for address in PINS:
            image = bytearray(self.image)
            image[address - 0x82000000] ^= 1
            with self.subTest(address=hex(address)), self.assertRaisesRegex(ValueError, 'changed'):
                qualify_branch(image)

    def test_both_vertex_programs_keep_all_consumed_exports_exact(self):
        matrix_shapes = (
            [[0.0] * 4 for _ in range(4)],
            [[float(row == lane) for lane in range(4)] for row in range(4)],
            [[(row * 4 + lane - 7) * .125 for lane in range(4)] for row in range(4)],
        )
        comparisons = 0
        dead_export_changes = [0, 0]
        for stage, inputs, constants, expected in original.vertex_cases(self.vs):
            baseline = consumed_vertex(expected)
            for shape in matrix_shapes:
                for ambient in (-1.0, 0.0, .4, 2.0):
                    inherited = [row[:] for row in constants]
                    inherited[21:25] = [row[:] for row in shape]
                    inherited[25] = [ambient, 0.0, 0.0, 0.0]
                    actual = original.execute(self.vs[stage], inputs, inherited)
                    self.assertEqual(consumed_vertex(actual), baseline)
                    dead_export_changes[stage] += packed(actual[8:12]) != packed(expected[8:12])
                    comparisons += 1
        self.assertEqual(comparisons, 768)
        self.assertTrue(all(n > 0 for n in dead_export_changes))

    def test_unprojected_pixel_programs_ignore_inherited_projection(self):
        for stage, expected in ((0, [0]), (1, [1, 0])):
            fetches = [row['fields']['fetch_constant_index']
                       for row in self.ps[stage]['rows'].values() if row['fetch']]
            self.assertEqual(fetches, expected)
        poisons = ([math.nan] * 4,
                   [math.inf, -math.inf, 65536.0, -65536.0],
                   [-2.0, 3.0, -4.0, 5.0])
        comparisons = 0
        for stage, mask, inputs, expected in original.pixel_cases(self.ps):
            if stage >= 2:
                continue
            for poison in poisons:
                inherited = [row[:] for row in inputs]
                inherited[1] = list(poison)
                actual = original.execute(self.ps[stage], inherited, shadow_mask=mask)
                self.assertTrue(all(math.isfinite(x) for x in actual))
                self.assertEqual(packed(actual), packed(expected))
                comparisons += 1
        self.assertEqual(comparisons, 2304)

    def test_projected_programs_actually_consume_the_same_channel(self):
        changes = [0, 0]
        for stage, mask, inputs, expected in original.pixel_cases(self.ps):
            if stage < 2:
                continue
            changed = [row[:] for row in inputs]
            changed[1] = [-2.0, 3.0, -4.0, 5.0]
            actual = original.execute(self.ps[stage], changed, shadow_mask=mask)
            changes[stage - 2] += packed(actual) != packed(expected)
        self.assertTrue(all(n > 0 for n in changes))


if __name__ == '__main__':
    unittest.main()
