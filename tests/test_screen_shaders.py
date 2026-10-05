"""Exact shader proofs and rejecting boundary tests; all mutations stay in RAM."""
from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import analyze_screen_shaders as shader


class FieldTests(unittest.TestCase):
    def test_asymmetric_fetch_selectors(self):
        self.assertEqual(shader.destination_swizzle(0xB08), [0, 1, 4, 5])
        self.assertEqual(shader.destination_swizzle(0xFC8), [0, 1, 7, 7])
        self.assertEqual(shader.destination_swizzle(0x688), [0, 1, 2, 3])

    def test_relative_not_absolute_alu_swizzles(self):
        self.assertEqual(shader.relative_swizzle(0), [0, 1, 2, 3])
        self.assertEqual(shader.relative_swizzle(0xB0), [0, 1, 1, 1])
        self.assertEqual(shader.relative_swizzle(0x1B), [3, 3, 3, 3])

    def test_reject_unknown_alu_or_state(self):
        for raw in [(0xC80F8000, 0, 0x83000000),
                    (0xC90F8000, 0, 0x81000000),
                    (0xC81F8000, 0, 0x81000000),
                    (0xC80F8000, 1 << 28, 0x81000000),
                    (0xC80F8000, 0, 0x81800000)]:
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                shader.decode_alu(raw)

    def test_reject_unknown_fetch_or_predication(self):
        for raw in [(31, 0, 0), (1, 0x80000000, 0), (0, 6, 0), (1 << 11, 0, 0)]:
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                shader.decode_fetch(raw)

    def test_word_range_checks(self):
        for offset, count in [(-4, 1), (1, 1), (0, 0), (4, 1)]:
            with self.subTest(offset=offset, count=count), self.assertRaises(ValueError):
                shader.words(b"\0" * 4, offset, count)


class OriginalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = (ROOT / "analysis/simpsons.pe").read_bytes()
        cls.report = shader.inspect_image(cls.image)
        cls.by_name = {r["name"]: r for r in cls.report["records"]}

    def record(self, name):
        va, offset, size, _, _ = shader.PROFILES[name]
        return self.image[va-shader.BASE:va-shader.BASE+offset+size]

    def test_sprite_unready_query_branch_precedes_draw_and_blend_reset(self):
        def original(address, size):
            return self.image[address-shader.BASE:address-shader.BASE+size].hex()
        self.assertEqual(original(0x8276B0D0, 16), "817d0004896b001a2b0b0000419a0180")
        self.assertEqual(0x8276B0DC + 0x180, 0x8276B25C)
        self.assertEqual(original(0x8276B24C, 20), "4bfffa7538800000807fcaf84bcd0159382100d0")

    def test_actual_nine_instructions(self):
        all_ops = [i for r in self.by_name.values() for i in r["instructions"]]
        self.assertEqual(len(all_ops), 9)
        self.assertEqual(sum(i["kind"] == "alu" for i in all_ops), 5)
        self.assertEqual(sum(i["kind"] == "vertex_fetch" for i in all_ops), 3)
        self.assertEqual(sum(i["kind"] == "texture_fetch" for i in all_ops), 1)

    def test_vertex_position_z_w_and_exports(self):
        for name, reg in [("VSFlat", 0), ("VSTextured", 1)]:
            ops = self.by_name[name]["instructions"]
            self.assertEqual(ops[0]["destination_register"], reg)
            self.assertEqual(ops[0]["destination_swizzle"], [0, 1, 4, 5])
            pos = next(i for i in ops if i.get("export_register") == 62)
            self.assertEqual((pos["operation"], pos["vector_mask"]), ("MAX", 15))
            self.assertEqual(pos["sources"], [{"bank": "temporary", "register": reg,
                                               "swizzle": [0, 1, 2, 3]}] * 2)

    def test_textured_interpolator_xy_only(self):
        op = self.by_name["VSTextured"]["instructions"][-1]
        self.assertEqual((op["export_register"], op["vector_mask"]), (0, 3))
        self.assertEqual(op["sources"], [{"bank": "temporary", "register": 0,
                                         "swizzle": [0, 1, 1, 1]}] * 2)

    def test_flat_color_including_alpha(self):
        op, = self.by_name["PSFlat"]["instructions"]
        self.assertEqual((op["operation"], op["export_register"], op["vector_mask"]),
                         ("MAX", 0, 15))
        self.assertEqual(op["sources"], [{"bank": "constant", "register": 0,
                                         "swizzle": [0, 1, 2, 3]}] * 2)

    def test_textured_color_and_alpha_multiply(self):
        op = self.by_name["PSTextured"]["instructions"][-1]
        self.assertEqual((op["operation"], op["export_register"], op["vector_mask"]),
                         ("MUL", 0, 15))
        self.assertEqual(op["sources"], [
            {"bank": "temporary", "register": 0, "swizzle": [0, 1, 2, 3]},
            {"bank": "constant", "register": 0, "swizzle": [0, 1, 2, 3]}])

    def test_texture_coordinates_and_inherited_filters(self):
        op = self.by_name["PSTextured"]["instructions"][0]
        self.assertTrue(op["normalized_coordinates"])
        self.assertEqual(op["dimension_field"], 1)
        self.assertEqual(op["source_components"][:2], [0, 1])
        self.assertEqual(op["destination_swizzle"], [0, 1, 2, 3])
        self.assertTrue(op["computed_lod"])
        self.assertFalse(op["register_lod"] or op["register_gradients"])
        self.assertEqual(op["offset_fields"], [0, 0, 0])
        self.assertEqual(op["lod_bias_field"], 0)
        self.assertEqual([op[k] for k in ("mag_filter", "min_filter", "mip_filter",
                                        "volume_mag_filter", "volume_min_filter")], [3] * 5)
        self.assertEqual(op["anisotropy"], 7)

    def test_reject_changed_image_or_size(self):
        with self.assertRaises(ValueError):
            shader.inspect_image(self.image[:-1])
        bad = bytearray(self.image)
        bad[0x1525D0] ^= 1
        with self.assertRaises(ValueError):
            shader.inspect_image(bad)

    def test_reject_each_altered_record(self):
        for name in self.by_name:
            bad = bytearray(self.record(name))
            bad[-1] ^= 1
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "hash"):
                shader.inspect_record(bad, name)

    def test_reject_framing_and_profile_changes(self):
        data = self.record("PSFlat")
        for bad in (data[:-1], data+b"\0", data[:4]+b"\xff"*4+data[8:]):
            with self.subTest(size=len(bad)), self.assertRaises(ValueError):
                shader.inspect_record(bad, "PSFlat")
        with self.assertRaises(ValueError):
            shader.inspect_record(data, "Other")

    def test_reject_exec_trailer_or_unknown_branch(self):
        offset = shader.PROFILES["PSFlat"][1]
        original = self.record("PSFlat")[offset:]
        # Second packed CF starts in high half of word1: replace address1 by2.
        bad = bytearray(original)
        struct.pack_into(">I", bad, 4, 0x1002C400)
        with self.assertRaisesRegex(ValueError, "trailer"):
            shader.decode_schedule(bad, 1)
        bad = bytearray(original)
        struct.pack_into(">I", bad, 8, 0x42000000)
        with self.assertRaisesRegex(ValueError, "opcode"):
            shader.decode_schedule(bad, 1)

    def test_stdout_is_deterministic_and_cli_rejects(self):
        outputs = []
        for _ in range(2):
            output = io.StringIO()
            with redirect_stdout(output):
                self.assertEqual(shader.main([]), 0)
            outputs.append(output.getvalue())
        self.assertEqual(outputs[0], outputs[1])
        self.assertEqual(json.loads(outputs[0]), json.loads(json.dumps(self.report)))
        output, error = io.StringIO(), io.StringIO()
        with patch.object(Path, "read_bytes", return_value=b"bad"), \
                redirect_stdout(output), redirect_stderr(error):
            self.assertEqual(shader.main([]), 2)
        self.assertEqual(output.getvalue(), "")
        self.assertIn("size", error.getvalue())


if __name__ == "__main__":
    unittest.main()
