#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "renderer/skin_mesh.h"
#include "renderer/native_material_compiler.h"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void require(bool value,const char* why){++checks;if(!value)throw Error(why);}
template<class F>void rejects(F action){try{action();}catch(const Error&){++checks;return;}require(false,"Unqualified skin variant operation succeeded");}
uint32_t word(std::span<const uint8_t> bytes,size_t at){uint32_t value;std::memcpy(&value,bytes.data()+at,4);return value;}
struct Pair {uint32_t vertex,pixel;bool alpha,second;const char* name;};
constexpr std::array<Pair,6> pairs{{
    {0x8202579C,0x82027C18,false,false,"gloss opaque"},{0x820269E8,0x82028258,true,false,"gloss alpha"},
    {0x8203D93C,0x82040118,false,false,"flipbook opaque"},{0x8203ED38,0x82040510,true,false,"flipbook alpha"},
    {0x8204BA4C,0x8204E2DC,false,true,"dual UV opaque"},{0x8204CE90,0x8204E75C,true,true,"dual UV alpha"}
}};
}
#include "header/test_engine_binding_reset.h"
namespace Simpsons::Graphics {
struct NativeRecordingProbe {static ID3D11DeviceContext* context(NativeBackend& backend){return backend.context.Get();}};
}
namespace {
void run(const std::vector<uint8_t>& image,bool hardware,const Pair& pair){
    NativeBackend backend(!hardware);NativeMaterialCompiler compiler(backend);MaterialRegistry registry;
    const CompiledMaterial *vs=nullptr,*ps=nullptr;
    for(const auto& record:originalMaterialIdentities())if(record.originalAddress==pair.vertex||record.originalAddress==pair.pixel){
        const auto id=registry.create(record.originalAddress,std::span<const uint8_t>(image).subspan(record.originalAddress-0x82000000,record.recordBytes));
        const auto& shader=registry.prepareForBind(id,compiler);if(record.stage==MaterialStage::Vertex)vs=&shader;else ps=&shader;
    }
    require(vs&&ps,"New skin pair lacks immutable shader owners");rejects([&]{backend.validateSkinShaders(*ps,*vs);});
    auto color=backend.createTarget(16,16,TargetFormat::RGB10A2),sibling=backend.createTarget(16,16,TargetFormat::RGB10A2);
    auto depth=backend.createDepthTarget(16,16);const std::array<uint8_t,4> firstTexel{64,128,192,255},secondTexel{192,64,128,128};
    auto base=backend.createTexture(1,1,TextureFormat::RGBA8,firstTexel),second=backend.createTexture(1,1,TextureFormat::RGBA8,secondTexel);
    std::array<SkinVertex,4> vertices{};vertices[0].position={-1,1,.25f};vertices[1].position={-1,-1,.25f};
    vertices[2].position={1,1,.25f};vertices[3].position={1,-1,.25f};
    for(auto& vertex:vertices){vertex.normal={0,0,1};vertex.indices={1,0,0,0};vertex.weights={1,0,0,0};
        vertex.color={1,1,1,.8f};vertex.uv={.25f,.5f};vertex.uv1={.75f,.125f};
        vertex.morph1={.25f,-.5f,.125f};vertex.morph6={-.125f,.25f,.5f};}
    const std::array<uint16_t,4> indices{0,1,2,3};auto mesh=backend.uploadSkinMesh(vertices,indices,pair.vertex);
    require(backend.uploadSkinMesh(vertices,indices,pair.vertex).get()==mesh.get(),"Exact skin variant bytes failed their own cache");
    const auto read=backend.readbackSkinMeshVertices(mesh);require(read.size()==vertices.size()&&std::memcmp(read.data(),vertices.data(),sizeof(vertices))==0,"Skin variant upload changed consumed/unused source lanes");
    require(backend.readbackSkinMeshIndices(mesh)==std::vector<uint16_t>(indices.begin(),indices.end()),"Skin variant changed R16 strip indices");
    SkinVertexConstants vc{};SkinPixelConstants pc{};for(size_t i=0;i<4;++i){vc[i][i]=1;vc[12+i][i]=1;}
    for(size_t i=0;i<3;++i)vc[55+i][i]=1;vc[22][0]=.25f;
    if(pair.second)vc[46]={1,1,1,1};else if(pair.vertex==0x8203D93C||pair.vertex==0x8203ED38)vc[47]={1,1,1,0};
    pc[4]={0,0,3,1};pc[40][3]=.5f;pc[49]={-1,0,0,0};pc[46][0]=.3f;pc[47][0]=.4f;pc[50][0]=2;
    if(pair.second){pc[42]={0,1,1,1};pc[46][0]=0;pc[47][0]=0;}
    SkinMeshDraw draw{};draw.primitiveType=6;draw.indexCount=4;draw.viewport={0,0,16,16,0x3F800000,0};
    draw.depthEnable=draw.depthWrite=1;draw.depthCompare=7;draw.colorMask=15;draw.halfPixelOffset=1;
    draw.primitiveReset=draw.viewportEnable=draw.multisampleAntialias=1;draw.primitiveResetIndex=0xFFFF;draw.multisampleMask=0xFFFFFFFF;
    draw.depthPolicy=ShadowMeshDepthPolicy::Reference20e4Rne;draw.blendWord=pair.alpha?0x07060706u:0x00010001u;
    draw.expandedBlend=1;draw.blendEnable=pair.alpha;draw.baseTexture=base;if(pair.second)draw.secondTexture=second;
    for(auto& sampler:draw.samplers){sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sampler.MaxAnisotropy=1;
        sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxLOD=13;}
    backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.bindSkinShaders(*vs,*ps);
    backend.bindSkinMeshVertices(mesh);backend.bindSkinMeshDeclaration(mesh);backend.bindSkinMeshIndices(mesh);
    backend.bindEngineTexture(0,base);if(pair.second)backend.bindEngineTexture(1,second);
    auto commit=backend.commitSkin(*vs,*ps,vc,pc);require(backend.readbackSkinVertexConstants(commit)==vc&&backend.readbackSkinPixelConstants(commit)==pc,"New skin immutable constant banks differ");
    backend.clearTarget(sibling,{0,1,1,1});const auto siblingBefore=backend.readbackTarget(sibling);
    const auto clear=[&]{backend.clearTarget(color,{1,0,1,1});backend.clearDepthTarget(depth,0,0x67);};
    clear();const auto blank=backend.readbackTarget(color);const EngineBindingResetProbe::Snapshot before(NativeRecordingProbe::context(backend));
    backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,draw);backend.waitIdle();
    require(EngineBindingResetProbe::Snapshot(NativeRecordingProbe::context(backend))==before,"New skin direct draw changed retained bindings");
    const auto immediate=backend.readbackTarget(color),depths=backend.readbackDepthTarget(depth);
    require(immediate!=blank,"New skin mesh did not emit visible original material output");
    for(size_t pixel=0;pixel<256;++pixel)require(word(depths,8*pixel)==std::bit_cast<uint32_t>(.75f)&&depths[8*pixel+4]==0x67,"New skin Reference20e4 depth/stencil changed");
    const auto count=backend.skinMeshDrawCount();auto invalid=draw;invalid.baseTexture.reset();rejects([&]{backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,invalid);});
    invalid=draw;invalid.characterShadow=depth;rejects([&]{backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,invalid);});
    invalid=draw;if(pair.second)invalid.secondTexture.reset();else invalid.secondTexture=second;
    rejects([&]{backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,invalid);});
    invalid=draw;invalid.samplers[pair.second?1:0].MaxLOD=0;rejects([&]{backend.drawSkinMesh(color,depth,mesh,*vs,*ps,commit,invalid);});
    const auto wrongAddress=pair.vertex==0x8202579C?0x820269E8u:0x8202579Cu;auto wrongMesh=backend.uploadSkinMesh(vertices,indices,wrongAddress);
    require(wrongMesh.get()!=mesh.get(),"New skin exact VS/pass cache identity aliased");
    rejects([&]{backend.drawSkinMesh(color,depth,wrongMesh,*vs,*ps,commit,draw);});
    require(backend.skinMeshDrawCount()==count&&backend.readbackTarget(color)==immediate,"Rejected new skin owner changed draw count or pixels");
    auto recording=backend.createRecordingContext();auto payload=backend.allocateRecordingPayload(recording,0x3000);NativeRecordingMask mask{};
    backend.beginRecordingPayload(payload,4,mask,mask);auto live=backend.createSkinReplayConstants(vc,pc);
    const EngineBindingResetProbe::Snapshot beforeRecord(NativeRecordingProbe::context(backend));
    rejects([&]{backend.recordSkinMesh(payload,color,depth,wrongMesh,*vs,*ps,vc,pc,live,draw);});
    backend.recordSkinMesh(payload,color,depth,mesh,*vs,*ps,vc,pc,live,draw);backend.finishRecordingPayload(payload);
    require(EngineBindingResetProbe::Snapshot(NativeRecordingProbe::context(backend))==beforeRecord&&backend.readbackTarget(color)==immediate,"New skin recording changed live output or bindings");
    clear();const EngineBindingResetProbe::Snapshot beforeReplay(NativeRecordingProbe::context(backend));
    backend.executeRecordingPayload(payload);backend.waitIdle();require(backend.readbackTarget(color)==immediate&&backend.readbackDepthTarget(depth)==depths,"New skin recorded output differs from direct material/depth");
    require(EngineBindingResetProbe::Snapshot(NativeRecordingProbe::context(backend))==beforeReplay,"New skin replay changed retained bindings");
    require(backend.readbackTarget(sibling)==siblingBefore,"New skin draw changed sibling target");
    backend.releaseSkinReplayConstants(live);rejects([&]{backend.executeRecordingPayload(payload);});
    backend.releaseRecordingPayload(payload);backend.releaseRecordingContext(recording);
    std::printf("PASS skin %s %s: direct/record material/depth, strict resources/pair/cache, immutable uploads and retained state\n",pair.name,hardware?"hardware":"WARP");
}
}
int main(int argc,char** argv)try{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);require(argc==2||(argc==3&&std::string(argv[2])=="--hardware"),"Supply original image and optional --hardware");
    std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> image{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};require(image.size()==15466496,"Original image extent differs");
    for(const auto& pair:pairs)run(image,argc==3,pair);std::printf("PASS skin variant mesh: %zu checks across six exact original pairs\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL skin variant mesh: %zu checks %s\n",checks,error.what());return 1;}
