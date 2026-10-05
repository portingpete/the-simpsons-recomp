#include "runtime.h"
#include "native_window.h"
#include "native_content.h"
#include "native_controllers.h"
#include "native_local_players.h"
#include "native_notifications.h"
#include <cstdio>
#include <exception>
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
// A real Win32 platform window. It owns host values only; the import's original
// output pointers never enter a callback or survive the import's stack frame.
struct Selector {
    Runtime& rt;
    const Platform::LocalProfile profile;
    const uint32_t slot;
    const uint64_t requested;
    Platform::NativeStorage storage;
    std::shared_ptr<Platform::NativeControllers> input;
    std::shared_ptr<Platform::NativeNotifications> notifications;
    uint64_t token=0;
    HWND window=nullptr,owner=nullptr,accept=nullptr;
    HFONT font=nullptr;
    DPI_AWARENESS_CONTEXT previousDpi=nullptr;
    bool closed=false,selected=false,published=false,disabledOwner=false;
    std::exception_ptr error;
    Selector(Runtime& runtime,Platform::LocalProfile player,uint32_t user,uint64_t bytes,Platform::NativeStorage destination)
        :rt(runtime),profile(std::move(player)),slot(user),requested(bytes),storage(std::move(destination)),
         input(rt.controllerSource()),notifications(rt.notificationSource()){}
    ~Selector(){
        if(window)DestroyWindow(window);
        if(disabledOwner&&IsWindow(owner))EnableWindow(owner,TRUE);
        if(font)DeleteObject(font);
        if(previousDpi)SetThreadDpiAwarenessContext(previousDpi);
        if(token)try{input->endModal(token);}catch(...){rt.requestStop("Native storage UI input cleanup failed");}
        if(published)try{notifications->publish({9,0});}catch(...){rt.requestStop("Native storage UI close notification failed");}
    }
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam){
        auto* self=reinterpret_cast<Selector*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Selector*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));self->window=hwnd;}
        if(!self)return DefWindowProcW(hwnd,message,wparam,lparam);
        try{
            if(message==WM_CLOSE){DestroyWindow(hwnd);return 0;}
            if(message==WM_DESTROY){self->closed=true;self->window=nullptr;return 0;}
            if(message==WM_COMMAND&&(LOWORD(wparam)==IDOK||LOWORD(wparam)==IDCANCEL)){
                if(LOWORD(wparam)==IDOK){
                    if(!IsWindowEnabled(self->accept))return 0;
                    auto refreshed=Platform::inspectNativeStorage(self->storage.path);
                    if(!refreshed.fits(self->requested)){
                        EnableWindow(self->accept,FALSE);SetWindowTextW(GetDlgItem(hwnd,103),L"There is no longer enough space in this folder.");return 0;
                    }
                    const auto current=self->rt.localPlayerSource()->profile(self->slot);
                    need(current&&current->id==self->profile.id,"active player changed during selection");
                    self->storage=std::move(refreshed);self->selected=true;
                }
                DestroyWindow(hwnd);return 0;
            }
            if(message==WM_TIMER){
                if(WaitForSingleObject(self->rt.stopEvent,0)==WAIT_OBJECT_0){DestroyWindow(hwnd);return 0;}
                XINPUT_STATE state{};
                if(self->input->modalState(self->token,state)==ERROR_SUCCESS){
                    if(state.Gamepad.wButtons&XINPUT_GAMEPAD_B)SendMessageW(hwnd,WM_COMMAND,IDCANCEL,0);
                    else if(state.Gamepad.wButtons&XINPUT_GAMEPAD_A)SendMessageW(hwnd,WM_COMMAND,IDOK,0);
                }
                return 0;
            }
        }catch(...){self->error=std::current_exception();if(IsWindow(hwnd))DestroyWindow(hwnd);return 0;}
        return DefWindowProcW(hwnd,message,wparam,lparam);
    }
    HWND control(const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id,int dpi){
        HWND child=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,MulDiv(x,dpi,96),MulDiv(y,dpi,96),MulDiv(w,dpi,96),MulDiv(h,dpi,96),
            window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        need(child!=nullptr,"control creation failed");SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return child;
    }
    bool run(){
        need(rt.window&&rt.window->handle()&&!rt.window->closed,"owned game window is absent");owner=rt.window->handle();
        DWORD process{};need(GetWindowThreadProcessId(owner,&process)!=0&&process==GetCurrentProcessId(),"game window ownership differs");
        previousDpi=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        need(previousDpi!=nullptr,"native UI DPI context failed");const int dpi=int(GetDpiForWindow(owner));need(dpi>0,"native UI DPI query failed");
        WNDCLASSW type{};type.lpfnWndProc=procedure;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"SimpsonsNativeStorageSelector";
        type.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));type.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
        need(RegisterClassW(&type)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"native UI class registration failed");
        font=CreateFontW(-MulDiv(10,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        need(font!=nullptr,"native UI font creation failed");
        constexpr DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU;RECT bounds{0,0,MulDiv(700,dpi,96),MulDiv(285,dpi,96)},parent{};
        need(AdjustWindowRectExForDpi(&bounds,style,FALSE,WS_EX_DLGMODALFRAME,UINT(dpi))&&GetWindowRect(owner,&parent),"native UI layout query failed");
        const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
        token=input->beginModal(slot);
        HWND created=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,type.lpszClassName,L"The Simpsons Game — Save storage",style,
            parent.left+(parent.right-parent.left-width)/2,parent.top+(parent.bottom-parent.top-height)/2,width,height,owner,nullptr,type.hInstance,this);
        need(created!=nullptr,"native UI window creation failed");
        control(L"STATIC",L"Choose where to save",0,24,18,650,28,100,dpi);
        const std::wstring player=L"Player: "+std::wstring(profile.name.begin(),profile.name.end());
        control(L"STATIC",player.c_str(),0,24,53,650,23,101,dpi);
        control(L"EDIT",storage.path.c_str(),ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP|WS_BORDER,24,82,650,70,102,dpi);
        wchar_t capacity[256]{};swprintf_s(capacity,L"Available: %.1f GB     Needed: %.1f MB",double(storage.availableBytes)/1000000000.0,double(requested)/1000000.0);
        control(L"STATIC",capacity,0,24,162,650,25,103,dpi);
        if(!storage.fits(requested))SetWindowTextW(GetDlgItem(window,103),L"There is not enough space in this folder.");
        accept=control(L"BUTTON",L"Use this folder (A)",BS_DEFPUSHBUTTON|WS_TABSTOP,24,221,280,36,IDOK,dpi);
        control(L"BUTTON",L"Continue without saving (B)",BS_PUSHBUTTON|WS_TABSTOP,326,221,348,36,IDCANCEL,dpi);
        EnableWindow(accept,storage.fits(requested));
        need(SetTimer(window,1,16,nullptr)!=0,"native UI input timer failed");
        disabledOwner=IsWindowEnabled(owner)!=FALSE;if(disabledOwner)EnableWindow(owner,FALSE);
        ShowWindow(window,SW_SHOW);UpdateWindow(window);SetForegroundWindow(window);SetFocus(storage.fits(requested)?accept:GetDlgItem(window,IDCANCEL));
        need(IsWindowVisible(window)!=FALSE,"storage selector was not shown");
        notifications->publish({9,1});published=true;
        std::fprintf(stderr,"[NATIVE STORAGE UI] visible hwnd=%p player=%s folder=%ls available=%llu requested=%llu accept=%u; A=use folder B=continue without saving\n",
            static_cast<void*>(window),profile.name.c_str(),storage.path.c_str(),static_cast<unsigned long long>(storage.availableBytes),static_cast<unsigned long long>(requested),storage.fits(requested));
        while(!closed){
            MSG message{};const BOOL got=GetMessageW(&message,nullptr,0,0);need(got>0,"native UI message loop stopped");
            if(message.message==WM_KEYDOWN&&(message.wParam==VK_RETURN||message.wParam==VK_ESCAPE)){
                const bool cancel=message.wParam==VK_ESCAPE||GetFocus()==GetDlgItem(window,IDCANCEL);
                SendMessageW(window,WM_COMMAND,cancel?IDCANCEL:IDOK,0);continue;
            }
            if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
        }
        if(disabledOwner&&IsWindow(owner)){EnableWindow(owner,TRUE);disabledOwner=false;SetForegroundWindow(owner);}
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
    const auto root=rt.contentRoot.empty()?rt.gameRoot.parent_path()/"userdata"/"content":rt.contentRoot;
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
