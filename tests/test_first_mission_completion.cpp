// CPU-only qualification of the opt-in completion and direct-stage shortcuts.
// This executes the retail argv setter and EpisodeComplete helper, then observes
// the completion entry. It does not load a world, play a movie or write a save.
#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/native_controllers.h"
#include "runtime/audit_stage.h"
#include "ppc_recomp_shared.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string_view>
#include <type_traits>

void SimpsonsNativeRenderTestStartup(PPCContext&,uint8_t*);
void SimpsonsNativeFirstMissionCompletionReady(PPCContext&,uint8_t*);
void SimpsonsNativeGameplayOwnerRetired(PPCContext&,uint8_t*);
void SimpsonsNativeMapLoadRequestAudit(PPCContext&,uint8_t*);
void SimpsonsNativeFrontendStateAudit(PPCContext&,uint8_t*);
void SimpsonsNativeEpisodeFailureAudit(PPCContext&,uint8_t*);
bool SimpsonsNativeMovieSkip(PPCContext&,uint8_t*);
namespace Simpsons {
void firstMissionCompletionFrame(PPCContext&,uint8_t*);
bool firstMissionCompletionSkipBootstrap(uint8_t*);
bool auditStageSkipBootstrap(uint8_t*);
}

using namespace Simpsons;
namespace {
size_t checks{},completionCalls{};
uint32_t expectedManager{},observedManager{},observedHelper{};
PPCContext* incomingContext{};
void need(bool ok,const char* why){++checks;if(!ok)throw Failure(why);}
template<class F>void rejects(F&& call,const char* why){
    bool failed=false;try{call();}catch(const Failure&){failed=true;}need(failed,why);
}
template<size_t N>void pin(uint8_t* base,uint32_t address,const std::array<uint32_t,N>& words,const char* why){
    for(size_t i=0;i<N;++i)need(PPC_LOAD_U32(address+uint32_t(i)*4)==words[i],why);
}
void unchanged(const PPCContext& actual,const PPCContext& expected,const char* why){
    static_assert(std::is_trivially_copyable_v<PPCContext>);
    need(!std::memcmp(&actual,&expected,sizeof(actual)),why);
}
PPCContext caller(){
    PPCContext c{};std::memset(&c,0xA5,sizeof(c));
    c.r1.u64=0x20000;c.r13.u64=0;c.lr=0xFEDCBA98826B83D4ull;c.fpscr.csr=0x1F80;
    return c;
}
void originalEvidence(uint8_t* base){
    pin(base,0x8289ADF8,std::array{0x7D8802A6u,0x481A15C5u,0x9421FF30u,0x7C7F1B78u},
        "Original pre-publication map callback entry changed");
    pin(base,0x82899890,std::array{0x80BF0004u,0x80DF0008u,0x809F0000u,0x81650008u,
        0x396B0002u,0x556B1838u,0x7D6BF02Au,0xF9610050u,0x81610054u,0x7C6BF214u,
        0x81610050u,0x7D6903A6u,0x4E800421u},"Original queued map callback ABI changed");
    pin(base,0x8289E264,std::array{0x3929ADF8u,0x394AB108u,0x90C10058u,0x38600020u,
        0x90E10060u,0x91010068u,0x91210070u},"Original manager map callback materialization changed");
    pin(base,0x8289E2AC,std::array{0xF8FF0018u,0xF8DF0020u,0xF97F0048u},
        "Original manager map callback pair slot changed");
    pin(base,0x8289AED0,std::array{0x3B400000u,0x937F00ACu,0x38800002u,0x937F00A8u,
        0x7FE3FB78u,0x935F10FCu,0x4BFFECE1u},"Original requested map publication order changed");
    pin(base,0x8239E140,std::array{0x7D8802A6u,0x9181FFF8u,0xFBC1FFE8u,0xFBE1FFF0u},
        "Original frontend observer entry changed");
    pin(base,0x8289DEE0,std::array{0x816304B0u,0x556BA7FEu,0x2B0B0000u,0x4C9A0020u},
        "Original failure observer busy check changed");
    // Direct retail EpisodeComplete helper: load the actual gameflow manager,
    // then tail-call its original completion body. No test-defined dispatch.
    pin(base,0x8296FFC8,std::array{0x3D6082D1u,0x806B8BA8u,0x4BF2EA40u},
        "Retail EpisodeComplete helper bytes changed");
    // Original post-streaming start resumes gameplay, publishes ready/state0,
    // and runs normal score/reset handling before the hook at 823BB5D8.
    pin(base,0x823BB578,std::array{
        0x7D8802A6u,0x9181FFF8u,0xFBE1FFF0u,0x9421FFA0u,0x3D6082D1u,
        0x7C7F1B78u,0x806B90E8u,0x2B030000u,0x419A0008u,0x484F2835u,
        0x484C7F89u,0x39600001u,0x38600001u,0x997F0011u,0x482D5F81u,
        0x38600001u,0x482D5DB1u,0x38600001u,0x482FC831u,0x39600000u,
        0x917F000Cu,0x3D6082D1u,0x806B8BA8u,0x484E3795u,0x38210060u},
        "Original streaming-ready/start boundary bytes changed");
    pin(base,0x8289EA10,std::array{
        0x7D8802A6u,0x9181FFF8u,0xFBC1FFE8u,0xFBE1FFF0u,0x9421FF80u,
        0x7C7F1B78u,0x817F04B0u,0x556AA7FEu,0x2B0A0000u,0x419A0010u,
        0x556B67FEu,0x2B0B0000u,0x419A00E4u},
        "Original completion transition/restart guard bytes changed");
    // The real setter owns no borrowed argv: globals are argc, argv, owned0.
    pin(base,0x828759D8,std::array{0x3D6082E0u,0x3D4082E0u,0x93EB7584u,
        0x3D6082E0u,0x93CB7588u,0x39600000u,0x916A7580u},
        "Original borrowed-argv setter bytes changed");
    // The stock -stream branch passes MODE_STANDARD, empty checkpoint text,
    // include-intro1 and null checkpoint GUID to the real gameflow selector.
    pin(base,0x8285FB34,std::array{0x3CE06609u,0x807A8BA8u,0x39200000u,
        0x39000001u,0x60E78359u,0x7F86E378u,0x7D655B78u,0x4803D951u},
        "Original standard-mode stream dispatch bytes changed");
    need(!std::memcmp(base+0x8215F6C8,"-stream",8),"Original stream option spelling changed");
    pin(base,0x8289D7E4,std::array{0x7C7F1B78u,0x817F0014u,0x2B0B0000u},
        "Original map folder selection field changed");
    pin(base,0x8289D800,std::array{0x481A41A1u,0x2F030000u,0x409A0074u,
        0x809F001Cu},"Original map folder/stream comparison changed");
    pin(base,0x8289BAE0,std::array{0x48009961u,0x574B063Eu,0x2B0B0000u,
        0x419A0050u,0x2B030000u,0x419A0048u},
        "Original direct-map preceding-intro policy changed");
    pin(base,0x823BA794,std::array{0x3D608200u,0x3D4082D1u,0x396B445Cu,0x7FE3FB78u,
        0x917F0008u,0x39600000u,0x916A8C34u,0x482D5FE1u},
        "Original derived cleanup no longer clears gameplay owner before event cleanup");
    pin(base,0x823BBCD4,std::array{0x7C7F1B78u,0x7C9E2378u,0x4BFFE8B5u,
        0x57CB07FEu,0x7FE3FB78u,0x2B0B0000u,0x419A000Cu,0x482E01C1u},
        "Original gameplay retirement boundary is not post-cleanup/pre-free");
}
void startup(Runtime& rt){
    auto* base=rt.base;auto c=caller();currentContext=&c;const auto before=c;
    // Existing borrowed arguments remain untouched during an ordinary launch.
    constexpr uint32_t oldArgs=0x50100;
    PPC_STORE_U32(0x82E07580,0);PPC_STORE_U32(0x82E07584,7);PPC_STORE_U32(0x82E07588,oldArgs);
    const auto allocations=rt.allocations.size();
    SimpsonsNativeRenderTestStartup(c,base);
    need(PPC_LOAD_U32(0x82E07584)==7&&PPC_LOAD_U32(0x82E07588)==oldArgs,
         "Ordinary launch replaced original arguments");
    need(rt.allocations.size()==allocations,"Ordinary launch allocated shortcut arguments");
    unchanged(c,before,"Inactive startup changed the whole caller context");
    rt.firstMissionCompletion=true;
    SimpsonsNativeRenderTestStartup(c,base);
    need(PPC_LOAD_U32(0x82E07584)==3,"Completion startup did not use original argc3");
    need(PPC_LOAD_U32(0x82E07580)==0,"Completion arguments were marked owned by original parser");
    const auto args=PPC_LOAD_U32(0x82E07588);
    need(args&&args!=oldArgs&&PPC_LOAD_U32(args+12)==0,"Completion argv framing differs");
    constexpr std::array<std::string_view,3> expected{"-stream","loc","loc.str"};
    for(size_t i=0;i<expected.size();++i){
        const auto word=PPC_LOAD_U32(args+uint32_t(i)*4);
        need(!std::memcmp(rt.pointer(word,unsigned(expected[i].size()+1),false),
                         expected[i].data(),expected[i].size()+1),"Completion startup did not retain retail LOC stream arguments");
    }
    need(rt.allocations.size()==allocations+1,"Completion argv lacks one runtime-owned allocation");
    unchanged(c,before,"Borrowed original argv setter leaked caller registers");
    need(currentContext==&c,"Original argv setter failed to restore callback context");
    need(!rt.firstMissionCompletionTriggered,"Startup forged mission completion");
    rt.firstMissionCompletion=false;
    rt.bartmanBegins=true;
    SimpsonsNativeRenderTestStartup(c,base);
    need(PPC_LOAD_U32(0x82E07584)==3&&PPC_LOAD_U32(0x82E07580)==0,
         "Bartman stage startup did not retain original borrowed argc3");
    const auto bartmanArgs=PPC_LOAD_U32(0x82E07588);
    need(bartmanArgs&&bartmanArgs!=args&&PPC_LOAD_U32(bartmanArgs+12)==0,
         "Bartman original argv framing differs");
    constexpr std::array<std::string_view,3> bartmanExpected{"-stream","brt","brt.str"};
    for(size_t i=0;i<bartmanExpected.size();++i){
        const auto word=PPC_LOAD_U32(bartmanArgs+uint32_t(i)*4);
        need(!std::memcmp(rt.pointer(word,unsigned(bartmanExpected[i].size()+1),false),
                         bartmanExpected[i].data(),bartmanExpected[i].size()+1),
             "Bartman startup did not use the authored standard map route");
    }
    need(rt.allocations.size()==allocations+2,"Bartman argv lacks one runtime-owned allocation");
    unchanged(c,before,"Bartman borrowed original argv setter leaked caller registers");
    need(currentContext==&c,"Bartman original argv setter failed to restore callback context");
    need(!rt.firstMissionCompletionTriggered&&completionCalls==0,
         "Bartman stage startup manufactured LOC completion");
    rt.bartmanBegins=false;rt.renderTestFirstMission=true;
    SimpsonsNativeRenderTestStartup(c,base);
    need(rt.allocations.size()==allocations+3,"Existing first-mission rendering mode lost its owned argv");
    const auto renderArgs=PPC_LOAD_U32(0x82E07588);
    for(size_t i=0;i<expected.size();++i){
        const auto word=PPC_LOAD_U32(renderArgs+uint32_t(i)*4);
        need(!std::memcmp(rt.pointer(word,unsigned(expected[i].size()+1),false),
                         expected[i].data(),expected[i].size()+1),
             "Existing first-mission rendering route changed");
    }
    unchanged(c,before,"Existing render-test original argv setter leaked caller registers");
    const auto stableAllocations=rt.allocations.size();
    for(const auto modes:{3u,5u,6u,7u}){
        rt.renderTestFirstMission=(modes&1u)!=0;
        rt.firstMissionCompletion=(modes&2u)!=0;rt.bartmanBegins=(modes&4u)!=0;
        rejects([&]{SimpsonsNativeRenderTestStartup(c,base);},"Ambiguous direct startup modes accepted");
        need(rt.allocations.size()==stableAllocations&&PPC_LOAD_U32(0x82E07588)==renderArgs,
             "Rejected direct startup modes changed original argv storage");
        unchanged(c,before,"Rejected direct startup modes leaked caller registers");
    }
    rt.renderTestFirstMission=false;rt.firstMissionCompletion=false;rt.bartmanBegins=false;
    std::puts("PASS normal isolation / actual borrowed argv setter / LOC and authored Bartman streams / conflicting modes / whole caller ABI");
}
void originalNames(Runtime& rt){
    auto* base=rt.base;auto c=caller();currentContext=&c;EngineCpuCalls cpu(c,base);
    struct Name {const char* literal;uint32_t hash;};
    // Execute the actual retail case-folding name hash. These names come from
    // the original LOC episode's Lua declarations, not the native gate code.
    constexpr std::array names{
        Name{"LAND_OF_CHOCOLATE",0x629E5EE8},Name{"MODE_STANDARD",0x66098359},
        Name{"SPR_HUB",0x9FA8DE6B},Name{"SCORE_EVENT_LAND_OF_CHOCOLATE_COMPLETE",0x51A559A2},
        Name{"BARTMAN_BEGINS",0xE7FEB1F0},Name{"SCORE_EVENT_BARTMAN_BEGINS_COMPLETE",0x16DF4FB6},
        Name{"GameMode_TwoPlayer",0x6D26E1E3},Name{"gam_igc01",0xE8F06E3A}};
    for(const auto& name:names){
        const auto length=unsigned(std::strlen(name.literal)+1);
        std::memcpy(rt.pointer(0x50100,length,true),name.literal,length);
        need(cpu.invoke(0x827451C0,0x50100)==name.hash,"Completion gate hash disagrees with actual retail name hashing");
    }
    std::puts("PASS actual retail name hashes for LOC / Bartman / standard mode / completion events / players / intro");
}
struct Fixture {
    static constexpr uint32_t owner=0x51000,manager=0x52000,episode=0x54000,
        mode=0x54100,map=0x54200,movie=0x54300,folder=0x54400,stream=0x54410;
    Runtime& rt;uint8_t* base;PPCContext c;
    explicit Fixture(Runtime& runtime):rt(runtime),base(rt.base),c(caller()){reset();}
    void reset(){
        rt.auditStage.clear();rt.auditStageMapStarted=false;rt.auditStagePlayIntro=false;
        std::memset(rt.pointer(owner,0x4000,true),0,0x4000);
        rt.bartmanBegins=false;rt.bartmanBeginsMapStarted=false;
        rt.firstMissionCompletion=true;rt.firstMissionCompletionTriggered=false;rt.firstMissionCompletionOwner=0;rt.firstMissionCompletionManager=0;
        PPC_STORE_U32(0x82D08C34,owner);PPC_STORE_U32(0x82D08BA8,manager);
        PPC_STORE_U32(0x82D09750,movie);PPC_STORE_U32(0x82E06F5C,0);
        PPC_STORE_U8(owner+17,1);
        PPC_STORE_U32(manager,0x821822E8);PPC_STORE_U32(manager+160,episode);
        PPC_STORE_U32(manager+164,mode);PPC_STORE_U32(manager+168,map);PPC_STORE_U32(manager+172,map);
        PPC_STORE_U32(episode+32,0x629E5EE8); // LAND_OF_CHOCOLATE
        PPC_STORE_U32(mode+8,3);PPC_STORE_U32(mode+32,0x66098359); // MODE_STANDARD
        PPC_STORE_U32(map+8,1);PPC_STORE_U32(map+20,folder);PPC_STORE_U32(map+28,stream);
        PPC_STORE_U32(map+52,0x9FA8DE6B); // SPR_HUB successor supplied by retail gameflow.lua
        PPC_STORE_U32(map+56,0x51A559A2); // SCORE_EVENT_LAND_OF_CHOCOLATE_COMPLETE
        std::memcpy(rt.pointer(folder,4,true),"loc",4);std::memcpy(rt.pointer(stream,8,true),"loc.str",8);
        c=caller();c.r31.u64=owner;currentContext=&c;incomingContext=&c;expectedManager=manager;
    }
    template<class F>void preserved(F&& operation,const char* why){
        const auto before=c;const auto previous=currentContext;const auto fp=PPCFPSCRRegister::getcsr();
        operation();unchanged(c,before,why);need(currentContext==previous,"Completion callback leaked TLS context");
        need(PPCFPSCRRegister::getcsr()==fp,"Completion callback leaked host FP control/status");
    }
    void arm(){preserved([&]{SimpsonsNativeFirstMissionCompletionReady(c,base);},"Ready hook changed caller context");}
    void frame(){preserved([&]{firstMissionCompletionFrame(c,base);},"Completion frame changed caller context");}
    void pending(){need(!rt.firstMissionCompletionTriggered&&completionCalls==0,"Unready state dispatched/consumed completion");}
};
void auditStageReadiness(Runtime& rt,std::string_view requested={}) {
    Fixture f(rt);auto* base=rt.base;
    const auto previousCompletions=completionCalls;
    for(const auto stage:auditStages) {
        if(!requested.empty()&&stage!=requested)continue;
        f.reset();rt.firstMissionCompletion=false;rt.auditStage=stage;
        need(auditStageSkipBootstrap(base),"Pending stage did not select bootstrap skip");
        rt.auditStagePlayIntro=true;
        need(!auditStageSkipBootstrap(base),"Requested original stage intro was skipped");
        rt.auditStagePlayIntro=false;
        const auto filename=std::string(stage)+".str";
        const auto before=f.c;
        const auto allocations=rt.allocations.size();
        SimpsonsNativeRenderTestStartup(f.c,base);
        unchanged(f.c,before,"Stage setter changed original ABI");
        need(rt.allocations.size()==allocations+1,"Stage setter did not own borrowed argv");
        const auto args=PPC_LOAD_U32(0x82E07588);
        const std::array<std::string,3> expected{"-stream",std::string(stage),filename};
        for(uint32_t i=0;i<3;++i)need(!std::memcmp(rt.pointer(PPC_LOAD_U32(args+4*i),unsigned(expected[i].size()+1),false),
            expected[i].c_str(),expected[i].size()+1),"Original stage argv bytes differ");
        PPC_STORE_U32(Fixture::map+20,0x55000);PPC_STORE_U32(Fixture::map+28,0x55100);
        std::memcpy(rt.pointer(0x55000,unsigned(stage.size()+1),true),std::string(stage).c_str(),stage.size()+1);
        std::memcpy(rt.pointer(0x55100,unsigned(filename.size()+1),true),filename.c_str(),filename.size()+1);
        PPC_STORE_U32(Fixture::manager+168,0);f.arm();
        need(!rt.auditStageMapStarted,"Audit startup accepted an intro package");
        PPC_STORE_U32(Fixture::manager+168,Fixture::map);
        PPC_STORE_U8(0x55100,'?');f.arm();need(!rt.auditStageMapStarted,"Audit startup accepted the wrong stream");
        PPC_STORE_U8(0x55100,uint8_t(filename.front()));
        f.arm();need(rt.auditStageMapStarted,"Audit stage did not restore movie controls at original map-ready boundary");
        need(!auditStageSkipBootstrap(base),"Ready stage retained bootstrap skip");
        need(!rt.firstMissionCompletionTriggered&&completionCalls==previousCompletions,"Stage audit forged a mission completion");
        PPC_STORE_U32(Fixture::manager+172,0);PPC_STORE_U32(0x82D08C34,0);f.arm();
        need(rt.auditStageMapStarted,"Map retirement re-enabled bootstrap movie skipping");
        need(!auditStageSkipBootstrap(base),"Map retirement skipped a later movie");
    }
    f.reset();rt.firstMissionCompletion=false;rt.auditStage="../loc";
    const auto allocations=rt.allocations.size();
    rejects([&]{SimpsonsNativeRenderTestStartup(f.c,base);},"Audit stage accepted a foreign path");
    need(rt.allocations.size()==allocations,"Rejected stage allocated borrowed argv");
    rt.auditStage="loc";rt.bartmanBegins=true;
    rejects([&]{SimpsonsNativeRenderTestStartup(f.c,base);},"Audit stage accepted conflicting startup modes");
    rt.auditStage.clear();rt.bartmanBegins=false;
    std::puts("PASS independent original stage argv setter / exact map-ready boundary / retirement / malformed paths");
}
std::filesystem::path auditLifecycle(Runtime& rt) {
    // These metadata-only snapshots verify the audit callbacks, not a world
    // reload. Instruction pins above establish their real AOT boundaries.
    Fixture f(rt);auto* base=rt.base;
    const auto path=std::filesystem::temp_directory_path()/
        ("simpsons-lifecycle-observer-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+".jsonl");
    rt.resourceAudit.configure(path);rt.resourceAudit.action("cpu-lifecycle-observer");
    rt.firstMissionCompletion=false;rt.auditStage="loc";
    std::array<uint8_t,0x4000> guest{};
    const auto snapshot=[&]{std::memcpy(guest.data(),rt.pointer(Fixture::owner,unsigned(guest.size()),false),guest.size());};
    const auto unchangedGuest=[&]{need(!std::memcmp(guest.data(),rt.pointer(Fixture::owner,unsigned(guest.size()),false),guest.size()),
        "Lifecycle observation changed guest world, scores or checkpoint state");};
    snapshot();f.arm();unchangedGuest();
    need(rt.auditGameplayOwner==Fixture::owner&&rt.auditGameplayAsset=="loc/loc.str"&&rt.auditGameplayGeneration==1,
        "Genuine ready boundary did not retain exact owner/map identity");
    need(rt.auditGameplayParameters.find("rootEpisode=none")!=std::string::npos&&
         rt.auditGameplayParameters.find("episodeFlags=0x00000000")!=std::string::npos&&
         rt.auditGameplayParameters.find("quittableByManagerFields=false")!=std::string::npos,
         "Original direct-entry root absence was not captured before menu gating");
    need(rt.auditStageMapStarted,"Lifecycle observer changed original stage-ready admission");
    snapshot();f.arm();unchangedGuest();
    need(rt.auditGameplayBoundaryOrdinal==2&&rt.auditGameplayGeneration==1,
        "Repeated original ready boundary disappeared behind the startup latch or created another owner lifetime");
    // Current manager metadata can already describe a successor when the old
    // owner retires. The retirement receipt must retain its proven old asset.
    std::memcpy(rt.pointer(Fixture::folder,4,true),"brt",4);
    std::memcpy(rt.pointer(Fixture::stream,8,true),"brt.str",8);
    PPC_STORE_U32(0x82D08C34,0);f.c.r30.u64=0;
    snapshot();f.preserved([&]{SimpsonsNativeGameplayOwnerRetired(f.c,base);},"Retirement observer changed caller context");unchangedGuest();
    need(!rt.auditGameplayOwner&&rt.auditGameplayAsset.empty(),"Matching original cleanup retained stale owner identity");
    f.reset();rt.firstMissionCompletion=false;rt.auditStage="loc";
    snapshot();f.arm();unchangedGuest();
    need(rt.auditGameplayGeneration==2&&rt.auditGameplayOwner==Fixture::owner,
        "Reused guest owner address inherited the retired lifetime");
    // Diagnostic failure cannot create a new rejection in an ordinary launch.
    rt.auditStage.clear();f.c.r31.u64=0x60000;
    snapshot();f.arm();unchangedGuest();
    need(rt.auditGameplayGeneration==2&&rt.auditGameplayOwner==Fixture::owner,
        "Unreadable snapshot replaced the proven live owner identity");
    // Existing opt-in admission must still reject the same malformed owner.
    f.reset();f.c.r31.u64=Fixture::owner+4;snapshot();
    const auto before=f.c;rejects([&]{f.arm();},"Audit observation weakened malformed original ready admission");
    unchanged(f.c,before,"Rejected audited ready boundary changed caller context");unchangedGuest();
    std::ifstream input(path);need(bool(input),"Cannot inspect flushed lifecycle observer receipts");
    std::vector<std::string> rows;for(std::string line;std::getline(input,line);)rows.push_back(std::move(line));
    need(rows.size()==6,"Lifecycle audit deduplicated original boundaries or lost unknown diagnostics");
    for(const auto& row:rows)need(row.find("\"event\":\"lifecycle\"")!=std::string::npos,"Original lifecycle boundary became an asset encounter");
    const auto group=[&](size_t i){const auto at=rows[i].find("\"group\":");need(at!=std::string::npos,"Lifecycle receipt lost its stable group");return rows[i].substr(at);};
    need(group(0)==group(1),"Transient boundary ordinals entered lifecycle combination grouping");
    need(rows[2].find("\"asset\":\"loc/loc.str\"")!=std::string::npos&&
        rows[2].find("phase=cleanup-complete")!=std::string::npos&&rows[2].find("globalOwnerCleared=true")!=std::string::npos,
        "Old owner cleanup was attributed to current successor metadata");
    need(rows[3].find("ownerGeneration=2")!=std::string::npos,"Reused owner lifecycle receipt lost its generation");
    need(rows[4].find("snapshot=unreadable qualification=unknown")!=std::string::npos&&
        rows[4].find("\"asset\":\"unknown\"")!=std::string::npos,"Unreadable snapshot fabricated a qualified map identity");
    need(rows[5].find("qualification=unqualified")!=std::string::npos,"Rejected original owner lost its pre-validation snapshot");
    rt.firstMissionCompletion=false;rt.auditStage.clear();
    // These are observation-only metadata cases. They never invoke the death
    // producer or mutate actor health, checkpoint progress or game messages.
    f.reset();f.c.r3.u64=Fixture::manager;f.c.lr=0x823BBACC;
    const auto oldFP=PPCFPSCRRegister::getcsr();PPCFPSCRRegister::restoreHostCSR(0x5F80);SetLastError(0x12345678);
    snapshot();f.preserved([&]{SimpsonsNativeEpisodeFailureAudit(f.c,base);},"Failure request observer changed caller context");
    const auto observedFP=PPCFPSCRRegister::getcsr();const auto observedError=GetLastError();PPCFPSCRRegister::restoreHostCSR(oldFP);
    need(observedFP==0x5F80&&observedError==0x12345678,"Failure observation changed host FP/error state");unchangedGuest();
    f.c.r3.u64=0x60000;snapshot();f.preserved([&]{SimpsonsNativeEpisodeFailureAudit(f.c,base);},"Unknown failure observer changed caller context");unchangedGuest();
    const std::array frontendGlobals{0x82D08C98u,0x82D08C9Cu,0x82CD0EE4u,0x82D09750u};
    std::array<uint32_t,4> previous{};for(size_t i=0;i<4;++i)previous[i]=PPC_LOAD_U32(frontendGlobals[i]);
    PPC_STORE_U32(0x82D08C98,6);PPC_STORE_U32(0x82D08C9C,6);PPC_STORE_U32(0x82CD0EE4,1001);PPC_STORE_U32(0x82D09750,0);
    f.c.lr=0x8239C9E4;snapshot();f.preserved([&]{SimpsonsNativeFrontendStateAudit(f.c,base);},"Frontend observer changed caller context");unchangedGuest();
    for(size_t i=0;i<4;++i)PPC_STORE_U32(frontendGlobals[i],previous[i]);
    input.close();input.open(path);rows.clear();for(std::string line;std::getline(input,line);)rows.push_back(std::move(line));
    need(rows.size()==9&&rows[6].find("source=whole-party-death")!=std::string::npos&&
         rows[6].find("phase=original-request")!=std::string::npos&&rows[7].find("snapshot=unreadable")!=std::string::npos&&
         rows[8].find("selector=6 pending=6 notification=1001 movieState=0")!=std::string::npos,
         "Lifecycle request observations lost original/unknown state or fabricated completion");
    std::printf("PASS original map lifecycle snapshots / repeated ready / old-owner cleanup / address reuse / unknown read / unchanged rejection and whole ABI; receipts=%s\n",path.string().c_str());
    return path;
}
void auditEarlyMapRequest(Runtime& rt,const std::filesystem::path& path) {
    // Metadata-only observation fixture. It qualifies the source-pinned entry
    // ABI without executing a map callback, loading a world or manufacturing a
    // request, ready event, death, input or successful original result.
    Fixture f(rt);auto* base=rt.base;constexpr uint32_t record=0x54500;
    // Retain the existing sink and live ready-owner cache from auditLifecycle.
    // ResourceAudit intentionally rejects reconfiguration within one Runtime.
    std::ifstream baseline(path);need(bool(baseline),"Cannot inspect preceding lifecycle observer receipts");
    size_t rows{};for(std::string line;std::getline(baseline,line);)++rows;
    const auto baselineRows=rows;need(baselineRows==9,"Early map fixture lost its explicit preceding lifecycle baseline");
    baseline.close();rt.resourceAudit.action("cpu-pre-publication-map-observer");
    const auto cachedOwner=rt.auditGameplayOwner;
    const auto cachedGeneration=rt.auditGameplayGeneration;
    const auto cachedAsset=rt.auditGameplayAsset,cachedParameters=rt.auditGameplayParameters;
    need(cachedOwner==Fixture::owner&&cachedGeneration==2&&cachedAsset=="loc/loc.str"&&!cachedParameters.empty(),
         "Early map fixture lacks the preceding proven ready-owner snapshot");
    const auto prepare=[&] {
        f.reset();rt.firstMissionCompletion=false;rt.resourceAudit.mission("frontend");
        PPC_STORE_U32(Fixture::manager+24,0x8289ADF8);PPC_STORE_U32(Fixture::manager+28,0);
        PPC_STORE_U32(Fixture::manager+168,0);PPC_STORE_U32(Fixture::manager+172,0);
        PPC_STORE_U32(record,0);PPC_STORE_U32(record+4,Fixture::map);PPC_STORE_U32(record+8,0);
        f.c.r3.u64=Fixture::manager;f.c.r4.u64=0;f.c.r5.u64=Fixture::map;f.c.r6.u64=0;
        f.c.r30.u64=Fixture::manager;f.c.r31.u64=record;f.c.lr=0x828998C4;
    };
    const auto observe=[&](std::string_view mission,std::string_view qualification) {
        std::array<uint8_t,0x4000> guest{};
        std::memcpy(guest.data(),rt.pointer(Fixture::owner,unsigned(guest.size()),false),guest.size());
        const auto global=PPC_LOAD_U32(0x82D08BA8);
        const auto before=f.c;const auto previous=currentContext;
        const auto oldFP=PPCFPSCRRegister::getcsr();PPCFPSCRRegister::restoreHostCSR(0x5F80);SetLastError(0x12345678);
        SimpsonsNativeMapLoadRequestAudit(f.c,base);
        const auto observedFP=PPCFPSCRRegister::getcsr();const auto observedError=GetLastError();PPCFPSCRRegister::restoreHostCSR(oldFP);
        unchanged(f.c,before,"Map request observation changed full PPC context");
        need(currentContext==previous&&observedFP==0x5F80&&observedError==0x12345678,
             "Map request observation changed TLS/host FP/error state");
        need(PPC_LOAD_U32(0x82D08BA8)==global&&!std::memcmp(guest.data(),rt.pointer(Fixture::owner,unsigned(guest.size()),false),guest.size()),
             "Map request observation changed original manager/package/record memory");
        need(rt.auditGameplayOwner==cachedOwner&&rt.auditGameplayGeneration==cachedGeneration&&
             rt.auditGameplayAsset==cachedAsset&&rt.auditGameplayParameters==cachedParameters&&!rt.auditStageMapStarted,
             "Map request observation created or replaced a ready gameplay lifetime");
        std::ifstream input(path);need(bool(input),"Cannot inspect flushed map request observer receipt");
        size_t count{};std::string line,last;while(std::getline(input,line)){last=std::move(line);++count;}
        need(count==++rows,"Map request observer lost or deduplicated a genuine callback boundary");
        need(last.find("\"event\":\"lifecycle\"")!=std::string::npos&&
             last.find("\"mission\":\""+std::string(mission)+"\"")!=std::string::npos&&
             last.find(qualification)!=std::string::npos,"Map request observer fabricated or lost mission/qualification context");
        return last;
    };
    prepare();const auto first=observe("loc","qualification=map-load-request");
    need(first.find("phase=map-load-request")!=std::string::npos&&first.find("\"asset\":\"loc/loc.str\"")!=std::string::npos&&
         first.find("publishedPackage=0x00000000 publishedMap=0x00000000")!=std::string::npos,
         "Early map identity required a ready or already-published package");
    const auto groupAt=first.find("\"group\":");need(groupAt!=std::string::npos,"Early map receipt lost stable combination group");
    const auto second=observe("loc","qualification=map-load-request");
    need(first.substr(groupAt)==second.substr(second.find("\"group\":")),"Transient map request record/ordinal entered combination grouping");
    prepare();f.c.lr-=4;observe("frontend","qualification=unqualified");
    prepare();PPC_STORE_U32(0x82D08BA8,0);observe("frontend","qualification=unqualified");
    prepare();PPC_STORE_U32(Fixture::manager,0);observe("frontend","qualification=unqualified");
    prepare();f.c.r30.u64=Fixture::manager+4;observe("frontend","qualification=unqualified");
    prepare();PPC_STORE_U32(Fixture::manager+24,0);observe("frontend","qualification=unqualified");
    prepare();PPC_STORE_U32(Fixture::manager+28,4);observe("frontend","qualification=unqualified");
    prepare();PPC_STORE_U32(record+4,Fixture::map+4);observe("frontend","qualification=unqualified");
    prepare();PPC_STORE_U32(Fixture::map+8,2);observe("frontend","qualification=unqualified");
    prepare();PPC_STORE_U32(Fixture::map+20,0x60000);
    const auto unknown=observe("frontend","snapshot=unreadable qualification=unknown");
    need(unknown.find("\"asset\":\"unknown\"")!=std::string::npos,"Unreadable early map fabricated asset identity");
    prepare();std::memset(rt.pointer(Fixture::folder,256,true),'x',256);observe("frontend","snapshot=unreadable qualification=unknown");
    prepare();f.c.r5.u64=0xFFFFFFFC;PPC_STORE_U32(record+4,f.c.r5.u32);observe("frontend","snapshot=unreadable qualification=unknown");
    prepare();f.c.r4.u64=1;PPC_STORE_U32(record,1);f.c.r5.u64=0xFFFFFFFC;
    const auto removal=observe("frontend","qualification=other-map-operation");
    need(removal.find("phase=map-callback-entry")!=std::string::npos,"Original remove operation became a map-load request");
    const auto before=f.c;SimpsonsNativeMapLoadRequestAudit(f.c,base+1);
    unchanged(f.c,before,"Foreign map observer changed caller context");
    std::ifstream input(path);size_t count{};for(std::string line;std::getline(input,line);)++count;
    need(count==rows,"Foreign map observer read another runtime or emitted a qualified receipt");
    std::printf("PASS source-pinned early map request observer / unpublished map / repeated boundary / wrong caller, manager, callback, record and package / bounded unknown reads / full ABI; preceding=%zu requests=%zu receipts=%s\n",baselineRows,rows-baselineRows,path.string().c_str());
}
void auditRetirementAttribution(Runtime& rt,const std::filesystem::path& path) {
    // Observation-only callbacks, separate from the frozen live world exit.
    // Run last so the preceding fixture's explicit9-row baseline is intact.
    Fixture f(rt);auto* base=rt.base;rt.firstMissionCompletion=false;
    rt.resourceAudit.action("cpu-retirement-attribution");
    const auto lastRow=[&] {
        std::ifstream input(path);need(bool(input),"Cannot inspect attribution audit receipt");
        std::string line,last;while(std::getline(input,line))last=std::move(line);
        need(!last.empty(),"Attribution audit receipt missing");return last;
    };
    const auto observe=[&](auto&& callback) {
        const auto context=f.c;const auto previous=currentContext;
        std::array<uint8_t,0x4000> guest{};
        std::memcpy(guest.data(),rt.pointer(Fixture::owner,unsigned(guest.size()),false),guest.size());
        const std::array globals{PPC_LOAD_U32(0x82D08C34),PPC_LOAD_U32(0x82D08BA8),PPC_LOAD_U32(0x82D09750)};
        const auto oldFP=PPCFPSCRRegister::getcsr();PPCFPSCRRegister::restoreHostCSR(0x5F80);SetLastError(0x12345678);
        callback();const auto observedFP=PPCFPSCRRegister::getcsr();const auto observedError=GetLastError();
        PPCFPSCRRegister::restoreHostCSR(oldFP);
        unchanged(f.c,context,"Attribution observation changed full PPC context");
        need(currentContext==previous&&observedFP==0x5F80&&observedError==0x12345678,
             "Attribution observation changed TLS/host FP/error state");
        need(!std::memcmp(guest.data(),rt.pointer(Fixture::owner,unsigned(guest.size()),false),guest.size()),
             "Attribution observation changed guest owner/manager/package bytes");
        need(globals==std::array{PPC_LOAD_U32(0x82D08C34),PPC_LOAD_U32(0x82D08BA8),PPC_LOAD_U32(0x82D09750)},
             "Attribution observation changed original global publications");
    };
    const auto probe=[&](const char* phase,const char* mission) {
        rt.resourceAudit.observe("fixture-attribution","cpu/retirement-observer",uint32_t(f.c.lr),
            phase,"metadata-only; no world cleanup/reload credit");
        const auto row=lastRow();
        need(row.find("\"mission\":\""+std::string(mission)+"\"")!=std::string::npos,
             "Resource after retirement inherited stale or fabricated mission attribution");
    };
    observe([&]{SimpsonsNativeFirstMissionCompletionReady(f.c,base);});
    const auto generation=rt.auditGameplayGeneration;
    need(rt.auditGameplayOwner==Fixture::owner&&rt.auditGameplayAsset=="loc/loc.str",
         "Attribution fixture lacks a qualified initial owner");
    probe("initial-qualified-ready","loc");
    PPC_STORE_U32(0x82D08C34,0);f.c.r31.u64=Fixture::owner+4;
    observe([&]{SimpsonsNativeGameplayOwnerRetired(f.c,base);});
    need(rt.auditGameplayOwner==Fixture::owner&&rt.auditGameplayGeneration==generation,
         "Uncached retirement removed another ready lifetime");
    probe("foreign-retirement","loc");
    f.c.r31.u64=Fixture::owner;
    const auto beforeForeign=lastRow();
    observe([&]{SimpsonsNativeGameplayOwnerRetired(f.c,base+1);});
    need(lastRow()==beforeForeign&&rt.auditGameplayOwner==Fixture::owner,
         "Foreign runtime retirement changed attribution or emitted a receipt");
    observe([&]{SimpsonsNativeGameplayOwnerRetired(f.c,base);});
    const auto retired=lastRow();
    need(retired.find("phase=cleanup-complete")!=std::string::npos&&
         retired.find("cachedReady=true globalOwnerCleared=true")!=std::string::npos&&
         retired.find("\"asset\":\"loc/loc.str\"")!=std::string::npos&&
         retired.find("\"mission\":\"loc\"")!=std::string::npos,
         "Qualified cleanup lost its old mission/owner identity before attribution reset");
    need(!rt.auditGameplayOwner&&rt.auditGameplayAsset.empty()&&rt.auditGameplayParameters.empty()&&
         rt.auditGameplayGeneration==generation,"Retirement fabricated a successor lifetime");
    probe("after-qualified-retirement","unknown");
    f.c.lr=0x8239C9E4;
    observe([&]{SimpsonsNativeFrontendStateAudit(f.c,base);});
    need(lastRow().find("\"mission\":\"unknown\"")!=std::string::npos,
         "Frontend selector snapshot retained a retired mission");
    f.c.r3.u64=0xFFFFFFFC;f.c.r5.u64=0xFFFFFFFC;f.c.r31.u64=0xFFFFFFFC;f.c.lr=0x828998C4;
    observe([&]{SimpsonsNativeMapLoadRequestAudit(f.c,base);});
    need(lastRow().find("qualification=unknown")!=std::string::npos,
         "Unreadable map request fabricated qualification");
    probe("unreadable-successor-request","unknown");
    f.c.r31.u64=0xFFFFFFFC;
    observe([&]{SimpsonsNativeGameplayOwnerRetired(f.c,base);});
    probe("uncached-retirement-after-cleanup","unknown");
    f.reset();rt.firstMissionCompletion=false;
    observe([&]{SimpsonsNativeFirstMissionCompletionReady(f.c,base);});
    need(rt.auditGameplayOwner==Fixture::owner&&rt.auditGameplayAsset=="loc/loc.str"&&
         rt.auditGameplayGeneration==generation+1,"Reused owner address inherited the retired generation");
    probe("new-qualified-ready-at-reused-address","loc");
    std::printf("PASS retirement attribution / old cleanup receipt / unknown frontend and malformed request / qualified reused-owner generation / unchanged guest, full ABI, CSR and LastError; receipts=%s\n",path.string().c_str());
}
void readiness(Runtime& rt){
    Fixture f(rt);auto* base=f.base;
    rt.firstMissionCompletion=false;PPC_STORE_U32(0x82D08C34,0);f.c.r31.u64=0xDEADBEEFu;
    f.arm();f.frame();need(!rt.firstMissionCompletionOwner&&!rt.firstMissionCompletionManager,"Inactive completion mode armed an owner");f.pending();
    need(!firstMissionCompletionSkipBootstrap(base),"Ordinary launch skipped an original movie");
    f.reset();f.frame();f.pending(); // No ready receipt has occurred.
    need(firstMissionCompletionSkipBootstrap(base),"Opt-in bootstrap intro did not retain its skip policy");
    rejects([&]{SimpsonsNativeFirstMissionCompletionReady(f.c,base+1);},"Foreign ready runtime accepted");
    rejects([&]{firstMissionCompletionFrame(f.c,base+1);},"Foreign frame runtime accepted");
    f.c.r31.u64=Fixture::owner+4;
    rejects([&]{f.arm();},"Foreign original map-start owner accepted");f.pending();
    f.reset();PPC_STORE_U32(Fixture::owner+12,8);
    rejects([&]{f.arm();},"Streaming application state armed completion");f.pending();
    f.reset();PPC_STORE_U8(Fixture::owner+17,0);
    rejects([&]{f.arm();},"Unready original application armed completion");f.pending();
    f.reset();PPC_STORE_U32(0x82D08C34,0);f.c.r31.u64=0;
    rejects([&]{f.arm();},"Null original map-start owner accepted");f.pending();

    // These mutations cross different real gameflow boundaries: another
    // episode, a challenge mode, an intro/outro package, or a wrong destination.
    // A map pointer appears before loading, so current-package equality matters.
    struct WordCase {uint32_t address,value;};
    const std::array wrongMaps{
        WordCase{0x82D08BA8,0},WordCase{Fixture::manager,0},
        WordCase{Fixture::manager+160,0},WordCase{Fixture::manager+164,0},
        WordCase{Fixture::manager+172,0},WordCase{Fixture::manager+168,Fixture::movie},
        WordCase{Fixture::episode+32,0x629E5EE9},WordCase{Fixture::mode+8,1},
        WordCase{Fixture::mode+32,0x66098358},WordCase{Fixture::map+8,2},
        WordCase{Fixture::map+52,0},WordCase{Fixture::map+56,0},
        WordCase{Fixture::map+20,0},WordCase{Fixture::map+28,0}};
    for(const auto test:wrongMaps){
        f.reset();PPC_STORE_U32(test.address,test.value);f.arm();f.frame();
        need(!rt.firstMissionCompletionOwner,"Foreign/incomplete gameflow package armed completion");f.pending();
    }
    for(const auto address:{Fixture::folder,Fixture::stream}){
        f.reset();PPC_STORE_U8(address,'L');f.arm();
        need(!rt.firstMissionCompletionOwner,"Different retail stream spelling armed completion");f.pending();
    }
    f.reset();f.arm();need(rt.firstMissionCompletionOwner==Fixture::owner,"Original LOC ready receipt did not arm its owner");
    need(rt.firstMissionCompletionManager==Fixture::manager,"Ready receipt lost its original LOC manager identity");
    f.pending();
    std::puts("PASS inactive/foreign/unready owners / original LOC standard map qualification / post-load ready receipt");
}
void dispatch(Runtime& rt){
    Fixture f(rt);auto* base=f.base;
    // Retry gates do not consume the one-shot request. Each returns while the
    // real application, decoder, or active package still owns transition work.
    struct Gate {uint32_t address,value;bool byte;};
    const std::array gates{
        Gate{0x82D08C34,Fixture::owner+0x100,false},Gate{0x82D08BA8,Fixture::manager+0x100,false},
        Gate{Fixture::manager+1200,0x1000,false},Gate{Fixture::manager+1200,0x100000,false},
        Gate{Fixture::manager+1200,0x101000,false},Gate{Fixture::owner+12,8,false},
        Gate{Fixture::owner+17,0,true},Gate{Fixture::owner+19,1,true},
        Gate{0x82D09750,0,false},Gate{Fixture::movie+20,2,false},
        Gate{Fixture::movie+20,4,false},Gate{Fixture::movie+20,6,false},
        Gate{0x82E06F5C,Fixture::movie+0x100,false},
        Gate{Fixture::manager+168,Fixture::movie,false},
        Gate{Fixture::map+56,0,false}};
    for(const auto gate:gates){
        f.reset();f.arm();const auto old=gate.byte?uint32_t(PPC_LOAD_U8(gate.address)):PPC_LOAD_U32(gate.address);
        if(gate.byte)PPC_STORE_U8(gate.address,gate.value);else PPC_STORE_U32(gate.address,gate.value);
        f.frame();f.pending();need(rt.firstMissionCompletionOwner==Fixture::owner,"Retry gate lost its original ready owner");
        if(gate.byte)PPC_STORE_U8(gate.address,old);else PPC_STORE_U32(gate.address,old);
    }
    f.reset();f.arm();PPC_STORE_U32(Fixture::manager+1200,0x41000);
    // Original completion can remove the map and start the outro before the
    // clock retries. Recognize its success from the armed manager beforehand.
    PPC_STORE_U32(Fixture::manager+168,Fixture::movie);PPC_STORE_U32(Fixture::manager+172,0);
    PPC_STORE_U32(Fixture::movie+20,2);PPC_STORE_U32(Fixture::owner+12,8);
    f.frame();need(rt.firstMissionCompletionTriggered&&completionCalls==0,
                  "Naturally queued original completion dispatched a duplicate helper");
    need(PPC_LOAD_U32(Fixture::manager+1200)==0x41000,"Shortcut changed naturally queued original flags");
    need(!firstMissionCompletionSkipBootstrap(base),"Natural completion outro inherited bootstrap auto-skip");
    f.frame();need(completionCalls==0,"Naturally queued completion repeated");

    f.reset();f.arm();PPC_STORE_U32(Fixture::manager+1200,0x40000);
    PPC_STORE_U32(Fixture::manager+172,0);PPC_STORE_U32(Fixture::movie+20,2);
    need(!firstMissionCompletionSkipBootstrap(base)&&rt.firstMissionCompletionTriggered&&completionCalls==0,
         "Movie predicate skipped an original success outro before another clock frame");
    f.reset();f.arm();PPC_STORE_U32(0x82D08BA8,Fixture::manager+0x100);
    PPC_STORE_U32(Fixture::manager+0x100+1200,0x40000);
    need(firstMissionCompletionSkipBootstrap(base)&&!rt.firstMissionCompletionTriggered,
         "Foreign manager success consumed the armed LOC request");

    f.reset();f.arm();
    std::array<uint8_t,0x4000> before{};std::memcpy(before.data(),rt.pointer(Fixture::owner,unsigned(before.size()),false),before.size());
    f.frame();need(completionCalls==1&&rt.firstMissionCompletionTriggered,"Ready LOC did not invoke original completion once");
    need(!firstMissionCompletionSkipBootstrap(base),"Dispatched completion outro inherited bootstrap auto-skip");
    need(observedManager==Fixture::manager&&observedHelper==0x8296FFC8,"Original completion observation missing");
    need(!std::memcmp(before.data(),rt.pointer(Fixture::owner,unsigned(before.size()),false),before.size()),
         "Shortcut manufactured guest completion, scores or world state");
    f.frame();f.arm();need(completionCalls==1,"Completed one-shot rearmed or dispatched again");
    rt.firstMissionCompletion=false;rt.firstMissionCompletionTriggered=false;f.frame();
    need(completionCalls==1,"Ordinary gameplay inherited a completion request");
    std::puts("PASS stale/busy/restart/pause/movie/decoder retries / natural success before clock / preserved outro / real retail helper / one-shot / whole-context ABI");
}
void bartmanReadiness(Runtime& rt){
    Fixture f(rt);auto* base=f.base;
    const auto originalCalls=completionCalls;
    const auto prepare=[&]{
        f.reset();rt.firstMissionCompletion=false;rt.bartmanBegins=true;
        PPC_STORE_U32(Fixture::episode+32,0xE7FEB1F0);
        PPC_STORE_U32(Fixture::map+52,0);PPC_STORE_U32(Fixture::map+56,0x16DF4FB6);
        std::memcpy(rt.pointer(Fixture::folder,4,true),"brt",4);
        std::memcpy(rt.pointer(Fixture::stream,8,true),"brt.str",8);
    };
    const auto unchangedGuest=[&](const auto& before){
        need(!std::memcmp(before.data(),rt.pointer(Fixture::owner,unsigned(before.size()),false),before.size()),
             "Bartman readiness manufactured guest world, scores or completion");
        need(completionCalls==originalCalls&&!rt.firstMissionCompletionTriggered&&
             !rt.firstMissionCompletionOwner&&!rt.firstMissionCompletionManager,
             "Bartman map-start gate inherited LOC completion dispatch");
    };
    const auto arm=[&]{
        std::array<uint8_t,0x4000> before{};
        std::memcpy(before.data(),rt.pointer(Fixture::owner,unsigned(before.size()),false),before.size());
        f.arm();f.frame();unchangedGuest(before);
    };
    const auto rejectArm=[&](const char* why){
        const auto before=f.c;
        std::array<uint8_t,0x4000> guest{};
        std::memcpy(guest.data(),rt.pointer(Fixture::owner,unsigned(guest.size()),false),guest.size());
        rejects([&]{f.arm();},why);unchanged(f.c,before,"Rejected Bartman readiness leaked caller registers");
        unchangedGuest(guest);need(!rt.bartmanBeginsMapStarted,"Rejected Bartman map-start ended bootstrap skipping");
    };
    prepare();rt.bartmanBegins=false;PPC_STORE_U32(0x82D08C34,0);f.c.r31.u64=0xDEADBEEF;
    arm();need(!rt.bartmanBeginsMapStarted,"Inactive Bartman mode consumed a ready event");
    prepare();
    rejects([&]{SimpsonsNativeFirstMissionCompletionReady(f.c,base+1);},"Bartman readiness accepted a foreign runtime");
    f.c.r31.u64=Fixture::owner+4;rejectArm("Bartman readiness accepted a foreign original application owner");
    prepare();PPC_STORE_U32(Fixture::owner+12,8);rejectArm("Bartman readiness accepted active streaming");
    prepare();PPC_STORE_U8(Fixture::owner+17,0);rejectArm("Bartman readiness accepted an unready application");
    prepare();PPC_STORE_U32(0x82D08C34,0);f.c.r31.u64=0;rejectArm("Bartman readiness accepted an absent original owner");
    struct WordCase {uint32_t address,value;};
    const std::array wrongMaps{
        WordCase{0x82D08BA8,0},WordCase{Fixture::manager,0},
        WordCase{Fixture::manager+160,0},WordCase{Fixture::manager+164,0},
        WordCase{Fixture::manager+172,0},WordCase{Fixture::manager+168,Fixture::movie},
        WordCase{Fixture::episode+32,0x629E5EE8},WordCase{Fixture::mode+8,1},
        WordCase{Fixture::mode+32,0x66098358},WordCase{Fixture::map+8,2},
        WordCase{Fixture::map+56,0},WordCase{Fixture::map+20,0},WordCase{Fixture::map+28,0},
        WordCase{Fixture::map+36,0x19E30557},WordCase{Fixture::map+40,0x45CE48A2},
        WordCase{Fixture::map+44,0x977A3DEC},WordCase{Fixture::map+48,0x2E8D53D1}};
    for(const auto test:wrongMaps){
        prepare();PPC_STORE_U32(test.address,test.value);arm();
        need(!rt.bartmanBeginsMapStarted,"Wrong/challenge Bartman package ended bootstrap skipping");
    }
    for(const auto address:{Fixture::folder,Fixture::stream}){
        prepare();PPC_STORE_U8(address,'B');arm();
        need(!rt.bartmanBeginsMapStarted,"Different authored Bartman stream spelling qualified");
    }
    prepare();arm();need(rt.bartmanBeginsMapStarted,"Genuine authored Bartman map-start did not end bootstrap skipping");
    // Subsequent original movie updates must retain manual Start controls. This
    // exercises the real policy's negative path, with no movie/decoder callbacks
    // or physical controller poll. Positive stop/destructor behavior has its own
    // original movie integration fixture.
    rt.controllerSource()->beginMovie(Fixture::movie);
    for(const auto state:{2u,3u,6u}){
        PPC_STORE_U32(Fixture::movie+20,state);f.c.r3.u64=Fixture::movie;
        f.preserved([&]{need(!SimpsonsNativeMovieSkip(f.c,base),"Later Bartman cutscene inherited bootstrap auto-skip");},
                    "Later Bartman movie policy leaked the original caller ABI");
        need(PPC_LOAD_U32(Fixture::movie+20)==state,"Manual-only movie policy changed original playback state");
    }
    rt.controllerSource()->endMovie();
    // The flag is a one-shot startup boundary, not an ongoing map-pointer test.
    // Original map exit/restart must not re-enable automatic later-movie skips.
    PPC_STORE_U32(0x82D08C34,0);PPC_STORE_U32(Fixture::manager+172,0);f.c.r31.u64=0xDEADBEEF;
    arm();need(rt.bartmanBeginsMapStarted,"Original Bartman map retirement re-enabled bootstrap skipping");
    rt.bartmanBegins=false;rt.bartmanBeginsMapStarted=false;
    std::puts("PASS authored Bartman standard-map gates / nil challenge checkpoint / monotonic ready boundary / later movie preservation / no LOC completion / whole caller ABI");
}
}

// Only the world-dependent completion body is observed. The production helper
// above still executes its three original PPC instructions and original call.
PPC_FUNC(sub_8289EA10){
    need(active&&base==active->base,"Completion observer received a foreign runtime");
    need(&ctx!=incomingContext&&currentContext==&ctx,"Completion did not enter an isolated original callback context");
    observedManager=ctx.r3.u32;observedHelper=ctx.lastFunction;
    need(observedManager==expectedManager,"Retail helper did not load the live gameflow manager");
    need(observedHelper==0x8296FFC8,"Shortcut bypassed the actual retail EpisodeComplete helper");
    ++completionCalls;
    // Deliberately clobber call registers. These effects belong to the temporary
    // original callback context and must never leak into the caller's clock.
    ctx.r3.u64=0xFEDCBA9876543210ull;ctx.r4.u64=0x123456789ABCDEF0ull;
    ctx.r14.u64=0x777788889999AAAAull;ctx.r31.u64=0x3333444455556666ull;
    ctx.cr0.lt=true;ctx.lr=0x0123456789ABCDEFull;ctx.fpscr.csr=0x3F80;
}

int main(int argc,char** argv){try{
    need(argc==2||argc==3,"Original image and optional stage required");
    if(argc==3)need(isAuditStage(argv[2]),"Unknown independent audit stage");
    auto noRuntime=caller();
    rejects([&]{SimpsonsNativeFirstMissionCompletionReady(noRuntime,nullptr);},"Ready hook accepted absent runtime");
    rejects([&]{firstMissionCompletionFrame(noRuntime,nullptr);},"Completion frame accepted absent runtime");
    Runtime rt;rt.load(argv[1]);rt.map(0x10000,0x10000,true,"Completion CPU fixture stack");
    rt.map(0x50000,0x10000,true,"Completion CPU fixture owners");
    if(argc==3)auditStageReadiness(rt,argv[2]);
    else {originalEvidence(rt.base);startup(rt);originalNames(rt);readiness(rt);dispatch(rt);bartmanReadiness(rt);auditStageReadiness(rt);const auto auditPath=auditLifecycle(rt);auditEarlyMapRequest(rt,auditPath);auditRetirementAttribution(rt,auditPath);}
    std::printf("PASS first mission completion / Bartman stage CPU qualification: %zu checks\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL completion fixture after %zu checks: %s\n",checks,e.what());return 1;}}
