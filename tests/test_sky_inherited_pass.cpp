// Original alpha cleanup followed by an opaque sky draw with its real retained
// blend tuple. Reuse the original sky evidence, fixture and callback wrappers.
#define main OriginalSkyBaselineEntrypoint
#include "test_sky_pass.cpp"
#undef main
#include "effect_fallback_blend_helpers.h"

namespace {
void inheritedRun(Runtime& rt,const PPCContext& entry) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;EngineCpuCalls cpu(entry,base);
    OriginalDrawCatalogCleanup cleanup(rt);evidence(base);originalFallbackBlendEvidence(base);
    rt.map(area,0x10000,true,"original inherited sky blend fixture");
    std::memset(rt.pointer(area,0x10000,true),0,0x10000);
    stage="inherited sky blend original effect and image constructors";
    const auto context=PPC_LOAD_U32(0x82D5DA74),options=cpu.registers().r1.u32+0x60;
    PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    const auto manager=cpu.invoke(0x8269BF70,0x260,options);cpu.invoke(0x826B6F60,manager,context);
    need(!cpu.invoke(0x827019E8,table,25)&&!cpu.invoke(0x827019E8,secondTable,24),"Original inherited sky blend catalogs failed");
    cpu.invoke(0x826B7218,manager);
    const auto typed=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(secondTable+13*16+4));
    const auto shadow=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(table+4*16+4));
    need(typed&&shadow,"Original inherited sky blend typed effect parents absent");
    const auto id=PPC_LOAD_U32(typed+0x1C),wrapper=PPC_LOAD_U32(typed+0x18);
    cache(rt,cpu,id,body);queries(rt,cpu,id,body);const auto immutable=snapshot(rt,source,11712);
    need(!cpu.invoke(0x826FF0F8,context,0)&&driver.builtinTextures().complete(),"Original inherited sky blend image parent failed");
    const auto white=PPC_LOAD_U32(0x82D63004),black=PPC_LOAD_U32(0x82D6300C);
    need(white&&black&&white!=black,"Original inherited sky blend image owners absent");
    for(uint32_t slot=0;slot<2;++slot) {
        const auto camera=PPC_LOAD_U32(shadow+0x5B4+4*slot);PPC_STORE_U32(area+0x80,0x000000FF);
        need(cpu.invoke(0x823F1B80,camera,area+0x80,7)==camera,"Original inherited sky blend shadow clear failed");
        cpu.invoke(0x82707220,shadow,slot);
    }
    const auto viewport=cpu.invoke(0x8269D788);need(viewport,"Original inherited sky blend viewport missing");
    cpu.registers().f1.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12CC));cpu.registers().f2.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D0));
    cpu.registers().f3.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D4));cpu.registers().f4.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D8));
    cpu.invoke(0x8269D608,viewport,0);const auto camera=PPC_LOAD_U32(viewport+0x14),frame=PPC_LOAD_U32(camera+4);
    fixture(rt,camera,typed,shadow,white);
    const auto cached=cpu.invoke(0x82701BD8,elements,PPC_LOAD_U32(geometry+8)),declaration=PPC_LOAD_U32(cached+4);
    need(declaration&&rt.engineAudio->allocationGeneration(declaration,0x50),"Original inherited sky blend declaration has no allocator owner");
    PPC_STORE_U32(geometry+0x30,cached);
    Restore flags(rt,0x82D6CCA8,4),nativeContext(rt,0x82D6D890,4),published(rt,0x82D63028,4),
        materialRoot(rt,0x82D6D814,4),recordEnabled(rt,0x82CF0BE8,4),dirty(rt,0x82D00F80,4),
        propertyBase(rt,0x82D6C0C0,4),view(rt,0x82D0CA70,64),projection(rt,0x82CD1AB0,64),cameraMatrix(rt,frame+0x10,64),
        environment(rt,0x82D6C7F0,4);
    PPC_STORE_U32(0x82D6CCA8,0);PPC_STORE_U32(0x82D6D890,context);PPC_STORE_U32(0x82D63028,0);
    PPC_STORE_U32(0x82D6D814,headers);PPC_STORE_U32(0x82CF0BE8,0);PPC_STORE_U32(0x82D00F80,0);PPC_STORE_U32(0x82D6C0C0,0);
    PPC_STORE_U32(0x82D6C7F0,black);
    need(cpu.invoke(0x823F1A18,camera)==camera,"Original inherited sky blend camera begin failed");
    identity(base,frame+0x10);identity(base,0x82D0CA70);identity(base,0x82CD1AB0);
    const auto geometryBytes=snapshot(rt,geometry,0x700);
    constexpr std::array<std::array<uint32_t,2>,22> states={{{0x28,1},{0x2C,7},{0x30,1},{0x34,0},{0x3C,0},{0x60,0},{0x6C,0},
        {0xAC,0},{0xC0,1},{0xC4,UINT32_MAX},{0xC8,0},{0xCC,0},{0xD0,0},{0xD4,15},{0xE4,0},{0x130,1},
        {0x144,1},{0x148,1},{0x14C,0xFFFF},{0x150,0},{0xD8,0},{0xDC,0}}};
    uint32_t dispatches=0,drawEntries=0,vectors=0,textures=0;
    Observation observed([&](uint32_t pc,PPCContext& c){
        if(pc==0x82740680){++dispatches;need(uint32_t(c.lr)==0x8273B4E0&&c.r3.u32==packet,"Inherited sky blend original dispatcher differs");}
        else if(pc==0x82701220){++drawEntries;need(uint32_t(c.lr)==0x827402B0&&c.r5.u32==typed,"Inherited sky blend original mesh producer differs");}
        else if(pc==0x8270BBC0)++vectors;else if(pc==0x8270BE50)++textures;
    });
    std::vector<uint8_t> reference;
    stage="original inherited sky blend draw all eight Boolean passes";
    for(uint32_t bit1=0;bit1<2;++bit1)for(uint32_t bit2=0;bit2<2;++bit2)for(uint32_t phase=0;phase<2;++phase) {
        const uint32_t alpha=1-phase;
        if(alpha) {
            if(PPC_LOAD_U32(manager+4))cpu.invoke(0x826B4B18,wrapper);
            for(const auto& row:states)driver.directScalar(base,row[0],row[1]);
            cpu.invoke(0x826B7968,PPC_LOAD_U32(0x82E06F80+0x38),1,1);
        } else {
            originalInheritedBlend(driver,"Original alpha cleanup did not supply the inherited sky blend tuple");
        }
        PPC_STORE_U8(packet+12,uint8_t(alpha));PPC_STORE_U32(metadata+8,1|(bit1<<1)|(bit2<<2));
        PPC_STORE_U32(area+0x80,0x00FF00FF);need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original inherited sky blend camera clear failed");
        const auto binding=driver.cameraBinding();
        const auto clear=driver.readbackColor(binding.colorIdentity);
        EngineCpuCalls pass(entry,base);const auto before=fullAbi(pass.registers());pass.invoke(0x8273B4D0,packet);
        need(fullAbi(pass.registers())==before,"Original inherited sky draw damaged nonvolatile ABI");
        need(PPC_LOAD_U32(manager+8)==id&&PPC_LOAD_U32(manager+12)==(alpha?0x7FFFCu:0x3FFFCu),
             "Original inherited sky selected the wrong registered technique");
        originalInheritedBlend(driver,"Original sky dispatch changed inherited enable/factors or expanded cleanup");
        const auto pixels=driver.readbackColor(binding.colorIdentity);
        need(pixel(pixels,640,360)==UINT32_MAX&&pixel(pixels,1100,360)==pixel(clear,1100,360),"Original inherited sky blend quad pixels differ");
        if(reference.empty())reference=pixels;else need(reference==pixels,"Inherited sky blend Boolean passes changed effective quad pixels");
        same(rt,geometry,geometryBytes,"Original inherited sky blend draw mutated its owned geometry/index input");
    }
    need(dispatches==8&&drawEntries==8&&vectors==16&&textures==24,"Original inherited sky blend callback/draw traversal differs");
    cpu.invoke(0x826B4B18,wrapper);need(cpu.invoke(0x823F1A08,camera)==camera,"Original inherited sky blend camera end failed");
    stage="original inherited sky blend declaration and effect retirement";
    cpu.invoke(0x82700A78);
    rejects([&]{rt.engineAudio->allocationGeneration(declaration,0x50);},"Original inherited sky blend declaration retained allocator ownership");
    cleanup.release(rt,cpu,manager);same(rt,source,immutable,"Original inherited sky blend lifecycle changed serialized source");
    std::printf("AUDIT_SKY_BLEND_LIFECYCLE source=%08X opaque_vertex=82036C2C opaque_pixel=820371EC alpha_vertex=82036F08 alpha_pixel=820374E8 blend=07060706 enable=1 expanded=0 original_alpha_to_opaque=passed create=passed use=passed declaration_release=passed fx_release=passed cpu_cache_release=passed stale_use=passed backend_mesh_cache=owner_resident full_gpu_retirement=unproven\n",source);
}
}

int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==2,"Original image required for inherited sky blend lifecycle");
        Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;
        try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original inherited sky blend startup boundary missing");inheritedRun(rt,entry);
        std::printf("PASS original sky inherited blend: %zu checks; original alpha-to-opaque sequences, selected passes, pixels/ABI and original declaration/FX/cache release\n",checks);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original sky inherited blend: %zu checks stage=%s: %s\n",checks,stage,error.what());return 1;}
}
