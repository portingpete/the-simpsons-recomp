// Whole original decal parent and both virtual children. Geometry construction,
// cached-triangle filtering, model transforms and list traversal remain original.
#include "immediate_original_helpers.h"
#include "runtime/engine_shadow_textures.h"
extern "C" void __imp__sub_82764608(PPCContext&,uint8_t*);
extern "C" void __imp__sub_82766D98(PPCContext&,uint8_t*);
namespace {
constexpr uint32_t area=0x60000,group=area+0x40,definition=area+0x200,quad=area+0x400,mesh=area+0x1000;
constexpr uint32_t actor=area+0x3000,actorFrame=area+0x3100,visible=area+0x3400,hidden=area+0x3420;
uint32_t quadCalls{},meshCalls{};
using Vertex=std::array<float,6>;
void exercise(const char* path){
    ImmediateFixture f(path);auto& rt=f.rt;auto* base=f.base;auto& cpu=*f.cpu;auto& c=cpu.registers();auto& d=f.driver();
    rt.map(area,0x5000,true,"original decal group and cached geometry");std::memset(rt.pointer(area,0x5000,true),0,0x5000);
    Restore projection(rt,0x82DFEAE0,64),unrelated(rt,0x82DFEAA0,64),ring(rt,f.ring,0x2000);
    matrix(base,0x82DFEAE0,{.75f,0,0,0,0,.5f,0,0,0,0,1,0,.125f,.125f,0,1});matrix(base,0x82DFEAA0,translated(4,4));
    PPC_STORE_U16(definition+0x10,0);PPC_STORE_U8(definition+0x14,0);PPC_STORE_U8(definition+0x15,0);
    put(base,definition+0x24,.5f);put(base,definition+0x28,.25f);put(base,definition+0x2C,.125f);
    put(base,definition+0x58,.5f);put(base,definition+0x5C,.375f);
    auto initialize=[&](uint32_t node,bool cached){
        PPC_STORE_U32(node,cached?0x82153240:0x82153174);PPC_STORE_U32(node+0x10,definition);
        put(base,node+0x18,.5f);put(base,node+0x1C,.75f);put(base,node+0x20,.125f);put(base,node+0x24,.25f);
        put(base,node+0x28,.6f);put(base,node+0x2C,.8f);matrix(base,node+0x40,translated(.125f,-.125f));
    };
    initialize(quad,false);initialize(mesh,true);PPC_STORE_U16(mesh+0x1158,2);PPC_STORE_U32(mesh+0x80,actor+0x40);
    PPC_STORE_U8(actor+2,4);PPC_STORE_U32(actor+4,actorFrame);matrix(base,actorFrame+0x50,translated(.25f,.125f));
    PPC_STORE_U8(visible+12,1);PPC_STORE_U8(hidden+12,0);
    auto list=[&](bool simple,bool triangles){cpu.invoke(0x82764A40,group);PPC_STORE_U32(group,definition);PPC_STORE_U32(group+8,f.texture);
        if(simple)cpu.invoke(0x827649E0,group,quad);if(triangles)cpu.invoke(0x82764A10,group,mesh);};
    auto invoke=[&]{seed(c);const auto before=abi(c);cpu.invoke(0x827640A0,group);need(abi(c)==before,"Original decal parent changed nonvolatile GPR/FPR/VMX, SP or LR");};
    auto clear=[&](uint32_t compare){PPC_STORE_U32(0x50010,0x000000FF);need(cpu.invoke(0x823EE940,f.camera,0x50010,7)==1,"Original decal camera clear failed");
        d.directScalar(base,uint32_t(Graphics::ScalarState::DepthCompare),compare);f.poison(0x2000);};
    auto checkVertices=[&](const std::vector<Vertex>& expected){need(PPC_LOAD_U32(f.row)==f.ring+32*expected.size(),"Original decal reservation size differs");
        for(uint32_t v=0;v<expected.size();++v){const uint32_t at=f.ring+32*v;for(uint32_t lane=0;lane<6;++lane)
            need(std::abs(get(base,at+4*lane)-expected[v][lane])<.000003f,"Original decal position/alpha/UV differs");
            need(PPC_LOAD_U32(at+24)==UINT32_MAX&&PPC_LOAD_U32(at+28)==UINT32_MAX,"Original decal overwrote unused UV lanes");}};
    auto expectedQuad=[&](float alpha){const float k=.75f*.7071067690849304f,x=.6f*k,y=.8f*k;
        return std::vector<Vertex>{{x,y,0,alpha,.125f,.25f},{-y,x,0,alpha,.125f,.625f},{y,-x,0,alpha,.625f,.25f},{-x,-y,0,alpha,.625f,.625f}};};
    auto after=[&](const std::vector<uint8_t>& depth){sameDepth(f.depth(),depth);f.lifetime();using S=Graphics::ScalarState;
        need(d.effectiveState().scalar(S::DepthEnable)==1&&d.effectiveState().scalar(S::DepthWrite)==1&&
             !d.effectiveState().scalar(S::DepthBias)&&!d.effectiveState().scalar(S::SlopeBias)&&!d.effectiveState().scalar(S::AlphaTest),"Original decal cleanup left render state active");};
    stage="original empty decal lists";list(false,false);clear(4);const auto black=f.color();auto before=d.immediateDrawCount();auto depth=f.depth();invoke();
    need(d.immediateDrawCount()==before&&PPC_LOAD_U32(f.row)==f.ring&&f.color()==black,"Empty decal lists submitted geometry");after(depth);
    stage="mode-one quad and original positive depth bias";list(true,false);clear(4);before=d.immediateDrawCount();depth=f.depth();const auto original=snapshot(rt,area,0x5000);
    const auto oldQuads=quadCalls;invoke();need(quadCalls==oldQuads+1&&d.immediateDrawCount()==before+1,"Decal parent omitted original quad child");
    checkVertices(expectedQuad(.5f));same(rt,area,original,"Original decal draw changed its inputs/list");after(depth);const auto full=f.color();need(colored(full)>100,"Mode-one decal omitted biased GPU pixels");
    for(uint32_t y=0;y<720;++y)for(uint32_t x=0;x<1280;++x)if(pixel(full,4*(1280*y+x))&0x3FFFFFFF)
        need(x>=575&&x<=987&&y>=259&&y<=416,"Decal ignored original model/combined projection");
    stage="depth-bias rejects equality at original zero depth";clear(2);before=d.immediateDrawCount();depth=f.depth();invoke();
    need(d.immediateDrawCount()==before+1&&f.color()==black,"Mode-one decal lost its original positive constant depth bias");after(depth);
    stage="retained owner and alpha modulation";clear(4);put(base,quad+0x18,.25f);invoke();checkVertices(expectedQuad(.25f));const auto dim=f.color();uint32_t ratios=0;
    for(size_t i=0;i<full.size();i+=4){const auto a=pixel(full,i),b=pixel(dim,i);for(uint32_t shift:{0u,10u,20u}){const auto av=(a>>shift)&1023,bv=(b>>shift)&1023;
        if(av>=16&&av<900){need(std::abs(float(bv)-float(av)*.5f)<=1.5f,"Decal additive blend ignored vertex alpha");++ratios;}}}need(ratios>100,"Decal alpha regression had no eligible pixels");
    stage="original alpha threshold";put(base,quad+0x18,0);clear(4);before=d.immediateDrawCount();depth=f.depth();invoke();
    need(d.immediateDrawCount()==before&&PPC_LOAD_U32(f.row)==f.ring&&f.color()==black,"Invisible decal reserved or drew geometry");after(depth);put(base,quad+0x18,.5f);
    stage="original alpha-test reference";PPC_STORE_U8(definition+0x15,255);clear(4);before=d.immediateDrawCount();depth=f.depth();invoke();
    need(d.immediateDrawCount()==before+1&&f.color()==black,"Decal ignored original alpha-test reference");after(depth);PPC_STORE_U8(definition+0x15,0);
    const std::array<Vertex,6> triangles={{{-.5f,-.375f,0,.5f,.125f,.25f},{.5f,-.375f,0,.5f,.625f,.25f},{-.5f,.375f,0,.5f,.125f,.625f},
        {.5f,-.375f,0,.5f,.625f,.25f},{.5f,.375f,0,.5f,.625f,.625f},{-.5f,.375f,0,.5f,.125f,.625f}}};
    auto polygon=[&](bool hideSecond,bool transform){PPC_STORE_U32(mesh+0x10F0,6);for(uint32_t v=0;v<6;++v){const uint32_t at=mesh+0xD0+32*v;const auto& values=triangles[v];
        for(uint32_t lane=0;lane<3;++lane)put(base,at+4*lane,values[lane]);put(base,at+12,values[4]);put(base,at+16,values[5]);
        PPC_STORE_U32(at+20,actor);PPC_STORE_U32(at+24,hideSecond&&v>=3?hidden:visible);PPC_STORE_U32(at+28,transform?0:1);}};
    stage="original cached triangle list";list(false,true);polygon(false,false);clear(4);before=d.immediateDrawCount();depth=f.depth();const auto cached=snapshot(rt,area,0x5000);
    const auto oldMeshes=meshCalls;invoke();need(meshCalls==oldMeshes+1&&d.immediateDrawCount()==before+1,"Decal parent omitted original triangle child");
    checkVertices({triangles.begin(),triangles.end()});need(colored(f.color())>100,"Cached decal triangles produced no GPU pixels");same(rt,area,cached,"Cached decal input changed");after(depth);
    stage="original visibility filtering";polygon(true,false);clear(4);before=d.immediateDrawCount();depth=f.depth();invoke();checkVertices({triangles.begin(),triangles.begin()+3});
    need(d.immediateDrawCount()==before+1&&colored(f.color())>100,"Original triangle visibility filtering lost surviving face");after(depth);
    stage="original actor flags skip all triangles";PPC_STORE_U8(actor+2,0);clear(4);before=d.immediateDrawCount();depth=f.depth();invoke();
    need(d.immediateDrawCount()==before&&PPC_LOAD_U32(f.row)==f.ring&&f.color()==black,"Hidden actor triangles were submitted");after(depth);PPC_STORE_U8(actor+2,4);
    stage="original actor-to-decal transform";polygon(false,true);clear(4);before=d.immediateDrawCount();depth=f.depth();invoke();
    auto transformed=std::vector<Vertex>(triangles.begin(),triangles.end());for(auto& v:transformed){v[0]+=.125f;v[1]+=.25f;}checkVertices(transformed);
    need(d.immediateDrawCount()==before+1&&colored(f.color())>100,"Actor-transformed decal produced no output");after(depth);
    stage="cached mesh original quad fallback";PPC_STORE_U32(mesh+0x10F0,0);clear(4);before=d.immediateDrawCount();depth=f.depth();const auto fallbackQuads=quadCalls;invoke();
    need(quadCalls==fallbackQuads+1&&d.immediateDrawCount()==before+1,"Empty cached mesh lost original quad fallback");checkVertices(expectedQuad(.5f));after(depth);
    stage="both original intrusive lists";list(true,true);polygon(false,false);clear(4);before=d.immediateDrawCount();depth=f.depth();invoke();
    auto both=expectedQuad(.5f);both.insert(both.end(),triangles.begin(),triangles.end());checkVertices(both);need(d.immediateDrawCount()==before+2,"Decal parent omitted one intrusive list");after(depth);
    stage="original mesh rebuild deferral and expiry";
    constexpr uint32_t waitingMesh=area+0x3500;initialize(waitingMesh,true);put(base,waitingMesh+0x18,0);
    PPC_STORE_U16(waitingMesh+0x1158,0);PPC_STORE_U16(waitingMesh+0x115A,5);put(base,waitingMesh+0x14,.25f);put(base,definition+0x18,.75f);
    list(false,true);cpu.invoke(0x82764A10,group,waitingMesh);PPC_STORE_U16(mesh+0x1158,0);PPC_STORE_U32(mesh+0x1100,0);put(base,mesh+0x18,0);
    clear(4);before=d.immediateDrawCount();depth=f.depth();const auto deferredMeshes=meshCalls;invoke();
    need(meshCalls==deferredMeshes+2&&d.immediateDrawCount()==before&&PPC_LOAD_U32(f.row)==f.ring&&f.color()==black,"Deferred original meshes reserved geometry or lost a callback");
    need(PPC_LOAD_U16(mesh+0x1158)==2&&!PPC_LOAD_U32(mesh+0x10F0)&&PPC_LOAD_U16(waitingMesh+0x115A)==6&&get(base,waitingMesh+0x14)==1,
         "Original mesh rebuild counter, retry expiry or lifetime update differs");after(depth);put(base,mesh+0x18,.5f);
    stage="malformed original owner guards";list(true,false);clear(4);const auto valid=c;const auto guardPixels=f.color();const auto guardCaches=caches(base);before=d.immediateDrawCount();
    auto rejected=[&]{rejects([&]{cpu.invoke(0x827640A0,group);},"Invalid decal ownership accepted");c=valid;
        need(d.immediateDrawCount()==before&&PPC_LOAD_U32(f.row)==f.ring&&f.color()==guardPixels&&caches(base)==guardCaches,"Rejected decal changed rendering");};
    PPC_STORE_U32(quad,0x82153178);rejected();PPC_STORE_U32(quad,0x82153174);
    PPC_STORE_U32(quad+0x10,definition+0x80);rejected();PPC_STORE_U32(quad+0x10,definition);
    PPC_STORE_U32(quad+0x30,quad);rejected();list(true,false);
    stage="valid owner after guards";clear(4);before=d.immediateDrawCount();invoke();need(d.immediateDrawCount()==before+1&&colored(f.color())>100,"Rejected decal poisoned later valid scope");
    stage="original mode-one null texture and valid retry";list(true,false);PPC_STORE_U32(group+8,0);PPC_STORE_U8(definition+0x15,1);
    clear(4);before=d.immediateDrawCount();depth=f.depth();invoke();checkVertices(expectedQuad(.5f));
    need(d.immediateDrawCount()==before+1&&f.color()==black,"Mode-one null texture bypassed original alpha discard");after(depth);
    PPC_STORE_U32(group+8,f.texture);clear(4);before=d.immediateDrawCount();invoke();
    need(d.immediateDrawCount()==before+1&&colored(f.color())>100,"Mode-one null texture poisoned valid retry");PPC_STORE_U8(definition+0x15,0);
    stage="real projected shadow owner construction";f.end();constexpr uint32_t table=0x82CEFD20,options=0x50020;
    PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    const auto manager=cpu.invoke(0x8269BF70,0x260,options);need(manager!=0,"Original projected effect manager allocation failed");
    need(cpu.invoke(0x826B6F60,manager,PPC_LOAD_U32(0x82D5DA74))==manager,"Original projected manager constructor failed");
    need(!cpu.invoke(0x827019E8,table,25),"Original projected catalog registration failed");cpu.invoke(0x826B7218,manager);
    const auto shadowOwner=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(table+4*16+4));need(shadowOwner!=0,"Original projected shadows owner missing");
    const auto shadowIdentity=PPC_LOAD_U32(shadowOwner+0xF0),shadowCamera=PPC_LOAD_U32(shadowOwner+0x5B4);
    auto& shadows=d.shadowTextures();const auto resource=shadows.view(shadowIdentity);
    need(resource.owner==shadowOwner&&resource.field==0xF0&&resource.width==1024&&resource.height==1024,"Original projected shadow ownership differs");
    Restore publishedShadow(rt,0x82DFEB98,4);
    PPC_STORE_U32(0x82DFEB98,shadowOwner);
    // Populate the genuine shadow camera through the original six-face CPU
    // queue, then execute the complete original depth-copy helper. The fixture
    // therefore samples a real owned depth resource with a known copied value.
    stage="original shadow source draw and copy";need(cpu.invoke(0x823F1A18,shadowCamera)==shadowCamera,"Original projected shadow camera begin failed");
    need(cpu.invoke(0x823F1B80,shadowCamera,0x50010,7)==shadowCamera,"Original projected shadow clear failed");
    using S=Graphics::ScalarState;using T=Graphics::SamplerState;
    d.directScalar(base,uint32_t(S::DepthCompare),7);d.directScalar(base,uint32_t(S::DepthBias),0);d.directScalar(base,uint32_t(S::SlopeBias),0);
    matrix(base,0x82DFEAE0,{1,0,0,0,0,4,0,0,0,0,0,0,0,0,.25f,1});
    constexpr uint32_t shadowQueue=0x70000;rt.map(shadowQueue,0x3000,true,"original projected shadow source queue");cpu.invoke(0x823CB168,shadowQueue);
    const auto row=cpu.invoke(0x823CB178,shadowQueue);put(base,row,-2);put(base,row+4,0);put(base,row+8,.5f);
    put(base,row+12,2);put(base,row+16,0);put(base,row+20,.5f);put(base,row+24,.4f);PPC_STORE_U32(row+28,f.texture);put(base,row+32,0);put(base,row+36,1);
    f.poison(0x2000);cpu.invoke(0x823CB2A0,shadowQueue);
    {EngineCpuCalls copy(f.entry,base);auto& copyContext=copy.registers();copyContext.r1.u32-=0xE0;PPC_STORE_U32(copyContext.r1.u32,copyContext.r1.u32+0xE0);
    copyContext.lr=0x827073BC;copyContext.r31.u32=shadowOwner;copyContext.r26.u32=0;copyContext.r24.u32=shadowCamera;
    const auto copies=shadows.copyCount();copy.invoke(0x82704BE8,shadowIdentity);need(shadows.copyCount()==copies+1,"Original projected shadow copy omitted");}
    const auto shadowPixels=shadows.readback(shadowIdentity);uint32_t sampleX=0,sampleY=0;
    auto shadowValue=[&](uint32_t x,uint32_t y){float value{};std::memcpy(&value,shadowPixels.data()+8*(1024*y+x),4);return value;};
    for(uint32_t y=2;y<1022&&!sampleX;++y) {
        for(uint32_t x=2;x<1022;++x) {
            bool good=true;
            for(int dy=-1;dy<=1;++dy) {
                for(int dx=-1;dx<=1;++dx) {
                    good&=shadowValue(uint32_t(int(x)+dx),uint32_t(int(y)+dy))==.75f;
                }
            }
            if(good){sampleX=x;sampleY=y;break;}
        }
    }
    need(sampleX!=0,"Original shadow source did not produce a stable copied depth footprint");
    need(cpu.invoke(0x823F1A08,shadowCamera)==shadowCamera&&cpu.invoke(0x823F1A18,f.camera)==f.camera,"Original projected camera restoration failed");
    f.bound=d.cameraBinding();matrix(base,0x82DFEAE0,{.75f,0,0,0,0,.5f,0,0,0,0,1,0,.125f,.125f,0,1});
    for(uint32_t sampler:{0u,2u})for(const auto& [id,value]:std::array<std::pair<T,uint32_t>,4>{{{T::LodBiasBits,0},{T::MinimumMip,0},{T::MaximumMip,13},{T::MaximumAnisotropy,1}}})
        d.directSampler(base,sampler,uint32_t(id),value);
    const float u=(float(sampleX)+.5f)/1024,v=(float(sampleY)+.5f)/1024;
    matrix(base,shadowOwner+0x260,{0,0,0,0,0,0,0,0,0,0,0,0,4*u-2,2-4*v,1,2});
    auto projectedRun=[&](float ambient,bool trianglesToo){list(true,trianglesToo);polygon(false,false);PPC_STORE_U16(definition+0x10,0x20);put(base,definition+0x48,ambient);
        clear(4);const auto count=d.immediateDrawCount();const auto z=f.depth();const auto input=snapshot(rt,area,0x5000);invoke();
        need(d.immediateDrawCount()==count+(trianglesToo?2:1),"Projected decal lost an original child draw");
        auto expected=expectedQuad(.5f);if(trianglesToo)expected.insert(expected.end(),triangles.begin(),triangles.end());checkVertices(expected);
        same(rt,area,input,"Projected decal changed original inputs");need(shadows.readback(shadowIdentity)==shadowPixels,"Projected decal changed sampled shadow pixels");after(z);return f.color();};
    stage="whole original projected decal negative predicate";const auto projectedFull=projectedRun(-1,false);
    need(projectedFull==full,"Negative projected predicate differs from original mode-one pixels");
    stage="whole original projected decal shadow interpolation";const auto projectedDim=projectedRun(.25f,false);uint32_t shadowRatios=0;
    for(size_t i=0;i<projectedFull.size();i+=4){const auto a=pixel(projectedFull,i),b=pixel(projectedDim,i);need((a>>30)==(b>>30),"Projected lighting changed decal alpha");
        for(uint32_t shift:{0u,10u,20u}){const auto av=(a>>shift)&1023,bv=(b>>shift)&1023;if(av>=16&&av<900){
            need(std::abs(float(bv)-float(av)*.25f)<=1.5f,"Projected decal lost original shadow matrix or ambient term");++shadowRatios;}}}
    need(shadowRatios>100,"Projected decal shadow regression had no eligible pixels");
    stage="whole original projected quad and triangle children";need(!colored(projectedRun(0,true)),"Fully shadowed projected children retained RGB");
    stage="projected original owner remains live";need(shadows.view(shadowIdentity).owner==shadowOwner&&PPC_LOAD_U32(0x82DFEB98)==shadowOwner,"Projected shadow owner changed");
    stage="original projected null texture and valid retry";list(true,false);put(base,definition+0x48,.25f);PPC_STORE_U8(definition+0x15,1);PPC_STORE_U32(group+8,0);
    clear(4);before=d.immediateDrawCount();depth=f.depth();invoke();checkVertices(expectedQuad(.5f));
    need(d.immediateDrawCount()==before+1&&f.color()==black,"Projected null texture bypassed original alpha discard");after(depth);
    need(shadows.readback(shadowIdentity)==shadowPixels,"Null projected decal changed shadow pixels");
    PPC_STORE_U32(group+8,f.texture);clear(4);before=d.immediateDrawCount();invoke();
    need(d.immediateDrawCount()==before+1&&colored(f.color())>100,"Projected null texture poisoned valid retry");PPC_STORE_U8(definition+0x15,0);
    // Bounded neighboring consumers preserve the exact original frame and
    // upload calls, while supplying CPU vertices at their reservation boundary.
    // Full original billboard/trail simulation is outside this fixture.
    stage="bounded projected billboard and trail variants";
    constexpr uint32_t boundaryArea=0x80000,boundaryOwner=boundaryArea,boundaryDefinition=boundaryArea+0x400,boundaryMaterial=boundaryArea+0x800;
    rt.map(boundaryArea,0x1000,true,"projected billboard and trail boundary inputs");
    auto bounded=[&](bool billboard,bool dual,bool projected,float ambient){
        std::memset(rt.pointer(boundaryArea,0x1000,true),0,0x1000);clear(7);const auto initial=c;const auto z=f.depth();const auto draws=d.immediateDrawCount();
        const uint32_t frameBytes=billboard?0x1F0:0x1B0,mode=(dual?2u:0u)|(projected?4u:0u);
        c.r1.u32=initial.r1.u32-frameBytes;const uint32_t stack=c.r1.u32,entry=stack+0x70;PPC_STORE_U32(stack,initial.r1.u32);
        c.r31.u32=boundaryOwner;
        if(billboard){
            PPC_STORE_U32(boundaryOwner,0x821530EC);PPC_STORE_U32(boundaryOwner+0x118,boundaryDefinition);PPC_STORE_U32(boundaryOwner+0x11C,boundaryMaterial);
            PPC_STORE_U32(boundaryOwner+0xD0,f.texture);PPC_STORE_U32(boundaryOwner+0xD4,dual?f.texture:0);
            PPC_STORE_U8(boundaryDefinition+0x40,dual?1:0);PPC_STORE_U8(boundaryDefinition+0x104,projected?4:0);
            PPC_STORE_U32(boundaryDefinition+0xD0,0x80);PPC_STORE_U32(boundaryDefinition+0xD4,1);PPC_STORE_U8(boundaryDefinition+0x106,1);
            put(base,boundaryDefinition+0xE4,ambient);c.lr=0x8275F21C;cpu.invoke(0x82751B68,entry,stack+0x64,mode);
            c.r3.u32=entry;c.r4.u32=f.texture;c.r5.u32=dual?f.texture:0;d.billboardOperation(c,base,0x8275F228);
        }else{
            PPC_STORE_U32(boundaryOwner,0x82158BB0);PPC_STORE_U32(boundaryOwner+0xA0,boundaryDefinition);PPC_STORE_U32(boundaryOwner+0xA4,f.texture);PPC_STORE_U32(boundaryOwner+0xA8,dual?f.texture:0);
            PPC_STORE_U32(boundaryDefinition+0x20,projected?0x20:0);PPC_STORE_U8(boundaryDefinition+0x24,0);put(base,boundaryDefinition+0x74,ambient);
            for(uint32_t lane=0;lane<3;++lane)put(base,boundaryDefinition+0x44+4*lane,.25f);
            c.lr=0x8277E3BC;cpu.invoke(0x82751B68,entry,stack+0x50,mode);c.f19.f64=0;c.f31.f64=.5;d.beginTrail(c,base);
            c.lr=0x8277E518;const auto helperAbi=abi(c);cpu.invoke(0x827518D0,entry,0x8215A470,projected?1:0);
            need(abi(c)==helperAbi,"Original projected trail matrix helper changed ABI");
        }
        const auto input=snapshot(rt,boundaryArea,0x1000);std::vector<uint8_t> first;
        for(unsigned pass=0;pass<2;++pass){
            if(billboard){c.lr=0x8275F494;const auto helperAbi=abi(c);cpu.invoke(0x827518D0,entry,0x8215A470,projected?1:0);
                need(abi(c)==helperAbi,"Original projected billboard matrix helper changed ABI");
                c.r31.u32=boundaryOwner;for(uint32_t lane=0;lane<4;++lane)put(base,stack+0x80+4*lane,lane==3?.5f:.25f);
                d.billboardOperation(c,base,0x8275F494);c.f1.f64=ambient;d.billboardOperation(c,base,0x8275F4E8);}
            c.r3.u32=entry;c.r4.u32=6;c.r5.u32=4;c.lr=billboard?0x8275F6E4:0x8277E640;d.reserveImmediate(c,base);const auto vertices=c.r3.u32;
            constexpr std::array<std::array<float,6>,4> corners={{{-.5f,.5f,.25f,1,.125f,.25f},{-.5f,-.5f,.25f,1,.125f,.625f},
                {.5f,.5f,.25f,1,.625f,.25f},{.5f,-.5f,.25f,1,.625f,.625f}}};
            for(uint32_t vertex=0;vertex<4;++vertex){for(uint32_t lane=0;lane<6;++lane)put(base,vertices+32*vertex+4*lane,corners[vertex][lane]);
                PPC_STORE_U32(vertices+32*vertex+24,dual?std::bit_cast<uint32_t>(.375f):UINT32_MAX);
                PPC_STORE_U32(vertices+32*vertex+28,dual?std::bit_cast<uint32_t>(.4375f):UINT32_MAX);}
            const auto bytes=snapshot(rt,vertices,128);if(billboard){c.r3.u32=vertices;d.billboardOperation(c,base,0x8275F7EC);}
            else{c.r29.u32=vertices+128;d.finishTrailBatch(c,base);}same(rt,vertices,bytes,"Projected boundary changed original CPU vertices");
            const auto output=f.color();if(!pass)first=output;else{uint32_t ratios=0;for(size_t offset=0;offset<output.size();offset+=4){const auto a=pixel(first,offset),b=pixel(output,offset);
                for(uint32_t shift:{0u,10u,20u}){const auto av=(a>>shift)&1023,bv=(b>>shift)&1023;if(av>4&&av<400){
                    need(std::abs(float(bv)-2*float(av))<=1.5f,"Projected boundary lost repeated additive blend");++ratios;}}}
                if(ambient!=0||!projected)need(ratios>100,"Projected boundary had no measurable repeated contribution");}
        }
        c.r3.u32=entry;c.lr=billboard?0x8275F818:0x8277E89C;d.immediateGeometryState(c,base,false);
        need(d.immediateDrawCount()==draws+2&&PPC_LOAD_U32(f.row)==f.ring+256,"Projected boundary draw or reservation count differs");
        same(rt,boundaryArea,input,"Projected boundary changed owner/material inputs");sameDepth(f.depth(),z);
        need(shadows.readback(shadowIdentity)==shadowPixels,"Projected boundary changed sampled shadow resource");c=initial;return first;
    };
    for(bool billboard:{false,true})for(bool dual:{false,true}){
        const auto flat=bounded(billboard,dual,false,-1),negative=bounded(billboard,dual,true,-1),shadowed=bounded(billboard,dual,true,.25f);
        need(negative==flat,"Projected boundary negative predicate differs from original flat shader");uint32_t ratios=0;
        for(size_t i=0;i<flat.size();i+=4){const auto a=pixel(flat,i),b=pixel(shadowed,i);need((a>>30)==(b>>30),"Projected boundary lighting changed alpha");
            for(uint32_t shift:{0u,10u,20u}){const auto av=(a>>shift)&1023,bv=(b>>shift)&1023;if(av>=16&&av<900){
                need(std::abs(float(bv)-float(av)*.25f)<=1.5f,"Projected boundary matrix, c25 or second texture differs");++ratios;}}}
        need(ratios>100,"Projected boundary shadow oracle had no eligible pixels");need(!colored(bounded(billboard,dual,true,0)),"Fully shadowed projected boundary retained RGB");
    }

