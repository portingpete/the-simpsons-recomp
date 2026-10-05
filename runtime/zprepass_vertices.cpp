#include "zprepass_vertices.h"
#include "runtime.h"
#include "common/geometry_extent.h"
#include <bit>

namespace Simpsons {
namespace {
void require(bool value,const char* why) {if(!value)throw Failure(why);}
uint32_t word(std::span<const uint8_t> bytes,size_t at) {
    require(at<=bytes.size()&&bytes.size()-at>=4,"Truncated static Z-prepass word");
    return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];
}
}
std::vector<Graphics::ZPrepassVertex> decodeStaticZPrepassVertices(
    std::span<const uint8_t> bytes,std::span<const uint8_t> declaration,uint32_t stride) {
    // Original stream stride is stored as one byte of dwords (8243C6A8/B4).
    require(validOriginalVertexExtent(bytes.size(),stride),
            "Static Z-prepass vertex stride or extent is invalid");
    require(declaration.size()>=24&&declaration.size()%12==0&&declaration.size()<=65*12,
            "Static Z-prepass declaration extent is invalid");
    uint32_t positionOffset=0,found=0;
    for(size_t at=0;at<declaration.size();at+=12) {
        const auto streamOffset=word(declaration,at),type=word(declaration,at+4),semantic=word(declaration,at+8);
        const auto stream=streamOffset>>16,offset=streamOffset&65535;
        if(at+12==declaration.size()) {
            require(streamOffset==0x00FF0000&&type==UINT32_MAX&&semantic==0,
                    "Static Z-prepass declaration terminator differs");
            break;
        }
        require(stream==0&&type!=UINT32_MAX&&(semantic&0xFF0000FF)==0,
                "Unsupported static Z-prepass declaration stream/method/reserved byte");
        const auto usage=(semantic>>16)&255,index=(semantic>>8)&255;
        uint32_t slot=0,width=0,expected=0;
        // Live013's six-row layout. UV0 is optional and just as dead as UV1:
        // this shader has no UV input or output when its Boolean is false.
        if(usage==0&&index==0) {slot=0;width=12;expected=0x002A23B9;}
        else if(usage==3&&index==0) {slot=1;width=4;expected=0x002A2187;}
        else if(usage==10&&index==0) {slot=2;width=4;expected=0x00182886;}
        else if(usage==5&&index==0) {slot=3;width=8;expected=0x002C23A5;}
        else if(usage==5&&index==1) {slot=4;width=8;expected=0x002C23A5;}
        // Live016: stride40, packed tangent0 at16, before color and UV0/1.
        // Its four payload bytes are dead under the original Boolean-false path.
        else if(usage==6&&index==0) {slot=5;width=4;expected=0x002A2187;}
        else throw Failure("Unqualified static Z-prepass vertex semantic");
        require(type==expected&&!(offset&3)&&offset<=stride&&width<=stride-offset&&!(found&(1u<<slot)),
                "Static Z-prepass input format, offset or uniqueness differs");
        if(!slot)positionOffset=offset;
        found|=1u<<slot;
    }
    require(found&1,"Static Z-prepass declaration lacks position0");
    const auto count=originalFetchedVertexCount(bytes.size(),stride,positionOffset+12);
    require(count!=0,"Static Z-prepass vertex owner lacks its first consumed record");
    std::vector<Graphics::ZPrepassVertex> result(count,Graphics::ZPrepassVertex{});
    for(size_t vertex=0;vertex<result.size();++vertex)for(size_t lane=0;lane<3;++lane) {
        const auto bits=word(bytes,vertex*stride+positionOffset+4*lane);
        require((bits&0x7F800000)!=0x7F800000,"Nonfinite static Z-prepass position");
        result[vertex].position[lane]=std::bit_cast<float>(bits);
    }
    return result;
}
}
