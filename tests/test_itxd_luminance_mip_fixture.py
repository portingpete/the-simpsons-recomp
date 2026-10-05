"""Independent source, mip-range and coordinate checks for the L8 oracle."""
import sys

sys.dont_write_bytecode = True

from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import prepare_itxd_luminance_mip_fixture as fixture


class LuminanceMipFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original, cls.metadata = fixture.original_case()

    def test_original_three_level_provenance_and_pixels(self):
        original = self.original
        self.assertEqual(original[3], fixture.BEAM_DESCRIPTOR)
        self.assertEqual(len(original[4]), 12288)
        self.assertEqual([len(level) for level in original[5]], [16384, 4096, 1024])
        self.assertEqual(tuple(fixture.digest(level) for level in original[5]), fixture.BEAM_LEVEL_HASHES)
        self.assertEqual(original[6]['provenance']['entry']['index'], 13)
        self.assertEqual(original[6]['provenance']['resource']['name'], 'spr_hub_split9.itxd')
        self.assertEqual(original[6]['layout'], [(0, 4096, 64, 0, 0), (4096, 4096, 32, 0, 0),
                                              (8192, 4096, 32, 16, 0)])
        for level in original[5]:
            for at in range(0, len(level), 4):
                self.assertEqual(level[at:at + 3], bytes((level[at],)) * 3)
                self.assertEqual(level[at + 3], 255)

    def test_packed_tail_mutation_changes_only_one_level_and_three_rgb_lanes(self):
        storage = self.original[4]
        changed = bytearray(storage)
        physical = next(offset for offset in range(4096)
                        if fixture.inverse_address(offset, 32, 1) == (16, 0))
        changed[8192 + physical] ^= 0x5A
        levels = fixture.inverse_levels(changed, 64, 64, fixture.BEAM_LAYOUT)
        self.assertEqual(levels[:2], self.original[5][:2])
        differences = [at for at, (before, after) in enumerate(zip(self.original[5][2], levels[2]))
                       if before != after]
        self.assertEqual(differences, [0, 1, 2])
        self.assertEqual(levels[2][:3], bytes(value ^ 0x5A for value in self.original[5][2][:3]))
        self.assertEqual(self.original[4], storage)

    def test_oracle_rejects_missing_allocation_and_incomplete_rectangle(self):
        storage = self.original[4]
        with self.assertRaises(AssertionError):
            fixture.inverse_levels(storage[:-1], 64, 64, fixture.BEAM_LAYOUT)
        with self.assertRaises(AssertionError):
            fixture.inverse_levels(storage + b'\0', 64, 64, fixture.BEAM_LAYOUT)
        shortened = [*fixture.BEAM_LAYOUT[:2], (8192, 4095, 32, 16, 0)]
        with self.assertRaises(AssertionError):
            fixture.inverse_levels(storage, 64, 64, shortened)
        wrong_origin = [*fixture.BEAM_LAYOUT[:2], (8192, 4096, 32, 4096, 0)]
        with self.assertRaises(AssertionError):
            fixture.inverse_levels(storage, 64, 64, wrong_origin)

    def test_binary_family_covers_full_rectangular_tails_and_exact_extents(self):
        cases = [self.original, *fixture.synthetic_cases()]
        self.assertEqual(len(cases), 8)
        self.assertEqual(sum(len(case[5]) for case in cases), 70)
        self.assertEqual({(case[1], case[2]) for case in cases},
                         {(64, 64), (128, 64), (64, 128), (512, 128), (128, 512),
                          (2048, 64), (64, 2048)})
        encoded = fixture.family_bytes(cases)
        self.assertEqual(encoded[:8], b'L8M1' + struct.pack('<I', 8))
        at = 8
        for _, width, height, descriptor, storage, levels, details in cases:
            self.assertEqual(struct.unpack_from('<9I', encoded, at), (width, height, len(storage), *descriptor))
            at += 36
            self.assertEqual(encoded[at:at + len(storage)], storage)
            at += len(storage)
            for level_index, level in enumerate(levels):
                self.assertEqual(len(level), max(1, width >> level_index) * max(1, height >> level_index) * 4)
                self.assertEqual(encoded[at:at + len(level)], level)
                at += len(level)
            layout = details['layout']
            packed = min(width.bit_length(), height.bit_length()) - 5
            for index, (start, length, _, _, _) in enumerate(layout):
                self.assertEqual(start % 4096, 0)
                self.assertEqual(length % 4096, 0)
                if index > packed:
                    self.assertEqual(layout[index][:3], layout[packed][:3])
            if details.get('synthetic'):
                self.assertEqual(len(levels), max(width, height).bit_length())
        self.assertEqual(at, len(encoded))


if __name__ == '__main__':
    unittest.main()
