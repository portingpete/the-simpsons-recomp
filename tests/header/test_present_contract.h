#pragma once
// Include inside test_engine_driver.cpp's anonymous namespace after require /
// rejects. Uses existing includes and public driver APIs. Place presentContracts
// AFTER cameraClearContracts (it changes the real source pixels), before stop.

struct PresentReceiptCheckpoint {
    std::array<uint32_t,4> receipts{};
    std::array<uint32_t,2> histories{},frontIds{};
};
struct PresentTestBytes {
    struct Range {uint8_t* at;std::vector<uint8_t> bytes;};
    std::vector<Range> ranges;
    void add(Simpsons::Runtime& runtime,uint32_t address,uint32_t length) {
        auto* p=runtime.pointer(address,length,true);ranges.push_back({p,{p,p+length}});
    }
    bool unchanged() const {
        for(const auto& r:ranges) if(std::memcmp(r.at,r.bytes.data(),r.bytes.size())) return false;
        return true;
    }
    void restore() const noexcept {for(const auto& r:ranges) std::memcpy(r.at,r.bytes.data(),r.bytes.size());}
};
struct PresentTestRestore {
    PresentTestBytes bytes;
    ~PresentTestRestore() {bytes.restore();}
};
inline bool presentSameState(const Simpsons::Graphics::EngineState& a,const Simpsons::Graphics::EngineState& b) {
    for(const auto& f:Simpsons::Graphics::scalarStateEvidence()) if(a.scalar(f.id)!=b.scalar(f.id)) return false;
    for(uint32_t s=0;s<16;++s) for(const auto& f:Simpsons::Graphics::samplerStateEvidence())
        if(a.sampler(s,f.id)!=b.sampler(s,f.id)) return false;
    for(uint32_t i=0;i<4;++i) if(a.effectiveBlend(i)!=b.effectiveBlend(i)) return false;
    return true;
}
inline void presentBytePins(uint8_t* base) {
    constexpr std::array<uint32_t,21> wrapper={0x7D8802A6,0x48634395,0x9421FF80,0x3D6082D1,
        0x7C7F1B78,0x7C9E2378,0x7CBD2B78,0x816BCA68,0x838B0098,0x4BFF2925,0x7FA5EB78,
        0x7FC4F378,0x7FE3FB78,0x7F8903A6,0x4E800421,0x2C030000,0x7FE3FB78,0x40820008,
        0x38600000,0x38210080,0x48634398};
    constexpr std::array<uint32_t,15> tail={0x3D6082D1,0x39200000,0x394BD0DC,0x396A0004,
        0x810A0000,0x39080001,0x912B0000,0x912B0004,0x912B0008,0x2B080004,0x912B000C,
        0x910A0000,0x4D980020,0x912A0000,0x4E800020};
    for(size_t i=0;i<wrapper.size();++i) require(PPC_LOAD_U32(0x82408030+uint32_t(4*i))==wrapper[i],"Original raster-present wrapper byte pin changed");
    for(size_t i=0;i<tail.size();++i) require(PPC_LOAD_U32(0x823FC5B8+uint32_t(4*i))==tail[i],"Original present CPU-tail byte pin changed");
}

