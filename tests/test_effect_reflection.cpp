// Compare the native decoder with independent original-instruction evidence.
#include "renderer/effect_reflection.h"
#include <cstdio>
#include <fstream>
#include <iterator>

namespace {
using namespace Simpsons::Graphics;
size_t checks{};
void need(bool ok,const char* why) {++checks;if(!ok) throw std::runtime_error(why);}
std::vector<uint8_t> read(const char* path) {
    std::ifstream f(path,std::ios::binary);need(bool(f),"Cannot read test input");
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
struct Fixture {
    std::vector<uint8_t> b;size_t at=0;
    uint32_t word() {
        need(at<=b.size() && b.size()-at>=4,"Truncated reflection golden");
        uint32_t out=0;for(unsigned i=0;i<4;++i) out=out<<8|b[at++];return out;
    }
    uint64_t quad() {const auto high=word();return uint64_t(high)<<32|word();}
    std::string text() {
        const auto n=word();need(at<=b.size() && n<=b.size()-at,"Truncated golden string");
        std::string s(b.begin()+at,b.begin()+at+n);at+=n;return s;
    }
};
void binding(Fixture& f,const EffectBinding& b) {
    std::array<uint32_t,6> words{};for(auto& w:words) w=f.word();
    const auto elements=f.word();
    need(b.handle==words[0] && b.usage==words[1],"Parameter handle/usage differs");
    need(b.arrayElements.has_value()==(elements!=UINT32_MAX),"Array classification differs");
    if(b.arrayElements) need(*b.arrayElements==elements,"Array element count differs");
    for(uint32_t lane=0;lane<2;++lane) {
        need(b.lanes[lane].has_value()==bool(b.usage&(lane?0xAA:0x55)),"Inactive binding range represented as data");
        if(b.lanes[lane]) {
            need(b.lanes[lane]->start==words[2+lane],"Active binding start differs");
            need(b.arrayElements.value_or(b.lanes[lane]->count)==words[4+lane],"Active binding count differs");
        }
    }
}
void classification(Fixture& f,const EffectClassification& c) {
    for(auto w:c.words) need(w==f.word(),"Defined classification word differs");
}
}

int main(int argc,char** argv) {
    try {
        need(argc==3,"Original image and independent fixture required");
        auto image=read(argv[1]);Fixture f{read(argv[2])};
        need(image.size()==15466496,"Original image size differs");
        need(f.word()==0x46585246 && f.word()==1 && f.word()==47,"Fixture identity differs");
        const auto original=image;
        constexpr std::string_view pool[]={"g_ViewProjection","g_WorldEyePosition","g_UTransform","g_VTransform",
            "kWorldToViewPortTfmLight","kWorldToViewPortTfmCharLight","kShadowDepthSampler","kShadowCharDepthSampler",
            "kShadowEdgeSampler","kShadowAmt","kIsShadowReceiver"};
        size_t params=0,classes=0,lightArrays=0,passes=0,clears=0;
        for(unsigned row=0;row<47;++row) {
            const auto address=f.word(),bytes=f.word();const bool notSkinned=f.word()!=0;
            const auto cube=f.word(),parameterCount=f.word();
            need(address>=0x82000000 && size_t(address-0x82000000)+bytes<=image.size(),"Golden source extent");
            // The returned reflection must own its outputs after source destruction.
            const EffectReflection reflection=[&] {
                EffectRecord record(address,std::span(image).subspan(address-0x82000000,bytes));
                try {return EffectReflection(record,notSkinned,pool);}
                catch(const std::exception& e) {throw std::runtime_error(std::string(record.identity().name)+": "+e.what());}
            }();
            need(reflection.reflectionCubeIndex==cube,"Reflection-cube local index differs");
            need(reflection.parameters.size()==parameterCount,"Local parameter count differs");
            for(const auto& p:reflection.parameters) {
                need(p.name==f.text(),"Local enumeration order differs");
                binding(f,p.binding);classification(f,p.classification);++params;
            }
            need(reflection.classified.size()==f.word(),"Selected classification count differs");
            for(const auto& c:reflection.classified) {classification(f,c);++classes;}
            need(reflection.lights.size()==f.word(),"Light child count differs");
            lightArrays+=!reflection.lights.empty();
            for(const auto& b:reflection.lights) for(const auto& m:b.members) binding(f,m);
            need(reflection.activeLights==f.word() && reflection.lightFlags==f.word(),"Selected light flags differ");
            need(reflection.passes.size()==f.word(),"Two-pass selection differs");
            for(const auto& p:reflection.passes) {
                need(p.techniqueHandle==f.word() && p.passHandle==f.word(),"Pass identity differs");
                for(auto mask:p.masks) need(mask==f.quad(),"Original pass mask differs");
                need(p.clears.size()==f.word(),"Mutable clear count differs");
                for(const auto& c:p.clears) {
                    need(c.namespaceIndex==f.word() && c.firstLeaf==f.word() && c.leafCount==f.word(),
                         "Mutable clear order or subtree differs");++clears;
                }
                ++passes;
            }
        }
        need(f.at==f.b.size(),"Unconsumed golden data");need(image==original,"Decoder changed original data");
        need(params==1153 && classes==365 && lightArrays==27 && passes==87,"All49 reflection coverage differs");
        std::printf("PASS native FX reflection: %zu checks,47 gateway profiles,1153 parameters,365 classifications,27 light arrays,87 pass records,%zu ordered clears; pure CPU metadata only\n",checks,clears);
        return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL native FX reflection: %s\n",e.what());return 1;}
}
