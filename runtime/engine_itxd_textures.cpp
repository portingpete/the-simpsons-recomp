#include "engine_itxd_textures.h"
#include "engine_cpu_calls.h"
#include "engine_driver.h"
#include "renderer/native_backend.h"
#include "renderer/itxd_blocks.h"
#include "renderer/native_input_prompts.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace {
using namespace Simpsons;
void need(bool ok,const char* why){if(!ok)throw Failure(std::string("Native ITXD: ")+why);}
uint32_t address(uint64_t value){need(value<=UINT32_MAX,"address arithmetic overflow");return uint32_t(value);}
struct HostState {
    const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
    HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
};
using Metadata=std::array<uint8_t,0x100>;
uint32_t word(const Metadata& m,uint32_t at){return uint32_t(m[at])<<24|uint32_t(m[at+1])<<16|uint32_t(m[at+2])<<8|m[at+3];}
void word(Metadata& m,uint32_t at,uint32_t v){for(uint32_t i=0;i<4;++i)m[at+i]=uint8_t(v>>(24-8*i));}
EngineITXDTextures& service(uint8_t* base){
    need(active&&active->base==base&&active->engineDriver,"runtime/driver owner absent");
    return active->engineDriver->itxdTextures();
}
}
namespace Simpsons {
struct EngineITXDTextures::State {
    Runtime& runtime;Graphics::NativeBackend& backend;const uint32_t context;
    mutable std::mutex mutex;
    enum class Phase {Relocated,Published,Releasing};
    struct Source {
        uint32_t u{},p{},n{},key{};Metadata metadata{};
        std::array<uint32_t,6> descriptor{};
    };
    struct Record {
        Source source;uint32_t t{},p{},allocator{},vtable{};
        std::array<uint32_t,4> allocatorFunctions{};
        uint64_t generation{};Phase phase=Phase::Relocated;
        Metadata relocated{};std::vector<uint8_t> pixels;
        std::shared_ptr<Graphics::Texture> texture;
    };
    struct Copy {
        Source source;uint32_t sp{},t{},allocator{},vtable{};
        std::array<uint32_t,4> allocatorFunctions{};bool payloadAllocation{};
        std::optional<Record> copied;
    };
    struct Load {
        uint32_t sp{},begin{},bytes{},owner{},stream{},cursor{},plugin{};
        std::vector<Source> sources;std::optional<Copy> copy;
    };
    struct Release {uint32_t t{},sp{};uint64_t generation{};bool entered{},recovered{};};
    std::unordered_map<DWORD,Load> loads;
    std::unordered_map<DWORD,Release> releases;
    std::unordered_map<uint32_t,Record> records; // T, never embedded SDK H.
    uint64_t nextGeneration=1;
    Graphics::NativeInputPrompts inputPrompts;
    State(Runtime& r,Graphics::NativeBackend& b,uint32_t c):runtime(r),backend(b),context(c){}
    std::shared_ptr<Graphics::Texture> nativeTexture(Record& r);
    void require(uint8_t* base) const {
        need(active==&runtime&&base==runtime.base,"wrong runtime");runtime.checkRunning();
        // No renderer-thread check here: CPU ownership can originate on workers.
        need(context&&PPC_LOAD_U32(0x82D5DA74)==context&&!PPC_LOAD_U32(0x82D0CAF8),"native context publication differs");
    }
    void frame(PPCContext& c,uint8_t* base) const {
        require(base);need(currentContext==&c&&c.r1.u32>=0x200&&!(c.r1.u32&15),"caller context/stack differs");
        runtime.pointer(c.r1.u32-0x100,0x200,true);
    }
    uint32_t read(uint32_t at) const {auto* base=runtime.base;runtime.pointer(at,4,false);return PPC_LOAD_U32(at);}
    void span(const Load& l,uint32_t at,uint32_t n) const {
        need(at>=l.begin&&uint64_t(at)+n<=uint64_t(l.begin)+l.bytes,"source outside declared dictionary payload");
        runtime.pointer(at,n,false);
    }
    Source source(const Load& l,uint32_t u) const {
        if(runtime.resourceAudit.active())try {
        char parameters[256],instance[128];std::string name="unreadable";
        try {
            const auto* raw=runtime.pointer(u,0x100,false);
            auto rd=[&](uint32_t off){return (uint32_t(raw[off])<<24)|(uint32_t(raw[off+1])<<16)|(uint32_t(raw[off+2])<<8)|raw[off+3];};
            const auto* text=reinterpret_cast<const char*>(raw+0x10);
            size_t length=0;while(length<0x40&&text[length])++length;name.assign(text,length);
            std::snprintf(parameters,sizeof(parameters),"payload_bytes=%u descriptor=%08X,%08X,%08X,%08X,%08X,%08X",
                rd(0xBC),rd(0xE8),rd(0xEC),rd(0xF0),rd(0xF4),rd(0xF8),rd(0xFC));
        }catch(...){std::snprintf(parameters,sizeof(parameters),"metadata=unreadable");}
        std::snprintf(instance,sizeof(instance),"metadata=%08X dictionary=%08X bytes=%u stream=%08X",u,l.begin,l.bytes,l.stream);
        runtime.resourceAudit.observe("texture",name,currentContext?uint32_t(currentContext->lr):0,
            parameters,"original-source-unvalidated",runtime.nativeDepthCopyCount.load(),instance);
        }catch(...){std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] texture source capture failed\n");}
        auto* base=runtime.base;span(l,u,0x100);Source s;s.u=u;
        std::memcpy(s.metadata.data(),runtime.pointer(u,0x100,false),0x100);
        const auto r=address(uint64_t(l.begin)+word(s.metadata,0));
        need(r==address(uint64_t(u)+0x78)&&word(s.metadata,0x78)==word(s.metadata,0),"copied raster layout differs");
        need(read(0x82E3DC94)==0x34,"raster extension offset differs");
        const auto h=address(uint64_t(l.begin)+word(s.metadata,0xAC));
        need(h==address(uint64_t(u)+0xCC),"embedded header layout differs");
        s.n=word(s.metadata,0xBC);const auto offset=word(s.metadata,0xC0);
        need(offset&&s.n,"null copied payload is unqualified");
        s.p=address(uint64_t(l.begin)+offset);span(l,s.p,s.n);
        need(l.plugin>=0x58&&l.plugin<=0x60,"texture plugin exceeds copied metadata");
        s.key=word(s.metadata,l.plugin+4);
        for(uint32_t i=0;i<6;++i)s.descriptor[i]=word(s.metadata,0xE8+4*i);
        need((s.descriptor[1]&0xFFFFF000u)<s.n&&(s.descriptor[5]&0xFFFFF000u)<s.n,
             "stored base/mip address exceeds copied pixel allocation");
        return s;
    }
    Load& load(){auto i=loads.find(GetCurrentThreadId());need(i!=loads.end(),"copy/relocate outside normal load scope");return i->second;}
    Record& find(uint32_t t){
        auto i=records.find(t);
        if(i==records.end()) {
            const auto* c=currentContext;
            std::fprintf(stderr,"[NATIVE ITXD LOOKUP REJECTED] T=%08X H=%08X records=%zu fn=%08X lr=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X r28=%08X r29=%08X r30=%08X r31=%08X\n",
                t,t+0xCC,records.size(),c?c->lastFunction:0,c?uint32_t(c->lr):0,
                c?c->r3.u32:0,c?c->r4.u32:0,c?c->r5.u32:0,c?c->r6.u32:0,
                c?c->r7.u32:0,c?c->r8.u32:0,c?c->r28.u32:0,c?c->r29.u32:0,
                c?c->r30.u32:0,c?c->r31.u32:0);
            need(false,"unknown or stale copied texture");
        }
        return i->second;
    }
    void allocator(const Record& r) const {
        need(read(0x82D57244)==r.allocator&&read(r.allocator)==r.vtable,"original allocator identity changed");
        constexpr std::array<uint32_t,4> slots={0,4,0x20,0x24};
        for(size_t i=0;i<slots.size();++i)need(read(r.vtable+slots[i])==r.allocatorFunctions[i],"original allocator family changed");
    }
    void sameSource(const Record& r,const Source& src) const {
        need(r.source.key==src.key&&r.source.descriptor==src.descriptor&&r.source.n==src.n&&
             !std::memcmp(r.source.metadata.data()+0x10,src.metadata.data()+0x10,0x40)&&
             !std::memcmp(r.pixels.data(),runtime.pointer(src.p,src.n,false),src.n),"cache key reused incompatible source bytes");
        for(uint32_t i=0x7C;i<0x100;++i){
            if((i>=0xAC&&i<0xB0)||(i>=0xC0&&i<0xC4))continue;
            // Original826F25F8 cache-hit branch skips the incoming copy and
            // relocation entirely. Authored hud_target_center/shine1 copies
            // have identical pixels/descriptors but this unused incoming word
            // varies. Keep the first owner's exact metadata unchanged.
            if(i>=0xA8&&i<0xAC){
                const auto a=word(r.source.metadata,0xA8),b=word(src.metadata,0xA8);
                if((a==0||a==0x01001000)&&(b==0||b==0x01001000))continue;
            }
            need(r.source.metadata[i]==src.metadata[i],"cache key reused incompatible raster/header metadata");
        }
    }
    void metadata(const Record& r,bool cleared) const {
        auto* base=runtime.base;runtime.pointer(r.t,0x100,false);
        need(PPC_LOAD_U32(r.t)==r.t+0x78&&read(0x82E3DC94)==0x34,"copied raster owner changed");
        auto expected=r.relocated;
        if(cleared){word(expected,0xBC,0);word(expected,0xC0,0);}
        // Names, dictionary links, texture sampler, refs and EA2F plugin fields
        // remain controlled by the original CPU. Raster/header bytes are fixed.
        need(!std::memcmp(runtime.pointer(r.t+0x78,0x88,false),expected.data()+0x78,0x88),"copied raster/header bytes changed");
        need(!std::memcmp(runtime.pointer(r.t+0x10,0x40,false),r.source.metadata.data()+0x10,0x40),"copied texture name changed");
        const auto plugin=read(0x82CF0600);
        need(plugin>=0x58&&plugin<=0x60&&read(r.t+plugin)==word(r.source.metadata,plugin)&&
             read(r.t+plugin+4)==r.source.key,"copied texture plugin identity changed");
        allocator(r);
    }
    void unbound(const Record& r) const {
        for(uint32_t stage=0;stage<8;++stage)need(read(0x82D0E3F8+24*stage)!=r.t+0x78,"final release while raster remains bound");
    }
    void releasable(const Record& r) const {
        metadata(r,true);unbound(r);
        need(!(r.source.descriptor[1]&0xFFFFF000u)&&!(r.p&0xFFF)&&
             (word(r.relocated,0xEC)&0xFFFFF000u)==r.p,"release requires allocation-base level zero");
        need(!word(r.relocated,0xB0),"nonzero auxiliary header release is unqualified");
        runtime.pointer(r.p,r.source.n,true);runtime.pointer(r.t,0x100,true);
    }
};
EngineITXDTextures::EngineITXDTextures(Runtime& r,Graphics::NativeBackend& b,uint32_t c):state(std::make_unique<State>(r,b,c)){}
EngineITXDTextures::~EngineITXDTextures(){
    if(!state->records.empty()||!state->loads.empty()||!state->releases.empty())
        std::fprintf(stderr,"[NATIVE ITXD] terminal teardown records=%zu loads=%zu releases=%zu; no original cleanup success implied\n",state->records.size(),state->loads.size(),state->releases.size());
}
void EngineITXDTextures::beginLoad(PPCContext& c,uint8_t* base){
    auto& s=*state;std::unique_lock lock(s.mutex);s.frame(c,base);
    need(!s.loads.contains(GetCurrentThreadId()),"nested ITXD load is unqualified");
    need(uint32_t(c.lr)==0x826F26E8&&c.r6.u32==0,"borrowed mode or non-normal loader is unqualified");
    const auto request=c.r31.u32;
    need(request==address(uint64_t(c.r1.u32)+0xD0)&&s.read(c.r1.u32+0x68)==0x8271191C,"named-loader frame provenance differs");
    s.runtime.pointer(request,0x18,false);const auto stream=s.read(request+0x10);
    s.runtime.pointer(stream,0x18,false);need(s.read(stream)==3,"ITXD source is not a memory stream");
    const auto cursor=s.read(stream+0xC),length=s.read(stream+0x10),bytes=s.read(request+0x14);
    need(cursor<=length&&bytes<=length-cursor&&bytes>=0x10,"declared payload exceeds memory stream");
    State::Load l;l.sp=c.r1.u32;l.begin=address(uint64_t(s.read(stream+0x14))+cursor);l.bytes=bytes;
    l.owner=s.read(request+4);l.stream=stream;l.cursor=cursor;l.plugin=s.read(0x82CF0600);
    need(uint64_t(l.begin)+bytes<=0x100000000ull&&c.r3.u32==l.owner&&c.r4.u32==l.begin&&c.r5.u32==bytes,"normal load arguments differ from declared envelope");
    s.span(l,l.begin,bytes);s.runtime.pointer(l.owner,4,false);
    const uint32_t count=PPC_LOAD_U16(l.begin+4)+PPC_LOAD_U16(l.begin+6);
    const auto sentinel=address(uint64_t(l.begin)+8ull*(count+1)+8);s.span(l,sentinel,8);
    std::unordered_set<uint32_t> nodes;
    uint32_t link=s.read(sentinel);
    for(;;){
        need(link,"null dictionary link");const auto node=address(uint64_t(l.begin)+link);
        if(node==sentinel)break;
        need(node>=8&&nodes.insert(node).second&&nodes.size()<=bytes/8,"dictionary link cycle");
        s.span(l,node,8);l.sources.push_back(s.source(l,node-8));link=s.read(node);
    }
    {
        EngineCpuCalls cpu(c,base);
        for(const auto& src:l.sources){
            const auto t=cpu.invoke(0x826F8520,src.key);
            if(t){auto& r=s.find(t);need(r.phase==State::Phase::Published,"cache hit names incomplete or retiring owner");s.metadata(r,true);s.sameSource(r,src);}
        }
    }
    // Every selected record and payload is bounded before the original walker
    // allocates a group or publishes an index. No GPU qualification occurs here.
    std::fprintf(stderr,"[NATIVE ITXD LOAD] B=%08X M=%08X S=%08X cursor=%08X records=%zu owner=%08X thread=%u\n",l.begin,l.bytes,l.stream,l.cursor,l.sources.size(),l.owner,GetCurrentThreadId());
    s.loads.emplace(GetCurrentThreadId(),std::move(l));
    lock.unlock();if(boundaryObserver)boundaryObserver(0x826F24D8,c,base);
}
void EngineITXDTextures::beginCopy(PPCContext& c,uint8_t* base){
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(c,base);auto& l=s.load();
    need(!l.copy&&uint32_t(c.lr)==0x826F2608&&c.r1.u32+0x90==l.sp&&c.r4.u32==l.begin,"copy caller/scope differs");
    auto i=std::find_if(l.sources.begin(),l.sources.end(),[&](const auto& x){return x.u==c.r3.u32;});
    need(i!=l.sources.end(),"copy source not in bounded dictionary walk");
    auto src=s.source(l,i->u);need(src.metadata==i->metadata,"source metadata changed after load preflight");
    State::Copy copy;copy.source=std::move(src);copy.sp=c.r1.u32;l.copy=std::move(copy);
}
void EngineITXDTextures::observeAllocation(PPCContext& c,uint8_t* base,bool payload){
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(c,base);auto& l=s.load();
    need(l.copy.has_value(),"allocation outside captured copy scope");auto& copy=*l.copy;
    need(c.r1.u32+0xA0==copy.sp&&c.r25.u32==copy.source.u&&c.r28.u32==copy.source.n,"allocation copy frame/source differs");
    if(!payload){
        need(!copy.allocator&&c.r3.u32==c.r30.u32&&c.r4.u32==0x100&&c.r5.u32==c.r1.u32+0x50&&
             !s.read(c.r5.u32)&&!s.read(c.r5.u32+4)&&!s.read(c.r5.u32+8),"metadata allocator request differs");
        copy.allocator=c.r3.u32;need(copy.allocator&&copy.allocator==s.read(0x82D57244),"metadata allocator identity differs");
        copy.vtable=s.read(copy.allocator);constexpr std::array<uint32_t,4> slots={0,4,0x20,0x24};
        for(size_t i=0;i<slots.size();++i){copy.allocatorFunctions[i]=s.read(copy.vtable+slots[i]);need(copy.allocatorFunctions[i],"allocator family callback missing");}
        need(c.ctr.u32==copy.allocatorFunctions[0],"metadata allocator dispatch differs");
    }else{
        need(copy.allocator&&!copy.payloadAllocation&&c.r3.u32==copy.allocator&&c.r30.u32==copy.allocator&&
             c.r4.u32==copy.source.n&&c.r5.u32==UINT32_MAX&&c.r6.u32==0x1000&&c.r7.u32==0x404&&c.r8.u32==1&&
             c.ctr.u32==copy.allocatorFunctions[2],"pixel allocator request/family differs");
        need(c.r31.u32&&c.r29.u32==address(uint64_t(c.r31.u32)+0xAC),"metadata allocation result differs");
        copy.t=c.r31.u32;copy.payloadAllocation=true;
    }
}
void EngineITXDTextures::finishCopy(PPCContext& c,uint8_t* base){
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(c,base);auto& l=s.load();
    need(l.copy.has_value(),"copy completion without pending source");auto& copy=*l.copy;
    need(!copy.copied&&copy.payloadAllocation&&c.r31.u32==copy.t&&c.r1.u32+0xA0==copy.sp&&uint32_t(c.lr)==0x82737050&&c.r25.u32==copy.source.u&&
         c.r27.u32==copy.source.p&&c.r28.u32==copy.source.n,"copy completion registers differ");
    const uint32_t t=c.r31.u32,p=c.r30.u32;
    need(t&&p&&!(p&0xFFF)&&uint64_t(t)+0x100<=0x100000000ull&&uint64_t(p)+copy.source.n<=0x100000000ull&&
         c.r29.u32==t+0xAC&&!s.records.contains(t),"copied allocation identity/extent differs");
    need(uint64_t(t)+0x100<=p||uint64_t(p)+copy.source.n<=t,"copied allocations overlap");
    need(uint64_t(p)+copy.source.n<=l.begin||uint64_t(l.begin)+l.bytes<=p,"pixel allocation aliases source resource");
    need(uint64_t(t)+0x100<=l.begin||uint64_t(l.begin)+l.bytes<=t,"metadata allocation aliases source resource");
    s.runtime.pointer(t,0x100,true);s.runtime.pointer(p,copy.source.n,true);
    auto expected=copy.source.metadata;word(expected,0,0x78);word(expected,0x78,0x78);word(expected,0xAC,0xCC);
    need(!std::memcmp(s.runtime.pointer(t,0x100,false),expected.data(),0x100),"original 100-byte copy differs");
    need(!std::memcmp(s.runtime.pointer(p,copy.source.n,false),s.runtime.pointer(copy.source.p,copy.source.n,false),copy.source.n),"original N-byte pixel copy differs");
    State::Record r;r.source=copy.source;r.t=t;r.p=p;r.allocator=copy.allocator;
    r.vtable=copy.vtable;r.allocatorFunctions=copy.allocatorFunctions;s.allocator(r);
    r.pixels.assign(s.runtime.pointer(p,r.source.n,false),s.runtime.pointer(p,r.source.n,false)+r.source.n);
    word(expected,0xC0,p-t);r.relocated=expected;copy.t=t;copy.copied=std::move(r);
}
void EngineITXDTextures::finishRelocate(PPCContext& c,uint8_t* base){
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(c,base);auto& l=s.load();
    need(l.copy&&l.copy->copied,"relocation without captured copies");auto& copy=*l.copy;auto& r=*copy.copied;
    need(c.r31.u32==r.t&&c.r1.u32+0x60==copy.sp&&s.read(c.r1.u32+0x58)==0x826F2610&&
         uint32_t(c.lr)==0x82736F3C,"relocation caller/frame differs");
    auto expected=r.relocated;word(expected,0,r.t+0x78);word(expected,0x78,r.t+0x78);word(expected,0xAC,r.t+0xCC);
    if(word(expected,0xB0))word(expected,0xB0,address(uint64_t(r.t)+word(expected,0xB0)));
    const auto relocate=[&](uint32_t w){return (address(uint64_t(w&0xFFFFF000u)+r.p)&0xFFFFF000u)|(w&0xFFF);};
    word(expected,0xEC,relocate(word(expected,0xEC)));
    // Original 82C20E80 patches H+30 only when its level-count helper returns
    // more than one. Admit either exact original branch result, no other edits.
    const auto tail=s.read(r.t+0xFC),before=word(expected,0xFC);
    need(tail==before||tail==relocate(before),"original mip-address relocation differs");word(expected,0xFC,tail);
    need(!std::memcmp(s.runtime.pointer(r.t,0x100,false),expected.data(),0x100),"original metadata relocation differs");
    need(!std::memcmp(s.runtime.pointer(r.p,r.source.n,false),r.pixels.data(),r.source.n),"payload changed during relocation");
    r.relocated=expected;r.generation=s.nextGeneration++;s.allocator(r);
    const auto t=r.t;
    std::fprintf(stderr,"[NATIVE ITXD COPY] T=%08X R=%08X H=%08X P=%08X N=%08X allocator=%08X generation=%llu format=%08X name=%.64s CPU copies/relocation verified; GPU deferred\n",t,t+0x78,t+0xCC,r.p,r.source.n,r.allocator,static_cast<unsigned long long>(r.generation),word(expected,0xC4),reinterpret_cast<const char*>(r.source.metadata.data()+0x10));
    std::string evidence="[NATIVE ITXD SOURCE] generation="+std::to_string(r.generation)+" metadata=";
    constexpr char hex[]="0123456789ABCDEF";
    auto appendHex=[&](uint8_t b){evidence+=hex[b>>4];evidence+=hex[b&15];};
    for(auto b:r.source.metadata)appendHex(b);
    evidence+=" payload_prefix=";
    for(size_t i=0;i<std::min(size_t(64),r.pixels.size());++i)appendHex(r.pixels[i]);
    std::fprintf(stderr,"%s\n",evidence.c_str());
    need(s.records.emplace(t,std::move(r)).second,"duplicate copied generation");l.copy.reset();
}
void EngineITXDTextures::endLoad(PPCContext& c,uint8_t* base){
    auto& s=*state;std::unique_lock lock(s.mutex);s.frame(c,base);auto& l=s.load();
    need(!l.copy&&c.r1.u32+0x90==l.sp&&c.r28.u32==l.begin&&!c.r25.u32,"load completion scope differs");
    need(s.read(l.stream+0xC)==l.cursor&&s.read(l.stream+0x14)+uint64_t(l.cursor)==l.begin,"source stream moved during synchronous load");
    const auto group=c.r27.u32;s.runtime.pointer(group,12,false);
    need(group&&s.read(group)==s.read(l.owner)&&s.read(group+8)==0x82736D50,"original copied group owner/destructor differs");
    std::vector<uint32_t> members;std::unordered_set<uint32_t> links;
    for(auto at=s.read(group+4);at;at=s.read(at+4)){
        s.runtime.pointer(at,8,false);need(links.insert(at).second&&links.size()<=l.sources.size(),"original group list cycle/extent differs");
        const auto t=s.read(at);auto& r=s.find(t);need(r.phase!=State::Phase::Releasing,"cache reused a retiring generation");
        s.metadata(r,true);need(s.read(t+0x54)>=2,"original group did not retain texture reference");members.push_back(t);
    }
    need(members.size()==l.sources.size(),"original group texture count differs");
    // Use the original read-only lookup; index/tree implementation stays AOT.
    {
        EngineCpuCalls cpu(c,base);
        for(const auto& src:l.sources){
            const auto t=cpu.invoke(0x826F8520,src.key);auto& r=s.find(t);
            auto i=std::find(members.begin(),members.end(),t);need(i!=members.end(),"original group/index association differs");members.erase(i);
            s.sameSource(r,src);
            r.phase=State::Phase::Published;
        }
    }
    std::fprintf(stderr,"[NATIVE ITXD LOAD] group=%08X records=%zu original index/group publication verified\n",group,l.sources.size());
    s.loads.erase(GetCurrentThreadId());
    lock.unlock();if(boundaryObserver)boundaryObserver(0x826F2654,c,base);
}
void EngineITXDTextures::preflightRelease(PPCContext& c,uint8_t* base){
    if(state->runtime.resourceAudit.active())try {
    state->runtime.resourceAudit.observe("texture_release","guest-record",uint32_t(c.lr),"final-group-release",
        "owner-unvalidated",state->runtime.nativeDepthCopyCount.load(),"record="+std::to_string(c.r3.u32));
    }catch(...){std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] texture release capture failed\n");}
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(c,base);const DWORD thread=GetCurrentThreadId();
    if(uint32_t(c.lr)==0x826F81D8){
        // Before 826F80C0 removes either index or runs plugin callbacks. The
        // group walker has already performed its original reference decrement.
        need(c.r4.u32==0x82736D50&&!s.releases.contains(thread),"final-release destructor/scope differs");
        auto& r=s.find(c.r3.u32);need(r.phase==State::Phase::Published,"final release of unpublished/retiring generation");s.releasable(r);
        const auto plugin=s.read(0x82CF0600);need(!s.read(r.t+plugin+0x10),"final release still has an additional group owner");
        s.releases.emplace(thread,State::Release{r.t,c.r1.u32,r.generation,false,false});r.phase=State::Phase::Releasing;
    }else{
        need(uint32_t(c.lr)==0x826F815C,"copied destructor called outside final group release");
        auto i=s.releases.find(thread);need(i!=s.releases.end(),"copied destructor lacks pre-index preflight");auto& release=i->second;auto& r=s.find(release.t);
        need(!release.entered&&c.r3.u32==r.t&&c.r1.u32+0x90==release.sp&&r.generation==release.generation,"copied destructor generation/frame differs");
        s.releasable(r);release.entered=true;
    }
}
void EngineITXDTextures::recoverReleasePayload(PPCContext& c,uint8_t* base){
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(c,base);
    auto i=s.releases.find(GetCurrentThreadId());need(i!=s.releases.end(),"release payload recovery without final owner");auto& release=i->second;auto& r=s.find(release.t);
    need(release.entered&&!release.recovered&&c.r1.u32+0x110==release.sp&&c.r29.u32==r.t&&c.r31.u32==r.allocator&&
         c.r30.u32==r.t+0xCC&&c.r3.u32==r.t+0xCC&&!c.r4.u32&&c.r5.u32==c.r1.u32+0x50&&!c.r6.u32&&c.r7.u32==0x1003&&
         r.generation==release.generation,"scoped destructor lock output ABI differs");s.releasable(r);
    s.runtime.pointer(c.r1.u32+0x54,4,true);PPC_STORE_U32(c.r1.u32+0x54,r.p);
    // Only the pointer consumed at DB4 is supplied. Original allocator vtable
    // +24 frees P; vtable +4 frees T with size100. No native free is substituted.
    release.recovered=true;
}
void EngineITXDTextures::finishRelease(PPCContext& c,uint8_t* base){
    auto& s=*state;std::lock_guard lock(s.mutex);s.frame(c,base);
    auto i=s.releases.find(GetCurrentThreadId());need(i!=s.releases.end(),"release completion without final owner");const auto release=i->second;auto& r=s.find(release.t);
    need(release.recovered&&c.r1.u32+0x110==release.sp&&c.r29.u32==r.t&&c.r31.u32==r.allocator&&
         uint32_t(c.lr)==0x82736DE4&&r.generation==release.generation,"original allocator-free completion differs");
    // Guest bytes may already be unmapped/reused; never inspect freed memory.
    // Native shared_ptr aliases independently retain any outstanding GPU use.
    std::fprintf(stderr,"[NATIVE ITXD RELEASE] T=%08X P=%08X N=%08X generation=%llu original payload/metadata free calls completed\n",r.t,r.p,r.source.n,static_cast<unsigned long long>(r.generation));
    s.records.erase(release.t);s.releases.erase(i);
}
bool EngineITXDTextures::ownsRaster(uint32_t raster) const {
    auto& s=*state;std::lock_guard lock(s.mutex);s.require(s.runtime.base);
    if(raster<0x78)return false;const auto i=s.records.find(raster-0x78);
    return i!=s.records.end()&&i->second.phase==State::Phase::Published;
}
uint32_t EngineITXDTextures::rasterByHeader(uint8_t* base,uint32_t header) const {
    auto& s=*state;std::lock_guard lock(s.mutex);s.require(base);
    if(header<0xCC)return 0;
    const auto i=s.records.find(header-0xCC);
    if(i==s.records.end()||i->second.phase!=State::Phase::Published)return 0;
    const auto& record=i->second;
    if(uint64_t(record.t)+0xCC!=header)return 0;
    s.metadata(record,true);
    return record.t+0x78;
}
size_t EngineITXDTextures::count() const {auto& s=*state;std::lock_guard lock(s.mutex);s.require(s.runtime.base);return s.records.size();}
std::optional<EngineITXDTextures::AuditView> EngineITXDTextures::auditIdentity(uint32_t header) const {
    auto& s=*state;std::lock_guard lock(s.mutex);
    if(header<0xCC)return {};
    const auto found=s.records.find(header-0xCC);if(found==s.records.end())return {};
    const auto& r=found->second;const auto* text=reinterpret_cast<const char*>(r.source.metadata.data()+0x10);
    size_t size=0;while(size<0x40&&text[size])++size;
    return AuditView{std::string(text,size),r.source.descriptor,r.source.n,uint32_t(r.phase),r.source.u,r.source.p,r.generation};
}
void EngineITXDTextures::requireReleased() const {
    auto& s=*state;std::lock_guard lock(s.mutex);
    need(s.records.empty()&&s.loads.empty()&&s.releases.empty(),"driver shutdown retains copied records or incomplete load/release scopes");
}
std::shared_ptr<Graphics::Texture> EngineITXDTextures::texture(uint8_t* base,uint32_t raster){
    auto& s=*state;std::lock_guard lock(s.mutex);s.require(base);s.backend.validateSubmissionContext();
    need(raster>=0x78,"unknown copied raster");auto& r=s.find(raster-0x78);
    return s.nativeTexture(r);
}
std::shared_ptr<Graphics::Texture> EngineITXDTextures::textureFromHeader(uint8_t* base,uint32_t header){
    auto& s=*state;std::lock_guard lock(s.mutex);s.require(base);s.backend.validateSubmissionContext();
    need(header>=0xCC,"unknown copied texture header");auto& record=s.find(header-0xCC);
    return s.nativeTexture(record);
}
std::shared_ptr<Graphics::Texture> EngineITXDTextures::inputPromptTexture(uint8_t* base,uint32_t raster,const NativeControlSettings* controls){
    auto& s=*state;std::lock_guard lock(s.mutex);s.require(base);s.backend.validateSubmissionContext();
    if(raster<0x78)return {};
    const auto found=s.records.find(raster-0x78);
    if(found==s.records.end())return {};
    auto& r=found->second;
    need(r.phase==State::Phase::Published,"prompt consumption before publication or during release");
    const auto* name=reinterpret_cast<const char*>(r.source.metadata.data()+0x10);
    const auto end=std::find(name,name+64,'\0');
    if(!Graphics::isInputPromptAtlas(std::string_view(name,size_t(end-name)),word(r.relocated,0x84),word(r.relocated,0x88)))return {};
    s.metadata(r,true);
    return controls?s.inputPrompts.texture(s.backend,Graphics::keyboardMousePromptLayout(*controls)):s.inputPrompts.texture(s.backend);
}
std::shared_ptr<Graphics::Texture> EngineITXDTextures::paletteFromHeader(uint8_t* base,uint32_t header){
    auto& s=*state;std::lock_guard lock(s.mutex);s.require(base);s.backend.validateSubmissionContext();
    need(header>=0xCC,"unknown palette header");auto& r=s.find(header-0xCC);
    need(!std::memcmp(r.source.metadata.data()+0x10,"simpsons_palette",sizeof("simpsons_palette")),"effect header does not name the original palette");
    return s.nativeTexture(r);
}
std::shared_ptr<Graphics::Texture> EngineITXDTextures::State::nativeTexture(Record& r){
    auto& s=*this;const auto raster=r.t+0x78;
    need(r.phase==State::Phase::Published,"texture consumption before publication or during release");s.metadata(r,true);
    const auto width=word(r.relocated,0x84),height=word(r.relocated,0x88);
    const auto format=word(r.relocated,0xC4);
    if(format==0x28000102 && !word(r.relocated,0xB0)) {
        if(!r.texture){std::vector<std::vector<uint8_t>> levels;
            try {levels=Graphics::decodeITXDLuminanceMipChain(r.source.descriptor,width,height,r.pixels);}
            catch(const Graphics::Error&) {
                const auto& d=r.source.descriptor;
                std::fprintf(stderr,"[NATIVE ITXD L8 REJECTED] R=%08X H=%08X source=%08X generation=%llu name=%.64s extent=%ux%u payload=%08X bytes=%zu descriptor=%08X,%08X,%08X,%08X,%08X,%08X fn=%08X lr=%08X\n",
                    raster,r.t+0xCC,r.source.u,static_cast<unsigned long long>(r.generation),
                    reinterpret_cast<const char*>(r.source.metadata.data()+0x10),width,height,r.p,r.pixels.size(),
                    d[0],d[1],d[2],d[3],d[4],d[5],currentContext?currentContext->lastFunction:0,
                    currentContext?uint32_t(currentContext->lr):0);
                throw;
            }
            std::vector<std::span<const uint8_t>> views;for(const auto& level:levels)views.emplace_back(level);
            r.texture=s.backend.createTextureMipChain(width,height,Graphics::TextureFormat::RGBA8,views);
            std::fprintf(stderr,"[NATIVE ITXD L8] R=%08X generation=%llu extent=%ux%u name=%.64s levels=%zu; tiled luminance RRR1 owned upload\n",
                raster,static_cast<unsigned long long>(r.generation),width,height,reinterpret_cast<const char*>(r.source.metadata.data()+0x10),levels.size());}
        s.backend.validateTexture(r.texture);return r.texture;
    }
    if(format==0x18280186 && !word(r.relocated,0xB0) &&
       (!std::memcmp(r.source.metadata.data()+0x10,"simpsons_palette",sizeof("simpsons_palette")) ||
        !std::memcmp(r.source.metadata.data()+0x10,"dual_simpsons_palette",sizeof("dual_simpsons_palette")))){
        if(!r.texture){const auto pixels=Graphics::decodeITXDPaletteRGBA8(r.source.descriptor,width,height,r.pixels);
            r.texture=s.backend.createTexture(width,height,Graphics::TextureFormat::RGBA8,pixels);
            std::fprintf(stderr,"[NATIVE ITXD PALETTE] R=%08X H=%08X generation=%llu extent=%ux%u; original tiled8-in-32/ZYXW decoded to owned RGBA8\n",
                raster,r.t+0xCC,static_cast<unsigned long long>(r.generation),width,height);}
        s.backend.validateTexture(r.texture);return r.texture;
    }
    // Layout admission is based on the exact descriptor/dimensions/allocation
    // checked by the decoder, not the asset's human-readable name.
    if(format==0x18280186 && !word(r.relocated,0xB0)) {
        if(!r.texture){
            const auto levels=Graphics::decodeITXDCandyRGBA8(r.source.descriptor,width,height,r.pixels);
            std::vector<std::span<const uint8_t>> views;for(const auto& level:levels)views.emplace_back(level);
            r.texture=s.backend.createTextureMipChain(width,height,Graphics::TextureFormat::RGBA8,views);
            std::fprintf(stderr,"[NATIVE ITXD RGBA8] R=%08X generation=%llu extent=%ux%u levels=%zu; original qualified RGBA8 owned upload\n",
                raster,static_cast<unsigned long long>(r.generation),width,height,levels.size());
        }
        s.backend.validateTexture(r.texture);return r.texture;
    }
    if(word(r.relocated,0xB0)||(format!=0x1A200152&&format!=0x1A200153&&format!=0x1A200154)){
        const auto& d=r.source.descriptor;
        std::fprintf(stderr,"[NATIVE ITXD GPU REJECTED] R=%08X generation=%llu name=%.64s extent=%ux%u format=%08X auxiliary=%08X payload=%08X bytes=%zu descriptor=%08X,%08X,%08X,%08X,%08X,%08X\n",
            raster,static_cast<unsigned long long>(r.generation),reinterpret_cast<const char*>(r.source.metadata.data()+0x10),
            width,height,word(r.relocated,0xC4),word(r.relocated,0xB0),r.p,r.pixels.size(),d[0],d[1],d[2],d[3],d[4],d[5]);
        need(false,"copied texture format/auxiliary profile is unqualified for GPU consumption");
    }
    if(!r.texture){
        const auto levels=Graphics::decodeITXDBCMipChain(r.source.descriptor,format&255,width,height,r.pixels);
        std::vector<std::span<const uint8_t>> views;for(const auto& level:levels)views.emplace_back(level);
        const auto nativeFormat=format==0x1A200152?Graphics::TextureFormat::BC1:
                                format==0x1A200153?Graphics::TextureFormat::BC2:Graphics::TextureFormat::BC3;
        r.texture=s.backend.createTextureMipChain(width,height,nativeFormat,views);
        std::fprintf(stderr,"[NATIVE ITXD GPU] R=%08X generation=%llu extent=%ux%u BC%u levels=%zu owned upload\n",raster,static_cast<unsigned long long>(r.generation),width,height,(format&255)-0x51,levels.size());
    }
    s.backend.validateTexture(r.texture);return r.texture;
}
}
void SimpsonsNativeITXDBeginLoad(PPCContext& c,uint8_t* b){HostState host;service(b).beginLoad(c,b);}
void SimpsonsNativeITXDEndLoad(PPCContext& c,uint8_t* b){HostState host;service(b).endLoad(c,b);}
void SimpsonsNativeITXDBeginCopy(PPCContext& c,uint8_t* b){HostState host;service(b).beginCopy(c,b);}
void SimpsonsNativeITXDMetadataAllocation(PPCContext& c,uint8_t* b){HostState host;service(b).observeAllocation(c,b,false);}
void SimpsonsNativeITXDPayloadAllocation(PPCContext& c,uint8_t* b){HostState host;service(b).observeAllocation(c,b,true);}
void SimpsonsNativeITXDFinishCopy(PPCContext& c,uint8_t* b){HostState host;service(b).finishCopy(c,b);}
void SimpsonsNativeITXDPreflightRelease(PPCContext& c,uint8_t* b){HostState host;service(b).preflightRelease(c,b);}
void SimpsonsNativeITXDFinishRelocate(PPCContext& c,uint8_t* b){HostState host;service(b).finishRelocate(c,b);}
void SimpsonsNativeITXDRecoverReleasePayload(PPCContext& c,uint8_t* b){HostState host;service(b).recoverReleasePayload(c,b);}
void SimpsonsNativeITXDFinishRelease(PPCContext& c,uint8_t* b){HostState host;service(b).finishRelease(c,b);}
