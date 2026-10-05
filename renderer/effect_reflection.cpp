#include "effect_reflection.h"
#include <algorithm>
#include <bit>
#include <functional>

namespace Simpsons::Graphics {
namespace {
void need(bool ok,const char* why) {if(!ok) throw EffectError(std::string("reflection: ")+why);}
struct Reader {
    std::span<const uint8_t> bytes;
    std::span<const uint8_t> take(uint64_t at,uint64_t n) const {
        if(at>bytes.size() || n>bytes.size()-at)
            throw EffectError("reflection: metadata bounds at="+std::to_string(at)+" size="+std::to_string(n)+
                              " extent="+std::to_string(bytes.size()));
        return bytes.subspan(size_t(at),size_t(n));
    }
    uint32_t word(uint64_t at) const {
        auto b=take(at,4);return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];
    }
    uint32_t half(uint64_t at) const {auto b=take(at,2);return uint32_t(b[0])<<8|b[1];}
    uint64_t quad(uint64_t at) const {return uint64_t(word(at))<<32|word(at+4);}
    std::string text(uint64_t at,uint64_t end) const {
        need(at<end && end<=bytes.size(),"string range");std::string out;
        for(auto b:take(at,end-at)) {
            if(!b) return out;
            need(b<128,"non-ASCII classification text");out+=char(b);
        }
        throw EffectError("reflection: unterminated string");
    }
    std::string text(uint64_t at) const {return text(at,bytes.size());}
};
bool equalFold(std::string_view a,std::string_view b) {
    if(a.size()!=b.size()) return false;
    auto fold=[](char c){return c>='A' && c<='Z'?char(c+32):c;};
    for(size_t i=0;i<a.size();++i) if(fold(a[i])!=fold(b[i])) return false;
    return true;
}
constexpr std::string_view scopes[]={"ENGINE","PER_MATERIAL","PER_MATERIAL_ENGINE","ENGINE_ONCE_PER_FRAME","INVALID_SCOPE"};
constexpr std::string_view types[]={
    "WORLD_MATRIX","VIEW_PROJ_MATRIX","WORLD_EYE_POS","WORLD_INVERSE_MATRIX","AMBIENT_COLOR",
    "REFLECTION_CUBEMAP","TINT_COLOR","LIGHTING","DELTA_TIME","LIGHTMAP_FLAG","BRUISE_MAP_BLEND",
    "VERTEX_COLOR_OVERBRIGHT","SKY_CLOUD_INTENSITY","LIGHTSCATTER_CONST1","LIGHTSCATTER_CONST2",
    "LIGHTSCATTER_CONST3","LIGHTSCATTER_CONST4","LIGHTSCATTER_CONST5","LIGHTSCATTER_CONST6",
    "LIGHTSCATTER_CONST7","U_TRANSFORM","V_TRANSFORM","LIGHTMAP_LUMINANCE_SAMPLER","LIGHTMAP_COLOR_SAMPLER",
    "LIGHTMAP_SEP_COLOR_LUMINANCE","APPLY_EXTERIOR_LIGHTING","FOG_COLOR","FOG_PARAMS","SHADOW_TFM_LIGHT",
    "SHADOW_TFM_CHAR_LIGHT","SHADOW_AMOUNTS","SHADOW_RECEIVER_FLAG","TINT_COLOR_INDEX","OBJECT_ID",
    "BLENDSHAPES_FLAG","BLENDING_WEIGHTS","PLAYER_STATUS","RIM_HIGHLIGHT","RIM_HIGHLIGHTVALS",
    "ENV_SHADOW_MAP","CHAR_SHADOW_MAP","INVALID_ENGINE_TYPE"};
constexpr std::string_view defaults[]={"Invalid Texture","Default Albedo Map","Default Normal Map","Default Gloss Map",
    "Default Reflection Map","Default Depth Map","Black Map","White Map","Default Light Map","Default Base Map","Default Cube Map"};
uint32_t lookup(std::string_view value,std::span<const std::string_view> table,uint32_t fallback) {
    for(uint32_t i=0;i<table.size();++i) if(equalFold(value,table[i])) return i;
    return fallback;
}
struct Annotation {std::string name,text;uint32_t valueClass,word;};
struct Descriptor {
    uint32_t handle,index,kind,span,elements,valueClass,columns,firstAnnotation,annotationCount;
    std::string name;
};
class Decoder {
    Reader r;
    std::vector<Annotation> annotations;
    std::array<std::vector<Descriptor>,2> descriptors;
public:
    explicit Decoder(const EffectRecord& source):r{source.body()} {
        need(r.word(0x268)==0,"unqualified effect-level annotations");
        const uint32_t count=r.word(0x264),at=r.word(0x260),names=r.word(0x2A0);
        const uint32_t storage=r.word(0x26C),size=r.word(0x270);
        need(count<=0x3FFF,"annotation capacity");if(count) r.take(storage,size);
        for(uint32_t i=0;i<count;++i) {
            const uint32_t w=r.word(at+8*i),d=r.word(at+8*i+4),cls=(w>>2)&3;
            need(!(w&3) && cls>=1 && !(w&(3u<<7)) && !(w&(3u<<9)),"annotation shape");
            const uint64_t off=uint64_t(storage)+4*(d&0xFFFF);
            Annotation a{r.text(r.word(names+4*i)),{},cls,0};
            if(cls==3) a.text=r.text(off,uint64_t(storage)+size);
            else {
                need(off>=storage && off+4<=uint64_t(storage)+size,"annotation word extent");
                a.word=r.word(off);
                if(cls==2) need(a.name=="expand" && w==8 && d>>16==1 && a.word==1,
                               "unqualified unqueried scalar annotation");
            }
            annotations.push_back(std::move(a));
        }
        for(uint32_t ns=0;ns<2;++ns) {
            const uint32_t atDesc=ns?r.word(r.word(0x10C)):r.word(0x108);
            const uint32_t atNames=ns?r.word(r.word(0x29C)):r.word(0x298);
            const uint32_t slots=r.word(ns?0x11C:0x118);
            need(slots<=0x3FFF,"descriptor capacity");
            descriptors[ns].resize(slots);
            uint32_t leaf=0;
            for(uint32_t i=1;i<slots;++i) {
                const uint32_t w=r.word(atDesc+8*i),d=r.word(atDesc+8*i+4),kind=w&3;
                need(kind<=2,"descriptor kind");
                const uint32_t span=kind?d&0xFFFF:1;
                need(span && span<=slots-i,"descriptor subtree extent");
                const uint32_t annCount=count?(ns?r.half(r.word(0x284)+2*i):w>>21):0;
                const uint32_t first=annCount?r.half(r.word(ns?0x280:0x27C)+2*i):0;
                need(first<=count && annCount<=count-first,"parameter annotation extent");
                descriptors[ns][i]={i<<18|leaf<<1|ns,i,kind,span,kind?(w>>2)&0x3FFF:0,
                    kind?5:(w>>2)&3,kind?1:1+((w>>9)&3),first,annCount,r.text(r.word(atNames+4*i))};
                if(!kind) ++leaf;
            }
            need(leaf==r.word(ns?0x134:0x130),"descriptor leaf count");
        }
    }
    const Descriptor& desc(uint32_t handle) const {
        const auto& list=descriptors[handle&1];const uint32_t index=handle>>18;
        need(index && index<list.size() && list[index].handle==handle,"descriptor handle");return list[index];
    }
    std::vector<uint32_t> children(uint32_t handle) const {
        const auto& d=desc(handle);need(d.kind && d.elements<d.span,"child layout");
        const auto& list=descriptors[handle&1];uint32_t cursor=d.index+1;std::vector<uint32_t> out;
        for(uint32_t i=0;i<d.elements;++i) {
            need(cursor<d.index+d.span,"child count exceeds subtree");
            const auto& c=list[cursor];out.push_back(c.handle);cursor+=c.span;
        }
        need(cursor==d.index+d.span,"child stride mismatch");return out;
    }
    EffectMutableClear clear(uint32_t handle) const {
        const auto& d=desc(handle);uint32_t leaves=0;
        for(uint32_t i=d.index;i<d.index+d.span;++i) leaves+=descriptors[handle&1][i].kind==0;
        return {handle&1,(handle>>1)&0x1FFFF,leaves};
    }
    EffectBinding binding(uint32_t context,uint32_t handle) const {
        const auto& d=desc(handle);const uint32_t leaf=(handle>>1)&0x1FFFF,ns=handle&1;
        uint32_t usage=0;
        for(uint32_t i=0;i<8;++i) usage|=uint32_t((r.quad(r.word(context+32*ns+4*i)+8*(leaf/64))>>(63-leaf%64))&1)<<i;
        EffectBinding out{handle,usage,{},d.kind==2?std::optional(d.elements):std::nullopt};
        constexpr uint32_t tables[6][8]={
            {1,2,1,2,3,3,1,2},{0x3FF,0x3FF,0xFF000,0xFF000,0xFF,0xFF00,0x3FC00000,0x3FC00000},
            {0,0,12,12,0,8,22,22},{1,2,1,2,1,2,1,1},
            {0xC00,0xC00,0x300000,0x300000,0xC0000000,0xC0000000,0,0},{10,10,20,20,30,30,0,0}};
        for(uint32_t lane=0;lane<2;++lane) {
            const uint32_t subset=usage&(lane?0xAA:0x55);
            if(!subset) continue;
            const unsigned k=31u-unsigned(std::countl_zero(subset));
            const uint64_t at=uint64_t(r.word(context+0x40+4*ns))+16*leaf;
            out.lanes[lane]=EffectConstantRange{
                (r.word(at+4*tables[0][k])&tables[1][k])>>tables[2][k],
                ((r.word(at+4*tables[3][k])&tables[4][k])>>tables[5][k])+1};
        }
        return out;
    }
    EffectClassification classification(uint32_t handle,uint32_t localIndex) const {
        const auto& d=desc(handle);
        const uint32_t shape=d.valueClass==3?1:(d.valueClass<=2 && d.columns>=1 && d.columns<=4?
                                            2+4*d.valueClass+d.columns-1:0);
        EffectClassification out{{4,41,shape,1,localIndex,0}};bool defaultFound=false;
        for(uint32_t i=0;i<d.annotationCount;++i) {
            const auto& a=annotations[d.firstAnnotation+i];
            for(uint32_t f=0;f<2;++f) if(equalFold(a.name,f?"paramScopeType":"paramScope")) {
                need(a.valueClass==1 || a.valueClass==3,"unqualified scope value class");
                out.words[f]=a.valueClass==1?a.word:lookup(a.text,f?std::span<const std::string_view>(types):std::span<const std::string_view>(scopes),f?41:4);
            }
            if(!defaultFound && equalFold(a.name,"defaultname")) {
                need(a.valueClass==3,"unqualified defaultname value class");
                out.words[3]=lookup(a.text,defaults,0);defaultFound=true;
            }
        }
        return out;
    }
    std::vector<uint32_t> roots() const {
        const uint32_t count=r.word(0x2AC),at=r.word(0x2A8);
        need(count==r.word(0x110)+r.word(0x114) && count<=64,"local named capacity");
        std::vector<uint32_t> out;
        for(uint32_t i=0;i<count;++i) {const auto h=r.word(at+4*i);desc(h);out.push_back(h);}
        auto unique=out;std::sort(unique.begin(),unique.end());
        need(std::adjacent_find(unique.begin(),unique.end())==unique.end(),"repeated root");return out;
    }
    EffectReflectedPass pass(const EffectTechnique& t,const std::vector<EffectReflectedParameter>& params,
                             bool notSkinned,std::span<const std::string_view> poolNames) const {
        EffectReflectedPass out{t.handle,t.passHandle,{}, {}};
        std::array<std::array<bool,256>,2> occupied{};
        auto rangeMask=[](uint32_t start,uint32_t count) {
            // Exact64-bit PPC srad/srd construction, including out-of-range shifts.
            const uint32_t first=(start&~3u)/4,last=(((start+count+3)&~3u)-1)/4;
            const uint32_t width=std::min((last-first)&127u,63u),shift=first&127u;
            const uint64_t spread=UINT64_MAX<<(63-width);
            return shift>=64?uint64_t(0):spread>>shift;
        };
        std::function<void(uint32_t,bool)> walk=[&](uint32_t handle,bool output) {
            const auto b=binding(t.contextOffset,handle);if(!b.usage) return;
            if(desc(handle).kind) {
                out.clears.push_back(clear(handle));for(auto c:children(handle)) walk(c,output);return;
            }
            for(uint32_t lane=0;lane<2;++lane) if(b.lanes[lane]) {
                const auto range=*b.lanes[lane];
                if(output) {out.masks[lane]|=rangeMask(range.start,range.count);out.clears.push_back(clear(handle));}
                else {
                    const uint32_t begin=range.start&~3u,end=(range.start+range.count+3)&~3u;
                    need(end<=256 && begin<=end,"occupancy outside256 components");
                    for(uint32_t i=begin;i<end;++i) occupied[lane][i]=true;
                }
            }
        };
        for(uint32_t scope:{0u,2u}) {
            uint32_t count=0;
            for(const auto& p:params) if(p.binding.usage && p.classification.words[0]==scope && count<64) {
                walk(p.binding.handle,true);++count;
            }
        }
        for(const auto& p:params) {
            if(p.classification.words[0]!=2 && std::find(poolNames.begin(),poolNames.end(),p.name)!=poolNames.end())
                walk(p.binding.handle,true);
            if(notSkinned) walk(p.binding.handle,false);
        }
        if(notSkinned) for(uint32_t lane=0;lane<2;++lane) {
            bool active=false;uint32_t first=0,length=0;
            for(uint32_t i=0;i<252;++i) {
                if(active) {
                    if(!occupied[lane][i]) ++length;
                    if(occupied[lane][i]) {
                        length&=~3u;active=false;if(length) out.masks[lane]|=rangeMask(first,length);
                    }
                } else if(!occupied[lane][i] && i%4==0) {active=true;first=i;length=1;}
            }
            // The original scan has no trailing-run flush.
        }
        return out;
    }
};
}

