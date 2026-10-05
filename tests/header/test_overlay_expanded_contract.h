#pragma once
// Included INSIDE test_engine_driver.cpp's anonymous namespace, after its
// require/rejects helpers. Declare both hooks at GLOBAL scope before that
// namespace: extern void SimpsonsNativeOverlayExpandedEnter(PPCContext&,uint8_t*);
//            extern void SimpsonsNativeOverlayExpandedExit(PPCContext&,uint8_t*);
// Uses the main test's runtime/driver/CPU/state/backend and standard includes.
void overlayExpandedContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    using namespace Simpsons;
    using namespace Simpsons::Graphics;
    const auto first=checks;
    auto& driver=*runtime.engineDriver;
    const uint32_t camera=PPC_LOAD_U32(0x82E07248);
    require(cpu.invoke(0x823F1A18,camera)==camera,"Overlay expansion camera begin failed");
    auto& ctx=cpu.registers();
    PPCContext incoming;std::memcpy(&incoming,&ctx,sizeof(ctx));
    const auto initialMode=driver.effectiveState().scalar(ScalarState::ExpandedBlend0);
    const auto binding=driver.cameraBinding();
    bool sampledAlphaOne{};
    const auto target=driver.color(binding.colorIdentity,sampledAlphaOne);
    const auto depth=driver.depth(binding.depthIdentity);
    require(target && target->format==TargetFormat::RGB10A2 && depth,
            "Overlay expansion fixture needs actual owned RGB10A2/depth targets");
    const auto colorBytes=driver.readbackColor(binding.colorIdentity);
    const auto depthBytes=driver.readbackDepth(binding.depthIdentity);

    struct Bytes {
        uint8_t* p;std::vector<uint8_t> saved;
        Bytes(uint8_t* address,uint32_t n):p(address),saved(address,address+n) {}
        ~Bytes() {std::memcpy(p,saved.data(),saved.size());}
    };
    // No hook may synchronize caches, consume a queue, change a stream/shader,
    // or touch the original frame. Snapshots also restore test fault injection.
    std::vector<Bytes> bytes;
    bytes.reserve(8);
    for(auto [address,n]:std::array<std::pair<uint32_t,uint32_t>,7>{{
        {0x82D0D170,0x2FBC},{0x82E3D160,0xB24},{0x82CD1A64,0x14},
        {0x82D0CAB0,0xB0},{0x82D5DB78,0x41D4},{0x82D0CF58,8},{camera,0x100}}})
        bytes.emplace_back(runtime.pointer(address,n,true),n);
    bytes.emplace_back(runtime.pointer(ctx.r1.u32,0xD0,true),0xD0);
    auto checkBytes=[&] {
        for(const auto& b:bytes)
            require(!std::memcmp(b.p,b.saved.data(),b.saved.size()),"Overlay expansion changed original memory/cache/frame bytes");
    };
    auto activity=[&] {
        return std::array<uint64_t,15>{runtime.allocations.size(),runtime.physicalAllocations.size(),
            runtime.regions.size(),runtime.graphicsStorage.size(),runtime.nextAllocation,driver.rasterCount(),
            driver.cameraClearCount(),driver.cameraCopyCount(),driver.screenDrawCount(),driver.im2DDrawCount(),
            driver.im2DUploadCount(),driver.presentationCount(),driver.presentationAttemptCount(),
            driver.frontCopyCount(),driver.bindingResetCount()};
    };
    const auto originalActivity=activity();
    auto checkState=[&](const EngineState& before,uint32_t expectedMode) {
        const auto& after=driver.effectiveState();
        for(const auto& f:scalarStateEvidence())
            require(after.scalar(f.id)==(f.id==0x134?expectedMode:before.scalar(f.id)),
                    "Overlay expansion changed the wrong effective scalar");
        for(uint32_t stage=0;stage<16;++stage)for(const auto& f:samplerStateEvidence())
            require(after.sampler(stage,f.id)==before.sampler(stage,f.id),"Overlay expansion changed a sampler");
        for(uint32_t i=0;i<4;++i)
            require(after.effectiveBlend(i)==before.effectiveBlend(i),"Overlay expansion changed a blend equation");
        const auto now=driver.cameraBinding();
        require(now.camera==binding.camera && now.colorRaster==binding.colorRaster && now.depthRaster==binding.depthRaster &&
                now.colorIdentity==binding.colorIdentity && now.depthIdentity==binding.depthIdentity && now.viewport==binding.viewport,
                "Overlay expansion changed camera/attachments/logical viewport");
        bool alpha{};
        require(driver.color(binding.colorIdentity,alpha)==target && alpha==sampledAlphaOne &&
                driver.depth(binding.depthIdentity)==depth,"Overlay expansion replaced native target ownership");
        require(activity()==originalActivity,"Overlay expansion allocated guest storage/resources or submitted native work");
        checkBytes();
    };
    auto prepare=[&](uint32_t request) {
        std::memcpy(&ctx,&incoming,sizeof(ctx));
        ctx.r3.u64=0;ctx.r4.u64=request;
        ctx.r28.u64=ctx.r31.u64=0xFFFFFFFF82D10000ull;ctx.r30.u64=camera;
        ctx.lr=request?0x827F5AE4:0x827F5C00; // Previous original call's LR.
    };
    auto call=[&](bool enter,PPCContext& c,uint8_t* memory) {
        if(enter)::SimpsonsNativeOverlayExpandedEnter(c,memory);
        else ::SimpsonsNativeOverlayExpandedExit(c,memory);
    };
    auto good=[&](uint32_t request) {
        prepare(request);const auto state=driver.effectiveState();
        PPCContext expected;std::memcpy(&expected,&ctx,sizeof(ctx));
        expected.lr=request?0x827F5AF4:0x827F5C0C;
        const auto fp=PPCFPSCRRegister::getcsr();const auto error=GetLastError();
        call(request!=0,ctx,base);
        require(PPCFPSCRRegister::getcsr()==fp && GetLastError()==error,"Overlay expansion changed host FP/error state");
        require(!std::memcmp(&ctx,&expected,sizeof(ctx)) && currentContext==&ctx,
                "Overlay expansion changed input/nonvolatile/FP/vector registers or the BL continuation");
        checkState(state,request);
        require(driver.readbackColor(binding.colorIdentity)==colorBytes && driver.readbackDepth(binding.depthIdentity)==depthBytes,
                "Overlay expansion changed real GPU packed color/depth storage");
    };
    // Faults are injected after a valid ABI is prepared. Compare CPU registers
    // immediately; restore only the fixture's injected word before owner queries.
    auto bad=[&](bool enter,PPCContext& c,uint8_t* memory) {
        PPCContext before;std::memcpy(&before,&c,sizeof(c));
        const auto fp=PPCFPSCRRegister::getcsr();const auto error=GetLastError();
        rejects([&]{call(enter,c,memory);});
        require(!std::memcmp(&before,&c,sizeof(c)),"Rejected overlay expansion changed CPU registers/LR");
        require(PPCFPSCRRegister::getcsr()==fp && GetLastError()==error,"Rejected overlay expansion changed host FP/error state");
    };
    auto badWord=[&](uint32_t address,uint32_t value,bool enter) {
        prepare(enter?1:0);const auto state=driver.effectiveState();
        {
            Bytes word(runtime.pointer(address,4,true),4);PPC_STORE_U32(address,value);
            bad(enter,ctx,base);
        }
        checkState(state,state.scalar(ScalarState::ExpandedBlend0));
    };
    auto restore=[&] {
        std::memcpy(&ctx,&incoming,sizeof(ctx));
        driver.directScalar(base,0x134,initialMode);
    };
    try {
        // Independently pinned original words, including the parent's camera
        // register and frame, argument loads and both exact BL displacements.
        for(auto [address,word]:std::array<std::pair<uint32_t,uint32_t>,10>{{
            {0x827F58D8,0x7D8802A6},{0x827F58E0,0x9421FF30},{0x827F58E4,0x7C9E2378},
            {0x827F5AE4,0x3F8082D1},{0x827F5AE8,0x38800001},{0x827F5AEC,0x807CCAF8},
            {0x827F5AF0,0x4BC458C1},{0x827F5C00,0x38800000},{0x827F5C04,0x807CCAF8},{0x827F5C08,0x4BC457A9}}})
            require(PPC_LOAD_U32(address)==word,"Overlay expansion original instruction pin changed");
        require(PPC_LOAD_U32(0x82CD28B8+3*0x134+4)==0x8243B3B0,"Overlay expansion original scalar setter changed");
        // All old/request pairs, including repeated same-value calls. Exit is
        // a literal zero write, never a restore of an earlier expansion value.
        for(uint32_t old:{0u,1u})for(uint32_t request:{0u,1u}) {
            driver.directScalar(base,0x134,old);good(request);good(request);
        }
        for(bool enter:{false,true}) {
            const auto state=driver.effectiveState();
            for(uint64_t value:{uint64_t(enter?0:1),uint64_t(2),uint64_t(0xFFFFFFFF),
                                uint64_t(0x100000000ull|(enter?1:0))}) {
                prepare(enter?1:0);ctx.r4.u64=value;bad(enter,ctx,base);
                checkState(state,state.scalar(ScalarState::ExpandedBlend0));
            }
            for(uint64_t value:{uint64_t(1),uint64_t(0x100000000ull)}) {
                prepare(enter?1:0);ctx.r3.u64=value;bad(enter,ctx,base);
                checkState(state,state.scalar(ScalarState::ExpandedBlend0));
            }
            for(uint32_t which=0;which<7;++which) {
                prepare(enter?1:0);
                if(which==0)ctx.r28.u32=0;
                if(which==1)ctx.r31.u32=0;
                if(which==2)ctx.r30.u32=0;
                if(which==3)ctx.r30.u32=camera+4;
                if(which==4)ctx.r1.u32|=1;
                if(which==5)ctx.r1.u32=0;
                if(which==6)ctx.r1.u32=0xFFFFF000;
                bad(enter,ctx,base);checkState(state,state.scalar(ScalarState::ExpandedBlend0));
            }
            for(auto* memory:{static_cast<uint8_t*>(nullptr),base+1}) {
                prepare(enter?1:0);bad(enter,ctx,memory);checkState(state,state.scalar(ScalarState::ExpandedBlend0));
            }
            prepare(enter?1:0);
            PPCContext other;std::memcpy(&other,&ctx,sizeof(ctx));bad(enter,other,base);
            checkState(state,state.scalar(ScalarState::ExpandedBlend0));
            {
                auto* previous=currentContext;currentContext=nullptr;
                try {bad(enter,ctx,base);} catch(...) {currentContext=previous;throw;}
                currentContext=previous;
            }
            checkState(state,state.scalar(ScalarState::ExpandedBlend0));
            for(auto [address,value]:std::array<std::pair<uint32_t,uint32_t>,11>{{
                {0x82D0CAF8,1},{0x82D0CA68,0},{0x82D5DA74,0},{0x82D6D890,0},
                {0x82D5DA74,0xDEADBEEF},{0x82D6D890,0xDEADBEEF},{0x82E3DD60,0},
                {PPC_LOAD_U32(0x82D0CA68),0},{0x82D0CB1C,0},{0x82D0CF5C,0},{0x82D0CF58,0}}})
                badWord(address,value,enter);
            badWord(0x82D0CB1C,2,enter);
            badWord(camera+0x60,0,enter); // Original camera's color raster association.
            badWord(0x82CD28B8+3*0x134+4,0,enter); // Last directScalar precommit gate.
            {
                const auto owned=runtime.engineDriver;
                runtime.engineDriver.reset();
                try {prepare(enter?1:0);bad(enter,ctx,base);} catch(...) {runtime.engineDriver=owned;throw;}
                runtime.engineDriver=owned;
            }
            checkState(state,state.scalar(ScalarState::ExpandedBlend0));
            {
                auto* previous=active;active=nullptr;
                try {prepare(enter?1:0);bad(enter,ctx,base);} catch(...) {active=previous;throw;}
                active=previous;
            }
            checkState(state,state.scalar(ScalarState::ExpandedBlend0));
            {
                const auto format=target->format;target->format=TargetFormat::RGBA8;
                try {prepare(enter?1:0);bad(enter,ctx,base);} catch(...) {target->format=format;throw;}
                target->format=format;
            }
            checkState(state,state.scalar(ScalarState::ExpandedBlend0));
            // Supply correct thread-local CPU identity so rejection must also
            // enforce the native driver's actual thread owner.
            prepare(enter?1:0);PPCContext foreign;std::memcpy(&foreign,&ctx,sizeof(ctx));
            PPCContext expected;std::memcpy(&expected,&foreign,sizeof(foreign));
            bool rejected=false;
            std::thread worker([&] {
                auto* previous=currentContext;currentContext=&foreign;
                try {call(enter,foreign,base);} catch(const std::exception&) {rejected=true;}
                currentContext=previous;
            });
            worker.join();
            require(rejected && !std::memcmp(&foreign,&expected,sizeof(foreign)),"Overlay expansion accepted a foreign native thread or mutated its ABI");
            checkState(state,state.scalar(ScalarState::ExpandedBlend0));
        }
        require(driver.readbackColor(binding.colorIdentity)==colorBytes && driver.readbackDepth(binding.depthIdentity)==depthBytes,
                "Rejected overlay expansion changed real GPU storage");
    } catch(...) {restore();throw;}
    restore();
    // Restore snapshots while the camera is still active. In particular CB1C
    // must not be restored to one after the original camera-end call below.
    bytes.clear();
    require(cpu.invoke(0x823F1A08,camera)==camera,"Overlay expansion camera end failed");
    std::fprintf(stderr,"PASS overlay expansion:%zu checks; exact callback ABI, effective field only, real owned storage preserved; console blend precision unverified\n",checks-first);
}
