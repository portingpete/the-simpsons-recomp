// Whole original 827793A0 ribbon parent, including its per-point fade/width
// calculations, seven-vertex tessellation, segment selection and list traversal.
#include "immediate_original_helpers.h"

extern "C" void __imp__sub_82778F58(PPCContext&,uint8_t*);
namespace {
std::function<void(PPCContext&,uint8_t*,bool)> segmentObserver;
struct Observation {
    explicit Observation(decltype(segmentObserver) value){need(!segmentObserver,"Nested ribbon observation");segmentObserver=std::move(value);}
    ~Observation(){segmentObserver={};}
};
constexpr uint32_t owner=0x60000,definition=0x60200,collection=0x60300,entries=0x60340,
    item=0x60400,transform=0x60600,system=0x60800,points=0x61000;
void setup(ImmediateFixture& f) {
    auto* base=f.base;f.rt.map(owner,0x3000,true,"original ribbon CPU input owners");
    std::memset(f.rt.pointer(owner,0x3000,true),0,0x3000);
    PPC_STORE_U32(owner,0x821588E0);PPC_STORE_U32(owner+0x14,system);PPC_STORE_U32(owner+0xB4,definition);
    PPC_STORE_U32(owner+0xB8,f.texture);PPC_STORE_U32(owner+0x1BC,collection);
    PPC_STORE_U32(collection+8,1);PPC_STORE_U32(collection+12,entries);PPC_STORE_U32(entries,item);
    PPC_STORE_U32(item+12,3);PPC_STORE_U32(item+16,points);PPC_STORE_U32(item+24,transform);
    put(base,owner+0x9C,1);put(base,owner+0xC8,1); // Angular fade is exactly one.
    put(base,owner+0x1A4,.5f);put(base,owner+0x1AC,.25f); // Constant alpha/width curve descriptors.
    put(base,definition+0x38,1);put(base,definition+0x44,0);put(base,definition+0x48,4);put(base,definition+0x4C,.5f);
    for(uint32_t i=0;i<3;++i)put(base,definition+0x50+4*i,.125f);
    put(base,definition+0x60,1);put(base,definition+0x6C,1);put(base,definition+0x70,1);
    matrix(base,transform+0x40,translated());matrix(base,transform+0x80,translated());
    for(uint32_t i=0;i<31;++i) {
        const uint32_t at=points+32*i;put(base,at,-.5f+float(i)*.5f);put(base,at+8,.5f);
        PPC_STORE_U8(at+15,2);put(base,at+20,1); // Original point normal is +Y.
    }
}
struct Emission {uint32_t segment;float phase;};
std::vector<Emission> emissions(uint8_t* base,uint32_t count) {
    std::vector<Emission> result;float phase=0;
    for(uint32_t i=0;i+1<std::min(count,30u);++i) {
        if(PPC_LOAD_U8(points+32*(i+1)+15)==2){result.push_back({i,phase});phase+=1;}
        else phase=0;
    }
    return result;
}
// The chosen fixture uses the original constant curve mode, unit angular fade,
// range [0,4], midpoint .5, and stored +Y direction. This scalar derivation is
// independent of the native bridge and original vector helper implementation.
std::array<std::array<float,6>,7> expected(uint8_t* base,const Emission& e) {
    const float x0=get(base,points+32*e.segment),x1=get(base,points+32*(e.segment+1));
    const float distance0=std::sqrt(x0*x0+.25f),distance1=std::sqrt(x1*x1+.25f);
    const float width=get(base,owner+0x1AC),alpha=get(base,owner+0x1A4);
    const float w0=width*(1-.1875f*distance0),w1=width*(1-.1875f*distance1);
    const float a0=alpha*distance0*.5f,a1=alpha*distance1*.5f;
    const float midAlpha=(a0+a1)*.5f;
    return {{{x0,0,.5f,a0,e.phase,0},{x0,w0,.5f,a0,e.phase,1},
        {x0+(x1-x0)*.25f,0,.5f,midAlpha,e.phase+.25f,0},
        {(x0+x1)*.5f,(w0+w1)*.5f,.5f,midAlpha,e.phase+.5f,1},
        {x0+(x1-x0)*.75f,0,.5f,midAlpha,e.phase+.75f,0},
        {x1,w1,.5f,a1,e.phase+1,1},{x1,0,.5f,a1,e.phase+1,0}}};
}
void run(ImmediateFixture& f) {
    auto* base=f.base;auto& driver=f.driver();auto& c=f.cpu->registers();setup(f);
    Restore globalCamera(f.rt,0x82DFF098,4),viewProjection(f.rt,0x82DFEAE0,64);
    PPC_STORE_U32(0x82DFF098,0);matrix(base,0x82DFEAE0,translated());
    need(get(base,0x821DD368)==.75f&&get(base,0x821DD39C)==.25f&&get(base,0x82000FB8)==.5f,
         "Original ribbon interpolation constants changed");
    auto invoke=[&] {
        seed(c);const auto before=abi(c);f.cpu->invoke(0x827793A0,owner);
        need(abi(c)==before,"Whole original ribbon parent changed nonvolatile ABI");
    };
    stage="original hidden, empty and missing item branches";f.clear();
    const auto black=f.color();const auto depth=f.depth();
    for(uint32_t kind=0;kind<4;++kind) {
        if(kind==0){PPC_STORE_U32(owner+0x10,1);PPC_STORE_U32(owner+0x1BC,0);}
        if(kind==1)PPC_STORE_U32(collection+8,0);
        if(kind==2)PPC_STORE_U32(entries,0);
        if(kind==3)PPC_STORE_U32(item+24,0);
        const auto draws=driver.immediateDrawCount();const auto beforeCursor=PPC_LOAD_U32(f.row);const auto beforeCaches=caches(base);
        invoke();need(driver.immediateDrawCount()==draws&&PPC_LOAD_U32(f.row)==beforeCursor&&caches(base)==beforeCaches&&f.color()==black,
            "Original skipped ribbon entered immediate rendering");
        PPC_STORE_U32(owner+0x10,0);PPC_STORE_U32(owner+0x1BC,collection);PPC_STORE_U32(collection+8,1);
        PPC_STORE_U32(entries,item);PPC_STORE_U32(item+24,transform);
    }
    auto draw=[&](uint32_t count,bool probeCleanup=false,uint32_t repeats=1) {
        f.clear();f.poison(0x2000);PPC_STORE_U32(item+12,count);PPC_STORE_U32(collection+8,repeats);
        for(uint32_t i=0;i<repeats;++i)PPC_STORE_U32(entries+4*i,item);
        const auto selected=emissions(base,count);const auto startDraws=driver.immediateDrawCount();uint32_t calls=0;bool probed=false;
        const auto ownerPrefix=snapshot(f.rt,owner,0xD0),ownerSuffix=snapshot(f.rt,owner+0x194,0x30);
        const auto parameters=snapshot(f.rt,definition,0x100),model=snapshot(f.rt,transform,0xC0),inputPoints=snapshot(f.rt,points,31*32);
        Observation observation([&](PPCContext& segment,uint8_t* memory,bool after) {
            need(memory==base&&uint32_t(segment.lr)==0x82779C60,"Ribbon bypassed its original parent-to-segment call");
            if(!after) {
                need(segment.r3.u32==owner+0xD0&&segment.r4.u32==segment.r1.u32+0x88&&calls<selected.size()*repeats,
                     "Ribbon segment argument or count differs");
                const auto& e=selected[calls%selected.size()];
                need(segment.r28.u32==std::min(count,30u)-1-e.segment&&segment.r29.u32==points+0x2E+32*e.segment,
                     "Original ribbon segment selection/order differs");
            } else {
                const uint32_t source=f.ring+224*calls;const auto oracle=expected(base,selected[calls%selected.size()]);
                for(uint32_t vertex=0;vertex<7;++vertex) {
                    for(uint32_t lane=0;lane<6;++lane) {
                        const float actual=get(base,source+32*vertex+4*lane);
                        if(!(std::abs(actual-oracle[vertex][lane])<.00001f))
                            std::fprintf(stderr,"Ribbon mismatch segment=%u vertex=%u lane=%u actual=%.9g expected=%.9g\n",calls,vertex,lane,actual,oracle[vertex][lane]);
                        need(std::abs(actual-oracle[vertex][lane])<.00001f,"Original ribbon position, fade, width, UV or tessellation differs");
                    }
                    need(PPC_LOAD_U32(source+32*vertex+24)==UINT32_MAX&&PPC_LOAD_U32(source+32*vertex+28)==UINT32_MAX,
                         "Ribbon changed unwritten mode-zero UV lanes");
                }
                ++calls;
                if(probeCleanup&&!probed&&calls<selected.size()) {
                    const auto saved=segment;segment.r3.u32=segment.r1.u32+0x88;segment.lr=0x82779CE0;
                    rejects([&]{driver.immediateGeometryState(segment,base,false);},"Ribbon accepted cleanup before selected segments completed");
                    segment=saved;probed=true;
                }
            }
        });
        invoke();need(calls==selected.size()*repeats&&driver.immediateDrawCount()==startDraws+calls&&PPC_LOAD_U32(f.row)==f.ring+224*calls,
            "Whole original ribbon lost/repeated a segment or advanced the ring incorrectly");
        need(!probeCleanup||probed,"Ribbon incomplete cleanup guard was not exercised");
        same(f.rt,owner,ownerPrefix,"Ribbon changed original owner input fields");same(f.rt,owner+0x194,ownerSuffix,"Ribbon changed original curve/list inputs");
        same(f.rt,definition,parameters,"Ribbon changed original definition");same(f.rt,transform,model,"Ribbon changed source object transforms");
        same(f.rt,points,inputPoints,"Ribbon changed input points");sameDepth(f.depth(),depth);f.lifetime();
        using S=Graphics::ScalarState;
        need(!driver.effectiveState().scalar(S::AlphaTest)&&!driver.effectiveState().scalar(S::BlendEnable)&&
             driver.effectiveState().scalar(S::DepthEnable)==1&&driver.effectiveState().scalar(S::DepthWrite)==1,
             "Original ribbon end left immediate direct state active");
        return f.color();
    };
    stage="original seven-vertex tessellation and multiple segments";const auto baseline=draw(3,true);need(colored(baseline)>100,"Original ribbon produced no GPU color");
    stage="original null ribbon texture";PPC_STORE_U32(owner+0xB8,0);
    need(draw(3)==black,"Null ribbon texture escaped original alpha rejection or retained the previous texture");
    stage="original ribbon null-to-valid texture transition";PPC_STORE_U32(owner+0xB8,f.texture);
    need(draw(3)==baseline,"Ribbon failed to restore its valid texture after a null draw");
    stage="original model transform";matrix(base,transform+0x40,translated(.25f));matrix(base,transform+0x80,translated(-.25f));put(base,owner+0x90,.25f);
    const auto shifted=draw(3);bool translatedPixels=true;
    for(uint32_t y=0;y<720;++y)for(uint32_t x=0;x<1280;++x) {
        const auto expectedPixel=x>=160?pixel(baseline,4*(size_t(y)*1280+x-160)):0xC0000000u;
        translatedPixels&=pixel(shifted,4*(size_t(y)*1280+x))==expectedPixel;
    }
    need(translatedPixels,"Ribbon ignored or repeated its original model-to-view matrix product");
    matrix(base,transform+0x40,translated());matrix(base,transform+0x80,translated());put(base,owner+0x90,0);
    stage="original repeated collection entries";const auto doubled=draw(3,false,2);bool additive=false;
    for(size_t at=0;at<baseline.size();at+=4)for(uint32_t shift:{0u,10u,20u}) {
        const auto a=(pixel(baseline,at)>>shift)&1023,b=(pixel(doubled,at)>>shift)&1023;
        if(a>3&&a<150){need(b+2>=2*a&&b<=2*a+2,"Ribbon collection batches lost direct additive blending");additive=true;}
    }
    need(additive,"Repeated ribbon collection had no measurable color");
    stage="original skipped segment";PPC_STORE_U8(points+32+15,1);const auto skipped=draw(3);need(colored(skipped)>0&&skipped!=baseline,"Ribbon ignored original segment selection");PPC_STORE_U8(points+32+15,2);
    stage="original empty batch cleanup";need(draw(0)==black&&draw(1)==black,"Zero/one-point ribbon emitted geometry");
    stage="original alpha curve zero";put(base,owner+0x1A4,0);need(draw(3)==black,"Ribbon ignored original alpha fade");put(base,owner+0x1A4,.5f);
    stage="original width curve zero";put(base,owner+0x1AC,0);need(draw(3)==black,"Ribbon ignored original width curve");put(base,owner+0x1AC,.25f);
    stage="original thirty-point clamp";for(uint32_t i=0;i<31;++i)put(base,points+32*i,-.5f+float(i)/32);
    need(colored(draw(31))>100,"Original ribbon point clamp lost geometry");
    stage="unknown original ribbon owner rejection";f.clear();f.poison(0x2000);PPC_STORE_U32(item+12,3);PPC_STORE_U32(collection+8,1);
    const auto beforeCount=driver.immediateDrawCount();const auto before=f.color();const auto good=c;
    PPC_STORE_U32(owner,0x821588D8);rejects(invoke,"Unrelated object accepted as original ribbon owner");c=good;PPC_STORE_U32(owner,0x821588E0);
    need(driver.immediateDrawCount()==beforeCount&&PPC_LOAD_U32(f.row)==f.ring&&f.color()==before,"Rejected ribbon owner reserved or submitted geometry");
    stage="ribbon draw after rejected owner";need(colored(draw(3))>0,"Rejected ribbon owner poisoned subsequent draw");f.end();
}
}
PPC_FUNC(sub_82778F58) {
    const auto before=abi(ctx);if(segmentObserver)segmentObserver(ctx,base,false);__imp__sub_82778F58(ctx,base);
    need(abi(ctx)==before,"Original seven-vertex helper changed nonvolatile ABI");if(segmentObserver)segmentObserver(ctx,base,true);
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try{need(argc==2,"Original image required");ImmediateFixture fixture(argv[1]);run(fixture);
        std::printf("PASS original ribbon: %zu checks; whole parent/list/segment loops, fade/width/UV, model matrix, GPU pixels, ABI and cleanup\n",checks);return 0;}
    catch(const std::exception& error){std::fprintf(stderr,"FAIL original ribbon: %zu checks stage=%s: %s\n",checks,stage,error.what());return 1;}
}
