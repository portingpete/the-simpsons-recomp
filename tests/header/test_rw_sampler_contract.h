#pragma once
// Include after test_rw_rebuild_contract.h inside the test namespace. No native
// hook externs are needed: the dispatcher and traced helpers remain real AOT.
struct RwSamplerBodyTrace;
inline RwSamplerBodyTrace* rwSamplerActiveTrace{};
inline void rwSamplerTraceAddress(PPCContext&,uint8_t*);
inline void rwSamplerTraceFilter(PPCContext&,uint8_t*);
struct RwSamplerBodyTrace {
    uint8_t* base;
    std::array<PPCFunc*,2> original{};
    uint32_t calls{},completed{},helperSp{};
    bool inject{},sawCacheMutation{},sawEffectiveMutation{};
    explicit RwSamplerBodyTrace(uint8_t* memory):base(memory) {
        require(!rwSamplerActiveTrace,"Nested RW sampler trace fixture");
        original={PPC_LOOKUP_FUNC(base,0x82401660),PPC_LOOKUP_FUNC(base,0x824017A0)};
        require(original[0] && original[1],"Missing original RW sampler helper mapping");
        rwSamplerActiveTrace=this;
        PPC_LOOKUP_FUNC(base,0x82401660)=rwSamplerTraceAddress;
        PPC_LOOKUP_FUNC(base,0x824017A0)=rwSamplerTraceFilter;
    }
    ~RwSamplerBodyTrace() {
        PPC_LOOKUP_FUNC(base,0x82401660)=original[0];
        PPC_LOOKUP_FUNC(base,0x824017A0)=original[1];rwSamplerActiveTrace=nullptr;
    }
    RwSamplerBodyTrace(const RwSamplerBodyTrace&)=delete;
    RwSamplerBodyTrace& operator=(const RwSamplerBodyTrace&)=delete;
    void invoke(PPCContext& ctx,uint8_t* memory,uint32_t which) {
        require(memory==base && Simpsons::currentContext==&ctx,"RW sampler helper lost its scoped CPU context");
        const auto incoming=ctx;helperSp=ctx.r1.u32;++calls;
        auto* cache=Simpsons::active->pointer(RwCpuContractFixture::cacheBase,RwCpuContractFixture::cacheSize,false);
        const std::vector<uint8_t> before(cache,cache+RwCpuContractFixture::cacheSize);
        const auto host=Simpsons::active->engineDriver->effectiveState();
        original[which](ctx,memory);
        ++completed;rwRebuildAbi(ctx,incoming);
        require(ctx.r3.u32==1,"Original RW sampler helper lost its Boolean result");
        if(which) require(PPC_LOAD_U32(helperSp-0x80)==helperSp &&
                          PPC_LOAD_U32(helperSp-8)==uint32_t(incoming.lr),
                          "RW filter helper skipped its original 80-byte frame");
        sawCacheMutation=std::memcmp(cache,before.data(),before.size())!=0;
        sawEffectiveMutation=!rwRebuildSameState(host,Simpsons::active->engineDriver->effectiveState());
        if(inject)throw Simpsons::Failure("Injected RW sampler failure after original helper changes");
    }
};
inline void rwSamplerTraceAddress(PPCContext& ctx,uint8_t* base) {rwSamplerActiveTrace->invoke(ctx,base,0);}
inline void rwSamplerTraceFilter(PPCContext& ctx,uint8_t* base) {rwSamplerActiveTrace->invoke(ctx,base,1);}

