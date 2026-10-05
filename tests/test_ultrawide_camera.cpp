#include "runtime/native_ultrawide_camera.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include "runtime/native_window.h"
#include "runtime/runtime.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace {
using namespace Simpsons;
constexpr uint32_t manager=0x20000,camera=0x21000,frame=0x22000,
                   color=0x23000,depth=0x24000,engine=0x25000,foreign=0x26000;
constexpr float baselineWidth=std::bit_cast<float>(0x3F13CD3Au),
                baselineHeight=std::bit_cast<float>(0x3EA646E1u);
void need(bool value,const char* why){if(!value)throw Failure(why);}
void setFloat(uint8_t* base,uint32_t at,float value){PPC_STORE_U32(at,std::bit_cast<uint32_t>(value));}
float getFloat(uint8_t* base,uint32_t at){return std::bit_cast<float>(PPC_LOAD_U32(at));}
std::array<std::array<float,3>,8> corners(uint8_t* base) {
    std::array<std::array<float,3>,8> result{};
    for(uint32_t i=0;i<8;++i)for(uint32_t lane=0;lane<3;++lane)
        result[i][lane]=getFloat(base,camera+0x124+12*i+4*lane);
    return result;
}
bool closeFloat(float a,float b){return std::abs(a-b)<=0.00002f*std::max(1.0f,std::max(std::abs(a),std::abs(b)));}
void visibilityMathAllocation(PPCContext& ctx,uint8_t*) {
    need(ctx.r3.u32==16384&&ctx.r4.u32==0x40401&&uint32_t(ctx.lr)==0x823EBB50,
         "Original reciprocal-square-root allocation ABI changed");
    ctx.r3.u32=0x40000;
}
void initializeVisibilityMath(Runtime& rt,const PPCContext& incoming,uint8_t* base) {
    // The game initializes this RenderWare math plugin before creating cameras.
    // Run its actual 4096-entry reciprocal-square-root initializer, supplying
    // only bounded allocator storage. Original plane normalization consumes it.
    rt.map(0x40000,0x4000,true,"original visibility reciprocal-square-root table");
    PPC_STORE_U32(0x82D0C910,0x500);
    constexpr uint32_t allocationEntry=0x8238E880;
    struct AllocationDispatch {
        uint8_t* base;PPCFunc* previous;
        explicit AllocationDispatch(uint8_t* memory):base(memory),previous(PPC_LOOKUP_FUNC(base,allocationEntry)) {
            need(previous!=nullptr,"Original allocator dispatch is absent");
            PPC_LOOKUP_FUNC(base,allocationEntry)=visibilityMathAllocation;
            PPC_STORE_U32(engine+0x108,allocationEntry);
        }
        ~AllocationDispatch(){PPC_LOOKUP_FUNC(base,allocationEntry)=previous;PPC_STORE_U32(engine+0x108,0);}
    } dispatch(base);
    EngineCpuCalls cpu(incoming,base);
    need(cpu.invoke(0x823EBB20)==1&&PPC_LOAD_U32(engine+0x504)==0x40000,
         "Original reciprocal-square-root table initialization failed");
    cpu.registers().f1.f64=4.0;
    cpu.invoke(0x823EBF68);
    need(closeFloat(float(cpu.registers().f1.f64),0.5f),"Original plane normalization table is invalid");
}
void exerciseEarlyVisibility(Runtime& rt,const PPCContext& incoming,uint8_t* base) {
    // A real renderer owner supplies its frozen aspect to the generated hook.
    // The fixture runs original CPU visibility only; no game or GPU draw runs.
    struct BackgroundWindow {
        std::string previous;bool existed{};
        BackgroundWindow(){if(const auto value=std::getenv("SIMPSONS_BACKGROUND_WINDOW")){previous=value;existed=true;}need(!_putenv_s("SIMPSONS_BACKGROUND_WINDOW","1"),"Background fixture environment failed");}
        ~BackgroundWindow(){_putenv_s("SIMPSONS_BACKGROUND_WINDOW",existed?previous.c_str():"");}
    } background;
    rt.videoSettings=NativeVideoSettings{};
    rt.window=std::make_unique<NativeWindow>();
    ShowWindow(rt.window->handle(),SW_HIDE);
    rt.engineDriver=std::make_shared<EngineDriver>(rt,rt.window->width.load(),rt.window->height.load());
    need(rt.engineDriver->renderAspect()==16.0/9.0,"Original visibility fixture render aspect differs");
    initializeVisibilityMath(rt,incoming,base);
    constexpr uint32_t scene=0x30000,world=0x33000,box=0x34000,planes=scene+6384;
    rt.map(scene,0x5000,true,"original early scene visibility fixture");
    PPC_STORE_U32(scene+12,world); // Empty spatial tree still publishes all six original planes.
    constexpr std::array syncWords{0x7D8802A6u,0x9181FFF8u,0xFBE1FFF0u};
    for(size_t i=0;i<syncWords.size();++i)
        need(PPC_LOAD_U32(0x823F1790+uint32_t(i)*4)==syncWords[i],"Original early camera sync hook bytes differ");
    constexpr std::array earlyWords{0x4BCCBF21u,0x38A00001u,0x7FE4FB78u,0x7FC3F378u,0x4BF890E9u};
    for(size_t i=0;i<earlyWords.size();++i)
        need(PPC_LOAD_U32(0x827258D8+uint32_t(i)*4)==earlyWords[i],"Original frustum-before-visibility caller bytes differ");
    const auto setter=[&](float width,float height) {
        EngineCpuCalls cpu(incoming,base);const auto pair=cpu.registers().r1.u32+0x80;
        setFloat(base,pair,width);setFloat(base,pair+4,height);
        need(cpu.invoke(0x823F1C98,camera,pair)==camera,"Original early visibility source setter failed");
    };
    const auto visibility=[&] {
        EngineCpuCalls cpu(incoming,base);const auto before=cpu.registers();
        cpu.invoke(0x827258B8,scene,camera);
        need(cpu.registers().r1.u64==before.r1.u64&&cpu.registers().lr==before.lr&&
             cpu.registers().r30.u64==before.r30.u64&&cpu.registers().r31.u64==before.r31.u64,
             "Original early visibility preparation changed nonvolatile ABI");
        need(!PPC_LOAD_U32(scene+6176),"Empty original visibility fixture produced spatial-tree records");
    };
    const auto classified=[&](float x,float y,float z) {
        // Original 8270E490 consumes the copied six-plane visibility record.
        // It classifies a min/max AABB as 1=inside, 2=overlap, 3=outside.
        for(uint32_t lane=0;lane<3;++lane) {
            const std::array point{x,y,z};
            setFloat(base,box+4*lane,point[lane]-0.05f);
            setFloat(base,box+16+4*lane,point[lane]+0.05f);
        }
        EngineCpuCalls cpu(incoming,base);return cpu.invoke(0x8270E490,box,planes);
    };
    setter(baselineWidth,baselineHeight);visibility();
    need(classified(0,0,100)==1,"Original center object was culled");
    need(classified(70,0,100)==3&&classified(-70,0,100)==3&&
         classified(0,40,100)==3&&classified(0,-40,100)==3,
         "Stock camera did not cull the fixture's objects beyond its FOV");
    const auto source=std::array{PPC_LOAD_U32(camera+0x68),PPC_LOAD_U32(camera+0x6C)};
    // Pending internal-resolution preferences must not change the live
    // renderer's aspect while the selected FOV changes immediately.
    rt.videoSettings.renderResolution=8;
    for(const uint32_t reference:{110u,80u,0u,110u}) {
        rt.videoSettings.fieldOfView=reference;visibility();
        const bool widened=reference!=0;
        for(const auto point:std::array<std::array<float,3>,4>{{{70,0,100},{-70,0,100},{0,40,100},{0,-40,100}}})
            need(classified(point[0],point[1],point[2])==(widened?1u:3u),
                 "Original early visibility used stock FOV after selecting a wider render view");
        need(classified(300,0,100)==3&&classified(0,180,100)==3&&
             classified(0,0,0.5f)==3&&classified(0,0,6000)==3,
             "FOV fix disabled side/top/near/far visibility rejection");
        need(closeFloat(getFloat(base,camera+0x68)/getFloat(base,camera+0x6C),16.0f/9.0f),
             "Early FOV publication used pending resolution instead of frozen renderer aspect");
        if(!reference)need(source==std::array{PPC_LOAD_U32(camera+0x68),PPC_LOAD_U32(camera+0x6C)},
                           "Original early visibility did not restore exact authored projection");
        const auto published=std::array{PPC_LOAD_U32(camera+0x68),PPC_LOAD_U32(camera+0x6C)};
        visibility();
        need(published==std::array{PPC_LOAD_U32(camera+0x68),PPC_LOAD_U32(camera+0x6C)},
             "Repeated early frustum rebuild accumulated FOV");
        need(PPC_LOAD_U32(engine+0xBC)==frame+8,"Early frustum publication lost the original dirty frame");
        {EngineCpuCalls cpu(incoming,base);cpu.invoke(0x8240D5C0);}
        need(PPC_LOAD_U32(engine+0xBC)==engine+0xBC&&PPC_LOAD_U32(engine+0xC0)==engine+0xBC&&
             !(PPC_LOAD_U8(frame+3)&3),"Early FOV sync corrupted or reinserted the original dirty list");
        // Reproduce the normal controller updating its authored stock window
        // before the next visibility collection, while custom FOV is active.
        setter(baselineWidth,baselineHeight);
    }
    // This path calls the hook from the original dirty-list callback instead
    // of the direct early-culling helper, and must retire that same list safely.
    {EngineCpuCalls cpu(incoming,base);cpu.invoke(0x8240D5C0);}
    need(closeFloat(getFloat(base,camera+0x68),float(std::tan(110.0*3.14159265358979323846/360.0)))&&
         PPC_LOAD_U32(engine+0xBC)==engine+0xBC&&PPC_LOAD_U32(engine+0xC0)==engine+0xBC,
         "Original dirty-list frustum callback did not publish selected FOV or retire safely");
    // The incoming hook's own register and host state remains observational.
    auto hook=incoming;const auto before=hook;const auto hostFp=PPCFPSCRRegister::getcsr();SetLastError(0x4173);
    SimpsonsNativeUltrawideCameraSync(hook,base);
    need(std::memcmp(&hook,&before,sizeof(hook))==0&&PPCFPSCRRegister::getcsr()==hostFp&&GetLastError()==0x4173,
         "Early frustum hook changed PPC/host ABI state");
    rt.engineDriver.reset();rt.window.reset();
}
void exercise(const char* image) {
    Runtime rt;rt.load(image);PPCContext ctx{};rt.initialize(ctx);auto* base=rt.base;
    rt.map(0x20000,0x7000,true,"ultrawide original camera fixture");
    PPC_STORE_U32(0x82D08B10,manager);PPC_STORE_U32(manager,0x820B6DEC);
    PPC_STORE_U32(manager+8,0x820B6DE8);PPC_STORE_U32(manager+0x128,4);PPC_STORE_U32(manager+0x14,camera);
    PPC_STORE_U32(0x82D0CA68,engine);PPC_STORE_U32(engine+0xBC,engine+0xBC);PPC_STORE_U32(engine+0xC0,engine+0xBC);
    PPC_STORE_U32(camera,0x04000000);PPC_STORE_U32(camera+4,frame);
    PPC_STORE_U32(camera+8,frame+0x90);PPC_STORE_U32(camera+0xC,frame+0x90);
    PPC_STORE_U32(camera+0x10,0x823D2940);PPC_STORE_U32(camera+0x14,1);
    PPC_STORE_U32(camera+0x18,0x823D1100);PPC_STORE_U32(camera+0x1C,0x823D1160);
    PPC_STORE_U32(camera+0x60,color);PPC_STORE_U32(camera+0x64,depth);
    setFloat(base,camera+0x80,1);setFloat(base,camera+0x84,5000);
    for(const auto raster:{color,depth}) {
        PPC_STORE_U32(raster,raster);PPC_STORE_U32(raster+0xC,1280);PPC_STORE_U32(raster+0x10,720);
        PPC_STORE_U8(raster+0x20,raster==color?5:1);
    }
    PPC_STORE_U32(frame+0xA0,frame);PPC_STORE_U32(frame+0x90,camera+8);PPC_STORE_U32(frame+0x94,camera+8);
    for(const auto block:{0x10u,0x50u})for(uint32_t i=0;i<16;++i)
        setFloat(base,frame+block+4*i,i%5==0?1.0f:0.0f);
    // Real registered plugin wrapper forwards sync to its saved standard
    // callback. Its optional CPU state owner is absent in this bounded fixture.
    PPC_STORE_U32(0x82D09A20,0x400);PPC_STORE_U32(camera+0x400+0x18,0x823F1790);
    ctx.r3.u64=camera;ctx.lr=0x826B7E70;ctx.lastFunction=0x823F1870;
    auto authoredWindow=[&](float width,float height) {
        EngineCpuCalls cpu(ctx,base);const auto pair=cpu.registers().r1.u32+0x80;
        setFloat(base,pair,width);setFloat(base,pair+4,height);
        need(cpu.invoke(0x823F1C98,camera,pair)==camera,"Original baseline camera setter failed");
        cpu.invoke(0x8240D5C0);
        need(PPC_LOAD_U32(engine+0xBC)==engine+0xBC,"Original frame sync did not retire its dirty list");
    };
    auto baselineWindow=[&]{authoredWindow(baselineWidth,baselineHeight);};
    baselineWindow();const auto baseline=corners(base);
    bool nonzero=false;for(const auto& point:baseline)nonzero|=point[0]!=0;
    need(nonzero,"Original perspective frustum fixture produced no horizontal corners");
    constexpr std::array<std::array<uint32_t,2>,4> extents{{{2560,1080},{3440,1440},{3840,1600},{5120,1440}}};
    for(const auto extent:extents) {
        baselineWindow();const double aspect=double(extent[0])/extent[1];const auto before=ctx;
        const auto height=PPC_LOAD_U32(camera+0x6C);
        const auto hostFp=PPCFPSCRRegister::getcsr();SetLastError(0x4171);
        need(applyNativeUltrawideCamera(ctx,base,aspect),"Wide scene camera was not adjusted");
        need(PPCFPSCRRegister::getcsr()==hostFp&&GetLastError()==0x4171,"Native camera adjustment changed host FP or last-error state");
        need(std::memcmp(&before,&ctx,sizeof(ctx))==0&&currentContext==&ctx,"Native camera adjustment changed incoming PPC state/context");
        need(PPC_LOAD_U32(camera+0x6C)==height,"Ultrawide camera changed vertical FOV");
        const float width=getFloat(base,camera+0x68);
        need(PPC_LOAD_U32(camera+0x70)==std::bit_cast<uint32_t>(1.0f/width),"Original camera reciprocal did not follow widened window");
        need((PPC_LOAD_U8(frame+3)&3)==3&&PPC_LOAD_U32(engine+0xBC)==frame+8,"Original setter did not enqueue dirty-frame synchronization");
        need(!applyNativeUltrawideCamera(ctx,base,aspect),"Repeated camera begin accumulated horizontal FOV");
        {EngineCpuCalls cpu(ctx,base);cpu.invoke(0x8240D5C0);}
        const auto actual=corners(base);const float factor=float(aspect/(16.0/9.0));
        for(uint32_t i=0;i<8;++i) {
            need(closeFloat(actual[i][0],baseline[i][0]*factor),"Original frustum did not widen with horizontal FOV");
            need(closeFloat(actual[i][1],baseline[i][1])&&closeFloat(actual[i][2],baseline[i][2]),"Ultrawide frustum changed vertical/depth extent");
        }
        need(PPC_LOAD_U32(engine+0xBC)==engine+0xBC,"Ultrawide original frame sync did not complete");
    }
    constexpr double radians=3.14159265358979323846/180.0;
    for(const double aspect:{16.0/9.0,32.0/9.0})for(uint32_t reference=60;reference<=110;reference+=5) {
        baselineWindow();const auto before=ctx;
        const double scale=std::tan(double(reference)*radians/2.0)/double(baselineWidth);
        const float expectedHeight=float(double(baselineHeight)*scale);
        const float expectedWidth=float(double(expectedHeight)*aspect);
        const bool changed=std::bit_cast<uint32_t>(expectedWidth)!=PPC_LOAD_U32(camera+0x68)||
            std::bit_cast<uint32_t>(expectedHeight)!=PPC_LOAD_U32(camera+0x6C);
        const auto hostFp=PPCFPSCRRegister::getcsr();SetLastError(0x4172);
        need(applyNativeUltrawideCamera(ctx,base,aspect,reference)==changed,"Reference FOV did not update the original window");
        need(closeFloat(getFloat(base,camera+0x68),expectedWidth)&&closeFloat(getFloat(base,camera+0x6C),expectedHeight),
            "Reference FOV did not scale authored perspective tangents");
        need(std::memcmp(&before,&ctx,sizeof(ctx))==0&&currentContext==&ctx&&
             PPCFPSCRRegister::getcsr()==hostFp&&GetLastError()==0x4172,"FOV adjustment changed PPC/host ABI state");
        need(!applyNativeUltrawideCamera(ctx,base,aspect,reference),"Repeated FOV begin accumulated projection scale");
        {EngineCpuCalls cpu(ctx,base);cpu.invoke(0x8240D5C0);}
        const auto actual=corners(base);const float horizontal=float(scale*aspect/(16.0/9.0));
        for(uint32_t i=0;i<8;++i) {
            need(closeFloat(actual[i][0],baseline[i][0]*horizontal)&&closeFloat(actual[i][1],baseline[i][1]*float(scale)),
                 "Original frustum did not follow FOV and aspect adjustment");
            need(closeFloat(actual[i][2],baseline[i][2]),"FOV changed original near/far depth extents");
        }
        applyNativeUltrawideCamera(ctx,base,aspect,0);
        need(PPC_LOAD_U32(camera+0x6C)==std::bit_cast<uint32_t>(baselineHeight)&&
             closeFloat(getFloat(base,camera+0x68),float(double(baselineHeight)*aspect)),"Original did not restore authored FOV");
        need(!applyNativeUltrawideCamera(ctx,base,aspect,0),"Repeated Original begin changed the restored window");
        applyNativeUltrawideCamera(ctx,base,16.0/9.0,0);
        need(PPC_LOAD_U32(camera+0x68)==std::bit_cast<uint32_t>(baselineWidth),"Returning to 16:9 did not restore authored width");
    }
    // Reference degrees scale the source FOV rather than replacing authored zoom.
    // Use several original setter updates, with no resets of native provenance.
    for(const float sourceHeight:{0.35f,0.7f,0.45f}) {
        const float sourceWidth=float(double(sourceHeight)*(16.0/9.0));
        authoredWindow(sourceWidth,sourceHeight);const auto sourceCorners=corners(base);
        const double scale=std::tan(80.0*radians/2.0)/double(baselineWidth);
        need(applyNativeUltrawideCamera(ctx,base,16.0/9.0,80),"New authored zoom did not reach native FOV");
        need(closeFloat(getFloat(base,camera+0x6C),float(double(sourceHeight)*scale)),"FOV flattened an authored zoom update");
        need(!applyNativeUltrawideCamera(ctx,base,16.0/9.0,80),"Authored zoom accumulated on another begin");
        {EngineCpuCalls cpu(ctx,base);cpu.invoke(0x8240D5C0);}
        const auto adjusted=corners(base);
        for(uint32_t i=0;i<8;++i)
            need(closeFloat(adjusted[i][0],sourceCorners[i][0]*float(scale))&&
                 closeFloat(adjusted[i][1],sourceCorners[i][1]*float(scale))&&closeFloat(adjusted[i][2],sourceCorners[i][2]),
                 "Animated original frustum did not preserve authored zoom");
        need(applyNativeUltrawideCamera(ctx,base,16.0/9.0,110),"Live FOV change did not replace the previous scale");
        need(closeFloat(getFloat(base,camera+0x6C),float(double(sourceHeight)*std::tan(110.0*radians/2.0)/double(baselineWidth))),
             "Live FOV change scaled a previously adjusted window");
        need(applyNativeUltrawideCamera(ctx,base,16.0/9.0,0),"Original did not clear custom FOV without a controller update");
        need(PPC_LOAD_U32(camera+0x68)==std::bit_cast<uint32_t>(sourceWidth)&&
             PPC_LOAD_U32(camera+0x6C)==std::bit_cast<uint32_t>(sourceHeight),"Original did not restore exact authored projection bits");
    }
    baselineWindow();
    // Pin and execute the actual writer/forwarder seen on the live slot-zero
    // scene camera. This profile differs from the untouched factory fixture.
    constexpr std::array forwarderWords{0x3D6082E2u,0x396B7A00u,0x816B0770u,0x7D6903A6u,0x4E800420u};
    constexpr std::array writerWords{0x8143001Cu,0x7F0A4040u,0x419A0028u,0x812B0724u,0x7F091840u,
                                    0x419A001Cu,0x3D2082A7u,0x914B0770u,0x39497EE0u,0x9143001Cu};
    for(size_t i=0;i<forwarderWords.size();++i)
        need(PPC_LOAD_U32(0x82A77EE0+uint32_t(i)*4)==forwarderWords[i],"Original scene end forwarder bytes differ");
    for(size_t i=0;i<writerWords.size();++i)
        need(PPC_LOAD_U32(0x82A78200+uint32_t(i)*4)==writerWords[i],"Original scene end installation bytes differ");
    constexpr std::array setterWords{0x7D8802A6u,0x9181FFF8u,0xFBE1FFF0u};
    for(size_t i=0;i<setterWords.size();++i)
        need(PPC_LOAD_U32(0x823F1C98+uint32_t(i)*4)==setterWords[i],"Original view-window observer hook bytes differ");
    need(PPC_LOAD_U64(0x8214E7B0)==0x3FE0C15240000000ull,"Original stock half-FOV constant differs");
    PPC_STORE_U32(0x82E28124,0);PPC_STORE_U32(0x82E28170,0);
    {
        EngineCpuCalls cpu(ctx,base);const auto sp=cpu.registers().r1.u32;
        need(cpu.invoke(0x82A78188,camera)==1&&PPC_LOAD_U32(camera+0x1C)==0x82A77EE0&&
             PPC_LOAD_U32(0x82E28170)==0x823D1160,"Original scene writer did not retain and wrap its end callback");
        need(cpu.registers().r1.u32==sp,"Original scene writer changed the caller stack");
        need(cpu.invoke(0x82A78188,camera)==1&&PPC_LOAD_U32(0x82E28170)==0x823D1160,
             "Repeated original scene writer captured its own forwarder");
    }
    // With no optional plugin allocation, the actual registered end clears the
    // engine's CPU state owner and returns the camera. Both direct and wrapped
    // dispatch must preserve that real behavior and their original caller ABI.
    PPC_STORE_U32(0x82D09A20,0);
    for(const auto callback:{0x823D1160u,0x82A77EE0u}) {
        PPC_STORE_U32(engine+4,camera);
        EngineCpuCalls cpu(ctx,base);const auto before=cpu.registers();
        need(cpu.invoke(callback,camera)==camera&&!PPC_LOAD_U32(engine+4),"Original scene end forwarder lost plugin end behavior");
        need(cpu.registers().r1.u32==before.r1.u32&&cpu.registers().lr==before.lr&&
             cpu.registers().r31.u64==before.r31.u64,"Original scene end forwarder changed its caller ABI");
    }
    PPC_STORE_U32(0x82D09A20,0x400);
    const auto wrappedOriginal=std::array{PPC_LOAD_U32(camera+0x68),PPC_LOAD_U32(camera+0x6C)};
    need(applyNativeUltrawideCamera(ctx,base,16.0/9.0,110),"Live scene end profile rejected custom FOV");
    need(std::abs(2.0*std::atan(double(getFloat(base,camera+0x68)))/radians-110.0)<0.0001,
         "Stock 60-degree camera did not reach the selected horizontal FOV");
    need(!applyNativeUltrawideCamera(ctx,base,16.0/9.0,110),"Wrapped scene begin accumulated custom FOV");
    {EngineCpuCalls cpu(ctx,base);cpu.invoke(0x8240D5C0);}
    const auto wrappedCorners=corners(base);const float wrappedScale=float(std::tan(110.0*radians/2.0)/double(baselineWidth));
    for(uint32_t i=0;i<8;++i)
        need(closeFloat(wrappedCorners[i][0],baseline[i][0]*wrappedScale)&&
             closeFloat(wrappedCorners[i][1],baseline[i][1]*wrappedScale)&&closeFloat(wrappedCorners[i][2],baseline[i][2]),
             "Wrapped scene FOV did not rebuild the original frustum");
    need(applyNativeUltrawideCamera(ctx,base,16.0/9.0,0)&&
         wrappedOriginal==std::array{PPC_LOAD_U32(camera+0x68),PPC_LOAD_U32(camera+0x6C)},
         "Wrapped scene Original did not restore exact authored view");
    need(!applyNativeUltrawideCamera(ctx,base,16.0/9.0,0),"Wrapped scene Original changed its restored view again");
    // An authored setter can deliberately publish our exact previous override.
    // The setter observer must treat this as a new source rather than restore an
    // older zoom, while the native setter's own publication stays idempotent.
    need(applyNativeUltrawideCamera(ctx,base,16.0/9.0,110),"Wrapped custom FOV was not reapplied");
    const float equalWidth=getFloat(base,camera+0x68),equalHeight=getFloat(base,camera+0x6C);
    authoredWindow(equalWidth,equalHeight);
    need(!rt.nativeCameraProjection.applied,"Identical authored setter did not retire the previous native publication");
    need(applyNativeUltrawideCamera(ctx,base,16.0/9.0,110)&&
         closeFloat(getFloat(base,camera+0x6C),equalHeight*wrappedScale),
         "Identical authored source was mistaken for an unchanged native window");
    need(!applyNativeUltrawideCamera(ctx,base,16.0/9.0,110)&&!rt.nativeCameraProjection.nativePublication,
         "Native publication was observed as authored or left its guard armed");
    need(applyNativeUltrawideCamera(ctx,base,16.0/9.0,0)&&
         PPC_LOAD_U32(camera+0x68)==std::bit_cast<uint32_t>(equalWidth)&&
         PPC_LOAD_U32(camera+0x6C)==std::bit_cast<uint32_t>(equalHeight),
         "Original restored stale source after an identical authored setter");
    baselineWindow();
    // Original APT/effect helpers restore a saved, already-adjusted pair through
    // the same setter after temporary camera work. Those restores must retain
    // the authored source and never apply the FOV twice.
    need(PPC_LOAD_U32(0x827F5D20)==0x4BBFBF79u,"Original APT window restoration caller differs");
    need(PPC_LOAD_U32(0x826D48F0)==0x4BD1D3A9u,"Original effect window restoration caller differs");
    const auto overlayWindow=[&](float width,float height,uint32_t caller) {
        EngineCpuCalls cpu(ctx,base);const auto pair=cpu.registers().r1.u32+0x80;
        setFloat(base,pair,width);setFloat(base,pair+4,height);cpu.registers().lr=caller;
        need(cpu.invoke(0x823F1C98,camera,pair)==camera,"Original temporary window setter failed");
        need(cpu.registers().lr==caller,"Original temporary window setter changed its return address");
    };
    for(const auto restoreCaller:{0x827F5D24u,0x826D48F4u})for(const double aspect:{16.0/9.0,32.0/9.0}) {
        need(applyNativeUltrawideCamera(ctx,base,aspect,110),"FOV setup before temporary camera window failed");
        const float savedWidth=getFloat(base,camera+0x68),savedHeight=getFloat(base,camera+0x6C);
        overlayWindow(1,1,0x827F5944);
        need(rt.nativeCameraProjection.applied,"Square temporary setter replaced the authored source");
        overlayWindow(savedWidth,savedHeight,restoreCaller);
        need(rt.nativeCameraProjection.applied&&!applyNativeUltrawideCamera(ctx,base,aspect,110)&&
             PPC_LOAD_U32(camera+0x68)==std::bit_cast<uint32_t>(savedWidth)&&
             PPC_LOAD_U32(camera+0x6C)==std::bit_cast<uint32_t>(savedHeight),
             "Restored temporary window was mistaken for a new authored source or accumulated FOV");
        need(applyNativeUltrawideCamera(ctx,base,aspect,0)&&
             PPC_LOAD_U32(camera+0x6C)==std::bit_cast<uint32_t>(baselineHeight),
             "Original lost the authored vertical view after temporary restoration");
        applyNativeUltrawideCamera(ctx,base,16.0/9.0,0);
        need(wrappedOriginal==std::array{PPC_LOAD_U32(camera+0x68),PPC_LOAD_U32(camera+0x6C)},
             "Original lost the authored baseline after temporary restoration");
    }
    const auto preserved=[&](auto change,auto restore,const char* why) {
        change();const std::array<uint32_t,4> words{PPC_LOAD_U32(camera+0x68),PPC_LOAD_U32(camera+0x6C),PPC_LOAD_U32(camera+0x70),PPC_LOAD_U32(camera+0x74)};
        need(!applyNativeUltrawideCamera(ctx,base,32.0/9.0),why);
        need(!applyNativeUltrawideCamera(ctx,base,16.0/9.0,100),"Custom FOV adjusted an excluded camera");
        need(words==std::array<uint32_t,4>{PPC_LOAD_U32(camera+0x68),PPC_LOAD_U32(camera+0x6C),PPC_LOAD_U32(camera+0x70),PPC_LOAD_U32(camera+0x74)},"Excluded camera changed projection fields");restore();
    };
    preserved([&]{ctx.r3.u64=foreign;},[&]{ctx.r3.u64=camera;},"Foreign/loading camera was adjusted");
    preserved([&]{PPC_STORE_U32(camera+0x14,2);},[&]{PPC_STORE_U32(camera+0x14,1);},"Orthographic shadow camera was adjusted");
    preserved([&]{PPC_STORE_U32(color+0xC,640);},[&]{PPC_STORE_U32(color+0xC,1280);},"Half-width viewport camera was adjusted");
    preserved([&]{PPC_STORE_U32(depth+0x10,1024);},[&]{PPC_STORE_U32(depth+0x10,720);},"Square/private depth camera was adjusted");
    preserved([&]{PPC_STORE_U8(color+0x20,2);},[&]{PPC_STORE_U8(color+0x20,5);},"Loading UI raster camera was adjusted");
    preserved([&]{PPC_STORE_U32(color+0x1C,1);},[&]{PPC_STORE_U32(color+0x1C,0);},"Offset/subraster camera was adjusted");
    preserved([&]{PPC_STORE_U32(camera+0x18,0x823F1870);},[&]{PPC_STORE_U32(camera+0x18,0x823D1100);},"Foreign callback profile was adjusted");
    preserved([&]{PPC_STORE_U32(camera+0x1C,0x823F1800);},[&]{PPC_STORE_U32(camera+0x1C,0x82A77EE0);},"Foreign end callback was adjusted");
    for(const auto end:{0u,0x82A77EE0u,0x823D1100u,0x823F1800u})
        preserved([&]{PPC_STORE_U32(0x82E28170,end);},[&]{PPC_STORE_U32(0x82E28170,0x823D1160);},"Forwarder with a foreign saved end was adjusted");
    preserved([&]{ctx.lr=0x827F59AC;},[&]{ctx.lr=0x826B7E70;},"Original APT overlay camera begin was adjusted");
    preserved([&]{setFloat(base,camera+0x68,baselineHeight);},[&]{setFloat(base,camera+0x68,baselineWidth);},"Square UI window on the world camera was adjusted");
    need(!applyNativeUltrawideCamera(ctx,base,16.0/9.0)&&!applyNativeUltrawideCamera(ctx,base,std::numeric_limits<double>::quiet_NaN()),"Original/nonfinite aspect was adjusted");
    for(const uint32_t invalid:{55u,61u,115u,UINT32_MAX})
        need(!applyNativeUltrawideCamera(ctx,base,16.0/9.0,invalid),"Invalid FOV reference was adjusted");
    exerciseEarlyVisibility(rt,ctx,base);
}
}
int main(int argc,char** argv) {
    try {need(argc==2,"Original image required");exercise(argv[1]);std::puts("PASS original camera setter/frustum, early visibility and six-plane object culling, FOV/authored zoom/reset, aspect/idempotence, ABI and camera exclusions");return 0;}
    catch(const std::exception& error){std::fprintf(stderr,"FAIL ultrawide camera: %s\n",error.what());return 1;}
}
