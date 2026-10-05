// Original parent selects/allocates16/256 owners and runs the complete cube
// constructor. A diagnostic observer stops at image loading before its writes.
// Original paired destructor is distinguished from explicit fixture cleanup of
// the rasters/frame it leaves alive. All actual startup/output is muted.
#include "effect_catalog_lifecycle_helpers.h"
#include "runtime/engine_reflection_textures.h"
#include <set>
#include <thread>

namespace {
template<class F> void rejected(F&& f,const char* cause) {
    try {f();} catch(const Failure& e) {
        if(std::string(e.what()).find(cause)==std::string::npos) {
            std::fprintf(stderr,"Expected '%s', got '%s'\n",cause,e.what());need(false,"Reflection rejection cause differs");
        }
        ++checks;return;
    }
    need(false,"Reflection operation unexpectedly accepted");
}
struct Owner {
    uint32_t address,size,camera,frame,colorRaster,depthRaster,colorId,depthId,container;
    std::array<uint32_t,3> ids;
    std::vector<uint8_t> bytes;
    std::weak_ptr<Graphics::CubeTexture> cube;
    std::array<std::weak_ptr<Graphics::RenderTarget>,2> companions;
    std::weak_ptr<Graphics::RenderTarget> color;
    std::weak_ptr<Graphics::DepthTarget> depth;
};
void intact(Runtime& rt,const Owner& o) {
    auto& service=rt.engineDriver->reflectionTextures();const auto view=service.view(o.address);
    need(view.size==o.size && view.phase==EngineReflectionTextures::Phase::Ready && view.completedFaces==6 &&
         !view.staging && view.contextRetained && view.identities==o.ids,"Live reflection owner state differs");
    same(rt,o.address,o.bytes,"Other reflection operation changed original owner bytes");
    need(!o.cube.expired() && !o.companions[0].expired() && !o.companions[1].expired() &&
         !o.color.expired() && !o.depth.expired(),"Other reflection operation released live backing");
}
Owner create(Runtime& rt,const PPCContext& entry,uint32_t size,std::set<uint32_t>& ids) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& service=driver.reflectionTextures();
    const auto oldCount=service.count(),oldLeases=service.leaseCount(),oldRasters=driver.rasterCount();
    const auto constants=snapshot(rt,0x82D63004,0x18),stages=snapshot(rt,0x82D0E3F8,8*0x18),attachments=snapshot(rt,0x82D0CF58,20);
    const uint32_t context=PPC_LOAD_U32(0x82D5DA74);
    {
        EngineCpuCalls cpu(entry,base);
        // The original parent provides the real allocator descriptor, constructor
        // LR and size. No substituted guest callback or fabricated positive LR.
        struct ImageBoundary{};bool observed=false;
        rt.builtinImageBoundaryObserver=[&](uint32_t pc,PPCContext& c,uint8_t*) {
            need(pc==0x82B84838 && uint32_t(c.lr)==0x826FF1D8 && c.r4.u32==0x82CED9A8,"Reflection parent image boundary differs");throw ImageBoundary{};
        };
        try{cpu.invoke(0x826FF0F8,context,size==16?0:1);}catch(const ImageBoundary&){observed=true;}
        rt.builtinImageBoundaryObserver={};need(observed,"Reflection parent image boundary absent");
    }
    Owner o{};o.address=PPC_LOAD_U32(0x82D6301C);o.size=size;
    const auto v=service.view(o.address);o.ids=v.identities;o.camera=v.camera;o.container=v.container;
    need(service.count()==oldCount+1 && service.leaseCount()==oldLeases+1 && driver.rasterCount()==oldRasters+2,
         "Original reflection construction ownership counts differ");
    need(v.size==size && v.context==context && v.contextRetained && v.phase==EngineReflectionTextures::Phase::Ready &&
         v.completedFaces==6 && !v.staging && v.quad && v.camera,"Original reflection constructor did not complete");
    need(PPC_LOAD_U32(o.address)==size && !PPC_LOAD_U32(o.address+4) && PPC_LOAD_U32(o.address+0x14)==context &&
         PPC_LOAD_U8(o.address+0x84)==(size==16?0:1),"Original parent publication/enable selection differs");
    for(uint32_t i=0;i<3;++i) {
        need(o.ids[i] && ids.insert(o.ids[i]).second && service.owns(o.ids[i]) &&
             PPC_LOAD_U32(o.address+8+4*i)==o.ids[i] && !rt.pageAccess[o.ids[i]>>12].load(),"Reflection resource identity is reused/addressable/unpublished");
    }
    need(PPC_LOAD_U32(o.address+0x64)==0x8214E72C && PPC_LOAD_U32(o.address+0x68)==o.container &&
         !PPC_LOAD_U32(o.address+0x6C) && PPC_LOAD_U32(o.address+0x70)==1024 &&
         PPC_LOAD_U32(o.address+0x74)==1024 && !PPC_LOAD_U32(o.address+0x78),"Original reflection CPU container fields differ");
    rt.pointer(o.container,4096,false);
    o.frame=PPC_LOAD_U32(o.camera+4);o.colorRaster=PPC_LOAD_U32(o.camera+0x60);o.depthRaster=PPC_LOAD_U32(o.camera+0x64);
    need(o.frame && o.colorRaster && o.depthRaster && !PPC_LOAD_U32(o.camera+PPC_LOAD_U32(0x82CED790)),"Original reflection camera ownership differs");
    const uint32_t extension=PPC_LOAD_U32(0x82E3DC94);
    o.colorId=PPC_LOAD_U32(o.colorRaster+extension);o.depthId=PPC_LOAD_U32(o.depthRaster+extension);
    bool alphaOne{};o.color=driver.color(o.colorId,alphaOne);o.depth=driver.depth(o.depthId);need(!alphaOne,"Reflection camera color sampling role differs");
    o.cube=service.cube(o.address);
    for(uint32_t i=0;i<2;++i) {
        auto texture=service.companion(o.address,i);const uint32_t n=i?size/2:size;
        need(texture && texture->width==n && texture->height==n && texture->format==Graphics::TargetFormat::RGB10A2,"Reflection companion extent/format differs");
        o.companions[i]=texture;
    }
    for(uint32_t face=0;face<6;++face) {
        const auto bytes=service.readbackFace(o.address,face);
        need(bytes.size()==size_t(size)*size*4,"Native reflection face readback extent differs");
        const size_t known=size_t(size)*(size==16?8:size)*4;
        need(std::all_of(bytes.begin(),bytes.begin()+known,[](uint8_t b){return b==0;}),"Original reflection clear did not reach native face rows");
        // Remaining N16 rows are unspecified. Preservation is checked separately
        // by the seeded WARP/hardware backend test, never by assuming initial zero.
    }
    same(rt,0x82D63004,constants,"Guarded image loader changed constant-texture outputs");
    same(rt,0x82D0E3F8,stages,"Reflection constructor changed shader texture stages");
    same(rt,0x82D0CF58,attachments,"Reflection constructor changed target attachments");
    o.bytes=snapshot(rt,o.address,0x90);intact(rt,o);
    rejected([&]{service.requireReleased();},"retain native context/resources");
    const auto savedContext=PPC_LOAD_U32(o.address+0x14);PPC_STORE_U32(o.address+0x14,0);
    rejected([&]{service.view(o.address);},"retained context field differs");PPC_STORE_U32(o.address+0x14,savedContext);intact(rt,o);
    {
        EngineCpuCalls cpu(entry,base);auto& c=cpu.registers();c.r3.u64=o.ids[0];
        rejected([&]{service.lock(c,base);},"Unsupported original reflection face-lock profile");
        rejected([&]{service.unlock(c,base);},"Unsupported original reflection face-unlock profile");
    }
    intact(rt,o);
    std::exception_ptr error;
    std::thread thread([&]{try {rejected([&]{service.count();},"wrong runtime/thread");} catch(...) {error=std::current_exception();}});
    thread.join();if(error) std::rethrow_exception(error);intact(rt,o);
    std::printf("REFLECTION owner=%08X size=%u cube=%08X companions=%08X,%08X camera=%08X frame=%08X; six original clears\n",
                 o.address,size,o.ids[0],o.ids[1],o.ids[2],o.camera,o.frame);
    return o;
}
void destroy(Runtime& rt,const PPCContext& entry,const Owner& o) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& service=driver.reflectionTextures();intact(rt,o);
    const auto oldCount=service.count(),oldLeases=service.leaseCount(),oldRasters=driver.rasterCount();
    const auto color=snapshot(rt,o.colorRaster,PPC_LOAD_U32(0x82CD1E28)),depth=snapshot(rt,o.depthRaster,PPC_LOAD_U32(0x82CD1E28));
    const auto global=PPC_LOAD_U32(0x82D6301C);const auto aliases=snapshot(rt,0x82D63004,0x18);
    {
        EngineCpuCalls cpu(entry,base);const auto before=abi(cpu.registers());cpu.invoke(0x8273C000,o.address);
        need(abi(cpu.registers())==before,"Original reflection destructor changed nonvolatile ABI");
    }
    need(service.count()+1==oldCount && service.leaseCount()+1==oldLeases,"Paired destructor retained native context/resources");
    need(o.cube.expired() && o.companions[0].expired() && o.companions[1].expired(),"Paired destructor retained native cube/2D backing");
    auto expected=o.bytes;for(uint32_t field:{8u,12u,16u,20u}) std::fill_n(expected.begin()+field,4,uint8_t(0));
    same(rt,o.address,expected,"Paired destructor changed unexpected original owner bytes");
    need(PPC_LOAD_U32(0x82D6301C)==global,"Paired destructor changed the original global publication");
    same(rt,0x82D63004,aliases,"Paired destructor changed unrelated constant texture aliases");
    for(const uint32_t id:o.ids) need(!service.owns(id),"Retired reflection identity still owned");
    rejected([&]{service.view(o.address);},"Unknown or stale native reflection owner");
    rejected([&]{service.readbackFace(o.address,0);},"Unknown or stale native reflection owner");
    // Exact observed ownership limit: direct camera destruction detaches its
    // frame-list link but does not destroy the frame or either attached raster.
    need(driver.rasterCount()==oldRasters && !o.color.expired() && !o.depth.expired(),"Original direct camera cleanup unexpectedly removed attached rasters");
    same(rt,o.colorRaster,color,"Direct camera cleanup changed retained color raster");
    same(rt,o.depthRaster,depth,"Direct camera cleanup changed retained depth raster");
    need(PPC_LOAD_U32(o.frame+0x90)==o.frame+0x90 && PPC_LOAD_U32(o.frame+0x94)==o.frame+0x90,
         "Direct camera destructor did not detach its original frame-list link");
    // Explicit isolated fixture cleanup, after checking the real paired result.
    // These original helpers also appear in82714220; they are not substituted
    // into the game's owner destructor or normal global teardown.
    {
        EngineCpuCalls cpu(entry,base);cpu.invoke(0x82407DC0,o.colorRaster);cpu.invoke(0x82407DC0,o.depthRaster);
        cpu.invoke(0x823F2B60,o.frame);cpu.invoke(0x8269BEB0,o.address);
    }
    need(driver.rasterCount()+2==oldRasters && o.color.expired() && o.depth.expired(),"Explicit fixture attachment cleanup retained backing");
    std::printf("REFLECTION paired cleanup=%08X released context/three textures/camera/container; two rasters/frame then explicitly cleaned by fixture\n",o.address);
}
void run(Runtime& rt,const PPCContext& entry) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& service=driver.reflectionTextures();
    const auto originalRasters=driver.rasterCount();uint32_t manager{};
    {
        EngineCpuCalls cpu(entry,base);const auto opts=cpu.registers().r1.u32+0x60;
        PPC_STORE_U32(opts,2);PPC_STORE_U32(opts+4,16);PPC_STORE_U32(opts+8,0);
        manager=cpu.invoke(0x8269BF70,0x260,opts);
        need(manager && cpu.invoke(0x826B6F60,manager,PPC_LOAD_U32(0x82D5DA74))==manager,"Original effect manager allocation failed");
        need(cpu.invoke(0x827019E8,table,25)==0 && cpu.invoke(0x827019E8,0x82CD1448,24)==0,"Original effect registration failed");
        cpu.invoke(0x826B7218,manager);
    }
    need(driver.effects().count()==49 && driver.rasterCount()==originalRasters+4,"Original effect/shadow setup differs");
    const auto baseline=driver.rasterCount();std::set<uint32_t> ids;std::vector<Owner> previous;
    for(uint32_t cycle=0;cycle<2;++cycle) {
        stage="original reflection construction and clears";
        std::array<Owner,2> pair{create(rt,entry,16,ids),create(rt,entry,256,ids)};intact(rt,pair[0]);
        need(service.count()==2 && service.leaseCount()==2 && pair[0].cube.lock()!=pair[1].cube.lock(),"Concurrent reflection ownership aliases");
        for(const auto& old:previous) for(const auto id:old.ids) need(!service.owns(id),"Old reflection resource ID revived after another constructor");
        stage="original paired reflection cleanup";
        const uint32_t first=cycle?1:0,second=1-first;
        destroy(rt,entry,pair[first]);intact(rt,pair[second]);destroy(rt,entry,pair[second]);
        need(service.count()==0 && service.leaseCount()==0 && driver.rasterCount()==baseline,"Reflection fixture cycle did not restore baseline ownership");
        service.requireReleased();previous.insert(previous.end(),pair.begin(),pair.end());
    }
    {
        EngineCpuCalls cpu(entry,base);cpu.invoke(0x82701118,table,25);cpu.invoke(0x82701118,0x82CD1448,24);
        need(!driver.effects().count() && !driver.shadowTextures().count() && !driver.quadDeclarations().count(),"Original effect cleanup retained its native owners");
        need(cpu.invoke(0x826B7600,manager,1)==manager && !PPC_LOAD_U32(0x82D08BFC),"Original effect manager cleanup failed");
    }
    need(driver.rasterCount()==baseline,"Reflection fixture changed the known shadows raster cleanup gap");
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==2,"Original image required");Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);const auto entry=original;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;
        try {runOriginal(original,rt.base);} catch(const Observed&) {observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original pre-FX startup boundary absent");run(rt,entry);
        std::printf("PASS original reflection texture lifecycle:%zu checks; four parent/constructor lifetimes,24 original face clears,paired context/resource cleanup,both orders; direct camera attachment gap observed;ALL MUTED\n",checks);
        return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL reflection lifecycle:stage=%s checks=%zu %s\n",stage,checks,e.what());return 1;}
}
