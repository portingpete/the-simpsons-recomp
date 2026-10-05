#include "native_backend.h"
#include "d3d_call_stats.h"
#include "presentation_window.h"
#include <algorithm>
#include <cstdio>

namespace Simpsons::Graphics {
// Deliberately opaque to runtime receipts. A native query is not a guest SDK
// fence/token, and completing this copy says nothing about display acceptance.
class NativeCopySubmission {
    friend class NativeBackend;
    const NativeBackend* owner{};
    ComPtr<ID3D11Query> event;
    ComPtr<ID3D11Texture2D> sourceResource,destinationResource;
    std::shared_ptr<void> sourceOwner,destinationOwner;
    bool submitted{},complete{};
};
namespace {
constexpr ULONGLONG waitMilliseconds=5000;
void check(HRESULT result,const char* operation) {
    if(FAILED(result)) {
        char message[180];std::snprintf(message,sizeof(message),"Native presentation %s failed: 0x%08lX",operation,ULONG(result));
        throw Error(message);
    }
}
void noPredication(ID3D11DeviceContext* context) {
    ComPtr<ID3D11Predicate> predicate;BOOL value{};context->GetPredication(&predicate,&value);
    if(predicate) throw Error("Native front copy/event requires unpredicated resource operations");
}
ComPtr<ID3D11Query> eventQuery(ID3D11Device* device) {
    D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT,0};ComPtr<ID3D11Query> query;
    check(device->CreateQuery(&desc,&query),"completion query creation");return query;
}
bool eventComplete(ID3D11DeviceContext* context,ID3D11Query* query) {
    BOOL complete=FALSE;
    const HRESULT result=context->GetData(query,&complete,sizeof(complete),D3D11_ASYNC_GETDATA_DONOTFLUSH);
    check(result,"completion query");
    if(result==S_FALSE) return false;
    if(result!=S_OK || complete!=TRUE) throw Error("Native completion query returned an invalid event result");
    return true;
}
void boundedWait(const NativeBackend& backend,ID3D11DeviceContext* context,ID3D11Query* query) {
    // A Sleep(1) poll can consume an entire scheduler tick for each of the
    // frame's completion queries. This timer only paces polling; GetData is
    // still the sole evidence of GPU completion, with the same deadline.
    // https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw
    // https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-setwaitabletimerex
    struct PollTimer {
        HANDLE handle=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
        ~PollTimer(){if(handle)CloseHandle(handle);}
        void pause() const {
            if(!handle){Sleep(1);return;} // Older Windows retains the original correct wait.
            LARGE_INTEGER due{};due.QuadPart=-1000; // Relative 100 microseconds.
            if(!SetWaitableTimerEx(handle,&due,0,nullptr,nullptr,nullptr,0))
                throw Error("Native GPU polling timer could not be armed");
            if(WaitForSingleObject(handle,1000)!=WAIT_OBJECT_0)
                throw Error("Native GPU polling timer failed to signal");
        }
    };
    static thread_local PollTimer timer;
    const ULONGLONG start=GetTickCount64();
    // Short transfers may finish while a polling timer is being armed. Try
    // their real query for at most250us before sleeping, once per wait. This
    // changes only polling cadence: every attempt retains the owner/device
    // gate, GetData is still completion evidence, and the5s deadline remains.
    LARGE_INTEGER frequency{},spinStart{};
    bool spinning=QueryPerformanceFrequency(&frequency) && frequency.QuadPart>0 && QueryPerformanceCounter(&spinStart);
    bool flushed=false;
    for(;;) {
        backend.validateSubmissionContext();
        if(eventComplete(context,query)) {backend.validateSubmissionContext();return;}
        // Copies no longer Flush at submission. Submit queued commands once
        // before polling with DONOTFLUSH, so a still-unsubmitted event cannot
        // sit in the command buffer until the deadline.
        if(!flushed) {context->Flush();flushed=true;continue;}
        if(GetTickCount64()-start>=waitMilliseconds)
            throw Error("Native GPU completion timed out after 5000 ms; submitted work cannot be rolled back");
        if(spinning) {
            LARGE_INTEGER current{};
            if(QueryPerformanceCounter(&current) && current.QuadPart>=spinStart.QuadPart &&
               current.QuadPart-spinStart.QuadPart<frequency.QuadPart/4000) {
                YieldProcessor();continue;
            }
            spinning=false;
        }
        timer.pause();
    }
}
bool viewOf(ID3D11View* view,ID3D11Resource* resource) {
    if(!view) return false;
    ComPtr<ID3D11Resource> actual;view->GetResource(&actual);return actual.Get()==resource;
}
}

