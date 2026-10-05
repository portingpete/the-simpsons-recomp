// Execute the original three-effect chain after an original-loader fixture
// supplies the unchanged level palette. This is a CPU/GPU contract test, not
// a claim that normal startup loads this level asset or reaches a game menu.
#include "effect_catalog_lifecycle_helpers.h"
#include "runtime/engine_scene_copies.h"
#include "runtime/engine_itxd_textures.h"
#include "runtime/engine_viewport_surfaces.h"
#include <bit>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace {
struct Loaded {};
uint32_t named(Runtime& rt,uint32_t group,const char* name) {
    auto* base=rt.base;std::unordered_set<uint32_t> seen;
    for(uint32_t p=PPC_LOAD_U32(group+4);p;p=PPC_LOAD_U32(p+4)) {
        need(seen.insert(p).second,"Original group cycle");const auto t=PPC_LOAD_U32(p);
        if(stringAt(rt,t+0x10)==name)return t;
    }
    return 0;
}
void run(Runtime& rt,const PPCContext& startup,const PPCContext& loader,const std::filesystem::path& root) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& fx=driver.effects();
    const auto dictionary=readFile(root/"loc_split4.itxd");
    const auto payload=rt.allocatePhysical(0,uint32_t(dictionary.size()),PAGE_READWRITE,0,UINT32_MAX,4096);
    std::memcpy(rt.pointer(payload,uint32_t(dictionary.size()),true),dictionary.data(),dictionary.size());
    const auto request=loader.r31.u32,stream=PPC_LOAD_U32(request+0x10);
    const auto oldStream=snapshot(rt,stream,0x18),oldRequest=snapshot(rt,request,0x18);
    PPC_STORE_U32(stream+0xC,0);PPC_STORE_U32(stream+0x10,uint32_t(dictionary.size()));PPC_STORE_U32(stream+0x14,payload);
    PPC_STORE_U32(request+0x14,uint32_t(dictionary.size()));
    auto loading=loader;loading.r4.u64=payload;loading.r5.u64=dictionary.size();
    const auto beforeLoad=abi(loading);auto* previous=currentContext;currentContext=&loading;
    {PPCGuestFloatingPointScope fp(loading.fpscr);PPCSafeIndirect(loading,base,0x826F24D8);}
    currentContext=previous;need(abi(loading)==beforeLoad,"Palette loader ABI changed");
    const auto group=loading.r3.u32,palette=named(rt,group,"simpsons_palette");need(palette,"Palette not published by original loader");
    std::memcpy(rt.pointer(stream,0x18,true),oldStream.data(),0x18);std::memcpy(rt.pointer(request,0x18,true),oldRequest.data(),0x18);rt.freePhysical(payload);
    EngineCpuCalls cpu(startup,base);
    need(cpu.invoke(0x823C7140)==palette+0xCC,"Original palette name lookup/header differs");
    const auto paletteBytes=readFile(root/"simpsons_palette.rgba");
    need(driver.readbackTextureRaster(palette+0x78)==paletteBytes,"Native palette differs from independent asset fixture");
    const auto manager=PPC_LOAD_U32(0x82D08BFC),typed=cpu.invoke(0x826B7088,manager,0x820062FC);
    need(typed,"Original edgeAA lookup failed");const auto id=PPC_LOAD_U32(typed+0x1C);const auto metadata=fx.view(id);
    need(metadata.source==0x82034008 && metadata.defaultVectorWords.size()==188 && fx.typedReflectionCount(id)==1,"Original edgeAA reflection differs");
    const auto original=metadata.defaultVectorWords;const auto asset=snapshot(rt,0x82034008,9280);
    std::vector<uint32_t> scalars,samplers;
    for(const auto& row:metadata.scalars)scalars.push_back(cpu.invoke(0x826B7940,PPC_LOAD_U32(0x82E06F80+row.sdkId)));
    for(const auto& row:metadata.samplers)samplers.push_back(cpu.invoke(0x826B79B0,row.stage,PPC_LOAD_U32(0x82E07118+row.sdkId)));
    const auto camera=PPC_LOAD_U32(0x82E07248),colorCopy=cpu.invoke(0x823ED9B8),depthCopy=cpu.invoke(0x823ED9C8);
    cpu.invoke(0x82751510,0); // Original full-size viewport header selection.
    rejects([&]{driver.sampledColorCopy(colorCopy,camera);},"Uncopied shared color accepted");
    rejects([&]{driver.sampledDepthCopy(depthCopy,camera);},"Uncopied shared depth accepted");
    rt.map(0x50000,4096,true,"edgeAA independent clear fixture");PPC_STORE_U32(0x50000,0);
    // Startup keeps post-processing disabled until a level has loaded. The
    // fixture selects the original enabled branch with real catalog/assets.
    const auto mode=PPC_LOAD_U8(0x82CD1430),enabled=PPC_LOAD_U8(0x82CD1424);
    PPC_STORE_U8(0x82CD1430,1);PPC_STORE_U8(0x82CD1424,1);
    for(uint32_t cycle=0;cycle<2;++cycle) {
        cpu.invoke(0x823EE940,camera,0x50000,7);cpu.invoke(0x823F1A18,camera);
        PPC_STORE_U32(0x82CD1438,cycle?0x40000000:0x3FC00000);
        PPC_STORE_U32(0x82CD1434,cycle?0x3F400000:0x3F800000);
        PPC_STORE_U32(0x82CD1444,cycle?0x3F000000:0x430C0000);
        const auto depthBefore=driver.readbackDepth(driver.cameraBinding().depthIdentity);
        const auto prior=abi(cpu.registers());const auto edge=fx.edgeDrawCount(),aa=fx.aaDrawCount(),composite=fx.edgeAADrawCount();
        need(cpu.invoke(0x823C7500,0,camera)==1,"Original three-effect parent did not complete");
        need(abi(cpu.registers())==prior && !fx.activeEdge() && fx.edgeDrawCount()==edge+1 && fx.aaDrawCount()==aa+1 && fx.edgeAADrawCount()==composite+1,
             "Original three-effect chain ABI/ownership/draw count differs");
        auto expected=original;
        const std::array<std::pair<uint32_t,uint32_t>,11> inputs={{{368,0x82CD142C},{384,0x82CD1438},{400,0x82CD1434},
            {432,0x82CD1444},{464,0x82CD143C},{512,0x82D098C0},{528,0x82D098B4},{544,0x82D098C4},{448,0x82D098F4},
            {624,0x82D09894},{688,0x82D6C7F0}}};
        for(const auto& [offset,global]:inputs)expected[offset/4]=PPC_LOAD_U32(global);
        expected[240/4]=colorCopy;expected[304/4]=depthCopy;expected[560/4]=palette+0xCC;
        expected[416/4]=0x41200000;expected[480/4]=0x44A00000;expected[496/4]=0x44340000;
        const auto actual=fx.view(id).defaultVectorWords;
        for(size_t i=0;i<expected.size();++i)if(actual[i]!=expected[i]){
            std::fprintf(stderr,"edgeAA word%zu actual=%08X expected=%08X\n",i,actual[i],expected[i]);need(false,"Original parameter value/untouched lane differs");}
        need(actual==expected,"Original edgeAA parameters differ");same(rt,0x82034008,asset,"Original edgeAA asset changed");
        const auto dirty=fx.privateModifiedMask(id);const auto poolDirty=snapshot(rt,metadata.pool,128);
        need(std::all_of(dirty.begin(),dirty.end(),[](auto x){return x==0;}) && std::all_of(poolDirty.begin(),poolDirty.end(),[](auto x){return x==0;}),"Original dirty cache lines did not clear");
        size_t i=0;for(const auto& row:metadata.scalars)need(cpu.invoke(0x826B7940,PPC_LOAD_U32(0x82E06F80+row.sdkId))==scalars[i++],"Original scalar state not restored");
        i=0;for(const auto& row:metadata.samplers)need(cpu.invoke(0x826B79B0,row.stage,PPC_LOAD_U32(0x82E07118+row.sdkId))==samplers[i++],"Original five-sampler state not restored");
        const auto pixels=driver.readbackColor(driver.cameraBinding().colorIdentity);
        // Zero packed base selects the black palette entry at (0,0); uniform
        // input has no edge. Its authored no-edge alpha is one.
        for(size_t p=0;p<pixels.size();p+=4)need(pixels[p]==0 && pixels[p+1]==0 && pixels[p+2]==0 && pixels[p+3]==0xC0,"Original chain black-palette output differs");
        need(depthBefore==driver.readbackDepth(driver.cameraBinding().depthIdentity),"Original chain changed camera depth/stencil");
        const auto copyAbi=abi(cpu.registers());const auto copyCount=driver.viewportSurfaces().depthCopyCount();
        cpu.invoke(0x82751700,camera);
        need(abi(cpu.registers())==copyAbi && driver.viewportSurfaces().depthCopyCount()==copyCount+1,"Original post depth-copy caller ABI/count differs");
        need(driver.viewportSurfaces().readbackDepthTexture(0x82DFE840)==depthBefore &&
             driver.readbackDepth(driver.cameraBinding().depthIdentity)==depthBefore && driver.readbackColor(driver.cameraBinding().colorIdentity)==pixels,
             "Post depth-copy destination, source or color changed unexpectedly");
        {
            auto bad=cpu.registers();bad.r1.u32-=0x80;bad.lastFunction=0x82751700;bad.r3.u64=0;bad.r4.u64=0x14;bad.r5.u64=0;
            bad.r6.u64=0x82DFE840;bad.r7.u64=bad.r8.u64=bad.r9.u64=bad.r10.u64=0;bad.f1.f64=0;
            auto* priorContext=currentContext;currentContext=&bad;
            bad.r4.u64=0x114;rejects([&]{driver.viewportSurfaces().copyDepth(bad,base);},"Post copy accepted a clear-enable flag");bad.r4.u64=0x14;
            bad.r6.u64=0x82DFE874;rejects([&]{driver.viewportSurfaces().copyDepth(bad,base);},"Post copy accepted a split-screen header");bad.r6.u64=0x82DFE840;
            bad.lastFunction=0x82751704;rejects([&]{driver.viewportSurfaces().copyDepth(bad,base);},"Post copy accepted a foreign caller");
            currentContext=priorContext;
            need(driver.viewportSurfaces().depthCopyCount()==copyCount+1 && driver.viewportSurfaces().readbackDepthTexture(0x82DFE840)==depthBefore,
                 "Rejected post depth-copy request changed work or pixels");
        }
        need(driver.sampledColorCopy(colorCopy,camera)!=nullptr && driver.sampledDepthCopy(depthCopy,camera)!=nullptr,"Copied resources lack sampling provenance");
        rejects([&]{driver.sampledColorCopy(depthCopy,camera);},"Depth identity accepted as sampled color");
        rejects([&]{driver.sampledDepthCopy(depthCopy,camera+4);},"Wrong sampled camera accepted");
        cpu.invoke(0x823F1A08,camera);
    }
    PPC_STORE_U8(0x82CD1430,mode);PPC_STORE_U8(0x82CD1424,enabled);cpu.invoke(0x823C70C0);driver.sceneCopies().requireReleased();
    cpu.invoke(0x826F8168,group);rejects([&]{driver.itxdTextures().paletteFromHeader(base,palette+0xCC);},"Released palette header accepted");
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(exceptionFilter);
    try {
        need(argc==3,"Original image and palette fixture directory required");Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        PPCContext loader{};std::mutex mutex;std::unordered_map<DWORD,PPCContext> entries;const auto ownerThread=GetCurrentThreadId();bool installed=false,observed=false;
        rt.audioBoundaryObserver=[&](uint32_t pc,PPCContext&,uint8_t*){
            if(pc!=0x828166FC || installed)return;
            rt.engineDriver->itxdTextures().boundaryObserver=[&](uint32_t at,PPCContext& c,uint8_t*){
                std::lock_guard lock(mutex);
                if(at==0x826F24D8){entries[GetCurrentThreadId()]=c;return;}
                need(at==0x826F2654,"Unexpected ITXD observation");
                if(named(rt,c.r27.u32,"HighlanderStdBold6060b")) {
                    need(GetCurrentThreadId()==ownerThread,"Font loaded on unqualified worker");loader=entries.at(ownerThread);throw Loaded{};
                }
            };installed=true;
        };
        try{runOriginal(startup,rt.base);}catch(const Loaded&){observed=true;}
        rt.audioBoundaryObserver={};need(installed && observed,"Original font-loader checkpoint missing");rt.engineDriver->itxdTextures().boundaryObserver={};
        stage="original edgeAA chain";run(rt,entry,loader,argv[2]);
        std::printf("PASS %zu original edge/AA/edgeAA chain, parameter, palette, state, pixel and lifetime checks; ALL MUTED\n",checks);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL edgeAA parameters after%zu checks: %s\n",checks,e.what());return 1;}
}
