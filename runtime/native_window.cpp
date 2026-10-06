#include "native_window.h"
#include "runtime.h"
#include "stall_profiler.h"
#include "native_controllers.h"
#include "native_input_recording.h"
#include <bit>
#include <cwchar>
#include <cstdio>
#include <cstdlib>

namespace Simpsons {
namespace {
constexpr UINT_PTR fpsTimerId=1;
constexpr UINT videoMessage=WM_APP+17;
constexpr UINT menuMouseMessage=WM_APP+18;
constexpr wchar_t windowTitle[]=L"The Simpsons Game - Native development";
}
void NativeWindow::setMenuMouse(bool active) {
    if(menuMouse.load(std::memory_order_relaxed)==active)return;
    keyboard->menuMode(active);
    menuMouse.store(active,std::memory_order_relaxed);
    // Preserve each transition even if a menu opens and closes before the
    // window thread dispatches it. Entering must release its existing capture.
    if(const auto window=handle())PostMessageW(window,menuMouseMessage,active?1:0,0);
}
void NativeWindow::configureVideo(uint32_t outputWidth,uint32_t outputHeight,bool fullscreen) {
    const HWND window=handle();
    StallProfiler::Scope messageProfile(StallProfiler::Section::Wait,"NativeWindow::configureVideo.SendMessage",nullptr,reinterpret_cast<uintptr_t>(window));
    if(!SendMessageW(window,videoMessage,WPARAM(outputWidth)|(WPARAM(outputHeight)<<16),fullscreen?1:0))
        throw Failure("Native video window change failed");
}
NativeWindow::NativeWindow(std::shared_ptr<Platform::NativeInputRecording> recording):keyboard(std::make_shared<Platform::NativeKeyboard>()),inputRecording(std::move(recording)) {
    ui=std::thread([this]{run();});
    std::unique_lock lock(mutex);
    StallProfiler::Scope readyProfile(StallProfiler::Section::Wait,"NativeWindow::initialize",nullptr,reinterpret_cast<uintptr_t>(&ready));
    ready.wait(lock,[this]{return initialized;});
    readyProfile.finish();
    if(!error.empty()) {
        lock.unlock();
        StallProfiler::Scope joinProfile(StallProfiler::Section::Wait,"NativeWindow::initialize.failureJoin",nullptr,reinterpret_cast<uintptr_t>(ui.native_handle()));
        ui.join();joinProfile.finish();throw Failure(error);
    }
}
NativeWindow::~NativeWindow() {
    if(HWND window=hwnd.load()) PostMessageW(window,WM_CLOSE,0,0);
    if(ui.joinable()) {
        StallProfiler::Scope joinProfile(StallProfiler::Section::Wait,"NativeWindow::~NativeWindow.join",nullptr,reinterpret_cast<uintptr_t>(ui.native_handle()));
        ui.join();
    }
}
void NativeWindow::captureMouse(HWND window,bool active) {
    if(active==mouseCaptured)return;
    if(active) {
        if(isMenuMouse())return;
        if(!IsWindowEnabled(window)||(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_NOACTIVATE))return;
        if(GetFocus()!=window||GetForegroundWindow()!=window)return;
        RECT client{};POINT origin{};
        if(!GetClientRect(window,&client)||!ClientToScreen(window,&origin))return;
        OffsetRect(&client,origin.x,origin.y);
        restoreCursor=GetCursorPos(&savedCursor)!=FALSE;
        restoreClip=GetClipCursor(&savedClip)!=FALSE;
        SetCapture(window);
        if(GetCapture()!=window)return;
        if(!ClipCursor(&client)) {ReleaseCapture();return;}
        cursorHideCalls=0;
        do {++cursorHideCalls;}while(ShowCursor(FALSE)>=0);
        mouseCaptured=true;resumeMouseAfterMenu=true;keyboard->captureMouse(true);
    } else {
        // ReleaseCapture sends WM_CAPTURECHANGED synchronously.
        mouseCaptured=false;keyboard->captureMouse(false);
        if(GetCapture()==window)ReleaseCapture();
        ClipCursor(restoreClip?&savedClip:nullptr);
        while(cursorHideCalls>0) {ShowCursor(TRUE);--cursorHideCalls;}
        if(restoreCursor)SetCursorPos(savedCursor.x,savedCursor.y);
        restoreCursor=restoreClip=false;
    }
    std::fprintf(stderr,"[NATIVE MOUSE] %s; left=attack right=special/back middle=right-stick-click; Esc=pause/release\n",
        active?"captured":"released");
    SendMessageW(window,WM_TIMER,fpsTimerId,0);
}
LRESULT CALLBACK NativeWindow::procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    auto* self=reinterpret_cast<NativeWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        auto* create=reinterpret_cast<CREATESTRUCTW*>(lparam);
        self=static_cast<NativeWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
    }
    if(message==WM_CLOSE && self) {
        // DefWindowProc destroys the HWND synchronously, sending focus/capture
        // callbacks before WM_DESTROY. Stop guest execution before that teardown.
        if(Simpsons::active&&Simpsons::active->window.get()==self)
            Simpsons::active->requestStop("Native window closed");
        self->closed=true;
        return DefWindowProcW(window,message,wparam,lparam);
    }
    if(message==WM_DESTROY) {
        KillTimer(window,fpsTimerId);
        if(self) {
            // Also cover direct DestroyWindow and initialization failure. Keep
            // cancellation ahead of reentrant cursor/input cleanup here too.
            if(Simpsons::active&&Simpsons::active->window.get()==self)
                Simpsons::active->requestStop("Native window closed");
            self->closed=true;
            self->resumeMouseAfterMenu=false;
            self->captureMouse(window,false);self->keyboard->focus(false);self->hwnd=nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
    if(self) {
        if(message==menuMouseMessage) {
            if(wparam)self->captureMouse(window,false);
            else if(!self->isMenuMouse()&&self->resumeMouseAfterMenu)self->captureMouse(window,true);
            SendMessageW(window,WM_TIMER,fpsTimerId,0);
            return 0;
        }
        if(message==WM_GETMINMAXINFO) {
            // A visible captioned window is otherwise capped to the monitor's
            // default tracking extent, shortening a 1080p client by its title
            // bar on a 1080p desktop. Honor the explicit video-menu extent.
            auto* sizes=reinterpret_cast<MINMAXINFO*>(lparam);
            if(sizes->ptMaxTrackSize.x<self->videoWindowExtent.x)sizes->ptMaxTrackSize.x=self->videoWindowExtent.x;
            if(sizes->ptMaxTrackSize.y<self->videoWindowExtent.y)sizes->ptMaxTrackSize.y=self->videoWindowExtent.y;
            return 0;
        }
        if(message==videoMessage) {
            const bool full=lparam!=0;
            // Replacing the window style must retain live visibility and input
            // state. Dropping WS_VISIBLE hides even an actively rendered game.
            const DWORD state=DWORD(GetWindowLongPtrW(window,GWL_STYLE))&(WS_VISIBLE|WS_DISABLED);
            const DWORD style=(full?WS_POPUP:WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX)|state;
            RECT previous{};if(!GetWindowRect(window,&previous))return 0;
            RECT bounds{};
            if(full) {
                MONITORINFO monitor{sizeof(monitor)};
                if(!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor))return 0;
                bounds=monitor.rcMonitor;
            } else {
                bounds={0,0,LONG(wparam&0xffff),LONG((wparam>>16)&0xffff)};
                if(!AdjustWindowRectExForDpi(&bounds,style,FALSE,0,GetDpiForWindow(window)))return 0;
                const auto origin=self->fullscreen?self->windowedRect:previous;
                OffsetRect(&bounds,origin.left-bounds.left,origin.top-bounds.top);
            }
            // Staged render/filter/AA changes and live pacing changes also
            // publish Video settings. Keep capture and the native window alone
            // unless its actual output extent or display mode must change.
            // Window-size preferences do not change a fullscreen desktop.
            if(full==self->fullscreen && EqualRect(&bounds,&previous))return 1;
            self->resumeMouseAfterMenu=false;
            self->captureMouse(window,false);
            if(full&&!self->fullscreen)self->windowedRect=previous;
            self->videoWindowExtent={bounds.right-bounds.left,bounds.bottom-bounds.top};
            SetWindowLongPtrW(window,GWL_STYLE,style);
            if(!SetWindowPos(window,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,
                SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED))return 0;
            RECT client{};if(!GetClientRect(window,&client))return 0;
            self->presentationWidth=uint32_t(client.right);self->presentationHeight=uint32_t(client.bottom);self->fullscreen=full;
            return 1;
        }
        if(message==WM_TIMER && wparam==fpsTimerId) {
            const auto now=std::chrono::steady_clock::now();
            const double seconds=std::chrono::duration<double>(now-self->fpsSampleStart).count();
            const auto frames=self->presentedFrames.exchange(0,std::memory_order_relaxed);
            self->fpsSampleStart=now;
            const double fps=seconds>0?double(frames)/seconds:0;
            wchar_t title[256]{};
            const wchar_t* mouse=self->isMenuMouse()?L"Mouse: select / click | Right click: back":
                self->mouseCaptured?L"Esc pause / release mouse | F6 release":L"Click to use mouse | F6 capture";
            if(std::swprintf(title,256,L"%ls | FPS: %.1f | %ls",windowTitle,fps,mouse)<0) return 0;
            if(self->inputRecording) {
                using Status=Platform::NativeInputRecording::Status;
                const auto status=self->inputRecording->status();
                const wchar_t* label=status==Status::Recording?L"REC | F9 finish inputs | F8 stop":
                    status==Status::Saved?L"Inputs saved | F8 record":status==Status::Error?L"REC ERROR | see log":L"F8 record inputs";
                if(std::swprintf(title,256,L"%ls | FPS: %.1f | %ls | %ls",windowTitle,fps,label,mouse)<0) return 0;
            }
            SetWindowTextW(window,title);
            return 0;
        }
        if(message==WM_SETFOCUS) self->keyboard->focus(true);
        if(message==WM_KILLFOCUS) {
            self->resumeMouseAfterMenu=false;self->captureMouse(window,false);self->keyboard->focus(false);
        }
        if(message==WM_CANCELMODE||message==WM_ENTERSIZEMOVE||
           (message==WM_CAPTURECHANGED&&self->mouseCaptured&&reinterpret_cast<HWND>(lparam)!=window)) {
            // Our ReleaseCapture reenters with mouseCaptured already false.
            // External capture loss cancels automatic resume; a pause does not.
            self->resumeMouseAfterMenu=false;self->captureMouse(window,false);
        }
        if(message==WM_SETCURSOR&&self->mouseCaptured) {SetCursor(nullptr);return TRUE;}
        const auto pointerPosition=[&](LPARAM position) {
            RECT client{};
            if(GetClientRect(window,&client))self->keyboard->pointerMove(
                int16_t(LOWORD(position)),int16_t(HIWORD(position)),uint32_t(client.right),uint32_t(client.bottom));
        };
        if(message==WM_MOUSEMOVE) {
            pointerPosition(lparam);
            // Disabled test/modal windows receive targeted messages without the
            // physical cursor entering them. Do not schedule a real leave event.
            if(IsWindowEnabled(window)) {
                TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,window,0};TrackMouseEvent(&tracking);
            }
            return 0;
        }
        if(message==WM_MOUSELEAVE) {self->keyboard->pointerLeave();return 0;}
        if(message==WM_MOUSEWHEEL) {
            POINT point{int16_t(LOWORD(lparam)),int16_t(HIWORD(lparam))};
            if(ScreenToClient(window,&point))pointerPosition(MAKELPARAM(point.x,point.y));
            self->keyboard->pointerWheel(int16_t(HIWORD(wparam)));return 0;
        }
        if(message==WM_INPUT&&self->mouseCaptured&&GET_RAWINPUT_CODE_WPARAM(wparam)==RIM_INPUT) {
            RAWINPUT raw{};UINT size=sizeof(raw);
            const auto bytes=GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam),RID_INPUT,&raw,&size,sizeof(RAWINPUTHEADER));
            if(bytes!=UINT(-1)&&bytes>=sizeof(RAWINPUTHEADER)+sizeof(RAWMOUSE)&&
               raw.header.dwType==RIM_TYPEMOUSE&&!(raw.data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE))
                self->keyboard->mouseMotion(raw.data.mouse.lLastX,raw.data.mouse.lLastY);
            // DefWindowProc performs the required foreground raw-input cleanup.
        }
        {
            uint32_t button=0;bool down=false;
            switch(message) {
            case WM_LBUTTONDOWN:button=VK_LBUTTON;down=true;break;
            case WM_LBUTTONUP:button=VK_LBUTTON;break;
            case WM_RBUTTONDOWN:button=VK_RBUTTON;down=true;break;
            case WM_RBUTTONUP:button=VK_RBUTTON;break;
            case WM_MBUTTONDOWN:button=VK_MBUTTON;down=true;break;
            case WM_MBUTTONUP:button=VK_MBUTTON;break;
            case WM_XBUTTONDOWN:button=HIWORD(wparam)==XBUTTON1?VK_XBUTTON1:VK_XBUTTON2;down=true;break;
            case WM_XBUTTONUP:button=HIWORD(wparam)==XBUTTON1?VK_XBUTTON1:VK_XBUTTON2;break;
            }
            if(button) {
                pointerPosition(lparam);
                if(down&&!self->mouseCaptured&&!self->keyboard->rebindActive())self->captureMouse(window,true);
                self->keyboard->mouseButton(button,down);
                return message==WM_XBUTTONDOWN||message==WM_XBUTTONUP?TRUE:0;
            }
        }
        if((message==WM_KEYDOWN||message==WM_KEYUP)&&self->keyboard->rebindActive()&&
           (wparam==VK_F6||wparam==VK_F8||wparam==VK_F9)) {
            self->keyboard->key(UINT(wparam),message==WM_KEYDOWN);return 0;
        }
        if((message==WM_KEYDOWN||message==WM_KEYUP)&&wparam==VK_F6) {
            if(message==WM_KEYDOWN&&!(lparam&(LPARAM(1)<<30))) {
                if(self->mouseCaptured||self->resumeMouseAfterMenu) {
                    self->resumeMouseAfterMenu=false;self->captureMouse(window,false);
                } else self->captureMouse(window,true);
            }
            return 0;
        }
        if(self->inputRecording && (wparam==VK_F8||wparam==VK_F9) &&
           (message==WM_KEYDOWN||message==WM_KEYUP)) {
            if(message==WM_KEYDOWN && !(lparam&(LPARAM(1)<<30))) {
                if(wparam==VK_F8)self->inputRecording->toggle();
                else self->inputRecording->checkpoint();
                // Ignore auto-repeat; neither hotkey reaches game input.
                SendMessageW(window,WM_TIMER,fpsTimerId,0);
            }
            return 0;
        }
        if(message==WM_KEYDOWN || message==WM_KEYUP || message==WM_SYSKEYDOWN || message==WM_SYSKEYUP) {
            const bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN;
            // Preserve the window manager's Alt+F4 close command.
            if((message==WM_SYSKEYDOWN||message==WM_SYSKEYUP)&&wparam==VK_F4)return DefWindowProcW(window,message,wparam,lparam);
            if(down&&wparam==VK_ESCAPE)self->captureMouse(window,false);
            // Window key messages use generic VK_SHIFT; the scan code identifies
            // the left key without mapping the right key to a game action.
            const auto code=wparam==VK_SHIFT?
                MapVirtualKeyW(UINT((lparam>>16)&0xFF),MAPVK_VSC_TO_VK_EX):
                wparam==VK_CONTROL?((lparam&(LPARAM(1)<<24))?VK_RCONTROL:VK_LCONTROL):
                wparam==VK_MENU?((lparam&(LPARAM(1)<<24))?VK_RMENU:VK_LMENU):UINT(wparam);
            if(self->keyboard->key(code,down))return 0;
        }
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
void NativeWindow::run() {
    try {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        HINSTANCE instance=GetModuleHandleW(nullptr);
        WNDCLASSW type{};
        type.lpfnWndProc=procedure;
        type.hInstance=instance;
        type.lpszClassName=L"SimpsonsNativeWindow";
        type.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)); // standard arrow cursor
        type.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        if(!RegisterClassW(&type) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) throw Failure("Native window class registration failed");
        // Fixed initial client extent; runtime resize/recreation needs a verified
        // engine render-target boundary before enabling a resizable frame.
        DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
        RECT rect{0,0,LONG(width.load(std::memory_order_relaxed)),LONG(height.load(std::memory_order_relaxed))};
        if(!AdjustWindowRectExForDpi(&rect,style,FALSE,0,GetDpiForSystem())) throw Failure("Native window extent calculation failed");
        // Automated measurement runs (SIMPSONS_BACKGROUND_WINDOW=1) must never take
        // focus, appear in the taskbar or cover the user's windows: no activation,
        // tool-window style and the bottom of the z-order. Normal launches are unchanged.
        const char* quietRequest=std::getenv("SIMPSONS_BACKGROUND_WINDOW");
        const bool quiet=quietRequest&&*quietRequest&&*quietRequest!='0';
        HWND window=CreateWindowExW(quiet?DWORD(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW):0,type.lpszClassName,L"The Simpsons Game - Native development | FPS: -- | Click to use mouse | F6 capture",style,
            CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,this);
        if(!window) throw Failure("Native window creation failed");
        hwnd=window;
        // Foreground-only raw mouse input. Buttons continue through window
        // messages; a client click captures the cursor for relative motion.
        RAWINPUTDEVICE mouse{0x01,0x02,0,window};
        if(!RegisterRawInputDevices(&mouse,1,sizeof(mouse)))throw Failure("Native raw mouse registration failed");
        RECT client{};
        if(!GetClientRect(window,&client)) throw Failure("Native client extent query failed");
        width.store(uint32_t(client.right),std::memory_order_relaxed);
        height.store(uint32_t(client.bottom),std::memory_order_relaxed);
        fpsSampleStart=std::chrono::steady_clock::now();
        if(!SetTimer(window,fpsTimerId,1000,nullptr)) throw Failure("Native FPS timer creation failed");
        if(quiet) {
            SetWindowPos(window,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            ShowWindow(window,SW_SHOWNOACTIVATE);
        } else ShowWindow(window,SW_SHOWNORMAL);
        UpdateWindow(window);
        {
            std::lock_guard lock(mutex);
            initialized=true;
        }
        ready.notify_one();
        MSG message{};
        while(GetMessageW(&message,nullptr,0,0)>0) {TranslateMessage(&message);DispatchMessageW(&message);}
    } catch(const std::exception& failure) {
        if(HWND window=hwnd.exchange(nullptr)) DestroyWindow(window);
        {
            std::lock_guard lock(mutex);
            error=failure.what(); initialized=true;
        }
        ready.notify_one();
    }
    closed=true;
}
}

