#pragma once
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
#include <memory>
#include <utility>
#include <vector>
namespace {
using namespace Simpsons;
inline size_t checks{};inline const char* stage="startup";
inline void need(bool value,const char* why){++checks;if(!value)throw Failure(why);}
template<class F>void rejects(F&& f,const char* why){try{f();}catch(const Failure&){++checks;return;}need(false,why);}
struct Observed{};
struct Abi {
    uint64_t sp,lr;std::array<uint64_t,18> gpr,fpr;std::array<std::array<uint32_t,4>,2> vector;
    bool operator==(const Abi&)const=default;
};
inline Abi abi(const PPCContext& c){return {c.r1.u64,c.lr,
    {c.r14.u64,c.r15.u64,c.r16.u64,c.r17.u64,c.r18.u64,c.r19.u64,c.r20.u64,c.r21.u64,c.r22.u64,c.r23.u64,c.r24.u64,c.r25.u64,c.r26.u64,c.r27.u64,c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64},
    {c.f14.u64,c.f15.u64,c.f16.u64,c.f17.u64,c.f18.u64,c.f19.u64,c.f20.u64,c.f21.u64,c.f22.u64,c.f23.u64,c.f24.u64,c.f25.u64,c.f26.u64,c.f27.u64,c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64},
    {{{c.v126.u32[0],c.v126.u32[1],c.v126.u32[2],c.v126.u32[3]},{c.v127.u32[0],c.v127.u32[1],c.v127.u32[2],c.v127.u32[3]}}}};}
inline void seed(PPCContext& c){
    const std::array g={&c.r14,&c.r15,&c.r16,&c.r17,&c.r18,&c.r19,&c.r20,&c.r21,&c.r22,&c.r23,&c.r24,&c.r25,&c.r26,&c.r27,&c.r28,&c.r29,&c.r30,&c.r31};
    const std::array f={&c.f14,&c.f15,&c.f16,&c.f17,&c.f18,&c.f19,&c.f20,&c.f21,&c.f22,&c.f23,&c.f24,&c.f25,&c.f26,&c.f27,&c.f28,&c.f29,&c.f30,&c.f31};
    for(size_t i=0;i<g.size();++i){g[i]->u64=0xAC11000000000100ull+i;f[i]->f64=double(i)+.125;}
    for(uint32_t i=0;i<4;++i){c.v126.u32[i]=0xABC00000+i;c.v127.u32[i]=0x12340000+i;}
}
struct Restore {
    uint8_t* destination;std::vector<uint8_t> bytes;
    Restore(Runtime& rt,uint32_t address,uint32_t size):destination(rt.pointer(address,size,true)),bytes(destination,destination+size){}
    ~Restore(){std::memcpy(destination,bytes.data(),bytes.size());}
};
inline std::vector<uint8_t> snapshot(Runtime& rt,uint32_t address,uint32_t bytes){const auto* p=rt.pointer(address,bytes,false);return {p,p+bytes};}
inline void same(Runtime& rt,uint32_t address,const std::vector<uint8_t>& before,const char* why){need(!std::memcmp(rt.pointer(address,uint32_t(before.size()),false),before.data(),before.size()),why);}
inline void put(uint8_t* base,uint32_t address,float value){PPC_STORE_U32(address,std::bit_cast<uint32_t>(value));}
inline float get(uint8_t* base,uint32_t address){return std::bit_cast<float>(PPC_LOAD_U32(address));}
using Matrix=std::array<float,16>;
inline Matrix translated(float x=0,float y=0,float z=0){return {1,0,0,0,0,1,0,0,0,0,1,0,x,y,z,1};}
inline void matrix(uint8_t* base,uint32_t address,const Matrix& m){for(uint32_t i=0;i<16;++i)put(base,address+4*i,m[i]);}
inline std::array<uint32_t,6> caches(uint8_t* base){return {PPC_LOAD_U32(0x82CD1A68),PPC_LOAD_U32(0x82CD1A6C),PPC_LOAD_U32(0x82CD1A70),PPC_LOAD_U32(0x82D0CAB0),PPC_LOAD_U32(0x82D0CAB4),PPC_LOAD_U32(0x82D0CAB8)};}
inline uint32_t pixel(const std::vector<uint8_t>& bytes,size_t offset){uint32_t word{};std::memcpy(&word,bytes.data()+offset,4);return word;}
inline uint32_t colored(const std::vector<uint8_t>& bytes){uint32_t count=0;for(size_t i=0;i<bytes.size();i+=4)count+=(pixel(bytes,i)&0x3FFFFFFF)!=0;return count;}
inline void sameDepth(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b){
    need(a.size()==size_t(1280)*720*8&&a.size()==b.size(),"Immediate depth extent differs");
    bool equal=true;for(size_t i=0;i<a.size();i+=8)equal&=!std::memcmp(a.data()+i,b.data()+i,5);need(equal,"Immediate changed depth/stencil");
}
struct ImmediateFixture {
    Runtime rt;PPCContext entry{};uint8_t* base{};std::unique_ptr<EngineCpuCalls> cpu;
    uint32_t camera{},texture{},refs{},row{},ring{};NativeCameraBinding bound{};std::unique_ptr<Restore> cursor;
    explicit ImmediateFixture(const char* path){
        rt.load(path);PPCContext startup{};rt.initialize(startup);entry=startup;base=rt.base;
        rt.audioBoundaryObserver=[](uint32_t pc,PPCContext&,uint8_t*){if(pc==0x828166FC)throw Observed{};};
        bool observed=false;try{runOriginal(startup,base);}catch(const Observed&){observed=true;}rt.audioBoundaryObserver={};need(observed,"Original startup checkpoint missing");
        cpu=std::make_unique<EngineCpuCalls>(entry,base);camera=PPC_LOAD_U32(0x82E07248);texture=PPC_LOAD_U32(0x82E071E8);
        refs=PPC_LOAD_U32(texture+0x54);row=PPC_LOAD_U32(0x82DFEB20);ring=PPC_LOAD_U32(row+4);cursor=std::make_unique<Restore>(rt,row,4);PPC_STORE_U32(row,ring);
        rt.map(0x50000,0x1000,true,"immediate fixture clear arguments");put(base,0x50000,0);put(base,0x50004,0);put(base,0x50008,0);put(base,0x5000C,1);
        need(cpu->invoke(0x823F1A18,camera)==camera,"Original loading camera begin failed");bound=driver().cameraBinding();
        using S=Graphics::ScalarState;using T=Graphics::SamplerState;
        const std::array<std::pair<S,uint32_t>,23> states={{{S::DepthCompare,7},{S::StencilEnable,0},{S::ScissorEnable,0},{S::Fill,0},{S::ClipPlaneEnable,0},
            {S::AlphaToMask,0},{S::DepthBias,0},{S::SlopeBias,0},{S::ColorMask0,15},{S::ColorMask1,0},{S::ColorMask2,0},{S::ColorMask3,0},
            {S::TessellationMode,0},{S::ViewportEnable,1},{S::HalfPixelOffset,1},{S::MultisampleMask,UINT32_MAX},{S::BlendEnable,0},
            {S::GuardBandX,0x3F800000},{S::GuardBandY,0x3F800000},{S::PrimitiveResetEnable,1},{S::PrimitiveResetIndex,0xFFFF},{S::ExpandedBlend0,0},{S::ExpandedBlend1,0}}};
        for(const auto& [id,value]:states)driver().directScalar(base,uint32_t(id),value);
        for(const auto& [id,value]:std::array<std::pair<T,uint32_t>,4>{{{T::LodBiasBits,0},{T::MinimumMip,0},{T::MaximumMip,13},{T::MaximumAnisotropy,1}}})driver().directSampler(base,0,uint32_t(id),value);
    }
    EngineDriver& driver(){return *rt.engineDriver;}
    void clear(){auto& c=cpu->registers();c.r8.u32=0x50000;c.r9.u32=3;c.r10.u32=0;c.f1.f64=c.f2.f64=0;c.f3.f64=1280;c.f4.f64=720;
        c.f5.f64=c.f6.f64=0;c.f7.f64=c.f8.f64=1;cpu->invoke(0x82756480,camera);driver().directScalar(base,uint32_t(Graphics::ScalarState::DepthCompare),7);}
    void poison(uint32_t bytes){PPC_STORE_U32(row,ring);std::memset(rt.pointer(ring,bytes,true),0xFF,bytes);}
    std::vector<uint8_t> color(){return driver().readbackColor(bound.colorIdentity);}
    std::vector<uint8_t> depth(){return driver().readbackDepth(bound.depthIdentity);}
    void lifetime(){need(PPC_LOAD_U32(texture+0x54)==refs&&driver().cameraBinding().camera==camera&&!PPC_LOAD_U32(0x82D0CAF8),"Immediate changed resource/camera/device ownership");}
    void end(){need(cpu->invoke(0x823F1A08,camera)==camera,"Original camera end failed");}
};
}
