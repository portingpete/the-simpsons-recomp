#pragma once
// Shared test helpers for actual original FX registration/cache/name checks.
// No production parser output is used as the expected value.
#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include "runtime/engine_effects.h"
#include "runtime/engine_shadow_textures.h"
#include "runtime/engine_quad_declarations.h"
#include "renderer/native_backend.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace Simpsons;
size_t checks{};const char* stage="startup";
constexpr uint32_t table=0x82CEFD20,root=0x82D6D2F8;
void need(bool value,const char* message) {
    ++checks;if(!value) {std::fprintf(stderr,"CHECK %zu stage=%s: %s\n",checks,stage,message);throw Failure(message);}
}
template<class F> void rejects(F&& f,const char* message) {
    try {f();}catch(const Failure&){++checks;return;}need(false,message);
}
struct Observed {};
[[maybe_unused]] void observeGraphics(uint32_t pc,PPCContext&,uint8_t* base) {
    need(pc==0x823458C0 && active && active->engineDriver && active->engineDriver->started(),
         "Original pre-output graphics startup boundary differs");
    need(!PPC_LOAD_U32(0x82D08BFC),"Graphics manager already exists");throw Observed{};
}
[[maybe_unused]] void observe(uint32_t pc,PPCContext&,uint8_t* base) {
    if(pc==0x82345920) return;
    need(pc==0x828166FC && active && active->engineDriver && active->engineDriver->started(),"Original startup boundary differs");
    need(!PPC_LOAD_U32(0x82D08BFC),"Graphics manager already exists");throw Observed{};
}
std::vector<uint8_t> snapshot(Runtime& rt,uint32_t address,uint32_t size) {
    const auto* p=rt.pointer(address,size,false);return {p,p+size};
}
void same(Runtime& rt,uint32_t address,const std::vector<uint8_t>& bytes,const char* message) {
    need(!std::memcmp(rt.pointer(address,uint32_t(bytes.size()),false),bytes.data(),bytes.size()),message);
}
std::string stringAt(Runtime& rt,uint32_t address) {
    std::string value;for(uint32_t i=0;i<256;++i) {
        const auto c=*rt.pointer(address+i,1,false);if(!c) return value;value+=char(c);
    }
    throw Failure("Fixture unterminated original name");
}
struct SavedAbi {
    uint64_t sp,lr;
    std::array<uint64_t,18> gpr;
    bool operator==(const SavedAbi&) const=default;
};
SavedAbi abi(const PPCContext& c) {
    return {c.r1.u64,c.lr,{c.r14.u64,c.r15.u64,c.r16.u64,c.r17.u64,c.r18.u64,c.r19.u64,
        c.r20.u64,c.r21.u64,c.r22.u64,c.r23.u64,c.r24.u64,c.r25.u64,c.r26.u64,c.r27.u64,c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64}};
}
// Walk original descriptors independently of the native metadata parser. Leaf
// ordinal counts descriptors, never matrix rows or default-storage vectors.
uint32_t leaves(Runtime& rt,uint32_t at,uint32_t end) {
    auto* base=rt.base;uint32_t count=0;
    for(uint32_t p=at;p<end;p+=8) {need(p+8<=end,"Descriptor range truncated");if(!(PPC_LOAD_U32(p)&3)) ++count;}
    return count;
}
inline void queries(Runtime& rt,EngineCpuCalls& cpu,uint32_t id,uint32_t f) {
    auto* base=rt.base;const uint32_t descriptors=f+PPC_LOAD_U32(f+0x108),names=f+PPC_LOAD_U32(f+0x288);
    uint32_t descriptor=1,leafOrdinal=0,name=names;
    for(uint32_t i=0;i<PPC_LOAD_U32(f+0x110);++i) {
        const uint32_t at=descriptors+8*descriptor,w0=PPC_LOAD_U32(at),w1=PPC_LOAD_U32(at+4);
        const uint32_t span=(w0&3)?w1&0xFFFF:1,handle=(descriptor<<18)|(leafOrdinal<<1);
        const auto before=abi(cpu.registers());
        need(cpu.invoke(0x823C7B20,id,name)==handle,"Original private-name entry returned wrong descriptor/leaf handle");
        need(abi(cpu.registers())==before,"Native metadata query changed original nonvolatile ABI");
        name+=uint32_t(stringAt(rt,name).size())+1;
        leafOrdinal+=leaves(rt,at,at+8*span);descriptor+=span;
    }
    need(leafOrdinal==PPC_LOAD_U32(f+0x130),"Private descriptor leaf total differs");
    need(!rt.engineDriver->effects().parameter(id,"__missing_fx_parameter__"),"Missing FX name was fabricated");
}
inline void cache(Runtime& rt,EngineCpuCalls& cpu,uint32_t id,uint32_t f) {
    auto* base=rt.base;auto& effects=rt.engineDriver->effects();const auto v=effects.view(id);
    const uint32_t nt=PPC_LOAD_U32(f+0x20C),techniques=f+PPC_LOAD_U32(f+0x200),c=v.cache,w=v.wrapper;
    need(v.phase==EngineEffects::Phase::Reflected && PPC_LOAD_U32(w+0x10)==id && PPC_LOAD_U32(w+0x18)==id &&
         PPC_LOAD_U32(w+0x1C)==c && PPC_LOAD_U32(w+0x20)==v.cacheBytes && PPC_LOAD_U32(w+0x24)==c &&
         PPC_LOAD_U32(w+0x28)==nt && !PPC_LOAD_U32(w+0x2C),"Reflected original wrapper fields differ");
    uint32_t cursor=24*nt;
    for(uint32_t i=0;i<nt;++i) {
        const uint32_t t=f+PPC_LOAD_U32(techniques+4*i),p=f+PPC_LOAD_U32(t+16);
        need(PPC_LOAD_U32(t+4)==1,"Fixture corpus pass count changed");
        const uint32_t scalar=f+PPC_LOAD_U32(p+12),sampler=f+PPC_LOAD_U32(p+16);
        const uint32_t ns=PPC_LOAD_U32(scalar+0x18),nm=PPC_LOAD_U32(sampler+0x88),header=c+24*i;
        need(!PPC_LOAD_U32(scalar+0x10) && !PPC_LOAD_U32(scalar+0x14) &&
             !PPC_LOAD_U32(sampler+0x80) && !PPC_LOAD_U32(sampler+0x84),"Fixture encountered parameter-driven state");
        const std::array<uint32_t,6> expected={(i<<18)|0x3FFFC,0,c+cursor,c+cursor+12*ns,ns,nm};
        for(uint32_t j=0;j<6;++j) need(PPC_LOAD_U32(header+4*j)==expected[j],"Per-technique cache header/order differs");
        need(cpu.invoke(0x823C7CA0,id,f+PPC_LOAD_U32(t))==expected[0],"Original technique-name entry returned wrong handle");
        for(uint32_t j=0;j<ns;++j) {
            const uint32_t sdk=PPC_LOAD_U32(scalar+0x1C+8*j),value=PPC_LOAD_U32(scalar+0x20+8*j);
            need(PPC_LOAD_U32(c+cursor)==PPC_LOAD_U32(0x82E06F80+sdk) && PPC_LOAD_U32(c+cursor+4)==value,
                 "Native cache scalar row differs from live original map/value");cursor+=12;
        }
        for(uint32_t j=0;j<nm;++j) {
            const uint32_t packed=PPC_LOAD_U32(sampler+0x8C+8*j),value=PPC_LOAD_U32(sampler+0x90+8*j);
            need(PPC_LOAD_U32(c+cursor)==packed>>16 && PPC_LOAD_U32(c+cursor+4)==PPC_LOAD_U32(0x82E07118+(packed&0xFFFF)) &&
                 PPC_LOAD_U32(c+cursor+8)==value,"Native cache sampler row differs from original stage/live map/value");cursor+=16;
        }
    }
    need(cursor==v.cacheBytes,"Complete cache extent differs");
    // Saved-value slots are unwritten by reflection; no initial heap pattern is assumed.
}
}
