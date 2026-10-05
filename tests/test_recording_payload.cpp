#include "renderer/native_backend.h"
#include "renderer/device_availability.h"
#include "VSFlat.h"
#include "PSFlat.h"
#include <d3d11sdklayers.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>

using namespace Simpsons::Graphics;
size_t payloadChecks{};
void require(bool value,const char* reason){++payloadChecks;if(!value)throw Error(reason);}
template<class F> void rejects(F action) {
    bool rejected=false;try{action();}catch(const Error&){rejected=true;}
    require(rejected,"Invalid recording payload operation did not reject");
}
#include "header/test_engine_binding_reset.h"

namespace Simpsons::Graphics {
// Same existing private fixture seam as NativeRecordingTests; no public raw
// D3D entry point and no runtime shader compiler are introduced.
struct NativeRecordingProbe {
    static ID3D11Device* device(NativeBackend& backend){return backend.device.Get();}
    static ID3D11DeviceContext* immediate(NativeBackend& backend){return backend.context.Get();}
    static ID3D11DeviceContext* deferred(NativeBackend& backend,const std::shared_ptr<NativeRecordingPayload>& payload) {
        return backend.checkedRecordingPayloadContext(payload);
    }
    static void draw(NativeBackend& backend,const std::shared_ptr<NativeRecordingPayload>& payload,
        std::span<const uint8_t> data,const std::shared_ptr<void>& owner,NativeRecordingPrepare prepare,
        const std::function<void(ID3D11DeviceContext*)>& record) {
        backend.recordRecordingDraw(payload,data,owner,std::move(prepare),record);
    }
    static void retire(NativeBackend& backend){backend.retireRecordingPayloads();}
    static size_t pending(const NativeBackend& backend){return backend.pendingRecordingPayloads.size();}
    // Cache-hit replacement seam: swap/restore the immediate context to prove
    // a warmed identity proof still re-checks a changed pair. ComPtr copies
    // keep both sides alive across the swap; no mutable bindings are touched.
    static ComPtr<ID3D11DeviceContext> saveImmediate(NativeBackend& backend){return backend.context;}
    static void setImmediate(NativeBackend& backend,const ComPtr<ID3D11DeviceContext>& replacement){backend.context=replacement;}
    static void debug(NativeBackend& backend,bool hardware) {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        HRESULT result=D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_DEBUG,levels,2,D3D11_SDK_VERSION,&device,&level,&context);
        if(result==E_INVALIDARG)result=D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_DEBUG,levels+1,1,D3D11_SDK_VERSION,&device,&level,&context);
        if(SUCCEEDED(result)) {
            backend.availability=std::make_shared<DeviceAvailability>(device.Get());backend.device=device;
            backend.context=context;backend.featureLevel=level;
            std::fprintf(stderr,"[RECORDING PAYLOAD TEST] D3D11 debug layer enabled\n");
        }
    }
};
}

