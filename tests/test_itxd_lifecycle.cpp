// Actual original named-resource load, allocator copies, relocation, index/cache
// reuse and shared group release. Requires the player's original game data and
// tools/prepare_itxd_font_fixture.py outputs; no original asset is modified.
#include "effect_catalog_lifecycle_helpers.h"
#include "runtime/engine_itxd_textures.h"
#include <mutex>
#include <thread>
#include <unordered_set>

namespace {
struct Fixture {const char* name;const char* extension;uint32_t width,height,bytes,offset;Graphics::TextureFormat format;};
constexpr Fixture fontFixture{"HighlanderStdBold6060b",".bc2",1024,512,524288,0x928,Graphics::TextureFormat::BC2};
constexpr Fixture sharedFixture{"8_SharedLibrary",".bc3",512,256,131072,0xA28,Graphics::TextureFormat::BC3};
constexpr Fixture cacheFixture{"hud_target_center",".bc2",32,32,16384,0x128,Graphics::TextureFormat::BC2};
constexpr Fixture paletteFixture{"simpsons_palette",".rgba",64,64,16384,0x728,Graphics::TextureFormat::RGBA8};
constexpr Fixture buttonsFixture{"buttons",".bc2",256,256,65536,0x628,Graphics::TextureFormat::BC2};
Fixture fixture=fontFixture;
struct FontLoaded {};
struct ContextScope {
    PPCContext* previous=currentContext;
    explicit ContextScope(PPCContext& c){currentContext=&c;}
    ~ContextScope(){currentContext=previous;}
};
struct Capture {PPCContext entry{},finished{};uint32_t group{},font{};};
uint32_t findFont(Runtime& rt,uint32_t group){
    auto* base=rt.base;std::unordered_set<uint32_t> nodes;
    for(auto p=PPC_LOAD_U32(group+4);p;p=PPC_LOAD_U32(p+4)){
        rt.pointer(p,8,false);need(nodes.insert(p).second,"Fixture group cycle");const auto t=PPC_LOAD_U32(p);
        if(stringAt(rt,t+0x10)==fixture.name)return t;
    }
    return 0;
}
uint32_t loadAgain(Runtime& rt,const Capture& captured){
    auto c=captured.entry;ContextScope owner(c);const auto saved=abi(c);
    PPCGuestFloatingPointScope floatingPoint(c.fpscr);PPCSafeIndirect(c,rt.base,0x826F24D8);
    need(abi(c)==saved,"Original ITXD load nonvolatile ABI differs");return c.r3.u32;
}
void run(Runtime& rt,const Capture& captured,const std::filesystem::path& fixtureRoot){
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& service=driver.itxdTextures();
    const auto t=captured.font,r=t+0x78,group=captured.group,plugin=PPC_LOAD_U32(0x82CF0600);
    const auto records=service.count();need(records&&service.ownsRaster(r),"Original font has no published owner");
    need(plugin==0x58&&PPC_LOAD_U32(t+plugin+0xC)==group&&!PPC_LOAD_U32(t+plugin+0x10)&&PPC_LOAD_U32(t+0x54)==2,
         "Fixture requires font's first original owning group");
    need(PPC_LOAD_U32(t)==r&&PPC_LOAD_U32(r)==r&&PPC_LOAD_U32(t+0xAC)==t+0xCC&&
         !PPC_LOAD_U32(t+0xBC)&&!PPC_LOAD_U32(t+0xC0),"Original pointer relocation/bookkeeping clear differs");
    const auto metadata=snapshot(rt,t,0x100),source=Simpsons::readFile(fixtureRoot/(std::string(fixture.name)+".metadata"));
    need(source.size()==0x100&&!std::memcmp(source.data()+0x10,metadata.data()+0x10,0x40),"Font name differs from original asset fixture");
    const auto pixels=PPC_LOAD_U32(t+0xEC)&0xFFFFF000u;
    const auto tiled=Simpsons::readFile(fixtureRoot/(std::string(fixture.name)+".tiled"));
    need(tiled.size()==fixture.bytes,"Original tiled fixture extent differs");same(rt,pixels,tiled,"Original N-byte copy differs from fixture");

    // Invalid source inputs reject before a new group/index or registry owner.
    const auto indexRoots=snapshot(rt,0x82D62FB0,8);
    auto entry=captured.entry;ContextScope context(entry);
    auto reject=[&]{const auto before=entry;rejects([&]{service.beginLoad(entry,base);},"Malformed ITXD accepted");
        need(!std::memcmp(&before,&entry,sizeof(entry)),"Preflight changed PPC context");need(service.count()==records,"Rejected load published an owner");same(rt,0x82D62FB0,indexRoots,"Rejected load changed index roots");};
    entry.r6.u32=1;reject();entry=captured.entry;
    entry.r4.u32+=4;reject();entry=captured.entry;
    const auto request=entry.r31.u32,stream=PPC_LOAD_U32(request+0x10),begin=entry.r4.u32,bytes=entry.r5.u32;
    const auto oldLength=PPC_LOAD_U32(stream+0x10);PPC_STORE_U32(stream+0x10,0);reject();PPC_STORE_U32(stream+0x10,oldLength);
    PPC_STORE_U32(request+0x14,bytes-1);entry.r5.u32=bytes-1;reject();PPC_STORE_U32(request+0x14,bytes);entry=captured.entry;
    // Exact record offset is independently pinned in the fixture preparer.
    const auto u=begin+fixture.offset;
    need(stringAt(rt,u+0x10)==fixture.name,"Captured dictionary differs from the independently identified fixture");
    const auto oldN=PPC_LOAD_U32(u+0xBC),next=PPC_LOAD_U32(u+8);
    PPC_STORE_U32(u+0xBC,UINT32_MAX);reject();PPC_STORE_U32(u+0xBC,oldN);
    PPC_STORE_U32(u+8,u+8-begin);reject();PPC_STORE_U32(u+8,next);
    const auto authoredWord=PPC_LOAD_U32(u+0xA8);
    PPC_STORE_U32(u+0xA8,0x12345678);reject();PPC_STORE_U32(u+0xA8,authoredWord);

    // CPU lookup is allowed across threads; GPU resolution remains on its real
    // backend owner. A rejected cross-thread resolve must not allocate/upload.
    bool cpuAllowed=false,gpuRejected=false;
    std::thread worker([&]{cpuAllowed=service.ownsRaster(r)&&service.count()==records;
        try{service.texture(base,r);}catch(const std::exception&){gpuRejected=true;}});worker.join();
    need(cpuAllowed&&gpuRejected,"CPU/GPU thread ownership boundary differs");
    const auto originalFormat=PPC_LOAD_U32(t+0xC4);
    PPC_STORE_U32(t+0xC4,originalFormat^7);
    rejects([&]{service.texture(base,r);},"Altered original texture format accepted");
    PPC_STORE_U32(t+0xC4,originalFormat);
    const auto texture=service.texture(base,r);need(texture->width==fixture.width&&texture->height==fixture.height&&texture->format==fixture.format&&texture->levelCount()==1,
        "Original resource native format/extent differs");
    const auto promptTexture=service.inputPromptTexture(base,r);
    if(fixture.name==buttonsFixture.name){
        need(promptTexture&&promptTexture!=texture&&promptTexture->width==256&&promptTexture->height==256&&
             promptTexture->format==Graphics::TextureFormat::RGBA8&&promptTexture->levelCount()==1,
             "Native input prompt atlas did not preserve original UV dimensions");
        need(service.inputPromptTexture(base,r)==promptTexture,"Native input prompt atlas uploaded twice");
        need(!service.inputPromptTexture(base,0)&&!service.inputPromptTexture(base,r+4),
             "Native input prompts accepted a null or interior raster");
        bool promptThreadRejected=false;
        std::thread promptWorker([&]{try{service.inputPromptTexture(base,r);}catch(const std::exception&){promptThreadRejected=true;}});
        promptWorker.join();need(promptThreadRejected,"Native input prompt atlas accepted a foreign render thread");
        same(rt,t,metadata,"Native input prompt query changed original metadata");
    }else need(!promptTexture,"Unrelated original texture was replaced by input prompts");
    if(fixture.name==paletteFixture.name){
        need(service.paletteFromHeader(base,t+0xCC)==texture,"Palette header did not resolve its actual owned texture");
        rejects([&]{service.paletteFromHeader(base,t+0xCB);},"Interior palette header accepted");
        rejects([&]{service.paletteFromHeader(base,r);},"Raster accepted as embedded palette header");
    }
    const auto linear=Simpsons::readFile(fixtureRoot/(std::string(fixture.name)+fixture.extension));
    // Main's checked driver readback must route ITXD raster ownership too.
    need(driver.readbackTextureRaster(r)==linear,"Native compressed bytes differ from independently prepared original fixture");
    need(service.texture(base,r)==texture,"Lazy GPU owner uploaded twice");same(rt,t,metadata,"GPU creation mutated original CPU metadata");

    // The Ball Homer effect calls the shared texture helper directly at
    // 82771B40. Its r5 is the copied raster's embedded header pointer, while
    // the RenderWare stage cache can still be null. Exercise that actual owner
    // shape and require the host stage to receive the owned ITXD texture.
    const uint32_t extension=PPC_LOAD_U32(0x82E3DC94),header=PPC_LOAD_U32(r+extension);
    const uint32_t stageCache=0x82D0E3F8;
    need(extension==0x34&&header==t+0xCC&&!PPC_LOAD_U32(r+extension+4)&&!PPC_LOAD_U32(stageCache),
         "Copied ITXD direct-bind owner or initial stage cache differs");
    driver.setTextureRaster(base,0,header,0x80000000ull,0x82771B44);
    PPC_STORE_U32(stageCache,r);
    driver.preflightNullRaster(base,r,0); // Requires the same native texture in host stage 0.
    rejects([&]{driver.setTextureRaster(base,0,header+4,0x80000000ull,0x82771B44);},
            "Direct ITXD bind accepted a foreign embedded-header identity");
    driver.preflightNullRaster(base,r,0);
    PPC_STORE_U32(stageCache,0);
    rejects([&]{driver.setTextureRaster(base,0,header,0x80000000ull,0x82771B40);},
            "Direct ITXD bind accepted an unqualified caller");
    rejects([&]{driver.setTextureRaster(base,8,header,0x80000000ull,0x82771B44);},
            "Direct ITXD bind accepted an unsupported stage");
    rejects([&]{driver.setTextureRaster(base,0,header,0x40000000ull,0x82771B44);},
            "Direct ITXD bind accepted an incorrect stage mask");
    rejects([&]{driver.resetNullTexture(base,1,0x40000000ull,0x82771DB4);},
            "Direct ITXD clear accepted a nonzero stage");
    PPC_STORE_U32(stageCache,r);
    driver.preflightNullRaster(base,r,0);
    PPC_STORE_U32(stageCache,0);
    driver.resetNullTexture(base,0,0x80000000ull,0x82771DB4);
    PPC_STORE_U32(stageCache,r);
    bool cleared=false;
    try {driver.preflightNullRaster(base,r,0);}catch(const std::exception& error){
        cleared=std::string(error.what())=="Actual native engine texture binding differs from its original cache";
    }
    need(cleared,"Direct ITXD bind was not cleared");
    PPC_STORE_U32(stageCache,0);
    same(rt,t,metadata,"Direct ITXD bind changed copied CPU metadata");
    // 82751988 uses this same owned header from beam and other immediate
    // effects. A corrupted header must reject before replacing the cleared
    // host stage, and a successful call must bind this exact copied texture.
    const auto headerWord=PPC_LOAD_U32(header);
    PPC_STORE_U32(header,headerWord^1);
    rejects([&]{driver.setTextureRaster(base,0,header,0x80000000ull,0x827519D4);},
            "Immediate ITXD bind accepted changed copied header metadata");
    PPC_STORE_U32(header,headerWord);PPC_STORE_U32(stageCache,r);
    cleared=false;
    try {driver.preflightNullRaster(base,r,0);}catch(const std::exception& error){
        cleared=std::string(error.what())=="Actual native engine texture binding differs from its original cache";
    }
    need(cleared,"Rejected immediate ITXD bind changed the cleared native stage");
    PPC_STORE_U32(stageCache,0);
    driver.setTextureRaster(base,0,header,0x80000000ull,0x827519D4);
    PPC_STORE_U32(stageCache,r);driver.preflightNullRaster(base,r,0);
    rejects([&]{driver.setTextureRaster(base,0,header+4,0x80000000ull,0x827519D4);},
            "Immediate ITXD bind accepted an interior header identity");
    driver.preflightNullRaster(base,r,0);
    need(service.texture(base,r)==texture,"Immediate ITXD bind replaced its owned texture");
    PPC_STORE_U32(stageCache,0);driver.resetNullTexture(base,0,0x80000000ull,0x82771DB4);
    same(rt,t,metadata,"Immediate ITXD bind changed copied CPU metadata");

    // A predictable final-release rejection occurs before index removal. This
    // calls only preflight: the real group walker normally decrements refs first.
    auto release=entry;release.lr=0x826F81D8;release.r3.u32=t;release.r4.u32=0x82736D50;
    for(uint32_t stageIndex=0;stageIndex<8;++stageIndex){
        const auto slot=0x82D0E3F8+stageIndex*24,oldCache=PPC_LOAD_U32(slot);PPC_STORE_U32(slot,r);
        {ContextScope current(release);rejects([&]{service.preflightRelease(release,base);},"Bound final release accepted");}
        PPC_STORE_U32(slot,oldCache);
        same(rt,t,metadata,"Rejected final release changed original refs/plugin/metadata");
    }
    same(rt,0x82D62FB0,indexRoots,"Rejected final release changed lookup roots");need(service.ownsRaster(r),"Rejected release retired generation");

    if(fixture.name==cacheFixture.name){
        const auto alternate=Simpsons::readFile(fixtureRoot/"hud_target_center.alternate");
        need(alternate.size()==256&&authoredWord==0x01001000,"Authored cache fixture differs");
        // Only fixture-owned input changes: transplant the independently pinned
        // other authored word, then exercise the actual AOT cache-hit branch.
        std::memcpy(rt.pointer(u+0xA8,4,true),alternate.data()+0xA8,4);
    }
    stage="cached group";const auto second=loadAgain(rt,captured);
    PPC_STORE_U32(u+0xA8,authoredWord);
    need(second&&second!=group&&findFont(rt,second)==t&&service.count()==records&&PPC_LOAD_U32(t+0x54)==3,
        "Original second group did not reuse exact copied record/refcount");
    need(service.texture(base,r)==texture,"Original cache hit replaced native texture");
    if(promptTexture)need(service.inputPromptTexture(base,r)==promptTexture,"Cached group changed native input prompt atlas");
    need(PPC_LOAD_U32(t+0xA8)==authoredWord,"Original cache hit overwrote first-owner metadata");
    EngineCpuCalls cpu(captured.finished,base);const auto saved=abi(cpu.registers());
    cpu.invoke(0x826F8168,group);need(abi(cpu.registers())==saved,"Original first group release ABI differs");
    need(service.ownsRaster(r)&&service.texture(base,r)==texture&&PPC_LOAD_U32(t+0x54)==2&&
         PPC_LOAD_U32(t+plugin+0xC)==second&&!PPC_LOAD_U32(t+plugin+0x10),"Primary owner promotion invalidated shared texture");
    need(driver.readbackTextureRaster(r)==linear,"Shared texture data changed after first group release");
    if(promptTexture)need(service.inputPromptTexture(base,r)==promptTexture,"Shared owner release invalidated native input prompt atlas");
    stage="final group";const uint32_t key=PPC_LOAD_U32(t+plugin+4);
    cpu.invoke(0x826F8168,second);need(abi(cpu.registers())==saved,"Original final group release ABI differs");
    need(!service.ownsRaster(r)&&!cpu.invoke(0x826F8520,key),"Original final release retained index/native generation");
    rejects([&]{service.texture(base,r);},"Stale copied raster consumed after original frees");
    PPC_STORE_U32(stageCache,r);
    rejects([&]{driver.setTextureRaster(base,0,header,0x80000000ull,0x827519D4);},
            "Immediate ITXD bind accepted a stale copied raster/header owner");
    PPC_STORE_U32(stageCache,0);
    need(!service.inputPromptTexture(base,r),"Stale copied raster selected native input prompts after original frees");
    if(fixture.name==paletteFixture.name)rejects([&]{service.paletteFromHeader(base,t+0xCC);},"Stale palette header consumed after original frees");
    need(texture->width==fixture.width,"Outstanding owned texture reference lost on original CPU release");
    // The real CPU allocator may reuse addresses. A fresh load gets a fresh
    // native owner even while a client retains the prior GPU generation.
    stage="new generation";const auto third=loadAgain(rt,captured),newT=findFont(rt,third);
    need(newT&&service.ownsRaster(newT+0x78)&&service.texture(base,newT+0x78)!=texture,"Reload reused stale native generation");
    need(driver.readbackTextureRaster(newT+0x78)==linear,"Reloaded original atlas bytes changed");
    if(promptTexture){
        const auto reloadedPrompt=service.inputPromptTexture(base,newT+0x78);
        need(reloadedPrompt&&reloadedPrompt->width==256&&reloadedPrompt->height==256&&
             reloadedPrompt->format==Graphics::TextureFormat::RGBA8,
             "Reloaded original atlas did not resolve native input prompts");
        need(promptTexture->width==256,"Outstanding native prompt atlas reference lost during CPU release");
    }
    cpu.invoke(0x826F8168,third);need(!service.ownsRaster(newT+0x78),"Reloaded group did not retire");
    need(!service.inputPromptTexture(base,newT+0x78),"Reloaded retired raster still selected native input prompts");
}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(exceptionFilter);
    try{
        need(argc==3||(argc==4&&(std::string(argv[3])==sharedFixture.name||std::string(argv[3])==cacheFixture.name||
             std::string(argv[3])==paletteFixture.name||std::string(argv[3])==buttonsFixture.name)),
             "Usage: ITXDLifecycleTests original-image fixture-directory [8_SharedLibrary|hud_target_center|simpsons_palette|buttons]");
        const bool paletteRequested=argc==4&&std::string(argv[3])==paletteFixture.name;
        if(argc==4&&!paletteRequested)fixture=std::string(argv[3])==sharedFixture.name?sharedFixture:
            std::string(argv[3])==buttonsFixture.name?buttonsFixture:cacheFixture;
        Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);
        Capture captured;std::mutex captures;std::unordered_map<DWORD,PPCContext> entries;
        const auto ownerThread=GetCurrentThreadId();bool installed=false,observed=false;
        rt.audioBoundaryObserver=[&](uint32_t pc,PPCContext&,uint8_t*){
            if(pc!=0x828166FC||installed)return;
            rt.engineDriver->itxdTextures().boundaryObserver=[&](uint32_t at,PPCContext& c,uint8_t*){
                std::lock_guard lock(captures);
                if(at==0x826F24D8){entries[GetCurrentThreadId()]=c;return;}
                need(at==0x826F2654,"ITXD fixture observation PC differs");
                if(const auto font=findFont(rt,c.r27.u32)){
                    need(GetCurrentThreadId()==ownerThread,"Font fixture observed on a worker; controlled startup interception needs extension");
                    captured={entries.at(GetCurrentThreadId()),c,c.r27.u32,font};throw FontLoaded{};
                }
            };installed=true;
        };
        try{runOriginal(startup,rt.base);}catch(const FontLoaded&){observed=true;}
        rt.audioBoundaryObserver={};need(installed&&observed,"Actual original font load was not observed");
        rt.engineDriver->itxdTextures().boundaryObserver={};
        uint32_t fixturePayload=0,fixtureStream=0,fixtureRequest=0;std::vector<uint8_t> oldStream,oldRequest;
        if(paletteRequested){
            // Feed the unchanged original loc dictionary through the observed
            // normal CPU loader envelope. This tests copy/relocation/lifetime;
            // it is not a claim that startup naturally loads the level palette.
            fixture=paletteFixture;const auto dictionary=Simpsons::readFile(std::filesystem::path(argv[2])/"loc_split4.itxd");
            fixturePayload=rt.allocatePhysical(0,uint32_t(dictionary.size()),PAGE_READWRITE,0,UINT32_MAX,4096);std::memcpy(rt.pointer(fixturePayload,uint32_t(dictionary.size()),true),dictionary.data(),dictionary.size());
            auto entry=captured.entry;auto* base=rt.base;fixtureRequest=entry.r31.u32;fixtureStream=PPC_LOAD_U32(fixtureRequest+0x10);
            oldStream=snapshot(rt,fixtureStream,0x18);oldRequest=snapshot(rt,fixtureRequest,0x18);
            PPC_STORE_U32(fixtureStream+0xC,0);PPC_STORE_U32(fixtureStream+0x10,uint32_t(dictionary.size()));PPC_STORE_U32(fixtureStream+0x14,fixturePayload);
            PPC_STORE_U32(fixtureRequest+0x14,uint32_t(dictionary.size()));entry.r4.u64=fixturePayload;entry.r5.u64=dictionary.size();
            auto finished=entry;{ContextScope owner(finished);PPCGuestFloatingPointScope floatingPoint(finished.fpscr);PPCSafeIndirect(finished,base,0x826F24D8);}
            const auto group=finished.r3.u32;captured={entry,finished,group,findFont(rt,group)};need(captured.font,"Original loc dictionary loader did not publish the palette");
        }
        stage="original copied texture";run(rt,captured,argv[2]);
        if(fixturePayload){std::memcpy(rt.pointer(fixtureStream,uint32_t(oldStream.size()),true),oldStream.data(),oldStream.size());
            std::memcpy(rt.pointer(fixtureRequest,uint32_t(oldRequest.size()),true),oldRequest.data(),oldRequest.size());rt.freePhysical(fixturePayload);}
        std::printf("PASS original ITXD lifecycle:%zu checks for %s; named load/copies/relocation/native compressed bytes/cache/shared groups/original frees/stale generations; ALL MUTED\n",checks,fixture.name);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL original ITXD lifecycle:%zu checks stage=%s %s\n",checks,stage,e.what());return 1;}
}
