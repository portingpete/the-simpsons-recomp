#include "runtime/scene_depth_telemetry.h"
#include <bit>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
size_t checks{};
void need(bool value, const char* why) { ++checks; if(!value) throw std::runtime_error(why); }
bool has(const std::string& text, const char* fragment) { return text.find(fragment) != std::string::npos; }
void set(std::vector<uint8_t>& bytes, unsigned col, unsigned row, float value) {
    const size_t x = 40 + 80*col, y = 40 + 80*row;
    const size_t at = (y*1280 + x)*8;
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    std::memcpy(bytes.data() + at, &bits, 4);
}
void run() {
    std::vector<uint8_t> bytes(size_t(1280)*720*8);
    set(bytes, 0, 0, 1.0f);
    set(bytes, 1, 0, 0.5f);
    set(bytes, 15, 8, 0.25f);
    Simpsons::SceneDepthGridStats stats;
    const auto json = Simpsons::sceneDepthGridJson(bytes, 91, true, &stats);
    need(has(json, "\"available\":true"), "Fresh full-size D32 copy was unavailable");
    need(has(json, "\"presentation\":91,\"width\":16,\"height\":9"), "Grid extent or presentation differs");
    need(has(json, "\"encoding\":\"reversed_depth_log_u8_0_far_255_near\""), "Log reversed-depth semantics missing");
    need(has(json, "\"values\":[[255,239,0"), "Top-left cells were sampled incorrectly");
    need(has(json, ",223]]}"), "Bottom-right cell was sampled incorrectly");
    need(stats.rawMin == 0 && stats.rawMax == 1 && stats.distinctRaw == 4 && stats.distinctEncoded == 4,
         "Raw and encoded depth statistics differ from sampled cells");
    need(has(Simpsons::sceneDepthGridJson(bytes, 92, false), "\"available\":false"),
         "Stale depth copy was accepted");
    need(has(Simpsons::sceneDepthGridJson(std::span<const uint8_t>(bytes.data(), bytes.size()-8), 93, true),
             "\"available\":false"), "Truncated depth copy was accepted");
    set(bytes, 0, 0, std::bit_cast<float>(0x7FC00000u));
    need(has(Simpsons::sceneDepthGridJson(bytes, 94, true), "\"available\":false"),
         "NaN depth was accepted");
    set(bytes, 0, 0, -0.1f);
    need(has(Simpsons::sceneDepthGridJson(bytes, 95, true), "\"available\":false"),
         "Out-of-range depth was accepted");
    std::vector<uint8_t> highResolution(size_t(1920)*1080*8);
    const float nearDepth=1;
    const size_t center=(size_t(60)*1920+60)*8;
    std::memcpy(highResolution.data()+center,&nearDepth,sizeof(nearDepth));
    const auto scaled=Simpsons::sceneDepthGridJson(highResolution,96,true,&stats,1920,1080);
    need(has(scaled,"\"values\":[[255,0")&&stats.distinctRaw==2,"Depth grid did not sample the actual internal resolution");
}
}
int main() {
    try { run(); std::printf("PASS scene depth grid: %zu checks\n", checks); return 0; }
    catch(const std::exception& error) { std::fprintf(stderr, "FAIL scene depth grid: %s\n", error.what()); return 1; }
}
