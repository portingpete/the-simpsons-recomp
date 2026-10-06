// Original Dof/Blur/Bloom/Fog/Sat pass code through the derived native bridge.
// Fixture setup writes only the passes' own layer blocks; gating, constant math,
// vertex stores and layer resets are original AOT. Uniform scene color makes
// every tap equal, so short oracles over the constants the original CPU wrote
// check resource selection, blending and packing. Camera clears leave depth at
// the far plane (zero), so depth-driven output takes the original far branches.
#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include "runtime/engine_viewport_surfaces.h"
#include "renderer/engine_state.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
namespace {
using namespace Simpsons;
size_t checks{};
void need(bool b,const char* why){++checks;if(!b)throw Failure(why);}
template<class F>void rejects(F f){try{f();}catch(const std::exception&){++checks;return;}throw Failure("Invalid screen-effect request accepted");}
struct Observed{};
struct Abi {uint64_t sp,lr;std::array<uint64_t,18> gpr,fpr;bool operator==(const Abi&)const=default;};
Abi abi(const PPCContext& c){return {c.r1.u64,c.lr,{c.r14.u64,c.r15.u64,c.r16.u64,c.r17.u64,c.r18.u64,c.r19.u64,c.r20.u64,c.r21.u64,c.r22.u64,c.r23.u64,c.r24.u64,c.r25.u64,c.r26.u64,c.r27.u64,c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64},
    {c.f14.u64,c.f15.u64,c.f16.u64,c.f17.u64,c.f18.u64,c.f19.u64,c.f20.u64,c.f21.u64,c.f22.u64,c.f23.u64,c.f24.u64,c.f25.u64,c.f26.u64,c.f27.u64,c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64}};}
using Pixel=std::array<float,4>;
Pixel unpack(uint32_t p){return {float(p&1023)/1023.0f,float(p>>10&1023)/1023.0f,float(p>>20&1023)/1023.0f,float(p>>30)/3.0f};}
uint32_t pack(Pixel p){uint32_t r=0;for(uint32_t i=0;i<4;++i)r|=uint32_t(std::nearbyint(std::clamp(p[i],0.0f,1.0f)*(i==3?3.0f:1023.0f)))<<(10*i);return r;}
// Blend words used by these passes: 0x10001 copy, 0x10706 alpha, 0x10006 src*alpha.
uint32_t blend(Pixel source,uint32_t destination,uint32_t word){
    const Pixel d=unpack(destination);const float sf=(word&31)==6?source[3]:1,df=((word>>8)&31)==7?1-source[3]:0;
    for(uint32_t i=0;i<3;++i)source[i]=source[i]*sf+d[i]*df;return pack(source);
}
void compare(const std::vector<uint8_t>& pixels,const std::vector<uint32_t>& expected,const char* why){
    need(pixels.size()==expected.size()*4,"Screen-effect readback extent differs");
    for(size_t i=0;i<expected.size();++i){uint32_t got{};std::memcpy(&got,pixels.data()+4*i,4);
        for(uint32_t channel=0;channel<4;++channel){const uint32_t mask=channel==3?3:1023;
            const int a=int(got>>(10*channel)&mask),b=int(expected[i]>>(10*channel)&mask);
            if(std::abs(a-b)>(channel==3?0:1)){std::fprintf(stderr,"pixel%zu channel%u got%d expected%d\n",i,channel,a,b);need(false,why);}}
        ++checks;}
}
void exercise(const char* image){
    Runtime rt;rt.load(image);PPCContext startup{};rt.initialize(startup);const auto entry=startup;auto* base=rt.base;
    rt.audioBoundaryObserver=[](uint32_t pc,PPCContext&,uint8_t*){if(pc==0x828166FC)throw Observed{};};
    bool observed=false;try{runOriginal(startup,base);}catch(const Observed&){observed=true;}rt.audioBoundaryObserver={};need(observed,"Original startup checkpoint missing");
    auto& d=*rt.engineDriver;EngineCpuCalls cpu(entry,base);auto& c=cpu.registers();rt.map(0x50000,0x1000,true,"screen-effect fixture arguments");
    need(rt.videoSettings.bloom&&rt.videoSettings.depthOfField&&rt.videoSettings.motionBlur&&rt.videoSettings.atmosphericFog&&
         rt.videoSettings.colorGrading&&rt.videoSettings.cinematicLetterbox,"Original screen effects are not enabled by default");
    const auto camera=PPC_LOAD_U32(0x82E07248),color=PPC_LOAD_U32(0x82D0CB00),depth=PPC_LOAD_U32(0x82D0CAFC);
    auto& viewport=d.viewportSurfaces();
    auto storeFloat=[&](uint32_t address,float value){PPC_STORE_U32(address,std::bit_cast<uint32_t>(value));};
    auto setColor=[&](float r,float g,float b,float a){const std::array<float,4> v={r,g,b,a};for(uint32_t i=0;i<4;++i)storeFloat(0x50000+4*i,v[i]);};
    auto flat=[&](float r,float g,float b){setColor(r,g,b,1);c.r8.u32=0x50000;c.r9.u32=3;c.r10.u32=0;
        c.f1.f64=0;c.f2.f64=0;c.f3.f64=1280;c.f4.f64=720;c.f5.f64=0;c.f6.f64=0;c.f7.f64=1;c.f8.f64=1;
        const auto before=abi(c);cpu.invoke(0x82756480,camera);need(abi(c)==before,"Original flat screen draw damaged ABI");
        auto pixels=d.readbackColor(color);uint32_t word{};std::memcpy(&word,pixels.data(),4);return word;};
    auto invoke=[&](uint32_t function,uint32_t lr,uint32_t argument){c.lr=lr;const auto before=abi(c);cpu.invoke(function,argument);
        need(abi(c)==before,"Original screen-effect pass damaged nonvolatile ABI");};
    auto draws=[&]{std::array<uint64_t,7> r{};for(uint32_t i=0;i<7;++i)r[i]=d.screenEffectDrawCount(i);return r;};
    auto sameState=[&](const Graphics::EngineState& before,const char* why){
        for(const auto& f:Graphics::scalarStateEvidence())need(d.effectiveState().scalar(f.id)==before.scalar(f.id),why);};
    auto finalState=[&](bool depthWrite){using S=Graphics::ScalarState;const auto& e=d.effectiveState();
        need(e.scalar(S::DepthEnable)==1&&!e.scalar(S::BlendEnable)&&!e.scalar(S::ExpandedBlend0)&&!e.scalar(S::Cull)&&!e.scalar(S::AlphaTest)&&
             e.scalar(S::HalfPixelOffset)==1&&(!depthWrite||e.scalar(S::DepthWrite)==1),"Original screen-effect post-state differs");};
    cpu.invoke(0x823F1A18,camera);cpu.invoke(0x826B09A0,0x50040,1);
    d.directScalar(base,0x158,0x3F800000);d.directScalar(base,0x15C,0x3F800000);
    constexpr uint32_t dof=0x82DFF0B0,blur=0x82DFF100,bloom=0x82DFF130,fog=0x82DFF290,sat=0x82CF24C0;
    std::vector<uint8_t> layerBackup;for(uint32_t a:{dof,blur,bloom,fog,sat}){const auto* p=rt.pointer(a,0x80,false);layerBackup.insert(layerBackup.end(),p,p+0x80);}
    for(uint32_t a:{dof,blur,bloom,fog})std::memset(rt.pointer(a,0x80,true),0,0x80);std::memset(rt.pointer(sat,0x20,true),0,0x20);
    // Idle frame: 82751700 copies depth and returns from inactive Fog and Dof;
    // Blur, Bloom's parent and Sat return before any SDK endpoint.
    const auto idleScene=flat(.5f,.25f,.75f);const auto idleState=d.effectiveState();const auto idleDraws=draws();
    const auto idleCopies=viewport.colorCopyCount(),idleDepthCopies=viewport.depthCopyCount();
    invoke(0x82751700,0x82751770,camera);invoke(0x82754C90,0x827517C0,camera);invoke(0x82755A70,0x827517C8,camera);invoke(0x8276FE60,0x827517B8,camera);
    need(draws()==idleDraws&&viewport.colorCopyCount()==idleCopies&&viewport.depthCopyCount()==idleDepthCopies+1,
         "Inactive original screen effects drew or resolved");
    auto pixels=d.readbackColor(color);uint32_t word{};std::memcpy(&word,pixels.data(),4);need(word==idleScene,"Inactive screen effects changed the scene");
    sameState(idleState,"Inactive screen effects changed effective state");
    need(PPC_LOAD_U8(blur+0x20)==1,"Inactive original Blur did not arm its first-frame flag");
    const auto sceneDepth=d.readbackDepth(depth);
    // Sat: layer color/weight at 82CF24C0; the original CPU derives c0/c1 on its stack.
    for(bool enabled:{false,true,false}) {
        rt.videoSettings.colorGrading=enabled;
        const auto scene=flat(.5f,.25f,.75f);const auto before=draws();const auto copies=viewport.colorCopyCount();
        for(uint32_t i=0;i<4;++i)storeFloat(sat+4*i,std::array<float,4>{.9f,.6f,.3f,.8f}[i]);storeFloat(sat+16,1);
        invoke(0x8276FE60,0x827517B8,camera);
        const auto k=d.screenEffectConstants();const Pixel s=unpack(scene);Pixel out{};
        for(uint32_t i=0;i<3;++i)out[i]=s[i]*k[0][i]*(k[1][3]*8+k[1][i]);out[3]=k[0][3];
        need(draws()[4]==before[4]+uint64_t(enabled)&&viewport.colorCopyCount()==copies+1,"Live color grading toggle lost its original resolve");
        need(k[0][0]>.89f&&k[0][0]<.91f,"Original Sat c0 is not its normalized layer color");
        std::vector<uint32_t> expected(1280*720,enabled?blend(out,scene,0x10006):scene);compare(d.readbackColor(color),expected,"Live color grading pixels differ");
        need(d.readbackDepth(depth)==sceneDepth,"Original Sat changed depth/stencil");finalState(true);
        need(PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(0x82CF24B4),"Original Sat pixel shader cache differs");
        need(std::bit_cast<float>(PPC_LOAD_U32(sat+16))==0,"Original Sat did not reset its weight");
    }
    rt.videoSettings.colorGrading=true;
    // Blur: arm, prime history (resolve only), then blend history over a new scene.
    {
        rt.videoSettings.motionBlur=false;
        const auto history=flat(.2f,.8f,.4f);const auto before=draws();const auto copies=viewport.colorCopyCount();
        for(uint32_t i=0;i<4;++i)storeFloat(blur+4*i,std::array<float,4>{.8f,1.2f,.6f,.5f}[i]);storeFloat(blur+16,1);
        invoke(0x82754C90,0x827517C0,camera);
        need(draws()[1]==before[1]&&viewport.colorCopyCount()==copies+1&&!PPC_LOAD_U8(blur+0x20),"Original Blur first frame did not only prime its history");
        need(viewport.readbackColorTexture(0x82DFE3FC)==d.readbackColor(color),"Original Blur did not resolve the scene into its history");
        rt.videoSettings.motionBlur=true;
        const auto scene=flat(.6f,.3f,.1f);
        for(uint32_t i=0;i<4;++i)storeFloat(blur+4*i,std::array<float,4>{.8f,1.2f,.6f,.5f}[i]);storeFloat(blur+16,1);
        invoke(0x82754C90,0x827517C0,camera);
        const auto k=d.screenEffectConstants();const Pixel h=unpack(history);Pixel out{};
        for(uint32_t i=0;i<3;++i)out[i]=h[i]*k[0][i];out[3]=k[0][3];
        need(draws()[1]==before[1]+1&&viewport.colorCopyCount()==copies+2,"Original Blur did not draw and resolve");
        std::vector<uint32_t> expected(1280*720,blend(out,scene,0x10706));compare(d.readbackColor(color),expected,"Original Blur pixels differ");
        need(viewport.readbackColorTexture(0x82DFE3FC)==d.readbackColor(color),"Original Blur did not keep the blended frame as history");
        need(d.readbackDepth(depth)==sceneDepth,"Original Blur changed depth/stencil");finalState(false);
        uint32_t lastHistory=expected.front();
        // Live off -> on -> off: every disabled frame must refresh history.
        for(bool enabled:{false,true,false}) {
            rt.videoSettings.motionBlur=enabled;
            const auto current=flat(enabled?.7f:.1f,enabled?.3f:.6f,enabled?.2f:.9f);
            const auto currentDraws=draws();const auto currentCopies=viewport.colorCopyCount();
            for(uint32_t i=0;i<4;++i)storeFloat(blur+4*i,std::array<float,4>{.8f,1.2f,.6f,.5f}[i]);storeFloat(blur+16,1);
            invoke(0x82754C90,0x827517C0,camera);
            const auto constants=d.screenEffectConstants();const Pixel previous=unpack(lastHistory);Pixel blurred{};
            for(uint32_t i=0;i<3;++i)blurred[i]=previous[i]*constants[0][i];blurred[3]=constants[0][3];
            const uint32_t want=enabled?blend(blurred,current,0x10706):current;
            compare(d.readbackColor(color),std::vector<uint32_t>(1280*720,want),"Live Motion Blur toggle pixels differ");
            need(draws()[1]==currentDraws[1]+uint64_t(enabled)&&viewport.colorCopyCount()==currentCopies+1,
                 "Live Motion Blur toggle skipped its history resolve or issued a disabled draw");
            need(viewport.readbackColorTexture(0x82DFE3FC)==d.readbackColor(color),"Disabled Motion Blur retained stale history");
            need(d.readbackDepth(depth)==sceneDepth,"Live Motion Blur toggle changed depth/stencil");finalState(false);
            need(std::bit_cast<float>(PPC_LOAD_U32(blur+16))==0,"Live Motion Blur toggle did not reset its request weight");
            lastHistory=want;
        }
        rt.videoSettings.motionBlur=true;
    }
    // Bloom through its parent 82755A70: c1.w above any query value makes the
    // modulate term exactly c1.w regardless of the query texture contents.
    for(bool enabled:{false,true,false}) {
        rt.videoSettings.bloom=enabled;
        const auto scene=flat(.7f,.5f,.3f);const auto before=draws();const auto copies=viewport.colorCopyCount();
        const std::array<float,8> layer{.9f,.6f,.3f,.5f,.2f,.1f,.3f,2.0f};
        for(uint32_t i=0;i<8;++i)storeFloat(bloom+4*i,layer[i]);storeFloat(bloom+32,1);
        invoke(0x82755A70,0x827517C8,camera);
        const auto k=d.screenEffectConstants();
        need(draws()[2]==before[2]+uint64_t(enabled)&&viewport.colorCopyCount()==copies+1,"Live Bloom toggle skipped its resolve or issued a disabled draw");
        need(std::isfinite(k[1][3])&&k[1][3]>=1,"Bloom fixture did not produce a dominant c1.w");
        const Pixel s=unpack(scene);Pixel out{};
        for(uint32_t i=0;i<3;++i)out[i]=std::clamp(s[i]-k[1][i],0.0f,1.0f)*k[1][3]*k[0][i]*k[0][3]+s[i];out[3]=1;
        std::vector<uint32_t> expected(1280*720,enabled?blend(out,scene,0x10001):scene);compare(d.readbackColor(color),expected,"Live Bloom toggle pixels differ");
        need(d.readbackDepth(depth)==sceneDepth,"Original Bloom changed depth/stencil");finalState(false);
        need(std::bit_cast<float>(PPC_LOAD_U32(bloom+32))==0,"Original Bloom parent did not reset its weight");
    }
    rt.videoSettings.bloom=true;
    // Fog through 82751700: far-plane depth exports zero color and alpha.
    for(uint32_t type:{1u,2u,3u}) for(bool enabled:{false,true,false}) {
        rt.videoSettings.atmosphericFog=enabled;
        const auto scene=flat(.4f,.6f,.2f);const auto before=draws();
        // Request: +10 color/alpha, +20 start, +24 end, +28/+2C curve; the original clamps start below end.
        PPC_STORE_U32(fog,type);const std::array<float,11> values{0,0,0,.4f,.6f,.2f,.8f,10,200,.25f,1.5f};
        for(uint32_t i=0;i<11;++i)storeFloat(fog+4+4*i,values[i]);storeFloat(fog+0x30,1);
        invoke(0x82751700,0x82751770,camera);
        need(draws()[3]==before[3]+uint64_t(enabled)&&draws()[0]==before[0],"Live Fog toggle drew an unrelated effect");
        need(PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(type==1?0x82CF2408:type==2?0x82CF2414:0x82CF2420),"Original Fog selected a different shader");
        Pixel s=unpack(scene);s[3]=0;std::vector<uint32_t> expected(1280*720,enabled?pack(s):scene);
        compare(d.readbackColor(color),expected,"Original far-plane Fog pixels differ");
        need(d.readbackDepth(depth)==sceneDepth,"Original Fog changed depth/stencil");finalState(false);
        std::memset(rt.pointer(fog,0x80,true),0,0x80);
    }
    rt.videoSettings.atmosphericFog=true;
    // Dof through 82751700: far-plane depth collapses every tap to the center.
    // Original reciprocal math also permits zero focus distance/range. These
    // inputs intentionally produce infinite c1.z/w, without changing the request.
    // Both zero yields infinite c1.z + NaN c1.w; original shader saturation
    // makes its focus amount zero, preserving a finite center-tap result.
    for(uint32_t edgeCase=0;edgeCase<4;++edgeCase) {
        const auto scene=flat(.3f,.5f,.9f);const auto before=draws();
        std::array<float,12> request{.6f,.8f,.4f,.7f,.5f,2.0f,.25f,0,1,0,0,0};
        if(edgeCase==1)request[4]=0; // far / focusDistance = infinity; focus scale is zero.
        if(edgeCase==2)request[5]=0; // finite focus depth; focus scale divides by zero.
        if(edgeCase==3)request[4]=request[5]=0; // Infinite focus depth; 0/0 focus scale.
        for(uint32_t i=0;i<12;++i)storeFloat(dof+4*i,request[i]);storeFloat(dof+32,1);
        invoke(0x82751700,0x82751770,camera);
        const auto k=d.screenEffectConstants();
        need(draws()[0]==before[0]+1&&draws()[3]==before[3],"Original Dof did not draw once");
        need(k[0][0]>.59f&&k[0][0]<.61f&&k[0][3]>.69f&&k[0][3]<.71f,"Original Dof c0 is not its current layer color");
        if(edgeCase==1)need(std::isinf(k[1][2])&&k[1][3]==0,"Original zero-distance Dof lost its reciprocal coefficients");
        if(edgeCase==2)need(std::isfinite(k[1][2])&&std::isinf(k[1][3]),"Original zero-range Dof lost its reciprocal coefficients");
        if(edgeCase==3)need(std::isinf(k[1][2])&&std::isnan(k[1][3]),"Original combined-zero Dof lost its reciprocal coefficients");
        const Pixel s=unpack(scene);Pixel out{};for(uint32_t i=0;i<3;++i)out[i]=s[i]+k[0][3]*(s[i]*k[0][i]-s[i]);out[3]=1;
        std::vector<uint32_t> expected(1280*720,pack(out));compare(d.readbackColor(color),expected,"Original far-plane Dof pixels differ");
        need(d.readbackDepth(depth)==sceneDepth,"Original Dof changed depth/stencil");finalState(false);
        need(std::bit_cast<float>(PPC_LOAD_U32(dof+32))==0,"Original Dof did not clear its request weight");
    }
    // Live Dof off -> on -> off keeps the original depth and scene resolves,
    // layer reset and cached-state lifecycle; unrelated Fog remains active.
    for(bool enabled:{false,true,false}) {
        rt.videoSettings.depthOfField=enabled;rt.videoSettings.bloom=false;rt.videoSettings.motionBlur=false;
        const auto scene=flat(.3f,.5f,.9f);const auto before=draws();
        const auto copies=viewport.colorCopyCount(),depthCopies=viewport.depthCopyCount();
        const std::array<float,12> request{.6f,.8f,.4f,.7f,.5f,2.0f,.25f,0,1,0,0,0};
        for(uint32_t i=0;i<12;++i)storeFloat(dof+4*i,request[i]);storeFloat(dof+32,1);
        PPC_STORE_U32(fog,1);const std::array<float,11> fogRequest{0,0,0,.4f,.6f,.2f,.8f,10,200,.25f,1.5f};
        for(uint32_t i=0;i<11;++i)storeFloat(fog+4+4*i,fogRequest[i]);storeFloat(fog+0x30,1);
        invoke(0x82751700,0x82751770,camera);
        need(draws()[0]==before[0]+uint64_t(enabled)&&draws()[3]==before[3]+1&&
             viewport.colorCopyCount()==copies+1&&viewport.depthCopyCount()==depthCopies+1,
             "Live Dof toggle changed resolves or disabled another effect");
        const auto k=d.screenEffectConstants();const Pixel s=unpack(scene);Pixel out=s;out[3]=enabled?1.0f:0.0f;
        if(enabled)for(uint32_t i=0;i<3;++i)out[i]=s[i]+k[0][3]*(s[i]*k[0][i]-s[i]);
        compare(d.readbackColor(color),std::vector<uint32_t>(1280*720,pack(out)),"Live Dof toggle pixels differ");
        need(d.readbackDepth(depth)==sceneDepth,"Live Dof toggle changed depth/stencil");finalState(false);
        need(std::bit_cast<float>(PPC_LOAD_U32(dof+32))==0,"Disabled Dof did not clear its request weight");
        std::memset(rt.pointer(fog,0x80,true),0,0x80);
    }
    rt.videoSettings.bloom=rt.videoSettings.depthOfField=rt.videoSettings.motionBlur=true;
    // Letterbox 82756268 (screen command case 5): two flat full-width bars of
    // height clamp(alpha,0,0.5)*2 in clip space, drawn with PSFlat and copy blend.
    for(bool enabled:{false,true,false}) {
        rt.videoSettings.cinematicLetterbox=enabled;
        const auto scene=flat(.3f,.6f,.2f);const auto before=draws();const auto copies=viewport.colorCopyCount();
        const std::array<float,4> bar{.8f,.1f,.5f,.4f};for(uint32_t i=0;i<4;++i)storeFloat(0x50100+4*i,bar[i]);
        c.r4.u32=0x50100;invoke(0x82756268,0x8276E3FC,camera);
        need(draws()[5]==before[5]+(enabled?2u:0u)&&viewport.colorCopyCount()==copies,"Live letterbox toggle lost its original lifecycle");
        need(PPC_LOAD_U32(0x82CD1A6C)==PPC_LOAD_U32(0x82CF231C)&&PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(0x82CF2310),"Original letterbox shader caches differ");
        const float height=std::clamp(bar[3],0.0f,.5f)*2;const uint32_t barWord=pack(bar);
        const auto got=d.readbackColor(color);need(got.size()==1280*720*4,"Letterbox readback extent differs");
        for(uint32_t row=0;row<720;++row){const float y=1.0f-(float(row)+.5f)/360.0f;
            if(std::abs(y-(height-1))<1e-3f||std::abs(y-(1-height))<1e-3f)continue;
            const bool covered=enabled&&(y<=height-1||y>=1-height);std::vector<uint32_t> expected(1280,covered?barWord:scene);
            compare(std::vector<uint8_t>(got.begin()+size_t(row)*1280*4,got.begin()+size_t(row+1)*1280*4),expected,"Original letterbox bar coverage differs");}
        need(d.readbackDepth(depth)==sceneDepth,"Original letterbox changed depth/stencil");finalState(false);
        const auto state=d.effectiveState();c.r4.u32=0x50100;storeFloat(0x5010C,0);invoke(0x82756268,0x8276E3FC,camera);
        need(draws()[5]==before[5]+(enabled?2u:0u),"Transparent letterbox drew");sameState(state,"Transparent letterbox changed state");
    }
    rt.videoSettings.cinematicLetterbox=true;
    // Actual type-zero producer 8276DF38 derives the modulated flag from its
    // input byte at DFC0 and calls 82755FD0 at DFC4 (LR DFC8). The unrelated
    // 8276E344 call site always passes zero and cannot exercise this route.
    {
        const uint32_t producer=PPC_LOAD_U32(0x82DFF28C),entry=0x50180;need(producer!=0,"Corona producer is absent");
        const uint32_t savedEntry=PPC_LOAD_U32(producer+0x48);
        std::memset(rt.pointer(entry,0x20,true),0,0x20);PPC_STORE_U8(entry+0x18,21);PPC_STORE_U8(entry+0x19,5);PPC_STORE_U8(entry+0x1A,1);
        PPC_STORE_U32(producer+0x48,entry);
        const auto scene=flat(.5f,.4f,.7f);const auto before=draws();
        const std::array<float,4> tint{.9f,.7f,.3f,.8f};for(uint32_t i=0;i<4;++i)storeFloat(0x50100+4*i,tint[i]);
        for(const auto [address,word]:std::array<std::array<uint32_t,2>,5>{{
            {0x8276DFAC,0x897D0000},{0x8276DFB8,0x7D6B0034},{0x8276DFBC,0x556BDFFE},
            {0x8276DFC0,0x69650001},{0x8276DFC4,0x4BFE800D}}})
            need(PPC_LOAD_U32(address)==word,"Original modulated producer instructions differ");
        constexpr uint32_t source=0x50300,channel=0x50400,definition=0x50500,request=0x50600,flag=0x50700;
        std::memset(rt.pointer(source,0x500,true),0,0x500);
        PPC_STORE_U32(source+20,channel);PPC_STORE_U32(source+180,request);
        storeFloat(source+188,tint[3]);storeFloat(source+192,1);PPC_STORE_U32(channel+8,definition);
        storeFloat(channel+124,1);storeFloat(definition+68,1);
        for(uint32_t i=0;i<3;++i)storeFloat(request+28+4*i,tint[i]);PPC_STORE_U8(flag,1);
        const auto modulated=[&] {
            c.r4.u32=camera;c.r5.u32=flag;const auto old=abi(c);cpu.invoke(0x8276DF38,source);
            need(abi(c)==old,"Original modulated producer damaged nonvolatile ABI");
        };
        modulated();
        need(draws()[6]==before[6]+1,"Original modulated overlay did not draw");
        need(PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(0x82CF2304),"Original modulated overlay pixel shader cache differs");
        const auto query=viewport.readbackColorTexture(0x82DFE8DC);uint32_t cell{};std::memcpy(&cell,query.data()+4*(5*64+21),4);
        const float visibility=unpack(cell)[0];Pixel out{};for(uint32_t i=0;i<4;++i)out[i]=visibility*tint[i];
        std::vector<uint32_t> expected(1280*720,blend(out,scene,0x10706));compare(d.readbackColor(color),expected,"Original modulated overlay pixels differ");
        need(d.readbackDepth(depth)==sceneDepth,"Original modulated overlay changed depth/stencil");
        // Ready byte zero: 8276AEE0 returns -1 and the original skips the overlay.
        PPC_STORE_U8(entry+0x1A,0);(void)flat(.5f,.4f,.7f);const auto skipDraws=draws();const auto skipScreen=d.screenDrawCount();
        modulated();
        need(draws()==skipDraws&&d.screenDrawCount()==skipScreen,"Unready corona query drew an overlay");
        // No query entry: flag bit0 clears and the existing flat overlay draws.
        PPC_STORE_U32(producer+0x48,0);const auto flatScreen=d.screenDrawCount();
        modulated();
        need(draws()==skipDraws&&d.screenDrawCount()==flatScreen+1&&PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(0x82CF2310),
             "Queryless overlay did not take the original flat path");
        PPC_STORE_U32(producer+0x48,savedEntry);
    }
    // A wrong caller is rejected at its first endpoint, before any state change.
    {
        (void)flat(.5f,.5f,.5f);for(uint32_t i=0;i<4;++i)storeFloat(sat+4*i,.5f);storeFloat(sat+16,1);
        const auto state=d.effectiveState();const auto before=draws();const auto copies=viewport.colorCopyCount();const auto good=c;
        rejects([&]{invoke(0x8276FE60,0x827517C0,camera);});c=good;
        need(draws()==before&&viewport.colorCopyCount()==copies,"Rejected screen-effect caller drew or resolved");
        sameState(state,"Rejected screen-effect caller changed state");std::memset(rt.pointer(sat,0x20,true),0,0x20);
    }
    // A binding reset after the passes accepts their cached original shaders.
    cpu.invoke(0x823EFDA0);(void)flat(1,0,0);
    pixels=d.readbackColor(color);std::memcpy(&word,pixels.data(),4);need(word==0xC00003FF,"Screen draw after screen effects/reset failed");
    size_t offset=0;for(uint32_t a:{dof,blur,bloom,fog,sat}){std::memcpy(rt.pointer(a,0x80,true),layerBackup.data()+offset,0x80);offset+=0x80;}
    rt.engineDriver.reset();
}
}
int main(int argc,char** argv){try{if(argc!=2)throw std::runtime_error("Original image required");exercise(argv[1]);
    std::printf("PASS: %zu original screen-effect pass checks (Dof, Blur, Bloom, Fog x3, Sat, letterbox, query overlay); fixture layers, no gameplay claim\n",checks);return 0;}
catch(const std::exception& e){std::fprintf(stderr,"FAIL after %zu checks: %s\n",checks,e.what());return 1;}}
