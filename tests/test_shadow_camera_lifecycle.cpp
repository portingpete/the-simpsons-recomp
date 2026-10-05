// Actual startup and original camera/frame/raster ownership. 82714220 is
// explicit isolated-owner cleanup, NOT the shadows destructor 82705700.
// --cube also executes8273BC58 at sizes16/256. Isolated full cleanup remains
// distinct from the reflection owner's direct camera destructor823F1D48.
// --viewport uses the original manager factory/slot constructor/reset cleanup,
// table82CD12CC and both rectangular profiles; no guest ownership is fabricated.
// Full1280x720 private IDs share driver color/depth backing, as qualified by
// native-movie-composition.md and native-fullsize-depth-alias.md. Other tested
// sizes retain their existing bounded ownership profile.
// No replacement
// dispatch slots, guest allocation shim, fabricated LR, draw, or initial clear.
#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include "runtime/engine_effects.h"
#include "runtime/engine_audio_output.h"
#include "runtime/threads.h"
#include "renderer/native_backend.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {
using namespace Simpsons;
size_t checks{};
const char* stage="startup";
constexpr uint32_t createCamera=0x82704C68,destroyCamera=0x82714220;
bool cubeProfile=false;
bool viewportProfile=false;
constexpr std::array<uint32_t,6> roles={0x82D0CB00,0x82D0CAFC,0x82D0CF84,0x82D0CF90,0x82D0CF8C,0x82D0CF88};
void need(bool value,const char* message) {
    ++checks;
    if(!value) {
        std::fprintf(stderr,"CHECK %zu stage=%s: %s\n",checks,stage,message);
        throw Failure(message);
    }
}
std::vector<uint8_t> bytes(Runtime& rt,uint32_t at,uint32_t size) {
    const auto* p=rt.pointer(at,size,false);return {p,p+size};
}
void same(Runtime& rt,uint32_t at,const std::vector<uint8_t>& expected,const char* message) {
    need(!std::memcmp(rt.pointer(at,uint32_t(expected.size()),false),expected.data(),expected.size()),message);
}
template<class F> void rejects(F&& operation,const char* message) {
    try {operation();} catch(const Failure& error) {
        need(std::string(error.what()).find("target ID")!=std::string::npos,"ID rejection failed at an unrelated boundary");return;
    }
    need(false,message);
}
struct SavedAbi {uint64_t sp,lr;std::array<uint64_t,18> gpr,fpr;};
SavedAbi abi(const PPCContext& c) {
    return {c.r1.u64,c.lr,{c.r14.u64,c.r15.u64,c.r16.u64,c.r17.u64,c.r18.u64,c.r19.u64,
        c.r20.u64,c.r21.u64,c.r22.u64,c.r23.u64,c.r24.u64,c.r25.u64,c.r26.u64,c.r27.u64,
        c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64},
        {c.f14.u64,c.f15.u64,c.f16.u64,c.f17.u64,c.f18.u64,c.f19.u64,c.f20.u64,c.f21.u64,c.f22.u64,
         c.f23.u64,c.f24.u64,c.f25.u64,c.f26.u64,c.f27.u64,c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64}};
}
void sameAbi(const PPCContext& c,const SavedAbi& before) {
    const auto after=abi(c);need(after.sp==before.sp && after.lr==before.lr && after.gpr==before.gpr && after.fpr==before.fpr,
        "Original helper changed SP/LR or nonvolatile GPRs/FPRs");
}
using Nodes=std::vector<std::array<uint32_t,2>>; // address and original raster
Nodes nodes(Runtime& rt) {
    auto* base=rt.base;Nodes out;std::set<uint32_t> seen;
    for(uint32_t n=PPC_LOAD_U32(0x82D0D01C);n;n=PPC_LOAD_U32(n+4)) {
        need(out.size()<65536 && seen.insert(n).second,"Cyclic/unbounded original raster list");
        rt.pointer(n,8,false);out.push_back({n,PPC_LOAD_U32(n)});
    }
    return out;
}
uint32_t nodeFor(const Nodes& list,uint32_t raster) {
    uint32_t found=0;
    for(const auto& n:list) if(n[1]==raster) {need(!found,"Duplicate original raster node");found=n[0];}
    need(found!=0,"Missing original raster node");return found;
}
void eraseRaster(Nodes& list,uint32_t raster) {
    const auto old=list.size();std::erase_if(list,[&](const auto& row){return row[1]==raster;});
    need(list.size()+1==old,"Expected exactly one removed raster node");
}
// The same original plugin registry shape is used by cameras, frames and rasters.
void registry(Runtime& rt,uint32_t root,uint32_t id,uint32_t offset,uint32_t size,uint32_t ctor,uint32_t dtor) {
    auto* base=rt.base;std::set<uint32_t> seen;uint32_t previous=0;bool found=false;
    need(offset+size<=PPC_LOAD_U32(root),"Plugin exceeds original allocation extent");
    for(uint32_t n=PPC_LOAD_U32(root+0x10);n;n=PPC_LOAD_U32(n+0x30)) {
        need(seen.size()<256 && seen.insert(n).second,"Invalid original plugin registry chain");
        rt.pointer(n,0x3C,false);
        need(PPC_LOAD_U32(n+0x34)==previous && PPC_LOAD_U32(n+0x38)==root,"Plugin registry owner/backlink differs");
        if(PPC_LOAD_U32(n+8)==id) {
            need(!found && PPC_LOAD_U32(n)==offset && PPC_LOAD_U32(n+4)==size &&
                PPC_LOAD_U32(n+0x20)==ctor && PPC_LOAD_U32(n+0x24)==dtor,"Original plugin callbacks/extent differ");
            found=true;
        }
        previous=n;
    }
    need(found && previous==PPC_LOAD_U32(root+0x14),"Plugin missing or registry tail differs");
}
struct Camera {
    uint32_t camera{},frame{},color{},depth{},colorId{},depthId{},state{},stateRows{};
    bool sharesDefaultBacking{};
    std::vector<uint8_t> cameraBytes,colorBytes,depthBytes,stateBytes,rowBytes;
    std::weak_ptr<Graphics::RenderTarget> colorOwner;
    std::weak_ptr<Graphics::DepthTarget> depthOwner;
};
void raster(Runtime& rt,uint32_t r,uint32_t type,uint32_t identity,uint32_t extent,uint32_t height) {
    auto* base=rt.base;const uint32_t x=r+PPC_LOAD_U32(0x82E3DC94);
    rt.pointer(r,PPC_LOAD_U32(0x82CD1E28),false);
    need(PPC_LOAD_U32(r)==r && !PPC_LOAD_U32(r+4) && !PPC_LOAD_U32(r+8),"Raster root/pixel/palette differs");
    need(PPC_LOAD_U32(r+0xC)==extent && PPC_LOAD_U32(r+0x10)==height && PPC_LOAD_U32(r+0x14)==32,
        "Original raster dimensions/normalized depth differ");
    need(!PPC_LOAD_U16(r+0x1C) && !PPC_LOAD_U16(r+0x1E) && PPC_LOAD_U8(r+0x20)==type &&
        !PPC_LOAD_U8(r+0x21) && !PPC_LOAD_U8(r+0x22) && PPC_LOAD_U8(r+0x23)==(type==5?0xB:9),
        "Original root raster flags/offsets/format index differ");
    need(PPC_LOAD_U32(x)==identity && !PPC_LOAD_U32(x+4) &&
        PPC_LOAD_U32(x+8)==(type==5?0x010000FFu:0xFFu) && !PPC_LOAD_U32(x+0xC) &&
        PPC_LOAD_U32(x+0x18)==(type==5?0x182801B6u:0x1A220197u),"Original raster extension differs");
    // R+18/+24/+28/+2C/+30 and X+10/+14/+1C are unspecified on allocation.
    // Capture them below, without poisoning real storage or asserting zeros.
    need(identity && !rt.pageAccess[identity>>12].load(),"Owned surface ID is missing or SDK-addressable");
    for(const uint32_t slot:roles) need(identity!=PPC_LOAD_U32(slot),"Private logical surface ID aliases a default/copy/front ID");
}
void intact(Runtime& rt,const Camera& c) {
    same(rt,c.camera,c.cameraBytes,"Other camera allocation/cleanup changed this camera");
    same(rt,c.color,c.colorBytes,"Other camera changed color metadata/padding");
    same(rt,c.depth,c.depthBytes,"Other camera changed depth metadata/padding");
    if(c.state) {
        same(rt,c.state,c.stateBytes,"Other camera changed CPU state ownership");
        same(rt,c.stateRows-4,c.rowBytes,"Other camera changed CPU state rows");
    }
    need(!c.colorOwner.expired() && !c.depthOwner.expired(),"Other camera released this camera's native backing");
}
void retiredBacking(Runtime& rt,const Camera& c) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;
    if(c.sharesDefaultBacking) {
        // Retiring a private view revokes its ID, not the driver's storage.
        bool alphaOne=true;
        const auto color=driver.color(PPC_LOAD_U32(roles[0]),alphaOne);
        const auto depth=driver.depth(PPC_LOAD_U32(roles[1]));
        need(color && depth && !alphaOne && c.colorOwner.lock()==color && c.depthOwner.lock()==depth,
            "Fullsize private retirement released or replaced shared driver backing");
    } else need(c.colorOwner.expired() && c.depthOwner.expired(),"Original cleanup retained independent native target owners");
}
Camera make(Runtime& rt,EngineCpuCalls& cpu,Nodes& expected,std::set<uint32_t>& identities,uint32_t extent,uint32_t manager=0,uint32_t slot=0) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;Camera c;
    const uint32_t height=viewportProfile?720:extent;
    c.sharesDefaultBacking=extent==1280 && height==720;
    const auto beforeAbi=abi(cpu.registers());const auto beforeNodes=nodes(rt);
    const auto scratchLeft=bytes(rt,0x82D0CFFC,4),scratchRight=bytes(rt,0x82D0D004,0x18);
    const auto count=driver.rasterCount();
    if(viewportProfile) {
        const uint32_t row=0x82CD12CC+slot*20;
        auto& ctx=cpu.registers();
        ctx.f1.f64=std::bit_cast<float>(PPC_LOAD_U32(row));ctx.f2.f64=std::bit_cast<float>(PPC_LOAD_U32(row+4));
        ctx.f3.f64=std::bit_cast<float>(PPC_LOAD_U32(row+8));ctx.f4.f64=std::bit_cast<float>(PPC_LOAD_U32(row+12));
        need(!PPC_LOAD_U32(manager+0x14+4*slot),"Viewport slot already owns a camera");
        cpu.invoke(0x8269D608,manager,slot);c.camera=PPC_LOAD_U32(manager+0x14+4*slot);
    } else c.camera=cubeProfile?cpu.invoke(0x8273BC58,extent):cpu.invoke(createCamera,extent,extent);
    sameAbi(cpu.registers(),beforeAbi);
    need(c.camera && !(c.camera&3),"Original shadow camera allocation failed");
    rt.pointer(c.camera,PPC_LOAD_U32(0x82CD1B30),false);
    c.frame=PPC_LOAD_U32(c.camera+4);c.color=PPC_LOAD_U32(c.camera+0x60);c.depth=PPC_LOAD_U32(c.camera+0x64);
    need(c.frame && c.color && c.depth && c.color!=c.depth,"Camera lost original frame/raster allocations");
    // Registered plugin 0509 replaces the base camera callbacks (823D29E0).
    need(PPC_LOAD_U32(c.camera)==0x04000000 && PPC_LOAD_U32(c.camera+0x10)==0x823D2940 &&
        PPC_LOAD_U32(c.camera+0x14)==((cubeProfile||viewportProfile)?1u:2u) && PPC_LOAD_U32(c.camera+0x18)==0x823D1100 &&
        PPC_LOAD_U32(c.camera+0x1C)==0x823D1160,"Original camera header/projection/plugin callbacks differ");
    for(uint32_t i=0;i<2;++i) {
        // Reflection retains base823F1DB0's1.0 window and zero offset;
        // the shadow helper replaces them with its two original globals.
        const float ratio=float(extent)/float(height);
        const float horizontal=std::bit_cast<float>(0x3F13CD3Au);
        const uint32_t value=viewportProfile?std::bit_cast<uint32_t>(i?horizontal/ratio:horizontal):
            (cubeProfile?PPC_LOAD_U32(0x82000BB0):PPC_LOAD_U32(0x82D6C0A8+4*i));
        const float reciprocal=1.0f/std::bit_cast<float>(value);
        need(PPC_LOAD_U32(c.camera+0x68+4*i)==value &&
            PPC_LOAD_U32(c.camera+0x70+4*i)==std::bit_cast<uint32_t>(reciprocal),"Original camera view window/reciprocal differs");
        need(PPC_LOAD_U32(c.camera+0x78+4*i)==PPC_LOAD_U32((cubeProfile||viewportProfile)?0x821DD0D8:0x82D6C0A0+4*i),"Camera view offset differs");
    }
    need(PPC_LOAD_U32(c.camera+0x80)==0x3F800000 && PPC_LOAD_U32(c.camera+0x84)==PPC_LOAD_U32(viewportProfile?0x8214E7A8:(cubeProfile?0x82150490:0x8214E510)),
        "Original shadow near/far clip differs");
    rt.pointer(c.frame,PPC_LOAD_U32(0x82CD1B50),false);
    need(!PPC_LOAD_U32(c.frame+4) && !PPC_LOAD_U32(c.frame+0x98) && !PPC_LOAD_U32(c.frame+0x9C) &&
        PPC_LOAD_U32(c.frame+0xA0)==c.frame && PPC_LOAD_U32(c.frame)==0xF,"Frame root/children/dirty flags differ");
    need(PPC_LOAD_U32(c.frame+0x90)==c.camera+8 && PPC_LOAD_U32(c.frame+0x94)==c.camera+8 &&
        PPC_LOAD_U32(c.camera+8)==c.frame+0x90 && PPC_LOAD_U32(c.camera+0xC)==c.frame+0x90,
        "Camera is not the sole original frame-list attachment");
    for(uint32_t block:{0x10u,0x50u}) for(uint32_t word=0;word<16;++word)
        need(PPC_LOAD_U32(c.frame+block+word*4)==((word==0 || word==5 || word==10 || word==15)?0x3F800000u:0u),
            "Original frame identity matrix differs");
    c.state=PPC_LOAD_U32(c.camera+PPC_LOAD_U32(0x82CED790));
    if(cubeProfile) {
        // Plugin constructor8269E2B8 initializes this field to zero. Only the
        // shadow and viewport helpers call826B84D8 and replace it with21 rows.
        need(!c.state,"Reflection camera unexpectedly acquired shadow state rows");
    } else {
        need(c.state && PPC_LOAD_U32(c.state)==21,"Camera lacks the actual 21-entry CPU state allocation");
        c.stateRows=PPC_LOAD_U32(c.state+4);
        need(c.stateRows && PPC_LOAD_U32(c.stateRows-4)==21,"CPU state array/header differs");
        rt.pointer(c.stateRows,21*48,false);
        for(uint32_t i=0;i<21;++i) need(PPC_LOAD_U32(c.stateRows+i*48+20)!=0,"CPU state row lacks original owned storage");
    }
    const auto extension=PPC_LOAD_U32(0x82E3DC94);
    c.colorId=PPC_LOAD_U32(c.color+extension);c.depthId=PPC_LOAD_U32(c.depth+extension);
    raster(rt,c.color,5,c.colorId,extent,height);raster(rt,c.depth,1,c.depthId,extent,height);
    need(identities.insert(c.colorId).second && identities.insert(c.depthId).second,"Native surface identity reused");
    need(!PPC_LOAD_U32(0x82CD1D88),"Private camera changed the consumed shared-depth flag");
    need(PPC_LOAD_U32(0x82D0D000)==0x01200B00,"Original format-helper scratch effect was lost");
    same(rt,0x82D0CFFC,scratchLeft,"Format helper changed preceding scratch bytes");
    same(rt,0x82D0D004,scratchRight,"Format helper changed following scratch bytes");
    const auto list=nodes(rt);const auto cn=nodeFor(list,c.color),dn=nodeFor(list,c.depth);
    expected=beforeNodes;expected.insert(expected.begin(),{cn,c.color});expected.insert(expected.begin(),{dn,c.depth});
    need(list==expected && cn!=dn && driver.rasterCount()==count+2,"Original raster insertion/order/count differs");
    bool alphaOne=true;
    {
        const auto color=driver.color(c.colorId,alphaOne);const auto depth=driver.depth(c.depthId);
        need(color && !alphaOne && color->width==extent && color->height==height &&
            color->format==Graphics::TargetFormat::RGB10A2,"Owned color backing/alpha policy differs");
        need(depth && depth->pixelWidth()==extent && depth->pixelHeight()==height,"Owned depth backing extent differs");
        bool defaultAlpha{};
        const auto defaultColor=driver.color(PPC_LOAD_U32(roles[0]),defaultAlpha);
        const auto defaultDepth=driver.depth(PPC_LOAD_U32(roles[1]));
        if(c.sharesDefaultBacking)
            need(color==defaultColor && depth==defaultDepth,"Fullsize private views do not share qualified driver backing");
        else need(color!=defaultColor && depth!=defaultDepth,"Other qualified camera size changed its independent backing profile");
        // Same-format resolve textures and rotating fronts are separate storage.
        need(depth!=driver.depth(PPC_LOAD_U32(roles[2])),"Private depth aliases the separate depth copy");
        for(size_t role:{size_t(3),size_t(4),size_t(5)}) {
            bool copyAlpha{};
            need(color!=driver.color(PPC_LOAD_U32(roles[role]),copyAlpha),"Private color aliases a separate copy/front texture");
        }
        c.colorOwner=color;c.depthOwner=depth;
    }
    // A real copy/map proves backing exists. Undefined initial GPU contents and
    // the unused bytes in each native depth/stencil texel have no expected value.
    need(driver.readbackColor(c.colorId).size()==size_t(extent)*height*4,"Owned RGB10A2 readback extent differs");
    need(driver.readbackDepth(c.depthId).size()==size_t(extent)*height*8,"Owned depth/stencil readback extent differs");
    rejects([&]{driver.depth(c.colorId);},"Color ID accepted as depth");
    rejects([&]{driver.color(c.depthId,alphaOne);},"Depth ID accepted as color");
    c.cameraBytes=bytes(rt,c.camera,PPC_LOAD_U32(0x82CD1B30));
    c.colorBytes=bytes(rt,c.color,PPC_LOAD_U32(0x82CD1E28));c.depthBytes=bytes(rt,c.depth,PPC_LOAD_U32(0x82CD1E28));
    if(c.state) {c.stateBytes=bytes(rt,c.state,8);c.rowBytes=bytes(rt,c.stateRows-4,4+21*48);}
    std::printf("CAMERA %08X frame=%08X color=%08X/%08X depth=%08X/%08X nodes=%08X,%08X state=%08X rows=%08X\n",
        c.camera,c.frame,c.color,c.colorId,c.depth,c.depthId,cn,dn,c.state,c.stateRows);
    return c;
}
void destroy(Runtime& rt,EngineCpuCalls& cpu,const Camera& c,Nodes& expected) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;
    intact(rt,c);
    // 82714220 also visits children and can detach a parent. Those paths are
    // deliberately excluded: this helper just created a root with one object.
    need(!PPC_LOAD_U32(c.frame+4) && !PPC_LOAD_U32(c.frame+0x98) &&
        PPC_LOAD_U32(c.frame+0x90)==c.camera+8 && PPC_LOAD_U32(c.frame+0x94)==c.camera+8,
        "Full cleanup has unqualified frame ownership");
    for(uint32_t r:{c.color,c.depth}) driver.preflightRasterDestroy(base,r);
    const auto saved=abi(cpu.registers());const auto count=driver.rasterCount();
    cpu.invoke(destroyCamera,c.camera);sameAbi(cpu.registers(),saved);
    eraseRaster(expected,c.color);eraseRaster(expected,c.depth);
    need(nodes(rt)==expected && driver.rasterCount()+2==count,"Original camera cleanup retained raster ownership/list entries");
    retiredBacking(rt,c);
    bool alphaOne{};
    rejects([&]{driver.color(c.colorId,alphaOne);},"Retired color ID still resolves");
    rejects([&]{driver.depth(c.depthId);},"Retired depth ID still resolves");
    rejects([&]{driver.readbackColor(c.colorId);},"Retired color ID still reads back");
    rejects([&]{driver.readbackDepth(c.depthId);},"Retired depth ID still reads back");
    need(!PPC_LOAD_U32(0x82CD1D88),"Original cleanup rearmed shared depth");
    // Freed guest objects are not dereferenced. Subsequent genuine allocator
    // reuse, stale-ID rejection and backing lifetime are retirement checks.
}
void probe(Runtime& rt,EngineCpuCalls& cpu) {
    auto* base=rt.base;need(rt.engineDriver && rt.engineDriver->started(),"Missing actual native driver");
    auto& driver=*rt.engineDriver;const uint32_t context=PPC_LOAD_U32(0x82D5DA74);
    driver.requireContext(context);
    need(!PPC_LOAD_U32(0x82D0CAF8) && !PPC_LOAD_U32(0x82D08BFC) && !driver.effects().count(),"Unexpected SDK device/FX manager/native FX");
    need(!PPC_LOAD_U32(0x82CD1D88),"Startup did not consume the shared-depth role");
    registry(rt,0x82CD1E28,0x40C,PPC_LOAD_U32(0x82E3DC94),0x20,0x823F52B8,0x823F52C0);
    registry(rt,0x82CD1B30,0xEA44,PPC_LOAD_U32(0x82CED790),4,0x8269E2B8,0x8269E2D0);
    const auto baseline=nodes(rt);auto expected=baseline;const auto count=driver.rasterCount(),clears=driver.cameraClearCount();
    const auto table=bytes(rt,0x82CEFD20,25*16),stages=bytes(rt,0x82D0E3F8,8*0x18),attachments=bytes(rt,0x82D0CF58,20);
    std::array<uint32_t,6> roleIds{};for(size_t i=0;i<roles.size();++i) roleIds[i]=PPC_LOAD_U32(roles[i]);
    const uint32_t startupCamera=PPC_LOAD_U32(0x82E07248);
    need(startupCamera!=0 && count>=2,"Actual startup camera is absent");
    const auto startupBytes=bytes(rt,startupCamera,PPC_LOAD_U32(0x82CD1B30));
    std::set<uint32_t> identities;std::array<Camera,2> previous;
    if(cubeProfile) {
        for(const auto profile:std::array<std::array<uint32_t,2>,3>{{{16,1},{256,64},{1024,832}}}) {
            const auto saved=abi(cpu.registers());
            need(cpu.invoke(0x823ED930,profile[0],profile[0],0x18280186,0)==profile[1],"Original private-depth placement scalar differs");
            sameAbi(cpu.registers(),saved);
        }
    }
    for(uint32_t cycle=0;cycle<2;++cycle) {
        stage=cubeProfile?"two simultaneous original reflection cameras":"two simultaneous original shadow cameras";
        std::array<Camera,2> pair;
        pair[0]=make(rt,cpu,expected,identities,cubeProfile?16:1024);
        pair[1]=make(rt,cpu,expected,identities,cubeProfile?256:1024);intact(rt,pair[0]);
        need(pair[0].camera!=pair[1].camera && pair[0].frame!=pair[1].frame &&
             (cubeProfile || (pair[0].state!=pair[1].state && pair[0].stateRows!=pair[1].stateRows)),
             "Concurrent cameras alias original CPU ownership");
        need(pair[0].colorOwner.lock()!=pair[1].colorOwner.lock() && pair[0].depthOwner.lock()!=pair[1].depthOwner.lock(),
            "Concurrent private surfaces share backing");
        if(cycle) {
            bool rasterReuse=false,cameraReuse=false,frameReuse=false,crossTypeReuse=false;
            for(const auto& old:previous) for(const auto& now:pair) {
                cameraReuse|=old.camera==now.camera;frameReuse|=old.frame==now.frame;
                rasterReuse|=old.color==now.color || old.color==now.depth || old.depth==now.color || old.depth==now.depth;
                crossTypeReuse|=old.frame==now.color || old.frame==now.depth || old.color==now.frame || old.depth==now.frame;
            }
            // Frames use the general engine heap. A freed frame can become a
            // raster or another allocation; same-type frame reuse is not an
            // allocator guarantee. Shadows demonstrate same-raster reuse;
            // reflection's different heap traffic reuses an old frame as a
            // raster. Both paths must reject all previous native surface IDs.
            need(cameraReuse && (cubeProfile?crossTypeReuse:rasterReuse),"Second cycle did not demonstrate original camera/CPU heap reuse");
            std::printf("REUSE camera=%u raster=%u frame=%u cross_type=%u; allocation type reuse is observed, not forced\n",
                unsigned(cameraReuse),unsigned(rasterReuse),unsigned(frameReuse),unsigned(crossTypeReuse));
            bool alphaOne{};
            for(const auto& old:previous) {
                rejects([&]{driver.color(old.colorId,alphaOne);},"Old color ID revived after heap reuse");
                rejects([&]{driver.depth(old.depthId);},"Old depth ID revived after heap reuse");
            }
        }
        stage="original full camera cleanup";
        // Remove the older camera first, exercising interior raster-list unlink.
        destroy(rt,cpu,pair[0],expected);intact(rt,pair[1]);destroy(rt,cpu,pair[1],expected);
        need(expected==baseline && nodes(rt)==baseline && driver.rasterCount()==count,"Camera cycle did not restore baseline list/count");
        previous=pair;
    }
    for(size_t i=0;i<roles.size();++i) need(PPC_LOAD_U32(roles[i])==roleIds[i],"Private lifetime changed a driver role identity");
    same(rt,startupCamera,startupBytes,"Shadow fixture changed the startup camera");
    same(rt,0x82CEFD20,table,"Shadow fixture modified FX registration table");
    same(rt,0x82D0E3F8,stages,"Allocation-only fixture changed raster sampling stages");
    same(rt,0x82D0CF58,attachments,"Allocation-only fixture changed target attachment caches");
    need(driver.cameraClearCount()==clears && !driver.effects().count() && !PPC_LOAD_U32(0x82D08BFC) &&
        !PPC_LOAD_U32(0x82D0CAF8) && currentContext==&cpu.registers(),"Allocation lifetime created FX/SDK device or cleared a camera");
    driver.requireContext(context);
    bool alphaOne{};need(bool(driver.color(roleIds[0],alphaOne)) && bool(driver.depth(roleIds[1])),"Default backing did not survive private cleanup");
}
void viewportProbe(Runtime& rt,EngineCpuCalls& cpu) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;stage="original viewport manager construction";
    const auto baseline=nodes(rt);const auto count=driver.rasterCount(),clears=driver.cameraClearCount();
    const auto stages=bytes(rt,0x82D0E3F8,8*0x18),attachments=bytes(rt,0x82D0CF58,20);
    const uint32_t sourceCamera=PPC_LOAD_U32(0x82D61D50);
    need(sourceCamera!=0,"Original viewport source camera absent");
    const auto source=bytes(rt,sourceCamera,PPC_LOAD_U32(0x82CD1B30));
    const auto sourceRaster=PPC_LOAD_U32(sourceCamera+0x60);
    need(PPC_LOAD_U32(sourceRaster+0xC)==1280 && PPC_LOAD_U32(sourceRaster+0x10)==720 && PPC_LOAD_U32(sourceRaster+0x14)==32,
        "Original viewport source dimensions/depth differ");
    need(!PPC_LOAD_U32(0x82D08B10),"Viewport manager already exists before fixture");
    const uint32_t manager=cpu.invoke(0x8269D788);
    need(manager && PPC_LOAD_U32(0x82D08B10)==manager && PPC_LOAD_U32(manager+0x128)==4,"Original viewport manager factory differs");
    for(uint32_t width:{1280u,640u}) {
        const auto saved=abi(cpu.registers());
        need(cpu.invoke(0x823ED930,width,720,0x18280186,0)==(width==1280?0x2D0u:0x168u),"Original rectangular depth placement differs");
        sameAbi(cpu.registers(),saved);
    }
    cpu.registers().f1.u64=PPC_LOAD_U64(0x8214E7B0);cpu.invoke(0x82A3C710);
    need(cpu.registers().f1.u64==0x3FE279A74FFE43EBull &&
        std::bit_cast<uint32_t>(float(cpu.registers().f1.f64))==0x3F13CD3A,"Original camera projection scalar differs");
    std::set<uint32_t> identities;Nodes expected=baseline;std::array<Camera,3> previous;
    for(uint32_t cycle=0;cycle<2;++cycle) {
        stage="original viewport manager slot construction";std::array<Camera,3> cameras;
        for(uint32_t slot=0;slot<3;++slot) {
            cameras[slot]=make(rt,cpu,expected,identities,slot?640:1280,manager,slot);
            const uint32_t left=slot==2?640:0,right=slot==1?640:1280,row=manager+16*slot;
            need(PPC_LOAD_U32(row+0x24)==left && !PPC_LOAD_U32(row+0x28) && PPC_LOAD_U32(row+0x2C)==right && PPC_LOAD_U32(row+0x30)==720,
                "Original viewport pixel rectangle differs");
            const uint32_t normalizedLeft=slot==2?0x3F000000:0,normalizedRight=slot==1?0x3F000000:0x3F800000;
            need(PPC_LOAD_U32(row+0xA4)==normalizedLeft && !PPC_LOAD_U32(row+0xA8) && PPC_LOAD_U32(row+0xAC)==normalizedRight && PPC_LOAD_U32(row+0xB0)==0x3F800000,
                "Original normalized viewport rectangle differs");
            need(PPC_LOAD_U32(manager+0xE4+4*slot)==(slot?0x820B68C0u:0x820B67B8u),"Original resolution descriptor differs");
            need(PPC_LOAD_U32(row+0x64)==(slot==2?0u:0xBF800000u) && PPC_LOAD_U32(row+0x68)==0x3F800000 &&
                PPC_LOAD_U32(row+0x6C)==(slot==1?0u:0x3F800000u) && PPC_LOAD_U32(row+0x70)==0xBF800000,
                "Original clip-space viewport rectangle differs");
            for(uint32_t earlier=0;earlier<slot;++earlier) {
                intact(rt,cameras[earlier]);
                need(cameras[earlier].camera!=cameras[slot].camera && cameras[earlier].frame!=cameras[slot].frame &&
                    cameras[earlier].state!=cameras[slot].state && cameras[earlier].stateRows!=cameras[slot].stateRows &&
                    cameras[earlier].colorOwner.lock()!=cameras[slot].colorOwner.lock() && cameras[earlier].depthOwner.lock()!=cameras[slot].depthOwner.lock(),
                    "Concurrent viewport cameras alias original or native ownership");
            }
        }
        need(!PPC_LOAD_U32(manager+0x20),"Three viewport allocations changed the unused fourth slot");
        if(cycle) {
            bool reused=false;for(const auto& old:previous) for(const auto& now:cameras) reused|=old.camera==now.camera;
            need(reused,"Original camera allocator did not demonstrate reuse");
            bool alpha{};for(const auto& old:previous) {
                rejects([&]{driver.color(old.colorId,alpha);},"Old rectangular color ID revived after reuse");
                rejects([&]{driver.depth(old.depthId);},"Old rectangular depth ID revived after reuse");
            }
        }
        stage="original viewport manager paired reset";
        auto managerAfter=bytes(rt,manager,0x140);for(uint32_t slot=0;slot<3;++slot)
            std::fill_n(managerAfter.begin()+0x14+4*slot,4,uint8_t(0));
        for(const auto& c:cameras) {intact(rt,c);driver.preflightRasterDestroy(base,c.color);driver.preflightRasterDestroy(base,c.depth);}
        const auto saved=abi(cpu.registers());cpu.invoke(0x8269C438,manager);sameAbi(cpu.registers(),saved);
        same(rt,manager,managerAfter,"Original manager reset changed fields beyond its camera slots");
        for(const auto& c:cameras) {
            eraseRaster(expected,c.color);eraseRaster(expected,c.depth);
            retiredBacking(rt,c);
            bool alpha{};rejects([&]{driver.color(c.colorId,alpha);},"Retired viewport color remains valid");
            rejects([&]{driver.depth(c.depthId);},"Retired viewport depth remains valid");
            rejects([&]{driver.readbackColor(c.colorId);},"Retired viewport color reads back");
            rejects([&]{driver.readbackDepth(c.depthId);},"Retired viewport depth reads back");
        }
        need(expected==baseline && nodes(rt)==baseline && driver.rasterCount()==count,"Manager reset failed to restore native/original raster ownership");
        cpu.invoke(0x8269C438,manager);same(rt,manager,managerAfter,"Repeated empty manager reset changed its fields");
        previous=cameras;
    }
    // The original destructor also owns an array of four CPU camera controllers
    // and two message subscriptions. Exercise its real virtual deleting entry.
    stage="original viewport manager deleting destructor";cpu.invoke(0x8269CC28);
    need(!PPC_LOAD_U32(0x82D08B10) && driver.rasterCount()==count && nodes(rt)==baseline,"Original manager deletion retained singleton/raster ownership");
    same(rt,sourceCamera,source,"Viewport manager lifetime changed the original source camera");
    same(rt,0x82D0E3F8,stages,"Viewport allocation changed sampling stages");same(rt,0x82D0CF58,attachments,"Viewport allocation changed target attachments");
    need(driver.cameraClearCount()==clears && !PPC_LOAD_U32(0x82D0CAF8),"Viewport lifetime cleared a camera or created an SDK device");
}
struct StartupObserved {};
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        cubeProfile=argc==3 && std::string(argv[2])=="--cube";
        viewportProfile=argc==3 && std::string(argv[2])=="--viewport";
        need(argc==2 || cubeProfile || viewportProfile,"Original image path and optional --cube/--viewport required");
        Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);
        try {
            const auto entry=original;bool sourceObserved=false,startupObserved=false;
            rt.audioBoundaryObserver=[&](uint32_t pc,PPCContext&,uint8_t* base) {
                need(bool(rt.engineAudioOutput),"Missing actual muted audio output");const auto view=rt.engineAudioOutput->view();
                need(view.nativeEngine && view.muted && view.configured && view.identity,"Missing actual muted native Dac source");
                if(pc==0x82345920) {
                    need(!sourceObserved && !view.active && !view.workerId,"Source observed after worker construction");sourceObserved=true;return;
                }
                need(pc==0x828166FC && sourceObserved && view.active && view.workerId && view.event && !PPC_LOAD_U32(0x82D08BFC),
                    "Missing established post-audio/pre-FX startup boundary");
                std::shared_ptr<KernelHandle> worker;
                {std::lock_guard lock(rt.threadMutex);for(const auto& thread:rt.threads) if(thread->id==view.workerId) worker=thread->object;}
                need(worker && GetThreadId(worker->native)==view.workerId && view.workerId!=GetCurrentThreadId() &&
                    WaitForSingleObject(worker->native,0)==WAIT_TIMEOUT,"Dac worker is not a real live OS thread");
                throw StartupObserved{};
            };
            try {runOriginal(original,rt.base);} catch(const StartupObserved&) {startupObserved=true;}
            rt.audioBoundaryObserver={};need(startupObserved,"Startup missed its established observer boundary");
            EngineCpuCalls cpu(entry,rt.base);
            if(viewportProfile) {
                viewportProbe(rt,cpu);
                std::printf("PASS viewport camera lifecycle: %zu checks; original manager factory, three original slots, two cycles, paired manager reset/deletion, actual heap reuse, original projection/rectangles, real backing; ALL MUTED; no initial pixel or drawing claim\n",checks);
            } else {
                probe(rt,cpu);
                std::printf("PASS %s camera lifecycle: %zu checks; two concurrent cameras, two cycles, actual heap reuse, explicit isolated-owner cleanup 82714220, stale IDs, real backing; ALL MUTED; allocation/lifetime only; normal owner teardown unqualified\n",cubeProfile?"reflection":"shadow",checks);
            }
            std::fflush(stdout);
        } catch(const std::exception& error) {
            std::fprintf(stderr,"Shadow fixture failed before Runtime teardown: stage=%s checks=%zu error=%s\n",stage,checks,error.what());throw;
        }
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL shadow camera lifecycle: %s\n",error.what());return 1;}
}
