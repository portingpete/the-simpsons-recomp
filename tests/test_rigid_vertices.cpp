#include "runtime/rigid_vertices.h"
#include "runtime/runtime.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
using Bytes=std::vector<uint8_t>;
size_t checks{};
void need(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
void put(Bytes& b,size_t at,uint32_t v) {
    b.at(at)=uint8_t(v>>24);b.at(at+1)=uint8_t(v>>16);b.at(at+2)=uint8_t(v>>8);b.at(at+3)=uint8_t(v);
}
uint32_t word(const Bytes& b,size_t at){return uint32_t(b.at(at))<<24|uint32_t(b.at(at+1))<<16|uint32_t(b.at(at+2))<<8|b.at(at+3);}
Bytes declaration() {
    constexpr uint32_t rows[][3]={{0,0x002A23B9,0},{12,0x002A2187,0x00030000},
        {16,0x00182886,0x000A0000},{20,0x002C23A5,0x00050000},{0x00FF0000,UINT32_MAX,0}};
    Bytes b(60);for(size_t i=0;i<5;++i)for(size_t j=0;j<3;++j)put(b,i*12+j*4,rows[i][j]);return b;
}
template<class F>void rejects(F fn,const char* why){bool rejected=false;try{fn();}catch(const Simpsons::Failure&){rejected=true;}need(rejected,why);}
Bytes read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);need(bool(f),"Missing captured rigid data");return Bytes(std::istreambuf_iterator<char>(f),{});}
}
int main(int argc,char** argv)try {
    using Simpsons::decodeRigidVertices;
    auto decl=declaration();Bytes source(1024*28);
    for(uint32_t i=0;i<1024;++i) {
        const auto at=size_t(i)*28;
        put(source,at,0x80000000);put(source,at+4,0x3E000001);put(source,at+8,0xC47A0000);
        put(source,at+12,i|((1023-i)<<10)|(((i+513)&1023)<<20)|((i&3)<<30));
        put(source,at+16,((i&255)<<24)|(((i+17)&255)<<16)|(((i+83)&255)<<8)|((i+211)&255));
        put(source,at+20,0x80000000);put(source,at+24,0x3F000001);
    }
    const auto original=source;auto vertices=decodeRigidVertices(source,decl,28);
    need(vertices.size()==1024&&source==original,"Rigid decoder changed source or vertex extent");
    for(size_t i=0;i<vertices.size();++i) {
        const auto& v=vertices[i];
        for(size_t j=0;j<3;++j)need(std::bit_cast<uint32_t>(v.position[j])==word(source,28*i+4*j),"Rigid position bits changed");
        for(size_t j=0;j<2;++j)need(std::bit_cast<uint32_t>(v.uv[j])==word(source,28*i+20+4*j),"Rigid UV bits changed");
        const auto packed=word(source,28*i+12);
        for(size_t j=0;j<3;++j) {
            const auto raw=(packed>>(10*j))&1023;
            // Independent magnitude/sign oracle covers every encoding per lane.
            const double expected=raw<512?double(raw)/511.0:-std::min(1.0,double(1024-raw)/511.0);
            need(v.normal[j]==float(expected),"Rigid signed normal decoding differs");
        }
        const auto color=word(source,28*i+16);
        const uint32_t channels[]={(color>>16)&255,(color>>8)&255,color&255,color>>24};
        for(size_t j=0;j<4;++j)need(v.color[j]==float(double(channels[j])/255.0),"Rigid color normalization/channel differs");
    }
    Bytes one(source.begin(),source.begin()+28);
    // LIVE029 contains referenced 00000000 packed normals. Dead high bits
    // cannot turn these into fallback directions or modify source attributes.
    for(uint32_t high:{0u,0x40000000u,0x80000000u,0xC0000000u}) {
        auto zero=one;put(zero,12,high);const auto saved=zero;
        const auto decoded=decodeRigidVertices(zero,decl,28);
        need(zero==saved&&decoded.size()==1,"Zero-normal decoding changed original storage");
        for(float n:decoded[0].normal)need(std::bit_cast<uint32_t>(n)==0,"Original zero normal was replaced");
        need(decoded[0].position==vertices[0].position&&decoded[0].color==vertices[0].color&&
             decoded[0].uv==vertices[0].uv,"Zero-normal decoding changed other attributes");
    }
    for(size_t at:{size_t(0),size_t(4),size_t(8),size_t(20),size_t(24)})for(uint32_t invalid:{0x7F800000u,0xFF800000u,0x7FC12345u}) {
        auto bad=one;put(bad,at,invalid);rejects([&]{decodeRigidVertices(bad,decl,28);},"Nonfinite consumed rigid input accepted");
    }
    for(uint32_t stride:{0u,27u,29u,1024u})rejects([&]{decodeRigidVertices(one,decl,stride);},"Invalid rigid stride accepted");
    rejects([&]{decodeRigidVertices({},decl,28);},"Empty rigid stream accepted");
    for(size_t row=0;row<4;++row)for(size_t field=0;field<3;++field) {
        auto bad=decl;put(bad,12*row+4*field,UINT32_MAX);
        rejects([&]{decodeRigidVertices(one,bad,28);},"Invalid rigid declaration row accepted");
    }
    {auto bad=decl;put(bad,12+8,0);rejects([&]{decodeRigidVertices(one,bad,28);},"Duplicate rigid input accepted");}
    {auto bad=decl;put(bad,12,0x0001000C);rejects([&]{decodeRigidVertices(one,bad,28);},"Alternate rigid stream accepted");}
    {auto bad=decl;put(bad,12,27);rejects([&]{decodeRigidVertices(one,bad,28);},"Unaligned rigid input accepted");}
    for(size_t at=48;at<60;at+=4){auto bad=decl;put(bad,at,1);rejects([&]{decodeRigidVertices(one,bad,28);},"Invalid rigid terminator accepted");}
    // The selected VS ignores a known tangent and UV1 even when payload is NaN.
    auto extra=decl;extra.resize(84);std::copy(decl.begin()+48,decl.end(),extra.begin()+72);
    put(extra,48,28);put(extra,52,0x002C23A5);put(extra,56,0x00050100);
    put(extra,60,36);put(extra,64,0x002A2187);put(extra,68,0x00060000);
    auto padded=one;padded.resize(40,255);auto extended=decodeRigidVertices(padded,extra,40);
    need(extended.size()==1&&extended[0].normal==vertices[0].normal&&extended[0].uv==vertices[0].uv,"Dead rigid inputs affected decoded attributes");
    need(extended[0].uv1==std::array<float,2>{},"Unconsumed UV1 was not zero initialized");
    need(extended[0].tangent==std::array<float,3>{},"Unconsumed tangent was not zero initialized");
    rejects([&]{decodeRigidVertices(one,decl,28,true);},"Missing consumed UV1 accepted");
    rejects([&]{decodeRigidVertices(padded,extra,40,true);},"Nonfinite consumed UV1 accepted");
    auto dualDecl=decl;dualDecl.resize(72);std::copy(decl.begin()+48,decl.end(),dualDecl.begin()+60);
    put(dualDecl,48,28);put(dualDecl,52,0x002C23A5);put(dualDecl,56,0x00050100);
    auto dualSource=one;dualSource.resize(36);put(dualSource,28,0x80000000);put(dualSource,32,0x3F000001);
    const auto savedDual=dualSource;const auto dual=decodeRigidVertices(dualSource,dualDecl,36,true);
    need(dualSource==savedDual&&dual.size()==1&&dual[0].uv==vertices[0].uv,
         "Dual UV decode changed source or UV0");
    for(size_t lane=0;lane<2;++lane)need(std::bit_cast<uint32_t>(dual[0].uv1[lane])==word(dualSource,28+4*lane),
         "Dual UV1 bits changed");
    for(size_t at:{size_t(28),size_t(32)})for(uint32_t invalid:{0x7F800000u,0xFF800000u,0x7FC12345u}) {
        auto bad=dualSource;put(bad,at,invalid);
        rejects([&]{decodeRigidVertices(bad,dualDecl,36,true);},"Nonfinite dual UV1 accepted");
        need(decodeRigidVertices(bad,dualDecl,36)[0].uv1==std::array<float,2>{},"Dead UV1 affected decode");
    }
    // Tangent is opt-in packed 002A2187 XYZ, same as normals. High two bits
    // are dead; handedness is not inferred. Uses usage6/index0 semantic.
    rejects([&]{decodeRigidVertices(one,decl,28,false,true);},"Missing consumed tangent accepted");
    rejects([&]{decodeRigidVertices(dualSource,dualDecl,36,false,true);},"Missing tangent with UV1 accepted");
    need(decodeRigidVertices(padded,extra,40)[0].tangent==std::array<float,3>{},"Dead tangent affected decode");
    {
        // Stride40 layout mirrors the observed extra declaration: UV1 at28,
        // tangent at36. Verify opt-in decode preserves source and other attrs.
        auto tangentSource=padded;const auto savedTangent=tangentSource;
        const auto withTangent=decodeRigidVertices(tangentSource,extra,40,false,true);
        need(tangentSource==savedTangent&&withTangent.size()==1,"Tangent decode changed source or extent");
        need(withTangent[0].position==vertices[0].position&&withTangent[0].normal==vertices[0].normal&&
             withTangent[0].color==vertices[0].color&&withTangent[0].uv==vertices[0].uv,
             "Tangent decode changed other attributes");
        need(withTangent[0].uv1==std::array<float,2>{},"Unconsumed UV1 changed during tangent decode");
        // 0xFFFFFFFF payload gives per-lane raw1023; high bits must not leak.
        for(float t:withTangent[0].tangent)need(t==float(double(-1)/511.0),"All-ones tangent lane differs");
        // Zero packed tangent decodes to zero, not a fallback direction.
        for(uint32_t high:{0u,0x40000000u,0x80000000u,0xC0000000u}) {
            auto zero=tangentSource;put(zero,36,high);const auto savedZero=zero;
            const auto decoded=decodeRigidVertices(zero,extra,40,false,true);
            need(zero==savedZero&&decoded.size()==1,"Zero-tangent decoding changed storage");
            for(float t:decoded[0].tangent)need(std::bit_cast<uint32_t>(t)==0,"Original zero tangent was replaced");
            need(decoded[0].position==vertices[0].position,"Zero-tangent decode changed position");
        }
        // Exhaustive per-lane 10-bit encodings with high-bit variations.
        for(size_t lane=0;lane<3;++lane)for(uint32_t raw=0;raw<1024;++raw) {
            for(uint32_t high:{0u,0x40000000u,0x80000000u,0xC0000000u}) {
                auto sample=tangentSource;
                uint32_t packed=0;
                for(size_t other=0;other<3;++other) {
                    uint32_t value=(other==lane)?raw:0;
                    packed|=value<<(10*other);
                }
                packed|=high;
                put(sample,36,packed);
                const auto decoded=decodeRigidVertices(sample,extra,40,false,true);
                const double expected=raw<512?double(raw)/511.0:-std::min(1.0,double(1024-raw)/511.0);
                need(decoded[0].tangent[lane]==float(expected),"Tangent packed decoding differs");
                for(size_t other=0;other<3;++other)if(other!=lane)
                    need(decoded[0].tangent[other]==0.0f,"Tangent lane crosstalk");
            }
        }
        // UV1 and tangent together remain independent.
        auto both=tangentSource;put(both,28,0x3F800000);put(both,32,0x40000000);
        put(both,36,0x00000000);
        const auto decodedBoth=decodeRigidVertices(both,extra,40,true,true);
        need(decodedBoth[0].uv1==std::array<float,2>{1.0f,2.0f},"UV1+tangent UV1 bits changed");
        for(float t:decodedBoth[0].tangent)need(t==0.0f,"UV1+tangent tangent changed");
    }
    {
        // Mesh particles fetch position, packed color and UV only. No normal
        // may be read from the position bytes when that element is absent.
        Bytes particleDecl(48);const uint32_t rows[][3]={{0,0x002A23B9,0},{12,0x00182886,0x000A0000},
            {16,0x002C23A5,0x00050000},{0x00FF0000,UINT32_MAX,0}};
        for(size_t i=0;i<4;++i)for(size_t j=0;j<3;++j)put(particleDecl,12*i+4*j,rows[i][j]);
        Bytes data(24);std::copy_n(one.begin(),12,data.begin());std::copy(one.begin()+16,one.end(),data.begin()+12);
        const auto decoded=decodeRigidVertices(data,particleDecl,24,false,false,false);
        need(decoded.size()==1&&decoded[0].position==vertices[0].position&&decoded[0].color==vertices[0].color&&
             decoded[0].uv==vertices[0].uv&&decoded[0].normal==std::array<float,3>{},"Particle input decoding differs");
        rejects([&]{decodeRigidVertices(data,particleDecl,24);},"Lit mesh accepted a missing normal");
        auto missingUv=particleDecl;put(missingUv,24+8,0x00050001);
        rejects([&]{decodeRigidVertices(data,missingUv,24,false,false,false);},"Particle accepted missing UV0");
    }
    if(argc==2) {
        const std::filesystem::path dir=argv[1];auto geometry=read(dir/"scene-geometry.bin");
        auto elements=read(dir/"scene-elements.bin"),data=read(dir/"scene-vertices.bin");
        const auto stride=word(geometry,4);const auto captured=decodeRigidVertices(data,elements,stride);
        need(captured.size()==2613&&stride==28,"LIVE017 rigid geometry extent differs");
        for(size_t i=0;i<captured.size();++i) {
            for(size_t lane=0;lane<3;++lane)need(std::bit_cast<uint32_t>(captured[i].position[lane])==word(data,i*stride+4*lane),"Live rigid position bits changed");
            for(float n:captured[i].normal)need(std::isfinite(n)&&n>=-1&&n<=1,"Live rigid normal outside normalized range");
        }
        std::printf("Verified LIVE017's %zu rigid vertices. ",captured.size());
    } else need(argc==1,"Supply optional capture directory");
    std::printf("PASS rigid vertex decoder: %zu checks, all 1024 normal and 256 color encodings per lane\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL rigid vertex decoder after %zu checks: %s\n",checks,e.what());return 1;}