const D3D11_TEXTURE2D_DESC* NativeBackend::provenDescriptor(ID3D11Texture2D* texture,TextureDescriptorProof& proof) const {
    if(!texture)return nullptr;
    if(proof.device && proof.device==device.Get() && proof.texture.Get()==texture)return &proof.desc;
    ComPtr<ID3D11Device> owner;texture->GetDevice(&owner);
    if(owner.Get()!=device.Get())return nullptr;
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    proof.texture=texture;proof.device=device.Get();proof.desc=desc;
    return &proof.desc;
}
const D3D11_TEXTURE2D_DESC* NativeBackend::attachmentDescriptor(const RenderTarget& target) const {
    return provenDescriptor(target.texture.Get(),target.descriptorProof);
}
void NativeBackend::validateFrontTarget(const std::shared_ptr<RenderTarget>& target) const {
    if(!target || !target->texture || !target->view || !target->sampledView)
        throw Error("Native front copy requires an owned target resource and views");
    {
        const auto& proof=target->frontProof;
        if(proof.device && proof.device==device.Get() && proof.texture.Get()==target->texture.Get() &&
           proof.view.Get()==target->view.Get() && proof.sampledView.Get()==target->sampledView.Get() &&
           proof.width==target->width && proof.height==target->height && proof.rowBytes==target->rowBytes &&
           proof.physicalWidth==target->physicalWidth && proof.physicalHeight==target->physicalHeight && proof.format==target->format)
            return;
    }
    ComPtr<ID3D11Device> actual;target->texture->GetDevice(&actual);
    if(actual.Get()!=device.Get()) throw Error("Native front target belongs to another graphics device");
    D3D11_TEXTURE2D_DESC desc{};target->texture->GetDesc(&desc);
    if(target->format!=TargetFormat::RGB10A2 || !target->width || !target->height ||
       target->width>16384 || target->height>16384 || target->rowBytes!=target->pixelWidth()*4 ||
       desc.Width!=target->pixelWidth() || desc.Height!=target->pixelHeight() || desc.Format!=DXGI_FORMAT_R10G10B10A2_UNORM ||
       desc.MipLevels!=1 || desc.ArraySize!=1 || desc.SampleDesc.Count!=1 || desc.SampleDesc.Quality ||
       desc.Usage!=D3D11_USAGE_DEFAULT || desc.BindFlags!=(D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE) ||
       desc.CPUAccessFlags || desc.MiscFlags)
        throw Error("Native front target exceeds the exact packed RGB10A2 single-sample profile");
    D3D11_RENDER_TARGET_VIEW_DESC rtv{};target->view->GetDesc(&rtv);
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};target->sampledView->GetDesc(&srv);
    if(!viewOf(target->view.Get(),target->texture.Get()) || !viewOf(target->sampledView.Get(),target->texture.Get()) ||
       rtv.Format!=desc.Format || rtv.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D || rtv.Texture2D.MipSlice ||
       srv.Format!=desc.Format || srv.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D ||
       srv.Texture2D.MostDetailedMip || srv.Texture2D.MipLevels!=1)
        throw Error("Native front target view does not match its packed backing");
    // Publish the proof only after the full check succeeded.
    target->frontProof={target->texture,target->view,target->sampledView,device.Get(),target->width,target->height,
        target->rowBytes,target->physicalWidth,target->physicalHeight,target->format};
}
void NativeBackend::validateFrontCopy(const std::shared_ptr<RenderTarget>& source,
                                      const std::shared_ptr<RenderTarget>& front) const {
    validateSubmissionContext();validateFrontTarget(source);validateFrontTarget(front);noPredication(context.Get());
    if(source->texture.Get()==front->texture.Get()) throw Error("Native front copy source and destination must be distinct");
    if(source->width!=front->width || source->height!=front->height || source->pixelWidth()!=front->pixelWidth() || source->pixelHeight()!=front->pixelHeight()) throw Error("Native front copy extents differ");
    ComPtr<ID3D11RenderTargetView> actual;context->OMGetRenderTargets(1,&actual,nullptr);
    if(actual.Get()!=source->view.Get()) throw Error("Native front copy source is not actual OM color zero");
    requireOwner();
}

