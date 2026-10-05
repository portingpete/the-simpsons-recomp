#include "native_video_settings.h"
#include "runtime.h"
#include "native_window.h"
#include "engine_cpu_calls.h"
#include "renderer/native_backend.h"
#include <algorithm>
#include <fstream>
#include <array>
#include <cstdio>
#include <cstring>

namespace Simpsons {
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
    if(!(in>>version>>res>>mode>>sync>>rate) || version<1 || version>5 || mode>=2 || sync>=2 ||
       !(version==5?validFrameRate(rate):(rate==0||rate==60||rate==120)))return out;
    if(res>=(version>=4?windowResolutionCount:3u))return out;
    if(version==1)out={res,mode!=0,sync!=0,rate,res,0,0};
    else {
        uint32_t render{},filter{},aa{};
        if(!(in>>render>>filter) || render>=(version>=4?renderResolutionCount:5u) || filter>=4)return out;
        if(version>=3 && (!(in>>aa) || aa>=4))return out;
        out={res,mode!=0,sync!=0,rate,render,filter,aa};
        if(version==5) {
            uint32_t fov{},scale{},glow{},dof{},blur{};
            if(!(in>>fov>>scale>>glow>>dof>>blur) ||
               (fov!=0&&(fov<60||fov>110||fov%5)) ||
               std::find(renderScales.begin(),renderScales.end(),scale)==renderScales.end() ||
               glow>1||dof>1||blur>1)return {};
            out.fieldOfView=fov;out.renderScale=scale;
            out.bloom=glow!=0;out.depthOfField=dof!=0;out.motionBlur=blur!=0;
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
    std::ofstream out(file,std::ios::trunc);out<<5<<' '<<resolution<<' '<<fullscreen<<' '<<vsync<<' '<<frameRate<<' '<<renderResolution<<' '<<textureFiltering<<' '<<antialiasing
        <<' '<<fieldOfView<<' '<<renderScale<<' '<<bloom<<' '<<depthOfField<<' '<<motionBlur<<'\n';out.close();
    if(!out)throw Failure("Unable to save native video settings");
}
void NativeVideoSettings::step(uint32_t row,int direction) {
    const auto cycle=[&](uint32_t& value,const auto& choices,bool rendering=false) {
        const auto found=std::find(choices.begin(),choices.end(),value);
        if(found==choices.end())throw Failure("Invalid native video choice");
        const size_t original=size_t(found-choices.begin());
        for(size_t step=1;step<=choices.size();++step) {
            const size_t index=direction>0?(original+step)%choices.size():(original+choices.size()-step)%choices.size();
            value=choices[index];if(!rendering||validRendering())return;
        }
        throw Failure("No supported native rendering choice");
    };
    if(row==1) {
        // Keep persisted extent IDs stable, but expose the wide alternatives
        // alongside the standard resolutions instead of burying them after 4K.
        constexpr std::array order{0u,1u,2u,5u,3u,6u,7u,4u,8u};
        static_assert(order.size()==renderResolutionCount);
        cycle(renderResolution,order,true);
    }
    else if(row==2)resolution=(resolution+(direction>0?1u:windowResolutionCount-1))%windowResolutionCount;
    else if(row==3)fullscreen=!fullscreen;
    else if(row==4)vsync=!vsync;
    else if(row==5)cycle(frameRate,frameRates);
    else if(row==6)textureFiltering=(textureFiltering+(direction>0?1u:3u))%4;
    else if(row==7){constexpr std::array modes{0u,1u,2u,3u};cycle(antialiasing,modes,true);}
    else if(row==8){constexpr std::array fovs{0u,60u,65u,70u,75u,80u,85u,90u,95u,100u,105u,110u};cycle(fieldOfView,fovs);}
    else if(row==9)cycle(renderScale,renderScales,true);
    else if(row==10)bloom=!bloom;
    else if(row==11)depthOfField=!depthOfField;
    else if(row==12)motionBlur=!motionBlur;
    else throw Failure("Unknown native video settings row");
}
std::string NativeVideoSettings::renderLabel() const {
    const bool wide=uint64_t(renderWidth())*9>uint64_t(renderHeight())*16;
    return "Render resolution: "+std::to_string(renderWidth())+"x"+std::to_string(renderHeight())+
        (wide?" Ultrawide (restart)":" (restart)");
}
void applyNativeFrameRate(Runtime& rt) {
    if(rt.requestedFrameRate!=rt.videoSettings.frameRate)rt.framePacer.reset();
    rt.requestedFrameRate=rt.videoSettings.frameRate;
    rt.uncappedFrameRate=rt.videoSettings.frameRate==0;
    // Original 82718D48 publishes intervals at +144, consumed by 826B8080.
    // Update that existing owner as well as native pacing when changed live.
    const auto owner=PPCLoadU32(rt.base,0x82D576A0);
    if(owner)PPCStoreU32(rt.base,owner+144,rt.videoSettings.frameRate==60?1u:0u);
}
void applyNativeVideoSettings(Runtime& rt) {
    if(!rt.videoSettingsPending||!rt.window)return;
    rt.window->configureVideo(rt.videoSettings.width(),rt.videoSettings.height(),rt.videoSettings.fullscreen);
    applyNativeFrameRate(rt);
    rt.vsyncEnabled=rt.videoSettings.vsync;rt.videoSettingsPending=false;
    std::fprintf(stderr,"[NATIVE VIDEO] window=%ux%u fullscreen=%u vsync=%u frame_rate=%u render_index=%u render=%ux%u aa=%u anisotropy=%u fov=%u render_scale=%u bloom=%u dof=%u motion_blur=%u; render/AA/filter/scale changes apply after restart\n",
        rt.window->presentationWidth.load(),rt.window->presentationHeight.load(),rt.videoSettings.fullscreen,rt.videoSettings.vsync,rt.videoSettings.frameRate,
        rt.videoSettings.renderResolution,rt.videoSettings.renderWidth(),rt.videoSettings.renderHeight(),
        rt.videoSettings.antialiasing,rt.videoSettings.anisotropy(),rt.videoSettings.fieldOfView,rt.videoSettings.renderScale,
        rt.videoSettings.bloom,rt.videoSettings.depthOfField,rt.videoSettings.motionBlur);
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
    constexpr std::array aaNames{"Original","FXAA","FXAA + Original","SSAA 4x"};
    const std::array<std::string,Simpsons::NativeVideoSettings::nativeRowCount> values={video.renderLabel(),
        "Window size: "+std::to_string(video.width())+" x "+std::to_string(video.height()),
        std::string("Display mode: ")+(video.fullscreen?"Fullscreen":"Windowed"),
        std::string("VSync: ")+(video.vsync?"On":"Off"),"Frame limit: "+(video.frameRate?std::to_string(video.frameRate)+" FPS":std::string("Unlimited")),
        "Texture filtering: "+(video.textureFiltering?std::to_string(video.anisotropy())+"x":std::string("Original"))+" (restart)",
        std::string("Antialiasing: ")+aaNames[video.antialiasing]+" (restart)",
        "FOV (16:9): "+(video.fieldOfView?std::to_string(video.fieldOfView)+" degrees":std::string("Original")),
        "Render scale: "+std::to_string(video.renderScale)+"% (restart)",
        std::string("Bloom: ")+(video.bloom?"On":"Off"),
        std::string("Depth of field: ")+(video.depthOfField?"On":"Off"),
        std::string("Motion blur: ")+(video.motionBlur?"On":"Off")};
    const std::array names{"resolution","windowsize","windowmode","vsync","framecap","filtering","antialiasing","fov","renderscale","bloom","depthoffield","motionblur"};
    static_assert(names.size()==values.size());
    // Use the same string-argument Apt bridge as the original brightness row.
    for(size_t i=0;i<values.size();++i) {
        // The variadic bridge spills r7-r10 at +48 through +79 before reading
        // its string arguments. Keep both buffers outside that outgoing area.
        // Leave 64 bytes for each value, including the Ultrawide annotation,
        // before the method-name buffer at +0xA0. Both end below saved LR.
        if(values[i].size()>63)throw Simpsons::Failure("Video UI label exceeds callback storage");
        const auto value=cpu.registers().r1.u32+0x60;
        std::memcpy(PPCGuestPointer(base,value,unsigned(values[i].size()+1),true),values[i].c_str(),values[i].size()+1);
        cpu.invoke(0x827BF8F8,guestText(cpu,base,std::string("VideoMenu.set_")+names[i]),0,target,1,value);
    }
}
}
void SimpsonsNativeVideoPopulate(PPCContext& ctx,uint8_t* base) {
    Simpsons::active->videoSettingsBeforeMenu=Simpsons::active->videoSettings;
    const auto& video=Simpsons::active->videoSettings;
    std::fprintf(stderr,"[NATIVE VIDEO MENU] opened render_choices=%u render_index=%u render=%ux%u\n",
        Simpsons::NativeVideoSettings::renderResolutionCount,video.renderResolution,video.renderWidth(),video.renderHeight());
    Simpsons::EngineCpuCalls cpu(ctx,base);
    const auto target=cpu.invoke(0x827F20E0,ctx.r3.u32);
    // Preserve the original brightness population and Apt result convention.
    cpu.invoke(0x823A4958,target,0x82002A00,7);
    labels(ctx,base);ctx.r3.u32=cpu.invoke(0x827BBFF8);
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
        std::fprintf(stderr,"[NATIVE VIDEO MENU] action=%u row=%u direction=%d render_index=%u->%u label=\"%s\"\n",
            action,row,direction,before,rt.videoSettings.renderResolution,rt.videoSettings.renderLabel().c_str());
        labels(ctx,base);
    } else if(action==0||action==100||action==101) {
        if(action)cpu.invoke(0x827BF8F8,guestText(cpu,base,action==100?"gizmoMoveLeft":"gizmoMoveRight"),0,target,0);
        // The existing brightness slider still uses the original save path.
        cpu.invoke(0x823A50E0,ctx.r3.u32);
    } else throw Simpsons::Failure("Invalid VideoMenu action");
    ctx.r3.u32=cpu.invoke(0x827BBFF8);
}
void SimpsonsNativeVideoLeave(PPCContext& ctx,uint8_t*) {
    auto& rt=*Simpsons::active;
    if(ctx.r30.u32==6){rt.videoSettings.save(rt.videoSettingsPath);std::fprintf(stderr,"[NATIVE VIDEO MENU] accepted and saved\n");}
    else if(ctx.r30.u32==7){rt.videoSettings=rt.videoSettingsBeforeMenu;rt.videoSettingsPending=true;std::fprintf(stderr,"[NATIVE VIDEO MENU] cancelled and restored\n");}
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