// Isolated two-link fixture nodes occupy the caller's already reserved ABI
// scratch, not the original allocator. Only 823FA978 sees them. Original heads,
// Q fields and scratch bytes are restored before any later original service.
struct PresentListFixture {
    Simpsons::Runtime& runtime;
    uint8_t* base;
    uint32_t q{},a{},b{};
    std::array<uint32_t,4> nodes{};
    PresentTestRestore saved;
    std::vector<std::pair<uint32_t,uint32_t>> expected;
    PresentListFixture(Simpsons::Runtime& rt,uint8_t* memory,uint32_t scratch,uint32_t shape):runtime(rt),base(memory) {
        const uint64_t address=uint64_t(PPC_LOAD_U32(0x82D0CA68))+PPC_LOAD_U32(0x82E3DC88);
        require(address+0x28<=0x100000000ull && !(address&3),"Present list extension overflows guest memory");
        q=uint32_t(address);runtime.pointer(q,0x28,true);
        a=PPC_LOAD_U32(q+0x20);b=PPC_LOAD_U32(q+0x24);
        require(a && b && a!=b && !(a&3) && !(b&3),"Present original list heads are invalid");
        saved.bytes.add(runtime,q+8,4);saved.bytes.add(runtime,q+0x20,8);
        saved.bytes.add(runtime,a,8);saved.bytes.add(runtime,b,8);saved.bytes.add(runtime,scratch,32);
        for(uint32_t i=0;i<4;++i) nodes[i]=scratch+8*i;
        auto link=[&](uint32_t at,uint32_t next,uint32_t previous) {
            PPC_STORE_U32(at,next);PPC_STORE_U32(at+4,previous);
        };
        const bool source=shape&1,destination=shape&2;
        link(a,source?nodes[0]:a,source?nodes[1]:a);
        link(b,destination?nodes[2]:b,destination?nodes[3]:b);
        link(nodes[0],nodes[1],a);link(nodes[1],a,nodes[0]);
        link(nodes[2],nodes[3],b);link(nodes[3],b,nodes[2]);
        PPC_STORE_U32(q+8,0x5A11C0DE);
        // Derive the final circular order independently: destination's nodes
        // followed by source's nodes, then empty source and swapped head roles.
        std::vector<uint32_t> order;
        if(destination) {order.push_back(nodes[2]);order.push_back(nodes[3]);}
        if(source) {order.push_back(nodes[0]);order.push_back(nodes[1]);}
        for(uint32_t at:{a,b,nodes[0],nodes[1],nodes[2],nodes[3]}) {
            expected.push_back({at,PPC_LOAD_U32(at)});expected.push_back({at+4,PPC_LOAD_U32(at+4)});
        }
        auto expect=[&](uint32_t at,uint32_t value) {
            for(auto& word:expected) if(word.first==at) {word.second=value;return;}
            expected.push_back({at,value});
        };
        expect(a,a);expect(a+4,a);
        expect(b,order.empty()?b:order.front());expect(b+4,order.empty()?b:order.back());
        for(size_t i=0;i<order.size();++i) {
            expect(order[i],i+1<order.size()?order[i+1]:b);
            expect(order[i]+4,i?order[i-1]:b);
        }
        expect(q+0x20,b);expect(q+0x24,a);expect(q+8,0);
    }
    void verify() const {
        for(auto [address,value]:expected)
            require(PPC_LOAD_U32(address)==value,"Original present wrapper list splice differs from its circular-list contract");
    }
};

inline PPCFunc* presentOriginalPlatform{};
inline PPCFunc* presentOriginalTail{};
inline PresentListFixture* presentExpectedList{};
inline uint32_t presentExpectedRaster{},presentPlatformCalls{},presentTailCalls{};
inline void presentTracePlatform(PPCContext& ctx,uint8_t* base) {
    if(presentExpectedList) {
        require(ctx.r3.u32==presentExpectedRaster && ctx.r4.u32==0 && ctx.r5.u32==1 && ctx.lr==0x8240806C,
                "Original raster wrapper changed present arguments or callsite LR");
        presentExpectedList->verify(); // Splice must ALREADY have occurred.
    }
    ++presentPlatformCalls;presentOriginalPlatform(ctx,base);
}
inline void presentTraceTail(PPCContext& ctx,uint8_t* base) {
    require(ctx.lr==0x823EE8B0 && Simpsons::currentContext==&ctx,"Present CPU tail lost original LR/callback context");
    const uint32_t index=PPC_LOAD_U32(0x82D0D0DC),receipt=PPC_LOAD_U32(0x82D0CF94),previous=PPC_LOAD_U32(0x82D0CF98);
    // This presentation's copy may still be queued (native uploads snapshot
    // their bytes); every earlier presentation must have really completed.
    auto& driver=*Simpsons::active->engineDriver;
    require(index<4 && receipt,"Original CPU ring advanced without a native presentation receipt");
    (void)driver.submissionCompleted(receipt); // Known receipt; unknown identities reject.
    require(!previous || driver.submissionCompleted(previous),
            "Original CPU ring advanced before an earlier presentation's real copy completed");
    presentOriginalTail(ctx,base);++presentTailCalls;
    require(PPC_LOAD_U32(0x82D0D0DC)==(index+1)%4,"Original present tail did not advance the four-way ring");
    for(uint32_t i=0;i<4;++i) require(!PPC_LOAD_U32(0x82D0D0E0+4*i),"Original present tail failed to reset all four cursors");
}
struct PresentAotTrace {
    uint8_t* base;
    explicit PresentAotTrace(uint8_t* memory):base(memory) {
        require(!presentOriginalPlatform && !presentOriginalTail,"Nested present AOT fixture");
        auto* platform=PPC_LOOKUP_FUNC(base,0x823EE820);
        auto* tail=PPC_LOOKUP_FUNC(base,0x823FC5B8);
        require(platform && tail,"Original present/tail AOT mapping is missing");
        presentOriginalPlatform=platform;presentOriginalTail=tail;
        presentPlatformCalls=0;presentTailCalls=0;
        PPC_LOOKUP_FUNC(base,0x823EE820)=presentTracePlatform;PPC_LOOKUP_FUNC(base,0x823FC5B8)=presentTraceTail;
    }
    ~PresentAotTrace() {
        PPC_LOOKUP_FUNC(base,0x823EE820)=presentOriginalPlatform;PPC_LOOKUP_FUNC(base,0x823FC5B8)=presentOriginalTail;
        presentOriginalPlatform=nullptr;presentOriginalTail=nullptr;presentExpectedList=nullptr;
    }
};
inline void presentFixtureAbi(const PPCContext& now,const PPCContext& before) {
    require(now.r1.u64==before.r1.u64 && now.lr==before.lr &&
            now.r28.u64==before.r28.u64 && now.r29.u64==before.r29.u64 &&
            now.r30.u64==before.r30.u64 && now.r31.u64==before.r31.u64 &&
            now.r14.u64==before.r14.u64 && now.r20.u64==before.r20.u64 &&
            now.f14.u64==before.f14.u64 && now.f31.u64==before.f31.u64,
            "Original present wrapper/native callback damaged saved-register or stack ABI");
}

