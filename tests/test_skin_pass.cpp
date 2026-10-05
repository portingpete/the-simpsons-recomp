// Whole original skin traversal, genuine joint owners and material callbacks.
// Source metadata pins each family; no draw, bone or material callback is faked.
#include "effect_catalog_lifecycle_helpers.h"
#include "effect_draw_cleanup_helpers.h"
#include "runtime/engine_itxd_textures.h"
#include "runtime/engine_audio.h"
#include "renderer/engine_state.h"
#include <bit>
#include <chrono>
#include <fstream>
#include <functional>
#include <string_view>
#include "effect_screen_replacement_helpers.h"

extern "C" void __imp__sub_82740680(PPCContext&,uint8_t*);
extern "C" void __imp__sub_827400F8(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8270BBC0(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8270BE50(PPCContext&,uint8_t*);
extern "C" void __imp__sub_826FE7C8(PPCContext&,uint8_t*);
extern "C" void __imp__sub_826FE710(PPCContext&,uint8_t*);
extern "C" void __imp__sub_826FDBE0(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8270D1C0(PPCContext&,uint8_t*);
extern "C" void __imp__sub_82701638(PPCContext&,uint8_t*);

namespace {
constexpr uint32_t secondTable=0x82CD1448,noTexture=UINT32_MAX;
struct Family {
    const char* name;uint32_t source,row,bytes,descriptors,leaves,words,opaqueContext,alphaContext,alphaVertex,alphaPixel;
};
constexpr std::array<Family,3> families={{{"textured",0x8200FB98,7,0x6D60,91,86,1148,0x5C20,0x6350,0x820126EC,0x82013F8C},
    {"base",0x82006348,0,0x6970,91,86,1148,0x5830,0x5F60,0x82008E20,0x8200A4A4},
    {"dual",0x8201CD48,9,0x7110,95,90,1188,0x5F50,0x66C0,0x8201F984,0x82021344}}};
const Family* family=&families[0];
uint32_t source=family->source,body=source+12;
bool paddedLayout=false,inheritedBlend=false,groups65Layout=false,submeshes65536Layout=false;
std::filesystem::path submeshAuditPath;
struct MaterialInput {uint32_t handle,word,texture;};
std::vector<MaterialInput> materialInputs() {
    if(source==0x82006348)return {{0x0048001C,80,noTexture},{0x00500020,88,noTexture},
        {0x015C00A4,1120,0},{0x016000A6,1136,noTexture},{0x016400A8,1140,noTexture},{0x016800AA,1144,noTexture}};
    if(source==0x8201CD48)return {{0x0048001C,80,noTexture},{0x00500020,88,noTexture},
        {0x016000A6,1124,0},{0x016C00AC,1172,noTexture},{0x017000AE,1176,noTexture}};
    return {{0x0048001C,80,noTexture},{0x00500020,88,noTexture},{0x016000A6,1124,0},
        {0x016400A8,1140,noTexture},{0x016800AA,1144,noTexture}};
}
constexpr uint32_t area=0x60000,packet=area,metadata=0x60100,object=0x61000,data=0x63000,
    offsets=0x64000,geometry=0x65000,elements=0x65100,vertices=0x65200,indices=0x65400,
    submeshes=0x65500,declCache=0x65600,headers=0x65700,objectFrame=0x65800,
    collection=0x66000,material=0x66100,materialRows=0x66200,values=0x66500,
    property=0x67000,hierarchy=0x68000,skinPlugin=0x68100,bindMatrices=0x68200,
    boneMatrices=0x68300,boneMap=0x68400,boneGroups=0x68500,
    auxiliary=0x68D00,auxiliaryGeometries=0x68D40,dictionary=0x80000;
std::function<void(uint32_t,PPCContext&,bool)> observer;
void forward(uint32_t pc,PPCContext& c,uint8_t* base,void(*original)(PPCContext&,uint8_t*)) {
    if(observer)observer(pc,c,false);original(c,base);if(observer)observer(pc,c,true);
}
struct Observation {
    explicit Observation(decltype(observer) value){need(!observer,"Nested skin observer");observer=std::move(value);}
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
    need(bytes.size()==size_t(1280)*720*4,"Skin color extent differs");
    uint32_t result{};std::memcpy(&result,bytes.data()+4*(1280*y+x),4);return result;
}
void originalEvidence(uint8_t* base) {
    constexpr std::array<std::array<uint32_t,2>,36> instructions={{{0x827408B4,0x81690008},
        {0x827408B8,0x5575FFFE},{0x827408F0,0x897F000C},{0x827408FC,0x7FD7F378},
        {0x82740B18,0x7EA5AB78},{0x82740B1C,0x7EE4BB78},{0x82740B20,0x7FE3FB78},
        {0x82740B24,0x4BFFF5D5},{0x82740120,0x80BF0004},{0x827016AC,0x4BFFD11D},
        {0x827016D4,0x4BFFC50D},{0x82740290,0x4BFC13A9},
        {0x8274010C,0x7CDC3378},{0x82740918,0x5556F7FE},{0x8270165C,0x7CFE3B78},
        {0x82701734,0x839E0004},{0x827017EC,0x809E0020},{0x827017F4,0x807E001C},
        {0x82701800,0x4BFFCF11},{0x8270181C,0x4BFFC3C5},
        {0x826FE748,0x89640001},{0x826FE758,0x7C6BDA14},{0x826FE79C,0x4833E5E5},{0x826FE7BC,0x93BA0000},
        {0x826FF388,0x7F64DB78},{0x826FF38C,0x4800DE35},{0x826FF390,0x3D6082D6},
        {0x826FF394,0x39000001},{0x826FF398,0x80FB0004},{0x826FF39C,0x3BEB3028},
        {0x826FF3A0,0x38C00000},{0x826FF3A4,0x38BB0038},{0x826FF3A8,0x38800000},
        {0x826FF3AC,0x7C791B78},{0x826FF3B0,0x807F0000},{0x826FF3B4,0x4BD3D20D}}};
    for(const auto& row:instructions)need(PPC_LOAD_U32(row[0])==row[1],"Original skin dispatch/bone instruction changed");
    constexpr std::array<uint32_t,18> alphaSetup={0x2B1E0000,0x419A0060,0x38A00000,0x38800001,0x38600044,0x4BF77789,
        0x38A00001,0x38800001,0x38600006,0x4BF77779,0x38A00001,0x38800006,0x38600009,0x4BF77769,
        0x38A00001,0x38800007,0x3860000A,0x4BF77759};
    constexpr std::array<uint32_t,14> alphaCleanup={0x2B1E0000,0x419A0030,0x38A00000,0x38800000,0x38600044,0x4BF776A5,
        0x578B063E,0x2B0B0000,0x419A0014,0x38A00001,0x38800001,0x38600003,0x4BF77689,0x38600000};
    for(uint32_t i=0;i<alphaSetup.size();++i)need(PPC_LOAD_U32(0x827401CC+4*i)==alphaSetup[i],"Original skin alpha blend producer changed");
    for(uint32_t i=0;i<alphaCleanup.size();++i)need(PPC_LOAD_U32(0x827402B0+4*i)==alphaCleanup[i],"Original skin retained blend cleanup changed");
    need(PPC_LOAD_U32(secondTable+family->row*16)==source&&PPC_LOAD_U32(secondTable+family->row*16+12)==0x823CA960,
         "Original skin catalog association changed");
    need(PPC_LOAD_U32(source+4)+12==family->bytes&&PPC_LOAD_U32(body+0x118)==family->descriptors&&
         PPC_LOAD_U32(body+0x130)==family->leaves&&PPC_LOAD_U32(body+0x138)==family->words*4,
         "Original skin descriptor/leaf/storage count differs");
    need(body+PPC_LOAD_U32(body+family->alphaContext+0x48)+8==family->alphaVertex&&
         body+PPC_LOAD_U32(body+family->alphaContext+0x4C)+8==family->alphaPixel,"Original skin alpha shader association differs");
    const auto descriptors=body+PPC_LOAD_U32(body+0x108);
    for(const auto& input:materialInputs()) {
        const auto d=descriptors+8*(input.handle>>18);
        need(!(PPC_LOAD_U32(d)&3)&&4*(PPC_LOAD_U32(d+4)&0xFFFF)==input.word,"Original skin material storage differs");
    }
}
std::array<uint32_t,2> loadTextures(Runtime& rt,const PPCContext& entry,const char* path) {
    auto* base=rt.base;EngineCpuCalls cpu(entry,base);
    std::ifstream input(path,std::ios::binary|std::ios::ate);need(bool(input),"Original ITXD dictionary required");
    const auto length=input.tellg();need(length>0&&length<=0x200000,"ITXD fixture extent differs");
    const auto bytes=uint32_t(length);rt.map(dictionary,bytes,true,"original skin texture dictionary");
    input.seekg(0);input.read(reinterpret_cast<char*>(rt.pointer(dictionary,bytes,true)),bytes);
    need(bool(input),"Original dictionary read failed");const auto original=snapshot(rt,dictionary,bytes);
    PPC_STORE_U32(area+0x80,dictionary);PPC_STORE_U32(area+0x84,bytes);
    const auto stream=cpu.invoke(0x823F9598,3,1,area+0x80);need(stream,"Original memory stream construction failed");
    uint32_t group=0;
    {
        EngineCpuCalls load(entry,base);load.registers().lr=0x8271191C;
        const auto request=load.registers().r1.u32+0x60;std::memset(rt.pointer(request,0x18,true),0,0x18);
        PPC_STORE_U32(area+0x90,0x534B494E);PPC_STORE_U32(request+4,area+0x90);
        PPC_STORE_U32(request+0x10,stream);PPC_STORE_U32(request+0x14,bytes);
        const auto saved=fullAbi(load.registers());group=load.invoke(0x826F26B0,0,request);
        need(group&&fullAbi(load.registers())==saved,"Original dictionary parent loader lost return or ABI");
    }
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
    auto* base=rt.base;const bool dual=source==0x8201CD48;const uint32_t stride=dual?56:48;
    PPC_STORE_U32(packet,metadata);PPC_STORE_U32(packet+4,object);PPC_STORE_U32(packet+8,camera);
    PPC_STORE_U8(packet+0xC,1);PPC_STORE_U32(packet+0x18,typed);PPC_STORE_U32(packet+0x1C,shadow);PPC_STORE_U32(packet+0x20,shadow);
    PPC_STORE_U32(object,0x01000500);PPC_STORE_U32(object+4,objectFrame);
    PPC_STORE_U32(object+8,object+8);PPC_STORE_U32(object+0xC,object+8);PPC_STORE_U32(object+0x10,0x823CD3D8);
    PPC_STORE_U32(object+0x18,data);PPC_STORE_U32(object+0x3C,property);PPC_STORE_U32(data+0x24,offsets);
    PPC_STORE_U32(objectFrame+0xA0,objectFrame);identity(base,objectFrame+0x10);PPC_STORE_U8(property,0x40);
    PPC_STORE_U32(metadata,0x00030002);PPC_STORE_U32(metadata+4,0xB5F8FBF2);PPC_STORE_U32(metadata+0xC,geometry);
    PPC_STORE_U32(metadata+0x10,1);PPC_STORE_U32(metadata+0x14,submeshes);PPC_STORE_U32(metadata+0x18,1);
    PPC_STORE_U32(metadata+0x24,2);PPC_STORE_U32(metadata+0x28,boneMap);PPC_STORE_U32(metadata+0x34,collection);
    PPC_STORE_U32(geometry,4*stride);PPC_STORE_U32(geometry+4,stride);PPC_STORE_U32(geometry+8,dual?14:13);
    PPC_STORE_U32(geometry+0xC,elements);PPC_STORE_U32(geometry+0x10,vertices);PPC_STORE_U32(geometry+0x14,8);
    PPC_STORE_U32(geometry+0x18,1);PPC_STORE_U32(geometry+0x1C,indices);PPC_STORE_U32(geometry+0x30,declCache);
    PPC_STORE_U32(geometry+0x50,vertices|3);PPC_STORE_U32(geometry+0x54,2);PPC_STORE_U32(geometry+0x58,0x20000002);
    PPC_STORE_U32(geometry+0x70,indices);PPC_STORE_U32(geometry+0x74,8);PPC_STORE_U32(declCache+4,declCache+0x40);
    uint32_t row=0;const auto add=[&](uint32_t offset,uint32_t type,uint32_t semantic){
        PPC_STORE_U32(elements+12*row,offset);PPC_STORE_U32(elements+12*row+4,type);PPC_STORE_U32(elements+12*row+8,semantic);++row;};
    add(0,0x002A23B9,0);add(12,0x002A2187,0x00030000);add(16,0x002C23A5,0x00050000);
    if(dual)add(24,0x002C23A5,0x00050100);
    const uint32_t influence=dual?32:24,weight=influence+4,color=weight+16;
    add(influence,0x001A2286,0x00020000);add(weight,0x001A23A6,0x00010000);add(color,0x00182886,0x000A0000);
    for(uint32_t morph=1;morph<=6;++morph)add(morph<<16,0x002A23B9,morph<<8);add(0x00FF0000,UINT32_MAX,0);
    constexpr float xy[][2]={{-.5f,.5f},{-.5f,-.5f},{.5f,.5f},{.5f,-.5f}};
    for(uint32_t i=0;i<4;++i){
        PPC_STORE_U32(vertices+stride*i,std::bit_cast<uint32_t>(xy[i][0]));PPC_STORE_U32(vertices+stride*i+4,std::bit_cast<uint32_t>(xy[i][1]));
        PPC_STORE_U32(vertices+stride*i+8,0x3F000000);PPC_STORE_U32(vertices+stride*i+12,511u<<10);
        PPC_STORE_U32(vertices+stride*i+16,0x3E800000);PPC_STORE_U32(vertices+stride*i+20,0x3E800000);
        if(dual){PPC_STORE_U32(vertices+stride*i+24,0x3E800000);PPC_STORE_U32(vertices+stride*i+28,0x3E800000);}
        PPC_STORE_U32(vertices+stride*i+influence,i>=2?1:0);PPC_STORE_U32(vertices+stride*i+weight,0x3F800000);
        PPC_STORE_U32(vertices+stride*i+color,0xCC6699B3);PPC_STORE_U16(indices+2*i,uint16_t(i));
    }
    PPC_STORE_U32(submeshes,2);PPC_STORE_U32(submeshes+12,6);PPC_STORE_U32(submeshes+24,4);
    PPC_STORE_U32(offsets+8,0x30);PPC_STORE_U32(collection,1);PPC_STORE_U32(collection+12,material);
    // The same genuine plugin graph used by OriginalMonoPass: the original
    //826FE7C8 composes these matrices and resolves the authored byte bone map.
    const auto atomicOffset=PPC_LOAD_U32(0x82E27994),skinOffset=PPC_LOAD_U32(0x82E27998);
    need(atomicOffset>=0x28&&atomicOffset<0x1000&&skinOffset>0x24&&skinOffset<0x1000,
         "Original skeleton plugin offsets overlap skin fixture fields");
    need(PPC_LOAD_U32(0x82CF05EC)<0x1000&&PPC_LOAD_U32(0x82D6CAB0)<0x1000&&
         PPC_LOAD_U32(0x82D6CAB0)!=skinOffset&&PPC_LOAD_U32(0x82D6CAB0)!=0x24,"Original auxiliary plugin offsets overlap skin fixture");
    PPC_STORE_U32(object+atomicOffset,hierarchy);PPC_STORE_U32(data+skinOffset,skinPlugin);
    PPC_STORE_U32(hierarchy+8,boneMatrices);PPC_STORE_U32(skinPlugin+12,bindMatrices);
    identity(base,bindMatrices);identity(base,bindMatrices+64);identity(base,boneMatrices);identity(base,boneMatrices+64);
    PPC_STORE_U32(boneMatrices+48,std::bit_cast<uint32_t>(-.125f));PPC_STORE_U32(boneMatrices+112,std::bit_cast<uint32_t>(.125f));
    PPC_STORE_U8(boneMap,1);PPC_STORE_U8(boneMap+1,0);
    const auto inputs=materialInputs();const auto defaults=body+PPC_LOAD_U32(body+0x128);
    const auto bindings=PPC_LOAD_U32(typed+0x28),bindingCount=PPC_LOAD_U32(typed+0x24);
    need(bindingCount&&bindingCount<=64,"Original skin material binding extent differs");
    for(uint32_t i=0;i<inputs.size();++i){
        uint32_t index=UINT32_MAX;for(uint32_t j=0;j<bindingCount;++j)if(PPC_LOAD_U32(bindings+24*j)==inputs[i].handle)index=j;
        need(index!=UINT32_MAX,"Original skin material binding missing");const bool texture=inputs[i].texture!=noTexture;const auto value=values+16*i;
        PPC_STORE_U32(materialRows+12*i,0x00400000|(index<<16)|(texture?1u:3u));PPC_STORE_U32(materialRows+12*i+8,value);
        if(texture)PPC_STORE_U32(value+4,textures[inputs[i].texture]);
        else{
            std::memcpy(rt.pointer(value,16,true),rt.pointer(defaults+4*inputs[i].word,16,false),16);
            // Authored fixture vectors disable optional rim/morph/cutout work;
            // real callbacks still carry every lane through original storage.
            for(uint32_t lane=0;lane<4;++lane)PPC_STORE_U32(value+4*lane,0);
            if(inputs[i].handle==0x0048001C)PPC_STORE_U32(value+4,0x3F800000);
        }
    }
    PPC_STORE_U32(material+0xC,uint32_t(inputs.size())<<10);PPC_STORE_U32(material+0x14,materialRows);
    if(paddedLayout) {
        const auto original=snapshot(rt,vertices,4*stride);
        std::memset(rt.pointer(vertices,4*80,true),0xFF,4*80);
        for(uint32_t i=0;i<4;++i)std::memcpy(rt.pointer(vertices+80*i+16,stride,true),original.data()+stride*i,stride);
        for(uint32_t i=0;i+1<row;++i)
            if(!(PPC_LOAD_U32(elements+12*i)>>16))PPC_STORE_U32(elements+12*i,PPC_LOAD_U32(elements+12*i)+16);
        PPC_STORE_U32(geometry,4*80);PPC_STORE_U32(geometry+4,80);
    }
}
void originalGroupPoolEvidence(uint8_t* base) {
    constexpr std::array<std::array<uint32_t,2>,39> pins={{{0x8282F4D0,0x3D6082E0},
        {0x8282F4D4,0x906B6F74},{0x8282F4D8,0x3D6082E0},{0x8282F4DC,0x908B6F78},{0x8282F4E0,0x4E800020},
        {0x82862068,0x3FC082D5},{0x82862078,0x409A000C},{0x8286207C,0x4BE2C775},
        {0x82862084,0x38800000},{0x82862088,0x4BFCD449},{0x8282F628,0x3BC00000},
        {0x8282F634,0x2B1F0000},{0x8282F644,0x2B04000C},{0x8282F654,0x4BBC9A6D},
        {0x8282F668,0x4BBC8321},{0x8282F684,0x93DF0010},{0x8282F690,0x4BFFFE59},
        {0x8282F6AC,0x4BBC9A15},{0x8282F6D0,0x4BFFFE89},{0x8282F6EC,0x4BBC99D5},{0x8282F6F8,0x48001B89},
        {0x82831280,0x81430014},{0x82831284,0x3963001C},{0x82831294,0x814B0004},
        {0x8283129C,0x814B0000},{0x828312A0,0x7D2A182E},{0x828312A8,0x7D291A14},
        {0x828312B0,0x7D292214},{0x828312B8,0x7D2A192E},{0x8273B780,0x817D0000},
        {0x8273B7B0,0x484E4301},{0x8273B7DC,0x484E436D},{0x8273B7F4,0x4BFC63E5},
        {0x8273B7F8,0x907F0030},{0x8282F8A0,0x807E6F74},{0x8282F8B0,0x4E800421},
        {0x8282F8D4,0x4E800421},{0x8282F8DC,0x917F000C},{0x8282F8E0,0x917F0010}}};
    for(const auto& pin:pins)need(PPC_LOAD_U32(pin[0])==pin[1],"Original group reader/initializer/relocation/setup/release instruction changed");
}
struct OriginalGroupPool {
    static constexpr uint32_t firstBytes=0x100,rowOffset=0x80;
    uint32_t header{},wire{},wireBytes{},first{},second{},groups{},secondBytes{};
    uint64_t firstGeneration{},secondGeneration{};
    std::vector<uint8_t> originalWire;
    void read(Runtime& rt,EngineCpuCalls& cpu,uint32_t serialized,uint32_t owner,uint32_t count) {
        auto* base=rt.base;need(count==1||count==65,"Group fixture count is not source-qualified");
        header=owner;wire=serialized;groups=count;secondBytes=2*groups;wireBytes=12+firstBytes+secondBytes;
        std::memset(rt.pointer(header,0x20,true),0,0x20);
        std::memset(rt.pointer(wire,wireBytes,true),0,wireBytes);
        // Only the reader's twelve-byte prefix is LE. Its opaque first and
        // second pools, including relocation rows and submesh words, stay BE.
        const auto little=[&](uint32_t at,uint32_t value){for(uint32_t i=0;i<4;++i)PPC_STORE_U8(at+i,uint8_t(value>>(8*i)));};
        little(wire,16);little(wire+4,firstBytes);little(wire+8,secondBytes);
        const auto rawFirst=wire+12,rawRow=rawFirst+rowOffset,rawSecond=rawFirst+firstBytes;
        PPC_STORE_U32(rawFirst+20,1);PPC_STORE_U32(rawFirst+28,rowOffset+32);PPC_STORE_U32(rawFirst+32,1);
        PPC_STORE_U32(rawRow,2);PPC_STORE_U32(rawRow+12,6);PPC_STORE_U32(rawRow+24,4);
        PPC_STORE_U32(rawRow+28,groups);PPC_STORE_U32(rawRow+32,0);
        PPC_STORE_U8(rawSecond,63);PPC_STORE_U8(rawSecond+1,2);
        for(uint32_t i=1;i<groups;++i){PPC_STORE_U8(rawSecond+2*i,65);PPC_STORE_U8(rawSecond+2*i+1,0);}
        originalWire=snapshot(rt,wire,wireBytes);
        PPC_STORE_U32(area+0xD700,wire);PPC_STORE_U32(area+0xD704,wireBytes);
        const auto before=fullAbi(cpu.registers());
        const auto stream=cpu.invoke(0x823F9598,3,1,area+0xD700);
        need(stream&&fullAbi(cpu.registers())==before,"Original group memory stream constructor/ABI differs");
        need(cpu.invoke(0x8282F618,stream,wireBytes,header,0)==stream&&fullAbi(cpu.registers())==before,
             "Original group two-pool reader/relocation failed or changed ABI");
        need(cpu.invoke(0x823F94A0,stream,0)==1&&fullAbi(cpu.registers())==before,
             "Original group memory stream close/ABI differs");
        first=PPC_LOAD_U32(header+12);second=PPC_LOAD_U32(header+16);
        need(first&&second&&first!=second&&!(first&15)&&!(second&15)&&
             PPC_LOAD_U32(header)==16&&PPC_LOAD_U32(header+4)==firstBytes&&PPC_LOAD_U32(header+8)==secondBytes,
             "Original group reader did not publish its two exact pools");
        need(PPC_LOAD_U32(first+20)==1&&PPC_LOAD_U32(first+28)==rowOffset+32&&PPC_LOAD_U32(first+32)==1&&
             PPC_LOAD_U32(first+rowOffset+28)==groups&&PPC_LOAD_U32(first+rowOffset+32)==second,
             "Original group range field was not relocated through selector1");
        same(rt,second,std::vector<uint8_t>(originalWire.begin()+12+firstBytes,originalWire.end()),"Original group second pool changed authored ranges");
        same(rt,wire,originalWire,"Original group reader changed immutable stream bytes");
        firstGeneration=rt.engineAudio->allocationGeneration(first,firstBytes);
        secondGeneration=rt.engineAudio->allocationGeneration(second,secondBytes);
        const auto firstOwner=rt.engineAudio->allocationSpan(first),secondOwner=rt.engineAudio->allocationSpan(second);
        need(firstOwner&&secondOwner&&firstOwner->address==first&&firstOwner->extent==firstBytes&&
             firstOwner->generation==firstGeneration&&secondOwner->address==second&&
             secondOwner->extent==secondBytes&&secondOwner->generation==secondGeneration,
             "Original group reader lost exact logical pool ownership");
        std::printf("AUDIT_GROUP_POOL_CREATE groups=%u first=%08X bytes=%u generation=%llu second=%08X bytes=%u generation=%llu reader=8282F618 relocation=82831280 stream_close=passed ABI=passed\n",
            groups,first,firstBytes,static_cast<unsigned long long>(firstGeneration),second,secondBytes,static_cast<unsigned long long>(secondGeneration));
    }
    uint32_t row() const {return first+rowOffset;}
    void release(Runtime& rt,EngineCpuCalls& cpu) const {
        auto* base=rt.base;const auto before=fullAbi(cpu.registers());
        need(cpu.invoke(0x8282F878,header,0)==header&&fullAbi(cpu.registers())==before,
             "Original group pool retirement/ABI differs");
        need(!PPC_LOAD_U32(header+12)&&!PPC_LOAD_U32(header+16)&&!PPC_LOAD_U32(header+8),
             "Original group pool retirement retained published owners");
        rejects([&]{rt.engineAudio->allocationGeneration(first,firstBytes);},"Retired original first group pool remained live");
        rejects([&]{rt.engineAudio->allocationGeneration(second,secondBytes);},"Retired original second group pool remained live");
        need(!rt.engineAudio->allocationSpan(first)&&!rt.engineAudio->allocationSpan(second),
             "Retired original group pool retained a borrowed allocation owner");
        same(rt,wire,originalWire,"Original group retirement changed its immutable stream source");
    }
};

// Original allocation/reader/relocation owns every synthetic row.
// Allocator generations are queried at exact bases; row views use containing spans.
// This synthetic range fixture grants no shipped-asset or gameplay coverage.
struct OriginalSubmeshPool {
    static constexpr uint32_t rowOffset=0x80,pointerField=0x40;
    uint32_t header{},wire{},wireBytes{},first{},firstBytes{},count{};
    uint64_t firstGeneration{},wireGeneration{};
    std::vector<uint8_t> originalWire,relocatedFirst;
    void read(Runtime& rt,EngineCpuCalls& cpu,uint32_t owner,uint32_t rows,uint32_t options) {
        auto* base=rt.base;need(rows==1||rows==65536,"Submesh candidate count differs");
        header=owner;count=rows;
        need(uint64_t(rowOffset)+uint64_t(count)*36<=UINT32_MAX,"Submesh candidate byte owner wraps");
        firstBytes=rowOffset+36*count;wireBytes=12+firstBytes;
        std::memset(rt.pointer(header,0x20,true),0,0x20);
        const auto saved=fullAbi(cpu.registers());wire=cpu.invoke(0x8269BF70,wireBytes,options);
        need(wire&&fullAbi(cpu.registers())==saved,"Original immutable submesh stream allocation/ABI differs");
        wireGeneration=rt.engineAudio->allocationGeneration(wire,wireBytes);
        std::memset(rt.pointer(wire,wireBytes,true),0,wireBytes);
        const auto little=[&](uint32_t at,uint32_t value){for(uint32_t i=0;i<4;++i)PPC_STORE_U8(at+i,uint8_t(value>>(8*i)));};
        little(wire,16);little(wire+4,firstBytes);little(wire+8,0);
        const auto rawFirst=wire+12;
        PPC_STORE_U32(rawFirst,0xBFBFBFBF);PPC_STORE_U32(rawFirst+4,0x01000000);
        PPC_STORE_U32(rawFirst+20,1);PPC_STORE_U32(rawFirst+24,0);
        PPC_STORE_U32(rawFirst+28,pointerField);PPC_STORE_U32(rawFirst+32,0);
        PPC_STORE_U32(rawFirst+pointerField,rowOffset);
        for(uint32_t i=0;i<count;++i) {
            const auto row=rawFirst+rowOffset+36*i;
            PPC_STORE_U32(row,i+1<count?7u:2u);PPC_STORE_U32(row+4,0);
            PPC_STORE_U32(row+12,6);PPC_STORE_U32(row+24,4);
        }
        originalWire=snapshot(rt,wire,wireBytes);
        PPC_STORE_U32(area+0xD9C0,wire);PPC_STORE_U32(area+0xD9C4,wireBytes);
        const auto stream=cpu.invoke(0x823F9598,3,1,area+0xD9C0);
        need(stream&&fullAbi(cpu.registers())==saved,"Original submesh memory stream constructor/ABI differs");
        need(cpu.invoke(0x8282F618,stream,wireBytes,header,0)==stream&&fullAbi(cpu.registers())==saved,
             "Original submesh first-pool reader/relocation failed or changed ABI");
        need(cpu.invoke(0x823F94A0,stream,0)==1&&fullAbi(cpu.registers())==saved,
             "Original submesh stream close/ABI differs");
        first=PPC_LOAD_U32(header+12);
        need(first&&!(first&15)&&PPC_LOAD_U32(header)==16&&PPC_LOAD_U32(header+4)==firstBytes&&
             !PPC_LOAD_U32(header+8)&&!PPC_LOAD_U32(header+16)&&PPC_LOAD_U32(first+pointerField)==first+rowOffset,
             "Original first-pool reader did not publish exact relocated submesh owner");
        firstGeneration=rt.engineAudio->allocationGeneration(first,firstBytes);
        const auto owned=rt.engineAudio->allocationSpan(row());
        need(owned&&owned->address==first&&owned->extent==firstBytes&&owned->generation==firstGeneration&&
             uint64_t(row())-owned->address+uint64_t(36)*count==owned->extent,
             "Original submesh rows lost complete logical allocator ownership");
        rejects([&]{rt.engineAudio->allocationGeneration(first,firstBytes+1);},
                "Submesh logical owner admitted an extra byte");
        relocatedFirst=snapshot(rt,first,firstBytes);
        same(rt,wire,originalWire,"Original submesh reader changed immutable serialized bytes");
        std::printf("AUDIT_SUBMESH_POOL_CREATE count=%u row_bytes=%u first_bytes=%u reader=8282F618 relocation=82831280 selector=0 stream_close=passed logical_owner=passed ABI=passed\n",count,36*count,firstBytes);
    }
    uint32_t row() const {return first+rowOffset;}
    void unchanged(Runtime& rt) const {
        same(rt,first,relocatedFirst,"Original submesh traversal changed owned relocated rows");
        same(rt,wire,originalWire,"Original submesh traversal changed immutable serialized bytes");
        const auto owned=rt.engineAudio->allocationSpan(row());
        need(owned&&owned->address==first&&owned->extent==firstBytes&&owned->generation==firstGeneration&&
             uint64_t(row())-owned->address+uint64_t(36)*count==owned->extent&&
             rt.engineAudio->allocationGeneration(first,firstBytes)==firstGeneration&&
             rt.engineAudio->allocationGeneration(wire,wireBytes)==wireGeneration,
             "Original submesh traversal lost logical row ownership");
    }
    void release(Runtime& rt,EngineCpuCalls& cpu) const {
        auto* base=rt.base;unchanged(rt);const auto saved=fullAbi(cpu.registers());
        need(cpu.invoke(0x8282F878,header,0)==header&&fullAbi(cpu.registers())==saved,
             "Original submesh pool retirement/ABI differs");
        need(!PPC_LOAD_U32(header+12)&&!PPC_LOAD_U32(header+16)&&!PPC_LOAD_U32(header+8),
             "Original submesh pool retirement retained published storage");
        rejects([&]{rt.engineAudio->allocationGeneration(first,firstBytes);},
                "Retired original submesh row owner remained live");
        need(!rt.engineAudio->allocationSpan(first)&&!rt.engineAudio->allocationSpan(row()),
             "Retired original submesh base/interior owner remained borrowed");
        same(rt,wire,originalWire,"Original submesh retirement changed immutable stream source");
        cpu.invoke(0x8269BF10,wire);need(fullAbi(cpu.registers())==saved,"Original stream-source free damaged ABI");
        rejects([&]{rt.engineAudio->allocationGeneration(wire,wireBytes);},
                "Original submesh serialized source remained live after free");
    }
};

std::string auditString(const std::string& row,const char* field) {
    const std::string marker=std::string("\"")+field+"\":\"";
    const auto at=row.find(marker);need(at!=std::string::npos,"Submesh audit string field is absent");
    std::string result;
    for(size_t i=at+marker.size();i<row.size();++i) {
        if(row[i]=='"')return result;
        if(row[i]=='\\') {
            need(++i<row.size()&&(row[i]=='"'||row[i]=='\\'||row[i]=='/'),"Unexpected submesh audit escape");
        }
        result+=row[i];
    }
    need(false,"Unterminated submesh audit field");return {};
}
std::string auditHex(uint32_t value) {char text[9];std::snprintf(text,sizeof(text),"%08X",value);return text;}
bool auditWord(const std::string& text,const char* name,const std::string& value) {
    const auto token=std::string(name)+"="+value;const auto at=text.find(token);
    return at!=std::string::npos&&(at+token.size()==text.size()||text[at+token.size()]==' ');
}
std::vector<std::string> submeshAuditRows() {
    std::ifstream input(submeshAuditPath);need(bool(input),"Preserved submesh entry audit log is missing");
    std::vector<std::string> result;std::string line;
    while(std::getline(input,line))if(line.find("boundary=skin_mesh_entry ")!=std::string::npos&&
        line.find("\"kind\":\"effect_producer_entry\"")!=std::string::npos)result.push_back(line);
    return result;
}
void verifySubmeshAudit(const std::string& line,const OriginalSubmeshPool& pool,uint32_t count,uint32_t rows,
                        uint32_t typed,const char* action,const char* event) {
    need(auditString(line,"event")==event&&auditString(line,"asset")=="source:"+auditHex(source)&&
         line.find("\"caller\":"+std::to_string(0x82740294u)+",")!=std::string::npos&&
         auditString(line,"mission")=="synthetic-submesh65536-fixture"&&auditString(line,"last_action")==action,
         "Submesh pre-validation receipt lost its source/caller/mission/action");
    const auto parameters=auditString(line,"parameters"),ownership=auditString(line,"ownership"),instance=auditString(line,"instance");
    need(auditWord(parameters,"boundary","skin_mesh_entry")&&auditWord(parameters,"source",auditHex(source))&&
         auditWord(parameters,"requested_technique","00000000")&&auditWord(parameters,"argument5_role","typed-mesh-owner")&&
         auditWord(parameters,"submesh_count",std::to_string(count))&&auditWord(parameters,"bones","2")&&
         auditWord(parameters,"submesh_owner","observed-live")&&auditWord(ownership,"admission","unvalidated")&&
         auditWord(ownership,"runtime_match","1")&&auditWord(ownership,"thread_match","1")&&auditWord(ownership,"cpu_match","1"),
         "Submesh pre-validation receipt lost its raw count or unvalidated owner state");
    need(auditWord(instance,"r3",auditHex(metadata))&&auditWord(instance,"r4",auditHex(object))&&
         auditWord(instance,"r5",auditHex(typed))&&auditWord(instance,"r7",auditHex(count))&&
         auditWord(instance,"submeshes",auditHex(rows))&&auditWord(instance,"submesh_owner_base",auditHex(pool.first))&&
         auditWord(instance,"submesh_owner_extent",std::to_string(pool.firstBytes))&&
         auditWord(instance,"submesh_owner_generation",std::to_string(pool.firstGeneration)),
         "Submesh receipt hid the actual array pointer/logical extent/generation");
    const auto group=auditString(line,"group");
    need(group.find("submeshes=")==std::string::npos&&group.find("submesh_owner_base=")==std::string::npos&&
         group.find("submesh_owner_extent=")==std::string::npos&&group.find("submesh_owner_generation=")==std::string::npos,
         "Submesh instance addresses/generation leaked into the stable combination group");
}

struct Scenario {const char* name;uint32_t bones,submeshes,skipped;};
void layout(Runtime& rt,const Scenario& shape,const OriginalGroupPool* grouped=nullptr,const OriginalSubmeshPool* streamed=nullptr) {
    auto* base=rt.base;need(shape.bones>=1&&shape.bones<=255&&shape.submeshes>=1&&(streamed?shape.submeshes==streamed->count:shape.submeshes<=3)&&
        shape.skipped<shape.submeshes,"Original skin scenario extent differs");
    PPC_STORE_U32(metadata+0x24,shape.bones);PPC_STORE_U32(metadata+0x10,shape.submeshes);
    // The original palette may contain repeated authored joint indices. Two
    // genuine joint/bind owners therefore compose every requested entry,
    // including the255-entry bounded scratch case, without fake matrices.
    for(uint32_t bone=0;bone<shape.bones;++bone)PPC_STORE_U8(boneMap+bone,uint8_t(1-(bone&1)));
    PPC_STORE_U32(collection,2);PPC_STORE_U32(collection+12,material);PPC_STORE_U32(collection+16,material);
    PPC_STORE_U32(offsets+28,0x70);PPC_STORE_U16(headers+0x70,0x20);
    if(!streamed)for(uint32_t i=0;i<shape.submeshes;++i) {
        const auto row=submeshes+36*i;std::memset(rt.pointer(row,36,true),0,36);
        PPC_STORE_U32(row,i<shape.skipped?7u:2u);PPC_STORE_U32(row+4,i?1u:0u);
        PPC_STORE_U32(row+12,6);PPC_STORE_U32(row+24,4);
        if(shape.bones>64&&i>=shape.skipped) {
            const uint32_t table=boneGroups+16*i;
            const bool multiple=i==shape.submeshes-1;
            PPC_STORE_U32(row+28,multiple?3u:1u);PPC_STORE_U32(row+32,table);
            PPC_STORE_U8(table,uint8_t(shape.bones-(multiple?1u:2u)));PPC_STORE_U8(table+1,multiple?1u:2u);
            if(multiple){PPC_STORE_U8(table+2,uint8_t(shape.bones-2));PPC_STORE_U8(table+3,1);
                // The original memcpy receives a valid empty trailing range.
                PPC_STORE_U8(table+4,uint8_t(shape.bones));PPC_STORE_U8(table+5,0);}
        }
    }
    const uint32_t stride=PPC_LOAD_U32(geometry+4),influence=(source==0x8201CD48?32u:24u)+(paddedLayout?16u:0u);
    for(uint32_t i=0;i<4;++i)PPC_STORE_U32(vertices+stride*i+influence,shape.bones==1?0u:uint32_t(i>=2));
    if(grouped) {
        need(shape.bones==65&&shape.submeshes==1&&!shape.skipped,"Original streamed group shape differs");
        // This row and its pointer were created/relocated by8282F618. Only the
        // metadata's ordinary borrowed row list is selected here.
        PPC_STORE_U32(metadata+0x14,grouped->row());
        need(PPC_LOAD_U32(grouped->row()+28)==grouped->groups&&PPC_LOAD_U32(grouped->row()+32)==grouped->second,
             "Original streamed group row changed before traversal");
    } else if(streamed) {
        need(shape.bones==2&&shape.skipped+1==shape.submeshes,"Original streamed submesh shape differs");
        streamed->unchanged(rt);PPC_STORE_U32(metadata+0x14,PPC_LOAD_U32(streamed->first+OriginalSubmeshPool::pointerField));
    } else PPC_STORE_U32(metadata+0x14,submeshes);
}
uint32_t composedWord(uint32_t bone,uint32_t lane) {
    return lane%5==0?0x3F800000u:lane==12?std::bit_cast<uint32_t>((bone&1)?-.125f:.125f):0u;
}
void run(Runtime& rt,const PPCContext& entry,const char* dictionaryPath) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();EngineCpuCalls cpu(entry,base);
    OriginalDrawCatalogCleanup cleanup(rt);
    stage="original skin family instruction and binding evidence";originalEvidence(base);
    rt.map(area,0x10000,true,"original skin dispatcher fixture");std::memset(rt.pointer(area,0x10000,true),0,0x10000);
    stage="real skin family catalog construction";
    const auto context=PPC_LOAD_U32(0x82D5DA74),options=cpu.registers().r1.u32+0x60;
    PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    const auto manager=cpu.invoke(0x8269BF70,0x260,options);cpu.invoke(0x826B6F60,manager,context);
    need(!cpu.invoke(0x827019E8,table,25)&&!cpu.invoke(0x827019E8,secondTable,24),"Original catalog registration failed");
    cpu.invoke(0x826B7218,manager);
    const auto typed=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(secondTable+family->row*16+4));
    const auto shadow=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(table+4*16+4));
    need(typed&&shadow&&PPC_LOAD_U32(typed)==0x82061714,"Original skin/shared shadows typed owner missing");
    const auto id=PPC_LOAD_U32(typed+0x1C),wrapper=PPC_LOAD_U32(typed+0x18);
    need(effects.count()==49&&effects.view(id).source==source&&effects.view(id).defaultVectorWords.size()==family->words,
         "Original skin family source or private bank differs");
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
    uint32_t ownedDeclaration=0,ownedAlternate=0;
    std::array<OriginalGroupPool,3> streamedPools;
    const OriginalGroupPool* activeGroups=nullptr;
    std::array<OriginalSubmeshPool,2> submeshPools;const OriginalSubmeshPool* activeSubmeshes=nullptr;
    const uint32_t previousPoolManager=PPC_LOAD_U32(0x82E06F74),previousPoolArgument=PPC_LOAD_U32(0x82E06F78);
    if(groups65Layout||submeshes65536Layout) {
        stage="source-qualified original two-pool reader initialization";originalGroupPoolEvidence(base);
        const auto before=fullAbi(cpu.registers());
        // Original82862068..8C loads this manager or obtains its real singleton
        // through8268E7F0, then invokes8282F4D0(manager,0). No guest allocator
        // global, callback or replacement visitor is manufactured by the test.
        auto poolManager=PPC_LOAD_U32(0x82D57244);
        if(!poolManager)poolManager=cpu.invoke(0x8268E7F0);
        need(poolManager&&PPC_LOAD_U32(poolManager)==0x820B60B8,"Original group allocator singleton is not initialized");
        cpu.invoke(0x8282F4D0,poolManager,0);
        need(fullAbi(cpu.registers())==before&&PPC_LOAD_U32(0x82E06F74)==poolManager&&!PPC_LOAD_U32(0x82E06F78),
             "Original group allocator publication/ABI differs");
        if(groups65Layout) {
        stage="original memory-stream two-pool allocation and relocation";
        streamedPools[0].read(rt,cpu,area+0xD100,area+0xD000,1);
        streamedPools[1].read(rt,cpu,area+0xD300,area+0xD040,65);
        streamedPools[2].read(rt,cpu,area+0xD500,area+0xD080,65);
        need(streamedPools[1].second!=streamedPools[2].second&&
             snapshot(rt,streamedPools[1].second,130)==snapshot(rt,streamedPools[2].second,130),
             "Independent original group tables lost identical authored bytes");
        } else {
            stage="original streamed one-row and65536-row allocation/relocation";
            submeshPools[0].read(rt,cpu,area+0xD900,1,options);
            submeshPools[1].read(rt,cpu,area+0xD940,65536,options);
        }
        stage="whole original8273B760 skin geometry and declaration construction";
        need(cpu.invoke(0x8273B760,metadata)==0&&fullAbi(cpu.registers())==before,
             "Original skin geometry construction returned an error or damaged ABI");
        const auto cached=PPC_LOAD_U32(geometry+0x30);
        ownedDeclaration=PPC_LOAD_U32(cached+4);ownedAlternate=PPC_LOAD_U32(cached+8);
        need(ownedDeclaration&&ownedAlternate&&rt.engineAudio->allocationGeneration(ownedDeclaration,0x50)&&
             rt.engineAudio->allocationGeneration(ownedAlternate,0x50),
             "Original group geometry constructor did not own both declarations");
    } else if(paddedLayout||inheritedBlend) {
        // Construct the exact relocated declaration through retail's cache
        // and SDK CPU constructor, then use it throughout the original draws.
        const auto cached=cpu.invoke(0x82701BD8,elements,PPC_LOAD_U32(geometry+8));
        ownedDeclaration=PPC_LOAD_U32(cached+4);ownedAlternate=PPC_LOAD_U32(cached+8);
        need(ownedDeclaration&&ownedAlternate,"Original padded skin declarations missing");
        PPC_STORE_U32(geometry+0x30,cached);
        need(rt.engineAudio->allocationGeneration(ownedDeclaration,0x50)!=0,"Original padded skin declaration has no live allocator owner");
    }
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
        // Isolated Boolean cases own their baseline. A separate inherited
        // sequence below deliberately keeps the preceding original alpha
        // dispatcher's blend state across its following opaque draws.
        for(const auto& row:states)driver.directScalar(base,row[0],row[1]);
        cpu.invoke(0x826B7968,PPC_LOAD_U32(0x82E06F80+0x38),1,1);
    };
    resetPassState();
    const auto bound=driver.cameraBinding();uint32_t dispatcherCalls=0,fallbackCalls=0,vectorCalls=0,textureCalls=0,boneCalls=0,
        boneSetCalls=0,groupCalls=0,expectedAlpha=0,expectedBit1=0,expectedBit2=0,parentCalls=0,drawCalls=0,expectedBoneSets=0,expectedGroupCalls=0;
    Scenario shape{"two joint whole palette",2,1,0};bool firstRejections=false,groupRejections=false,rangeMutationRejected=false;
    uint32_t auxiliaryLookups=0;bool streamRejections=false;
    bool groupPoolPreflight=false,groupPoolPreflightArmed=false,groupPoolIdentity=false,groupPoolEquivalentMutation=false;
    bool submeshPreflight=false,submeshPreflightArmed=false;
    Observation observation([&](uint32_t pc,PPCContext& c,bool returned) {
        if(pc==0x82701638&&!returned&&activeSubmeshes&&activeSubmeshes->count==65536&&submeshPreflightArmed&&!submeshPreflight) {
            const auto saved=c;const auto row=activeSubmeshes->row(),last=row+36*65535;
            need(uint32_t(c.lr)==0x82740294&&c.r3.u32==metadata&&c.r4.u32==object&&c.r5.u32==typed&&
                 PPC_LOAD_U32(metadata+0x10)==65536&&PPC_LOAD_U32(metadata+0x14)==row,
                 "Original large-submesh preflight lost its actual parent/row owner");
            const auto before=effects.view(id).defaultVectorWords;const auto mask=effects.privateModifiedMask(id);
            const auto composed=snapshot(rt,0x82D64080,2*64),selected=snapshot(rt,0x82D63070,64*64);
            const auto lastBytes=snapshot(rt,last,36);
            const auto reject=[&](const std::function<void()>& mutate,const char* message,const char* expected,const char* action) {
                c=saved;c.lastFunction=pc;mutate();rt.resourceAudit.action(action);bool failed=false;
                std::array<uint8_t,sizeof(PPCContext)> beforeContext{};std::memcpy(beforeContext.data(),&c,sizeof(c));
                const auto savedCsr=PPCFPSCRRegister::getcsr();const auto savedError=GetLastError();
                const auto probeCsr=(savedCsr&~0x6000u)|0x4000u;constexpr DWORD probeError=0xA17D6536;
                PPCFPSCRRegister::restoreHostCSR(probeCsr);SetLastError(probeError);
                uint32_t returnedCsr{};DWORD returnedError{};std::string reason;
                try{effects.skinMeshOperation(c,base,pc);}catch(const Failure& error){
                    returnedCsr=PPCFPSCRRegister::getcsr();returnedError=GetLastError();failed=true;reason=error.what();
                }
                PPCFPSCRRegister::restoreHostCSR(savedCsr);SetLastError(savedError);
                need(failed,message);need(reason==expected,"Large-submesh negative rejected at the wrong original frontier");
                need(returnedCsr==probeCsr&&returnedError==probeError&&
                     !std::memcmp(beforeContext.data(),&c,sizeof(c)),"Large-submesh rejection changed PPC/CSR/LastError");
                const auto entries=submeshAuditRows();need(!entries.empty(),"Large-submesh rejection omitted its entry receipt");
                verifySubmeshAudit(entries.back(),*activeSubmeshes,PPC_LOAD_U32(metadata+0x10),PPC_LOAD_U32(metadata+0x14),typed,action,"encounter");
                rt.resourceAudit.failure(reason);
                const auto failureRows=submeshAuditRows();need(failureRows.size()==entries.size()+1,
                    "Large-submesh failure was not paired after its entry observation");
                verifySubmeshAudit(failureRows.back(),*activeSubmeshes,PPC_LOAD_U32(metadata+0x10),PPC_LOAD_U32(metadata+0x14),typed,action,"failure");
                need(auditString(failureRows.back(),"reason")==expected,"Large-submesh failure receipt changed the rejection");
                PPC_STORE_U32(metadata+0x10,65536);PPC_STORE_U32(metadata+0x14,row);
                std::memcpy(rt.pointer(last,36,true),lastBytes.data(),36);c=saved;
                need(effects.view(id).defaultVectorWords==before&&effects.privateModifiedMask(id)==mask,
                     "Rejected large-submesh input changed FX values/dirty mask");
                same(rt,0x82D64080,composed,"Rejected large-submesh input composed a matrix");
                same(rt,0x82D63070,selected,"Rejected large-submesh input changed grouped scratch");
                activeSubmeshes->unchanged(rt);
                PPCFPSCRRegister::restoreHostCSR(savedCsr);SetLastError(savedError);
            };
            reject([&]{PPC_STORE_U32(metadata+0x10,65537);c.r7.u32=65537;},"Submesh count beyond exact logical owner was accepted",
                   "Original skin submesh rows exceed their observed allocation owner","malformed-owner-count65537");
            reject([&]{PPC_STORE_U32(metadata+0x14,row+36);},"Shifted submesh view overran its logical owner",
                   "Original skin submesh rows exceed their observed allocation owner","malformed-shifted-row-view");
            reject([&]{PPC_STORE_U32(last+4,PPC_LOAD_U32(collection));},"Compiled material index==count was accepted",
                   "Skin submesh material index is out of range","malformed-compiled-material-index");
            reject([&]{PPC_STORE_U32(last,UINT32_MAX);},"Wrapped RwMaterial pointer selector was accepted",
                   "Skin material offset table overflows","malformed-wrapped-material-selector");
            rt.resourceAudit.action("valid-streamed65536-alpha-after-malformed");
            submeshPreflight=true;
        }

        if(pc==0x82701638) {
            if(returned||!activeGroups||activeGroups->groups!=65||!groupPoolPreflightArmed||groupPoolPreflight)return;
            const auto saved=c;const auto row=activeGroups->row(),range=activeGroups->second;
            need(uint32_t(c.lr)==0x82740294&&c.r3.u32==metadata&&c.r4.u32==object&&c.r5.u32==typed&&
                 PPC_LOAD_U32(metadata+0x14)==row,"Original grouped preflight lost its genuine mesh parent");
            const auto before=effects.view(id).defaultVectorWords;const auto mask=effects.privateModifiedMask(id);
            const auto composed=snapshot(rt,0x82D64080,65*64),selected=snapshot(rt,0x82D63070,64*64);
            const auto rowBytes=snapshot(rt,row,36),rangeBytes=snapshot(rt,range,130);
            const auto reject=[&](const std::function<void()>& mutate,const char* message,const char* expected) {
                c=saved;c.lastFunction=pc;mutate();bool rejected=false;
                try{effects.skinMeshOperation(c,base,pc);}catch(const Failure& error){
                    rejected=std::strstr(error.what(),expected)!=nullptr;
                }
                std::memcpy(rt.pointer(row,36,true),rowBytes.data(),36);
                std::memcpy(rt.pointer(range,130,true),rangeBytes.data(),130);c=saved;
                need(rejected,message);
                need(effects.view(id).defaultVectorWords==before&&effects.privateModifiedMask(id)==mask,
                     "Rejected original group preflight changed FX storage or dirty mask");
                same(rt,0x82D64080,composed,"Rejected group preflight composed an original matrix");
                same(rt,0x82D63070,selected,"Rejected group preflight copied an original matrix");
            };
            // The second pool is exactly130 logical bytes:66 pairs need132.
            reject([&]{PPC_STORE_U32(row+28,66);},"Original group table logical overrun was accepted","allocation owner");
            reject([&]{PPC_STORE_U8(range,64);PPC_STORE_U8(range+1,2);},
                   "Original group source beyond composed palette was accepted","composed/shader palette");
            reject([&]{PPC_STORE_U8(range,0);PPC_STORE_U8(range+1,65);},
                   "Original selected shader palette65 was accepted","composed/shader palette");
            groupPoolPreflight=true;return;
        }
        if(pc==0x8270D1C0) {
            need(uint32_t(c.lr)==0x826FF390&&c.r27.u32==geometry,
                 "Alternate original skin stream lookup lost its parent or geometry");
            if(returned) {++auxiliaryLookups;need(c.r3.u32==0,"Original auxiliary geometry index differs");return;}
            need(c.r3.u32==auxiliary&&c.r4.u32==geometry&&
                 PPC_LOAD_U32(auxiliary+16)==1&&PPC_LOAD_U32(auxiliary+20)==auxiliaryGeometries&&
                 PPC_LOAD_U32(auxiliaryGeometries)==geometry,
                 "Original auxiliary owner/list association differs");
            if(!streamRejections) {
                const auto saved=c;
                // Negative scaffolds use this genuine parent/owner frame and
                // the source-pinned argument moves following the lookup.
                // Positive work always continues through the original body.
                PPCContext arguments=saved;arguments.r3.u32=PPC_LOAD_U32(0x82D63028);
                arguments.r4.u32=0;arguments.r5.u32=geometry+0x38;arguments.r6.u32=0;
                arguments.r7.u32=PPC_LOAD_U32(geometry+4);arguments.r8.u32=1;
                const auto before=effects.view(id).defaultVectorWords;const auto mask=effects.privateModifiedMask(id);
                const auto reject=[&](const std::function<void()>& mutate) {
                    c=arguments;mutate();bool failed=false;
                    try{effects.skinSamplerSkip(c,base);}catch(const Failure& error){
                        failed=std::strstr(error.what(),"Unqualified original skin sampler-commit call shape")!=nullptr;
                    }
                    c=saved;need(failed,"Malformed alternate original skin stream was admitted");
                    need(effects.view(id).defaultVectorWords==before&&effects.privateModifiedMask(id)==mask,
                         "Rejected alternate stream changed material values or dirty bits");
                };
                reject([&]{c.r4.u32=1;});reject([&]{c.r6.u32=4;});reject([&]{c.r8.u32=2;});
                reject([&]{c.r5.u32+=4;});reject([&]{c.r27.u32+=4;});
                reject([&]{c.r7.u32=c.r7.u32==48?56:48;});
                const auto stride=PPC_LOAD_U32(geometry+4);
                reject([&]{PPC_STORE_U32(geometry+4,49);c.r7.u32=49;});PPC_STORE_U32(geometry+4,stride);
                streamRejections=true;
            }
            return;
        }
        if(pc==0x826FE7C8){if(returned){++boneCalls;need(c.r3.u32==0x82D64080,"Original bone palette address differs");
            for(uint32_t bone=0;bone<shape.bones;++bone)for(uint32_t lane=0;lane<16;++lane)
                need(PPC_LOAD_U32(c.r3.u32+64*bone+4*lane)==composedWord(bone,lane),"Original skin joint composition/bone remap differs");
        }return;}
        if(pc==0x826FE710){if(returned){++groupCalls;const auto table=PPC_LOAD_U32(c.r30.u32+32),groups=PPC_LOAD_U32(c.r30.u32+28);
            uint32_t count=0;for(uint32_t i=0;i<groups;++i)count+=PPC_LOAD_U8(table+2*i+1);
            const uint32_t expected=groups==1?0x82D64080+64*PPC_LOAD_U8(table):0x82D63070;
            need(c.r3.u32==expected&&PPC_LOAD_U32(c.r1.u32+0x50)==count,"Original group selection address/count differs");
        }return;}
        if(pc==0x826FDBE0) {
            const bool grouped=uint32_t(c.lr)==0x82701820;
            need(grouped||uint32_t(c.lr)==0x827016D8,"Unexpected original bone setter caller");
            if(!returned) {
                const auto saved=c;
                if(grouped&&activeGroups&&activeGroups->groups==65&&(!groupPoolIdentity||!groupPoolEquivalentMutation)) {
                    const auto row=activeGroups->row(),range=activeGroups->second;
                    const auto before=effects.view(id).defaultVectorWords;const auto mask=effects.privateModifiedMask(id);
                    need(c.r30.u32==row&&c.r5.u32==0x82D63070&&c.r6.u32==2,
                         "Original65-group bone setter lost copied two-matrix palette");
                    const auto reject=[&](const char* message,const char* expected) {
                        c.lastFunction=0x826FDBE0;bool failed=false;
                        try{effects.setShadowBones(c,base);}catch(const Failure& error){failed=std::strstr(error.what(),expected)!=nullptr;}
                        c=saved;need(failed,message);
                        need(effects.view(id).defaultVectorWords==before&&effects.privateModifiedMask(id)==mask,
                             "Rejected original65-group mutation changed FX values or dirty bits");
                    };
                    PPC_STORE_U32(row+32,streamedPools[2].second);
                    reject("Same-byte independent original group table identity was accepted","submesh changed");
                    PPC_STORE_U32(row+32,range);groupPoolIdentity=true;
                    // Repeated source joint indices make matrices61/62 equal
                    // to63/64. The native snapshot must still retain the exact
                    // original range bytes, not just the resulting matrices.
                    need(!std::memcmp(rt.pointer(0x82D64080+61*64,128,false),rt.pointer(0x82D63070,128,false),128),
                         "Equivalent authored group mutation does not preserve selected matrix bits");
                    PPC_STORE_U8(range,61);
                    reject("Same-result original65-group range mutation was accepted","ranges changed");
                    PPC_STORE_U8(range,63);groupPoolEquivalentMutation=true;
                }
                if((grouped&&!groupRejections)||(!grouped&&!firstRejections)) {
                    const auto before=effects.view(id).defaultVectorWords;const auto mask=effects.privateModifiedMask(id);
                    const auto rejects=[&](const std::function<void()>& mutate,const char* message) {
                        c=saved;mutate();bool failed=false;
                        // Test the same C++ endpoint from this genuine owner
                        // frame. /EHsc assumes named extern-C functions cannot
                        // throw, so a nested direct__imp__ call is unsuitable
                        // for catching a deliberate rejection.
                        c.lastFunction=0x826FDBE0;
                        try{effects.setShadowBones(c,base);}catch(const Failure&){failed=true;}
                        c=saved;need(c.r5.u64==saved.r5.u64&&c.r6.u64==saved.r6.u64,"Rejected palette context was not restored");
                        need(failed,message);
                        need(effects.view(id).defaultVectorWords==before&&effects.privateModifiedMask(id)==mask,
                             "Rejected original bone setter changed private values or dirty bits");
                    };
                    rejects([&]{c.r5.u32+=64;},"Foreign original bone palette source accepted");
                    rejects([&]{++c.r6.u32;},"Wrong original bone palette count accepted");
                    if(grouped) {
                        const auto table=PPC_LOAD_U32(c.r30.u32+32);const auto original=PPC_LOAD_U8(table+1);
                        rejects([&]{PPC_STORE_U8(table+1,65);},"Oversized original per-submesh matrix range accepted");
                        PPC_STORE_U8(table+1,original);groupRejections=true;
                    } else firstRejections=true;
                }
                if(!grouped)need(c.r28.u32==expectedBit2,"Original first bone call lost retained caller flag");
                else need(c.r28.u32==PPC_LOAD_U32(c.r30.u32+4),"Original second bone call lost its material index");
                if(grouped&&!rangeMutationRejected&&PPC_LOAD_U32(c.r30.u32+28)>1) {
                    const auto table=PPC_LOAD_U32(c.r30.u32+32);const auto original=PPC_LOAD_U8(table);
                    need(original>=2,"Copied palette cannot exercise a valid equivalent-matrix range mutation");
                    const auto before=effects.view(id).defaultVectorWords;const auto mask=effects.privateModifiedMask(id);
                    // Joint remapping alternates two poses, so shifting this
                    // start by two leaves the selected matrix bits identical.
                    // Only the pre-composition authored-byte snapshot detects it.
                    PPC_STORE_U8(table,uint8_t(original-2));c.lastFunction=0x826FDBE0;
                    bool rejected=false;try{effects.setShadowBones(c,base);}catch(const Failure& error){
                        rejected=std::strstr(error.what(),"ranges changed during traversal")!=nullptr;
                    }
                    PPC_STORE_U8(table,original);c=saved;
                    need(rejected&&effects.view(id).defaultVectorWords==before&&effects.privateModifiedMask(id)==mask,
                         "Valid equivalent-matrix range mutation was accepted or changed private values");
                    rangeMutationRejected=true;
                }
            } else {
                ++boneSetCalls;const auto committed=effects.view(id).defaultVectorWords;
                const uint32_t firstLeaf=source==0x82006348?18u:19u,firstWord=source==0x82006348?96u:100u;
                for(uint32_t bone=0;bone<c.r6.u32;++bone)for(uint32_t row=0;row<4;++row)for(uint32_t lane=0;lane<4;++lane)
                    need(committed[firstWord+16*bone+4*row+lane]==PPC_LOAD_U32(c.r5.u32+64*bone+16*lane+4*row),
                         "Original skin selected matrix transpose differs");
                const auto mask=effects.privateModifiedMask(id);
                for(uint32_t bone=0;bone<c.r6.u32;++bone)need(mask[(firstLeaf+bone)/8]&(0x80>>((firstLeaf+bone)&7)),
                    "Original selected bone upload did not dirty its authored leaf");
            }
            return;
        }
        if(returned)return;
        if(pc==0x82740680){++dispatcherCalls;need(uint32_t(c.lr)==0x8273B4E0&&c.r3.u32==packet,"Original crashing dispatcher caller changed");}
        else if(pc==0x827400F8){++fallbackCalls;need(uint32_t(c.lr)==0x82740B28&&c.r3.u32==packet&&
            c.r4.u32==expectedAlpha&&c.r5.u32==expectedBit1&&c.r6.u32==expectedBit2,"Original skin fallback flags or caller differ");}
        else if(pc==0x8270BBC0)++vectorCalls;else if(pc==0x8270BE50)++textureCalls;
    });
    PPC_STORE_U32(area+0x80,0x00FF00FF);need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original main clear failed");
    const auto clearColor=driver.readbackColor(bound.colorIdentity);const auto inputs=materialInputs();
    const auto call=[&](uint32_t alpha,uint32_t bit1,uint32_t bit2,bool reset=true) {
        expectedAlpha=alpha;expectedBit1=bit1;expectedBit2=bit2;layout(rt,shape,activeGroups,activeSubmeshes);
        const auto mesh=snapshot(rt,geometry,0x700),jointsBefore=snapshot(rt,bindMatrices,0x400);
        // Only original metadata and the packet's alpha-eligibility byte vary.
        // The real dispatcher, rather than the fixture, supplies r4/r5.
        PPC_STORE_U32(metadata+8,1|(bit1<<1)|(bit2<<2));PPC_STORE_U8(packet+0xC,uint8_t(alpha));
        if(reset)resetPassState();
        else {
            const auto& state=driver.effectiveState();
            using S=Simpsons::Graphics::ScalarState;
            need(!alpha&&state.scalar(S::BlendEnable)==1&&state.effectiveBlend(0)==0x07060706&&
                 state.scalar(S::ExpandedBlend0)==0,
                 "Following original opaque draw lost retained alpha blend state");
        }
        need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original variant clear failed");
        EngineCpuCalls draw(entry,base);const auto saved=fullAbi(draw.registers());draw.invoke(0x8273B4D0,packet);
        need(fullAbi(draw.registers())==saved,"Whole original skin wrapper changed nonvolatile ABI");
        need(effects.privateModifiedMask(id)==std::array<uint8_t,128>{},"Skin dirty bank remains after commit");
        need(!PPC_LOAD_U32(0x82D0CAF8),"Skin draw published a console device");
        need(PPC_LOAD_U32(manager+8)==id&&PPC_LOAD_U32(manager+0xC)==(alpha?0x0007FFFCu:0x0003FFFCu),
             "Original skin family selected the wrong owner or technique");
        if(!reset) {
            const auto& state=driver.effectiveState();
            using S=Simpsons::Graphics::ScalarState;
            need(state.scalar(S::BlendEnable)==1&&state.effectiveBlend(0)==0x07060706&&
                 state.scalar(S::ExpandedBlend0)==0,
                 "Original opaque dispatcher changed its inherited blend equation");
        }
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
             "Original skin draw produced no center pixel or escaped geometry bounds");
        same(rt,geometry,mesh,"Skin dispatcher changed immutable input geometry");
        same(rt,bindMatrices,jointsBefore,"Skin draw changed input skeleton, bone map or range tables");
        ++parentCalls;const auto selectedDraws=shape.submeshes-shape.skipped;drawCalls+=selectedDraws;
        expectedBoneSets+=shape.bones>64?selectedDraws:1;expectedGroupCalls+=shape.bones>64?selectedDraws:0;return pixels;
    };
    for(uint32_t bit2=0;bit2<2;++bit2) {
        stage="whole original family all three Boolean fallback flags";const auto alpha0=call(1,0,bit2),alpha1=call(1,1,bit2);
        need(alpha0==alpha1,"Unused incoming r5 changed original skin alpha pixels");
        const auto opaque1=call(0,1,bit2),opaque0=call(0,0,bit2);
        need(opaque1==opaque0,"Unused incoming r5 changed original skin opaque pixels");
    }
    if(inheritedBlend) {
        shape={"original alpha cleanup followed by opaque dispatch without blend reset",2,1,0};stage=shape.name;
        const auto replacement=call(0,0,1);
        (void)call(1,0,1);
        const auto retained0=call(0,0,1,false),retained1=call(0,1,1,false);
        need(retained0==retained1,"Unused incoming r5 changed inherited skin blend pixels");
        need(retained0!=replacement,"Following opaque skin draw ignored original retained blending");
    }
    shape={"exact house exit one-bone caller flag",1,1,0};stage=shape.name;(void)call(0,1,1);
    shape={"whole palette64 boundary",64,1,0};stage=shape.name;(void)call(1,0,1);
    shape={"three original submeshes first skipped",2,3,1};stage=shape.name;(void)call(0,1,1);(void)call(1,0,1);
    shape={"65-entry composed palette contiguous alias and copied ranges",65,3,1};stage=shape.name;(void)call(0,1,1);(void)call(1,0,1);
    shape={"255-entry bounded composed palette contiguous alias and copied ranges",255,3,1};stage=shape.name;(void)call(0,1,1);(void)call(1,0,1);
    {
        // Real auxiliary plugin data takes 826FF348 -> 826FF388, executes
        // 8270D1C0 and binds the original geometry descriptor at 826FF3B4.
        const auto auxiliaryOffset=PPC_LOAD_U32(0x82D6CAB0),lookupOffset=PPC_LOAD_U32(0x82CED798);
        need(auxiliaryOffset<0x1000&&lookupOffset<0x1000,
             "Original auxiliary plugin lookup lies outside the owned fixture");
        need(!PPC_LOAD_U32(object+lookupOffset+16),"Fixture has an authored morph-stream list");
        Restore auxiliarySlot(rt,data+auxiliaryOffset,4);
        PPC_STORE_U32(auxiliary+16,1);PPC_STORE_U32(auxiliary+20,auxiliaryGeometries);
        PPC_STORE_U32(auxiliaryGeometries,geometry);PPC_STORE_U32(data+auxiliaryOffset,auxiliary);
        shape={"alternate auxiliary skin stream for both opaque and alpha layouts",2,1,0};stage=shape.name;
        const auto opaque0=call(0,0,1),opaque1=call(0,1,1);need(opaque0==opaque1,"Auxiliary opaque stream changed Boolean pixel equivalence");
        const auto alpha0=call(1,0,1),alpha1=call(1,1,1);need(alpha0==alpha1,"Auxiliary alpha stream changed Boolean pixel equivalence");
        need(auxiliaryLookups==4&&streamRejections,"Alternate original stream branch or strict negatives were not exercised");
    }
    if(groups65Layout) {
        shape={"original one-range versus65-range two-matrix owner",65,1,0};
        stage="whole original one-group(63,2) baseline through owned relocated table";
        activeGroups=&streamedPools[0];const auto opaqueAlias=call(0,1,1),alphaAlias=call(1,0,1);
        stage="whole original65-group(63,2)+(65,0)*64 palette opaque and alpha";
        activeGroups=&streamedPools[1];const auto opaqueCopied=call(0,1,1);
        // Establish the positive65-range path first. A still-restrictive
        // production guard must fail that valid input, not a prior negative.
        groupPoolPreflightArmed=true;const auto alphaCopied=call(1,0,1);
        need(opaqueAlias==opaqueCopied&&alphaAlias==alphaCopied,
             "Original65-group copied palette changed equivalent original one-group pixels");
        need(groupPoolPreflight&&groupPoolIdentity&&groupPoolEquivalentMutation,
             "Original group logical-range and byte/identity negatives did not execute");
        same(rt,streamedPools[1].second,std::vector<uint8_t>(streamedPools[1].originalWire.end()-130,streamedPools[1].originalWire.end()),
             "Whole original65-group use changed its authored range bytes");
        std::printf("AUDIT_GROUP65_USE source=%08X composed=65 groups=1/65 selected=2 opaque_pixels=equivalent alpha_pixels=equivalent composition=original group_helper=original upload=original callbacks=original ABI=passed malformed=logical_extent_source_selected_total_table_identity_same_result_range_bytes\n",source);
    }

    if(submeshes65536Layout) {
        stage="whole original streamed one-row skin baseline";
        shape={"one-row original streamed owner",2,1,0};activeSubmeshes=&submeshPools[0];
        rt.resourceAudit.action("valid-streamed-one-row");
        const auto opaqueOne=call(0,1,1),alphaOne=call(1,0,1);
        std::printf("AUDIT_SUBMESH_FRONTIER source=%08X baseline_count=1 baseline_opaque=passed baseline_alpha=passed next_count=65536 first_owner=original_reader\n",source);
        stage="whole original65536-row skin owner with65535 valid material skips";
        shape={"65536-row original streamed owner",2,65536,65535};activeSubmeshes=&submeshPools[1];
        rt.resourceAudit.action("valid-streamed65536-before-malformed");
        const auto opaqueMany=call(0,1,1);
        // Establish the valid frontier before malformed controls. The old
        // native count cap must fail this valid whole-original call first.
        submeshPreflightArmed=true;const auto alphaMany=call(1,0,1);
        need(opaqueMany==opaqueOne&&alphaMany==alphaOne,
             "Original65536-row skips changed one-row opaque/alpha pixels");
        need(submeshPreflight,"Original65536-row malformed owner/index cases did not execute");
        submeshPools[0].unchanged(rt);submeshPools[1].unchanged(rt);
        activeSubmeshes=nullptr;shape={"later valid ordinary two-bone original skin",2,1,0};
        rt.resourceAudit.action("later-valid-ordinary-skin");
        (void)call(0,1,1);(void)call(1,0,1);
        const auto auditRows=submeshAuditRows();bool oneSeen=false,manySeen=false,alphaAfter=false,laterSeen=false;
        for(const auto& auditRow:auditRows) {
            if(auditString(auditRow,"event")!="encounter")continue;
            const auto action=auditString(auditRow,"last_action");
            if(action=="valid-streamed-one-row") {verifySubmeshAudit(auditRow,submeshPools[0],1,submeshPools[0].row(),typed,action.c_str(),"encounter");oneSeen=true;}
            if(action=="valid-streamed65536-before-malformed") {verifySubmeshAudit(auditRow,submeshPools[1],65536,submeshPools[1].row(),typed,action.c_str(),"encounter");manySeen=true;}
            if(action=="valid-streamed65536-alpha-after-malformed") {verifySubmeshAudit(auditRow,submeshPools[1],65536,submeshPools[1].row(),typed,action.c_str(),"encounter");alphaAfter=true;}
            if(action=="later-valid-ordinary-skin")laterSeen=true;
        }
        need(oneSeen&&manySeen&&alphaAfter&&laterSeen,"Valid submesh counts or later continuation disappeared from the audit");
        std::printf("AUDIT_SUBMESH65536_ENTRY source=%08X caller=82740294 counts=1_65536 malformed=4 exact_frontiers=passed observation_before_failure=passed ppc=preserved csr=preserved last_error=preserved logical_owner=passed instance_generation=passed stable_group=instance_excluded later_valid=passed receipts=%s\n",source,submeshAuditPath.string().c_str());
        std::printf("AUDIT_SUBMESH65536_USE source=%08X count=65536 row_bytes=00240000 bones=2 skipped=65535 selected_draws=1 opaque_pixels=equivalent alpha_pixels=equivalent composition=original grouping=unused callbacks=original later_valid=passed malformed=logical_owner_shifted_view_collection_index_wrapped_selector ABI=passed\n",source);
    }
    const auto textureCount=uint32_t(std::count_if(inputs.begin(),inputs.end(),[](const auto& input){return input.texture!=noTexture;}));
    const auto vectorCount=uint32_t(inputs.size())-textureCount;
    need(dispatcherCalls==parentCalls&&fallbackCalls==parentCalls&&vectorCalls==drawCalls*vectorCount&&textureCalls==drawCalls*textureCount&&
         boneCalls==parentCalls&&boneSetCalls==expectedBoneSets&&groupCalls==expectedGroupCalls&&firstRejections&&groupRejections&&rangeMutationRejected,
         "Original skin family material traversal differs");
    screenReplacementRegression(rt,cpu,camera,typed,wrapper,area+0xB0,[&]{(void)call(0,1,1);});
    same(rt,source,sourceBytes,"Skin regression changed original serialized shader record");
    cpu.invoke(0x826B4B18,wrapper);need(cpu.invoke(0x823F1A08,camera)==camera,"Original main camera end failed");
    if(paddedLayout||inheritedBlend||groups65Layout||submeshes65536Layout) {
        cpu.invoke(0x82700A78);
        rejects([&]{rt.engineAudio->allocationGeneration(ownedDeclaration,0x50);},"Original padded skin primary declaration was not retired");
        rejects([&]{rt.engineAudio->allocationGeneration(ownedAlternate,0x50);},"Original padded skin alternate declaration was not retired");
        cleanup.release(rt,cpu,manager);
        same(rt,source,sourceBytes,"Original padded skin cleanup changed serialized source");
        if(paddedLayout)std::printf("AUDIT_GEOMETRY_LIFECYCLE source=%08X stride=80 delta=16 create=passed use=passed declaration_release=passed fx_release=passed cpu_cache_release=passed malformed=passed malformed_scope=original_bones_streams_screen_cache stale_use=passed backend_mesh_cache=owner_resident full_gpu_retirement=unproven\n",source);
        if(inheritedBlend)std::printf("AUDIT_SKIN_BLEND_LIFECYCLE source=%08X blend=07060706 enable=1 expanded=0 original_alpha_to_opaque=passed create=passed use=passed declaration_release=passed fx_release=passed cpu_cache_release=passed stale_use=passed backend_mesh_cache=owner_resident full_gpu_retirement=unproven\n",source);

        if(submeshes65536Layout) {
            stage="paired original submesh pool retirement after camera/declaration/FX/cache cleanup";
            for(const auto& pool:submeshPools)pool.release(rt,cpu);
            cpu.invoke(0x8282F4D0,previousPoolManager,previousPoolArgument);
            need(PPC_LOAD_U32(0x82E06F74)==previousPoolManager&&PPC_LOAD_U32(0x82E06F78)==previousPoolArgument,
                 "Original submesh allocator publication was not restored by its original setter");
            std::printf("AUDIT_SUBMESH65536_LIFECYCLE source=%08X count=65536 bones=2 row_bytes=00240000 create=original_first_pool_reader relocation=82831280 use=passed opaque_alpha_pixels=equivalent camera_end=passed declaration_release=passed fx_release=passed cpu_cache_release=passed pool_release=8282F878 stream_source_release=8269BF10 stale_allocation=passed malformed=passed later_valid=passed backend_mesh_cache=owner_resident full_gpu_retirement=unproven\n",source);
        }
        if(groups65Layout) {
            stage="paired original8282F878 group pool retirement after camera and FX owners";
            for(const auto& pool:streamedPools)pool.release(rt,cpu);
            stage="original group reader fresh generation after normal pool retirement";
            const auto oldSecond=streamedPools[1].second;const auto oldGeneration=streamedPools[1].secondGeneration;
            OriginalGroupPool replacement;replacement.read(rt,cpu,area+0xD300,area+0xD040,65);
            need(replacement.secondGeneration!=oldGeneration,
                 "Original group reader reused retired allocation generation");
            same(rt,replacement.second,std::vector<uint8_t>(streamedPools[1].originalWire.end()-130,streamedPools[1].originalWire.end()),
                 "Original group reader reallocation changed identical serialized ranges");
            const bool addressReused=replacement.second==oldSecond;
            replacement.release(rt,cpu);
            cpu.invoke(0x8282F4D0,previousPoolManager,previousPoolArgument);
            need(PPC_LOAD_U32(0x82E06F74)==previousPoolManager&&PPC_LOAD_U32(0x82E06F78)==previousPoolArgument,
                 "Original group allocator publication was not restored through its original setter");
            std::printf("AUDIT_GROUP65_LIFECYCLE source=%08X groups=65 selected=2 create=original_two_pool_reader use=passed camera_end=passed declaration_release=passed fx_release=passed cpu_cache_release=passed pool_release=8282F878 stale_allocation=passed new_generation=passed address_reused=%u reuse_scope=allocator_only live_generation_rejection=unexecuted backend_mesh_cache=owner_resident full_gpu_retirement=unproven\n",source,uint32_t(addressReused));
        }
    }
}
}
PPC_FUNC(sub_82740680){forward(0x82740680,ctx,base,__imp__sub_82740680);}
PPC_FUNC(sub_827400F8){forward(0x827400F8,ctx,base,__imp__sub_827400F8);}
PPC_FUNC(sub_8270BBC0){forward(0x8270BBC0,ctx,base,__imp__sub_8270BBC0);}
PPC_FUNC(sub_8270BE50){forward(0x8270BE50,ctx,base,__imp__sub_8270BE50);}
PPC_FUNC(sub_826FE7C8){forward(0x826FE7C8,ctx,base,__imp__sub_826FE7C8);}
PPC_FUNC(sub_826FE710){forward(0x826FE710,ctx,base,__imp__sub_826FE710);}
PPC_FUNC(sub_826FDBE0){forward(0x826FDBE0,ctx,base,__imp__sub_826FDBE0);}
PPC_FUNC(sub_8270D1C0){forward(0x8270D1C0,ctx,base,__imp__sub_8270D1C0);}
PPC_FUNC(sub_82701638){forward(0x82701638,ctx,base,__imp__sub_82701638);}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc>=3&&argc<=5,"Original image, loc_split4 ITXD dictionary, optional family and padded/inherited/groups65/submeshes65536 case required");
        if(argc>=4) {
            const auto choice=std::find_if(families.begin(),families.end(),[&](const auto& candidate){return std::string_view(argv[3])==candidate.name;});
            need(choice!=families.end(),"Unknown original skin family");family=&*choice;source=family->source;body=source+12;
        }
        if(argc==5){const std::string_view choice(argv[4]);need(choice=="padded"||choice=="inherited"||choice=="groups65"||choice=="submeshes65536","Unknown original skin case");
            paddedLayout=choice=="padded";inheritedBlend=choice=="inherited";groups65Layout=choice=="groups65";submeshes65536Layout=choice=="submeshes65536";}
        Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        if(submeshes65536Layout) {
            submeshAuditPath=std::filesystem::current_path()/("skin-submeshes65536-"+std::string(family->name)+"-"+
                std::to_string(GetCurrentProcessId())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
            const auto auditFile=CreateFileW(submeshAuditPath.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            need(auditFile!=INVALID_HANDLE_VALUE,"Cannot reserve unique submesh audit receipt");need(CloseHandle(auditFile)!=0,"Cannot close submesh audit reservation");
            rt.resourceAudit.configure(submeshAuditPath);rt.resourceAudit.mission("synthetic-submesh65536-fixture");
            rt.resourceAudit.action("whole-original-skin-fixture-setup");
        }
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original startup boundary missing");run(rt,entry,argv[2]);
        std::printf("PASS original skin %s: %zu checks; real catalogs/textures/cameras/joints, all three Boolean fallback flags,1/2/64/65/255 composed bones, skipped/nonzero material submeshes, actual contiguous/copied group palettes, strict source/count/range rejections, pixels/callbacks/nonvolatile ABI\n",family->name,checks);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original skin %s: %zu checks stage=%s: %s\n",family->name,checks,stage,error.what());return 1;}
}
