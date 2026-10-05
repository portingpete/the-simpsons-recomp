#include "renderer/native_backend.h"
#include "renderer/device_availability.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <thread>

using namespace Simpsons::Graphics;
void require(bool value,const char* reason) {if(!value) throw Error(reason);}
template<class F> void rejects(F action) {
    try {action();} catch(const Error&) {return;}
    throw Error("Invalid native presentation operation did not fail");
}
// Reuse actual Get* binding snapshots; do not run or change that fixture here.
#include "header/test_engine_binding_reset.h"

namespace {
struct HiddenWindow {
    HWND handle{};
    HiddenWindow(uint32_t width,uint32_t height,bool framed=false) {
        WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);
        type.lpszClassName=L"SimpsonsNativePresentationProbe";
        if(!RegisterClassW(&type) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) throw Error("Presentation probe window class failed");
        const DWORD style=framed?WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX:WS_POPUP;
        RECT extent{0,0,LONG(width),LONG(height)};
        require(AdjustWindowRectEx(&extent,style,FALSE,WS_EX_TOOLWINDOW)!=FALSE,"Presentation probe extent failed");
        handle=CreateWindowExW(WS_EX_TOOLWINDOW,type.lpszClassName,L"Native presentation contract",style,
                              -32000,-32000,extent.right-extent.left,extent.bottom-extent.top,nullptr,nullptr,type.hInstance,nullptr);
        require(handle!=nullptr,"Hidden presentation window creation failed");
        require(!IsWindowVisible(handle),"Presentation probe unexpectedly opened a visible window");
    }
    void close() {if(handle) {DestroyWindow(handle);handle=nullptr;}}
    ~HiddenWindow() {close();}
    void minimize() {
        ShowWindow(handle,SW_SHOWMINNOACTIVE);ShowWindow(handle,SW_HIDE);
        require(IsIconic(handle)!=FALSE,"Presentation probe did not minimize");
    }
    void restore() {
        ShowWindow(handle,SW_SHOWNOACTIVATE);ShowWindow(handle,SW_HIDE);
        require(IsIconic(handle)==FALSE,"Presentation probe did not restore");
    }
};
}
namespace Simpsons::Graphics {
struct NativePresentationProbe {
    using Snapshot=EngineBindingResetProbe::Snapshot;
    static void checked(HRESULT result,const char* reason) {require(SUCCEEDED(result),reason);}
    static ComPtr<ID3D11Texture2D> boundColor(NativeBackend& backend) {
        ComPtr<ID3D11RenderTargetView> view;backend.context->OMGetRenderTargets(1,&view,nullptr);
        require(bool(view),"Presentation probe has no color zero");
        ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
        ComPtr<ID3D11Texture2D> texture;checked(resource.As(&texture),"Presentation probe color is not 2D");return texture;
    }
    static std::vector<uint8_t> readTexture(NativeBackend& backend,ID3D11Texture2D* texture) {
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
        require(desc.Format==DXGI_FORMAT_R10G10B10A2_UNORM,"Presentation transfer silently changed packed format");
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
        ComPtr<ID3D11Texture2D> staging;
        checked(backend.device->CreateTexture2D(&desc,nullptr,&staging),"Presentation staging allocation failed");
        backend.context->CopyResource(staging.Get(),texture);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        checked(backend.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Presentation staging map failed");
        std::vector<uint8_t> result(size_t(desc.Width)*desc.Height*4);
        for(uint32_t row=0;row<desc.Height;++row)
            std::memcpy(result.data()+size_t(row)*desc.Width*4,
                        static_cast<const uint8_t*>(mapped.pData)+size_t(row)*mapped.RowPitch,size_t(desc.Width)*4);
        backend.context->Unmap(staging.Get(),0);return result;
    }
    static void seedBindings(NativeBackend& backend,const std::shared_ptr<RenderTarget>& source,
                             const std::shared_ptr<DepthTarget>& depth) {
        ScreenDraw draw{};draw.vertices={{{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,-1,1,1}}};
        draw.color={0,0,0,1};draw.blendSelector=3;draw.colorWriteMask=15;backend.drawScreen(source,draw);
        backend.bindTargets({source,nullptr,nullptr,nullptr},depth);
        auto* context=backend.context.Get();
        ComPtr<ID3D11Buffer> vertex,constant;UINT stride{},offset{};
        context->IAGetVertexBuffers(0,1,&vertex,&stride,&offset);context->PSGetConstantBuffers(0,1,&constant);
        require(vertex && constant,"Presentation probe did not seed native draw buffers");
        ID3D11Buffer* v=vertex.Get();context->IASetVertexBuffers(4,1,&v,&stride,&offset);
        context->IASetVertexBuffers(31,1,&v,&stride,&offset);
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=4;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        std::array<uint32_t,16> pixels{};D3D11_SUBRESOURCE_DATA data{pixels.data(),16,64};
        ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> sampled;
        checked(backend.device->CreateTexture2D(&desc,&data,&texture),"Presentation probe texture creation failed");
        checked(backend.device->CreateShaderResourceView(texture.Get(),nullptr,&sampled),"Presentation probe SRV creation failed");
        D3D11_SAMPLER_DESC samplerDesc{};samplerDesc.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDesc.AddressU=samplerDesc.AddressV=samplerDesc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxAnisotropy=1;samplerDesc.ComparisonFunc=D3D11_COMPARISON_ALWAYS;samplerDesc.MaxLOD=D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        checked(backend.device->CreateSamplerState(&samplerDesc,&sampler),"Presentation probe sampler creation failed");
        std::array<ID3D11ShaderResourceView*,128> reads{};reads.fill(sampled.Get());
        std::array<ID3D11SamplerState*,16> samplers{};samplers.fill(sampler.Get());
        std::array<ID3D11Buffer*,14> constants{};constants.fill(constant.Get());
#define PRESENT_SEED(STAGE) \
        context->STAGE##SetShaderResources(0,128,reads.data()); \
        context->STAGE##SetSamplers(0,16,samplers.data()); \
        context->STAGE##SetConstantBuffers(0,14,constants.data())
        PRESENT_SEED(PS);PRESENT_SEED(VS);PRESENT_SEED(GS);PRESENT_SEED(HS);PRESENT_SEED(DS);PRESENT_SEED(CS);
#undef PRESENT_SEED
        const D3D11_VIEWPORT viewport{1.5f,2.5f,93,21,0.25f,0.75f};context->RSSetViewports(1,&viewport);
        const D3D11_RECT scissor{3,4,90,23};context->RSSetScissorRects(1,&scissor);
        context->SetPredication(nullptr,TRUE); // Null predicate's value must also survive.
    }
    template<class F> static void rejectUnchanged(NativeBackend& backend,F action) {
        const Snapshot before(backend.context.Get());const auto count=backend.presentationCount(),pending=backend.pendingCopies.size();
        rejects(action);
        require(Snapshot(backend.context.Get())==before,"Rejected presentation changed engine bindings");
        require(backend.presentationCount()==count && backend.pendingCopies.size()==pending,"Rejected presentation published native work");
    }
    static void minimizeRestore(bool hardware,bool minimizedAtAttach) {
        constexpr uint32_t width=320,height=180;
        HiddenWindow window(width,height,true);NativeBackend backend(!hardware);
        auto source=backend.createTarget(width,height,TargetFormat::RGB10A2);
        auto front=backend.createTarget(width,height,TargetFormat::RGB10A2);
        auto depth=backend.createDepthTarget(width,height);
        seedBindings(backend,source,depth);backend.clearTarget(source,{0.25f,0.5f,0.75f,1});
        backend.clearDepthTarget(depth,0.5f,0xA7);
        const Snapshot bindings(backend.context.Get());
        const auto expected=backend.readbackTarget(source),depthBefore=backend.readbackDepthTarget(depth);
        if(minimizedAtAttach)window.minimize();
        backend.attachWindow(window.handle,width,height);
        auto wrongExtent=backend.createTarget(width-1,height,TargetFormat::RGB10A2);
        for(unsigned cycle=0;cycle<3;++cycle) {
            window.minimize();
            RECT client{};require(GetClientRect(window.handle,&client)!=FALSE,"Minimized client query failed");
            require(client.right!=LONG(width) || client.bottom!=LONG(height),"Probe did not reproduce minimized extent change");
            rejectUnchanged(backend,[&]{backend.presentFront(wrongExtent);});
            for(unsigned restored=0;restored<2;++restored) {
                if(restored)window.restore();
                auto copy=backend.copyFront(source,front);
                auto transfer=backend.submitFrontPresentation(front);backend.waitCopy(transfer);
                ComPtr<ID3D11Texture2D> back;
                checked(backend.swapChain->GetBuffer(0,IID_PPV_ARGS(&back)),"Minimize/restore buffer query failed");
                require(readTexture(backend,back.Get())==expected,"Minimize/restore lost front-to-swapchain copy");
                const auto count=backend.presentationCount();const bool accepted=backend.presentFront(front);
                require(backend.presentationCount()==count+uint64_t(accepted),"Minimize/restore display count is wrong");
                require(backend.copyComplete(copy) && backend.pendingCopies.empty(),"Minimize/restore did not retire GPU copies");
                require(Snapshot(backend.context.Get())==bindings,"Minimize/restore changed engine bindings");
                require(backend.readbackTarget(front)==expected && backend.readbackTarget(source)==expected &&
                        backend.readbackDepthTarget(depth)==depthBefore,"Minimize/restore changed rendering contents");
            }
        }
        RECT outer{};require(GetWindowRect(window.handle,&outer)!=FALSE,"Restored window query failed");
        require(SetWindowPos(window.handle,nullptr,0,0,outer.right-outer.left+1,outer.bottom-outer.top,
                            SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"Resize rejection setup failed");
        rejectUnchanged(backend,[&]{backend.presentFront(front);});
        window.minimize();window.close();
        rejectUnchanged(backend,[&]{backend.presentFront(front);});
        std::printf("PASS minimize/restore %s: attach=%s, 3 cycles, exact transfer/retirement/state, resize/closed rejection\n",
                    hardware?"hardware":"WARP",minimizedAtAttach?"minimized":"normal");
    }
    static void run(bool hardware) {
        constexpr uint32_t width=128,height=32;
        HiddenWindow window(width,height);NativeBackend backend(!hardware),foreign(!hardware);
        auto source=backend.createTarget(width,height,TargetFormat::RGB10A2);
        auto front=backend.createTarget(width,height,TargetFormat::RGB10A2);
        auto depth=backend.createDepthTarget(width,height);
        seedBindings(backend,source,depth);backend.clearDepthTarget(depth,0.5f,0xA7);
        const auto depthBefore=backend.readbackDepthTarget(depth);
        auto sourceTexture=boundColor(backend);
        std::vector<uint32_t> packed(width*height);
        std::array<bool,1024> red{},green{},blue{};std::array<bool,4> alpha{};
        for(uint32_t i=0;i<packed.size();++i) {
            const uint32_t r=i&1023,g=(i*341+17)&1023,b=(i*683+91)&1023,a=i/1024;
            packed[i]=r|(g<<10)|(b<<20)|(a<<30);red[r]=green[g]=blue[b]=alpha[a]=true;
        }
        require(std::all_of(red.begin(),red.end(),[](bool v){return v;}) &&
                std::all_of(green.begin(),green.end(),[](bool v){return v;}) &&
                std::all_of(blue.begin(),blue.end(),[](bool v){return v;}) &&
                std::all_of(alpha.begin(),alpha.end(),[](bool v){return v;}),"Packed probe omitted component codes");
        std::vector<uint8_t> expected(packed.size()*4);std::memcpy(expected.data(),packed.data(),expected.size());
        backend.context->UpdateSubresource(sourceTexture.Get(),0,nullptr,packed.data(),width*4,UINT(expected.size()));
        backend.clearTarget(front,{0,0,0,0});
        const Snapshot bindings(backend.context.Get());const auto draws=backend.screenDrawCount();
        rejectUnchanged(backend,[&]{backend.validateFrontPresentation(front);});
        rejectUnchanged(backend,[&]{backend.presentFront(front);});
        rejectUnchanged(backend,[&]{backend.attachWindow(window.handle,width+1,height);});
        backend.attachWindow(window.handle,width,height);
        DXGI_SWAP_CHAIN_DESC1 swapDesc{};checked(backend.swapChain->GetDesc1(&swapDesc),"Swapchain descriptor unavailable");
        require(swapDesc.Format==DXGI_FORMAT_R10G10B10A2_UNORM && swapDesc.AlphaMode==DXGI_ALPHA_MODE_IGNORE,
                "Presentation did not use packed RGB10A2/alpha-ignore swapchain");
        require(Snapshot(backend.context.Get())==bindings,"Swapchain attachment changed engine bindings");
        rejectUnchanged(backend,[&]{backend.attachWindow(window.handle,width,height);});
        auto wrongFormat=backend.createTarget(width,height,TargetFormat::RGBA8);
        auto wrongExtent=backend.createTarget(width-1,height,TargetFormat::RGB10A2);
        auto foreignTarget=foreign.createTarget(width,height,TargetFormat::RGB10A2);
        auto alias=std::make_shared<RenderTarget>(*source);
        for(const auto& bad:{std::shared_ptr<RenderTarget>{},std::make_shared<RenderTarget>(),wrongFormat,wrongExtent,foreignTarget,alias})
            rejectUnchanged(backend,[&]{backend.copyFront(source,bad);});
        rejectUnchanged(backend,[&]{backend.copyFront(foreignTarget,front);});
        rejectUnchanged(backend,[&]{backend.copyFront(source,source);});
        backend.bindTargets({front,nullptr,nullptr,nullptr},depth);
        rejectUnchanged(backend,[&]{backend.copyFront(source,front);});
        backend.bindTargets({source,nullptr,nullptr,nullptr},depth);
        source->width=width+1;rejectUnchanged(backend,[&]{backend.copyFront(source,front);});source->width=width;
        front->format=TargetFormat::RGBA8;rejectUnchanged(backend,[&]{backend.presentFront(front);});front->format=TargetFormat::RGB10A2;
        rejectUnchanged(backend,[&]{backend.validateFrontPresentation(wrongExtent);});
        rejectUnchanged(backend,[&]{backend.copyComplete(nullptr);});
        rejectUnchanged(backend,[&]{backend.waitCopy(nullptr);});

        D3D11_QUERY_DESC predicateDesc{D3D11_QUERY_OCCLUSION_PREDICATE,0};ComPtr<ID3D11Predicate> predicate;
        checked(backend.device->CreatePredicate(&predicateDesc,&predicate),"Probe predicate creation failed");
        backend.context->Begin(predicate.Get());backend.context->End(predicate.Get());backend.context->SetPredication(predicate.Get(),TRUE);
        rejectUnchanged(backend,[&]{backend.copyFront(source,front);});
        rejectUnchanged(backend,[&]{backend.presentFront(front);});rejectUnchanged(backend,[&]{backend.waitIdle();});
        ComPtr<ID3D11Predicate> actualPredicate;BOOL predicateValue{};
        backend.context->GetPredication(&actualPredicate,&predicateValue);
        require(actualPredicate.Get()==predicate.Get() && predicateValue==TRUE,"Rejection changed native predication");
        backend.context->SetPredication(nullptr,TRUE);
        auto* output=backend.backbuffer.Get();backend.context->OMSetRenderTargets(1,&output,nullptr);
        rejectUnchanged(backend,[&]{backend.validateFrontPresentation(front);});
        rejectUnchanged(backend,[&]{backend.presentFront(front);});backend.bindTargets({source,nullptr,nullptr,nullptr},depth);

        backend.validateFrontCopy(source,front);backend.validateFrontPresentation(front);
        auto copy=backend.copyFront(source,front);
        require(copy && backend.pendingCopies.size()==1,"Submitted copy did not acquire a real retained receipt");
        const bool immediatelyComplete=backend.copyComplete(copy); // Either real result is legal; never assume pending.
        backend.waitCopy(copy);require(backend.copyComplete(copy),"Wait did not prove real copy completion");
        require(backend.pendingCopies.empty(),"Completed copy retained an unnecessary backend lease");
        require(backend.readbackTarget(front)==expected,"RGB10A2 copy lost raw component or two-bit alpha codes");
        require(backend.readbackTarget(source)==expected && backend.readbackDepthTarget(depth)==depthBefore,
                "Front copy changed source or depth/stencil contents");
        require(Snapshot(backend.context.Get())==bindings,"Copy/wait changed engine bindings");
        rejects([&]{foreign.copyComplete(copy);});rejects([&]{foreign.waitCopy(copy);});
        std::vector<std::function<void()>> wrongThreadCalls={
            [&]{backend.validateFrontCopy(source,front);},[&]{backend.copyFront(source,front);},
            [&]{backend.copyComplete(copy);},[&]{backend.waitCopy(copy);},
            [&]{backend.validateFrontPresentation(front);},[&]{backend.presentFront(front);},[&]{backend.waitIdle();}};
        for(auto& call:wrongThreadCalls) {
            std::atomic<bool> rejected=false;
            std::thread thread([&]{try {call();} catch(const Error& error) {
                rejected=std::string(error.what()).starts_with("Native graphics immediate context used from a different thread");
            }});thread.join();require(rejected,"Presentation API accepted a foreign owner thread");
        }
        require(Snapshot(backend.context.Get())==bindings,"Foreign owner changed native bindings");

        // Exercise the exact private transfer prefix used by presentFront, then
        // inspect its real swapchain buffer BEFORE flip-discard Present makes
        // those contents undefined. No test callback/mutator exists in production.
        auto transfer=backend.submitFrontPresentation(front);backend.waitCopy(transfer);
        ComPtr<ID3D11Texture2D> back;
        checked(backend.swapChain->GetBuffer(0,IID_PPV_ARGS(&back)),"Swapchain transfer buffer query failed");
        require(readTexture(backend,back.Get())==expected,"Actual front-to-swapchain transfer changed packed 10-bit/alpha codes");
        require(backend.presentationCount()==0,"Transfer completion was counted as display acceptance");
        require(Snapshot(backend.context.Get())==bindings,"Swapchain copy changed native bindings");
        transfer.reset();back.Reset();
        std::array<std::shared_ptr<Buffer>,4> priorBuffers;
        std::vector<uint8_t> priorBytes(0x40000);
        for(size_t i=0;i<priorBytes.size();++i)priorBytes[i]=uint8_t(i*19+(i>>9));
        for(auto& buffer:priorBuffers) {
            buffer=backend.createBuffer(UINT(priorBytes.size()),BufferKind::Vertex);
            backend.writeBuffer(buffer,0,priorBytes);
        }
        D3D11_QUERY_DESC priorDesc{D3D11_QUERY_EVENT,0};ComPtr<ID3D11Query> priorEvent;
        checked(backend.device->CreateQuery(&priorDesc,&priorEvent),"Prior submission event allocation failed");
        backend.context->End(priorEvent.Get());
        // Keep the last raw upload pending until the real presentation barrier.
        backend.queueIm2DBufferWrite(priorBuffers.back(),0,priorBytes);
        const auto rawUploads=backend.bufferUploadCount();
        const bool accepted=backend.presentFront(front);
        require(backend.bufferUploadCount()==rawUploads+1 && !backend.pendingIm2DBuffer,
            "Presentation did not submit queued raw vertex bytes before its transfer event");
        BOOL priorComplete=FALSE;
        require(backend.context->GetData(priorEvent.Get(),&priorComplete,sizeof(priorComplete),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK&&priorComplete,
            "Completed front transfer did not retire earlier GPU buffer use without an extra idle wait");
        for(const auto& buffer:priorBuffers)
            require(backend.readbackBuffer(buffer)==priorBytes,"Presentation retirement lost prior buffer contents");
        require(backend.presentationCount()==(accepted?1u:0u),"Presentation acceptance/occlusion count is wrong");
        require(backend.copyComplete(copy),"Display status changed independent copy completion");
        require(Snapshot(backend.context.Get())==bindings,"Actual Present changed engine bindings");
        require(backend.readbackTarget(front)==expected && backend.readbackTarget(source)==expected &&
                backend.readbackDepthTarget(depth)==depthBefore,"Presentation changed front/source/depth contents");
        require(backend.screenDrawCount()==draws,"Presentation secretly rendered an extra screen quad");
        backend.context->GetPredication(&actualPredicate,&predicateValue);
        require(!actualPredicate && predicateValue==TRUE,"Presentation changed null predication state");

        // Queued scheduling: queue a NEW scene->front copy and enter presentFront
        // immediately, without a pre-wait or readback (either would mask
        // scheduling). The single-owner immediate context queues CopyResource
        // asynchronously, so order preserves source->front->swapchain while the
        // queued Present(1,0) is no longer serialized behind the scene->front
        // wait. presentFront return still proves the real front->swapchain
        // transfer; the queued scene->front receipt must also be complete.
        // The flip-discard buffer is NOT read after Present: its contents are
        // undefined once flipped.
        std::vector<uint32_t> packed2(width*height);
        std::array<bool,1024> red2{},green2{},blue2{};std::array<bool,4> alpha2{};
        for(uint32_t i=0;i<packed2.size();++i) {
            const uint32_t r=(i*7u+311u)&1023u,g=(i*13u+577u)&1023u,b=(i*29u+991u)&1023u,a=(i*3u+1u)&3u;
            packed2[i]=r|(g<<10)|(b<<20)|(a<<30);red2[r]=green2[g]=blue2[b]=alpha2[a]=true;
        }
        require(std::all_of(red2.begin(),red2.end(),[](bool v){return v;}) &&
                std::all_of(green2.begin(),green2.end(),[](bool v){return v;}) &&
                std::all_of(blue2.begin(),blue2.end(),[](bool v){return v;}) &&
                std::all_of(alpha2.begin(),alpha2.end(),[](bool v){return v;}),"Queued probe omitted component codes");
        std::vector<uint8_t> expected2(packed2.size()*4);std::memcpy(expected2.data(),packed2.data(),expected2.size());
        require(expected2!=expected,"Queued probe pattern did not change packed pixels");
        backend.context->UpdateSubresource(sourceTexture.Get(),0,nullptr,packed2.data(),width*4,UINT(expected2.size()));
        backend.clearTarget(front,{1,0,0,0}); // Queued before the copy so the copy must overwrite front.
        require(Snapshot(backend.context.Get())==bindings,"Queued pattern upload changed engine bindings");
        std::array<std::shared_ptr<Buffer>,4> priorBuffers2;
        std::vector<uint8_t> priorBytes2(0x40000);
        for(size_t i=0;i<priorBytes2.size();++i)priorBytes2[i]=uint8_t(i*23+(i>>7));
        for(auto& buffer:priorBuffers2) {
            buffer=backend.createBuffer(UINT(priorBytes2.size()),BufferKind::Vertex);
            backend.writeBuffer(buffer,0,priorBytes2);
        }
        D3D11_QUERY_DESC priorDesc2{D3D11_QUERY_EVENT,0};ComPtr<ID3D11Query> priorEvent2;
        checked(backend.device->CreateQuery(&priorDesc2,&priorEvent2),"Queued prior submission event allocation failed");
        backend.context->End(priorEvent2.Get());
        backend.queueIm2DBufferWrite(priorBuffers2.back(),0,priorBytes2);
        const auto rawUploads2=backend.bufferUploadCount();
        auto queuedCopy=backend.copyFront(source,front);
        require(queuedCopy && !backend.pendingCopies.empty(),"Queued copy did not acquire a real retained receipt");
        const auto countBeforeQueued=backend.presentationCount();
        const bool accepted2=backend.presentFront(front);
        require(backend.copyComplete(queuedCopy),"Queued scene->front copy did not actually complete after queued Present");
        require(backend.pendingCopies.empty(),"Queued presentation retained a copy lease");
        require(backend.bufferUploadCount()==rawUploads2+1 && !backend.pendingIm2DBuffer,
            "Queued presentation did not submit queued raw vertex bytes before its transfer event");
        BOOL priorComplete2=FALSE;
        require(backend.context->GetData(priorEvent2.Get(),&priorComplete2,sizeof(priorComplete2),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK&&priorComplete2,
            "Queued front transfer did not retire earlier GPU buffer use without an extra idle wait");
        for(const auto& buffer:priorBuffers2)
            require(backend.readbackBuffer(buffer)==priorBytes2,"Queued presentation retirement lost prior buffer contents");
        require(backend.presentationCount()==countBeforeQueued+(accepted2?1u:0u),"Queued presentation acceptance/occlusion count is wrong");
        require(Snapshot(backend.context.Get())==bindings,"Queued Present changed engine bindings");
        require(backend.readbackTarget(front)==expected2 && backend.readbackTarget(source)==expected2 &&
                backend.readbackDepthTarget(depth)==depthBefore,"Queued presentation changed front/source/depth contents");
        require(backend.screenDrawCount()==draws,"Queued presentation secretly rendered an extra screen quad");
        backend.context->GetPredication(&actualPredicate,&predicateValue);
        require(!actualPredicate && predicateValue==TRUE,"Queued presentation changed null predication state");

        // An abandoned receipt still owns CPU wrappers until actual retirement.
        auto temporarySource=backend.createTarget(width,height,TargetFormat::RGB10A2);
        auto temporaryFront=backend.createTarget(width,height,TargetFormat::RGB10A2);
        backend.bindTargets({temporarySource,nullptr,nullptr,nullptr},depth);
        std::weak_ptr<RenderTarget> weakSource=temporarySource,weakFront=temporaryFront;
        auto abandoned=backend.copyFront(temporarySource,temporaryFront);
        backend.bindTargets({source,nullptr,nullptr,nullptr},depth);
        temporarySource.reset();temporaryFront.reset();abandoned.reset();
        require(!weakSource.expired() && !weakFront.expired(),"Abandoned copy lost source/front ownership before retirement");
        backend.waitIdle();require(weakSource.expired() && weakFront.expired(),"Idle did not retire abandoned copy ownership");
        std::array<std::shared_ptr<Buffer>,4> buffers;
        std::vector<uint8_t> bufferBytes(0x40000);
        for(size_t i=0;i<bufferBytes.size();++i) bufferBytes[i]=uint8_t(i*37+(i>>8));
        for(auto& buffer:buffers) {buffer=backend.createBuffer(UINT(bufferBytes.size()),BufferKind::Vertex);backend.writeBuffer(buffer,0,bufferBytes);}
        backend.queueIm2DBufferWrite(buffers.back(),0,bufferBytes);
        const auto beforeIdleUploads=backend.bufferUploadCount();
        backend.waitIdle();
        require(backend.bufferUploadCount()==beforeIdleUploads+1 && !backend.pendingIm2DBuffer,
            "Idle did not drain queued raw vertex bytes before its event");
        for(const auto& buffer:buffers) require(backend.readbackBuffer(buffer)==bufferBytes,"Idle did not cover all four native buffers");
        // Existing clear/present APIs remain usable with the new packed chain.
        backend.clear({0,0,0,1});const auto legacyCount=backend.presentationCount();const bool legacy=backend.present();
        require(backend.presentationCount()==legacyCount+(legacy?1u:0u),"Legacy presentation counter regressed");
        backend.waitIdle();
        require(SetWindowPos(window.handle,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,
                "Empty restored client setup failed");
        require(!IsIconic(window.handle),"Empty client test unexpectedly minimized the window");
        rejectUnchanged(backend,[&]{backend.presentFront(front);});
        window.close();
        rejectUnchanged(backend,[&]{backend.validateFrontPresentation(front);});
        rejectUnchanged(backend,[&]{backend.presentFront(front);});
        backend.clearBindings();
        // Inject the real registered Win32 notification into an isolated device.
        // This exercises the actual asynchronous callback and rejection path;
        // it does not reset the GPU or claim a hardware-removal experiment.
        {
            NativeBackend notifiedBackend(!hardware);
            auto& notification=*notifiedBackend.availability;
            require(notification.registered,"Current D3D11 fixture lacks removal notifications");
            notifiedBackend.validateSubmissionContext();
            require(SetEvent(notification.event)!=FALSE,"Removal notification injection failed");
            const auto deadline=GetTickCount64()+1000;
            while(!notification.removed.load(std::memory_order_acquire) && GetTickCount64()<deadline)Sleep(1);
            require(notification.removed.load(),"Registered removal callback did not run");
            rejects([&]{notifiedBackend.validateSubmissionContext();});
            rejects([&]{notifiedBackend.createBuffer(64,BufferKind::Vertex);});
            backend.validateSubmissionContext(); // Another owned device stays usable.
        }
        std::printf("PASS native presentation %s: all 1024 RGB codes/all 4 alpha codes, real copy events, swapchain transfer, Present=%s, initial copy=%s; hidden window, no scanout-equivalence claim\n",
                    hardware?"hardware":"WARP",accepted?"accepted":"occluded",immediatelyComplete?"complete":"pending");
    }
};
}
int main(int argc,char** argv) {
    try {
        const bool hardware=argc==2 && std::string(argv[1])=="--hardware";
        NativePresentationProbe::run(hardware);
        NativePresentationProbe::minimizeRestore(hardware,false);
        NativePresentationProbe::minimizeRestore(hardware,true);return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"Native presentation contract failure: %s\n",error.what());return 1;}
}
