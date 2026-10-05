#include "runtime/player_telemetry.h"
#include <array>
#include <limits>
#include <stdexcept>
#include <cstdio>

namespace {
void need(bool condition,const char* reason) {if(!condition)throw std::runtime_error(reason);}
std::array<float,16> frame(float x,float y,float z) {
    return {1,0,0,0, 0,1,0,0, 0,0,1,0, x,y,z,1};
}
}

int main() {
    try {
        Simpsons::PlayerTelemetry telemetry;
        need(telemetry.takeJson().find("\"available\":false")!=std::string::npos,
             "A frame with no character reused a position");
        telemetry.observeCharacterFrame(0x1234,43,frame(-2.75f,0.5f,-23.0f),true);
        const auto valid=telemetry.takeJson();
        need(valid.find("\"available\":true")!=std::string::npos &&
             valid.find("\"position\":[-2.75,0.5,-23]")!=std::string::npos,
             "Unique verified child-frame LTM did not expose its world translation");
        need(telemetry.takeJson().find("\"available\":false")!=std::string::npos,
             "Previous presentation leaked into the next one");
        telemetry.observeCharacterFrame(0x1234,43,frame(1,2,3),true);
        telemetry.observeCharacterFrame(0x1234,43,frame(4,5,6),true);
        need(telemetry.takeJson().find("\"position\":[4,5,6]")!=std::string::npos,
             "Same character was not allowed to update within one presentation");
        telemetry.observeCharacterFrame(0x1234,43,frame(1,2,3),true);
        telemetry.observeCharacterFrame(0x5678,43,frame(4,5,6),true);
        need(telemetry.takeJson().find("\"available\":false")!=std::string::npos,
             "Two 43-bone characters were mistaken for one player");
        telemetry.observeCharacterFrame(0x1234,17,frame(1,2,3),true);
        need(telemetry.takeJson().find("\"available\":false")!=std::string::npos,
             "A different skeleton entered the player field");
        telemetry.observeCharacterFrame(0x1234,43,frame(1,2,3),false);
        need(telemetry.takeJson().find("\"available\":false")!=std::string::npos,
             "Unverified frame ownership entered the player field");
        auto bad=frame(1,2,3);bad[0]=bad[1]=bad[2]=0;
        telemetry.observeCharacterFrame(0x1234,43,bad,true);
        need(telemetry.takeJson().find("\"available\":false")!=std::string::npos,
             "Degenerate world basis entered the player field");
        bad=frame(1,2,3);bad[3]=std::numeric_limits<float>::quiet_NaN();
        telemetry.observeCharacterFrame(0x1234,43,bad,true);
        need(telemetry.takeJson().find("\"available\":true")!=std::string::npos,
             "RenderWare matrix flags were mistaken for homogeneous float lanes");
        bad=frame(1,2,3);bad[12]=std::numeric_limits<float>::quiet_NaN();
        telemetry.observeCharacterFrame(0x1234,43,bad,true);
        need(telemetry.takeJson().find("\"available\":false")!=std::string::npos,
             "Nonfinite position entered the player field");
        std::puts("PASS player telemetry: unique character, RenderWare world matrix, and per-presentation freshness");
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL player telemetry: %s\n",error.what());
        return 1;
    }
}
