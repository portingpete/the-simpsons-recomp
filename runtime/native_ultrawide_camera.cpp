#include "native_ultrawide_camera.h"
#include "native_controllers.h"
#include "native_window.h"
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
    const double zoom=active->freeCamera.active?active->freeCamera.zoom:1.0;
    if(!fieldOfView&&aspect<=16.0/9.0&&zoom==1.0&&!active->nativeCameraProjection.applied)return reject(CameraGate::Original,"original-16:9");
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
    const double scale=(fieldOfView?std::tan(double(fieldOfView)*radians/2.0)/double(originalHorizontalWindow):1.0)/zoom;
    const bool adjusted=fieldOfView||zoom!=1.0;
    const float adjustedHeight=adjusted?float(double(authoredHeight)*scale):authoredHeight;
    const float widened=aspect>16.0/9.0||adjusted?
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
    const bool settingChanged=(provenance.appliedFieldOfView!=fieldOfView||provenance.appliedAspect!=aspect)&&zoom==1.0;
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
    Simpsons::updateNativeFreeCamera(ctx,base,ctx.r3.u32);
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

void SimpsonsNativeTemporalProjection(PPCContext& ctx,uint8_t* base) {
    CameraHostState hostState;
    auto* rt=Simpsons::active;
    if(!rt||base!=rt->base||!rt->engineDriver)return;
    auto& t=rt->nativeTemporalCamera;
    // 823F0210 caches projection slot two from 82CD1AB0 (written just above).
    if(ctx.r3.u32!=2||ctx.r4.u32!=0x82CD1AB0)return;
    // Matrices are captured for every main world begin (ambient occlusion
    // reads them too); only TAA jitters the projection.
    const auto extent=rt->engineDriver->nativeTemporalJitterExtent();
    const bool jitter=extent[0]&&extent[1];
    if(!jitter)t.previousValid=false;
    const auto camera=ctx.r31.u32;
    if(!camera||camera!=Simpsons::worldSceneCameraId(*rt))return;
    rt->pointer(camera,0x8C,false);
    // Perspective world view only: the Apt overlay reuses this camera with a
    // square window, and its 2D draws are never resolved.
    if(PPC_LOAD_U32(camera+0x14)!=1)return;
    const float windowWidth=std::bit_cast<float>(PPC_LOAD_U32(camera+0x68)),windowHeight=std::bit_cast<float>(PPC_LOAD_U32(camera+0x6C));
    if(!(windowWidth>0)||!(windowHeight>0)||double(windowWidth)/double(windowHeight)<16.0/9.0-0.000002)return;
    const uint64_t frame=rt->engineDriver->presentationSerial();
    // A begin after this frame's resolve draws nothing that is resolved again.
    if(t.resolvedFrame==frame)return;
    std::array<double,16> projection{},view{};
    for(uint32_t i=0;i<16;++i) {
        projection[i]=double(std::bit_cast<float>(PPC_LOAD_U32(0x82CD1AB0+4*i)));
        view[i]=double(std::bit_cast<float>(PPC_LOAD_U32(0x82D0CA70+4*i)));
    }
    for(const double value:projection)if(!std::isfinite(value))return;
    for(const double value:view)if(!std::isfinite(value))return;
    if(t.frame!=frame||t.camera!=camera) {
        // Eight-sample Halton(2,3) sequence centred on the pixel, in physical pixels.
        t.frame=frame;t.camera=camera;t.sample=t.sample%8+1;
        const auto halton=[](uint32_t index,uint32_t radix) {
            double result=0,fraction=1;
            while(index){fraction/=radix;result+=fraction*double(index%radix);index/=radix;}
            return result;
        };
        const double x=halton(t.sample,2)-0.5,y=halton(t.sample,3)-0.5;
        t.jitterNdc=jitter?std::array{2.0*x/double(extent[0]),-2.0*y/double(extent[1])}:std::array{0.0,0.0};
        t.view=view;t.projection=projection;t.currentValid=true;
    }
    if(!jitter)return;
    // clip.x += jitter * w (w = view z): offset the z row of the x/y columns.
    PPC_STORE_U32(0x82CD1AB0+0x20,std::bit_cast<uint32_t>(float(projection[8]+t.jitterNdc[0])));
    PPC_STORE_U32(0x82CD1AB0+0x24,std::bit_cast<uint32_t>(float(projection[9]+t.jitterNdc[1])));
    ++t.jitteredBegins;
}

