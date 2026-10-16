#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "renderer/skin_mesh.h"
#include "header/test_graphics_device.h"
#include "renderer/d3d_call_stats.h"
#include "renderer/device_availability.h"
#include "renderer/native_material_compiler.h"
#include "renderer/effect_reflection.h"
#include "runtime/skin_material_constants.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <thread>
#include <vector>
using namespace Simpsons::Graphics;
extern "C" uint64_t gD3DDeviceCounts[64];
void require(bool b,const char* why){if(!b)throw Error(why);}
template<class F>void rejects(F action){bool failed=false;try{action();}catch(const Error&){failed=true;}require(failed,"Invalid skin operation succeeded");}
#include "header/test_engine_binding_reset.h"
#include "header/test_mesh_gpu_retirement.h"
#include "header/test_r16_strip_boundary.h"
#include "header/test_palette_interpolation.h"
namespace Simpsons::Graphics {
struct NativeRecordingProbe {
    static ID3D11DeviceContext* context(NativeBackend& b){return b.context.Get();}
    static ID3D11Device* device(NativeBackend& b){return b.device.Get();}
    static ComPtr<ID3D11Buffer> depthConstants(NativeBackend& b,const SkinMeshDraw& d) {
        return b.sceneDepthConstantBuffer(d.viewport[4]==0x3F800000&&d.viewport[5]==0,d.depthBiasBits,d.slopeBiasBits);
    }
    static void resetSceneCache(NativeBackend& b){b.resetSceneStateCacheForDevice();}
    static void replaceDevice(NativeBackend& b,bool hardware) {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        const auto result=createTestDevice(hardware,D3D11_CREATE_DEVICE_BGRA_SUPPORT,&device,&level,&context);
        require(SUCCEEDED(result),"Skin depth-constant device replacement failed");
        b.availability=std::make_shared<DeviceAvailability>(device.Get());b.device=device;b.context=context;b.featureLevel=level;
    }
};
}
void depthConstantBytes(NativeBackend& backend,ID3D11Buffer* buffer,const SkinMeshDraw& draw) {
    ComPtr<ID3D11Device> device;buffer->GetDevice(&device);
    require(device.Get()==NativeRecordingProbe::device(backend),"Skin depth constant belongs to another device");
    D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);
    require(desc.ByteWidth==16&&desc.Usage==D3D11_USAGE_IMMUTABLE&&desc.BindFlags==D3D11_BIND_CONSTANT_BUFFER&&
        !desc.CPUAccessFlags&&!desc.MiscFlags&&!desc.StructureByteStride,"Skin recorded depth constant is not an exact immutable16 buffer");
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;require(SUCCEEDED(device->CreateBuffer(&desc,nullptr,&staging)),"Skin depth constant staging failed");
    auto* context=NativeRecordingProbe::context(backend);context->CopyResource(staging.Get(),buffer);
    D3D11_MAPPED_SUBRESOURCE mapped{};require(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Skin depth constant readback failed");
    const std::array<uint32_t,4> expected{uint32_t(draw.viewport[4]==0x3F800000&&draw.viewport[5]==0),draw.depthBiasBits,draw.slopeBiasBits,0};
    const bool exact=mapped.pData&&mapped.RowPitch>=16&&!std::memcmp(mapped.pData,expected.data(),16);context->Unmap(staging.Get(),0);
    require(exact,"Skin recorded depth constant GPU bytes changed");
}
uint32_t word(std::span<const uint8_t> b,size_t at){uint32_t v;std::memcpy(&v,b.data()+at,4);return v;}
void uploadCacheReuse(bool hardware) {
    NativeBackend backend(!hardware);
    std::array<SkinVertex,4> base{};
    base[0].position={-1,1,.25f};base[1].position={-1,-1,.25f};base[2].position={1,1,.25f};base[3].position={1,-1,.25f};
    for(auto& v:base){v.normal={0,0,1};v.indices={1,0,0,0};v.weights={1,0,0,0};v.color={0,.3f,1,1};v.uv={.25f,.25f};v.uv1={.140625f,.125f};}
    const std::array<uint16_t,4> strip{0,1,2,3};
    std::vector<SkinVertex> vv(base.begin(), base.end());
    std::vector<uint16_t> ii(strip.begin(), strip.end());
    auto first = backend.uploadSkinMesh(vv, ii, 0x8201E6DC);
    require(backend.uploadSkinMesh(vv, ii, 0x8201E6DC).get() == first.get(), "Skin repeat upload did not reuse exact bytes");
    // Same content at a different guest address must still hit (never by pointer).
    std::vector<SkinVertex> vvAlias = vv;
    std::vector<uint16_t> iiAlias = ii;
    require(vvAlias.data() != vv.data(), "Skin alias fixture shares guest address");
    require(backend.uploadSkinMesh(vvAlias, iiAlias, 0x8201E6DC).get() == first.get(), "Skin identical bytes at new address missed");
    // Identical bytes under the other shader profile must never alias.
    auto otherProfile = backend.uploadSkinMesh(vv, ii, 0x82007C1C);
    require(otherProfile.get() != first.get(), "Skin identical bytes aliased across shader profiles");
    require(backend.uploadSkinMesh(vv, ii, 0x82007C1C).get() == otherProfile.get(), "Skin base-profile repeat missed");
    require(backend.uploadSkinMesh(vv, ii, 0x8201E6DC).get() == first.get(), "Skin dual-profile hit lost after base upload");
    auto texturedProfile=backend.uploadSkinMesh(vv,ii,0x8201146C);
    require(texturedProfile.get()!=first.get()&&texturedProfile.get()!=otherProfile.get(),"Textured skin cache aliased another source profile");
    require(backend.uploadSkinMesh(vvAlias,iiAlias,0x820126EC).get()==texturedProfile.get(),"Textured skin opaque/alpha exact bytes missed their qualified common layout cache");
    // Caller mutation isolation: the cache snapshots exact bytes on insertion.
    const std::vector<SkinVertex> origV = vv;
    const std::vector<uint16_t> origI = ii;
    vv[0].uv[0] += 0.5f;
    require(backend.uploadSkinMesh(vv, ii, 0x8201E6DC).get() != first.get(), "Skin single-float change incorrectly hit");
    require(backend.uploadSkinMesh(origV, origI, 0x8201E6DC).get() == first.get(), "Skin original bytes missed after caller mutation");
    const auto snapshot = backend.readbackSkinMeshVertices(first);
    require(snapshot.size() == origV.size() &&
        !std::memcmp(snapshot.data(), origV.data(), origV.size() * sizeof(SkinVertex)),
        "Skin cached mesh changed after caller mutation");
    // Invalid bytes can never hit: they miss and run the existing checks.
    bool rejected = false;
    try {
        auto bad = origV;
        bad[0].position[0] = std::numeric_limits<float>::quiet_NaN();
        backend.uploadSkinMesh(bad, origI, 0x8201E6DC);
    } catch (const Error& e) {
        rejected = std::string(e.what()).find("Nonfinite skin position") != std::string::npos;
    }
    require(rejected, "Skin invalid position accepted after caching");
    const std::array<uint16_t,3> opaque = {0, 1, 4};
    const auto indexed=backend.uploadSkinMesh(origV,opaque,0x8201E6DC);
    require(indexed!=first&&indexed->indexCount()==opaque.size()&&
            backend.uploadSkinMesh(origV,opaque,0x8201E6DC)==indexed,
            "Skin opaque R16 bytes lost immutable upload/cache ownership before draw qualification");
    require(backend.uploadSkinMesh(origV, origI, 0x8201E6DC).get() == first.get(), "Skin valid bytes missed after invalid rejections");
    // Device isolation: no static global, no cross-device hits.
    NativeBackend foreign(!hardware);
    auto foreignMesh = foreign.uploadSkinMesh(origV, origI, 0x8201E6DC);
    require(foreignMesh.get() != first.get(), "Skin cross-device hit");
    rejected = false;
    try {
        backend.bindSkinMeshVertices(foreignMesh);
    } catch (const Error& e) {
        rejected = std::string(e.what()).find("another backend") != std::string::npos;
    }
    require(rejected, "Skin foreign mesh bound on local device");
    require(backend.uploadSkinMesh(origV, origI, 0x8201E6DC).get() == first.get(), "Skin local hit lost after foreign upload");
    // Owner isolation: a different thread must fail owner checks, never hit.
    bool ownerRejected = false;
    std::thread probe([&] {
        try {
            backend.uploadSkinMesh(origV, origI, 0x8201E6DC);
        } catch (const Error& e) {
            ownerRejected = std::string(e.what()).find("different thread") != std::string::npos;
        }
    });
    probe.join();
    require(ownerRejected, "Skin cross-thread upload bypassed owner checks");
}
void opaqueEyeRimControl(const std::vector<uint8_t>& image,bool hardware) {
    NativeBackend backend(!hardware);NativeMaterialCompiler compiler(backend);MaterialRegistry registry;
    const CompiledMaterial *vs=nullptr,*ps=nullptr;
    constexpr uint32_t vertexAddress=0x82007C1C,pixelAddress=0x8200A02C,extent=64;
    for(const auto& r:originalMaterialIdentities())if(r.originalAddress==vertexAddress||r.originalAddress==pixelAddress) {
        const auto id=registry.create(r.originalAddress,std::span<const uint8_t>(image).subspan(r.originalAddress-0x82000000,r.recordBytes));
        const auto& shader=registry.prepareForBind(id,compiler);
        if(r.stage==MaterialStage::Vertex)vs=&shader;else ps=&shader;
    }
    require(vs&&ps,"Missing original opaque skin shaders for eye rim control");
    auto color=backend.createTarget(extent,extent,TargetFormat::RGB10A2);auto depth=backend.createDepthTarget(extent,extent);
    SkinMeshDraw draw{};draw.primitiveType=6;draw.indexCount=3;draw.viewport={0,0,extent,extent,0x3F800000,0};
    draw.depthEnable=draw.depthWrite=1;draw.depthCompare=7;draw.colorMask=15;draw.halfPixelOffset=1;
    draw.primitiveReset=draw.viewportEnable=draw.multisampleAntialias=1;draw.primitiveResetIndex=0xFFFF;draw.multisampleMask=0xFFFFFFFF;
    draw.depthPolicy=ShadowMeshDepthPolicy::Reference20e4Rne;draw.blendWord=0x00010001;draw.expandedBlend=1;
    SkinVertexConstants vc{};SkinPixelConstants pc{};
    vc[0][0]=vc[1][1]=1;vc[2][2]=.5f;vc[3][2]=1;
    for(size_t i=0;i<4;++i)vc[12+i][i]=1;
    for(size_t i=0;i<3;++i)vc[55+i][i]=1;
    // Clip W is the vertex Z (3/5/7), with a constant .5 depth. The camera
    // faces these -Z normals, so the original opaque formula emits .125 rim
    // shadow whenever floor(color.x)==0. Zero fake-light is authored input.
    pc[4]={17,-9,200,1};pc[40]={0,.34f,-.2f,1};pc[47]={1,0,0,1};pc[49][2]=7;
    constexpr std::array<float,3> w{3,5,7};
    constexpr std::array<std::array<float,2>,3> ndc{{{-.8f,.8f},{-.8f,-.8f},{.8f,.8f}}};
    const std::array<uint16_t,3> indices{0,1,2};
    std::array<SkinVertex,3> vertices{};
    for(size_t i=0;i<vertices.size();++i) {
        auto& v=vertices[i];v.position={ndc[i][0]*w[i],ndc[i][1]*w[i],w[i]};
        v.normal={0,0,-1};v.indices={1,0,0,0};v.weights={1,0,0,0};
        // White-eye palette code32, deliberately away from a packed-UV floor.
        v.uv={.0111f,.5217f};v.color={1,0,0,1};
    }
    auto* context=NativeRecordingProbe::context(backend);
    backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.bindSkinShaders(*vs,*ps);
    auto commit=backend.commitSkin(*vs,*ps,vc,pc);
    auto recording=backend.createRecordingContext();auto live=backend.createSkinReplayConstants(vc,pc);NativeRecordingMask mask{};
    size_t checked=0;
    for(unsigned control=0;control<3;++control) {
        for(size_t i=0;i<vertices.size();++i)vertices[i].color[0]=control==0?1.0f:control==1?0.0f:float(i!=0);
        const auto mesh=backend.uploadSkinMesh(vertices,indices,vertexAddress);
        backend.bindSkinMeshVertices(mesh);backend.bindSkinMeshDeclaration(mesh);backend.bindSkinMeshIndices(mesh);
        auto payload=backend.allocateRecordingPayload(recording,0x3000);backend.beginRecordingPayload(payload,4,mask,mask);
        const EngineBindingResetProbe::Snapshot beforeRecord(context);
        backend.recordSkinMesh(payload,color,depth,mesh,*vs,*ps,vc,pc,live,draw);backend.finishRecordingPayload(payload);
        require(EngineBindingResetProbe::Snapshot(context)==beforeRecord,"Eye rim recording changed immediate bindings");
        std::vector<uint8_t> immediate,immediateDepth;
        for(bool deferred:{false,true}) {
            backend.clearTarget(color,{1,0,1,1});backend.clearDepthTarget(depth,0,0x67);
            const EngineBindingResetProbe::Snapshot before(context);
            if(deferred)backend.executeRecordingPayload(payload);else backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,draw);
            backend.waitIdle();
            require(EngineBindingResetProbe::Snapshot(context)==before,"Eye rim draw changed retained bindings");
            const auto pixels=backend.readbackTarget(color),depths=backend.readbackDepthTarget(depth);
            size_t covered=0,interior=0;
            for(unsigned y=0;y<extent;++y)for(unsigned x=0;x<extent;++x) {
                const size_t i=size_t(y)*extent+x;const auto packed=word(pixels,4*i);
                if(packed>>30)continue; // untouched magenta has alpha3; skin exports alpha0.
                ++covered;
                require((packed&1023)==7&&((packed>>10)&1023)==32,"Eye rim fixture changed material or palette code");
                require(word(depths,8*i)==std::bit_cast<uint32_t>(.5f)&&depths[8*i+4]==0x67,"Eye rim fixture changed mapped depth or stencil");
                // A mixed0/1 control stays strictly between0 and1 away from
                // the opposite edge, under either perspective convention.
                const float nx=2*(float(x)+.5f)/extent-1,ny=1-2*(float(y)+.5f)/extent;
                const float b=(.8f-ny)/1.6f,c=(nx+.8f)/1.6f,a=1-b-c;
                if(control==2&&std::min({a,b,c})<=.05f)continue;
                ++interior;
                const uint32_t expectedBlue=control==0?256u:384u;
                if(((packed>>20)&1023)!=expectedBlue) {
                    char why[220];std::snprintf(why,sizeof(why),"Opaque eye rim control%u %s pixel%u,%u: blue%u expected%u (vertex color.x is %s)",
                        control,deferred?"recorded":"direct",x,y,(packed>>20)&1023,expectedBlue,control==0?"uniform1":control==1?"uniform0":"mixed0/1");throw Error(why);
                }
                ++checked;
            }
            require(covered>500&&interior>400,"Eye rim fixture lacks oblique triangle/interior coverage");
            if(!deferred){immediate=pixels;immediateDepth=depths;}
            else require(pixels==immediate&&depths==immediateDepth,"Eye rim direct/recorded pixels differ");
        }
        backend.releaseRecordingPayload(payload);
    }
    backend.releaseSkinReplayConstants(live);backend.releaseRecordingContext(recording);
    std::printf("PASS opaque eye rim control %s: %zu pixels; varying clipW, uniform1 suppresses rim, uniform0/mixed0-1 retain rim, direct/recorded parity\n",
        hardware?"hardware":"WARP",checked);
}
// Dual-textured skin rim flag: PS82020B9C suppresses rim shadow where
// floor(color.x) is 1, like the base PS. A flag every vertex shares arrives
// exactly on Xenos; host perspective interpolation can land an ulp below 1,
// where floor gives 0 and rim shadow speckles. The draw wrapper keeps the flag
// noperspective. Over a steep perspective quad, on the recorded and immediate
// paths, uniform 1 and uniform 0 must each give one exact word in Original and
// Clean; a mixed flag keeps rim shadow inside its triangles, and Clean splits
// it at the affine .5 line.
void dualRimControl(const std::vector<uint8_t>& image,bool hardware) {
    NativeBackend backend(!hardware);NativeMaterialCompiler compiler(backend);MaterialRegistry registry;
    const CompiledMaterial *vs=nullptr,*ps=nullptr;
    constexpr uint32_t vertexAddress=0x8201E6DC,pixelAddress=0x82020B9C,width=256,height=144,id=714;
    for(const auto& r:originalMaterialIdentities())if(r.originalAddress==vertexAddress||r.originalAddress==pixelAddress) {
        const auto shaderId=registry.create(r.originalAddress,std::span<const uint8_t>(image).subspan(r.originalAddress-0x82000000,r.recordBytes));
        const auto& shader=registry.prepareForBind(shaderId,compiler);if(r.stage==MaterialStage::Vertex)vs=&shader;else ps=&shader;
    }
    require(vs&&ps,"Missing original dual skin shaders for rim control");
    auto color=backend.createTarget(width,height,TargetFormat::RGB10A2);auto depth=backend.createDepthTarget(width,height);
    auto shadow=backend.createDepthTarget(1024,1024);backend.clearDepthTarget(shadow,.25f,0x43);
    const std::array<uint8_t,4> texel{64,128,192,255};auto base=backend.createTexture(1,1,TextureFormat::RGBA8,texel);
    SkinMeshDraw d{};d.primitiveType=6;d.indexCount=4;d.viewport={0,0,width,height,0x3F800000,0};
    d.depthEnable=d.depthWrite=1;d.depthCompare=7;d.colorMask=15;d.halfPixelOffset=1;
    d.primitiveReset=d.viewportEnable=d.multisampleAntialias=1;d.primitiveResetIndex=0xFFFF;d.multisampleMask=0xFFFFFFFF;
    d.depthPolicy=ShadowMeshDepthPolicy::Reference20e4Rne;d.blendWord=0x00010001;d.expandedBlend=1;
    d.shadowSamplePolicy=RigidShadowSamplePolicy::ReferenceD24FS8DepthRRRR;d.characterShadow=shadow;d.baseTexture=base;
    for(size_t i=0;i<2;++i){auto& s=d.samplers[i];s.Filter=i?D3D11_FILTER_MIN_MAG_MIP_LINEAR:D3D11_FILTER_MIN_MAG_MIP_POINT;
        s.AddressU=s.AddressV=s.AddressW=i?D3D11_TEXTURE_ADDRESS_WRAP:D3D11_TEXTURE_ADDRESS_CLAMP;
        s.MaxAnisotropy=1;s.ComparisonFunc=D3D11_COMPARISON_NEVER;s.MaxLOD=i?13.0f:0.0f;}
    // clip=(x,y,z/2,z) through bone 1: position z carries W, so the screen
    // quad keeps its corners while W differs per vertex (1..9).
    SkinVertexConstants vc{};SkinPixelConstants pc{};
    vc[0][0]=vc[1][1]=1;vc[2][2]=.5f;vc[3][2]=1;
    for(size_t i=0;i<4;++i)vc[12+i][i]=1;
    for(size_t i=0;i<3;++i)vc[55+i][i]=1;
    pc[4]={0,0,3,1};pc[36]={0,0,1,0};pc[49]={0,0,float(id),1};pc[46][0]=pc[47][0]=pc[31][0]=1;pc[30][1]=.25f;
    constexpr std::array<std::array<float,3>,4> corners={{{-1,1,1},{-1,-1,4},{1,1,6},{1,-1,9}}};
    const std::array<uint16_t,4> indices{0,1,2,3};
    // Blue is .2*base.b+.25*(1-floor(color.x))+.5*(color.z>=.9) with color.z 1:
    // 666 where the flag suppresses rim shadow, 921 with rim shadow.
    constexpr uint32_t flagged=666,rimmed=921;
    auto* context=NativeRecordingProbe::context(backend);
    auto recording=backend.createRecordingContext();auto live=backend.createSkinReplayConstants(vc,pc);NativeRecordingMask mask{};
    size_t checked=0,lit=0,rim=0;
    for(unsigned control=0;control<3;++control) {
        std::array<SkinVertex,4> vertices{};
        for(size_t i=0;i<4;++i) {
            const float w=corners[i][2];auto& v=vertices[i];
            v.position={corners[i][0]*w,corners[i][1]*w,w};v.normal={0,0,-1};v.indices={1,0,0,0};v.weights={1,0,0,0};
            // Mixed flags the top-left and bottom-right corners: each strip
            // triangle has one flagged vertex, whose affine weight is |ny-nx|/2.
            const float flag=control==0?1.0f:control==1?0.0f:float(i==0||i==3);
            // An off-grid palette coordinate keeps red exact under any interpolation.
            v.color={flag,1,1,1};v.uv={.25f,.25f};v.uv1={.320299983f,.789099991f};
        }
        const auto mesh=backend.uploadSkinMesh(vertices,indices,vertexAddress);
        backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.bindSkinShaders(*vs,*ps);
        backend.bindSkinMeshVertices(mesh);backend.bindSkinMeshDeclaration(mesh);backend.bindSkinMeshIndices(mesh);
        backend.bindRigidShadowDepth(0,shadow);backend.bindEngineTexture(1,base);
        const auto commit=backend.commitSkin(*vs,*ps,vc,pc);
        // Recorded while Original; Clean reaches it through the live re-record.
        auto payload=backend.allocateRecordingPayload(recording,0x3000);backend.beginRecordingPayload(payload,4,mask,mask);
        backend.recordSkinMesh(payload,color,depth,mesh,*vs,*ps,vc,pc,live,d);backend.finishRecordingPayload(payload);
        for(bool clean:{false,true}) {
            require(backend.configureCleanEyes(clean)&&backend.cleanEyes()==clean,"Dual rim Clean eyes did not apply");
            std::vector<uint8_t> direct;
            for(bool deferred:{false,true}) {
                backend.clearTarget(color,{1,0,1,1});backend.clearDepthTarget(depth,0,0x67);
                const EngineBindingResetProbe::Snapshot before(context);
                if(deferred)backend.executeRecordingPayload(payload);
                else backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,d);
                backend.waitIdle();
                require(EngineBindingResetProbe::Snapshot(context)==before,"Dual rim draw changed retained bindings");
                const auto pixels=backend.readbackTarget(color);
                const uint32_t first=word(pixels,0),blueMask=1023u<<20;
                for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
                    const uint32_t packed=word(pixels,4*(size_t(y)*width+x)),blue=(packed>>20)&1023;
                    uint32_t expected=control==0?flagged:rimmed;
                    if(control==2) {
                        // Skip the triangle edges and Clean's .5 line.
                        const float nx=2*(float(x)+.5f)/width-1,ny=1-2*(float(y)+.5f)/height;
                        const float flag=std::abs(ny-nx)/2;
                        const float edge=std::min({flag,(1-nx)/2,(1+nx)/2,(1-ny)/2,(1+ny)/2});
                        if(edge<=.05f||(clean&&std::abs(flag-.5f)<=.05f))continue;
                        if(clean&&flag>.5f)expected=flagged;
                        if(clean&&!deferred)++(expected==flagged?lit:rim);
                    }
                    if(blue!=expected||(packed&~blueMask)!=(first&~blueMask)||(control!=2&&packed!=first)) {
                        char why[260];std::snprintf(why,sizeof(why),"Dual rim control%u %s %s pixel%u,%u: word%08X blue%u expected%u first%08X (vertex color.x is %s)",
                            control,clean?"Clean":"Original",deferred?"recorded":"direct",x,y,packed,blue,expected,first,
                            control==0?"uniform1":control==1?"uniform0":"mixed0/1");throw Error(why);
                    }
                    ++checked;
                }
                if(!deferred)direct=pixels;else require(pixels==direct,"Dual rim direct/recorded pixels differ");
            }
        }
        require(backend.configureCleanEyes(false)&&!backend.cleanEyes(),"Dual rim Clean eyes did not switch back");
        backend.releaseRecordingPayload(payload);
    }
    require(lit>4000&&rim>10000,"Dual rim fixture lacks both sides of the Clean flag midline");
    backend.releaseSkinReplayConstants(live);backend.releaseRecordingContext(recording);
    std::printf("PASS dual skin rim control %s: %zu pixels over a %ux%u quad with clip W 1..9; uniform1 suppresses rim, uniform0/mixed0-1 retain it, Clean splits mixed at .5 (%zu lit, %zu rim), direct/recorded parity\n",
        hardware?"hardware":"WARP",checked,width,height,lit,rim);
}
void run(const std::vector<uint8_t>& image,bool hardware,bool textured=false) {
    NativeBackend backend(!hardware);NativeMaterialCompiler compiler(backend);MaterialRegistry registry;
    const CompiledMaterial *vs=nullptr,*ps=nullptr,*oldVS=nullptr;
    const uint32_t vertexAddress=textured?0x8201146Cu:0x8201E6DCu,pixelAddress=textured?0x82013900u:0x82020B9Cu;
    for(const auto& r:originalMaterialIdentities())if(r.originalAddress==vertexAddress||r.originalAddress==pixelAddress||r.originalAddress==0x82007C1C) {
        const auto id=registry.create(r.originalAddress,std::span<const uint8_t>(image).subspan(r.originalAddress-0x82000000,r.recordBytes));
        const auto& shader=registry.prepareForBind(id,compiler);
        if(r.originalAddress==0x82007C1C)oldVS=&shader;else if(r.stage==MaterialStage::Vertex)vs=&shader;else ps=&shader;
    }
    require(vs&&ps&&oldVS,"Missing skin shader owners");
    rejects([&]{backend.validateSkinShaders(*oldVS,*ps);});rejects([&]{backend.validateSkinShaders(*ps,*vs);});
    constexpr uint32_t extent=16;
    auto color=backend.createTarget(extent,extent,TargetFormat::RGB10A2),sibling=backend.createTarget(extent,extent,TargetFormat::RGB10A2);
    auto depth=backend.createDepthTarget(extent,extent),shadow=backend.createDepthTarget(1024,1024);
    const std::array<uint8_t,4> texel{64,128,192,255};auto base=backend.createTexture(1,1,TextureFormat::RGBA8,texel);
    SkinMeshDraw draw{};draw.primitiveType=6;draw.indexCount=4;draw.viewport={0,0,extent,extent,0x3F800000,0};
    draw.depthEnable=draw.depthWrite=1;draw.depthCompare=7;draw.colorMask=15;draw.halfPixelOffset=1;
    draw.primitiveReset=draw.viewportEnable=draw.multisampleAntialias=1;draw.primitiveResetIndex=0xFFFF;draw.multisampleMask=0xFFFFFFFF;
    draw.depthPolicy=ShadowMeshDepthPolicy::Reference20e4Rne;draw.blendWord=0x00010001;draw.expandedBlend=1;
    draw.shadowSamplePolicy=RigidShadowSamplePolicy::ReferenceD24FS8DepthRRRR;draw.characterShadow=shadow;draw.baseTexture=base;
    for(size_t i=0;i<2;++i){auto& s=draw.samplers[i];s.Filter=i?D3D11_FILTER_MIN_MAG_MIP_LINEAR:D3D11_FILTER_MIN_MAG_MIP_POINT;
        s.AddressU=s.AddressV=s.AddressW=i?D3D11_TEXTURE_ADDRESS_WRAP:D3D11_TEXTURE_ADDRESS_CLAMP;
        s.MaxAnisotropy=1;s.ComparisonFunc=D3D11_COMPARISON_NEVER;s.MaxLOD=i?13.0f:0.0f;}
    std::array<SkinVertex,4> vertices{};
    vertices[0].position={-1,1,.25f};vertices[1].position={-1,-1,.25f};vertices[2].position={1,1,.25f};vertices[3].position={1,-1,.25f};
    for(auto& v:vertices){v.normal={0,0,1};v.indices={1,0,0,0};v.weights={1,0,0,0};v.color={0,.3f,1,1};v.uv={.25f,.25f};v.uv1={.140625f,.125f};}
    if(textured)for(auto& v:vertices){v.color={1,1,1,1};v.uv1={17,-23};}
    // Nonzero selected streams must survive the complete GPU upload/readback.
    // This fixture leaves the morph predicate disabled; the separate shader
    // oracle tests enabled morph arithmetic with all six independent inputs.
    for(auto& v:vertices){v.morph1={.125f,-.25f,.5f};v.morph2={-.5f,.75f,1};v.morph3={1.25f,-1.5f,.125f};
        v.morph4={-2,.5f,1};v.morph5={.125f,2,-.5f};v.morph6={-.25f,1,.75f};}
    const std::array<uint16_t,4> indices{0,1,2,3};auto mesh=backend.uploadSkinMesh(vertices,indices,vertexAddress);
    auto oldMesh=backend.uploadSkinMesh(vertices,indices);
    const auto readVertices=backend.readbackSkinMeshVertices(mesh);require(readVertices.size()==vertices.size()&&
        std::memcmp(readVertices.data(),vertices.data(),sizeof(vertices))==0,"Skin upload changed owned vertex data");
    SkinVertexConstants vc{};SkinPixelConstants pc{};
    for(size_t i=0;i<4;++i){vc[i][i]=1;vc[12+i][i]=1;}for(size_t i=0;i<3;++i)vc[55+i][i]=1;
    vc[28][3]=.5f;vc[29][3]=1;pc[4]={0,0,3,1};pc[36]={0,0,1,0};pc[49]={0,0,7,1};pc[46][0]=pc[47][0]=pc[31][0]=1;pc[30][1]=.25f;
    if(textured){vc[26][3]=vc[27][3]=.5f;pc[49]={-1,0,0,0};pc[40]={.1f,.2f,.3f,1};pc[46][0]=0;}
    auto* context=NativeRecordingProbe::context(backend);
    const auto bind=[&]{backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.bindSkinShaders(*vs,*ps);
        backend.bindSkinMeshVertices(mesh);backend.bindSkinMeshDeclaration(mesh);backend.bindSkinMeshIndices(mesh);
        backend.bindRigidShadowDepth(0,shadow);backend.bindEngineTexture(1,base);};
    backend.clearTarget(sibling,{0,1,1,1});backend.clearDepthTarget(shadow,.25f,0x43);bind();
    observeMeshGpuRetirement(context,mesh,textured?"skin_textured":"skin_dual");
    auto commit=backend.commitSkin(*vs,*ps,vc,pc);
    require(backend.readbackSkinVertexConstants(commit)==vc&&backend.readbackSkinPixelConstants(commit)==pc,"Skin immutable constants differ");
    // Explicit inherited sentinel samplers exercise restoration of both slots.
    auto sentinel=draw.samplers[0];sentinel.AddressU=D3D11_TEXTURE_ADDRESS_MIRROR;
    ComPtr<ID3D11Device> device;context->GetDevice(&device);ComPtr<ID3D11SamplerState> beforeSampler;
    require(SUCCEEDED(device->CreateSamplerState(&sentinel,&beforeSampler)),"Sampler sentinel creation failed");
    ID3D11SamplerState* sentinels[]{beforeSampler.Get(),beforeSampler.Get()};context->PSSetSamplers(0,2,sentinels);
    const auto siblingBefore=backend.readbackTarget(sibling);
    // The textured original instruction oracle yields RGB
    // (0.0000977517120,0.5083088875,0.6505882740), with alpha .7 for
    // character depth .25 and alpha1 for depth1. Channel-dependent raw D3D
    // R001 depth sampling would leave alpha .7 at depth1, so that replay
    // specifically qualifies the original RRRR adapter.
    const uint32_t r=textured?0u:uint32_t(std::lround(1023.0*272.0/2046.0));
    const uint32_t b=textured?666u:uint32_t(std::lround(1023.0*(.75+.2*192.0/255.0)));
    const auto verifyPixels=[&](uint32_t alpha,float expectedDepth) {
        const auto colors=backend.readbackTarget(color),depths=backend.readbackDepthTarget(depth);
        require(colors.size()==extent*extent*4&&depths.size()==extent*extent*8,"Skin target readback size differs");
        const uint32_t expected=r|(520u<<10)|(b<<20)|(alpha<<30);
        for(size_t i=0;i<extent*extent;++i) {
            if(word(colors,4*i)!=expected||word(depths,8*i)!=std::bit_cast<uint32_t>(expectedDepth)||depths[8*i+4]!=0x67) {
                char why[220];std::snprintf(why,sizeof(why),"Skin pixel%zu color=%08X expected=%08X depth=%08X expected=%08X stencil=%02X",i,
                    word(colors,4*i),expected,word(depths,8*i),std::bit_cast<uint32_t>(expectedDepth),depths[8*i+4]);throw Error(why);
            }
        }
        require(backend.readbackTarget(sibling)==siblingBefore,"Skin draw changed sibling target");return colors;
    };
    const auto clear=[&]{backend.clearTarget(color,{1,0,1,1});backend.clearDepthTarget(depth,.375f,0x67);};
    clear();const EngineBindingResetProbe::Snapshot before(context);const auto shadowBefore=backend.readbackDepthTarget(shadow);
    backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,draw);backend.waitIdle();
    require(EngineBindingResetProbe::Snapshot(context)==before,"Skin immediate draw changed retained state");
    const auto immediate=verifyPixels(2,.75f);require(backend.readbackDepthTarget(shadow)==shadowBefore,"Skin draw changed sampled depth");
    const auto count=backend.skinMeshDrawCount();
    auto invalid=draw;invalid.baseTexture.reset();rejects([&]{backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,invalid);});
    invalid=draw;invalid.characterShadow=depth;rejects([&]{backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,invalid);});
    invalid=draw;invalid.samplers[0].MaxLOD=13;rejects([&]{backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,invalid);});
    rejects([&]{backend.drawSkinMesh(color,depth,oldMesh,*vs,*ps,commit,draw);});
    require(backend.skinMeshDrawCount()==count&&backend.readbackTarget(color)==immediate,"Rejected skin draw changed pixels/accounting");
    auto recording=backend.createRecordingContext();NativeRecordingMask mask{};
    auto payload=backend.allocateRecordingPayload(recording,0x3000);backend.beginRecordingPayload(payload,4,mask,mask);
    auto live=backend.createSkinReplayConstants(vc,pc);const EngineBindingResetProbe::Snapshot recordingBefore(context);
    const auto rejectDraw=[&](const std::shared_ptr<NativeSkinMesh>& owner,const SkinMeshDraw& rejected) {
        const auto draws=backend.skinMeshDrawCount();const auto receipt=backend.recordingPayloadReceipt(payload);
        rejects([&]{backend.drawSkinMesh(color,depth,owner,*vs,*ps,commit,rejected);});
        rejects([&]{backend.recordSkinMesh(payload,color,depth,owner,*vs,*ps,vc,pc,live,rejected);});
        require(backend.skinMeshDrawCount()==draws&&backend.recordingPayloadReceipt(payload).recordedDraws==receipt.recordedDraws&&
                EngineBindingResetProbe::Snapshot(context)==recordingBefore&&backend.readbackTarget(color)==immediate,
                "Rejected skin range/blend changed draw receipts, pixels or bindings");
    };
    invalid=draw;invalid.baseVertex=-1;rejectDraw(mesh,invalid);
    invalid=draw;invalid.baseVertex=std::numeric_limits<int32_t>::max();rejectDraw(mesh,invalid);
    invalid=draw;invalid.startIndex=UINT32_MAX;rejectDraw(mesh,invalid);
    invalid=draw;invalid.primitiveResetIndex=0xFFFE;rejectDraw(mesh,invalid);
    const std::array<uint16_t,3> outside{0,1,4};auto outsideMesh=backend.uploadSkinMesh(vertices,outside,vertexAddress);
    invalid=draw;invalid.indexCount=3;rejectDraw(outsideMesh,invalid);
    invalid=draw;invalid.blendEnable=2;rejectDraw(mesh,invalid);
    invalid=draw;invalid.expandedBlend=2;rejectDraw(mesh,invalid);
    invalid=draw;invalid.blendEnable=1;rejectDraw(mesh,invalid);
    invalid=draw;invalid.blendWord=0x00010706;rejectDraw(mesh,invalid);
    invalid=draw;invalid.blendEnable=1;invalid.blendWord=0x00010706;rejectDraw(mesh,invalid);
    invalid=draw;invalid.blendEnable=1;invalid.blendWord=0x07060706;invalid.expandedBlend=2;rejectDraw(mesh,invalid);
    installD3DCallStats(NativeRecordingProbe::context(backend),NativeRecordingProbe::device(backend));
    NativeRecordingProbe::resetSceneCache(backend);
    auto buffersBefore=gD3DDeviceCounts[3];
    backend.recordSkinMesh(payload,color,depth,mesh,*vs,*ps,vc,pc,live,draw);
    require(gD3DDeviceCounts[3]==buffersBefore+3,"First recorded skin draw did not create two private banks and one immutable depth buffer");
    backend.finishRecordingPayload(payload);
    buffersBefore=gD3DDeviceCounts[3];auto originalDepthConstants=NativeRecordingProbe::depthConstants(backend,draw);
    auto* originalDepthIdentity=originalDepthConstants.Get();
    require(gD3DDeviceCounts[3]==buffersBefore,"Recorded skin depth buffer was absent from the shared cache");
    depthConstantBytes(backend,originalDepthConstants.Get(),draw);
    {
        auto repeated=backend.allocateRecordingPayload(recording,0x3000);backend.beginRecordingPayload(repeated,4,mask,mask);
        buffersBefore=gD3DDeviceCounts[3];backend.recordSkinMesh(repeated,color,depth,mesh,*vs,*ps,vc,pc,live,draw);
        require(gD3DDeviceCounts[3]==buffersBefore+2,"Repeated recorded skin draw recreated immutable depth or shared its writable banks");
        backend.finishRecordingPayload(repeated);backend.releaseRecordingPayload(repeated);
        std::array<SkinMeshDraw,3> changed{draw,draw,draw};
        changed[0].depthBiasBits=0x80000000;changed[1].slopeBiasBits=0x80000000;
        changed[2].viewport[4]=0;changed[2].viewport[5]=0x3F800000;
        for(const auto& different:changed) {
            auto separate=backend.allocateRecordingPayload(recording,0x3000);backend.beginRecordingPayload(separate,4,mask,mask);
            buffersBefore=gD3DDeviceCounts[3];backend.recordSkinMesh(separate,color,depth,mesh,*vs,*ps,vc,pc,live,different);
            require(gD3DDeviceCounts[3]==buffersBefore+3,"Distinct recorded skin depth bits incorrectly hit another key");
            backend.finishRecordingPayload(separate);
            buffersBefore=gD3DDeviceCounts[3];const auto constants=NativeRecordingProbe::depthConstants(backend,different);
            require(gD3DDeviceCounts[3]==buffersBefore&&constants.Get()!=originalDepthConstants.Get(),"Recorded skin signed zero or reverse key was lost");
            depthConstantBytes(backend,constants.Get(),different);backend.releaseRecordingPayload(separate);
        }
        NativeBackend foreign(!hardware);buffersBefore=gD3DDeviceCounts[3];
        const auto foreignConstants=NativeRecordingProbe::depthConstants(foreign,draw);
        require(gD3DDeviceCounts[3]==buffersBefore+1&&foreignConstants.Get()!=originalDepthConstants.Get(),"Skin depth constants crossed backend ownership");
        depthConstantBytes(foreign,foreignConstants.Get(),draw);
        auto* oldDevice=NativeRecordingProbe::device(foreign);NativeRecordingProbe::replaceDevice(foreign,hardware);
        require(oldDevice!=NativeRecordingProbe::device(foreign),"Skin depth constant device replacement did not occur");
        buffersBefore=gD3DDeviceCounts[3];const auto replaced=NativeRecordingProbe::depthConstants(foreign,draw);
        require(gD3DDeviceCounts[3]==buffersBefore+1&&replaced.Get()!=foreignConstants.Get(),"Skin depth constants crossed replacement devices");
        depthConstantBytes(foreign,replaced.Get(),draw);ComPtr<ID3D11Device> retained;foreignConstants->GetDevice(&retained);
        require(retained.Get()==oldDevice,"Skin device replacement mutated a retained immutable depth owner");
    }
    require(EngineBindingResetProbe::Snapshot(context)==recordingBefore&&backend.readbackTarget(color)==immediate,"Skin recording changed immediate state/output");
    clear();const EngineBindingResetProbe::Snapshot executionBefore(context);backend.executeRecordingPayload(payload);backend.waitIdle();
    require(EngineBindingResetProbe::Snapshot(context)==executionBefore,"Recorded skin execution changed immediate state");
    require(verifyPixels(2,.75f)==immediate,"Immediate and recorded skin pixels differ");
    {
        for(float bias:{1.f,2.f,3.f,4.f,5.f,6.f,7.f,8.f,9.f}) {
            auto different=draw;different.depthBiasBits=std::bit_cast<uint32_t>(bias);
            (void)NativeRecordingProbe::depthConstants(backend,different);
        }
        const auto afterEviction=NativeRecordingProbe::depthConstants(backend,draw);
        require(afterEviction.Get()!=originalDepthIdentity,"Skin depth cache eviction fixture did not retire the original entry");
        NativeRecordingProbe::resetSceneCache(backend);
        // Drop the observer reference too: only retained draw/list ownership
        // may keep this old immutable buffer alive after cache retirement.
        require(originalDepthConstants.Detach()->Release()>0,"Skin payload failed to retain its immutable depth buffer");
        clear();backend.executeRecordingPayload(payload);backend.waitIdle();
        require(EngineBindingResetProbe::Snapshot(context)==executionBefore&&verifyPixels(2,.75f)==immediate&&
            backend.readbackDepthTarget(shadow)==shadowBefore,"Skin recorded depth owner failed after cache eviction/reset");
        depthConstantBytes(backend,originalDepthIdentity,draw);
    }
    {
        const auto originalDepth=backend.readbackDepthTarget(depth);
        // Inherit projection and the shadow gate so this list exercises both
        // writable banks while its material rows stay independently captured.
        NativeRecordingMask queuedMask{};queuedMask[0]=0x80;queuedMask[8]=0x01;
        auto queuedLive=backend.createSkinReplayConstants(vc,pc);
        auto queuedPayload=backend.allocateRecordingPayload(recording,0x3000);
        backend.beginRecordingPayload(queuedPayload,4,queuedMask,queuedMask);
        backend.recordSkinMesh(queuedPayload,color,depth,mesh,*vs,*ps,vc,pc,queuedLive,draw);
        backend.finishRecordingPayload(queuedPayload);
        auto changedVS=vc;changedVS[0][0]=.5f;
        auto changedPS=pc;changedPS[31][0]=-1;
        const auto changedCommit=backend.commitSkin(*vs,*ps,changedVS,changedPS);
        clear();backend.drawSkinMesh(color,depth,mesh,*vs,*ps,changedCommit,draw);backend.waitIdle();
        const auto expectedColor=backend.readbackTarget(color),expectedDepth=backend.readbackDepthTarget(depth);
        require(expectedColor!=immediate,"Skin queued replay fixture did not change visible projection/shadow output");
        commit=backend.commitSkin(*vs,*ps,vc,pc);
        std::array<std::shared_ptr<RenderTarget>,2> queued;
        std::array<std::shared_ptr<DepthTarget>,2> queuedDepth;
        for(size_t i=0;i<queued.size();++i) {
            queued[i]=backend.createTarget(extent,extent,TargetFormat::RGB10A2);
            queuedDepth[i]=backend.createDepthTarget(extent,extent);
        }
        const EngineBindingResetProbe::Snapshot queueBefore(context);
        for(size_t i=0;i<queued.size();++i) {
            clear();backend.executeRecordingPayload(queuedPayload);
            (void)backend.copyFront(color,queued[i]);(void)backend.copyDepth(depth,queuedDepth[i]);
        }
        // The two earlier draws/copies are still queued when these constants
        // change. The driver must retain their old contents in command order.
        backend.updateSkinReplayConstants(queuedLive,changedVS,changedPS);
        clear();backend.executeRecordingPayload(queuedPayload);backend.waitIdle();
        require(EngineBindingResetProbe::Snapshot(context)==queueBefore&&backend.recordingPayloadReceipt(queuedPayload).executions==3,
                "Queued skin replays changed bindings or execution accounting");
        for(size_t i=0;i<queued.size();++i) {
            require(backend.readbackTarget(queued[i])==immediate,"Changed skin constants overwrote an earlier queued color result");
            const auto copied=backend.readbackDepthTarget(queuedDepth[i]);
            for(size_t p=0;p<extent*extent;++p)
                require(word(copied,8*p)==word(originalDepth,8*p)&&copied[8*p+4]==originalDepth[8*p+4],
                        "Changed skin constants overwrote an earlier queued depth/stencil result");
        }
        require(backend.readbackTarget(color)==expectedColor,"Queued skin changed constants differ from the immediate color oracle");
        const auto actualDepth=backend.readbackDepthTarget(depth);
        for(size_t p=0;p<extent*extent;++p)
            require(word(actualDepth,8*p)==word(expectedDepth,8*p)&&actualDepth[8*p+4]==expectedDepth[8*p+4],
                    "Queued skin changed constants differ from the immediate depth/stencil oracle");
        backend.releaseSkinReplayConstants(queuedLive);rejects([&]{backend.executeRecordingPayload(queuedPayload);});
        backend.releaseRecordingPayload(queuedPayload);
        clear();backend.executeRecordingPayload(payload);backend.waitIdle();verifyPixels(2,.75f);
    }
    for(int32_t baseOffset:{2,-2,0,65532}) {
        std::vector<SkinVertex> owned(vertices.begin(),vertices.end());
        std::vector<uint16_t> words;
        auto shifted=draw;shifted.baseVertex=baseOffset;shifted.startIndex=1;
        if(baseOffset>0){owned.insert(owned.begin(),size_t(baseOffset),vertices[0]);words={65534,0,1,2,65535,2,1,3,65534};shifted.indexCount=7;}
        else if(baseOffset<0){words={65534,2,3,4,65535,4,3,5,65534};shifted.indexCount=7;}
        else {words={65534,0,1,2,3,65534};shifted.indexCount=4;}
        const auto owner=backend.uploadSkinMesh(owned,words,vertexAddress);
        auto shiftedPayload=backend.allocateRecordingPayload(recording,0x3000);backend.beginRecordingPayload(shiftedPayload,4,mask,mask);
        backend.recordSkinMesh(shiftedPayload,color,depth,owner,*vs,*ps,vc,pc,live,shifted);backend.finishRecordingPayload(shiftedPayload);
        backend.bindSkinMeshVertices(owner);backend.bindSkinMeshDeclaration(owner);backend.bindSkinMeshIndices(owner);
        clear();const EngineBindingResetProbe::Snapshot beforeShifted(context);
        backend.drawSkinMesh(color,depth,owner,*vs,*ps,commit,shifted);backend.waitIdle();
        require(EngineBindingResetProbe::Snapshot(context)==beforeShifted&&verifyPixels(2,.75f)==immediate,
                "Original-style skin effective indices changed direct pixels or retained bindings");
        clear();backend.executeRecordingPayload(shiftedPayload);backend.waitIdle();
        require(EngineBindingResetProbe::Snapshot(context)==beforeShifted&&verifyPixels(2,.75f)==immediate,
                "Original-style skin effective indices changed recorded pixels or retained bindings");
        if(baseOffset==65532) {
            observeMeshGpuRetirement(context,owner,"skin_large_owner");
            auto invalidRange=shifted;invalidRange.baseVertex=65533;
            const auto draws=backend.skinMeshDrawCount();auto empty=backend.allocateRecordingPayload(recording,0x3000);
            backend.beginRecordingPayload(empty,4,mask,mask);
            rejects([&]{backend.drawSkinMesh(color,depth,owner,*vs,*ps,commit,invalidRange);});
            rejects([&]{backend.recordSkinMesh(empty,color,depth,owner,*vs,*ps,vc,pc,live,invalidRange);});
            require(!backend.recordingPayloadReceipt(empty).recordedDraws&&backend.skinMeshDrawCount()==draws&&
                EngineBindingResetProbe::Snapshot(context)==beforeShifted&&verifyPixels(2,.75f)==immediate,
                "Malformed large skin selected range changed receipts, pixels or bindings");
            backend.releaseRecordingPayload(empty);
        }
        backend.releaseRecordingPayload(shiftedPayload);rejects([&]{backend.executeRecordingPayload(shiftedPayload);});
        if(baseOffset==65532)std::printf("AUDIT_GPU_LARGE_OWNER family=skin vertices=65536 base_vertex=65532 create=passed direct_draw=passed recorded_draw=passed payload_release=passed stale_use=passed malformed=passed gpu_buffer_retirement=separate\n");
    }
    {
        const auto owned=Test::stripBoundaryVertices(vertices);const auto words=Test::stripBoundaryIndices();
        const auto owner=backend.uploadSkinMesh(owned,words,vertexAddress);
        backend.bindSkinMeshVertices(owner);backend.bindSkinMeshDeclaration(owner);backend.bindSkinMeshIndices(owner);
        observeMeshGpuRetirement(context,owner,textured?"skin_textured_strip_boundary":"skin_dual_strip_boundary");
        auto selected=draw;selected.startIndex=Test::stripBoundaryStart;selected.indexCount=Test::stripBoundaryCount;
        const auto direct=[&](const SkinMeshDraw& d) {
            const auto before=EngineBindingResetProbe::Snapshot(context);const auto countBefore=backend.skinMeshDrawCount();
            backend.drawSkinMesh(color,depth,owner,*vs,*ps,commit,d);backend.waitIdle();
            require(EngineBindingResetProbe::Snapshot(context)==before&&backend.skinMeshDrawCount()==countBefore+1,
                    "Skin split changed bindings or logical draw accounting");
        };
        for(uint32_t cull:{2u,6u}) {
            selected.cull=cull;clear();direct(selected);
            const auto actualColor=backend.readbackTarget(color),actualDepth=backend.readbackDepthTarget(depth);
            const uint32_t painted=r|(520u<<10)|(b<<20)|(2u<<30);
            for(UINT y=0;y<extent;++y)for(UINT x=0;x<extent;++x) {
                const auto i=y*extent+x;const bool covered=Test::originalStripBoundaryCovered(x,y,cull);
                require(word(actualColor,4*i)==(covered?painted:0xFFF003FF)&&
                    word(actualDepth,8*i)==std::bit_cast<uint32_t>(covered?.75f:.375f)&&actualDepth[8*i+4]==0x67,
                    "Skin split coverage differs from independent triangle winding");
            }
            clear();for(const auto& packet:Test::originalStripBoundaryPackets) {
                auto part=selected;part.indexCount=packet[0];part.startIndex=packet[1];direct(part);
            }
            require(backend.readbackTarget(color)==actualColor&&backend.readbackDepthTarget(depth)==actualDepth,
                    "Skin full draw differs from independent original packet pixels");
            auto boundary=backend.allocateRecordingPayload(recording,0x3000);backend.beginRecordingPayload(boundary,4,mask,mask);
            const auto retained=EngineBindingResetProbe::Snapshot(context);
            backend.recordSkinMesh(boundary,color,depth,owner,*vs,*ps,vc,pc,live,selected);backend.finishRecordingPayload(boundary);
            require(EngineBindingResetProbe::Snapshot(context)==retained&&backend.readbackTarget(color)==actualColor&&
                backend.readbackDepthTarget(depth)==actualDepth&&backend.recordingPayloadReceipt(boundary).recordedDraws==1,
                "Skin split recording emitted immediate work or changed logical receipt");
            clear();backend.executeRecordingPayload(boundary);backend.waitIdle();
            require(EngineBindingResetProbe::Snapshot(context)==retained&&backend.readbackTarget(color)==actualColor&&
                backend.readbackDepthTarget(depth)==actualDepth,"Skin split direct and deferred pixels differ");
            backend.releaseRecordingPayload(boundary);rejects([&]{backend.executeRecordingPayload(boundary);});
        }
        auto empty=backend.allocateRecordingPayload(recording,0x3000);backend.beginRecordingPayload(empty,4,mask,mask);
        for(const auto [start,count,baseOffset]:{std::array<int64_t,3>{1,65538,0},{UINT32_MAX,65536,0},{1,65536,-1},{1,65536,1}}) {
            auto bad=selected;bad.startIndex=uint32_t(start);bad.indexCount=uint32_t(count);bad.baseVertex=int32_t(baseOffset);
            const auto retained=EngineBindingResetProbe::Snapshot(context);const auto countBefore=backend.skinMeshDrawCount();
            const auto colorBefore=backend.readbackTarget(color),depthsBefore=backend.readbackDepthTarget(depth);
            rejects([&]{backend.drawSkinMesh(color,depth,owner,*vs,*ps,commit,bad);});
            rejects([&]{backend.recordSkinMesh(empty,color,depth,owner,*vs,*ps,vc,pc,live,bad);});
            require(EngineBindingResetProbe::Snapshot(context)==retained&&backend.skinMeshDrawCount()==countBefore&&
                backend.readbackTarget(color)==colorBefore&&backend.readbackDepthTarget(depth)==depthsBefore&&
                !backend.recordingPayloadReceipt(empty).recordedDraws,"Malformed skin split emitted a prefix or changed state");
        }
        backend.releaseRecordingPayload(empty);
        std::printf("AUDIT_GPU_STRIP_BOUNDARY family=skin vertex=%08X pixel=%08X count=65536 reset_position=65530 selected_start=1 sdk_packets=65534_4 sdk_advance=65532 cull=2_6 create=passed direct_pixels=passed original_packet_pixels=passed recorded_pixels=passed record_without_immediate_work=passed malformed=passed payload_release=passed stale_use=passed gpu_buffer_retirement=separate\n",vertexAddress,pixelAddress);
    }
    backend.bindSkinMeshVertices(mesh);backend.bindSkinMeshDeclaration(mesh);backend.bindSkinMeshIndices(mesh);
    backend.clearDepthTarget(shadow,1,0x43);backend.executeRecordingPayload(payload);backend.waitIdle();verifyPixels(3,.75f);
    // Sampled depth is replicated RRRR before all nine original channel selectors.
    backend.clearDepthTarget(shadow,.25f,0x43);draw.viewport[4]=0;draw.viewport[5]=0x3F800000;
    clear();backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,draw);backend.waitIdle();verifyPixels(2,.25f);
    // Source-specific material rows and authoritative reflection mappings.
    if(!textured) {
    std::unique_ptr<EffectRecord> effect,pool;
    for(const auto& id:originalEffectIdentities())if(id.originalAddress==0x8201CD48||id.originalAddress==0x820D5730) {
        auto value=std::make_unique<EffectRecord>(id.originalAddress,std::span<const uint8_t>(image).subspan(id.originalAddress-0x82000000,id.recordBytes));
        if(id.originalAddress==0x8201CD48)effect=std::move(value);else pool=std::move(value);
    }
    require(effect&&pool,"Missing skin/pool metadata");std::vector<std::string_view> names;for(const auto& p:pool->parameters(true))names.push_back(p.name);
    const EffectReflection reflected(*effect,false,names);
    for(const auto& p:reflected.parameters)if(p.name=="g_Sampler"||p.name=="kShadowCharDepthSampler")
        require(p.binding.usage==0x80&&p.binding.lanes[1]&&p.binding.lanes[1]->start==(p.name=="g_Sampler"?1u:0u)&&p.binding.lanes[1]->count==1,"Skin texture reflected stage differs");
    std::array<uint8_t,128> eligible{};eligible.fill(0xFF);
    for(const auto& p:reflected.passes)for(const auto& clear:p.clears)if(!clear.namespaceIndex)
        for(uint32_t i=0;i<clear.leafCount;++i){const auto leaf=clear.firstLeaf+i;eligible[leaf/8]&=uint8_t(~(0x80u>>(leaf&7)));}
    constexpr std::array<uint32_t,9> inherited{2,8,9,10,11,12,15,17,18};
    for(uint32_t leaf=0;leaf<90;++leaf)require(bool(eligible[leaf/8]&(0x80>>(leaf&7)))==
        (std::find(inherited.begin(),inherited.end(),leaf)==inherited.end()),"Dual skin inheritance classification changed");
    std::vector<uint32_t> words(1188);std::array<uint8_t,128> dirty{};SkinPixelConstants material{};
    for(const auto& row:Simpsons::skinPixelMaterialRows(0x8201CD48)) {
        require(eligible[row.leaf/8]&(0x80>>(row.leaf&7)),"Skin material row unexpectedly inherited");
        dirty[row.leaf/8]|=uint8_t(0x80>>(row.leaf&7));words[row.word]=std::bit_cast<uint32_t>(float(row.reg));
    }
    Simpsons::projectSkinMaterial(words,dirty,material,0x8201CD48);
    require(material[49][0]==49&&material[47][0]==47&&material[46][0]==46&&material[40][0]==0,"Dual skin material projection differs");
    }
    if(textured) {
        // The same opaque original pixel shader can inherit the alpha path's
        // pipeline equation. Its fractional .7 output alpha makes an ignored
        // equation observable against the magenta destination.
        auto retained=draw;retained.blendEnable=1;retained.blendWord=0x07060706;
        retained.viewport[4]=0x3F800000;retained.viewport[5]=0;
        constexpr std::array<double,3> originalRGB{.0000977517120,.5083088875,.6505882740};
        std::vector<uint8_t> precisionBaseline;
        for(uint32_t expanded:{0u,1u}) {
            retained.expandedBlend=expanded;
            auto inherited=backend.allocateRecordingPayload(recording,0x3000);backend.beginRecordingPayload(inherited,4,mask,mask);
            backend.recordSkinMesh(inherited,color,depth,mesh,*vs,*ps,vc,pc,live,retained);backend.finishRecordingPayload(inherited);
            std::vector<uint8_t> direct;
            for(bool deferred:{false,true}) {
                clear();const EngineBindingResetProbe::Snapshot retainedBefore(context);
                if(deferred)backend.executeRecordingPayload(inherited);else backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,retained);
                backend.waitIdle();const auto pixels=backend.readbackTarget(color);
                require(EngineBindingResetProbe::Snapshot(context)==retainedBefore,"Inherited opaque skin blend changed retained bindings");
                for(size_t i=0;i<extent*extent;++i) {
                    const auto packed=word(pixels,4*i);
                    for(size_t lane=0;lane<3;++lane) {
                        const auto expected=int(std::lround((originalRGB[lane]*.7+(lane==1?0:.3))*1023));
                        require(std::abs(int((packed>>(10*lane))&1023)-expected)<=1,"Inherited opaque skin RGB equation differs");
                    }
                    require((packed>>30)==2,"Inherited opaque skin alpha equation differs");
                }
                require(pixels!=immediate,"Opaque skin inherited alpha equation was ignored");
                if(!deferred)direct=pixels;else require(pixels==direct,"Inherited opaque skin direct/recorded blend differs");
            }
            if(!expanded)precisionBaseline=direct;else require(direct==precisionBaseline,"Bounded inherited skin precision variants differ");
            backend.releaseRecordingPayload(inherited);rejects([&]{backend.executeRecordingPayload(inherited);});
        }
    }
    backend.releaseSkinReplayConstants(live);rejects([&]{backend.executeRecordingPayload(payload);});
    backend.releaseRecordingPayload(payload);backend.releaseRecordingContext(recording);
    std::printf("PASS %s skin mesh %s: color/depth, RRRR samples, recorded parity, input/binding ownership and state restoration\n",textured?"textured":"dual",hardware?"hardware":"WARP");
}
void alphaBlend(const std::vector<uint8_t>& image,bool hardware,unsigned profile) {
    NativeBackend backend(!hardware);NativeMaterialCompiler compiler(backend);MaterialRegistry registry;
    const CompiledMaterial *vs=nullptr,*ps=nullptr;
    const uint32_t vertexAddress=profile==2?0x820126ECu:profile==1?0x8201F984u:0x82008E20u,
        pixelAddress=profile==2?0x82013F8Cu:profile==1?0x82021344u:0x8200A4A4u;
    for(const auto& r:originalMaterialIdentities())if(r.originalAddress==vertexAddress||r.originalAddress==pixelAddress) {
        const auto id=registry.create(r.originalAddress,std::span<const uint8_t>(image).subspan(r.originalAddress-0x82000000,r.recordBytes));
        const auto& shader=registry.prepareForBind(id,compiler);if(r.stage==MaterialStage::Vertex)vs=&shader;else ps=&shader;
    }
    require(vs&&ps,"Missing original skin-alpha shaders");
    auto color=backend.createTarget(16,16,TargetFormat::RGB10A2);auto depth=backend.createDepthTarget(16,16);
    const std::array<uint8_t,4> texel{64,128,192,255};auto base=backend.createTexture(1,1,TextureFormat::RGBA8,texel);
    std::array<SkinVertex,4> vertices{};
    vertices[0].position={-1,1,.25f};vertices[1].position={-1,-1,.25f};vertices[2].position={1,1,.25f};vertices[3].position={1,-1,.25f};
    for(auto& v:vertices){v.normal={0,0,1};v.indices={1,0,0,0};v.weights={1,0,0,0};v.color={1,1,1,.8f};v.uv={.5f,.5f};v.uv1={17,-23};}
    const std::array<uint16_t,4> indices{0,1,2,3};auto mesh=backend.uploadSkinMesh(vertices,indices,vertexAddress);
    SkinVertexConstants vc{};SkinPixelConstants pc{};
    for(size_t i=0;i<4;++i){vc[i][i]=1;vc[12+i][i]=1;}for(size_t i=0;i<3;++i)vc[55+i][i]=1;
    pc[4]={0,0,3,1};pc[49][0]=-1;pc[40][3]=.5f;
    SkinMeshDraw d{};d.primitiveType=6;d.indexCount=4;d.viewport={0,0,16,16,0x3F800000,0};
    d.depthEnable=d.depthWrite=1;d.depthCompare=7;d.colorMask=15;d.halfPixelOffset=1;
    d.primitiveReset=d.viewportEnable=d.multisampleAntialias=1;d.primitiveResetIndex=0xFFFF;d.multisampleMask=0xFFFFFFFF;
    d.depthPolicy=ShadowMeshDepthPolicy::Reference20e4Rne;d.blendEnable=1;d.blendWord=0x07060706;d.expandedBlend=1;d.baseTexture=base;
    auto& sampler=d.samplers[0];sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxLOD=13;
    backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.bindSkinShaders(*vs,*ps);
    backend.bindSkinMeshVertices(mesh);backend.bindSkinMeshDeclaration(mesh);backend.bindSkinMeshIndices(mesh);backend.bindEngineTexture(0,base);
    auto commit=backend.commitSkin(*vs,*ps,vc,pc);
    // All original alpha programs consume only base stage0, even when their
    // opaque source family uses character depth and a second UV.
    const auto beforeRejected=backend.skinMeshDrawCount();
    auto invalid=d;invalid.baseTexture.reset();rejects([&]{backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,invalid);});
    invalid=d;invalid.characterShadow=depth;rejects([&]{backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,invalid);});
    invalid=d;invalid.samplers[0].MaxLOD=0;rejects([&]{backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,invalid);});
    auto wrongProfile=backend.uploadSkinMesh(vertices,indices,profile==2?0x8201E6DCu:0x8201146Cu);
    rejects([&]{backend.drawSkinMesh(color,depth,wrongProfile,*vs,*ps,commit,d);});
    require(backend.skinMeshDrawCount()==beforeRejected,"Rejected alpha resource combination emitted a draw");
    auto recording=backend.createRecordingContext();auto payload=backend.allocateRecordingPayload(recording,0x3000);
    NativeRecordingMask mask{};backend.beginRecordingPayload(payload,4,mask,mask);auto live=backend.createSkinReplayConstants(vc,pc);
    backend.recordSkinMesh(payload,color,depth,mesh,*vs,*ps,vc,pc,live,d);backend.finishRecordingPayload(payload);
    std::vector<uint8_t> direct;
    for(unsigned path=0;path<2;++path) {
        backend.clearTarget(color,{0,1,0,1});backend.clearDepthTarget(depth,0,0x67);
        const EngineBindingResetProbe::Snapshot before(NativeRecordingProbe::context(backend));
        if(path)backend.executeRecordingPayload(payload);else backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,d);
        require(EngineBindingResetProbe::Snapshot(NativeRecordingProbe::context(backend))==before,"Skin-alpha blend changed retained state");
        auto pixels=backend.readbackTarget(color);auto depths=backend.readbackDepthTarget(depth);
        for(size_t i=0;i<256;++i) {
            auto packed=word(pixels,4*i);
            for(size_t lane=0;lane<3;++lane) {
                const auto expected=int(std::lround((.4*texel[lane]/255.0+(lane==1?.6:0))*1023));
                require(std::abs(int((packed>>(10*lane))&1023)-expected)<=1,"Skin-alpha source/destination blend differs");
            }
            require(packed>>30==2,"Skin-alpha alpha-channel blend differs");
            require(word(depths,8*i)==std::bit_cast<uint32_t>(.75f)&&depths[8*i+4]==0x67,"Skin-alpha depth/stencil differs");
        }
        if(!path)direct=pixels;else require(pixels==direct,"Skin-alpha direct/recorded blend differs");
    }
    backend.releaseRecordingPayload(payload);backend.releaseRecordingContext(recording);backend.releaseSkinReplayConstants(live);
    std::printf("PASS %s skin-alpha blend %s: fractional opacity, RGB/alpha equation, depth/stencil, base0-only resources, direct/recorded parity and retained state\n",profile==2?"textured":profile==1?"dual":"base",hardware?"hardware":"WARP");
}
// Video menu "Character eyes: Clean": the eye-rim fixture's mixed 0/1 flag
// triangle loses its rim wherever the affine flag reaches .5; uniform flags
// keep their original result. The toggle is live and re-records sealed draws.
void cleanEyesControl(const std::vector<uint8_t>& image,bool hardware) {
    NativeBackend backend(!hardware);NativeMaterialCompiler compiler(backend);MaterialRegistry registry;
    const CompiledMaterial *vs=nullptr,*ps=nullptr;
    constexpr uint32_t vertexAddress=0x82007C1C,pixelAddress=0x8200A02C,extent=64;
    for(const auto& r:originalMaterialIdentities())if(r.originalAddress==vertexAddress||r.originalAddress==pixelAddress) {
        const auto id=registry.create(r.originalAddress,std::span<const uint8_t>(image).subspan(r.originalAddress-0x82000000,r.recordBytes));
        const auto& shader=registry.prepareForBind(id,compiler);
        if(r.stage==MaterialStage::Vertex)vs=&shader;else ps=&shader;
    }
    require(vs&&ps,"Missing original opaque skin shaders for clean eyes");
    auto color=backend.createTarget(extent,extent,TargetFormat::RGB10A2);auto depth=backend.createDepthTarget(extent,extent);
    SkinMeshDraw draw{};draw.primitiveType=6;draw.indexCount=3;draw.viewport={0,0,extent,extent,0x3F800000,0};
    draw.depthEnable=draw.depthWrite=1;draw.depthCompare=7;draw.colorMask=15;draw.halfPixelOffset=1;
    draw.primitiveReset=draw.viewportEnable=draw.multisampleAntialias=1;draw.primitiveResetIndex=0xFFFF;draw.multisampleMask=0xFFFFFFFF;
    draw.depthPolicy=ShadowMeshDepthPolicy::Reference20e4Rne;draw.blendWord=0x00010001;draw.expandedBlend=1;
    SkinVertexConstants vc{};SkinPixelConstants pc{};
    vc[0][0]=vc[1][1]=1;vc[2][2]=.5f;vc[3][2]=1;
    for(size_t i=0;i<4;++i)vc[12+i][i]=1;
    for(size_t i=0;i<3;++i)vc[55+i][i]=1;
    pc[4]={17,-9,200,1};pc[40]={0,.34f,-.2f,1};pc[47]={1,0,0,1};pc[49][2]=7;
    constexpr std::array<float,3> w{3,5,7};
    constexpr std::array<std::array<float,2>,3> ndc{{{-.8f,.8f},{-.8f,-.8f},{.8f,.8f}}};
    const std::array<uint16_t,3> indices{0,1,2};
    std::array<SkinVertex,3> vertices{};
    for(size_t i=0;i<vertices.size();++i) {
        auto& v=vertices[i];v.position={ndc[i][0]*w[i],ndc[i][1]*w[i],w[i]};
        v.normal={0,0,-1};v.indices={1,0,0,0};v.weights={1,0,0,0};
        v.uv={.0111f,.5217f};v.color={float(i!=0),0,0,1};
    }
    auto* context=NativeRecordingProbe::context(backend);
    backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.bindSkinShaders(*vs,*ps);
    auto commit=backend.commitSkin(*vs,*ps,vc,pc);
    auto recording=backend.createRecordingContext();auto live=backend.createSkinReplayConstants(vc,pc);NativeRecordingMask mask{};
    const auto mesh=backend.uploadSkinMesh(vertices,indices,vertexAddress);
    backend.bindSkinMeshVertices(mesh);backend.bindSkinMeshDeclaration(mesh);backend.bindSkinMeshIndices(mesh);
    // Recorded while Original; Clean must reach it through the live re-record.
    auto payload=backend.allocateRecordingPayload(recording,0x3000);backend.beginRecordingPayload(payload,4,mask,mask);
    backend.recordSkinMesh(payload,color,depth,mesh,*vs,*ps,vc,pc,live,draw);backend.finishRecordingPayload(payload);
    size_t lit=0,rim=0;
    for(bool clean:{false,true}) {
        require(backend.configureCleanEyes(clean)&&backend.cleanEyes()==clean,"Clean eyes did not apply");
        std::vector<uint8_t> direct;
        for(bool deferred:{false,true}) {
            backend.clearTarget(color,{1,0,1,1});backend.clearDepthTarget(depth,0,0x67);
            const EngineBindingResetProbe::Snapshot before(context);
            if(deferred)backend.executeRecordingPayload(payload);else backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,draw);
            backend.waitIdle();
            require(EngineBindingResetProbe::Snapshot(context)==before,"Clean eyes draw changed retained bindings");
            const auto pixels=backend.readbackTarget(color);
            for(unsigned y=0;y<extent;++y)for(unsigned x=0;x<extent;++x) {
                const auto packed=word(pixels,4*(size_t(y)*extent+x));
                if(packed>>30)continue;
                const float nx=2*(float(x)+.5f)/extent-1,ny=1-2*(float(y)+.5f)/extent;
                const float b=(.8f-ny)/1.6f,c=(nx+.8f)/1.6f,a=1-b-c;
                // Flag = 1-a (vertex0 is the unflagged corner).
                if(std::min({a,b,c})<=.05f||std::abs(a-.5f)<=.05f)continue;
                const uint32_t blue=(packed>>20)&1023;
                const uint32_t expected=clean&&a<.5f?256u:384u;
                if(blue!=expected) {
                    char why[200];std::snprintf(why,sizeof(why),"Clean eyes %s %s pixel%u,%u: blue%u expected%u",clean?"on":"off",
                        deferred?"recorded":"direct",x,y,blue,expected);throw Error(why);
                }
                if(clean&&!deferred)++(expected==256u?lit:rim);
            }
            if(!deferred)direct=pixels;else require(pixels==direct,"Clean eyes direct/recorded pixels differ");
        }
    }
    require(lit>400&&rim>100,"Clean eyes fixture lacks both sides of the flag midline");
    require(backend.configureCleanEyes(false)&&!backend.cleanEyes(),"Clean eyes did not switch back");
    backend.releaseRecordingPayload(payload);backend.releaseSkinReplayConstants(live);backend.releaseRecordingContext(recording);
    std::printf("PASS clean eyes %s: mixed-flag rim splits at .5 (%zu lit, %zu rim), Original unchanged, live re-record parity\n",
        hardware?"hardware":"WARP",lit,rim);
}
// Character detail draws the same range as a smoothed list. A flat quad with
// perpendicular normals stays flat, so every level must match the original
// pixels exactly, directly and recorded, and restore the original streams.
void characterDetailParity(const std::vector<uint8_t>& image,bool hardware) {
    std::vector<uint8_t> reference,referenceDepth;
    for(uint32_t level:{0u,1u,3u}) {
        NativeBackend backend(!hardware);backend.configureCharacterDetail(level);
        NativeMaterialCompiler compiler(backend);MaterialRegistry registry;
        const CompiledMaterial *vs=nullptr,*ps=nullptr;
        for(const auto& r:originalMaterialIdentities())if(r.originalAddress==0x82008E20u||r.originalAddress==0x8200A4A4u) {
            const auto id=registry.create(r.originalAddress,std::span<const uint8_t>(image).subspan(r.originalAddress-0x82000000,r.recordBytes));
            const auto& shader=registry.prepareForBind(id,compiler);if(r.stage==MaterialStage::Vertex)vs=&shader;else ps=&shader;
        }
        require(vs&&ps,"Missing original skin-alpha shaders for character detail");
        auto color=backend.createTarget(16,16,TargetFormat::RGB10A2);auto depth=backend.createDepthTarget(16,16);
        const std::array<uint8_t,4> texel{64,128,192,255};auto base=backend.createTexture(1,1,TextureFormat::RGBA8,texel);
        std::array<SkinVertex,4> vertices{};
        vertices[0].position={-1,1,.25f};vertices[1].position={-1,-1,.25f};vertices[2].position={1,1,.25f};vertices[3].position={1,-1,.25f};
        for(auto& v:vertices){v.normal={0,0,1};v.indices={1,0,0,0};v.weights={1,0,0,0};v.color={1,1,1,.8f};v.uv={.5f,.5f};v.uv1={17,-23};}
        const std::array<uint16_t,4> indices{0,1,2,3};auto mesh=backend.uploadSkinMesh(vertices,indices,0x82008E20u);
        SkinVertexConstants vc{};SkinPixelConstants pc{};
        for(size_t i=0;i<4;++i){vc[i][i]=1;vc[12+i][i]=1;}for(size_t i=0;i<3;++i)vc[55+i][i]=1;
        pc[4]={0,0,3,1};pc[49][0]=-1;pc[40][3]=.5f;
        SkinMeshDraw d{};d.primitiveType=6;d.indexCount=4;d.viewport={0,0,16,16,0x3F800000,0};
        d.depthEnable=d.depthWrite=1;d.depthCompare=7;d.colorMask=15;d.halfPixelOffset=1;
        d.primitiveReset=d.viewportEnable=d.multisampleAntialias=1;d.primitiveResetIndex=0xFFFF;d.multisampleMask=0xFFFFFFFF;
        d.depthPolicy=ShadowMeshDepthPolicy::Reference20e4Rne;d.blendEnable=1;d.blendWord=0x07060706;d.expandedBlend=1;d.baseTexture=base;
        auto& sampler=d.samplers[0];sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxLOD=13;
        backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.bindSkinShaders(*vs,*ps);
        backend.bindSkinMeshVertices(mesh);backend.bindSkinMeshDeclaration(mesh);backend.bindSkinMeshIndices(mesh);backend.bindEngineTexture(0,base);
        auto commit=backend.commitSkin(*vs,*ps,vc,pc);
        auto recording=backend.createRecordingContext();auto payload=backend.allocateRecordingPayload(recording,0x3000);
        NativeRecordingMask mask{};backend.beginRecordingPayload(payload,4,mask,mask);auto live=backend.createSkinReplayConstants(vc,pc);
        backend.recordSkinMesh(payload,color,depth,mesh,*vs,*ps,vc,pc,live,d);backend.finishRecordingPayload(payload);
        for(unsigned path=0;path<2;++path) {
            backend.clearTarget(color,{0,1,0,1});backend.clearDepthTarget(depth,0,0x67);
            const EngineBindingResetProbe::Snapshot before(NativeRecordingProbe::context(backend));
            const auto detailed=backend.skinDetailDrawCount();
            if(path)backend.executeRecordingPayload(payload);else backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,d);
            require(EngineBindingResetProbe::Snapshot(NativeRecordingProbe::context(backend))==before,"Character detail changed retained state");
            require(backend.skinDetailDrawCount()==detailed+(path==0&&level?1:0),"Character detail draw accounting differs");
            const auto pixels=backend.readbackTarget(color),depths=backend.readbackDepthTarget(depth);
            if(reference.empty()){reference=pixels;referenceDepth=depths;}
            else require(pixels==reference&&depths==referenceDepth,"Character detail changed a flat surface's pixels or depth");
        }
        backend.releaseRecordingPayload(payload);backend.releaseRecordingContext(recording);backend.releaseSkinReplayConstants(live);
    }
    std::printf("PASS character detail parity %s: Off/Low/High flat quad direct and recorded pixels, depth and retained streams\n",hardware?"hardware":"WARP");
}
// Palette coordinates, skin side of RigidMeshTests paletteInterpolation. The
// opaque skin PS packs uv0 as 32*floor(32*frac(2v))+floor(64*frac(u)) into
// green; dual-textured, gloss, flipbook and dual-textured UV (opaque and
// alpha) pack uv1 plus the object id into red. A palette value every vertex
// shares arrives exactly on Xenos; host perspective interpolation can land an
// ulp below it and change the code on alternate rows. Every pixel of a steep
// perspective quad must keep one exact word on the recorded and immediate draw.
void paletteInterpolation(const std::vector<uint8_t>& image,bool hardware) {
    struct Family {const char* name;uint32_t vertex,pixel;bool red,shadowed,base,second;};
    const std::array<Family,6> families={{
        {"skin",0x82007C1C,0x8200A02C,false,false,false,false},
        {"dualtextured",0x8201E6DC,0x82020B9C,true,true,true,false},
        {"gloss",0x8202579C,0x82027C18,true,false,true,false},
        {"flipbook",0x8203D93C,0x82040118,true,false,true,false},
        {"dualtextured-uv",0x8204BA4C,0x8204E2DC,true,false,true,true},
        {"dualtextured-uv-alpha",0x8204CE90,0x8204E75C,true,false,true,true}}};
    constexpr uint32_t width=Test::paletteWidth,height=Test::paletteHeight,id=Test::paletteId;
    const auto& cases=Test::paletteCases;const auto& corners=Test::paletteCorners;
    size_t draws=0;
    for(const auto& family:families) {
        NativeBackend backend(!hardware);NativeMaterialCompiler compiler(backend);MaterialRegistry registry;
        const CompiledMaterial *vs=nullptr,*ps=nullptr;
        for(const auto& r:originalMaterialIdentities())if(r.originalAddress==family.vertex||r.originalAddress==family.pixel) {
            const auto shaderId=registry.create(r.originalAddress,std::span<const uint8_t>(image).subspan(r.originalAddress-0x82000000,r.recordBytes));
            const auto& shader=registry.prepareForBind(shaderId,compiler);if(r.stage==MaterialStage::Vertex)vs=&shader;else ps=&shader;
        }
        require(vs&&ps,"Missing original skin palette shaders");
        auto color=backend.createTarget(width,height,TargetFormat::RGB10A2);auto depth=backend.createDepthTarget(width,height);
        auto shadow=backend.createDepthTarget(1024,1024);backend.clearDepthTarget(shadow,.25f,0x43);
        SkinMeshDraw d{};d.primitiveType=6;d.indexCount=4;d.viewport={0,0,width,height,0x3F800000,0};
        d.depthEnable=d.depthWrite=1;d.depthCompare=7;d.colorMask=15;d.halfPixelOffset=1;
        d.primitiveReset=d.viewportEnable=d.multisampleAntialias=1;d.primitiveResetIndex=0xFFFF;d.multisampleMask=0xFFFFFFFF;
        d.depthPolicy=ShadowMeshDepthPolicy::Reference20e4Rne;d.blendWord=0x00010001;d.expandedBlend=1;
        D3D11_SAMPLER_DESC point{},wrap{};
        point.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;point.AddressU=point.AddressV=point.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        point.MaxAnisotropy=1;point.ComparisonFunc=D3D11_COMPARISON_NEVER;
        wrap.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;wrap.AddressU=wrap.AddressV=wrap.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        wrap.MaxAnisotropy=1;wrap.ComparisonFunc=D3D11_COMPARISON_NEVER;wrap.MaxLOD=13;
        const std::array<uint8_t,4> baseTexel{64,128,192,255},secondTexel{32,160,224,192};
        if(family.base)d.baseTexture=backend.createTexture(1,1,TextureFormat::RGBA8,baseTexel);
        if(family.second)d.secondTexture=backend.createTexture(1,1,TextureFormat::RGBA8,secondTexel);
        if(family.shadowed) {
            // Character depth t0 (point/clamp), base t1.
            d.shadowSamplePolicy=RigidShadowSamplePolicy::ReferenceD24FS8DepthRRRR;d.characterShadow=shadow;d.samplers={point,wrap};
        } else d.samplers={wrap,wrap};
        // clip=(x,y,z/2,z) through bone 1: position z carries w, so the screen
        // quad keeps its corners while w differs per vertex (1..9).
        SkinVertexConstants vc{};SkinPixelConstants pc{};
        vc[0][0]=vc[1][1]=1;vc[2][2]=.5f;vc[3][2]=1;
        for(size_t i=0;i<4;++i)vc[12+i][i]=1;
        for(size_t i=0;i<3;++i)vc[55+i][i]=1;
        // c49.z is the object id.
        pc[4]={0,0,3,1};pc[36]={0,0,1,0};pc[49]={0,0,float(id),1};pc[46][0]=pc[47][0]=pc[31][0]=1;pc[30][1]=.25f;
        const auto commit=[&]{return backend.commitSkin(*vs,*ps,vc,pc);};
        auto recording=backend.createRecordingContext();auto live=backend.createSkinReplayConstants(vc,pc);NativeRecordingMask mask{};
        for(const auto& coordinate:cases) {
            std::array<SkinVertex,4> vertices{};
            for(size_t i=0;i<4;++i) {
                const float w=corners[i][2];auto& v=vertices[i];
                v.position={corners[i][0]*w,corners[i][1]*w,w};v.normal={0,0,-1};v.indices={1,0,0,0};v.weights={1,0,0,0};
                // Colour clear of the floor(color.x) rim flag and other
                // thresholds; opaqueEyeRimControl and dualRimControl cover those.
                v.color={.5f,.5f,.5f,.6f};v.uv={.25f,.25f};v.uv1={.25f,.25f};
                (family.red?v.uv1:v.uv)=coordinate;
            }
            const std::array<uint16_t,4> indices{0,1,2,3};
            const auto mesh=backend.uploadSkinMesh(vertices,indices,family.vertex);
            const uint32_t palette=Test::paletteCode(coordinate[0],coordinate[1]);
            const uint32_t expected=Test::paletteChannel(family.red,palette);
            require(Test::paletteChannelExact(family.red,palette),"Skin palette fixture code must be even");
            const auto check=[&](const char* path) {
                const auto colors=backend.readbackTarget(color);
                const uint32_t first=word(colors,0);
                for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
                    const uint32_t value=word(colors,4*(size_t(y)*width+x));
                    const uint32_t channel=family.red?value&1023:(value>>10)&1023;
                    if(channel!=expected||value!=first) {
                        char text[240];std::snprintf(text,sizeof(text),"Skin %s %s palette (%.9g,%.9g) pixel(%u,%u) channel=%u expected=%u word=%08X first=%08X",
                            family.name,path,coordinate[0],coordinate[1],x,y,channel,expected,value,first);throw Error(text);
                    }
                }
            };
            auto payload=backend.allocateRecordingPayload(recording,0x3000);backend.beginRecordingPayload(payload,4,mask,mask);
            backend.recordSkinMesh(payload,color,depth,mesh,*vs,*ps,vc,pc,live,d);backend.finishRecordingPayload(payload);
            backend.clearTarget(color,{1,0,1,1});backend.clearDepthTarget(depth,0,0x67);
            backend.executeRecordingPayload(payload);backend.waitIdle();check("recorded");
            backend.releaseRecordingPayload(payload);
            backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.bindSkinShaders(*vs,*ps);
            backend.bindSkinMeshVertices(mesh);backend.bindSkinMeshDeclaration(mesh);backend.bindSkinMeshIndices(mesh);
            if(family.shadowed){backend.bindRigidShadowDepth(0,shadow);backend.bindEngineTexture(1,d.baseTexture);}
            else if(family.base) {
                backend.bindEngineTexture(0,d.baseTexture);
                if(family.second)backend.bindEngineTexture(1,d.secondTexture);
            }
            backend.clearTarget(color,{1,0,1,1});backend.clearDepthTarget(depth,0,0x67);
            backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit(),d);backend.waitIdle();check("immediate");
            draws+=2;
        }
        backend.releaseSkinReplayConstants(live);backend.releaseRecordingContext(recording);
    }
    std::printf("PASS skin palette interpolation %s: skin/dual-textured/gloss/flipbook/dual-textured UV opaque and alpha keep one exact code for %zu grid-aligned triangle-constant palette coordinates over a %ux%u perspective quad (%zu recorded and immediate draws)\n",
        hardware?"hardware":"WARP",cases.size(),width,height,draws);
}
int main(int argc,char** argv)try {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    require(argc==2||(argc==3&&std::string(argv[2])=="--hardware"),"Supply original image and optional --hardware");
    std::ifstream input(argv[1],std::ios::binary);std::vector<uint8_t> image((std::istreambuf_iterator<char>(input)),{});
    require(image.size()==15466496,"Original image size differs");opaqueEyeRimControl(image,argc==3);dualRimControl(image,argc==3);run(image,argc==3);run(image,argc==3,true);for(unsigned profile=0;profile<3;++profile)alphaBlend(image,argc==3,profile);uploadCacheReuse(argc==3);characterDetailParity(image,argc==3);cleanEyesControl(image,argc==3);paletteInterpolation(image,argc==3);requireMeshGpuRetirements([](bool value,const char* message){require(value,message);});return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL skin mesh: %s\n",e.what());return 1;}
