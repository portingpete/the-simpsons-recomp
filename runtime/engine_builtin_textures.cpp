#include "engine_builtin_textures.h"
#include "engine_cpu_calls.h"
#include "engine_driver.h"
#include "engine_reflection_textures.h"
#include "renderer/native_backend.h"
#include <bcrypt.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
using namespace Simpsons;
void need(bool ok,const char* why){if(!ok)throw Failure(std::string("Native built-in images: ")+why);}
struct HostState {
    const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
    HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
};
struct Profile{uint32_t caller,source,bytes,output,n,format,mips;const char* hash;};
constexpr std::array profiles={
    Profile{0x826FF1D8,0x82CED9A8,0x102C,0x82D63004,32,0x18280086,6,"f3170a8dbe573c58301af8d14dc170e6f7c48c777041368866dccf4f6eebd87a"},
    Profile{0x826FF1F8,0x82CEECF0,0x102C,0x82D6300C,32,0x18280086,6,"11902ad81bc906fee9886160ace564c2833962ef9e48092ae806a7cfac2890ec"},
    Profile{0x826FF218,0x82CEE9D8,0x312,0x82D63008,16,0x28280086,5,"abdd5f8fc1b285531063c29eb190a7672d58feae200ef331bf7eb80338d0abda"}};
void sourceIdentity(Runtime& rt,const Profile& p){
    std::array<uint8_t,32> hash{};std::array<char,65> text{};
    need(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,rt.pointer(p.source,p.bytes,false),p.bytes,hash.data(),ULONG(hash.size()))>=0,"source SHA256 failed");
    constexpr char hex[]="0123456789abcdef";
    for(size_t i=0;i<hash.size();++i){text[2*i]=hex[hash[i]>>4];text[2*i+1]=hex[hash[i]&15];}
    need(!std::strcmp(text.data(),p.hash),"original embedded image identity changed");
}
void descriptor(uint8_t* base,uint32_t at,uint32_t data,uint32_t n,uint32_t format){
    for(uint32_t i=0;i<21;++i)PPC_STORE_U32(at+4*i,0);
    PPC_STORE_U32(at,data);PPC_STORE_U32(at+4,format);PPC_STORE_U32(at+8,n*4);
    for(uint32_t offset:{0x10u,0x28u}){PPC_STORE_U32(at+offset+8,n);PPC_STORE_U32(at+offset+12,n);PPC_STORE_U32(at+offset+20,1);}
}
// Original CPU allocation and destructors own all temporary decoding/filtering
// storage. Native image creation captures its level bytes before this retires.
struct CpuStorage {
    EngineCpuCalls& cpu;uint32_t address{};bool imageLive{},filterLive{};
    explicit CpuStorage(EngineCpuCalls& c):cpu(c){
        address=cpu.invoke(0x8238E880,0x3040,0x24810000);need(address!=0,"CPU workspace allocation failed");
    }
    void close(){
        if(filterLive){cpu.invoke(0x82BC61D0,address+0xD0);filterLive=false;}
        if(imageLive){cpu.invoke(0x82BBE1F8,address);imageLive=false;}
        if(address){cpu.invoke(0x8238EB00,address,0x24810000);address=0;}
    }
    ~CpuStorage(){try{close();}catch(const std::exception& e){std::fprintf(stderr,"[NATIVE BUILTIN] CPU cleanup failed: %s\n",e.what());}}
};
std::shared_ptr<Graphics::Texture> build(Runtime& rt,Graphics::NativeBackend& backend,PPCContext& ctx,uint8_t* base,const Profile& p,uint32_t context){
    EngineCpuCalls cpu(ctx,base);CpuStorage owned(cpu);const uint32_t o=owned.address,info=o+0x80,req=o+0xB0,filter=o+0xD0,src=o+0x100,dst=o+0x180;
    rt.pointer(o,0x3040,true);cpu.invoke(0x82BBD720,o);owned.imageLive=true;
    need(!cpu.invoke(0x82BC2FF8,o,p.source,p.bytes,info,1),"original CPU decoder failed");
    auto data=PPC_LOAD_U32(o+4);const std::array<uint32_t,21> expected={p.format,data,0,p.n,p.n,1,0,0,p.n,p.n,0,1,p.n*4,0,1,0,1,3,2,0,0};
    need(data!=0,"original CPU image has no owned pixels");
    for(uint32_t i=0;i<expected.size();++i)need(PPC_LOAD_U32(o+4*i)==expected[i],"original decoded CPU image profile differs");
    const std::array<uint32_t,7> fileInfo={p.n,p.n,1,1,p.format,3,2};
    for(uint32_t i=0;i<fileInfo.size();++i)need(PPC_LOAD_U32(info+4*i)==fileInfo[i],"original file info differs");
    for(uint32_t i=0;i<3;++i)PPC_STORE_U32(req+4*i,i==2?1:p.n);
    PPC_STORE_U32(req+12,0xFFFFFFFF);PPC_STORE_U32(req+16,p.format);
    auto& c=cpu.registers();c.r3.u64=context;c.r4.u64=req;c.r5.u64=req+4;c.r6.u64=req+8;c.r7.u64=req+12;c.r8.u64=0;c.r9.u64=req+16;c.r10.u64=1;
    PPC_STORE_U32(c.r1.u32+0x54,3);need(!cpu.invoke(0x82B7F950),"original texture requirements failed");
    const std::array<uint32_t,5> normalized={p.n,p.n,1,p.mips,p.format};
    for(uint32_t i=0;i<normalized.size();++i)need(PPC_LOAD_U32(req+4*i)==normalized[i],"original texture requirements differ");
    std::vector<std::vector<uint8_t>> levels;levels.reserve(p.mips);uint32_t sourceN=p.n;
    for(uint32_t level=0,n=p.n;level<p.mips;++level,n>>=1){
        const uint32_t destination=o+(level&1?0x2010:0x1010),bytes=n*n*4;
        descriptor(base,src,data,sourceN,p.format);descriptor(base,dst,destination,n,p.format);
        cpu.invoke(0x82BC52E0,filter);owned.filterLive=true;
        need(!cpu.invoke(0x82BC8EB8,filter,dst,src,level?5:0x80004),"original CPU mip filter failed");
        need(!PPC_LOAD_U32(filter)&&!PPC_LOAD_U32(filter+4),"original filter retained CPU helper ownership");
        cpu.invoke(0x82BC61D0,filter);owned.filterLive=false;
        const auto* original=rt.pointer(destination,bytes,false);std::vector<uint8_t> pixels(bytes);
        for(uint32_t i=0;i<bytes;i+=4){
            // BE32 ARGB memory. ZYX1 retains its unused X byte even when the
            // original box filter changes it to zero; native sampling returns1.
            pixels[i]=original[i+(p.format==0x18280086?1:3)];pixels[i+1]=original[i+2];
            pixels[i+2]=original[i+(p.format==0x18280086?3:1)];pixels[i+3]=original[i];
        }
        levels.push_back(std::move(pixels));data=destination;sourceN=n;
    }
    std::vector<std::span<const uint8_t>> spans;spans.reserve(levels.size());for(const auto& level:levels)spans.emplace_back(level);
    auto texture=backend.createTextureMipChain(p.n,p.n,p.format==0x18280086?Graphics::TextureFormat::RGBA8:Graphics::TextureFormat::BGRX8,spans);
    owned.close();return texture;
}
EngineDriver& driver(uint8_t* base){need(active&&active->base==base&&active->engineDriver,"driver owner absent");return *active->engineDriver;}
}
namespace Simpsons {
struct EngineBuiltinTextures::State {
    Runtime& runtime;Graphics::NativeBackend& backend;const uint32_t context,thread;
    struct Record{View view{};std::shared_ptr<Graphics::Texture> texture;};
    std::array<Record,3> records{};size_t loaded{};bool committed{};uint32_t parentSp{},reflection{};
    State(Runtime& rt,Graphics::NativeBackend& b,uint32_t c):runtime(rt),backend(b),context(c),thread(GetCurrentThreadId()){}
    void require(uint8_t* base) const {
        need(active==&runtime&&base==runtime.base&&GetCurrentThreadId()==thread,"wrong runtime/thread");runtime.checkRunning();
        runtime.engineDriver->requireContext(context);backend.validateSubmissionContext();
        need(PPC_LOAD_U32(0x82D5DA74)==context&&!PPC_LOAD_U32(0x82D0CAF8),"native context publication differs");
    }
    void frame(PPCContext& ctx,uint8_t* base) const {
        require(base);need(currentContext==&ctx&&ctx.r1.u32>=0x200&&!(ctx.r1.u32&15),"caller stack/context differs");runtime.pointer(ctx.r1.u32-0x100,0x200,true);
    }
    void publications(uint8_t* base) const {
        for(size_t i=0;i<records.size();++i){const auto& r=records[i];
            need(PPC_LOAD_U32(profiles[i].output)==(i<loaded?r.view.identity:0),"primary global publication differs");
            if(i<loaded){need(r.texture&&!runtime.pageAccess[r.view.identity>>12].load(),"native backing/identity differs");backend.validateTexture(r.texture);}
        }
        const std::array<uint32_t,3> aliases={0x82D63010,0x82D63014,0x82D63018};
        for(size_t i=0;i<aliases.size();++i)need(PPC_LOAD_U32(aliases[i])==(committed?records[i==1?0:1].view.identity:0),"borrowed alias publication differs");
    }
    const Record& find(uint32_t id) const {for(size_t i=0;i<loaded;++i)if(id&&records[i].view.identity==id)return records[i];throw Failure("Native built-in images: unknown or stale identity");}
};
EngineBuiltinTextures::EngineBuiltinTextures(Runtime& r,Graphics::NativeBackend& b,uint32_t c):state(std::make_unique<State>(r,b,c)){}
EngineBuiltinTextures::~EngineBuiltinTextures(){if(state->loaded)std::fprintf(stderr,"[NATIVE BUILTIN] terminal release of %zu image owners; original global teardown is a no-op, paired release unqualified\n",state->loaded);}
void EngineBuiltinTextures::load(PPCContext& ctx,uint8_t* base){
    auto& s=*state;s.frame(ctx,base);
    // Diagnostic observation precedes mutation. Returning always executes the
    // same original CPU decoding/filtering and native creation as production.
    if(s.runtime.builtinImageBoundaryObserver)s.runtime.builtinImageBoundaryObserver(0x82B84838,ctx,base);
    need(!s.committed&&s.loaded<profiles.size(),"duplicate image bundle or unsupported loader caller");const auto& p=profiles[s.loaded];
    need(uint32_t(ctx.lr)==p.caller&&ctx.r3.u32==s.context&&ctx.r4.u32==p.source&&ctx.r5.u32==p.bytes&&ctx.r6.u32==p.output&&ctx.r28.u32==s.context,"unsupported loader caller/source/output profile");
    s.publications(base);s.runtime.pointer(p.output,4,true);sourceIdentity(s.runtime,p);
    const uint32_t reflection=PPC_LOAD_U32(0x82D6301C);const auto v=s.runtime.engineDriver->reflectionTextures().view(reflection);
    need(v.phase==EngineReflectionTextures::Phase::Ready&&v.context==s.context,"original reflection constructor is incomplete");
    if(s.loaded)need(ctx.r1.u32==s.parentSp&&reflection==s.reflection,"original parent frame/owner changed");
    auto texture=build(s.runtime,s.backend,ctx,base,p,s.context);
    const uint32_t id=s.runtime.engineDriver->allocateTargetIdentity();
    s.records[s.loaded]={{id,p.source,p.output,p.n,p.format,p.mips},std::move(texture)};
    s.parentSp=ctx.r1.u32;s.reflection=reflection;++s.loaded;PPC_STORE_U32(p.output,id);ctx.r3.u64=0;
    std::fprintf(stderr,"[NATIVE BUILTIN] source=%08X id=%08X extent=%u levels=%u original-format=%08X original CPU decode/filter complete, native upload captured, CPU storage retired\n",p.source,id,p.n,p.mips,p.format);
}
void EngineBuiltinTextures::commit(PPCContext& ctx,uint8_t* base){
    auto& s=*state;s.frame(ctx,base);need(!s.committed&&s.loaded==3&&ctx.r1.u32==s.parentSp&&uint32_t(ctx.lr)==0x826FF218&&!ctx.r3.u32&&ctx.r28.u32==s.context&&PPC_LOAD_U32(0x82D6301C)==s.reflection,"original parent completion differs");
    // Original stores826FF224/22C/238 have now published the borrowed aliases.
    s.committed=true;try{s.publications(base);}catch(...){s.committed=false;throw;}
    std::fprintf(stderr,"[NATIVE BUILTIN] original parent completed: three owned images,17 mip levels,three borrowed aliases; no extra retains\n");
}
size_t EngineBuiltinTextures::count() const {state->require(state->runtime.base);return state->loaded;}
bool EngineBuiltinTextures::complete() const {state->require(state->runtime.base);state->publications(state->runtime.base);return state->committed;}
bool EngineBuiltinTextures::owns(uint32_t id) const {state->require(state->runtime.base);for(size_t i=0;i<state->loaded;++i)if(id&&state->records[i].view.identity==id)return true;return false;}
EngineBuiltinTextures::View EngineBuiltinTextures::view(uint32_t id) const {state->require(state->runtime.base);state->publications(state->runtime.base);return state->find(id).view;}
std::shared_ptr<Graphics::Texture> EngineBuiltinTextures::texture(uint32_t id) const {(void)view(id);return state->find(id).texture;}
std::vector<uint8_t> EngineBuiltinTextures::readback(uint32_t id,uint32_t level){return state->backend.readbackMip(texture(id),level);}
void EngineBuiltinTextures::requireReleased() const {need(active==&state->runtime&&GetCurrentThreadId()==state->thread,"retirement runtime/thread differs");need(!state->loaded,"original global images retain native resources at driver stop");}
}
void SimpsonsNativeBuiltinImageLoad(PPCContext& c,uint8_t* b){HostState fp;driver(b).builtinTextures().load(c,b);}
void SimpsonsNativeBuiltinImagesCommit(PPCContext& c,uint8_t* b){HostState fp;driver(b).builtinTextures().commit(c,b);}
