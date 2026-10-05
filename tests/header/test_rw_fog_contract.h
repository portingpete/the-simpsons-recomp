#pragma once
// Include inside test_engine_driver.cpp's anonymous namespace after require()
// and rejects(). No independent includes or test entry point are needed.
// Both fixtures call the real AOT dispatcher; neither calls a native hook or
// state commit directly. Source-reviewed only until the parent's FP fix lands.

struct RwCpuContractFixture {
    static constexpr uint32_t cacheBase=0x82D0D170,cacheSize=0x2FBC;
    static constexpr uint32_t pending=0x82D0F3B0,queue=0x82D0ED08,count=0x82D10114;
    Simpsons::Runtime& runtime;
    Simpsons::EngineCpuCalls& cpu;
    uint8_t* base;
    uint8_t* cacheBytes;
    uint8_t* capBytes;
    uint8_t* stackBytes;
    uint8_t* appliedBytes;
    const PPCContext incoming;
    const std::vector<uint8_t> savedCache,savedCaps,savedStack,appliedBefore;
    const Simpsons::Graphics::EngineState hostBefore;
    const Simpsons::EngineDriver* driverBefore;
    const uint32_t sp;

    RwCpuContractFixture(Simpsons::Runtime& rt,Simpsons::EngineCpuCalls& calls,uint8_t* memory)
        :runtime(rt),cpu(calls),base(memory),
         cacheBytes(rt.pointer(cacheBase,cacheSize,true)),
         capBytes(rt.pointer(0x82E3DFC4,4,true)),
         stackBytes(rt.pointer(calls.registers().r1.u32-0x70,0x70,true)),
         appliedBytes(rt.pointer(0x82E3D580,0x6A4,false)),
         incoming(calls.registers()),savedCache(cacheBytes,cacheBytes+cacheSize),
         savedCaps(capBytes,capBytes+4),savedStack(stackBytes,stackBytes+0x70),
         appliedBefore(appliedBytes,appliedBytes+0x6A4),hostBefore(rt.engineDriver->effectiveState()),
         driverBefore(rt.engineDriver.get()),
         sp(calls.registers().r1.u32) {}
    ~RwCpuContractFixture() noexcept {
        // Direct copies to already-checked backing also work during exception
        // unwinding; do not call a cancellation-sensitive guest accessor here.
        std::memcpy(cacheBytes,savedCache.data(),savedCache.size());
        std::memcpy(capBytes,savedCaps.data(),savedCaps.size());
        std::memcpy(stackBytes,savedStack.data(),savedStack.size());
        cpu.registers()=incoming;
    }
    RwCpuContractFixture(const RwCpuContractFixture&)=delete;
    RwCpuContractFixture& operator=(const RwCpuContractFixture&)=delete;

