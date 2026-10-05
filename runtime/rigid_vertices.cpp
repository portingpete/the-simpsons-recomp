#include "rigid_vertices.h"
#include "runtime.h"
#include "common/geometry_extent.h"
#include <algorithm>
#include <bit>

namespace Simpsons {
namespace {
void require(bool value,const char* why) {if(!value)throw Failure(why);}
uint32_t word(std::span<const uint8_t> bytes,size_t at) {
    require(at<=bytes.size()&&bytes.size()-at>=4,"Truncated rigid vertex word");
    return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];
}
float number(std::span<const uint8_t> bytes,size_t at) {
    const auto bits=word(bytes,at);
    require((bits&0x7F800000)!=0x7F800000,"Nonfinite rigid vertex attribute");
    return std::bit_cast<float>(bits);
}
}
std::vector<Graphics::RigidVertex> decodeRigidVertices(std::span<const uint8_t> bytes,
    std::span<const uint8_t> declaration,uint32_t stride,bool consumeUv1,bool consumeTangent,bool consumeNormal) {
    require(validOriginalVertexExtent(bytes.size(),stride),
        "Rigid vertex stride or extent is invalid");
    require(declaration.size()>=(consumeNormal?60u:48u)&&declaration.size()%12==0&&declaration.size()<=65*12,
        "Rigid declaration extent is invalid");
    std::array<uint32_t,6> offsets{};uint32_t found=0;
    for(size_t at=0;at<declaration.size();at+=12) {
        const auto streamOffset=word(declaration,at),type=word(declaration,at+4),semantic=word(declaration,at+8);
        const auto stream=streamOffset>>16,offset=streamOffset&65535;
        if(at+12==declaration.size()) {
            require(streamOffset==0x00FF0000&&type==UINT32_MAX&&semantic==0,"Rigid declaration terminator differs");break;
        }
        require(stream==0&&type!=UINT32_MAX&&(semantic&0xFF0000FF)==0,
            "Unqualified rigid declaration stream, method or reserved byte");
        const auto usage=(semantic>>16)&255,index=(semantic>>8)&255;
        uint32_t slot=0,width=0,expected=0;
        if(usage==0&&index==0){slot=0;width=12;expected=0x002A23B9;}
        else if(usage==3&&index==0){slot=1;width=4;expected=0x002A2187;}
        else if(usage==10&&index==0){slot=2;width=4;expected=0x00182886;}
        else if(usage==5&&index==0){slot=3;width=8;expected=0x002C23A5;}
        else if(usage==5&&index==1){slot=4;width=8;expected=0x002C23A5;}
        else if(usage==6&&index==0){slot=5;width=4;expected=0x002A2187;}
        else throw Failure("Unqualified rigid vertex semantic");
        require(type==expected&&!(offset&3)&&offset<=stride&&width<=stride-offset&&!(found&(1u<<slot)),
            "Rigid input format, offset or uniqueness differs");
        offsets[slot]=offset;
        found|=1u<<slot;
    }
    const uint32_t required=consumeNormal?15u:13u;
    require((found&required)==required,"Rigid declaration lacks a consumed shader input");
    if(consumeUv1)require(found&16,"UV1 consumption requires a UV1 declaration");
    if(consumeTangent)require(found&32,"Tangent consumption requires a tangent declaration");
    auto fetchEnd=std::max({offsets[0]+12,offsets[2]+4,offsets[3]+8});
    if(consumeNormal)fetchEnd=std::max(fetchEnd,offsets[1]+4);
    if(consumeUv1)fetchEnd=std::max(fetchEnd,offsets[4]+8);
    if(consumeTangent)fetchEnd=std::max(fetchEnd,offsets[5]+4);
    const auto count=originalFetchedVertexCount(bytes.size(),stride,fetchEnd);
    require(count!=0,"Rigid vertex owner lacks its first consumed record");
    std::vector<Graphics::RigidVertex> result(count);
    for(size_t i=0;i<result.size();++i) {
        const auto at=i*stride;auto& v=result[i];
        for(size_t lane=0;lane<3;++lane)v.position[lane]=number(bytes,at+offsets[0]+4*lane);
        // Declaration 002A2187 supplies format7/sign1/integer0, XYZ1.
        // Original VS fetch5 preserves signed_rf_mode=0 (b14); SDK patch
        // 8245EFB8 masks with BFC0CFFF, retaining it. This mode has two -1
        // encodings. The high two bits are dead under the XYZ fetch mask.
        if(consumeNormal)for(size_t lane=0;lane<3;++lane) {
            const auto packed=word(bytes,at+offsets[1]);
            const auto raw=(packed>>(10*lane))&1023;
            const auto signedValue=int32_t(raw)-(raw&512?1024:0);
            v.normal[lane]=std::max(-1.0f,float(signedValue)/511.0f);
        }
        const auto color=word(bytes,at+offsets[2]);
        // 00182886 maps normalized byte4 through ZYXW after 8-in-32 endian.
        v.color={float((color>>16)&255)/255.0f,float((color>>8)&255)/255.0f,
                 float(color&255)/255.0f,float(color>>24)/255.0f};
        for(size_t lane=0;lane<2;++lane)v.uv[lane]=number(bytes,at+offsets[3]+4*lane);
        if(consumeUv1&&(found&16))for(size_t lane=0;lane<2;++lane)v.uv1[lane]=number(bytes,at+offsets[4]+4*lane);
        // Tangent uses the same 002A2187 packed 10-bit signed XYZ as normals.
        // High two bits are dead under the XYZ fetch mask; any value is
        // accepted and must not affect XYZ. Handedness is not inferred.
        if(consumeTangent&&(found&32)) {
            const auto packedTangent=word(bytes,at+offsets[5]);
            for(size_t lane=0;lane<3;++lane) {
                const auto raw=(packedTangent>>(10*lane))&1023;
                const auto signedValue=int32_t(raw)-(raw&512?1024:0);
                v.tangent[lane]=std::max(-1.0f,float(signedValue)/511.0f);
            }
        }
    }
    return result;
}
}
