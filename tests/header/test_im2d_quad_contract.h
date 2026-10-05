#pragma once
// Real original frontend quad producer -> original Im2D upload -> native raw
// storage -> owned native decode. Synthetic caller data, no game draw success.
void im2dQuadContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    using namespace Simpsons::Graphics;
    const auto first=checks;auto& driver=*runtime.engineDriver;
    const uint32_t camera=PPC_LOAD_U32(0x82E07248),engine=PPC_LOAD_U32(0x82D0CA68);
    require(cpu.invoke(0x823F1A18,camera)==camera,"Quad fixture failed original camera begin");
    require(cpu.invoke(0x824025A8,1,0)==1,"Quad fixture failed original null raster state");
    cpu.invoke(0x82400040);
    const PPCContext incoming=cpu.registers();
    Im2DUploadObservation observation(base); // Independently isolate producer/upload from the draw tests.
    const uint32_t memory=runtime.allocatePhysical(0,4096,PAGE_READWRITE,0,UINT32_MAX,4096);
    require(memory!=0,"Quad caller fixture allocation failed");
    std::fill_n(runtime.pointer(memory,4096,true),4096,0);
    auto put=[&](uint32_t offset,float value){PPC_STORE_U32(memory+offset,std::bit_cast<uint32_t>(value));};
    auto loadFloat=[&](uint32_t address){return std::bit_cast<float>(PPC_LOAD_U32(address));};
    put(0,0.125f);put(4,0.25f);put(8,0.375f);put(12,0.5f);
    put(32,0.125f);put(36,0.25f);put(40,0.5f);put(44,0.5f);
    put(48,0.25f);put(52,0.5f);put(56,0);put(60,1); // Pixel pivot, 90 degree rotation.
    PPC_STORE_U32(memory+256+12,camera);
    const uint32_t originalFrontend=PPC_LOAD_U32(0x82D097AC);
    auto* arrays=runtime.pointer(0x82D0D0DC,0x34,true);
    auto* scratch=runtime.pointer(0x82D101C4,0x18,true);
    const std::vector<uint8_t> savedArrays(arrays,arrays+0x34),savedScratch(scratch,scratch+0x18);
    const auto draws=driver.screenDrawCount(),presents=driver.presentationCount();
    const auto beforeColor=driver.readbackColor(driver.cameraBinding().colorIdentity);
    const auto beforeDepth=driver.readbackDepth(driver.cameraBinding().depthIdentity);
    const auto nativeBefore=driver.effectiveState();
    DeclarationRegistry declarations;
    const std::array<uint8_t,48> originalDeclaration={
        0,0,0,0,0,0x1A,0x23,0xA6,0,0,0,0,0,0,0,16,0,0x18,0x28,0x86,0,10,0,0,
        0,0,0,20,0,0x2C,0x23,0xA5,0,5,0,0,0,0xFF,0,0,0xFF,0xFF,0xFF,0xFF,0,0,0,0};
    const auto declaration=declarations.record(declarations.create(originalDeclaration));
    const uint32_t raster=PPC_LOAD_U32(camera+0x60);
    const float width=float(PPC_LOAD_U32(raster+12)),height=float(PPC_LOAD_U32(raster+16));
    const float z=loadFloat(engine+0x18),rhw=loadFloat(0x82000BB0)/loadFloat(camera+0x80);
    require(std::isfinite(z) && std::isfinite(rhw),"Original camera quad constants are nonfinite");
    const auto physicalCount=runtime.physicalAllocations.size();
    try {
        PPC_STORE_U32(0x82D097AC,memory+256);
        for(bool rotate:{false,true})for(uint32_t c=0;c<16;++c) {
            cpu.registers()=incoming;
            std::memcpy(arrays,savedArrays.data(),savedArrays.size());
            std::memcpy(scratch,savedScratch.data(),savedScratch.size());
            PPC_STORE_U32(0x82D0D0DC,0);
            for(uint32_t i=0;i<4;++i)PPC_STORE_U32(0x82D0D0E0+4*i,0);
            auto* color=runtime.pointer(memory+16,4,true);
            color[0]=uint8_t(c*17);color[1]=uint8_t(255-c*13);color[2]=uint8_t(c*11+37);color[3]=uint8_t(c*7+19);
            const auto uploads=driver.im2DUploadCount();
            cpu.registers().r8.u32=memory+48;cpu.registers().r9.u32=rotate?memory+56:0;
            bool reached=false;
            try {cpu.invoke(0x826D54B8,memory,memory+8,memory+16,0,memory+32);}
            catch(const Simpsons::Failure& e) {
                if(std::string(e.what()).find("Original Im2D setup has unqualified explicit shader bindings")==std::string::npos)throw;
                reached=true;
            }
            require(reached && driver.im2DUploadCount()==uploads+1,"Original quad did not upload before its real fixed-function guard");
            require(cpu.registers().r1.u32==incoming.r1.u32-0x130-0xA0,"Original quad/Im2D stack frames differ");
            const auto buffer=driver.readbackDynamicBuffer(PPC_LOAD_U32(0x82D101CC));
            require(PPC_LOAD_U32(0x82D101C8)==0 && PPC_LOAD_U32(0x82D0D0E0)==112,"Original quad allocation range differs");
            const auto vertices=decodeIm2DVertices(*declaration,std::span(buffer.data(),112),true);
            const uint32_t originalSource=incoming.r1.u32-0x130+0x60;
            require(std::equal(buffer.begin(),buffer.begin()+112,runtime.pointer(originalSource,112,false)),"GPU storage differs from original quad stack vertices");
            for(size_t i=0;i<4;++i) {
                const float x=(i>=2?0.5f:0.125f)*width,y=(i&1?0.75f:0.25f)*height;
                const float px=0.25f*width,py=0.5f*height;
                const auto& v=vertices[i];
                require(v.position[0]==(rotate?px-(y-py):x) && v.position[1]==(rotate?py+(x-px):y),"Original normalized quad/rotation coordinates differ");
                require(v.position[2]==z && v.position[3]==rhw,"Original camera quad Z/RHW differs");
                require(v.uv[0]==(i>=2?0.625f:0.125f) && v.uv[1]==(i&1?0.75f:0.25f),"Original quad UV rectangle differs");
                for(size_t channel=0;channel<4;++channel)require(v.color[channel]==float(color[channel])/255.0f,"Original frontend byte packing or native RGBA conversion differs");
                require(buffer[i*28+16]==color[3] && buffer[i*28+17]==color[0] && buffer[i*28+18]==color[1] && buffer[i*28+19]==color[2],"Original quad stored a different ARGB word");
            }
            require(runtime.physicalAllocations.size()==physicalCount,"Original quad upload leaked native staging");
        }
        require(driver.screenDrawCount()==draws && driver.presentationCount()==presents &&
            driver.readbackColor(driver.cameraBinding().colorIdentity)==beforeColor &&
            driver.readbackDepth(driver.cameraBinding().depthIdentity)==beforeDepth,
            "Original quad fixture falsely drew, presented or changed targets");
        for(const auto& field:scalarStateEvidence())require(driver.effectiveState().scalar(field.id)==nativeBefore.scalar(field.id),"Original quad prefix changed native scalar state");
    }catch(...) {
        PPC_STORE_U32(0x82D097AC,originalFrontend);cpu.registers()=incoming;
        std::memcpy(arrays,savedArrays.data(),savedArrays.size());std::memcpy(scratch,savedScratch.data(),savedScratch.size());
        runtime.freePhysical(memory);throw;
    }
    PPC_STORE_U32(0x82D097AC,originalFrontend);cpu.registers()=incoming;
    std::memcpy(arrays,savedArrays.data(),savedArrays.size());std::memcpy(scratch,savedScratch.data(),savedScratch.size());
    runtime.freePhysical(memory);
    require(cpu.invoke(0x823F1A08,camera)==camera,"Quad fixture failed original camera end");
    std::fprintf(stderr,"PASS original frontend Im2D quad:%zu checks; original rectangle/rotation/color/UV builder and native uploaded bytes, no draw success\n",checks-first);
}
