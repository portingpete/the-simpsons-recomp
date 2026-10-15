#include "runtime/native_video_settings.h"
#include "runtime/native_window.h"
#include "runtime/native_controllers.h"
#include "runtime/runtime.h"
#include "renderer/graphics_api.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <string>
#include <cstdio>
#include <cstdlib>
using namespace Simpsons;
void SimpsonsNativeVideoLeave(PPCContext&,uint8_t*);
namespace {
void need(bool value,const char* why){if(!value)throw Failure(why);}
// Observe only this isolated test window. No hardware input or gameplay runs.
// A rewrite is any style change, anything the window's own Video handler does,
// or a move, resize, z-order or visibility change from anywhere. After a
// captioned/popup transition DWM posts WM_DWMNCRENDERINGCHANGED, and uxtheme
// answers it by refreshing the frame (SWP_FRAMECHANGED without move, size or
// z-order). DWM decides when that arrives, so it is not a Video rewrite.
class WindowMutationMonitor {
    HWND window;
    static inline std::atomic<WNDPROC> originalProcedure=nullptr;
    static inline std::atomic<uint32_t> styleChanges=0,positionChanges=0;
    static inline std::atomic<bool> foregroundOwned=false;
    static inline uint32_t videoDepth=0; // Window thread only.
    static inline std::atomic<uint32_t> videoMessages=0;
    // The latest counted position changes, reported when an unchanged check fails.
    struct Change {UINT flags;int x,y,cx,cy;bool video;};
    static inline std::array<Change,8> changes{};
    static LRESULT CALLBACK observe(HWND handle,UINT message,WPARAM wparam,LPARAM lparam) {
        if(GetForegroundWindow()==handle)foregroundOwned=true;
        if(message==WM_STYLECHANGED)styleChanges.fetch_add(1);
        if(message==WM_WINDOWPOSCHANGED) {
            const auto& position=*reinterpret_cast<const WINDOWPOS*>(lparam);
            constexpr UINT unmoved=SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER;
            const bool frameOnly=(position.flags&unmoved)==unmoved && !(position.flags&(SWP_SHOWWINDOW|SWP_HIDEWINDOW));
            if(videoDepth || !frameOnly)
                changes[positionChanges.fetch_add(1)%changes.size()]={position.flags,position.x,position.y,position.cx,position.cy,videoDepth!=0};
        }
        if(message!=NativeWindow::videoMessage)return CallWindowProcW(originalProcedure.load(),handle,message,wparam,lparam);
        ++videoDepth;videoMessages.fetch_add(1);
        const LRESULT result=CallWindowProcW(originalProcedure.load(),handle,message,wparam,lparam);
        --videoDepth;
        return result;
    }
public:
    explicit WindowMutationMonitor(HWND handle):window(handle) {
        foregroundOwned=GetForegroundWindow()==window;
        originalProcedure=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window,GWLP_WNDPROC));
        need(SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(observe))!=0,"Unable to observe isolated window mutations");
        reset();
    }
    ~WindowMutationMonitor(){SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(originalProcedure.load()));}
    void reset(){styleChanges=0;positionChanges=0;}
    bool unchanged() const {return styleChanges.load()==0&&positionChanges.load()==0;}
    // Attribution needs the handler's message; guard against it drifting.
    bool observedVideo() const {return videoMessages.load()>0;}
    bool resized() const {return positionChanges.load()>0;}
    bool neverForeground() const {return !foregroundOwned.load()&&GetForegroundWindow()!=window;}
    void requireUnchanged(const char* why) const {
        if(unchanged())return;
        std::string text=std::string(why)+" (styles="+std::to_string(styleChanges.load())+", positions="+std::to_string(positionChanges.load());
        const uint32_t count=std::min<uint32_t>(positionChanges.load(),uint32_t(changes.size()));
        for(uint32_t i=0;i<count;++i) {
            const auto& c=changes[i];char item[96];
            std::snprintf(item,sizeof(item),"; flags=%04X pos=%d,%d size=%dx%d %s",c.flags,c.x,c.y,c.cx,c.cy,c.video?"video":"external");
            text+=item;
        }
        throw Failure(text+")");
    }
};
}
int main() {
    const auto folder=std::filesystem::temp_directory_path()/(L"SimpsonsVideoTest-"+std::to_wstring(GetCurrentProcessId()));
    try {
        std::filesystem::create_directory(folder);const auto file=folder/"video.cfg";
        // A fixed monitor keeps desktop-dependent choices deterministic.
        NativeVideoSettings::overrideDisplay({2560,1440},144,{{1280,720,60},{1920,1080,60},{1920,1080,144},
            {2560,1440,60},{2560,1440,120},{2560,1440,144}});
        auto video=NativeVideoSettings::load(file);need(video.frameRate==120&&!video.vsync&&!video.fullscreen&&video.antialiasing==0,"Default video policy changed");
        need(video.sharpening==0&&video.backgroundFrameRate==30&&video.maxFrameLatency==1,"Default presentation policy changed");
        need(video.renderResolution==NativeVideoSettings::matchDisplayResolution&&video.renderWidth()==1280&&video.renderHeight()==720&&
             video.textureFiltering==0&&video.anisotropy()==1&&!video.exclusiveFullscreen,"Default rendering does not match the default window");
        for(const auto mode:{4u,1u,5u,2u,7u,16u,0u}){video.step(7,1);need(video.antialiasing==mode,"Forward antialiasing cycle failed");}
        for(const auto mode:{16u,7u,2u,5u,1u,4u,0u}){video.step(7,-1);need(video.antialiasing==mode,"Reverse antialiasing cycle failed");}
        video.step(1,-1);need(video.renderWidth()==5120&&video.renderHeight()==1440&&video.width()==1280,"Internal resolution did not change independently of window size");
        video.step(1,1);need(video.renderResolution==NativeVideoSettings::matchDisplayResolution,"Internal resolution did not wrap to Match display");
        // Fixed internal extents are independent of the window below.
        video.renderResolution=0;
        video.step(2,-1);need(video.width()==5120&&video.height()==1440&&video.renderWidth()==1280,"Window size did not change independently of internal resolution");
        video.step(2,1);need(video.resolution==0,"Window resolution did not wrap");
        constexpr uint32_t renderWidths[]{1280,1600,1920,2560,3840,2560,3440,3840,5120,1280};
        constexpr uint32_t renderHeights[]{720,900,1080,1440,2160,1080,1440,1600,1440,720};
        constexpr uint32_t renderCycle[]{0,1,2,5,3,6,7,4,8,9};
        constexpr uint32_t windowWidths[]{1280,1600,1920,2560,3440,3840,5120,2560,3840};
        constexpr uint32_t windowHeights[]{720,900,1080,1080,1440,1600,1440,1440,2160};
        constexpr uint32_t windowCycle[]{0,1,2,3,7,4,5,8,6};
        for(uint32_t i=1;i<=10;++i){
            video.step(1,1);const auto index=renderCycle[i%10];
            need(video.renderResolution==index&&video.renderWidth()==renderWidths[index]&&video.renderHeight()==renderHeights[index]&&video.width()==1280,"Forward render resolution cycle or preserved extent mapping failed");
            const auto label=video.renderLabel();
            need(label.find(std::to_string(renderWidths[index])+"x"+std::to_string(renderHeights[index]))!=std::string::npos,"Render label lost selected dimensions");
            need((label.find("Ultrawide")!=std::string::npos)==(index>=5&&index<9),"Ultrawide render choices are not identified in the native row");
            need((label.find("Match display")!=std::string::npos)==(index==9),"Match display render choice is not identified in the native row");
            need(label.find("(restart)")==std::string::npos&&label.size()<64,"Live render label kept a restart notice or exceeds native Apt value storage");
        }
        for(uint32_t i=10;i>0;--i){video.step(1,-1);const auto index=renderCycle[i-1];need(video.renderResolution==index&&video.renderWidth()==renderWidths[index]&&video.renderHeight()==renderHeights[index]&&video.width()==1280,"Reverse render resolution cycle failed");}
        for(uint32_t i=1;i<=9;++i){video.step(2,1);const auto index=windowCycle[i%9];need(video.resolution==index&&video.width()==windowWidths[index]&&video.height()==windowHeights[index]&&video.renderWidth()==1280,"Forward window resolution cycle or preserved extent mapping failed");}
        for(uint32_t i=9;i>0;--i){video.step(2,-1);const auto index=windowCycle[i-1];need(video.resolution==index&&video.width()==windowWidths[index]&&video.height()==windowHeights[index]&&video.renderWidth()==1280,"Reverse window resolution cycle failed");}
        video.step(3,1);video.step(4,-1);video.step(5,1);need(video.frameRate==144,"120 FPS did not advance to 144 FPS");
        while(video.frameRate)video.step(5,1);
        video.step(6,-1);video.step(7,-1);
        need(video.fullscreen&&video.vsync&&video.frameRate==0,"Native controls did not change policy");
        video.step(1,1);video.step(1,1);
        video.save(file);const auto restored=NativeVideoSettings::load(file);
        need(restored.fullscreen&&!restored.exclusiveFullscreen&&restored.vsync&&restored.frameRate==0&&restored.textureFiltering==3&&restored.anisotropy()==1&&restored.renderWidth()==1920&&restored.antialiasing==16,"Video preferences did not persist");
        {std::ifstream in(file);uint32_t version{};in>>version;need(version==10,"New video preferences did not use version 10");}
        // Renderer (version 9): row 19 toggles when this build includes Vulkan,
        // persists, and is absent (D3D11) in version 8 preferences.
        need(restored.renderer==0,"Default renderer is not D3D11");
        {
            auto choice=restored;choice.step(19,1);
            need(choice.renderer==(Graphics::vulkanAvailable()?1u:0u),"Renderer row did not select Vulkan");
            choice.save(file);need(NativeVideoSettings::load(file).renderer==choice.renderer,"Renderer choice did not persist");
            choice.step(19,-1);need(choice.renderer==0,"Renderer row did not return to D3D11");
            {std::ofstream out(file);out<<"8 7 1 1 165 6 3 0 100 125 0 1 0 0 1 0 75 15 3";}
            const auto v8=NativeVideoSettings::load(file);need(v8.renderer==0&&v8.resolution==7&&v8.maxFrameLatency==3,"Version 8 preferences did not load with D3D11");
            {std::ofstream out(file);out<<"9 7 1 1 165 6 3 0 100 125 0 1 0 0 1 0 75 15 3 1";}
            const auto v9=NativeVideoSettings::load(file);need(v9.renderer==1&&v9.resolution==7&&v9.maxFrameLatency==3,"Version 9 renderer was not read");
            for(const char* malformed:{"9 7 1 1 165 6 3 0 100 125 0 1 0 0 1 0 75 15 3 2","9 7 1 1 165 6 3 0 100 125 0 1 0 0 1 0 75 15 3"}) {
                {std::ofstream out(file);out<<malformed;}
                const auto rejected=NativeVideoSettings::load(file);need(rejected.renderer==0&&rejected.resolution==0,"Malformed version 9 renderer was accepted");
            }
            restored.save(file);
        }
        for(const auto mode:NativeVideoSettings::antialiasingModes) {
            video.antialiasing=mode;video.save(file);const auto aa=NativeVideoSettings::load(file);
            need(aa.antialiasing==mode&&aa.textureFiltering==3&&aa.renderResolution==2&&aa.fullscreen&&aa.vsync&&aa.frameRate==0,"Antialiasing preference did not persist independently");
        }
        for(uint32_t render=5;render<9;++render)for(uint32_t output=3;output<9;++output) {
            auto wide=restored;wide.renderResolution=render;wide.resolution=output;wide.save(file);
            const auto loaded=NativeVideoSettings::load(file);
            need(loaded.renderWidth()==renderWidths[render]&&loaded.renderHeight()==renderHeights[render]&&loaded.width()==windowWidths[output]&&loaded.height()==windowHeights[output]&&loaded.fullscreen&&loaded.vsync&&loaded.frameRate==0&&loaded.textureFiltering==3&&loaded.antialiasing==16,"Independent ultrawide preferences did not persist");
        }
        video.step(5,1);need(video.frameRate==30,"Unlimited did not wrap to 30 FPS");
        video.step(5,-1);need(video.frameRate==0,"Reverse frame limit cycle failed");
        for(const auto rate:NativeVideoSettings::frameRates){video.step(5,1);need(video.frameRate==rate,"Expanded frame-rate forward cycle failed");}
        for(size_t i=NativeVideoSettings::frameRates.size()-1;i>0;--i){video.step(5,-1);need(video.frameRate==NativeVideoSettings::frameRates[i-1],"Expanded frame-rate reverse cycle failed");}
        video.step(5,-1);need(video.frameRate==0,"Expanded frame-rate reverse wrap failed");
        need(video.fieldOfView==0&&video.renderScale==100&&video.bloom&&video.depthOfField&&video.motionBlur,"Legacy visual defaults changed");
        for(uint32_t fov=60;fov<=110;fov+=5){video.step(8,1);need(video.fieldOfView==fov,"FOV forward cycle failed");}
        video.step(8,1);need(video.fieldOfView==0,"FOV did not return to Original");
        video.step(8,-1);need(video.fieldOfView==110,"FOV reverse cycle failed");
        video.step(9,-1);need(video.renderScale==75,"Render-scale reverse cycle failed");
        video.step(10,1);video.step(11,-1);video.step(12,1);
        need(video.atmosphericFog&&video.colorGrading&&video.cinematicLetterbox,"New screen effects did not retain original defaults");
        video.step(13,1);video.step(14,-1);video.step(15,1);
        video.frameRate=165;video.save(file);const auto expanded=NativeVideoSettings::load(file);
        need(expanded.fieldOfView==110&&expanded.renderScale==75&&!expanded.bloom&&!expanded.depthOfField&&!expanded.motionBlur&&expanded.frameRate==165&&!expanded.atmosphericFog&&!expanded.colorGrading&&!expanded.cinematicLetterbox,"Expanded graphics settings did not persist independently");
        for(uint32_t row=13;row<=15;++row) {video.step(row,-1);video.step(row,1);}
        need(!video.atmosphericFog&&!video.colorGrading&&!video.cinematicLetterbox,"New effect toggles did not cycle both directions");
        for(const auto sharp:{25u,50u,75u,100u,0u}) {video.step(16,1);need(video.sharpening==sharp,"Sharpening forward cycle failed");}
        video.step(16,-1);need(video.sharpening==100,"Sharpening reverse wrap failed");
        for(const auto rate:{60u,0u,15u,30u}) {video.step(17,1);need(video.backgroundFrameRate==rate,"Background frame-limit forward cycle failed");}
        video.step(17,-1);need(video.backgroundFrameRate==15,"Background frame-limit reverse cycle failed");
        for(const auto latency:{2u,3u,1u}) {video.step(18,1);need(video.maxFrameLatency==latency,"Frame queue forward cycle failed");}
        video.step(18,-1);need(video.maxFrameLatency==3,"Frame queue reverse wrap failed");
        video.save(file);const auto presentation=NativeVideoSettings::load(file);
        need(presentation.sharpening==100&&presentation.backgroundFrameRate==15&&presentation.maxFrameLatency==3&&presentation.antialiasing==16&&presentation.fieldOfView==110,"Presentation settings did not persist independently");
        auto budget=expanded;budget.renderResolution=4;budget.antialiasing=7;budget.renderScale=100;
        need(budget.validRendering(),"Supported 4K SMAA budget was lost");
        budget.step(9,1);need(budget.renderScale==125&&budget.validRendering(),"Render-scale cycle skipped a supported single-sample scene");
        budget.renderScale=200;need(budget.validRendering(),"4K at 200% lost its exact single-sample pixel budget");
        budget.renderScale=200;budget.antialiasing=2;budget.step(7,1);
        need(budget.antialiasing==7&&budget.validRendering(),"SMAA choice did not retain the supported render extent");
        budget.renderResolution=6;budget.antialiasing=7;budget.renderScale=200;budget.step(1,1);
        need(budget.renderResolution==7&&budget.validRendering(),"Render cycle skipped a supported single-sample combination");
        budget.step(1,-1);need(budget.renderResolution==6&&budget.validRendering(),"Reverse render cycle skipped a supported single-sample combination");
        {std::ofstream out(file);out<<"1 2 1 1 60";}
        const auto legacy=NativeVideoSettings::load(file);
        need(legacy.width()==1920&&legacy.fullscreen&&legacy.vsync&&legacy.frameRate==60&&legacy.renderWidth()==1920&&legacy.antialiasing==0,"Legacy resolution preference did not migrate to internal rendering");
        {std::ofstream out(file);out<<"2 1 1 1 60 4 2";}
        const auto v2=NativeVideoSettings::load(file);
        need(v2.resolution==1&&v2.fullscreen&&v2.vsync&&v2.frameRate==60&&v2.renderResolution==4&&v2.textureFiltering==0&&v2.anisotropy()==8&&v2.antialiasing==0,"Version 2 preferences did not preserve existing settings with Original antialiasing");
        {std::ofstream out(file);out<<"3 2 1 1 60 4 2 2";}
        const auto v3=NativeVideoSettings::load(file);
        need(v3.resolution==2&&v3.fullscreen&&v3.vsync&&v3.frameRate==60&&v3.renderResolution==4&&v3.textureFiltering==0&&v3.anisotropy()==8&&v3.antialiasing==2,"Version 3 preferences did not preserve existing settings and antialiasing");
        v3.save(file);const auto migrated=NativeVideoSettings::load(file);
        need(migrated.width()==1920&&migrated.renderWidth()==3840&&migrated.renderHeight()==2160&&migrated.antialiasing==2,"Version 3 preference migration changed extent indices or antialiasing");
        {std::ofstream out(file);out<<"5 6 1 1 165 8 2 1 90 75 0 1 0";}
        const auto v5=NativeVideoSettings::load(file);
        need(v5.resolution==6&&v5.width()==5120&&v5.renderResolution==8&&v5.fieldOfView==90&&v5.renderScale==75&&!v5.bloom&&v5.depthOfField&&!v5.motionBlur&&v5.atmosphericFog&&v5.colorGrading&&v5.cinematicLetterbox,"Version 5 migration changed existing indices, effects or defaults");
        {std::ofstream out(file);out<<"6 8 1 0 144 6 3 3 100 75 1 0 1 0 1 0";}
        const auto v6=NativeVideoSettings::load(file);
        need(v6.resolution==8&&v6.renderResolution==6&&v6.antialiasing==0&&v6.fieldOfView==100&&!v6.depthOfField&&!v6.atmosphericFog&&!v6.cinematicLetterbox&&v6.sharpening==0&&v6.backgroundFrameRate==30&&v6.maxFrameLatency==1,"Version 6 migration changed stored settings or new presentation defaults");
        v6.save(file);const auto migratedV6=NativeVideoSettings::load(file);
        need(migratedV6.resolution==8&&migratedV6.antialiasing==0&&!migratedV6.atmosphericFog&&migratedV6.backgroundFrameRate==30,"Version 6 migration did not survive saving version 8");
        for(const uint32_t retired:{3u,6u}) {
            {std::ofstream out(file);out<<"7 7 1 1 165 6 3 "<<retired<<" 100 125 0 1 0 0 1 0 75 15 3";}
            const auto old=NativeVideoSettings::load(file);
            need(old.antialiasing==0&&old.resolution==7&&old.renderResolution==6&&old.fullscreen&&old.vsync&&old.frameRate==165&&old.textureFiltering==0&&old.anisotropy()==16&&old.fieldOfView==100&&old.renderScale==125&&
                 !old.bloom&&old.depthOfField&&!old.motionBlur&&!old.atmosphericFog&&old.colorGrading&&!old.cinematicLetterbox&&old.sharpening==75&&old.backgroundFrameRate==15&&old.maxFrameLatency==3,
                 "Retired SSAA migration changed independent stored preferences");
            old.save(file);need(NativeVideoSettings::load(file).antialiasing==0,"Retired AA migration did not persist in version8");
        }
        // Known retired IDs are field-only migration in current preferences,
        // including a complete independent nondefault menu/pacing/effects gold.
        for(const uint32_t retired:{3u,6u,11u,12u,13u,14u,15u}) {
            {std::ofstream out(file);out<<"8 7 1 1 165 6 3 "<<retired<<" 100 125 0 1 0 0 1 0 75 15 3";}
            const auto old=NativeVideoSettings::load(file);
            need(old.antialiasing==0&&old.resolution==7&&old.renderResolution==6&&old.fullscreen&&old.vsync&&old.frameRate==165&&old.textureFiltering==0&&old.anisotropy()==16&&old.fieldOfView==100&&old.renderScale==125&&
                 !old.bloom&&old.depthOfField&&!old.motionBlur&&!old.atmosphericFog&&old.colorGrading&&!old.cinematicLetterbox&&old.sharpening==75&&old.backgroundFrameRate==15&&old.maxFrameLatency==3,
                 "Known retired/current temporal AA migration changed independent stored preferences");
            old.save(file);const auto migrated=NativeVideoSettings::load(file);
            need(migrated.antialiasing==0&&migrated.resolution==7&&migrated.renderResolution==6&&migrated.fullscreen&&migrated.vsync&&migrated.frameRate==165&&migrated.textureFiltering==0&&migrated.anisotropy()==16&&migrated.fieldOfView==100&&migrated.renderScale==125&&
                 !migrated.bloom&&migrated.depthOfField&&!migrated.motionBlur&&!migrated.atmosphericFog&&migrated.colorGrading&&!migrated.cinematicLetterbox&&migrated.sharpening==75&&migrated.backgroundFrameRate==15&&migrated.maxFrameLatency==3,
                 "Migrated Original choice or independent preferences did not survive the current-version save roundtrip");
        }
        // Removed MSAA 2x/4x/8x preferences migrate only the AA field, to SMAA,
        // in both version 8 and current version 9 files.
        for(const uint32_t version:{8u,9u})for(const uint32_t removed:{8u,9u,10u}) {
            {std::ofstream out(file);out<<version<<" 7 1 1 165 6 3 "<<removed<<" 100 125 0 1 0 0 1 0 75 15 3"<<(version>=9?" 0":"");}
            const auto old=NativeVideoSettings::load(file);
            need(old.antialiasing==7&&old.resolution==7&&old.renderResolution==6&&old.fullscreen&&old.vsync&&old.frameRate==165&&old.textureFiltering==0&&old.anisotropy()==16&&old.fieldOfView==100&&old.renderScale==125&&
                 !old.bloom&&old.depthOfField&&!old.motionBlur&&!old.atmosphericFog&&old.colorGrading&&!old.cinematicLetterbox&&old.sharpening==75&&old.backgroundFrameRate==15&&old.maxFrameLatency==3,
                 "Removed MSAA preference did not migrate to SMAA with independent preferences intact");
            old.save(file);need(NativeVideoSettings::load(file).antialiasing==7,"MSAA-to-SMAA migration did not persist in the current version");
        }
        // Native direct rendering validity rejects removed choices; it does not
        // silently perform the persistence migration for an explicit mode.
        for(const uint32_t excluded:{3u,6u,8u,9u,10u,11u,12u,13u,14u,15u,17u,0xffffffffu}) {
            NativeVideoSettings explicitChoice;explicitChoice.antialiasing=excluded;
            need(!explicitChoice.validRendering(),"Native rendering accepted an excluded/unknown explicit AA mode");
        }
        for(const uint32_t invalid:{16u,0xffffffffu}) {
            {std::ofstream out(file);out<<"8 2 1 1 165 2 3 "<<invalid<<" 100 125 0 1 0 0 1 0 75 15 3";}
            const auto rejected=NativeVideoSettings::load(file);
            need(rejected.antialiasing==0&&rejected.resolution==0&&rejected.frameRate==120,"Version8 accepted an unknown AA ID");
        }
        for(uint32_t output:{7u,8u}) {auto high=v5;high.resolution=output;high.save(file);need(NativeVideoSettings::load(file).resolution==output,"New standard window extent did not persist");}
        for(const char* malformed:{"2 0 0 0 120","2 0 0 0 120 5 0","2 3 0 0 120 2 0","2 0 0 0 120 2 4","3 2 1 1 60 2 2","3 2 1 1 60 2 2 4","3 2 1 1 60 5 2 1","3 3 1 1 60 2 2 1","3 2 1 1 60 2 4 1","4 7 1 1 60 8 2 1","4 6 1 1 60 9 2 1","4 6 1 1 60 8 2 4","4 6 1 1 60 8 2","5 2 1 1 60 2 2 1","5 0 0 0 120 0 0 0 55 100 1 1 1","5 0 0 0 120 0 0 0 111 100 1 1 1","5 0 0 0 120 0 0 0 70 101 1 1 1","5 0 0 0 120 0 0 0 70 100 2 1 1","5 0 0 0 121 0 0 0 70 100 1 1 1","1 3 0 0 120","1 0 2 0 120","1 0 0 1 144","broken"}) {
            {std::ofstream out(file);out<<malformed;}
            const auto defaults=NativeVideoSettings::load(file);need(defaults.resolution==0&&!defaults.fullscreen&&!defaults.vsync&&defaults.frameRate==120&&defaults.renderResolution==NativeVideoSettings::matchDisplayResolution&&defaults.textureFiltering==0&&defaults.antialiasing==0&&defaults.fieldOfView==0&&defaults.renderScale==100&&defaults.bloom&&defaults.depthOfField&&defaults.motionBlur,"Malformed settings were accepted");
        }
        for(const char* malformed:{"5 7 1 1 165 8 2 1 90 75 0 1 0","6 9 0 0 120 0 0 0 0 100 1 1 1 1 1 1","6 0 0 0 120 0 0 0 0 100 1 1 1","6 0 0 0 120 0 0 0 0 100 1 1 1 2 1 1","6 0 0 0 120 0 0 0 0 100 1 1 1 1 2 1","6 0 0 0 120 0 0 0 0 100 1 1 1 1 1 2"}) {
            {std::ofstream out(file);out<<malformed;}
            const auto defaults=NativeVideoSettings::load(file);
            need(defaults.resolution==0&&defaults.frameRate==120&&defaults.atmosphericFog&&defaults.colorGrading&&defaults.cinematicLetterbox,"Malformed version 6 screen effects or legacy extent accepted");
        }
        for(const char* malformed:{"7 0 0 0 120 0 0 0 0 100 1 1 1 1 1 1","7 0 0 0 120 0 0 7 0 100 1 1 1 1 1 1 0 30 1","7 0 0 0 120 0 0 0 0 100 1 1 1 1 1 1 20 30 1","7 0 0 0 120 0 0 0 0 100 1 1 1 1 1 1 0 120 1","7 0 0 0 120 0 0 0 0 100 1 1 1 1 1 1 0 30 0","7 0 0 0 120 0 0 0 0 100 1 1 1 1 1 1 0 30 4","6 0 0 0 120 0 0 4 0 100 1 1 1 1 1 1"}) {
            {std::ofstream out(file);out<<malformed;}
            const auto defaults=NativeVideoSettings::load(file);
            need(defaults.antialiasing==0&&defaults.sharpening==0&&defaults.backgroundFrameRate==30&&defaults.maxFrameLatency==1,"Malformed version 7 presentation choices or new AA in legacy preferences were accepted");
        }
        {
            // Display modes: Windowed -> Borderless -> Fullscreen (exclusive) -> Windowed.
            NativeVideoSettings modes;modes.step(3,1);
            need(modes.fullscreen&&!modes.exclusiveFullscreen&&modes.outputExtent()==std::array{2560u,1440u}&&
                 modes.renderWidth()==2560&&modes.renderHeight()==1440,"Borderless did not present and render at the desktop extent");
            need(modes.label(2)=="Display resolution: Desktop 2560 x 1440"&&modes.label(20)=="Refresh rate: Desktop (144 Hz)","Borderless labels changed");
            modes.step(2,1);modes.step(20,1);need(modes.outputExtent()==std::array{2560u,1440u}&&modes.refreshRate==0,"Borderless accepted a display mode change");
            modes.step(3,1);
            need(modes.fullscreen&&modes.exclusiveFullscreen&&modes.displayWidth==2560&&modes.displayHeight==1440&&
                 modes.exclusiveMode()==NativeDisplayMode{2560,1440,144},"Exclusive fullscreen did not start at the desktop mode and highest refresh");
            modes.step(20,1);need(modes.exclusiveMode().refresh==60&&modes.label(20)=="Refresh rate: 60 Hz","Refresh rate did not wrap to the lowest mode");
            modes.step(20,1);need(modes.exclusiveMode().refresh==120,"Refresh rate did not advance");
            modes.step(2,-1);need(modes.exclusiveMode()==NativeDisplayMode{1920,1080,144}&&modes.refreshRate==0&&
                 modes.renderWidth()==1920&&modes.renderHeight()==1080,"Exclusive resolution did not select an available mode and refresh");
            need(modes.label(2)=="Display resolution: 1920 x 1080"&&modes.label(3)=="Display mode: Fullscreen","Exclusive labels changed");
            modes.step(20,-1);need(modes.exclusiveMode().refresh==60,"Refresh rate reverse cycle failed");
            modes.step(2,-1);need(modes.exclusiveMode()==NativeDisplayMode{1280,720,60},"Exclusive resolution reverse cycle failed");
            modes.step(2,-1);need(modes.exclusiveMode().width==2560,"Exclusive resolution did not wrap");
            modes.save(file);const auto reloaded=NativeVideoSettings::load(file);
            need(reloaded.exclusiveFullscreen&&reloaded.displayWidth==2560&&reloaded.displayHeight==1440&&reloaded.refreshRate==modes.refreshRate,"Exclusive mode did not persist");
            modes.step(3,1);need(!modes.fullscreen&&!modes.exclusiveFullscreen&&modes.renderWidth()==1280,"Display mode did not return to Windowed");
            modes.step(3,-1);need(modes.fullscreen&&modes.exclusiveFullscreen,"Display mode reverse cycle failed");
            // Match display fits the supported 16:9..32:9, 1280x720..5120x2160 extents.
            using F=std::array<uint32_t,2>;
            need(NativeVideoSettings::fitRenderExtent(1920,1200)==F{1920,1080}&&NativeVideoSettings::fitRenderExtent(5120,2880)==F{3840,2160}&&
                 NativeVideoSettings::fitRenderExtent(1024,768)==F{1280,720}&&NativeVideoSettings::fitRenderExtent(3440,1440)==F{3440,1440}&&
                 NativeVideoSettings::fitRenderExtent(7680,1440)==F{5120,1440}&&NativeVideoSettings::fitRenderExtent(1600,900)==F{1600,900},
                 "Match display fitting changed");
            // Each version-10 row cycles, persists and labels within native Apt storage.
            NativeVideoSettings rows;
            for(uint32_t row=1;row<=NativeVideoSettings::nativeRowCount;++row) {
                const auto label=rows.label(row);need(!label.empty()&&label.size()<64,"Native Video label is empty or exceeds Apt storage");
            }
            for(const auto level:{2u,4u,8u,16u,1u}){rows.step(21,1);need(rows.anisotropy()==level,"Anisotropic filtering cycle failed");}
            for(const auto mode:{1u,2u,3u,0u}){rows.step(6,1);need(rows.textureFiltering==mode,"Texture filtering cycle failed");}
            rows.step(6,1);need(rows.label(6)=="Texture filtering: Nearest","Texture filtering label changed");
            rows.step(22,1);rows.step(23,1);need(!rows.lensFlare&&!rows.screenShake,"Lens flare and screen shake toggles failed");
            for(const auto quality:{2u,3u,0u,1u}){rows.step(24,1);need(rows.shadowQuality==quality,"Shadow quality cycle failed");}
            rows.step(24,1);rows.step(25,1);rows.step(25,1);rows.step(26,1);
            need(rows.shadowQuality==2&&rows.ambientOcclusion==2&&rows.upscaling==1,"Quality rows did not change");
            for(const auto look:{1u,2u,3u,4u,0u}){rows.step(27,1);need(rows.colorLook==look,"Color look cycle failed");}
            rows.step(28,1);need(rows.vibrance==10&&rows.colorLook==4,"Editing an adjustment did not select the Custom look");
            rows.step(29,-1);rows.step(30,1);rows.step(30,1);rows.step(31,-1);
            need(rows.saturation==-10&&rows.contrast==20&&rows.exposure==-10&&rows.label(30)=="Contrast: +20%","Custom adjustments changed");
            for(uint32_t i=0;i<6;++i)rows.step(28,-1);need(rows.vibrance==-50,"Vibrance did not stop at its lower bound order");
            rows.step(28,-1);need(rows.vibrance==50,"Vibrance did not wrap");
            for(const auto ink:{1u,2u,0u}){rows.step(32,1);need(rows.inkOutlines==ink,"Ink outline cycle failed");}
            rows.step(32,1);rows.step(33,1);rows.step(34,-1);rows.step(35,1);
            need(rows.inkOutlines==1&&rows.outlineStrength==75&&rows.outlineColor==NativeVideoSettings::outlineColorCount-1&&rows.eyeShading==1,"Outline rows did not change");
            rows.step(36,1);rows.step(37,1);rows.step(38,1);rows.step(39,-1);
            rows.step(40,-1);need(rows.startEpisode==NativeVideoSettings::episodeCount-1&&rows.label(40)=="New game episode: Meet Thy Player (restart)","New game episode did not wrap");
            rows.step(40,1);rows.step(40,1);need(rows.startEpisode==1,"New game episode cycle failed");
            need(rows.fpsCounter&&rows.menuFrameRate==30&&rows.skipIntros&&rows.characterDetail==3,"Performance rows did not change");
            rows.save(file);const auto v10=NativeVideoSettings::load(file);
            need(v10.textureFiltering==1&&v10.anisotropy()==1&&!v10.lensFlare&&!v10.screenShake&&v10.shadowQuality==2&&v10.ambientOcclusion==2&&
                 v10.upscaling==1&&v10.colorLook==4&&v10.vibrance==50&&v10.saturation==-10&&v10.contrast==20&&v10.exposure==-10&&
                 v10.inkOutlines==1&&v10.outlineStrength==75&&v10.outlineColor==NativeVideoSettings::outlineColorCount-1&&v10.eyeShading==1&&
                 v10.fpsCounter&&v10.menuFrameRate==30&&v10.skipIntros&&v10.characterDetail==3&&v10.startEpisode==1,"Version 10 rows did not persist");
            // Version 9 4x/8x/16x filtering keeps its anisotropy with Original filters.
            {std::ofstream out(file);out<<"9 7 1 1 165 6 2 0 100 125 0 1 0 0 1 0 75 15 3 1";}
            const auto v9=NativeVideoSettings::load(file);
            need(v9.textureFiltering==0&&v9.anisotropy()==8&&v9.renderResolution==6&&v9.renderer==1&&v9.shadowQuality==1&&v9.lensFlare,"Version 9 filtering did not migrate");
            const std::string good="10 0 0 0 120 9 0 16 0 100 1 1 1 1 1 1 0 30 1 0 ";
            for(const char* tail:{"0 0 0 0 1 1 1 1 0 0 0 0 0 0 0 0 50 0 0 0 0 0 0","2 0 0 0 1 1 1 1 0 0 0 0 0 0 0 0 50 0 0 0 0 0 0 0",
                                  "0 0 0 0 3 1 1 1 0 0 0 0 0 0 0 0 50 0 0 0 0 0 0 0","0 1280 0 0 1 1 1 1 0 0 0 0 0 0 0 0 50 0 0 0 0 0 0 0",
                                  "0 0 0 0 1 1 1 4 0 0 0 0 0 0 0 0 50 0 0 0 0 0 0 0","0 0 0 0 1 1 1 1 0 0 0 5 0 0 0 0 50 0 0 0 0 0 0 0",
                                  "0 0 0 0 1 1 1 1 0 0 0 15 0 0 0 0 50 0 0 0 0 0 0 0","0 0 0 0 1 1 1 1 0 0 0 0 0 0 0 0 60 0 0 0 0 0 0 0",
                                  "0 0 0 0 1 1 1 1 0 0 0 0 0 0 0 0 50 8 0 0 0 0 0 0","0 0 0 0 1 1 1 1 0 0 0 0 0 0 0 0 50 0 0 0 45 0 0 0",
                                  "0 0 0 0 1 1 1 1 0 0 0 0 0 0 0 0 50 0 0 0 0 0 4 0",
                                  "0 0 0 0 1 1 1 1 0 0 0 0 0 0 0 0 50 0 0 0 0 0 0 16"}) {
                {std::ofstream out(file);out<<good<<tail;}
                const auto rejected=NativeVideoSettings::load(file);
                need(rejected.antialiasing==0&&rejected.shadowQuality==1&&rejected.menuFrameRate==0,"Malformed version 10 preferences were accepted");
            }
            {std::ofstream out(file);out<<good<<"0 0 0 0 1 1 1 1 0 0 0 0 0 0 0 0 50 0 0 0 0 0 0 0";}
            need(NativeVideoSettings::load(file).antialiasing==16,"Valid version 10 TAA preference was rejected");
        }
        {
            Runtime rt;PPCContext menu{};rt.videoSettingsPath=file;
            rt.videoSettings=presentation;rt.videoSettings.save(file);rt.videoSettingsBeforeMenu=rt.videoSettings;
            rt.videoSettings.antialiasing=4;rt.videoSettings.sharpening=25;rt.videoSettings.backgroundFrameRate=0;rt.videoSettings.maxFrameLatency=1;
            rt.videoSettings.fieldOfView=65;rt.videoSettingsPending=false;menu.r30.u32=7;
            SimpsonsNativeVideoLeave(menu,rt.base);
            const auto cancelled=NativeVideoSettings::load(file);
            need(rt.videoSettings.antialiasing==16&&rt.videoSettings.sharpening==100&&rt.videoSettings.backgroundFrameRate==15&&rt.videoSettings.maxFrameLatency==3&&rt.videoSettings.fieldOfView==110&&rt.videoSettingsPending,
                 "Cancel did not restore all presentation and graphics previews");
            need(cancelled.sharpening==100&&cancelled.backgroundFrameRate==15&&cancelled.maxFrameLatency==3,"Cancel overwrote saved presentation preferences");
            rt.videoSettings.antialiasing=5;rt.videoSettings.sharpening=50;rt.videoSettings.backgroundFrameRate=0;rt.videoSettings.maxFrameLatency=2;menu.r30.u32=6;
            SimpsonsNativeVideoLeave(menu,rt.base);const auto accepted=NativeVideoSettings::load(file);
            need(accepted.antialiasing==5&&accepted.sharpening==50&&accepted.backgroundFrameRate==0&&accepted.maxFrameLatency==2&&accepted.fieldOfView==110,
                 "Accept did not persist the complete graphics and presentation menu session");
        }
        need(_putenv_s("SIMPSONS_BACKGROUND_WINDOW","1")==0,"Unable to configure non-activating Video fixture");
        NativeWindow window;
        WindowMutationMonitor mutations(window.handle());
        const auto interactive=[&]{
            const auto extendedStyle=GetWindowLongPtrW(window.handle(),GWL_EXSTYLE);
            need((extendedStyle&WS_EX_NOACTIVATE)!=0&&(extendedStyle&WS_EX_TRANSPARENT)==0,
                "Video fixture lost non-activating, hit-testable window style");
            need(mutations.neverForeground(),"Video fixture acquired desktop foreground");
            need(IsWindowVisible(window.handle())&&IsWindowEnabled(window.handle()),"Video settings hid or disabled the game window");
            POINT point{LONG(window.presentationWidth.load()/2),LONG(window.presentationHeight.load()/2)};
            need(ClientToScreen(window.handle(),&point)!=FALSE,"Game window client coordinates unavailable");
            need(SendMessageW(window.handle(),WM_NCHITTEST,0,MAKELPARAM(point.x,point.y))==HTCLIENT,"Game window client no longer accepts mouse hits");
        };
        interactive();
        window.configureVideo(1280,720,false);
        need(mutations.observedVideo(),"Window monitor did not observe the Video handler message");
        mutations.requireUnchanged("Unchanged Video settings rewrote the native window");
        interactive();
        for(const auto resolution:{0u,1u,2u,3u,4u,5u,6u,7u,8u,0u}) {
            video.resolution=resolution;window.configureVideo(video.width(),video.height(),false);
            if(window.presentationWidth!=video.width()||window.presentationHeight!=video.height())
                std::fprintf(stderr,"Requested %ux%u, actual %ux%u, DPI %u\n",video.width(),video.height(),window.presentationWidth.load(),window.presentationHeight.load(),GetDpiForWindow(window.handle()));
            need(window.presentationWidth==video.width()&&window.presentationHeight==video.height(),"Window resolution change failed");
            need(window.width==1280&&window.height==720,"Output change disturbed original engine dimensions");
            interactive();
            mutations.reset();
            // A short menu-navigation press must remain pending until the game
            // samples it, including when staged settings publish the same window.
            window.keyboard->focus(true);window.keyboard->key(VK_DOWN,true);window.keyboard->key(VK_DOWN,false);
            window.configureVideo(video.width(),video.height(),false);
            mutations.requireUnchanged("Repeated Video settings resized or restyled the native window");
            need((window.keyboard->sample().Gamepad.wButtons&XINPUT_GAMEPAD_DPAD_DOWN)!=0,"Repeated Video settings dropped queued keyboard input");
            need(window.keyboard->sample().Gamepad.wButtons==0,"Menu-navigation pulse remained latched after sampling");
            interactive();
        }
        mutations.reset();
        window.configureVideo(1280,720,true);need(window.presentationWidth>=1280&&window.presentationHeight>=720,"Fullscreen did not cover the display");
        need(mutations.resized(),"Real fullscreen transition did not resize the native window");
        interactive();
        const auto fullscreenWidth=window.presentationWidth.load(),fullscreenHeight=window.presentationHeight.load();
        mutations.reset();window.configureVideo(1280,720,true);window.configureVideo(3440,1440,true);
        mutations.requireUnchanged("Unchanged fullscreen output rewrote the native window");
        need(window.presentationWidth==fullscreenWidth&&window.presentationHeight==fullscreenHeight,"Window-size preference disturbed fullscreen desktop output");
        interactive();
        window.keyboard->focus(true);window.keyboard->key(VK_ESCAPE,true);window.keyboard->key(VK_ESCAPE,false);
        mutations.reset();
        window.configureVideo(1280,720,false);need(window.presentationWidth==1280&&window.presentationHeight==720,"Fullscreen exit failed to restore window size");
        need(mutations.resized(),"Real fullscreen exit did not resize the native window");
        need((window.keyboard->sample().Gamepad.wButtons&XINPUT_GAMEPAD_START)!=0,"Real fullscreen exit dropped queued pause input");
        need(window.keyboard->sample().Gamepad.wButtons==0,"Pause pulse remained latched after fullscreen exit");
        interactive();
        EnableWindow(window.handle(),FALSE);
        need(!IsWindowEnabled(window.handle())&&mutations.neverForeground(),"Completed Video fixture remained enabled or acquired foreground");
        std::filesystem::remove_all(folder);std::puts("Native video persistence, navigation cycles and real window changes passed");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());std::filesystem::remove_all(folder);return 1;}
}
