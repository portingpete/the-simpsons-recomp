#include "runtime/native_control_settings.h"
#include <windows.h>
#include <fstream>
#include <cstdio>
#include <sstream>
#include <stdexcept>
using namespace Simpsons;
namespace {
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::string serialize(const NativeControlSettings& settings,uint32_t version=2) {
    std::ostringstream out;out<<version<<' '<<settings.mouseSensitivity<<' '<<settings.invertMouseX<<' '<<settings.invertMouseY<<'\n';
    const uint32_t count=version==1?NativeControlSettings::version1ActionCount:NativeControlSettings::actionCount;
    for(uint32_t action=0;action<count;++action)out<<settings.bindings[action][0]<<' '<<settings.bindings[action][1]<<'\n';
    return out.str();
}
}
int main() {
    const auto folder=std::filesystem::temp_directory_path()/(L"SimpsonsControlsTest-"+std::to_wstring(GetCurrentProcessId()));
    try {
        std::filesystem::create_directory(folder);const auto file=folder/"profile.controls.cfg";
        const NativeControlSettings defaults;
        need(defaults.valid()&&NativeControlSettings::load(file)==defaults,"Missing control settings did not select valid defaults");
        need(defaults.bindings[uint32_t(ControlAction::Attack)]==std::array<uint32_t,2>{'J',VK_LBUTTON},"Default keyboard/mouse attack aliases changed");
        need(defaults.bindings[uint32_t(ControlAction::LeftTrigger)]==std::array<uint32_t,2>{VK_LCONTROL,VK_RCONTROL},"Default Ctrl aliases changed");
        need(NativeControlSettings::actionCount==23&&defaults.bindings[uint32_t(ControlAction::FreeCamera)][0]==VK_F7&&
             defaults.bindings[uint32_t(ControlAction::PhotoMode)][0]=='P'&&defaults.bindings[uint32_t(ControlAction::Walk)][0]=='C'&&
             defaults.bindings[uint32_t(ControlAction::LookUp)][0]==VK_NUMPAD8,"Version 2 default actions changed");
        need(NativeControlSettings::actionLabel(ControlAction::PhotoMode)=="Photo mode"&&
             NativeControlSettings::actionLabel(ControlAction::FreeCamera)=="Free camera","Version 2 action labels differ");
        auto settings=defaults;
        settings.setBinding(ControlAction::Jump,0,'J');
        need(settings.bindings[uint32_t(ControlAction::Jump)][0]=='J'&&settings.bindings[uint32_t(ControlAction::Attack)][0]==VK_SPACE&&settings.valid(),"Primary conflict did not exchange the displaced binding");
        settings.setBinding(ControlAction::Jump,1,VK_LBUTTON);
        need(settings.bindings[uint32_t(ControlAction::Jump)][1]==VK_LBUTTON&&settings.bindings[uint32_t(ControlAction::Attack)][1]==VK_RETURN&&settings.valid(),"Secondary conflict did not preserve the displaced action");
        settings.setBinding(ControlAction::MoveForward,1,VK_XBUTTON2);settings.setBinding(ControlAction::CharacterMenu,1,0);
        settings.step(0,-1);settings.step(1,1);settings.step(2,1);
        settings.save(file);need(NativeControlSettings::load(file)==settings,"Bindings and camera options did not persist independently");
        {std::ifstream in(file);uint32_t version=0;in>>version;need(version==2,"Controls persistence version changed");}
        // Wheel directions bind like keys and keep their own labels.
        auto wheel=settings;wheel.setBinding(ControlAction::SwitchCharacter,1,NativeControlSettings::wheelUp);
        wheel.setBinding(ControlAction::TargetLock,1,NativeControlSettings::wheelDown);
        need(wheel.valid()&&wheel.bindingLabel(ControlAction::SwitchCharacter,1)=="Wheel up"&&
             wheel.bindingLabel(ControlAction::TargetLock,1)=="Wheel down","Mouse wheel bindings differ");
        wheel.save(file);need(NativeControlSettings::load(file)==wheel,"Mouse wheel bindings did not persist");
        // Version 1 files keep their 16 original actions; new actions keep
        // their defaults unless that key is already used, then start unbound.
        auto legacy=defaults;legacy.bindings[uint32_t(ControlAction::Jump)][0]='C';legacy.bindings[uint32_t(ControlAction::Attack)][1]='P';
        {std::ofstream out(file);out<<serialize(legacy,1);}
        const auto migrated=NativeControlSettings::load(file);
        need(migrated.bindings[uint32_t(ControlAction::Jump)][0]=='C'&&migrated.bindings[uint32_t(ControlAction::Attack)][1]=='P'&&
             migrated.bindings[uint32_t(ControlAction::Walk)][0]==0&&migrated.bindings[uint32_t(ControlAction::PhotoMode)][0]==0&&
             migrated.bindings[uint32_t(ControlAction::FreeCamera)][0]==VK_F7&&migrated.bindings[uint32_t(ControlAction::LookUp)][0]==VK_NUMPAD8&&
             migrated.valid(),"Version 1 controls did not migrate");
        settings.mouseSensitivity=25;settings.step(0,-1);need(settings.mouseSensitivity==300,"Sensitivity did not wrap backwards");
        settings.step(0,1);need(settings.mouseSensitivity==25,"Sensitivity did not wrap forwards");
        for(uint32_t value=50;value<=300;value+=25){settings.step(0,1);need(settings.mouseSensitivity==value,"Sensitivity skipped a 25 percent step");}
        settings.step(3,1);need(settings==defaults,"Reset did not restore bindings and mouse options");
        constexpr std::array<uint32_t,7> rejectedKeys{VK_F6,VK_F8,VK_F9,VK_LWIN,VK_RWIN,256u,0xFFu};
        for(const uint32_t code:rejectedKeys) {
            need(!NativeControlSettings::validKey(code),"Reserved or nonphysical virtual key was accepted");
            bool rejected=false;try{settings.setBinding(ControlAction::Jump,0,code);}catch(const std::runtime_error&){rejected=true;}
            need(rejected&&settings==defaults,"Rejected binding changed stored settings");
        }
        for(const uint32_t code:{VK_XBUTTON1,VK_XBUTTON2,VK_RSHIFT,VK_F12,VK_OEM_1,VK_NUMPAD5})need(NativeControlSettings::validKey(code),"Physical key/button was rejected");
        need(NativeControlSettings::keyLabel(VK_XBUTTON1)=="Mouse 4"&&NativeControlSettings::keyLabel(VK_XBUTTON2)=="Mouse 5"&&NativeControlSettings::keyLabel(VK_F12)=="F12"&&settings.bindingLabel(ControlAction::Jump,0)=="Space","Binding labels differ from physical keys");
        auto changed=defaults;changed.mouseSensitivity=175;changed.setBinding(ControlAction::MoveForward,0,'I');
        auto malformed=changed;malformed.bindings[0][1]=malformed.bindings[1][0];const auto badDuplicate=serialize(malformed);
        malformed=changed;malformed.bindings[0][0]=VK_F6;const auto badReserved=serialize(malformed);
        malformed=changed;malformed.mouseSensitivity=101;const auto badSensitivity=serialize(malformed);
        auto badVersion=serialize(changed);badVersion[0]='3';
        auto badBoolean=serialize(changed);badBoolean.replace(6,1,"2");const auto valid=serialize(changed);
        for(const auto& value:{std::string("broken"),std::string("1 100 0 0\n"),std::string("2 100 0 0\n")+serialize(changed,1).substr(10),valid.substr(0,valid.size()-5),valid+"garbage",badVersion,badDuplicate,badReserved,badSensitivity,badBoolean}) {
            {std::ofstream out(file);out<<value;}
            need(NativeControlSettings::load(file)==defaults,"Invalid controls file partially applied settings");
        }
        std::filesystem::remove(file);std::filesystem::remove(folder);
        std::puts("PASS native controls persistence (version 2 and version 1 migration), physical key and wheel labels, conflict exchange, validated configuration and mouse option cycles");return 0;
    }catch(const std::exception& e) {
        std::fprintf(stderr,"FAIL native controls: %s\n",e.what());
        std::error_code ignored;std::filesystem::remove(folder/"profile.controls.cfg",ignored);std::filesystem::remove(folder,ignored);return 1;
    }
}