    f.end();cpu.invoke(0x82701118,table,25);shadows.requireReleased();need(cpu.invoke(0x826B7600,manager,1)==manager,"Original projected manager retirement failed");
}
}
PPC_FUNC(sub_82764608){const auto before=abi(ctx);need(uint32_t(ctx.lr)==0x827641E0||uint32_t(ctx.lr)==0x8276421C||uint32_t(ctx.lr)==0x82766E08,"Original decal quad caller differs");
    ++quadCalls;__imp__sub_82764608(ctx,base);need(abi(ctx)==before,"Original decal quad changed nonvolatile ABI");}
PPC_FUNC(sub_82766D98){const auto before=abi(ctx);need(uint32_t(ctx.lr)==0x827641E0||uint32_t(ctx.lr)==0x8276421C,"Original decal mesh caller differs");
    ++meshCalls;__imp__sub_82766D98(ctx,base);need(abi(ctx)==before,"Original decal mesh changed nonvolatile ABI");}
int main(int argc,char** argv){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try{need(argc==2,"Original image required");exercise(argv[1]);std::printf("PASS original decals: %zu checks; original lists/quad/triangles, transforms/UV/alpha, depth bias, GPU, ABI\n",checks);return 0;}
    catch(const std::exception& error){std::fprintf(stderr,"FAIL original decals: %zu checks stage=%s: %s\n",checks,stage,error.what());return 1;}}