inline PresentReceiptCheckpoint presentContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    presentBytePins(base);
    auto& driver=*runtime.engineDriver;
    const uint32_t camera=PPC_LOAD_U32(0x82E07248),engine=PPC_LOAD_U32(0x82D0CA68);
    const uint32_t raster=PPC_LOAD_U32(camera+0x60),source=PPC_LOAD_U32(0x82D0CB00),depth=PPC_LOAD_U32(0x82D0CAFC);
    const PPCContext incoming=cpu.registers();
    struct ContextRestore {PPCContext& at;const PPCContext saved;~ContextRestore(){at=saved;}} contextRestore{cpu.registers(),incoming};
    if(PPC_LOAD_U32(0x82D0CB1C) || PPC_LOAD_U32(0x82E3DD60) || PPC_LOAD_U32(engine))
        require(cpu.invoke(0x823F1A08,camera)==camera,"Present fixture could not retain original camera-end wrapper");
    require(!PPC_LOAD_U32(0x82D0CB1C) && !PPC_LOAD_U32(0x82E3DD60) && !PPC_LOAD_U32(engine),
            "Present fixture has not ended the original camera");
    const auto cameraBefore=driver.cameraBinding();
    const auto effectiveBefore=driver.effectiveState();
    const auto depthBefore=driver.readbackDepth(depth);
    const uint32_t scratch=cpu.registers().r1.u32+0x40;
    PresentTestRestore scratchRestore;scratchRestore.bytes.add(runtime,scratch,0x40);
    PresentTestBytes stateBefore;
    stateBefore.add(runtime,0x82D0CF58,0x14);stateBefore.add(runtime,0x82D0D170,0x2FBC);
    stateBefore.add(runtime,0x82E3D160,0xB24);stateBefore.add(runtime,0x82D501E0,0x140);
    stateBefore.add(runtime,0x82D5DB78,0x41D4);
    PresentReceiptCheckpoint checkpoint;
    checkpoint.frontIds={PPC_LOAD_U32(0x82D0CF90),PPC_LOAD_U32(0x82D0CF8C)};
    bool alpha0=false,alpha1=false;
    const auto front0=driver.color(checkpoint.frontIds[0],alpha0),front1=driver.color(checkpoint.frontIds[1],alpha1);
    require(front0 && front1 && front0!=front1 && alpha0 && alpha1,"Present fixture lacks distinct alpha-one front roles");
    PresentAotTrace trace(base);
    presentExpectedRaster=raster;
    std::array<std::vector<uint8_t>,2> knownFrontPixels;
    const std::array<uint32_t,4> rgba={0xFF000000,0x00FF00FF,0x0000FF00,0xFFFFFFFF};
    const std::array<uint32_t,4> packed={0x000003FF,0xC00FFC00,0x3FF00000,0xFFFFFFFF};
    for(uint32_t frame=0;frame<4;++frame) {
        // Real endpoint-only engine clear makes every copy's source different,
        // including stored alpha zero. These are diagnostic fixture pixels.
        PPC_STORE_U32(scratch+0x30,rgba[frame]);driver.clearCamera(base,camera,scratch+0x30,1);
        const auto pixels=driver.readbackColor(source);
        require(pixels.size()==1280u*720u*4u,"Present source readback has the wrong extent");
        bool exact=true;
        for(size_t i=0;i<pixels.size();i+=4) {uint32_t word;std::memcpy(&word,pixels.data()+i,4);exact=exact && word==packed[frame];}
        require(exact,"Present fixture's real source pixels are not its requested packed endpoint values");
        const uint32_t old90=PPC_LOAD_U32(0x82D0CF90),old8C=PPC_LOAD_U32(0x82D0CF8C),old94=PPC_LOAD_U32(0x82D0CF94);
        const uint64_t attempts=driver.presentationAttemptCount(),copies=driver.frontCopyCount(),accepted=driver.presentationCount();
        PPC_STORE_U32(0x82D0D0DC,frame);
        for(uint32_t i=0;i<4;++i) {
            const uint32_t size=PPC_LOAD_U32(0x82D0D0F0+4*i);
            require(size>=256,"Original dynamic ring size cannot hold bounded cursor fixtures");
            PPC_STORE_U32(0x82D0D0E0+4*i,i==3?size:16*(frame+i+1));
        }
        cpu.registers().lr=0x823F1BEC;
        cpu.registers().r28.u64=0x2801020304050607ull;cpu.registers().r29.u64=0x2901020304050607ull;
        cpu.registers().r30.u64=0x3001020304050607ull;cpu.registers().r31.u64=0x3101020304050607ull;
        const PPCContext before=cpu.registers();
        const auto tails=presentTailCalls,platforms=presentPlatformCalls;
        {
            PresentListFixture lists(runtime,base,scratch,frame);
            presentExpectedList=&lists;
            const uint32_t result=cpu.invoke(0x82408030,raster,0,1);
            presentExpectedList=nullptr;
            require(result==raster,"Original raster-present wrapper lost its raster/Boolean return conversion");
            lists.verify();
        }
        require(presentPlatformCalls==platforms+1 && presentTailCalls==tails+1,"Present bypassed/duplicated the original wrapper or CPU tail");
        presentFixtureAbi(cpu.registers(),before);
        require(PPC_LOAD_U32(before.r1.u32-0x80)==before.r1.u32 && PPC_LOAD_U32(before.r1.u32-8)==uint32_t(before.lr) &&
                PPC_LOAD_U64(before.r1.u32-0x28)==before.r28.u64 && PPC_LOAD_U64(before.r1.u32-0x10)==before.r31.u64,
                "Present fixture did not execute the original wrapper's 80-byte frame/save helper");
        require(Simpsons::currentContext==&cpu.registers(),"Present leaked its nested original CPU callback context");
        require(PPC_LOAD_U32(0x82D0CF90)==old8C && PPC_LOAD_U32(0x82D0CF8C)==old90,"Original front-role rotation was omitted or reversed");
        const uint32_t receipt=PPC_LOAD_U32(0x82D0CF94);checkpoint.receipts[frame]=receipt;
        require(receipt>=0x00700001 && receipt<0x00800000 && !runtime.pageAccess[receipt>>12].load(),
                "Native present receipt is absent, reused as an SDK pointer or outside its identity range");
        require(receipt!=old94 && (!frame || receipt>checkpoint.receipts[frame-1]),"Native present receipt identity was reused");
        require(PPC_LOAD_U32(0x82D0CF98)==old94 && driver.waitSubmission(receipt),"Present history does not identify copy work that really completes");
        require(runtime.graphicsPresentReceipts.size()==(old94?2u:1u),"Present retained metadata outside its two original history words");
        require(driver.presentationAttemptCount()==attempts+1 && driver.frontCopyCount()==copies+1,
                "Successful present failed to record exactly one real copy/display attempt");
        require(driver.presentationCount()>=accepted && driver.presentationCount()<=accepted+1,
                "Present counted more than one accepted display or mishandled occlusion");
        // Existing public runtime metadata distinguishes acceptance from copy
        // completion; no accepted count or S_OK is treated as scanout evidence.
        const auto found=runtime.graphicsPresentReceipts.find(receipt);
        require(found!=runtime.graphicsPresentReceipts.end(),"Completed copy lacks persistent native receipt metadata");
        const auto& metadata=found->second;
        require(metadata.source==source && metadata.front==old90 && metadata.submitted && metadata.copyCompleted && metadata.displayTransferred,
                "Present receipt lost the actual source/front or completed transfer states");
        require(driver.presentationCount()==accepted+uint64_t(metadata.displayAccepted),"Occluded display was counted as accepted");
        require(driver.readbackColor(old90)==pixels && driver.readbackColor(source)==pixels,
                "Front copy lost packed color/alpha bits, copied the wrong resource, or altered its source");
        const uint32_t destinationIndex=old90==checkpoint.frontIds[0]?0:1,other=1-destinationIndex;
        if(!knownFrontPixels[other].empty())
            require(driver.readbackColor(old8C)==knownFrontPixels[other],"Present changed the other physical front texture");
        knownFrontPixels[destinationIndex]=pixels;
        bool a0=false,a1=false;
        require(driver.color(checkpoint.frontIds[0],a0)==front0 && driver.color(checkpoint.frontIds[1],a1)==front1 && a0 && a1,
                "Front rotation changed immutable native ID-to-resource ownership or alpha-one sampling policy");
        require(stateBefore.unchanged() && presentSameState(effectiveBefore,driver.effectiveState()),
                "Presentation altered engine target/state/application caches or inherited effective state");
        require(driver.cameraBinding().viewport==cameraBefore.viewport && !PPC_LOAD_U32(0x82D0CB1C) && !PPC_LOAD_U32(0x82E3DD60),
                "Presentation reactivated the camera or changed its logical reversed viewport");
    }
    require(driver.readbackDepth(depth)==depthBefore,"Color presentation altered depth/stencil contents");
    checkpoint.histories={PPC_LOAD_U32(0x82D0CF94),PPC_LOAD_U32(0x82D0CF98)};

    // Rejection tests call the service directly, without the preceding wrapper
    // splice. Query owner/counters only before corruption and after restoration.
    auto rejection=[&](uint32_t address,uint32_t value,bool byte=false,uint32_t argument=0xFFFFFFFFu,uint32_t stack=0xFFFFFFFFu) {
        const uint32_t saved=address?(byte?PPC_LOAD_U8(address):PPC_LOAD_U32(address)):0;
        const auto host=driver.effectiveState();
        const uint64_t copies=driver.frontCopyCount(),attempts=driver.presentationAttemptCount(),accepted=driver.presentationCount();
        const auto receiptCount=runtime.graphicsPresentReceipts.size();
        const auto tails=presentTailCalls;
        PresentTestBytes lists;const uint32_t q=engine+PPC_LOAD_U32(0x82E3DC88);
        lists.add(runtime,q,0x28);lists.add(runtime,PPC_LOAD_U32(q+0x20),8);lists.add(runtime,PPC_LOAD_U32(q+0x24),8);
        PresentTestRestore corruptRestore;
        if(address) {corruptRestore.bytes.add(runtime,address,byte?1:4);if(byte) PPC_STORE_U8(address,value);else PPC_STORE_U32(address,value);}
        PresentTestBytes bad;
        bad.add(runtime,0x82D0CF58,0x44);bad.add(runtime,0x82D0D0DC,0x34);
        bad.add(runtime,0x82E3DCE0,0x7C);bad.add(runtime,0x82D0CAF8,4);
        if(address) bad.add(runtime,address,byte?1:4);
        const uint32_t requested=argument==0xFFFFFFFFu?raster:argument;
        PPCContext call=cpu.registers();call.r3.u32=requested;call.lr=0x8240806C;
        if(stack!=0xFFFFFFFFu) call.r1.u64=stack;
        const PPCContext before=call;
        rejects([&]{driver.present(call,base,requested);});
        require(bad.unchanged() && lists.unchanged(),"Rejected direct present changed CPU words or performed the outer wrapper's list splice");
        presentFixtureAbi(call,before);
        if(address) {if(byte) PPC_STORE_U8(address,saved);else PPC_STORE_U32(address,saved);}
        require(driver.frontCopyCount()==copies && driver.presentationAttemptCount()==attempts && driver.presentationCount()==accepted &&
                runtime.graphicsPresentReceipts.size()==receiptCount && presentTailCalls==tails,
                "Rejected present copied/submitted work, published a receipt or advanced the CPU ring");
        require(presentSameState(host,driver.effectiveState()),"Rejected present changed native effective state");
    };
    for(auto [address,value]:std::array<std::pair<uint32_t,uint32_t>,15>{{
        {0x82D0CF90,PPC_LOAD_U32(0x82D0CF8C)},{0x82D0CF8C,0x00FFFFFF},
        {0x82D0CF5C,PPC_LOAD_U32(0x82D0CF90)},{0x82D0CF60,source},
        {0x82D0CAF8,0x12340000},{0x82D0CB1C,1},{0x82E3DD60,camera},
        {0x82E3DCE0,1279},{0x82E3DCE4,719},{0x82E3DCE8,0x18280086},
        {0x82E3DD20,0x182801B6},{0x82D0D0DC,4},{0x82D0D0DC,0xFFFFFFFF},
        {0x82D0D0E0,PPC_LOAD_U32(0x82D0D0F0)+1},{0x82D0D0EC,0xFFFFFFFF}}}) rejection(address,value);
    rejection(0x82D55BCE,1,true);
    rejection(0,0,false,0);rejection(0,0,false,raster+4);rejection(0,0,false,PPC_LOAD_U32(camera+0x64));
    rejection(0,0,false,raster,0);rejection(0,0,false,raster,cpu.registers().r1.u32+1);
    rejection(0x82D0CF94,0x007FFFFF);rejection(0x82D0CF98,0x007FFFFF);
    bool foreign=false;const auto copies=driver.frontCopyCount(),attempts=driver.presentationAttemptCount();
    PresentTestBytes foreignBytes;foreignBytes.add(runtime,0x82D0CF58,0x44);foreignBytes.add(runtime,0x82D0D0DC,0x34);
    const PPCContext foreignContext=cpu.registers();
    std::thread worker([&]{PPCContext call=foreignContext;call.lr=0x8240806C;call.r3.u32=raster;
        try {driver.present(call,base,raster);} catch(const Simpsons::Failure&) {foreign=true;}});
    worker.join();require(foreign && foreignBytes.unchanged() && driver.frontCopyCount()==copies && driver.presentationAttemptCount()==attempts,
                          "Present accepted a foreign thread or submitted before its owner check");
    for(uint32_t unknown:{0u,0x006FFFFFu,0x00800000u,checkpoint.receipts[0],checkpoint.receipts[1]})
        rejects([&]{driver.submissionCompleted(unknown);}); // Unknown/retired is not a claim of unfinished work.
    for(uint32_t i=0;i<2;++i)
        require(driver.readbackColor(checkpoint.frontIds[i])==knownFrontPixels[i],"Rejected present changed a real front's packed pixels");
    require(driver.readbackColor(source)==knownFrontPixels[PPC_LOAD_U32(0x82D0CF8C)==checkpoint.frontIds[0]?0:1] &&
            driver.readbackDepth(depth)==depthBefore,"Rejected present changed source or depth contents");
    return checkpoint; // Keep actual front/history/ring publications; do not fake rollback of GPU work.
}

// Call after the original stop/close/reopen/start sequence creates its new
// driver, before that cycle's stop. No new camera/submission readiness needed.
// This checks archived metadata only, not end-to-end rendering after restart.
inline void presentRestartContracts(Simpsons::Runtime& runtime,uint8_t* base,const PresentReceiptCheckpoint& before) {
    auto& driver=*runtime.engineDriver;
    require(PPC_LOAD_U32(0x82D0CF94)==before.histories[0] && PPC_LOAD_U32(0x82D0CF98)==before.histories[1],
            "Driver restart erased original retained presentation histories");
    for(uint32_t receipt:before.histories)
        require(receipt && driver.submissionCompleted(receipt),"Driver restart forgot a real completed native receipt");
    for(uint32_t receipt:before.receipts) if(receipt!=before.histories[0] && receipt!=before.histories[1])
        rejects([&]{driver.submissionCompleted(receipt);}); // Pruned metadata, not an unfinished GPU operation.
    for(uint32_t id:before.frontIds) {
        bool alpha=false;rejects([&]{driver.color(id,alpha);});
        require(id!=PPC_LOAD_U32(0x82D0CF90) && id!=PPC_LOAD_U32(0x82D0CF8C),"Driver restart reused a retired front identity");
    }
}
