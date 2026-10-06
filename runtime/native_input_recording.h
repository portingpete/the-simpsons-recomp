#pragma once
#include <windows.h>
#include <xinput.h>
#include <filesystem>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string_view>
#include "native_mouse_input.h"

namespace Simpsons::Platform {
// Records the final input returned to each consumer, without changing input.
// Each JSON line goes directly to Windows, not a process-local stdio buffer.
// Version 1 retains every controller field. Optional mouse_camera header and
// mouse_native/mouse_x/mouse_y input fields extend it for raw camera replay;
// older readers can ignore them, and recordings without them remain valid.
class NativeInputRecording {
public:
    enum class Status { Ready, Recording, Saved, Error };
    explicit NativeInputRecording(std::filesystem::path directory,
        const std::atomic<uint64_t>* scene=nullptr,bool autoStart=false);
    ~NativeInputRecording();
    NativeInputRecording(const NativeInputRecording&)=delete;
    NativeInputRecording& operator=(const NativeInputRecording&)=delete;
    void toggle();
    void checkpoint();
    void maybeStart(uint32_t slot);
    void stop();
    void sample(uint32_t slot,DWORD result,const XINPUT_STATE& state,bool modal=false,
        const NativeMouseMotion& mouse={});
    Status status() const;
    std::filesystem::path path() const;
private:
    mutable std::mutex mutex;
    std::filesystem::path directory,currentPath;
    HANDLE file=INVALID_HANDLE_VALUE;
    Status currentStatus=Status::Ready;
    uint64_t sequence=0,session=0;
    const std::atomic<uint64_t>* scene=nullptr;
    bool autoStartArmed=false;
    bool checkpointPending=false;
    LARGE_INTEGER started{},frequency{};
    bool write(std::string_view line);
    void failed(DWORD error);
    void finish(const char* reason);
    void begin();
    uint64_t elapsed() const;
};
}
