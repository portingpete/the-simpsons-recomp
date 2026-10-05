#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "renderer/skin_mesh.h"
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
void require(bool b,const char* why){if(!b)throw Error(why);}
template<class F>void rejects(F action){bool failed=false;try{action();}catch(const Error&){failed=true;}require(failed,"Invalid skin operation succeeded");}
#include "header/test_engine_binding_reset.h"
#include "header/test_mesh_gpu_retirement.h"
#include "header/test_r16_strip_boundary.h"
namespace Simpsons::Graphics {
struct NativeRecordingProbe {static ID3D11DeviceContext* context(NativeBackend& b){return b.context.Get();}};
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
    backend.recordSkinMesh(payload,color,depth,mesh,*vs,*ps,vc,pc,live,draw);backend.finishRecordingPayload(payload);
    require(EngineBindingResetProbe::Snapshot(context)==recordingBefore&&backend.readbackTarget(color)==immediate,"Skin recording changed immediate state/output");
    clear();const EngineBindingResetProbe::Snapshot executionBefore(context);backend.executeRecordingPayload(payload);backend.waitIdle();
    require(EngineBindingResetProbe::Snapshot(context)==executionBefore,"Recorded skin execution changed immediate state");
    require(verifyPixels(2,.75f)==immediate,"Immediate and recorded skin pixels differ");
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
int main(int argc,char** argv)try {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    require(argc==2||(argc==3&&std::string(argv[2])=="--hardware"),"Supply original image and optional --hardware");
    std::ifstream input(argv[1],std::ios::binary);std::vector<uint8_t> image((std::istreambuf_iterator<char>(input)),{});
    require(image.size()==15466496,"Original image size differs");opaqueEyeRimControl(image,argc==3);run(image,argc==3);run(image,argc==3,true);for(unsigned profile=0;profile<3;++profile)alphaBlend(image,argc==3,profile);uploadCacheReuse(argc==3);requireMeshGpuRetirements([](bool value,const char* message){require(value,message);});return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL skin mesh: %s\n",e.what());return 1;}
