#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/native_controllers.h"
#include "runtime/native_mouse_camera.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Simpsons::Platform {
struct NativeControllersTestAccess {
    static void bind(NativeControllers& source,decltype(&XInputGetState) query) {
        source.getState=query;source.disconnectedPollMs=0;
    }
};
}
namespace {
using namespace Simpsons;
constexpr uint32_t orbit=0x20000,look=0x21000,character=0x22000,
                   manager=0x24000,cameraControllers=0x25000,input=0x26000,inputSettings=0x27000,
                   engine=0x28000;
size_t checks{};bool physical{};uint32_t slot{};float stickX{},stickY{};
void need(bool value,const char* why){++checks;if(!value)throw Failure(why);}
void putFloat(uint8_t* base,uint32_t at,float value){PPC_STORE_U32(at,std::bit_cast<uint32_t>(value));}
float getFloat(uint8_t* base,uint32_t at){return std::bit_cast<float>(PPC_LOAD_U32(at));}
bool close(float actual,float expected){return std::abs(actual-expected)<0.000002f;}
DWORD WINAPI stateQuery(DWORD index,XINPUT_STATE* result) noexcept {
    *result={};return index==0&&physical?ERROR_SUCCESS:ERROR_DEVICE_NOT_CONNECTED;
}
uint32_t mathAllocations{};
void mathAllocation(PPCContext& ctx,uint8_t*) {
    need(ctx.r3.u32==16384&&ctx.r4.u32==0x40401&&
         (ctx.lr==0x823EB9C8||ctx.lr==0x823EBB50)&&mathAllocations<2,
         "Original vector normalization allocation ABI changed");
    ctx.r3.u32=0x40000+0x4000*mathAllocations++;
}
void initializeVectorMath(Runtime& rt,const PPCContext& entry,uint8_t* base) {
    rt.map(0x40000,0x8000,true,"original camera vector sqrt and reciprocal-sqrt tables");
    PPC_STORE_U32(0x82D0CA68,engine);PPC_STORE_U32(0x82D0C910,0x500);
    constexpr uint32_t allocationEntry=0x8238E880;
    struct AllocationDispatch {
        uint8_t* base;PPCFunc* previous;
        explicit AllocationDispatch(uint8_t* memory):base(memory),previous(PPC_LOOKUP_FUNC(base,allocationEntry)) {
            need(previous!=nullptr,"Original math allocator dispatch is absent");
            PPC_LOOKUP_FUNC(base,allocationEntry)=mathAllocation;
            PPC_STORE_U32(engine+0x108,allocationEntry);
        }
        ~AllocationDispatch(){PPC_LOOKUP_FUNC(base,allocationEntry)=previous;PPC_STORE_U32(engine+0x108,0);}
    } dispatch(base);
    EngineCpuCalls cpu(entry,base);
    need(cpu.invoke(0x823EB998)==1&&cpu.invoke(0x823EBB20)==1&&
         PPC_LOAD_U32(engine+0x500)==0x40000&&PPC_LOAD_U32(engine+0x504)==0x44000,
         "Original camera vector math initialization failed");
}
struct Abi {
    uint64_t sp,lr;std::array<uint64_t,18> gpr,fpr;
    bool operator==(const Abi&)const=default;
};
Abi abi(const PPCContext& c) {
    return {c.r1.u64,c.lr,
        {c.r14.u64,c.r15.u64,c.r16.u64,c.r17.u64,c.r18.u64,c.r19.u64,c.r20.u64,c.r21.u64,c.r22.u64,
         c.r23.u64,c.r24.u64,c.r25.u64,c.r26.u64,c.r27.u64,c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64},
        {c.f14.u64,c.f15.u64,c.f16.u64,c.f17.u64,c.f18.u64,c.f19.u64,c.f20.u64,c.f21.u64,c.f22.u64,
         c.f23.u64,c.f24.u64,c.f25.u64,c.f26.u64,c.f27.u64,c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64}};
}
void exercise(const char* image) {
    Runtime rt;rt.load(image);PPCContext entry{};rt.initialize(entry);auto* base=rt.base;
    rt.map(orbit,0xA000,true,"original native mouse angular-consumer fixture");
    initializeVectorMath(rt,entry,base);
    rt.controllers=std::make_shared<Platform::NativeControllers>();
    Platform::NativeControllersTestAccess::bind(*rt.controllers,stateQuery);
    auto keys=std::make_shared<Platform::NativeKeyboard>();rt.controllers->attachKeyboard(keys);
    keys->focus(true);keys->captureMouse(true);
    PPC_STORE_U32(orbit,0x8218AF48);PPC_STORE_U32(orbit+0x10C,character);
    PPC_STORE_U32(look,0x8218B204);PPC_STORE_U32(look+0x70,character);
    PPC_STORE_U32(character+0x7F0,0x8200505C);
    need(PPC_LOAD_U32(0x8200505C+8)==0x823BE620&&PPC_LOAD_U32(0x823BE620)==0x8063002C&&
         PPC_LOAD_U32(0x823BE624)==0x4E800020,
         "Original live character controlling-player getter changed");
    PPC_STORE_U32(0x82D08B10,manager);PPC_STORE_U32(manager,0x820B6DEC);
    PPC_STORE_U32(manager+8,0x820B6DE8);PPC_STORE_U32(manager+0x128,4);
    PPC_STORE_U32(manager+0x10,cameraControllers);
    PPC_STORE_U32(0x82D08DA8,input);PPC_STORE_U32(0x82D08DAC,inputSettings);
    putFloat(base,inputSettings+1492,0.01f);
    PPC_STORE_U32(0x82D08D90,0); // Original defaults: horizontal normal, vertical convention retained.
    PPC_STORE_U32(0x82D572BC,0x1234);
    putFloat(base,orbit+0x70,2.8f);putFloat(base,orbit+0x74,0.1f);
    putFloat(base,look+0x58,0.1f);putFloat(base,look+0x5C,0.1f);
    {
        EngineCpuCalls cpu(entry,base);
        need(cpu.invoke(0x826BE340,input,0)==input+40&&cpu.invoke(0x826BE340,input,5)==input+40&&
             cpu.invoke(0x826BE340,input,4)==0&&cpu.invoke(0x826BE340,input,6)==0,
             "Original logical controller IDs no longer select their documented input records");
    }
    const auto poll=[&](int32_t x=0,int32_t y=0) {
        keys->mouseMotion(x,y);XINPUT_STATE value{};
        need(rt.controllers->state(0,value)==ERROR_SUCCESS,"Native mouse source disconnected");
        return value;
    };
    const auto reset=[&](bool alternate,float dt=1.0f,float speed=2.0f) {
        const auto owner=alternate?look:orbit;
        PPC_STORE_U32(cameraControllers+0x78,owner);
        putFloat(base,owner+0x48,dt);
        putFloat(base,owner+(alternate?0x60:0x78),speed);
        putFloat(base,owner+(alternate?0x64:0x7C),speed);
        putFloat(base,owner+(alternate?0x7C:0x124),0.5f);
        putFloat(base,owner+(alternate?0x80:0x128),1.0f);
    };
    const auto run=[&](bool alternate) {
        // The original getter is a direct AOT call. Populate its real slot
        // record so legacy checks execute the original vector/deadzone math.
        putFloat(base,input+60*slot+48,stickX);putFloat(base,input+60*slot+52,stickY);
        PPC_STORE_U32(character+0x81C,slot);
        EngineCpuCalls cpu(entry,base);const auto before=abi(cpu.registers());
        cpu.invoke(alternate?0x82960870:0x8295C9C0,alternate?look:orbit);
        need(abi(cpu.registers())==before,"Original mouse camera consumer changed nonvolatile ABI");
        return std::array{getFloat(base,(alternate?look:orbit)+(alternate?0x7C:0x124)),
                          getFloat(base,(alternate?look:orbit)+(alternate?0x80:0x128))};
    };
    constexpr std::array orbitWords{0xC0010068u,0xFC000210u,0xFF00F000u,0x41990014u};
    constexpr std::array lookWords{0xFC00E210u,0xFF00F000u,0x41990010u,0xFC00EA10u};
    for(uint32_t i=0;i<4;++i) {
        need(PPC_LOAD_U32(0x8295CB18+4*i)==orbitWords[i],"Original orbit consumer hook bytes differ");
        need(PPC_LOAD_U32(0x8296099C+4*i)==lookWords[i],"Original look consumer hook bytes differ");
    }
    // Full original angular consumers execute their time scaling, thresholds,
    // native hook, original pitch clamp/yaw wrap, activity timers and epilogues.
    for(bool alternate:{false,true}) {
        reset(alternate);poll(1,1);const auto gentle=run(alternate);
        need(close(gentle[0],alternate?0.5025f:0.4975f)&&close(gentle[1],1.0025f),
             "Single raw count was deadzoned or had the wrong direction");
        reset(alternate);poll(100,20);const auto ordinary=run(alternate);
        reset(alternate,0.125f,19.0f);poll(100,20);const auto fasterFrame=run(alternate);
        need(ordinary==fasterFrame,"Raw mouse rotation depends on frame time or stick speed");
        need(close(ordinary[0],alternate?0.55f:0.45f)&&close(ordinary[1],1.25f),
             "Raw counts did not produce proportional angular displacement");
        const auto consumed=run(alternate);
        need(consumed==ordinary,"Consumed mouse displacement replayed on a second camera query");
        poll();need(run(alternate)==ordinary,"Stopped mouse left an input velocity tail");
        reset(alternate);poll(100,0);const auto single=run(alternate);
        reset(alternate);poll(25,0);run(alternate);poll(75,0);const auto split=run(alternate);
        need(close(single[1],split[1]),"Mouse sensitivity depends on poll batching");
        reset(alternate);poll(-100,-20);const auto opposite=run(alternate);
        need(close(opposite[0],alternate?0.45f:0.55f)&&close(opposite[1],0.75f),
             "Negative mouse displacement did not preserve direction");
    }
    for(bool alternate:{false,true}) for(uint32_t sensitivity:{25u,100u,300u}) for(bool invert:{false,true}) {
        rt.controlSettings.mouseSensitivity=sensitivity;
        rt.controlSettings.invertMouseX=rt.controlSettings.invertMouseY=invert;
        reset(alternate);poll(10,2);const auto result=run(alternate);
        const float sign=invert?-1.0f:1.0f,scale=float(sensitivity)/100.0f;
        need(close(result[0],0.5f+(alternate?1.0f:-1.0f)*sign*0.005f*scale)&&close(result[1],1.0f+sign*0.025f*scale),
             "Mouse sensitivity or native inversion did not reach the original angular consumer");
    }
    rt.controlSettings={};
    reset(false);poll(0,10000);const auto limited=run(false);
    need(close(limited[0],0.1f),"Native mouse bypassed the original pitch limit");
    need(PPC_LOAD_U32(orbit+0x144)==0x1234&&PPC_LOAD_U32(orbit+0x148)==0x1234,
         "Original camera activity timers did not follow native movement");
    reset(false);poll(-1000,0);const auto wrapped=run(false);
    need(close(wrapped[1],float(6.2831853071795864769-1.5)),"Native mouse bypassed original yaw wrapping");
    reset(false);poll(100000,0);const auto manyTurns=run(false);
    const auto expectedYaw=float(std::fmod(1.0+100000*nativeMouseRadiansPerCount,6.2831853071795864769));
    need(close(manyTurns[1],expectedYaw),"Unsaturated raw motion exceeded the original wrap domain");
    // Exact production hook state: orbit writes only two stack words; alternate
    // writes only the two delta FPRs. Host FP/last error/currentContext survive.
    auto hook=entry;hook.r31.u32=orbit;hook.r30.u32=0;
    PPC_STORE_U32(cameraControllers+0x78,orbit);
    poll(10,2);const auto before=hook;const auto hostFp=PPCFPSCRRegister::getcsr();SetLastError(0x5147);
    SimpsonsNativeMouseOrbit(hook,base);
    need(std::memcmp(&hook,&before,sizeof(hook))==0&&PPCFPSCRRegister::getcsr()==hostFp&&
         GetLastError()==0x5147&&currentContext==&entry,"Orbit hook changed PPC or host state");
    need(close(getFloat(base,hook.r1.u32+0x68),-0.005f)&&close(getFloat(base,hook.r1.u32+0x6C),0.025f),
         "Orbit hook did not publish the original angular argument pair");
    hook=entry;hook.r31.u32=look;hook.r30.u32=0;PPC_STORE_U32(cameraControllers+0x78,look);
    poll(10,2);auto allowed=hook;
    allowed.f28.f64=0.005f;allowed.f29.f64=0.025f;SetLastError(0x5148);
    SimpsonsNativeMouseLook(hook,base);
    need(std::memcmp(&hook,&allowed,sizeof(hook))==0&&PPCFPSCRRegister::getcsr()==hostFp&&
         GetLastError()==0x5148&&currentContext==&entry,"Look hook changed state outside its angular FPRs");
    // A physical controller or an uncaptured mouse keeps the original stick
    // path, including its original frame-time multiplier and angular threshold.
    keys->captureMouse(false);poll();stickX=0.2f;stickY=0.1f;
    reset(false,0.5f,2.0f);const auto legacy=run(false);
    const float filtered=(std::sqrt(0.05f)-0.01f)/(0.99f*std::sqrt(0.05f));
    // The original 4096-entry square-root table approximates ideal math.
    const auto closeLegacy=[](float actual,float expected){return std::abs(actual-expected)<0.00003f;};
    need(closeLegacy(legacy[0],0.5f-0.1f*filtered)&&closeLegacy(legacy[1],1.0f+0.2f*filtered),
         "Uncaptured mouse changed original controller behavior");
    physical=true;keys->captureMouse(true);poll(100,20);reset(false,0.25f,2.0f);const auto pad=run(false);
    need(closeLegacy(pad[0],0.5f-0.05f*filtered)&&closeLegacy(pad[1],1.0f+0.1f*filtered),
         "Physical controller lost its original time-dependent behavior");
    physical=false;stickX=stickY=0;slot=1;poll(100,20);reset(false);const auto secondary=run(false);
    need(secondary==std::array{0.5f,1.0f},"Primary native mouse steered a secondary player camera");
    slot=0;need(close(run(false)[1],1.25f),"Excluded secondary camera consumed primary raw motion");
    reset(false);reset(true);poll(100,20);
    need(run(false)==std::array{0.5f,1.0f},"Outgoing blended camera consumed active camera mouse motion");
    const auto selected=run(true);
    need(close(selected[0],0.55f)&&close(selected[1],1.25f),"Selected blend camera lost native displacement");
    // Prior Win32 down produced negative signed XInput Y. The original input
    // staging function negates that axis before the camera's inversion option.
    // Prove that stage directly, then compare both consumers under all options.
    constexpr uint32_t profile=0x2A000,players=0x2B000,axisController=0x2C000,axes=0x2C100;
    rt.map(profile,0x4000,true,"original mouse direction and player inversion fixture");
    PPC_STORE_U32(axisController+40,axes);
    PPC_STORE_U32(axes,0);PPC_STORE_U32(axes+4,0);
    PPC_STORE_U32(axes+8,500);PPC_STORE_U32(axes+12,uint32_t(-250));
    {
        EngineCpuCalls cpu(entry,base);cpu.invoke(0x826BD798,input,axisController,input+40);
        need(close(getFloat(base,input+48),500.0f/950)&&close(getFloat(base,input+52),250.0f/950),
             "Original input stage no longer negates signed right-stick Y");
    }
    PPC_STORE_U32(0x82D08D90,profile);PPC_STORE_U32(0x82D08B9C,players);
    for(uint32_t options:{0u,2u,4u,6u})for(bool alternate:{false,true}) {
        PPC_STORE_U32(profile+160,options);stickX=0.2f;stickY=0.1f;
        keys->captureMouse(false);poll();reset(alternate,0.5f,2.0f);const auto beforeMouse=run(alternate);
        keys->captureMouse(true);poll(100,20);reset(alternate);const auto mouse=run(alternate);
        need((beforeMouse[0]-0.5f)*(mouse[0]-0.5f)>0&&
             (beforeMouse[1]-1.0f)*(mouse[1]-1.0f)>0,
             "Native mouse changed direction relative to original player inversion options");
    }
    PPC_STORE_U32(0x82D08D90,0);PPC_STORE_U32(0x82D08B9C,0);
    stickX=stickY=0;
    std::printf("PASS native mouse camera: %zu checks; original orbit/look consumers, proportional raw displacement, frame independence, clamp/wrap, one-time consumption, controller priority and ABI\n",checks);
}
}
int main(int argc,char** argv) {
    try{need(argc==2,"Original image required");exercise(argv[1]);return 0;}
    catch(const std::exception& error){std::fprintf(stderr,"FAIL native mouse camera: %s\n",error.what());return 1;}
}
