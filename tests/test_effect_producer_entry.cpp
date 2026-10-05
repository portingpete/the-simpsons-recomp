// Malformed native activation attempts derived at the genuine public mono
// caller. The valid continuation always forwards the generated original body.
#define MONO_IMMEDIATE_NO_MAIN
#include "test_mono_immediate_pass.cpp"
#undef MONO_IMMEDIATE_NO_MAIN
#include <chrono>
#include <fstream>

extern "C" void __imp__sub_826B5FC0(PPCContext&,uint8_t*);
namespace {
bool probeEnabled{},probeComplete{};
size_t earlyRejections{};
void preservedObservation(EngineEffects& effects,PPCContext& c,uint8_t* base) {
    std::array<uint8_t,sizeof(PPCContext)> before{};std::memcpy(before.data(),&c,sizeof(c));
    const auto savedCsr=PPCFPSCRRegister::getcsr();const auto savedError=GetLastError();
    const auto probeCsr=(savedCsr&~0x6000u)|0x4000u;
    PPCFPSCRRegister::restoreHostCSR(probeCsr);SetLastError(0xA17D09E3);
    effects.observeProducerEntry(c,base,"fixture_preservation_probe");
    const auto returnedCsr=PPCFPSCRRegister::getcsr();const auto returnedError=GetLastError();
    PPCFPSCRRegister::restoreHostCSR(savedCsr);SetLastError(savedError);
    need(returnedCsr==probeCsr&&returnedError==0xA17D09E3,
         "Entry observation changed host rounding/FP status or LastError");
    need(!std::memcmp(before.data(),&c,sizeof(c)),"Entry observation changed a PPC field or trace");
}
void rejectionAtOriginalEntry(PPCContext& c,uint8_t* base) {
    auto& rt=*active;auto& driver=*rt.engineDriver;auto& effects=driver.effects();
    need(uint32_t(c.lr)==0x8273A93C&&c.r30.u32&&c.r3.u32&&c.r4.u32,
         "Early-rejection probe did not reach the genuine original mono caller");
    const auto saved=c;
    // This is the same host trace operation at the intercepted function's
    // actual entry. Restore it before forwarding the generated body, whose
    // normal prologue records the entry exactly once for the valid call.
    PPC_TRACE_ENTRY(c,0x826B5FC0);
    const auto wrapper=c.r4.u32,manager=c.r3.u32;
    const auto wrapperBefore=snapshot(rt,wrapper,0x30),managerBefore=snapshot(rt,manager,0x20);
    const auto bound=driver.cameraBinding();const auto colorBefore=driver.readbackColor(bound.colorIdentity);
    const auto draws=effects.monoMeshDrawCount();
    const auto reject=[&](const char* action,const char* expected) {
        rt.resourceAudit.action(action);preservedObservation(effects,c,base);
        std::array<uint8_t,sizeof(PPCContext)> before{};std::memcpy(before.data(),&c,sizeof(c));
        bool failed=false;
        try {effects.beginEdge(c,base);}catch(const Failure& error) {
            failed=true;need(std::string_view(error.what()).find(expected)!=std::string_view::npos,
                            "Malformed entry rejected at a different frontier");
            rt.resourceAudit.failure(error.what());++earlyRejections;
        }
        need(failed,"Malformed early activation was accepted");
        need(!std::memcmp(before.data(),&c,sizeof(c)),"Early activation rejection changed PPC state");
        need(effects.monoMeshDrawCount()==draws&&driver.readbackColor(bound.colorIdentity)==colorBefore,
             "Early activation rejection changed native draws or pixels");
    };
    {Restore owner(rt,wrapper+0x10,4);PPC_STORE_U32(wrapper+0x10,0x0050FFFF);
        reject("probe-owner","Unknown or stale native FX identity");}
    same(rt,wrapper,wrapperBefore,"Rejected owner changed its wrapper");
    {Restore camera(rt,packet+8,4);PPC_STORE_U32(packet+8,0);
        reject("probe-camera","Original mono camera differs");}
    const auto technique=c.r5.u64;c.r5.u32=0x0013FFFC;
    reject("probe-technique","Unqualified original mono activation");c.r5.u64=technique;
    const auto incomingWrapper=c.r4.u64;c.r4.u32=0xFFFFFFFC;
    reject("probe-unreadable-owner","");c.r4.u64=incomingWrapper;
    {
        // Observe an unreadable metadata address independently, then restore
        // the genuine packet before any original validation or continuation.
        Restore packetMetadata(rt,packet,4);PPC_STORE_U32(packet,0xFFFFFFFC);
        rt.resourceAudit.action("probe-unreadable-metadata");preservedObservation(effects,c,base);
    }
    need(PPC_LOAD_U32(packet)==metadata,"Metadata observation changed the original packet");
    same(rt,wrapper,wrapperBefore,"Early probes changed the original wrapper");
    same(rt,manager,managerBefore,"Early probes changed the original manager");
    std::memcpy(&c,&saved,sizeof(c));
    rt.resourceAudit.action("later-valid-original-draw");probeComplete=true;
}
void verifyReceipts(const std::filesystem::path& path) {
    std::ifstream input(path);need(bool(input),"Entry receipt log was not preserved");
    std::string line;std::array<bool,4> seen{};bool laterValid=false,unreadable=false,unreadableMetadata=false;
    size_t failures=0;
    while(std::getline(input,line)) {
        if(line.find("\"event\":\"failure\"")!=std::string::npos) {
            ++failures;
            need(line.find("\"kind\":\"effect_producer_entry\"")!=std::string::npos&&
                 line.find("\"asset\":\"source:8211F480\"")!=std::string::npos&&
                 line.find("\"caller\":2188618044")!=std::string::npos&&
                 line.find("\"mission\":\"fixture\"")!=std::string::npos&&
                 line.find("boundary=mono_begin")!=std::string::npos&&
                 line.find("submesh_count=2 bones=0 submesh_owner=unobserved")!=std::string::npos&&
                 line.find("argument5_role=technique")!=std::string::npos&&
                 line.find("admission=unvalidated")!=std::string::npos,
                 "Early failure lost its pre-validation original combination/context");
            constexpr std::array actions{"probe-owner","probe-camera","probe-technique","probe-unreadable-owner"};
            for(size_t i=0;i<actions.size();++i)if(line.find(std::string("\"last_action\":\"")+actions[i]+"\"")!=std::string::npos)seen[i]=true;
            if(line.find("probe-technique")!=std::string::npos)need(line.find("requested_technique=0013FFFC")!=std::string::npos&&
                 line.find("selection_state=unresolved")!=std::string::npos,"Unknown requested technique was hidden");
            if(line.find("probe-camera")!=std::string::npos)need(line.find("camera_matches_global=0")!=std::string::npos,
                 "Malformed camera identity was hidden");
            if(line.find("probe-unreadable-owner")!=std::string::npos)unreadable=
                line.find("r4=FFFFFFFC")!=std::string::npos&&line.find("unreadable_mask=00000002")!=std::string::npos;
        }
        if(line.find("\"event\":\"encounter\"")!=std::string::npos&&line.find("probe-unreadable-metadata")!=std::string::npos)
            unreadableMetadata=line.find("metadata=FFFFFFFC")!=std::string::npos&&
                line.find("submesh_count=0 bones=0 submesh_owner=unreadable unreadable_mask=0003CE00")!=std::string::npos&&
                line.find("admission=unvalidated")!=std::string::npos;
        if(line.find("\"event\":\"encounter\"")!=std::string::npos&&
           line.find("later-valid-original-draw")!=std::string::npos&&
           line.find("\"kind\":\"effect_pass\"")!=std::string::npos&&line.find("vs=82120C04 ps=82122BD4")!=std::string::npos)
            laterValid=true;
    }
    need(failures==4&&earlyRejections==4&&std::all_of(seen.begin(),seen.end(),[](bool b){return b;}),
         "Independent early failures were omitted or merged");
    need(unreadable&&unreadableMetadata&&laterValid,"Unreadable owner/metadata or later valid original pass was hidden");
}
}
PPC_FUNC(sub_826B5FC0) {
    if(probeEnabled&&!probeComplete&&uint32_t(ctx.lr)==0x8273A93C)rejectionAtOriginalEntry(ctx,base);
    __imp__sub_826B5FC0(ctx,base);
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==3,"Original image and receipt directory required");
        const auto receipt=std::filesystem::path(argv[2])/("effect-producer-entry-"+std::to_string(GetCurrentProcessId())+"-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
        Runtime rt;rt.load(argv[1]);rt.resourceAudit.configure(receipt);rt.resourceAudit.mission("fixture");
        PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original producer-entry graphics startup missing");
        probeEnabled=true;immediateMono(rt,entry,false);probeEnabled=false;
        need(probeComplete,"Whole original public path omitted the entry probe");verifyReceipts(receipt);
        std::printf("AUDIT_EFFECT_PRODUCER_ENTRY source=8211F480 original_wrapper=8273B4D0 original_entry=826B5FC0 original_caller=8273A93C early_owner=passed early_camera=passed early_technique=passed unreadable_owner=passed ppc_preserved=passed csr_preserved=passed last_error_preserved=passed later_original_draw=passed original_retirement=passed\n");
        std::printf("PASS original effect producer entry: %zu checks; four early failures retain exact inputs and a later whole original draw/retirement succeeds; receipts=%s\n",
            checks,receipt.string().c_str());return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original effect producer entry:%zu checks stage=%s:%s\n",checks,stage,error.what());return 1;}
}
