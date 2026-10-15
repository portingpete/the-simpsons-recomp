#include "vulkan_formats.h"
#include "renderer/native_backend.h"
#include <cstdio>

namespace Simpsons::Graphics::Vulkan {
namespace {
constexpr VkImageAspectFlags C=VK_IMAGE_ASPECT_COLOR_BIT,D=VK_IMAGE_ASPECT_DEPTH_BIT,S=VK_IMAGE_ASPECT_STENCIL_BIT;
// family groups TYPELESS formats with their typed members (D3D view casting
// and CopyResource compatibility).
struct Entry {DXGI_FORMAT dxgi;VkFormat vk;uint32_t bytes,bw,bh;VkImageAspectFlags aspects;bool typeless,integer,signedInteger;uint32_t family;};
// Vulkan packs A2B10G10R10 and B10G11R11 from the high bit down, which is
// D3D's R10G10B10A2/R11G11B10 low-bit-first layout.
// https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#formats-packed
constexpr Entry entries[]={
    {DXGI_FORMAT_R32G32B32A32_TYPELESS,VK_FORMAT_R32G32B32A32_SFLOAT,16,1,1,C,true,false,false,1},
    {DXGI_FORMAT_R32G32B32A32_FLOAT,VK_FORMAT_R32G32B32A32_SFLOAT,16,1,1,C,false,false,false,1},
    {DXGI_FORMAT_R32G32B32A32_UINT,VK_FORMAT_R32G32B32A32_UINT,16,1,1,C,false,true,false,1},
    {DXGI_FORMAT_R32G32B32A32_SINT,VK_FORMAT_R32G32B32A32_SINT,16,1,1,C,false,true,true,1},
    {DXGI_FORMAT_R32G32B32_TYPELESS,VK_FORMAT_R32G32B32_SFLOAT,12,1,1,C,true,false,false,2},
    {DXGI_FORMAT_R32G32B32_FLOAT,VK_FORMAT_R32G32B32_SFLOAT,12,1,1,C,false,false,false,2},
    {DXGI_FORMAT_R32G32B32_UINT,VK_FORMAT_R32G32B32_UINT,12,1,1,C,false,true,false,2},
    {DXGI_FORMAT_R32G32B32_SINT,VK_FORMAT_R32G32B32_SINT,12,1,1,C,false,true,true,2},
    {DXGI_FORMAT_R16G16B16A16_TYPELESS,VK_FORMAT_R16G16B16A16_SFLOAT,8,1,1,C,true,false,false,3},
    {DXGI_FORMAT_R16G16B16A16_FLOAT,VK_FORMAT_R16G16B16A16_SFLOAT,8,1,1,C,false,false,false,3},
    {DXGI_FORMAT_R16G16B16A16_UNORM,VK_FORMAT_R16G16B16A16_UNORM,8,1,1,C,false,false,false,3},
    {DXGI_FORMAT_R16G16B16A16_UINT,VK_FORMAT_R16G16B16A16_UINT,8,1,1,C,false,true,false,3},
    {DXGI_FORMAT_R16G16B16A16_SNORM,VK_FORMAT_R16G16B16A16_SNORM,8,1,1,C,false,false,false,3},
    {DXGI_FORMAT_R16G16B16A16_SINT,VK_FORMAT_R16G16B16A16_SINT,8,1,1,C,false,true,true,3},
    {DXGI_FORMAT_R32G32_TYPELESS,VK_FORMAT_R32G32_SFLOAT,8,1,1,C,true,false,false,4},
    {DXGI_FORMAT_R32G32_FLOAT,VK_FORMAT_R32G32_SFLOAT,8,1,1,C,false,false,false,4},
    {DXGI_FORMAT_R32G32_UINT,VK_FORMAT_R32G32_UINT,8,1,1,C,false,true,false,4},
    {DXGI_FORMAT_R32G32_SINT,VK_FORMAT_R32G32_SINT,8,1,1,C,false,true,true,4},
    {DXGI_FORMAT_R32G8X24_TYPELESS,VK_FORMAT_D32_SFLOAT_S8_UINT,8,1,1,D|S,true,false,false,5},
    {DXGI_FORMAT_D32_FLOAT_S8X24_UINT,VK_FORMAT_D32_SFLOAT_S8_UINT,8,1,1,D|S,false,false,false,5},
    {DXGI_FORMAT_R10G10B10A2_TYPELESS,VK_FORMAT_A2B10G10R10_UNORM_PACK32,4,1,1,C,true,false,false,6},
    {DXGI_FORMAT_R10G10B10A2_UNORM,VK_FORMAT_A2B10G10R10_UNORM_PACK32,4,1,1,C,false,false,false,6},
    {DXGI_FORMAT_R10G10B10A2_UINT,VK_FORMAT_A2B10G10R10_UINT_PACK32,4,1,1,C,false,true,false,6},
    {DXGI_FORMAT_R11G11B10_FLOAT,VK_FORMAT_B10G11R11_UFLOAT_PACK32,4,1,1,C,false,false,false,7},
    {DXGI_FORMAT_R8G8B8A8_TYPELESS,VK_FORMAT_R8G8B8A8_UNORM,4,1,1,C,true,false,false,8},
    {DXGI_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM,4,1,1,C,false,false,false,8},
    {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,VK_FORMAT_R8G8B8A8_SRGB,4,1,1,C,false,false,false,8},
    {DXGI_FORMAT_R8G8B8A8_UINT,VK_FORMAT_R8G8B8A8_UINT,4,1,1,C,false,true,false,8},
    {DXGI_FORMAT_R8G8B8A8_SNORM,VK_FORMAT_R8G8B8A8_SNORM,4,1,1,C,false,false,false,8},
    {DXGI_FORMAT_R8G8B8A8_SINT,VK_FORMAT_R8G8B8A8_SINT,4,1,1,C,false,true,true,8},
    {DXGI_FORMAT_R16G16_TYPELESS,VK_FORMAT_R16G16_SFLOAT,4,1,1,C,true,false,false,9},
    {DXGI_FORMAT_R16G16_FLOAT,VK_FORMAT_R16G16_SFLOAT,4,1,1,C,false,false,false,9},
    {DXGI_FORMAT_R16G16_UNORM,VK_FORMAT_R16G16_UNORM,4,1,1,C,false,false,false,9},
    {DXGI_FORMAT_R16G16_UINT,VK_FORMAT_R16G16_UINT,4,1,1,C,false,true,false,9},
    {DXGI_FORMAT_R16G16_SNORM,VK_FORMAT_R16G16_SNORM,4,1,1,C,false,false,false,9},
    {DXGI_FORMAT_R16G16_SINT,VK_FORMAT_R16G16_SINT,4,1,1,C,false,true,true,9},
    {DXGI_FORMAT_R32_TYPELESS,VK_FORMAT_R32_SFLOAT,4,1,1,C,true,false,false,10},
    {DXGI_FORMAT_D32_FLOAT,VK_FORMAT_D32_SFLOAT,4,1,1,D,false,false,false,10},
    {DXGI_FORMAT_R32_FLOAT,VK_FORMAT_R32_SFLOAT,4,1,1,C,false,false,false,10},
    {DXGI_FORMAT_R32_UINT,VK_FORMAT_R32_UINT,4,1,1,C,false,true,false,10},
    {DXGI_FORMAT_R32_SINT,VK_FORMAT_R32_SINT,4,1,1,C,false,true,true,10},
    {DXGI_FORMAT_R24G8_TYPELESS,VK_FORMAT_D24_UNORM_S8_UINT,4,1,1,D|S,true,false,false,11},
    {DXGI_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D24_UNORM_S8_UINT,4,1,1,D|S,false,false,false,11},
    {DXGI_FORMAT_R8G8_TYPELESS,VK_FORMAT_R8G8_UNORM,2,1,1,C,true,false,false,12},
    {DXGI_FORMAT_R8G8_UNORM,VK_FORMAT_R8G8_UNORM,2,1,1,C,false,false,false,12},
    {DXGI_FORMAT_R8G8_UINT,VK_FORMAT_R8G8_UINT,2,1,1,C,false,true,false,12},
    {DXGI_FORMAT_R8G8_SNORM,VK_FORMAT_R8G8_SNORM,2,1,1,C,false,false,false,12},
    {DXGI_FORMAT_R8G8_SINT,VK_FORMAT_R8G8_SINT,2,1,1,C,false,true,true,12},
    {DXGI_FORMAT_R16_TYPELESS,VK_FORMAT_R16_SFLOAT,2,1,1,C,true,false,false,13},
    {DXGI_FORMAT_R16_FLOAT,VK_FORMAT_R16_SFLOAT,2,1,1,C,false,false,false,13},
    {DXGI_FORMAT_D16_UNORM,VK_FORMAT_D16_UNORM,2,1,1,D,false,false,false,13},
    {DXGI_FORMAT_R16_UNORM,VK_FORMAT_R16_UNORM,2,1,1,C,false,false,false,13},
    {DXGI_FORMAT_R16_UINT,VK_FORMAT_R16_UINT,2,1,1,C,false,true,false,13},
    {DXGI_FORMAT_R16_SNORM,VK_FORMAT_R16_SNORM,2,1,1,C,false,false,false,13},
    {DXGI_FORMAT_R16_SINT,VK_FORMAT_R16_SINT,2,1,1,C,false,true,true,13},
    {DXGI_FORMAT_R8_TYPELESS,VK_FORMAT_R8_UNORM,1,1,1,C,true,false,false,14},
    {DXGI_FORMAT_R8_UNORM,VK_FORMAT_R8_UNORM,1,1,1,C,false,false,false,14},
    {DXGI_FORMAT_R8_UINT,VK_FORMAT_R8_UINT,1,1,1,C,false,true,false,14},
    {DXGI_FORMAT_R8_SNORM,VK_FORMAT_R8_SNORM,1,1,1,C,false,false,false,14},
    {DXGI_FORMAT_R8_SINT,VK_FORMAT_R8_SINT,1,1,1,C,false,true,true,14},
    {DXGI_FORMAT_A8_UNORM,VK_FORMAT_R8_UNORM,1,1,1,C,false,false,false,15},
    {DXGI_FORMAT_BC1_TYPELESS,VK_FORMAT_BC1_RGBA_UNORM_BLOCK,8,4,4,C,true,false,false,16},
    {DXGI_FORMAT_BC1_UNORM,VK_FORMAT_BC1_RGBA_UNORM_BLOCK,8,4,4,C,false,false,false,16},
    {DXGI_FORMAT_BC1_UNORM_SRGB,VK_FORMAT_BC1_RGBA_SRGB_BLOCK,8,4,4,C,false,false,false,16},
    {DXGI_FORMAT_BC2_TYPELESS,VK_FORMAT_BC2_UNORM_BLOCK,16,4,4,C,true,false,false,17},
    {DXGI_FORMAT_BC2_UNORM,VK_FORMAT_BC2_UNORM_BLOCK,16,4,4,C,false,false,false,17},
    {DXGI_FORMAT_BC2_UNORM_SRGB,VK_FORMAT_BC2_SRGB_BLOCK,16,4,4,C,false,false,false,17},
    {DXGI_FORMAT_BC3_TYPELESS,VK_FORMAT_BC3_UNORM_BLOCK,16,4,4,C,true,false,false,18},
    {DXGI_FORMAT_BC3_UNORM,VK_FORMAT_BC3_UNORM_BLOCK,16,4,4,C,false,false,false,18},
    {DXGI_FORMAT_BC3_UNORM_SRGB,VK_FORMAT_BC3_SRGB_BLOCK,16,4,4,C,false,false,false,18},
    {DXGI_FORMAT_BC4_TYPELESS,VK_FORMAT_BC4_UNORM_BLOCK,8,4,4,C,true,false,false,19},
    {DXGI_FORMAT_BC4_UNORM,VK_FORMAT_BC4_UNORM_BLOCK,8,4,4,C,false,false,false,19},
    {DXGI_FORMAT_BC4_SNORM,VK_FORMAT_BC4_SNORM_BLOCK,8,4,4,C,false,false,false,19},
    {DXGI_FORMAT_BC5_TYPELESS,VK_FORMAT_BC5_UNORM_BLOCK,16,4,4,C,true,false,false,20},
    {DXGI_FORMAT_BC5_UNORM,VK_FORMAT_BC5_UNORM_BLOCK,16,4,4,C,false,false,false,20},
    {DXGI_FORMAT_BC5_SNORM,VK_FORMAT_BC5_SNORM_BLOCK,16,4,4,C,false,false,false,20},
    {DXGI_FORMAT_B8G8R8A8_TYPELESS,VK_FORMAT_B8G8R8A8_UNORM,4,1,1,C,true,false,false,21},
    {DXGI_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_B8G8R8A8_UNORM,4,1,1,C,false,false,false,21},
    {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,VK_FORMAT_B8G8R8A8_SRGB,4,1,1,C,false,false,false,21},
    {DXGI_FORMAT_B8G8R8X8_TYPELESS,VK_FORMAT_B8G8R8A8_UNORM,4,1,1,C,true,false,false,22},
    {DXGI_FORMAT_B8G8R8X8_UNORM,VK_FORMAT_B8G8R8A8_UNORM,4,1,1,C,false,false,false,22},
    {DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,VK_FORMAT_B8G8R8A8_SRGB,4,1,1,C,false,false,false,22},
    {DXGI_FORMAT_B5G6R5_UNORM,VK_FORMAT_R5G6B5_UNORM_PACK16,2,1,1,C,false,false,false,23},
    {DXGI_FORMAT_BC7_TYPELESS,VK_FORMAT_BC7_UNORM_BLOCK,16,4,4,C,true,false,false,24},
    {DXGI_FORMAT_BC7_UNORM,VK_FORMAT_BC7_UNORM_BLOCK,16,4,4,C,false,false,false,24},
    {DXGI_FORMAT_BC7_UNORM_SRGB,VK_FORMAT_BC7_SRGB_BLOCK,16,4,4,C,false,false,false,24},
};
const Entry* find(DXGI_FORMAT format) {
    for(const auto& entry:entries) if(entry.dxgi==format) return &entry;
    return nullptr;
}
[[noreturn]] void unsupported(const char* what,DXGI_FORMAT format) {
    char message[160];std::snprintf(message,sizeof(message),"Native Vulkan has no mapping for %s DXGI format %d",what,int(format));
    throw Error(message);
}
constexpr VkComponentMapping identity{VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY};
}

FormatInfo resourceFormat(DXGI_FORMAT format,uint32_t bindFlags) {
    const Entry* entry=find(format);
    if(!entry) unsupported("resource",format);
    FormatInfo info;
    info.format=entry->vk;info.blockBytes=entry->bytes;info.blockWidth=entry->bw;info.blockHeight=entry->bh;
    info.aspects=entry->aspects;info.typeless=entry->typeless;info.compressed=entry->bw>1;info.integer=entry->integer;
    // A depth-bindable typeless color family stores its depth member.
    constexpr uint32_t depthBind=0x40; // D3D11_BIND_DEPTH_STENCIL
    if(entry->typeless && (bindFlags&depthBind)) {
        if(format==DXGI_FORMAT_R32_TYPELESS){info.format=VK_FORMAT_D32_SFLOAT;info.aspects=D;}
        else if(format==DXGI_FORMAT_R16_TYPELESS){info.format=VK_FORMAT_D16_UNORM;info.aspects=D;}
    }
    return info;
}

ViewFormat viewFormat(DXGI_FORMAT view,const FormatInfo& resource) {
    ViewFormat out;out.swizzle=identity;
    const VkComponentMapping depthSwizzle{VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ONE};
    const VkComponentMapping stencilSwizzle{VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ONE};
    if(resource.depth() || resource.stencil()) {
        out.format=resource.format;
        switch(view) {
        case DXGI_FORMAT_UNKNOWN:case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_D32_FLOAT:case DXGI_FORMAT_D16_UNORM:
            out.aspect=resource.aspects;return out;
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        case DXGI_FORMAT_R32_FLOAT:case DXGI_FORMAT_R16_UNORM:
            if(!resource.depth()) unsupported("depth view",view);
            out.aspect=D;out.swizzle=depthSwizzle;return out;
        case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
            if(!resource.stencil()) unsupported("stencil view",view);
            out.aspect=S;out.swizzle=stencilSwizzle;out.integer=true;return out;
        default:unsupported("depth/stencil view",view);
        }
    }
    if(view==DXGI_FORMAT_UNKNOWN) {
        if(resource.typeless) unsupported("untyped view of typeless resource",view);
        out.format=resource.format;out.aspect=C;out.integer=resource.integer;
        return out;
    }
    const Entry* entry=find(view);
    if(!entry || entry->typeless) unsupported("view",view);
    out.format=entry->vk;out.aspect=C;out.integer=entry->integer;out.signedInteger=entry->signedInteger;
    if(view==DXGI_FORMAT_B8G8R8X8_UNORM || view==DXGI_FORMAT_B8G8R8X8_UNORM_SRGB) out.swizzle.a=VK_COMPONENT_SWIZZLE_ONE;
    if(view==DXGI_FORMAT_A8_UNORM)
        out.swizzle={VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_R};
    return out;
}

VkFormat vertexFormat(DXGI_FORMAT format) {
    switch(format) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:return VK_FORMAT_R32G32B32A32_SFLOAT;
    case DXGI_FORMAT_R32G32B32A32_UINT:return VK_FORMAT_R32G32B32A32_UINT;
    case DXGI_FORMAT_R32G32B32A32_SINT:return VK_FORMAT_R32G32B32A32_SINT;
    case DXGI_FORMAT_R32G32B32_FLOAT:return VK_FORMAT_R32G32B32_SFLOAT;
    case DXGI_FORMAT_R32G32B32_UINT:return VK_FORMAT_R32G32B32_UINT;
    case DXGI_FORMAT_R32G32B32_SINT:return VK_FORMAT_R32G32B32_SINT;
    case DXGI_FORMAT_R32G32_FLOAT:return VK_FORMAT_R32G32_SFLOAT;
    case DXGI_FORMAT_R32G32_UINT:return VK_FORMAT_R32G32_UINT;
    case DXGI_FORMAT_R32G32_SINT:return VK_FORMAT_R32G32_SINT;
    case DXGI_FORMAT_R32_FLOAT:return VK_FORMAT_R32_SFLOAT;
    case DXGI_FORMAT_R32_UINT:return VK_FORMAT_R32_UINT;
    case DXGI_FORMAT_R32_SINT:return VK_FORMAT_R32_SINT;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:return VK_FORMAT_R16G16B16A16_SFLOAT;
    case DXGI_FORMAT_R16G16B16A16_UNORM:return VK_FORMAT_R16G16B16A16_UNORM;
    case DXGI_FORMAT_R16G16B16A16_SNORM:return VK_FORMAT_R16G16B16A16_SNORM;
    case DXGI_FORMAT_R16G16B16A16_UINT:return VK_FORMAT_R16G16B16A16_UINT;
    case DXGI_FORMAT_R16G16B16A16_SINT:return VK_FORMAT_R16G16B16A16_SINT;
    case DXGI_FORMAT_R16G16_FLOAT:return VK_FORMAT_R16G16_SFLOAT;
    case DXGI_FORMAT_R16G16_UNORM:return VK_FORMAT_R16G16_UNORM;
    case DXGI_FORMAT_R16G16_SNORM:return VK_FORMAT_R16G16_SNORM;
    case DXGI_FORMAT_R16G16_UINT:return VK_FORMAT_R16G16_UINT;
    case DXGI_FORMAT_R16G16_SINT:return VK_FORMAT_R16G16_SINT;
    case DXGI_FORMAT_R8G8B8A8_UNORM:return VK_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_SNORM:return VK_FORMAT_R8G8B8A8_SNORM;
    case DXGI_FORMAT_R8G8B8A8_UINT:return VK_FORMAT_R8G8B8A8_UINT;
    case DXGI_FORMAT_R8G8B8A8_SINT:return VK_FORMAT_R8G8B8A8_SINT;
    case DXGI_FORMAT_B8G8R8A8_UNORM:return VK_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_UNORM:return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case DXGI_FORMAT_R10G10B10A2_UINT:return VK_FORMAT_A2B10G10R10_UINT_PACK32;
    case DXGI_FORMAT_R11G11B10_FLOAT:return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case DXGI_FORMAT_R16_FLOAT:return VK_FORMAT_R16_SFLOAT;
    case DXGI_FORMAT_R16_UINT:return VK_FORMAT_R16_UINT;
    case DXGI_FORMAT_R16_SINT:return VK_FORMAT_R16_SINT;
    case DXGI_FORMAT_R16_UNORM:return VK_FORMAT_R16_UNORM;
    case DXGI_FORMAT_R16_SNORM:return VK_FORMAT_R16_SNORM;
    case DXGI_FORMAT_R8G8_UNORM:return VK_FORMAT_R8G8_UNORM;
    case DXGI_FORMAT_R8G8_UINT:return VK_FORMAT_R8G8_UINT;
    case DXGI_FORMAT_R8_UNORM:return VK_FORMAT_R8_UNORM;
    case DXGI_FORMAT_R8_UINT:return VK_FORMAT_R8_UINT;
    default:return VK_FORMAT_UNDEFINED;
    }
}
uint32_t vertexFormatBytes(DXGI_FORMAT format) {
    const Entry* entry=find(format);
    return entry?entry->bytes:0;
}
bool sameFamily(DXGI_FORMAT a,DXGI_FORMAT b) {
    if(a==b) return true;
    const Entry* left=find(a);const Entry* right=find(b);
    if(!left || !right) {
        // View-only depth/stencil formats belong to their combined families.
        auto family=[](DXGI_FORMAT format)->uint32_t {
            switch(format) {
            case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:return 5;
            case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:case DXGI_FORMAT_X24_TYPELESS_G8_UINT:return 11;
            default:return 0;
            }
        };
        const uint32_t l=left?left->family:family(a),r=right?right->family:family(b);
        return l && l==r;
    }
    return left->family==right->family;
}
}
