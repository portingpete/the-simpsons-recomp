#include "native_mouse_camera.h"
#include "native_controllers.h"
#include "native_mouse_input.h"
#include "engine_cpu_calls.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
struct HostState {
    const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
    HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
};
bool tracing() {
    static const bool enabled=[] {
        const auto value=std::getenv("SIMPSONS_MOUSE_CAMERA_TRACE");
        return value&&std::strcmp(value,"1")==0;
    }();
    return enabled;
}
bool traceCamera(PPCContext& ctx,uint8_t* base,bool look,const char* phase,
                 uint32_t vtable=0,uint32_t method=0,uint32_t character=0) {
    if(!tracing())return false;
    struct Seen {uint32_t owner,slot;const char* phase;};
    static thread_local std::array<Seen,64> seen{};
    static thread_local size_t count{};
    for(size_t i=0;i<count;++i)
        if(seen[i].owner==ctx.r31.u32&&seen[i].slot==ctx.r30.u32&&seen[i].phase==phase)return false;
    if(count==seen.size())return false;
    seen[count++]={ctx.r31.u32,ctx.r30.u32,phase};
    const auto manager=PPC_LOAD_U32(0x82D08B10);
    uint32_t primary{},secondary{},viewports{};
    std::array<uint32_t,4> indices{},selected{};
    if(manager>=0x10000&&manager<=UINT32_MAX-0x12C) {
        Simpsons::active->probe(manager,0x12C,false);
        primary=PPC_LOAD_U32(manager);secondary=PPC_LOAD_U32(manager+8);
        viewports=PPC_LOAD_U32(manager+0x128);
        const auto controllers=PPC_LOAD_U32(manager+0x10);
        if(controllers>=0x10000&&controllers<=UINT32_MAX-440*4&&viewports==4) {
            Simpsons::active->probe(controllers,440*4,false);
            for(uint32_t i=0;i<4;++i) {
                const auto controller=controllers+440*i;
                indices[i]=PPC_LOAD_U32(controller+0x108);
                if(indices[i]<12)selected[i]=PPC_LOAD_U32(controller+0x78+12*indices[i]);
            }
        }
    }
    std::fprintf(stderr,"[NATIVE MOUSE CAMERA GATE] method=%s phase=%s slot=%u owner=%08X vtable=%08X input=%08X character=%08X manager=%08X roots=%08X,%08X count=%u selected=%u:%08X,%u:%08X,%u:%08X,%u:%08X\n",
        look?"look":"orbit",phase,ctx.r30.u32,ctx.r31.u32,vtable,method,character,manager,
        primary,secondary,viewports,indices[0],selected[0],indices[1],selected[1],indices[2],selected[2],indices[3],selected[3]);
    std::fflush(stderr);
    return true;
}
bool selectedCamera(Simpsons::Runtime& rt,uint8_t* base,uint32_t owner) {
    const auto manager=PPC_LOAD_U32(0x82D08B10);
    if(!manager)return false;
    rt.probe(manager,0x12C,false);
    if(PPC_LOAD_U32(manager)!=0x820B6DEC||PPC_LOAD_U32(manager+8)!=0x820B6DE8||
       PPC_LOAD_U32(manager+0x128)!=4)return false;
    const auto controllers=PPC_LOAD_U32(manager+0x10);
    if(!controllers)return false;
    rt.probe(controllers,440*4,false);
    // 827126F0 updates all weighted cameras during a blend. Only the selected
    // entry should consume the raw displacement, even if an outgoing camera
    // appears earlier in that loop. Each viewport owns one 440-byte controller
    // and selects one of twelve pointer/weight/speed records at +0x78.
    for(uint32_t viewport=0;viewport<4;++viewport) {
        const auto controller=controllers+440*viewport;
        const auto index=PPC_LOAD_U32(controller+0x108);
        if(index<12&&PPC_LOAD_U32(controller+0x78+12*index)==owner)return true;
    }
    return false;
}
bool cameraMotion(PPCContext& ctx,uint8_t* base,bool look,float& pitch,float& yaw) {
    auto* rt=Simpsons::active;
    if(!rt||base!=rt->base||!rt->controllers)return false;
    if(ctx.r30.u32!=0){traceCamera(ctx,base,look,"slot");return false;}
    const auto owner=ctx.r31.u32;
    if(owner<0x10000||owner>UINT32_MAX-0x17C){traceCamera(ctx,base,look,"owner");return false;}
    rt->probe(owner,look?0x90:0x17C,false);
    const auto vtable=PPC_LOAD_U32(owner);
    if(vtable<0x82000000||vtable>0x82FFFFE0){traceCamera(ctx,base,look,"vtable",vtable);return false;}
    rt->probe(vtable+0x1C,4,false);
    // The original inherited input virtual may occur in several camera types.
    // Its actual method, controlling character and slot gate this consumer.
    const auto method=PPC_LOAD_U32(vtable+0x1C),character=PPC_LOAD_U32(owner+(look?0x70:0x10C));
    if(method!=(look?0x82960870u:0x8295C9C0u)||!character){traceCamera(ctx,base,look,"method",vtable,method,character);return false;}
    if(!selectedCamera(*rt,base,owner)){traceCamera(ctx,base,look,"selected",vtable,method,character);return false;}
    const auto motion=rt->controllers->takeMouseMotion();
    if(!motion.active){traceCamera(ctx,base,look,"inactive",vtable,method,character);return false;}
    pitch=yaw=0;
    if(!motion.x&&!motion.y)return true;
    // Retain the same player's original horizontal/vertical inversion options.
    // These original helpers are read-only; EngineCpuCalls isolates call-clobbers.
    Simpsons::EngineCpuCalls cpu(ctx,base);
    const auto& controls=rt->controlSettings;
    const bool invertX=(cpu.invoke(0x82A1AAA8,0)!=0)!=controls.invertMouseX;
    const bool invertY=(cpu.invoke(0x82A1AA38,0)!=0)!=controls.invertMouseY;
    constexpr double turn=6.283185307179586476925286766559;
    const double sensitivity=double(controls.mouseSensitivity)/100.0;
    const double x=double(motion.x)*Simpsons::nativeMouseRadiansPerCount*sensitivity;
    const double y=double(motion.y)*Simpsons::nativeMouseRadiansPerCount*sensitivity;
    // Original spherical yaw wraps one revolution at a time. Reducing complete
    // turns preserves direction for any unsaturated raw-count accumulation.
    yaw=float(std::remainder(invertX?-x:x,turn));
    pitch=float((invertY?-y:y)*(look?-1.0:1.0));
    static thread_local uint32_t traceCount{};
    if(tracing()&&traceCount++<512) {
        std::fprintf(stderr,"[NATIVE MOUSE CAMERA] method=%s owner=%08X counts=%d,%d radians=%.9g,%.9g dt=%.9g\n",
            look?"look":"orbit",owner,motion.x,motion.y,double(pitch),double(yaw),
            double(std::bit_cast<float>(PPC_LOAD_U32(owner+0x48))));
        std::fflush(stderr);
    }
    return true;
}
}

