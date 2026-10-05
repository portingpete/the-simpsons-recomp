// Original textured-skin metadata, dirty projection and consumed vertex layout.
// CPU-only coverage: no original scene traversal or native draw is claimed.
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
size_t checks{};
void need(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}
uint32_t word(std::span<const uint8_t> b,size_t at){
    need(at<=b.size()&&b.size()-at>=4,"Original textured skin evidence exceeds body");
    return uint32_t(b[at])<<24|uint32_t(b[at+1])<<16|uint32_t(b[at+2])<<8|b[at+3];
}
uint64_t quad(std::span<const uint8_t> b,size_t at){return uint64_t(word(b,at))<<32|word(b,at+4);}
void put(std::span<uint8_t> b,size_t at,uint32_t value){for(uint32_t i=0;i<4;++i)b[at+i]=uint8_t(value>>(24-8*i));}
template<class F>void rejected(F f){try{f();}catch(const std::exception&){++checks;return;}need(false,"Malformed textured skin accepted");}
void baseSharedMaterial(std::span<const uint8_t> image){
    // The base opaque morph flag/weights vector genuinely has BOTH stage
    // maps. A pixel-only map assumption rejected this shipped legacy pass.
    using namespace Simpsons::Graphics;
    constexpr uint32_t source=0x82006348,handle=0x00500020;
    EffectRecord record(source,image.subspan(0x6348,0x6970));const auto body=record.body();
    const auto descriptors=word(body,0x108);
    need((word(body,descriptors+8*(handle>>18)+4)&0xFFFF)*4==88,
         "Original base shared-stage private word offset differs");
    for(uint32_t pass=0;pass<2;++pass){
        const uint32_t context=pass?0x5F60:0x5830,technique=pass?0x7FFFC:0x3FFFC,map=word(body,context+0x40);
        need(map==(pass?0x6080u:0x5950u)&&word(body,map+16*16)==handle&&
             word(body,map+16*16+4)==39&&word(body,map+16*16+8)==(pass?0u:39u)&&!word(body,map+16*16+12),
             "Original base selected morph material raw map differs");
        uint32_t usage=0;for(uint32_t cat=0;cat<8;++cat)
            usage|=uint32_t((quad(body,word(body,context+4*cat))>>(63-16))&1)<<cat;
        need(usage==(pass?1u:3u),"Original base morph vector lost its selected stage categories");
        const auto binding=effectPassBinding(record,technique,handle);
        need(binding.usage==usage&&binding.lanes[0]&&binding.lanes[0]->start==39&&binding.lanes[0]->count==1&&
             (pass?!binding.lanes[1]:binding.lanes[1]&&binding.lanes[1]->start==39&&binding.lanes[1]->count==1),
             "Original base morph vector reflection lost its VS/PS association");
        const auto rows=skinPixelMaterialRows(source,pass!=0);
        const auto pixelRow=std::find_if(rows.begin(),rows.end(),[](const auto& row){return row.leaf==16;});
        need(pass?pixelRow==rows.end():pixelRow!=rows.end()&&pixelRow->word==88&&pixelRow->reg==39,
             "Base selected material dropped a consumed PS vector or projected the alpha-unused row");
        std::vector<uint32_t> words(1148);words[88]=0x80000000;words[89]=1;words[90]=0x3E800000;words[91]=0x3F800000;
        SkinPixelConstants pixels{};SkinVertexConstants vertices{};
        for(size_t r=0;r<pixels.size();++r)for(size_t lane=0;lane<4;++lane)pixels[r][lane]=float(1000+4*r+lane);
        for(size_t r=0;r<vertices.size();++r)for(size_t lane=0;lane<4;++lane)vertices[r][lane]=float(2000+4*r+lane);
        const auto initialPS=pixels;const auto originalStaging=vertices;std::array<uint8_t,128> dirty{};dirty[2]=0x80;
        projectSkinMaterial(words,dirty,pixels,source,pass!=0);
        projectSkinVertexMaterial(words,dirty,vertices,source,pass!=0);
        for(size_t r=0;r<pixels.size();++r)for(size_t lane=0;lane<4;++lane)
            need(std::bit_cast<uint32_t>(pixels[r][lane])==(!pass&&r==39?words[88+lane]:std::bit_cast<uint32_t>(initialPS[r][lane])),
                 "Base dirty shared-stage vector changed an unconsumed/clean PS lane");
        need(vertices==originalStaging,"Base managed morph vector overwrote the genuine original VS staging");
    }
    // Original executable reads: VS morph enable w and coefficients x/y;
    // opaque PS slot5 reads the same c39.w. Keep map and consumption proof.
    constexpr std::array<std::array<uint32_t,4>,4> instructions{{
        {0x82008BD8,0xC8010000,0x001BC600,0x0527FF00},
        {0x82008C20,0xC8070000,0x00C06CC0,0xAB062700},
        {0x82008C2C,0xC8070005,0x00C0B1C0,0xAB072700},
        {0x8200A304,0x34180303,0x001B1B6C,0x2627FD03}}};
    for(const auto& instruction:instructions)for(size_t lane=0;lane<3;++lane)
        need(word(image,instruction[0]-0x82000000+4*lane)==instruction[lane+1],
             "Original base c39 executable consumption changed");
}
void projection(bool alpha){
    constexpr std::array<SkinMaterialRow,4> opaque{{{14,80,49},{16,88,48},{84,1140,47},{85,1144,46}}};
    constexpr std::array<SkinMaterialRow,2> transparent{{{14,80,49},{16,88,48}}};
    const auto expected=alpha?std::span<const SkinMaterialRow>(transparent):std::span<const SkinMaterialRow>(opaque);
    const auto rows=skinPixelMaterialRows(0x8200FB98,alpha);need(rows.size()==expected.size(),"Textured skin selected material extent differs");
    std::vector<uint32_t> words(1148);for(uint32_t i=0;i<words.size();++i)words[i]=std::bit_cast<uint32_t>(float(i)*.03125f-7);
    words[81]=0x80000000;words[89]=1;
    Graphics::SkinPixelConstants pixels{};for(uint32_t r=0;r<pixels.size();++r)for(uint32_t l=0;l<4;++l)pixels[r][l]=float(1000+4*r+l);
    const auto initial=pixels;std::array<uint8_t,128> dirty{};
    for(size_t i=0;i<expected.size();++i){
        const auto row=expected[i];need(rows[i].leaf==row.leaf&&rows[i].word==row.word&&rows[i].reg==row.reg,"Textured skin material table differs");
        dirty.fill(0);dirty[row.leaf/8]=uint8_t(0x80>>(row.leaf&7));const auto before=pixels;
        projectSkinMaterial(words,dirty,pixels,0x8200FB98,alpha);
        for(uint32_t r=0;r<pixels.size();++r)for(uint32_t l=0;l<4;++l)
            need(std::bit_cast<uint32_t>(pixels[r][l])==(r==row.reg?words[row.word+l]:std::bit_cast<uint32_t>(before[r][l])),
                 "Partial textured skin projection changed unmapped or clean lanes");
    }
    const auto committed=pixels;dirty.fill(0xFF);projectSkinMaterial(words,dirty,pixels,0x8200FB98,alpha);
    need(pixels==committed,"Full material dirty mask projected an unrelated skin row");
    dirty.fill(0);projectSkinMaterial(words,dirty,pixels,0x8200FB98,alpha);need(pixels==committed,"Clean textured skin recommitted material");
    need(pixels[40]==initial[40]&&pixels[39]==initial[39],"Inherited object or base-skin c39 was overwritten");
    if(alpha)need(pixels[46]==initial[46]&&pixels[47]==initial[47],"Alpha projected unused opaque shadow/rim rows");
    rejected([&]{projectSkinMaterial(std::span<const uint32_t>(words).first(1147),dirty,pixels,0x8200FB98,alpha);});
    need(pixels==committed,"Rejected short material bank changed retained constants");
    Graphics::SkinVertexConstants bones{};for(uint32_t r=0;r<bones.size();++r)for(uint32_t l=0;l<4;++l)bones[r][l]=float(2000+4*r+l);
    const auto before=bones;dirty.fill(0);dirty[19/8]|=uint8_t(0x80>>(19&7));dirty[82/8]|=uint8_t(0x80>>(82&7));
    projectSkinBoneMatrices(words,dirty,bones,0x8200FB98);
    for(uint32_t r=0;r<bones.size();++r)for(uint32_t l=0;l<4;++l){
        const bool first=r>=52&&r<55,last=r>=241&&r<244;
        const auto expectedBits=first?words[100+4*(r-52)+l]:last?words[100+16*63+4*(r-241)+l]:std::bit_cast<uint32_t>(before[r][l]);
        need(std::bit_cast<uint32_t>(bones[r][l])==expectedBits,"Textured skin bone projection lost original three-vector extent");
    }
    const auto palette=bones;rejected([&]{projectSkinBoneMatrices(std::span<const uint32_t>(words).first(1147),dirty,bones,0x8200FB98);});
    need(bones==palette,"Rejected short bone bank changed palette");
}
void vertices(){
    std::vector<uint8_t> bytes(48),declaration(156);size_t at=0;
    const auto add=[&](uint32_t offset,uint32_t type,uint32_t semantic){put(declaration,at,offset);put(declaration,at+4,type);put(declaration,at+8,semantic);at+=12;};
    add(0,0x002A23B9,0);add(12,0x002A2187,0x00030000);add(16,0x002C23A5,0x00050000);
    add(24,0x001A2286,0x00020000);add(28,0x001A23A6,0x00010000);add(44,0x00182886,0x000A0000);
    for(uint32_t s=1;s<=6;++s)add(s<<16,0x002A23B9,s<<8);add(0x00FF0000,UINT32_MAX,0);
    put(bytes,0,std::bit_cast<uint32_t>(-.5f));put(bytes,4,std::bit_cast<uint32_t>(.75f));put(bytes,8,0x3F000000);
    put(bytes,12,511|(512u<<10)|(1023u<<20));put(bytes,16,0x3E800000);put(bytes,20,0x3F000000);
    put(bytes,24,0x00000009);put(bytes,28,0x3F800000);put(bytes,44,0x80402010);
    const auto saved=bytes,declSaved=declaration;const auto result=decodeSkinVertices(bytes,declaration,48,0x8200FB98);
    const auto baseline=decodeSkinVertices(bytes,declaration,48,0x82006348);
    need(result.size()==1&&result[0].position==std::array<float,3>{-.5f,.75f,.5f},"Textured skin position differs");
    need(result[0].normal==std::array<float,3>{1,-1,-1.0f/511}&&result[0].uv==std::array<float,2>{.25f,.5f},"Textured skin normal/UV format differs");
    need(result[0].indices==std::array<float,4>{9,0,0,0}&&result[0].weights==std::array<float,4>{1,0,0,0},"Textured skin original influence byte order differs");
    need(result[0].color==std::array<float,4>{64.0f/255,32.0f/255,16.0f/255,128.0f/255}&&result[0].uv1==std::array<float,2>{},"Textured skin consumed color/UV count differs");
    need(!std::memcmp(result.data(),baseline.data(),sizeof(result[0])),"Single-UV skin decoding changed across source profiles");
    auto bad=declaration;put(bad,8,0x00050100);rejected([&]{decodeSkinVertices(bytes,bad,48,0x8200FB98);});
    const auto padded=decodeSkinVertices(bytes,declaration,56,0x8200FB98);
    need(padded.size()==1&&!std::memcmp(padded.data(),result.data(),sizeof(result[0])),
         "Original stride56 rejected a complete first fetched record or changed its attributes");
    rejected([&]{decodeSkinVertices(bytes,declaration,57,0x8200FB98);});
    rejected([&]{decodeSkinVertices(std::span<const uint8_t>(bytes).first(47),declaration,56,0x8200FB98);});
    auto invalid=bytes;put(invalid,24,64);rejected([&]{decodeSkinVertices(invalid,declaration,48,0x8200FB98);});
    invalid=bytes;put(invalid,16,0x7FC00000);rejected([&]{decodeSkinVertices(invalid,declaration,48,0x8200FB98);});
    need(bytes==saved&&declaration==declSaved,"Skin decoder changed source bytes");
}
}
int main(int argc,char** argv)try{
    using namespace Simpsons;using namespace Simpsons::Graphics;
    need(argc==2,"Original flat image required");std::ifstream f(argv[1],std::ios::binary);need(bool(f),"Original textured skin image absent");
    std::vector<uint8_t> image{std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};need(image.size()==15466496,"Original image extent differs");const auto original=image;
    EffectRecord record(0x8200FB98,std::span(image).subspan(0xFB98,0x6D60));const auto body=record.body();
    need(word(body,0x118)==91&&word(body,0x120)==2&&word(body,0x124)==1&&word(body,0x130)==86&&word(body,0x134)==11&&word(body,0x138)==4592,"Original textured skin bank framing differs");
    const auto profile=skinProfile(0x8200FB98);const auto alpha=skinAlphaPass(0x8200FB98);
    need(profile.vertex==0x8201146C&&profile.pixel==0x82013900&&profile.context==0x5C20&&profile.samplers==12&&profile.words==1148&&
         profile.boneArray==0x005C0026&&profile.boneLeaf==19&&profile.boneWord==100&&profile.leaves==86&&profile.stride==48&&!profile.dual&&profile.textured,"Textured skin production profile differs from original evidence");
    need(alpha.vertex==0x820126EC&&alpha.pixel==0x82013F8C&&alpha.context==0x6350&&alpha.samplers==6,"Textured skin alpha profile differs");
    for(uint32_t pass=0;pass<2;++pass){
        const uint32_t context=pass?0x6350:0x5C20,technique=pass?0x7FFFC:0x3FFFC;
        const auto t=std::find_if(record.techniques().begin(),record.techniques().end(),[&](const auto& v){return v.handle==technique;});
        need(t!=record.techniques().end()&&t->name==(pass?"skinalpha":"skin")&&t->contextOffset==context&&
             t->vertexShaderAddress==(pass?0x820126ECu:0x8201146Cu)&&t->pixelShaderAddress==(pass?0x82013F8Cu:0x82013900u)&&
             t->scalars.size()==2&&t->samplers.size()==(pass?6u:12u),"Original selected textured skin pass changed");
        const auto map=word(body,context+0x40);need(map==(pass?0x6470u:0x5D40u)&&word(body,context+0x44)==(pass?0x69D0u:0x62A0u),"Original selected private/shared maps differ");
        for(uint32_t leaf=0;leaf<86;++leaf){
            uint32_t usage=0;for(uint32_t cat=0;cat<8;++cat)usage|=uint32_t((quad(body,word(body,context+4*cat)+8*(leaf/64))>>(63-leaf%64))&1)<<cat;
            const uint32_t expected=leaf==2||leaf==17||leaf==18||(leaf>=19&&leaf<=82)?1u:
                leaf==14||leaf==15||leaf==16||(!pass&&(leaf==84||leaf==85))?2u:leaf==83?0x80u:0u;
            need(usage==expected,"Original selected private usage bitmap differs");
            if(leaf>=19&&leaf<=82){need(word(body,map+16*leaf)==0x00600026+0x40002*(leaf-19)&&word(body,map+16*leaf+4)==0x834+3*(leaf-19)&&!word(body,map+16*leaf+8),"Original textured skin bone register map differs");}
        }
        for(const auto& row:skinPixelMaterialRows(0x8200FB98,pass!=0)){
            need(word(body,map+16*row.leaf+8)==row.reg,"Original selected textured skin material register differs");
            const auto binding=effectPassBinding(record,technique,word(body,map+16*row.leaf));
            need(binding.usage==2&&!binding.lanes[0]&&binding.lanes[1]&&binding.lanes[1]->start==row.reg&&binding.lanes[1]->count==1,"Selected textured skin material reflection differs");
            const auto descriptors=word(body,0x108);need((word(body,descriptors+8*(binding.handle>>18)+4)&0xFFFF)*4==row.word,"Original material storage differs");
        }
        const auto sampler=effectPassBinding(record,technique,0x016000A6);need(pixelTextureStage(sampler)==(pass?0u:1u),"Original selected textured skin base stage differs");
    }
    baseSharedMaterial(image);projection(false);projection(true);vertices();need(image==original,"Textured skin test changed original image");
    std::printf("PASS original textured skin material: %zu checks; base shared-stage c39 original maps/consumption and dirty projection, both textured contexts/all86 private usage rows/three-vector bones/48-byte layout; CPU only\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL original textured skin material: %zu checks %s\n",checks,e.what());return 1;}
