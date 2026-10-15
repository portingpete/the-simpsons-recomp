// Single translation unit for the vendored header-only implementations
// (third_party/vulkan): the volk loader and the Vulkan Memory Allocator.
#define VOLK_IMPLEMENTATION
#define VMA_IMPLEMENTATION
#include "vulkan_api.h"
