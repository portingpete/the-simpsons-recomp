#include "sky_vertices.h"
#include "runtime.h"
#include "common/geometry_extent.h"
#include <algorithm>
#include <bit>
#include <bitset>

namespace Simpsons {
namespace {
void require(bool value,const char* why) {if(!value)throw Failure(why);}
uint32_t word(std::span<const uint8_t> bytes,size_t at) {
    require(at<=bytes.size()&&bytes.size()-at>=4,"Truncated sky vertex word");
    return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];
}
float number(std::span<const uint8_t> bytes,size_t at) {
    const auto bits=word(bytes,at);
    require((bits&0x7F800000)!=0x7F800000,"Nonfinite sky vertex attribute");
    return std::bit_cast<float>(bits);
}
}
std::vector<Graphics::SkyVertex> decodeSkyVertices(std::span<const uint8_t> bytes,
    std::span<const uint8_t> declaration,uint32_t stride) {
    // Original stream binding reads geometry+4;8243C6A8/B4 stores DWORD
    // stride in one byte. Fetch association8245EE64..84 uses semantic/index.
    require(validOriginalVertexExtent(bytes.size(),stride),
        "Sky vertex stride or extent is invalid");
    require(declaration.size()==48,"Sky declaration extent is invalid");
    std::array<uint32_t,3> offsets{};uint32_t found=0;std::bitset<1020> occupied;
    for(size_t at=0;at<36;at+=12) {
        const auto streamOffset=word(declaration,at),type=word(declaration,at+4),semantic=word(declaration,at+8);
        require(!(streamOffset>>16)&&(semantic&0xFF0000FF)==0,"Sky declaration stream, method or reserved byte differs");
        const auto offset=streamOffset&65535;uint32_t slot{},width{},expected{};
        if(semantic==0){slot=0;width=12;expected=0x002A23B9;}
        else if(semantic==0x00050000){slot=1;width=8;expected=0x002C23A5;}
        else if(semantic==0x00050100){slot=2;width=8;expected=0x002C23A5;}
        else throw Failure("Sky declaration semantic differs");
        require(type==expected&&!(offset&3)&&offset<=stride&&width<=stride-offset&&!(found&(1u<<slot)),
            "Sky input format, offset or uniqueness differs");
        for(uint32_t byte=offset;byte<offset+width;++byte) {
            require(!occupied.test(byte),"Overlapping sky vertex attributes");occupied.set(byte);
        }
        offsets[slot]=offset;found|=1u<<slot;
    }
    require(found==7,"Sky declaration lacks a consumed shader input");
    require(word(declaration,36)==0x00FF0000&&word(declaration,40)==UINT32_MAX&&word(declaration,44)==0,
        "Sky declaration terminator differs");
    // Both original sky vertex programs fetch position0 and UV0. UV1 is
    // retained when its complete bytes exist, but does not constrain a fetch.
    const auto fetchEnd=std::max(offsets[0]+12,offsets[1]+8);
    const auto count=originalFetchedVertexCount(bytes.size(),stride,fetchEnd);
    require(count!=0,"Sky vertex owner lacks its first consumed record");
    std::vector<Graphics::SkyVertex> result(count);
    for(size_t i=0;i<result.size();++i) {
        const auto at=i*stride;auto& v=result[i];
        for(size_t lane=0;lane<3;++lane)v.position[lane]=number(bytes,at+offsets[0]+4*lane);
        for(size_t lane=0;lane<2;++lane)v.uv[lane]=number(bytes,at+offsets[1]+4*lane);
        if(at+offsets[2]+8<=bytes.size())
            for(size_t lane=0;lane<2;++lane)v.uv1[lane]=number(bytes,at+offsets[2]+4*lane);
    }
    return result;
}
}
