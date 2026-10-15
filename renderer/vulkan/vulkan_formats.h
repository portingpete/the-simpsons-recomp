#pragma once
#include "vulkan_api.h"
#include <dxgiformat.h>

namespace Simpsons::Graphics::Vulkan {
// Storage of one DXGI resource format. TYPELESS formats choose a concrete
// member of their family and are created mutable for typed views.
struct FormatInfo {
    VkFormat format=VK_FORMAT_UNDEFINED;
    uint32_t blockBytes=0,blockWidth=1,blockHeight=1;
    VkImageAspectFlags aspects=0;
    bool typeless=false,compressed=false,integer=false;
    bool depth() const {return (aspects&VK_IMAGE_ASPECT_DEPTH_BIT)!=0;}
    bool stencil() const {return (aspects&VK_IMAGE_ASPECT_STENCIL_BIT)!=0;}
};
// bindFlags decide the depth or color member of R32/R24G8/R32G8X24/R16 TYPELESS.
FormatInfo resourceFormat(DXGI_FORMAT format,uint32_t bindFlags);
// A typed view of a resource. Depth/stencil sampling views select one aspect
// of the combined image and reproduce D3D's component placement (stencil in G).
struct ViewFormat {
    VkFormat format=VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect=0;
    VkComponentMapping swizzle{};
    bool integer=false,signedInteger=false;
};
ViewFormat viewFormat(DXGI_FORMAT view,const FormatInfo& resource);
VkFormat vertexFormat(DXGI_FORMAT format);
uint32_t vertexFormatBytes(DXGI_FORMAT format);
bool sameFamily(DXGI_FORMAT a,DXGI_FORMAT b);
}
