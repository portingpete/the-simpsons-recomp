#include "scene_depth_telemetry.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <sstream>

namespace Simpsons {
namespace {
constexpr uint32_t gridWidth = 16, gridHeight = 9;
constexpr const char* source = "original_viewport_slot0_post_depth_copy_D32";
constexpr const char* encoding = "reversed_depth_log_u8_0_far_255_near";
static_assert(std::endian::native == std::endian::little);

std::string unavailable(uint64_t presentation) {
    return "{\"available\":false,\"source\":\"" + std::string(source) +
           "\",\"presentation\":" + std::to_string(presentation) + "}";
}
}

std::string sceneDepthGridJson(std::span<const uint8_t> depthBytes,
                               uint64_t presentation, bool fresh,
                               SceneDepthGridStats* stats,uint32_t imageWidth,uint32_t imageHeight) {
    if(stats) *stats = {};
    if(!fresh || !imageWidth || !imageHeight || imageWidth>16384 || imageHeight>16384 ||
       depthBytes.size() != size_t(imageWidth)*imageHeight*8) return unavailable(presentation);
    std::array<uint32_t,gridWidth*gridHeight> rawBits{};
    std::array<uint8_t,gridWidth*gridHeight> codes{};
    float rawMin=1.0f,rawMax=0.0f;
    const double logScale=std::log1p(65535.0);
    std::ostringstream json;
    json << "{\"available\":true,\"source\":\"" << source
         << "\",\"presentation\":" << presentation
         << ",\"width\":" << gridWidth << ",\"height\":" << gridHeight
         << ",\"encoding\":\"" << encoding << "\",\"values\":[";
    for(uint32_t row = 0; row < gridHeight; ++row) {
        if(row) json << ',';
        json << '[';
        const uint32_t y = (2*row + 1)*imageHeight/(2*gridHeight);
        for(uint32_t col = 0; col < gridWidth; ++col) {
            if(col) json << ',';
            const uint32_t x = (2*col + 1)*imageWidth/(2*gridWidth);
            const size_t offset = (size_t(y)*imageWidth + x)*8;
            float depth{};
            std::memcpy(&depth, depthBytes.data() + offset, sizeof(depth));
            if(!std::isfinite(depth) || depth < 0 || depth > 1)
                return unavailable(presentation);
            rawMin=std::min(rawMin,depth);rawMax=std::max(rawMax,depth);
            const auto cell=row*gridWidth+col;
            rawBits[cell]=std::bit_cast<uint32_t>(depth);
            // Native reversed D32 depths in this scene cluster near zero.
            // This monotonic mapping retains useful screen-space variation,
            // while zero and one keep their exact far/near endpoints.
            const auto quantized=uint32_t(255.0*std::log1p(65535.0*double(depth))/logScale+0.5);
            codes[cell]=uint8_t(std::min(quantized,255u));
            json << unsigned(codes[cell]);
        }
        json << ']';
    }
    json << "]}";
    if(stats) {
        std::sort(rawBits.begin(),rawBits.end());
        std::sort(codes.begin(),codes.end());
        stats->rawMin=rawMin;stats->rawMax=rawMax;
        stats->distinctRaw=uint32_t(std::unique(rawBits.begin(),rawBits.end())-rawBits.begin());
        stats->distinctEncoded=uint32_t(std::unique(codes.begin(),codes.end())-codes.begin());
    }
    return json.str();
}
}
