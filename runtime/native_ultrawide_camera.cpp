#include "native_ultrawide_camera.h"
#include "engine_cpu_calls.h"
#include "engine_driver.h"
#include "engine_audio.h"
#include "world_telemetry.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {
struct CameraHostState {
    const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
    CameraHostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~CameraHostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
};
enum class CameraGate : uint8_t {
    Context,Setting,Original,Overlay,SceneIdentity,Profile,RasterPair,RasterExtent,
    RasterType,Window,WindowRatio,AdjustedWindow,Unchanged,Applied,AuthoredSource,Count
};
void cameraDiagnostic(CameraGate gate,const char* reason,const PPCContext& ctx,uint8_t* base,
                      double aspect,uint32_t fov) {
    // Opt-in, bounded observations only. Never change a qualification decision
    // or make a best-effort diagnostic read turn a rejected camera into a fault.
    static const bool enabled=[] {
        const char* text=std::getenv("SIMPSONS_CAMERA_DEBUG");
        return text&&std::strcmp(text,"1")==0;
    }();
    if(!enabled)return;
    struct Key {uint32_t camera{},caller{};CameraGate gate{};};
    static thread_local std::array<Key,64> seen{};
    static thread_local std::array<uint8_t,size_t(CameraGate::Count)> counts{};
    static thread_local size_t size{};
    const Key key{ctx.r3.u32,uint32_t(ctx.lr),gate};
    if(size==seen.size()||counts[size_t(gate)]>=8)return;
    for(size_t i=0;i<size;++i)if(seen[i].camera==key.camera&&seen[i].caller==key.caller&&seen[i].gate==gate)return;
    seen[size++]=key;++counts[size_t(gate)];
    struct Snapshot {
        uint32_t valid{},manager{},managerType{},managerAux{},slots{},slot0{},scene{};
        uint32_t type{},frame{},sync{},projection{},begin{},end{},savedEnd{},color{},depth{},width{},height{};
        std::array<uint32_t,5> colorFields{},depthFields{};
    } s;
    if(Simpsons::active&&base==Simpsons::active->base) {
        auto& rt=*Simpsons::active;
        s.scene=Simpsons::worldSceneCameraId(rt);
        try {s.savedEnd=PPCLoadU32(base,0x82E28170);} catch(const std::exception&) {}
        try {
            s.manager=PPCLoadU32(base,0x82D08B10);
            if(s.manager) {
                rt.pointer(s.manager,0x12C,false);s.valid|=1;
                s.managerType=PPC_LOAD_U32(s.manager);s.managerAux=PPC_LOAD_U32(s.manager+8);
                s.slots=PPC_LOAD_U32(s.manager+0x128);s.slot0=PPC_LOAD_U32(s.manager+0x14);
            }
        } catch(const std::exception&) {}
        try {
            if(key.camera) {
                rt.pointer(key.camera,0x8C,false);s.valid|=2;
                s.type=PPC_LOAD_U32(key.camera);s.frame=PPC_LOAD_U32(key.camera+4);
                s.sync=PPC_LOAD_U32(key.camera+0x10);s.projection=PPC_LOAD_U32(key.camera+0x14);
                s.begin=PPC_LOAD_U32(key.camera+0x18);s.end=PPC_LOAD_U32(key.camera+0x1C);
                s.color=PPC_LOAD_U32(key.camera+0x60);s.depth=PPC_LOAD_U32(key.camera+0x64);
                s.width=PPC_LOAD_U32(key.camera+0x68);s.height=PPC_LOAD_U32(key.camera+0x6C);
            }
        } catch(const std::exception&) {}
        const auto raster=[&](uint32_t address,std::array<uint32_t,5>& fields,uint32_t valid) {
            try {
                if(address) {
                    rt.pointer(address,0x34,false);s.valid|=valid;
                    fields={PPC_LOAD_U32(address),PPC_LOAD_U32(address+0xC),PPC_LOAD_U32(address+0x10),
                            PPC_LOAD_U32(address+0x1C),PPC_LOAD_U8(address+0x20)};
                }
            } catch(const std::exception&) {}
        };
        raster(s.color,s.colorFields,4);raster(s.depth,s.depthFields,8);
    }
    std::fprintf(stderr,"[NATIVE CAMERA DEBUG] gate=%s caller=%08X camera=%08X scene=%08X manager=%08X manager_type=%08X/%08X slots=%u slot0=%08X valid=%X type=%08X frame=%08X callbacks=%08X/%08X/%08X saved_end=%08X projection=%u color=%08X parent=%08X extent=%ux%u offset=%08X type=%u depth=%08X parent=%08X extent=%ux%u offset=%08X type=%u window=%08X/%08X (%.9gx%.9g) aspect=%.9g fov=%u\n",
        reason,key.caller,key.camera,s.scene,s.manager,s.managerType,s.managerAux,s.slots,s.slot0,s.valid,
        s.type,s.frame,s.sync,s.begin,s.end,s.savedEnd,s.projection,s.color,s.colorFields[0],s.colorFields[1],s.colorFields[2],s.colorFields[3],s.colorFields[4],
        s.depth,s.depthFields[0],s.depthFields[1],s.depthFields[2],s.depthFields[3],s.depthFields[4],
        s.width,s.height,double(std::bit_cast<float>(s.width)),double(std::bit_cast<float>(s.height)),aspect,fov);
}
}

