#include "engine_particles.h"
#include "engine_driver.h"
#include "engine_cpu_calls.h"
#include "engine_itxd_textures.h"
#include "engine_shadow_textures.h"
#include "renderer/particle_draw.h"
#include "renderer/engine_state.h"
#include <bit>
#include <cstdio>
#include <fstream>

namespace Simpsons {
namespace {
void need(bool ok,const char* message){if(!ok)throw Failure(message);}
struct HostState {
    const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
    HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
};
}
struct EngineParticles::State {
    Runtime& runtime;Graphics::NativeBackend& backend;uint32_t thread,staging{},emitter{},count{},stack{};
    uint64_t draws{},projectedDraws{},dualDraws{};uint32_t projector{},shadowId{},reportedVariants{};Graphics::ParticleDraw draw;
    State(Runtime& r,Graphics::NativeBackend& b):runtime(r),backend(b),thread(GetCurrentThreadId()){}
    void require(uint8_t* base){need(active==&runtime&&base==runtime.base&&GetCurrentThreadId()==thread&&runtime.engineDriver,"Particle owner differs");runtime.checkRunning();}
};
EngineParticles::EngineParticles(Runtime& r,Graphics::NativeBackend& b):state(std::make_unique<State>(r,b)){}
EngineParticles::~EngineParticles(){if(state->staging)state->runtime.freePhysical(state->staging);}
uint64_t EngineParticles::drawCount() const{return state->draws;}
void EngineParticles::begin(PPCContext& c,uint8_t* base){
    HostState host;
    auto& s=*state;s.require(base);need(!s.emitter,"Nested particle CPU upload");auto& driver=*s.runtime.engineDriver;
    need(currentContext==&c&&c.r1.u32>=0x200&&!(c.r1.u32&15)&&PPC_LOAD_U32(c.r1.u32)==c.r1.u32+0x200,
         "Particle original CPU upload frame differs");
    const uint32_t emitter=c.r30.u32;s.runtime.pointer(emitter,0x120,false);
    const uint32_t count=PPC_LOAD_U16(emitter+0xF4),capacity=PPC_LOAD_U16(emitter+0xC8);
    const uint32_t definition=PPC_LOAD_U32(emitter+0x118),parameters=PPC_LOAD_U32(emitter+0x11C);
    s.runtime.pointer(definition,0x108,false);s.runtime.pointer(parameters,0x90,false);
    need(count&&count<=capacity&&capacity<=4096&&PPC_LOAD_U16(emitter+0xF6)<capacity&&c.r25.u32==count,"Particle ring bounds differ");
    const uint8_t type100=PPC_LOAD_U8(definition+0x100),flags104=PPC_LOAD_U8(definition+0x104),mode40=PPC_LOAD_U8(definition+0x40);
    // Original82772D94..DC0 selects the VS solely from type100==5.
    // Original82772F94..73028 independently selects the four PS records
    // from mode40==1 and the successfully established projector. Other
    // definition flags still run through the original CPU upload below.
    const bool dual=mode40==1;
    const bool type5=type100==5;
    // 82772DFC/E00 leaves r10==0 when the requested projector is null;
    // the ordinary/dual PS is selected and never consumes stage2 or TEX1.
    const uint32_t selectedProjector=(flags104&4)?PPC_LOAD_U32(0x82DFEB98):0;
    const bool projected=selectedProjector!=0;
    if(c.r29.u32) {
        if(!s.runtime.frameCaptureDirectory.empty()) {
            const auto dir=s.runtime.frameCaptureDirectory/"particle-variant";std::filesystem::create_directories(dir);
            const auto dump=[&](const char* name,uint32_t address,uint32_t bytes) {
                std::ofstream out(dir/name,std::ios::binary);
                out.write(reinterpret_cast<const char*>(s.runtime.pointer(address,bytes,false)),bytes);
                need(bool(out),"Cannot save original particle variant evidence");
            };
            dump("emitter.bin",emitter,0x120);dump("definition.bin",definition,0x108);dump("parameters.bin",parameters,0x90);
            dump("frame.bin",0x82DFEA20,0x1A0);dump("shader-registry.bin",0x82CF2590,0xAC);
            if(const uint32_t projector=PPC_LOAD_U32(0x82DFEB98))dump("projector.bin",projector,0x2A0);
        }
        char message[512];std::snprintf(message,sizeof(message),
            "Unimplemented particle shader variant: emitter=%08X definition=%08X parameters=%08X count=%u capacity=%u "
            "type100=%02X flags104=%02X mode40=%02X flagsD0=%08X flagsD4=%08X r29=%08X texture=%08X SP=%08X LR=%08X entry=%08X",
            emitter,definition,parameters,count,capacity,PPC_LOAD_U8(definition+0x100),PPC_LOAD_U8(definition+0x104),
            PPC_LOAD_U8(definition+0x40),PPC_LOAD_U32(definition+0xD0),PPC_LOAD_U32(definition+0xD4),c.r29.u32,
            PPC_LOAD_U32(emitter+0xD0),c.r1.u32,uint32_t(c.lr),c.lastFunction);
        throw Failure(message);
    }
    const uint32_t pixelRecord=projected?(dual?0x82156DF0u:0x82156B60u):(dual?0x82156948u:0x82156770u);
    const uint32_t pixelRegistry=projected?(dual?0x82CF25ECu:0x82CF25E0u):(dual?0x82CF25D4u:0x82CF25C8u);
    need(PPC_LOAD_U32(type5?0x82CF2604:0x82CF25F8)==(type5?0x821578E0:0x821570E0)&&
         PPC_LOAD_U32(pixelRegistry)==pixelRecord,"Particle original shader association differs");
    constexpr uint32_t declaration[]={0,0x001A23A6,0,16,0x001A23A6,0x50000,32,0x001A23A6,0x50100,
        48,0x002A23B9,0x50200,60,0x001A2086,0xA0000,0xFF0000,0xFFFFFFFF,0};
    for(unsigned i=0;i<std::size(declaration);++i)need(PPC_LOAD_U32(0x821580D0+4*i)==declaration[i],"Particle declaration changed");
    Graphics::ParticleDraw draw{};draw.vertexPolicy=type5?Graphics::ParticleVertexPolicy::Type5:
        Graphics::ParticleVertexPolicy::Ordinary;auto& vc=draw.constants;
    auto copy=[&](uint32_t first,uint32_t n,uint32_t address){s.runtime.pointer(address,n*16,false);for(uint32_t r=0;r<n;++r)for(uint32_t j=0;j<4;++j)vc[first+r][j]=std::bit_cast<float>(PPC_LOAD_U32(address+16*r+4*j));};
    uint32_t modelView=0x82DFEA60;
    if(PPC_LOAD_U32(emitter+0x10)&4){
        // Preserve the original CPU matrix multiply, including its FP order.
        modelView=c.r1.u32+0x160;EngineCpuCalls cpu(c,base);cpu.invoke(0x827B8328,emitter+0x60,0x82DFEA60,modelView);
    }
    copy(0,4,modelView);copy(4,4,0x82DFEAA0);copy(8,2,c.r1.u32+0xA0);copy(12,9,parameters);
    // Original827730CC..EC converts only definition BE16+42 bit0 to c10.z.
    // The other lanes remain zero from82772F94..FA8. This selects the
    // original dual-texture interpolation, including the projected pair.
    if(dual)vc[10][2]=float(PPC_LOAD_U16(definition+0x42)&1);
    // Original writes at82772F50..88 and827730EC..12C are VS constants25
    // and10 (device offsets910 and820, constant bank starts780).
    // A requested-but-null projector leaves inherited c21..25 in retail.
    // Those affect only TEX1, which neither selected unprojected PS reads;
    // canonicalize this dead export without manufacturing a shadow owner.
    vc[25][0]=std::bit_cast<float>(PPC_LOAD_U32(0x821DD110));
    uint32_t projector=0,shadowId=0;
    if(projected) {
        projector=selectedProjector;
        s.runtime.pointer(projector,0x2A0,false);shadowId=PPC_LOAD_U32(projector+0xF0);
        const auto view=driver.shadowTextures().view(shadowId);
        need(view.owner==projector&&view.field==0xF0&&view.width==1024&&view.height==1024&&
             view.format==0x1A220197&&view.phase==EngineShadowTextures::Phase::Uploaded,
             "Projected particle shadow/shader ownership differs");
        draw.shadow=driver.shadowTextures().depth(shadowId);
        need(bool(draw.shadow),"Projected particle depth resource has no native owner");
        draw.shadowPolicy=Graphics::ParticleShadowSamplePolicy::ReferenceD24FS8DepthRRRR;
        auto& sampler=draw.shadowSampler;sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU=sampler.AddressV=D3D11_TEXTURE_ADDRESS_MIRROR;sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxLOD=13;
        // Preserve the two original827B8328 routes at82772E24/E34/E48.
        // These are CPU matrix operations, using the original stack outputs.
        EngineCpuCalls cpu(c,base);uint32_t matrix=c.r1.u32+0x120;
        if(PPC_LOAD_U32(emitter+0x10)&4) {
            matrix=c.r1.u32+0xE0;cpu.invoke(0x827B8328,0x82DFEA20,emitter+0x60,matrix);
            cpu.invoke(0x827B8328,matrix,projector+0x260,matrix);
        } else cpu.invoke(0x827B8328,0x82DFEA20,projector+0x260,matrix);
        copy(21,4,matrix);vc[25][0]=std::bit_cast<float>(PPC_LOAD_U32(definition+0xE4));
        for(unsigned lane=1;lane<4;++lane)vc[25][lane]=std::bit_cast<float>(PPC_LOAD_U32(0x821DD0D8));
    }
    const uint32_t texture=PPC_LOAD_U32(emitter+0xD0),raster=PPC_LOAD_U32(texture);
    if(type5) {
        static bool firstType5Reported{};
        if(!firstType5Reported) {
            std::fprintf(stderr,
                "[NATIVE PARTICLE TYPE5] emitter=%08X count=%u texture=%08X raster=%08X flagsD0=%08X flags104=%02X\n",
                emitter,count,texture,raster,PPC_LOAD_U32(definition+0xD0),flags104);
            firstType5Reported=true;
        }
    }
    draw.texture=driver.textureRaster(raster);
    const uint32_t sampling=PPC_LOAD_U32(texture+0x50),flags=PPC_LOAD_U32(definition+0xD0);
    draw.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    draw.sampler.AddressU=(sampling&0xF00)==0x100?D3D11_TEXTURE_ADDRESS_WRAP:D3D11_TEXTURE_ADDRESS_CLAMP;
    draw.sampler.AddressV=(sampling&0xF000)==0x1000?D3D11_TEXTURE_ADDRESS_WRAP:D3D11_TEXTURE_ADDRESS_CLAMP;
    draw.sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;draw.sampler.MaxAnisotropy=1;
    draw.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;draw.sampler.MaxLOD=13;
    if(dual) {
        const uint32_t second=PPC_LOAD_U32(emitter+0xD4);
        need(second,"Dual particle has no original second texture; inherited binding is unqualified");
        s.runtime.pointer(second,0x54,false);
        draw.secondaryTexture=driver.textureRaster(PPC_LOAD_U32(second));
        draw.secondaryPolicy=Graphics::ParticleSecondarySamplePolicy::DualTexture1LinearRepeat;
        auto& sampler=draw.secondarySampler;sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxLOD=13;
    }
    draw.alphaReference=PPC_LOAD_U8(definition+0x106);
    draw.blendWord=flags&0x80?0x10106:(flags&0x100?0x10186:0x10706);
    using S=Graphics::ScalarState;const auto& effective=driver.effectiveState();
    // 82772D0C enables the original high-precision blend path for this
    // particle scope; 82773264 installs its equation. Ordinary ALPHABLENDENABLE
    // remains zero until the 82773664 cleanup, so it cannot gate this draw.
    // The packed native shader implements that same high-precision equation.
    const uint32_t expandedBlend=effective.scalar(S::ExpandedBlend0);
    need(expandedBlend==1,"Original particle high-precision blend scope differs");
    draw.blendEnable=expandedBlend;
    draw.depthEnable=!(PPC_LOAD_U32(definition+0xD4)&1);draw.depthWrite=(flags>>28)&1;
    draw.depthCompare=effective.scalar(S::DepthCompare);draw.colorMask=effective.scalar(S::ColorMask0);
    draw.stencilEnable=effective.scalar(S::StencilEnable);draw.fill=effective.scalar(S::Fill);
    draw.scissorEnable=effective.scalar(S::ScissorEnable);draw.halfPixelOffset=effective.scalar(S::HalfPixelOffset);
    draw.viewportEnable=effective.scalar(S::ViewportEnable);draw.clipPlaneEnable=effective.scalar(S::ClipPlaneEnable);
    draw.alphaToMask=effective.scalar(S::AlphaToMask);draw.multisampleMask=effective.scalar(S::MultisampleMask);
    draw.depthBiasBits=effective.scalar(S::DepthBias);draw.slopeBiasBits=effective.scalar(S::SlopeBias);
    draw.viewport=driver.cameraBinding().viewport;
    if(!s.staging)s.staging=s.runtime.allocatePhysical(0,0x40000,PAGE_READWRITE,0,UINT32_MAX,4096);
    need(s.staging,"Particle CPU staging allocation failed");s.runtime.pointer(s.staging,count*64,true);
    draw.vertices.reserve(count*4);s.draw=std::move(draw);s.emitter=emitter;s.count=count;s.stack=c.r1.u32;
    s.projector=projector;s.shadowId=shadowId;
    // Resume original82773394..82773658 so UV animation and particle flag
    // updates run in their AOT CPU code, writing real owned staging memory.
    c.r29.u32=s.staging;c.f31.f64=std::bit_cast<float>(PPC_LOAD_U32(0x821DD0D8));
}
void EngineParticles::finish(PPCContext& c,uint8_t* base){
    HostState host;
    auto& s=*state;s.require(base);need(s.emitter&&c.r30.u32==s.emitter&&c.r1.u32==s.stack&&
        c.r29.u32==s.staging+s.count*64&&c.r25.u32==0,"Particle CPU upload completion differs");
    auto& driver=*s.runtime.engineDriver;auto& draw=s.draw;
    if(s.projector) {
        const auto view=driver.shadowTextures().view(s.shadowId);
        need(PPC_LOAD_U32(0x82DFEB98)==s.projector&&PPC_LOAD_U32(s.projector+0xF0)==s.shadowId&&
             view.owner==s.projector&&view.field==0xF0&&view.phase==EngineShadowTextures::Phase::Uploaded&&
             driver.shadowTextures().depth(s.shadowId)==draw.shadow,"Projected particle shadow changed during original CPU upload");
    }
    for(uint32_t i=0;i<s.count;++i){
        const uint32_t at=s.staging+i*64;Graphics::ParticleVertex v{};
        auto read=[&](auto& values,uint32_t offset){for(uint32_t j=0;j<values.size();++j)values[j]=std::bit_cast<float>(PPC_LOAD_U32(at+offset+4*j));};
        read(v.position,0);read(v.velocity,16);read(v.uvTime,32);read(v.size,48);
        // SDK8245EF94..EFC0 maps 001A2086 to format6, unsigned normalized.
        // Its num_format_all bit13 is zero; the later VS math is independent.
        const uint32_t color=PPC_LOAD_U32(at+60);
        for(unsigned j=0;j<4;++j)v.color[j]=float((color>>(8*j))&255)/255.0f;
        for(uint32_t corner=0;corner<4;++corner){v.originalVertexId=float(i*4+corner);draw.vertices.push_back(v);}
    }
    const auto camera=driver.cameraBinding();bool alphaOne=false;
    const bool dual=draw.secondaryPolicy==Graphics::ParticleSecondarySamplePolicy::DualTexture1LinearRepeat;
    if((!s.draws||(s.projector&&!s.projectedDraws)||(dual&&!s.dualDraws))&&!s.runtime.frameCaptureDirectory.empty()){
        const auto dir=s.runtime.frameCaptureDirectory/(s.projector?"particle-projected-native":(dual?"particle-dual-native":"particle-native"));std::filesystem::create_directories(dir);
        auto dump=[&](const char* name,const void* bytes,size_t size){std::ofstream f(dir/name,std::ios::binary);f.write(static_cast<const char*>(bytes),size);need(bool(f),"Cannot save native particle evidence");};
        dump("vertices.bin",draw.vertices.data(),draw.vertices.size()*sizeof(Graphics::ParticleVertex));
        dump("constants.bin",draw.constants.data(),sizeof(draw.constants));
        dump("original-staging.bin",s.runtime.pointer(s.staging,s.count*64,false),s.count*64);
    }
    s.backend.drawParticles(driver.color(camera.colorIdentity,alphaOne),driver.depth(camera.depthIdentity),draw);
    const bool type5=draw.vertexPolicy==Graphics::ParticleVertexPolicy::Type5;
    const uint32_t variant=(type5?4u:0u)|(s.projector?2u:0u)|(dual?1u:0u),bit=1u<<variant;
    if(!(s.reportedVariants&bit)) {
        std::fprintf(stderr,"[NATIVE PARTICLE FAMILY] emitter=%08X variant=%u VS=%08X PS=%08X dualMode=%g; original CPU upload and owned resources\n",
            s.emitter,variant,type5?0x821578E0u:0x821570E0u,
            s.projector?(dual?0x82156DF0u:0x82156B60u):(dual?0x82156948u:0x82156770u),double(draw.constants[10][2]));
        s.reportedVariants|=bit;
    }
    if(s.projector&&++s.projectedDraws<=5)std::fprintf(stderr,
        "[NATIVE PROJECTED PARTICLE DRAW] emitter=%08X projector=%08X shadow=%08X particles=%u draws=%llu; original matrix product, point/mirror depth sampling\n",
        s.emitter,s.projector,s.shadowId,s.count,static_cast<unsigned long long>(s.projectedDraws));
    if(dual&&++s.dualDraws<=5)std::fprintf(stderr,
        "[NATIVE DUAL PARTICLE DRAW] emitter=%08X particles=%u draws=%llu texture1=t3 sampler1=linear/linear/point repeat\n",
        s.emitter,s.count,static_cast<unsigned long long>(s.dualDraws));
    ++s.draws;if(s.draws<=5)std::fprintf(stderr,"[NATIVE PARTICLE DRAW] emitter=%08X particles=%u draws=%llu original_cpu_upload=true\n",s.emitter,s.count,static_cast<unsigned long long>(s.draws));
    s.emitter=s.count=s.stack=s.projector=s.shadowId=0;s.draw={};
    // Original8277365C..A0 resets these direct states, independently of the
    // engine's material cache. The native draw restored its actual bindings.
    using S=Graphics::ScalarState;
    driver.directScalar(base,uint32_t(S::BlendEnable),0);driver.directScalar(base,uint32_t(S::AlphaTest),0);
    driver.directScalar(base,uint32_t(S::DepthEnable),1);driver.directScalar(base,uint32_t(S::DepthWrite),1);
    s.backend.clearEngineTexture(0);
}
}
void SimpsonsNativeParticleFinish(PPCContext& c,uint8_t* base){
    if(!Simpsons::active||!Simpsons::active->engineDriver)throw Simpsons::Failure("Particle completion has no driver");
    Simpsons::active->engineDriver->particles().finish(c,base);
}
void SimpsonsNativeParticleExpandedBegin(PPCContext& c,uint8_t* base){
    Simpsons::HostState host;
    if(!Simpsons::active||!Simpsons::active->engineDriver||c.r3.u32||c.r4.u32!=1)
        throw Simpsons::Failure("Particle expanded-blend prefix ABI differs");
    Simpsons::active->engineDriver->directScalar(base,uint32_t(Simpsons::Graphics::ScalarState::ExpandedBlend0),1);c.lr=0x82772D10;
}
void SimpsonsNativeParticleExpandedEnd(PPCContext& c,uint8_t* base){
    Simpsons::HostState host;
    if(!Simpsons::active||!Simpsons::active->engineDriver)throw Simpsons::Failure("Particle expanded-blend suffix has no driver");
    Simpsons::active->engineDriver->directScalar(base,uint32_t(Simpsons::Graphics::ScalarState::ExpandedBlend0),0);c.lr=0x827736B0;
}
