"""Original-byte/storage evidence plus explicit uncertainty counterexamples.

All mutations are in RAM; no renderer/backend, writes, or hardware oracle.
"""
from contextlib import redirect_stderr, redirect_stdout
from fractions import Fraction as F
import io
import json
from pathlib import Path
import random
import sys
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import analyze_expanded_blend as blend


class DescriptorTests(unittest.TestCase):
    def test_all_format_transitions(self):
        off = {2: 2, 3: 3, 10: 2, 12: 3}
        on = {2: 10, 3: 12, 10: 10, 12: 12}
        for fmt in range(16):
            self.assertEqual(blend.expanded_format(fmt, 0), off.get(fmt, fmt))
            self.assertEqual(blend.expanded_format(fmt, 1), on.get(fmt, fmt))

    def test_descriptor_bits_independent_and_idempotent(self):
        rng = random.Random(0x8243B3B0)
        for fmt in range(16):
            for request in (0, 1):
                for _ in range(64):
                    original = (rng.getrandbits(32) & ~0xF0000) | (fmt << 16)
                    changed = blend.update_descriptor(original, request)
                    self.assertEqual((original ^ changed) & ~0xF0000, 0)
                    self.assertEqual((changed >> 16) & 15, blend.expanded_format(fmt, request))
                    self.assertEqual(blend.update_descriptor(changed, request), changed)

    def test_toggle_roundtrip_and_no_target(self):
        for fmt in (2, 3):
            descriptor = 0xB6F02DA9 | fmt << 16
            self.assertEqual(blend.update_descriptor(blend.update_descriptor(descriptor, 1), 0), descriptor)
        self.assertIsNone(blend.update_descriptor(None, 1))
        self.assertIsNone(blend.update_descriptor(None, 0))
        # Saved request can be applied to a different subsequently bound target.
        request = 1
        self.assertEqual((blend.update_descriptor(0x00520003, request) >> 16) & 15, 10)
        self.assertEqual((blend.update_descriptor(0x00A30007, request) >> 16) & 15, 12)

    def test_reject_noncanonical_or_out_of_range_inputs(self):
        for fmt in (-1, 16, 0xFFFFFFFF, True, "2", 2.0):
            with self.subTest(fmt=fmt), self.assertRaises(ValueError):
                blend.expanded_format(fmt, 1)
        for request in (-1, 2, 0xFFFFFFFF, True, "1"):
            with self.subTest(request=request), self.assertRaises(ValueError):
                blend.update_descriptor(None, request)
        for descriptor in (-1, 1 << 32, False, 1.0):
            with self.assertRaises(ValueError):
                blend.update_descriptor(descriptor, 1)


class StorageTests(unittest.TestCase):
    def test_asymmetric_channels_and_two_bit_alpha(self):
        codes = (1, 0x155, 0x2AA, 2)
        expected = 0xAAA55401
        self.assertEqual(blend.pack_storage_codes(codes, 2), expected)
        self.assertEqual(blend.pack_storage_codes(codes, 10), expected)
        self.assertEqual(blend.unpack_storage_codes(expected, 10), codes)
        for alpha in range(4):
            self.assertEqual(blend.unpack_storage_codes(alpha << 30, 10), (0, 0, 0, alpha))

    def test_all_individual_component_codes(self):
        for component, limit in enumerate((1024, 1024, 1024, 4)):
            for code in range(limit):
                codes = [17, 301, 999, 3]
                codes[component] = code
                packed = blend.pack_storage_codes(codes, 10)
                self.assertEqual(blend.unpack_storage_codes(packed, 2), tuple(codes))

    def test_arbitrary_words_survive_precision_mode_change(self):
        rng = random.Random(0x2101010)
        for packed in [0, 0xFFFFFFFF, 0x80000001] + [rng.getrandbits(32) for _ in range(1024)]:
            self.assertEqual(blend.pack_storage_codes(blend.unpack_storage_codes(packed, 10), 2), packed)
            self.assertEqual(blend.storage_format(10), blend.storage_format(2))

    def test_reject_other_formats_and_bad_codes(self):
        for fmt in (0, 3, 12, 54, True, 2.0):
            with self.assertRaises(ValueError):
                blend.pack_storage_codes((0, 0, 0, 0), fmt)
        for codes in ((0, 0, 0), (0, 0, 0, 4), (1024, 0, 0, 0),
                      (0, -1, 0, 0), (0, 0, True, 0), (0.0, 0, 0, 0)):
            with self.assertRaises(ValueError):
                blend.pack_storage_codes(codes, 10)
        for packed in (-1, 1 << 32, True, 0.5):
            with self.assertRaises(ValueError):
                blend.unpack_storage_codes(packed, 2)


