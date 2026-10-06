#include "runtime/native_input_recording.h"
#include <charconv>
#include <cstdio>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using Simpsons::Platform::NativeInputRecording;
using Simpsons::Platform::NativeMouseMotion;
static_assert(std::is_trivially_copyable_v<NativeMouseMotion>);
static_assert(std::is_standard_layout_v<NativeMouseMotion>);

void need(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
int64_t number(const std::string& row,const char* field) {
    const std::string key="\""+std::string(field)+"\":";
    const auto found=row.find(key);
    need(found!=std::string::npos,"Recording field is missing");
    const auto begin=row.data()+found+key.size();
    int64_t value=0;
    const auto parsed=std::from_chars(begin,row.data()+row.size(),value);
    need(parsed.ec==std::errc{} && parsed.ptr!=begin && parsed.ptr!=row.data()+row.size() &&
         (*parsed.ptr==',' || *parsed.ptr=='}'),"Recording field is not numeric");
    return value;
}
struct Directory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("Simpsons-mouse-recording-"+std::to_string(GetCurrentProcessId())+"-"+
         std::to_string(GetTickCount64()));
    std::filesystem::path recording;
    Directory(){need(CreateDirectoryW(path.c_str(),nullptr)!=FALSE,"Cannot create exclusive recording test directory");}
    ~Directory() {
        // The fixture owns one file. Avoid recursive cleanup of the directory.
        if(!recording.empty())DeleteFileW(recording.c_str());
        RemoveDirectoryW(path.c_str());
    }
};
void recordingExtension() {
    Directory directory;
    NativeInputRecording recorder(directory.path);
    recorder.toggle();
    need(recorder.status()==NativeInputRecording::Status::Recording,"Cannot start input recorder");
    directory.recording=recorder.path();
    const XINPUT_STATE state{77,{XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_Y,1,255,-32768,32767,-123,456}};
    auto rawMouseState=state;
    rawMouseState.Gamepad.sThumbRX=0;
    rawMouseState.Gamepad.sThumbRY=0;
    const NativeMouseMotion extreme{true,std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max()};
    recorder.sample(0,ERROR_SUCCESS,rawMouseState,false,extreme);
    recorder.sample(0,ERROR_SUCCESS,state); // Existing callers retain neutral mouse input.
    recorder.sample(0,ERROR_SUCCESS,state,false,{false,12,-34});
    recorder.sample(0,ERROR_SUCCESS,rawMouseState,false,{true,0,0});
    recorder.sample(1,ERROR_SUCCESS,state,false,extreme);
    recorder.sample(0,ERROR_SUCCESS,state,true,extreme);
    recorder.sample(0,ERROR_DEVICE_NOT_CONNECTED,state,false,extreme);
    recorder.sample(0,ERROR_BAD_ARGUMENTS,state,false,extreme);
    recorder.stop();
    need(recorder.status()==NativeInputRecording::Status::Saved,"Cannot finalize input recorder");

    std::ifstream stream(directory.recording);
    std::vector<std::string> rows;
    for(std::string row;std::getline(stream,row);)rows.push_back(std::move(row));
    need(stream.eof() && rows.size()==10,"Recording extension lost an input poll");
    need(number(rows[0],"version")==1 &&
         rows[0].find("\"boundary\":\"returned_controller_state\"")!=std::string::npos &&
         rows[0].find("\"mouse_camera\":\"raw_counts_per_slot0_poll\"")!=std::string::npos,
         "Recording extension changed the v1 boundary or lacks raw-count metadata");
    const auto verifyMouse=[&](size_t sample,const NativeMouseMotion& expected) {
        const auto& row=rows[sample+1];
        need(number(row,"seq")==int64_t(sample),"Recording sequence changed");
        need(number(row,"mouse_native")==int64_t(expected.active) &&
             number(row,"mouse_x")==expected.x && number(row,"mouse_y")==expected.y,
             "Mouse recording lost raw counts or leaked input across consumer boundaries");
    };
    verifyMouse(0,extreme);
    verifyMouse(1,{});
    verifyMouse(2,{});
    verifyMouse(3,{true,0,0});
    verifyMouse(4,{});
    verifyMouse(5,{});
    verifyMouse(6,{});
    verifyMouse(7,{});
    need(number(rows[1],"packet")==77 && number(rows[1],"buttons")==0x9000 &&
         number(rows[1],"lt")==1 && number(rows[1],"rt")==255 &&
         number(rows[1],"lx")==-32768 && number(rows[1],"ly")==32767 &&
         number(rows[1],"rx")==0 && number(rows[1],"ry")==0 &&
         number(rows[2],"rx")==-123 && number(rows[2],"ry")==456,
         "Recording extension changed returned controller values");
    need(number(rows[5],"slot")==1 && rows[6].find("\"consumer\":\"modal\"")!=std::string::npos,
         "Recording extension changed the input consumer");
    for(size_t index:{size_t(7),size_t(8)}) {
        for(const auto* field:{"packet","buttons","lt","rt","lx","ly","rx","ry"})
            need(number(rows[index],field)==0,"Failed input poll serialized stale controller data");
    }
    need(number(rows.back(),"samples")==8,"Recording extension changed the finalized sample count");
    const auto neutral=NativeMouseMotion{};
    need(!neutral.active && neutral.x==0 && neutral.y==0 && neutral==NativeMouseMotion{},
         "Default native mouse snapshot is not neutral");
}
}
int main() {
    try {
        recordingExtension();
        std::puts("PASS: raw native mouse recording, neutral consumer boundaries, preserved v1 controller fields");
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what());
        return 1;
    }
}
