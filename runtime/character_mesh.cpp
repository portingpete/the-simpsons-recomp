#include "character_mesh.h"
#include "runtime.h"
#include "common/geometry_extent.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace Simpsons {
namespace {
void require(bool value,const char* why){if(!value)throw Failure(why);}
uint32_t word(std::span<const uint8_t> b,size_t at) {
    require(at<=b.size()&&b.size()-at>=4,"Truncated character mesh word");
    return uint32_t(b[at])<<24|uint32_t(b[at+1])<<16|uint32_t(b[at+2])<<8|b[at+3];
}
float number(std::span<const uint8_t> b,size_t at) {
    const float result=std::bit_cast<float>(word(b,at));
    require(std::isfinite(result),"Nonfinite character vertex attribute");return result;
}
}
std::vector<Graphics::ShadowMeshVertex> decodeCharacterVertices(
        std::span<const uint8_t> bytes,std::span<const uint8_t> declaration,uint32_t stride,uint32_t bones) {
    require(validOriginalVertexExtent(bytes.size(),stride),
            "Character vertex stride or extent is invalid");
    require(bones>0&&bones<=64,"Character shadow mesh bone count is unqualified");
    require(declaration.size()>=24&&declaration.size()%12==0&&declaration.size()<=65*12,
            "Character declaration extent is invalid");
    std::array<uint32_t,4> offsets{};uint32_t found=0;bool unusedUV1=false;
    for(size_t at=0;at<declaration.size();at+=12) {
        const auto streamOffset=word(declaration,at),type=word(declaration,at+4),semantic=word(declaration,at+8);
        const auto stream=streamOffset>>16,offset=streamOffset&65535;
        if(at+12==declaration.size()) {
            require(streamOffset==0x00FF0000&&type==UINT32_MAX&&(semantic&0xFFFFFF00)==0,
                    "Character declaration terminator differs");break;
        }
        require(stream<16&&type!=UINT32_MAX&&(semantic>>24)==0,"Unsupported character declaration stream/method");
        const auto usage=(semantic>>16)&255,index=(semantic>>8)&255;
        // VS820C2FA0 consumes position0, texcoord0, blendweight0 and
        // blendindices0. Extra morph streams and normal/color do not feed it.
        if(stream) {
            require(usage==0&&index==stream&&offset==0&&type==0x002A23B9,
                    "Unqualified character morph declaration");continue;
        }
        // Live011's 56-byte layout also declares UV1. Original fetch metadata
        // 820C3DA8..B4 selects position0, UV0, weights0 and indices0 by both
        // usage and index; neither skinned nor unskinned execution reads UV1.
        if(usage==5&&index==1){
            require(type==0x002C23A5&&!(offset&3)&&offset<=stride&&8<=stride-offset&&!unusedUV1,
                    "Unqualified unused character UV1");
            unusedUV1=true;continue;
        }
        require(!index,"Unqualified character stream-zero semantic index");
        uint32_t slot=0,width=0,expected=0;
        switch(usage) {
        case 0:slot=0;width=12;expected=0x002A23B9;break;
        case 5:slot=1;width=8;expected=0x002C23A5;break;
        case 1:slot=2;width=16;expected=0x001A23A6;break;
        case 2:slot=3;width=4;expected=0x001A2286;break;
        case 3:require(type==0x002A2187&&offset+4<=stride,"Unqualified unused character normal");continue;
        case 10:require(type==0x00182886&&offset+4<=stride,"Unqualified unused character color");continue;
        default:throw Failure("Unqualified character vertex semantic");
        }
        require(type==expected&&offset<=stride&&width<=stride-offset&&!(offset&3)&&!(found&(1u<<slot)),
                "Character shadow input format, offset or uniqueness differs");
        offsets[slot]=offset;found|=1u<<slot;
    }
    require(found==15,"Character declaration lacks a shadow shader input");
    const auto fetchEnd=std::max({offsets[0]+12,offsets[1]+8,offsets[2]+16,offsets[3]+4});
    const auto count=originalFetchedVertexCount(bytes.size(),stride,fetchEnd);
    require(count!=0,"Character vertex owner lacks its first consumed record");
    std::vector<Graphics::ShadowMeshVertex> result(count);
    for(size_t i=0;i<result.size();++i) {
        const auto at=i*stride;auto& v=result[i];
        for(size_t lane=0;lane<3;++lane)v.position[lane]=number(bytes,at+offsets[0]+4*lane);
        for(size_t lane=0;lane<2;++lane)v.uv[lane]=number(bytes,at+offsets[1]+4*lane);
        const auto indices=word(bytes,at+offsets[3]);
        for(size_t lane=0;lane<4;++lane) {
            v.weights[lane]=number(bytes,at+offsets[2]+4*lane);
            const auto index=(indices>>(lane*8))&255;
            require(index<64,"Character vertex bone index exceeds native constant bank");
            require(v.weights[lane]==0||index<bones,"Character weighted bone index exceeds mesh palette");
            v.indices[lane]=float(index);
        }
    }
    return result;
}
std::vector<uint16_t> decodeCharacterIndices(std::span<const uint8_t> bytes) {
    const auto count=originalFetchedIndexCount(bytes.size());
    require(count,"Character index extent has no complete original R16 words");
    std::vector<uint16_t> result(count);
    for(size_t i=0;i<result.size();++i)result[i]=uint16_t(uint16_t(bytes[2*i])<<8|bytes[2*i+1]);
    return result;
}
}
