#include "effect_resources.h"
#include "build/generated/effect_catalog.h"
#include <algorithm>
#include <functional>
#include <limits>
#include <set>
#include <utility>

namespace Simpsons::Graphics {
namespace detail {
// Defined with the existing portable SHA implementation in material_resources.cpp.
bool matchesOriginalRecordDigest(std::span<const uint8_t>,std::string_view);
}
namespace {
constexpr uint32_t nullOffset=0xFFFFFFFFu;
constexpr uint32_t maxCount=0x3FFFu;

void bounds(bool ok,const char* message) {
    if(!ok) throw EffectError(std::string("bounds: ")+message);
}
void valid(bool ok,const char* message) {
    if(!ok) throw EffectError(std::string("malformed: ")+message);
}
void supported(bool ok,const char* message) {
    if(!ok) throw EffectError(std::string("unrecognized framing: ")+message);
}
uint64_t align16(uint64_t n) {return (n+15u)&~uint64_t(15u);}
uint32_t narrow(uint64_t n) {
    bounds(n<=std::numeric_limits<uint32_t>::max(),"u32 extent overflow");
    return static_cast<uint32_t>(n);
}

class Reader {
public:
    explicit Reader(std::span<const uint8_t> bytes):bytes_(bytes) {}
    std::span<const uint8_t> take(uint64_t off,uint64_t size,uint32_t alignment=1) const {
        bounds(off<=bytes_.size() && size<=bytes_.size()-off,"serialized span outside effect body");
        bounds(off%alignment==0,"serialized field alignment");
        return bytes_.subspan(static_cast<size_t>(off),static_cast<size_t>(size));
    }
    uint32_t word(uint64_t off,bool packed=false) const {
        const auto p=take(off,4,packed?1u:4u);
        return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|uint32_t(p[3]);
    }
    uint32_t half(uint64_t off) const {
        const auto p=take(off,2,2);
        return uint32_t(p[0])<<8|uint32_t(p[1]);
    }
    std::string string(uint32_t off,uint64_t end) const {
        bounds(off<end && end<=bytes_.size(),"name outside its string area");
        const auto bytes=take(off,std::min<uint64_t>(256,end-off));
        std::string result;
        for(uint8_t c:bytes) {
            if(c==0) {valid(!result.empty(),"empty metadata name");return result;}
            valid(c>=32 && c<127,"non-ASCII metadata name");
            result.push_back(static_cast<char>(c));
        }
        throw EffectError("bounds: unterminated bounded metadata name");
    }
    std::string string(uint32_t off) const {return string(off,bytes_.size());}
    bool zero(uint64_t off,uint64_t size) const {
        const auto bytes=take(off,size);
        return std::all_of(bytes.begin(),bytes.end(),[](uint8_t c){return c==0;});
    }
    void target(uint32_t off) const {if(off!=nullOffset) take(off,1);}
private:
    std::span<const uint8_t> bytes_;
};

struct ArraySpan {
    uint32_t offset,count,bytes;
    uint64_t end() const {return uint64_t(offset)+bytes;}
};
struct ShaderEntry {uint32_t offset,address;};
template<class T> struct StateBlock {uint32_t offset;std::vector<T> values;};
struct Context {uint32_t offset,vertex,pixel;};
struct Pass {
    uint32_t offset,index,context,scalar,sampler;
};
struct Descriptor {
    std::array<uint32_t,2> words;
    uint32_t kind,span,handle,logicalBytes,childCount,storageOffset,rows,columns,reflectionClass;
    std::string name;
};
struct Parsed {
    std::vector<EffectTechnique> techniques;
    std::array<std::vector<EffectParameter>,2> parameters;
    std::array<uint32_t,2> defaultOffsets{},defaultBytes{};
    std::vector<EffectShader> shaders;
    uint32_t cacheBytes{};
};

template<class T> const T& atOffset(const std::vector<T>& items,uint32_t offset,const char* message) {
    const auto found=std::find_if(items.begin(),items.end(),[offset](const auto& item){return item.offset==offset;});
    bounds(found!=items.end(),message);
    return *found;
}

class Parser {
public:
    Parser(uint32_t bodyAddress,std::span<const uint8_t> body,uint32_t prefix)
        :r_(body),bodyAddress_(bodyAddress),prefix_(prefix) {}
    Parsed run() {
        parseShaderArray(0x230,MaterialStage::Vertex,vertexEntries_);
        parseShaderArray(0x23C,MaterialStage::Pixel,pixelEntries_);
        parseStateArrays();
        parseContexts();
        parseTechniques();
        parseParameters(false);
        parseParameters(true);
        checkAuxiliary();
        return std::move(result_);
    }
private:
    ArraySpan array(uint32_t header,uint32_t minimumEntry,uint32_t alignment) const {
        ArraySpan a{r_.word(header),r_.word(header+4),r_.word(header+8)};
        bounds(a.count>0 && a.count<=maxCount && a.count<=a.bytes/minimumEntry,"resource array count");
        r_.take(a.offset,a.bytes,alignment);
        return a;
    }
    void parseShaderArray(uint32_t header,MaterialStage stage,std::vector<ShaderEntry>& entries) {
        const auto a=array(header,8,4);
        uint64_t cursor=a.offset;
        for(uint32_t i=0;i<a.count;++i) {
            bounds(cursor+8<=a.end(),"shader entry header exceeds array");
            const uint32_t handle=r_.word(cursor),size=r_.word(cursor+4);
            supported(handle==0,"serialized shader handle is nonzero");
            bounds(uint64_t(size)<=a.end()-cursor-8,"shader record exceeds resource array");
            const uint32_t recordOffset=narrow(cursor+8);
            uint32_t address=0;
            if(i==0) valid(size==0,"shader sentinel has payload");
            else {
                bounds(size>=36,"truncated shader record");
                const Reader s(r_.take(recordOffset,size,4));
                supported(s.word(0)==(stage==MaterialStage::Vertex?0x102A1101u:0x102A1100u),"shader stage tag");
                const uint32_t hb=s.word(4),pb=s.word(8);
                valid(hb>=36 && hb%4==0 && hb<=size && pb==size-hb,"shader header/payload framing");
                for(uint32_t j=3;j<=6;++j) {
                    const auto off=s.word(j*4);
                    bounds(off==0 || (off>=36 && off<hb && off%4==0),"shader metadata offset");
                }
                supported(s.word(28)==0 && s.word(32)==0,"unknown shader reserved fields");
                const auto codeMetadata=s.word(24);
                bounds(codeMetadata!=0 && uint64_t(codeMetadata)+8<=hb,"shader code metadata extent");
                const auto prefix=s.word(codeMetadata),code=s.word(codeMetadata+4);
                valid(prefix%4==0 && prefix<=pb && code==pb-prefix && code>=24 && code%12==0,"shader prefix/code framing");
                address=narrow(uint64_t(bodyAddress_)+recordOffset);
                const auto& ids=Generated::effectMaterialIdentities;
                const auto id=std::find_if(ids.begin(),ids.end(),[address](const auto& x){return x.originalAddress==address;});
                if(id==ids.end() || id->recordBytes!=size || id->stage!=stage ||
                   !detail::matchesOriginalRecordDigest(r_.take(recordOffset,size),id->sha256))
                    throw EffectError("identity: FX shader record does not match catalog");
                result_.shaders.push_back({address,recordOffset,size,stage});
            }
            entries.push_back({narrow(cursor),address});
            cursor+=8+uint64_t(size);
        }
        valid(cursor==a.end(),"shader array count/trailing bytes mismatch");
    }
    template<class T,class Decode> void parseStates(uint32_t header,uint32_t base,std::vector<StateBlock<T>>& blocks,Decode decode) {
        const auto a=array(header,base+4,16);
        uint64_t cursor=a.offset;
        for(uint32_t i=0;i<a.count;++i) {
            bounds(cursor+base<=a.end(),"state header exceeds array");
            const uint32_t integers=r_.word(cursor+base-12),floats=r_.word(cursor+base-8),literals=r_.word(cursor+base-4);
            const uint64_t count=uint64_t(integers)+floats+literals;
            const uint64_t size=align16(base+4+8*count);
            bounds(size<=a.end()-cursor,"state count/stride exceeds array");
            supported(integers==0 && floats==0,"parameter-driven state categories are not qualified");
            StateBlock<T> block{narrow(cursor),{}};
            if(i==0) valid(r_.zero(cursor,size),"nonzero state sentinel");
            for(uint32_t j=0;j<literals;++j) block.values.push_back(decode(cursor+base+8*uint64_t(j)));
            supported(r_.zero(cursor+base+8*count,size-base-8*count),"unknown state block padding");
            blocks.push_back(std::move(block));
            cursor+=size;
        }
        valid(cursor==a.end(),"state array count/trailing bytes mismatch");
    }
    void parseStateArrays() {
        parseStates(0x248,0x1C,scalars_,[this](uint64_t off) {
            const uint32_t id=r_.word(off);
            supported(id>=0x24 && id<=0x198 && id%4==0,"scalar state SDK ID");
            return EffectScalar{id,r_.word(off+4)};
        });
        parseStates(0x254,0x8C,samplers_,[this](uint64_t off) {
            const uint32_t stage=r_.half(off),id=r_.half(off+2);
            supported(stage<32 && id<=0x7C && id%4==0,"sampler SDK stage/ID");
            return EffectSampler{stage,id,r_.word(off+4)};
        });
    }
    void parseContexts() {
        const auto a=array(0x224,0x58,16);
        valid(a.offset==prefix_,"copy-prefix/context start mismatch");
        const auto privateGroups=r_.word(0x120),sharedGroups=r_.word(0x124);
        const auto privateLeaves=r_.word(0x130),sharedLeaves=r_.word(0x134);
        uint64_t stride=0x58+64*(uint64_t(privateGroups)+sharedGroups);
        for(auto leaves:{privateLeaves,sharedLeaves}) if(leaves) stride=align16(stride)+16*uint64_t(leaves);
        stride=align16(stride);
        bounds(stride<=a.bytes,"context base stride exceeds array");
        uint64_t cursor=a.offset;
        for(uint32_t i=0;i<a.count;++i) {
            bounds(cursor+0x58<=a.end(),"context header exceeds array");
            const auto extension=r_.word(cursor+0x54);
            const uint64_t size=stride+extension;
            bounds(size<=a.end()-cursor,"context stride exceeds array");
            // Original82C16E50 includes +54 in the next-context stride;
            // +50 points to the base end, before the opaque optional tail.
            // Only simpsons_edgeAA has the observed16-byte tail. Its bytes
            // remain owned and SHA-pinned; no application semantics are added.
            valid(r_.word(cursor+0x50)==cursor+stride,"context end link disagrees with base stride");
            supported(extension==0 || extension==16,"unknown context extension");
            uint64_t inner=cursor+0x58;
            for(uint32_t group=0;group<2;++group) {
                const uint64_t width=8*uint64_t(group?sharedGroups:privateGroups);
                for(uint32_t j=0;j<8;++j) {
                    const auto ptr=r_.word(cursor+4*(group*8+j));
                    valid(ptr==inner+j*width,"context bookkeeping link geometry");
                    bounds(uint64_t(ptr)+width<=cursor+stride,"context bookkeeping slice");
                    r_.take(ptr,width);
                }
                inner+=8*width;
            }
            for(uint32_t group=0;group<2;++group) {
                const auto leaves=group?sharedLeaves:privateLeaves;
                const auto ptr=r_.word(cursor+0x40+4*group);
                if(leaves) {
                    inner=cursor+align16(inner-cursor);
                    valid(ptr==inner,"context vector-cache link geometry");
                    bounds(inner+16*uint64_t(leaves)<=cursor+stride,"context vector-cache slice");
                    r_.take(inner,16*uint64_t(leaves));
                    inner+=16*uint64_t(leaves);
                } else valid(ptr==nullOffset,"empty context vector-cache link");
            }
            valid(cursor+align16(inner-cursor)==cursor+stride,"context inner extent");
            const auto& vs=atOffset(vertexEntries_,r_.word(cursor+0x48),"context vertex link not at an entry boundary");
            const auto& ps=atOffset(pixelEntries_,r_.word(cursor+0x4C),"context pixel link not at an entry boundary");
            contexts_.push_back({narrow(cursor),vs.address,ps.address});
            cursor+=size;
        }
        valid(cursor==a.end(),"context count/total byte mismatch");
    }
    void parseTechniques() {
        const uint32_t table=r_.word(0x200),count=r_.word(0x20C),passBase=r_.word(0x210),passCount=r_.word(0x220);
        bounds(count>0 && count<=maxCount && passCount>0 && passCount<=maxCount,"technique/pass count");
        r_.take(table,4*uint64_t(count),4);
        r_.take(passBase,20*uint64_t(passCount),4);
        std::vector<Pass> passes;
        for(uint32_t i=0;i<passCount;++i) {
            const auto off=narrow(uint64_t(passBase)+20*uint64_t(i));
            r_.string(r_.word(off)); // Preserve raw pass names/flags in the owned body.
            const auto context=r_.word(off+8),scalar=r_.word(off+12),sampler=r_.word(off+16);
            atOffset(contexts_,context,"pass context link not at a record boundary");
            atOffset(scalars_,scalar,"pass scalar link not at a block boundary");
            atOffset(samplers_,sampler,"pass sampler link not at a block boundary");
            passes.push_back({off,i,context,scalar,sampler});
        }
        std::set<uint32_t> used;
        std::set<std::string> names;
        uint64_t cacheBytes=24*uint64_t(count);
        for(uint32_t i=0;i<count;++i) {
            const auto off=r_.word(uint64_t(table)+4*uint64_t(i));
            r_.take(off,16,4);
            const auto n=r_.word(uint64_t(off)+4);
            bounds(n>0 && n<=passCount,"technique pass count");
            r_.take(off,16+4*uint64_t(n),4);
            supported(n==1,"multi-pass techniques are outside this admitted corpus API");
            const auto& pass=atOffset(passes,r_.word(uint64_t(off)+16),"technique pass link not at a pass boundary");
            used.insert(pass.offset);
            const auto& ctx=atOffset(contexts_,pass.context,"technique context");
            EffectTechnique t{r_.string(r_.word(off)),(i<<18)|0x3FFFCu,(pass.index<<18)|0x3FFFEu,
                              ctx.offset,ctx.vertex,ctx.pixel,{}, {}};
            supported(names.insert(t.name).second,"duplicate technique name");
            t.scalars=atOffset(scalars_,pass.scalar,"scalar block").values;
            t.samplers=atOffset(samplers_,pass.sampler,"sampler block").values;
            // The runtime consumes each technique's scalar block followed by its
            // sampler block before proceeding to the next technique.
            cacheBytes+=12*uint64_t(t.scalars.size());
            cacheBytes+=16*uint64_t(t.samplers.size());
            result_.techniques.push_back(std::move(t));
        }
        supported(used.size()==passes.size(),"unreferenced pass records");
        result_.cacheBytes=narrow(cacheBytes);
    }
    void parseParameters(bool shared) {
        const size_t space=shared?1u:0u;
        const auto base=[this,shared](uint32_t p,uint32_t s) {
            const auto off=r_.word(shared?s:p);
            return shared?r_.word(off):off;
        };
        const uint32_t count=r_.word(shared?0x114:0x110),slots=r_.word(shared?0x11C:0x118);
        const uint32_t leaves=r_.word(shared?0x134:0x130),size=r_.word(shared?0x13C:0x138);
        const uint32_t desc=base(0x108,0x10C),values=base(0x128,0x12C);
        const uint32_t names=base(0x288,0x290),nameMap=base(0x298,0x29C),nameBytes=r_.word(shared?0x294:0x28C);
        if(slots==0) {
            valid(shared && count==0 && leaves==0 && size==0 && nameBytes==0,"empty parameter namespace counts");
            valid(desc==nullOffset && values==nullOffset && names==nullOffset && nameMap==nullOffset,"empty shared pointer cells");
            return;
        }
        supported(!shared || count==4 || count==11,"unqualified shared namespace profile");
        bounds(count>0 && count<slots && slots<=maxCount && leaves>0 && leaves<slots,"parameter descriptor/name/leaf count");
        r_.take(desc,8*uint64_t(slots),4); // Eight-byte stride need not imply eight-byte base alignment.
        r_.take(values,size,16);
        r_.take(names,nameBytes);
        r_.take(nameMap,4*uint64_t(slots),4);
        valid(size%16==0,"default block alignment");
        valid(r_.word(desc)==0 && r_.word(uint64_t(desc)+4)==0,"descriptor sentinel");
        const uint64_t namesEnd=uint64_t(names)+nameBytes;
        const bool annotations=r_.word(0x264)!=0;
        if(shared && annotations) r_.take(r_.word(0x284),2*uint64_t(slots),2);
        std::vector<Descriptor> descriptors;
        std::vector<uint32_t> leafIndices;
        uint32_t priorLeaves=0;
        for(uint32_t i=0;i<slots;++i) {
            const uint32_t w0=r_.word(uint64_t(desc)+8*uint64_t(i)),w1=r_.word(uint64_t(desc)+8*uint64_t(i)+4);
            const uint32_t kind=w0&3u;
            supported(kind<=2,"unknown parameter descriptor kind");
            const uint32_t np=r_.word(uint64_t(nameMap)+4*uint64_t(i));
            std::string name;
            if(np!=nullOffset) {
                bounds(np>=names && np<namesEnd,"per-descriptor name outside name area");
                name=r_.string(np,namesEnd);
            }
            const uint32_t cls=(w0>>2)&3u;
            descriptors.push_back({{w0,w1},kind,kind?(w1&0xFFFFu):1u,
                                   (i<<18)|(priorLeaves<<1)|uint32_t(shared),4*(w1>>16),
                                   (w0>>2)&0x3FFFu,16*(w1&0xFFFFu),1+((w0>>7)&3u),
                                   1+((w0>>9)&3u),cls,std::move(name)});
            if(i==0) {valid(np==nullOffset,"descriptor sentinel name");continue;}
            if(kind==0) {++priorLeaves;leafIndices.push_back(i);}
            if(shared && annotations) r_.half(uint64_t(r_.word(0x284))+2*uint64_t(i));
        }
        valid(priorLeaves==leaves,"descriptor leaf count mismatch");
        for(size_t i=0;i<leafIndices.size();++i) {
            const auto& d=descriptors[leafIndices[i]];
            const uint32_t start=d.storageOffset;
            const uint32_t end=i+1<leafIndices.size()?descriptors[leafIndices[i+1]].storageOffset:size;
            bounds(start<end && end<=size,"descriptor default storage slot/interval");
            supported(i!=0 || start==0,"default storage prefix gap");
            if(d.reflectionClass!=3) {
                supported(end-start==16*d.rows,"numeric default storage row stride");
                valid(d.logicalBytes==4*d.rows*d.columns,"numeric descriptor logical extent");
            } else supported(d.logicalBytes==0,"unknown object logical extent");
            r_.take(uint64_t(values)+start,end-start,16);
        }
        std::function<uint32_t(uint32_t,uint32_t,uint32_t)> walk;
        walk=[&](uint32_t index,uint32_t limit,uint32_t depth)->uint32_t {
            bounds(depth<=32 && index>=1 && index<limit && limit<=slots,"descriptor nesting/index bounds");
            const auto& d=descriptors[index];
            bounds(d.span>0 && d.span<=limit-index,"descriptor subtree stride");
            const uint32_t end=index+d.span;
            if(d.kind) {
                bounds(d.childCount>0 && d.childCount<d.span,"descriptor child count/stride");
                uint32_t cursor=index+1;
                uint64_t logical=0;
                for(uint32_t child=0;child<d.childCount;++child) {
                    bounds(cursor<end,"descriptor child exceeds subtree");
                    logical+=descriptors[cursor].logicalBytes;
                    cursor=walk(cursor,end,depth+1);
                }
                valid(cursor==end,"descriptor children do not consume subtree");
                valid(logical==d.logicalBytes,"container logical extent differs from children");
            }
            return end;
        };
        uint32_t cursor=1,nameAt=names;
        std::set<std::string> seen;
        for(uint32_t i=0;i<count;++i) {
            bounds(cursor<slots,"top-level named descriptor count");
            auto name=r_.string(nameAt,namesEnd);
            const auto& d=descriptors[cursor];
            valid(name==d.name,"packed/per-descriptor name mismatch");
            supported(seen.insert(name).second,"duplicate top-level parameter name");
            nameAt=narrow(uint64_t(nameAt)+name.size()+1);
            result_.parameters[space].push_back({std::move(name),d.handle,d.words});
            cursor=walk(cursor,slots,0);
        }
        valid(cursor==slots,"top-level names do not consume descriptor table");
        result_.defaultOffsets[space]=values;
        result_.defaultBytes[space]=size;
    }
    void checkAuxiliary() const {
        // Original relocator's root pointers. Auxiliary grammars and shader
        // reflection internals remain opaque; no query capability is invented.
        constexpr uint32_t fields[]={0x108,0x10C,0x128,0x12C,0x200,0x210,0x224,0x230,
            0x23C,0x248,0x254,0x260,0x26C,0x274,0x278,0x27C,0x280,0x284,
            0x288,0x290,0x298,0x29C,0x2A0,0x2A4,0x2A8};
        for(auto field:fields) r_.target(r_.word(field));
        const auto count=r_.word(0x264),at=r_.word(0x2A0);
        bounds(count<=maxCount,"auxiliary link count");
        if(count) {
            r_.take(at,4*uint64_t(count),2);
            for(uint32_t i=0;i<count;++i) r_.target(r_.word(uint64_t(at)+4*uint64_t(i),true));
        } else supported(at==nullOffset,"empty auxiliary link array");
        const auto block=r_.word(0x26C),size=r_.word(0x270);
        if(size) r_.take(block,size,4);
        else supported(block==nullOffset,"empty auxiliary byte block");
    }
    Reader r_;
    uint32_t bodyAddress_,prefix_;
    Parsed result_;
    std::vector<ShaderEntry> vertexEntries_,pixelEntries_;
    std::vector<StateBlock<EffectScalar>> scalars_;
    std::vector<StateBlock<EffectSampler>> samplers_;
    std::vector<Context> contexts_;
};
}

std::span<const EffectIdentity> originalEffectIdentities() noexcept {return Generated::effectIdentities;}

EffectRecord::EffectRecord(uint32_t address,std::span<const uint8_t> source) {
    const auto& identities=Generated::effectIdentities;
    const auto found=std::find_if(identities.begin(),identities.end(),[address](const auto& i){return i.originalAddress==address;});
    if(found==identities.end()) throw EffectError("identity: unknown original FX address");
    if(source.size()!=found->recordBytes) throw EffectError("identity: FX record must have exact pinned length");
    identity_=&*found;
    bytes_.assign(source.begin(),source.end());
    const Reader envelope(bytes_);
    supported(envelope.word(0)==0xA3D70141u,"FX magic/version");
    const auto length=envelope.word(4),prefix=envelope.word(8);
    valid(length>=0x310 && uint64_t(length)+12==bytes_.size() && prefix>=0x310 && prefix<=length,
          "exact FX envelope length/copy prefix");
    auto parsed=Parser(narrow(uint64_t(address)+12),body(),prefix).run();
    if(!detail::matchesOriginalRecordDigest(bytes_,identity_->sha256)) throw EffectError("identity: original FX SHA256 mismatch");
    techniques_=std::move(parsed.techniques);
    parameters_=std::move(parsed.parameters);
    defaultOffsets_=parsed.defaultOffsets;
    defaultBytes_=parsed.defaultBytes;
    shaders_=std::move(parsed.shaders);
    cacheBytes_=parsed.cacheBytes;
}

}
