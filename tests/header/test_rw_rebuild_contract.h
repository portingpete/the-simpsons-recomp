#pragma once
// Include inside test_engine_driver.cpp's anonymous namespace after require /
// rejects. Call rwRebuildContracts(runtime,cpu,base) after the existing RW tests.
// Uses the real 82400D50 AOT body through the driver's checked outer operation.

inline bool rwRebuildSameState(const Simpsons::Graphics::EngineState& a,
                               const Simpsons::Graphics::EngineState& b) {
    if(a.initialized()!=b.initialized()) return false;
    for(const auto& f:Simpsons::Graphics::scalarStateEvidence()) if(a.scalar(f.id)!=b.scalar(f.id)) return false;
    for(uint32_t stage=0;stage<16;++stage)
        for(const auto& f:Simpsons::Graphics::samplerStateEvidence()) if(a.sampler(stage,f.id)!=b.sampler(stage,f.id)) return false;
    for(uint32_t target=0;target<4;++target) if(a.effectiveBlend(target)!=b.effectiveBlend(target)) return false;
    return true;
}
struct RwRebuildSavedBytes {
    struct Range {uint8_t* at;std::vector<uint8_t> bytes;};
    std::vector<Range> ranges;
    explicit RwRebuildSavedBytes(Simpsons::Runtime& runtime) {
        for(auto [address,size]:std::array<std::pair<uint32_t,uint32_t>,7>{{
            {0x82CD1A64,0x14},{0x82D0CAB0,0x40},{0x82D0D170,0x2FBC},
            {0x82E3D160,0xB24},{0x82D501E0,0x140},{0x82D5DB78,0x41D4},{0x82D0CAF8,4}}}) {
            auto* p=runtime.pointer(address,size,true);ranges.push_back({p,{p,p+size}});
        }
    }
    void restore() const noexcept {for(const auto& r:ranges) std::memcpy(r.at,r.bytes.data(),r.bytes.size());}
    bool unchanged() const {
        for(const auto& r:ranges) if(std::memcmp(r.at,r.bytes.data(),r.bytes.size())) return false;
        return true;
    }
};
inline void rwRebuildAbi(const PPCContext& after,const PPCContext& before) {
    const std::array<const PPCRegister*,36> a={
        &after.r14,&after.r15,&after.r16,&after.r17,&after.r18,&after.r19,&after.r20,&after.r21,&after.r22,
        &after.r23,&after.r24,&after.r25,&after.r26,&after.r27,&after.r28,&after.r29,&after.r30,&after.r31,
        &after.f14,&after.f15,&after.f16,&after.f17,&after.f18,&after.f19,&after.f20,&after.f21,&after.f22,
        &after.f23,&after.f24,&after.f25,&after.f26,&after.f27,&after.f28,&after.f29,&after.f30,&after.f31};
    const std::array<const PPCRegister*,36> b={
        &before.r14,&before.r15,&before.r16,&before.r17,&before.r18,&before.r19,&before.r20,&before.r21,&before.r22,
        &before.r23,&before.r24,&before.r25,&before.r26,&before.r27,&before.r28,&before.r29,&before.r30,&before.r31,
        &before.f14,&before.f15,&before.f16,&before.f17,&before.f18,&before.f19,&before.f20,&before.f21,&before.f22,
        &before.f23,&before.f24,&before.f25,&before.f26,&before.f27,&before.f28,&before.f29,&before.f30,&before.f31};
    require(after.r1.u64==before.r1.u64 && after.lr==before.lr,"RW rebuild damaged caller SP/LR");
    for(size_t i=0;i<a.size();++i) require(a[i]->u64==b[i]->u64,"RW rebuild damaged a saved GPR/FPR");
}

