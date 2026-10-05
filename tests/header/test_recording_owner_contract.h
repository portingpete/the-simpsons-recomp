#pragma once
// Include inside the driver's fixture namespace after require/rejects. Include
// runtime/engine_recording.h outside that namespace. Run while the driver and
// original allocator are live, before application construction of the singleton.

struct RecordingTestBytes {
    struct Range {uint8_t* at;std::vector<uint8_t> bytes;};
    std::vector<Range> ranges;
    void add(Simpsons::Runtime& runtime,uint32_t address,uint32_t size) {
        auto* p=runtime.pointer(address,size,false);ranges.push_back({p,{p,p+size}});
    }
    bool unchanged() const {
        for(const auto& r:ranges) if(std::memcmp(r.at,r.bytes.data(),r.bytes.size())) return false;
        return true;
    }
};
struct RecordingWordRestore {
    uint8_t* base;uint32_t address,value;
    RecordingWordRestore(uint8_t* memory,uint32_t at):base(memory),address(at),value(PPC_LOAD_U32(at)) {}
    ~RecordingWordRestore() {PPC_STORE_U32(address,value);}
};
struct RecordingContextRestore {
    PPCContext& ctx;PPCContext saved;
    explicit RecordingContextRestore(PPCContext& value):ctx(value),saved(value) {}
    ~RecordingContextRestore() {ctx=saved;}
};
inline std::array<uint64_t,15> recordingAbi(const PPCContext& c) {
    return {c.r1.u64,c.lr,c.r3.u64,c.r4.u64,c.r5.u64,c.r6.u64,c.r7.u64,
        c.r8.u64,c.r9.u64,c.r10.u64,c.r11.u64,c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64};
}
inline std::vector<uint32_t> recordingPoolChain(Simpsons::Runtime& runtime,uint8_t* base) {
    std::vector<uint32_t> chain;
    for(uint32_t p=PPC_LOAD_U32(0x82DFE10C);p;p=PPC_LOAD_U32(p+4)) {
        require(chain.size()<65536 && std::find(chain.begin(),chain.end(),p)==chain.end(),"Recording fixture found a cyclic pool registry");
        runtime.pointer(p,0x38,false);chain.push_back(p);
    }
    return chain;
}
inline std::vector<std::array<uint32_t,4>> recordingEventNodes(Simpsons::Runtime& runtime,uint8_t* base) {
    std::vector<std::array<uint32_t,4>> result;
    if(const uint32_t root=PPC_LOAD_U32(0x82D61DD0)) {
        runtime.pointer(root,0x20,false);uint32_t previous=root;
        for(uint32_t at=PPC_LOAD_U32(root);at;at=PPC_LOAD_U32(at)) {
            require(result.size()<65536 && at!=root &&
                std::none_of(result.begin(),result.end(),[&](const auto& n){return n[0]==at;}),"Recording event fixture found a cycle");
            runtime.pointer(at,0x10,false);
            require(PPC_LOAD_U32(at+4)==previous,"Recording event fixture found an invalid backlink");
            result.push_back({at,PPC_LOAD_U32(at+8),PPC_LOAD_U16(at+0xC),PPC_LOAD_U16(at+0xE)});previous=at;
        }
    }
    return result;
}
inline void recordingSnapshot(Simpsons::Runtime& runtime,uint8_t* base,uint32_t owner,RecordingTestBytes& bytes) {
    bytes.add(runtime,owner,0x7C);
    for(uint32_t at:{0x82D09784u,0x82DFE10Cu,0x82D6D890u,0x82D63028u,0x82D0CAF8u,0x82D0CB08u}) bytes.add(runtime,at,4);
    for(uint32_t p:recordingPoolChain(runtime,base)) bytes.add(runtime,p,0x38);
    bytes.add(runtime,PPC_LOAD_U32(owner+0x3C),2000*0x34+4+0x13);
    bytes.add(runtime,0x82D61DD0,8);
    if(uint32_t root=PPC_LOAD_U32(0x82D61DD0)) {
        bytes.add(runtime,root,0x10);std::vector<uint32_t> nodes{root};
        for(uint32_t node=PPC_LOAD_U32(root);node;node=PPC_LOAD_U32(node)) {
            require(nodes.size()<65536 && std::find(nodes.begin(),nodes.end(),node)==nodes.end(),"Recording fixture found a cyclic event registry");
            bytes.add(runtime,node,0x10);nodes.push_back(node);
        }
    }
}
inline void recordingOwnerContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    using namespace Simpsons;
    auto& driver=*runtime.engineDriver;auto& owners=driver.recordingOwners();auto& ctx=cpu.registers();
    RecordingContextRestore restoreContext(ctx);
    require(!PPC_LOAD_U32(0x82D09784) && owners.count()==0,"Recording fixture needs the original empty manager singleton");
    owners.requireEmpty();
    constexpr std::array<std::pair<uint32_t,uint32_t>,10> pins={{{0x826F4988,0x7D8802A6},
        {0x826F4AD0,0x4BD5DA71},{0x826F4AD4,0x7FE3FB78},{0x826F3908,0x7D8802A6},
        {0x826F394C,0x480571C5},{0x826F3954,0x4BF9CE3D},{0x826F396C,0x916A9784},
        {0x826F39A8,0x3863FFFC},{0x826F39AC,0x48001134},{0x826F4AFC,0x4BFFEE0D}}};
    for(auto [address,word]:pins) require(PPC_LOAD_U32(address)==word,"Recording original hook/CPU-helper byte pin changed");
    const std::array<uint32_t,3> aliases={PPC_LOAD_U32(0x82D6D890),PPC_LOAD_U32(0x82D63028),PPC_LOAD_U32(0x82D0CAF8)};
    auto aliasesUnchanged=[&] {
        require(PPC_LOAD_U32(0x82D6D890)==aliases[0] && PPC_LOAD_U32(0x82D63028)==aliases[1] &&
            PPC_LOAD_U32(0x82D0CAF8)==aliases[2],"Recording ownership changed main/secondary context aliases or CAF8");
    };
    const auto originalPools=recordingPoolChain(runtime,base);
    const auto originalEventNodes=recordingEventNodes(runtime,base);
    const uint32_t originalEventRoot=PPC_LOAD_U32(0x82D61DD0);
    const uint16_t originalEventCount=PPC_LOAD_U16(0x82D61DD4);
    // Use the original allocator and real CPU pool helpers. Retain this storage
    // across two nondeleting lifetimes to force deterministic same-address reuse.
    const uint32_t owner=cpu.invoke(0x8269BD70,0x7C);
    require(owner!=0,"Original recording-owner allocation failed");runtime.pointer(owner,0x7C,true);
    {
        RecordingContextRestore restore(ctx);ctx.r3.u64=owner;ctx.lr=0;
        RecordingTestBytes before;before.add(runtime,owner,0x7C);
        for(uint32_t at:{0x82D09784u,0x82DFE10Cu,0x82D6D890u,0x82D63028u,0x82D0CAF8u}) before.add(runtime,at,4);
        const auto abi=recordingAbi(ctx);
        rejects([&]{cpu.invoke(0x826F4988);});
        require(ctx.lastFunction==0x826F4988 && before.unchanged() && recordingAbi(ctx)==abi && owners.count()==0,
            "Wrong original constructor caller reached CPU publication or native ownership");
        rejects([&]{owners.createCommit(ctx,base);});rejects([&]{owners.destroyCommit(ctx,base);});
        require(before.unchanged() && recordingAbi(ctx)==abi && owners.count()==0,"Unscoped recording commit mutated empty ownership");
    }
    uint32_t previousId=0;
    for(uint32_t cycle=0;cycle<3;++cycle) {
        std::memset(runtime.pointer(owner,0x7C,true),0xA5,0x7C);
        const uint32_t sentinel=0xC060A500+cycle;PPC_STORE_U32(owner+0x60,sentinel);
        ctx.r28.u64=0x1828384858687888ull;ctx.r29.u64=0x1929394959697989ull;
        ctx.r30.u64=0x1A2A3A4A5A6A7A8Aull;ctx.r31.u64=0x1B2B3B4B5B6B7B8Bull;
        const std::array<uint64_t,4> nonvol={ctx.r28.u64,ctx.r29.u64,ctx.r30.u64,ctx.r31.u64};
        const uint64_t sp=ctx.r1.u64;ctx.lr=0x823B748C;
        require(cpu.invoke(0x826F4988,owner)==owner,"Original recording constructor returned the wrong owner");
        require(ctx.r1.u64==sp && ctx.lr==0x823B748C &&
            std::array<uint64_t,4>{ctx.r28.u64,ctx.r29.u64,ctx.r30.u64,ctx.r31.u64}==nonvol,
            "Recording constructor lost its original stack/LR/nonvolatile ABI");
        const uint32_t id=owners.identity(owner);auto weak=owners.nativeContext(owner,id);
        require(id>=0x00600001 && id<0x00700000 && id>previousId && !runtime.pageAccess[id>>12].load(),
            "Recording identity is reused, mapped, or outside its bounded namespace");
        require(owners.count()==1 && !weak.expired(),"Recording constructor did not retain a real native context owner");
        owners.validateOwner(owner,id);
        if(previousId) rejects([&]{owners.validateOwner(owner,previousId);});
        require(PPC_LOAD_U32(0x82D09784)==owner && PPC_LOAD_U32(owner)==0x820B856C &&
            PPC_LOAD_U32(owner+4)==0x820B8564 && PPC_LOAD_U32(owner+8)==1 && PPC_LOAD_U32(owner+0x68)==id &&
            PPC_LOAD_U32(owner+0x60)==sentinel,"Recording constructor lost CPU identity or changed untouched O+60");
        for(uint32_t off:{0x44u,0x48u,0x4Cu,0x50u,0x54u,0x58u,0x5Cu,0x64u,0x6Cu,0x70u,0x74u,0x78u})
            require(!PPC_LOAD_U32(owner+off),"Original constructor did not initialize empty CPU recording fields");
        auto expectedPools=originalPools;expectedPools.insert(expectedPools.begin(),owner+0xC);
        require(recordingPoolChain(runtime,base)==expectedPools,"Original constructor failed to publish exactly one CPU pool");
        const uint32_t pool=owner+0xC,block=PPC_LOAD_U32(pool+0x30),first=PPC_LOAD_U32(block+8);
        require(PPC_LOAD_U32(pool+0xC)==2000 && !PPC_LOAD_U32(pool+0x10) && PPC_LOAD_U32(pool+0x14)==2000 &&
            PPC_LOAD_U32(pool+0x18)==2000 && PPC_LOAD_U32(pool+0x1C)==1 && PPC_LOAD_U32(pool+0x28)==0x34,
            "Original constructor skipped the real 2000-item pool prewarm");
        // The first borrowed item was cleared by the constructor and returned.
        for(uint32_t i=1;i<13;++i) require(PPC_LOAD_U32(first+4*i)==0,"Original recording prewarm omitted item initialization");
        aliasesUnchanged();
        std::fprintf(stderr,"[RECORDING TEST] cycle=%u owner=%08X id=%08X pool=%08X block=%08X available=%u native-owned\n",
            cycle,owner,id,pool,block,PPC_LOAD_U32(pool+0xC));

        if(cycle==0) {
            auto rejectDirect=[&](const std::function<void()>& action) {
                RecordingTestBytes snapshot;recordingSnapshot(runtime,base,owner,snapshot);
                const auto abi=recordingAbi(ctx);rejects(action);
                require(snapshot.unchanged(),"Rejected recording operation changed CPU owner/pool/registry bytes");
                require(recordingAbi(ctx)==abi,"Rejected recording operation changed its callback ABI");
                require(owners.count()==1 && !weak.expired(),"Rejected recording operation lost native ownership");
            };
            rejectDirect([&]{owners.requireEmpty();});
            rejectDirect([&]{owners.validateOwner(owner+4,id);});
            rejectDirect([&]{owners.validateOwner(owner,id+1);});
            rejectDirect([&]{owners.createCommit(ctx,base);});
            rejectDirect([&]{owners.destroyCommit(ctx,base);});
            for(const auto site:{0x82737400u,0x8273743Cu,0x827374A4u})
                rejectDirect([&]{owners.deleteRecord(ctx,base,site);});
            rejectDirect([&]{owners.pluginDestroyBegin(ctx,base);});
            ctx.r3.u64=owner;ctx.lr=0x823B748C;
            rejectDirect([&]{owners.createBegin(ctx,base);}); // Duplicate before original publication.
            ctx.lr=0x826F4B00;
            rejectDirect([&]{owners.destroyBegin(ctx,base+0x1000);});
            {PPCContext foreign=ctx;rejectDirect([&]{owners.destroyBegin(foreign,base);});}
            ctx.lr=0x826F3908;
            rejectDirect([&]{owners.destroyBegin(ctx,base);});
            ctx.lr=0x826F4B00;
            // stop constructs its own ABI scratch frame before requireEmpty;
            // compare resource/CPU registries, not that documented scratch.
            rejectDirect([&]{driver.stop(ctx,base);});
            require(driver.started() && driver.submissionConfigured(),"Rejected stop released the live driver");
            auto corrupt=[&](uint32_t address,uint32_t value) {
                RecordingTestBytes snapshot;recordingSnapshot(runtime,base,owner,snapshot);
                RecordingWordRestore restore(base,address);PPC_STORE_U32(address,value);
                // Refresh saved byte ranges without following the deliberately
                // corrupted pointer. Only this test's one word has changed.
                for(auto& range:snapshot.ranges) std::memcpy(range.bytes.data(),range.at,range.bytes.size());
                const auto abi=recordingAbi(ctx);
                rejects([&]{owners.destroyBegin(ctx,base);});
                require(PPC_LOAD_U32(address)==value && snapshot.unchanged(),
                    "Malformed recording destructor changed rejected input");
                require(recordingAbi(ctx)==abi && owners.count()==1 && !weak.expired(),"Malformed recording destructor changed ABI/native lifetime");
            };
            corrupt(0x82D09784,owner+4);corrupt(owner,0);corrupt(owner+4,0);corrupt(owner+8,0);
            corrupt(owner+0x68,id+1);corrupt(owner+0x54,1);corrupt(owner+0x78,1);
            corrupt(pool+0xC,1999);corrupt(pool+0x10,1);corrupt(pool+0x14,1999);
            corrupt(pool+0x30,0);corrupt(pool+0x34,first+1);corrupt(block+4,block);
            corrupt(first,first);corrupt(pool+4,pool);corrupt(0x82DFE10C,0);
            corrupt(0x82D6D890,id);corrupt(0x82D63028,id);
            corrupt(0x82D6D890,id+1);corrupt(0x82D63028,id+1);corrupt(0x82D0CAF8,1);
            owners.validateOwner(owner,id);aliasesUnchanged();
            bool threadRejected=false;
            std::thread worker([&] {
                try {owners.validateOwner(owner,id);} catch(const Failure&) {threadRejected=true;}
            });worker.join();
            require(threadRejected,"Recording native ownership accepted a foreign thread");
            require(!weak.expired() && owners.count()==1,"Wrong-thread rejection released native ownership");
        }

        // Both real vtable routes reach the same scoped original destructor.
        // The last route sets deleting bit 0 and executes original 8269BEB0.
        const uint32_t receiver=owner+(cycle==0?0:4),vtable=PPC_LOAD_U32(receiver),entry=PPC_LOAD_U32(vtable);
        require(entry==(cycle==0?0x826F4AE0u:0x826F39A8u),"Original recording deletion vtable mapping changed");
        RecordingTestBytes eventBefore;eventBefore.add(runtime,0x82D61DD0,8);
        const auto nodesBefore=recordingEventNodes(runtime,base);
        if(originalEventRoot) eventBefore.add(runtime,originalEventRoot,0x20);
        for(const auto& n:nodesBefore) eventBefore.add(runtime,n[0],0x10);
        const uint32_t flags=cycle==2?1:0;ctx.lr=0x823B748C;
        require(cpu.invoke(entry,receiver,flags)==owner,"Original recording deleting wrapper returned the wrong owner");
        require(ctx.r1.u64==sp && ctx.lr==0x823B748C &&
            std::array<uint64_t,4>{ctx.r28.u64,ctx.r29.u64,ctx.r30.u64,ctx.r31.u64}==nonvol,
            "Recording destructor lost its original stack/LR/nonvolatile ABI");
        require(!PPC_LOAD_U32(0x82D09784) && owners.count()==0 && weak.expired(),
            "Paired recording destructor retained singleton/native context ownership");
        require(recordingPoolChain(runtime,base)==originalPools,"Original destructor failed to unlink exactly its CPU pool");
        require(eventBefore.unchanged(),"Original manager destructor unexpectedly changed iMsgPreRender subscription bytes");
        owners.requireEmpty();rejects([&]{owners.validateOwner(owner,id);});rejects([&]{owners.identity(owner);});
        if(!flags) {
            require(PPC_LOAD_U32(owner)==0x820B8560 && PPC_LOAD_U32(owner+4)==0x82001660 &&
                PPC_LOAD_U32(pool)==0x8215093C && !PPC_LOAD_U32(owner+0x68) && PPC_LOAD_U32(owner+0x60)==sentinel,
                "Paired destructor omitted original CPU base metadata or native identity clear");
            // P+30 and P+34 are dangling original values now; never dereference.
        }
        // The manager destructor leaves iMsgPreRender subscribed. Production
        // retains that original effect until global event teardown. This live,
        // repeated-lifetime fixture instead explicitly calls the ORIGINAL
        // unsubscribe service as fixture cleanup (not as a native destructor
        // effect). It compares r3 to node+8; it never dereferences the receiver,
        // including after the final deleting wrapper has freed O.
        if(originalEventRoot) {
            require(!(PPC_LOAD_U16(originalEventRoot+0x1E)&0xC000),"Recording fixture cannot unlink during event dispatch/deferred cleanup");
            require(PPC_LOAD_U32(0x8268F4A4)==0x7F071840 && PPC_LOAD_U32(0x8268F4C0)==0xB1440004 &&
                PPC_LOAD_U32(0x8268F4D4)==0xB14B000C && PPC_LOAD_U32(0x8268F530)==0x4807D648,
                "Original event unsubscribe comparison/count/pool-return pins changed");
            cpu.invoke(0x8268F470,owner+4,0x82D61DD0);
            require(PPC_LOAD_U32(0x82D61DD0)==originalEventRoot && PPC_LOAD_U16(0x82D61DD4)==originalEventCount &&
                recordingEventNodes(runtime,base)==originalEventNodes,"Original fixture unsubscribe did not remove exactly its receiver reference");
        }
        aliasesUnchanged();previousId=id;
    }
    // The final deleting wrapper freed O. No subsequent read through O or its
    // freed CPU pool is allowed, even when original heap pages remain mapped.
    std::fprintf(stderr,"[RECORDING TEST] PASS: three original CPU lifetimes, O/O+4 deletion, same-address stale IDs, malformed preflight and host release\n");
}
