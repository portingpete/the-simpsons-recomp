#include "runtime/character_mesh.h"
#include "runtime/rigid_vertices.h"
#include "runtime/skin_vertices.h"
#include "runtime/sky_vertices.h"
#include "runtime/static_shadow_mesh.h"
#include "runtime/zprepass_vertices.h"
#include "runtime/runtime.h"
#include "common/geometry_extent.h"
#include "renderer/r16_index_validation.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>

namespace {
using Bytes=std::vector<uint8_t>;
size_t checks{};
void need(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F action){bool failed=false;try{action();}catch(const Simpsons::Failure&){failed=true;}need(failed,"Malformed consumed geometry span admitted");}
void put(Bytes& bytes,size_t at,uint32_t value){for(size_t lane=0;lane<4;++lane)bytes.at(at+lane)=uint8_t(value>>(24-8*lane));}
template<size_t N>Bytes literal(const std::array<std::array<uint32_t,3>,N>& rows) {
    Bytes result(12*N);for(size_t row=0;row<N;++row)for(size_t lane=0;lane<3;++lane)put(result,12*row+4*lane,rows[row][lane]);return result;
}
Bytes rigidDeclaration() {
    return literal(std::array<std::array<uint32_t,3>,5>{{
        {0,0x002A23B9,0},{12,0x002A2187,0x00030000},{16,0x00182886,0x000A0000},
        {20,0x002C23A5,0x00050000},{0x00FF0000,UINT32_MAX,0}}});
}
Bytes skyDeclaration() {
    return literal(std::array<std::array<uint32_t,3>,4>{{
        {0,0x002A23B9,0},{12,0x002C23A5,0x00050000},
        {20,0x002C23A5,0x00050100},{0x00FF0000,UINT32_MAX,0}}});
}
Bytes skinDeclaration() {
    return literal(std::array<std::array<uint32_t,3>,13>{{
        {0,0x002A23B9,0},{12,0x002A2187,0x00030000},{16,0x002C23A5,0x00050000},
        {24,0x001A2286,0x00020000},{28,0x001A23A6,0x00010000},{44,0x00182886,0x000A0000},
        {0x00010000,0x002A23B9,0x00000100},{0x00020000,0x002A23B9,0x00000200},
        {0x00030000,0x002A23B9,0x00000300},{0x00040000,0x002A23B9,0x00000400},
        {0x00050000,0x002A23B9,0x00000500},{0x00060000,0x002A23B9,0x00000600},
        {0x00FF0000,UINT32_MAX,0}}});
}
Bytes record() {
    Bytes result(64);put(result,0,0x3FA00000);put(result,4,0xC0200000);put(result,8,0x40700000);return result;
}
template<class Decode>void ownedSpans(const char* family,size_t consumedEnd,const Bytes& declaration,Decode decode) {
    constexpr size_t count=65536,stride=64;
    const auto row=record();Bytes owner(count*stride);
    for(size_t at=0;at<owner.size();at+=stride)std::copy(row.begin(),row.end(),owner.begin()+at);
    const auto original=owner,declBefore=declaration;
    auto result=decode(owner,declaration,uint32_t(stride));
    need(result.size()==count&&result.front().position==std::array<float,3>{1.25f,-2.5f,3.75f}&&
         result.back().position==result.front().position,"Large owned span lost fetched vertices or position bits");
    // This tail is aligned, smaller than every selected attribute end, and
    // deliberately contains nonfinite bytes. No shader fetch may consume it.
    owner.resize(count*stride+4,0xFF);
    auto tail=decode(owner,declaration,uint32_t(stride));
    need(tail.size()==count&&!std::memcmp(tail.data(),result.data(),count*sizeof(result[0])),
         "Unused partial owner tail changed the fetched vertex set");
    owner.resize((count-1)*stride+consumedEnd);
    auto unpadded=decode(owner,declaration,uint32_t(stride));
    need(unpadded.size()==count&&!std::memcmp(unpadded.data(),result.data(),count*sizeof(result[0])),
         "Absent final unused record padding rejected a complete selected fetch");
    owner.resize(owner.size()-4);
    auto truncated=decode(owner,declaration,uint32_t(stride));
    need(truncated.size()==count-1,"Missing consumed attribute bytes acquired an addressable vertex");
    constexpr std::array<uint16_t,7> indices{0,1,2,UINT16_MAX,2,1,3};
    need(Simpsons::Graphics::validR16DrawRange(indices,uint32_t(unpadded.size()),0,7,65532)&&
         !Simpsons::Graphics::validR16DrawRange(indices,uint32_t(truncated.size()),0,7,65532),
         "Selected signed-base range ignored the consumed owner boundary");
    owner.resize(consumedEnd-4);rejects([&]{decode(owner,declaration,uint32_t(stride));});
    owner=original;owner.pop_back();rejects([&]{decode(owner,declaration,uint32_t(stride));});
    rejects([&]{decode(original,declaration,63);});rejects([&]{decode(original,declaration,1024);});
    auto nonfinite=original;put(nonfinite,(count-1)*stride,0x7FC12345);const auto badBefore=nonfinite;
    rejects([&]{decode(nonfinite,declaration,uint32_t(stride));});
    need(nonfinite==badBefore&&declaration==declBefore,"Rejected large input changed its owned source");
    std::printf("AUDIT_GEOMETRY_DECODE_EXTENT family=%s stride=64 vertices=65536 consumed_end=%zu full_owner=passed unused_tail=passed absent_final_padding=passed selected_bound=passed malformed=passed scope=cpu_decoder\n",family,consumedEnd);
}
void morphOwner() {
    std::vector<Simpsons::Graphics::SkinVertex> vertices(65536);Bytes bytes(vertices.size()*12+4);
    put(bytes,0,0x3F800000);put(bytes,(vertices.size()-1)*12+8,0x40000000);put(bytes,bytes.size()-4,0x7FC12345);
    const auto original=bytes;Simpsons::decodeSkinMorphStream(vertices,1,bytes);
    need(vertices.front().morph1[0]==1&&vertices.back().morph1[2]==2&&bytes==original,
         "Large morph owner or its unused tail changed consumed deltas");
    const auto before=vertices;bytes.resize(vertices.size()*12-4);
    rejects([&]{Simpsons::decodeSkinMorphStream(vertices,1,bytes);});
    need(!std::memcmp(vertices.data(),before.data(),vertices.size()*sizeof(vertices[0])),
         "Rejected short morph owner partially published deltas");
    // An actual host allocation, rather than a fabricated out-of-array span,
    // verifies the first unproved upper resource bit fails before publication.
    Bytes upperAlias(Simpsons::originalGeometryMaxVertexBytes+4);
    rejects([&]{Simpsons::decodeSkinMorphStream(vertices,1,upperAlias);});
    need(!std::memcmp(vertices.data(),before.data(),vertices.size()*sizeof(vertices[0])),
         "Rejected upper-bit morph owner partially published deltas");
}
}
int main()try {
    const auto rigid=rigidDeclaration(),sky=skyDeclaration(),skin=skinDeclaration();
    // Mono consumes the rigid float3/normal/color/UV decoder with normal dead;
    // character and static shadow share a host layout but different fetch sets.
    ownedSpans("rigid",28,rigid,[](const auto& b,const auto& d,uint32_t s){return Simpsons::decodeRigidVertices(b,d,s);});
    ownedSpans("mono",28,rigid,[](const auto& b,const auto& d,uint32_t s){return Simpsons::decodeRigidVertices(b,d,s,false,false,false);});
    ownedSpans("sky",20,sky,[](const auto& b,const auto& d,uint32_t s){return Simpsons::decodeSkyVertices(b,d,s);});
    ownedSpans("skin",48,skin,[](const auto& b,const auto& d,uint32_t s){return Simpsons::decodeSkinVertices(b,d,s);});
    ownedSpans("character_shadow",44,skin,[](const auto& b,const auto& d,uint32_t s){return Simpsons::decodeCharacterVertices(b,d,s,1);});
    ownedSpans("static_shadow",28,rigid,[](const auto& b,const auto& d,uint32_t s){return Simpsons::decodeStaticShadowVertices(b,d,s);});
    ownedSpans("zprepass",12,rigid,[](const auto& b,const auto& d,uint32_t s){return Simpsons::decodeStaticZPrepassVertices(b,d,s);});
    morphOwner();std::printf("PASS large geometry decoders: %zu checks; actual consumed fetch bounds, selected signed-base indices and atomic malformed morph rejection\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL large geometry decoders after %zu checks: %s\n",checks,error.what());return 1;}
