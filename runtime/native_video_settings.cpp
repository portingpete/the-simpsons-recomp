#include "native_video_settings.h"
#include "runtime.h"
#include "native_window.h"
#include "engine_driver.h"
#include "engine_audio_output.h"
#include "engine_cpu_calls.h"
#include "renderer/native_backend.h"
#include "renderer/graphics_api.h"
#include <algorithm>
#include <fstream>
#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>

namespace Simpsons {
namespace {
// The monitor description is shared by the menu (game thread), the window
// thread and tests. A test override replaces the real monitor completely.
struct MonitorDescription {
    std::array<uint32_t,2> desktop{};
    uint32_t refresh{};
    std::vector<NativeDisplayMode> modes;
};
std::mutex& monitorMutex(){static std::mutex mutex;return mutex;}
std::optional<MonitorDescription>& monitorOverride(){static std::optional<MonitorDescription> value;return value;}
const MonitorDescription& monitor() {
    static MonitorDescription real;
    static bool enumerated=false;
    std::lock_guard lock(monitorMutex());
    if(monitorOverride())return *monitorOverride();
    if(!enumerated) {
        enumerated=true;
        DEVMODEW current{};current.dmSize=sizeof(current);
        if(EnumDisplaySettingsW(nullptr,ENUM_CURRENT_SETTINGS,&current)) {
            real.desktop={current.dmPelsWidth,current.dmPelsHeight};
            real.refresh=current.dmDisplayFrequency>1?current.dmDisplayFrequency:60;
        } else real.desktop={1920,1080},real.refresh=60;
        for(DWORD index=0;;++index) {
            DEVMODEW mode{};mode.dmSize=sizeof(mode);
            if(!EnumDisplaySettingsW(nullptr,index,&mode))break;
            // Progressive 32-bit modes the renderer can present without a
            // sub-720p swap chain. Duplicate scaling/orientation rows collapse.
            if(mode.dmBitsPerPel!=32||(mode.dmDisplayFlags&DM_INTERLACED)||mode.dmPelsWidth<1280||mode.dmPelsHeight<720||
               mode.dmPelsWidth>16384||mode.dmPelsHeight>16384||mode.dmDisplayFrequency<50)continue;
            const NativeDisplayMode entry{mode.dmPelsWidth,mode.dmPelsHeight,mode.dmDisplayFrequency};
            if(std::find(real.modes.begin(),real.modes.end(),entry)==real.modes.end())real.modes.push_back(entry);
        }
        if(real.modes.empty())real.modes.push_back({real.desktop[0],real.desktop[1],real.refresh});
        std::sort(real.modes.begin(),real.modes.end(),[](const auto& a,const auto& b) {
            const uint64_t left=uint64_t(a.width)*a.height,right=uint64_t(b.width)*b.height;
            return left!=right?left<right:a.width!=b.width?a.width<b.width:a.refresh<b.refresh;
        });
    }
    return real;
}
// Distinct resolutions in mode order, and the refresh rates of one of them.
std::vector<std::array<uint32_t,2>> modeResolutions() {
    std::vector<std::array<uint32_t,2>> result;
    for(const auto& mode:NativeVideoSettings::displayModes()) {
        const std::array<uint32_t,2> extent{mode.width,mode.height};
        if(std::find(result.begin(),result.end(),extent)==result.end())result.push_back(extent);
    }
    return result;
}
std::vector<uint32_t> modeRefreshes(uint32_t width,uint32_t height) {
    std::vector<uint32_t> result;
    for(const auto& mode:NativeVideoSettings::displayModes())
        if(mode.width==width&&mode.height==height&&std::find(result.begin(),result.end(),mode.refresh)==result.end())
            result.push_back(mode.refresh);
    std::sort(result.begin(),result.end());
    return result;
}
template<class T,size_t N> bool contains(const std::array<T,N>& values,T value) {
    return std::find(values.begin(),values.end(),value)!=values.end();
}
std::string signedPercent(int32_t value) {
    return (value>0?"+":"")+std::to_string(value)+"%";
}
}
std::array<uint32_t,2> NativeVideoSettings::desktopExtent(){return monitor().desktop;}
uint32_t NativeVideoSettings::desktopRefresh(){return monitor().refresh;}
const std::vector<NativeDisplayMode>& NativeVideoSettings::displayModes(){return monitor().modes;}
void NativeVideoSettings::overrideDisplay(std::array<uint32_t,2> desktop,uint32_t refresh,std::vector<NativeDisplayMode> modes) {
    std::lock_guard lock(monitorMutex());
    if(modes.empty())modes.push_back({desktop[0],desktop[1],refresh});
    monitorOverride()=MonitorDescription{desktop,refresh,std::move(modes)};
}
std::array<uint32_t,2> NativeVideoSettings::fitRenderExtent(uint32_t width,uint32_t height) {
    if(!width||!height)return {1280,720};
    uint64_t w=width,h=height;
    // The original camera keeps a 16:9 vertical view, so a narrower display
    // receives the widest 16:9 image it can hold and a wider one is capped at 32:9.
    if(w*9<h*16)h=w*9/16;
    else if(w*9>h*32)w=h*32/9;
    if(w>5120){h=h*5120/w;w=5120;}
    if(h>2160){w=w*2160/h;h=2160;}
    if(w<1280||h<720){
        // Smaller outputs render at the minimum extent of the same aspect and scale down.
        const double scale=std::max(1280.0/double(w),720.0/double(h));
        w=uint64_t(double(w)*scale+0.5);h=uint64_t(double(h)*scale+0.5);
    }
    w&=~uint64_t(1);h&=~uint64_t(1);
    if(w*9<h*16)h=w*9/16;
    if(w*9>h*32)w=h*32/9;
    return {uint32_t(std::clamp<uint64_t>(w,1280,5120)),uint32_t(std::clamp<uint64_t>(h,720,2160))};
}
NativeDisplayMode NativeVideoSettings::exclusiveMode() const {
    const auto desktop=desktopExtent();
    const uint32_t w=displayWidth?displayWidth:desktop[0],h=displayHeight?displayHeight:desktop[1];
    auto refreshes=modeRefreshes(w,h);
    uint32_t rate=refreshRate;
    if(refreshes.empty())return {w,h,rate?rate:desktopRefresh()};
    if(!rate||std::find(refreshes.begin(),refreshes.end(),rate)==refreshes.end())rate=refreshes.back();
    return {w,h,rate};
}
std::array<uint32_t,2> NativeVideoSettings::outputExtent() const {
    if(!fullscreen)return {width(),height()};
    if(!exclusiveFullscreen)return desktopExtent();
    const auto mode=exclusiveMode();return {mode.width,mode.height};
}
std::array<uint32_t,2> NativeVideoSettings::renderExtent() const {
    if(renderResolution==matchDisplayResolution) {
        const auto output=outputExtent();return fitRenderExtent(output[0],output[1]);
    }
    constexpr uint32_t widths[]{1280,1600,1920,2560,3840,2560,3440,3840,5120};
    constexpr uint32_t heights[]{720,900,1080,1440,2160,1080,1440,1600,1440};
    return renderResolution<renderResolutionCount?std::array{widths[renderResolution],heights[renderResolution]}:std::array{1280u,720u};
}
bool NativeVideoSettings::validFrameRate(uint32_t rate) {
    return std::find(frameRates.begin(),frameRates.end(),rate)!=frameRates.end();
}
bool NativeVideoSettings::validRendering() const {
    return Graphics::NativeBackend::supportsRendering(renderWidth(),renderHeight(),anisotropy(),
        Graphics::Antialiasing(antialiasing),renderScale);
}
NativeVideoSettings NativeVideoSettings::load(const std::filesystem::path& file) {
    NativeVideoSettings out;
    std::ifstream in(file);uint32_t version{},res{},mode{},sync{},rate{};
    if(!(in>>version>>res>>mode>>sync>>rate) || version<1 || version>10 || mode>=2 || sync>=2 ||
       !(version>=5?validFrameRate(rate):(rate==0||rate==60||rate==120)))return out;
    if(res>=(version>=6?windowResolutionCount:version>=4?7u:3u))return out;
    // Version 10 separates anisotropy from the filtering mode; older 4x/8x/16x
    // choices keep their anisotropy with the original material filters.
    const auto legacyFiltering=[&](uint32_t filter) {
        out.textureFiltering=0;out.anisotropicFiltering=filter==3?16u:filter==2?8u:filter==1?4u:1u;
    };
    if(version==1){out.resolution=res;out.fullscreen=mode!=0;out.vsync=sync!=0;out.frameRate=rate;out.renderResolution=res;}
    else {
        uint32_t render{},filter{},aa{};
        if(!(in>>render>>filter) || render>=(version>=10?renderResolutionCount+1:version>=4?renderResolutionCount:5u) || filter>=4)return out;
        if(version>=3 && (!(in>>aa) || (version>=10?aa>16:version>=8?aa>15:aa>=(version>=7?7u:4u))))return out;
        // Known retired SSAA and excluded temporal preferences migrate only
        // the AA field to Original, and removed MSAA 2x/4x/8x to SMAA;
        // independent display, effect and pacing fields survive. Unknown IDs
        // and malformed remaining fields still reject the file.
        if(aa==3 || aa==6 || (aa>=11 && aa<=15))aa=0;
        else if(aa>=8 && aa<=10)aa=7;
        out.resolution=res;out.fullscreen=mode!=0;out.vsync=sync!=0;out.frameRate=rate;
        out.renderResolution=render;out.antialiasing=aa;
        if(version>=10)out.textureFiltering=filter;else legacyFiltering(filter);
        if(version>=5) {
            uint32_t fov{},scale{},glow{},dof{},blur{};
            if(!(in>>fov>>scale>>glow>>dof>>blur) ||
               (fov!=0&&(fov<60||fov>110||fov%5)) ||
               std::find(renderScales.begin(),renderScales.end(),scale)==renderScales.end() ||
               glow>1||dof>1||blur>1)return {};
            out.fieldOfView=fov;out.renderScale=scale;
            out.bloom=glow!=0;out.depthOfField=dof!=0;out.motionBlur=blur!=0;
            if(version>=6) {
                uint32_t fog{},grading{},bars{};
                if(!(in>>fog>>grading>>bars)||fog>1||grading>1||bars>1)return {};
                out.atmosphericFog=fog!=0;out.colorGrading=grading!=0;out.cinematicLetterbox=bars!=0;
            }
            if(version>=7) {
                uint32_t sharp{},background{},latency{};
                if(!(in>>sharp>>background>>latency) ||
                   std::find(sharpeningStrengths.begin(),sharpeningStrengths.end(),sharp)==sharpeningStrengths.end() ||
                   std::find(backgroundFrameRates.begin(),backgroundFrameRates.end(),background)==backgroundFrameRates.end() ||
                   std::find(frameLatencies.begin(),frameLatencies.end(),latency)==frameLatencies.end())return {};
                out.sharpening=sharp;out.backgroundFrameRate=background;out.maxFrameLatency=latency;
            }
            if(version>=9) {
                uint32_t renderer{};
                if(!(in>>renderer)||renderer>1)return {};
                out.renderer=renderer;
            }
            if(version>=10) {
                uint32_t exclusive{},displayW{},displayH{},refresh{},aniso{},flare{},shake{},shadows{},ao{},upscale{},look{};
                int32_t vib{},sat{},con{},exp{};
                uint32_t ink{},strength{},color{},eyes{},fps{},menuRate{},intros{},detail{},episode{};
                if(!(in>>exclusive>>displayW>>displayH>>refresh>>aniso>>flare>>shake>>shadows>>ao>>upscale>>look
                       >>vib>>sat>>con>>exp>>ink>>strength>>color>>eyes>>fps>>menuRate>>intros>>detail>>episode))return {};
                const auto adjustment=[](int32_t value){return contains(lookAdjustments,value);};
                if(exclusive>1||(displayW==0)!=(displayH==0)||(displayW&&(displayW<1280||displayH<720||displayW>16384||displayH>16384))||
                   refresh>1000||!contains(anisotropyLevels,aniso)||flare>1||shake>1||shadows>3||ao>2||upscale>1||look>4||
                   !adjustment(vib)||!adjustment(sat)||!adjustment(con)||!adjustment(exp)||ink>2||!contains(outlineStrengths,strength)||
                   color>=outlineColorCount||eyes>1||fps>1||!contains(menuFrameRates,menuRate)||intros>1||detail>3||episode>=episodeCount)return {};
                out.exclusiveFullscreen=exclusive!=0;out.displayWidth=displayW;out.displayHeight=displayH;out.refreshRate=refresh;
                out.anisotropicFiltering=aniso;out.lensFlare=flare!=0;out.screenShake=shake!=0;out.shadowQuality=shadows;
                out.ambientOcclusion=ao;out.upscaling=upscale;out.colorLook=look;
                out.vibrance=vib;out.saturation=sat;out.contrast=con;out.exposure=exp;
                out.inkOutlines=ink;out.outlineStrength=strength;out.outlineColor=color;out.eyeShading=eyes;
                out.fpsCounter=fps!=0;out.menuFrameRate=menuRate;out.skipIntros=intros!=0;out.characterDetail=detail;out.startEpisode=episode;
            }
            if(!out.validRendering())return {};
        }
    }
    return out;
}
void NativeVideoSettings::save(const std::filesystem::path& file) const {
    if(file.empty())return;
    // Profile ancestors deliberately deny rename access. This small, versioned
    // preference file is separate from the strict profile directory; a partial
    // write is rejected by load() and safely falls back to defaults.
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file,std::ios::trunc);out<<10<<' '<<resolution<<' '<<fullscreen<<' '<<vsync<<' '<<frameRate<<' '<<renderResolution<<' '<<textureFiltering<<' '<<antialiasing
        <<' '<<fieldOfView<<' '<<renderScale<<' '<<bloom<<' '<<depthOfField<<' '<<motionBlur
        <<' '<<atmosphericFog<<' '<<colorGrading<<' '<<cinematicLetterbox
        <<' '<<sharpening<<' '<<backgroundFrameRate<<' '<<maxFrameLatency<<' '<<renderer
        <<' '<<exclusiveFullscreen<<' '<<displayWidth<<' '<<displayHeight<<' '<<refreshRate<<' '<<anisotropicFiltering
        <<' '<<lensFlare<<' '<<screenShake<<' '<<shadowQuality<<' '<<ambientOcclusion<<' '<<upscaling<<' '<<colorLook
        <<' '<<vibrance<<' '<<saturation<<' '<<contrast<<' '<<exposure
        <<' '<<inkOutlines<<' '<<outlineStrength<<' '<<outlineColor<<' '<<eyeShading
        <<' '<<fpsCounter<<' '<<menuFrameRate<<' '<<skipIntros<<' '<<characterDetail<<' '<<startEpisode<<'\n';out.close();
    if(!out)throw Failure("Unable to save native video settings");
}
void NativeVideoSettings::step(uint32_t row,int direction) {
    const auto cycle=[&](auto& value,const auto& choices,bool rendering=false) {
        const auto found=std::find(choices.begin(),choices.end(),value);
        if(found==choices.end())throw Failure("Invalid native video choice");
        const size_t original=size_t(found-choices.begin());
        for(size_t step=1;step<=choices.size();++step) {
            const size_t index=direction>0?(original+step)%choices.size():(original+choices.size()-step)%choices.size();
            value=choices[index];if(!rendering||validRendering())return;
        }
        throw Failure("No supported native rendering choice");
    };
    const auto toggle=[](bool& value){value=!value;};
    const auto range=[&](uint32_t& value,uint32_t count){value=(value+(direction>0?1u:count-1u))%count;};
    const auto keepRendering=[&](const NativeVideoSettings& previous) {
        // An output change can move a Match display extent outside the budget.
        if(!validRendering())*this=previous;
    };
    if(row==1) {
        // Keep persisted extent IDs stable, but expose the wide alternatives
        // alongside the standard resolutions instead of burying them after 4K.
        constexpr std::array order{matchDisplayResolution,0u,1u,2u,5u,3u,6u,7u,4u,8u};
        cycle(renderResolution,order,true);
    }
    else if(row==2) {
        const auto previous=*this;
        if(!fullscreen){constexpr std::array order{0u,1u,2u,3u,7u,4u,5u,8u,6u};cycle(resolution,order);}
        else if(exclusiveFullscreen) {
            const auto resolutions=modeResolutions();
            const auto current=exclusiveMode();
            auto found=std::find(resolutions.begin(),resolutions.end(),std::array{current.width,current.height});
            size_t index=found==resolutions.end()?resolutions.size()-1:size_t(found-resolutions.begin());
            index=direction>0?(index+1)%resolutions.size():(index+resolutions.size()-1)%resolutions.size();
            displayWidth=resolutions[index][0];displayHeight=resolutions[index][1];
            const auto refreshes=modeRefreshes(displayWidth,displayHeight);
            if(refreshRate&&std::find(refreshes.begin(),refreshes.end(),refreshRate)==refreshes.end())refreshRate=0;
        }
        keepRendering(previous);
    }
    else if(row==3) {
        // Windowed -> Borderless -> Exclusive fullscreen.
        const auto previous=*this;
        uint32_t mode=!fullscreen?0u:exclusiveFullscreen?2u:1u;range(mode,3);
        fullscreen=mode!=0;exclusiveFullscreen=mode==2;
        if(exclusiveFullscreen&&!displayWidth) {
            const auto desktop=desktopExtent();displayWidth=desktop[0];displayHeight=desktop[1];
        }
        keepRendering(previous);
    }
    else if(row==4)vsync=!vsync;
    else if(row==5)cycle(frameRate,frameRates);
    else if(row==6)range(textureFiltering,4);
    else if(row==7)cycle(antialiasing,antialiasingModes,true);
    else if(row==8){constexpr std::array fovs{0u,60u,65u,70u,75u,80u,85u,90u,95u,100u,105u,110u};cycle(fieldOfView,fovs);}
    else if(row==9)cycle(renderScale,renderScales,true);
    else if(row==10)bloom=!bloom;
    else if(row==11)depthOfField=!depthOfField;
    else if(row==12)motionBlur=!motionBlur;
    else if(row==13)atmosphericFog=!atmosphericFog;
    else if(row==14)colorGrading=!colorGrading;
    else if(row==15)cinematicLetterbox=!cinematicLetterbox;
    else if(row==16)cycle(sharpening,sharpeningStrengths);
    else if(row==17)cycle(backgroundFrameRate,backgroundFrameRates);
    else if(row==18)cycle(maxFrameLatency,frameLatencies);
    // A build without the Vulkan renderer keeps D3D11.
    else if(row==19)renderer=Graphics::vulkanAvailable()?renderer^1u:0u;
    else if(row==20) {
        // Only an exclusive display mode owns its refresh rate.
        if(fullscreen&&exclusiveFullscreen) {
            const auto mode=exclusiveMode();
            auto refreshes=modeRefreshes(mode.width,mode.height);
            if(!refreshes.empty()) {
                auto found=std::find(refreshes.begin(),refreshes.end(),mode.refresh);
                size_t index=found==refreshes.end()?refreshes.size()-1:size_t(found-refreshes.begin());
                index=direction>0?(index+1)%refreshes.size():(index+refreshes.size()-1)%refreshes.size();
                refreshRate=refreshes[index];
            }
        }
    }
    else if(row==21)cycle(anisotropicFiltering,anisotropyLevels,true);
    else if(row==22)toggle(lensFlare);
    else if(row==23)toggle(screenShake);
    else if(row==24)range(shadowQuality,4);
    else if(row==25)range(ambientOcclusion,3);
    else if(row==26)range(upscaling,2);
    else if(row==27)range(colorLook,5);
    else if(row>=28&&row<=31) {
        // Editing an adjustment selects the Custom look that applies it.
        int32_t& value=row==28?vibrance:row==29?saturation:row==30?contrast:exposure;
        cycle(value,lookAdjustments);colorLook=4;
    }
    else if(row==32)range(inkOutlines,3);
    else if(row==33)cycle(outlineStrength,outlineStrengths);
    else if(row==34)range(outlineColor,outlineColorCount);
    else if(row==35)range(eyeShading,2);
    else if(row==36)toggle(fpsCounter);
    else if(row==37)cycle(menuFrameRate,menuFrameRates);
    else if(row==38)toggle(skipIntros);
    else if(row==39)range(characterDetail,4);
    else if(row==40)range(startEpisode,episodeCount);
    else throw Failure("Unknown native video settings row");
}
std::string NativeVideoSettings::renderLabel() const {
    const auto extent=renderExtent();
    const bool wide=uint64_t(extent[0])*9>uint64_t(extent[1])*16;
    if(renderResolution==matchDisplayResolution)
        return "Render resolution: Match display ("+std::to_string(extent[0])+"x"+std::to_string(extent[1])+")";
    return "Render resolution: "+std::to_string(extent[0])+"x"+std::to_string(extent[1])+(wide?" Ultrawide":"");
}
std::string NativeVideoSettings::label(uint32_t row) const {
    const auto onOff=[](bool value){return std::string(value?"On":"Off");};
    const auto rate=[](uint32_t fps){return fps?std::to_string(fps)+" FPS":std::string("Unlimited");};
    constexpr std::array aaNames{"Original","FXAA","FXAA + Original","Retired","FXAA Low","FXAA High","Retired","SMAA"};
    constexpr std::array filters{"Original","Nearest","Bilinear","Trilinear"};
    constexpr std::array qualities{"Low","Original","High","Ultra"};
    constexpr std::array occlusion{"Off","Low","High"};
    constexpr std::array looks{"Original","Vivid","Punchy","Soft","Custom"};
    constexpr std::array inks{"Original","Soft","Off"};
    constexpr std::array inkColors{"Black","Charcoal","Brown","Navy","Forest","Plum","Crimson","Sepia"};
    constexpr std::array details{"Off","Low","Medium","High"};
    const auto custom=[&](const char* name,int32_t value) {
        return std::string(name)+": "+signedPercent(value)+(colorLook==4?"":" (Custom look)");
    };
    switch(row) {
    case 1:return renderLabel();
    case 2:
        if(!fullscreen)return "Window size: "+std::to_string(width())+" x "+std::to_string(height());
        if(!exclusiveFullscreen){const auto d=desktopExtent();return "Display resolution: Desktop "+std::to_string(d[0])+" x "+std::to_string(d[1]);}
        {const auto m=exclusiveMode();return "Display resolution: "+std::to_string(m.width)+" x "+std::to_string(m.height);}
    case 3:return std::string("Display mode: ")+(!fullscreen?"Windowed":exclusiveFullscreen?"Fullscreen":"Borderless");
    case 4:return "VSync: "+onOff(vsync);
    case 5:return "Frame limit: "+rate(frameRate);
    case 6:return std::string("Texture filtering: ")+filters[textureFiltering<4?textureFiltering:0];
    case 7:return std::string("Antialiasing: ")+(antialiasing==16?"TAA":antialiasing<aaNames.size()?aaNames[antialiasing]:"Original");
    case 8:return "FOV (16:9): "+(fieldOfView?std::to_string(fieldOfView)+" degrees":std::string("Original"));
    case 9:return "Render scale: "+std::to_string(renderScale)+"%";
    case 10:return "Bloom: "+onOff(bloom);
    case 11:return "Depth of field: "+onOff(depthOfField);
    case 12:return "Motion blur: "+onOff(motionBlur);
    case 13:return "Atmospheric fog: "+onOff(atmosphericFog);
    case 14:return "Color grading: "+onOff(colorGrading);
    case 15:return "Cinematic bars: "+onOff(cinematicLetterbox);
    case 16:return "Sharpening: "+std::to_string(sharpening)+"%";
    case 17:return "Background limit: "+rate(backgroundFrameRate);
    case 18:return "Frame queue: "+std::to_string(maxFrameLatency)+" frame"+(maxFrameLatency==1?"":"s");
    case 19:return std::string("Renderer: ")+(renderer?"Vulkan":"D3D11")+" (restart)";
    case 20:
        if(fullscreen&&exclusiveFullscreen)return "Refresh rate: "+std::to_string(exclusiveMode().refresh)+" Hz";
        return "Refresh rate: Desktop ("+std::to_string(desktopRefresh())+" Hz)";
    case 21:return "Anisotropic filtering: "+(anisotropicFiltering>1?std::to_string(anisotropicFiltering)+"x":std::string("Off"));
    case 22:return "Lens flare: "+onOff(lensFlare);
    case 23:return "Screen shake: "+onOff(screenShake);
    case 24:return std::string("Shadow quality: ")+qualities[shadowQuality<4?shadowQuality:1];
    case 25:return std::string("Ambient occlusion: ")+occlusion[ambientOcclusion<3?ambientOcclusion:0];
    case 26:return std::string("Upscaling: ")+(upscaling?"Edge-adaptive":"Bilinear");
    case 27:return std::string("Color look: ")+looks[colorLook<5?colorLook:0];
    case 28:return custom("Vibrance",vibrance);
    case 29:return custom("Saturation",saturation);
    case 30:return custom("Contrast",contrast);
    case 31:return custom("Exposure",exposure);
    case 32:return std::string("Ink outlines: ")+inks[inkOutlines<3?inkOutlines:0];
    case 33:return "Soft outline strength: "+std::to_string(outlineStrength)+"%";
    case 34:return std::string("Outline color: ")+inkColors[outlineColor<outlineColorCount?outlineColor:0];
    case 35:return std::string("Character eyes: ")+(eyeShading?"Clean":"Original");
    case 36:return "FPS counter: "+onOff(fpsCounter);
    case 37:return "Menu frame limit: "+(menuFrameRate?std::to_string(menuFrameRate)+" FPS":std::string("Same as gameplay"));
    case 38:return "Skip intro videos: "+onOff(skipIntros)+" (next launch)";
    case 39:return std::string("Character detail: ")+details[characterDetail<4?characterDetail:0]+" (restart)";
    case 40:return std::string("New game episode: ")+episodeNames[startEpisode<episodeCount?startEpisode:0]+" (restart)";
    default:throw Failure("Unknown native video settings row");
    }
}
void applyNativeFrameRate(Runtime& rt) {
    if(rt.requestedFrameRate!=rt.videoSettings.frameRate)rt.framePacer.reset();
    rt.requestedFrameRate=rt.videoSettings.frameRate;
    rt.uncappedFrameRate=rt.videoSettings.frameRate==0;
    // Original 82718D48 publishes intervals at +144, consumed by 826B8080.
    // Update that existing owner as well as native pacing when changed live.
    const auto owner=PPCLoadU32(rt.base,0x82D576A0);
    if(owner)PPCStoreU32(rt.base,owner+144,0u);
}
void applyNativeVideoSettings(Runtime& rt) {
    if(!rt.videoSettingsPending||!rt.window)return;
    const auto& video=rt.videoSettings;
    NativeWindow::VideoRequest request{video.width(),video.height(),video.fullscreen,video.exclusiveFullscreen,{}};
    if(video.fullscreen&&video.exclusiveFullscreen)request.mode=video.exclusiveMode();
    rt.window->configureVideo(request);
    applyNativeFrameRate(rt);
    if(rt.vsyncEnabled!=rt.videoSettings.vsync)rt.framePacer.reset();
    rt.vsyncEnabled=rt.videoSettings.vsync;rt.videoSettingsPending=false;
    std::fprintf(stderr,"[NATIVE VIDEO] window=%ux%u fullscreen=%u exclusive=%u display=%ux%u@%u vsync=%u frame_rate=%u render_index=%u render=%ux%u aa=%u filtering=%u anisotropy=%u fov=%u render_scale=%u bloom=%u dof=%u motion_blur=%u fog=%u grading=%u letterbox=%u flare=%u shake=%u shadows=%u ao=%u upscaling=%u look=%u ink=%u/%u/%u eyes=%u sharpening=%u background_rate=%u menu_rate=%u max_frame_latency=%u fps=%u renderer=%s; render/AA/filter/scale apply between frames, renderer changes after restart\n",
        rt.window->presentationWidth.load(),rt.window->presentationHeight.load(),video.fullscreen,video.exclusiveFullscreen,
        request.mode.width,request.mode.height,request.mode.refresh,video.vsync,video.frameRate,
        video.renderResolution,video.renderWidth(),video.renderHeight(),
        video.antialiasing,video.textureFiltering,video.anisotropy(),video.fieldOfView,video.renderScale,
        video.bloom,video.depthOfField,video.motionBlur,video.atmosphericFog,video.colorGrading,video.cinematicLetterbox,
        video.lensFlare,video.screenShake,video.shadowQuality,video.ambientOcclusion,video.upscaling,video.colorLook,
        video.inkOutlines,video.outlineStrength,video.outlineColor,video.eyeShading,
        video.sharpening,video.backgroundFrameRate,video.menuFrameRate,video.maxFrameLatency,video.fpsCounter,
        video.renderer?"Vulkan":"D3D11");
    // Controlled tests wait on this completed settings receipt. Keep normal
    // per-frame diagnostics buffered when no local command channel is attached.
    if(rt.controllerCommands)std::fflush(stderr);
}
}
namespace {
uint32_t guestText(Simpsons::EngineCpuCalls& cpu,uint8_t* base,const std::string& text) {
    const auto address=cpu.registers().r1.u32+0xA0;
    if(text.size()>79)throw Simpsons::Failure("Video UI method exceeds callback storage");
    std::memcpy(PPCGuestPointer(base,address,unsigned(text.size()+1),true),text.c_str(),text.size()+1);return address;
}
void labels(PPCContext& ctx,uint8_t* base) {
    auto& rt=*Simpsons::active;Simpsons::EngineCpuCalls cpu(ctx,base);
    const auto target=cpu.invoke(0x827F20E0,ctx.r3.u32);
    const auto& video=rt.videoSettings;
    constexpr std::array names{"resolution","windowsize","windowmode","vsync","framecap","filtering","antialiasing","fov","renderscale","bloom","depthoffield","motionblur","atmosphericfog","colorgrading","cinematicbars","sharpening","backgroundcap","framelatency","renderer",
        "refreshrate","anisotropy","lensflare","screenshake","shadowquality","ambientocclusion","upscaling","colorlook","vibrance","saturation","contrast","exposure","inkoutlines","outlinestrength","outlinecolor","eyeshading","fpscounter","menucap","skipintros","characterdetail","startepisode"};
    static_assert(names.size()==Simpsons::NativeVideoSettings::nativeRowCount);
    // Use the same string-argument Apt bridge as the original brightness row.
    for(size_t i=0;i<names.size();++i) {
        const auto value=video.label(uint32_t(i+1));
        // The variadic bridge spills r7-r10 at +48 through +79 before reading
        // its string arguments. Keep both buffers outside that outgoing area.
        // Leave 64 bytes for each value, including the Ultrawide annotation,
        // before the method-name buffer at +0xA0. Both end below saved LR.
        if(value.size()>63)throw Simpsons::Failure("Video UI label exceeds callback storage");
        const auto address=cpu.registers().r1.u32+0x60;
        std::memcpy(PPCGuestPointer(base,address,unsigned(value.size()+1),true),value.c_str(),value.size()+1);
        cpu.invoke(0x827BF8F8,guestText(cpu,base,std::string("VideoMenu.set_")+names[i]),0,target,1,address);
    }
}
}
void SimpsonsNativeVideoPopulate(PPCContext& ctx,uint8_t* base) {
    Simpsons::active->videoSettingsBeforeMenu=Simpsons::active->videoSettings;
    const auto& video=Simpsons::active->videoSettings;
    std::fprintf(stderr,"[NATIVE VIDEO MENU] opened render_choices=%u render_index=%u render=%ux%u\n",
        Simpsons::NativeVideoSettings::renderResolutionCount+1,video.renderResolution,video.renderWidth(),video.renderHeight());
    Simpsons::EngineCpuCalls cpu(ctx,base);
    const auto target=cpu.invoke(0x827F20E0,ctx.r3.u32);
    // Preserve the original brightness population and Apt result convention.
    cpu.invoke(0x823A4958,target,0x82002A00,7);
    labels(ctx,base);ctx.r3.u32=cpu.invoke(0x827BBFF8);
    if(Simpsons::active->controllerCommands)std::fflush(stderr);
}
void SimpsonsNativeVideoSave(PPCContext& ctx,uint8_t* base) {
    auto& rt=*Simpsons::active;Simpsons::EngineCpuCalls cpu(ctx,base);
    const auto target=cpu.invoke(0x827F20E0,ctx.r3.u32);
    const auto name=guestText(cpu,base,"VideoMenu.getNativeAction");
    cpu.invoke(0x827BF8F8,name,0,target,0);
    const auto action=cpu.invoke(0x823A4628);
    if(action>=102&&action<=101+2*Simpsons::NativeVideoSettings::nativeRowCount) {
        const auto row=(action-100)/2;
        const auto direction=action&1?1:-1;
        const auto before=rt.videoSettings.renderResolution;
        rt.videoSettings.step(row,direction);rt.videoSettingsPending=true;
        std::fprintf(stderr,"[NATIVE VIDEO MENU] action=%u row=%u direction=%d render_index=%u->%u label=\"%s\" value=\"%s\"\n",
            action,row,direction,before,rt.videoSettings.renderResolution,rt.videoSettings.renderLabel().c_str(),rt.videoSettings.label(row).c_str());
        labels(ctx,base);
    } else if(action==100+2*Simpsons::NativeVideoSettings::nativePageRow||action==101+2*Simpsons::NativeVideoSettings::nativePageRow) {
        cpu.invoke(0x827BF8F8,guestText(cpu,base,action&1?"VideoMenu.nextPage":"VideoMenu.previousPage"),0,target,0);
        labels(ctx,base);
        std::fprintf(stderr,"[NATIVE VIDEO MENU] switched page; native settings unchanged\n");
    } else if(action==0||action==100||action==101) {
        if(action)cpu.invoke(0x827BF8F8,guestText(cpu,base,action==100?"gizmoMoveLeft":"gizmoMoveRight"),0,target,0);
        // The existing brightness slider still uses the original save path.
        cpu.invoke(0x823A50E0,ctx.r3.u32);
    } else throw Simpsons::Failure("Invalid VideoMenu action");
    ctx.r3.u32=cpu.invoke(0x827BBFF8);
    if(rt.controllerCommands)std::fflush(stderr);
}
void SimpsonsNativeVideoLeave(PPCContext& ctx,uint8_t*) {
    auto& rt=*Simpsons::active;
    if(ctx.r30.u32==6){rt.videoSettings.save(rt.videoSettingsPath);std::fprintf(stderr,"[NATIVE VIDEO MENU] accepted and saved\n");}
    else if(ctx.r30.u32==7){rt.videoSettings=rt.videoSettingsBeforeMenu;rt.videoSettingsPending=true;std::fprintf(stderr,"[NATIVE VIDEO MENU] cancelled and restored\n");}
    if(rt.controllerCommands&&(ctx.r30.u32==6||ctx.r30.u32==7))std::fflush(stderr);
}

