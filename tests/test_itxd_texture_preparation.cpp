#include "runtime/itxd_texture_preparation.h"
#include "renderer/itxd_blocks.h"
#include "renderer/d3d_call_stats.h"
#include <d3d11_4.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <future>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

using Simpsons::ITXDTexturePreparation;
using namespace Simpsons::Graphics;
using namespace std::chrono_literals;
namespace {
using Preparation=ITXDTexturePreparation;
size_t checks{};
void need(bool value,const char* why){++checks;if(!value)throw Error(why);}
template<class F>void rejects(F&& f,const char* why){
    bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}need(rejected,why);
}
template<class Predicate>void eventually(Predicate predicate,const char* why) {
    const auto end=std::chrono::steady_clock::now()+5s;
    while(!predicate()) {
        if(std::chrono::steady_clock::now()>=end)need(false,why);
        std::this_thread::sleep_for(1ms);
    }
    need(true,why);
}
struct Gate {
    std::mutex mutex;std::condition_variable wake;bool open{};size_t entered{};
    void wait(){std::unique_lock lock(mutex);++entered;wake.notify_all();wake.wait(lock,[&]{return open;});}
    bool started(){std::lock_guard lock(mutex);return entered!=0;}
    void release(){std::lock_guard lock(mutex);open=true;wake.notify_all();}
};
struct ReleaseGate {std::shared_ptr<Gate> gate;~ReleaseGate(){gate->release();}};
auto views(const Preparation::Levels& levels) {
    std::vector<std::span<const uint8_t>> result;result.reserve(levels.size());
    for(const auto& level:levels)result.emplace_back(level);return result;
}
size_t bytes(const Preparation::Levels& levels){size_t result{};for(const auto& level:levels)result+=level.size();return result;}
struct Fixture {
    Preparation::Request request;std::vector<uint8_t> input;
    static Fixture rgba(uint32_t extent=512,uint32_t maxLevel=0) {
        Fixture result;auto& r=result.request;r.key={1,1};r.width=r.height=extent;r.format=TextureFormat::RGBA8;
        r.descriptor={0x80000002u|((extent/32)<<22),0x86,((extent-1)<<13)|(extent-1),0xC14,
                      maxLevel<<6,maxLevel?(extent*extent*4|0xA00u):0x200u};
        size_t size{};for(uint32_t level=0;level<=maxLevel;++level)size+=size_t(std::max(32u,extent>>level))*std::max(32u,extent>>level)*4;
        result.input.resize(size);result.pattern();return result;
    }
    static Fixture bc(TextureFormat format,uint32_t extent=128,uint32_t maxLevel=0) {
        Fixture result;auto& r=result.request;r.key={1,1};r.width=r.height=extent;r.format=format;
        const auto raw=format==TextureFormat::BC1?0x52u:format==TextureFormat::BC2?0x53u:0x54u;
        const auto block=raw==0x52?8u:16u,pitch=std::max(128u,extent);
        const auto packed=uint32_t(std::bit_width(extent)-1>4?std::bit_width(extent)-5:0);
        size_t size{},base{};
        for(uint32_t level=0;level<=(maxLevel?std::min(maxLevel,packed):0);++level) {
            const auto w=level?std::max(1u,extent>>level):pitch,h=std::max(1u,extent>>level);
            const auto stored=(size_t((((w+3)/4)+31)&~31u)*((((h+3)/4)+31)&~31u)*block+4095)&~size_t(4095);
            if(!level)base=stored;size+=stored;
        }
        r.descriptor={0x80000002u|((pitch/32)<<22),raw,((extent-1)<<13)|(extent-1),0xD10,maxLevel<<6,
                      maxLevel?(uint32_t(base)|0xA00u):0x200u};
        result.input.resize(size);result.pattern();return result;
    }
    void pattern(){for(size_t i=0;i<input.size();++i)input[i]=uint8_t((i*37)^(i>>7)^(i>>13)^0xA5);}
    Preparation::Levels decode() const {
        const auto& r=request;
        return r.format==TextureFormat::RGBA8?decodeITXDCandyRGBA8(r.descriptor,r.width,r.height,input):
            decodeITXDBCMipChain(r.descriptor,r.descriptor[1],r.width,r.height,input);
    }
};
auto waitReady(Preparation& worker,const Preparation::Handle& handle,const Preparation::Request& expected) {
    std::optional<Preparation::Ready> result;
    eventually([&]{auto ready=worker.takeReady(handle,expected);if(ready)result.emplace(std::move(*ready));return result.has_value();},
               "Prepared texture did not become ready");return result;
}
// A no-GPU upload model permits exact queue/resource-cap probes without
// allocating thousands of driver resources. Real creation is tested separately.
auto modelUpload(std::atomic<size_t>* destroyed=nullptr,std::atomic<size_t>* ownerDestructions=nullptr) {
    const auto owner=GetCurrentThreadId();
    return [destroyed,ownerDestructions,owner](const Preparation::Request& r,const Preparation::Levels&){
        auto result=std::shared_ptr<Texture>(new Texture,[destroyed,ownerDestructions,owner](Texture* t){
            delete t;if(destroyed)++*destroyed;if(ownerDestructions&&GetCurrentThreadId()==owner)++*ownerDestructions;
        });
        result->width=r.width;result->height=r.height;result->format=r.format;return result;
    };
}
}
namespace Simpsons::Graphics {
struct NativeImmutableMipFactoryTestProbe {
    static auto threadSafeDevice(const ComPtr<ID3D11Device>& device) {
        need(device&&!(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED),"Fixture device is not thread-safe");
        need(!d3dCallStatsInstalled(),"Fixture cannot overlap installed D3D counters");
        return std::shared_ptr<const NativeImmutableMipFactory>(new NativeImmutableMipFactory(device));
    }
};
struct NativeMipTextureProbe {
    static ComPtr<ID3D11Device> device(const std::shared_ptr<Texture>& t){ComPtr<ID3D11Device> result;t->texture->GetDevice(&result);return result;}
    static bool proofEmpty(const std::shared_ptr<Texture>& t){return !t->storageProof.texture&&!t->storageProof.view&&!t->storageProof.device;}
    static D3D11_TEXTURE2D_DESC descriptor(const std::shared_ptr<Texture>& t){D3D11_TEXTURE2D_DESC result{};t->texture->GetDesc(&result);return result;}
    static void exactView(const std::shared_ptr<Texture>& t,const D3D11_TEXTURE2D_DESC& desc) {
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};t->view->GetDesc(&view);ComPtr<ID3D11Resource> backing;t->view->GetResource(&backing);
        need(backing.Get()==t->texture.Get()&&view.Format==desc.Format&&view.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D&&
             !view.Texture2D.MostDetailedMip&&view.Texture2D.MipLevels==desc.MipLevels,"Prepared SRV does not expose exact backing mips");
    }
    static ComPtr<ID3D11ShaderResourceView> view(const std::shared_ptr<Texture>& t){return t->view;}
    static std::vector<uint8_t> retainedBCReadback(const std::shared_ptr<Texture>& t,uint32_t level) {
        auto owner=device(t);ComPtr<ID3D11DeviceContext> context;owner->GetImmediateContext(&context);
        auto desc=descriptor(t);need(level<desc.MipLevels&&t->format==TextureFormat::BC1,"Retained BC fixture level/format differs");
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;need(SUCCEEDED(owner->CreateTexture2D(&desc,nullptr,&staging)),"Retained texture staging creation failed");
        context->CopySubresourceRegion(staging.Get(),level,0,0,0,t->texture.Get(),level,nullptr);
        const auto rowBytes=((std::max(1u,t->width>>level)+3)/4)*8,rows=(std::max(1u,t->height>>level)+3)/4;
        std::vector<uint8_t> result(size_t(rowBytes)*rows);D3D11_MAPPED_SUBRESOURCE mapped{};
        need(SUCCEEDED(context->Map(staging.Get(),level,D3D11_MAP_READ,0,&mapped)),"Retained texture mapping failed");
        if(!mapped.pData||mapped.RowPitch<rowBytes){context->Unmap(staging.Get(),level);throw Error("Retained texture row layout differs");}
        for(uint32_t row=0;row<rows;++row)std::memcpy(result.data()+size_t(row)*rowBytes,
            static_cast<const uint8_t*>(mapped.pData)+size_t(row)*mapped.RowPitch,rowBytes);
        context->Unmap(staging.Get(),level);return result;
    }
};
}
namespace {
DXGI_FORMAT dxgi(TextureFormat format) {
    switch(format){case TextureFormat::BC1:return DXGI_FORMAT_BC1_UNORM;case TextureFormat::BC2:return DXGI_FORMAT_BC2_UNORM;
        case TextureFormat::BC3:return DXGI_FORMAT_BC3_UNORM;default:return DXGI_FORMAT_R8G8B8A8_UNORM;}
}
void exactGPU(NativeBackend& backend,const ComPtr<ID3D11Device>& device,const Preparation::Ready& ready,
              const Preparation::Request& request,const Preparation::Levels& expected) {
    const auto& texture=ready.texture();need(texture&&ready.request()==request,"Ready request/texture identity changed");
    need(texture->width==request.width&&texture->height==request.height&&texture->format==request.format&&
         texture->levelCount()==expected.size(),"Prepared wrapper format/extent/mips differ");
    need(NativeMipTextureProbe::proofEmpty(texture),"Worker wrote backend validation state");
    const auto desc=NativeMipTextureProbe::descriptor(texture);
    need(desc.Width==request.width&&desc.Height==request.height&&desc.MipLevels==expected.size()&&desc.ArraySize==1&&
         desc.Format==dxgi(request.format)&&desc.Usage==D3D11_USAGE_IMMUTABLE&&desc.BindFlags==D3D11_BIND_SHADER_RESOURCE&&
         !desc.CPUAccessFlags&&!desc.MiscFlags&&desc.SampleDesc.Count==1&&!desc.SampleDesc.Quality,"Prepared GPU creation descriptor differs");
    need(NativeMipTextureProbe::device(texture).Get()==device.Get(),"Prepared GPU texture belongs to another device");
    NativeMipTextureProbe::exactView(texture,desc);backend.validateTexture(texture);
    for(uint32_t level=0;level<expected.size();++level)
        need(backend.readbackMip(texture,level)==expected[level],"Existing pure decoder and actual GPU mip bytes differ");
}
// Object-local observation; no process-wide counter installation. The factory
// uses only CreateTexture2D/CreateSRV. The existing main context stays untouched.
template<class Interface>bool sameDeviceInterface(const ComPtr<ID3D11Device>& device) {
    ComPtr<Interface> actual;const auto result=device.As(&actual);
    if(result==E_NOINTERFACE)return false;
    need(SUCCEEDED(result),"Device interface qualification failed");
    return static_cast<void*>(actual.Get())==static_cast<void*>(device.Get());
}
size_t qualifiedDeviceVtableSize(const ComPtr<ID3D11Device>& device) {
    // Public Windows SDK26100 vtables, including inherited IUnknown methods.
    // The driver's CreateTexture2D validator dispatches slot53; a base-only
    // clone truncates a newer same-pointer interface. Query before any hook.
    if(sameDeviceInterface<ID3D11Device5>(device))return 69;
    if(sameDeviceInterface<ID3D11Device4>(device))return 67;
    if(sameDeviceInterface<ID3D11Device3>(device))return 65;
    if(sameDeviceInterface<ID3D11Device2>(device))return 54;
    if(sameDeviceInterface<ID3D11Device1>(device))return 50;
    return 43;
}
struct DeviceCalls {
    ComPtr<ID3D11Device> device;void** original{};std::vector<void*> table;
    std::atomic<size_t> textures{},views{},workerImmediate{},wrongThread{};
    std::atomic<DWORD> creationThread{};
    DWORD owner=GetCurrentThreadId();std::shared_ptr<Gate> uploadGate;
    static inline DeviceCalls* active{};
    static HRESULT STDMETHODCALLTYPE createTexture(ID3D11Device* d,const D3D11_TEXTURE2D_DESC* desc,
              const D3D11_SUBRESOURCE_DATA* data,ID3D11Texture2D** texture) {
        auto& s=*active;
        if(desc&&desc->Usage==D3D11_USAGE_IMMUTABLE&&desc->BindFlags==D3D11_BIND_SHADER_RESOURCE) {
            ++s.textures;s.creationThread=GetCurrentThreadId();if(GetCurrentThreadId()==s.owner)++s.wrongThread;
            if(s.uploadGate)s.uploadGate->wait();
        }
        using Method=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const D3D11_TEXTURE2D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture2D**);
        return reinterpret_cast<Method>(s.original[5])(d,desc,data,texture);
    }
    static HRESULT STDMETHODCALLTYPE createView(ID3D11Device* d,ID3D11Resource* resource,
              const D3D11_SHADER_RESOURCE_VIEW_DESC* desc,ID3D11ShaderResourceView** view) {
        auto& s=*active;++s.views;if(GetCurrentThreadId()==s.owner)++s.wrongThread;
        using Method=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,ID3D11Resource*,const D3D11_SHADER_RESOURCE_VIEW_DESC*,ID3D11ShaderResourceView**);
        return reinterpret_cast<Method>(s.original[7])(d,resource,desc,view);
    }
    static void STDMETHODCALLTYPE immediate(ID3D11Device* d,ID3D11DeviceContext** context) {
        auto& s=*active;if(GetCurrentThreadId()!=s.owner)++s.workerImmediate;
        using Method=void(STDMETHODCALLTYPE*)(ID3D11Device*,ID3D11DeviceContext**);reinterpret_cast<Method>(s.original[40])(d,context);
    }
    explicit DeviceCalls(ComPtr<ID3D11Device> owned,std::shared_ptr<Gate> gate={}):device(std::move(owned)),uploadGate(std::move(gate)) {
        need(!active,"Nested device observer");table.resize(qualifiedDeviceVtableSize(device));
        original=*reinterpret_cast<void***>(device.Get());std::copy_n(original,table.size(),table.begin());
        table[5]=reinterpret_cast<void*>(createTexture);table[7]=reinterpret_cast<void*>(createView);table[40]=reinterpret_cast<void*>(immediate);
        active=this;*reinterpret_cast<void***>(device.Get())=table.data();
    }
    ~DeviceCalls(){*reinterpret_cast<void***>(device.Get())=original;active=nullptr;}
    DeviceCalls(const DeviceCalls&)=delete;DeviceCalls& operator=(const DeviceCalls&)=delete;
};
std::shared_ptr<const NativeImmutableMipFactory> qualifiedFactory(NativeBackend& backend,const ComPtr<ID3D11Device>& device) {
    D3D11_FEATURE_DATA_THREADING threading{};
    const bool eligible=!(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)&&!d3dCallStatsInstalled()&&
        SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_THREADING,&threading,sizeof(threading)))&&threading.DriverConcurrentCreates;
    const auto production=backend.immutableMipPreparationFactory();need(bool(production)==eligible,"Production concurrent-create gate disagrees with actual device");
    if(production)return production;
    std::printf("Production concurrent-create gate declined this driver; exercising sealed factory with TU-only thread-safe device proof\n");
    Preparation unavailable(production);auto fixture=Fixture::bc(TextureFormat::BC1);
    need(unavailable.submit(fixture.request,fixture.input).expired()&&!unavailable.stats().available,"Unavailable production gate admitted speculative work");
    return NativeImmutableMipFactoryTestProbe::threadSafeDevice(device);
}
void exactOwnedGPU(NativeBackend& backend,NativeBackend& foreign,const ComPtr<ID3D11Device>& device,
                   const std::shared_ptr<const NativeImmutableMipFactory>& factory) {
    ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);
    const std::array<uint8_t,4> pixel{1,2,3,4};auto anchor=backend.createTexture(1,1,TextureFormat::RGBA8,pixel);
    auto anchorView=NativeMipTextureProbe::view(anchor);context->PSSetShaderResources(7,1,anchorView.GetAddressOf());
    DeviceCalls calls(device);
    for(auto fixture:{Fixture::bc(TextureFormat::BC1,128,7),Fixture::bc(TextureFormat::BC2,128,0),
                      Fixture::bc(TextureFormat::BC3,256,8),Fixture::rgba(512,5),Fixture::rgba(1024,0)}) {
        const auto expected=fixture.decode();const auto request=fixture.request;auto gate=std::make_shared<Gate>();
        Preparation worker(factory,{},[gate](const auto&){gate->wait();});ReleaseGate release{gate};
        DWORD producerThread{};auto producer=std::async(std::launch::async,[&]{producerThread=GetCurrentThreadId();return worker.submit(request,fixture.input);});
        const auto submitStatus=producer.wait_for(1s);if(submitStatus!=std::future_status::ready)gate->release();
        auto handle=producer.get();need(submitStatus==std::future_status::ready,"Producer waited for background decode/upload");
        need(!handle.expired(),"Original producer thread failed to submit owned snapshot");eventually([&]{return gate->started();},"Worker did not begin owned snapshot");
        const auto reservation=worker.stats().cpuReservedBytes;
        need(reservation>=fixture.input.size()+bytes(expected)+4096,"CPU reservation omits source/output/bookkeeping");
        std::fill(fixture.input.begin(),fixture.input.end(),0);fixture.input.clear();fixture.input.shrink_to_fit();fixture.request.descriptor.fill(0);
        need(!worker.takeReady(handle,request),"Running conversion was exposed to demand");gate->release();auto ready=waitReady(worker,handle,request);
        eventually([&]{return worker.stats().cpuReservedBytes==0&&worker.stats().pendingJobs==0;},"Completed GPU result retained CPU reservation");
        need(worker.stats().gpuReservedBytes==bytes(expected)&&worker.stats().gpuReservedResources==1,"GPU reservation differs from authored mip bytes/resources");
        exactGPU(backend,device,*ready,request,expected);
        need(calls.creationThread!=producerThread,"Original producer thread called the graphics device");
        rejects([&]{foreign.readbackMip(ready->texture(),0);},"Foreign device consumed prepared GPU texture");
        need(!worker.takeReady(handle,request),"Prepared texture was consumed twice");
        ComPtr<ID3D11ShaderResourceView> bound;context->PSGetShaderResources(7,1,&bound);need(bound.Get()==anchorView.Get(),"Device-only worker changed immediate shader binding");
        ready.reset();eventually([&]{return worker.stats().gpuReservedResources==0;},"Taken result failed to retire GPU reservation");worker.stop();
    }
    need(calls.textures==5&&calls.views==5&&!calls.workerImmediate&&!calls.wrongThread,"Worker creation used owner/context or missed actual device creation");
    ID3D11ShaderResourceView* none{};context->PSSetShaderResources(7,1,&none);
}
void exactIdentityAndTryLock(const std::shared_ptr<const NativeImmutableMipFactory>& factory) {
    auto fixture=Fixture::bc(TextureFormat::BC1);Preparation worker(factory);auto handle=worker.submit(fixture.request,fixture.input);
    eventually([&]{return worker.stats().ready==1;},"Identity result did not become ready");
    for(size_t field=0;field<10;++field) {
        auto wrong=fixture.request;
        if(field<6)wrong.descriptor[field]^=1;
        else if(field==6)++wrong.key.record;else if(field==7)++wrong.key.generation;else if(field==8)++wrong.width;else wrong.format=TextureFormat::BC3;
        need(!worker.takeReady(handle,wrong),"Mismatched request consumed ready texture");
    }
    auto wrong=fixture.request;++wrong.height;need(!worker.takeReady(handle,wrong),"Mismatched height consumed ready texture");
    need(worker.submit(fixture.request,fixture.input).expired()&&worker.stats().rejectedDuplicate==1,"Duplicate outstanding generation was admitted");
    Preparation foreign(factory);need(!foreign.takeReady(handle,fixture.request),"Foreign helper consumed another owner's ticket");foreign.cancel(handle);
    auto gate=std::make_shared<Gate>();std::jthread locker([&]{worker.testingWithStateLock([&]{gate->wait();});});ReleaseGate release{gate};
    eventually([&]{return gate->started();},"State lock fixture did not start");
    auto demand=std::async(std::launch::async,[&]{return worker.takeReady(handle,fixture.request).has_value();});
    const auto status=demand.wait_for(1s);gate->release();locker.join();
    need(status==std::future_status::ready&&!demand.get(),"Demand waited for locked worker state");
    need(worker.stats().takeBusy>=1&&worker.stats().ready==1,"Busy/mismatched demand changed result ownership");
    auto ready=waitReady(worker,handle,fixture.request);need(bool(ready->texture()),"Matching owner lost ready result");
}
void cancellationAndInFlightDriver(const ComPtr<ID3D11Device>& device,const std::shared_ptr<const NativeImmutableMipFactory>& factory) {
    auto fixture=Fixture::bc(TextureFormat::BC2);auto gate=std::make_shared<Gate>();DeviceCalls calls(device,gate);
    Preparation worker(factory,{64u*1024u*1024u,64u*1024u*1024u,2,2});ReleaseGate release{gate};
    auto old=worker.submit(fixture.request,fixture.input);eventually([&]{return gate->started();},"Real driver creation did not reach gate");
    auto secondRequest=fixture.request;++secondRequest.key.record;auto queued=worker.submit(secondRequest,fixture.input);
    need(!queued.expired(),"Queued cancellation fixture was rejected");const auto cpu=worker.stats().cpuReservedBytes,gpu=worker.stats().gpuReservedBytes;
    worker.cancel(queued);worker.cancel(old);need(!worker.takeReady(old,fixture.request),"Cancelled driver call became visible");
    // Cancellation is nonblocking and the running call still owns both leases.
    need(worker.stats().running==1&&worker.stats().cpuReservedBytes==cpu&&worker.stats().gpuReservedBytes==gpu,
         "Cancellation freed live driver/input reservation on the caller");
    std::atomic<bool> joined{};std::jthread stopper([&]{worker.stop();joined=true;});ReleaseGate stopRelease{gate};
    eventually([&]{return worker.stats().stopped;},"Stop did not mark in-flight worker stopped");
    need(!joined.load()&&worker.stats().gpuReservedResources==1,"Stop joined/released a blocked driver call prematurely");
    gate->release();stopper.join();need(joined&&worker.stats().cpuReservedBytes==0&&worker.stats().gpuReservedBytes==0&&
        worker.stats().pendingJobs==0&&worker.stats().gpuReservedResources==0,"Joined worker retained cancelled resources");
    need(!worker.takeReady(queued,secondRequest)&&!worker.takeReady(old,fixture.request),"Stop published stale generation");
    need(calls.textures==1&&!calls.workerImmediate,"Cancelled queued job entered the device");worker.stop();
}
void generationsCoexist(NativeBackend& backend,const ComPtr<ID3D11Device>& device,
                       const std::shared_ptr<const NativeImmutableMipFactory>& factory) {
    auto fixture=Fixture::bc(TextureFormat::BC1);fixture.request.key={17,1};const auto expected=fixture.decode();
    auto gate=std::make_shared<Gate>();Preparation worker(factory,{},[gate](const auto& r){if(r.key.generation==1)gate->wait();});ReleaseGate release{gate};
    auto old=worker.submit(fixture.request,fixture.input);eventually([&]{return gate->started();},"Old generation did not enter decode gate");
    worker.cancel(old);auto freshRequest=fixture.request;freshRequest.key.generation=2;
    auto fresh=worker.submit(freshRequest,fixture.input);need(!fresh.expired()&&worker.stats().pendingJobs==2,"Fresh generation could not coexist with cancelled running predecessor");
    need(!worker.takeReady(old,freshRequest)&&!worker.takeReady(fresh,fixture.request),"Queued generation consumed another request's payload");
    gate->release();eventually([&]{return worker.stats().ready==1&&worker.stats().pendingJobs==0;},"Fresh generation did not survive predecessor retirement");
    worker.cancel(old);need(!worker.takeReady(old,freshRequest)&&!worker.takeReady(fresh,fixture.request)&&worker.stats().ready==1,
        "Stale handle cancellation or demand changed fresh generation ownership");
    auto ready=waitReady(worker,fresh,freshRequest);exactGPU(backend,device,*ready,freshRequest,expected);
    ready.reset();eventually([&]{return worker.stats().gpuReservedResources==0&&worker.stats().cpuReservedBytes==0;},"Coexisting generation reservations failed to retire");
}
size_t cpuCharge(const Fixture& fixture) {
    auto gate=std::make_shared<Gate>();Preparation worker({},modelUpload(),[gate](const auto&){gate->wait();});ReleaseGate release{gate};
    const auto handle=worker.submit(fixture.request,fixture.input);need(!handle.expired(),"Charge fixture rejected");
    eventually([&]{return gate->started();},"Charge fixture did not start");const auto charge=worker.stats().cpuReservedBytes;
    need(charge>=fixture.input.size()+bytes(fixture.decode())+4096,"CPU charge does not bound owned inputs/output");gate->release();worker.stop();return charge;
}
void boundedCPUAndProducers() {
    auto fixture=Fixture::bc(TextureFormat::BC1,4);const auto charge=cpuCharge(fixture);auto gate=std::make_shared<Gate>();
    Preparation worker({charge*3,64u*1024u*1024u,3,1024},modelUpload(),[gate](const auto&){gate->wait();});ReleaseGate release{gate};
    std::array<Preparation::Handle,12> handles;std::array<std::jthread,12> producers;
    for(size_t i=0;i<producers.size();++i)producers[i]=std::jthread([&,i]{auto r=fixture.request;r.key.record=uint32_t(i+1);handles[i]=worker.submit(r,fixture.input);});
    for(auto& producer:producers)producer.join();eventually([&]{return gate->started();},"Concurrent worker did not start");
    need(worker.stats().submitted==3&&worker.stats().pendingJobs==3&&worker.stats().cpuReservedBytes==charge*3,"Concurrent producers exceeded exact CPU/job bounds");
    void* inaccessible=VirtualAlloc(nullptr,fixture.input.size(),MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);need(inaccessible!=nullptr,"No-access input allocation failed");
    struct Free {void* value;~Free(){VirtualFree(value,0,MEM_RELEASE);}} free{inaccessible};
    auto next=fixture.request;next.key.record=99;
    need(worker.submit(next,{static_cast<const uint8_t*>(inaccessible),fixture.input.size()}).expired(),"CPU budget rejection touched inaccessible input");
    Preparation tooSmall({charge-1,64u*1024u*1024u,128,1024},modelUpload());
    need(tooSmall.submit(next,{static_cast<const uint8_t*>(inaccessible),fixture.input.size()}).expired()&&
        tooSmall.stats().cpuReservedBytes==0&&tooSmall.stats().rejectedCpuBudget==1,"One-byte-short CPU bound copied inaccessible input");
    gate->release();worker.stop();need(worker.stats().cpuReservedBytes==0&&worker.stats().pendingJobs==0,"Stopped producer queue retained CPU memory");
    for(const bool byteBound:{false,true}) {
        auto large=byteBound?Fixture::rgba(1024,6):fixture;const auto each=cpuCharge(large);auto capGate=std::make_shared<Gate>();
        Preparation capped({std::numeric_limits<size_t>::max(),std::numeric_limits<size_t>::max(),1000,10000},modelUpload(),[capGate](const auto&){capGate->wait();});ReleaseGate capRelease{capGate};
        const size_t maximum=std::min(size_t(128),size_t(64u*1024u*1024u)/each);
        for(size_t i=0;i<maximum+2;++i){auto r=large.request;r.key.record=uint32_t(i+1);capped.submit(r,large.input);}
        need(capped.stats().pendingJobs==maximum&&capped.stats().cpuReservedBytes==each*maximum&&
             capped.stats().highWaterPendingJobs<=128&&capped.stats().highWaterCpuBytes<=64u*1024u*1024u,"Caller limits escaped hard production CPU caps");
        capGate->release();capped.stop();
    }
}
void GPUReservationsAndEviction() {
    auto fixture=Fixture::bc(TextureFormat::BC1,4);const auto authored=bytes(fixture.decode());std::atomic<size_t> destroyed{},ownerDestructions{};
    Preparation worker({64u*1024u*1024u,authored*2,128,2},modelUpload(&destroyed,&ownerDestructions));
    auto first=worker.submit(fixture.request,fixture.input);auto pinned=waitReady(worker,first,fixture.request);
    auto secondRequest=fixture.request;++secondRequest.key.record;auto second=worker.submit(secondRequest,fixture.input);
    eventually([&]{return worker.stats().ready==1;},"Second GPU result did not become ready");
    auto thirdRequest=fixture.request;thirdRequest.key.record=3;auto third=worker.submit(thirdRequest,fixture.input);
    eventually([&]{return worker.stats().completed==3;},"Replacement result did not complete");
    need(worker.stats().evicted==1&&worker.stats().gpuReservedResources==2&&worker.stats().gpuReservedBytes==authored*2&&
         !worker.takeReady(second,secondRequest)&&pinned->texture()->width==4,"Oldest ready eviction lost pinned lease or exceeded exact GPU bound");
    worker.cancel(third);eventually([&]{return worker.stats().gpuReservedResources==1;},"Ready cancellation did not retire on worker");
    need(destroyed==2&&ownerDestructions==0,"Active eviction/cancellation destroyed GPU payloads on demand thread");
    worker.stop();need(worker.stats().gpuReservedBytes==authored&&worker.stats().gpuReservedResources==1,"Stop freed demand-pinned result");
    pinned.reset();need(worker.stats().gpuReservedBytes==0&&worker.stats().gpuReservedResources==0&&destroyed==3,"Post-stop pinned teardown leaked resources");

    auto admissionGate=std::make_shared<Gate>();
    Preparation saturated({64u*1024u*1024u,authored,128,1},modelUpload(),
        [admissionGate](const Preparation::Request& request){if(request.key.record==2)admissionGate->wait();});
    ReleaseGate releaseAdmission{admissionGate};
    auto kept=saturated.submit(fixture.request,fixture.input);auto held=waitReady(saturated,kept,fixture.request);
    auto blocked=fixture.request;blocked.key.record=2;auto failed=saturated.submit(blocked,fixture.input);
    eventually([&]{return admissionGate->started();},"Pinned GPU saturation did not reach admitted decode boundary");
    const auto admitted=saturated.stats();
    need(!failed.expired()&&admitted.submitted==2&&admitted.rejected==0&&admitted.pendingJobs==1&&
         admitted.running==1&&admitted.cpuReservedBytes!=0,"Pinned GPU saturation incorrectly rejected CPU admission");
    admissionGate->release();
    eventually([&]{return saturated.stats().failed==1&&saturated.stats().pendingJobs==0;},"Pinned saturation did not fail speculatively");
    need(saturated.stats().rejectedGpuBudget==1&&saturated.stats().evicted==0&&saturated.stats().gpuReservedBytes==authored&&
         saturated.stats().gpuReservedResources==1&&!saturated.takeReady(failed,blocked),"Pinned GPU reservation was evicted or exceeded");
    held.reset();eventually([&]{return saturated.stats().gpuReservedResources==0;},"Demand retirement did not free saturated reservation");
    auto retry=saturated.submit(blocked,fixture.input);auto recovered=waitReady(saturated,retry,blocked);
    need(bool(recovered->texture()),"Freed GPU capacity was not reusable");

    Preparation hard({std::numeric_limits<size_t>::max(),std::numeric_limits<size_t>::max(),1000,10000},modelUpload());
    std::vector<Preparation::Handle> handles;handles.reserve(1025);
    for(uint32_t i=0;i<1025;++i) {
        auto r=fixture.request;r.key.record=i+1;handles.push_back(hard.submit(r,fixture.input));need(!handles.back().expired(),"Hard GPU cap fixture admission failed");
        eventually([&]{return hard.stats().completed==size_t(i)+1;},"Hard GPU resource fixture did not complete");
    }
    need(hard.stats().ready==1024&&hard.stats().gpuReservedResources==1024&&hard.stats().gpuReservedBytes==authored*1024&&
         hard.stats().highWaterGpuResources<=1024&&hard.stats().evicted==1&&!hard.takeReady(handles.front(),fixture.request),"Caller limits escaped 1024-resource production cap");
    hard.stop();need(hard.stats().gpuReservedResources==0,"Stopped hard resource cap retained results");

    const auto large=Fixture::bc(TextureFormat::BC3,2048);const auto largeBytes=bytes(large.decode());
    Preparation byteCap({std::numeric_limits<size_t>::max(),std::numeric_limits<size_t>::max(),1000,10000},modelUpload());
    const auto maximum=size_t(64u*1024u*1024u)/largeBytes;
    for(size_t i=0;i<maximum+1;++i){auto r=large.request;r.key.record=uint32_t(i+1);byteCap.submit(r,large.input);
        eventually([&]{return byteCap.stats().completed==i+1;},"Hard GPU byte-cap result did not complete");}
    need(byteCap.stats().gpuReservedBytes==maximum*largeBytes&&byteCap.stats().highWaterGpuBytes<=64u*1024u*1024u&&
         byteCap.stats().evicted==1,"Caller limits escaped authored GPU byte cap");
}
void speculativeErrorsAndFallback(NativeBackend& backend,const std::shared_ptr<const NativeImmutableMipFactory>& factory) {
    auto fixture=Fixture::bc(TextureFormat::BC3);const auto expected=fixture.decode();
    for(const bool cpuFailure:{false,true}) {
        auto admissionGate=std::make_shared<Gate>();
        Preparation::TestUpload upload=cpuFailure?Preparation::TestUpload(modelUpload()):
            Preparation::TestUpload([](const auto&,const auto&)->std::shared_ptr<Texture>{throw Error("injected upload failure");});
        Preparation worker({},std::move(upload),
            [admissionGate,cpuFailure](const auto&){admissionGate->wait();if(cpuFailure)throw Error("injected decode failure");});
        ReleaseGate releaseAdmission{admissionGate};
        auto handle=worker.submit(fixture.request,fixture.input);
        eventually([&]{return admissionGate->started();},"Failure fixture did not reach admitted decode boundary");
        const auto admitted=worker.stats();
        need(!handle.expired()&&admitted.submitted==1&&admitted.rejected==0&&admitted.pendingJobs==1&&
             admitted.running==1&&admitted.cpuReservedBytes!=0,"Failure fixture was not admitted");
        admissionGate->release();
        eventually([&]{const auto s=worker.stats();return s.failed==1&&s.pendingJobs==0&&s.cpuReservedBytes==0&&s.gpuReservedBytes==0;},"Speculative exception escaped or retained pending lease");
        need(!worker.takeReady(handle,fixture.request)&&worker.stats().cpuReservedBytes==0&&worker.stats().gpuReservedBytes==0,"Failed upload/decode published or leaked resources");
        const auto synchronous=fixture.decode();need(synchronous==expected,"Speculative failure changed legacy synchronous decoder");
        const auto spans=views(synchronous);const auto fallback=backend.createTextureMipChain(fixture.request.width,fixture.request.height,fixture.request.format,spans);
        for(uint32_t level=0;level<expected.size();++level)need(backend.readbackMip(fallback,level)==expected[level],"Synchronous GPU fallback changed bytes after speculative failure");
    }
    Preparation nullUpload({},[](const auto&,const auto&)->std::shared_ptr<Texture>{return {};});
    auto handle=nullUpload.submit(fixture.request,fixture.input);eventually([&]{const auto s=nullUpload.stats();return s.failed==1&&s.cpuReservedBytes==0&&s.gpuReservedBytes==0;},"Null device result was published");
    need(!nullUpload.takeReady(handle,fixture.request),"Null upload became demand-ready");
    for(size_t field=0;field<6;++field) {
        auto bad=fixture;bad.request.key.record=uint32_t(field+1);bad.request.descriptor[field]^=1;
        rejects([&]{bad.decode();},"Malformed descriptor changed legacy decoder's original rejection");
        Preparation worker(factory);auto rejected=worker.submit(bad.request,bad.input);
        if(!rejected.expired())eventually([&]{return worker.stats().failed==1;},"Malformed speculative profile failed to retire");
        need(!worker.takeReady(rejected,bad.request),"Malformed profile became ready");
    }
    Preparation worker(factory);auto unsupported=fixture.request;unsupported.format=TextureFormat::R8;
    need(worker.submit(unsupported,fixture.input).expired(),"Unsupported scalar/luminance format entered immutable preparation");
    auto truncated=fixture.input;truncated.pop_back();auto truncatedRequest=fixture.request;truncatedRequest.key.record=99;
    auto truncatedHandle=worker.submit(truncatedRequest,truncated);
    if(!truncatedHandle.expired())eventually([&]{return worker.stats().failed==1;},"Truncated storage failed to retire");
    need(!worker.takeReady(truncatedHandle,truncatedRequest),"Truncated storage became ready");
    auto smallRGBA=Fixture::rgba(256);need(worker.submit(smallRGBA.request,smallRGBA.input).expired(),"Small RGBA profile escaped the large-output gate");
    auto zero=fixture.request;zero.key.record=0;need(worker.submit(zero,fixture.input).expired(),"Null original record entered preparation");
    zero=fixture.request;zero.key.generation=0;need(worker.submit(zero,fixture.input).expired(),"Unowned generation entered preparation");
    auto huge=fixture.request;huge.width=4096;need(worker.submit(huge,fixture.input).expired(),"Unbounded dimensions entered preparation");
}
void idleNotificationRace() {
    const auto fixture=Fixture::bc(TextureFormat::BC1,4);
    for(const bool demandRetirement:{false,true}) {
        std::atomic<size_t> destroyed{},ownerDestructions{};
        Preparation worker({},modelUpload(&destroyed,&ownerDestructions));auto gate=std::make_shared<Gate>();ReleaseGate release{gate};
        auto handle=worker.submit(fixture.request,fixture.input);eventually([&]{return worker.stats().ready==1;},"Idle race result did not become ready");
        std::optional<Preparation::Ready> receipt;
        if(demandRetirement){auto ready=waitReady(worker,handle,fixture.request);receipt.emplace(std::move(*ready));}
        auto once=std::make_shared<std::atomic<bool>>(false);
        worker.testingBeforeIdleWait([gate,once]{if(!once->exchange(true))gate->wait();});
        eventually([&]{return gate->started();},"Worker did not pause between idle scan and atomic wait");
        if(demandRetirement)receipt.reset();else worker.cancel(handle);
        need(worker.stats().gpuReservedResources==1,"Caller synchronously destroyed idle worker payload");
        // This gate releases only its own condition variable. Do not submit,
        // stop or install another hook until the already-notified work retires.
        gate->release();eventually([&]{return worker.stats().gpuReservedResources==0;},"Idle scan/wait gap lost cancellation or demand-retirement notification");
        need(destroyed==1&&ownerDestructions==0&&worker.stats().ready==0,"Idle retirement used demand thread or retained speculative ready state");
    }
}
void deviceLifetime(bool hardware) {
    std::optional<Preparation::Ready> pinned;ComPtr<ID3D11Device> device;
    const auto fixture=Fixture::bc(TextureFormat::BC1,128,7);const auto expected=fixture.decode();
    {
        NativeBackend backend(!hardware);const std::array<uint8_t,4> pixel{0,0,0,0};auto anchor=backend.createTexture(1,1,TextureFormat::RGBA8,pixel);
        device=NativeMipTextureProbe::device(anchor);auto factory=qualifiedFactory(backend,device);
        {
            Preparation worker(factory);auto handle=worker.submit(fixture.request,fixture.input);auto ready=waitReady(worker,handle,fixture.request);
            exactGPU(backend,device,*ready,fixture.request,expected);pinned.emplace(std::move(*ready));worker.stop();
            need(worker.stats().gpuReservedResources==1,"Stop released a live demand texture");
        }
        factory.reset();anchor.reset();device.Reset();
    }
    // Drop the receipt too, eliminating its retained State/factory. Only the
    // published texture's device children may keep this original device alive.
    auto texture=pinned->texture();pinned.reset();
    device=NativeMipTextureProbe::device(texture);need(device&&SUCCEEDED(device->GetDeviceRemovedReason()),"Transferred texture lost device lifetime");
    const auto desc=NativeMipTextureProbe::descriptor(texture);need(desc.Width==128&&desc.MipLevels==8,"Retired worker changed immutable backing");
    NativeMipTextureProbe::exactView(texture,desc);device.Reset();
    for(uint32_t level=0;level<expected.size();++level)need(NativeMipTextureProbe::retainedBCReadback(texture,level)==expected[level],
        "Published GPU bytes changed after worker/factory/backend/receipt retirement");
}
}
int main(int argc,char** argv){try {
    const bool hardware=argc==2&&std::string(argv[1])=="--hardware";
    need(argc==1||hardware,"Usage: ITXDTexturePreparationTests [--hardware]");
    NativeBackend backend(!hardware),foreign(!hardware);const std::array<uint8_t,4> pixel{0,0,0,0};
    auto anchor=backend.createTexture(1,1,TextureFormat::RGBA8,pixel);auto device=NativeMipTextureProbe::device(anchor);
    const auto factory=qualifiedFactory(backend,device);need(backend.matchesImmutableMipPreparationFactory(factory)&&
        !foreign.matchesImmutableMipPreparationFactory(factory)&&!backend.matchesImmutableMipPreparationFactory({}),"Factory device ownership check differs");
    exactOwnedGPU(backend,foreign,device,factory);exactIdentityAndTryLock(factory);cancellationAndInFlightDriver(device,factory);
    generationsCoexist(backend,device,factory);
    boundedCPUAndProducers();GPUReservationsAndEviction();speculativeErrorsAndFallback(backend,factory);idleNotificationRace();deviceLifetime(hardware);
    std::printf("PASS %zu ITXD immutable preparation checks (%s): owned pure-decoder GPU bytes, leases, request/device isolation, bounds, cancellation, nonblocking demand and fallback\n",checks,hardware?"hardware":"WARP");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL ITXD immutable preparation after %zu checks: %s\n",checks,e.what());return 1;}}