namespace {
using Snapshot=EngineBindingResetProbe::Snapshot;
void hr(HRESULT result,const char* reason){require(SUCCEEDED(result),reason);}
using Constants=std::array<float,8>;
std::span<const uint8_t> bytes(const Constants& value) {
    return {reinterpret_cast<const uint8_t*>(value.data()),sizeof(value)};
}
NativeRecordingMask mask() {
    NativeRecordingMask value{};value[0]=0xFF;value[8]=0xFF;value[9]=0xE0;return value;
}
struct ReplayControl {
    float factor=1;
    bool fail=false;
    size_t calls{};
    std::function<void()> before;
};
struct DrawOwner {
    ComPtr<ID3D11Buffer> vertices,constants;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depthState;
};
NativeRecordingPrepare prepare(const std::shared_ptr<ReplayControl>& control) {
    return [control](ID3D11DeviceContext* immediate,const std::shared_ptr<void>& erased,std::span<const uint8_t> data) {
        require(immediate && immediate->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE,"Replay preparation did not receive immediate context");
        require(data.size()==sizeof(Constants),"Replay snapshot length changed");
        if(control->fail)throw Error("Injected replay preparation failure");
        if(control->before)control->before();
        Constants value{};std::memcpy(value.data(),data.data(),data.size());
        // Preserve the recorded material's RGB direction and alpha; vary only
        // current-frame intensity. Each draw has a DISTINCT native buffer.
        for(size_t i=0;i<3;++i)value[i]*=control->factor;
        const auto owned=std::static_pointer_cast<DrawOwner>(erased);
        immediate->UpdateSubresource(owned->constants.Get(),0,nullptr,value.data(),0,0);++control->calls;
    };
}
void actualDraw(ID3D11DeviceContext* deferred,const std::shared_ptr<DrawOwner>& owned) {
    require(deferred->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED,"Fixture draw was sent to immediate context");
    const UINT stride=2*sizeof(float),offset=0;auto* vertex=owned->vertices.Get();
    deferred->IASetVertexBuffers(0,1,&vertex,&stride,&offset);deferred->IASetInputLayout(owned->layout.Get());
    deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    auto* constant=owned->constants.Get();deferred->PSSetConstantBuffers(0,1,&constant);
    deferred->RSSetState(owned->raster.Get());deferred->OMSetDepthStencilState(owned->depthState.Get(),0x73);
    deferred->OMSetBlendState(nullptr,nullptr,0xFFFFFFFF);
    // VS/PS, targets and viewport/scissor come ONLY from the begin seed.
    // No Update/Map occurs here: inherited data cannot be frozen in the list.
    deferred->Draw(4,0);
}
struct Fixture {
    NativeBackend backend;
    std::shared_ptr<NativeRecordingContext> recording;
    std::shared_ptr<RenderTarget> target,sentinel;
    std::shared_ptr<DepthTarget> depth;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    static constexpr uint32_t width=16,height=8;
    explicit Fixture(bool hardware):backend(!hardware) {
        NativeRecordingProbe::debug(backend,hardware);
        auto* device=NativeRecordingProbe::device(backend);
        hr(device->CreateVertexShader(kVSFlat,sizeof(kVSFlat),nullptr,&vs),"Fixture VS creation");
        hr(device->CreatePixelShader(kPSFlat,sizeof(kPSFlat),nullptr,&ps),"Fixture PS creation");
        target=backend.createTarget(width,height,TargetFormat::RGBA32Float);
        sentinel=backend.createTarget(width,height,TargetFormat::RGBA32Float);depth=backend.createDepthTarget(width,height);
        recording=backend.createRecordingContext();seed();clear();
    }
    ID3D11DeviceContext* immediate(){return NativeRecordingProbe::immediate(backend);}
    void seed() {
        backend.bindTargets({target,nullptr,nullptr,nullptr},depth);
        immediate()->VSSetShader(vs.Get(),nullptr,0);immediate()->PSSetShader(ps.Get(),nullptr,0);
        const D3D11_VIEWPORT vp{1,1,14,6,0,1};const D3D11_RECT sc{2,2,14,6};
        immediate()->RSSetViewports(1,&vp);immediate()->RSSetScissorRects(1,&sc);
    }
    void clear() {
        backend.clearTarget(target,{0.125f,0.25f,0.5f,1});backend.clearTarget(sentinel,{0.5f,0,0.25f,1});
        backend.clearDepthTarget(depth,0.75f,0x73);
    }
    std::shared_ptr<NativeRecordingPayload> begin(uint32_t capacity=0x3000,uint32_t flags=4,
        const NativeRecordingMask& input=mask(),const NativeRecordingMask& output=mask()) {
        seed();auto payload=backend.allocateRecordingPayload(recording,capacity);
        backend.beginRecordingPayload(payload,flags,input,output);return payload;
    }
    std::shared_ptr<DrawOwner> owner(bool right=false) {
        auto result=std::make_shared<DrawOwner>();auto* device=NativeRecordingProbe::device(backend);
        const float left=right?0.0f:-1.0f,rightEdge=right?1.0f:0.0f;
        const std::array<float,8> vertices={left,1,left,-1,rightEdge,1,rightEdge,-1};
        D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof(vertices);buffer.Usage=D3D11_USAGE_IMMUTABLE;buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{vertices.data(),0,0};hr(device->CreateBuffer(&buffer,&data,&result->vertices),"Fixture vertex buffer");
        buffer.Usage=D3D11_USAGE_DEFAULT;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        const Constants zero{};data.pSysMem=zero.data();hr(device->CreateBuffer(&buffer,&data,&result->constants),"Fixture per-draw constants");
        const D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        hr(device->CreateInputLayout(&element,1,kVSFlat,sizeof(kVSFlat),&result->layout),"Fixture input layout");
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;
        raster.DepthClipEnable=TRUE;raster.ScissorEnable=TRUE;
        hr(device->CreateRasterizerState(&raster,&result->raster),"Fixture rasterizer");
        D3D11_DEPTH_STENCIL_DESC depthState{};depthState.DepthEnable=TRUE;depthState.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
        depthState.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
        depthState.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};
        depthState.BackFace=depthState.FrontFace;
        hr(device->CreateDepthStencilState(&depthState,&result->depthState),"Fixture depth state");return result;
    }
    void append(const std::shared_ptr<NativeRecordingPayload>& payload,const std::shared_ptr<DrawOwner>& owned,
        const Constants& constants,const std::shared_ptr<ReplayControl>& control) {
        NativeRecordingProbe::draw(backend,payload,bytes(constants),owned,prepare(control),[owned](auto* deferred){actualDraw(deferred,owned);});
    }
    void unrelatedImmediateState() {
        backend.bindTargets({sentinel,nullptr,nullptr,nullptr},nullptr);
        immediate()->VSSetShader(nullptr,nullptr,0);immediate()->PSSetShader(nullptr,nullptr,0);
        const D3D11_VIEWPORT vp{0,0,16,8,0.25f,0.75f};const D3D11_RECT sc{0,0,1,1};
        immediate()->RSSetViewports(1,&vp);immediate()->RSSetScissorRects(1,&sc);
        immediate()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    }
    void pixels(float intensity,bool both=true) {
        const auto color=backend.readbackTarget(target),depthBytes=backend.readbackDepthTarget(depth);
        require(color.size()==width*height*16 && depthBytes.size()==width*height*8,"Fixture readback dimensions");
        for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
            const bool inside=x>=2 && x<14 && y>=2 && y<6 && (both || x<8);
            const std::array<float,4> expected=inside?(x<8?std::array<float,4>{intensity,0,0,1}:
                std::array<float,4>{0,intensity,0,1}):std::array<float,4>{0.125f,0.25f,0.5f,1};
            const size_t pixel=y*width+x;
            for(size_t lane=0;lane<4;++lane) {
                uint32_t actual{};std::memcpy(&actual,color.data()+pixel*16+lane*4,4);
                if(actual!=std::bit_cast<uint32_t>(expected[lane])) {
                    std::fprintf(stderr,"pixel %u,%u lane %zu got %08X expected %08X\n",x,y,lane,actual,std::bit_cast<uint32_t>(expected[lane]));
                    require(false,"Recorded GPU pixel output mismatch");
                }
                ++payloadChecks;
            }
            uint32_t actualDepth{};std::memcpy(&actualDepth,depthBytes.data()+pixel*8,4);
            require(actualDepth==std::bit_cast<uint32_t>(inside?0.0f:0.75f),"Recorded depth output mismatch");
            require(depthBytes[pixel*8+4]==0x73,"Recorded draw changed stencil");
        }
    }
    void debugErrors() {
        ComPtr<ID3D11InfoQueue> queue;
        if(FAILED(NativeRecordingProbe::device(backend)->QueryInterface(IID_PPV_ARGS(&queue))))return;
        for(UINT64 i=0;i<queue->GetNumStoredMessagesAllowedByRetrievalFilter();++i) {
            SIZE_T count{};hr(queue->GetMessage(i,nullptr,&count),"Debug message length");std::vector<uint8_t> storage(count);
            auto* message=reinterpret_cast<D3D11_MESSAGE*>(storage.data());hr(queue->GetMessage(i,message,&count),"Debug message");
            if(message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR) {
                std::fprintf(stderr,"D3D11: %s\n",message->pDescription);require(false,"Recording emitted a D3D11 error");
            }
        }
        queue->ClearStoredMessages();
    }
};

