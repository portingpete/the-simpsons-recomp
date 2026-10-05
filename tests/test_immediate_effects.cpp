// Full original beam owner/instance/segment loops using the original startup
// resources. The observer always forwards the original segment function.
#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include "renderer/engine_state.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

extern "C" void __imp__sub_8277B040(PPCContext&,uint8_t*);
namespace {
using namespace Simpsons;
size_t checks{};const char* stage="startup";
void need(bool value,const char* why){++checks;if(!value)throw Failure(why);}
template<class F>void rejects(F&& f,const char* why){try{f();}catch(const Failure&){++checks;return;}need(false,why);}
struct Observed{};
struct Abi {
    uint64_t sp,lr;std::array<uint64_t,18> gpr,fpr;
    bool operator==(const Abi&)const=default;
};
Abi abi(const PPCContext& c){return {c.r1.u64,c.lr,
    {c.r14.u64,c.r15.u64,c.r16.u64,c.r17.u64,c.r18.u64,c.r19.u64,c.r20.u64,c.r21.u64,c.r22.u64,c.r23.u64,c.r24.u64,c.r25.u64,c.r26.u64,c.r27.u64,c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64},
    {c.f14.u64,c.f15.u64,c.f16.u64,c.f17.u64,c.f18.u64,c.f19.u64,c.f20.u64,c.f21.u64,c.f22.u64,c.f23.u64,c.f24.u64,c.f25.u64,c.f26.u64,c.f27.u64,c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64}};}
void seed(PPCContext& c){
    const std::array g={&c.r14,&c.r15,&c.r16,&c.r17,&c.r18,&c.r19,&c.r20,&c.r21,&c.r22,&c.r23,&c.r24,&c.r25,&c.r26,&c.r27,&c.r28,&c.r29,&c.r30,&c.r31};
    const std::array f={&c.f14,&c.f15,&c.f16,&c.f17,&c.f18,&c.f19,&c.f20,&c.f21,&c.f22,&c.f23,&c.f24,&c.f25,&c.f26,&c.f27,&c.f28,&c.f29,&c.f30,&c.f31};
    for(size_t i=0;i<g.size();++i){g[i]->u64=0xAA11000000000100ull+i;f[i]->f64=double(i)+.125;}
}
std::function<void(PPCContext&,uint8_t*,bool)> observer;
struct Observation {
    explicit Observation(decltype(observer) f){need(!observer,"Nested beam observer");observer=std::move(f);}
    ~Observation(){observer={};}
};
struct Restore {
    uint8_t* destination;std::vector<uint8_t> bytes;
    Restore(Runtime& rt,uint32_t address,uint32_t size):destination(rt.pointer(address,size,true)),bytes(destination,destination+size){}
    ~Restore(){std::memcpy(destination,bytes.data(),bytes.size());}
};
std::vector<uint8_t> snapshot(Runtime& rt,uint32_t address,uint32_t bytes){const auto* p=rt.pointer(address,bytes,false);return {p,p+bytes};}
void same(Runtime& rt,uint32_t address,const std::vector<uint8_t>& before,const char* why){need(!std::memcmp(rt.pointer(address,uint32_t(before.size()),false),before.data(),before.size()),why);}
void put(uint8_t* base,uint32_t address,float value){PPC_STORE_U32(address,std::bit_cast<uint32_t>(value));}
float get(uint8_t* base,uint32_t address){return std::bit_cast<float>(PPC_LOAD_U32(address));}
using Matrix=std::array<float,16>;
Matrix translated(float x,float y){return {1,0,0,0,0,1,0,0,0,0,1,0,x,y,0,1};}
void matrix(uint8_t* base,uint32_t address,const Matrix& m){for(uint32_t i=0;i<16;++i)put(base,address+4*i,m[i]);}
std::array<uint32_t,6> caches(uint8_t* base){return {PPC_LOAD_U32(0x82CD1A68),PPC_LOAD_U32(0x82CD1A6C),PPC_LOAD_U32(0x82CD1A70),PPC_LOAD_U32(0x82D0CAB0),PPC_LOAD_U32(0x82D0CAB4),PPC_LOAD_U32(0x82D0CAB8)};}
constexpr uint32_t area=0x58000,owner=area,definition=area+0x200,items=area+0x400,points=area+0x800,stateSave=area+0x1000;
void item(uint8_t* base,uint32_t at,uint32_t source,uint32_t count,float alpha=.5f){
    matrix(base,at,translated(.25f,.125f));PPC_STORE_U32(at+0x60,source);PPC_STORE_U32(at+0x68,count);
    put(base,at+0x6C,.5f);put(base,at+0x70,alpha);PPC_STORE_U32(at+0x78,0);
}
void point(uint8_t* base,uint32_t index,float x,float y=0){put(base,points+16*index,x);put(base,points+16*index+4,y);put(base,points+16*index+8,.5f);PPC_STORE_U32(points+16*index+12,index);}
void sameDepth(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b){
    need(a.size()==size_t(1280)*720*8&&a.size()==b.size(),"Beam depth extent differs");
    bool equal=true;for(size_t i=0;i<a.size();i+=8)equal&=!std::memcmp(a.data()+i,b.data()+i,5);
    need(equal,"Beam changed depth or stencil");
}
struct Pixels {uint64_t red{};uint32_t changed{},minX=1280,maxX{},minY=720,maxY{};};
Pixels pixels(const std::vector<uint8_t>& bytes){
    need(bytes.size()==size_t(1280)*720*4,"Beam color extent differs");Pixels result;bool redOnly=true;
    for(uint32_t y=0;y<720;++y)for(uint32_t x=0;x<1280;++x){uint32_t word{};std::memcpy(&word,bytes.data()+4*(1280*y+x),4);
        redOnly&=!(word&0x3FFFFC00);const auto red=word&1023;result.red+=red;
        if(red){++result.changed;result.minX=std::min(result.minX,x);result.maxX=std::max(result.maxX,x);result.minY=std::min(result.minY,y);result.maxY=std::max(result.maxY,y);}}
    need(redOnly,"Beam ignored original RGB modulation");return result;
}
void exercise(Runtime& rt,const PPCContext& entry){
    auto* base=rt.base;auto& d=*rt.engineDriver;EngineCpuCalls cpu(entry,base);auto& c=cpu.registers();seed(c);
    rt.map(area,0x2000,true,"original beam owner fixture");std::memset(rt.pointer(area,0x2000,true),0,0x2000);
    const auto camera=PPC_LOAD_U32(0x82E07248),frame=PPC_LOAD_U32(camera+4),texture=PPC_LOAD_U32(0x82E071E8);
    const auto row=PPC_LOAD_U32(0x82DFEB20),start=PPC_LOAD_U32(row+4),refs=PPC_LOAD_U32(texture+0x54);
    Restore cameraGlobal(rt,0x82DFF098,4),projection(rt,0x82DFEAA0,64),combinedProjection(rt,0x82DFEAE0,64),publishedMatrix(rt,0x82DFF160,4),
        cameraFrame(rt,frame+0x10,64),ring(rt,row,4),vertices(rt,start,0x1000);
    PPC_STORE_U32(owner,0x8215895C);PPC_STORE_U32(owner+0xA4,definition);PPC_STORE_U32(owner+0xA8,texture);
    PPC_STORE_U32(owner+0xC0,items);PPC_STORE_U32(owner+0xC8,1);put(base,owner+0xD4,.5f);
    put(base,definition+0x74,.25f);put(base,definition+0x78,0);put(base,definition+0x7C,0);put(base,definition+0x80,.125f);
    item(base,items,points,2);point(base,0,-.75f);point(base,1,.75f);
    cpu.invoke(0x823F1A18,camera);cpu.invoke(0x826B09A0,stateSave,1);
    need(PPC_LOAD_U32(frame+0xA0)==frame,"Beam fixture camera is not an original root frame");
    matrix(base,frame+0x10,translated(.125f,-.125f));PPC_STORE_U32(0x82DFF098,camera);
    matrix(base,0x82DFEAA0,{.75f,0,0,0,0,.5f,0,0,0,0,1,0,-.125f,-.125f,0,1});
    matrix(base,0x82DFEAE0,translated(4,4)); // A different family uses this combined matrix.
    using S=Graphics::ScalarState;
    constexpr std::array<std::array<uint32_t,2>,23> states={{{0x28,1},{0x2C,7},{0x30,0},{0x34,0},{0x3C,0},{0x60,0},{0x6C,0},
        {0xAC,0},{0xC0,1},{0xC4,UINT32_MAX},{0xC8,0},{0xCC,0},{0xD0,0},{0xD4,15},{0xE4,0},{0x130,1},
        {0x144,1},{0x148,1},{0x14C,0xFFFF},{0x150,0},{0xD8,0},{0xDC,0},{0x134,0}}};
    for(const auto& state:states)d.directScalar(base,state[0],state[1]);
    d.directScalar(base,0x158,0x3F800000);d.directScalar(base,0x15C,0x3F800000);
    // Screen clear uses the exact original CPU rectangle and known black output.
    auto clear=[&]{put(base,area+0xF00,0);put(base,area+0xF04,0);put(base,area+0xF08,0);put(base,area+0xF0C,1);
        c.r8.u32=area+0xF00;c.r9.u32=3;c.r10.u32=0;c.f1.f64=c.f2.f64=0;c.f3.f64=1280;c.f4.f64=720;
        c.f5.f64=c.f6.f64=0;c.f7.f64=c.f8.f64=1;cpu.invoke(0x82756480,camera);d.directScalar(base,uint32_t(S::DepthCompare),7);};
    const auto binding=d.cameraBinding();clear();const auto black=d.readbackColor(binding.colorIdentity),depth=d.readbackDepth(binding.depthIdentity);
    auto invoke=[&](bool ownerLoop){const auto before=abi(c);if(ownerLoop)cpu.invoke(0x8277B450,owner);else cpu.invoke(0x8277B230,owner,items);
        need(abi(c)==before,"Original beam loop changed nonvolatile ABI");};
    stage="original CPU early exits";
    const auto retiredPoints=rt.allocatePhysical(0,4096,PAGE_READWRITE,0,UINT32_MAX,4096);
    need(retiredPoints!=0,"Faded beam released-points fixture allocation failed");rt.freePhysical(retiredPoints);
    for(unsigned which=0;which<7;++which){
        item(base,items,points,which<2?which:2);if(which==2)PPC_STORE_U32(items+0x78,1);if(which==3)put(base,items+0x70,0);
        if(which>=5){put(base,items+0x70,0);PPC_STORE_U32(items+0x60,which==5?0:retiredPoints);PPC_STORE_U32(owner+0xA4,0);PPC_STORE_U32(owner+0xA8,0);}
        const auto sourceBefore=snapshot(rt,owner,0xC00);
        const auto priorItems=PPC_LOAD_U32(owner+0xC8);if(which==4)PPC_STORE_U32(owner+0xC8,0);
        const auto draws=d.immediateDrawCount();const auto cursor=PPC_LOAD_U32(row);const auto beforeCaches=caches(base);const auto beforeState=d.effectiveState();
        invoke(which==4);need(d.immediateDrawCount()==draws&&PPC_LOAD_U32(row)==cursor&&caches(base)==beforeCaches,"Empty/hidden beam submitted or changed caches");
        for(const auto& field:Graphics::scalarStateEvidence())need(d.effectiveState().scalar(field.id)==beforeState.scalar(field.id),"Empty/hidden beam changed effective state");
        need(d.readbackColor(binding.colorIdentity)==black,"Empty/hidden beam changed color");PPC_STORE_U32(owner+0xC8,priorItems);
        same(rt,owner,sourceBefore,"Empty/faded beam changed original owner or point metadata");sameDepth(d.readbackDepth(binding.depthIdentity),depth);
        PPC_STORE_U32(owner+0xA4,definition);PPC_STORE_U32(owner+0xA8,texture);
    }
    auto run=[&](uint32_t count,float alpha,bool ownerLoop,bool probeCleanup){
        clear();item(base,items,points,count,alpha);PPC_STORE_U32(row,start);std::memset(rt.pointer(start,0x1000,true),0xFF,0x1000);
        const auto input=snapshot(rt,owner,0xC00);const auto beforeDraws=d.immediateDrawCount();uint32_t calls=0;bool cleanupProbed=false;
        Observation observation([&](PPCContext& segment,uint8_t* memory,bool after){
            need(memory==base&&uint32_t(segment.lr)==0x8277B424,"Beam did not use the original parent-to-segment call");
            if(!after){need(segment.r3.u32==owner&&segment.r4.u32==items&&segment.r5.u32==calls,"Original beam segment arguments/order differ");++calls;}
            else if(probeCleanup&&!cleanupProbed){const auto saved=segment;segment.r3.u32=segment.r1.u32+0x58;segment.lr=0x8277B440;
                rejects([&]{d.beamOperation(segment,base,0x82751DA0);},"Early beam cleanup accepted incomplete loop");segment=saved;cleanupProbed=true;}
        });
        invoke(ownerLoop);need(calls==count-1&&d.immediateDrawCount()==beforeDraws+count-1&&PPC_LOAD_U32(row)==start+128*(count-1),"Whole beam did not submit exactly one quad per segment");
        need(!probeCleanup||cleanupProbed,"Early cleanup guard was not exercised");
        // Independent scalar expectations for model translation minus camera
        // translation, then the 2D perpendicular to each transformed segment.
        for(uint32_t segment=0;segment<count-1;++segment){const float x0=get(base,points+16*segment)+.125f,y0=get(base,points+16*segment+4)+.25f;
            const float x1=get(base,points+16*(segment+1))+.125f,y1=get(base,points+16*(segment+1)+4)+.25f;
            const float dx=x1-x0,dy=y1-y0,lengthSquared=dx*dx+dy*dy,length=std::sqrt(lengthSquared),scale=lengthSquared>.01f?.125f/length:0;
            for(uint32_t vertex=0;vertex<4;++vertex){const bool end=vertex>=2,negative=(vertex&1)!=0;
                const float sign=negative?-1.f:1.f;const std::array<float,6> expected={(end?x1:x0)-sign*dy*scale,(end?y1:y0)+sign*dx*scale,.5f,1,
                    .5f*float(segment+(negative?0:1)),end?1.f:0.f};const uint32_t at=start+128*segment+32*vertex;
                for(uint32_t lane=0;lane<6;++lane){const auto actual=get(base,at+4*lane);
                    if(!(std::abs(actual-expected[lane])<.000001f))std::fprintf(stderr,"Beam vertex mismatch count=%u segment=%u vertex=%u lane=%u address=%08X actual=%.9g expected=%.9g\n",count,segment,vertex,lane,at+4*lane,actual,expected[lane]);
                    need(std::abs(actual-expected[lane])<.000001f,"Original beam transformed position/width/alpha/UV differs");}
                need(PPC_LOAD_U32(at+24)==UINT32_MAX&&PPC_LOAD_U32(at+28)==UINT32_MAX,"Beam changed unwritten mode-zero UV lanes");}}
        same(rt,owner,input,"Beam changed its original owner/definition/point inputs");sameDepth(d.readbackDepth(binding.depthIdentity),depth);
        need(!d.effectiveState().scalar(S::AlphaTest)&&d.effectiveState().scalar(S::DepthEnable)==1&&d.effectiveState().scalar(S::DepthWrite)==1&&
            !d.effectiveState().scalar(S::BlendEnable),"Original beam cleanup left direct state active");
        need(PPC_LOAD_U32(texture+0x54)==refs&&!PPC_LOAD_U32(0x82D0CAF8)&&d.cameraBinding().camera==camera,"Beam changed texture references, camera, or legacy device ownership");
        return d.readbackColor(binding.colorIdentity);
    };
    stage="original single segment GPU output";const auto full=pixels(run(2,.5f,false,false));
    need(full.changed>100&&full.red>1000&&full.minX>=259&&full.maxX<=981&&full.minY>=336&&full.maxY<=384,"Beam GPU output ignored original model/view/projection or width");
    stage="original alpha product";const auto half=pixels(run(2,.25f,true,false));
    need(half.red<full.red&&std::abs(double(half.red)*2-double(full.red))<double(full.changed+half.changed)*2,"Beam ignored original instance alpha product");
    stage="original alpha test";const auto clipped=pixels(run(2,.03125f,false,false));need(!clipped.changed,"Beam did not retain original alpha threshold eight");
    stage="original null texture beam";PPC_STORE_U32(owner+0xA8,0);
    need(run(2,.5f,false,false)==black,"Original null beam texture reused color or bypassed alpha test");
    stage="valid texture after null beam";PPC_STORE_U32(owner+0xA8,texture);
    need(pixels(run(2,.5f,false,false)).changed>100,"Null beam texture poisoned a later valid binding");
    stage="multiple segments and cleanup guard";point(base,0,-.75f);point(base,1,0);point(base,2,.75f);
    need(pixels(run(3,.5f,true,true)).changed>100,"Original owner loop lost multiple beam segments");
    stage="turning segments";point(base,0,-.75f,-.25f);point(base,1,0,.25f);point(base,2,.75f,-.25f);
    need(pixels(run(3,.5f,false,false)).changed>100,"Original perpendicular width failed on turning segments");
    stage="degenerate segment";point(base,0,-.75f);point(base,1,-.75f);need(run(2,.5f,false,false)==black,"Degenerate original beam generated pixels");
    stage="original squared-length threshold";point(base,0,0);point(base,1,.0625f);need(run(2,.5f,false,false)==black,"Original short-segment threshold generated width");
    stage="maximum original stack extent";for(uint32_t i=0;i<25;++i)point(base,i,-1.5f+float(i)/8);
    need(pixels(run(25,.5f,true,false)).changed>100,"Maximum bounded beam did not render");
    stage="original owner array traversal";clear();point(base,0,-.75f);point(base,1,.75f);PPC_STORE_U32(row,start);
    item(base,items,points,1);item(base,items+0x90,points,2);item(base,items+0x120,points,2,.25f);PPC_STORE_U32(owner+0xC8,3);
    const auto arrayDraws=d.immediateDrawCount();const auto arrayAbi=abi(c);
    cpu.invoke(0x8277B450,owner);need(abi(c)==arrayAbi&&d.immediateDrawCount()==arrayDraws+2&&PPC_LOAD_U32(row)==start+256,
        "Original owner array skipped active items or retained a previous item batch");
    need(pixels(d.readbackColor(binding.colorIdentity)).changed>100,"Original multiple-item owner produced no pixels");
    sameDepth(d.readbackDepth(binding.depthIdentity),depth);PPC_STORE_U32(owner+0xC8,1);
    stage="owner and extent guards";item(base,items,points,2);const auto good=c;const auto guardsDraws=d.immediateDrawCount();
    const auto guardsCursor=PPC_LOAD_U32(row);const auto guardsPixels=d.readbackColor(binding.colorIdentity);const auto guardsCaches=caches(base);
    for(unsigned which=0;which<3;++which){if(which==0)PPC_STORE_U32(owner,0x82158958);if(which==1)PPC_STORE_U32(owner+0xC0,items+4);if(which==2)PPC_STORE_U32(items+0x68,26);
        rejects([&]{cpu.invoke(0x8277B230,owner,items);},"Invalid beam owner/extent accepted");c=good;
        PPC_STORE_U32(owner,0x8215895C);PPC_STORE_U32(owner+0xC0,items);PPC_STORE_U32(items+0x68,2);
        need(d.immediateDrawCount()==guardsDraws&&PPC_LOAD_U32(row)==guardsCursor&&caches(base)==guardsCaches&&d.readbackColor(binding.colorIdentity)==guardsPixels,"Rejected beam changed draw, cursor, caches, or attachment");}
    stage="draw after guard rejection";point(base,0,-.75f);point(base,1,.75f);need(pixels(run(2,.5f,false,false)).changed>100,"Rejected beam poisoned subsequent rendering");
    cpu.invoke(0x826B09F0,stateSave);cpu.invoke(0x823F1A08,camera);
}
}
PPC_FUNC(sub_8277B040){const auto before=abi(ctx);if(observer)observer(ctx,base,false);__imp__sub_8277B040(ctx,base);
    need(abi(ctx)==before,"Original segment changed nonvolatile ABI");if(observer)observer(ctx,base,true);}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try{need(argc==2,"Original image required");Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        bool observed=false;rt.audioBoundaryObserver=[](uint32_t pc,PPCContext&,uint8_t*){if(pc==0x828166FC)throw Observed{};};
        try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}rt.audioBoundaryObserver={};need(observed,"Original startup boundary missing");
        exercise(rt,entry);std::printf("PASS original immediate effects: %zu checks; whole beam owner/point/segment loops, transforms, RGB/alpha, GPU pixels, bounded storage, ABI, cleanup\n",checks);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original immediate effects: %zu checks stage=%s: %s\n",checks,stage,error.what());return 1;}
}
