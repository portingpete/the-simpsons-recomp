#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Simpsons::Graphics {
// Power-of-two 64..2048 base-only tiled L8, pitch=width, no endian exchange,
// RRR1 selection. Every texel is bounded by its original owned allocation.
std::vector<uint8_t> decodeITXDLuminanceBase(const std::array<uint32_t,6>& descriptor,
    uint32_t width,uint32_t height,std::span<const uint8_t> storage);
// Same L8 base profile with optional authored, packed mip levels. Each stored
// subresource is 4KB aligned; the tail starts at the first 16-texel dimension.
// Returns original RRR1 pixels for every level, without generating any mips.
std::vector<std::vector<uint8_t>> decodeITXDLuminanceMipChain(const std::array<uint32_t,6>& descriptor,
    uint32_t width,uint32_t height,std::span<const uint8_t> storage);
// Stored ITXD resource description BEFORE original CPU address relocation.
// This reads asset storage, not GPU commands. The caller owns and bounds the
// original allocation and verifies the raster's format/dimensions separately.
// Only tiled, unsigned XYZW BC2/BC3, one level, base-zero, 8-in-16 is qualified.
std::vector<uint8_t> decodeITXDBC2Base(const std::array<uint32_t,6>& descriptor,
    uint32_t width,uint32_t height,std::span<const uint8_t> storage);
std::vector<uint8_t> decodeITXDBC3Base(const std::array<uint32_t,6>& descriptor,
    uint32_t width,uint32_t height,std::span<const uint8_t> storage);
// Tiled BC1/BC2/BC3, base-zero, 8-in-16, unsigned XYZW; optional packed mips.
// format is the original low format byte (52/53/54 hex), independently supplied
// from the raster metadata. Returns every authored level, without generating any.
std::vector<std::vector<uint8_t>> decodeITXDBCMipChain(const std::array<uint32_t,6>& descriptor,
    uint32_t format,uint32_t width,uint32_t height,std::span<const uint8_t> storage);
// Descriptor-driven tiled 8888,8-in-32,ZYXW, power-of-two 32..1024.
// Base-only or authored mips through the first packed 16-texel level;
// rejects padded pitch, deeper shared tails and other fetch controls.
// Original 64/256/512-square mip chains have independent fixture coverage.
// Also accepts the authored 16x16 base-only profile with exact 32-texel pitch
// and 4096-byte allocation. Other small dimensions and additional mips on this
// 16x16 profile reject.
std::vector<std::vector<uint8_t>> decodeITXDCandyRGBA8(const std::array<uint32_t,6>& descriptor,
    uint32_t width,uint32_t height,std::span<const uint8_t> storage);
// Exact simpsons_palette profile:64x64 tiled 8888,8-in-32,ZYXW,one level.
std::vector<uint8_t> decodeITXDPaletteRGBA8(const std::array<uint32_t,6>& descriptor,
    uint32_t width,uint32_t height,std::span<const uint8_t> storage);
}
