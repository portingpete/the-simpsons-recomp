// Independent original selected maps + dirty projection and consumed UV1.
// This test uses CPU metadata/decoding; whole traversal is a separate fixture.
#include "renderer/effect_reflection.h"
#include "renderer/material_texture_usage.h"
#include "runtime/skin_material_constants.h"
#include "runtime/skin_vertices.h"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
namespace {
using namespace Simpsons;
constexpr uint32_t source=0x8204A058;
size_t checks{};
void need(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
uint32_t word(std::span<const uint8_t> bytes,size_t at){need(at<=bytes.size()&&bytes.size()-at>=4,"Original dual UV evidence overrun");return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];}
void put(std::span<uint8_t> bytes,size_t at,uint32_t value){for(uint32_t i=0;i<4;++i)bytes[at+i]=uint8_t(value>>(24-8*i));}
template<class F>void rejected(F function){try{function();}catch(const std::exception&){++checks;return;}need(false,"Malformed dual UV input admitted");}
constexpr std::array<SkinMaterialRow,5> vertexRows{{{19,100,47},{20,104,46},{21,108,45},{22,112,44},{23,116,43}}};
constexpr std::array<SkinMaterialRow,3> pixelRows{{{14,80,49},{24,120,42},{93,1200,48}}};

void projection(bool alpha){
    std::vector<uint32_t> words(1204);for(uint32_t i=0;i<words.size();++i)words[i]=std::bit_cast<uint32_t>(float(i)*.03125f-7);
    words[101]=0x80000000;words[105]=1;
    Graphics::SkinVertexConstants vertices{};Graphics::SkinPixelConstants pixels{};
    for(uint32_t r=0;r<vertices.size();++r)for(uint32_t lane=0;lane<4;++lane)vertices[r][lane]=float(1000+4*r+lane);
    for(uint32_t r=0;r<pixels.size();++r)for(uint32_t lane=0;lane<4;++lane)pixels[r][lane]=float(2000+4*r+lane);
    std::array<uint8_t,128> dirty{};
    // Every private leaf is visited independently. Unmapped leaves must not
    // overwrite inherited eye/world/ticker/object/palette or bank-tail lanes.
    for(uint32_t leaf=0;leaf<94;++leaf){
        dirty.fill(0);dirty[leaf/8]=uint8_t(0x80>>(leaf&7));const auto beforeVS=vertices;const auto beforePS=pixels;
        projectSkinVertexMaterial(words,dirty,vertices,source,alpha);
        projectSkinMaterial(words,dirty,pixels,source,alpha);
        const auto v=std::find_if(vertexRows.begin(),vertexRows.end(),[&](const auto& r){return r.leaf==leaf;});
        const auto p=std::find_if(pixelRows.begin(),pixelRows.end(),[&](const auto& r){return r.leaf==leaf;});
        for(uint32_t r=0;r<vertices.size();++r)for(uint32_t lane=0;lane<4;++lane)
            need(std::bit_cast<uint32_t>(vertices[r][lane])==(v!=vertexRows.end()&&r==v->reg?words[v->word+lane]:std::bit_cast<uint32_t>(beforeVS[r][lane])),"Dirty dual UV VS projection changed clean/unmapped lanes");
        for(uint32_t r=0;r<pixels.size();++r)for(uint32_t lane=0;lane<4;++lane)
            need(std::bit_cast<uint32_t>(pixels[r][lane])==(p!=pixelRows.end()&&r==p->reg?words[p->word+lane]:std::bit_cast<uint32_t>(beforePS[r][lane])),"Dirty dual UV PS projection changed clean/unmapped lanes");
    }
    dirty.fill(0xFF);const auto vs=vertices;const auto ps=pixels;
    projectSkinVertexMaterial(words,dirty,vertices,source,alpha);projectSkinMaterial(words,dirty,pixels,source,alpha);
    need(vertices==vs&&pixels==ps,"Full dual UV material dirty mask changed unrelated inherited rows");
    dirty.fill(0);projectSkinVertexMaterial(words,dirty,vertices,source,alpha);projectSkinMaterial(words,dirty,pixels,source,alpha);
    need(vertices==vs&&pixels==ps,"Clean dual UV material changed constants");
    rejected([&]{projectSkinVertexMaterial(std::span<const uint32_t>(words).first(1203),dirty,vertices,source,alpha);});
    rejected([&]{projectSkinMaterial(std::span<const uint32_t>(words).first(1203),dirty,pixels,source,alpha);});
    need(vertices==vs&&pixels==ps,"Rejected short dual UV material bank changed constants");
    dirty.fill(0);dirty[26/8]|=uint8_t(0x80>>(26&7));dirty[89/8]|=uint8_t(0x80>>(89&7));
    projectSkinBoneMatrices(words,dirty,vertices,source);
    for(uint32_t r=0;r<vertices.size();++r)for(uint32_t lane=0;lane<4;++lane){
        const auto expected=r>=52&&r<55?words[128+4*(r-52)+lane]:r>=241&&r<244?words[1136+4*(r-241)+lane]:std::bit_cast<uint32_t>(vs[r][lane]);
        need(std::bit_cast<uint32_t>(vertices[r][lane])==expected,"Relocated dual UV palette lost original three-row extent");
    }
}

void vertices(){
    std::vector<uint8_t> bytes(56),declaration(168);size_t at=0;
    const auto add=[&](uint32_t offset,uint32_t type,uint32_t semantic){put(declaration,at,offset);put(declaration,at+4,type);put(declaration,at+8,semantic);at+=12;};
    add(0,0x002A23B9,0);add(12,0x002A2187,0x00030000);add(16,0x002C23A5,0x00050000);add(24,0x002C23A5,0x00050100);
    add(32,0x001A2286,0x00020000);add(36,0x001A23A6,0x00010000);add(52,0x00182886,0x000A0000);
    for(uint32_t s=1;s<=6;++s)add(s<<16,0x002A23B9,s<<8);add(0x00FF0000,UINT32_MAX,0);
    put(bytes,0,std::bit_cast<uint32_t>(-.5f));put(bytes,4,std::bit_cast<uint32_t>(.75f));put(bytes,8,0x3F000000);
    put(bytes,12,511|(512u<<10)|(1023u<<20));put(bytes,16,0x3E800000);put(bytes,20,0x3F000000);
    put(bytes,24,0x3F400000);put(bytes,28,0x3E000000);put(bytes,32,0x00000009);put(bytes,36,0x3F800000);put(bytes,52,0x80402010);
    const auto saved=bytes,declSaved=declaration;const auto out=decodeSkinVertices(bytes,declaration,56,source);
    need(out.size()==1&&out[0].position==std::array<float,3>{-.5f,.75f,.5f},"Dual UV position differs");
    need(out[0].uv==std::array<float,2>{.25f,.5f}&&out[0].uv1==std::array<float,2>{.75f,.125f},"Dual UV consumed UV0/UV1 association differs");
    need(out[0].indices==std::array<float,4>{9,0,0,0}&&out[0].weights==std::array<float,4>{1,0,0,0},"Dual UV influence byte order differs");
    need(out[0].normal==std::array<float,3>{1,-1,-1.0f/511}&&out[0].color==std::array<float,4>{64.0f/255,32.0f/255,16.0f/255,128.0f/255},"Dual UV packed normal/color differs");
    need(skinConsumesUV1(source,false)&&skinConsumesUV1(source,true),"Dual UV selected pass lost an original UV1 fetch");
    const auto alpha=decodeSkinVertices(bytes,declaration,56,source,true);
    need(alpha.size()==1&&alpha[0].uv==out[0].uv&&alpha[0].uv1==out[0].uv1,
         "Dual UV alpha omitted an original consumed UV1 value");
    auto invalid=declaration;put(invalid,3*12+8,0x00050000);rejected([&]{decodeSkinVertices(bytes,invalid,56,source);});
    rejected([&]{decodeSkinVertices(bytes,declaration,48,source);});
    invalid=bytes;put(invalid,24,0x7FC00000);rejected([&]{decodeSkinVertices(invalid,declaration,56,source);});
    rejected([&]{decodeSkinVertices(invalid,declaration,56,source,true);});
    std::vector<uint8_t> shortBytes(48),shortDeclaration(156);std::copy_n(bytes.begin(),24,shortBytes.begin());
    std::copy(bytes.begin()+32,bytes.end(),shortBytes.begin()+24);size_t shortAt=0;
    for(size_t row=0;row<14;++row){if(row==3)continue;for(size_t lane=0;lane<3;++lane){
        auto value=word(declaration,12*row+4*lane);if(!lane&&(row==4||row==5||row==6))value-=8;
        put(shortDeclaration,shortAt+4*lane,value);}shortAt+=12;}
    rejected([&]{decodeSkinVertices(shortBytes,shortDeclaration,48,source,false);});
    rejected([&]{decodeSkinVertices(shortBytes,shortDeclaration,48,source,true);});
    invalid=bytes;put(invalid,32,64);rejected([&]{decodeSkinVertices(invalid,declaration,56,source);});
    need(bytes==saved&&declaration==declSaved,"Dual UV decoder changed immutable inputs");
}
}
int main(int argc,char** argv)try{
    using namespace Simpsons;using namespace Simpsons::Graphics;
    need(argc==2,"Original flat image required");std::ifstream f(argv[1],std::ios::binary);need(bool(f),"Original skin dual UV image absent");
    std::vector<uint8_t> image{std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};need(image.size()==15466496,"Original image extent differs");const auto original=image;
    EffectRecord record(source,std::span(image).subspan(0x4A058,0x77B0));const auto body=record.body();
    need(word(body,0x118)==99&&word(body,0x130)==94&&word(body,0x134)==11&&word(body,0x138)==4816,"Original dual UV bank framing differs");
    const auto profile=skinProfile(source);const auto alpha=skinAlphaPass(source);
    need(profile.vertex==0x8204BA4C&&profile.pixel==0x8204E2DC&&profile.context==0x6570&&profile.samplers==12&&profile.words==1204&&
         profile.boneArray==0x00780034&&profile.boneLeaf==26&&profile.boneWord==128&&profile.leaves==94&&profile.stride==56,"Dual UV production profile differs from original");
    need(alpha.vertex==0x8204CE90&&alpha.pixel==0x8204E75C&&alpha.context==0x6D20&&alpha.samplers==12,"Original dual UV alpha profile differs");
    for(uint32_t pass=0;pass<2;++pass){
        const auto technique=pass?0x7FFFCu:0x3FFFCu,context=pass?0x6D20u:0x6570u,map=word(body,context+0x40);
        for(const auto& row:vertexRows){
            const auto binding=effectPassBinding(record,technique,word(body,map+16*row.leaf));
            need(binding.usage==1&&binding.lanes[0]&&!binding.lanes[1]&&binding.lanes[0]->start==row.reg&&binding.lanes[0]->count==1,"Original dual UV selected VS material map differs");
        }
        for(const auto& row:pixelRows){
            const auto binding=effectPassBinding(record,technique,word(body,map+16*row.leaf));
            need(binding.usage==2&&!binding.lanes[0]&&binding.lanes[1]&&binding.lanes[1]->start==row.reg&&binding.lanes[1]->count==1,"Original dual UV selected PS material map differs");
        }
        need(pixelTextureStage(effectPassBinding(record,technique,0x018000B6))==0&&pixelTextureStage(effectPassBinding(record,technique,0x018400B8))==1,"Original dual UV material texture stages differ");
        projection(pass!=0);
    }
    vertices();need(image==original,"Dual UV metadata test changed original image");
    std::printf("PASS original skin dual UV material: %zu checks; original selected VS/PS/stage maps, every dirty leaf and sentinel lane, relocated3-row bones,56-byte UV1; CPU only\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL skin dual UV material: %zu checks %s\n",checks,e.what());return 1;}