void lifecycleAndPixels(Fixture& f) {
    const auto before=f.backend.readbackTarget(f.target),depthBefore=f.backend.readbackDepthTarget(f.depth);
    const Snapshot seedState(f.immediate());
    auto payload=f.begin();auto& backend=f.backend;
    auto receipt=backend.recordingPayloadReceipt(payload);
    require(receipt.state==NativeRecordingPayloadState::Recording && receipt.capacityBytes==0x3000 &&
        receipt.ownedDataCapacityBytes==(1u<<20) && receipt.flags==4 &&
        receipt.inputMask==mask() && receipt.outputMask==mask() && !receipt.ownedDataBytes && !receipt.recordedDraws,"Begin receipt mismatch");
    require(Snapshot(f.immediate())==seedState,"Begin changed immediate bindings");
    auto* deferred=NativeRecordingProbe::deferred(backend,payload);
    require(deferred!=f.immediate(),"Payload accessor returned immediate context");
    ComPtr<ID3D11RenderTargetView> colorA,colorB;ComPtr<ID3D11DepthStencilView> depthA,depthB;
    f.immediate()->OMGetRenderTargets(1,&colorA,&depthA);deferred->OMGetRenderTargets(1,&colorB,&depthB);
    require(colorA==colorB && depthA==depthB,"Begin did not seed actual native attachments");
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    deferred->VSGetShader(&vs,nullptr,nullptr);deferred->PSGetShader(&ps,nullptr,nullptr);
    require(vs==f.vs && ps==f.ps,"Begin did not seed original actual shaders");
    D3D11_VIEWPORT viewport{};D3D11_RECT scissor{};UINT count=1;deferred->RSGetViewports(&count,&viewport);
    require(count==1 && viewport.TopLeftX==1 && viewport.Width==14 && viewport.Height==6,"Deferred viewport seed mismatch");
    count=1;deferred->RSGetScissorRects(&count,&scissor);require(count==1 && scissor.left==2 && scissor.bottom==6,"Deferred scissor seed mismatch");

    rejects([&]{backend.finishRecordingPayload(payload);});
    rejects([&]{backend.executeRecordingPayload(payload);});
    rejects([&]{backend.beginRecordingPayload(payload,4,mask(),mask());});
    rejects([&]{backend.releaseRecordingContext(f.recording);});
    auto concurrent=backend.allocateRecordingPayload(f.recording,0x9000);
    rejects([&]{backend.beginRecordingPayload(concurrent,0,{},{});});backend.releaseRecordingPayload(concurrent);
    auto control=std::make_shared<ReplayControl>();auto left=f.owner(),right=f.owner(true);
    std::weak_ptr<DrawOwner> leftLease=left,rightLease=right;
    Constants red{1,0,0,1,0,0,0,0},green{0,1,0,1,0,0,0,0};
    f.append(payload,left,red,control);f.append(payload,right,green,control);
    red.fill(9);green.fill(9);left.reset();right.reset(); // Future execution must use owned snapshots/leases.
    receipt=backend.recordingPayloadReceipt(payload);
    require(receipt.ownedDataBytes==2*sizeof(Constants) && receipt.recordedDraws==2 && !receipt.executions && !receipt.executedDraws,
        "Recording did not account exact copied draw data");
    require(!leftLease.expired() && !rightLease.expired(),"Recording dropped per-draw owners");
    backend.finishRecordingPayload(payload);
    require(backend.recordingPayloadReceipt(payload).state==NativeRecordingPayloadState::Sealed,"Finish did not seal payload");
    rejects([&]{backend.finishRecordingPayload(payload);});rejects([&]{NativeRecordingProbe::deferred(backend,payload);});
    require(Snapshot(f.immediate())==seedState,"Recording/finish changed immediate bindings");
    require(backend.readbackTarget(f.target)==before && backend.readbackDepthTarget(f.depth)==depthBefore,
        "Begin/record/finish executed work before playback");

    // A sealed payload remains executable after its deferred context is released.
    backend.releaseRecordingContext(f.recording);
    f.unrelatedImmediateState();const Snapshot restore(f.immediate());const auto sentinelBefore=backend.readbackTarget(f.sentinel);
    control->fail=true;rejects([&]{backend.executeRecordingPayload(payload);});control->fail=false;
    require(backend.readbackTarget(f.target)==before && backend.recordingPayloadReceipt(payload).executions==0,
        "Failed preparation executed the list");
    require(Snapshot(f.immediate())==restore,"Failed preparation changed bindings");
    control->before=[&]{backend.releaseRecordingPayload(payload);};
    rejects([&]{backend.executeRecordingPayload(payload);});control->before={};
    require(!backend.recordingPayloadReceipt(payload).executedDraws,"Reentrant preparation executed work");

    backend.executeRecordingPayload(payload);f.pixels(1);
    require(control->calls==2,"Replay did not prepare each draw exactly once");
    require(Snapshot(f.immediate())==restore,"ExecuteCommandList failed to restore immediate bindings");
    require(backend.readbackTarget(f.sentinel)==sentinelBefore,"Playback drew into the current target instead of the recorded target");
    control->factor=0.25f;backend.executeRecordingPayload(payload);f.pixels(0.25f);
    receipt=backend.recordingPayloadReceipt(payload);
    require(receipt.recordedDraws==2 && receipt.executedDraws==4 && receipt.executions==2 && control->calls==4,
        "Record and execution counts were conflated");
    require(backend.recordingDrawCount()==2 && backend.recordingExecutedDrawCount()==4,"Backend recording counters mismatch");
    require(!backend.screenDrawCount() && !backend.presentationCount(),"Recording changed unrelated counters");
    backend.waitIdle();NativeRecordingProbe::retire(backend);backend.releaseRecordingPayload(payload);
    require(leftLease.expired() && rightLease.expired(),"Released payload retained draw owners after GPU retirement");
    rejects([&]{backend.recordingPayloadReceipt(payload);});rejects([&]{backend.executeRecordingPayload(payload);});
    rejects([&]{backend.releaseRecordingPayload(payload);});
    f.recording=backend.createRecordingContext();f.seed();f.clear();
}

