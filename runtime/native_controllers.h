#pragma once
#include <windows.h>
#include <xinput.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <memory>
#include <filesystem>
#include <deque>
#include <string>
#include <vector>
#include <type_traits>
#include <optional>
#include "native_mouse_input.h"
#include "native_control_settings.h"

namespace Simpsons::Platform {
class NativeInputRecording;
// Replays the exact returned XInput polls from a version-one input recording.
// It activates at the first slot-zero poll after the requested scene count.
class NativeInputPlayback {
    struct Poll {uint32_t slot;DWORD status;XINPUT_STATE state;NativeMouseMotion mouse;};
    std::vector<Poll> polls;
    const std::atomic<uint64_t>& scene;
    uint64_t startScene;
    size_t cursor=0;
    bool started=false,ended=false,continueLive=false,neutralPrefix=true;
    uint64_t expectedEndScene=0;
    XINPUT_STATE previous{};
public:
    NativeInputPlayback(const std::filesystem::path& path,uint64_t startScene,const std::atomic<uint64_t>& scene,
        bool resumeLive=false,uint64_t checkpointScene=0);
    bool ready(uint32_t slot) const;
    bool active() const{return started;}
    bool handoffReady(uint32_t slot) const;
    DWORD sample(uint32_t slot,XINPUT_STATE& result,NativeMouseMotion* mouse=nullptr);
    size_t count() const{return polls.size();}
};
// Explicitly enabled local command stream. Commands are button/stick taps consumed
// by the ordinary controller query, never guest-memory or game-state writes.
class NativeCommandInput {
    friend struct NativeCommandInputTestAccess;
    struct Command { XINPUT_GAMEPAD gamepad{}; uint32_t holdMs{}; bool pad{}; };
    HANDLE file=INVALID_HANDLE_VALUE;
    std::deque<Command> queued;
    decltype(&GetTickCount64) clock=&GetTickCount64;
    ULONGLONG holdUntil{};
    XINPUT_GAMEPAD held{};
    XINPUT_GAMEPAD releasePad{};
    bool releaseWasPad=false;
    std::string line;
    bool overlong=false,releaseNext=false,holding=false;
public:
    explicit NativeCommandInput(const std::filesystem::path& path);
    ~NativeCommandInput();
    NativeCommandInput(const NativeCommandInput&)=delete;
    NativeCommandInput& operator=(const NativeCommandInput&)=delete;
    XINPUT_GAMEPAD sample(); // Called only under NativeControllers' mutex.
    void discard();
};
// Input from the owned game window only. Short presses survive until the next
// game poll; focus loss releases both held and pending input.
struct NativeMenuPointer {
    bool active=false,moved=false;
    int32_t x=0,y=0,wheel=0;
    uint32_t width=0,height=0;
    uint8_t pressed=0;
};
enum class NativeRebindStatus { Bound,Cleared,Cancelled,Rejected };
struct NativeRebindResult {
    ControlAction action;
    uint32_t slot;
    NativeRebindStatus status;
    uint32_t code;
};
class NativeKeyboard {
    std::mutex mutex;
    // Track each physical key independently: releasing an alias must not release
    // another key or mouse button which supplies the same game action.
    std::array<bool,256> held{},pressed{};
    std::array<bool,256> physicalHeld{},blocked{};
    NativeControlSettings controlSettings{};
    bool capturing=false;
    ControlAction captureAction=ControlAction::MoveForward;
    uint32_t captureSlot=0,captureRelease=0;
    std::optional<NativeRebindResult> captureResult;
    std::deque<NativeRebindResult> rebindResults;
    bool captureInput(uint32_t code,bool down,bool wasHeld);
    uint8_t mouseHeld=0,mousePressed=0;
    int32_t mouseX=0,mouseY=0;
    bool mouseCaptured=false;
    uint64_t mouseGeneration=1;
    XINPUT_GAMEPAD last{};
    DWORD packet=1;
    bool focused=false;
    bool movieEnterHeld=false;
    bool menuMouse=false,pointerInside=false;
    NativeMenuPointer pointer{};
    NativeMenuPointer pressPosition{};
    uint8_t pointerHeld=0;
public:
    static constexpr WORD buttons=XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B|
        XINPUT_GAMEPAD_X|XINPUT_GAMEPAD_Y|
        XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER|
        XINPUT_GAMEPAD_LEFT_THUMB|XINPUT_GAMEPAD_RIGHT_THUMB|
        XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_DOWN|XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_RIGHT;
    void focus(bool active);
    bool key(uint32_t code,bool down);
    void captureMouse(bool active);
    bool mouseButton(uint32_t code,bool down);
    NativeControlSettings controls();
    void configureControls(const NativeControlSettings& settings);
    void beginRebind(ControlAction action,uint32_t slot);
    void cancelRebind();
    bool rebindActive();
    std::optional<NativeRebindResult> takeRebindResult();
    void mouseMotion(int32_t x,int32_t y);
    bool mouseCaptureActive(uint64_t generation);
    void menuMode(bool active);
    void pointerMove(int32_t x,int32_t y,uint32_t width,uint32_t height);
    void pointerLeave();
    void pointerWheel(int32_t delta);
    NativeMenuPointer menuPointer();
    XINPUT_STATE sample(bool movie=false,WORD* directionPresses=nullptr,NativeMouseMotion* motion=nullptr,uint64_t* generation=nullptr,WORD* directionHeld=nullptr);
    void discard(bool pointerEvents=true);
};
// Stack-owned receipt for this poll's exact fresh movie Start acceptance.
// Recording and returned controller state keep their existing masked values.
struct NativeMovieStartObservation {
    XINPUT_STATE raw;
    uint32_t movieOwner;
    bool accepted;
};
// Snapshot of the keyboard movement delivered by the ordinary slot-zero poll.
// Apt reads it without consuming another input sample; gameplay keeps its stick.
struct NativeKeyboardNavigation {
    bool active=false;
    WORD pressed=0,held=0;
};
static_assert(std::is_trivial_v<NativeMovieStartObservation> &&
              std::is_standard_layout_v<NativeMovieStartObservation>);
// Owned native Windows query source. No console device or guest memory lives
// here. The friend fixture substitutes the OS query only, before publication.
class NativeControllers {
    friend struct NativeControllersTestAccess;
    decltype(&XInputGetState) getState=&XInputGetState;
    decltype(&XInputGetCapabilities) getCapabilities=&XInputGetCapabilities;
    decltype(&XInputSetState) setState=&XInputSetState;
    std::mutex mutex;
    std::array<DWORD,4> previous={0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF};
    std::weak_ptr<NativeKeyboard> keyboard;
    std::atomic<bool> keyboardMouseSource=false;
    std::shared_ptr<NativeCommandInput> commands;
    std::shared_ptr<NativeInputRecording> recording;
    std::shared_ptr<NativeInputPlayback> playback;
    XINPUT_GAMEPAD fallbackLast{};
    DWORD fallbackPacket=1;
    NativeKeyboardNavigation navigation{};
    NativeMouseMotion mouse{};
    uint64_t mouseCaptureGeneration=0;
    bool mousePlayback=false;
    WORD navigationPrevious=0;
    DWORD stateUnlocked(uint32_t slot,XINPUT_STATE& result);
    // XInputGetState on an empty slot enumerates devices: it costs 0.1 ms to several ms, in
    // bursts, for each of the three empty slots the game polls every frame. A slot that
    // just reported "not connected" is queried again only after disconnectedPollMs (0
    // restores a query per call; the test fixture binds 0), so hot-plug detection waits
    // at most that long and the frame thread stops stalling on device enumeration.
    uint32_t disconnectedPollMs=250;
    std::array<uint64_t,4> disconnectedAt{};
    DWORD queryState(uint32_t slot,XINPUT_STATE* result);
    uint64_t modalToken=0,nextModalToken=1;
    uint32_t modalSlot=0;
    bool modalArmed=false;
    std::array<bool,4> releaseAfterModal{};
    uint32_t movieOwner=0;
    bool movieSkipPending=false;
    std::array<bool,4> movieArmed{},releaseMovieStart{};
public:
    void attachKeyboard(const std::shared_ptr<NativeKeyboard>& source);
    void attachCommands(const std::shared_ptr<NativeCommandInput>& source);
    // Opt-in (SIMPSONS_IGNORE_PHYSICAL_CONTROLLERS=1, set by the measurement tools): physical
    // XInput devices read as disconnected and receive no vibration, so a controller in use
    // elsewhere on the machine can neither steer nor rumble a background run. Scripted
    // command input and the keyboard fallback are unaffected.
    void ignorePhysicalDevices();
    void attachRecording(const std::shared_ptr<NativeInputRecording>& source);
    void attachPlayback(const std::shared_ptr<NativeInputPlayback>& source);
    bool usesKeyboardMouse() const noexcept {return keyboardMouseSource.load(std::memory_order_relaxed);}
    NativeKeyboardNavigation keyboardNavigation();
    NativeMenuPointer menuPointer();
    // The camera consumes this poll's raw displacement once. Subsequent camera
    // updates remain native/neutral, so controller easing cannot create a tail.
    NativeMouseMotion takeMouseMotion();
    // Connection-only probes must not consume pending buttons or mouse motion.
    DWORD connectionStatus(uint32_t slot);
    DWORD state(uint32_t slot,XINPUT_STATE& result,NativeMovieStartObservation* observation=nullptr);
    DWORD capabilities(uint32_t slot,uint32_t flags,XINPUT_CAPABILITIES& result);
    DWORD vibration(uint32_t slot,const XINPUT_VIBRATION& value);
    // Exclusive native platform UI input. Game queries remain connected but
    // neutral; only this token consumes the selected controller/command stream.
    uint64_t beginModal(uint32_t slot);
    DWORD modalState(uint64_t token,XINPUT_STATE& result);
    void endModal(uint64_t token);
    // Observe ordinary game polls only. A fresh Start during this movie is
    // consumed once; holding it cannot act on the following menu/movie.
    void beginMovie(uint32_t owner);
    bool takeMovieSkip(uint32_t owner);
    void endMovie();
};
}
