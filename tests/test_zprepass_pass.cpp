// Whole original8273B4D0 ->82740680, real catalog/camera, native static depth.
// Test entry observers always forward to the complete generated function.
#include "effect_catalog_lifecycle_helpers.h"
#include "renderer/engine_state.h"
#include <bit>
#include <functional>

extern "C" void __imp__sub_826FF6D8(PPCContext&,uint8_t*);
extern "C" void __imp__sub_82704600(PPCContext&,uint8_t*);
extern "C" void __imp__sub_823C8EB0(PPCContext&,uint8_t*);
extern "C" void __imp__sub_826B3980(PPCContext&,uint8_t*);
extern "C" void __imp__sub_826FE668(PPCContext&,uint8_t*);

namespace {
std::function<void(uint32_t,PPCContext&,uint8_t*,bool)> entryObserver;
struct Observation {
    explicit Observation(decltype(entryObserver) f){need(!entryObserver,"Nested zprepass observer");entryObserver=std::move(f);}
    ~Observation(){entryObserver={};}
};
void forward(uint32_t pc,PPCContext& c,uint8_t* base,void (*body)(PPCContext&,uint8_t*)) {
    if(entryObserver)entryObserver(pc,c,base,false);
    body(c,base);
    if(entryObserver)entryObserver(pc,c,base,true);
}
struct Restore {
    uint8_t* destination;std::vector<uint8_t> bytes;
    Restore(Runtime& rt,uint32_t address,uint32_t size):destination(rt.pointer(address,size,true)),bytes(destination,destination+size){}
    ~Restore(){std::memcpy(destination,bytes.data(),bytes.size());}
};
struct FullAbi {
    SavedAbi integer;std::array<uint64_t,18> floating;
    bool operator==(const FullAbi&) const=default;
};
FullAbi fullAbi(const PPCContext& c) {
    return {abi(c),{c.f14.u64,c.f15.u64,c.f16.u64,c.f17.u64,c.f18.u64,c.f19.u64,c.f20.u64,c.f21.u64,c.f22.u64,
        c.f23.u64,c.f24.u64,c.f25.u64,c.f26.u64,c.f27.u64,c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64}};
}
void seedAbi(PPCContext& c) {
    const std::array gpr={&c.r14,&c.r15,&c.r16,&c.r17,&c.r18,&c.r19,&c.r20,&c.r21,&c.r22,
        &c.r23,&c.r24,&c.r25,&c.r26,&c.r27,&c.r28,&c.r29,&c.r30,&c.r31};
    const std::array fpr={&c.f14,&c.f15,&c.f16,&c.f17,&c.f18,&c.f19,&c.f20,&c.f21,&c.f22,
        &c.f23,&c.f24,&c.f25,&c.f26,&c.f27,&c.f28,&c.f29,&c.f30,&c.f31};
    for(size_t i=0;i<gpr.size();++i){gpr[i]->u64=0xA510000012340000ull+i;fpr[i]->f64=double(i)+0.125;}
}
std::vector<uint32_t> effective(EngineDriver& driver) {
    std::vector<uint32_t> result;
    for(const auto& f:Graphics::scalarStateEvidence())result.push_back(driver.effectiveState().scalar(f.id));
    for(uint32_t s=0;s<16;++s)for(const auto& f:Graphics::samplerStateEvidence())result.push_back(driver.effectiveState().sampler(s,f.id));
    return result;
}
std::array<uint32_t,11> binding(EngineDriver& driver) {
    const auto c=driver.cameraBinding();return {c.camera,c.colorRaster,c.depthRaster,c.colorIdentity,c.depthIdentity,
        c.viewport[0],c.viewport[1],c.viewport[2],c.viewport[3],c.viewport[4],c.viewport[5]};
}
std::array<uint8_t,128> dirty(Runtime& rt,uint32_t address) {
    std::array<uint8_t,128> value{};std::memcpy(value.data(),rt.pointer(address,128,false),128);return value;
}
void sameDepth(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b,const char* why) {
    need(a.size()==size_t(1280)*720*8&&a.size()==b.size(),"Zprepass depth extent differs");
    bool match=true;for(size_t i=0;i<a.size();i+=8)match&=!std::memcmp(a.data()+i,b.data()+i,5);
    need(match,why); // X24 padding is not a depth/stencil component.
}
float pixel(const std::vector<uint8_t>& bytes,uint32_t x,uint32_t y) {
    float value{};std::memcpy(&value,bytes.data()+8*(1280*y+x),4);return value;
}
using Matrix=std::array<float,16>;
Matrix diagonal(float x,float y,float z,float tx=0,float ty=0,float tz=0) {
    return {x,0,0,0,0,y,0,0,0,0,z,0,tx,ty,tz,1};
}
void writeMatrix(uint8_t* base,uint32_t address,const Matrix& m) {
    for(uint32_t i=0;i<16;++i)PPC_STORE_U32(address+4*i,std::bit_cast<uint32_t>(m[i]));
}
constexpr uint32_t area=0x60000,packet=0x60000,metadata=0x60100,object=0x61000,data=0x63000,
    offsets=0x64000,geometry=0x65000,elements=0x65100,vertices=0x65200,indices=0x65400,
    submeshes=0x65500,declCache=0x65600,materials=0x65700,objectFrame=0x65800;
void geometryFixture(Runtime& rt,uint32_t camera) {
    auto* base=rt.base;rt.map(area,0x10000,true,"original zprepass CPU geometry fixture");
    std::memset(rt.pointer(area,0x10000,true),0,0x10000);
    PPC_STORE_U32(packet,metadata);PPC_STORE_U32(packet+4,object);PPC_STORE_U32(packet+8,camera);PPC_STORE_U8(packet+12,1);
    PPC_STORE_U32(object,0x01000500);PPC_STORE_U32(object+4,objectFrame);
    PPC_STORE_U32(object+8,object+8);PPC_STORE_U32(object+12,object+8);
    PPC_STORE_U32(object+16,0x823CD3D8);PPC_STORE_U32(object+24,data);PPC_STORE_U32(data+0x24,offsets);
    // These are actual registered plugin offsets, with absent auxiliary data.
    need(PPC_LOAD_U32(0x82CF05EC)<0x1000&&PPC_LOAD_U32(0x82D6CAB0)<0x1000&&
         PPC_LOAD_U32(0x82D6CAB0)!=0x24,"Fixture plugin offsets overlap material offsets or exceed storage");
    PPC_STORE_U32(objectFrame+0xA0,objectFrame);writeMatrix(base,objectFrame+0x10,diagonal(.5f,.75f,.5f,.5f,.25f,.125f));
    PPC_STORE_U32(metadata,0x00030002);PPC_STORE_U32(metadata+4,0xB5F8FBF2);
    PPC_STORE_U32(metadata+12,geometry);PPC_STORE_U32(metadata+16,2);PPC_STORE_U32(metadata+20,submeshes);PPC_STORE_U32(metadata+24,1);
    // metadata+24h/+28h are zero: the entire original dispatcher chooses static.
    PPC_STORE_U32(geometry,8*36);PPC_STORE_U32(geometry+4,36);PPC_STORE_U32(geometry+8,6);
    PPC_STORE_U32(geometry+12,elements);PPC_STORE_U32(geometry+16,vertices);PPC_STORE_U32(geometry+20,16);
    PPC_STORE_U32(geometry+24,1);PPC_STORE_U32(geometry+28,indices);PPC_STORE_U32(geometry+0x30,declCache);
    PPC_STORE_U32(geometry+0x50,vertices|3);PPC_STORE_U32(geometry+0x54,2);PPC_STORE_U32(geometry+0x58,0x20000002);
    PPC_STORE_U32(geometry+0x70,indices);PPC_STORE_U32(geometry+0x74,16);PPC_STORE_U32(declCache+4,declCache+0x40);
    constexpr uint32_t declaration[][3]={{0,0x002A23B9,0},{12,0x002A2187,0x00030000},{16,0x00182886,0x000A0000},
        {20,0x002C23A5,0x00050000},{28,0x002C23A5,0x00050100},{0x00FF0000,UINT32_MAX,0}};
    for(uint32_t row=0;row<6;++row)for(uint32_t lane=0;lane<3;++lane)PPC_STORE_U32(elements+12*row+4*lane,declaration[row][lane]);
    // Two distinct strips with the original cull2 front winding. Material2
    // addresses the left strip, material7 the right; submesh start indices differ.
    for(uint32_t q=0;q<2;++q)for(uint32_t i=0;i<4;++i) {
        const uint32_t v=vertices+36*(4*q+i);const float x=float(q)-1+float(i/2),y=i%2?-1.0f:1.0f;
        PPC_STORE_U32(v,std::bit_cast<uint32_t>(x));PPC_STORE_U32(v+4,std::bit_cast<uint32_t>(y));PPC_STORE_U32(v+8,0x3F000000);
        PPC_STORE_U32(v+12,0x055C7E3D);PPC_STORE_U32(v+16,0xE5000064);
        for(uint32_t j=0;j<4;++j)PPC_STORE_U32(v+20+4*j,std::bit_cast<uint32_t>(float(j)+.125f));
        PPC_STORE_U16(indices+2*(4*q+i),uint16_t(4*q+i));
    }
    for(uint32_t q=0;q<2;++q) {
        PPC_STORE_U32(submeshes+36*q,q?7:2);PPC_STORE_U32(submeshes+36*q+12,6);
        PPC_STORE_U32(submeshes+36*q+20,4*q);PPC_STORE_U32(submeshes+36*q+24,4);
    }
    PPC_STORE_U32(offsets+8,0x30);PPC_STORE_U32(offsets+28,0x70);
    PPC_STORE_U16(materials+0x30,0x20);PPC_STORE_U16(materials+0x70,4);
}
void run(Runtime& rt,const PPCContext& entry) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();EngineCpuCalls cpu(entry,base);
    stage="real catalog construction";
    const auto context=PPC_LOAD_U32(0x82D5DA74),loading=PPC_LOAD_U32(0x82E07248);driver.requireContext(context);
    const auto options=cpu.registers().r1.u32+0x60;PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    const auto manager=cpu.invoke(0x8269BF70,0x260,options);cpu.invoke(0x826B6F60,manager,context);
    need(cpu.invoke(0x827019E8,table,25)==0,"Original catalog registration failed");cpu.invoke(0x826B7218,manager);
    uint32_t owner=0;
    for(uint32_t row=0;row<25;++row)if(PPC_LOAD_U32(table+16*row)==0x821490E0)
        owner=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(table+16*row+4));
    need(owner&&PPC_LOAD_U32(owner)==0x8215022C,"Catalog did not construct original zprepass typed owner");
    const auto id=PPC_LOAD_U32(owner+0x1C),wrapper=PPC_LOAD_U32(owner+0x18);const auto v=effects.view(id);
    need(v.source==0x821490E0&&v.manager==manager&&v.wrapper==wrapper&&v.defaultVectorWords.size()==1100&&
         PPC_LOAD_U32(owner+0xAC)==0x0003FFFC&&PPC_LOAD_U32(owner+0xB8)==0x00300014&&
         PPC_LOAD_U32(owner+0xC0)==0x00040001,"Original zprepass reflection differs");
    const auto source=snapshot(rt,0x821490E0,0x5330);cache(rt,cpu,id,0x821490EC);queries(rt,cpu,id,0x821490EC);
    stage="real main camera construction";
    const auto viewport=cpu.invoke(0x8269D788);need(viewport,"Missing original viewport manager");
    cpu.registers().f1.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12CC));
    cpu.registers().f2.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D0));
    cpu.registers().f3.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D4));
    cpu.registers().f4.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D8));
    cpu.invoke(0x8269D608,viewport,0);const auto camera=PPC_LOAD_U32(viewport+0x14),cameraFrame=PPC_LOAD_U32(camera+4);
    need(camera&&PPC_LOAD_U32(cameraFrame+0xA0)==cameraFrame,"Main camera has no genuine root frame");
    geometryFixture(rt,camera);
    {
        Restore flags(rt,0x82D6CCA8,4),zowner(rt,0x82D6D8A0,4),nativeContext(rt,0x82D6D890,4),published(rt,0x82D63028,4),
            materialRoot(rt,0x82D6D814,4),stencil(rt,0x82D0CB14,4),dirtyGlobal(rt,0x82D00F80,4),
            viewMatrix(rt,0x82D0CA70,64),projection(rt,0x82CD1AB0,64),cameraMatrix(rt,cameraFrame+0x10,64);
        PPC_STORE_U32(0x82D6CCA8,0x40);PPC_STORE_U32(0x82D6D8A0,owner);PPC_STORE_U32(0x82D6D890,context);
        PPC_STORE_U32(0x82D63028,0);PPC_STORE_U32(0x82D6D814,materials);PPC_STORE_U32(0x82D00F80,0);
        PPC_STORE_U32(area+0x80,0xFF00FFFF);PPC_STORE_U32(0x82D0CB14,0x5A);
        need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original main camera clear failed");
        need(cpu.invoke(0x823F1A18,camera)==camera,"Original main camera begin failed");
        const auto bound=driver.cameraBinding();need(bound.viewport==std::array<uint32_t,6>{0,0,1280,720,0x3F800000,0},"Main viewport differs");
        writeMatrix(base,cameraFrame+0x10,diagonal(1,1,1,.25f,.125f,.0625f));
        // View translation is deliberately nonzero:8270BF08 replaces it and
        // instead subtracts the actual camera frame position from world position.
        writeMatrix(base,0x82D0CA70,diagonal(.5f,1,2,15,20,-7));writeMatrix(base,0x82CD1AB0,diagonal(1,.5f,.5f));
        const auto combined=diagonal(.25f,.375f,.5f,.125f,.0625f,.0625f);
        constexpr std::array<std::array<uint32_t,2>,21> inherited={{{0x28,1},{0x2C,6},{0x30,1},{0x34,0},{0x60,0},{0x6C,0},
            {0xAC,0},{0xC0,1},{0xC4,UINT32_MAX},{0xC8,0},{0xCC,0},{0xD0,0},{0xD4,0},{0xE4,0},{0x130,1},
            {0x144,1},{0x148,1},{0x14C,0xFFFF},{0x150,0},{0xD8,0},{0xDC,0}}};
        for(const auto& row:inherited)driver.directScalar(base,row[0],row[1]);
        const auto cullSelector=PPC_LOAD_U32(0x82E06F80+0x38);cpu.invoke(0x826B7968,cullSelector,1,1);
        const auto priorCull=cpu.invoke(0x826B7940,cullSelector),priorNativeCull=driver.effectiveState().scalar(Graphics::ScalarState::Cull);
        const auto colorBefore=driver.readbackColor(bound.colorIdentity),initialDepth=driver.readbackDepth(bound.depthIdentity);
        const auto resolvedColor=PPC_LOAD_U32(0x82D0CF90),resolvedDepth=PPC_LOAD_U32(0x82D0CF84);
        const auto otherColor=driver.readbackColor(resolvedColor),otherDepth=driver.readbackDepth(resolvedDepth);
        const auto bindingBefore=binding(driver);const auto clears=driver.cameraClearCount(),copies=driver.cameraCopyCount();
        const auto shared=effects.sharedParameterStorage(id,0x00040001);const auto originalPrivate=effects.view(id).defaultVectorWords;
        std::array<uint32_t,976> expectedGpu{};
        for(uint32_t r=0;r<4;++r)for(uint32_t col=0;col<4;++col)
            expectedGpu[4*r+col]=std::bit_cast<uint32_t>(combined[4*col+r]);
        for(uint32_t i=0;i<8;++i)expectedGpu[4*36+i]=originalPrivate[68+i];
        for(uint32_t bone=0;bone<64;++bone)for(uint32_t r=0;r<3;++r)for(uint32_t col=0;col<4;++col)
            expectedGpu[4*(52+3*bone+r)+col]=originalPrivate[76+16*bone+4*r+col];
        const auto inputGeometry=snapshot(rt,geometry,0x700),frameBefore=snapshot(rt,objectFrame,0xB0);
        auto invoke=[&] {
            EngineCpuCalls call(entry,base);seedAbi(call.registers());const auto before=fullAbi(call.registers());
            need(call.invoke(0x8273B4D0,packet)==1,"Original wrapper did not return successful dispatcher Boolean");
            need(fullAbi(call.registers())==before,"Whole zprepass wrapper changed nonvolatile GPR/FPR,SP or LR");
        };
        auto rejectedBegin=[&](uint32_t address,uint32_t replacement,const char* diagnostic,bool publishesContext) {
            Restore restore(rt,address,4);PPC_STORE_U32(address,replacement);
            const auto ownerBytes=snapshot(rt,owner,0xC8),wrapperBytes=snapshot(rt,wrapper,0x30),managerBytes=snapshot(rt,manager,0x18),
                cacheBytes=snapshot(rt,v.cache,v.cacheBytes),poolBytes=snapshot(rt,v.pool,512),values=snapshot(rt,shared,64);
            const auto beforePrivate=effects.view(id).defaultVectorWords;const auto beforeDirty=effects.privateModifiedMask(id);
            const auto stateBefore=effective(driver);const auto draws=effects.zprepassDrawCount();
            const auto compiled=effects.compiledShaderCount(id);const auto contextBefore=PPC_LOAD_U32(0x82D63028);
            uint32_t contextStores=0;bool rejected=false;std::string caught="<no failure>";
            {
                Observation observation([&](uint32_t pc,PPCContext&,uint8_t*,bool after) {
                    if(pc==0x826FF6D8&&after)++contextStores;
                });
                try{invoke();}catch(const Failure& error){caught=error.what();rejected=caught.find(diagnostic)!=std::string::npos;}
            }
            std::fprintf(stderr,"[TEST ZPREPASS REJECTION] field=%08X value=%08X expected=\"%s\" actual=\"%s\" context_stores=%u\n",
                address,replacement,diagnostic,caught.c_str(),contextStores);
            need(rejected,"Original dispatcher rejected for an unexpected reason or accepted invalid activation");
            same(rt,owner,ownerBytes,"Rejected activation changed typed owner");same(rt,wrapper,wrapperBytes,"Rejected activation changed wrapper");
            same(rt,manager,managerBytes,"Rejected activation changed manager");same(rt,v.cache,cacheBytes,"Rejected activation changed saved cache");
            same(rt,v.pool,poolBytes,"Rejected activation changed shared pool");same(rt,shared,values,"Rejected activation changed shared constants");
            need(effects.view(id).defaultVectorWords==beforePrivate&&effects.privateModifiedMask(id)==beforeDirty&&
                 effects.zprepassDrawCount()==draws&&effects.compiledShaderCount(id)==compiled&&effective(driver)==stateBefore,
                 "Rejected activation changed parameters,dirty,state,shader compilation or draw count");
            need(contextStores==uint32_t(publishesContext)&&PPC_LOAD_U32(0x82D63028)==(publishesContext?context:contextBefore)&&
                 !PPC_LOAD_U32(0x82D0CAF8)&&PPC_LOAD_U32(0x82D6CCA8)==0x40,
                 "Rejection performed the wrong original context-store closure or changed flags/device");
            sameDepth(driver.readbackDepth(bound.depthIdentity),initialDepth,"Rejected activation wrote depth/stencil");
            need(driver.readbackColor(bound.colorIdentity)==colorBefore,"Rejected activation wrote color");
        };
        stage="activation rejects before effect mutation";
        rejectedBegin(metadata+0x24,1,"Skinned zprepass geometry is not yet qualified",true);
        // The dispatcher checks this owner field before its prologue/context
        // setter. Preserve the prior publication; do not infer a new store.
        rejectedBegin(owner+0xAC,0x0007FFFC,"Original depth prepass lost its reflected owner",false);
        rejectedBegin(packet+8,loading,"Original zprepass camera differs",true);
        auto pass=[&](bool fresh,bool reseed,uint64_t expectedDraws) {
            const auto draws=effects.zprepassDrawCount();const auto privateBefore=effects.privateModifiedMask(id);const auto sharedBefore=dirty(rt,v.pool);
            auto expectedPrivate=privateBefore,expectedShared=sharedBefore;
            if(fresh){expectedPrivate.fill(0);std::fill_n(expectedPrivate.begin(),16,uint8_t(0xFF));}
            if(fresh&&reseed){expectedShared.fill(0);std::fill_n(expectedShared.begin(),16,uint8_t(0xFF));}
            std::array<uint32_t,5> beforeCalls{},afterCalls{};
            const auto cacheBefore=snapshot(rt,v.cache,v.cacheBytes);
            Observation observation([&](uint32_t pc,PPCContext& c,uint8_t* memory,bool after) {
                need(memory==base,"Observer changed original memory domain");
                const uint32_t index=pc==0x826FF6D8?0:pc==0x82704600?1:pc==0x823C8EB0?2:pc==0x826B3980?3:4;
                ++(after?afterCalls:beforeCalls)[index];
                if(pc==0x826FF6D8) {
                    need(c.r31.u32==packet&&uint32_t(c.lr)==0x827406A0&&PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0xF0,
                         "Original dispatcher context setter frame differs");
                    if(after)need(!c.r3.u32&&PPC_LOAD_U32(0x82D63028)==context,"Original context setter did not publish native identity");
                    else need(c.r3.u32==context&&!PPC_LOAD_U32(0x82D0CAF8),"Context adapter fabricated a legacy SDK device");
                } else if(pc==0x82704600) {
                    need(c.r3.u32==id&&c.r4.u32==0x00040001&&c.r5.u32==c.r1.u32+0x90&&uint32_t(c.lr)==0x8270C270,
                         "Original combined-matrix setter arguments differ");
                    for(uint32_t i=0;i<16;++i)need(PPC_LOAD_U32(c.r5.u32+4*i)==std::bit_cast<uint32_t>(combined[i]),
                        "Original CPU world/camera/view/projection composition differs from independent matrix product");
                    if(after){expectedShared[0]|=0x80;for(uint32_t r=0;r<4;++r)for(uint32_t col=0;col<4;++col)
                        need(PPC_LOAD_U32(shared+16*r+4*col)==std::bit_cast<uint32_t>(combined[4*col+r]),"Shared matrix setter transpose differs");}
                    need(dirty(rt,v.pool)==expectedShared&&effects.privateModifiedMask(id)==expectedPrivate,"Matrix/begin dirty predicate,extent or exact mask differs");
                } else if(pc==0x823C8EB0) {
                    need(c.r3.u32==wrapper&&c.r4.u32==0x00300014&&!c.r5.u32&&uint32_t(c.lr)==0x82740818,
                         "Original static Boolean setter arguments differ");
                    if(after)expectedPrivate[1]|=0x20;
                    need(effects.view(id).defaultVectorWords==originalPrivate&&effects.privateModifiedMask(id)==expectedPrivate,
                         "Static Boolean changed other lanes/defaults or failed its exact dirty bit");
                } else if(pc==0x826B3980) {
                    need(c.r3.u32==wrapper&&uint32_t(c.lr)==0x82740820,"Original static commit caller differs");
                    if(after){
                        expectedPrivate.fill(0);expectedShared.fill(0);
                        need(effects.readbackZPrepassConstants()==expectedGpu,"Actual VS float buffer differs from CPU matrix/default mapping");
                        need(effects.readbackZPrepassBooleans()==std::array<uint32_t,4>{},"Actual VS Boolean buffer did not select unskinned static input");
                    }
                    need(effects.privateModifiedMask(id)==expectedPrivate&&dirty(rt,v.pool)==expectedShared,
                         "Original commit failed to consume private/shared dirty storage");
                    need(PPC_LOAD_U32(manager+4)==wrapper&&PPC_LOAD_U32(manager+8)==id&&PPC_LOAD_U32(manager+12)==0x0003FFFC&&
                         PPC_LOAD_U32(wrapper+0x2C)==v.cache,"Original manager/cache selected a different technique");
                } else if(!after) {
                    // Reached only after original826FF5B0. The native cleanup
                    // reads back slot1 and rejects unless buffer/stride/offset=0.
                    need(c.r3.u32==object&&uint32_t(c.lr)==0x826FF5BC&&c.r4.u32==1&&!c.r5.u32&&!c.r6.u32&&!c.r7.u32&&c.r8.u32==1,
                         "Original stream1 cleanup arguments/order differ");
                    need(effects.zprepassDrawCount()==draws+expectedDraws,"Original material walk drew the wrong submesh count");
                }
            });
            invoke();
            need(beforeCalls==std::array<uint32_t,5>{1,1,1,1,1}&&afterCalls==beforeCalls,"Whole original path omitted or repeated a setter/commit/cleanup");
            need(effects.zprepassDrawCount()==draws+expectedDraws,"Whole original pass has the wrong actual draw count");
            if(!fresh)same(rt,v.cache,cacheBefore,"Repeated active begin overwrote original saved cache values");
            need(PPC_LOAD_U32(v.cache+56)==priorCull,"Original cull row did not save its application value");
            need(driver.effectiveState().scalar(Graphics::ScalarState::Cull)==2,"Pass did not apply its exact cull literal");
            for(const auto& row:inherited)need(driver.effectiveState().scalar(row[0])==row[1],"Zprepass altered inherited scalar state");
        };
        stage="original right submesh draw and material20 skip";pass(true,true,1);
        auto depth=driver.readbackDepth(bound.depthIdentity);
        for(uint32_t y:{280u,410u}){need(pixel(depth,800,y)==.6875f,"Right strip did not write independently transformed reverse depth");
            need(pixel(depth,640,y)==0,"Original material20 skip still drew the left strip");}
        need(pixel(depth,500,300)==0&&pixel(depth,950,300)==0&&pixel(depth,800,150)==0,"Zprepass drew outside transformed geometry");
        stage="active cache hit, isolated dirty bits and left submesh draw";
        PPC_STORE_U16(materials+0x30,4);PPC_STORE_U16(materials+0x70,0x20);pass(false,false,1);
        depth=driver.readbackDepth(bound.depthIdentity);
        for(uint32_t y:{280u,410u})for(uint32_t x:{640u,800u})need(pixel(depth,x,y)==.6875f,"Material swap did not retain right depth and render left strip");
        auto end=[&] {
            const auto parameters=effects.view(id).defaultVectorWords;
            EngineCpuCalls call(entry,base);seedAbi(call.registers());const auto saved=fullAbi(call.registers());call.invoke(0x826B4B18,wrapper);
            need(fullAbi(call.registers())==saved&&!PPC_LOAD_U32(manager+4)&&PPC_LOAD_U32(manager+8)==id&&!PPC_LOAD_U32(manager+12)&&
                 !PPC_LOAD_U32(wrapper+0x2C),"Original end omitted stream cleanup/selection restoration or changed ABI");
            need(cpu.invoke(0x826B7940,cullSelector)==priorCull&&driver.effectiveState().scalar(Graphics::ScalarState::Cull)==priorNativeCull,
                 "Original cull restoration differs from its saved application value");
            need(effects.view(id).defaultVectorWords==parameters&&effects.privateModifiedMask(id)==std::array<uint8_t,128>{}&&dirty(rt,v.pool)==std::array<uint8_t,128>{},
                 "Original end changed constants or dirtiness");
            for(const auto& row:inherited)need(driver.effectiveState().scalar(row[0])==row[1],"End changed inherited state");
        };
        end();
        stage="fresh begin preserves attached shared dirtiness when global nonzero";
        PPC_STORE_U32(0x82D00F80,1);auto* poolDirty=rt.pointer(v.pool,128,true);std::memset(poolDirty,0,128);poolDirty[9]=0x42;poolDirty[127]=0x80;
        PPC_STORE_U16(materials+0x30,0x24);PPC_STORE_U16(materials+0x70,0x20);pass(true,false,0);end();
        sameDepth(driver.readbackDepth(bound.depthIdentity),depth,"All-skipped pass or end changed depth/stencil");
        bool stencilMatches=true;for(size_t i=0;i<depth.size();i+=8)stencilMatches&=depth[i+4]==0x5A;
        need(stencilMatches&&driver.readbackColor(bound.colorIdentity)==colorBefore,"Depth-only pass changed stencil or color");
        sameDepth(driver.readbackDepth(resolvedDepth),otherDepth,"Zprepass wrote the separate resolved depth copy");
        need(driver.readbackColor(resolvedColor)==otherColor&&binding(driver)==bindingBefore&&driver.cameraClearCount()==clears&&driver.cameraCopyCount()==copies,
             "Zprepass changed another target,camera ownership,clear or copy count");
        same(rt,geometry,inputGeometry,"Original geometry/buffer/declaration inputs changed");same(rt,objectFrame,frameBefore,"Original object frame changed");
        need(!PPC_LOAD_U32(0x82D0CAF8)&&PPC_LOAD_U32(0x82D6CCA8)==0x40,"Original dispatcher changed legacy device or branch flags");
        stage="completed zprepass original mode-zero reset and logical end";
        PPC_STORE_U16(materials+0x30,4);PPC_STORE_U16(materials+0x70,0x20);pass(true,false,1);
        const auto resetTyped=snapshot(rt,owner,0xC8),resetWrapper=snapshot(rt,wrapper,0x30),resetManager=snapshot(rt,manager,0x18);
        const auto resetCache=snapshot(rt,v.cache,v.cacheBytes),resetPool=snapshot(rt,v.pool,512);
        const auto resetParameters=effects.view(id).defaultVectorWords;const auto resetDirty=effects.privateModifiedMask(id);
        const auto resetColor=driver.readbackColor(bound.colorIdentity),resetDepth=driver.readbackDepth(bound.depthIdentity);
        const auto resetCount=driver.bindingResetCount(),resetDraws=effects.zprepassDrawCount(),resetClears=driver.cameraClearCount();
        need(PPC_LOAD_U32(0x823EFDA0)==0x7D8802A6&&PPC_LOAD_U32(0x823F46EC)==0x4BFFB6B4&&
             PPC_LOAD_U32(0x826B4644)==0x2B0B0000,"Original mode-zero reset or manager-end instruction changed");
        {EngineCpuCalls reset(entry,base);seedAbi(reset.registers());const auto saved=fullAbi(reset.registers());
            reset.invoke(0x823EFDA0);need(fullAbi(reset.registers())==saved,"Zprepass binding reset changed nonvolatile ABI");}
        need(driver.bindingResetCount()==resetCount+1&&effects.zprepassDrawCount()==resetDraws&&driver.cameraClearCount()==resetClears,
             "Zprepass reset drew or cleared instead of resetting bindings");
        for(uint32_t address:{0x82CD1A68u,0x82CD1A6Cu,0x82CD1A70u,0x82CD1A74u})
            need(!PPC_LOAD_U32(address),"Zprepass original reset retained a shader/declaration/index cache");
        same(rt,owner,resetTyped,"Zprepass reset changed typed owner");same(rt,wrapper,resetWrapper,"Zprepass reset ended its wrapper");
        same(rt,manager,resetManager,"Zprepass reset ended its manager");same(rt,v.cache,resetCache,"Zprepass reset changed saved cache rows");
        same(rt,v.pool,resetPool,"Zprepass reset changed shared data or dirty storage");
        need(effects.view(id).defaultVectorWords==resetParameters&&effects.privateModifiedMask(id)==resetDirty,
             "Zprepass reset changed parameters or dirty flags");
        sameDepth(driver.readbackDepth(bound.depthIdentity),resetDepth,"Zprepass reset changed rendered depth/stencil");
        need(driver.readbackColor(bound.colorIdentity)==resetColor,"Zprepass reset changed rendered color");
        {Restore restore(rt,manager+8,4);PPC_STORE_U32(manager+8,id^1u);bool rejected=false;
            try{EngineCpuCalls invalid(entry,base);invalid.invoke(0x826B4628,manager);}catch(const Failure&){rejected=true;}
            need(rejected&&PPC_LOAD_U32(wrapper+0x2C)==v.cache,"Zprepass reset receipt admitted another logical effect identity");}
        {EngineCpuCalls finish(entry,base);seedAbi(finish.registers());const auto saved=fullAbi(finish.registers());
            finish.invoke(0x826B4628,manager);need(fullAbi(finish.registers())==saved,"Zprepass end after reset changed nonvolatile ABI");}
        need(!PPC_LOAD_U32(manager+4)&&PPC_LOAD_U32(manager+8)==id&&!PPC_LOAD_U32(manager+12)&&!PPC_LOAD_U32(wrapper+0x2C),
             "Original zprepass end after reset retained logical selection");
        {const auto after=snapshot(rt,manager,0x18);EngineCpuCalls noop(entry,base);seedAbi(noop.registers());const auto saved=fullAbi(noop.registers());
            noop.invoke(0x826B4628,manager);need(fullAbi(noop.registers())==saved,"Ended zprepass manager no-op changed ABI");
            same(rt,manager,after,"Ended zprepass manager no-op changed retained association");}
        need(effects.zprepassDrawCount()==resetDraws&&driver.cameraClearCount()==resetClears&&effects.view(id).defaultVectorWords==resetParameters&&
             effects.privateModifiedMask(id)==resetDirty&&driver.readbackColor(bound.colorIdentity)==resetColor,
             "Zprepass end after reset drew,cleared or changed parameters/dirty/color");
        sameDepth(driver.readbackDepth(bound.depthIdentity),resetDepth,"Zprepass end after reset changed rendered depth/stencil");
        need(cpu.invoke(0x823F1A08,camera)==camera,"Original main camera end failed");
    }
    stage="paired original retirement";
    cpu.invoke(0x823EE6C8,loading);cpu.invoke(0x8269CC28);cpu.invoke(0x82701118,table,25);cpu.invoke(0x826B7600,manager,1);
    need(!effects.count(),"Original catalog retirement retained zprepass owners");same(rt,0x821490E0,source,"Test changed original effect source");
}
}
PPC_FUNC(sub_826FF6D8){forward(0x826FF6D8,ctx,base,__imp__sub_826FF6D8);}
PPC_FUNC(sub_82704600){forward(0x82704600,ctx,base,__imp__sub_82704600);}
PPC_FUNC(sub_823C8EB0){forward(0x823C8EB0,ctx,base,__imp__sub_823C8EB0);}
PPC_FUNC(sub_826B3980){forward(0x826B3980,ctx,base,__imp__sub_826B3980);}
PPC_FUNC(sub_826FE668){forward(0x826FE668,ctx,base,__imp__sub_826FE668);}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==2,"Original image required");Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original startup boundary missing");run(rt,entry);
        std::printf("PASS original zprepass:%zu checks; catalog,camera,whole dispatcher,matrix,Boolean,dirty,state,material skip,depth,mode-zero reset/logical end,cleanup,ABI,rejection,retirement\n",checks);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original zprepass:%zu checks stage=%s:%s\n",checks,stage,error.what());return 1;}
}
