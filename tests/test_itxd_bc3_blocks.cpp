#include "renderer/itxd_blocks.h"
#include "renderer/native_backend.h"
#include <cstdio>
#include <fstream>
#include <iterator>

using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void need(bool value,const char* why){++checks;if(!value)throw Error(why);}
std::vector<uint8_t> read(const char* path){
    std::ifstream file(path,std::ios::binary);if(!file)throw Error("BC3 fixture missing");
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const Error&){failed=true;}need(failed,"Unqualified BC3 profile accepted");}
}
int main(int argc,char** argv){try{
    need(argc==3,"Expected original tiled resource and independent BC3 fixture");
    const auto tiled=read(argv[1]),expected=read(argv[2]);
    const std::array<uint32_t,6> d={0x84000002,0x54,0x1FE1FF,0xD10,0,0x200};
    need(tiled.size()==131072&&expected.size()==131072,"Original shared-library extent changed");
    const auto decoded=decodeITXDBC3Base(d,512,256,tiled);
    need(decoded==expected,"Original BC3 blocks differ from independent original-resource fixture");
    auto mutated=tiled;mutated[0]^=0x5A;
    const auto changed=decodeITXDBC3Base(d,512,256,mutated);
    for(size_t i=0;i<decoded.size();++i)
        need(changed[i]==uint8_t(decoded[i]^(i==1?0x5A:0)),"BC3 byte swap changed unrelated block/lane");
    for(size_t field=0;field<6;++field){auto wrong=d;wrong[field]^=1;rejects([&]{decodeITXDBC3Base(wrong,512,256,tiled);});}
    auto bc2=d;bc2[1]=0x53;rejects([&]{decodeITXDBC3Base(bc2,512,256,tiled);});
    rejects([&]{decodeITXDBC2Base(d,512,256,tiled);});
    auto address=d;address[1]|=0x1000;rejects([&]{decodeITXDBC3Base(address,512,256,tiled);});
    auto mip=d;mip[4]=0x40;rejects([&]{decodeITXDBC3Base(mip,512,256,tiled);});
    auto pitch=d;pitch[0]=0x82000002;rejects([&]{decodeITXDBC3Base(pitch,512,256,tiled);});
    for(uint32_t width:{0u,511u,256u,4096u})rejects([&]{decodeITXDBC3Base(d,width,256,tiled);});
    rejects([&]{decodeITXDBC3Base(d,512,256,std::span(tiled).first(tiled.size()-1));});
    mutated=tiled;mutated.push_back(0);rejects([&]{decodeITXDBC3Base(d,512,256,mutated);});
    need(tiled==read(argv[1]),"BC3 decoder changed original storage");
    std::printf("PASS: %zu original shared-library BC3 block/descriptor checks\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL after %zu BC3 checks: %s\n",checks,e.what());return 1;}}
