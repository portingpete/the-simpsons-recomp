#pragma once
// Real AOT dispatcher/helper paths on the running native driver's CPU caches.
// Expected bytes are independently derived from the reviewed original queue
// schedule. Effective native state must remain untouched until a later commit.
inline void rwFrontendContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    const size_t firstCheck=checks;
    RwCpuContractFixture f(runtime,cpu,base);
    constexpr uint32_t lr=0x826D5184;
    auto get=[&](const std::vector<uint8_t>& bytes,uint32_t address) {
        const auto i=address-f.cacheBase;
        return (uint32_t(bytes[i])<<24)|(uint32_t(bytes[i+1])<<16)|(uint32_t(bytes[i+2])<<8)|bytes[i+3];
    };
    auto queue=[&](std::vector<uint8_t>& e,uint32_t id,uint32_t value,bool helper=false) {
        if(helper && get(e,f.pending+8*id)==value)return;
        f.put(e,f.pending+8*id,value);
        if(!get(e,f.pending+8*id+4)) {
            const auto n=get(e,f.count);f.put(e,f.pending+8*id+4,1);
            f.put(e,f.queue+4*n,id);f.put(e,f.count,n+1);
        }
    };
    constexpr std::array<uint32_t,12> factors={0,0,1,4,5,6,7,10,11,8,9,16};
    for(uint32_t selector:{10u,11u})for(uint32_t request=0;request<factors.size();++request)for(bool dirty:{false,true}) {
        f.reset();const uint32_t cache=selector==10?0x82D0E4B8:0x82D0E4BC,id=selector==10?0x48:0x4C;
        PPC_STORE_U32(cache,(request+1)%12);f.seedDirty(0x38,2);
        if(dirty)f.seedDirty(id,0x11111111);
        auto e=f.snapshot();f.put(e,cache,request);queue(e,id,factors[request]);
        f.call(selector,request,lr,e);
        // Retained-request equality suppresses even an independently changed
        // pending value; direct application state and RW requests are distinct.
        PPC_STORE_U32(f.pending+8*id,0x11223344);f.call(selector,request,lr,f.snapshot());
    }
    for(uint32_t old:{0u,1u})for(uint32_t other:{0u,1u})for(uint32_t request:{0u,1u,2u,0xFFFFFFFFu})for(uint32_t dirty=0;dirty<4;++dirty) {
        f.reset();PPC_STORE_U32(0x82D0E3B4,old);PPC_STORE_U32(0x82D0E3B0,other);
        if(dirty&2)f.seedDirty(0x2C,7);if(dirty&1)f.seedDirty(0x28,0);
        auto e=f.snapshot();const uint32_t normalized=request!=0;
        if(old!=normalized) {
            f.put(e,0x82D0E3B4,normalized);
            if(!other)queue(e,0x28,normalized);
            queue(e,0x2C,normalized?6:7);
        }
        f.call(6,request,lr,e);f.call(6,request,lr,f.snapshot());
    }
    for(uint32_t old:{0u,1u})for(uint32_t texture:{0u,1u})for(uint32_t alpha:{0u,1u})
        for(uint32_t request:{0u,1u,2u,0xFFFFFFFFu})for(uint32_t dirty=0;dirty<4;++dirty) {
        f.reset();PPC_STORE_U32(0x82D0E3D8,old);PPC_STORE_U32(0x82D0E3DC,texture);PPC_STORE_U32(0x82D0E4C4,alpha);
        PPC_STORE_U32(f.pending+8*0x60,alpha);
        if(dirty&2)f.seedDirty(0x60,alpha);if(dirty&1)f.seedDirty(0x3C,0);
        auto e=f.snapshot();const uint32_t normalized=request!=0;
        if(old!=normalized) {
            f.put(e,0x82D0E3D8,normalized);
            if(!texture) {queue(e,0x3C,normalized);queue(e,0x60,normalized?alpha:0,true);}
        }
        f.call(12,request,lr,e);f.call(12,request,lr,f.snapshot());
    }
    for(uint32_t request=0;request<256;++request)for(bool dirty:{false,true}) {
        f.reset();PPC_STORE_U32(f.pending+8*0x64,request^255);
        f.seedDirty(0x38,2);if(dirty)f.seedDirty(0x64,request^255);
        auto e=f.snapshot();queue(e,0x64,request,true);
        f.call(30,request,lr,e);f.call(30,request,lr,f.snapshot());
    }
    // Last available queue slot and full-but-already-dirty overwrites.
    f.reset();PPC_STORE_U32(0x82D0E4B8,0);
    for(uint32_t id=0;id<425;++id)if(id!=0x48)f.seedDirty(id);
    auto e=f.snapshot();f.put(e,0x82D0E4B8,5);queue(e,0x48,6);f.call(10,5,lr,e);
    e=f.snapshot();f.put(e,0x82D0E4B8,6);queue(e,0x48,7);f.call(10,6,lr,e);

    for(uint32_t request:{12u,0xFFFFFFFFu}) {
        f.reset();f.reject(10,request,lr,"Original RenderWare blend index is out of bounds");
        f.reject(11,request,lr,"Original RenderWare blend index is out of bounds");
    }
    for(uint32_t request:{256u,0xFFFFFFFFu}) {
        f.reset();f.reject(30,request,lr,"Unqualified RenderWare alpha reference above255");
    }
    for(uint32_t address:{0x82D0E3B0u,0x82D0E3B4u}) {
        f.reset();PPC_STORE_U32(address,2);f.reject(6,0,lr,"Original RenderWare depth-test Boolean is invalid");
    }
    for(uint32_t address:{0x82D0E3D8u,0x82D0E3DCu,0x82D0E4C4u}) {
        f.reset();PPC_STORE_U32(address,2);f.reject(12,0,lr,"Original RenderWare alpha Boolean is invalid");
    }
    for(uint32_t dirty:{1u,2u}) {
        f.reset();PPC_STORE_U32(0x82D0E4B8,0);PPC_STORE_U32(f.pending+8*0x48+4,dirty);
        f.reject(10,5,lr,"Original RenderWare scalar dirty membership is inconsistent");
    }
    f.reset();PPC_STORE_U32(0x82D0E4B8,0);f.seedDirty(0x48);f.seedDirty(0x48);
    f.reject(10,5,lr,"Original RenderWare scalar dirty membership is inconsistent");
    f.reset();PPC_STORE_U32(0x82D0E4B8,0);PPC_STORE_U32(f.count,426);
    f.reject(10,5,lr,"Original RenderWare scalar dirty queue is invalid");
    f.reset();PPC_STORE_U32(0x82D0E4B8,0);
    for(uint32_t id=0;id<425;++id)if(id!=0x48)f.seedDirty(id);
    PPC_STORE_U32(f.queue+4*424,0);PPC_STORE_U32(f.count,425);
    f.reject(10,5,lr,"Original RenderWare scalar dirty queue is full");
    f.reset();const auto factor=PPC_LOAD_U32(0x82062CBC+20);PPC_STORE_U32(0x82062CBC+20,factor^1);
    f.reject(10,5,lr,"Original RenderWare blend conversion table changed");PPC_STORE_U32(0x82062CBC+20,factor);
    f.reset();f.reject(13,1,lr,"Unimplemented native engine graphics boundary 0x824025A8,");
    std::printf("PASS original frontend RenderWare setters:%zu checks; deferred original CPU queue, complete cache bytes and ABI; no immediate draw claim\n",checks-firstCheck);
}