inline void rwSamplerContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    const size_t firstCheck=checks;
    auto& driver=*runtime.engineDriver;
    RwCpuContractFixture f(runtime,cpu,base);
    RwSamplerBodyTrace trace(base);
    constexpr uint32_t app=0x82D5DB78,lr=0x827F5B54;
    constexpr uint32_t uCache=0x82D0E3FC,vCache=0x82D0E400,filterCache=0x82D0E404,anisoCache=0x82D0E40C;
    constexpr std::array<uint32_t,5> addresses={0,0,1,2,6};
    constexpr std::array<uint32_t,28> filters={2,2,0,2,1,2,0,0,1,0,0,1,1,1,4,2,4,2,4,2,4,0,4,0,4,1,4,1};
    const auto initialHost=driver.effectiveState();
    const auto initialRegisters=cpu.registers();
    struct SavedRange {
        uint8_t* at;std::vector<uint8_t> bytes;
        SavedRange(Simpsons::Runtime& rt,uint32_t address,uint32_t size):at(rt.pointer(address,size,true)),bytes(at,at+size) {}
        void restore() const noexcept {std::memcpy(at,bytes.data(),bytes.size());}
        bool unchanged() const {return !std::memcmp(at,bytes.data(),bytes.size());}
        ~SavedRange() {restore();}
    } application(runtime,app,0x41D4),applied(runtime,0x82E3D160,0xB24),
      records(runtime,0x82D501E0,0x140),stack(runtime,f.sp-0x1F0,0x1F0);
    const auto allocations=runtime.allocations;
    const auto physical=runtime.physicalAllocations;
    const uint32_t nextAllocation=runtime.nextAllocation;
    auto counters=[&] {return std::array<uint64_t,12>{driver.screenDrawCount(),driver.im2DDrawCount(),
        driver.im2DUploadCount(),driver.cameraClearCount(),driver.cameraCopyCount(),driver.bindingResetCount(),
        driver.presentationCount(),driver.presentationAttemptCount(),driver.frontCopyCount(),driver.rasterCount(),
        runtime.graphicsStorage.size(),runtime.graphicsPresentReceipts.size()};};
    const auto initialCounters=counters();
    auto noOtherEffects=[&] {
        require(runtime.engineDriver.get()==f.driverBefore && driver.started(),"RW sampler replaced or stopped its driver owner");
        require(application.unchanged() && applied.unchanged() && records.unchanged(),
                "RW sampler changed independent application, applied-state or stage-record bytes");
        require(PPC_LOAD_U32(0x82E3DFC4)==0,"RW sampler changed the fixture's read-only capability word");
        bool same=runtime.nextAllocation==nextAllocation && runtime.allocations.size()==allocations.size() &&
                  runtime.physicalAllocations.size()==physical.size();
        if(same) {
            for(size_t i=0;i<allocations.size();++i) {
                const auto& a=allocations[i];const auto& b=runtime.allocations[i];
                same=same && a.address==b.address && a.size==b.size && a.pageSize==b.pageSize && a.committed==b.committed;
            }
            for(size_t i=0;i<physical.size();++i) {
                const auto& a=physical[i];const auto& b=runtime.physicalAllocations[i];
                same=same && a.address==b.address && a.physical==b.physical && a.size==b.size &&
                     a.protect==b.protect && a.pageSize==b.pageSize && a.pageProtect==b.pageProtect;
            }
        }
        require(same,"RW sampler changed guest allocation ownership or committed backing");
        require(counters()==initialCounters,"RW sampler allocated a raster, uploaded, drew, copied, reset or presented");
        require(Simpsons::currentContext==&cpu.registers(),"RW sampler leaked its nested helper context");
    };
    auto restoreHost=[&] {
        const auto fields=Simpsons::Graphics::samplerStateEvidence();
        for(uint32_t stage=0;stage<16;++stage)for(uint32_t i=0;i<fields.size();++i)
            driver.applicationSampler(base,app,stage,i+1,initialHost.sampler(stage,fields[i].id),true);
    };
    auto hostSampler=[&](uint32_t id,uint32_t value) {
        const auto fields=Simpsons::Graphics::samplerStateEvidence();
        for(uint32_t i=0;i<fields.size();++i)if(fields[i].id==id) {
            driver.applicationSampler(base,app,0,i+1,value,true);return;
        }
        require(false,"RW sampler fixture has no original application selector mapping");
    };
    auto reset=[&] {
        application.restore();restoreHost();f.reset();stack.restore();cpu.registers()=initialRegisters;
        // Keep unrelated deferred scalar and stage work pending. Exact whole
        // cache comparisons detect either an accidental commit or an enqueue.
        f.seedDirty(0x38,6);f.seedDirty(0x195,1);
        constexpr uint32_t stageId=3*33+2;
        PPC_STORE_U32(0x82D0DB70+8*stageId,0x12345678);PPC_STORE_U32(0x82D0DB74+8*stageId,1);
        PPC_STORE_U32(0x82D0E4C8,3);PPC_STORE_U32(0x82D0E4CC,2);PPC_STORE_U32(0x82D10118,1);
        PPC_STORE_U32(anisoCache,1);
    };
    struct Expected {std::vector<uint8_t> cache;Simpsons::Graphics::EngineState host;};
    auto expected=[&](uint32_t selector,uint32_t request) {
        Expected e{f.snapshot(),driver.effectiveState()};
        auto sampler=[&](uint32_t id,uint32_t value) {
            f.put(e.cache,f.cacheBase+4*id,value);e.host.setSampler(0,id,value);
        };
        if(selector==2) {
            if(PPC_LOAD_U32(uCache)!=request) {f.put(e.cache,uCache,request);sampler(0,addresses[request]);}
            if(PPC_LOAD_U32(vCache)!=request) {f.put(e.cache,vCache,request);sampler(4,addresses[request]);}
        } else {
            if(int32_t(PPC_LOAD_U32(anisoCache))>1) {f.put(e.cache,anisoCache,1);sampler(0x24,1);}
            if(PPC_LOAD_U32(filterCache)!=request) {
                f.put(e.cache,filterCache,request);sampler(0x14,filters[2*request]);sampler(0x10,filters[2*request]);
                if(PPC_LOAD_U32(f.cacheBase+0x60)!=filters[2*request+1])sampler(0x18,filters[2*request+1]);
            }
        }
        return e;
    };
    auto call=[&](uint32_t selector,uint32_t request) {
        const auto e=expected(selector,request);
        f.prepare(lr);const auto incoming=cpu.registers();const auto calls=trace.calls,completed=trace.completed;
        require(cpu.invoke(0x824025A8,selector,request)==1,"RW sampler dispatcher lost its original Boolean result");
        f.abi(lr);rwRebuildAbi(cpu.registers(),incoming);
        require(trace.calls==calls+1 && trace.completed==completed+1 && trace.helperSp==f.sp-0x170,
                "RW sampler did not execute one original helper in its nested callback frame");
        require(PPC_LOAD_U32(f.sp-0x70)==f.sp && PPC_LOAD_U32(f.sp-8)==lr &&
                PPC_LOAD_U64(f.sp-0x18)==0xA1B2C3D455660010ull && PPC_LOAD_U64(f.sp-0x10)==0xA1B2C3D455660011ull,
                "RW sampler skipped the original dispatcher save/restore frame");
        require(f.snapshot()==e.cache,"RW sampler changed unexpected cache, pending or queue bytes");
        require(rwRebuildSameState(e.host,driver.effectiveState()),"RW sampler changed the wrong native fields or stages");
        noOtherEffects();
    };
    // Cleanup runs before native owner observations, allowing malformed device
    // and owner identities to remain installed throughout the actual rejection.
    auto reject=[&](uint32_t selector,uint32_t request,const char* prefix,
                    const std::function<void()>& cleanup=std::function<void()>{}) {
        f.prepare(lr);const auto incoming=cpu.registers();const auto cache=f.snapshot();
        const std::vector<uint8_t> beforeStack(stack.at,stack.at+stack.bytes.size());
        const auto calls=trace.calls;bool precise=false;
        try {
            rejects([&] {
                try {cpu.invoke(0x824025A8,selector,request);}
                catch(const std::exception& error) {precise=std::string(error.what()).starts_with(prefix);throw;}
            });
        } catch(...) {if(cleanup)cleanup();throw;}
        if(cleanup)cleanup();
        require(precise && cpu.registers().lastFunction==0x824025A8 && trace.calls==calls,
                "RW sampler rejection did not occur at the intended dispatcher preflight");
        rwRebuildAbi(cpu.registers(),incoming);
        require(f.snapshot()==cache && !std::memcmp(stack.at,beforeStack.data(),beforeStack.size()),
                "Rejected RW sampler entered the dispatcher frame or mutated cache bytes");
        require(rwRebuildSameState(initialHost,driver.effectiveState()),"Rejected RW sampler changed native effective state");
        noOtherEffects();
    };
    try {
        for(uint32_t request=0;request<addresses.size();++request)
            for(uint32_t matches=0;matches<4;++matches)for(bool rawEqual:{false,true}) {
            reset();hostSampler(0,7);hostSampler(4,5);
            PPC_STORE_U32(uCache,(matches&1)?request:(request+1)%5);
            PPC_STORE_U32(vCache,(matches&2)?request:(request+1)%5);
            const uint32_t raw=rawEqual?addresses[request]:(addresses[request]+1)%8;
            PPC_STORE_U32(f.cacheBase,raw);PPC_STORE_U32(f.cacheBase+0x10,raw);
            call(2,request);
            hostSampler(0,7);hostSampler(4,5);call(2,request); // Source equality preserves application overrides.
        }
        for(uint32_t request=1;request<=6;++request)for(bool retainedEqual:{false,true})
            for(bool mipEqual:{false,true})for(uint32_t aniso:{0u,1u,2u,16u,0x80000000u,0xFFFFFFFFu}) {
            reset();hostSampler(0x14,filters[2*request]^1);hostSampler(0x10,filters[2*request]^1);
            hostSampler(0x18,(filters[2*request+1]+1)%3);
            PPC_STORE_U32(filterCache,retainedEqual?request:request%6+1);PPC_STORE_U32(anisoCache,aniso);
            // Equal raw min/mag caches cannot suppress updates selected by a
            // changed retained filter. Mip has its own raw-cache suppression.
            PPC_STORE_U32(f.cacheBase+0x50,filters[2*request]);PPC_STORE_U32(f.cacheBase+0x40,filters[2*request]);
            PPC_STORE_U32(f.cacheBase+0x60,mipEqual?filters[2*request+1]:(filters[2*request+1]+1)%3);
            PPC_STORE_U32(f.cacheBase+0x90,7);
            call(9,request);
        }
        // Unsupported min/mag values must reject before even the preceding
        // anisotropy reset. Matching retained filter values suppress conversion
        // but still perform a separately required anisotropy normalization.
        for(uint32_t request:{0u,7u,8u,9u,10u,11u,12u,13u}) {
            reset();PPC_STORE_U32(filterCache,1);PPC_STORE_U32(anisoCache,16);
            reject(9,request,"Unsupported sampler state ID or value");
            for(uint32_t aniso:{1u,16u}) {
                reset();PPC_STORE_U32(filterCache,request);PPC_STORE_U32(anisoCache,aniso);
                hostSampler(0x14,1);hostSampler(0x10,0);hostSampler(0x18,0);call(9,request);
            }
        }
        for(uint32_t request:{5u,6u,0x80000000u,0xFFFFFFFFu}) {
            reset();PPC_STORE_U32(uCache,request);PPC_STORE_U32(vCache,request);
            reject(2,request,"Original RenderWare address index is out of bounds");
        }
        for(uint32_t request:{14u,15u,0x80000000u,0xFFFFFFFFu}) {
            reset();PPC_STORE_U32(filterCache,request);reject(9,request,"Original RenderWare filter index is out of bounds");
        }
        for(uint32_t selector:{2u,9u}) {
            const uint32_t table=selector==2?0x82062CEC:0x82062D00,size=selector==2?5:28;
            for(uint32_t i=0;i<size;++i) {
                reset();SavedRange word(runtime,table+4*i,4);
                PPC_STORE_U32(table+4*i,PPC_LOAD_U32(table+4*i)^1);
                reject(selector,2,"Original RW rebuild conversion table changed");
            }
            reset();{
                SavedRange word(runtime,0x82062DE8+selector-1,1);
                PPC_STORE_U8(0x82062DE8+selector-1,0xFF);
                reject(selector,2,"Original RenderWare sampler dispatch table changed");
            }
            reset();{
                SavedRange device(runtime,0x82D0CAF8,4);PPC_STORE_U32(0x82D0CAF8,0x12340000);
                reject(selector,2,"Native driver cannot own a console SDK device",[&]{device.restore();});
            }
            reset();{
                SavedRange engine(runtime,0x82D0CA68,4);PPC_STORE_U32(0x82D0CA68,0);
                reject(selector,2,"Original engine owner changed while native driver is live",[&]{engine.restore();});
            }
            reset();{
                SavedRange context(runtime,0x82D6D890,4);PPC_STORE_U32(0x82D6D890,0);
                reject(selector,2,"Unknown, stale or inactive native backend context identity",[&]{context.restore();});
            }
        }
        // Calling either helper directly lacks the required engine-owned scope.
        for(uint32_t entry:{0x82401660u,0x824017A0u}) {
            reset();f.prepare(lr);const auto incoming=cpu.registers();const auto cache=f.snapshot();
            const auto beforeStack=std::vector<uint8_t>(stack.at,stack.at+stack.bytes.size());
            const auto completed=trace.completed;bool precise=false;
            rejects([&] {
                try {cpu.invoke(entry,2);}
                catch(const Simpsons::Failure& error) {
                    precise=std::string(error.what()).starts_with("RenderWare sampler callback is outside its native CPU scope");throw;
                }
            });
            require(precise && cpu.registers().lastFunction==entry && trace.completed==completed,
                    "Direct RW sampler helper escaped its entry scope guard");
            rwRebuildAbi(cpu.registers(),incoming);
            require(f.snapshot()==cache && !std::memcmp(stack.at,beforeStack.data(),beforeStack.size()) &&
                    rwRebuildSameState(initialHost,driver.effectiveState()),"Direct helper rejection changed state or stack");
            noOtherEffects();
        }
        // The injected exception follows the complete original helper and its
        // real CPU/effective mutations. The inner 180-byte region rolls back;
        // the outer 70-byte dispatcher frame was already entered and persists.
        for(uint32_t selector:{2u,9u}) {
            reset();hostSampler(0,7);hostSampler(4,5);hostSampler(0x14,0);hostSampler(0x10,0);hostSampler(0x18,2);
            PPC_STORE_U32(uCache,1);PPC_STORE_U32(vCache,1);PPC_STORE_U32(filterCache,1);
            PPC_STORE_U32(anisoCache,16);PPC_STORE_U32(f.cacheBase+0x60,2);
            std::memset(stack.at,0xCD,stack.bytes.size());f.prepare(lr);
            const auto cache=f.snapshot();const auto host=driver.effectiveState();
            const auto beforeStack=std::vector<uint8_t>(stack.at,stack.at+stack.bytes.size());
            const auto completed=trace.completed;trace.inject=true;bool precise=false;
            try {
                rejects([&] {
                    try {cpu.invoke(0x824025A8,selector,selector==2?3:6);}
                    catch(const Simpsons::Failure& error) {
                        precise=std::string(error.what()).starts_with("Injected RW sampler failure after original helper changes");throw;
                    }
                });
            } catch(...) {trace.inject=false;throw;}
            trace.inject=false;
            require(precise && trace.completed==completed+1 && trace.sawCacheMutation && trace.sawEffectiveMutation,
                    "RW sampler rollback fault did not follow real original CPU and native changes");
            require(f.snapshot()==cache && rwRebuildSameState(host,driver.effectiveState()),
                    "Failed RW sampler did not roll back CPU caches and native effective state");
            require(!std::memcmp(stack.at,beforeStack.data(),0x180),"Failed RW sampler did not restore its complete nested helper stack");
            require(cpu.registers().r1.u32==f.sp-0x70 && PPC_LOAD_U32(f.sp-0x70)==f.sp &&
                    PPC_LOAD_U32(f.sp-8)==lr && std::memcmp(stack.at+0x180,beforeStack.data()+0x180,0x70)!=0,
                    "RW sampler failure incorrectly claimed rollback of its already-entered outer dispatcher frame");
            noOtherEffects();
            // Explicit fixture unwind of the outer caller precedes retry.
            cpu.registers()=initialRegisters;stack.restore();call(selector,selector==2?3:6);
        }
        reset();f.prepare(lr);{
            const auto cache=f.snapshot();const auto incoming=cpu.registers();const auto calls=trace.calls;
            const auto beforeStack=std::vector<uint8_t>(stack.at,stack.at+stack.bytes.size());
            bool foreignRejected=false;
            std::thread worker([&] {
                PPCContext foreign=incoming;foreign.r3.u32=2;foreign.r4.u32=3;
                try {(PPC_LOOKUP_FUNC(base,0x824025A8))(foreign,base);}
                catch(const std::exception& error) {
                    foreignRejected=std::string(error.what()).starts_with("Native driver accessed outside its runtime/thread owner");
                }
                catch(...) {foreignRejected=false;}
            });
            worker.join();
            require(foreignRejected && trace.calls==calls && f.snapshot()==cache &&
                    !std::memcmp(stack.at,beforeStack.data(),beforeStack.size()),"Foreign thread entered or changed RW sampler state");
            rwRebuildAbi(cpu.registers(),incoming);
            require(rwRebuildSameState(initialHost,driver.effectiveState()),"Foreign sampler caller changed native state");noOtherEffects();
        }
        application.restore();restoreHost();cpu.registers()=initialRegisters;
        require(rwRebuildSameState(initialHost,driver.effectiveState()),"RW sampler fixture failed to restore effective state");
    } catch(...) {
        application.restore();cpu.registers()=initialRegisters;
        try {restoreHost();} catch(...) {} // Preserve the primary diagnostic during teardown.
        throw;
    }
    std::printf("PASS original RenderWare samplers:%zu checks; exact CPU caches, immediate stage-zero fields, scoped helper ABI and post-mutation rollback; no draw claim\n",checks-firstCheck);
}
