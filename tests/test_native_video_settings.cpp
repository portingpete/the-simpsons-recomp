#include "runtime/native_video_settings.h"
#include "runtime/native_window.h"
#include "runtime/native_controllers.h"
#include "runtime/runtime.h"
#include <fstream>
#include <cstdio>
#include <cstdlib>
using namespace Simpsons;
namespace {
void need(bool value,const char* why){if(!value)throw Failure(why);}
// Observe only this isolated test window. No hardware input or gameplay runs.
class WindowMutationMonitor {
    HWND window;
    static inline std::atomic<WNDPROC> originalProcedure=nullptr;
    static inline std::atomic<uint32_t> styleChanges=0,positionChanges=0;
    static inline std::atomic<bool> foregroundOwned=false;
    static LRESULT CALLBACK observe(HWND handle,UINT message,WPARAM wparam,LPARAM lparam) {
        if(GetForegroundWindow()==handle)foregroundOwned=true;
        if(message==WM_STYLECHANGED)styleChanges.fetch_add(1);
        if(message==WM_WINDOWPOSCHANGED)positionChanges.fetch_add(1);
        return CallWindowProcW(originalProcedure.load(),handle,message,wparam,lparam);
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
    bool resized() const {return positionChanges.load()>0;}
    bool neverForeground() const {return !foregroundOwned.load()&&GetForegroundWindow()!=window;}
};
}
int main() {
    const auto folder=std::filesystem::temp_directory_path()/(L"SimpsonsVideoTest-"+std::to_wstring(GetCurrentProcessId()));
    try {
        std::filesystem::create_directory(folder);const auto file=folder/"video.cfg";
        auto video=NativeVideoSettings::load(file);need(video.frameRate==120&&!video.vsync&&!video.fullscreen&&video.antialiasing==0,"Default video policy changed");
        for(const auto mode:{1u,2u,3u,0u}){video.step(7,1);need(video.antialiasing==mode,"Forward antialiasing cycle failed");}
        for(const auto mode:{3u,2u,1u,0u}){video.step(7,-1);need(video.antialiasing==mode,"Reverse antialiasing cycle failed");}
        video.step(1,-1);need(video.renderWidth()==5120&&video.renderHeight()==1440&&video.width()==1280,"Internal resolution did not change independently of window size");
        video.step(1,1);need(video.renderResolution==0,"Internal resolution did not wrap");
        video.step(2,-1);need(video.width()==5120&&video.height()==1440&&video.renderWidth()==1280,"Window size did not change independently of internal resolution");
        video.step(2,1);need(video.resolution==0,"Window resolution did not wrap");
        constexpr uint32_t renderWidths[]{1280,1600,1920,2560,3840,2560,3440,3840,5120};
        constexpr uint32_t renderHeights[]{720,900,1080,1440,2160,1080,1440,1600,1440};
        constexpr uint32_t renderCycle[]{0,1,2,5,3,6,7,4,8};
        constexpr uint32_t windowWidths[]{1280,1600,1920,2560,3440,3840,5120,2560,3840};
        constexpr uint32_t windowHeights[]{720,900,1080,1080,1440,1600,1440,1440,2160};
        constexpr uint32_t windowCycle[]{0,1,2,3,7,4,5,8,6};
        for(uint32_t i=1;i<=9;++i){
            video.step(1,1);const auto index=renderCycle[i%9];
            need(video.renderResolution==index&&video.renderWidth()==renderWidths[index]&&video.renderHeight()==renderHeights[index]&&video.width()==1280,"Forward render resolution cycle or preserved extent mapping failed");
            const auto label=video.renderLabel();
            need(label.find(std::to_string(renderWidths[index])+"x"+std::to_string(renderHeights[index]))!=std::string::npos,"Render label lost selected dimensions");
            need((label.find("Ultrawide")!=std::string::npos)==(index>=5),"Ultrawide render choices are not identified in the native row");
            need(label.find("(restart)")!=std::string::npos&&label.size()<64,"Render label lost restart notice or exceeds native Apt value storage");
        }
        for(uint32_t i=9;i>0;--i){video.step(1,-1);const auto index=renderCycle[i-1];need(video.renderResolution==index&&video.renderWidth()==renderWidths[index]&&video.renderHeight()==renderHeights[index]&&video.width()==1280,"Reverse render resolution cycle failed");}
        for(uint32_t i=1;i<=9;++i){video.step(2,1);const auto index=windowCycle[i%9];need(video.resolution==index&&video.width()==windowWidths[index]&&video.height()==windowHeights[index]&&video.renderWidth()==1280,"Forward window resolution cycle or preserved extent mapping failed");}
        for(uint32_t i=9;i>0;--i){video.step(2,-1);const auto index=windowCycle[i-1];need(video.resolution==index&&video.width()==windowWidths[index]&&video.height()==windowHeights[index]&&video.renderWidth()==1280,"Reverse window resolution cycle failed");}
        video.step(3,1);video.step(4,-1);video.step(5,1);need(video.frameRate==144,"120 FPS did not advance to 144 FPS");
        while(video.frameRate)video.step(5,1);
        video.step(6,-1);video.step(7,-1);
        need(video.fullscreen&&video.vsync&&video.frameRate==0,"Native controls did not change policy");
        video.step(1,1);video.step(1,1);
        video.save(file);const auto restored=NativeVideoSettings::load(file);
        need(restored.fullscreen&&restored.vsync&&restored.frameRate==0&&restored.anisotropy()==16&&restored.renderWidth()==1920&&restored.antialiasing==3,"Video preferences did not persist");
        {std::ifstream in(file);uint32_t version{};in>>version;need(version==6,"New video preferences did not use version 6");}
        for(const auto mode:{0u,1u,2u,3u}) {
            video.antialiasing=mode;video.save(file);const auto aa=NativeVideoSettings::load(file);
            need(aa.antialiasing==mode&&aa.anisotropy()==16&&aa.renderResolution==2&&aa.fullscreen&&aa.vsync&&aa.frameRate==0,"Antialiasing preference did not persist independently");
        }
        for(uint32_t render=5;render<9;++render)for(uint32_t output=3;output<9;++output) {
            auto wide=restored;wide.renderResolution=render;wide.resolution=output;wide.save(file);
            const auto loaded=NativeVideoSettings::load(file);
            need(loaded.renderWidth()==renderWidths[render]&&loaded.renderHeight()==renderHeights[render]&&loaded.width()==windowWidths[output]&&loaded.height()==windowHeights[output]&&loaded.fullscreen&&loaded.vsync&&loaded.frameRate==0&&loaded.anisotropy()==16&&loaded.antialiasing==3,"Independent ultrawide preferences did not persist");
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
        auto budget=expanded;budget.renderResolution=4;budget.antialiasing=3;budget.renderScale=100;
        need(budget.validRendering(),"Original 4K SSAA combination was lost");
        budget.step(9,1);need(budget.renderScale==50&&budget.validRendering(),"Render-scale cycle admitted an oversized scene");
        budget.renderScale=200;budget.antialiasing=2;budget.step(7,1);
        need(budget.antialiasing==0&&budget.validRendering(),"Antialiasing cycle admitted oversized SSAA");
        budget.renderResolution=2;budget.antialiasing=3;budget.renderScale=200;budget.step(1,1);
        need(budget.renderResolution==0&&budget.validRendering(),"Render cycle did not skip unsupported combinations");
        budget.step(1,-1);need(budget.renderResolution==2&&budget.validRendering(),"Reverse render cycle did not skip unsupported combinations");
        {std::ofstream out(file);out<<"1 2 1 1 60";}
        const auto legacy=NativeVideoSettings::load(file);
        need(legacy.width()==1920&&legacy.fullscreen&&legacy.vsync&&legacy.frameRate==60&&legacy.renderWidth()==1920&&legacy.antialiasing==0,"Legacy resolution preference did not migrate to internal rendering");
        {std::ofstream out(file);out<<"2 1 1 1 60 4 2";}
        const auto v2=NativeVideoSettings::load(file);
        need(v2.resolution==1&&v2.fullscreen&&v2.vsync&&v2.frameRate==60&&v2.renderResolution==4&&v2.textureFiltering==2&&v2.antialiasing==0,"Version 2 preferences did not preserve existing settings with Original antialiasing");
        {std::ofstream out(file);out<<"3 2 1 1 60 4 2 2";}
        const auto v3=NativeVideoSettings::load(file);
        need(v3.resolution==2&&v3.fullscreen&&v3.vsync&&v3.frameRate==60&&v3.renderResolution==4&&v3.textureFiltering==2&&v3.antialiasing==2,"Version 3 preferences did not preserve existing settings and antialiasing");
        v3.save(file);const auto migrated=NativeVideoSettings::load(file);
        need(migrated.width()==1920&&migrated.renderWidth()==3840&&migrated.renderHeight()==2160&&migrated.antialiasing==2,"Version 3 preference migration changed extent indices or antialiasing");
        {std::ofstream out(file);out<<"5 6 1 1 165 8 2 1 90 75 0 1 0";}
        const auto v5=NativeVideoSettings::load(file);
        need(v5.resolution==6&&v5.width()==5120&&v5.renderResolution==8&&v5.fieldOfView==90&&v5.renderScale==75&&!v5.bloom&&v5.depthOfField&&!v5.motionBlur&&v5.atmosphericFog&&v5.colorGrading&&v5.cinematicLetterbox,"Version 5 migration changed existing indices, effects or defaults");
        for(uint32_t output:{7u,8u}) {auto high=v5;high.resolution=output;high.save(file);need(NativeVideoSettings::load(file).resolution==output,"New standard window extent did not persist");}
        for(const char* malformed:{"2 0 0 0 120","2 0 0 0 120 5 0","2 3 0 0 120 2 0","2 0 0 0 120 2 4","3 2 1 1 60 2 2","3 2 1 1 60 2 2 4","3 2 1 1 60 5 2 1","3 3 1 1 60 2 2 1","3 2 1 1 60 2 4 1","4 7 1 1 60 8 2 1","4 6 1 1 60 9 2 1","4 6 1 1 60 8 2 4","4 6 1 1 60 8 2","5 2 1 1 60 2 2 1","5 0 0 0 120 0 0 0 55 100 1 1 1","5 0 0 0 120 0 0 0 111 100 1 1 1","5 0 0 0 120 0 0 0 70 101 1 1 1","5 0 0 0 120 0 0 0 70 100 2 1 1","5 0 0 0 121 0 0 0 70 100 1 1 1","5 0 0 0 120 4 0 3 70 200 1 1 1","1 3 0 0 120","1 0 2 0 120","1 0 0 1 144","broken"}) {
            {std::ofstream out(file);out<<malformed;}
            const auto defaults=NativeVideoSettings::load(file);need(defaults.resolution==0&&!defaults.fullscreen&&!defaults.vsync&&defaults.frameRate==120&&defaults.renderResolution==0&&defaults.textureFiltering==0&&defaults.antialiasing==0&&defaults.fieldOfView==0&&defaults.renderScale==100&&defaults.bloom&&defaults.depthOfField&&defaults.motionBlur,"Malformed settings were accepted");
        }
        for(const char* malformed:{"5 7 1 1 165 8 2 1 90 75 0 1 0","6 9 0 0 120 0 0 0 0 100 1 1 1 1 1 1","6 0 0 0 120 0 0 0 0 100 1 1 1","6 0 0 0 120 0 0 0 0 100 1 1 1 2 1 1","6 0 0 0 120 0 0 0 0 100 1 1 1 1 2 1","6 0 0 0 120 0 0 0 0 100 1 1 1 1 1 2"}) {
            {std::ofstream out(file);out<<malformed;}
            const auto defaults=NativeVideoSettings::load(file);
            need(defaults.resolution==0&&defaults.frameRate==120&&defaults.atmosphericFog&&defaults.colorGrading&&defaults.cinematicLetterbox,"Malformed version 6 screen effects or legacy extent accepted");
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
        need(mutations.unchanged(),"Unchanged Video settings rewrote the native window");
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
            need(mutations.unchanged(),"Repeated Video settings resized or restyled the native window");
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
        need(mutations.unchanged(),"Unchanged fullscreen output rewrote the native window");
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