PPC_FUNC(__imp__XGetVideoMode) {
    auto& rt=*Simpsons::active;
    uint32_t output=ctx.r3.u32;
    PPCGuestPointer(base,output,48,true);
    if(!rt.window) {
        rt.window=std::make_unique<Simpsons::NativeWindow>(rt.inputRecording);
        rt.window->keyboard->configureControls(rt.controlSettings);
        // A connection or storage query can create the controller source before
        // video initialization. Publish this window's input in either order.
        rt.controllerSource()->attachKeyboard(rt.window->keyboard);
    }
    auto& window=*rt.window;
    const uint32_t width=window.width.load(std::memory_order_relaxed);
    const uint32_t height=window.height.load(std::memory_order_relaxed);
    const float refresh=window.refreshRate.load(std::memory_order_relaxed);
    memset(PPCGuestPointer(base,output,48,true),0,48);
    PPC_STORE_U32(output,width);
    PPC_STORE_U32(output+4,height);
    PPC_STORE_U32(output+8,0); // native progressive rendering
    PPC_STORE_U32(output+12,uint64_t(width)*3>uint64_t(height)*4);
    PPC_STORE_U32(output+16,height>=720);
    PPC_STORE_U32(output+20,std::bit_cast<uint32_t>(refresh));
    PPC_STORE_U32(output+24,1); // US/NTSC-M compatibility profile; reserved fields remain zero.
    fprintf(stderr,"[WINDOW] client=%ux%u native presentation target=%.1fHz; no original pixels rendered\n",width,height,double(refresh));
}
