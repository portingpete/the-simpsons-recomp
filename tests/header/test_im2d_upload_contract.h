#pragma once
// Production AOT prefix and allocator, isolated by an intentionally unsupported
// explicit-shader selection. The production setup rejects it before mutation.
// Arbitrary byte patterns here are buffer tests, not valid rendered vertices.
struct Im2DUploadObservation {
    uint8_t* base;uint32_t shader;
    explicit Im2DUploadObservation(uint8_t* memory):base(memory),shader(PPC_LOAD_U32(0x82CD1A6C)) {
        PPC_STORE_U32(0x82CD1A6C,0xDEADBEEF);
    }
    ~Im2DUploadObservation(){PPC_STORE_U32(0x82CD1A6C,shader);}
};
void im2dUploadContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    const auto first=checks;auto& driver=*runtime.engineDriver;
    const uint32_t camera=PPC_LOAD_U32(0x82E07248);
    require(cpu.invoke(0x823F1A18,camera)==camera,"Im2D fixture failed original camera begin");
    const PPCContext incoming=cpu.registers();
    Im2DUploadObservation observation(base);
    const uint32_t source=runtime.allocatePhysical(0,0x40000,PAGE_READWRITE,0,UINT32_MAX,4096);
    require(source!=0,"Im2D fixture source allocation failed");
    auto* sourceBytes=runtime.pointer(source,0x40000,true);
    for(uint32_t i=0;i<0x40000;++i)sourceBytes[i]=uint8_t((i*37+(i>>8)*19)^0xAB);
    const auto physicalCount=runtime.physicalAllocations.size();
    auto* arrays=runtime.pointer(0x82D0D0DC,0x34,true);
    auto* scratch=runtime.pointer(0x82D101C4,0x18,true);
    const std::vector<uint8_t> savedArrays(arrays,arrays+0x34),savedScratch(scratch,scratch+0x18);
    auto* states=runtime.pointer(0x82D0D170,0x2FBC,false);
    const std::vector<uint8_t> savedStates(states,states+0x2FBC);
    const auto nativeState=driver.effectiveState();
    const auto draws=driver.screenDrawCount(),presents=driver.presentationCount();
    const auto beforeColor=driver.readbackColor(driver.cameraBinding().colorIdentity);
    const auto beforeDepth=driver.readbackDepth(driver.cameraBinding().depthIdentity);
    const std::array<uint32_t,4> ids={PPC_LOAD_U32(0x82D0D100),PPC_LOAD_U32(0x82D0D104),PPC_LOAD_U32(0x82D0D108),PPC_LOAD_U32(0x82D0D10C)};
    std::array<std::vector<uint8_t>,4> expected;
    for(uint32_t i=0;i<4;++i)expected[i]=driver.readbackDynamicBuffer(ids[i]);
    auto reset=[&](uint32_t current,std::array<uint32_t,4> cursors) {
        cpu.registers()=incoming;std::memcpy(arrays,savedArrays.data(),savedArrays.size());
        std::memcpy(scratch,savedScratch.data(),savedScratch.size());PPC_STORE_U32(0x82D0D0DC,current);
        for(uint32_t i=0;i<4;++i)PPC_STORE_U32(0x82D0D0E0+4*i,cursors[i]);
    };
    auto exercise=[&](uint32_t current,std::array<uint32_t,4> cursors,uint32_t count,uint32_t selected,uint32_t offset) {
        reset(current,cursors);const auto uploads=driver.im2DUploadCount();bool reached=false;
        try {cpu.invoke(0x82409308,4,source,count);}catch(const Simpsons::Failure& e){
            if(std::string(e.what()).find("Original Im2D setup has unqualified explicit shader bindings")==std::string::npos)throw;
            reached=true;
        }
        require(reached,"Original Im2D prefix did not reach the fixed-function observation");
        require(cpu.registers().r1.u32==incoming.r1.u32-0xA0 && cpu.registers().lr==0x8240950C,
            "Im2D prefix did not preserve its original function frame/call ABI");
        cpu.registers()=incoming;
        require(driver.im2DUploadCount()==uploads+1,"Original Im2D prefix omitted/duplicated native upload");
        require(PPC_LOAD_U32(0x82D0D0DC)==selected && PPC_LOAD_U32(0x82D101CC)==ids[selected] &&
            PPC_LOAD_U32(0x82D101C8)==offset/28,"Original allocator lost current/output/start-vertex publications");
        for(uint32_t i=0;i<4;++i)require(PPC_LOAD_U32(0x82D0D0E0+4*i)==(i==selected?offset+count*28:cursors[i]),
            "Original dynamic allocator modified the wrong cursor/range");
        std::copy_n(sourceBytes,count*28,expected[selected].begin()+offset);
        require(driver.readbackDynamicBuffer(ids[selected])==expected[selected],
            "Native buffer bytes differ from original memcpy or overwrite neighboring ranges");
        require(runtime.physicalAllocations.size()==physicalCount,"Native vertex lock leaked staging after upload");
        require(std::memcmp(states,savedStates.data(),savedStates.size())==0,"Im2D upload changed deferred/applied CPU state");
    };
    try {
        // Every modulo28 alignment residue, all four native buffers.
        for(uint32_t slot=0;slot<4;++slot)for(uint32_t residue=0;residue<28;++residue) {
            std::array<uint32_t,4> cursors={0x40000,0x40000,0x40000,0x40000};cursors[slot]=28+residue;
            exercise(slot,cursors,4,slot,residue?56:28);
        }
        for(uint32_t current=0;current<4;++current) {
            // Exhaustion discards the next ring buffer; a fitting other slot
            // is selected in the original index0..3 search order.
            exercise(current,{0x40000,0x40000,0x40000,0x40000},4,(current+1)%4,0);
            auto cursors=std::array<uint32_t,4>{0x40000,0x40000,0x40000,0x40000};
            const uint32_t available=(current+2)%4;cursors[available]=1;
            exercise(current,cursors,4,available,28);
        }
        exercise(0,{0,0,0,0},9362,0,0);
        exercise(0,{0x40000,0,0,0},9362,1,0);
        exercise(0,{262024,0,0,0},4,0,262024);
        for(uint32_t i=0;i<4;++i)require(driver.readbackDynamicBuffer(ids[i])==expected[i],"Im2D upload modified another native buffer");
        // Invalid inputs reject before original CPU writes or a native upload.
        auto bad=[&](uint32_t primitive,uint32_t address,uint32_t count) {
            const std::vector<uint8_t> a(arrays,arrays+0x34),b(scratch,scratch+0x18);
            const auto uploads=driver.im2DUploadCount();
            rejects([&]{cpu.invoke(0x82409308,primitive,address,count);});cpu.registers()=incoming;
            require(driver.im2DUploadCount()==uploads && std::equal(a.begin(),a.end(),arrays) &&
                std::equal(b.begin(),b.end(),scratch),"Rejected Im2D prefix mutated buffers or original cursor state");
        };
        reset(0,{0,0,0,0});
        for(uint32_t count:{0u,1u,2u,9363u,0xFFFFFFFFu})bad(4,source,count);
        for(uint32_t primitive:{0u,1u,2u,3u,5u,0xFFFFFFFFu})bad(primitive,source,4);
        bad(4,0xFFFFFFF0,4);bad(4,0,4);
        PPC_STORE_U32(0x82D0D0DC,4);bad(4,source,4);PPC_STORE_U32(0x82D0D0DC,0);
        PPC_STORE_U32(0x82D0D0E0,0x40001);bad(4,source,4);PPC_STORE_U32(0x82D0D0E0,0);
        PPC_STORE_U32(0x82D0D0F0,0x40004);bad(4,source,4);PPC_STORE_U32(0x82D0D0F0,0x40000);
        PPC_STORE_U32(0x82D101C4,1);bad(4,source,4);PPC_STORE_U32(0x82D101C4,0);
        PPC_STORE_U32(0x82D0D100,ids[1]);bad(4,source,4);PPC_STORE_U32(0x82D0D100,ids[0]);
        const auto head=PPC_LOAD_U32(0x82D0D0D4),recordOutput=PPC_LOAD_U32(head+12);
        PPC_STORE_U32(head+12,recordOutput+4);bad(4,source,4);PPC_STORE_U32(head+12,recordOutput);
        require(driver.screenDrawCount()==draws && driver.presentationCount()==presents &&
            driver.readbackColor(driver.cameraBinding().colorIdentity)==beforeColor &&
            driver.readbackDepth(driver.cameraBinding().depthIdentity)==beforeDepth,
            "Native vertex upload falsely drew, presented or changed target/depth pixels");
        for(const auto& field:Simpsons::Graphics::scalarStateEvidence())
            require(driver.effectiveState().scalar(field.id)==nativeState.scalar(field.id),"Im2D upload changed native scalar state");
    } catch(...) {
        cpu.registers()=incoming;runtime.freePhysical(source);throw;
    }
    cpu.registers()=incoming;
    std::memcpy(arrays,savedArrays.data(),savedArrays.size());std::memcpy(scratch,savedScratch.data(),savedScratch.size());
    runtime.freePhysical(source);
    require(cpu.invoke(0x823F1A08,camera)==camera,"Im2D fixture failed original camera end");
    std::fprintf(stderr,"PASS original Im2D upload:%zu checks; original AOT buffer selection/alignment/copy, exact GPU bytes, staging release; no draw success\n",checks-first);
}
