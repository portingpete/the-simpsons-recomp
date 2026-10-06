#include "native_menu_mouse.h"
#include "native_control_menu.h"
#include "native_controllers.h"
#include "native_window.h"
#include "engine_cpu_calls.h"
#include "engine_driver.h"
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint32_t exitQueryCallback=0x8239C7B0;
struct HostState {
    const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
    HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
};
bool mouseTrace() {
    static const bool enabled=[] {char value[2]{};return GetEnvironmentVariableA("SIMPSONS_MENU_MOUSE_TRACE",value,2)&&value[0]=='1';}();
    return enabled;
}
void traceState(const char* phase,std::array<uint32_t,8> state) {
    if(!mouseTrace())return;
    static thread_local uint32_t prints=0;
    static thread_local const char* previousPhase=nullptr;
    static thread_local std::array<uint32_t,8> previous{};
    if(prints>=96||(previousPhase==phase&&previous==state))return;
    previousPhase=phase;previous=state;++prints;
    std::fprintf(stderr,"[NATIVE MENU QUERY] phase=%s words=%08X %08X %08X %08X %08X %08X %08X %08X\n",
        phase,state[0],state[1],state[2],state[3],state[4],state[5],state[6],state[7]);
}
uint32_t menuReceiver(Simpsons::Runtime& rt,uint8_t* base,uint32_t manager,uint32_t controllerDomain) {
    if(!manager)return 0;rt.probe(manager,0x64,false);
    const auto count=PPC_LOAD_U32(manager+0x44);
    if(!count||count>16||controllerDomain>=32)return 0;
    // Original 827F1BA8 searches the same stack from its newest subscriber.
    // The original dispatcher obtains the controller event domain from its
    // input driver; it is distinct from the UI event/button enumeration.
    for(uint32_t i=count;i;--i) {
        const auto receiver=PPC_LOAD_U32(manager+4*i);
        if(!receiver)continue;rt.probe(receiver,0x64,false);
        if((PPC_LOAD_U32(receiver+12)&(1u<<controllerDomain))&&
           (PPC_LOAD_U32(receiver+8)&((1u<<6)|(1u<<7)|(1u<<10))))return receiver;
    }
    return 0;
}
uint32_t scriptQuery(Simpsons::EngineCpuCalls& cpu,uint8_t* base,uint32_t target,
                     const Simpsons::NativeMenuPoint& point,uint32_t update,double center) {
    auto& c=cpu.registers();const auto storage=c.r1.u32;
    constexpr std::array offsets{0xA0u,0xB0u,0xC0u,0xD0u};
    const std::array values{point.x,point.y,double(update),center};
    for(size_t i=0;i<values.size();++i) {
        char text[16]{};std::snprintf(text,sizeof(text),"%.4f",values[i]);
        std::memcpy(PPCGuestPointer(base,storage+offsets[i],16,true),text,16);
    }
    const char method[]="nativeMouseQuery";
    std::memcpy(PPCGuestPointer(base,storage+0x80,sizeof(method),true),method,sizeof(method));
    const auto result=storage+0x60;std::memset(PPCGuestPointer(base,result,32,true),0,32);
    c.r7.u64=storage+offsets[0];c.r8.u64=storage+offsets[1];
    c.r9.u64=storage+offsets[2];c.r10.u64=storage+offsets[3];
    // The original bridge accepts string args and copies the return's string
    // representation. Keep all buffers beyond its outgoing argument area.
    cpu.invoke(0x827BF8F8,storage+0x80,result,target,4);
    const auto* text=reinterpret_cast<const char*>(PPCGuestPointer(base,result,32,false));
    if(mouseTrace()) {
        static thread_local uint32_t prints=0,previousTarget=0;
        static thread_local std::array<char,32> previous{};
        if(prints<64&&(!prints||previousTarget!=target||std::memcmp(previous.data(),text,32)||update)) {
            ++prints;previousTarget=target;std::memcpy(previous.data(),text,32);
            const char* path=target?reinterpret_cast<const char*>(PPCGuestPointer(base,target,1,false)):"";
            std::fprintf(stderr,"[NATIVE MENU QUERY] target=%08X path='%.63s' point=%.4f,%.4f update=%u center=%.4f raw='%.31s'\n",
                target,path,point.x,point.y,uint32_t(update),center,text);
        }
    }
    uint32_t hit=0;const auto end=static_cast<const char*>(std::memchr(text,0,32));
    if(!end)return 0;
    const auto parsed=std::from_chars(text,end,hit);
    return parsed.ec==std::errc()&&parsed.ptr==end&&(hit==0||hit==1||(hit>=102&&hit<=113))?hit:0;
}
}

