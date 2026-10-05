#include "renderer/itxd_blocks.h"
#include "renderer/native_backend.h"
#include <cstdio>
#include <fstream>
#include <iterator>

using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void need(bool condition,const char* why){++checks;if(!condition)throw Error(why);}
std::vector<uint8_t> read(const char* path) {
    std::ifstream file(path,std::ios::binary);if(!file)throw Error("ITXD fixture input unavailable");
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const Error&){failed=true;}need(failed,"Malformed ITXD block profile accepted");}
}
int main(int argc,char** argv){try {
    need(argc==3,"Expected original tiled atlas and independent linear fixture");
    const auto tiled=read(argv[1]),expected=read(argv[2]);
    const std::array<uint32_t,6> description={0x88000002,0x53,0x3FE3FF,0xD10,0,0x200};
    need(tiled.size()==524288 && expected.size()==524288,"Original font fixture extent differs");
    const auto result=decodeITXDBC2Base(description,1024,512,tiled);
    need(result.size()==expected.size(),"Decoded BC2 atlas size differs");
    for(size_t i=0;i<result.size();++i)need(result[i]==expected[i],"Original font BC2 block/lane differs from independent decode");
    auto mutated=tiled;mutated[0]^=0x5A;
    const auto changed=decodeITXDBC2Base(description,1024,512,mutated);
    need(changed[1]==uint8_t(result[1]^0x5A),"Original BC2 8-in-16 byte lane was not exchanged");
    for(size_t i=0;i<result.size();++i)if(i!=1)need(changed[i]==result[i],"Single stored lane changed unrelated logical blocks");
    for(size_t field=0;field<6;++field){auto wrong=description;wrong[field]^=1;rejects([&]{decodeITXDBC2Base(wrong,1024,512,tiled);});}
    auto address=description;address[1]|=0x1000;rejects([&]{decodeITXDBC2Base(address,1024,512,tiled);});
    auto mip=description;mip[4]=0x40;rejects([&]{decodeITXDBC2Base(mip,1024,512,tiled);});
    auto pitch=description;pitch[0]=0x84000002;rejects([&]{decodeITXDBC2Base(pitch,1024,512,tiled);});
    rejects([&]{decodeITXDBC2Base(description,512,512,tiled);});
    rejects([&]{decodeITXDBC2Base(description,1023,512,tiled);});
    rejects([&]{decodeITXDBC2Base(description,0,512,tiled);});
    rejects([&]{decodeITXDBC2Base(description,4096,512,tiled);});
    rejects([&]{decodeITXDBC2Base(description,1024,512,std::span(tiled).first(tiled.size()-1));});
    mutated=tiled;mutated.push_back(0);rejects([&]{decodeITXDBC2Base(description,1024,512,mutated);});
    need(tiled==read(argv[1]),"ITXD decoder changed original source storage");
    std::printf("PASS: %zu ITXD BC2 block checks; exact original atlas bytes, no GPU/ownership claim\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL after %zu checks: %s\n",checks,e.what());return 1;}}