void SimpsonsNativeAptInputGateTrace(PPCContext& ctx,uint8_t* base) {
    if(!Simpsons::active||base!=Simpsons::active->base)return;
    struct HostState {
        const uint32_t floatingPoint=PPCFPSCRRegister::getcsr();
        const DWORD error=GetLastError();
        ~HostState(){PPCFPSCRRegister::restoreHostCSR(floatingPoint);SetLastError(error);}
    } hostState;
    // The gate's 112-byte frame retains its caller LR at +104. Current LR
    // belongs to its string comparison/cleanup, not the menu dispatcher.
    const auto caller=PPC_LOAD_U32(ctx.r1.u32+104);
    constexpr std::array callers{0x82391F38u,0x82392988u,0x82392F8Cu,0x82393980u,
        0x82398E88u,0x82399408u,0x82399DB8u,0x8239A7C8u,0x8239F354u,
        0x823A5788u,0x823A6C9Cu,0x823A9458u,0x823B1304u,0x823B25F0u};
    struct Sample {bool seen=false;uint32_t allowed=0,mode=0;};
    static thread_local std::array<Sample,callers.size()> samples{};
    for(size_t i=0;i<callers.size();++i)if(callers[i]==caller) {
        const auto allowed=ctx.r3.u32;
        const auto mode=caller==0x823A5788u?PPC_LOAD_U32(0x82D08DE0):0u;
        auto& sample=samples[i];
        if(!sample.seen||sample.allowed!=allowed||sample.mode!=mode) {
            sample={true,allowed,mode};
            std::fprintf(stderr,"[NATIVE APT INPUT GATE] caller=%08X allowed=%u options_mode=%u; original gate retained\n",caller,allowed,mode);
        }
        break;
    }
}

