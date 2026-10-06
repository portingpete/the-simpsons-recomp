"""Check PC text against all shipped copies and original lookup identities."""
import hashlib
import struct
import unittest
from build_native_pc_text import (ROOT, TEXT_PACKAGES, PACKAGE_SHA, LH2_SHA,
                                 REPLACEMENTS, build, patch_text, text_slots)
from inspect_assets import stoc_entries, decode_entry, resource_chunks


def payload(data):
    entry = stoc_entries(data)['entries'][0]
    decoded, _ = decode_entry(data, entry)
    chunk = resource_chunks(decoded)[0]
    at, size = chunk['payload_decoded_offset'], chunk['payload_size']
    return decoded[at:at+size]


class NativePCTextTests(unittest.TestCase):
    def test_all_retail_copies_and_unrelated_text_preserved(self):
        natives = set()
        for package in TEXT_PACKAGES:
            with self.subTest(package=package):
                source = ROOT/'Simpsons Game, The (USA)'/package
                before = source.read_bytes()
                self.assertEqual(hashlib.sha256(before).hexdigest(), PACKAGE_SHA)
                original = payload(before)
                self.assertEqual(hashlib.sha256(original).hexdigest(), LH2_SHA)
                native = build(source)
                pc = payload(native)
                natives.add(native)
                self.assertEqual(len(pc), len(original))
                pool = 32+struct.unpack_from('>I', original, 16)[0]*8
                self.assertEqual(pc[:pool], original[:pool])
                changes = 0
                for old, new in zip(text_slots(original), text_slots(pc)):
                    self.assertEqual(old[:2], new[:2])
                    expected = old[2]
                    for platform, host in REPLACEMENTS:
                        expected = expected.replace(platform, host)
                    self.assertEqual(new[2], expected)
                    changes += old[2] != new[2]
                self.assertEqual(changes, 14)
                self.assertNotIn(b'Xbox 360', pc)
                self.assertIn(b"Loading profile. Please don't turn off your PC.\0", pc)
                self.assertIn(b"Saving profile. Please don't turn off your PC.\0", pc)
                self.assertIn(b'Xbox LIVE', pc)
                self.assertIn(b'Xbox 720', pc)
                self.assertEqual(source.read_bytes(), before)
                self.assertEqual(stoc_entries(native)['entries'][0]['encoding'], 'raw')
        self.assertEqual(len(natives), 1)

    def test_unknown_retail_text_rejected(self):
        original = payload((ROOT/'Simpsons Game, The (USA)'/TEXT_PACKAGES[0]).read_bytes())
        changed = bytearray(original)
        changed[-2] ^= 1
        with self.assertRaisesRegex(ValueError, 'identity'):
            patch_text(changed)

    def test_malformed_lookup_and_string_slots_rejected(self):
        original = payload((ROOT/'Simpsons Game, The (USA)'/TEXT_PACKAGES[0]).read_bytes())
        count = struct.unpack_from('>I', original, 16)[0]
        mutations = ((4, 1), (16, 0), (32, 0xffffffff),
                     (32+count*4, 0), (32+(count*2-1)*4, len(original)))
        for at, value in mutations:
            with self.subTest(offset=at):
                changed = bytearray(original)
                struct.pack_into('>I', changed, at, value)
                with self.assertRaises(ValueError):
                    text_slots(changed)
        changed = bytearray(original)
        start, end, _ = text_slots(original)[0]
        changed[start:end] = b'x'*(end-start)
        with self.assertRaisesRegex(ValueError, 'termination'):
            text_slots(changed)


if __name__ == '__main__':
    unittest.main()
