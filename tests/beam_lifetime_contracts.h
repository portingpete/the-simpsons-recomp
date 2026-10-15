#pragma once
// Included at global scope after the existing driver fixture namespace closes.
// Positive receipts come only from whole original activation/factory/constructors.
extern "C" void __imp__sub_8274C5A8(PPCContext&,uint8_t*);
extern "C" void __imp__sub_827502E8(PPCContext&,uint8_t*);

namespace {
struct BeamFixtureProviderReady {};
struct BeamFixtureProviderStartup {
    Simpsons::Runtime& runtime;
    uint8_t* base;
    PPCContext* context;
    uint32_t provider{},receipt{};
    uint64_t presentations{};
    bool observed{};
};
BeamFixtureProviderStartup* beamFixtureProviderStartup{};
struct BeamFixtureProviderObservation {
    explicit BeamFixtureProviderObservation(BeamFixtureProviderStartup& value) {
        require(!beamFixtureProviderStartup,"Nested Beam original-provider startup observation");
        beamFixtureProviderStartup=&value;
    }
    ~BeamFixtureProviderObservation(){beamFixtureProviderStartup=nullptr;}
};
struct BeamFixtureWitness {
    Simpsons::Runtime& runtime;
    uint8_t* base;
    uint32_t provider{},vtable{},allocate{},release{};
    PPCFunc* allocateBody{};
    PPCFunc* releaseBody{};
    PPCFunc* deleteBody{};
    std::optional<Simpsons::NativeBeamLifetime> expected;
    bool nullNext{};
    bool failedConstructor{};
    uint32_t suppliedNull{},allocations{},releases{},retired{},closing{},closed{};
    uint32_t lastAllocated{},lastReleased{};
};
BeamFixtureWitness* beamFixtureWitness{};

void beamFixtureRejectsLifetime(Simpsons::EngineDriver& driver,const Simpsons::NativeBeamLifetime& receipt) {
    ++checks;
    try {driver.requireBeamLifetime(receipt);}
    catch(const std::exception& error) {
        require(std::strcmp(error.what(),"Original Beam lost its genuine captured typed construction lifetime")==0,
                "Beam stale receipt did not reject at the pre-read typed lifetime predicate");
        return;
    }
    throw Simpsons::Failure("Beam stale typed lifetime was accepted");
}
void observeBeamFixtureAllocation(PPCContext& c,uint8_t* base) {
    auto& w=*beamFixtureWitness;
    if(base==w.base&&c.r3.u32==w.provider&&c.r4.u32==208&&c.r31.u32==0x82CF26C4&&c.lr==0x8274D574) {
        require(Simpsons::currentContext==&c&&c.r5.u32==c.r1.u32+0x50&&
                PPC_LOAD_U32(c.r5.u32)==2&&PPC_LOAD_U32(c.r5.u32+4)==16&&!PPC_LOAD_U32(c.r5.u32+8),
                "Beam fixture allocation is not the genuine factory208/16 provider call");
        if(w.nullNext) {
            w.nullNext=false;++w.suppliedNull;c.r3.u32=0;
            return; // Negative-only provider failure; no positive allocation/receipt is supplied.
        }
        w.allocateBody(c,base);++w.allocations;w.lastAllocated=c.r3.u32;
        require(c.r3.u32&&!(c.r3.u32&15),"Real Beam provider failed its positive208-byte aligned allocation");
        return;
    }
    w.allocateBody(c,base);
}
void observeBeamFixtureRelease(PPCContext& c,uint8_t* base) {
    auto& w=*beamFixtureWitness;
    const uint32_t storage=c.r4.u32;
    const bool ours=base==w.base&&c.r3.u32==w.provider&&storage&&storage==w.lastAllocated;
    if(ours) {
        require(Simpsons::currentContext==&c&&!c.r5.u32,
                "Beam fixture free lost the actual provider ABI");
        if(w.expected) {
            const auto actual=w.runtime.engineDriver->observedBeamLifetime(storage);
            require(c.lr==0x8275A9E0&&actual&&actual->generation==w.expected->generation&&
                    actual->context==w.expected->context&&actual->phase==Simpsons::NativeBeamLifetimePhase::Closed,
                    "Real generic Beam free lacks the same genuine Closed receipt");
            beamFixtureRejectsLifetime(*w.runtime.engineDriver,*w.expected);
            ++w.closed;
        } else if(w.failedConstructor) {
            const auto actual=w.runtime.engineDriver->observedBeamLifetime(storage);
            require(c.lr==0x82774504&&(!actual||actual->phase!=Simpsons::NativeBeamLifetimePhase::Constructed),
                    "Non-null failed Beam constructor acquired a successful typed birth");
        } else throw Simpsons::Failure("Unclassified owned Beam release");
    }
    w.releaseBody(c,base); // Every actual allocation is freed by the original provider exactly once.
    if(ours) {++w.releases;w.lastReleased=storage;} // Metadata only; freed storage is not read.
}
void observeBeamFixtureFactoryDelete(PPCContext& c,uint8_t* base) {
    auto& w=*beamFixtureWitness;
    if(base==w.base&&w.expected&&c.r3.u32==0x82CF26C4&&c.r4.u32==w.expected->owner) {
        require(c.lr==0x8275A9D8&&Simpsons::currentContext==&c,
                "Beam deletion is not the original retirement's actual virtual callback");
        const auto actual=w.runtime.engineDriver->observedBeamLifetime(c.r4.u32);
        require(actual&&actual->generation==w.expected->generation&&actual->context==w.expected->context&&
                actual->phase==Simpsons::NativeBeamLifetimePhase::Retired,
                "Real Beam virtual deletion was not preceded by genuine logical retirement");
        beamFixtureRejectsLifetime(*w.runtime.engineDriver,*w.expected);++w.retired;
    }
    w.deleteBody(c,base);
}
struct BeamFixtureObservation {
    BeamFixtureWitness& witness;
    explicit BeamFixtureObservation(BeamFixtureWitness& w):witness(w) {
        auto* base=w.base;
        require(!beamFixtureWitness&&w.allocate&&w.release&&w.allocate!=w.release&&
                w.allocate!=0x82775E40&&w.release!=0x82775E40,
                "Nested or aliased Beam fixture callback ownership");
        w.allocateBody=PPC_LOOKUP_FUNC(base,w.allocate);
        w.releaseBody=PPC_LOOKUP_FUNC(base,w.release);
        w.deleteBody=PPC_LOOKUP_FUNC(base,0x82775E40);
        require(w.allocateBody&&w.releaseBody&&w.deleteBody,"Real Beam provider/delete callback is unavailable");
        beamFixtureWitness=&w;
        PPC_LOOKUP_FUNC(base,w.allocate)=observeBeamFixtureAllocation;
        PPC_LOOKUP_FUNC(base,w.release)=observeBeamFixtureRelease;
        PPC_LOOKUP_FUNC(base,0x82775E40)=observeBeamFixtureFactoryDelete;
    }
    ~BeamFixtureObservation() {
        auto* base=witness.base;
        PPC_LOOKUP_FUNC(base,witness.allocate)=witness.allocateBody;
        PPC_LOOKUP_FUNC(base,witness.release)=witness.releaseBody;
        PPC_LOOKUP_FUNC(base,0x82775E40)=witness.deleteBody;
        beamFixtureWitness=nullptr;
    }
};
void observeBeamFixtureClosing(PPCContext& c,uint8_t* base) {
    auto* w=beamFixtureWitness;
    if(!w||base!=w->base||!w->expected||c.r3.u32!=w->expected->owner+0xC0||
       (c.lr!=0x82775F04&&c.lr!=0x82775CE4))return;
    std::array<uint8_t,sizeof(PPCContext)> before{};
    std::memcpy(before.data(),&c,sizeof(c));
    require(Simpsons::currentContext==&c,"Original Beam curve teardown changed its CPU context");
    const auto actual=w->runtime.engineDriver->observedBeamLifetime(w->expected->owner);
    require(actual&&actual->generation==w->expected->generation&&actual->context==w->expected->context&&
            actual->phase==Simpsons::NativeBeamLifetimePhase::Closing,
            "Actual Beam destructor did not publish Closing before original curve teardown");
    beamFixtureRejectsLifetime(*w->runtime.engineDriver,*w->expected);
    require(!std::memcmp(before.data(),&c,sizeof(c)),"Closing observation altered original CPU/FP/vector state");
    ++w->closing;
}
// An allocator-owned input block, not an observed/adopted producer. No test map or forged bank receipt.
uint32_t allocateBeamFixtureInput(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,
                                 uint8_t* base,uint32_t bytes,std::vector<uint32_t>& owned) {
    cpu.registers().r31.u32=0; // This service call is not a Beam factory call.
    const uint32_t address=cpu.invoke(0x8274D530,bytes,16);
    require(address&&!(address&15)&&runtime.engineAudio,"Original FX provider did not allocate the test input");
    const auto span=runtime.engineAudio->allocationSpan(address);
    require(span&&span->generation&&address>=span->address&&
            uint64_t(address)+bytes<=uint64_t(span->address)+span->extent,
            "Actual fixture input is not wholly inside a live observed original bank");
    owned.push_back(address);std::memset(runtime.pointer(address,bytes,true),0,bytes);
    return address;
}
struct BeamFixtureAbi {
    std::array<uint64_t,18> integer,floating;
    uint32_t stack{};
    uint64_t link{};
    bool operator==(const BeamFixtureAbi&) const=default;
};
BeamFixtureAbi beamFixtureAbi(const PPCContext& c) {
    return {{c.r14.u64,c.r15.u64,c.r16.u64,c.r17.u64,c.r18.u64,c.r19.u64,c.r20.u64,c.r21.u64,c.r22.u64,
             c.r23.u64,c.r24.u64,c.r25.u64,c.r26.u64,c.r27.u64,c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64},
            {c.f14.u64,c.f15.u64,c.f16.u64,c.f17.u64,c.f18.u64,c.f19.u64,c.f20.u64,c.f21.u64,c.f22.u64,
             c.f23.u64,c.f24.u64,c.f25.u64,c.f26.u64,c.f27.u64,c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64},
            c.r1.u32,c.lr};
}
void temporalBeamLifetimeContracts(const char* image) {
    using namespace Simpsons;
    Runtime runtime;runtime.videoSettings.antialiasing=uint32_t(Graphics::Antialiasing::DLAA);
    runtime.load(image);PPCContext startup{};runtime.initialize(startup);
    const PPCContext entry=startup;auto* base=runtime.base;bool reached=false;
    BeamFixtureProviderStartup providerStartup{runtime,base,&startup};
    {BeamFixtureProviderObservation observation(providerStartup);
        try {runOriginal(startup,base);}catch(const BeamFixtureProviderReady&){reached=true;}}
    require(reached&&providerStartup.observed&&!beamFixtureProviderStartup&&runtime.engineDriver&&runtime.engineAudio&&
            providerStartup.presentations&&providerStartup.receipt&&providerStartup.provider==PPC_LOAD_U32(0x82DFE328),
            "Beam fixture did not reach genuine completed original provider initialization and native GPU completion");
    auto& driver=*runtime.engineDriver;EngineCpuCalls cpu(entry,base);
    const uint32_t provider=PPC_LOAD_U32(0x82DFE328);
    require(provider,"Original startup did not publish its actual FX provider");runtime.pointer(provider,4,false);
    const uint32_t vtable=PPC_LOAD_U32(provider);runtime.pointer(vtable,8,false);
    const uint32_t allocate=PPC_LOAD_U32(vtable),release=PPC_LOAD_U32(vtable+4);
    require(allocate&&release&&PPC_LOOKUP_FUNC(base,allocate)&&PPC_LOOKUP_FUNC(base,release),
            "Original FX provider lacks its real allocation/free callbacks");
    require(PPC_LOAD_U32(0x82CF26C4)==0x8215866C&&PPC_LOAD_U32(0x82CF26D4)==2&&
            !(PPC_LOAD_U32(0x82CF26D0)&0x80000000)&&PPC_LOAD_U32(0x82CF26DC)>PPC_LOAD_U32(0x82CF26D8),
            "Actual initialized Beam factory is disabled/full or has a different type");
    const uint32_t initialLive=PPC_LOAD_U32(0x82CF26D8);
    std::vector<uint32_t> inputAllocations;
    const uint32_t parent=allocateBeamFixtureInput(runtime,cpu,base,0xF0,inputAllocations);
    const uint32_t data=allocateBeamFixtureInput(runtime,cpu,base,0x100,inputAllocations);
    const uint32_t definition=allocateBeamFixtureInput(runtime,cpu,base,0x80,inputAllocations);
    const uint32_t record=parent+0xD0;
    PPC_STORE_U32(parent+8,data);PPC_STORE_U16(parent+0x70,1);
    for(uint32_t row=0;row<4;++row)for(uint32_t col=0;col<4;++col)
        PPC_STORE_U32(parent+0x20+16*row+4*col,std::bit_cast<uint32_t>(row==col?1.0f:0.0f));
    PPC_STORE_U32(parent+0x84,0x3F800000);PPC_STORE_U32(data+0x14,1);
    PPC_STORE_U32(data+0x34,PPC_LOAD_U32(0x8215D110)); // Exact activation time oracle from original literal.
    PPC_STORE_U32(data+0x10,data+0xC0);PPC_STORE_U32(data+0xC0,0xBADAADBA); // Original default lookup category.
    PPC_STORE_U32(record,0x20);PPC_STORE_U32(record+8,0x82CF26C4);PPC_STORE_U32(record+12,definition);
    PPC_STORE_U32(definition+0x2C,0x3F800000);
    PPC_STORE_U32(definition+0x58,0xE1E2E3E4); // Original constant curve variant, value0 at+5C.
    BeamFixtureWitness witness{runtime,base,provider,vtable,allocate,release};
    uint64_t firstGeneration{};uint32_t firstOwner{};
    {
        BeamFixtureObservation observation(witness);
        const auto activate=[&] {
            require(!PPC_LOAD_U32(record+4),"Fixture attempted original creation over a still-associated owner");
            const auto abi=beamFixtureAbi(cpu.registers());cpu.invoke(0x82758200,parent);
            require(beamFixtureAbi(cpu.registers())==abi,"Whole original Beam activation violated its nonvolatile/stack/FP ABI");
            return PPC_LOAD_U32(record+4);
        };
        const auto completed=[&](uint32_t owner) {
            const auto actual=driver.observedBeamLifetime(owner);
            require(owner&&owner==witness.lastAllocated&&actual&&actual->owner==owner&&actual->generation&&
                    actual->phase==NativeBeamLifetimePhase::Constructed&&actual->context==PPC_LOAD_U32(0x82D6D890)&&
                    actual->creationCaller==0x8275828C&&actual->parent==parent&&actual->record==record&&actual->index==0&&
                    actual->definition==definition&&!actual->texture&&!actual->raster&&
                    actual->allocator==provider&&actual->allocatorVtable==vtable&&actual->allocateCallback==allocate&&actual->freeCallback==release,
                    "Whole original activation/factory/ctor did not furnish the exact live typed association");
            require(driver.requireBeamLifetime(*actual)==*actual,"Original outer record publication did not qualify its typed birth");
            require(PPC_LOAD_U32(owner)==0x821586A8&&PPC_LOAD_U32(owner+0xA0)==definition&&
                    PPC_LOAD_U32(owner+0xC0)==0&&PPC_LOAD_U32(0x82CF26D8)==initialLive+1,
                    "Original Beam base/constant-curve/live-count completion differs");
            return *actual;
        };
        const auto retire=[&](const NativeBeamLifetime& value) {
            witness.expected=value;
            const auto retired=witness.retired,closing=witness.closing,closed=witness.closed,released=witness.releases;
            const auto abi=beamFixtureAbi(cpu.registers());cpu.invoke(0x827749F0,value.owner);
            require(beamFixtureAbi(cpu.registers())==abi,"Whole original Beam retirement violated its nonvolatile/stack/FP ABI");
            const auto ended=driver.observedBeamLifetime(value.owner);
            require(ended&&ended->generation==value.generation&&ended->context==value.context&&
                    ended->phase==NativeBeamLifetimePhase::Freed&&witness.retired==retired+1&&
                    witness.closing==closing+1&&witness.closed==closed+1&&witness.releases==released+1&&
                    witness.lastReleased==value.owner&&PPC_LOAD_U32(0x82CF26D8)==initialLive,
                    "Real Beam Retired/Closing/Closed/free chain did not complete once with the same typed birth");
            beamFixtureRejectsLifetime(driver,value); // Only host metadata after free; no freed-owner/definition dereference.
            PPC_STORE_U32(record+4,0); // Fixture-owned parent record is no longer associated; not a lifecycle event.
            witness.expected.reset();
        };
        auto first=completed(activate());firstGeneration=first.generation;firstOwner=first.owner;
        auto wrong=first;++wrong.generation;beamFixtureRejectsLifetime(driver,wrong);
        wrong=first;wrong.context^=1;beamFixtureRejectsLifetime(driver,wrong);
        wrong=first;wrong.owner=1;beamFixtureRejectsLifetime(driver,wrong);
        const auto mutateReject=[&](uint32_t address,uint32_t replacement) {
            const uint32_t saved=PPC_LOAD_U32(address);PPC_STORE_U32(address,replacement);
            try {rejects([&]{driver.requireBeamLifetime(first);});}catch(...){PPC_STORE_U32(address,saved);throw;}
            PPC_STORE_U32(address,saved);require(driver.requireBeamLifetime(first)==first,"Negative association mutation was not restored");
        };
        mutateReject(record+4,0);mutateReject(record+12,definition+16);
        mutateReject(parent+8,0);mutateReject(first.owner+0xA4,1);mutateReject(definition+0x2C,0);
        retire(first);

        witness.nullNext=true;const auto allocationsBefore=witness.allocations;
        require(!activate()&&witness.suppliedNull==1&&!witness.nullNext&&witness.allocations==allocationsBefore&&
                PPC_LOAD_U32(0x82CF26D8)==initialLive,"Original null allocation acquired a Beam or changed factory occupancy");
        auto second=completed(activate()); // A leaked pending null-allocation receipt would reject this same real stack/context call.
        require(second.generation>firstGeneration,"Genuine subsequent completed factory reused an old typed generation");
        beamFixtureRejectsLifetime(driver,first);retire(second);

        const std::array<uint8_t,16> missingName={'n','o','_','b','e','a','m','_','t','e','x','t','u','r','e',0};
        std::memcpy(runtime.pointer(definition,16,true),missingName.data(),16);witness.failedConstructor=true;
        const auto failedReleases=witness.releases,failedAllocations=witness.allocations;
        require(!activate()&&witness.allocations==failedAllocations+1&&witness.releases==failedReleases+1&&
                witness.lastAllocated&&witness.lastReleased==witness.lastAllocated&&PPC_LOAD_U32(0x82CF26D8)==initialLive,
                "Actual non-null failed Beam constructor did not return null and free its real storage once");
        const auto failed=driver.observedBeamLifetime(witness.lastAllocated);
        require(!failed||failed->phase!=NativeBeamLifetimePhase::Constructed,
                "Rejected non-null constructor was silently adopted as a Beam birth");
        std::memset(runtime.pointer(definition,16,true),0,16);witness.failedConstructor=false;
        auto third=completed(activate());require(third.generation>second.generation,
                "Failed constructor cleanup prevented a new genuine completed Beam birth");
        beamFixtureRejectsLifetime(driver,second);retire(third);
        require(witness.allocations==4&&witness.releases==4&&witness.suppliedNull==1&&
                witness.retired==3&&witness.closing==3&&witness.closed==3,
                "Bounded lifecycle fixture did not execute its exact real allocation/teardown schedule");
    }
    require(!beamFixtureWitness&&PPC_LOOKUP_FUNC(base,allocate)==witness.allocateBody&&
            PPC_LOOKUP_FUNC(base,release)==witness.releaseBody&&PPC_LOOKUP_FUNC(base,0x82775E40)==witness.deleteBody,
            "Beam fixture did not restore its scoped original callbacks");
    for(auto it=inputAllocations.rbegin();it!=inputAllocations.rend();++it)cpu.invoke(0x8274D588,*it);
    std::printf("PASS: genuine AA11 Beam original provider/activation/factory/base constructor,3 typed births, null allocation/non-null ctor rejection, current association/stale identity and3 real Retired/Closing/Closed/free chains / %zu checks; input association is fixture-built, no motion/world/recycle/reuse claim; first_owner=%08X first_birth=%llu\n",
                checks,firstOwner,static_cast<unsigned long long>(firstGeneration));
}
} // anonymous fixture namespace

