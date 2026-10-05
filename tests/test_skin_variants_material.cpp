// Independent original selected maps, material projection and UV1 geometry.
// Whole original dispatcher/owner coverage lives in test_skin_variants_pass.
#include "renderer/effect_reflection.h"
#include "renderer/material_texture_usage.h"
#include "runtime/skin_material_constants.h"
#include "runtime/skin_vertices.h"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace {
using namespace Simpsons;
size_t checks{};
void need(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
uint32_t word(std::span<const uint8_t> b,size_t at){
    need(at<=b.size()&&b.size()-at>=4,"Original skin variant evidence exceeds body");
    return uint32_t(b[at])<<24|uint32_t(b[at+1])<<16|uint32_t(b[at+2])<<8|b[at+3];
}
uint64_t quad(std::span<const uint8_t> b,size_t at){return uint64_t(word(b,at))<<32|word(b,at+4);}
void put(std::span<uint8_t> b,size_t at,uint32_t value){for(uint32_t i=0;i<4;++i)b[at+i]=uint8_t(value>>(24-8*i));}
template<class F>void rejected(F action){try{action();}catch(const std::exception&){++checks;return;}need(false,"Malformed skin variant accepted");}
struct Source {
    uint32_t address,bytes,words,leaves,descriptors,boneHandle,boneLeaf,boneWord,baseHandle,baseLeaf,baseWord;
    std::array<uint32_t,2> contexts,vertices,pixels;
};
constexpr std::array<Source,2> sources{{
    {0x82023E58,0x6F20,1172,89,94,0x005C0026,19,100,0x016400A8,84,1128,
        {0x5D80,0x64E0},{0x8202579C,0x820269E8},{0x82027C18,0x82028258}},
    {0x8203C008,0x6F50,1168,88,93,0x0064002A,21,108,0x016800AA,85,1132,
        {0x5DD0,0x6520},{0x8203D93C,0x8203ED38},{0x82040118,0x82040510}}
}};
std::span<const SkinMaterialRow> pixelRows(const Source& source,bool alpha){
    static constexpr SkinMaterialRow alphaRows[]={{14,80,49}};
    static constexpr SkinMaterialRow gloss[]={{13,76,50},{14,80,49},{83,1124,47},{86,1160,46},{87,1164,45},{88,1168,44}};
    static constexpr SkinMaterialRow flipbook[]={{14,80,49},{87,1164,46}};
    return alpha?std::span<const SkinMaterialRow>(alphaRows):source.address==0x82023E58?
        std::span<const SkinMaterialRow>(gloss):std::span<const SkinMaterialRow>(flipbook);
}
void projection(const Source& source,bool alpha){
    const auto expected=pixelRows(source,alpha),actual=skinPixelMaterialRows(source.address,alpha);
    need(actual.size()==expected.size(),"Selected skin variant material extent differs");
    std::vector<uint32_t> words(source.words);for(uint32_t i=0;i<words.size();++i)words[i]=std::bit_cast<uint32_t>(float(i)*.03125f-7);
    words[81]=0x80000000;words[83]=1;words[102]=0x80000000;
    Graphics::SkinPixelConstants pixels{};
    for(uint32_t r=0;r<pixels.size();++r)for(uint32_t l=0;l<4;++l)pixels[r][l]=float(1000+4*r+l);
    const auto initial=pixels;std::array<uint8_t,128> dirty{};
    for(size_t i=0;i<expected.size();++i){
        const auto row=expected[i];need(actual[i].leaf==row.leaf&&actual[i].word==row.word&&actual[i].reg==row.reg,"Selected skin variant PS rows differ");
        dirty.fill(0);dirty[row.leaf/8]=uint8_t(0x80>>(row.leaf&7));const auto before=pixels;
        projectSkinMaterial(words,dirty,pixels,source.address,alpha);
        for(uint32_t r=0;r<pixels.size();++r)for(uint32_t l=0;l<4;++l)
            need(std::bit_cast<uint32_t>(pixels[r][l])==(r==row.reg?words[row.word+l]:std::bit_cast<uint32_t>(before[r][l])),
                 "Partial selected skin PS projection changed a clean/unmapped lane");
    }
    const auto committed=pixels;dirty.fill(0xFF);projectSkinMaterial(words,dirty,pixels,source.address,alpha);
    need(pixels==committed&&pixels[40]==initial[40]&&pixels[48]==initial[48],"Skin variant projected managed object/unused alpha-test rows");
    dirty.fill(0);projectSkinMaterial(words,dirty,pixels,source.address,alpha);need(pixels==committed,"Clean selected skin PS rows recommitted");
    rejected([&]{projectSkinMaterial(std::span<const uint32_t>(words).first(source.words-1),dirty,pixels,source.address,alpha);});
    need(pixels==committed,"Rejected skin PS extent changed constants");
    Graphics::SkinVertexConstants vertices{};for(uint32_t r=0;r<vertices.size();++r)for(uint32_t l=0;l<4;++l)vertices[r][l]=float(2000+4*r+l);
    const auto before=vertices;dirty.fill(0);dirty[source.boneLeaf/8]|=uint8_t(0x80>>(source.boneLeaf&7));
    const auto lastLeaf=source.boneLeaf+63;dirty[lastLeaf/8]|=uint8_t(0x80>>(lastLeaf&7));
    projectSkinBoneMatrices(words,dirty,vertices,source.address);
    for(uint32_t r=0;r<vertices.size();++r)for(uint32_t l=0;l<4;++l){
        const auto expectedBits=r>=52&&r<55?words[source.boneWord+4*(r-52)+l]:r>=241&&r<244?
            words[source.boneWord+16*63+4*(r-241)+l]:std::bit_cast<uint32_t>(before[r][l]);
        need(std::bit_cast<uint32_t>(vertices[r][l])==expectedBits,"Skin variant bone projection lost selected three-vector extent");
    }
    const auto bones=vertices;dirty.fill(0xFF);projectSkinVertexMaterial(words,dirty,vertices,source.address,alpha);
    const auto atlas=skinVertexMaterialRows(source.address,alpha);
    need(atlas.size()==(source.address==0x8203C008?1u:0u),"Skin variant selected vertex material extent differs");
    if(!atlas.empty())need(atlas[0].leaf==19&&atlas[0].word==100&&atlas[0].reg==47,"Original skin flipbook atlas row differs");
    for(uint32_t r=0;r<vertices.size();++r)for(uint32_t l=0;l<4;++l)
        need(std::bit_cast<uint32_t>(vertices[r][l])==(source.address==0x8203C008&&r==47?words[100+l]:std::bit_cast<uint32_t>(bones[r][l])),
             "Skin vertex material projection changed TimeTicker, bones or inherited rows");
    const auto full=vertices;dirty.fill(0);projectSkinVertexMaterial(words,dirty,vertices,source.address,alpha);need(vertices==full,"Clean skin vertex material was projected");
    rejected([&]{projectSkinVertexMaterial(std::span<const uint32_t>(words).first(source.words-1),dirty,vertices,source.address,alpha);});
    need(vertices==full,"Rejected skin vertex material extent changed constants");
}
void geometry(const Source& source){
    std::vector<uint8_t> bytes(56),declaration(168);size_t at=0;
    const auto add=[&](uint32_t offset,uint32_t type,uint32_t semantic){put(declaration,at,offset);put(declaration,at+4,type);put(declaration,at+8,semantic);at+=12;};
    add(0,0x002A23B9,0);add(12,0x002A2187,0x00030000);add(16,0x002C23A5,0x00050000);add(24,0x002C23A5,0x00050100);
    add(32,0x001A2286,0x00020000);add(36,0x001A23A6,0x00010000);add(52,0x00182886,0x000A0000);
    for(uint32_t stream=1;stream<=6;++stream)add(stream<<16,0x002A23B9,stream<<8);add(0x00FF0000,UINT32_MAX,0);
    put(bytes,0,std::bit_cast<uint32_t>(-.5f));put(bytes,4,std::bit_cast<uint32_t>(.75f));put(bytes,8,0x3F000000);
    put(bytes,12,511|(512u<<10)|(1023u<<20));put(bytes,16,0x3E800000);put(bytes,20,0x3F000000);
    put(bytes,24,0x3F400000);put(bytes,28,0x3F800000);put(bytes,32,0x0000003F);put(bytes,36,0x3F800000);put(bytes,52,0x80402010);
    const auto saved=bytes,declSaved=declaration;const auto decoded=decodeSkinVertices(bytes,declaration,56,source.address);
    need(decoded.size()==1&&decoded[0].uv==std::array<float,2>{.25f,.5f}&&decoded[0].uv1==std::array<float,2>{.75f,1},"Skin variant lost its original distinct UV1 input");
    need(decoded[0].indices==std::array<float,4>{63,0,0,0}&&decoded[0].weights==std::array<float,4>{1,0,0,0},"Original skin variant influence order differs");
    auto bad=declaration;put(bad,44,0x00050000);rejected([&]{decodeSkinVertices(bytes,bad,56,source.address);});
    rejected([&]{decodeSkinVertices(bytes,declaration,48,source.address);});
    auto invalid=bytes;put(invalid,32,64);rejected([&]{decodeSkinVertices(invalid,declaration,56,source.address);});
    invalid=bytes;put(invalid,24,0x7FC00000);rejected([&]{decodeSkinVertices(invalid,declaration,56,source.address);});
    const auto alphaUnused=decodeSkinVertices(invalid,declaration,56,source.address,true);
    need(alphaUnused.size()==1&&alphaUnused[0].uv==decoded[0].uv&&alphaUnused[0].uv1==std::array<float,2>{},
         "Skin variant alpha consumed its unused authored UV1 value");
    std::vector<uint8_t> alphaBytes(48),alphaDeclaration(156);std::copy_n(bytes.begin(),24,alphaBytes.begin());
    std::copy(bytes.begin()+32,bytes.end(),alphaBytes.begin()+24);size_t alphaAt=0;
    for(size_t row=0;row<14;++row){if(row==3)continue;for(size_t lane=0;lane<3;++lane){
        auto value=word(declaration,12*row+4*lane);if(!lane&&(row==4||row==5||row==6))value-=8;
        put(alphaDeclaration,alphaAt+4*lane,value);}alphaAt+=12;}
    const auto alphaAbsent=decodeSkinVertices(alphaBytes,alphaDeclaration,48,source.address,true);
    need(alphaAbsent.size()==1&&alphaAbsent[0].uv==decoded[0].uv&&alphaAbsent[0].indices==decoded[0].indices&&
         alphaAbsent[0].weights==decoded[0].weights&&alphaAbsent[0].uv1==std::array<float,2>{},
         "Skin variant alpha rejected its original UV0-only layout");
    rejected([&]{decodeSkinVertices(alphaBytes,alphaDeclaration,48,source.address,false);});
    need(bytes==saved&&declaration==declSaved,"Skin variant decoder changed source input");
}
}
int main(int argc,char** argv)try{
    using namespace Simpsons;using namespace Simpsons::Graphics;
    need(argc==2,"Original flat image required");std::ifstream file(argv[1],std::ios::binary);need(bool(file),"Original skin variant image absent");
    std::vector<uint8_t> image{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};need(image.size()==15466496,"Original image extent differs");const auto original=image;
    for(const auto& source:sources){
        EffectRecord record(source.address,std::span(image).subspan(source.address-0x82000000,source.bytes));const auto body=record.body();
        need(word(body,0x118)==source.descriptors&&word(body,0x130)==source.leaves&&word(body,0x138)==source.words*4,"Original skin variant framing differs");
        const auto profile=skinProfile(source.address);const auto alpha=skinAlphaPass(source.address);
        need(isSkinSource(source.address)&&profile.vertex==source.vertices[0]&&profile.pixel==source.pixels[0]&&profile.context==source.contexts[0]&&profile.samplers==6&&profile.words==source.words&&profile.stride==56&&
            profile.boneArray==source.boneHandle&&profile.boneLeaf==source.boneLeaf&&profile.boneWord==source.boneWord&&profile.leaves==source.leaves,"Skin variant profile differs from immutable source");
        need(alpha.vertex==source.vertices[1]&&alpha.pixel==source.pixels[1]&&alpha.context==source.contexts[1]&&alpha.samplers==6,"Skin variant alpha profile differs");
        for(uint32_t pass=0;pass<2;++pass){
            const auto context=source.contexts[pass],technique=pass?0x7FFFCu:0x3FFFCu,map=word(body,context+0x40);
            const auto selected=std::find_if(record.techniques().begin(),record.techniques().end(),[&](const auto& t){return t.handle==technique;});
            need(selected!=record.techniques().end()&&selected->contextOffset==context&&selected->vertexShaderAddress==source.vertices[pass]&&selected->pixelShaderAddress==source.pixels[pass]&&selected->scalars.size()==2&&selected->samplers.size()==6,"Original skin variant selected pass differs");
            for(uint32_t leaf=0;leaf<source.leaves;++leaf){
                uint32_t usage=0;for(uint32_t cat=0;cat<8;++cat)usage|=uint32_t((quad(body,word(body,context+4*cat)+8*(leaf/64))>>(63-leaf%64))&1)<<cat;
                const bool vertex=leaf==2||leaf==17||leaf==18||(leaf>=source.boneLeaf&&leaf<source.boneLeaf+64)||(source.address==0x8203C008&&(leaf==19||leaf==20));
                const bool pixel=leaf==14||leaf==15||(!pass&&((source.address==0x82023E58&&(leaf==13||leaf==83||leaf>=86))||(source.address==0x8203C008&&leaf==87)));
                need(usage==(vertex?1u:pixel?2u:leaf==source.baseLeaf?128u:0u),"Original skin variant selected usage bitmap differs");
                if(leaf>=source.boneLeaf&&leaf<source.boneLeaf+64)need(word(body,map+16*leaf+4)==0x834+3*(leaf-source.boneLeaf),"Original skin variant bone register map differs");
            }
            for(const auto& row:pixelRows(source,pass!=0)){
                const auto binding=effectPassBinding(record,technique,word(body,map+16*row.leaf));
                need(binding.usage==2&&binding.lanes[1]&&binding.lanes[1]->start==row.reg&&binding.lanes[1]->count==1&&!binding.lanes[0],"Original skin variant selected PS map differs");
                need((word(body,word(body,0x108)+8*(binding.handle>>18)+4)&0xFFFF)*4==row.word,"Original skin variant material storage differs");
            }
            const auto base=effectPassBinding(record,technique,source.baseHandle);need(pixelTextureStage(base)==0,"Original skin variant base sampler differs");
            if(source.address==0x8203C008){
                const auto atlas=effectPassBinding(record,technique,0x005C0026),ticker=effectPassBinding(record,technique,0x00600028);
                need(atlas.usage==1&&atlas.lanes[0]&&atlas.lanes[0]->start==47&&ticker.usage==1&&ticker.lanes[0]&&ticker.lanes[0]->start==22,"Original atlas/TimeTicker selected VS maps differ");
            }
            projection(source,pass!=0);
        }
        geometry(source);
    }
    need(image==original,"Skin variant evidence changed original image");
    std::printf("PASS original skin gloss/flipbook material: %zu checks; exact selected maps/dirty lane projection/managed ticker exclusion/bones/56-byte UV1 layout\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL skin variants material: %zu checks %s\n",checks,error.what());return 1;}
