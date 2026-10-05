#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include "runtime/engine_viewport_surfaces.h"
#include "renderer/engine_state.h"
#include <array>
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <limits>
namespace {
using namespace Simpsons;
size_t checks{};
void need(bool b,const char* why){++checks;if(!b)throw Failure(why);}
template<class F>void rejects(F f){try{f();}catch(const std::exception&){++checks;return;}throw Failure("Invalid screen bridge request accepted");}
struct Observed{};
struct Abi {uint64_t sp,lr;std::array<uint64_t,18> gpr,fpr;bool operator==(const Abi&)const=default;};
Abi abi(const PPCContext& c){return {c.r1.u64,c.lr,{c.r14.u64,c.r15.u64,c.r16.u64,c.r17.u64,c.r18.u64,c.r19.u64,c.r20.u64,c.r21.u64,c.r22.u64,c.r23.u64,c.r24.u64,c.r25.u64,c.r26.u64,c.r27.u64,c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64},
    {c.f14.u64,c.f15.u64,c.f16.u64,c.f17.u64,c.f18.u64,c.f19.u64,c.f20.u64,c.f21.u64,c.f22.u64,c.f23.u64,c.f24.u64,c.f25.u64,c.f26.u64,c.f27.u64,c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64}};}
std::array<uint32_t,6> caches(uint8_t* base){return {PPC_LOAD_U32(0x82CD1A68),PPC_LOAD_U32(0x82CD1A6C),PPC_LOAD_U32(0x82CD1A70),PPC_LOAD_U32(0x82D0CAB0),PPC_LOAD_U32(0x82D0CAB4),PPC_LOAD_U32(0x82D0CAB8)};}
void exercise(const char* image){
    Runtime rt;rt.load(image);PPCContext startup{};rt.initialize(startup);const auto entry=startup;auto* base=rt.base;
    rt.audioBoundaryObserver=[](uint32_t pc,PPCContext&,uint8_t*){if(pc==0x828166FC)throw Observed{};};
    bool observed=false;try{runOriginal(startup,base);}catch(const Observed&){observed=true;}rt.audioBoundaryObserver={};need(observed,"Original startup checkpoint missing");
    auto& d=*rt.engineDriver;EngineCpuCalls cpu(entry,base);auto& c=cpu.registers();rt.map(0x50000,0x1000,true,"screen fixture arguments");
    const auto camera=PPC_LOAD_U32(0x82E07248),color=PPC_LOAD_U32(0x82D0CB00),depth=PPC_LOAD_U32(0x82D0CAFC);
    const auto texture=PPC_LOAD_U32(0x82E071E8),refs=PPC_LOAD_U32(texture+0x54);
    need(PPC_LOAD_U32(0x82DFEB30)&&PPC_LOAD_U32(0x82DFEB34),"Original screen declarations were not created");
    auto setColor=[&](float r,float g,float b,float a){const std::array<float,4> v={r,g,b,a};for(uint32_t i=0;i<4;++i)PPC_STORE_U32(0x50000+4*i,std::bit_cast<uint32_t>(v[i]));};
    auto draw=[&](uint32_t selector,uint32_t tex,uint32_t cam){
        c.r8.u32=0x50000;c.r9.u32=selector;c.r10.u32=tex;
        c.f1.f64=0;c.f2.f64=0;c.f3.f64=1280;c.f4.f64=720;c.f5.f64=0;c.f6.f64=0;c.f7.f64=1;c.f8.f64=1;
        const auto before=abi(c);cpu.invoke(0x82756480,cam);need(abi(c)==before,"Original screen prefix/epilogue damaged nonvolatile ABI");
    };
    // Below-threshold CPU early-out precedes camera/texture/declaration use.
    setColor(0,0,0,0);const auto cacheBefore=caches(base);const auto count=d.screenDrawCount();const auto stateBefore=d.effectiveState();
    draw(0xFFFFFFFF,0x12345678,0);need(caches(base)==cacheBefore&&d.screenDrawCount()==count,"Original alpha early-out changed caches or submitted a draw");
    for(const auto& f:Graphics::scalarStateEvidence())need(d.effectiveState().scalar(f.id)==stateBefore.scalar(f.id),"Original early-out changed effective state");
    cpu.invoke(0x823F1A18,camera);cpu.invoke(0x826B09A0,0x50040,1);
    // The direct application requests are fixture setup; geometry and shader
    // resources remain those produced by the original AOT CPU routines.
    d.directScalar(base,0x158,0x3F800000);d.directScalar(base,0x15C,0x3F800000);
    const auto z=d.readbackDepth(depth);const auto higher=std::vector<uint8_t>(rt.pointer(0x82D5DB78,0x41D4,false),rt.pointer(0x82D5DB78,0x41D4,false)+0x41D4);
    std::array<uint32_t,4> sentinels{};for(uint32_t i=0;i<4;++i){sentinels[i]=PPC_LOAD_U32(0x82D0CABC+16*i);PPC_STORE_U32(0x82D0CABC+16*i,0xA5510000+i);}
    setColor(0,0,0,1);draw(3,0,camera);need(d.screenDrawCount()==count+1,"Original flat draw was not submitted");
    auto pixels=d.readbackColor(color);bool black=true;for(size_t i=0;i<pixels.size();i+=4){uint32_t word{};std::memcpy(&word,pixels.data()+i,4);black&=word==0xC0000000;}need(black,"Original flat coordinates did not cover the target exactly");
    need(caches(base)==std::array<uint32_t,6>{PPC_LOAD_U32(0x82DFEB30),PPC_LOAD_U32(0x82CF231C),PPC_LOAD_U32(0x82CF2310),0,0,0},"Original flat cache effects differ");
    for(uint32_t i=0;i<4;++i)need(PPC_LOAD_U32(0x82D0CABC+16*i)==0xA5510000+i,"Screen draw overwrote unused stream word");
    setColor(1,1,1,1);draw(0,texture,camera);need(d.screenDrawCount()==count+2,"Original textured draw was not submitted");
    pixels=d.readbackColor(color);bool content=false;for(size_t i=0;i<pixels.size();i+=4){uint32_t word{};std::memcpy(&word,pixels.data()+i,4);content|=(word&0x3FFFFFFF)!=0;}need(content,"Original BC3 artwork produced no nonblack pixels");
    need(PPC_LOAD_U32(texture+0x54)==refs&&d.readbackDepth(depth)==z,"Original screen changed texture references or depth/stencil storage");
    need(!std::memcmp(higher.data(),rt.pointer(0x82D5DB78,0x41D4,false),higher.size()),"Direct screen state overwrote application caches");
    need(d.effectiveState().scalar(Graphics::ScalarState::DepthEnable)==1&&!d.effectiveState().scalar(Graphics::ScalarState::AlphaTest)&&
        !d.effectiveState().scalar(Graphics::ScalarState::ExpandedBlend0)&&!d.effectiveState().scalar(Graphics::ScalarState::Cull),"Original screen post-state differs");
    const auto goodContext=c;const auto goodCaches=caches(base);const auto draws=d.screenDrawCount();
    // Corrupted original CPU shader header and native declaration publication
    // reject before target/cache mutation. Only fixture-owned guest memory changes.
    const auto shader=PPC_LOAD_U32(0x82CF2340),header=shader+0x368;const auto old=PPC_LOAD_U32(header);PPC_STORE_U32(header,old^1);
    rejects([&]{draw(0,texture,camera);});PPC_STORE_U32(header,old);c=goodContext;
    const auto sourceHeader=PPC_LOAD_U32(0x82152884);PPC_STORE_U32(0x82152884,0xFFFFFFF0);
    rejects([&]{draw(0,texture,camera);});PPC_STORE_U32(0x82152884,sourceHeader);c=goodContext;
    const auto decl=PPC_LOAD_U32(0x82DFEB34);PPC_STORE_U32(0x82DFEB34,0x12345678);rejects([&]{draw(0,texture,camera);});PPC_STORE_U32(0x82DFEB34,decl);c=goodContext;
    need(d.screenDrawCount()==draws&&caches(base)==goodCaches&&d.readbackColor(color)==pixels,"Rejected original screen draw altered target or caches");
    // Original8276B0DC skips an unready query, after binding the sprite's
    // vertex inputs and direct states but before selecting its pixel shader.
    PPC_STORE_U8(0x50080,1);PPC_STORE_U32(0x50084,0x500A0);PPC_STORE_U8(0x500BA,0);
    c.r9.u32=0x50080;c.r10.u32=texture;c.f1.f64=640;c.f2.f64=360;
    c.f3.f64=0;c.f4.f64=16;c.f5.f64=16;
    const auto spriteAbi=abi(c);cpu.invoke(0x8276AF78,0x50000);
    need(abi(c)==spriteAbi,"Unready sprite query damaged the original nonvolatile ABI");
    need(d.screenDrawCount()==draws&&d.readbackColor(color)==pixels&&d.readbackDepth(depth)==z,
         "Unready sprite query submitted geometry or altered attachments");
    need(PPC_LOAD_U32(0x82CD1A6C)==PPC_LOAD_U32(0x82CF2340)&&PPC_LOAD_U32(0x82CD1A68)==PPC_LOAD_U32(0x82DFEB34)&&
         PPC_LOAD_U32(0x82CD1A70)==goodCaches[2],"Unready sprite query shader/declaration cache effects differ");
    using S=Graphics::ScalarState;using T=Graphics::SamplerState;
    const auto& spriteState=d.effectiveState();
    need(!spriteState.scalar(S::AlphaTest)&&spriteState.scalar(S::BlendEnable)==1&&spriteState.scalar(S::ExpandedBlend0)==1&&
         !spriteState.scalar(S::DepthEnable)&&!spriteState.scalar(S::DepthWrite)&&!spriteState.scalar(S::Cull),
         "Unready sprite query direct scalar state differs");
    need(spriteState.sampler(0,T::Magnification)==1&&spriteState.sampler(0,T::Minification)==1&&
         spriteState.sampler(0,T::AddressU)==1&&spriteState.sampler(0,T::AddressV)==1,
         "Unready sprite query direct sampler state differs");
    need(!std::memcmp(higher.data(),rt.pointer(0x82D5DB78,0x41D4,false),higher.size()),"Sprite changed application state caches");
    const auto batchAbi=abi(c);cpu.invoke(0x8276B750);
    need(abi(c)==batchAbi&&d.screenDrawCount()==draws,"Sprite batch setup changed nonvolatile ABI or submitted geometry");
    need(d.effectiveState().sampler(1,T::Magnification)==0&&d.effectiveState().sampler(1,T::Minification)==0&&
         d.effectiveState().sampler(1,T::AddressU)==1&&d.effectiveState().sampler(1,T::AddressV)==1,
         "Sprite batch query sampler differs");
    cpu.invoke(0x8276B898);
    need(abi(c)==batchAbi&&!PPC_LOAD_U32(0x82CD1A68)&&d.effectiveState().scalar(S::DepthEnable)==1&&
         d.effectiveState().scalar(S::DepthWrite)==1&&!d.effectiveState().scalar(S::BlendEnable),"Sprite batch cleanup differs");
    // B6B4 reaches the same emitter cleanup without calling AF78 when its
    // texture is zero. Execute the complete original projection/attenuation
    // parent, rather than assigning the return address at the native endpoint.
    for(const auto [address,word]:std::array<std::array<uint32_t,2>,6>{{
        {0x8276B280,0x9421FEC0},{0x8276B598,0x480145F1},{0x8276B6AC,0x815F00A4},
        {0x8276B6B0,0x2B0A0000},{0x8276B6B4,0x419A0024},{0x8276B6D4,0x4BFFF8A5}}})
        need(PPC_LOAD_U32(address)==word,"Original zero-texture emitter instructions differ");
    const auto emitterStorage=rt.allocatePhysical(0,4096,PAGE_READWRITE,0,UINT32_MAX,4096);
    need(emitterStorage!=0,"Original emitter parameter fixture allocation failed");
    std::memset(rt.pointer(emitterStorage,4096,true),0,4096);
    const auto emitter=emitterStorage,emitterDefinition=emitterStorage+0x100,emitterOutput=emitterStorage+0x200;
    const auto emitterFloat=[&](uint32_t at,float value){PPC_STORE_U32(at,std::bit_cast<uint32_t>(value));};
    const auto emitterMatrix=std::vector<uint8_t>(rt.pointer(0x82DFEA50,144,false),rt.pointer(0x82DFEA50,144,false)+144);
    const auto emitterCamera=std::vector<uint8_t>(rt.pointer(camera+0x70,0x14,false),rt.pointer(camera+0x70,0x14,false)+0x14);
    std::memset(rt.pointer(0x82DFEA50,144,true),0,144);
    for(uint32_t row=0;row<3;++row)emitterFloat(0x82DFEA60+16*row+4*row,1);
    for(uint32_t row=0;row<4;++row)emitterFloat(0x82DFEAA0+16*row+4*row,1);
    emitterFloat(camera+0x70,1);emitterFloat(camera+0x74,1);emitterFloat(camera+0x80,.1f);
    emitterFloat(emitter+152,1);emitterFloat(emitter+156,1);emitterFloat(emitter+184,1);
    PPC_STORE_U32(emitter+160,emitterDefinition);emitterFloat(emitterDefinition+28,.05f);
    for(uint32_t lane=0;lane<3;++lane)emitterFloat(emitterDefinition+32+4*lane,1);
    const auto emitterProducer=PPC_LOAD_U32(0x82DFF28C);need(emitterProducer!=0,"Original emitter query manager is absent");
    const auto emitterPreviousQuery=PPC_LOAD_U32(emitterProducer+72);
    const auto emitterPixels=d.readbackColor(color),emitterDepth=d.readbackDepth(depth);
    const auto emitterCaches=caches(base);const auto emitterDraws=d.screenDrawCount();const auto emitterAbi=abi(c);
    need(cpu.invoke(0x8276B270,emitter,camera,emitterOutput)==1,"Original zero-texture emitter did not complete cleanup");
    need(abi(c)==emitterAbi&&d.screenDrawCount()==emitterDraws&&d.readbackColor(color)==emitterPixels&&
         d.readbackDepth(depth)==emitterDepth,"Original zero-texture emitter changed ABI or emitted geometry");
    need(caches(base)==emitterCaches&&!PPC_LOAD_U32(0x82CD1A68)&&
         d.effectiveState().scalar(S::DepthEnable)==1&&d.effectiveState().scalar(S::DepthWrite)==1&&
         !d.effectiveState().scalar(S::BlendEnable)&&d.effectiveState().scalar(S::ExpandedBlend0)==1,
         "Original zero-texture emitter cleanup changed retained shaders or skipped scalar state");
    need(std::bit_cast<float>(PPC_LOAD_U32(emitterOutput+24))>0&&
         std::bit_cast<float>(PPC_LOAD_U32(emitterOutput+28))>0&&
         std::bit_cast<float>(PPC_LOAD_U32(emitterOutput+40))>0,
         "Original zero-texture emitter bypassed projection and output generation");
    std::memcpy(rt.pointer(0x82DFEA50,144,true),emitterMatrix.data(),144);
    std::memcpy(rt.pointer(camera+0x70,0x14,true),emitterCamera.data(),0x14);
    PPC_STORE_U32(emitterProducer+72,emitterPreviousQuery);
    PPC_STORE_U8(0x50080,0);c.r9.u32=0x50080;c.r10.u32=texture;
    c.f1.f64=640;c.f2.f64=360;c.f3.f64=.35;c.f4.f64=32;c.f5.f64=16;
    const auto simpleAbi=abi(c);cpu.invoke(0x8276AF78,0x50000);
    need(abi(c)==simpleAbi&&d.screenDrawCount()==draws+1,"Original rotated sprite CPU quad did not complete");
    need(!d.effectiveState().scalar(S::DepthEnable)&&!d.effectiveState().scalar(S::DepthWrite)&&
         !d.effectiveState().scalar(S::ExpandedBlend0)&&d.readbackDepth(depth)==z,"Original rotated sprite post-state/depth differs");
    // Original producer CPU vertices, ready-byte publication and list reset.
    const auto immediateRow=PPC_LOAD_U32(0x82DFEB20),priorCursor=PPC_LOAD_U32(immediateRow);
    PPC_STORE_U32(immediateRow,PPC_LOAD_U32(immediateRow+4));c.r5.u32=0;
    const auto setupAbi=abi(c);cpu.invoke(0x82751B68,0x500D0);
    need(abi(c)==setupAbi&&PPC_LOAD_U32(0x82D0CAB0)==immediateRow+8&&!PPC_LOAD_U32(0x82D0CAB4)&&
         PPC_LOAD_U32(0x82D0CAB8)==32&&PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(0x82CF1F8C),"Original immediate buffer/setup cache differs");
    cpu.invoke(0x82751DA0,0x500D0);PPC_STORE_U32(immediateRow,priorCursor);
    PPC_STORE_U32(immediateRow,PPC_LOAD_U32(immediateRow+4));c.r5.u32=2;
    const auto dualSetupAbi=abi(c);cpu.invoke(0x82751B68,0x500D0);
    need(abi(c)==dualSetupAbi&&PPC_LOAD_U32(0x500D4)==2&&
         PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(0x82CF1F98),"Original dual immediate setup/mode cache differs");
    cpu.invoke(0x82751DA0,0x500D0);
    need(PPC_LOAD_U32(0x500D4)==2,"Original dual immediate end did not preserve its mode");
    PPC_STORE_U32(0x500D4,0);PPC_STORE_U32(immediateRow,priorCursor);c.r5.u32=0;
    const auto producer=PPC_LOAD_U32(0x82DFF28C);const auto queryDraws=d.coronaQueryDrawCount();
    const auto beforeQuery=d.readbackColor(color);
    PPC_STORE_U32(0x500A0,0);PPC_STORE_U8(0x500B8,0);PPC_STORE_U8(0x500B9,0);PPC_STORE_U8(0x500BA,0);
    c.r4.u32=0x500A0;c.f1.f64=640;c.f2.f64=360;c.f3.f64=.5;c.f4.f64=16;c.f5.f64=8;
    cpu.invoke(0x8276A7F0,producer);
    need(PPC_LOAD_U32(producer+0x44)==0x500A0&&PPC_LOAD_U32(producer+0x4C)==1,"Original corona query queue differs");
    const auto queryAbi=abi(c);cpu.invoke(0x8276A820,producer);
    need(abi(c)==queryAbi&&d.coronaQueryDrawCount()==queryDraws+1,"Original corona CPU draw or ABI differs");
    need(PPC_LOAD_U8(0x500BA)==1&&!PPC_LOAD_U32(producer+0x44)&&!PPC_LOAD_U32(producer+0x4C),"Original corona ready/list reset differs");
    need(d.readbackColor(color)==beforeQuery&&d.readbackDepth(depth)==z,"Original corona producer changed scene attachments");
    need(!d.effectiveState().scalar(S::BlendEnable)&&d.effectiveState().scalar(S::DepthEnable)&&d.effectiveState().scalar(S::DepthWrite),"Corona producer post-state differs");
    const auto emptyAbi=abi(c);cpu.invoke(0x8276A820,producer);
    need(abi(c)==emptyAbi&&d.coronaQueryDrawCount()==queryDraws+1,"Empty original corona list submitted a draw");
    PPC_STORE_U8(0x50080,1);c.r9.u32=0x50080;c.r10.u32=texture;
    c.f1.f64=640;c.f2.f64=360;c.f3.f64=.35;c.f4.f64=32;c.f5.f64=16;
    const auto visibleAbi=abi(c);cpu.invoke(0x8276AF78,0x50000);
    need(abi(c)==visibleAbi&&d.screenDrawCount()==draws+2&&PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(0x82CF23E0),"Ready original corona sprite did not select and draw its exact shader");
    const auto savedHeader=PPC_LOAD_U32(immediateRow+8);PPC_STORE_U32(immediateRow+8,savedHeader^1);
    const auto resetContext=c;rejects([&]{cpu.invoke(0x823EFDA0);});c=resetContext;PPC_STORE_U32(immediateRow+8,savedHeader);
    cpu.invoke(0x823EFDA0);need(!PPC_LOAD_U32(0x82CD1A6C)&&!PPC_LOAD_U32(0x82CD1A70)&&!PPC_LOAD_U32(0x82CD1A68)&&
         !PPC_LOAD_U32(0x82D0CAB0),"Original reset did not accept owned screen/immediate-buffer bindings");
    // A projected billboard (modes 4 and 6) leaves screen slots 22 (VS), 23 and 24 (PS) cached. The next device reset must accept
    // those same shared shader objects (live neverquest: it failed on the cached object address, so the process died).
    need(PPC_LOAD_U32(0x82CF1FC8)&&PPC_LOAD_U32(0x82CF1FA4)&&PPC_LOAD_U32(0x82CF1FB0),"Projected billboard shader objects are not published in the fixture");
    for(const uint32_t pixelField:{0x82CF1FA4u,0x82CF1FB0u}) {
        PPC_STORE_U32(0x82CD1A6C,PPC_LOAD_U32(0x82CF1FC8));PPC_STORE_U32(0x82CD1A70,PPC_LOAD_U32(pixelField));
        cpu.invoke(0x823EFDA0);
        need(!PPC_LOAD_U32(0x82CD1A6C)&&!PPC_LOAD_U32(0x82CD1A70),"Original reset did not accept a projected billboard shader pair");
    }
    // A cached shader that is neither a native material nor one of the shared screen shaders still rejects.
    {const auto foreign=PPC_LOAD_U32(0x82CF1FC8)^0x10;PPC_STORE_U32(0x82CD1A6C,foreign);PPC_STORE_U32(0x82CD1A70,0);
     const auto foreignContext=c;rejects([&]{cpu.invoke(0x823EFDA0);});c=foreignContext;PPC_STORE_U32(0x82CD1A6C,0);}
    // Gameplay color overlay 82755FD0 used to enter the console immediate
    // allocator and stall after 827561F4, with audio/window threads still alive.
    setColor(0,0,0,1);draw(3,0,camera);
    const auto overlayStart=d.screenDrawCount();const auto overlayCaches=caches(base);
    const auto overlayState=d.effectiveState();
    const auto overlayDepth=d.readbackDepth(depth);
    auto overlay=[&](uint32_t flags){
        c.r4.u32=0x50000;c.r5.u32=flags;
        const auto before=abi(c);cpu.invoke(0x82755FD0,camera);
        need(abi(c)==before,"Original overlay prefix/epilogue damaged nonvolatile ABI");
    };
    setColor(1,0,0,0);overlay(1); // Alpha early-out precedes query/resource use.
    need(d.screenDrawCount()==overlayStart&&caches(base)==overlayCaches,"Invisible overlay changed caches or drew");
    for(const auto& f:Graphics::scalarStateEvidence())need(d.effectiveState().scalar(f.id)==overlayState.scalar(f.id),"Invisible overlay changed state");
    std::fprintf(stderr,"[OVERLAY REGRESSION] drawing original 82755FD0\n");std::fflush(stderr);
    setColor(1,0,0,.5f);overlay(0);
    need(d.screenDrawCount()==overlayStart+1,"Original gameplay overlay was not submitted");
    pixels=d.readbackColor(color);
    for(size_t i=0;i<pixels.size();i+=4){uint32_t word{};std::memcpy(&word,pixels.data()+i,4);need(word==0x80000200,"Overlay rectangle coverage or alpha blend differs");}
    need(d.readbackDepth(depth)==overlayDepth,"Overlay changed depth/stencil pixels");
    need(caches(base)==std::array<uint32_t,6>{PPC_LOAD_U32(0x82DFEB30),PPC_LOAD_U32(0x82CF231C),PPC_LOAD_U32(0x82CF2310),0,0,0},"Overlay cache publication differs");
    need(d.effectiveState().scalar(S::DepthEnable)==1&&!d.effectiveState().scalar(S::AlphaTest)&&
         !d.effectiveState().scalar(S::BlendEnable)&&!d.effectiveState().scalar(S::ExpandedBlend0)&&
         d.effectiveState().scalar(S::HalfPixelOffset)==1,"Overlay post-state differs");
    for(uint32_t i=0;i<4;++i)need(PPC_LOAD_U32(0x82D0CABC+16*i)==0xA5510000+i,"Overlay changed unused stream fields");
    const auto overlayGood=c;const auto overlayPixels=pixels;const auto postOverlayCaches=caches(base);
    setColor(std::numeric_limits<float>::quiet_NaN(),0,0,1);rejects([&]{overlay(0);});c=overlayGood;
    need(d.screenDrawCount()==overlayStart+1&&d.readbackColor(color)==overlayPixels&&caches(base)==postOverlayCaches,"Invalid overlay mutated output/caches");
    // A subsequent ordinary screen draw verifies the retained native state.
    setColor(0,0,1,1);draw(3,0,camera);
    pixels=d.readbackColor(color);uint32_t finalPixel{};std::memcpy(&finalPixel,pixels.data(),4);
    need(finalPixel==0xFFF00000,"Screen draw after gameplay overlay failed");
    // 82773B30 previously stalled in the console immediate allocator. Its
    // rectangle replaces alpha with zero without touching any RGB/depth bits.
    setColor(1,1,1,1);draw(0,texture,camera);
    const auto alphaPixels=d.readbackColor(color),alphaDepth=d.readbackDepth(depth);
    const auto alphaDraws=d.screenDrawCount();const auto alphaAbi=abi(c);
    const auto alphaState=d.effectiveState();const auto alphaCaches=caches(base);
    const auto alphaFlags=PPC_LOAD_U32(0x82D6CCA8);
    const auto alphaContext=c;const auto flatDecl=PPC_LOAD_U32(0x82DFEB30);
    PPC_STORE_U32(0x82DFEB30,0x12345678);rejects([&]{cpu.invoke(0x82773B30);});
    PPC_STORE_U32(0x82DFEB30,flatDecl);c=alphaContext;
    need(d.screenDrawCount()==alphaDraws&&d.readbackColor(color)==alphaPixels&&caches(base)==alphaCaches&&
         PPC_LOAD_U32(0x82D6CCA8)==alphaFlags,"Rejected alpha clear changed pixels/caches/flags");
    for(const auto& f:Graphics::scalarStateEvidence())need(d.effectiveState().scalar(f.id)==alphaState.scalar(f.id),"Rejected alpha clear changed effective state");
    cpu.invoke(0x82773B30);
    need(abi(c)==alphaAbi&&d.screenDrawCount()==alphaDraws+1,"Original alpha clear did not finish with preserved ABI");
    pixels=d.readbackColor(color);
    for(size_t i=0;i<pixels.size();i+=4){uint32_t before{},after{};std::memcpy(&before,alphaPixels.data()+i,4);std::memcpy(&after,pixels.data()+i,4);
        need(after==(before&0x3FFFFFFF),"Post alpha clear changed RGB or left nonzero alpha");}
    need(d.readbackDepth(depth)==alphaDepth,"Post alpha clear changed depth/stencil");
    need(PPC_LOAD_U32(0x82D6CCA8)==(alphaFlags|2),"Original post alpha CPU flag store differs");
    auto expectedAlpha=alphaState;
    expectedAlpha.setScalar(S::ColorMask0,8);expectedAlpha.setScalar(S::DepthEnable,1);expectedAlpha.setScalar(S::DepthWrite,0);
    expectedAlpha.setScalar(S::Cull,2);expectedAlpha.setScalar(S::AlphaTest,0);expectedAlpha.setScalar(S::BlendEnable,0);
    expectedAlpha.setScalar(S::SlopeBias,0x3F000000);expectedAlpha.setScalar(S::DepthBias,0x37D1B717);
    for(const auto& f:Graphics::scalarStateEvidence())need(d.effectiveState().scalar(f.id)==expectedAlpha.scalar(f.id),"Post alpha clear retained state differs");
    need(caches(base)==std::array<uint32_t,6>{PPC_LOAD_U32(0x82DFEB30),PPC_LOAD_U32(0x82CF231C),PPC_LOAD_U32(0x82CF2310),alphaCaches[3],alphaCaches[4],alphaCaches[5]},"Post alpha clear cache effects differ");
    // The original post-filter's invisible branch restores color mask/bias
    // and clears the alpha-pass flag even when no post quad is necessary.
    setColor(0,0,0,0);c.r4.u32=0x50000;c.r6.u32=0;c.r9.u32=0;c.lr=0x8276E1C4;
    cpu.invoke(0x82773D40,camera);
    need(!(PPC_LOAD_U32(0x82D6CCA8)&2)&&d.effectiveState().scalar(S::ColorMask0)==15&&
         !d.effectiveState().scalar(S::DepthBias)&&!d.effectiveState().scalar(S::SlopeBias),"Original post-filter cleanup did not restore alpha-pass state");
    setColor(0,0,1,1);draw(3,0,camera);pixels=d.readbackColor(color);std::memcpy(&finalPixel,pixels.data(),4);
    need(finalPixel==0xFFF00000,"Screen draw after alpha preparation/cleanup failed");
    // Ball Homer's 82771A18 must complete its original animated UV and quad
    // stores without entering the null console allocator. Use real startup
    // texture/shader/declaration owners and fixture-owned effect parameters.
    const uint32_t ball=0x50100,definition=0x50200,system=0x50300,timeline=0x50400;
    auto storeFloat=[&](uint32_t address,float value){PPC_STORE_U32(address,std::bit_cast<uint32_t>(value));};
    const auto matrixBytes=std::vector<uint8_t>(rt.pointer(0x82DFEA60,128,false),rt.pointer(0x82DFEA60,128,false)+128);
    for(unsigned i=0;i<32;++i)storeFloat(0x82DFEA60+4*i,(i%16)%5==0?1.0f:0.0f);
    PPC_STORE_U32(ball+20,system);PPC_STORE_U16(ball+30,0);PPC_STORE_U32(system+8,timeline);
    storeFloat(system+124,1);storeFloat(timeline+68,1);storeFloat(timeline+72,0);
    storeFloat(ball+144,0);storeFloat(ball+148,0);storeFloat(ball+152,.5f);storeFloat(ball+156,1);
    PPC_STORE_U32(ball+164,definition);storeFloat(ball+168,.75f);storeFloat(ball+172,.5f);
    storeFloat(ball+176,.4f);storeFloat(ball+180,1);PPC_STORE_U32(ball+184,texture);
    PPC_STORE_U8(definition+17,1);PPC_STORE_U32(definition+20,2);
    storeFloat(definition+76,1);storeFloat(definition+80,.5f);
    PPC_STORE_U16(definition+84,2);PPC_STORE_U16(definition+86,2);
    storeFloat(definition+88,.5f);storeFloat(definition+92,.5f);
    const auto ballAbi=abi(c);cpu.invoke(0x82771960);
    need(abi(c)==ballAbi,"Original Ball effect setup damaged nonvolatile ABI");
    d.directScalar(base,uint32_t(S::DepthWrite),0);d.directScalar(base,uint32_t(S::DepthCompare),7);
    d.directScalar(base,uint32_t(S::Cull),0);
    const auto ballBefore=d.readbackColor(color),ballDepth=d.readbackDepth(depth);
    std::vector<uint32_t> effectStaging,priorPhysical;
    auto snapshotPhysical=[&]{priorPhysical.clear();for(const auto& a:rt.physicalAllocations)priorPhysical.push_back(a.address);};
    auto recordStaging=[&]{const auto before=effectStaging.size();
        for(const auto& a:rt.physicalAllocations)if(std::find(priorPhysical.begin(),priorPhysical.end(),a.address)==priorPhysical.end()){
            need(a.size==4096,"Effect allocated an unexpected guest physical range");effectStaging.push_back(a.address);}
        need(effectStaging.size()==before+1,"Effect CPU staging allocation was not uniquely identified");};
    snapshotPhysical();
    const auto ballDraws=d.ballEffectDrawCount(),ballScreenDraws=d.screenDrawCount();
    const auto ballContext=c;const auto ballDecl=PPC_LOAD_U32(0x82DFEB34);
    PPC_STORE_U32(0x82DFEB34,0x12345678);rejects([&]{cpu.invoke(0x82771A18,ball);});
    PPC_STORE_U32(0x82DFEB34,ballDecl);c=ballContext;
    need(d.ballEffectDrawCount()==ballDraws&&d.readbackColor(color)==ballBefore,
         "Rejected Ball effect changed draw output");
    cpu.invoke(0x82771A18,ball);recordStaging();
    need(abi(c)==ballAbi&&d.ballEffectDrawCount()==ballDraws+1&&d.screenDrawCount()==ballScreenDraws,
         "Original Ball effect quad/ABI did not complete");
    need(d.readbackColor(color)!=ballBefore&&d.readbackDepth(depth)==ballDepth,
         "Ball effect failed to render or changed read-only depth");
    need(PPC_LOAD_U32(0x82CD1A6C)==PPC_LOAD_U32(0x82CF25B4)&&PPC_LOAD_U32(0x82CD1A70)==PPC_LOAD_U32(0x82CF2584)&&
         PPC_LOAD_U32(0x82CD1A68)==ballDecl,"Original Ball effect shader/declaration cache differs");
    // Check that its original final texture clear actually ran.
    const auto ballRaster=PPC_LOAD_U32(texture),stageRaster=PPC_LOAD_U32(0x82D0E3F8);
    PPC_STORE_U32(0x82D0E3F8,ballRaster);rejects([&]{d.preflightNullRaster(base,ballRaster,0);});
    PPC_STORE_U32(0x82D0E3F8,stageRaster);
    // A reset following the newly retained shaders must accept their real
    // owners, then an ordinary screen draw must still work.
    cpu.invoke(0x823EFDA0);
    setColor(0,0,1,1);draw(3,0,camera);pixels=d.readbackColor(color);std::memcpy(&finalPixel,pixels.data(),4);
    need(finalPixel==0xFFF00000,"Screen draw after Ball effect/reset failed");
    // Exercise the original parent, including every pass after the sprite:
    // preserve scene, render the mask, filter twice, encode, restore, composite.
    // A successful isolated sprite must not hide the next console-only call.
    auto& viewport=d.viewportSurfaces();
    const auto phaseHead=PPC_LOAD_U32(0x82DFF580),phaseNext=PPC_LOAD_U32(ball+0xA0);
    PPC_STORE_U32(ball+0xA0,0);PPC_STORE_U32(0x82DFF580,0);
    const auto emptyPhasePixels=d.readbackColor(color),emptyPhaseDepth=d.readbackDepth(depth);
    const auto emptyPhaseCaches=caches(base);const auto emptyPhaseState=d.effectiveState();
    const auto phaseCount=d.distortionPhaseCount(),phaseDraws=d.distortionDrawCount();
    const auto phaseCopies=viewport.colorCopyCount(),phaseSprites=d.ballEffectDrawCount();
    c.lr=0x827517A8;const auto emptyPhaseAbi=abi(c);cpu.invoke(0x82772468,camera);
    need(abi(c)==emptyPhaseAbi&&d.distortionPhaseCount()==phaseCount&&d.distortionDrawCount()==phaseDraws&&
         viewport.colorCopyCount()==phaseCopies&&d.ballEffectDrawCount()==phaseSprites,
         "Empty original Ball phase changed ABI or submitted work");
    need(caches(base)==emptyPhaseCaches&&d.readbackColor(color)==emptyPhasePixels&&d.readbackDepth(depth)==emptyPhaseDepth,
         "Empty original Ball phase changed caches or attachments");
    for(const auto& f:Graphics::scalarStateEvidence())need(d.effectiveState().scalar(f.id)==emptyPhaseState.scalar(f.id),
         "Empty original Ball phase changed effective state");

    setColor(1,1,1,1);draw(0,texture,camera);
    d.directScalar(base,uint32_t(S::DepthWrite),0);d.directScalar(base,uint32_t(S::DepthCompare),7);
    const auto phaseSource=d.readbackColor(color),phaseDepth=d.readbackDepth(depth);
    const auto phaseBinding=d.cameraBinding();
    const auto phaseClears=d.cameraClearCount(),phasePresents=d.presentationCount(),phaseFrontCopies=d.frontCopyCount();
    const auto phaseFlags=PPC_LOAD_U32(0x82D6CCA8);
    PPC_STORE_U32(0x82DFF580,ball);c.lr=0x827517A8;
    snapshotPhysical();
    const auto phaseAbi=abi(c);cpu.invoke(0x82772468,camera);recordStaging();
    need(abi(c)==phaseAbi&&d.distortionPhaseCount()==phaseCount+1&&d.distortionDrawCount()==phaseDraws+5&&
         d.ballEffectDrawCount()==phaseSprites+1&&viewport.colorCopyCount()==phaseCopies+5,
         "Original Ball phase did not complete its sprite, five quads and five resolves with preserved ABI");
    need(viewport.readbackColorTexture(0x82DFE360)==phaseSource,
         "Original Ball phase failed to preserve the exact scene before mask rendering");
    const auto mask=viewport.readbackColorTexture(0x82DFE498);
    const auto filtered=viewport.readbackColorTexture(0x82DFE708);
    const auto encoded=viewport.readbackColorTexture(0x82DFE7A4);
    const auto nonzero=[](const auto& bytes){for(auto value:bytes)if(value)return true;return false;};
    need(mask.size()==phaseSource.size()&&nonzero(mask)&&nonzero(filtered)&&nonzero(encoded),
         "Original Ball phase failed to publish its mask/filter/encode snapshots");
    const auto distorted=d.readbackColor(color);bool displacedRgb=false;
    for(size_t i=0;i<distorted.size();i+=4){uint32_t before{},after{};
        std::memcpy(&before,phaseSource.data()+i,4);std::memcpy(&after,distorted.data()+i,4);
        displacedRgb|=((before^after)&0x3FFFFFFF)!=0;}
    need(displacedRgb&&d.readbackDepth(depth)==phaseDepth,
         "Original Ball distortion did not displace scene RGB or changed depth/stencil");
    const auto afterPhaseBinding=d.cameraBinding();
    need(afterPhaseBinding.camera==phaseBinding.camera&&afterPhaseBinding.colorIdentity==phaseBinding.colorIdentity&&
         afterPhaseBinding.depthIdentity==phaseBinding.depthIdentity&&afterPhaseBinding.viewport==phaseBinding.viewport,
         "Original Ball target stack did not restore the main camera attachments and viewport");
    need(caches(base)==std::array<uint32_t,6>{ballDecl,PPC_LOAD_U32(0x82CF2340),PPC_LOAD_U32(0x82CF25A8),
         emptyPhaseCaches[3],emptyPhaseCaches[4],emptyPhaseCaches[5]},"Original Ball phase final shader/declaration/stream caches differ");
    need(d.effectiveState().scalar(S::DepthEnable)==1&&d.effectiveState().scalar(S::DepthWrite)==1&&
         !d.effectiveState().scalar(S::BlendEnable),"Original Ball phase final depth/blend state differs");
    need(PPC_LOAD_U32(0x82D6CCA8)==phaseFlags&&PPC_LOAD_U32(0x82DFF580)==ball&&!PPC_LOAD_U32(ball+0xA0)&&
         d.cameraClearCount()==phaseClears&&d.presentationCount()==phasePresents&&d.frontCopyCount()==phaseFrontCopies,
         "Original Ball phase changed owner flags/list or unrelated clear/presentation counters");
    // Zero opacity still executes the complete graph. Its output must use the
    // new scene and newly cleared mask, not any preceding frame's distortion.
    cpu.invoke(0x823EFDA0);setColor(0,0,1,1);draw(3,0,camera);
    d.directScalar(base,uint32_t(S::DepthWrite),0);d.directScalar(base,uint32_t(S::DepthCompare),7);
    const auto zeroPhaseSource=d.readbackColor(color);storeFloat(ball+172,0);
    c.lr=0x827517A8;const auto zeroPhaseAbi=abi(c);cpu.invoke(0x82772468,camera);
    need(abi(c)==zeroPhaseAbi&&d.distortionPhaseCount()==phaseCount+2&&d.distortionDrawCount()==phaseDraws+10&&
         viewport.colorCopyCount()==phaseCopies+10&&d.ballEffectDrawCount()==phaseSprites+2,
         "Repeated zero-opacity Ball phase failed to complete the original graph");
    need(viewport.readbackColorTexture(0x82DFE360)==zeroPhaseSource&&
         !nonzero(viewport.readbackColorTexture(0x82DFE498))&&d.readbackDepth(depth)==phaseDepth,
         "Repeated Ball phase reused stale scene/mask data or changed read-only depth");
    // PS82156340 emits zero alpha for zero mask.B. The original 0x10706
    // blend preserves destination RGB while replacing alpha with source zero.
    pixels=d.readbackColor(color);
    need(d.effectiveState().scalar(S::ColorMask0)==15,"Original Ball phase color write mask differs");
    for(size_t i=0;i<pixels.size();i+=4){uint32_t before{},after{};
        std::memcpy(&before,zeroPhaseSource.data()+i,4);std::memcpy(&after,pixels.data()+i,4);
        need(after==(before&0x3FFFFFFF),"Zero-opacity Ball composite changed scene RGB or retained nonzero alpha");}
    storeFloat(ball+172,.5f);PPC_STORE_U32(0x82DFF580,phaseHead);PPC_STORE_U32(ball+0xA0,phaseNext);
    cpu.invoke(0x823EFDA0);setColor(1,0,0,1);draw(3,0,camera);
    pixels=d.readbackColor(color);std::memcpy(&finalPixel,pixels.data(),4);
    need(finalPixel==0xC00003FF&&d.readbackDepth(depth)==phaseDepth,
         "Original screen draw after repeated Ball distortion phases failed");
    // Original 82770AF0 resolves the scene, then draws PS821559D8 once through
    // its three add/sub/lerp luma layers. Layer tests, weight normalization,
    // strip stores and resets are original CPU work on fixture layer values.
    const auto lumaLayers=std::vector<uint8_t>(rt.pointer(0x82CF24F0,0x90,false),rt.pointer(0x82CF24F0,0x90,false)+0x90);
    auto lumaLayer=[&](uint32_t index,std::array<float,4> color,float x,float y,float weight){const uint32_t layer=0x82CF24F0+0x30*index;
        for(uint32_t i=0;i<4;++i)storeFloat(layer+4*i,color[i]);storeFloat(layer+16,x);storeFloat(layer+20,y);
        PPC_STORE_U32(layer+24,0);PPC_STORE_U32(layer+28,0);storeFloat(layer+32,weight);};
    auto lumaPass=[&](uint32_t lr){c.lr=lr;const auto before=abi(c);cpu.invoke(0x82770AF0,camera);need(abi(c)==before,"Original luma pass damaged nonvolatile ABI");};
    // Independent reference for one flat RGB10A2 scene pixel; alpha exports one.
    using LumaBank=std::array<std::array<float,4>,6>;
    auto lumaReference=[](uint32_t scene,const LumaBank& k){
        const std::array<float,3> rgb={float(scene&1023)/1023.0f,float(scene>>10&1023)/1023.0f,float(scene>>20&1023)/1023.0f};
        const float l=std::clamp(rgb[2]*.11f+rgb[0]*.3f+rgb[1]*.59f,0.0f,1.0f);
        const float spread=l*2*(1-l),doubled=spread+spread,centered=l*2-1;
        auto weight=[&](float x,float y){const float x5=x*x*x*x*x,keep=1-x5*x5,bend=y*-.5f*y*y*keep;
            return std::clamp(centered*(((.25f-bend*bend)*-y*keep)*doubled+bend)+(l+(spread*x+x5)),0.0f,1.0f);};
        const float added=weight(k[1][0],k[1][1])*k[0][3],subtracted=k[2][3]*weight(k[3][0],k[3][1]),lerp=weight(k[5][0],k[5][1]);
        uint32_t result=3u<<30;
        for(uint32_t i=0;i<3;++i){const float a=added*k[0][i]+rgb[i],s=a-subtracted*k[2][i],v=(lerp*k[4][i]-s)*k[4][3]+s;
            result|=uint32_t(std::nearbyint(std::clamp(v,0.0f,1.0f)*1023))<<(10*i);}
        return result;};
    auto lumaCompare=[&](const std::vector<uint8_t>& pixels,uint32_t expected){
        for(size_t i=0;i<pixels.size();i+=4){uint32_t got{};std::memcpy(&got,pixels.data()+i,4);
            for(uint32_t channel=0;channel<4;++channel){const uint32_t mask=channel==3?3:1023;
                const int a=int(got>>(10*channel)&mask),b=int(expected>>(10*channel)&mask);
                need(std::abs(a-b)<=(channel==3?0:1),"Original luma layers differ from the independent reference");}}};
    const auto lumaDraws=d.lumaDrawCount(),lumaCopies=viewport.colorCopyCount();
    for(uint32_t i=0;i<3;++i)lumaLayer(i,{0,0,0,0},0,0,0);
    setColor(.5f,.25f,.75f,1);draw(3,0,camera);
    const auto lumaScene=d.readbackColor(color),lumaDepth=d.readbackDepth(depth);
    const auto lumaState=d.effectiveState();const auto lumaCaches=caches(base);
    lumaPass(0x827517B0);
    need(d.lumaDrawCount()==lumaDraws&&viewport.colorCopyCount()==lumaCopies&&d.readbackColor(color)==lumaScene&&caches(base)==lumaCaches,
         "Inactive original luma layers resolved, drew or changed caches");
    for(const auto& f:Graphics::scalarStateEvidence())need(d.effectiveState().scalar(f.id)==lumaState.scalar(f.id),"Inactive luma layers changed state");
    // Unnormalized fixture layers: the original CPU divides add/sub by weight.
    lumaLayer(0,{.4f,.2f,.1f,1},.6f,.4f,2);lumaLayer(1,{.05f,.025f,.1f,.125f},.25f,-.125f,.5f);lumaLayer(2,{.9f,.3f,.1f,.4f},.7f,.6f,3);
    const LumaBank activeBank={{{.2f,.1f,.05f,.5f},{.3f,.2f,0,0},{.1f,.05f,.2f,.25f},{.5f,-.25f,0,0},{.9f,.3f,.1f,.4f},{.7f,.6f,0,0}}};
    // An unqualified caller is rejected at the first endpoint, before any work.
    const auto lumaContext=c;rejects([&]{lumaPass(0x827517A8);});c=lumaContext;
    need(d.lumaDrawCount()==lumaDraws&&viewport.colorCopyCount()==lumaCopies&&d.readbackColor(color)==lumaScene,"Rejected luma caller changed output");
    for(const auto& f:Graphics::scalarStateEvidence())need(d.effectiveState().scalar(f.id)==lumaState.scalar(f.id),"Rejected luma caller changed state");
    uint32_t lumaSceneWord{};std::memcpy(&lumaSceneWord,lumaScene.data(),4);
    snapshotPhysical();lumaPass(0x827517B0);recordStaging();
    need(d.lumaDrawCount()==lumaDraws+1&&viewport.colorCopyCount()==lumaCopies+1&&viewport.readbackColorTexture(0x82DFE360)==lumaScene,
         "Original luma pass did not resolve the exact scene and draw once");
    need(lumaReference(lumaSceneWord,activeBank)!=lumaSceneWord,"Luma fixture layers have no visible effect");
    lumaCompare(d.readbackColor(color),lumaReference(lumaSceneWord,activeBank));
    need(d.readbackDepth(depth)==lumaDepth,"Original luma pass changed depth/stencil");
    need(caches(base)==std::array<uint32_t,6>{PPC_LOAD_U32(0x82DFEB30),PPC_LOAD_U32(0x82CF234C),PPC_LOAD_U32(0x82CF24E4),lumaCaches[3],lumaCaches[4],lumaCaches[5]},
         "Original luma shader/declaration cache effects differ");
    need(d.effectiveState().scalar(S::DepthEnable)==1&&!d.effectiveState().scalar(S::BlendEnable)&&!d.effectiveState().scalar(S::Cull)&&
         !d.effectiveState().scalar(S::AlphaTest)&&d.effectiveState().scalar(S::HalfPixelOffset)==1&&d.effectiveState().effectiveBlend(0)==0x10001&&
         d.effectiveState().sampler(0,T::AddressU)==2&&d.effectiveState().sampler(0,T::AddressV)==2&&
         d.effectiveState().sampler(0,T::Magnification)==1&&d.effectiveState().sampler(0,T::Minification)==1,"Original luma post-state differs");
    // Add/sub colors and curves scale by zero; every weight, including lerp's, clears.
    for(uint32_t i=0;i<3;++i){const uint32_t layer=0x82CF24F0+0x30*i;
        for(uint32_t lane:{0u,1u,2u,3u,4u,5u,8u})if(i<2||lane==8)
            need(std::bit_cast<float>(PPC_LOAD_U32(layer+4*lane))==0.0f,"Original luma pass did not reset its layers");}
    need(std::bit_cast<float>(PPC_LOAD_U32(0x82CF2550))==.9f&&std::bit_cast<float>(PPC_LOAD_U32(0x82CF2564))==.6f,
         "Original luma pass changed retained lerp color/curve");
    // Only the subtract layer: add/lerp colors become the default zero while
    // their retained curves stay in c1/c5 and must cancel exactly.
    setColor(.5f,.25f,.75f,1);draw(3,0,camera);
    lumaLayer(1,{.4f,.8f,.2f,1},.25f,-.5f,1);
    const LumaBank subtractBank={{{0,0,0,0},{.3f,.2f,0,0},{.4f,.8f,.2f,1},{.25f,-.5f,0,0},{0,0,0,0},{.7f,.6f,0,0}}};
    lumaPass(0x827517B0);
    need(d.lumaDrawCount()==lumaDraws+2&&viewport.colorCopyCount()==lumaCopies+2,"Single-layer original luma pass did not draw once");
    need(lumaReference(lumaSceneWord,subtractBank)!=lumaSceneWord,"Single-layer luma fixture has no visible subtraction");
    lumaCompare(d.readbackColor(color),lumaReference(lumaSceneWord,subtractBank));
    need(d.readbackDepth(depth)==lumaDepth,"Single-layer luma pass changed depth/stencil");
    std::memcpy(rt.pointer(0x82CF24F0,0x90,true),lumaLayers.data(),0x90);
    cpu.invoke(0x823EFDA0);setColor(1,0,0,1);draw(3,0,camera);
    pixels=d.readbackColor(color);std::memcpy(&finalPixel,pixels.data(),4);
    need(finalPixel==0xC00003FF,"Original screen draw after luma layers failed");
    // Bounded trail/billboard bridge fixtures start from the scalar-zero state
    // left by the original Ball/immediate epilogues. The original CB80 command
    // still selects additive blending, independently of that retained shadow.
    rt.map(0x51000,0x2000,true,"immediate state regression owners");
    const auto immediateContext=c;const auto immediateMatrix=std::vector<uint8_t>(rt.pointer(0x82DFEAE0,64,false),rt.pointer(0x82DFEAE0,64,false)+64);
    const auto immediateCursor=PPC_LOAD_U32(immediateRow);
    for(uint32_t row=0;row<4;++row)for(uint32_t lane=0;lane<4;++lane)storeFloat(0x82DFEAE0+row*16+lane*4,row==lane?1.0f:0.0f);
    auto fillImmediateQuad=[&](uint32_t output){
        constexpr std::array<std::array<float,8>,4> vertices={{{-.75f,.75f,.5f,1,0,0,0,0},{-.75f,-.75f,.5f,1,0,1,0,1},{.75f,.75f,.5f,1,1,0,1,0},{.75f,-.75f,.5f,1,1,1,1,1}}};
        for(uint32_t i=0;i<4;++i)for(uint32_t j=0;j<8;++j)storeFloat(output+i*32+j*4,vertices[i][j]);
    };
    for(bool billboard:{false,true}){
        c=immediateContext;setColor(0,0,0,1);draw(3,0,camera);
        d.directScalar(base,uint32_t(S::DepthCompare),7);d.directScalar(base,uint32_t(S::BlendEnable),0);
        const auto beforeDepth=d.readbackDepth(depth);const uint32_t frameBytes=billboard?0x1F0:0x1B0,owner=billboard?0x51400:0x51000,definition=billboard?0x51600:0x51200;
        c.r1.u32=immediateContext.r1.u32-frameBytes;const uint32_t batchStack=c.r1.u32,batchEntry=batchStack+0x70;
        PPC_STORE_U32(batchStack,immediateContext.r1.u32);PPC_STORE_U32(immediateRow,PPC_LOAD_U32(immediateRow+4));
        c.r3.u32=batchEntry;c.r5.u32=0;d.immediateGeometryState(c,base,true);c.r31.u32=owner;
        if(billboard){PPC_STORE_U32(owner,0x821530EC);PPC_STORE_U32(owner+0x118,definition);PPC_STORE_U32(owner+0x11C,0x51800);PPC_STORE_U32(owner+0xD0,texture);
            PPC_STORE_U32(definition+0xD0,0x80);PPC_STORE_U32(definition+0xD4,1);PPC_STORE_U8(definition+0x106,1);
            c.r3.u32=batchEntry;c.r4.u32=texture;c.r5.u32=0;d.billboardOperation(c,base,0x8275F228);
        }else{PPC_STORE_U32(owner+0xA0,definition);PPC_STORE_U32(owner+0xA4,texture);PPC_STORE_U32(owner+0xA8,0);
            for(uint32_t lane=0;lane<3;++lane)storeFloat(definition+0x44+lane*4,.125f);PPC_STORE_U8(definition+0x24,0);c.f31.f64=.5;
            d.beginTrail(c,base);
        }
        need(!d.effectiveState().scalar(S::BlendEnable)&&d.effectiveState().effectiveBlend(0)==0x10106,
             "Immediate direct packed blend changed the retained scalar enable shadow");
        need(d.effectiveState().sampler(0,T::MipFilter)==1,"Original immediate texture helper point-mip selection differs");
        std::vector<uint8_t> firstImmediate;
        for(unsigned pass=0;pass<2;++pass){
            if(billboard){c.r1.u32=batchStack-0x100;PPC_STORE_U32(c.r1.u32,batchStack);PPC_STORE_U32(batchStack-8,0x8275F494);
                c.r31.u32=0x82DFEA20;c.r28.u32=0;c.r3.u32=c.r4.u32=0;c.r5.u32=0x82DFEAE0;c.r6.u32=4;c.r7.u64=0x8000000000000000ull;
                d.billboardOperation(c,base,0x82751934);c.r1.u32=batchStack;c.r31.u32=owner;
                for(uint32_t lane=0;lane<4;++lane)storeFloat(batchStack+0x80+lane*4,lane==3?.5f:.125f);
                d.billboardOperation(c,base,0x8275F494);c.f1.f64=0;d.billboardOperation(c,base,0x8275F4E8);
            }
            c.r3.u32=batchEntry;c.r4.u32=6;c.r5.u32=4;c.lr=billboard?0x8275F6E4:0x8277E640;d.reserveImmediate(c,base);
            const uint32_t vertices=c.r3.u32;fillImmediateQuad(vertices);
            if(billboard){c.r3.u32=vertices;d.billboardOperation(c,base,0x8275F7EC);}
            else {c.r29.u32=vertices+128;d.finishTrailBatch(c,base);}
            auto output=d.readbackColor(color);
            if(!pass)firstImmediate=std::move(output);
            else {bool additive=false;
                for(size_t i=0;i<output.size()&&!additive;i+=4){uint32_t a{},b{};std::memcpy(&a,firstImmediate.data()+i,4);std::memcpy(&b,output.data()+i,4);
                    for(unsigned shift:{0u,10u,20u}){const auto first=(a>>shift)&1023,second=(b>>shift)&1023;
                        if(first>3&&first<200){need(second+1>=2*first&&second<=2*first+1,"Immediate scalar-zero batch ignored its direct additive equation");additive=true;break;}}}
                need(additive,"Immediate blend fixture produced no measurable source contribution");
            }
        }
        c.r3.u32=batchEntry;c.lr=billboard?0x8275F818:0x8277E89C;d.immediateGeometryState(c,base,false);
        need(!d.effectiveState().scalar(S::BlendEnable)&&d.readbackDepth(depth)==beforeDepth,"Immediate blend regression changed scalar cleanup or depth storage");
    }
    c=immediateContext;PPC_STORE_U32(immediateRow,immediateCursor);std::memcpy(rt.pointer(0x82DFEAE0,64,true),immediateMatrix.data(),64);
    cpu.invoke(0x823EFDA0);

    std::memcpy(rt.pointer(0x82DFEA60,128,true),matrixBytes.data(),128);
    cpu.invoke(0x826B09F0,0x50040);cpu.invoke(0x823F1A08,camera);
    for(uint32_t i=0;i<4;++i)PPC_STORE_U32(0x82D0CABC+16*i,sentinels[i]);
    // Driver destruction precedes address-space teardown, so leaked staging
    // cannot be hidden by Runtime's eventual release of every guest allocation.
    need(effectStaging.size()==3,"Ball, full distortion and luma staging owners differ");
    rt.engineDriver.reset();
    for(const auto address:effectStaging)need(std::none_of(rt.physicalAllocations.begin(),rt.physicalAllocations.end(),
        [address](const auto& allocation){return allocation.address==address;}),"Effect staging survived native driver retirement");

}
}
int main(int argc,char** argv){try{if(argc!=2)throw std::runtime_error("Original image required");exercise(argv[1]);printf("PASS: %zu original AOT screen bridge checks; fixture setup, no gameplay claim\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL after %zu checks: %s\n",checks,e.what());return 1;}}
