// Independent bind-only probe: the draw never calls recordRigidMesh or binds
// an SRV. Otherwise the rigid draw's own bindings could conceal a broken hook.
#include "renderer/native_backend.h"
#include "VSFlat.h"
#include "PSRecordingDepthBinding.h"
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

using namespace Simpsons::Graphics;
size_t depthBindingChecks{};
void require(bool value,const char* reason) {
    ++depthBindingChecks;if(!value)throw Error(reason);
}
template<class F> void rejects(F action) {
    bool rejected=false;try {action();}catch(const Error&){rejected=true;}
    require(rejected,"Invalid recording depth binding did not reject");
}
#include "header/test_engine_binding_reset.h"

namespace Simpsons::Graphics {
struct NativeRecordingProbe {
    static ID3D11Device* device(NativeBackend& backend){return backend.device.Get();}
    static ID3D11DeviceContext* immediate(NativeBackend& backend){return backend.context.Get();}
    static void draw(NativeBackend& backend,const std::shared_ptr<NativeRecordingPayload>& payload,
        std::span<const uint8_t> data,const std::shared_ptr<void>& owner,
        const std::function<void(ID3D11DeviceContext*)>& callback) {
        backend.recordRecordingDraw(payload,data,owner,{},callback);
    }
    static size_t pending(const NativeBackend& backend){return backend.pendingRecordingPayloads.size();}
};
}

namespace {
using Snapshot=EngineBindingResetProbe::Snapshot;
constexpr uint32_t width=8,height=4;
constexpr std::array<float,4> untouched={0.125f,0.375f,0.625f,0.875f};
void hr(HRESULT result,const char* reason){require(SUCCEEDED(result),reason);}

struct DrawOwner {
    // These exact owned vertex bytes are the recording receipt's data. There
    // are deliberately no shadow owners or SRVs here or in the draw callback.
    std::array<float,8> positions={-1,1,-1,-1,1,1,1,-1};
    ComPtr<ID3D11Buffer> vertices;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depthState;
};

void actualDraw(ID3D11DeviceContext* deferred,const std::shared_ptr<DrawOwner>& owner) {
    require(deferred&&deferred->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED,
            "Bind-only probe draw reached the immediate context");
    auto* vertices=owner->vertices.Get();const UINT stride=2*sizeof(float),offset=0;
    deferred->IASetVertexBuffers(0,1,&vertices,&stride,&offset);
    deferred->IASetInputLayout(owner->layout.Get());
    deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    deferred->RSSetState(owner->raster.Get());
    deferred->OMSetDepthStencilState(owner->depthState.Get(),0);
    deferred->OMSetBlendState(nullptr,nullptr,0xFFFFFFFF);
    // Targets, viewport/scissor, VSFlat and the independent PS came from begin.
    // The ONLY texture bindings in this command list came from the API under
    // test. In particular, do not add PSSetShaderResources here.
    deferred->Draw(4,0);
}

struct Fixture {
    NativeBackend backend;
    std::shared_ptr<NativeRecordingContext> recording;
    std::shared_ptr<RenderTarget> output,sentinel;
    std::shared_ptr<DepthTarget> outputDepth,sourceDepth;
    std::array<std::shared_ptr<DepthTarget>,2> copied;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;

