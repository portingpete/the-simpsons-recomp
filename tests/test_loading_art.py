import hashlib
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import extract_loading_art as art


def block(a, b, selectors):
    packed = sum(v << (i*3) for i, v in enumerate(selectors))
    # Ascending RGB endpoints must still use four colors in BC3.
    return bytes([a, b]) + packed.to_bytes(6, 'little') + struct.pack('<HHI', 0, 0xffff, 0xeeeeeeee)


class LoadingArtTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = (ROOT/'analysis/simpsons.pe').read_bytes()
        cls.stream = cls.image[art.STREAM_OFFSET:art.STREAM_OFFSET+art.STREAM_SIZE]

    def test_bc3_eight_alpha_values(self):
        pixels = art.bc3_block(block(200, 30, list(range(8))*2))
        self.assertEqual([p[3] for p in pixels[:8]], [200, 30, 175, 151, 127, 102, 78, 54])
        self.assertEqual([p[3] for p in pixels[8:]], [200, 30, 175, 151, 127, 102, 78, 54])
        self.assertEqual(pixels[0][:3], (85, 85, 85))
        self.assertEqual(pixels[1][:3], (170, 170, 170))

    def test_bc3_six_alpha_and_implicit_extremes(self):
        pixels = art.bc3_block(block(10, 210, list(range(8))*2))
        self.assertEqual([p[3] for p in pixels[:8]], [10, 210, 50, 90, 130, 170, 0, 255])
        equal = art.bc3_block(block(80, 80, [6, 7]*8))
        self.assertEqual([p[3] for p in equal], [0, 255]*8)
        self.assertNotEqual(equal[0][:3], (0, 0, 0))  # Transparent RGB is preserved.
        for size in (0, 8, 15, 17):
            with self.assertRaises(ValueError):
                art.bc3_block(bytes(size))

    def test_serialized_linear_rows_and_endian(self):
        # Different first/last corners and a non-tile-boundary block, formed
        # directly in serialized block order, distinguish linear from tiling.
        data = bytearray(block(255, 0, [0]*16)*4096)
        for index, alpha in ((0, 40), (63, 70), (64, 100), (4095, 130)):
            data[index*16:index*16+16] = block(alpha, 0, [0]*16)
        storage = bytes(data[i ^ 1] for i in range(len(data)))
        rgba, linear, _ = art.decode_texture(dict(width=256, height=256, payload=storage))
        self.assertEqual(linear, data)
        for x, y, alpha in ((0, 0, 40), (252, 0, 70), (0, 4, 100), (255, 255, 130), (4, 0, 255)):
            self.assertEqual(rgba[(y*256+x)*4+3], alpha)

    def test_original_chunk_profile_and_byte_order(self):
        self.assertEqual(hashlib.sha256(self.stream).hexdigest(), art.STREAM_HASH)
        textures = art.parse_dictionary(self.stream)
        self.assertEqual([t['name'] for t in textures], ['frame2', 'frame1'])
        self.assertEqual([len(t['payload']) for t in textures], [65536, 65536])
        bad = bytearray(self.stream)
        size_offset = textures[0]['header_offset'] + 88
        struct.pack_into('>I', bad, size_offset, 65536)
        with self.assertRaises(ValueError):
            art.parse_dictionary(bad)
        for data in (self.stream[:-1], self.stream+b'\0', bytes(12), self.stream[:30]):
            with self.assertRaises(ValueError):
                art.parse_dictionary(data)

    def test_original_identity_and_reproduction(self):
        first = art.artifacts(self.image)
        self.assertEqual(first, art.artifacts(self.image))
        for name, data in first.items():
            self.assertEqual(data, (ROOT/'build/loading-art'/name).read_bytes())
        corrupted = bytearray(self.image)
        corrupted[0x1000] ^= 1
        with self.assertRaises(ValueError):
            art.artifacts(corrupted)


if __name__ == '__main__':
    unittest.main()
