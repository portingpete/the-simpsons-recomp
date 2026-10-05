#include "native_texture_stream.h"
#include <algorithm>

namespace Simpsons::Graphics {
namespace {
uint32_t be32(std::span<const uint8_t> bytes,size_t offset) {
    return (uint32_t(bytes[offset])<<24)|(uint32_t(bytes[offset+1])<<16)|
           (uint32_t(bytes[offset+2])<<8)|bytes[offset+3];
}
uint16_t be16(std::span<const uint8_t> bytes,size_t offset) {
    return uint16_t((uint16_t(bytes[offset])<<8)|bytes[offset+1]);
}
std::string name(std::span<const uint8_t> bytes,size_t offset) {
    auto first=bytes.begin()+offset,last=first+32;
    auto end=std::find(first,last,uint8_t(0));
    if(end==last) throw Error("Native texture name lacks its bounded terminator");
    return std::string(first,end);
}
}
SerializedTexture decodeNativeTextureStruct(std::span<const uint8_t> bytes) {
    if(bytes.size()<92) throw Error("Truncated native texture struct header");
    if(be32(bytes,0)!=9) throw Error("Unsupported native texture stream platform");
    SerializedTexture result;
    result.sampler=be32(bytes,4);result.name=name(bytes,8);result.maskName=name(bytes,40);
    result.rasterFormat=be32(bytes,72);result.nativeFormat=be32(bytes,76);
    result.width=be16(bytes,80);result.height=be16(bytes,82);
    result.depthField=bytes[84];result.levels=bytes[85];result.type=bytes[86];result.flags=bytes[87];
    if(result.nativeFormat!=0x1A200154 || result.rasterFormat!=0x300 ||
       result.depthField!=16 || result.levels!=1 || result.type!=4 || result.flags!=9)
        throw Error("Unverified native texture format, mip, type or flag profile");
    if(!result.width||!result.height||(result.width&3)||(result.height&3)||result.width>16384||result.height>16384)
        throw Error("Unverified native compressed texture dimensions");
    uint32_t size=uint32_t(bytes[88])|(uint32_t(bytes[89])<<8)|(uint32_t(bytes[90])<<16)|(uint32_t(bytes[91])<<24);
    uint64_t expected=uint64_t(result.width/4)*(result.height/4)*16;
    if(size!=expected || bytes.size()!=92+expected) throw Error("Native texture level size or struct extent mismatch");
    result.format=TextureFormat::BC3;result.blocks.resize(size);
    // 8-in-16 is the descriptor's observed endian mode. This serial source is
    // linear; applying the runtime console tiling here corrupts the artwork.
    for(size_t i=0;i<size;i+=2) {result.blocks[i]=bytes[92+i+1];result.blocks[i+1]=bytes[92+i];}
    return result;
}
}