namespace Simpsons {
bool applyNativeUltrawideCamera(const PPCContext& incoming,uint8_t* base,double aspect,uint32_t fieldOfView) {
    CameraHostState hostState;
    const auto reject=[&](CameraGate gate,const char* reason) {
        cameraDiagnostic(gate,reason,incoming,base,aspect,fieldOfView);return false;
    };
    if(!active||base!=active->base||!std::isfinite(aspect)||aspect<=0)return reject(CameraGate::Context,"context");
    if(fieldOfView && (fieldOfView<60||fieldOfView>110||fieldOfView%5))return reject(CameraGate::Setting,"setting");
    if(!fieldOfView&&aspect<=16.0/9.0&&!active->nativeCameraProjection.applied)return reject(CameraGate::Original,"original-16:9");
    // The original overlay parent temporarily reuses the main scene camera.
    // Its begin wrapper and registered plugin tail-call this public entry, so
    // its original continuation survives here even before overlay draw scope.
    if(uint32_t(incoming.lr)==0x827F59AC)return reject(CameraGate::Overlay,"apt-overlay");
    const auto camera=incoming.r3.u32;
    if(!camera||camera!=worldSceneCameraId(*active))return reject(CameraGate::SceneIdentity,"scene-identity");
    active->pointer(camera,0x8C,true);
    // Registered camera plugin 0509 wraps the standard begin/end/sync paths.
    // Identity plus projection/raster gates exclude loading UI, private square
    // reflection/shadow cameras, and both half-width viewport cameras.
    // Original 82A78188 installs an end forwarder at 82A78224 after saving the
    // previous callback at 82A7821C. Its five-instruction 82A77EE0 tail calls
    // 82E28170 without changing the camera argument. Admit only that exact
    // forwarding profile when it still resolves to the registered plugin end.
    const auto end=PPC_LOAD_U32(camera+0x1C);
    const bool standardEnd=end==0x823D1160||
        (end==0x82A77EE0&&PPC_LOAD_U32(0x82E28170)==0x823D1160);
    if(PPC_LOAD_U32(camera)!=0x04000000||PPC_LOAD_U32(camera+0x14)!=1||
       PPC_LOAD_U32(camera+0x10)!=0x823D2940||PPC_LOAD_U32(camera+0x18)!=0x823D1100||
       !standardEnd)return reject(CameraGate::Profile,"camera-profile");
    const auto color=PPC_LOAD_U32(camera+0x60),depth=PPC_LOAD_U32(camera+0x64);
    if(!color||!depth||color==depth)return reject(CameraGate::RasterPair,"raster-pair");
    active->pointer(color,0x34,false);active->pointer(depth,0x34,false);
    for(const auto raster:{color,depth})
        if(PPC_LOAD_U32(raster)!=raster||PPC_LOAD_U32(raster+0xC)!=1280||
           PPC_LOAD_U32(raster+0x10)!=720||PPC_LOAD_U32(raster+0x1C))return reject(CameraGate::RasterExtent,"raster-extent");
    if(PPC_LOAD_U8(color+0x20)!=5||PPC_LOAD_U8(depth+0x20)!=1)return reject(CameraGate::RasterType,"raster-type");
    const auto frame=PPC_LOAD_U32(camera+4);
    const auto generation=[&](uint32_t address) {
        if(active->engineAudio)if(const auto owner=active->engineAudio->allocationSpan(address))return owner->generation;
        return uint64_t(0);
    };
    const auto cameraGeneration=generation(camera),frameGeneration=generation(frame);
    auto& provenance=active->nativeCameraProjection;
    if(provenance.camera!=camera||provenance.frame!=frame||provenance.color!=color||provenance.depth!=depth||
       provenance.cameraGeneration!=cameraGeneration||provenance.frameGeneration!=frameGeneration)
        provenance={camera,frame,color,depth,cameraGeneration,frameGeneration};
    const auto heightBits=PPC_LOAD_U32(camera+0x6C);
    const auto currentWidthBits=PPC_LOAD_U32(camera+0x68);
    const float height=std::bit_cast<float>(heightBits),width=std::bit_cast<float>(currentWidthBits);
    if(!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0)return reject(CameraGate::Window,"view-window");
    // Gameplay controllers retain the original 16:9 view-window ratio. APT
    // instead installs a square window on this same camera; other narrow/UI
    // windows must retain their own projection. Allow original float rounding.
    if(double(width)/double(height)<16.0/9.0-0.000002)return reject(CameraGate::WindowRatio,"view-window-ratio");
    // Any original controller/animation update supplies a new source window.
    // An unchanged last publication instead reuses its authored source, including
    // when FOV changes or returns to Original without a controller update.
    if(!provenance.applied||provenance.appliedWidth!=currentWidthBits||provenance.appliedHeight!=heightBits) {
        provenance.originalWidth=currentWidthBits;provenance.originalHeight=heightBits;
        provenance.applied=false;
    }
    const float authoredHeight=std::bit_cast<float>(provenance.originalHeight);
    constexpr double radians=3.14159265358979323846/180.0;
    // Original factory 82714390..DCh loads the 30-degree half-FOV, calls its
    // tangent helper, rounds to Float32 3F13CD3A and divides by raster aspect.
    // Use that stock 60-degree horizontal reference while retaining every
    // authored zoom update as a relative scale of the source view window.
    constexpr float originalHorizontalWindow=std::bit_cast<float>(0x3F13CD3Au);
    const double scale=fieldOfView?std::tan(double(fieldOfView)*radians/2.0)/double(originalHorizontalWindow):1.0;
    const float adjustedHeight=fieldOfView?float(double(authoredHeight)*scale):authoredHeight;
    const float widened=aspect>16.0/9.0||fieldOfView?
        float(double(adjustedHeight)*std::max(aspect,16.0/9.0)):std::bit_cast<float>(provenance.originalWidth);
    if(!std::isfinite(widened)||widened<=0||!std::isfinite(adjustedHeight)||adjustedHeight<=0)return reject(CameraGate::AdjustedWindow,"adjusted-window");
    const auto widthBits=std::bit_cast<uint32_t>(widened);
    const auto adjustedHeightBits=std::bit_cast<uint32_t>(adjustedHeight);
    if(widthBits==currentWidthBits&&adjustedHeightBits==heightBits)return reject(CameraGate::Unchanged,"unchanged");
    EngineCpuCalls cpu(incoming,base);
    // EngineCpuCalls reserves its own 0x100-byte caller frame. +0x80 is outside
    // the outgoing GPR spill area, below the incoming frame's still-live locals,
    // and above the setter's own new 0x60-byte stack frame.
    const auto pair=cpu.registers().r1.u32+0x80;
    PPC_STORE_U32(pair,widthBits);PPC_STORE_U32(pair+4,adjustedHeightBits);
    {
        struct Publication {
            bool& flag;const bool previous;
            explicit Publication(bool& value):flag(value),previous(value){flag=true;}
            ~Publication(){flag=previous;}
        } publication(provenance.nativePublication);
        if(cpu.invoke(0x823F1C98,camera,pair)!=camera||PPC_LOAD_U32(camera+0x68)!=widthBits||
           PPC_LOAD_U32(camera+0x6C)!=adjustedHeightBits)
            throw Failure("Original native camera view-window setter did not retain its input");
    }
    const bool settingChanged=provenance.appliedFieldOfView!=fieldOfView||provenance.appliedAspect!=aspect;
    provenance.appliedWidth=widthBits;provenance.appliedHeight=adjustedHeightBits;
    provenance.applied=widthBits!=provenance.originalWidth||adjustedHeightBits!=provenance.originalHeight;
    provenance.appliedFieldOfView=fieldOfView;provenance.appliedAspect=aspect;
    cameraDiagnostic(CameraGate::Applied,"applied",incoming,base,aspect,fieldOfView);
    static thread_local uint32_t samples{};
    if(samples++<3||settingChanged)std::fprintf(stderr,"[NATIVE ULTRAWIDE CAMERA] camera=%08X view_window=%.9gx%.9g aspect=%.9g fov_reference_16_9=%u scale=%.9g authored_hfov=%.6g actual_hfov=%.6g actual_vfov=%.6g; original dirty-frame/frustum sync follows\n",
        camera,double(widened),double(adjustedHeight),aspect,fieldOfView,scale,
        2.0*std::atan(double(authoredHeight)*(16.0/9.0))/radians,
        2.0*std::atan(double(widened))/radians,2.0*std::atan(double(adjustedHeight))/radians);
    return true;
}
}

