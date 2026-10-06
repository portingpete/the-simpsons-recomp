#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "renderer/rigid_mesh.h"
#include "renderer/native_material_compiler.h"
#include "renderer/effect_reflection.h"
#include "renderer/device_availability.h"
#include "VSFlat.h"
#include "PSFlat.h"
#include "VSRigidNormalTangent.h"
#include "GSRigidNormalTangentProbe.h"
#include "VSChocolate.h"
#include "GSChocolateUVProbe.h"
#include <d3d11sdklayers.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <thread>

using namespace Simpsons::Graphics;
size_t rigidChecks{};
void require(bool value,const char* reason){++rigidChecks;if(!value)throw Error(reason);}
template<class F>void rejects(F action){bool rejected=false;try{action();}catch(const Error&){rejected=true;}require(rejected,"Invalid rigid operation did not reject");}
#include "header/test_engine_binding_reset.h"
#include "header/test_mesh_gpu_retirement.h"
#include "header/test_r16_strip_boundary.h"
namespace Simpsons::Graphics {
struct NativeRecordingProbe {
    static ID3D11Device* device(NativeBackend& b){return b.device.Get();}
    static ID3D11DeviceContext* immediate(NativeBackend& b){return b.context.Get();}
    // TEST ONLY: drops the backend's exact-content rigid cache. shared_ptr reset
    // is valid on the incomplete pointee; live handles keep their meshes alive.
    static void resetMeshCache(NativeBackend& b){b.rigidMeshCache_.reset();}
    static void replaceDevice(NativeBackend& b,bool hardware) {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        const auto hr=D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,2,D3D11_SDK_VERSION,&device,&level,&context);
        if(FAILED(hr)) throw Error("Deterministic rigid device replacement failed");
        b.availability=std::make_shared<DeviceAvailability>(device.Get());b.device=device;b.context=context;b.featureLevel=level;
    }
    static void debug(NativeBackend& b,bool hardware) {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        auto hr=D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_DEBUG,levels,2,D3D11_SDK_VERSION,&device,&level,&context);
        if(hr==E_INVALIDARG)hr=D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_DEBUG,levels+1,1,D3D11_SDK_VERSION,&device,&level,&context);
        if(SUCCEEDED(hr)){b.availability=std::make_shared<DeviceAvailability>(device.Get());b.device=device;b.context=context;b.featureLevel=level;
            std::fprintf(stderr,"[RIGID MESH TEST] D3D11 debug layer enabled\n");}
    }
};
}
namespace {
using Vec=std::array<float,4>;
void hr(HRESULT value,const char* message){require(SUCCEEDED(value),message);}
uint32_t word(const std::vector<uint8_t>& data,size_t at){uint32_t result{};std::memcpy(&result,data.data()+at,4);return result;}
uint32_t bits(float value){return std::bit_cast<uint32_t>(value);}
struct Snapshot {
    EngineBindingResetProbe::Snapshot bindings;
    std::vector<uintptr_t> extra;
    explicit Snapshot(ID3D11DeviceContext* context):bindings(context) {
        ID3D11Predicate* predicate{};BOOL value{};context->GetPredication(&predicate,&value);
        EngineBindingResetProbe::Snapshot::pointer(extra,predicate);extra.push_back(value);
        std::array<ID3D11Buffer*,4> buffers{};context->SOGetTargets(4,buffers.data());
        for(auto* buffer:buffers)EngineBindingResetProbe::Snapshot::pointer(extra,buffer);
    }
    bool operator==(const Snapshot& other)const{return bindings==other.bindings&&extra==other.extra;}
};
// Numerical 20e4 reference independent of the shader's exponent bit packing.
float quantize(float input) {
    const double z=std::clamp(double(input),0.0,1.0);if(z==0)return 0;
    const double step=std::ldexp(1.0,z<std::ldexp(1.0,-14)?-34:std::ilogb(z)-20);
    const double units=z/step,lo=std::floor(units),fraction=units-lo;
    return float((lo+(fraction>.5||(fraction==.5&&(uint64_t(lo)&1))))*step);
}
NativeRecordingMask capturedMask(){NativeRecordingMask mask{};mask[0]=0xFF;mask[8]=0xFF;mask[9]=0xE0;return mask;}
constexpr std::array<uint16_t,9> strip={0,1,2,3,0xFFFF,4,5,6,7};
std::array<RigidVertex,8> geometry(float z=.5f,float xSlope=0) {
    std::array<RigidVertex,8> vertices{};
    const std::array<std::array<float,2>,8> xy={{{-1,1},{-1,-1},{0,1},{0,-1},{0,1},{0,-1},{1,1},{1,-1}}};
    for(size_t i=0;i<vertices.size();++i){vertices[i].position={xy[i][0],xy[i][1],z+xSlope*xy[i][0]};
        vertices[i].normal={0,1,0};vertices[i].color={0,1,0,1};vertices[i].uv={.25f,.25f};}
    return vertices;
}
void checkDebug(ID3D11Device* device) {
    ComPtr<ID3D11InfoQueue> queue;if(FAILED(device->QueryInterface(IID_PPV_ARGS(&queue))))return;
    for(UINT64 i=0;i<queue->GetNumStoredMessagesAllowedByRetrievalFilter();++i) {
        SIZE_T count{};hr(queue->GetMessage(i,nullptr,&count),"Debug message size");std::vector<uint8_t> data(count);
        auto* message=reinterpret_cast<D3D11_MESSAGE*>(data.data());hr(queue->GetMessage(i,message,&count),"Debug message");
        if(message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"D3D11: %s\n",message->pDescription);require(false,"Rigid recording emitted a D3D11 error");}
    }
    queue->ClearStoredMessages();
}
struct Fixture {
    NativeBackend backend;NativeMaterialCompiler compiler;MaterialRegistry registry;
    const CompiledMaterial* vs{};const CompiledMaterial* ps{};
    std::shared_ptr<RenderTarget> color,sibling,sourceColor;
    std::shared_ptr<DepthTarget> depth,sourceDepth;
    std::array<std::shared_ptr<DepthTarget>,2> shadows;
    std::shared_ptr<NativeRecordingContext> recording;
    std::shared_ptr<NativeRigidReplayConstants> live;
    RigidVertexConstants vc{};RigidPixelConstants pc{};RigidMeshDraw draw{};
    ComPtr<ID3D11Buffer> sentinelConstants;
    ID3D11DeviceContext* immediate{};
    static constexpr UINT extent=16;
    explicit Fixture(const std::vector<uint8_t>& image,bool hardware,bool textured=false,bool gloss=false,bool multitone=false,bool alpha=false,bool vfx=false,bool uv=false,bool singleUv=false,bool flipbook=false):backend(!hardware),compiler(backend) {
        NativeRecordingProbe::debug(backend,hardware);immediate=NativeRecordingProbe::immediate(backend);
        for(const auto& record:originalMaterialIdentities())if(record.originalAddress==(flipbook?(alpha?0x82039D70u:0x820398BCu):singleUv?(alpha?0x82043BC0u:0x8204364Cu):uv?(alpha?0x82047318u:0x82046D9Cu):vfx?0x8205BE70u:alpha?0x8200D734u:multitone?0x82054F2Cu:gloss?0x8201A07Cu:textured?0x8201700Cu:0x8200D3ACu)||
            record.originalAddress==(flipbook?(alpha?0x8203A644u:0x8203A24Cu):singleUv?(alpha?0x82044850u:0x82044068u):uv?(alpha?0x82047F44u:0x820477A8u):vfx?0x8205C100u:alpha?0x8200E1BCu:multitone?0x82055710u:gloss?0x8201A6ECu:textured?0x82017658u:0x8200D9E8u)) {
            auto id=registry.create(record.originalAddress,std::span<const uint8_t>(image).subspan(record.originalAddress-0x82000000,record.recordBytes));
            const auto& shader=registry.prepareForBind(id,compiler);if(record.stage==MaterialStage::Vertex)vs=&shader;else ps=&shader;
        }
        require(vs&&ps,"Missing exact original rigid pair");
        color=backend.createTarget(extent,extent,TargetFormat::RGB10A2);sibling=backend.createTarget(extent,extent,TargetFormat::RGB10A2);
        depth=backend.createDepthTarget(extent,extent);sourceColor=backend.createTarget(1024,1024,TargetFormat::RGB10A2);
        sourceDepth=backend.createDepthTarget(1024,1024);for(auto& shadow:shadows)shadow=backend.createDepthTarget(1024,1024);
        copyShadows(.75f,.75f);
        for(size_t first:{0u,12u,22u,26u})for(size_t row=0;row<4;++row)vc[first+row][row]=1;
        pc[31][0]=1;pc[36]={0,1,0,0};pc[40][0]=7;
        // These are OUTSIDE captured input mask. Each draw's recorded material
        // must survive even though the live bank deliberately disagrees.
        pc[46][0]=8;pc[47][0]=0;pc[49][2]=999;
        live=backend.createRigidReplayConstants(vc,pc);recording=backend.createRecordingContext();
        draw.primitiveType=6;draw.indexCount=4;draw.viewport={0,0,extent,extent,0x3F800000,0};draw.scissor={0,0,extent,extent};
        draw.depthEnable=draw.depthWrite=1;draw.depthCompare=6;draw.colorMask=15;draw.halfPixelOffset=1;
        draw.primitiveReset=draw.viewportEnable=draw.multisampleAntialias=1;draw.primitiveResetIndex=0xFFFF;draw.multisampleMask=0xFFFFFFFF;
        draw.depthPolicy=ShadowMeshDepthPolicy::Reference20e4Rne;draw.blendWord=0x00010001;draw.expandedBlend=1;
        draw.shadowSamplePolicy=RigidShadowSamplePolicy::ReferenceD24FS8DepthRRRR;draw.shadows=shadows;
        for(auto& s:draw.samplers){s.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
            s.MaxAnisotropy=1;s.MaxLOD=0;s.ComparisonFunc=D3D11_COMPARISON_NEVER;}
        seed();clear();backend.clearTarget(sibling,{0,1,0,1});
    }
    void copyShadows(float world,float character) {
        backend.bindTargets({sourceColor,nullptr,nullptr,nullptr},sourceDepth);
        for(size_t i=0;i<2;++i){backend.clearDepthTarget(sourceDepth,i?character:world,uint8_t(0x40+i));
            auto copy=backend.copyDepth(sourceDepth,shadows[i]);backend.waitCopy(copy);}
        backend.bindTargets({color,nullptr,nullptr,nullptr},depth);
    }
    void seed() {
        auto* device=NativeRecordingProbe::device(backend);
        ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;ComPtr<ID3D11InputLayout> layout;
        hr(device->CreateVertexShader(kVSFlat,sizeof(kVSFlat),nullptr,&vertex),"Sentinel VS");
        hr(device->CreatePixelShader(kPSFlat,sizeof(kPSFlat),nullptr,&pixel),"Sentinel PS");
        D3D11_INPUT_ELEMENT_DESC input{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        hr(device->CreateInputLayout(&input,1,kVSFlat,sizeof(kVSFlat),&layout),"Sentinel declaration");
        immediate->VSSetShader(vertex.Get(),nullptr,0);immediate->PSSetShader(pixel.Get(),nullptr,0);immediate->IASetInputLayout(layout.Get());
        const std::array<uint32_t,32> data={0x3F800000,0x3F000000,0x3E800000,0x3F800000};
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(data);bd.BindFlags=D3D11_BIND_VERTEX_BUFFER|D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{data.data(),0,0};ComPtr<ID3D11Buffer> vb;
        hr(device->CreateBuffer(&bd,&initial,&vb),"Sentinel IA buffer");
        std::array<ID3D11Buffer*,4> streams{};streams.fill(vb.Get());const UINT strides[]={8,12,16,20},offsets[]={0,4,8,12};
        immediate->IASetVertexBuffers(0,4,streams.data(),strides,offsets);immediate->IASetIndexBuffer(vb.Get(),DXGI_FORMAT_R32_UINT,4);
        immediate->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;hr(device->CreateBuffer(&bd,&initial,&sentinelConstants),"Sentinel constants");
        std::array<ID3D11Buffer*,14> constants{};constants.fill(sentinelConstants.Get());
        immediate->VSSetConstantBuffers(0,14,constants.data());immediate->PSSetConstantBuffers(0,14,constants.data());
        immediate->GSSetConstantBuffers(0,14,constants.data());immediate->CSSetConstantBuffers(0,14,constants.data());
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_WIREFRAME;rd.CullMode=D3D11_CULL_FRONT;rd.DepthClipEnable=TRUE;
        ComPtr<ID3D11RasterizerState> raster;hr(device->CreateRasterizerState(&rd,&raster),"Sentinel rasterizer");immediate->RSSetState(raster.Get());
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=TRUE;dd.DepthFunc=D3D11_COMPARISON_NEVER;
        dd.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};dd.BackFace=dd.FrontFace;
        ComPtr<ID3D11DepthStencilState> ds;hr(device->CreateDepthStencilState(&dd,&ds),"Sentinel depth state");immediate->OMSetDepthStencilState(ds.Get(),0x73);
        const FLOAT factors[]={.125f,.25f,.5f,1};immediate->OMSetBlendState(nullptr,factors,0x13579BDF);
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        sd.MaxAnisotropy=1;sd.MaxLOD=7;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;ComPtr<ID3D11SamplerState> sampler;
        hr(device->CreateSamplerState(&sd,&sampler),"Sentinel sampler");std::array<ID3D11SamplerState*,16> samplers{};samplers.fill(sampler.Get());
        immediate->VSSetSamplers(0,16,samplers.data());immediate->PSSetSamplers(0,16,samplers.data());
        // Seed an unrelated real SRV in every stage0/1 as well as distant slots.
        const std::array<uint8_t,4> rgba={23,67,89,255};auto texture=backend.createTexture(1,1,TextureFormat::RGBA8,rgba);
        backend.bindEngineTexture(0,texture);ComPtr<ID3D11ShaderResourceView> srv;immediate->PSGetShaderResources(0,1,&srv);
        std::array<ID3D11ShaderResourceView*,128> srvs{};srvs.fill(srv.Get());immediate->PSSetShaderResources(0,128,srvs.data());
        immediate->VSSetShaderResources(0,128,srvs.data());
        const D3D11_VIEWPORT vp[]={ {1,2,7,8,.25f,.75f},{2,1,9,10,0,1} };immediate->RSSetViewports(2,vp);
        const D3D11_RECT sc[]={ {2,3,11,13},{0,1,5,7} };immediate->RSSetScissorRects(2,sc);
    }
    std::vector<uint8_t> sentinelBytes() {
        auto* device=NativeRecordingProbe::device(backend);D3D11_BUFFER_DESC d{};sentinelConstants->GetDesc(&d);const UINT bytes=d.ByteWidth;
        d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Buffer> copy;
        hr(device->CreateBuffer(&d,nullptr,&copy),"Sentinel readback allocation");immediate->CopyResource(copy.Get(),sentinelConstants.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};hr(immediate->Map(copy.Get(),0,D3D11_MAP_READ,0,&mapped),"Sentinel readback");
        std::vector<uint8_t> out(bytes);std::memcpy(out.data(),mapped.pData,bytes);immediate->Unmap(copy.Get(),0);return out;
    }
    void clear(float z=0){backend.clearTarget(color,{1,0,1,1});backend.clearDepthTarget(depth,z,0x6D);}
    RigidPixelConstants material(float id) const {auto result=pc;result[31][0]=0;result[46][0]=0;result[47][0]=1;result[49][2]=id;return result;}
    std::shared_ptr<NativeRecordingPayload> begin(const NativeRecordingMask& mask=capturedMask()) {
        auto payload=backend.allocateRecordingPayload(recording,0x3000);backend.beginRecordingPayload(payload,4,mask,mask);return payload;
    }
    void record(const std::shared_ptr<NativeRecordingPayload>& payload,const std::shared_ptr<NativeRigidMesh>& mesh,
                const RigidVertexConstants& vertices,const RigidPixelConstants& pixels,const RigidMeshDraw& state) {
        backend.bindRigidShadowDepth(payload,0,shadows[0]);backend.bindRigidShadowDepth(payload,1,shadows[1]);
        backend.bindRigidMeshVertices(payload,mesh);backend.bindRigidMeshDeclaration(payload,mesh);backend.bindRigidMeshIndices(payload,mesh);
        backend.recordRigidMesh(payload,color,depth,mesh,*vs,*ps,vertices,pixels,live,state);
    }
    void execute(const std::shared_ptr<NativeRecordingPayload>& payload) {
        const Snapshot before(immediate);const auto constantBytes=sentinelBytes();
        const auto siblingBytes=backend.readbackTarget(sibling),sourceBytes=backend.readbackDepthTarget(sourceDepth);
        const auto worldBytes=backend.readbackDepthTarget(shadows[0]),characterBytes=backend.readbackDepthTarget(shadows[1]);
        const auto old=backend.recordingPayloadReceipt(payload);
        const auto count=backend.recordingExecutedDrawCount();
        backend.executeRecordingPayload(payload);backend.waitIdle();
        const auto now=backend.recordingPayloadReceipt(payload);
        require(now.executions==old.executions+1&&now.executedDraws==old.executedDraws+old.recordedDraws&&
                backend.recordingExecutedDrawCount()==count+old.recordedDraws,"Recording execution accounting differs from actual list draws");
        require(Snapshot(immediate)==before,"ExecuteCommandList changed immediate pipeline state");
        require(sentinelBytes()==constantBytes,"Replay preparation overwrote immediate constant storage");
        require(backend.readbackTarget(sibling)==siblingBytes,"Rigid execution changed a sibling color target");
        require(backend.readbackDepthTarget(sourceDepth)==sourceBytes,"Rigid execution changed the copy source");
        require(backend.readbackDepthTarget(shadows[0])==worldBytes&&backend.readbackDepthTarget(shadows[1])==characterBytes,"Rigid sampling changed copied shadow depth/stencil");
    }
    template<class Color,class Depth>void pixels(Color expectedColor,Depth expectedDepth) {
        const auto colors=backend.readbackTarget(color),depths=backend.readbackDepthTarget(depth);
        for(UINT y=0;y<extent;++y)for(UINT x=0;x<extent;++x) {
            const size_t i=y*extent+x;const uint32_t colorWord=word(colors,4*i),depthWord=word(depths,8*i);
            if(colorWord!=expectedColor(x,y)||depthWord!=bits(expectedDepth(x,y))||depths[8*i+4]!=0x6D) {
                char text[240];std::snprintf(text,sizeof(text),"Rigid pixel(%u,%u): color%08X expected%08X depth%08X expected%08X stencil%02X",x,y,
                    colorWord,expectedColor(x,y),depthWord,bits(expectedDepth(x,y)),depths[8*i+4]);throw Error(text);
            }
            rigidChecks+=3;
        }
    }
    template<class F>void rejected(F action,const char* fragment) {
        const Snapshot before(immediate);const auto colorBefore=backend.readbackTarget(color),depthBefore=backend.readbackDepthTarget(depth);
        const auto recorded=backend.recordingDrawCount(),executed=backend.recordingExecutedDrawCount(),direct=backend.rigidMeshDrawCount();bool caught=false;
        try{action();}catch(const Error& error){caught=true;if(std::string(error.what()).find(fragment)==std::string::npos){
            std::fprintf(stderr,"Expected diagnostic containing '%s', actual '%s'\n",fragment,error.what());require(false,"Unexpected rigid rejection diagnostic");}}
        require(caught,"Invalid rigid operation succeeded");require(Snapshot(immediate)==before,"Rigid rejection changed immediate state");
        require(backend.readbackTarget(color)==colorBefore&&backend.readbackDepthTarget(depth)==depthBefore,"Rigid rejection changed output pixels");
        require(backend.recordingDrawCount()==recorded&&backend.recordingExecutedDrawCount()==executed&&backend.rigidMeshDrawCount()==direct,"Rigid rejection changed draw counters");
    }
};
constexpr uint32_t untouched=0xFFF003FFu;
void stripBoundary(const std::vector<uint8_t>& image,bool hardware) {
    Fixture f(image,hardware);const auto vertices=Test::stripBoundaryVertices(geometry());const auto words=Test::stripBoundaryIndices();
    const auto mesh=f.backend.uploadRigidMesh(vertices,words);auto selected=f.draw;
    selected.startIndex=Test::stripBoundaryStart;selected.indexCount=Test::stripBoundaryCount;
    f.backend.bindTargets({f.color,nullptr,nullptr,nullptr},f.depth);f.backend.bindRigidShaders(*f.vs,*f.ps);
    for(uint32_t slot=0;slot<2;++slot)f.backend.bindRigidShadowDepth(slot,f.shadows[slot]);
    f.backend.bindRigidMeshVertices(mesh);f.backend.bindRigidMeshDeclaration(mesh);f.backend.bindRigidMeshIndices(mesh);
    auto values=f.material(317);values[31][0]=1;const auto commit=f.backend.commitRigid(*f.vs,*f.ps,f.vc,values);
    observeMeshGpuRetirement(f.immediate,mesh,"rigid_strip_boundary");
    const auto direct=[&](const RigidMeshDraw& d) {
        const Snapshot before(f.immediate);const auto count=f.backend.rigidMeshDrawCount();
        f.backend.drawRigidMesh(f.color,f.depth,mesh,*f.vs,*f.ps,commit,d);f.backend.waitIdle();
        require(Snapshot(f.immediate)==before&&f.backend.rigidMeshDrawCount()==count+1,
                "Rigid split changed bindings or logical draw accounting");
    };
    for(uint32_t cull:{2u,6u}) {
        selected.cull=cull;f.clear();direct(selected);
        const auto covered=[&](UINT x,UINT y){return Test::originalStripBoundaryCovered(x,y,cull);};
        f.pixels([&](UINT x,UINT y){return covered(x,y)?(317u|(528u<<10)|(1u<<30)):untouched;},
                 [&](UINT x,UINT y){return covered(x,y)?.5f:0.f;});
        const auto color=f.backend.readbackTarget(f.color),depth=f.backend.readbackDepthTarget(f.depth);f.clear();
        for(const auto& packet:Test::originalStripBoundaryPackets) {
            auto part=selected;part.indexCount=packet[0];part.startIndex=packet[1];direct(part);
        }
        require(f.backend.readbackTarget(f.color)==color&&f.backend.readbackDepthTarget(f.depth)==depth,
                "Rigid full draw differs from independent original packet pixels");
        const Snapshot before(f.immediate);auto payload=f.begin();f.record(payload,mesh,f.vc,f.material(317),selected);
        f.backend.finishRecordingPayload(payload);
        require(Snapshot(f.immediate)==before&&f.backend.readbackTarget(f.color)==color&&
                f.backend.readbackDepthTarget(f.depth)==depth&&f.backend.recordingPayloadReceipt(payload).recordedDraws==1,
                "Rigid split recording emitted immediate work or changed logical receipt");
        f.clear();f.execute(payload);
        require(f.backend.readbackTarget(f.color)==color&&f.backend.readbackDepthTarget(f.depth)==depth,
                "Rigid split direct and deferred pixels differ");
        f.backend.releaseRecordingPayload(payload);f.rejected([&]{f.backend.executeRecordingPayload(payload);},"released");
    }
    auto empty=f.begin();
    for(const auto [start,count,base]:{std::array<int64_t,3>{1,65538,0},{UINT32_MAX,65536,0},{1,65536,-1},{1,65536,1}}) {
        auto bad=selected;bad.startIndex=uint32_t(start);bad.indexCount=uint32_t(count);bad.baseVertex=int32_t(base);
        f.rejected([&]{f.backend.drawRigidMesh(f.color,f.depth,mesh,*f.vs,*f.ps,commit,bad);},"effective index range");
        f.rejected([&]{f.backend.recordRigidMesh(empty,f.color,f.depth,mesh,*f.vs,*f.ps,f.vc,f.material(317),f.live,bad);},"effective index range");
        require(!f.backend.recordingPayloadReceipt(empty).recordedDraws,"Malformed rigid split recorded a prefix");
    }
    f.backend.releaseRecordingPayload(empty);f.backend.releaseRigidReplayConstants(f.live);f.backend.releaseRecordingContext(f.recording);
    checkDebug(NativeRecordingProbe::device(f.backend));
    std::printf("AUDIT_GPU_STRIP_BOUNDARY family=rigid vertex=8200D3AC pixel=8200D9E8 count=65536 reset_position=65530 selected_start=1 sdk_packets=65534_4 sdk_advance=65532 cull=2_6 create=passed direct_pixels=passed original_packet_pixels=passed recorded_pixels=passed record_without_immediate_work=passed malformed=passed payload_release=passed replay_owner_release=passed stale_use=passed gpu_buffer_retirement=separate\n");
}
void largeOwner(const std::vector<uint8_t>& image,bool hardware) {
    Fixture f(image,hardware);const auto full=geometry();
    const std::span<const RigidVertex> quad(full.data(),4);
    constexpr std::array<uint16_t,7> words{0,1,2,UINT16_MAX,2,1,3};
    const auto baselineMesh=f.backend.uploadRigidMesh(quad,words);auto selected=f.draw;selected.indexCount=7;
    auto baseline=f.begin();f.record(baseline,baselineMesh,f.vc,f.material(317),selected);f.backend.finishRecordingPayload(baseline);
    f.clear();f.execute(baseline);
    f.pixels([](UINT x,UINT){return x<8?(317u|(528u<<10)|(1u<<30)):untouched;},[](UINT x,UINT){return x<8?.5f:0.f;});
    const auto color=f.backend.readbackTarget(f.color),depth=f.backend.readbackDepthTarget(f.depth);
    std::vector<RigidVertex> owned(65536,full[0]);std::copy(quad.begin(),quad.end(),owned.end()-4);
    const auto owner=f.backend.uploadRigidMesh(owned,words);selected.baseVertex=65532;
    const auto copied=f.backend.readbackRigidMeshVertices(owner);
    require(owner->vertexCount()==65536&&copied.size()==owned.size()&&
        !std::memcmp(copied.data(),owned.data(),owned.size()*sizeof(RigidVertex)),"Large rigid GPU owner lost vertex bytes");
    auto recorded=f.begin();f.record(recorded,owner,f.vc,f.material(317),selected);f.backend.finishRecordingPayload(recorded);
    f.clear();f.execute(recorded);
    require(f.backend.readbackTarget(f.color)==color&&f.backend.readbackDepthTarget(f.depth)==depth,
        "Large rigid selected range changed recorded pixels/depth/stencil");
    f.backend.bindTargets({f.color,nullptr,nullptr,nullptr},f.depth);f.backend.bindRigidShaders(*f.vs,*f.ps);
    for(uint32_t slot=0;slot<2;++slot)f.backend.bindRigidShadowDepth(slot,f.shadows[slot]);
    f.backend.bindRigidMeshVertices(owner);f.backend.bindRigidMeshDeclaration(owner);f.backend.bindRigidMeshIndices(owner);
    auto values=f.material(317);values[31][0]=1;
    const auto commit=f.backend.commitRigid(*f.vs,*f.ps,f.vc,values);
    observeMeshGpuRetirement(f.immediate,owner,"rigid_large_owner");f.clear();const Snapshot before(f.immediate);
    f.backend.drawRigidMesh(f.color,f.depth,owner,*f.vs,*f.ps,commit,selected);f.backend.waitIdle();
    require(Snapshot(f.immediate)==before&&f.backend.readbackTarget(f.color)==color&&f.backend.readbackDepthTarget(f.depth)==depth,
        "Large rigid selected range changed direct pixels or retained bindings");
    auto invalid=selected;invalid.baseVertex=65533;auto empty=f.begin();
    f.rejected([&]{f.backend.drawRigidMesh(f.color,f.depth,owner,*f.vs,*f.ps,commit,invalid);},"effective index range");
    f.rejected([&]{f.backend.recordRigidMesh(empty,f.color,f.depth,owner,*f.vs,*f.ps,f.vc,f.material(317),f.live,invalid);},"effective index range");
    require(!f.backend.recordingPayloadReceipt(empty).recordedDraws,"Rejected large rigid draw published a recording receipt");
    f.backend.releaseRecordingPayload(empty);f.backend.releaseRecordingPayload(recorded);f.backend.releaseRecordingPayload(baseline);
    f.rejected([&]{f.backend.executeRecordingPayload(recorded);},"released");
    f.backend.releaseRigidReplayConstants(f.live);f.backend.releaseRecordingContext(f.recording);
    checkDebug(NativeRecordingProbe::device(f.backend));
    std::printf("AUDIT_GPU_LARGE_OWNER family=rigid vertices=65536 base_vertex=65532 create=passed direct_draw=passed recorded_draw=passed payload_release=passed stale_use=passed malformed=passed gpu_buffer_retirement=separate\n");
}
void singleAnimatedUv(const std::vector<uint8_t>& image,bool hardware) {
    for(bool alpha:{false,true}) {
        Fixture f(image,hardware,false,false,false,alpha,false,false,true);
        auto raw=geometry();for(auto& v:raw)v.uv1={.875f,-17.f}; // Not fetched by either original VS.
        auto mesh=f.backend.uploadRigidMesh(raw,strip);auto d=f.draw;d.indexCount=9;
        if(alpha) {
            d.shadows={};d.samplers={};d.shadowSamplePolicy=RigidShadowSamplePolicy::NotUsed;
            d.blendEnable=1;d.blendWord=0x07060706;
        }
        const std::array<uint8_t,4> texel={192,48,32,192};
        d.baseTexture=f.backend.createTexture(1,1,TextureFormat::RGBA8,texel);
        d.baseSampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        d.baseSampler.AddressU=d.baseSampler.AddressV=d.baseSampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        d.baseSampler.MaxAnisotropy=1;d.baseSampler.MaxLOD=13;d.baseSampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
        auto vc=f.vc;vc[45]={1,1,0,0};vc[46]={0,0,0,0};vc[47]={0,0,0,0};
        auto pc=f.pc;pc[40]={0,0,0,1};pc[43]={0,0,0,0};pc[44]={0,0,0,0};pc[48]={0,0,0,0};pc[49]={-1,0,0,0};
        f.backend.bindRigidShaders(*f.vs,*f.ps);
        f.backend.bindRigidMeshVertices(mesh);f.backend.bindRigidMeshDeclaration(mesh);f.backend.bindRigidMeshIndices(mesh);
        if(!alpha)for(uint32_t i=0;i<2;++i)f.backend.bindRigidShadowDepth(i,d.shadows[i]);
        f.backend.bindEngineTexture(alpha?0u:2u,d.baseTexture);
        auto commit=f.backend.commitRigid(*f.vs,*f.ps,vc,pc);
        const auto draw=[&](const RigidMeshDraw& state){f.backend.drawRigidMesh(f.color,f.depth,mesh,*f.vs,*f.ps,commit,state);};
        f.rejected([&]{auto bad=d;bad.baseTexture.reset();draw(bad);},"base texture");
        if(alpha)f.rejected([&]{auto bad=d;bad.shadows[0]=f.shadows[0];draw(bad);},"depth sampling policy");
        else f.rejected([&]{auto bad=d;bad.shadows[1]=bad.shadows[0];draw(bad);},"distinct");
        f.rejected([&]{auto bad=d;bad.uvTextures[0]=d.baseTexture;draw(bad);},"UV");
        f.clear();const auto cleared=f.backend.readbackTarget(f.color);const Snapshot before(f.immediate);draw(d);
        require(Snapshot(f.immediate)==before,"Single UV direct draw changed retained bindings");
        const auto direct=f.backend.readbackTarget(f.color),directDepth=f.backend.readbackDepthTarget(f.depth);
        require(direct!=cleared,"Single UV draw wrote no pixels");
        auto payload=f.begin(NativeRecordingMask{});
        f.backend.recordRigidMesh(payload,f.color,f.depth,mesh,*f.vs,*f.ps,vc,pc,f.live,d);
        f.backend.finishRecordingPayload(payload);f.clear();f.execute(payload);
        require(f.backend.readbackTarget(f.color)==direct&&f.backend.readbackDepthTarget(f.depth)==directDepth,
            "Single UV recorded draw differs from immediate pixels/depth/stencil");
        f.backend.releaseRecordingPayload(payload);f.backend.releaseRigidReplayConstants(f.live);
        f.backend.releaseRecordingContext(f.recording);checkDebug(NativeRecordingProbe::device(f.backend));
    }
}
void flipbookVariants(const std::vector<uint8_t>& image,bool hardware) {
    for(bool alpha:{false,true}) {
        Fixture f(image,hardware,false,false,false,alpha,false,false,false,true);
        auto raw=geometry();for(auto& v:raw)v.uv1={.875f,-17.f}; // Not fetched by either original VS.
        auto mesh=f.backend.uploadRigidMesh(raw,strip);auto d=f.draw;d.indexCount=9;
        d.shadows={};d.samplers={};d.shadowSamplePolicy=RigidShadowSamplePolicy::NotUsed;
        if(alpha) {
            d.blendEnable=1;d.blendWord=0x07060706;
        }
        const std::array<uint8_t,4> texel={192,48,32,192};
        d.baseTexture=f.backend.createTexture(1,1,TextureFormat::RGBA8,texel);
        d.baseSampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        d.baseSampler.AddressU=d.baseSampler.AddressV=d.baseSampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        d.baseSampler.MaxAnisotropy=1;d.baseSampler.MaxLOD=13;d.baseSampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
        auto vc=f.vc;vc[22]={.125f,.125f,.125f,.125f};vc[47]={1,1,1,1};
        auto pc=f.pc;pc[40]={0,0,0,1};pc[46]={0,0,0,0};pc[48]={0,0,0,0};pc[49]={-1,0,0,0};
        f.backend.bindRigidShaders(*f.vs,*f.ps);
        f.backend.bindRigidMeshVertices(mesh);f.backend.bindRigidMeshDeclaration(mesh);f.backend.bindRigidMeshIndices(mesh);
        f.backend.bindEngineTexture(0,d.baseTexture);
        auto commit=f.backend.commitRigid(*f.vs,*f.ps,vc,pc);
        const auto draw=[&](const RigidMeshDraw& state){f.backend.drawRigidMesh(f.color,f.depth,mesh,*f.vs,*f.ps,commit,state);};
        f.rejected([&]{auto bad=d;bad.baseTexture.reset();draw(bad);},"base texture");
        f.rejected([&]{auto bad=d;bad.shadows[0]=f.shadows[0];draw(bad);},"depth sampling policy");
        f.rejected([&]{auto bad=d;bad.uvTextures[0]=d.baseTexture;draw(bad);},"UV");
        f.clear();const auto cleared=f.backend.readbackTarget(f.color);const Snapshot before(f.immediate);draw(d);
        require(Snapshot(f.immediate)==before,"Flipbook direct draw changed retained bindings");
        const auto direct=f.backend.readbackTarget(f.color),directDepth=f.backend.readbackDepthTarget(f.depth);
        require(direct!=cleared,"Flipbook draw wrote no pixels");
        auto payload=f.begin(NativeRecordingMask{});
        f.backend.recordRigidMesh(payload,f.color,f.depth,mesh,*f.vs,*f.ps,vc,pc,f.live,d);
        f.backend.finishRecordingPayload(payload);f.clear();f.execute(payload);
        require(f.backend.readbackTarget(f.color)==direct&&f.backend.readbackDepthTarget(f.depth)==directDepth,
            "Flipbook recorded draw differs from immediate pixels/depth/stencil");
        f.backend.releaseRecordingPayload(payload);f.backend.releaseRigidReplayConstants(f.live);
        f.backend.releaseRecordingContext(f.recording);checkDebug(NativeRecordingProbe::device(f.backend));
    }
}
void animatedUv(const std::vector<uint8_t>& image,bool hardware) {
    for(bool alpha:{false,true}) {
        Fixture f(image,hardware,false,false,false,alpha,false,true);
        auto raw=geometry();for(auto& v:raw)v.uv1={.75f,.25f};
        auto mesh=f.backend.uploadRigidMesh(raw,strip);auto d=f.draw;d.indexCount=9;
        d.shadows={};d.samplers={};
        if(alpha) {
            d.shadowSamplePolicy=RigidShadowSamplePolicy::NotUsed;
            d.blendEnable=1;d.blendWord=0x07060706;
        } else {d.shadows[0]=f.shadows[1];d.samplers[0]=f.draw.samplers[1];}
        const std::array<uint8_t,4> a={192,48,32,128},b={32,160,224,192};
        d.uvTextures[0]=f.backend.createTexture(1,1,TextureFormat::RGBA8,a);
        d.uvTextures[1]=f.backend.createTexture(1,1,TextureFormat::RGBA8,b);
        for(auto& s:d.uvSamplers) {
            s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
            s.MaxAnisotropy=1;s.MaxLOD=13;s.ComparisonFunc=D3D11_COMPARISON_NEVER;
        }
        auto vc=f.vc;vc[22]={.125f,.125f,.125f,.125f};vc[43]={1,1,1,1};vc[46]={1,1,1,1};
        auto pc=f.pc;pc[40]={0,0,0,.75f};pc[42]={0,0,0,0};pc[48]={0,0,0,0};pc[49]={-1,0,0,0};
        f.backend.bindRigidShaders(*f.vs,*f.ps);
        f.backend.bindRigidMeshVertices(mesh);f.backend.bindRigidMeshDeclaration(mesh);f.backend.bindRigidMeshIndices(mesh);
        if(!alpha)f.backend.bindRigidShadowDepth(0,d.shadows[0]);
        for(uint32_t i=0;i<2;++i)f.backend.bindEngineTexture(i+(alpha?0:1),d.uvTextures[i]);
        auto commit=f.backend.commitRigid(*f.vs,*f.ps,vc,pc);
        const auto draw=[&](const RigidMeshDraw& state){f.backend.drawRigidMesh(f.color,f.depth,mesh,*f.vs,*f.ps,commit,state);};
        f.rejected([&]{auto bad=d;bad.uvTextures[1].reset();draw(bad);},"UV");
        f.rejected([&]{auto bad=d;bad.shadows[1]=f.shadows[0];draw(bad);},alpha?"depth sampling policy":"shadow owner");
        f.clear();const auto cleared=f.backend.readbackTarget(f.color);const Snapshot before(f.immediate);draw(d);
        require(Snapshot(f.immediate)==before,"UV direct draw changed retained bindings");
        const auto direct=f.backend.readbackTarget(f.color),directDepth=f.backend.readbackDepthTarget(f.depth);
        require(direct!=cleared,"UV draw wrote no pixels");
        auto payload=f.begin(NativeRecordingMask{});
        f.backend.recordRigidMesh(payload,f.color,f.depth,mesh,*f.vs,*f.ps,vc,pc,f.live,d);
        f.backend.finishRecordingPayload(payload);f.clear();f.execute(payload);
        require(f.backend.readbackTarget(f.color)==direct&&f.backend.readbackDepthTarget(f.depth)==directDepth,
            "UV recorded draw differs from immediate pixels/depth/stencil");
        f.backend.releaseRecordingPayload(payload);
        f.backend.releaseRigidReplayConstants(f.live);f.backend.releaseRecordingContext(f.recording);
        checkDebug(NativeRecordingProbe::device(f.backend));
    }
}
void rigidAlpha(const std::vector<uint8_t>& image,bool hardware) {
    const EffectRecord effect(0x8200CCB8,std::span<const uint8_t>(image).subspan(0xCCB8,12000));
    for(uint32_t handle:{0x001C000Du,0x0020000Fu}) {
        const auto front=effectPassBinding(effect,0x0003FFFC,handle),alpha=effectPassBinding(effect,0x0007FFFC,handle);
        require(front.usage==0x80&&front.lanes[1]&&alpha.usage==0&&!alpha.lanes[0]&&!alpha.lanes[1],
                "Selected alpha pass inherited front-pass shadow usage");
    }
    const auto base=effectPassBinding(effect,0x0007FFFC,0x00500020),eye=effectPassBinding(effect,0x0007FFFC,0x00080003);
    require(base.usage==0x80&&!base.lanes[0]&&base.lanes[1]&&base.lanes[1]->start==0&&base.lanes[1]->count==1,
            "Original alpha base texture mapping differs");
    require(eye.usage==2&&!eye.lanes[0]&&eye.lanes[1]&&eye.lanes[1]->start==4&&eye.lanes[1]->count==1,
            "Original alpha eye mapping differs");
    bool rejected=false;try{(void)effectPassBinding(effect,0xBFFFC,0x00080003);}catch(const EffectError&){rejected=true;}
    require(rejected,"Unknown original technique was accepted");
    Fixture f(image,hardware,false,false,false,true);
    auto raw=geometry();for(auto& v:raw)v.normal={0,0,0};
    auto mesh=f.backend.uploadRigidMesh(raw,strip);
    auto d=f.draw;d.indexCount=9;d.shadows={};d.samplers={};d.shadowSamplePolicy=RigidShadowSamplePolicy::NotUsed;
    d.blendEnable=1;d.blendWord=0x07060706;d.expandedBlend=1;
    d.baseSampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    d.baseSampler.AddressU=d.baseSampler.AddressV=d.baseSampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    d.baseSampler.MaxAnisotropy=1;d.baseSampler.ComparisonFunc=D3D11_COMPARISON_NEVER;d.baseSampler.MaxLOD=13;
    const std::array<uint8_t,4> rgba={64,128,191,7};
    d.baseTexture=f.backend.createTexture(1,1,TextureFormat::RGBA8,rgba);
    auto pc=f.pc;pc[4]={0,0,3,0};pc[40][3]=.5f;pc[49]={-.25f,.375f,91,37};
    f.backend.bindRigidShaders(*f.vs,*f.ps);
    f.backend.bindRigidMeshVertices(mesh);f.backend.bindRigidMeshDeclaration(mesh);f.backend.bindRigidMeshIndices(mesh);
    f.backend.bindEngineTexture(0,d.baseTexture);
    auto commit=f.backend.commitRigid(*f.vs,*f.ps,f.vc,pc);
    const auto draw=[&](const RigidMeshDraw& state){f.backend.drawRigidMesh(f.color,f.depth,mesh,*f.vs,*f.ps,commit,state);};
    f.rejected([&]{auto bad=d;bad.baseTexture.reset();draw(bad);},"base texture");
    f.rejected([&]{auto bad=d;bad.shadows[0]=f.shadows[0];draw(bad);},"sampling policy");
    f.rejected([&]{auto bad=d;bad.shadowSamplePolicy=RigidShadowSamplePolicy::ReferenceD24FS8DepthRRRR;draw(bad);},"sampling policy");
    f.rejected([&]{auto bad=d;bad.baseSampler.AddressU=D3D11_TEXTURE_ADDRESS_CLAMP;draw(bad);},"linear/wrap");
    const auto verify=[&](bool silhouette) {
        const auto color=f.backend.readbackTarget(f.color),depth=f.backend.readbackDepthTarget(f.depth);
        for(size_t i=0;i<256;++i) {
            const auto packed=word(color,4*i);
            for(size_t lane=0;lane<3;++lane){
                const double source=silhouette?.375:double(rgba[lane])/255;
                const double opacity=silhouette?1:.5;
                const double blended=source*opacity+(lane==1?0:1)*(1-opacity);
                require(std::abs(int((packed>>(10*lane))&1023)-int(std::lround(blended*1023)))<=1,"Rigid alpha base slot/color blend differs");
            }
            require((packed>>30)==(silhouette?3u:2u),"Rigid alpha opacity blend differs");
            require(word(depth,8*i)==bits(.5f)&&depth[8*i+4]==0x6D,"Rigid alpha depth/stencil differs");
        }
    };
    for(bool silhouette:{false,true}) {
        pc[49][0]=silhouette?0.f:-.25f;
        commit=f.backend.commitRigid(*f.vs,*f.ps,f.vc,pc);
        f.clear();const Snapshot before(f.immediate);const auto sibling=f.backend.readbackTarget(f.sibling);
        draw(d);verify(silhouette);
        require(Snapshot(f.immediate)==before,"Rigid alpha direct state was not restored");
        require(f.backend.readbackTarget(f.sibling)==sibling,"Rigid alpha changed sibling output");
        const auto direct=f.backend.readbackTarget(f.color);
        auto payload=f.begin(NativeRecordingMask{});
        f.backend.recordRigidMesh(payload,f.color,f.depth,mesh,*f.vs,*f.ps,f.vc,pc,f.live,d);
        f.backend.finishRecordingPayload(payload);f.clear();f.execute(payload);verify(silhouette);
        require(f.backend.readbackTarget(f.color)==direct,"Rigid alpha immediate/recorded output differs");
        f.backend.releaseRecordingPayload(payload);
    }
    f.backend.releaseRigidReplayConstants(f.live);f.backend.releaseRecordingContext(f.recording);
    checkDebug(NativeRecordingProbe::device(f.backend));
    std::printf("PASS rigid alpha mesh %s: base slot0, alpha blend, depth/stencil, immediate/recorded parity and restoration\n",hardware?"hardware":"WARP");
}