    static void put(std::vector<uint8_t>& bytes,uint32_t address,uint32_t value) {
        require(address>=cacheBase && uint64_t(address)+4<=uint64_t(cacheBase)+cacheSize,
                "RW fixture expected write is outside its saved region");
        for(uint32_t i=0;i<4;++i) bytes[address-cacheBase+i]=uint8_t(value>>(24-8*i));
    }
    std::vector<uint8_t> snapshot() const {return {cacheBytes,cacheBytes+cacheSize};}
    void reset(uint32_t caps=0) {
        std::memcpy(cacheBytes,savedCache.data(),savedCache.size());
        for(uint32_t i=0;i<425;++i) {
            PPC_STORE_U32(pending+8*i+4,0);
            PPC_STORE_U32(queue+4*i,0xD15EA5ED); // Catch extra queue writes.
        }
        PPC_STORE_U32(count,0);
        PPC_STORE_U32(0x82E3DFC4,caps);
    }
    void seedDirty(uint32_t id,uint32_t value=0) {
        const auto n=PPC_LOAD_U32(count);
        require(id<425 && n<425,"RW fixture dirty seed is out of bounds");
        PPC_STORE_U32(pending+8*id,value);PPC_STORE_U32(pending+8*id+4,1);
        PPC_STORE_U32(queue+4*n,id);PPC_STORE_U32(count,n+1);
    }
    void prepare(uint32_t lr) {
        cpu.registers()=incoming;
        auto& ctx=cpu.registers();
        const std::array<PPCRegister*,18> saved={&ctx.r14,&ctx.r15,&ctx.r16,&ctx.r17,&ctx.r18,&ctx.r19,
            &ctx.r20,&ctx.r21,&ctx.r22,&ctx.r23,&ctx.r24,&ctx.r25,&ctx.r26,&ctx.r27,&ctx.r28,&ctx.r29,&ctx.r30,&ctx.r31};
        for(uint32_t i=0;i<saved.size();++i) saved[i]->u64=0xA1B2C3D455660000ull+i;
        ctx.lr=lr;
    }
    void abi(uint32_t lr) {
        auto& ctx=cpu.registers();
        const std::array<const PPCRegister*,18> saved={&ctx.r14,&ctx.r15,&ctx.r16,&ctx.r17,&ctx.r18,&ctx.r19,
            &ctx.r20,&ctx.r21,&ctx.r22,&ctx.r23,&ctx.r24,&ctx.r25,&ctx.r26,&ctx.r27,&ctx.r28,&ctx.r29,&ctx.r30,&ctx.r31};
        bool intact=ctx.r1.u32==sp && ctx.lr==lr;
        for(uint32_t i=0;i<saved.size();++i) intact=intact && saved[i]->u64==0xA1B2C3D455660000ull+i;
        require(intact,"RW AOT dispatcher damaged saved GPRs, SP or LR");
    }
    void unchangedOwners() {
        require(std::memcmp(appliedBytes,appliedBefore.data(),appliedBefore.size())==0,
                "RW setter updated applied scalars before a commit");
        require(runtime.engineDriver.get()==driverBefore,"RW setter replaced its native driver owner");
        const auto& now=runtime.engineDriver->effectiveState();
        bool same=now.initialized()==hostBefore.initialized();
        for(const auto& field:Simpsons::Graphics::scalarStateEvidence())
            same=same && now.scalar(field.id)==hostBefore.scalar(field.id);
        for(uint32_t stage=0;stage<16;++stage)
            for(const auto& field:Simpsons::Graphics::samplerStateEvidence())
                same=same && now.sampler(stage,field.id)==hostBefore.sampler(stage,field.id);
        for(uint32_t target=0;target<4;++target)
            same=same && now.effectiveBlend(target)==hostBefore.effectiveBlend(target);
        require(same,"RW setter changed native-effective ownership/state before a commit");
    }
    void call(uint32_t selector,uint32_t value,uint32_t lr,const std::vector<uint8_t>& expected) {
        prepare(lr);
        const auto cap=PPC_LOAD_U32(0x82E3DFC4);
        require(cpu.invoke(0x824025A8,selector,value)==1,"RW original dispatcher lost its Boolean return");
        abi(lr);
        require(PPC_LOAD_U32(sp-0x70)==sp && PPC_LOAD_U32(sp-8)==lr &&
                PPC_LOAD_U64(sp-0x18)==0xA1B2C3D455660010ull && PPC_LOAD_U64(sp-0x10)==0xA1B2C3D455660011ull,
                "RW fixture did not execute the original dispatcher save/restore frame");
        require(snapshot()==expected,"RW AOT setter changed unexpected cache bytes or queue order");
        require(PPC_LOAD_U32(0x82E3DFC4)==cap,"RW setter changed its read-only capability word");
        unchangedOwners();
    }
    void reject(uint32_t selector,uint32_t value,uint32_t lr,const char* expectedPrefix) {
        prepare(lr);
        const auto before=snapshot(),stackBefore=std::vector<uint8_t>(stackBytes,stackBytes+0x70);
        const auto cap=PPC_LOAD_U32(0x82E3DFC4);
        bool preciseFailure=false;
        rejects([&] {
            try {cpu.invoke(0x824025A8,selector,value);}
            catch(const Simpsons::Failure& failure) {
                preciseFailure=std::string(failure.what()).starts_with(expectedPrefix);
                throw;
            }
        });
        require(preciseFailure && cpu.registers().lastFunction==0x824025A8,
                "RW rejection was not its intended entry preflight/guard");
        abi(lr);
        require(snapshot()==before && PPC_LOAD_U32(0x82E3DFC4)==cap &&
                std::memcmp(stackBytes,stackBefore.data(),stackBefore.size())==0,
                "Rejected RW request mutated CPU state or entered the AOT prologue");
        unchangedOwners();
    }
};