void expandedSnapshotCapacityAndPixels(Fixture& f) {
    // Full rigid constant snapshots are 1,624 host bytes per draw, whereas the
    // original SDK capacities describe GPU command bytes. Twenty-four draws
    // cross BOTH original allocation sizes without exhausting the host budget.
    constexpr size_t snapshotBytes=sizeof(RigidVertexConstants)+sizeof(RigidPixelConstants)+sizeof(NativeRecordingMask);
    static_assert(snapshotBytes==1624);
    constexpr size_t drawCount=24;
    auto& backend=f.backend;
    for(uint32_t sdkCapacity:{0x3000u,0x9000u}) {
        f.seed();f.clear();const Snapshot seedState(f.immediate());
        const auto colorBefore=backend.readbackTarget(f.target),depthBefore=backend.readbackDepthTarget(f.depth);
        const auto recordedBefore=backend.recordingDrawCount(),executedBefore=backend.recordingExecutedDrawCount();
        auto payload=f.begin(sdkCapacity);auto control=std::make_shared<ReplayControl>();
        std::array<std::weak_ptr<DrawOwner>,drawCount> leases;
        for(size_t i=0;i<drawCount;++i) {
            auto owned=f.owner((i&1)!=0);leases[i]=owned;
            const Constants constants=(i&1)?Constants{0,1,0,1,0,0,0,0}:Constants{1,0,0,1,0,0,0,0};
            const uint8_t tag=uint8_t(i+1);std::array<uint8_t,snapshotBytes> snapshot;
            snapshot.fill(tag);std::memcpy(snapshot.data(),constants.data(),sizeof(constants));
            auto checkAndPrepare=[constants,tag,base=prepare(control)](ID3D11DeviceContext* immediate,
                const std::shared_ptr<void>& erased,std::span<const uint8_t> data) {
                require(data.size()==snapshotBytes,"Expanded replay snapshot length changed");
                const auto expected=bytes(constants);
                require(std::equal(expected.begin(),expected.end(),data.begin()),"Expanded replay constants changed during host storage growth");
                require(std::all_of(data.begin()+sizeof(Constants),data.end(),[tag](uint8_t value){return value==tag;}),
                    "Expanded replay snapshot tail changed during host storage growth");
                base(immediate,erased,data.first(sizeof(Constants)));
            };
            NativeRecordingProbe::draw(backend,payload,snapshot,owned,std::move(checkAndPrepare),
                [owned](auto* deferred){actualDraw(deferred,owned);});
            snapshot.fill(0xCD); // Replay must ignore the caller's subsequently overwritten storage.
        }
        const auto recorded=backend.recordingPayloadReceipt(payload);
        require(recorded.capacityBytes==sdkCapacity && recorded.ownedDataCapacityBytes==(1u<<20),
            "Expanded snapshots changed original allocation metadata or the host budget");
        require(recorded.ownedDataBytes==drawCount*snapshotBytes && recorded.ownedDataBytes>sdkCapacity &&
            recorded.recordedDraws==drawCount && !recorded.executions && !recorded.executedDraws,
            "Expanded snapshots were not accounted independently of SDK command bytes");
        for(const auto& lease:leases)require(!lease.expired(),"Storage growth lost a recorded draw resource owner");
        backend.finishRecordingPayload(payload);
        require(backend.recordingPayloadReceipt(payload).state==NativeRecordingPayloadState::Sealed,
            "Expanded recording did not seal beyond the original SDK byte capacity");
        require(Snapshot(f.immediate())==seedState && backend.readbackTarget(f.target)==colorBefore &&
            backend.readbackDepthTarget(f.depth)==depthBefore,"Expanded recording submitted work before playback");
        f.unrelatedImmediateState();const Snapshot restore(f.immediate());
        backend.executeRecordingPayload(payload);f.pixels(1);
        control->factor=0.25f;backend.executeRecordingPayload(payload);f.pixels(0.25f);
        const auto replayed=backend.recordingPayloadReceipt(payload);
        require(replayed.capacityBytes==recorded.capacityBytes && replayed.ownedDataCapacityBytes==recorded.ownedDataCapacityBytes &&
            replayed.ownedDataBytes==recorded.ownedDataBytes && replayed.recordedDraws==drawCount &&
            replayed.executedDraws==2*drawCount && replayed.executions==2 && control->calls==2*drawCount,
            "Expanded replay changed owned accounting or skipped per-draw preparation");
        require(backend.recordingDrawCount()==recordedBefore+drawCount && backend.recordingExecutedDrawCount()==executedBefore+2*drawCount,
            "Expanded replay global counters differ from actual recorded and executed draws");
        require(Snapshot(f.immediate())==restore,"Expanded replay changed current immediate bindings");
        backend.waitIdle();NativeRecordingProbe::retire(backend);backend.releaseRecordingPayload(payload);
        for(const auto& lease:leases)require(lease.expired(),"Retired expanded recording retained a draw resource owner");
    }
    f.seed();f.clear();
}

