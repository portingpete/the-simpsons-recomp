#include "renderer/native_backend.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
#include <thread>

using namespace Simpsons::Graphics;
size_t recordingChecks{};
void require(bool value,const char* reason) {++recordingChecks;if(!value) throw Error(reason);}
template<class F> void rejects(F action) {
    try {action();} catch(const Error&) {return;}
    throw Error("Invalid native recording ownership operation did not fail");
}
// Reuse native Get* snapshots, without executing or changing the binding test.
#include "header/test_engine_binding_reset.h"

namespace Simpsons::Graphics {
struct NativeRecordingProbe {
    using Snapshot=EngineBindingResetProbe::Snapshot;
    static void checked(HRESULT result,const char* reason) {require(SUCCEEDED(result),reason);}
    template<class F> static void rejectUnchanged(NativeBackend& backend,F action,const char* prefix) {
        const Snapshot before(backend.context.Get());
        const auto draws=backend.screenDrawCount(),presents=backend.presentationCount();
        bool rejected=false;
        try {action();} catch(const Error& error) {rejected=std::string(error.what()).starts_with(prefix);}
        require(rejected,"Recording operation did not reject with its expected ownership reason");
        require(Snapshot(backend.context.Get())==before,"Rejected recording ownership operation changed immediate bindings");
        require(backend.screenDrawCount()==draws && backend.presentationCount()==presents,
                "Rejected recording ownership operation submitted a draw/presentation");
    }
    static void actualIdentity(NativeBackend& backend,const std::shared_ptr<NativeRecordingContext>& owner) {
        auto* deferred=backend.checkedRecordingContext(owner);
        require(deferred && deferred!=backend.context.Get(),"Recording owner returned the immediate context");
        require(deferred->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED,"Recording owner lacks a real deferred context");
        require(deferred->GetContextFlags()==0,"Recording context has nonzero reserved flags");
        ComPtr<ID3D11Device> actual;deferred->GetDevice(&actual);
        require(actual.Get()==backend.device.Get(),"Deferred context was created by a different device");
        require(backend.context->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE,"Recording creation replaced the immediate context");
    }
    static void seedBindings(NativeBackend& backend,const std::shared_ptr<RenderTarget>& color,
                             const std::shared_ptr<DepthTarget>& depth) {
        // Synthetic existing screen pipeline supplies actual VS/PS/layout/VB.
        // No method in the recording service records or executes this draw.
        ScreenDraw draw{};draw.vertices={{{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,-1,1,1}}};
        draw.color={0,0,0,1};draw.blendSelector=3;draw.colorWriteMask=15;backend.drawScreen(color,draw);
        backend.bindTargets({color,nullptr,nullptr,nullptr},depth);
        auto* context=backend.context.Get();
        ComPtr<ID3D11Buffer> vertex,constant;UINT stride{},offset{};
        context->IAGetVertexBuffers(0,1,&vertex,&stride,&offset);context->PSGetConstantBuffers(0,1,&constant);
        require(vertex && constant,"Recording test did not seed real buffers");
        auto* v=vertex.Get();context->IASetVertexBuffers(3,1,&v,&stride,&offset);context->IASetVertexBuffers(31,1,&v,&stride,&offset);
        std::array<uint32_t,16> pixels{};
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=4;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{pixels.data(),16,64};ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> sampled;
        checked(backend.device->CreateTexture2D(&desc,&data,&texture),"Recording fixture texture creation failed");
        checked(backend.device->CreateShaderResourceView(texture.Get(),nullptr,&sampled),"Recording fixture SRV creation failed");
        D3D11_SAMPLER_DESC samplerDesc{};samplerDesc.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDesc.AddressU=samplerDesc.AddressV=samplerDesc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxAnisotropy=1;samplerDesc.ComparisonFunc=D3D11_COMPARISON_ALWAYS;samplerDesc.MaxLOD=D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        checked(backend.device->CreateSamplerState(&samplerDesc,&sampler),"Recording fixture sampler creation failed");
        std::array<ID3D11ShaderResourceView*,128> reads{};reads.fill(sampled.Get());
        std::array<ID3D11SamplerState*,16> samplers{};samplers.fill(sampler.Get());
        std::array<ID3D11Buffer*,14> constants{};constants.fill(constant.Get());
#define RECORDING_SEED(STAGE) \
        context->STAGE##SetShaderResources(0,128,reads.data()); \
        context->STAGE##SetSamplers(0,16,samplers.data()); \
        context->STAGE##SetConstantBuffers(0,14,constants.data())
        RECORDING_SEED(PS);RECORDING_SEED(VS);RECORDING_SEED(GS);
        RECORDING_SEED(HS);RECORDING_SEED(DS);RECORDING_SEED(CS);
#undef RECORDING_SEED
        std::array<D3D11_VIEWPORT,2> viewports{{{1.5f,2.5f,18,9,0.25f,0.75f},{0,0,8,8,0,1}}};
        std::array<D3D11_RECT,2> scissors{{{2,3,11,15},{0,1,5,6}}};
        context->RSSetViewports(2,viewports.data());context->RSSetScissorRects(2,scissors.data());
        context->SetPredication(nullptr,TRUE);
    }
    static void backendLifetime(bool hardware) {
        // Reuse the EXACT C++ backend address, while the old deferred owner
        // keeps its original device alive. Pointer identity alone is insufficient.
        alignas(NativeBackend) std::byte storage[sizeof(NativeBackend)];
        auto destroy=[](NativeBackend* backend){std::destroy_at(backend);};
        std::unique_ptr<NativeBackend,decltype(destroy)> backend(
            std::construct_at(reinterpret_cast<NativeBackend*>(storage),!hardware),destroy);
        auto retained=backend->createRecordingContext();
        auto* deferred=backend->checkedRecordingContext(retained); // Borrowed, no independent AddRef.
        backend.reset();
        require(deferred->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED,"Deferred owner depended on destroyed backend memory");
        {ComPtr<ID3D11Device> actual;deferred->GetDevice(&actual);
         require(actual && SUCCEEDED(actual->GetDeviceRemovedReason()),"Retained deferred context lost its real device");}
        backend.reset(std::construct_at(reinterpret_cast<NativeBackend*>(storage),!hardware));
        rejectUnchanged(*backend,[&]{backend->validateRecordingContext(retained);},"Native recording context belongs to another graphics device");
        rejectUnchanged(*backend,[&]{backend->releaseRecordingContext(retained);},"Native recording context belongs to another graphics device");
        auto fresh=backend->createRecordingContext();actualIdentity(*backend,fresh);
        backend->releaseRecordingContext(fresh);backend.reset();
        require(deferred->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED,"Replacement backend teardown damaged original deferred owner");
        // Ordinary opaque shared_ptr destruction after both backends are gone
        // releases COM ownership without calling either expired backend address.
        std::weak_ptr<NativeRecordingContext> weak=retained;retained.reset();
        require(weak.expired(),"Orphaned recording wrapper retained an unexpected owner");
    }
    static void run(bool hardware) {
        NativeBackend backend(!hardware),foreign(!hardware);
        auto color=backend.createTarget(32,16,TargetFormat::RGB10A2);auto depth=backend.createDepthTarget(32,16);
        seedBindings(backend,color,depth);backend.clearTarget(color,{0.25f,0.5f,0.75f,1});backend.clearDepthTarget(depth,0.5f,0x73);
        const auto colorBefore=backend.readbackTarget(color),depthBefore=backend.readbackDepthTarget(depth);
        const Snapshot before(backend.context.Get()),foreignBefore(foreign.context.Get());
        const auto draws=backend.screenDrawCount(),presents=backend.presentationCount();
        auto recording=backend.createRecordingContext();actualIdentity(backend,recording);
        const NativeBackend& query=backend;query.validateRecordingContext(recording);
        auto alias=recording;
        rejectUnchanged(backend,[&]{backend.validateRecordingContext(nullptr);},"Native recording context owner is missing");
        rejectUnchanged(backend,[&]{backend.releaseRecordingContext(nullptr);},"Native recording context owner is missing");
        rejectUnchanged(foreign,[&]{foreign.validateRecordingContext(recording);},"Native recording context belongs to another backend owner");
        rejectUnchanged(foreign,[&]{foreign.releaseRecordingContext(recording);},"Native recording context belongs to another backend owner");
        actualIdentity(backend,recording); // Foreign release must not destroy the real owner.
        const std::array<std::function<void()>,3> threadCalls={
            [&]{backend.createRecordingContext();},[&]{backend.validateRecordingContext(recording);},[&]{backend.releaseRecordingContext(recording);}};
        for(const auto& call:threadCalls) {
            std::atomic<bool> rejected=false;
            std::thread thread([&]{try {call();} catch(const Error& error) {
                rejected=std::string(error.what()).starts_with("Native graphics immediate context used from a different thread");
            }});thread.join();require(rejected,"Native recording operation accepted a foreign thread");
            actualIdentity(backend,recording);
        }
        std::vector<std::shared_ptr<NativeRecordingContext>> simultaneous;
        std::vector<ID3D11DeviceContext*> contexts;
        for(size_t i=0;i<8;++i) {
            auto next=backend.createRecordingContext();actualIdentity(backend,next);
            auto* raw=backend.checkedRecordingContext(next);
            require(raw!=backend.checkedRecordingContext(recording) && std::find(contexts.begin(),contexts.end(),raw)==contexts.end(),
                    "Separate native recording owners share one deferred context");
            contexts.push_back(raw);simultaneous.push_back(std::move(next));
        }
        backend.releaseRecordingContext(recording);
        rejectUnchanged(backend,[&]{backend.validateRecordingContext(alias);},"Native recording context was explicitly released");
        rejectUnchanged(backend,[&]{backend.releaseRecordingContext(alias);},"Native recording context was explicitly released");
        for(const auto& next:simultaneous) {actualIdentity(backend,next);backend.releaseRecordingContext(next);}
        simultaneous.clear();recording.reset();alias.reset();
        for(size_t i=0;i<32;++i) {
            auto next=backend.createRecordingContext();actualIdentity(backend,next);
            std::weak_ptr<NativeRecordingContext> weak=next;backend.releaseRecordingContext(next);next.reset();
            require(weak.expired(),"Released recording wrapper was retained unexpectedly");
        }
        // RAII final-reference destruction does not call the backend/context
        // mutators; COM release remains safe even on a different host thread.
        auto automatic=backend.createRecordingContext();std::weak_ptr<NativeRecordingContext> weak=automatic;
        std::thread dispose([owned=std::move(automatic)]() mutable {owned.reset();});dispose.join();
        require(weak.expired(),"RAII recording destruction retained its wrapper");
        require(Snapshot(backend.context.Get())==before && Snapshot(foreign.context.Get())==foreignBefore,
                "Deferred ownership operations changed immediate context bindings");
        ComPtr<ID3D11Predicate> predicate;BOOL predicateValue{};backend.context->GetPredication(&predicate,&predicateValue);
        require(!predicate && predicateValue==TRUE,"Deferred ownership changed immediate predication");
        require(backend.readbackTarget(color)==colorBefore && backend.readbackDepthTarget(depth)==depthBefore,
                "Deferred ownership changed immediate color/depth contents");
        require(backend.screenDrawCount()==draws && backend.presentationCount()==presents,
                "Deferred ownership submitted rendering or presentation");
        backendLifetime(hardware);backend.clearBindings();
        std::printf("PASS native recording ownership %s: %zu checks; real deferred contexts, owner/device/type/release/lifetime; no recording, finish or replay implemented\n",
                    hardware?"hardware":"WARP",recordingChecks);
    }
};
}
int main(int argc,char** argv) {
    try {NativeRecordingProbe::run(argc==2 && std::string(argv[1])=="--hardware");return 0;}
    catch(const std::exception& error) {std::fprintf(stderr,"Native recording ownership failure: %s\n",error.what());return 1;}
}