// Check the actual inner callback ABI, not only the driver's unchanged outer
// context. This dispatch wrapper delegates to the complete generated body.
inline PPCFunc* rwRebuildOriginalBody{};
inline uint32_t rwRebuildBodyCalls{},rwRebuildBodySp{};
inline bool rwRebuildBodyThrew{};
inline std::vector<uint8_t> rwRebuildBodyStack;
inline void rwRebuildTraceBody(PPCContext& ctx,uint8_t* base) {
    const auto incoming=ctx;
    rwRebuildBodySp=ctx.r1.u32;
    auto* stack=Simpsons::active->pointer(rwRebuildBodySp-0x1C0,0x1C0,false);
    rwRebuildBodyStack.assign(stack,stack+0x1C0);
    ++rwRebuildBodyCalls;rwRebuildBodyThrew=false;
    try {
        rwRebuildOriginalBody(ctx,base);
        rwRebuildAbi(ctx,incoming);
        require(PPC_LOAD_U32(incoming.r1.u32-0xC0)==incoming.r1.u32 &&
                PPC_LOAD_U32(incoming.r1.u32-8)==uint32_t(incoming.lr),
                "RW rebuild did not execute its original C0-byte save/restore frame");
    } catch(...) {rwRebuildBodyThrew=true;throw;}
}
struct RwRebuildBodyTrace {
    uint8_t* base;
    explicit RwRebuildBodyTrace(uint8_t* memory):base(memory) {
        require(!rwRebuildOriginalBody,"Nested RW rebuild body fixture");
        rwRebuildOriginalBody=PPC_LOOKUP_FUNC(base,0x82400D50);
        require(rwRebuildOriginalBody!=nullptr,"Missing original RW rebuild body");
        rwRebuildBodyCalls=0;rwRebuildBodyThrew=false;
        PPC_LOOKUP_FUNC(base,0x82400D50)=rwRebuildTraceBody;
    }
    ~RwRebuildBodyTrace() {
        PPC_LOOKUP_FUNC(base,0x82400D50)=rwRebuildOriginalBody;
        rwRebuildOriginalBody=nullptr;
    }
};

// The sole injected fault replaces a CPU callback dispatch slot, not original
// image bytes. Execute the first two REAL 8240EBB0 callbacks, then throw after
// their record writes and after all 56 sampler updates have happened.
inline PPCFunc* rwRebuildOriginalStage{};
inline uint32_t rwRebuildFaultCalls{};
inline bool rwRebuildSawMutation{};
inline void rwRebuildFailAfterStage(PPCContext& ctx,uint8_t* base) {
    rwRebuildOriginalStage(ctx,base);
    if(++rwRebuildFaultCalls==2) {
        rwRebuildSawMutation=PPC_LOAD_U32(0x82D501E0+40+12)==1 &&
            PPC_LOAD_U32(0x82D501E0+40+28)==1 &&
            Simpsons::active->engineDriver->effectiveState().sampler(0,0x18)==0;
        throw Simpsons::Failure("Injected RW rebuild failure after original CPU stage writes");
    }
}
struct RwRebuildStageFault {
    uint8_t* base;
    explicit RwRebuildStageFault(uint8_t* memory):base(memory) {
        require(!rwRebuildOriginalStage,"Nested RW rebuild test injection");
        rwRebuildOriginalStage=PPC_LOOKUP_FUNC(base,0x8240EBB0);
        require(rwRebuildOriginalStage!=nullptr,"Missing original CPU stage callback");
        rwRebuildFaultCalls=0;rwRebuildSawMutation=false;
        PPC_LOOKUP_FUNC(base,0x8240EBB0)=rwRebuildFailAfterStage;
    }
    ~RwRebuildStageFault() {
        PPC_LOOKUP_FUNC(base,0x8240EBB0)=rwRebuildOriginalStage;
        rwRebuildOriginalStage=nullptr;
    }
};