void vfxRigid(const std::vector<uint8_t>& image,bool hardware) {
    const EffectRecord effect(0x8205B848,std::span<const uint8_t>(image).subspan(0x5B848,0x1A90));
    const auto passes=effect.techniques();
    require(passes.size()==1&&passes.front().scalars.empty()&&passes.front().samplers.size()==6,
            "Original VFX rigid pass state differs");
    for(auto handle:{0x001C000Du,0x0020000Fu,0x00240011u}) {
        const auto b=effectPassBinding(effect,0x0003FFFC,handle);
        require(!b.usage&&!b.lanes[0]&&!b.lanes[1],"VFX rigid unexpectedly samples shadows");
    }
    const auto b=effectPassBinding(effect,0x0003FFFC,0x00500020);
    require(b.usage==0x80&&!b.lanes[0]&&b.lanes[1]&&b.lanes[1]->start==0&&b.lanes[1]->count==1,
            "VFX rigid base sampler is not slot zero");
    Fixture f(image,hardware,false,false,false,false,true);
    auto raw=geometry();for(auto& v:raw)v.color={.125f,.875f,.25f,.5f};
    auto mesh=f.backend.uploadRigidMesh(raw,strip);
    auto d=f.draw;d.indexCount=9;d.shadows={};d.samplers={};d.shadowSamplePolicy=RigidShadowSamplePolicy::NotUsed;
    d.baseSampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    d.baseSampler.AddressU=d.baseSampler.AddressV=d.baseSampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    d.baseSampler.MaxAnisotropy=1;d.baseSampler.ComparisonFunc=D3D11_COMPARISON_NEVER;d.baseSampler.MaxLOD=13;
    // A two-texel source makes the original UV transform observable.
    const std::array<uint8_t,8> rgba={64,128,192,128,192,64,128,255};
    d.baseTexture=f.backend.createTexture(2,1,TextureFormat::RGBA8,rgba);
    auto vc=f.vc;auto pc=f.pc;pc[16]={.75f,.5f,.25f,.8f};
    vc[5]={1,0,0,0};vc[6]={0,1,0,0};
    f.backend.bindRigidShaders(*f.vs,*f.ps);
    f.backend.bindRigidMeshVertices(mesh);f.backend.bindVfxRigidMeshDeclaration(mesh);f.backend.bindRigidMeshIndices(mesh);
    f.backend.bindEngineTexture(0,d.baseTexture);
    for(unsigned variant=0;variant<5;++variant) {
        d.blendEnable=variant>=3?1:0;d.blendWord=variant>=3?0x07060706:0x00010001;
        d.expandedBlend=variant==4?0:1;
        vc[5][2]=variant==1?.5f:0.f;pc[16][3]=variant==2?0.f:.8f;
        const auto commit=f.backend.commitRigid(*f.vs,*f.ps,vc,pc);
        f.clear();const Snapshot before(f.immediate);
        f.backend.drawRigidMesh(f.color,f.depth,mesh,*f.vs,*f.ps,commit,d);
        require(Snapshot(f.immediate)==before,"VFX rigid draw changed retained state");
        const auto color=f.backend.readbackTarget(f.color),depth=f.backend.readbackDepthTarget(f.depth);
        const auto texel=variant==1?4u:0u;
        for(size_t i=0;i<256;++i) {
            const auto packed=word(color,4*i);
            for(size_t lane=0;lane<3;++lane) {
                const double opacity=double(rgba[texel+3])/255*.5*pc[16][3];
                const double source=double(rgba[texel+lane])/255*pc[16][lane];
                const double expected=variant>=3?source*opacity+(lane==1?0:1)*(1-opacity):source;
                require(std::abs(int((packed>>(10*lane))&1023)-int(std::lround(expected*1023)))<=1,
                        "VFX rigid texture/tint RGB differs (vertex RGB must not multiply the result)");
            }
            const double opacity=double(rgba[texel+3])/255*.5*pc[16][3];
            const auto expectedAlpha=unsigned(std::lround((variant>=3?opacity*opacity+1-opacity:opacity)*3));
            require((packed>>30)==expectedAlpha,"VFX rigid texture/vertex/tint alpha differs");
            require(word(depth,8*i)==bits(.5f)&&depth[8*i+4]==0x6D,"VFX rigid depth/stencil differs");
        }
        auto payload=f.begin(NativeRecordingMask{});
        f.backend.recordRigidMesh(payload,f.color,f.depth,mesh,*f.vs,*f.ps,vc,pc,f.live,d);
        f.backend.finishRecordingPayload(payload);f.clear();f.execute(payload);
        require(f.backend.readbackTarget(f.color)==color&&f.backend.readbackDepthTarget(f.depth)==depth,
                "VFX rigid immediate/recorded output differs");
        f.backend.releaseRecordingPayload(payload);
    }
    f.backend.releaseRigidReplayConstants(f.live);f.backend.releaseRecordingContext(f.recording);
    checkDebug(NativeRecordingProbe::device(f.backend));
    std::printf("PASS VFX rigid mesh %s: original UV transform, texture/tint RGB, vertex alpha, zero opacity, alpha blending, depth/stencil, recorded parity and state restoration\n",hardware?"hardware":"WARP");
}
void chocolateBindings(const std::vector<uint8_t>& image) {
    const EffectRecord effect(0x8205D2D8,std::span<const uint8_t>(image).subspan(0x5D2D8,0x4100));
    const auto palette=effectPassBinding(effect,0x0007FFFC,0x00700030u);
    require(palette.usage==0&&!palette.lanes[0]&&!palette.lanes[1],"Chocolate palette sampler unexpectedly occupies a texture stage");
    const std::array<uint32_t,3> handles{0x00740032u,0x00780034u,0x007C0036u};
    for(uint32_t stage=0;stage<handles.size();++stage) {
        const auto binding=effectPassBinding(effect,0x0007FFFC,handles[stage]);
        require(binding.usage==0x80&&!binding.lanes[0]&&binding.lanes[1]&&binding.lanes[1]->start==stage&&binding.lanes[1]->count==1,
                "Chocolate material sampler does not map one-to-one to t0/t1/t2");
    }
}
void textured(const std::vector<uint8_t>& image,bool hardware) {
    Fixture fixture(image,hardware,true);
    auto mesh=fixture.backend.uploadRigidMesh(geometry(),strip);
    fixture.draw.indexCount=9;
    fixture.draw.baseSampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    fixture.draw.baseSampler.AddressU=fixture.draw.baseSampler.AddressV=fixture.draw.baseSampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    fixture.draw.baseSampler.MaxAnisotropy=1;fixture.draw.baseSampler.MaxLOD=13;
    fixture.draw.baseSampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
    const std::array<uint8_t,4> firstPixel{64,128,255,9},secondPixel{192,32,0,255};
    fixture.draw.baseTexture=fixture.backend.createTexture(1,1,TextureFormat::RGBA8,firstPixel);
    auto first=fixture.begin();
    const Snapshot before(fixture.immediate);const auto oldColor=fixture.backend.readbackTarget(fixture.color);
    fixture.record(first,mesh,fixture.vc,fixture.material(71),fixture.draw);
    fixture.backend.finishRecordingPayload(first);
    require(Snapshot(fixture.immediate)==before&&fixture.backend.readbackTarget(fixture.color)==oldColor,
            "Textured recording changed immediate state or rendered early");
    std::weak_ptr<Texture> retained=fixture.draw.baseTexture;
    fixture.draw.baseTexture=fixture.backend.createTexture(1,1,TextureFormat::RGBA8,secondPixel);
    auto second=fixture.begin();fixture.record(second,mesh,fixture.vc,fixture.material(315),fixture.draw);
    fixture.backend.finishRecordingPayload(second);
    require(!retained.expired(),"Textured payload did not retain its original texture");
    for(const bool original:{true,false,true}) {
        fixture.clear();fixture.execute(original?first:second);
        const uint32_t expected=(original?71u:315u)|((original?520u:152u)<<10)|((original?205u:0u)<<20)|(3u<<30);
        fixture.pixels([&](UINT,UINT){return expected;},[](UINT,UINT){return .5f;});
    }
    fixture.backend.bindRigidShaders(*fixture.vs,*fixture.ps);
    fixture.backend.bindRigidMeshVertices(mesh);fixture.backend.bindRigidMeshDeclaration(mesh);fixture.backend.bindRigidMeshIndices(mesh);
    for(UINT stage=0;stage<2;++stage)fixture.backend.bindRigidShadowDepth(stage,fixture.shadows[stage]);
    fixture.backend.bindEngineTexture(2,fixture.draw.baseTexture);
    auto constants=fixture.material(419);constants[31]=fixture.pc[31];
    auto commit=fixture.backend.commitRigid(*fixture.vs,*fixture.ps,fixture.vc,constants);
    const auto direct=[&](const RigidMeshDraw& draw) {
        fixture.backend.drawRigidMesh(fixture.color,fixture.depth,mesh,*fixture.vs,*fixture.ps,commit,draw);
    };
    fixture.clear();const Snapshot directBefore(fixture.immediate);direct(fixture.draw);
    require(Snapshot(fixture.immediate)==directBefore,"Textured direct draw changed immediate sampler or shader state");
    fixture.pixels([](UINT,UINT){return 419u|(152u<<10)|(3u<<30);},[](UINT,UINT){return .5f;});
    fixture.rejected([&]{auto invalid=fixture.draw;invalid.baseTexture.reset();direct(invalid);},"base texture");
    fixture.rejected([&]{auto invalid=fixture.draw;invalid.baseSampler.AddressU=D3D11_TEXTURE_ADDRESS_CLAMP;direct(invalid);},"linear/wrap");
    fixture.backend.releaseRecordingPayload(first);require(retained.expired(),"Retired textured payload leaked its texture");
    fixture.backend.releaseRecordingPayload(second);fixture.backend.releaseRecordingContext(fixture.recording);
    fixture.backend.releaseRigidReplayConstants(fixture.live);checkDebug(NativeRecordingProbe::device(fixture.backend));
    std::printf("PASS textured rigid: independent base textures, A/B/A replay, direct draw, state restoration and ownership\n");
}
void gloss(const std::vector<uint8_t>& image,bool hardware) {
    Fixture f(image,hardware,true,true);auto vertices=geometry();
    for(auto& v:vertices){v.normal={.8f,.6f,0};v.color={0,0,1,1};v.uv1={0,0};}
    auto mesh=f.backend.uploadRigidMesh(vertices,strip);f.draw.indexCount=9;
    auto& sampler=f.draw.baseSampler;sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.MaxAnisotropy=1;sampler.MaxLOD=13;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
    const std::array<uint8_t,4> texel{32,64,96,255};
    f.draw.baseTexture=f.backend.createTexture(1,1,TextureFormat::RGBA8,texel);
    f.pc[4]={16777216,0,0,0};f.pc[50][0]=99;f.pc[45][0]=99;f.pc[47][0]=99;
    f.backend.updateRigidReplayConstants(f.live,f.vc,f.pc);
    auto a=f.pc;a[45][0]=1;a[46][0]=0;a[47][0]=.5f;a[49][2]=70;a[50][0]=1;
    auto b=a;b[50][0]=2;
    const auto expected=[](unsigned exponent) {
        const double spec=.5*std::pow(.28,exponent);
        const auto red=uint32_t(std::floor(32*(32.0/255+spec)));
        const auto green=uint32_t(std::floor(32*(64.0/255+spec)));
        const auto blue=uint32_t(std::floor((.5+.2*(96.0/255+spec))*1023+.5));
        return 35u|((32*green+red)<<10)|(blue<<20)|(2u<<30);
    };
    auto first=f.begin();f.record(first,mesh,f.vc,a,f.draw);f.backend.finishRecordingPayload(first);
    auto second=f.begin();f.record(second,mesh,f.vc,b,f.draw);f.backend.finishRecordingPayload(second);
    for(bool original:{true,false,true}) {
        f.clear();f.execute(original?first:second);
        f.pixels([&](UINT,UINT){return expected(original?1:2);},[](UINT,UINT){return .5f;});
    }
    f.backend.bindRigidShaders(*f.vs,*f.ps);f.backend.bindRigidMeshVertices(mesh);
    f.backend.bindRigidMeshDeclaration(mesh);f.backend.bindRigidMeshIndices(mesh);
    for(UINT stage=0;stage<2;++stage)f.backend.bindRigidShadowDepth(stage,f.shadows[stage]);
    f.backend.bindEngineTexture(2,f.draw.baseTexture);
    for(unsigned exponent:{1u,2u}) {
        auto c=a;c[50][0]=float(exponent);auto commit=f.backend.commitRigid(*f.vs,*f.ps,f.vc,c);
        f.clear();const Snapshot before(f.immediate);
        f.backend.drawRigidMesh(f.color,f.depth,mesh,*f.vs,*f.ps,commit,f.draw);
        require(Snapshot(f.immediate)==before,"Gloss direct adapter changed retained bindings");
        f.pixels([&](UINT,UINT){return expected(exponent);},[](UINT,UINT){return .5f;});
    }
    f.backend.releaseRecordingPayload(first);f.backend.releaseRecordingPayload(second);
    f.backend.releaseRecordingContext(f.recording);f.backend.releaseRigidReplayConstants(f.live);
    checkDebug(NativeRecordingProbe::device(f.backend));
    std::puts("PASS gloss rigid: real VS/PS adapter, c50-dependent A/B/A replay and immediate RGB10A2/depth/stencil");
}
void multitone(const std::vector<uint8_t>& image,bool hardware) {
    Fixture f(image,hardware,true,false,true);auto vertices=geometry();
    for(auto& v:vertices){v.color={0,0,1,1};v.uv1={0,0};}
    auto mesh=f.backend.uploadRigidMesh(vertices,strip);f.draw.indexCount=9;
    auto& sampler=f.draw.baseSampler;sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.MaxAnisotropy=1;sampler.MaxLOD=13;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
    f.draw.noiseSampler=sampler;
    const std::array<uint8_t,4> base{64,96,128,255},low{0,0,0,255},high{255,255,255,0};
    f.draw.baseTexture=f.backend.createTexture(1,1,TextureFormat::RGBA8,base);
    f.draw.noiseTexture=f.backend.createTexture(1,1,TextureFormat::RGBA8,low);
    auto a=f.pc;a[45]={.5f,.5f,.5f,0};a[46]={1,1,1,0};a[47]={1,1,0,0};a[49]={0,0,70,1};
    auto b=a;b[49][2]=110;auto va=f.vc;va[46]={2,3,4,1};
    // Live c46 disagrees with each retained material; the original mask excludes it.
    f.vc[46]={99,98,97,0};f.backend.updateRigidReplayConstants(f.live,f.vc,f.pc);
    auto inheritance=capturedMask();inheritance[1]=0xE0; // Original multitone VS groups0..10.
    auto first=f.begin(inheritance);f.record(first,mesh,va,a,f.draw);f.backend.finishRecordingPayload(first);
    std::weak_ptr<Texture> retained=f.draw.noiseTexture;
    f.draw.noiseTexture=f.backend.createTexture(1,1,TextureFormat::RGBA8,high);
    auto second=f.begin(inheritance);f.record(second,mesh,va,b,f.draw);f.backend.finishRecordingPayload(second);
    require(!retained.expired(),"Multitone recording lost noise owner");
    const auto expected=[&](bool original) {
        const float gain=original?.5f:1.5f;
        const float r=float(base[0])/255*gain,g=float(base[1])/255*gain,z=float(base[2])/255*gain;
        const auto rg=uint32_t(std::floor(32*g))*32+uint32_t(std::floor(32*r));
        const auto blue=uint32_t(std::floor((.5f+.2f*z)*1023+.5f));
        return (original?35u:55u)|(rg<<10)|(blue<<20)|(2u<<30);
    };
    for(bool original:{true,false,true}) {f.clear();f.execute(original?first:second);
        f.pixels([&](UINT,UINT){return expected(original);},[](UINT,UINT){return .5f;});}
    f.backend.bindRigidShaders(*f.vs,*f.ps);f.backend.bindRigidMeshVertices(mesh);
    f.backend.bindRigidMeshDeclaration(mesh);f.backend.bindRigidMeshIndices(mesh);
    for(UINT stage=0;stage<2;++stage)f.backend.bindRigidShadowDepth(stage,f.shadows[stage]);
    f.backend.bindEngineTexture(2,f.draw.baseTexture);f.backend.bindEngineTexture(3,f.draw.noiseTexture);
    auto commit=f.backend.commitRigid(*f.vs,*f.ps,va,b);f.clear();const Snapshot before(f.immediate);
    f.backend.drawRigidMesh(f.color,f.depth,mesh,*f.vs,*f.ps,commit,f.draw);
    require(Snapshot(f.immediate)==before,"Multitone immediate draw changed retained slot3/state");
    f.pixels([&](UINT,UINT){return expected(false);},[](UINT,UINT){return .5f;});
    f.rejected([&]{auto invalid=f.draw;invalid.noiseTexture.reset();
        f.backend.drawRigidMesh(f.color,f.depth,mesh,*f.vs,*f.ps,commit,invalid);},"noise texture");
    f.backend.releaseRecordingPayload(first);require(retained.expired(),"Multitone retired payload leaked noise owner");
    f.backend.releaseRecordingPayload(second);f.backend.releaseRecordingContext(f.recording);
    f.backend.releaseRigidReplayConstants(f.live);checkDebug(NativeRecordingProbe::device(f.backend));
    std::puts("PASS multitone: real VS/PS/depth adapter, owned noise A/B/A, RGB10A2/depth/stencil and immediate state");
}
uint32_t packed(uint32_t id,uint32_t alpha){return id|(528u<<10)|(alpha<<30);}
void inheritedOpaque(const std::vector<uint8_t>& image,bool hardware) {
    Fixture f(image,hardware);auto mesh=f.backend.uploadRigidMesh(geometry(),strip);
    auto values=f.material(317);
    // This direct commit owns the complete PS bank. Enable original c31's
    // shadow branches, as the recorded-path fixture's inherited live bank
    // does, so both copied .75 depths produce the intended source alpha .4.
    values[31][0]=1;f.backend.updateRigidReplayConstants(f.live,f.vc,values);
    f.backend.bindRigidShaders(*f.vs,*f.ps);
    f.backend.bindRigidMeshVertices(mesh);f.backend.bindRigidMeshDeclaration(mesh);f.backend.bindRigidMeshIndices(mesh);
    observeMeshGpuRetirement(f.immediate,mesh,"rigid_opaque_inherited");
    for(UINT i=0;i<2;++i)f.backend.bindRigidShadowDepth(i,f.shadows[i]);
    const auto commit=f.backend.commitRigid(*f.vs,*f.ps,f.vc,values);
    auto draw=f.draw;draw.indexCount=9;
    f.clear();f.backend.drawRigidMesh(f.color,f.depth,mesh,*f.vs,*f.ps,commit,draw);f.backend.waitIdle();
    f.pixels([](UINT,UINT){return packed(317,1);},[](UINT,UINT){return .5f;});
    const auto replacement=f.backend.readbackTarget(f.color);
    draw.blendEnable=1;draw.blendWord=0x07060706;
    std::vector<uint8_t> reference;
    for(uint32_t expanded:{0u,1u}) {
        draw.expandedBlend=expanded;auto payload=f.begin();f.record(payload,mesh,f.vc,values,draw);
        f.backend.finishRecordingPayload(payload);std::vector<uint8_t> direct;
        for(bool deferred:{false,true}) {
            f.clear();const Snapshot before(f.immediate);
            if(deferred)f.execute(payload);else f.backend.drawRigidMesh(f.color,f.depth,mesh,*f.vs,*f.ps,commit,draw);
            f.backend.waitIdle();const auto pixels=f.backend.readbackTarget(f.color);
            require(Snapshot(f.immediate)==before,"Inherited opaque rigid blend changed caller bindings");
            // Original PS8200D9E8 emits alpha .4 for this copied .75 depth.
            // SRC_ALPHA/INV_SRC_ALPHA blends its fractional RGB and alpha.
            constexpr std::array<uint32_t,3> expected{741,211,614};
            for(size_t i=0;i<pixels.size();i+=4) {
                const auto value=word(pixels,i);
                for(size_t lane=0;lane<3;++lane)
                    require(std::abs(int((value>>(10*lane))&1023)-int(expected[lane]))<=1,
                        "Inherited opaque rigid RGB factors differ");
                require((value>>30)==2,"Inherited opaque rigid alpha factors differ");
            }
            require(pixels!=replacement,"Opaque rigid ignored inherited blending");
            if(!deferred)direct=pixels;else require(pixels==direct,"Inherited rigid direct/recorded pixels differ");
            if(reference.empty())reference=pixels;else require(pixels==reference,"Expanded rigid tuple changed its equation");
        }
        f.backend.releaseRecordingPayload(payload);
        f.rejected([&]{f.backend.executeRecordingPayload(payload);},"released");
    }
    auto pending=f.begin();
    const auto malformed=[&](const RigidMeshDraw& bad) {
        f.rejected([&]{f.backend.drawRigidMesh(f.color,f.depth,mesh,*f.vs,*f.ps,commit,bad);},"blend");
        f.rejected([&]{f.backend.recordRigidMesh(pending,f.color,f.depth,mesh,*f.vs,*f.ps,f.vc,values,f.live,bad);},"blend");
        require(f.backend.recordingPayloadReceipt(pending).recordedDraws==0,"Malformed inherited rigid blend recorded a draw");
    };
    auto bad=draw;bad.blendEnable=2;malformed(bad);
    bad=draw;bad.expandedBlend=2;malformed(bad);
    bad=draw;bad.blendWord=0x00010706;malformed(bad);
    bad=draw;bad.blendWord=0x00010001;malformed(bad);
    bad=draw;bad.blendEnable=0;malformed(bad);
    f.backend.releaseRecordingPayload(pending);f.backend.releaseRigidReplayConstants(f.live);
    f.backend.releaseRecordingContext(f.recording);checkDebug(NativeRecordingProbe::device(f.backend));
}
void tangent(const std::vector<uint8_t>& image,bool hardware) {
    // Tangent GPU input layout and shader input interface only. No normalmap
    // shading, material staging or source admission is exercised here.
    Fixture f(image,hardware);
    auto raw=geometry();
    const std::array<std::array<float,3>,8> tangents={{{1,0,0},{0,1,0},{0,0,1},{-1,0,0},
        {0,-1,0},{0,0,-1},{0,0,0},{.5f,-.25f,.75f}}};
    for(size_t i=0;i<raw.size();++i){raw[i].tangent=tangents[i];raw[i].uv1={float(i)*.125f,float(i)*-.0625f};}
    auto mesh=f.backend.uploadRigidMesh(raw,strip);
    const auto read=f.backend.readbackRigidMeshVertices(mesh);
    require(read.size()==raw.size()&&std::memcmp(read.data(),raw.data(),sizeof(raw))==0,
        "Tangent immutable upload changed a decoded attribute");
    auto* device=NativeRecordingProbe::device(f.backend);
    auto* context=NativeRecordingProbe::immediate(f.backend);
    // The default 5-element declaration still binds with the 68-byte stride;
    // every existing consumer keeps its exact binding.
    f.backend.bindRigidMeshVertices(mesh);f.backend.bindRigidMeshDeclaration(mesh);f.backend.bindRigidMeshIndices(mesh);
    ComPtr<ID3D11InputLayout> five;
    {ComPtr<ID3D11Buffer> vb;UINT stride{},offset{};context->IAGetVertexBuffers(0,1,&vb,&stride,&offset);
     context->IAGetInputLayout(&five);
     require(stride==sizeof(RigidVertex)&&!offset,"Default rigid declaration changed stride/offset");}
    // The explicit normalmap declaration binds a distinct layout with the same
    // stride and retains it on the device context.
    f.backend.bindRigidNormalMeshDeclaration(mesh);
    ComPtr<ID3D11InputLayout> six;
    {ComPtr<ID3D11Buffer> vb;UINT stride{},offset{};context->IAGetVertexBuffers(0,1,&vb,&stride,&offset);
     context->IAGetInputLayout(&six);
     require(stride==sizeof(RigidVertex)&&!offset,"Tangent rigid declaration changed stride/offset");}
    require(five.Get()&&six.Get()&&five.Get()!=six.Get(),"Tangent declaration aliases the default 5-element layout");
    // Stream-output round-trip through the production tangent layout and the
    // passthrough VS8205855C input interface: every lane must arrive bit-exact.
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11GeometryShader> gs;
    hr(device->CreateVertexShader(kVSRigidNormalTangent,sizeof(kVSRigidNormalTangent),nullptr,&vs),"Normalmap tangent VS");
    const D3D11_SO_DECLARATION_ENTRY entries[]={{0,"SV_Position",0,0,4,0},{0,"TEXCOORD",0,0,3,0},
        {0,"TEXCOORD",1,0,4,0},{0,"TEXCOORD",2,0,2,0},{0,"TEXCOORD",3,0,2,0},{0,"TEXCOORD",4,0,3,0}};
    const UINT soStride=18*sizeof(float);
    hr(device->CreateGeometryShaderWithStreamOutput(kGSRigidNormalTangentProbe,sizeof(kGSRigidNormalTangentProbe),
        entries,6,&soStride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&gs),"Normalmap tangent observer");
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(raw.size()*soStride);desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_STREAM_OUTPUT;
    ComPtr<ID3D11Buffer> out;hr(device->CreateBuffer(&desc,nullptr,&out),"Tangent stream outputs");
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;hr(device->CreateBuffer(&desc,nullptr,&staging),"Tangent stream staging");
    ComPtr<ID3D11VertexShader> oldVS;ComPtr<ID3D11GeometryShader> oldGS;ComPtr<ID3D11PixelShader> oldPS;ComPtr<ID3D11InputLayout> oldLayout;
    D3D11_PRIMITIVE_TOPOLOGY oldTopology{};
    context->VSGetShader(&oldVS,nullptr,nullptr);context->GSGetShader(&oldGS,nullptr,nullptr);
    context->PSGetShader(&oldPS,nullptr,nullptr);context->IAGetInputLayout(&oldLayout);context->IAGetPrimitiveTopology(&oldTopology);
    context->OMSetRenderTargets(0,nullptr,nullptr);
    f.backend.bindRigidMeshVertices(mesh);f.backend.bindRigidNormalMeshDeclaration(mesh);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    context->VSSetShader(vs.Get(),nullptr,0);context->GSSetShader(gs.Get(),nullptr,0);context->PSSetShader(nullptr,nullptr,0);
    const UINT zero=0;auto* output=out.Get();context->SOSetTargets(1,&output,&zero);
    context->Draw(UINT(raw.size()),0);
    context->SOSetTargets(0,nullptr,nullptr);context->CopyResource(staging.Get(),out.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Tangent stream readback");
    std::vector<float> actual(raw.size()*18);std::memcpy(actual.data(),mapped.pData,raw.size()*soStride);context->Unmap(staging.Get(),0);
    const auto laneBits=[&](size_t vertex,size_t lane){return std::bit_cast<uint32_t>(actual[vertex*18+lane]);};
    for(size_t i=0;i<raw.size();++i) {
        for(size_t lane=0;lane<3;++lane) {
            require(laneBits(i,lane)==std::bit_cast<uint32_t>(raw[i].position[lane]),"Tangent VS position lane changed");
            require(laneBits(i,4+lane)==std::bit_cast<uint32_t>(raw[i].normal[lane]),"Tangent VS normal lane changed");
            require(laneBits(i,15+lane)==std::bit_cast<uint32_t>(raw[i].tangent[lane]),"Tangent VS tangent lane changed");
        }
        require(laneBits(i,3)==std::bit_cast<uint32_t>(1.0f),"Tangent VS homogeneous W differs");
        for(size_t lane=0;lane<4;++lane)
            require(laneBits(i,7+lane)==std::bit_cast<uint32_t>(raw[i].color[lane]),"Tangent VS color lane changed");
        for(size_t lane=0;lane<2;++lane) {
            require(laneBits(i,11+lane)==std::bit_cast<uint32_t>(raw[i].uv[lane]),"Tangent VS UV lane changed");
            require(laneBits(i,13+lane)==std::bit_cast<uint32_t>(raw[i].uv1[lane]),"Tangent VS UV1 lane changed");
        }
        rigidChecks+=18;
    }
    // The Chocolate VS declares tangent before color/UV in its input
    // signature. Reusing the normalmap layout silently routes the constant
    // second UV into the animated first UV. Exercise the production Chocolate
    // declaration with the actual Chocolate VS and stream out its texture
    // coordinates before any pixel shader or authored texture can mask it.
    {
        auto river=raw;
        for(size_t i=0;i<river.size();++i) {
            river[i].uv={.125f+float(i)*.0625f,.625f-float(i)*.0625f};
            river[i].uv1={.8125f,.9375f};
        }
        auto riverMesh=f.backend.uploadRigidMesh(river,strip);
        ComPtr<ID3D11VertexShader> chocolateVS;
        hr(device->CreateVertexShader(kVSChocolate,sizeof(kVSChocolate),nullptr,&chocolateVS),"Chocolate VS");
        ComPtr<ID3D11GeometryShader> chocolateGS;
        const D3D11_SO_DECLARATION_ENTRY uvEntry{0,"TEXCOORD",0,0,4,0};
        const UINT uvStride=4*sizeof(float);
        hr(device->CreateGeometryShaderWithStreamOutput(kGSChocolateUVProbe,sizeof(kGSChocolateUVProbe),
            &uvEntry,1,&uvStride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&chocolateGS),"Chocolate UV observer");
        RigidVertexConstants constants{};
        constants[0]={0,1,0,0};constants[1]={0,0,1,0};
        constants[2]={1,0,0,0};constants[3]={0,0,0,1};
        constants[46]={1,1,1,1}; // t0.xyzw must equal UV0.xyxy.
        D3D11_BUFFER_DESC constantDesc{};constantDesc.ByteWidth=sizeof(constants);
        constantDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;constantDesc.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA initial{constants.data(),0,0};
        ComPtr<ID3D11Buffer> chocolateConstants;
        hr(device->CreateBuffer(&constantDesc,&initial,&chocolateConstants),"Chocolate VS constants");
        ComPtr<ID3D11Buffer> oldConstants;
        context->VSGetConstantBuffers(0,1,&oldConstants);
        f.backend.bindRigidMeshVertices(riverMesh);
        f.backend.bindChocolateMeshDeclaration(riverMesh);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        context->VSSetShader(chocolateVS.Get(),nullptr,0);
        auto* cb=chocolateConstants.Get();context->VSSetConstantBuffers(0,1,&cb);
        context->GSSetShader(chocolateGS.Get(),nullptr,0);context->PSSetShader(nullptr,nullptr,0);
        auto* output=out.Get();context->SOSetTargets(1,&output,&zero);
        context->Draw(UINT(river.size()),0);
        context->SOSetTargets(0,nullptr,nullptr);context->CopyResource(staging.Get(),out.Get());
        D3D11_MAPPED_SUBRESOURCE chocolateMapped{};
        hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&chocolateMapped),"Chocolate UV stream readback");
        std::vector<float> chocolateActual(river.size()*4);
        std::memcpy(chocolateActual.data(),chocolateMapped.pData,river.size()*uvStride);
        context->Unmap(staging.Get(),0);
        for(size_t i=0;i<river.size();++i)for(size_t lane=0;lane<4;++lane) {
            const float expected=river[i].uv[lane&1];
            require(std::bit_cast<uint32_t>(chocolateActual[i*4+lane])==std::bit_cast<uint32_t>(expected),
                "Chocolate VS sampled constant UV1 instead of varying UV0");
        }
        cb=oldConstants.Get();context->VSSetConstantBuffers(0,1,&cb);
    }
    context->VSSetShader(oldVS.Get(),nullptr,0);context->GSSetShader(oldGS.Get(),nullptr,0);context->PSSetShader(oldPS.Get(),nullptr,0);
    context->IASetInputLayout(oldLayout.Get());context->IASetPrimitiveTopology(oldTopology);
    // The deferred recording path accepts the same explicit declaration.
    auto payload=f.begin();
    f.backend.bindRigidMeshVertices(payload,mesh);f.backend.bindRigidNormalMeshDeclaration(payload,mesh);f.backend.bindRigidMeshIndices(payload,mesh);
    f.backend.releaseRecordingPayload(payload);
    // Rebinding the default declaration restores the exact 5-element consumer.
    f.backend.bindRigidMeshDeclaration(mesh);
    {ComPtr<ID3D11InputLayout> actual;context->IAGetInputLayout(&actual);require(actual.Get()==five.Get(),"Default rigid declaration did not restore");}
    auto bad=raw;bad[0].tangent[0]=std::numeric_limits<float>::quiet_NaN();
    f.rejected([&]{f.backend.uploadRigidMesh(bad,strip);},"Nonfinite rigid tangent");
    f.rejected([&]{f.backend.bindRigidNormalMeshDeclaration(std::shared_ptr<NativeRigidMesh>());},"declaration owner");
    NativeBackend other(!hardware);auto foreign=other.uploadRigidMesh(raw,strip);
    f.rejected([&]{f.backend.bindRigidNormalMeshDeclaration(foreign);},"another backend");
    f.backend.releaseRecordingContext(f.recording);f.backend.releaseRigidReplayConstants(f.live);
    checkDebug(NativeRecordingProbe::device(f.backend));
    std::printf("PASS rigid tangent input %s: %zu checks; 6-element layout, bit-exact VS/UV1/tangent round-trip, deferred bind, default-declaration preservation and ownership\n",
        hardware?"hardware":"WARP",rigidChecks);
}
void direct(const std::vector<uint8_t>& image,bool hardware) {
    Fixture f(image,hardware);const auto raw=geometry();auto mesh=f.backend.uploadRigidMesh(raw,strip);
    f.backend.bindRigidShaders(*f.vs,*f.ps);
    f.backend.bindRigidMeshVertices(mesh);f.backend.bindRigidMeshDeclaration(mesh);f.backend.bindRigidMeshIndices(mesh);
    for(UINT i=0;i<2;++i)f.backend.bindRigidShadowDepth(i,f.shadows[i]);
    const auto commit=[&](uint32_t id) {
        auto values=f.material(float(id));values[31]=f.pc[31];auto vertices=f.vc;
        auto result=f.backend.commitRigid(*f.vs,*f.ps,vertices,values);
        vertices[0][3]=99;values[49][2]=999; // The committed backing must own a copy.
        return result;
    };
    auto current=commit(71);
    const auto draw=[&](const RigidMeshDraw& state){f.backend.drawRigidMesh(f.color,f.depth,mesh,*f.vs,*f.ps,current,state);};
    const auto execute=[&](const RigidMeshDraw& state) {
        const Snapshot before(f.immediate);const auto sentinel=f.sentinelBytes();
        const auto vertices=f.backend.readbackRigidVertexConstants(current);
        const auto pixels=f.backend.readbackRigidPixelConstants(current);
        const auto sibling=f.backend.readbackTarget(f.sibling),source=f.backend.readbackDepthTarget(f.sourceDepth);
        const auto world=f.backend.readbackDepthTarget(f.shadows[0]),character=f.backend.readbackDepthTarget(f.shadows[1]);
        const auto recorded=f.backend.recordingDrawCount(),executed=f.backend.recordingExecutedDrawCount(),count=f.backend.rigidMeshDrawCount();
        draw(state);f.backend.waitIdle();
        require(f.backend.rigidMeshDrawCount()==count+1,"Direct rigid draw did not produce exactly one native submission");
        require(f.backend.recordingDrawCount()==recorded&&f.backend.recordingExecutedDrawCount()==executed,"Direct rigid draw fabricated recording accounting");
        require(Snapshot(f.immediate)==before,"Direct rigid draw changed retained native pipeline state");
        require(f.sentinelBytes()==sentinel&&f.backend.readbackRigidVertexConstants(current)==vertices&&
                f.backend.readbackRigidPixelConstants(current)==pixels,"Direct rigid draw overwrote committed or unrelated constants");
        require(f.backend.readbackTarget(f.sibling)==sibling&&f.backend.readbackDepthTarget(f.sourceDepth)==source&&
                f.backend.readbackDepthTarget(f.shadows[0])==world&&f.backend.readbackDepthTarget(f.shadows[1])==character,
                "Direct rigid draw changed sibling, source or sampled resources");
    };
    execute(f.draw);f.pixels([](UINT x,UINT){return x<8?packed(71,1):untouched;},[](UINT x,UINT){return x<8?.5f:0.f;});
    auto old=current;current=commit(603);auto right=f.draw;right.startIndex=5;right.multisampleMask=0x0000FFFF;
    execute(right);f.pixels([](UINT x,UINT){return packed(x<8?71:603,1);},[](UINT,UINT){return .5f;});
    f.rejected([&]{f.backend.drawRigidMesh(f.color,f.depth,mesh,*f.vs,*f.ps,old,f.draw);},"constants differ");
    f.rejected([&]{auto d=f.draw;d.startIndex=8;draw(d);},"range");
    f.rejected([&]{auto d=f.draw;d.baseVertex=-1;draw(d);},"effective index range");
    f.rejected([&]{auto d=f.draw;d.blendEnable=1;draw(d);},"blend");
    f.rejected([&]{auto d=f.draw;d.depthPolicy=ShadowMeshDepthPolicy::Unqualified;draw(d);},"depth policy");
    f.rejected([&]{auto d=f.draw;d.shadows[1]=d.shadows[0];draw(d);},"distinct depth owners");
    f.rejected([&]{auto d=f.draw;d.samplers[0].AddressU=D3D11_TEXTURE_ADDRESS_WRAP;draw(d);},"samplers");
    f.rejected([&]{f.backend.bindRigidShadowDepth(2,f.shadows[0]);},"stage0/1");
    f.rejected([&]{f.backend.bindRigidShadowDepth(0,f.depth);},"1024-square");
    // Same bytes now share one immutable upload, so the stale-binding check
    // needs distinct bytes to produce a genuinely different mesh owner.
    auto rawMut = raw; rawMut[0].uv[0] += 0.5f;
    auto different=f.backend.uploadRigidMesh(rawMut,strip);f.backend.bindRigidMeshVertices(different);
    f.rejected([&]{draw(f.draw);},"vertex binding");f.backend.bindRigidMeshVertices(mesh);
    f.backend.bindRigidShadowDepth(0,f.shadows[1]);
    f.rejected([&]{draw(f.draw);},"shadow binding");f.backend.bindRigidShadowDepth(0,f.shadows[0]);
    NativeBackend other(!hardware);auto foreign=other.uploadRigidMesh(raw,strip);
    f.rejected([&]{f.backend.bindRigidMeshIndices(foreign);},"another backend");
    f.rejected([&]{f.backend.drawRigidMesh(f.color,f.depth,foreign,*f.vs,*f.ps,current,f.draw);},"another backend");
    // Both winding/parity sections consume a referenced zero-normal strip.
    auto zeroRaw=raw;for(size_t i=0;i<4;++i)zeroRaw[i].normal={0,-0.f,0};mesh=f.backend.uploadRigidMesh(zeroRaw,strip);
    f.backend.bindRigidMeshVertices(mesh);f.backend.bindRigidMeshDeclaration(mesh);f.backend.bindRigidMeshIndices(mesh);
    auto zeroPS=f.material(317);zeroPS[31]=f.pc[31];zeroPS[46][0]=1;
    current=f.backend.commitRigid(*f.vs,*f.ps,f.vc,zeroPS);auto full=f.draw;full.indexCount=9;f.clear();execute(full);
    f.pixels([](UINT x,UINT){return x<8?packed(317,0)|(128u<<20):packed(317,1);},[](UINT,UINT){return .5f;});
    f.backend.releaseRecordingContext(f.recording);f.backend.releaseRigidReplayConstants(f.live);
    checkDebug(NativeRecordingProbe::device(f.backend));
    std::printf("PASS rigid direct mesh %s: %llu real direct draws; immutable commits, exact color/depth/stencil, state restoration and rejection ownership\n",
        hardware?"hardware":"WARP",static_cast<unsigned long long>(f.backend.rigidMeshDrawCount()));
}
void uploadCacheReuse(bool hardware) {
    NativeBackend backend(!hardware);
    const auto base = geometry();
    std::vector<RigidVertex> vv(base.begin(), base.end());
    std::vector<uint16_t> ii(strip.begin(), strip.end());
    auto first = backend.uploadRigidMesh(vv, ii);
    require(backend.uploadRigidMesh(vv, ii).get() == first.get(), "Rigid repeat upload did not reuse exact bytes");
    // Same content at a different guest address must still hit (never by pointer).
    std::vector<RigidVertex> vvAlias = vv;
    std::vector<uint16_t> iiAlias = ii;
    require(vvAlias.data() != vv.data(), "Rigid alias fixture shares guest address");
    require(backend.uploadRigidMesh(vvAlias, iiAlias).get() == first.get(), "Rigid identical bytes at new address missed");
    // Caller mutation isolation: the cache snapshots exact bytes on insertion.
    const std::vector<RigidVertex> origV = vv;
    const std::vector<uint16_t> origI = ii;
    vv[0].uv[0] += 0.5f;
    require(backend.uploadRigidMesh(vv, ii).get() != first.get(), "Rigid single-float change incorrectly hit");
    require(backend.uploadRigidMesh(origV, origI).get() == first.get(), "Rigid original bytes missed after caller mutation");
    const auto snapshot = backend.readbackRigidMeshVertices(first);
    require(snapshot.size() == origV.size() &&
        !std::memcmp(snapshot.data(), origV.data(), origV.size() * sizeof(RigidVertex)),
        "Rigid cached mesh changed after caller mutation");
    // Invalid bytes can never hit: they miss and run the existing checks.
    bool rejected = false;
    try {
        auto bad = origV;
        bad[0].position[0] = std::numeric_limits<float>::quiet_NaN();
        backend.uploadRigidMesh(bad, origI);
    } catch (const Error& e) {
        rejected = std::string(e.what()).find("Nonfinite rigid position") != std::string::npos;
    }
    require(rejected, "Rigid invalid position accepted after caching");
    const std::array<uint16_t,3> opaque = {0, 1, 8};
    const auto indexed=backend.uploadRigidMesh(origV,opaque);
    require(indexed!=first&&indexed->indexCount()==opaque.size()&&backend.uploadRigidMesh(origV,opaque)==indexed,
            "Rigid opaque R16 bytes lost immutable upload/cache ownership before draw qualification");
    require(backend.uploadRigidMesh(origV, origI).get() == first.get(), "Rigid valid bytes missed after invalid rejections");
    // Device isolation: no static global, no cross-device hits.
    NativeBackend foreign(!hardware);
    auto foreignMesh = foreign.uploadRigidMesh(origV, origI);
    require(foreignMesh.get() != first.get(), "Rigid cross-device hit");
    rejected = false;
    try {
        backend.bindRigidMeshVertices(foreignMesh);
    } catch (const Error& e) {
        rejected = std::string(e.what()).find("another backend") != std::string::npos;
    }
    require(rejected, "Rigid foreign mesh bound on local device");
    require(backend.uploadRigidMesh(origV, origI).get() == first.get(), "Rigid local hit lost after foreign upload");
    // Deterministic device replacement through the fixture: both the stale
    // handle bind and the exact-byte reupload must reject via State::validate.
    auto stale = first;
    ID3D11Device* beforeDevice = NativeRecordingProbe::device(backend);
    NativeRecordingProbe::replaceDevice(backend, hardware);
    require(NativeRecordingProbe::device(backend) != beforeDevice, "Rigid device replacement did not occur");
    rejected = false;
    try {
        backend.bindRigidMeshVertices(stale);
    } catch (const Error& e) {
        rejected = std::string(e.what()).find("another backend") != std::string::npos;
    }
    require(rejected, "Rigid stale mesh survived a device replacement");
    rejected = false;
    try {
        backend.uploadRigidMesh(origV, origI);
    } catch (const Error& e) {
        rejected = std::string(e.what()).find("another backend") != std::string::npos;
    }
    require(rejected, "Rigid cached reupload accepted stale device bytes");
    // Owner isolation: a different thread must fail owner checks, never hit.
    bool ownerRejected = false;
    std::thread probe([&] {
        try {
            backend.uploadRigidMesh(origV, origI);
        } catch (const Error& e) {
            ownerRejected = std::string(e.what()).find("different thread") != std::string::npos;
        }
    });
    probe.join();
    require(ownerRejected, "Rigid cross-thread upload bypassed owner checks");
}
void run(const std::vector<uint8_t>& image,bool hardware) {
    Fixture f(image,hardware);const auto raw=geometry();auto mesh=f.backend.uploadRigidMesh(raw,strip);
    const auto readVertices=f.backend.readbackRigidMeshVertices(mesh);
    const auto readIndices=f.backend.readbackRigidMeshIndices(mesh);
    require(readVertices.size()==raw.size()&&std::memcmp(readVertices.data(),raw.data(),sizeof(raw))==0,"Rigid immutable vertex upload changed a decoded attribute");
    require(std::equal(readIndices.begin(),readIndices.end(),strip.begin(),strip.end()),"Rigid immutable R16 upload changed restart/range");
    const Snapshot initial(f.immediate);const auto initialConstants=f.sentinelBytes();
    const auto colorBefore=f.backend.readbackTarget(f.color),depthBefore=f.backend.readbackDepthTarget(f.depth);
    const auto recorded=f.backend.recordingDrawCount(),executed=f.backend.recordingExecutedDrawCount();
    auto payload=f.begin();
    // Material VS deliberately disagrees with current inherited VS. It would
    // clip everything if replay failed to substitute the captured VS groups.
    auto frozenVS=f.vc;frozenVS[0][3]=8;
    auto left=f.material(64),right=f.material(512);auto rightDraw=f.draw;rightDraw.startIndex=5;
    rightDraw.multisampleMask=0x0000FFFF; // Actual forced game state; same pixel coverage as FFFFFFFF.
    f.record(payload,mesh,frozenVS,left,f.draw);f.record(payload,mesh,frozenVS,right,rightDraw);
    left[49][2]=right[49][2]=1000;frozenVS[0][3]=100; // Caller storage is reusable now.
    f.backend.finishRecordingPayload(payload);
    require(Snapshot(f.immediate)==initial,"Begin/bind/record/finish mutated immediate full pipeline state");
    require(f.sentinelBytes()==initialConstants,"Record sequence touched immediate constants");
    require(f.backend.readbackTarget(f.color)==colorBefore&&f.backend.readbackDepthTarget(f.depth)==depthBefore,"Recorded commands rendered before Execute");
    auto receipt=f.backend.recordingPayloadReceipt(payload);
    require(receipt.state==NativeRecordingPayloadState::Sealed&&receipt.recordedDraws==2&&receipt.executedDraws==0&&receipt.executions==0&&
            receipt.ownedDataBytes==2*(sizeof(RigidVertexConstants)+sizeof(RigidPixelConstants)+sizeof(NativeRecordingMask))&&
            receipt.inputMask==capturedMask()&&receipt.outputMask==capturedMask(),"Rigid payload did not own two independent value snapshots");
    require(f.backend.recordingDrawCount()==recorded+2&&f.backend.recordingExecutedDrawCount()==executed,"Recording fabricated an execution");
    f.execute(payload);
    // Both copied depths are .75. Replicated RRRR makes every comparison false:
    // original alpha .4 -> RGB10A2 alpha code1. Default R32 G/B zeros would fail.
    f.pixels([](UINT x,UINT){return packed(x<8?64:512,1);},[](UINT,UINT){return .5f;});
    std::array<std::shared_ptr<RenderTarget>,2> queued;
    std::array<std::shared_ptr<DepthTarget>,2> queuedDepth;
    for(size_t i=0;i<queued.size();++i) {
        queued[i]=f.backend.createTarget(Fixture::extent,Fixture::extent,TargetFormat::RGB10A2);
        queuedDepth[i]=f.backend.createDepthTarget(Fixture::extent,Fixture::extent);
    }
    const auto queuedColor=f.backend.readbackTarget(f.color),oldDepth=f.backend.readbackDepthTarget(f.depth);
    const Snapshot queueBefore(f.immediate);const auto queueExecutions=f.backend.recordingPayloadReceipt(payload).executions;
    // Preserve two unchanged GPU draws before updating the same private
    // constant buffers. There is no readback or wait inside this queue.
    for(size_t i=0;i<queued.size();++i) {
        f.clear();f.backend.executeRecordingPayload(payload);
        (void)f.backend.copyFront(f.color,queued[i]);(void)f.backend.copyDepth(f.depth,queuedDepth[i]);
    }
    f.clear();auto currentVS=f.vc;currentVS[0][0]=.5f;currentVS[2][3]=.25f;
    auto currentPS=f.pc;currentPS[31][0]=-1;currentPS[40][0]=700;currentPS[49][2]=1001;
    f.backend.updateRigidReplayConstants(f.live,currentVS,currentPS);f.backend.executeRecordingPayload(payload);f.backend.waitIdle();
    require(Snapshot(f.immediate)==queueBefore&&f.backend.recordingPayloadReceipt(payload).executions==queueExecutions+3,
            "Queued rigid replays changed bindings or execution accounting");
    for(size_t i=0;i<queued.size();++i) {
        require(f.backend.readbackTarget(queued[i])==queuedColor,"Changed rigid constants overwrote an earlier queued color result");
        const auto copied=f.backend.readbackDepthTarget(queuedDepth[i]);
        for(size_t p=0;p<Fixture::extent*Fixture::extent;++p)
            require(word(copied,8*p)==word(oldDepth,8*p)&&copied[8*p+4]==oldDepth[8*p+4],
                    "Changed rigid constants overwrote an earlier queued depth/stencil result");
    }
    // Same list, new inherited geometry/receiver. Recorded IDs remain DISTINCT
    // and ignore the caller's rewritten arrays and unrelated live c49 value.
    f.pixels([](UINT x,UINT){return x>=4&&x<12?packed(x<8?64:512,0):untouched;},[](UINT x,UINT){return x>=4&&x<12?.25f:0.f;});
    f.backend.updateRigidReplayConstants(f.live,f.vc,f.pc);
    // Real source->owned-depth copies feed both banks. Separate cases expose
    // swapping a missing bank for the other, depth equality and format expansion.
    for(const auto& pair:std::array<std::array<float,2>,4>{{{.25f,.25f},{.75f,.25f},{.25f,.75f},{.5f,.5f}}}) {
        f.copyShadows(pair[0],pair[1]);f.clear();f.execute(payload);
        const uint32_t alpha=pair[0]>.5f||pair[1]>.5f?1:0;
        f.pixels([&](UINT x,UINT){return packed(x<8?64:512,alpha);},[](UINT,UINT){return .5f;});
    }
    // Several cached objects share one deferred context but retain independent
    // inherited banks and immutable materials. Replay old lists after newer
    // recordings and after changing only one object's current transform.
    std::array<std::shared_ptr<NativeRecordingPayload>,3> cached;
    std::array<std::shared_ptr<NativeRigidReplayConstants>,3> objectLive;
    std::array<RigidVertexConstants,3> objectVS{f.vc,f.vc,f.vc};
    objectVS[1][0][3]=1;objectVS[1][2][3]=.25f;
    objectVS[2][0][3]=.5f;objectVS[2][2][3]=-.25f;
    auto objectPS=f.pc;objectPS[31][0]=-1;objectPS[49][2]=1000;
    const auto sharedLive=f.live;f.clear();const Snapshot cacheBefore(f.immediate);
    const auto cacheColor=f.backend.readbackTarget(f.color),cacheDepth=f.backend.readbackDepthTarget(f.depth);
    for(size_t i=0;i<cached.size();++i) {
        objectLive[i]=f.backend.createRigidReplayConstants(objectVS[i],objectPS);f.live=objectLive[i];
        cached[i]=f.begin();auto saved=f.material(float(121+i));saved[47][0]=0;
        f.record(cached[i],mesh,f.vc,saved,f.draw);f.backend.finishRecordingPayload(cached[i]);
        saved[49][2]=999; // The completed list must not borrow this reused array.
    }
    f.live=sharedLive;
    require(Snapshot(f.immediate)==cacheBefore&&f.backend.readbackTarget(f.color)==cacheColor&&
            f.backend.readbackDepthTarget(f.depth)==cacheDepth,"Building multiple cached objects changed immediate rendering");
    for(size_t i:{0u,1u,0u,2u,1u}) {
        f.clear();f.execute(cached[i]);const UINT left=i==0?0:i==1?8:4,right=left+8;
        const float z=i==0?.5f:i==1?.25f:.75f;
        f.pixels([&](UINT x,UINT){return x>=left&&x<right?packed(uint32_t(121+i),0):untouched;},
                 [&](UINT x,UINT){return x>=left&&x<right?z:0.f;});
    }
    objectVS[0][0][3]=.5f;objectVS[0][2][3]=.25f;
    f.backend.updateRigidReplayConstants(objectLive[0],objectVS[0],objectPS);
    f.clear();f.execute(cached[0]);
    f.pixels([](UINT x,UINT){return x>=4&&x<12?packed(121,0):untouched;},[](UINT x,UINT){return x>=4&&x<12?.25f:0.f;});
    f.clear();f.execute(cached[2]);
    f.pixels([](UINT x,UINT){return x>=4&&x<12?packed(123,0):untouched;},[](UINT x,UINT){return x>=4&&x<12?.75f:0.f;});
    for(size_t i=0;i<cached.size();++i) {
        std::weak_ptr<NativeRigidReplayConstants> retained=objectLive[i];objectLive[i].reset();
        require(!retained.expired(),"Cached list lost its per-object inherited owner");
        f.backend.releaseRecordingPayload(cached[i]);
        require(retained.expired(),"Released cached list leaked its per-object inherited owner");
    }

    // No-inheritance recording freezes both banks despite later live updates.
    f.clear();auto frozen=f.begin({});auto state=f.draw;auto material=f.material(93);material[31][0]=0;
    f.record(frozen,mesh,f.vc,material,state);f.backend.finishRecordingPayload(frozen);
    f.backend.updateRigidReplayConstants(f.live,currentVS,currentPS);f.execute(frozen);
    f.pixels([](UINT x,UINT){return x<8?packed(93,0):untouched;},[](UINT x,UINT){return x<8?.5f:0.f;});
    f.backend.updateRigidReplayConstants(f.live,f.vc,f.pc);
    f.backend.releaseRecordingPayload(frozen);

    // Explicit scissor, masked color writes and both bias scales use the SAME
    // audited depth adapter as the shadow/z pass. This list records one strip
    // containing restartFFFF, so both halves must be drawn by ONE native draw.
    auto sloped=f.backend.uploadRigidMesh(geometry(.5f,.125f),strip);auto biased=f.draw;
    biased.indexCount=9;biased.scissorEnable=1;biased.scissor={2,3,14,13};biased.colorMask=1;
    biased.depthBiasBits=bits(std::ldexp(1.f,-12));biased.slopeBiasBits=bits(2.f);
    auto biasPayload=f.begin();material=f.material(219);material[47][0]=0;
    f.record(biasPayload,sloped,f.vc,material,biased);f.backend.finishRecordingPayload(biasPayload);f.clear();f.execute(biasPayload);
    f.pixels([](UINT x,UINT y){return x>=2&&x<14&&y>=3&&y<13?(untouched&~1023u)|219u:untouched;},[&](UINT x,UINT y){
        if(x<2||x>=14||y<3||y>=13)return 0.f;
        const float ndc=(float(x)+.5f)/8-1;volatile float z=1-(.5f+.125f*ndc);
        volatile float constant=std::ldexp(1.f,-12),scale=2,term=scale*(.25f/16);
        volatile float bias=term+constant;return quantize(z+bias);
    });
    f.backend.releaseRecordingPayload(biasPayload);

    // A referenced zero-normal strip and an ordinary strip share one immutable
    // mesh. Zero normals have zero light dot, rim=1/8 and open shadow gates;
    // the nonzero neighbors remain shadowed and have no rim contribution.
    auto zeroRaw=geometry();for(size_t i=0;i<4;++i)zeroRaw[i].normal={0,-0.f,0};
    auto zeroMesh=f.backend.uploadRigidMesh(zeroRaw,strip);
    const auto zeroRead=f.backend.readbackRigidMeshVertices(zeroMesh);
    require(zeroRead.size()==zeroRaw.size()&&std::memcmp(zeroRead.data(),zeroRaw.data(),sizeof(zeroRaw))==0,
            "Zero-normal immutable upload changed original attribute bits");
    f.copyShadows(.75f,.75f);f.clear();const Snapshot zeroBefore(f.immediate);
    const auto zeroColor=f.backend.readbackTarget(f.color),zeroDepth=f.backend.readbackDepthTarget(f.depth);
    auto zeroPayload=f.begin();auto zeroDraw=f.draw;zeroDraw.indexCount=9;
    auto zeroMaterial=f.material(317);zeroMaterial[46][0]=1;
    f.record(zeroPayload,zeroMesh,f.vc,zeroMaterial,zeroDraw);f.backend.finishRecordingPayload(zeroPayload);
    require(Snapshot(f.immediate)==zeroBefore&&f.backend.readbackTarget(f.color)==zeroColor&&
            f.backend.readbackDepthTarget(f.depth)==zeroDepth,"Recording zero-normal geometry changed immediate outputs");
    f.execute(zeroPayload);
    f.pixels([](UINT x,UINT){return x<8?packed(317,0)|(128u<<20):packed(317,1);},[](UINT,UINT){return .5f;});
    f.backend.releaseRecordingPayload(zeroPayload);f.copyShadows(.5f,.5f);

    // Rejections must leave both the list's accounting and all immediate state
    // untouched; the same still-recording payload remains usable afterward.
    auto pending=f.begin();const auto zeroReceipt=f.backend.recordingPayloadReceipt(pending);
    f.rejected([&]{f.backend.bindRigidShadowDepth(pending,2,f.shadows[0]);},"stage0/1");
    f.rejected([&]{f.backend.bindRigidShadowDepth(pending,0,f.depth);},"1024-square");
    const auto attempt=[&](const RigidMeshDraw& d){f.backend.recordRigidMesh(pending,f.color,f.depth,mesh,*f.vs,*f.ps,f.vc,f.material(5),f.live,d);};
    f.rejected([&]{auto d=f.draw;d.startIndex=8;attempt(d);},"range");
    f.rejected([&]{auto d=f.draw;d.startIndex=uint32_t(strip.size()+1);d.indexCount=0;attempt(d);},"range");
    f.rejected([&]{auto d=f.draw;d.baseVertex=INT32_MAX;attempt(d);},"effective index range");
    f.rejected([&]{auto d=f.draw;d.primitiveType=4;attempt(d);},"primitive6");
    f.rejected([&]{auto d=f.draw;d.depthPolicy=ShadowMeshDepthPolicy::Unqualified;attempt(d);},"depth policy");
    f.rejected([&]{auto d=f.draw;d.shadowSamplePolicy=RigidShadowSamplePolicy::Unqualified;attempt(d);},"sampling policy");
    f.rejected([&]{auto d=f.draw;d.depthCompare=8;attempt(d);},"depth state");
    f.rejected([&]{auto d=f.draw;d.fill=1;attempt(d);},"fill");
    f.rejected([&]{auto d=f.draw;d.colorMask=16;attempt(d);},"color mask");
    f.rejected([&]{auto d=f.draw;d.blendEnable=1;attempt(d);},"blend");
    f.rejected([&]{auto d=f.draw;d.blendWord=0x07060706;attempt(d);},"blend");
    f.rejected([&]{auto d=f.draw;d.stencilEnable=1;attempt(d);},"stencil");
    f.rejected([&]{auto d=f.draw;d.alphaTest=1;attempt(d);},"alpha");
    f.rejected([&]{auto d=f.draw;d.multisampleMask=1;attempt(d);},"full-mask");
    f.rejected([&]{auto d=f.draw;d.depthBiasBits=0x7F800000;attempt(d);},"nonfinite");
    f.rejected([&]{auto d=f.draw;d.viewport[2]=15;attempt(d);},"viewport");
    f.rejected([&]{auto d=f.draw;d.scissorEnable=1;d.scissor[2]=17;attempt(d);},"scissor");
    f.rejected([&]{auto d=f.draw;d.samplers[0].Filter=D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;attempt(d);},"samplers");
    f.rejected([&]{auto d=f.draw;d.samplers[1].AddressU=D3D11_TEXTURE_ADDRESS_WRAP;attempt(d);},"samplers");
    f.rejected([&]{auto d=f.draw;d.shadows[1]=d.shadows[0];attempt(d);},"distinct depth owners");
    auto undersizedDepth=f.backend.createDepthTarget(32,32);
    f.rejected([&]{auto d=f.draw;d.shadows[0]=undersizedDepth;attempt(d);},"1024-square");
    f.rejected([&]{auto d=f.draw;d.viewport[2]=d.viewport[3]=1024;
        f.backend.recordRigidMesh(pending,f.sourceColor,f.shadows[0],mesh,*f.vs,*f.ps,f.vc,f.material(5),f.live,d);},"aliases");
    f.rejected([&]{f.backend.recordRigidMesh(pending,f.color,f.depth,mesh,*f.ps,*f.vs,f.vc,f.material(5),f.live,f.draw);},"exact compiled owners");
    f.rejected([&]{auto invalid=f.pc;invalid[49][2]=std::numeric_limits<float>::quiet_NaN();f.backend.updateRigidReplayConstants(f.live,f.vc,invalid);},"nonfinite");
    for(float invalidValue:{std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        f.rejected([&]{auto invalid=geometry();invalid[0].normal[0]=invalidValue;f.backend.uploadRigidMesh(invalid,strip);},"Nonfinite rigid normal");
        f.rejected([&]{auto invalid=geometry();invalid[0].position[0]=invalidValue;f.backend.uploadRigidMesh(invalid,strip);},"Nonfinite rigid position");
        f.rejected([&]{auto invalid=geometry();invalid[0].color[0]=invalidValue;f.backend.uploadRigidMesh(invalid,strip);},"Nonfinite rigid color");
        f.rejected([&]{auto invalid=geometry();invalid[0].uv[0]=invalidValue;f.backend.uploadRigidMesh(invalid,strip);},"Nonfinite rigid UV");
    }
    f.rejected([&]{auto invalid=strip;invalid[0]=8;const auto badMesh=f.backend.uploadRigidMesh(raw,invalid);
        f.backend.recordRigidMesh(pending,f.color,f.depth,badMesh,*f.vs,*f.ps,f.vc,f.material(5),f.live,f.draw);},"effective index range");
    NativeBackend other(!hardware);auto foreignMesh=other.uploadRigidMesh(raw,strip);auto foreignLive=other.createRigidReplayConstants(f.vc,f.pc);
    auto foreignShadow=other.createDepthTarget(1024,1024);
    f.rejected([&]{f.backend.bindRigidMeshVertices(pending,foreignMesh);},"another backend");
    f.rejected([&]{f.backend.bindRigidMeshDeclaration(pending,foreignMesh);},"another backend");
    f.rejected([&]{f.backend.bindRigidMeshIndices(pending,foreignMesh);},"another backend");
    f.rejected([&]{f.backend.recordRigidMesh(pending,f.color,f.depth,mesh,*f.vs,*f.ps,f.vc,f.material(5),foreignLive,f.draw);},"another backend");
    f.rejected([&]{auto d=f.draw;d.shadows[0]=foreignShadow;attempt(d);},"another device");
    const auto afterReject=f.backend.recordingPayloadReceipt(pending);
    require(afterReject.state==NativeRecordingPayloadState::Recording&&afterReject.recordedDraws==zeroReceipt.recordedDraws&&afterReject.ownedDataBytes==0,"Rejected rigid draw corrupted its payload");
    f.record(pending,mesh,f.vc,f.material(333),f.draw);f.backend.finishRecordingPayload(pending);f.clear();f.execute(pending);
    f.pixels([](UINT x,UINT){return x<8?packed(333,0):untouched;},[](UINT x,UINT){return x<8?.5f:0.f;});
    f.rejected([&]{f.backend.bindRigidMeshVertices(pending,mesh);},"not recording");
    f.backend.releaseRecordingPayload(pending);
    f.rejected([&]{f.backend.bindRigidMeshIndices(pending,mesh);},"released");

    // The alpha effect inherits an unused stage1 from preceding draws. Its
    // two observed base-level profiles must be real native descriptors; use
    // the rigid PS here to exercise them against the uniform copied depth.
    for(bool mirror:{false,true}) {
        auto state=f.draw;auto& sampler=state.samplers[1];
        sampler.Filter=mirror?D3D11_FILTER_MIN_MAG_MIP_POINT:D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU=sampler.AddressV=mirror?D3D11_TEXTURE_ADDRESS_MIRROR:D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=0;
        auto inherited=f.begin();f.record(inherited,mesh,f.vc,f.material(333),state);
        f.backend.finishRecordingPayload(inherited);f.clear();f.execute(inherited);
        f.pixels([](UINT x,UINT){return x<8?packed(333,0):untouched;},[](UINT x,UINT){return x<8?.5f:0.f;});
        f.backend.releaseRecordingPayload(inherited);
    }

    // Recorded owners outlive caller geometry aliases. A released live-values
    // handle invalidates EVERY recorded alias before any further Execute.
    std::weak_ptr<NativeRigidMesh> retainedMesh=mesh;mesh.reset();require(!retainedMesh.expired(),"Sealed list did not retain geometry ownership");
    // Cache ownership drops while the sealed list still retains the draw:
    // eviction must not destroy in-flight geometry; list release must retire it.
    NativeRecordingProbe::resetMeshCache(f.backend);
    require(!retainedMesh.expired(),"Cache eviction destroyed an in-flight retained draw");
    f.clear();f.execute(payload);f.pixels([](UINT x,UINT){return packed(x<8?64:512,0);},[](UINT,UINT){return .5f;});
    auto alias=f.live;f.backend.releaseRigidReplayConstants(alias);
    f.rejected([&]{f.backend.updateRigidReplayConstants(alias,f.vc,f.pc);},"released");
    f.rejected([&]{f.backend.executeRecordingPayload(payload);},"released");
    require(f.backend.recordingPayloadReceipt(payload).state==NativeRecordingPayloadState::Sealed,"Preparation failure destroyed a sealed native list");
    f.backend.releaseRecordingPayload(payload);require(retainedMesh.expired(),"Released, retired list leaked its geometry owner");
    f.backend.releaseRecordingContext(f.recording);checkDebug(NativeRecordingProbe::device(f.backend));
    std::printf("PASS rigid recorded mesh %s: %zu checks; %llu real recorded draws/%llu executed draws; immediate-state preservation, independent material snapshots, inherited replays, copied RRRR depth, RGB10A2/depth/stencil and rejection ownership\n",
        hardware?"hardware":"WARP",rigidChecks,static_cast<unsigned long long>(f.backend.recordingDrawCount()),static_cast<unsigned long long>(f.backend.recordingExecutedDrawCount()));
}
}
int main(int argc,char** argv)try {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    require(argc==2||(argc==3&&std::string(argv[2])=="--hardware"),"Supply analysis/simpsons.pe and optional --hardware");
    std::ifstream input(argv[1],std::ios::binary);const std::vector<uint8_t> image((std::istreambuf_iterator<char>(input)),{});
    require(image.size()==15466496,"Original flat image size differs");chocolateBindings(image);uploadCacheReuse(argc==3);run(image,argc==3);direct(image,argc==3);textured(image,argc==3);gloss(image,argc==3);multitone(image,argc==3);tangent(image,argc==3);rigidAlpha(image,argc==3);inheritedOpaque(image,argc==3);largeOwner(image,argc==3);stripBoundary(image,argc==3);vfxRigid(image,argc==3);animatedUv(image,argc==3);singleAnimatedUv(image,argc==3);flipbookVariants(image,argc==3);requireMeshGpuRetirements([](bool value,const char* message){require(value,message);});return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL rigid recorded mesh after %zu checks: %s\n",rigidChecks,error.what());return 1;}
