#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include "renderer/native_backend.h"
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace Simpsons::Graphics;
namespace {size_t checks{};}
void require(bool value,const char* reason){++checks;if(!value)throw Error(reason);}
template<class F> void rejects(F action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}require(rejected,"Unqualified viewport copy accepted");}
#include "header/test_engine_binding_reset.h"

namespace Simpsons::Graphics {
// Test access is confined to seeding independent pixel fixtures and inspecting
// real D3D state. Production copy never uploads or guesses depth values.
struct NativePresentationProbe {
    static void run(bool software){
        NativeBackend b(software),foreign(software);constexpr uint32_t w=64,h=16;
        auto z=b.createDepthTarget(w,h),dst=b.createDepthTarget(w,h),other=b.createDepthTarget(w,h);
        auto color=b.createTarget(w,h,TargetFormat::RGB10A2);b.clearTarget(color,{1,0,1,1});
        b.bindTargets({color,nullptr,nullptr,nullptr},z);b.clearDepthTarget(dst,1,0x53);
        const D3D11_VIEWPORT vp{1,2,31,7,0.25f,0.75f};b.context->RSSetViewports(1,&vp);
        const D3D11_RECT scissor{2,3,25,8};b.context->RSSetScissorRects(1,&scissor);b.context->SetPredication(nullptr,TRUE);
        constexpr std::array<uint32_t,8> values={0,0x2E800000,0x38000000,0x3E800000,0x3F000000,0x3F7FFFF8,0x3F800000,0x3D555550};
        std::vector<uint32_t> pixels(w*h*2);for(uint32_t i=0;i<w*h;++i){pixels[2*i]=values[(i/256+i)%values.size()];pixels[2*i+1]=i&255;}
        D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R32G8X24_TYPELESS;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
        const D3D11_SUBRESOURCE_DATA data{pixels.data(),w*8,w*h*8};ComPtr<ID3D11Texture2D> upload;
        require(SUCCEEDED(b.device->CreateTexture2D(&desc,&data,&upload)),"Independent depth fixture upload failed");
        ComPtr<ID3D11DepthStencilView> dsv;b.context->OMGetRenderTargets(0,nullptr,&dsv);ComPtr<ID3D11Resource> resource;dsv->GetResource(&resource);
        b.context->CopyResource(resource.Get(),upload.Get());
        const auto before=EngineBindingResetProbe::Snapshot(b.context.Get());const auto colorBefore=b.readbackTarget(color);
        auto checkPixels=[&](const std::shared_ptr<DepthTarget>& target){const auto read=b.readbackDepthTarget(target);
            require(read.size()==w*h*8,"Depth copy readback extent differs");for(uint32_t i=0;i<w*h;++i){uint32_t bits{};std::memcpy(&bits,read.data()+i*8,4);
                require(bits==pixels[i*2]&&read[i*8+4]==uint8_t(pixels[i*2+1]),"Native full copy changed a depth bit or stencil value");}};
        checkPixels(z);auto token=b.copyDepth(z,dst);b.waitCopy(token);checkPixels(z);checkPixels(dst);
        require(EngineBindingResetProbe::Snapshot(b.context.Get())==before&&b.readbackTarget(color)==colorBefore,"Depth copy changed native pipeline or color pixels");
        require(b.screenDrawCount()==0&&b.presentationCount()==0,"Depth copy drew or presented a frame");
        auto rejectUnchanged=[&](auto action){const auto count=b.pendingCopies.size();const auto image=b.readbackDepthTarget(dst);rejects(action);
            require(b.pendingCopies.size()==count&&b.readbackDepthTarget(dst)==image&&EngineBindingResetProbe::Snapshot(b.context.Get())==before,"Rejected native depth copy changed work,bindings or pixels");};
        rejectUnchanged([&]{b.copyDepth(z,z);});rejectUnchanged([&]{b.copyDepth(other,dst);});
        rejectUnchanged([&]{b.copyDepth(z,b.createDepthTarget(w/2,h));});rejectUnchanged([&]{b.copyDepth(z,foreign.createDepthTarget(w,h));});
        rejectUnchanged([&]{b.copyDepth(nullptr,dst);});rejectUnchanged([&]{b.copyDepth(z,nullptr);});
        rejectUnchanged([&]{foreign.waitCopy(token);});
        bool threadRejected=false;std::thread wrong([&]{try{b.copyDepth(z,dst);}catch(const Error&){threadRejected=true;}});wrong.join();require(threadRejected,"Foreign-thread depth copy accepted");
        ComPtr<ID3D11Predicate> predicate;const D3D11_QUERY_DESC query{D3D11_QUERY_OCCLUSION_PREDICATE,0};
        require(SUCCEEDED(b.device->CreatePredicate(&query,&predicate)),"Depth copy predication fixture failed");
        b.context->SetPredication(predicate.Get(),TRUE);rejects([&]{b.copyDepth(z,dst);});
        ComPtr<ID3D11Predicate> actual;BOOL condition{};b.context->GetPredication(&actual,&condition);require(actual==predicate&&condition==TRUE,"Rejected depth copy changed predication");b.context->SetPredication(nullptr,TRUE);
        token.reset();b.waitIdle();std::weak_ptr<DepthTarget> sourceLease=z,destinationLease=dst;
        b.copyDepth(z,dst);z.reset();dst.reset();require(!sourceLease.expired()&&!destinationLease.expired(),"Discarded copy receipt lost resource ownership before retirement");
        b.waitIdle();require(sourceLease.expired()&&destinationLease.expired(),"Completed copy leaked depth resource ownership");
        std::printf("Native depth copy %s:independent exact float/stencil fixture,unchanged pipeline,real event ownership\n",software?"WARP":"hardware");
    }
};
}
namespace {
using namespace Simpsons;
struct Observed{};
struct Abi{uint64_t sp,lr;std::array<uint64_t,18> gpr,fpr;bool operator==(const Abi&)const=default;};
Abi abi(const PPCContext& c){return{c.r1.u64,c.lr,{c.r14.u64,c.r15.u64,c.r16.u64,c.r17.u64,c.r18.u64,c.r19.u64,c.r20.u64,c.r21.u64,c.r22.u64,c.r23.u64,c.r24.u64,c.r25.u64,c.r26.u64,c.r27.u64,c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64},
    {c.f14.u64,c.f15.u64,c.f16.u64,c.f17.u64,c.f18.u64,c.f19.u64,c.f20.u64,c.f21.u64,c.f22.u64,c.f23.u64,c.f24.u64,c.f25.u64,c.f26.u64,c.f27.u64,c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64}};}
void original(const char* image){
    Runtime rt;rt.load(image);PPCContext startup{};rt.initialize(startup);const auto entry=startup;auto* base=rt.base;
    bool observed=false;rt.audioBoundaryObserver=[](uint32_t pc,PPCContext&,uint8_t*){if(pc==0x828166FC)throw Observed{};};
    try{runOriginal(startup,base);}catch(const Observed&){observed=true;}rt.audioBoundaryObserver={};require(observed,"Missing original startup boundary");
    auto& d=*rt.engineDriver;const auto presentsBefore=d.presentationCount(),frontCopiesBefore=d.frontCopyCount();
    EngineCpuCalls cpu(entry,base);auto& ctx=cpu.registers();const auto saved=abi(ctx);rt.map(0x50000,0x1000,true,"viewport copy fixture");
    const auto loading=PPC_LOAD_U32(0x82E07248),colorCopy=PPC_LOAD_U32(0x82D0CF88),depthCopy=PPC_LOAD_U32(0x82D0CF84);
    const auto defaultColor=PPC_LOAD_U32(0x82D0CB00),defaultDepth=PPC_LOAD_U32(0x82D0CAFC);const auto baseline=d.rasterCount();
    const auto manager=cpu.invoke(0x8269D788);std::array<uint32_t,3> camera{},color{},depth{};
    for(uint32_t slot=0;slot<3;++slot){const auto row=0x82CD12CC+20*slot;ctx.f1.f64=std::bit_cast<float>(PPC_LOAD_U32(row));ctx.f2.f64=std::bit_cast<float>(PPC_LOAD_U32(row+4));
        ctx.f3.f64=std::bit_cast<float>(PPC_LOAD_U32(row+8));ctx.f4.f64=std::bit_cast<float>(PPC_LOAD_U32(row+12));cpu.invoke(0x8269D608,manager,slot);camera[slot]=PPC_LOAD_U32(manager+0x14+4*slot);
        const auto offset=PPC_LOAD_U32(0x82E3DC94);color[slot]=PPC_LOAD_U32(PPC_LOAD_U32(camera[slot]+0x60)+offset);depth[slot]=PPC_LOAD_U32(PPC_LOAD_U32(camera[slot]+0x64)+offset);}
    auto clear=[&](uint32_t c,uint32_t rgba,uint32_t stencil){PPC_STORE_U32(0x50000,rgba);PPC_STORE_U32(0x82D0CB14,stencil);cpu.invoke(0x823EE940,c,0x50000,7);};
    clear(loading,0xFF0000FF,0x11);for(uint32_t i=0;i<3;++i)clear(camera[i],i?0x0000FFFF:0x00FF00FF,0x80+i);
    const auto copy=[&](uint32_t c,uint32_t cd,uint32_t zd){const auto clears=d.cameraClearCount();const auto binding=d.cameraBinding();
        cpu.invoke(0x826B08B0,cd,zd,c);require(abi(ctx)==saved,"Original viewport copy changed nonvolatile integer/floating-point ABI");
        const auto after=d.cameraBinding();require(after.camera==binding.camera&&after.viewport==binding.viewport&&after.colorIdentity==binding.colorIdentity&&after.depthIdentity==binding.depthIdentity&&d.cameraClearCount()==clears,"Original copy changed camera binding or cleared it");};
    cpu.invoke(0x823F1A18,loading);copy(loading,colorCopy,depthCopy);cpu.invoke(0x823F1A08,loading);
    const auto defaultColorPixels=d.readbackColor(defaultColor),defaultDepthPixels=d.readbackDepth(defaultDepth);
    require(d.readbackColor(colorCopy)==defaultColorPixels,"Loading camera full color copy differs");
    bool alpha{};require(d.color(color[0],alpha)==d.color(defaultColor,alpha)&&d.depth(depth[0])==d.depth(defaultDepth),
        "Full-size copy sources lost shared default working storage");
    require(d.color(colorCopy,alpha)!=d.color(defaultColor,alpha)&&d.depth(depthCopy)!=d.depth(defaultDepth),
        "Resolved copy destination aliases its working source");
    auto depthEqual=[&](uint32_t a,uint32_t b){const auto x=d.readbackDepth(a),y=d.readbackDepth(b);require(x.size()==y.size(),"Depth role extents differ");
        bool equal=true;for(size_t i=0;i<x.size();i+=8)equal&=!std::memcmp(x.data()+i,y.data()+i,5);require(equal,"Depth/stencil role copy differs");};
    depthEqual(defaultDepth,depthCopy);cpu.invoke(0x823F1A18,camera[0]);
    const auto defaultCopies=d.cameraCopyCount();copy(camera[0],0,depthCopy);depthEqual(depth[0],depthCopy);
    require(d.readbackColor(colorCopy)==defaultColorPixels&&d.cameraCopyCount()==defaultCopies+1,"Depth-only copy changed color role or counted another operation");
    copy(camera[0],colorCopy,0);require(d.readbackColor(colorCopy)==d.readbackColor(color[0]),"Private full color copy differs");
    copy(camera[0],colorCopy,depthCopy);depthEqual(depth[0],depthCopy);const auto count=d.cameraCopyCount();copy(camera[0],0,0);require(d.cameraCopyCount()==count,"Empty original copy submitted native work");
    const auto savedColor=d.readbackColor(colorCopy),savedDepth=d.readbackDepth(depthCopy);
    auto rejected=[&](auto f){rejects(f);require(d.cameraCopyCount()==count&&d.readbackColor(colorCopy)==savedColor&&d.readbackDepth(depthCopy)==savedDepth,"Rejected engine copy changed destination or count");};
    rejected([&]{copy(camera[0],colorCopy,defaultDepth);});rejected([&]{copy(camera[0],depthCopy,depthCopy);});rejected([&]{copy(camera[1],0,depthCopy);});
    const auto right=PPC_LOAD_U32(manager+0x2C);PPC_STORE_U32(manager+0x2C,right-1);rejected([&]{copy(camera[0],colorCopy,depthCopy);});PPC_STORE_U32(manager+0x2C,right);
    const auto cached=PPC_LOAD_U32(0x82D0CF58);PPC_STORE_U32(0x82D0CF58,0);rejected([&]{copy(camera[0],0,depthCopy);});PPC_STORE_U32(0x82D0CF58,cached);
    cpu.invoke(0x823F1A08,camera[0]);rejected([&]{copy(camera[0],0,depthCopy);});
    for(uint32_t i=1;i<3;++i){cpu.invoke(0x823F1A18,camera[i]);rejected([&]{copy(camera[i],colorCopy,depthCopy);});cpu.invoke(0x823F1A08,camera[i]);}
    require(d.readbackColor(defaultColor)==defaultColorPixels&&d.readbackDepth(defaultDepth)==defaultDepthPixels,"Private copies altered default attachments");
    std::weak_ptr<DepthTarget> privateLease=d.depth(depth[0]);cpu.invoke(0x823EE6C8,loading);cpu.invoke(0x8269C438,manager);
    require(d.rasterCount()==baseline,"Original camera reset retained raster ownership");rejects([&]{d.depth(depth[0]);});
    // The previous readbacks completed the GPU copies. Another real copy polls
    // and retires pending leases before submitting new work.
    cpu.invoke(0x823F1A18,loading);copy(loading,0,depthCopy);cpu.invoke(0x823F1A08,loading);require(privateLease.lock()==d.depth(defaultDepth),"Retiring the full-size view invalidated shared default working depth");
    cpu.invoke(0x8269CC28);require(!PPC_LOAD_U32(0x82D08B10)&&abi(ctx)==saved,"Original viewport deletion changed ABI or retained manager");
    require(d.presentationCount()==presentsBefore&&d.frontCopyCount()==frontCopiesBefore,"Viewport copies submitted presentation work");
    std::printf("Original viewport copies:loading/private full-size sources,exact shared destinations,depth-only/color-only/both,no-op,partial rejection and paired camera cleanup\n");
}
}
int main(int argc,char** argv){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try{require(argc==2,"Original image required");NativePresentationProbe::run(true);NativePresentationProbe::run(false);original(argv[1]);std::printf("PASS native viewport copies:%zu checks; no original draw or gameplay claim,ALL MUTED\n",checks);return 0;}
    catch(const std::exception& e){std::fprintf(stderr,"FAIL viewport copies:%zu checks:%s\n",checks,e.what());return 1;}}
