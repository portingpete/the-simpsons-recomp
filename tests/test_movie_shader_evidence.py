"""Pinned decoding, corruption rejection and independent exact-rational oracle.

No game, shared build, GPU or original writes. Run with python -B -m unittest
discover -s tests -p test_movie_shader_evidence.py -v.
"""
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import random
import re
import struct
import sys
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import analyze_movie_shader as movie


def rounded(value):
    """Independent integer-only nearest-even binary32 rounding of a Fraction."""
    if not value:
        return Fraction(0)
    sign = -1 if value < 0 else 1
    n, d = abs(value.numerator), value.denominator
    exponent = n.bit_length() - d.bit_length()
    below_power = n < (d << exponent) if exponent >= 0 else (n << -exponent) < d
    if below_power:
        exponent -= 1
    shift = 23 - max(exponent, -126)
    if shift >= 0:
        n <<= shift
    else:
        d <<= -shift
    q, remainder = divmod(n, d)
    if remainder * 2 > d or (remainder * 2 == d and q & 1):
        q += 1
    return sign * (Fraction(q, 1 << shift) if shift >= 0 else Fraction(q << -shift))


def rational_oracle(samples):
    # Direct scalar equations, independent of decoded instructions, masks,
    # swizzle helper, constant reader, and float32 arithmetic implementation.
    y, cr, cb = [rounded(Fraction(x)) for x in samples]
    y = rounded(y - Fraction(1, 16))
    cr = rounded(cr - Fraction(1, 2))
    cb = rounded(cb - Fraction(1, 2))
    # Exact binary fractions from the pinned coefficient bits.
    ky = Fraction(9767553, 8388608)
    kr = Fraction(13388445, 8388608)
    kgcr = -Fraction(13639340, 16777216)
    kgcb = -Fraction(13145351, 33554432)
    kb = Fraction(8460884, 4194304)
    red = rounded(rounded(y * ky) + rounded(cr * kr))
    green = rounded(rounded(rounded(cr * kgcr) + rounded(cb * kgcb)) + rounded(y * ky))
    blue = rounded(rounded(y * ky) + rounded(cb * kb))
    return [float(red), float(green), float(blue), 0.0]


def read_compiled_listing(path):
    """Bounded FXC /Lx listing reader, not an HLSL evaluator or GPU emulator."""
    statements = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith(("//", "dcl_")) or line in ("ps_5_0", "ret"):
            continue
        if line.startswith("sample_indexable"):
            match = re.fullmatch(r"sample_indexable .* (r\d+\.[xyzw]+), v1\.xyxx, t([012])\.([xyzw]{4}), s([012])", line)
            if not match or match[2] != match[4]:
                raise ValueError("Unexpected compiled sample form")
            statements.append(("sample", match[1], int(match[2]), match[3]))
        else:
            operation = line.split()[0]
            if operation not in ("add", "add_sat", "mul", "mov", "mov_sat", "round_ne", "ftou"):
                raise ValueError("Unexpected compiled opcode: " + operation)
            if "[precise" not in line:
                raise ValueError("Missing compiled precise modifier")
            operands = re.findall(r"[ro]\d+\.[xyzw]+|l\([^)]*\)", line)
            expected = 3 if operation in ("add", "add_sat", "mul") else 2
            if len(operands) != expected:
                raise ValueError("Unexpected compiled operands")
            statements.append((operation, *operands))
    if sum(s[0] == "sample" for s in statements) != 3:
        raise ValueError("Expected three compiled samples")
    return statements


def evaluate_compiled(statements, sample):
    regs = {"r0": [None]*4, "r1": [None]*4, "o0": [None]*4}

    def source(operand):
        if operand.startswith("l("):
            result = [movie.from_bits(int(x.strip(), 16)) for x in operand[2:-1].split(",")]
        else:
            name, swizzle = operand.split(".")
            result = [regs[name]["xyzw".index(c)] for c in swizzle]
        return result * 4 if len(result) == 1 else result

    for operation, destination, *operands in statements:
        name, mask = destination.split(".")
        if operation == "sample":
            stage, swizzle = operands
            # Poison unused GBA: verify compiled samples actually consume X.
            view = [movie.f32(sample[stage]), 0.123, 0.789, 0.456]
            value = [view["xyzw".index(c)] for c in swizzle]
        else:
            src = list(map(source, operands))
            value = [None] * 4
            for component in mask:
                i = "xyzw".index(component)
                if operation.startswith("add"):
                    result = movie.f32(src[0][i] + src[1][i])
                elif operation == "mul":
                    result = movie.f32(src[0][i] * src[1][i])
                elif operation == "round_ne":
                    result = round(src[0][i])
                elif operation == "ftou":
                    result = int(src[0][i])
                else:
                    result = src[0][i]
                value[i] = min(1.0, max(0.0, result)) if operation.endswith("_sat") else result
        for component in mask:
            i = "xyzw".index(component)
            regs[name][i] = value[i]
    return regs["o0"]


class MovieEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = (ROOT / "analysis/simpsons.pe").read_bytes()
        cls.record_bytes = cls.image[movie.VA - movie.BASE:movie.VA - movie.BASE + movie.SIZE]
        cls.report = movie.inspect_image(cls.image)
        cls.record = cls.report["record"]
        cls.compiled = read_compiled_listing(ROOT / "build/movie-shader/PSMovie.asm")
        cls.compiled_packed = read_compiled_listing(ROOT / "build/movie-shader/PSMoviePacked.asm")

    def test_identity_and_saved_report(self):
        self.assertEqual(hashlib.sha256(self.image).hexdigest(), movie.screen.IMAGE_SHA256)
        self.assertEqual(json.loads(movie.REPORT.read_text(encoding="utf-8")), self.report)

    def test_payload_is_not_all_instructions(self):
        self.assertEqual(self.record["literal_payload_bytes"], 64)
        self.assertEqual(self.record["executable_offset"], 0x1A4)
        self.assertEqual(self.record["executable_size"], 120)
        self.assertEqual(self.record["executed_slots"], list(range(2, 9)))
        self.assertEqual([x["kind"] for x in self.record["instructions"]], ["texture_fetch"] * 3 + ["alu"] * 4)
        with self.assertRaises(ValueError):
            movie.decode_program(self.record_bytes[0x164:])

    def test_all_4320_single_bit_record_corruptions_rejected(self):
        for byte in range(len(self.record_bytes)):
            for bit in range(8):
                mutated = bytearray(self.record_bytes)
                mutated[byte] ^= 1 << bit
                with self.subTest(byte=byte, bit=bit), self.assertRaises(ValueError):
                    movie.decode_record(mutated)

    def test_truncated_extra_swapped_and_unrelated_image_changes_rejected(self):
        for value in (self.record_bytes[:-1], self.record_bytes + b"\0",
                      b"".join(self.record_bytes[i:i+4][::-1] for i in range(0, movie.SIZE, 4))):
            with self.assertRaises(ValueError):
                movie.decode_record(value)
        for value in (self.image[:-1], self.image + b"\0", bytes([self.image[0] ^ 1]) + self.image[1:]):
            with self.assertRaises(ValueError):
                movie.inspect_image(value)

    def test_field_rejection_independent_of_record_hash(self):
        a, b, c = movie.EXPECTED_WORDS[4]
        bad_alus = [(a | (1 << 24), b, c), (a | (1 << 20), b, c),
                    (a, b | (1 << 28), c), (a | 64, b, c),
                    (a, b, c | (128 << 16)), (a, b, (c & 0xE0FFFFFF) | (11 << 24)),
                    (a, b, c & ~0xFF00)]
        for value in bad_alus:
            with self.subTest(value=value), self.assertRaises(ValueError):
                movie.decode_alu(value)
        a, b, c = movie.EXPECTED_WORDS[0]
        for value in ((a | 1 << 11, b, c), (a | 1 << 25, b, c),
                      (a, b | 1 << 29, c), (a, b, c | 1 << 16),
                      (a, (b & ~4095) | 0x688, c), (a, b, c | 2)):
            with self.subTest(value=value), self.assertRaises(ValueError):
                movie.decode_fetch(value)

    def test_schedule_rejects_overlap_repeat_fetch_swap_and_trailer_execution(self):
        code = bytearray(self.record_bytes[movie.CODE_OFFSET:])
        for offset, value in ((0, 0x00153001), (0, 0x00154002), (0, 0x00113002),
                              (12, 0x00005005), (12, 0x00004004), (20, 1)):
            changed = code.copy()
            struct.pack_into(">I", changed, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                movie.decode_program(changed)

    def test_constants_full_unsigned_indices_and_no_c0(self):
        alus = self.record["instructions"][3:]
        actual = sorted({x["register"] for i in alus for x in i["sources"] if x["bank"] == "constant"})
        self.assertEqual(actual, [253, 254, 255])
        self.assertEqual(self.record["metadata"]["literal_descriptor"], "01FC0010")
        self.assertEqual([x["register"] for x in self.record["literals"]], [252, 253, 254, 255])
        self.assertEqual(self.record["literals"][1]["float32"][2:], [-0.0625, -0.5])

    def test_literal_packet_provenance_and_reject_other_bindings(self):
        packet = self.record["literal_upload"]
        self.assertEqual(packet["offset_type"], "000007F0")
        self.assertEqual(packet["first_pixel_constant"], 252)
        self.assertEqual(packet["hardware_register_start"], "000047F0")
        self.assertEqual(packet["hardware_register_end_inclusive"], "000047FF")
        metadata = self.record_bytes[0x118:0x140]
        for at, value in ((0x14, 0x01000010), (0x14, 0x01FC000F), (0x18, 16), (0x1C, 1)):
            changed = bytearray(metadata)
            struct.pack_into(">I", changed, at, value)
            with self.assertRaises(ValueError):
                movie.decode_literal_upload(changed, 0xB8)

    def test_r8_descriptor_unsigned_fractional_xxx1_and_no_gamma(self):
        view = movie.r8_contract()
        self.assertEqual(view["hardware_format"], 2)
        self.assertEqual(view["resource_word28_low19"], "00001400")
        self.assertEqual(view["resource_swizzle_xyzw"], [0, 0, 0, 5])
        self.assertEqual(view["signs_xyzw"], [0, 0, 0, 0])
        self.assertEqual(view["number_format"], 0)
        for changed in (0x28000002 | (3 << 9), 0x28000002 | (1 << 17), 0x28000003):
            with self.assertRaises(ValueError):
                movie.r8_contract(changed)
        with self.assertRaises(ValueError):
            movie.r8_contract(exponent_adjust=1)

    def test_r8_x_only_preserve_fetch_writes_and_same_uv(self):
        fetches = self.record["instructions"][:3]
        self.assertEqual([x["fetch_constant_index"] for x in fetches], [0, 1, 2])
        self.assertEqual([x["destination_swizzle"] for x in fetches], [[0, 7, 7, 7], [7, 0, 7, 7], [7, 7, 0, 7]])
        self.assertEqual([x["source_components"][:2] for x in fetches], [[0, 1]] * 3)
        output, trace = movie.evaluate_samples(self.record, [0.125, 0.25, 0.75], True)
        self.assertEqual(trace[0]["r1"], [0.125, None, None, None])
        self.assertEqual(trace[1]["r1"], [0.125, 0.25, None, None])
        self.assertEqual(trace[2]["r1"], [0.125, 0.25, 0.75, None])
        self.assertIsNone(trace[3]["r0"][3])  # Never requires undefined W.
        self.assertEqual(output[3], 0)

    def test_alpha_is_explicit_export_zero_not_one_or_uninitialized(self):
        exports = self.record["instructions"][4:]
        self.assertEqual([i["vector_mask"] for i in exports], [1, 2, 4])
        self.assertEqual([i["constant_zero_mask"] for i in exports], [14, 0, 0])
        _, trace = movie.evaluate_samples(self.record, [0.8, 0.3, 0.7], True)
        self.assertEqual(trace[4]["color0"][1:], [0.0, 0.0, 0.0])
        self.assertEqual([step["color0"][3] for step in trace[4:]], [0.0] * 3)

    def test_live_boot150_golden_first_frame_is_not_exact_black(self):
        value = movie.evaluate_samples(self.record, [16/255, 128/255, 128/255])
        self.assertEqual(list(map(movie.bits, value)), [0x3B5FCC97, 0xBB081BEA, 0x3B8AF67C, 0])
        self.assertEqual(movie.pack_color(value), [3, 0, 4, 0])
        self.assertEqual(evaluate_compiled(self.compiled_packed, [16/255, 128/255, 128/255]), [3, 0, 4, 0])
        self.assertLess(value[1], 0)
        self.assertGreater(value[0], 0)
        self.assertGreater(value[2], 0)
        self.assertEqual(movie.evaluate_samples(self.record, [0.0625, 0.5, 0.5]), [0.0] * 4)

    def test_no_saturation_or_byte_neutral_substitution(self):
        value = movie.evaluate_samples(self.record, [235/255, 128/255, 128/255])
        self.assertGreater(value[0], 1)
        self.assertGreater(value[2], 1)
        value = movie.evaluate_samples(self.record, [0.5, 1, 0])
        self.assertLess(value[2], 0)
        self.assertNotEqual(value, movie.evaluate_samples(self.record, [0.5, 0, 1]))

    def test_rational_rounder_ties(self):
        self.assertEqual(rounded(Fraction(1) + Fraction(1, 1 << 24)), 1)
        self.assertEqual(rounded(Fraction(1) + Fraction(3, 1 << 24)), Fraction(1) + Fraction(1, 1 << 22))
        self.assertEqual(rounded(-Fraction(1) - Fraction(1, 1 << 24)), -1)

    def test_independent_rational_oracle_byte_sweeps_and_filtered_samples(self):
        rng = random.Random(0x82152B68)
        corpus = [[v/255, 128/255, 128/255] for v in range(256)]
        corpus += [[93/255, v/255, 37/255] for v in range(256)]
        corpus += [[93/255, 211/255, v/255] for v in range(256)]
        corpus += [[rng.randrange(256)/255 for _ in range(3)] for _ in range(1024)]
        # Fixed 1/256 filter-weight mixtures cover values between byte levels.
        corpus += [[(rng.randrange(255)*256 + rng.randrange(257))/65280 for _ in range(3)] for _ in range(256)]
        for sample in corpus:
            actual = movie.evaluate_samples(self.record, sample)
            expected = rational_oracle(sample)
            self.assertEqual(list(map(movie.bits, actual)), list(map(movie.bits, expected)), sample)
            native = evaluate_compiled(self.compiled, sample)
            self.assertEqual(list(map(movie.bits, native)), list(map(movie.bits, expected)), sample)
            self.assertEqual(evaluate_compiled(self.compiled_packed, sample), movie.pack_color(expected), sample)

    def test_compiled_artifacts_match_source_and_manifest(self):
        manifest = json.loads((ROOT / "build/movie-shader/compile.json").read_text(encoding="utf-8"))
        for name, digest in manifest["artifacts_sha256"].items():
            self.assertEqual(hashlib.sha256((ROOT / name).read_bytes()).hexdigest(), digest, name)
        self.assertEqual(manifest["entries"], ["PSMovie", "PSMoviePacked"])
        self.assertTrue(all(s[0] not in ("mad", "dp2", "dp3") for s in self.compiled))
        self.assertTrue(all(not s[0].endswith("_sat") for s in self.compiled))

    def test_compiled_vertex_pixel_linkage(self):
        vertex = (ROOT / "build/movie-shader/VSTextured.asm").read_text(encoding="utf-8")
        output = vertex.split("// Output signature:", 1)[1].split("vs_5_0", 1)[0]
        patterns = [r"// SV_Position\s+0\s+xyzw\s+0\s+POS\s+float", r"// TEXCOORD\s+0\s+xy\s+1\s+NONE\s+float"]
        for pattern in patterns:
            self.assertRegex(output, pattern)
        for entry in ("PSMovie", "PSMoviePacked"):
            listing = (ROOT / f"build/movie-shader/{entry}.asm").read_text(encoding="utf-8")
            inputs = listing.split("// Input signature:", 1)[1].split("// Output signature:", 1)[0]
            for pattern in patterns:
                self.assertRegex(inputs, pattern)
            self.assertIn("dcl_input_ps linear v1.xy", listing)
        # The original broken entry must stay rejected: numerically correct
        # shader arithmetic alone did not establish producer/consumer linkage.
        with self.assertRaises(ValueError):
            read_compiled_listing(ROOT / "build/movie-shader/PSMovie.before-linkage.asm")

    def test_compiled_channel_corruption_detected_by_asymmetric_oracle(self):
        changed = [list(s) for s in self.compiled]
        sample_instructions = [s for s in changed if s[0] == "sample"]
        sample_instructions[1][2], sample_instructions[2][2] = sample_instructions[2][2], sample_instructions[1][2]
        sample = [93/255, 211/255, 37/255]
        self.assertNotEqual(evaluate_compiled(changed, sample), rational_oracle(sample))
        sample_instructions[0][3] = "yyyy"
        self.assertNotEqual(evaluate_compiled(changed, sample), rational_oracle(sample))

    def test_numeric_domain_rejects_nonfinite_or_non_normalized(self):
        for sample in ([0, 0], [0, float("nan"), 0], [0, float("inf"), 0], [-0.1, 0, 0], [0, 1.1, 0]):
            with self.assertRaises(ValueError):
                movie.evaluate_samples(self.record, sample)


if __name__ == "__main__":
    unittest.main()
