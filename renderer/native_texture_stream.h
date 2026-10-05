#pragma once
#include "native_backend.h"
#include <string>

namespace Simpsons::Graphics {
struct SerializedTexture {
    std::string name,maskName;
    uint32_t sampler{},rasterFormat{},nativeFormat{};
    uint16_t width{},height{};
    uint8_t depthField{},levels{},type{},flags{};
    TextureFormat format{};
    std::vector<uint8_t> blocks;
};
// The contents of the native stream's struct chunk: BE 88-byte header,
// LE level size, then linear endian-swapped blocks. Chunk traversal/extensions
// belong to the original CPU stream/plugin code. Only the proven loading profile
// is supported; other formats/mips/flags fail explicitly.
SerializedTexture decodeNativeTextureStruct(std::span<const uint8_t> bytes);
}