void rejectionAndDiscard(Fixture& f,bool hardware) {
    auto& backend=f.backend;NativeBackend foreign(!hardware);const Snapshot before(f.immediate());
    rejects([&]{backend.allocateRecordingPayload(nullptr,0x3000);});
    for(uint32_t capacity:{0u,1u,0x2FFFu,0x3001u,0x8FFFu,0x9001u,0xFFFFFFFFu})
        rejects([&]{backend.allocateRecordingPayload(f.recording,capacity);});
    auto payload=backend.allocateRecordingPayload(f.recording,0x9000);
    rejects([&]{foreign.recordingPayloadReceipt(payload);});rejects([&]{foreign.beginRecordingPayload(payload,0,{},{});});
    rejects([&]{foreign.releaseRecordingPayload(payload);});
    for(uint32_t flags:{1u,2u,8u,0xFFFFFFFFu})rejects([&]{backend.beginRecordingPayload(payload,flags,{},{});});
    auto bad=mask();bad[39]=1;rejects([&]{backend.beginRecordingPayload(payload,4,bad,mask());});
    rejects([&]{backend.beginRecordingPayload(payload,4,mask(),bad);});
    auto output=mask();output[0]=0x80;backend.beginRecordingPayload(payload,0,mask(),output);
    const auto receipt=backend.recordingPayloadReceipt(payload);
    require(receipt.capacityBytes==0x9000 && receipt.ownedDataCapacityBytes==(1u<<20) &&
        receipt.flags==0 && receipt.inputMask==mask() && receipt.outputMask==output,
        "Distinct exact masks or capacity were not retained");
    std::atomic<bool> wrongThreadRejected=false;
    // Receipt is otherwise valid in this phase; rejection must not accidentally
    // be caused by an unrelated sealed-state requirement.
    std::thread other([&]{try{backend.recordingPayloadReceipt(payload);}catch(const Error& error){
        wrongThreadRejected=std::string(error.what()).starts_with("Native graphics immediate context used from a different thread");
    }});other.join();
    require(wrongThreadRejected,"Payload accepted another thread");
    const auto colorBefore=backend.readbackTarget(f.target),depthBefore=backend.readbackDepthTarget(f.depth);
    auto owned=f.owner();const Constants red{1,0,0,1,0,0,0,0};auto control=std::make_shared<ReplayControl>();
    bool called=false;auto draw=[&](auto* context){called=true;actualDraw(context,owned);};
    std::vector<uint8_t> tooLarge(size_t(receipt.ownedDataCapacityBytes)+1,0);
    rejects([&]{NativeRecordingProbe::draw(backend,payload,tooLarge,owned,prepare(control),draw);});
    rejects([&]{NativeRecordingProbe::draw(backend,payload,{},owned,prepare(control),draw);});
    rejects([&]{NativeRecordingProbe::draw(backend,payload,bytes(red),{},prepare(control),draw);});
    rejects([&]{NativeRecordingProbe::draw(backend,payload,bytes(red),owned,{},draw);});
    rejects([&]{NativeRecordingProbe::draw(backend,payload,bytes(red),owned,prepare(control),{});});
    require(!called && !backend.recordingPayloadReceipt(payload).recordedDraws,"Rejected draw emitted native work");
    // Release an empty active session by discarding, never successful finish.
    backend.releaseRecordingPayload(payload);
    auto failed=f.begin();const auto recorded=backend.recordingDrawCount();
    rejects([&]{NativeRecordingProbe::draw(backend,failed,bytes(red),owned,prepare(control),[&](auto* context){
        actualDraw(context,owned);throw Error("Injected failure after native draw emission");
    });});
    require(backend.recordingPayloadReceipt(failed).state==NativeRecordingPayloadState::Failed,"Partial recording was not marked failed");
    require(backend.recordingDrawCount()==recorded,"Failed draw callback published successful accounting");
    rejects([&]{backend.finishRecordingPayload(failed);});rejects([&]{backend.executeRecordingPayload(failed);});
    backend.releaseRecordingPayload(failed);
    require(backend.readbackTarget(f.target)==colorBefore && backend.readbackDepthTarget(f.depth)==depthBefore,
        "Discarded recording changed GPU pixels");

    // Charge the EXACT separate host capacity, then reject one more byte before
    // draw. Guest allocation metadata remains 0x3000 throughout.
    auto exact=f.begin(0x3000,0,{},{});
    const auto hostCapacity=backend.recordingPayloadReceipt(exact).ownedDataCapacityBytes;
    std::vector<uint8_t> full(hostCapacity,0x4D);
    NativeRecordingProbe::draw(backend,exact,full,owned,{},[&](auto* context){actualDraw(context,owned);});
    const auto fullReceipt=backend.recordingPayloadReceipt(exact);
    require(fullReceipt.capacityBytes==0x3000 && fullReceipt.ownedDataBytes==hostCapacity &&
        fullReceipt.ownedDataCapacityBytes==hostCapacity,"Exact native host byte budget was not accounted");
    called=false;const uint8_t extra=0;
    rejects([&]{NativeRecordingProbe::draw(backend,exact,std::span<const uint8_t>(&extra,1),owned,{},draw);});
    const auto afterOverflow=backend.recordingPayloadReceipt(exact);
    require(!called && afterOverflow.recordedDraws==1 && afterOverflow.ownedDataBytes==fullReceipt.ownedDataBytes &&
        afterOverflow.state==NativeRecordingPayloadState::Recording,"Host capacity rejection emitted draw work or changed accounting");
    backend.releaseRecordingPayload(exact);
    // Warmed identity proofs must still reject a replaced immediate context
    // and accept the restored original (cache not poisoned by the failure).
    {
        ComPtr<ID3D11DeviceContext> original=NativeRecordingProbe::saveImmediate(backend);
        backend.validateSubmissionContext();
        auto probePayload=backend.allocateRecordingPayload(f.recording,0x3000);
        (void)backend.recordingPayloadReceipt(probePayload);
        ComPtr<ID3D11DeviceContext> foreignImmediate(NativeRecordingProbe::immediate(foreign));
        NativeRecordingProbe::setImmediate(backend,foreignImmediate);
        rejects([&]{backend.validateSubmissionContext();});
        rejects([&]{backend.recordingPayloadReceipt(probePayload);});
        NativeRecordingProbe::setImmediate(backend,original);
        backend.validateSubmissionContext();
        (void)backend.recordingPayloadReceipt(probePayload);
        backend.releaseRecordingPayload(probePayload);
    }
    require(Snapshot(f.immediate())==before,"Rejection/discard changed immediate bindings");
    require(backend.readbackTarget(f.target)==colorBefore,"Capacity fixture leaked deferred drawing into immediate context");

    // Unsupported MRT seed rejects while still Allocated, before ClearState on
    // the deferred context; removing that unsupported input permits a retry.
    auto unsupported=backend.allocateRecordingPayload(f.recording,0x3000);
    backend.bindTargets({f.target,f.sentinel,nullptr,nullptr},f.depth);
    const Snapshot mrt(f.immediate());rejects([&]{backend.beginRecordingPayload(unsupported,0,{},{});});
    require(Snapshot(f.immediate())==mrt && backend.recordingPayloadReceipt(unsupported).state==NativeRecordingPayloadState::Allocated,
        "Rejected begin published recording readiness");
    f.seed();backend.beginRecordingPayload(unsupported,0,{},{});backend.releaseRecordingPayload(unsupported);
}

