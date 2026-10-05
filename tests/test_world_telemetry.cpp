#include "runtime/world_telemetry.h"
#include "runtime/runtime.h"
#include "runtime/guest_memory.h"
#include <bit>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
using namespace Simpsons;
size_t checks{};
void need(bool value, const char* why) { ++checks; if(!value) throw Failure(why); }
constexpr uint32_t manager = 0x20000, camera = 0x21000, frame = 0x22000,
                   raster = 0x23000, viewportGlobal = 0x82D08B10;
bool has(const std::string& json, const char* fragment) {
    return json.find(fragment) != std::string::npos;
}
void setFloat(uint8_t* base, uint32_t at, float value) { PPC_STORE_U32(at, std::bit_cast<uint32_t>(value)); }

void run() {
    Runtime runtime;
    uint8_t* base = runtime.base;
    need(has(worldTelemetryJson(runtime, 1), "\"available\":false"), "Unmapped world state was reported available");
    runtime.map(0x20000, 0x4000, true, "world telemetry fixture");
    runtime.map(0x82D08000, 0x1000, true, "viewport global fixture");
    PPC_STORE_U32(viewportGlobal, manager);
    PPC_STORE_U32(manager, 0x820B6DEC);
    PPC_STORE_U32(manager + 8, 0x820B6DE8);
    PPC_STORE_U32(manager + 0x128, 4);
    PPC_STORE_U32(manager + 0x14, camera);
    PPC_STORE_U32(camera + 4, frame);
    PPC_STORE_U32(camera + 0x60, raster);
    PPC_STORE_U32(raster + 0xC, 1280);
    PPC_STORE_U32(raster + 0x10, 720);
    PPC_STORE_U32(frame + 0xA0, frame);
    PPC_STORE_U32(frame + 0x4C, 0x3F800000);
    setFloat(base, frame + 0x40, 1.25f);
    setFloat(base, frame + 0x44, -2.5f);
    setFloat(base, frame + 0x48, 3.75f);
    const auto before = std::string(reinterpret_cast<const char*>(runtime.pointer(frame, 0xA4, false)), 0xA4);
    auto json = worldTelemetryJson(runtime, 12);
    need(has(json, "\"available\":true"), "Qualified camera frame was unavailable");
    need(has(json, "\"camera_eye\":[1.25,-2.5,3.75]"), "Camera translation was decoded incorrectly");
    need(has(json, "\"presentation\":12"), "Capture presentation was not attached to world observation");
    need(worldSceneCameraId(runtime) == camera, "Scene camera identity was not qualified");
    need(has(json, "\"entities_available\":false,\"objective_available\":false"),
         "Unqualified entities or objectives were claimed");
    need(!std::memcmp(runtime.pointer(frame, 0xA4, false), before.data(), before.size()),
         "Read-only world sampler changed guest frame bytes");

    setFloat(base, frame + 0x40, 4.5f);
    json = worldTelemetryJson(runtime, 13);
    need(has(json, "\"camera_eye\":[4.5,-2.5,3.75]"), "New camera position was not sampled");
    PPC_STORE_U32(frame + 0xA0, camera);
    need(has(worldTelemetryJson(runtime, 14), "\"available\":false"), "Foreign frame owner was accepted");
    need(worldSceneCameraId(runtime) == 0, "Foreign frame owner supplied a scene camera identity");
    PPC_STORE_U32(frame + 0xA0, frame);
    PPC_STORE_U32(frame + 0x40, 0x7FC00000);
    need(has(worldTelemetryJson(runtime, 15), "\"available\":false"), "NaN camera coordinate was accepted");
    setFloat(base, frame + 0x40, 4.5f);
    PPC_STORE_U32(raster + 0xC, 640);
    need(has(worldTelemetryJson(runtime, 16), "\"available\":false"), "Half-width camera was accepted as scene slot zero");
}
}

int main() {
    try { run(); std::printf("PASS original world-camera telemetry: %zu checks\n", checks); return 0; }
    catch(const std::exception& error) { std::fprintf(stderr, "FAIL world telemetry: %s\n", error.what()); return 1; }
}
