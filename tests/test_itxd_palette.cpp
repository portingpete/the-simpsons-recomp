#include "renderer/itxd_blocks.h"
#include "renderer/native_backend.h"
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void need(bool value,const char* why){++checks;if(!value)throw Error(why);}
std::vector<uint8_t> read(const char* path){std::ifstream f(path,std::ios::binary);if(!f)throw Error("Missing original palette fixture");return {std::istreambuf_iterator<char>(f),{}};}
template<class F>void rejects(F f){try{f();}catch(const Error&){++checks;return;}throw Error("Unqualified palette profile accepted");}
}
int main(int argc,char** argv){try{
    need(argc==3,"Original tiled and independent linear palette required");const auto tiled=read(argv[1]),linear=read(argv[2]);
    const std::array<uint32_t,6> descriptor={0x80800002,0x86,0x7E03F,0xC14,0,0x200};
    const auto decoded=decodeITXDPaletteRGBA8(descriptor,64,64,tiled);need(decoded.size()==16384&&linear.size()==16384,"Palette extent differs");
    for(size_t i=0;i<decoded.size();++i)need(decoded[i]==linear[i],"Original palette coordinate/channel differs from inverse-address fixture");
    // Each byte of the first texel exposes endian versus ZYXW selection.
    for(size_t source=0;source<4;++source){auto changed=tiled;changed[source]^=0x5A;
        const auto actual=decodeITXDPaletteRGBA8(descriptor,64,64,changed);const size_t affected=(source+3)%4;
        for(size_t i=0;i<actual.size();++i)need(actual[i]==uint8_t(decoded[i]^(i==affected?0x5A:0)),"Palette lane mutation changed unrelated output");}
    for(size_t field=0;field<6;++field)for(unsigned bit=0;bit<32;++bit){auto changed=descriptor;changed[field]^=1u<<bit;rejects([&]{decodeITXDPaletteRGBA8(changed,64,64,tiled);});}
    rejects([&]{decodeITXDPaletteRGBA8(descriptor,32,64,tiled);});rejects([&]{decodeITXDPaletteRGBA8(descriptor,64,32,tiled);});
    rejects([&]{decodeITXDPaletteRGBA8(descriptor,64,64,std::span(tiled).first(16383));});
    auto large=tiled;large.push_back(0);rejects([&]{decodeITXDPaletteRGBA8(descriptor,64,64,large);});
    need(read(argv[1])==tiled,"Original palette storage changed");
    std::printf("PASS %zu original palette byte/coordinate/channel/bounds checks\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL palette after%zu checks: %s\n",checks,e.what());return 1;}}