inline void rwRebuildContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    auto& driver=*runtime.engineDriver;
    const auto initialHost=driver.effectiveState();
    const auto initialContext=cpu.registers();
    RwRebuildSavedBytes initialBytes(runtime);
    RwRebuildBodyTrace bodyTrace(base);
    auto restoreHost=[&] {
        // Direct owner operations deliberately leave the application's retained
        // source cache untouched. Restore its original effective snapshot too.
        for(const auto& field:Simpsons::Graphics::scalarStateEvidence())
            driver.directScalar(base,field.id,initialHost.scalar(field.id));
        for(uint32_t stage=0;stage<16;++stage)
            for(const auto& field:Simpsons::Graphics::samplerStateEvidence())
                driver.applicationSampler(base,0x82D5DB78,stage,field.id/4+1,initialHost.sampler(stage,field.id),true);
    };
    auto seed=[&](uint32_t variant) {
        initialBytes.restore();
        // Bounded nondefault source requests. All table rows are original;
        // these tests deliberately vary retained enums rather than defaulting.
        PPC_STORE_U32(0x82D0E3B0,variant&1);PPC_STORE_U32(0x82D0E3B4,(variant>>1)&1);
        PPC_STORE_U32(0x82D0E3B8,variant&1);
        for(uint32_t a:{0x82D0E3BCu,0x82D0E3C0u,0x82D0E3C4u}) PPC_STORE_U32(a,1);
        PPC_STORE_U32(0x82D0E3C8,8);PPC_STORE_U32(0x82D0E3CC,19*variant);
        PPC_STORE_U32(0x82D0E3D0,variant?17*variant:0);PPC_STORE_U32(0x82D0E3D4,255-13*variant);
        PPC_STORE_U32(0x82D0E3D8,variant&1);PPC_STORE_U32(0x82D0E3DC,0);
        PPC_STORE_U32(0x82D0E3E0,variant%4);PPC_STORE_U32(0x82D0E3E4,variant&1);
        PPC_STORE_U32(0x82D0E3E8,variant%4);PPC_STORE_U32(0x82D0E3EC,0x3E800000+variant*0x10000);
        PPC_STORE_U32(0x82D0E3F0,0xAB120000+variant);PPC_STORE_U32(0x82D0E3F4,variant%3);
        PPC_STORE_U32(0x82D0E4B8,variant);PPC_STORE_U32(0x82D0E4BC,11-variant);
        PPC_STORE_U32(0x82D0E4C0,variant%9);PPC_STORE_U32(0x82D0E4C4,(variant>>1)&1);
        PPC_STORE_U32(0x82D0F6D0,17+19*variant); // Saved before 823FFE78 clears pending.
        PPC_STORE_U32(0x82D10078,0x3F000000+variant);PPC_STORE_U32(0x82D10080,0x41000000+variant);
        for(uint32_t stage=0;stage<8;++stage) {
            const uint32_t source=0x82D0E3F8+24*stage;
            PPC_STORE_U32(source,0);PPC_STORE_U32(source+4,(variant+stage)%5);
            PPC_STORE_U32(source+8,(variant+2*stage+1)%5);
            PPC_STORE_U32(source+12,1+(variant+stage)%6);
            PPC_STORE_U32(source+16,0);PPC_STORE_U32(source+20,1);
        }
    };
    auto rejectUnchanged=[&](const char* failurePrefix=nullptr) {
        RwRebuildSavedBytes before(runtime);
        const auto host=driver.effectiveState();
        const auto incoming=cpu.registers();
        const auto count=driver.bindingResetCount();
        const auto bodyCalls=rwRebuildBodyCalls;
        rwRebuildBodyThrew=false;
        bool precise=!failurePrefix;
        rejects([&] {
            try {driver.resetBindings(cpu.registers(),base);}
            catch(const std::exception& error) {
                if(failurePrefix) precise=std::string(error.what()).starts_with(failurePrefix);
                throw;
            }
        });
        require(precise,"RW rebuild rejected at a different boundary from the injected failure");
        require(before.unchanged(),"Rejected RW rebuild changed CPU cache/application/record bytes");
        require(rwRebuildSameState(host,driver.effectiveState()),"Rejected RW rebuild changed effective state");
        require(driver.bindingResetCount()==count && driver.started(),"Rejected RW rebuild published or stopped native bindings");
        if(!failurePrefix) require(rwRebuildBodyCalls==bodyCalls,"Malformed RW source entered the destructive original rebuild body");
        if(rwRebuildBodyThrew) {
            auto* stack=runtime.pointer(rwRebuildBodySp-0x1C0,0x1C0,false);
            require(!std::memcmp(stack,rwRebuildBodyStack.data(),rwRebuildBodyStack.size()),
                    "Rejected original RW rebuild did not restore its guest callback stack bytes");
        }
        require(Simpsons::currentContext==&cpu.registers(),"Rejected rebuild leaked its callback TLS context");
        rwRebuildAbi(cpu.registers(),incoming);
    };
    try {
        for(uint32_t variant=0;variant<12;++variant) {
            seed(variant);
            const auto previous=driver.effectiveState();
            const auto incoming=cpu.registers();
            auto expected=previous;
            constexpr uint32_t blends[]={0,0,1,4,5,6,7,10,11,8,9,16};
            constexpr uint32_t culls[]={0,0,2,6},fogs[]={0,3,1,2};
            const std::array<std::pair<uint32_t,uint32_t>,21> scalarRequests={{
                {0x30,variant&1},{0x2C,(variant&2)?6u:7u},{0x28,(variant&3)?1u:0u},
                {0x6C,variant&1},{0x74,0},{0x78,0},{0x7C,0},{0x80,7},{0x84,19*variant},
                {0x88,17*variant},{0x8C,255-13*variant},{0x48,blends[variant]},{0x4C,blends[11-variant]},
                {0x68,variant%9?variant%9-1:0},{0x64,17+19*variant},{0x3C,variant&1},
                {0x60,(variant&1)?((variant>>1)&1):0},{0x38,culls[variant%4]},
                {0x196,variant&1},{0x1A1,fogs[variant%4]},{0x195,variant%3}}};
            for(auto [id,value]:scalarRequests) if(id<0x194 && !((id==0x88 || id==0x8C) && value==0))
                expected.setScalar(id,value);
            constexpr uint32_t addresses[]={0,0,1,2,6};
            constexpr uint32_t minMag[]={0,0,1,0,1,0,1},mips[]={0,2,2,0,0,1,1};
            constexpr uint32_t samplerIds[]={0x14,0x10,0x18,0,4,0xC,0x24};
            std::array<std::array<uint32_t,7>,8> samplerRequests{};
            for(uint32_t stage=0;stage<8;++stage) {
                const uint32_t filter=1+(variant+stage)%6;
                samplerRequests[stage]={minMag[filter],minMag[filter],mips[filter],
                    addresses[(variant+stage)%5],addresses[(variant+2*stage+1)%5],0,1};
                for(uint32_t k=0;k<7;++k) expected.setSampler(stage,samplerIds[k],samplerRequests[stage][k]);
            }
            auto* app=runtime.pointer(0x82D5DB78,0x41D4,false);
            const std::vector<uint8_t> applicationBefore(app,app+0x41D4);
            auto* records=runtime.pointer(0x82D501E0,0x140,false);
            std::vector<uint8_t> expectedRecords(records,records+0x140);
            auto putRecord=[&](uint32_t at,uint32_t value) {
                for(uint32_t i=0;i<4;++i) expectedRecords[at+i]=uint8_t(value>>(24-8*i));
            };
            for(uint32_t stage=0;stage<8;++stage) {
                putRecord(40*stage+12,stage?1:3);putRecord(40*stage+28,stage?1:3);
            }
            putRecord(8,0);putRecord(24,0);
            const auto count=driver.bindingResetCount();
            driver.resetBindings(cpu.registers(),base);
            require(driver.bindingResetCount()==count+1,"RW rebuild did not commit exactly once");
            rwRebuildAbi(cpu.registers(),incoming);
            require(Simpsons::currentContext==&cpu.registers(),"Successful rebuild leaked its callback TLS context");
            require(rwRebuildSameState(expected,driver.effectiveState()),
                    "Original retained RW state did not become effective, or changed an inherited field/stage 8..15");
            require(!std::memcmp(app,applicationBefore.data(),applicationBefore.size()),"RW rebuild rewrote the independent application cache");
            require(!std::memcmp(records,expectedRecords.data(),expectedRecords.size()),"RW rebuild changed unexpected original CPU stage record bytes");
            require(PPC_LOAD_U32(0x82D10114)==0 && PPC_LOAD_U32(0x82D10118)==0,"RW rebuild left pending queue entries");
            for(auto [id,value]:scalarRequests)
                require(PPC_LOAD_U32(0x82E3D580+4*id)==value,"RW rebuild lost a retained scalar/table request");
            for(auto [id,value]:std::array<std::pair<uint32_t,uint32_t>,6>{{
                {0x19B,0x3E800000+variant*0x10000},{0x199,0x3F000000+variant},
                {0x19A,0x41000000+variant},{0x198,0xAB120000+variant},{0x19F,0},{0x1A0,0xFFFFFFFF}}})
                require(PPC_LOAD_U32(0x82E3D580+4*id)==value,"RW rebuild substituted a CPU-only retained word");
            for(uint32_t id=0;id<425;++id) {
                require(PPC_LOAD_U32(0x82D0F3B0+8*id)==PPC_LOAD_U32(0x82E3D580+4*id),"RW scalar pending/applied mismatch");
                require(PPC_LOAD_U32(0x82D0F3B4+8*id)==0,"RW scalar dirty flag survived commit");
            }
            for(uint32_t stage=0;stage<8;++stage) {
                require(PPC_LOAD_U32(0x82D0E3F8+24*stage)==0,"RW rebuild did not clear its borrowed texture cache");
                for(uint32_t word=0;word<80;++word) {
                    uint32_t value=0xFFFFFFFF;
                    for(uint32_t k=0;k<7;++k) if(word==samplerIds[k]) value=samplerRequests[stage][k];
                    require(PPC_LOAD_U32(0x82D0D170+320*stage+4*word)==value,"RW sampler cache store/stride or untouched word is wrong");
                }
                for(uint32_t id=0;id<33;++id) {
                    uint32_t value=0xFFFFFFFF;
                    if(id==1 || id==4) value=stage?1:3;
                    if(!stage && (id==3 || id==6)) value=0;
                    const uint32_t index=stage*33+id;
                    require(PPC_LOAD_U32(0x82D0DB70+8*index)==value && PPC_LOAD_U32(0x82E3D160+4*index)==value &&
                            !PPC_LOAD_U32(0x82D0DB74+8*index),"RW stage cache/dirty record differs from original schedule");
                }
            }
        }
        // Invalid/out-of-bounds source indices, unsupported but in-bounds
        // requests, then malformed queues: reject before destroying CPU state.
        for(auto [address,value]:std::array<std::pair<uint32_t,uint32_t>,21>{{
            {0x82D0E3F4,3},{0x82D0E3E8,4},{0x82D0E3E0,4},{0x82D0E4B8,12},{0x82D0E4BC,0xFFFFFFFF},
            {0x82D0E3BC,9},{0x82D0E3C8,9},{0x82D0E4C0,9},{0x82D0E3FC+7*24,5},
            {0x82D0E400,0x40000000},{0x82D0E404,14},{0x82D0E404,0},{0x82D0E404,7},
            {0x82D0E408,1},{0x82D0E40C,0},{0x82D0F6D0,256},{0x82D0E3B0,2},
            {0x82D0E3CC,256},{0x82D0E3BC,2},{0x82D10114,426},{0x82D10118,265}}}) {
            seed(0);PPC_STORE_U32(address,value);rejectUnchanged();
        }
        seed(0);PPC_STORE_U32(0x82D10114,1);PPC_STORE_U32(0x82D0ED08,425);rejectUnchanged();
        seed(0);PPC_STORE_U32(0x82D10118,1);PPC_STORE_U32(0x82D0E4C8,8);rejectUnchanged();
        seed(0);PPC_STORE_U32(0x82D10118,1);PPC_STORE_U32(0x82D0E4C8,0);PPC_STORE_U32(0x82D0E4CC,33);rejectUnchanged();
        seed(0);
        // Queries also reject a console device. Capture owners first, exercise
        // the rejection, then restore the fault before querying them again.
        const auto beforeDevice=driver.effectiveState();
        const auto beforeDeviceCount=driver.bindingResetCount();
        const auto beforeDeviceCalls=rwRebuildBodyCalls;
        PPC_STORE_U32(0x82D0CAF8,0x12340000);
        RwRebuildSavedBytes invalidDevice(runtime);
        rejects([&]{driver.resetBindings(cpu.registers(),base);});
        require(invalidDevice.unchanged(),"Rejected console device changed original CPU state");
        PPC_STORE_U32(0x82D0CAF8,0);
        require(rwRebuildSameState(beforeDevice,driver.effectiveState()) &&
                driver.bindingResetCount()==beforeDeviceCount && rwRebuildBodyCalls==beforeDeviceCalls,
                "Rejected console device reached native reset/rebuild publication");

        seed(2); // stage-0 mip is 0, so the fault observes a changed effective value.
        driver.applicationSampler(base,0x82D5DB78,0,7,2,true);
        PPC_STORE_U32(0x82D501E0+40+12,11);PPC_STORE_U32(0x82D501E0+40+28,11);
        {
            RwRebuildStageFault fault(base);
            rejectUnchanged("Injected RW rebuild failure after original CPU stage writes");
            require(rwRebuildFaultCalls==2 && rwRebuildSawMutation,"RW rollback test failed before real CPU/effective mutations");
        }
        // Successful retry proves the execution scope and original callback
        // dispatch slot were released by the failing transaction.
        driver.resetBindings(cpu.registers(),base);
        require(driver.effectiveState().sampler(0,0x18)==0,"RW rebuild could not retry after a partial CPU failure");
        initialBytes.restore();restoreHost();cpu.registers()=initialContext;
        require(rwRebuildSameState(initialHost,driver.effectiveState()),"RW fixture failed to restore its initial effective state");
    } catch(...) {
        initialBytes.restore();cpu.registers()=initialContext;
        try {restoreHost();} catch(...) {} // Preserve the primary test failure on teardown/cancellation.
        throw;
    }
}
