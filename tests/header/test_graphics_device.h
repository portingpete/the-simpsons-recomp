#pragma once
// TEST ONLY: probes that swap a backend's device must create a device of the
// selected graphics API (SIMPSONS_RENDERER), or a Vulkan suite run would
// silently test D3D11. The D3D11 debug layer is a D3D11-only diagnostic.
#include "renderer/graphics_api.h"
#if SIMPSONS_VULKAN
#include "renderer/vulkan/vulkan_device_factory.h"
#endif
#include <d3d11.h>

namespace Simpsons::Graphics {
inline bool testUsesVulkan() {return graphicsApi()==GraphicsApi::Vulkan;}
inline HRESULT createTestDevice(bool hardware,UINT flags,ID3D11Device** device,D3D_FEATURE_LEVEL* level,ID3D11DeviceContext** context) {
#if SIMPSONS_VULKAN
    if(testUsesVulkan()) return Vulkan::createDevice(device,level,context);
#endif
    const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
    const D3D_DRIVER_TYPE driver=hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP;
    HRESULT result=D3D11CreateDevice(nullptr,driver,nullptr,flags,levels,2,D3D11_SDK_VERSION,device,level,context);
    if(result==E_INVALIDARG) result=D3D11CreateDevice(nullptr,driver,nullptr,flags,levels+1,1,D3D11_SDK_VERSION,device,level,context);
    return result;
}
}