void NativeBackend::validateDepthCopyTarget(const std::shared_ptr<DepthTarget>& target) const {
    if(!target || !target->texture || !target->view || !target->depthView || !target->stencilView)
        throw Error("Native depth copy requires owned depth/stencil storage and views");
    {
        const auto& proof=target->copyProof;
        if(proof.device && proof.device==device.Get() && proof.texture.Get()==target->texture.Get() &&
           proof.view.Get()==target->view.Get() && proof.depthView.Get()==target->depthView.Get() &&
           proof.stencilView.Get()==target->stencilView.Get() && proof.width==target->width && proof.height==target->height &&
           proof.physicalWidth==target->physicalWidth && proof.physicalHeight==target->physicalHeight)
            return;
    }
    ComPtr<ID3D11Device> actual;target->texture->GetDevice(&actual);
    if(actual.Get()!=device.Get()) throw Error("Native depth copy target belongs to another device");
    D3D11_TEXTURE2D_DESC desc{};target->texture->GetDesc(&desc);
    if(!target->width || !target->height || desc.Width!=target->storageWidth() || desc.Height!=target->storageHeight() ||
       desc.Format!=DXGI_FORMAT_R32G8X24_TYPELESS || desc.MipLevels!=1 || desc.ArraySize!=1 ||
       desc.SampleDesc.Count!=1 || desc.SampleDesc.Quality || desc.Usage!=D3D11_USAGE_DEFAULT ||
       desc.BindFlags!=(D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE) || desc.CPUAccessFlags || desc.MiscFlags)
        throw Error("Native depth copy target exceeds the exact single-sample depth/stencil profile");
    D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};target->view->GetDesc(&dsv);
    D3D11_SHADER_RESOURCE_VIEW_DESC depth{},stencil{};target->depthView->GetDesc(&depth);target->stencilView->GetDesc(&stencil);
    if(!viewOf(target->view.Get(),target->texture.Get()) || !viewOf(target->depthView.Get(),target->texture.Get()) ||
       !viewOf(target->stencilView.Get(),target->texture.Get()) || dsv.Format!=DXGI_FORMAT_D32_FLOAT_S8X24_UINT ||
       dsv.ViewDimension!=D3D11_DSV_DIMENSION_TEXTURE2D || dsv.Flags || dsv.Texture2D.MipSlice ||
       depth.Format!=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS || stencil.Format!=DXGI_FORMAT_X32_TYPELESS_G8X24_UINT ||
       depth.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D || stencil.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D ||
       depth.Texture2D.MostDetailedMip || stencil.Texture2D.MostDetailedMip || depth.Texture2D.MipLevels!=1 || stencil.Texture2D.MipLevels!=1)
        throw Error("Native depth copy views do not match their backing");
    // Publish the proof only after the full check succeeded.
    target->copyProof={target->texture,target->view,target->depthView,target->stencilView,device.Get(),
        target->width,target->height,target->physicalWidth,target->physicalHeight};
}
void NativeBackend::validateDepthCopy(const std::shared_ptr<DepthTarget>& source,const std::shared_ptr<DepthTarget>& destination) const {
    validateSubmissionContext();validateDepthCopyTarget(source);validateDepthCopyTarget(destination);noPredication(context.Get());
    if(source->texture.Get()==destination->texture.Get()) throw Error("Native depth copy source and destination must be distinct");
    if(source->width!=destination->width || source->height!=destination->height || source->storageWidth()!=destination->storageWidth() || source->storageHeight()!=destination->storageHeight())
        throw Error("Native depth copy requires identical full-subresource extents; partial depth transfer is unported");
    ComPtr<ID3D11DepthStencilView> actual;context->OMGetRenderTargets(0,nullptr,&actual);
    if(actual.Get()!=source->view.Get()) throw Error("Native depth copy source is not the actual selected depth target");
    requireOwner();
}
std::shared_ptr<NativeCopySubmission> NativeBackend::copyDepth(const std::shared_ptr<DepthTarget>& source,const std::shared_ptr<DepthTarget>& destination) {
    validateDepthCopy(source,destination);
    return submitCopy(source,destination,source->texture.Get(),destination->texture.Get());
}
std::shared_ptr<NativeCopySubmission> NativeBackend::submitCopy(const std::shared_ptr<void>& sourceOwner,
    const std::shared_ptr<void>& destinationOwner,ID3D11Texture2D* source,ID3D11Texture2D* destination) {
    flushIm2D();
    retireCopies();
    auto submission=std::make_shared<NativeCopySubmission>();
    submission->owner=this;submission->event=eventQuery(device.Get());
    submission->sourceResource=source;submission->destinationResource=destination;
    submission->sourceOwner=sourceOwner;submission->destinationOwner=destinationOwner;
    pendingCopies.push_back(submission); // All potentially allocating work precedes submission.
    // Same storage format and full extent: preserves all color codes or both
    // depth/stencil components. No shader, rounding, clear or binding changes.
    context->CopyResource(destination,source);
    context->End(submission->event.Get());submission->submitted=true;
    // No per-copy Flush: with a threaded driver each Flush synchronizes with its
    // submission thread. Present, a full command buffer, or boundedWait submit
    // the queued copy/event; the GPU still executes them in command order.
    requireOwner(); // A failure here retains the submission in pendingCopies.
    return submission;
}
std::shared_ptr<NativeCopySubmission> NativeBackend::copyFront(const std::shared_ptr<RenderTarget>& source,
                                                              const std::shared_ptr<RenderTarget>& front) {
    validateFrontCopy(source,front);
    return submitCopy(source,front,source->texture.Get(),front->texture.Get());
}
bool NativeBackend::pollCopy(const std::shared_ptr<NativeCopySubmission>& submission) {
    validateSubmissionContext();
    if(!submission || submission->owner!=this || !submission->submitted || !submission->event ||
       !submission->sourceResource || !submission->destinationResource)
        throw Error("Native copy receipt is missing or belongs to another backend");
    ComPtr<ID3D11Device> actual;submission->event->GetDevice(&actual);
    if(actual.Get()!=device.Get()) throw Error("Native copy receipt event belongs to another device");
    if(!submission->complete) submission->complete=eventComplete(context.Get(),submission->event.Get());
    requireOwner();return submission->complete;
}
void NativeBackend::retireCopies() {
    // Event queries on this single immediate context signal in submission
    // order: once one is still pending, every later one is too. Stop polling
    // there instead of querying the driver for every pending copy.
    for(const auto& copy:pendingCopies) if(!pollCopy(copy)) break;
    std::erase_if(pendingCopies,[](const auto& copy){return copy->complete;});
}
bool NativeBackend::copyComplete(const std::shared_ptr<NativeCopySubmission>& submission) {
    const bool complete=pollCopy(submission);
    std::erase_if(pendingCopies,[](const auto& copy){return copy->complete;});
    return complete;
}
void NativeBackend::waitCopy(const std::shared_ptr<NativeCopySubmission>& submission) {
    if(copyComplete(submission)) return;
    boundedWait(*this,context.Get(),submission->event.Get());
    if(!copyComplete(submission)) throw Error("Native copy event lost its proven completion");
}
void NativeBackend::waitIdle() {
    flushIm2D();
    validateSubmissionContext();noPredication(context.Get());
    check(device->GetDeviceRemovedReason(),"device availability before retirement");
    auto event=eventQuery(device.Get());
    context->End(event.Get());context->Flush();
    boundedWait(*this,context.Get(),event.Get());
    retireCopies();
    check(device->GetDeviceRemovedReason(),"device availability after retirement");
    retireRecordingPayloads();
    if(!pendingCopies.empty()) throw Error("Native idle event completed before an earlier copy event");
    // Covers all earlier immediate-context draws/buffer uses, not scanout.
}