void SimpsonsNativeCameraShake(PPCContext& ctx,uint8_t* base) {
    auto* rt=Simpsons::active;
    if(!rt||base!=rt->base)return;
    static uint64_t evaluations=0,suppressed=0;++evaluations;
    if(rt->videoSettings.screenShake)return;
    // The modifier's own timer, decay and completion flag have already
    // advanced; only its contribution to the camera becomes zero.
    for(const uint32_t offset:{104u,108u,112u,120u,124u,128u})PPC_STORE_U32(ctx.r1.u32+offset,0u);
    if(++suppressed<=3||!(suppressed%1024))
        std::fprintf(stderr,"[NATIVE CAMERA SHAKE] suppressed=%llu evaluations=%llu; original modifier state retained\n",
            static_cast<unsigned long long>(suppressed),static_cast<unsigned long long>(evaluations));
}

namespace Simpsons {
namespace {
using FreeMatrix=std::array<double,16>;
FreeMatrix loadMatrix(uint8_t* base,uint32_t address) {
    FreeMatrix m{};for(uint32_t i=0;i<16;++i)m[i]=double(std::bit_cast<float>(PPC_LOAD_U32(address+4*i)));
    return m;
}
// RwMatrix rows: right(+00), up(+10), at(+20), pos(+30); the fourth word of
// each row is the flags/padding and is kept exactly.
void storeBasis(uint8_t* base,uint32_t address,const FreeMatrix& m) {
    for(uint32_t row=0;row<4;++row)for(uint32_t column=0;column<3;++column)
        PPC_STORE_U32(address+16*row+4*column,std::bit_cast<uint32_t>(float(m[row*4+column])));
}
std::array<double,3> cross(const std::array<double,3>& a,const std::array<double,3>& b) {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
std::array<double,3> normalized(std::array<double,3> v) {
    const double length=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    if(!(length>1e-9))return {0,0,1};
    for(auto& value:v)value/=length;
    return v;
}
// Affine inverse of a row-vector RW matrix (rotation part orthonormal-ish).
FreeMatrix affineInverse(const FreeMatrix& m) {
    const double a=m[0],b=m[1],c=m[2],d=m[4],e=m[5],f=m[6],g=m[8],h=m[9],i=m[10];
    const double det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    FreeMatrix r{};
    if(std::abs(det)<1e-12){r[0]=r[5]=r[10]=r[15]=1;return r;}
    r[0]=(e*i-f*h)/det;r[1]=(c*h-b*i)/det;r[2]=(b*f-c*e)/det;
    r[4]=(f*g-d*i)/det;r[5]=(a*i-c*g)/det;r[6]=(c*d-a*f)/det;
    r[8]=(d*h-e*g)/det;r[9]=(b*g-a*h)/det;r[10]=(a*e-b*d)/det;
    for(int column=0;column<3;++column)r[12+column]=-(m[12]*r[column]+m[13]*r[4+column]+m[14]*r[8+column]);
    r[15]=1;return r;
}
FreeMatrix multiply(const FreeMatrix& a,const FreeMatrix& b) {
    FreeMatrix r{};
    for(int row=0;row<4;++row)for(int column=0;column<4;++column) {
        double value=0;for(int k=0;k<4;++k)value+=a[row*4+k]*b[k*4+column];r[row*4+column]=value;
    }
    return r;
}
double axis(int16_t value,double deadzone) {
    const double v=double(value)/(value<0?32768.0:32767.0);
    const double magnitude=std::abs(v);
    if(magnitude<=deadzone)return 0;
    return (v<0?-1:1)*(magnitude-deadzone)/(1-deadzone);
}
}
void updateNativeFreeCamera(const PPCContext& ctx,uint8_t* base,uint32_t camera) {
    CameraHostState hostState;
    auto* rt=active;
    if(!rt||base!=rt->base||!rt->window||!rt->controllers)return;
    if(!camera||camera!=worldSceneCameraId(*rt))return;
    rt->pointer(camera,0x8C,false);
    if(PPC_LOAD_U32(camera+0x14)!=1)return; // Perspective world view only.
    auto& f=rt->freeCamera;
    const auto presses=rt->window->keyboard->takeActionPresses();
    const bool freeKey=presses&(1u<<uint32_t(ControlAction::FreeCamera));
    const bool photoKey=presses&(1u<<uint32_t(ControlAction::PhotoMode));
    // Controller: L3+R3 toggles the free camera; Y toggles photo mode in it.
    Platform::NativeControllers::FreeCameraInput input{};
    if(f.active) {
        // A paused game need not poll input; photo mode polls slot zero itself.
        // The active free camera owns that poll, so the game still sees neutral input.
        if(f.photo){XINPUT_STATE state{};(void)rt->controllers->state(0,state);}
        input=rt->controllers->takeFreeCameraInput();
    }
    else input.pad.wButtons=rt->controllers->slotZeroButtons();
    constexpr uint16_t thumbs=XINPUT_GAMEPAD_LEFT_THUMB|XINPUT_GAMEPAD_RIGHT_THUMB;
    const uint16_t padButtons=input.pad.wButtons;
    // Both sticks must be released after a toggle before they can toggle again.
    if((padButtons&thumbs)!=thumbs)f.thumbsReleased=true;
    const bool thumbsToggle=(padButtons&thumbs)==thumbs&&f.thumbsReleased;
    if(thumbsToggle)f.thumbsReleased=false;
    const bool padPhoto=f.active&&((input.pressedButtons|uint16_t(padButtons&~f.previousPadButtons))&XINPUT_GAMEPAD_Y);
    f.previousPadButtons=padButtons;
    const uint32_t frame=PPC_LOAD_U32(camera+4);
    if(!frame)return;
    rt->pointer(frame,0xA4,true);
    const auto finish=[&](const char* why) {
        f.active=f.photo=f.placed=false;f.zoom=1;rt->controllers->setFreeCamera(false);
        std::fprintf(stderr,"[NATIVE FREE CAMERA] off (%s)\n",why);
    };
    if(f.active&&f.camera!=camera){finish("camera changed");return;}
    if(freeKey||thumbsToggle||(photoKey&&!f.active)) {
        if(f.active)finish("toggle");
        else {
            // Start exactly at the game's current view.
            const auto ltm=loadMatrix(base,frame+0x50);
            const std::array<double,3> right{ltm[0],ltm[1],ltm[2]},up{ltm[4],ltm[5],ltm[6]},at{ltm[8],ltm[9],ltm[10]};
            const auto direction=normalized(at);
            const auto c=cross(up,direction);
            f.handedness=c[0]*right[0]+c[1]*right[1]+c[2]*right[2]<0?-1:1;
            f.position={ltm[12],ltm[13],ltm[14]};
            f.yaw=std::atan2(direction[0],direction[2]);f.pitch=std::asin(std::clamp(direction[1],-1.0,1.0));
            f.zoom=1;f.active=true;f.camera=camera;f.placed=false;f.photo=photoKey;
            LARGE_INTEGER now{};QueryPerformanceCounter(&now);f.lastTicks=now.QuadPart;
            rt->controllers->setFreeCamera(true);
            std::fprintf(stderr,"[NATIVE FREE CAMERA] on position=%.3f,%.3f,%.3f yaw=%.3f pitch=%.3f photo=%u\n",
                f.position[0],f.position[1],f.position[2],f.yaw,f.pitch,unsigned(f.photo));
            return;
        }
        return;
    }
    if(!f.active)return;
    if(photoKey||padPhoto) {
        f.photo=!f.photo;
        std::fprintf(stderr,"[NATIVE FREE CAMERA] photo mode %s\n",f.photo?"on":"off");
    }
    LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
    const double dt=std::clamp(double(now.QuadPart-f.lastTicks)/double(frequency.QuadPart),0.0,0.1);
    f.lastTicks=now.QuadPart;
    const auto& pad=input.pad;
    const auto& controls=rt->controlSettings;
    const double mouseScale=0.0025*double(controls.mouseSensitivity)/100.0;
    f.yaw+=double(input.mouseX)*mouseScale*(controls.invertMouseX?-1:1)*f.handedness;
    f.pitch-=double(input.mouseY)*mouseScale*(controls.invertMouseY?-1:1);
    f.yaw+=axis(pad.sThumbRX,0.24)*2.2*dt*f.handedness;
    f.pitch+=axis(pad.sThumbRY,0.24)*1.6*dt;
    f.pitch=std::clamp(f.pitch,-1.5,1.5);
    const std::array<double,3> at{std::cos(f.pitch)*std::sin(f.yaw),std::sin(f.pitch),std::cos(f.pitch)*std::cos(f.yaw)};
    const auto right=normalized(cross({0,1,0},at));
    const std::array<double,3> rightAxis{right[0]*f.handedness,right[1]*f.handedness,right[2]*f.handedness};
    const auto upCross=cross(at,rightAxis);
    const std::array<double,3> up{upCross[0]*f.handedness,upCross[1]*f.handedness,upCross[2]*f.handedness};
    double speed=5.0;
    if(pad.wButtons&XINPUT_GAMEPAD_A)speed*=3;
    if(pad.wButtons&XINPUT_GAMEPAD_X)speed*=0.25;
    const double forward=axis(pad.sThumbLY,0.24),strafe=axis(pad.sThumbLX,0.24)*f.handedness;
    const double rise=(double(pad.bRightTrigger)-double(pad.bLeftTrigger))/255.0;
    for(int i=0;i<3;++i)f.position[size_t(i)]+=(at[size_t(i)]*forward+right[size_t(i)]*strafe)*speed*dt;
    f.position[1]+=rise*speed*dt;
    if(pad.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER)f.zoom*=std::exp(-1.2*dt);
    if(pad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER)f.zoom*=std::exp(1.2*dt);
    if((input.pressedButtons&XINPUT_GAMEPAD_RIGHT_THUMB)&&!(pad.wButtons&XINPUT_GAMEPAD_LEFT_THUMB))f.zoom=1;
    f.zoom=std::clamp(f.zoom,0.25,6.0);
    FreeMatrix desired{rightAxis[0],rightAxis[1],rightAxis[2],0,up[0],up[1],up[2],0,at[0],at[1],at[2],0,
        f.position[0],f.position[1],f.position[2],1};
    // LTM = modelling * parent LTM; keep any parent frame the camera hangs from.
    const uint32_t parent=PPC_LOAD_U32(frame+4);
    FreeMatrix modelling=desired;
    if(parent&&parent!=frame) {
        rt->pointer(parent,0xA4,false);
        auto parentLtm=loadMatrix(base,parent+0x50);
        for(int row=0;row<4;++row)parentLtm[size_t(row*4+3)]=row==3?1.0:0.0;
        modelling=multiply(desired,affineInverse(parentLtm));
    }
    storeBasis(base,frame+0x10,modelling);storeBasis(base,frame+0x50,desired);
    if(!f.placed) {
        f.placed=true;
        std::fprintf(stderr,"[NATIVE FREE CAMERA] placing camera=%08X frame=%08X parent=%08X handedness=%.0f\n",camera,frame,parent,f.handedness);
    }
}
}
void SimpsonsNativePhotoPause(PPCContext& ctx,uint8_t* base) {
    auto* rt=Simpsons::active;
    if(!rt||base!=rt->base)return;
    // r3 is the original pause query (nonzero while the pause menu runs).
    if((ctx.r3.u32&0xff)&&rt->engineDriver)rt->engineDriver->noteGameplayPaused();
    // Same skip as the pause menu's paused state; rendering continues.
    if(!rt->freeCamera.photo)return;
    ctx.r3.u64=1;
    static uint64_t paused=0;
    if(!(paused++%600))std::fprintf(stderr,"[NATIVE FREE CAMERA] photo mode paused gameplay update count=%llu\n",static_cast<unsigned long long>(paused));
}
void SimpsonsNativeHudBegin(PPCContext&,uint8_t* base) {
    auto* rt=Simpsons::active;
    if(rt&&base==rt->base&&rt->engineDriver)rt->engineDriver->photoHud(true);
}
void SimpsonsNativeHudEnd(PPCContext&,uint8_t* base) {
    auto* rt=Simpsons::active;
    if(rt&&base==rt->base&&rt->engineDriver)rt->engineDriver->photoHud(false);
}
bool SimpsonsNativePhotoHud(PPCContext&,uint8_t* base) {
    auto* rt=Simpsons::active;
    // Photo mode skips the Apt overlay render (HUD and subtitles).
    return rt&&base==rt->base&&rt->freeCamera.photo;
}
