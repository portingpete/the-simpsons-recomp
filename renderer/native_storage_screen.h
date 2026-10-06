#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Simpsons::Graphics {
struct StorageScreenBackground {
    std::vector<uint8_t> rgba;
    uint32_t width{},height{};
};
struct NativeStorageScreenState {
    std::wstring player,folder;
    uint64_t availableBytes{},requiredBytes{};
    unsigned selectedRow{};
    bool storageAvailable=true;
    bool controller=false;
    bool operator==(const NativeStorageScreenState&) const=default;
};
struct StorageScreenRect {float left,top,right,bottom;};

// A renderer owned by the modal caller thread. The host owns the child HWND,
// navigation and completion; this object contains no guest memory pointers.
class NativeStorageScreen {
public:
    NativeStorageScreen(HWND child,uint32_t clientWidth,uint32_t clientHeight,
        StorageScreenBackground background,const std::filesystem::path& assetDirectory);
    ~NativeStorageScreen();
    NativeStorageScreen(const NativeStorageScreen&)=delete;
    NativeStorageScreen& operator=(const NativeStorageScreen&)=delete;
    bool render(const NativeStorageScreenState& state);
    void resize(uint32_t clientWidth,uint32_t clientHeight);
    int hitTest(int clientX,int clientY) const;
    // Logical 1280x720 rectangles; input uses the same aspect-preserving map.
    static std::array<StorageScreenRect,2> rowRectangles();
    std::vector<uint8_t> readbackRGBA();
    uint32_t width() const;
    uint32_t height() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
