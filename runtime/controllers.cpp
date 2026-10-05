#include "runtime.h"
#include "native_controllers.h"
#include "native_input_recording.h"
#include "native_window.h"
#include "engine_cpu_calls.h"
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <string_view>

namespace Simpsons {bool firstMissionCompletionSkipBootstrap(uint8_t*);bool auditStageSkipBootstrap(uint8_t*);}

namespace Simpsons::Platform {
namespace {
int64_t playbackNumber(std::string_view line,std::string_view name) {
    const std::string key="\""+std::string(name)+"\":";
    const auto found=line.find(key);
    if(found==std::string_view::npos)throw Failure("Input playback record lacks "+std::string(name));
    const auto begin=line.data()+found+key.size();
    int64_t value=0;
    const auto parsed=std::from_chars(begin,line.data()+line.size(),value);
    if(parsed.ec!=std::errc{} || parsed.ptr==begin ||
       (parsed.ptr!=line.data()+line.size() && *parsed.ptr!=',' && *parsed.ptr!='}'))
        throw Failure("Input playback record has invalid "+std::string(name));
    return value;
}
template<class T>T playbackRange(std::string_view line,std::string_view name,int64_t minimum,int64_t maximum) {
    const auto value=playbackNumber(line,name);
    if(value<minimum || value>maximum)throw Failure("Input playback record has out-of-range "+std::string(name));
    return T(value);
}
bool parsePad(std::string_view line,XINPUT_GAMEPAD& pad,uint32_t& duration) {
    if(!line.starts_with("PAD "))return false;
    std::array<std::string_view,5> fields{};
    size_t count=0;
    size_t start=4;
    while(true) {
        if(start>=line.size() || count==fields.size())return false;
        const auto end=line.find(' ',start);
        fields[count++]=line.substr(start,end==std::string_view::npos?end:end-start);
        if(fields[count-1].empty())return false;
        if(end==std::string_view::npos)break;
        start=end+1;
    }
    if(count!=4 && count!=5)return false;
    auto parse=[](std::string_view field,int base,int& value) {
        const auto result=std::from_chars(field.data(),field.data()+field.size(),value,base);
        return result.ec==std::errc{} && result.ptr==field.data()+field.size();
    };
    int buttons=0,lx=0,ly=0,milliseconds=0,rightTrigger=0;
    const auto hexDigit=[](char digit){return (digit>='0'&&digit<='9')||(digit>='a'&&digit<='f')||(digit>='A'&&digit<='F');};
    if(fields[0].size()!=4 || !hexDigit(fields[0][0]) || !hexDigit(fields[0][1]) ||
        !hexDigit(fields[0][2]) || !hexDigit(fields[0][3]) ||
        !parse(fields[0],16,buttons) || (buttons&~NativeKeyboard::buttons) ||
        !parse(fields[1],10,lx) || lx<-32768 || lx>32767 ||
        !parse(fields[2],10,ly) || ly<-32768 || ly>32767 ||
        !parse(fields[3],10,milliseconds) || milliseconds<50 || milliseconds>2000 ||
        (count==5 && (!parse(fields[4],10,rightTrigger) || rightTrigger<0 || rightTrigger>255)))return false;
    pad={};pad.wButtons=WORD(buttons);pad.sThumbLX=SHORT(lx);pad.sThumbLY=SHORT(ly);
    pad.bRightTrigger=BYTE(rightTrigger);
    duration=uint32_t(milliseconds);
    return true;
}
}
NativeInputPlayback::NativeInputPlayback(const std::filesystem::path& path,uint64_t firstScene,
        const std::atomic<uint64_t>& sceneCount,bool resumeLive,uint64_t checkpointScene):
    scene(sceneCount),startScene(firstScene),continueLive(resumeLive),expectedEndScene(checkpointScene) {
    if(continueLive && !expectedEndScene)throw Failure("Live input handoff requires a checkpoint scene");
    std::ifstream file(path);
    if(!file)throw Failure("Cannot open input playback recording");
    std::string line;bool header=false,end=false;
    while(std::getline(file,line)) {
        if(line.size()>1024)throw Failure("Input playback line exceeds 1024 bytes");
        if(line.empty() || line.back()!='}') {
            if(file.eof())break; // An abrupt crash may leave one incomplete tail.
            throw Failure("Input playback has an incomplete line before the end");
        }
        const std::string_view row(line);
        if(row.find("\"type\":\"header\"")!=std::string_view::npos) {
            if(header || !polls.empty() || playbackNumber(row,"version")!=1 ||
               row.find("\"boundary\":\"returned_controller_state\"")==std::string_view::npos)
                throw Failure("Input playback header is unsupported");
            header=true;
        } else if(row.find("\"type\":\"input\"")!=std::string_view::npos) {
            if(!header || end)throw Failure("Input playback input is outside the recording");
            const auto seq=playbackRange<uint64_t>(row,"seq",0,std::numeric_limits<int64_t>::max());
            if(seq!=polls.size())throw Failure("Input playback sequence has a gap");
            Poll poll{};
            poll.slot=playbackRange<uint32_t>(row,"slot",0,3);
            if(poll.slot!=polls.size()%4 || row.find("\"consumer\":\"game\"")==std::string_view::npos)
                throw Failure("Input playback requires ordered game polls for all four slots");
            poll.status=playbackRange<DWORD>(row,"status",0,std::numeric_limits<DWORD>::max());
            if(poll.status!=ERROR_SUCCESS && poll.status!=ERROR_DEVICE_NOT_CONNECTED)
                throw Failure("Input playback has an unsupported connection status");
            poll.state.dwPacketNumber=playbackRange<DWORD>(row,"packet",0,std::numeric_limits<DWORD>::max());
            auto& pad=poll.state.Gamepad;
            pad.wButtons=playbackRange<WORD>(row,"buttons",0,65535);
            pad.bLeftTrigger=playbackRange<BYTE>(row,"lt",0,255);
            pad.bRightTrigger=playbackRange<BYTE>(row,"rt",0,255);
            pad.sThumbLX=playbackRange<SHORT>(row,"lx",-32768,32767);
            pad.sThumbLY=playbackRange<SHORT>(row,"ly",-32768,32767);
            pad.sThumbRX=playbackRange<SHORT>(row,"rx",-32768,32767);
            pad.sThumbRY=playbackRange<SHORT>(row,"ry",-32768,32767);
            if(poll.status==ERROR_DEVICE_NOT_CONNECTED &&
               (poll.state.dwPacketNumber || pad.wButtons || pad.bLeftTrigger || pad.bRightTrigger ||
                pad.sThumbLX || pad.sThumbLY || pad.sThumbRX || pad.sThumbRY))
                throw Failure("Disconnected input playback state is not zero");
            if ((poll.slot==0 && poll.status!=ERROR_SUCCESS) ||
                (poll.slot!=0 && poll.status!=ERROR_DEVICE_NOT_CONNECTED) ||
                pad.wButtons || pad.bLeftTrigger || pad.bRightTrigger ||
                pad.sThumbLX || pad.sThumbLY || pad.sThumbRX || pad.sThumbRY)
                neutralPrefix=false;
            polls.push_back(poll);
        } else if(row.find("\"type\":\"end\"")!=std::string_view::npos) {
            if(!header || end || playbackNumber(row,"samples")!=int64_t(polls.size()))
                throw Failure("Input playback end count differs from recorded polls");
            end=true;
        } else throw Failure("Input playback contains an unknown row");
    }
    if(!file.eof() || !header || polls.empty() || polls.size()%4)
        throw Failure("Input playback does not contain complete four-slot polls");
    std::fprintf(stderr,"[INPUT PLAYBACK] loaded polls=%zu cycles=%zu start_scene=%llu\n",
        polls.size(),polls.size()/4,static_cast<unsigned long long>(startScene));
}
bool NativeInputPlayback::ready(uint32_t slot) const {
    return started || (slot==0 && scene.load(std::memory_order_acquire)>=startScene);
}
bool NativeInputPlayback::handoffReady(uint32_t slot) const {
    if(!continueLive || !started || cursor!=polls.size())return false;
    // A neutral recording can run out of polls before the same scripted scene
    // reaches its checkpoint. Keep returning neutral input until it catches up.
    if(neutralPrefix && slot!=0)return false;
    if(slot!=0)throw Failure("Input playback cannot hand off in the middle of a controller poll cycle");
    const auto current=scene.load(std::memory_order_acquire);
    if(neutralPrefix && current<expectedEndScene)return false;
    const auto difference=current>expectedEndScene?current-expectedEndScene:expectedEndScene-current;
    if(difference>15)throw Failure("Input playback checkpoint scene diverged: expected "+
        std::to_string(expectedEndScene)+", reached "+std::to_string(current));
    std::fprintf(stderr,"[INPUT PLAYBACK] LIVE scene=%llu expected=%llu difference=%llu; ordinary controller input restored\n",
        static_cast<unsigned long long>(current),static_cast<unsigned long long>(expectedEndScene),
        static_cast<unsigned long long>(difference));
    return true;
}
DWORD NativeInputPlayback::sample(uint32_t slot,XINPUT_STATE& result) {
    if(!started) {
        started=true;
        std::fprintf(stderr,"[INPUT PLAYBACK] START scene=%llu polls=%zu\n",
            static_cast<unsigned long long>(scene.load()),polls.size());
    }
    if(cursor==polls.size()) {
        if(!ended) {
            ended=true;
            std::fprintf(stderr,"[INPUT PLAYBACK] END scene=%llu; returning neutral controller state\n",
                static_cast<unsigned long long>(scene.load()));
        }
        result={};return slot==0?ERROR_SUCCESS:ERROR_DEVICE_NOT_CONNECTED;
    }
    const auto& row=polls[cursor];
    if(row.slot!=slot)throw Failure("Input playback poll order diverged at sequence "+std::to_string(cursor));
    result=row.state;
    if(slot==0 && (result.dwPacketNumber!=previous.dwPacketNumber ||
            std::memcmp(&result.Gamepad,&previous.Gamepad,sizeof(result.Gamepad)))) {
        const auto& pad=result.Gamepad;
        std::fprintf(stderr,"[INPUT PLAYBACK] state seq=%zu scene=%llu packet=%lu buttons=%04X rt=%u left=(%d,%d)\n",
            cursor,static_cast<unsigned long long>(scene.load()),result.dwPacketNumber,pad.wButtons,
            unsigned(pad.bRightTrigger),int(pad.sThumbLX),int(pad.sThumbLY));
        previous=result;
    }
    ++cursor;
    return row.status;
}
NativeCommandInput::NativeCommandInput(const std::filesystem::path& path) {
    file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) throw Failure("Cannot open controller command file: "+std::to_string(GetLastError()));
    LARGE_INTEGER zero{};
    if(GetFileType(file)!=FILE_TYPE_DISK || !SetFilePointerEx(file,zero,nullptr,FILE_END)) {
        CloseHandle(file);file=INVALID_HANDLE_VALUE;throw Failure("Controller commands require an existing regular file");
    }
    std::fprintf(stderr,"[NATIVE INPUT] local command channel ready; existing contents skipped; append START, A, B, BACK, UP, DOWN, LEFT, RIGHT, MOVE_W, MOVE_S, MOVE_A, MOVE_D or PAD buttons lx ly duration_ms [rt] with a newline\n");
}
NativeCommandInput::~NativeCommandInput(){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
void NativeCommandInput::discard() {
    LARGE_INTEGER zero{};
    if(!SetFilePointerEx(file,zero,nullptr,FILE_END)) throw Failure("Cannot discard controller command input");
    queued.clear();line.clear();overlong=releaseNext=holding=releaseWasPad=false;held=releasePad={};holdUntil=0;
}
XINPUT_GAMEPAD NativeCommandInput::sample() {
    if(holding){if(clock()<holdUntil)return held;holding=false;held={};}
    // Read when the bounded queue drains, including before a release poll so
    // adjacent PAD segments can preserve a continuous stick direction.
    if(queued.empty()) {
        std::array<char,64> bytes{};DWORD count=0;
        if(!ReadFile(file,bytes.data(),DWORD(bytes.size()),&count,nullptr)) throw Failure("Cannot read controller command input");
        for(DWORD i=0;i<count;++i) {
            const auto value=bytes[i];
            if(value=='\n') {
                if(!line.empty() && line.back()=='\r')line.pop_back();
                Command command{};bool valid=false;
                if(!overlong) {
                    if(line.starts_with("PAD ")){
                        valid=parsePad(line,command.gamepad,command.holdMs);
                        command.pad=valid;
                    }
                    else {
                        bool hold=false;
                        if(line.ends_with("_HOLD")){line.resize(line.size()-5);hold=true;}
                        if(line=="START")command.gamepad.wButtons=XINPUT_GAMEPAD_START;
                        else if(line=="A")command.gamepad.wButtons=XINPUT_GAMEPAD_A;
                        else if(line=="B")command.gamepad.wButtons=XINPUT_GAMEPAD_B;
                        else if(line=="BACK")command.gamepad.wButtons=XINPUT_GAMEPAD_BACK;
                        else if(line=="UP")command.gamepad.wButtons=XINPUT_GAMEPAD_DPAD_UP;
                        else if(line=="DOWN")command.gamepad.wButtons=XINPUT_GAMEPAD_DPAD_DOWN;
                        else if(line=="LEFT")command.gamepad.wButtons=XINPUT_GAMEPAD_DPAD_LEFT;
                        else if(line=="RIGHT")command.gamepad.wButtons=XINPUT_GAMEPAD_DPAD_RIGHT;
                        else if(line=="MOVE_W")command.gamepad.sThumbLY=32767;
                        else if(line=="MOVE_S")command.gamepad.sThumbLY=-32768;
                        else if(line=="MOVE_A")command.gamepad.sThumbLX=-32768;
                        else if(line=="MOVE_D")command.gamepad.sThumbLX=32767;
                        valid=command.gamepad.wButtons || command.gamepad.sThumbLX || command.gamepad.sThumbLY;
                        command.holdMs=hold?250:0;
                    }
                }
                if(valid)queued.push_back(command);
                else std::fprintf(stderr,"[NATIVE INPUT] rejected unknown or oversized local command\n");
                line.clear();overlong=false;
            } else if(line.size()<64)line.push_back(value);
            else overlong=true;
        }
    }
    // A release poll separates repeated button taps. When the next PAD hold
    // continues the same movement, release only buttons/trigger so the stick
    // does not center for a frame between the two jump presses.
    if(releaseNext){
        releaseNext=false;
        XINPUT_GAMEPAD released{};
        if(releaseWasPad && !queued.empty() && queued.front().pad &&
           queued.front().gamepad.sThumbLX==releasePad.sThumbLX &&
           queued.front().gamepad.sThumbLY==releasePad.sThumbLY) {
            released.sThumbLX=releasePad.sThumbLX;
            released.sThumbLY=releasePad.sThumbLY;
        }
        return released;
    }
    if(queued.empty())return {};
    const auto command=queued.front();queued.pop_front();releaseNext=true;
    releasePad=command.gamepad;releaseWasPad=command.pad;
    if(command.holdMs){held=command.gamepad;holdUntil=clock()+command.holdMs;holding=true;}
    const auto& value=command.gamepad;
    std::fprintf(stderr,"[NATIVE INPUT] local command tap buttons=%04X hold_ms=%u; left=(%d,%d) rt=%u; delivered to native controller source\n",
        value.wButtons,command.holdMs,int(value.sThumbLX),int(value.sThumbLY),unsigned(value.bRightTrigger));
    // Opted-in diagnostic drivers wait for this receipt before their next
    // timed action. Presentation logging may otherwise flush seconds later.
    std::fflush(stderr);
    return value;
}
namespace {
WORD keyboardButton(uint32_t code) {
    switch(code) {
    case VK_ESCAPE:return XINPUT_GAMEPAD_START;
    case VK_RETURN:case VK_SPACE:return XINPUT_GAMEPAD_A;
    case 'J':return XINPUT_GAMEPAD_X;
    case 'K':return XINPUT_GAMEPAD_B;
    case 'E':return XINPUT_GAMEPAD_Y;
    case 'Q':return XINPUT_GAMEPAD_LEFT_SHOULDER;
    case 'R':return XINPUT_GAMEPAD_RIGHT_SHOULDER;
    case 'F':return XINPUT_GAMEPAD_LEFT_THUMB;
    case 'G':return XINPUT_GAMEPAD_RIGHT_THUMB;
    case VK_TAB:case VK_BACK:return XINPUT_GAMEPAD_BACK;
    case VK_UP:return XINPUT_GAMEPAD_DPAD_UP;
    case VK_DOWN:return XINPUT_GAMEPAD_DPAD_DOWN;
    case VK_LEFT:return XINPUT_GAMEPAD_DPAD_LEFT;
    case VK_RIGHT:return XINPUT_GAMEPAD_DPAD_RIGHT;
    default:return 0;
    }
}
uint32_t integerSquareRoot(uint64_t value) {
    uint32_t low=0,high=65536;
    while(low+1<high) {
        const uint32_t middle=low+(high-low)/2;
        if(uint64_t(middle)*middle<=value)low=middle;else high=middle;
    }
    return low;
}
void mouseStick(int32_t x,int32_t y,SHORT& rightX,SHORT& rightY) {
    const auto distance=integerSquareRoot(uint64_t(int64_t(x)*x+int64_t(y)*y));
    if(!distance) {rightX=rightY=0;return;}
    // Mouse counts express intended camera movement rather than stick drift.
    // Add a radial deadzone offset so gentle motion survives the game's stick
    // filtering, preserving direction and limiting diagonal magnitude as well.
    const uint32_t magnitude=(std::min)(uint32_t(32767),distance+XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    const auto project=[&](int32_t axis) {
        const auto value=int64_t(axis)*magnitude/distance;
        return SHORT(value<=-32767?-32768:value>=32767?32767:value);
    };
    rightX=project(x);rightY=project(y);
}
}
void NativeKeyboard::focus(bool active) {
    std::lock_guard lock(mutex);focused=active;
    if(!active) {
        held.fill(false);pressed.fill(false);mouseHeld=mousePressed=0;
        mouseX=mouseY=0;mouseCaptured=false;movieEnterHeld=false;
        pointerInside=false;pointer={};pointerHeld=0;
    }
}
bool NativeKeyboard::key(uint32_t code,bool down) {
    if(code>=held.size())return false;
    if(!keyboardButton(code)&&code!=VK_LSHIFT&&code!=VK_CONTROL&&code!=VK_LCONTROL&&code!=VK_RCONTROL&&
       code!='W'&&code!='S'&&code!='A'&&code!='D')return false;
    std::lock_guard lock(mutex);
    if(focused) {
        if(code==VK_RETURN&&(!down||!held[code]))movieEnterHeld=false;
        if(down) {pressed[code]=pressed[code]||!held[code];held[code]=true;}
        else held[code]=false;
    }
    return true;
}
void NativeKeyboard::captureMouse(bool active) {
    std::lock_guard lock(mutex);mouseCaptured=active&&focused&&!menuMouse;
    mouseHeld=mousePressed=0;mouseX=mouseY=0;
}
bool NativeKeyboard::mouseButton(uint32_t code,bool down) {
    const uint8_t bit=code==VK_LBUTTON?1:code==VK_RBUTTON?2:code==VK_MBUTTON?4:0;
    if(!bit)return false;
    std::lock_guard lock(mutex);
    if(focused) {
        if(menuMouse) {
            if(down) {
                if(!(pointerHeld&bit)&&pointerInside) {
                    if(!pointer.pressed)pressPosition=pointer;
                    pointer.pressed|=bit;
                }
                pointerHeld|=bit;
            }
            else pointerHeld&=uint8_t(~bit);
            return true;
        }
        if(down) {mousePressed|=uint8_t(bit&~mouseHeld);mouseHeld|=bit;}
        else mouseHeld&=uint8_t(~bit);
    }
    return true;
}
void NativeKeyboard::menuMode(bool active) {
    std::lock_guard lock(mutex);
    if(menuMouse==active)return;
    menuMouse=active;mouseHeld=mousePressed=0;mouseX=mouseY=0;
    pointer.pressed=0;pointer.wheel=0;pointerHeld=0;
    pointer.moved=pointerInside;
    if(active)mouseCaptured=false;
}
void NativeKeyboard::pointerMove(int32_t x,int32_t y,uint32_t width,uint32_t height) {
    std::lock_guard lock(mutex);
    const bool inside=x>=0&&y>=0&&uint32_t(x)<width&&uint32_t(y)<height;
    pointer.moved=pointer.moved||!pointerInside||pointer.x!=x||pointer.y!=y||pointer.width!=width||pointer.height!=height;
    pointer.x=x;pointer.y=y;pointer.width=width;pointer.height=height;pointerInside=inside;
}
void NativeKeyboard::pointerLeave() {
    std::lock_guard lock(mutex);pointerInside=false;pointer.pressed=0;pointer.wheel=0;pointerHeld=0;
}
void NativeKeyboard::pointerWheel(int32_t delta) {
    std::lock_guard lock(mutex);
    if(!focused||!menuMouse||!pointerInside)return;
    const auto sum=int64_t(pointer.wheel)+delta;
    pointer.wheel=int32_t(sum<-12000?-12000:sum>12000?12000:sum);
}
NativeMenuPointer NativeKeyboard::menuPointer() {
    std::lock_guard lock(mutex);
    auto out=pointer;out.active=focused&&menuMouse&&pointerInside;
    if(pointer.pressed) {
        out.x=pressPosition.x;out.y=pressPosition.y;out.width=pressPosition.width;out.height=pressPosition.height;
        pointer.moved=pointer.x!=out.x||pointer.y!=out.y||pointer.width!=out.width||pointer.height!=out.height;
    } else pointer.moved=false;
    pointer.pressed=0;pointer.wheel=0;
    return out.active?out:NativeMenuPointer{};
}
void NativeKeyboard::mouseMotion(int32_t x,int32_t y) {
    std::lock_guard lock(mutex);
    if(!focused||!mouseCaptured)return;
    // Integer-only conversion avoids changing guest/host floating-point state.
    // Raw motion is a one-poll impulse, bounded at the native stick endpoints.
    const auto bounded=[](int64_t value){return int32_t(value < -32768?-32768:value > 32767?32767:value);};
    mouseX=bounded(int64_t(mouseX)+int64_t(x)*1024);
    mouseY=bounded(int64_t(mouseY)-int64_t(y)*1024);
}
XINPUT_STATE NativeKeyboard::sample(bool movie,WORD* directionPresses) {
    std::lock_guard lock(mutex);
    XINPUT_GAMEPAD value{};
    const auto active=[&](uint32_t code){return held[code]||pressed[code];};
    for(uint32_t code=0;code<held.size();++code)if(code!=VK_RETURN&&active(code))value.wButtons|=keyboardButton(code);
    // Enter confirms normally and retains the existing movie skip action. A
    // movie-held Enter must release before it can confirm the following menu.
    if(!active(VK_RETURN))movieEnterHeld=false;
    else if(movie) {value.wButtons|=XINPUT_GAMEPAD_START;movieEnterHeld=held[VK_RETURN];}
    else if(!movieEnterHeld)value.wButtons|=XINPUT_GAMEPAD_A;
    const auto mouse=menuMouse?uint8_t(0):uint8_t(mouseHeld|mousePressed);
    if(mouse&1)value.wButtons|=XINPUT_GAMEPAD_X;
    if(mouse&2)value.wButtons|=XINPUT_GAMEPAD_B;
    if(mouse&4)value.wButtons|=XINPUT_GAMEPAD_RIGHT_THUMB;
    value.bRightTrigger=active(VK_LSHIFT)?255:0;
    value.bLeftTrigger=active(VK_CONTROL)||active(VK_LCONTROL)||active(VK_RCONTROL)?255:0;
    const bool left=active('A'),right=active('D'),up=active('W'),down=active('S');
    value.sThumbLX=left==right?0:(right?32767:-32768);
    value.sThumbLY=up==down?0:(up?32767:-32768);
    if(directionPresses) {
        *directionPresses=0;
        if(left&&!right&&pressed['A'])*directionPresses|=XINPUT_GAMEPAD_DPAD_LEFT;
        if(right&&!left&&pressed['D'])*directionPresses|=XINPUT_GAMEPAD_DPAD_RIGHT;
        if(up&&!down&&pressed['W'])*directionPresses|=XINPUT_GAMEPAD_DPAD_UP;
        if(down&&!up&&pressed['S'])*directionPresses|=XINPUT_GAMEPAD_DPAD_DOWN;
    }
    mouseStick(mouseX,mouseY,value.sThumbRX,value.sThumbRY);
    pressed.fill(false);mousePressed=0;mouseX=mouseY=0;
    if(std::memcmp(&value,&last,sizeof(value))) {
        ++packet;last=value;
        std::fprintf(stderr,"[NATIVE INPUT] game-window keyboard buttons=%04X rt=%u left=(%d,%d) packet=%lu lt=%u right=(%d,%d)\n",
            value.wButtons,unsigned(value.bRightTrigger),int(value.sThumbLX),int(value.sThumbLY),packet,
            unsigned(value.bLeftTrigger),int(value.sThumbRX),int(value.sThumbRY));
    }
    XINPUT_STATE result{};result.dwPacketNumber=packet;result.Gamepad=value;return result;
}
void NativeKeyboard::discard(bool pointerEvents) {
    std::lock_guard lock(mutex);pressed.fill(false);mousePressed=0;mouseX=mouseY=0;
    if(pointerEvents) {pointer.pressed=0;pointer.wheel=0;}
}
void NativeControllers::attachKeyboard(const std::shared_ptr<NativeKeyboard>& source) {
    std::lock_guard lock(mutex);keyboard=source;
    navigation={};navigationPrevious=0;
    keyboardMouseSource.store(bool(source)&&previous[0]!=ERROR_SUCCESS,std::memory_order_relaxed);
}
void NativeControllers::attachCommands(const std::shared_ptr<NativeCommandInput>& source) {
    std::lock_guard lock(mutex);commands=source;
}
void NativeControllers::attachRecording(const std::shared_ptr<NativeInputRecording>& source) {
    std::lock_guard lock(mutex);recording=source;
}
void NativeControllers::attachPlayback(const std::shared_ptr<NativeInputPlayback>& source) {
    std::lock_guard lock(mutex);
    if(auto keys=keyboard.lock())keys->discard();
    playback=source;
}
DWORD NativeControllers::connectionStatus(uint32_t slot){
    if(slot>=4)throw Failure("Native controller slot is outside the qualified range0..3");
    std::lock_guard lock(mutex);XINPUT_STATE ignored{};
    const auto status=queryState(slot,&ignored);
    if(status!=ERROR_SUCCESS&&status!=ERROR_DEVICE_NOT_CONNECTED)throw Failure("Windows controller connection query failed: "+std::to_string(status));
    if(slot==0&&status==ERROR_DEVICE_NOT_CONNECTED&&(!keyboard.expired()||commands))return ERROR_SUCCESS;
    return status;
}
DWORD NativeControllers::state(uint32_t slot,XINPUT_STATE& result,NativeMovieStartObservation* observation){
    constexpr uint32_t kMaxControllers=4;
    if(slot>=kMaxControllers)throw Failure("Native controller slot is outside the qualified range0..3");
    if(observation)*observation={};
    std::lock_guard lock(mutex);
    if(playback && playback->handoffReady(slot)) {
        if(auto keys=keyboard.lock())keys->discard();
        playback.reset();
    }
    if(playback && playback->ready(slot) && (!modalToken || playback->active())) {
        if(slot==0){navigation={};navigationPrevious=0;}
        if(modalToken)throw Failure("Input playback reached an unrecorded native modal dialog");
        const auto status=playback->sample(slot,result);
        // Recordings contain controller-shaped values, not device identity.
        // Keep the selected live prompt set until ordinary input resumes.
        if(recording) {
            recording->maybeStart(slot);
            recording->sample(slot,status,result);
        }
        return status;
    }
    if(recording && !modalToken)recording->maybeStart(slot);
    if(modalToken){
        if(slot==0){navigation={};navigationPrevious=0;}
        const auto status=queryState(slot,&result);
        if(status!=ERROR_SUCCESS&&status!=ERROR_DEVICE_NOT_CONNECTED)throw Failure("Windows controller state query failed during native UI");
        if(slot==0)keyboardMouseSource.store(status==ERROR_DEVICE_NOT_CONNECTED&&!keyboard.expired(),std::memory_order_relaxed);
        const bool fallback=slot==0&&(!keyboard.expired()||commands);
        const DWORD packet=status==ERROR_SUCCESS?result.dwPacketNumber+1:fallbackPacket+1;
        result={};result.dwPacketNumber=packet;
        const DWORD returned=status==ERROR_SUCCESS||fallback?ERROR_SUCCESS:ERROR_DEVICE_NOT_CONNECTED;
        if(recording)recording->sample(slot,returned,result);
        return returned;
    }
    const auto status=stateUnlocked(slot,result);
    if(releaseAfterModal[slot]){
        const auto& pad=result.Gamepad;
        const bool released=status!=ERROR_SUCCESS||(!pad.wButtons&&!pad.bLeftTrigger&&!pad.bRightTrigger&&
            !pad.sThumbLX&&!pad.sThumbLY&&!pad.sThumbRX&&!pad.sThumbRY);
        result.Gamepad={};if(released)releaseAfterModal[slot]=false;
    }
    const bool start=status==ERROR_SUCCESS&&(result.Gamepad.wButtons&XINPUT_GAMEPAD_START);
    if(movieOwner){
        if(status!=ERROR_SUCCESS)movieArmed[slot]=false;
        else if(!start&&!movieArmed[slot]){
            movieArmed[slot]=true;
            std::fprintf(stderr,"[NATIVE MOVIE INPUT] armed owner=%08X slot=%u; neutral input observed\n",movieOwner,slot);
        }
        else if(start&&movieArmed[slot]){
            if(observation)*observation={result,movieOwner,true};
            movieSkipPending=true;movieArmed[slot]=false;releaseMovieStart[slot]=true;
        }
    }
    if(releaseMovieStart[slot]){
        result.Gamepad.wButtons&=WORD(~XINPUT_GAMEPAD_START);
        if(!start)releaseMovieStart[slot]=false;
    }
    if(recording)recording->sample(slot,status,result);
    return status;
}
void NativeControllers::beginMovie(uint32_t owner){
    std::lock_guard lock(mutex);
    if(!owner)throw Failure("Absent movie input owner");
    movieOwner=owner;movieSkipPending=false;movieArmed.fill(false);
    if(auto keys=keyboard.lock())keys->discard();
}
bool NativeControllers::takeMovieSkip(uint32_t owner){
    std::lock_guard lock(mutex);
    if(!owner||owner!=movieOwner||!movieSkipPending)return false;
    movieSkipPending=false;return true;
}
void NativeControllers::endMovie(){
    std::lock_guard lock(mutex);movieOwner=0;movieSkipPending=false;movieArmed.fill(false);
    if(auto keys=keyboard.lock())keys->discard();
}
DWORD NativeControllers::queryState(uint32_t slot,XINPUT_STATE* result){
    if(slot>=4)return getState(slot,result);
    const uint64_t now=GetTickCount64();
    if(disconnectedPollMs && disconnectedAt[slot] && now-disconnectedAt[slot]<disconnectedPollMs) {
        *result={};return ERROR_DEVICE_NOT_CONNECTED;
    }
    const DWORD status=getState(slot,result);
    disconnectedAt[slot]=status==ERROR_DEVICE_NOT_CONNECTED?(now?now:1):0;
    return status;
}
DWORD NativeControllers::stateUnlocked(uint32_t slot,XINPUT_STATE& result){
    const auto status=queryState(slot,&result);
    if(status!=ERROR_SUCCESS&&status!=ERROR_DEVICE_NOT_CONNECTED)throw Failure("Windows controller state query failed: "+std::to_string(status));
    if(previous[slot]!=status){std::fprintf(stderr,"[NATIVE INPUT] Windows controller slot=%u status=%lu; real device query\n",slot,status);previous[slot]=status;}
    if(slot==0) {
        navigation={};
        const auto keys=keyboard.lock();
        keyboardMouseSource.store(status==ERROR_DEVICE_NOT_CONNECTED&&bool(keys),std::memory_order_relaxed);
        if(status==ERROR_DEVICE_NOT_CONNECTED && (keys || commands)) {
            WORD directionPresses=0;
            XINPUT_GAMEPAD gamepad=keys?keys->sample(movieOwner&&!modalToken,&directionPresses).Gamepad:XINPUT_GAMEPAD{};
            WORD directions=0;
            if(gamepad.sThumbLX<0)directions|=XINPUT_GAMEPAD_DPAD_LEFT;
            if(gamepad.sThumbLX>0)directions|=XINPUT_GAMEPAD_DPAD_RIGHT;
            if(gamepad.sThumbLY<0)directions|=XINPUT_GAMEPAD_DPAD_DOWN;
            if(gamepad.sThumbLY>0)directions|=XINPUT_GAMEPAD_DPAD_UP;
            bool commandStick=false;
            if(commands){
                const auto command=commands->sample();gamepad.wButtons|=command.wButtons;
                commandStick=command.sThumbLX||command.sThumbLY;
                if(command.bRightTrigger)gamepad.bRightTrigger=command.bRightTrigger;
                if(command.sThumbLX)gamepad.sThumbLX=command.sThumbLX;
                if(command.sThumbLY)gamepad.sThumbLY=command.sThumbLY;
            }
            if(keys&&!movieOwner&&!modalToken&&!commandStick)
                navigation={true,WORD(directionPresses|(directions&~navigationPrevious)),directions};
            navigationPrevious=navigation.active?directions:0;
            if(std::memcmp(&gamepad,&fallbackLast,sizeof(gamepad))){fallbackLast=gamepad;++fallbackPacket;}
            result={};result.dwPacketNumber=fallbackPacket;result.Gamepad=gamepad;return ERROR_SUCCESS;
        }
        if(keys)keys->discard(false);
        if(commands)commands->discard();
        navigationPrevious=0;
    }
    return status;
}
NativeKeyboardNavigation NativeControllers::keyboardNavigation(){
    std::lock_guard lock(mutex);
    return movieOwner||modalToken||releaseAfterModal[0]||keyboard.expired()||(playback&&playback->active())?
        NativeKeyboardNavigation{}:navigation;
}
NativeMenuPointer NativeControllers::menuPointer(){
    std::lock_guard lock(mutex);
    const auto keys=keyboard.lock();
    if(!keys)return {};
    const auto pointer=keys->menuPointer();
    return movieOwner||modalToken||releaseAfterModal[0]||(playback&&playback->active())?
        NativeMenuPointer{}:pointer;
}
uint64_t NativeControllers::beginModal(uint32_t slot){
    if(slot>=4)throw Failure("Invalid native UI controller slot");std::lock_guard lock(mutex);
    if(playback && playback->active())throw Failure("Input playback reached an unrecorded native modal dialog");
    if(modalToken||!nextModalToken)throw Failure("Native controller input already belongs to a UI");
    movieSkipPending=false;movieArmed.fill(false);
    // Commands queued before the dialog opened must not choose a destination.
    if(commands)commands->discard();if(auto keys=keyboard.lock())keys->discard();
    XINPUT_STATE state{};const auto status=stateUnlocked(slot,state);
    const auto& pad=state.Gamepad;
    modalArmed=status!=ERROR_SUCCESS||(!pad.wButtons&&!pad.bLeftTrigger&&!pad.bRightTrigger&&
        !pad.sThumbLX&&!pad.sThumbLY&&!pad.sThumbRX&&!pad.sThumbRY);
    modalSlot=slot;modalToken=nextModalToken++;return modalToken;
}
DWORD NativeControllers::modalState(uint64_t token,XINPUT_STATE& result){
    std::lock_guard lock(mutex);if(!token||token!=modalToken)throw Failure("Foreign native UI input token");
    const auto status=stateUnlocked(modalSlot,result);
    if(!modalArmed){
        const auto& pad=result.Gamepad;
        modalArmed=status!=ERROR_SUCCESS||(!pad.wButtons&&!pad.bLeftTrigger&&!pad.bRightTrigger&&
            !pad.sThumbLX&&!pad.sThumbLY&&!pad.sThumbRX&&!pad.sThumbRY);
        result.Gamepad={};
    }
    if(recording)recording->sample(modalSlot,status,result,true);
    return status;
}
void NativeControllers::endModal(uint64_t token){
    std::lock_guard lock(mutex);if(!token||token!=modalToken)throw Failure("Foreign native UI input release");
    if(commands)commands->discard();if(auto keys=keyboard.lock())keys->discard();
    modalToken=0;modalArmed=false;releaseAfterModal.fill(true);
}
DWORD NativeControllers::capabilities(uint32_t slot,uint32_t flags,XINPUT_CAPABILITIES& result){
    if(slot>=4||flags>1)throw Failure("Native controller capabilities query is outside the qualified contract");
    std::lock_guard lock(mutex);const auto status=getCapabilities(slot,flags,&result);
    if(status!=ERROR_SUCCESS&&status!=ERROR_DEVICE_NOT_CONNECTED)throw Failure("Windows controller capabilities query failed: "+std::to_string(status));
    if(slot==0 && status==ERROR_DEVICE_NOT_CONNECTED && (!keyboard.expired() || commands)) {
        result={};result.Type=XINPUT_DEVTYPE_GAMEPAD;result.SubType=XINPUT_DEVSUBTYPE_GAMEPAD;
        result.Gamepad.wButtons=NativeKeyboard::buttons;
        if(!keyboard.expired()) {
            result.Gamepad.bLeftTrigger=result.Gamepad.bRightTrigger=255;
            result.Gamepad.sThumbRX=result.Gamepad.sThumbRY=32767;
        }
        if(commands){result.Gamepad.wButtons|=XINPUT_GAMEPAD_X|XINPUT_GAMEPAD_Y;result.Gamepad.bRightTrigger=255;}
        if(!keyboard.expired() || commands) {result.Gamepad.sThumbLX=32767;result.Gamepad.sThumbLY=32767;}
        return ERROR_SUCCESS;
    }
    return status;
}
namespace {
DWORD WINAPI noPhysicalState(DWORD,XINPUT_STATE*) noexcept {return ERROR_DEVICE_NOT_CONNECTED;}
DWORD WINAPI noPhysicalCapabilities(DWORD,DWORD,XINPUT_CAPABILITIES*) noexcept {return ERROR_DEVICE_NOT_CONNECTED;}
DWORD WINAPI noPhysicalVibration(DWORD,XINPUT_VIBRATION*) noexcept {return ERROR_DEVICE_NOT_CONNECTED;}
}
void NativeControllers::ignorePhysicalDevices(){
    std::lock_guard lock(mutex);
    getState=&noPhysicalState;getCapabilities=&noPhysicalCapabilities;setState=&noPhysicalVibration;
}
DWORD NativeControllers::vibration(uint32_t slot,const XINPUT_VIBRATION& value){
    if(slot>=4)throw Failure("Native controller vibration slot is outside the qualified range0..3");
    std::lock_guard lock(mutex);auto copy=value;const auto status=setState(slot,&copy);
    if(status!=ERROR_SUCCESS&&status!=ERROR_DEVICE_NOT_CONNECTED)throw Failure("Windows controller vibration query failed: "+std::to_string(status));
    if(slot==0&&status==ERROR_DEVICE_NOT_CONNECTED&&(!keyboard.expired()||commands))return ERROR_SUCCESS;
    return status;
}
}
namespace Simpsons {
std::shared_ptr<Platform::NativeControllers> Runtime::controllerSource(){
    std::lock_guard lock(controllerMutex);
    if(!controllers) {
        controllers=std::make_shared<Platform::NativeControllers>();
        if(const char* ignore=std::getenv("SIMPSONS_IGNORE_PHYSICAL_CONTROLLERS");ignore&&*ignore&&*ignore!='0') {
            controllers->ignorePhysicalDevices();
            std::fprintf(stderr,"[NATIVE INPUT] physical controllers ignored (SIMPSONS_IGNORE_PHYSICAL_CONTROLLERS)\n");
        }
        if(window) controllers->attachKeyboard(window->keyboard);
        if(controllerCommands)controllers->attachCommands(controllerCommands);
        if(inputRecording)controllers->attachRecording(inputRecording);
        if(inputPlayback)controllers->attachPlayback(inputPlayback);
    }
    return controllers;
}
}
namespace {
using namespace Simpsons;
struct HostState {
    uint32_t csr=PPCFPSCRRegister::getcsr();DWORD error=GetLastError();
    HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~HostState(){PPCFPSCRRegister::restoreHostCSR(csr);SetLastError(error);}
};
Runtime& runtime(uint8_t* base){if(!active||base!=active->base)throw Failure("Invalid native controller runtime");active->checkRunning();return *active;}
void need(bool b,const char* why){if(!b)throw Failure(std::string("Native controller query: ")+why);}
void word(uint8_t* p,uint16_t v){p[0]=uint8_t(v>>8);p[1]=uint8_t(v);}
void gamepad(uint8_t* p,const XINPUT_GAMEPAD& g){
    word(p,g.wButtons);p[2]=g.bLeftTrigger;p[3]=g.bRightTrigger;word(p+4,uint16_t(g.sThumbLX));word(p+6,uint16_t(g.sThumbLY));
    word(p+8,uint16_t(g.sThumbRX));word(p+10,uint16_t(g.sThumbRY));
}
uint8_t* output(Runtime& rt,uint32_t p,uint32_t n){need(p&&!(p&3),"absent or unaligned output");return rt.pointer(p,n,true);}
const uint8_t* input(Runtime& rt,uint32_t p,uint32_t n){need(p&&!(p&3),"absent or unaligned input");return rt.pointer(p,n,false);}
std::string movieAuditHex(uint32_t value){char text[11];std::snprintf(text,sizeof(text),"0x%08X",value);return text;}
std::optional<uint32_t> movieAuditWord(Runtime& rt,uint64_t address) noexcept {
    try {if(address>UINT32_MAX-3)return {};const auto* p=rt.pointer(uint32_t(address),4,false);
        return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
    }catch(...){return {};}
}
// Original826B8B70 copies a RwString, appends .vp6, normalizes through a
//128-byte buffer and copies it back with82743600. This byte string stores
// pointer/uint16 length/uint16 capacity;82743600 writes its terminating NUL.
// Read only that bounded owned extent. An unknown identity never admits it.
void auditMovieBoundary(Runtime& rt,const PPCContext& ctx,uint32_t owner,bool begin) noexcept {
    if(!rt.resourceAudit.active())return;
    try {
        const auto global=movieAuditWord(rt,0x82D09750);
        const auto state=owner?movieAuditWord(rt,uint64_t(owner)+0x14):std::nullopt;
        const auto flags=owner?movieAuditWord(rt,uint64_t(owner)+0x2C):std::nullopt;
        const auto callback=owner?movieAuditWord(rt,uint64_t(owner)+0x30):std::nullopt;
        const auto decoder=movieAuditWord(rt,0x82E06F5C);
        const auto name=owner?movieAuditWord(rt,uint64_t(owner)+0x1C):std::nullopt;
        std::string asset="unknown",nameState="unknown",nameExtent="unknown";
        try {
            if(owner&&uint64_t(owner)+0x23<=UINT32_MAX){const auto* extent=rt.pointer(owner+0x20,4,false);
                const auto length=uint32_t(extent[0])<<8|extent[1],capacity=uint32_t(extent[2])<<8|extent[3];
                nameExtent=std::to_string(length)+"/"+std::to_string(capacity);
                if(name&&*name&&length&&length<128&&capacity>length&&uint64_t(*name)+length<=UINT32_MAX){
                    const auto* bytes=rt.pointer(*name,length+1,false);bool ascii=!bytes[length];
                    for(uint32_t i=0;i<length;++i)ascii=ascii&&bytes[i]>=32&&bytes[i]<127;
                    if(ascii){asset.assign(reinterpret_cast<const char*>(bytes),length);nameState="bounded-original-string";}
                }
            }
        }catch(...){}
        const auto frontend=movieAuditWord(rt,0x82D08C94);
        const auto index=frontend&&*frontend?movieAuditWord(rt,uint64_t(*frontend)+0x80):std::nullopt;
        const auto selector=movieAuditWord(rt,0x82D08C98),pending=movieAuditWord(rt,0x82D08C9C);
        const auto decimal=[](const std::optional<uint32_t>& value){return value?std::to_string(*value):"unknown";};
        const auto hex=[](const std::optional<uint32_t>& value){return value?movieAuditHex(*value):"unknown";};
        const auto presence=[](const std::optional<uint32_t>& value){return value?(*value?"present":"absent"):"unknown";};
        // Successful-start hook926C is inside91E0's112-byte stack frame.
        // __savegprlr29 saved its original caller at oldSP-8=currentSP+104.
        const auto creator=begin?movieAuditWord(rt,uint64_t(ctx.r1.u32)+104):std::optional<uint32_t>(uint32_t(ctx.lr));
        const auto caller=creator.value_or(0);
        const std::string phase=begin?"input-ready":"decoder-stop-request";
        const std::string parameters="phase="+phase+" state="+decimal(state)+" flags="+hex(flags)+
            " frontendSelector="+decimal(selector)+" frontendPending="+decimal(pending)+" frontendMovieIndex="+decimal(index);
        const std::string ownership="movieGlobalMatches="+std::string(global?(*global==owner?"true":"false"):"unknown")+
            " filename="+nameState+" callback="+presence(callback)+" decoder="+presence(decoder)+
            " caller="+(creator?"original-boundary":"unknown");
        const std::string instance="owner="+movieAuditHex(owner)+" movieGlobal="+hex(global)+" name="+hex(name)+
            " nameLengthCapacity="+nameExtent+" callback="+hex(callback)+" decoder="+hex(decoder)+
            " frontendOwner="+hex(frontend)+" entryLR="+movieAuditHex(uint32_t(ctx.lr))+" sp="+movieAuditHex(ctx.r1.u32);
        rt.resourceAudit.lifecycle("movie-lifetime",asset,caller,parameters,ownership,0,instance);
        std::fprintf(stderr,"[NATIVE MOVIE AUDIT] asset=%s caller=%08X %s; %s; %s\n",
            asset.c_str(),caller,parameters.c_str(),ownership.c_str(),instance.c_str());
    }catch(...){std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] original movie boundary snapshot failed\n");}
}
}
namespace {
void auditAcceptedMovieStart(Simpsons::Runtime& rt,const PPCContext& ctx,uint32_t slot,
        const Simpsons::Platform::NativeMovieStartObservation& observed,const XINPUT_STATE& returned) noexcept {
    try {
        const auto& pad=observed.raw.Gamepad;char action[256],parameters[384],asset[32],instance[128];
        const int a=std::snprintf(action,sizeof(action),
            "controller buttons=%04X lt=%u rt=%u left=(%d,%d) right=(%d,%d) movie_start=accepted slot=%u caller=%08X",
            unsigned(pad.wButtons),unsigned(pad.bLeftTrigger),unsigned(pad.bRightTrigger),
            int(pad.sThumbLX),int(pad.sThumbLY),int(pad.sThumbRX),int(pad.sThumbRY),slot,uint32_t(ctx.lr));
        const int p=std::snprintf(parameters,sizeof(parameters),
            "boundary=fresh-start-before-movie-mask slot=%u raw_buttons=%04X raw_lt=%u raw_rt=%u raw_left=(%d,%d) raw_right=(%d,%d) returned_buttons=%04X",
            slot,unsigned(pad.wButtons),unsigned(pad.bLeftTrigger),unsigned(pad.bRightTrigger),
            int(pad.sThumbLX),int(pad.sThumbLY),int(pad.sThumbRX),int(pad.sThumbRY),unsigned(returned.Gamepad.wButtons));
        const int id=std::snprintf(asset,sizeof(asset),"controller:slot:%u",slot);
        const int i=std::snprintf(instance,sizeof(instance),"movieOwner=%08X raw_packet=%lu returned_packet=%lu",
            observed.movieOwner,observed.raw.dwPacketNumber,returned.dwPacketNumber);
        if(a<=0||size_t(a)>=sizeof(action)||p<=0||size_t(p)>=sizeof(parameters)||id<=0||size_t(id)>=sizeof(asset)||i<=0||size_t(i)>=sizeof(instance)){
            std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] accepted movie controller action format failed\n");return;
        }
        rt.resourceAudit.action(action);
        rt.resourceAudit.lifecycle("controller-movie-start",asset,uint32_t(ctx.lr),parameters,
            currentContext==&ctx?"runtime=match source=runtime-controller movie-input=owned context=match":
                                "runtime=match source=runtime-controller movie-input=owned context=unknown",0,instance);
    }catch(...){std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] accepted movie controller action capture failed\n");}
}
}
PPC_FUNC(__imp__XamInputGetState){
    HostState host;auto& rt=runtime(base);const auto slot=ctx.r3.u32,flags=ctx.r4.u32,out=ctx.r5.u32;
    need(slot<4&&!flags,"unsupported slot or device flags");uint8_t* destination=out?output(rt,out,16):nullptr;
    if(!destination) {ctx.r3.u64=rt.controllerSource()->connectionStatus(slot);return;}
    const bool auditing=rt.resourceAudit.active();Platform::NativeMovieStartObservation observed{};
    XINPUT_STATE state{};const auto status=rt.controllerSource()->state(slot,state,auditing?&observed:nullptr);std::array<uint8_t,16> record{};
    if(status==ERROR_SUCCESS&&auditing&&observed.accepted) auditAcceptedMovieStart(rt,ctx,slot,observed,state);
    if(slot==0&&status==ERROR_SUCCESS&&auditing&&!observed.accepted) {
        const auto& pad=state.Gamepad;
        if(pad.wButtons||pad.bLeftTrigger||pad.bRightTrigger||pad.sThumbLX||pad.sThumbLY||pad.sThumbRX||pad.sThumbRY) {
            char action[160];
            std::snprintf(action,sizeof(action),"controller buttons=%04X lt=%u rt=%u left=(%d,%d) right=(%d,%d)",
                unsigned(pad.wButtons),unsigned(pad.bLeftTrigger),unsigned(pad.bRightTrigger),
                int(pad.sThumbLX),int(pad.sThumbLY),int(pad.sThumbRX),int(pad.sThumbRY));
            rt.resourceAudit.action(action);
        }
    }
    if(status==ERROR_SUCCESS){record[0]=uint8_t(state.dwPacketNumber>>24);record[1]=uint8_t(state.dwPacketNumber>>16);
        record[2]=uint8_t(state.dwPacketNumber>>8);record[3]=uint8_t(state.dwPacketNumber);gamepad(record.data()+4,state.Gamepad);}
    // Original ABI zeroes disconnected output. A null state is a status query.
    // Preserve raw buttons/triggers/sticks: original823210B0 applies its own
    // button mapping and signed integer normalization, with no native deadzone.
    if(destination)std::memcpy(destination,record.data(),record.size());ctx.r3.u64=status;
}
PPC_FUNC(__imp__XamInputGetCapabilities){
    HostState host;auto& rt=runtime(base);const auto slot=ctx.r3.u32,flags=ctx.r4.u32,out=ctx.r5.u32;
    need(slot<4&&flags<=1,"unsupported slot or capability flags");auto* destination=output(rt,out,20);
    XINPUT_CAPABILITIES caps{};const auto status=rt.controllerSource()->capabilities(slot,flags,caps);std::array<uint8_t,20> record{};
    if(status==ERROR_SUCCESS){record[0]=caps.Type;record[1]=caps.SubType;word(record.data()+2,caps.Flags);gamepad(record.data()+4,caps.Gamepad);
        word(record.data()+16,caps.Vibration.wLeftMotorSpeed);word(record.data()+18,caps.Vibration.wRightMotorSpeed);}
    std::memcpy(destination,record.data(),record.size());ctx.r3.u64=status;
}
PPC_FUNC(__imp__XamInputSetState){
    HostState host;auto& rt=runtime(base);const auto slot=ctx.r3.u32,flags=ctx.r4.u32,in=ctx.r5.u32;
    need(slot<4&&!flags&&!ctx.r6.u64&&!ctx.r7.u64&&!ctx.r8.u64,"unsupported vibration slot, flags or reserved arguments");
    const auto* source=input(rt,in,4);XINPUT_VIBRATION value{};
    value.wLeftMotorSpeed=uint16_t(source[0]<<8|source[1]);value.wRightMotorSpeed=uint16_t(source[2]<<8|source[3]);
    ctx.r3.u64=rt.controllerSource()->vibration(slot,value);
}

