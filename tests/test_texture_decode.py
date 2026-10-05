"""CPU texture tests: independent inverse layouts, asymmetric blocks and originals.

All fixtures are in memory; no generated images or test files are written.
"""

from contextlib import redirect_stderr
import copy
import io
import json
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch
import zlib

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import decode_itxd as dec
import inspect_itxd

SAMPLE = ROOT / "build/extracted/frontend_global.itxd"
SOURCE = ROOT / "Simpsons Game, The (USA)/frontend/frontend_global.str"

# Independently recorded before decoder implementation, from original blocks
# using the read-only reference's address formula and byte-lane permutation.
LINEAR_HASHES = [
    "3c7f54dd56feaa68201c509f591c612440ed794e1b0e33f72273af8cde04e8dd",
    "2053b0f7e44ce71fbc32be52c27df51463c8f44b658445b6a391192fb829fa22",
    "b52e3d81429fbb7281a159e1a4cd752e721b292654e031344438b57f4d56e8f5",
    "ad57deaf9e8bb76751e24da7184516c511a9d4ac9e4ef5228307d5fd64c6a132",
    "c4b96c90b26b322bd5d1e7147ffb10274c7155efc548b8b642c3d2780ed0178d",
    "c9b44fcfcece71dd4f57909a85b4f023a68b0e5de157546f97206da09a292f0f",
    "112bbfa24d8794fcfe00ce65591fcb0842e928908001517a6d65d751d042c131",
    "96cad945340e538c87e0988285d464a9109e0bddee99c39828d37cf8be6b3a15",
    "8b2b4012c7ecfa016c7cdde0cb2dfffb16651b6fab0a8e84f7b7ea3aec048e0f",
    "6680a5e44c1e47fe3582386ff75a08350efcc0ae7029e0b2568ff0fb59fafc7e",
    "7f57b98bb347ea655b8d54e7abb0bdeb25559d354ee7152ce9d3702770448799",
    "e813007d3f906a9083d61e2e04f6687515b9a291570033847f4d58921a78bc3d",
    "540486cd0b1a5f2f7f569961fb4bd70d74cc9438e0d4c29a782848311266c34f",
    "0b00d829e16d18e02168857c6168502e947314fddd0373c139ca80ac00eae571",
    "cb64e8d43435b6f58633baff4b5f204aa4baba4e6b67ab71c75d2db212601eca",
]


