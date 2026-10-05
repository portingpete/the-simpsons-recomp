#include "renderer/itxd_blocks.h"
#include "renderer/native_backend.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void need(bool v,const char* why){++checks;if(!v)throw Error(why);}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const Error&){failed=true;}need(failed,"Unqualified RGBA8 profile accepted");}
}
int main(int argc,char** argv){try{
    need(argc==2,"Expected original RGBA8 fixture");
    std::ifstream input(argv[1],std::ios::binary);need(bool(input),"Missing fixture");
    const std::vector<uint8_t> file{std::istreambuf_iterator<char>(input),{}};
    const bool tonal=file.size()==24+0x56000+349184;
    const bool buildings=file.size()==24+0x6000+21504;
    const uint32_t extent=buildings?64u:tonal?256u:512u,levels=buildings?3u:tonal?5u:6u;
    const size_t storageBytes=buildings?0x6000u:tonal?0x56000u:0x156000u;
    need(buildings||tonal||file.size()==24+0x156000+1397760,"Fixture extent differs");
    std::array<uint32_t,6> d{};
    for(size_t i=0;i<6;++i)for(size_t b=0;b<4;++b)d[i]|=uint32_t(file[i*4+b])<<(b*8);
    const auto tiled=std::span(file).subspan(24,storageBytes);
    const auto decoded=decodeITXDCandyRGBA8(d,extent,extent,tiled);
    need(decoded.size()==levels,"Authored level count differs");size_t cursor=24+storageBytes;
    NativeBackend backend(true);
    std::vector<std::span<const uint8_t>> views;
    for(const auto& level:decoded){
        need(std::ranges::equal(level,std::span(file).subspan(cursor,level.size())),"Original RGBA8 differs from inverse-address fixture");
        cursor+=level.size();views.emplace_back(level);
    }
    need(cursor==file.size(),"Unconsumed fixture bytes");
    const auto texture=backend.createTextureMipChain(extent,extent,TextureFormat::RGBA8,views);
    for(uint32_t level=0;level<levels;++level)need(backend.readbackMip(texture,level)==decoded[level],"GPU mip bytes differ");
    for(size_t field=0;field<6;++field)for(unsigned bit=0;bit<32;++bit){auto bad=d;bad[field]^=1u<<bit;rejects([&]{decodeITXDCandyRGBA8(bad,extent,extent,tiled);});}
    rejects([&]{decodeITXDCandyRGBA8(d,extent/2,extent,tiled);});
    rejects([&]{decodeITXDCandyRGBA8(d,extent,extent/2,tiled);});
    rejects([&]{decodeITXDCandyRGBA8(d,extent,extent,tiled.first(tiled.size()-1));});
    auto extra=std::vector<uint8_t>(tiled.begin(),tiled.end());extra.push_back(0);
    rejects([&]{decodeITXDCandyRGBA8(d,extent,extent,extra);});
    rejects([&]{decodeITXDPaletteRGBA8(d,extent,extent,tiled);});
    for(size_t lane=0;lane<4;++lane){auto changed=std::vector<uint8_t>(tiled.begin(),tiled.end());changed[lane]^=0x5A;
        auto expected=decoded;expected[0][(lane+3)%4]^=0x5A;
        need(decodeITXDCandyRGBA8(d,extent,extent,changed)==expected,"Lane mutation changed unrelated pixels or levels");}
    std::printf("PASS %zu RGBA8 checks: %u original levels, exact native upload/readback and rejected profiles\n",checks,levels);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL RGBA8 after %zu checks: %s\n",checks,e.what());return 1;}}
