// Whole original public scene dispatcher -> immediate mono world/material/
// mesh path. This is a valid-input frontier regression until that lifetime is
// ported. Observers inspect arguments and always execute original bodies.
#define main OriginalMonoBurpBaselineEntrypoint
#include "test_mono_pass.cpp"
#undef main
#include "effect_draw_cleanup_helpers.h"
#include "runtime/engine_audio.h"
#include <string_view>

extern "C" void __imp__sub_82740680(PPCContext&,uint8_t*);
extern "C" void __imp__sub_827400F8(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8273A878(PPCContext&,uint8_t*);
extern "C" void __imp__sub_82701220(PPCContext&,uint8_t*);

namespace {
void immediateMono(Runtime& rt,const PPCContext& entry,bool alpha) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();EngineCpuCalls cpu(entry,base);
    OriginalDrawCatalogCleanup cleanup(rt);
    constexpr uint32_t second=0x82CD1448,collection=0x68100,material=0x68200;
    stage="original mono immediate producer and catalog construction";
    constexpr std::array<std::array<uint32_t,2>,12> producer={{{0x8273A894,0x7CFF3B78},
        {0x8273A8A0,0x57EA063E},{0x8273A8A4,0x2B0A0000},{0x8273A928,0x419A000C},
        {0x8273A92C,0x809E00A8},{0x8273A934,0x809E00AC},{0x8273A938,0x4BF7B741},
        {0x82740108,0x7C9E2378},{0x82740110,0x7FC7F378},{0x82740130,0x4E800421},
        {0x82740B44,0x38E00000},{0x8273B4DC,0x480051A5}}};
    for(const auto& word:producer)need(PPC_LOAD_U32(word[0])==word[1],"Original mono immediate producer instruction changed");
    const auto context=PPC_LOAD_U32(0x82D5DA74),options=cpu.registers().r1.u32+0x60;
    PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    const auto manager=cpu.invoke(0x8269BF70,0x260,options);cpu.invoke(0x826B6F60,manager,context);
    need(!cpu.invoke(0x827019E8,table,25)&&!cpu.invoke(0x827019E8,second,24),"Original mono paired catalog registration failed");
    cpu.invoke(0x826B7218,manager);
    uint32_t owner=0;
    for(uint32_t row=0;row<25;++row)if(PPC_LOAD_U32(table+16*row)==0x8211F480)
        owner=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(table+16*row+4));
    const auto shadow=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(table+4*16+4));
    need(owner&&shadow&&PPC_LOAD_U32(owner)==0x8215020C,"Original mono/shadow typed owners missing");
    const auto id=PPC_LOAD_U32(owner+0x1C),wrapper=PPC_LOAD_U32(owner+0x18);const auto v=effects.view(id);
    need(v.source==0x8211F480&&v.defaultVectorWords.size()==1092&&PPC_LOAD_U32(owner+0xA8)==0x0007FFFC&&
         PPC_LOAD_U32(owner+0xAC)==0x0003FFFC,"Original mono opaque/alpha handle association differs");
    cache(rt,cpu,id,0x8211F48C);queries(rt,cpu,id,0x8211F48C);const auto source=snapshot(rt,0x8211F480,0x5310);
    const auto viewport=cpu.invoke(0x8269D788);need(viewport,"Original viewport manager missing");
    cpu.registers().f1.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12CC));
    cpu.registers().f2.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D0));
    cpu.registers().f3.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D4));
    cpu.registers().f4.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D8));
    cpu.invoke(0x8269D608,viewport,0);const auto camera=PPC_LOAD_U32(viewport+0x14),frame=PPC_LOAD_U32(camera+4);
    geometryFixture(rt,camera,owner);layout(base,false);
    // Original general dispatcher uses packet+18 for the world effect and
    // packet+1C/+20 for related shadow ownership. Burp uses a different slot.
    PPC_STORE_U32(packet+0x18,owner);PPC_STORE_U32(packet+0x1C,shadow);PPC_STORE_U32(packet+0x20,shadow);
    PPC_STORE_U32(metadata+8,1);PPC_STORE_U32(metadata+0x34,collection);
    PPC_STORE_U32(collection,1);PPC_STORE_U32(collection+12,material);
    // Mono has no material texture/vector leaves. An original material owner
    // is still supplied so the original submesh collection walker executes.
    const auto cached=cpu.invoke(0x82701BD8,elements,PPC_LOAD_U32(geometry+8));
    const auto declaration=PPC_LOAD_U32(cached+4),alternate=PPC_LOAD_U32(cached+8);
    const auto declarationBytes=0x38+12*PPC_LOAD_U32(declaration+0x18);
    need(declaration&&rt.engineAudio->allocationGeneration(declaration,declarationBytes),"Original mono immediate declaration has no owner");
    const auto alternateBytes=alternate?0x38+12*PPC_LOAD_U32(alternate+0x18):0;
    if(alternate)need(rt.engineAudio->allocationGeneration(alternate,alternateBytes),"Original alternate mono declaration has no owner");
    PPC_STORE_U32(geometry+0x30,cached);
    Restore flags(rt,0x82D6CCA8,4),nativeContext(rt,0x82D6D890,4),published(rt,0x82D63028,4),
        materialRoot(rt,0x82D6D814,4),recordEnabled(rt,0x82CF0BE8,4),dirty(rt,0x82D00F80,4),
        propertyBase(rt,0x82D6C0C0,4),view(rt,0x82D0CA70,64),projection(rt,0x82CD1AB0,64),cameraMatrix(rt,frame+0x10,64);
    PPC_STORE_U32(0x82D6CCA8,0);PPC_STORE_U32(0x82D6D890,context);PPC_STORE_U32(0x82D63028,0);
    PPC_STORE_U32(0x82D6D814,materials);PPC_STORE_U32(0x82CF0BE8,0);PPC_STORE_U32(0x82D00F80,0);PPC_STORE_U32(0x82D6C0C0,0);
    need(cpu.invoke(0x823F1A18,camera)==camera,"Original main camera begin failed");
    writeMatrix(base,frame+0x10,translated());writeMatrix(base,0x82D0CA70,translated());writeMatrix(base,0x82CD1AB0,translated());
    constexpr std::array<std::array<uint32_t,2>,22> states={{{0x28,1},{0x2C,6},{0x30,1},{0x34,0},{0x3C,0},{0x60,0},{0x6C,0},
        {0xAC,0},{0xC0,1},{0xC4,UINT32_MAX},{0xC8,0},{0xCC,0},{0xD0,0},{0xD4,15},{0xE4,0},{0x130,1},
        {0x144,1},{0x148,1},{0x14C,0xFFFF},{0x150,0},{0xD8,0},{0xDC,0}}};
    for(const auto& row:states)driver.directScalar(base,row[0],row[1]);
    cpu.invoke(0x826B7968,PPC_LOAD_U32(0x82E06F80+0x38),1,1);
    const auto bound=driver.cameraBinding();PPC_STORE_U32(area+0x80,0x00FF00FF);
    need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original immediate mono clear failed");
    const auto clearColor=driver.readbackColor(bound.colorIdentity),geometryBefore=snapshot(rt,geometry,0x700);
    uint32_t dispatcherCalls=0,immediateCalls=0,worldCalls=0,meshCalls=0;
    Observation observation([&](uint32_t pc,PPCContext& c,uint8_t* memory,bool after) {
        need(memory==base,"Mono immediate observer changed memory domain");if(after)return;
        if(pc==0x82740680){++dispatcherCalls;need(uint32_t(c.lr)==0x8273B4E0&&c.r3.u32==packet,"Whole mono immediate dispatcher caller differs");}
        if(pc==0x827400F8){++immediateCalls;need(uint32_t(c.lr)==0x82740B28&&c.r3.u32==packet&&
            c.r4.u32==uint32_t(alpha)&&!c.r5.u32&&!c.r6.u32,"Original mono immediate flag producer differs");}
        if(pc==0x8273A878){++worldCalls;need(uint32_t(c.lr)==0x82740134&&c.r3.u32==owner&&c.r4.u32==metadata&&
            c.r5.u32==object&&c.r6.u32==camera&&c.r7.u32==uint32_t(alpha),"Original mono immediate world caller differs");}
        if(pc==0x82701220){++meshCalls;need(uint32_t(c.lr)==0x827402B0&&c.r3.u32==metadata&&c.r5.u32==owner,
            "Original mono immediate mesh entry differs");}
    });
    const auto draw=[&]{
        PPC_STORE_U8(packet+0xC,uint8_t(alpha));EngineCpuCalls original(entry,base);seedAbi(original.registers());
        const auto saved=fullAbi(original.registers());original.invoke(0x8273B4D0,packet);
        need(fullAbi(original.registers())==saved,"Whole mono immediate wrapper changed nonvolatile ABI");
    };
    stage=alpha?"whole original immediate mono alpha frontier":"whole original immediate mono opaque frontier";
    const auto draws=effects.monoMeshDrawCount();draw();
    need(dispatcherCalls==1&&immediateCalls==1&&worldCalls==1&&meshCalls==1&&effects.monoMeshDrawCount()==draws+1,
         "Original mono immediate continuation omitted/repeated a draw or caller");
    need(PPC_LOAD_U32(manager+8)==id&&PPC_LOAD_U32(manager+0xC)==(alpha?0x0007FFFCu:0x0003FFFCu),
         "Whole original mono selected wrong technique");
    const auto color=driver.readbackColor(bound.colorIdentity);
    need(pixel(color,640,360)==0xFFFFFFFFu&&pixel(color,1100,360)==pixel(clearColor,1100,360),
         "Original mono immediate white export changed or escaped geometry bounds");
    same(rt,geometry,geometryBefore,"Whole mono immediate changed immutable CPU geometry");
    need(effects.privateModifiedMask(id)==std::array<uint8_t,128>{},"Mono immediate left dirty private storage");
    stage="whole original mono malformed selected handle rejection";
    {Restore selected(rt,owner+(alpha?0xA8:0xAC),4);PPC_STORE_U32(owner+(alpha?0xA8:0xAC),0x0013FFFC);
        rejects([&]{draw();},"Mono immediate admitted an unknown original technique");}
    need(effects.monoMeshDrawCount()==draws+1&&driver.readbackColor(bound.colorIdentity)==color,
         "Malformed mono handle changed pixels or draw count");
    stage="whole original mono malformed selected cache rejection";
    {const auto selectedCache=v.cache+(alpha?24u:0u);Restore header(rt,selectedCache,4);
        PPC_STORE_U32(selectedCache,0x0013FFFC);rejects([&]{draw();},"Mono immediate admitted a changed selected cache");}
    need(effects.monoMeshDrawCount()==draws+1&&driver.readbackColor(bound.colorIdentity)==color,
         "Malformed mono cache changed pixels or draw count");
    cpu.invoke(0x826B4B18,wrapper);need(cpu.invoke(0x823F1A08,camera)==camera,"Original mono immediate camera end failed");
    stage="original mono immediate declaration and paired effect retirement";cpu.invoke(0x82700A78);
    rejects([&]{rt.engineAudio->allocationGeneration(declaration,declarationBytes);},"Retired mono declaration remains owned");
    if(alternate)rejects([&]{rt.engineAudio->allocationGeneration(alternate,alternateBytes);},"Retired alternate mono declaration remains owned");
    cleanup.release(rt,cpu,manager);same(rt,0x8211F480,source,"Mono immediate lifecycle changed serialized source");
    std::printf("AUDIT_MONO_IMMEDIATE_LIFECYCLE source=8211F480 technique=%08X pass=%08X vertex=%08X pixel=%08X original_wrapper=8273B4D0 original_immediate=827400F8 create=passed use=passed declaration_release=passed fx_release=passed cpu_cache_release=passed malformed=passed stale_use=passed backend_mesh_cache=owner_resident full_gpu_retirement=unproven\n",
        alpha?0x0007FFFCu:0x0003FFFCu,alpha?0x0007FFFEu:0x0003FFFEu,
        alpha?0x82121BE8u:0x82120C04u,alpha?0x82122D38u:0x82122BD4u);
}
}
PPC_FUNC(sub_82740680){forward(0x82740680,ctx,base,__imp__sub_82740680);}
PPC_FUNC(sub_827400F8){forward(0x827400F8,ctx,base,__imp__sub_827400F8);}
PPC_FUNC(sub_8273A878){forward(0x8273A878,ctx,base,__imp__sub_8273A878);}
PPC_FUNC(sub_82701220){forward(0x82701220,ctx,base,__imp__sub_82701220);}
#ifndef MONO_IMMEDIATE_NO_MAIN
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==3&&(std::string_view(argv[2])=="alpha"||std::string_view(argv[2])=="opaque"),"Original image and alpha/opaque mode required");
        Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original mono graphics startup missing");
        immediateMono(rt,entry,std::string_view(argv[2])=="alpha");
        std::printf("PASS original mono immediate %s: %zu checks; whole original dispatcher/immediate loop, selected original shaders, pixels/ABI, malformed handle and original declaration/FX/cache release\n",argv[2],checks);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original mono immediate:%zu checks stage=%s:%s\n",checks,stage,error.what());return 1;}
}
#endif
