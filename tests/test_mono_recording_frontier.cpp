// Genuine original dispatcher-selected mono recording frontier. There is no
// inferred alpha flag: record eligibility preserves packet+0C and requires
// the real producer to choose opaque. Observers always forward original code.
#define main OriginalMonoBurpRecordingBaselineEntrypoint
#include "test_mono_pass.cpp"
#undef main
#include "effect_draw_cleanup_helpers.h"
#include "runtime/engine_audio.h"
#include "runtime/engine_recording.h"
#include <string_view>
extern "C" void __imp__sub_82740680(PPCContext&,uint8_t*);
extern "C" void __imp__sub_82740420(PPCContext&,uint8_t*);
extern "C" void __imp__sub_827402F0(PPCContext&,uint8_t*);
extern "C" void __imp__sub_82701448(PPCContext&,uint8_t*);
extern "C" void __imp__sub_826FF6D8(PPCContext&,uint8_t*);
namespace {
void recordingMonoFrontier(Runtime& rt,const PPCContext& entry,bool bit2) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();EngineCpuCalls cpu(entry,base);
    OriginalDrawCatalogCleanup cleanup(rt);
    constexpr uint32_t second=0x82CD1448,collection=0x68100,material=0x68200;
    stage="original mono recording producer and catalog construction";
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
    const auto source=snapshot(rt,0x8211F480,0x5310);
    need(v.source==0x8211F480&&v.defaultVectorWords.size()==1092&&PPC_LOAD_U32(owner+0xA8)==0x0007FFFC&&
         PPC_LOAD_U32(owner+0xAC)==0x0003FFFC,"Original mono opaque/alpha handle association differs");
    cache(rt,cpu,id,0x8211F48C);queries(rt,cpu,id,0x8211F48C);
    const auto viewport=cpu.invoke(0x8269D788);need(viewport,"Original viewport manager missing");
    cpu.registers().f1.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12CC));
    cpu.registers().f2.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D0));
    cpu.registers().f3.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D4));
    cpu.registers().f4.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D8));
    cpu.invoke(0x8269D608,viewport,0);const auto camera=PPC_LOAD_U32(viewport+0x14),frame=PPC_LOAD_U32(camera+4);
    geometryFixture(rt,camera,owner);layout(base,false);
    stage="original empty shadow parents establish recording scissor";
    for(uint32_t slot=0;slot<2;++slot) {
        const auto shadowCamera=PPC_LOAD_U32(shadow+0x5B4+4*slot);PPC_STORE_U32(area+0x80,0x000000FF);
        need(cpu.invoke(0x823F1B80,shadowCamera,area+0x80,7)==shadowCamera,"Original mono recording shadow clear failed");
        EngineCpuCalls pass(entry,base);const auto saved=fullAbi(pass.registers());pass.invoke(0x82707220,shadow,slot);
        need(fullAbi(pass.registers())==saved,"Original empty shadow parent changed mono recording ABI");
    }
    // Original general dispatcher uses packet+18 for the world effect and
    // packet+1C/+20 for related shadow ownership. Burp uses a different slot.
    PPC_STORE_U32(packet+0x18,owner);PPC_STORE_U32(packet+0x1C,shadow);PPC_STORE_U32(packet+0x20,shadow);
    PPC_STORE_U32(metadata+8,bit2?4u:0u);PPC_STORE_U32(metadata+0x34,collection);
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
    PPC_STORE_U32(0x82D6CCA8,0);PPC_STORE_U32(0x82D6D890,context);
    PPC_STORE_U32(0x82D6D814,materials);PPC_STORE_U32(0x82CF0BE8,0);PPC_STORE_U8(0x82CF0BE8,1);PPC_STORE_U32(0x82D00F80,0);PPC_STORE_U32(0x82D6C0C0,0);
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
    stage="genuine original mono recording singleton construction";
    auto& owners=driver.recordingOwners();auto recording=PPC_LOAD_U32(0x82D09784);
    // The original startup may already have reached823B7488->826F4988 before
    // this graphics observation boundary. Use that genuine registered owner,
    // or run the same original allocator/constructor if startup has not yet
    // constructed it. Never overwrite a live singleton with a fixture object.
    if(!recording) {
        recording=cpu.invoke(0x8269BD70,0x7C);cpu.registers().lr=0x823B748C;
        need(cpu.invoke(0x826F4988,recording)==recording,"Original mono recording singleton constructor failed");
    }
    need(owners.count()==1&&PPC_LOAD_U32(recording+0x18)==2000&&!PPC_LOAD_U32(recording+0x1C),
         "Original mono recording singleton is absent or already owns cached work");
    const auto recordingIdentity=owners.identity(recording);
    const auto nativeLease=owners.nativeContext(recording,recordingIdentity);
    owners.validateOwner(recording,recordingIdentity);
    const auto recordingPool=PPC_LOAD_U32(recording+0x3C);
    constexpr uint32_t recordingPoolBytes=2000*0x34+4+0x13;
    const auto deleteManager=[&](const PPCContext& origin) {
        EngineCpuCalls original(origin,base);original.registers().lr=0x823B748C;
        return original.invoke(0x826F4AE0,recording,1);
    };
    const auto rejectManagerDeletion=[&](const PPCContext& origin,const char* message) {
        const auto ownerBefore=snapshot(rt,recording,0x7C),poolBefore=snapshot(rt,recordingPool,recordingPoolBytes);
        const std::array<uint32_t,2> aliasesBefore={PPC_LOAD_U32(0x82D6D890),PPC_LOAD_U32(0x82D63028)};
        const auto saved=fullAbi(origin);rejects([&]{deleteManager(origin);},message);
        same(rt,recording,ownerBefore,"Rejected mono manager deletion changed CPU owner fields");
        same(rt,recordingPool,poolBefore,"Rejected mono manager deletion changed CPU pool ownership");
        need(fullAbi(origin)==saved&&owners.count()==1&&!nativeLease.expired()&&owners.identity(recording)==recordingIdentity,
            "Rejected mono manager deletion changed its caller or native context ownership");
        need(aliasesBefore==std::array<uint32_t,2>{PPC_LOAD_U32(0x82D6D890),PPC_LOAD_U32(0x82D63028)},
            "Rejected mono manager deletion changed context aliases");
    };
    PPC_STORE_U8(packet+12,1); // Original recordable-packet eligibility.
    uint32_t dispatcherCalls=0,recordingCalls=0,replayCalls=0,publicationCalls=0;
    bool sessionDeletionRejected=false,publicationNegativesDone=false;
    Observation observed([&](uint32_t pc,PPCContext& c,uint8_t* memory,bool after){
        need(memory==base,"Mono recording observer changed memory domain");
        if(pc==0x826FF6D8&&uint32_t(c.lr)==0x827406A0) {
            if(after){need(!c.r3.u32&&PPC_LOAD_U32(0x82D63028)==context,
                "Whole original scene context setter did not publish its live device owner");++publicationCalls;
                if(!publicationNegativesDone) {
                    const PPCContext returned=c;
                    const auto badPublication=[&](const std::function<void(PPCContext&)>& corrupt) {
                        c=returned;c.r3.u64=context;corrupt(c);const auto saved=fullAbi(c);
                        const auto before=snapshot(rt,recording,0x7C);
                        rejects([&]{owners.observeIdleContextPublication(c,base);},"Malformed idle publication was admitted");
                        same(rt,recording,before,"Rejected idle publication changed CPU recording ownership");
                        need(fullAbi(c)==saved&&owners.count()==1&&!nativeLease.expired(),
                            "Rejected idle publication changed caller/native ownership");c=returned;
                    };
                    badPublication([](PPCContext& wrong){wrong.lr=0x827406A4;});
                    badPublication([](PPCContext& wrong){wrong.lastFunction=0x82740680;});
                    badPublication([](PPCContext& wrong){wrong.r1.u32=1;});
                    badPublication([&](PPCContext& wrong){wrong.r3.u64=context+1;});
                    {Restore wrongFlags(rt,0x82D6CCA8,4);PPC_STORE_U32(0x82D6CCA8,1);
                        badPublication([](PPCContext&){});}
                    {Restore wrongCaller(rt,c.r1.u32+0xE8,4);PPC_STORE_U32(c.r1.u32+0xE8,0x8273B4E4);
                        badPublication([](PPCContext&){});}
                    c=returned;c.r3.u64=context;
                    {PPCContext foreign=c;rejects([&]{owners.observeIdleContextPublication(foreign,base);},
                        "Idle publication admitted a foreign CPU context");}
                    rejects([&]{owners.observeIdleContextPublication(c,base+0x1000);},
                        "Idle publication admitted a foreign address space");c=returned;
                    publicationNegativesDone=true;
                }
            }
            else need(c.r3.u32==context,"Original scene context setter argument lost its qualified native owner");
            return;
        }
        if(after)return;
        if(pc==0x82740680){++dispatcherCalls;need(uint32_t(c.lr)==0x8273B4E0&&c.r3.u32==packet,
            "Whole mono recording dispatcher caller differs");}
        if(pc==0x82740420){++recordingCalls;need(uint32_t(c.lr)==0x82740A60&&c.r3.u32==packet&&
            !c.r4.u32&&!c.r5.u32&&c.r6.u32==uint32_t(bit2)&&c.r7.u32==PPC_LOAD_U32(packet+0x10)&&
            PPC_LOAD_U8(packet+0xC)==1,"Original mono recording producer arguments differ");
            std::printf("AUDIT_MONO_RECORDING_FRONTIER source=8211F480 original_wrapper=8273B4D0 dispatcher=82740680 recording=82740420 caller=82740A60 alpha=0 flag2=%u bucket=%u create=passed recording_entry=encountered use=unproven release=unproven\n",
                c.r6.u32,c.r7.u32);}
        if(pc==0x827402F0){++replayCalls;need(uint32_t(c.lr)==0x82740AFC&&c.r3.u32==packet,
            "Original mono cached replay caller differs");}
        if(pc==0x82701448) {
            using S=Graphics::ScalarState;const auto& effective=driver.effectiveState();
            std::printf("AUDIT_MONO_RECORDING_STATE source=8211F480 caller=%08X flag2=%u halfpixel=%u msaa=%u sample_mask=%08X scissor_enable=%u primitive_reset=%u color_mask=%u\n",
                uint32_t(c.lr),unsigned(bit2),effective.scalar(S::HalfPixelOffset),effective.scalar(S::MultisampleAntialias),
                effective.scalar(S::MultisampleMask),effective.scalar(S::ScissorEnable),effective.scalar(S::PrimitiveResetEnable),effective.scalar(S::ColorMask0));
            // The real recording builder has already borrowed its context and
            // pool slot. A nested original deleting wrapper must reject before
            // touching that active session, then the original mesh loop runs.
            rejectManagerDeletion(c,"Original mono manager deletion admitted an active recording session");
            sessionDeletionRejected=true;
        }
    });
    stage=bit2?"whole original mono recording bit2 frontier":"whole original mono recording default frontier";
    const auto call=[&]{
        EngineCpuCalls original(entry,base);seedAbi(original.registers());const auto saved=fullAbi(original.registers());
        original.invoke(0x8273B4D0,packet);need(fullAbi(original.registers())==saved,"Whole mono recording wrapper changed its ABI");
    };
    call();
    const auto plugin=PPC_LOAD_U32(0x82D6D850),node=PPC_LOAD_U32(object+plugin+4),payload=PPC_LOAD_U32(node+0x28);
    need(publicationCalls==1&&publicationNegativesDone&&sessionDeletionRejected&&dispatcherCalls==1&&recordingCalls==1&&replayCalls==1&&node&&PPC_LOAD_U32(node+0x1C)==metadata&&
         PPC_LOAD_U32(node+0x2C)==2&&PPC_LOAD_U32(recording+0x18)==1999&&PPC_LOAD_U32(recording+0x1C)==1,
         "Whole mono recording omitted original cache publication or first replay");
    owners.requireReplay(packet,payload);
    const auto first=driver.readbackColor(bound.colorIdentity);
    need(pixel(first,640,360)==0xFFFFFFFF&&pixel(first,1100,360)==pixel(clearColor,1100,360),
         "Recorded mono white export changed or escaped geometry bounds");
    stage="whole original mono cached replay";
    need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original mono replay clear failed");call();
    need(recordingCalls==1&&replayCalls==2&&PPC_LOAD_U32(object+plugin+4)==node&&PPC_LOAD_U32(node+0x28)==payload&&
         driver.readbackColor(bound.colorIdentity)==first,"Mono cache hit rerecorded work or changed pixels");
    stage="cached original mono matrix inheritance";
    writeMatrix(base,objectFrame+0x10,translated(.5f));
    need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original moved mono clear failed");call();
    const auto moved=driver.readbackColor(bound.colorIdentity);
    need(recordingCalls==1&&replayCalls==3&&PPC_LOAD_U32(node+0x28)==payload&&
         pixel(moved,640,360)==pixel(clearColor,640,360)&&pixel(moved,960,360)==0xFFFFFFFF,
         "Mono cached replay omitted the real original matrix callback");
    stage="original mono malformed selected handle and cached payload";
    {Restore selected(rt,owner+0xAC,4);PPC_STORE_U32(owner+0xAC,0x0013FFFC);
        rejects([&]{call();},"Mono recording admitted a malformed original selected handle");}
    {Restore cachedPayload(rt,node+0x28,4);PPC_STORE_U32(node+0x28,payload+1);
        rejects([&]{call();},"Mono replay admitted a foreign cached payload identity");}
    need(driver.readbackColor(bound.colorIdentity)==moved,"Malformed mono recording changed pixels");
    same(rt,geometry,geometryBefore,"Mono recording changed immutable CPU geometry");
    stage="original mono manager rejects live cached payload retirement";
    rejectManagerDeletion(entry,"Original mono manager deletion admitted retained cached payload ownership");
    owners.requireReplay(packet,payload);
    // Use the real registered atomic plugin registry and its complete original
    // destructor walk. The fixture's skeleton pointers are borrowed synthetic
    // inputs for the unrelated skin route, and are not live in this static case.
    stage="original mono cache plugin retirement";
    PPC_STORE_U32(object+PPC_LOAD_U32(0x82E27994),0);
    need(cpu.invoke(0x823FB3A8,0x82CD1678,object)==0x82CD1678,"Original atomic plugin walker did not complete");
    need(!PPC_LOAD_U32(object+plugin+4)&&PPC_LOAD_U32(recording+0x18)==2000&&!PPC_LOAD_U32(recording+0x1C)&&
         !PPC_LOAD_U32(recording+0x44)&&!PPC_LOAD_U32(recording+0x48)&&!PPC_LOAD_U32(recording+0x4C),
         "Original mono cache retirement retained CPU list/accounting ownership");
    rejects([&]{owners.requireReplay(packet,payload);},"Retired mono cached payload remained usable");
    cpu.invoke(0x826B4B18,wrapper);need(cpu.invoke(0x823F1A08,camera)==camera,"Original mono recording camera end failed");
    stage="original mono manager malformed retired history and pool partition";
    for(const auto offset:{0x50u,0x54u,0x58u,0x5Cu,0x60u,0x64u,0x6Cu,0x70u,0x74u,0x78u,
                          0x44u,0x48u,0x4Cu,0x18u,0x1Cu}) {
        Restore corrupt(rt,recording+offset,4);PPC_STORE_U32(recording+offset,PPC_LOAD_U32(recording+offset)^1u);
        rejectManagerDeletion(entry,"Original mono manager deletion admitted corrupted retired history/accounting/pool partition");
    }
    owners.validateOwner(recording,recordingIdentity);owners.validateFrame(base);
    stage="original mono manager rejects foreign or reverted idle context aliases";
    for(const auto address:{0x82D6D890u,0x82D63028u})for(const auto value:{0u,recordingIdentity,recordingIdentity+1u,context+1u}) {
        Restore corrupt(rt,address,4);PPC_STORE_U32(address,value);
        rejectManagerDeletion(entry,"Original mono manager deletion admitted foreign/reverted idle context aliases");
    }
    stage="original mono recording manager retirement";
    std::printf("AUDIT_MONO_RECORDING_RETIRE_STATE source=8211F480 flag2=%u free=%u allocated=%u lru_first=%08X lru_last=%08X recorded_bytes=%u success_count=%u history54=%08X history58=%08X history5c=%08X history60=%08X history64=%08X history6c=%08X history70=%08X history74=%08X history78=%08X use=passed payload_release=passed recording_context_release=unproven\n",
        unsigned(bit2),PPC_LOAD_U32(recording+0x18),PPC_LOAD_U32(recording+0x1C),PPC_LOAD_U32(recording+0x44),
        PPC_LOAD_U32(recording+0x48),PPC_LOAD_U32(recording+0x4C),PPC_LOAD_U32(recording+0x50),
        PPC_LOAD_U32(recording+0x54),PPC_LOAD_U32(recording+0x58),PPC_LOAD_U32(recording+0x5C),PPC_LOAD_U32(recording+0x60),
        PPC_LOAD_U32(recording+0x64),PPC_LOAD_U32(recording+0x6C),PPC_LOAD_U32(recording+0x70),PPC_LOAD_U32(recording+0x74),PPC_LOAD_U32(recording+0x78));
    cpu.registers().lr=0x823B748C;need(cpu.invoke(0x826F4AE0,recording,1)==recording,"Original mono recording manager deletion failed");
    need(nativeLease.expired()&&!owners.count()&&!PPC_LOAD_U32(0x82D09784),"Original mono recording retained its native context");
    rejects([&]{owners.validateOwner(recording,recordingIdentity);},"Retired mono recording manager identity remained usable");
    cpu.invoke(0x8268F470,recording+4,0x82D61DD0); // Original fixture unsubscribe, as in the owner contract fixture.
    stage="original mono declaration and paired effect retirement";cpu.invoke(0x82700A78);
    rejects([&]{rt.engineAudio->allocationGeneration(declaration,declarationBytes);},"Retired mono declaration remains owned");
    if(alternate)rejects([&]{rt.engineAudio->allocationGeneration(alternate,alternateBytes);},"Retired alternate mono declaration remains owned");
    cleanup.release(rt,cpu,manager);same(rt,0x8211F480,source,"Mono recording changed serialized source");
    std::printf("AUDIT_MONO_RECORDING_LIFECYCLE source=8211F480 technique=0003FFFC pass=0003FFFE vertex=82120C04 pixel=82122BD4 original_wrapper=8273B4D0 original_recording=82740420 original_context_setter=826FF6D8 context_store=826FF6F0 flag2=%u create=passed use=passed replay=passed inherited_matrix=passed idle_publication=passed payload_release=passed recording_context_release=passed declaration_release=passed fx_release=passed cpu_cache_release=passed malformed=passed stale_use=passed backend_mesh_cache=owner_resident full_gpu_retirement=unproven\n",unsigned(bit2));
}
}
PPC_FUNC(sub_82740680){forward(0x82740680,ctx,base,__imp__sub_82740680);}
PPC_FUNC(sub_82740420){forward(0x82740420,ctx,base,__imp__sub_82740420);}
PPC_FUNC(sub_827402F0){forward(0x827402F0,ctx,base,__imp__sub_827402F0);}
PPC_FUNC(sub_82701448){forward(0x82701448,ctx,base,__imp__sub_82701448);}
PPC_FUNC(sub_826FF6D8){forward(0x826FF6D8,ctx,base,__imp__sub_826FF6D8);}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try{
        need(argc==3&&(std::string_view(argv[2])=="default"||std::string_view(argv[2])=="bit2"),"Original image and default/bit2 recording case required");
        Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original mono recording graphics startup missing");
        recordingMonoFrontier(rt,entry,std::string_view(argv[2])=="bit2");
        std::printf("PASS original mono recording %s: %zu checks; genuine original producer, deferred cache publication/replay, matrix inheritance, malformed input and original payload/context/declaration/FX retirement\n",argv[2],checks);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original mono recording:%zu checks stage=%s:%s\n",checks,stage,error.what());return 1;}
}
