#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace Simpsons::Graphics {

// Original declaration: POSITION float2 at +0, TEXCOORD0 float2 at +8.
// Host float storage, not a big-endian guest byte buffer. No backend types.
struct MovieVertex {float x,y,u,v;};
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);
static_assert(std::is_standard_layout_v<MovieVertex> &&
              std::is_trivially_copyable_v<MovieVertex>);
static_assert(sizeof(MovieVertex)==16 && offsetof(MovieVertex,x)==0 &&
              offsetof(MovieVertex,y)==4 && offsetof(MovieVertex,u)==8 &&
              offsetof(MovieVertex,v)==12);
static_assert(sizeof(std::array<MovieVertex,3>)==48 && sizeof(std::array<MovieVertex,4>)==64);

struct MovieGeometry {
    // Original primitive8, count3, stride16 (48 bytes).
    std::array<MovieVertex,3> originalVertices;
    // Native triangle strip: indices (0,1,2), (2,1,3). Original order/UV
    // survives; vertex3 completes the rectangle. Never flip Y or V here.
    std::array<MovieVertex,4> nativeVertices;
};

struct MovieGeometryError : std::runtime_error {using std::runtime_error::runtime_error;};

// Exact 8282E3E8 geometry. Width is compared as a signed int32 against 640;
// every byte value is accepted, with zero/nonzero semantics at presenter+0x41.
// Zero/negative widths still select the original <=640 branch. Resource and
// viewport validity belong to the caller; height is not a geometry input.
MovieGeometry buildMovieGeometry(int32_t lumaWidth,uint8_t presenterByte41);

// Accepts exactly one of the original movie's three distinct XYUV profiles,
// in original buffer order. Rejects other counts, coordinates or UV bit patterns
// (including nonfinite values, signed zero and unproved fractional rectangles).
// Returns owned vertices without modifying/retaining the source. No FP math,
// device access, raster-state changes or arbitrary rectangle emulation.
std::array<MovieVertex,4> expandMovieRectangle(std::span<const MovieVertex> original);

}
