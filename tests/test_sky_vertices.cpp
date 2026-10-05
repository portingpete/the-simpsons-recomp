#include "runtime/sky_vertices.h"
#include "runtime/runtime.h"
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
size_t checks{};
void need(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
uint32_t word(const std::vector<uint8_t>& b,size_t at){return uint32_t(b.at(at))<<24|uint32_t(b.at(at+1))<<16|uint32_t(b.at(at+2))<<8|b.at(at+3);}
void store(std::vector<uint8_t>& b,size_t at,uint32_t v){b.at(at)=v>>24;b.at(at+1)=v>>16;b.at(at+2)=v>>8;b.at(at+3)=v;}
uint32_t bits(float f){uint32_t v;std::memcpy(&v,&f,4);return v;}
template<class F>void rejects(F action){bool failed=false;try{action();}catch(const Simpsons::Failure&){failed=true;}need(failed,"Malformed sky input succeeded");}
}
int main(int argc,char** argv)try {
    using Simpsons::decodeSkyVertices;
    if(argc==2) {
    const std::filesystem::path dir=argv[1];
    auto read=[&](const char* n){std::ifstream f(dir/n,std::ios::binary);if(!f)throw std::runtime_error("Missing capture");return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),{});};
    auto geo=read("scene-geometry.bin"),elem=read("scene-elements.bin"),data=read("scene-vertices.bin");
    const auto stride=word(geo,4);
    const auto decoded=decodeSkyVertices(data,elem,stride);
    need(stride==28,"Sky geometry stride differs");
    for(size_t i=0;i<decoded.size();++i) {
        const auto& v=decoded[i];
        for(size_t l=0;l<3;++l)need(std::bit_cast<uint32_t>(v.position[l])==word(data,i*stride+4*l),"Sky position bits changed");
        for(size_t l=0;l<2;++l)need(std::bit_cast<uint32_t>(v.uv[l])==word(data,i*stride+12+4*l),"Sky UV bits changed");
        for(size_t l=0;l<2;++l)need(std::bit_cast<uint32_t>(v.uv1[l])==word(data,i*stride+20+4*l),"Sky UV1 bits changed");
    }
    std::printf("PASS sky vertex decoder: %zu checks, %zu verts stride28\n",checks,decoded.size());return 0;
    } else need(argc==1,"Supply optional capture directory");
    // Synthetic: two verts exercising position/uv/uv1 lanes.
    std::vector<uint8_t> elem(48,0);
    store(elem,0,0);store(elem,4,0x002A23B9);store(elem,8,0);
    store(elem,12,12);store(elem,16,0x002C23A5);store(elem,20,0x00050000);
    store(elem,24,20);store(elem,28,0x002C23A5);store(elem,32,0x00050100);
    store(elem,36,0x00FF0000);store(elem,40,0xFFFFFFFFu);store(elem,44,0);
    std::vector<uint8_t> data(56,0);
    const float pos[6]={1,2,4,-1,-2,-4},uv[4]={0,0.5f,1,0.25f},uv1[4]={0,1,0.5f,0.75f};
    for(size_t i=0;i<2;++i) {
        for(size_t l=0;l<3;++l)store(data,i*28+4*l,bits(pos[3*i+l]));
        for(size_t l=0;l<2;++l)store(data,i*28+12+4*l,bits(uv[2*i+l]));
        for(size_t l=0;l<2;++l)store(data,i*28+20+4*l,bits(uv1[2*i+l]));
    }
    const auto decoded=decodeSkyVertices(data,elem,28);
    need(decoded.size()==2,"Sky synthetic extent differs");
    for(size_t i=0;i<2;++i) {
        for(size_t l=0;l<3;++l)need(decoded[i].position[l]==pos[3*i+l],"Sky synthetic position differs");
        for(size_t l=0;l<2;++l)need(decoded[i].uv[l]==uv[2*i+l],"Sky synthetic uv differs");
        for(size_t l=0;l<2;++l)need(decoded[i].uv1[l]==uv1[2*i+l],"Sky synthetic uv1 differs");
    }
    for(uint32_t stride=28;stride<=1020;stride+=4) {
        auto declaration=elem;const auto offset=stride-28;
        for(size_t row=0;row<3;++row)store(declaration,12*row,word(declaration,12*row)+offset);
        // Reverse the input rows while preserving exact original semantics.
        for(size_t lane=0;lane<12;++lane)std::swap(declaration[lane],declaration[24+lane]);
        std::vector<uint8_t> padded(2*stride,0xFF);
        for(size_t i=0;i<2;++i)std::memcpy(padded.data()+i*stride+offset,data.data()+i*28,28);
        const auto before=padded,declarationBefore=declaration;
        const auto result=decodeSkyVertices(padded,declaration,stride);
        need(result.size()==decoded.size()&&!std::memcmp(result.data(),decoded.data(),decoded.size()*sizeof(decoded[0])),
             "Representable padded/reordered sky input changed decoded attributes");
        need(padded==before&&declaration==declarationBefore,"Sky decoder mutated original input");
    }
    for(uint32_t invalidStride:std::array<uint32_t,5>{0,24,29,1024,4096}) {
        auto padded=data;padded.resize(size_t(2)*invalidStride);
        rejects([&]{decodeSkyVertices(padded,elem,invalidStride);});
    }
    for(const auto change:std::array<std::array<uint32_t,2>,10>{{{0,0x00010000},{4,0x002C23A5},
        {8,0x01000000},{8,1},{12,8},{12,13},{12,24},{20,0},{36,0},{44,1}}}) {
        auto bad=elem;store(bad,change[0],change[1]);rejects([&]{decodeSkyVertices(data,bad,28);});
    }
    for(size_t lane=0;lane<7;++lane) {
        auto bad=data;store(bad,4*lane,0x7FC00000);rejects([&]{decodeSkyVertices(bad,elem,28);});
    }
    auto shortData=data;shortData.pop_back();rejects([&]{decodeSkyVertices(shortData,elem,28);});
    std::printf("PASS sky vertex decoder: %zu checks (synthetic, no capture)\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL sky vertices after %zu checks: %s\n",checks,e.what());return 1;}
