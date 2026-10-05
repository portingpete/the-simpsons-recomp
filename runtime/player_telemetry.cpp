#include "player_telemetry.h"
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace Simpsons {

void PlayerTelemetry::observeCharacterFrame(uint32_t object,uint32_t boneCount,
                                            const std::array<float,16>& m,
                                            bool frameValidated) noexcept {
    if(!object || boneCount!=43)return;
    if(seen_&&object_!=object){ambiguous_=true;return;}
    seen_=true;object_=object;
    if(!frameValidated){invalid_=true;return;}
    // RwMatrix has right/up/at/position XYZ triplets. The fourth word of
    // each row is flags/padding, not a homogeneous float, and is ignored.
    // Original sub_823F2540 chooses this LTM for a child frame; +0x80..88
    // are its world-space position.
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<3;++col)
        if(!std::isfinite(m[4*row+col])){invalid_=true;return;}
    for(unsigned row=0;row<3;++row) {
        const double length=double(m[4*row])*m[4*row]+double(m[4*row+1])*m[4*row+1]+double(m[4*row+2])*m[4*row+2];
        if(length<1e-8||length>1e8){invalid_=true;return;}
    }
    for(unsigned i=12;i<15;++i)if(std::fabs(m[i])>1e6f){invalid_=true;return;}
    position_={m[12],m[13],m[14]};
}

std::string PlayerTelemetry::takeJson() {
    const bool valid=seen_&&!ambiguous_&&!invalid_;
    std::string json;
    if(valid) {
        try {
            std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(9);
            out<<"{\"available\":true,\"source\":\"original_43_bone_atomic_frame_ltm\",\"position\":["
               <<position_[0]<<','<<position_[1]<<','<<position_[2]<<"]}";
            json=out.str();
        }catch(...) {json="{\"available\":false,\"source\":\"original_43_bone_atomic_frame_ltm\"}";}
    } else json="{\"available\":false,\"source\":\"original_43_bone_atomic_frame_ltm\"}";
    seen_=ambiguous_=invalid_=false;object_=0;position_={};
    return json;
}

}
