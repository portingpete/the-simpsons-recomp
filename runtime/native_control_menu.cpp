#include "native_control_menu.h"
#include "runtime.h"
#include "native_controllers.h"
#include "native_window.h"
#include "engine_cpu_calls.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
struct HostState {
    const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
    HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
};
struct MenuState {
    Simpsons::Runtime* runtime=nullptr;
    uint32_t movie=0,page=0,slot=0;
    int captureRow=-1;
    bool rejected=false;
} menu;
constexpr std::array names{"ctlpage","ctlrow0","ctlrow1","ctlrow2","ctlrow3",
    "ctlrow4","ctlrow5","ctlrow6","ctlrow7","ctlaccept"};
uint32_t guestText(Simpsons::EngineCpuCalls& cpu,uint8_t* base,const std::string& text,uint32_t offset,uint32_t capacity) {
    if(text.size()>=capacity)throw Simpsons::Failure("Control menu text exceeds callback storage");
    const auto address=cpu.registers().r1.u32+offset;
    std::memcpy(PPCGuestPointer(base,address,unsigned(text.size()+1),true),text.c_str(),text.size()+1);
    return address;
}
uint32_t target(Simpsons::EngineCpuCalls& cpu,uint32_t movie) {return cpu.invoke(0x827F20E0,movie);}
void method(Simpsons::EngineCpuCalls& cpu,uint8_t* base,uint32_t movie,const std::string& name) {
    const auto path=target(cpu,movie);
    cpu.invoke(0x827BF8F8,guestText(cpu,base,name,0xA0,80),0,path,0);
}
uint32_t action(Simpsons::EngineCpuCalls& cpu,uint8_t* base,uint32_t movie,const char* name) {
    method(cpu,base,movie,name);return cpu.invoke(0x823A4628);
}
void labels(Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    auto& rt=*menu.runtime;const auto path=target(cpu,menu.movie);
    std::array<std::string,10> values;
    if(menu.page<2) {
        values[0]="Keyboard "+std::to_string(menu.page+1)+"/2 - "+(menu.slot?"Secondary":"Primary")+"; Enter assigns";
        for(uint32_t i=0;i<8;++i) {
            const auto binding=Simpsons::ControlAction(menu.page*8+i);
            values[i+1]=std::string(Simpsons::NativeControlSettings::actionLabel(binding))+" ["+std::to_string(menu.slot+1)+"]: "+
                rt.controlSettings.bindingLabel(binding,0)+" / "+rt.controlSettings.bindingLabel(binding,1);
        }
        if(menu.captureRow>=0) {
            values[size_t(menu.captureRow)+1]=std::string(Simpsons::NativeControlSettings::actionLabel(Simpsons::ControlAction(menu.page*8+uint32_t(menu.captureRow))))+
                (menu.rejected?": Reserved key - choose another":": Press key; Esc cancels; Delete clears");
        }
    } else {
        values[0]="Mouse / Controller - page 3/3";
        values[1]="Mouse sensitivity: "+std::to_string(rt.controlSettings.mouseSensitivity)+"%";
        values[2]=std::string("Invert mouse X: ")+(rt.controlSettings.invertMouseX?"On":"Off");
        values[3]=std::string("Invert mouse Y: ")+(rt.controlSettings.invertMouseY?"On":"Off");
        values[4]="Reset keyboard and mouse defaults";
    }
    values[9]="Accept changes";
    // Apt's variadic bridge spills r7-r10 at +48..+79. Both text buffers sit
    // above that area and below the ABI's saved LR at +248.
    for(size_t i=0;i<values.size();++i)if(!values[i].empty()) {
        const auto value=guestText(cpu,base,values[i],0x60,64);
        cpu.invoke(0x827BF8F8,guestText(cpu,base,std::string("ControlsMenu.set_")+names[i],0xA0,80),0,path,1,value);
    }
}
void changePage(Simpsons::EngineCpuCalls& cpu,uint8_t* base,int direction) {
    menu.page=(menu.page+(direction>0?1u:2u))%3;
    method(cpu,base,menu.movie,menu.page==2?"ControlsMenu.nativeMousePage":"ControlsMenu.nativeKeyboardPage");
    labels(cpu,base);
}
bool matching(Simpsons::Runtime* rt,uint8_t* base) {
    return rt&&base==rt->base&&menu.runtime==rt&&menu.movie&&rt->window&&rt->window->keyboard;
}
void publish(Simpsons::Runtime& rt) {rt.window->keyboard->configureControls(rt.controlSettings);}
void edit(Simpsons::EngineCpuCalls& cpu,uint8_t* base,uint32_t row,int direction,bool select) {
    auto& rt=*menu.runtime;auto& keyboard=*rt.window->keyboard;
    if(keyboard.rebindActive())return;
    if(row==0)changePage(cpu,base,direction);
    else if(menu.page<2&&row<=8) {
        if(select) {
            menu.captureRow=int(row-1);menu.rejected=false;
            keyboard.beginRebind(Simpsons::ControlAction(menu.page*8+row-1),menu.slot);
            labels(cpu,base);
        } else {menu.slot=direction>0?1u:0u;labels(cpu,base);}
    } else if(menu.page==2&&row<=4) {
        // Reset is a deliberate Select action; direction keys only edit values.
        if(row!=4||select){rt.controlSettings.step(row-1,direction);publish(rt);labels(cpu,base);}
    } else if(menu.page==2&&row<=7) {
        method(cpu,base,menu.movie,select?"ControlsMenu.nativeControllerToggle":direction>0?"gizmoMoveRight":"gizmoMoveLeft");
        cpu.invoke(0x823A51E8,menu.movie); // Unchanged retail gamepad preferences.
    }
}
}

