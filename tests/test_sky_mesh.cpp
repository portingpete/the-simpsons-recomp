#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "renderer/sky_mesh.h"
#include "renderer/native_material_compiler.h"
#include "renderer/effect_reflection.h"
#include "renderer/post_filter.h"
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
void require(bool value,const char* message){if(!value)throw Error(message);}
template<class F>void rejects(F action){bool rejected=false;try{action();}catch(const Error&){rejected=true;}require(rejected,"Invalid sky operation succeeded");}
#include "header/test_engine_binding_reset.h"
#include "header/test_mesh_gpu_retirement.h"
#include "header/test_r16_strip_boundary.h"
namespace Simpsons::Graphics {
struct NativeRecordingProbe {
    static ID3D11DeviceContext* context(NativeBackend& b){return b.context.Get();}
    // TEST ONLY: drops the backend's exact-content sky cache. shared_ptr reset
    // is valid on the incomplete pointee; live handles keep their meshes alive.
    static void resetMeshCache(NativeBackend& b){b.skyMeshCache_.reset();}
};
}
void uploadCacheReuse(bool hardware) {
    NativeBackend backend(!hardware);
    const std::array<SkyVertex,4> base={{{{-1,1,0},{.25f,.5f},{}},{{-1,-1,0},{.25f,.5f},{}},
        {{1,1,0},{.25f,.5f},{}},{{1,-1,0},{.25f,.5f},{}}}};
    const std::array<uint16_t,4> strip{0,1,2,3};
    std::vector<SkyVertex> vv(base.begin(), base.end());
    std::vector<uint16_t> ii(strip.begin(), strip.end());
    auto first = backend.uploadSkyMesh(vv, ii);
    require(backend.uploadSkyMesh(vv, ii).get() == first.get(), "Sky repeat upload did not reuse exact bytes");
    // Same content at a different guest address must still hit (never by pointer).
    std::vector<SkyVertex> vvAlias = vv;
    std::vector<uint16_t> iiAlias = ii;
    require(vvAlias.data() != vv.data(), "Sky alias fixture shares guest address");
    require(backend.uploadSkyMesh(vvAlias, iiAlias).get() == first.get(), "Sky identical bytes at new address missed");
    // Caller mutation isolation: the cache snapshots exact bytes on insertion.
    const std::vector<SkyVertex> origV = vv;
    const std::vector<uint16_t> origI = ii;
    vv[0].uv[0] += 0.5f;
    require(backend.uploadSkyMesh(vv, ii).get() != first.get(), "Sky single-float change incorrectly hit");
    require(backend.uploadSkyMesh(origV, origI).get() == first.get(), "Sky original bytes missed after caller mutation");
    // Invalid bytes can never hit: they miss and run the existing checks.
    bool rejected = false;
    try {
        auto bad = origV;
        bad[0].position[0] = std::numeric_limits<float>::quiet_NaN();
        backend.uploadSkyMesh(bad, origI);
    } catch (const Error& e) {
        rejected = std::string(e.what()).find("Nonfinite sky position") != std::string::npos;
    }
    require(rejected, "Sky invalid position accepted after caching");
    const std::array<uint16_t,3> opaque = {0, 1, 4};
    const auto indexed=backend.uploadSkyMesh(origV,opaque);
    require(indexed!=first&&indexed->indexCount()==opaque.size()&&backend.uploadSkyMesh(origV,opaque)==indexed,
            "Sky opaque R16 bytes lost immutable upload/cache ownership before draw qualification");
    require(backend.uploadSkyMesh(origV, origI).get() == first.get(), "Sky valid bytes missed after invalid rejections");
    // Device isolation: no static global, no cross-device hits.
    NativeBackend foreign(!hardware);
    auto foreignMesh = foreign.uploadSkyMesh(origV, origI);
    require(foreignMesh.get() != first.get(), "Sky cross-device hit");
    rejected = false;
    try {
        backend.bindSkyMeshVertices(foreignMesh);
    } catch (const Error& e) {
        rejected = std::string(e.what()).find("another backend") != std::string::npos;
    }
    require(rejected, "Sky foreign mesh bound on local device");
    require(backend.uploadSkyMesh(origV, origI).get() == first.get(), "Sky local hit lost after foreign upload");
    // Owner isolation: a different thread must fail owner checks, never hit.
    bool ownerRejected = false;
    std::thread probe([&] {
        try {
            backend.uploadSkyMesh(origV, origI);
        } catch (const Error& e) {
            ownerRejected = std::string(e.what()).find("different thread") != std::string::npos;
        }
    });
    probe.join();
    require(ownerRejected, "Sky cross-thread upload bypassed owner checks");
}
void run(const std::vector<uint8_t>& image,bool hardware,bool opaque) {
    NativeBackend backend(!hardware);NativeMaterialCompiler compiler(backend);MaterialRegistry registry;
    const CompiledMaterial *vs=nullptr,*ps=nullptr;
    const uint32_t vertexAddress=opaque?0x82036C2Cu:0x82036F08u,pixelAddress=opaque?0x820371ECu:0x820374E8u;
    for(const auto& record:originalMaterialIdentities())if(record.originalAddress==vertexAddress||record.originalAddress==pixelAddress) {
        const auto id=registry.create(record.originalAddress,std::span<const uint8_t>(image).subspan(record.originalAddress-0x82000000,record.recordBytes));
        const auto& shader=registry.prepareForBind(id,compiler);if(record.stage==MaterialStage::Vertex)vs=&shader;else ps=&shader;
    }
    require(vs&&ps,"Missing original sky shader owners");
    constexpr UINT extent=16;
    auto color=backend.createTarget(extent,extent,TargetFormat::RGB10A2),sibling=backend.createTarget(extent,extent,TargetFormat::RGB10A2);
    auto depth=backend.createDepthTarget(extent,extent);
    backend.bindTargets({sibling,nullptr,nullptr,nullptr},depth);backend.clearTarget(color,{1,0,1,1});backend.clearTarget(sibling,{0,0,1,1});
    backend.clearDepthTarget(depth,.375f,0x67);
    auto* context=NativeRecordingProbe::context(backend);
    SkyMeshDraw draw{};draw.primitiveType=6;draw.indexCount=4;draw.viewport={0,0,extent,extent,0,0x3F800000};
    draw.depthCompare=7;draw.colorMask=15;draw.halfPixelOffset=1;draw.primitiveReset=draw.viewportEnable=draw.multisampleAntialias=1;
    draw.primitiveResetIndex=0xFFFF;draw.multisampleMask=0xFFFFFFFF;draw.depthPolicy=ShadowMeshDepthPolicy::Reference20e4Rne;
    draw.blendWord=0x00010001;draw.expandedBlend=1;
    const std::array<uint8_t,8> redGreen{255,0,0,255,0,255,0,255};
    draw.textures[0]=backend.createTexture(2,1,TextureFormat::RGBA8,redGreen);
    const std::array<uint8_t,4> transparent{};
    for(UINT i=1;i<4;++i)draw.textures[i]=backend.createTexture(1,1,TextureFormat::RGBA8,transparent);
    for(UINT i=0;i<4;++i){auto& sampler=draw.samplers[i];sampler.Filter=i<3?D3D11_FILTER_MIN_MAG_MIP_LINEAR:D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=i<3?D3D11_TEXTURE_ADDRESS_WRAP:D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD=13;sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;}
    const std::array<SkyVertex,4> vertices={{{{-1,1,0},{.25f,.5f},{}},{{-1,-1,0},{.25f,.5f},{}},
        {{1,1,0},{.25f,.5f},{}},{{1,-1,0},{.25f,.5f},{}}}};
    const std::array<uint16_t,4> indices{0,1,2,3};auto mesh=backend.uploadSkyMesh(vertices,indices);
    SkyVertexConstants vc{};SkyPixelConstants pc{};vc[0][0]=vc[1][1]=vc[3][3]=1;vc[22][0]=1;vc[47][0]=99;
    pc[23]={0,0,1,0};
    auto live=backend.createSkyReplayConstants(),secondLive=backend.createSkyReplayConstants();
    auto recording=backend.createRecordingContext();
    // Original sky inputs inherit projection, ticker and palette coordinates;
    // cloud velocities at c46/c47 are material-owned and deliberately disagree.
    std::unique_ptr<EffectRecord> sky,pool;
    for(const auto& id:originalEffectIdentities())if(id.originalAddress==0x82036448||id.originalAddress==0x820D5730) {
        auto record=std::make_unique<EffectRecord>(id.originalAddress,std::span<const uint8_t>(image).subspan(id.originalAddress-0x82000000,id.recordBytes));
        if(id.originalAddress==0x82036448)sky=std::move(record);else pool=std::move(record);
    }
    require(sky&&pool,"Missing original sky/pool metadata");
    std::vector<std::string_view> names;for(const auto& p:pool->parameters(true))names.push_back(p.name);
    const EffectReflection reflection(*sky,true,names);require(!reflection.passes.empty(),"Sky reflection has no first pass");
    NativeRecordingMask mask{};
    for(size_t stage=0;stage<2;++stage)for(size_t byte=0;byte<8;++byte)
        mask[8*stage+byte]=uint8_t(reflection.passes.front().masks[stage]>>(56-8*byte));
    require((mask[0]&0x84)==0x84&&!(mask[1]&0x10)&&(mask[8]&0x04),"Original sky mask lost projection/ticker/palette inheritance or inherited material velocities");
    const auto begin=[&] {auto p=backend.allocateRecordingPayload(recording,0x3000);backend.beginRecordingPayload(p,4,mask,mask);return p;};
    SkyVertexConstants moving{},slower{};SkyPixelConstants materialPS{};moving[47][0]=.5f;slower[47][0]=.25f;
    const EngineBindingResetProbe::Snapshot initial(context);
    const auto untouched=backend.readbackTarget(color),depthBefore=backend.readbackDepthTarget(depth),siblingBefore=backend.readbackTarget(sibling);
    auto first=begin();backend.bindSkyMeshVertices(first,mesh);backend.bindSkyMeshDeclaration(first,mesh);backend.bindSkyMeshIndices(first,mesh);
    backend.recordSkyMesh(first,color,depth,mesh,*vs,*ps,moving,materialPS,live,draw);backend.finishRecordingPayload(first);
    auto second=begin();backend.recordSkyMesh(second,color,depth,mesh,*vs,*ps,slower,materialPS,secondLive,draw);backend.finishRecordingPayload(second);
    require(EngineBindingResetProbe::Snapshot(context)==initial,"Sky recording changed immediate state");
    require(backend.readbackTarget(color)==untouched,"Sky recording drew before execution");
    rejects([&]{backend.executeRecordingPayload(first);});
    require(backend.recordingPayloadReceipt(first).executions==0,"Unready sky replay counted an execution");
    backend.updateSkyReplayConstants(live,vc,pc);backend.updateSkyReplayConstants(secondLive,vc,pc);
    auto execute=[&](const std::shared_ptr<NativeRecordingPayload>& p) {
        backend.clearTarget(color,{1,0,1,1});const EngineBindingResetProbe::Snapshot before(context);
        const auto receipt=backend.recordingPayloadReceipt(p);backend.executeRecordingPayload(p);backend.waitIdle();
        require(EngineBindingResetProbe::Snapshot(context)==before,"Sky replay changed immediate bindings");
        require(backend.readbackDepthTarget(depth)==depthBefore&&backend.readbackTarget(sibling)==siblingBefore,"Sky replay changed unrelated attachments");
        require(backend.recordingPayloadReceipt(p).executions==receipt.executions+1,"Sky replay execution accounting differs");
        return backend.readbackTarget(color);
    };
    const auto solid=[&](const std::vector<uint8_t>& bytes,uint32_t expected,uint32_t tolerance=0) {
        require(bytes.size()==extent*extent*4,"Sky target readback size differs");
        for(size_t i=0;i<bytes.size();i+=4){uint32_t value{};std::memcpy(&value,bytes.data()+i,4);
            bool matches=(value>>30)==(expected>>30);
            for(UINT shift:{0u,10u,20u})matches&=uint32_t(std::abs(int((value>>shift)&1023)-int((expected>>shift)&1023)))<=tolerance;
            if(!matches){std::fprintf(stderr,"Sky pixel %zu=%08X expected=%08X\n",i/4,value,expected);throw Error("Sky texture/material result differs");}}
    };
    // Linear filtering/UNORM conversion can round this exact midpoint to
    // either adjacent RGB10 code on hardware. Endpoint colors stay exact.
    solid(execute(first),1023u<<10);solid(execute(second),(512u<<10)|512u,1);
    {
        std::array<std::shared_ptr<RenderTarget>,2> queued;
        for(auto& target:queued)target=backend.createTarget(extent,extent,TargetFormat::RGB10A2);
        // copyFront validates the selected OM source. Restore the fixture's
        // sibling selection after testing this ordered queue.
        backend.bindTargets({color,nullptr,nullptr,nullptr},depth);
        const EngineBindingResetProbe::Snapshot queueBefore(context);
        const auto executions=backend.recordingPayloadReceipt(first).executions;
        for(auto& target:queued) {
            backend.clearTarget(color,{1,0,1,1});backend.executeRecordingPayload(first);
            (void)backend.copyFront(color,target);
        }
        vc[22][0]=0;backend.updateSkyReplayConstants(live,vc,pc);
        backend.clearTarget(color,{1,0,1,1});backend.executeRecordingPayload(first);backend.waitIdle();
        require(EngineBindingResetProbe::Snapshot(context)==queueBefore&&backend.recordingPayloadReceipt(first).executions==executions+3,
                "Queued sky replays changed bindings or execution accounting");
        for(const auto& target:queued)solid(backend.readbackTarget(target),1023u<<10);
        solid(backend.readbackTarget(color),1023);
        require(backend.readbackDepthTarget(depth)==depthBefore&&backend.readbackTarget(sibling)==siblingBefore,
                "Queued sky replay changed unrelated attachments");
        vc[22][0]=1;backend.updateSkyReplayConstants(live,vc,pc);
        backend.bindTargets({sibling,nullptr,nullptr,nullptr},depth);
    }
    vc[22][0]=0;backend.updateSkyReplayConstants(live,vc,pc);solid(execute(first),1023);solid(execute(second),(512u<<10)|512u,1);
    vc[22][0]=1;backend.updateSkyReplayConstants(live,vc,pc);const auto recorded=execute(first);solid(recorded,1023u<<10);
    // Compare real immediate and recorded draws using the same effective banks.
    auto effective=vc;effective[46]=moving[46];effective[47]=moving[47];
    backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.bindRigidShaders(*vs,*ps);
    for(UINT i=0;i<4;++i)backend.bindEngineTexture(i,draw.textures[i]);
    backend.bindSkyMeshVertices(mesh);backend.bindSkyMeshDeclaration(mesh);backend.bindSkyMeshIndices(mesh);
    observeMeshGpuRetirement(context,mesh,opaque?"sky_opaque":"sky_alpha");
    auto commit=backend.commitSky(*vs,*ps,effective,pc);backend.clearTarget(color,{1,0,1,1});
    backend.drawSkyMesh(color,depth,mesh,*vs,*ps,commit,draw);backend.waitIdle();
    require(backend.readbackTarget(color)==recorded,"Immediate and recorded sky rendering differ");
    {
        auto rejectedPayload=begin();const auto draws=backend.skyMeshDrawCount();const EngineBindingResetProbe::Snapshot before(context);
        const auto rejectRange=[&](const std::shared_ptr<NativeSkyMesh>& owner,const SkyMeshDraw& rejected) {
            rejects([&]{backend.drawSkyMesh(color,depth,owner,*vs,*ps,commit,rejected);});
            rejects([&]{backend.recordSkyMesh(rejectedPayload,color,depth,owner,*vs,*ps,moving,materialPS,live,rejected);});
            require(backend.recordingPayloadReceipt(rejectedPayload).recordedDraws==0&&backend.skyMeshDrawCount()==draws&&
                    backend.readbackTarget(color)==recorded&&EngineBindingResetProbe::Snapshot(context)==before,
                    "Rejected sky effective range changed receipts, pixels or bindings");
        };
        auto invalid=draw;invalid.baseVertex=-1;rejectRange(mesh,invalid);
        invalid=draw;invalid.baseVertex=std::numeric_limits<int32_t>::max();rejectRange(mesh,invalid);
        invalid=draw;invalid.startIndex=UINT32_MAX;rejectRange(mesh,invalid);
        invalid=draw;invalid.primitiveResetIndex=0xFFFE;rejectRange(mesh,invalid);
        const std::array<uint16_t,3> outside{0,1,4};auto outsideMesh=backend.uploadSkyMesh(vertices,outside);
        invalid=draw;invalid.indexCount=3;rejectRange(outsideMesh,invalid);
        backend.releaseRecordingPayload(rejectedPayload);
    }
    for(int32_t baseOffset:{2,-2,0,65532}) {
        std::vector<SkyVertex> owned(vertices.begin(),vertices.end());
        std::vector<uint16_t> words;
        auto shifted=draw;shifted.baseVertex=baseOffset;shifted.startIndex=1;
        if(baseOffset>0){owned.insert(owned.begin(),size_t(baseOffset),SkyVertex{});words={65534,0,1,2,65535,2,1,3,65534};shifted.indexCount=7;}
        else if(baseOffset<0){words={65534,2,3,4,65535,4,3,5,65534};shifted.indexCount=7;}
        else {words={65534,0,1,2,3,65534};shifted.indexCount=4;}
        const auto owner=backend.uploadSkyMesh(owned,words);
        auto shiftedPayload=begin();backend.recordSkyMesh(shiftedPayload,color,depth,owner,*vs,*ps,moving,materialPS,live,shifted);
        backend.finishRecordingPayload(shiftedPayload);
        backend.bindSkyMeshVertices(owner);backend.bindSkyMeshDeclaration(owner);backend.bindSkyMeshIndices(owner);
        backend.clearTarget(color,{1,0,1,1});const EngineBindingResetProbe::Snapshot before(context);
        backend.drawSkyMesh(color,depth,owner,*vs,*ps,commit,shifted);backend.waitIdle();
        require(EngineBindingResetProbe::Snapshot(context)==before&&backend.readbackTarget(color)==recorded,
                "Original-style sky effective indices changed direct pixels or retained bindings");
        require(execute(shiftedPayload)==recorded,"Original-style sky effective indices changed recorded pixels");
        if(baseOffset==65532) {
            observeMeshGpuRetirement(context,owner,"sky_large_owner");
            auto invalidRange=shifted;invalidRange.baseVertex=65533;auto empty=begin();const auto draws=backend.skyMeshDrawCount();
            rejects([&]{backend.drawSkyMesh(color,depth,owner,*vs,*ps,commit,invalidRange);});
            rejects([&]{backend.recordSkyMesh(empty,color,depth,owner,*vs,*ps,moving,materialPS,live,invalidRange);});
            require(!backend.recordingPayloadReceipt(empty).recordedDraws&&backend.skyMeshDrawCount()==draws&&
                EngineBindingResetProbe::Snapshot(context)==before&&backend.readbackTarget(color)==recorded,
                "Malformed large sky selected range changed receipts, pixels or bindings");
            backend.releaseRecordingPayload(empty);
        }
        backend.releaseRecordingPayload(shiftedPayload);rejects([&]{backend.executeRecordingPayload(shiftedPayload);});
        if(baseOffset==65532)std::printf("AUDIT_GPU_LARGE_OWNER family=sky vertices=65536 base_vertex=65532 create=passed direct_draw=passed recorded_draw=passed payload_release=passed stale_use=passed malformed=passed gpu_buffer_retirement=separate\n");
    }
    {
        const auto owned=Test::stripBoundaryVertices(vertices);const auto words=Test::stripBoundaryIndices();
        const auto owner=backend.uploadSkyMesh(owned,words);
        backend.bindSkyMeshVertices(owner);backend.bindSkyMeshDeclaration(owner);backend.bindSkyMeshIndices(owner);
        observeMeshGpuRetirement(context,owner,opaque?"sky_opaque_strip_boundary":"sky_alpha_strip_boundary");
        auto selected=draw;selected.startIndex=Test::stripBoundaryStart;selected.indexCount=Test::stripBoundaryCount;
        const auto direct=[&](const SkyMeshDraw& d) {
            const auto before=EngineBindingResetProbe::Snapshot(context);const auto countBefore=backend.skyMeshDrawCount();
            backend.drawSkyMesh(color,depth,owner,*vs,*ps,commit,d);backend.waitIdle();
            require(EngineBindingResetProbe::Snapshot(context)==before&&backend.skyMeshDrawCount()==countBefore+1,
                    "Sky split changed bindings or logical draw accounting");
        };
        for(uint32_t cull:{2u,6u}) {
            selected.cull=cull;backend.clearTarget(color,{1,0,1,1});direct(selected);
            const auto actualColor=backend.readbackTarget(color),actualDepth=backend.readbackDepthTarget(depth);
            for(UINT y=0;y<extent;++y)for(UINT x=0;x<extent;++x) {
                uint32_t value{};std::memcpy(&value,actualColor.data()+4*(y*extent+x),4);
                require(value==(Test::originalStripBoundaryCovered(x,y,cull)?1023u<<10:0xFFF003FF),
                        "Sky split coverage differs from independent triangle winding");
            }
            backend.clearTarget(color,{1,0,1,1});for(const auto& packet:Test::originalStripBoundaryPackets) {
                auto part=selected;part.indexCount=packet[0];part.startIndex=packet[1];direct(part);
            }
            require(backend.readbackTarget(color)==actualColor&&backend.readbackDepthTarget(depth)==actualDepth,
                    "Sky full draw differs from independent original packet pixels");
            auto boundary=begin();const auto retained=EngineBindingResetProbe::Snapshot(context);
            backend.recordSkyMesh(boundary,color,depth,owner,*vs,*ps,moving,materialPS,live,selected);backend.finishRecordingPayload(boundary);
            require(EngineBindingResetProbe::Snapshot(context)==retained&&backend.readbackTarget(color)==actualColor&&
                backend.readbackDepthTarget(depth)==actualDepth&&backend.recordingPayloadReceipt(boundary).recordedDraws==1,
                "Sky split recording emitted immediate work or changed logical receipt");
            require(execute(boundary)==actualColor,"Sky split direct and deferred pixels differ");
            backend.releaseRecordingPayload(boundary);rejects([&]{backend.executeRecordingPayload(boundary);});
        }
        auto empty=begin();
        for(const auto [start,count,baseOffset]:{std::array<int64_t,3>{1,65538,0},{UINT32_MAX,65536,0},{1,65536,-1},{1,65536,1}}) {
            auto bad=selected;bad.startIndex=uint32_t(start);bad.indexCount=uint32_t(count);bad.baseVertex=int32_t(baseOffset);
            const auto retained=EngineBindingResetProbe::Snapshot(context);const auto countBefore=backend.skyMeshDrawCount();
            const auto colorBefore=backend.readbackTarget(color),depthsBefore=backend.readbackDepthTarget(depth);
            rejects([&]{backend.drawSkyMesh(color,depth,owner,*vs,*ps,commit,bad);});
            rejects([&]{backend.recordSkyMesh(empty,color,depth,owner,*vs,*ps,moving,materialPS,live,bad);});
            require(EngineBindingResetProbe::Snapshot(context)==retained&&backend.skyMeshDrawCount()==countBefore&&
                backend.readbackTarget(color)==colorBefore&&backend.readbackDepthTarget(depth)==depthsBefore&&
                !backend.recordingPayloadReceipt(empty).recordedDraws,"Malformed sky split emitted a prefix or changed state");
        }
        backend.releaseRecordingPayload(empty);
        std::printf("AUDIT_GPU_STRIP_BOUNDARY family=sky vertex=%08X pixel=%08X count=65536 reset_position=65530 selected_start=1 sdk_packets=65534_4 sdk_advance=65532 cull=2_6 create=passed direct_pixels=passed original_packet_pixels=passed recorded_pixels=passed record_without_immediate_work=passed malformed=passed payload_release=passed stale_use=passed gpu_buffer_retirement=separate\n",vertexAddress,pixelAddress);
    }
    backend.bindSkyMeshVertices(mesh);backend.bindSkyMeshDeclaration(mesh);backend.bindSkyMeshIndices(mesh);
    // Both complete original pairs are supported, but mixed opaque/alpha
    // records may never borrow the other's artifact or constants.
    const CompiledMaterial *otherVS=nullptr,*otherPS=nullptr;
    for(const auto& record:originalMaterialIdentities())if(record.originalAddress==(opaque?0x82036F08u:0x82036C2Cu)||
        record.originalAddress==(opaque?0x820374E8u:0x820371ECu)) {
        const auto id=registry.create(record.originalAddress,std::span<const uint8_t>(image).subspan(record.originalAddress-0x82000000,record.recordBytes));
        const auto& shader=registry.prepareForBind(id,compiler);if(record.stage==MaterialStage::Vertex)otherVS=&shader;else otherPS=&shader;
    }
    require(otherVS&&otherPS,"Missing opposite sky pair for strict admission test");
    rejects([&]{backend.validateRigidShaders(*vs,*otherPS);});
    rejects([&]{backend.validateRigidShaders(*otherVS,*ps);});
    rejects([&]{backend.commitSky(*vs,*otherPS,effective,pc);});
    // Original alpha cleanup leaves the same SRC_ALPHA/INV_SRC_ALPHA
    // equation for a subsequent opaque pair. Both programs have identical
    // original arithmetic; fractional blue exposes RGB and alpha factors.
    {
        auto retained=draw;retained.blendEnable=1;retained.blendWord=0x07060706;
        const auto malformed=[&](const SkyMeshDraw& bad) {
            auto rejectedPayload=begin();const auto before=EngineBindingResetProbe::Snapshot(context);
            const auto pixels=backend.readbackTarget(color),depths=backend.readbackDepthTarget(depth);
            const auto direct=backend.skyMeshDrawCount(),recordedCount=backend.recordingDrawCount();
            rejects([&]{backend.drawSkyMesh(color,depth,mesh,*vs,*ps,commit,bad);});
            rejects([&]{backend.recordSkyMesh(rejectedPayload,color,depth,mesh,*vs,*ps,moving,materialPS,live,bad);});
            require(backend.recordingPayloadReceipt(rejectedPayload).recordedDraws==0&&
                backend.skyMeshDrawCount()==direct&&backend.recordingDrawCount()==recordedCount&&
                backend.readbackTarget(color)==pixels&&backend.readbackDepthTarget(depth)==depths&&
                EngineBindingResetProbe::Snapshot(context)==before,"Malformed sky blend changed accounting, outputs or bindings");
            backend.releaseRecordingPayload(rejectedPayload);
        };
        auto bad=retained;bad.blendEnable=2;malformed(bad);
        bad=retained;bad.expandedBlend=2;malformed(bad);
        bad=retained;bad.blendWord=0x00010706;malformed(bad);
        bad=retained;bad.blendWord=0x00010001;malformed(bad);
        bad=retained;bad.blendEnable=0;malformed(bad);
        const std::array<uint8_t,4> halfBlue{0,0,128,255};
        retained.textures[0]=backend.createTexture(1,1,TextureFormat::RGBA8,halfBlue);
        backend.bindEngineTexture(0,retained.textures[0]);std::vector<uint8_t> reference;
        for(uint32_t expanded:{0u,1u}) {
            retained.expandedBlend=expanded;
            auto payload=begin();backend.recordSkyMesh(payload,color,depth,mesh,*vs,*ps,moving,materialPS,live,retained);
            backend.finishRecordingPayload(payload);std::vector<uint8_t> direct;
            for(bool deferred:{false,true}) {
                backend.clearTarget(color,{1,0,1,1});const EngineBindingResetProbe::Snapshot before(context);
                if(deferred)backend.executeRecordingPayload(payload);
                else backend.drawSkyMesh(color,depth,mesh,*vs,*ps,commit,retained);
                backend.waitIdle();require(EngineBindingResetProbe::Snapshot(context)==before,"Inherited sky blend changed caller bindings");
                const auto result=backend.readbackTarget(color);solid(result,509u|(767u<<20)|(2u<<30),1);
                if(!deferred)direct=result;else require(result==direct,"Inherited sky immediate/recorded factors differ");
                if(reference.empty())reference=result;else require(result==reference,"Expanded sky blend tuple changed the same equation");
            }
            backend.releaseRecordingPayload(payload);rejects([&]{backend.executeRecordingPayload(payload);});
        }
        backend.bindEngineTexture(0,draw.textures[0]);
    }
    // A genuine original screen draw displaces these sky shaders. The
    // receipt qualifies that replacement without weakening the draw guard.
    const CompiledMaterial *screenVS[2]{},*screenPS[3]{};
    for(const auto& record:originalMaterialIdentities()) {
        const bool flatVS=record.originalAddress==0x821525E8,flatPS=record.originalAddress==0x821524C8;
        const bool texVS=record.originalAddress==0x82152880,texPS=record.originalAddress==0x82152708;
        const bool queryPS=record.originalAddress==0x821536F0;
        if(!flatVS&&!flatPS&&!texVS&&!texPS&&!queryPS)continue;
        const auto id=registry.create(record.originalAddress,std::span<const uint8_t>(image).subspan(record.originalAddress-0x82000000,record.recordBytes));
        const auto& shader=registry.prepareForBind(id,compiler);
        if(flatVS||texVS)screenVS[texVS?1:0]=&shader;else screenPS[queryPS?2:texPS?1:0]=&shader;
    }
    require(screenVS[0]&&screenVS[1]&&screenPS[0]&&screenPS[1]&&screenPS[2],"Screen replacement original artifacts are missing");
    {
        NativeBackend foreign(!hardware);
        auto query=backend.createTarget(64,8,TargetFormat::RGBA8);backend.clearTarget(query,{1,1,1,1});
        for(UINT textured=0;textured<2;++textured) {
            ScreenDraw screen;screen.vertices={{{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,-1,1,1}}};
            screen.color={0,0,0,1};screen.colorWriteMask=15;screen.blendSelector=3;
            if(textured){screen.texture=draw.textures[0];screen.sampler=draw.samplers[0];}
            const auto count=backend.screenDrawCount();
            rejects([&]{backend.completedScreenReplacement(count,*screenVS[textured],*screenPS[textured]);});
            backend.drawOriginalScreen(color,depth,screen,false,7);
            auto receipt=backend.completedScreenReplacement(count,*screenVS[textured],*screenPS[textured]);
            backend.requireScreenReplacement(receipt);
            rejects([&]{backend.requireRigidShaders(*vs,*ps);});
            rejects([&]{foreign.requireScreenReplacement(receipt);});
            rejects([&]{backend.completedScreenReplacement(count,*vs,*screenPS[textured]);});
            PostFilterDraw modulated;modulated.pixelShader=0x82152318;modulated.secondaryInput=query;
            modulated.secondarySampler=draw.samplers[0];modulated.vertices={-1,-1,1,-1,-1,1};
            modulated.viewport={0,0,color->pixelWidth(),color->pixelHeight(),0x3F800000,0};
            modulated.pixelConstants[0]={0,0,0,1};modulated.pixelConstants[1]={.5f,.5f,0,0};
            modulated.blendWord=0x10706;modulated.blendEnable=1;modulated.expandedBlend=1;
            for(UINT repeat=0;repeat<2;++repeat) {
                const auto postBefore=backend.postFilterDrawCount();
                rejects([&]{backend.completedRestoredScreenReplacement(postBefore,receipt);});
                const EngineBindingResetProbe::Snapshot before(context);
                backend.drawPostFilter(color,modulated);
                require(EngineBindingResetProbe::Snapshot(context)==before,"Modulated replacement changed retained bindings");
                backend.requireCompletedModulatedPostFilter(postBefore);
                backend.requireScreenReplacement(receipt); // A scoped restore retains the prior pair/epoch.
                receipt=backend.completedRestoredScreenReplacement(postBefore,receipt);
                backend.requireScreenReplacement(receipt);
                rejects([&]{backend.completedRestoredScreenReplacement(postBefore+1,receipt);});
            }
            ComPtr<ID3D11VertexShader> actualVS;ComPtr<ID3D11PixelShader> actualPS;
            context->VSGetShader(&actualVS,nullptr,nullptr);context->PSGetShader(&actualPS,nullptr,nullptr);
            const auto pixels=backend.readbackTarget(color);
            context->VSSetShader(nullptr,nullptr,0);rejects([&]{backend.requireScreenReplacement(receipt);});
            context->VSSetShader(actualVS.Get(),nullptr,0);
            context->PSSetShader(nullptr,nullptr,0);rejects([&]{backend.requireScreenReplacement(receipt);});
            context->PSSetShader(actualPS.Get(),nullptr,0);backend.requireScreenReplacement(receipt);
            require(backend.screenDrawCount()==count+1&&backend.readbackTarget(color)==pixels,"Receipt rejection mutated screen output");
            auto unrelated=modulated;unrelated.pixelShader=0x821524C8;unrelated.secondaryInput.reset();
            unrelated.strip=std::array<float,8>{-1,-1,1,-1,-1,1,1,1};
            const auto unrelatedBefore=backend.postFilterDrawCount();backend.drawPostFilter(color,unrelated);
            backend.requireScreenReplacement(receipt); // Unrelated scoped work cannot retire the logical FX.
            rejects([&]{backend.requireCompletedModulatedPostFilter(unrelatedBefore);});
            rejects([&]{backend.completedRestoredScreenReplacement(unrelatedBefore,receipt);});
            backend.drawOriginalScreen(color,depth,screen,false,7);
            rejects([&]{backend.requireScreenReplacement(receipt);});
            rejects([&]{backend.completedScreenReplacement(count,*screenVS[textured],*screenPS[textured]);});
            const auto next=backend.completedScreenReplacement(count+1,*screenVS[textured],*screenPS[textured]);
            backend.retireScreenReplacement(next);rejects([&]{backend.requireScreenReplacement(next);});
            const auto lastCount=backend.screenDrawCount();backend.drawOriginalScreen(color,depth,screen,false,7);
            const auto last=backend.completedScreenReplacement(lastCount,*screenVS[textured],*screenPS[textured]);
            context->VSGetShader(&actualVS,nullptr,nullptr);context->PSGetShader(&actualPS,nullptr,nullptr);
            backend.bindRigidShaders(*vs,*ps);
            context->VSSetShader(actualVS.Get(),nullptr,0);context->PSSetShader(actualPS.Get(),nullptr,0);
            rejects([&]{backend.requireScreenReplacement(last);}); // Same COM pair cannot revive an old epoch.
            backend.bindRigidShaders(*vs,*ps);
        }
        // A skipped original query changes only the proven vertex inputs;
        // it cannot claim a completed screen draw or permit arbitrary PS.
        const auto beforeInputs=backend.screenDrawCount();const auto pixels=backend.readbackTarget(color);
        const auto inputs=backend.bindDirectSpriteInputs(draw.textures[0],draw.samplers[0]);
        backend.requireScreenInputReplacement(inputs);
        rejects([&]{backend.requireScreenInputReplacement(NativeScreenInputReceipt{});});
        rejects([&]{foreign.requireScreenInputReplacement(inputs);});
        rejects([&]{backend.requireRigidShaders(*vs,*ps);});
        rejects([&]{backend.completedScreenReplacement(beforeInputs,*screenVS[1],*screenPS[1]);});
        ComPtr<ID3D11VertexShader> inputVS;ComPtr<ID3D11PixelShader> retainedPS;
        context->VSGetShader(&inputVS,nullptr,nullptr);context->PSGetShader(&retainedPS,nullptr,nullptr);
        context->VSSetShader(nullptr,nullptr,0);rejects([&]{backend.requireScreenInputReplacement(inputs);});
        context->VSSetShader(inputVS.Get(),nullptr,0);
        context->PSSetShader(nullptr,nullptr,0);rejects([&]{backend.requireScreenInputReplacement(inputs);});
        context->PSSetShader(retainedPS.Get(),nullptr,0);backend.requireScreenInputReplacement(inputs);
        require(backend.screenDrawCount()==beforeInputs&&backend.readbackTarget(color)==pixels,
                "Sprite input transaction submitted pixels or changed its draw count");
        const auto repeated=backend.bindDirectSpriteInputs(draw.textures[0],draw.samplers[0]);
        rejects([&]{backend.requireScreenInputReplacement(inputs);});
        backend.requireScreenInputReplacement(repeated);
        ComPtr<ID3D11InputLayout> inputLayout;context->IAGetInputLayout(&inputLayout);
        const auto batch=backend.finishSpriteBatch(false);backend.requireScreenBatchRetirement(batch);
        backend.requireScreenInputReplacement(repeated);
        require(backend.screenDrawCount()==beforeInputs&&backend.readbackTarget(color)==pixels,
                "Original sprite batch input cleanup submitted a draw");
        rejects([&]{backend.requireScreenBatchRetirement(NativeScreenBatchReceipt{});});
        rejects([&]{foreign.requireScreenBatchRetirement(batch);});
        context->IASetInputLayout(inputLayout.Get());rejects([&]{backend.requireScreenBatchRetirement(batch);});
        context->IASetInputLayout(nullptr);backend.requireScreenBatchRetirement(batch);
        context->PSSetShader(nullptr,nullptr,0);rejects([&]{backend.requireScreenBatchRetirement(batch);});
        context->PSSetShader(retainedPS.Get(),nullptr,0);backend.requireScreenBatchRetirement(batch);
        const auto nextBatch=backend.finishSpriteBatch(false);
        rejects([&]{backend.requireScreenBatchRetirement(batch);});backend.requireScreenBatchRetirement(nextBatch);
        backend.retireScreenInputReplacement(repeated);
        rejects([&]{backend.requireScreenInputReplacement(repeated);});
        rejects([&]{backend.requireScreenBatchRetirement(nextBatch);});
        backend.bindRigidShaders(*vs,*ps);
        auto coronaQuery=backend.createTarget(64,8,TargetFormat::RGB10A2);backend.clearTarget(coronaQuery,{1,1,1,1});
        for(bool queried:{false,true}) {
            const auto pending=backend.bindDirectSpriteInputs(draw.textures[0],draw.samplers[0]);
            backend.requireScreenInputReplacement(pending);
            ScreenDraw sprite;sprite.vertices={{{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,-1,1,1}}};
            sprite.texture=draw.textures[0];sprite.sampler=draw.samplers[0];sprite.color={0,0,0,1};
            sprite.blendSelector=0;sprite.colorWriteMask=15;
            if(queried){sprite.coronaQuery=coronaQuery;sprite.coronaUV={.5f,.5f};}
            const auto count=backend.screenDrawCount();backend.drawOriginalScreen(color,depth,sprite,false,7,false);
            rejects([&]{backend.requireScreenInputReplacement(pending);});
            const auto completed=backend.completedScreenReplacement(count,*screenVS[1],*screenPS[queried?2:1]);
            backend.requireScreenReplacement(completed);
            rejects([&]{backend.completedScreenReplacement(count,*screenVS[1],*screenPS[queried?1:2]);});
            const auto completedBatch=backend.finishSpriteBatch();backend.requireScreenBatchRetirement(completedBatch);
            backend.requireScreenReplacement(completed);
            require(backend.screenDrawCount()==count+1,"Completed sprite batch cleanup submitted an extra draw");
            backend.retireScreenReplacement(completed);
            rejects([&]{backend.requireScreenBatchRetirement(completedBatch);});
            backend.bindRigidShaders(*vs,*ps);
        }
    }
    for(UINT i=0;i<4;++i)backend.bindEngineTexture(i,draw.textures[i]);
    backend.bindSkyMeshVertices(mesh);backend.bindSkyMeshDeclaration(mesh);backend.bindSkyMeshIndices(mesh);
    commit=backend.commitSky(*vs,*ps,effective,pc);
    // A sky vertex exports clip z==w. With the game's reversed viewport this
    // is depth zero, behind every foreground sample, even when drawn last.
    // Seed alternating foreground/background depth to catch color overwrite
    // as well as depth corruption in both immediate and cached draws.
    ComPtr<ID3D11Device> device;context->GetDevice(&device);
    ComPtr<ID3D11DepthStencilView> depthView;context->OMGetRenderTargets(0,nullptr,&depthView);
    ComPtr<ID3D11Resource> depthResource;depthView->GetResource(&depthResource);
    ComPtr<ID3D11Texture2D> depthTexture;require(SUCCEEDED(depthResource.As(&depthTexture)),"Sky test depth resource is not a texture");
    D3D11_TEXTURE2D_DESC depthDesc{};depthTexture->GetDesc(&depthDesc);
    depthDesc.BindFlags=0;depthDesc.MiscFlags=0;
    for(bool reverse:{true,false})for(UINT write:{0u,1u}) {
        auto occluded=draw;occluded.depthEnable=1;occluded.depthWrite=write;
        occluded.depthCompare=reverse?6:3;
        occluded.viewport[4]=reverse?0x3F800000:0;occluded.viewport[5]=reverse?0:0x3F800000;
        std::vector<uint32_t> seeded(extent*extent*2);
        for(UINT y=0;y<extent;++y)for(UINT x=0;x<extent;++x) {
            const size_t i=y*extent+x;
            seeded[2*i]=std::bit_cast<uint32_t>((x+y)%2?.375f:(reverse?0.f:1.f));seeded[2*i+1]=0x67;
        }
        D3D11_SUBRESOURCE_DATA seedData{seeded.data(),extent*8,0};ComPtr<ID3D11Texture2D> seed;
        require(SUCCEEDED(device->CreateTexture2D(&depthDesc,&seedData,&seed)),"Sky test depth seed creation failed");
        auto cached=begin();backend.recordSkyMesh(cached,color,depth,mesh,*vs,*ps,moving,materialPS,live,occluded);
        backend.finishRecordingPayload(cached);
        for(bool deferred:{false,true}) {
            context->CopyResource(depthTexture.Get(),seed.Get());backend.clearTarget(color,{1,0,1,1});
            const auto originalDepth=backend.readbackDepthTarget(depth);
            const EngineBindingResetProbe::Snapshot before(context);
            if(deferred)backend.executeRecordingPayload(cached);
            else backend.drawSkyMesh(color,depth,mesh,*vs,*ps,commit,occluded);
            backend.waitIdle();
            require(EngineBindingResetProbe::Snapshot(context)==before,"Sky depth draw changed caller bindings");
            const auto pixels=backend.readbackTarget(color),depths=backend.readbackDepthTarget(depth);
            for(UINT y=0;y<extent;++y)for(UINT x=0;x<extent;++x) {
                const size_t i=y*extent+x;uint32_t actual{};std::memcpy(&actual,pixels.data()+4*i,4);
                const uint32_t expected=(x+y)%2?0xFFF003FFu:1023u<<10;
                if(actual!=expected) {
                    std::fprintf(stderr,"Sky occlusion reverse=%u write=%u deferred=%u pixel(%u,%u)=%08X expected=%08X\n",
                        unsigned(reverse),write,unsigned(deferred),x,y,actual,expected);
                    throw Error("Sky covered foreground or failed to fill background");
                }
                require(std::memcmp(depths.data()+8*i,originalDepth.data()+8*i,5)==0,
                    "Sky corrupted foreground depth or stencil");
            }
        }
        backend.releaseRecordingPayload(cached);
    }
    // Original PS slot8 discards when the line-mask red channel is > .99.
    // 252/255 is below that threshold; 253..255 are above it. Isolate the
    // shader's discard from fixed-function rejection with an ALWAYS depth test.
    {
        const std::array<uint8_t,16> maskPixels{252,0,0,255,253,0,0,255,254,0,0,255,255,0,0,255};
        auto masked=draw;masked.textures[3]=backend.createTexture(4,1,TextureFormat::RGBA8,maskPixels);
        masked.depthEnable=masked.depthWrite=1;masked.depthCompare=7;
        masked.viewport[4]=0x3F800000;masked.viewport[5]=0;
        backend.bindEngineTexture(3,masked.textures[3]);
        auto cached=begin();backend.recordSkyMesh(cached,color,depth,mesh,*vs,*ps,moving,materialPS,live,masked);
        backend.finishRecordingPayload(cached);
        for(bool deferred:{false,true}) {
            backend.clearDepthTarget(depth,.375f,0x67);backend.clearTarget(color,{1,0,1,1});
            const EngineBindingResetProbe::Snapshot before(context);
            if(deferred)backend.executeRecordingPayload(cached);
            else backend.drawSkyMesh(color,depth,mesh,*vs,*ps,commit,masked);
            backend.waitIdle();
            require(EngineBindingResetProbe::Snapshot(context)==before,"Sky mask draw changed caller bindings");
            const auto pixels=backend.readbackTarget(color),depths=backend.readbackDepthTarget(depth);
            for(UINT y=0;y<extent;++y)for(UINT x=0;x<extent;++x) {
                const size_t i=y*extent+x;uint32_t actual{},z{};
                std::memcpy(&actual,pixels.data()+4*i,4);std::memcpy(&z,depths.data()+8*i,4);
                const bool discarded=x>=extent/4;
                require(actual==(discarded?0xFFF003FFu:1023u<<10),"Sky mask failed to discard foreground color");
                require(z==std::bit_cast<uint32_t>(discarded?.375f:0.f)&&depths[8*i+4]==0x67,
                    "Sky mask failed to preserve foreground depth/stencil");
            }
        }
        backend.releaseRecordingPayload(cached);backend.bindEngineTexture(3,draw.textures[3]);
    }
    backend.clearDepthTarget(depth,.375f,0x67);
    backend.bindTargets({sibling,nullptr,nullptr,nullptr},depth);
    // Each payload retains wrappers after caller references disappear.
    std::weak_ptr<Texture> texture=draw.textures[0];std::weak_ptr<NativeSkyMesh> geometry=mesh;
    auto pending=begin();auto invalid=draw;invalid.textures[0].reset();
    rejects([&]{backend.recordSkyMesh(pending,color,depth,mesh,*vs,*ps,moving,materialPS,live,invalid);});
    require(backend.recordingPayloadReceipt(pending).recordedDraws==0,"Rejected sky draw changed its receipt");
    backend.recordSkyMesh(pending,color,depth,mesh,*vs,*ps,moving,materialPS,live,draw);backend.finishRecordingPayload(pending);
    rejects([&]{backend.bindSkyMeshVertices(pending,mesh);});backend.releaseRecordingPayload(pending);
    auto nonfinite=vc;nonfinite[63][0]=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{backend.updateSkyReplayConstants(live,nonfinite,pc);});
    for(auto& value:draw.textures)value.reset();for(auto& value:invalid.textures)value.reset();mesh.reset();
    require(!texture.expired()&&!geometry.expired(),"Sky payload lost an owned resource");solid(execute(first),1023u<<10);
    // Cache ownership drops while the sealed lists still retain the draw:
    // eviction must not destroy in-flight geometry; list release must retire it.
    NativeRecordingProbe::resetMeshCache(backend);
    require(!geometry.expired(),"Cache eviction destroyed an in-flight retained draw");
    backend.releaseSkyReplayConstants(live);rejects([&]{backend.executeRecordingPayload(first);});
    rejects([&]{backend.updateSkyReplayConstants(live,vc,pc);});
    backend.releaseRecordingPayload(first);backend.releaseRecordingPayload(second);backend.releaseSkyReplayConstants(secondLive);
    require(texture.expired()&&geometry.expired(),"Released sky payload retained resource wrappers");
    backend.releaseRecordingContext(recording);
    std::printf("PASS sky mesh %s %s: forward/reversed foreground occlusion, background fill, depth-write on/off, actual recorded/immediate pixel parity, live ticker/material inheritance, independent payloads, retained resources, state preservation and rejected lifetimes\n",opaque?"opaque":"alpha",hardware?"hardware":"WARP");
}
int main(int argc,char** argv)try {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    require(argc==2||(argc==3&&std::string(argv[2])=="--hardware"),"Supply original image and optional --hardware");
    std::ifstream input(argv[1],std::ios::binary);std::vector<uint8_t> image((std::istreambuf_iterator<char>(input)),{});
    require(image.size()==15466496,"Original image size differs");run(image,argc==3,false);run(image,argc==3,true);uploadCacheReuse(argc==3);requireMeshGpuRetirements([](bool value,const char* message){require(value,message);});return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL sky mesh: %s\n",error.what());return 1;}