void retirementAndLifetime(Fixture& f,bool hardware) {
    auto& backend=f.backend;auto control=std::make_shared<ReplayControl>();auto owned=f.owner();
    std::weak_ptr<DrawOwner> lease=owned;const Constants red{1,0,0,1,0,0,0,0};
    auto payload=f.begin();f.append(payload,owned,red,control);backend.finishRecordingPayload(payload);
    std::weak_ptr<NativeRecordingPayload> pending=payload;owned.reset();backend.executeRecordingPayload(payload);payload.reset();
    require(!pending.expired() && !lease.expired() && NativeRecordingProbe::pending(backend)!=0,
        "Dropping the caller token lost in-flight native owners");
    backend.waitIdle();NativeRecordingProbe::retire(backend);
    require(pending.expired() && lease.expired() && NativeRecordingProbe::pending(backend)==0,
        "Completed recording payload lease did not retire");
    f.pixels(1,false);f.clear();

    // Reconstruct NativeBackend at the very same C++ address. The old payload
    // retains the old device and must be rejected by the replacement backend.
    alignas(NativeBackend) std::byte storage[sizeof(NativeBackend)];
    auto destroy=[](NativeBackend* value){std::destroy_at(value);};
    std::unique_ptr<NativeBackend,decltype(destroy)> temporary(
        std::construct_at(reinterpret_cast<NativeBackend*>(storage),!hardware),destroy);
    auto context=temporary->createRecordingContext();auto retained=temporary->allocateRecordingPayload(context,0x3000);
    temporary.reset();temporary.reset(std::construct_at(reinterpret_cast<NativeBackend*>(storage),!hardware));
    rejects([&]{temporary->recordingPayloadReceipt(retained);});rejects([&]{temporary->releaseRecordingPayload(retained);});
    temporary.reset();std::weak_ptr<NativeRecordingPayload> weak=retained;
    std::thread cleanup([value=std::move(retained)]() mutable {value.reset();});cleanup.join();
    require(weak.expired(),"Opaque payload RAII depended on a surviving backend/thread");context.reset();
}

