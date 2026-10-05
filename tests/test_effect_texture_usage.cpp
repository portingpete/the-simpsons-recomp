// Selected material sampler admission, independently pinned to the original
// context category-7 bitmaps. This is CPU metadata coverage, not a draw claim.
#include "renderer/material_texture_usage.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace {
using namespace Simpsons::Graphics;
size_t checks{};
void need(bool value,const char* why) {++checks;if(!value)throw std::runtime_error(why);}
uint32_t word(std::span<const uint8_t> bytes,size_t at) {
    need(at<=bytes.size()&&bytes.size()-at>=4,"Original sampler evidence exceeds body");
    return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];
}
uint64_t quad(std::span<const uint8_t> bytes,size_t at) {return uint64_t(word(bytes,at))<<32|word(bytes,at+4);}
struct Sampler {const char* name;uint32_t handle,word;int opaque,alpha;};
struct Family {uint32_t source,opaqueContext,alphaContext;std::span<const Sampler> samplers;};
constexpr Sampler rigid[]={{"g_BaseSampler",0x00500020,88,-1,0},{"g_ProjtexSampler",0x00540022,104,-1,-1}};
constexpr Sampler textured[]={{"g_BaseSampler",0x00540022,92,2,0},{"g_ProjtexSampler",0x00580024,108,-1,-1}};
constexpr Sampler dual[]={{"g_BaseSampler",0x00540022,92,2,0},{"g_PaletteSampler",0x00580024,108,-1,-1},
    {"g_ProjtexSampler",0x005C0026,124,-1,-1}};
constexpr Sampler uv[]={{"g_PaletteSampler",0x00700030,120,-1,-1},{"g_BaseSampler",0x00740032,136,1,0},
    {"g_BaseSampler2",0x00780034,152,2,1}};
constexpr Sampler singleUv[]={{"g_BaseSampler",0x006C002E,116,2,0}};
constexpr Sampler flipbook[]={{"g_BaseSampler",0x005C0026,100,0,0}};
constexpr Sampler gloss[]={{"g_BaseSampler",0x00540022,92,2,0},{"g_PaletteSampler",0x00580024,108,-1,-1}};
constexpr Sampler multitone[]={{"g_BaseSampler",0x00540022,92,2,0},{"g_PaletteSampler",0x00580024,108,-1,-1},
    {"g_NoiseSampler",0x005C0026,124,3,-1}};
constexpr Sampler normalmap[]={{"g_BaseSampler",0x00540022,92,2,0},{"g_PaletteSampler",0x00580024,108,-1,-1},
    {"g_NormalMapSampler",0x005C0026,124,3,-1}};
constexpr Sampler sky[]={{"g_BaseSampler",0x00600028,104,0,0},{"g_AlphaSampler1",0x0064002A,120,1,1},
    {"g_AlphaSampler2",0x0068002C,136,2,2},{"g_PaletteSampler",0x006C002E,152,-1,-1},
    {"g_LineSampler",0x00700030,168,3,3}};
constexpr Sampler chocolate[]={{"g_PaletteSampler",0x00700030,120,-1,-1},{"g_BaseSampler",0x00740032,136,2,0},
    {"g_BaseSampler2",0x00780034,152,3,1},{"g_NormalMapSampler",0x007C0036,168,-1,2}};
constexpr Sampler vfx[]={{"g_BaseSampler",0x00500020,88,0,-1}};
constexpr Sampler skin[]={{"g_Sampler",0x015C00A4,1120,-1,0}};
constexpr Sampler skinTextured[]={{"g_Sampler",0x016000A6,1124,1,0}};
constexpr Sampler skinDual[]={{"g_Sampler",0x016000A6,1124,1,0},{"g_PaletteSampler",0x016400A8,1140,-1,-1},
    {"g_ProjtexSampler",0x016800AA,1156,-1,-1}};
