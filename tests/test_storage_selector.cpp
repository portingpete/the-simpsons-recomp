#include "runtime/runtime.h"
#include "runtime/native_window.h"
#include "runtime/native_controllers.h"
#include "runtime/native_local_players.h"
#include "runtime/native_notifications.h"
#include "runtime/engine_cpu_calls.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>

PPC_FUNC(__imp__XamShowDeviceSelectorUI);
namespace Simpsons::Platform {
struct NativeControllersTestAccess {
    static DWORD WINAPI disconnected(DWORD,XINPUT_STATE*) noexcept {return ERROR_DEVICE_NOT_CONNECTED;}
    static void bind(NativeControllers& source){source.getState=disconnected;source.disconnectedPollMs=0;}
};
}
namespace {
using namespace Simpsons;namespace fs=std::filesystem;
std::atomic<size_t> checks{};
void need(bool ok,const char* why){++checks;if(!ok)throw Failure(why);}
struct Temp {
    fs::path parent=fs::canonical(fs::temp_directory_path());
    fs::path root=parent/("SimpsonsStorageUI-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    Temp(){need(fs::create_directory(root),"UI fixture directory already exists");}
    ~Temp(){if(root.is_absolute()&&root.parent_path()==parent&&root.filename().wstring().starts_with(L"SimpsonsStorageUI-")){
        std::error_code error;fs::remove_all(root,error);}}
};
constexpr uint32_t owner=0x12000,device=0x13000,ov=0x13100;
struct Fixture {
    Temp temp;Runtime rt;PPCContext entry{};Platform::LocalProfile profile;
    std::shared_ptr<Platform::NotificationListener> observed;
    fs::path commands=temp.root/L"input.commands";
    fs::path audit=temp.root/L"resources.jsonl";
    uint32_t savedDriver=0;
    size_t handlesBeforeOwner=0;
    std::array<uint32_t,2> ownerListeners{};
    bool retired=false;
    HANDLE writer=INVALID_HANDLE_VALUE;
    explicit Fixture(const char* image){
        rt.load(image);rt.initialize(entry);rt.map(0x10000,0x10000,true,"storage UI fixture");rt.map(0x40000,0x1000,false,"storage UI readonly output");
        rt.gameRoot=temp.root;rt.contentRoot=temp.root/L"content";fs::create_directory(rt.contentRoot);
        rt.configureLocalPlayers(temp.root/L"profiles");auto players=rt.localPlayerSource();profile=players->create("Player");players->activate(0,profile.id);
        writer=CreateFileW(commands.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        need(writer!=INVALID_HANDLE_VALUE,"UI input fixture creation failed");
        rt.window=std::make_unique<NativeWindow>();rt.controllers=std::make_shared<Platform::NativeControllers>();
        Platform::NativeControllersTestAccess::bind(*rt.controllers);rt.controllers->attachKeyboard(rt.window->keyboard);
        rt.controllers->attachCommands(std::make_shared<Platform::NativeCommandInput>(commands));
        rt.resourceAudit.configure(audit);rt.resourceAudit.mission("storage-selector-fixture");
        rt.resourceAudit.action("original construct/request/modal/poll/release");
        auto* base=rt.base;
        for(const auto [address,word]:std::array<std::array<uint32_t,2>,8>{{
            {0x8285CF10,0x39400007},{0x8285CF28,0x38FF031C},{0x8285CF2C,0x897F0004},{0x8285CF30,0x7BC664E4},
            {0x8285CF44,0x38A00200},{0x8285CF54,0x60A50100},{0x8285CF60,0x4BBD5EE1},{0x8285CDC0,0x57CB07FE}}})
            need(PPC_LOAD_U32(address)==word,"Original storage producer/destructor instruction changed");
        handlesBeforeOwner=rt.handles.size();savedDriver=PPC_LOAD_U32(0x82E31728);
        need(!savedDriver,"Original fixture driver slot is already owned");
        EngineCpuCalls cpu(entry,rt.base);need(cpu.invoke(0x8285CC78,owner)==owner,"Original save-owner constructor failed");
        // Bind the explicitly constructed test owner to its original provider
        // slot. The retail public setters themselves publish user and flag.
        PPC_STORE_U32(0x82E31728,owner);
        cpu.invoke(0x82CA3A68,0,0);cpu.invoke(0x82CA3A78,0,1);
        need(rt.handles.size()==handlesBeforeOwner+2,"Original constructor did not own exactly two notification handles");
        ownerListeners={PPC_LOAD_U32(owner+0x320),PPC_LOAD_U32(owner+0x324)};
        for(const auto id:ownerListeners){const auto listener=rt.getHandle(id);
            need(listener&&listener->type==KernelHandle::Type::Notification,"Original owner listener is not an actual notification handle");}
        observed=rt.notificationSource()->create(1,2);
    }
    ~Fixture(){if(writer!=INVALID_HANDLE_VALUE)CloseHandle(writer);}
    void retire(){
        need(!retired,"Original storage owner retired twice");auto* base=rt.base;
        const auto handlesBeforeRetirement=rt.handles.size();
        EngineCpuCalls cpu(entry,base);need(cpu.invoke(0x8285CDA0,owner,0)==owner,"Original storage destructor returned a foreign owner");
        need(PPC_LOAD_U32(owner+0x320)==UINT32_MAX&&PPC_LOAD_U32(owner+0x324)==UINT32_MAX&&
            !rt.getHandle(ownerListeners[0])&&!rt.getHandle(ownerListeners[1])&&
            rt.handles.size()+2==handlesBeforeRetirement,"Original storage destructor did not close both real notification handles");
        // The first real selector completion publishes the caller's thread
        // handle in OVERLAPPED+8. That handle belongs to Runtime, not this
        // driver's two notification listeners, and must survive its dtor.
        need(rt.handles.size()==handlesBeforeOwner+1,"Original retirement retained an unexplained handle");
        for(const auto& [id,handle]:rt.handles)
            need(handle==rt.mainThreadHandle,"Original retirement released its completion thread or retained another owner");
        need(!FindWindowExW(rt.window->handle(),nullptr,L"SimpsonsNativeStorageSelector",nullptr)&&IsWindowEnabled(rt.window->handle()),"Original retirement retained modal/window ownership");
        PPC_STORE_U32(0x82E31728,savedDriver);retired=true;
    }
    std::string auditText(){std::ifstream input(audit);return {std::istreambuf_iterator<char>(input),{}};}
    void append(const char* command){DWORD written{};const auto size=DWORD(std::strlen(command));
        need(WriteFile(writer,command,size,&written,nullptr)&&written==size&&FlushFileBuffers(writer),"UI command append failed");}
    PPCContext prepare(uint64_t requested=0){
        auto* base=rt.base;std::memset(rt.pointer(ov-8,0x2C,true),0xA7,0x2C);std::memset(rt.pointer(ov,0x1C,true),0,0x1C);
        PPC_STORE_U32(ov+0x14,0xABCDEF01);PPC_STORE_U32(device,0xDEADBEEF);
        PPCContext c{};std::memset(&c,0xA5,sizeof(c));c.r13.u32=entry.r13.u32;c.r3.u64=0;c.r4.u64=1;c.r5.u64=0x300;c.r6.u64=requested;c.r7.u64=device;c.r8.u64=ov;return c;
    }
};
template<class Invoke>void drive(Fixture& f,Invoke&& invoke,const char* command,bool enabled,const fs::path& screenshot={},bool stop=false,bool heldKeyboard=false){
    if(!screenshot.empty())need(SetEnvironmentVariableW(L"SIMPSONS_STORAGE_CAPTURE",screenshot.c_str())!=FALSE,"UI capture request failed");
    std::exception_ptr error;std::jthread user([&]{HWND window=nullptr;try{
        // Query and click in the same physical-pixel coordinates as the game
        // and selector threads, including when Windows scales this monitor.
        struct DpiContext {
            DPI_AWARENESS_CONTEXT previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            ~DpiContext(){if(previous)SetThreadDpiAwarenessContext(previous);}
        } dpi;
        need(dpi.previous!=nullptr,"UI fixture DPI context failed");
        const auto deadline=GetTickCount64()+5000;bool opened=false;
        while(GetTickCount64()<deadline){if(auto n=f.observed->read(9)){need(n->parameter==1,"Native UI did not publish an actual opening");opened=true;break;}Sleep(5);}
        need(opened,"Native UI did not open within bound");
        window=FindWindowExW(f.rt.window->handle(),nullptr,L"SimpsonsNativeStorageSelector",L"Save Storage");DWORD process{};
        need(window&&GetWindowThreadProcessId(window,&process)!=0&&process==GetCurrentProcessId()&&IsWindowVisible(window),"Actual owned native selector is not visible");
        need(GetParent(window)==f.rt.window->handle()&&IsWindowEnabled(f.rt.window->handle()),"Storage screen is not inside its enabled game owner");
        const auto style=GetWindowLongPtrW(window,GWL_STYLE);
        need((style&WS_CHILD)&&!(style&WS_CAPTION)&&!GetDlgItem(window,IDOK),"Storage screen retained decorated dialog controls");
        RECT client{},parent{},bounds{};need(GetClientRect(window,&client)&&GetClientRect(f.rt.window->handle(),&parent)&&GetWindowRect(window,&bounds),"UI extent query failed");
        MapWindowPoints(nullptr,f.rt.window->handle(),reinterpret_cast<POINT*>(&bounds),2);
        if(!EqualRect(&client,&parent)||!EqualRect(&bounds,&parent))std::fprintf(stderr,
            "[STORAGE EXTENT] child=%ld,%ld,%ld,%ld parent=%ld,%ld,%ld,%ld mapped=%ld,%ld,%ld,%ld\n",
            client.left,client.top,client.right,client.bottom,parent.left,parent.top,parent.right,parent.bottom,
            bounds.left,bounds.top,bounds.right,bounds.bottom);
        need(EqualRect(&client,&parent)&&EqualRect(&bounds,&parent),"Storage screen does not cover the game client");
        if(!screenshot.empty())need(fs::file_size(screenshot)==sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)+size_t(client.right)*size_t(client.bottom)*4,"GPU screen capture is absent or truncated");
        if(stop){f.rt.requestStop("Storage UI cancellation fixture");return;}
        if(heldKeyboard){
            SendMessageW(f.rt.window->handle(),WM_KEYDOWN,VK_SPACE,LPARAM(1)<<30);
            Sleep(80);need(IsWindowVisible(window)!=FALSE,"Held initiating Space auto-repeat accepted storage before release");
            f.rt.window->keyboard->key(VK_SPACE,false);Sleep(40);
        }
        if(!enabled){SendMessageW(window,WM_COMMAND,IDOK,0);need(IsWindowVisible(window)!=FALSE,"Insufficient-capacity folder was selected");}
        XINPUT_STATE state{};for(int i=0;i<10;++i)need(f.rt.controllers->state(0,state)==0&&!state.Gamepad.wButtons,"Game input escaped the modal selector");
        if(!std::strcmp(command,"mouse-accept")||!std::strcmp(command,"mouse-cancel")){
            const int x=client.right/2,y=MulDiv(!std::strcmp(command,"mouse-accept")?480:537,client.bottom,720);
            SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));return;
        }
        if(!std::strcmp(command,"keyboard-row-cancel")){
            SendMessageW(f.rt.window->handle(),WM_SETFOCUS,0,0);
            SendMessageW(f.rt.window->handle(),WM_KEYDOWN,'S',0);Sleep(80);
            SendMessageW(f.rt.window->handle(),WM_KEYUP,'S',0);Sleep(40);
            SendMessageW(f.rt.window->handle(),WM_KEYDOWN,VK_RETURN,0);Sleep(80);
            SendMessageW(f.rt.window->handle(),WM_KEYUP,VK_RETURN,0);
            f.rt.window->keyboard->focus(false);return;
        }
        f.append(command);
    }catch(...){error=std::current_exception();if(window)PostMessageW(window,WM_CLOSE,0,0);f.rt.requestStop("Native storage UI fixture failed");}});
    std::exception_ptr invocation;try{invoke();}catch(...){invocation=std::current_exception();}
    user.join();if(!screenshot.empty())SetEnvironmentVariableW(L"SIMPSONS_STORAGE_CAPTURE",nullptr);
    if(error)std::rethrow_exception(error);if(invocation)std::rethrow_exception(invocation);
    need(FindWindowExW(f.rt.window->handle(),nullptr,L"SimpsonsNativeStorageSelector",nullptr)==nullptr&&IsWindowEnabled(f.rt.window->handle()),"Native selector leaked its child window or changed its owner's enablement");
    const auto closed=f.observed->read(9);need(closed&&closed->parameter==0&&!f.observed->read(9),"Native UI did not publish exactly one paired closing");
}
void original(Fixture& f,const fs::path& screenshot,uint32_t flagPair=3){
    auto* base=f.rt.base;EngineCpuCalls cpu(f.entry,base);const auto sp=cpu.registers().r1.u64;
    need(flagPair<4,"Original flag pair is outside its two boolean inputs");
    cpu.invoke(0x82CA3A68,0,0);cpu.invoke(0x82CA3A78,0,(flagPair>>1)&1);
    need(!PPC_LOAD_U32(owner+8)&&PPC_LOAD_U8(owner+4)==((flagPair>>1)&1),"Original storage setters changed user/flag semantics");
    // This original argument is64-bit before its <<12 conversion. The generic
    // invoke overload initializes only the low32 bits of an argument register.
    cpu.registers().r4.u64=0;cpu.registers().r5.u64=flagPair&1;
    const auto selectedBeforeRequest=PPC_LOAD_U32(owner+0x360);
    drive(f,[&]{need(cpu.invoke(0x8285CED8,owner)==0,"Original storage request did not receive997");},"A\n",true,screenshot);
    need(cpu.registers().r1.u64==sp&&PPC_LOAD_U32(owner+0x31C)==1&&PPC_LOAD_U32(owner+0x360)==selectedBeforeRequest,"Native selector overwrote original selected-device state");
    need(!PPC_LOAD_U32(owner+0x33C)&&!PPC_LOAD_U32(owner+0x354)&&f.rt.getHandle(PPC_LOAD_U32(owner+0x344))==f.rt.mainThreadHandle,"Original selector completion differs");
    need(cpu.invoke(0x8285C868,owner)==15&&PPC_LOAD_U8(owner+0x328)==1,"Original poll did not wait for the UI-close notification");
    need(cpu.invoke(0x8285C868,owner)==0&&!PPC_LOAD_U8(owner+0x328)&&PPC_LOAD_U32(owner+0x360)==1,"Original poll did not publish the chosen native device");
    const auto receipt=f.auditText();char flags[32];std::snprintf(flags,sizeof(flags),"flags=0x%08X",flagPair<<8);
    need(receipt.find("\"kind\":\"storage-selector\"")!=std::string::npos&&
        receipt.find("\"caller\":"+std::to_string(0x8285CF64u))!=std::string::npos&&
        receipt.find(flags)!=std::string::npos&&receipt.find("requested=0")!=std::string::npos&&
        receipt.find("profile=active")!=std::string::npos&&receipt.find("event=none callback=none")!=std::string::npos&&
        receipt.find("profileId="+f.profile.id)!=std::string::npos,
        "Original selector prevalidation receipt omitted producer/ownership context");
    std::printf("[ORIGINAL STORAGE FLAGS] flags=%03X constructor/setters/request/modal/open+close polls passed; device=1 completion=0 caller=8285CF64\n",flagPair<<8);
}
void saveRootResolution(Fixture& f){
    const auto originalGameRoot=f.rt.gameRoot,explicitRoot=f.rt.contentRoot;
    const auto launchRoot=f.temp.root/L"private-game-root";
    const auto defaultRoot=launchRoot/L"saves";
    f.rt.gameRoot=launchRoot/L"Simpsons Game, The (USA)";
    need(fs::create_directories(f.rt.gameRoot)&&fs::create_directory(defaultRoot),"Default save destination fixture creation failed");
    for(bool explicitOverride:{false,true}){
        f.rt.contentRoot=explicitOverride?explicitRoot:fs::path{};
        f.rt.resourceAudit.action(explicitOverride?"explicit private content override":"default sibling save directory");
        const auto before=f.auditText();auto c=f.prepare();
        drive(f,[&]{__imp__XamShowDeviceSelectorUI(c,f.rt.base);},"A\n",true);
        auto* base=f.rt.base;
        need(c.r3.u64==ERROR_IO_PENDING&&PPC_LOAD_U32(device)==1&&!PPC_LOAD_U32(ov)&&!PPC_LOAD_U32(ov+0x18),
            "Resolved native save folder did not complete the accepted choice");
        const auto after=f.auditText();need(after.starts_with(before),"Save destination audit lost earlier receipts");
        const auto segment=after.substr(before.size());
        const auto& expected=explicitOverride?explicitRoot:defaultRoot;
        const auto& excluded=explicitOverride?defaultRoot:explicitRoot;
        need(segment.find("\"asset\":\""+expected.generic_string()+"\"")!=std::string::npos&&
            segment.find("\"asset\":\""+excluded.generic_string()+"\"")==std::string::npos,
            "Storage selector audited a different folder than its default or explicit private override");
        need(fs::is_empty(defaultRoot)&&fs::is_empty(explicitRoot)&&!fs::exists(launchRoot/L"userdata"),
            "Save destination selection created save data or used the old default directory");
    }
    f.rt.gameRoot=originalGameRoot;f.rt.contentRoot=explicitRoot;
    f.rt.resourceAudit.action("original construct/request/modal/poll/release");
    std::printf("Storage destination: accepted sibling saves default and explicit private override with exact folder audit; no save data written\n");
}
void contracts(Fixture& f){
    saveRootResolution(f);
    auto* base=f.rt.base;const auto saved=PPCFPSCRRegister::getcsr();
    f.rt.window->keyboard->focus(true);f.rt.window->keyboard->key(VK_SPACE,true);
    auto held=f.prepare();drive(f,[&]{__imp__XamShowDeviceSelectorUI(held,base);},"A\n",true,{},false,true);
    need(PPC_LOAD_U32(device)==1&&!PPC_LOAD_U32(ov),"Fresh selection after releasing initiating key failed");
    f.rt.window->keyboard->focus(false);
    for(const auto command:{"mouse-accept","mouse-cancel","keyboard-row-cancel"}){
        auto c=f.prepare();drive(f,[&]{__imp__XamShowDeviceSelectorUI(c,base);},command,true);
        const bool accepted=!std::strcmp(command,"mouse-accept");
        need(PPC_LOAD_U32(device)==unsigned(accepted)&&PPC_LOAD_U32(ov)==(accepted?0:ERROR_CANCELLED),"In-game pointer/keyboard row choice returned the wrong completion");
    }
    for(uint32_t fp:{0x1F80u,0x3FC0u,0x5F80u,0x9FC0u,0xE07Fu}){
        auto c=f.prepare();PPCContext expected;std::memcpy(&expected,&c,sizeof(c));expected.r3.u64=997;
        drive(f,[&]{PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(0x12345678);__imp__XamShowDeviceSelectorUI(c,base);
            const auto actualFP=PPCFPSCRRegister::getcsr();const auto error=GetLastError();PPCFPSCRRegister::restoreHostCSR(saved);
            need(actualFP==fp&&error==0x12345678&&!std::memcmp(&c,&expected,sizeof(c)),"Native selector changed CPU/host state");},"B\n",true);
        need(!PPC_LOAD_U32(device)&&PPC_LOAD_U32(ov)==ERROR_CANCELLED&&!PPC_LOAD_U32(ov+4)&&PPC_LOAD_U32(ov+0x18)==ERROR_CANCELLED&&
            PPC_LOAD_U32(ov+0x14)==0xABCDEF01&&PPC_LOAD_U64(ov-8)==0xA7A7A7A7A7A7A7A7ull&&PPC_LOAD_U64(ov+0x1C)==0xA7A7A7A7A7A7A7A7ull,
            "Cancelled native selector completion or bounds differ");
    }
    auto c=f.prepare(UINT64_MAX);drive(f,[&]{__imp__XamShowDeviceSelectorUI(c,base);},"B\n",false);
    need(PPC_LOAD_U32(ov)==ERROR_CANCELLED&&!PPC_LOAD_U32(device),"Insufficient storage returned a selected device");
    auto rejects=[&](PPCContext value){PPCContext before;std::memcpy(&before,&value,sizeof(value));
        std::array<uint8_t,0x2C> bytes{};std::memcpy(bytes.data(),f.rt.pointer(ov-8,unsigned(bytes.size()),false),bytes.size());const auto originalDevice=PPC_LOAD_U32(device);
        bool failed=false;try{__imp__XamShowDeviceSelectorUI(value,base);}catch(const Failure&){failed=true;}
        need(failed&&!std::memcmp(&value,&before,sizeof(value))&&!std::memcmp(bytes.data(),f.rt.pointer(ov-8,unsigned(bytes.size()),false),bytes.size())&&PPC_LOAD_U32(device)==originalDevice,
            "Rejected selector changed CPU/output bytes");need(!FindWindowExW(f.rt.window->handle(),nullptr,L"SimpsonsNativeStorageSelector",nullptr)&&!f.observed->read(9),"Rejected selector showed a UI");};
    c=f.prepare();c.r3.u32=4;rejects(c);c=f.prepare();c.r4.u32=2;rejects(c);
    for(uint32_t bit:{1u,0x400u,0x80000000u}){c=f.prepare();c.r5.u32|=bit;rejects(c);}
    c=f.prepare();c.r7.u32=ov;rejects(c);c=f.prepare();c.r8.u32=0x40000;rejects(c);
    c=f.prepare();PPC_STORE_U32(ov+0xC,1);rejects(c);c=f.prepare();PPC_STORE_U32(ov+0x10,1);rejects(c);
    c=f.prepare();PPC_STORE_U32(f.entry.r13.u32+0x150,1);rejects(c);PPC_STORE_U32(f.entry.r13.u32+0x150,0);
    f.rt.localPlayerSource()->signOut(0);c=f.prepare();rejects(c);f.rt.localPlayerSource()->activate(0,f.profile.id);
    need(fs::is_empty(f.rt.contentRoot),"Selector created save data or retained a storage probe");
}
}
int main(int argc,char** argv){try{
    need(argc==2||argc==3||(argc==4&&!std::strcmp(argv[2],"--flags")),"Expected original image, optional UI capture or --flags0..3");
    Fixture f(argv[1]);
    if(argc==4){need(argv[3][0]>='0'&&argv[3][0]<='3'&&!argv[3][1],"Expected independent flag pair0..3");
        const auto pair=uint32_t(argv[3][0]-'0');original(f,{},pair);contracts(f);f.retire();
        std::printf("PASS original storage flags=%03X:%zu checks; real modal input, original create/use/poll/destructor and balanced notification handles; malformed flags/completions still reject\n",pair<<8,checks.load());return 0;}
    for(uint32_t pair=0;pair<4;++pair)original(f,argc==3&&pair==3?fs::path(argv[2]):fs::path{},pair);
    contracts(f);f.retire();
    auto c=f.prepare();bool cancelled=false;try{drive(f,[&]{__imp__XamShowDeviceSelectorUI(c,f.rt.base);},"",true,{},true);}catch(const Failure&){cancelled=true;}
    need(cancelled&&f.rt.stopping&&!FindWindowExW(f.rt.window->handle(),nullptr,L"SimpsonsNativeStorageSelector",nullptr)&&IsWindowEnabled(f.rt.window->handle()),"Stop did not retire actual native UI ownership");
    std::printf("PASS native storage selector:%zu checks; real visible UI, file commands, native directory/capacity, original request/poll/notifications, cancellation and CPU/host state\n",checks.load());return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL native storage selector:%s\n",error.what());return 1;}}
