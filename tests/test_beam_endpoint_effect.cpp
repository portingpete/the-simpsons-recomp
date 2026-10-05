// Execute the complete original 8286D638 endpoint effect. Only its qualified
// graphics boundaries are native; camera-facing widths, UVs, endpoint alpha,
// optional color callback, visibility branch, and ABI remain original code.
#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include "renderer/engine_state.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace {
using namespace Simpsons;
size_t checks{};const char* stage="startup";
void need(bool value,const char* why){++checks;if(!value)throw Failure(why);}
struct Observed{};
struct Abi {
    uint64_t sp,lr;std::array<uint64_t,18> gpr,fpr;
    bool operator==(const Abi&)const=default;
};
Abi abi(const PPCContext& c) {
    return {c.r1.u64,c.lr,{c.r14.u64,c.r15.u64,c.r16.u64,c.r17.u64,c.r18.u64,c.r19.u64,c.r20.u64,c.r21.u64,c.r22.u64,
        c.r23.u64,c.r24.u64,c.r25.u64,c.r26.u64,c.r27.u64,c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64},
        {c.f14.u64,c.f15.u64,c.f16.u64,c.f17.u64,c.f18.u64,c.f19.u64,c.f20.u64,c.f21.u64,c.f22.u64,
        c.f23.u64,c.f24.u64,c.f25.u64,c.f26.u64,c.f27.u64,c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64}};
}
void seedAbi(PPCContext& c) {
    const std::array gpr={&c.r14,&c.r15,&c.r16,&c.r17,&c.r18,&c.r19,&c.r20,&c.r21,&c.r22,
        &c.r23,&c.r24,&c.r25,&c.r26,&c.r27,&c.r28,&c.r29,&c.r30,&c.r31};
    const std::array fpr={&c.f14,&c.f15,&c.f16,&c.f17,&c.f18,&c.f19,&c.f20,&c.f21,&c.f22,
        &c.f23,&c.f24,&c.f25,&c.f26,&c.f27,&c.f28,&c.f29,&c.f30,&c.f31};
    for(size_t i=0;i<gpr.size();++i){gpr[i]->u64=0xA751000012340000ull+i;fpr[i]->f64=double(i)+.125;}
}
std::vector<uint8_t> snapshot(Runtime& rt,uint32_t at,uint32_t size) {
    const auto* p=rt.pointer(at,size,false);return {p,p+size};
}
struct Restore {
    uint8_t* destination;std::vector<uint8_t> bytes;
    Restore(Runtime& rt,uint32_t at,uint32_t size):destination(rt.pointer(at,size,true)),bytes(destination,destination+size){}
    ~Restore(){std::memcpy(destination,bytes.data(),bytes.size());}
};
void storeFloat(uint8_t* base,uint32_t at,float value){PPC_STORE_U32(at,std::bit_cast<uint32_t>(value));}
void identity(uint8_t* base,uint32_t at) {
    for(uint32_t i=0;i<16;++i)storeFloat(base,at+4*i,i%5==0?1.0f:0.0f);
}
uint32_t pixel(const std::vector<uint8_t>& bytes,size_t offset) {
    uint32_t value{};std::memcpy(&value,bytes.data()+offset,4);return value;
}
void exercise(const char* image) {
    Runtime rt;rt.load(image);PPCContext startup{};rt.initialize(startup);const auto entry=startup;auto* base=rt.base;
    rt.audioBoundaryObserver=[](uint32_t pc,PPCContext&,uint8_t*){if(pc==0x828166FC)throw Observed{};};
    bool observed=false;try{runOriginal(startup,base);}catch(const Observed&){observed=true;}
    rt.audioBoundaryObserver={};need(observed,"Original startup checkpoint missing");
    auto& driver=*rt.engineDriver;EngineCpuCalls cpu(entry,base);auto& c=cpu.registers();
    constexpr uint32_t area=0x60000,owner=area,definition=area+0x200,system=area+0x300,
        eye=area+0x400,eyeFrame=area+0x500,tint=area+0x600,tintVtable=area+0x700,tintFrame=area+0x800,
        black=area+0x900;
    rt.map(area,0x1000,true,"original endpoint beam CPU inputs");std::memset(rt.pointer(area,0x1000,true),0,0x1000);
    const auto camera=PPC_LOAD_U32(0x82E07248),texture=PPC_LOAD_U32(0x82E071E8);
    const auto refs=PPC_LOAD_U32(texture+0x54),row=PPC_LOAD_U32(0x82DFEB20),ring=PPC_LOAD_U32(row+4);
    Restore eyeGlobal(rt,0x82DFF098,4),matrix(rt,0x82DFEAE0,64),cursor(rt,row,4);
    // Startup allocates the ring before the first game frame resets its cursor.
    PPC_STORE_U32(row,ring);
    PPC_STORE_U32(0x82DFF098,eye);PPC_STORE_U32(eye+4,eyeFrame);PPC_STORE_U32(eyeFrame+0xA0,eyeFrame);
    identity(base,eyeFrame+0x10);identity(base,0x82DFEAE0);
    PPC_STORE_U32(owner,0x8217FFD8);PPC_STORE_U32(owner+0x14,system);PPC_STORE_U32(owner+0xA0,texture);
    PPC_STORE_U32(owner+0xB0,definition);PPC_STORE_U8(owner+0xEC,2);PPC_STORE_U8(owner+0xED,3);
    storeFloat(base,owner+0xF0,.125f);storeFloat(base,owner+0xF4,.25f);
    storeFloat(base,owner+0x100,-.5f);storeFloat(base,owner+0x108,.5f);storeFloat(base,owner+0x10C,1);
    storeFloat(base,owner+0x110,.5f);storeFloat(base,owner+0x118,.5f);storeFloat(base,owner+0x11C,1);
    for(uint32_t lane=0;lane<3;++lane)storeFloat(base,definition+0x14+4*lane,.125f);
    // A fixture virtual object calls the real original camera-position helper
    // as its four-float color method. No host implementation supplies its result.
    PPC_STORE_U32(tint,tintVtable);PPC_STORE_U32(tintVtable+12,0x82757C18);PPC_STORE_U32(tint+4,tintFrame);
    PPC_STORE_U32(tintFrame+0xA0,tintFrame);identity(base,tintFrame+0x10);
    storeFloat(base,tintFrame+0x40,.5f);storeFloat(base,tintFrame+0x44,.25f);storeFloat(base,tintFrame+0x48,.75f);
    storeFloat(base,black+12,1);
    need(cpu.invoke(0x823F1A18,camera)==camera,"Original camera begin failed");
    const auto bound=driver.cameraBinding();
    using S=Graphics::ScalarState;using T=Graphics::SamplerState;
    const std::array<std::pair<S,uint32_t>,18> states={{{S::DepthCompare,7},{S::StencilEnable,0},{S::ScissorEnable,0},
        {S::Fill,0},{S::ClipPlaneEnable,0},{S::AlphaToMask,0},{S::DepthBias,0},{S::SlopeBias,0},
        {S::ColorMask0,15},{S::TessellationMode,0},{S::ViewportEnable,1},{S::HalfPixelOffset,1},
        {S::MultisampleMask,UINT32_MAX},{S::BlendEnable,0},{S::GuardBandX,0x3F800000},{S::GuardBandY,0x3F800000},
        {S::PrimitiveResetEnable,1},{S::PrimitiveResetIndex,0xFFFF}}};
    for(const auto& [id,value]:states)driver.directScalar(base,uint32_t(id),value);
    for(const auto& [id,value]:std::array<std::pair<T,uint32_t>,4>{{{T::LodBiasBits,0},{T::MinimumMip,0},{T::MaximumMip,13},{T::MaximumAnisotropy,1}}})
        driver.directSampler(base,0,uint32_t(id),value);
    auto clear=[&] {
        c.r8.u32=black;c.r9.u32=3;c.r10.u32=0;c.f1.f64=0;c.f2.f64=0;c.f3.f64=1280;c.f4.f64=720;
        c.f5.f64=0;c.f6.f64=0;c.f7.f64=1;c.f8.f64=1;cpu.invoke(0x82756480,camera);
    };
    auto invoke=[&] {
        seedAbi(c);const auto before=abi(c);cpu.invoke(0x8286D638,owner);
        need(abi(c)==before,"Whole endpoint effect changed nonvolatile GPR/FPR, SP or LR");
    };
    stage="original visibility early-out";clear();
    PPC_STORE_U32(owner+0x10,1);PPC_STORE_U32(owner+0xA0,0);PPC_STORE_U32(owner+0xB0,0);
    const auto beforeEarly=driver.readbackColor(bound.colorIdentity);
    const auto earlyCount=driver.immediateDrawCount();const auto earlyCursor=PPC_LOAD_U32(row);
    const auto stateBefore=driver.effectiveState();invoke();
    need(driver.immediateDrawCount()==earlyCount&&PPC_LOAD_U32(row)==earlyCursor&&
         driver.readbackColor(bound.colorIdentity)==beforeEarly,"Hidden original endpoint effect reserved or submitted geometry");
    for(const auto& field:Graphics::scalarStateEvidence())need(driver.effectiveState().scalar(field.id)==stateBefore.scalar(field.id),"Hidden effect changed scalar state");
    PPC_STORE_U32(owner+0x10,0);PPC_STORE_U32(owner+0xA0,texture);PPC_STORE_U32(owner+0xB0,definition);
    stage="unknown endpoint owner rejection";
    const auto invalidContext=c;const auto invalidDepth=driver.readbackDepth(bound.depthIdentity);
    PPC_STORE_U32(owner,0x8217FFD0);bool rejected=false;
    try{invoke();}catch(const Failure& error){
        if(!std::strstr(error.what(),"Endpoint beam lost its original object"))throw;
        rejected=true;
    }
    c=invalidContext;PPC_STORE_U32(owner,0x8217FFD8);
    need(rejected&&driver.immediateDrawCount()==earlyCount&&PPC_LOAD_U32(row)==earlyCursor&&
         driver.readbackColor(bound.colorIdentity)==beforeEarly&&driver.readbackDepth(bound.depthIdentity)==invalidDepth,
         "Unknown endpoint owner submitted or reserved native geometry");
    std::vector<uint8_t> plain;
    for(bool tinted:{false,true}) {
        stage=tinted?"original optional color callback":"original endpoint widths, alpha and additive draw";
        clear();PPC_STORE_U32(system+0x10,tinted?tint:0);
        const auto input=snapshot(rt,area,0x1000),depthBefore=driver.readbackDepth(bound.depthIdentity);
        const auto count=driver.immediateDrawCount(),clears=driver.cameraClearCount(),copies=driver.cameraCopyCount();
        std::vector<uint8_t> first;
        for(uint32_t pass=0;pass<2;++pass) {
            const auto source=ring+128*pass;PPC_STORE_U32(row,source);
            for(uint32_t vertex=0;vertex<4;++vertex){PPC_STORE_U32(source+32*vertex+24,0x7FC01234);PPC_STORE_U32(source+32*vertex+28,0x7F800000);}
            invoke();need(driver.immediateDrawCount()==count+pass+1&&PPC_LOAD_U32(row)==source+128,"Original endpoint effect missed or repeated its four-vertex draw");
            constexpr std::array<std::array<float,6>,4> expected={{{-.5f,.125f,.5f,2,0,0},{-.5f,-.125f,.5f,2,1,0},
                {.5f,.25f,.5f,3,0,1},{.5f,-.25f,.5f,3,1,1}}};
            for(uint32_t vertex=0;vertex<4;++vertex) {
                for(uint32_t lane=0;lane<6;++lane)need(std::abs(std::bit_cast<float>(PPC_LOAD_U32(source+32*vertex+4*lane))-expected[vertex][lane])<.000003f,
                    "Original endpoint position, width, UV or unnormalized alpha differs");
                need(PPC_LOAD_U32(source+32*vertex+24)==0x7FC01234&&PPC_LOAD_U32(source+32*vertex+28)==0x7F800000,"Native draw changed unwritten guest UV lanes");
            }
            const auto output=driver.readbackColor(bound.colorIdentity);
            if(!pass)first=output;
            else {
                bool contribution=false;
                for(size_t i=0;i<output.size();i+=4)for(uint32_t shift:{0u,10u,20u}) {
                    const auto a=(pixel(first,i)>>shift)&1023,b=(pixel(output,i)>>shift)&1023;
                    if(a>3&&a<200){need(b+1>=2*a&&b<=2*a+1,"Repeated endpoint draw lost its direct additive equation");contribution=true;}
                }
                need(contribution,"Endpoint beam raster contained no measurable source color");
            }
            need(driver.readbackDepth(bound.depthIdentity)==depthBefore,"Endpoint effect changed depth/stencil storage");
            need(driver.effectiveState().scalar(S::DepthEnable)==1&&driver.effectiveState().scalar(S::DepthWrite)==1&&
                 !driver.effectiveState().scalar(S::BlendEnable)&&!driver.effectiveState().scalar(S::AlphaTest),"Original immediate epilogue did not restore scalar state");
        }
        if(!tinted)plain=first;
        else {
            bool checkedTint=false;constexpr std::array<float,3> tintValues={.5f,.25f,.75f};
            for(size_t i=0;i<first.size();i+=4)for(uint32_t lane=0;lane<3;++lane) {
                const auto a=(pixel(plain,i)>>(lane*10))&1023,b=(pixel(first,i)>>(lane*10))&1023;
                if(a>10&&a<200){need(std::abs(float(b)-float(a)*tintValues[lane])<=1.5f,"Original optional color callback was omitted or applied twice");checkedTint=true;}
            }
            need(checkedTint,"Tint regression found no measurable source contribution");
        }
        need(snapshot(rt,area,0x1000)==input,"Endpoint effect changed its source owner, endpoints or color inputs");
        need(PPC_LOAD_U32(texture+0x54)==refs&&driver.cameraBinding().camera==camera&&
             driver.cameraClearCount()==clears&&driver.cameraCopyCount()==copies,"Endpoint effect changed texture/camera lifetime");
        need(!PPC_LOAD_U32(0x82D0CAF8),"Endpoint effect fabricated a legacy graphics device");
    }
    const auto originalVertices=snapshot(rt,ring,128);PPC_STORE_U32(system+0x10,0);
    for(bool nullTexture:{true,false}) {
        stage=nullTexture?"original null endpoint texture":"original endpoint null-to-valid texture transition";
        PPC_STORE_U32(owner+0xA0,nullTexture?0:texture);clear();PPC_STORE_U32(row,ring);
        std::memset(rt.pointer(ring,128,true),0xFF,128);
        for(uint32_t vertex=0;vertex<4;++vertex){PPC_STORE_U32(ring+32*vertex+24,0x7FC01234);PPC_STORE_U32(ring+32*vertex+28,0x7F800000);}
        const auto input=snapshot(rt,area,0x1000),colorBefore=driver.readbackColor(bound.colorIdentity),depthBefore=driver.readbackDepth(bound.depthIdentity);
        const auto count=driver.immediateDrawCount(),clears=driver.cameraClearCount(),copies=driver.cameraCopyCount();
        invoke();
        need(driver.immediateDrawCount()==count+1&&PPC_LOAD_U32(row)==ring+128,
             "Endpoint texture transition skipped or repeated its original four-vertex draw");
        need(snapshot(rt,ring,128)==originalVertices,"Endpoint texture transition changed original geometry or unused UV lanes");
        need(driver.readbackColor(bound.colorIdentity)==(nullTexture?colorBefore:plain),
             "Endpoint null texture retained old pixels or failed to restore a valid texture");
        need(driver.readbackDepth(bound.depthIdentity)==depthBefore,"Endpoint texture transition changed depth/stencil storage");
        need(snapshot(rt,area,0x1000)==input,"Endpoint texture transition changed original inputs");
        need(PPC_LOAD_U32(texture+0x54)==refs&&driver.cameraBinding().camera==camera&&
             driver.cameraClearCount()==clears&&driver.cameraCopyCount()==copies,"Endpoint texture transition changed texture/camera lifetime");
        need(driver.effectiveState().scalar(S::DepthEnable)==1&&driver.effectiveState().scalar(S::DepthWrite)==1&&
             !driver.effectiveState().scalar(S::BlendEnable)&&!driver.effectiveState().scalar(S::AlphaTest),
             "Original endpoint texture transition left immediate state active");
    }
    stage="original camera retirement";need(cpu.invoke(0x823F1A08,camera)==camera,"Original camera end failed");
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try{need(argc==2,"Original image required");exercise(argv[1]);
        std::printf("PASS original endpoint beam: %zu checks; whole CPU path, visibility, widths/UV/alpha, optional color, additive output, depth, ABI and lifetime\n",checks);return 0;}
    catch(const std::exception& error){std::fprintf(stderr,"FAIL original endpoint beam: %zu checks stage=%s: %s\n",checks,stage,error.what());return 1;}
}
