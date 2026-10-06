#pragma once
#include <windows.h>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <string>
#include <memory>

namespace Simpsons {
namespace Platform {class NativeKeyboard;class NativeInputRecording;}
class NativeWindow {
    friend struct NativeMouseCaptureTestAccess;
public:
    explicit NativeWindow(std::shared_ptr<Platform::NativeInputRecording> recording={});
    ~NativeWindow();
    NativeWindow(const NativeWindow&)=delete;
    NativeWindow& operator=(const NativeWindow&)=delete;
    HWND handle() const {return hwnd.load();}
    // Rendering only counts frames; the UI timer owns the title and elapsed time.
    void recordPresentedFrame() noexcept {presentedFrames.fetch_add(1,std::memory_order_relaxed);}
    void configureVideo(uint32_t outputWidth,uint32_t outputHeight,bool fullscreen);
    void setMenuMouse(bool active);
    bool isMenuMouse() const {return menuMouse.load(std::memory_order_relaxed);}
    std::atomic<uint32_t> presentationWidth{1280},presentationHeight{720};
    std::atomic<uint32_t> width{1280},height{720};
    // Native presentation policy for the original US 60 Hz simulation profile.
    std::atomic<float> refreshRate{60.0f};
    std::atomic<bool> closed=false;
    const std::shared_ptr<Platform::NativeKeyboard> keyboard;
private:
    const std::shared_ptr<Platform::NativeInputRecording> inputRecording;
    void run();
    void captureMouse(HWND window,bool active);
    static LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM);
    std::atomic<HWND> hwnd=nullptr;
    std::atomic<bool> menuMouse=false;
    std::atomic<uint64_t> presentedFrames=0;
    std::chrono::steady_clock::time_point fpsSampleStart;
    std::thread ui;
    std::mutex mutex;
    std::condition_variable ready;
    bool initialized=false;
    // Accessed only by the window thread. Restored on focus/capture loss/close.
    bool mouseCaptured=false,restoreCursor=false,restoreClip=false;
    // The player's last successful gameplay capture survives Escape/menu
    // release. Explicit release, focus loss and actual video changes cancel it.
    bool resumeMouseAfterMenu=false;
    POINT savedCursor{};
    RECT savedClip{};
    int cursorHideCalls=0;
    std::string error;
    bool fullscreen=false;
    RECT windowedRect{};
    POINT videoWindowExtent{}; // Requested outer extent; owned by the UI thread.
};
}
