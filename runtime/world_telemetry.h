#pragma once
#include <cstdint>
#include <string>

namespace Simpsons {
class Runtime;

// Read-only, best-effort observation for the exact completed-front capture.
// The caller serializes this object as telemetry.world in that frame's metadata.
std::string worldTelemetryJson(Runtime& runtime, uint64_t presentation);
// Same validated full-size scene camera identity for depth-copy provenance.
// Returns zero when unavailable; never treats a loading camera as scene slot 0.
uint32_t worldSceneCameraId(Runtime& runtime);
}
