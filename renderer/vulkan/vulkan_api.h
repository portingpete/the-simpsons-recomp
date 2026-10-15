#pragma once
// Vulkan entry points come from volk (loaded from vulkan-1.dll at runtime), so
// the game links no Vulkan SDK import library and still starts on systems
// without a Vulkan driver when the D3D11 renderer is selected.
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <windows.h>
#include "volk.h"
#ifndef VMA_STATIC_VULKAN_FUNCTIONS
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#endif
#ifndef VMA_DYNAMIC_VULKAN_FUNCTIONS
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#endif
#include "vk_mem_alloc.h"
#include <cstdint>

namespace Simpsons::Graphics::Vulkan {
const char* resultName(VkResult result) noexcept;
// Throws Simpsons::Graphics::Error with the operation and VkResult name.
[[noreturn]] void fail(VkResult result,const char* operation);
inline void check(VkResult result,const char* operation) {
    if(result<0) fail(result,operation);
}
}
