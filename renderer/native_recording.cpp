#include "native_backend.h"
#include "runtime/stall_profiler.h"
#include <algorithm>
#include <cstdio>
#include <limits>

namespace Simpsons::Graphics {
namespace {
// Host snapshot policy, independent of the original 0x3000/0x9000 GPU command
// allocation. The original cache still accounts the actual copied bytes and
// applies its 15,000,000-byte aggregate quota; no GPU byte estimate is invented.
constexpr uint32_t recordingOwnedDataCapacityBytes=1u<<20;
thread_local bool recordingCallbackActive=false;
void noRecordingCallback() {
    if(recordingCallbackActive) throw Error("Native recording callback cannot reenter recording lifecycle");
}
struct RecordingCallbackScope {
    RecordingCallbackScope(){noRecordingCallback();recordingCallbackActive=true;}
    ~RecordingCallbackScope(){recordingCallbackActive=false;}
};
void recordingCheck(HRESULT result,const char* operation) {
    if(FAILED(result)) {
        char reason[192];std::snprintf(reason,sizeof(reason),"Native recording %s failed: 0x%08lX",operation,ULONG(result));
        throw Error(reason);
    }
}
template<class T> void recordingDevice(T* resource,ID3D11Device* expected) {
    if(!resource)return;
    ComPtr<ID3D11Device> actual;resource->GetDevice(&actual);
    if(actual.Get()!=expected)throw Error("Native recording resource belongs to another graphics device");
}
bool inherits(const NativeRecordingMask& mask) {
    return std::any_of(mask.begin(),mask.end(),[](uint8_t byte){return byte!=0;});
}
void recordingMasks(uint32_t flags,const NativeRecordingMask& input,const NativeRecordingMask& output) {
    if(flags!=0 && flags!=4)throw Error("Native recording flags must be zero or four");
    for(size_t i=16;i<input.size();++i)
        if(input[i] || output[i])throw Error("Native recording supports only float-register inheritance masks");
}

// These are actual native Get* references, never guessed logical engine state.
// Only the original begin seed is copied. Geometry, constant buffers, samplers
// and fixed state must be supplied by the qualified recorded draw operation.
struct RecordingSeed {
    ComPtr<ID3D11RenderTargetView> color;
    ComPtr<ID3D11DepthStencilView> depth;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
    std::array<D3D11_RECT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors{};
    UINT viewportCount{},scissorCount{};

    static RecordingSeed capture(ID3D11DeviceContext* source,ID3D11Device* device,D3D_FEATURE_LEVEL level) {
        RecordingSeed seed;
        std::array<ID3D11RenderTargetView*,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rawColors{};
        source->OMGetRenderTargets(UINT(rawColors.size()),rawColors.data(),&seed.depth);
        std::array<ComPtr<ID3D11RenderTargetView>,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> colors;
        for(size_t i=0;i<colors.size();++i)colors[i].Attach(rawColors[i]);
        for(size_t i=1;i<colors.size();++i)
            if(colors[i])throw Error("Native recording begin does not support additional color attachments");
        seed.color=colors[0];
        // FL11.1 exposes 64 UAV slots; checking only the first eight would miss
        // unsupported output state on that device profile.
        std::array<ID3D11UnorderedAccessView*,64> rawUavs{};
        const UINT uavCount=level>=D3D_FEATURE_LEVEL_11_1?64u:8u;
        source->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,uavCount,rawUavs.data());
        bool hasUav=false;
        for(auto* view:rawUavs)if(view){hasUav=true;view->Release();}
        if(hasUav)throw Error("Native recording begin does not support output UAV bindings");