// True original PPC symbol linkage: direct BL is not a lookup-table dispatch.
extern "C" void sub_8274C5A8(PPCContext& c,uint8_t* base) {
    observeBeamFixtureClosing(c,base);
    __imp__sub_8274C5A8(c,base); // Whole original curve teardown exactly once, including unrelated calls.
}

// Observe the genuine direct BL only after the complete original provider init.
// Passive for every other fixture/runtime/caller; never supplies provider inputs.
extern "C" void sub_827502E8(PPCContext& c,uint8_t* base) {
    auto* value=beamFixtureProviderStartup;
    const bool ours=value&&base==value->base&&Simpsons::active==&value->runtime&&
                    value->context==&c&&Simpsons::currentContext==&c&&c.lr==0x823B74BC;
    if(!ours){__imp__sub_827502E8(c,base);return;}
    const uint32_t provider=c.r3.u32;
    const auto originalAbi=beamFixtureAbi(c);
    __imp__sub_827502E8(c,base); // Whole registry/provider callback/factory initialization exactly once.
    std::array<uint8_t,sizeof(PPCContext)> returned{};
    std::memcpy(returned.data(),&c,sizeof(c));
    require(beamFixtureAbi(c)==originalAbi,
            "Genuine provider initialization changed original nonvolatile PPC ABI/SP/LR");
    require(!value->observed&&Simpsons::active==&value->runtime&&Simpsons::currentContext==&c&&
            value->runtime.engineDriver&&value->runtime.engineAudio&&provider&&
            PPC_LOAD_U32(0x82DFE328)==provider,
            "Original provider return did not publish the exact genuine input owner");
    value->runtime.pointer(provider,4,false);
    const uint32_t vtable=PPC_LOAD_U32(provider);value->runtime.pointer(vtable,12,false);
    require(PPC_LOAD_U32(vtable)&&PPC_LOAD_U32(vtable+4)&&PPC_LOAD_U32(vtable+8)&&
            PPC_LOOKUP_FUNC(base,PPC_LOAD_U32(vtable))&&PPC_LOOKUP_FUNC(base,PPC_LOAD_U32(vtable+4)),
            "Completed original provider lacks its actual live callbacks");
    auto& driver=*value->runtime.engineDriver;
    const uint64_t presentations=driver.frontCopyCount();
    const uint32_t receipt=PPC_LOAD_U32(0x82D0CF94);
    require(presentations&&receipt&&driver.waitSubmission(receipt),
            "Original provider startup lacks an actual front-copy receipt with completed GPU work");
    require(!std::memcmp(returned.data(),&c,sizeof(c)),
            "Provider-return observation altered complete original CPU/FP/vector bytes");
    value->provider=provider;value->receipt=receipt;value->presentations=presentations;value->observed=true;
    std::fprintf(stderr,"[BEAM FIXTURE PROVIDER READY] caller=823B74BC provider=%08X vtable=%08X front_copies=%llu receipt=%08X original_init_complete=1 gpu_complete=1\n",
                 provider,vtable,static_cast<unsigned long long>(presentations),receipt);
    throw BeamFixtureProviderReady{}; // Dedicated terminal startup boundary, only this scoped fixture.
}
