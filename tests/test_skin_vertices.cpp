#include "runtime/skin_vertices.h"
#include "runtime/character_mesh.h"
#include "runtime/runtime.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <bit>
#include <cmath>
#include <cstring>

namespace {
size_t checks{};
void need(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
uint32_t word(const std::vector<uint8_t>& b,size_t at){return uint32_t(b.at(at))<<24|uint32_t(b.at(at+1))<<16|uint32_t(b.at(at+2))<<8|b.at(at+3);}
void put(std::vector<uint8_t>& b,size_t at,uint32_t value){for(size_t i=0;i<4;++i)b.at(at+i)=uint8_t(value>>(24-8*i));}
template<class F>void rejects(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}need(failed,"Malformed skin input succeeded");}
void synthetic(bool dual) {
    const uint32_t stride=dual?56:48,source=dual?0x8201CD48:0x82006348;
    std::vector<uint8_t> data(2*stride),decl(dual?168:156);size_t row=0;
    const auto add=[&](uint32_t offset,uint32_t type,uint32_t semantic){put(decl,row,offset);put(decl,row+4,type);put(decl,row+8,semantic);row+=12;};
    add(0,0x002A23B9,0);add(12,0x002A2187,3<<16);add(16,0x002C23A5,5<<16);
    if(dual)add(24,0x002C23A5,(5<<16)|(1<<8));
    const uint32_t indices=dual?32:24,weights=indices+4,color=weights+16;
    add(indices,0x001A2286,2<<16);add(weights,0x001A23A6,1<<16);add(color,0x00182886,10<<16);
    for(uint32_t i=1;i<=6;++i)add(i<<16,0x002A23B9,i<<8);
    add(0x00FF0000,UINT32_MAX,0);
    for(size_t i=0;i<2;++i) {
        const size_t at=i*stride;const auto number=[&](size_t offset,float value){put(data,at+offset,std::bit_cast<uint32_t>(value));};
        number(0,float(i)+1);number(4,-2);number(8,.25f);
        put(data,at+12,511|(512<<10)|(1023<<20));number(16,-.125f);number(20,2.5f);
        if(dual){number(24,10.5f);number(28,-7.25f);}
        put(data,at+indices,0x003F011F);number(weights,.25f);number(weights+4,.5f);number(weights+8,.125f);number(weights+12,.125f);
        put(data,at+color,0x80402010);
    }
    const auto result=Simpsons::decodeSkinVertices(data,decl,stride,source);need(result.size()==2,"Synthetic skin vertex count differs");
    const auto shadow=Simpsons::decodeCharacterVertices(data,decl,stride,64);
    for(size_t i=0;i<2;++i) {
        const auto& v=result[i];need(v.position==std::array<float,3>{float(i)+1,-2,.25f},"Skin position decoding differs");
        need(v.normal==std::array<float,3>{1,-1,-1.0f/511},"Signed packed normal decoding differs");
        need(v.uv==std::array<float,2>{-.125f,2.5f},"Skin UV0 decoding differs");
        need(v.uv1==(dual?std::array<float,2>{10.5f,-7.25f}:std::array<float,2>{}),"Skin UV1 decoding differs");
        need(v.indices==std::array<float,4>{31,1,63,0},"Skin index byte order differs");
        need(v.indices==shadow[i].indices&&v.weights==shadow[i].weights,
             "Character color and shadow passes attach vertices to different bones");
        need(v.weights==std::array<float,4>{.25f,.5f,.125f,.125f},"Skin weight decoding differs");
        need(v.color==std::array<float,4>{64.0f/255,32.0f/255,16.0f/255,128.0f/255},"Skin color byte order differs");
        need(v.morph1==std::array<float,3>{}&&v.morph6==std::array<float,3>{},"Absent morph values differ");
    }
    // Actual captured vertices use 00 00 00 09 with weightX=1. The packed
    // byte4 field is decoded AFTER 8-in-32 endian conversion, so X is bone9.
    auto singleBone=data;put(singleBone,indices,9);put(singleBone,weights,0x3F800000);
    for(size_t lane=1;lane<4;++lane)put(singleBone,weights+4*lane,0);
    const auto attached=Simpsons::decodeSkinVertices(singleBone,decl,stride,source);
    need(attached[0].indices==std::array<float,4>{9,0,0,0}&&attached[0].weights==std::array<float,4>{1,0,0,0},
         "Single-influence character vertex no longer selects its original bone9");
    // Exhaust the DWORD-stride byte's representable range. Move the complete
    // input to the end of each padded record, crossing the old64-bit mask.
    // Declaration association is by semantic/index, not capture offsets.
    for(uint32_t paddedStride=stride;paddedStride<=1020;paddedStride+=4) {
        auto relocated=decl;const uint32_t offset=paddedStride-stride;
        for(size_t at=0;at+12<relocated.size();at+=12)
            if(!(word(relocated,at)>>16))put(relocated,at,word(relocated,at)+offset);
        std::vector<uint8_t> padded(2*paddedStride,0xFF);
        for(size_t i=0;i<2;++i)std::memcpy(padded.data()+i*paddedStride+offset,data.data()+i*stride,stride);
        const auto original=padded,declarationBefore=relocated;
        const auto out=Simpsons::decodeSkinVertices(padded,relocated,paddedStride,source);
        need(out.size()==result.size()&&!std::memcmp(out.data(),result.data(),result.size()*sizeof(result[0])),
             "Representable padded/relocated skin input changed decoded attributes");
        need(padded==original&&relocated==declarationBefore,"Padded skin decoder mutated original input");
    }
    for(uint32_t invalidStride:std::array<uint32_t,4>{0,49,1024,4096}) {
        auto padded=data;padded.resize(size_t(2)*invalidStride);
        rejects([&]{Simpsons::decodeSkinVertices(padded,decl,invalidStride,source);});
    }
    rejects([&]{Simpsons::decodeSkinVertices(data,decl,stride,0x82000000);});
    rejects([&]{Simpsons::decodeSkinVertices(data,decl,stride-4,source);});
    auto bad=data;bad.pop_back();rejects([&]{Simpsons::decodeSkinVertices(bad,decl,stride,source);});
    for(size_t offset:std::vector<size_t>{0,16,weights,dual?24u:20u}) {
        bad=data;put(bad,offset,0x7FC00000);rejects([&]{Simpsons::decodeSkinVertices(bad,decl,stride,source);});
    }
    bad=data;put(bad,indices,0x0040011F);rejects([&]{Simpsons::decodeSkinVertices(bad,decl,stride,source);});
    auto malformed=decl;put(malformed,24,12);rejects([&]{Simpsons::decodeSkinVertices(data,malformed,stride,source);});
    malformed=decl;put(malformed,4,0);rejects([&]{Simpsons::decodeSkinVertices(data,malformed,stride,source);});
    malformed=decl;put(malformed,decl.size()-12,0);rejects([&]{Simpsons::decodeSkinVertices(data,malformed,stride,source);});
    const auto firstMorph=dual?84:72;malformed=decl;
    for(size_t i=0;i<12;++i)malformed[firstMorph+12+i]=malformed[firstMorph+i];
    rejects([&]{Simpsons::decodeSkinVertices(data,malformed,stride,source);});
    malformed=decl;put(malformed,8,1);rejects([&]{Simpsons::decodeSkinVertices(data,malformed,stride,source);});
    if(dual){malformed=decl;put(malformed,44,5<<16);rejects([&]{Simpsons::decodeSkinVertices(data,malformed,stride,source);});}
}
void morphStreams() {
    std::vector<Simpsons::Graphics::SkinVertex> vertices(3);
    const std::array<uint64_t,6> masks{1,1,2,2,2,4};
    for(uint32_t stream=1;stream<=6;++stream) {
        need(Simpsons::skinMorphStreamDirtyMask(stream)==masks[stream-1],"Original morph fetch-group mask differs");
        std::vector<uint8_t> bytes(vertices.size()*12);
        for(size_t i=0;i<vertices.size();++i)for(size_t lane=0;lane<3;++lane)
            put(bytes,i*12+4*lane,std::bit_cast<uint32_t>(float(int(stream*10+i*3+lane)-35)*.125f));
        Simpsons::decodeSkinMorphStream(vertices,stream,bytes);
        for(size_t i=0;i<vertices.size();++i) {
            const auto& v=vertices[i];const std::array<std::array<float,3>,6> rows{v.morph1,v.morph2,v.morph3,v.morph4,v.morph5,v.morph6};
            for(size_t lane=0;lane<3;++lane)need(rows[stream-1][lane]==float(int(stream*10+i*3+lane)-35)*.125f,"Morph stream order/endian conversion differs");
            need(v.position==std::array<float,3>{},"Morph decode changed base position");
        }
        const auto before=vertices;
        auto invalid=bytes;put(invalid,invalid.size()-4,0x7FC00000);
        rejects([&]{Simpsons::decodeSkinMorphStream(vertices,stream,invalid);});
        need(std::memcmp(vertices.data(),before.data(),vertices.size()*sizeof(vertices[0]))==0,"Rejected morph changed vertices");
        bytes.pop_back();rejects([&]{Simpsons::decodeSkinMorphStream(vertices,stream,bytes);});
    }
    rejects([&]{Simpsons::skinMorphStreamDirtyMask(0);});rejects([&]{Simpsons::skinMorphStreamDirtyMask(7);});
    const auto& last=vertices.back();
    need(last.morph1!=last.morph2&&last.morph2!=last.morph3&&last.morph3!=last.morph4&&last.morph4!=last.morph5&&last.morph5!=last.morph6,
         "Distinct morph streams overwrote one another");
}
}
int main(int argc,char** argv)try {
    using Simpsons::decodeSkinVertices;
    synthetic(false);synthetic(true);morphStreams();
    if(argc==2) {
    const std::filesystem::path dir=argv[1];
    auto read=[&](const char* n){std::ifstream f(dir/n,std::ios::binary);if(!f)throw std::runtime_error("Missing capture");return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),{});};
    auto geo=read("scene-geometry.bin"),elem=read("scene-elements.bin"),data=read("scene-vertices.bin");
    const auto stride=word(geo,4);
    const auto decoded=decodeSkinVertices(data,elem,stride,stride==56?0x8201CD48:0x82006348);
    need((stride==48&&decoded.size()==121)||(stride==56&&decoded.size()==374),"Skin geometry extent differs");
    for(size_t i=0;i<decoded.size();++i) {
        const auto& v=decoded[i];
        for(size_t l=0;l<3;++l)need(std::bit_cast<uint32_t>(v.position[l])==word(data,i*stride+4*l),"Skin position bits changed");
        for(size_t l=0;l<2;++l)need(std::bit_cast<uint32_t>(v.uv[l])==word(data,i*stride+16+4*l),"Skin UV bits changed");
        if(stride==56)for(size_t l=0;l<2;++l)need(std::bit_cast<uint32_t>(v.uv1[l])==word(data,i*stride+24+4*l),"Skin UV1 bits changed");
        double sum=0;for(float w:v.weights)sum+=w;
        need(std::abs(sum-1.0)<1e-5,"Skin weights do not sum to one");
        for(float b:v.indices)need(b>=0&&b<=63&&std::floor(b)==b,"Skin bone index range");
    }
    std::printf("PASS skin vertex decoder: %zu checks, %zu verts stride%u\n",checks,decoded.size(),stride);return 0;
    } else need(argc==1,"Supply optional capture directory");
    std::printf("PASS skin vertex decoder: %zu checks (synthetic, no capture)\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL skin vertices after %zu checks: %s\n",checks,e.what());return 1;}