void sameReceipt(const NativeRecordingReceipt& batch,const NativeRecordingReceipt& single,const char* stage) {
    require(batch.state==single.state,"Batch receipt state differs from single");
    require(batch.capacityBytes==single.capacityBytes,"Batch receipt capacity differs from single");
    require(batch.ownedDataCapacityBytes==single.ownedDataCapacityBytes,"Batch receipt host capacity differs from single");
    require(batch.flags==single.flags,"Batch receipt flags differ from single");
    require(batch.inputMask==single.inputMask,"Batch receipt input mask differs from single");
    require(batch.outputMask==single.outputMask,"Batch receipt output mask differs from single");
    require(batch.ownedDataBytes==single.ownedDataBytes,"Batch receipt data bytes differ from single");
    require(batch.recordedDraws==single.recordedDraws,"Batch receipt recorded draws differ from single");
    require(batch.executedDraws==single.executedDraws,"Batch receipt executed draws differ from single");
    require(batch.executions==single.executions,"Batch receipt executions differ from single");
    (void)stage;
}

void checkBatch(NativeBackend& backend,const std::vector<std::shared_ptr<NativeRecordingPayload>>& items,const char* stage) {
    std::vector<NativeRecordingReceipt> out(items.size());
    backend.recordingPayloadReceipts(items,out);
    require(out.size()==items.size(),"Batch receipt output length changed");
    for(size_t i=0;i<items.size();++i) {
        const auto single=backend.recordingPayloadReceipt(items[i]);
        sameReceipt(out[i],single,stage);
    }
}