EffectBinding effectPassBinding(const EffectRecord& record,uint32_t technique,uint32_t handle) {
    const auto passes=record.techniques();
    const auto pass=std::find_if(passes.begin(),passes.end(),[&](const auto& p){return p.handle==technique;});
    need(pass!=passes.end(),"unknown selected technique");
    return Decoder(record).binding(pass->contextOffset,handle);
}

EffectReflection::EffectReflection(const EffectRecord& record,bool notSkinned,
                                   std::span<const std::string_view> poolNames) {
    Decoder d(record);const auto roots=d.roots();
    need(!record.techniques().empty(),"no technique0/pass0");
    const uint32_t context=record.techniques()[0].contextOffset;
    for(uint32_t i=0;i<roots.size();++i) {
        const auto h=roots[i];auto b=d.binding(context,h);auto c=d.classification(h,i);
        parameters.push_back({d.desc(h).name,std::move(b),c});
        if(parameters.back().binding.usage && c.words[0]==0 && classified.size()<24) classified.push_back(c);
    }
    for(const auto& c:classified) {
        if(c.words[1]==5) reflectionCubeIndex=c.words[4];
        if(c.words[1]!=7) continue;
        const uint32_t h=parameters[c.words[4]].binding.handle;
        const auto& parent=d.desc(h);need(parent.kind==2 && (parent.elements==1 || parent.elements==4),"light array profile");
        lights.clear();
        constexpr std::string_view members[]={"type","position","color","direction","property"};
        for(auto child:d.children(h)) {
            EffectLightBlock block{};const auto fields=d.children(child);need(fields.size()==5,"light member count");
            for(uint32_t k=0;k<5;++k) {
                const auto found=std::find_if(fields.begin(),fields.end(),[&](uint32_t f){return d.desc(f).name==members[k];});
                need(found!=fields.end(),"light member identity");block.members[k]=d.binding(context,*found);
            }
            lights.push_back(std::move(block));
        }
        activeLights=0;
        for(const auto& b:lights) {
            auto used=[&](uint32_t mask){return std::any_of(b.members.begin(),b.members.end(),[&](const auto& m){return bool(m.usage&mask);});};
            if(used(0x55)) {++activeLights;lightFlags|=2;} else if(used(0xAA)) {++activeLights;lightFlags|=1;}
        }
    }
    for(const auto& t:record.techniques()) {
        if(passes.size()==2) break;
        passes.push_back(d.pass(t,parameters,notSkinned,poolNames));
    }
}
}
