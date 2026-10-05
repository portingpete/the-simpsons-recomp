// Whole original skin traversal, genuine joint owners and material callbacks.
// Source metadata pins each family; no draw, bone or material callback is faked.
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
extern "C" void __imp__sub_826FE7C8(PPCContext&,uint8_t*);
extern "C" void __imp__sub_826FE710(PPCContext&,uint8_t*);
extern "C" void __imp__sub_826FDBE0(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8270D1C0(PPCContext&,uint8_t*);

namespace {
constexpr uint32_t secondTable=0x82CD1448,noTexture=UINT32_MAX;
struct Family {
    const char* name;uint32_t source,row,bytes,descriptors,leaves,words,opaqueContext,alphaContext,alphaVertex,alphaPixel;
};
constexpr std::array<Family,1> families={{{"dual-uv",0x8204A058,17,0x77B0,99,94,1204,0x6570,0x6D20,0x8204CE90,0x8204E75C}}};
const Family* family=&families[0];
uint32_t source=family->source,body=source+12;
struct MaterialInput {uint32_t handle,word,texture;};
std::vector<MaterialInput> materialInputs() {
    return {{0x0048001C,80,noTexture},{0x00500020,88,noTexture},
        {0x005C0026,100,noTexture},{0x00600028,104,noTexture},{0x0064002A,108,noTexture},
        {0x0068002C,112,noTexture},{0x006C002E,116,noTexture},{0x00700030,120,noTexture},
        {0x018000B6,1168,0},{0x018400B8,1184,1},{0x018800BA,1200,noTexture}};

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
    need(PPC_LOAD_U32(secondTable+family->row*16)==source&&PPC_LOAD_U32(secondTable+family->row*16+12)==0x823CA960,
         "Original animated dual-UV skin catalog association changed");
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
    // Independent original masks/maps pin every active AND inactive leaf.
    // The array parent is distinct from its first numeric matrix child.
    need(PPC_LOAD_U32(descriptors+8*30)==0x00200102&&PPC_LOAD_U32(descriptors+8*30+4)==0x03000041,
         "Original dual UV64-matrix array framing changed");
    constexpr std::array<std::array<uint32_t,4>,16> common{{
        {2,0x000C0004,0x80C,0},{14,0x0048001C,0,49},{15,0x004C001E,0,40},
        {17,0x00540022,39,0},{18,0x00580024,38,0},{19,0x005C0026,47,0},
        {20,0x00600028,46,0},{21,0x0064002A,45,0},{22,0x0068002C,44,0},
        {23,0x006C002E,43,0},{24,0x00700030,0,42},{25,0x00740032,22,0},
        {91,0x018000B6,0,0},{92,0x018400B8,0,0x400000},{93,0x018800BA,0,48},
        {UINT32_MAX,0,0,0}}};
    constexpr std::array<uint32_t,13> semantics{{0x0010000A,0x0000300B,0x0000500C,0x0001500D,
        0x0000200E,0x0000100F,0x0000A010,0x00010011,0x00020012,0x00030013,0x00040014,0x00050015,0x00260016}};
    for(uint32_t pass=0;pass<2;++pass) {
        const auto context=pass?0x6D20u:0x6570u,map=PPC_LOAD_U32(body+context+0x40),shared=PPC_LOAD_U32(body+context+0x44);
        need(map==(pass?0x6E40u:0x6690u)&&shared==(pass?0x7420u:0x6C70u),"Original dual UV selected maps changed");
        const auto vertex=pass?0x8204CE90u:0x8204BA4Cu;
        for(uint32_t i=0;i<semantics.size();++i)need(PPC_LOAD_U32(vertex+3920+4*i)==semantics[i],"Original dual UV13 consumed input associations differ");
        for(uint32_t leaf=0;leaf<94;++leaf) {
            uint32_t usage=0;
            for(uint32_t cat=0;cat<8;++cat) {
                const auto pointer=body+PPC_LOAD_U32(body+context+4*cat)+8*(leaf/64);
                const auto bits=uint64_t(PPC_LOAD_U32(pointer))<<32|PPC_LOAD_U32(pointer+4);
                usage|=uint32_t((bits>>(63-leaf%64))&1)<<cat;
            }
            const uint32_t expectedUsage=leaf==2||leaf==17||leaf==18||(leaf>=19&&leaf<=23)||leaf==25||(leaf>=26&&leaf<=89)?1u:
                leaf==14||leaf==15||leaf==24||leaf==93?2u:leaf==91||leaf==92?0x80u:0u;
            need(usage==expectedUsage,"Original dual UV private usage differs");
            std::array<uint32_t,4> row{};
            if(leaf>=26&&leaf<=89)row={0x007C0034+0x40002*(leaf-26),0x834+3*(leaf-26),0,0};
            else for(const auto& value:common)if(value[0]==leaf)row={value[1],value[2],value[3],0};
            for(uint32_t i=0;i<4;++i)need(PPC_LOAD_U32(body+map+16*leaf+4*i)==row[i],"Original dual UV private register/stage row differs");
        }
        for(uint32_t leaf=0;leaf<11;++leaf) {
            uint32_t usage=0;
            for(uint32_t cat=0;cat<8;++cat) {
                const auto pointer=body+PPC_LOAD_U32(body+context+32+4*cat);
                const auto bits=uint64_t(PPC_LOAD_U32(pointer))<<32|PPC_LOAD_U32(pointer+4);
                usage|=uint32_t((bits>>(63-leaf))&1)<<cat;
            }
            need(usage==(leaf==0?1u:leaf==1?2u:0u),"Original dual UV shared usage includes an unconsumed shadow resource");
            const std::array<uint32_t,4> row=leaf==0?std::array<uint32_t,4>{0x00040001,0xC00,0,0}:
                leaf==1?std::array<uint32_t,4>{0x00080003,0,4,0}:std::array<uint32_t,4>{};
            for(uint32_t i=0;i<4;++i)need(PPC_LOAD_U32(body+shared+16*leaf+4*i)==row[i],"Original dual UV shared register row differs");
        }
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
    auto* base=rt.base;const bool dual=source==0x8204A058;const uint32_t stride=dual?56:48;
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
            // Fixture vectors preserve authored UV scale and disable optional motion/rim;
            // real callbacks still carry every lane through original storage.
            if(inputs[i].handle!=0x00600028)for(uint32_t lane=0;lane<4;++lane)PPC_STORE_U32(value+4*lane,0);
            if(inputs[i].handle==0x0048001C)PPC_STORE_U32(value+4,0x3F800000);
        }
    }
    PPC_STORE_U32(material+0xC,uint32_t(inputs.size())<<10);PPC_STORE_U32(material+0x14,materialRows);
}
struct Scenario {const char* name;uint32_t bones,submeshes,skipped;};
void layout(Runtime& rt,const Scenario& shape) {
    auto* base=rt.base;need(shape.bones>=1&&shape.bones<=255&&shape.submeshes>=1&&shape.submeshes<=3&&
        shape.skipped<shape.submeshes,"Original skin scenario extent differs");
    PPC_STORE_U32(metadata+0x24,shape.bones);PPC_STORE_U32(metadata+0x10,shape.submeshes);
    // The original palette may contain repeated authored joint indices. Two
    // genuine joint/bind owners therefore compose every requested entry,
    // including the255-entry bounded scratch case, without fake matrices.
    for(uint32_t bone=0;bone<shape.bones;++bone)PPC_STORE_U8(boneMap+bone,uint8_t(1-(bone&1)));
    PPC_STORE_U32(collection,2);PPC_STORE_U32(collection+12,material);PPC_STORE_U32(collection+16,material);
    PPC_STORE_U32(offsets+28,0x70);PPC_STORE_U16(headers+0x70,0x20);
    for(uint32_t i=0;i<shape.submeshes;++i) {
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
    const uint32_t stride=PPC_LOAD_U32(geometry+4),influence=source==0x8204A058?32u:24u;
    for(uint32_t i=0;i<4;++i)PPC_STORE_U32(vertices+stride*i+influence,shape.bones==1?0u:uint32_t(i>=2));
}
uint32_t composedWord(uint32_t bone,uint32_t lane) {
    return lane%5==0?0x3F800000u:lane==12?std::bit_cast<uint32_t>((bone&1)?-.125f:.125f):0u;
}
void run(Runtime& rt,const PPCContext& entry,const char* dictionaryPath) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();EngineCpuCalls cpu(entry,base);
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
    const auto bound=driver.cameraBinding();uint32_t dispatcherCalls=0,fallbackCalls=0,vectorCalls=0,textureCalls=0,boneCalls=0,
        boneSetCalls=0,groupCalls=0,expectedAlpha=0,expectedBit1=0,expectedBit2=0,parentCalls=0,drawCalls=0,expectedBoneSets=0,expectedGroupCalls=0;
    Scenario shape{"two joint whole palette",2,1,0};bool firstRejections=false,groupRejections=false,rangeMutationRejected=false;
    uint32_t auxiliaryLookups=0;bool streamRejections=false;
    Observation observation([&](uint32_t pc,PPCContext& c,bool returned) {
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
                reject([&]{c.r7.u32+=4;});
                const auto stride=PPC_LOAD_U32(geometry+4);
                for(uint32_t malformed:{0u,65u,1024u}) {
                    reject([&]{PPC_STORE_U32(geometry+4,malformed);c.r7.u32=malformed;});
                    PPC_STORE_U32(geometry+4,stride);
                }
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
                const uint32_t firstLeaf=26u,firstWord=128u;
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
    const auto call=[&](uint32_t alpha,uint32_t bit1,uint32_t bit2) {
        expectedAlpha=alpha;expectedBit1=bit1;expectedBit2=bit2;layout(rt,shape);
        const auto mesh=snapshot(rt,geometry,0x700),jointsBefore=snapshot(rt,bindMatrices,0x400);
        // Only original metadata and the packet's alpha-eligibility byte vary.
        // The real dispatcher, rather than the fixture, supplies r4/r5.
        PPC_STORE_U32(metadata+8,1|(bit1<<1)|(bit2<<2));PPC_STORE_U8(packet+0xC,uint8_t(alpha));
        resetPassState();
        need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original variant clear failed");
        EngineCpuCalls draw(entry,base);const auto saved=fullAbi(draw.registers());draw.invoke(0x8273B4D0,packet);
        need(fullAbi(draw.registers())==saved,"Whole original skin wrapper changed nonvolatile ABI");
        need(effects.privateModifiedMask(id)==std::array<uint8_t,128>{},"Skin dirty bank remains after commit");
        need(!PPC_LOAD_U32(0x82D0CAF8),"Skin draw published a console device");
        need(PPC_LOAD_U32(manager+8)==id&&PPC_LOAD_U32(manager+0xC)==(alpha?0x0007FFFCu:0x0003FFFCu),
             "Original skin family selected the wrong owner or technique");
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
        {
            // The formerly rejected stride64 is a valid original SDK fetch
            // stride. Repack all owned records and execute the real auxiliary
            // producer for both complete passes, preserving their pixels.
            Restore extent(rt,geometry,8),payload(rt,vertices,4*64);
            const auto stride=PPC_LOAD_U32(geometry+4);const auto original=snapshot(rt,vertices,4*stride);
            need(stride<64,"Auxiliary padding fixture has no additional space");
            std::memset(rt.pointer(vertices,4*64,true),0xFF,4*64);
            for(uint32_t i=0;i<4;++i)
                std::memcpy(rt.pointer(vertices+64*i,stride,true),original.data()+stride*i,stride);
            PPC_STORE_U32(geometry,4*64);PPC_STORE_U32(geometry+4,64);
            stage="original auxiliary stream with valid padded stride64";
            need(call(0,0,1)==opaque0&&call(0,1,1)==opaque1,
                 "Padded original auxiliary opaque stream changed pixels");
            need(call(1,0,1)==alpha0&&call(1,1,1)==alpha1,
                 "Padded original auxiliary alpha stream changed pixels");
        }
        need(auxiliaryLookups==8&&streamRejections,"Alternate original stream branch or strict negatives were not exercised");
    }
    const auto textureCount=uint32_t(std::count_if(inputs.begin(),inputs.end(),[](const auto& input){return input.texture!=noTexture;}));
    const auto vectorCount=uint32_t(inputs.size())-textureCount;
    need(dispatcherCalls==parentCalls&&fallbackCalls==parentCalls&&vectorCalls==drawCalls*vectorCount&&textureCalls==drawCalls*textureCount&&
         boneCalls==parentCalls&&boneSetCalls==expectedBoneSets&&groupCalls==expectedGroupCalls&&firstRejections&&groupRejections&&rangeMutationRejected,
         "Original skin family material traversal differs");
    screenReplacementRegression(rt,cpu,camera,typed,wrapper,area+0xB0,[&]{(void)call(0,1,1);});
    same(rt,source,sourceBytes,"Skin regression changed original serialized shader record");
    cpu.invoke(0x826B4B18,wrapper);need(cpu.invoke(0x823F1A08,camera)==camera,"Original main camera end failed");
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
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==3||argc==4,"Original image, loc_split4 ITXD dictionary and optional family required");
        if(argc==4) {
            const auto choice=std::find_if(families.begin(),families.end(),[&](const auto& candidate){return std::string_view(argv[3])==candidate.name;});
            need(choice!=families.end(),"Unknown original skin family");family=&*choice;source=family->source;body=source+12;
        }
        Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original startup boundary missing");run(rt,entry,argv[2]);
        std::printf("PASS original skin %s: %zu checks; real catalogs/textures/cameras/joints, all three Boolean fallback flags,1/2/64/65/255 composed bones, skipped/nonzero material submeshes, actual contiguous/copied group palettes, strict source/count/range rejections, pixels/callbacks/nonvolatile ABI\n",family->name,checks);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original skin %s: %zu checks stage=%s: %s\n",family->name,checks,stage,error.what());return 1;}
}
