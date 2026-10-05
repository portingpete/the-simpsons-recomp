// The native hook skips the console shader-binding block, then resumes the
// complete retail CPU upload. Pin the skipped selection independently; run
// the real producer, ring upload, matrix helper and native resource owners.
#include "effect_catalog_lifecycle_helpers.h"
#include "runtime/engine_particles.h"
#include "runtime/engine_itxd_textures.h"
#include "renderer/particle_draw.h"
#include "renderer/engine_state.h"
#include <bit>
#include <filesystem>
#include <fstream>

extern "C" void __imp__sub_827B8328(PPCContext&,uint8_t*);
namespace Simpsons::Graphics {
// Isolated fixture baseline: camera selection creates/owns its attachments,
// but intentionally does not establish an RS viewport. Use the existing
// resource probe to set that one context field before the original draw.
struct NativeRenderResolutionProbe {
    static D3D11_VIEWPORT baseline(const std::shared_ptr<RenderTarget>& target) {
        ComPtr<ID3D11Device> device;target->texture->GetDevice(&device);
        ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);
        const D3D11_VIEWPORT viewport{0,0,float(target->pixelWidth()),float(target->pixelHeight()),0,1};
        context->RSSetViewports(1,&viewport);
        D3D11_VIEWPORT observed{};UINT count=1;context->RSGetViewports(&count,&observed);
        if(count!=1)throw Failure("Particle fixture viewport baseline was not retained");
        return observed;
    }
};
}
namespace {
constexpr uint32_t area=0x60000,emitter=0x61000,definition=0x62000,
    parameters=0x63000,parent=0x64000,curve=0x65000,bucket=0x66000,
    texture0=0x67000,texture1=0x67100,dictionary=0x80000;
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
    return {abi(c),{c.f14.u64,c.f15.u64,c.f16.u64,c.f17.u64,c.f18.u64,c.f19.u64,c.f20.u64,
        c.f21.u64,c.f22.u64,c.f23.u64,c.f24.u64,c.f25.u64,c.f26.u64,c.f27.u64,c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64}};
}
void identity(uint8_t* base,uint32_t at) {
    for(uint32_t i=0;i<16;++i)PPC_STORE_U32(at+4*i,(i%5)?0:0x3F800000);
}
void scalar(uint8_t* base,uint32_t at,float value){PPC_STORE_U32(at,std::bit_cast<uint32_t>(value));}
struct Variant {uint8_t type,flags,mode;uint32_t vsRegistry,vsRecord,psRegistry,psRecord;};
// These are the retail registry records, not native classification output.
constexpr std::array<Variant,8> variants={{{0,0,0,0x82CF25F8,0x821570E0,0x82CF25C8,0x82156770},
    {0,0,1,0x82CF25F8,0x821570E0,0x82CF25D4,0x82156948},
    {0,4,0,0x82CF25F8,0x821570E0,0x82CF25E0,0x82156B60},
    {0,4,1,0x82CF25F8,0x821570E0,0x82CF25EC,0x82156DF0},
    {5,0,0,0x82CF2604,0x821578E0,0x82CF25C8,0x82156770},
    {5,0,1,0x82CF2604,0x821578E0,0x82CF25D4,0x82156948},
    {5,4,0,0x82CF2604,0x821578E0,0x82CF25E0,0x82156B60},
    {5,4,1,0x82CF2604,0x821578E0,0x82CF25EC,0x82156DF0}}};
