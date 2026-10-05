#include "renderer/itxd_blocks.h"
#include "renderer/native_backend.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void need(bool v,const char* message){++checks;if(!v)throw Error(message);}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const Error&){failed=true;}need(failed,"Unqualified BC chain accepted");}
}
int main(int argc,char** argv){try{
    need(argc==2,"Expected original BC mip fixtures");
    std::ifstream input(argv[1],std::ios::binary);need(bool(input),"BC mip fixtures missing");
    const std::vector<uint8_t> file{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};size_t cursor{};
    auto word=[&]{need(cursor+4<=file.size(),"Fixture word exceeds file");uint32_t v{};
        for(uint32_t i=0;i<4;++i)v|=uint32_t(file[cursor++])<<(8*i);return v;};
    auto bytes=[&](size_t n){need(cursor+n<=file.size(),"Fixture bytes exceed file");const auto v=std::span(file).subspan(cursor,n);cursor+=n;return v;};
    NativeBackend backend(true);const auto count=word();need(count==6,"BC fixture count changed");size_t levelsTested{};
    for(uint32_t c=0;c<count;++c){
        const auto w=word(),h=word(),format=word();std::array<uint32_t,6> d{};for(auto& v:d)v=word();
        const auto length=word(),levels=word();const auto tiled=bytes(length);
        const auto decoded=decodeITXDBCMipChain(d,format,w,h,tiled);
        need(decoded.size()==levels,"Authored BC mip count differs");
        for(const auto& level:decoded){const auto expected=bytes(word());need(std::ranges::equal(level,expected),"BC mip differs from inverse original-resource fixture");++levelsTested;}
        std::vector<std::span<const uint8_t>> views;for(const auto& level:decoded)views.emplace_back(level);
        const auto texture=backend.createTextureMipChain(w,h,format==0x52?TextureFormat::BC1:format==0x53?TextureFormat::BC2:TextureFormat::BC3,views);
        for(uint32_t level=0;level<levels;++level)need(backend.readbackMip(texture,level)==decoded[level],"Original BC mip native bytes differ");
        for(uint32_t field=0;field<6;++field){auto wrong=d;wrong[field]^=1;rejects([&]{decodeITXDBCMipChain(wrong,format,w,h,tiled);});}
        rejects([&]{decodeITXDBCMipChain(d,format^1,w,h,tiled);});
        rejects([&]{decodeITXDBCMipChain(d,format,w/2,h,tiled);});
        rejects([&]{decodeITXDBCMipChain(d,format,w,h,tiled.first(tiled.size()-1));});
        auto wrong=d;wrong[5]+=4096;rejects([&]{decodeITXDBCMipChain(wrong,format,w,h,tiled);});
        wrong=d;wrong[4]=15<<6;rejects([&]{decodeITXDBCMipChain(wrong,format,w,h,tiled);});
    }
    need(cursor==file.size(),"Unconsumed BC mip fixture data");
    // Preserve the existing small, single-level UI profile when sharing the
    // chain decoder. These are ordinary base levels, with no packed tail.
    for(uint32_t extent:{4u,8u,16u})for(uint32_t format:{0x53u,0x54u}){
        const std::array<uint32_t,6> d={0x81000002,format,((extent-1)<<13)|(extent-1),0xD10,0,0x200};
        std::vector<uint8_t> tiled(16384);for(size_t i=0;i<tiled.size();++i)tiled[i]=uint8_t(i*17+(i>>7)*29);
        const auto chain=decodeITXDBCMipChain(d,format,extent,extent,tiled);
        const auto base=format==0x53?decodeITXDBC2Base(d,extent,extent,tiled):decodeITXDBC3Base(d,extent,extent,tiled);
        need(chain.size()==1 && chain[0]==base,"Small single-level UI compatibility differs");
    }
    std::printf("PASS: %zu BC mip checks, %zu independently addressed original/synthetic levels and exact native uploads\n",checks,levelsTested);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL after %zu BC mip checks: %s\n",checks,e.what());return 1;}}