// Explicit native skip policy for the original shared movie player. These
// hooks retain the decoder's real stop, worker exit wait, destructors and
// completion event. No end-of-file/thread status or game progress is forged.
void SimpsonsNativeMovieInputBegin(PPCContext& ctx,uint8_t* base){
    HostState host;auto& rt=runtime(base);const auto owner=ctx.r31.u32;
    auditMovieBoundary(rt,ctx,owner,true);
    need(owner>=0x10000 && owner<=UINT32_MAX-0x1C,"movie input owner address out of range");
    need(owner==PPC_LOAD_U32(0x82D09750)&&PPC_LOAD_U32(owner+0x14)==2,
         "movie input began outside the original successful start");
    rt.controllerSource()->beginMovie(owner);
    const auto name=PPC_LOAD_U32(owner+0x1C);
    std::fprintf(stderr,"[NATIVE MOVIE INPUT] ready owner=%08X name=%08X; fresh Start/Enter skips\n",owner,name);
}
void SimpsonsNativeMovieInputEnd(PPCContext& ctx,uint8_t* base){
    HostState host;auto& rt=runtime(base);
    //8282D998 entry precedes the original decoder stop/join. It is a stop
    // request receipt; the original completion and filename release follow.
    if(rt.resourceAudit.active()){
        const auto owner=movieAuditWord(rt,0x82D09750);
        auditMovieBoundary(rt,ctx,owner.value_or(0),false);
    }
    rt.controllerSource()->endMovie();
}
bool SimpsonsNativeMovieSkip(PPCContext& ctx,uint8_t* base){
    HostState host;auto& rt=runtime(base);const auto owner=ctx.r3.u32;
    need(owner>=0x10000 && owner<=UINT32_MAX-0x1C,"movie skip owner address out of range");
    need(owner==PPC_LOAD_U32(0x82D09750),"foreign movie update owner");
    const auto state=PPC_LOAD_U32(owner+0x14);
    const bool shortcutSkip=rt.renderTestFirstMission||
        Simpsons::auditStageSkipBootstrap(base)||
        (rt.bartmanBegins&&!rt.bartmanBeginsMapStarted)||Simpsons::firstMissionCompletionSkipBootstrap(base);
    if((state!=2&&state!=3&&state!=6)||
       (!shortcutSkip&&!rt.controllerSource()->takeMovieSkip(owner)))return false;
    std::fprintf(stderr,"[NATIVE MOVIE SKIP] %s owner=%08X; original stop and completion begin\n",
        shortcutSkip?"startup shortcut":"Start accepted",owner);
    EngineCpuCalls cpu(ctx,base);
    cpu.invoke(0x826B9290,owner);
    need(PPC_LOAD_U32(owner+0x14)==4&&!PPC_LOAD_U32(0x82E06F5C),
         "original movie stop did not release its decoder");
    cpu.invoke(0x826B8AD8,owner);
    std::fprintf(stderr,"[NATIVE MOVIE SKIP] original decoder released and completion dispatched owner=%08X\n",owner);
    return true;
}
