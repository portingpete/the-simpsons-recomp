// Native Video/Audio menu extras outside the original engine passes: texture
// filter modes and anisotropy, shadow-quality storage, the world colour look,
// the photo-mode colour stash, ambient occlusion and native TAA, plus the
// master-volume preference file.
#include "renderer/native_backend.h"
#include "runtime/native_audio_settings.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
using namespace Simpsons::Graphics;
namespace Simpsons::Graphics {
// Friend seam: the private scene sampler cache builds the adjusted descriptions.
struct NativeRenderResolutionProbe {
    static ComPtr<ID3D11SamplerState> sampler(NativeBackend& backend,const D3D11_SAMPLER_DESC& desc){return backend.sceneSamplerState(desc);}
    static std::array<uint32_t,4> extent(const std::shared_ptr<DepthTarget>& d){return {d->width,d->height,d->physicalWidth,d->physicalHeight};}
};
}
namespace {
size_t checks{};
void need(bool value,const char* why){++checks;if(!value)throw Error(why);}
uint32_t word(const std::vector<uint8_t>& bytes,size_t index){uint32_t v;std::memcpy(&v,bytes.data()+4*index,4);return v;}
uint32_t channel(uint32_t packed,uint32_t lane){return (packed>>(10*lane))&1023;}
int diff(uint32_t a,uint32_t b,uint32_t lane){return std::abs(int(channel(a,lane))-int(channel(b,lane)));}
// Every pixel within `tolerance` of `expected` per RGB channel, alpha bits exact.
void uniform(const std::vector<uint8_t>& pixels,uint32_t expected,int tolerance,const char* why) {
    for(size_t i=0;i<pixels.size()/4;++i) {
        const auto value=word(pixels,i);
        if((value>>30)!=(expected>>30)||diff(value,expected,0)>tolerance||diff(value,expected,1)>tolerance||diff(value,expected,2)>tolerance) {
            char text[220];std::snprintf(text,sizeof(text),"%s: pixel %zu is %08X, expected %08X",why,i,value,expected);throw Error(text);
        }
    }
}

void filtering(bool software) {
    D3D11_SAMPLER_DESC linear{};linear.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    linear.AddressU=linear.AddressV=linear.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;linear.MaxAnisotropy=1;
    linear.ComparisonFunc=D3D11_COMPARISON_NEVER;linear.MaxLOD=D3D11_FLOAT32_MAX;
    auto bilinear=linear;bilinear.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    auto point=linear;point.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    auto comparison=linear;comparison.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;comparison.ComparisonFunc=D3D11_COMPARISON_LESS_EQUAL;
    for(const auto filter:{TextureFilter::Original,TextureFilter::Nearest,TextureFilter::Bilinear,TextureFilter::Trilinear}) {
        for(const uint32_t anisotropy:{1u,2u,16u}) {
            NativeBackend backend(software,false);backend.configureRendering(1280,720,anisotropy,Antialiasing::Original,100,filter);
            need(backend.textureFilter()==filter&&backend.anisotropy()==anisotropy,"Configured filter mode or anisotropy differs");
            for(const auto& source:{linear,bilinear}) {
                const auto out=backend.materialSampling(source);
                D3D11_FILTER expected=source.Filter;
                if(filter==TextureFilter::Nearest)expected=D3D11_FILTER_MIN_MAG_MIP_POINT;
                else {
                    if(filter==TextureFilter::Bilinear)expected=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
                    if(filter==TextureFilter::Trilinear)expected=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                    if(anisotropy>1)expected=D3D11_FILTER_ANISOTROPIC;
                }
                need(out.Filter==expected&&(expected!=D3D11_FILTER_ANISOTROPIC||out.MaxAnisotropy==anisotropy),"Material filter mode mapping differs");
                need(out.AddressU==source.AddressU&&out.MaxLOD==source.MaxLOD,"Material filtering changed addressing or LOD");
                // The adjusted description must make a real sampler.
                auto state=NativeRenderResolutionProbe::sampler(backend,out);D3D11_SAMPLER_DESC actual{};state->GetDesc(&actual);
                need(actual.Filter==out.Filter,"Material filter did not reach a native sampler");
            }
            need(backend.materialSampling(point).Filter==D3D11_FILTER_MIN_MAG_MIP_POINT,"Filtering changed a point-sampled lookup");
            need(backend.materialSampling(comparison).Filter==comparison.Filter,"Filtering changed a comparison sampler");
        }
    }
}

void shadowStorage(bool software) {
    constexpr std::array<uint32_t,4> sizes{512,1024,2048,4096};
    for(uint32_t quality=0;quality<4;++quality) {
        NativeBackend backend(software,false);
        backend.configureRendering(1280,720,1,Antialiasing::Original,100,TextureFilter::Original,quality);
        need(backend.shadowQuality()==quality,"Shadow quality did not configure");
        auto shadow=backend.createDepthTarget(1024,1024,TargetScale::Shadow);auto fixed=backend.createDepthTarget(1024,1024);
        need(NativeRenderResolutionProbe::extent(shadow)==std::array<uint32_t,4>{1024,1024,sizes[quality],sizes[quality]},
             "Shadow-role storage does not follow shadow quality");
        need(NativeRenderResolutionProbe::extent(fixed)==std::array<uint32_t,4>{1024,1024,1024,1024},"Fixed depth storage followed shadow quality");
    }
    // Live change re-backs the same shadow owner at the new storage size.
    NativeBackend backend(software,false);backend.configureRendering(1280,720,1);
    auto shadow=backend.createDepthTarget(1024,1024,TargetScale::Shadow);
    need(backend.reconfigureRendering(1280,720,1,Antialiasing::Original,100,TextureFilter::Original,3),"Live shadow quality change was deferred");
    need(NativeRenderResolutionProbe::extent(shadow)==std::array<uint32_t,4>{1024,1024,4096,4096}&&backend.shadowQuality()==3,"Live shadow quality did not re-back the shadow owner");
    need(backend.reconfigureRendering(1280,720,1,Antialiasing::Original,100,TextureFilter::Trilinear,0)&&NativeRenderResolutionProbe::extent(shadow)[2]==512&&
         backend.textureFilter()==TextureFilter::Trilinear,"Live shadow/filter change did not apply together");
}

void worldLook(bool software) {
    NativeBackend backend(software,false);backend.configureRendering(1280,720,1);
    auto color=backend.createTarget(64,36,TargetFormat::RGB10A2);
    backend.clearTarget(color,{.6f,.3f,.1f,2.0f/3});const auto before=backend.readbackTarget(color);
    const auto original=word(before,0);need((original>>30)==2,"World look fixture lost its packed alpha flags");
    backend.applyWorldLook(color,{1,1,0,1});
    uniform(backend.readbackTarget(color),original,1,"Identity world look changed the image");
    backend.applyWorldLook(color,{1,0,0,1});
    const auto grey=backend.readbackTarget(color);
    for(size_t i=0;i<grey.size()/4;++i) {
        const auto v=word(grey,i);
        need((v>>30)==2&&diff(v,(channel(v,0)*0x100401u)|(2u<<30),1)<=2&&diff(v,(channel(v,0)*0x100401u)|(2u<<30),2)<=2,
             "Zero saturation did not make grey or lost alpha flags");
    }
    backend.clearTarget(color,{.3f,.3f,.3f,1.0f/3});const auto dim=word(backend.readbackTarget(color),0);
    backend.applyWorldLook(color,{1.5f,1,0,1});const auto bright=word(backend.readbackTarget(color),0);
    need((bright>>30)==1&&channel(bright,0)>channel(dim,0)+40,"Exposure did not brighten or lost alpha flags");
    bool rejected=false;try{backend.applyWorldLook(color,{NAN,1,0,1});}catch(const Error&){rejected=true;}
    need(rejected,"Non-finite world look was accepted");
}

void photoStash(bool software) {
    NativeBackend backend(software,false);backend.configureRendering(1280,720,1);
    auto color=backend.createTarget(32,32,TargetFormat::RGB10A2);
    backend.clearTarget(color,{.2f,.4f,.6f,1});const auto original=backend.readbackTarget(color);
    backend.stashColor(color);backend.clearTarget(color,{1,0,0,0});
    need(backend.restoreStashedColor(color)&&backend.readbackTarget(color)==original,"Photo stash did not restore the image");
    need(!backend.restoreStashedColor(color),"Photo stash restored twice");
    auto other=backend.createTarget(32,32,TargetFormat::RGB10A2);backend.stashColor(color);
    need(!backend.restoreStashedColor(other),"Photo stash restored into another target");
}

void ambientOcclusion(bool software) {
    NativeBackend backend(software,false);backend.configureRendering(1280,720,1);
    auto color=backend.createTarget(128,72,TargetFormat::RGB10A2,TargetScale::Scene);
    auto depth=backend.createDepthTarget(128,72,TargetScale::Scene);
    NativeBackend::AmbientOcclusionFrame frame{};frame.projection={1.0f,16.0f/9,1.0001f,-0.1f};
    for(uint32_t quality:{1u,2u}) {
        backend.clearTarget(color,{.5f,.5f,.5f,2.0f/3});backend.clearDepthTarget(depth,.25f,0);
        const auto original=word(backend.readbackTarget(color),0);
        frame.quality=quality;++frame.frame;backend.applyAmbientOcclusion(color,depth,frame);
        // A plane facing the camera occludes nothing.
        uniform(backend.readbackTarget(color),original,3,"Ambient occlusion darkened a flat plane or lost alpha flags");
    }
    // Orthographic or degenerate projections leave the image untouched.
    backend.clearTarget(color,{.1f,.7f,.2f,1});const auto before=backend.readbackTarget(color);
    frame.projection={1,1,1,0};backend.applyAmbientOcclusion(color,depth,frame);
    need(backend.readbackTarget(color)==before,"Ambient occlusion ran on a non-perspective view");
    frame.projection={1.0f,16.0f/9,1.0001f,-0.1f};frame.quality=3;
    bool rejected=false;try{backend.applyAmbientOcclusion(color,depth,frame);}catch(const Error&){rejected=true;}
    need(rejected,"Unknown ambient occlusion quality was accepted");
}

void temporal(bool software) {
    NativeBackend backend(software,false);backend.configureRendering(1280,720,1);
    auto color=backend.createTarget(128,72,TargetFormat::RGB10A2,TargetScale::Scene);
    auto depth=backend.createDepthTarget(128,72,TargetScale::Scene);backend.clearDepthTarget(depth,.5f,0);
    NativeBackend::NativeTemporalFrame frame{};
    for(size_t i=0;i<4;++i)frame.reprojection[i][i]=1;
    const auto resolves=backend.nativeTemporalResolveCount();
    backend.clearTarget(color,{.4f,.5f,.6f,2.0f/3});const auto first=word(backend.readbackTarget(color),0);
    backend.resolveNativeTemporal(color,depth,frame);
    uniform(backend.readbackTarget(color),first,1,"First TAA frame changed the image or alpha flags");
    need(backend.nativeTemporalHistory()&&backend.nativeTemporalResolveCount()==resolves+1,"TAA did not keep history");
    frame.historyValid=true;backend.resolveNativeTemporal(color,depth,frame);
    uniform(backend.readbackTarget(color),first,1,"Static TAA history drifted");
    // A new uniform image clamps stale history to its own neighbourhood.
    backend.clearTarget(color,{.9f,.1f,.2f,1.0f/3});const auto second=word(backend.readbackTarget(color),0);
    backend.resolveNativeTemporal(color,depth,frame);
    uniform(backend.readbackTarget(color),second,1,"TAA ghosted stale history into a changed image");
    backend.resetNativeTemporal();need(!backend.nativeTemporalHistory(),"TAA reset kept history");
    auto other=backend.createDepthTarget(64,36);bool rejected=false;
    try{backend.resolveNativeTemporal(color,other,frame);}catch(const Error&){rejected=true;}
    need(rejected,"TAA accepted mismatched depth storage");
}

void audioSettings() {
    const auto folder=std::filesystem::temp_directory_path()/(L"SimpsonsAudioTest-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);const auto file=folder/"profile.audio.cfg";
    try {
        using Simpsons::NativeAudioSettings;
        const NativeAudioSettings defaults;
        need(NativeAudioSettings::load(file)==defaults&&defaults.masterVolume==100&&defaults.gain()==1.0f,"Missing audio file did not select full volume");
        auto settings=defaults;settings.stepMaster(1);need(settings.masterVolume==100,"Master volume wrapped above 100");
        for(int i=0;i<3;++i)settings.stepMaster(-1);
        need(settings.masterVolume==85&&settings.label()=="Master volume: 85%"&&std::abs(settings.gain()-.85f)<1e-6f,"Master volume step/label/gain differ");
        settings.save(file);need(NativeAudioSettings::load(file)==settings,"Master volume did not persist");
        for(int i=0;i<30;++i)settings.stepMaster(-1);need(settings.masterVolume==0&&settings.gain()==0,"Master volume did not stop at silence");
        for(const char* bad:{"1 101\n","1 33\n","2 50\n","1 50 7\n","broken"}) {
            {std::ofstream out(file);out<<bad;}
            need(NativeAudioSettings::load(file)==defaults,"Invalid audio file partially applied");
        }
    } catch(...) {std::error_code ignored;std::filesystem::remove_all(folder,ignored);throw;}
    std::filesystem::remove_all(folder);
}
}

int main(int argc,char** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    const bool hardware=argc==2&&std::string(argv[1])=="--hardware";
    if(argc>2||(argc==2&&!hardware))throw Error("Usage: NativeVideoExtrasTests [--hardware]");
    filtering(!hardware);shadowStorage(!hardware);worldLook(!hardware);photoStash(!hardware);
    ambientOcclusion(!hardware);temporal(!hardware);audioSettings();
    std::printf("PASS native video extras %s: %zu checks; filter modes/anisotropy, shadow storage, world look, photo stash, AO, TAA and master volume\n",
        hardware?"hardware":"WARP",checks);
    return 0;
} catch(const std::exception& error) {
    std::fprintf(stderr,"FAIL native video extras: %s\n",error.what());return 1;
}