    explicit Fixture(bool hardware):backend(!hardware) {
        auto* device=NativeRecordingProbe::device(backend);
        hr(device->CreateVertexShader(kVSFlat,sizeof(kVSFlat),nullptr,&vertex),"Probe VS creation failed");
        hr(device->CreatePixelShader(kPSRecordingDepthBinding,sizeof(kPSRecordingDepthBinding),nullptr,&pixel),
           "Probe PS creation failed");
        output=backend.createTarget(width,height,TargetFormat::RGBA32Float);
        sentinel=backend.createTarget(width,height,TargetFormat::RGBA32Float);
        outputDepth=backend.createDepthTarget(width,height);
        sourceDepth=backend.createDepthTarget(1024,1024);
        for(auto& value:copied)value=backend.createDepthTarget(1024,1024);
        // Exercise the actual source-selected depth-copy operation. Never
        // initialize the destination with the expected values directly.
        backend.bindTargets({},sourceDepth);
        const std::array<float,2> values={0.25f,0.75f};
        for(size_t i=0;i<copied.size();++i) {
            backend.clearDepthTarget(copied[i],0,0);
            backend.clearDepthTarget(sourceDepth,values[i],uint8_t(0x31+i));
            const auto receipt=backend.copyDepth(sourceDepth,copied[i]);
            backend.waitCopy(receipt);
        }
        recording=backend.createRecordingContext();
        backend.clearTarget(output,untouched);backend.clearTarget(sentinel,{1,0,1,1});
        backend.clearDepthTarget(outputDepth,0.625f,0x79);
        seed();
    }
    ID3D11DeviceContext* immediate(){return NativeRecordingProbe::immediate(backend);}
    void seed() {
        backend.bindTargets({output,nullptr,nullptr,nullptr},outputDepth);
        immediate()->VSSetShader(vertex.Get(),nullptr,0);immediate()->PSSetShader(pixel.Get(),nullptr,0);
        const D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};
        const D3D11_RECT scissor{0,0,LONG(width),LONG(height)};
        immediate()->RSSetViewports(1,&viewport);immediate()->RSSetScissorRects(1,&scissor);
        // Nonnull, unrelated immediate SRVs expose an accidental immediate
        // bind. They must remain unchanged throughout recording and playback.
        for(uint32_t stage=0;stage<2;++stage) {
            const std::array<uint8_t,4> rgba={uint8_t(19+stage),67,113,255};
            auto texture=backend.createTexture(1,1,TextureFormat::RGBA8,rgba);
            backend.bindEngineTexture(stage,texture);
        }
    }
    std::shared_ptr<DrawOwner> drawOwner() {
        auto result=std::make_shared<DrawOwner>();auto* device=NativeRecordingProbe::device(backend);
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(sizeof(result->positions));
        desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        const D3D11_SUBRESOURCE_DATA data{result->positions.data(),0,0};
        hr(device->CreateBuffer(&desc,&data,&result->vertices),"Probe vertex upload failed");
        const D3D11_INPUT_ELEMENT_DESC input{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        hr(device->CreateInputLayout(&input,1,kVSFlat,sizeof(kVSFlat),&result->layout),"Probe input layout failed");
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;
        raster.DepthClipEnable=TRUE;raster.ScissorEnable=TRUE;
        hr(device->CreateRasterizerState(&raster,&result->raster),"Probe raster state failed");
        D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=FALSE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
        depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
        depth.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};
        depth.BackFace=depth.FrontFace;
        hr(device->CreateDepthStencilState(&depth,&result->depthState),"Probe depth state failed");
        return result;
    }
    void unrelatedImmediate() {
        backend.bindTargets({sentinel,nullptr,nullptr,nullptr},nullptr);
        immediate()->VSSetShader(nullptr,nullptr,0);immediate()->PSSetShader(nullptr,nullptr,0);
        immediate()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        const D3D11_VIEWPORT viewport{1,1,2,2,0.25f,0.75f};const D3D11_RECT scissor{1,1,2,2};
        immediate()->RSSetViewports(1,&viewport);immediate()->RSSetScissorRects(1,&scissor);
    }
    std::vector<uint8_t> depthPixels() {
        const auto raw=backend.readbackDepthTarget(outputDepth);
        require(raw.size()==size_t(width)*height*8,"Probe depth readback extent differs");
        std::vector<uint8_t> result(size_t(width)*height*5);
        // D32/S8 are meaningful; the remaining X24 bytes are not components.
        for(size_t i=0;i<size_t(width)*height;++i)std::memcpy(result.data()+5*i,raw.data()+8*i,5);
        return result;
    }
    void pixels() {
        const auto result=backend.readbackTarget(output);
        require(result.size()==size_t(width)*height*16,"Probe readback extent differs");
        constexpr std::array<float,4> expected={0.25f,0.75f,0.5f,1};
        for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x)for(size_t lane=0;lane<4;++lane) {
            uint32_t value{};std::memcpy(&value,result.data()+16*(size_t(y)*width+x)+4*lane,4);
            if(value!=std::bit_cast<uint32_t>(expected[lane])) {
                std::fprintf(stderr,"Depth binding pixel %u,%u lane %zu: got %08X expected %08X\n",
                    x,y,lane,value,std::bit_cast<uint32_t>(expected[lane]));
                require(false,"Independent GPU probe found a missing, swapped or changed depth binding");
            }
            ++depthBindingChecks;
        }
    }
};

