#pragma once

#include <cstddef>
#include <cstdint>
#include <emmintrin.h>

namespace Simpsons {

// Equality-only byte-range comparison for the exact-content mesh caches.
//
// Returns true iff the two ranges hold identical bytes. This replaces the
// equality std::memcmp(...) != 0 tests on cache snapshot planes with
// identical accept/reject semantics: every byte is compared, so there is no
// hash-only acceptance and no weakened content validation.
//
// Implementation: unaligned SSE2 16-byte blocks (64-byte unrolled with a
// combined compare plus a single movemask) plus a bounded scalar tail. x64/SSE2 is the
// project baseline, so this adds no CPU minimum and uses no AVX and no FP.
//
// Bounds: every vector load is wholly inside [data, data + size); the block
// counts are rounded down, so there is no overread even within the same
// page. An empty range compares equal without touching either pointer (no
// dereference and no null pointer arithmetic), so null pointers are
// permitted when size is 0. A non-empty range requires valid non-null
// pointers, exactly like memcmp.
inline bool ByteRangesEqual(const uint8_t* a, const uint8_t* b, size_t size) noexcept {
    if (size == 0) {
        return true;
    }
    size_t offset = 0;
    const size_t bulk64 = size & ~size_t(63);
    for (; offset < bulk64; offset += 64) {
        const __m128i a0 =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + offset));
        const __m128i b0 =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + offset));
        const __m128i a1 =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + offset + 16));
        const __m128i b1 =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + offset + 16));
        const __m128i a2 =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + offset + 32));
        const __m128i b2 =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + offset + 32));
        const __m128i a3 =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + offset + 48));
        const __m128i b3 =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + offset + 48));
        const __m128i c0 = _mm_cmpeq_epi8(a0, b0);
        const __m128i c1 = _mm_cmpeq_epi8(a1, b1);
        const __m128i c2 = _mm_cmpeq_epi8(a2, b2);
        const __m128i c3 = _mm_cmpeq_epi8(a3, b3);
        // Each compare yields 0xFF per equal byte and 0x00 per differing
        // byte; ANDing combines all four blocks so a single movemask is
        // 0xFFFF iff all 64 bytes are equal. Any differing byte in any block
        // clears its lane and reports inequality.
        const __m128i combined = _mm_and_si128(_mm_and_si128(c0, c1), _mm_and_si128(c2, c3));
        if (_mm_movemask_epi8(combined) != 0xFFFF) {
            return false;
        }
    }
    const size_t bulk16 = size & ~size_t(15);
    for (; offset < bulk16; offset += 16) {
        const __m128i av =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + offset));
        const __m128i bv =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + offset));
        if (_mm_movemask_epi8(_mm_cmpeq_epi8(av, bv)) != 0xFFFF) {
            return false;
        }
    }
    for (; offset < size; ++offset) {
        if (a[offset] != b[offset]) {
            return false;
        }
    }
    return true;
}

}  // namespace Simpsons