void batchReceipts(Fixture& f,bool hardware) {
    auto& backend=f.backend;
    f.seed();f.clear();
    auto second=backend.createRecordingContext();
    f.seed();
    auto p1=backend.allocateRecordingPayload(f.recording,0x3000);
    backend.beginRecordingPayload(p1,4,mask(),mask());
    auto p2=backend.allocateRecordingPayload(second,0x3000);
    backend.beginRecordingPayload(p2,4,mask(),mask());
    auto c1=std::make_shared<ReplayControl>(),c2=std::make_shared<ReplayControl>();
    auto o1=f.owner(),o2=f.owner(true);
    const Constants red{1,0,0,1,0,0,0,0},green{0,1,0,1,0,0,0,0};
    f.append(p1,o1,red,c1);f.append(p2,o2,green,c2);
    checkBatch(backend,{p1,p2},"Recording");
    {
        const auto r1=backend.recordingPayloadReceipt(p1);
        require(r1.state==NativeRecordingPayloadState::Recording && r1.recordedDraws==1 &&
            r1.ownedDataBytes==sizeof(Constants) && !r1.executions,"Recording batch stage accounting mismatch");
    }
    backend.finishRecordingPayload(p1);backend.finishRecordingPayload(p2);
    checkBatch(backend,{p1,p2},"Sealed");
    {
        const auto r1=backend.recordingPayloadReceipt(p1);
        require(r1.state==NativeRecordingPayloadState::Sealed,"Sealed batch stage did not seal");
    }
    backend.executeRecordingPayload(p1);backend.executeRecordingPayload(p2);
    f.pixels(1);
    checkBatch(backend,{p1,p2},"Executed");
    {
        const auto r1=backend.recordingPayloadReceipt(p1),r2=backend.recordingPayloadReceipt(p2);
        require(r1.executions==1 && r1.executedDraws==1 && r2.executions==1 && r2.executedDraws==1,
            "Executed batch stage counts did not update");
    }
    backend.executeRecordingPayload(p1);
    checkBatch(backend,{p1,p2},"Re-executed");
    {
        const auto r1=backend.recordingPayloadReceipt(p1);
        require(r1.executions==2 && r1.executedDraws==2,"Re-executed batch stage counts did not update fresh");
    }

    const Snapshot stable(f.immediate());
    const auto drawsBefore=backend.recordingDrawCount(),executedBefore=backend.recordingExecutedDrawCount();
    const auto colorAfterExec=backend.readbackTarget(f.target),depthAfterExec=backend.readbackDepthTarget(f.depth);
    auto unchanged=[&](const char* stage){
        require(Snapshot(f.immediate())==stable,"Rejected batch changed immediate bindings");
        require(backend.recordingDrawCount()==drawsBefore && backend.recordingExecutedDrawCount()==executedBefore,
            "Rejected batch changed GPU counters");
        require(backend.readbackTarget(f.target)==colorAfterExec && backend.readbackDepthTarget(f.depth)==depthAfterExec,
            "Rejected batch changed GPU pixels");
        (void)stage;
    };
    {
        std::vector<std::shared_ptr<NativeRecordingPayload>> emptyItems;
        std::vector<NativeRecordingReceipt> emptyOut;
        backend.recordingPayloadReceipts(emptyItems,emptyOut);
        require(emptyOut.empty(),"Empty batch output mismatch");
        unchanged("empty");
    }
    {
        std::vector<std::shared_ptr<NativeRecordingPayload>> one={p1};
        std::vector<NativeRecordingReceipt> zero,two(2);
        rejects([&]{backend.recordingPayloadReceipts(one,zero);});
        rejects([&]{backend.recordingPayloadReceipts(one,two);});
        std::vector<std::shared_ptr<NativeRecordingPayload>> emptyItems;
        std::vector<NativeRecordingReceipt> oneOut(1);
        rejects([&]{backend.recordingPayloadReceipts(emptyItems,oneOut);});
        unchanged("size mismatch");
    }
    {
        std::vector<std::shared_ptr<NativeRecordingPayload>> withNull={p1,nullptr};
        std::vector<NativeRecordingReceipt> out(2);
        rejects([&]{backend.recordingPayloadReceipts(withNull,out);});
        unchanged("null");
    }
    NativeBackend foreign(!hardware);
    {
        std::vector<std::shared_ptr<NativeRecordingPayload>> items={p1,p2};
        std::vector<NativeRecordingReceipt> out(2);
        rejects([&]{foreign.recordingPayloadReceipts(items,out);});
        auto fctx=foreign.createRecordingContext();
        auto fp=foreign.allocateRecordingPayload(fctx,0x3000);
        std::vector<std::shared_ptr<NativeRecordingPayload>> foreignItems={fp};
        std::vector<NativeRecordingReceipt> single(1);
        rejects([&]{backend.recordingPayloadReceipts(foreignItems,single);});
        foreign.releaseRecordingPayload(fp);
        foreign.releaseRecordingContext(fctx);
        unchanged("foreign backend");
    }
    {
        auto tmp=backend.allocateRecordingPayload(second,0x3000);
        backend.releaseRecordingPayload(tmp);
        std::vector<std::shared_ptr<NativeRecordingPayload>> items={tmp};
        std::vector<NativeRecordingReceipt> out(1);
        rejects([&]{backend.recordingPayloadReceipts(items,out);});
        unchanged("released item");
    }
    {
        std::atomic<bool> wrongRejected=false;
        std::thread other([&]{try{
            std::vector<std::shared_ptr<NativeRecordingPayload>> items={p1,p2};
            std::vector<NativeRecordingReceipt> out(2);
            backend.recordingPayloadReceipts(items,out);
        }catch(const Error& error){
            wrongRejected=std::string(error.what()).starts_with("Native graphics immediate context used from a different thread");
        }});
        other.join();
        require(wrongRejected,"Batch accepted another thread");
        unchanged("wrong thread");
    }
    {
        ComPtr<ID3D11DeviceContext> original=NativeRecordingProbe::saveImmediate(backend);
        backend.validateSubmissionContext();
        ComPtr<ID3D11DeviceContext> foreignImmediate(NativeRecordingProbe::immediate(foreign));
        NativeRecordingProbe::setImmediate(backend,foreignImmediate);
        {
            std::vector<std::shared_ptr<NativeRecordingPayload>> items={p1,p2};
            std::vector<NativeRecordingReceipt> out(2);
            rejects([&]{backend.recordingPayloadReceipts(items,out);});
        }
        NativeRecordingProbe::setImmediate(backend,original);
        backend.validateSubmissionContext();
        checkBatch(backend,{p1,p2},"Restored context");
        unchanged("changed immediate context");
    }
    backend.waitIdle();NativeRecordingProbe::retire(backend);
    backend.releaseRecordingPayload(p1);backend.releaseRecordingPayload(p2);
    backend.releaseRecordingContext(second);
    f.seed();f.clear();
}
}

int main(int argc,char** argv) {
    try {
        const bool hardware=argc==2 && std::string(argv[1])=="--hardware";
        if(argc>2 || (argc==2 && !hardware))throw Error("Usage: RecordingPayloadTests [--hardware]");
        Fixture fixture(hardware);lifecycleAndPixels(fixture);expandedSnapshotCapacityAndPixels(fixture);rejectionAndDiscard(fixture,hardware);
        batchReceipts(fixture,hardware);
        retirementAndLifetime(fixture,hardware);fixture.backend.waitIdle();fixture.debugErrors();
        fixture.backend.releaseRecordingContext(fixture.recording);fixture.backend.clearBindings();
        std::printf("PASS recording payload %s: %zu checks; actual deferred draws/finish/execute, fresh per-draw replay data, native accounting and lifetime\n",
            hardware?"hardware":"WARP",payloadChecks);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"Recording payload failure: %s\n",error.what());return 1;}
}
