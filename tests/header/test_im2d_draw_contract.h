#pragma once
// Original Im2D prologue, allocator, memcpy, setup/commit, real native draw,
// override reset and return. This fixture supplies vertices; it is not gameplay.
void im2dDrawContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    using namespace Simpsons::Graphics;
    const auto first=checks;auto& driver=*runtime.engineDriver;
    const uint32_t camera=PPC_LOAD_U32(0x82E07248);
    require(cpu.invoke(0x823F1A18,camera)==camera,"Im2D draw camera begin failed");
    require(cpu.invoke(0x824025A8,1,0)==1,"Im2D draw null raster selection failed");
    cpu.invoke(0x82400040);
    const PPCContext incoming=cpu.registers();const auto beforeState=driver.effectiveState();
    struct Saved {uint8_t* p;std::vector<uint8_t> bytes;};std::vector<Saved> saved;
    for(auto [a,n]:std::array<std::pair<uint32_t,uint32_t>,8>{{{0x82D0D170,0x2FBC},{0x82E3D160,0xB24},
        {0x82D501E0,0x140},{0x82D50328,0xB8},{0x82D0D0DC,0x34},{0x82D101C4,0x18},{0x82CD1A64,0x14},{0x82D0CB14,4}}}) {
        auto* p=runtime.pointer(a,n,true);saved.push_back({p,{p,p+n}});
    }
    auto* stream=runtime.pointer(0x82D0CAB0,16,true);saved.push_back({stream,{stream,stream+16}});
    const auto source=runtime.allocatePhysical(0,4096,PAGE_READWRITE,0,UINT32_MAX,4096);
    require(source!=0,"Im2D draw input allocation failed");
    auto restore=[&] {
        cpu.registers()=incoming;
        for(const auto& field:scalarStateEvidence())driver.directScalar(base,field.id,beforeState.scalar(field.id));
        for(auto& s:saved)std::copy(s.bytes.begin(),s.bytes.end(),s.p);
        runtime.freePhysical(source);
    };
    try {
        for(uint32_t i=0;i<4;++i) {
            const auto row=source+28*i;
            PPC_STORE_U32(row,std::bit_cast<uint32_t>(i>=2?24.5f:8.5f));
            PPC_STORE_U32(row+4,std::bit_cast<uint32_t>(i&1?24.5f:8.5f));
            PPC_STORE_U32(row+8,0);PPC_STORE_U32(row+12,0x7FA12345); // Original VS ignores RHW.
            PPC_STORE_U32(row+16,0xFFFF0000);PPC_STORE_U32(row+20,0x7FC12345);PPC_STORE_U32(row+24,0x7FC12345);
        }
        const uint32_t rgba=source+128;PPC_STORE_U32(rgba,0);
        require(cpu.invoke(0x823EE940,camera,rgba,1)==1,"Im2D draw color clear failed");
        const auto binding=driver.cameraBinding();const auto depth=driver.readbackDepth(binding.depthIdentity);
        const auto physicalCount=runtime.physicalAllocations.size();
        const auto uploads=driver.im2DUploadCount(),draws=driver.im2DDrawCount(),screens=driver.screenDrawCount(),presents=driver.presentationCount();
        driver.directScalar(base,0x134,1); // Actual boot140's retained target0 expansion request.
        // These are actual original queued requests;82408CC0 must commit them.
        for(auto [id,value]:std::array<std::pair<uint32_t,uint32_t>,10>{{{0x28,0},{0x30,0},{0x2C,7},
            {0x38,2},{0x3C,1},{0x60,1},{0x64,0},{0x68,4},{0x48,6},{0x4C,7}}})cpu.invoke(0x82400170,id,value);
        require(PPC_LOAD_U32(0x82D10114)>0,"Im2D draw fixture lacks pending original state");
        const auto call=cpu.registers();
        require(cpu.invoke(0x82409308,4,source,4)==1,"Original Im2D did not return its own success Boolean");
        require(cpu.registers().r1.u32==call.r1.u32 && cpu.registers().lr==call.lr,"Original Im2D epilogue failed stack/LR restoration");
        require(cpu.registers().r24.u64==call.r24.u64 && cpu.registers().r25.u64==call.r25.u64 &&
            cpu.registers().r26.u64==call.r26.u64 && cpu.registers().r27.u64==call.r27.u64 &&
            cpu.registers().r28.u64==call.r28.u64 && cpu.registers().r29.u64==call.r29.u64 &&
            cpu.registers().r30.u64==call.r30.u64 && cpu.registers().r31.u64==call.r31.u64,
            "Original Im2D changed nonvolatile registers");
        require(driver.im2DUploadCount()==uploads+1 && driver.im2DDrawCount()==draws+1,"Original Im2D did not submit exactly one uploaded draw");
        require(!PPC_LOAD_U32(0x82D503D4) && !PPC_LOAD_U32(0x82D10114) && !PPC_LOAD_U32(0x82D10118),
            "Original Im2D did not reset screen override or commit pending queues");
        require(driver.effectiveState().effectiveBlend(0)==0x07060706,"Original Im2D lost nonseparate alpha blend");
        require(driver.effectiveState().scalar(ScalarState::ExpandedBlend0)==1,"Native Im2D cleared the retained expansion request");
        const auto slot=PPC_LOAD_U32(0x82D0D0DC),start=PPC_LOAD_U32(0x82D101C8);
        require(start*28==PPC_LOAD_U32(0x82D0D0E0+4*slot),"Original Im2D final start vertex did not advance by count");
        const auto pixels=driver.readbackColor(binding.colorIdentity);
        for(size_t y=0;y<720;++y)for(size_t x=0;x<1280;++x) {
            uint32_t value;std::memcpy(&value,pixels.data()+4*(y*1280+x),4);
            if(value!=((x>=8 && x<24 && y>=8 && y<24)?0xC00003FFu:0u))
                throw Simpsons::Failure("Original Im2D rectangle coverage/color differs from real native readback");
        }
        ++checks;
        require(driver.readbackDepth(binding.depthIdentity)==depth,"Original Im2D altered disabled depth/stencil");
        require(driver.screenDrawCount()==screens && driver.presentationCount()==presents,"Original Im2D fabricated screen draw or presentation");
        require(runtime.physicalAllocations.size()==physicalCount,"Original Im2D leaked upload/program scratch");
        // Cached declaration and stream must still have real native owners on
        // the second draw when the original CPU setters omit redundant work.
        require(cpu.invoke(0x82409308,4,source,4)==1 && driver.im2DDrawCount()==draws+2,"Original Im2D cached binding reuse failed");
        require(driver.readbackColor(binding.colorIdentity)==pixels,"Repeated opaque Im2D draw changed its exact result");
        driver.directScalar(base,0x134,0);
        require(cpu.invoke(0x82409308,4,source,4)==1 && driver.im2DDrawCount()==draws+3,
            "Original Im2D failed after the real expansion request changed");
        require(driver.readbackColor(binding.colorIdentity)==pixels,"Im2D expansion transition lost packed storage");
        // Actual154's original UI background, captured before upload. The
        // shipped VS preserves Z; camera selection retains endpoints1,0.
        // Replaying through82409308 tests the bridge/CB/PS together, including
        // the original upload, queue commit and override-reset epilogue.
        constexpr std::array actualVertices={
            std::array{0xBE72C000u,0xBED38800u,0x3EFF7CEEu,0x41200000u,0xFF000000u,0u,0u},
            std::array{0xBE72C000u,0x4433E798u,0x3EFF7CEEu,0x41200000u,0xFF000000u,0u,0u},
            std::array{0x44A0489Au,0xBED38800u,0x3EFF7CEEu,0x41200000u,0xFF000000u,0u,0u},
            std::array{0x44A0489Au,0x4433E798u,0x3EFF7CEEu,0x41200000u,0xFF000000u,0u,0u}};
        for(uint32_t i=0;i<4;++i)for(uint32_t j=0;j<7;++j)PPC_STORE_U32(source+28*i+4*j,actualVertices[i][j]);
        PPC_STORE_U32(0x82D0CB14,0x6D);PPC_STORE_U32(rgba,0xFFFFFFFF);
        require(cpu.invoke(0x823EE940,camera,rgba,7)==1,"Im2D UI fixture original color/depth/stencil clear failed");
        const auto uiBinding=driver.cameraBinding();
        require(uiBinding.viewport[4]==0x3F800000 && uiBinding.viewport[5]==0,
            "Im2D UI fixture did not retain the original reversed camera range");
        // Reconcile the RW caches with this fixture's earlier direct queued
        // requests. Then repeat actual827F5B08/5B20's write-on/test-off pair.
        for(auto [id,value]:std::array<std::pair<uint32_t,uint32_t>,6>{{{8,0},{6,0},{8,1},{6,1},{8,1},{6,0}}})
            require(cpu.invoke(0x824025A8,id,value)==1,"Im2D UI original RW depth request failed");
        for(auto [id,value]:std::array<std::pair<uint32_t,uint32_t>,3>{{{0x38,0},{0x60,0},{0x68,7}}})cpu.invoke(0x82400170,id,value);
        const auto uiCall=cpu.registers();
        require(cpu.invoke(0x82409308,4,source,4)==1,"Original UI Im2D ALWAYS-write draw failed");
        require(cpu.registers().r1.u32==uiCall.r1.u32 && cpu.registers().lr==uiCall.lr &&
            cpu.registers().r31.u64==uiCall.r31.u64,"Original UI Im2D epilogue changed ABI state");
        require(driver.effectiveState().scalar(ScalarState::DepthEnable)==1 &&
            driver.effectiveState().scalar(ScalarState::DepthWrite)==1 &&
            driver.effectiveState().scalar(ScalarState::DepthCompare)==7,
            "Original RW write-on/test-off did not commit enable/write/ALWAYS");
        require(PPC_LOAD_U32(0x82D0E3B0)==1 && PPC_LOAD_U32(0x82D0E3B4)==0 &&
            !PPC_LOAD_U32(0x82D503D4) && !PPC_LOAD_U32(0x82D10114) && !PPC_LOAD_U32(0x82D10118),
            "Original UI Im2D lost RW cache, queue commit or screen reset");
        const auto uiColor=driver.readbackColor(binding.colorIdentity),uiDepth=driver.readbackDepth(binding.depthIdentity);
        require(uiColor.size()==1280*720*4 && uiDepth.size()==1280*720*8,"Original UI Im2D readback dimensions differ");
        for(size_t i=0;i<1280*720;++i) {
            uint32_t colorBits,depthBits;std::memcpy(&colorBits,uiColor.data()+4*i,4);std::memcpy(&depthBits,uiDepth.data()+8*i,4);
            // Original bottomY=719.61865234375, followed by VS subtraction
            // of0.5: native pixel center719.5 is outside. Preserve the exact
            // captured geometry and require the last row to retain its clear.
            const bool covered=i/1280<719;
            if(colorBits!=(covered?0xC0000000u:0xFFFFFFFFu) || depthBits!=(covered?0x3F004188u:0u) || uiDepth[8*i+4]!=0x6D) {
                std::fprintf(stderr,"Original UI pixel=%zu color=%08X depth=%08X stencil=%02X\n",i,colorBits,depthBits,uiDepth[8*i+4]);
                throw Simpsons::Failure("Original UI Im2D differs from captured black/reversed20e4 depth or changed stencil");
            }
        }
        ++checks;
        for(uint32_t i=0;i<4;++i)for(uint32_t j=0;j<7;++j)
            require(PPC_LOAD_U32(source+28*i+4*j)==actualVertices[i][j],"Original UI Im2D mutated source vertices");
        require(driver.cameraBinding().viewport==uiBinding.viewport,"Original UI Im2D changed the logical depth range");
        require(driver.im2DUploadCount()==uploads+4 && driver.im2DDrawCount()==draws+4 &&
            driver.screenDrawCount()==screens && driver.presentationCount()==presents,
            "Original UI Im2D submission counters differ");
        require(runtime.physicalAllocations.size()==physicalCount,"Original UI Im2D leaked upload/program scratch");
        // Primitive3 is an independent triangle list. Two separated rectangles
        // catch accidental strip connections and odd-triangle winding changes.
        // The real AOT path computes count/3, maps the original topology table,
        // uploads all12 vertices, and advances the original cursor by12.
        constexpr std::array<std::array<float,2>,6> listCorners={{{0,0},{0,16},{16,0},{16,16},{16,0},{0,16}}};
        std::array<std::array<uint32_t,7>,12> listWords{};
        for(uint32_t i=0;i<12;++i) {
            const bool second=i>=6;
            listWords[i]={std::bit_cast<uint32_t>((second?32.5f:8.5f)+listCorners[i%6][0]),
                std::bit_cast<uint32_t>(8.5f+listCorners[i%6][1]),second?0x3F400000u:0x3E800000u,
                0x7FA12345u,second?0xFF0000FFu:0xFF00FF00u,0x7FC12345u,0x7FC12345u};
            for(uint32_t j=0;j<7;++j)PPC_STORE_U32(source+28*i+4*j,listWords[i][j]);
        }
        const uint32_t listClear=source+512;PPC_STORE_U32(listClear,0);
        require(cpu.invoke(0x823EE940,camera,listClear,7)==1,"Original list clear failed");
        const auto listCall=cpu.registers();
        require(cpu.invoke(0x82409308,3,source,12)==1,"Original triangle list did not return success");
        require(cpu.registers().r1.u64==listCall.r1.u64 && cpu.registers().lr==listCall.lr &&
            cpu.registers().r26.u64==listCall.r26.u64 && cpu.registers().r29.u64==listCall.r29.u64,
            "Original triangle list damaged its count/topology or epilogue ABI");
        const auto listColor=driver.readbackColor(binding.colorIdentity),listDepth=driver.readbackDepth(binding.depthIdentity);
        for(size_t y=0;y<720;++y)for(size_t x=0;x<1280;++x) {
            const size_t i=y*1280+x;const bool firstRect=x>=8&&x<24&&y>=8&&y<24,secondRect=x>=32&&x<48&&y>=8&&y<24;
            uint32_t value,z;std::memcpy(&value,listColor.data()+4*i,4);std::memcpy(&z,listDepth.data()+8*i,4);
            if(value!=(firstRect?0xC00FFC00u:secondRect?0xFFF00000u:0u) ||
               z!=(firstRect?0x3F400000u:secondRect?0x3E800000u:0u) || listDepth[8*i+4]!=0x6D)
                throw Simpsons::Failure("Original triangle list coverage/order/color/reversed depth differs");
        }
        ++checks;
        const auto listSlot=PPC_LOAD_U32(0x82D0D0DC);
        require(PPC_LOAD_U32(0x82D101C8)*28==PPC_LOAD_U32(0x82D0D0E0+4*listSlot) &&
            !PPC_LOAD_U32(0x82D503D4)&&!PPC_LOAD_U32(0x82D10114)&&!PPC_LOAD_U32(0x82D10118),
            "Original list did not advance its cursor/commit state/reset override");
        for(uint32_t i=0;i<12;++i)for(uint32_t j=0;j<7;++j)
            require(PPC_LOAD_U32(source+28*i+4*j)==listWords[i][j],"Original list changed source vertices");
        require(driver.im2DUploadCount()==uploads+5 && driver.im2DDrawCount()==draws+5 &&
            driver.screenDrawCount()==screens && driver.presentationCount()==presents &&
            runtime.physicalAllocations.size()==physicalCount,"Original list counters or temporary ownership differ");

        // Replay the same geometry through the distinct original single-
        // triangle callback. Scatter the source so each call must use all
        // three original indices, rather than copy a contiguous prefix.
        for(uint32_t i=0;i<12;++i) {
            listWords[i][3]=0x3F800000;listWords[i][5]=0;listWords[i][6]=0;
            for(uint32_t j=0;j<7;++j)PPC_STORE_U32(source+28*((i*5)%12)+4*j,listWords[i][j]);
        }
        const auto triangleCall=cpu.registers();
        auto* triangleArrays=runtime.pointer(0x82D0D0DC,0x34,false);
        auto* triangleScratch=runtime.pointer(0x82D101C4,0x18,false);
        const std::vector<uint8_t> triangleArraysBefore(triangleArrays,triangleArrays+0x34);
        const std::vector<uint8_t> triangleScratchBefore(triangleScratch,triangleScratch+0x18);
        for(const auto indices:std::array<std::array<uint32_t,3>,3>{{{12,1,2},{0,12,2},{0,1,0xFFFFFFFF}}}) {
            rejects([&]{cpu.invoke(0x824090A8,source,12,indices[0],indices[1],indices[2]);});
            cpu.registers()=triangleCall;
            require(driver.im2DUploadCount()==uploads+5 && driver.im2DDrawCount()==draws+5 &&
                std::equal(triangleArraysBefore.begin(),triangleArraysBefore.end(),triangleArrays) &&
                std::equal(triangleScratchBefore.begin(),triangleScratchBefore.end(),triangleScratch),
                "Rejected original triangle mutated buffer ownership/cursors or drew");
        }
        require(cpu.invoke(0x823EE940,camera,listClear,7)==1,"Original single-triangle clear failed");
        for(uint32_t triangle=0;triangle<4;++triangle) {
            const auto call=cpu.registers();const uint32_t firstVertex=3*triangle;
            require(cpu.invoke(0x824090A8,source,12,(firstVertex*5)%12,((firstVertex+1)*5)%12,((firstVertex+2)*5)%12)==1,
                "Original single-triangle callback did not return success");
            require(cpu.registers().r1.u64==call.r1.u64 && cpu.registers().lr==call.lr &&
                cpu.registers().r26.u64==call.r26.u64 && cpu.registers().r27.u64==call.r27.u64 &&
                cpu.registers().r28.u64==call.r28.u64 && cpu.registers().r29.u64==call.r29.u64 &&
                cpu.registers().r30.u64==call.r30.u64 && cpu.registers().r31.u64==call.r31.u64,
                "Original single-triangle epilogue changed its stack/LR/nonvolatile registers");
            const uint32_t slot=PPC_LOAD_U32(0x82D0D0DC),end=PPC_LOAD_U32(0x82D101C8)*28;
            require(end>=84 && end==PPC_LOAD_U32(0x82D0D0E0+4*slot),"Original triangle cursor did not advance by three");
            const auto buffer=driver.readbackDynamicBuffer(PPC_LOAD_U32(0x82D101CC));
            for(uint32_t i=0;i<3;++i)for(uint32_t j=0;j<7;++j) {
                const auto bits=listWords[firstVertex+i][j];
                for(uint32_t k=0;k<4;++k)require(buffer[end-84+28*i+4*j+k]==uint8_t(bits>>(24-8*k)),
                    "Original indexed triangle selected/copied different vertex bytes");
            }
        }
        require(driver.readbackColor(binding.colorIdentity)==listColor && driver.readbackDepth(binding.depthIdentity)==listDepth,
            "Original single triangles differ from the independent list coverage/color/depth/stencil");
        require(!PPC_LOAD_U32(0x82D503D4) && !PPC_LOAD_U32(0x82D10114) && !PPC_LOAD_U32(0x82D10118) &&
            driver.im2DUploadCount()==uploads+9 && driver.im2DDrawCount()==draws+9 &&
            driver.screenDrawCount()==screens && driver.presentationCount()==presents &&
            runtime.physicalAllocations.size()==physicalCount,"Original single-triangle state/counters/temporary ownership differ");
        for(uint32_t i=0;i<12;++i)for(uint32_t j=0;j<7;++j)
            require(PPC_LOAD_U32(source+28*((i*5)%12)+4*j)==listWords[i][j],"Original single triangle mutated its source");
        // Original queued ZERO/ADD/ONE must commit before the single-triangle
        // draw. Preserve the two colored rectangles while replacing their
        // distinct depths with0.5; skipping these draws cannot pass this test.
        for(uint32_t i=0;i<12;++i) {
            listWords[i][2]=0x3F000000;listWords[i][4]=0x5B112233;
            for(uint32_t j=0;j<7;++j)PPC_STORE_U32(source+28*((i*5)%12)+4*j,listWords[i][j]);
        }
        for(auto [id,value]:std::array<std::pair<uint32_t,uint32_t>,7>{{{0x3C,1},{0x40,0},{0x48,0},
            {0x4C,1},{0x50,0},{0x60,0},{0x134,1}}})cpu.invoke(0x82400170,id,value);
        require(PPC_LOAD_U32(0x82D10114)>0,"Destination-preserving fixture lacks pending original blend state");
        for(uint32_t triangle=0;triangle<4;++triangle) {
            const auto call=cpu.registers();const uint32_t firstVertex=3*triangle;
            require(cpu.invoke(0x824090A8,source,12,(firstVertex*5)%12,((firstVertex+1)*5)%12,((firstVertex+2)*5)%12)==1,
                "Original destination-preserving triangle failed");
            require(cpu.registers().r1.u64==call.r1.u64 && cpu.registers().lr==call.lr &&
                cpu.registers().r26.u64==call.r26.u64 && cpu.registers().r27.u64==call.r27.u64 &&
                cpu.registers().r28.u64==call.r28.u64 && cpu.registers().r29.u64==call.r29.u64 &&
                cpu.registers().r30.u64==call.r30.u64 && cpu.registers().r31.u64==call.r31.u64,
                "Original destination-preserving triangle damaged its ABI");
            const uint32_t slot=PPC_LOAD_U32(0x82D0D0DC),end=PPC_LOAD_U32(0x82D101C8)*28;
            require(end>=84 && end==PPC_LOAD_U32(0x82D0D0E0+4*slot),"Destination-preserving triangle lost original cursor advancement");
        }
        require(driver.effectiveState().effectiveBlend(0)==0x01000100 &&
            driver.effectiveState().scalar(ScalarState::ExpandedBlend0)==1 &&
            driver.effectiveState().scalar(ScalarState::DepthEnable)==1 &&
            driver.effectiveState().scalar(ScalarState::DepthWrite)==1 &&
            driver.effectiveState().scalar(ScalarState::DepthCompare)==7,
            "Original destination-preserving blend/depth requests did not commit");
        require(driver.readbackColor(binding.colorIdentity)==listColor,"Original ZERO/ONE changed packed color or alpha");
        const auto retainedDepth=driver.readbackDepth(binding.depthIdentity);
        for(size_t y=0;y<720;++y)for(size_t x=0;x<1280;++x) {
            const size_t i=y*1280+x;const bool covered=y>=8&&y<24&&((x>=8&&x<24)||(x>=32&&x<48));
            uint32_t z;std::memcpy(&z,retainedDepth.data()+8*i,4);
            if(z!=(covered?0x3F000000u:0u)||retainedDepth[8*i+4]!=0x6D)
                throw Simpsons::Failure("Original ZERO/ONE omitted depth writes, lost coverage or altered stencil");
        }
        ++checks;
        require(!PPC_LOAD_U32(0x82D503D4) && !PPC_LOAD_U32(0x82D10114) && !PPC_LOAD_U32(0x82D10118) &&
            driver.im2DUploadCount()==uploads+13 && driver.im2DDrawCount()==draws+13 &&
            driver.screenDrawCount()==screens && driver.presentationCount()==presents &&
            runtime.physicalAllocations.size()==physicalCount,"Original ZERO/ONE state/counters/temporary ownership differ");
        for(uint32_t i=0;i<12;++i)for(uint32_t j=0;j<7;++j)
            require(PPC_LOAD_U32(source+28*((i*5)%12)+4*j)==listWords[i][j],"Original ZERO/ONE mutated source vertices");
    } catch(...) {restore();throw;}
    restore();require(cpu.invoke(0x823F1A08,camera)==camera,"Im2D draw camera end failed");
    std::fprintf(stderr,"PASS original Im2D draw:%zu checks; original setup/commit/epilogue, disabled-depth preservation, actual154 UI reversed20e4=3F004188 and primitive3 list coverage/depth/cursor; fixture replay\n",checks-first);
}
