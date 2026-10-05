// Complete original 82775F68 eight-vertex strip: camera-facing cross product,
// width/UV math, endpoint fading, closed-strip copies and VMX ABI are original.
#include "immediate_original_helpers.h"
namespace {
constexpr uint32_t area=0x60000,owner=area,definition=area+0x200,eye=area+0x400,eyeFrame=area+0x500;
void exercise(const char* path){
    ImmediateFixture f(path);auto& rt=f.rt;auto* base=f.base;auto& cpu=*f.cpu;auto& c=cpu.registers();auto& d=f.driver();
    rt.map(area,0x1000,true,"original eight-vertex effect inputs");std::memset(rt.pointer(area,0x1000,true),0,0x1000);
    Restore cameraGlobal(rt,0x82DFF098,4),projection(rt,0x82DFEAE0,64),unrelated(rt,0x82DFEAA0,64),published(rt,0x82DFF160,4),ring(rt,f.ring,0x1000);
    PPC_STORE_U32(owner,0x821586A8);PPC_STORE_U32(owner+0xA0,definition);PPC_STORE_U32(owner+0xA4,f.texture);
    matrix(base,owner+0x60,translated(.125f));put(base,owner+0xB4,0);put(base,owner+0xB8,.5f);
    PPC_STORE_U16(definition+0x10,1);PPC_STORE_U8(definition+0x12,0);put(base,definition+0x18,.125f);put(base,definition+0x1C,.25f);
    for(uint32_t lane=0;lane<3;++lane)put(base,definition+0x20+4*lane,.125f);
    put(base,definition+0x30,-.5f);put(base,definition+0x34,0);put(base,definition+0x38,.5f);
    put(base,definition+0x3C,.5f);put(base,definition+0x40,0);put(base,definition+0x44,.5f);
    put(base,definition+0x48,.25f);put(base,definition+0x4C,.5f);put(base,definition+0x50,.5f);
    PPC_STORE_U32(0x82DFF098,eye);PPC_STORE_U32(eye+4,eyeFrame);PPC_STORE_U32(eyeFrame+0xA0,eyeFrame);matrix(base,eyeFrame+0x10,translated(.125f));
    matrix(base,0x82DFEAE0,{.75f,0,0,0,0,.5f,0,0,0,0,1,0,-.125f,.125f,0,1});matrix(base,0x82DFEAA0,translated(4,4));
    auto invoke=[&]{seed(c);const auto before=abi(c);cpu.invoke(0x82775F68,owner);need(abi(c)==before,"Original strip changed nonvolatile GPR/FPR/VMX, SP or LR");};
    stage="original alpha and cross-product early exits";f.clear();const auto black=f.color();const auto initialDepth=f.depth();
    for(bool degenerate:{false,true}){
        put(base,owner+0xB8,degenerate?.5f:0);put(base,definition+0x3C,degenerate?-.5f:.5f);
        const auto before=d.immediateDrawCount();const auto cursor=PPC_LOAD_U32(f.row);const auto cache=caches(base);const auto stateBefore=d.effectiveState();invoke();
        need(d.immediateDrawCount()==before&&PPC_LOAD_U32(f.row)==cursor&&caches(base)==cache&&f.color()==black,"Early original strip reserved or drew geometry");
        for(const auto& field:Graphics::scalarStateEvidence())need(d.effectiveState().scalar(field.id)==stateBefore.scalar(field.id),"Early original strip changed state");sameDepth(f.depth(),initialDepth);
    }
    put(base,owner+0xB8,.5f);put(base,definition+0x3C,.5f);
    auto run=[&](uint16_t flags){
        PPC_STORE_U16(definition+0x10,flags);f.clear();f.poison(0x1000);const auto input=snapshot(rt,area,0x1000);const auto depth=f.depth();const auto before=d.immediateDrawCount();invoke();
        need(d.immediateDrawCount()==before+1&&PPC_LOAD_U32(f.row)==f.ring+256,"Original strip did not produce exactly eight vertices");
        const bool reverse=(flags&0x10)!=0,opaqueEdge=(flags&0x20)!=0;
        const float ax=reverse?.625f:-.375f,bx=reverse?-.375f:.625f,aw=reverse?.25f:.125f,bw=reverse?.125f:.25f;
        const float aa=reverse?.375f:.5f,ba=reverse?.5f:.375f,av=reverse?.5f:0,bv=reverse?0:.5f;
        const std::array<std::array<float,6>,8> expected={{{ax,0,.5f,aa,.5f,av},{ax,aw,.5f,opaqueEdge?aa:0,.5f+aw,av},
            {bx,0,.5f,ba,.5f,bv},{bx,bw,.5f,opaqueEdge?ba:0,.5f+bw,bv},{bx,-bw,.5f,opaqueEdge?ba:0,.5f-bw,bv},
            {bx,0,.5f,ba,.5f,bv},{ax,-aw,.5f,opaqueEdge?aa:0,.5f-aw,av},{ax,0,.5f,aa,.5f,av}}};
        for(uint32_t vertex=0;vertex<8;++vertex){const auto at=f.ring+32*vertex;
            for(uint32_t lane=0;lane<6;++lane){const float actual=get(base,at+4*lane);if(!(std::abs(actual-expected[vertex][lane])<.000003f))
                std::fprintf(stderr,"Eight-vertex mismatch flags=%X vertex=%u lane=%u actual=%.9g expected=%.9g\n",flags,vertex,lane,actual,expected[vertex][lane]);
                need(std::abs(actual-expected[vertex][lane])<.000003f,"Original strip position/width/UV/fade differs");}
            need(PPC_LOAD_U32(at+24)==UINT32_MAX&&PPC_LOAD_U32(at+28)==UINT32_MAX,"Original strip changed unused UV lanes");}
        need(!std::memcmp(rt.pointer(f.ring,32,false),rt.pointer(f.ring+224,32,false),32)&&
            !std::memcmp(rt.pointer(f.ring+64,32,false),rt.pointer(f.ring+160,32,false),32),"Original closed strip copies differ");
        same(rt,area,input,"Original strip changed its source definition/model/eye");sameDepth(f.depth(),depth);f.lifetime();
        using S=Graphics::ScalarState;need(!d.effectiveState().scalar(S::AlphaTest)&&d.effectiveState().scalar(S::DepthEnable)==1&&
            d.effectiveState().scalar(S::DepthWrite)==1&&!d.effectiveState().scalar(S::BlendEnable),"Original strip failed immediate cleanup");return f.color();
    };
    stage="original feathered strip";const auto full=run(1);need(colored(full)>100,"Original eight-vertex strip generated no GPU color");
    for(uint32_t y=0;y<720;++y)for(uint32_t x=0;x<1280;++x)if(pixel(full,4*(1280*y+x))&0x3FFFFFFF)
        need(x>=378&&x<=862&&y>=268&&y<=362,"Strip ignored original world/projection transforms");
    stage="repeated additive strip";const auto repeats=d.immediateDrawCount();invoke();const auto twice=f.color();bool contribution=false;
    need(d.immediateDrawCount()==repeats+1&&PPC_LOAD_U32(f.row)==f.ring+512,"Repeated strip retained a stale reservation");
    for(size_t i=0;i<full.size();i+=4)for(uint32_t shift:{0u,10u,20u}){const auto a=(pixel(full,i)>>shift)&1023,b=(pixel(twice,i)>>shift)&1023;
        if(a>3&&a<200){need(b+1>=2*a&&b<=2*a+1,"Repeated strip did not retain additive blending");contribution=true;}}
    need(contribution,"Strip had no measurable repeated contribution");
    stage="original null eight-vertex texture";PPC_STORE_U32(owner+0xA4,0);
    need(run(1)==black,"Null strip texture escaped original alpha rejection or retained the previous texture");
    stage="original eight-vertex null-to-valid texture transition";PPC_STORE_U32(owner+0xA4,f.texture);
    need(run(1)==full,"Original strip failed to restore its valid texture after a null draw");
    stage="original endpoint reversal";need(colored(run(0x11))>100,"Reversed original strip generated no pixels");
    stage="original opaque edge flag";need(colored(run(0x21))>=colored(full),"Original edge-alpha flag reduced strip coverage");
    stage="original RGB constants";for(uint32_t lane=0;lane<3;++lane)put(base,definition+0x20+4*lane,.0625f);const auto tinted=run(1);bool measured=false;
    for(size_t i=0;i<full.size();i+=4)for(uint32_t shift:{0u,10u,20u}){const auto a=(pixel(full,i)>>shift)&1023,b=(pixel(tinted,i)>>shift)&1023;
        if(a>3&&a<200){need(std::abs(float(b)-.5f*float(a))<=1.5f,"Strip ignored original RGB modulation");measured=true;}}
    need(measured,"Strip tint had no measurable source color");f.end();
}
}
int main(int argc,char** argv){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try{need(argc==2,"Original image required");exercise(argv[1]);std::printf("PASS original eight-vertex strip: %zu checks; original cross/width/UV/fade/closing copies, flags, GPU output, ABI and lifetime\n",checks);return 0;}
    catch(const std::exception& error){std::fprintf(stderr,"FAIL original eight-vertex strip: %zu checks stage=%s: %s\n",checks,stage,error.what());return 1;}}
