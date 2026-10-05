// One canonical original FX case per process, original setup/metadata use/retire.
// Null-callback particles retains its required canonical predecessor in the
// same registrar call because retail reuses r29 across rows.
// Unsupported draws never stop later CTest cases. This is setup-only evidence.
#include "effect_catalog_lifecycle_helpers.h"
#include "renderer/effect_reflection.h"
#include <charconv>

namespace {
uint32_t originalRow(uint32_t row) {return row<25?table+16*row:0x82CD1448+16*(row-25);}

template<class F> void rejectsMetadata(F&& f,const char* message) {
    try {f();}catch(const Graphics::EffectError&){++checks;return;}
    need(false,message);
}

void setup(Runtime& rt,EngineCpuCalls& cpu,uint32_t row) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();
    const auto beforeAbi=abi(cpu.registers());const auto context=PPC_LOAD_U32(0x82D5DA74);
    const auto rowAddress=originalRow(row),source=PPC_LOAD_U32(rowAddress),name=PPC_LOAD_U32(rowAddress+4);
    const auto effectName=stringAt(rt,name);const auto rasterBaseline=driver.rasterCount();
    const auto firstTable=snapshot(rt,table,400),secondTable=snapshot(rt,0x82CD1448,384);
    const auto original=snapshot(rt,source,PPC_LOAD_U32(source+4)+12);
    need(!effects.count() && !driver.quadDeclarations().count() && !driver.shadowTextures().count(),
         "Independent effect case did not start with empty native owners");
    need(!PPC_LOAD_U32(rowAddress+8) && !PPC_LOAD_U32(0x82D08BFC),"Independent original row/manager already published");
    stage="original manager construction";
    const uint32_t options=cpu.registers().r1.u32+0x60;
    PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    const auto manager=cpu.invoke(0x8269BF70,0x260,options);
    need(manager && cpu.invoke(0x826B6F60,manager,context)==manager,"Original manager construction failed");

    // Genuine first shared producer populates the root-owned schema. Its
    // wrapper is retired before the target case; no unrelated target stays live.
    // Native attachShared requires this real original producer, not a fabricated
    // copied pool or a direct native create call with a spoofed original caller.
    stage="original shared producer registration and retirement";
    need(PPC_LOAD_U32(table+16)==0x820D5730,"Original shared producer row changed");
    need(cpu.invoke(0x827019E8,table+16,1)==0 && effects.count()==1,"Original shared producer registration failed");
    const auto sharedId=PPC_LOAD_U32(PPC_LOAD_U32(table+24)+16);
    need(sharedId && effects.view(sharedId).source==0x820D5730,"Shared producer lost canonical original identity");
    cpu.invoke(0x82701118,table+16,1);
    need(!effects.count() && !PPC_LOAD_U32(table+24),"Original shared producer wrapper survived retirement");
    rejects([&]{effects.view(sharedId);},"Retired shared producer remained queryable");
    const auto pool=PPC_LOAD_U32(root),backing=rt.effectPoolBacking;
    need(pool && backing && PPC_LOAD_U32(pool+0x180)==backing && PPC_LOAD_U32(pool+0x188)==1,
         "Original shared root/backing lifetime differs");

    stage="independent canonical original row registration";
    // Original827019E8 initializes r29=0. A null callback branches from
    // 82701AD8 to82701B20 and retains the preceding row's typed owner via
    // its vtable+4. Retail particles therefore requires original row1 in
    // this same invocation; beginning at row2 alone is not valid original
    // input. Keep the genuine two-row producer/cleanup sequence intact.
    const uint32_t registrationStart=row==2?table+16:rowAddress,registrationCount=row==2?2u:1u;
    need(cpu.invoke(0x827019E8,registrationStart,registrationCount)==0 && effects.count()==registrationCount,
         "Independent original row registration failed");
    const auto wrapper=PPC_LOAD_U32(rowAddress+8);
    need(wrapper!=0,"Independent original row did not publish its wrapper");
    const auto id=PPC_LOAD_U32(wrapper+16);const auto view=effects.view(id);
    need(id && id!=sharedId && !rt.pageAccess[id>>12].load() && view.wrapper==wrapper &&
         view.source==source && view.manager==manager && view.pool==pool && view.phase==EngineEffects::Phase::Reflected,
         "Independent effect lost canonical native ownership/provenance");
    const auto owned=effects.originalBytes(id);
    need(owned.size()==original.size() && owned.data()!=rt.pointer(source,uint32_t(owned.size()),false) &&
         std::equal(owned.begin(),owned.end(),original.begin()),"Independent effect borrowed/changed original bytes");
    stage="original technique/name queries and copied state-cache use";
    cache(rt,cpu,id,source+12);queries(rt,cpu,id,source+12);
    need(!effects.technique(id,"__missing_fx_technique__"),"Unknown original technique was fabricated");
    const auto typed=cpu.invoke(0x826B7088,manager,name);
    need((typed!=0)==(PPC_LOAD_U32(rowAddress+12)!=0),"Independent original typed callback differs");

    stage="original typed finalizer metadata use";
    if(typed) {
        const auto vtable=PPC_LOAD_U32(typed),finalizer=PPC_LOAD_U32(vtable+12);
        need(finalizer!=0,"Original typed finalizer slot is empty");
        const auto before=effects.typedReflectionCount(id);
        cpu.invoke(finalizer,typed);
        need(effects.typedReflectionCount(id)==before+(row!=0),"Independent original finalizer reflection count differs");
    }

