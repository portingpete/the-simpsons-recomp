#include "effect_catalog_lifecycle_helpers.h"
#include "renderer/engine_state.h"
#include "runtime/engine_scene_copies.h"
#include <bit>

namespace {
void run(Runtime& rt,const PPCContext& entry) {
    auto* base=rt.base;auto& fx=rt.engineDriver->effects();EngineCpuCalls cpu(entry,base);
    const auto camera=PPC_LOAD_U32(0x82E07248);
    rt.map(0x50000,4096,true,"edge independent clear fixture");PPC_STORE_U32(0x50000,0xFF0000FF);
    cpu.invoke(0x823EE940,camera,0x50000,7);cpu.invoke(0x823F1A18,camera);
    // Execute the original parent with its pre-manager startup state to create
    // and publish the actual pair and submit the first camera copy.
    const auto mode=PPC_LOAD_U8(0x82CD1430);PPC_STORE_U8(0x82CD1430,1);
    cpu.invoke(0x823C7500,0,camera);PPC_STORE_U8(0x82CD1430,mode);
    need(rt.engineDriver->sceneCopies().count()==2,"Original camera copy pair missing");
    const auto options=cpu.registers().r1.u32+0x60;
    PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    const auto manager=cpu.invoke(0x8269BF70,0x260,options);
    need(manager && cpu.invoke(0x826B6F60,manager,PPC_LOAD_U32(0x82D5DA74))==manager,"Manager construction failed");
    need(cpu.invoke(0x827019E8,table,25)==0 && cpu.invoke(0x827019E8,0x82CD1448,24)==0,"Catalog registration failed");
    const uint32_t wrapper=PPC_LOAD_U32(0x82CD1470),id=PPC_LOAD_U32(wrapper+0x10);
    const auto typed=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(0x82CD146C));
    need(typed && fx.view(id).source==0x8202DF98,"Edge lookup failed");
    const auto beforeAbi=abi(cpu.registers());cpu.invoke(0x823C8F68,typed);
    need(abi(cpu.registers())==beforeAbi && fx.typedReflectionCount(id)==1,"Original edge finalizer ABI/count changed");
    const auto original=fx.view(id).defaultVectorWords;
    const auto source=snapshot(rt,0x8202DF98,6880);
    const auto metadata=fx.view(id);std::vector<uint32_t> savedScalars,savedSamplers;
    for(const auto& row:metadata.scalars)savedScalars.push_back(cpu.invoke(0x826B7940,PPC_LOAD_U32(0x82E06F80+row.sdkId)));
    for(const auto& row:metadata.samplers)savedSamplers.push_back(cpu.invoke(0x826B79B0,row.stage,PPC_LOAD_U32(0x82E07118+row.sdkId)));
    auto& activation=cpu.registers();activation.r31.u32=typed;activation.r30.u32=camera;activation.lr=0x823CA5B4;
    const auto activationAbi=abi(activation);cpu.invoke(0x826B6078,wrapper,0x0003FFFC);
    need(abi(activation)==activationAbi && fx.activeEdge()==id && fx.compiledShaderCount(id)==2,"Native edge begin ABI/compiled ownership differs");
    const auto cacheBytes=snapshot(rt,metadata.cache,metadata.cacheBytes);
    activation.r31.u32=typed;activation.r30.u32=camera;activation.lr=0x823CA5B4;
    cpu.invoke(0x826B6078,wrapper,0x0003FFFC);same(rt,metadata.cache,cacheBytes,"Edge pair-cache hit resaved application values");
    activation.r3.u32=manager;activation.r4.u32=wrapper;activation.r5.u32=0x0003FFFE;activation.lastFunction=0x826B5FC0;
    rejects([&]{fx.beginEdge(activation,base);},"Edge begin accepted a pass handle as a technique");
    same(rt,metadata.cache,cacheBytes,"Rejected activation changed state cache");
    for(uint32_t cycle=0;cycle<4;++cycle) {
        // Cycle 3 retains an expanded-precision request set by an earlier pass: retail's unready sprite branch skips the reset,
        // and the unblended edge rectangle ignores it. It must draw exactly as the other cycles do.
        const uint32_t expandedScalar=uint32_t(Graphics::ScalarState::ExpandedBlend0);
        rt.engineDriver->directScalar(base,expandedScalar,cycle==3?1u:0u);
        need(rt.engineDriver->effectiveState().scalar(Graphics::ScalarState::ExpandedBlend0)==(cycle==3?1u:0u),"Edge fixture could not set the retained expanded request");
        PPC_STORE_U32(0x82CD1428,cycle==0?0x3F800000:(cycle==1?0x3FC00000:0x40200000));
        PPC_STORE_U32(0x82CD1444,cycle==0?0x3E800000:0x3F400000);
        PPC_STORE_U8(0x82CD1431,cycle?0x80:0);PPC_STORE_U8(0x82D09898,cycle==2?0:1);
        std::memset(rt.pointer(metadata.pool,128,true),0xA5,128);
        const auto setterAbi=abi(cpu.registers());cpu.invoke(0x823C90D0,typed);
        need(abi(cpu.registers())==setterAbi,"Original parameter/commit epilogue changed ABI");
        const auto dirty=fx.privateModifiedMask(id);
        need(std::all_of(dirty.begin(),dirty.end(),[](auto b){return b==0;}),"Private dirty cache line did not clear");
        const auto sharedDirty=snapshot(rt,metadata.pool,128);
        need(std::all_of(sharedDirty.begin(),sharedDirty.end(),[](auto b){return b==0;}),"Shared dirty cache line did not clear");
        const auto values=fx.view(id).defaultVectorWords;auto expected=original;
        expected[368/4]=PPC_LOAD_U32(0x82CD1428);expected[400/4]=PPC_LOAD_U32(0x82CD1444);
        expected[384/4]=cycle?0x3F800000:0;expected[416/4]=cycle==2?0:0x3F800000;
        expected[432/4]=0x44A00000;expected[448/4]=0x44340000;
        expected[240/4]=PPC_LOAD_U32(0x82D09894);expected[304/4]=cpu.invoke(0x823ED9C8);
        const auto palette=cpu.invoke(0x823C7140);if(palette)expected[464/4]=palette;
        need(values==expected,"Original edge parameter values or untouched lanes differ");
        same(rt,0x8202DF98,source,"Original effect asset changed");
        const auto depthBefore=rt.engineDriver->readbackDepth(rt.engineDriver->cameraBinding().depthIdentity);
        const auto drawCount=fx.edgeDrawCount();const auto drawAbi=abi(cpu.registers());
        cpu.invoke(0x823CA448,typed,camera);
        need(abi(cpu.registers())==drawAbi && fx.edgeDrawCount()==drawCount+1,"Original rectangle ABI/draw count differs");
        const auto pixels=rt.engineDriver->readbackColor(rt.engineDriver->cameraBinding().colorIdentity);
        need(pixels.size()==1280*720*4,"Edge output extent differs");
        for(size_t p=0;p<pixels.size();p+=4)need(pixels[p]==0 && pixels[p+1]==0 && pixels[p+2]==0 && pixels[p+3]==0xC0,
            "Uniform original source produced an edge or wrong alpha code");
        need(rt.engineDriver->readbackDepth(rt.engineDriver->cameraBinding().depthIdentity)==depthBefore,"Edge draw changed disabled depth/stencil");
        need(rt.engineDriver->effectiveState().scalar(Graphics::ScalarState::ExpandedBlend0)==(cycle==3?1u:0u),"Edge draw changed the retained expanded request");
        rt.engineDriver->directScalar(base,expandedScalar,0);
        auto& c=cpu.registers();c.r31.u32=typed;c.r3.u32=wrapper;c.r4.u32=0x00380018;c.r5.u32=0x100;
        c.lr=0x823C9190;c.lastFunction=0x823C8EB0;fx.edgeBoolean(c,base);
        need(fx.view(id).defaultVectorWords[384/4]==0,"Boolean setter used bits outside the original low byte");
        c.r4.u32=0x00340016;
        const auto unchanged=fx.view(id).defaultVectorWords;
        rejects([&]{fx.edgeBoolean(c,base);},"Boolean setter accepted numeric parameter");
        need(fx.view(id).defaultVectorWords==unchanged,"Rejected boolean write mutated parameters");
    }
    // Mode-zero device reset retains the original selected logical effect.
    // Its later manager end must restore the saved requests without requiring
    // the shaders that the original reset deliberately nulled.
    const auto resetCamera=rt.engineDriver->cameraBinding();
    const auto resetPixels=rt.engineDriver->readbackColor(resetCamera.colorIdentity);
    const auto resetDepth=rt.engineDriver->readbackDepth(resetCamera.depthIdentity);
    const auto resets=rt.engineDriver->bindingResetCount(),resetDraws=fx.edgeDrawCount();
    const auto resetClear=rt.engineDriver->cameraClearCount();const auto resetDirty=fx.privateModifiedMask(id);
    const auto retainedCache=PPC_LOAD_U32(wrapper+0x2C);
    need(PPC_LOAD_U32(0x823EFDA0)==0x7D8802A6&&PPC_LOAD_U32(0x823F46EC)==0x4BFFB6B4,
         "Original mode-zero reset instructions changed");
    {EngineCpuCalls reset(entry,base);const auto saved=abi(reset.registers());reset.invoke(0x823EFDA0);
        need(abi(reset.registers())==saved,"Edge binding reset changed nonvolatile ABI");}
    need(rt.engineDriver->bindingResetCount()==resets+1&&fx.edgeDrawCount()==resetDraws&&
         rt.engineDriver->cameraClearCount()==resetClear,"Edge reset emitted a draw or clear");
    for(uint32_t address:{0x82CD1A68u,0x82CD1A6Cu,0x82CD1A70u,0x82CD1A74u})
        need(!PPC_LOAD_U32(address),"Original edge reset retained a shader/declaration/index cache");
    need(fx.activeEdge()==id&&PPC_LOAD_U32(manager+4)==wrapper&&PPC_LOAD_U32(manager+8)==id&&
         PPC_LOAD_U32(manager+0xC)==0x0003FFFC&&PPC_LOAD_U32(wrapper+0x2C)==retainedCache&&
         fx.privateModifiedMask(id)==resetDirty,"Edge reset changed its retained logical selection");
    same(rt,metadata.cache,cacheBytes,"Edge reset changed saved application requests");
    const auto endContext=cpu.registers();PPC_STORE_U32(manager+8,id^1);
    rejects([&]{cpu.invoke(0x826B4B18,wrapper);},"Edge reset end accepted a changed effect identity");
    PPC_STORE_U32(manager+8,id);cpu.registers()=endContext;
    need(fx.activeEdge()==id&&PPC_LOAD_U32(manager+4)==wrapper&&PPC_LOAD_U32(wrapper+0x2C)==retainedCache&&
         fx.edgeDrawCount()==resetDraws,"Rejected edge reset end changed ownership or drew");
    activation.r31.u32=typed;activation.lr=0x823CA5DC;cpu.invoke(0x826B4B18,wrapper);
    need(rt.engineDriver->readbackColor(resetCamera.colorIdentity)==resetPixels&&
         rt.engineDriver->readbackDepth(resetCamera.depthIdentity)==resetDepth&&fx.edgeDrawCount()==resetDraws&&
         rt.engineDriver->cameraClearCount()==resetClear,"Edge reset/end changed pixels, depth or draw counts");
    need(!fx.activeEdge() && !PPC_LOAD_U32(manager+4) && !PPC_LOAD_U32(manager+0xC) && !PPC_LOAD_U32(wrapper+0x2C),"Edge end retained selection");
    size_t i=0;
    for(const auto& row:metadata.scalars)need(cpu.invoke(0x826B7940,PPC_LOAD_U32(0x82E06F80+row.sdkId))==savedScalars[i++],"Edge end did not restore original scalar requests");
    i=0;for(const auto& row:metadata.samplers)need(cpu.invoke(0x826B79B0,row.stage,PPC_LOAD_U32(0x82E07118+row.sdkId))==savedSamplers[i++],"Edge end did not restore original sampler requests");
    // The next authored pass reads the second copy after the edge rectangle.
    activation.lr=0x823C7684;activation.r26.u32=camera;
    cpu.invoke(0x826B08B0,PPC_LOAD_U32(0x82D6C7F0),0,camera);
    const auto aaWrapper=PPC_LOAD_U32(0x82CD1480),aaId=PPC_LOAD_U32(aaWrapper+0x10);
    const auto aaTyped=cpu.invoke(0x826B7088,manager,0x8200630C);
    need(aaTyped && fx.view(aaId).source==0x8202FA78,"Original AA lookup differs");cpu.invoke(0x823C8290,aaTyped);
    const auto aaSource=snapshot(rt,0x8202FA78,6256);const auto aaOriginal=fx.view(aaId).defaultVectorWords;
    for(uint32_t cycle=0;cycle<2;++cycle){
        PPC_STORE_U32(0x82CD142C,cycle?0x3F19999A:0x3E19999A);
        const auto prior=abi(cpu.registers());const auto draws=fx.aaDrawCount();
        cpu.invoke(0x823CA568,aaTyped,camera,0);
        need(abi(cpu.registers())==prior && fx.aaDrawCount()==draws+1 && !fx.activeEdge(),"Original AA begin/setter/draw/end ABI or ownership differs");
        auto expected=aaOriginal;expected[368/4]=PPC_LOAD_U32(0x82CD142C);expected[240/4]=PPC_LOAD_U32(0x82D6C7F0);
        expected[384/4]=0x44A00000;expected[400/4]=0x44340000;
        need(fx.view(aaId).defaultVectorWords==expected,"AA original parameters or untouched lanes differ");
        same(rt,0x8202FA78,aaSource,"AA original asset changed");
        const auto pixels=rt.engineDriver->readbackColor(rt.engineDriver->cameraBinding().colorIdentity);
        for(size_t p=0;p<pixels.size();p+=4)need(pixels[p]==0 && pixels[p+1]==0 && pixels[p+2]==0 && pixels[p+3]==0xC0,
            "AA of uniform edge output differs");
    }
    cpu.invoke(0x823F1A08,camera);
    cpu.invoke(0x823C70C0);rt.engineDriver->sceneCopies().requireReleased();
    cpu.invoke(0x82701118,0x82CD1448,24);rejects([&]{fx.view(id);},"Retired private parameter owner survived");
    cpu.invoke(0x82701118,table,25);fx.requireReleased();cpu.invoke(0x826B7600,manager,1);
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {need(argc==2,"Original image required");Runtime rt;rt.load(argv[1]);PPCContext c{};rt.initialize(c);const auto entry=c;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;try{runOriginal(c,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original startup checkpoint missing");run(rt,entry);
        std::printf("PASS %zu original edge/AA begin/parameter/commit/rectangle/end ownership and pixel checks\n",checks);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL edge parameters after%zu checks: %s\n",checks,e.what());return 1;}
}
