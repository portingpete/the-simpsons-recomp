#pragma once
#include "runtime/engine_viewport_surfaces.h"
#include <cmath>
// Genuine original screen calls between a completed FX draw and its original
// manager retirement. Native-only receipt corruption is tested separately.
namespace {
template<class DrawAgain>
void screenReplacementRegression(Runtime& rt,EngineCpuCalls& cpu,uint32_t camera,uint32_t typed,
    uint32_t wrapper,uint32_t scratch,DrawAgain drawAgain) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();
    const auto id=PPC_LOAD_U32(typed+0x1C),manager=PPC_LOAD_U32(wrapper+0xC);
    const auto binding=driver.cameraBinding();
    const auto color=[&](std::array<float,4> rgba){for(uint32_t i=0;i<4;++i)PPC_STORE_U32(scratch+4*i,std::bit_cast<uint32_t>(rgba[i]));};
    const auto finish=[&](bool resetExpanded=false) {
        const auto selected=PPC_LOAD_U32(manager+0xC),cache=PPC_LOAD_U32(wrapper+0x2C);
        need(PPC_LOAD_U32(manager+4)==wrapper&&PPC_LOAD_U32(manager+8)==id&&selected&&cache,
             "Screen replacement lost the original logical FX/cache");
        const auto saved=cpu.registers();const auto old=PPC_LOAD_U32(0x82CD1A70);const auto count=driver.screenDrawCount();
        const auto pixels=driver.readbackColor(binding.colorIdentity);
        // Retail 826B5FD8..5FEC returns without a bind for the same manager
        // wrapper/technique. It still cannot authorize a scene geometry draw.
        const auto declaration=PPC_LOAD_U32(0x82CD1A68),vertex=PPC_LOAD_U32(0x82CD1A6C);
        const auto before=abi(cpu.registers());cpu.invoke(0x826B6078,wrapper,selected);
        need(abi(cpu.registers())==before&&PPC_LOAD_U32(manager+4)==wrapper&&
             PPC_LOAD_U32(manager+0xC)==selected&&PPC_LOAD_U32(wrapper+0x2C)==cache&&
             PPC_LOAD_U32(0x82CD1A68)==declaration&&PPC_LOAD_U32(0x82CD1A6C)==vertex&&
             PPC_LOAD_U32(0x82CD1A70)==old&&driver.screenDrawCount()==count&&
             driver.readbackColor(binding.colorIdentity)==pixels,"Same-selection original no-op changed screen/cache state");
        bool drawRejected=false;try {
            if(PPC_LOAD_U32(typed)==0x82061714)effects.requireSkinSelection(typed);
            else effects.requireRigidSelection(typed);
        }catch(const Graphics::Error&){drawRejected=true;}
        need(drawRejected,"Same-selection manager no-op incorrectly authorized scene shaders");
        PPC_STORE_U32(0x82CD1A70,old^1);
        bool rejected=false;try{cpu.invoke(0x826B4B18,wrapper);}catch(const Failure& error){
            rejected=std::strstr(error.what(),"screen replacement shader/declaration cache changed")!=nullptr;
        }
        PPC_STORE_U32(0x82CD1A70,old);cpu.registers()=saved;
        need(rejected&&PPC_LOAD_U32(manager+4)==wrapper&&PPC_LOAD_U32(manager+0xC)==selected&&
             PPC_LOAD_U32(wrapper+0x2C)==cache&&driver.screenDrawCount()==count&&
             driver.readbackColor(binding.colorIdentity)==pixels,"Corrupted screen cache retired an FX or changed output");
        cpu.invoke(0x826B4B18,wrapper);
        need(!PPC_LOAD_U32(manager+4)&&!PPC_LOAD_U32(manager+0xC)&&!PPC_LOAD_U32(wrapper+0x2C),
              "Original manager did not retire a completed screen replacement");
        if(resetExpanded) {
            // Retail's unready branch skips the B250 expanded-blend reset,
            // and B898 deliberately retains that scalar. Preserve it through
            // the original no-op/end proofs above, then establish this bounded
            // fixture's next scene baseline via the verified 8243B3B0 setter.
            need(driver.effectiveState().scalar(Graphics::ScalarState::ExpandedBlend0)==1,
                 "Original unready sprite expanded state did not survive manager retirement");
            driver.directScalar(base,uint32_t(Graphics::ScalarState::ExpandedBlend0),0);
            need(!driver.effectiveState().scalar(Graphics::ScalarState::ExpandedBlend0),
                 "Original scene fixture expanded baseline differs");
        }
        drawAgain(); // Exact original scene dispatcher/FX begin and real draw.
    };
    const auto check=[&](uint64_t before,bool textured,uint32_t expected) {
        need(driver.screenDrawCount()==before+1,"Original replacement did not submit exactly one real screen draw");
        need(PPC_LOAD_U32(0x82CD1A68)==PPC_LOAD_U32(textured?0x82DFEB34:0x82DFEB30)&&
             PPC_LOAD_U32(0x82CD1A6C)==PPC_LOAD_U32(textured?0x82CF2340:0x82CF231C)&&
             PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(textured?0x82CF2334:0x82CF2310),
             "Original screen replacement cache publication differs");
        need(effects.privateModifiedMask(id)==std::array<uint8_t,128>{},"Screen replacement dirtied original material values");
        const auto pixels=driver.readbackColor(binding.colorIdentity);
        if(!textured)for(size_t at=0;at<pixels.size();at+=4){uint32_t word{};std::memcpy(&word,pixels.data()+at,4);
            need(word==expected,"Original replacement screen coverage/blending differs");}
    };
    stage="original FX to gameplay overlay to manager retirement";
    color({0,0,0,1});auto before=driver.screenDrawCount();const auto depth=driver.readbackDepth(binding.depthIdentity);
    cpu.registers().r4.u32=scratch;cpu.registers().r5.u32=0;const auto saved=abi(cpu.registers());
    cpu.invoke(0x82755FD0,camera);need(abi(cpu.registers())==saved,"Original replacement overlay damaged nonvolatile ABI");
    check(before,false,0xC0000000);need(driver.readbackDepth(binding.depthIdentity)==depth,"Overlay replacement changed depth/stencil");
    // Retail batch cleanup can follow the flat overlay as well as a sprite.
    // It clears only the declaration/input layout and submits no geometry.
    const auto flatVertex=PPC_LOAD_U32(0x82CD1A6C),flatPixel=PPC_LOAD_U32(0x82CD1A70);
    const auto flatPixels=driver.readbackColor(binding.colorIdentity),flatDepth=driver.readbackDepth(binding.depthIdentity);
    const auto flatDraws=driver.screenDrawCount();const auto flatBatchAbi=abi(cpu.registers());
    cpu.invoke(0x8276B898);
    need(abi(cpu.registers())==flatBatchAbi&&driver.screenDrawCount()==flatDraws&&!PPC_LOAD_U32(0x82CD1A68)&&
         PPC_LOAD_U32(0x82CD1A6C)==flatVertex&&PPC_LOAD_U32(0x82CD1A70)==flatPixel&&
         driver.readbackColor(binding.colorIdentity)==flatPixels&&driver.readbackDepth(binding.depthIdentity)==flatDepth,
         "Original flat-overlay batch retirement changed shaders or submitted geometry");
    // The actual type-zero producer obtains r5 from its input byte, rather
    // than the unrelated 8276E344 call site (which always passes zero).
    for(const auto [address,word]:std::array<std::array<uint32_t,2>,5>{{
        {0x8276DFAC,0x897D0000},{0x8276DFB8,0x7D6B0034},{0x8276DFBC,0x556BDFFE},
        {0x8276DFC0,0x69650001},{0x8276DFC4,0x4BFE800D}}})
        need(PPC_LOAD_U32(address)==word,"Original modulated producer instructions differ");
    // Plain authored CPU records drive original DA58/74938 color/time math.
    // Every texture/declaration/query manager comes from original constructors.
    const auto records=rt.allocatePhysical(0,4096,PAGE_READWRITE,0,UINT32_MAX,4096);
    need(records!=0,"Modulated producer fixture allocation failed");
    std::memset(rt.pointer(records,4096,true),0,4096);
    const auto channel=records+0x100,definition=records+0x200,request=records+0x300,
        flag=records+0x400,queryEntry=records+0x420;
    const auto number=[&](uint32_t address,float value){PPC_STORE_U32(address,std::bit_cast<uint32_t>(value));};
    PPC_STORE_U32(records+20,channel);PPC_STORE_U32(records+180,request);
    number(records+188,1);number(records+192,1);PPC_STORE_U32(channel+8,definition);
    number(channel+124,1);number(definition+68,1);number(request+28,1);PPC_STORE_U8(flag,1);
    if(!PPC_LOAD_U32(0x82DFF28C))cpu.invoke(0x8276BEA8);
    const auto producer=PPC_LOAD_U32(0x82DFF28C);need(producer!=0,"Original corona manager is absent");
    const auto oldQuery=PPC_LOAD_U32(producer+0x48);
    PPC_STORE_U8(queryEntry+0x18,21);PPC_STORE_U8(queryEntry+0x19,5);PPC_STORE_U8(queryEntry+0x1A,1);
    PPC_STORE_U32(producer+0x48,queryEntry);
    const auto query=driver.viewportSurfaces().readbackColorTexture(0x82DFE8DC);
    need(query.size()==64*8*4,"Original corona texture extent differs");uint32_t cell{};
    std::memcpy(&cell,query.data()+4*(5*64+21),4);const float visibility=float(cell&1023)/1023.0f;
    stage="original FX flat to repeated genuine modulated producer to retirement";
    for(unsigned iteration=0;iteration<2;++iteration) {
        const auto pixels=driver.readbackColor(binding.colorIdentity),oldDepth=driver.readbackDepth(binding.depthIdentity);
        const auto screens=driver.screenDrawCount(),post=driver.screenEffectDrawCount(6);
        auto& c=cpu.registers();c.r4.u32=camera;c.r5.u32=flag;const auto old=abi(c);
        cpu.invoke(0x8276DF38,records);
        need(abi(c)==old,"Original modulated producer damaged nonvolatile ABI");
        need(driver.screenDrawCount()==screens&&driver.screenEffectDrawCount(6)==post+1,
             "Genuine modulated producer did not complete exactly one postfilter draw");
        need(PPC_LOAD_U32(0x82CD1A68)==PPC_LOAD_U32(0x82DFEB30)&&
             PPC_LOAD_U32(0x82CD1A6C)==PPC_LOAD_U32(0x82CF231C)&&
             PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(0x82CF2304),"Genuine modulated producer cache publication differs");
        need(effects.privateModifiedMask(id)==std::array<uint8_t,128>{},"Modulated producer dirtied original material values");
        const auto result=driver.readbackColor(binding.colorIdentity);need(result.size()==pixels.size(),"Modulated readback extent differs");
        for(size_t at=0;at<result.size();at+=4) {
            uint32_t destination{},got{};std::memcpy(&destination,pixels.data()+at,4);std::memcpy(&got,result.data()+at,4);
            for(uint32_t lane=0;lane<4;++lane) {
                const auto mask=lane==3?3u:1023u;const float prior=float(destination>>(10*lane)&mask)/float(mask);
                const float expected=lane==3?visibility:(lane==0?visibility*visibility:0)+prior*(1-visibility);
                const auto packed=uint32_t(std::nearbyint(std::clamp(expected,0.0f,1.0f)*float(mask)));
                need(std::abs(int(got>>(10*lane)&mask)-int(packed))<=int(lane==3?0:1),"Genuine modulated producer pixels differ");
            }
        }
        need(driver.readbackDepth(binding.depthIdentity)==oldDepth&&
             driver.viewportSurfaces().readbackColorTexture(0x82DFE8DC)==query,"Modulated producer changed depth or its query source");
    }
    PPC_STORE_U32(producer+0x48,oldQuery);finish();
    const auto screen=[&](uint32_t texture) {
        color({1,0,0,1});auto& c=cpu.registers();c.r8.u32=scratch;c.r9.u32=3;c.r10.u32=texture;
        c.f1.f64=0;c.f2.f64=0;c.f3.f64=1280;c.f4.f64=720;c.f5.f64=c.f6.f64=0;c.f7.f64=c.f8.f64=1;
        const auto old=abi(c);const auto count=driver.screenDrawCount();cpu.invoke(0x82756480,camera);
        need(abi(c)==old,"Original replacement screen damaged nonvolatile ABI");check(count,texture!=0,0xC00003FF);finish();
    };
    stage="original FX to flat screen to manager retirement";screen(0);
    // This is the original startup artwork texture, when the chosen genuine
    // graphics startup boundary has published it. No raster owner is invented.
    if(const auto texture=PPC_LOAD_U32(0x82E071E8)) {
        stage="original FX to stock textured screen to manager retirement";screen(texture);
        stage="retained original FX and screen through complete distortion phase and retirement";
        // These plain effect parameters feed the same original producer as
        // the live Tree Hugger ability. Texture, declarations, five shader
        // owners, resolve targets and the selected FX all come from originals.
        const auto ball=records+0x500,definition=records+0x600,system=records+0x700,timeline=records+0x800;
        std::memset(rt.pointer(ball,0x400,true),0,0x400);
        std::array<uint8_t,128> matrices{};
        std::memcpy(matrices.data(),rt.pointer(0x82DFEA60,128,false),128);
        const auto number=[&](uint32_t at,float value){PPC_STORE_U32(at,std::bit_cast<uint32_t>(value));};
        for(unsigned i=0;i<32;++i)number(0x82DFEA60+4*i,(i%16)%5==0?1.0f:0.0f);
        PPC_STORE_U32(ball+20,system);PPC_STORE_U32(system+8,timeline);
        number(system+124,1);number(timeline+68,1);number(timeline+72,0);
        number(ball+152,.5f);number(ball+156,1);PPC_STORE_U32(ball+164,definition);
        number(ball+168,.75f);number(ball+172,.5f);number(ball+176,.4f);number(ball+180,1);
        PPC_STORE_U32(ball+184,texture);PPC_STORE_U8(definition+17,1);PPC_STORE_U32(definition+20,2);
        number(definition+76,1);number(definition+80,.5f);PPC_STORE_U16(definition+84,2);PPC_STORE_U16(definition+86,2);
        number(definition+88,.5f);number(definition+92,.5f);
        cpu.invoke(0x82771960);
        const auto phaseHead=PPC_LOAD_U32(0x82DFF580);PPC_STORE_U32(0x82DFF580,ball);
        color({1,0,0,1});auto& c=cpu.registers();c.r8.u32=scratch;c.r9.u32=3;c.r10.u32=texture;
        c.f1.f64=c.f2.f64=0;c.f3.f64=1280;c.f4.f64=720;c.f5.f64=c.f6.f64=0;c.f7.f64=c.f8.f64=1;
        cpu.invoke(0x82756480,camera);
        const auto phases=driver.distortionPhaseCount(),draws=driver.distortionDrawCount();
        const auto copied=driver.viewportSurfaces().colorCopyCount();
        const auto oldDepth=driver.readbackDepth(binding.depthIdentity);
        const auto selected=PPC_LOAD_U32(manager+0xC),cache=PPC_LOAD_U32(wrapper+0x2C);
        bool incompleteRejected=false;
        try{effects.completeDistortionScreenReplacement(base,UINT64_MAX,PPC_LOAD_U32(0x82CD1A68),
            PPC_LOAD_U32(0x82CD1A6C),PPC_LOAD_U32(0x82CD1A70));}catch(const Graphics::Error&){incompleteRejected=true;}
        need(incompleteRejected,"Incomplete original distortion falsely authorized a cache replacement");
        driver.directScalar(base,uint32_t(Graphics::ScalarState::DepthWrite),0);
        driver.directScalar(base,uint32_t(Graphics::ScalarState::DepthCompare),7);
        c.lr=0x827517A8;const auto phaseAbi=abi(c);cpu.invoke(0x82772468,camera);
        need(abi(c)==phaseAbi&&driver.distortionPhaseCount()==phases+1&&driver.distortionDrawCount()==draws+5&&
             driver.viewportSurfaces().colorCopyCount()==copied+5&&driver.readbackDepth(binding.depthIdentity)==oldDepth,
             "Retained FX distortion did not complete original five draws/resolves or preserved depth/ABI");
        need(PPC_LOAD_U32(manager+4)==wrapper&&PPC_LOAD_U32(manager+8)==id&&PPC_LOAD_U32(manager+0xC)==selected&&
             PPC_LOAD_U32(wrapper+0x2C)==cache&&PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(0x82CF25A8),
             "Complete original distortion changed logical FX or lost its authored final composite cache");
        // finish rejects cache corruption, retires through original manager,
        // and executes the original scene draw again using the same owner.
        finish();
        PPC_STORE_U32(0x82DFF580,phaseHead);std::memcpy(rt.pointer(0x82DFEA60,128,true),matrices.data(),128);
        std::puts("AUDIT_DISTORTION_LIFECYCLE original_phase=82772468 draws=5 resolves=5 retained_fx=passed manager_release=passed subsequent_use=passed malformed_cache=passed incomplete_phase=passed");
    }
    stage="original FX to alpha-only screen clear to manager retirement";
    const auto alphaPixels=driver.readbackColor(binding.colorIdentity),alphaDepth=driver.readbackDepth(binding.depthIdentity);
    const auto alphaCount=driver.screenDrawCount();const auto alphaFlags=PPC_LOAD_U32(0x82D6CCA8);
    const std::array<uint32_t,3> streams{PPC_LOAD_U32(0x82D0CAB0),PPC_LOAD_U32(0x82D0CAB4),PPC_LOAD_U32(0x82D0CAB8)};
    const auto floating=[](const PPCContext& c){return std::array<uint64_t,18>{c.f14.u64,c.f15.u64,c.f16.u64,c.f17.u64,
        c.f18.u64,c.f19.u64,c.f20.u64,c.f21.u64,c.f22.u64,c.f23.u64,c.f24.u64,c.f25.u64,c.f26.u64,c.f27.u64,
        c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64};};
    const auto alphaAbi=abi(cpu.registers());const auto alphaFloating=floating(cpu.registers());
    cpu.invoke(0x82773B30);
    need(abi(cpu.registers())==alphaAbi,"Original alpha clear damaged nonvolatile integer ABI");
    need(floating(cpu.registers())==alphaFloating,"Original alpha clear damaged nonvolatile floating ABI");
    need(driver.screenDrawCount()==alphaCount+1,"Original alpha clear did not submit one real draw");
    const auto cleared=driver.readbackColor(binding.colorIdentity);need(cleared.size()==alphaPixels.size(),"Alpha clear readback extent differs");
    for(size_t at=0;at<cleared.size();at+=4) {
        uint32_t before{},after{};std::memcpy(&before,alphaPixels.data()+at,4);std::memcpy(&after,cleared.data()+at,4);
        need(after==(before&0x3FFFFFFF),"Original FX alpha clear changed RGB or retained alpha");
    }
    need(driver.readbackDepth(binding.depthIdentity)==alphaDepth&&PPC_LOAD_U32(0x82D6CCA8)==(alphaFlags|2),
         "Original FX alpha clear changed depth or omitted its CPU flag");
    need(PPC_LOAD_U32(0x82CD1A68)==PPC_LOAD_U32(0x82DFEB30)&&
         PPC_LOAD_U32(0x82CD1A6C)==PPC_LOAD_U32(0x82CF231C)&&PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(0x82CF2310)&&
         streams==std::array<uint32_t,3>{PPC_LOAD_U32(0x82D0CAB0),PPC_LOAD_U32(0x82D0CAB4),PPC_LOAD_U32(0x82D0CAB8)},
         "Original FX alpha clear cache/stream publication differs");
    need(effects.privateModifiedMask(id)==std::array<uint8_t,128>{},"Alpha clear dirtied original material values");
    PPC_STORE_U32(0x82D6CCA8,alphaFlags);finish();
    stage="genuine embedded raster dictionary for retained FX sprites";
    // Exact original 82862A28 memory-stream/dictionary path, without creating
    // its unrelated frontend scheduler object. The native bridge verifies the
    // full original source hash, and original RW constructors own both rasters.
    for(const auto [address,word]:std::array<std::array<uint32_t,2>,9>{{
        {0x82862A68,0x4BB96B31},{0x82862A8C,0x4BB954CD},{0x82862A9C,0x4BB9CD0D},
        {0x82862AAC,0x4BB969F5},{0x82862AD0,0x4BB9B5D9},{0x8276B0DC,0x419A0180},
        {0x8276B250,0x38800000},{0x8276B254,0x807FCAF8},{0x8276B258,0x4BCD0159}}})
        need(PPC_LOAD_U32(address)==word,"Original stock texture/sprite branch evidence differs");
    need(!std::strcmp(reinterpret_cast<const char*>(rt.pointer(0x8217F9A0,7,false)),"frame2"),
         "Original stock sprite texture name differs");
    PPC_STORE_U32(records+0x600,0x8215F820);PPC_STORE_U32(records+0x604,0x20150);
    const auto loadAbi=abi(cpu.registers());const auto stream=cpu.invoke(0x823F9598,3,1,records+0x600);
    need(stream&&abi(cpu.registers())==loadAbi,"Original stock memory stream/ABI differs");
    need(cpu.invoke(0x823F7F58,stream,22,0,0)==1,"Original stock dictionary chunk is absent");
    const auto dictionary=cpu.invoke(0x823FF7A8,stream);need(dictionary!=0,"Original stock raster dictionary failed");
    need(cpu.invoke(0x823F94A0,stream,0)==1,"Original stock memory stream did not close");
    const auto texture=cpu.invoke(0x823FE0A8,dictionary,0x8217F9A0);
    need(texture&&PPC_LOAD_U32(texture)&&PPC_LOAD_U32(texture+0x54)==1&&PPC_LOAD_U32(texture+4)==dictionary,
         "Original stock sprite texture lacks its dictionary/raster owner");
    need(abi(cpu.registers())==loadAbi,"Original stock dictionary calls damaged nonvolatile ABI");
    // The original query producer leaves the logical scene FX selected while
    // publishing its textured restoration pair. Its empty-list branch skips
    // every query bind/draw; it must not manufacture a completed transaction.
    for(const auto [address,word]:std::array<std::array<uint32_t,2>,4>{{
        {0x8276A83C,0x83170044},{0x8276A840,0x2B180000},{0x8276A844,0x419A03C0},{0x8276AC04,0x93B70044}}})
        need(PPC_LOAD_U32(address)==word,"Original empty-query dispatch evidence differs");
    const auto queryLogical=[&]{return std::array<uint32_t,4>{PPC_LOAD_U32(manager+4),PPC_LOAD_U32(manager+8),
        PPC_LOAD_U32(manager+0xC),PPC_LOAD_U32(wrapper+0x2C)};};
    const auto querySelected=queryLogical();
    const auto dofWeight=PPC_LOAD_U32(0x82DFF0D0),fogWeight=PPC_LOAD_U32(0x82DFF2C0);
    PPC_STORE_U32(0x82DFF0D0,0);PPC_STORE_U32(0x82DFF2C0,0);
    const auto copies=driver.viewportSurfaces().depthCopyCount();cpu.invoke(0x82751700,camera);
    PPC_STORE_U32(0x82DFF0D0,dofWeight);PPC_STORE_U32(0x82DFF2C0,fogWeight);
    need(driver.viewportSurfaces().depthCopyCount()==copies+1,"Original sprite query depth copy is absent");
    const auto entry=records+0x840,descriptor=records+0x820,spriteColor=records+0x800;
    need(!PPC_LOAD_U32(producer+0x44)&&!PPC_LOAD_U32(producer+0x4C),"Original sprite query producer is busy");
    const auto queryScene=driver.readbackColor(binding.colorIdentity),queryDepth=driver.readbackDepth(binding.depthIdentity);
    auto& c=cpu.registers();
    const auto emptyQuery=[&] {
        need(!PPC_LOAD_U32(producer+0x44)&&!PPC_LOAD_U32(producer+0x4C),"Original zero-entry query producer is busy");
        const auto draws=driver.coronaQueryDrawCount(),screens=driver.screenDrawCount();
        const std::array<uint32_t,3> cached{PPC_LOAD_U32(0x82CD1A68),PPC_LOAD_U32(0x82CD1A6C),PPC_LOAD_U32(0x82CD1A70)};
        const auto queryPixels=driver.viewportSurfaces().readbackColorTexture(0x82DFE8DC);
        const auto oldAbi=abi(c);const auto oldFloating=floating(c);cpu.invoke(0x8276A820,producer);
        need(abi(c)==oldAbi&&floating(c)==oldFloating&&driver.coronaQueryDrawCount()==draws&&driver.screenDrawCount()==screens&&
             queryLogical()==querySelected&&cached==std::array<uint32_t,3>{PPC_LOAD_U32(0x82CD1A68),PPC_LOAD_U32(0x82CD1A6C),PPC_LOAD_U32(0x82CD1A70)}&&
             driver.readbackColor(binding.colorIdentity)==queryScene&&driver.readbackDepth(binding.depthIdentity)==queryDepth&&
             driver.viewportSurfaces().readbackColorTexture(0x82DFE8DC)==queryPixels,
             "Original zero-entry query changed retained FX/caches/output or submitted a draw");
    };
    stage="retained FX genuine zero-entry query then repeated query producer";emptyQuery();
    for(unsigned iteration=0;iteration<2;++iteration) {
        const auto queryDraws=driver.coronaQueryDrawCount(),screenDraws=driver.screenDrawCount();
        c.r4.u32=entry;c.f1.f64=1100;c.f2.f64=360;c.f3.f64=.5;c.f4.f64=16;c.f5.f64=8;
        const auto queryAbi=abi(c);const auto queryFloating=floating(c);
        cpu.invoke(0x8276A7F0,producer);cpu.invoke(0x8276A820,producer);
        need(abi(c)==queryAbi&&floating(c)==queryFloating&&driver.coronaQueryDrawCount()==queryDraws+1&&
             driver.screenDrawCount()==screenDraws&&PPC_LOAD_U8(entry+0x1A)==1&&
             !PPC_LOAD_U32(producer+0x44)&&!PPC_LOAD_U32(producer+0x4C),"Original retained FX query did not publish/reset its live entry");
        need(queryLogical()==querySelected&&effects.privateModifiedMask(id)==std::array<uint8_t,128>{}&&
             PPC_LOAD_U32(0x82CD1A68)==PPC_LOAD_U32(0x82DFEB34)&&PPC_LOAD_U32(0x82CD1A6C)==PPC_LOAD_U32(0x82CF2340)&&
             PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(0x82CF2334),"Original retained FX query lost its logical/restored shader owner");
        need(driver.readbackColor(binding.colorIdentity)==queryScene&&driver.readbackDepth(binding.depthIdentity)==queryDepth,
             "Original retained FX query producer changed scene/depth pixels");
        const auto ready=driver.viewportSurfaces().readbackColorTexture(0x82DFE8DC);
        const auto col=PPC_LOAD_U8(entry+0x18),r=PPC_LOAD_U8(entry+0x19);uint32_t value{};
        need(col<64&&r<8&&ready.size()==64*8*4,"Original retained FX query cell is out of bounds");
        std::memcpy(&value,ready.data()+4*(r*64+col),4);need((value&1023)==1023,"Retained FX query outside geometry is not fully visible");
    }
    emptyQuery();
    const auto produced=driver.viewportSurfaces().readbackColorTexture(0x82DFE8DC);uint32_t visible{};
    const auto column=PPC_LOAD_U8(entry+0x18),row=PPC_LOAD_U8(entry+0x19);
    need(column<64&&row<8&&produced.size()==64*8*4,"Original sprite query cell is out of bounds");
    std::memcpy(&visible,produced.data()+4*(row*64+column),4);
    need((visible&1023)==1023,"Original sprite query outside geometry is not fully visible");
    stage="retained FX genuine completed query to gameplay overlay to retirement";
    color({0,0,0,1});c.r4.u32=scratch;c.r5.u32=0;
    const auto queryOverlay=driver.screenDrawCount(),completedQueries=driver.coronaQueryDrawCount();
    const auto queryOverlayAbi=abi(c);const auto queryOverlayFloating=floating(c);cpu.invoke(0x82755FD0,camera);
    need(abi(c)==queryOverlayAbi&&floating(c)==queryOverlayFloating&&queryLogical()==querySelected&&
         driver.coronaQueryDrawCount()==completedQueries,"Original query-to-overlay lost its completed query/logical FX/ABI");
    check(queryOverlay,false,0xC0000000);need(driver.readbackDepth(binding.depthIdentity)==queryDepth,
         "Original query-to-overlay changed retained scene depth");finish();
    for(uint32_t lane=0;lane<4;++lane)number(spriteColor+4*lane,1);PPC_STORE_U32(descriptor+4,entry);
    std::vector<uint8_t> ordinary,ordinaryScene;
    for(unsigned variant=0;variant<3;++variant) {
        stage=variant==0?"retained FX original ordinary sprite":variant==1?"retained FX original ready query sprite":
            "retained FX original unready query zero-draw sprite";
        PPC_STORE_U8(descriptor,uint8_t(variant!=0));if(variant==2)PPC_STORE_U8(entry+0x1A,0);
        const auto input=driver.readbackColor(binding.colorIdentity),oldDepth=driver.readbackDepth(binding.depthIdentity);
        const auto screens=driver.screenDrawCount(),postFilters=driver.screenEffectDrawCount(6);
        const auto priorPixel=PPC_LOAD_U32(0x82CD1A70);
        c.r9.u32=descriptor;c.r10.u32=texture;c.f1.f64=640;c.f2.f64=360;c.f3.f64=0;c.f4.f64=64;c.f5.f64=64;
        const auto spriteAbi=abi(c);cpu.invoke(0x8276AF78,spriteColor);
        need(abi(c)==spriteAbi&&driver.screenDrawCount()==screens+(variant==2?0:1)&&
             driver.screenEffectDrawCount(6)==postFilters,"Original retained-FX sprite draw count/ABI differs");
        need(PPC_LOAD_U32(0x82CD1A68)==PPC_LOAD_U32(0x82DFEB34)&&
              PPC_LOAD_U32(0x82CD1A6C)==PPC_LOAD_U32(0x82CF2340)&&
              PPC_LOAD_U32(0x82CD1A70)==(variant==2?priorPixel:PPC_LOAD_U32(variant?0x82CF23E0:0x82CF2334)),
              "Original retained-FX sprite shader publication differs");
        need(driver.effectiveState().scalar(Graphics::ScalarState::ExpandedBlend0)==uint32_t(variant==2),
             "Original ready/unready sprite expanded-blend branch differs");
        const auto output=driver.readbackColor(binding.colorIdentity);
        need(driver.readbackDepth(binding.depthIdentity)==oldDepth&&effects.privateModifiedMask(id)==std::array<uint8_t,128>{},
             "Original retained-FX sprite changed depth or material dirtiness");
        if(!variant){need(output!=input,"Original ordinary sprite produced no pixels");ordinary=output;ordinaryScene=input;}
        else if(variant==1)need(input==ordinaryScene&&output==ordinary,"Fully visible original query sprite differs from ordinary pixels");
        else need(output==input,"Original unready sprite changed pixels without a draw");
        const auto batchAbi=abi(c);const auto batchVertex=PPC_LOAD_U32(0x82CD1A6C),batchPixel=PPC_LOAD_U32(0x82CD1A70);
        const auto batchDraws=driver.screenDrawCount();cpu.invoke(0x8276B898);
        need(abi(c)==batchAbi&&driver.screenDrawCount()==batchDraws&&!PPC_LOAD_U32(0x82CD1A68)&&
             PPC_LOAD_U32(0x82CD1A6C)==batchVertex&&PPC_LOAD_U32(0x82CD1A70)==batchPixel&&
              driver.readbackColor(binding.colorIdentity)==output&&driver.readbackDepth(binding.depthIdentity)==oldDepth,
              "Original sprite batch end changed shader caches, pixels, depth or draw count");
        need(driver.effectiveState().scalar(Graphics::ScalarState::ExpandedBlend0)==uint32_t(variant==2),
             "Original sprite batch end changed expanded-blend state");
        finish(variant==2);
    }
    need(cpu.invoke(0x823FEC80,dictionary)==1,"Original sprite dictionary/raster cleanup failed");
    // Original mode-zero's binding-reset factory retains the logical FX while
    // clearing the physical shaders. Exercise both a scene pair and a completed
    // fade receipt, then let the real Im2D setter publish its scratch declaration.
    // No fixture writes a cleared shader cache or creates a reset receipt.
    need(PPC_LOAD_U32(0x823EFDA0)==0x7D8802A6&&PPC_LOAD_U32(0x823F46EC)==0x4BFFB6B4,
         "Original binding-reset entry/caller evidence differs");
    const auto logical=[&]{return std::array<uint32_t,4>{PPC_LOAD_U32(manager+4),PPC_LOAD_U32(manager+8),
        PPC_LOAD_U32(manager+0xC),PPC_LOAD_U32(wrapper+0x2C)};};
    const auto caches=[&]{return std::array<uint32_t,4>{PPC_LOAD_U32(0x82CD1A68),PPC_LOAD_U32(0x82CD1A6C),
        PPC_LOAD_U32(0x82CD1A70),PPC_LOAD_U32(0x82CD1A74)};};
    const auto im2dVertices=records+0x900;
    for(unsigned faded=0;faded<2;++faded) {
        stage=faded?"retained fade FX original binding reset and Im2D to retirement":
            "retained scene FX original binding reset and Im2D to retirement";
        if(faded) {
            color({0,0,0,1});c.r4.u32=scratch;c.r5.u32=0;
            const auto fadeAbi=abi(c);const auto fadeFloating=floating(c);const auto fadeDraws=driver.screenDrawCount();
            cpu.invoke(0x82755FD0,camera);
            need(abi(c)==fadeAbi&&floating(c)==fadeFloating,"Original pre-reset fade damaged nonvolatile ABI");
            check(fadeDraws,false,0xC0000000);
        }
        const auto selected=logical();const auto pixels=driver.readbackColor(binding.colorIdentity);
        const auto oldDepth=driver.readbackDepth(binding.depthIdentity);
        const auto resets=driver.bindingResetCount(),screens=driver.screenDrawCount(),uploads=driver.im2DUploadCount(),
            im2d=driver.im2DDrawCount(),clears=driver.cameraClearCount(),presents=driver.presentationCount(),
            postFilters=driver.screenEffectDrawCount(6);
        const auto resetAbi=abi(c);const auto resetFloating=floating(c);cpu.invoke(0x823EFDA0);
        need(abi(c)==resetAbi&&floating(c)==resetFloating,"Original FX binding reset damaged nonvolatile ABI");
        need(driver.bindingResetCount()==resets+1&&caches()==std::array<uint32_t,4>{}&&
             PPC_LOAD_U32(0x82CD1A64)==UINT32_MAX,"Original FX reset did not publish its genuine cleared bindings");
        need(logical()==selected&&effects.privateModifiedMask(id)==std::array<uint8_t,128>{},
             "Original binding reset changed the retained FX/material owner");
        need(driver.screenDrawCount()==screens&&driver.im2DUploadCount()==uploads&&driver.im2DDrawCount()==im2d&&
             driver.cameraClearCount()==clears&&driver.presentationCount()==presents&&
             driver.screenEffectDrawCount(6)==postFilters&&driver.readbackColor(binding.colorIdentity)==pixels&&
             driver.readbackDepth(binding.depthIdentity)==oldDepth,"Original FX binding reset drew or changed target contents");
        const auto noOpAbi=abi(c);const auto noOpFloating=floating(c);cpu.invoke(0x826B6078,wrapper,selected[2]);
        need(abi(c)==noOpAbi&&floating(c)==noOpFloating&&logical()==selected&&caches()==std::array<uint32_t,4>{}&&
             driver.screenDrawCount()==screens&&driver.im2DDrawCount()==im2d&&
             driver.readbackColor(binding.colorIdentity)==pixels&&driver.readbackDepth(binding.depthIdentity)==oldDepth,
             "Original same-selection no-op rebound a reset FX");
        // A retained reset cannot adopt a different original effect identity.
        // Restore both the source word and the original CPU frame after the
        // deliberately rejected genuine manager callback, before continuing.
        const auto beforeBadIdentity=c;PPC_STORE_U32(typed+0x1C,id^1);bool identityRejected=false;
        try{cpu.invoke(0x826B4B18,wrapper);}catch(const Failure&){identityRejected=true;}
        PPC_STORE_U32(typed+0x1C,id);c=beforeBadIdentity;
        need(identityRejected&&logical()==selected&&caches()==std::array<uint32_t,4>{}&&
             driver.bindingResetCount()==resets+1&&driver.screenDrawCount()==screens&&driver.im2DDrawCount()==im2d&&
             driver.readbackColor(binding.colorIdentity)==pixels&&driver.readbackDepth(binding.depthIdentity)==oldDepth,
             "Changed original effect identity retired or mutated a reset FX");
        // The original 28-byte upload graph supplies the actual native scratch
        // declaration, queue commit, draw and epilogue. These are the same SDK
        // state requests as the independent original Im2D draw contract fixture.
        need(cpu.invoke(0x824025A8,1,0)==1,"Reset FX original Im2D null raster selection failed");cpu.invoke(0x82400040);
        driver.directScalar(base,0x158,0x3F800000);driver.directScalar(base,0x15C,0x3F800000);
        for(uint32_t i=0;i<4;++i) {
            const auto row=im2dVertices+28*i;number(row,i>=2?24.5f:8.5f);number(row+4,i&1?24.5f:8.5f);
            number(row+8,0);number(row+12,1);PPC_STORE_U32(row+16,0xFFFF0000);number(row+20,0);number(row+24,0);
        }
        const std::vector<uint8_t> sourceBytes(rt.pointer(im2dVertices,112,false),rt.pointer(im2dVertices,112,false)+112);
        for(const auto [state,value]:std::array<std::array<uint32_t,2>,10>{{{0x28,0},{0x30,0},{0x2C,7},
            {0x38,2},{0x3C,1},{0x60,1},{0x64,0},{0x68,4},{0x48,6},{0x4C,7}}})cpu.invoke(0x82400170,state,value);
        const auto drawAbi=abi(c);const auto drawFloating=floating(c);
        need(cpu.invoke(0x82409308,4,im2dVertices,4)==1,"Original reset FX Im2D upload/draw failed");
        need(abi(c)==drawAbi&&floating(c)==drawFloating,"Original reset FX Im2D draw damaged nonvolatile ABI");
        need(driver.im2DUploadCount()==uploads+1&&driver.im2DDrawCount()==im2d+1&&
             driver.screenDrawCount()==screens&&driver.screenEffectDrawCount(6)==postFilters&&
             driver.cameraClearCount()==clears&&driver.presentationCount()==presents,
             "Original reset FX Im2D did not complete exactly one genuine uploaded draw");
        need(PPC_LOAD_U32(0x82CD1A68)==PPC_LOAD_U32(0x82D101D8)&&PPC_LOAD_U32(0x82CD1A68)&&
             !PPC_LOAD_U32(0x82CD1A6C)&&!PPC_LOAD_U32(0x82CD1A70)&&logical()==selected&&
             std::equal(sourceBytes.begin(),sourceBytes.end(),rt.pointer(im2dVertices,112,false)),
             "Original Im2D declaration publication lost reset FX/source ownership");
        const auto drawn=driver.readbackColor(binding.colorIdentity);bool exact=drawn.size()==1280*720*4&&pixels.size()==drawn.size();
        if(exact)for(size_t y=0;y<720;++y)for(size_t x=0;x<1280;++x) {
            uint32_t prior{},got{};const auto at=4*(y*1280+x);std::memcpy(&prior,pixels.data()+at,4);std::memcpy(&got,drawn.data()+at,4);
            exact=exact&&(got==((x>=8&&x<24&&y>=8&&y<24)?0xC00003FFu:prior));
        }
        need(exact&&driver.readbackDepth(binding.depthIdentity)==oldDepth,
             "Original reset FX Im2D rectangle pixels/depth differ");
        need(effects.privateModifiedMask(id)==std::array<uint8_t,128>{},"Original reset FX Im2D dirtied material values");
        finish(); // Real no-op, strict draw/cache negatives, original end and next scene draw.
    }
    // Retail stage14's corona work and stage16's movie presenter can retain
    // stage12's scene manager. The optional pre16 fade can run before the
    // presenter; no post-movie stage18 overlay is asserted by this fixture.
    stage="retained FX original movie provider and plane construction";
    for(const auto [address,word]:std::array<std::array<uint32_t,2>,16>{{
        {0x8282EC60,0x9421FF80},{0x8282ED1C,0x93DF0014},{0x8282ED3C,0x4BBD8E6D},
        {0x8282ED54,0x7F64DB78},{0x8282ED58,0x7FE3FB78},{0x8282ED5C,0x4BFFF68D},
        {0x8282E450,0x908B1A6C},{0x8282E468,0x908B1A70},{0x8282E540,0x908B1A68},
        {0x8282E910,0x90830004},{0x8274B1E8,0x909F0004},{0x8243E598,0x39000100},
        {0x8282EC34,0x915F0014},{0x8282EC3C,0x4BE6D275},
        {0x8282ED6C,0x396BD4F0},{0x8282EB24,0x396BD4B0}}})
        need(PPC_LOAD_U32(address)==word,"Original retained-FX movie producer/ownership evidence differs");
    struct RetainedMovieAbi {
        SavedAbi integer;uint64_t r2,r13;std::array<uint64_t,18> fpr;
        std::array<uint8_t,12> condition;std::array<uint8_t,192> vectors;
        bool operator==(const RetainedMovieAbi&) const=default;
    };
    const auto movieAbi=[&](const PPCContext& ctx) {
        RetainedMovieAbi result{abi(ctx),ctx.r2.u64,ctx.r13.u64,floating(ctx),
            {ctx.cr2.lt,ctx.cr2.gt,ctx.cr2.eq,ctx.cr2.so,ctx.cr3.lt,ctx.cr3.gt,ctx.cr3.eq,ctx.cr3.so,
             ctx.cr4.lt,ctx.cr4.gt,ctx.cr4.eq,ctx.cr4.so},{}};
        const std::array<const PPCVRegister*,12> regs{&ctx.v20,&ctx.v21,&ctx.v22,&ctx.v23,&ctx.v24,&ctx.v25,
            &ctx.v26,&ctx.v27,&ctx.v28,&ctx.v29,&ctx.v30,&ctx.v31};
        for(size_t i=0;i<regs.size();++i)std::memcpy(result.vectors.data()+16*i,regs[i]->u8,16);
        return result;
    };
    const auto rasterBaseline=driver.rasterCount();const auto constructionAbi=movieAbi(c);
    const auto heap=cpu.invoke(0x8268E7F0),adapter=cpu.invoke(0x8269BD70,8);
    need(heap&&adapter&&cpu.invoke(0x8274B1C8,adapter,heap)==adapter,
         "Original retained-FX movie heap adapter construction failed");
    const auto provider=cpu.invoke(0x8269BD70,8);need(provider&&cpu.invoke(0x8282ED68,provider)==provider,
         "Original retained-FX movie provider construction failed");
    cpu.invoke(0x8282E910,provider,adapter);
    const auto presenter=cpu.invoke(0x8269BD70,0x44);
    need(presenter&&cpu.invoke(0x8282EB00,presenter)==presenter&&PPC_LOAD_U32(presenter)==0x8215D4B0&&
         !PPC_LOAD_U32(presenter+0x14)&&!PPC_LOAD_U32(presenter+0x18)&&
         PPC_LOAD_U32(provider)==0x8215D4F0&&PPC_LOAD_U32(provider+4)==adapter&&
         PPC_LOAD_U32(adapter)==0x8215094C&&PPC_LOAD_U32(adapter+4)==heap&&movieAbi(c)==constructionAbi,
         "Original retained-FX movie constructor identities/callback/ABI differ");
    struct RetainedMoviePlane {
        uint32_t raster{},context{},data{},pitch{},width{},height{};
        std::vector<uint8_t> bytes,contextBytes;std::weak_ptr<Graphics::Texture> texture;
    };
    struct RetainedMovieFrame {
        uint32_t allocation{},address{};std::vector<uint8_t> descriptor;std::array<RetainedMoviePlane,3> planes;
    };
    std::array<RetainedMovieFrame,2> movieFrames;
    for(uint32_t frame=0;frame<movieFrames.size();++frame) {
        auto& f=movieFrames[frame];f.allocation=cpu.invoke(0x8269BD70,0x54+32);
        need(f.allocation!=0,"Original retained-FX movie descriptor allocation failed");f.address=f.allocation+16;
        std::memset(rt.pointer(f.allocation,0x54+32,true),0xCD,0x54+32);
        const auto oldAbi=movieAbi(c);cpu.invoke(0x823738C0,f.address);c.r8.u64=3;
        cpu.invoke(0x8282E940,provider,f.address,1280,720,0);
        need(movieAbi(c)==oldAbi&&PPC_LOAD_U32(f.address)==0x37047734&&
             PPC_LOAD_U32(f.address+0x28)==1280&&PPC_LOAD_U32(f.address+0x2C)==720&&
             !PPC_LOAD_U32(f.address+0x30)&&PPC_LOAD_U32(f.address+0x38)==3,
             "Original retained-FX movie descriptor allocation/ABI differs");
        // Nonboolean descriptor51 exercises the genuine parent's r4/r27 byte.
        PPC_STORE_U8(f.address+0x51,uint8_t(0xA0+frame));
        for(uint32_t i=0;i<3;++i) {
            auto& p=f.planes[i];p.context=PPC_LOAD_U32(f.address+0x44+4*i);
            need(p.context!=0,"Original movie plane has no RasterContext");p.raster=PPC_LOAD_U32(p.context);
            p.data=PPC_LOAD_U32(f.address+4+4*i);p.pitch=PPC_LOAD_U32(f.address+0x1C+4*i);
            p.width=i?640:1280;p.height=i?360:720;
            need(p.raster&&p.data&&p.pitch==(i?768u:1280u)&&PPC_LOAD_U32(p.raster)==p.raster&&
                 PPC_LOAD_U32(p.raster+0xC)==p.width&&PPC_LOAD_U32(p.raster+0x10)==p.height&&
                 PPC_LOAD_U32(p.raster+0x14)==8&&PPC_LOAD_U32(p.raster+4)==p.data&&
                 PPC_LOAD_U32(p.raster+0x18)==p.pitch&&PPC_LOAD_U8(p.raster+0x22)==4&&
                 PPC_LOAD_U32(p.context+4)==p.data&&PPC_LOAD_U32(p.context+8)==p.pitch,
                 "Original movie linear R8 plane/pitch/initial lock owner differs");
            // Only pitch*height belongs to the plane, not its containing pool.
            auto* bytes=rt.pointer(p.data,p.pitch*p.height,true);std::memset(bytes,0xD5,p.pitch*p.height);
            const auto value=frame?std::array<uint8_t,3>{93,37,211}[i]:std::array<uint8_t,3>{16,128,128}[i];
            for(uint32_t y=0;y<p.height;++y)std::memset(bytes+size_t(y)*p.pitch,value,p.width);
            p.bytes=snapshot(rt,p.data,p.pitch*p.height);p.contextBytes=snapshot(rt,p.context,12);
        }
        f.descriptor=snapshot(rt,f.allocation,0x54+32);
        need(std::all_of(f.descriptor.begin(),f.descriptor.begin()+16,[](uint8_t b){return b==0xCD;})&&
             std::all_of(f.descriptor.end()-16,f.descriptor.end(),[](uint8_t b){return b==0xCD;}),
             "Original movie allocation touched descriptor guard bytes");
    }
    need(driver.rasterCount()==rasterBaseline+6,"Original movie frames do not own six independent planes");
    const auto movieInputBaseline=[&] {
        // These are verified original SDK requests establishing the bounded
        // fixture's inherited state; no cache/program/readiness is injected.
        for(const auto [state,value]:std::array<std::array<uint32_t,2>,16>{{
            {0x6C,0},{0x34,0},{0xD4,15},{0xD0,0},{0xCC,0},{0x150,0},{0xC4,UINT32_MAX},{0xE4,0},
            {0x158,0x3F800000},{0x15C,0x3F800000},{0xAC,0},{0xC8,0},{0x130,1},
            {0x134,0},{0x138,0},{0x13C,0}}})driver.directScalar(base,state,value);
        driver.directScalar(base,0x140,0);
        for(uint32_t state=0xF0;state<=0x12C;state+=4)driver.directScalar(base,state,0);
        for(const auto [state,value]:std::array<std::array<uint32_t,2>,7>{{
            {0,2},{4,2},{8,2},{0x20,0},{0x34,13},{0x1C,0},{0x24,1}}}) {
            uint32_t selector=0;
            for(uint32_t index=1;index<=20;++index)if(PPC_LOAD_U32(0x821506E0+4*index)==state)selector=index;
            need(selector!=0,"Original movie inherited sampler selector is absent");
            for(uint32_t i=0;i<3;++i)driver.applicationSampler(base,0x82D5DB78,i,selector,value,true);
        }
    };
    for(unsigned variant=0;variant<3;++variant) {
        stage=variant==0?"retained scene FX repeated original movie to binding reset/retirement":
            variant==1?"retained query FX repeated original movie to binding reset/retirement":
            "retained query FX pre-movie fade repeated original movie to binding reset/retirement";
        if(variant) {
            const auto selected=logical();const auto pixels=driver.readbackColor(binding.colorIdentity),oldDepth=driver.readbackDepth(binding.depthIdentity);
            const auto oldDof=PPC_LOAD_U32(0x82DFF0D0),oldFog=PPC_LOAD_U32(0x82DFF2C0);
            PPC_STORE_U32(0x82DFF0D0,0);PPC_STORE_U32(0x82DFF2C0,0);const auto copied=driver.viewportSurfaces().depthCopyCount();
            cpu.invoke(0x82751700,camera);PPC_STORE_U32(0x82DFF0D0,oldDof);PPC_STORE_U32(0x82DFF2C0,oldFog);
            need(driver.viewportSurfaces().depthCopyCount()==copied+1,"Original pre-movie corona depth copy is absent");
            c.r4.u32=entry;c.f1.f64=1100;c.f2.f64=360;c.f3.f64=.5;c.f4.f64=16;c.f5.f64=8;
            const auto oldAbi=movieAbi(c);const auto queries=driver.coronaQueryDrawCount();cpu.invoke(0x8276A7F0,producer);cpu.invoke(0x8276A820,producer);
            need(movieAbi(c)==oldAbi&&driver.coronaQueryDrawCount()==queries+1&&logical()==selected&&
                 driver.readbackColor(binding.colorIdentity)==pixels&&driver.readbackDepth(binding.depthIdentity)==oldDepth&&
                 PPC_LOAD_U8(entry+0x1A)==1&&!PPC_LOAD_U32(producer+0x44)&&!PPC_LOAD_U32(producer+0x4C),
                 "Original pre-movie corona did not preserve scene/logical FX or publish a genuine ready query");
            const auto ready=driver.viewportSurfaces().readbackColorTexture(0x82DFE8DC);
            const auto column=PPC_LOAD_U8(entry+0x18),row=PPC_LOAD_U8(entry+0x19);uint32_t visibility{};
            need(column<64&&row<8&&ready.size()==64*8*4,"Original pre-movie query cell extent differs");
            std::memcpy(&visibility,ready.data()+4*(row*64+column),4);
            need((visibility&1023)==1023,"Original pre-movie query cell is not genuinely fully visible");
            if(variant==2) {
                color({0,0,0,1});c.r4.u32=scratch;c.r5.u32=0;
                const auto faded=driver.screenDrawCount();const auto fadeAbi=movieAbi(c);cpu.invoke(0x82755FD0,camera);
                need(movieAbi(c)==fadeAbi,"Original pre-movie fade damaged caller ABI");check(faded,false,0xC0000000);
            }
        }
        movieInputBaseline();const auto selected=logical();
        const auto oldDepth=driver.readbackDepth(binding.depthIdentity);
        const auto screens=driver.screenDrawCount(),queries=driver.coronaQueryDrawCount(),im2d=driver.im2DDrawCount(),
            uploads=driver.im2DUploadCount(),clears=driver.cameraClearCount(),presents=driver.presentationCount(),
            resets=driver.bindingResetCount(),post=driver.screenEffectDrawCount(6),movies=driver.movieDrawCount();
        for(uint32_t frame=0;frame<movieFrames.size();++frame) {
            auto& f=movieFrames[frame];const auto oldAbi=movieAbi(c);cpu.invoke(0x8282EC58,presenter,f.address);
            need(movieAbi(c)==oldAbi&&driver.movieDrawCount()==movies+frame+1&&PPC_LOAD_U32(presenter+0x14)==f.address,
                 "Full original movie presenter did not commit one frame and restore caller ABI");
            need(logical()==selected&&effects.privateModifiedMask(id)==std::array<uint8_t,128>{}&&
                 PPC_LOAD_U32(0x82CD1A68)==PPC_LOAD_U32(0x82DFEB34)&&PPC_LOAD_U32(0x82CD1A6C)==PPC_LOAD_U32(0x82CF2340)&&
                 PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(0x82CF2328),"Movie lost the retained FX or original movie cache publication");
            for(auto& p:f.planes) {
                need(!PPC_LOAD_U32(p.raster+4)&&!PPC_LOAD_U32(p.raster+0x18)&&!(PPC_LOAD_U8(p.raster+0x22)&6),
                     "Original movie presenter did not unlock its current frame");
                p.texture=driver.textureRaster(p.raster);same(rt,p.context,p.contextBytes,"Movie changed its RasterContext publication");
                same(rt,p.data,p.bytes,"Movie changed CPU plane samples or pitch padding");
            }
            for(const auto& descriptorFrame:movieFrames)same(rt,descriptorFrame.allocation,descriptorFrame.descriptor,
                "Movie changed descriptor fields or allocation guards");
            const auto pixels=driver.readbackColor(binding.colorIdentity);bool exact=pixels.size()==1280*720*4;
            const uint32_t expected=frame?0x00039B7F:0x00400003;
            for(size_t at=0;exact&&at<pixels.size();at+=4){uint32_t word{};std::memcpy(&word,pixels.data()+at,4);exact=word==expected;}
            need(exact&&driver.readbackDepth(binding.depthIdentity)==oldDepth,"Original movie plane order/packed full-frame pixels/depth differ");
        }
        const auto pixels=driver.readbackColor(binding.colorIdentity);const auto cached=caches();
        const auto beforeRejected=c;PPC_STORE_U32(0x82CD1A70,cached[2]^1);bool cacheRejected=false;
        try{cpu.invoke(0x823EFDA0);}catch(const Failure&){cacheRejected=true;}
        PPC_STORE_U32(0x82CD1A70,cached[2]);c=beforeRejected;
        PPC_STORE_U32(typed+0x1C,id^1);bool identityRejected=false;
        try{cpu.invoke(0x826B4B18,wrapper);}catch(const Failure&){identityRejected=true;}
        PPC_STORE_U32(typed+0x1C,id);c=beforeRejected;
        need(cacheRejected&&identityRejected&&logical()==selected&&caches()==cached&&driver.bindingResetCount()==resets&&
             driver.movieDrawCount()==movies+2&&driver.readbackColor(binding.colorIdentity)==pixels&&
             driver.readbackDepth(binding.depthIdentity)==oldDepth,"Corrupted movie cache/effect identity changed or retired its retained owner");
        const auto resetAbi=movieAbi(c);cpu.invoke(0x823EFDA0);
        need(movieAbi(c)==resetAbi&&driver.bindingResetCount()==resets+1&&caches()==std::array<uint32_t,4>{}&&
             logical()==selected&&PPC_LOAD_U32(0x82CD1A64)==UINT32_MAX&&driver.movieDrawCount()==movies+2&&
             driver.screenDrawCount()==screens&&driver.coronaQueryDrawCount()==queries&&driver.im2DDrawCount()==im2d&&
             driver.im2DUploadCount()==uploads&&driver.cameraClearCount()==clears&&driver.presentationCount()==presents&&
             driver.screenEffectDrawCount(6)==post&&driver.readbackColor(binding.colorIdentity)==pixels&&
             driver.readbackDepth(binding.depthIdentity)==oldDepth,"Original post-movie reset changed logical FX, ABI, draw counts or targets");
        finish();
    }
    stage="retained FX original movie presenter and plane retirement";
    const auto retirementAbi=movieAbi(c);
    // The real presenter destructor clears descriptor14 before freeing its
    // storage. Frame retirement then uses its independent allocation provider;
    // no fixture clears a current pointer or reads retired pooled CPU storage.
    need(cpu.invoke(0x8282EC08,presenter,1)==presenter,"Original movie presenter deleting destructor failed");
    for(auto& f:movieFrames) {
        cpu.invoke(0x8282EA50,provider,f.address);
        for(const auto& p:f.planes)need(p.texture.expired(),"Movie retained a retired plane texture owner");
        cpu.invoke(0x8269BEB0,f.allocation);
    }
    need(cpu.invoke(0x8282ED78,provider,1)==provider&&cpu.invoke(0x8274B220,adapter,1)==adapter&&
         cpu.invoke(0x8268E7F0)==heap&&movieAbi(c)==retirementAbi&&driver.rasterCount()==rasterBaseline,
         "Original retained-FX movie provider/adapter teardown leaked owners or changed caller ABI");
}
}