// Original Audio menu with the native Master volume row (tools/build_native_audio_menu.py).
namespace {
void audioLabel(PPCContext& ctx,uint8_t* base) {
    auto& rt=*Simpsons::active;Simpsons::EngineCpuCalls cpu(ctx,base);
    const auto target=cpu.invoke(0x827F20E0,ctx.r3.u32);
    const auto value=rt.audioSettings.label();
    if(value.size()>63)throw Simpsons::Failure("Audio UI label exceeds callback storage");
    const auto address=cpu.registers().r1.u32+0x60;
    std::memcpy(PPCGuestPointer(base,address,unsigned(value.size()+1),true),value.c_str(),value.size()+1);
    cpu.invoke(0x827BF8F8,guestText(cpu,base,"AudioMenu.set_mastervolume"),0,target,1,address);
}
void applyMasterVolume(Simpsons::Runtime& rt) {
    if(rt.engineAudioOutput)rt.engineAudioOutput->setMasterVolume(rt.audioSettings.gain());
}
}
void SimpsonsNativeAudioPopulate(PPCContext& ctx,uint8_t* base) {
    auto& rt=*Simpsons::active;rt.audioSettingsBeforeMenu=rt.audioSettings;
    Simpsons::EngineCpuCalls cpu(ctx,base);
    // Original 823A5438 body: populate the sliders and selector, then return.
    cpu.invoke(0x823A4E98,ctx.r3.u32);
    audioLabel(ctx,base);ctx.r3.u32=cpu.invoke(0x827BBFF8);
    std::fprintf(stderr,"[NATIVE AUDIO MENU] opened master=%u%%\n",rt.audioSettings.masterVolume);
    if(rt.controllerCommands)std::fflush(stderr);
}
void SimpsonsNativeAudioSave(PPCContext& ctx,uint8_t* base) {
    auto& rt=*Simpsons::active;Simpsons::EngineCpuCalls cpu(ctx,base);
    const auto target=cpu.invoke(0x827F20E0,ctx.r3.u32);
    cpu.invoke(0x827BF8F8,guestText(cpu,base,"AudioMenu.getNativeAction"),0,target,0);
    const auto action=cpu.invoke(0x823A4628);
    if(action==102||action==103) {
        rt.audioSettings.stepMaster(action&1?1:-1);applyMasterVolume(rt);audioLabel(ctx,base);
        std::fprintf(stderr,"[NATIVE AUDIO MENU] master=%u%%\n",rt.audioSettings.masterVolume);
    } else if(action==0||action==100||action==101) {
        if(action)cpu.invoke(0x827BF8F8,guestText(cpu,base,action==100?"gizmoMoveLeft":"gizmoMoveRight"),0,target,0);
        // Original 823A5460 body: the retail slider/subtitle save.
        cpu.invoke(0x823A4F50,ctx.r3.u32);
    } else throw Simpsons::Failure("Invalid AudioMenu action");
    ctx.r3.u32=cpu.invoke(0x827BBFF8);
    if(rt.controllerCommands)std::fflush(stderr);
}
void SimpsonsNativeAudioLeave(PPCContext& ctx,uint8_t*) {
    auto& rt=*Simpsons::active;
    if(ctx.r30.u32==6){rt.audioSettings.save(rt.audioSettingsPath);std::fprintf(stderr,"[NATIVE AUDIO MENU] accepted and saved master=%u%%\n",rt.audioSettings.masterVolume);}
    else if(ctx.r30.u32==7){rt.audioSettings=rt.audioSettingsBeforeMenu;applyMasterVolume(rt);std::fprintf(stderr,"[NATIVE AUDIO MENU] cancelled and restored master=%u%%\n",rt.audioSettings.masterVolume);}
    if(rt.controllerCommands&&(ctx.r30.u32==6||ctx.r30.u32==7))std::fflush(stderr);
}