    stage="all selected-technique metadata bindings";
    Graphics::EffectRecord metadata(source,owned);size_t bindings=0;
    for(const auto& technique:metadata.techniques()) {
        need(effects.technique(id,technique.name)==technique.handle,"Selected original technique handle differs");
        for(bool shared:{false,true}) for(const auto& parameter:metadata.parameters(shared)) {
            const auto binding=Graphics::effectPassBinding(metadata,technique.handle,parameter.handle);
            const uint32_t leaf=(parameter.handle>>1)&0x1FFFF,ns=parameter.handle&1;
            const uint32_t f=source+12,originalContext=f+technique.contextOffset;
            uint32_t usage=0;
            for(uint32_t lane=0;lane<8;++lane) {
                const auto mask=f+PPC_LOAD_U32(originalContext+32*ns+4*lane)+8*(leaf/64);
                usage|=uint32_t((PPC_LOAD_U64(mask)>>(63-leaf%64))&1)<<lane;
            }
            need(binding.handle==parameter.handle && binding.usage==usage,"Metadata binding differs from original selected-pass mask");
            need(bool(binding.lanes[0])==bool(usage&0x55) && bool(binding.lanes[1])==bool(usage&0xAA),
                 "Metadata binding invented or omitted an original used lane");
            ++bindings;
        }
    }
    // Malformed copies are independent of the genuine guest image. Rejection
    // must leave the original owner usable and its original bytes untouched.
    stage="malformed metadata and stale-identity rejection";
    auto changed=original;changed[0]^=1;
    rejectsMetadata([&]{Graphics::EffectRecord bad(source,changed);},"Malformed original FX envelope was accepted");
    rejectsMetadata([&]{Graphics::EffectRecord bad(source,std::span<const uint8_t>(original.data(),original.size()-1));},
                    "Truncated original FX envelope was accepted");
    if(!metadata.parameters(false).empty()) {
        const auto handle=metadata.parameters(false)[0].handle;
        rejectsMetadata([&]{Graphics::effectPassBinding(metadata,UINT32_MAX,handle);},"Unknown selected technique accepted");
    }
    rejects([&]{effects.view(id^0x80000000u);},"Unowned native effect identity was accepted");
    need(effects.count()==registrationCount && effects.view(id).source==source,"Rejected metadata damaged the original owner");
    same(rt,source,original,"Rejected metadata mutated the original guest source");

    stage="original target retirement and stale use rejection";
    cpu.invoke(0x82701118,registrationStart,registrationCount);
    need(!effects.count() && !PPC_LOAD_U32(rowAddress+8) && !cpu.invoke(0x826B7088,manager,name),
         "Independent original target retirement retained publication/typed ownership");
    rejects([&]{effects.view(id);},"Retired independent effect view remained usable");
    rejects([&]{effects.originalBytes(id);},"Retired independent effect bytes remained borrowable");
    rejects([&]{effects.parameter(id,"__stale_parameter__");},"Retired independent effect answered a parameter query");
    effects.requireReleased();effects.requirePoolReleased(pool);
    driver.quadDeclarations().requireReleased();driver.shadowTextures().requireReleased();
    need(!driver.quadDeclarations().count() && !driver.shadowTextures().count() && !PPC_LOAD_U32(0x82D099A0),
         "Independent original target retirement retained declaration/texture ownership");
    need(driver.rasterCount()==rasterBaseline+(row==4?4u:0u),
         "Original shadow camera teardown behavior changed without separate proof");
    same(rt,table,firstTable,"Independent target altered first original registration table");
    same(rt,0x82CD1448,secondTable,"Independent target altered second original registration table");
    same(rt,source,original,"Independent target altered original envelope");
    need(PPC_LOAD_U32(root)==pool && rt.effectPoolBacking==backing && PPC_LOAD_U32(pool+0x188)==1,
         "Independent target retired the root-owned original shared pool");
    stage="original manager destruction";
    need(cpu.invoke(0x826B7600,manager,1)==manager && !PPC_LOAD_U32(0x82D08BFC),"Original manager destruction failed");
    need(abi(cpu.registers())==beforeAbi && !PPC_LOAD_U32(0x82D0CAF8) && PPC_LOAD_U32(0x82D5DA74)==context,
         "Independent original lifetime changed nonvolatile ABI/context or created a console device");
    std::printf("AUDIT_EFFECT_SETUP row=%u source=%08X name=%s techniques=%zu bindings=%zu create=passed metadata_use=passed release=passed malformed=passed draw=unproven shadow_camera_rasters_retained=%u\n",
        row,source,effectName.c_str(),metadata.techniques().size(),bindings,row==4?4u:0u);
}
}

int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==3,"Original image and independent row ordinal0..48 required");
        uint32_t row=0;const auto* end=argv[2]+std::strlen(argv[2]);const auto parsed=std::from_chars(argv[2],end,row);
        need(parsed.ec==std::errc{} && parsed.ptr==end && row<49,"Invalid independent original row ordinal");
        Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);
        try {
            const auto entry=original;bool observed=false;rt.graphicsStartupObserver=observeGraphics;
            try {runOriginal(original,rt.base);}catch(const Observed&){observed=true;}
            rt.graphicsStartupObserver={};need(observed,"Original graphics startup boundary not reached");
            EngineCpuCalls cpu(entry,rt.base);setup(rt,cpu,row);
            std::printf("PASS independent original effect setup: row=%u checks=%zu; setup/query/finalizer/metadata-binding/retire only; ALL MUTED; draw and complete shadow camera teardown unproven\n",row,checks);
        }catch(const std::exception& error) {
            std::fprintf(stderr,"Independent FX fixture failed before Runtime teardown: row=%u stage=%s checks=%zu error=%s\n",
                         row,stage,checks,error.what());throw;
        }
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL independent original FX setup: %s\n",error.what());return 1;}
}
