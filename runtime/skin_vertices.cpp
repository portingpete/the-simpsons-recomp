#include "skin_vertices.h"
#include "skin_profile.h"
#include "runtime.h"
#include "common/geometry_extent.h"
#include <algorithm>
#include <bit>
#include <bitset>

namespace Simpsons {
namespace {
void require(bool value,const char* why) {if(!value)throw Failure(why);}
uint32_t word(std::span<const uint8_t> bytes,size_t at) {
    require(at<=bytes.size()&&bytes.size()-at>=4,"Truncated skin vertex word");
    return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];
}
float number(std::span<const uint8_t> bytes,size_t at) {
    const auto bits=word(bytes,at);
    require((bits&0x7F800000)!=0x7F800000,"Nonfinite skin vertex attribute");
    return std::bit_cast<float>(bits);
}
}
uint64_t skinMorphStreamDirtyMask(uint32_t stream) {
    require(stream>=1&&stream<=6,"Skin morph stream is outside1..6");
    // Original826FF41C..430 computes a 64-bit fetch-group dirty mask.
    const uint32_t shift=(((95-stream)*21846u)>>16)+32;
    return (uint64_t(1)<<63)>>shift;
}
void decodeSkinMorphStream(std::span<Graphics::SkinVertex> vertices,uint32_t stream,std::span<const uint8_t> bytes) {
    (void)skinMorphStreamDirtyMask(stream);
    require(!vertices.empty()&&vertices.size()<=SIZE_MAX/12&&bytes.size()>=vertices.size()*12&&
            bytes.size()<=originalGeometryMaxVertexBytes&&!(bytes.size()&3),
            "Skin morph extent differs from its base vertex count");
    std::vector<std::array<float,3>> values(vertices.size());
    for(size_t i=0;i<vertices.size();++i)for(size_t lane=0;lane<3;++lane)values[i][lane]=number(bytes,i*12+4*lane);
    // Publish only after every component is finite and the complete span fits.
    for(size_t i=0;i<vertices.size();++i) {
        auto& v=vertices[i];const std::array<std::array<float,3>*,6> rows{&v.morph1,&v.morph2,&v.morph3,&v.morph4,&v.morph5,&v.morph6};
        *rows[stream-1]=values[i];
    }
}
std::vector<Graphics::SkinVertex> decodeSkinVertices(
    std::span<const uint8_t> bytes,std::span<const uint8_t> declaration,uint32_t stride,uint32_t source,bool alpha) {
    const auto profile=skinProfile(source);
    const bool optionalUV1=alpha&&(profile.gloss||profile.flipbook);
    const bool declaredUV1=profile.dual||profile.gloss||profile.flipbook||profile.dualUv;
    const bool consumedUV1=skinConsumesUV1(source,alpha);
    const bool shortAlpha=optionalUV1&&declaration.size()==156;
    // Original826FF398 binds geometry+4;8243C6A8/B4 retains its DWORD
    // stride in one byte. The observed48/56 layouts are not format limits.
    require(validOriginalVertexExtent(bytes.size(),stride),
        "Skin vertex stride or extent is invalid");
    require((shortAlpha||declaration.size()==(declaredUV1?168u:156u))&&declaration.size()%12==0,
        "Skin declaration extent is invalid");
    // Verify stream0 rows (6 attributes) plus morph streams1..6 plus terminator.
    // Row order from capture: pos0, normal3/0, uv5/0, indices2/0, weights1/0,
    // color10/0, morph0/1..6, terminator.
    std::array<uint32_t,7> offsets{};uint32_t found=0,morphs=0;std::bitset<1020> occupied;
    for(size_t at=0;at<declaration.size();at+=12) {
        const auto streamOffset=word(declaration,at),type=word(declaration,at+4),semantic=word(declaration,at+8);
        const auto stream=streamOffset>>16,offset=streamOffset&65535;
        if(at+12==declaration.size()) {
            require(streamOffset==0x00FF0000&&type==UINT32_MAX&&semantic==0,"Skin declaration terminator differs");break;
        }
        if(stream!=0) {
            // Morph streams1..6: usage0/index==stream, type002A23B9, offset0.
            const auto usage=(semantic>>16)&255,index=(semantic>>8)&255;
            require(stream>=1&&stream<=6&&usage==0&&index==stream&&offset==0&&type==0x002A23B9&&
                    !(semantic&0xFF0000FF)&&!(morphs&(1u<<stream)),"Unqualified skin morph declaration");
            morphs|=1u<<stream;continue;
        }
        require(stream==0&&type!=UINT32_MAX&&(semantic&0xFF0000FF)==0,
            "Unqualified skin declaration stream/method");
        const auto usage=(semantic>>16)&255,index=(semantic>>8)&255;
        uint32_t slot=0,width=0,expected=0;
        if(usage==0&&index==0){slot=0;width=12;expected=0x002A23B9;}
        else if(usage==3&&index==0){slot=1;width=4;expected=0x002A2187;}
        else if(usage==5&&index==0){slot=2;width=8;expected=0x002C23A5;}
        else if(usage==2&&index==0){slot=3;width=4;expected=0x001A2286;}
        else if(usage==1&&index==0){slot=4;width=16;expected=0x001A23A6;}
        else if(usage==10&&index==0){slot=5;width=4;expected=0x00182886;}
        else if(declaredUV1&&usage==5&&index==1){slot=6;width=8;expected=0x002C23A5;}
        else throw Failure("Unqualified skin vertex semantic");
        require(type==expected&&!(offset&3)&&offset<=stride&&width<=stride-offset&&!(found&(1u<<slot)),
            "Skin input format, offset or uniqueness differs");
        offsets[slot]=offset;
        // A64-bit byte mask would shift out of range for valid relocated
        // attributes. Check the full representable original stream instead.
        for(uint32_t byte=offset;byte<offset+width;++byte) {
            require(!occupied.test(byte),"Overlapping skin vertex attributes");occupied.set(byte);
        }
        found|=1u<<slot;
    }
    require(found==(declaredUV1&&!shortAlpha?127u:63u)&&morphs==126,"Skin declaration lacks a consumed shader input");
    auto fetchEnd=std::max({offsets[0]+12,offsets[1]+4,offsets[2]+8,offsets[3]+4,offsets[4]+16,offsets[5]+4});
    if(consumedUV1)fetchEnd=std::max(fetchEnd,offsets[6]+8);
    const auto count=originalFetchedVertexCount(bytes.size(),stride,fetchEnd);
    require(count!=0,"Skin vertex owner lacks its first consumed record");
    std::vector<Graphics::SkinVertex> result(count);
    for(size_t i=0;i<result.size();++i) {
        const auto at=i*stride;auto& v=result[i];
        for(size_t lane=0;lane<3;++lane)v.position[lane]=number(bytes,at+offsets[0]+4*lane);
        const auto packed=word(bytes,at+offsets[1]);
        for(size_t lane=0;lane<3;++lane) {
            const auto raw=(packed>>(10*lane))&1023;
            const auto sv=int32_t(raw)-(raw&512?1024:0);
            v.normal[lane]=std::max(-1.0f,float(sv)/511.0f);
        }
        for(size_t lane=0;lane<2;++lane)v.uv[lane]=number(bytes,at+offsets[2]+4*lane);
        if(consumedUV1)for(size_t lane=0;lane<2;++lane)v.uv1[lane]=number(bytes,at+offsets[6]+4*lane);
        const auto idx=word(bytes,at+offsets[3]);
        // Declaration001A2286 is unsigned byte4 with XYZW selectors. After
        // the stream's 8-in-32 conversion, X occupies the low byte: original
        // bytes 00 00 00 09 select bone9 for weightX. Match the shadow decoder;
        // the shader's separate WZYX fetch swizzle still applies exactly once.
        v.indices={float(idx&255),float((idx>>8)&255),float((idx>>16)&255),float((idx>>24)&255)};
        for(size_t lane=0;lane<4;++lane)v.weights[lane]=number(bytes,at+offsets[4]+4*lane);
        const auto color=word(bytes,at+offsets[5]);
        v.color={float((color>>16)&255)/255.0f,float((color>>8)&255)/255.0f,
                 float(color&255)/255.0f,float(color>>24)/255.0f};
        v.morph1={0,0,0};v.morph2={0,0,0};v.morph3={0,0,0};
        v.morph4={0,0,0};v.morph5={0,0,0};v.morph6={0,0,0};
        for(float w:v.weights)require(std::isfinite(w),"Nonfinite skin weight");
        for(float b:v.indices)require(std::isfinite(b)&&b>=0&&b<=63&&std::floor(b)==b,"Skin bone index not integer 0..63");
    }
    return result;
}
}
