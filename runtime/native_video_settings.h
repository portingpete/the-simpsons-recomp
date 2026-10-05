#pragma once
#include <filesystem>
#include <cstdint>
#include <string>
#include <array>

namespace Simpsons {
struct NativeVideoSettings {
    static constexpr uint32_t windowResolutionCount=7,renderResolutionCount=9;
    uint32_t resolution=0; // Window output: three 16:9 and four ultrawide extents.
    bool fullscreen=false,vsync=false;
    uint32_t frameRate=120; // 0 means unlimited.
    uint32_t renderResolution=0; // Internal: five 16:9 and four ultrawide extents. Relaunch.
    uint32_t textureFiltering=0; // Original, 4x, 8x, 16x anisotropic. Relaunch.
    uint32_t antialiasing=0; // Original, FXAA, FXAA + Original, SSAA 4x. Relaunch.
    uint32_t fieldOfView=0; // Original (0), or 60..110 horizontal degrees at a 16:9 reference. Live.
    uint32_t renderScale=100; // Percentage of selected internal extent. Relaunch.
    bool bloom=true,depthOfField=true,motionBlur=true; // Original effects enabled by default. Live.
    static constexpr std::array frameRates{30u,60u,90u,120u,144u,165u,240u,0u};
    static constexpr std::array renderScales{50u,67u,75u,100u,125u,150u,200u};
    static constexpr uint32_t nativeRowCount=12;
    static bool validFrameRate(uint32_t rate);
    bool validRendering() const;
    static NativeVideoSettings load(const std::filesystem::path&);
    void save(const std::filesystem::path&) const;
    void step(uint32_t row,int direction);
    std::string renderLabel() const;
    uint32_t width() const {constexpr uint32_t widths[]{1280,1600,1920,2560,3440,3840,5120};return resolution<windowResolutionCount?widths[resolution]:1280u;}
    uint32_t height() const {constexpr uint32_t heights[]{720,900,1080,1080,1440,1600,1440};return resolution<windowResolutionCount?heights[resolution]:720u;}
    uint32_t renderWidth() const {constexpr uint32_t widths[]{1280,1600,1920,2560,3840,2560,3440,3840,5120};return renderResolution<renderResolutionCount?widths[renderResolution]:1280u;}
    uint32_t renderHeight() const {constexpr uint32_t heights[]{720,900,1080,1440,2160,1080,1440,1600,1440};return renderResolution<renderResolutionCount?heights[renderResolution]:720u;}
    uint32_t anisotropy() const {return textureFiltering==3?16u:textureFiltering==2?8u:textureFiltering==1?4u:1u;}
};
class Runtime;
void applyNativeVideoSettings(Runtime&);
void applyNativeFrameRate(Runtime&);
}