def inverse_address(byte_offset, pitch, size):
    """Physical byte address -> coordinate, separate from the production direction.

    XGAddress2DTiledX/Y relationships from leeao/Noesis-Plugins,
    Textures/inc_xbox360_untile.py. This is not a forward-then-forward roundtrip.
    """
    log = size.bit_length() - 1
    intermediate = ((byte_offset & ~4095) >> 3) + ((byte_offset & 1792) >> 2) + (byte_offset & 63)
    macro = intermediate >> (7 + log)
    tile_x = (((intermediate >> (5 + log)) & 2) + (byte_offset >> 6)) & 3
    micro_x = ((((intermediate >> 1) & ~15) + (intermediate & 15)) & ((size << 3) - 1)) >> log
    x = ((macro % (pitch >> 5)) * 4 + tile_x) * 8 + micro_x
    tile_y = ((intermediate >> (6 + log)) & 1) + ((byte_offset & 2048) >> 10)
    micro_y = (((intermediate & (((size << 6) - 1) & ~31)) + ((intermediate & 15) << 1)) >> (3 + log)) & ~1
    y = ((macro // (pitch >> 5)) * 4 + tile_y) * 8 + micro_y + ((intermediate & 16) >> 4)
    return x, y


def asymmetric_fixture(width, height, pitch, size):
    """Construct physical storage via inverse coordinates, never production tiling."""
    block = lambda x, y: bytes((13 * x + 29 * y + 71 * lane + (x ^ (3 * y))) & 255 for lane in range(size))
    storage = bytearray([0xcd]) * (dec.align32(height + 64) * (pitch + 128) * size)
    seen = set()
    for offset in range(0, len(storage), size):
        x, y = inverse_address(offset, pitch, size)
        if x < width and y < height:
            if (x, y) in seen:
                raise AssertionError("inverse fixture aliases coordinates")
            seen.add((x, y))
            value = block(x, y)
            storage[offset:offset + size] = value if size == 1 else bytes(value[i ^ 1] for i in range(size))
    if len(seen) != width * height:
        raise AssertionError("inverse fixture did not cover the logical rectangle")
    return bytes(storage), b"".join(block(x, y) for y in range(height) for x in range(width))


def read_png(png):
    """Independent minimal PNG reader/checker for emitted filter-zero RGBA files."""
    if png[:8] != b"\x89PNG\r\n\x1a\n":
        raise AssertionError("bad PNG signature")
    cursor, chunks, image_data = 8, [], bytearray()
    while cursor < len(png):
        size = struct.unpack_from(">I", png, cursor)[0]
        kind, data = png[cursor + 4:cursor + 8], png[cursor + 8:cursor + 8 + size]
        crc = struct.unpack_from(">I", png, cursor + 8 + size)[0]
        if zlib.crc32(kind + data) & 0xffffffff != crc:
            raise AssertionError("bad PNG CRC")
        chunks.append(kind)
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", data)
            if (depth, color, compression, filtering, interlace) != (8, 6, 0, 0, 0):
                raise AssertionError("unexpected PNG encoding")
        elif kind == b"IDAT":
            image_data.extend(data)
        cursor += size + 12
    if chunks != [b"IHDR", b"IDAT", b"IEND"] or cursor != len(png):
        raise AssertionError("unexpected PNG chunk sequence")
    raw = zlib.decompress(image_data)
    stride = width * 4 + 1
    if len(raw) != height * stride or any(raw[y * stride] for y in range(height)):
        raise AssertionError("bad PNG scanlines")
    return width, height, b"".join(raw[y * stride + 1:(y + 1) * stride] for y in range(height))


class TextureDecodeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = SAMPLE.read_bytes()
        cls.inventory = inspect_itxd.parse_itxd(cls.data)
        if not cls.inventory["input"]["matches_verified_sample"]:
            raise AssertionError("exact original fixture required")
        cls.decoded = [dec.decode_base(cls.data, t) for t in cls.inventory["textures"]]

    def test_independent_inverse_93120_coordinates(self):
        checked = 0
        for size in (1, 8, 16):
            for pitch in (32, 64, 96, 128):
                for y in range(97):
                    for x in range(pitch):
                        self.assertEqual(inverse_address(dec.tiled_offset_2d(x, y, pitch, size), pitch, size), (x, y))
                        checked += 1
        self.assertEqual(checked, 93120)

    def test_asymmetric_physical_fixtures_all_block_sizes(self):
        for size, pitch, width, height in ((1, 96, 67, 35), (1, 128, 37, 65),
                                           (8, 96, 65, 37), (8, 64, 39, 67),
                                           (16, 96, 67, 35), (16, 128, 35, 67)):
            with self.subTest(size=size, pitch=pitch, width=width, height=height):
                storage, expected = asymmetric_fixture(width, height, pitch, size)
                actual, info = dec.untile_blocks(storage, width, height, pitch, size, "none" if size == 1 else "8in16")
                self.assertEqual(actual, expected)
                self.assertEqual(info["addressed_blocks"], width * height)
                self.assertNotEqual(expected[size:2 * size], expected[width * size:(width + 1) * size])
                with self.assertRaisesRegex(dec.DecodeError, "outside base allocation"):
                    dec.untile_blocks(storage[:info["maximum_addressed_end"] - 1], width, height, pitch,
                                      size, "none" if size == 1 else "8in16")

    def test_non_square_macro_pitch_vectors(self):
        expected = {1: (407, 121, 512, 1536, 4625),
                    8: (4536, 1624, 8192, 24576, 40984),
                    16: (9136, 5232, 16384, 49152, 81968)}
        for size in expected:
            result = tuple(dec.tiled_offset_2d(x, y, 96, size) for x, y in ((7, 9), (9, 7), (32, 0), (0, 32), (65, 33)))
            self.assertEqual(result, expected[size])

    def test_endian_applies_to_endpoints_selectors_and_alpha(self):
        for size in (8, 16):
            canonical = bytes(range(size))
            stored = bytes(canonical[i ^ 1] for i in range(size))
            linear, _ = dec.untile_blocks(stored, 1, 1, 32, size, "8in16")
            self.assertEqual(linear, canonical)
            self.assertNotEqual(linear, bytes(stored[i ^ 3] for i in range(size)))

    def test_bc1_four_color_selector_order(self):
        selectors = [0, 1, 2, 3, 3, 2, 1, 0, 1, 3, 0, 2, 2, 0, 3, 1]
        index = sum(v << (2 * i) for i, v in enumerate(selectors))
        block = struct.pack("<HHI", 0xf800, 0x001f, index)
        palette = [(255, 0, 0, 255), (0, 0, 255, 255), (170, 0, 85, 255), (85, 0, 170, 255)]
        self.assertEqual(dec.decode_bc_block(block, 18), [palette[i] for i in selectors])

    def test_bc1_three_color_and_equal_endpoints(self):
        for first, second, middle in ((0, 0xffff, (127, 127, 127, 255)),
                                      (0xffff, 0xffff, (255, 255, 255, 255))):
            pixels = dec.decode_bc_block(struct.pack("<HHI", first, second, 0xe4e4e4e4), 18)
            self.assertEqual(pixels[2], middle)
            self.assertEqual(pixels[3], (0, 0, 0, 0))

    def test_reference_endpoint_expansion_and_rounding(self):
        self.assertEqual(dec.rgb565(0x1800), (24, 0, 0))  # bit replication, not 25 from normalized rounding
        self.assertEqual(dec.rgb565(0x0020), (0, 4, 0))
        self.assertEqual(dec.rgb565(0x0003), (0, 0, 24))
        block = struct.pack("<HHI", 0x1800, 0x0800, 0xaaaaaaaa)
        self.assertEqual(dec.decode_bc_block(block, 18), [(18, 0, 0, 255)] * 16)  # +1 rule would yield 19

    def test_bc2_four_colors_even_when_endpoints_ascending(self):
        block = b"\xff" * 8 + struct.pack("<HHI", 0, 0xffff, 0xe4e4e4e4)
        self.assertEqual(dec.decode_bc_block(block, 19)[:4],
                         [(0, 0, 0, 255), (255, 255, 255, 255), (85, 85, 85, 255), (170, 170, 170, 255)])

    def test_bc2_alpha_ramp_and_hidden_rgb_preserved(self):
        alpha = sum(i << (4 * i) for i in range(16)).to_bytes(8, "little")
        block = alpha + struct.pack("<HHI", 0xffff, 0, 0)
        self.assertEqual(dec.decode_bc_block(block, 19), [(255, 255, 255, i * 17) for i in range(16)])

    def test_l8_channel_selection(self):
        self.assertEqual(dec.decode_linear(bytes([0, 19, 201, 255, 81, 33]), 3, 2, 2),
                         bytes(c for value in (0, 19, 201, 255, 81, 33) for c in (value, value, value, 255)))

    def test_asymmetric_bc_pixel_rectangle_and_edge_cropping(self):
        colors = (0xf800, 0x07e0, 0x001f, 0xffff, 0x0000, 0xffe0)
        linear = b"".join(struct.pack("<HHI", c, 0, 0) for c in colors)
        rgba = dec.decode_linear(linear, 9, 5, 18)
        expected = ((255, 0, 0), (0, 255, 0), (0, 0, 255),
                    (255, 255, 255), (0, 0, 0), (255, 255, 0))
        for y in range(5):
            for x in range(9):
                offset = (y * 9 + x) * 4
                self.assertEqual(tuple(rgba[offset:offset + 4]), (*expected[(y // 4) * 3 + x // 4], 255))

    def test_all_original_base_linear_hashes_and_bounds(self):
        for index, (rgba, meta) in enumerate(self.decoded):
            self.assertEqual(meta["linear_base_sha256"], LINEAR_HASHES[index])
            self.assertEqual(len(rgba), meta["width"] * meta["height"] * 4)
            self.assertLessEqual(meta["addressing"]["maximum_addressed_end"], meta["base_allocation_prefix_bytes"])
        self.assertEqual(sum(len(rgba) // 4 for rgba, _ in self.decoded), 306432)

    def test_original_bc1_has_no_transparent_selectors(self):
        for _, meta in self.decoded:
            if meta["format_id"] == 18:
                self.assertEqual(meta["alpha_histogram"], {"255": meta["width"] * meta["height"]})

    def test_all_original_pngs_preserve_exact_rgba(self):
        for rgba, meta in self.decoded:
            png = dec.png_rgba(meta["width"], meta["height"], rgba)
            self.assertEqual(read_png(png), (meta["width"], meta["height"], rgba))

    def test_png_asymmetric_rows_and_multiblock_deflate(self):
        for width, height in ((3, 2), (257, 65)):
            rgba = bytes((i * 71 + i // 37) & 255 for i in range(width * height * 4))
            self.assertEqual(read_png(dec.png_rgba(width, height, rgba)), (width, height, rgba))

    def test_bad_block_lengths_formats_and_layout_fields(self):
        for size in (0, 7, 9, 15, 17):
            with self.assertRaises(dec.DecodeError):
                dec.decode_bc_block(bytes(size), 18)
        for args in ((-1, 0, 32, 8), (32, 0, 32, 8), (0, 0, 31, 8), (0, 0, 32, 4), (0.5, 0, 32, 8)):
            with self.assertRaises(dec.DecodeError):
                dec.tiled_offset_2d(*args)
        for fmt in (6, 20, 49):
            with self.assertRaisesRegex(dec.DecodeError, "unsupported texture format"):
                dec.decode_linear(bytes(16), 4, 4, fmt)
        with self.assertRaisesRegex(dec.DecodeError, "endian"):
            dec.untile_blocks(bytes(16), 1, 1, 32, 16, "8in32")
        with self.assertRaisesRegex(dec.DecodeError, "byte count"):
            dec.decode_linear(bytes(15), 4, 4, 19)

    def test_uncertain_resource_layouts_are_rejected(self):
        changes = {"dimension": "cube", "tiled": False, "packed_mips": True,
                   "channel_swizzle": list("ZYXW"), "endianness": "8in32",
                   "component_signs": ["gamma"] * 4, "numeric_format_bit": 1,
                   "base_address_field_bytes": 4096, "mip_min_level": 1,
                   "base_pitch_texels": 32, "mip_address_field_bytes": 8192}
        for field, value in changes.items():
            with self.subTest(field=field):
                texture = copy.deepcopy(self.inventory["textures"][0])
                texture["descriptor"][field] = value
                with self.assertRaises(dec.DecodeError):
                    dec.decode_base(self.data, texture)
        with self.assertRaisesRegex(dec.DecodeError, "base level zero"):
            dec.decode_base(self.data, self.inventory["textures"][9], mip=1)

    def test_other_sample_hash_is_rejected_even_if_structurally_valid(self):
        altered = bytearray(self.data)
        altered[-1] ^= 1
        with self.assertRaisesRegex(dec.DecodeError, "exact verified ITXD SHA256"):
            dec.build_outputs(altered, SAMPLE)

    def test_manifest_original_provenance_and_determinism(self):
        a = dec.build_outputs(self.data, SAMPLE, SOURCE)
        b = dec.build_outputs(self.data, SAMPLE, SOURCE)
        self.assertEqual(a, b)
        m = json.loads(a["manifest.json"])
        self.assertEqual(len(a), 17)
        self.assertEqual(m["summary"]["decoded_base_levels"], 15)
        self.assertTrue(m["original_resource"]["verified_byte_identical"])
        for t in m["textures"]:
            self.assertEqual(dec.digest(a[t["png"]]), t["png_sha256"])
            self.assertEqual(dec.digest(read_png(a[t["png"]])[2]), t["rgba_sha256"])
        self.assertEqual(dec.digest(a["inspection-sheet.png"]), m["inspection_preview"]["png_sha256"])

    def test_inspection_sheet_is_separate_and_explicitly_derived(self):
        rgba = bytes((240, 20, 10, 255, 255, 255, 255, 0))
        frames = [(f"{i}.png", 2, 1, rgba) for i in range(15)]
        png, info = dec.inspection_sheet(frames)
        w, h, image = read_png(png)
        self.assertEqual((w, h), (1360, 816))
        self.assertIn("not a source texture", info["purpose"])
        start = (8 * w + 8) * 4
        self.assertEqual(image[start:start + 4], bytes((240, 20, 10, 255)))
        transparent = (8 * w + 8 + 128) * 4
        self.assertEqual(image[transparent:transparent + 4], bytes((112, 112, 112, 255)))
        self.assertEqual(frames[0][3], rgba)

    def test_output_scope_guard(self):
        for path in (ROOT, SAMPLE.parent, ROOT / "analysis", ROOT / "build/decoded-textures/../../tools"):
            with self.assertRaisesRegex(dec.DecodeError, "stay under"):
                dec.checked_output_dir(path)

    def test_unsupported_mip_cli_fails_before_publish(self):
        with redirect_stderr(io.StringIO()), patch.object(dec, "publish_outputs") as publish:
            result = dec.main(["--input", str(SAMPLE), "--mip", "1"])
        self.assertEqual(result, 2)
        publish.assert_not_called()


if __name__ == "__main__":
    unittest.main()