void SimpsonsNativeControlsPopulate(PPCContext& ctx,uint8_t* base) {
    HostState host;auto& rt=*Simpsons::active;
    if(base!=rt.base||!rt.window||!rt.window->keyboard)throw Simpsons::Failure("Control menu lacks native keyboard");
    rt.window->keyboard->cancelRebind();(void)rt.window->keyboard->takeRebindResult();
    rt.controlSettingsBeforeMenu=rt.controlSettings;publish(rt);menu={&rt,ctx.r3.u32};
    Simpsons::EngineCpuCalls cpu(ctx,base);
    cpu.invoke(0x823A5150,menu.movie); // Original gamepad invert/rumble population.
    labels(cpu,base);ctx.r3.u64=cpu.invoke(0x827BBFF8);
    std::fprintf(stderr,"[NATIVE CONTROL MENU] opened keyboard/mouse preferences\n");
}
void SimpsonsNativeControlsSave(PPCContext& ctx,uint8_t* base) {
    HostState host;auto* rt=Simpsons::active;
    if(!matching(rt,base))throw Simpsons::Failure("Control menu callback lacks its owner");
    Simpsons::EngineCpuCalls cpu(ctx,base);
    const auto value=action(cpu,base,menu.movie,"ControlsMenu.getNativeAction");
    if(value>=100&&value<=119)edit(cpu,base,(value-100)/2,value&1?1:-1,false);
    else if(value==0)cpu.invoke(0x823A51E8,menu.movie);
    else throw Simpsons::Failure("Invalid ControlsMenu action");
    ctx.r3.u64=cpu.invoke(0x827BBFF8);
}
void SimpsonsNativeControlsLeave(PPCContext& ctx,uint8_t* base) {
    HostState host;auto* rt=Simpsons::active;
    if(!matching(rt,base))return;
    const auto event=ctx.r30.u32;
    if(event!=6&&event!=7)return;
    auto& keyboard=*rt->window->keyboard;Simpsons::EngineCpuCalls cpu(ctx,base);
    if(keyboard.rebindActive()) {
        if(event==7){keyboard.cancelRebind();(void)keyboard.takeRebindResult();menu.captureRow=-1;menu.rejected=false;labels(cpu,base);}
        // Original event6/7 branches do no work when the event is neutral.
        ctx.r30.u64=0;return;
    }
    if(event==7) {
        rt->controlSettings=rt->controlSettingsBeforeMenu;publish(*rt);menu={};
        std::fprintf(stderr,"[NATIVE CONTROL MENU] cancelled and restored\n");return;
    }
    const auto selected=action(cpu,base,menu.movie,"ControlsMenu.getNativeSelection");
    const auto accept=menu.page==2?208u:209u;
    if(selected>=1200||selected==accept) {
        rt->controlSettings=keyboard.controls();rt->controlSettings.save(rt->controlSettingsPath);menu={};
        std::fprintf(stderr,"[NATIVE CONTROL MENU] accepted and saved\n");return;
    }
    if(selected<200||selected>accept)throw Simpsons::Failure("Invalid ControlsMenu selection");
    edit(cpu,base,selected-200,1,true);ctx.r30.u64=0;
}
void SimpsonsNativeControlMenuTick(PPCContext& ctx,uint8_t* base) {
    HostState host;auto* rt=Simpsons::active;if(!matching(rt,base))return;
    // The retail Options dispatcher uses mode4 for Controls. No Apt calls
    // occur after this movie leaves its native screen, including focus loss.
    if(PPCLoadU32(base,0x82D08DE0)!=4) {
        rt->window->keyboard->cancelRebind();(void)rt->window->keyboard->takeRebindResult();menu={};return;
    }
    auto& keyboard=*rt->window->keyboard;
    if(const auto result=keyboard.takeRebindResult()) {
        menu.rejected=result->status==Simpsons::Platform::NativeRebindStatus::Rejected;
        rt->controlSettings=keyboard.controls();
        if(result->status!=Simpsons::Platform::NativeRebindStatus::Rejected)menu.captureRow=-1;
        Simpsons::EngineCpuCalls cpu(ctx,base);labels(cpu,base);
        std::fprintf(stderr,"[NATIVE CONTROL MENU] capture action=%u slot=%u status=%u key=%u\n",
            unsigned(result->action),result->slot,unsigned(result->status),result->code);
    }
}
