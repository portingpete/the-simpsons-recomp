#pragma once
#include <windows.h>
#include <cstdint>

namespace Simpsons::Graphics {
inline bool presentationWindowMatches(HWND window,uint32_t width,uint32_t height) {
    DWORD process{};
    if(!window || !IsWindow(window) || !GetWindowThreadProcessId(window,&process) || process!=GetCurrentProcessId())
        return false;
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
