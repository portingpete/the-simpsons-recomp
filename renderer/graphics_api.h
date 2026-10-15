#pragma once
#include <string_view>

namespace Simpsons::Graphics {
// Device behind NativeBackend. D3D11 is the default; Vulkan runs the same
// renderer through renderer/vulkan's native implementation of the D3D11
// interface subset, with every HLSL shader compiled to a checked SPIR-V twin.
enum class GraphicsApi {D3D11,Vulkan};
// Process-wide selection, frozen when the first backend is created. The
// environment variable SIMPSONS_RENDERER=d3d11|vulkan seeds the default so
// tests and tools select the device without code changes; the --renderer
// launch option overrides it.
GraphicsApi graphicsApi() noexcept;
void selectGraphicsApi(GraphicsApi api) noexcept;
bool parseGraphicsApi(std::string_view text,GraphicsApi& api) noexcept;
const char* graphicsApiName(GraphicsApi api) noexcept;
// False when the build excludes the Vulkan renderer (SIMPSONS_VULKAN=OFF).
bool vulkanAvailable() noexcept;
}