inline void rwFogContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    RwCpuContractFixture f(runtime,cpu,base);
    constexpr uint32_t cached=0x82D0E3E4,value=0x82D10060,dirty=0x82D10064,lr=0x82726870;
    auto reset=[&](uint32_t caps) {f.reset(caps);PPC_STORE_U32(cached,0);PPC_STORE_U32(value,0);};
    auto expected=[&](uint32_t c,uint32_t v,uint32_t d,uint32_t n,bool append=false) {
        auto e=f.snapshot();f.put(e,cached,c);f.put(e,value,v);f.put(e,dirty,d);
        f.put(e,f.count,n);
        if(append) f.put(e,f.queue+4*PPC_LOAD_U32(f.count),0x196);
        return e;
    };
    // A missing capability is successful original CPU behavior, not an enable.
    for(uint32_t caps:{0u,0x40u,0x200u,0xFFFFFE7Fu}) {
        reset(caps);f.seedDirty(0x38,2);
        f.call(14,1,lr,f.snapshot());
    }
    for(uint32_t caps:{0x80u,0x100u,0x180u}) {
        for(uint32_t request:{1u,2u,0x80000000u,0xFFFFFFFFu}) {
            reset(caps);f.seedDirty(0x38,2);f.seedDirty(0x28,1);
            f.call(14,request,lr,expected(1,1,1,3,true));
            // Already enabled must not requeue or reinterpret missing caps.
            PPC_STORE_U32(0x82E3DFC4,0);
            f.call(14,request,lr,f.snapshot());
            // Disable ignores caps and overwrites the same dirty entry.
            f.call(14,0,lr,expected(0,0,1,3));
            f.call(14,0,lr,f.snapshot());
            PPC_STORE_U32(0x82E3DFC4,caps);
            f.call(14,request,lr,expected(1,1,1,3));
        }
    }
    // Fresh disable also needs an append; cached equality must leave even an
    // independently seeded pending value alone.
    reset(0);PPC_STORE_U32(cached,1);PPC_STORE_U32(value,1);
    f.call(14,0,lr,expected(0,0,1,1,true));
    reset(0x180);PPC_STORE_U32(cached,1);f.call(14,1,lr,f.snapshot());
    reset(0);PPC_STORE_U32(value,1);f.call(14,0,lr,f.snapshot());

    reset(0x180);
    for(uint32_t id=0;id<425;++id) if(id!=0x196) f.seedDirty(id);
    f.call(14,1,lr,expected(1,1,1,425,true)); // Last legal queue slot.
    f.call(14,0,lr,expected(0,0,1,425));       // Full but already dirty.

    reset(0x180);
    for(uint32_t id=0;id<425;++id) if(id!=0x196) f.seedDirty(id);
    // A full queue missing one of 425 IDs necessarily has an unrelated duplicate
    // or invalid ID. Seed that corruption explicitly to exercise the capacity
    // guard without inventing a 426th slot or missing the selected-ID check.
    PPC_STORE_U32(f.queue+4*424,0);PPC_STORE_U32(f.count,425);
    f.reject(14,1,lr,"Original RenderWare fog dirty queue is full");
    for(uint32_t n:{426u,0xFFFFFFFFu}) {
        reset(0x180);PPC_STORE_U32(f.count,n);
        f.reject(14,1,lr,"Original RenderWare fog cache or dirty queue is invalid");
    }
    for(uint32_t address:{cached,dirty}) {
        reset(0x180);PPC_STORE_U32(address,2);
        f.reject(14,1,lr,"Original RenderWare fog cache or dirty queue is invalid");
    }
    reset(0x180);PPC_STORE_U32(dirty,1);
    f.reject(14,1,lr,"Original RenderWare fog dirty membership is inconsistent");
    reset(0x180);f.seedDirty(0x196);PPC_STORE_U32(dirty,0);
    f.reject(14,1,lr,"Original RenderWare fog dirty membership is inconsistent");
    reset(0x180);f.seedDirty(0x196);f.seedDirty(0x196);
    f.reject(14,1,lr,"Original RenderWare fog dirty membership is inconsistent");
    reset(0x180);
    f.reject(13,1,lr,"Unimplemented native engine graphics boundary 0x824025A8,");
}

