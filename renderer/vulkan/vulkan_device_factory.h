#pragma once
// Entry points of the native Vulkan device, without its Vulkan/VMA headers.
#include <d3d11.h>
#include <string>

namespace Simpsons::Graphics::Vulkan {
// Creates the Vulkan-backed device and returns its D3D11 interfaces.
HRESULT createDevice(ID3D11Device** device,D3D_FEATURE_LEVEL* level,ID3D11DeviceContext** context);
// Description of the adapter behind a Vulkan-backed device, or empty.
std::string deviceDescription(ID3D11Device* device);
bool isVulkanDevice(ID3D11Device* device);
}