        std::array<ID3D11ClassInstance*,D3D11_SHADER_MAX_INTERFACES> classes{};
        UINT classCount=UINT(classes.size());
        source->VSGetShader(&seed.vertex,classes.data(),&classCount);
        for(auto* instance:classes)if(instance)instance->Release();
        if(classCount)throw Error("Native recording begin does not support vertex shader class instances");
        classes.fill(nullptr);classCount=UINT(classes.size());
        source->PSGetShader(&seed.pixel,classes.data(),&classCount);
        for(auto* instance:classes)if(instance)instance->Release();
        if(classCount)throw Error("Native recording begin does not support pixel shader class instances");
        seed.viewportCount=UINT(seed.viewports.size());source->RSGetViewports(&seed.viewportCount,seed.viewports.data());
        seed.scissorCount=UINT(seed.scissors.size());source->RSGetScissorRects(&seed.scissorCount,seed.scissors.data());
        if(seed.viewportCount>seed.viewports.size() || seed.scissorCount>seed.scissors.size())
            throw Error("Native recording begin has invalid viewport/scissor counts");
        recordingDevice(seed.color.Get(),device);recordingDevice(seed.depth.Get(),device);
        recordingDevice(seed.vertex.Get(),device);recordingDevice(seed.pixel.Get(),device);
        return seed;
    }
    void bind(ID3D11DeviceContext* destination) const {
        auto* target=color.Get();destination->OMSetRenderTargets(1,&target,depth.Get());
        destination->RSSetViewports(viewportCount,viewportCount?viewports.data():nullptr);
        destination->RSSetScissorRects(scissorCount,scissorCount?scissors.data():nullptr);
        destination->VSSetShader(vertex.Get(),nullptr,0);destination->PSSetShader(pixel.Get(),nullptr,0);
    }
};
}