void SimpsonsNativeMainMenuExit(PPCContext& ctx,uint8_t* base) {
    HostState host;auto* rt=Simpsons::active;
    if(!rt||base!=rt->base||!rt->window)return;
    // The original MainMenu Select path copies SetSafeString into this
    // string object's data pointer before comparing the stock menu IDs.
    // Both physical/controller input and the native mouse use that handler.
    constexpr char selection[]="NativeExitGame";
    const auto text=PPC_LOAD_U32(ctx.r1.u32+80);
    if(!text)return;
    // Stock selections can end at a mapped boundary. Read only the bytes
    // needed to reject a shorter/different ID, including the exact final NUL.
    for(size_t i=0;i<sizeof(selection);++i) {
        const auto at=uint64_t(text)+i;
        if(at>UINT32_MAX||*PPCGuestPointer(base,uint32_t(at),1,false)!=uint8_t(selection[i]))return;
    }
    if(rt->nativeMainMenuExitQuery)return;
    const auto manager=PPC_LOAD_U32(0x82D08E60);
    if(!manager)return;
    rt->probe(manager,160,false);
    // The original popup owner has five slots. Do not create an unowned
    // request when its enqueue path cannot retain it.
    if(PPC_LOAD_U32(manager+128)>=5)return;
    Simpsons::EngineCpuCalls cpu(ctx,base);
    const auto request=cpu.invoke(0x8269BD70,96);
    if(!request)return;
    cpu.invoke(0x823A7E28,request);
    constexpr char question[]="Are you sure you want to exit the game?";
    const auto textOut=cpu.registers().r1.u32+0x80;
    std::memcpy(PPCGuestPointer(base,textOut,sizeof(question),true),question,sizeof(question));
    cpu.invoke(0x823A70D8,request,textOut);
    cpu.invoke(0x823A7218,request,2); // Original Yes/No popup type.
    cpu.invoke(0x823A7250,request,0);
    cpu.invoke(0x823A7278,request,1);
    cpu.invoke(0x823A72C8,request,1);
    cpu.invoke(0x823A73E0,request,0);
    // Type two has separate No/Yes buttons; its Yes shortcut forwards the
    // constructor's selected value zero. Only a successfully queued popup
    // may confirm, since the not-ready path also calls back with zero.
    PPC_STORE_U32(request+92,exitQueryCallback);
    rt->nativeMainMenuExitQuery=request;
    rt->nativeMainMenuExitQueryQueued=false;
    std::fprintf(stderr,"[NATIVE MAIN MENU] Exit Game selected; confirmation requested\n");
    // Enqueue owns the payload and may synchronously invoke its callback.
    // Do not dereference it after this call.
    cpu.invoke(0x823A8748,request);
    if(rt->nativeMainMenuExitQuery==request)rt->nativeMainMenuExitQueryQueued=true;
}

bool SimpsonsNativeMainMenuExitQueryCallback(PPCContext& ctx,uint8_t* base) {
    HostState host;auto* rt=Simpsons::active;
    if(!rt||base!=rt->base||!rt->nativeMainMenuExitQuery||
       ctx.r4.u32!=rt->nativeMainMenuExitQuery)return false;
    const bool accepted=rt->nativeMainMenuExitQueryQueued&&ctx.r3.u32==0&&ctx.r5.u8==0;
    const auto request=rt->nativeMainMenuExitQuery;rt->nativeMainMenuExitQuery=0;
    rt->nativeMainMenuExitQueryQueued=false;
    // Match the original free-only callback's string and allocation cleanup.
    // Cancel/back and popup-owner teardown both release the pending token.
    Simpsons::EngineCpuCalls cpu(ctx,base);
    cpu.invoke(0x82390610,request);
    cpu.invoke(0x8269BEB0,request);
    if(accepted&&rt->window) {
        // Cancel guest execution before the window thread tears down its HWND.
        rt->requestStop("Native window closed");
        if(const auto window=rt->window->handle())PostMessageW(window,WM_CLOSE,0,0);
    }
    std::fprintf(stderr,"[NATIVE MAIN MENU] Exit Game confirmation %s\n",accepted?"accepted":"cancelled");
    return true;
}

