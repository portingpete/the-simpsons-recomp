#include "native_control_settings.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace Simpsons {
bool NativeControlSettings::reservedKey(uint32_t code) {
    return code==0x75||code==0x77||code==0x78||code==0x5B||code==0x5C;
}
bool NativeControlSettings::validKey(uint32_t code) {
    if(!code)return true;
    if(code>255||reservedKey(code))return false;
    // Win32 key-message virtual keys and all five physical mouse buttons.
    // Exclude packet/process pseudo keys and unassigned/reserved ranges.
    if(code==1||code==2||code==4||code==5||code==6)return true;
    if(code==8||code==9||code==12||code==13||code==16||code==17||code==18||code==19||code==20||code==27)return true;
    if(code>=0x20&&code<=0x2F)return true;
    if(code>=0x30&&code<=0x39)return true;
    if(code>=0x41&&code<=0x5A)return true;
    if(code==0x5D)return true;
    if(code>=0x60&&code<=0x87)return true;
    if(code==0x90||code==0x91)return true;
    if(code>=0xA0&&code<=0xB7)return true;
    if(code>=0xBA&&code<=0xC0)return true;
    return (code>=0xDB&&code<=0xDF)||code==0xE2;
}
bool NativeControlSettings::valid() const {
    if(mouseSensitivity<25||mouseSensitivity>300||mouseSensitivity%25)return false;
    std::array<bool,256> used{};
    for(const auto& action:bindings)for(const auto code:action) {
        if(!validKey(code)||(code&&used[code]))return false;
        if(code)used[code]=true;
    }
    return true;
}
NativeControlSettings NativeControlSettings::load(const std::filesystem::path& file) {
    NativeControlSettings out;
    std::ifstream in(file);uint32_t version=0,sensitivity=0,invertX=0,invertY=0;
    if(!(in>>version>>sensitivity>>invertX>>invertY)||version!=1||invertX>1||invertY>1)return {};
    out.mouseSensitivity=sensitivity;out.invertMouseX=invertX!=0;out.invertMouseY=invertY!=0;
    for(auto& action:out.bindings)for(auto& code:action)if(!(in>>code))return {};
    in>>std::ws;
    if(!in.eof()||!out.valid())return {};
    return out;
}
void NativeControlSettings::save(const std::filesystem::path& file) const {
    if(file.empty())return;
    if(!valid())throw std::runtime_error("Cannot save invalid native controls");
    if(!file.parent_path().empty())std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file,std::ios::trunc);
    out<<1<<' '<<mouseSensitivity<<' '<<invertMouseX<<' '<<invertMouseY<<'\n';
    for(const auto& action:bindings)out<<action[0]<<' '<<action[1]<<'\n';
    out.close();if(!out)throw std::runtime_error("Unable to save native control settings");
}
void NativeControlSettings::reset(){*this={};}
void NativeControlSettings::setBinding(ControlAction action,uint32_t slot,uint32_t code) {
    const auto index=uint32_t(action);
    if(index>=actionCount||slot>=slotCount||!validKey(code))throw std::runtime_error("Invalid native control binding");
    auto& current=bindings[index][slot];
    if(current==code)return;
    if(code)for(uint32_t other=0;other<actionCount;++other)for(uint32_t alias=0;alias<slotCount;++alias)
        if(bindings[other][alias]==code){std::swap(current,bindings[other][alias]);return;}
    current=code;
}
void NativeControlSettings::step(uint32_t row,int direction) {
    if(row==0)mouseSensitivity=uint32_t((int(mouseSensitivity)-25+(direction>0?25:275))%300)+25;
    else if(row==1)invertMouseX=!invertMouseX;
    else if(row==2)invertMouseY=!invertMouseY;
    else if(row==3)reset();
    else throw std::runtime_error("Unknown native control settings row");
}
std::string NativeControlSettings::actionLabel(ControlAction action) {
    constexpr std::array names{"Move forward","Move backward","Move left","Move right","Jump","Attack","Special attack","Action / interact",
        "Switch character","Target lock","Left stick click","Right stick click","Character menu","Special power","Left trigger","Pause"};
    const auto index=uint32_t(action);if(index>=names.size())throw std::runtime_error("Unknown native control action");
    return names[index];
}
std::string NativeControlSettings::keyLabel(uint32_t code) {
    if((code>=0x30&&code<=0x39)||(code>=0x41&&code<=0x5A))return std::string(1,char(code));
    if(code>=0x70&&code<=0x87)return "F"+std::to_string(code-0x6F);
    if(code>=0x60&&code<=0x69)return "Num "+std::to_string(code-0x60);
    switch(code) {
    case 0:return "Unbound";
    case 1:return "Mouse 1";case 2:return "Mouse 2";case 4:return "Mouse 3";case 5:return "Mouse 4";case 6:return "Mouse 5";
    case 8:return "Backspace";case 9:return "Tab";case 12:return "Clear";case 13:return "Enter";
    case 16:return "Shift";case 17:return "Ctrl";case 18:return "Alt";case 19:return "Pause";case 20:return "Caps Lock";case 27:return "Escape";
    case 32:return "Space";case 33:return "Page Up";case 34:return "Page Down";case 35:return "End";case 36:return "Home";
    case 37:return "Left";case 38:return "Up";case 39:return "Right";case 40:return "Down";case 44:return "Print Screen";case 45:return "Insert";case 46:return "Delete";
    case 93:return "Menu";case 106:return "Num *";case 107:return "Num +";case 108:return "Num Enter";case 109:return "Num -";case 110:return "Num .";case 111:return "Num /";
    case 144:return "Num Lock";case 145:return "Scroll Lock";case 160:return "L Shift";case 161:return "R Shift";case 162:return "L Ctrl";case 163:return "R Ctrl";case 164:return "L Alt";case 165:return "R Alt";
    case 186:return ";";case 187:return "=";case 188:return ",";case 189:return "-";case 190:return ".";case 191:return "/";case 192:return "`";
    case 219:return "[";case 220:return "\\";case 221:return "]";case 222:return "'";case 226:return "OEM 102";
    default:return "Key "+std::to_string(code);
    }
}
std::string NativeControlSettings::bindingLabel(ControlAction action,uint32_t slot) const {
    const auto index=uint32_t(action);if(index>=actionCount||slot>=slotCount)throw std::runtime_error("Unknown native control binding slot");
    return keyLabel(bindings[index][slot]);
}
}
