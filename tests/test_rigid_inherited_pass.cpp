// Whole original rigid alpha-to-opaque sequences with the real retained blend
// tuple, source-specific callbacks and paired declaration/FX/cache cleanup.
#define main OriginalRigidBaselineEntrypoint
#include "test_rigid_dual_pass.cpp"
#undef main
#include "effect_draw_cleanup_helpers.h"
#include "effect_fallback_blend_helpers.h"

extern "C" void __imp__sub_82701220(PPCContext&,uint8_t*);
PPC_FUNC(sub_82701220){forward(0x82701220,ctx,base,__imp__sub_82701220);}

namespace {
void inheritedRun(Runtime& rt,const PPCContext& entry,const char* dictionaryPath) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();EngineCpuCalls cpu(entry,base);
    OriginalDrawCatalogCleanup cleanup(rt);
    stage="original rigid family instruction and binding evidence";originalEvidence(base);originalFallbackBlendEvidence(base);
    rt.map(area,0x10000,true,"original dual dispatcher fixture");std::memset(rt.pointer(area,0x10000,true),0,0x10000);
    stage="real rigid family catalog construction";
    const auto context=PPC_LOAD_U32(0x82D5DA74),options=cpu.registers().r1.u32+0x60;
    PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    const auto manager=cpu.invoke(0x8269BF70,0x260,options);cpu.invoke(0x826B6F60,manager,context);
    need(!cpu.invoke(0x827019E8,table,25)&&!cpu.invoke(0x827019E8,secondTable,24),"Original catalog registration failed");
    cpu.invoke(0x826B7218,manager);
    const auto typed=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(secondTable+family->row*16+4));
    const auto shadow=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(table+4*16+4));
    need(typed&&shadow&&PPC_LOAD_U32(typed)==0x820616C0,"Original dual/shared rigid typed owner missing");
    const auto id=PPC_LOAD_U32(typed+0x1C),wrapper=PPC_LOAD_U32(typed+0x18);
    need(effects.count()==49&&effects.view(id).source==source&&effects.view(id).defaultVectorWords.size()==family->words,
         "Original rigid family source or private bank differs");
    cache(rt,cpu,id,body);queries(rt,cpu,id,body);const auto sourceBytes=snapshot(rt,source,family->bytes);
    stage="original two texture dictionary load";const auto textures=loadTextures(rt,entry,dictionaryPath);
    stage="original world and character shadow copies";
    for(uint32_t slot=0;slot<2;++slot) {
        const auto camera=PPC_LOAD_U32(shadow+0x5B4+4*slot);PPC_STORE_U32(area+0x80,0x000000FF);
        need(cpu.invoke(0x823F1B80,camera,area+0x80,7)==camera,"Original shadow camera clear failed");
        EngineCpuCalls pass(entry,base);const auto saved=fullAbi(pass.registers());pass.invoke(0x82707220,shadow,slot);
        need(fullAbi(pass.registers())==saved,"Whole original empty shadow parent changed nonvolatile ABI");
        need(driver.shadowTextures().view(PPC_LOAD_U32(shadow+0xF0+4*slot)).phase==EngineShadowTextures::Phase::Uploaded,
             "Original parent did not upload its world/character depth copy");
    }
    const auto viewport=cpu.invoke(0x8269D788);need(viewport,"Original viewport manager missing");
    cpu.registers().f1.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12CC));
    cpu.registers().f2.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D0));
    cpu.registers().f3.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D4));
    cpu.registers().f4.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D8));
    cpu.invoke(0x8269D608,viewport,0);const auto camera=PPC_LOAD_U32(viewport+0x14),frame=PPC_LOAD_U32(camera+4);
    fixture(rt,camera,typed,shadow,textures);
    const auto cached=cpu.invoke(0x82701BD8,elements,PPC_LOAD_U32(geometry+8));
    const auto declaration=PPC_LOAD_U32(cached+4),alternate=PPC_LOAD_U32(cached+8);
    need(declaration&&rt.engineAudio->allocationGeneration(declaration,0x50),
         "Original inherited rigid declaration has no allocator owner");
    if(alternate)need(rt.engineAudio->allocationGeneration(alternate,0x50)!=0,
         "Original inherited rigid alternate declaration has no allocator owner");
    PPC_STORE_U32(geometry+0x30,cached);
    Restore flags(rt,0x82D6CCA8,4),nativeContext(rt,0x82D6D890,4),published(rt,0x82D63028,4),
        materialRoot(rt,0x82D6D814,4),recordEnabled(rt,0x82CF0BE8,4),dirty(rt,0x82D00F80,4),
        propertyBase(rt,0x82D6C0C0,4),view(rt,0x82D0CA70,64),projection(rt,0x82CD1AB0,64),cameraMatrix(rt,frame+0x10,64);
    PPC_STORE_U32(0x82D6CCA8,0);PPC_STORE_U32(0x82D6D890,context);PPC_STORE_U32(0x82D63028,0);
    PPC_STORE_U32(0x82D6D814,headers);PPC_STORE_U32(0x82CF0BE8,0);PPC_STORE_U32(0x82D00F80,0);PPC_STORE_U32(0x82D6C0C0,0);
    need(cpu.invoke(0x823F1A18,camera)==camera,"Original main camera begin failed");
    identity(base,frame+0x10);identity(base,0x82D0CA70);identity(base,0x82CD1AB0);
    constexpr std::array<std::array<uint32_t,2>,22> states={{{0x28,1},{0x2C,6},{0x30,1},{0x34,0},{0x3C,0},{0x60,0},{0x6C,0},
        {0xAC,0},{0xC0,1},{0xC4,UINT32_MAX},{0xC8,0},{0xCC,0},{0xD0,0},{0xD4,15},{0xE4,0},{0x130,1},
        {0x144,1},{0x148,1},{0x14C,0xFFFF},{0x150,0},{0xD8,0},{0xDC,0}}};
    const auto resetPassState=[&] {
        // Independent Boolean cases own their baseline. The inherited
        // sequence keeps the preceding original alpha cleanup unchanged.
        for(const auto& row:states)driver.directScalar(base,row[0],row[1]);
        cpu.invoke(0x826B7968,PPC_LOAD_U32(0x82E06F80+0x38),1,1);
    };
    resetPassState();
    const auto bound=driver.cameraBinding();uint32_t dispatcherCalls=0,fallbackCalls=0,vectorCalls=0,textureCalls=0,expectedAlpha=0,expectedBit1=0,meshEntries=0;
    Observation observation([&](uint32_t pc,PPCContext& c) {
        if(pc==0x82740680){++dispatcherCalls;need(uint32_t(c.lr)==0x8273B4E0&&c.r3.u32==packet,"Original crashing dispatcher caller changed");}
        else if(pc==0x827400F8){++fallbackCalls;need(uint32_t(c.lr)==0x82740B28&&c.r3.u32==packet&&
            c.r4.u32==expectedAlpha&&c.r5.u32==expectedBit1&&c.r6.u32==0,"Original dual fallback flags or caller differ");}
        else if(pc==0x82701220){++meshEntries;need(uint32_t(c.lr)==0x827402B0&&c.r5.u32==typed,
            "Original rigid/sky draw producer lost its zero-bone caller or typed owner");}
        else if(pc==0x8270BBC0)++vectorCalls;else if(pc==0x8270BE50)++textureCalls;
    });
    PPC_STORE_U32(area+0x80,0x00FF00FF);need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original main clear failed");
    const auto clearColor=driver.readbackColor(bound.colorIdentity);auto mesh=snapshot(rt,geometry,0x700);
    const auto inputs=materialInputs();
    constexpr std::array<std::array<uint32_t,2>,7> unusedSampler{{{0,7},{4,6},{8,5},
        {0x10,0},{0x14,1},{0x18,0},{0x30,1}}};
    const auto call=[&](uint32_t alpha,uint32_t bit1,bool poisonUnused=false,bool reset=true) {
        expectedAlpha=alpha;expectedBit1=bit1;
        // Only original metadata and the packet's alpha-eligibility byte vary.
        // The real dispatcher, rather than the fixture, supplies r4/r5.
        PPC_STORE_U32(metadata+8,1|(bit1<<1));PPC_STORE_U8(packet+0xC,uint8_t(alpha));
        if(reset)resetPassState();
        else {need(!alpha,"Inherited rigid sequence must select opaque next");
            originalInheritedBlend(driver,"Original alpha cleanup lost its inherited rigid tuple");}
        if(poisonUnused)for(const auto& row:unusedSampler)driver.directSampler(base,1,row[0],row[1]);
        need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original variant clear failed");
        EngineCpuCalls draw(entry,base);const auto saved=fullAbi(draw.registers());draw.invoke(0x8273B4D0,packet);
        need(fullAbi(draw.registers())==saved,"Whole original dual wrapper changed nonvolatile ABI");
        need(effects.privateModifiedMask(id)==std::array<uint8_t,128>{},"Dual dirty bank remains after commit");
        need(!PPC_LOAD_U32(0x82D0CAF8),"Dual draw published a console device");
        need(PPC_LOAD_U32(manager+8)==id&&PPC_LOAD_U32(manager+0xC)==(alpha?0x0007FFFCu:0x0003FFFCu),
             "Original rigid family selected the wrong owner or technique");
        const auto committed=effects.view(id).defaultVectorWords;
        for(uint32_t i=0;i<inputs.size();++i) {
            const auto& input=inputs[i];
            if(input.texture!=noTexture)need(committed[input.word]==textures[input.texture],
                "Original family callback did not store its real texture header");
            else for(uint32_t lane=0;lane<4;++lane)need(committed[input.word+lane]==PPC_LOAD_U32(values+16*i+4*lane),
                "Original family callback did not retain its authored vector lane");
        }
        if(!reset)originalInheritedBlend(driver,"Original opaque rigid dispatcher changed inherited blending");
        const auto pixels=driver.readbackColor(bound.colorIdentity);
        need((!reset||pixel(pixels,640,360)!=pixel(clearColor,640,360))&&pixel(pixels,1100,360)==pixel(clearColor,1100,360),
             "Original dual draw produced no center pixel or escaped geometry bounds");
        same(rt,geometry,mesh,"Dual dispatcher changed immutable input geometry");return pixels;
    };
    stage="whole original family alpha fallback flags 1/0";const auto alpha0=call(1,0);
    stage="whole original crashing family alpha fallback flags 1/1";const auto alpha1=call(1,1);
    need(alpha0==alpha1,"Unused incoming r5 changed original dual alpha pixels");
    if(source==0x820168F8) {
        // Original PS82017E4C has one stage0 fetch. The alpha context
        // leaves stage1 untouched, so its valid inherited state is free to
        // differ from prior character-shadow and mirror profiles.
        std::array<uint32_t,20> beforeSampler;
        for(uint32_t i=0;i<beforeSampler.size();++i)beforeSampler[i]=driver.effectiveState().sampler(1,4*i);
        stage="whole original textured alpha with distinct unused stage1 state";
        need(call(1,1,true)==alpha1,"Unused sampler state changed textured alpha pixels");
        for(const auto& row:unusedSampler)need(driver.effectiveState().sampler(1,row[0])==row[1],
            "Textured alpha mutated its unused inherited sampler state");
        for(uint32_t i=0;i<beforeSampler.size();++i)driver.directSampler(base,1,4*i,beforeSampler[i]);
    }
    stage="whole original family opaque fallback flags 0/1";const auto opaque1=call(0,1);
    stage="whole original family opaque fallback flags 0/0";const auto opaque0=call(0,0);
    need(opaque1==opaque0,"Unused incoming r5 changed original dual opaque pixels");
    stage="original rigid alpha cleanup followed by opaque work without a blend reset";
    (void)call(1,0);
    const auto inherited0=call(0,0,false,false),inherited1=call(0,1,false,false);
    need(inherited0==inherited1,"Unused incoming r5 changed inherited opaque rigid pixels");
    const auto textureCount=uint32_t(std::count_if(inputs.begin(),inputs.end(),[](const auto& input){return input.texture!=noTexture;}));
    const auto vectorCount=uint32_t(inputs.size())-textureCount;
    const uint32_t draws=source==0x820168F8?8u:7u;
    need(dispatcherCalls==draws&&fallbackCalls==draws&&meshEntries==draws&&vectorCalls==draws*vectorCount&&textureCalls==draws*textureCount,
         "Original rigid family material traversal differs");
    screenReplacementRegression(rt,cpu,camera,typed,wrapper,area+0xB0,[&]{(void)call(0,0);});
    same(rt,source,sourceBytes,"Dual regression changed original serialized shader record");
    cpu.invoke(0x826B4B18,wrapper);need(cpu.invoke(0x823F1A08,camera)==camera,"Original main camera end failed");
    stage="original inherited rigid declaration and effect retirement";cpu.invoke(0x82700A78);
    rejects([&]{rt.engineAudio->allocationGeneration(declaration,0x50);},"Original inherited rigid declaration retained ownership");
    if(alternate)rejects([&]{rt.engineAudio->allocationGeneration(alternate,0x50);},"Original inherited rigid alternate retained ownership");
    cleanup.release(rt,cpu,manager);same(rt,source,sourceBytes,"Original inherited rigid cleanup changed serialized source");
    const auto opaqueVS=body+PPC_LOAD_U32(body+family->opaqueContext+0x48)+8;
    const auto opaquePS=body+PPC_LOAD_U32(body+family->opaqueContext+0x4C)+8;
    std::printf("AUDIT_RIGID_BLEND_LIFECYCLE source=%08X opaque_vertex=%08X opaque_pixel=%08X alpha_vertex=%08X alpha_pixel=%08X blend=07060706 enable=1 expanded=0 original_alpha_to_opaque=passed create=passed use=passed declaration_release=passed fx_release=passed cpu_cache_release=passed stale_use=passed backend_mesh_cache=owner_resident full_gpu_retirement=unproven\n",
        source,opaqueVS,opaquePS,family->alphaVertex,family->alphaPixel);
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==4,"Original image, loc_split4 ITXD dictionary and rigid family required");
        const auto choice=std::find_if(families.begin(),families.end(),[&](const auto& candidate){return std::string_view(argv[3])==candidate.name;});
        need(choice!=families.end(),"Unknown original rigid inherited family");family=&*choice;source=family->source;body=source+12;
        Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;
        try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original inherited rigid startup boundary missing");inheritedRun(rt,entry,argv[2]);
        std::printf("PASS original rigid inherited blend %s: %zu checks; original alpha-to-opaque sequence, selected pairs, pixels/callbacks/ABI and original declaration/FX/cache release\n",family->name,checks);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original rigid inherited blend %s: %zu checks stage=%s: %s\n",family->name,checks,stage,error.what());return 1;}
}
