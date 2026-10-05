#include "renderer/itxd_blocks.h"
#include "renderer/native_backend.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <algorithm>
using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void need(bool value,const char* why){++checks;if(!value)throw Error(why);}
std::vector<uint8_t> read(const char* path){std::ifstream f(path,std::ios::binary);if(!f)throw Error("Missing original luminance fixture");return {std::istreambuf_iterator<char>(f),{}};}
template<class F>void rejects(F f){try{f();}catch(const Error&){++checks;return;}throw Error("Unqualified luminance profile accepted");}
void mipFamily(const char* path,bool hardware) {
    const auto data=read(path);size_t at{};
    auto word=[&](){need(at+4<=data.size(),"Truncated luminance mip fixture header");
        uint32_t value{};std::memcpy(&value,data.data()+at,4);at+=4;return value;};
    need(word()==0x314D384Cu,"Luminance mip fixture signature differs");
    const auto count=word();need(count==8,"Luminance mip fixture coverage count differs");
    NativeBackend backend(!hardware);uint32_t uploaded{};
    bool originalBeam=false,landscapeTail=false,portraitTail=false,deepTail=false;
    for(uint32_t n=0;n<count;++n) {
        const auto width=word(),height=word(),storageSize=word();
        std::array<uint32_t,6> descriptor{};for(auto& value:descriptor)value=word();
        const auto maxLevel=(descriptor[4]>>6)&15;
        need(width>=64&&width<=2048&&height>=64&&height<=2048&&maxLevel<=11,
             "Luminance mip fixture exceeds qualified bounds");
        need(storageSize&&storageSize<=data.size()-at,"Truncated luminance mip fixture storage");
        const auto tiled=std::span(data).subspan(at,storageSize);at+=storageSize;
        const auto decoded=decodeITXDLuminanceMipChain(descriptor,width,height,tiled);
        need(decoded.size()==size_t(maxLevel)+1,"Luminance authored mip count differs");
        std::vector<std::span<const uint8_t>> views;
        for(uint32_t level=0;level<=maxLevel;++level) {
            const size_t bytes=size_t(std::max(1u,width>>level))*std::max(1u,height>>level)*4;
            need(bytes<=data.size()-at,"Truncated independent luminance mip pixels");
            const auto expected=std::span(data).subspan(at,bytes);at+=bytes;
            need(decoded[level].size()==bytes,"Luminance mip pixel extent differs");
            for(size_t i=0;i<bytes;++i)need(decoded[level][i]==expected[i],"Luminance mip texel/RRR1 differs from inverse-address oracle");
            views.emplace_back(decoded[level]);
        }
        const auto texture=backend.createTextureMipChain(width,height,TextureFormat::RGBA8,views);
        need(texture->levelCount()==maxLevel+1,"Native luminance chain dropped authored levels");
        for(uint32_t level=0;level<=maxLevel;++level) {
            need(backend.readbackMip(texture,level)==decoded[level],"Native luminance mip upload/readback differs");++uploaded;
        }
        rejects([&]{backend.readbackMip(texture,maxLevel+1);});
        // Preserve the original strict base API: mip allocation is never
        // silently truncated to a base prefix and missing mips never generated.
        if(maxLevel)rejects([&]{decodeITXDLuminanceBase(descriptor,width,height,tiled);});
        else need(decoded[0]==decodeITXDLuminanceBase(descriptor,width,height,tiled),"Base API and chain API differ");
        for(size_t field=0;field<6;++field)for(unsigned bit=0;bit<32;++bit) {
            // Bits6..9 select a supported mip count. They may describe a
            // different valid subset of the same packed tail allocation.
            if(field==4&&bit>=6&&bit<=9)continue;
            auto bad=descriptor;bad[field]^=1u<<bit;
            rejects([&]{decodeITXDLuminanceMipChain(bad,width,height,tiled);});
        }
        auto badCount=descriptor;badCount[4]=15u<<6;
        rejects([&]{decodeITXDLuminanceMipChain(badCount,width,height,tiled);});
        for(auto bad:{0u,32u,96u,4096u,0xFFFFFFFFu}) {
            rejects([&]{decodeITXDLuminanceMipChain(descriptor,bad,height,tiled);});
            rejects([&]{decodeITXDLuminanceMipChain(descriptor,width,bad,tiled);});
        }
        rejects([&]{decodeITXDLuminanceMipChain(descriptor,width,height,tiled.first(storageSize-1));});
        std::vector<uint8_t> large(tiled.begin(),tiled.end());large.push_back(0);
        rejects([&]{decodeITXDLuminanceMipChain(descriptor,width,height,large);});
        auto changed=std::vector<uint8_t>(tiled.begin(),tiled.end());changed[0]^=0x5A;
        const auto altered=decodeITXDLuminanceMipChain(descriptor,width,height,changed);
        for(size_t level=0;level<altered.size();++level)for(size_t i=0;i<altered[level].size();++i)
            need(altered[level][i]==uint8_t(decoded[level][i]^(!level&&i<3?0x5A:0)),
                 "Base-byte mutation changed another level, texel or alpha channel");
        if(n==0) {
            need(width==64&&height==64&&storageSize==12288&&maxLevel==2&&
                 descriptor==std::array<uint32_t,6>{0x80800002,2,0x7E03F,0x1400,0x80,0x1A00},
                 "Original beam1 mip descriptor/allocation changed");originalBeam=true;
        }
        if(maxLevel>=5){deepTail=true;if(width>height)landscapeTail=true;if(height>width)portraitTail=true;}
    }
    need(originalBeam&&landscapeTail&&portraitTail&&deepTail&&uploaded==70,
         "Luminance mip fixtures miss original, mip levels or packed-tail orientations");
    need(at==data.size()&&read(path)==data,"Luminance mip fixture has trailing bytes or was modified");
    std::printf("PASS %zu L8 mip pixel/bounds/descriptor checks, %u cases and %u %s level readbacks\n",checks,count,uploaded,hardware?"hardware":"WARP");
}
void family(const char* path,bool hardware) {
    const auto data=read(path);size_t at{};
    auto word=[&](){need(at+4<=data.size(),"Truncated luminance fixture header");
        uint32_t value{};std::memcpy(&value,data.data()+at,4);at+=4;return value;};
    const auto count=word();need(count==10,"Luminance family case count differs");
    NativeBackend backend(!hardware);
    for(uint32_t n=0;n<count;++n) {
        const auto width=word(),height=word();std::array<uint32_t,6> descriptor{};for(auto& value:descriptor)value=word();
        need(width>=64&&width<=2048&&height>=64&&height<=2048,"Fixture dimensions exceed bounds");
        const size_t size=size_t(width)*height;need(size*5<=data.size()-at,"Truncated luminance fixture storage/pixels");
        const auto tiled=std::span(data).subspan(at,size),expected=std::span(data).subspan(at+size,size*4);at+=size*5;
        const auto pixels=decodeITXDLuminanceBase(descriptor,width,height,tiled);
        const auto chain=decodeITXDLuminanceMipChain(descriptor,width,height,tiled);
        need(chain.size()==1&&chain[0]==pixels,"Existing base-only luminance changed through chain API");
        need(pixels.size()==expected.size(),"Luminance family output extent differs");
        for(size_t i=0;i<pixels.size();++i)need(pixels[i]==expected[i],"Luminance family texel/channel differs from inverse-address oracle");
        const auto texture=backend.createTexture(width,height,TextureFormat::RGBA8,pixels);
        need(backend.readback(texture)==pixels,"Luminance family GPU upload/readback differs");
        for(size_t field=0;field<6;++field)for(unsigned bit=0;bit<32;++bit) {
            auto bad=descriptor;bad[field]^=1u<<bit;
            rejects([&]{decodeITXDLuminanceBase(bad,width,height,tiled);});
        }
        for(auto bad:{0u,32u,96u,4096u,0xFFFFFFFFu}) {
            rejects([&]{decodeITXDLuminanceBase(descriptor,bad,height,tiled);});
            rejects([&]{decodeITXDLuminanceBase(descriptor,width,bad,tiled);});
        }
        rejects([&]{decodeITXDLuminanceBase(descriptor,width,height,tiled.first(size-1));});
        std::vector<uint8_t> large(tiled.begin(),tiled.end());large.push_back(0);
        rejects([&]{decodeITXDLuminanceBase(descriptor,width,height,large);});
    }
    need(at==data.size()&&read(path)==data,"Luminance fixtures have trailing bytes or were modified");
    std::printf("PASS %zu L8 family pixel/bounds/descriptor checks and %u %s texture readbacks\n",checks,count,hardware?"hardware":"WARP");
}
}
int main(int argc,char** argv){try{
    if(argc>=3&&!std::strcmp(argv[1],"--mips")) {
        need(argc==3||(argc==4&&!std::strcmp(argv[3],"--hardware")),"Invalid luminance mip arguments");
        mipFamily(argv[2],argc==4);return 0;
    }
    if(argc>=3&&!std::strcmp(argv[1],"--family")) {
        need(argc==3||(argc==4&&!std::strcmp(argv[3],"--hardware")),"Invalid luminance family arguments");
        family(argv[2],argc==4);return 0;
    }
    need(argc==3,"Original tiled and independent linear luminance required");const auto tiled=read(argv[1]),linear=read(argv[2]);
    const std::array<uint32_t,6> descriptor={0x80800002,2,0x7E03F,0x1400,0,0x200};
    const auto decoded=decodeITXDLuminanceBase(descriptor,64,64,tiled);need(decoded.size()==16384&&linear.size()==16384,"Luminance extent differs");
    for(size_t i=0;i<decoded.size();++i)need(decoded[i]==linear[i],"Original luminance coordinate/channel differs from inverse-address fixture");
    auto changed=tiled;changed[0]^=0x5A;
    const auto altered=decodeITXDLuminanceBase(descriptor,64,64,changed);
    for(size_t i=0;i<altered.size();++i)need(altered[i]==uint8_t(decoded[i]^(i<3?0x5A:0)),"Luminance mutation changed unrelated channel/texel");
    NativeBackend backend(true);auto texture=backend.createTexture(64,64,TextureFormat::RGBA8,decoded);
    need(backend.readback(texture)==linear,"Native luminance RGB/opaque alpha upload differs");
    for(size_t field=0;field<6;++field)for(unsigned bit=0;bit<32;++bit){auto changed=descriptor;changed[field]^=1u<<bit;rejects([&]{decodeITXDLuminanceBase(changed,64,64,tiled);});}
    rejects([&]{decodeITXDLuminanceBase(descriptor,32,64,tiled);});rejects([&]{decodeITXDLuminanceBase(descriptor,64,32,tiled);});
    rejects([&]{decodeITXDLuminanceBase(descriptor,64,64,std::span(tiled).first(4095));});
    auto large=tiled;large.push_back(0);rejects([&]{decodeITXDLuminanceBase(descriptor,64,64,large);});
    need(read(argv[1])==tiled,"Original luminance storage changed");
    std::printf("PASS %zu original luminance byte/coordinate/channel/bounds checks\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL luminance after%zu checks: %s\n",checks,e.what());return 1;}}
