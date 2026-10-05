#pragma once
// Real dispatcher/helper, including the changed-cache SDK call, pending alpha
// queue effects and cache-hit suppression. GPU preservation is tested separately.
inline void nullRasterContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    RwCpuContractFixture f(runtime,cpu,base);
    auto& driver=*runtime.engineDriver;
    const uint32_t texture=PPC_LOAD_U32(0x82E071E8),raster=PPC_LOAD_U32(texture);
    const uint32_t refs=PPC_LOAD_U32(texture+0x54);
    const auto held=driver.textureRaster(raster);
    auto* scratch=runtime.pointer(f.sp-0xF0,0xF0,true);
    const std::vector<uint8_t> stack(scratch,scratch+0xF0);
    struct StackRestore {uint8_t* p;const std::vector<uint8_t>& bytes;~StackRestore(){std::copy(bytes.begin(),bytes.end(),p);}} restore{scratch,stack};
    for(uint32_t stage=0;stage<8;++stage) for(uint32_t bound=0;bound<2;++bound)
    for(uint32_t alpha=0;alpha<2;++alpha) for(uint32_t vertex=0;vertex<2;++vertex) {
        f.reset();
        PPC_STORE_U32(0x82D0E3F8+24*stage,bound?raster:0);
        PPC_STORE_U32(0x82D0E3DC,alpha);PPC_STORE_U32(0x82D0E3D8,vertex);
        PPC_STORE_U32(f.pending+8*0x3C,1);PPC_STORE_U32(f.pending+8*0x60,1);
        auto expected=f.snapshot();
        RwCpuContractFixture::put(expected,0x82D0E3F8+24*stage,0);
        if(!stage && alpha) {
            RwCpuContractFixture::put(expected,0x82D0E3DC,0);
            if(!vertex) {
                for(uint32_t id:{0x3Cu,0x60u}) {
                    RwCpuContractFixture::put(expected,f.pending+8*id,0);
                    RwCpuContractFixture::put(expected,f.pending+8*id+4,1);
                }
                RwCpuContractFixture::put(expected,f.count,2);
                RwCpuContractFixture::put(expected,f.queue,0x3C);
                RwCpuContractFixture::put(expected,f.queue+4,0x60);
            }
        }
        f.prepare(0x11223344);
        const auto result=stage?cpu.invoke(0x82401940,0,stage):cpu.invoke(0x824025A8,1,0);
        require(result==1,"Original null raster helper lost its return value");f.abi(0x11223344);
        require(f.snapshot()==expected,"Null raster changed unexpected source/cache/queue words");
        f.unchangedOwners();
        require(PPC_LOAD_U32(texture+0x54)==refs && driver.textureRaster(raster)==held,
                "Null raster binding released a logical texture reference");
    }
    f.reset();PPC_STORE_U32(0x82D0E3DC,1);PPC_STORE_U32(0x82D0E3D8,0);
    f.seedDirty(0x3C,1);f.seedDirty(0x60,1);
    auto expected=f.snapshot();
    RwCpuContractFixture::put(expected,0x82D0E3DC,0);
    RwCpuContractFixture::put(expected,f.pending+8*0x3C,0);RwCpuContractFixture::put(expected,f.pending+8*0x60,0);
    require(cpu.invoke(0x824025A8,1,0)==1 && f.snapshot()==expected,"Null raster duplicated existing alpha queue entries");
    f.unchangedOwners();
    f.reset();PPC_STORE_U32(0x82D0E3DC,1);PPC_STORE_U32(0x82D0E3D8,0);
    PPC_STORE_U32(f.pending+8*0x3C+4,1); // Dirty without membership: reject before original publication.
    expected=f.snapshot();
    rejects([&]{cpu.invoke(0x824025A8,1,0);});
    require(f.snapshot()==expected,"Rejected null raster partially published alpha/cache state");
    f.reset();expected=f.snapshot();
    rejects([&]{cpu.invoke(0x82401940,0,8);});
    rejects([&]{cpu.invoke(0x82401940,raster+4,0);});
    require(f.snapshot()==expected,"Unsupported raster binding changed CPU state");

    // The same original dispatcher/helper now consumes a checked owned raster.
    // Alpha queues remain deferred, including the cache-hit path. Nothing in
    // this fixture changes the original raster metadata or its texture refcount.
    for(uint32_t stage=0;stage<8;++stage) for(uint32_t alpha=0;alpha<2;++alpha)
    for(uint32_t vertex=0;vertex<2;++vertex) for(uint32_t retained=0;retained<2;++retained) {
        f.reset();PPC_STORE_U32(0x82D0E3F8+24*stage,0);
        PPC_STORE_U32(0x82D0E3DC,alpha);PPC_STORE_U32(0x82D0E3D8,vertex);PPC_STORE_U32(0x82D0E4C4,retained);
        PPC_STORE_U32(f.pending+8*0x3C,0);PPC_STORE_U32(f.pending+8*0x60,0);
        expected=f.snapshot();f.put(expected,0x82D0E3F8+24*stage,raster);
        if(!stage && !alpha) {
            f.put(expected,0x82D0E3DC,1);
            if(!vertex) {
                f.put(expected,f.pending+8*0x3C,1);f.put(expected,f.pending+8*0x3C+4,1);
                f.put(expected,f.queue,0x3C);f.put(expected,f.count,1);
                if(retained) {
                    f.put(expected,f.pending+8*0x60,1);f.put(expected,f.pending+8*0x60+4,1);
                    f.put(expected,f.queue+4,0x60);f.put(expected,f.count,2);
                }
            }
        }
        f.prepare(0x55667788);
        require((stage?cpu.invoke(0x82401940,raster,stage):cpu.invoke(0x824025A8,1,raster))==1,
                "Original owned raster binding lost its return value");
        f.abi(0x55667788);require(f.snapshot()==expected,"Owned raster changed unexpected alpha/cache/queue words");
        f.unchangedOwners();
        require(cpu.invoke(0x82401940,raster,stage)==1 && f.snapshot()==expected,
                "Owned raster cache hit changed state or duplicated dirty entries");
        require(PPC_LOAD_U32(texture+0x54)==refs && driver.textureRaster(raster)==held,
                "Owned raster bind/cache hit changed texture ownership");
        require(cpu.invoke(0x82401940,0,stage)==1,"Owned raster fixture could not clear its native binding");
    }
    f.reset();PPC_STORE_U32(0x82D0E3F8,raster);PPC_STORE_U32(0x82D0E3DC,0);
    expected=f.snapshot();
    rejects([&]{cpu.invoke(0x824025A8,1,raster);});
    require(f.snapshot()==expected,"A cache hit without its native view changed CPU state");
    f.reset();PPC_STORE_U32(0x82D0E3F8,0);PPC_STORE_U32(0x82D0E3DC,0);
    PPC_STORE_U32(0x82D0E3D8,0);PPC_STORE_U32(0x82D0E4C4,1);
    for(uint32_t id=0;id<425;++id) if(id!=0x3C) f.seedDirty(id,0);
    PPC_STORE_U32(f.queue+4*424,0xFFFFFFFF);PPC_STORE_U32(f.count,425);
    expected=f.snapshot();rejects([&]{cpu.invoke(0x824025A8,1,raster);});
    require(f.snapshot()==expected && driver.textureRaster(raster)==held,
            "Queue-full texture rejection changed cache/queue/resource ownership");
}
