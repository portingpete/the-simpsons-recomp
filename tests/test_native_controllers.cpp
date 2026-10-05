#include "runtime/runtime.h"
#include "runtime/native_controllers.h"
#include "runtime/native_input_recording.h"
#include "runtime/native_window.h"
#include "runtime/engine_cpu_calls.h"
#include <array>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <tuple>
#include <fstream>
#include <iterator>

namespace Simpsons::Platform {
struct NativeCommandInputTestAccess {
    inline static ULONGLONG now=1000;
    static ULONGLONG WINAPI tick(){return now;}
    static void bind(NativeCommandInput& input){input.clock=tick;}
};
struct NativeControllersTestAccess {
    static void bind(NativeControllers& source,decltype(&XInputGetState) state,decltype(&XInputGetCapabilities) caps,decltype(&XInputSetState) set){source.getState=state;source.disconnectedPollMs=0;source.getCapabilities=caps;source.setState=set;}
};
}
PPC_EXTERN_FUNC(__imp__XamInputGetState);
PPC_EXTERN_FUNC(__imp__XamInputGetCapabilities);
PPC_EXTERN_FUNC(__imp__XamInputSetState);
PPC_EXTERN_FUNC(__imp__XGetVideoMode);
void SimpsonsNativeMovieInputBegin(PPCContext&,uint8_t*);
void SimpsonsNativeMovieInputEnd(PPCContext&,uint8_t*);
namespace {
using namespace Simpsons;
size_t checks{};
void need(bool b,const char* s){++checks;if(!b)throw Failure(s);}
template<class F>void rejects(F&& f){bool bad=false;try{f();}catch(const Failure&){bad=true;}need(bad,"Unqualified controller query was accepted");}
struct Sample {DWORD status=ERROR_DEVICE_NOT_CONNECTED;XINPUT_STATE state{};XINPUT_CAPABILITIES caps{};XINPUT_VIBRATION vibration{};uint32_t reads{},capReads{},setReads{};};
std::array<Sample,4> samples;
DWORD WINAPI stateQuery(DWORD slot,XINPUT_STATE* result) noexcept {if(slot>=4||!result)return ERROR_BAD_ARGUMENTS;auto& s=samples[slot];++s.reads;*result=s.state;return s.status;}
DWORD WINAPI capsQuery(DWORD slot,DWORD flags,XINPUT_CAPABILITIES* result) noexcept {if(slot>=4||flags>1||!result)return ERROR_BAD_ARGUMENTS;auto& s=samples[slot];++s.capReads;*result=s.caps;return s.status;}
DWORD WINAPI setQuery(DWORD slot,XINPUT_VIBRATION* value) noexcept {if(slot>=4||!value)return ERROR_BAD_ARGUMENTS;auto& s=samples[slot];++s.setReads;s.vibration=*value;return s.status;}
std::shared_ptr<Platform::NativeControllers> source(){auto s=std::make_shared<Platform::NativeControllers>();Platform::NativeControllersTestAccess::bind(*s,stateQuery,capsQuery,setQuery);return s;}
struct Abi{uint64_t sp,lr;std::array<uint64_t,18> gpr,fpr;bool operator==(const Abi&)const=default;};
struct CommandFixture {
    inline static std::atomic<uint64_t> nextId{0};
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("Simpsons-input-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+
         std::to_string(nextId.fetch_add(1,std::memory_order_relaxed))+".commands");
    HANDLE writer=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    CommandFixture(){need(writer!=INVALID_HANDLE_VALUE,"Cannot create exclusive controller command fixture");}
    ~CommandFixture(){CloseHandle(writer);DeleteFileW(path.c_str());}
    void append(std::string_view text){DWORD written=0;need(WriteFile(writer,text.data(),DWORD(text.size()),&written,nullptr)&&written==text.size()&&FlushFileBuffers(writer),"Cannot append controller command fixture");}
};
Abi abi(const PPCContext& c){return {c.r1.u64,c.lr,{c.r14.u64,c.r15.u64,c.r16.u64,c.r17.u64,c.r18.u64,c.r19.u64,c.r20.u64,c.r21.u64,c.r22.u64,c.r23.u64,c.r24.u64,c.r25.u64,c.r26.u64,c.r27.u64,c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64},
    {c.f14.u64,c.f15.u64,c.f16.u64,c.f17.u64,c.f18.u64,c.f19.u64,c.f20.u64,c.f21.u64,c.f22.u64,c.f23.u64,c.f24.u64,c.f25.u64,c.f26.u64,c.f27.u64,c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64}};}
void hostQueries(){
    Platform::NativeControllers native;
    for(uint32_t slot=0;slot<4;++slot){XINPUT_STATE state{};const auto s=native.state(slot,state);need(s==0||s==ERROR_DEVICE_NOT_CONNECTED,"Unexpected native controller state");
        XINPUT_CAPABILITIES caps{};const auto c=native.capabilities(slot,1,caps);need(c==0||c==ERROR_DEVICE_NOT_CONNECTED,"Unexpected native controller capabilities");
        std::printf("Actual Windows input slot%u state=%lu capabilities=%lu\n",slot,s,c);}
}
void keyboardQueries(){
    samples={};auto native=source();auto keys=std::make_shared<Platform::NativeKeyboard>();native->attachKeyboard(keys);
    need(native->usesKeyboardMouse(),"Attached native keyboard did not select native prompts before the first poll");
    XINPUT_STATE state{};XINPUT_CAPABILITIES caps{};
    need(native->state(0,state)==ERROR_SUCCESS&&!state.Gamepad.wButtons,"Window keyboard did not supply an idle native input source");
    need(native->state(1,state)==ERROR_DEVICE_NOT_CONNECTED,"Keyboard escaped slot zero");
    need(native->capabilities(0,1,caps)==ERROR_SUCCESS&&caps.Gamepad.wButtons==Platform::NativeKeyboard::buttons&&
         caps.Gamepad.bRightTrigger==255&&caps.Gamepad.bLeftTrigger==255&&
         caps.Gamepad.sThumbLX==32767&&caps.Gamepad.sThumbLY==32767&&
         caps.Gamepad.sThumbRX==32767&&caps.Gamepad.sThumbRY==32767&&
         !caps.Flags&&!caps.Vibration.wLeftMotorSpeed&&!caps.Vibration.wRightMotorSpeed,"Keyboard capabilities differ from supported controls");
    keys->key(VK_RETURN,true);need(native->state(0,state)==0&&!state.Gamepad.wButtons,"Unfocused keyboard accepted a key");
    keys->focus(true);keys->key(VK_RETURN,true);keys->key(VK_RETURN,false);
    need(native->state(0,state)==0&&state.Gamepad.wButtons==XINPUT_GAMEPAD_A,"Short Enter confirm press was lost or paused the game");
    const auto packet=state.dwPacketNumber;
    need(native->state(0,state)==0&&!state.Gamepad.wButtons&&state.dwPacketNumber!=packet,"Enter release did not reach the next poll");
    const std::array<std::pair<uint32_t,WORD>,15> mappings={{{VK_SPACE,XINPUT_GAMEPAD_A},{'J',XINPUT_GAMEPAD_X},{'K',XINPUT_GAMEPAD_B},{'E',XINPUT_GAMEPAD_Y},{VK_ESCAPE,XINPUT_GAMEPAD_START},
        {VK_BACK,XINPUT_GAMEPAD_BACK},{VK_UP,XINPUT_GAMEPAD_DPAD_UP},{VK_DOWN,XINPUT_GAMEPAD_DPAD_DOWN},
        {VK_LEFT,XINPUT_GAMEPAD_DPAD_LEFT},{VK_RIGHT,XINPUT_GAMEPAD_DPAD_RIGHT},{VK_TAB,XINPUT_GAMEPAD_BACK},
        {'Q',XINPUT_GAMEPAD_LEFT_SHOULDER},{'R',XINPUT_GAMEPAD_RIGHT_SHOULDER},
        {'F',XINPUT_GAMEPAD_LEFT_THUMB},{'G',XINPUT_GAMEPAD_RIGHT_THUMB}}};
    for(const auto& [key,button]:mappings){
        keys->key(key,true);need(native->state(0,state)==0&&state.Gamepad.wButtons==button,"Keyboard mapping changed");
        const auto heldPacket=state.dwPacketNumber;keys->key(key,true);
        need(native->state(0,state)==0&&state.Gamepad.wButtons==button&&state.dwPacketNumber==heldPacket,"Key repeat changed the held state");
        keys->key(key,false);need(native->state(0,state)==0&&!state.Gamepad.wButtons,"Key remained held after release");
    }
    need(!keys->key(VK_RSHIFT,true),"Right Shift unexpectedly activates a game control");
    keys->key(VK_LSHIFT,true);keys->key(VK_LSHIFT,false);
    need(native->state(0,state)==0&&state.Gamepad.bRightTrigger==255,"Short Left Shift trigger press was lost");
    need(native->state(0,state)==0&&!state.Gamepad.bRightTrigger,"Left Shift trigger release did not reach the next poll");
    keys->key(VK_LSHIFT,true);native->state(0,state);const auto triggerPacket=state.dwPacketNumber;
    keys->key(VK_LSHIFT,true);
    need(native->state(0,state)==0&&state.Gamepad.bRightTrigger==255&&state.dwPacketNumber==triggerPacket,"Left Shift repeat changed the held trigger");
    keys->key(VK_LSHIFT,false);
    need(native->state(0,state)==0&&!state.Gamepad.bRightTrigger&&state.dwPacketNumber!=triggerPacket,"Left Shift trigger stayed held after release");
    const std::array<std::tuple<uint32_t,SHORT,SHORT>,4> movement={{{'W',0,32767},{'S',0,-32768},{'A',-32768,0},{'D',32767,0}}};
    for(const auto& [key,x,y]:movement){
        keys->key(key,true);need(native->state(0,state)==0&&state.Gamepad.sThumbLX==x&&state.Gamepad.sThumbLY==y,"WASD movement mapping changed");
        const auto heldPacket=state.dwPacketNumber;keys->key(key,true);
        need(native->state(0,state)==0&&state.Gamepad.sThumbLX==x&&state.Gamepad.sThumbLY==y&&state.dwPacketNumber==heldPacket,"WASD key repeat changed movement state");
        keys->key(key,false);need(native->state(0,state)==0&&!state.Gamepad.sThumbLX&&!state.Gamepad.sThumbLY,"WASD movement remained held after release");
    }
    keys->key('W',true);keys->key('D',true);
    need(native->state(0,state)==0&&state.Gamepad.sThumbLX==32767&&state.Gamepad.sThumbLY==32767,"Diagonal WASD movement was not preserved");
    keys->key('S',true);need(native->state(0,state)==0&&state.Gamepad.sThumbLX==32767&&!state.Gamepad.sThumbLY,"Opposing vertical WASD keys did not cancel");
    keys->key('W',false);keys->key('S',false);keys->key('D',false);native->state(0,state);
    keys->key('W',true);keys->key('W',false);
    need(native->state(0,state)==0&&state.Gamepad.sThumbLY==32767,"Short WASD press was lost before the game poll");
    need(native->state(0,state)==0&&!state.Gamepad.sThumbLY,"Short WASD release did not reach the next poll");
    keys->key('W',true);
    need(native->state(0,state)==0&&state.Gamepad.sThumbLY==32767,"Forward movement did not start before keyboard double jump");
    keys->key(VK_SPACE,true);
    need(native->state(0,state)==0&&state.Gamepad.wButtons==XINPUT_GAMEPAD_A&&state.Gamepad.sThumbLY==32767,
        "First keyboard jump lost forward movement");
    keys->key(VK_SPACE,false);
    need(native->state(0,state)==0&&!state.Gamepad.wButtons&&state.Gamepad.sThumbLY==32767,
        "Keyboard jump release lost forward movement");
    keys->key(VK_SPACE,true);
    need(native->state(0,state)==0&&state.Gamepad.wButtons==XINPUT_GAMEPAD_A&&state.Gamepad.sThumbLY==32767,
        "Second keyboard jump lost forward movement");
    keys->key(VK_SPACE,false);keys->key('W',false);
    need(native->state(0,state)==0&&!state.Gamepad.wButtons&&!state.Gamepad.sThumbLY,
        "Keyboard double jump retained released forward movement");
    keys->key(VK_SPACE,true);keys->key(VK_LSHIFT,true);keys->focus(false);
    need(native->state(0,state)==0&&!state.Gamepad.wButtons&&!state.Gamepad.bRightTrigger&&
         !state.Gamepad.sThumbLX&&!state.Gamepad.sThumbLY,"Focus loss retained held or pending input");
    keys->focus(true);keys->key(VK_RETURN,true);keys->key(VK_RETURN,false);
    samples[0].status=0;samples[0].state={42,{XINPUT_GAMEPAD_Y,0,0,0,0,0,0}};
    need(native->state(0,state)==0&&state.dwPacketNumber==42&&state.Gamepad.wButtons==XINPUT_GAMEPAD_Y,"Keyboard replaced a connected physical controller");
    need(!native->usesKeyboardMouse(),"Connected slot zero did not select controller prompts");
    native->state(1,state);need(!native->usesKeyboardMouse(),"Disconnected secondary slot changed primary prompt source");
    samples[0].status=ERROR_DEVICE_NOT_CONNECTED;
    need(native->state(0,state)==0&&!state.Gamepad.wButtons,"Keyboard replayed stale input after physical controller removal");
    need(native->usesKeyboardMouse(),"Disconnected slot zero did not restore native prompts");
    keys.reset();need(native->state(0,state)==ERROR_DEVICE_NOT_CONNECTED,"Expired game window retained keyboard connection");
    need(!native->usesKeyboardMouse(),"Retired native input source retained native prompts");
}
void keyboardNavigationQueries(){
    samples={};auto native=source();auto keys=std::make_shared<Platform::NativeKeyboard>();
    native->attachKeyboard(keys);keys->focus(true);XINPUT_STATE state{};
    const auto poll=[&]{need(native->state(0,state)==0,"Navigation keyboard disconnected");return native->keyboardNavigation();};
    auto nav=poll();need(nav.active&&!nav.held&&!nav.pressed,"Idle keyboard navigation snapshot differs");
    keys->key('W',true);nav=poll();
    need(nav.pressed==XINPUT_GAMEPAD_DPAD_UP&&nav.held==XINPUT_GAMEPAD_DPAD_UP&&state.Gamepad.sThumbLY==32767,
        "Navigation snapshot lost W edge or altered gameplay movement");
    need(native->keyboardNavigation().pressed==nav.pressed,"Menu query consumed the navigation edge");
    keys->key('W',true);nav=poll();
    need(!nav.pressed&&nav.held==XINPUT_GAMEPAD_DPAD_UP&&state.Gamepad.sThumbLY==32767,"Key autorepeat retriggered menu edge");
    keys->key('W',false);keys->key('W',true);nav=poll();
    need(nav.pressed==XINPUT_GAMEPAD_DPAD_UP&&nav.held==XINPUT_GAMEPAD_DPAD_UP,"Fast release/repress between polls lost the menu edge");
    keys->key('S',true);nav=poll();need(!nav.pressed&&!nav.held&&!state.Gamepad.sThumbLY,"Opposing menu directions did not cancel");
    keys->key('W',false);nav=poll();need(nav.pressed==XINPUT_GAMEPAD_DPAD_DOWN&&nav.held==XINPUT_GAMEPAD_DPAD_DOWN,"Changed menu direction lost edge");
    keys->key('S',false);poll();keys->key('A',true);keys->key('A',false);nav=poll();
    need(nav.pressed==XINPUT_GAMEPAD_DPAD_LEFT&&nav.held==XINPUT_GAMEPAD_DPAD_LEFT,"Short menu tap was lost");
    nav=poll();need(!nav.pressed&&!nav.held,"Short menu tap remained held");
    keys->key('D',true);nav=poll();need(nav.pressed==XINPUT_GAMEPAD_DPAD_RIGHT,"Right menu tap was lost");
    keys->focus(false);nav=poll();need(!nav.held&&!nav.pressed,"Focus loss retained menu movement");
    keys->focus(true);keys->key('W',true);poll();native->beginMovie(123);
    need(!native->keyboardNavigation().active,"Movie ownership retained menu navigation");
    nav=poll();need(!nav.active,"Movie poll republished menu navigation");native->endMovie();
    const auto token=native->beginModal(0);need(!native->keyboardNavigation().active,"Modal ownership retained menu navigation");
    native->modalState(token,state);need(!native->keyboardNavigation().active,"Modal poll published menu navigation");native->endModal(token);
    keys->focus(false);poll();keys->focus(true);keys->key('W',true);poll();
    samples[0].status=0;samples[0].state={};nav=poll();need(!nav.active,"Physical controller retained keyboard menu navigation");
    samples[0].status=ERROR_DEVICE_NOT_CONNECTED;nav=poll();need(nav.active,"Keyboard menu navigation failed to resume after controller removal");
    CommandFixture input;native->attachCommands(std::make_shared<Platform::NativeCommandInput>(input.path));
    input.append("MOVE_D\n");nav=poll();need(!nav.active&&state.Gamepad.sThumbLX==32767,"Command stick movement was remapped as keyboard menu input");
    nav=poll();need(nav.active,"Keyboard menu input did not resume after command stick release");
    keys.reset();need(!native->keyboardNavigation().active,"Retired window retained menu navigation");
}
void mouseQueries(){
    samples={};auto native=source();auto keys=std::make_shared<Platform::NativeKeyboard>();native->attachKeyboard(keys);
    XINPUT_STATE state{};
    const auto poll=[&]()->const XINPUT_GAMEPAD& {need(native->state(0,state)==0,"Mouse fallback disconnected");return state.Gamepad;};
    keys->focus(true);keys->mouseButton(VK_LBUTTON,true);keys->mouseMotion(100,100);
    need(poll().wButtons==XINPUT_GAMEPAD_X&&!state.Gamepad.sThumbRX&&!state.Gamepad.sThumbRY,"Focused mouse click required a hidden capture hotkey or uncaptured motion moved the camera");
    keys->mouseButton(VK_LBUTTON,false);need(!poll().wButtons,"Uncaptured mouse release was lost");
    keys->captureMouse(true);
    need(!keys->mouseButton(VK_XBUTTON1,true),"Unmapped mouse button was accepted");
    keys->mouseButton(VK_LBUTTON,true);keys->mouseButton(VK_LBUTTON,false);
    need(poll().wButtons==XINPUT_GAMEPAD_X,"Short mouse attack was lost before polling");
    need(!poll().wButtons,"Short mouse attack remained held");
    keys->mouseButton(VK_LBUTTON,true);keys->key('J',true);poll();keys->key('J',false);
    need(poll().wButtons==XINPUT_GAMEPAD_X,"Releasing J released a held mouse attack");
    keys->key('J',true);poll();keys->mouseButton(VK_LBUTTON,false);
    need(poll().wButtons==XINPUT_GAMEPAD_X,"Releasing mouse attack released held J");
    keys->key('J',false);poll();
    keys->mouseButton(VK_RBUTTON,true);keys->key(VK_LSHIFT,true);poll();keys->key(VK_LSHIFT,false);
    need(poll().wButtons==XINPUT_GAMEPAD_B&&!state.Gamepad.bRightTrigger,"Right mouse did not retain special attack independently of Shift");
    keys->key(VK_LSHIFT,true);poll();keys->mouseButton(VK_RBUTTON,false);
    need(poll().bRightTrigger==255&&!state.Gamepad.wButtons,"Releasing special attack changed held Shift or retained B");
    keys->key(VK_LSHIFT,false);need(!poll().bRightTrigger,"Shift trigger remained held after release");
    keys->mouseButton(VK_RBUTTON,true);keys->key('K',true);poll();keys->key('K',false);
    need(poll().wButtons==XINPUT_GAMEPAD_B,"Releasing K canceled a held right-click special attack");
    keys->key('K',true);poll();keys->mouseButton(VK_RBUTTON,false);
    need(poll().wButtons==XINPUT_GAMEPAD_B,"Releasing right click canceled held K");
    keys->key('K',false);need(!poll().wButtons,"Special attack aliases did not release");
    keys->key(VK_ESCAPE,true);keys->key(VK_RETURN,true);poll();keys->key(VK_ESCAPE,false);
    need(poll().wButtons==XINPUT_GAMEPAD_A,"Escape release canceled Enter confirm or Enter still paused");
    keys->key(VK_SPACE,true);poll();keys->key(VK_RETURN,false);
    need(poll().wButtons==XINPUT_GAMEPAD_A,"Enter release canceled held Space confirm");
    keys->key(VK_RETURN,true);poll();keys->key(VK_SPACE,false);
    need(poll().wButtons==XINPUT_GAMEPAD_A,"Space release canceled held Enter confirm");
    keys->key(VK_RETURN,false);need(!poll().wButtons,"Confirm input did not release");
    keys->mouseButton(VK_RBUTTON,true);keys->mouseButton(VK_RBUTTON,false);
    need(native->connectionStatus(0)==ERROR_SUCCESS&&native->connectionStatus(0)==ERROR_SUCCESS,"Native connection probe lost the mouse source");
    need(poll().wButtons==XINPUT_GAMEPAD_B&&!state.Gamepad.bRightTrigger,"Connection probes drained special attack or mapped it to Homer Ball");
    need(!poll().wButtons,"Short special attack did not release");
    keys->mouseButton(VK_MBUTTON,true);keys->key('G',true);poll();keys->key('G',false);
    need(poll().wButtons==XINPUT_GAMEPAD_RIGHT_THUMB,"Releasing G released the held middle mouse button");
    keys->mouseButton(VK_MBUTTON,false);need(!poll().wButtons,"Middle mouse button remained held");
    keys->key(VK_TAB,true);keys->key(VK_BACK,true);poll();keys->key(VK_TAB,false);
    need(poll().wButtons==XINPUT_GAMEPAD_BACK,"Tab release canceled held Backspace");
    keys->key(VK_BACK,false);poll();
    keys->key(VK_LCONTROL,true);keys->key(VK_RCONTROL,true);poll();keys->key(VK_LCONTROL,false);
    need(poll().bLeftTrigger==255,"Left Ctrl release canceled held Right Ctrl");
    keys->key(VK_RCONTROL,false);need(!poll().bLeftTrigger,"Ctrl trigger did not release");
    keys->key(VK_CONTROL,true);keys->key(VK_CONTROL,false);
    need(poll().bLeftTrigger==255,"Short Ctrl press was lost");need(!poll().bLeftTrigger,"Short Ctrl release was lost");
    keys->mouseMotion(1,0);
    need(native->connectionStatus(0)==ERROR_SUCCESS&&native->connectionStatus(0)==ERROR_SUCCESS,"Connection probes lost the captured mouse source");
    need(poll().sThumbRX>XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE&&state.Gamepad.sThumbRX<32767&&!state.Gamepad.sThumbRY,
        "Gentle mouse motion remained inside the right-stick deadzone");
    const auto gentleX=state.Gamepad.sThumbRX;
    keys->mouseMotion(2,0);need(poll().sThumbRX>gentleX&&state.Gamepad.sThumbRX<32767,"Mouse response lost proportional magnitude");
    keys->mouseMotion(2,-3);keys->mouseMotion(1,1);
    poll();
    const auto directionError=int32_t(state.Gamepad.sThumbRX)*2-int32_t(state.Gamepad.sThumbRY)*3;
    need(state.Gamepad.sThumbRX>0&&state.Gamepad.sThumbRY>0&&directionError>=-3&&directionError<=3,
        "Relative mouse motion lost accumulated direction or Y inversion");
    const auto motionPacket=state.dwPacketNumber;
    need(!poll().sThumbRX&&!state.Gamepad.sThumbRY&&state.dwPacketNumber!=motionPacket,"Consumed mouse motion did not return to neutral with a new packet");
    const auto neutralPacket=state.dwPacketNumber;
    poll();need(state.dwPacketNumber==neutralPacket,"Idle mouse changed the packet number");
    keys->mouseMotion(INT32_MAX,0);
    need(poll().sThumbRX==32767&&!state.Gamepad.sThumbRY,"Positive mouse saturation overflowed");
    keys->mouseMotion(INT32_MIN,0);
    need(poll().sThumbRX==-32768&&!state.Gamepad.sThumbRY,"Negative mouse saturation overflowed");
    keys->mouseMotion(0,INT32_MIN);
    need(!poll().sThumbRX&&state.Gamepad.sThumbRY==32767,"Upward mouse saturation overflowed");
    keys->mouseMotion(0,INT32_MAX);
    need(!poll().sThumbRX&&state.Gamepad.sThumbRY==-32768,"Downward mouse saturation overflowed");
    keys->mouseMotion(INT32_MAX,INT32_MIN);poll();
    const auto diagonalSquared=int64_t(state.Gamepad.sThumbRX)*state.Gamepad.sThumbRX+int64_t(state.Gamepad.sThumbRY)*state.Gamepad.sThumbRY;
    need(state.Gamepad.sThumbRX>0&&state.Gamepad.sThumbRY>0&&diagonalSquared<=int64_t(32768)*32768,
        "Diagonal mouse response exceeded the stick's radial range");
    keys->mouseButton(VK_LBUTTON,true);keys->mouseButton(VK_RBUTTON,true);keys->mouseMotion(4,5);
    keys->captureMouse(false);
    need(!poll().wButtons&&!state.Gamepad.bRightTrigger&&!state.Gamepad.sThumbRX&&!state.Gamepad.sThumbRY,"Capture release retained mouse input");
    keys->captureMouse(true);keys->mouseButton(VK_MBUTTON,true);keys->mouseMotion(8,9);keys->key(VK_LCONTROL,true);
    keys->focus(false);
    need(!poll().wButtons&&!state.Gamepad.bLeftTrigger&&!state.Gamepad.sThumbRX&&!state.Gamepad.sThumbRY,"Focus loss retained mouse or Ctrl input");
    keys->focus(true);keys->mouseMotion(3,4);keys->mouseButton(VK_LBUTTON,true);
    need(poll().wButtons==XINPUT_GAMEPAD_X&&!state.Gamepad.sThumbRX&&!state.Gamepad.sThumbRY,"Focus return blocked mouse buttons or silently restored camera capture");
    keys->mouseButton(VK_LBUTTON,false);poll();
    keys->captureMouse(true);keys->mouseButton(VK_LBUTTON,true);keys->mouseButton(VK_LBUTTON,false);keys->mouseMotion(7,8);
    samples[0].status=ERROR_SUCCESS;samples[0].state={90,{XINPUT_GAMEPAD_A,0,0,0,0,123,456}};
    need(poll().wButtons==XINPUT_GAMEPAD_A&&state.Gamepad.sThumbRX==123&&state.Gamepad.sThumbRY==456&&!native->usesKeyboardMouse(),"Mouse replaced physical controller state or prompts");
    samples[0].status=ERROR_DEVICE_NOT_CONNECTED;
    need(!poll().wButtons&&!state.Gamepad.sThumbRX&&!state.Gamepad.sThumbRY&&native->usesKeyboardMouse(),"Stale mouse input replayed after controller removal");
}
void menuPointerQueries(){
    samples={};auto native=source();auto keys=std::make_shared<Platform::NativeKeyboard>();native->attachKeyboard(keys);
    XINPUT_STATE state{};keys->focus(true);keys->menuMode(true);
    keys->pointerMove(640,360,1280,720);keys->mouseButton(VK_LBUTTON,true);keys->mouseButton(VK_LBUTTON,false);
    native->state(0,state);
    need(!state.Gamepad.wButtons&&!state.Gamepad.sThumbRX&&!state.Gamepad.sThumbRY,"Menu click leaked an attack or camera input");
    auto pointer=native->menuPointer();
    need(pointer.active&&pointer.moved&&pointer.pressed==1&&pointer.x==640&&pointer.y==360&&pointer.width==1280&&pointer.height==720,
        "Short menu click lost its client coordinates before the UI consumed it");
    pointer=native->menuPointer();need(pointer.active&&!pointer.moved&&!pointer.pressed,"Menu pointer replayed consumed movement or click");
    keys->mouseButton(VK_LBUTTON,true);keys->mouseButton(VK_LBUTTON,false);keys->pointerMove(900,500,1280,720);
    pointer=native->menuPointer();need(pointer.pressed==1&&pointer.x==640&&pointer.y==360,"Movement after a click retargeted the press");
    pointer=native->menuPointer();need(pointer.moved&&pointer.x==900&&pointer.y==500&&!pointer.pressed,"Post-click hover movement was lost");
    keys->mouseButton(VK_LBUTTON,true);pointer=native->menuPointer();need(pointer.pressed==1,"Fresh menu press missing");
    keys->mouseButton(VK_LBUTTON,true);need(!native->menuPointer().pressed,"Held menu press repeated");
    keys->mouseButton(VK_LBUTTON,false);keys->mouseButton(VK_RBUTTON,true);keys->mouseButton(VK_RBUTTON,false);
    keys->pointerWheel(120);keys->pointerWheel(120);pointer=native->menuPointer();
    need(pointer.pressed==2&&pointer.wheel==240,"Menu back or accumulated wheel input missing");
    need(!native->menuPointer().wheel,"Menu wheel was consumed twice");
    samples[0].status=ERROR_SUCCESS;keys->mouseButton(VK_LBUTTON,true);keys->mouseButton(VK_LBUTTON,false);native->state(0,state);
    need(native->menuPointer().pressed==1,"Connected controller discarded independent menu pointer click");
    samples[0].status=ERROR_DEVICE_NOT_CONNECTED;
    keys->pointerMove(-1,360,1280,720);need(!native->menuPointer().active,"Pointer outside the client activated a menu");
    keys->pointerMove(1280,720,1280,720);need(!native->menuPointer().active,"Exclusive client boundary activated a menu");
    keys->pointerMove(100,80,1920,1080);keys->mouseButton(VK_LBUTTON,true);keys->focus(false);
    need(!native->menuPointer().active,"Focus loss retained a clickable menu pointer");
    keys->focus(true);need(!native->menuPointer().active,"Focus return reused a stale pointer position");
    keys->pointerMove(100,80,1920,1080);native->beginMovie(0x50000);
    keys->mouseButton(VK_LBUTTON,true);keys->mouseButton(VK_LBUTTON,false);need(!native->menuPointer().active,"Movie exposed menu mouse input");
    native->endMovie();need(!native->menuPointer().pressed,"Movie click leaked into the following menu");
    native->beginMovie(0x50000);keys->mouseButton(VK_LBUTTON,true);keys->mouseButton(VK_LBUTTON,false);native->endMovie();
    need(!native->menuPointer().pressed,"Unpolled movie click leaked into the next menu");
    const auto token=native->beginModal(0);keys->mouseButton(VK_LBUTTON,true);keys->mouseButton(VK_LBUTTON,false);
    need(!native->menuPointer().active,"Exclusive native modal exposed Apt pointer input");native->endModal(token);native->state(0,state);
    keys->menuMode(false);keys->mouseButton(VK_LBUTTON,true);native->state(0,state);
    need(state.Gamepad.wButtons==XINPUT_GAMEPAD_X,"Leaving a menu did not restore gameplay mouse actions");
    need(!native->menuPointer().active,"Gameplay exposed menu pointer input");keys->mouseButton(VK_LBUTTON,false);
    NativeWindow window;native->attachKeyboard(window.keyboard);const auto hwnd=window.handle();
    EnableWindow(hwnd,FALSE);SendMessageW(hwnd,WM_SETFOCUS,0,0);window.setMenuMouse(true);
    SendMessageW(hwnd,WM_MOUSEMOVE,0,MAKELPARAM(500,220));
    SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(500,220));SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(500,220));
    native->state(0,state);pointer=native->menuPointer();
    need(window.isMenuMouse()&&pointer.active&&pointer.pressed==1&&pointer.x==500&&pointer.y==220&&!state.Gamepad.wButtons,
        "Owned window did not route absolute mouse click to the menu");
    GUITHREADINFO gui{};gui.cbSize=sizeof(gui);
    need(GetGUIThreadInfo(GetWindowThreadProcessId(hwnd,nullptr),&gui)&&gui.hwndCapture!=hwnd,"Menu click captured or hid the desktop cursor");
    SendMessageW(hwnd,WM_MOUSELEAVE,0,0);need(!native->menuPointer().active,"Window mouse leave retained a hit target");
    window.setMenuMouse(false);
}
void windowMouseQueries(){
    samples={};auto native=source();NativeWindow window;native->attachKeyboard(window.keyboard);
    const auto hwnd=window.handle();XINPUT_STATE state{};
    const auto poll=[&]()->const XINPUT_GAMEPAD& {need(native->state(0,state)==0,"Window mouse fallback disconnected");return state.Gamepad;};
    SetForegroundWindow(hwnd);
    // Exercise the actual window procedure even if desktop focus cannot be
    // acquired in an unattended run. The capture check below requires real focus.
    SendMessageW(hwnd,WM_SETFOCUS,0,0);
    SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(20,20));
    need(poll().wButtons==XINPUT_GAMEPAD_X,"The first client click was swallowed instead of attacking");
    GUITHREADINFO gui{};gui.cbSize=sizeof(gui);
    need(GetGUIThreadInfo(GetWindowThreadProcessId(hwnd,nullptr),&gui)!=FALSE,"Cannot inspect test window capture");
    if(GetForegroundWindow()==hwnd&&gui.hwndFocus==hwnd)
        need(gui.hwndCapture==hwnd,"Focused client click did not capture the mouse automatically");
    else std::printf("Window click delivery tested; real cursor capture requires available desktop foreground focus\n");
    SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(20,20));need(!poll().wButtons,"Window left-click release was lost");
    SendMessageW(hwnd,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(20,20));
    need(poll().wButtons==XINPUT_GAMEPAD_B&&!state.Gamepad.bRightTrigger,"Window right click did not deliver special attack/back");
    SendMessageW(hwnd,WM_RBUTTONUP,0,MAKELPARAM(20,20));need(!poll().wButtons,"Window special attack did not release");
    SendMessageW(hwnd,WM_KEYDOWN,VK_ESCAPE,0);
    need(poll().wButtons==XINPUT_GAMEPAD_START,"Escape activated special attack instead of pause");
    need(GetGUIThreadInfo(GetWindowThreadProcessId(hwnd,nullptr),&gui)!=FALSE&&gui.hwndCapture!=hwnd,"Escape did not release mouse capture");
    SendMessageW(hwnd,WM_KEYUP,VK_ESCAPE,0);need(!poll().wButtons,"Escape pause did not release");
    SendMessageW(hwnd,WM_KEYDOWN,VK_RETURN,0);
    need(poll().wButtons==XINPUT_GAMEPAD_A,"Window Enter paused instead of confirming");
    SendMessageW(hwnd,WM_KEYUP,VK_RETURN,0);need(!poll().wButtons,"Window Enter confirm did not release");
    SendMessageW(hwnd,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(20,20));poll();
    SendMessageW(hwnd,WM_KILLFOCUS,0,0);
    need(!poll().wButtons&&!state.Gamepad.sThumbRX&&!state.Gamepad.sThumbRY,"Window focus loss retained mouse input");
    need(GetGUIThreadInfo(GetWindowThreadProcessId(hwnd,nullptr),&gui)!=FALSE&&gui.hwndCapture!=hwnd,"Window focus loss retained mouse capture");
}
void earlyControllerWindowQueries(){
    samples={};Runtime rt;rt.map(0x50000,0x1000,true,"bounded early-window video-mode output");
    // Reproduce a native connection/storage query which precedes video setup.
    auto early=rt.controllerSource();
    Platform::NativeControllersTestAccess::bind(*early,stateQuery,capsQuery,setQuery);
    need(!rt.window&&!early->usesKeyboardMouse()&&early->connectionStatus(0)==ERROR_DEVICE_NOT_CONNECTED,
         "Early controller source unexpectedly had a game-window input source");
    PPCContext context{};context.r3.u32=0x50100;
    __imp__XGetVideoMode(context,rt.base);
    need(rt.window&&rt.window->handle()&&rt.controllerSource()==early&&early->usesKeyboardMouse(),
         "Video setup did not attach native input to the existing controller source");
    auto* firstWindow=rt.window.get();const auto keyboard=firstWindow->keyboard;
    keyboard->focus(true);keyboard->key('W',true);keyboard->mouseButton(VK_RBUTTON,true);
    XINPUT_STATE state{};
    need(early->state(0,state)==ERROR_SUCCESS&&state.Gamepad.wButtons==XINPUT_GAMEPAD_B&&state.Gamepad.sThumbLY==32767,
         "Controller source created before the window lost native mouse/keyboard input");
    __imp__XGetVideoMode(context,rt.base);
    need(rt.window.get()==firstWindow&&rt.window->keyboard==keyboard&&rt.controllerSource()==early&&
         early->state(0,state)==ERROR_SUCCESS&&state.Gamepad.wButtons==XINPUT_GAMEPAD_B&&state.Gamepad.sThumbLY==32767,
         "Repeated video-mode query recreated input or released held controls");
    keyboard->focus(false);
    samples[0].status=ERROR_SUCCESS;samples[0].state={77,{XINPUT_GAMEPAD_A,0,0,0,0,0,0}};
    early->state(0,state);need(!early->usesKeyboardMouse(),"Physical controller did not take priority after late keyboard attachment");
    __imp__XGetVideoMode(context,rt.base);
    need(!early->usesKeyboardMouse(),"Repeated video-mode query reset the selected physical prompt source");
}
void commandQueries(){
    CommandFixture input;input.append("START\n");samples={};auto native=source();
    auto commands=std::make_shared<Platform::NativeCommandInput>(input.path);native->attachCommands(commands);
    XINPUT_STATE state{};XINPUT_CAPABILITIES caps{};
    const auto poll=[&]{need(native->state(0,state)==0,"Command channel disconnected");return state.Gamepad.wButtons;};
    need(!poll(),"Old commands replayed at startup");
    need(native->state(1,state)==ERROR_DEVICE_NOT_CONNECTED,"Commands escaped slot zero");
    need(native->capabilities(0,1,caps)==0&&
        caps.Gamepad.wButtons==(Platform::NativeKeyboard::buttons|XINPUT_GAMEPAD_X|XINPUT_GAMEPAD_Y)&&
        caps.Gamepad.bRightTrigger==255&&!caps.Gamepad.bLeftTrigger&&
        !caps.Flags&&!caps.Vibration.wLeftMotorSpeed,"Commands did not advertise the supported actions");
    input.append("STA");need(!poll(),"Partial command pressed a button");input.append("RT\r\nSTART\nA\n");
    need(poll()==XINPUT_GAMEPAD_START,"Completed Start command missing");const auto packet=state.dwPacketNumber;
    need(!poll()&&state.dwPacketNumber!=packet,"Command release/packet missing");
    need(poll()==XINPUT_GAMEPAD_START&&!poll()&&poll()==XINPUT_GAMEPAD_A&&!poll(),"Repeated taps coalesced or reordered");
    input.append("invalid\n"+std::string(80,'A')+"\nB\n");
    need(!poll()&&poll()==XINPUT_GAMEPAD_B&&!poll(),"Malformed commands pressed a button or blocked later input");
    auto keys=std::make_shared<Platform::NativeKeyboard>();native->attachKeyboard(keys);keys->focus(true);keys->key(VK_UP,true);keys->key('W',true);
    input.append("START\n");need(poll()==(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_DPAD_UP)&&state.Gamepad.sThumbLY==32767,"Commands lost simultaneous keyboard state");
    need(poll()==XINPUT_GAMEPAD_DPAD_UP,"Command release released a held keyboard button");keys->focus(false);need(!poll(),"Keyboard focus release failed beside command input");
    input.append("START\nSTA");samples[0].status=0;samples[0].state={42,{XINPUT_GAMEPAD_Y,0,0,0,0,0,0}};
    need(poll()==XINPUT_GAMEPAD_Y&&state.dwPacketNumber==42,"Commands replaced physical controller state");
    samples[0].status=ERROR_DEVICE_NOT_CONNECTED;need(!poll(),"Commands replayed after physical controller removal");
    input.append("A\nB\nBACK\nUP\nDOWN\nLEFT\nRIGHT\n");
    for(WORD bit:{WORD(XINPUT_GAMEPAD_A),WORD(XINPUT_GAMEPAD_B),WORD(XINPUT_GAMEPAD_BACK),WORD(XINPUT_GAMEPAD_DPAD_UP),WORD(XINPUT_GAMEPAD_DPAD_DOWN),WORD(XINPUT_GAMEPAD_DPAD_LEFT),WORD(XINPUT_GAMEPAD_DPAD_RIGHT)})
        need(poll()==bit&&!poll(),"Command mapping or release changed");
    input.append("MOVE_W\nMOVE_S\nMOVE_A\nMOVE_D\n");
    for(const auto& [x,y]:std::array<std::pair<SHORT,SHORT>,4>{{{0,32767},{0,-32768},{-32768,0},{32767,0}}}) {
        need(!poll()&&state.Gamepad.sThumbLX==x&&state.Gamepad.sThumbLY==y,"Command movement mapping changed");
        need(!poll()&&!state.Gamepad.sThumbLX&&!state.Gamepad.sThumbLY,"Command movement release changed");
    }
    Platform::NativeCommandInputTestAccess::bind(*commands);
    input.append("START_HOLD\nA\n");
    need(poll()==XINPUT_GAMEPAD_START && poll()==XINPUT_GAMEPAD_START,"Held Start did not survive consecutive polls");
    Platform::NativeCommandInputTestAccess::now=1249;
    need(poll()==XINPUT_GAMEPAD_START,"Held Start released before its requested duration");
    Platform::NativeCommandInputTestAccess::now=1250;
    need(!poll() && poll()==XINPUT_GAMEPAD_A && !poll(),"Held command expiry lost the release or next queued command");
    input.append("START_HOLD\n");need(poll()==XINPUT_GAMEPAD_START,"Second held Start was lost");
    commands->discard();need(!poll(),"Discard did not release a held command");
    input.append("MOVE_W_HOLD\n");need(!poll()&&state.Gamepad.sThumbLY==32767,"Held movement command was lost");
    Platform::NativeCommandInputTestAccess::now=1499;
    need(!poll()&&state.Gamepad.sThumbLY==32767,"Held movement released before its requested duration");
    Platform::NativeCommandInputTestAccess::now=1500;
    need(!poll()&&!state.Gamepad.sThumbLX&&!state.Gamepad.sThumbLY,"Held movement command did not release");
}
void padQueries(){
    CommandFixture input;samples={};auto native=source();
    auto commands=std::make_shared<Platform::NativeCommandInput>(input.path);native->attachCommands(commands);
    Platform::NativeCommandInputTestAccess::bind(*commands);
    Platform::NativeCommandInputTestAccess::now=1000;
    XINPUT_STATE state{};
    const auto poll=[&]{need(native->state(0,state)==ERROR_SUCCESS,"PAD channel disconnected");return state.Gamepad;};
    input.append("PAD 1000 32767 ");
    need(!poll().wButtons,"Partial PAD action was consumed");
    input.append("-32768 500\nB\n");
    auto pad=poll();
    need(pad.wButtons==XINPUT_GAMEPAD_A&&pad.sThumbLX==32767&&pad.sThumbLY==-32768&&!pad.bRightTrigger,
        "Simultaneous PAD movement and jump were not delivered");
    const auto heldPacket=state.dwPacketNumber;
    Platform::NativeCommandInputTestAccess::now=1499;pad=poll();
    need(pad.wButtons==XINPUT_GAMEPAD_A&&pad.sThumbLX==32767&&pad.sThumbLY==-32768&&
        state.dwPacketNumber==heldPacket,"PAD action changed before its duration expired");
    Platform::NativeCommandInputTestAccess::now=1500;pad=poll();
    need(!pad.wButtons&&!pad.sThumbLX&&!pad.sThumbLY&&state.dwPacketNumber!=heldPacket,
        "PAD action did not release at its duration boundary");
    need(poll().wButtons==XINPUT_GAMEPAD_B&&!poll().wButtons,"PAD release did not separate the next command");

    Platform::NativeCommandInputTestAccess::now=2000;
    input.append("PAD 3000 -32768 32767 2000\n");pad=poll();
    need(pad.wButtons==(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B)&&pad.sThumbLX==-32768&&pad.sThumbLY==32767,
        "PAD button combination or signed stick endpoint changed");
    Platform::NativeCommandInputTestAccess::now=3999;
    need(poll().wButtons==(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B),"Maximum PAD duration ended early");
    Platform::NativeCommandInputTestAccess::now=4000;
    pad=poll();need(!pad.wButtons&&!pad.sThumbLX&&!pad.sThumbLY&&!pad.bRightTrigger,"Maximum PAD duration did not release");

    Platform::NativeCommandInputTestAccess::now=5000;
    input.append("PAD 0000 0 0 50\nA\n");
    need(!poll().wButtons,"Neutral PAD wait produced a button");
    Platform::NativeCommandInputTestAccess::now=5049;
    need(!poll().wButtons,"Action after neutral PAD wait arrived early");
    Platform::NativeCommandInputTestAccess::now=5050;
    need(!poll().wButtons&&poll().wButtons==XINPUT_GAMEPAD_A&&!poll().wButtons,
        "Neutral PAD wait lost its release poll or following action");

    Platform::NativeCommandInputTestAccess::now=6000;
    input.append("PAD C000 0 32767 150 255\n");pad=poll();
    need(pad.wButtons==(XINPUT_GAMEPAD_X|XINPUT_GAMEPAD_Y)&&pad.sThumbLY==32767&&pad.bRightTrigger==255,
        "Attack, interact, and full right trigger were not delivered together");
    const auto triggerPacket=state.dwPacketNumber;
    Platform::NativeCommandInputTestAccess::now=6149;pad=poll();
    need(pad.bRightTrigger==255&&state.dwPacketNumber==triggerPacket,"Right trigger did not hold for the PAD duration");
    Platform::NativeCommandInputTestAccess::now=6150;pad=poll();
    need(!pad.wButtons&&!pad.bRightTrigger&&state.dwPacketNumber!=triggerPacket,
        "Right trigger and new buttons did not release at the PAD boundary");
    Platform::NativeCommandInputTestAccess::now=7000;
    input.append("PAD 0000 0 0 50 1\n");pad=poll();
    need(!pad.wButtons&&pad.bRightTrigger==1,"Small right trigger magnitude was not preserved");
    Platform::NativeCommandInputTestAccess::now=7050;
    need(!poll().bRightTrigger,"Right trigger-only PAD command did not release");

    // The observed house-exit route includes a 123 ms directional press.
    // Preserve each DPad bit alongside face/stick input until that boundary.
    const std::array<std::pair<const char*,WORD>,4> directions={{{"0001",XINPUT_GAMEPAD_DPAD_UP},
        {"0002",XINPUT_GAMEPAD_DPAD_DOWN},{"0004",XINPUT_GAMEPAD_DPAD_LEFT},{"0008",XINPUT_GAMEPAD_DPAD_RIGHT}}};
    uint64_t directionStart=8000;
    for(const auto& [hex,bit]:directions) {
        Platform::NativeCommandInputTestAccess::now=directionStart;
        input.append(std::string("PAD ")+hex+" 0 32767 123\nB\n");pad=poll();
        need(pad.wButtons==bit&&pad.sThumbLY==32767,"Timed PAD direction was not delivered");
        const auto directionPacket=state.dwPacketNumber;
        Platform::NativeCommandInputTestAccess::now=directionStart+122;pad=poll();
        need(pad.wButtons==bit&&pad.sThumbLY==32767&&state.dwPacketNumber==directionPacket,
            "Timed PAD direction released before 123 ms");
        Platform::NativeCommandInputTestAccess::now=directionStart+123;pad=poll();
        need(!pad.wButtons&&!pad.sThumbLY&&state.dwPacketNumber!=directionPacket,
            "Timed PAD direction did not release at 123 ms");
        need(poll().wButtons==XINPUT_GAMEPAD_B&&!poll().wButtons,
            "Timed PAD direction lost the release poll or queued face action");
        directionStart+=200;
    }
    input.append("PAD 1009 0 0 50\n");pad=poll();
    need(pad.wButtons==(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_RIGHT),
        "Timed PAD direction could not combine with a face button");
    Platform::NativeCommandInputTestAccess::now+=50;
    need(!poll().wButtons,"Combined PAD face and direction did not release");

    for(const WORD bit:{WORD(XINPUT_GAMEPAD_START),WORD(XINPUT_GAMEPAD_BACK),WORD(XINPUT_GAMEPAD_LEFT_THUMB),
        WORD(XINPUT_GAMEPAD_RIGHT_THUMB),WORD(XINPUT_GAMEPAD_LEFT_SHOULDER),WORD(XINPUT_GAMEPAD_RIGHT_SHOULDER)}) {
        char command[64];std::snprintf(command,sizeof(command),"PAD %04X 0 0 50\n",unsigned(bit));
        input.append(command);pad=poll();need(pad.wButtons==bit,"Original controller bit was rejected by the timed command whitelist");
        Platform::NativeCommandInputTestAccess::now+=49;need(poll().wButtons==bit,"Timed original controller bit released early");
        ++Platform::NativeCommandInputTestAccess::now;need(!poll().wButtons,"Timed original controller bit did not release");
    }
    for(const auto malformed:{"PAD 1000 0 0 49", "PAD 1000 0 0 2001", "PAD 1000 32768 0 50",
        "PAD 1000 0 -32769 50", "PAD 0800 0 0 50", "PAD 0400 0 0 50",
        "PAD 100 0 0 50", "PAD -000 0 0 50", "PAD 1000  0 0 50",
        "PAD 1000 0 0 50 ", "PAD 1000 0 0 50 junk", "PAD 1000 +1 0 50",
        "PAD 1000 0 0 50_HOLD", "PAD 1000 0 0", "PAD 1000 0 0 999999999999999999999",
        "PAD 1000 0 0 50 -1", "PAD 1000 0 0 50 256", "PAD 1000 0 0 50 1 2",
        "PAD 1000 0 0 50 1 "}) {
        input.append(std::string(malformed)+"\n");pad=poll();
        need(!pad.wButtons&&!pad.sThumbLX&&!pad.sThumbLY&&!pad.bRightTrigger,
            "Malformed PAD command changed controller state");
    }
    input.append("PAD 2000 0 32767 100\n");pad=poll();
    need(pad.wButtons==XINPUT_GAMEPAD_B&&pad.sThumbLY==32767,"Valid PAD action was lost after malformed input");
    commands->discard();pad=poll();
    need(!pad.wButtons&&!pad.sThumbLX&&!pad.sThumbLY&&!pad.bRightTrigger,"Discard retained a held PAD action");
}
void forwardDoubleJumpQueries(){
    CommandFixture input;samples={};auto native=source();
    auto commands=std::make_shared<Platform::NativeCommandInput>(input.path);native->attachCommands(commands);
    Platform::NativeCommandInputTestAccess::bind(*commands);
    Platform::NativeCommandInputTestAccess::now=1000;
    XINPUT_STATE state{};
    const auto poll=[&]{need(native->state(0,state)==ERROR_SUCCESS,"Double-jump PAD channel disconnected");return state.Gamepad;};
    input.append("PAD 1000 0 32767 100\nPAD 0000 0 32767 180\nPAD 1000 0 32767 100\n");
    auto pad=poll();
    need(pad.wButtons==XINPUT_GAMEPAD_A&&pad.sThumbLY==32767,"First PAD jump did not move forward");
    const auto firstPacket=state.dwPacketNumber;
    Platform::NativeCommandInputTestAccess::now=1100;pad=poll();
    need(!pad.wButtons&&pad.sThumbLY==32767&&state.dwPacketNumber!=firstPacket,
        "Release poll centered the stick or hid the first A-up edge");
    const auto releasePacket=state.dwPacketNumber;
    pad=poll();
    need(!pad.wButtons&&pad.sThumbLY==32767&&state.dwPacketNumber==releasePacket,
        "Explicit PAD jump gap lost forward movement or invented an input change");
    Platform::NativeCommandInputTestAccess::now=1280;pad=poll();
    need(!pad.wButtons&&pad.sThumbLY==32767,"Release poll centered the stick before second PAD jump");
    pad=poll();
    need(pad.wButtons==XINPUT_GAMEPAD_A&&pad.sThumbLY==32767&&state.dwPacketNumber!=releasePacket,
        "Second PAD jump or its forward movement was lost");
    Platform::NativeCommandInputTestAccess::now=1380;pad=poll();
    need(!pad.wButtons&&!pad.sThumbLY,"Final PAD hold did not release its stick and button");
}
void modalQueries(){
    CommandFixture input;samples={};auto native=source();
    native->attachCommands(std::make_shared<Platform::NativeCommandInput>(input.path));
    XINPUT_STATE state{};input.append("A\n");
    const auto token=native->beginModal(0);
    rejects([&]{native->beginModal(0);});rejects([&]{native->modalState(token+1,state);});
    need(native->modalState(token,state)==0&&!state.Gamepad.wButtons,"Pre-dialog command selected a UI choice");
    input.append("A\nB\n");
    for(int i=0;i<10;++i)need(native->state(0,state)==0&&!state.Gamepad.wButtons,"Game consumed modal UI input");
    need(native->state(1,state)==ERROR_DEVICE_NOT_CONNECTED,"Modal UI fabricated another controller");
    need(native->modalState(token,state)==0&&state.Gamepad.wButtons==XINPUT_GAMEPAD_A,"UI lost its actual queued choice");
    native->endModal(token);rejects([&]{native->endModal(token);});
    need(native->state(0,state)==0&&!state.Gamepad.wButtons,"Queued UI choice leaked after closing");
    input.append("START\n");need(native->state(0,state)==0&&state.Gamepad.wButtons==XINPUT_GAMEPAD_START,"Game input did not resume after native UI");
    need(native->state(0,state)==0&&!state.Gamepad.wButtons,"Resumed command release failed");
    samples[0].status=0;samples[0].state.Gamepad.wButtons=XINPUT_GAMEPAD_A;
    const auto second=native->beginModal(0);
    need(native->modalState(second,state)==0&&!state.Gamepad.wButtons,"Held physical A accepted a dialog on opening");
    samples[0].state.Gamepad.wButtons=0;need(native->modalState(second,state)==0&&!state.Gamepad.wButtons,"Physical release was not neutral");
    samples[0].state.Gamepad.wButtons=XINPUT_GAMEPAD_A;
    need(native->modalState(second,state)==0&&state.Gamepad.wButtons==XINPUT_GAMEPAD_A,"Fresh physical UI choice was lost");
    native->endModal(second);
    need(native->state(0,state)==0&&!state.Gamepad.wButtons,"Held physical UI choice leaked into the game");
    samples[0].state.Gamepad.wButtons=0;native->state(0,state);
    samples[0].state.Gamepad.wButtons=XINPUT_GAMEPAD_B;
    need(native->state(0,state)==0&&state.Gamepad.wButtons==XINPUT_GAMEPAD_B,"Physical game input failed to resume");
}
void recordingQueries(){
    using Recording=Platform::NativeInputRecording;
    CommandFixture fixture;
    const auto directory=std::filesystem::path(fixture.path.wstring()+L".recordings");
    auto recording=std::make_shared<Recording>(directory);
    samples={};auto native=source();native->attachRecording(recording);
    NativeWindow window(recording);native->attachKeyboard(window.keyboard);
    const auto hwnd=window.handle();XINPUT_STATE state{};
    window.keyboard->focus(true);
    native->state(0,state);
    need(recording->status()==Recording::Status::Ready&&!std::filesystem::exists(directory),"Recording started before F8");
    SendMessageW(hwnd,WM_KEYDOWN,VK_F8,0);
    const auto firstPath=recording->path();
    need(recording->status()==Recording::Status::Recording&&std::filesystem::is_regular_file(firstPath),"F8 did not start recording");
    wchar_t title[256]{};GetWindowTextW(hwnd,title,256);
    need(std::wstring(title).find(L"REC | F9 finish inputs | F8 stop")!=std::wstring::npos,"Recording indicator missing from game title");
    need(std::wstring(title).find(L"Click to use mouse")!=std::wstring::npos,"Mouse capture hint missing from game title");
    SendMessageW(hwnd,WM_KEYDOWN,VK_F8,LPARAM(1)<<30);
    need(recording->status()==Recording::Status::Recording,"F8 repeat stopped recording");
    SendMessageW(hwnd,WM_KEYUP,VK_F8,LPARAM(1)<<30);
    // A real WM_KILLFOCUS (desktop use, or another test's window) may have
    // cleared keyboard focus since setup; this check is about key mapping.
    window.keyboard->focus(true);
    SendMessageW(hwnd,WM_KEYDOWN,VK_SHIFT,LPARAM(0x2A)<<16);
    native->state(0,state);
    need(state.Gamepad.bRightTrigger==255,"Left Shift window key did not reach the recorded controller poll");
    SendMessageW(hwnd,WM_KEYUP,VK_SHIFT,LPARAM(0x2A)<<16);
    native->state(0,state);
    need(!state.Gamepad.bRightTrigger,"Left Shift window key release was lost");
    SendMessageW(hwnd,WM_KEYDOWN,VK_SHIFT,LPARAM(0x36)<<16);
    native->state(0,state);
    need(!state.Gamepad.bRightTrigger,"Right Shift window key activated Homer Ball");
    SendMessageW(hwnd,WM_KEYUP,VK_SHIFT,LPARAM(0x36)<<16);
    window.keyboard->focus(true);window.keyboard->key('W',true);window.keyboard->key(VK_SPACE,true);
    native->state(0,state);
    need(state.Gamepad.wButtons==XINPUT_GAMEPAD_A&&state.Gamepad.sThumbLY==32767,"Recorder changed keyboard input");
    window.keyboard->focus(false);native->state(0,state);
    need(!state.Gamepad.wButtons&&!state.Gamepad.sThumbLY,"Recording lost focus release");
    samples[0].status=ERROR_SUCCESS;samples[0].state={99,{XINPUT_GAMEPAD_Y,7,255,-32768,32767,-123,456}};
    native->state(0,state);need(state.dwPacketNumber==99&&state.Gamepad.bRightTrigger==255,"Recorder changed physical controller input");
    samples[3].state=samples[0].state;native->state(3,state);
    samples[0].state.Gamepad={};const auto token=native->beginModal(0);
    samples[0].state.Gamepad.wButtons=XINPUT_GAMEPAD_B;
    native->state(0,state);need(!state.Gamepad.wButtons,"Recorder leaked modal input into gameplay");
    native->modalState(token,state);need(state.Gamepad.wButtons==XINPUT_GAMEPAD_B,"Recorder lost native dialog input");
    samples[0].state.Gamepad={};native->endModal(token);native->state(0,state);
    native->beginMovie(123);native->state(0,state);
    samples[0].state.Gamepad.wButtons=XINPUT_GAMEPAD_START;native->state(0,state);
    need(!state.Gamepad.wButtons&&native->takeMovieSkip(123),"Recorder changed movie skip behavior");native->endMovie();
    const auto read=[&](const std::filesystem::path& path){std::ifstream stream(path);return std::string(std::istreambuf_iterator<char>(stream),{});};
    auto contents=read(firstPath);
    need(contents.find("\"type\":\"end\"")==std::string::npos&&contents.find("\"seq\":8")!=std::string::npos,"Active recording was buffered or missing polls");
    need(contents.find("\"buttons\":4096,\"lt\":0,\"rt\":0,\"lx\":0,\"ly\":32767")!=std::string::npos,"Keyboard state absent from recording");
    need(contents.find("\"buttons\":32768,\"lt\":7,\"rt\":255,\"lx\":-32768,\"ly\":32767,\"rx\":-123,\"ry\":456")!=std::string::npos,"Raw physical values absent from recording");
    need(contents.find("\"slot\":3,\"status\":1167,\"packet\":0,\"buttons\":0")!=std::string::npos,"Disconnect recorded stale controller state");
    need(contents.find("\"consumer\":\"modal\"")!=std::string::npos,"Modal consumer missing");
    SendMessageW(hwnd,WM_KEYDOWN,VK_F8,0);
    need(recording->status()==Recording::Status::Saved,"F8 did not stop recording");
    GetWindowTextW(hwnd,title,256);need(std::wstring(title).find(L"Inputs saved | F8 record")!=std::wstring::npos,"Saved indicator missing from game title");
    contents=read(firstPath);need(contents.find("\"samples\":12")!=std::string::npos,"Stop did not finalize all polls");
    native->state(0,state);need(read(firstPath)==contents,"Stopped recording kept receiving input");
    SendMessageW(hwnd,WM_KEYDOWN,VK_F8,0);
    const auto secondPath=recording->path();need(secondPath!=firstPath&&read(firstPath)==contents,"Restart overwrote previous recording");
    SendMessageW(hwnd,WM_KEYDOWN,VK_F9,0);
    for(uint32_t slot=0;slot<4;++slot)native->state(slot,state);
    need(recording->status()==Recording::Status::Saved&&
         read(secondPath).find("\"reason\":\"checkpoint\"")!=std::string::npos,"F9 did not finish the input recording");
    std::atomic<uint64_t> scene{0};
    auto automatic=std::make_shared<Recording>(directory/L"automatic",&scene,true);
    native->attachRecording(automatic);
    native->state(1,state);
    need(automatic->status()==Recording::Status::Ready,"Automatic recording began before the first slot-zero game poll");
    native->state(0,state);
    need(automatic->status()==Recording::Status::Recording,"Automatic recording did not begin on the first slot-zero game poll");
    automatic->checkpoint();
    for(uint32_t slot=1;slot<4;++slot)native->state(slot,state);
    need(read(automatic->path()).find("\"start_scene\":0")!=std::string::npos,
         "Automatic recording lost its first-poll origin");
    // A real filesystem error is reported without affecting the controller.
    auto broken=std::make_shared<Recording>(fixture.path);native->attachRecording(broken);broken->toggle();
    need(broken->status()==Recording::Status::Error,"Recorder did not report invalid output directory");
    samples[0].state.Gamepad.wButtons=XINPUT_GAMEPAD_A;native->state(0,state);
    need(state.Gamepad.wButtons==XINPUT_GAMEPAD_A,"Recording failure disrupted gameplay input");
    need(directory.parent_path()==fixture.path.parent_path()&&directory.filename().wstring().starts_with(L"Simpsons-input-"),"Unexpected fixture cleanup path");
    std::filesystem::remove_all(directory);
}
void playbackQueries(){
    CommandFixture fixture;
    const auto path=std::filesystem::path(fixture.path.wstring()+L".jsonl");
    {
        std::ofstream stream(path);
        stream<<"{\"type\":\"header\",\"version\":1,\"boundary\":\"returned_controller_state\"}\n";
        for(unsigned seq=0;seq<8;++seq) {
            const auto slot=seq%4;
            stream<<"{\"type\":\"input\",\"seq\":"<<seq<<",\"consumer\":\"game\",\"slot\":"<<slot
                  <<",\"status\":"<<(slot?ERROR_DEVICE_NOT_CONNECTED:ERROR_SUCCESS)
                  <<",\"packet\":"<<(slot?0:(seq?2:1))
                  <<",\"buttons\":"<<(seq==4?XINPUT_GAMEPAD_A:0)
                  <<",\"lt\":0,\"rt\":0,\"lx\":0,\"ly\":"<<(seq==4?32767:0)
                  <<",\"rx\":0,\"ry\":0}\n";
        }
        stream<<"{\"type\":\"end\",\"samples\":8}\n";
    }
    std::atomic<uint64_t> scene{10};
    auto replay=std::make_shared<Platform::NativeInputPlayback>(path,11,scene);
    need(replay->count()==8,"Input playback did not load both complete four-slot cycles");
    samples={};auto native=source();native->attachPlayback(replay);XINPUT_STATE state{};
    need(native->state(0,state)==ERROR_DEVICE_NOT_CONNECTED,"Input playback activated before its scene gate");
    auto keys=std::make_shared<Platform::NativeKeyboard>();native->attachKeyboard(keys);keys->focus(true);
    need(native->state(0,state)==ERROR_SUCCESS&&native->usesKeyboardMouse(),
         "Native input did not select keyboard/mouse prompts before playback");
    keys->key('E',true);
    scene=11;
    need(native->state(1,state)==ERROR_DEVICE_NOT_CONNECTED,"Input playback activated mid-cycle");
    need(native->state(0,state)==ERROR_SUCCESS&&state.dwPacketNumber==1&&!state.Gamepad.wButtons,
         "Input playback first neutral state differs from recording");
    need(native->usesKeyboardMouse(),"Playback's controller-shaped record replaced native prompts");
    rejects([&]{native->state(2,state);}); // Poll-order divergence is explicit.
    for(unsigned slot=1;slot<4;++slot)need(native->state(slot,state)==ERROR_DEVICE_NOT_CONNECTED&&
        !state.dwPacketNumber&&!state.Gamepad.wButtons,"Input playback disconnected slot differs");
    need(native->state(0,state)==ERROR_SUCCESS&&state.dwPacketNumber==2&&
        state.Gamepad.wButtons==XINPUT_GAMEPAD_A&&state.Gamepad.sThumbLY==32767,
        "Input playback lost the exact packet/button/stick state");
    need(native->usesKeyboardMouse(),"Recorded gameplay input replaced keyboard/mouse prompts");
    for(unsigned slot=1;slot<4;++slot)need(native->state(slot,state)==ERROR_DEVICE_NOT_CONNECTED,
        "Input playback second cycle differs");
    need(native->state(0,state)==ERROR_SUCCESS&&!state.Gamepad.wButtons&&!state.Gamepad.sThumbLY,
         "Input playback did not release inputs after the recording ends");
    need(native->usesKeyboardMouse(),"Ended playback discarded the selected native prompt set");
    auto resume=std::make_shared<Platform::NativeInputPlayback>(path,0,scene,true,11);
    samples={};auto resumed=source();
    auto resumeKeys=std::make_shared<Platform::NativeKeyboard>();resumed->attachKeyboard(resumeKeys);
    need(resumed->state(0,state)==ERROR_SUCCESS&&resumed->usesKeyboardMouse(),
         "Native input did not select keyboard/mouse prompts before checkpoint replay");
    resumed->attachPlayback(resume);
    samples={};samples[0].status=ERROR_SUCCESS;samples[0].state.dwPacketNumber=77;
    samples[0].state.Gamepad.wButtons=XINPUT_GAMEPAD_Y;
    for(unsigned seq=0;seq<8;++seq) {
        const auto slot=seq%4;
        need(resumed->state(slot,state)==(slot?ERROR_DEVICE_NOT_CONNECTED:ERROR_SUCCESS),
             "Checkpoint playback did not consume its saved poll prefix");
        need(resumed->usesKeyboardMouse(),"Checkpoint playback changed the selected native prompt set");
        if(!slot)need(state.dwPacketNumber==(seq<4?1u:2u)&&
                     state.Gamepad.wButtons==(seq<4?0:XINPUT_GAMEPAD_A)&&
                     state.Gamepad.sThumbLY==(seq<4?0:32767),
                     "Keeping native prompts changed the checkpoint's recorded input");
    }
    need(!samples[0].reads,"Checkpoint playback sampled live input before its end");
    need(resumed->state(0,state)==ERROR_SUCCESS&&state.dwPacketNumber==77&&
         state.Gamepad.wButtons==XINPUT_GAMEPAD_Y&&samples[0].reads==1,
         "Checkpoint playback did not restore live input at the next slot-zero poll");
    need(!resumed->usesKeyboardMouse(),"Live physical controller handoff retained native prompts");
    auto physicalReplay=std::make_shared<Platform::NativeInputPlayback>(path,0,scene);
    resumed->attachPlayback(physicalReplay);
    need(resumed->state(0,state)==ERROR_SUCCESS&&state.dwPacketNumber==1&&!state.Gamepad.wButtons&&
         !resumed->usesKeyboardMouse(),"Playback did not preserve an already selected physical-controller prompt set");
    auto diverged=std::make_shared<Platform::NativeInputPlayback>(path,0,scene,true,11);
    auto rejected=source();rejected->attachPlayback(diverged);
    for(unsigned seq=0;seq<8;++seq)rejected->state(seq%4,state);
    scene=40;
    rejects([&]{rejected->state(0,state);});
    std::filesystem::remove(path);
}
void neutralReplayCatchup(){
    CommandFixture fixture;
    const auto path=std::filesystem::path(fixture.path.wstring()+L".neutral.jsonl");
    {
        std::ofstream stream(path);
        stream<<"{\"type\":\"header\",\"version\":1,\"boundary\":\"returned_controller_state\"}\n";
        for(unsigned seq=0;seq<8;++seq) {
            const auto slot=seq%4;
            stream<<"{\"type\":\"input\",\"seq\":"<<seq<<",\"consumer\":\"game\",\"slot\":"<<slot
                  <<",\"status\":"<<(slot?ERROR_DEVICE_NOT_CONNECTED:ERROR_SUCCESS)
                  <<",\"packet\":"<<(slot?0:1)
                  <<",\"buttons\":0,\"lt\":0,\"rt\":0,\"lx\":0,\"ly\":0,\"rx\":0,\"ry\":0}\n";
        }
        stream<<"{\"type\":\"end\",\"samples\":8}\n";
    }
    std::atomic<uint64_t> scene{366};
    auto replay=std::make_shared<Platform::NativeInputPlayback>(path,0,scene,true,460);
    samples={};samples[0].status=ERROR_SUCCESS;samples[0].state.dwPacketNumber=77;
    samples[0].state.Gamepad.wButtons=XINPUT_GAMEPAD_Y;
    auto native=source();native->attachPlayback(replay);
    XINPUT_STATE state{};
    for(unsigned seq=0;seq<8;++seq) {
        const auto slot=seq%4;
        need(native->state(slot,state)==(slot?ERROR_DEVICE_NOT_CONNECTED:ERROR_SUCCESS),
             "Neutral checkpoint playback lost a recorded poll");
    }
    for(unsigned cycle=0;cycle<2;++cycle)for(unsigned slot=0;slot<4;++slot) {
        need(native->state(slot,state)==(slot?ERROR_DEVICE_NOT_CONNECTED:ERROR_SUCCESS)&&
             !state.Gamepad.wButtons&&!state.Gamepad.bLeftTrigger&&!state.Gamepad.bRightTrigger&&
             !state.Gamepad.sThumbLX&&!state.Gamepad.sThumbLY&&!state.Gamepad.sThumbRX&&!state.Gamepad.sThumbRY,
             "Neutral checkpoint catch-up emitted live input or broke a four-slot cycle");
    }
    need(!samples[0].reads,"Neutral checkpoint sampled live input before the target scene");
    scene=460;
    need(native->state(0,state)==ERROR_SUCCESS&&state.dwPacketNumber==77&&
         state.Gamepad.wButtons==XINPUT_GAMEPAD_Y&&samples[0].reads==1,
         "Neutral checkpoint did not restore live input at the target scene");

    scene=366;
    auto overshot=std::make_shared<Platform::NativeInputPlayback>(path,0,scene,true,460);
    auto rejected=source();rejected->attachPlayback(overshot);
    for(unsigned seq=0;seq<8;++seq)rejected->state(seq%4,state);
    scene=476;
    rejects([&]{rejected->state(0,state);});
    std::filesystem::remove(path);
}
void records(const char* image){
    samples={};Runtime rt;rt.load(image);PPCContext entry{};rt.initialize(entry);auto* base=rt.base;rt.map(0x50000,0x2000,true,"bounded controller records");rt.controllers=source();
    EngineCpuCalls cpu(entry,base);auto& c=cpu.registers();constexpr uint32_t p=0x50100;
    for(uint32_t slot=0;slot<4;++slot){auto& s=samples[slot];s.status=0;s.state={0x12345678,{0xA5D3,0x12,0xFE,-32768,-1,0,32767}};
        s.caps={1,2,0xBEEF,{0x1357,0x81,0x29,0x1234,-32767,0x7F00,-292},{0x2468,0xACE0}};
        std::memset(rt.pointer(p-16,64,true),0xCD,64);const auto before=abi(c);const auto csr=PPCFPSCRRegister::getcsr();SetLastError(0xAABBCCDD);
        need(cpu.invoke(0x82B766A8,slot,p)==0,"Original state wrapper failed");need(abi(c)==before&&PPCFPSCRRegister::getcsr()==csr&&GetLastError()==0xAABBCCDD,"State query changed nonvolatile ABI or host state");
        constexpr std::array<uint8_t,16> expected={0x12,0x34,0x56,0x78,0xA5,0xD3,0x12,0xFE,0x80,0,0xFF,0xFF,0,0,0x7F,0xFF};
        need(!std::memcmp(rt.pointer(p,16,false),expected.data(),16),"Controller state byte order differs");
        for(uint32_t i=0;i<16;++i)need(*rt.pointer(p-16+i,1,false)==0xCD&&*rt.pointer(p+16+i,1,false)==0xCD,"State query escaped output bounds");
        need(cpu.invoke(0x82B766A8,slot,0)==0,"Null state did not query real connection");
        need(cpu.invoke(0x82B766A0,slot,1,p)==0&&abi(c)==before,"Original capabilities wrapper/ABI differs");
        constexpr std::array<uint8_t,20> capBytes={1,2,0xBE,0xEF,0x13,0x57,0x81,0x29,0x12,0x34,0x80,1,0x7F,0,0xFE,0xDC,0x24,0x68,0xAC,0xE0};
        need(!std::memcmp(rt.pointer(p,20,false),capBytes.data(),20),"Capabilities byte order/resolution differs");
        PPC_STORE_U16(p,0x1234);PPC_STORE_U16(p+2,0xABCD);const auto setReads=s.setReads;
        need(cpu.invoke(0x82B766B8,slot,p)==0&&abi(c)==before,"Original vibration wrapper/ABI differs");
        need(s.setReads==setReads+1&&s.vibration.wLeftMotorSpeed==0x1234&&s.vibration.wRightMotorSpeed==0xABCD,"Vibration byte order or publication differs");
        s.status=ERROR_DEVICE_NOT_CONNECTED;need(cpu.invoke(0x82B766A8,slot,p)==ERROR_DEVICE_NOT_CONNECTED,"Disconnected state fabricated a device");
        for(uint32_t i=0;i<16;++i)need(!*rt.pointer(p+i,1,false),"Disconnected state retained stale inputs");
        need(cpu.invoke(0x82B766A0,slot,0,p)==ERROR_DEVICE_NOT_CONNECTED,"Disconnected capabilities fabricated a device");
        for(uint32_t i=0;i<20;++i)need(!*rt.pointer(p+i,1,false),"Disconnected capabilities retained stale features");
        PPC_STORE_U16(p,0x0102);PPC_STORE_U16(p+2,0x0304);
        need(cpu.invoke(0x82B766B8,slot,p)==ERROR_DEVICE_NOT_CONNECTED,"Disconnected vibration fabricated a device");
    }
    std::memset(rt.pointer(p,20,true),0xA7,20);
    for(uint32_t slot:{4u,0xFEu,0xFFu,0xFFFFFFFFu})rejects([&]{cpu.invoke(0x82B766A8,slot,p);});
    c.r3.u64=0;c.r4.u64=1;c.r5.u64=p;rejects([&]{__imp__XamInputGetState(c,base);});
    c.r3.u64=0;c.r4.u64=1;c.r5.u64=p;c.r6.u64=c.r7.u64=c.r8.u64=0;rejects([&]{__imp__XamInputSetState(c,base);});
    c.r4.u64=0;c.r5.u64=p+1;rejects([&]{__imp__XamInputSetState(c,base);});
    c.r5.u64=p;c.r6.u64=1;rejects([&]{__imp__XamInputSetState(c,base);});c.r6.u64=0;
    rejects([&]{cpu.invoke(0x82B766A0,0,2,p);});rejects([&]{cpu.invoke(0x82B766A8,0,p+1);});rejects([&]{cpu.invoke(0x82B766A0,0,1,0);});
    samples[0].status=ERROR_ACCESS_DENIED;rejects([&]{cpu.invoke(0x82B766A8,0,p);});
    for(uint32_t i=0;i<20;++i)need(*rt.pointer(p+i,1,false)==0xA7,"Rejected controller query partially published output");
}
void movieQueries(){
    samples={};auto native=source();auto keys=std::make_shared<Platform::NativeKeyboard>();
    native->attachKeyboard(keys);keys->focus(true);XINPUT_STATE state{};
    auto poll=[&]{native->state(0,state);return state.Gamepad.wButtons;};
    keys->key(VK_RETURN,true);need(poll()==XINPUT_GAMEPAD_A,"Enter outside a movie paused instead of confirming");
    native->beginMovie(123);
    need(poll()==XINPUT_GAMEPAD_START&&!native->takeMovieSkip(123),"Held launch input skipped a new movie");
    keys->key(VK_RETURN,false);poll();
    keys->key(VK_RETURN,true);keys->key(VK_RETURN,false);
    need(!poll(),"Movie Start leaked to the ordinary game input handler");
    poll();poll(); // A short tap must survive until the movie update runs.
    const auto reads=samples[0].reads;
    need(!native->takeMovieSkip(456)&&native->takeMovieSkip(123)&&!native->takeMovieSkip(123),
         "Movie skip was lost, repeated or consumed by a foreign owner");
    need(samples[0].reads==reads,"Taking movie skip sampled the input source twice");
    keys->key(VK_SPACE,true);need(poll()==XINPUT_GAMEPAD_A&&!native->takeMovieSkip(123),"A became a skip button");
    keys->key(VK_SPACE,false);poll();
    keys->key(VK_RETURN,true);poll();need(native->takeMovieSkip(123),"Second fresh Start was lost");
    native->endMovie();need(!poll(),"Held skip pressed Start in the following menu");
    native->beginMovie(456);need(poll()==XINPUT_GAMEPAD_START&&!native->takeMovieSkip(456),"Held skip skipped a second movie");
    keys->key(VK_RETURN,false);poll();keys->key(VK_RETURN,true);poll();
    need(native->takeMovieSkip(456),"Fresh Start did not skip the second movie");
    native->endMovie();keys->key(VK_RETURN,false);poll();
    keys->key(VK_RETURN,true);need(poll()==XINPUT_GAMEPAD_A,"Enter failed to confirm outside movies");
    keys->key(VK_RETURN,false);poll();native->beginMovie(457);poll();
    keys->key(VK_RETURN,true);poll();need(native->takeMovieSkip(457),"Held Enter did not skip test movie");
    native->endMovie();keys->key(VK_RETURN,false);keys->key(VK_RETURN,true);
    need(poll()==XINPUT_GAMEPAD_A,"Fresh Enter confirm between polls stayed masked after movie skip");
    keys->key(VK_RETURN,false);poll();native->beginMovie(458);poll();
    keys->key(VK_RETURN,true);keys->key(VK_RETURN,false);poll();
    need(native->takeMovieSkip(458),"Short Enter did not skip test movie");native->endMovie();
    keys->key(VK_RETURN,true);
    need(poll()==XINPUT_GAMEPAD_A,"Fresh Enter stayed masked after an already released movie tap");
    keys->focus(false);poll();
    CommandFixture input;native->attachCommands(std::make_shared<Platform::NativeCommandInput>(input.path));
    native->beginMovie(789);poll();input.append("START\n");need(!poll()&&native->takeMovieSkip(789),"Command Start did not skip");poll();
    input.append("START\n");poll();native->endMovie();
    native->beginMovie(790);need(!native->takeMovieSkip(790),"Pending skip survived a movie end");
    poll();const auto token=native->beginModal(0);input.append("START\n");
    native->state(0,state);native->modalState(token,state);
    need(!native->takeMovieSkip(790),"Modal Start skipped a movie");native->endModal(token);poll();
    input.append("START\n");poll();need(native->takeMovieSkip(790),"Movie input did not resume after a modal");
    native->endMovie();
    for(uint32_t slot=0;slot<4;++slot){
        samples[slot].status=ERROR_SUCCESS;samples[slot].state={};native->beginMovie(800+slot);
        native->state(slot,state);samples[slot].state.Gamepad.wButtons=XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_B;
        native->state(slot,state);
        need(state.Gamepad.wButtons==XINPUT_GAMEPAD_B&&native->takeMovieSkip(800+slot),"Physical controller Start/other buttons differ");
        native->endMovie();samples[slot].state.Gamepad={};native->state(slot,state);samples[slot].status=ERROR_DEVICE_NOT_CONNECTED;
    }
}
struct Observed{};
void movieBoundarySnapshots(const char* image){
    CommandFixture identity;
    struct AuditFile {std::filesystem::path path;~AuditFile(){DeleteFileW(path.c_str());}} audit{identity.path.wstring()+L".jsonl"};
    Runtime rt;rt.load(image);PPCContext entry{};rt.initialize(entry);
    rt.map(0x50000,0x4000,true,"movie boundary diagnostic fixture");
    constexpr uint32_t owner=0x50000,name=0x51000,sourceText=0x51100,frontend=0x52000;
    auto* base=rt.base;rt.resourceAudit.configure(audit.path);
    rt.resourceAudit.mission("movie-observer-fixture");rt.resourceAudit.action("bounded original string observer; no decoder start/completion claim");
    PPC_STORE_U32(owner+0x1C,name);PPC_STORE_U16(owner+0x22,128);
    std::memcpy(rt.pointer(sourceText,12,true),"foxlogo.vp6",12);
    EngineCpuCalls cpu(entry,base);
    need(cpu.invoke(0x82743600,owner+0x1C,sourceText)==owner+0x1C&&
        PPC_LOAD_U16(owner+0x20)==11&&!PPC_LOAD_U8(name+11),"Original RwString copy layout/terminator differs");
    PPC_STORE_U32(0x82D09750,owner);PPC_STORE_U32(owner+0x14,2);PPC_STORE_U32(owner+0x2C,3);
    PPC_STORE_U32(owner+0x30,0x53000);PPC_STORE_U32(0x82E06F5C,0x53100);
    PPC_STORE_U32(0x82D08C94,frontend);PPC_STORE_U32(frontend+0x80,1);
    PPC_STORE_U32(0x82D08C98,3);PPC_STORE_U32(0x82D08C9C,3);
    PPCContext ctx=entry;ctx.r31.u32=owner;ctx.r1.u32-=112;ctx.lr=0x826B9238;
    PPC_STORE_U32(ctx.r1.u32+104,0x826B95A4);
    const auto savedFP=PPCFPSCRRegister::getcsr();
    auto boundary=[&](bool begin){PPCContext before;std::memcpy(&before,&ctx,sizeof(ctx));
        std::array<uint8_t,0x34> bytes{};std::memcpy(bytes.data(),rt.pointer(owner,uint32_t(bytes.size()),false),bytes.size());
        PPCFPSCRRegister::restoreHostCSR(0x5F80);SetLastError(0x12345678);
        if(begin)SimpsonsNativeMovieInputBegin(ctx,base);else SimpsonsNativeMovieInputEnd(ctx,base);
        const auto fp=PPCFPSCRRegister::getcsr();const auto error=GetLastError();PPCFPSCRRegister::restoreHostCSR(savedFP);
        need(!std::memcmp(&before,&ctx,sizeof(ctx))&&fp==0x5F80&&error==0x12345678&&
            !std::memcmp(bytes.data(),rt.pointer(owner,uint32_t(bytes.size()),false),bytes.size()),"Movie diagnostic altered CPU/host/original owner state");};
    boundary(true);ctx.lr=0x826B92C8;boundary(false);
    // Unreadable, unterminated, oversize and non-ASCII names remain unknown.
    // The observer may not add a filename guard to the original input latch.
    for(uint32_t variant=0;variant<4;++variant){
        PPC_STORE_U32(owner+0x1C,variant==0?0xDEAD0000:name);
        PPC_STORE_U16(owner+0x20,variant==2?128:11);PPC_STORE_U16(owner+0x22,256);
        std::memcpy(rt.pointer(name,12,true),"foxlogo.vp6",11);PPC_STORE_U8(name+11,variant==1?'X':0);
        if(variant==3)PPC_STORE_U8(name,0x80);
        ctx.lr=0x826B9238;boundary(true);ctx.lr=0x826B92C8;boundary(false);
    }
    // Prevalidation must also snapshot a foreign begin before its existing
    // owner guard rejects it, and tolerate an unreadable owner at End entry.
    ctx.r31.u32=0xDEAD0000;rejects([&]{SimpsonsNativeMovieInputBegin(ctx,base);});
    PPC_STORE_U32(0x82D09750,0xDEAD0000);ctx.lr=0x826B92C8;boundary(false);
    std::ifstream input(audit.path);const std::string receipts{std::istreambuf_iterator<char>(input),{}};
    need(receipts.find("\"asset\":\"foxlogo.vp6\"")!=std::string::npos&&
        receipts.find("\"caller\":"+std::to_string(0x826B95A4u))!=std::string::npos&&
        receipts.find("\"caller\":"+std::to_string(0x826B92C8u))!=std::string::npos&&
        receipts.find("frontendMovieIndex=1")!=std::string::npos&&receipts.find("flags=0x00000003")!=std::string::npos&&
        receipts.find("callback=present decoder=present")!=std::string::npos&&
        receipts.find("phase=decoder-stop-request")!=std::string::npos&&receipts.find("\"asset\":\"unknown\"")!=std::string::npos,
        "Movie prevalidation receipt lost bounded asset/caller/parameters/ownership or unknown context");
    size_t rows=0;for(size_t at=0;(at=receipts.find("\"event\":\"lifecycle\"",at))!=std::string::npos;++at)++rows;
    need(rows==12,"Movie lifetime observer suppressed a repeated original boundary or rejection snapshot");
    std::printf("Movie boundary observer: original RwString copy, bounded identity/unknown, original caller/flags/frontend index, repeated pre-stop boundaries, CPU/host unchanged; no movie playback/completion claim\n");
}
// Exercise the optional stack receipt itself, including stale caller storage
// and policy paths that must never claim a newly accepted live Start.
void movieObservationPolicy(){
    using Observation=Platform::NativeMovieStartObservation;
    auto empty=[&](const Observation& observed){const XINPUT_STATE zero{};
        need(!observed.accepted&&!observed.movieOwner&&!std::memcmp(&observed.raw,&zero,sizeof(zero)),"Nonaccepting poll retained a raw movie receipt");};
    samples={};auto native=source();XINPUT_STATE returned{};Observation observed{};
    auto poll=[&](uint32_t slot){const auto reads=samples[slot].reads;
        observed={samples[slot].state,0xDEADBEEF,true};const auto result=native->state(slot,returned,&observed);
        need(samples[slot].reads==reads+1,"Stack movie observation sampled the source twice");return result;};
    for(uint32_t slot=0;slot<4;++slot){
        samples[slot].status=ERROR_SUCCESS;samples[slot].state={30+slot,{}};native->beginMovie(0x50000);
        need(poll(slot)==ERROR_SUCCESS&&!returned.Gamepad.wButtons,"Stack observation changed neutral movie input");empty(observed);
        samples[slot].state.Gamepad={WORD(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_B),17,255,-32768,32767,-9,123};
        need(poll(slot)==ERROR_SUCCESS&&observed.accepted&&observed.movieOwner==0x50000&&
            !std::memcmp(&observed.raw,&samples[slot].state,sizeof(observed.raw))&&returned.Gamepad.wButtons==XINPUT_GAMEPAD_B,
            "Fresh stack receipt lost the exact accepted owner/raw state");
        need(poll(slot)==ERROR_SUCCESS&&returned.Gamepad.wButtons==XINPUT_GAMEPAD_B,"Held accepted Start changed final output");empty(observed);
        need(native->takeMovieSkip(0x50000)&&!native->takeMovieSkip(0x50000),"Stack observation changed once-only movie skip");native->endMovie();
        need(poll(slot)==ERROR_SUCCESS&&returned.Gamepad.wButtons==XINPUT_GAMEPAD_B,"Held Start escaped post-End release policy");empty(observed);
        samples[slot].state.Gamepad={};poll(slot);empty(observed);
        samples[slot].state.Gamepad.wButtons=XINPUT_GAMEPAD_START;
        need(poll(slot)==ERROR_SUCCESS&&returned.Gamepad.wButtons==XINPUT_GAMEPAD_START,"Ordinary Start changed outside movie ownership");empty(observed);
        native->beginMovie(0x50000);samples[slot].status=ERROR_DEVICE_NOT_CONNECTED;
        need(poll(slot)==ERROR_DEVICE_NOT_CONNECTED&&!native->takeMovieSkip(0x50000),"Disconnected poll became movie skip");empty(observed);
        samples[slot].status=ERROR_SUCCESS;need(poll(slot)==ERROR_SUCCESS&&returned.Gamepad.wButtons==XINPUT_GAMEPAD_START,
            "Reconnection held Start became armed");empty(observed);native->endMovie();
    }
    samples={};samples[0].status=ERROR_SUCCESS;native=source();native->beginMovie(0x50000);poll(0);
    const auto token=native->beginModal(0);samples[0].state.Gamepad.wButtons=WORD(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_B);
    need(poll(0)==ERROR_SUCCESS&&!returned.Gamepad.wButtons,"Modal ordinary state was not neutral");empty(observed);
    need(native->modalState(token,returned)==ERROR_SUCCESS&&returned.Gamepad.wButtons==WORD(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_B)&&
        !native->takeMovieSkip(0x50000),"Modal consumer became live movie acceptance");native->endModal(token);
    need(poll(0)==ERROR_SUCCESS&&!returned.Gamepad.wButtons,"Held postmodal input escaped release gating");empty(observed);
    samples[0].state.Gamepad={};poll(0);empty(observed);samples[0].state.Gamepad.wButtons=XINPUT_GAMEPAD_START;
    need(poll(0)==ERROR_SUCCESS&&!returned.Gamepad.wButtons&&observed.accepted,"Fresh postmodal Start did not resume acceptance");native->endMovie();
    samples[0].status=ERROR_ACCESS_DENIED;observed={samples[0].state,0xDEADBEEF,true};
    const auto reads=samples[0].reads;rejects([&]{native->state(0,returned,&observed);});
    need(samples[0].reads==reads+1,"Failed state changed sample count");empty(observed);
    const auto before=observed;rejects([&]{native->state(4,returned,&observed);});
    need(observed.accepted==before.accepted&&observed.movieOwner==before.movieOwner&&
        !std::memcmp(&observed.raw,&before.raw,sizeof(observed.raw))&&samples[0].reads==reads+1,"Malformed slot touched receipt or sampled input");
}
// CPU-only original wrapper and movie input observer lifetime. The supplied
// original return context is not an executed controller-manager traversal.
void movieActionAudit(const char* image){
    movieObservationPolicy();
    using Recording=Platform::NativeInputRecording;
    using Output=std::array<uint8_t,16>;
    struct Trace {std::vector<Output> output;std::array<uint32_t,4> reads{};std::vector<std::string> recording;};
    auto exercise=[&](bool auditing){
        CommandFixture input;samples={};
        struct Files {
            std::filesystem::path audit,directory,recording,playback,preserved;
            ~Files(){
                std::error_code error;
                if(std::filesystem::exists(audit,error)){
                    if(std::filesystem::copy_file(audit,preserved,std::filesystem::copy_options::none,error))
                        std::printf("AUDIT_CONTROLLER_MOVIE_GROUP_RECEIPTS preserved=%s\n",preserved.string().c_str());
                    else std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] cannot preserve movie fixture receipts: %s\n",error.message().c_str());
                }
                DeleteFileW(audit.c_str());if(!recording.empty())DeleteFileW(recording.c_str());DeleteFileW(playback.c_str());RemoveDirectoryW(directory.c_str());
            }
        } files{input.path.wstring()+L".movie-action.jsonl",input.path.wstring()+L".movie-action-recordings",{},input.path.wstring()+L".movie-action-replay.jsonl",
            std::filesystem::current_path()/(input.path.filename().wstring()+L".movie-group-preserved.jsonl")};
        Runtime rt;rt.load(image);PPCContext entry{};rt.initialize(entry);auto* base=rt.base;
        need(!rt.window&&!rt.engineDriver&&!rt.engineAudioOutput&&rt.threads.empty(),"Movie action fixture acquired GUI/GPU/workers");
        rt.controllers=source();const auto native=rt.controllers;
        native->attachCommands(std::make_shared<Platform::NativeCommandInput>(input.path));
        auto recording=std::make_shared<Recording>(files.directory);recording->toggle();files.recording=recording->path();
        need(recording->status()==Recording::Status::Recording,"CPU movie action recording did not start");native->attachRecording(recording);
        if(auditing)rt.resourceAudit.configure(files.audit);else rt.resourceAudit.configure();
        rt.resourceAudit.mission("controller-movie-action-fixture");
        const auto regions=rt.regions.size();const auto pages=rt.committedPages();
        rt.map(0x50000,0x4000,true,"CPU movie action original wrapper observer");
        constexpr uint32_t owner=0x50000,name=0x51000,text=0x51100,frontend=0x52000,out=0x53080,caller=0x82321114;
        Trace trace;
        auto rows=[&](const char* kind){std::ifstream stream(files.audit);std::vector<std::string> result;std::string row;
            const std::string match="\"kind\":\""+std::string(kind)+"\"";
            while(std::getline(stream,row))if(row.find(match)!=std::string::npos)result.push_back(row);return result;};
        auto action=[&](const std::string& row){const auto begin=row.find("\"last_action\":\"");need(begin!=std::string::npos,"Action field absent");
            const auto first=begin+15,end=row.find('"',first);need(end!=std::string::npos,"Action field unterminated");return row.substr(first,end-first);};
        auto checkpoint=[&]{if(!auditing)return std::string{};
            rt.resourceAudit.lifecycle("controller-action-checkpoint","fixture",0,"read current action","fixture-only");return action(rows("controller-action-checkpoint").back());};
        {
            EngineCpuCalls cpu(entry,base);auto& c=cpu.registers();c.lr=caller;
            PPC_STORE_U32(owner+0x1C,name);PPC_STORE_U16(owner+0x22,128);std::memcpy(rt.pointer(text,12,true),"foxlogo.vp6",12);
            need(cpu.invoke(0x82743600,owner+0x1C,text)==owner+0x1C&&PPC_LOAD_U16(owner+0x20)==11&&!PPC_LOAD_U8(name+11),"Movie action original RwString differs");
            PPC_STORE_U32(0x82D09750,owner);PPC_STORE_U32(owner+0x14,2);PPC_STORE_U32(owner+0x2C,3);
            PPC_STORE_U32(owner+0x30,0x53000);PPC_STORE_U32(0x82E06F5C,0x53100);
            PPC_STORE_U32(0x82D08C94,frontend);PPC_STORE_U32(frontend+0x80,1);
            PPC_STORE_U32(0x82D08C98,3);PPC_STORE_U32(0x82D08C9C,3);
            PPCContext boundary=entry;boundary.r31.u32=owner;boundary.r1.u32-=112;PPC_STORE_U32(boundary.r1.u32+104,0x826B95A4);
            auto movie=[&](bool begin){boundary.lr=begin?0x826B9238:0x826B92C8;PPCContext saved;std::memcpy(&saved,&boundary,sizeof(saved));
                const auto snapshotOwner=boundary.r31.u32;
                std::array<uint8_t,0x34> before{};std::memcpy(before.data(),rt.pointer(snapshotOwner,uint32_t(before.size()),false),before.size());
                const auto csr=PPCFPSCRRegister::getcsr();SetLastError(0x826B92C8);
                if(begin)SimpsonsNativeMovieInputBegin(boundary,base);else SimpsonsNativeMovieInputEnd(boundary,base);
                need(PPCFPSCRRegister::getcsr()==csr&&GetLastError()==0x826B92C8&&!std::memcmp(&saved,&boundary,sizeof(saved))&&
                    !std::memcmp(before.data(),rt.pointer(snapshotOwner,uint32_t(before.size()),false),before.size()),"Movie action observer changed owner/CPU/host state");};
            auto poll=[&](uint32_t slot,bool live=true){
                std::memset(rt.pointer(out-16,48,true),0xA7,48);const auto before=abi(c);const auto reads=samples[slot].reads;
                const auto csr=PPCFPSCRRegister::getcsr();SetLastError(0x82321114);
                const auto status=cpu.invoke(0x82B766A8,slot,out);
                need(abi(c)==before&&PPCFPSCRRegister::getcsr()==csr&&GetLastError()==0x82321114,"Movie action wrapper changed nonvolatile ABI/host state");
                need(samples[slot].reads==reads+(live?1u:0u),"Movie action wrapper changed source sampling");
                for(uint32_t i=0;i<16;++i)need(PPC_LOAD_U8(out-16+i)==0xA7&&PPC_LOAD_U8(out+16+i)==0xA7,"Movie action query escaped sixteen output bytes");
                Output value{};std::memcpy(value.data(),rt.pointer(out,16,false),16);trace.output.push_back(value);
                const DWORD expected=!live?ERROR_SUCCESS:(slot||samples[0].status==ERROR_SUCCESS?samples[slot].status:ERROR_SUCCESS);
                need(status==expected,"Movie action query status differs");return PPC_LOAD_U16(out+4);
            };
            auto accepted=[&](size_t prior,uint32_t slot,uint16_t raw,uint16_t returned){if(!auditing)return;
                const auto values=rows("controller-movie-start");need(values.size()==prior+1,"Accepted movie action missing or duplicated");
                char rawText[32],returnedText[32];std::snprintf(rawText,sizeof(rawText),"raw_buttons=%04X",unsigned(raw));
                std::snprintf(returnedText,sizeof(returnedText),"returned_buttons=%04X",unsigned(returned));const auto& row=values.back();
                need(row.find(rawText)!=std::string::npos&&row.find(returnedText)!=std::string::npos&&row.find("fresh-start-before-movie-mask")!=std::string::npos&&
                    row.find("\"asset\":\"controller:slot:"+std::to_string(slot)+"\"")!=std::string::npos&&
                    row.find("\"caller\":"+std::to_string(caller)+",")!=std::string::npos&&row.find("movieOwner=00050000")!=std::string::npos&&
                    row.find("movie-input=owned context=match")!=std::string::npos,"Accepted movie action lost raw input/caller/owner");
                const auto current=action(row);need(current.find("movie_start=accepted")!=std::string::npos&&current.find("00050000")==std::string::npos,"Accepted action/group embeds relocated owner");
            };
            auto endAction=[&](uint16_t raw){movie(false);if(!auditing)return;
                const auto stop=rows("movie-lifetime").back();char buttons[32];std::snprintf(buttons,sizeof(buttons),"buttons=%04X",unsigned(raw));
                need(stop.find("phase=decoder-stop-request")!=std::string::npos&&action(stop).find(buttons)!=std::string::npos&&
                    action(stop).find("movie_start=accepted")!=std::string::npos&&stop.find("\"asset\":\"foxlogo.vp6\"")!=std::string::npos&&
                    stop.find("\"caller\":"+std::to_string(0x826B92C8u)+",")!=std::string::npos,"Pre-stop movie receipt lost accepted trigger/identity/caller");
            };
            movie(true);need(!poll(0),"Neutral command poll failed to arm movie");
            size_t countAccepted=rows("controller-movie-start").size();input.append("START\nA\n");need(!poll(0),"Accepted command Start leaked to original wrapper");accepted(countAccepted,0,XINPUT_GAMEPAD_START,0);
            const auto startAction=checkpoint();need(!poll(0),"Command release poll changed");
            need(!native->takeMovieSkip(owner+4)&&native->takeMovieSkip(owner)&&!native->takeMovieSkip(owner),"Movie pending skip foreign/once semantics changed");
            // A remains independently queued after exactly one consumed tap.
            need(poll(0)==XINPUT_GAMEPAD_A,"Movie audit consumed a later queued command");need(!poll(0),"A release changed");
            // The A poll is a subsequent action; create a separate accepted
            // Start for the pre-stop assertion rather than relabeling it.
            countAccepted=rows("controller-movie-start").size();input.append("START\n");need(!poll(0),"Second command Start escaped masking");accepted(countAccepted,0,XINPUT_GAMEPAD_START,0);
            const auto heldAction=checkpoint();need(!poll(0)&&!poll(0),"Following neutral command polls changed");
            if(auditing)need(checkpoint()==heldAction&&startAction.find("buttons=0010")!=std::string::npos,"Neutral poll overwrote accepted action");
            const auto readsBeforeTake=samples[0].reads;need(native->takeMovieSkip(owner)&&!native->takeMovieSkip(owner)&&samples[0].reads==readsBeforeTake,"Taking accepted skip sampled again");
            endAction(XINPUT_GAMEPAD_START);need(!native->takeMovieSkip(owner),"Movie End retained pending skip");
            const auto previous=checkpoint();const auto acceptedBefore=rows("controller-movie-start").size();
            const auto noReads=samples[0].reads;std::memset(rt.pointer(out,16,true),0xA7,16);
            for(uint32_t slot:{4u,UINT32_MAX})rejects([&]{cpu.invoke(0x82B766A8,slot,out);});
            c.r3.u32=0;c.r4.u32=1;c.r5.u32=out;rejects([&]{__imp__XamInputGetState(c,base);});
            rejects([&]{cpu.invoke(0x82B766A8,0,out+1);});rejects([&]{cpu.invoke(0x82B766A8,0,0xDEAD0000);});
            c.r3.u32=0;c.r4.u32=0;c.r5.u32=out;rejects([&]{__imp__XamInputGetState(c,base+1);});
            need(samples[0].reads==noReads,"Malformed movie query sampled source");for(uint32_t i=0;i<16;++i)need(PPC_LOAD_U8(out+i)==0xA7,"Malformed query partially published output");
            if(auditing)need(checkpoint()==previous&&rows("controller-movie-start").size()==acceptedBefore,"Malformed movie query changed action");
            movie(true);poll(0);input.append("UNKNOWN\n");need(!poll(0),"Malformed command became input");
            if(auditing)need(rows("controller-movie-start").size()==acceptedBefore,"Malformed command became accepted action");
            input.append("START\n");const auto connectionReads=samples[0].reads;
            need(cpu.invoke(0x82B766A8,0,0)==ERROR_SUCCESS&&samples[0].reads==connectionReads+1,"Connection probe changed OS query semantics");
            need(!poll(0),"Connection probe consumed queued Start");accepted(acceptedBefore,0,XINPUT_GAMEPAD_START,0);endAction(XINPUT_GAMEPAD_START);poll(0);
            samples[0].status=ERROR_SUCCESS;samples[0].state.Gamepad.wButtons=XINPUT_GAMEPAD_START;
            movie(true);countAccepted=rows("controller-movie-start").size();need(poll(0)==XINPUT_GAMEPAD_START&&!native->takeMovieSkip(owner),"Held launch Start became accepted skip");
            if(auditing)need(rows("controller-movie-start").size()==countAccepted,"Unarmed held Start published acceptance");
            samples[0].state.Gamepad={};poll(0);samples[0].state.Gamepad.wButtons=XINPUT_GAMEPAD_START;need(!poll(0),"Fresh physical Start leaked");accepted(countAccepted,0,XINPUT_GAMEPAD_START,0);endAction(XINPUT_GAMEPAD_START);
            countAccepted=rows("controller-movie-start").size();need(!poll(0),"Held skip leaked after movie End");
            if(auditing)need(rows("controller-movie-start").size()==countAccepted,"Held skip relabeled after movie End");
            samples[0].state.Gamepad={};poll(0);samples[0].state.Gamepad.wButtons=XINPUT_GAMEPAD_START;need(poll(0)==XINPUT_GAMEPAD_START,"Fresh menu Start did not resume");
            samples[0].state.Gamepad={};samples[0].status=ERROR_DEVICE_NOT_CONNECTED;poll(0);
            movie(true);poll(0);const auto token=native->beginModal(0);input.append("START\n");countAccepted=rows("controller-movie-start").size();
            for(unsigned i=0;i<3;++i)need(!poll(0),"Ordinary query leaked modal input");
            XINPUT_STATE modal{};rejects([&]{native->modalState(token+1,modal);});need(native->modalState(token,modal)==ERROR_SUCCESS&&modal.Gamepad.wButtons==XINPUT_GAMEPAD_START&&!native->takeMovieSkip(owner),"Modal Start changed movie ownership");
            if(auditing)need(rows("controller-movie-start").size()==countAccepted,"Modal Start published movie action");
            native->endModal(token);poll(0);input.append("START\n");need(!poll(0),"Resumed movie Start escaped masking");accepted(countAccepted,0,XINPUT_GAMEPAD_START,0);endAction(XINPUT_GAMEPAD_START);poll(0);
            for(uint32_t slot=0;slot<4;++slot){
                samples[slot].status=ERROR_SUCCESS;samples[slot].state={100+slot,{}};movie(true);need(!poll(slot),"Physical neutral movie poll changed");
                samples[slot].state.Gamepad={WORD(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_B),7,255,-32768,32767,-123,456};
                countAccepted=rows("controller-movie-start").size();need(poll(slot)==XINPUT_GAMEPAD_B,"Movie mask altered mixed physical input");accepted(countAccepted,slot,WORD(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_B),XINPUT_GAMEPAD_B);
                need(PPC_LOAD_U32(out)==100+slot&&PPC_LOAD_U8(out+6)==7&&PPC_LOAD_U8(out+7)==255&&PPC_LOAD_U16(out+8)==0x8000&&
                    PPC_LOAD_U16(out+10)==0x7FFF&&PPC_LOAD_U16(out+12)==uint16_t(-123)&&PPC_LOAD_U16(out+14)==456,"Mixed Start mask changed packet/triggers/sticks");
                endAction(WORD(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_B));need(!native->takeMovieSkip(owner),"End retained mixed slot skip");
                samples[slot].state.Gamepad={};poll(slot);samples[slot].status=ERROR_DEVICE_NOT_CONNECTED;
            }
            // Two real wrapper acceptances of the same Start combination must
            // keep one group even when the sample packet and movie owner move.
            // These CPU begin/end observers do not execute the decoder graph.
            auto field=[&](const std::string& row,const char* key){
                const auto label="\""+std::string(key)+"\":\"";
                const auto at=row.find(label);need(at!=std::string::npos,"Grouping receipt field absent");
                const auto first=at+label.size();auto last=first;
                for(;last<row.size();++last){if(row[last]=='\\'){++last;continue;}if(row[last]=='"')break;}
                need(last<row.size(),"Grouping receipt field unterminated");return row.substr(first,last-first);
            };
            const XINPUT_GAMEPAD groupPad{WORD(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_B),7,255,-32768,32767,-123,456};
            constexpr uint32_t secondOwner=owner+0x80;
            std::memcpy(rt.pointer(secondOwner,0x34,true),rt.pointer(owner,0x34,false),0x34);
            auto groupedStart=[&](DWORD packet,XINPUT_GAMEPAD pad,uint32_t selectedOwner,uint32_t selectedCaller,const char* mission){
                rt.resourceAudit.mission(mission);PPC_STORE_U32(0x82D09750,selectedOwner);boundary.r31.u32=selectedOwner;c.lr=selectedCaller;
                samples[0].status=ERROR_SUCCESS;samples[0].state={packet,{}};
                movie(true);need(!poll(0),"Grouping neutral poll failed to arm movie");
                const auto prior=rows("controller-movie-start").size();samples[0].state.Gamepad=pad;
                std::array<uint8_t,0x34> originalOwner{};
                std::memcpy(originalOwner.data(),rt.pointer(selectedOwner,uint32_t(originalOwner.size()),false),originalOwner.size());
                need(poll(0)==WORD(pad.wButtons&~XINPUT_GAMEPAD_START),"Grouping probe changed masked output");
                need(PPC_LOAD_U8(out+6)==pad.bLeftTrigger&&PPC_LOAD_U8(out+7)==pad.bRightTrigger&&
                    PPC_LOAD_U16(out+8)==uint16_t(pad.sThumbLX)&&PPC_LOAD_U16(out+10)==uint16_t(pad.sThumbLY)&&
                    PPC_LOAD_U16(out+12)==uint16_t(pad.sThumbRX)&&PPC_LOAD_U16(out+14)==uint16_t(pad.sThumbRY),
                    "Grouping probe changed returned trigger/stick lanes");
                need(PPC_LOAD_U32(out)==packet&&!std::memcmp(originalOwner.data(),rt.pointer(selectedOwner,uint32_t(originalOwner.size()),false),originalOwner.size()),
                    "Grouping probe changed packet or movie owner bytes");
                std::string row;
                if(auditing){const auto acceptedRows=rows("controller-movie-start");need(acceptedRows.size()==prior+1,"Grouping acceptance lifecycle was deduplicated");row=acceptedRows.back();}
                need(native->takeMovieSkip(selectedOwner)&&!native->takeMovieSkip(selectedOwner),"Grouping acceptance lost exact once-only owner");
                movie(false);samples[0].state.Gamepad={};need(!poll(0),"Grouping release stayed held");return row;
            };
            const auto groupedFirst=groupedStart(1000,groupPad,owner,caller,"controller-movie-action-fixture");
            const auto groupedSecond=groupedStart(2000,groupPad,secondOwner,caller,"controller-movie-action-fixture");
            if(auditing){
                need(field(groupedFirst,"group")==field(groupedSecond,"group"),"Movie sample counters split an equivalent stable group");
                need(field(groupedFirst,"parameters").find("packet")==std::string::npos&&field(groupedSecond,"parameters").find("packet")==std::string::npos,
                    "Movie sample counter remained in semantic parameters");
                need(field(groupedFirst,"instance")=="movieOwner=00050000 raw_packet=1000 returned_packet=1000"&&
                    field(groupedSecond,"instance")=="movieOwner=00050080 raw_packet=2000 returned_packet=2000","Grouping lost exact sample/owner instances");
            }
            auto changed=groupPad;changed.bLeftTrigger=8;
            const auto triggerVariant=groupedStart(1000,changed,owner,caller,"controller-movie-action-fixture");
            changed=groupPad;changed.sThumbRY=457;
            const auto stickVariant=groupedStart(1000,changed,owner,caller,"controller-movie-action-fixture");
            changed=groupPad;changed.wButtons=XINPUT_GAMEPAD_START;
            const auto maskVariant=groupedStart(1000,changed,owner,caller,"controller-movie-action-fixture");
            const auto callerVariant=groupedStart(1000,groupPad,owner,caller+4,"controller-movie-action-fixture");
            const auto missionVariant=groupedStart(1000,groupPad,owner,caller,"controller-movie-group-other-fixture");
            if(auditing)for(const auto* variant:{&triggerVariant,&stickVariant,&maskVariant,&callerVariant,&missionVariant})
                need(field(*variant,"group")!=field(groupedFirst,"group"),"Movie semantic input/caller/mission variant collapsed");
            rt.resourceAudit.mission("controller-movie-action-fixture");PPC_STORE_U32(0x82D09750,owner);boundary.r31.u32=owner;c.lr=caller;
            samples[0].status=ERROR_DEVICE_NOT_CONNECTED;
            // Playback stores already-returned states. A recorded Start is
            // returned exactly, without inventing a live skip acceptance.
            {
                std::ofstream stream(files.playback);need(bool(stream),"Cannot create CPU movie action playback fixture");
                stream<<"{\"type\":\"header\",\"version\":1,\"boundary\":\"returned_controller_state\"}\n";
                for(uint32_t sequence=0;sequence<8;++sequence){
                    stream<<"{\"type\":\"input\",\"seq\":"<<sequence<<",\"consumer\":\"game\",\"slot\":"<<sequence%4
                        <<",\"status\":0,\"packet\":"<<200+sequence<<",\"buttons\":"<<(sequence<4?0:WORD(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_B))
                        <<",\"lt\":0,\"rt\":0,\"lx\":0,\"ly\":0,\"rx\":0,\"ry\":0}\n";
                }
                stream<<"{\"type\":\"end\",\"samples\":8}\n";need(bool(stream),"Cannot write CPU movie action playback fixture");
            }
            std::atomic<uint64_t> scene{0};auto playback=std::make_shared<Platform::NativeInputPlayback>(files.playback,0,scene);
            native->attachPlayback(playback);movie(true);countAccepted=rows("controller-movie-start").size();
            for(uint32_t sequence=0;sequence<8;++sequence){
                need(poll(sequence%4,false)==(sequence<4?0:WORD(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_B))&&PPC_LOAD_U32(out)==200+sequence,
                    "Movie action audit changed exact returned playback values");
                need(!native->takeMovieSkip(owner),"Recorded returned Start became a fresh live skip");
            }
            if(auditing)need(rows("controller-movie-start").size()==countAccepted,"Playback published a raw live movie action");
            movie(false);native->attachPlayback({});playback.reset();
            samples[0].status=ERROR_ACCESS_DENIED;const auto failureReads=samples[0].reads;const auto prior=checkpoint();
            std::memset(rt.pointer(out,16,true),0xA7,16);rejects([&]{cpu.invoke(0x82B766A8,0,out);});
            need(samples[0].reads==failureReads+1,"OS error query sampled twice");for(uint32_t i=0;i<16;++i)need(PPC_LOAD_U8(out+i)==0xA7,"OS error partially published state");
            if(auditing)need(checkpoint()==prior,"OS error published an action");samples[0].status=ERROR_DEVICE_NOT_CONNECTED;
            native->endMovie();recording->stop();need(recording->status()==Recording::Status::Saved,"Movie action recording did not retire");native->attachRecording({});native->attachCommands({});
        }
        std::ifstream record(files.recording);std::string row;bool masked=false;
        while(std::getline(record,row))if(row.find("\"type\":\"input\"")!=std::string::npos){
            const auto begin=row.find("\"t_us\":");need(begin!=std::string::npos,"Recording time field absent");const auto end=row.find(',',begin);need(end!=std::string::npos,"Recording time field malformed");
            row.erase(begin,end-begin+1);trace.recording.push_back(row);if(row.find("\"buttons\":0,")!=std::string::npos)masked=true;
        }
        need(masked&&!trace.recording.empty(),"Recorder lost normalized movie polls");
        for(uint32_t slot=0;slot<4;++slot)trace.reads[slot]=samples[slot].reads;
        rt.unmap(0x50000);need(rt.regions.size()==regions&&rt.committedPages()==pages,"Movie action observer map survived release");rejects([&]{rt.pointer(owner,1,false);});
        if(auditing){
            std::printf("AUDIT_CONTROLLER_MOVIE_GROUP group=packet_owner_excluded lifecycle=both_emitted variants=distinct raw_instances=exact returned_state=unchanged owner_begin_end=passed map_release=passed decoder_lifetime=unproven receipts=%s\n",files.preserved.string().c_str());
        }
        need(!rt.window&&!rt.engineDriver&&!rt.engineAudioOutput&&rt.threads.empty(),"Movie action test acquired GUI/GPU/worker lifetime");return trace;
    };
    const auto without=exercise(false);need(!active&&!currentContext,"Unaudited movie controller Runtime survived destruction");
    const auto with=exercise(true);need(!active&&!currentContext,"Audited movie controller Runtime survived destruction");
    need(without.output==with.output&&without.reads==with.reads&&without.recording==with.recording,"Accepted action audit changed output/sampling/returned-state recording");
    std::printf("AUDIT_CONTROLLER_MOVIE_ACTION whole_wrapper=passed accepted_start=passed mixed_start=passed all_slots=passed returned_state=unchanged recorder=returned_only modal=preserved playback=returned_only receipt_reset=passed owner_begin_end=passed caller_context=fixture_supplied map_release=passed decoder_lifetime=unproven\n");
}
void originalPoll(const char* image){
    CommandFixture input;samples={};Runtime rt;rt.load(image);rt.controllers=source();PPCContext startup{};rt.initialize(startup);const auto entry=startup;auto* base=rt.base;
    bool observed=false;rt.audioBoundaryObserver=[](uint32_t pc,PPCContext&,uint8_t*){if(pc==0x828166FC)throw Observed{};};
    try{runOriginal(startup,base);}catch(const Observed&){observed=true;}rt.audioBoundaryObserver={};need(observed,"Original input fixture missed established startup boundary");
    // Production video setup now correctly attaches its window keyboard even
    // to this early-created controller source. Isolate the physical-device
    // connect/disconnect section; native input is explicitly attached below.
    rt.controllers->attachKeyboard({});
    need(!rt.controllers->usesKeyboardMouse(),"Physical-only original input fixture retained its window keyboard");
    const auto owner=PPC_LOAD_U32(0x82E36B84);need(owner&&PPC_LOAD_U32(owner)==0x821DC300,"Actual original input manager is absent");
    EngineCpuCalls cpu(entry,base);const auto before=abi(cpu.registers());
    auto poll=[&]{const auto tick=PPC_LOAD_U32(owner+8);cpu.invoke(0x823210B0,owner);need(abi(cpu.registers())==before&&PPC_LOAD_U32(owner+8)==tick+1,"Original input poll changed ABI/tick progression");};
    poll();
    for(uint32_t slot=0;slot<4;++slot){const auto s=PPC_LOAD_U32(owner+0x1C+slot*4);need(!PPC_LOAD_U32(s+8),"Disconnected original slot was marked connected");}
    for(uint32_t cycle=0;cycle<3;++cycle){
        for(uint32_t slot=0;slot<4;++slot){auto& s=samples[slot];s.status=0;s.state={1,{uint16_t(0x1001u<<slot),uint8_t(17+slot),uint8_t(254-slot),-32768,-1,0,32767}};s.caps.Type=1;s.caps.SubType=1;}
        poll();
        for(uint32_t slot=0;slot<4;++slot){const auto descriptor=PPC_LOAD_U32(owner+0xC+slot*4),s=PPC_LOAD_U32(owner+0x1C+slot*4);
            need(PPC_LOAD_U32(descriptor+4)==0&&PPC_LOAD_U32(descriptor+8)==16&&PPC_LOAD_U32(descriptor+0xC)==4,"Original connected descriptor differs");
            need(PPC_LOAD_U32(s+8)==1&&PPC_LOAD_U32(s+0x14)==14&&PPC_LOAD_U32(s+0x18)==2&&PPC_LOAD_U32(s+0x1C)==4,"Original connected storage layout differs");
            need(PPC_LOAD_U32(s+0x10)==PPC_LOAD_U32(owner+8),"Original slot update sequence differs");
            const auto buttons=PPC_LOAD_U32(s+0x20),triggers=PPC_LOAD_U32(s+0x24),axes=PPC_LOAD_U32(s+0x28);
            const uint16_t bits=uint16_t(1u<<slot)|uint16_t(1u<<(slot+10));
            need(PPC_LOAD_U8(buttons)==uint8_t(bits)&&PPC_LOAD_U8(buttons+1)==uint8_t(bits>>8),"Original digital button compaction differs");
            need(PPC_LOAD_U8(triggers)==17+slot&&PPC_LOAD_U8(triggers+1)==254-slot,"Original trigger magnitudes differ");
            for(uint32_t i=0;i<4;++i)need(int32_t(PPC_LOAD_U32(axes+4*i))==std::array<int32_t,4>{-1000,-1,0,1000}[i],"Original signed stick normalization differs");
            need(samples[slot].capReads==cycle+1,"Original first-connection capability query count differs");
        }
        for(auto& s:samples){++s.state.dwPacketNumber;s.state.Gamepad={};}
        poll();
        for(uint32_t slot=0;slot<4;++slot){const auto s=PPC_LOAD_U32(owner+0x1C+slot*4),buttons=PPC_LOAD_U32(s+0x20),triggers=PPC_LOAD_U32(s+0x24),axes=PPC_LOAD_U32(s+0x28);
            need(!PPC_LOAD_U8(buttons)&&!PPC_LOAD_U8(buttons+1)&&!PPC_LOAD_U8(triggers)&&!PPC_LOAD_U8(triggers+1),"Original released buttons/triggers stayed pressed");
            for(uint32_t i=0;i<4;++i)need(!PPC_LOAD_U32(axes+4*i),"Original centered sticks were not centered");}
        for(auto& s:samples)s.status=ERROR_DEVICE_NOT_CONNECTED;
        poll();
        for(uint32_t slot=0;slot<4;++slot){const auto d=PPC_LOAD_U32(owner+0xC+slot*4),s=PPC_LOAD_U32(owner+0x1C+slot*4);
            need(PPC_LOAD_U32(d+4)==3&&!PPC_LOAD_U32(d+8)&&!PPC_LOAD_U32(d+0xC)&&!PPC_LOAD_U32(d+0x10),"Original disconnect did not release the descriptor mapping");
            need(!PPC_LOAD_U32(s+8)&&PPC_LOAD_U32(s+0x14)==8&&PPC_LOAD_U32(s+0x18)==8&&PPC_LOAD_U32(s+0x1C)==4,"Original disconnected state layout differs");}
    }
    auto keyboard=std::make_shared<Platform::NativeKeyboard>();rt.controllers->attachKeyboard(keyboard);keyboard->focus(true);
    keyboard->key(VK_ESCAPE,true);keyboard->key(VK_ESCAPE,false);poll();
    const auto state=PPC_LOAD_U32(owner+0x1C),buttons=PPC_LOAD_U32(state+0x20);
    need(PPC_LOAD_U32(state+8)==1&&PPC_LOAD_U8(buttons)==0x10&&!PPC_LOAD_U8(buttons+1),"Original manager did not consume the native keyboard Start press");
    poll();need(!PPC_LOAD_U8(buttons)&&!PPC_LOAD_U8(buttons+1),"Original manager did not consume keyboard release");
    keyboard->key(VK_RETURN,true);keyboard->key(VK_RETURN,false);poll();
    need(!PPC_LOAD_U8(buttons)&&PPC_LOAD_U8(buttons+1)==4,"Original manager mapped Enter to pause instead of A");
    poll();need(!PPC_LOAD_U8(buttons)&&!PPC_LOAD_U8(buttons+1),"Original manager did not release Enter confirm");
    keyboard->key(VK_SPACE,true);poll();need(!PPC_LOAD_U8(buttons)&&PPC_LOAD_U8(buttons+1)==4,"Original manager did not map keyboard A");
    keyboard->key(VK_SPACE,false);keyboard->key('W',true);poll();
    const auto axes=PPC_LOAD_U32(state+0x28);
    need(!PPC_LOAD_U32(axes)&&int32_t(PPC_LOAD_U32(axes+4))==1000,"Original manager did not consume keyboard W as forward left-stick movement");
    keyboard->focus(false);poll();need(!PPC_LOAD_U8(buttons)&&!PPC_LOAD_U8(buttons+1),"Original manager retained keyboard input after focus loss");
    keyboard->focus(true);keyboard->captureMouse(true);keyboard->mouseMotion(2,-3);
    keyboard->mouseButton(VK_RBUTTON,true);keyboard->mouseButton(VK_RBUTTON,false);
    need(cpu.invoke(0x82B766A8,0,0)==0&&cpu.invoke(0x82B766A8,0,0)==0,
         "Original null-output connection probes lost the native source");
    poll();
    need(!PPC_LOAD_U8(buttons)&&PPC_LOAD_U8(buttons+1)==8,
         "Null-output probes drained special attack before the original input manager");
    need(int32_t(PPC_LOAD_U32(axes+8))>0&&int32_t(PPC_LOAD_U32(axes+8))<=1000&&
         int32_t(PPC_LOAD_U32(axes+12))>0&&int32_t(PPC_LOAD_U32(axes+12))<=1000,
         "Original input manager lost native mouse camera motion or its Y direction");
    poll();need(!PPC_LOAD_U8(buttons+1)&&!PPC_LOAD_U32(axes+8)&&!PPC_LOAD_U32(axes+12),
                "Original input manager retained consumed mouse buttons or camera motion");
    keyboard->mouseMotion(4,5);keyboard->focus(false);poll();
    need(!PPC_LOAD_U32(axes+8)&&!PPC_LOAD_U32(axes+12),"Original input manager retained mouse motion after focus loss");
    auto commands=std::make_shared<Platform::NativeCommandInput>(input.path);rt.controllers->attachCommands(commands);
    input.append("START\nA\n");poll();need(PPC_LOAD_U8(buttons)==0x10&&!PPC_LOAD_U8(buttons+1),"Original manager did not consume command Start");
    poll();need(!PPC_LOAD_U8(buttons)&&!PPC_LOAD_U8(buttons+1),"Original manager did not consume command release");
    poll();need(!PPC_LOAD_U8(buttons)&&PPC_LOAD_U8(buttons+1)==4,"Original manager did not map command A");
    poll();need(!PPC_LOAD_U8(buttons)&&!PPC_LOAD_U8(buttons+1),"Original command A remained held");
    Platform::NativeCommandInputTestAccess::bind(*commands);
    Platform::NativeCommandInputTestAccess::now=1000;
    input.append("PAD 1000 0 32767 100\nPAD 0000 0 32767 180\nPAD 1000 0 32767 100\n");
    const auto moving=[&]{return int32_t(PPC_LOAD_U32(axes+4))==1000;};
    poll();need(PPC_LOAD_U8(buttons+1)==4&&moving(),"Original manager lost first forward PAD jump");
    Platform::NativeCommandInputTestAccess::now=1100;
    poll();need(!PPC_LOAD_U8(buttons+1)&&moving(),"Original manager centered stick on first PAD jump release");
    poll();need(!PPC_LOAD_U8(buttons+1)&&moving(),"Original manager lost forward PAD jump gap");
    Platform::NativeCommandInputTestAccess::now=1280;
    poll();need(!PPC_LOAD_U8(buttons+1)&&moving(),"Original manager centered stick before second PAD jump");
    poll();need(PPC_LOAD_U8(buttons+1)==4&&moving(),"Original manager lost second forward PAD jump");
    Platform::NativeCommandInputTestAccess::now=1380;
    poll();need(!PPC_LOAD_U8(buttons+1)&&!PPC_LOAD_U32(axes+4),"Original manager retained final PAD movement");
    Platform::NativeCommandInputTestAccess::now=2000;
    input.append("PAD 0001 0 0 123\n");poll();
    need(PPC_LOAD_U8(buttons)==1&&!PPC_LOAD_U8(buttons+1),"Original manager did not compact timed PAD Up");
    Platform::NativeCommandInputTestAccess::now=2122;poll();
    need(PPC_LOAD_U8(buttons)==1,"Original manager released timed PAD Up before 123 ms");
    Platform::NativeCommandInputTestAccess::now=2123;poll();
    need(!PPC_LOAD_U8(buttons)&&!PPC_LOAD_U8(buttons+1),"Original manager retained timed PAD Up after 123 ms");
    std::printf("Original input manager %08X: four slots,three connect/release/disconnect cycles,original button/trigger/stick consumers,window-keyboard Start/A/release\n",owner);
}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try{
        if(argc==3&&std::strcmp(argv[1],"--movie-action-only")==0){
            movieActionAudit(argv[2]);std::printf("PASS CPU movie action controllers:%zu checks; no decoder playback/GPU/gameplay claim\n",checks);return 0;
        }
        need(argc==2,"Original image or --checkpoint-only required");
        if(std::strcmp(argv[1],"--checkpoint-only")==0) {
            recordingQueries();playbackQueries();neutralReplayCatchup();
            std::printf("PASS checkpoint controllers:%zu checks; F9 marker, first-poll recording, neutral catch-up, replay-to-live handoff and scene divergence\n",checks);
            return 0;
        }
        hostQueries();keyboardQueries();keyboardNavigationQueries();mouseQueries();menuPointerQueries();windowMouseQueries();earlyControllerWindowQueries();commandQueries();padQueries();forwardDoubleJumpQueries();modalQueries();movieQueries();recordingQueries();playbackQueries();neutralReplayCatchup();records(argv[1]);movieBoundarySnapshots(argv[1]);originalPoll(argv[1]);std::printf("PASS native controllers:%zu checks; real Windows query,original ABI and input manager,raw state/capabilities,disconnect/reconnect,window keyboard and mouse,local command stream,exclusive native UI input,movie skip edges,F8 input recording,exact poll playback;no gameplay claim,ALL MUTED\n",checks);return 0;
    }
    catch(const std::exception& e){std::fprintf(stderr,"FAIL native controllers:%zu checks:%s\n",checks,e.what());return 1;}
}