template<class F> void rejectedUnchanged(Fixture& f,F action) {
    const Snapshot before(f.immediate());
    const auto recorded=f.backend.recordingDrawCount(),executed=f.backend.recordingExecutedDrawCount();
    rejects(action);
    require(Snapshot(f.immediate())==before,"Rejected bind changed the immediate pipeline");
    require(f.backend.recordingDrawCount()==recorded&&f.backend.recordingExecutedDrawCount()==executed,
            "Rejected bind changed native draw accounting");
}

void run(Fixture& f,bool hardware) {
    auto& backend=f.backend;
    NativeBackend foreign(!hardware);
    const auto foreignDepth=foreign.createDepthTarget(1024,1024);
    const auto foreignContext=foreign.createRecordingContext();
    const auto foreignPayload=foreign.allocateRecordingPayload(foreignContext,0x3000);
    const auto undersized=backend.createDepthTarget(32,32);
    auto unready=backend.allocateRecordingPayload(f.recording,0x3000);
    rejectedUnchanged(f,[&]{backend.bindRigidShadowDepth(unready,0,f.copied[0]);});
    require(backend.recordingPayloadReceipt(unready).state==NativeRecordingPayloadState::Allocated,
            "Unready bind advanced payload state");
    backend.releaseRecordingPayload(unready);
    rejectedUnchanged(f,[&]{backend.bindRigidShadowDepth(unready,0,f.copied[0]);}); // Stale alias.
    rejectedUnchanged(f,[&]{backend.bindRigidShadowDepth({},0,f.copied[0]);});
    rejectedUnchanged(f,[&]{backend.bindRigidShadowDepth(foreignPayload,0,f.copied[0]);});
    foreign.releaseRecordingPayload(foreignPayload);foreign.releaseRecordingContext(foreignContext);

    const Snapshot initial(f.immediate());
    const auto outputBefore=backend.readbackTarget(f.output),depthBefore=f.depthPixels();
    const auto recordedBefore=backend.recordingDrawCount(),executedBefore=backend.recordingExecutedDrawCount();
    auto payload=backend.allocateRecordingPayload(f.recording,0x3000);
    // The probe consumes no inherited constants. Preserve flags4 while using
    // empty masks so no artificial preparation callback or CB upload is needed.
    backend.beginRecordingPayload(payload,4,{},{});
    backend.bindRigidShadowDepth(payload,1,f.copied[1]);
    backend.bindRigidShadowDepth(payload,0,f.copied[0]); // Reverse call order must not reverse slots.
    rejectedUnchanged(f,[&]{backend.bindRigidShadowDepth(payload,2,f.copied[1]);});
    rejectedUnchanged(f,[&]{backend.bindRigidShadowDepth(payload,UINT32_MAX,f.copied[1]);});
    rejectedUnchanged(f,[&]{backend.bindRigidShadowDepth(payload,0,{});});
    rejectedUnchanged(f,[&]{backend.bindRigidShadowDepth(payload,0,undersized);});
    rejectedUnchanged(f,[&]{backend.bindRigidShadowDepth(payload,0,foreignDepth);});
    bool wrongThread=false;
    std::thread worker([&]{try {backend.bindRigidShadowDepth(payload,0,f.copied[1]);}
                          catch(const Error&){wrongThread=true;}});
    worker.join();require(wrongThread,"Depth bind accepted a foreign thread");
    rejectedUnchanged(f,[&]{backend.finishRecordingPayload(payload);});
    const auto bound=backend.recordingPayloadReceipt(payload);
    require(bound.state==NativeRecordingPayloadState::Recording&&bound.capacityBytes==0x3000&&bound.flags==4&&
            bound.inputMask==NativeRecordingMask{}&&bound.outputMask==NativeRecordingMask{}&&
            bound.ownedDataBytes==0&&bound.recordedDraws==0&&bound.executedDraws==0&&bound.executions==0,
            "Texture-only bindings fabricated work or empty finish damaged the payload");
    require(backend.recordingDrawCount()==recordedBefore&&backend.recordingExecutedDrawCount()==executedBefore,
            "Texture-only bindings changed global draw counts");
    require(Snapshot(f.immediate())==initial&&backend.readbackTarget(f.output)==outputBefore&&
            f.depthPixels()==depthBefore,"Bind-only phase changed immediate state or pixels");

    // Neither the fixture draw owner nor any copy receipt retains these
    // wrappers. Correct pixels now require native deferred/command-list COM
    // ownership of the actual copied textures. The source is gone too.
    f.copied={};f.sourceDepth.reset();
    auto owner=f.drawOwner();std::weak_ptr<DrawOwner> drawLease=owner;
    const auto data=std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(owner->positions.data()),sizeof(owner->positions));
    NativeRecordingProbe::draw(backend,payload,data,owner,[owner](auto* deferred){actualDraw(deferred,owner);});
    backend.finishRecordingPayload(payload);owner.reset();
    const auto sealed=backend.recordingPayloadReceipt(payload);
    require(sealed.state==NativeRecordingPayloadState::Sealed&&sealed.recordedDraws==1&&sealed.executedDraws==0&&
            sealed.executions==0&&sealed.ownedDataBytes==8*sizeof(float),"Probe draw receipt differs from its owned vertex data");
    rejectedUnchanged(f,[&]{backend.bindRigidShadowDepth(payload,0,foreignDepth);});
    require(Snapshot(f.immediate())==initial&&backend.readbackTarget(f.output)==outputBefore&&
            f.depthPixels()==depthBefore,"Recording or finish executed the probe early");
    // Remove the deferred context itself before execution. Only the sealed
    // command list can supply the two textures; the draw owns neither one.
    backend.releaseRecordingContext(f.recording);f.recording.reset();
    f.unrelatedImmediate();const Snapshot beforeExecute(f.immediate());
    const auto sentinelBefore=backend.readbackTarget(f.sentinel);
    backend.executeRecordingPayload(payload);
    const auto executed=backend.recordingPayloadReceipt(payload);
    require(executed.executions==1&&executed.executedDraws==1&&executed.recordedDraws==1&&
            backend.recordingDrawCount()==recordedBefore+1&&backend.recordingExecutedDrawCount()==executedBefore+1,
            "Probe execution counts do not match its one real draw");
    std::weak_ptr<NativeRecordingPayload> pending=payload;payload.reset();
    require(!pending.expired()&&!drawLease.expired()&&NativeRecordingProbe::pending(backend)==1,
            "Dropping caller owners lost the submitted command list or draw resources");
    backend.waitIdle();
    require(pending.expired()&&drawLease.expired()&&NativeRecordingProbe::pending(backend)==0,
            "Completed probe payload did not retire its owned resources");
    require(Snapshot(f.immediate())==beforeExecute,"Probe execution failed to restore immediate bindings");
    require(backend.readbackTarget(f.sentinel)==sentinelBefore&&f.depthPixels()==depthBefore,
            "Bind-only probe changed unrelated color or depth/stencil pixels");
    f.pixels();backend.clearBindings();
}
}

int main(int argc,char** argv) {
    try {
        const bool hardware=argc==2&&std::string(argv[1])=="--hardware";
        if(argc>2||(argc==2&&!hardware))throw Error("Usage: RecordingDepthBindingTests [--hardware]");
        Fixture fixture(hardware);run(fixture,hardware);
        std::printf("PASS recording depth binding %s: %zu checks; copied .25/.75, independent t0/t1 Load, no SRV rebind at draw, deferred lifetime and rejection\n",
                    hardware?"hardware":"WARP",depthBindingChecks);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"Recording depth binding failure: %s\n",error.what());return 1;}
}
