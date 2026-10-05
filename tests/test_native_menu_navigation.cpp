#include "runtime/runtime.h"
#include "runtime/native_controllers.h"
#include "runtime/engine_cpu_calls.h"
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>

namespace Simpsons::Platform {
struct NativeControllersTestAccess {
    static void bind(NativeControllers& source,decltype(&XInputGetState) query) {
        source.getState=query;source.disconnectedPollMs=0;
    }
};
}
void SimpsonsNativeMenuKeyboardPressed(PPCContext&,uint8_t*);
void SimpsonsNativeMenuKeyboardHeld(PPCContext&,uint8_t*);
void SimpsonsNativeMenuKeyboardDirection(PPCContext&,uint8_t*);
void SimpsonsNativeMenuKeyboardStick(PPCContext&,uint8_t*);

namespace {
using namespace Simpsons;
constexpr uint32_t owner=0x50000,events=0x51000,input=0x52000,wrapper=0x53000,
                   mode=0x54000,queue=0x55000,leftRecord=0x82CD1088;
struct Direction {uint32_t key,row,binding,event;WORD button;};
constexpr std::array<Direction,4> directions{{
    {'A',0x82CD0FA8,6,2,XINPUT_GAMEPAD_DPAD_LEFT},
    {'D',0x82CD0FC4,7,3,XINPUT_GAMEPAD_DPAD_RIGHT},
    {'W',0x82CD0FE0,8,4,XINPUT_GAMEPAD_DPAD_UP},
    {'S',0x82CD0FFC,9,5,XINPUT_GAMEPAD_DPAD_DOWN}}};
size_t checks=0;uint32_t reads=0;DWORD physicalStatus=ERROR_DEVICE_NOT_CONNECTED;
void need(bool value,const char* why){++checks;if(!value)throw Failure(why);}
DWORD WINAPI query(DWORD slot,XINPUT_STATE* state) noexcept {
    ++reads;*state={};return slot?ERROR_DEVICE_NOT_CONNECTED:physicalStatus;
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
PPCContext context(const PPCContext& entry,uint32_t row) {
    auto c=entry;
#define SEED(n) c.r##n.u64=0x7193000000000000ull+n;c.f##n.f64=double(n)+0.25
    SEED(14);SEED(15);SEED(16);SEED(17);SEED(18);SEED(19);SEED(20);SEED(21);SEED(22);
    SEED(23);SEED(24);SEED(25);SEED(26);SEED(27);SEED(28);SEED(29);SEED(30);SEED(31);
#undef SEED
    c.r3.u64=0;c.r26.u64=owner;c.r27.u64=0;c.r31.u64=row;
    c.v0.u32[0]=0xF17893A5;c.v0.u32[3]=0x517E2604;c.lr=0x823A206C;
    return c;
}
using Hook=void(*)(PPCContext&,uint8_t*);
void checkHook(Runtime& rt,Hook hook,PPCContext c,bool inject=false,bool clear=false,uint8_t* memory=nullptr) {
    auto* base=rt.base;auto expected=c;if(inject)expected.r3.u64=1;
    std::array<uint8_t,0x44> before{};std::memcpy(before.data(),rt.pointer(owner,uint32_t(before.size()),false),before.size());
    auto expectedOwner=before;if(clear)std::memset(expectedOwner.data()+0xC,0,8);
    const auto hostFp=PPCFPSCRRegister::getcsr();const auto priorContext=currentContext;const auto priorReads=reads;
    SetLastError(0x6191);hook(c,memory?memory:base);
    need(std::memcmp(&c,&expected,sizeof(c))==0,"Menu hook changed an unrelated PPC register, FP lane or trace/context field");
    need(PPCFPSCRRegister::getcsr()==hostFp&&GetLastError()==0x6191,"Menu hook changed host FP or last-error state");
    need(currentContext==priorContext&&reads==priorReads,"Menu hook changed current context or sampled input twice");
    need(std::memcmp(expectedOwner.data(),rt.pointer(owner,uint32_t(expectedOwner.size()),false),expectedOwner.size())==0,
         "Menu hook changed owner bytes outside its exact two left-stick outputs");
}
void pinSource(uint8_t* base) {
    // These words pin both the original query calls and their untouched next
    // instructions. Hooks add keyboard input without replacing those calls.
    constexpr std::array<std::array<uint32_t,2>,8> words{{
        {0x823A2068,0x48319F09},{0x823A206C,0x2F030000},
        {0x823A208C,0x48319EED},{0x823A2090,0x2F030000},
        {0x823A21DC,0x48319B1D},{0x823A21E0,0xC01F0000},
        {0x823A2294,0x48319A65},{0x823A2298,0xC01F0000}}};
    for(const auto& word:words)need(PPC_LOAD_U32(word[0])==word[1],"Menu hook source instruction identity changed");
    for(const auto& d:directions)
        need(PPC_LOAD_U32(d.row)==d.binding&&PPC_LOAD_U32(d.row+8)==d.event&&PPC_LOAD_U8(d.row+13)==1,
             "Original D-pad binding/event/repeat table identity changed");
    need(PPC_LOAD_U32(leftRecord)==0&&PPC_LOAD_U32(leftRecord+12)==1,"Original left/right analog source identities changed");
}
void guards(Runtime& rt,const PPCContext& entry,const std::shared_ptr<Platform::NativeKeyboard>& keys) {
    auto* base=rt.base;XINPUT_STATE state{};
    auto neutral=[&]{keys->focus(false);keys->focus(true);rt.controllers->state(0,state);};
    for(const auto& d:directions) {
        neutral();keys->key(d.key,true);rt.controllers->state(0,state);
        const auto navigation=rt.controllers->keyboardNavigation();
        need(navigation.active&&navigation.pressed==d.button&&navigation.held==d.button,"Keyboard direction snapshot differs from the ordinary poll");
        auto c=context(entry,d.row);
        checkHook(rt,SimpsonsNativeMenuKeyboardPressed,c,true);
        checkHook(rt,SimpsonsNativeMenuKeyboardHeld,c,true);
        c.r3.u64=0x718A000000000000ull;
        checkHook(rt,SimpsonsNativeMenuKeyboardPressed,c,true); // Original cmpwi uses the low scalar lane.
        c.r3.u64=0x718A000000000002ull;checkHook(rt,SimpsonsNativeMenuKeyboardPressed,c);
        c=context(entry,d.row);rt.controllers->state(0,state);
        checkHook(rt,SimpsonsNativeMenuKeyboardPressed,c);
        checkHook(rt,SimpsonsNativeMenuKeyboardHeld,c,true);
        for(const auto& other:directions)if(other.key!=d.key) {
            c=context(entry,other.row);checkHook(rt,SimpsonsNativeMenuKeyboardHeld,c);
        }
        for(uint32_t slot:{1u,2u,3u,4u}) {
            c=context(entry,d.row);c.r27.u64=slot;checkHook(rt,SimpsonsNativeMenuKeyboardHeld,c);
        }
        c=context(entry,d.row+4);checkHook(rt,SimpsonsNativeMenuKeyboardHeld,c);
        c=context(entry,0x82CD0F00);checkHook(rt,SimpsonsNativeMenuKeyboardHeld,c);
        const auto binding=PPC_LOAD_U32(d.row);PPC_STORE_U32(d.row,binding+1);
        c=context(entry,d.row);checkHook(rt,SimpsonsNativeMenuKeyboardHeld,c);PPC_STORE_U32(d.row,binding);
        const auto event=PPC_LOAD_U32(d.row+8);PPC_STORE_U32(d.row+8,event+1);
        checkHook(rt,SimpsonsNativeMenuKeyboardHeld,c);PPC_STORE_U32(d.row+8,event);
    }
    neutral();keys->key('A',true);rt.controllers->state(0,state);
    auto c=context(entry,directions[0].row);
    PPC_STORE_U8(owner+8,0);checkHook(rt,SimpsonsNativeMenuKeyboardHeld,c);PPC_STORE_U8(owner+8,1);
    const auto vtable=PPC_LOAD_U32(owner);PPC_STORE_U32(owner,vtable+4);
    checkHook(rt,SimpsonsNativeMenuKeyboardHeld,c);PPC_STORE_U32(owner,vtable);
    PPC_STORE_U32(0x82D08B14,owner+4);checkHook(rt,SimpsonsNativeMenuKeyboardHeld,c);PPC_STORE_U32(0x82D08B14,owner);
    c.r26.u64=0xFFFFFFFF;checkHook(rt,SimpsonsNativeMenuKeyboardHeld,c);
    c=context(entry,directions[0].row);checkHook(rt,SimpsonsNativeMenuKeyboardHeld,c,false,false,base+1);
    auto source=rt.controllers;rt.controllers.reset();checkHook(rt,SimpsonsNativeMenuKeyboardHeld,c);rt.controllers=source;
    physicalStatus=ERROR_SUCCESS;source->state(0,state);checkHook(rt,SimpsonsNativeMenuKeyboardHeld,c);
    physicalStatus=ERROR_DEVICE_NOT_CONNECTED;neutral();

    auto vector=[&]{
        PPC_STORE_U32(owner+12,0x3F800000);PPC_STORE_U32(owner+16,0xBF800000);
        PPC_STORE_U32(owner+28,0x40200000);PPC_STORE_U32(owner+32,0xC0300000);
    };
    c=context(entry,owner+16);c.r23.u64=leftRecord;c.r30.u64=owner+12;vector();
    checkHook(rt,SimpsonsNativeMenuKeyboardDirection,c,false,true);
    vector();c.r23.u64=leftRecord+12;checkHook(rt,SimpsonsNativeMenuKeyboardDirection,c);
    c.r23.u64=leftRecord;c.r30.u64=owner+28;checkHook(rt,SimpsonsNativeMenuKeyboardDirection,c);
    c.r30.u64=owner+12;c.r27.u64=1;checkHook(rt,SimpsonsNativeMenuKeyboardDirection,c);
    c=context(entry,owner+12);c.r30.u64=leftRecord;c.r29.u64=owner+16;vector();
    checkHook(rt,SimpsonsNativeMenuKeyboardStick,c,false,true);
    vector();c.r30.u64=leftRecord+12;c.r31.u64=owner+28;c.r29.u64=owner+32;
    checkHook(rt,SimpsonsNativeMenuKeyboardStick,c);
    c=context(entry,owner+12);c.r30.u64=leftRecord;c.r29.u64=owner+16;
    PPC_STORE_U32(leftRecord,1);checkHook(rt,SimpsonsNativeMenuKeyboardStick,c);PPC_STORE_U32(leftRecord,0);
    const auto token=source->beginModal(0);checkHook(rt,SimpsonsNativeMenuKeyboardStick,c);source->endModal(token);
    neutral();source->beginMovie(owner);checkHook(rt,SimpsonsNativeMenuKeyboardStick,c);source->endMovie();neutral();
    vector();checkHook(rt,SimpsonsNativeMenuKeyboardStick,c,false,true);
}
void cadence(Runtime& rt,const PPCContext& entry,const std::shared_ptr<Platform::NativeKeyboard>& keys) {
    auto* base=rt.base;EngineCpuCalls cpu(entry,base);const auto saved=abi(cpu.registers());
    // Original event queue layout used by 827F1B28, without an Apt/graphics
    // implementation or callback substitution. Every accepted event is the
    // original dispatcher calling its original ring-buffer publisher.
    PPC_STORE_U32(events+72,queue);PPC_STORE_U32(events+76,128);
    PPC_STORE_U32(0x82D08BA8,mode);PPC_STORE_U32(0x82D08B9C,mode);
    PPC_STORE_U32(0x82D08DA8,input);PPC_STORE_U32(0x82D08DAC,wrapper);PPC_STORE_U32(0x82D08DB0,events);
    PPC_STORE_U8(input+94,1);PPC_STORE_U8(0x82D090F9,0);
    PPC_STORE_U8(owner+9,0);PPC_STORE_U8(owner+44,1);
    cpu.invoke(0x823A19F8,owner,0); // Disable all digital actions through their original setter.
    for(const auto& d:directions)cpu.invoke(0x823A1A20,owner,PPC_LOAD_U32(d.row+4),1);
    cpu.invoke(0x823A1A68,owner,1); // Enable original left/right analog records as well.
    auto step=[&](uint32_t now) {
        XINPUT_STATE state{};need(rt.controllers->state(0,state)==ERROR_SUCCESS,"Ordinary keyboard poll disconnected");
        const auto& pad=state.Gamepad;
        // Match the actual original normalized source endpoints; gameplay's
        // returned WASD sticks remain held while this consumer uses D-pad.
        const auto axis=[](SHORT value){return value<0?-1.0f:value>0?1.0f:0.0f;};
        PPC_STORE_U32(input+40,std::bit_cast<uint32_t>(axis(pad.sThumbLX)));
        PPC_STORE_U32(input+44,std::bit_cast<uint32_t>(axis(pad.sThumbLY)));
        PPC_STORE_U32(0x82D572B0,now);const auto before=PPC_LOAD_U32(events+84);
        const auto hostFp=PPCFPSCRRegister::getcsr();SetLastError(0x6192);
        cpu.invoke(0x823A1EB0,owner);
        need(abi(cpu.registers())==saved,"Whole original menu dispatcher changed nonvolatile GPR/FPR/stack/LR ABI");
        need(PPCFPSCRRegister::getcsr()==hostFp&&GetLastError()==0x6192,"Whole original menu dispatcher changed host FP or last-error state");
        need(!PPC_LOAD_U32(owner+12)&&!PPC_LOAD_U32(owner+16)&&!PPC_LOAD_U32(owner+48)&&!PPC_LOAD_U32(owner+52),
             "Held WASD reached the original per-frame analog menu accumulator");
        return PPC_LOAD_U32(events+84)-before;
    };
    auto event=[&](uint32_t expected) {
        const auto count=PPC_LOAD_U32(events+84);need(count!=0,"Expected original menu event was absent");
        const auto last=queue+12*(count-1);
        need(PPC_LOAD_U32(last)==0&&PPC_LOAD_U32(last+4)==expected&&!PPC_LOAD_U32(last+8),
             "Original menu queue changed slot, direction event or payload");
    };
    for(const auto& d:directions) {
        keys->focus(false);keys->focus(true);need(step(900)==0,"Released keyboard emitted a menu direction");
        keys->key(d.key,true);need(step(1000)==1,"Fresh WASD did not emit exactly one original digital menu event");event(d.event);
        need(step(1000)==0,"Polling at the same clock emitted a repeated menu direction");
        for(uint32_t now:{1001u,1016u,1100u,1200u,1399u,1400u})
            need(step(now)==0,"Held WASD repeated inside the original inclusive 400 ms delay");
        need(step(1401)==1,"Original initial repeat did not begin strictly after 400 ms");event(d.event);
        need(step(1481)==0&&step(1482)==1,"Original 80 ms repeat boundary changed");event(d.event);
        need(step(3401)==1,"Held menu direction did not retain its repeat state");event(d.event);
        need(step(3441)==0&&step(3442)==1,"Original accelerated 40 ms repeat boundary after two seconds changed");event(d.event);
        keys->key(d.key,false);need(step(3500)==0,"Released WASD retained an original held direction");
        keys->key(d.key,true);keys->key(d.key,false);
        need(step(3501)==1,"Short WASD tap between game polls lost its original menu event");event(d.event);
        need(step(3502)==0,"Short WASD tap was repeated after release");
    }
    keys->key('W',true);keys->key('D',true);
    // The original dispatcher publishes the first eligible digital row and
    // then enters its analog route. Right precedes Up in the original table;
    // retain that ordering while the keyboard snapshot retains both axes.
    need(step(4000)==1,"Diagonal WASD changed the original one-event digital dispatch policy");event(3);
    need(rt.controllers->keyboardNavigation().held==WORD(XINPUT_GAMEPAD_DPAD_RIGHT|XINPUT_GAMEPAD_DPAD_UP),
         "Diagonal WASD lost a direction in the ordinary keyboard snapshot");
    keys->key('S',true);need(step(4001)==0,"Opposing WASD keys invented a vertical menu direction");
    keys->focus(false);keys->focus(true);need(step(5000)==0,"Focus loss retained menu input");
    // The ordinary slot gate still excludes a keyboard direction when this
    // dispatcher is deliberately assigned another original controller slot.
    keys->key('A',true);PPC_STORE_U8(owner+9,1);need(step(6000)==0,"Keyboard menu direction escaped slot zero");
    PPC_STORE_U8(owner+9,0);keys->focus(false);keys->focus(true);step(6001);
    // A disabled original binding is still disabled even with fresh native input.
    cpu.invoke(0x823A1A20,owner,PPC_LOAD_U32(directions[0].row+4),0);
    keys->key('A',true);need(step(6002)==0,"Keyboard hook bypassed an original disabled menu binding");
    keys->focus(false);keys->focus(true);step(6003);
    need(!rt.window&&!rt.engineDriver&&!rt.engineAudioOutput&&rt.threads.empty(),"CPU menu fixture acquired GUI/GPU/audio/worker lifetime");
}
void exercise(const char* image) {
    Runtime rt;rt.load(image);PPCContext entry{};rt.initialize(entry);auto* base=rt.base;
    pinSource(base);rt.map(owner,0x6000,true,"original CPU menu navigation fixture");
    rt.controllers=std::make_shared<Platform::NativeControllers>();
    Platform::NativeControllersTestAccess::bind(*rt.controllers,query);
    auto keys=std::make_shared<Platform::NativeKeyboard>();keys->focus(true);rt.controllers->attachKeyboard(keys);
    {
        EngineCpuCalls cpu(entry,base);const auto saved=abi(cpu.registers());
        need(cpu.invoke(0x823A1B88,owner)==owner&&abi(cpu.registers())==saved,"Original menu bridge constructor failed or changed nonvolatile ABI");
        need(PPC_LOAD_U32(owner)==0x8200264C&&PPC_LOAD_U32(0x82D08B14)==owner&&!PPC_LOAD_U8(owner+8),
             "Original menu bridge constructor identity/publication changed");
        cpu.invoke(0x823A1C40,owner);need(PPC_LOAD_U8(owner+8)==1&&abi(cpu.registers())==saved,"Original menu bridge activation changed");
    }
    guards(rt,entry,keys);cadence(rt,entry,keys);
    rt.controllers->attachKeyboard({});rt.unmap(owner);
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==2,"Original image required");exercise(argv[1]);
        std::printf("PASS original menu navigation:%zu checks; original constructor/activation/dispatcher/event queue, WASD fresh/held/short/diagonal/release, exact 400/80/40 ms boundaries, source/table/slot gates, left-only analog suppression, right-stick exclusion and full hook/host/nonvolatile ABI; no live gameplay claim\n",checks);
        return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL original menu navigation:%zu checks:%s\n",checks,e.what());return 1;}
}
