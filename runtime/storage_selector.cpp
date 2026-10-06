#include "runtime.h"
#include "native_window.h"
#include "native_content.h"
#include "native_controllers.h"
#include "native_local_players.h"
#include "native_notifications.h"
#include "engine_driver.h"
#include "renderer/native_storage_screen.h"
#include <cstdio>
#include <exception>
#include <fstream>
#include <optional>

namespace {
using namespace Simpsons;
void need(bool ok,const char* why){if(!ok)throw Failure(std::string("Native storage selector: ")+why);}
bool overlaps(uint32_t a,uint32_t n,uint32_t b,uint32_t m){return uint64_t(a)<uint64_t(b)+m&&uint64_t(b)<uint64_t(a)+n;}
std::string auditHex(uint32_t value){char text[11];std::snprintf(text,sizeof(text),"0x%08X",value);return text;}
// Observe raw producer arguments before admission. A diagnostic read never
// qualifies a completion pointer, profile or window for the actual operation.
void auditRequest(Runtime& rt,const PPCContext& ctx,const std::filesystem::path& root,
                  const Platform::LocalProfile* actualProfile=nullptr,bool profileKnown=false) noexcept {
    if(!rt.resourceAudit.active())return;
    try {
        std::string ownership="profile=unknown window=absent completion=unknown";
        std::string instance="deviceOut="+auditHex(ctx.r7.u32)+" overlapped="+auditHex(ctx.r8.u32)+
            " threadContext="+auditHex(ctx.r13.u32)+" sp="+auditHex(ctx.r1.u32);
        const std::string profile=profileKnown?(actualProfile?"active":"inactive"):"unknown";
        if(actualProfile)instance+=" profileId="+actualProfile->id;
        std::string completion="unknown";
        try {const auto* p=rt.pointer(ctx.r8.u32,0x1C,false);
            const auto word=[&](uint32_t at){return uint32_t(p[at])<<24|uint32_t(p[at+1])<<16|uint32_t(p[at+2])<<8|p[at+3];};
            const auto event=word(0xC),callback=word(0x10);
            completion="readable event="+std::string(event?"present":"none")+" callback="+(callback?"present":"none");
            instance+=" completionEvent="+auditHex(event)+" completionCallback="+auditHex(callback);
        }catch(...){}
        ownership="profile="+profile+" window="+(rt.window&&rt.window->handle()&&!rt.window->closed?"owned":"absent")+
            " completion="+completion;
        rt.resourceAudit.observe("storage-selector",root.generic_string(),uint32_t(ctx.lr),
            "user="+std::to_string(ctx.r3.u32)+" type="+std::to_string(ctx.r4.u32)+
            " flags="+auditHex(ctx.r5.u32)+" requested="+std::to_string(ctx.r6.u64),ownership,0,instance);
    }catch(...){std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] original storage request snapshot failed\n");}
}
struct HostState {
    const uint32_t fp=PPCFPSCRRegister::getcsr();const DWORD error=GetLastError();
    HostState(){PPCFPSCRRegister::restoreHostCSR(PPCFPSCRRegister::DefaultCSR);}
    ~HostState(){PPCFPSCRRegister::restoreHostCSR(fp);SetLastError(error);}
};
// A GPU-rendered child surface inside the actual game window. Original guest
// outputs remain on the import stack; only host values enter its callback.
struct Selector {
    Runtime& rt;
    const Platform::LocalProfile profile;
    const uint32_t slot;
    const uint64_t requested;
    Platform::NativeStorage storage;
    std::shared_ptr<Platform::NativeControllers> input;
    std::shared_ptr<Platform::NativeNotifications> notifications;
    std::unique_ptr<Graphics::NativeStorageScreen> screen;
    uint64_t token=0;
    HWND window=nullptr,owner=nullptr;
    DPI_AWARENESS_CONTEXT previousDpi=nullptr;
    unsigned row=0;
    WORD previousButtons=0;
    bool closed=false,selected=false,published=false,previousMenuMouse=false,menuOwned=false;
    std::exception_ptr error;
    Selector(Runtime& runtime,Platform::LocalProfile player,uint32_t user,uint64_t bytes,Platform::NativeStorage destination)
        :rt(runtime),profile(std::move(player)),slot(user),requested(bytes),storage(std::move(destination)),
         input(rt.controllerSource()),notifications(rt.notificationSource()){}
    ~Selector(){
        screen.reset();if(window)DestroyWindow(window);
        if(menuOwned&&rt.window&&!rt.window->closed)rt.window->setMenuMouse(previousMenuMouse);
        if(previousDpi)SetThreadDpiAwarenessContext(previousDpi);
        if(token)try{input->endModal(token);}catch(...){rt.requestStop("Native storage UI input cleanup failed");}
        if(published)try{notifications->publish({9,0});}catch(...){rt.requestStop("Native storage UI close notification failed");}
    }
    void render(){
        if(!screen)return;
        Graphics::NativeStorageScreenState state;
        state.player=std::filesystem::path(profile.name).wstring();state.folder=storage.path.wstring();
        state.availableBytes=storage.availableBytes;state.requiredBytes=requested;
        // Inspection proved folder access; capacity is a separate refusal.
        state.selectedRow=row;state.storageAvailable=true;
        state.controller=slot!=0||!input->usesKeyboardMouse();
        // Occlusion/minimize can decline DXGI display acceptance while the
        // owned screen and input remain live. Real GPU errors still throw.
        if(screen->render(state))rt.window->recordPresentedFrame();
    }
    void capture(const std::filesystem::path& path){
        const auto rgba=screen->readbackRGBA();const uint32_t w=screen->width(),h=screen->height();
        need(rgba.size()==size_t(w)*h*4,"screen capture extent differs");
        auto bgra=rgba;for(size_t at=0;at<bgra.size();at+=4)std::swap(bgra[at],bgra[at+2]);
        BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=LONG(w);info.biHeight=-LONG(h);
        info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
        BITMAPFILEHEADER header{};header.bfType=0x4D42;header.bfOffBits=sizeof(header)+sizeof(info);
        header.bfSize=header.bfOffBits+DWORD(bgra.size());
        std::ofstream out(path,std::ios::binary|std::ios::trunc);
        out.write(reinterpret_cast<const char*>(&header),sizeof(header));out.write(reinterpret_cast<const char*>(&info),sizeof(info));
        out.write(reinterpret_cast<const char*>(bgra.data()),std::streamsize(bgra.size()));out.close();
        need(bool(out),"screen capture write failed");
    }
    void choose(bool useFolder){
        if(useFolder){
            if(!storage.fits(requested))return;
            auto refreshed=Platform::inspectNativeStorage(storage.path);
            if(!refreshed.fits(requested)){storage=std::move(refreshed);row=1;render();return;}
            const auto current=rt.localPlayerSource()->profile(slot);
            need(current&&current->id==profile.id,"active player changed during selection");
            storage=std::move(refreshed);selected=true;
        }
        DestroyWindow(window);
    }
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam){
        auto* self=reinterpret_cast<Selector*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Selector*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));self->window=hwnd;}
        if(!self)return DefWindowProcW(hwnd,message,wparam,lparam);
        try{
            if(message==WM_CLOSE){DestroyWindow(hwnd);return 0;}
            if(message==WM_DESTROY){
                self->closed=true;self->window=nullptr;
                // A synchronous SendMessage can destroy the child while
                // GetMessage is dispatching sent messages internally. Wake
                // that wait so it can observe closed even with no timer left.
                PostThreadMessageW(GetCurrentThreadId(),WM_NULL,0,0);return 0;
            }
            if(message==WM_ERASEBKGND)return 1;
            if(message==WM_PAINT){PAINTSTRUCT paint{};BeginPaint(hwnd,&paint);EndPaint(hwnd,&paint);self->render();return 0;}
            // Keep keyboard focus on the game owner: transferring it to this
            // child would clear held keys and could turn auto-repeat into a
            // fresh acceptance before the initiating key is released.
            if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
            if(message==WM_KEYDOWN||message==WM_KEYUP||message==WM_SYSKEYDOWN||message==WM_SYSKEYUP){
                if(wparam==VK_F4&&(message==WM_SYSKEYDOWN||message==WM_SYSKEYUP)){
                    if(message==WM_SYSKEYDOWN)PostMessageW(self->owner,WM_CLOSE,0,0);return 0;
                }
                const bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN;
                if(wparam==VK_TAB){if(down&&!(lparam&(LPARAM(1)<<30))){self->row=self->row?0:1;self->render();}return 0;}
                self->rt.window->keyboard->key(uint32_t(wparam),down);return 0;
            }
            if(message==WM_COMMAND&&(LOWORD(wparam)==IDOK||LOWORD(wparam)==IDCANCEL)){
                self->choose(LOWORD(wparam)==IDOK);return 0;
            }
            if(message==WM_RBUTTONDOWN){self->choose(false);return 0;}
            if((message==WM_MOUSEMOVE||message==WM_LBUTTONDOWN)&&self->screen){
                const int row=self->screen->hitTest(int(short(LOWORD(lparam))),int(short(HIWORD(lparam))));
                if(row>=0){const auto value=unsigned(row);if(value!=self->row){self->row=value;self->render();}
                    if(message==WM_LBUTTONDOWN)self->choose(value==0);}
                return 0;
            }
            if(message==WM_TIMER){
                if(WaitForSingleObject(self->rt.stopEvent,0)==WAIT_OBJECT_0||self->rt.window->closed){DestroyWindow(hwnd);return 0;}
                RECT client{};need(GetClientRect(self->owner,&client)!=FALSE,"game client extent query failed");
                if(self->screen&&(self->screen->width()!=uint32_t(client.right)||self->screen->height()!=uint32_t(client.bottom))&&client.right>0&&client.bottom>0){
                    need(SetWindowPos(hwnd,nullptr,0,0,client.right,client.bottom,SWP_NOACTIVATE|SWP_NOZORDER)!=FALSE,"screen extent change failed");
                    self->screen->resize(uint32_t(client.right),uint32_t(client.bottom));self->render();
                }
                XINPUT_STATE state{};
                if(self->input->modalState(self->token,state)==ERROR_SUCCESS){
                    auto buttons=state.Gamepad.wButtons;
                    if(state.Gamepad.sThumbLY>16000)buttons|=XINPUT_GAMEPAD_DPAD_UP;
                    if(state.Gamepad.sThumbLY<-16000)buttons|=XINPUT_GAMEPAD_DPAD_DOWN;
                    const auto pressed=WORD(buttons&~self->previousButtons);self->previousButtons=buttons;
                    if(pressed&XINPUT_GAMEPAD_B)self->choose(false);
                    else if(pressed&XINPUT_GAMEPAD_A)self->choose(self->row==0);
                    else if(pressed&(XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_DOWN)){
                        self->row=self->row?0:1;self->render();
                    }
                }
                return 0;
            }
        }catch(...){self->error=std::current_exception();if(IsWindow(hwnd))DestroyWindow(hwnd);return 0;}
        return DefWindowProcW(hwnd,message,wparam,lparam);
    }
    bool run(){
        need(rt.window&&rt.window->handle()&&!rt.window->closed,"owned game window is absent");owner=rt.window->handle();
        DWORD process{};need(GetWindowThreadProcessId(owner,&process)!=0&&process==GetCurrentProcessId(),"game window ownership differs");
        previousDpi=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        need(previousDpi!=nullptr,"native UI DPI context failed");RECT client{};
        need(GetClientRect(owner,&client)&&client.right>0&&client.bottom>0,"game client extent is empty");
        WNDCLASSW type{};type.lpfnWndProc=procedure;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"SimpsonsNativeStorageSelector";
        type.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
        need(RegisterClassW(&type)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"in-game screen class registration failed");
        previousMenuMouse=rt.window->isMenuMouse();rt.window->setMenuMouse(true);menuOwned=true;
        Graphics::StorageScreenBackground backdrop;
        {
            std::lock_guard lifetime(rt.engineDriverMutex);
            if(rt.engineDriver)backdrop.rgba=rt.engineDriver->readbackMenuFrame(backdrop.width,backdrop.height);
        }
        token=input->beginModal(slot);
        need(CreateWindowExW(WS_EX_NOACTIVATE,type.lpszClassName,L"Save Storage",WS_CHILD,
            0,0,client.right,client.bottom,owner,nullptr,type.hInstance,this)!=nullptr,"in-game screen creation failed");
        wchar_t executable[32768]{};const DWORD length=GetModuleFileNameW(nullptr,executable,DWORD(std::size(executable)));
        need(length&&length<std::size(executable),"native asset directory lookup failed");
        const auto assets=std::filesystem::path(executable).parent_path()/L"native-assets"/L"storage-screen";
        screen=std::make_unique<Graphics::NativeStorageScreen>(window,uint32_t(client.right),uint32_t(client.bottom),std::move(backdrop),assets);
        row=storage.fits(requested)?0u:1u;
        need(SetTimer(window,1,16,nullptr)!=0,"native UI input timer failed");
        ShowWindow(window,SW_SHOWNOACTIVATE);
        need(IsWindowVisible(window)!=FALSE,"in-game screen was not shown");
        render();
        wchar_t capturePath[32768]{};const DWORD captureLength=GetEnvironmentVariableW(L"SIMPSONS_STORAGE_CAPTURE",capturePath,DWORD(std::size(capturePath)));
        if(captureLength){need(captureLength<std::size(capturePath),"screen capture path is too long");capture(capturePath);}
        notifications->publish({9,1});published=true;
        std::fprintf(stderr,"[NATIVE STORAGE UI] visible hwnd=%p owner=%p in_game=1 player=%s folder=%ls available=%llu requested=%llu accept=%u; A=use folder B=continue without saving\n",
            static_cast<void*>(window),static_cast<void*>(owner),profile.name.c_str(),storage.path.c_str(),static_cast<unsigned long long>(storage.availableBytes),static_cast<unsigned long long>(requested),storage.fits(requested));
        while(!closed){
            MSG message{};const BOOL got=GetMessageW(&message,nullptr,0,0);need(got>0,"native UI message loop stopped");
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        screen.reset();
        input->endModal(token);token=0;
        if(error)std::rethrow_exception(error);rt.checkRunning();
        return selected;
    }
    void notifyClosed(){notifications->publish({9,0});published=false;}
};

}

