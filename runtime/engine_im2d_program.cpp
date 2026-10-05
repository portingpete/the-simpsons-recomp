#include "engine_im2d_program.h"
#include "engine_cpu_calls.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <cstring>

namespace Simpsons {
static void buildIm2DProgram(Runtime& runtime,EngineCpuCalls& cpu,uint8_t* base,bool textured) {
    if(PPC_LOAD_U32(0x82D10114) || PPC_LOAD_U32(0x82D10118) ||
       PPC_LOAD_U32(0x82D503D4)!=1 || PPC_LOAD_U32(0x82CD1A6C) || PPC_LOAD_U32(0x82CD1A70))
        throw Failure("Original Im2D program selection precedes commit or screen override");
    struct Scratch {
        Runtime& runtime;uint32_t address;
        ~Scratch(){if(address)runtime.freePhysical(address);}
    } scratch{runtime,runtime.allocatePhysical(0,4096,PAGE_READWRITE,0,UINT32_MAX,4096)};
    if(!scratch.address)throw Failure("Native Im2D CPU program scratch allocation failed");
    const uint32_t source=scratch.address,key=source+2112;
    auto text=[&](uint32_t address,uint32_t bound) {
        std::string value;
        for(uint32_t i=0;i<bound;++i) {
            const auto c=*runtime.pointer(address+i,1,false);
            if(!c)return value;value+=char(c);
        }
        throw Failure("Original Im2D program text exceeds its bounded storage");
    };
    std::fill_n(runtime.pointer(source,2048,true),2048,0);
    if(cpu.invoke(0x8240EAA8,source,2048)!=0)throw Failure("Original Im2D pixel source builder failed");
    auto pixel=text(source,2048);
    pixel.erase(std::remove_if(pixel.begin(),pixel.end(),[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}),pixel.end());
    const char* expected=textured?
        "uniformexternsamplerg_texsamp0:register(s0);float4main(infloat4diffuse:COLOR0,infloat2texcoord0:TEXCOORD0):COLOR{float4current=1.f;"
        "float4stage0_texel=tex2D(g_texsamp0,texcoord0);float3stage0_color_arg1=stage0_texel.xyz;float3stage0_color_arg2=diffuse.xyz;"
        "current.xyz=stage0_color_arg1*stage0_color_arg2;floatstage0_alpha_arg1=stage0_texel.w;floatstage0_alpha_arg2=diffuse.w;"
        "current.w=stage0_alpha_arg1*stage0_alpha_arg2;returncurrent;}":
        "float4main(infloat4diffuse:COLOR0):COLOR{float4current=1.f;float3stage0_color_arg2=diffuse.xyz;"
        "current.xyz=stage0_color_arg2;floatstage0_alpha_arg2=diffuse.w;current.w=stage0_alpha_arg2;returncurrent;}";
    // After a textured draw, the original SELECTARG2 stage keeps ARG1's
    // texture selector. Its CPU builder still declares/samples texture0 and
    // enables UV0, but neither color nor alpha reads that sample. Qualify the
    // entire reached expression; the native flat shader has the same output.
    const char* flatWithUnusedTexture=
        "uniformexternsamplerg_texsamp0:register(s0);float4main(infloat4diffuse:COLOR0,infloat2texcoord0:TEXCOORD0):COLOR{float4current=1.f;"
        "float4stage0_texel=tex2D(g_texsamp0,texcoord0);float3stage0_color_arg2=diffuse.xyz;"
        "current.xyz=stage0_color_arg2;floatstage0_alpha_arg2=diffuse.w;current.w=stage0_alpha_arg2;returncurrent;}";
    const bool unusedTexture=!textured && pixel==flatWithUnusedTexture;
    if(pixel!=expected && !unusedTexture) {
        std::fprintf(stderr,"[IM2D PROGRAM REJECTED] textured=%u raster=%08X source=%s\n",textured?1u:0u,PPC_LOAD_U32(0x82D0E3F8),pixel.c_str());
        dumpGuestStack(cpu.registers());
        throw Failure("Original Im2D pixel program is outside the recovered flat/modulate expressions");
    }
    // Exact 22-word key construction82410A74..82410B94. Preserve sentinels
    // except the four explicitly normalized texture-transform/index fields.
    auto put=[&](uint32_t i,uint32_t v){PPC_STORE_U32(key+4*i,v);};
    auto stage=[&](uint32_t address,bool high) {
        const auto v=PPC_LOAD_U32(address);return v==0xFFFFFFFF?0u:high?v&0xFFFF0000:v;
    };
    put(0,PPC_LOAD_U32(0x82E3DC10));put(1,0);
    put(2,stage(0x82E3D1C0,false));put(3,stage(0x82E3D244,false));
    put(4,stage(0x82E3D18C,true));put(5,stage(0x82E3D210,true));
    put(6,cpu.invoke(0x8240F0B0,0));put(7,cpu.invoke(0x8240F0B0,1));
    put(8,cpu.invoke(0x8240F150,0));put(9,cpu.invoke(0x8240F150,4));
    constexpr std::array<uint32_t,12> fields={0x82E3DBD8,0x82E3DBF0,0x82E3DC04,0x82E3DBDC,
        0x82E3DBFC,0x82D503D4,0x82E3DC14,0x82E3DC18,0x82E3DC1C,0x82E3DC20,0x82D503D8,0x82D503DC};
    for(uint32_t i=0;i<fields.size();++i)put(10+i,PPC_LOAD_U32(fields[i]));
    const uint32_t macros=cpu.invoke(0x82410588,key);
    if(macros!=0x82D50328)throw Failure("Original Im2D vertex macro output owner changed");
    const std::array<std::pair<const char*,const char*>,20> expectedMacros={{{"K_NORMALIZENORMALS","false"},
        {"K_TWEENENABLE","false"},{"K_FOGENABLE","false"},{"K_RANGEFOGENABLE","true"},
        {"K_SPECULARENABLE","false"},{"K_LIGHTING","false"},{"K_SCREENSPACEPOSITIONS","true"},
        {"K_DIFFUSEMATERIALSOURCE","D3DMCS_MATERIAL"},{"K_SPECULARMATERIALSOURCE","D3DMCS_MATERIAL"},
        {"K_AMBIENTMATERIALSOURCE","D3DMCS_MATERIAL"},{"K_EMISSIVEMATERIALSOURCE","D3DMCS_MATERIAL"},
        {"K_TEXTURETRANSFORMFLAGS0","D3DTTFF_DISABLE"},{"K_TEXTURETRANSFORMFLAGS1","D3DTTFF_DISABLE"},
        {"K_TEXCOORDINDEX0","D3DTSS_TCI_PASSTHRU"},{"K_TEXCOORDINDEX1","D3DTSS_TCI_PASSTHRU"},
        {"K_COLOR0ENABLE","true"},{"K_COLOR1ENABLE","false"},
        {"K_TEXCOORDINDEX0ENABLE",(textured||unusedTexture)?"true":"false"},{"K_TEXCOORDINDEX1ENABLE","false"},
        {"K_FOGVERTEXMODE","D3DFOG_LINEAR"}}};
    for(uint32_t i=0;i<expectedMacros.size();++i) {
        if(text(PPC_LOAD_U32(macros+8*i),128)!=expectedMacros[i].first ||
           text(PPC_LOAD_U32(macros+8*i+4),128)!=expectedMacros[i].second)
            throw Failure("Original Im2D vertex program options exceed the recovered unlit screen-space branch");
    }
    if(PPC_LOAD_U32(macros+160) || PPC_LOAD_U32(macros+164))
        throw Failure("Original Im2D vertex macro list exceeds twenty options");
}
void qualifyIm2DProgram(Runtime& runtime,EngineCpuCalls& cpu,uint8_t* base,bool textured) {
    if(active!=&runtime||runtime.base!=base)throw Failure("Im2D program cache belongs to another runtime");runtime.checkRunning();
    // These live gates must precede every cache lookup, including a hit.
    if(PPC_LOAD_U32(0x82D10114)||PPC_LOAD_U32(0x82D10118)||PPC_LOAD_U32(0x82D503D4)!=1||
       PPC_LOAD_U32(0x82CD1A6C)||PPC_LOAD_U32(0x82CD1A70))
        throw Failure("Original Im2D program selection precedes commit or screen override");
    if(!runtime.im2dProgramCache){runtime.im2dProgramCache=std::make_shared<Im2DProgramCache>();runtime.im2dProgramCache->thread=GetCurrentThreadId();}
    auto& cache=*runtime.im2dProgramCache;if(cache.thread!=GetCurrentThreadId())throw Failure("Im2D program cache changed owner thread");
    Im2DProgramCache::Entry entry;size_t at=0;
    auto bytes=[&](uint32_t address,uint32_t count){std::memcpy(entry.key.data()+at,runtime.pointer(address,count,false),count);at+=count;};
    // Pixel builder8240EAA8 and stage predicates8240F0B0/F150 read all eight
    // 40-byte records. The remaining spans are EVERY input to the original
    // 22-word vertex key, plus the engine's formatting callback identity.
    bytes(0x82D501E0,0x140);bytes(0x82E3DBD8,0x4C);
    for(uint32_t address:{0x82E3D1C0u,0x82E3D244u,0x82E3D18Cu,0x82E3D210u})bytes(address,4);
    bytes(0x82D503D4,12);bytes(0x82D0CA68,4);bytes(PPC_LOAD_U32(0x82D0CA68)+0xC4,4);
    entry.key[at]=uint8_t(textured);at+=4;
    if(at!=entry.key.size())throw Failure("Im2D program key extent differs");
    for(const auto& found:cache.entries)if(found.key==entry.key){
        // The original macro builder overwrites this entire output each call.
        // Re-publish the exact validated bytes even if another caller changed it.
        std::memcpy(runtime.pointer(0x82D50328,168,true),found.macros.data(),168);++cache.hits;return;
    }
    buildIm2DProgram(runtime,cpu,base,textured);++cache.builds;
    std::memcpy(entry.macros.data(),runtime.pointer(0x82D50328,168,false),168);
    if(cache.entries.size()==32)cache.entries.erase(cache.entries.begin());cache.entries.push_back(std::move(entry));
}
}
