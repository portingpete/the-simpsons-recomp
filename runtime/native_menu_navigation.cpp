#include "runtime.h"
#include "native_controllers.h"

namespace {
struct HostState {
    const uint32_t floatingPoint=PPCFPSCRRegister::getcsr();
    const DWORD error=GetLastError();
    ~HostState(){PPCFPSCRRegister::restoreHostCSR(floatingPoint);SetLastError(error);}
};

// These hooks run only inside the original Apt input dispatcher (823A1EB0).
// Its digital route owns fresh presses and its existing 400/80/40 ms repeat
// policy. The left-stick route instead accumulates one full WASD step every
// frame, so keyboard directions use the digital route for this consumer only.
Simpsons::Platform::NativeKeyboardNavigation navigation(PPCContext& ctx,uint8_t* base) {
    auto* rt=Simpsons::active;
    if(!rt||base!=rt->base||!rt->controllers||ctx.r27.u32!=0)return {};
    const auto value=rt->controllers->keyboardNavigation();
    if(!value.active)return {};
    const auto owner=ctx.r26.u32;
    if(owner<0x10000||owner>UINT32_MAX-0x44||
       owner!=PPC_LOAD_U32(0x82D08B14))return {};
    rt->probe(owner,0x44,false);
    if(PPC_LOAD_U32(owner)!=0x8200264C||!PPC_LOAD_U8(owner+8))return {};
    return value;
}

WORD direction(PPCContext& ctx,uint8_t* base) {
    // These are the four original D-pad bindings, not arbitrary Apt events.
    const auto row=ctx.r31.u32;
    uint32_t binding=0,event=0;WORD button=0;
    switch(row) {
    case 0x82CD0FA8:binding=6;event=2;button=XINPUT_GAMEPAD_DPAD_LEFT;break;
    case 0x82CD0FC4:binding=7;event=3;button=XINPUT_GAMEPAD_DPAD_RIGHT;break;
    case 0x82CD0FE0:binding=8;event=4;button=XINPUT_GAMEPAD_DPAD_UP;break;
    case 0x82CD0FFC:binding=9;event=5;button=XINPUT_GAMEPAD_DPAD_DOWN;break;
    default:return 0;
    }
    return PPC_LOAD_U32(row)==binding&&PPC_LOAD_U32(row+8)==event?button:0;
}

bool leftStickRecord(uint32_t row,uint8_t* base) {
    return row==0x82CD1088&&PPC_LOAD_U32(row)==0;
}

void clearLeftVector(PPCContext& ctx,uint8_t* base) {
    // Exactly two original float outputs: X at +0xC and Y at +0x10.
    // Leave the dispatcher accumulators and the right-stick outputs intact.
    const auto out=ctx.r26.u32+0xC;
    Simpsons::active->probe(out,8,true);
    PPC_STORE_U32(out,0);PPC_STORE_U32(out+4,0);
}
}

void SimpsonsNativeMenuKeyboardPressed(PPCContext& ctx,uint8_t* base) {
    HostState host;
    const auto value=navigation(ctx,base);
    if(value.active&&!ctx.r3.u32&&(value.pressed&direction(ctx,base)))ctx.r3.u64=1;
}

void SimpsonsNativeMenuKeyboardHeld(PPCContext& ctx,uint8_t* base) {
    HostState host;
    const auto value=navigation(ctx,base);
    if(value.active&&!ctx.r3.u32&&(value.held&direction(ctx,base)))ctx.r3.u64=1;
}

void SimpsonsNativeMenuKeyboardDirection(PPCContext& ctx,uint8_t* base) {
    HostState host;
    if(!navigation(ctx,base).active||!leftStickRecord(ctx.r23.u32,base))return;
    const auto owner=ctx.r26.u32;
    if(ctx.r30.u32==owner+0xC&&ctx.r31.u32==owner+0x10)clearLeftVector(ctx,base);
}

void SimpsonsNativeMenuKeyboardStick(PPCContext& ctx,uint8_t* base) {
    HostState host;
    if(!navigation(ctx,base).active||!leftStickRecord(ctx.r30.u32,base))return;
    const auto owner=ctx.r26.u32;
    if(ctx.r31.u32==owner+0xC&&ctx.r29.u32==owner+0x10)clearLeftVector(ctx,base);
}
