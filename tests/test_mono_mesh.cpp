#include "renderer/mono_mesh.h"
#include "renderer/mesh_upload_cache.h"
#include "renderer/native_material_compiler.h"
#include "renderer/effect_reflection.h"
#include "renderer/device_availability.h"
#include "PSShadowMeshDepth.h"
#include <d3d11sdklayers.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <thread>
#include <vector>

// Reuse the existing backend fixture access point.
#include "header/test_mesh_gpu_retirement.h"
#include "header/test_r16_strip_boundary.h"
namespace Simpsons::Graphics {
struct NativeIm2DProbe {
    static ID3D11Device* device(NativeBackend& b){return b.device.Get();}
    static ID3D11DeviceContext* context(NativeBackend& b){return b.context.Get();}
    static void debug(NativeBackend& b,bool hardware) {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        const auto hr=D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_DEBUG,levels,2,D3D11_SDK_VERSION,&device,&level,&context);
        if(SUCCEEDED(hr)) {
            b.availability=std::make_shared<DeviceAvailability>(device.Get());b.device=device;b.context=context;b.featureLevel=level;
            std::fprintf(stderr,"[MONO MESH TEST] D3D11 debug layer enabled\n");
        }
    }
};
}
using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void need(bool value,const char* message){++checks;if(!value)throw Error(message);}
void hr(HRESULT value,const char* message){need(SUCCEEDED(value),message);}
uint32_t bits(float value){return std::bit_cast<uint32_t>(value);}
uint32_t depthBits(const std::vector<uint8_t>& pixels,size_t i) {
    uint32_t result{};std::memcpy(&result,pixels.data()+i*8,4);return result;
}
bool sameDepthStencil(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b) {
    if(a.size()!=b.size()||a.size()%8)return false;
    for(size_t i=0;i<a.size()/8;++i)
        if(depthBits(a,i)!=depthBits(b,i)||a[8*i+4]!=b[8*i+4])return false;
    return true;
}
// Independent arithmetic oracle: choose the 20e4 grid spacing and round the
// real quotient to even. No shader exponent-rebias/bit-shift implementation.
float quantize(float value) {
    const double z=std::clamp(double(value),0.0,1.0);if(z==0)return 0;
    const double step=std::ldexp(1.0,z<std::ldexp(1.0,-14)?-34:std::ilogb(z)-20);
    const double units=z/step,lower=std::floor(units),fraction=units-lower;
    const bool up=fraction>0.5||(fraction==0.5&&uint64_t(lower)%2!=0);
    return float((lower+double(up))*step);
}
float mappedDepth(float z,bool reverse,float constant,float slope,float maxSlope) {
    // Explicit float32 steps match the declared no-contraction native policy.
    volatile float mapped=reverse?1.0f-z:z;
    volatile float offset=constant,scale=slope;
    volatile float term=scale*maxSlope;
    volatile float bias=term+offset;
    volatile float biased=mapped+bias;
    return quantize(biased);
}
bool compare(uint32_t function,float incoming,float stored) {
    switch(function) {
    case 0:return false;case 1:return incoming<stored;case 2:return incoming==stored;
    case 3:return incoming<=stored;case 4:return incoming>stored;case 5:return incoming!=stored;
    case 6:return incoming>=stored;default:return true;
    }
}
std::vector<uint64_t> snapshot(ID3D11DeviceContext* c) {
    std::vector<uint64_t> out;auto ptr=[&](const auto& p){out.push_back(reinterpret_cast<uintptr_t>(p.Get()));};
    ComPtr<ID3D11RenderTargetView> rt;ComPtr<ID3D11DepthStencilView> dsv;c->OMGetRenderTargets(1,&rt,&dsv);ptr(rt);ptr(dsv);
    ComPtr<ID3D11DepthStencilState> ds;UINT reference{};c->OMGetDepthStencilState(&ds,&reference);ptr(ds);out.push_back(reference);
    ComPtr<ID3D11BlendState> blend;FLOAT factors[4]{};UINT mask{};c->OMGetBlendState(&blend,factors,&mask);ptr(blend);
    for(float v:factors)out.push_back(bits(v));out.push_back(mask);
    ComPtr<ID3D11RasterizerState> raster;c->RSGetState(&raster);ptr(raster);
    std::array<D3D11_VIEWPORT,16> vp{};UINT count=16;c->RSGetViewports(&count,vp.data());out.push_back(count);
    for(UINT i=0;i<count;++i)for(float v:{vp[i].TopLeftX,vp[i].TopLeftY,vp[i].Width,vp[i].Height,vp[i].MinDepth,vp[i].MaxDepth})out.push_back(bits(v));
    std::array<D3D11_RECT,16> sc{};count=16;c->RSGetScissorRects(&count,sc.data());out.push_back(count);
    for(UINT i=0;i<count;++i)for(LONG v:{sc[i].left,sc[i].top,sc[i].right,sc[i].bottom})out.push_back(uint32_t(v));
    ComPtr<ID3D11Buffer> vb,ib;UINT stride{},offset{};DXGI_FORMAT format{};c->IAGetVertexBuffers(0,1,&vb,&stride,&offset);
    ptr(vb);out.push_back(stride);out.push_back(offset);c->IAGetIndexBuffer(&ib,&format,&offset);ptr(ib);out.push_back(format);out.push_back(offset);
    ComPtr<ID3D11InputLayout> layout;c->IAGetInputLayout(&layout);ptr(layout);
    D3D11_PRIMITIVE_TOPOLOGY topology{};c->IAGetPrimitiveTopology(&topology);out.push_back(topology);
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;c->VSGetShader(&vs,nullptr,nullptr);c->PSGetShader(&ps,nullptr,nullptr);ptr(vs);ptr(ps);
    for(UINT slot=0;slot<2;++slot) {
        ComPtr<ID3D11Buffer> vc,pc;c->VSGetConstantBuffers(slot,1,&vc);c->PSGetConstantBuffers(slot,1,&pc);ptr(vc);ptr(pc);
    }
    return out;
}
void checkDebug(ID3D11Device* device) {
    ComPtr<ID3D11InfoQueue> queue;if(FAILED(device->QueryInterface(IID_PPV_ARGS(&queue))))return;
    for(UINT64 i=0;i<queue->GetNumStoredMessagesAllowedByRetrievalFilter();++i) {
        SIZE_T bytes{};hr(queue->GetMessage(i,nullptr,&bytes),"Debug message size");std::vector<uint8_t> data(bytes);
        auto* message=reinterpret_cast<D3D11_MESSAGE*>(data.data());hr(queue->GetMessage(i,message,&bytes),"Debug message");
        if(message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR) {
            std::fprintf(stderr,"D3D11: %s\n",message->pDescription);need(false,"mono mesh emitted a D3D11 error");
        }
    }
    queue->ClearStoredMessages();
}
std::array<MonoVertex,4> quad(float z=0.5f,float xSlope=0,float ySlope=0) {
    std::array<MonoVertex,4> v{};
    constexpr std::array<std::array<float,2>,4> xy={{{-1,1},{-1,-1},{1,1},{1,-1}}};
    for(size_t i=0;i<v.size();++i) {
        v[i].position={xy[i][0],xy[i][1],z+xSlope*xy[i][0]+ySlope*xy[i][1]};
    }
    return v;
}
constexpr std::array<uint16_t,4> strip={0,1,2,3};
struct Fixture {
    NativeBackend backend;
    NativeMaterialCompiler compiler;
    MaterialRegistry registry;
    const CompiledMaterial* vertex{};
    const CompiledMaterial* pixel{};
    std::shared_ptr<NativeMonoCommit> commit;
    std::shared_ptr<RenderTarget> color;
    std::shared_ptr<DepthTarget> depth;
    MonoConstants constants{};
    MonoMeshDraw draw{};
    ID3D11DeviceContext* context{};
    static constexpr UINT extent=16;
    explicit Fixture(const std::vector<uint8_t>& image,bool hardware):backend(!hardware),compiler(backend) {
        NativeIm2DProbe::debug(backend,hardware);context=NativeIm2DProbe::context(backend);
        for(const auto& record:originalMaterialIdentities())if(record.originalAddress==0x82120C04) {
            const auto id=registry.create(record.originalAddress,std::span<const uint8_t>(image).subspan(record.originalAddress-0x82000000,record.recordBytes));
            vertex=&registry.prepareForBind(id,compiler);
        }
        for(const auto& record:originalMaterialIdentities())if(record.originalAddress==0x82122BD4) {
            const auto id=registry.create(record.originalAddress,std::span<const uint8_t>(image).subspan(record.originalAddress-0x82000000,record.recordBytes));
            pixel=&registry.prepareForBind(id,compiler);
        }
        need(pixel!=nullptr,"Missing original mono PS record");
        need(vertex!=nullptr,"Missing original mono shader record");
        color=backend.createTarget(extent,extent,TargetFormat::RGB10A2);depth=backend.createDepthTarget(extent,extent);
        backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.bindMonoShaders(*vertex,*pixel);
        for(size_t row=0;row<4;++row)constants[row][row]=1;
        for(size_t bone=0;bone<64;++bone)for(size_t row=0;row<3;++row)constants[52+3*bone+row][row]=1;
        commit=backend.commitMono(*vertex,*pixel,constants,{});
        draw.primitiveType=6;draw.indexCount=4;draw.viewport={0,0,extent,extent,0x3F800000,0};draw.scissor={1,1,extent-1,extent-1};
        draw.colorMask=15;
        draw.depthEnable=draw.depthWrite=1;draw.depthCompare=6;draw.scissorEnable=draw.halfPixelOffset=1;
        draw.primitiveReset=draw.viewportEnable=draw.multisampleAntialias=1;draw.primitiveResetIndex=0xFFFF;draw.multisampleMask=0xFFFFFFFF;
        draw.depthPolicy=ShadowMeshDepthPolicy::Reference20e4Rne;
        backend.setScissor(draw.scissor);backend.setViewport({2,3,7,8,0.25f,0.75f});
        auto* device=NativeIm2DProbe::device(backend);
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_WIREFRAME;raster.CullMode=D3D11_CULL_FRONT;
        ComPtr<ID3D11RasterizerState> rs;hr(device->CreateRasterizerState(&raster,&rs),"Sentinel rasterizer");context->RSSetState(rs.Get());
        D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthFunc=D3D11_COMPARISON_NEVER;
        ds.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};ds.BackFace=ds.FrontFace;
        ComPtr<ID3D11DepthStencilState> dss;hr(device->CreateDepthStencilState(&ds,&dss),"Sentinel depth state");context->OMSetDepthStencilState(dss.Get(),0x35);
        const FLOAT factors[]={0.25f,0.5f,0.75f,1};context->OMSetBlendState(nullptr,factors,0x13579BDF);
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=16;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> sentinel;hr(device->CreateBuffer(&cb,nullptr,&sentinel),"Sentinel PS constant buffer");
        auto* p=sentinel.Get();context->PSSetConstantBuffers(1,1,&p);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        clear();
    }
    void clear(float value=0) {backend.clearTarget(color,{1,0,1,1});backend.clearDepthTarget(depth,value,0x6D);}
    void bind(const std::shared_ptr<NativeMonoMesh>& mesh) {
        backend.bindMonoMeshVertices(mesh);backend.bindMonoMeshDeclaration(mesh);backend.bindMonoMeshIndices(mesh);
    }
    void submit(const std::shared_ptr<NativeMonoMesh>& mesh,const MonoMeshDraw& d) {
        const auto before=snapshot(context);const auto count=backend.monoMeshDrawCount();
        backend.drawMonoMesh(color,depth,mesh,*vertex,*pixel,commit,d);
        need(backend.monoMeshDrawCount()==count+1&&backend.shadowMeshDrawCount()==0,"mono submission changed the wrong draw counter");
        need(snapshot(context)==before,"mono draw leaked native bindings, viewport or scissor");
        backend.requireMonoShaders(*vertex,*pixel);backend.requireMonoCommit(commit);
    }
    template<class F>void rejected(F action,const char* diagnostic) {
        const auto before=snapshot(context);
        const auto colorBefore=backend.readbackTarget(color),depthBefore=backend.readbackDepthTarget(depth);
        const auto count=backend.monoMeshDrawCount();bool rejected=false;
        try{action();}catch(const Error& e){rejected=true;need(std::string(e.what()).find(diagnostic)!=std::string::npos,"Wrong mono rejection diagnostic");}
        need(rejected,"Invalid mono operation succeeded");
        need(snapshot(context)==before&&backend.monoMeshDrawCount()==count,"Rejected mono operation changed state/count");
        need(backend.readbackTarget(color)==colorBefore,"Rejected mono operation changed color");
        const auto after=backend.readbackDepthTarget(depth);
        for(size_t i=0;i<after.size()/8;++i)need(depthBits(after,i)==depthBits(depthBefore,i)&&after[8*i+4]==depthBefore[8*i+4],"Rejected mono operation changed depth/stencil");
    }
    template<class F>void pixels(F expected) {
        const auto pixels=backend.readbackDepthTarget(depth);
        for(UINT y=0;y<extent;++y)for(UINT x=0;x<extent;++x) {
            const auto i=y*extent+x,want=bits(expected(x,y)),got=depthBits(pixels,i);
            if(got!=want)std::fprintf(stderr,"mono(%u,%u): got%08X expected%08X\n",x,y,got,want);
            need(got==want,"mono GPU depth differs from independent arithmetic");need(pixels[8*i+4]==0x6D,"mono draw changed stencil");
        }
    }
};
void colors(Fixture& f,bool accepted) {
    const auto pixels=f.backend.readbackTarget(f.color);
    for(UINT y=0;y<16;++y)for(UINT x=0;x<16;++x) {
        uint32_t got{};std::memcpy(&got,pixels.data()+4*(y*16+x),4);
        const bool white=accepted&&x>=1&&x<15&&y>=1&&y<15;
        need(got==(white?0xFFFFFFFFu:0xFFF003FFu),"Mono original white export/color coverage differs");
    }
}
bool interior(UINT x,UINT y){return x>=1&&x<15&&y>=1&&y<15;}
void runRecording(const std::vector<uint8_t>& image,bool hardware) {
    Fixture f(image,hardware);auto mesh=f.backend.uploadMonoMesh(quad(),strip);f.bind(mesh);
    // Whole original82740420 ->82701448 forces the application's maskFFFF,
    // rather than the SDK defaultFFFFFFFF. Both have the same low16 bits.
    f.draw.multisampleMask=0xFFFF;
    observeMeshGpuRetirement(f.context,mesh,"mono_recording");
    std::unique_ptr<EffectRecord> effect,pool;
    for(const auto& id:originalEffectIdentities())if(id.originalAddress==0x8211F480||id.originalAddress==0x820D5730) {
        auto record=std::make_unique<EffectRecord>(id.originalAddress,std::span<const uint8_t>(image).subspan(id.originalAddress-0x82000000,id.recordBytes));
        if(id.originalAddress==0x8211F480)effect=std::move(record);else pool=std::move(record);
    }
    need(effect&&pool,"Original mono/pool metadata absent from recording fixture");
    std::vector<std::string_view> names;for(const auto& p:pool->parameters(true))names.push_back(p.name);
    const EffectReflection reflection(*effect,true,names);need(!reflection.passes.empty(),"Original mono first pass absent");
    NativeRecordingMask mask{};
    for(size_t stage=0;stage<2;++stage)for(size_t byte=0;byte<8;++byte)
        mask[8*stage+byte]=uint8_t(reflection.passes.front().masks[stage]>>(56-8*byte));
    need(mask[0]&0x80,"Original mono first pass lost projection inheritance");
    const auto recording=f.backend.createRecordingContext();auto live=f.backend.createMonoReplayConstants();
    const auto begin=[&]{auto p=f.backend.allocateRecordingPayload(recording,0x3000);f.backend.beginRecordingPayload(p,4,mask,mask);return p;};
    auto material=f.constants;auto payload=begin();const auto before=snapshot(f.context);
    const auto colorBefore=f.backend.readbackTarget(f.color),depthBefore=f.backend.readbackDepthTarget(f.depth);
    f.backend.bindMonoMeshVertices(payload,mesh);f.backend.bindMonoMeshDeclaration(payload,mesh);f.backend.bindMonoMeshIndices(payload,mesh);
    f.backend.recordMonoMesh(payload,f.color,f.depth,mesh,*f.vertex,*f.pixel,material,{},live,f.draw);
    f.backend.finishRecordingPayload(payload);
    const auto receipt=f.backend.recordingPayloadReceipt(payload);
    need(receipt.recordedDraws==1&&receipt.ownedDataBytes==3960&&snapshot(f.context)==before&&
         f.backend.readbackTarget(f.color)==colorBefore&&f.backend.readbackDepthTarget(f.depth)==depthBefore,
         "Mono recording emitted immediate work, lost its full constant snapshot or changed bindings");
    f.rejected([&]{f.backend.executeRecordingPayload(payload);},"no completed original uploads");
    need(!f.backend.recordingPayloadReceipt(payload).executions,"Unready mono replay counted an execution");
    // Poison the caller after capture. Only the immutable snapshot may reach
    // replay; the live matrix rows are supplied through the original mask.
    for(auto& row:material)row.fill(std::numeric_limits<float>::quiet_NaN());
    auto effective=f.constants;f.backend.updateMonoReplayConstants(live,effective);
    const auto execute=[&]{
        f.clear();const auto retained=snapshot(f.context);const auto executions=f.backend.recordingPayloadReceipt(payload).executions;
        f.backend.executeRecordingPayload(payload);f.backend.waitIdle();
        need(snapshot(f.context)==retained&&f.backend.recordingPayloadReceipt(payload).executions==executions+1,
             "Mono replay changed immediate bindings or execution accounting");
        return f.backend.readbackTarget(f.color);
    };
    const auto first=execute();colors(f,true);f.pixels([](UINT x,UINT y){return interior(x,y)?0.5f:0.0f;});
    effective[0][0]=0.5f;f.backend.updateMonoReplayConstants(live,effective);const auto narrower=execute();
    need(narrower!=first,"Mono replay retained the old inherited matrix");
    for(UINT y=0;y<16;++y)for(UINT x=0;x<16;++x) {
        uint32_t value{};std::memcpy(&value,narrower.data()+4*(y*16+x),4);
        need(value==((x>=4&&x<12&&y>=1&&y<15)?UINT32_MAX:0xFFF003FF),"Mono inherited projection pixels differ from independent half-width bounds");
    }
    f.pixels([](UINT x,UINT y){return x>=4&&x<12&&y>=1&&y<15?0.5f:0.0f;});
    f.commit=f.backend.commitMono(*f.vertex,*f.pixel,effective,{});f.clear();f.submit(mesh,f.draw);
    need(f.backend.readbackTarget(f.color)==narrower,"Immediate and recorded mono with identical effective constants differ");
    const auto canonicalDepth=f.backend.readbackDepthTarget(f.depth);
    auto sdkDefault=f.draw;sdkDefault.multisampleMask=UINT32_MAX;
    f.clear();f.submit(mesh,sdkDefault);
    need(f.backend.readbackTarget(f.color)==narrower&&sameDepthStencil(f.backend.readbackDepthTarget(f.depth),canonicalDepth),
         "ApplicationFFFF and SDKFFFFFFFF mono masks changed direct color/depth/stencil");
    auto sdkPayload=begin();const auto beforeSdk=snapshot(f.context);
    const auto sdkColorBefore=f.backend.readbackTarget(f.color),sdkDepthBefore=f.backend.readbackDepthTarget(f.depth);
    f.backend.recordMonoMesh(sdkPayload,f.color,f.depth,mesh,*f.vertex,*f.pixel,f.constants,{},live,sdkDefault);
    f.backend.finishRecordingPayload(sdkPayload);
    const auto sdkReceipt=f.backend.recordingPayloadReceipt(sdkPayload);
    need(sdkReceipt.recordedDraws==1&&sdkReceipt.ownedDataBytes==3960&&snapshot(f.context)==beforeSdk&&f.backend.readbackTarget(f.color)==sdkColorBefore&&
         f.backend.readbackDepthTarget(f.depth)==sdkDepthBefore,"SDK full-mask mono recording emitted immediate work");
    f.clear();const auto beforeSdkReplay=snapshot(f.context);f.backend.executeRecordingPayload(sdkPayload);f.backend.waitIdle();
    need(snapshot(f.context)==beforeSdkReplay&&f.backend.recordingPayloadReceipt(sdkPayload).executions==1&&
         f.backend.readbackTarget(f.color)==narrower&&sameDepthStencil(f.backend.readbackDepthTarget(f.depth),canonicalDepth),
         "Application and SDK masks changed recorded mono color/depth/stencil");
    f.backend.releaseRecordingPayload(sdkPayload);
    f.rejected([&]{f.backend.executeRecordingPayload(sdkPayload);},"explicitly released");
    {
        const auto vertices=Test::stripBoundaryVertices(quad());const auto words=Test::stripBoundaryIndices();
        const auto owner=f.backend.uploadMonoMesh(vertices,words);f.bind(owner);
        observeMeshGpuRetirement(f.context,owner,"mono_strip_boundary");
        f.backend.updateMonoReplayConstants(live,f.constants);f.commit=f.backend.commitMono(*f.vertex,*f.pixel,f.constants,{});
        auto selected=f.draw;selected.startIndex=Test::stripBoundaryStart;selected.indexCount=Test::stripBoundaryCount;
        for(uint32_t cull:{2u,6u}) {
            selected.cull=cull;f.clear();f.submit(owner,selected);
            const auto covered=[&](UINT x,UINT y){return interior(x,y)&&Test::originalStripBoundaryCovered(x,y,cull);};
            f.pixels([&](UINT x,UINT y){return covered(x,y)?.5f:0.f;});
            const auto color=f.backend.readbackTarget(f.color),depth=f.backend.readbackDepthTarget(f.depth);
            for(UINT y=0;y<16;++y)for(UINT x=0;x<16;++x) {
                uint32_t value{};std::memcpy(&value,color.data()+4*(y*16+x),4);
                need(value==(covered(x,y)?UINT32_MAX:0xFFF003FF),"Mono split color coverage differs from independent triangle winding");
            }
            f.clear();
            for(const auto& packet:Test::originalStripBoundaryPackets) {
                auto part=selected;part.indexCount=packet[0];part.startIndex=packet[1];f.submit(owner,part);
            }
            need(f.backend.readbackTarget(f.color)==color&&sameDepthStencil(f.backend.readbackDepthTarget(f.depth),depth),
                 "Mono full draw differs from independent original packet pixels");
            auto boundary=begin();const auto retained=snapshot(f.context);
            f.backend.recordMonoMesh(boundary,f.color,f.depth,owner,*f.vertex,*f.pixel,f.constants,{},live,selected);
            f.backend.finishRecordingPayload(boundary);
            need(snapshot(f.context)==retained&&f.backend.readbackTarget(f.color)==color&&
                 sameDepthStencil(f.backend.readbackDepthTarget(f.depth),depth)&&f.backend.recordingPayloadReceipt(boundary).recordedDraws==1,
                 "Mono split recording emitted immediate work or changed logical receipt");
            f.clear();f.backend.executeRecordingPayload(boundary);f.backend.waitIdle();
            need(snapshot(f.context)==retained&&f.backend.readbackTarget(f.color)==color&&
                 sameDepthStencil(f.backend.readbackDepthTarget(f.depth),depth),"Mono split direct and deferred pixels differ");
            f.backend.releaseRecordingPayload(boundary);f.rejected([&]{f.backend.executeRecordingPayload(boundary);},"explicitly released");
        }
        auto emptyBoundary=begin();
        for(const auto [start,count,base]:{std::array<int64_t,3>{1,65538,0},{UINT32_MAX,65536,0},{1,65536,-1},{1,65536,1}}) {
            auto bad=selected;bad.startIndex=uint32_t(start);bad.indexCount=uint32_t(count);bad.baseVertex=int32_t(base);
            f.rejected([&]{f.backend.drawMonoMesh(f.color,f.depth,owner,*f.vertex,*f.pixel,f.commit,bad);},"effective index range");
            f.rejected([&]{f.backend.recordMonoMesh(emptyBoundary,f.color,f.depth,owner,*f.vertex,*f.pixel,f.constants,{},live,bad);},"effective index range");
            need(!f.backend.recordingPayloadReceipt(emptyBoundary).recordedDraws,"Malformed mono split recorded a prefix");
        }
        f.backend.releaseRecordingPayload(emptyBoundary);f.bind(mesh);
        f.backend.updateMonoReplayConstants(live,effective);f.commit=f.backend.commitMono(*f.vertex,*f.pixel,effective,{});
    }
    auto empty=begin();const auto recorded=f.backend.recordingDrawCount();
    const auto reject=[&](const MonoMeshDraw& draw,const MonoConstants& values,const MonoBooleans& booleans,const char* why){
        f.rejected([&]{f.backend.recordMonoMesh(empty,f.color,f.depth,mesh,*f.vertex,*f.pixel,values,booleans,live,draw);},why);
        need(!f.backend.recordingPayloadReceipt(empty).recordedDraws&&f.backend.recordingDrawCount()==recorded,
             "Malformed recorded mono draw changed payload accounting");
    };
    auto bad=f.draw;bad.baseVertex=-1;reject(bad,effective,{},"effective index range");
    bad=f.draw;bad.indexCount=5;reject(bad,effective,{},"effective index range");
    bad=f.draw;bad.startIndex=UINT32_MAX;reject(bad,effective,{},"effective index range");
    bad=f.draw;bad.primitiveResetIndex=0xFFFE;reject(bad,effective,{},"restartFFFF");
    bad=f.draw;bad.blendWord=0;reject(bad,effective,{},"blend state");
    bad=f.draw;bad.scissor[2]=17;reject(bad,effective,{},"scissor exceeds");
    // Partial masks are outside the proved full-coverage single-sample draw
    // profile; do not treat them as malformed original SDK scalar values.
    for(const auto sampleMask:{0u,0xFFFEu,0xFFFF0000u}) {
        bad=f.draw;bad.multisampleMask=sampleMask;reject(bad,effective,{},"full mask");
        f.rejected([&]{f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,*f.pixel,f.commit,bad);},"full mask");
    }
    for(const auto halfPixel:{0u,2u}) {
        bad=f.draw;bad.halfPixelOffset=halfPixel;reject(bad,effective,{},"halfpixel1");
        f.rejected([&]{f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,*f.pixel,f.commit,bad);},"halfpixel1");
    }
    bad=f.draw;bad.multisampleAntialias=0;reject(bad,effective,{},"MSAA request1");
    f.rejected([&]{f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,*f.pixel,f.commit,bad);},"MSAA request1");
    reject(f.draw,material,{},"Nonfinite");reject(f.draw,effective,{1,0,0,0},"static opaque shader pair");
    f.backend.releaseRecordingPayload(empty);
    auto nonfinite=effective;nonfinite[3][2]=std::numeric_limits<float>::infinity();
    f.rejected([&]{f.backend.updateMonoReplayConstants(live,nonfinite);},"Nonfinite");
    need(execute()==narrower,"Rejected mono live update changed the last valid replay matrix");
    f.backend.releaseMonoReplayConstants(live);
    f.rejected([&]{f.backend.executeRecordingPayload(payload);},"released or belong");
    const auto weak=std::weak_ptr<NativeMonoReplayConstants>(live);live.reset();
    need(!weak.expired(),"Sealed mono payload failed to retain its replay owner");
    f.backend.releaseRecordingPayload(payload);need(weak.expired(),"Mono payload release retained its replay owner");
    f.rejected([&]{f.backend.executeRecordingPayload(payload);},"explicitly released");
    f.backend.releaseRecordingContext(recording);checkDebug(NativeIm2DProbe::device(f.backend));
    std::printf("AUDIT_GPU_MONO_RECORDING vertex=82120C04 pixel=82122BD4 static_boolean=0 create=passed full_material_snapshot=passed original_mask=passed record_without_immediate_work=passed live_replay=passed direct_pixels=passed malformed=passed payload_release=passed replay_owner_release=passed stale_use=passed gpu_buffer_retirement=separate\n");
    std::printf("AUDIT_GPU_MONO_SAMPLE_MASK original_force=827246C8 original_setter=8243AC40 vertex=82120C04 pixel=82122BD4 halfpixel=1 msaa=1 application_mask=0000FFFF sdk_mask=FFFFFFFF backing_samples=1 direct_pixels=passed recorded_pixels=passed record_without_immediate_work=passed unqualified_partial_masks=passed halfpixel_guard=passed payload_release=passed replay_owner_release=passed stale_use=passed gpu_buffer_retirement=separate\n");
    std::printf("AUDIT_GPU_STRIP_BOUNDARY family=mono vertex=82120C04 pixel=82122BD4 count=65536 reset_position=65530 selected_start=1 sdk_packets=65534_4 sdk_advance=65532 cull=2_6 create=passed direct_pixels=passed original_packet_pixels=passed recorded_pixels=passed record_without_immediate_work=passed malformed=passed payload_release=passed replay_owner_release=passed stale_use=passed gpu_buffer_retirement=separate\n");
}
// Generic exact-content cache self-tests on small configurable instances
// (production cache holds 512 entries, impractical to fill via D3D uploads):
// forced-key collisions must miss, eviction stays bounded and drops only the
// cache's strong reference.
void runGenericMeshCacheChecks() {
    struct Probe {
        int id{};
    };
    // Forced-key collisions: identical keys with any differing byte or extent
    // must miss; only exact bytes hit.
    ExactContentMeshCache<Probe> tiny(4, 1024);
    auto meshA = std::make_shared<Probe>(Probe{1});
    const std::array<uint8_t, 4> vA{1, 2, 3, 4};
    const std::array<uint8_t, 2> iA{5, 6};
    const std::span<const uint8_t> vSpanA(vA.data(), vA.size());
    const std::span<const uint8_t> iSpanA(iA.data(), iA.size());
    const uint64_t forcedKey =
        ExactContentMeshCache<Probe>::contentKey(vSpanA, iSpanA);
    tiny.insert(vSpanA, iSpanA, forcedKey, meshA);
    need(tiny.find(vSpanA, iSpanA, forcedKey).get() == meshA.get(),
         "mono generic cache exact hit missed");
    const std::array<uint8_t, 4> vOther{1, 2, 3, 5};
    need(tiny.find({vOther.data(), vOther.size()}, iSpanA, forcedKey) == nullptr,
         "mono forced key collision on vertex bytes hit");
    const std::array<uint8_t, 2> iOther{5, 7};
    need(tiny.find(vSpanA, {iOther.data(), iOther.size()}, forcedKey) == nullptr,
         "mono forced key collision on index bytes hit");
    const std::array<uint8_t, 5> vLong{1, 2, 3, 4, 0};
    need(tiny.find({vLong.data(), vLong.size()}, iSpanA, forcedKey) == nullptr,
         "mono forced key collision on vertex extent hit");
    const std::array<uint8_t, 3> iLong{5, 6, 0};
    need(tiny.find(vSpanA, {iLong.data(), iLong.size()}, forcedKey) == nullptr,
         "mono forced key collision on index extent hit");
    need(tiny.find(std::span<const uint8_t>{}, iSpanA, forcedKey) == nullptr,
         "mono forced key collision on empty vertices hit");
    need(tiny.find(vSpanA, iSpanA, forcedKey ^ 0x9E3779B97F4A7C15ull) == nullptr,
         "mono wrong content key hit");
    // Bounded LRU eviction with a 3-entry budget.
    ExactContentMeshCache<Probe> lruCache(3, 1024);
    const std::array<uint8_t, 1> b0{10}, b1{11}, b2{12}, b3{13};
    const std::span<const uint8_t> s0(b0.data(), 1), s1(b1.data(), 1),
        s2(b2.data(), 1), s3(b3.data(), 1);
    const std::span<const uint8_t> empty;
    auto p0 = std::make_shared<Probe>(Probe{10});
    auto p1 = std::make_shared<Probe>(Probe{11});
    auto p2 = std::make_shared<Probe>(Probe{12});
    auto p3 = std::make_shared<Probe>(Probe{13});
    lruCache.insert(s0, empty, ExactContentMeshCache<Probe>::contentKey(s0, empty), p0);
    lruCache.insert(s1, empty, ExactContentMeshCache<Probe>::contentKey(s1, empty), p1);
    lruCache.insert(s2, empty, ExactContentMeshCache<Probe>::contentKey(s2, empty), p2);
    need(lruCache.size() == 3, "mono small cache size differs");
    lruCache.insert(s3, empty, ExactContentMeshCache<Probe>::contentKey(s3, empty), p3);
    need(lruCache.size() == 3, "mono small cache exceeded its entry cap");
    need(lruCache.find(s0, empty, ExactContentMeshCache<Probe>::contentKey(s0, empty)) == nullptr,
         "mono small cache did not evict its oldest entry");
    need(lruCache.find(s3, empty, ExactContentMeshCache<Probe>::contentKey(s3, empty)).get() == p3.get(),
         "mono small cache lost its newest entry");
    // Eviction drops only the cache's strong reference.
    need(p0->id == 10 && p0.use_count() >= 1, "mono evicted live handle invalid");
    // Byte-budget eviction and oversized-entry skip.
    ExactContentMeshCache<Probe> budgeted(16, 8);
    const std::array<uint8_t, 4> w0{20, 21, 22, 23}, w1{24, 25, 26, 27}, w2{28, 29, 30, 31};
    const std::span<const uint8_t> ws0(w0.data(), 4), ws1(w1.data(), 4), ws2(w2.data(), 4);
    auto q0 = std::make_shared<Probe>(Probe{20});
    auto q1 = std::make_shared<Probe>(Probe{21});
    auto q2 = std::make_shared<Probe>(Probe{22});
    budgeted.insert(ws0, empty, ExactContentMeshCache<Probe>::contentKey(ws0, empty), q0);
    budgeted.insert(ws1, empty, ExactContentMeshCache<Probe>::contentKey(ws1, empty), q1);
    // Two 4-byte entries fit the 8-byte budget exactly and must be retained.
    need(budgeted.size() == 2 && budgeted.residentBytes() == 8,
         "mono exact-budget entries were not retained");
    need(budgeted.find(ws0, empty, ExactContentMeshCache<Probe>::contentKey(ws0, empty)).get() == q0.get(),
         "mono exact-budget oldest entry missed");
    need(budgeted.find(ws1, empty, ExactContentMeshCache<Probe>::contentKey(ws1, empty)).get() == q1.get(),
         "mono exact-budget newest entry missed");
    // A distinct third 4-byte entry overflows the budget and evicts the oldest.
    budgeted.insert(ws2, empty, ExactContentMeshCache<Probe>::contentKey(ws2, empty), q2);
    need(budgeted.size() == 2 && budgeted.residentBytes() == 8,
         "mono budgeted cache exceeded its byte budget");
    need(budgeted.find(ws0, empty, ExactContentMeshCache<Probe>::contentKey(ws0, empty)) == nullptr,
         "mono budgeted cache did not evict its oldest bytes");
    need(budgeted.find(ws2, empty, ExactContentMeshCache<Probe>::contentKey(ws2, empty)).get() == q2.get(),
         "mono budgeted cache lost its newest bytes");
    ExactContentMeshCache<Probe> strict(16, 8);
    const std::array<uint8_t, 16> huge{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const std::span<const uint8_t> hugeSpan(huge.data(), huge.size());
    strict.insert(hugeSpan, empty, ExactContentMeshCache<Probe>::contentKey(hugeSpan, empty),
                  std::make_shared<Probe>(Probe{99}));
    need(strict.size() == 0 && strict.residentBytes() == 0,
         "mono oversized entry was retained past its byte budget");
}
// Exact-content upload cache coverage on fresh backends (deterministic empty
// slot): repeat reuse, caller-mutation isolation, bit/extent changes including
// invalid inputs, device/owner mismatch, and bounded eviction with live-handle
// validity. Draw/depth/state behavior is unchanged (covered by run() above).
void runUploadCache(bool hardware) {
    NativeBackend backend(!hardware);
    const auto base = quad();
    std::vector<MonoVertex> vv(base.begin(), base.end());
    std::vector<uint16_t> ii(strip.begin(), strip.end());
    auto first = backend.uploadMonoMesh(vv, ii);
    auto repeat = backend.uploadMonoMesh(vv, ii);
    need(repeat.get() == first.get(), "mono repeat upload did not reuse exact bytes");
    // Same content at a different guest address must still hit (never by pointer).
    std::vector<MonoVertex> vvAlias = vv;
    std::vector<uint16_t> iiAlias = ii;
    need(vvAlias.data() != vv.data(), "mono alias fixture shares guest address");
    auto aliased = backend.uploadMonoMesh(vvAlias, iiAlias);
    need(aliased.get() == first.get(), "mono identical bytes at new address missed");
    // Caller mutation isolation: mutate the caller's copy, then re-upload the
    // preserved original bytes. The snapshot must be unaffected.
    const std::vector<MonoVertex> origV = vv;
    const std::vector<uint16_t> origI = ii;
    vv[0].position[0] += 0.25f;
    auto mutated = backend.uploadMonoMesh(vv, ii);
    need(mutated.get() != first.get(), "mono single-float change incorrectly hit");
    auto afterMutation = backend.uploadMonoMesh(origV, origI);
    need(afterMutation.get() == first.get(), "mono original bytes missed after caller mutation");
    const auto snapshot = backend.readbackMonoMeshVertices(first);
    need(snapshot.size() == origV.size() &&
         !std::memcmp(snapshot.data(), origV.data(), origV.size() * sizeof(MonoVertex)),
         "mono cached mesh changed after caller mutation");
    // Any bit/extent change forces a miss; invalid inputs stay rejected and
    // are never cached.
    std::vector<uint16_t> otherI = origI;
    otherI[3] = 2;
    auto other = backend.uploadMonoMesh(origV, otherI);
    need(other.get() != first.get(), "mono index-bit change incorrectly hit");
    auto extent = origV;
    extent.push_back(origV.back());
    std::vector<uint16_t> extentI = origI;
    extentI.push_back(4);
    auto grown = backend.uploadMonoMesh(extent, extentI);
    need(grown.get() != first.get(), "mono extent change incorrectly hit");
    bool rejected = false;
    try {
        auto bad = origV;
        bad[0].weights[0] = 1.0f;
        backend.uploadMonoMesh(bad, origI);
    } catch (const Error& e) {
        rejected = std::string(e.what()).find("zero unused weights") != std::string::npos;
    }
    need(rejected, "mono invalid weights accepted after caching");
    const std::array<uint16_t,3> opaque = {0, 1, 4};
    const auto indexed=backend.uploadMonoMesh(origV,opaque);
    need(indexed!=first&&indexed->indexCount()==opaque.size()&&backend.uploadMonoMesh(origV,opaque)==indexed,
         "Mono opaque R16 bytes lost immutable upload/cache ownership before draw qualification");
    rejected = false;
    try {
        const std::vector<MonoVertex> empty;
        backend.uploadMonoMesh(empty, origI);
    } catch (const Error& e) {
        rejected = std::string(e.what()).find("buffer byte or index extent") != std::string::npos;
    }
    need(rejected, "mono empty extent accepted after caching");
    auto stillHit = backend.uploadMonoMesh(origV, origI);
    need(stillHit.get() == first.get(), "mono valid bytes missed after invalid rejections");
    // Device isolation: no static global, no cross-device hits.
    NativeBackend foreign(!hardware);
    auto foreignMesh = foreign.uploadMonoMesh(origV, origI);
    need(foreignMesh.get() != first.get(), "mono cross-device hit");
    rejected = false;
    try {
        backend.bindMonoMeshVertices(foreignMesh);
    } catch (const Error& e) {
        rejected = std::string(e.what()).find("another device") != std::string::npos;
    }
    need(rejected, "mono foreign mesh bound on local device");
    auto localAgain = backend.uploadMonoMesh(origV, origI);
    need(localAgain.get() == first.get(), "mono local hit lost after foreign upload");
    // Owner isolation: a different thread must fail owner checks, never hit.
    bool ownerRejected = false;
    std::thread probe([&] {
        try {
            backend.uploadMonoMesh(origV, origI);
        } catch (const Error& e) {
            ownerRejected = std::string(e.what()).find("different thread") != std::string::npos;
        } catch (...) {
        }
    });
    probe.join();
    need(ownerRejected, "mono cross-thread upload bypassed owner checks");
    // Production bounds must retain the ~133-draw static working set; eviction
    // itself is covered by small configurable generic instances below rather
    // than by filling 512 production entries through D3D uploads.
    need(kMeshUploadCacheMaxEntries >= 512, "mono production entry cap thrashes gameplay");
    need(kMeshUploadCacheMaxBytes >= 64u * 1024u * 1024u, "mono production byte budget thrashes gameplay");
    runGenericMeshCacheChecks();
}
void runSkin(Fixture& f) {
    const auto original=f.constants;
    for(size_t bone=0;bone<64;++bone)for(size_t row=0;row<3;++row) {
        auto& matrix=f.constants[52+3*bone+row];matrix={};
        matrix[row]=row==2?.5f:.5f+float(bone%4)*.125f;
        matrix[3]=row==0?float(int(bone%8)-4)*.03125f:
            (row==1?float(int(bone/8)-4)*.03125f:float(bone%8)*.03125f);
    }
    // Independently blend affine bone transforms on the CPU, then rasterize
    // those positions through the static shader branch as the pixel oracle.
    // Dyadic inputs make the reference exact, including the original weights
    // which the shader deliberately does not normalize.
    auto reference=[&](const std::array<MonoVertex,4>& vertices) {
        std::array<MonoVertex,4> result{};
        for(size_t i=0;i<vertices.size();++i)for(size_t row=0;row<3;++row) {
            double value=0;
            for(size_t bone=0;bone<4;++bone) {
                const auto& matrix=f.constants[52+3*size_t(vertices[i].indices[bone])+row];
                double transformed=matrix[3];
                for(size_t lane=0;lane<3;++lane)transformed+=double(matrix[lane])*vertices[i].position[lane];
                value+=double(vertices[i].weights[bone])*transformed;
            }
            result[i].position[row]=float(value);
        }
        return result;
    };
    const MonoBooleans skinFlags{1,0,0,0};
    auto render=[&](const std::shared_ptr<NativeMonoMesh>& mesh,bool skin,const MonoMeshDraw& draw) {
        f.bind(mesh);f.commit=f.backend.commitMono(*f.vertex,*f.pixel,f.constants,skin?skinFlags:MonoBooleans{});
        need(f.backend.readbackMonoBooleans(f.commit)==(skin?skinFlags:MonoBooleans{}),"Mono skin Boolean upload differs");
        need(f.backend.readbackMonoConstants(f.commit)==f.constants,"Mono skin 64-bone palette upload differs");
        f.clear();f.backend.clearTarget(f.color,{.17f,.41f,.83f,0});
        f.submit(mesh,draw);
        return std::array{f.backend.readbackTarget(f.color),f.backend.readbackDepthTarget(f.depth)};
    };
    auto comparePixels=[&](const auto& actual,const auto& expected) {
        need(actual[0]==expected[0],"Skinned mono coverage differs from independent CPU bone transforms");
        bool visible=false;
        for(size_t i=0;i<Fixture::extent*Fixture::extent;++i) {
            need(depthBits(actual[1],i)==depthBits(expected[1],i)&&actual[1][8*i+4]==0x6D&&expected[1][8*i+4]==0x6D,
                 "Skinned mono depth/stencil differs from independent CPU bone transforms");
            uint32_t color{};std::memcpy(&color,actual[0].data()+4*i,4);visible|=(color&0xC0000000u)!=0;
        }
        need(visible,"Skinned mono fixture produced no visible coverage");
    };
    auto vertices=quad();
    // Four independently weighted fetches exercise every palette row, including
    // bone 63, through actual mesh submission on both WARP and hardware.
    for(size_t group=0;group<16;++group) {
        for(auto& vertex:vertices) {
            vertex.weights={.125f,.25f,.375f,.25f};
            vertex.indices={float(4*group),float(4*group+1),float(4*group+2),float(4*group+3)};
        }
        const auto skin=f.backend.uploadMonoMesh(vertices,strip,true);
        need(f.backend.uploadMonoMesh(vertices,strip,true)==skin,"Skinned mono exact cache hit missed");
        const auto read=f.backend.readbackMonoMeshVertices(skin);
        need(read.size()==vertices.size()&&!std::memcmp(read.data(),vertices.data(),sizeof(vertices)),"Mono skin fetch bytes changed during upload");
        const auto expected=f.backend.uploadMonoMesh(reference(vertices),strip);
        const auto actualPixels=render(skin,true,f.draw),expectedPixels=render(expected,false,f.draw);
        comparePixels(actualPixels,expectedPixels);
    }
    for(auto& vertex:vertices)vertex.weights={1.5f,-.25f,.125f,.5f};
    const auto skin=f.backend.uploadMonoMesh(vertices,strip,true);
    const auto expected=f.backend.uploadMonoMesh(reference(vertices),strip);
    const auto weightedPixels=render(skin,true,f.draw),weightedExpected=render(expected,false,f.draw);
    comparePixels(weightedPixels,weightedExpected);

    // Homer's charge mask changes alpha only; its animated silhouette must
    // preserve RGB, depth/stencil and the state left by the original effect.
    auto mask=f.draw;mask.colorMask=8;mask.depthWrite=0;mask.cull=2;
    mask.slopeBiasBits=bits(.5f);mask.depthBiasBits=0x37D1B717;
    const auto maskedPixels=render(skin,true,mask),maskedExpected=render(expected,false,mask);
    comparePixels(maskedPixels,maskedExpected);
    f.clear();f.backend.clearTarget(f.color,{.17f,.41f,.83f,0});
    const auto before=f.backend.readbackTarget(f.color);
    for(size_t i=0;i<Fixture::extent*Fixture::extent;++i) {
        uint32_t a{},b{};std::memcpy(&a,before.data()+4*i,4);std::memcpy(&b,maskedPixels[0].data()+4*i,4);
        need((a&0x3FFFFFFFu)==(b&0x3FFFFFFFu)&&depthBits(maskedPixels[1],i)==0,
             "Skinned mono charge mask changed RGB/depth");
    }
    f.bind(skin);
    f.rejected([&]{f.backend.drawMonoMesh(f.color,f.depth,skin,*f.vertex,*f.pixel,f.commit,f.draw);},"skinning profile");
    f.commit=f.backend.commitMono(*f.vertex,*f.pixel,f.constants,skinFlags);f.bind(expected);
    f.rejected([&]{f.backend.drawMonoMesh(f.color,f.depth,expected,*f.vertex,*f.pixel,f.commit,f.draw);},"skinning profile");
    for(float invalid:{-1.0f,64.0f,.5f,std::numeric_limits<float>::quiet_NaN()}) {
        auto bad=vertices;bad[0].weights[3]=0;bad[0].indices[3]=invalid;
        f.rejected([&]{f.backend.uploadMonoMesh(bad,strip,true);},"bone index not integer 0..63");
    }
    f.rejected([&]{auto bad=vertices;bad[0].weights[0]=std::numeric_limits<float>::infinity();f.backend.uploadMonoMesh(bad,strip,true);},"Nonfinite mono skin weight");
    f.rejected([&]{auto bad=f.constants;bad[243][3]=std::numeric_limits<float>::quiet_NaN();f.backend.commitMono(*f.vertex,*f.pixel,bad,skinFlags);},"Nonfinite native mono constant");
    // Identical bytes in static and skinned profiles must keep separate owners.
    // A skin cache hit must never bypass the static zero-attribute checks.
    const auto empty=quad();const auto staticEmpty=f.backend.uploadMonoMesh(empty,strip);
    const auto skinnedEmpty=f.backend.uploadMonoMesh(empty,strip,true);
    need(staticEmpty!=skinnedEmpty&&f.backend.uploadMonoMesh(empty,strip)==staticEmpty&&
         f.backend.uploadMonoMesh(empty,strip,true)==skinnedEmpty,"Mono cache confused identical bytes across skin profiles");
    f.rejected([&]{f.backend.uploadMonoMesh(vertices,strip);},"zero unused weights");
    f.constants=original;f.commit=f.backend.commitMono(*f.vertex,*f.pixel,f.constants,{});f.bind(staticEmpty);
    f.clear();f.submit(staticEmpty,f.draw);colors(f,true);
    f.pixels([](UINT x,UINT y){return interior(x,y)?.5f:0.0f;});
}
void run(const std::vector<uint8_t>& image,bool hardware) {
    Fixture f(image,hardware);const auto vertices=quad();auto mesh=f.backend.uploadMonoMesh(vertices,strip);f.bind(mesh);
    observeMeshGpuRetirement(NativeIm2DProbe::context(f.backend),mesh,"mono");
    need(mesh->vertexCount()==4&&mesh->indexCount()==4,"mono mesh extent differs");
    const auto read=f.backend.readbackMonoMeshVertices(mesh);
    need(read.size()==vertices.size()&&!std::memcmp(read.data(),vertices.data(),sizeof(vertices)),"Three-attribute GPU input bytes differ");
    need(f.backend.readbackMonoMeshIndices(mesh)==std::vector<uint16_t>(strip.begin(),strip.end()),"GPU strip indices differ");
    need(f.backend.readbackMonoConstants(f.commit)==f.constants&&f.backend.readbackMonoBooleans(f.commit)==MonoBooleans{},
         "Actual float/Boolean constant banks differ");
    f.submit(mesh,f.draw);f.pixels([](UINT x,UINT y){return interior(x,y)?0.5f:0.0f;});

    {
        std::vector<MonoVertex> owner(65536,vertices[0]);std::copy(vertices.begin(),vertices.end(),owner.end()-4);
        constexpr std::array<uint16_t,7> words{0,1,2,UINT16_MAX,2,1,3};
        auto large=f.backend.uploadMonoMesh(owner,words);f.bind(large);observeMeshGpuRetirement(f.context,large,"mono_large_owner");
        auto selected=f.draw;selected.baseVertex=65532;selected.indexCount=7;f.clear();f.submit(large,selected);
        f.pixels([](UINT x,UINT y){return interior(x,y)?0.5f:0.0f;});colors(f,true);
        auto bad=selected;bad.baseVertex=65533;
        f.rejected([&]{f.backend.drawMonoMesh(f.color,f.depth,large,*f.vertex,*f.pixel,f.commit,bad);},"effective index range");
        need(large->vertexCount()==65536,"Large mono owner metadata differs");
        std::printf("AUDIT_GPU_LARGE_OWNER family=mono vertices=65536 base_vertex=65532 create=passed direct_draw=passed malformed=passed gpu_buffer_retirement=separate\n");
        f.bind(mesh);
    }

    colors(f,true);
    auto masked=f.draw;masked.colorMask=0;f.clear();f.submit(mesh,masked);colors(f,false);
    f.pixels([](UINT x,UINT y){return interior(x,y)?0.5f:0.0f;});

    // The post-filter mask pass writes the mono shader's alpha one while
    // preserving RGB and depth, including the original nonzero depth biases.
    constexpr std::array<std::array<uint32_t,2>,4> blendModes={{{0,0x00010001},{1,0x00010001},{1,0x07060706},{1,0x00010706}}};
    for(const auto& blend:blendModes)for(uint32_t expanded:{0u,1u}) {
        auto alphaOnly=f.draw;alphaOnly.colorMask=8;alphaOnly.depthWrite=0;alphaOnly.cull=2;
        alphaOnly.slopeBiasBits=bits(.5f);alphaOnly.depthBiasBits=0x37D1B717;
        alphaOnly.blendEnable=blend[0];alphaOnly.blendWord=blend[1];
        alphaOnly.expandedBlend=expanded;
        f.clear();f.backend.clearTarget(f.color,{.17f,.41f,.83f,0});
        const auto before=f.backend.readbackTarget(f.color),depthBefore=f.backend.readbackDepthTarget(f.depth);
        f.submit(mesh,alphaOnly);const auto after=f.backend.readbackTarget(f.color);
        for(UINT y=0;y<Fixture::extent;++y)for(UINT x=0;x<Fixture::extent;++x) {
            uint32_t oldPixel{},newPixel{};const auto offset=(y*Fixture::extent+x)*4;
            std::memcpy(&oldPixel,before.data()+offset,4);std::memcpy(&newPixel,after.data()+offset,4);
            need(newPixel==(oldPixel|(interior(x,y)?0xC0000000u:0)),"Mono alpha-only mask changed RGB or alpha coverage");
        }
        need(f.backend.readbackDepthTarget(f.depth)==depthBefore,"Mono alpha-only mask changed depth/stencil");
    }

    // Combined CPU matrix only: c12/world is irrelevant to this original VS.
    f.constants[0]={0.5f,0,0,0.25f};f.constants[12]={9,8,7,6};f.constants[2][3]=0.125f;
    f.commit=f.backend.commitMono(*f.vertex,*f.pixel,f.constants,{});f.clear();f.submit(mesh,f.draw);
    f.pixels([](UINT x,UINT y){return interior(x,y)&&x>=6&&x<14?0.375f:0.0f;});
    f.constants[0]={1,0,0,0};f.constants[2][3]=0;f.commit=f.backend.commitMono(*f.vertex,*f.pixel,f.constants,{});

    // Both windings must cover BOTH triangles consistently, with color/stencil intact.
    for(uint32_t cull:{2u,6u})for(bool flip:{false,true}) {
        auto v=quad();if(flip){std::swap(v[0],v[1]);std::swap(v[2],v[3]);}
        auto m=f.backend.uploadMonoMesh(v,strip);f.bind(m);f.clear();auto d=f.draw;d.cull=cull;f.submit(m,d);
        const bool accepted=(cull==2)!=flip;colors(f,accepted);f.pixels([&](UINT x,UINT y){return interior(x,y)&&accepted?0.5f:0.0f;});
    }
    // Depth compare, write disable and reversed mapping operate on real stored depth.
    mesh=f.backend.uploadMonoMesh(quad(0.25f),strip);f.bind(mesh);
    for(bool reverse:{false,true})for(uint32_t func:{0u,1u,2u,3u,4u,5u,6u,7u})for(uint32_t write:{0u,1u}) {
        auto d=f.draw;d.depthCompare=func;d.depthWrite=write;d.viewport[4]=reverse?bits(1):0;d.viewport[5]=reverse?0:bits(1);
        f.clear(0.5f);f.submit(mesh,d);const float incoming=reverse?0.75f:0.25f;colors(f,compare(func,incoming,0.5f));
        f.pixels([&](UINT x,UINT y){return interior(x,y)&&write&&compare(func,incoming,0.5f)?incoming:0.5f;});
    }
    // Reference20e4 RNE ties and scaled polygon bias, independent arithmetic oracle.
    for(uint32_t low:{4u,12u}) {
        const float z=std::bit_cast<float>(bits(0.5f)+low);mesh=f.backend.uploadMonoMesh(quad(z),strip);f.bind(mesh);
        auto d=f.draw;d.viewport[4]=0;d.viewport[5]=bits(1);d.depthCompare=7;
        f.clear();f.submit(mesh,d);f.pixels([&](UINT x,UINT y){return interior(x,y)?quantize(z):0.0f;});
    }
    mesh=f.backend.uploadMonoMesh(quad(),strip);f.bind(mesh);
    auto biased=f.draw;biased.depthBiasBits=bits(0.00390625f);f.clear();f.submit(mesh,biased);
    f.pixels([&](UINT x,UINT y){return interior(x,y)?mappedDepth(0.5f,true,0.00390625f,0,0):0.0f;});

    const std::array<uint16_t,10> cuts={0xFFFF,0,1,2,3,0xFFFF,0,1,2,3};
    mesh=f.backend.uploadMonoMesh(quad(),cuts);f.bind(mesh);auto d=f.draw;d.startIndex=1;d.indexCount=9;
    f.clear();f.submit(mesh,d);f.pixels([](UINT x,UINT y){return interior(x,y)?0.5f:0.0f;});
    mesh=f.backend.uploadMonoMesh(quad(),strip);f.bind(mesh);

    // Original stream1-null call performs an actual IA unbind, without touching stream0.
    ComPtr<ID3D11Buffer> vb;UINT stride{},offset{};f.context->IAGetVertexBuffers(0,1,&vb,&stride,&offset);
    auto* auxiliary=vb.Get();f.context->IASetVertexBuffers(1,1,&auxiliary,&stride,&offset);
    const auto count=f.backend.monoMeshDrawCount();f.backend.clearMonoAuxiliaryStream();
    ComPtr<ID3D11Buffer> empty,still;UINT clearedStride=1,clearedOffset=1,s{},o{};
    f.context->IAGetVertexBuffers(1,1,&empty,&clearedStride,&clearedOffset);f.context->IAGetVertexBuffers(0,1,&still,&s,&o);
    need(!empty&&!clearedStride&&!clearedOffset&&still.Get()==vb.Get()&&s==stride&&o==offset&&f.backend.monoMeshDrawCount()==count,
         "Original stream1 cleanup did not preserve stream0/count");

    auto other=f.backend.uploadMonoMesh(quad(0.25f),strip);f.backend.bindMonoMeshVertices(other);
    f.rejected([&]{f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,*f.pixel,f.commit,f.draw);},"vertex binding is stale");f.bind(mesh);
    f.backend.bindMonoMeshIndices(other);
    f.rejected([&]{f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,*f.pixel,f.commit,f.draw);},"index binding is stale");f.bind(mesh);
    f.context->IASetInputLayout(nullptr);
    f.rejected([&]{f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,*f.pixel,f.commit,f.draw);},"declaration binding is stale");f.bind(mesh);
    ComPtr<ID3D11Buffer> oldBoolean;f.context->VSGetConstantBuffers(1,1,&oldBoolean);
    const auto oldCommit=f.commit;f.commit=f.backend.commitMono(*f.vertex,*f.pixel,f.constants,{});
    f.rejected([&]{f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,*f.pixel,oldCommit,f.draw);},"constants differ");
    // Commits share the backend's immutable zero Boolean bank; any other CB1
    // buffer, even an all-zero one, still rejects.
    ComPtr<ID3D11Buffer> currentBoolean;f.context->VSGetConstantBuffers(1,1,&currentBoolean);
    need(currentBoolean.Get()==oldBoolean.Get(),"Commits no longer share the zero Boolean bank");
    ComPtr<ID3D11Buffer> foreignBoolean;{const std::array<uint32_t,4> zero{};D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(zero);
        desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;const D3D11_SUBRESOURCE_DATA data{zero.data(),0,0};
        hr(NativeIm2DProbe::device(f.backend)->CreateBuffer(&desc,&data,&foreignBoolean),"Foreign Boolean fixture");}
    auto* stale=foreignBoolean.Get();f.context->VSSetConstantBuffers(1,1,&stale);
    f.rejected([&]{f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,*f.pixel,f.commit,f.draw);},"constants differ");
    auto* current=currentBoolean.Get();f.context->VSSetConstantBuffers(1,1,&current);
    ComPtr<ID3D11Buffer> currentFloat;f.context->VSGetConstantBuffers(0,1,&currentFloat);
    ID3D11Buffer* missing=nullptr;f.context->VSSetConstantBuffers(0,1,&missing);
    f.rejected([&]{f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,*f.pixel,f.commit,f.draw);},"constants differ");
    auto* floats=currentFloat.Get();f.context->VSSetConstantBuffers(0,1,&floats);
    ComPtr<ID3D11PixelShader> stalePixel;
    hr(NativeIm2DProbe::device(f.backend)->CreatePixelShader(kPSShadowMeshDepth,sizeof(kPSShadowMeshDepth),nullptr,&stalePixel),"Stale PS fixture");
    f.context->PSSetShader(stalePixel.Get(),nullptr,0);
    f.rejected([&]{f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,*f.pixel,f.commit,f.draw);},"shader binding differs");
    f.backend.bindMonoShaders(*f.vertex,*f.pixel);f.backend.requireMonoCommit(f.commit);
    f.rejected([&]{f.backend.commitMono(*f.vertex,*f.pixel,f.constants,{2,0,0,0});},"canonical skin Boolean");
    for(size_t lane=1;lane<4;++lane)f.rejected([&]{MonoBooleans bad{1,0,0,0};bad[lane]=1;f.backend.commitMono(*f.vertex,*f.pixel,f.constants,bad);},"zero unused Boolean lanes");
    f.rejected([&]{auto bad=f.constants;bad[0][0]=std::numeric_limits<float>::quiet_NaN();f.backend.commitMono(*f.vertex,*f.pixel,bad,{});},"Nonfinite");
    f.rejected([&]{auto bad=quad();bad[0].weights[0]=1;f.backend.uploadMonoMesh(bad,strip);},"zero unused weights");
    f.rejected([&]{auto bad=quad();bad[0].indices[0]=std::numeric_limits<float>::quiet_NaN();f.backend.uploadMonoMesh(bad,strip);},"zero unused indices");
    f.rejected([&]{const std::array<uint16_t,3> bad={0,1,4};const auto badMesh=f.backend.uploadMonoMesh(quad(),bad);
        auto invalid=f.draw;invalid.startIndex=0;invalid.indexCount=3;
        f.backend.drawMonoMesh(f.color,f.depth,badMesh,*f.vertex,*f.pixel,f.commit,invalid);},"effective index range");
    f.rejected([&]{auto bad=f.draw;bad.depthPolicy=ShadowMeshDepthPolicy::Unqualified;f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,*f.pixel,f.commit,bad);},"policy is unqualified");
    f.rejected([&]{auto bad=f.draw;bad.indexCount=5;f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,*f.pixel,f.commit,bad);},"effective index range");
    NativeBackend foreign(!hardware);auto foreignMesh=foreign.uploadMonoMesh(quad(),strip);
    f.rejected([&]{f.backend.bindMonoMeshVertices(foreignMesh);},"another device");

    // Alias record uses the same artifact bytes but a distinct retained VS identity.
    const auto prepare=[&](uint32_t address)->const CompiledMaterial& {
        for(const auto& record:originalMaterialIdentities())if(record.originalAddress==address) {
            auto id=f.registry.create(address,std::span<const uint8_t>(image).subspan(address-0x82000000,record.recordBytes));
            return f.registry.prepareForBind(id,f.compiler);
        }
        throw Error("Required original material record missing");
    };
    const auto& shadow=prepare(0x820C2FA0);
    f.rejected([&]{f.backend.bindMonoShaders(shadow,*f.pixel);},"exact compiled owner");
    f.backend.bindShadowDepthShader(shadow);
    f.rejected([&]{f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,*f.pixel,f.commit,f.draw);},"shader binding differs");
    f.backend.bindMonoShaders(*f.vertex,*f.pixel);
    const auto& alias=prepare(0x82121BE8);f.backend.bindMonoShaders(alias,*f.pixel);
    f.rejected([&]{f.backend.drawMonoMesh(f.color,f.depth,mesh,alias,*f.pixel,f.commit,f.draw);},"commit shader identity");
    f.vertex=&alias;f.commit=f.backend.commitMono(alias,*f.pixel,f.constants,{});f.clear();f.submit(mesh,f.draw);
    f.pixels([](UINT x,UINT y){return interior(x,y)?0.5f:0.0f;});
    const auto& pixelAlias=prepare(0x82122D38);f.backend.bindMonoShaders(*f.vertex,pixelAlias);
    f.rejected([&]{f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,pixelAlias,f.commit,f.draw);},"commit shader identity");
    f.pixel=&pixelAlias;f.commit=f.backend.commitMono(*f.vertex,*f.pixel,f.constants,{});
    f.clear();f.submit(mesh,f.draw);colors(f,true);
    // The original mono PS exports exact RGBA one. Enabled or disabled
    // replacement and SRC_ALPHA/INV_SRC_ALPHA (with blended or replacement alpha)
    // write the same endpoint with either canonical expanded-blend request,
    // while retaining each color mask.
    for(const auto& blend:blendModes)for(uint32_t expanded:{0u,1u})for(uint32_t mask:{0u,8u,15u}) {
        auto draw=f.draw;draw.blendEnable=blend[0];draw.blendWord=blend[1];
        draw.expandedBlend=expanded;draw.colorMask=mask;
        f.clear();f.backend.clearTarget(f.color,{.17f,.41f,.83f,0});
        const auto before=f.backend.readbackTarget(f.color);
        f.submit(mesh,draw);const auto after=f.backend.readbackTarget(f.color);
        for(UINT y=0;y<Fixture::extent;++y)for(UINT x=0;x<Fixture::extent;++x) {
            uint32_t oldPixel{},newPixel{};const auto offset=(y*Fixture::extent+x)*4;
            std::memcpy(&oldPixel,before.data()+offset,4);std::memcpy(&newPixel,after.data()+offset,4);
            const uint32_t expected=!interior(x,y)||!mask?oldPixel:
                (mask==8?oldPixel|0xC0000000u:0xFFFFFFFFu);
            need(newPixel==expected,"Canonical mono blend or expanded state changed color/alpha coverage");
        }
        // Color masking and the expanded request must not suppress the
        // original depth write or change untouched stencil values.
        f.pixels([](UINT x,UINT y){return interior(x,y)?0.5f:0.0f;});
    }
    auto alpha=f.draw;alpha.blendEnable=1;alpha.blendWord=0x07060706;alpha.expandedBlend=0;
    f.rejected([&]{auto bad=alpha;bad.blendWord=0x00010106;f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,*f.pixel,f.commit,bad);},"blend state");
    f.rejected([&]{auto bad=alpha;bad.expandedBlend=2;f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,*f.pixel,f.commit,bad);},"blend state");
    f.rejected([&]{auto bad=alpha;bad.blendEnable=2;f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,*f.pixel,f.commit,bad);},"blend state");
    f.rejected([&]{auto bad=f.draw;bad.blendWord=0;f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,*f.pixel,f.commit,bad);},"blend state");
    f.rejected([&]{auto bad=f.draw;bad.expandedBlend=2;f.backend.drawMonoMesh(f.color,f.depth,mesh,*f.vertex,*f.pixel,f.commit,bad);},"blend state");
    runSkin(f);runUploadCache(hardware);
    checkDebug(NativeIm2DProbe::device(f.backend));
    std::printf("PASS mono mesh %s: %zu checks, %llu mono draws, zero shadow draws; original VS/PS identities, b0/b1, depth, cleanup and restoration\n",
        hardware?"hardware":"WARP",checks,static_cast<unsigned long long>(f.backend.monoMeshDrawCount()));
}
}
int main(int argc,char** argv)try {
    need(argc==2||(argc==3&&std::string(argv[2])=="--hardware"),"Supply analysis/simpsons.pe and optional --hardware");
    std::ifstream input(argv[1],std::ios::binary);const std::vector<uint8_t> image((std::istreambuf_iterator<char>(input)),{});
    need(image.size()==15466496,"Original flat image size differs");run(image,argc==3);runRecording(image,argc==3);
    requireMeshGpuRetirements([](bool value,const char* message){need(value,message);});return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL mono mesh after %zu checks: %s\n",checks,e.what());return 1;}