void NativeBackend::validateFrontPresentation(const std::shared_ptr<RenderTarget>& front) const {
    validateSubmissionContext();validateFrontTarget(front);noPredication(context.Get());
    if(!swapChain || !backbuffer) throw Error("Native front presentation has no attached swapchain");
    DXGI_SWAP_CHAIN_DESC1 desc{};check(swapChain->GetDesc1(&desc),"swapchain descriptor query");
    HWND window{};check(swapChain->GetHwnd(&window),"swapchain window query");
    BOOL fullscreen{};
    check(swapChain->GetFullscreenState(&fullscreen,nullptr),"fullscreen state query");
    const uint32_t expectedWidth=presentationWidth?presentationWidth:front->width;
    const uint32_t expectedHeight=presentationHeight?presentationHeight:front->height;
    const bool windowMatches=presentationWindowMatches(window,expectedWidth,expectedHeight);
    if(fullscreen || !windowMatches) {
        // Keep the failed gate and a fresh Win32 snapshot separate: a restore
        // can finish between them. These observations explain the rejection;
        // they do not authorize a resized, foreign or invalid window.
        DWORD process{};
        const DWORD windowThread=GetWindowThreadProcessId(window,&process);
        const BOOL valid=IsWindow(window),iconicBefore=IsIconic(window);
        RECT client{},outer{};
        const BOOL clientRead=GetClientRect(window,&client),outerRead=GetWindowRect(window,&outer);
        const BOOL iconicAfter=IsIconic(window),visible=IsWindowVisible(window),zoomed=IsZoomed(window);
        const LONG_PTR style=GetWindowLongPtrW(window,GWL_STYLE);
        std::fprintf(stderr,"[NATIVE PRESENT WINDOW REJECTED] hwnd=%p expected=%ux%u gate_matches=%u dxgi_fullscreen=%u valid=%u owner_process=%lu current_process=%lu owner_thread=%lu client_read=%u client=(%ld,%ld,%ld,%ld) outer_read=%u outer=(%ld,%ld,%ld,%ld) iconic=%u/%u visible=%u zoomed=%u style=%llX swapchain=%ux%u source=%ux%u logical=%ux%u\n",
            static_cast<void*>(window),expectedWidth,expectedHeight,unsigned(windowMatches),unsigned(fullscreen!=FALSE),unsigned(valid!=FALSE),
            ULONG(process),ULONG(GetCurrentProcessId()),ULONG(windowThread),unsigned(clientRead!=FALSE),client.left,client.top,client.right,client.bottom,
            unsigned(outerRead!=FALSE),outer.left,outer.top,outer.right,outer.bottom,unsigned(iconicBefore!=FALSE),unsigned(iconicAfter!=FALSE),
            unsigned(visible!=FALSE),unsigned(zoomed!=FALSE),static_cast<unsigned long long>(style),desc.Width,desc.Height,
            presentationSourceWidth,presentationSourceHeight,presentationLogicalWidth,presentationLogicalHeight);
        throw Error("Native front presentation window state or client extent changed");
    }
    const uint32_t samples=antialiasingMode==Antialiasing::SSAA4x?2:1;
    const auto canvas=presentationExtent(front->pixelWidth()/samples,front->pixelHeight()/samples,
        presentationWidth?presentationWidth:presentationLogicalWidth,presentationHeight?presentationHeight:presentationLogicalHeight,
        presentationLogicalWidth,presentationLogicalHeight);
    if(front->pixelWidth()%samples || front->pixelHeight()%samples ||
       front->pixelWidth()/samples!=presentationSourceWidth || front->pixelHeight()/samples!=presentationSourceHeight ||
       desc.Width!=canvas[0] || desc.Height!=canvas[1] || desc.Format!=DXGI_FORMAT_R10G10B10A2_UNORM ||
       desc.Stereo || desc.SampleDesc.Count!=1 || desc.SampleDesc.Quality || desc.BufferCount!=2 ||
       desc.BufferUsage!=DXGI_USAGE_RENDER_TARGET_OUTPUT || desc.SwapEffect!=DXGI_SWAP_EFFECT_FLIP_DISCARD ||
       desc.Scaling!=DXGI_SCALING_STRETCH || desc.AlphaMode!=DXGI_ALPHA_MODE_IGNORE ||
       desc.Flags!=(tearingEnabled?DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING:0u))
        throw Error("Native front presentation swapchain is not the matching RGB10A2 alpha-ignore profile");
    ComPtr<ID3D11Device> actualDevice;check(swapChain->GetDevice(IID_PPV_ARGS(&actualDevice)),"swapchain device query");
    if(actualDevice.Get()!=device.Get()) throw Error("Native presentation swapchain belongs to another device");
    ComPtr<ID3D11Texture2D> target;check(swapChain->GetBuffer(0,IID_PPV_ARGS(&target)),"presentation buffer query");
    D3D11_TEXTURE2D_DESC actual{};target->GetDesc(&actual);
    if(actual.Width!=desc.Width || actual.Height!=desc.Height || actual.Format!=desc.Format ||
       actual.ArraySize!=1 || actual.MipLevels!=1 || actual.SampleDesc.Count!=1 || actual.SampleDesc.Quality ||
       actual.Usage!=D3D11_USAGE_DEFAULT || actual.CPUAccessFlags || !viewOf(backbuffer.Get(),target.Get()) ||
       front->texture.Get()==target.Get())
        throw Error("Native presentation buffer backing differs from its descriptor");
    // Flip Present can unbind its backbuffer. Reject that pre-existing use,
    // instead of changing engine bindings or restoring hazard-nulled guesses.
    bool bound=false;
    std::array<ID3D11RenderTargetView*,8> colors{};context->OMGetRenderTargets(8,colors.data(),nullptr);
    for(auto* view:colors) {bound=bound || viewOf(view,target.Get());if(view) view->Release();}
    auto reads=[&](auto getter) {
        std::array<ID3D11ShaderResourceView*,128> views{};(context.Get()->*getter)(0,128,views.data());
        for(auto* view:views) {bound=bound || viewOf(view,target.Get());if(view) view->Release();}
    };
    reads(&ID3D11DeviceContext::PSGetShaderResources);reads(&ID3D11DeviceContext::VSGetShaderResources);
    reads(&ID3D11DeviceContext::GSGetShaderResources);reads(&ID3D11DeviceContext::HSGetShaderResources);
    reads(&ID3D11DeviceContext::DSGetShaderResources);reads(&ID3D11DeviceContext::CSGetShaderResources);
    if(bound) throw Error("Native front presentation would disturb a bound swapchain buffer");
    requireOwner();
}
std::shared_ptr<NativeCopySubmission> NativeBackend::submitFrontPresentation(const std::shared_ptr<RenderTarget>& front) {
    validateFrontPresentation(front);
    const auto output=resolveAntialiasing(front);
    const auto extent=presentationExtent(output->pixelWidth(),output->pixelHeight(),
        presentationWidth?presentationWidth:presentationLogicalWidth,presentationHeight?presentationHeight:presentationLogicalHeight,
        presentationLogicalWidth,presentationLogicalHeight);
    auto copied=output;
    if(extent[0]!=output->pixelWidth()||extent[1]!=output->pixelHeight()) {
        if(!presentationCanvas||presentationCanvas->pixelWidth()!=extent[0]||presentationCanvas->pixelHeight()!=extent[1])
            presentationCanvas=createTarget(extent[0],extent[1],TargetFormat::RGB10A2);
        clearTarget(presentationCanvas,{0,0,0,1});
        context->CopySubresourceRegion(presentationCanvas->texture.Get(),0,
            (extent[0]-output->pixelWidth())/2,(extent[1]-output->pixelHeight())/2,0,output->texture.Get(),0,nullptr);
        copied=presentationCanvas;
    }
    ComPtr<ID3D11Texture2D> target;check(swapChain->GetBuffer(0,IID_PPV_ARGS(&target)),"presentation copy target query");
    auto transfer=submitCopy(copied,nullptr,copied->texture.Get(),target.Get());
    lastPresentedFront=output;
    return transfer;
}
void NativeBackend::configureVideoPresentation(bool vsync,uint32_t width,uint32_t height) {
    validateSubmissionContext();
    if(!width||!height||width>16384||height>16384)throw Error("Invalid native output extent");
    if(swapChain) {
        const auto extent=presentationExtent(presentationSourceWidth,presentationSourceHeight,width,height,
            presentationLogicalWidth,presentationLogicalHeight);
        DXGI_SWAP_CHAIN_DESC1 desc{};check(swapChain->GetDesc1(&desc),"output aspect descriptor");
        if(desc.Width!=extent[0]||desc.Height!=extent[1]) {
            waitIdle();
            ComPtr<ID3D11Resource> resource;backbuffer->GetResource(&resource);
            bool bound=false;
            std::array<ID3D11RenderTargetView*,8> colors{};context->OMGetRenderTargets(8,colors.data(),nullptr);
            for(auto* view:colors) {bound|=viewOf(view,resource.Get());if(view)view->Release();}
            auto reads=[&](auto getter) {
                std::array<ID3D11ShaderResourceView*,128> views{};(context.Get()->*getter)(0,128,views.data());
                for(auto* view:views) {bound|=viewOf(view,resource.Get());if(view)view->Release();}
            };
            reads(&ID3D11DeviceContext::PSGetShaderResources);reads(&ID3D11DeviceContext::VSGetShaderResources);
            reads(&ID3D11DeviceContext::GSGetShaderResources);reads(&ID3D11DeviceContext::HSGetShaderResources);
            reads(&ID3D11DeviceContext::DSGetShaderResources);reads(&ID3D11DeviceContext::CSGetShaderResources);
            if(bound)throw Error("Native output aspect change would disturb a bound swapchain buffer");
            resource.Reset();backbuffer.Reset();
            check(swapChain->ResizeBuffers(desc.BufferCount,extent[0],extent[1],desc.Format,desc.Flags),"output aspect resize");
            ComPtr<ID3D11Texture2D> target;check(swapChain->GetBuffer(0,IID_PPV_ARGS(&target)),"resized output buffer");
            check(device->CreateRenderTargetView(target.Get(),nullptr,&backbuffer),"resized output view");
        }
    }
    vsyncEnabled=vsync;presentationWidth=width;presentationHeight=height;
}
std::shared_ptr<NativeCopySubmission> NativeBackend::presentFrontQueued(const std::shared_ptr<RenderTarget>& front,bool& accepted) {
    if(d3dCallStatsRequested()) {
        static uint64_t presents=0;
        if(++presents%600==0)dumpD3DCallStats(600,"600-frame window");
    }
    auto transfer=submitFrontPresentation(front);
    // Queue the DXGI presentation after the front->swapchain copy. CopyResource
    // is queued on the single-owner immediate context, so submission order
    // preserves source->front->swapchain. The returned receipt proves nothing
    // until it completes; the caller decides when it must wait.
    accepted=present(); // Configured sync interval; false only for DXGI_STATUS_OCCLUDED.
    return transfer;
}
bool NativeBackend::presentFront(const std::shared_ptr<RenderTarget>& front) {
    bool accepted=false;
    auto transfer=presentFrontQueued(front,accepted);
    waitCopy(transfer); // Return proves real transfer completion; DXGI owns its own display buffers.
    return accepted;
}
}