void SimpsonsNativeUltrawideCameraBegin(PPCContext& ctx,uint8_t* base) {
    CameraHostState hostState;
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)return;
    Simpsons::applyNativeUltrawideCamera(ctx,base,Simpsons::active->engineDriver->renderAspect(),Simpsons::active->videoSettings.fieldOfView);
}

void SimpsonsNativeUltrawideCameraSync(PPCContext& ctx,uint8_t* base) {
    CameraHostState hostState;
    if(!Simpsons::active||base!=Simpsons::active->base||!Simpsons::active->engineDriver)return;
    // Original visibility preparation 827258B8 calls 823F17F8 -> 823F1790,
    // then 826AE9D0 copies the six camera planes into the scene's visibility
    // record. Camera begin runs later, after that record has already culled
    // objects. Apply the same qualified view window before the real frustum
    // rebuild instead of leaving the earlier visibility pass on authored FOV.
    // The setter only marks the frame dirty; it never invokes this callback.
    // A frame already in 8240D5C0's dirty list is not inserted again.
    Simpsons::applyNativeUltrawideCamera(ctx,base,Simpsons::active->engineDriver->renderAspect(),Simpsons::active->videoSettings.fieldOfView);
}

void SimpsonsNativeCameraViewWindowSource(PPCContext& ctx,uint8_t* base) {
    CameraHostState hostState;
    if(!Simpsons::active||base!=Simpsons::active->base)return;
    // Original overlay 827F58D8 and effect helper 826D4750 save the current
    // adjusted window. Their restores at 827F5D20 / 826D48F0 are not authored
    // zoom changes, even though both use the real setter. The effect helper
    // can reuse the main scene camera when its alternate camera is absent.
    if(uint32_t(ctx.lr)==0x827F5D24||uint32_t(ctx.lr)==0x826D48F4)return;
    auto& rt=*Simpsons::active;auto& source=rt.nativeCameraProjection;
    // Observe the original setter before it stores either window component.
    // A real authored update can equal our last adjusted pair bit for bit;
    // its call, rather than byte inequality, identifies the new source.
    if(!source.applied||source.nativePublication||source.camera!=ctx.r3.u32||
       source.camera!=Simpsons::worldSceneCameraId(rt))return;
    const auto camera=source.camera;
    rt.pointer(camera,0x8C,false);
    if(source.frame!=PPC_LOAD_U32(camera+4)||source.color!=PPC_LOAD_U32(camera+0x60)||
       source.depth!=PPC_LOAD_U32(camera+0x64))return;
    const auto generation=[&](uint32_t address) {
        if(rt.engineAudio)if(const auto owner=rt.engineAudio->allocationSpan(address))return owner->generation;
        return uint64_t(0);
    };
    if(source.cameraGeneration!=generation(camera)||source.frameGeneration!=generation(source.frame))return;
    const auto pair=ctx.r4.u32;rt.pointer(pair,8,false);
    const auto widthBits=PPC_LOAD_U32(pair),heightBits=PPC_LOAD_U32(pair+4);
    const float width=std::bit_cast<float>(widthBits),height=std::bit_cast<float>(heightBits);
    if(!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0||
       double(width)/double(height)<16.0/9.0-0.000002)return;
    source.originalWidth=widthBits;source.originalHeight=heightBits;source.applied=false;
    cameraDiagnostic(CameraGate::AuthoredSource,"authored-source",ctx,base,source.appliedAspect,source.appliedFieldOfView);
}