PPC_FUNC_IMPL(__imp__sub_8239C7B0);
PPC_FUNC(sub_8239C7B0) {
    if(!SimpsonsNativeMainMenuExitQueryCallback(ctx,base))__imp__sub_8239C7B0(ctx,base);
}

void SimpsonsNativeMenuMouse(PPCContext& ctx,uint8_t* base) {
    HostState host;auto* rt=Simpsons::active;
    if(!rt||base!=rt->base||!rt->window||!rt->controllers)return;
    SimpsonsNativeControlMenuTick(ctx,base);
    const auto owner=ctx.r26.u32;
    const auto globalOwner=PPC_LOAD_U32(0x82D08B14);
    if(!owner||owner!=globalOwner){traceState("owner",{owner,globalOwner});return;}
    rt->probe(owner,0x44,false);
    const auto vtable=PPC_LOAD_U32(owner);
    const auto active=PPC_LOAD_U8(owner+8);
    if(vtable!=0x8200264C||!active) {traceState("inactive",{owner,globalOwner,vtable,active});rt->window->setMenuMouse(false);return;}
    Simpsons::EngineCpuCalls cpu(ctx,base);
    const auto domainOut=cpu.registers().r1.u32+0xE0;
    // Native keyboard/mouse is slot zero. The original mapper 823A1970 writes
    // its digital/analog event domains; driver+148 is only a selection mode.
    cpu.invoke(0x823A1970,0,domainOut,domainOut+4);
    const auto controllerDomain=PPC_LOAD_U32(domainOut);
    const auto manager=PPC_LOAD_U32(0x82D08DB0);
    const auto receiver=menuReceiver(*rt,base,manager,controllerDomain);
    if(!receiver){traceState("receiver",{owner,manager,manager?PPC_LOAD_U32(manager+0x44):0,controllerDomain});rt->window->setMenuMouse(false);return;}
    // UIxMovie's constructor 827F4028 embeds this typed event receiver at
    // movie+100, with its owner at receiver+24. Press thunk 827F2008 performs
    // the same owner load before invoking the screen. Other subscribers do
    // not have the UIxMovie target-string layout used by 827F20E0.
    const auto receiverType=PPC_LOAD_U32(receiver),movie=PPC_LOAD_U32(receiver+24);
    if(receiverType!=0x8215BCD8||!movie||uint64_t(movie)+100!=receiver) {
        traceState("movie",{receiver,receiverType,movie});rt->window->setMenuMouse(false);return;
    }
    rt->probe(movie,128,false);
    const auto target=cpu.invoke(0x827F20E0,movie);
    const bool blocked=PPC_LOAD_U32(receiver+20)!=0;
    traceState("selected",{controllerDomain,manager,movie,receiver,PPC_LOAD_U32(receiver+8),PPC_LOAD_U32(receiver+12),uint32_t(blocked),target});
    // Original 827F1BA8 stops at this subscriber while +20 blocks dispatch.
    // Its movie can still be loading, so entering the AVM here would query
    // partially initialized methods. Preserve its cursor without script calls.
    if(blocked){rt->window->setMenuMouse(true);(void)rt->controllers->menuPointer();return;}
    const auto pointer=rt->controllers->menuPointer();
    const auto scalar=[&](uint32_t address) {PPCRegister v{};v.u32=PPC_LOAD_U32(address);return double(v.f32);};
    // 827F4E08 derives drawing scales from these authored extents; 827F4EF8
    // applies them to every Apt display matrix. They are not the movie header.
    const auto stageWidth=scalar(0x82CF7D7C),stageHeight=scalar(0x82CF7D80);
    if(!std::isfinite(stageWidth)||!std::isfinite(stageHeight)||stageWidth<=0||stageHeight<=0||
       stageWidth>65536||stageHeight>65536||!rt->engineDriver) {
        traceState("stage",{PPC_LOAD_U32(0x82CF7D7C),PPC_LOAD_U32(0x82CF7D80),uint32_t(bool(rt->engineDriver))});return;
    }
    const auto point=Simpsons::nativeMenuPoint(pointer.x,pointer.y,pointer.width,pointer.height,
        stageWidth,stageHeight,rt->engineDriver->renderAspect());
    const uint32_t update=!pointer.active||!point.inside?0:
        (pointer.pressed?3:(pointer.moved?1:(pointer.wheel?2:0)));
    const auto hit=scriptQuery(cpu,base,target,point,update,stageWidth/2);
    rt->window->setMenuMouse(hit!=0);
    if(pointer.moved||pointer.pressed||pointer.wheel)traceState("pointer",{hit,uint32_t(pointer.active),uint32_t(pointer.x),uint32_t(pointer.y),uint32_t(pointer.moved),pointer.pressed,uint32_t(pointer.wheel),uint32_t(point.inside)});
    if(!pointer.active||!point.inside)return;
    const auto event=Simpsons::nativeMenuMouseEvent(hit,pointer.pressed,pointer.wheel);
    if(event<0)return;
    cpu.invoke(0x827F1B28,manager,controllerDomain,uint32_t(event),0);
    std::fprintf(stderr,"[NATIVE MENU MOUSE] receiver=%08X pointer=%d,%d stage=%.4f,%.4f hit=%u event=%d\n",
        receiver,pointer.x,pointer.y,point.x,point.y,hit,event);
}

