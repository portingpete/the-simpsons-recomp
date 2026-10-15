#include "renderer/zprepass_mesh.h"
#include "header/test_graphics_device.h"
#include "renderer/temporal_scene.h"
#include "renderer/mesh_upload_cache.h"
#include "header/test_owned_mesh_cache.h"
#include "renderer/native_material_compiler.h"
#include "renderer/device_availability.h"
#include "renderer/d3d_call_stats.h"
#include "PSShadowMeshDepth.h"
#include "renderer/rigid_mesh.h"
#include <d3d11sdklayers.h>
#include <d3d11_4.h>
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

// Existing diagnostic counters observe the actual device calls, so identical
// COM objects returned by driver interning cannot disguise repeated creation.
extern "C" uint64_t gD3DDeviceCounts[64];

// Reuse the existing backend fixture access point.
#include "header/test_mesh_gpu_retirement.h"
#include "header/test_r16_strip_boundary.h"
namespace Simpsons::Graphics {
struct NativeIm2DProbe {
    static void installTemporalBackgroundOutput(NativeBackend& b,const std::shared_ptr<RenderTarget>& output,uint32_t width,uint32_t height,ID3D11Texture2D* actualTexture) {
        b.tagRenderExtent(actualTexture,width,height);
        b.temporalScene=std::make_shared<TemporalScenePipeline>(b.device.Get(),b.context.Get(),DlssExtent{width,height,output->pixelWidth(),output->pixelHeight()});
        b.temporalOutput=output;
    }
    static void resetMeshCache(NativeBackend& b){b.zprepassMeshCache_.reset();}
    static void installTemporalPipeline(NativeBackend& b,uint32_t width,uint32_t height) {
        // Exercise the actual renderer pipeline/owners without claiming an
        // NGX evaluation. Real SDK integration remains test_dlss's scope.
        b.temporalScene=std::make_shared<TemporalScenePipeline>(b.device.Get(),b.context.Get(),
            DlssExtent{width,height,width,height});
    }
    static DlssFrame frame(NativeBackend& b){return b.temporalScene->frame();}
    static std::array<ID3D11Texture2D*,2> temporalAttachments(NativeBackend& b) {
        return {b.temporalScene->motion(),b.temporalScene->biasCurrent()};
    }
    static void markReconstructedForGateTest(NativeBackend& b){b.temporalScene->markReconstructed();}
    static void resetSceneCache(NativeBackend& b){b.resetSceneStateCacheForDevice();}
    static ComPtr<ID3D11Buffer> cachedDepth(NativeBackend& b,uint32_t reverse,uint32_t constantBits,uint32_t slopeBits) {
        for(const auto& entry:b.sceneDepthConstants)
            if(entry.used&&entry.reverse==reverse&&entry.constantBits==constantBits&&entry.slopeBits==slopeBits)return entry.buffer;
        return {};
    }
    static ID3D11Device* device(NativeBackend& b){return b.device.Get();}
    static ID3D11DeviceContext* context(NativeBackend& b){return b.context.Get();}
    static void replaceDevice(NativeBackend& b,bool hardware) {
        NativeBackend replacement(!hardware);
        b.availability=replacement.availability;b.device=replacement.device;
        b.context=replacement.context;b.featureLevel=replacement.featureLevel;
    }
    static void debug(NativeBackend& b,bool hardware) {
        if(testUsesVulkan())return; // D3D11 debug layer: a D3D11-only diagnostic.
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        const auto hr=D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_DEBUG,levels,2,D3D11_SDK_VERSION,&device,&level,&context);
        if(SUCCEEDED(hr)) {
            b.availability=std::make_shared<DeviceAvailability>(device.Get());b.device=device;b.context=context;b.featureLevel=level;
            std::fprintf(stderr,"[ZPREPASS MESH TEST] D3D11 debug layer enabled\n");
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
    for(UINT slot=0;slot<2;++slot) {
        ComPtr<ID3D11Buffer> vb;UINT stride{},offset{};c->IAGetVertexBuffers(slot,1,&vb,&stride,&offset);
        ptr(vb);out.push_back(stride);out.push_back(offset);
    }
    ComPtr<ID3D11Buffer> ib;UINT offset{};DXGI_FORMAT format{};
    c->IAGetIndexBuffer(&ib,&format,&offset);ptr(ib);out.push_back(format);out.push_back(offset);
    ComPtr<ID3D11InputLayout> layout;c->IAGetInputLayout(&layout);ptr(layout);
    D3D11_PRIMITIVE_TOPOLOGY topology{};c->IAGetPrimitiveTopology(&topology);out.push_back(topology);
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;c->VSGetShader(&vs,nullptr,nullptr);c->PSGetShader(&ps,nullptr,nullptr);ptr(vs);ptr(ps);
    for(UINT slot=0;slot<2;++slot) {
        ComPtr<ID3D11Buffer> vc,pc;c->VSGetConstantBuffers(slot,1,&vc);c->PSGetConstantBuffers(slot,1,&pc);ptr(vc);ptr(pc);
    }
    ComPtr<ID3D11Buffer> temporal;c->VSGetConstantBuffers(11,1,&temporal);ptr(temporal);
    ComPtr<ID3D11DeviceContext1> ranged;
    if(SUCCEEDED(c->QueryInterface(IID_PPV_ARGS(&ranged)))) {
        UINT first{},count{};temporal.Reset();ranged->VSGetConstantBuffers1(11,1,&temporal,&first,&count);
        out.push_back(first);out.push_back(count);
    }
    return out;
}
void checkDebug(ID3D11Device* device) {
    ComPtr<ID3D11InfoQueue> queue;if(FAILED(device->QueryInterface(IID_PPV_ARGS(&queue))))return;
    for(UINT64 i=0;i<queue->GetNumStoredMessagesAllowedByRetrievalFilter();++i) {
        SIZE_T bytes{};hr(queue->GetMessage(i,nullptr,&bytes),"Debug message size");std::vector<uint8_t> data(bytes);
        auto* message=reinterpret_cast<D3D11_MESSAGE*>(data.data());hr(queue->GetMessage(i,message,&bytes),"Debug message");
        if(message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR) {
            std::fprintf(stderr,"D3D11: %s\n",message->pDescription);need(false,"Z-prepass mesh emitted a D3D11 error");
        }
    }
    queue->ClearStoredMessages();
}
std::array<ZPrepassVertex,4> quad(float z=0.5f,float xSlope=0,float ySlope=0) {
    std::array<ZPrepassVertex,4> v{};
    constexpr std::array<std::array<float,2>,4> xy={{{-1,1},{-1,-1},{1,1},{1,-1}}};
    for(size_t i=0;i<v.size();++i) {
        v[i].position={xy[i][0],xy[i][1],z+xSlope*xy[i][0]+ySlope*xy[i][1]};
    }
    return v;
}
constexpr std::array<uint16_t,4> strip={0,1,2,3};
std::vector<uint8_t> actualBufferBytes(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Buffer* source) {
    D3D11_BUFFER_DESC desc{};source->GetDesc(&desc);const UINT bytes=desc.ByteWidth;
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;hr(device->CreateBuffer(&desc,nullptr,&staging),"Packed input staging allocation");
    context->CopyResource(staging.Get(),source);D3D11_MAPPED_SUBRESOURCE mapped{};
    hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Packed input staging map");
    struct Unmap {ID3D11DeviceContext* context;ID3D11Buffer* buffer;~Unmap(){context->Unmap(buffer,0);}} unmap{context,staging.Get()};
    need(mapped.pData&&mapped.RowPitch>=bytes,"Packed input staging extent differs");
    std::vector<uint8_t> result(bytes);std::memcpy(result.data(),mapped.pData,bytes);return result;
}
ComPtr<ID3D11Buffer> packedInputs(NativeBackend& backend,const std::shared_ptr<NativeZPrepassMesh>& mesh) {
    auto* context=NativeIm2DProbe::context(backend);auto* device=NativeIm2DProbe::device(backend);
    ComPtr<ID3D11Buffer> positions,zeros;UINT positionStride{},zeroStride{},positionOffset{},zeroOffset{};
    context->IAGetVertexBuffers(0,1,&positions,&positionStride,&positionOffset);
    context->IAGetVertexBuffers(1,1,&zeros,&zeroStride,&zeroOffset);
    need(positions&&zeros&&positionStride==12&&!positionOffset&&!zeroStride&&!zeroOffset,
         "Packed Z-prepass position/zero stream bindings differ");
    for(const auto& buffer:{positions,zeros}) {
        D3D11_BUFFER_DESC desc{};ComPtr<ID3D11Device> owner;buffer->GetDesc(&desc);buffer->GetDevice(&owner);
        need(owner.Get()==device&&desc.Usage==D3D11_USAGE_IMMUTABLE&&desc.BindFlags==D3D11_BIND_VERTEX_BUFFER&&
             !desc.CPUAccessFlags&&!desc.MiscFlags&&!desc.StructureByteStride&&
             desc.ByteWidth==(buffer.Get()==positions.Get()?mesh->vertexCount()*12:104),
             "Packed Z-prepass immutable GPU descriptor/device differs");
    }
    const auto bytes=actualBufferBytes(device,context,zeros.Get());
    need(bytes.size()==104&&std::all_of(bytes.begin(),bytes.end(),[](uint8_t value){return value==0;}),
         "Actual shared GPU zero record is not bitwise positive zero");
    const auto logical=backend.readbackZPrepassMeshVertices(mesh);
    const auto positionBytes=actualBufferBytes(device,context,positions.Get());
    need(logical.size()==mesh->vertexCount()&&positionBytes.size()==logical.size()*12,"Packed readback logical extent differs");
    for(size_t i=0;i<logical.size();++i) {
        need(!std::memcmp(logical[i].position.data(),positionBytes.data()+12*i,12),"Logical position readback did not use actual packed GPU bytes");
        need(!std::memcmp(reinterpret_cast<const uint8_t*>(&logical[i])+12,bytes.data(),104),
             "Logical zero readback did not use actual shared GPU bytes");
    }
    return zeros;
}
void runZeroBankRetirement(bool hardware) {
    ComPtr<ID3D11Buffer> observed;
    std::shared_ptr<NativeZPrepassMesh> live;
    {
        NativeBackend backend(!hardware);live=backend.uploadZPrepassMesh(quad(),strip);
        backend.bindZPrepassMeshVertices(live);observed=packedInputs(backend,live);
        backend.clearZPrepassAuxiliaryStream();
        NativeIm2DProbe::resetMeshCache(backend);NativeIm2DProbe::replaceDevice(backend,hardware);
        const auto replacement=backend.uploadZPrepassMesh(quad(.25f),strip);
        backend.bindZPrepassMeshVertices(replacement);
        need(packedInputs(backend,replacement).Get()!=observed.Get(),"Replacement device retained the old static zero bank");
    }
    // The old context/backend/cache/device slot have retired. An original live
    // mesh must still pin its bank independently until its final release.
    need(bool(observed),"Static zero bank lifetime probe lost its diagnostic reference");
    const ULONG liveReferences=observed->AddRef();observed->Release();
    need(liveReferences>2,"A live static mesh did not retain its retired-device zero bank");
    live.reset();
    need(observed&&observed.Detach()->Release()==0,"Backend/mesh destruction retained the actual GPU zero bank");
    std::printf("AUDIT_GPU_STATIC_ZERO_BANK immutable=passed actual_bytes=104 logical_readback=passed live_owner_after_reset=passed final_release=passed\n");
}
struct Fixture {
    NativeBackend backend;
    NativeMaterialCompiler compiler;
    MaterialRegistry registry;
    const CompiledMaterial* vertex{};
    std::shared_ptr<NativeZPrepassCommit> commit;
    std::shared_ptr<RenderTarget> color;
    std::shared_ptr<DepthTarget> depth;
    ZPrepassConstants constants{};
    ShadowMeshDraw draw{};
    ID3D11DeviceContext* context{};
    static constexpr UINT extent=16;
    explicit Fixture(const std::vector<uint8_t>& image,bool hardware,bool debugLayer=true):backend(!hardware),compiler(backend) {
        if(debugLayer)NativeIm2DProbe::debug(backend,hardware);
        context=NativeIm2DProbe::context(backend);
        for(const auto& record:originalMaterialIdentities())if(record.originalAddress==0x8214A8A4) {
            const auto id=registry.create(record.originalAddress,std::span<const uint8_t>(image).subspan(record.originalAddress-0x82000000,record.recordBytes));
            vertex=&registry.prepareForBind(id,compiler);
        }
        need(vertex!=nullptr,"Missing original Z-prepass shader record");
        color=backend.createTarget(extent,extent,TargetFormat::RGB10A2);
        depth=backend.createDepthTarget(extent,extent);
        backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.bindZPrepassShader(*vertex);
        for(size_t row=0;row<4;++row)constants[row][row]=1;
        for(size_t bone=0;bone<64;++bone)for(size_t row=0;row<3;++row)constants[52+3*bone+row][row]=1;
        commit=backend.commitZPrepass(*vertex,constants,{});
        draw.primitiveType=6;draw.indexCount=4;draw.viewport={0,0,extent,extent,0x3F800000,0};draw.scissor={1,1,extent-1,extent-1};
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
    void bind(const std::shared_ptr<NativeZPrepassMesh>& mesh) {
        backend.bindZPrepassMeshVertices(mesh);backend.bindZPrepassMeshDeclaration(mesh);backend.bindZPrepassMeshIndices(mesh);
    }
    void submit(const std::shared_ptr<NativeZPrepassMesh>& mesh,const ShadowMeshDraw& d) {
        const auto before=snapshot(context);const auto count=backend.zprepassMeshDrawCount();
        const auto colorBefore=backend.readbackTarget(color);
        backend.drawZPrepassMesh(color,depth,mesh,*vertex,commit,d);
        need(backend.zprepassMeshDrawCount()==count+1&&backend.shadowMeshDrawCount()==0,"Z-prepass submission changed the wrong draw counter");
        need(snapshot(context)==before,"Z-prepass draw leaked native bindings, viewport or scissor");
        need(backend.readbackTarget(color)==colorBefore,"Depth-only Z-prepass draw changed color");
        backend.requireZPrepassShader(*vertex);backend.requireZPrepassCommit(commit);
    }
    template<class F>void rejected(F action,const char* diagnostic) {
        const auto before=snapshot(context);
        const auto colorBefore=backend.readbackTarget(color),depthBefore=backend.readbackDepthTarget(depth);
        const auto count=backend.zprepassMeshDrawCount();bool rejected=false;
        try{action();}catch(const Error& e){rejected=true;need(std::string(e.what()).find(diagnostic)!=std::string::npos,"Wrong Z-prepass rejection diagnostic");}
        need(rejected,"Invalid Z-prepass operation succeeded");
        need(snapshot(context)==before&&backend.zprepassMeshDrawCount()==count,"Rejected Z-prepass operation changed state/count");
        need(backend.readbackTarget(color)==colorBefore,"Rejected Z-prepass operation changed color");
        const auto after=backend.readbackDepthTarget(depth);
        for(size_t i=0;i<after.size()/8;++i)need(depthBits(after,i)==depthBits(depthBefore,i)&&after[8*i+4]==depthBefore[8*i+4],"Rejected Z-prepass operation changed depth/stencil");
    }
    template<class F>void pixels(F expected) {
        const auto pixels=backend.readbackDepthTarget(depth);
        for(UINT y=0;y<extent;++y)for(UINT x=0;x<extent;++x) {
            const auto i=y*extent+x,want=bits(expected(x,y)),got=depthBits(pixels,i);
            if(got!=want)std::fprintf(stderr,"Z-prepass(%u,%u): got%08X expected%08X\n",x,y,got,want);
            need(got==want,"Z-prepass GPU depth differs from independent arithmetic");need(pixels[8*i+4]==0x6D,"Z-prepass draw changed stencil");
        }
    }
};
bool interior(UINT x,UINT y){return x>=1&&x<15&&y>=1&&y<15;}
// Test only: observe this owned immediate context's actual indexed draw, even
// when the one-shot call-counter installation observed an earlier context.
// Forward every argument through its saved entry (including an existing counter
// stub), and restore the original vtable before releasing the strong owner.
class DrawDepthCapture {
    using Draw=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);
    inline static DrawDepthCapture* active{};
    ComPtr<ID3D11DeviceContext> context;
    // Keep every queried interface alive through restoration. A derived COM
    // interface may share the base pointer and therefore this patched vtable.
    ComPtr<ID3D11DeviceContext1> context1;
    ComPtr<ID3D11DeviceContext2> context2;
    ComPtr<ID3D11DeviceContext3> context3;
    ComPtr<ID3D11DeviceContext4> context4;
    void*** slot;
    void** original;
    std::vector<void*> cloned;
    template<class T>void preserveSurface(ComPtr<T>& queried,size_t sdkSlots,size_t& cloneSlots) {
        const HRESULT result=context.As(&queried);
        need(result==E_NOINTERFACE||SUCCEEDED(result),"Static draw observer extension query failed");
        if(SUCCEEDED(result)&&reinterpret_cast<void*>(queried.Get())==reinterpret_cast<void*>(context.Get()))
            cloneSlots=std::max(cloneSlots,sdkSlots);
    }
    static void STDMETHODCALLTYPE observe(ID3D11DeviceContext* context,UINT count,UINT first,INT base) {
        auto& capture=*active;
        if(context==capture.context.Get()) {
            context->PSGetConstantBuffers(1,1,&capture.buffer);++capture.draws;
        }
        reinterpret_cast<Draw>(capture.original[12])(context,count,first,base);
    }
public:
    ComPtr<ID3D11Buffer> buffer;
    UINT draws{};
    explicit DrawDepthCapture(ID3D11DeviceContext* value):context(value),slot(reinterpret_cast<void***>(value)),original(value?*slot:nullptr) {
        need(context&&context->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE&&!active,"Static draw depth observer requires one owned immediate context");
        // Installed Windows SDK10.0.26100.0 C vtable declarations include
        // 115/134/144/147/149 slots for Context/Context1/2/3/4 respectively.
        // VSGetConstantBuffers1 is slot125: truncating to the base115 surface
        // invalidates the ranged-state oracle when Context1 shares this pointer.
        size_t cloneSlots=115;
        preserveSurface(context1,134,cloneSlots);preserveSurface(context2,144,cloneSlots);
        preserveSurface(context3,147,cloneSlots);preserveSurface(context4,149,cloneSlots);
        cloned.resize(cloneSlots); // All queries/allocation precede publication.
        std::copy_n(original,cloned.size(),cloned.begin());
        need(cloned[12]!=nullptr,"Static draw depth observer lost its original indexed draw");
        cloned[12]=reinterpret_cast<void*>(&observe);active=this;*slot=cloned.data();
    }
    ~DrawDepthCapture(){*slot=original;active=nullptr;}
    DrawDepthCapture(const DrawDepthCapture&)=delete;
    DrawDepthCapture& operator=(const DrawDepthCapture&)=delete;
    void reset(){buffer.Reset();draws=0;}
};
void depthBytes(NativeBackend& backend,ID3D11Buffer* buffer,const ShadowMeshDraw& draw) {
    need(buffer!=nullptr,"Actual static draw did not bind immutable depth constants");
    ComPtr<ID3D11Device> owner;D3D11_BUFFER_DESC desc{};buffer->GetDevice(&owner);buffer->GetDesc(&desc);
    need(owner.Get()==NativeIm2DProbe::device(backend)&&desc.ByteWidth==16&&desc.Usage==D3D11_USAGE_IMMUTABLE&&
         desc.BindFlags==D3D11_BIND_CONSTANT_BUFFER&&!desc.CPUAccessFlags&&!desc.MiscFlags&&!desc.StructureByteStride,
         "Actual static depth constants descriptor/device differs");
    const auto bytes=actualBufferBytes(owner.Get(),NativeIm2DProbe::context(backend),buffer);
    const std::array<uint32_t,4> expected{uint32_t(draw.viewport[4]==0x3F800000&&draw.viewport[5]==0),
                                          draw.depthBiasBits,draw.slopeBiasBits,0};
    need(bytes.size()==sizeof(expected)&&!std::memcmp(bytes.data(),expected.data(),sizeof(expected)),
         "Actual static depth GPU bytes changed float bits or reserved zero");
}
void runDepthConstantReuse(const std::vector<uint8_t>& image,bool hardware) {
    Fixture f(image,hardware,false);installD3DCallStats(f.context,NativeIm2DProbe::device(f.backend));
    const auto first=f.backend.uploadZPrepassMesh(quad(),strip);
    const auto second=f.backend.uploadZPrepassMesh(quad(.25f),strip);
    const auto unused=f.backend.uploadZPrepassMesh(quad(.75f),strip);
    DrawDepthCapture capture(f.context);
    const auto submit=[&](const std::shared_ptr<NativeZPrepassMesh>& mesh,const ShadowMeshDraw& draw,uint64_t creates,float z) {
        f.bind(mesh);f.clear();capture.reset();const auto before=gD3DDeviceCounts[3];
        f.submit(mesh,draw);
        need(gD3DDeviceCounts[3]==before+creates,"Static immutable depth reuse made the wrong CreateBuffer count");
        need(capture.draws==1&&capture.buffer,"Static depth observer did not see exactly one original indexed draw");
        depthBytes(f.backend,capture.buffer.Get(),draw);
        const bool reverse=draw.viewport[4]==0x3F800000&&draw.viewport[5]==0;
        const float expected=mappedDepth(z,reverse,std::bit_cast<float>(draw.depthBiasBits),std::bit_cast<float>(draw.slopeBiasBits),0);
        f.pixels([&](UINT x,UINT y){return interior(x,y)?expected:0.0f;});
        return capture.buffer;
    };
    auto original=submit(first,f.draw,1,.5f);
    need(submit(second,f.draw,0,.25f).Get()==original.Get(),"Different static meshes did not share exact immutable depth bits");
    need(submit(first,f.draw,0,.5f).Get()==original.Get(),"Static local depth cache did not preserve its fast path");
    auto signedZero=f.draw;signedZero.depthBiasBits=0x80000000;
    const auto signedOwner=submit(first,signedZero,1,.5f);
    need(signedOwner.Get()!=original.Get(),"Static depth constant cache collapsed signed zero");
    need(submit(second,signedZero,0,.25f).Get()==signedOwner.Get(),"Static signed-zero depth key failed cross-mesh reuse");
    auto biased=f.draw;biased.depthBiasBits=bits(1.0f/256.0f);
    need(submit(first,biased,1,.5f).Get()!=original.Get(),"Static nonzero depth bias incorrectly reused zero bits");
    auto slope=f.draw;slope.slopeBiasBits=bits(1.0f/128.0f);
    need(submit(first,slope,1,.5f).Get()!=original.Get(),"Static nonzero slope bias incorrectly reused zero bits");
    auto forward=f.draw;forward.viewport[4]=0;forward.viewport[5]=bits(1);
    need(submit(first,forward,1,.5f).Get()!=original.Get(),"Static depth direction incorrectly reused reversed bits");
    // More than eight distinct global keys evict the original backend entry.
    // First mesh still owns its local reference and must issue no new upload.
    for(unsigned i=1;i<=9;++i) {
        auto varied=f.draw;varied.slopeBiasBits=bits(float(i)/32.0f);submit(second,varied,1,.25f);
    }
    need(!NativeIm2DProbe::cachedDepth(f.backend,1,0,0),"Depth fixture failed to evict its original bounded global key");
    auto* originalBuffer=original.Get();
    capture.reset();
    need(original.Detach()->Release()>0,"Global eviction lost the immutable static depth owner without a diagnostic reference");
    need(submit(first,f.draw,0,.5f).Get()==originalBuffer,"Global eviction replaced an original local immutable depth owner");
    // Neither the observer nor a diagnostic ComPtr may keep the original key
    // alive while the backend cache is reset. Reacquire its actual bound owner
    // only after the original mesh submits through its independent local cache.
    capture.reset();
    NativeIm2DProbe::resetSceneCache(f.backend);
    need(!NativeIm2DProbe::cachedDepth(f.backend,1,0,0),"Scene cache reset retained a depth key");
    original=submit(first,f.draw,0,.5f);
    need(original.Get()==originalBuffer,"Scene reset invalidated the live local immutable depth owner");
    need(submit(unused,f.draw,1,.75f).Get()!=original.Get(),"New mesh reused a retired global immutable depth owner");
    depthBytes(f.backend,original.Get(),f.draw);
    const auto beforeDevice=NativeIm2DProbe::device(f.backend);
    NativeIm2DProbe::replaceDevice(f.backend,hardware);
    ComPtr<ID3D11Device> retainedDevice;original->GetDevice(&retainedDevice);
    need(retainedDevice.Get()==beforeDevice&&retainedDevice.Get()!=NativeIm2DProbe::device(f.backend),
         "Device replacement changed a retained immutable static depth owner");
    // Read retained bytes through the original device/context, never the new
    // backend. The original native context is pinned for this lifetime probe.
    ComPtr<ID3D11DeviceContext> originalContext;retainedDevice->GetImmediateContext(&originalContext);
    const auto retained=actualBufferBytes(retainedDevice.Get(),originalContext.Get(),original.Get());
    const std::array<uint32_t,4> expected{1,0,0,0};
    need(retained.size()==sizeof(expected)&&!std::memcmp(retained.data(),expected.data(),sizeof(expected)),
         "Device replacement changed retained actual depth GPU bytes");
    std::printf("AUDIT_GPU_STATIC_DEPTH_REUSE meshes=2 immutable_bytes=16 first_create=1 second_create=0 signed_zero=passed exact_gpu_bytes=passed bounded_eviction=passed scene_reset=passed retained_device_owner=passed state_restore=passed\n");
}
// Generic exact-content cache self-tests on small configurable instances
// (production cache holds 512 entries, impractical to fill via D3D uploads):
// forced-key collisions must miss, eviction stays bounded and drops only the
// cache's strong reference.
void runGenericMeshCacheChecks() {
    runOwnedMeshCacheChecks(need);
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
         "Z-prepass generic cache exact hit missed");
    const std::array<uint8_t, 4> vOther{1, 2, 3, 5};
    need(tiny.find({vOther.data(), vOther.size()}, iSpanA, forcedKey) == nullptr,
         "Z-prepass forced key collision on vertex bytes hit");
    const std::array<uint8_t, 2> iOther{5, 7};
    need(tiny.find(vSpanA, {iOther.data(), iOther.size()}, forcedKey) == nullptr,
         "Z-prepass forced key collision on index bytes hit");
    const std::array<uint8_t, 5> vLong{1, 2, 3, 4, 0};
    need(tiny.find({vLong.data(), vLong.size()}, iSpanA, forcedKey) == nullptr,
         "Z-prepass forced key collision on vertex extent hit");
    const std::array<uint8_t, 3> iLong{5, 6, 0};
    need(tiny.find(vSpanA, {iLong.data(), iLong.size()}, forcedKey) == nullptr,
         "Z-prepass forced key collision on index extent hit");
    need(tiny.find(std::span<const uint8_t>{}, iSpanA, forcedKey) == nullptr,
         "Z-prepass forced key collision on empty vertices hit");
    need(tiny.find(vSpanA, iSpanA, forcedKey ^ 0x9E3779B97F4A7C15ull) == nullptr,
         "Z-prepass wrong content key hit");
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
    need(lruCache.size() == 3, "Z-prepass small cache size differs");
    lruCache.insert(s3, empty, ExactContentMeshCache<Probe>::contentKey(s3, empty), p3);
    need(lruCache.size() == 3, "Z-prepass small cache exceeded its entry cap");
    need(lruCache.find(s0, empty, ExactContentMeshCache<Probe>::contentKey(s0, empty)) == nullptr,
         "Z-prepass small cache did not evict its oldest entry");
    need(lruCache.find(s3, empty, ExactContentMeshCache<Probe>::contentKey(s3, empty)).get() == p3.get(),
         "Z-prepass small cache lost its newest entry");
    // Eviction drops only the cache's strong reference.
    need(p0->id == 10 && p0.use_count() >= 1, "Z-prepass evicted live handle invalid");
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
         "Z-prepass exact-budget entries were not retained");
    need(budgeted.find(ws0, empty, ExactContentMeshCache<Probe>::contentKey(ws0, empty)).get() == q0.get(),
         "Z-prepass exact-budget oldest entry missed");
    need(budgeted.find(ws1, empty, ExactContentMeshCache<Probe>::contentKey(ws1, empty)).get() == q1.get(),
         "Z-prepass exact-budget newest entry missed");
    // A distinct third 4-byte entry overflows the budget and evicts the oldest.
    budgeted.insert(ws2, empty, ExactContentMeshCache<Probe>::contentKey(ws2, empty), q2);
    need(budgeted.size() == 2 && budgeted.residentBytes() == 8,
         "Z-prepass budgeted cache exceeded its byte budget");
    need(budgeted.find(ws0, empty, ExactContentMeshCache<Probe>::contentKey(ws0, empty)) == nullptr,
         "Z-prepass budgeted cache did not evict its oldest bytes");
    need(budgeted.find(ws2, empty, ExactContentMeshCache<Probe>::contentKey(ws2, empty)).get() == q2.get(),
         "Z-prepass budgeted cache lost its newest bytes");
    ExactContentMeshCache<Probe> strict(16, 8);
    const std::array<uint8_t, 16> huge{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const std::span<const uint8_t> hugeSpan(huge.data(), huge.size());
    strict.insert(hugeSpan, empty, ExactContentMeshCache<Probe>::contentKey(hugeSpan, empty),
                  std::make_shared<Probe>(Probe{99}));
    need(strict.size() == 0 && strict.residentBytes() == 0,
         "Z-prepass oversized entry was retained past its byte budget");
}
// Exact-content upload cache coverage on fresh backends (deterministic empty
// slot): repeat reuse, caller-mutation isolation, bit/extent changes including
// invalid inputs, device/owner mismatch, and bounded eviction with live-handle
// validity. Draw/depth/state behavior is unchanged (covered by run() above).
void runOwnedUpload(bool hardware) {
    NativeBackend backend(!hardware);
    const auto input=quad();
    const std::vector<ZPrepassVertex> expected(input.begin(),input.end());
    const std::vector<uint16_t> expectedIndices(strip.begin(),strip.end());
    auto vertices=expected;auto indices=expectedIndices;
    auto mesh=backend.uploadZPrepassMeshOwned(vertices,indices);
    need(vertices.empty()&&indices==expectedIndices,"Owned Z-prepass miss did not consume only vertices");
    backend.bindZPrepassMeshVertices(mesh);auto zeroBank=packedInputs(backend,mesh);
    vertices.assign(expected.begin(),expected.end());vertices[0].position[0]+=2;indices[0]=3;
    need(backend.uploadZPrepassMesh(expected,expectedIndices)==mesh,"Owned Z-prepass exact bytes missed legacy lookup");
    const auto read=backend.readbackZPrepassMeshVertices(mesh);
    need(read.size()==expected.size()&&!std::memcmp(read.data(),expected.data(),expected.size()*sizeof(ZPrepassVertex))&&
         backend.readbackZPrepassMeshIndices(mesh)==expectedIndices,"Owned Z-prepass upload changed immutable GPU bytes");
    auto hit=expected;const auto* hitPointer=hit.data();
    need(backend.uploadZPrepassMeshOwned(hit,expectedIndices)==mesh&&hit.data()==hitPointer&&
         !std::memcmp(hit.data(),expected.data(),expected.size()*sizeof(ZPrepassVertex)),
         "Owned Z-prepass hit consumed caller vertices");
    auto bad=expected;bad[0].weights[0]=1;const auto* badPointer=bad.data();bool rejected=false;
    try{backend.uploadZPrepassMeshOwned(bad,expectedIndices);}catch(const Error&){rejected=true;}
    need(rejected&&bad.data()==badPointer&&bad.size()==expected.size()&&bad[0].weights[0]==1,
         "Owned Z-prepass rejection consumed caller vertices");
    auto wrongThread=expected;const auto* threadPointer=wrongThread.data();bool ownerRejected=false;
    std::thread probe([&]{try{backend.uploadZPrepassMeshOwned(wrongThread,expectedIndices);}catch(const Error&){ownerRejected=true;}});
    probe.join();need(ownerRejected&&wrongThread.data()==threadPointer&&wrongThread.size()==expected.size(),
                     "Owned Z-prepass thread rejection consumed vertices");
    NativeBackend foreign(!hardware);auto foreignVertices=expected;
    const auto foreignMesh=foreign.uploadZPrepassMeshOwned(foreignVertices,expectedIndices);rejected=false;
    try{backend.bindZPrepassMeshVertices(foreignMesh);}catch(const Error&){rejected=true;}
    need(rejected&&foreignVertices.empty(),"Owned Z-prepass device isolation changed");
    NativeIm2DProbe::resetMeshCache(backend);
    const auto retained=backend.readbackZPrepassMeshVertices(mesh);
    need(retained.size()==expected.size()&&!std::memcmp(retained.data(),expected.data(),expected.size()*sizeof(ZPrepassVertex)),
         "Owned cache reset invalidated live Z-prepass GPU owner");
    auto replacement=expected;
    auto next=backend.uploadZPrepassMeshOwned(replacement,expectedIndices);
    need(next!=mesh&&replacement.empty(),
         "Owned Z-prepass cache reset reused retired cache ownership");
    backend.bindZPrepassMeshVertices(next);
    need(packedInputs(backend,next).Get()==zeroBank.Get(),"Same-device geometry cache reset recreated the shared zero bank");
}
void runUploadCache(bool hardware) {
    runOwnedUpload(hardware);runZeroBankRetirement(hardware);
    NativeBackend backend(!hardware);
    installD3DCallStats(NativeIm2DProbe::context(backend),NativeIm2DProbe::device(backend));
    const auto pipelineCreates=[] {
        // ID3D11Device slots: input layout, pixel shader, blend, depth, raster.
        return std::array<uint64_t,5>{gD3DDeviceCounts[11],gD3DDeviceCounts[15],
            gD3DDeviceCounts[20],gD3DDeviceCounts[21],gD3DDeviceCounts[22]};
    };
    const auto base = quad();
    std::vector<ZPrepassVertex> vv(base.begin(), base.end());
    std::vector<uint16_t> ii(strip.begin(), strip.end());
    const auto beforeFirst=pipelineCreates();
    const auto firstBuffers=gD3DDeviceCounts[3];
    auto first = backend.uploadZPrepassMesh(vv, ii);
    const auto afterFirst=pipelineCreates();
    for(size_t i=0;i<afterFirst.size();++i)
        need(afterFirst[i]>beforeFirst[i],"Z-prepass pipeline creation counter was not active");
    need(gD3DDeviceCounts[3]==firstBuffers+3,"First static upload did not create exactly position/index/shared-zero buffers");
    const auto repeatBuffers=gD3DDeviceCounts[3];
    auto repeat = backend.uploadZPrepassMesh(vv, ii);
    need(repeat.get() == first.get(), "Z-prepass repeat upload did not reuse exact bytes");
    need(gD3DDeviceCounts[3]==repeatBuffers,"Static cache hit allocated a packed GPU buffer");
    backend.bindZPrepassMeshVertices(first);auto firstZero=packedInputs(backend,first);
    // Same content at a different guest address must still hit (never by pointer).
    std::vector<ZPrepassVertex> vvAlias = vv;
    std::vector<uint16_t> iiAlias = ii;
    need(vvAlias.data() != vv.data(), "Z-prepass alias fixture shares guest address");
    auto aliased = backend.uploadZPrepassMesh(vvAlias, iiAlias);
    need(aliased.get() == first.get(), "Z-prepass identical bytes at new address missed");
    // Caller mutation isolation: mutate the caller's copy, then re-upload the
    // preserved original bytes. The snapshot must be unaffected.
    const std::vector<ZPrepassVertex> origV = vv;
    const std::vector<uint16_t> origI = ii;
    vv[0].position[0] += 0.25f;
    const auto beforeMiss=pipelineCreates();
    const auto missBuffers=gD3DDeviceCounts[3];
    auto mutated = backend.uploadZPrepassMesh(vv, ii);
    need(mutated.get() != first.get(), "Z-prepass single-float change incorrectly hit");
    need(pipelineCreates()==beforeMiss,"Different Z-prepass geometry recreated identical pipeline objects");
    need(gD3DDeviceCounts[3]==missBuffers+2,"Different static geometry recreated the shared zero GPU bank");
    backend.bindZPrepassMeshVertices(mutated);
    need(packedInputs(backend,mutated).Get()==firstZero.Get(),"Different static geometry lost the shared zero-bank identity");
    auto afterMutation = backend.uploadZPrepassMesh(origV, origI);
    need(afterMutation.get() == first.get(), "Z-prepass original bytes missed after caller mutation");
    const auto snapshot = backend.readbackZPrepassMeshVertices(first);
    need(snapshot.size() == origV.size() &&
         !std::memcmp(snapshot.data(), origV.data(), origV.size() * sizeof(ZPrepassVertex)),
         "Z-prepass cached mesh changed after caller mutation");
    auto signedPosition=origV;signedPosition[0].position[0]=-0.0f;
    const auto signedMesh=backend.uploadZPrepassMesh(signedPosition,origI);
    auto positivePosition=signedPosition;positivePosition[0].position[0]=0.0f;
    need(backend.uploadZPrepassMesh(positivePosition,origI)!=signedMesh,"Packed static cache lost signed-zero position bits");
    const auto signedRead=backend.readbackZPrepassMeshVertices(signedMesh);
    need(signedRead.size()==signedPosition.size()&&!std::memcmp(signedRead.data(),signedPosition.data(),signedPosition.size()*sizeof(ZPrepassVertex)),
         "Packed GPU position or reconstructed logical bytes lost signed zero");
    // Every logical unused lane remains part of full-content validation/cache
    // proof. Packing cannot admit -0, denormals, nonzero values or NaNs.
    for(size_t lane=0;lane<26;++lane)for(uint32_t invalid:{0x80000000u,1u,0x3F800000u,0x7FC12345u}) {
        auto bad=origV;std::memcpy(reinterpret_cast<uint8_t*>(bad.data())+12+4*lane,&invalid,4);
        const auto before=bad;const auto* pointer=bad.data();const auto buffers=gD3DDeviceCounts[3];
        bool ownedRejected=false,spanRejected=false;
        try{backend.uploadZPrepassMeshOwned(bad,origI);}catch(const Error&){ownedRejected=true;}
        try{backend.uploadZPrepassMesh(bad,origI);}catch(const Error&){spanRejected=true;}
        need(ownedRejected&&spanRejected&&bad.data()==pointer&&bad.size()==before.size()&&
             !std::memcmp(bad.data(),before.data(),before.size()*sizeof(ZPrepassVertex))&&gD3DDeviceCounts[3]==buffers,
             "Packed static invalid logical input allocated GPU storage or consumed caller ownership");
    }
    // Any bit/extent change forces a miss; invalid inputs stay rejected and
    // are never cached.
    std::vector<uint16_t> otherI = origI;
    otherI[3] = 2;
    auto other = backend.uploadZPrepassMesh(origV, otherI);
    need(other.get() != first.get(), "Z-prepass index-bit change incorrectly hit");
    auto extent = origV;
    extent.push_back(origV.back());
    std::vector<uint16_t> extentI = origI;
    extentI.push_back(4);
    auto grown = backend.uploadZPrepassMesh(extent, extentI);
    need(grown.get() != first.get(), "Z-prepass extent change incorrectly hit");
    bool rejected = false;
    try {
        auto bad = origV;
        bad[0].weights[0] = 1.0f;
        backend.uploadZPrepassMesh(bad, origI);
    } catch (const Error& e) {
        rejected = std::string(e.what()).find("zero unused weights") != std::string::npos;
    }
    need(rejected, "Z-prepass invalid weights accepted after caching");
    const std::array<uint16_t,3> opaque = {0, 1, 4};
    const auto indexed=backend.uploadZPrepassMesh(origV,opaque);
    need(indexed!=first&&indexed->indexCount()==opaque.size()&&backend.uploadZPrepassMesh(origV,opaque)==indexed,
         "Z-prepass opaque R16 bytes lost immutable upload/cache ownership before draw qualification");
    rejected = false;
    try {
        const std::vector<ZPrepassVertex> empty;
        backend.uploadZPrepassMesh(empty, origI);
    } catch (const Error& e) {
        rejected = std::string(e.what()).find("buffer byte or index extent") != std::string::npos;
    }
    need(rejected, "Z-prepass empty extent accepted after caching");
    auto stillHit = backend.uploadZPrepassMesh(origV, origI);
    need(stillHit.get() == first.get(), "Z-prepass valid bytes missed after invalid rejections");
    // Device isolation: no static global, no cross-device hits.
    NativeBackend foreign(!hardware);
    auto foreignMesh = foreign.uploadZPrepassMesh(origV, origI);
    need(foreignMesh.get() != first.get(), "Z-prepass cross-device hit");
    rejected = false;
    try {
        backend.bindZPrepassMeshVertices(foreignMesh);
    } catch (const Error& e) {
        rejected = std::string(e.what()).find("another device") != std::string::npos;
    }
    need(rejected, "Z-prepass foreign mesh bound on local device");
    auto localAgain = backend.uploadZPrepassMesh(origV, origI);
    need(localAgain.get() == first.get(), "Z-prepass local hit lost after foreign upload");
    // Owner isolation: a different thread must fail owner checks, never hit.
    bool ownerRejected = false;
    std::thread probe([&] {
        try {
            backend.uploadZPrepassMesh(origV, origI);
        } catch (const Error& e) {
            ownerRejected = std::string(e.what()).find("different thread") != std::string::npos;
        } catch (...) {
        }
    });
    probe.join();
    need(ownerRejected, "Z-prepass cross-thread upload bypassed owner checks");
    // Replace the actual device on this backend while old meshes remain live.
    // The declaration/shader cache and shared fixed states must follow the new
    // device; identity reuse alone cannot permit foreign backing to bind.
    NativeIm2DProbe::replaceDevice(backend,hardware);
    const auto recreated=backend.uploadZPrepassMesh(origV,origI);
    need(recreated!=first,"Z-prepass device replacement reused an old mesh");
    backend.bindZPrepassMeshVertices(recreated);backend.bindZPrepassMeshDeclaration(recreated);
    backend.bindZPrepassMeshIndices(recreated);
    const auto replacementZero=packedInputs(backend,recreated);
    need(replacementZero.Get()!=firstZero.Get(),"Device replacement reused a foreign shared zero bank");
    ComPtr<ID3D11InputLayout> actualLayout;ComPtr<ID3D11Device> layoutDevice;
    NativeIm2DProbe::context(backend)->IAGetInputLayout(&actualLayout);actualLayout->GetDevice(&layoutDevice);
    need(layoutDevice.Get()==NativeIm2DProbe::device(backend),"Z-prepass replacement reused a foreign declaration");
    rejected=false;
    try {backend.bindZPrepassMeshVertices(first);} catch(const Error& e) {
        rejected=std::string(e.what()).find("another device")!=std::string::npos;
    }
    need(rejected,"Z-prepass replaced device accepted an old live mesh");
    // Production bounds must retain the ~133-draw static working set; eviction
    // itself is covered by small configurable generic instances below rather
    // than by filling 512 production entries through D3D uploads.
    need(kMeshUploadCacheMaxEntries >= 512, "Z-prepass production entry cap thrashes gameplay");
    need(kMeshUploadCacheMaxBytes >= 64u * 1024u * 1024u, "Z-prepass production byte budget thrashes gameplay");
    runGenericMeshCacheChecks();
}
void run(const std::vector<uint8_t>& image,bool hardware) {
    Fixture f(image,hardware);const auto vertices=quad();auto mesh=f.backend.uploadZPrepassMesh(vertices,strip);f.bind(mesh);
    observeMeshGpuRetirement(NativeIm2DProbe::context(f.backend),mesh,"zprepass");
    need(mesh->vertexCount()==4&&mesh->indexCount()==4,"Z-prepass mesh extent differs");
    const auto read=f.backend.readbackZPrepassMeshVertices(mesh);
    need(read.size()==vertices.size()&&!std::memcmp(read.data(),vertices.data(),sizeof(vertices)),"Nine-attribute GPU input bytes differ");
    need(f.backend.readbackZPrepassMeshIndices(mesh)==std::vector<uint16_t>(strip.begin(),strip.end()),"GPU strip indices differ");
    need(f.backend.readbackZPrepassConstants(f.commit)==f.constants&&f.backend.readbackZPrepassBooleans(f.commit)==ZPrepassBooleans{},
         "Actual float/Boolean constant banks differ");
    f.submit(mesh,f.draw);f.pixels([](UINT x,UINT y){return interior(x,y)?0.5f:0.0f;});

    {
        std::vector<ZPrepassVertex> owner(65536,vertices[0]);std::copy(vertices.begin(),vertices.end(),owner.end()-4);
        constexpr std::array<uint16_t,7> words{0,1,2,UINT16_MAX,2,1,3};
        auto large=f.backend.uploadZPrepassMesh(owner,words);f.bind(large);observeMeshGpuRetirement(f.context,large,"zprepass_large_owner");
        auto selected=f.draw;selected.baseVertex=65532;selected.indexCount=7;f.clear();f.submit(large,selected);
        f.pixels([](UINT x,UINT y){return interior(x,y)?0.5f:0.0f;});
        auto bad=selected;bad.baseVertex=65533;
        f.rejected([&]{f.backend.drawZPrepassMesh(f.color,f.depth,large,*f.vertex,f.commit,bad);},"effective index range");
        need(large->vertexCount()==65536,"Large Z-prepass owner metadata differs");
        std::printf("AUDIT_GPU_LARGE_OWNER family=zprepass vertices=65536 base_vertex=65532 create=passed direct_draw=passed malformed=passed gpu_buffer_retirement=separate\n");
        f.bind(mesh);
    }

    {
        const auto boundaryVertices=Test::stripBoundaryVertices(quad());const auto words=Test::stripBoundaryIndices();
        const auto owner=f.backend.uploadZPrepassMesh(boundaryVertices,words);f.bind(owner);
        observeMeshGpuRetirement(f.context,owner,"zprepass_strip_boundary");
        need(f.backend.readbackZPrepassMeshIndices(owner)==words,"Z-prepass boundary index snapshot changed");
        auto selected=f.draw;selected.startIndex=Test::stripBoundaryStart;selected.indexCount=Test::stripBoundaryCount;
        for(uint32_t cull:{2u,6u}) {
            selected.cull=cull;f.clear();f.submit(owner,selected);
            f.pixels([&](UINT x,UINT y){return interior(x,y)&&Test::originalStripBoundaryCovered(x,y,cull)?.5f:0.f;});
            const auto actual=f.backend.readbackDepthTarget(f.depth);f.clear();
            for(const auto& packet:Test::originalStripBoundaryPackets) {
                auto part=selected;part.indexCount=packet[0];part.startIndex=packet[1];f.submit(owner,part);
            }
            const auto reference=f.backend.readbackDepthTarget(f.depth);
            for(size_t i=0;i<reference.size()/8;++i)
                need(depthBits(actual,i)==depthBits(reference,i)&&actual[8*i+4]==reference[8*i+4],
                     "Z-prepass full draw differs from independent original packet depth/stencil");
        }
        for(const auto [start,count,base]:{std::array<int64_t,3>{1,65538,0},{UINT32_MAX,65536,0},{1,65536,-1},{1,65536,1}}) {
            auto bad=selected;bad.startIndex=uint32_t(start);bad.indexCount=uint32_t(count);bad.baseVertex=int32_t(base);
            f.rejected([&]{f.backend.drawZPrepassMesh(f.color,f.depth,owner,*f.vertex,f.commit,bad);},"effective index range");
        }
        std::printf("AUDIT_GPU_STRIP_BOUNDARY family=zprepass vertex=8214A8A4 pixel=00000000 count=65536 reset_position=65530 selected_start=1 sdk_packets=65534_4 sdk_advance=65532 cull=2_6 create=passed direct_pixels=passed original_packet_pixels=passed malformed=passed recording=unqualified gpu_buffer_retirement=separate\n");
        f.bind(mesh);
    }
    // Combined CPU matrix only: c12/world is irrelevant to this original VS.
    f.constants[0]={0.5f,0,0,0.25f};f.constants[12]={9,8,7,6};f.constants[2][3]=0.125f;
    f.commit=f.backend.commitZPrepass(*f.vertex,f.constants,{});f.clear();f.submit(mesh,f.draw);
    f.pixels([](UINT x,UINT y){return interior(x,y)&&x>=6&&x<14?0.375f:0.0f;});
    f.constants[0]={1,0,0,0};f.constants[2][3]=0;f.commit=f.backend.commitZPrepass(*f.vertex,f.constants,{});

    // Both windings must cover BOTH triangles consistently, with color/stencil intact.
    for(uint32_t cull:{2u,6u})for(bool flip:{false,true}) {
        auto v=quad();if(flip){std::swap(v[0],v[1]);std::swap(v[2],v[3]);}
        auto m=f.backend.uploadZPrepassMesh(v,strip);f.bind(m);f.clear();auto d=f.draw;d.cull=cull;f.submit(m,d);
        const bool accepted=(cull==2)!=flip;f.pixels([&](UINT x,UINT y){return interior(x,y)&&accepted?0.5f:0.0f;});
    }
    // Depth compare, write disable and reversed mapping operate on real stored depth.
    mesh=f.backend.uploadZPrepassMesh(quad(0.25f),strip);f.bind(mesh);
    for(bool reverse:{false,true})for(uint32_t func:{1u,4u})for(uint32_t write:{0u,1u}) {
        auto d=f.draw;d.depthCompare=func;d.depthWrite=write;d.viewport[4]=reverse?bits(1):0;d.viewport[5]=reverse?0:bits(1);
        f.clear(0.5f);f.submit(mesh,d);const float incoming=reverse?0.75f:0.25f;
        f.pixels([&](UINT x,UINT y){return interior(x,y)&&write&&compare(func,incoming,0.5f)?incoming:0.5f;});
    }
    // Reference20e4 RNE ties and scaled polygon bias, independent arithmetic oracle.
    for(uint32_t low:{4u,12u}) {
        const float z=std::bit_cast<float>(bits(0.5f)+low);mesh=f.backend.uploadZPrepassMesh(quad(z),strip);f.bind(mesh);
        auto d=f.draw;d.viewport[4]=0;d.viewport[5]=bits(1);d.depthCompare=7;
        f.clear();f.submit(mesh,d);f.pixels([&](UINT x,UINT y){return interior(x,y)?quantize(z):0.0f;});
    }
    mesh=f.backend.uploadZPrepassMesh(quad(),strip);f.bind(mesh);
    auto biased=f.draw;biased.depthBiasBits=bits(0.00390625f);f.clear();f.submit(mesh,biased);
    f.pixels([&](UINT x,UINT y){return interior(x,y)?mappedDepth(0.5f,true,0.00390625f,0,0):0.0f;});

    const std::array<uint16_t,10> cuts={0xFFFF,0,1,2,3,0xFFFF,0,1,2,3};
    mesh=f.backend.uploadZPrepassMesh(quad(),cuts);f.bind(mesh);auto d=f.draw;d.startIndex=1;d.indexCount=9;
    f.clear();f.submit(mesh,d);f.pixels([](UINT x,UINT y){return interior(x,y)?0.5f:0.0f;});
    mesh=f.backend.uploadZPrepassMesh(quad(),strip);f.bind(mesh);

    // Original stream1-null call performs an actual IA unbind, without touching stream0.
    ComPtr<ID3D11Buffer> vb;UINT stride{},offset{};f.context->IAGetVertexBuffers(0,1,&vb,&stride,&offset);
    auto* auxiliary=vb.Get();f.context->IASetVertexBuffers(1,1,&auxiliary,&stride,&offset);
    const auto count=f.backend.zprepassMeshDrawCount();f.backend.clearZPrepassAuxiliaryStream();
    ComPtr<ID3D11Buffer> empty,still;UINT clearedStride=1,clearedOffset=1,s{},o{};
    f.context->IAGetVertexBuffers(1,1,&empty,&clearedStride,&clearedOffset);f.context->IAGetVertexBuffers(0,1,&still,&s,&o);
    need(!empty&&!clearedStride&&!clearedOffset&&still.Get()==vb.Get()&&s==stride&&o==offset&&f.backend.zprepassMeshDrawCount()==count,
         "Original stream1 cleanup did not preserve stream0/count");
    f.rejected([&]{f.backend.drawZPrepassMesh(f.color,f.depth,mesh,*f.vertex,f.commit,f.draw);},"zero input binding");
    f.bind(mesh);
    const auto zeroBank=packedInputs(f.backend,mesh);
    const auto drawBound=[&]{f.backend.drawZPrepassMesh(f.color,f.depth,mesh,*f.vertex,f.commit,f.draw);};
    // Even byte-identical immutable storage cannot replace the mesh's pinned
    // shared bank. Every rejection must preserve both IA slots and depth.
    ComPtr<ID3D11Buffer> differentZero;
    {
        const std::array<uint32_t,26> zero{};D3D11_BUFFER_DESC desc{};
        desc.ByteWidth=sizeof(zero);desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        const D3D11_SUBRESOURCE_DATA data{zero.data(),0,0};
        hr(NativeIm2DProbe::device(f.backend)->CreateBuffer(&desc,&data,&differentZero),"Different zero input fixture");
    }
    const auto bindZero=[&](ID3D11Buffer* buffer,UINT zStride,UINT zOffset) {
        f.context->IASetVertexBuffers(1,1,&buffer,&zStride,&zOffset);
    };
    bindZero(differentZero.Get(),0,0);f.rejected(drawBound,"zero input binding");
    bindZero(zeroBank.Get(),1,0);f.rejected(drawBound,"zero input binding");
    bindZero(zeroBank.Get(),0,4);f.rejected(drawBound,"zero input binding");
    bindZero(nullptr,0,0);f.rejected(drawBound,"zero input binding");f.bind(mesh);
    auto* positions=vb.Get();const UINT legacyStride=sizeof(ZPrepassVertex),zeroOffset=0;
    f.context->IASetVertexBuffers(0,1,&positions,&legacyStride,&zeroOffset);
    f.rejected(drawBound,"vertex binding is stale");f.bind(mesh);
    f.clear();f.submit(mesh,f.draw);f.pixels([](UINT x,UINT y){return interior(x,y)?0.5f:0.0f;});

    auto other=f.backend.uploadZPrepassMesh(quad(0.25f),strip);f.backend.bindZPrepassMeshVertices(other);
    f.rejected([&]{f.backend.drawZPrepassMesh(f.color,f.depth,mesh,*f.vertex,f.commit,f.draw);},"vertex binding is stale");f.bind(mesh);
    f.backend.bindZPrepassMeshIndices(other);
    f.rejected([&]{f.backend.drawZPrepassMesh(f.color,f.depth,mesh,*f.vertex,f.commit,f.draw);},"index binding is stale");f.bind(mesh);
    f.context->IASetInputLayout(nullptr);
    f.rejected([&]{f.backend.drawZPrepassMesh(f.color,f.depth,mesh,*f.vertex,f.commit,f.draw);},"declaration binding is stale");f.bind(mesh);
    ComPtr<ID3D11Buffer> oldBoolean;f.context->VSGetConstantBuffers(1,1,&oldBoolean);
    const auto oldCommit=f.commit;f.commit=f.backend.commitZPrepass(*f.vertex,f.constants,{});
    f.rejected([&]{f.backend.drawZPrepassMesh(f.color,f.depth,mesh,*f.vertex,oldCommit,f.draw);},"constants differ");
    const auto unchanged=f.backend.readbackZPrepassConstants(f.commit);
    need(!std::memcmp(unchanged.data(),f.constants.data(),sizeof(f.constants)),"Unchanged constant-bank bytes differ after a new commit");
    const auto savedConstants=f.constants;
    f.constants[12]={-0.0f,0,0,0};
    f.commit=f.backend.commitZPrepass(*f.vertex,f.constants,{});
    const auto signedZero=f.backend.readbackZPrepassConstants(f.commit);
    need(!std::memcmp(signedZero.data(),f.constants.data(),sizeof(f.constants)),"Constant-bank byte comparison lost signed zero");
    f.constants=savedConstants;f.commit=f.backend.commitZPrepass(*f.vertex,f.constants,{});
    // Commits share the backend's immutable zero Boolean bank; any other CB1
    // buffer, even an all-zero one, still rejects.
    ComPtr<ID3D11Buffer> currentBoolean;f.context->VSGetConstantBuffers(1,1,&currentBoolean);
    need(currentBoolean.Get()==oldBoolean.Get(),"Commits no longer share the zero Boolean bank");
    ComPtr<ID3D11Buffer> foreignBoolean;{const std::array<uint32_t,4> zero{};D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(zero);
        desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;const D3D11_SUBRESOURCE_DATA data{zero.data(),0,0};
        hr(NativeIm2DProbe::device(f.backend)->CreateBuffer(&desc,&data,&foreignBoolean),"Foreign Boolean fixture");}
    auto* stale=foreignBoolean.Get();f.context->VSSetConstantBuffers(1,1,&stale);
    f.rejected([&]{f.backend.drawZPrepassMesh(f.color,f.depth,mesh,*f.vertex,f.commit,f.draw);},"constants differ");
    auto* current=currentBoolean.Get();f.context->VSSetConstantBuffers(1,1,&current);
    ComPtr<ID3D11Buffer> currentFloat;f.context->VSGetConstantBuffers(0,1,&currentFloat);
    ID3D11Buffer* missing=nullptr;f.context->VSSetConstantBuffers(0,1,&missing);
    f.rejected([&]{f.backend.drawZPrepassMesh(f.color,f.depth,mesh,*f.vertex,f.commit,f.draw);},"constants differ");
    auto* floats=currentFloat.Get();f.context->VSSetConstantBuffers(0,1,&floats);
    ComPtr<ID3D11PixelShader> stalePixel;
    hr(NativeIm2DProbe::device(f.backend)->CreatePixelShader(kPSShadowMeshDepth,sizeof(kPSShadowMeshDepth),nullptr,&stalePixel),"Stale PS fixture");
    f.context->PSSetShader(stalePixel.Get(),nullptr,0);
    f.rejected([&]{f.backend.drawZPrepassMesh(f.color,f.depth,mesh,*f.vertex,f.commit,f.draw);},"shader binding differs");
    f.backend.bindZPrepassShader(*f.vertex);f.backend.requireZPrepassCommit(f.commit);
    f.rejected([&]{f.backend.commitZPrepass(*f.vertex,f.constants,{1,0,0,0});},"Boolean bank zero");
    f.rejected([&]{auto bad=f.constants;bad[0][0]=std::numeric_limits<float>::quiet_NaN();f.backend.commitZPrepass(*f.vertex,bad,{});},"Nonfinite");
    f.rejected([&]{auto bad=quad();bad[0].morph[5][2]=1;f.backend.uploadZPrepassMesh(bad,strip);},"zero unused morph");
    f.rejected([&]{auto bad=quad();bad[0].weights[0]=1;f.backend.uploadZPrepassMesh(bad,strip);},"zero unused weights");
    f.rejected([&]{auto bad=quad();bad[0].indices[0]=std::numeric_limits<float>::quiet_NaN();f.backend.uploadZPrepassMesh(bad,strip);},"zero unused indices");
    f.rejected([&]{const std::array<uint16_t,3> bad={0,1,4};const auto badMesh=f.backend.uploadZPrepassMesh(quad(),bad);
        auto invalid=f.draw;invalid.startIndex=0;invalid.indexCount=3;
        f.backend.drawZPrepassMesh(f.color,f.depth,badMesh,*f.vertex,f.commit,invalid);},"effective index range");
    f.rejected([&]{auto bad=f.draw;bad.depthPolicy=ShadowMeshDepthPolicy::Unqualified;f.backend.drawZPrepassMesh(f.color,f.depth,mesh,*f.vertex,f.commit,bad);},"policy is unqualified");
    f.rejected([&]{auto bad=f.draw;bad.indexCount=5;f.backend.drawZPrepassMesh(f.color,f.depth,mesh,*f.vertex,f.commit,bad);},"effective index range");
    NativeBackend foreign(!hardware);auto foreignMesh=foreign.uploadZPrepassMesh(quad(),strip);
    f.rejected([&]{f.backend.bindZPrepassMeshVertices(foreignMesh);},"another device");

    // Alias record uses the same artifact bytes but a distinct retained VS identity.
    const auto prepare=[&](uint32_t address)->const CompiledMaterial& {
        for(const auto& record:originalMaterialIdentities())if(record.originalAddress==address) {
            auto id=f.registry.create(address,std::span<const uint8_t>(image).subspan(address-0x82000000,record.recordBytes));
            return f.registry.prepareForBind(id,f.compiler);
        }
        throw Error("Required original material record missing");
    };
    const auto& shadow=prepare(0x820C2FA0);
    f.rejected([&]{f.backend.bindZPrepassShader(shadow);},"exact compiled owner");
    f.backend.bindShadowDepthShader(shadow);
    f.rejected([&]{f.backend.drawZPrepassMesh(f.color,f.depth,mesh,*f.vertex,f.commit,f.draw);},"shader binding differs");
    f.backend.bindZPrepassShader(*f.vertex);
    const auto& alias=prepare(0x8214B9B8);f.backend.bindZPrepassShader(alias);
    f.rejected([&]{f.backend.drawZPrepassMesh(f.color,f.depth,mesh,alias,f.commit,f.draw);},"commit shader identity");
    f.vertex=&alias;f.commit=f.backend.commitZPrepass(alias,f.constants,{});f.clear();f.submit(mesh,f.draw);
    f.pixels([](UINT x,UINT y){return interior(x,y)?0.5f:0.0f;});
    runUploadCache(hardware);runDepthConstantReuse(image,hardware);
    checkDebug(NativeIm2DProbe::device(f.backend));
    std::printf("PASS Z-prepass mesh %s: %zu checks, %llu Z draws, zero shadow draws; original VS identities, b0/b1, depth, cleanup and restoration\n",
        hardware?"hardware":"WARP",checks,static_cast<unsigned long long>(f.backend.zprepassMeshDrawCount()));
}
std::vector<uint8_t> temporalTextureBytes(NativeBackend& backend,ID3D11Texture2D* texture) {
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    const UINT pixelBytes=desc.Format==DXGI_FORMAT_R32G32_FLOAT?8:desc.Format==DXGI_FORMAT_R8_UNORM?1:0;
    need(pixelBytes!=0&&desc.SampleDesc.Count==1,"Temporal attachment readback profile");
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;hr(NativeIm2DProbe::device(backend)->CreateTexture2D(&desc,nullptr,&staging),"Temporal attachment readback");
    auto* context=NativeIm2DProbe::context(backend);context->CopyResource(staging.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Temporal attachment map");
    std::vector<uint8_t> result(size_t(desc.Width)*desc.Height*pixelBytes);
    for(UINT row=0;row<desc.Height;++row)
        std::memcpy(result.data()+size_t(row)*desc.Width*pixelBytes,
                    static_cast<const uint8_t*>(mapped.pData)+size_t(row)*mapped.RowPitch,size_t(desc.Width)*pixelBytes);
    context->Unmap(staging.Get(),0);return result;
}
// Actual pipeline owner and raster-footprint integration, deliberately not
// an NGX substitute. The original whole generated pass is tested separately.
void runTemporalPrepassEmptyBackground(Fixture& f,const std::shared_ptr<NativeZPrepassMesh>& mesh,const ShadowMeshDraw& draw) {
    const auto initialChecks=checks;
    auto output=f.backend.createTarget(24,24,TargetFormat::RGB10A2,TargetScale::Fixed);
    f.backend.bindTargets({output,nullptr,nullptr,nullptr},{});
    ComPtr<ID3D11RenderTargetView> actualOutput;f.context->OMGetRenderTargets(1,&actualOutput,nullptr);
    ComPtr<ID3D11Resource> resource;actualOutput->GetResource(&resource);ComPtr<ID3D11Texture2D> actualTexture;
    hr(resource.As(&actualTexture),"Empty-prepass actual owned output texture");
    output->width=output->height=Fixture::extent;
    NativeIm2DProbe::installTemporalBackgroundOutput(f.backend,output,Fixture::extent,Fixture::extent,actualTexture.Get());
    f.backend.registerTemporalWorldTargets(f.color,f.depth);f.backend.bindTargets({f.color,nullptr,nullptr,nullptr},f.depth);
    f.clear();f.backend.beginTemporalScene(f.color,f.depth,0x3000,1);
    const auto active=NativeIm2DProbe::frame(f.backend);need(active.jitter!=std::array<float,2>{},"Empty-prepass fixture has no actual service jitter");
    const auto attachments=NativeIm2DProbe::temporalAttachments(f.backend);
    const auto motionBefore=temporalTextureBytes(f.backend,attachments[0]),biasBefore=temporalTextureBytes(f.backend,attachments[1]);
    f.submit(mesh,draw);
    f.pixels([&](UINT x,UINT y){return float(x)+.5f>4.45f+active.jitter[0]&&float(x)+.5f<11.45f+active.jitter[0]&&
        float(y)+.5f>4.55f+active.jitter[1]&&float(y)+.5f<11.55f+active.jitter[1]?quantize(.5f):0.0f;});
    const auto empty=NativeIm2DProbe::frame(f.backend);need(!empty.geometryDraws&&!empty.motionDraws&&!f.backend.temporalSceneHasGeometry(),
        "Actual original depth-only shader fabricated world RGB/MV coverage for an empty background");
    const auto sourceGrid=f.backend.temporalSceneOriginGrid(f.color,f.depth);
    const auto origins=f.backend.temporalSceneOrigins();need(sourceGrid==active.jitter&&origins.components[0].kind==TemporalRasterOriginKind::Uniform&&
        origins.components[1].kind==TemporalRasterOriginKind::Grid&&origins.components[2].kind==TemporalRasterOriginKind::Uniform,
        "Depth-only empty background did not query its actual common Grid while ignoring Uniform color/stencil");
    const auto sourceColor=f.backend.readbackTarget(f.color),sourceDepth=f.backend.readbackDepthTarget(f.depth);const auto before=snapshot(f.context);
    auto background=f.backend.prepareTemporalUiBackground(f.color,f.depth,sourceGrid);
    need(background==output&&!f.backend.temporalSceneActive()&&snapshot(f.context)==before,"Empty-prepass background failed to capture the grid before reset or leaked context");
    auto promoted=f.backend.promoteUiComposite(f.color,f.depth,background,sourceGrid);const auto destination=f.backend.routeDepth(f.depth);
    const auto colors=f.backend.readbackTarget(background),depths=f.backend.readbackDepthTarget(destination);
    const uint32_t ow=background->pixelWidth(),oh=background->pixelHeight();size_t differsFromZero=0;
    auto candidates=[](uint32_t pixel,uint32_t outputExtent,float jitter) {
        if(double(jitter)*8==std::round(double(jitter)*8)) {
            const int64_t numerator=(2*int64_t(pixel)+1)*Fixture::extent*4+int64_t(std::llround(double(jitter)*8))*outputExtent,denominator=8*int64_t(outputExtent);
            int64_t index=numerator/denominator;if(numerator<0&&numerator%denominator)--index;
            const auto selected=uint32_t(std::clamp(index,int64_t(0),int64_t(Fixture::extent-1)));return std::array<uint32_t,2>{selected,selected};
        }
        const long double position=(static_cast<long double>(pixel)+.5L)*Fixture::extent/outputExtent+static_cast<long double>(jitter);
        const long double error=8*std::numeric_limits<float>::epsilon()*std::max(1.0L,std::abs(position));
        const int64_t low=int64_t(std::floor(position-error)),high=int64_t(std::floor(position+error));need(high-low<=1,"Empty-prepass source candidate interval is unbounded");
        return std::array<uint32_t,2>{uint32_t(std::clamp(low,int64_t(0),int64_t(Fixture::extent-1))),uint32_t(std::clamp(high,int64_t(0),int64_t(Fixture::extent-1)))};
    };
    need(colors.size()==size_t(ow)*oh*4&&depths.size()==size_t(ow)*oh*8,"Empty-prepass UI output readback dimensions differ");
    for(uint32_t y=0;y<oh;++y)for(uint32_t x=0;x<ow;++x) {
        const size_t at=size_t(y)*ow+x;uint32_t color{};std::memcpy(&color,colors.data()+at*4,4);
        need(color==0xFFF003FFu,"Empty-prepass spatial background changed the exact original packed magenta/alpha");
        const auto xs=candidates(x,ow,sourceGrid[0]),ys=candidates(y,oh,sourceGrid[1]);bool paired=false;
        for(uint32_t sy:ys)for(uint32_t sx:xs) {const size_t sourceAt=size_t(sy)*Fixture::extent+sx;uint32_t sourcePacked{};std::memcpy(&sourcePacked,sourceColor.data()+sourceAt*4,4);
            paired=paired||((color>>30)==(sourcePacked>>30)&&depthBits(depths,at)==depthBits(sourceDepth,sourceAt)&&depths[at*8+4]==sourceDepth[sourceAt*8+4]);}
        need(paired,"Empty-prepass alpha/depth/stencil do not share one actual source-grid nearest texel");
        const uint32_t zx=uint32_t((uint64_t(2*x+1)*Fixture::extent)/(2*ow)),zy=uint32_t((uint64_t(2*y+1)*Fixture::extent)/(2*oh));
        differsFromZero+=depthBits(depths,at)!=depthBits(sourceDepth,size_t(zy)*Fixture::extent+zx);
    }
    need(differsFromZero>0,"Original depth-only empty background did not distinguish source jitter from an omitted zero-grid copy");
    const auto finalSourceDepth=f.backend.readbackDepthTarget(f.depth);bool sourceDepthUnchanged=finalSourceDepth.size()==sourceDepth.size();
    for(size_t at=0;at<sourceDepth.size()/8;++at)sourceDepthUnchanged=sourceDepthUnchanged&&depthBits(finalSourceDepth,at)==depthBits(sourceDepth,at)&&finalSourceDepth[at*8+4]==sourceDepth[at*8+4];
    need(f.backend.readbackTarget(f.color)==sourceColor&&sourceDepthUnchanged&&
        temporalTextureBytes(f.backend,attachments[0])==motionBefore&&temporalTextureBytes(f.backend,attachments[1])==biasBefore,
        "Empty-prepass copy/promotion changed source color/depth/stencil or actual motion/bias pixels");
    need(promoted!=nullptr,"Empty-prepass promotion did not return its actual owned bundle");f.backend.waitIdle(); // Real backend GPU event, not a fabricated bundle accessor.
    f.backend.clearUiComposite();need(snapshot(f.context)==before,"Empty-prepass owned close failed to restore original context");checkDebug(NativeIm2DProbe::device(f.backend));
    std::printf("EMPTY_PREPASS_BACKGROUND completion=PASS local_checks=%zu original_depth_shader=1 actual_nonzero_grid=PASS depth_mapping_nonzero=PASS geometry_motion_counts=0 source_immutable=PASS ngx_evaluation=0\n",checks-initialChecks);
}
void runTemporalPrepass(const std::vector<uint8_t>& image,bool hardware) {
    Fixture f(image,hardware);
    NativeIm2DProbe::installTemporalPipeline(f.backend,Fixture::extent,Fixture::extent);
    f.backend.registerTemporalWorldTargets(f.color,f.depth);
    const auto worldColor=f.color;const auto worldDepth=f.depth;
    // Edges straddle pixel centers so the first actual nonzero sample changes
    // coverage. Independent CPU screen rectangle, no shader clip-offset code.
    constexpr float left=4.45f,right=11.45f,top=4.55f,bottom=11.55f;
    auto vertices=quad(.5f);
    for(size_t i=0;i<vertices.size();++i)
        vertices[i].position={2*(i<2?left:right)/Fixture::extent-1,
                             1-2*(i%2?bottom:top)/Fixture::extent,.5f};
    auto mesh=f.backend.uploadZPrepassMesh(vertices,strip);f.bind(mesh);
    auto draw=f.draw;draw.scissorEnable=0;
    const auto footprint=[&](const std::array<float,2>& sample) {
        f.pixels([&](UINT x,UINT y) {
            return float(x)+.5f>left+sample[0]&&float(x)+.5f<right+sample[0]&&
                   float(y)+.5f>top+sample[1]&&float(y)+.5f<bottom+sample[1]?quantize(.5f):0.0f;
        });
    };
    // A genuine ranged sentinel in the private slot must be restored.
    ComPtr<ID3D11DeviceContext1> ranged;
    hr(f.context->QueryInterface(IID_PPV_ARGS(&ranged)),"Temporal prepass ranged context");
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=512;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> sentinel;hr(NativeIm2DProbe::device(f.backend)->CreateBuffer(&desc,nullptr,&sentinel),"Temporal ranged sentinel");
    auto* buffer=sentinel.Get();const UINT first=16,count=16;ranged->VSSetConstantBuffers1(11,1,&buffer,&first,&count);
    f.clear();f.submit(mesh,draw);footprint({0,0}); // registered, but inactive
    need(f.backend.temporalSceneOrigins().components[1].kind==TemporalRasterOriginKind::Unknown,
         "Main inactive unadapted depth-only draw fabricated a qualified origin");
    bool untrackedRejected=false;try{f.backend.requireTemporalSceneOrigin(worldColor,worldDepth,{});}
    catch(const Error&){untrackedRejected=true;}
    need(untrackedRejected,"UI origin guard ignored actual unqualified main prepass depth");
    f.clear();f.backend.beginTemporalScene(worldColor,worldDepth,0x3000,1);
    const auto frameBefore=NativeIm2DProbe::frame(f.backend);
    need(frameBefore.jitter[0]!=0||frameBefore.jitter[1]!=0,"Active frame supplied no nonzero raster sample");
    const auto attachments=NativeIm2DProbe::temporalAttachments(f.backend);
    const auto motionBefore=temporalTextureBytes(f.backend,attachments[0]),biasBefore=temporalTextureBytes(f.backend,attachments[1]);
    f.submit(mesh,draw);footprint(frameBefore.jitter);
    const auto worldOrigins=f.backend.temporalSceneOrigins();
    need(worldOrigins.components[1].kind==TemporalRasterOriginKind::Grid&&
         worldOrigins.components[1].compatible(frameBefore.jitter),
         "Actual adapted prepass depth did not retain its current render-pixel grid");
    f.backend.requireTemporalSceneOrigin(worldColor,worldDepth,frameBefore.jitter);
    need(temporalTextureBytes(f.backend,attachments[0])==motionBefore&&temporalTextureBytes(f.backend,attachments[1])==biasBefore,
         "Depth-only prepass changed actual motion/bias pixels");
    const auto frameAfter=NativeIm2DProbe::frame(f.backend);
    need(frameAfter.geometryDraws==0&&frameAfter.motionDraws==0&&
         frameAfter.jitter==frameBefore.jitter&&!f.backend.temporalSceneHasGeometry(),
         "Depth-only prepass fabricated RGB/MV geometry coverage or advanced its sample");
    // Equal-size private owners must remain unjittered while main is active.
    f.color=f.backend.createTarget(Fixture::extent,Fixture::extent,TargetFormat::RGB10A2,TargetScale::Fixed);
    f.depth=f.backend.createDepthTarget(Fixture::extent,Fixture::extent,TargetScale::Fixed);
    f.backend.bindTargets({f.color,nullptr,nullptr,nullptr},f.depth);
    f.clear();f.submit(mesh,draw);footprint({0,0});
    need(f.backend.temporalSceneOrigins().components==worldOrigins.components,
         "Equal-size private prepass activity changed registered main attachment origins");
    // This is explicitly a state-gate test, not a synthetic NGX success.
    NativeIm2DProbe::markReconstructedForGateTest(f.backend);
    f.color=worldColor;f.depth=worldDepth;f.backend.bindTargets({f.color,nullptr,nullptr,nullptr},f.depth);
    f.rejected([&]{f.backend.drawZPrepassMesh(f.color,f.depth,mesh,*f.vertex,f.commit,draw);},
               "Temporal scene coverage unqualified or already reconstructed");
    f.backend.finishTemporalFrame();
    f.clear();f.submit(mesh,draw);footprint({0,0});
    checkDebug(NativeIm2DProbe::device(f.backend));
    std::printf("AUDIT_TEMPORAL_ZPREPASS actual_scene_owner=passed actual_sample=passed private_owner=unjittered reconstructed_scene=rejected ranged_restore=passed RGB_MV_pixels_counts=unchanged\n");
    runTemporalPrepassEmptyBackground(f,mesh,draw);
}

}
int main(int argc,char** argv)try {
    need(argc==2||(argc==3&&std::string(argv[2])=="--hardware"),"Supply analysis/simpsons.pe and optional --hardware");
    std::ifstream input(argv[1],std::ios::binary);const std::vector<uint8_t> image((std::istreambuf_iterator<char>(input)),{});
    need(image.size()==15466496,"Original flat image size differs");run(image,argc==3);runTemporalPrepass(image,argc==3);
    requireMeshGpuRetirements([](bool value,const char* message){need(value,message);});return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL Z-prepass mesh after %zu checks: %s\n",checks,e.what());return 1;}
