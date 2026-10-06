// Native callback ABI around actual retail UIxMovie target access, preference
// bitfield readers/setters, snapshot copy and undefined result. Retail Apt
// population/save boundaries, script destination and safe-string export are
// bounded fixture receivers; asset-byte tests execute appended scripts separately.
#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/native_control_menu.h"
#include "runtime/native_controllers.h"
#include "runtime/native_window.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>

namespace {
using namespace Simpsons;
constexpr uint32_t movie=0x20000,pathCell=0x20100,path=0x20200,profile=0x21000,
    inputOwner=0x22000,saveOwner=0x23000,undefined=0x23100;
size_t checks=0;uint32_t pendingNumber=0,pendingAction=0,selection=0,footer=0;
uint32_t gamepadPublishes=0,savePublishes=0,keyboardPages=0,mousePages=0;
std::array<bool,3> gamepad{};
std::map<std::string,std::string> labels;
void need(bool value,const char* why){++checks;if(!value)throw Failure(why);}
const char* text(uint8_t* base,uint32_t address){return reinterpret_cast<const char*>(PPCGuestPointer(base,address,1,false));}
void script(PPCContext& c,uint8_t* base) {
    need(c.r5.u32==path,"Controls bridge lost original UIxMovie target");
    need(c.r4.u32==0&&c.r6.u32<=1,"Controls bridge changed its destination/argument ABI");
    const std::string name=text(base,c.r3.u32);
    // Pin and exercise the retail bridge's outgoing r7-r10 register spills.
    const std::array<uint64_t,4> args{c.r7.u64,c.r8.u64,c.r9.u64,c.r10.u64};
    for(uint32_t i=0;i<args.size();++i)PPC_STORE_U64(c.r1.u32+48+i*8,args[i]);
    if(c.r6.u32) {
        const auto value=PPC_LOAD_U32(c.r1.u32+52);
        need(value>=c.r1.u32+0x60&&value<c.r1.u32+0xF0,
             "Controls label overlapped original outgoing argument spills");
        const std::string label=text(base,value);
        if(name=="ControlsMenu.setInvertY")gamepad[0]=label=="On";
        else if(name=="ControlsMenu.setInvertX")gamepad[1]=label=="On";
        else if(name=="ControlsMenu.setRumble")gamepad[2]=label=="On";
        else {need(name.starts_with("ControlsMenu.set_ctl"),"Unexpected native Controls label method");labels[name]=label;}
    } else if(name=="ControlsMenu.getNativeAction") {pendingNumber=pendingAction;pendingAction=0;}
    else if(name=="ControlsMenu.getNativeSelection") {pendingNumber=200+selection+1000*footer;footer=0;}
    else if(name=="ControlsMenu.nativeKeyboardPage") {++keyboardPages;selection=0;}
    else if(name=="ControlsMenu.nativeMousePage") {++mousePages;selection=0;}
    else if(name=="ControlsMenu.getInvertY")pendingNumber=gamepad[0];
    else if(name=="ControlsMenu.getInvertX")pendingNumber=gamepad[1];
    else if(name=="ControlsMenu.getRumble")pendingNumber=gamepad[2];
    else if(name=="gizmoMoveLeft"&&selection>=5&&selection<=7)gamepad[selection-5]=true;
    else if(name=="gizmoMoveRight"&&selection>=5&&selection<=7)gamepad[selection-5]=false;
    else if(name=="ControlsMenu.nativeControllerToggle"&&selection>=5&&selection<=7)gamepad[selection-5]=!gamepad[selection-5];
    else throw Failure("Unexpected Controls fixture script method "+name);
    c.r3.u64=undefined;
}
void number(PPCContext& c,uint8_t*){c.r3.u64=pendingNumber;}
void populateDestination(PPCContext& c,uint8_t* base) {
    need(c.r3.u32==movie,"Retail Controls population boundary lost its movie");
    EngineCpuCalls cpu(c,base);
    need(cpu.invoke(0x827F20E0,movie)==path,"Retail Controls population lost its real UIxMovie target");
    need(cpu.invoke(0x823A0EB8,6)==2,"Original unselected-controller slot mapper changed");
    gamepad[0]=cpu.invoke(0x823A0C58,profile,0,2)!=0;
    gamepad[1]=cpu.invoke(0x823A0C58,profile,0,1)!=0;
    gamepad[2]=cpu.invoke(0x823A0C58,profile,0,0)!=0;
}
void saveDestination(PPCContext& c,uint8_t* base) {
    need(c.r3.u32==movie,"Retail Controls save boundary lost its movie");
    EngineCpuCalls cpu(c,base);
    // Execute the real original setters. Their live-player notification checks
    // the original null player collection and naturally takes its empty path.
    cpu.invoke(0x823A11E8,profile,0,1,uint32_t(gamepad[1]));++gamepadPublishes;
    cpu.invoke(0x823A11E8,profile,0,2,uint32_t(gamepad[0]));++gamepadPublishes;
    cpu.invoke(0x823A11E8,profile,0,0,uint32_t(gamepad[2]));++gamepadPublishes;
    ++savePublishes;
}
struct Dispatch {
    uint8_t* base;
    std::array<uint32_t,4> addresses{0x827BF8F8,0x823A4628,0x823A5150,0x823A51E8};
    std::array<PPCFunc*,4> original{};
    explicit Dispatch(uint8_t* memory):base(memory) {
        const std::array<PPCFunc*,4> replacements{script,number,populateDestination,saveDestination};
        for(size_t i=0;i<addresses.size();++i) {
            original[i]=PPC_LOOKUP_FUNC(base,addresses[i]);need(original[i]!=nullptr,"Controls original call dispatch absent");
            PPC_LOOKUP_FUNC(base,addresses[i])=replacements[i];
        }
    }
    ~Dispatch(){for(size_t i=0;i<addresses.size();++i)PPC_LOOKUP_FUNC(base,addresses[i])=original[i];}
};
PPCContext seeded(const PPCContext& entry,uint32_t event=0) {
    auto c=entry;
#define SEED(n) c.r##n.u64=0x7193000000000000ull+n;c.f##n.f64=double(n)+0.25
    SEED(14);SEED(15);SEED(16);SEED(17);SEED(18);SEED(19);SEED(20);SEED(21);SEED(22);
    SEED(23);SEED(24);SEED(25);SEED(26);SEED(27);SEED(28);SEED(29);SEED(30);SEED(31);
#undef SEED
    c.r3.u64=movie;c.r30.u64=event;c.r31.u64=movie;c.lr=0x823A5A00;
    c.v0.u32[0]=0xF17893A5;c.v13.u32[3]=0x517E2604;c.v31.u32[1]=0xC729B351;
    return c;
}
using Hook=void(*)(PPCContext&,uint8_t*);
void invoke(Runtime& rt,const PPCContext& entry,Hook hook,uint32_t event=0,bool consume=false,bool leaf=false) {
    auto c=seeded(entry,event);auto expected=c;if(consume)expected.r30.u64=0;if(leaf)expected.r3.u64=undefined;
    const auto beforeContext=currentContext;const auto beforeFp=PPCFPSCRRegister::getcsr();SetLastError(0x6193);
    std::array<uint8_t,0xC0> frame{};std::memcpy(frame.data(),rt.pointer(c.r1.u32,uint32_t(frame.size()),false),frame.size());
    hook(c,rt.base);
    need(!std::memcmp(&c,&expected,sizeof(c)),"Controls callback changed an unrelated PPC register/FP/vector/context field");
    need(currentContext==beforeContext&&PPCFPSCRRegister::getcsr()==beforeFp&&GetLastError()==0x6193,
         "Controls callback changed host FP/error/current context");
    need(!std::memcmp(frame.data(),rt.pointer(c.r1.u32,uint32_t(frame.size()),false),frame.size()),
         "Controls native-to-original calls overwrote the caller's ABI frame");
}
void exercise(const char* image,const std::filesystem::path& settings) {
    Runtime rt;PPCContext entry{};rt.load(image);rt.initialize(entry);auto* base=rt.base;
    constexpr std::array<std::array<uint32_t,2>,18> words{{
        {0x823A54F0,0x7D8802A6},{0x823A54FC,0x4BFFFC55},{0x823A5500,0x48416AF9},
        {0x823A5518,0x7D8802A6},{0x823A5524,0x4BFFFCC5},{0x823A5528,0x48416AD1},
        {0x823A5A00,0x2F1E0006},{0x823A5A0C,0x4BFFF7DD},{0x823A5A40,0x4BFFBBA1},
        {0x823A51DC,0x4BFFF95D},{0x823A525C,0x4BFFBF8D},
        {0x827F20E0,0x81630060},{0x827BF900,0xF8E10030},{0x827BF90C,0xF9410048},
        {0x827BBFFC,0x806B1C7C},{0x823A0B38,0x38830008},
        {0x823A1600,0x4869B781},{0x823A1608,0x4BFFFE99}}};
    for(const auto word:words)need(PPC_LOAD_U32(word[0])==word[1],"Retail Controls callback/bridge/source identity changed");
    rt.map(movie,0x4000,true,"original Controls callback ABI fixture");
    PPC_STORE_U32(movie+96,pathCell);PPC_STORE_U32(pathCell,path);
    std::memcpy(rt.pointer(path,32,true),"_level0.controls_fixture",25);
    PPC_STORE_U32(0x82D08B14,inputOwner);PPC_STORE_U8(inputOwner+9,6);
    PPC_STORE_U32(0x82D08B9C,0);PPC_STORE_U32(0x82D08BA4,0);PPC_STORE_U32(0x82D08D90,profile);
    PPC_STORE_U32(0x82D08D70,saveOwner);PPC_STORE_U32(0x82E01C7C,undefined);
    PPC_STORE_U32(profile+160,5);PPC_STORE_U32(0x82D08DE0,4);
    struct Background {
        std::string previous;bool existed=false;
        Background(){if(const auto value=std::getenv("SIMPSONS_BACKGROUND_WINDOW")){previous=value;existed=true;}need(!_putenv_s("SIMPSONS_BACKGROUND_WINDOW","1"),"Controls fixture background environment failed");}
        ~Background(){_putenv_s("SIMPSONS_BACKGROUND_WINDOW",existed?previous.c_str():"");}
    } background;
    rt.window=std::make_unique<NativeWindow>();ShowWindow(rt.window->handle(),SW_HIDE);EnableWindow(rt.window->handle(),FALSE);
    rt.window->keyboard->focus(true);rt.controlSettingsPath=settings;
    Dispatch dispatch(base);
    {EngineCpuCalls cpu(entry,base);cpu.invoke(0x823A0B38,profile);}
    invoke(rt,entry,SimpsonsNativeControlsPopulate,0,false,true);
    need(gamepad==std::array{true,false,true},"Original Controls population did not retain profile bitfields");
    need(labels.at("ControlsMenu.set_ctlrow4").find("Space")!=std::string::npos,"Native keyboard binding was not populated");
    selection=5;invoke(rt,entry,SimpsonsNativeControlsLeave,6,true);
    need(rt.window->keyboard->rebindActive(),"Select left the native Controls menu instead of capturing");
    need(labels.at("ControlsMenu.set_ctlrow4").find("Press key")!=std::string::npos,"Native row lacks its capture prompt");
    rt.window->keyboard->key(VK_F6,true);rt.window->keyboard->key(VK_F6,false);
    invoke(rt,entry,SimpsonsNativeControlMenuTick);
    need(rt.window->keyboard->rebindActive()&&labels.at("ControlsMenu.set_ctlrow4").find("Reserved key")!=std::string::npos,
         "Reserved-key rejection left capture or lost its native prompt");
    rt.window->keyboard->key('Z',true);invoke(rt,entry,SimpsonsNativeControlMenuTick);
    need(rt.controlSettings.bindings[uint32_t(ControlAction::Jump)][0]==VK_SPACE,"Capture published its new binding before release");
    rt.window->keyboard->key('Z',false);invoke(rt,entry,SimpsonsNativeControlMenuTick);
    need(rt.controlSettings.bindings[uint32_t(ControlAction::Jump)][0]=='Z'&&!rt.window->keyboard->rebindActive(),"Released binding did not publish to keyboard and camera owner");
    // Cancel restores the menu-entry snapshot and leaves event7 for the
    // original profile rollback/transition branch.
    invoke(rt,entry,SimpsonsNativeControlsLeave,7);
    need(rt.controlSettings==NativeControlSettings{}&&rt.window->keyboard->controls()==NativeControlSettings{},"Controls Cancel did not restore both native preference owners");
    invoke(rt,entry,SimpsonsNativeControlsPopulate,0,false,true);
    selection=0;pendingAction=101;invoke(rt,entry,SimpsonsNativeControlsSave,0,false,true);
    need(keyboardPages==1&&labels.at("ControlsMenu.set_ctlpage").find("2/2")!=std::string::npos,"Second binding page not selected");
    pendingAction=101;invoke(rt,entry,SimpsonsNativeControlsSave,0,false,true);
    need(mousePages==1,"Native mouse/controller page not selected");
    pendingAction=103;invoke(rt,entry,SimpsonsNativeControlsSave,0,false,true);
    need(rt.controlSettings.mouseSensitivity==125&&rt.window->keyboard->controls().mouseSensitivity==125,"Sensitivity edit did not publish live");
    selection=5;pendingAction=111;invoke(rt,entry,SimpsonsNativeControlsSave,0,false,true);
    need((PPC_LOAD_U32(profile+160)&4u)==0&&savePublishes==1&&gamepadPublishes==3,
         "Native third page did not use the actual retail controller save callbacks");
    selection=5;invoke(rt,entry,SimpsonsNativeControlsLeave,6,true);
    need((PPC_LOAD_U32(profile+160)&4u)!=0,"Controller Select did not toggle through the retail bitfield setter");
    selection=8;invoke(rt,entry,SimpsonsNativeControlsLeave,6);
    need(NativeControlSettings::load(settings).mouseSensitivity==125,"Accept row did not save native preferences");
    invoke(rt,entry,SimpsonsNativeControlsPopulate,0,false,true);
    selection=1;footer=1;invoke(rt,entry,SimpsonsNativeControlsLeave,6);
    need(!rt.window->keyboard->rebindActive(),"Authored Accept footer started a binding capture");
    invoke(rt,entry,SimpsonsNativeControlsPopulate,0,false,true);
    {
        EngineCpuCalls cpu(entry,base);cpu.invoke(0x823A0B38,profile);
        cpu.invoke(0x823A11E8,profile,0,1,1);
        need(PPC_LOAD_U32(profile+160)==7,"Original controller bitfield setter failed before Cancel");
    }
    invoke(rt,entry,SimpsonsNativeControlsLeave,7);
    // Execute the actual memcpy call retained at original rollback823A1600.
    // The following audio/live-player publication requires the full game and
    // is outside this fixture; its retained caller/source words are pinned.
    {EngineCpuCalls cpu(entry,base);cpu.invoke(0x82A3CD80,profile+8,profile+168,160);}
    need(PPC_LOAD_U32(profile+160)==5,"Original controller rollback copy did not restore its native-menu entry snapshot");
    std::printf("PASS native Controls callbacks: %zu checks; real retail movie target, bitfields/setters, snapshot/rollback copy, capture/result transactions and full callback ABI\n",checks);
}
}
int main(int argc,char** argv) {
    std::filesystem::path settings;
    try {
        need(argc==2,"Original image required");
        settings=std::filesystem::temp_directory_path()/("simpsons-controls-abi-"+std::to_string(GetCurrentProcessId())+".cfg");
        exercise(argv[1],settings);std::filesystem::remove(settings);return 0;
    } catch(const std::exception& error) {
        if(!settings.empty())std::filesystem::remove(settings);
        std::fprintf(stderr,"FAIL native Controls callbacks: %s\n",error.what());return 1;
    }
}
