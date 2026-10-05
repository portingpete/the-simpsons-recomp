#include "native_input_recording.h"
#include <cstdio>
#include <cwchar>
#include <exception>
#include <utility>

namespace Simpsons::Platform {
NativeInputRecording::NativeInputRecording(std::filesystem::path target,
        const std::atomic<uint64_t>* sceneCount,bool autoStart):
    directory(std::move(target)),scene(sceneCount),autoStartArmed(autoStart) {}
NativeInputRecording::~NativeInputRecording(){std::lock_guard lock(mutex);finish("shutdown");}
NativeInputRecording::Status NativeInputRecording::status() const {std::lock_guard lock(mutex);return currentStatus;}
std::filesystem::path NativeInputRecording::path() const {std::lock_guard lock(mutex);return currentPath;}
uint64_t NativeInputRecording::elapsed() const {
    LARGE_INTEGER now{};QueryPerformanceCounter(&now);
    const auto ticks=uint64_t(now.QuadPart-started.QuadPart),hz=uint64_t(frequency.QuadPart);
    if(hz==0) return 0;
    return (ticks/hz)*1000000+(ticks%hz)*1000000/hz;
}
void NativeInputRecording::failed(DWORD error) {
    if(file!=INVALID_HANDLE_VALUE){CloseHandle(file);file=INVALID_HANDLE_VALUE;}
    currentStatus=Status::Error;
    std::fprintf(stderr,"[INPUT RECORDING] ERROR Windows=%lu; recording stopped; gameplay input unchanged\n",error);
    std::fflush(stderr);
}
bool NativeInputRecording::write(std::string_view line) {
    while(!line.empty()) {
        DWORD count=0;
        if(!WriteFile(file,line.data(),DWORD(line.size()),&count,nullptr)||!count){failed(GetLastError());return false;}
        line.remove_prefix(count);
    }
    return true;
}
void NativeInputRecording::toggle() {
    std::lock_guard lock(mutex);
    if(file!=INVALID_HANDLE_VALUE){finish("user");return;}
    autoStartArmed=false;
    begin();
}
void NativeInputRecording::checkpoint() {
    std::lock_guard lock(mutex);
    if(file!=INVALID_HANDLE_VALUE)checkpointPending=true;
}
void NativeInputRecording::maybeStart(uint32_t slot) {
    if(slot!=0)return;
    std::lock_guard lock(mutex);
    if(!autoStartArmed || currentStatus!=Status::Ready)return;
    autoStartArmed=false;
    begin();
}
void NativeInputRecording::begin() {
    try {
        std::filesystem::create_directories(directory);
        SYSTEMTIME utc{};GetSystemTime(&utc);
        wchar_t name[128]{};
        // CREATE_NEW never overwrites another recording, even within one ms.
        for(unsigned attempt=0;attempt<100;++attempt) {
            std::swprintf(name,128,L"inputs-%04u%02u%02u-%02u%02u%02u-%03uZ-%lu-%llu.jsonl",
                unsigned(utc.wYear),unsigned(utc.wMonth),unsigned(utc.wDay),unsigned(utc.wHour),
                unsigned(utc.wMinute),unsigned(utc.wSecond),unsigned(utc.wMilliseconds),GetCurrentProcessId(),++session);
            currentPath=directory/name;
            file=CreateFileW(currentPath.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file!=INVALID_HANDLE_VALUE)break;
            if(GetLastError()!=ERROR_FILE_EXISTS){failed(GetLastError());return;}
        }
        if(file==INVALID_HANDLE_VALUE){failed(ERROR_FILE_EXISTS);return;}
        if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0||!QueryPerformanceCounter(&started)){
            failed(ERROR_NOT_SUPPORTED);return;
        }
        sequence=0;
        checkpointPending=false;
        char header[512]{};
        const int count=std::snprintf(header,sizeof(header),
            "{\"type\":\"header\",\"version\":1,\"pid\":%lu,\"started_utc\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\","
            "\"clock\":\"qpc_microseconds\",\"boundary\":\"returned_controller_state\",\"start_scene\":%llu,\"save_state_captured\":false}\n",
            GetCurrentProcessId(),unsigned(utc.wYear),unsigned(utc.wMonth),unsigned(utc.wDay),unsigned(utc.wHour),
            unsigned(utc.wMinute),unsigned(utc.wSecond),unsigned(utc.wMilliseconds),
            static_cast<unsigned long long>(scene?scene->load(std::memory_order_acquire):0));
        if(count<=0||size_t(count)>=sizeof(header)){failed(ERROR_INSUFFICIENT_BUFFER);return;}
        if(!write({header,size_t(count)}))return;
        currentStatus=Status::Recording;
        std::fprintf(stderr,"[INPUT RECORDING] START file=%ls; F8 stops; every returned input poll is written immediately\n",currentPath.c_str());
        std::fflush(stderr);
    } catch(const std::exception& e) {
        std::fprintf(stderr,"[INPUT RECORDING] Cannot start: %s\n",e.what());failed(ERROR_OPEN_FAILED);
    }
}
void NativeInputRecording::sample(uint32_t slot,DWORD result,const XINPUT_STATE& state,bool modal) {
    std::lock_guard lock(mutex);
    if(file==INVALID_HANDLE_VALUE)return;
    // XInput output is undefined on disconnect; record the ABI's zero state.
    const XINPUT_STATE value=result==ERROR_SUCCESS?state:XINPUT_STATE{};
    const auto& pad=value.Gamepad;
    char line[512]{};
    const int count=std::snprintf(line,sizeof(line),
        "{\"type\":\"input\",\"seq\":%llu,\"t_us\":%llu,\"consumer\":\"%s\",\"slot\":%u,\"status\":%lu,\"packet\":%lu,"
        "\"buttons\":%u,\"lt\":%u,\"rt\":%u,\"lx\":%d,\"ly\":%d,\"rx\":%d,\"ry\":%d}\n",
        sequence,elapsed(),modal?"modal":"game",slot,result,value.dwPacketNumber,unsigned(pad.wButtons),
        unsigned(pad.bLeftTrigger),unsigned(pad.bRightTrigger),int(pad.sThumbLX),int(pad.sThumbLY),int(pad.sThumbRX),int(pad.sThumbRY));
    if(count<=0||size_t(count)>=sizeof(line)){failed(ERROR_INSUFFICIENT_BUFFER);return;}
    if(write({line,size_t(count)})) {
        ++sequence;
        if(checkpointPending && sequence%4==0)finish("checkpoint");
    }
}
void NativeInputRecording::finish(const char* reason) {
    if(file==INVALID_HANDLE_VALUE)return;
    char line[160]{};
    const int count=std::snprintf(line,sizeof(line),"{\"type\":\"end\",\"samples\":%llu,\"t_us\":%llu,\"reason\":\"%s\",\"end_scene\":%llu}\n",
        sequence,elapsed(),reason,static_cast<unsigned long long>(scene?scene->load(std::memory_order_acquire):0));
    if(count<=0||size_t(count)>=sizeof(line)){failed(ERROR_INSUFFICIENT_BUFFER);return;}
    if(!write({line,size_t(count)}))return;
    if(!FlushFileBuffers(file)){failed(GetLastError());return;}
    CloseHandle(file);file=INVALID_HANDLE_VALUE;currentStatus=Status::Saved;
    checkpointPending=false;
    std::fprintf(stderr,"[INPUT RECORDING] SAVED samples=%llu file=%ls\n",sequence,currentPath.c_str());std::fflush(stderr);
}
void NativeInputRecording::stop(){std::lock_guard lock(mutex);finish("user");}
}
