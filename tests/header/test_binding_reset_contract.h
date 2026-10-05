#pragma once
// Included inside the driver fixture namespace after its checked helpers.
inline void bindingResetContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    auto& driver=*runtime.engineDriver;
    const uint32_t color=PPC_LOAD_U32(0x82D0CB00),depth=PPC_LOAD_U32(0x82D0CAFC);
    const auto pixels=driver.readbackColor(color),depthPixels=driver.readbackDepth(depth);
    const auto camera=driver.cameraBinding();
    const auto count=driver.bindingResetCount(),clears=driver.cameraClearCount();
    require(count>0,"Original mode-zero reset did not reach native bindings");
    const uint32_t texture=PPC_LOAD_U32(0x82E071E8),raster=PPC_LOAD_U32(texture);
    const uint32_t references=PPC_LOAD_U32(texture+0x54);
    auto held=driver.textureRaster(raster);
    std::array<uint32_t,4> untouched{};
    for(uint32_t i=0;i<4;++i) {
        untouched[i]=PPC_LOAD_U32(0x82D0CABC+16*i);
        PPC_STORE_U32(0x82D0CAB0+16*i,PPC_LOAD_U32(0x82D0D100+4*i));
        PPC_STORE_U32(0x82D0CAB4+16*i,16*i);
        PPC_STORE_U32(0x82D0CAB8+16*i,28);
        PPC_STORE_U32(0x82D0CABC+16*i,0xA55A0000+i);
    }
    PPC_STORE_U32(0x82CD1A6C,PPC_LOAD_U32(0x82CF231C));
    PPC_STORE_U32(0x82CD1A70,PPC_LOAD_U32(0x82CF2310));
    PPC_STORE_U32(0x82CD1A68,PPC_LOAD_U32(0x82DFEB30));
    PPC_STORE_U32(0x82CD1A74,PPC_LOAD_U32(0x82D101D4));
    // A real owned texture in each RW cache exercises its null path, including
    // clearing texture-derived alpha flags without a logical reference release.
    for(uint32_t i=0;i<8;++i) PPC_STORE_U32(0x82D0E3F8+24*i,raster);
    PPC_STORE_U32(0x82D0E3DC,1);PPC_STORE_U32(0x82D0E3D8,0);
    std::array<std::array<uint32_t,5>,8> requests{};
    for(uint32_t i=0;i<8;++i) for(uint32_t j=0;j<5;++j)
        requests[i][j]=PPC_LOAD_U32(0x82D0E3FC+24*i+4*j);
    const auto sp=cpu.registers().r1.u32;
    cpu.registers().r22.u64=0x11223344AABBCCDDull;cpu.registers().r31.u64=0xAABBCCDD11223344ull;
    cpu.registers().lr=0x12345678;
    cpu.invoke(0x823EFDA0);
    require(cpu.registers().r1.u32==sp && cpu.registers().lr==0x12345678 &&
            cpu.registers().r22.u64==0x11223344AABBCCDDull && cpu.registers().r31.u64==0xAABBCCDD11223344ull,
            "Native engine binding reset damaged caller nonvolatile registers or stack");
    require(driver.bindingResetCount()==count+1 && driver.cameraClearCount()==clears,
            "Native binding reset was missing or incorrectly cleared attachments");
    require(PPC_LOAD_U32(0x82CD1A64)==0xFFFFFFFF,"Native reset lost original declaration invalidation sentinel");
    for(uint32_t field:{0x82CD1A68u,0x82CD1A6Cu,0x82CD1A70u,0x82CD1A74u})
        require(PPC_LOAD_U32(field)==0,"Native reset left an original resource cache selected");
    for(uint32_t i=0;i<4;++i) {
        require(!PPC_LOAD_U32(0x82D0CAB0+16*i) && !PPC_LOAD_U32(0x82D0CAB4+16*i) &&
                !PPC_LOAD_U32(0x82D0CAB8+16*i) && PPC_LOAD_U32(0x82D0CABC+16*i)==0xA55A0000+i,
                "Native reset changed the wrong stream record words");
        PPC_STORE_U32(0x82D0CABC+16*i,untouched[i]);
    }
    for(uint32_t i=0;i<8;++i) {
        require(!PPC_LOAD_U32(0x82D0E3F8+24*i),"Original null texture helper left a raster cached");
        for(uint32_t j=0;j<5;++j) require(PPC_LOAD_U32(0x82D0E3FC+24*i+4*j)==requests[i][j],
                "Native reset replaced retained RW texture-stage requests with defaults");
    }
    require(!PPC_LOAD_U32(0x82D0E3DC) && PPC_LOAD_U32(texture+0x54)==references && driver.textureRaster(raster)==held,
            "Native reset lost original alpha-flag semantics or released a logical texture reference");
    require(driver.cameraBinding().viewport==camera.viewport && PPC_LOAD_U32(0x82D0CF5C)==color && PPC_LOAD_U32(0x82D0CF58)==depth,
            "Cache-hit default target reset changed logical viewport or target roles");
    require(driver.readbackColor(color)==pixels && driver.readbackDepth(depth)==depthPixels,
            "Native binding reset altered color/depth/stencil contents");
    const auto committed=driver.bindingResetCount();
    const auto effective=driver.effectiveState();
    auto* cache=runtime.pointer(0x82D0D170,0x2FBC,false);
    const std::vector<uint8_t> before(cache,cache+0x2FBC);
    PPC_STORE_U32(0x82CD1A6C,0x00EFFFFF);
    rejects([&]{cpu.invoke(0x823EFDA0);});
    require(PPC_LOAD_U32(0x82CD1A6C)==0x00EFFFFF && std::vector<uint8_t>(cache,cache+0x2FBC)==before &&
            driver.bindingResetCount()==committed,"Rejected resource identity changed reset state");
    PPC_STORE_U32(0x82CD1A6C,0);
    for(const auto& field:Simpsons::Graphics::scalarStateEvidence())
        require(driver.effectiveState().scalar(field.id)==effective.scalar(field.id),"Rejected reset changed effective scalar state");
    bool foreign=false;
    std::thread worker([&]{try {driver.resetBindings(cpu.registers(),base);} catch(const Simpsons::Failure&) {foreign=true;}});
    worker.join();require(foreign && driver.bindingResetCount()==committed,"Binding reset accepted a foreign thread");
}
