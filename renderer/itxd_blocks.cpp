#include "itxd_blocks.h"
#include "native_backend.h"
#include <algorithm>
#include <bit>

namespace Simpsons::Graphics {
namespace {
std::vector<uint8_t> luminancePixels(uint32_t width,uint32_t height,uint32_t pitch,
        uint32_t originX,uint32_t originY,std::span<const uint8_t> storage) {
    std::vector<uint8_t> rgba(size_t(width)*height*4);
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x){
        const uint32_t tx=x+originX,ty=y+originY;
        const uint32_t macro=((tx>>5)+(ty>>5)*(pitch>>5))<<7;
        const uint32_t micro=(tx&7)+((ty&14)<<2);
        const uint32_t mixed=macro+((micro&~15u)<<1)+(micro&15)+((ty&1)<<4);
        const uint32_t at=((mixed&~511u)<<3)+((ty&16)<<7)+((mixed&448)<<2)+
            (((((ty&8)>>2)+(tx>>3))&3)<<6)+(mixed&63);
        if(at>=storage.size())throw Error("ITXD luminance texel exceeds owned subresource");
        const size_t out=(size_t(y)*width+x)*4;
        rgba[out]=rgba[out+1]=rgba[out+2]=storage[at];rgba[out+3]=255;
    }
    return rgba;
}
}
std::vector<uint8_t> decodeITXDLuminanceBase(const std::array<uint32_t,6>& d,
        uint32_t width,uint32_t height,std::span<const uint8_t> storage) {
    if(width<64 || height<64 || width>2048 || height>2048 ||
       !std::has_single_bit(width) || !std::has_single_bit(height))
        throw Error("ITXD L8 dimensions exceed the qualified power-of-two base profile");
    const std::array<uint32_t,6> expected={0x80000002u|((width/32)<<22),2,((height-1)<<13)|(width-1),0x1400,0,0x200};
    if(d!=expected || storage.size()!=size_t(width)*height)
        throw Error("ITXD L8 descriptor/allocation differs from tiled RRR1 base storage");
    // The descriptor pitch equals the authored width. L8 has one byte per
    // texel, no endian exchange, and RRR1 component selection. The 64x64
    // texture is one member of this layout, not a fixed-size GPU format.
    return luminancePixels(width,height,width,0,0,storage);
}
std::vector<std::vector<uint8_t>> decodeITXDLuminanceMipChain(const std::array<uint32_t,6>& d,
        uint32_t width,uint32_t height,std::span<const uint8_t> storage) {
    // Preserve the existing base admission and exact allocation contract.
    if(d[4]==0) {
        std::vector<std::vector<uint8_t>> result;
        result.push_back(decodeITXDLuminanceBase(d,width,height,storage));return result;
    }
    if(width<64 || height<64 || width>2048 || height>2048 ||
       !std::has_single_bit(width) || !std::has_single_bit(height))
        throw Error("ITXD L8 mip dimensions exceed the qualified power-of-two profile");
    const uint32_t maxLevel=(d[4]>>6)&15,logW=std::bit_width(width)-1,logH=std::bit_width(height)-1;
    const uint32_t packedLevel=std::min(logW,logH)-4;
    const std::array<uint32_t,6> expected={0x80000002u|((width/32)<<22),2,
        ((height-1)<<13)|(width-1),0x1400,maxLevel<<6,(width*height)|0xA00u};
    if(d!=expected || !maxLevel || maxLevel>std::max(logW,logH))
        throw Error("ITXD L8 mip descriptor has unqualified layout, address or fetch controls");
    // One-byte tiled mips can address beyond a tightly packed 32x32 byte
    // rectangle. Original beam1 stores each separate subresource in 4KB.
    // Subsequent levels from the first 16-texel dimension share one tail.
    const uint32_t lastStored=std::min(maxLevel,packedLevel);
    std::vector<size_t> starts(lastStored+1),sizes(lastStored+1);
    std::vector<uint32_t> pitches(lastStored+1);
    size_t required{};
    for(uint32_t level=0;level<=lastStored;++level) {
        pitches[level]=std::max(32u,width>>level);
        sizes[level]=(size_t(pitches[level])*std::max(32u,height>>level)+4095)&~size_t(4095);
        starts[level]=required;required+=sizes[level];
    }
    if(storage.size()!=required || (d[5]&0xFFFFF000u)!=sizes[0])
        throw Error("ITXD L8 mip allocation extent or address differs from its descriptor");
    std::vector<std::vector<uint8_t>> result;result.reserve(size_t(maxLevel)+1);
    for(uint32_t level=0;level<=maxLevel;++level) {
        const uint32_t stored=std::min(level,packedLevel);
        uint32_t originX{},originY{};
        if(level>=packedLevel) {
            const uint32_t relative=level-packedLevel;
            if(relative<3) {
                if(logW>logH)originY=16u>>relative;else originX=16u>>relative;
            } else {
                if(logW>logH)originX=(1u<<(logW-packedLevel))>>(relative-2);
                else originY=(1u<<(logH-packedLevel))>>(relative-2);
            }
        }
        result.push_back(luminancePixels(std::max(1u,width>>level),std::max(1u,height>>level),
            pitches[stored],originX,originY,storage.subspan(starts[stored],sizes[stored])));
    }
    return result;
}
namespace {
std::vector<uint8_t> decodeBase(const std::array<uint32_t,6>& d,uint32_t format,
                              uint32_t width,uint32_t height,std::span<const uint8_t> storage) {
    if(!width || !height || width>2048 || height>2048 || (width&3) || (height&3) ||
       (width&(width-1)) || (height&(height-1)))
        throw Error("ITXD BC2/BC3 base dimensions exceed the qualified power-of-two profile");
    // Stored signs/clamps/numeric and fetch control bits must be the recovered
    // profile. Runtime-patched address words are explicitly not file offsets.
    if((d[0]&0x803FFFFFu)!=0x80000002u || d[1]!=format ||
       d[2]!=((height-1)<<13 | (width-1)) || d[3]!=0xD10 || d[4] || d[5]!=0x200)
        throw Error("ITXD BC2/BC3 base descriptor has unqualified format, address, mip or sampler controls");
    const uint32_t pitch=((d[0]>>22)&511)*32;
    if(pitch<width || pitch%128 || pitch>2048)
        throw Error("ITXD BC2/BC3 tiled pitch is outside the qualified range");
    const uint32_t columns=width/4,rows=height/4,pitchBlocks=pitch/4;
    const size_t allocation=size_t(pitchBlocks)*((rows+31)&~31u)*16;
    if(storage.size()!=allocation)
        throw Error("ITXD BC2/BC3 base allocation extent differs from its descriptor");
    std::vector<uint8_t> result(size_t(columns)*rows*16);
    for(uint32_t y=0;y<rows;++y)for(uint32_t x=0;x<columns;++x) {
        // Original/reference 2D tiled address permutation, in 16-byte blocks.
        // BC2 and BC3 share storage addressing, but retain distinct formats.
        // Keep every address bounded by the caller-owned payload allocation.
        const uint32_t macro=((x>>5)+(y>>5)*(pitchBlocks>>5))<<11;
        const uint32_t micro=((x&7)+((y&14)<<2))<<4;
        const uint32_t mixed=macro+((micro&~15u)<<1)+(micro&15)+((y&1)<<4);
        const uint32_t at=((mixed&~511u)<<3)+((y&16)<<7)+((mixed&448)<<2)+
            (((((y&8)>>2)+(x>>3))&3)<<6)+(mixed&63);
        if(size_t(at)+16>storage.size())throw Error("ITXD BC2/BC3 tiled block exceeds its owned allocation");
        const size_t out=(size_t(y)*columns+x)*16;
        for(uint32_t i=0;i<16;++i)result[out+i]=storage[at+(i^1)];
    }
    return result;
}
}
std::vector<uint8_t> decodeITXDBC2Base(const std::array<uint32_t,6>& d,
                                    uint32_t width,uint32_t height,std::span<const uint8_t> storage) {
    return decodeBase(d,0x53,width,height,storage);
}
std::vector<uint8_t> decodeITXDBC3Base(const std::array<uint32_t,6>& d,
                                    uint32_t width,uint32_t height,std::span<const uint8_t> storage) {
    return decodeBase(d,0x54,width,height,storage);
}
std::vector<std::vector<uint8_t>> decodeITXDBCMipChain(const std::array<uint32_t,6>& d,
        uint32_t format,uint32_t width,uint32_t height,std::span<const uint8_t> storage) {
    if(width<4 || height<4 || width>2048 || height>2048 ||
       !std::has_single_bit(width) || !std::has_single_bit(height))
        throw Error("ITXD BC chain dimensions exceed the qualified power-of-two profile");
    const uint32_t maxLevel=(d[4]>>6)&15,logW=std::bit_width(width)-1,logH=std::bit_width(height)-1;
    const uint32_t packedLevel=std::min(logW,logH)>4?std::min(logW,logH)-4:0;
    if((format!=0x52 && format!=0x53 && format!=0x54) ||
       (d[0]&0x803FFFFFu)!=0x80000002u || d[1]!=format ||
       d[2]!=((height-1)<<13 | (width-1)) || d[3]!=0xD10 || d[4]!=(maxLevel<<6) ||
       maxLevel>std::max(logW,logH) || (maxLevel && !packedLevel) ||
       (maxLevel ? (d[5]&0xFFF)!=0xA00 : d[5]!=0x200))
        throw Error("ITXD BC chain descriptor has unqualified format, address, mip or sampler controls");
    const uint32_t pitch=((d[0]>>22)&511)*32,blockBytes=format==0x52?8u:16u;
    if(pitch<width || pitch%128 || pitch>2048)
        throw Error("ITXD BC chain tiled pitch is outside the qualified range");
    // Recovered asset storage layout: base uses its stored pitch, separate mips
    // use power-of-two level widths, 32x32-block tiles and 4KB subresources.
    // The tail begins when either extent reaches 16 texels. Its sublevels share
    // a tile, with offsets expressed in texels then divided by the BC block size.
    const uint32_t lastStored=maxLevel?std::min(maxLevel,packedLevel):0;
    std::vector<size_t> starts(lastStored+1),sizes(lastStored+1);
    std::vector<uint32_t> pitches(lastStored+1);
    size_t total{};
    for(uint32_t level=0;level<=lastStored;++level) {
        const auto w=level?std::max(1u,width>>level):pitch,h=std::max(1u,height>>level);
        pitches[level]=(((w+3)/4)+31)&~31u;
        sizes[level]=(size_t(pitches[level])*((((h+3)/4)+31)&~31u)*blockBytes+4095)&~size_t(4095);
        starts[level]=total;total+=sizes[level];
    }
    if((maxLevel && (d[5]&0xFFFFF000u)!=sizes[0]) || storage.size()!=total)
        throw Error("ITXD BC chain allocation extent or mip address differs from its descriptor");
    std::vector<std::vector<uint8_t>> result;result.reserve(size_t(maxLevel)+1);
    for(uint32_t level=0;level<=maxLevel;++level) {
        const uint32_t stored=maxLevel?std::min(level,packedLevel):0;
        uint32_t originX{},originY{};
        if(maxLevel && level>=packedLevel) {
            const auto relative=level-packedLevel;
            if(relative<3) {
                if(logW>logH)originY=16u>>relative;else originX=16u>>relative;
            } else {
                if(logW>logH)originX=(1u<<(logW-packedLevel))>>(relative-2);
                else originY=(1u<<(logH-packedLevel))>>(relative-2);
            }
            originX/=4;originY/=4;
        }
        const auto columns=(std::max(1u,width>>level)+3)/4,rows=(std::max(1u,height>>level)+3)/4;
        auto& output=result.emplace_back(size_t(columns)*rows*blockBytes);
        for(uint32_t y=0;y<rows;++y)for(uint32_t x=0;x<columns;++x) {
            const auto tx=x+originX,ty=y+originY;
            const auto log=std::countr_zero(blockBytes);
            const uint32_t macro=((tx>>5)+(ty>>5)*(pitches[stored]>>5))<<(log+7);
            const uint32_t micro=((tx&7)+((ty&14)<<2))<<log;
            const uint32_t mixed=macro+((micro&~15u)<<1)+(micro&15)+((ty&1)<<4);
            const uint32_t at=((mixed&~511u)<<3)+((ty&16)<<7)+((mixed&448)<<2)+
                (((((ty&8)>>2)+(tx>>3))&3)<<6)+(mixed&63);
            if(size_t(at)+blockBytes>sizes[stored])throw Error("ITXD BC mip block exceeds its owned subresource");
            const size_t out=(size_t(y)*columns+x)*blockBytes;
            for(uint32_t i=0;i<blockBytes;++i)output[out+i]=storage[starts[stored]+at+(i^1)];
        }
    }
    return result;
}
std::vector<std::vector<uint8_t>> decodeITXDCandyRGBA8(const std::array<uint32_t,6>& d,
        uint32_t width,uint32_t height,std::span<const uint8_t> storage) {
    // Authored moh_palette_dark_w_s is a 16x16 base image in one 32x32
    // tiled allocation. It has no packed tail or additional mip levels.
    const bool smallBase=width==16&&height==16&&d[4]==0;
    if((!smallBase&&(width<32||height<32))||width>1024||height>1024||
       !std::has_single_bit(width)||!std::has_single_bit(height))
        throw Error("ITXD RGBA8 dimensions exceed the qualified power-of-two family");
    const uint32_t maxLevel=(d[4]>>6)&15,packedLevel=std::min(std::bit_width(width),std::bit_width(height))-5;
    const uint32_t pitch=((d[0]>>22)&511)*32;
    const uint32_t basePitch=std::max(32u,width);
    // Authored base and separate mips through the first packed 16-texel level.
    // Deeper shared tails, padded base pitch, numeric/sign/swizzle variants,
    // nonzero base addresses, arrays and cubes are not admitted here.
    if(d[0]!=(0x80000002u|((basePitch/32)<<22))||pitch!=basePitch||d[1]!=0x86||
       d[2]!=((height-1)<<13|(width-1))||d[3]!=0xC14||d[4]!=(maxLevel<<6)||
       maxLevel>packedLevel||d[5]!=(maxLevel?(width*height*4|0xA00u):0x200u))
        throw Error("ITXD RGBA8 descriptor has unqualified layout or fetch controls");
    const uint32_t levels=maxLevel+1;
    size_t required{};
    for(uint32_t level=0;level<levels;++level)
        required+=size_t(std::max(32u,width>>level))*std::max(32u,height>>level)*4;
    if(storage.size()!=required)throw Error("ITXD RGBA8 allocation differs from descriptor mip extents");
    std::vector<std::vector<uint8_t>> result;
    size_t start{};
    for(uint32_t level=0;level<levels;++level) {
        const uint32_t w=width>>level,h=height>>level,levelPitch=std::max(32u,w);
        const size_t allocation=size_t(levelPitch)*std::max(32u,h)*4;
        auto& pixels=result.emplace_back(size_t(w)*h*4);
        for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x) {
            const bool packed=maxLevel&&level==packedLevel;
            const uint32_t tx=x+(packed&&width<=height?16u:0u),ty=y+(packed&&width>height?16u:0u);
            const uint32_t macro=((tx>>5)+(ty>>5)*(levelPitch>>5))<<9;
            const uint32_t micro=((tx&7)+((ty&14)<<2))<<2;
            const uint32_t mixed=macro+((micro&~15u)<<1)+(micro&15)+((ty&1)<<4);
            const uint32_t at=((mixed&~511u)<<3)+((ty&16)<<7)+((mixed&448)<<2)+
                (((((ty&8)>>2)+(tx>>3))&3)<<6)+(mixed&63);
            if(size_t(at)+4>allocation || start+allocation>storage.size())
                throw Error("ITXD RGBA8 texel exceeds its owned mip allocation");
            constexpr std::array<uint32_t,4> lanes={1,2,3,0};
            for(uint32_t lane=0;lane<4;++lane)
                pixels[(size_t(y)*w+x)*4+lane]=storage[start+at+lanes[lane]];
        }
        start+=allocation;
    }
    return result;
}
std::vector<uint8_t> decodeITXDPaletteRGBA8(const std::array<uint32_t,6>& d,
        uint32_t width,uint32_t height,std::span<const uint8_t> storage) {
    constexpr std::array<uint32_t,6> expected={0x80800002,0x86,0x7E03F,0xC14,0,0x200};
    if(d!=expected || width!=64 || height!=64 || storage.size()!=16384)
        throw Error("ITXD palette exceeds the exact 64x64,8-in-32,ZYXW tiled profile");
    std::vector<uint8_t> result(16384);
    for(uint32_t y=0;y<64;++y)for(uint32_t x=0;x<64;++x){
        const uint32_t macro=((x>>5)+(y>>5)*2)<<9;
        const uint32_t micro=((x&7)+((y&14)<<2))<<2;
        const uint32_t mixed=macro+((micro&~15u)<<1)+(micro&15)+((y&1)<<4);
        const uint32_t at=((mixed&~511u)<<3)+((y&16)<<7)+((mixed&448)<<2)+
            (((((y&8)>>2)+(x>>3))&3)<<6)+(mixed&63);
        if(size_t(at)+4>storage.size())throw Error("Palette texel exceeds original allocation");
        const size_t out=(size_t(y)*64+x)*4;
        // Endian8-in-32, followed by the descriptor's ZYXW channel selection.
        constexpr std::array<uint32_t,4> lanes={1,2,3,0};
        for(uint32_t lane=0;lane<4;++lane)result[out+lane]=storage[at+lanes[lane]];
    }
    return result;
}
}
