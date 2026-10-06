#include "runtime/native_window.h"
#include "runtime/native_controllers.h"
#include "runtime/runtime.h"
#include <cstdlib>
#include <cstdio>
#include <string>

namespace Simpsons {
// The test procedure runs on the window thread. It seeds only the suspended
// capture intent, never real cursor capture or desktop input.
struct NativeMouseCaptureTestAccess {
    static void intendCapture(NativeWindow& window) {window.resumeMouseAfterMenu=true;}
    static bool intended(const NativeWindow& window) {return window.resumeMouseAfterMenu;}
};
}
namespace {
constexpr UINT testMessage=WM_APP+90,menuMessage=WM_APP+18;
Simpsons::NativeWindow* testedWindow=nullptr;
WNDPROC nativeProcedure=nullptr;
WNDPROC closeNativeProcedure=nullptr;
Simpsons::Runtime* closingRuntime=nullptr;
std::atomic<uint32_t> closeObservation=0;
HANDLE queueFinished=nullptr;
bool observeQueue=false;
std::atomic<uint32_t> queuedTransitions=0,queuedPayloads=0,queuedMenuStates=0;
HWND ownedWindow=nullptr,lastForeground=nullptr;
unsigned externalForegroundChanges=0;
int failures=0,checks=0;
void check(bool result,const char* name) {
    ++checks;if(!result){std::fprintf(stderr,"FAIL: %s\n",name);++failures;}
    const auto foreground=GetForegroundWindow();
    ++checks;
    if(foreground==ownedWindow) {
        std::fprintf(stderr,"FAIL: owned background HWND became foreground at %s\n",name);++failures;
    } else if(foreground!=lastForeground) {
        ++externalForegroundChanges;
        std::printf("External foreground change at %s: %p -> %p; owned HWND=%p\n",name,
            static_cast<void*>(lastForeground),static_cast<void*>(foreground),static_cast<void*>(ownedWindow));
    }
    lastForeground=foreground;
}
LRESULT CALLBACK testProcedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if(message==testMessage) {
        if(wparam==1)Simpsons::NativeMouseCaptureTestAccess::intendCapture(*testedWindow);
        if(wparam==2) {
            // Both transitions happen while the UI thread handles this sent
            // message, so their posted callbacks cannot run between them.
            observeQueue=true;
            testedWindow->setMenuMouse(true);
            testedWindow->setMenuMouse(false);
            PostMessageW(window,testMessage,3,0);
        }
        if(wparam==3) {observeQueue=false;SetEvent(queueFinished);}
        return Simpsons::NativeMouseCaptureTestAccess::intended(*testedWindow)?1:0;
    }
    if(message==menuMessage&&observeQueue) {
        const auto index=queuedTransitions.fetch_add(1);
        if(index<32) {
            if(wparam)queuedPayloads.fetch_or(1u<<index);
            if(testedWindow->isMenuMouse())queuedMenuStates.fetch_or(1u<<index);
        }
    }
    return CallWindowProcW(nativeProcedure,window,message,wparam,lparam);
}
LRESULT CALLBACK closeProcedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if(message==WM_DESTROY&&closingRuntime) {
        // Observe entry into destruction before the native WM_DESTROY handler
        // can publish cancellation. This catches cancellation left too late.
        uint32_t observed=1;
        if(closingRuntime->stopping.load(std::memory_order_acquire))observed|=2;
        if(closingRuntime->window->closed.load())observed|=4;
        if(PPCStopRequested&&WaitForSingleObject(closingRuntime->stopEvent,0)==WAIT_OBJECT_0)observed|=8;
        closeObservation=observed;
    }
    return CallWindowProcW(closeNativeProcedure,window,message,wparam,lparam);
}
void exerciseClose(UINT message,WPARAM command) {
    Simpsons::Runtime runtime;
    runtime.window=std::make_unique<Simpsons::NativeWindow>();
    const auto hwnd=runtime.window->handle();ownedWindow=hwnd;
    check((GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_NOACTIVATE)!=0,"close fixture cannot activate the desktop");
    closingRuntime=&runtime;closeObservation=0;
    closeNativeProcedure=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(closeProcedure)));
    check(closeNativeProcedure!=nullptr,"close fixture installs its destruction observer");
    if(closeNativeProcedure) {
        SendMessageW(hwnd,message,command,0);
        check(closeObservation.load()==15,"accepted close cancels guest entries and waits before HWND destruction");
        check(runtime.window->closed&&!runtime.window->handle()&&!IsWindow(hwnd),"accepted close completes native window destruction");
        bool cancelled=false;
        try{runtime.checkRunning();}catch(const Simpsons::Failure& error){cancelled=std::string(error.what())=="Native window closed";}
        check(cancelled,"accepted close retains its explicit cancellation reason");
    }
    runtime.window.reset();closingRuntime=nullptr;closeNativeProcedure=nullptr;
}
class BackgroundEnvironment {
    std::string previous;
    bool configured=false;
public:
    BackgroundEnvironment() {
        if(const auto value=std::getenv("SIMPSONS_BACKGROUND_WINDOW"))previous=value;
        // NativeWindow reads the CRT environment. A Win32-only setter leaves
        // that snapshot unchanged and can accidentally create a normal window.
        configured=_putenv_s("SIMPSONS_BACKGROUND_WINDOW","1")==0;
    }
    bool ready() const {
        const auto value=std::getenv("SIMPSONS_BACKGROUND_WINDOW");
        return configured&&value&&std::string(value)=="1";
    }
    ~BackgroundEnvironment() {
        if(configured&&_putenv_s("SIMPSONS_BACKGROUND_WINDOW",previous.c_str())!=0)
            std::fprintf(stderr,"Cannot restore background fixture environment\n");
    }
};
}
int main() {
    BackgroundEnvironment background;
    if(!background.ready()) {
        std::fprintf(stderr,"Cannot configure CRT background fixture environment\n");return 1;
    }
    lastForeground=GetForegroundWindow();
    Simpsons::NativeWindow window;testedWindow=&window;
    const auto hwnd=window.handle();ownedWindow=hwnd;
    EnableWindow(hwnd,FALSE);
    const bool noActivate=(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_NOACTIVATE)!=0;
    check(noActivate,
        "background fixture retains its no-activation window style");
    if(!noActivate)return 1;
    nativeProcedure=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd,GWLP_WNDPROC));
    if(!nativeProcedure||!SetWindowLongPtrW(hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(testProcedure))) {
        std::fprintf(stderr,"Cannot install test window procedure\n");return 1;
    }
    const auto intended=[&] {return SendMessageW(hwnd,testMessage,0,0)!=0;};
    const auto seed=[&] {SendMessageW(hwnd,testMessage,1,0);};
    const auto menu=[&](bool active) {
        window.setMenuMouse(active);
        // Also dispatch synchronously so assertions observe the menu transition.
        SendMessageW(hwnd,menuMessage,active?1:0,0);
    };
    const auto noCapture=[&] {
        GUITHREADINFO info{};info.cbSize=sizeof(info);
        return GetGUIThreadInfo(GetWindowThreadProcessId(hwnd,nullptr),&info)&&info.hwndCapture!=hwnd;
    };
    SendMessageW(hwnd,WM_SETFOCUS,0,0);
    check(!intended(),"new gameplay window begins without capture intent");
    queueFinished=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!queueFinished) {
        SetWindowLongPtrW(hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(nativeProcedure));
        std::fprintf(stderr,"Cannot create queued menu test event\n");return 1;
    }
    SendMessageW(hwnd,testMessage,2,0);
    check(WaitForSingleObject(queueFinished,5000)==WAIT_OBJECT_0,"queued menu transitions finish on the window thread");
    check(queuedTransitions.load()==2&&queuedPayloads.load()==1&&queuedMenuStates.load()==0,
        "queued menu entry must retain its release event when gameplay resumes before dispatch");
    check(!window.isMenuMouse()&&!intended()&&noCapture(),"queued menu transitions keep a background window noncapturing");
    CloseHandle(queueFinished);queueFinished=nullptr;
    menu(true);menu(false);
    check(!intended()&&noCapture(),"menu navigation without prior capture cannot capture the cursor");
    seed();SendMessageW(hwnd,WM_KEYDOWN,VK_ESCAPE,0);
    check(intended(),"Escape must preserve prior gameplay capture intent for pause");
    check(window.keyboard->sample().Gamepad.wButtons==XINPUT_GAMEPAD_START,"Escape still supplies pause to gameplay");
    SendMessageW(hwnd,WM_KEYUP,VK_ESCAPE,0);
    menu(true);
    check(intended()&&noCapture(),"pause keeps capture intent while its menu exposes the cursor");
    SendMessageW(hwnd,WM_KEYDOWN,VK_ESCAPE,0);
    check(window.keyboard->sample().Gamepad.wButtons==XINPUT_GAMEPAD_B,"Escape supplies fixed Back while a native menu is active");
    SendMessageW(hwnd,WM_KEYUP,VK_ESCAPE,0);
    check(!window.keyboard->sample().Gamepad.wButtons,"native menu Escape releases its Back action");
    SendMessageW(hwnd,WM_CAPTURECHANGED,0,0);
    check(intended(),"intentional release notification cannot cancel suspended capture");
    menu(false);
    check(intended()&&noCapture(),"background resume retains intent without acquiring desktop capture");
    menu(true);SendMessageW(hwnd,WM_KEYDOWN,VK_F6,0);menu(false);
    check(!intended()&&noCapture(),"F6 during pause cancels automatic capture on resume");
    seed();SendMessageW(hwnd,WM_KEYDOWN,VK_F6,LPARAM(1)<<30);
    check(intended(),"F6 autorepeat cannot toggle suspended capture");
    SendMessageW(hwnd,WM_KEYDOWN,VK_F6,0);
    check(!intended(),"fresh F6 releases prior gameplay capture intent");
    seed();SendMessageW(hwnd,WM_KILLFOCUS,0,0);SendMessageW(hwnd,WM_SETFOCUS,0,0);menu(true);menu(false);
    check(!intended()&&noCapture(),"focus loss cancels capture across later focus and menu changes");
    seed();SendMessageW(hwnd,WM_CANCELMODE,0,0);
    check(!intended(),"cancel mode cancels automatic capture");
    seed();SendMessageW(hwnd,WM_ENTERSIZEMOVE,0,0);
    check(!intended(),"window move or resize cancels automatic capture");
    seed();window.configureVideo(window.presentationWidth.load(),window.presentationHeight.load(),false);
    check(intended(),"unchanged video output preserves gameplay capture intent");
    window.configureVideo(960,540,false);
    check(!intended()&&noCapture(),"actual video output change cancels automatic capture");
    SendMessageW(hwnd,WM_KEYDOWN,VK_F6,0);
    check(!intended()&&noCapture(),"disabled background window rejects explicit capture requests");
    SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(20,20));
    check(!intended()&&noCapture(),"background click cannot enable automatic capture");
    check(window.keyboard->sample().Gamepad.wButtons==XINPUT_GAMEPAD_X,"focused background click still exercises game button delivery");
    SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(20,20));
    window.keyboard->sample();
    window.keyboard->beginRebind(Simpsons::ControlAction::Jump,0);seed();
    for(const UINT code:{VK_F6,VK_F8,VK_F9}) {
        SendMessageW(hwnd,WM_KEYDOWN,code,0);
        const auto result=window.keyboard->takeRebindResult();
        check(result&&result->status==Simpsons::Platform::NativeRebindStatus::Rejected&&result->code==code&&window.keyboard->rebindActive(),
            "reserved window hotkey is rejected while binding capture remains active");
        check(intended()&&noCapture()&&!window.keyboard->sample().Gamepad.wButtons,
            "reserved capture input preserves cursor intent and delivers no game buttons");
        SendMessageW(hwnd,WM_KEYUP,code,0);
    }
    const LPARAM rightAlt=(LPARAM(1)<<24)|(LPARAM(0x38)<<16);
    SendMessageW(hwnd,WM_SYSKEYDOWN,VK_MENU,rightAlt);
    check(window.keyboard->rebindActive()&&!window.keyboard->takeRebindResult()&&!window.keyboard->sample().Gamepad.wButtons,
        "extended Alt capture waits for release without gameplay input");
    SendMessageW(hwnd,WM_SYSKEYUP,VK_MENU,rightAlt);
    auto rebound=window.keyboard->takeRebindResult();
    check(rebound&&rebound->status==Simpsons::Platform::NativeRebindStatus::Bound&&rebound->code==VK_RMENU,
        "system key messages retain the physical right Alt binding");
    SendMessageW(hwnd,WM_SYSKEYDOWN,VK_MENU,rightAlt);
    check(window.keyboard->sample().Gamepad.wButtons==XINPUT_GAMEPAD_A,"fresh right Alt message uses its rebound gameplay action");
    SendMessageW(hwnd,WM_SYSKEYUP,VK_MENU,rightAlt);window.keyboard->sample();
    window.keyboard->beginRebind(Simpsons::ControlAction::Attack,1);
    check(SendMessageW(hwnd,WM_XBUTTONDOWN,MAKEWPARAM(MK_XBUTTON2,XBUTTON2),MAKELPARAM(20,20))==TRUE,
        "handled XBUTTONDOWN reports success to the window manager");
    check(window.keyboard->rebindActive()&&!window.keyboard->sample().Gamepad.wButtons&&intended()&&noCapture(),
        "extra mouse binding capture suppresses clicks without acquiring the cursor");
    check(SendMessageW(hwnd,WM_XBUTTONUP,MAKEWPARAM(0,XBUTTON2),MAKELPARAM(20,20))==TRUE,
        "handled XBUTTONUP reports success to the window manager");
    rebound=window.keyboard->takeRebindResult();
    check(rebound&&rebound->status==Simpsons::Platform::NativeRebindStatus::Bound&&rebound->code==VK_XBUTTON2,
        "Mouse 5 window messages finish the requested secondary binding");
    SendMessageW(hwnd,WM_XBUTTONDOWN,MAKEWPARAM(MK_XBUTTON2,XBUTTON2),MAKELPARAM(20,20));
    check(window.keyboard->sample().Gamepad.wButtons==XINPUT_GAMEPAD_X,"fresh Mouse 5 message delivers its rebound gameplay action");
    SendMessageW(hwnd,WM_XBUTTONUP,MAKEWPARAM(0,XBUTTON2),MAKELPARAM(20,20));window.keyboard->sample();
    const auto beforeFocus=window.keyboard->controls();window.keyboard->beginRebind(Simpsons::ControlAction::Jump,0);
    SendMessageW(hwnd,WM_KEYDOWN,'V',0);SendMessageW(hwnd,WM_KILLFOCUS,0,0);
    rebound=window.keyboard->takeRebindResult();
    check(rebound&&rebound->status==Simpsons::Platform::NativeRebindStatus::Cancelled&&!window.keyboard->rebindActive()&&window.keyboard->controls()==beforeFocus,
        "window focus loss cancels an unreleased capture without changing bindings");
    SendMessageW(hwnd,WM_SETFOCUS,0,0);SendMessageW(hwnd,WM_KEYUP,'V',0);
    check(noCapture(),"capture lifecycle test ends without owned cursor capture");
    SetWindowLongPtrW(hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(nativeProcedure));
    exerciseClose(WM_CLOSE,0);
    // The default Alt+F4 handler dispatches SC_CLOSE through this same route.
    exerciseClose(WM_SYSCOMMAND,SC_CLOSE);
    std::printf("Native mouse capture policy: %d checks; external foreground changes=%u; physical foreground capture was not attempted\n",
        checks,externalForegroundChanges);
    return failures?1:0;
}
