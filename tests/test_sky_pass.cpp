// Genuine original sky scene dispatcher, reflected callbacks and draw loop.
#include "effect_catalog_lifecycle_helpers.h"
#include "effect_draw_cleanup_helpers.h"
#include "runtime/engine_builtin_textures.h"
#include "runtime/engine_audio.h"
#include "renderer/engine_state.h"
#include <bit>
#include <functional>
#include <string_view>
#include "effect_screen_replacement_helpers.h"

extern "C" void __imp__sub_82740680(PPCContext&,uint8_t*);
extern "C" void __imp__sub_827400F8(PPCContext&,uint8_t*);
extern "C" void __imp__sub_82701220(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8270BBC0(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8270BE50(PPCContext&,uint8_t*);
namespace {
constexpr uint32_t source=0x82036448,body=source+12,secondTable=0x82CD1448;
constexpr uint32_t area=0x60000,packet=area,metadata=0x60100,object=0x61000,data=0x63000,
    offsets=0x64000,geometry=0x65000,elements=0x65100,vertices=0x65200,indices=0x65400,
    submeshes=0x65500,declCache=0x65600,headers=0x65700,objectFrame=0x65800,
    collection=0x66000,material=0x66100,materialRows=0x66200,values=0x66500,property=0x67000;
std::function<void(uint32_t,PPCContext&)> observer;
bool paddedLayout=false;
void forward(uint32_t pc,PPCContext& c,uint8_t* b,void(*original)(PPCContext&,uint8_t*)) {
    if(observer)observer(pc,c);original(c,b);
}
struct Observation {
    explicit Observation(decltype(observer) value){need(!observer,"Nested sky observer");observer=std::move(value);}
    ~Observation(){observer={};}
};
struct Restore {
    uint8_t* address;std::vector<uint8_t> bytes;
    Restore(Runtime& rt,uint32_t at,uint32_t size):address(rt.pointer(at,size,true)),bytes(address,address+size){}
    ~Restore(){std::memcpy(address,bytes.data(),bytes.size());}
};
struct FullAbi {
    SavedAbi integer;std::array<uint64_t,18> floating;
    bool operator==(const FullAbi&)const=default;
};
FullAbi fullAbi(const PPCContext& c) {
    return {abi(c),{c.f14.u64,c.f15.u64,c.f16.u64,c.f17.u64,c.f18.u64,c.f19.u64,c.f20.u64,c.f21.u64,c.f22.u64,
        c.f23.u64,c.f24.u64,c.f25.u64,c.f26.u64,c.f27.u64,c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64}};
}
void identity(uint8_t* base,uint32_t at){for(uint32_t i=0;i<16;++i)PPC_STORE_U32(at+4*i,i%5?0:0x3F800000);}
uint32_t pixel(const std::vector<uint8_t>& bytes,uint32_t x,uint32_t y) {
    need(bytes.size()==size_t(1280)*720*4,"Sky color extent differs");uint32_t result{};
    std::memcpy(&result,bytes.data()+4*(1280*y+x),4);return result;
}
void evidence(uint8_t* base) {
    constexpr std::array<std::array<uint32_t,2>,17> pins={{{0x823CA740,0x54EB063E},{0x823CA744,0x2B0B0000},
        {0x823CA748,0x419A000C},{0x823CA74C,0x808300AC},{0x823CA750,0x48000008},{0x823CA754,0x808300A8},
        {0x823CA758,0x80630018},{0x823CA75C,0x482EB91D},{0x827408B4,0x81690008},{0x827408B8,0x5575FFFE},
        {0x82740914,0x81490008},{0x82740918,0x5556F7FE},{0x82740B14,0x7EC6B378},{0x82740B18,0x7EA5AB78},
        {0x82740B1C,0x7EE4BB78},{0x82740B20,0x7FE3FB78},{0x82740B24,0x4BFFF5D5}}};
    for(const auto& p:pins)need(PPC_LOAD_U32(p[0])==p[1],"Original sky selector/dispatcher instruction differs");
    need(PPC_LOAD_U32(0x8270ACCC)==0x3F6082D7&&PPC_LOAD_U32(0x8270ACD4)==0x83FBC7F0&&
         PPC_LOAD_U32(0x8270ACE0)==0x3D6082D6&&PPC_LOAD_U32(0x8270ACE4)==0x83EB3004&&
         PPC_LOAD_U32(0x8270ACEC)==0x93FBC7F0,"Original sky line callback source/publication differs");
    need(PPC_LOAD_U32(secondTable+13*16)==source,"Original sky catalog source differs");
    need(PPC_LOAD_U32(source+4)+12==11712&&PPC_LOAD_U32(body+0x118)==30&&
        PPC_LOAD_U32(body+0x130)==26&&PPC_LOAD_U32(body+0x138)==188*4,"Original sky material storage differs");
    constexpr uint32_t contexts[]={0x2480,0x27B0},vs[]={0x82036C2C,0x82036F08},ps[]={0x820371EC,0x820374E8};
    for(uint32_t pass=0;pass<2;++pass) {
        need(body+PPC_LOAD_U32(body+contexts[pass]+0x48)+8==vs[pass]&&
             body+PPC_LOAD_U32(body+contexts[pass]+0x4C)+8==ps[pass],"Original sky selected shader differs");
        const auto map=body+PPC_LOAD_U32(body+contexts[pass]+0x40);
        for(uint32_t leaf=0;leaf<26;++leaf) {
            const bool active=leaf==17||leaf==18||leaf==19||leaf==20||leaf==21||leaf==22||leaf==24||leaf==25;
            need(bool(PPC_LOAD_U32(map+16*leaf))==active,"Original sky used/unused material map differs");
        }
        need(PPC_LOAD_U32(map+16*17+4)==47&&PPC_LOAD_U32(map+16*18+4)==46&&
             PPC_LOAD_U32(map+16*19+4)==22&&PPC_LOAD_U32(map+16*25+8)==23,
             "Original sky animation/coordinate register differs");
    }
}
void fixture(Runtime& rt,uint32_t camera,uint32_t typed,uint32_t shadow,uint32_t white) {
    auto* base=rt.base;
    PPC_STORE_U32(packet,metadata);PPC_STORE_U32(packet+4,object);PPC_STORE_U32(packet+8,camera);
    PPC_STORE_U32(packet+0x18,typed);PPC_STORE_U32(packet+0x1C,shadow);PPC_STORE_U32(packet+0x20,shadow);
    PPC_STORE_U32(object,0x01000500);PPC_STORE_U32(object+4,objectFrame);
    PPC_STORE_U32(object+8,object+8);PPC_STORE_U32(object+0xC,object+8);PPC_STORE_U32(object+0x10,0x823CD3D8);
    PPC_STORE_U32(object+0x18,data);PPC_STORE_U32(object+0x3C,property);PPC_STORE_U32(data+0x24,offsets);
    PPC_STORE_U32(objectFrame+0xA0,objectFrame);identity(base,objectFrame+0x10);PPC_STORE_U8(property,0x40);
    const auto tickerOffset=PPC_LOAD_U32(0x82D6D86C);need(tickerOffset>=4&&tickerOffset<0x1000,"Sky ticker property offset differs");
    PPC_STORE_U32(property+tickerOffset,0xBE000000);
    PPC_STORE_U32(metadata,0x00030002);PPC_STORE_U32(metadata+4,0xB5F8FBF2);PPC_STORE_U32(metadata+0xC,geometry);
    PPC_STORE_U32(metadata+0x10,1);PPC_STORE_U32(metadata+0x14,submeshes);PPC_STORE_U32(metadata+0x18,1);
    PPC_STORE_U32(metadata+0x34,collection);
    PPC_STORE_U32(geometry,4*28);PPC_STORE_U32(geometry+4,28);PPC_STORE_U32(geometry+8,4);
    PPC_STORE_U32(geometry+0xC,elements);PPC_STORE_U32(geometry+0x10,vertices);PPC_STORE_U32(geometry+0x14,8);
    PPC_STORE_U32(geometry+0x18,1);PPC_STORE_U32(geometry+0x1C,indices);PPC_STORE_U32(geometry+0x30,declCache);
    PPC_STORE_U32(geometry+0x50,vertices|3);PPC_STORE_U32(geometry+0x54,2);PPC_STORE_U32(geometry+0x58,0x20000002);
    PPC_STORE_U32(geometry+0x70,indices);PPC_STORE_U32(geometry+0x74,8);PPC_STORE_U32(declCache+4,declCache+0x40);
    constexpr uint32_t declaration[][3]={{0,0x002A23B9,0},{12,0x002C23A5,0x00050000},
        {20,0x002C23A5,0x00050100},{0x00FF0000,UINT32_MAX,0}};
    for(uint32_t i=0;i<4;++i)for(uint32_t lane=0;lane<3;++lane)PPC_STORE_U32(elements+12*i+4*lane,declaration[i][lane]);
    constexpr float xy[][2]={{-.5f,.5f},{-.5f,-.5f},{.5f,.5f},{.5f,-.5f}};
    for(uint32_t i=0;i<4;++i) {
        PPC_STORE_U32(vertices+28*i,std::bit_cast<uint32_t>(xy[i][0]));
        PPC_STORE_U32(vertices+28*i+4,std::bit_cast<uint32_t>(xy[i][1]));PPC_STORE_U32(vertices+28*i+8,0x3F000000);
        PPC_STORE_U32(vertices+28*i+12,0x3E800000);PPC_STORE_U32(vertices+28*i+16,0x3F000000);
        PPC_STORE_U32(vertices+28*i+20,0x3F600000);PPC_STORE_U32(vertices+28*i+24,0xBF200000);
        PPC_STORE_U16(indices+2*i,uint16_t(i));
    }
    PPC_STORE_U32(submeshes,2);PPC_STORE_U32(submeshes+12,6);PPC_STORE_U32(submeshes+24,4);
    PPC_STORE_U32(offsets+8,0x30);PPC_STORE_U32(collection,1);PPC_STORE_U32(collection+12,material);
    constexpr uint32_t handles[]={0x00540022,0x00580024,0x00600028,0x0064002A,0x0068002C};
    const auto bindings=PPC_LOAD_U32(typed+0x28),count=PPC_LOAD_U32(typed+0x24);
    for(uint32_t i=0;i<std::size(handles);++i) {
        uint32_t binding=UINT32_MAX;for(uint32_t j=0;j<count;++j)if(PPC_LOAD_U32(bindings+24*j)==handles[i])binding=j;
        need(binding!=UINT32_MAX,"Original sky material handle missing");const auto value=values+16*i;
        PPC_STORE_U32(materialRows+12*i,0x00400000|(binding<<16)|(i>=2?1u:3u));PPC_STORE_U32(materialRows+12*i+8,value);
        if(i>=2)PPC_STORE_U32(value+4,white);
    }
    PPC_STORE_U32(material+0xC,uint32_t(std::size(handles))<<10);PPC_STORE_U32(material+0x14,materialRows);
    if(paddedLayout) {
        const auto original=snapshot(rt,vertices,4*28);
        std::memset(rt.pointer(vertices,4*64,true),0xFF,4*64);
        for(uint32_t i=0;i<4;++i)std::memcpy(rt.pointer(vertices+64*i+36,28,true),original.data()+28*i,28);
        for(uint32_t i=0;i<3;++i)PPC_STORE_U32(elements+12*i,PPC_LOAD_U32(elements+12*i)+36);
        // SDK semantic/index matching must preserve UV0/UV1 independently
        // when declaration row order differs from the original capture.
        const auto position=snapshot(rt,elements,12),uv1=snapshot(rt,elements+24,12);
        std::memcpy(rt.pointer(elements,12,true),uv1.data(),12);
        std::memcpy(rt.pointer(elements+24,12,true),position.data(),12);
        PPC_STORE_U32(geometry,4*64);PPC_STORE_U32(geometry+4,64);
    }
}
void run(Runtime& rt,const PPCContext& entry) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();EngineCpuCalls cpu(entry,base);
    OriginalDrawCatalogCleanup cleanup(rt);
    stage="sky original instruction evidence";evidence(base);rt.map(area,0x10000,true,"original sky fixture");
    std::memset(rt.pointer(area,0x10000,true),0,0x10000);
    stage="real sky catalog and builtin image owners";
    const auto context=PPC_LOAD_U32(0x82D5DA74),options=cpu.registers().r1.u32+0x60;
    PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    const auto manager=cpu.invoke(0x8269BF70,0x260,options);cpu.invoke(0x826B6F60,manager,context);
    need(!cpu.invoke(0x827019E8,table,25)&&!cpu.invoke(0x827019E8,secondTable,24),"Original sky catalogs failed");
    cpu.invoke(0x826B7218,manager);
    const auto typed=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(secondTable+13*16+4));
    const auto shadow=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(table+4*16+4));
    need(typed&&shadow&&PPC_LOAD_U32(typed)==0x820616C0,"Original sky typed owner missing");
    const auto id=PPC_LOAD_U32(typed+0x1C),wrapper=PPC_LOAD_U32(typed+0x18);
    need(effects.view(id).source==source&&effects.view(id).defaultVectorWords.size()==188,"Original sky private bank differs");
    cache(rt,cpu,id,body);queries(rt,cpu,id,body);const auto immutable=snapshot(rt,source,11712);
    need(!cpu.invoke(0x826FF0F8,context,0)&&driver.builtinTextures().complete(),"Original builtin image parent failed");
    const auto white=PPC_LOAD_U32(0x82D63004),black=PPC_LOAD_U32(0x82D6300C);
    need(white&&black&&white!=black&&driver.materialTexture(base,white)&&driver.materialTexture(base,black),
         "Sky builtin sampler owners are not published");
    stage="original empty shadow parents";
    for(uint32_t slot=0;slot<2;++slot) {
        const auto camera=PPC_LOAD_U32(shadow+0x5B4+4*slot);PPC_STORE_U32(area+0x80,0x000000FF);
        need(cpu.invoke(0x823F1B80,camera,area+0x80,7)==camera,"Original sky shadow clear failed");
        EngineCpuCalls pass(entry,base);const auto before=fullAbi(pass.registers());pass.invoke(0x82707220,shadow,slot);
        need(fullAbi(pass.registers())==before,"Original empty shadow parent lost ABI");
    }
    const auto viewport=cpu.invoke(0x8269D788);need(viewport,"Original sky viewport missing");
    cpu.registers().f1.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12CC));cpu.registers().f2.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D0));
    cpu.registers().f3.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D4));cpu.registers().f4.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D8));
    cpu.invoke(0x8269D608,viewport,0);const auto camera=PPC_LOAD_U32(viewport+0x14),frame=PPC_LOAD_U32(camera+4);
    fixture(rt,camera,typed,shadow,white);
    uint32_t ownedDeclaration=0;
    if(paddedLayout) {
        const auto cached=cpu.invoke(0x82701BD8,elements,PPC_LOAD_U32(geometry+8));
        ownedDeclaration=PPC_LOAD_U32(cached+4);need(ownedDeclaration,"Original padded sky declaration missing");
        PPC_STORE_U32(geometry+0x30,cached);
        need(rt.engineAudio->allocationGeneration(ownedDeclaration,0x50)!=0,"Original padded sky declaration has no allocator owner");
    }
    Restore flags(rt,0x82D6CCA8,4),nativeContext(rt,0x82D6D890,4),published(rt,0x82D63028,4),
        materialRoot(rt,0x82D6D814,4),recordEnabled(rt,0x82CF0BE8,4),dirty(rt,0x82D00F80,4),
        propertyBase(rt,0x82D6C0C0,4),view(rt,0x82D0CA70,64),projection(rt,0x82CD1AB0,64),cameraMatrix(rt,frame+0x10,64),
        environment(rt,0x82D6C7F0,4);
    PPC_STORE_U32(0x82D6CCA8,0);PPC_STORE_U32(0x82D6D890,context);PPC_STORE_U32(0x82D63028,0);
    PPC_STORE_U32(0x82D6D814,headers);PPC_STORE_U32(0x82CF0BE8,0);PPC_STORE_U32(0x82D00F80,0);PPC_STORE_U32(0x82D6C0C0,0);
    // The original ENGINE5 callback consumes this real published image. Its
    // identity, not a fabricated material setter or SDK texture, reaches t3.
    PPC_STORE_U32(0x82D6C7F0,black);
    need(cpu.invoke(0x823F1A18,camera)==camera,"Original sky camera begin failed");
    identity(base,frame+0x10);identity(base,0x82D0CA70);identity(base,0x82CD1AB0);
    constexpr std::array<std::array<uint32_t,2>,22> states={{{0x28,1},{0x2C,7},{0x30,1},{0x34,0},{0x3C,0},{0x60,0},{0x6C,0},
        {0xAC,0},{0xC0,1},{0xC4,UINT32_MAX},{0xC8,0},{0xCC,0},{0xD0,0},{0xD4,15},{0xE4,0},{0x130,1},
        {0x144,1},{0x148,1},{0x14C,0xFFFF},{0x150,0},{0xD8,0},{0xDC,0}}};
    const auto baseline=[&]{for(const auto& row:states)driver.directScalar(base,row[0],row[1]);
        cpu.invoke(0x826B7968,PPC_LOAD_U32(0x82E06F80+0x38),1,1);};
    // Retail alpha fallback restores its metadata-bit2 depth-write request
    // after drawing. Selector3 is SDK scalar0x30, not the pass's draw state.
    for(const auto [address,word]:std::array<std::array<uint32_t,2>,7>{{
        {0x8215058C,0x00000030},{0x82740224,0x38800000},{0x82740228,0x38600003},
        {0x8274022C,0x4BF7773D},{0x827402D8,0x38800001},{0x827402DC,0x38600003},
        {0x827402E0,0x4BF77689}}})
        need(PPC_LOAD_U32(address)==word,"Original sky alpha depth-write cleanup evidence differs");
    baseline();const auto bound=driver.cameraBinding();uint32_t calls{},fallbacks{},meshEntries{},vectors{},textures{},expectedAlpha{},expectedBit1{},expectedBit2{};
    Observation observing([&](uint32_t pc,PPCContext& c){
        if(pc==0x82740680){++calls;need(uint32_t(c.lr)==0x8273B4E0&&c.r3.u32==packet,"Sky genuine scene dispatcher differs");}
        else if(pc==0x827400F8){++fallbacks;need(c.r4.u32==expectedAlpha&&c.r5.u32==expectedBit1&&c.r6.u32==expectedBit2,
            "Sky original Boolean fallback arguments differ");}
        else if(pc==0x82701220){++meshEntries;
            need(uint32_t(c.lr)==0x827402B0&&c.r5.u32==typed,
                 "Sky original static mesh caller/owner differs");
            need(driver.effectiveState().scalar(Graphics::ScalarState::DepthEnable)==1&&
                 driver.effectiveState().scalar(Graphics::ScalarState::DepthWrite)==0,
                 "Sky original mesh did not receive the pass's depth enable/write0 state");}
        else if(pc==0x8270BBC0)++vectors;else if(pc==0x8270BE50)++textures;
    });
    PPC_STORE_U32(area+0x80,0x00FF00FF);need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original sky clear failed");
    const auto clear=driver.readbackColor(bound.colorIdentity),mesh=snapshot(rt,geometry,0x700);
    const auto draw=[&](uint32_t alpha,uint32_t bit1,uint32_t bit2,bool discarded){
        // Each qualification starts a new original pass. Resetting the
        // scheduler's baseline while the same technique remains selected
        // would correctly take retail's no-op begin without reapplying its
        // authored depth-write0 scalar.
        if(PPC_LOAD_U32(manager+4)) {
            need(PPC_LOAD_U32(manager+4)==wrapper,"Sky variant found a foreign selected wrapper");
            cpu.invoke(0x826B4B18,wrapper);
        }
        expectedAlpha=alpha;expectedBit1=bit1;expectedBit2=bit2;PPC_STORE_U8(packet+12,uint8_t(alpha));
        PPC_STORE_U32(metadata+8,1|(bit1<<1)|(bit2<<2));baseline();
        need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original sky variant clear failed");
        EngineCpuCalls pass(entry,base);const auto before=fullAbi(pass.registers());pass.invoke(0x8273B4D0,packet);
        need(fullAbi(pass.registers())==before,"Whole original sky draw lost nonvolatile ABI");
        need(PPC_LOAD_U32(manager+8)==id&&PPC_LOAD_U32(manager+12)==(alpha?0x7FFFCu:0x3FFFCu),
             "Original sky selected the wrong technique");
        need(driver.effectiveState().scalar(Graphics::ScalarState::DepthEnable)==1&&
             driver.effectiveState().scalar(Graphics::ScalarState::DepthWrite)==uint32_t(alpha&&bit2),
             "Sky original alpha cleanup did not restore its metadata depth-write request");
        need(PPC_LOAD_U32(0x82D6C0D0+16*22)==(PPC_LOAD_U32(property+PPC_LOAD_U32(0x82D6D86C))^0x80000000u),
             "Original sky TimeTicker callback omitted VS22");
        cpu.invoke(0x8269D428,area+0xA0,camera);
        for(uint32_t lane=0;lane<4;++lane)need(PPC_LOAD_U32(0x82D6C450+16*23+4*lane)==PPC_LOAD_U32(area+0xA0+4*lane),
             "Original sky camera texCoords callback omitted PS23");
        const auto result=driver.readbackColor(bound.colorIdentity);
        need(discarded?result==clear:pixel(result,640,360)==UINT32_MAX,"Sky original line discard or white layers differ");
        need(pixel(result,1100,360)==pixel(clear,1100,360),"Sky original geometry escaped its bounds");
        need(effects.privateModifiedMask(id)==std::array<uint8_t,128>{}&&!PPC_LOAD_U32(0x82D0CAF8),"Sky commit left dirty data or console device");
        const auto words=effects.view(id).defaultVectorWords;
        need(words[104]==white&&words[120]==white&&words[136]==white&&!words[168],
             "Sky material callback or inherited line owner differs");
        same(rt,geometry,mesh,"Sky original draw changed source geometry");return result;
    };
    stage="all sky opaque/alpha Boolean dispatcher combinations";
    std::vector<uint8_t> reference;
    for(uint32_t alpha=0;alpha<2;++alpha)for(uint32_t bit1=0;bit1<2;++bit1)for(uint32_t bit2=0;bit2<2;++bit2){
        const auto result=draw(alpha,bit1,bit2,false);if(reference.empty())reference=result;
        need(result==reference,"Sky Boolean metadata changed the original white-layer result");
    }
    stage="original sky inherited line white discard";PPC_STORE_U32(0x82D6C7F0,white);draw(0,0,0,true);draw(1,1,1,true);
    stage="original sky ticker refresh";PPC_STORE_U32(0x82D6C7F0,black);
    PPC_STORE_U32(property+PPC_LOAD_U32(0x82D6D86C),0xBE800000);need(draw(0,1,1,false)==reference,"Sky zero cloud velocity changed with ticker");
    need(calls==11&&fallbacks==11&&meshEntries==11&&vectors==22&&textures==33,"Original sky callback/draw traversal count differs");
    screenReplacementRegression(rt,cpu,camera,typed,wrapper,area+0xB0,[&]{draw(0,1,1,false);});
    same(rt,source,immutable,"Sky fixture changed immutable original effect source");cpu.invoke(0x826B4B18,wrapper);
    need(cpu.invoke(0x823F1A08,camera)==camera,"Original sky camera end failed");
    if(paddedLayout) {
        cpu.invoke(0x82700A78);
        rejects([&]{rt.engineAudio->allocationGeneration(ownedDeclaration,0x50);},"Original padded sky declaration was not retired");
        cleanup.release(rt,cpu,manager);
        same(rt,source,immutable,"Original padded sky cleanup changed serialized source");
        std::printf("AUDIT_GEOMETRY_LIFECYCLE source=%08X stride=64 delta=36 create=passed use=passed declaration_release=passed fx_release=passed cpu_cache_release=passed malformed=passed malformed_scope=original_screen_cache stale_use=passed backend_mesh_cache=owner_resident full_gpu_retirement=unproven\n",source);
    }
}
}
PPC_FUNC(sub_82740680){forward(0x82740680,ctx,base,__imp__sub_82740680);}
PPC_FUNC(sub_827400F8){forward(0x827400F8,ctx,base,__imp__sub_827400F8);}
PPC_FUNC(sub_82701220){forward(0x82701220,ctx,base,__imp__sub_82701220);}
PPC_FUNC(sub_8270BBC0){forward(0x8270BBC0,ctx,base,__imp__sub_8270BBC0);}
PPC_FUNC(sub_8270BE50){forward(0x8270BE50,ctx,base,__imp__sub_8270BE50);}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==2||argc==3,"Supply original image and optional padded layout");
        if(argc==3){need(std::string_view(argv[2])=="padded","Unknown original sky layout");paddedLayout=true;}
        Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original graphics startup boundary missing");run(rt,entry);
        std::printf("PASS original sky: %zu checks; distinct passes, all8 Boolean dispatcher combinations, real cloud/builtin line owners, original ticker/camera callbacks, discard and full ABI\n",checks);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original sky: %zu checks stage=%s: %s\n",checks,stage,error.what());return 1;}
}
