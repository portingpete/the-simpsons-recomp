#include "renderer/temporal_history.h"
#include <bit>
#include <cstring>
#include <limits>
using namespace Simpsons::Graphics;
namespace {
void require(bool condition,const char* failure) {
    if(!condition)throw std::runtime_error(failure);
}
}
int main() {
    TemporalGeometryHistory history;
    TemporalGeometryIdentity instance{0x1000,1,0x2000,0x2100,0x82007C1C};
    std::array<TemporalRegister,256> bank{};
    bank[0][0]=bank[1][1]=bank[2][2]=bank[3][3]=1;
    bank[38][0]=0.25f;bank[52][3]=2; // Actual morph control and first skin bone row.
    auto first=history.capture(instance,bank);require(!first.valid,"New instance consumed nonexistent transform history");
    history.finishFrame();
    bank[0][3]=1;bank[38][0]=0.75f;bank[52][3]=3;
    auto next=history.capture(instance,bank);
    require(next.valid&&next.previous[0][3]==0&&next.previous[38][0]==0.25f&&next.previous[52][3]==2,
        "Previous camera, morph, and bone registers were not retained independently");
    auto secondInstance=instance;secondInstance.object=0x1100;
    require(!history.capture(secondInstance,bank).valid,"Shared geometry reused another instance transform history");
    auto recycled=instance;recycled.generation=2;
    require(!history.capture(recycled,bank).valid,"Recycled original allocation consumed a retired generation");
    history.finishFrame();history.finishFrame();
    require(!history.capture(instance,bank).valid,"Visibility gap consumed stale bone history");
    history.reset();require(!history.capture(instance,bank).valid,"Camera reset retained previous transform history");
    history.reset();
    bank[100][0]=std::bit_cast<float>(0x7fc01234u);
    history.capture(instance,bank);history.capture(instance,bank); // Same NaN payload is exact bytes.
    bank[101][0]=-0.0f;
    bool rejected=false;try{history.capture(instance,bank);}catch(const std::runtime_error&){rejected=true;}
    require(rejected,"Signed-zero bank mutation hid within an aliased instance");
    const auto jitter=temporalJitter(0,1280,720,1280,720);
    require(jitter[0]==0&&std::abs(jitter[1]+1.0f/6.0f)<1e-6f,"Halton jitter begins at the wrong phase or axis sign");
    require(temporalJitter(0,1280,720,1280,720)==temporalJitter(8,1280,720,1280,720),"DLAA jitter cycle is not eight phases");
    require(temporalJitter(0,640,360,1280,720)==temporalJitter(32,640,360,1280,720),"DLSS jitter phase count ignores render ratio");
}
