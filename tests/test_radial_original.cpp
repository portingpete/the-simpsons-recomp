// Whole original flare owner loop, radial CPU vertices and original global retirement.
#include "tests/immediate_original_helpers.h"
#include <limits>
extern "C" void __imp__sub_8276B938(PPCContext&,uint8_t*);
namespace {
constexpr uint32_t area=0x5C000,parent=area,object=area+0x200,context=area+0x400,
    model=area+0x500,data=area+0x700,parentDef=area+0xA00,radialDef=area+0xB00;
ImmediateFixture* activeFixture{};
uint32_t radialCalls{},lastSegments{},lastStaging{};
std::vector<uint8_t> lastVertices;
void definitions(uint8_t* base,uint8_t rawSegments,uint8_t mode,bool reverse){
    PPC_STORE_U8(radialDef+0x10,mode);PPC_STORE_U8(radialDef+0x11,rawSegments);
    // Zero GUID selects the original untextured radial path.
    PPC_STORE_U32(radialDef+0x24,0xFF0000FF);PPC_STORE_U32(radialDef+0x28,0xFF0000FF);
    put(base,radialDef+0x1C,reverse?0.f:1.f);put(base,radialDef+0x20,reverse?1.f:0.f);
    put(base,radialDef+0x2C,0);put(base,radialDef+0x30,.5f);put(base,radialDef+0x34,0);put(base,radialDef+0x38,0);
}
void exercise(ImmediateFixture& f){
    auto* base=f.base;auto& rt=f.rt;auto& cpu=*f.cpu;auto& c=cpu.registers();auto& d=f.driver();seed(c);
    rt.map(area,0x2000,true,"original radial owner/model/definition fixture");std::memset(rt.pointer(area,0x2000,true),0,0x2000);
    Restore cameraGlobal(rt,0x82DFF098,4),view(rt,0x82DFEA20+0x30,0x90);
    PPC_STORE_U32(0x82DFF098,f.camera);
    for(uint32_t i=0;i<4;++i)put(base,0x82DFEA20+0x30+4*i,0);
    matrix(base,0x82DFEA20+0x40,translated());matrix(base,0x82DFEA20+0x80,translated());
    PPC_STORE_U32(context,model);PPC_STORE_U32(model+8,data);put(base,model+0x7C,1);
    put(base,data+0x44,1);put(base,data+0x48,0);put(base,parentDef+0x1C,.125f);
    put(base,parentDef+0x20,1);put(base,parentDef+0x24,1);put(base,parentDef+0x28,1);
    definitions(base,4,1,false);
    stage="original parent and radial constructors";
    need(cpu.invoke(0x827800C8,parent,context,parentDef)==parent,"Original flare parent constructor differs");
    need(cpu.invoke(0x82780608,object,context,radialDef)==object,"Original radial constructor differs");
    need(PPC_LOAD_U32(parent)==0x82158CD4&&PPC_LOAD_U32(object)==0x82158D0C,"Original radial owner vtables differ");
    // Install the valid intrusive-list links consumed by the original caller.
    // The constructors already establish object identity/definition/lifetime.
    const auto sentinel=parent+0x30;PPC_STORE_U32(parent+0xD0,object);PPC_STORE_U32(parent+0xD4,object);PPC_STORE_U32(parent+0xD8,sentinel);
    PPC_STORE_U32(object+0xA0,sentinel);PPC_STORE_U32(object+0xA4,sentinel);
    PPC_STORE_U32(sentinel+0xA0,object);PPC_STORE_U32(sentinel+0xA4,object);
    // The original descriptor preparer projects this finite source position.
    put(base,parent+0x90,.125f);put(base,parent+0x94,-.125f);put(base,parent+0x98,2);put(base,parent+0x9C,1);
    const auto declaration=PPC_LOAD_U32(0x82DFF284),companion=PPC_LOAD_U32(0x82DFF288),queryOwner=PPC_LOAD_U32(0x82DFF28C);
    need(declaration&&companion&&queryOwner,"Original radial startup owners missing");
    using S=Graphics::ScalarState;activeFixture=&f;
    const auto run=[&](uint32_t cull,uint8_t segments,uint8_t mode,bool reverse){
        definitions(base,segments,mode,reverse);f.clear();d.directScalar(base,uint32_t(S::Cull),cull);
        const auto beforeState=d.effectiveState();const auto input=snapshot(rt,radialDef,0x40);const auto beforeAbi=abi(c);
        const auto beforeDraws=d.immediateDrawCount();const auto beforeCalls=radialCalls;const auto beforeDepth=f.depth();lastVertices.clear();
        cpu.invoke(0x827803E0,parent);
        need(abi(c)==beforeAbi,"Original radial owner loop changed nonvolatile ABI");
        need(radialCalls==beforeCalls+1&&d.immediateDrawCount()==beforeDraws+1,"Original owner loop did not submit one radial object");
        const auto expected=std::clamp<uint32_t>(segments,3,32);const auto rounded=mode==2?(expected+1)&~1u:expected;
        need(lastSegments==rounded&&lastVertices.size()==size_t(2*(rounded+1))*12,"Original radial clamp/reservation differs");
        for(uint32_t pair=0;pair<=rounded;++pair){
            const auto angle=get(base,lastStaging+pair*24+8);
            float expectedAngle=pair==rounded?0.f:float(pair)*std::bit_cast<float>(0x40C90FDAu)/float(rounded);
            if(expectedAngle>std::bit_cast<float>(0x40490FDAu))expectedAngle-=std::bit_cast<float>(0x40C90FDAu);
            need(std::isfinite(angle)&&std::abs(angle-expectedAngle)<.00001f,
                 "Original radial closing angle or increment differs");
            for(uint32_t lane=0;lane<2;++lane){const auto at=lastStaging+pair*24+lane*12;
                need(PPC_LOAD_U32(at)==0xFF0000FF,"Original CPU radial packed color differs");
                const float radius=mode==2?(lane?get(base,radialDef+(pair&1?0x20:0x1C)):0.f):get(base,radialDef+0x1C+4*lane);
                need(get(base,at+4)==radius&&get(base,at+8)==angle,"Original CPU radial radius/angle pair differs");}}
        same(rt,radialDef,input,"Original radial draw changed its definition");sameDepth(f.depth(),beforeDepth);
        need(d.effectiveState().scalar(S::Cull)==cull&&d.effectiveState().scalar(S::DepthEnable)==1&&
             d.effectiveState().scalar(S::DepthWrite)==1&&!d.effectiveState().scalar(S::BlendEnable)&&!d.effectiveState().scalar(S::AlphaTest),
             "Original radial cleanup changed inherited culling or retained setup state");
        for(auto id:{S::StencilEnable,S::Fill,S::ClipPlaneEnable,S::ViewportEnable,S::HalfPixelOffset,S::AlphaToMask})
            need(d.effectiveState().scalar(id)==beforeState.scalar(id),"Radial loop changed an inherited state field");
        need(PPC_LOAD_U32(0x82DFF284)==declaration&&PPC_LOAD_U32(0x82DFF288)==companion&&PPC_LOAD_U32(0x82DFF28C)==queryOwner,
             "Radial draw changed original global owner identities");f.lifetime();return f.color();
    };
    stage="original radial winding and inherited culling";
    f.clear();const auto empty=f.color();
    const auto noCull=run(0,4,1,false);need(colored(noCull)>100,"Original radial fixture produced no meaningful coverage");
    const auto cull2=run(2,4,1,false),cull6=run(6,4,1,false);
    need((cull2==noCull&&cull6==empty)||(cull6==noCull&&cull2==empty),"Original radial inherited cull modes failed to preserve/reject winding");
    need(run(2,4,1,true)==cull6&&run(6,4,1,true)==cull2,"Original CPU ring winding did not reverse inherited culling");
    stage="original segment byte domain";
    for(uint8_t n:{uint8_t(0),uint8_t(2),uint8_t(3),uint8_t(31),uint8_t(32),uint8_t(255)})run(0,n,1,false);
    for(uint8_t n:{uint8_t(0),uint8_t(3),uint8_t(31),uint8_t(255)})run(0,n,2,false);
    stage="malformed declaration rejected before submission";
    const auto sourceBefore=PPC_LOAD_U32(0x82153DE8);const auto before=c;const auto beforePixels=f.color(),beforeDepth=f.depth();const auto beforeDraws=d.immediateDrawCount();
    PPC_STORE_U32(0x82153DE8,sourceBefore^1);
    rejects([&]{cpu.invoke(0x827803E0,parent);},"Malformed radial declaration was admitted");c=before;PPC_STORE_U32(0x82153DE8,sourceBefore);
    need(d.immediateDrawCount()==beforeDraws&&f.color()==beforePixels,"Malformed radial declaration changed attachments/draws");sameDepth(f.depth(),beforeDepth);
    need(colored(run(0,4,1,false))>100,"Radial rejection poisoned a later original draw");
    stage="original object and global retirement";activeFixture=nullptr;
    cpu.invoke(0x82780738,object);cpu.invoke(0x827804C8,parent);
    need(!PPC_LOAD_U32(object+0xA0),"Original radial destructor retained its list link");
    cpu.invoke(0x8276C010);need(!PPC_LOAD_U32(0x82DFF284)&&!PPC_LOAD_U32(0x82DFF288)&&!PPC_LOAD_U32(0x82DFF28C),
        "Original radial global release retained a declaration/query owner");
    f.end();
}
}
PPC_FUNC(sub_8276B938){
    const auto before=abi(ctx);const auto objectAddress=ctx.r3.u32;
    if(activeFixture){need(uint32_t(ctx.lr)==0x82780468,"Radial draw bypassed original parent virtual dispatch");++radialCalls;}
    __imp__sub_8276B938(ctx,base);need(abi(ctx)==before,"Original radial body changed nonvolatile ABI");
    if(activeFixture){const auto raw=PPC_LOAD_U8(PPC_LOAD_U32(objectAddress+0xB0)+0x11);lastSegments=std::clamp<uint32_t>(raw,3,32);
        if(PPC_LOAD_U8(PPC_LOAD_U32(objectAddress+0xB0)+0x10)==2)lastSegments=(lastSegments+1)&~1u;
        const auto bytes=2*(lastSegments+1)*12;need(ctx.r3.u32>=bytes,"Original radial CPU staging end wraps");
        lastStaging=ctx.r3.u32-bytes;lastVertices=snapshot(activeFixture->rt,lastStaging,bytes);}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try{need(argc==2,"Original image required");ImmediateFixture f(argv[1]);exercise(f);
        std::printf("PASS original radial owner loop: %zu checks; original constructors, setup/virtual draw/cleanup, CPU clamp/vertices, inherited culling, pixels and global release\n",checks);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original radial owner loop: %zu checks stage=%s: %s\n",checks,stage,error.what());return 1;}
}
