#pragma once
// Include after RwCpuContractFixture inside the driver's test namespace.
// Exercise the original dispatcher and scalar helper, without committing the
// deferred state or replacing either original body with a test implementation.
inline void rwOverlayStateContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    const size_t firstCheck=checks;
    RwCpuContractFixture f(runtime,cpu,base);
    constexpr uint32_t lr=0x827F5B20,compareCache=0x82D0E4C0,alphaFlag=0x82D0E4C4;
    constexpr uint32_t blendPending=0x82D0F590,alphaId=0x60,compareId=0x68;
    struct Conversion {
        uint32_t selector,table,cache,id,size;
        std::array<uint32_t,9> values;
    };
    constexpr std::array<Conversion,3> conversions={{
        {7,0x82062CA0,0x82D0E3F4,0x195,3,{0,1,2}},
        {20,0x82062D70,0x82D0E3E0,0x38,4,{0,0,2,6}},
        {29,0x82062DA4,compareCache,compareId,9,{0,0,1,2,3,4,5,6,7}}
    }};
    auto get=[&](const std::vector<uint8_t>& bytes,uint32_t address) {
        const auto i=address-f.cacheBase;
        return (uint32_t(bytes[i])<<24)|(uint32_t(bytes[i+1])<<16)|
               (uint32_t(bytes[i+2])<<8)|bytes[i+3];
    };
    auto enqueue=[&](std::vector<uint8_t>& e,uint32_t id,uint32_t value,bool helper=false) {
        // Only 82400170 suppresses an equal pending value. The three inline
        // dispatcher paths always write/queue after their RW request changes.
        if(helper && get(e,f.pending+8*id)==value)return;
        f.put(e,f.pending+8*id,value);
        if(!get(e,f.pending+8*id+4)) {
            const auto n=get(e,f.count);
            f.put(e,f.pending+8*id+4,1);f.put(e,f.queue+4*n,id);f.put(e,f.count,n+1);
        }
    };
    auto expected=[&](const Conversion& conversion,uint32_t request) {
        auto e=f.snapshot();
        if(get(e,conversion.cache)==request)return e;
        f.put(e,conversion.cache,request);
        if(conversion.selector==29) {
            const uint32_t flag=request!=8;
            f.put(e,alphaFlag,flag);
            if(get(e,blendPending))enqueue(e,alphaId,flag,true);
        }
        enqueue(e,conversion.id,conversion.values[request]);
        return e;
    };
    auto resetChanged=[&](const Conversion& conversion) {
        f.reset();PPC_STORE_U32(conversion.cache,0);
        if(conversion.selector==29) {
            PPC_STORE_U32(alphaFlag,0);PPC_STORE_U32(blendPending,1);
            PPC_STORE_U32(f.pending+8*alphaId,0);
        }
    }; // Request 1 changes the cache and, for selector 29, pending alpha too.

    for(const auto& conversion:conversions)
        for(uint32_t i=0;i<conversion.size;++i)
            require(PPC_LOAD_U32(conversion.table+4*i)==conversion.values[i],
                    "RW overlay conversion fixture differs from the original table");

    // Every shade/cull request and retained-request pair, with clean/dirty and
    // equal/different pending values. A changed request still queues when its
    // converted value equals pending (including cull indices 0 and 1).
    for(const auto& conversion:conversions) {
        if(conversion.selector==29)continue;
        for(uint32_t old=0;old<conversion.size;++old)
            for(uint32_t request=0;request<conversion.size;++request)
                for(bool dirty:{false,true})for(bool equalPending:{false,true}) {
            f.reset();PPC_STORE_U32(conversion.cache,old);f.seedDirty(0x28,1);
            const uint32_t pending=equalPending?conversion.values[request]:0x11223344;
            PPC_STORE_U32(f.pending+8*conversion.id,pending);
            if(dirty)f.seedDirty(conversion.id,pending);
            f.call(conversion.selector,request,lr,expected(conversion,request));
            // A matching retained request suppresses all pending writes even
            // if another CPU state producer has changed that pending value.
            PPC_STORE_U32(f.pending+8*conversion.id,0x55667788);
            f.call(conversion.selector,request,lr,f.snapshot());
        }
    }

    const auto& compare=conversions[2];
    // All compare indices, both blend states, both pending alpha Booleans and
    // all dirty permutations. Seed 68 before 60 to distinguish preservation
    // of existing order from the original append order (60, then 68).
    for(uint32_t request=0;request<compare.size;++request)
        for(uint32_t blend:{0u,1u})for(uint32_t alpha:{0u,1u})
            for(uint32_t dirty=0;dirty<4;++dirty)for(bool equalCompare:{false,true}) {
        f.reset();PPC_STORE_U32(compareCache,(request+1)%compare.size);
        PPC_STORE_U32(alphaFlag,uint32_t(request==8));PPC_STORE_U32(blendPending,blend);
        PPC_STORE_U32(f.pending+8*alphaId,alpha);
        const uint32_t pending=equalCompare?compare.values[request]:0x11223344;
        PPC_STORE_U32(f.pending+8*compareId,pending);f.seedDirty(0x28,1);
        if(dirty&2)f.seedDirty(compareId,pending);
        if(dirty&1)f.seedDirty(alphaId,alpha);
        f.call(29,request,lr,expected(compare,request));
        // Neither helper nor inline path runs again for a matching request.
        PPC_STORE_U32(f.pending+8*alphaId,uint32_t(request==8));
        PPC_STORE_U32(f.pending+8*compareId,0x55667788);
        f.call(29,request,lr,f.snapshot());
    }
    // Cache equality also preserves an independently retained alpha flag;
    // it is not permission to rebuild alpha state from the compare enum.
    for(uint32_t request=0;request<compare.size;++request)for(uint32_t flag:{0u,1u}) {
        f.reset();PPC_STORE_U32(compareCache,request);PPC_STORE_U32(alphaFlag,flag);
        PPC_STORE_U32(blendPending,1);PPC_STORE_U32(f.pending+8*alphaId,flag^1);
        f.call(29,request,lr,f.snapshot());
    }

    // Last legal slot and subsequent full-queue overwrite for each inline ID.
    for(const auto& conversion:conversions) {
        resetChanged(conversion);
        for(uint32_t id=0;id<425;++id)if(id!=conversion.id)f.seedDirty(id,0);
        // With blending disabled, compare must leave pending alpha untouched.
        f.call(conversion.selector,1,lr,expected(conversion,1));
        require(PPC_LOAD_U32(f.count)==425,"RW overlay did not fill the last legal queue slot");
        f.call(conversion.selector,2,lr,expected(conversion,2));
    }
    // All 60/68 dirty permutations at the exact remaining capacity. Changing
    // compare again on a full queue must overwrite both without appending.
    for(uint32_t dirty=0;dirty<4;++dirty) {
        resetChanged(compare);
        for(uint32_t id=0;id<425;++id)if(id!=alphaId && id!=compareId)f.seedDirty(id,0);
        PPC_STORE_U32(blendPending,1);
        if(dirty&2)f.seedDirty(compareId,0);
        if(dirty&1)f.seedDirty(alphaId,0);
        f.call(29,1,lr,expected(compare,1));
        require(PPC_LOAD_U32(f.count)==425,"RW overlay alpha/compare missed its exact queue capacity");
        f.call(29,8,lr,expected(compare,8));
    }
    // Pending alpha equality suppresses its helper, even with a clean 60 slot.
    // Only 68 is appended; this is not an unconditional two-state schedule.
    resetChanged(compare);PPC_STORE_U32(f.pending+8*alphaId,1);
    f.call(29,1,lr,expected(compare,1));
    require(PPC_LOAD_U32(f.count)==1 && PPC_LOAD_U32(f.queue)==compareId &&
            PPC_LOAD_U32(f.pending+8*alphaId+4)==0,
            "RW overlay queued an unchanged alpha-helper value");

    constexpr const char* bounds="Original RenderWare overlay index is out of bounds";
    constexpr const char* tableChanged="Original RenderWare overlay conversion table changed";
    constexpr const char* membership="Original RenderWare scalar dirty membership is inconsistent";
    constexpr const char* full="Original RenderWare scalar dirty queue is full";
    constexpr const char* invalidQueue="Original RenderWare scalar dirty queue is invalid";
    for(const auto& conversion:conversions) {
        for(uint32_t request:{conversion.size,conversion.size+1,0x80000000u,0xFFFFFFFFu}) {
            resetChanged(conversion);f.reject(conversion.selector,request,lr,bounds);
        }
        for(uint32_t n:{426u,0x80000000u,0xFFFFFFFFu}) {
            resetChanged(conversion);PPC_STORE_U32(f.count,n);
            f.reject(conversion.selector,1,lr,invalidQueue);
        }
        // A table mutation is restored even if the rejection assertion throws.
        // Check every entry both as the selected index and as an unrelated one.
        for(uint32_t i=0;i<conversion.size;++i) {
            const uint32_t address=conversion.table+4*i;
            {
                struct RestoreWord {
                    uint8_t* bytes;
                    std::array<uint8_t,4> saved;
                    explicit RestoreWord(uint8_t* p):bytes(p) {std::memcpy(saved.data(),p,4);}
                    ~RestoreWord() noexcept {std::memcpy(bytes,saved.data(),4);}
                    RestoreWord(const RestoreWord&)=delete;
                    RestoreWord& operator=(const RestoreWord&)=delete;
                } restore(runtime.pointer(address,4,true));
                PPC_STORE_U32(address,conversion.values[i]^1);
                for(uint32_t request:{i,(i+1)%conversion.size}) {
                    resetChanged(conversion);PPC_STORE_U32(conversion.cache,(request+1)%conversion.size);
                    f.reject(conversion.selector,request,lr,tableChanged);
                }
            }
            require(PPC_LOAD_U32(address)==conversion.values[i],"RW overlay table corruption fixture leaked a write");
        }
    }

    // Validate the membership of every state the original schedule will write:
    // invalid Boolean, missing entry, clean flag with an entry, and duplicates.
    for(const auto& conversion:conversions) {
        const std::array<uint32_t,2> ids={conversion.id,alphaId};
        const uint32_t selected=conversion.selector==29?2:1;
        for(uint32_t index=0;index<selected;++index) {
            const uint32_t id=ids[index];
            for(uint32_t dirty:{1u,2u,0xFFFFFFFFu}) {
                resetChanged(conversion);PPC_STORE_U32(f.pending+8*id+4,dirty);
                f.reject(conversion.selector,1,lr,membership);
            }
            resetChanged(conversion);f.seedDirty(id,0);PPC_STORE_U32(f.pending+8*id+4,0);
            f.reject(conversion.selector,1,lr,membership);
            resetChanged(conversion);f.seedDirty(id,0);f.seedDirty(id,0);
            f.reject(conversion.selector,1,lr,membership);
        }
    }

    // Filling a queue while omitting one of its 425 IDs necessarily requires
    // an unrelated duplicate. Keep the selected membership correct so these
    // failures specifically exercise capacity and precede all original writes.
    for(const auto& conversion:conversions) {
        resetChanged(conversion);
        for(uint32_t id=0;id<425;++id)if(id!=conversion.id)f.seedDirty(id,0);
        PPC_STORE_U32(f.queue+4*424,0);PPC_STORE_U32(f.count,425);
        f.reject(conversion.selector,1,lr,full);
    }
    for(uint32_t dirty=0;dirty<3;++dirty) {
        resetChanged(compare);
        for(uint32_t id=0;id<425;++id)if(id!=alphaId && id!=compareId)f.seedDirty(id,0);
        PPC_STORE_U32(blendPending,1);
        if(dirty&2)f.seedDirty(compareId,0);
        if(dirty&1)f.seedDirty(alphaId,0);
        // At 424 entries, two clean selected IDs cannot both be appended.
        if(!dirty) {
            PPC_STORE_U32(f.queue+4*423,0);PPC_STORE_U32(f.count,424);
            f.reject(29,1,lr,full);
        }
        PPC_STORE_U32(f.queue+4*424,0);PPC_STORE_U32(f.count,425);
        f.reject(29,1,lr,full);
    }
    for(uint32_t invalid:{2u,0x80000000u,0xFFFFFFFFu}) {
        resetChanged(compare);PPC_STORE_U32(blendPending,invalid);
        f.reject(29,1,lr,"Original RenderWare overlay blend Boolean is invalid");
    }
    for(uint32_t selector:{0u,13u}) {
        f.reset();f.reject(selector,1,lr,"Unimplemented native engine graphics boundary 0x824025A8,");
    }
    std::printf("PASS original overlay RenderWare setters:%zu checks; original deferred shade/cull/alpha queues, complete cache bytes and ABI; no immediate draw claim\n",checks-firstCheck);
}
