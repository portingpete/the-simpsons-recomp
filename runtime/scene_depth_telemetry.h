#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace Simpsons {

struct SceneDepthGridStats {
    float rawMin{}, rawMax{};
    uint32_t distinctRaw{}, distinctEncoded{};
};

// Encode a bounded screen-space grid from the native post-scene D32/S8 copy.
// `fresh` must be established by a new original viewport depth-copy receipt
// from the full-size scene camera in this same presentation.
std::string sceneDepthGridJson(std::span<const uint8_t> depthBytes,
                               uint64_t presentation, bool fresh,
                               SceneDepthGridStats* stats = nullptr,uint32_t imageWidth=1280,uint32_t imageHeight=720);
}
