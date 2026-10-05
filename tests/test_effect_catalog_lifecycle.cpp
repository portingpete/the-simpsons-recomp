// Actual first25 registration and paired destruction.
#include "effect_catalog_lifecycle_helpers.h"
namespace {
void cycle(Runtime& rt,EngineCpuCalls& cpu,uint32_t index,std::array<uint32_t,25>& previous,
           std::array<uint32_t,2>& previousDeclarations,uint32_t& sharedWord) {
    auto* base=rt.base;auto& effects=rt.engineDriver->effects();
    auto& shadowTextures=rt.engineDriver->shadowTextures();
    auto& declarations=rt.engineDriver->quadDeclarations();
    const auto rasterBaseline=rt.engineDriver->rasterCount();
    need(!shadowTextures.count(),"Previous shadows texture lifetime survived");
    need(!declarations.count(),"Previous quad declarations survived");
    const uint32_t context=PPC_LOAD_U32(0x82D5DA74),pool=PPC_LOAD_U32(root),oldBacking=rt.effectPoolBacking;
    const uint32_t options=cpu.registers().r1.u32+0x60;
    const auto originalTable=snapshot(rt,table,400);
    std::array<std::vector<uint8_t>,25> sourceBytes;
    for(uint32_t row=0;row<25;++row) {
        const uint32_t b=PPC_LOAD_U32(table+16*row);sourceBytes[row]=snapshot(rt,b,PPC_LOAD_U32(b+4)+12);
        need(!PPC_LOAD_U32(table+16*row+8),"Original registration row is already occupied");
    }
    stage="original manager construction";
    PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    const uint32_t manager=cpu.invoke(0x8269BF70,0x260,options);
    need(manager && cpu.invoke(0x826B6F60,manager,context)==manager,"Original manager allocation/constructor failed");
    stage="all 25 original registrations";const auto before=abi(cpu.registers());
    need(cpu.invoke(0x827019E8,table,25)==0,"Original complete registration failed");
    need(abi(cpu.registers())==before && effects.count()==25,"Registration ABI/native owner count differs");
    const uint32_t backing=rt.effectPoolBacking,values=PPC_LOAD_U32(pool+0x108);
    need(backing && PPC_LOAD_U32(pool+0x180)==backing && PPC_LOAD_U32(pool+0x184)==0x2A4 &&
         PPC_LOAD_U32(pool+0x188)==1,"Original shared metadata/root reference differs");
    if(index) need(backing==oldBacking && PPC_LOAD_U32(values+64)==sharedWord,"Later registration recopied/reallocated shared values");
    std::array<uint32_t,25> ids{};size_t shaderCount=0,compiled=0,typedCount=0;uint32_t shadows=0,quad=0;
    stage="complete metadata/query/cache verification";
    for(uint32_t row=0;row<25;++row) {
        const uint32_t b=PPC_LOAD_U32(table+16*row),w=PPC_LOAD_U32(table+16*row+8),name=PPC_LOAD_U32(table+16*row+4);
        need(w!=0,"Original row has no wrapper");ids[row]=PPC_LOAD_U32(w+16);
        need(ids[row] && ids[row]!=previous[row] && !rt.pageAccess[ids[row]>>12].load(),"Native FX identity is reused or SDK mapped");
        const auto v=effects.view(ids[row]);
        need(v.manager==manager && v.source==b && v.pool==pool && v.wrapper==w,"Native effect provenance differs");
        const auto owned=effects.originalBytes(ids[row]);
        need(owned.size()==sourceBytes[row].size() && owned.data()!=rt.pointer(b,uint32_t(owned.size()),false) &&
             std::equal(owned.begin(),owned.end(),sourceBytes[row].begin()),"FX did not own immutable complete original bytes");
        shaderCount+=effects.shaderCount(ids[row]);compiled+=effects.compiledShaderCount(ids[row]);
        const uint32_t typed=cpu.invoke(0x826B7088,manager,name);typedCount+=(typed!=0);
        if(row==4) shadows=typed;
        if(row==3) quad=typed;
        need((typed!=0)==(PPC_LOAD_U32(table+16*row+12)!=0),"Original typed callback registration differs");
        cache(rt,cpu,ids[row],b+12);queries(rt,cpu,ids[row],b+12);
    }
    need(shaderCount==131 && compiled==2 && typedCount==24,"Shader capability or original typed callback totals differ");
    stage="original quad CPU declaration ownership";
    need(quad && declarations.count()==2,"Original quad did not acquire both declarations");
    std::array<uint32_t,2> declarationIds{};
    for(uint32_t i=0;i<2;++i) {
        const uint32_t id=PPC_LOAD_U32(quad+0xA8+i*4),source=i?0x820B71A0:0x820B71C4,size=i?36:24;
        declarationIds[i]=id;need(id && id!=previousDeclarations[i] && !rt.pageAccess[id>>12].load(),
            "Native quad declaration ID was reused or mapped");
        const auto record=declarations.record(id);const auto owned=record->bytes();
        need(owned.size()==size && owned.data()!=rt.pointer(source,size,false) &&
             !std::memcmp(owned.data(),rt.pointer(source,size,false),size) &&
             record->elements().size()==i+1 && record->minimumStreamBytes()==(i?16u:8u),
             "Native quad declaration lost exact original records/extent");
    }
    need(declarationIds[0]!=declarationIds[1],"Distinct quad declarations share one identity");
    stage="original shadows texture construction and pixel upload";
    need(shadows && shadowTextures.count()==3 && rt.engineDriver->rasterCount()==rasterBaseline+4,
         "Original shadows did not create its separate camera surfaces and three textures");
    std::array<uint32_t,3> shadowIds{};
    std::array<std::weak_ptr<Graphics::DepthTarget>,2> shadowDepth;
    std::weak_ptr<Graphics::Texture> shadowBorder;
    const uint32_t borrowedDepth=PPC_LOAD_U32(0x82D0CF84);
    constexpr std::array<uint32_t,3> shadowFields={0xF0,0xF4,0xFC};
    for(size_t i=0;i<3;++i) {
        const uint32_t id=PPC_LOAD_U32(shadows+shadowFields[i]);shadowIds[i]=id;
        const auto v=shadowTextures.view(id);const bool border=i==2;
        need(id && !rt.pageAccess[id>>12].load() && id!=borrowedDepth && v.owner==shadows &&
             v.field==shadowFields[i] && v.width==(border?32u:1024u) && v.height==v.width &&
             v.format==(border?0x18280086u:0x1A220197u) && !v.staging &&
             v.phase==(border?EngineShadowTextures::Phase::Uploaded:EngineShadowTextures::Phase::Allocated),
             "Original shadows texture provenance, upload phase or extent differs");
        for(size_t j=0;j<i;++j) need(shadowIds[j]!=id,"Separate shadow textures alias one native identity");
        for(uint32_t cameraField:{0x5B4u,0x5B8u}) {
            const uint32_t camera=PPC_LOAD_U32(shadows+cameraField),extension=PPC_LOAD_U32(0x82E3DC94);
            for(uint32_t rasterField:{0x60u,0x64u})
                need(id!=PPC_LOAD_U32(PPC_LOAD_U32(camera+rasterField)+extension),"Shadow texture aliases its camera raster surface");
        }
        const auto pixels=shadowTextures.readback(id);
        if(border) {
            need(pixels.size()==4096,"Native border texture readback has wrong tight extent");
            for(size_t y=0;y<32;++y) for(size_t x=0;x<32;++x) for(size_t c=0;c<4;++c)
                need(pixels[(y*32+x)*4+c]==((y==0 || y==31 || x==0 || x==31)?255:0),
                     "Actual GPU border pixels differ from original procedural initialization");
            shadowBorder=shadowTextures.texture(id);
            need(!shadowTextures.depth(id),"Border texture accepted as native depth");
        } else {
            need(pixels.size()==size_t(1024)*1024*8,"Separate native shadow depth texture backing extent differs");
            const auto depth=shadowTextures.depth(id);shadowDepth[i]=depth;
            need(depth && depth==rt.engineDriver->depth(id) && depth!=rt.engineDriver->depth(borrowedDepth),
                 "Original shadow depth texture lacks its independent native owner");
            rejects([&]{shadowTextures.texture(id);},"Depth texture exposed as an uploaded color texture");
        }
    }
    need(shadowDepth[0].lock()!=shadowDepth[1].lock() && PPC_LOAD_U32(shadows+0xF8)==borrowedDepth,
         "Original shadow depth owners alias or changed their borrowed copy role");
    stage="shared pool association and edited values";
    const uint32_t sharedNames=PPC_LOAD_U32(pool+0x11C);uint32_t name=sharedNames;
    for(uint32_t i=0;i<11;++i) {
        const auto text=stringAt(rt,name);const uint32_t handle=((i+1)<<18)|(i<<1)|1;
        need(cpu.invoke(0x826B2528,pool,name)==handle,"Original global CPU pool lookup differs");
        for(uint32_t row=0;row<25;++row) {
            const uint32_t expected=(row==0 || row==3)?0:handle;
            need(cpu.invoke(0x823C7B20,ids[row],name)==expected,"Shared FX query did not use the live pool namespace");
            if(expected) {
                const uint32_t desc=PPC_LOAD_U32(pool+0x100)+8*(i+1),slot=PPC_LOAD_U32(desc+4)&0xFFFF;
                need(effects.sharedParameterStorage(ids[row],handle)==values+16*slot,"Shared leaf used ordinal rather than storage-slot association");
            }
        }
        name+=uint32_t(text.size())+1;
    }
    // Fixture-only mutable value. It must remain visible to both local profiles
    // and survive cleanup/re-registration without restoring serialized defaults.
    sharedWord=index?0x3F400000:0x3E800000;PPC_STORE_U32(values+64,sharedWord);
    for(uint32_t row=0;row<25;++row) if(row!=0 && row!=3)
        need(PPC_LOAD_U32(effects.sharedParameterStorage(ids[row],0x80003))==sharedWord,"Shared edited value is not aliased by both profiles");
    stage="real nonfinal retain/release and live retirement protection";
    cpu.invoke(0x82C1CEE8,pool);need(PPC_LOAD_U32(pool+0x188)==2,"Original pool retain failed");
    need(cpu.invoke(0x82C1CF00,pool)==1 && rt.effectPoolRoot==pool && rt.effectPoolBacking==backing &&
         rt.effectPoolThread==GetCurrentThreadId(),"Nonfinal original release invalidated the live pool provenance");
    rejects([&]{cpu.invoke(0x82C1CF00,pool);},"Live native owners allowed final CPU pool release");
    need(PPC_LOAD_U32(pool+0x188)==1 && effects.count()==25,"Rejected retirement changed pool/effects");
    stage="all 25 original paired cleanups";
    cpu.invoke(0x82701118,table,25);need(!effects.count(),"Paired cleanup retained native FX");
    need(!declarations.count(),"Paired quad cleanup retained native CPU declarations");declarations.requireReleased();
    for(uint32_t id:declarationIds) rejects([&]{declarations.record(id);},"Retired native quad declaration ID remained live");
    need(!shadowTextures.count() && shadowDepth[0].expired() && shadowDepth[1].expired() && shadowBorder.expired(),
         "Original shadows texture release retained native ownership");
    shadowTextures.requireReleased();
    for(uint32_t id:shadowIds) rejects([&]{shadowTextures.view(id);},"Retired shadows texture identity remained live");
    need(PPC_LOAD_U32(0x82D0CF84)==borrowedDepth && bool(rt.engineDriver->depth(borrowedDepth)),
         "Original shadows cleanup released its borrowed driver depth-copy role");
    // This is a known distinct production cleanup gap. Do not substitute the
    // isolated camera cleanup helper or claim complete shadow-resource teardown.
    need(rt.engineDriver->rasterCount()==rasterBaseline+4,"Normal shadows raster cleanup behavior changed without qualification");
    effects.requireReleased();effects.requirePoolReleased(pool);
    for(uint32_t row=0;row<25;++row) {
        need(!cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(table+16*row+4)),"Typed registration survived original cleanup");
        rejects([&]{effects.view(ids[row]);},"Stale native FX remained queryable");
        same(rt,PPC_LOAD_U32(table+16*row),sourceBytes[row],"Original FX source changed");
    }
    same(rt,table,originalTable,"Original complete registration table was not restored");
    need(PPC_LOAD_U32(root)==pool && rt.effectPoolBacking==backing && PPC_LOAD_U32(values+64)==sharedWord,
         "Effect cleanup lost root-owned shared values");
    stage="original manager destruction";
    need(cpu.invoke(0x826B7600,manager,1)==manager && !PPC_LOAD_U32(0x82D08BFC),"Original manager destructor failed");
    need(PPC_LOAD_U32(0x82D5DA74)==context && !PPC_LOAD_U32(0x82D0CAF8),"FX lifecycle constructed a console device");
    previous=ids;
    previousDeclarations=declarationIds;
    std::printf("Catalog cycle=%u:25 FX/24 typed callbacks/131 shader records(2 compiled), pool=%08X backing=%08X; FX/three-texture cleanup complete; normal shadows retains4 camera rasters and unqualified CPU extensions\n",index,pool,backing);
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==2,"Original image required");Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);
        try {
            const auto entry=original;bool observed=false;rt.graphicsStartupObserver=observeGraphics;
            try {runOriginal(original,rt.base);}catch(const Observed&){observed=true;}
            rt.graphicsStartupObserver={};need(observed,"Original post-audio startup boundary not reached");
            EngineCpuCalls cpu(entry,rt.base);std::array<uint32_t,25> previous{};
            std::array<uint32_t,2> previousDeclarations{};uint32_t sharedWord=0;
            for(uint32_t i=0;i<2;++i) cycle(rt,cpu,i,previous,previousDeclarations,sharedWord);
            std::printf("PASS effect catalog lifecycle: %zu checks; two original FX/three-texture registration/cleanup lifetimes; ALL MUTED; no draw, complete shadows camera teardown or normal CRT cleanup claim\n",checks);
        }catch(const std::exception& error){std::fprintf(stderr,"Catalog fixture failed before Runtime teardown: stage=%s checks=%zu error=%s\n",stage,checks,error.what());throw;}
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL effect catalog lifecycle: %s\n",error.what());return 1;}
}
