#pragma once
#include <windows.h>
#include <cstdint>

namespace Simpsons::Graphics {
// Measures a window in its own DPI awareness. The native window thread is per-monitor
// aware; a caller thread with another awareness reads DPI-virtualized coordinates on
// any display above 100% (a 1280x720 client reads 853x480 at 150%).
class WindowDpiScope {
    DPI_AWARENESS_CONTEXT previous_{};
public:
    explicit WindowDpiScope(HWND window) noexcept {
        if(const auto context=GetWindowDpiAwarenessContext(window))previous_=SetThreadDpiAwarenessContext(context);
    }
    ~WindowDpiScope() {if(previous_)SetThreadDpiAwarenessContext(previous_);}
    WindowDpiScope(const WindowDpiScope&)=delete;
    WindowDpiScope& operator=(const WindowDpiScope&)=delete;
};
inline bool presentationWindowMatches(HWND window,uint32_t width,uint32_t height) {
    DWORD process{};
    if(!window || !IsWindow(window) || !GetWindowThreadProcessId(window,&process) || process!=GetCurrentProcessId())
        return false;
    const WindowDpiScope dpi(window);
    // Minimize changes the Win32 client extent, not the fixed render targets or
    // swapchain buffers. Sample IsIconic on both sides of the client query.
    const ULONGLONG start=GetTickCount64();
    for(;;) {
        const bool minimizedBefore=IsIconic(window)!=FALSE;
        RECT client{};
        if(!GetClientRect(window,&client))return false;
        const bool minimizedAfter=IsIconic(window)!=FALSE;
        if(minimizedBefore || minimizedAfter ||
           (client.right-client.left==LONG(width) && client.bottom-client.top==LONG(height)))return true;
        // Restore can clear the iconic state while the client is still empty.
        // Give the UI thread a bounded opportunity to finish that transition;
        // a nonempty wrong size or a persistently empty window still fails.
        if((client.right>client.left && client.bottom>client.top) || GetTickCount64()-start>=1000)return false;
        Sleep(1);
    }
}
}
