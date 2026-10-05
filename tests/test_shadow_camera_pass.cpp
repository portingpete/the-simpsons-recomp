// Real catalog constructor establishes both shadow cameras. Exercise its actual
// RenderWare camera calls, character/static meshes, deferred-alpha queue and copies.
#include "effect_catalog_lifecycle_helpers.h"
#include "runtime/engine_audio.h"
#include "renderer/engine_state.h"
#include <bit>
#include <functional>

extern "C" void __imp__sub_82706130(PPCContext& ctx,uint8_t* base);
extern "C" void __imp__sub_826FE7C8(PPCContext&,uint8_t*);
extern "C" void __imp__sub_826FE710(PPCContext&,uint8_t*);
extern "C" void __imp__sub_826FDBE0(PPCContext&,uint8_t*);

namespace {
bool staticTangent=false;
std::vector<uint32_t> tangentDeclarations;
// The parent clears T+AC before collecting casters. Inject a nonempty count
// only at the subsequent alpha entry, then execute its entire original body.
// The strong test-only entry forwards to the generated weak alias's body.
std::function<void(PPCContext&,uint8_t*)> alphaEntryObserver;
std::function<void(uint32_t,PPCContext&,bool)> paletteObserver;
void paletteForward(uint32_t pc,PPCContext& c,uint8_t* base,void(*body)(PPCContext&,uint8_t*)) {
    if(paletteObserver)paletteObserver(pc,c,false);body(c,base);if(paletteObserver)paletteObserver(pc,c,true);
}
struct PaletteObservation {
    explicit PaletteObservation(decltype(paletteObserver) value){need(!paletteObserver,"Nested shadow palette observer");paletteObserver=std::move(value);}
    ~PaletteObservation(){paletteObserver={};}
};
struct AlphaEntryObservation {
    explicit AlphaEntryObservation(const std::function<void(PPCContext&,uint8_t*)>& observer) {
        need(!alphaEntryObserver,"Nested alpha entry observation");alphaEntryObserver=observer;
    }
    ~AlphaEntryObservation(){alphaEntryObserver={};}
};
std::vector<uint32_t> effectiveWords(EngineDriver& driver) {
    std::vector<uint32_t> result;const auto& state=driver.effectiveState();
    for(const auto& field:Graphics::scalarStateEvidence())result.push_back(state.scalar(field.id));
    for(uint32_t stage=0;stage<16;++stage)for(const auto& field:Graphics::samplerStateEvidence())
        result.push_back(state.sampler(stage,field.id));
    return result;
}
struct AlphaUnchanged {
    Runtime& rt;uint32_t owner,id;EngineEffects::View view;
    std::vector<uint8_t> typed,wrapper,manager,cache,pool,shared;
    std::array<uint8_t,128> dirty;std::vector<uint32_t> effective;
    size_t compiled;uint64_t draws;
    AlphaUnchanged(Runtime& runtime,uint32_t typedOwner):rt(runtime),owner(typedOwner),id(PPCLoadU32(rt.base,owner+0x1C)),
        view(rt.engineDriver->effects().view(id)),typed(snapshot(rt,owner,0x6C0)),wrapper(snapshot(rt,view.wrapper,0x30)),
        manager(snapshot(rt,view.manager,0x18)),cache(snapshot(rt,view.cache,view.cacheBytes)),pool(snapshot(rt,view.pool,512)),
        shared(snapshot(rt,rt.effectPoolBacking,0x2A4)),dirty(rt.engineDriver->effects().privateModifiedMask(id)),
        effective(effectiveWords(*rt.engineDriver)),compiled(rt.engineDriver->effects().compiledShaderCount(id)),
        draws(rt.engineDriver->effects().shadowMeshDrawCount()) {}
    void verify() const {
        auto& effects=rt.engineDriver->effects();
        same(rt,owner,typed,"Rejected alpha entry changed its typed owner/queue");
        same(rt,view.wrapper,wrapper,"Rejected alpha entry changed its wrapper selection");
        same(rt,view.manager,manager,"Rejected alpha entry changed its manager selection");
        same(rt,view.cache,cache,"Rejected alpha entry overwrote saved state-cache values");
        same(rt,view.pool,pool,"Rejected alpha entry changed shared pool data/dirty bits");
        same(rt,rt.effectPoolBacking,shared,"Rejected alpha entry changed shared parameter values");
        need(effects.view(id).defaultVectorWords==view.defaultVectorWords&&effects.privateModifiedMask(id)==dirty,
             "Rejected alpha entry changed private parameters/dirty bits");
        need(effects.compiledShaderCount(id)==compiled&&effects.shadowMeshDrawCount()==draws&&effectiveWords(*rt.engineDriver)==effective,
             "Rejected alpha entry compiled shaders,changed render state or submitted a draw");
    }
};
void depthPixels(EngineDriver& driver,uint32_t id,uint8_t stencil) {
    const auto z=driver.readbackDepth(id);need(z.size()==size_t(1024)*1024*8,"Shadow depth extent differs");
    bool match=true;for(size_t i=0;i<z.size();i+=8) {
        float d{};std::memcpy(&d,z.data()+i,4);match&=d==0.0f && z[i+4]==stencil;
    }
    need(match,"Actual shadow depth/stencil clear pixels differ");
}
void sameDepthStencil(const std::vector<uint8_t>& actual,const std::vector<uint8_t>& expected,const char* why) {
    need(actual.size()==size_t(1024)*1024*8&&actual.size()==expected.size(),"Copied shadow depth extent differs");
    // R32_FLOAT_X8X24 storage: compare all depth bits and the stencil byte;
    // the remaining X24 bits are not depth/stencil components.
    bool match=true;for(size_t i=0;i<actual.size();i+=8)match&=!std::memcmp(actual.data()+i,expected.data()+i,5);
    need(match,why);
}
std::array<uint32_t,11> cameraWords(EngineDriver& driver) {
    const auto c=driver.cameraBinding();
    return {c.camera,c.colorRaster,c.depthRaster,c.colorIdentity,c.depthIdentity,
        c.viewport[0],c.viewport[1],c.viewport[2],c.viewport[3],c.viewport[4],c.viewport[5]};
}
struct GuestBytesRestore {
    uint8_t* destination;std::vector<uint8_t> bytes;
    GuestBytesRestore(Runtime& rt,uint32_t address,uint32_t size):
        destination(rt.pointer(address,size,true)),bytes(destination,destination+size) {}
    ~GuestBytesRestore(){std::memcpy(destination,bytes.data(),bytes.size());}
};
void resetShadowSelection(Runtime& rt,const PPCContext& entry,uint32_t owner,uint32_t camera,
                          uint32_t colorId,uint32_t depthId,uint32_t technique) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();
    const auto id=PPC_LOAD_U32(owner+0x1C),wrapper=PPC_LOAD_U32(owner+0x18);const auto v=effects.view(id);
    const auto selectedCache=PPC_LOAD_U32(wrapper+0x2C);
    need(PPC_LOAD_U32(v.manager+4)==wrapper&&PPC_LOAD_U32(v.manager+8)==id&&PPC_LOAD_U32(v.manager+12)==technique&&selectedCache,
         "Character reset fixture lacks its real selected pass");
    need(PPC_LOAD_U32(0x823EFDA0)==0x7D8802A6&&PPC_LOAD_U32(0x823F46EC)==0x4BFFB6B4&&
         PPC_LOAD_U32(0x826B4644)==0x2B0B0000,"Original character reset/end instruction changed");
    const auto typed=snapshot(rt,owner,0x6C0),wrapperBefore=snapshot(rt,wrapper,0x30),managerBefore=snapshot(rt,v.manager,0x18);
    const auto cacheBefore=snapshot(rt,v.cache,v.cacheBytes),poolBefore=snapshot(rt,v.pool,512);
    const auto sharedBefore=snapshot(rt,rt.effectPoolBacking,0x2A4);const auto parameters=effects.view(id).defaultVectorWords;
    const auto dirtyBefore=effects.privateModifiedMask(id);
    const auto colorBefore=driver.readbackColor(colorId),depthBefore=driver.readbackDepth(depthId);
    const auto defaultColor=PPC_LOAD_U32(0x82D0CB00),defaultDepth=PPC_LOAD_U32(0x82D0CAFC);
    const auto defaultColorBefore=driver.readbackColor(defaultColor),defaultDepthBefore=driver.readbackDepth(defaultDepth);
    const auto resets=driver.bindingResetCount(),draws=effects.shadowMeshDrawCount(),clears=driver.cameraClearCount();
    {EngineCpuCalls reset(entry,base);const auto saved=abi(reset.registers());reset.invoke(0x823EFDA0);
        need(abi(reset.registers())==saved,"Character mode-zero reset changed nonvolatile ABI");}
    need(driver.bindingResetCount()==resets+1&&effects.shadowMeshDrawCount()==draws&&driver.cameraClearCount()==clears,
         "Character mode-zero reset drew or cleared attachments");
    for(uint32_t address:{0x82CD1A68u,0x82CD1A6Cu,0x82CD1A70u,0x82CD1A74u})
        need(!PPC_LOAD_U32(address),"Character original reset retained a shader/declaration/index cache");
    same(rt,owner,typed,"Character reset changed typed owner/queue");same(rt,wrapper,wrapperBefore,"Character reset ended its wrapper");
    same(rt,v.manager,managerBefore,"Character reset ended its manager");same(rt,v.cache,cacheBefore,"Character reset changed saved cache rows");
    same(rt,v.pool,poolBefore,"Character reset changed shared dirty storage");same(rt,rt.effectPoolBacking,sharedBefore,"Character reset changed shared parameters");
    need(effects.view(id).defaultVectorWords==parameters&&effects.privateModifiedMask(id)==dirtyBefore,
         "Character reset changed private parameters or dirty flags");
    {GuestBytesRestore restore(rt,v.manager+8,4);PPC_STORE_U32(v.manager+8,id^1u);bool rejected=false;
        try{EngineCpuCalls invalid(entry,base);invalid.invoke(0x826B4628,v.manager);}catch(const Failure&){rejected=true;}
        need(rejected&&PPC_LOAD_U32(wrapper+0x2C)==selectedCache,"Character reset receipt admitted another logical effect identity");}
    {EngineCpuCalls end(entry,base);const auto saved=abi(end.registers());end.invoke(0x826B4628,v.manager);
        need(abi(end.registers())==saved,"Character end after reset changed nonvolatile ABI");}
    need(!PPC_LOAD_U32(v.manager+4)&&PPC_LOAD_U32(v.manager+8)==id&&!PPC_LOAD_U32(v.manager+12)&&!PPC_LOAD_U32(wrapper+0x2C),
         "Character original end after reset retained logical selection");
    {const auto ended=snapshot(rt,v.manager,0x18);EngineCpuCalls noop(entry,base);const auto saved=abi(noop.registers());
        noop.invoke(0x826B4628,v.manager);need(abi(noop.registers())==saved,"Ended character manager no-op changed ABI");
        same(rt,v.manager,ended,"Ended character manager no-op changed retained association");}
    need(effects.shadowMeshDrawCount()==draws&&driver.cameraClearCount()==clears&&effects.view(id).defaultVectorWords==parameters&&
         effects.privateModifiedMask(id)==dirtyBefore,"Character end after reset drew,cleared or changed parameters/dirty storage");
    need(driver.readbackColor(colorId)==colorBefore&&driver.readbackColor(defaultColor)==defaultColorBefore&&
         driver.readbackDepth(defaultDepth)==defaultDepthBefore,"Character reset/end changed shadow color or default attachments");
    sameDepthStencil(driver.readbackDepth(depthId),depthBefore,"Character reset/end changed rendered shadow depth/stencil");
    // Reset's default-target restoration is genuine. Subsequent shadow work
    // re-enters its original camera callbacks rather than editing role caches.
    EngineCpuCalls select(entry,base);need(select.invoke(0x823F1A08,camera)==camera&&select.invoke(0x823F1A18,camera)==camera,
         "Character reset fixture could not re-enter its real camera");
}
void staticShadowMesh(Runtime& rt,const PPCContext& entry,uint32_t owner,uint32_t camera,
                      uint32_t colorId,uint32_t depthId,uint32_t frame) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();EngineCpuCalls cpu(entry,base);
    const auto id=PPC_LOAD_U32(owner+0x1C),wrapper=PPC_LOAD_U32(owner+0x18),manager=PPC_LOAD_U32(owner+0x10);
    const auto receiver=effects.sharedParameterStorage(id,0x002C0015);
    constexpr uint32_t fixture=0x51000,meta=0x51000,object=0x51040,data=0x51080,offsets=0x510C0,
        geometry=0x51100,elements=0x51200,vertices=0x51300,indices=0x51400,submeshes=0x51500,
        cache=0x51600,materials=0x51700,queue=0x51800;
    rt.map(fixture,4096,true,"original static shadow mesh fixture");
    std::memset(rt.pointer(fixture,4096,true),0,4096);
    // Restore these even if an assertion or the original continuation fails.
    // Queue capacity is sufficient, so original827060B8 never reallocates it.
    GuestBytesRestore restoreMaterials(rt,0x82D6D814,4),restoreQueue(rt,owner+0xA8,12),restoreFrame(rt,frame+0x10,64);
    need(PPC_LOAD_U32(camera+4)==frame&&PPC_LOAD_U32(frame+0xA0)==frame,
         "Static fixture lost its genuine constructor-owned root frame");
    // Match live009's frame-bearing object shape, with the real camera frame
    // and an independent material table. No captured process pointers are used.
    PPC_STORE_U32(object,0x01000500);PPC_STORE_U32(object+4,frame);
    PPC_STORE_U32(object+8,object+8);PPC_STORE_U32(object+12,object+8);
    PPC_STORE_U32(object+16,0x823CD3D8);PPC_STORE_U32(object+24,data);
    PPC_STORE_U32(data,0x08000000);PPC_STORE_U32(data+0x24,offsets);
    PPC_STORE_U32(0x82D6D814,materials);
    constexpr std::array<uint32_t,1> materialIndices{2},materialOffsets{0x30};
    constexpr std::array<uint8_t,1> references{0xC8};
    for(uint32_t i=0;i<1;++i) {
        PPC_STORE_U32(offsets+4*materialIndices[i],materialOffsets[i]);
        PPC_STORE_U8(materials+materialOffsets[i]+2,references[i]);
    }
    PPC_STORE_U32(meta,0x00030002);PPC_STORE_U32(meta+4,0xB5F1839E);PPC_STORE_U32(meta+8,1);
    PPC_STORE_U32(meta+12,geometry);PPC_STORE_U32(meta+16,1);PPC_STORE_U32(meta+20,submeshes);PPC_STORE_U32(meta+24,1);
    // meta+24h/+28h remain zero: no bones or matrix source. Whole82707678
    // consequently takes its r28=0 branch and supplies r6=0 to82706378.
    PPC_STORE_U32(geometry,4*36);PPC_STORE_U32(geometry+4,36);PPC_STORE_U32(geometry+8,6);
    PPC_STORE_U32(geometry+12,elements);PPC_STORE_U32(geometry+16,vertices);PPC_STORE_U32(geometry+20,8);
    PPC_STORE_U32(geometry+24,1);PPC_STORE_U32(geometry+28,indices);PPC_STORE_U32(geometry+48,cache);
    PPC_STORE_U32(geometry+0x50,vertices|3);PPC_STORE_U32(geometry+0x54,2);PPC_STORE_U32(geometry+0x58,0x20000002);
    PPC_STORE_U32(geometry+0x70,indices);PPC_STORE_U32(geometry+0x74,8);PPC_STORE_U32(cache+4,cache+0x40);
    // Exact six rows from live009/static-shadow-elements.bin: float3 position,
    // packed normal/color, two float2 UVs, terminator. Stride is36, not48.
    constexpr uint32_t declaration[][3]={{0,0x002A23B9,0},{12,0x002A2187,0x00030000},
        {16,0x00182886,0x000A0000},{20,0x002C23A5,0x00050000},{28,0x002C23A5,0x00050100},
        {0x00FF0000,UINT32_MAX,0}};
    for(uint32_t i=0;i<6;++i)for(uint32_t lane=0;lane<3;++lane)PPC_STORE_U32(elements+12*i+4*lane,declaration[i][lane]);
    constexpr float xy[][2]={{-.5f,-.5f},{-.5f,.5f},{.5f,-.5f},{.5f,.5f}};
    for(uint32_t i=0;i<4;++i) {
        PPC_STORE_U32(vertices+36*i,std::bit_cast<uint32_t>(xy[i][0]));
        PPC_STORE_U32(vertices+36*i+4,std::bit_cast<uint32_t>(xy[i][1]));PPC_STORE_U32(vertices+36*i+8,0x3E800000);
        PPC_STORE_U32(vertices+36*i+12,0x055C7E3D);PPC_STORE_U32(vertices+36*i+16,0xE5000064);
        for(uint32_t uv=0;uv<2;++uv)for(uint32_t lane=0;lane<2;++lane)
            PPC_STORE_U32(vertices+36*i+20+8*uv+4*lane,std::bit_cast<uint32_t>(xy[i][lane]+.5f));
    }
    if(staticTangent) {
        const auto original=snapshot(rt,vertices,4*36);
        for(uint32_t i=0;i<4;++i) {
            std::memcpy(rt.pointer(vertices+40*i,16,true),original.data()+36*i,16);
            PPC_STORE_U32(vertices+40*i+16,0x7FC12345);
            std::memcpy(rt.pointer(vertices+40*i+20,20,true),original.data()+36*i+16,20);
        }
        constexpr uint32_t tangentRows[][3]={{0,0x002A23B9,0},{12,0x002A2187,0x00030000},
            {16,0x002A2187,0x00060000},{20,0x00182886,0x000A0000},{24,0x002C23A5,0x00050000},
            {32,0x002C23A5,0x00050100},{0x00FF0000,UINT32_MAX,0}};
        for(uint32_t i=0;i<7;++i)for(uint32_t lane=0;lane<3;++lane)PPC_STORE_U32(elements+12*i+4*lane,tangentRows[i][lane]);
        PPC_STORE_U32(geometry,4*40);PPC_STORE_U32(geometry+4,40);PPC_STORE_U32(geometry+8,7);
        const auto originalCache=cpu.invoke(0x82701BD8,elements,7),primary=PPC_LOAD_U32(originalCache+4),alternate=PPC_LOAD_U32(originalCache+8);
        need(primary&&rt.engineAudio->allocationGeneration(primary,0x50),"Original tangent declaration lacks allocator ownership");
        PPC_STORE_U32(geometry+48,originalCache);tangentDeclarations.push_back(primary);
        if(alternate){need(rt.engineAudio->allocationGeneration(alternate,0x50),"Original tangent alternate lacks allocator ownership");tangentDeclarations.push_back(alternate);}
    }
    constexpr std::array<uint16_t,4> triangles{0,1,2,3};
    for(uint32_t i=0;i<triangles.size();++i)PPC_STORE_U16(indices+2*i,triangles[i]);
    for(uint32_t i=0;i<1;++i) {
        PPC_STORE_U32(submeshes+36*i,materialIndices[i]);PPC_STORE_U32(submeshes+36*i+12,6);
        PPC_STORE_U32(submeshes+36*i+20,0);PPC_STORE_U32(submeshes+36*i+24,4);
    }
    std::memset(rt.pointer(queue,64,true),0xA5,64);
    PPC_STORE_U32(owner+0xA8,queue);PPC_STORE_U32(owner+0xAC,0);PPC_STORE_U32(owner+0xB0,4);
    const auto queueBefore=snapshot(rt,queue,64),queueHeader=snapshot(rt,owner+0xA8,12);
    // End the previous skinned draw's Boolean through its original setter.
    // Seed receiver=false so the whole static entry must actually write true.
    {EngineCpuCalls flag(entry,base);flag.registers().lr=0x827071CC;flag.registers().r31.u32=owner;
        flag.invoke(0x823C8EB0,wrapper,0x00300014,0);}
    cpu.invoke(0x82705AA0,owner,0);
    need(!effects.view(id).defaultVectorWords[64]&&!PPC_LOAD_U32(owner+0xD4)&&!PPC_LOAD_U32(receiver),
         "Static fixture did not establish unskinned/nonreceiver inputs");
    std::array<uint32_t,16> matrix{};for(uint32_t i=0;i<4;++i)matrix[5*i]=0x3F800000;
    matrix[12]=0x3E800000;matrix[13]=0x3E000000; // Translate +0.25X,+0.125Y.
    for(uint32_t i=0;i<16;++i)PPC_STORE_U32(frame+0x10+4*i,matrix[i]);
    auto expectedPrivate=effects.view(id).defaultVectorWords;auto expectedGpu=effects.readbackShadowConstants();
    for(uint32_t row=0;row<4;++row)for(uint32_t lane=0;lane<4;++lane) {
        expectedPrivate[20+4*row+lane]=matrix[4*lane+row];expectedGpu[48+4*row+lane]=matrix[4*lane+row];
    }
    expectedGpu[160]=0;
    const auto inputs=snapshot(rt,fixture,0x800),frameInput=snapshot(rt,frame+0x10,64);
    const auto colorBefore=driver.readbackColor(colorId),depthBefore=driver.readbackDepth(depthId);
    const auto draws=effects.shadowMeshDrawCount(),clears=driver.cameraClearCount();
    const auto scissors=driver.shadowTextures().scissorCount(),copies=driver.shadowTextures().copyCount();
    const auto binding=cameraWords(driver);
    const auto cacheBefore=snapshot(rt,effects.view(id).cache,effects.view(id).cacheBytes);
    auto invokeStatic=[&] {
        EngineCpuCalls mesh(entry,base);const auto before=abi(mesh.registers());
        // All three boundaries are reached by original instructions: activate
        // LR827076DC, commit LR827076E4, mesh entry LR827076F8. No manual commit.
        mesh.invoke(0x82707678,owner,object,meta);
        need(abi(mesh.registers())==before,"Whole original static entry changed nonvolatile ABI");
    };
    invokeStatic();
    need(effects.shadowMeshDrawCount()==draws+1,"Original static loop did not submit exactly one quad draw");
    need(effects.view(id).defaultVectorWords==expectedPrivate&&effects.readbackShadowConstants()==expectedGpu&&
         effects.privateModifiedMask(id)==std::array<uint8_t,128>{},
         "Whole static entry did not assemble/commit world and c40.x=0 while retaining other constants");
    need(PPC_LOAD_U32(owner+0xD4)==0x3F800000&&PPC_LOAD_U32(receiver)==0x3F800000&&
         PPC_LOAD_U32(manager+4)==wrapper&&PPC_LOAD_U32(manager+8)==id&&PPC_LOAD_U32(manager+12)==0x0007FFFC,
         "Whole static entry omitted receiver or original effect activation");
    same(rt,fixture,inputs,"Original static draw changed its input mesh/material bytes");
    same(rt,frame+0x10,frameInput,"Original static draw changed its real source frame");
    same(rt,owner+0xA8,queueHeader,"Immediate static materials changed the deferred queue header");
    same(rt,queue,queueBefore,"Immediate static materials wrote deferred queue storage");
    same(rt,effects.view(id).cache,cacheBefore,"Repeated static activation overwrote the original saved state cache");
    const auto rendered=driver.readbackDepth(depthId);
    uint32_t renderedDraws=1;
    // Object z 0.25 reaches the reversed-depth viewport as 1-z. The quad has
    // constant depth, so only the retained constant offset (D0) applies.
    const float staticOffset=std::bit_cast<float>(driver.effectiveState().scalar(0xD0)),staticDepth=(1.0f-0.25f)+staticOffset;
    for(const auto pixel:std::array<uint32_t,2>{576*1024+512,320*1024+768}) {
        float z{};std::memcpy(&z,rendered.data()+8*pixel,4);
        const bool probed=staticOffset<0&&z>staticDepth-0.000001f&&z<staticDepth+0.000001f;
        if(!probed)std::fprintf(stderr,"[STATIC MESH] pixel=%u depth=%.9g expected=%.9g\n",pixel,z,staticDepth);
        need(probed,"One static triangle missed its independently transformed depth probe");
    }
    need(!std::memcmp(rendered.data()+8*(512*1024+300),depthBefore.data()+8*(512*1024+300),5),
         "Static world translation changed a depth pixel outside its quad");
    if(staticTangent) {
        // Change every packed tangent lane while using the same real original
        // owner and producer. The shadow shader's false branch must ignore it.
        for(uint32_t i=0;i<4;++i)PPC_STORE_U32(vertices+40*i+16,i&1?0xFF800000:UINT32_MAX);
        const auto changed=snapshot(rt,fixture,0x800);invokeStatic();++renderedDraws;
        need(effects.shadowMeshDrawCount()==draws+renderedDraws&&driver.readbackDepth(depthId)==rendered,
             "Unused original tangent payload changed shadow depth or draw count");
        same(rt,fixture,changed,"Original tangent draw changed its independent input payload");
        for(uint32_t i=0;i<4;++i)PPC_STORE_U32(vertices+40*i+16,0x7FC12345);
    }
    // The same real material lookup now defers the submesh. Original code
    // stores three pointers and one reference byte; padding must stay untouched.
    for(uint32_t i=0;i<1;++i)PPC_STORE_U16(materials+materialOffsets[i],4);
    const auto queuedInputs=snapshot(rt,fixture,0x800);
    const auto stateBefore=effectiveWords(driver);
    invokeStatic();
    need(PPC_LOAD_U32(owner+0xA8)==queue&&PPC_LOAD_U32(owner+0xAC)==1&&PPC_LOAD_U32(owner+0xB0)==4,
         "Original flags4 path did not append exactly one entry without reallocating");
    auto expectedQueue=queueBefore;
    for(uint32_t i=0;i<1;++i) {
        const std::array<uint32_t,3> pointers{submeshes+36*i,meta,object};
        for(uint32_t j=0;j<3;++j)for(uint32_t byte=0;byte<4;++byte)
            expectedQueue[16*i+4*j+byte]=uint8_t(pointers[j]>>(24-8*byte));
        expectedQueue[16*i+12]=references[i];
    }
    same(rt,queue,expectedQueue,"Original deferred entry pointers,reference,padding or unused capacity differ");
    same(rt,fixture,queuedInputs,"Original enqueue modified static mesh/material input data");
    need(effects.shadowMeshDrawCount()==draws+renderedDraws&&effects.readbackShadowConstants()==expectedGpu&&
         effectiveWords(driver)==stateBefore,"Deferred static casters drew or changed the committed values/effective state");
    // No fabricated textured-caster resources: exercise only the actual CPU
    // queue, then prove its nonempty native alpha activation still rejects.
    {const AlphaUnchanged unchanged(rt,owner);EngineCpuCalls alpha(entry,base);bool rejected=false;
        try{alpha.invoke(0x82706130,owner);}catch(const Failure& error){
            rejected=std::string(error.what()).find("Character shadow alpha queue is not yet qualified")!=std::string::npos&&
                alpha.registers().lastFunction==0x826B5FC0&&uint32_t(alpha.registers().lr)==0x8270614C;}
        need(rejected,"Native alpha accepted the original nonempty static-caster queue");unchanged.verify();}
    same(rt,queue,expectedQueue,"Rejected alpha activation consumed or altered a static queue entry");
    sameDepthStencil(driver.readbackDepth(depthId),rendered,"Deferred queue/alpha rejection changed rendered static depth or stencil");
    need(driver.readbackColor(colorId)==colorBefore&&driver.cameraClearCount()==clears&&
         driver.shadowTextures().scissorCount()==scissors&&driver.shadowTextures().copyCount()==copies&&cameraWords(driver)==binding,
         "Static fixture changed color,clear/scissor/copy counts or camera ownership");
}
void originalGroupedCharacter(Runtime& rt,const PPCContext& entry,uint32_t owner,uint32_t camera,uint32_t depthId) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();EngineCpuCalls cpu(entry,base);
    const auto id=PPC_LOAD_U32(owner+0x1C);constexpr uint32_t area=0x60000,meta=area,geometry=0x60100,
        elements=0x60300,vertices=0x60400,indices=0x60500,cache=0x60600,rows=0x60700,
        object=0x60800,data=0x60900,offsets=0x60A00,materials=0x60B00,boneMap=0x61000,groups=0x61100,
        bind=0x61200,joints=0x61300,hierarchy=0x61400,skin=0x61500,objectFrame=0x61600;
    rt.map(area,0x10000,true,"genuine grouped character shadow fixture");std::memset(rt.pointer(area,0x10000,true),0,0x10000);
    GuestBytesRestore restoreMaterials(rt,0x82D6D814,4);PPC_STORE_U32(0x82D6D814,materials);
    constexpr std::array<std::array<uint32_t,2>,9> pins={{{0x8270717C,0x4BFF764D},{0x82707188,0x41990018},
        {0x82706390,0x7CD73378},{0x82706410,0x809F0020},{0x82706418,0x807F001C},
        {0x82706420,0x4BFF82F1},{0x82706434,0x4BFF77AD},{0x8270643C,0x48517765},{0x82706408,0x40990068}}};
    for(const auto& p:pins)need(PPC_LOAD_U32(p[0])==p[1],"Original character grouped instruction changed");
    const auto matrix=[&](uint32_t address,float tx=0) {
        for(uint32_t i=0;i<16;++i)PPC_STORE_U32(address+4*i,i%5?0:0x3F800000);
        PPC_STORE_U32(address+48,std::bit_cast<uint32_t>(tx));
    };
    PPC_STORE_U32(object,0x01000500);PPC_STORE_U32(object+4,objectFrame);
    PPC_STORE_U32(object+8,object+8);PPC_STORE_U32(object+12,object+8);PPC_STORE_U32(object+16,0x823CD3D8);
    PPC_STORE_U32(object+24,data);PPC_STORE_U32(data+0x24,offsets);PPC_STORE_U32(objectFrame+0xA0,objectFrame);matrix(objectFrame+0x10);
    PPC_STORE_U32(meta,0x00030002);PPC_STORE_U32(meta+4,0xB5F8FBF2);PPC_STORE_U32(meta+12,geometry);PPC_STORE_U32(meta+20,rows);
    PPC_STORE_U32(meta+0x28,boneMap);PPC_STORE_U32(geometry,4*48);PPC_STORE_U32(geometry+4,48);PPC_STORE_U32(geometry+8,5);
    PPC_STORE_U32(geometry+12,elements);PPC_STORE_U32(geometry+16,vertices);PPC_STORE_U32(geometry+20,8);PPC_STORE_U32(geometry+24,1);
    PPC_STORE_U32(geometry+28,indices);PPC_STORE_U32(geometry+48,cache);PPC_STORE_U32(cache+4,cache+0x40);
    PPC_STORE_U32(geometry+0x50,vertices|3);PPC_STORE_U32(geometry+0x54,2);PPC_STORE_U32(geometry+0x58,0x20000002);
    PPC_STORE_U32(geometry+0x70,indices);PPC_STORE_U32(geometry+0x74,8);
    constexpr uint32_t declaration[][3]={{0,0x002A23B9,0},{16,0x002C23A5,0x00050000},
        {24,0x001A2286,0x00020000},{28,0x001A23A6,0x00010000},{0x00FF0000,UINT32_MAX,0}};
    for(uint32_t i=0;i<5;++i)for(uint32_t lane=0;lane<3;++lane)PPC_STORE_U32(elements+12*i+4*lane,declaration[i][lane]);
    constexpr float xy[][2]={{-.5f,-.5f},{-.5f,.5f},{.5f,-.5f},{.5f,.5f}};
    for(uint32_t i=0;i<4;++i) {
        PPC_STORE_U32(vertices+48*i,std::bit_cast<uint32_t>(xy[i][0]));PPC_STORE_U32(vertices+48*i+4,std::bit_cast<uint32_t>(xy[i][1]));
        PPC_STORE_U32(vertices+48*i+8,0x3F000000);PPC_STORE_U32(vertices+48*i+24,i>=2?1:0);
        PPC_STORE_U32(vertices+48*i+28,0x3F800000);PPC_STORE_U16(indices+2*i,uint16_t(i));
    }
    const auto atomicOffset=PPC_LOAD_U32(0x82E27994),skinOffset=PPC_LOAD_U32(0x82E27998);
    need(atomicOffset>=0x28&&atomicOffset<0x1000&&skinOffset>0x24&&skinOffset<0x1000,"Original grouped shadow plugin offsets differ");
    PPC_STORE_U32(object+atomicOffset,hierarchy);PPC_STORE_U32(data+skinOffset,skin);
    PPC_STORE_U32(hierarchy+8,joints);PPC_STORE_U32(skin+12,bind);matrix(bind);matrix(bind+64);matrix(joints,-.125f);matrix(joints+64,.125f);
    bool firstRejected=false,groupRejected=false,rangeMutationRejected=false;uint32_t currentBones=0,currentRows=0,compositions=0,selections=0,uploads=0;
    PaletteObservation observation([&](uint32_t pc,PPCContext& c,bool returned) {
        if(pc==0x826FE7C8&&returned) {
            ++compositions;need(c.r3.u32==0x82D64080,"Original shadow composed palette address differs");
            for(uint32_t bone=0;bone<currentBones;++bone)for(uint32_t lane=0;lane<16;++lane) {
                const uint32_t expected=lane%5==0?0x3F800000u:lane==12?std::bit_cast<uint32_t>((bone&1)?-.125f:.125f):0u;
                need(PPC_LOAD_U32(c.r3.u32+64*bone+4*lane)==expected,"Original shadow joint composition/remap differs");
            }
        } else if(pc==0x826FE710&&returned) {
            ++selections;const auto count=PPC_LOAD_U32(c.r1.u32+0x50),table=PPC_LOAD_U32(c.r31.u32+32);
            need(count==2&&c.r3.u32==(PPC_LOAD_U32(c.r31.u32+28)==1?0x82D64080+64*PPC_LOAD_U8(table):0x82D63070),
                 "Original shadow group alias/copy/count differs");
        } else if(pc==0x826FDBE0) {
            const bool grouped=uint32_t(c.lr)==0x82706438;
            need(grouped||uint32_t(c.lr)==0x827071A0,"Unexpected genuine shadow bone caller");
            if(!returned&&!(grouped?groupRejected:firstRejected)) {
                const auto saved=c;const auto values=effects.view(id).defaultVectorWords;
                const auto mask=effects.privateModifiedMask(id);
                const auto reject=[&](bool wrongSource) {
                    c=saved;if(wrongSource)c.r5.u32+=64;else ++c.r6.u32;bool rejected=false;
                    c.lastFunction=0x826FDBE0;
                    // Catch intentional failure through the C++ endpoint;
                    // /EHsc assumes named extern-C__imp__ calls cannot throw.
                    try{effects.setShadowBones(c,base);}catch(const Failure&){rejected=true;}c=saved;
                    need(c.r5.u64==saved.r5.u64&&c.r6.u64==saved.r6.u64,"Rejected shadow palette context was not restored");
                    need(rejected&&effects.view(id).defaultVectorWords==values&&effects.privateModifiedMask(id)==mask,
                         "Wrong genuine shadow palette source/count accepted or changed owned values");
                };
                reject(true);reject(false);if(grouped)groupRejected=true;else firstRejected=true;
            }
            if(!returned&&grouped&&!rangeMutationRejected&&PPC_LOAD_U32(c.r31.u32+28)>1) {
                const auto saved=c;const auto table=PPC_LOAD_U32(c.r31.u32+32);const auto original=PPC_LOAD_U8(table);
                const auto values=effects.view(id).defaultVectorWords;const auto mask=effects.privateModifiedMask(id);
                need(original>=2,"Copied shadow palette has no equivalent-matrix mutation case");
                PPC_STORE_U8(table,uint8_t(original-2));c.lastFunction=0x826FDBE0;
                bool rejected=false;try{effects.setShadowBones(c,base);}catch(const Failure& error){
                    rejected=std::strstr(error.what(),"ranges changed during traversal")!=nullptr;
                }
                PPC_STORE_U8(table,original);c=saved;
                need(rejected&&effects.view(id).defaultVectorWords==values&&effects.privateModifiedMask(id)==mask,
                     "Valid equivalent-matrix shadow range mutation accepted or changed owned values");
                rangeMutationRejected=true;
            }
            if(returned) {
                ++uploads;const auto values=effects.view(id).defaultVectorWords;const auto mask=effects.privateModifiedMask(id);
                for(uint32_t bone=0;bone<c.r6.u32;++bone)for(uint32_t row=0;row<4;++row)for(uint32_t lane=0;lane<4;++lane)
                    need(values[80+16*bone+4*row+lane]==PPC_LOAD_U32(c.r5.u32+64*bone+16*lane+4*row),
                         "Original character selected palette transpose differs");
                for(uint32_t bone=0;bone<c.r6.u32;++bone)need(mask[(14+bone)/8]&(0x80>>((14+bone)&7)),"Original shadow bone leaf was not dirtied");
            }
        }
    });
    // Original827063D8..E4 skips a zero-row loop, including a null row pointer.
    // Exercise the genuine parent before diagnostic sampling is exhausted.
    currentBones=65;currentRows=0;PPC_STORE_U32(meta+0x24,currentBones);PPC_STORE_U32(meta+0x10,0);PPC_STORE_U32(meta+0x14,0);
    for(uint32_t bone=0;bone<currentBones;++bone)PPC_STORE_U8(boneMap+bone,uint8_t(1-(bone&1)));
    const auto emptyInput=snapshot(rt,area,0x2000),emptyDepth=driver.readbackDepth(depthId);
    const auto emptyDraws=effects.shadowMeshDrawCount();EngineCpuCalls empty(entry,base);const auto emptyAbi=abi(empty.registers());
    empty.invoke(0x82707138,owner,object,meta);
    need(abi(empty.registers())==emptyAbi&&effects.shadowMeshDrawCount()==emptyDraws&&driver.readbackDepth(depthId)==emptyDepth,
         "Original null-row character traversal changed ABI or submitted a draw");
    same(rt,area,emptyInput,"Original null-row character traversal changed authored inputs");
    need(!effects.view(id).defaultVectorWords[64],"Original null-row parent did not reset its skin Boolean");
    PPC_STORE_U32(meta+0x14,rows);
    for(const uint32_t bones:{2u,64u,65u,255u}) {
        currentBones=bones;currentRows=bones==65?3:2;PPC_STORE_U32(meta+0x24,bones);PPC_STORE_U32(meta+0x10,currentRows);
        for(uint32_t bone=0;bone<bones;++bone)PPC_STORE_U8(boneMap+bone,uint8_t(1-(bone&1)));
        for(uint32_t i=0;i<currentRows;++i) {
            const auto row=rows+36*i,table=groups+16*i;std::memset(rt.pointer(row,36,true),0,36);
            PPC_STORE_U32(row+12,6);PPC_STORE_U32(row+24,4);
            if(bones>64) {
                PPC_STORE_U32(row+28,i%2?3:1);PPC_STORE_U32(row+32,table);
                PPC_STORE_U8(table,uint8_t(bones-(i%2?1:3)));PPC_STORE_U8(table+1,i%2?1:2);
                if(i%2){PPC_STORE_U8(table+2,uint8_t(bones-2));PPC_STORE_U8(table+3,1);
                    PPC_STORE_U8(table+4,uint8_t(bones));PPC_STORE_U8(table+5,0);}
            }
        }
        const auto input=snapshot(rt,area,0x2000);const auto draws=effects.shadowMeshDrawCount();
        const auto scissor=driver.effectiveState().scalar(0xC8);driver.directScalar(base,0xC8,0);
        need(cpu.invoke(0x823F1B80,camera,area+0x80,6)==camera,"Original grouped shadow depth/stencil clear failed");
        driver.directScalar(base,0xC8,scissor);
        const auto depthBefore=driver.readbackDepth(depthId);EngineCpuCalls pass(entry,base);const auto before=abi(pass.registers());
        pass.invoke(0x82707138,owner,object,meta);need(abi(pass.registers())==before,"Whole original grouped character parent changed nonvolatile ABI");
        need(effects.shadowMeshDrawCount()==draws+currentRows,"Original grouped character loop omitted submeshes");
        same(rt,area,input,"Original grouped character parent changed authored skeleton/group/geometry inputs");
        std::array<uint8_t,128> dirty{};dirty[1]=0x20;
        need(effects.privateModifiedMask(id)==dirty&&!effects.view(id).defaultVectorWords[64],
             "Original character parent did not retain exactly its final uncommitted skin-flag reset");
        const auto constants=effects.readbackShadowConstants();need(constants[160]==0x3F800000,"Grouped character GPU lost its committed skin flag");
        const auto pixels=driver.readbackDepth(depthId);float center{};std::memcpy(&center,pixels.data()+8*(512*1024+512),4);
        const auto offset=std::bit_cast<float>(driver.effectiveState().scalar(0xD0));
        need(center>0.5f+offset-.000001f&&center<0.5f+offset+.000001f&&
             !std::memcmp(pixels.data()+8*(512*1024+900),depthBefore.data()+8*(512*1024+900),5),
             "Genuine grouped character shadow missed its depth probe or escaped bounds");
        if(bones>64) {
            const auto original=PPC_LOAD_U8(groups+1);PPC_STORE_U8(groups+1,65);
            bool rejected=false;const auto composed=snapshot(rt,0x82D64080,64*bones);EngineCpuCalls invalid(entry,base);
            try{invalid.invoke(0x82707138,owner,object,meta);}catch(const Failure&){rejected=true;}PPC_STORE_U8(groups+1,original);
            need(rejected&&effects.shadowMeshDrawCount()==draws+currentRows,"Oversized shadow group reached a native draw");
            same(rt,0x82D64080,composed,"Rejected group ran original matrix composition before preflight");
        }
    }
    need(compositions==5&&selections==5&&uploads==7&&firstRejected&&groupRejected&&rangeMutationRejected,"Genuine grouped shadow traversal count differs");
}
void run(Runtime& rt,const PPCContext& entry) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& shadows=driver.shadowTextures();EngineCpuCalls cpu(entry,base);
    const auto source=PPC_LOAD_U32(table+4*16);const auto original=snapshot(rt,source,PPC_LOAD_U32(source+4)+12);
    const auto originalTable=snapshot(rt,table,400);
    const auto options=cpu.registers().r1.u32+0x60,context=PPC_LOAD_U32(0x82D5DA74),loading=PPC_LOAD_U32(0x82E07248);
    PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    const auto manager=cpu.invoke(0x8269BF70,0x260,options);cpu.invoke(0x826B6F60,manager,context);
    cpu.invoke(0x827019E8,table,25);cpu.invoke(0x826B7218,manager);
    const auto owner=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(table+4*16+4));need(owner,"Missing original shadows typed owner");
    const auto saved=abi(cpu.registers());const auto ext=PPC_LOAD_U32(0x82E3DC94),oldStencil=PPC_LOAD_U32(0x82D0CB14);
    rt.map(0x50000,4096,true,"shadow camera clear fixture");
    std::array<uint32_t,2> cameras{},colors{},depths{};
    for(uint32_t slot=0;slot<2;++slot) {
        cameras[slot]=PPC_LOAD_U32(owner+0x5B4+slot*4);need(shadows.ownsCamera(cameras[slot]),"Original shadow camera provenance absent");
        colors[slot]=PPC_LOAD_U32(PPC_LOAD_U32(cameras[slot]+0x60)+ext);depths[slot]=PPC_LOAD_U32(PPC_LOAD_U32(cameras[slot]+0x64)+ext);
        PPC_STORE_U32(0x50000,slot?0x00FF00FF:0xFF0000FF);PPC_STORE_U32(0x82D0CB14,0x35+slot);
        need(cpu.invoke(0x823F1B80,cameras[slot],0x50000,7)==cameras[slot],"Original clear did not return its camera");
        const auto pixels=driver.readbackColor(colors[slot]);const uint32_t expected=slot?0xC00FFC00:0xC00003FF;bool match=true;
        for(size_t i=0;i<pixels.size();i+=4){uint32_t p{};std::memcpy(&p,pixels.data()+i,4);match&=p==expected;}
        need(pixels.size()==size_t(1024)*1024*4 && match,"Shadow color initialized into wrong attachment");depthPixels(driver,depths[slot],uint8_t(0x35+slot));
    }
    std::array<std::vector<uint8_t>,2> colorBefore={driver.readbackColor(colors[0]),driver.readbackColor(colors[1])};
    // Use a genuine original camera as a frame-bearing RenderWare object.
    // The original827055E0 builds the homogeneous matrix on its own stack.
    const auto id=PPC_LOAD_U32(owner+0x1C),frame=PPC_LOAD_U32(cameras[0]+4),pool=PPC_LOAD_U32(root);
    const auto frameBefore=snapshot(rt,frame+0x10,64),poolBefore=snapshot(rt,pool,512),sharedBefore=snapshot(rt,rt.effectPoolBacking,0x2A4);
    auto expected=driver.effects().view(id).defaultVectorWords;auto mask=driver.effects().privateModifiedMask(id);
    for(uint32_t cycle=0;cycle<2;++cycle) {
        std::array<uint32_t,16> matrix{};
        for(uint32_t r=0;r<4;++r)for(uint32_t col=0;col<3;++col) {
            const auto value=std::bit_cast<uint32_t>(float(int(r*7+col+1)*(cycle?-1:1))*.125f);
            PPC_STORE_U32(frame+0x10+r*16+col*4,value);matrix[r*4+col]=value;
        }
        matrix[15]=0x3F800000;cpu.invoke(0x827055E0,owner,cameras[0]);
        for(uint32_t r=0;r<4;++r)for(uint32_t col=0;col<4;++col)expected[20+r*4+col]=matrix[col*4+r];
        mask[0]|=0x20;need(driver.effects().view(id).defaultVectorWords==expected && driver.effects().privateModifiedMask(id)==mask &&
            abi(cpu.registers())==saved,"Original shadow world matrix packing,neighbors,mask or ABI differs");
    }
    std::memcpy(rt.pointer(frame+0x10,64,true),frameBefore.data(),64);
    same(rt,pool,poolBefore,"Shadow world setter changed shared pool masks/metadata");same(rt,rt.effectPoolBacking,sharedBefore,"Shadow world setter changed shared values");
    const auto worldHandle=PPC_LOAD_U32(owner+0x660);PPC_STORE_U32(owner+0x660,0x00080002);
    rejects([&]{EngineCpuCalls invalid(entry,base);invalid.invoke(0x827055E0,owner,cameras[0]);},"Shadow setter accepted another valid matrix handle");
    PPC_STORE_U32(owner+0x660,worldHandle);rejects([&]{EngineCpuCalls invalid(entry,base);invalid.invoke(0x82704600,id,worldHandle,frame+0x10);},"Shadow setter accepted a foreign caller frame");
    need(driver.effects().view(id).defaultVectorWords==expected && driver.effects().privateModifiedMask(id)==mask,"Rejected matrix setter changed private data");
    const auto workingColor=driver.readbackColor(PPC_LOAD_U32(0x82D0CB00)),workingDepth=driver.readbackDepth(PPC_LOAD_U32(0x82D0CAFC));
    const auto copy0=shadows.readback(PPC_LOAD_U32(owner+0xF0)),copy1=shadows.readback(PPC_LOAD_U32(owner+0xF4));
    std::array<std::vector<uint8_t>,2> expectedCopies{copy0,copy1};
    const auto copiesBefore=shadows.copyCount();
    // Supply the caller's 224-byte frame, then let the entire82704BE8
    // helper create/save/restore its own128-byte frame and assemble the SDK
    // arguments. The real mid-BL82704C2C hook sees LR82704C00, not82704C30.
    auto copyShadow=[&](uint32_t selector,bool accepted,
                       const std::function<void(PPCContext&,uint32_t&)>& alter={}) {
        const auto destination=PPC_LOAD_U32(owner+0xF0+4*selector),sibling=PPC_LOAD_U32(owner+0xF0+4*(selector^1));
        const auto sourcePixels=driver.readbackDepth(depths[selector]),otherSource=driver.readbackDepth(depths[selector^1]);
        const auto destinationBefore=shadows.readback(destination),siblingBefore=shadows.readback(sibling);
        const auto border=PPC_LOAD_U32(owner+0xFC);const auto borderBefore=shadows.readback(border);
        const auto destinationView=shadows.view(destination),siblingView=shadows.view(sibling);
        const auto typedBefore=snapshot(rt,owner,0x6C0),managerBefore=snapshot(rt,manager,0x18);
        const auto effectsBefore=driver.effects().view(id).defaultVectorWords;const auto dirtyBefore=driver.effects().privateModifiedMask(id);
        const auto effectiveBefore=effectiveWords(driver);const auto bindingBefore=cameraWords(driver);
        const auto count=shadows.copyCount(),draws=driver.effects().shadowMeshDrawCount(),clears=driver.cameraClearCount(),scissors=shadows.scissorCount();
        EngineCpuCalls copy(entry,base);auto& c=copy.registers();c.r1.u32-=0xE0;
        rt.pointer(c.r1.u32,0xE0,true);PPC_STORE_U32(c.r1.u32,c.r1.u32+0xE0);
        c.lr=0x827073BC;c.r31.u32=owner;c.r26.u32=selector;c.r24.u32=cameras[selector];c.f31.f64=-19.25;
        auto argument=destination;if(alter)alter(c,argument);
        const auto before=abi(c);const auto parentSp=c.r1.u32;bool rejected=false;
        // Seed stale CPU attachment-cache entries without changing the real
        // native binding. Whole-helper823EE8F8 ->823EDD38 must invalidate all
        // five words (823EDD60..80), including on later copy-guard rejection.
        for(uint32_t at=0x82D0CF58;at<=0x82D0CF68;at+=4)
            PPC_STORE_U32(at,at==0x82D0CF58?depths[selector]:colors[selector]);
        try{copy.invoke(0x82704BE8,argument);}catch(const Failure&){rejected=true;}
        for(uint32_t at=0x82D0CF58;at<=0x82D0CF68;at+=4)
            need(!PPC_LOAD_U32(at),"Original copy helper did not retain attachment-cache invalidation");
        if(accepted) {
            need(!rejected&&c.r3.u64==0&&abi(c)==before&&c.f31.f64==-19.25,"Original depth-copy helper failed or changed its completion/nonvolatile ABI");
            need(PPC_LOAD_U32(parentSp-0x80)==parentSp&&PPC_LOAD_U32(parentSp-8)==0x827073BC&&
                 PPC_LOAD_U32(parentSp-12)==owner&&!PPC_LOAD_U32(parentSp-0x24)&&!PPC_LOAD_U32(parentSp-0x1C),
                 "Original depth-copy helper did not assemble its saved frame and zero stack arguments");
            need(shadows.copyCount()==count+1,"Original depth-copy helper did not submit exactly one copy");
            const auto after=shadows.view(destination);
            need(after.phase==EngineShadowTextures::Phase::Uploaded&&after.identity==destinationView.identity&&
                 after.owner==destinationView.owner&&after.field==destinationView.field&&after.width==destinationView.width&&
                 after.height==destinationView.height&&after.format==destinationView.format&&after.staging==destinationView.staging,
                 "Original depth copy changed destination ownership/extent or omitted Uploaded publication");
            sameDepthStencil(shadows.readback(destination),sourcePixels,"Original depth copy changed depth bits or stencil");
            expectedCopies[selector]=shadows.readback(destination);
        }else {
            need(rejected&&uint32_t(c.lr)==0x82704C00,"Unqualified copy did not reject at the original mid-BL boundary");
            need(shadows.copyCount()==count&&shadows.view(destination).phase==destinationView.phase&&
                 shadows.readback(destination)==destinationBefore,"Rejected copy submitted work,changed destination pixels or published a phase");
        }
        need(shadows.view(sibling).phase==siblingView.phase&&shadows.readback(sibling)==siblingBefore&&shadows.readback(border)==borderBefore,
             "Depth copy changed a sibling destination or the border texture");
        sameDepthStencil(driver.readbackDepth(depths[selector]),sourcePixels,"Depth copy changed its source attachment");
        sameDepthStencil(driver.readbackDepth(depths[selector^1]),otherSource,"Depth copy changed the other camera depth attachment");
        for(uint32_t slot=0;slot<2;++slot)need(driver.readbackColor(colors[slot])==colorBefore[slot],"Depth copy changed a camera color attachment");
        need(effectiveWords(driver)==effectiveBefore&&cameraWords(driver)==bindingBefore&&driver.cameraClearCount()==clears&&
             driver.effects().shadowMeshDrawCount()==draws&&shadows.scissorCount()==scissors,"Depth copy changed effective state,targets,viewport or draw/clear/scissor counts");
        same(rt,owner,typedBefore,"Depth copy changed typed shadow data");same(rt,manager,managerBefore,"Depth copy changed effect manager selection");
        need(driver.effects().view(id).defaultVectorWords==effectsBefore&&driver.effects().privateModifiedMask(id)==dirtyBefore,
             "Depth copy changed private parameters or committed pending constants");
    };
    for(uint32_t cycle=0;cycle<2;++cycle)for(uint32_t slot=0;slot<2;++slot) {
        const auto c=cameras[slot];PPC_STORE_U32(0x82D0CB14,0x65+cycle*2+slot);
        cpu.invoke(0x823F1B80,c,0x82D6C09C,6);need(abi(cpu.registers())==saved,"Original shadow clear changed ABI");
        depthPixels(driver,depths[slot],uint8_t(0x65+cycle*2+slot));
        const auto clears=driver.cameraClearCount();need(cpu.invoke(0x823F1A18,c)==c,"Original shadows begin failed");
        const auto binding=driver.cameraBinding();need(binding.camera==c && binding.colorIdentity==colors[slot] && binding.depthIdentity==depths[slot] &&
            binding.viewport==std::array<uint32_t,6>{0,0,1024,1024,0x3F800000,0},"Shadows selected wrong targets/viewport");
        need(cpu.invoke(0x823F1A08,c)==c && abi(cpu.registers())==saved,"Original shadows end/ABI differs");
        need(driver.cameraClearCount()==clears,"Camera pass unexpectedly cleared pixels");
        for(uint32_t i=0;i<2;++i)need(driver.readbackColor(colors[i])==colorBefore[i],"Depth/stencil clear or pass changed shadow color");
    }
    // Equal-sized valid rasters/cameras are insufficient: original constructor
    // association and generation must remain exactly the observed pair.
    const auto c=cameras[0],oldColor=PPC_LOAD_U32(c+0x60);const auto clears=driver.cameraClearCount();
    PPC_STORE_U32(c+0x60,PPC_LOAD_U32(cameras[1]+0x60));rejects([&]{driver.clearCamera(base,c,0x82D6C09C,6);},"Accepted other shadow camera color");PPC_STORE_U32(c+0x60,oldColor);
    PPC_STORE_U32(owner+0x5B4,cameras[1]);rejects([&]{driver.selectCamera(base,c);},"Accepted republished shadow camera");PPC_STORE_U32(owner+0x5B4,c);
    PPC_STORE_U32(c+0x14,1);rejects([&]{driver.selectCamera(base,c);},"Accepted altered shadow projection");PPC_STORE_U32(c+0x14,2);
    const auto foreign=cpu.invoke(0x82704C68,1024,1024);rejects([&]{driver.selectCamera(base,foreign);},"Unrelated real 1024 camera accepted");cpu.invoke(0x82714220,foreign);
    need(driver.cameraClearCount()==clears,"Rejected shadow camera operation cleared pixels");
    for(uint32_t selector=0;selector<2;++selector) {
        // Invoke the genuine parent; its prologue and selector choose the camera.
        // Stop at the real nonempty-alpha guard after the parent has cleared
        // and collected its (empty) caster list, preserving the mesh fixture.
        EngineCpuCalls pass(entry,base);bool caught=false;const auto before=driver.cameraClearCount(),borders=shadows.scissorCount();
        constexpr std::array<uint32_t,7> fields={5,0xF,0x10,0x11,0x29,0x2A,0x2B};std::array<uint32_t,7> stateBefore{};
        for(size_t i=0;i<fields.size();++i)stateBefore[i]=cpu.invoke(0x826B7940,fields[i]);
        std::unique_ptr<AlphaUnchanged> rejected;
        {
            AlphaEntryObservation observation([&](PPCContext& c,uint8_t* memory){
                need(memory==base&&c.r3.u32==owner&&uint32_t(c.lr)==0x82707040&&!PPC_LOAD_U32(owner+0xAC)&&!rejected,
                     "Parent alpha entry did not follow its original empty collection");
                PPC_STORE_U32(owner+0xAC,1);rejected=std::make_unique<AlphaUnchanged>(rt,owner);
            });
            try{pass.invoke(0x82707220,owner,selector);}catch(const Failure& e){
                caught=std::string(e.what()).find("Character shadow alpha queue is not yet qualified")!=std::string::npos &&
                    pass.registers().lastFunction==0x826B5FC0 && uint32_t(pass.registers().lr)==0x8270614C;
            }
        }
        need(caught&&rejected,"Original shadow parent did not reject the injected nonempty alpha queue");
        rejected->verify();PPC_STORE_U32(owner+0xAC,0);
        need(driver.cameraClearCount()==before+1 && shadows.scissorCount()==borders+1 && driver.cameraBinding().camera==cameras[selector],
             "Original shadow parent did not reach expected real FX boundary");
        need(cpu.invoke(0x826B7940,0x29)==1 && cpu.invoke(0x826B7940,0x2A)==PPC_LOAD_U32(owner+0xBC) &&
             cpu.invoke(0x826B7940,0x2B)==PPC_LOAD_U32(selector?0x82CEFF38:0x82CEFF34),"Original shadow bias/scissor state requests changed");
        if(selector==0){
            // The real parent has selected its character pass and camera.
            // Reject a deliberately wrong Boolean handle after activation,
            // before the null object/mesh arguments can be used. This fixture
            // tests activation and parameter contracts, not a complete mesh.
            auto& effects=driver.effects();const auto wrapper=PPC_LOAD_U32(owner+0x18),id=PPC_LOAD_U32(owner+0x1C);
            const auto view=effects.view(id);need(PPC_LOAD_U32(owner+0x5DC)==0x0007FFFC,"Parent did not select RenderShadowDepth");
            constexpr std::array<uint32_t,3> selectors{1,3,15};std::array<uint32_t,3> prior{};
            for(size_t i=0;i<3;++i)prior[i]=cpu.invoke(0x826B7940,selectors[i]);
            const auto color=driver.readbackColor(colors[selector]),depth=driver.readbackDepth(depths[selector]);
            auto activate=[&]{EngineCpuCalls character(entry,base);bool next=false;
                const auto flag=PPC_LOAD_U32(owner+0x674);PPC_STORE_U32(owner+0x674,0);
                try{character.invoke(0x82707138,owner,0,0);}catch(const Failure& e){
                    next=std::string(e.what()).find("Original character skinning flag parameter differs")!=std::string::npos&&
                        character.registers().lastFunction==0x823C8EB0&&uint32_t(character.registers().lr)==0x8270716C;}
                PPC_STORE_U32(owner+0x674,flag);
                need(next,"Original character pass did not bind its shader before the skinning setter boundary");};
            activate();need(effects.compiledShaderCount(id)==1,"Character activation compiled another pass or omitted its real VS");
            need(PPC_LOAD_U32(manager+4)==wrapper&&PPC_LOAD_U32(manager+8)==id&&PPC_LOAD_U32(manager+12)==0x0007FFFC&&
                PPC_LOAD_U32(wrapper+0x2C)==view.cache+24,"Original manager/cache did not select character shadow pass");
            const auto activeCache=snapshot(rt,view.cache,view.cacheBytes);activate();same(rt,view.cache,activeCache,"Repeated character activation overwrote saved state");
            const auto mask=effects.privateModifiedMask(id);
            for(size_t i=0;i<mask.size();++i)need(mask[i]==(i<16?0xFF:0),"Character activation dirty extent/tail differs from the original");
            const auto parameterPool=snapshot(rt,pool,512),parameterShared=snapshot(rt,rt.effectPoolBacking,0x2A4);
            auto parameters=effects.view(id).defaultVectorWords;
            for(const uint32_t caller:{0x8270716Cu,0x827071CCu})for(const uint32_t input:{0u,1u,0x100u,0xFFu,0x12340000u,0x80000001u}) {
                EngineCpuCalls flag(entry,base);auto& c=flag.registers();c.lr=caller;c.r31.u32=owner;
                const auto before=abi(c);flag.invoke(0x823C8EB0,wrapper,0x00300014,input);
                parameters[64]=uint8_t(input)?0x3F800000:0;
                need(effects.view(id).defaultVectorWords==parameters&&abi(c)==before,
                     "Character Boolean low-byte conversion,neighbor values or ABI differs");
            }
            rejects([&]{EngineCpuCalls flag(entry,base);flag.registers().lr=0x8270716C;flag.registers().r31.u32=owner;
                flag.invoke(0x823C8EB0,wrapper,0x00340016,1);},"Character Boolean accepted another private flag");
            const auto boneSource=snapshot(rt,0x82D64080,4096);
            // Deliberately asymmetric raw matrix words include signed zero and
            // NaN payloads. Check the original bitwise transpose, including the
            // fourth row/column, without arithmetic or canonicalizing payloads.
            for(uint32_t i=0;i<1024;++i)PPC_STORE_U32(0x82D64080+4*i,0x80000000u+i*0x00FD13B7u);
            for(const uint32_t count:{1u,7u,8u,9u,63u,64u,0u}) {
                const auto n=count?count:64;PPC_STORE_U32(0x50124,count);
                EngineCpuCalls bones(entry,base);auto& c=bones.registers();c.lr=0x827071A0;c.r31.u32=owner;
                c.r28.u32=0x82D64080;c.r29.u32=0x50100;c.r30.u32=count;const auto before=abi(c);
                bones.invoke(0x826FDBE0,id,0x0040001C,0x82D64080,count);
                for(uint32_t b=0;b<n;++b)for(uint32_t r=0;r<4;++r)for(uint32_t col=0;col<4;++col)
                    parameters[80+16*b+4*r+col]=PPC_LOAD_U32(0x82D64080+64*b+16*col+4*r);
                need(effects.view(id).defaultVectorWords==parameters&&abi(c)==before,
                     "Character bone array bytes,count convention,neighbors or ABI differs");
            }
            for(const uint32_t count:{65u,UINT32_MAX}) {
                PPC_STORE_U32(0x50124,count);
                rejects([&]{EngineCpuCalls bones(entry,base);auto& c=bones.registers();c.lr=0x827071A0;c.r31.u32=owner;
                    c.r28.u32=0x82D64080;c.r29.u32=0x50100;c.r30.u32=count;
                    bones.invoke(0x826FDBE0,id,0x0040001C,0x82D64080,count);},"Character bone setter accepted an oversized array");
            }
            std::memcpy(rt.pointer(0x82D64080,4096,true),boneSource.data(),4096);
            need(effects.view(id).defaultVectorWords==parameters&&effects.privateModifiedMask(id)==mask,
                 "Rejected character parameter changed owned values/masks");
            same(rt,pool,parameterPool,"Character parameter setters changed shared pool metadata/masks");
            same(rt,rt.effectPoolBacking,parameterShared,"Character parameter setters changed shared values");
            // A complete commit must upload real GPU bytes and clear dirty
            // flags only after a successful native bind. Seed asymmetric finite
            // matrices whose fourth rows differ sharply from the first three.
            for(uint32_t i=0;i<1024;++i)PPC_STORE_U32(0x82D64080+4*i,std::bit_cast<uint32_t>((float(i)-513.0f)*0.125f));
            PPC_STORE_U32(0x50124,64);
            {EngineCpuCalls bones(entry,base);auto& c=bones.registers();c.lr=0x827071A0;c.r31.u32=owner;
                c.r28.u32=0x82D64080;c.r29.u32=0x50100;c.r30.u32=64;
                bones.invoke(0x826FDBE0,id,0x0040001C,0x82D64080,64);}
            auto commit=[&]{EngineCpuCalls upload(entry,base);upload.registers().lr=0x827071A8;upload.registers().r31.u32=owner;
                const auto before=abi(upload.registers());upload.invoke(0x826B3980,wrapper);
                need(abi(upload.registers())==before,"Character constant commit changed original ABI");};
            commit();std::array<uint32_t,976> expectedGpu{};
            parameters=effects.view(id).defaultVectorWords;const auto shared=effects.sharedParameterStorage(id,0x00040001);
            for(uint32_t i=0;i<16;++i){expectedGpu[i]=PPC_LOAD_U32(shared+4*i);expectedGpu[48+i]=parameters[20+i];}
            for(uint32_t i=0;i<4;++i)expectedGpu[160+i]=parameters[64+i];
            for(uint32_t bone=0;bone<64;++bone)for(uint32_t r=0;r<3;++r)for(uint32_t col=0;col<4;++col)
                expectedGpu[208+12*bone+4*r+col]=PPC_LOAD_U32(0x82D64080+64*bone+16*col+4*r);
            const auto observedGpu=effects.readbackShadowConstants();
            need(observedGpu==expectedGpu,"Actual GPU character constant registers or bone vector packing differ");
            // Independent affine geometry check: the transformed origin must
            // equal the position vector, and each transformed unit axis must
            // equal that position plus the corresponding source basis vector.
            for(uint32_t bone=0;bone<64;++bone)for(uint32_t component=0;component<3;++component) {
                const auto g=208+12*bone+4*component;
                const auto position=std::bit_cast<float>(PPC_LOAD_U32(0x82D64080+64*bone+48+4*component));
                need(std::bit_cast<float>(observedGpu[g+3])==position,"Bone GPU transform lost its position vector");
                for(uint32_t axis=0;axis<3;++axis) {
                    const auto basis=std::bit_cast<float>(PPC_LOAD_U32(0x82D64080+64*bone+16*axis+4*component));
                    need(std::bit_cast<float>(observedGpu[g+axis])+std::bit_cast<float>(observedGpu[g+3])==basis+position,
                         "Bone GPU transform changed an affine basis direction");
                }
            }
            need(effects.privateModifiedMask(id)==std::array<uint8_t,128>{},"Character constant upload did not clear private dirtiness");
            for(uint32_t i=0;i<128;++i)need(!PPC_LOAD_U8(pool+i),"Character constant upload did not clear original shared dirty line");
            {EngineCpuCalls flag(entry,base);flag.registers().lr=0x827071CC;flag.registers().r31.u32=owner;
                flag.invoke(0x823C8EB0,wrapper,0x00300014,0x100);}
            std::array<uint8_t,128> flagDirty{};flagDirty[1]=0x20;
            need(effects.privateModifiedMask(id)==flagDirty&&effects.readbackShadowConstants()==expectedGpu,
                 "Character flag dirtiness or immutable committed GPU snapshot differs");
            commit();expectedGpu[160]=0;
            need(effects.readbackShadowConstants()==expectedGpu,"Repeated character constant upload retained stale skinning flag");
            // Failed upload must retain the previous real buffer and dirtiness.
            PPC_STORE_U32(0x82D64080,0x7FC12345);PPC_STORE_U32(0x50124,1);
            {EngineCpuCalls bones(entry,base);auto& c=bones.registers();c.lr=0x827071A0;c.r31.u32=owner;
                c.r28.u32=0x82D64080;c.r29.u32=0x50100;c.r30.u32=1;
                bones.invoke(0x826FDBE0,id,0x0040001C,0x82D64080,1);}
            std::array<uint8_t,128> boneDirty{};boneDirty[1]=0x02;
            need(effects.privateModifiedMask(id)==boneDirty,"Partial bone update dirtied another leaf");
            bool nonfinite=false;try{commit();}catch(const Graphics::Error&){nonfinite=true;}
            need(nonfinite&&effects.privateModifiedMask(id)==boneDirty&&effects.readbackShadowConstants()==expectedGpu,
                 "Rejected nonfinite character upload changed native constants or dirty flags");
            std::memcpy(rt.pointer(0x82D64080,4096,true),boneSource.data(),4096);
            need(driver.readbackColor(colors[selector])==color&&driver.readbackDepth(depths[selector])==depth,"Shader activation issued an unintended clear or draw");
            // A rejected technique transition must retain the active opaque
            // shader/commit as well as a deliberately pending bone dirty bit.
            PPC_STORE_U32(owner+0xAC,1);
            {
                const AlphaUnchanged unchanged(rt,owner);EngineCpuCalls alpha(entry,base);bool nonempty=false;
                try{alpha.invoke(0x82706130,owner);}catch(const Failure& e){
                    nonempty=std::string(e.what()).find("Character shadow alpha queue is not yet qualified")!=std::string::npos&&
                        alpha.registers().lastFunction==0x826B5FC0&&uint32_t(alpha.registers().lr)==0x8270614C;
                }
                need(nonempty,"Active character pass accepted a nonempty alpha queue");unchanged.verify();
                need(effects.readbackShadowConstants()==expectedGpu,"Rejected alpha transition lost the actual opaque shader/constant binding");
            }
            PPC_STORE_U32(owner+0xAC,0);
            // Exercise the genuine82706378 submesh loop with independently
            // authored CPU mesh bytes. Only its four SDK calls are replaced.
            const auto savedFrame=snapshot(rt,frame+0x10,64);
            const auto savedShared=snapshot(rt,shared,64);
            for(uint32_t row=0;row<4;++row)for(uint32_t lane=0;lane<4;++lane) {
                const auto value=row==lane?0x3F800000u:0u;
                PPC_STORE_U32(frame+0x10+16*row+4*lane,value);PPC_STORE_U32(shared+16*row+4*lane,value);
            }
            {EngineCpuCalls world(entry,base);world.invoke(0x827055E0,owner,cameras[0]);}
            for(uint32_t bone=0;bone<64;++bone)for(uint32_t row=0;row<4;++row)for(uint32_t lane=0;lane<4;++lane)
                PPC_STORE_U32(0x82D64080+64*bone+16*row+4*lane,row==lane?0x3F800000:0);
            PPC_STORE_U32(0x50124,64);
            {EngineCpuCalls bones(entry,base);auto& c=bones.registers();c.lr=0x827071A0;c.r31.u32=owner;
                c.r28.u32=0x82D64080;c.r29.u32=0x50100;c.r30.u32=64;
                bones.invoke(0x826FDBE0,id,0x0040001C,0x82D64080,64);}
            {EngineCpuCalls flag(entry,base);flag.registers().lr=0x8270716C;flag.registers().r31.u32=owner;
                flag.invoke(0x823C8EB0,wrapper,0x00300014,1);}
            commit();
            constexpr uint32_t meta=0x50500,object=0x50600,geometry=0x50700,elements=0x50800,vertices=0x50900,
                indices=0x50A00,submeshes=0x50B00,cache=0x50C00;
            std::memset(rt.pointer(meta,0x800,true),0,0x800);
            PPC_STORE_U32(meta+12,geometry);PPC_STORE_U32(meta+16,2);PPC_STORE_U32(meta+20,submeshes);PPC_STORE_U32(meta+36,1);
            PPC_STORE_U32(geometry,4*48);PPC_STORE_U32(geometry+4,48);PPC_STORE_U32(geometry+8,5);
            PPC_STORE_U32(geometry+12,elements);PPC_STORE_U32(geometry+16,vertices);PPC_STORE_U32(geometry+20,8);
            PPC_STORE_U32(geometry+24,1);
            PPC_STORE_U32(geometry+28,indices);PPC_STORE_U32(geometry+48,cache);
            PPC_STORE_U32(geometry+0x50,vertices|3);PPC_STORE_U32(geometry+0x70,indices);PPC_STORE_U32(geometry+0x74,8);
            PPC_STORE_U32(geometry+0x54,2);PPC_STORE_U32(geometry+0x58,0x20000002);
            PPC_STORE_U32(cache+4,0x50C40);
            const uint32_t records[][3]={{0,0x002A23B9,0},{16,0x002C23A5,0x00050000},
                {24,0x001A2286,0x00020000},{28,0x001A23A6,0x00010000},{0x00FF0000,UINT32_MAX,0}};
            for(uint32_t i=0;i<5;++i)for(uint32_t j=0;j<3;++j)PPC_STORE_U32(elements+12*i+4*j,records[i][j]);
            const float xy[][2]={{-.5f,-.5f},{-.5f,.5f},{.5f,-.5f},{.5f,.5f}};
            for(uint32_t i=0;i<4;++i) {
                PPC_STORE_U32(vertices+48*i,std::bit_cast<uint32_t>(xy[i][0]));PPC_STORE_U32(vertices+48*i+4,std::bit_cast<uint32_t>(xy[i][1]));
                PPC_STORE_U32(vertices+48*i+8,0x3F000000);PPC_STORE_U32(vertices+48*i+28,0x3F800000);PPC_STORE_U16(indices+2*i,uint16_t(i));
            }
            for(uint32_t i=0;i<2;++i){PPC_STORE_U32(submeshes+36*i+12,6);PPC_STORE_U32(submeshes+36*i+24,4);}
            const auto meshBytes=snapshot(rt,meta,0x800);const auto drawsBefore=effects.shadowMeshDrawCount();
            const auto meshColorMask=driver.effectiveState().scalar(0xD4);
            driver.directScalar(base,0xD4,0); // Reached real character pass disables color; interrupted parent fixture did not.
            auto drawCharacter=[&]{EngineCpuCalls mesh(entry,base);mesh.registers().lr=0x827071BC;mesh.registers().r31.u32=owner;
                const auto before=abi(mesh.registers());mesh.invoke(0x82706378,owner,object,meta,0x82D64080);
                need(abi(mesh.registers())==before,"Original mesh loop changed nonvolatile ABI");};
            drawCharacter();need(effects.shadowMeshDrawCount()==drawsBefore+2,"Original submesh loop did not submit both native indexed draws");
            same(rt,meta,meshBytes,"Native mesh bindings changed original mesh data");
            auto renderedDepth=driver.readbackDepth(depths[selector]);
            float center{};std::memcpy(&center,renderedDepth.data()+(512*1024+512)*8,4);
            // The identity-projected quad has constant z, so the slope term is
            // zero and only the retained constant offset (D0) moves its depth.
            const float offset=std::bit_cast<float>(driver.effectiveState().scalar(0xD0)),expectedCenter=0.5f+offset;
            const bool colorKept=driver.readbackColor(colors[selector])==color,depthWritten=offset<0&&center>expectedCenter-0.000001f&&center<expectedCenter+0.000001f;
            if(!depthWritten||!colorKept)std::fprintf(stderr,"[CHARACTER MESH] center depth=%.9g expected=%.9g color_preserved=%u\n",center,expectedCenter,unsigned(colorKept));
            need(depthWritten&&colorKept,"Original character mesh did not write depth while preserving color");
            PPC_STORE_U32(submeshes+24,5);bool invalidMesh=false;
            try{drawCharacter();}catch(const std::exception&){invalidMesh=true;}
            need(invalidMesh&&effects.shadowMeshDrawCount()==drawsBefore+2&&driver.readbackDepth(depths[selector])==renderedDepth,
                 "Out-of-range original submesh submitted a partial draw");
            const auto materialPoolBefore=PPC_LOAD_U32(0x82D6D814);
            const auto queueBeforeStatic=snapshot(rt,owner+0xA8,12);
            staticShadowMesh(rt,entry,owner,cameras[selector],colors[selector],depths[selector],frame);
            need(PPC_LOAD_U32(0x82D6D814)==materialPoolBefore,"Static fixture did not restore the original global material pool");
            same(rt,owner+0xA8,queueBeforeStatic,"Static fixture did not restore the original queue ownership/count/capacity");
            renderedDepth=driver.readbackDepth(depths[selector]);
            stage="genuine original2/64/65/255 character palettes and grouped submeshes";
            originalGroupedCharacter(rt,entry,owner,cameras[selector],depths[selector]);
            renderedDepth=driver.readbackDepth(depths[selector]);
            driver.directScalar(base,0xD4,meshColorMask);
            std::memcpy(rt.pointer(frame+0x10,64,true),savedFrame.data(),64);std::memcpy(rt.pointer(shared,64,true),savedShared.data(),64);
            std::memcpy(rt.pointer(0x82D64080,4096,true),boneSource.data(),4096);
            stage="completed character depth original reset and logical end";
            resetShadowSelection(rt,entry,owner,cameras[selector],colors[selector],depths[selector],0x0007FFFC);
            // Execute the full original deferred loop after the manual mesh
            // fixture. Count zero still binds the real alpha pair and writes
            // both booleans; its original branch performs no constant commit.
            need(PPC_LOAD_U32(owner+0x5E0)==0x0003FFFC&&PPC_LOAD_U32(owner+0x678)==0x00340016&&
                 PPC_LOAD_U32(owner+0x680)==0x002C0015&&!PPC_LOAD_U32(owner+0xAC),"Original empty-alpha inputs differ");
            auto alphaParameters=effects.view(id).defaultVectorWords;alphaParameters[0x11*4]=0x3F800000;
            auto alphaTyped=snapshot(rt,owner,0x6C0);alphaTyped[0xD4]=0x3F;alphaTyped[0xD5]=0x80;alphaTyped[0xD6]=alphaTyped[0xD7]=0;
            const auto queue=snapshot(rt,owner+0xA8,12);const auto alphaDraws=effects.shadowMeshDrawCount();
            const auto receiver=effects.sharedParameterStorage(id,0x002C0015);
            need(receiver==PPC_LOAD_U32(pool+0x108)+0x130&&receiver>=rt.effectPoolBacking&&
                 uint64_t(receiver)+16<=uint64_t(rt.effectPoolBacking)+0x2A4,"Shared receiver storage is not original pool slot13");
            auto alphaShared=snapshot(rt,rt.effectPoolBacking,0x2A4);const auto receiverOffset=receiver-rt.effectPoolBacking;
            alphaShared[receiverOffset]=0x3F;alphaShared[receiverOffset+1]=0x80;alphaShared[receiverOffset+2]=alphaShared[receiverOffset+3]=0;
            auto alphaPool=snapshot(rt,pool,512);
            // FX is attached to this genuine pool. Original826B1FF0..204C
            // seeds its dirty line only when the global shared-cache switch is
            // zero; FX+124=1 writes one16-byte block after clearing128 bytes.
            if(!PPC_LOAD_U32(0x82D00F80)) {
                std::fill_n(alphaPool.begin(),128,uint8_t(0));std::fill_n(alphaPool.begin(),16,uint8_t(0xFF));
            }
            alphaPool[1]|=0x20; // Original82705AA0 always dirties receiver leaf10.
            const auto samplerRows=PPC_LOAD_U32(view.cache+12);
            need(PPC_LOAD_U32(view.cache+20)==6,"Alpha pass sampler cache extent differs");
            std::array<uint32_t,6> priorSamplers{},priorNativeSamplers{},savedSamplers{},activeSamplers{},activeNativeSamplers{};
            constexpr std::array<uint32_t,6> samplerIds{0,4,8,0x10,0x14,0x18},samplerValues{0,0,0,0,0,2};
            for(uint32_t i=0;i<6;++i) {
                need(!PPC_LOAD_U32(samplerRows+16*i)&&PPC_LOAD_U32(samplerRows+16*i+4)==samplerIds[i]/4+1&&
                     PPC_LOAD_U32(samplerRows+16*i+8)==samplerValues[i],"Original alpha sampler stage,selector or literal differs");
                priorSamplers[i]=cpu.invoke(0x826B79B0,PPC_LOAD_U32(samplerRows+16*i),PPC_LOAD_U32(samplerRows+16*i+4));
                priorNativeSamplers[i]=driver.effectiveState().sampler(0,samplerIds[i]);
            }
            const auto alphaClears=driver.cameraClearCount(),alphaBorders=shadows.scissorCount();
            {EngineCpuCalls alpha(entry,base);alpha.registers().lr=0x82707040;const auto before=abi(alpha.registers());
                alpha.invoke(0x82706130,owner);need(abi(alpha.registers())==before,"Original empty-alpha loop changed nonvolatile ABI");}
            need(PPC_LOAD_U32(manager+4)==wrapper&&PPC_LOAD_U32(manager+8)==id&&PPC_LOAD_U32(manager+12)==0x0003FFFC&&
                 PPC_LOAD_U32(wrapper+0x2C)==view.cache,"Empty-alpha loop did not select its manager/cache after depth cleanup");
            need(effects.compiledShaderCount(id)==3,"Empty-alpha activation omitted its real shader pair or compiled another pass");
            need(effects.view(id).defaultVectorWords==alphaParameters,"Empty-alpha Boolean write changed private neighbor values");
            std::array<uint8_t,128> alphaDirty{};std::fill_n(alphaDirty.begin(),16,uint8_t(0xFF));
            need(effects.privateModifiedMask(id)==alphaDirty,"Empty-alpha loop committed constants or seeded an incorrect dirty extent");
            same(rt,owner,alphaTyped,"Empty-alpha loop changed typed data beyond receiver D4");
            same(rt,owner+0xA8,queue,"Empty-alpha loop changed queue storage/count/capacity");
            same(rt,pool,alphaPool,"Empty-alpha shared dirty initialization or receiver bit differs");
            same(rt,rt.effectPoolBacking,alphaShared,"Empty-alpha receiver write changed shared neighbor values");
            need(effects.shadowMeshDrawCount()==alphaDraws&&driver.cameraClearCount()==alphaClears&&shadows.scissorCount()==alphaBorders&&
                 driver.readbackDepth(depths[selector])==renderedDepth&&driver.readbackColor(colors[selector])==color,
                 "Empty-alpha loop drew,cleared or changed the rendered shadow pixels");
            for(uint32_t i=0;i<6;++i) {
                savedSamplers[i]=PPC_LOAD_U32(samplerRows+16*i+12);
                activeSamplers[i]=cpu.invoke(0x826B79B0,PPC_LOAD_U32(samplerRows+16*i),PPC_LOAD_U32(samplerRows+16*i+4));
                activeNativeSamplers[i]=driver.effectiveState().sampler(0,samplerIds[i]);
            }
            copyShadow(selector,false); // The actual alpha effect must end before copying.
            // The rejected whole copy still executes823EE8F8 ->823EDD38,
            // which invalidates the CPU attachment cache while retaining the
            // native shadow targets. Re-enter the real camera callbacks before
            // testing reset from its already selected target roles.
            {EngineCpuCalls reselect(entry,base);const auto saved=abi(reselect.registers());
                need(reselect.invoke(0x823F1A08,cameras[selector])==cameras[selector]&&
                     reselect.invoke(0x823F1A18,cameras[selector])==cameras[selector]&&abi(reselect.registers())==saved,
                     "Rejected shadow copy could not re-enter its original camera");}
            need(PPC_LOAD_U32(0x82D0CF5C)==colors[selector]&&PPC_LOAD_U32(0x82D0CF58)==depths[selector],
                 "Original shadow camera re-entry did not restore its selected target roles");
            stage="completed character alpha original reset and logical end";
            resetShadowSelection(rt,entry,owner,cameras[selector],colors[selector],depths[selector],0x0003FFFC);
            need(!PPC_LOAD_U32(manager+4)&&PPC_LOAD_U32(manager+8)==id&&!PPC_LOAD_U32(manager+12)&&!PPC_LOAD_U32(wrapper+0x2C),
                 "Original alpha end did not clear selection while retaining manager identity");
            need(effects.view(id).defaultVectorWords==alphaParameters&&effects.privateModifiedMask(id)==alphaDirty,
                 "Alpha end reset private parameters or cleared uncommitted dirtiness");
            same(rt,owner,alphaTyped,"Alpha end reset typed receiver/queue data");same(rt,pool,alphaPool,"Alpha end cleared shared dirtiness");
            same(rt,rt.effectPoolBacking,alphaShared,"Alpha end reset the shared receiver");
            need(effects.shadowMeshDrawCount()==alphaDraws&&driver.cameraClearCount()==alphaClears&&shadows.scissorCount()==alphaBorders&&
                 driver.readbackDepth(depths[selector])==renderedDepth&&driver.readbackColor(colors[selector])==color,
                 "Alpha end drew,cleared or changed the rendered shadow pixels");
            // Original826B36BC/36D4 saves the application getter at row+C,
            // not the direct SDK state. End826B3850 passes force=false:
            // 82723CC4..CD4 skips the SDK setter only on a cache hit; a miss
            // calls it at82723D04. Begin's direct literal already covers hits,
            // so both paths finish at the saved application value, which can
            // differ from the native value before begin. Nothing is deferred.
            bool samplerSaveApply=true,samplerRestore=true;
            for(uint32_t i=0;i<6;++i) {
                const auto at=samplerRows+16*i,stage=PPC_LOAD_U32(at),selector=PPC_LOAD_U32(at+4);
                const auto restored=cpu.invoke(0x826B79B0,stage,selector),native=driver.effectiveState().sampler(stage,samplerIds[i]);
                const auto savedAfter=PPC_LOAD_U32(at+12);
                std::fprintf(stderr,"[TEST ALPHA SAMPLER] row=%u stage=%u selector=%X sdk=%X literal=%08X "
                    "before_cached=%08X before_native=%08X saved=%08X active_cached=%08X active_native=%08X "
                    "after_cached=%08X after_native=%08X saved_after=%08X expected_cached=%08X expected_native=%08X restore_sdk=%u\n",
                    i,stage,selector,samplerIds[i],samplerValues[i],priorSamplers[i],priorNativeSamplers[i],savedSamplers[i],
                    activeSamplers[i],activeNativeSamplers[i],restored,native,savedAfter,priorSamplers[i],priorSamplers[i],
                    unsigned(priorSamplers[i]!=samplerValues[i]));
                samplerSaveApply&=savedSamplers[i]==priorSamplers[i]&&activeSamplers[i]==samplerValues[i]&&activeNativeSamplers[i]==samplerValues[i];
                samplerRestore&=restored==priorSamplers[i]&&native==priorSamplers[i]&&savedAfter==savedSamplers[i];
            }
            need(samplerSaveApply,"Original alpha sampler save or literal application differs (see per-row diagnostics)");
            need(samplerRestore,"Original alpha sampler restore differs from saved application values (see per-row diagnostics)");
            rejects([&]{EngineCpuCalls flag(entry,base);flag.registers().lr=0x8270716C;flag.registers().r31.u32=owner;
                flag.invoke(0x823C8EB0,wrapper,0x00300014,1);},"Ended shadow pass still accepts character parameters");
            rejects([&]{EngineCpuCalls flag(entry,base);flag.registers().lr=0x8270615C;flag.registers().r31.u32=owner;
                flag.invoke(0x823C8EB0,wrapper,0x00340016,1);},"Ended alpha pass still accepts alpha parameters");
            for(size_t i=0;i<3;++i)need(cpu.invoke(0x826B7940,selectors[i])==prior[i],"Original character state cache did not restore prior values");
        }
        if(!selector) {
            need(shadows.copyCount()==copiesBefore&&shadows.readback(PPC_LOAD_U32(owner+0xF0))==copy0&&
                 shadows.readback(PPC_LOAD_U32(owner+0xF4))==copy1,"Pre-copy shadow operations changed the sampled depth destinations");
            const auto pixels=driver.readbackDepth(depths[0]);uint32_t occupied=0;bool stencil=true;
            for(size_t at=0;at<pixels.size();at+=8){float d{};std::memcpy(&d,pixels.data()+at,4);occupied+=d>0;stencil&=pixels[at+4]==0x68;}
            need(occupied&&stencil,"Depth-copy fixture lacks actual nonzero mesh depth and original nonzero stencil");
        }
        // Follow original camera end; qualify the copy against the actual
        // retained attachments rather than requiring global camera fields zero.
        need(cpu.invoke(0x823F1A08,cameras[selector])==cameras[selector],"Interrupted fixture camera cleanup failed");
        copyShadow(selector,false,[&](PPCContext&,uint32_t& destination){destination=PPC_LOAD_U32(owner+0xF0+4*(selector^1));});
        copyShadow(selector,false,[&](PPCContext& c,uint32_t&){c.r24.u32=cameras[selector^1];});
        copyShadow(selector,false,[](PPCContext& c,uint32_t&){c.r26.u32=2;});
        copyShadow(selector,false,[](PPCContext& c,uint32_t&){c.lr=0x827073C0;});
        copyShadow(selector,false,[&](PPCContext& c,uint32_t&){PPC_STORE_U32(c.r1.u32,c.r1.u32+0xD0);});
        copyShadow(selector,true);
        for(size_t i=0;i<fields.size();++i)cpu.invoke(0x826B7968,fields[i],stateBefore[i],0);
    }
    // Also finish both genuine empty parents, without the alpha entry observer.
    // They construct both frames, end camera/FX and choose F0/F4 themselves.
    // Distinct stencil values expose a stale copy or an accidental sibling source.
    for(uint32_t selector=0;selector<2;++selector) {
        const auto destination=PPC_LOAD_U32(owner+0xF0+4*selector),sibling=PPC_LOAD_U32(owner+0xF0+4*(selector^1));
        const auto siblingBefore=shadows.readback(sibling),otherDepth=driver.readbackDepth(depths[selector^1]);
        const auto count=shadows.copyCount(),draws=driver.effects().shadowMeshDrawCount(),clears=driver.cameraClearCount(),scissors=shadows.scissorCount();
        PPC_STORE_U32(0x82D0CB14,0x91+selector);
        {EngineCpuCalls parent(entry,base);const auto before=abi(parent.registers());
            parent.invoke(0x82707220,owner,selector);need(abi(parent.registers())==before,"Complete original shadow parent changed nonvolatile ABI");}
        need(shadows.copyCount()==count+1&&driver.effects().shadowMeshDrawCount()==draws&&driver.cameraClearCount()==clears+1&&
             shadows.scissorCount()==scissors+1,"Complete empty parent did not perform exactly one clear,scissor and copy without drawing");
        need(!PPC_LOAD_U32(manager+4)&&!PPC_LOAD_U32(manager+12)&&driver.cameraBinding().camera==cameras[selector],
             "Complete parent did not end FX while retaining its actual source binding");
        for(uint32_t at=0x82D0CF58;at<=0x82D0CF68;at+=4)
            need(!PPC_LOAD_U32(at),"Complete parent republished CPU attachments after original copy-helper invalidation");
        depthPixels(driver,depths[selector],uint8_t(0x91+selector));
        sameDepthStencil(shadows.readback(destination),driver.readbackDepth(depths[selector]),"Complete parent copied stale or wrong depth/stencil");
        need(shadows.view(destination).phase==EngineShadowTextures::Phase::Uploaded&&shadows.readback(sibling)==siblingBefore,
             "Complete parent failed to publish the selected copy or changed its sibling");
        sameDepthStencil(driver.readbackDepth(depths[selector^1]),otherDepth,"Complete parent changed the other camera depth");
        for(uint32_t slot=0;slot<2;++slot)need(driver.readbackColor(colors[slot])==colorBefore[slot],"Complete empty parent changed camera color");
        expectedCopies[selector]=shadows.readback(destination);
    }
    need(driver.readbackColor(PPC_LOAD_U32(0x82D0CB00))==workingColor && driver.readbackDepth(PPC_LOAD_U32(0x82D0CAFC))==workingDepth,
         "Shadow operations changed game working targets");
    need(shadows.copyCount()==copiesBefore+4&&shadows.readback(PPC_LOAD_U32(owner+0xF0))==expectedCopies[0]&&
         shadows.readback(PPC_LOAD_U32(owner+0xF4))==expectedCopies[1],"Selected shadow depth copies were not retained");
    struct CacheOwner {uint32_t id,cache,bytes;};std::vector<CacheOwner> cacheOwners;
    const auto retainedPool=PPC_LOAD_U32(root),retainedBacking=rt.effectPoolBacking;
    if(staticTangent) {
        auto& effects=driver.effects();need(effects.count()==25&&!tangentDeclarations.empty(),"Original tangent lifecycle missed its declaration/FX owners");
        for(uint32_t row=0;row<25;++row) {
            const auto wrapper=PPC_LOAD_U32(table+16*row+8),effect=PPC_LOAD_U32(wrapper+0x10);const auto view=effects.view(effect);
            need(view.cache&&rt.engineAudio->allocationGeneration(view.cache,view.cacheBytes),"Original tangent FX cache lacks allocator ownership");
            cacheOwners.push_back({effect,view.cache,view.cacheBytes});
        }
        cpu.invoke(0x82700A78);
        for(const auto declaration:tangentDeclarations)rejects([&]{rt.engineAudio->allocationGeneration(declaration,0x50);},"Original tangent declaration was not retired");
    }
    cpu.invoke(0x823EE6C8,loading);cpu.invoke(0x82701118,table,25);
    for(const auto c:cameras) {need(!shadows.ownsCamera(c),"Retired shadow owner still authorizes camera");rejects([&]{driver.selectCamera(base,c);},"Retired shadow camera selected");}
    need(!shadows.count(),"Original shadow textures not retired");cpu.invoke(0x826B7600,manager,1);PPC_STORE_U32(0x82D0CB14,oldStencil);
    same(rt,source,original,"Shadow operations changed original effect bytes");
    if(staticTangent) {
        auto& effects=driver.effects();effects.requireReleased();effects.requirePoolReleased(retainedPool);driver.quadDeclarations().requireReleased();
        for(const auto cacheOwner:cacheOwners) {
            rejects([&]{effects.view(cacheOwner.id);},"Retired tangent FX identity remained usable");
            rejects([&]{effects.originalBytes(cacheOwner.id);},"Retired tangent FX source remained borrowable");
            rejects([&]{effects.compiledShaderCount(cacheOwner.id);},"Retired tangent FX shader remained usable");
            rejects([&]{rt.engineAudio->allocationGeneration(cacheOwner.cache,cacheOwner.bytes);},"Retired tangent FX CPU cache retained ownership");
        }
        same(rt,table,originalTable,"Original tangent FX table was not restored");
        need(PPC_LOAD_U32(root)==retainedPool&&rt.effectPoolBacking==retainedBacking&&PPC_LOAD_U32(retainedPool+0x188)==1,
             "Tangent lifecycle released the root-owned shared pool");
        std::printf("AUDIT_SHADOW_TANGENT_LIFECYCLE source=%08X stride=40 tangent=002A2187 original_static_entry=82707678 create=passed use=passed declaration_release=passed fx_release=passed cpu_cache_release=passed stale_use=passed malformed_scope=existing_original_queue_range backend_mesh_cache=owner_resident full_gpu_retirement=unproven\n",source);
    }
}
}
PPC_FUNC(sub_82706130) {
    if(alphaEntryObserver)alphaEntryObserver(ctx,base);
    __imp__sub_82706130(ctx,base);
}
PPC_FUNC(sub_826FE7C8){paletteForward(0x826FE7C8,ctx,base,__imp__sub_826FE7C8);}
PPC_FUNC(sub_826FE710){paletteForward(0x826FE710,ctx,base,__imp__sub_826FE710);}
PPC_FUNC(sub_826FDBE0){paletteForward(0x826FDBE0,ctx,base,__imp__sub_826FDBE0);}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==2||(argc==3&&std::string(argv[2])=="tangent"),"Original image and optional tangent case required");staticTangent=argc==3;
        Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original startup boundary missing");run(rt,entry);
        std::printf("PASS original shadows camera passes:%zu checks; real constructor,selection,clear,parameters,indexed character/static meshes,original material queue,empty-alpha closure,depth/alpha reset and logical end,depth copies,rejection,pixels,retirement\n",checks);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL shadows camera passes:%zu checks:%s\n",checks,e.what());return 1;}
}
