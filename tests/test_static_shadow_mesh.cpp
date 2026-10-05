#include "runtime/static_shadow_mesh.h"
#include "runtime/runtime.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
using Bytes=std::vector<uint8_t>;
using Simpsons::decodeStaticShadowVertices;
size_t checks{};
void need(bool value,const char* why) {++checks;if(!value)throw std::runtime_error(why);}
template<class F> void rejects(F&& f,const char* why) {
    bool rejected=false;
    try {f();} catch(const Simpsons::Failure&) {rejected=true;}
    need(rejected,why);
}
void put(Bytes& bytes,size_t at,uint32_t value) {
    if(at>bytes.size()||bytes.size()-at<4)throw std::runtime_error("Invalid fixture write");
    bytes[at]=uint8_t(value>>24);bytes[at+1]=uint8_t(value>>16);
    bytes[at+2]=uint8_t(value>>8);bytes[at+3]=uint8_t(value);
}
uint32_t word(const Bytes& bytes,size_t at) {
    if(at>bytes.size()||bytes.size()-at<4)throw std::runtime_error("Truncated capture word");
    return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];
}
template<size_t N> void bits(const std::array<float,N>& actual,const std::array<uint32_t,N>& expected) {
    for(size_t lane=0;lane<N;++lane)
        need(std::bit_cast<uint32_t>(actual[lane])==expected[lane],"Attribute bits or lane order changed");
}
void inert(const Simpsons::Graphics::ShadowMeshVertex& vertex) {
    // The native layout still fetches these eight lanes. Positive zero is a
    // deterministic absent attribute, never a fabricated unit skin weight.
    // Main must validate committed c40.x==0; this CPU decoder cannot do that.
    bits(vertex.weights,std::array<uint32_t,4>{});
    bits(vertex.indices,std::array<uint32_t,4>{});
}
Bytes declaration() {
    // Independent literal reach-game-009 declaration, including its terminator.
    constexpr std::array<uint32_t,18> words={
        0x00000000,0x002A23B9,0x00000000,
        0x0000000C,0x002A2187,0x00030000,
        0x00000010,0x00182886,0x000A0000,
        0x00000014,0x002C23A5,0x00050000,
        0x0000001C,0x002C23A5,0x00050100,
        0x00FF0000,0xFFFFFFFF,0x00000000};
    Bytes result(words.size()*4);
    for(size_t i=0;i<words.size();++i)put(result,i*4,words[i]);
    return result;
}
Bytes vertex() {
    // First captured vertex; normal/color/UV1 differ from all consumed lanes.
    constexpr std::array<uint32_t,9> words={
        0xBF3F41F2,0x3FAA7525,0x3E73B646,0x055C7E3D,0xE5000064,
        0x3F290FF9,0x3D75C290,0x3E67EF9D,0x3F2A0276};
    Bytes result(36);
    for(size_t i=0;i<words.size();++i)put(result,i*4,words[i]);
    return result;
}
void valuesAndOwnership() {
    auto d=declaration(),v=vertex(),second=vertex();
    put(second,0,0x80000000);put(second,4,0x00000001);put(second,8,0xFF7FFFFF);
    put(second,20,0xBF800000);put(second,24,0x40800000); // UVs are not clamped or flipped.
    // Dead normal/color/UV1 payloads need not be finite floats.
    for(size_t at:std::array<size_t,4>{12,16,28,32})put(second,at,0x7F800000);
    v.insert(v.end(),second.begin(),second.end());
    const auto originalV=v,originalD=d;
    const auto out=decodeStaticShadowVertices(v,d,36);
    need(out.size()==2,"Vertex count differs");
    bits(out[0].position,std::array<uint32_t,3>{0xBF3F41F2,0x3FAA7525,0x3E73B646});
    bits(out[0].uv,std::array<uint32_t,2>{0x3F290FF9,0x3D75C290});
    bits(out[1].position,std::array<uint32_t,3>{0x80000000,0x00000001,0xFF7FFFFF});
    bits(out[1].uv,std::array<uint32_t,2>{0xBF800000,0x40800000});
    inert(out[0]);inert(out[1]);
    need(v==originalV&&d==originalD,"Decoder mutated input memory");
    std::fill(v.begin(),v.end(),0);std::fill(d.begin(),d.end(),0);
    bits(out[0].position,std::array<uint32_t,3>{0xBF3F41F2,0x3FAA7525,0x3E73B646});
    bits(out[1].uv,std::array<uint32_t,2>{0xBF800000,0x40800000});
}
void tangentDeclaration() {
    auto d=declaration(),v=vertex();
    const auto expected=decodeStaticShadowVertices(v,d,36);
    const auto originalD=d,originalV=v;
    // Live stride40 layout inserts packed tangent0 after normal0 and shifts
    // color/UV0/UV1 by four bytes. Its payload is dead under c40.x=false.
    d.insert(d.begin()+24,12,0);
    put(d,24,16);put(d,28,0x002A2187);put(d,32,0x00060000);
    for(size_t row=3;row<6;++row)put(d,12*row,word(d,12*row)+4);
    v.insert(v.begin()+16,4,0);put(v,16,0x7FC12345);
    const auto beforeD=d,beforeV=v;
    const auto decoded=decodeStaticShadowVertices(v,d,40);
    const auto sameInputs=[&](const auto& actual){return actual.size()==expected.size()&&
        !std::memcmp(actual.data(),expected.data(),expected.size()*sizeof(expected[0]));};
    need(decoded.size()==1&&sameInputs(decoded),"Unused tangent changed shadow inputs");
    need(d==beforeD&&v==beforeV,"Tangent decoder changed owned declaration/payload");
    for(uint32_t bits:std::array<uint32_t,4>{0,0xFFFFFFFF,0x7F800000,0xFF800000}) {
        put(v,16,bits);need(sameInputs(decodeStaticShadowVertices(v,d,40)),"Packed dead tangent was interpreted as a finite float input");
    }
    for(const auto& change:std::array<std::array<uint32_t,2>,7>{{
        {24,17},{24,40},{24,0x00010010},{28,0x002C23A5},{32,0x00060100},{32,0x01060000},{28,UINT32_MAX}}}) {
        auto bad=d;put(bad,change[0],change[1]);
        rejects([&]{decodeStaticShadowVertices(v,bad,40);},"Malformed tangent declaration accepted");
    }
    auto duplicate=d;duplicate.insert(duplicate.end()-12,d.begin()+24,d.begin()+36);
    rejects([&]{decodeStaticShadowVertices(v,duplicate,40);},"Duplicate tangent semantic accepted");
    need(declaration()==originalD&&vertex()==originalV,"Literal baseline fixture changed");
}
void declarationValidation() {
    const auto d=declaration(),v=vertex();
    for(size_t row=0;row<5;++row) {
        // Dead rows still require the observed type, stream, method and bounds.
        for(const auto& change:std::array<std::array<uint32_t,2>,5>{{
                {0,1},{0,36},{0,0x00010000},{4,0},{8,0x01000000}}}) {
            auto bad=d;put(bad,row*12+change[0],change[1]);
            rejects([&]{decodeStaticShadowVertices(v,bad,36);},"Malformed declaration row accepted");
        }
        auto duplicate=d;
        duplicate.insert(duplicate.end()-12,d.begin()+row*12,d.begin()+(row+1)*12);
        rejects([&]{decodeStaticShadowVertices(v,duplicate,36);},"Duplicate semantic accepted");
    }
    for(size_t row:std::array<size_t,2>{0,3}) {
        auto missing=d;missing.erase(missing.begin()+row*12,missing.begin()+(row+1)*12);
        rejects([&]{decodeStaticShadowVertices(v,missing,36);},"Missing consumed semantic accepted");
    }
    // Absent skin attributes must not be inferred from blend/tangent/extra UV rows.
    for(uint32_t semantic:std::array<uint32_t,6>{0x00010000,0x00020000,0x00060000,0x00050200,0x00030100,0x000A0100}) {
        auto bad=d;put(bad,56,semantic);
        rejects([&]{decodeStaticShadowVertices(v,bad,36);},"Unqualified semantic accepted");
    }
    for(const auto& change:std::array<std::array<uint32_t,2>,5>{{
            {60,0xFFFF0000},{60,0x00FF0004},{64,0},{68,0x01000000},{68,0x00000100}}}) {
        auto bad=d;put(bad,change[0],change[1]);
        rejects([&]{decodeStaticShadowVertices(v,bad,36);},"Malformed terminator accepted");
    }
    auto early=d;std::copy_n(d.end()-12,12,early.begin()+12);
    rejects([&]{decodeStaticShadowVertices(v,early,36);},"Early terminator accepted");
    auto reordered=d;
    for(size_t row=0;row<5;++row)std::copy_n(d.begin()+row*12,12,reordered.begin()+(4-row)*12);
    bits(decodeStaticShadowVertices(v,reordered,36)[0].uv,std::array<uint32_t,2>{0x3F290FF9,0x3D75C290});
    // Follow offsets rather than declaration order or fixed capture offsets.
    auto moved=d,relocated=v;put(moved,0,20);put(moved,36,0);put(moved,48,12);
    std::copy_n(v.begin(),12,relocated.begin()+20);std::copy_n(v.begin()+20,8,relocated.begin());
    const auto out=decodeStaticShadowVertices(relocated,moved,36);
    bits(out[0].position,std::array<uint32_t,3>{0xBF3F41F2,0x3FAA7525,0x3E73B646});
    bits(out[0].uv,std::array<uint32_t,2>{0x3F290FF9,0x3D75C290});inert(out[0]);
}
void finiteAndExtents() {
    const auto d=declaration(),v=vertex();
    for(size_t at:std::array<size_t,5>{0,4,8,20,24})
        for(uint32_t invalid:std::array<uint32_t,3>{0x7FC12345,0x7F800000,0xFF800000}) {
            auto bad=v;put(bad,at,invalid);
            rejects([&]{decodeStaticShadowVertices(bad,d,36);},"Nonfinite consumed attribute accepted");
        }
    for(size_t size:std::array<size_t,4>{0,1,35,37}) {
        auto bad=v;bad.resize(size);
        rejects([&]{decodeStaticShadowVertices(bad,d,36);},"Partial or empty vertex payload accepted");
    }
    for(size_t size:std::array<size_t,6>{0,12,60,71,73,66*12}) {
        auto bad=d;bad.resize(size);
        rejects([&]{decodeStaticShadowVertices(v,bad,36);},"Invalid declaration extent/end accepted");
    }
    for(uint32_t stride:std::array<uint32_t,5>{0,4,20,37,1024}) {
        auto bad=v;bad.resize(stride);
        rejects([&]{decodeStaticShadowVertices(bad,d,stride);},"Invalid or unrepresentable original stride accepted");
    }
    Bytes largeOwner(size_t(65536)*36);
    for(size_t at=0;at<largeOwner.size();at+=36)std::copy(v.begin(),v.end(),largeOwner.begin()+at);
    const auto ownerBefore=largeOwner;
    const auto large=decodeStaticShadowVertices(largeOwner,d,36);
    need(large.size()==65536,"Valid original65536 static-shadow owner rejected");
    bits(large.front().position,std::array<uint32_t,3>{0xBF3F41F2,0x3FAA7525,0x3E73B646});
    bits(large.back().position,std::array<uint32_t,3>{0xBF3F41F2,0x3FAA7525,0x3E73B646});
    need(largeOwner==ownerBefore,"Large static-shadow decoder changed its owner");
    auto padded=v;padded.resize(40,0xFF);padded.insert(padded.end(),v.begin(),v.end());padded.resize(80,0xFF);
    const auto out=decodeStaticShadowVertices(padded,d,40);
    need(out.size()==2,"Padded stride lost a vertex");
    bits(out[1].position,std::array<uint32_t,3>{0xBF3F41F2,0x3FAA7525,0x3E73B646});inert(out[1]);
}
Bytes read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    need(bool(input),"Optional capture file could not be opened");
    Bytes result{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    need(!input.bad(),"Optional capture read failed");return result;
}
void capturedPayload(const std::filesystem::path& directory) {
    const auto d=read(directory/"static-shadow-elements.bin"),v=read(directory/"static-shadow-vertices.bin");
    const auto g=read(directory/"static-shadow-geometry.bin"),m=read(directory/"static-shadow-metadata.bin");
    need(d==declaration()&&v.size()==10152&&g.size()==120&&m.size()==44,"Unexpected captured profile/extent");
    need(word(g,4)==36&&word(g,8)==6&&word(m,0x24)==0,"Captured stride, row count or bone count differs");
    const auto out=decodeStaticShadowVertices(v,d,36);
    need(out.size()==282,"Captured vertex count differs");
    bits(out.front().position,std::array<uint32_t,3>{0xBF3F41F2,0x3FAA7525,0x3E73B646});
    bits(out.front().uv,std::array<uint32_t,2>{0x3F290FF9,0x3D75C290});
    bits(out.back().position,std::array<uint32_t,3>{0x3F26A162,0x3FE102DE,0xBE836113});
    bits(out.back().uv,std::array<uint32_t,2>{0x3D95182A,0x3E0346DC});
    for(size_t i=0;i<out.size();++i) {
        for(size_t lane=0;lane<3;++lane)
            need(std::bit_cast<uint32_t>(out[i].position[lane])==word(v,i*36+lane*4),"Captured position changed");
        for(size_t lane=0;lane<2;++lane)
            need(std::bit_cast<uint32_t>(out[i].uv[lane])==word(v,i*36+20+lane*4),"Captured UV changed");
        inert(out[i]);
    }
}
}

int main(int argc,char** argv) {
    if(argc>2) {std::fprintf(stderr,"Usage: StaticShadowMeshTests [capture-directory]\n");return 2;}
    size_t failures=0;
    auto run=[&](const char* name,auto&& test) {
        try {test();std::printf("PASS %s\n",name);}
        catch(const std::exception& e) {++failures;std::fprintf(stderr,"FAIL %s: %s\n",name,e.what());}
    };
    run("exact values, inert lanes and ownership",valuesAndOwnership);
    run("declaration validation",declarationValidation);
    run("original-compatible unused tangent and malformed rows",tangentDeclaration);
    run("finite attributes and extents",finiteAndExtents);
    if(argc==2)run("optional captured payload",[&]{capturedPayload(argv[1]);});
    std::printf("Static shadow mesh: %zu checks, %zu failing groups\n",checks,failures);
    return failures?1:0;
}