const std::array<Family,15> families{{
    {0x8200CCB8,0x2620,0x2910,rigid},{0x820168F8,0x27B0,0x2AB0,textured},
    {0x8202AD78,0x2920,0x2C30,dual},{0x820465E8,0x30F0,0x3440,uv},
    {0x82042F58,0x2D90,0x30A0,singleUv},
    {0x82039208,0x2560,0x2840,flipbook},
    {0x82019988,0x2B00,0x2DF0,gloss},{0x820547E8,0x2D40,0x3040,multitone},
    {0x82057E08,0x3140,0x3450,normalmap},{0x82036448,0x2480,0x27B0,sky},
    {0x8205D2D8,0x3780,0x3AD0,chocolate},{0x8205B848,0x1510,0,vfx},
    {0x82006348,0x5830,0x5F60,skin},{0x8201CD48,0x5F50,0x66C0,skinDual},
    {0x8200FB98,0x5C20,0x6350,skinTextured}
}};
template<class F> void rejected(F&& f) {
    try {f();}catch(const std::invalid_argument&) {++checks;return;}
    need(false,"Malformed selected material texture map accepted");
}
}

int main(int argc,char** argv)try {
    need(argc==2,"Original flat image required");std::ifstream file(argv[1],std::ios::binary);
    need(bool(file),"Cannot read original sampler image");
    std::vector<uint8_t> image{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    need(image.size()==15466496,"Original image size differs");const auto original=image;
    size_t rows=0,used=0,unused=0;
    for(const auto& family:families) {
        const auto ids=originalEffectIdentities();
        const auto id=std::find_if(ids.begin(),ids.end(),[&](const auto& i){return i.originalAddress==family.source;});
        need(id!=ids.end(),"Original sampler family identity absent");
        EffectRecord record(family.source,std::span(image).subspan(family.source-0x82000000,id->recordBytes));
        const auto body=record.body();size_t sampledLeaves=0;
        for(const auto& parameter:record.parameters(false))
            sampledLeaves+=!(parameter.descriptorWords[0]&3)&&((parameter.descriptorWords[0]>>2)&3)==3;
        need(sampledLeaves==family.samplers.size(),"Sampler matrix omitted an original private texture leaf");
        for(uint32_t pass=0;pass<2;++pass) {
            const auto context=pass?family.alphaContext:family.opaqueContext;if(!context)continue;
            const auto technique=pass?0x0007FFFCu:0x0003FFFCu;
            const auto selected=std::find_if(record.techniques().begin(),record.techniques().end(),
                [&](const auto& t){return t.handle==technique;});
            need(selected!=record.techniques().end()&&selected->contextOffset==context,"Original selected texture context differs");
            for(const auto& expected:family.samplers) {
                const auto parameter=std::find_if(record.parameters(false).begin(),record.parameters(false).end(),
                    [&](const auto& p){return p.name==expected.name;});
                need(parameter!=record.parameters(false).end()&&parameter->handle==expected.handle&&
                    4*(parameter->descriptorWords[1]&0xFFFF)==expected.word,"Original sampler handle/storage differs");
                const auto leaf=(expected.handle>>1)&0x1FFFF;uint32_t usage=0;
                // Original82C1E4F4/E508 selects only category7 AND private
                // dirtiness. Decode all categories independently of bindings.
                for(uint32_t category=0;category<8;++category)
                    usage|=uint32_t((quad(body,word(body,context+4*category)+8*(leaf/64))>>(63-leaf%64))&1)<<category;
                const int stage=pass?expected.alpha:expected.opaque;
                need(usage==(stage<0?0u:0x80u),"Original sampler usage differs from independently pinned selected pass");
                const auto binding=effectPassBinding(record,technique,expected.handle);
                need(binding.usage==usage&&!binding.lanes[0],"Native sampler decoder differs from original category masks");
                const auto admission=pixelTextureStage(binding);
                if(stage<0) {
                    need(!binding.lanes[1]&&!admission,"Unused material texture admitted an owner lookup");++unused;
                } else {
                    const auto map=word(body,context+0x40)+16*leaf;
                    need(word(body,map)==expected.handle&&((word(body,map+8)>>22)&255)==uint32_t(stage),
                        "Original sampler row/stage evidence differs");
                    need(binding.lanes[1]&&binding.lanes[1]->count==1&&binding.lanes[1]->start==uint32_t(stage)&&
                        admission&&*admission==uint32_t(stage),"Selected sampler stage0/range admission differs");++used;
                }
                ++rows;
            }
            if(family.source==0x82006348||family.source==0x8201CD48||family.source==0x8200FB98) {
                constexpr std::array<const char*,3> names{"kShadowDepthSampler","kShadowCharDepthSampler","kShadowEdgeSampler"};
                constexpr std::array<uint32_t,3> handles{0x001C000D,0x0020000F,0x00240011};
                for(uint32_t sampler=0;sampler<3;++sampler) {
                    const auto handle=handles[sampler],leaf=6+sampler;
                    const auto parameters=record.parameters(true);
                    const auto parameter=std::find_if(parameters.begin(),parameters.end(),
                        [&](const auto& p){return p.name==names[sampler];});
                    need(parameter!=parameters.end()&&parameter->handle==handle&&parameter->descriptorWords[0]==0xC,
                         "Original shared skin shadow sampler descriptor differs");
                    uint32_t usage=0;
                    for(uint32_t category=0;category<8;++category)
                        usage|=uint32_t((quad(body,word(body,context+32+4*category)+8*(leaf/64))>>(63-leaf%64))&1)<<category;
                    const bool consumes=(family.source==0x8201CD48||family.source==0x8200FB98)&&!pass&&sampler==1;
                    need(usage==(consumes?0x80u:0u),"Original selected skin shadow usage differs");
                    const auto binding=effectPassBinding(record,technique,handle);
                    need(binding.handle==handle&&binding.usage==usage&&!binding.lanes[0],
                         "Shared skin shadow decoder used another pass context");
                    const auto admission=pixelTextureStage(binding);const auto map=word(body,context+0x44)+16*leaf;
                    if(consumes) {
                        need(word(body,map)==handle&&word(body,map+8)==0&&binding.lanes[1]&&
                            binding.lanes[1]->start==0&&binding.lanes[1]->count==1&&admission&&*admission==0,
                             "Dual opaque original character shadow stage0 differs");++used;
                    } else {
                        for(uint32_t lane=0;lane<4;++lane)
                            need(!word(body,map+4*lane),"Unused original skin shadow row retains a map");
                        need(!binding.lanes[1]&&!admission,"Unused selected skin shadow required a texture owner");++unused;
                    }
                    ++rows;
                }
            }
        }
    }
    // Inactive and malformed binding lanes must not be mistaken for a stage.
    const EffectBinding absent{0x00500020,0,{},std::nullopt};need(!pixelTextureStage(absent),"No-use map admitted stage0");
    for(uint32_t lane=0;lane<2;++lane) {auto b=absent;b.lanes[lane]=EffectConstantRange{0,1};rejected([&]{pixelTextureStage(b);});}
    EffectBinding active{0x00500020,0x80,{},std::nullopt};active.lanes[1]=EffectConstantRange{0,1};
    need(pixelTextureStage(active)==0,"A real active stage0 was mistaken for unused");
    for(uint32_t mutation=0;mutation<7;++mutation) {
        auto binding=active;
        switch(mutation) {
            case 0:binding.usage=0x40;break;case 1:binding.usage=0x82;break;
            case 2:binding.lanes[0]=EffectConstantRange{0,1};break;case 3:binding.lanes[1].reset();break;
            case 4:binding.lanes[1]->count=0;break;case 5:binding.lanes[1]->count=2;break;
            case 6:binding.lanes[1]->start=16;break;
        }
        rejected([&]{pixelTextureStage(binding);});
    }
    need(rows==87&&used==42&&unused==45,"Complete selected texture matrix coverage differs");
    need(image==original,"Sampler audit changed original image");
    std::printf("PASS original selected material textures: %zu checks,15 families,69 private+18 shared skin sampler/pass rows,42 consumed/45 unused; exact contexts/category masks/stages and invalid-map rejection; CPU metadata only\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL original selected material textures: %s\n",e.what());return 1;}