inline void rwDepthWriteContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    RwCpuContractFixture f(runtime,cpu,base);
    constexpr uint32_t cached=0x82D0E3B0,sibling=0x82D0E3B4,lr=0x826B84C4;
    auto reset=[&](uint32_t c,uint32_t s) {
        f.reset();PPC_STORE_U32(cached,c);PPC_STORE_U32(sibling,s);
        PPC_STORE_U32(f.pending+8*0x28,c);PPC_STORE_U32(f.pending+8*0x30,c);
    };
    auto changed=[&](uint32_t normalized,bool includeDepth) {
        auto e=f.snapshot();f.put(e,cached,normalized);
        uint32_t n=PPC_LOAD_U32(f.count);
        for(uint32_t id:{0x28u,0x30u}) {
            if(id==0x28 && !includeDepth) continue;
            f.put(e,f.pending+8*id,normalized);
            if(!PPC_LOAD_U32(f.pending+8*id+4)) {
                f.put(e,f.pending+8*id+4,1);f.put(e,f.queue+4*n++,id);
            }
        }
        f.put(e,f.count,n);return e;
    };
    // Four cached/sibling combinations, with zero/canonical/noncanonical true.
    for(uint32_t c:{0u,1u}) for(uint32_t s:{0u,1u}) for(uint32_t request:{0u,1u,0xFFFFFFFFu}) {
        reset(c,s);f.seedDirty(0x38,2);
        const uint32_t normalized=request!=0;
        f.call(8,request,lr,c==normalized?f.snapshot():changed(normalized,s==0));
        f.call(8,request,lr,f.snapshot()); // Cache suppression, no repeat append.
    }
    // Neither, either, or both IDs already dirty. Preserve existing queue order,
    // overwrite the pending values, and append only missing IDs in 28,30 order.
    for(uint32_t mask=0;mask<4;++mask) {
        reset(0,0);f.seedDirty(0x38,2);
        if(mask&2) f.seedDirty(0x30,0);
        if(mask&1) f.seedDirty(0x28,0);
        f.call(8,0xFFFFFFFF,lr,changed(1,true));
        f.call(8,0,lr,changed(0,true));
    }
    // Sibling enabled leaves the separate depth-enable pending entry untouched.
    reset(0,1);f.seedDirty(0x28,1);f.seedDirty(0x30,0);
    f.call(8,1,lr,changed(1,false));f.call(8,0,lr,changed(0,false));

    reset(0,0);
    for(uint32_t id=0;id<425;++id) if(id!=0x28 && id!=0x30) f.seedDirty(id);
    f.call(8,1,lr,changed(1,true)); // Slots 423 and 424 receive 28 then 30.
    f.call(8,0,lr,changed(0,true)); // Full, both dirty: no new slots required.
    reset(0,0);
    for(uint32_t id=0;id<425;++id) if(id!=0x28 && id!=0x30) f.seedDirty(id);
    PPC_STORE_U32(f.queue+4*423,0);PPC_STORE_U32(f.count,424);
    f.reject(8,1,lr,"Original RenderWare depth dirty queue is full"); // Two slots needed, one left.
    for(uint32_t id:{0x28u,0x30u}) {
        reset(0,0);PPC_STORE_U32(f.pending+8*id+4,2);
        f.reject(8,1,lr,"Original RenderWare depth dirty membership is inconsistent");
        reset(0,0);PPC_STORE_U32(f.pending+8*id+4,1);
        f.reject(8,1,lr,"Original RenderWare depth dirty membership is inconsistent");
        reset(0,0);f.seedDirty(id);f.seedDirty(id);
        f.reject(8,1,lr,"Original RenderWare depth dirty membership is inconsistent");
    }
    for(uint32_t address:{cached,sibling}) {
        reset(0,0);PPC_STORE_U32(address,2);
        f.reject(8,1,lr,"Original RenderWare depth cache or queue is invalid");
    }
    for(uint32_t n:{426u,0xFFFFFFFFu}) {
        reset(0,0);PPC_STORE_U32(f.count,n);
        f.reject(8,1,lr,"Original RenderWare depth cache or queue is invalid");
    }
}