void SimpsonsNativeAptHitTest(PPCContext& ctx,uint8_t* base) {
    HostState host;if(!Simpsons::active)throw Simpsons::Failure("Apt mouse hit test lacks its runtime");
    auto& rt=*Simpsons::active;
    if(base!=rt.base)throw Simpsons::Failure("Apt mouse hit test lacks its runtime");
    Simpsons::EngineCpuCalls cpu(ctx,base);
    const auto none=[&] {ctx.r3.u64=cpu.invoke(0x827C6108,0);};
    // This Xbox leaf originally returned undefined. Implement the Flash point
    // bounds overload used by our menu scripts, preserving all other overloads.
    if(ctx.r4.u32!=3){ctx.r3.u64=PPC_LOAD_U32(0x82E01C7C);return;}
    const auto stack=0x82E02840u,size=PPC_LOAD_U32(stack),capacity=PPC_LOAD_U32(stack+4),items=PPC_LOAD_U32(stack+8);
    const uint64_t top=uint64_t(items)+4ull*size;
    if(size<3||size>capacity||!items||top>UINT32_MAX){none();return;}
    rt.probe(uint32_t(top-12),12,false);
    const auto arg=[&](uint32_t i){return PPC_LOAD_U32(uint32_t(top-4*(i+1)));};
    cpu.invoke(0x827C1BE8,arg(0));const auto x=cpu.registers().f1.f64;
    cpu.invoke(0x827C1BE8,arg(1));const auto y=cpu.registers().f1.f64;
    cpu.invoke(0x827C1BE8,arg(2));const auto shape=cpu.registers().f1.f64;
    if(!std::isfinite(shape)||shape!=0){ctx.r3.u64=PPC_LOAD_U32(0x82E01C7C);return;}
    if(!std::isfinite(x)||!std::isfinite(y)){none();return;}
    const auto receiver=ctx.r3.u32;rt.probe(receiver,0x78,false);
    // Visible flags belong to each ancestor as well as the tested row/footer.
    for(uint32_t item=receiver,depth=0;item;item=PPC_LOAD_U32(item+68)) {
        if(++depth>64){none();return;}rt.probe(item,0x78,false);
        if(!(PPC_LOAD_U32(item+88)&0x800u)){none();return;}
    }
    const auto out=cpu.registers().r1.u32+0x80;
    // Native Apt already computes four global bounds floats through each
    // parent's affine transform. Unlike _width, this retains min/max offsets.
    cpu.invoke(0x827E9720,receiver,out);
    const auto f=[&](uint32_t i){PPCRegister value{};value.u32=PPC_LOAD_U32(out+4*i);return double(value.f32);};
    const std::array bounds{f(0),f(1),f(2),f(3)};
    bool inside=true;for(const auto value:bounds)inside=inside&&std::isfinite(value);
    inside=inside&&bounds[0]<bounds[2]&&bounds[1]<bounds[3]&&
        x>=bounds[0]&&x<=bounds[2]&&y>=bounds[1]&&y<=bounds[3];
    ctx.r3.u64=cpu.invoke(0x827C6108,inside?1u:0u);
}
