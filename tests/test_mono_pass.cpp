// Burp post-filter mask: the whole original dispatcher selects static and skinned
// mono draws from real catalog owners. Observers always execute original bodies.
#include "effect_catalog_lifecycle_helpers.h"
#include "renderer/engine_state.h"
#include <bit>
#include <functional>

extern "C" void __imp__sub_82700318(PPCContext&,uint8_t*);
extern "C" void __imp__sub_826FF4C8(PPCContext&,uint8_t*);
extern "C" void __imp__sub_826FE7C8(PPCContext&,uint8_t*);
extern "C" void __imp__sub_826FE710(PPCContext&,uint8_t*);
extern "C" void __imp__sub_823C8EB0(PPCContext&,uint8_t*);

namespace {
std::function<void(uint32_t,PPCContext&,uint8_t*,bool)> entryObserver;
struct Observation {
    explicit Observation(decltype(entryObserver) observer){need(!entryObserver,"Nested mono observer");entryObserver=std::move(observer);}
    ~Observation(){entryObserver={};}
};
void forward(uint32_t pc,PPCContext& c,uint8_t* base,void(*body)(PPCContext&,uint8_t*)) {
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
    for(size_t i=0;i<gpr.size();++i){gpr[i]->u64=0xA510000012340000ull+i;fpr[i]->f64=double(i)+.125;}
}
using Matrix=std::array<float,16>;
Matrix translated(float tx=0) {return {1,0,0,0,0,1,0,0,0,0,1,0,tx,0,0,1};}
void writeMatrix(uint8_t* base,uint32_t address,const Matrix& value) {
    for(uint32_t i=0;i<16;++i)PPC_STORE_U32(address+4*i,std::bit_cast<uint32_t>(value[i]));
}
constexpr uint32_t area=0x60000,packet=area,metadata=0x60100,object=0x61000,data=0x63000,
    offsets=0x64000,geometry=0x65000,elements=0x65100,vertices=0x65200,indices=0x65400,
    submeshes=0x65500,declCache=0x65600,materials=0x65700,objectFrame=0x65800,
    hierarchy=0x66000,skinPlugin=0x66100,bindMatrices=0x66200,boneMatrices=0x66300,boneMap=0x66400,boneGroups=0x66500;
void geometryFixture(Runtime& rt,uint32_t camera,uint32_t owner) {
    auto* base=rt.base;rt.map(area,0x10000,true,"original mono CPU geometry fixture");
    std::memset(rt.pointer(area,0x10000,true),0,0x10000);
    PPC_STORE_U32(packet,metadata);PPC_STORE_U32(packet+4,object);PPC_STORE_U32(packet+8,camera);
    PPC_STORE_U8(packet+12,1);PPC_STORE_U32(packet+0x18,owner);PPC_STORE_U32(packet+0x1C,owner);
    PPC_STORE_U32(object,0x01000500);PPC_STORE_U32(object+4,objectFrame);
    PPC_STORE_U32(object+8,object+8);PPC_STORE_U32(object+12,object+8);
    PPC_STORE_U32(object+16,0x823CD3D8);PPC_STORE_U32(object+24,data);PPC_STORE_U32(data+0x24,offsets);
    PPC_STORE_U32(objectFrame+0xA0,objectFrame);writeMatrix(base,objectFrame+0x10,translated());
    PPC_STORE_U32(metadata,0x00030002);PPC_STORE_U32(metadata+4,0xB5F8FBF2);
    PPC_STORE_U32(metadata+12,geometry);PPC_STORE_U32(metadata+16,2);PPC_STORE_U32(metadata+20,submeshes);PPC_STORE_U32(metadata+24,1);
    PPC_STORE_U32(geometry+16,vertices);PPC_STORE_U32(geometry+20,16);PPC_STORE_U32(geometry+24,1);
    PPC_STORE_U32(geometry+28,indices);PPC_STORE_U32(geometry+0x30,declCache);
    PPC_STORE_U32(geometry+0x50,vertices|3);PPC_STORE_U32(geometry+0x54,2);PPC_STORE_U32(geometry+0x58,0x20000002);
    PPC_STORE_U32(geometry+0x70,indices);PPC_STORE_U32(geometry+0x74,16);PPC_STORE_U32(declCache+4,declCache+0x40);
    for(uint32_t q=0;q<2;++q) {
        PPC_STORE_U32(submeshes+36*q,q?7:2);PPC_STORE_U32(submeshes+36*q+12,6);
        PPC_STORE_U32(submeshes+36*q+20,4*q);PPC_STORE_U32(submeshes+36*q+24,4);
        for(uint32_t i=0;i<4;++i)PPC_STORE_U16(indices+2*(4*q+i),uint16_t(4*q+i));
    }
    PPC_STORE_U32(offsets+8,0x30);PPC_STORE_U32(offsets+28,0x70);
    PPC_STORE_U16(materials+0x30,4);PPC_STORE_U16(materials+0x70,0x20);
    const auto atomicOffset=PPC_LOAD_U32(0x82E27994),skinOffset=PPC_LOAD_U32(0x82E27998);
    need(atomicOffset>=0x28&&atomicOffset<0x1000&&skinOffset>0x24&&skinOffset<0x1000,
         "Original skeleton plugin offsets overlap fixture fields");
    need(PPC_LOAD_U32(0x82CF05EC)<0x1000&&PPC_LOAD_U32(0x82D6CAB0)<0x1000&&
         PPC_LOAD_U32(0x82D6CAB0)!=skinOffset&&PPC_LOAD_U32(0x82D6CAB0)!=0x24,"Original auxiliary plugin offsets overlap fixture");
    PPC_STORE_U32(object+atomicOffset,hierarchy);PPC_STORE_U32(data+skinOffset,skinPlugin);
    PPC_STORE_U32(hierarchy+8,boneMatrices);PPC_STORE_U32(skinPlugin+12,bindMatrices);
    writeMatrix(base,bindMatrices,translated());writeMatrix(base,bindMatrices+64,translated());
    writeMatrix(base,boneMatrices,translated(-.5f));writeMatrix(base,boneMatrices+64,translated(.5f));
    PPC_STORE_U8(boneMap,1);PPC_STORE_U8(boneMap+1,0);
}
void layout(uint8_t* base,bool skinned) {
    const uint32_t stride=skinned?40:12,count=skinned?5:2;
    PPC_STORE_U32(metadata+0x24,skinned?2:0);PPC_STORE_U32(metadata+0x28,skinned?boneMap:0);
    PPC_STORE_U32(geometry,8*stride);PPC_STORE_U32(geometry+4,stride);PPC_STORE_U32(geometry+8,count);PPC_STORE_U32(geometry+12,elements);
    constexpr uint32_t skin[][3]={{0,0x002A23B9,0},{12,0x002C23A5,0x00050000},
        {20,0x001A2286,0x00020000},{24,0x001A23A6,0x00010000},{0x00FF0000,UINT32_MAX,0}};
    for(uint32_t row=0;row<count;++row)for(uint32_t lane=0;lane<3;++lane)
        PPC_STORE_U32(elements+12*row+4*lane,skin[!skinned&&row==1?4:row][lane]);
    for(uint32_t q=0;q<2;++q)for(uint32_t i=0;i<4;++i) {
        const uint32_t v=vertices+stride*(4*q+i);
        const float x=(q?.625f:-.125f)+.25f*float(i/2),y=i%2?-.25f:.25f;
        PPC_STORE_U32(v,std::bit_cast<uint32_t>(x));PPC_STORE_U32(v+4,std::bit_cast<uint32_t>(y));PPC_STORE_U32(v+8,0x3F000000);
        if(skinned) {
            PPC_STORE_U32(v+12,0);PPC_STORE_U32(v+16,0);PPC_STORE_U32(v+20,0x00000100);
            PPC_STORE_U32(v+24,0x3F400000);PPC_STORE_U32(v+28,0x3E800000);PPC_STORE_U32(v+32,0);PPC_STORE_U32(v+36,0);
        }
    }
}
uint32_t pixel(const std::vector<uint8_t>& bytes,uint32_t x,uint32_t y) {
    uint32_t value{};std::memcpy(&value,bytes.data()+4*(1280*y+x),4);return value;
}
void sameDepth(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b) {
    need(a.size()==size_t(1280)*720*8&&a.size()==b.size(),"Mono depth extent differs");
    bool match=true;for(size_t i=0;i<a.size();i+=8)match&=!std::memcmp(a.data()+i,b.data()+i,5);
    need(match,"Alpha mask changed depth or stencil");
}
// Test-only SDK header for the complete original CPU array setter. The actual
// serialized mono descriptors define layout; expected output is independently
// transposed. Production still uses native ownership and never creates this.
void originalPalette(Runtime& rt,const PPCContext& entry) {
    auto* base=rt.base;constexpr uint32_t body=0x8211F48C,fx=0x68000,cells=0x68200,input=0x67000,values=0x69000;
    const auto descriptors=body+PPC_LOAD_U32(body+0x108);
    const auto slots=PPC_LOAD_U32(body+0x118);need(slots>14&&slots<128,"Original mono descriptor extent differs");
    const auto originalDescriptors=snapshot(rt,descriptors,8*slots);
    for(uint32_t count:{2u,9u,64u}) {
        std::memset(rt.pointer(fx,0x200,true),0,0x200);
        std::memset(rt.pointer(values,4368,true),0xCD,4368);
        PPC_STORE_U32(fx+0x100,fx);PPC_STORE_U32(fx+0x108,descriptors);PPC_STORE_U32(fx+0x10C,cells);
        PPC_STORE_U32(fx+0x128,values);PPC_STORE_U32(fx+0x12C,cells+4);
        PPC_STORE_U32(cells,descriptors);PPC_STORE_U32(cells+4,values);
        for(uint32_t i=0;i<64*16;++i)PPC_STORE_U32(input+4*i,std::bit_cast<uint32_t>(.25f*float(i)-17));
        const auto inputBefore=snapshot(rt,input,4096);
        EngineCpuCalls cpu(entry,base);seedAbi(cpu.registers());const auto before=fullAbi(cpu.registers());
        cpu.invoke(0x826FD060,fx,0x00340016,input,count);
        need(fullAbi(cpu.registers())==before,"Original matrix array setter changed nonvolatile ABI");
        for(uint32_t i=0;i<68;++i)need(PPC_LOAD_U32(values+4*i)==0xCDCDCDCD,"Original matrix array setter changed preceding values");
        for(uint32_t bone=0;bone<64;++bone)for(uint32_t r=0;r<4;++r)for(uint32_t col=0;col<4;++col) {
            const auto expected=bone<count?std::bit_cast<uint32_t>(.25f*float(16*bone+4*col+r)-17):0xCDCDCDCDu;
            need(PPC_LOAD_U32(values+4*(68+16*bone+4*r+col))==expected,"Original mono palette transpose or untouched extent differs");
        }
        std::array<uint8_t,128> expectedDirty{};
        for(uint32_t i=11;i<11+count;++i)expectedDirty[i/8]|=uint8_t(0x80u>>(i%8));
        need(!std::memcmp(rt.pointer(fx,128,false),expectedDirty.data(),128),"Original palette setter dirty leaf extent differs");
        same(rt,input,inputBefore,"Original palette setter changed source matrices");
    }
    same(rt,descriptors,originalDescriptors,"Original palette setter changed immutable descriptors");
}
void run(Runtime& rt,const PPCContext& entry) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();EngineCpuCalls cpu(entry,base);
    stage="real mono catalog construction";
    constexpr std::array<std::array<uint32_t,2>,8> groupedInstructions={{{0x82700390,0x4199002C},
        {0x8270041C,0x809F0020},{0x82700424,0x807F001C},{0x8270042C,0x4BFFE2E5},
        {0x82700440,0x4BFFCC21},{0x82700450,0x4BCC8A61},{0x82700458,0x4851D749},{0x82700408,0x409A006C}}};
    for(const auto& row:groupedInstructions)need(PPC_LOAD_U32(row[0])==row[1],"Original mono group instruction changed");
    const auto context=PPC_LOAD_U32(0x82D5DA74),loading=PPC_LOAD_U32(0x82E07248);driver.requireContext(context);
    const auto options=cpu.registers().r1.u32+0x60;PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    const auto manager=cpu.invoke(0x8269BF70,0x260,options);cpu.invoke(0x826B6F60,manager,context);
    need(cpu.invoke(0x827019E8,table,25)==0,"Original catalog registration failed");cpu.invoke(0x826B7218,manager);
    uint32_t owner=0;
    for(uint32_t row=0;row<25;++row)if(PPC_LOAD_U32(table+16*row)==0x8211F480)
        owner=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(table+16*row+4));
    need(owner&&PPC_LOAD_U32(owner)==0x8215020C,"Catalog did not construct original mono typed owner");
    const auto id=PPC_LOAD_U32(owner+0x1C),wrapper=PPC_LOAD_U32(owner+0x18);const auto v=effects.view(id);
    need(v.source==0x8211F480&&v.defaultVectorWords.size()==1092&&PPC_LOAD_U32(owner+0xAC)==0x0003FFFC&&
         PPC_LOAD_U32(owner+0xB8)==0x00300014&&PPC_LOAD_U32(owner+0xBC)==0x00340016,"Original mono reflection differs");
    const auto source=snapshot(rt,0x8211F480,0x5310);cache(rt,cpu,id,0x8211F48C);queries(rt,cpu,id,0x8211F48C);
    const auto viewport=cpu.invoke(0x8269D788);need(viewport,"Missing original viewport manager");
    cpu.registers().f1.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12CC));
    cpu.registers().f2.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D0));
    cpu.registers().f3.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D4));
    cpu.registers().f4.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D8));
    cpu.invoke(0x8269D608,viewport,0);const auto camera=PPC_LOAD_U32(viewport+0x14),cameraFrame=PPC_LOAD_U32(camera+4);
    geometryFixture(rt,camera,owner);
    stage="original mono palette transpose and dirty bounds";originalPalette(rt,entry);
    {
        Restore flags(rt,0x82D6CCA8,4),nativeContext(rt,0x82D6D890,4),published(rt,0x82D63028,4),
            materialRoot(rt,0x82D6D814,4),stencil(rt,0x82D0CB14,4),dirtyGlobal(rt,0x82D00F80,4),
            viewMatrix(rt,0x82D0CA70,64),projection(rt,0x82CD1AB0,64),cameraMatrix(rt,cameraFrame+0x10,64);
        PPC_STORE_U32(0x82D6CCA8,2);PPC_STORE_U32(0x82D6D890,context);PPC_STORE_U32(0x82D63028,0);
        PPC_STORE_U32(0x82D6D814,materials);PPC_STORE_U32(0x82D00F80,0);PPC_STORE_U32(0x82D0CB14,0x5A);
        PPC_STORE_U32(area+0x80,0x00FF0000);
        need(cpu.invoke(0x823F1A18,camera)==camera,"Original main camera begin failed");
        writeMatrix(base,cameraFrame+0x10,translated());writeMatrix(base,0x82D0CA70,translated());writeMatrix(base,0x82CD1AB0,translated());
        const auto bound=driver.cameraBinding();need(bound.camera==PPC_LOAD_U32(0x82E3DD60),"Main camera global differs");
        constexpr std::array<std::array<uint32_t,2>,22> states={{{0x28,1},{0x2C,6},{0x30,0},{0x34,0},{0x3C,0},{0x60,0},{0x6C,0},
            {0xAC,0},{0xC0,1},{0xC4,UINT32_MAX},{0xC8,0},{0xCC,0},{0xD0,0},{0xD4,8},{0xE4,0},{0x130,1},
            {0x144,1},{0x148,1},{0x14C,0xFFFF},{0x150,0},{0xD8,0},{0xDC,0}}};
        for(const auto& row:states)driver.directScalar(base,row[0],row[1]);
        const auto cullSelector=PPC_LOAD_U32(0x82E06F80+0x38);cpu.invoke(0x826B7968,cullSelector,1,1);
        const auto priorCull=cpu.invoke(0x826B7940,cullSelector),priorNativeCull=driver.effectiveState().scalar(Graphics::ScalarState::Cull);
        const auto runPass=[&](bool skinned,uint32_t bones=2,bool allSkipped=false) {
            layout(base,skinned);PPC_STORE_U32(metadata+0x10,bones>64?3:2);
            PPC_STORE_U32(submeshes,2);PPC_STORE_U32(submeshes+36,7);
            if(skinned) {
                PPC_STORE_U32(metadata+0x24,bones);for(uint32_t i=0;i<bones;++i)PPC_STORE_U8(boneMap+i,uint8_t(1-(i&1)));
            }
            if(bones>64) {
                // First and third original rows draw; the middle flag20 row
                // remains skipped and deliberately has no matrix-group table.
                for(uint32_t rowIndex:{0u,2u}) {
                    const auto row=submeshes+36*rowIndex,table=boneGroups+16*rowIndex;
                    std::memset(rt.pointer(row,36,true),0,36);PPC_STORE_U32(row,allSkipped?7:2);
                    PPC_STORE_U32(row+12,6);PPC_STORE_U32(row+24,4);
                    PPC_STORE_U32(row+28,rowIndex?3:1);PPC_STORE_U32(row+32,table);
                    PPC_STORE_U8(table,uint8_t(bones-(rowIndex?1:3)));PPC_STORE_U8(table+1,rowIndex?1:2);
                    if(rowIndex){PPC_STORE_U8(table+2,uint8_t(bones-2));PPC_STORE_U8(table+3,1);
                        PPC_STORE_U8(table+4,uint8_t(bones));PPC_STORE_U8(table+5,0);}
                }
            }
            need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original main camera clear failed");
            const auto colorBefore=driver.readbackColor(bound.colorIdentity),depthBefore=driver.readbackDepth(bound.depthIdentity);
            const auto geometryBefore=snapshot(rt,geometry,0x700),frameBefore=snapshot(rt,objectFrame,0xB0),bonesBefore=snapshot(rt,bindMatrices,0x500);
            const auto draws=effects.monoMeshDrawCount(),clears=driver.cameraClearCount(),copies=driver.cameraCopyCount();
            std::array<uint32_t,5> calls{};uint32_t selectedGroups=0;
            Observation observation([&](uint32_t pc,PPCContext& c,uint8_t* memory,bool after) {
                need(memory==base,"Observer changed memory domain");
                if(!after) {
                    ++calls[pc==0x82700318?0:pc==0x826FF4C8?1:pc==0x826FE7C8?2:pc==0x826FE710?4:3];
                    if(pc==0x82700318)need(skinned&&uint32_t(c.lr)==0x82740BB8&&c.r3.u32==metadata&&c.r5.u32==owner,"Original mono skin branch differs");
                    if(pc==0x826FF4C8)need(!skinned&&uint32_t(c.lr)==0x82740BD8,"Original mono static branch differs");
                } else if(pc==0x826FE7C8) {
                    need(c.r3.u32==0x82D64080,"Original bone palette address differs");
                    for(uint32_t bone=0;bone<bones;++bone)for(uint32_t i=0;i<16;++i)
                        need(PPC_LOAD_U32(c.r3.u32+64*bone+4*i)==std::bit_cast<uint32_t>(translated((bone&1)?-.5f:.5f)[i]),
                             "Original bone mapping/matrix composition differs");
                } else if(pc==0x826FE710) {
                    ++selectedGroups;const auto table=PPC_LOAD_U32(c.r31.u32+32),groups=PPC_LOAD_U32(c.r31.u32+28);
                    const auto address=groups==1?0x82D64080+64*PPC_LOAD_U8(table):0x82D63070;
                    need(c.r3.u32==address&&PPC_LOAD_U32(c.r1.u32+0x50)==2,"Original mono group alias/copy/count differs");
                    for(uint32_t bone=0;bone<2;++bone)for(uint32_t lane=0;lane<16;++lane)
                        need(PPC_LOAD_U32(address+64*bone+4*lane)==std::bit_cast<uint32_t>(translated(bone?-.5f:.5f)[lane]),
                             "Original mono selected group has wrong joint matrices");
                }
            });
            EngineCpuCalls call(entry,base);seedAbi(call.registers());const auto before=fullAbi(call.registers());
            // Original mono returns E_FAIL after rendering; its public wrapper returns false.
            need(call.invoke(0x8273B4D0,packet)==0,"Original mono wrapper return changed");
            need(fullAbi(call.registers())==before,"Whole mono wrapper changed nonvolatile GPR/FPR,SP or LR");
            const uint32_t groupedDraws=allSkipped?0:2,drawCount=bones>64?groupedDraws:1;
            const std::array<uint32_t,5> expected=skinned?std::array<uint32_t,5>{1,0,1,bones>64?1+groupedDraws:2,bones>64?groupedDraws:0}:
                std::array<uint32_t,5>{0,1,0,1,0};
            need(calls==expected&&selectedGroups==(bones>64?groupedDraws:0),"Original mono branch omitted/repeated skin setup");
            need(effects.monoMeshDrawCount()==draws+drawCount,"Original mono material skip or native draw count differs");
            const auto values=effects.view(id).defaultVectorWords;need(values[64]==(skinned&&!allSkipped?0x3F800000u:0u),"Mono committed stale skin Boolean");
            need(effects.privateModifiedMask(id)==std::array<uint8_t,128>{},"Original mono commit left dirty leaves");
            if(skinned)for(uint32_t bone=0;bone<2;++bone)for(uint32_t r=0;r<4;++r)for(uint32_t col=0;col<4;++col)
                need(values[68+16*bone+4*r+col]==std::bit_cast<uint32_t>(translated(bone?-.5f:.5f)[4*col+r]),"Mono palette setter transpose differs");
            const auto color=driver.readbackColor(bound.colorIdentity);
            need(color.size()==size_t(1280)*720*4&&color.size()==colorBefore.size(),"Mono color extent differs");
            bool rgb=true;for(size_t i=0;i<color.size();i+=4){uint32_t oldValue{},newValue{};std::memcpy(&oldValue,colorBefore.data()+i,4);std::memcpy(&newValue,color.data()+i,4);rgb&=(oldValue&0x3FFFFFFF)==(newValue&0x3FFFFFFF);}
            need(rgb,"Mono alpha mask changed RGB");
            const uint32_t inside=skinned?800:640,outside=skinned?640:800;
            if(allSkipped)need(color==colorBefore,"Entirely skipped grouped mono loop drew pixels");
            else need((pixel(color,inside,360)&0xC0000000)==0xC0000000,"Mono alpha mask missing at independently transformed skin position");
            need(pixel(color,outside,360)==pixel(colorBefore,outside,360),"Mono drew at stale static/skinned position");
            need(pixel(color,1100,360)==pixel(colorBefore,1100,360)&&pixel(color,inside,200)==pixel(colorBefore,inside,200),"Mono material skip or bounds differ");
            sameDepth(driver.readbackDepth(bound.depthIdentity),depthBefore);
            need(driver.cameraClearCount()==clears&&driver.cameraCopyCount()==copies&&driver.cameraBinding().camera==camera,"Mono changed camera ownership or clear/copy counts");
            same(rt,geometry,geometryBefore,"Mono changed input geometry");same(rt,objectFrame,frameBefore,"Mono changed object frame");
            same(rt,bindMatrices,bonesBefore,"Mono changed source skeleton matrices or bone mapping");
            need(!PPC_LOAD_U32(0x82D0CAF8)&&PPC_LOAD_U32(0x82D6CCA8)==2,"Mono changed legacy device or pass flags");
            for(const auto& row:states)need(driver.effectiveState().scalar(row[0])==row[1],"Mono changed inherited scalar state");
        };
        stage="static mono alpha mask";runPass(false);
        stage="static to skinned mono burp mask";runPass(true);
        stage="repeated skinned mono burp mask";runPass(true);
        stage="original mono64 palette boundary";runPass(true,64);
        stage="original mono65 per-submesh alias and copy";runPass(true,65);
        stage="original mono255 per-submesh alias and copy";runPass(true,255);
        stage="original mono65 all material rows skipped";runPass(true,65,true);
        stage="skinned to static mono alpha mask";runPass(false);
        stage="completed mono original mode-zero reset and logical end";
        const auto resetParameters=effects.view(id).defaultVectorWords;
        const auto resetDirty=effects.privateModifiedMask(id);const auto resetPool=snapshot(rt,v.pool,512);
        const auto resetTyped=snapshot(rt,owner,0xC0),resetWrapper=snapshot(rt,wrapper,0x30),resetManager=snapshot(rt,manager,0x18);
        const auto resetCache=snapshot(rt,v.cache,v.cacheBytes);
        const auto resetCamera=driver.cameraBinding();
        const auto resetColor=driver.readbackColor(resetCamera.colorIdentity),resetDepth=driver.readbackDepth(resetCamera.depthIdentity);
        const auto resetCount=driver.bindingResetCount(),resetDraws=effects.monoMeshDrawCount(),resetClears=driver.cameraClearCount();
        need(PPC_LOAD_U32(0x823EFDA0)==0x7D8802A6&&PPC_LOAD_U32(0x823F46EC)==0x4BFFB6B4&&
             PPC_LOAD_U32(0x826B4644)==0x2B0B0000,"Original mode-zero reset or manager-end instruction changed");
        {EngineCpuCalls reset(entry,base);seedAbi(reset.registers());const auto before=fullAbi(reset.registers());
            reset.invoke(0x823EFDA0);need(fullAbi(reset.registers())==before,"Mono binding reset changed nonvolatile ABI");}
        need(driver.bindingResetCount()==resetCount+1&&effects.monoMeshDrawCount()==resetDraws&&driver.cameraClearCount()==resetClears,
             "Mono mode-zero reset drew or cleared instead of resetting bindings");
        for(uint32_t address:{0x82CD1A68u,0x82CD1A6Cu,0x82CD1A70u,0x82CD1A74u})
            need(!PPC_LOAD_U32(address),"Mono original reset retained a shader/declaration/index cache");
        same(rt,owner,resetTyped,"Mono reset changed typed owner");same(rt,wrapper,resetWrapper,"Mono reset ended its wrapper");
        same(rt,manager,resetManager,"Mono reset ended its manager");same(rt,v.cache,resetCache,"Mono reset changed saved cache rows");
        same(rt,v.pool,resetPool,"Mono reset changed shared data or dirty storage");
        need(effects.view(id).defaultVectorWords==resetParameters&&effects.privateModifiedMask(id)==resetDirty&&
             driver.readbackColor(resetCamera.colorIdentity)==resetColor&&driver.readbackDepth(resetCamera.depthIdentity)==resetDepth,
             "Mono reset changed constants,dirty flags or rendered pixels");
        {Restore restore(rt,manager+8,4);PPC_STORE_U32(manager+8,id^1u);bool rejected=false;
            try{EngineCpuCalls invalid(entry,base);invalid.invoke(0x826B4628,manager);}catch(const Failure&){rejected=true;}
            need(rejected&&PPC_LOAD_U32(wrapper+0x2C)==v.cache,"Mono reset receipt admitted another logical effect identity");}
        EngineCpuCalls ending(entry,base);seedAbi(ending.registers());const auto beforeEnd=fullAbi(ending.registers());ending.invoke(0x826B4B18,wrapper);
        need(fullAbi(ending.registers())==beforeEnd&&!PPC_LOAD_U32(manager+4)&&!PPC_LOAD_U32(wrapper+0x2C),"Mono end failed selection or ABI restoration");
        need(cpu.invoke(0x826B7940,cullSelector)==priorCull&&driver.effectiveState().scalar(Graphics::ScalarState::Cull)==priorNativeCull,"Mono end failed cull restoration");
        {const auto after=snapshot(rt,manager,0x18);EngineCpuCalls noop(entry,base);seedAbi(noop.registers());const auto saved=fullAbi(noop.registers());
            noop.invoke(0x826B4628,manager);need(fullAbi(noop.registers())==saved,"Ended mono manager no-op changed ABI");
            same(rt,manager,after,"Ended mono manager no-op changed retained association");}
        need(effects.monoMeshDrawCount()==resetDraws&&driver.readbackColor(resetCamera.colorIdentity)==resetColor&&
             driver.readbackDepth(resetCamera.depthIdentity)==resetDepth&&effects.view(id).defaultVectorWords==resetParameters&&
             effects.privateModifiedMask(id)==resetDirty,"Mono end after reset drew or changed parameters/dirty pixels");
        need(cpu.invoke(0x823F1A08,camera)==camera,"Original main camera end failed");
    }
    stage="original mono retirement";
    cpu.invoke(0x823EE6C8,loading);cpu.invoke(0x8269CC28);cpu.invoke(0x82701118,table,25);cpu.invoke(0x826B7600,manager,1);
    need(!effects.count(),"Original catalog retirement retained mono owners");same(rt,0x8211F480,source,"Mono test changed original effect source");
}
}
PPC_FUNC(sub_82700318){forward(0x82700318,ctx,base,__imp__sub_82700318);}
PPC_FUNC(sub_826FF4C8){forward(0x826FF4C8,ctx,base,__imp__sub_826FF4C8);}
PPC_FUNC(sub_826FE7C8){forward(0x826FE7C8,ctx,base,__imp__sub_826FE7C8);}
PPC_FUNC(sub_826FE710){forward(0x826FE710,ctx,base,__imp__sub_826FE710);}
PPC_FUNC(sub_823C8EB0){forward(0x823C8EB0,ctx,base,__imp__sub_823C8EB0);}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==2,"Original image required");Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original startup boundary missing");run(rt,entry);
        std::printf("PASS original mono:%zu checks; real catalog/camera, whole dispatcher, static/skin transitions, mapped palette, alpha mask, material skip, mode-zero reset/logical end, ABI, retirement\n",checks);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original mono:%zu checks stage=%s:%s\n",checks,stage,error.what());return 1;}
}
