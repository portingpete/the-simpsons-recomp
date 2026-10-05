#include "runtime/native_input_recording.h"
#include <cstdio>

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    Simpsons::Platform::NativeInputRecording recording(argv[1]);
    recording.toggle();
    if(recording.status()!=Simpsons::Platform::NativeInputRecording::Status::Recording)return 3;
    XINPUT_STATE value{77,{XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_Y,1,255,-32768,32767,-123,456}};
    recording.sample(0,ERROR_SUCCESS,value);
    recording.sample(0,ERROR_SUCCESS,value); // Same packet still records a poll.
    recording.sample(3,ERROR_DEVICE_NOT_CONNECTED,value); // Stale OS output must be cleared.
    // An abrupt process death deliberately skips all destructors and stop/flush.
    TerminateProcess(GetCurrentProcess(),23);
    return 4;
}
