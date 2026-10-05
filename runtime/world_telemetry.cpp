#include "world_telemetry.h"
#include "runtime.h"
#include "guest_memory.h"
#include <bit>
#include <cmath>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>

namespace Simpsons {
namespace {
constexpr uint32_t viewportManagerGlobal = 0x82D08B10;
constexpr const char* source = "original_viewport_slot0_camera_frame";

std::string unavailable(uint64_t presentation) {
    return "{\"available\":false,\"source\":\"" + std::string(source) +
        "\",\"presentation\":" + std::to_string(presentation) +
        ",\"entities_available\":false,\"objective_available\":false}";
}
struct CameraSample { uint32_t id; float eye[3]; };
std::optional<CameraSample> sampleSceneCamera(Runtime& runtime) {
    // The original viewport manager owns three camera slots. Slot zero is the
    // full-size scene camera; the other two render private half-width views.
    // Its root frame's fourth matrix row is a world-space position. This is a
    // camera observation, not an entity, collision, or objective inventory.
    try {
        if(active != &runtime || !runtime.base) return std::nullopt;
        uint8_t* base = runtime.base;
        runtime.pointer(viewportManagerGlobal, 4, false);
        const uint32_t manager = PPC_LOAD_U32(viewportManagerGlobal);
        if(!manager || (manager & 3)) return std::nullopt;
        runtime.pointer(manager, 0x12C, false);
        if(PPC_LOAD_U32(manager) != 0x820B6DEC ||
           PPC_LOAD_U32(manager + 8) != 0x820B6DE8 ||
           PPC_LOAD_U32(manager + 0x128) != 4) return std::nullopt;

        const uint32_t camera = PPC_LOAD_U32(manager + 0x14);
        if(!camera || (camera & 3)) return std::nullopt;
        runtime.pointer(camera, 0x8C, false);
        const uint32_t raster = PPC_LOAD_U32(camera + 0x60);
        const uint32_t frame = PPC_LOAD_U32(camera + 4);
        if(!raster || !frame || (raster & 3) || (frame & 3)) return std::nullopt;
        runtime.pointer(raster, 0x14, false);
        runtime.pointer(frame, 0xA4, false);
        if(PPC_LOAD_U32(raster + 0xC) != 1280 || PPC_LOAD_U32(raster + 0x10) != 720 ||
           PPC_LOAD_U32(frame + 0xA0) != frame) return std::nullopt;

        // The original frame's 4x4 matrix at +10 has homogeneous lanes and
        // translation in its fourth row, at +40/+44/+48. Do not infer a
        // forward heading from the basis until its game-specific sign is proved.
        if(PPC_LOAD_U32(frame + 0x1C) || PPC_LOAD_U32(frame + 0x2C) ||
           PPC_LOAD_U32(frame + 0x3C) || PPC_LOAD_U32(frame + 0x4C) != 0x3F800000)
            return std::nullopt;
        CameraSample sample{camera,{}};
        for(unsigned i = 0; i < 3; ++i) {
            sample.eye[i] = std::bit_cast<float>(PPC_LOAD_U32(frame + 0x40 + 4*i));
            if(!std::isfinite(sample.eye[i])) return std::nullopt;
        }
        return sample;
    } catch(const std::exception&) {
        // An optional diagnostic must never make the original game fail.
        return std::nullopt;
    }
}
}

uint32_t worldSceneCameraId(Runtime& runtime) {
    const auto sample = sampleSceneCamera(runtime);
    return sample ? sample->id : 0;
}

std::string worldTelemetryJson(Runtime& runtime, uint64_t presentation) {
    const auto sample = sampleSceneCamera(runtime);
    if(!sample) return unavailable(presentation);
    std::ostringstream json;
    json.imbue(std::locale::classic());
    json << std::setprecision(9)
         << "{\"available\":true,\"source\":\"" << source
         << "\",\"presentation\":" << presentation
         << ",\"camera_eye\":[" << sample->eye[0] << ',' << sample->eye[1] << ',' << sample->eye[2]
         << "],\"entities_available\":false,\"objective_available\":false}";
    return json.str();
}
}