void evidence(uint8_t* base) {
    // Independent literal instruction evidence for type==5, mode==1, the
    // four PS choices, c10.z bit conversion, live ring index and UV flip.
    constexpr std::array<std::array<uint32_t,2>,31> pins={{{0x82772D18,0x3BA00000},
        {0x82772DA0,0x896B0100},{0x82772DA4,0x2B0B0005},{0x82772DB0,0x808B2600},
        {0x82772DBC,0x808B25F4},{0x82772DD8,0x896B0104},{0x82772DDC,0x556B077A},
        {0x82772DF8,0x817B0178},{0x82772DFC,0x2B0B0000},{0x82772E00,0x419A0194},
        {0x82772F94,0xD3E10060},{0x82772FA8,0x817E0118},
        {0x82772FB0,0x2B0B0001},{0x82772FB8,0x419A0044},{0x82772FDC,0x48000110},
        {0x82772FCC,0x808B25DC},{0x82772FE8,0x808B25C4},{0x82773000,0x419A0010},
        {0x82773008,0x808B25E8},{0x82773014,0x808B25D0},
        {0x82773028,0x817E00D4},{0x827730CC,0x817E0118},{0x827730D0,0xA16B0042},
        {0x827730D4,0x556B07FE},{0x827730D8,0xF9610070},{0x827730E4,0xFC000018},{0x827730EC,0x817FCAF8},
        {0x82773394,0xA15E00F6},{0x82773540,0x89260104},{0x82773544,0x55290672},
        {0x82773598,0xB12B0034}}};
    for(const auto& pin:pins)need(PPC_LOAD_U32(pin[0])==pin[1],"Original particle producer/selection evidence changed");
    for(const auto& v:variants)need(PPC_LOAD_U32(v.vsRegistry)==v.vsRecord&&PPC_LOAD_U32(v.psRegistry)==v.psRecord,
        "Original particle shader registry association changed");
}
std::array<uint32_t,2> loadTextures(Runtime& rt,const PPCContext& entry,const char* path) {
    auto* base=rt.base;EngineCpuCalls cpu(entry,base);
    std::ifstream input(path,std::ios::binary|std::ios::ate);need(bool(input),"Original ITXD dictionary required");
    const auto length=input.tellg();need(length>0&&length<=0x200000,"Particle ITXD dictionary extent differs");
    const auto bytes=uint32_t(length);rt.map(dictionary,bytes,true,"original particle texture dictionary");
    input.seekg(0);input.read(reinterpret_cast<char*>(rt.pointer(dictionary,bytes,true)),bytes);
    need(bool(input),"Original particle dictionary read failed");const auto before=snapshot(rt,dictionary,bytes);
    PPC_STORE_U32(area+0x80,dictionary);PPC_STORE_U32(area+0x84,bytes);
    const auto stream=cpu.invoke(0x823F9598,3,1,area+0x80);need(stream,"Original particle stream construction failed");
    EngineCpuCalls load(entry,base);load.registers().lr=0x8271191C;
    const auto request=load.registers().r1.u32+0x60;std::memset(rt.pointer(request,0x18,true),0,0x18);
    PPC_STORE_U32(area+0x90,0x50415254);PPC_STORE_U32(request+4,area+0x90);
    PPC_STORE_U32(request+0x10,stream);PPC_STORE_U32(request+0x14,bytes);
    const auto saved=fullAbi(load.registers());need(load.invoke(0x826F26B0,0,request)&&fullAbi(load.registers())==saved,
        "Original particle dictionary parent loader lost return or ABI");
    const auto plugin=PPC_LOAD_U32(0x82CF0600);
    const uint32_t count=PPC_LOAD_U16(dictionary+4)+PPC_LOAD_U16(dictionary+6),sentinel=dictionary+8*(count+1)+8;
    std::array<uint32_t,2> result{};uint32_t visited=0;
    for(uint32_t link=PPC_LOAD_U32(sentinel);dictionary+link!=sentinel;link=PPC_LOAD_U32(dictionary+link)) {
        need(link&&++visited<256,"Original particle dictionary link cycle");const auto texture=dictionary+link-8;
        const auto name=stringAt(rt,texture+0x10);
        if(name=="simpsons_palette"||name=="fire64bw3") {
            const auto copied=cpu.invoke(0x826F8520,PPC_LOAD_U32(texture+plugin+4));
            need(copied&&rt.engineDriver->itxdTextures().ownsRaster(copied+0x78),"Original particle texture not copied/published");
            result[name=="simpsons_palette"?0:1]=copied+0x78;
        }
    }
    need(result[0]&&result[1]&&result[0]!=result[1],"Distinct original particle rasters missing");
    same(rt,dictionary,before,"Particle loader changed immutable dictionary bytes");return result;
}
uint32_t matrixCalls{};bool watchMatrices{};
void seed(Runtime& rt,const Variant& v,const std::array<uint32_t,2>& textures,bool local,uint16_t ring,uint16_t dualMode) {
    auto* base=rt.base;std::memset(rt.pointer(emitter,0x200,true),0,0x200);
    std::memset(rt.pointer(definition,0x108,true),0,0x108);std::memset(rt.pointer(parameters,0x90,true),0,0x90);
    std::memset(rt.pointer(bucket,0x220,true),0,0x220);
    PPC_STORE_U32(emitter+0x14,parent);PPC_STORE_U32(parent+8,curve);scalar(base,parent+124,1);scalar(base,curve+68,1);
    scalar(base,emitter+0xB8,1);PPC_STORE_U16(emitter+0xC8,8);PPC_STORE_U16(emitter+0xF4,1);PPC_STORE_U16(emitter+0xF6,ring);
    PPC_STORE_U32(emitter+0x118,definition);PPC_STORE_U32(emitter+0x11C,parameters);PPC_STORE_U32(emitter+0x130,bucket);
    PPC_STORE_U32(emitter+0xD0,texture0);PPC_STORE_U32(emitter+0xD4,texture1);
    PPC_STORE_U32(texture0,textures[0]);PPC_STORE_U32(texture1,textures[1]);
    PPC_STORE_U32(texture0+0x50,0x1100);PPC_STORE_U32(texture1+0x50,0x1100);
    PPC_STORE_U32(emitter+0x10,local?4:0);identity(base,emitter+0x60);
    PPC_STORE_U8(definition+0x100,v.type);PPC_STORE_U8(definition+0x104,v.flags);PPC_STORE_U8(definition+0x40,v.mode);
    PPC_STORE_U16(definition+0x42,dualMode);PPC_STORE_U32(definition+0xD0,0x10000080);scalar(base,definition+0xE4,.4f);
    // Bounded finite shader inputs. These are synthetic particle parameters,
    // not a claim to have decoded authored VFX emitter definitions.
    constexpr float rows[9][4]={{0,0,0,0},{0,0,0,0},{0,0,0,0},{255,255,255,255},
        {1,1,1,63.75f},{1,1,1000,0},{1,0,0,1},{0,0,0,255},{1,1,0,0}};
    for(uint32_t r=0;r<9;++r)for(uint32_t lane=0;lane<4;++lane)scalar(base,parameters+16*r+4*lane,rows[r][lane]);
    scalar(base,parameters+128,1);scalar(base,parameters+132,1);
    const auto particle=bucket+32+64*ring;
    scalar(base,particle+8,.5f);scalar(base,particle+12,1);
    scalar(base,particle+16,.5f);scalar(base,particle+20,.125f);scalar(base,particle+28,1);
    scalar(base,particle+32,.5f);scalar(base,particle+40,1);PPC_STORE_U16(particle+54,0xFFFF);
}
std::vector<uint8_t> fileBytes(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary|std::ios::ate);need(bool(in),"Particle CPU evidence file missing");
    const auto size=in.tellg();need(size>0&&size<=0x10000,"Particle evidence file extent differs");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));in.seekg(0);in.read(reinterpret_cast<char*>(bytes.data()),size);
    need(bool(in),"Cannot read particle CPU evidence");return bytes;
}
uint32_t bigWord(const std::vector<uint8_t>& bytes,size_t at) {
    need(at+4<=bytes.size(),"Particle staging word outside evidence");
    return (uint32_t(bytes[at])<<24)|(uint32_t(bytes[at+1])<<16)|(uint32_t(bytes[at+2])<<8)|bytes[at+3];
}
void run(Runtime& rt,const PPCContext& entry,const char* path) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;EngineCpuCalls cpu(entry,base);stage="retail particle byte evidence";evidence(base);
    rt.map(area,0x10000,true,"bounded original particle dispatch fixture");std::memset(rt.pointer(area,0x10000,true),0,0x10000);
    const auto options=cpu.registers().r1.u32+0x60;PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    stage="real catalog and shadow ownership";const auto manager=cpu.invoke(0x8269BF70,0x260,options);
    cpu.invoke(0x826B6F60,manager,PPC_LOAD_U32(0x82D5DA74));
    need(!cpu.invoke(0x827019E8,table,25)&&!cpu.invoke(0x827019E8,0x82CD1448,24),"Original particle catalog registration failed");
    cpu.invoke(0x826B7218,manager);const auto projector=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(table+4*16+4));
    need(projector&&driver.effects().count()==49,"Original shared shadow owner missing");
    const auto shadowCamera=PPC_LOAD_U32(projector+0x5B4);PPC_STORE_U32(area+0x80,0x000000FF);
    need(cpu.invoke(0x823F1B80,shadowCamera,area+0x80,7)==shadowCamera,"Original particle shadow clear failed");
    cpu.invoke(0x82707220,projector,0);
    need(driver.shadowTextures().view(PPC_LOAD_U32(projector+0xF0)).phase==EngineShadowTextures::Phase::Uploaded,
        "Original particle depth copy not uploaded");
    stage="real original ITXD particle resources";const auto textures=loadTextures(rt,entry,path);
    const auto viewport=cpu.invoke(0x8269D788);need(viewport,"Original particle viewport manager missing");
    cpu.registers().f1.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12CC));
    cpu.registers().f2.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D0));
    cpu.registers().f3.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D4));
    cpu.registers().f4.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D8));cpu.invoke(0x8269D608,viewport,0);
    const auto camera=PPC_LOAD_U32(viewport+0x14);
    Restore frameGlobals(rt,0x82DFEA20,0xC0),projectorGlobal(rt,0x82DFEB98,4),shadowMatrix(rt,projector+0x260,64);
    identity(base,0x82DFEA20);identity(base,0x82DFEA60);identity(base,0x82DFEAA0);identity(base,projector+0x260);
    PPC_STORE_U32(0x82DFEB98,projector);
    need(cpu.invoke(0x823F1A18,camera)==camera,"Original particle main camera begin failed");
    const auto bound=driver.cameraBinding();
    bool sampledAlphaOne{};const auto target=driver.color(bound.colorIdentity,sampledAlphaOne);
    need(bool(target),"Particle fixture camera has no owned native color attachment");
    const auto retainedViewport=Graphics::NativeRenderResolutionProbe::baseline(target);
    need(retainedViewport.TopLeftX==0&&retainedViewport.TopLeftY==0&&retainedViewport.Width==1280&&
        retainedViewport.Height==720&&retainedViewport.MinDepth==0&&retainedViewport.MaxDepth==1,
        "Particle fixture baseline viewport extent differs");
    constexpr std::array<std::array<uint32_t,2>,22> states={{{0x28,1},{0x2C,6},{0x30,1},{0x34,0},{0x3C,0},{0x60,0},{0x6C,0},
        {0xAC,0},{0xC0,1},{0xC4,UINT32_MAX},{0xC8,0},{0xCC,0},{0xD0,0},{0xD4,15},{0xE4,0},{0x130,1},
        {0x144,1},{0x148,1},{0x14C,0xFFFF},{0x150,0},{0xD8,0},{0xDC,0}}};
    for(const auto& state:states)driver.directScalar(base,state[0],state[1]);
    rt.frameCaptureDirectory=std::filesystem::path("build/particle-family")/
        ("original-dispatch-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    std::filesystem::create_directories(rt.frameCaptureDirectory);
    uint32_t calls=0;
    const auto call=[&](const Variant& v,bool local,uint16_t ring,uint16_t dualMode) {
        seed(rt,v,textures,local,ring,dualMode);const auto originalDefinition=snapshot(rt,definition,0x108),
            originalParameters=snapshot(rt,parameters,0x90),originalEmitter=snapshot(rt,emitter,0x200);
        matrixCalls=0;watchMatrices=true;EngineCpuCalls pass(entry,base);const auto saved=fullAbi(pass.registers());
        const auto beforeDraws=driver.particles().drawCount();
        pass.invoke(0x82772CA8,emitter);watchMatrices=false;++calls;
        need(fullAbi(pass.registers())==saved,"Retail particle CPU upload lost nonvolatile integer/floating ABI");
        need(driver.particles().drawCount()==beforeDraws+1,"Original particle CPU producer did not complete one native submission");
        need(matrixCalls==uint32_t(local)+((v.flags&4)&&PPC_LOAD_U32(0x82DFEB98)?(local?2u:1u):0u),
            "Particle original matrix helper route differs");
        same(rt,definition,originalDefinition,"Particle upload mutated immutable definition");
        same(rt,parameters,originalParameters,"Particle upload mutated immutable parameters");
        same(rt,emitter,originalEmitter,"Particle upload changed emitter/ring owner");
        using S=Graphics::ScalarState;const auto& effective=driver.effectiveState();
        need(effective.scalar(S::ExpandedBlend0)==0&&effective.scalar(S::BlendEnable)==0&&
            effective.scalar(S::AlphaTest)==0&&effective.scalar(S::DepthEnable)==1&&effective.scalar(S::DepthWrite)==1,
            "Particle original suffix failed to restore direct states");
        const auto color=driver.readbackColor(bound.colorIdentity),depth=driver.readbackDepth(bound.depthIdentity);
        need(color.size()==size_t(1280)*720*4&&depth.size()==size_t(1280)*720*8,"Particle GPU attachments lost their original extent");
        need(driver.cameraBinding().camera==camera,"Particle submission leaked its original camera owner");
    };
    stage="all eight independent original particle shader combinations";
    for(uint32_t i=0;i<variants.size();++i) {
        const auto& v=variants[i];seed(rt,v,textures,false,0,3);
        // Poison the selected record; all other family records stay live.
        // A wrong VS/PS classification would fail these negative assertions.
        for(const auto registry:{v.vsRegistry,v.psRegistry}) {
            Restore saved(rt,registry,4);PPC_STORE_U32(registry,0);
            const auto before=driver.readbackColor(bound.colorIdentity);const auto beforeDraws=driver.particles().drawCount();
            rejects([&]{EngineCpuCalls bad(entry,base);bad.invoke(0x82772CA8,emitter);},"Particle accepted a mismatched selected retail shader record");
            need(driver.readbackColor(bound.colorIdentity)==before,"Rejected particle variant mutated the color attachment");
            need(driver.particles().drawCount()==beforeDraws,"Rejected particle variant submitted a native draw");
        }
        call(v,(i&1)!=0,uint16_t(i&7),3);
    }
    stage="original nonshader definition flags and default VS branch";
    auto flagged=variants[0];flagged.type=7;flagged.mode=2;flagged.flags=0x40;
    call(flagged,false,7,2);need(PPC_LOAD_U16(bucket+32+64*7+52)==4,"Original UV-flip flag mutation was skipped");
    stage="both original dual coordinate operands";call(variants[1],false,0,2);call(variants[3],true,7,2);
    stage="original requested projection with no live projector";
    {Restore saved(rt,0x82DFEB98,4);PPC_STORE_U32(0x82DFEB98,0);
        for(const auto index:{0u,1u,4u,5u}) {
            auto fallback=variants[index];fallback.flags=4;seed(rt,fallback,textures,false,0,3);
            for(const auto registry:{fallback.vsRegistry,fallback.psRegistry}) {
                Restore shader(rt,registry,4);PPC_STORE_U32(registry,0);
                const auto before=driver.readbackColor(bound.colorIdentity);const auto beforeDraws=driver.particles().drawCount();
                rejects([&]{EngineCpuCalls bad(entry,base);bad.invoke(0x82772CA8,emitter);},
                    "Null-projector fallback accepted a mismatched selected retail shader record");
                need(driver.readbackColor(bound.colorIdentity)==before&&driver.particles().drawCount()==beforeDraws,
                    "Rejected null-projector fallback mutated native output");
            }
            call(fallback,(index&1)!=0,uint16_t(index),3);
        }
    }
    need(calls==15,"Original particle family upload case count differs");
    stage="consumed inherited second binding stays unqualified";
    seed(rt,variants[1],textures,false,0,1);PPC_STORE_U32(emitter+0xD4,0);
    rejects([&]{EngineCpuCalls bad(entry,base);bad.invoke(0x82772CA8,emitter);},"Dual particle silently inherited an unowned second texture");
    // First ordinary and dual captures were produced by the original upload,
    // not by the test's shader expectations. The dual operand has bit0 set
    // while another bit is also set, so whole-word truthiness is insufficient.
    stage="captured original staging and restored dual constant";
    const auto staging=fileBytes(rt.frameCaptureDirectory/"particle-native/original-staging.bin");
    need(staging.size()==64&&bigWord(staging,8)==0x3F000000&&bigWord(staging,12)==0x3F800000&&
        bigWord(staging,16)==0x3F000000&&bigWord(staging,20)==0x3E000000&&bigWord(staging,48)==0x3F000000&&
        bigWord(staging,56)==0x3F800000&&bigWord(staging,60)==0x80F8F8F8,"Retail CPU ring upload/packed color differs");
    const auto constants=fileBytes(rt.frameCaptureDirectory/"particle-dual-native/constants.bin");
    need(constants.size()==sizeof(Graphics::ParticleConstants),"Captured particle constant bank extent differs");
    std::array<float,4> c10{};std::memcpy(c10.data(),constants.data()+10*16,16);
    need(c10==std::array<float,4>{0,0,1,0},"Original dual coordinate bit did not reach native c10.z");
    rt.frameCaptureDirectory.clear();driver.directScalar(base,uint32_t(Graphics::ScalarState::ExpandedBlend0),0);
    need(cpu.invoke(0x823F1A08,camera)==camera,"Original particle main camera end failed");
}
}
PPC_FUNC(sub_827B8328) {
    if(watchMatrices)++matrixCalls;
    __imp__sub_827B8328(ctx,base);
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==3,"Original image and loc_split4 ITXD dictionary required");
        Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original particle startup boundary missing");run(rt,entry,argv[2]);
        std::printf("PASS original particle dispatch: %zu checks; 8 shader pairs, 15 real CPU uploads including 4 null-projector fallbacks, selected registry rejects, owned ITXD/shadow resources, dual operand, UV flag mutation and full ABI; isolated fixture, no gameplay\n",checks);return 0;
    }catch(const std::exception& error){watchMatrices=false;std::fprintf(stderr,"FAIL original particle dispatch: %zu checks stage=%s: %s\n",checks,stage,error.what());return 1;}
}
