// The crashing animated dual-texture material, through original CPU ownership,
// dispatcher, material callbacks, recording/replay and alpha fallback.
#include "effect_catalog_lifecycle_helpers.h"
#include "runtime/engine_itxd_textures.h"
#include "runtime/engine_recording.h"
#include "renderer/engine_state.h"
#include <bit>
#include <fstream>
#include <functional>

extern "C" void __imp__sub_82740680(PPCContext&,uint8_t*);
extern "C" void __imp__sub_827400F8(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8270BBC0(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8270BE50(PPCContext&,uint8_t*);

namespace {
constexpr uint32_t secondTable=0x82CD1448,source=0x820465E8,body=source+12;
constexpr uint32_t area=0x60000,packet=area,metadata=0x60100,object=0x61000,data=0x63000,
    offsets=0x64000,geometry=0x65000,elements=0x65100,vertices=0x65200,indices=0x65400,
    submeshes=0x65500,declCache=0x65600,headers=0x65700,objectFrame=0x65800,
    collection=0x66000,material=0x66100,materialRows=0x66200,values=0x66500,
    property=0x67000,dictionary=0x80000;
std::function<void(uint32_t,PPCContext&,bool)> observer;
void forward(uint32_t pc,PPCContext& c,uint8_t* base,void(*original)(PPCContext&,uint8_t*)) {
    if(observer)observer(pc,c,false);original(c,base);if(observer)observer(pc,c,true);
}
struct Observation {
    explicit Observation(decltype(observer) value){need(!observer,"Nested UV observer");observer=std::move(value);}
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
    need(bytes.size()==size_t(1280)*720*4,"UV color extent differs");
    uint32_t result{};std::memcpy(&result,bytes.data()+4*(1280*y+x),4);return result;
}
std::array<uint32_t,2> loadTextures(Runtime& rt,const PPCContext& entry,const char* path) {
    auto* base=rt.base;EngineCpuCalls cpu(entry,base);
    std::ifstream input(path,std::ios::binary|std::ios::ate);need(bool(input),"Original ITXD dictionary required");
    const auto length=input.tellg();need(length>0&&length<=0x200000,"ITXD fixture extent differs");
    const auto bytes=uint32_t(length);rt.map(dictionary,bytes,true,"original UV texture dictionary");
    input.seekg(0);input.read(reinterpret_cast<char*>(rt.pointer(dictionary,bytes,true)),bytes);
    need(bool(input),"Original dictionary read failed");
    const auto original=snapshot(rt,dictionary,bytes);
    PPC_STORE_U32(area+0x80,dictionary);PPC_STORE_U32(area+0x84,bytes);
    const auto stream=cpu.invoke(0x823F9598,3,1,area+0x80);need(stream,"Original memory stream construction failed");
    // Retain the complete original parent loader. Its child frame verifies the
    // named caller LR and original stack-resident request at childSP+D0.
    EngineCpuCalls load(entry,base);load.registers().lr=0x8271191C;
    const auto request=load.registers().r1.u32+0x60;
    std::memset(rt.pointer(request,0x18,true),0,0x18);
    PPC_STORE_U32(area+0x90,0x55565458);PPC_STORE_U32(request+4,area+0x90);
    PPC_STORE_U32(request+0x10,stream);PPC_STORE_U32(request+0x14,bytes);
    const auto saved=fullAbi(load.registers());const auto group=load.invoke(0x826F26B0,0,request);
    need(group&&fullAbi(load.registers())==saved,"Original dictionary parent loader lost return or ABI");
    const auto plugin=PPC_LOAD_U32(0x82CF0600);
    const uint32_t count=PPC_LOAD_U16(dictionary+4)+PPC_LOAD_U16(dictionary+6);
    const auto sentinel=dictionary+8*(count+1)+8;
    std::array<uint32_t,2> result{};uint32_t visited=0;
    for(uint32_t link=PPC_LOAD_U32(sentinel);dictionary+link!=sentinel;link=PPC_LOAD_U32(dictionary+link)) {
        need(link&&++visited<256,"Original dictionary link cycle");const auto texture=dictionary+link-8;
        const auto name=stringAt(rt,texture+0x10);
        if(name=="simpsons_palette"||name=="fire64bw3") {
            const auto copied=cpu.invoke(0x826F8520,PPC_LOAD_U32(texture+plugin+4));
            need(copied&&rt.engineDriver->itxdTextures().ownsRaster(copied+0x78),"Original named texture was not copied/published");
            result[name=="simpsons_palette"?0:1]=copied+0xCC;
        }
    }
    need(result[0]&&result[1]&&result[0]!=result[1],"Two distinct original textures missing");
    same(rt,dictionary,original,"Original loader changed immutable input dictionary");return result;
}
void fixture(Runtime& rt,uint32_t camera,uint32_t typed,uint32_t shadow,const std::array<uint32_t,2>& textures) {
    auto* base=rt.base;
    PPC_STORE_U32(packet,metadata);PPC_STORE_U32(packet+4,object);PPC_STORE_U32(packet+8,camera);
    PPC_STORE_U8(packet+0xC,1);PPC_STORE_U32(packet+0x18,typed);PPC_STORE_U32(packet+0x1C,shadow);PPC_STORE_U32(packet+0x20,shadow);
    PPC_STORE_U32(object,0x01000500);PPC_STORE_U32(object+4,objectFrame);
    PPC_STORE_U32(object+8,object+8);PPC_STORE_U32(object+0xC,object+8);
    PPC_STORE_U32(object+0x10,0x823CD3D8);PPC_STORE_U32(object+0x18,data);PPC_STORE_U32(object+0x3C,property);
    PPC_STORE_U32(data+0x24,offsets);PPC_STORE_U32(objectFrame+0xA0,objectFrame);identity(base,objectFrame+0x10);
    PPC_STORE_U8(property,0x40);
    const auto tickerOffset=PPC_LOAD_U32(0x82D6D86C);need(tickerOffset>=4&&tickerOffset<0x1000,"TimeTicker extension extent differs");
    // A negative authored start time selects the original -startTime branch,
    // giving an exact positive ticker without relying on wall-clock globals.
    PPC_STORE_U32(property+tickerOffset,0xBE000000);
    PPC_STORE_U32(metadata,0x00030002);PPC_STORE_U32(metadata+4,0xB5F8FBF2);
    PPC_STORE_U32(metadata+0xC,geometry);PPC_STORE_U32(metadata+0x10,1);PPC_STORE_U32(metadata+0x14,submeshes);
    PPC_STORE_U32(metadata+0x18,1);PPC_STORE_U32(metadata+0x34,collection);
    PPC_STORE_U32(geometry,4*36);PPC_STORE_U32(geometry+4,36);PPC_STORE_U32(geometry+8,6);
    PPC_STORE_U32(geometry+0xC,elements);PPC_STORE_U32(geometry+0x10,vertices);PPC_STORE_U32(geometry+0x14,8);
    PPC_STORE_U32(geometry+0x18,1);PPC_STORE_U32(geometry+0x1C,indices);PPC_STORE_U32(geometry+0x30,declCache);
    PPC_STORE_U32(geometry+0x50,vertices|3);PPC_STORE_U32(geometry+0x54,2);PPC_STORE_U32(geometry+0x58,0x20000002);
    PPC_STORE_U32(geometry+0x70,indices);PPC_STORE_U32(geometry+0x74,8);PPC_STORE_U32(declCache+4,declCache+0x40);
    constexpr uint32_t declaration[][3]={{0,0x002A23B9,0},{12,0x002A2187,0x00030000},
        {16,0x00182886,0x000A0000},{20,0x002C23A5,0x00050000},{28,0x002C23A5,0x00050100},
        {0x00FF0000,UINT32_MAX,0}};
    for(uint32_t i=0;i<6;++i)for(uint32_t lane=0;lane<3;++lane)PPC_STORE_U32(elements+12*i+4*lane,declaration[i][lane]);
    constexpr float xy[][2]={{-.5f,.5f},{-.5f,-.5f},{.5f,.5f},{.5f,-.5f}};
    for(uint32_t i=0;i<4;++i) {
        PPC_STORE_U32(vertices+36*i,std::bit_cast<uint32_t>(xy[i][0]));
        PPC_STORE_U32(vertices+36*i+4,std::bit_cast<uint32_t>(xy[i][1]));PPC_STORE_U32(vertices+36*i+8,0x3F000000);
        PPC_STORE_U32(vertices+36*i+12,0x055C7E3D);PPC_STORE_U32(vertices+36*i+16,0xE5000064);
        for(uint32_t uv=0;uv<2;++uv)for(uint32_t lane=0;lane<2;++lane)
            PPC_STORE_U32(vertices+36*i+20+8*uv+4*lane,std::bit_cast<uint32_t>(xy[i][lane]+.5f));
        PPC_STORE_U16(indices+2*i,uint16_t(i));
    }
    PPC_STORE_U32(submeshes,2);PPC_STORE_U32(submeshes+12,6);PPC_STORE_U32(submeshes+24,4);
    PPC_STORE_U32(offsets+8,0x30);PPC_STORE_U32(collection,1);PPC_STORE_U32(collection+12,material);
    // Use the real constructor's leaf binding table, including its six-word
    // reflection records. Only material row inputs are fixture-owned.
    constexpr std::array<uint32_t,10> handles{0x0048001C,0x00540022,0x00580024,0x005C0026,0x00600028,
        0x0064002A,0x0068002C,0x00740032,0x00780034,0x007C0036};
    const auto bindings=PPC_LOAD_U32(typed+0x28),bindingCount=PPC_LOAD_U32(typed+0x24);
    need(bindingCount&&bindingCount<=64,"Original typed material binding extent differs");
    for(uint32_t i=0;i<handles.size();++i) {
        uint32_t bindingIndex=UINT32_MAX;
        for(uint32_t j=0;j<bindingCount;++j)if(PPC_LOAD_U32(bindings+24*j)==handles[i])bindingIndex=j;
        need(bindingIndex!=UINT32_MAX,"Original animated UV material binding missing");
        const bool texture=i==7||i==8;const auto value=values+16*i;
        PPC_STORE_U32(materialRows+12*i,0x00400000|(bindingIndex<<16)|(texture?1u:3u));
        PPC_STORE_U32(materialRows+12*i+8,value);
        if(texture)PPC_STORE_U32(value+4,textures[i-7]);
        else if(i==2||i==6)for(uint32_t lane=0;lane<4;++lane)PPC_STORE_U32(value+4*lane,0x3F800000);
        else if(i==0){PPC_STORE_U32(value,0x3E800000);PPC_STORE_U32(value+4,0x3E800000);}
    }
    // textureFlags.x=0 selects the first original dual-layer mode; Y/Z/W=1.
    PPC_STORE_U32(values+16*6,0);
    PPC_STORE_U32(material+0xC,uint32_t(handles.size())<<10);PPC_STORE_U32(material+0x14,materialRows);
}
void run(Runtime& rt,const PPCContext& entry,const char* dictionaryPath) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();EngineCpuCalls cpu(entry,base);
    rt.map(area,0x10000,true,"original UV dispatcher CPU fixture");std::memset(rt.pointer(area,0x10000,true),0,0x10000);
    stage="real animated UV catalog construction";
    const auto context=PPC_LOAD_U32(0x82D5DA74),options=cpu.registers().r1.u32+0x60;
    PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    const auto manager=cpu.invoke(0x8269BF70,0x260,options);cpu.invoke(0x826B6F60,manager,context);
    need(!cpu.invoke(0x827019E8,table,25)&&!cpu.invoke(0x827019E8,secondTable,24),"Original catalog registration failed");
    cpu.invoke(0x826B7218,manager);
    const auto typed=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(secondTable+18*16+4));
    const auto shadow=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(table+4*16+4));
    need(typed&&shadow&&PPC_LOAD_U32(typed)==0x820616C0,"Original UV/shared rigid typed owner missing");
    const auto id=PPC_LOAD_U32(typed+0x1C),wrapper=PPC_LOAD_U32(typed+0x18);
    need(effects.count()==49&&effects.view(id).source==source&&effects.view(id).defaultVectorWords.size()==172,
         "Original UV catalog source or 688-byte parameter bank differs");
    cache(rt,cpu,id,body);queries(rt,cpu,id,body);const auto sourceBytes=snapshot(rt,source,0x3A70);
    stage="original two texture dictionary load";const auto textures=loadTextures(rt,entry,dictionaryPath);
    stage="original recording singleton construction";
    auto recording=PPC_LOAD_U32(0x82D09784);
    if(!recording) {
        recording=cpu.invoke(0x8269BD70,0x7C);cpu.registers().lr=0x823B748C;
        need(cpu.invoke(0x826F4988,recording)==recording,"Original recording singleton constructor failed");
    }
    driver.recordingOwners().validateOwner(recording,PPC_LOAD_U32(recording+0x68));
    need(PPC_LOAD_U32(recording)==0x820B856C&&PPC_LOAD_U32(recording+0x18)==2000&&
        !PPC_LOAD_U32(recording+0x1C)&&!PPC_LOAD_U32(recording+0x44)&&!PPC_LOAD_U32(recording+0x50),
        "Fixture requires an untouched genuine original recording singleton");
    stage="original world and character shadow copies";
    for(uint32_t slot=0;slot<2;++slot) {
        const auto camera=PPC_LOAD_U32(shadow+0x5B4+4*slot);PPC_STORE_U32(area+0x80,0x000000FF);
        need(cpu.invoke(0x823F1B80,camera,area+0x80,7)==camera,"Original shadow camera clear failed");
        EngineCpuCalls pass(entry,base);const auto saved=fullAbi(pass.registers());
        pass.invoke(0x82707220,shadow,slot);
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
    PPC_STORE_U32(0x82D6D814,headers);PPC_STORE_U32(0x82CF0BE8,0x01000000);PPC_STORE_U32(0x82D00F80,0);PPC_STORE_U32(0x82D6C0C0,0);
    need(cpu.invoke(0x823F1A18,camera)==camera,"Original main camera begin failed");
    identity(base,frame+0x10);identity(base,0x82D0CA70);identity(base,0x82CD1AB0);
    constexpr std::array<std::array<uint32_t,2>,22> states={{{0x28,1},{0x2C,6},{0x30,1},{0x34,0},{0x3C,0},{0x60,0},{0x6C,0},
        {0xAC,0},{0xC0,1},{0xC4,UINT32_MAX},{0xC8,0},{0xCC,0},{0xD0,0},{0xD4,15},{0xE4,0},{0x130,1},
        {0x144,1},{0x148,1},{0x14C,0xFFFF},{0x150,0},{0xD8,0},{0xDC,0}}};
    for(const auto& row:states)driver.directScalar(base,row[0],row[1]);
    const auto cullSelector=PPC_LOAD_U32(0x82E06F80+0x38);cpu.invoke(0x826B7968,cullSelector,1,1);
    const auto bound=driver.cameraBinding();uint32_t dispatcherCalls=0,fallbackCalls=0,vectorCalls=0,textureCalls=0;
    Observation observation([&](uint32_t pc,PPCContext& c,bool after) {
        if(after)return;
        if(pc==0x82740680){++dispatcherCalls;need(uint32_t(c.lr)==0x8273B4E0&&c.r3.u32==packet,"Original crashing dispatcher caller changed");}
        else if(pc==0x827400F8){++fallbackCalls;need(c.r4.u32==1&&c.r5.u32<=1,"Original UV alpha fallback flags differ");}
        else if(pc==0x8270BBC0)++vectorCalls;else if(pc==0x8270BE50)++textureCalls;
    });
    const auto call=[&] {
        EngineCpuCalls draw(entry,base);const auto saved=fullAbi(draw.registers());
        try {draw.invoke(0x8273B4D0,packet);}catch(...) {
            const auto& c=draw.registers();std::fprintf(stderr,"[UV ORIGINAL FAILURE] fn=%08X lr=%08X r1=%08X r3=%08X r5=%08X r11=%08X r29=%08X r31=%08X shadow=%08X A8=%08X AC=%08X\n",
                c.lastFunction,uint32_t(c.lr),c.r1.u32,c.r3.u32,c.r5.u32,c.r11.u32,c.r29.u32,c.r31.u32,shadow,PPC_LOAD_U32(typed+0xA8),PPC_LOAD_U32(typed+0xAC));throw;
        }
        need(fullAbi(draw.registers())==saved,"Whole original UV wrapper changed nonvolatile ABI");
        need(effects.privateModifiedMask(id)==std::array<uint8_t,128>{},"UV material dirty bank remains after commit");
        need(!PPC_LOAD_U32(0x82D0CAF8),"UV draw published a console device");
    };
    PPC_STORE_U32(area+0x80,0x00FF00FF);need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original main clear failed");
    const auto clearColor=driver.readbackColor(bound.colorIdentity);const auto mesh=snapshot(rt,geometry,0x700);
    stage="whole original opaque UV recording";call();
    const auto rootOffset=PPC_LOAD_U32(0x82D6D850);need(rootOffset<0x1000,"Original object cache extension differs");
    const auto bucket=cpu.invoke(0x8269D240,camera)+1;need(bucket<4,"Original camera recording bucket differs");
    const auto node=PPC_LOAD_U32(object+rootOffset+4*bucket);need(node&&PPC_LOAD_U32(node+0x1C)==metadata&&PPC_LOAD_U32(node+0x2C)==2,
        "Whole UV dispatcher did not publish the real original cache node");
    need(PPC_LOAD_U32(recording+0x18)==1999&&PPC_LOAD_U32(recording+0x1C)==1&&
        PPC_LOAD_U32(recording+0x44)==node&&PPC_LOAD_U32(recording+0x48)==node&&PPC_LOAD_U32(recording+0x50)==1&&
        PPC_LOAD_U32(node+0x14)&&PPC_LOAD_U32(recording+0x4C)==PPC_LOAD_U32(node+0x14),
        "Original UV recording did not borrow one real CPU slot and account its owned payload");
    const auto payload=PPC_LOAD_U32(node+0x28);driver.recordingOwners().requireReplay(packet,payload);
    const auto first=driver.readbackColor(bound.colorIdentity);need(pixel(first,640,360)!=pixel(clearColor,640,360),"Opaque UV shader produced no center pixel");
    need(pixel(first,1100,360)==pixel(clearColor,1100,360),"Opaque UV shader escaped original geometry bounds");
    need(vectorCalls==8&&textureCalls==2&&!fallbackCalls,"Original UV material callback row traversal differs");
    const auto committed=effects.view(id).defaultVectorWords;
    need(committed[136]==textures[0]&&committed[152]==textures[1]&&committed[96]==0x3F800000&&committed[112]==0,
        "Original UV callbacks did not store both texture headers and animated material values");
    need(PPC_LOAD_U32(0x82D6C0D0+16*22)==0x3E000000,"Original DELTA_TIME callback did not upload VS c22");
    stage="whole original opaque UV cached replay";
    need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original replay clear failed");call();
    need(driver.readbackColor(bound.colorIdentity)==first&&vectorCalls==8&&textureCalls==2&&
        PPC_LOAD_U32(object+rootOffset+4*bucket)==node&&PPC_LOAD_U32(node+0x28)==payload,
        "UV cached replay rerecorded material rows, changed payload, or changed pixels");
    stage="cached UV replay updates original TimeTicker";
    PPC_STORE_U32(property+PPC_LOAD_U32(0x82D6D86C),0xBE800000);
    need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original ticker replay clear failed");call();
    need(PPC_LOAD_U32(0x82D6C0D0+16*22)==0x3E800000&&driver.readbackColor(bound.colorIdentity)==first&&
        vectorCalls==8&&textureCalls==2&&PPC_LOAD_U32(node+0x28)==payload,
        "Cached UV TimeTicker omitted original update or disturbed recorded material/geometry");
    stage="whole original alpha UV fallback";
    // The real dispatcher derives alpha from metadata bit0; setting bit1 also
    // supplies incoming r5=1, proven overwritten before use in original400F8.
    PPC_STORE_U32(metadata+8,3);PPC_STORE_U32(0x82CF0BE8,0);
    need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original alpha clear failed");call();
    const auto alpha=driver.readbackColor(bound.colorIdentity);
    if(pixel(alpha,640,360)==pixel(clearColor,640,360)||pixel(alpha,1100,360)!=pixel(clearColor,1100,360)) {
        size_t changed=0;for(size_t i=0;i<alpha.size();i+=4)changed+=std::memcmp(alpha.data()+i,clearColor.data()+i,4)!=0;
        std::fprintf(stderr,"[UV ALPHA PIXELS] changed=%zu center=%08X clear=%08X outside=%08X flags=%08X/%08X/%08X/%08X scale=%08X color=%08X\n",
            changed,pixel(alpha,640,360),pixel(clearColor,640,360),pixel(alpha,1100,360),
            PPC_LOAD_U32(values+16*6),PPC_LOAD_U32(values+16*6+4),PPC_LOAD_U32(values+16*6+8),PPC_LOAD_U32(values+16*6+12),
            PPC_LOAD_U32(values+16*2),PPC_LOAD_U32(vertices+16));
        std::fprintf(stderr,"[UV ALPHA VP]");for(uint32_t i=0;i<16;++i)std::fprintf(stderr," %08X",PPC_LOAD_U32(0x82D6C0D0+4*i));std::fputc('\n',stderr);
    }
    need(fallbackCalls==1&&dispatcherCalls==4&&vectorCalls==16&&textureCalls==4,"UV alpha fallback omitted original material callbacks");
    need(PPC_LOAD_U32(0x82D6C0D0+16*22)==0x3E800000,"Alpha UV fallback omitted original TimeTicker upload");
    need(pixel(alpha,640,360)!=pixel(clearColor,640,360)&&pixel(alpha,1100,360)==pixel(clearColor,1100,360),"Alpha UV shader output or bounds differs");
    need(PPC_LOAD_U32(object+rootOffset+4*bucket)==node&&PPC_LOAD_U32(node+0x28)==payload,"UV alpha fallback changed cached opaque ownership");
    same(rt,geometry,mesh,"UV dispatcher changed original input geometry");same(rt,source,sourceBytes,"UV regression changed original serialized shader record");
    cpu.invoke(0x826B4B18,wrapper);need(cpu.invoke(0x823F1A08,camera)==camera,"Original main camera end failed");
    // The completed cache deliberately remains alive, as in the recording
    // graph fixture. Runtime terminal teardown owns these test-only lifetimes.
}
}
PPC_FUNC(sub_82740680){forward(0x82740680,ctx,base,__imp__sub_82740680);}
PPC_FUNC(sub_827400F8){forward(0x827400F8,ctx,base,__imp__sub_827400F8);}
PPC_FUNC(sub_8270BBC0){forward(0x8270BBC0,ctx,base,__imp__sub_8270BBC0);}
PPC_FUNC(sub_8270BE50){forward(0x8270BE50,ctx,base,__imp__sub_8270BE50);}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==3,"Original image and loc_split4 ITXD dictionary required");Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original startup boundary missing");run(rt,entry,argv[2]);
        std::printf("PASS original rigid UV:%zu checks; real catalogs/textures/cameras, crashing dispatcher, material callbacks, opaque recording/replay, alpha fallback, dirty and ABI\n",checks);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original rigid UV:%zu checks stage=%s:%s\n",checks,stage,error.what());return 1;}
}
