#pragma once
#include <filesystem>
#include <cstdint>
#include <string>
#include <array>
#include <vector>

namespace Simpsons {
// One exclusive-fullscreen display mode of the game's monitor.
struct NativeDisplayMode {
    uint32_t width{},height{},refresh{};
    bool operator==(const NativeDisplayMode&) const=default;
};
struct NativeVideoSettings {
    static constexpr uint32_t windowResolutionCount=9,renderResolutionCount=9;
    // Render resolution ID 9 follows the presented display or window extent.
    static constexpr uint32_t matchDisplayResolution=9;
    uint32_t resolution=0; // Window output: five 16:9 and four ultrawide extents.
    bool fullscreen=false,vsync=false;
    // With fullscreen: false is borderless on the desktop mode, true switches
    // the monitor to displayWidth x displayHeight at refreshRate. Live.
    bool exclusiveFullscreen=false;
    uint32_t displayWidth=0,displayHeight=0; // Exclusive mode; 0 uses the desktop extent.
    uint32_t refreshRate=0; // Exclusive mode Hz; 0 uses the desktop/highest refresh.
    uint32_t frameRate=120; // 0 means unlimited.
    uint32_t renderResolution=matchDisplayResolution; // Internal extents, or Match display. Live.
    uint32_t textureFiltering=0; // Original, Nearest, Bilinear, Trilinear. Live.
    uint32_t anisotropicFiltering=1; // 1 (off), 2, 4, 8, 16. Live.
    uint32_t antialiasing=0; // Stable AA IDs; saved SSAA/legacy temporal IDs migrate to Original, MSAA to SMAA. Live.
    uint32_t fieldOfView=0; // Original (0), or 60..110 horizontal degrees at a 16:9 reference. Live.
    uint32_t renderScale=100; // Percentage of selected internal extent. Live.
    bool bloom=true,depthOfField=true,motionBlur=true; // Original effects enabled by default. Live.
    bool atmosphericFog=true,colorGrading=true,cinematicLetterbox=true; // Original screen passes. Live.
    uint32_t sharpening=0; // Final-image sharpening, percentage. Live.
    uint32_t backgroundFrameRate=30; // Inactive/minimized frame limit; 0 retains the foreground limit. Live.
    uint32_t maxFrameLatency=1; // Maximum queued presentation frames, 1..3. Live.
    uint32_t renderer=0; // Graphics device: 0 D3D11, 1 Vulkan. Relaunch.
    // Version 10.
    bool lensFlare=true,screenShake=true; // Original sun glare overlay and camera shake. Live.
    uint32_t shadowQuality=1; // Shadow map storage: 0 Low, 1 Original, 2 High, 3 Ultra. Live.
    uint32_t ambientOcclusion=0; // 0 Off, 1 Low, 2 High. Live.
    uint32_t upscaling=0; // Presentation scaling below the output extent: 0 Bilinear, 1 Edge-adaptive. Live.
    uint32_t colorLook=0; // 0 Original, 1 Vivid, 2 Punchy, 3 Soft, 4 Custom. Live.
    // Custom look adjustments, percent (-50..50 in steps of 10).
    int32_t vibrance=0,saturation=0,contrast=0,exposure=0;
    uint32_t inkOutlines=0; // 0 Original (hard), 1 Soft, 2 Off. Live.
    uint32_t outlineStrength=50; // Soft ink strength, 25..100 percent. Live.
    uint32_t outlineColor=0; // Ink colour preset; 0 is the original black. Live.
    uint32_t eyeShading=0; // 0 Original, 1 Clean. Live.
    bool fpsCounter=false; // On-screen frame rate counter. Live.
    uint32_t menuFrameRate=0; // Menu/pause presentation limit: 0 matches gameplay, else 30 or 60. Live.
    bool skipIntros=false; // Skip the startup logo videos. Next launch.
    uint32_t characterDetail=0; // Smoothed character meshes: 0 Off, 1 Low, 2 Medium, 3 High. Relaunch.
    uint32_t startEpisode=0; // Episode a New Game starts in (index into episodes). Relaunch.
    static constexpr std::array frameRates{30u,60u,90u,120u,144u,165u,240u,0u};
    static constexpr std::array renderScales{50u,67u,75u,100u,125u,150u,200u};
    static constexpr std::array antialiasingModes{0u,4u,1u,5u,2u,7u,16u};
    static constexpr std::array sharpeningStrengths{0u,25u,50u,75u,100u};
    static constexpr std::array backgroundFrameRates{15u,30u,60u,0u};
    static constexpr std::array frameLatencies{1u,2u,3u};
    static constexpr std::array anisotropyLevels{1u,2u,4u,8u,16u};
    static constexpr std::array outlineStrengths{25u,50u,75u,100u};
    static constexpr std::array menuFrameRates{0u,30u,60u};
    static constexpr std::array<int32_t,11> lookAdjustments{-50,-40,-30,-20,-10,0,10,20,30,40,50};
    static constexpr uint32_t outlineColorCount=8;
    // RGB 0..255 for each ink preset: Black, Charcoal, Brown, Navy, Forest,
    // Plum, Crimson, Sepia.
    static constexpr std::array<std::array<uint8_t,3>,outlineColorCount> outlineColors{{
        {{0,0,0}},{{58,58,64}},{{74,44,22}},{{20,30,84}},{{18,66,36}},{{72,24,82}},{{120,16,24}},{{102,74,44}}}};
    // Story episodes in gameflow order: original gameflow ID and display name.
    static constexpr uint32_t episodeCount=16;
    static constexpr std::array<const char*,episodeCount> episodeIds{"LAND_OF_CHOCOLATE","BARTMAN_BEGINS","EIGHTY_BITES",
        "TREEHUGGER","MOB_RULES","CHEATER","DOLPHINS","COLOSSAL_DONUT","SPRINGFIELD_STOOD_STILL","BARGAIN_BIN","NEVERQUEST",
        "GRAND_THEFT_SCRATCHY","MEDAL_OF_HOMER","BIG_SUPER","RHYMES_WITH_COMPLAINING","MEET_THY_PLAYER"};
    static constexpr std::array<const char*,episodeCount> episodeNames{"Land of Chocolate","Bartman Begins","Eighty Bites",
        "The Tree Hugger","Mob Rules","Cheater Cheater","Day of the Dolphins","Colossal Donut","Springfield Stood Still",
        "Bargain Bin","NeverQuest","Grand Theft Scratchy","Medal of Homer","Big Super Happy Fun","Rhymes with Complaining","Meet Thy Player"};
    static constexpr uint32_t nativeRowCount=40,nativePageRow=nativeRowCount+1;
    static bool validFrameRate(uint32_t rate);
    bool validRendering() const;
    static NativeVideoSettings load(const std::filesystem::path&);
    void save(const std::filesystem::path&) const;
    void step(uint32_t row,int direction);
    std::string renderLabel() const;
    std::string label(uint32_t row) const;
    uint32_t width() const {constexpr uint32_t widths[]{1280,1600,1920,2560,3440,3840,5120,2560,3840};return resolution<windowResolutionCount?widths[resolution]:1280u;}
    uint32_t height() const {constexpr uint32_t heights[]{720,900,1080,1080,1440,1600,1440,1440,2160};return resolution<windowResolutionCount?heights[resolution]:720u;}
    // Presented extent: the window size, desktop, or exclusive display mode.
    std::array<uint32_t,2> outputExtent() const;
    // Exclusive mode actually requested, with desktop/highest defaults resolved.
    NativeDisplayMode exclusiveMode() const;
    uint32_t renderWidth() const {return renderExtent()[0];}
    uint32_t renderHeight() const {return renderExtent()[1];}
    std::array<uint32_t,2> renderExtent() const;
    uint32_t anisotropy() const {return anisotropicFiltering;}
    // The game's monitor. Tests may replace both with a fixed description.
    static std::array<uint32_t,2> desktopExtent();
    static uint32_t desktopRefresh();
    static const std::vector<NativeDisplayMode>& displayModes();
    static void overrideDisplay(std::array<uint32_t,2> desktop,uint32_t refresh,std::vector<NativeDisplayMode> modes);
    // Largest supported internal extent (16:9..32:9, 1280x720..5120x2160) that
    // fits inside an output extent.
    static std::array<uint32_t,2> fitRenderExtent(uint32_t width,uint32_t height);
};
class Runtime;
void applyNativeVideoSettings(Runtime&);
void applyNativeFrameRate(Runtime&);
}
