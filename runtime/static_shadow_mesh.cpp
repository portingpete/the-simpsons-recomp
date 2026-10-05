#include "static_shadow_mesh.h"
#include "runtime.h"
#include "common/geometry_extent.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace Simpsons {
namespace {
void require(bool value,const char* why) {if(!value)throw Failure(why);}
uint32_t word(std::span<const uint8_t> bytes,size_t at) {
    require(at<=bytes.size()&&bytes.size()-at>=4,"Truncated static shadow mesh word");
    return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];
}
float number(std::span<const uint8_t> bytes,size_t at) {
    const float value=std::bit_cast<float>(word(bytes,at));
    require(std::isfinite(value),"Nonfinite static shadow vertex attribute");
    return value;
}
}

std::vector<Graphics::ShadowMeshVertex> decodeStaticShadowVertices(
        std::span<const uint8_t> bytes,std::span<const uint8_t> declaration,uint32_t stride) {
    // Original8243C6A8/B4 stores stride/4 in a single byte.
    require(validOriginalVertexExtent(bytes.size(),stride),
            "Static shadow vertex stride or extent is invalid");
    require(declaration.size()>=36&&declaration.size()%12==0&&declaration.size()<=65*12,
            "Static shadow declaration extent is invalid");
    std::array<uint32_t,2> offsets{};
    uint32_t found=0;
    for(size_t at=0;at<declaration.size();at+=12) {
        const auto streamOffset=word(declaration,at),type=word(declaration,at+4),semantic=word(declaration,at+8);
        const auto stream=streamOffset>>16,offset=streamOffset&65535;
        if(at+12==declaration.size()) {
            require(streamOffset==0x00FF0000&&type==UINT32_MAX&&(semantic&0xFFFFFF00)==0,
                    "Static shadow declaration terminator differs");
            break;
        }
        require(stream==0&&type!=UINT32_MAX&&(semantic>>24)==0,
                "Unsupported static shadow declaration stream/method");
        const auto usage=(semantic>>16)&255,index=(semantic>>8)&255;
        uint32_t slot=0,width=0,expected=0;
        // reach-game-009: float3 position, packed normal/color, float2 UV0/1.
        // Validate even dead rows rather than accepting arbitrary declarations.
        if(usage==0&&index==0) {slot=0;width=12;expected=0x002A23B9;}
        else if(usage==5&&index==0) {slot=1;width=8;expected=0x002C23A5;}
        else if(usage==3&&index==0) {slot=2;width=4;expected=0x002A2187;}
        else if(usage==10&&index==0) {slot=3;width=4;expected=0x00182886;}
        else if(usage==5&&index==1) {slot=4;width=8;expected=0x002C23A5;}
        // The original Boolean-false shadow program does not fetch tangent0.
        // Retain the exact packed tangent declaration used by live stride40
        // geometry and the original generic declaration constructor.
        else if(usage==6&&index==0) {slot=5;width=4;expected=0x002A2187;}
        else throw Failure("Unqualified static shadow vertex semantic");
        require(type==expected&&offset<=stride&&width<=stride-offset&&!(offset&3)&&!(found&(1u<<slot)),
                "Static shadow input format, offset or uniqueness differs");
        if(slot<offsets.size())offsets[slot]=offset;
        found|=1u<<slot;
    }
    require((found&3)==3,"Static shadow declaration lacks position0 or UV0");
    // The native input layout always fetches four attributes. Only position/UV
    // survive VS820C2FA0's c40.x==0 path; weight/index temporaries are overwritten
    // before use outside the skin branch. Zero initializes these absent inputs
    // deterministically without inventing a bone or a unit weight.
    const auto fetchEnd=std::max(offsets[0]+12,offsets[1]+8);
    const auto count=originalFetchedVertexCount(bytes.size(),stride,fetchEnd);
    require(count!=0,"Static shadow vertex owner lacks its first consumed record");
    std::vector<Graphics::ShadowMeshVertex> result(count,Graphics::ShadowMeshVertex{});
    for(size_t i=0;i<result.size();++i) {
        const auto at=i*stride;auto& vertex=result[i];
        for(size_t lane=0;lane<3;++lane)vertex.position[lane]=number(bytes,at+offsets[0]+4*lane);
        for(size_t lane=0;lane<2;++lane)vertex.uv[lane]=number(bytes,at+offsets[1]+4*lane);
    }
    return result;
}
}