class UncertaintyTests(unittest.TestCase):
    def test_distinct_plausible_source_alpha_precision_results(self):
        witness = blend.precision_witnesses()["selector0_source_alpha_precision"]
        self.assertEqual(witness["results"], {"alpha_first_quantized_to_2_bits": 0,
                         "alpha_first_quantized_to_10_bits": 5, "unquantized_source_alpha": 6})
        self.assertGreater(F(witness["source_alpha"]), F(1, 255))

    def test_even_odd_ties_are_distinct_from_truncation(self):
        self.assertEqual(blend.hypothetical_unorm_code(F(1, 2046), 10, "half_up"), 1)
        self.assertEqual(blend.hypothetical_unorm_code(F(1, 2046), 10, "nearest_even"), 0)
        self.assertEqual(blend.hypothetical_unorm_code(F(3, 2046), 10, "nearest_even"), 2)
        self.assertEqual(blend.hypothetical_unorm_code(F(3, 2046), 10, "truncate"), 1)
        for bits in (2, 10):
            for mode in ("half_up", "nearest_even", "truncate"):
                self.assertEqual(blend.hypothetical_unorm_code(F(0), bits, mode), 0)
                self.assertEqual(blend.hypothetical_unorm_code(F(1), bits, mode), (1 << bits) - 1)

    def test_persistent_storage_is_not_high_precision_accumulation(self):
        witness = blend.precision_witnesses()["persistent_storage_vs_deferred_quantization"]
        self.assertEqual(witness["quantize_each_write_code"], 0)
        self.assertEqual(witness["quantize_only_after_four_writes_code"], 1)

    def test_reject_witness_floats_nonfinite_and_undefined_rounding(self):
        for value in (0.0, float("nan"), float("inf"), F(-1, 10), F(11, 10), True):
            with self.assertRaises(ValueError):
                blend.hypothetical_unorm_code(value, 10, "half_up")
        with self.assertRaises(ValueError):
            blend.hypothetical_unorm_code(F(1, 2), 10, "hardware")
        for bits in (0, 8, 32, True):
            with self.assertRaises(ValueError):
                blend.hypothetical_unorm_code(F(1, 2), bits, "half_up")


class EvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = (ROOT / "analysis/simpsons.pe").read_bytes()

    def test_original_image_windows_and_reference_snapshots(self):
        report = blend.analyze(self.image)
        self.assertEqual(report["verified_original_instruction_words"], 2210)
        self.assertEqual(len(report["original_windows"]), 16)
        self.assertEqual(len(report["reference_sources"]), 7)
        self.assertFalse(report["native_exact_blend_contract_proven"])
        self.assertFalse(report["storage"]["persistent_expanded_alpha_bits"])
        self.assertIn("unsupported", report["decision"])
        self.assertTrue(report["unresolved"])

    def test_changed_or_truncated_image_rejected(self):
        changed = bytearray(self.image)
        changed[0x8243B408 - blend.BASE] ^= 1
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            blend.verify_image(changed)
        with self.assertRaisesRegex(ValueError, "expanded_target0_setter"):
            blend.verify_windows(changed)
        with self.assertRaises(ValueError):
            blend.verify_image(self.image[:-1])
        with self.assertRaises(ValueError):
            blend.verify_image(self.image + b"\0")

    def test_reference_changes_or_unknown_identity_rejected(self):
        relative = blend.REFERENCES[0][0]
        data = (blend.REFERENCE_ROOT / relative).read_bytes()
        for changed in (data[:-1], data + b" ", b"different reference"):
            with self.assertRaisesRegex(ValueError, "Changed reference"):
                blend.verify_reference(changed, relative)
        with self.assertRaisesRegex(ValueError, "Unknown reference"):
            blend.verify_reference(data, "another/header.h")

    def test_original_range_bounds_and_alignment(self):
        for va, size in ((blend.BASE - 4, 4), (blend.BASE + 1, 4), (blend.BASE, 0),
                         (blend.BASE, -4), (blend.BASE, 5), (0xFFFFFFFF, 4),
                         (blend.BASE + len(self.image), 4), (True, 4)):
            with self.assertRaises(ValueError):
                blend.original_range(self.image, va, size)

    def test_cli_determinism_and_no_false_success(self):
        outputs = []
        for _ in range(2):
            out = io.StringIO()
            with redirect_stdout(out):
                self.assertEqual(blend.main([]), 0)
            outputs.append(out.getvalue())
        self.assertEqual(outputs[0], outputs[1])
        self.assertFalse(json.loads(outputs[0])["native_exact_blend_contract_proven"])
        out, err = io.StringIO(), io.StringIO()
        with patch.object(Path, "read_bytes", side_effect=OSError("fixture read failure")):
            with redirect_stdout(out), redirect_stderr(err):
                self.assertEqual(blend.main([]), 1)
        self.assertEqual(out.getvalue(), "")
        self.assertIn("rejected", err.getvalue())


if __name__ == "__main__":
    unittest.main()