void SimpsonsNativeMouseCameraDispatch(PPCContext& ctx,uint8_t* base) {
    if(!tracing())return;
    auto* rt=Simpsons::active;
    if(!rt||base!=rt->base)return;
    HostState host;
    const auto owner=ctx.r3.u32;
    if(owner<0x10000||owner>UINT32_MAX-0x17C)return;
    rt->probe(owner,0x17C,false);
    const auto vtable=PPC_LOAD_U32(owner);
    if(vtable<0x82000000||vtable>0x82FFFFD0)return;
    rt->probe(vtable,0x2C,false);
    const auto method=PPC_LOAD_U32(vtable+0x1C);
    const auto orbitCharacter=PPC_LOAD_U32(owner+0x10C),lookCharacter=PPC_LOAD_U32(owner+0x70);
    uint32_t controllerVtable{},controllerMethod{};
    const auto character=method==0x8295C9C0?orbitCharacter:method==0x82960870?lookCharacter:0;
    if(character>=0x10000&&character<=UINT32_MAX-0x7FC) {
        rt->probe(character+0x7F0,4,false);controllerVtable=PPC_LOAD_U32(character+0x7F0);
        if(controllerVtable>=0x82000000&&controllerVtable<=0x82FFFFF4) {
            rt->probe(controllerVtable+8,4,false);controllerMethod=PPC_LOAD_U32(controllerVtable+8);
        }
    }
    auto view=ctx;view.r31.u32=owner;view.r30.u32=UINT32_MAX;
    const bool look=method==0x82960870;
    if(traceCamera(view,base,look,"dispatch",vtable,method,look?lookCharacter:orbitCharacter)) {
        std::fprintf(stderr,"[NATIVE MOUSE CAMERA DISPATCH] owner=%08X vtable=%08X virtuals=%08X,%08X,%08X,%08X,%08X character_orbit=%08X character_look=%08X orbit_locked=%u controller_vtable=%08X controller_method=%08X\n",
            owner,vtable,PPC_LOAD_U32(vtable+0x18),method,PPC_LOAD_U32(vtable+0x20),
            PPC_LOAD_U32(vtable+0x24),PPC_LOAD_U32(vtable+0x28),orbitCharacter,lookCharacter,
            unsigned(PPC_LOAD_U8(owner+0xA2)),controllerVtable,controllerMethod);
        std::fflush(stderr);
    }
}

void SimpsonsNativeMouseCameraEligibility(PPCContext& ctx,uint8_t* base) {
    if(!tracing())return;
    auto* rt=Simpsons::active;
    if(!rt||base!=rt->base)return;
    HostState host;
    const auto owner=ctx.r31.u32;
    if(owner<0x10000||owner>UINT32_MAX-0x17C)return;
    rt->probe(owner,0x17C,false);
    const auto vtable=PPC_LOAD_U32(owner);
    if(vtable<0x82000000||vtable>0x82FFFFE0)return;
    rt->probe(vtable+0x1C,4,false);
    auto view=ctx;view.r30.u32=ctx.r3.u32;
    traceCamera(view,base,false,"eligibility",vtable,PPC_LOAD_U32(vtable+0x1C),PPC_LOAD_U32(owner+0x10C));
}

void SimpsonsNativeMouseOrbit(PPCContext& ctx,uint8_t* base) {
    HostState host;float pitch{},yaw{};
    if(!cameraMotion(ctx,base,false,pitch,yaw))return;
    // 8295CB18 runs after controller deadzone, speed*time, inversion, mode
    // scaling and minimum-angle filtering. The following original test then
    // calls 82A2A480, preserving pitch limits, yaw wrapping and activity timers.
    Simpsons::active->probe(ctx.r1.u32+0x68,8,true);
    PPC_STORE_U32(ctx.r1.u32+0x68,std::bit_cast<uint32_t>(pitch));
    PPC_STORE_U32(ctx.r1.u32+0x6C,std::bit_cast<uint32_t>(yaw));
}

void SimpsonsNativeMouseLook(PPCContext& ctx,uint8_t* base) {
    HostState host;float pitch{},yaw{};
    if(!cameraMotion(ctx,base,true,pitch,yaw))return;
    // 8296099C is the matching consumer for the alternate look camera.
    // Its coordinate convention negates vertical input before inversion.
    // Keep 82960328's original angular limits and wrapping.
    ctx.f28.f64=pitch;ctx.f29.f64=yaw;
}
