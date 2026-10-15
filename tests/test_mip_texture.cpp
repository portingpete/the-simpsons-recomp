#include "renderer/native_backend.h"
#include "renderer/device_availability.h"
#include "renderer/d3d_call_stats.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <thread>
#include <utility>
using namespace Simpsons::Graphics;
namespace {
size_t checks{},sampled{};
void need(bool v,const char* why){++checks;if(!v)throw Error(why);}
template<class F> void rejects(F&& f,const char* reason){
    try{f();}catch(const Error& e){need(std::string(e.what()).find(reason)!=std::string::npos,"Unexpected mip rejection cause");return;}
    need(false,"Invalid native mip operation accepted");
}
auto views(const std::vector<std::vector<uint8_t>>& data){
    std::vector<std::span<const uint8_t>> result;for(const auto& mip:data)result.emplace_back(mip);return result;
}
auto pattern(uint32_t width,uint32_t height,uint32_t count){
    std::vector<std::vector<uint8_t>> result;
    for(uint32_t level=0;level<count;++level){
        const auto w=std::max(1u,width>>level),h=std::max(1u,height>>level);std::vector<uint8_t> bytes(w*h*4);
        for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)for(uint32_t c=0;c<4;++c)
            bytes[(y*w+x)*4+c]=uint8_t(x*(13+7*c)+y*(37+9*c)+c*47+level*59+3);
        result.push_back(std::move(bytes));
    }
    return result;
}
void exact(NativeBackend& b,const std::shared_ptr<Texture>& t,const std::vector<std::vector<uint8_t>>& expected){
    need(t->levelCount()==expected.size(),"Explicit level count changed");
    for(uint32_t i=0;i<expected.size();++i)need(b.readbackMip(t,i)==expected[i],"Uploaded native mip bytes differ");
    need(b.readback(t)==expected.front(),"Level-zero readback compatibility changed");
}
template<class Interface>bool sameDeviceInterface(const ComPtr<ID3D11Device>& device) {
    ComPtr<Interface> extended;const HRESULT result=device.As(&extended);
    if(result==E_NOINTERFACE)return false;
    need(SUCCEEDED(result),"Cannot qualify actual device interface table");
    return static_cast<void*>(extended.Get())==static_cast<void*>(device.Get());
}
size_t qualifiedDeviceVtableSize(const ComPtr<ID3D11Device>& device) {
    // Exact inherited public method counts from the SDK C vtables. Query each
    // interface before interception and copy its extent only when it aliases
    // this base pointer. CreateTexture2D can internally dispatch Device2 slot53;
    // copying only the base43 slots truncates the real device's table.
    if(sameDeviceInterface<ID3D11Device5>(device))return 69;
    if(sameDeviceInterface<ID3D11Device4>(device))return 67;
    if(sameDeviceInterface<ID3D11Device3>(device))return 65;
    if(sameDeviceInterface<ID3D11Device2>(device))return 54;
    if(sameDeviceInterface<ID3D11Device1>(device))return 50;
    return 43;
}
// Object-local qualification faults. No worker exists while this temporary
// table is installed, and every unmodified method forwards to the real device.
struct DeviceQualificationProbe {
    enum class Fault {SingleThreaded,NoConcurrentCreates,FailedQuery,Removed};
    ComPtr<ID3D11Device> device;
    void** original{};
    std::vector<void*> table;
    Fault fault;
    static inline DeviceQualificationProbe* active{};
    static UINT STDMETHODCALLTYPE creationFlags(ID3D11Device* d) {
        auto& s=*active;
        using Method=UINT(STDMETHODCALLTYPE*)(ID3D11Device*);
        const auto flags=reinterpret_cast<Method>(s.original[38])(d);
        return s.fault==Fault::SingleThreaded?flags|D3D11_CREATE_DEVICE_SINGLETHREADED:flags;
    }
    static HRESULT STDMETHODCALLTYPE threading(ID3D11Device* d,D3D11_FEATURE feature,void* data,UINT bytes) {
        auto& s=*active;
        if(feature==D3D11_FEATURE_THREADING && s.fault==Fault::FailedQuery)return E_FAIL;
        if(feature==D3D11_FEATURE_THREADING && s.fault==Fault::NoConcurrentCreates) {
            if(bytes!=sizeof(D3D11_FEATURE_DATA_THREADING))return E_INVALIDARG;
            *static_cast<D3D11_FEATURE_DATA_THREADING*>(data)={FALSE,FALSE};return S_OK;
        }
        using Method=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,D3D11_FEATURE,void*,UINT);
        return reinterpret_cast<Method>(s.original[33])(d,feature,data,bytes);
    }
    static HRESULT STDMETHODCALLTYPE removedReason(ID3D11Device* d) {
        auto& s=*active;
        if(s.fault==Fault::Removed)return DXGI_ERROR_DEVICE_REMOVED;
        using Method=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*);
        return reinterpret_cast<Method>(s.original[39])(d);
    }
    DeviceQualificationProbe(ComPtr<ID3D11Device> owned,Fault value):device(std::move(owned)),
        table(qualifiedDeviceVtableSize(device)),fault(value) {
        need(!active,"Nested device qualification probe");
        original=*reinterpret_cast<void***>(device.Get());std::copy_n(original,table.size(),table.begin());
        table[38]=reinterpret_cast<void*>(creationFlags);table[33]=reinterpret_cast<void*>(threading);
        table[39]=reinterpret_cast<void*>(removedReason);
        active=this;*reinterpret_cast<void***>(device.Get())=table.data();
    }
    ~DeviceQualificationProbe(){*reinterpret_cast<void***>(device.Get())=original;active=nullptr;}
    DeviceQualificationProbe(const DeviceQualificationProbe&)=delete;
    DeviceQualificationProbe& operator=(const DeviceQualificationProbe&)=delete;
};
}
namespace Simpsons::Graphics {
struct NativeImmutableMipFactoryTestProbe {
    // TU-only coverage on thread-safe real devices when the optional driver
    // performance gate is false; this does not qualify production preparation.
    static std::shared_ptr<const NativeImmutableMipFactory> threadSafeDevice(const ComPtr<ID3D11Device>& device) {
        need(device && !(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED),"Factory fixture device is single-threaded");
        need(!d3dCallStatsInstalled(),"Factory fixture cannot overlap installed diagnostic counters");
        return std::shared_ptr<const NativeImmutableMipFactory>(new NativeImmutableMipFactory(device));
    }
};
struct NativePresentationProbe {
    template<class F> static void removal(NativeBackend& b,F&& f) {
        const bool before=b.availability->removed.exchange(true);
        try{f();}catch(...){b.availability->removed.store(before);throw;}
        b.availability->removed.store(before);
    }
};
struct NativeMipTextureProbe {
    static ComPtr<ID3D11Device> device(const NativeBackend& b){return b.device;}
    static ComPtr<ID3D11DeviceContext> context(const NativeBackend& b){return b.context;}
    static void unvalidated(const std::shared_ptr<Texture>& t){
        need(!t->storageProof.device && !t->storageProof.texture && !t->storageProof.view,
             "Device-only factory published a backend validation proof");
    }
    static void owningDevice(const std::shared_ptr<Texture>& t,const ComPtr<ID3D11Device>& expected){
        ComPtr<ID3D11Device> actual;t->texture->GetDevice(&actual);
        need(actual.Get()==expected.Get(),"Factory texture belongs to a different device");
    }
    // Independent staging after backend destruction. This fixture owns the
    // immediate context only on main; the factory and worker never receive it.
    static std::vector<uint8_t> retainedReadback(const std::shared_ptr<Texture>& t,uint32_t level){
        ComPtr<ID3D11Device> device;t->texture->GetDevice(&device);
        ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);
        D3D11_TEXTURE2D_DESC d{};t->texture->GetDesc(&d);
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        need(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging)),"Retained-device staging creation failed");
        context->CopySubresourceRegion(staging.Get(),level,0,0,0,t->texture.Get(),level,nullptr);
        const auto w=std::max(1u,t->width>>level),h=std::max(1u,t->height>>level);
        std::vector<uint8_t> result(size_t(w)*h*4);D3D11_MAPPED_SUBRESOURCE mapped{};
        need(SUCCEEDED(context->Map(staging.Get(),level,D3D11_MAP_READ,0,&mapped)),"Retained-device readback map failed");
        if(!mapped.pData || mapped.RowPitch<w*4){context->Unmap(staging.Get(),level);throw Error("Retained-device map layout differs");}
        for(uint32_t row=0;row<h;++row)std::memcpy(result.data()+size_t(row)*w*4,
            static_cast<const uint8_t*>(mapped.pData)+size_t(row)*mapped.RowPitch,w*4);
        context->Unmap(staging.Get(),level);return result;
    }
    static void bindSentinel(const ComPtr<ID3D11DeviceContext>& context,const std::shared_ptr<Texture>& t){
        auto* view=t->view.Get();context->PSSetShaderResources(7,1,&view);
    }
    static void sentinel(const ComPtr<ID3D11DeviceContext>& context,const std::shared_ptr<Texture>& t){
        ComPtr<ID3D11ShaderResourceView> actual;context->PSGetShaderResources(7,1,&actual);
        need(actual.Get()==t->view.Get(),"Device-only creation changed an active shader binding");
    }
    static void guards(NativeBackend& b,NativeBackend& foreign,const std::shared_ptr<const NativeImmutableMipFactory>& factory){
        changed(b.context,ComPtr<ID3D11DeviceContext>{},[&]{
            rejects([&]{b.immutableMipPreparationFactory();},"actual D3D11 immediate submission context");
            rejects([&]{b.matchesImmutableMipPreparationFactory({});},"actual D3D11 immediate submission context");
        });
        changed(b.context,foreign.context,[&]{
            rejects([&]{b.immutableMipPreparationFactory();},"belongs to another device");
            rejects([&]{b.matchesImmutableMipPreparationFactory(factory);},"belongs to another device");
        });
    }
    static void replacement(NativeBackend& b,NativeBackend& foreign,const std::shared_ptr<const NativeImmutableMipFactory>& factory,
                            const std::shared_ptr<Texture>& old,const std::vector<std::vector<uint8_t>>& expected){
        changed(b.device,foreign.device,[&]{changed(b.context,foreign.context,[&]{
            need(!b.matchesImmutableMipPreparationFactory(factory),"Replaced device admitted old factory");
            rejects([&]{b.validateTexture(old);},"another graphics device");
            const auto fresh=b.createTextureMipChain(old->width,old->height,old->format,views(expected));
            owningDevice(fresh,foreign.device);exact(b,fresh,expected);
        });});
        need(b.matchesImmutableMipPreparationFactory(factory),"Restored device rejected its retained factory");
    }
    static void backing(const std::shared_ptr<Texture>& t){
        D3D11_TEXTURE2D_DESC d{};t->texture->GetDesc(&d);
        const auto format=t->format==TextureFormat::BC1?DXGI_FORMAT_BC1_UNORM:
            t->format==TextureFormat::BC2?DXGI_FORMAT_BC2_UNORM:t->format==TextureFormat::BC3?DXGI_FORMAT_BC3_UNORM:
            t->format==TextureFormat::BGRX8?DXGI_FORMAT_B8G8R8X8_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM;
        need(d.Width==t->width && d.Height==t->height && d.MipLevels==t->levelCount() && d.ArraySize==1 &&
             d.Usage==D3D11_USAGE_IMMUTABLE && d.BindFlags==D3D11_BIND_SHADER_RESOURCE && !d.CPUAccessFlags &&
             !d.MiscFlags && d.SampleDesc.Count==1 && !d.SampleDesc.Quality &&
             d.Format==format,"Actual mip backing differs");
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};t->view->GetDesc(&v);ComPtr<ID3D11Resource> resource;t->view->GetResource(&resource);
        need(resource.Get()==t->texture.Get() && v.Format==d.Format && v.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D &&
             !v.Texture2D.MostDetailedMip && v.Texture2D.MipLevels==d.MipLevels,"Mip view does not expose exactly the owned levels");
    }
    template<class T,class F> static void changed(T& field,T replacement,F&& f){auto old=field;field=replacement;try{f();}catch(...){field=old;throw;}field=old;}
    static void invalidMetadata(NativeBackend& b,const std::shared_ptr<Texture>& t){
        const auto baseline=b.readbackMip(t,1);
        for(uint32_t levels:{0u,1u,t->mipLevels+1,0xFFFFFFFFu})changed(t->mipLevels,levels,[&]{rejects([&]{b.readbackMip(t,1);},"metadata does not match");});
        changed(t->writable,true,[&]{rejects([&]{b.writeTexture(t,baseline);},t->format==TextureFormat::RGBA8?"metadata does not match":"support only RGBA8");});
        const auto other=b.createTextureMipChain(t->width,t->height,t->format,views(pattern(t->width,t->height,t->mipLevels)));
        changed(t->view,other->view,[&]{rejects([&]{b.readbackMip(t,1);},"view belongs to another resource");});
        ComPtr<ID3D11Device> device;t->texture->GetDevice(&device);
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};t->view->GetDesc(&v);v.Texture2D.MipLevels=1;
        ComPtr<ID3D11ShaderResourceView> narrow;need(SUCCEEDED(device->CreateShaderResourceView(t->texture.Get(),&v,&narrow)),"Probe view creation failed");
        changed(t->view,narrow,[&]{rejects([&]{b.readbackMip(t,1);},"view does not match");});
        need(b.readbackMip(t,1)==baseline,"Rejected metadata changed mip contents");
    }
    static void bindingPreservation(NativeBackend& b,const std::shared_ptr<Texture>& t){
        ComPtr<ID3D11Device> device;t->texture->GetDevice(&device);ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);
        ID3D11ShaderResourceView* view=t->view.Get();context->PSSetShaderResources(7,1,&view);
        const auto copy=b.createTextureMipChain(t->width,t->height,t->format,views(pattern(t->width,t->height,t->mipLevels)));
        b.readbackMip(copy,copy->levelCount()-1);ComPtr<ID3D11ShaderResourceView> after;context->PSGetShaderResources(7,1,&after);
        need(after.Get()==view,"Native mip creation/readback changed an active shader binding");
        view=nullptr;context->PSSetShaderResources(7,1,&view);
    }
};
}
namespace {
std::shared_ptr<Texture> workerUpload(const std::shared_ptr<const NativeImmutableMipFactory>& factory,
                                    uint32_t width,uint32_t height,TextureFormat format,std::vector<std::vector<uint8_t>> bytes){
    std::shared_ptr<Texture> result;std::exception_ptr failure;
    std::thread worker([&,owned=std::move(bytes)]() mutable {
        try {
            result=factory->createMipChain(width,height,format,views(owned));
            for(auto& mip:owned)std::fill(mip.begin(),mip.end(),0xEE);
            owned.clear();
        }catch(...){failure=std::current_exception();}
    });
    worker.join();if(failure)std::rethrow_exception(failure);return result;
}
std::shared_ptr<const NativeImmutableMipFactory> factoryContracts(NativeBackend& b,NativeBackend& foreign){
    need(!d3dCallStatsInstalled(),"Worker GPU fixture requires diagnostic counters disabled");
    const auto device=NativeMipTextureProbe::device(b);
    D3D11_FEATURE_DATA_THREADING threading{};
    const bool eligible=!(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED) &&
        SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_THREADING,&threading,sizeof(threading))) && threading.DriverConcurrentCreates==TRUE;
    auto factory=b.immutableMipPreparationFactory();
    need(bool(factory)==eligible,"Production factory did not honor actual device concurrent-create support");
    if(!factory)factory=NativeImmutableMipFactoryTestProbe::threadSafeDevice(device);
    need(b.matchesImmutableMipPreparationFactory(factory) && !b.matchesImmutableMipPreparationFactory({}),
         "Own/null immutable factory identity differs");
    need(!foreign.matchesImmutableMipPreparationFactory(factory),"Foreign backend admitted immutable factory");
    NativeMipTextureProbe::guards(b,foreign,factory);
    bool acquireRejected=false,matchRejected=false;
    std::thread wrongThread([&]{
        try{b.immutableMipPreparationFactory();}catch(const Error& e){acquireRejected=std::string(e.what()).find("different thread")!=std::string::npos;}
        try{b.matchesImmutableMipPreparationFactory({});}catch(const Error& e){matchRejected=std::string(e.what()).find("different thread")!=std::string::npos;}
    });wrongThread.join();need(acquireRejected&&matchRejected,"Factory owner guard was suppressed");
    for(auto fault:{DeviceQualificationProbe::Fault::SingleThreaded,DeviceQualificationProbe::Fault::NoConcurrentCreates,
                    DeviceQualificationProbe::Fault::FailedQuery}) {
        {
            DeviceQualificationProbe probe(device,fault);
            need(!b.immutableMipPreparationFactory(),"Optional factory accepted unqualified threading support");
        }
        // Exercise synchronous fallback against the restored real device,
        // rather than carrying an artificial threading answer into its driver.
        const auto expected=pattern(2,2,2);exact(b,b.createTextureMipChain(2,2,TextureFormat::RGBA8,views(expected)),expected);
    }
    {
        DeviceQualificationProbe probe(device,DeviceQualificationProbe::Fault::Removed);
        NativePresentationProbe::removal(b,[&]{
            rejects([&]{b.immutableMipPreparationFactory();},"availability failed");
            rejects([&]{b.matchesImmutableMipPreparationFactory({});},"availability failed");
        });
    }
    const auto expected=pattern(8,4,4);
    const auto old=workerUpload(factory,8,4,TextureFormat::RGBA8,expected);
    NativeMipTextureProbe::unvalidated(old);NativeMipTextureProbe::owningDevice(old,device);
    NativeMipTextureProbe::replacement(b,foreign,factory,old,expected);exact(b,old,expected);
    const auto context=NativeMipTextureProbe::context(b);NativeMipTextureProbe::bindSentinel(context,old);
    D3D11_PRIMITIVE_TOPOLOGY previousTopology{};context->IAGetPrimitiveTopology(&previousTopology);
    std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> previousViewports{};
    UINT previousCount=UINT(previousViewports.size());context->RSGetViewports(&previousCount,previousViewports.data());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
    D3D11_VIEWPORT viewport{3,4,11,9,0.25f,0.75f};context->RSSetViewports(1,&viewport);
    const auto another=workerUpload(factory,8,4,TextureFormat::RGBA8,expected);
    NativeMipTextureProbe::unvalidated(another);NativeMipTextureProbe::backing(another);
    NativeMipTextureProbe::sentinel(context,old);
    D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);
    D3D11_VIEWPORT after{};UINT count=1;context->RSGetViewports(&count,&after);
    need(topology==D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP && count==1 && !std::memcmp(&after,&viewport,sizeof(viewport)),
         "Device-only creation changed immediate IA/raster bindings");
    ID3D11ShaderResourceView* empty=nullptr;context->PSSetShaderResources(7,1,&empty);
    context->IASetPrimitiveTopology(previousTopology);context->RSSetViewports(previousCount,previousViewports.data());
    rejects([&]{workerUpload(factory,0,1,TextureFormat::RGBA8,{});},"dimensions exceed");
    rejects([&]{workerUpload(factory,4,4,TextureFormat::R8,{});},"support only RGBA8/BGRX8/BC1/BC2/BC3");
    rejects([&]{workerUpload(factory,4,4,TextureFormat::RGBA8,{});},"level count");
    auto invalid=pattern(4,4,3);invalid[1].pop_back();
    rejects([&]{workerUpload(factory,4,4,TextureFormat::RGBA8,invalid);},"byte count mismatch");
    return factory;
}
void factoryLifetime(bool hardware){
    auto backend=std::make_unique<NativeBackend>(!hardware);
    auto factory=backend->immutableMipPreparationFactory();
    if(!factory)factory=NativeImmutableMipFactoryTestProbe::threadSafeDevice(NativeMipTextureProbe::device(*backend));
    backend.reset(); // No test-owned device/context survives this point.
    const auto expected=pattern(19,7,5);
    auto texture=workerUpload(factory,19,7,TextureFormat::RGBA8,expected);
    NativeMipTextureProbe::unvalidated(texture);NativeMipTextureProbe::backing(texture);
    std::weak_ptr<const NativeImmutableMipFactory> weak=factory;factory.reset();
    need(weak.expired(),"Factory retained an unexpected backend or worker owner");
    for(uint32_t i=0;i<expected.size();++i)need(NativeMipTextureProbe::retainedReadback(texture,i)==expected[i],
        "Texture lost exact mip bytes after backend/factory/input destruction");
}
void statsFactoryRefusal(NativeBackend& b){
    installD3DCallStats(NativeMipTextureProbe::context(b).Get(),NativeMipTextureProbe::device(b).Get());
    need(d3dCallStatsInstalled() && !b.immutableMipPreparationFactory(),"Installed counters admitted concurrent creation");
    const auto expected=pattern(4,4,3);exact(b,b.createTextureMipChain(4,4,TextureFormat::RGBA8,views(expected)),expected);
}
void sampleLevels(NativeBackend& b,const std::shared_ptr<Texture>& t,const std::vector<std::vector<uint8_t>>& expected){
    for(uint32_t level=0;level<t->levelCount();++level){
        const auto w=std::max(1u,t->width>>level),h=std::max(1u,t->height>>level);
        auto target=b.createTarget(w,h,TargetFormat::RGBA32Float);ScreenDraw draw{};
        draw.vertices={ScreenVertex{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,-1,1,1}};
        draw.color={1,1,1,1};draw.texture=t;draw.blendSelector=3;draw.colorWriteMask=15;
        draw.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        draw.sampler.AddressU=draw.sampler.AddressV=draw.sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        draw.sampler.MaxAnisotropy=1;draw.sampler.ComparisonFunc=D3D11_COMPARISON_ALWAYS;
        draw.sampler.MinLOD=draw.sampler.MaxLOD=float(level);b.drawScreen(target,draw);++sampled;
        const auto bytes=b.readbackTarget(target);std::vector<float> pixels(bytes.size()/4);std::memcpy(pixels.data(),bytes.data(),bytes.size());
        need(pixels.size()==expected[level].size(),"Sampled output size differs");
        for(uint32_t i=0;i<pixels.size();++i){
            const uint32_t channel=i%4,at=i-channel;
            const float value=t->format==TextureFormat::BGRX8 ? (channel==3?1.0f:float(expected[level][at+(channel==0?2:channel==2?0:1)])/255.0f):float(expected[level][i])/255.0f;
            need(std::isfinite(pixels[i]) && std::abs(pixels[i]-value)<0.000002f,"Native mip sampling, channel order or forced alpha differs");
        }
    }
}
void profile(NativeBackend& b,NativeBackend& foreign,uint32_t w,uint32_t h,uint32_t count,TextureFormat format,
             const std::shared_ptr<const NativeImmutableMipFactory>& factory){
    const auto expected=pattern(w,h,count);std::shared_ptr<Texture> texture;
    {auto upload=expected;texture=b.createTextureMipChain(w,h,format,views(upload));for(auto& mip:upload)std::fill(mip.begin(),mip.end(),0xEE);}
    exact(b,texture,expected);NativeMipTextureProbe::backing(texture);sampleLevels(b,texture,expected);
    const auto prepared=workerUpload(factory,w,h,format,expected);
    NativeMipTextureProbe::unvalidated(prepared);NativeMipTextureProbe::backing(prepared);
    NativeMipTextureProbe::owningDevice(prepared,NativeMipTextureProbe::device(b));
    exact(b,prepared,expected);sampleLevels(b,prepared,expected);
    rejects([&]{b.readbackMip(texture,count);},"out of range");rejects([&]{b.readbackMip(texture,0xFFFFFFFF);},"out of range");
    rejects([&]{b.writeTexture(texture,expected[0]);},"requires writable storage");
    rejects([&]{foreign.readbackMip(texture,0);},"another graphics device");
    if(count>1){NativeMipTextureProbe::invalidMetadata(b,texture);NativeMipTextureProbe::bindingPreservation(b,texture);}
    for(uint32_t i=0;i<count;++i){
        auto invalid=expected;invalid[i].pop_back();rejects([&]{b.createTextureMipChain(w,h,format,views(invalid));},"byte count mismatch");
    }
    const auto before=texture->levelCount();bool rejected=false;std::thread thread([&]{try{b.readbackMip(texture,0);}catch(const Error&){rejected=true;}});thread.join();
    need(rejected && texture->levelCount()==before,"Wrong-thread mip readback accepted or changed ownership");
    exact(b,texture,expected);std::weak_ptr<Texture> weak=texture;auto alias=texture;texture.reset();need(!weak.expired(),"Alias lost mip ownership");alias.reset();need(weak.expired(),"Mip wrapper retained an unexpected owner");
}
void compressed(NativeBackend& b,NativeBackend& foreign,TextureFormat format,
                const std::shared_ptr<const NativeImmutableMipFactory>& factory){
    constexpr std::array<uint16_t,6> colors={0xF800,0x07E0,0x001F,0xFFE0,0x07FF,0xF81F};
    const auto blockBytes=format==TextureFormat::BC1?8u:16u;
    std::vector<std::vector<uint8_t>> blocks,pixels;
    for(uint32_t level=0;level<colors.size();++level){
        const auto w=std::max(1u,32u>>level),h=std::max(1u,16u>>level),color=uint32_t(colors[level]);
        std::vector<uint8_t> block(blockBytes);
        block[blockBytes-8]=uint8_t(color);block[blockBytes-7]=uint8_t(color>>8);
        uint8_t alpha=255;
        if(format==TextureFormat::BC2){alpha=uint8_t((level+1)*17);std::fill_n(block.begin(),8,alpha);}
        if(format==TextureFormat::BC3){alpha=uint8_t(30+level*30);block[0]=alpha;}
        auto& bytes=blocks.emplace_back();
        for(uint32_t i=0;i<((w+3)/4)*((h+3)/4);++i)bytes.insert(bytes.end(),block.begin(),block.end());
        auto& rgba=pixels.emplace_back();
        for(uint32_t i=0;i<w*h;++i)rgba.insert(rgba.end(),{uint8_t(color&0xF800?255:0),uint8_t(color&0x07E0?255:0),uint8_t(color&31?255:0),alpha});
    }
    auto upload=blocks;const auto texture=b.createTextureMipChain(32,16,format,views(upload));
    for(auto& level:upload)std::fill(level.begin(),level.end(),0);
    exact(b,texture,blocks);NativeMipTextureProbe::backing(texture);sampleLevels(b,texture,pixels);
    const auto prepared=workerUpload(factory,32,16,format,blocks);
    NativeMipTextureProbe::unvalidated(prepared);NativeMipTextureProbe::backing(prepared);
    NativeMipTextureProbe::owningDevice(prepared,NativeMipTextureProbe::device(b));
    exact(b,prepared,blocks);sampleLevels(b,prepared,pixels);
    rejects([&]{foreign.readbackMip(texture,5);},"another graphics device");
    rejects([&]{b.writeTexture(texture,blocks[0]);},"requires writable storage");
    for(size_t level=0;level<blocks.size();++level){auto invalid=blocks;invalid[level].pop_back();
        rejects([&]{b.createTextureMipChain(32,16,format,views(invalid));},"byte count mismatch");}
    const auto partialViews=views(blocks);
    const auto partial=b.createTextureMipChain(32,16,format,std::span(partialViews).first(3));
    need(partial->levelCount()==3 && b.readbackMip(partial,2)==blocks[2],"Partial BC chain changed");
    rejects([&]{b.readbackMip(partial,3);},"out of range");
}
}
int main(int argc,char** argv){
    try{
        const bool hardware=argc==2 && std::string(argv[1])=="--hardware";NativeBackend b(!hardware),foreign(!hardware);
        {
        const auto factory=factoryContracts(b,foreign);
        for(auto format:{TextureFormat::RGBA8,TextureFormat::BGRX8})for(const auto dimensions:std::array<std::array<uint32_t,2>,5>{{{32,32},{16,16},{19,7},{1,16},{1,1}}}){
            const auto w=dimensions[0],h=dimensions[1];profile(b,foreign,w,h,std::bit_width(std::max(w,h)),format,factory);
        }
        profile(b,foreign,32,16,3,TextureFormat::RGBA8,factory);
        rejects([&]{b.createTextureMipChain(32,32,TextureFormat::RGBA8,{});},"level count");
        rejects([&]{b.createTextureMipChain(1,1,TextureFormat::RGBA8,views(pattern(1,1,2)));},"level count");
        for(auto format:{TextureFormat::BC1,TextureFormat::BC2,TextureFormat::BC3}){
            compressed(b,foreign,format,factory);rejects([&]{b.createTextureMipChain(4,4,format,{});},"level count");
            rejects([&]{b.createTextureMipChain(2,4,format,{});},"compressed texture edge layout");
        }
        rejects([&]{b.createTextureMipChain(4,4,static_cast<TextureFormat>(999),{});},"support only RGBA8/BGRX8/BC1/BC2/BC3");
        rejects([&]{b.createTextureMipChain(0,1,TextureFormat::RGBA8,{});},"dimensions exceed");
        rejects([&]{b.readbackMip(nullptr,0);},"Missing native texture");
        factoryLifetime(hardware);
        } // All workers joined and factory aliases destroyed before installing counters.
        statsFactoryRefusal(b);
        std::printf("PASS native explicit mip textures:%zu checks,%zu sampled levels;RGBA8/BGRX8/BC1/BC2/BC3,full/partial chains,NPOT,all raw levels,1x1 BC sampling,device-only workers,qualification/owner/device/counter guards,retained-device/input lifetime;%s\n",checks,sampled,hardware?"hardware":"WARP");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL native mip textures:%zu checks %s\n",checks,e.what());return 1;}
}
