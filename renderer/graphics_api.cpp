#include "graphics_api.h"
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <string>

namespace Simpsons::Graphics {
namespace {
std::atomic<int> selected{-1};
GraphicsApi environmentDefault() noexcept {
    GraphicsApi api=GraphicsApi::D3D11;
    if(const char* text=std::getenv("SIMPSONS_RENDERER")) parseGraphicsApi(text,api);
    return api;
}
}
GraphicsApi graphicsApi() noexcept {
    int value=selected.load(std::memory_order_acquire);
    if(value<0) {
        int expected=-1;
        selected.compare_exchange_strong(expected,int(environmentDefault()),std::memory_order_acq_rel);
        value=selected.load(std::memory_order_acquire);
    }
    return GraphicsApi(value);
}
void selectGraphicsApi(GraphicsApi api) noexcept {selected.store(int(api),std::memory_order_release);}
bool parseGraphicsApi(std::string_view text,GraphicsApi& api) noexcept {
    std::string lower(text);
    for(auto& c:lower) c=char(std::tolower(static_cast<unsigned char>(c)));
    if(lower=="d3d11" || lower=="dx11" || lower=="direct3d11") {api=GraphicsApi::D3D11;return true;}
    if(lower=="vulkan" || lower=="vk") {api=GraphicsApi::Vulkan;return true;}
    return false;
}
const char* graphicsApiName(GraphicsApi api) noexcept {return api==GraphicsApi::Vulkan?"Vulkan":"D3D11";}
bool vulkanAvailable() noexcept {
#if SIMPSONS_VULKAN
    return true;
#else
    return false;
#endif
}
}
