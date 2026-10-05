"""Exact integer reference for the atlas-only SM5 reciprocal correction.

The rational reference rounds from the value represented by the original input
bits. The separate limb model follows the shader, including deliberately bad
estimates to exercise its bounded fallback. No expected shader case is changed.
"""
import random
import unittest

MASK = 0xFFFFFFFF


def rational_reciprocal(bits):
    sign, exponent, fraction = bits & 0x80000000, (bits >> 23) & 255, bits & 0x7FFFFF
    if exponent == 255:
        return sign if not fraction else bits | 0x400000
    if exponent == 0 and not fraction:
        return sign | 0x7F800000
    if exponent:
        numerator, denominator = 1, 0x800000 | fraction
        power = 150 - exponent
    else:
        numerator, denominator, power = 1, fraction, 149
    if power >= 0:
        numerator <<= power
    else:
        denominator <<= -power
    binary_exponent = numerator.bit_length() - denominator.bit_length()
    if (numerator < denominator << binary_exponent if binary_exponent >= 0
            else numerator << -binary_exponent < denominator):
        binary_exponent -= 1
    if binary_exponent > 127:
        return sign | 0x7F800000
    scale = 149 if binary_exponent < -126 else 23 - binary_exponent
    n, d = (numerator << scale, denominator) if scale >= 0 else (numerator, denominator << -scale)
    quotient, remainder = divmod(n, d)
    if remainder * 2 > d or remainder * 2 == d and quotient & 1:
        quotient += 1
    if binary_exponent < -126:
        return sign | quotient
    if quotient == 0x1000000:
        quotient >>= 1
        binary_exponent += 1
    if binary_exponent > 127:
        return sign | 0x7F800000
    return sign | ((binary_exponent + 127) << 23) | (quotient & 0x7FFFFF)


def corrected_reciprocal(bits, estimate_error=0):
    sign, e, m = bits & 0x80000000, (bits >> 23) & 255, bits & 0x7FFFFF
    if e == 255:
        return (sign if not m else bits | 0x400000), False
    if e == 0:
        if not m:
            return sign | 0x7F800000, False
        shift = 24 - m.bit_length()
        m <<= shift
        e = 1 - shift
    else:
        m |= 0x800000
    if m == 0x800000:
        power = 254 - e
        return (sign | (0x7F800000 if power >= 255 else power << 23 if power > 0 else 1 << (power + 22))), False
    result_exponent = 253 - e
    if result_exponent >= 255:
        return sign | 0x7F800000, False
    k = 47 if result_exponent > 0 else 46 + result_exponent
    numerator_high = 1 << (k - 32)
    q = (1 << k) // m + estimate_error
    q_low, m_low = q & 65535, m & 65535
    p0 = q_low * m_low
    cross = (q >> 16) * m_low + q_low * (m >> 16)
    lo = (p0 + (cross << 16)) & MASK
    hi = (q >> 16) * (m >> 16) + (cross >> 16) + int(lo < p0)
    for _ in range(4):
        if hi > numerator_high or hi == numerator_high and lo > 0:
            q -= 1
            old = lo
            lo = (lo - m) & MASK
            hi -= int(old < m)
        else:
            next_lo = (lo + m) & MASK
            next_hi = hi + int(next_lo < lo)
            if next_hi < numerator_high or next_hi == numerator_high and next_lo == 0:
                q += 1
                lo, hi = next_lo, next_hi
    next_lo = (lo + m) & MASK
    next_hi = hi + int(next_lo < lo)
    fallback = (hi > numerator_high or hi == numerator_high and lo > 0 or
                next_hi < numerator_high or next_hi == numerator_high and next_lo == 0)
    if fallback:
        q, remainder = 0, 1 << (k - 24)
        assert remainder < m
        for _ in range(24):
            remainder <<= 1
            q <<= 1
            if remainder >= m:
                remainder -= m
                q |= 1
    else:
        remainder = (-lo) & MASK
    assert q * m + remainder == 1 << k and 0 <= remainder < m
    if remainder * 2 > m or remainder * 2 == m and q & 1:
        q += 1
    if result_exponent <= 0:
        return sign | q, fallback
    if q == 0x1000000:
        q >>= 1
        result_exponent += 1
    if result_exponent >= 255:
        return sign | 0x7F800000, fallback
    return sign | (result_exponent << 23) | (q & 0x7FFFFF), fallback


class AtlasReciprocalTests(unittest.TestCase):
    def test_special_values_and_every_power_exponent(self):
        values = [0, 0x80000000, 0x7F800000, 0xFF800000, 0x7FC00001, 0xFFA12345]
        values += [(e << 23) | sign for sign in (0, 0x80000000) for e in range(1, 255)]
        values += [(1 << bit) | sign for sign in (0, 0x80000000) for bit in range(23)]
        for bits in values:
            self.assertEqual(corrected_reciprocal(bits, 100)[0], rational_reciprocal(bits), hex(bits))

    def test_normal_subnormal_and_adjacent_estimates(self):
        random_source = random.Random(0x8203C008)
        values = [random_source.getrandbits(32) for _ in range(12000)]
        values += [(e << 23) | m for e in range(255) for m in (1, 2, 0x3FFFFF, 0x400000, 0x7FFFFE, 0x7FFFFF)]
        for bits in values:
            expected = rational_reciprocal(bits)
            for error in (-4, -3, -1, 0, 1, 3, 4):
                result, fallback = corrected_reciprocal(bits, error)
                self.assertEqual(result, expected, (hex(bits), error))
                self.assertFalse(fallback, (hex(bits), error))

    def test_outside_estimate_bracket_uses_exact_bounded_fallback(self):
        for e in (0, 1, 2, 126, 127, 128, 252, 253, 254):
            for m in (1, 0x12345, 0x3FFFFF, 0x400001, 0x7FFFFF):
                bits = (e << 23) | m
                for error in (-100, 100):
                    result, fallback = corrected_reciprocal(bits, error)
                    self.assertEqual(result, rational_reciprocal(bits), (hex(bits), error))
                    if 0 < ((bits >> 23) & 255) < 255:
                        self.assertTrue(fallback)

    def test_hardware_frame33_and_direct_subnormal_packing(self):
        self.assertEqual(corrected_reciprocal(0x42040000)[0], 0x3CF83E10)  # 33.0
        self.assertEqual(corrected_reciprocal(0x7E800000)[0], 0x00800000)
        self.assertEqual(corrected_reciprocal(0x7F000000)[0], 0x00400000)
        self.assertEqual(corrected_reciprocal(0x7F7FFFFF)[0], 0x00200000)


if __name__ == '__main__':
    unittest.main()
