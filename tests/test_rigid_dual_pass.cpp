// Rigid materials through the original caller and alpha dispatcher. The two
// metadata variants reproduce completion crashes, with a separate original
// catalog/default/register record for each family.
#include "effect_catalog_lifecycle_helpers.h"
#include "runtime/engine_itxd_textures.h"
#include "renderer/engine_state.h"
#include <bit>
#include <fstream>
#include <functional>
#include <string_view>
#include "effect_screen_replacement_helpers.h"

extern "C" void __imp__sub_82740680(PPCContext&,uint8_t*);
extern "C" void __imp__sub_827400F8(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8270BBC0(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8270BE50(PPCContext&,uint8_t*);

namespace {
constexpr uint32_t secondTable=0x82CD1448,noTexture=UINT32_MAX;
struct Family {
    const char* name;uint32_t source,row,bytes,descriptors,leaves,words,opaqueContext,alphaContext,alphaVertex,alphaPixel;
};
constexpr std::array<Family,6> families={{{"dual",0x8202AD78,10,0x3220,28,24,156,0x2920,0x2C30,0x8202B884,0x8202C3BC},
    {"textured",0x820168F8,8,0x3090,27,23,140,0x27B0,0x2AB0,0x8201739C,0x82017E4C},
    {"gloss",0x82019988,11,0x33C0,26,22,136,0x2B00,0x2DF0,0x8201A430,0x8201B0BC},
    {"multitone",0x820547E8,20,0x3620,27,23,152,0x2D40,0x3040,0x82055440,0x820560B8},
    {"normalmap",0x82057E08,21,0x3A40,28,24,156,0x3140,0x3450,0x820589EC,0x82059880},
    {"base",0x8200CCB8,1,0x2EE0,26,22,136,0x2620,0x2910,0x8200D734,0x8200E1BC}}};
const Family* family=&families[0];
uint32_t source=family->source,body=source+12;
struct MaterialInput {uint32_t handle,word,texture;};
std::vector<MaterialInput> materialInputs() {
    if(source==0x8200CCB8)return {{0x0048001C,80,noTexture},{0x00500020,88,1},{0x00540022,104,0},
        {0x00580024,120,noTexture},{0x005C0026,124,noTexture}};
    if(source==0x820168F8)return {{0x0048001C,80,noTexture},{0x00540022,92,1},{0x00580024,108,0},
        {0x005C0026,124,noTexture},{0x00600028,128,noTexture},{0x0064002A,132,noTexture}};
    if(source==0x82019988)return {{0x0044001A,76,noTexture},{0x0048001C,80,noTexture},
        {0x00540022,92,1},{0x00580024,108,0},{0x005C0026,124,noTexture},
        {0x00600028,128,noTexture},{0x0064002A,132,noTexture}};
    if(source==0x820547E8)return {{0x0048001C,80,noTexture},{0x00540022,92,1},{0x00580024,108,0},
        {0x005C0026,124,1},{0x00600028,140,noTexture},{0x0064002A,144,noTexture},{0x0068002C,148,noTexture}};
    if(source==0x82057E08)return {{0x0044001A,76,noTexture},{0x0048001C,80,noTexture},
        {0x00540022,92,1},{0x00580024,108,0},{0x005C0026,124,1},
        {0x00600028,140,noTexture},{0x0064002A,144,noTexture},{0x0068002C,148,noTexture},{0x006C002E,152,noTexture}};
    return {{0x0048001C,80,noTexture},{0x00540022,92,1},{0x00580024,108,0},
        {0x00600028,140,noTexture},{0x0064002A,144,noTexture}};
}
constexpr uint32_t area=0x60000,packet=area,metadata=0x60100,object=0x61000,data=0x63000,
    offsets=0x64000,geometry=0x65000,elements=0x65100,vertices=0x65200,indices=0x65400,
    submeshes=0x65500,declCache=0x65600,headers=0x65700,objectFrame=0x65800,
    collection=0x66000,material=0x66100,materialRows=0x66200,values=0x66500,
    property=0x67000,dictionary=0x80000;
std::function<void(uint32_t,PPCContext&)> observer;
void forward(uint32_t pc,PPCContext& c,uint8_t* base,void(*original)(PPCContext&,uint8_t*)) {
    if(observer)observer(pc,c);original(c,base);
}
struct Observation {
    explicit Observation(decltype(observer) value){need(!observer,"Nested dual observer");observer=std::move(value);}
    ~Observation(){observer={};}
};
struct Restore {
    uint8_t* address;std::vector<uint8_t> bytes;
    Restore(Runtime& rt,uint32_t at,uint32_t size):address(rt.pointer(at,size,true)),bytes(address,address+size){}
    ~Restore(){std::memcpy(address,bytes.data(),bytes.size());}
};
struct FullAbi {
    SavedAbi integer;std::array<uint64_t,18> floating;
    bool operator==(const FullAbi&) const=default;
};
FullAbi fullAbi(const PPCContext& c) {
    return {abi(c),{c.f14.u64,c.f15.u64,c.f16.u64,c.f17.u64,c.f18.u64,c.f19.u64,c.f20.u64,c.f21.u64,c.f22.u64,
        c.f23.u64,c.f24.u64,c.f25.u64,c.f26.u64,c.f27.u64,c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64}};
}
void identity(uint8_t* base,uint32_t at) {
    for(uint32_t i=0;i<16;++i)PPC_STORE_U32(at+4*i,(i%5)?0:0x3F800000);
}
uint32_t pixel(const std::vector<uint8_t>& bytes,uint32_t x,uint32_t y) {
    need(bytes.size()==size_t(1280)*720*4,"Dual color extent differs");
    uint32_t result{};std::memcpy(&result,bytes.data()+4*(1280*y+x),4);return result;
}
void originalEvidence(uint8_t* base) {
    // Original words independently pin the dispatcher metadata bit1 -> r21,
    // the fallback argument moves/call, and the r5 overwrite before first use.
    constexpr std::array<std::array<uint32_t,2>,25> instructions={{{0x827408B4,0x81690008},
        {0x827408B8,0x5575FFFE},{0x827408F0,0x897F000C},{0x827408FC,0x7FD7F378},
        {0x82740B18,0x7EA5AB78},{0x82740B1C,0x7EE4BB78},{0x82740B20,0x7FE3FB78},
        {0x82740B24,0x4BFFF5D5},{0x827400F8,0x7D8802A6},{0x827400FC,0x482FC2CD},
        {0x82740100,0x9421FF80},{0x82740104,0x7C7F1B78},{0x82740108,0x7C9E2378},
        {0x8274010C,0x7CDC3378},{0x82740110,0x7FC7F378},{0x82740114,0x807F0018},
        {0x82740118,0x809F0000},{0x8274011C,0x80DF0008},{0x82740120,0x80BF0004},
        {0x82A3C3C8,0xFB81FFD8},{0x82A3C3CC,0xFBA1FFE0},{0x82A3C3D0,0xFBC1FFE8},
        {0x82A3C3D4,0xFBE1FFF0},{0x82A3C3D8,0x9181FFF8},{0x82A3C3DC,0x4E800020}}};
    for(const auto& row:instructions)need(PPC_LOAD_U32(row[0])==row[1],"Original dual fallback instruction evidence changed");
    need(PPC_LOAD_U32(secondTable+family->row*16)==source&&PPC_LOAD_U32(secondTable+family->row*16+12)==0x823CA6B0,
         "Original rigid family catalog association changed");
    need(PPC_LOAD_U32(source+4)+12==family->bytes&&PPC_LOAD_U32(body+0x118)==family->descriptors&&
         PPC_LOAD_U32(body+0x130)==family->leaves&&PPC_LOAD_U32(body+0x138)==family->words*4,
         "Original rigid family descriptor/leaf/storage counts differ");
    need(body+PPC_LOAD_U32(body+family->alphaContext+0x48)+8==family->alphaVertex&&
         body+PPC_LOAD_U32(body+family->alphaContext+0x4C)+8==family->alphaPixel,
         "Original rigid family alpha shader association differs");
    if(source==0x820547E8||source==0x82057E08) {
        constexpr std::array<uint32_t,5> semantics{0x00100003,0x00003004,0x0000A005,0x00005006,0x00215007};
        need(PPC_LOAD_U32(family->alphaVertex+0x164)==5,"Original family alpha fetch count differs");
        for(uint32_t i=0;i<semantics.size();++i)need(PPC_LOAD_U32(family->alphaVertex+0x170+4*i)==semantics[i],
            "Original family alpha position/normal/color/two-UV association changed");
    }
    // Base rigid uses leaves16/17 at words88/104. The other families use
    // leaves17/18 at words92/108; each has its own immutable context maps.
    const auto descriptors=body+PPC_LOAD_U32(body+0x108);
    const bool baseRigid=source==0x8200CCB8;
    const uint32_t baseLeaf=baseRigid?16u:17u,baseHandle=baseRigid?0x00500020u:0x00540022u,
        samplerDescriptor=baseRigid?20u:21u,baseWord=baseRigid?88u:92u;
    need(PPC_LOAD_U32(descriptors+samplerDescriptor*8)==0x0060002C&&
         PPC_LOAD_U32(descriptors+samplerDescriptor*8+4)==baseWord/4&&
         PPC_LOAD_U32(descriptors+(samplerDescriptor+1)*8)==0x0060002C&&
         PPC_LOAD_U32(descriptors+(samplerDescriptor+1)*8+4)==baseWord/4+4,
         "Original family sampler descriptor storage differs");
    for(const auto ctx:{family->opaqueContext,family->alphaContext}) {
        const auto map=body+PPC_LOAD_U32(body+ctx+0x40);
        const bool unused=baseRigid&&ctx==family->opaqueContext;
        need(PPC_LOAD_U32(map+16*baseLeaf)==(unused?0u:baseHandle)&&
             PPC_LOAD_U32(map+16*baseLeaf+4)==0&&
             PPC_LOAD_U32(map+16*baseLeaf+8)==(!baseRigid&&ctx==family->opaqueContext?0x00800000u:0u)&&
             PPC_LOAD_U32(map+16*baseLeaf+12)==0,
             "Original family base texture opaque/alpha stage mapping differs");
        need(PPC_LOAD_U32(map+16*14)==0x0048001C&&PPC_LOAD_U32(map+16*14+8)==49,
             "Original dual custom-lines pixel register differs");
    }
    const auto alphaMap=body+PPC_LOAD_U32(body+family->alphaContext+0x40);
    for(const auto& input:materialInputs()) {
        const auto descriptor=descriptors+8*(input.handle>>18);
        need((PPC_LOAD_U32(descriptor)&3)==0&&(PPC_LOAD_U32(descriptor+4)&0xFFFF)==input.word/4,
             "Original family material leaf storage differs");
        const auto leaf=(input.handle>>1)&0x1FFFF;
        uint32_t reg=UINT32_MAX;
        if(input.handle==0x0044001A)reg=50;
        if(source==0x82019988&&input.handle==0x005C0026)reg=47;
        if(source==0x82057E08&&input.handle==0x00600028)reg=47;
        if(reg!=UINT32_MAX)need(PPC_LOAD_U32(alphaMap+16*leaf)==input.handle&&
            PPC_LOAD_U32(alphaMap+16*leaf+4)==0&&PPC_LOAD_U32(alphaMap+16*leaf+8)==reg,
            "Original family alpha specular register differs");
        else if(input.handle!=0x0048001C&&input.handle!=baseHandle)
            need(!PPC_LOAD_U32(alphaMap+16*leaf)&&!PPC_LOAD_U32(alphaMap+16*leaf+4)&&
                 !PPC_LOAD_U32(alphaMap+16*leaf+8),"Unused family alpha leaf unexpectedly has a register/texture map");
    }
}
std::array<uint32_t,2> loadTextures(Runtime& rt,const PPCContext& entry,const char* path) {
    auto* base=rt.base;EngineCpuCalls cpu(entry,base);
    std::ifstream input(path,std::ios::binary|std::ios::ate);need(bool(input),"Original ITXD dictionary required");
    const auto length=input.tellg();need(length>0&&length<=0x200000,"ITXD fixture extent differs");
    const auto bytes=uint32_t(length);rt.map(dictionary,bytes,true,"original dual texture dictionary");
    input.seekg(0);input.read(reinterpret_cast<char*>(rt.pointer(dictionary,bytes,true)),bytes);
    need(bool(input),"Original dictionary read failed");const auto original=snapshot(rt,dictionary,bytes);
    PPC_STORE_U32(area+0x80,dictionary);PPC_STORE_U32(area+0x84,bytes);
    const auto stream=cpu.invoke(0x823F9598,3,1,area+0x80);need(stream,"Original memory stream construction failed");
    EngineCpuCalls load(entry,base);load.registers().lr=0x8271191C;
    const auto request=load.registers().r1.u32+0x60;std::memset(rt.pointer(request,0x18,true),0,0x18);
    PPC_STORE_U32(area+0x90,0x4455414C);PPC_STORE_U32(request+4,area+0x90);
    PPC_STORE_U32(request+0x10,stream);PPC_STORE_U32(request+0x14,bytes);
    const auto saved=fullAbi(load.registers());const auto group=load.invoke(0x826F26B0,0,request);
    need(group&&fullAbi(load.registers())==saved,"Original dictionary parent loader lost return or ABI");
    const auto plugin=PPC_LOAD_U32(0x82CF0600);
    const uint32_t count=PPC_LOAD_U16(dictionary+4)+PPC_LOAD_U16(dictionary+6),sentinel=dictionary+8*(count+1)+8;
    std::array<uint32_t,2> result{};uint32_t visited=0;
    for(uint32_t link=PPC_LOAD_U32(sentinel);dictionary+link!=sentinel;link=PPC_LOAD_U32(dictionary+link)) {
        need(link&&++visited<256,"Original dictionary link cycle");const auto texture=dictionary+link-8;
        const auto name=stringAt(rt,texture+0x10);
        if(name=="simpsons_palette"||name=="fire64bw3") {
            const auto copied=cpu.invoke(0x826F8520,PPC_LOAD_U32(texture+plugin+4));
            need(copied&&rt.engineDriver->itxdTextures().ownsRaster(copied+0x78),"Original named texture not copied/published");
            result[name=="simpsons_palette"?0:1]=copied+0xCC;
        }
    }
    need(result[0]&&result[1]&&result[0]!=result[1],"Two distinct original textures missing");
    same(rt,dictionary,original,"Original loader changed immutable input dictionary");return result;
}
void fixture(Runtime& rt,uint32_t camera,uint32_t typed,uint32_t shadow,const std::array<uint32_t,2>& textures) {
    auto* base=rt.base;
    const bool tangent=source==0x82057E08,baseRigid=source==0x8200CCB8;
    const uint32_t stride=tangent?40u:(baseRigid?28u:36u),declarationCount=tangent?7u:(baseRigid?5u:6u);
    PPC_STORE_U32(packet,metadata);PPC_STORE_U32(packet+4,object);PPC_STORE_U32(packet+8,camera);
    PPC_STORE_U8(packet+0xC,1);PPC_STORE_U32(packet+0x18,typed);PPC_STORE_U32(packet+0x1C,shadow);PPC_STORE_U32(packet+0x20,shadow);
    PPC_STORE_U32(object,0x01000500);PPC_STORE_U32(object+4,objectFrame);
    PPC_STORE_U32(object+8,object+8);PPC_STORE_U32(object+0xC,object+8);
    PPC_STORE_U32(object+0x10,0x823CD3D8);PPC_STORE_U32(object+0x18,data);PPC_STORE_U32(object+0x3C,property);
    PPC_STORE_U32(data+0x24,offsets);PPC_STORE_U32(objectFrame+0xA0,objectFrame);identity(base,objectFrame+0x10);
    PPC_STORE_U8(property,0x40);
    PPC_STORE_U32(metadata,0x00030002);PPC_STORE_U32(metadata+4,0xB5F8FBF2);
    PPC_STORE_U32(metadata+0xC,geometry);PPC_STORE_U32(metadata+0x10,1);PPC_STORE_U32(metadata+0x14,submeshes);
    PPC_STORE_U32(metadata+0x18,1);PPC_STORE_U32(metadata+0x34,collection);
    PPC_STORE_U32(geometry,4*stride);PPC_STORE_U32(geometry+4,stride);PPC_STORE_U32(geometry+8,declarationCount);
    PPC_STORE_U32(geometry+0xC,elements);PPC_STORE_U32(geometry+0x10,vertices);PPC_STORE_U32(geometry+0x14,8);
    PPC_STORE_U32(geometry+0x18,1);PPC_STORE_U32(geometry+0x1C,indices);PPC_STORE_U32(geometry+0x30,declCache);
    PPC_STORE_U32(geometry+0x50,vertices|3);PPC_STORE_U32(geometry+0x54,2);PPC_STORE_U32(geometry+0x58,0x20000002);
    PPC_STORE_U32(geometry+0x70,indices);PPC_STORE_U32(geometry+0x74,8);PPC_STORE_U32(declCache+4,declCache+0x40);
    constexpr uint32_t declaration[][3]={{0,0x002A23B9,0},{12,0x002A2187,0x00030000},
        {16,0x00182886,0x000A0000},{20,0x002C23A5,0x00050000},{28,0x002C23A5,0x00050100},
        {36,0x002A2187,0x00060000},{0x00FF0000,UINT32_MAX,0}};
    for(uint32_t i=0;i<declarationCount;++i)for(uint32_t lane=0;lane<3;++lane)
        PPC_STORE_U32(elements+12*i+4*lane,declaration[!tangent&&i==declarationCount-1?6:i][lane]);
    constexpr float xy[][2]={{-.5f,.5f},{-.5f,-.5f},{.5f,.5f},{.5f,-.5f}};
    for(uint32_t i=0;i<4;++i) {
        PPC_STORE_U32(vertices+stride*i,std::bit_cast<uint32_t>(xy[i][0]));
        PPC_STORE_U32(vertices+stride*i+4,std::bit_cast<uint32_t>(xy[i][1]));PPC_STORE_U32(vertices+stride*i+8,0x3F000000);
        PPC_STORE_U32(vertices+stride*i+12,0x055C7E3D);PPC_STORE_U32(vertices+stride*i+16,0xE5000064);
        for(uint32_t uv=0;uv<(baseRigid?1u:2u);++uv)for(uint32_t lane=0;lane<2;++lane)
            PPC_STORE_U32(vertices+stride*i+20+8*uv+4*lane,std::bit_cast<uint32_t>(xy[i][lane]+.5f));
        if(tangent)PPC_STORE_U32(vertices+stride*i+36,0x000001FF);
        PPC_STORE_U16(indices+2*i,uint16_t(i));
    }
    PPC_STORE_U32(submeshes,2);PPC_STORE_U32(submeshes+12,6);PPC_STORE_U32(submeshes+24,4);
    PPC_STORE_U32(offsets+8,0x30);PPC_STORE_U32(collection,1);PPC_STORE_U32(collection+12,material);
    // Each family supplies its own real handles and storage offsets. Numeric
    // inputs for the new families use the game's exact authored defaults;
    // the original dual regression keeps its previously verified inputs.
    const auto inputs=materialInputs();const auto defaults=body+PPC_LOAD_U32(body+0x128);
    const auto bindings=PPC_LOAD_U32(typed+0x28),bindingCount=PPC_LOAD_U32(typed+0x24);
    need(bindingCount&&bindingCount<=64,"Original typed material binding extent differs");
    for(uint32_t i=0;i<inputs.size();++i) {
        uint32_t bindingIndex=UINT32_MAX;
        for(uint32_t j=0;j<bindingCount;++j)if(PPC_LOAD_U32(bindings+24*j)==inputs[i].handle)bindingIndex=j;
        need(bindingIndex!=UINT32_MAX,"Original rigid family material binding missing");
        const bool texture=inputs[i].texture!=noTexture;const auto value=values+16*i;
        PPC_STORE_U32(materialRows+12*i,0x00400000|(bindingIndex<<16)|(texture?1u:3u));
        PPC_STORE_U32(materialRows+12*i+8,value);
        if(texture)PPC_STORE_U32(value+4,textures[inputs[i].texture]);
        else if(source!=0x8202AD78)std::memcpy(rt.pointer(value,16,true),rt.pointer(defaults+4*inputs[i].word,16,false),16);
        else if(!i){PPC_STORE_U32(value,0x3E800000);PPC_STORE_U32(value+4,0x3E800000);}
    }
    PPC_STORE_U32(material+0xC,uint32_t(inputs.size())<<10);PPC_STORE_U32(material+0x14,materialRows);
}
void run(Runtime& rt,const PPCContext& entry,const char* dictionaryPath) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();EngineCpuCalls cpu(entry,base);
    stage="original rigid family instruction and binding evidence";originalEvidence(base);
    rt.map(area,0x10000,true,"original dual dispatcher fixture");std::memset(rt.pointer(area,0x10000,true),0,0x10000);
    stage="real rigid family catalog construction";
    const auto context=PPC_LOAD_U32(0x82D5DA74),options=cpu.registers().r1.u32+0x60;
    PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    const auto manager=cpu.invoke(0x8269BF70,0x260,options);cpu.invoke(0x826B6F60,manager,context);
    need(!cpu.invoke(0x827019E8,table,25)&&!cpu.invoke(0x827019E8,secondTable,24),"Original catalog registration failed");
    cpu.invoke(0x826B7218,manager);
    const auto typed=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(secondTable+family->row*16+4));
    const auto shadow=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(table+4*16+4));
    need(typed&&shadow&&PPC_LOAD_U32(typed)==0x820616C0,"Original dual/shared rigid typed owner missing");
    const auto id=PPC_LOAD_U32(typed+0x1C),wrapper=PPC_LOAD_U32(typed+0x18);
    need(effects.count()==49&&effects.view(id).source==source&&effects.view(id).defaultVectorWords.size()==family->words,
         "Original rigid family source or private bank differs");
    cache(rt,cpu,id,body);queries(rt,cpu,id,body);const auto sourceBytes=snapshot(rt,source,family->bytes);
    stage="original two texture dictionary load";const auto textures=loadTextures(rt,entry,dictionaryPath);
    stage="original world and character shadow copies";
    for(uint32_t slot=0;slot<2;++slot) {
        const auto camera=PPC_LOAD_U32(shadow+0x5B4+4*slot);PPC_STORE_U32(area+0x80,0x000000FF);
        need(cpu.invoke(0x823F1B80,camera,area+0x80,7)==camera,"Original shadow camera clear failed");
        EngineCpuCalls pass(entry,base);const auto saved=fullAbi(pass.registers());pass.invoke(0x82707220,shadow,slot);
        need(fullAbi(pass.registers())==saved,"Whole original empty shadow parent changed nonvolatile ABI");
        need(driver.shadowTextures().view(PPC_LOAD_U32(shadow+0xF0+4*slot)).phase==EngineShadowTextures::Phase::Uploaded,
             "Original parent did not upload its world/character depth copy");
    }
    const auto viewport=cpu.invoke(0x8269D788);need(viewport,"Original viewport manager missing");
    cpu.registers().f1.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12CC));
    cpu.registers().f2.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D0));
    cpu.registers().f3.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D4));
    cpu.registers().f4.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D8));
    cpu.invoke(0x8269D608,viewport,0);const auto camera=PPC_LOAD_U32(viewport+0x14),frame=PPC_LOAD_U32(camera+4);
    fixture(rt,camera,typed,shadow,textures);
    Restore flags(rt,0x82D6CCA8,4),nativeContext(rt,0x82D6D890,4),published(rt,0x82D63028,4),
        materialRoot(rt,0x82D6D814,4),recordEnabled(rt,0x82CF0BE8,4),dirty(rt,0x82D00F80,4),
        propertyBase(rt,0x82D6C0C0,4),view(rt,0x82D0CA70,64),projection(rt,0x82CD1AB0,64),cameraMatrix(rt,frame+0x10,64);
    PPC_STORE_U32(0x82D6CCA8,0);PPC_STORE_U32(0x82D6D890,context);PPC_STORE_U32(0x82D63028,0);
    PPC_STORE_U32(0x82D6D814,headers);PPC_STORE_U32(0x82CF0BE8,0);PPC_STORE_U32(0x82D00F80,0);PPC_STORE_U32(0x82D6C0C0,0);
    need(cpu.invoke(0x823F1A18,camera)==camera,"Original main camera begin failed");
    identity(base,frame+0x10);identity(base,0x82D0CA70);identity(base,0x82CD1AB0);
    constexpr std::array<std::array<uint32_t,2>,22> states={{{0x28,1},{0x2C,6},{0x30,1},{0x34,0},{0x3C,0},{0x60,0},{0x6C,0},
        {0xAC,0},{0xC0,1},{0xC4,UINT32_MAX},{0xC8,0},{0xCC,0},{0xD0,0},{0xD4,15},{0xE4,0},{0x130,1},
        {0x144,1},{0x148,1},{0x14C,0xFFFF},{0x150,0},{0xD8,0},{0xDC,0}}};
    const auto resetPassState=[&] {
        // The outer scene scheduler establishes a fresh pass before returning
        // from transparent to opaque work. This bounded fixture owns that
        // baseline: a previous alpha dispatcher deliberately leaves blending
        // enabled, while the material's two-state cache only owns depth.
        for(const auto& row:states)driver.directScalar(base,row[0],row[1]);
        cpu.invoke(0x826B7968,PPC_LOAD_U32(0x82E06F80+0x38),1,1);
    };
    resetPassState();
    const auto bound=driver.cameraBinding();uint32_t dispatcherCalls=0,fallbackCalls=0,vectorCalls=0,textureCalls=0,expectedAlpha=0,expectedBit1=0;
    // The active manager's eye (manager+230, the WORLD_EYE_POS source) can move between the stage upload and a material commit.
    bool moveEye=false;uint32_t eyeMoves=0;
    Observation observation([&](uint32_t pc,PPCContext& c) {
        if(pc==0x82740680){++dispatcherCalls;need(uint32_t(c.lr)==0x8273B4E0&&c.r3.u32==packet,"Original crashing dispatcher caller changed");}
        else if(moveEye&&!eyeMoves&&pc==0x8270BBC0){++eyeMoves;++vectorCalls;
            for(uint32_t lane=0;lane<3;++lane)PPC_STORE_U32(manager+0x230+4*lane,std::bit_cast<uint32_t>(float(11+lane)));}
        else if(pc==0x827400F8){++fallbackCalls;need(uint32_t(c.lr)==0x82740B28&&c.r3.u32==packet&&
            c.r4.u32==expectedAlpha&&c.r5.u32==expectedBit1&&c.r6.u32==0,"Original dual fallback flags or caller differ");}
        else if(pc==0x8270BBC0)++vectorCalls;else if(pc==0x8270BE50)++textureCalls;
    });
    PPC_STORE_U32(area+0x80,0x00FF00FF);need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original main clear failed");
    const auto clearColor=driver.readbackColor(bound.colorIdentity);auto mesh=snapshot(rt,geometry,0x700);
    const auto inputs=materialInputs();
    constexpr std::array<std::array<uint32_t,2>,7> unusedSampler{{{0,7},{4,6},{8,5},
        {0x10,0},{0x14,1},{0x18,0},{0x30,1}}};
    const auto call=[&](uint32_t alpha,uint32_t bit1,bool poisonUnused=false,bool moveActiveEye=false) {
        expectedAlpha=alpha;expectedBit1=bit1;moveEye=moveActiveEye;eyeMoves=0;
        // Only original metadata and the packet's alpha-eligibility byte vary.
        // The real dispatcher, rather than the fixture, supplies r4/r5.
        PPC_STORE_U32(metadata+8,1|(bit1<<1));PPC_STORE_U8(packet+0xC,uint8_t(alpha));
        resetPassState();
        if(poisonUnused)for(const auto& row:unusedSampler)driver.directSampler(base,1,row[0],row[1]);
        need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original variant clear failed");
        std::array<uint32_t,3> eye{};for(uint32_t lane=0;lane<3;++lane)eye[lane]=PPC_LOAD_U32(manager+0x230+4*lane);
        EngineCpuCalls draw(entry,base);const auto saved=fullAbi(draw.registers());draw.invoke(0x8273B4D0,packet);
        need(fullAbi(draw.registers())==saved,"Whole original dual wrapper changed nonvolatile ABI");
        need(!moveActiveEye||eyeMoves==1,"The active-eye move hook never ran");
        if(moveActiveEye)for(uint32_t lane=0;lane<3;++lane)PPC_STORE_U32(manager+0x230+4*lane,eye[lane]);
        need(effects.privateModifiedMask(id)==std::array<uint8_t,128>{},"Dual dirty bank remains after commit");
        need(!PPC_LOAD_U32(0x82D0CAF8),"Dual draw published a console device");
        need(PPC_LOAD_U32(manager+8)==id&&PPC_LOAD_U32(manager+0xC)==(alpha?0x0007FFFCu:0x0003FFFCu),
             "Original rigid family selected the wrong owner or technique");
        const auto committed=effects.view(id).defaultVectorWords;
        for(uint32_t i=0;i<inputs.size();++i) {
            const auto& input=inputs[i];
            if(input.texture!=noTexture)need(committed[input.word]==textures[input.texture],
                "Original family callback did not store its real texture header");
            else for(uint32_t lane=0;lane<4;++lane)need(committed[input.word+lane]==PPC_LOAD_U32(values+16*i+4*lane),
                "Original family callback did not retain its authored vector lane");
        }
        const auto pixels=driver.readbackColor(bound.colorIdentity);
        need(pixel(pixels,640,360)!=pixel(clearColor,640,360)&&pixel(pixels,1100,360)==pixel(clearColor,1100,360),
             "Original dual draw produced no center pixel or escaped geometry bounds");
        same(rt,geometry,mesh,"Dual dispatcher changed immutable input geometry");return pixels;
    };
    stage="whole original family alpha fallback flags 1/0";const auto alpha0=call(1,0);
    stage="whole original crashing family alpha fallback flags 1/1";const auto alpha1=call(1,1);
    need(alpha0==alpha1,"Unused incoming r5 changed original dual alpha pixels");
    if(family->alphaPixel==0x8200E1BC) {
        // An excluded PC4 keeps the completed stage upload's eye. A camera move before the commit must neither fail the
        // draw nor change its pixels (live bargainbin: the old equality check ended the process).
        stage="whole original base alpha with the active eye moved before the commit";
        need(call(1,0,false,true)==alpha0,"Moving the active eye after the stage upload changed the original base alpha pixels");
    }
    if(source==0x820168F8) {
        // Original PS82017E4C has one stage0 fetch. The alpha context
        // leaves stage1 untouched, so its valid inherited state is free to
        // differ from prior character-shadow and mirror profiles.
        std::array<uint32_t,20> beforeSampler;
        for(uint32_t i=0;i<beforeSampler.size();++i)beforeSampler[i]=driver.effectiveState().sampler(1,4*i);
        stage="whole original textured alpha with distinct unused stage1 state";
        need(call(1,1,true)==alpha1,"Unused sampler state changed textured alpha pixels");
        for(const auto& row:unusedSampler)need(driver.effectiveState().sampler(1,row[0])==row[1],
            "Textured alpha mutated its unused inherited sampler state");
        for(uint32_t i=0;i<beforeSampler.size();++i)driver.directSampler(base,1,4*i,beforeSampler[i]);
    }
    stage="whole original family opaque fallback flags 0/1";const auto opaque1=call(0,1);
    stage="whole original family opaque fallback flags 0/0";const auto opaque0=call(0,0);
    need(opaque1==opaque0,"Unused incoming r5 changed original dual opaque pixels");
    if(source==0x82057E08) {
        const auto originalMesh=mesh;
        {
            Restore count(rt,geometry+8,4),tangentRow(rt,elements+60,12);
            // The original alpha VS has no tangent fetch. Keep all five
            // consumed inputs and the original stride, but remove the unused
            // tangent declaration. This tests the original caller, declaration
            // decoder and native layout together, rather than only the shader.
            PPC_STORE_U32(geometry+8,6);PPC_STORE_U32(elements+60,0x00FF0000);
            PPC_STORE_U32(elements+64,UINT32_MAX);PPC_STORE_U32(elements+68,0);
            mesh=snapshot(rt,geometry,0x700);
            stage="whole original normalmap alpha without a tangent declaration";
            need(call(1,1)==alpha1,"Unused tangent declaration changed original normalmap alpha pixels");
        }
        same(rt,geometry,originalMesh,"No-tangent alpha fixture did not restore complete original geometry");mesh=originalMesh;
    }
    const auto textureCount=uint32_t(std::count_if(inputs.begin(),inputs.end(),[](const auto& input){return input.texture!=noTexture;}));
    const auto vectorCount=uint32_t(inputs.size())-textureCount;
    const uint32_t draws=((source==0x82057E08||source==0x820168F8)?5u:4u)+(family->alphaPixel==0x8200E1BC?1u:0u); // The base family adds the moved-eye draw.
    need(dispatcherCalls==draws&&fallbackCalls==draws&&vectorCalls==draws*vectorCount&&textureCalls==draws*textureCount,
         "Original rigid family material traversal differs");
    screenReplacementRegression(rt,cpu,camera,typed,wrapper,area+0xB0,[&]{(void)call(0,0);});
    same(rt,source,sourceBytes,"Dual regression changed original serialized shader record");
    cpu.invoke(0x826B4B18,wrapper);need(cpu.invoke(0x823F1A08,camera)==camera,"Original main camera end failed");
}
}
PPC_FUNC(sub_82740680){forward(0x82740680,ctx,base,__imp__sub_82740680);}
PPC_FUNC(sub_827400F8){forward(0x827400F8,ctx,base,__imp__sub_827400F8);}
PPC_FUNC(sub_8270BBC0){forward(0x8270BBC0,ctx,base,__imp__sub_8270BBC0);}
PPC_FUNC(sub_8270BE50){forward(0x8270BE50,ctx,base,__imp__sub_8270BE50);}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==3||argc==4,"Original image, loc_split4 ITXD dictionary and optional family required");
        if(argc==4) {
            const auto choice=std::find_if(families.begin(),families.end(),[&](const auto& candidate){return std::string_view(argv[3])==candidate.name;});
            need(choice!=families.end(),"Unknown original rigid family");family=&*choice;source=family->source;body=source+12;
        }
        Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original startup boundary missing");run(rt,entry,argv[2]);
        std::printf("PASS original rigid %s: %zu checks; real catalogs/textures/cameras, crashing dispatcher, all Boolean fallback flags, pixel equivalence, authored material callbacks and full ABI\n",family->name,checks);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original rigid %s: %zu checks stage=%s: %s\n",family->name,checks,stage,error.what());return 1;}
}