// Entire implementation stays opaque. The backend address is only compared as
// an owner identity, never dereferenced here or during destruction. Retaining
// the device prevents a replacement backend at the same address from acquiring
// the same COM device identity while this live owner remains.
class NativeRecordingContext {
    friend class NativeBackend;
    const NativeBackend* owner{};
    DWORD thread{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> deferred;
    // Immutable deferred-identity proof: validated deferred+device pair with
    // strong refs pinning both objects against pointer ABA. Assigned only at
    // creation after the full GetDevice/type/flags checks succeed, and cleared
    // at explicit release alongside the actual resources. Deferred COM fields
    // are otherwise assigned once at creation, so a strong-identity hit skips
    // only the repeated GetDevice; owner/thread/released/device/cheap
    // type/flag/alias checks still run on every validation.
    ComPtr<ID3D11DeviceContext> provenDeferred;
    ComPtr<ID3D11Device> provenDevice;
    bool identityProven=false;
    std::weak_ptr<NativeRecordingPayload> active;
    // Kept separately from weak ownership: abandoning an active payload must
    // not allow its unfinished commands to be silently reused by another begin.
    bool activeSession=false;
    NativeRecordingContext()=default;
public:
    ~NativeRecordingContext()=default;
    NativeRecordingContext(const NativeRecordingContext&)=delete;
    NativeRecordingContext& operator=(const NativeRecordingContext&)=delete;
};

class NativeRecordingPayload {
    friend class NativeBackend;
    const NativeBackend* owner{};
    DWORD thread{};
    ComPtr<ID3D11Device> device;
    std::shared_ptr<NativeRecordingContext> recording;
    // Immutable ownership proofs for resources installed exactly once per
    // lifetime phase. Strong refs pin each proven object against pointer ABA.
    // commandsProven is published by finishRecordingPayload after its GetDevice
    // check; completionProven is published by executeRecordingPayload after the
    // new query's ownership check before first GPU mutation. Both skip only
    // the repeated GetDevice in validateRecordingPayload; lifecycle,
    // accounting, null sealed-command, released and current-device checks
    // still run every time. Every proof resets with its actual resource
    // (retire/release paths below).
    ComPtr<ID3D11CommandList> provenCommands;
    bool commandsProven=false;
    ComPtr<ID3D11Query> provenCompletion;
    bool completionProven=false;
    NativeRecordingReceipt receipt{};
    RecordingSeed seed;
    struct Draw {
        size_t offset{},bytes{};
        std::shared_ptr<void> owner;
        NativeRecordingPrepare prepare;
    };
    // Accounting is exactly data.size(), not sizeof(Draw), GPU allocation
    // size, command-list bytes, nor any fabricated original SDK receipt.
    std::vector<uint8_t> data;
    std::vector<Draw> draws;
    ComPtr<ID3D11CommandList> commands;
    ComPtr<ID3D11Query> completion;
    bool inFlight=false;
    NativeRecordingPayload()=default;
public:
    ~NativeRecordingPayload()=default;
    NativeRecordingPayload(const NativeRecordingPayload&)=delete;
    NativeRecordingPayload& operator=(const NativeRecordingPayload&)=delete;
};

ID3D11DeviceContext* NativeBackend::checkedRecordingContext(const std::shared_ptr<NativeRecordingContext>& recording) const {
    validateSubmissionContext();
    noRecordingCallback();
    if(!recording) throw Error("Native recording context owner is missing");
    if(recording->owner!=this || recording->thread!=owner)
        throw Error("Native recording context belongs to another backend owner");
    if(!recording->deferred || !recording->device)
        throw Error("Native recording context was explicitly released");
    if(recording->device.Get()!=device.Get())
        throw Error("Native recording context belongs to another graphics device");
    // Cheap checks run on every path: aliasing against the current immediate
    // context plus immutable type/creation flags (plain getters, no AddRef).
    // Only the AddRef/Release GetDevice round-trip is skipped on a proven hit.
    const bool proofHit=recording->identityProven && recording->provenDeferred.Get()==recording->deferred.Get() &&
        recording->provenDevice.Get()==recording->device.Get() && recording->provenDevice.Get()==device.Get();
    if(!proofHit) {
        ComPtr<ID3D11Device> actual;recording->deferred->GetDevice(&actual);
        if(actual.Get()!=device.Get())
            throw Error("Native recording context has invalid device, type or creation flags");
        if(recording->deferred.Get()==context.Get() ||
           recording->deferred->GetType()!=D3D11_DEVICE_CONTEXT_DEFERRED || recording->deferred->GetContextFlags()!=0)
            throw Error("Native recording context has invalid device, type or creation flags");
        requireOwner();
        // Publish proof only after the full checks succeed.
        recording->provenDeferred=recording->deferred;
        recording->provenDevice=recording->device;
        recording->identityProven=true;
    } else {
        if(recording->deferred.Get()==context.Get() ||
           recording->deferred->GetType()!=D3D11_DEVICE_CONTEXT_DEFERRED || recording->deferred->GetContextFlags()!=0)
            throw Error("Native recording context has invalid device, type or creation flags");
        requireOwner();
    }
    return recording->deferred.Get();
}

std::shared_ptr<NativeRecordingContext> NativeBackend::createRecordingContext() {
    validateSubmissionContext();
    noRecordingCallback();
    auto recording=std::shared_ptr<NativeRecordingContext>(new NativeRecordingContext);
    recording->owner=this;recording->thread=owner;recording->device=device;
    // ContextFlags is reserved and must be zero. All ownership allocation occurs
    // before the native call; a failed create/validation releases partial COM
    // ownership through RAII and never returns a successful recording.
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createdeferredcontext
    const HRESULT result=device->CreateDeferredContext(0,&recording->deferred);
    if(result!=S_OK) {
        char reason[160];std::snprintf(reason,sizeof(reason),"Native deferred-context creation failed: 0x%08lX",ULONG(result));
        throw Error(reason);
    }
    validateRecordingContext(recording);
    return recording;
}

void NativeBackend::validateRecordingContext(const std::shared_ptr<NativeRecordingContext>& recording) const {
    (void)checkedRecordingContext(recording);
}

void NativeBackend::releaseRecordingContext(const std::shared_ptr<NativeRecordingContext>& recording) {
    (void)checkedRecordingContext(recording); // All predictable rejection precedes mutation.
    if(recording->activeSession)throw Error("Native recording context still has an active payload");
    retireRecordingPayloads();
    // Completed payloads are independent of the deferred context's lifetime.
    // An empty context still releases without Finish/Execute or immediate edits.
    recording->deferred.Reset();
    recording->device.Reset();
    recording->provenDeferred.Reset();
    recording->provenDevice.Reset();
    recording->identityProven=false;
    // Keep owner identity so aliases produce an explicit released-state failure.
}

void NativeBackend::validateRecordingPayloadItem(const std::shared_ptr<NativeRecordingPayload>& payload) const {
    if(!payload)throw Error("Native recording payload owner is missing");
    if(payload->owner!=this || payload->thread!=owner)
        throw Error("Native recording payload belongs to another backend owner");
    if(payload->receipt.state==NativeRecordingPayloadState::Released || !payload->device)
        throw Error("Native recording payload was explicitly released");
    if(payload->device.Get()!=device.Get())throw Error("Native recording payload belongs to another graphics device");
    if(!payload->recording || (payload->receipt.capacityBytes!=0x3000 && payload->receipt.capacityBytes!=0x9000) ||
       payload->receipt.ownedDataCapacityBytes!=recordingOwnedDataCapacityBytes ||
       payload->data.size()!=payload->receipt.ownedDataBytes || payload->data.size()>payload->receipt.ownedDataCapacityBytes ||
       payload->draws.size()!=payload->receipt.recordedDraws)
        throw Error("Native recording payload ownership/accounting changed");
    if(payload->receipt.state==NativeRecordingPayloadState::Sealed && !payload->commands)
        throw Error("Native recording payload has no finished command list");
    // Immutable ownership proofs skip only the AddRef/Release GetDevice
    // round-trips. A strong-identity hit means the exact proven object is
    // still installed and its device was already verified; any pointer change
    // (including replacement with another same-device object) forces the full
    // check again. Null commands/completion need no GetDevice either way.
    if(payload->commands) {
        if(payload->commandsProven && payload->provenCommands.Get()==payload->commands.Get()) {
            // Proven hit: device already verified at installation.
        } else {
            recordingDevice(payload->commands.Get(),device.Get());
            requireOwner();
            payload->provenCommands=payload->commands;
            payload->commandsProven=true;
        }
    }
    if(payload->completion) {
        if(payload->completionProven && payload->provenCompletion.Get()==payload->completion.Get()) {
            // Proven hit: device already verified at installation.
        } else {
            recordingDevice(payload->completion.Get(),device.Get());
            requireOwner();
            payload->provenCompletion=payload->completion;
            payload->completionProven=true;
        }
    }
}

void NativeBackend::validateRecordingPayload(const std::shared_ptr<NativeRecordingPayload>& payload) const {
    validateSubmissionContext();noRecordingCallback();
    validateRecordingPayloadItem(payload);
    requireOwner();
}

void NativeBackend::recordingPayloadReceipts(
    std::span<const std::shared_ptr<NativeRecordingPayload>> payloads,
    std::span<NativeRecordingReceipt> receipts) const {
    validateSubmissionContext();noRecordingCallback();
    if(payloads.size()!=receipts.size())
        throw Error("Native recording receipt batch length mismatch");
    if(payloads.size()>2000)
        throw Error("Native recording receipt batch exceeds its bounded capacity");
    for(size_t i=0;i<payloads.size();++i) {
        validateRecordingPayloadItem(payloads[i]);
        receipts[i]=payloads[i]->receipt;
    }
    requireOwner();
}

std::shared_ptr<NativeRecordingPayload> NativeBackend::allocateRecordingPayload(
    const std::shared_ptr<NativeRecordingContext>& recording,uint32_t capacityBytes) {
    (void)checkedRecordingContext(recording);
    if(capacityBytes!=0x3000 && capacityBytes!=0x9000)
        throw Error("Native recording capacity must be 0x3000 or 0x9000");
    retireRecordingPayloads();
    auto payload=std::shared_ptr<NativeRecordingPayload>(new NativeRecordingPayload);
    payload->owner=this;payload->thread=owner;payload->device=device;payload->recording=recording;
    payload->receipt.state=NativeRecordingPayloadState::Allocated;payload->receipt.capacityBytes=capacityBytes;
    payload->receipt.ownedDataCapacityBytes=recordingOwnedDataCapacityBytes;
    payload->data.reserve(capacityBytes); // Modest initial host allocation; grow only as snapshots arrive.
    return payload;
}

void NativeBackend::beginRecordingPayload(const std::shared_ptr<NativeRecordingPayload>& payload,
    uint32_t flags,const NativeRecordingMask& inputMask,const NativeRecordingMask& outputMask) {
    validateRecordingPayload(payload);recordingMasks(flags,inputMask,outputMask);
    if(payload->receipt.state!=NativeRecordingPayloadState::Allocated)
        throw Error("Native recording payload cannot begin twice");
    auto* deferred=checkedRecordingContext(payload->recording);
    if(payload->recording->activeSession)throw Error("Native recording context already has an active payload");
    auto seed=RecordingSeed::capture(context.Get(),device.Get(),featureLevel);
    requireOwner(); // All qualification/allocation/Get* work precedes deferred mutation.
    payload->seed=std::move(seed);payload->receipt.flags=flags;
    payload->receipt.inputMask=inputMask;payload->receipt.outputMask=outputMask;
    payload->recording->active=payload;payload->recording->activeSession=true;
    payload->receipt.state=NativeRecordingPayloadState::Recording;
    try {
        deferred->ClearState();payload->seed.bind(deferred);requireOwner();
    } catch(...) {payload->receipt.state=NativeRecordingPayloadState::Failed;throw;}
}

ID3D11DeviceContext* NativeBackend::checkedRecordingPayloadContext(const std::shared_ptr<NativeRecordingPayload>& payload) const {
    validateRecordingPayload(payload);
    if(payload->receipt.state!=NativeRecordingPayloadState::Recording)
        throw Error("Native recording payload is not recording");
    auto* deferred=checkedRecordingContext(payload->recording);
    if(!payload->recording->activeSession || payload->recording->active.lock()!=payload)
        throw Error("Native recording payload is not its context's active session");
    return deferred;
}

void NativeBackend::recordRecordingDraw(const std::shared_ptr<NativeRecordingPayload>& payload,
    std::span<const uint8_t> ownedDrawData,const std::shared_ptr<void>& drawOwner,
    NativeRecordingPrepare prepare,const std::function<void(ID3D11DeviceContext*)>& draw) {
    auto* deferred=checkedRecordingPayloadContext(payload);
    if(!drawOwner || ownedDrawData.empty() || !draw)
        throw Error("Native recording draw requires owned data, a resource owner and an actual draw");
    if(inherits(payload->receipt.inputMask) && !prepare)
        throw Error("Native recording inherited constants require per-draw replay preparation");
    if(ownedDrawData.size()>payload->receipt.ownedDataCapacityBytes-payload->data.size())
        throw Error("Native recording owned draw data exceeds its host snapshot capacity");
    if(recordingDraws==std::numeric_limits<uint64_t>::max())throw Error("Native recording draw counter overflow");
    // Stage every potentially throwing allocation before invoking the one-draw
    // native backend callback. Input data may alias a caller buffer; copy first.
    std::vector<uint8_t> snapshot(ownedDrawData.begin(),ownedDrawData.end());
    NativeRecordingPayload::Draw entry{payload->data.size(),snapshot.size(),drawOwner,std::move(prepare)};
    const size_t required=payload->data.size()+snapshot.size();
    if(required>payload->data.capacity()) {
        const size_t growth=std::min<size_t>(payload->receipt.ownedDataCapacityBytes,
            std::max(required,payload->data.capacity()*2));
        payload->data.reserve(growth);
    }
    payload->draws.reserve(payload->draws.size()+1);
    try {
        {RecordingCallbackScope callback;draw(deferred);}
        requireOwner();
    } catch(...) {
        // Callback may already have emitted partial state/work. Only discard is
        // now legal; this list must never be sealed or executed as success.
        payload->receipt.state=NativeRecordingPayloadState::Failed;throw;
    }
    payload->data.insert(payload->data.end(),snapshot.begin(),snapshot.end()); // Reserved before the callback.
    payload->draws.push_back(std::move(entry)); // Reserved above; members move without allocation.
    payload->receipt.ownedDataBytes=payload->data.size();
    ++payload->receipt.recordedDraws;++recordingDraws;
}

NativeRecordingReceipt NativeBackend::recordingPayloadReceipt(const std::shared_ptr<NativeRecordingPayload>& payload) const {
    validateRecordingPayload(payload);return payload->receipt;
}

void NativeBackend::finishRecordingPayload(const std::shared_ptr<NativeRecordingPayload>& payload) {
    auto* deferred=checkedRecordingPayloadContext(payload);
    if(payload->draws.empty())throw Error("Native recording cannot finish zero recorded draws");
    ComPtr<ID3D11CommandList> commands;
    const HRESULT result=deferred->FinishCommandList(FALSE,&commands);
    if(FAILED(result) || !commands) {
        payload->receipt.state=NativeRecordingPayloadState::Failed;
        recordingCheck(result,"FinishCommandList");throw Error("Native recording finish returned no command list");
    }
    try {recordingDevice(commands.Get(),device.Get());requireOwner();}
    catch(...) {payload->receipt.state=NativeRecordingPayloadState::Failed;throw;}
    payload->commands=std::move(commands);payload->receipt.state=NativeRecordingPayloadState::Sealed;
    // Publish immutable ownership proof at installation: the GetDevice check
    // above already verified this exact list, so later receipts skip it.
    payload->provenCommands=payload->commands;
    payload->commandsProven=true;
    payload->recording->active.reset();payload->recording->activeSession=false;
}

namespace {
// Completed execution events kept for reuse; beyond this they are released.
constexpr size_t recordingEventPoolLimit=512;
}
void NativeBackend::retireRecordingPayloads() {
    validateSubmissionContext();noRecordingCallback();
    for(const auto& payload:pendingRecordingPayloads)
        if(!payload || !payload->inFlight || !payload->completion)
            throw Error("Native recording retirement lost its execution owner");
    // Reserved before any retirement so recycling an event never allocates mid-scan.
    if(recordingEventPool.capacity()<recordingEventPoolLimit)recordingEventPool.reserve(recordingEventPoolLimit);
    // The list holds each payload once, in the submission order of its current event, and
    // events on the one immediate context signal in submission order: the first incomplete
    // event ends the scan, because nothing submitted after it can have completed. (Polling
    // every pending event on every execution made retirement quadratic per frame.)
    size_t retired=0;
    while(retired<pendingRecordingPayloads.size()) {
        auto& payload=pendingRecordingPayloads[retired];
        BOOL completed=FALSE;
        const HRESULT result=context->GetData(payload->completion.Get(),&completed,sizeof(completed),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        recordingCheck(result,"execution retirement query");
        if(result!=S_OK || !completed)break;
        payload->inFlight=false;
        if(recordingEventPool.size()<recordingEventPoolLimit)recordingEventPool.push_back(std::move(payload->completion));
        payload->completion.Reset();
        payload->provenCompletion.Reset();payload->completionProven=false;
        ++retired;
    }
    pendingRecordingPayloads.erase(pendingRecordingPayloads.begin(),pendingRecordingPayloads.begin()+ptrdiff_t(retired));
    requireOwner();
}

bool NativeBackend::retireRecordingPayload(const std::shared_ptr<NativeRecordingPayload>& payload) {
    // Exact single-payload retirement for callers that need THIS payload's state, independent
    // of the in-order scan above.
    if(!payload->inFlight)return true;
    const auto it=std::find(pendingRecordingPayloads.begin(),pendingRecordingPayloads.end(),payload);
    if(it==pendingRecordingPayloads.end() || !payload->completion)
        throw Error("Native recording retirement lost its execution owner");
    if(recordingEventPool.capacity()<recordingEventPoolLimit)recordingEventPool.reserve(recordingEventPoolLimit);
    BOOL completed=FALSE;
    const HRESULT result=context->GetData(payload->completion.Get(),&completed,sizeof(completed),D3D11_ASYNC_GETDATA_DONOTFLUSH);
    recordingCheck(result,"execution retirement query");
    if(result!=S_OK || !completed)return false;
    payload->inFlight=false;
    if(recordingEventPool.size()<recordingEventPoolLimit)recordingEventPool.push_back(std::move(payload->completion));
    payload->completion.Reset();
    payload->provenCompletion.Reset();payload->completionProven=false;
    pendingRecordingPayloads.erase(it);
    return true;
}

void NativeBackend::executeRecordingPayload(const std::shared_ptr<NativeRecordingPayload>& payload) {
    validateRecordingPayload(payload);
    if(payload->receipt.state!=NativeRecordingPayloadState::Sealed || payload->draws.empty())
        throw Error("Native recording execution requires a sealed nonempty payload");
    if(payload->recording->activeSession)throw Error("Native recording execution overlaps an active recording session");
    const auto drawCount=payload->receipt.recordedDraws;
    if(payload->receipt.executions==std::numeric_limits<uint64_t>::max() ||
       payload->receipt.executedDraws>std::numeric_limits<uint64_t>::max()-drawCount ||
       recordingExecutedDraws>std::numeric_limits<uint64_t>::max()-drawCount)
        throw Error("Native recording execution counter overflow");
    retireRecordingPayloads();
    const bool alreadyPending=payload->inFlight;
    if(!alreadyPending)pendingRecordingPayloads.reserve(pendingRecordingPayloads.size()+1);
    const auto pendingAt=std::find(pendingRecordingPayloads.begin(),pendingRecordingPayloads.end(),payload);
    if(alreadyPending==(pendingAt==pendingRecordingPayloads.end()))
        throw Error("Native recording retirement lost its execution owner");
    ComPtr<ID3D11Query> completed;
    if(!recordingEventPool.empty()) {
        // A retired event of this device: its ownership was verified when it was created.
        completed=std::move(recordingEventPool.back());recordingEventPool.pop_back();
    } else {
        D3D11_QUERY_DESC description{D3D11_QUERY_EVENT,0};
        recordingCheck(device->CreateQuery(&description,&completed),"execution event creation");
        // Owner-device provenance is not assumed: verify the new query before the
        // first GPU mutation so later receipts can rely on the proof. No payload
        // state changes before this check, keeping a failure retryable.
        recordingDevice(completed.Get(),device.Get());
    }
    requireOwner();
    // Preparation updates each draw's retained native constant buffers on the
    // immediate context. No inherited-buffer Update* is recorded in the list.
    // A failed preparation submits no list, changes no counts, and is retryable.
    {
        StallProfiler::Scope prepareProfile(StallProfiler::Section::Rendering,"D3D11.PrepareRecordingDraws",nullptr,reinterpret_cast<uintptr_t>(payload->commands.Get()));
        for(const auto& entry:payload->draws)if(entry.prepare) {
            RecordingCallbackScope callback;
            entry.prepare(context.Get(),entry.owner,std::span<const uint8_t>(payload->data).subspan(entry.offset,entry.bytes));
        }
    }
    requireOwner();
    // Retain before issuing anything: even a post-submission device failure
    // leaves resource ownership anchored until retirement or backend teardown.
    payload->completion=std::move(completed);payload->inFlight=true;
    payload->provenCompletion=payload->completion;
    payload->completionProven=true;
    // Its new event is the latest submission: keep the list in event submission order.
    if(!alreadyPending)pendingRecordingPayloads.push_back(payload);
    else std::rotate(pendingAt,pendingAt+1,pendingRecordingPayloads.end());
    StallProfiler::Scope executeProfile(StallProfiler::Section::Rendering,"D3D11.ExecuteCommandList",nullptr,reinterpret_cast<uintptr_t>(payload->commands.Get()));
    context->ExecuteCommandList(payload->commands.Get(),TRUE);
    executeProfile.finish();
    context->End(payload->completion.Get());
    ++payload->receipt.executions;payload->receipt.executedDraws+=drawCount;recordingExecutedDraws+=drawCount;
    requireOwner();
}

void NativeBackend::releaseRecordingPayload(const std::shared_ptr<NativeRecordingPayload>& payload) {
    validateRecordingPayload(payload);retireRecordingPayloads();
    if(payload->inFlight && !retireRecordingPayload(payload))
        throw Error("Native recording payload still has GPU work in flight");
    if(payload->receipt.state==NativeRecordingPayloadState::Recording || payload->receipt.state==NativeRecordingPayloadState::Failed) {
        auto* deferred=checkedRecordingContext(payload->recording);
        if(!payload->recording->activeSession || payload->recording->active.lock()!=payload)
            throw Error("Native recording discard has no matching active session");
        // Finish solely to DISCARD pending commands, including zero draws. This
        // does not publish a sealed payload or count successful recorded work.
        ComPtr<ID3D11CommandList> discarded;
        const HRESULT result=deferred->FinishCommandList(FALSE,&discarded);
        if(FAILED(result)) {
            payload->receipt.state=NativeRecordingPayloadState::Failed;
            recordingCheck(result,"discarding unfinished recording");
        }
        payload->recording->active.reset();payload->recording->activeSession=false;
    }
    payload->commands.Reset();payload->completion.Reset();payload->seed=RecordingSeed{};
    payload->provenCommands.Reset();payload->commandsProven=false;
    payload->provenCompletion.Reset();payload->completionProven=false;
    payload->draws.clear();payload->data.clear();payload->recording.reset();payload->device.Reset();
    payload->receipt.state=NativeRecordingPayloadState::Released;
}

uint64_t NativeBackend::recordingDrawCount() const {validateSubmissionContext();noRecordingCallback();return recordingDraws;}
uint64_t NativeBackend::recordingExecutedDrawCount() const {validateSubmissionContext();noRecordingCallback();return recordingExecutedDraws;}
}