PPC_FUNC(__imp__XamShowDeviceSelectorUI){
    HostState host;need(active&&base==active->base,"foreign runtime");auto& rt=*active;rt.checkRunning();
    const uint32_t user=ctx.r3.u32,type=ctx.r4.u32,flags=ctx.r5.u32,deviceOut=ctx.r7.u32,ov=ctx.r8.u32;
    const uint64_t requested=ctx.r6.u64;
    const auto root=rt.contentRoot.empty()?rt.gameRoot.parent_path()/"saves":rt.contentRoot;
    auditRequest(rt,ctx,root);
    constexpr uint32_t kSelectorFlags=0x300;
    // Original8285CED8 independently adds0x200 from driver byte+4 and0x100
    // from its second boolean. Constructor8285CC78 clears byte+4; the public
    // request82CA3968 supplies the second boolean as0. All four pairs are
    // original inputs. Our one-folder platform still obtains a real UI choice.
    need(user<4&&type==1&&!(flags&~kSelectorFlags),"unqualified user/type/flags");
    need(deviceOut&&ov&&!(deviceOut&3)&&!(ov&3)&&!overlaps(deviceOut,4,ov,0x1C),"invalid or aliased selection/completion outputs");
    rt.pointer(deviceOut,4,true);rt.pointer(ov,0x1C,true);
    // The reached original request clears event and callback fields. This
    // selector retains no pointers for deferred callbacks or APC delivery.
    need(!PPC_LOAD_U32(ov+0xC)&&!PPC_LOAD_U32(ov+0x10),"event/callback selector completion is unqualified");
    rt.pointer(ctx.r13.u32,0x154,false);const auto thread=rt.threadObject(PPC_LOAD_U32(ctx.r13.u32+0x100));
    need(thread&&thread->type==KernelHandle::Type::Thread&&GetThreadId(thread->native)==GetCurrentThreadId(),"caller is not its actual native thread");
    need(!PPC_LOAD_U32(ctx.r13.u32+0x150),"interrupt-context selector is unqualified");
    const auto profile=rt.localPlayerSource()->profile(user);
    auditRequest(rt,ctx,root,profile?&*profile:nullptr,true);
    need(profile.has_value(),"selected player is inactive");
    need(rt.window&&rt.window->handle()&&!rt.window->closed,"owned game window is absent");
    auto storage=Platform::inspectNativeStorage(root);
    Selector selector(rt,*profile,user,requested,std::move(storage));
    uint32_t threadId=0;{std::lock_guard lock(rt.handleMutex);for(const auto& [id,object]:rt.handles)if(object==thread){threadId=id;break;}}
    if(!threadId)threadId=rt.addHandle(thread);
    PPC_STORE_U32(ov,ERROR_IO_PENDING);PPC_STORE_U32(ov+8,threadId);
    const bool selected=selector.run();
    const uint32_t status=selected?0:ERROR_CANCELLED;
    PPC_STORE_U32(deviceOut,selected?1:0);PPC_STORE_U32(ov+4,0);PPC_STORE_U32(ov+0x18,status);PPC_STORE_U32(ov,status);
    selector.notifyClosed();ctx.r3.u64=ERROR_IO_PENDING;
    std::fprintf(stderr,"[NATIVE STORAGE UI] completed selected=%u device=%u result=%u caller_thread=%08X; original completion and UI close published\n",selected,selected?1:0,status,threadId);
}
