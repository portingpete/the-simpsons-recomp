#include "renderer/shadow_mesh.h"
#include "renderer/native_material_compiler.h"
#include "renderer/device_availability.h"
#include <d3d11sdklayers.h>
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

// Reuse the existing backend fixture access point; no production header edits.
#include "header/test_mesh_gpu_retirement.h"
#include "header/test_r16_strip_boundary.h"
namespace Simpsons::Graphics {
struct NativeIm2DProbe {
    static ID3D11Device* device(NativeBackend& b){return b.device.Get();}
    static ID3D11DeviceContext* context(NativeBackend& b){return b.context.Get();}
    static void replaceDevice(NativeBackend& b,bool hardware) {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        const auto hr=D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,2,D3D11_SDK_VERSION,&device,&level,&context);
        if(FAILED(hr)) throw Error("Deterministic shadow device replacement failed");
        b.availability=std::make_shared<DeviceAvailability>(device.Get());b.device=device;b.context=context;b.featureLevel=level;
    }
    static void debug(NativeBackend& b,bool hardware) {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        const auto hr=D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_DEBUG,levels,2,D3D11_SDK_VERSION,&device,&level,&context);
        if(SUCCEEDED(hr)) {
            b.availability=std::make_shared<DeviceAvailability>(device.Get());b.device=device;b.context=context;b.featureLevel=level;
            std::fprintf(stderr,"[SHADOW MESH TEST] D3D11 debug layer enabled\n");
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
            std::fprintf(stderr,"D3D11: %s\n",message->pDescription);need(false,"Shadow mesh emitted a D3D11 error");
        }
    }
    queue->ClearStoredMessages();
}
std::array<ShadowMeshVertex,4> quad(float z=0.5f,float xSlope=0,float ySlope=0) {
    std::array<ShadowMeshVertex,4> v{};
    constexpr std::array<std::array<float,2>,4> xy={{{-1,1},{-1,-1},{1,1},{1,-1}}};
    for(size_t i=0;i<v.size();++i) {
        v[i].position={xy[i][0],xy[i][1],z+xSlope*xy[i][0]+ySlope*xy[i][1]};
        v[i].uv={float(i)*0.25f,1-float(i)*0.125f};v[i].weights={1,0,0,0};
    }
    return v;
}
constexpr std::array<uint16_t,4> strip={0,1,2,3};
struct Fixture {
    NativeBackend backend;
    NativeMaterialCompiler compiler;
    MaterialRegistry registry;
    const CompiledMaterial* vertex{};
    std::shared_ptr<NativeShadowDepthCommit> commit;
    std::shared_ptr<RenderTarget> color;
    std::shared_ptr<DepthTarget> depth;
    ShadowDepthConstants constants{};
    ShadowMeshDraw draw{};
    ID3D11DeviceContext* context{};
    static constexpr UINT extent=16;
    explicit Fixture(const std::vector<uint8_t>& image,bool hardware):backend(!hardware),compiler(backend) {
        NativeIm2DProbe::debug(backend,hardware);context=NativeIm2DProbe::context(backend);
        for(const auto& record:originalMaterialIdentities())if(record.originalAddress==0x820C2FA0) {
            const auto id=registry.create(record.originalAddress,std::span<const uint8_t>(image).subspan(record.originalAddress-0x82000000,record.recordBytes));
            vertex=&registry.prepareForBind(id,compiler);
        }
        need(vertex!=nullptr,"Missing original shadow shader record");
        color=backend.createTarget(extent,extent,TargetFormat::RGB10A2);depth=backend.createDepthTarget(extent,extent);
        backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.bindShadowDepthShader(*vertex);
        for(size_t row=0;row<4;++row)constants[row][row]=constants[12+row][row]=1;
        for(size_t bone=0;bone<64;++bone)for(size_t row=0;row<3;++row)constants[52+3*bone+row][row]=1;
        commit=backend.commitShadowDepth(*vertex,constants);
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
    void bind(const std::shared_ptr<NativeShadowMesh>& mesh) {
        backend.bindShadowMeshVertices(mesh);backend.bindShadowMeshDeclaration(mesh);backend.bindShadowMeshIndices(mesh);
    }
    void submit(const std::shared_ptr<NativeShadowMesh>& mesh,const ShadowMeshDraw& d) {
        const auto before=snapshot(context);const auto count=backend.shadowMeshDrawCount();
        const auto colorBefore=backend.readbackTarget(color);
        backend.drawShadowMesh(color,depth,mesh,*vertex,commit,d);
        need(backend.shadowMeshDrawCount()==count+1,"Shadow mesh submission count differs");
        need(snapshot(context)==before,"Shadow draw leaked native bindings, viewport or scissor");
        need(backend.readbackTarget(color)==colorBefore,"Depth-only shadow draw changed color");
        backend.requireShadowDepthShader(*vertex);backend.requireShadowDepthCommit(commit);
    }
    template<class F>void rejected(F action,const char* diagnostic) {
        const auto before=snapshot(context);
        const auto colorBefore=backend.readbackTarget(color),depthBefore=backend.readbackDepthTarget(depth);
        const auto count=backend.shadowMeshDrawCount();bool rejected=false;
        try{action();}catch(const Error& e){rejected=true;need(std::string(e.what()).find(diagnostic)!=std::string::npos,"Wrong shadow rejection diagnostic");}
        need(rejected,"Invalid shadow operation succeeded");
        need(snapshot(context)==before&&backend.shadowMeshDrawCount()==count,"Rejected shadow operation changed state/count");
        need(backend.readbackTarget(color)==colorBefore,"Rejected shadow operation changed color");
        const auto after=backend.readbackDepthTarget(depth);
        for(size_t i=0;i<after.size()/8;++i)need(depthBits(after,i)==depthBits(depthBefore,i)&&after[8*i+4]==depthBefore[8*i+4],"Rejected shadow operation changed depth/stencil");
    }
    template<class F>void pixels(F expected) {
        const auto pixels=backend.readbackDepthTarget(depth);
        for(UINT y=0;y<extent;++y)for(UINT x=0;x<extent;++x) {
            const auto i=y*extent+x,want=bits(expected(x,y)),got=depthBits(pixels,i);
            if(got!=want)std::fprintf(stderr,"shadow(%u,%u): got%08X expected%08X\n",x,y,got,want);
            need(got==want,"Shadow GPU depth differs from independent arithmetic");need(pixels[8*i+4]==0x6D,"Shadow draw changed stencil");
        }
    }
};
bool interior(UINT x,UINT y){return x>=1&&x<15&&y>=1&&y<15;}
void uploadCacheReuse(bool hardware) {
    NativeBackend backend(!hardware);
    const auto base = quad();
    std::vector<ShadowMeshVertex> vv(base.begin(), base.end());
    std::vector<uint16_t> ii(strip.begin(), strip.end());
    auto first = backend.uploadShadowMesh(vv, ii);
    need(backend.uploadShadowMesh(vv, ii).get() == first.get(), "Shadow repeat upload did not reuse exact bytes");
    // Same content at a different guest address must still hit (never by pointer).
    std::vector<ShadowMeshVertex> vvAlias = vv;
    std::vector<uint16_t> iiAlias = ii;
    need(vvAlias.data() != vv.data(), "Shadow alias fixture shares guest address");
    need(backend.uploadShadowMesh(vvAlias, iiAlias).get() == first.get(), "Shadow identical bytes at new address missed");
    // Caller mutation isolation: the cache snapshots exact bytes on insertion.
    const std::vector<ShadowMeshVertex> origV = vv;
    const std::vector<uint16_t> origI = ii;
    vv[0].uv[0] += 0.5f;
    need(backend.uploadShadowMesh(vv, ii).get() != first.get(), "Shadow single-float change incorrectly hit");
    need(backend.uploadShadowMesh(origV, origI).get() == first.get(), "Shadow original bytes missed after caller mutation");
    const auto snapshot = backend.readbackShadowMeshVertices(first);
    need(snapshot.size() == origV.size() &&
        !std::memcmp(snapshot.data(), origV.data(), origV.size() * sizeof(ShadowMeshVertex)),
        "Shadow cached mesh changed after caller mutation");
    // Invalid bytes can never hit: they miss and run the existing checks.
    bool rejected = false;
    try {
        auto bad = origV;
        bad[0].position[0] = std::numeric_limits<float>::quiet_NaN();
        backend.uploadShadowMesh(bad, origI);
    } catch (const Error& e) {
        rejected = std::string(e.what()).find("Nonfinite shadow mesh position") != std::string::npos;
    }
    need(rejected, "Shadow invalid position accepted after caching");
    const std::array<uint16_t,3> opaque = {0, 1, 4};
    const auto indexed=backend.uploadShadowMesh(origV,opaque);
    need(indexed!=first&&indexed->indexCount()==opaque.size()&&backend.uploadShadowMesh(origV,opaque)==indexed,
         "Shadow opaque R16 bytes lost immutable upload/cache ownership before draw qualification");
    need(backend.uploadShadowMesh(origV, origI).get() == first.get(), "Shadow valid bytes missed after invalid rejections");
    // Device isolation: no static global, no cross-device hits.
    NativeBackend foreign(!hardware);
    auto foreignMesh = foreign.uploadShadowMesh(origV, origI);
    need(foreignMesh.get() != first.get(), "Shadow cross-device hit");
    rejected = false;
    try {
        backend.bindShadowMeshVertices(foreignMesh);
    } catch (const Error& e) {
        rejected = std::string(e.what()).find("another device") != std::string::npos;
    }
    need(rejected, "Shadow foreign mesh bound on local device");
    need(backend.uploadShadowMesh(origV, origI).get() == first.get(), "Shadow local hit lost after foreign upload");
    // Deterministic device replacement through the fixture: both the stale
    // handle bind and the exact-byte reupload must reject via State::validate.
    auto stale = first;
    ID3D11Device* beforeDevice = NativeIm2DProbe::device(backend);
    NativeIm2DProbe::replaceDevice(backend, hardware);
    need(NativeIm2DProbe::device(backend) != beforeDevice, "Shadow device replacement did not occur");
    rejected = false;
    try {
        backend.bindShadowMeshVertices(stale);
    } catch (const Error& e) {
        rejected = std::string(e.what()).find("another device") != std::string::npos;
    }
    need(rejected, "Shadow stale mesh survived a device replacement");
    rejected = false;
    try {
        backend.uploadShadowMesh(origV, origI);
    } catch (const Error& e) {
        rejected = std::string(e.what()).find("another device") != std::string::npos;
    }
    need(rejected, "Shadow cached reupload accepted stale device bytes");
    // Owner isolation: a different thread must fail owner checks, never hit.
    bool ownerRejected = false;
    std::thread probe([&] {
        try {
            backend.uploadShadowMesh(origV, origI);
        } catch (const Error& e) {
            ownerRejected = std::string(e.what()).find("different thread") != std::string::npos;
        }
    });
    probe.join();
    need(ownerRejected, "Shadow cross-thread upload bypassed owner checks");
}
void run(const std::vector<uint8_t>& image,bool hardware) {
    Fixture f(image,hardware);auto vertices=quad();auto mesh=f.backend.uploadShadowMesh(vertices,strip);f.bind(mesh);
    observeMeshGpuRetirement(NativeIm2DProbe::context(f.backend),mesh,"shadow");
    need(mesh->vertexCount()==4&&mesh->indexCount()==4,"Mesh extent metadata differs");
    auto read=f.backend.readbackShadowMeshVertices(mesh);
    need(read.size()==vertices.size()&&!std::memcmp(read.data(),vertices.data(),sizeof(vertices)),"Immutable decoded vertex bytes differ");
    need(f.backend.readbackShadowMeshIndices(mesh)==std::vector<uint16_t>(strip.begin(),strip.end()),"Immutable index bytes differ");
    vertices[0].position[0]=999;read=f.backend.readbackShadowMeshVertices(mesh);
    need(read[0].position[0]==-1,"Mesh retained borrowed caller storage");
    f.submit(mesh,f.draw);f.pixels([](UINT x,UINT y){return interior(x,y)?0.5f:0.0f;});

    {
        const auto quadVertices=quad();std::vector<ShadowMeshVertex> owner(65536,quadVertices[0]);
        std::copy(quadVertices.begin(),quadVertices.end(),owner.end()-4);
        constexpr std::array<uint16_t,7> words{0,1,2,UINT16_MAX,2,1,3};
        auto large=f.backend.uploadShadowMesh(owner,words);f.bind(large);observeMeshGpuRetirement(f.context,large,"shadow_large_owner");
        auto selected=f.draw;selected.baseVertex=65532;selected.indexCount=7;f.clear();f.submit(large,selected);
        f.pixels([](UINT x,UINT y){return interior(x,y)?0.5f:0.0f;});
        auto bad=selected;bad.baseVertex=65533;
        f.rejected([&]{f.backend.drawShadowMesh(f.color,f.depth,large,*f.vertex,f.commit,bad);},"effective index range");
        need(large->vertexCount()==65536,"Large shadow owner metadata differs");
        std::printf("AUDIT_GPU_LARGE_OWNER family=shadow vertices=65536 base_vertex=65532 create=passed direct_draw=passed malformed=passed gpu_buffer_retirement=separate\n");
        f.bind(mesh);
    }

    {
        const auto boundaryVertices=Test::stripBoundaryVertices(quad());const auto words=Test::stripBoundaryIndices();
        const auto owner=f.backend.uploadShadowMesh(boundaryVertices,words);f.bind(owner);
        observeMeshGpuRetirement(f.context,owner,"shadow_strip_boundary");
        need(f.backend.readbackShadowMeshIndices(owner)==words,"Shadow boundary index snapshot changed");
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
                     "Shadow full draw differs from independent original packet depth/stencil");
        }
        for(const auto [start,count,base]:{std::array<int64_t,3>{1,65538,0},{UINT32_MAX,65536,0},{1,65536,-1},{1,65536,1}}) {
            auto bad=selected;bad.startIndex=uint32_t(start);bad.indexCount=uint32_t(count);bad.baseVertex=int32_t(base);
            f.rejected([&]{f.backend.drawShadowMesh(f.color,f.depth,owner,*f.vertex,f.commit,bad);},"effective index range");
        }
        std::printf("AUDIT_GPU_STRIP_BOUNDARY family=shadow vertex=820C2FA0 pixel=00000000 count=65536 reset_position=65530 selected_start=1 sdk_packets=65534_4 sdk_advance=65532 cull=2_6 create=passed direct_pixels=passed original_packet_pixels=passed malformed=passed recording=unqualified gpu_buffer_retirement=separate\n");
        f.bind(mesh);
    }
    // Both original winding mappings, including the second strip triangle.
    for(uint32_t cull:{0u,2u,6u})for(bool flip:{false,true}) {
        auto v=quad();if(flip){std::swap(v[0],v[1]);std::swap(v[2],v[3]);}
        auto m=f.backend.uploadShadowMesh(v,strip);f.bind(m);f.clear();auto d=f.draw;d.cull=cull;f.submit(m,d);
        // TL,BL,TR has the opposite winding to Im2D's TL,TR,BL fixture.
        const bool accepted=cull==0||((cull==2)!=flip);
        f.pixels([&](UINT x,UINT y){return interior(x,y)&&accepted?0.5f:0.0f;});
    }
    // All 32 native depth combinations in both logical ranges, lower/equal/
    // greater incoming values. Disabled writes and disabled depth retain pixels.
    for(float z:{0.25f,0.5f,0.75f}) {
        auto m=f.backend.uploadShadowMesh(quad(z),strip);f.bind(m);
        for(bool reverse:{false,true})for(UINT state=0;state<32;++state) {
            auto d=f.draw;d.depthEnable=(state>>4)&1;d.depthWrite=(state>>3)&1;d.depthCompare=state&7;
            d.viewport[4]=reverse?0x3F800000:0;d.viewport[5]=reverse?0:0x3F800000;f.clear(0.5f);f.submit(m,d);
            const float incoming=reverse?1-z:z;const bool writes=d.depthEnable&&d.depthWrite&&compare(d.depthCompare,incoming,0.5f);
            f.pixels([&](UINT x,UINT y){return interior(x,y)&&writes?incoming:0.5f;});
        }
    }
    // Pinned reached depth and independent RNE ties, zero, 20e4 subnormals and
    // normal boundary. Forward tests avoid losing small depths in 1-z rounding.
    constexpr std::array<uint32_t,10> cases={0,0x3F800000,0x3F000004,0x3F00000C,0x2E000000,0x2E800000,0x387FFFF8,0x38800000,0x3F7FFFFC,0x3EFF7CEE};
    for(uint32_t word:cases) {
        const float z=std::bit_cast<float>(word);auto m=f.backend.uploadShadowMesh(quad(z),strip);f.bind(m);
        auto d=f.draw;d.depthCompare=7;d.viewport[4]=0;d.viewport[5]=0x3F800000;f.clear();f.submit(m,d);
        f.pixels([&](UINT x,UINT y){return interior(x,y)?quantize(z):0.0f;});
    }
    mesh=f.backend.uploadShadowMesh(quad(std::bit_cast<float>(0x3EFF7CEEu)),strip);f.bind(mesh);f.clear();f.submit(mesh,f.draw);
    f.pixels([](UINT x,UINT y){return interior(x,y)?std::bit_cast<float>(0x3F004188u):0.0f;});

    // A depth-only pixel result cannot distinguish an equal-depth write from a
    // rejected fragment. Occlusion independently proves comparison uses the
    // RNE result rather than the greater, unquantized incoming float.
    mesh=f.backend.uploadShadowMesh(quad(std::bit_cast<float>(0x3F000004u)),strip);f.bind(mesh);
    for(uint32_t comparison:{2u,4u}) {
        auto d=f.draw;d.viewport[4]=0;d.viewport[5]=0x3F800000;d.depthCompare=comparison;f.clear(0.5f);
        const D3D11_QUERY_DESC desc{D3D11_QUERY_OCCLUSION,0};ComPtr<ID3D11Query> query;
        hr(NativeIm2DProbe::device(f.backend)->CreateQuery(&desc,&query),"Quantized comparison query");
        f.context->Begin(query.Get());f.submit(mesh,d);f.context->End(query.Get());
        f.backend.readbackDepthTarget(f.depth);UINT64 samples{};
        need(f.context->GetData(query.Get(),&samples,sizeof(samples),0)==S_OK,"Quantized comparison query incomplete");
        need(samples==(comparison==2?196u:0u),"Depth comparison did not use quantized SV_Depth");
    }

    // SDK CC is slope and D0 is constant offset. The SDK slope-times16 and
    // reference subpixel conversion cancel for these normal finite values.
    // Dyadic planes make slopes and raster-center depths exact on the native GPU.
    for(auto gradient:{std::array{0.0f,0.0f},std::array{0.125f,0.0f},std::array{0.0f,0.25f},std::array{0.125f,0.25f}}) {
        mesh=f.backend.uploadShadowMesh(quad(0.5f,gradient[0],gradient[1]),strip);f.bind(mesh);
        for(bool reverse:{false,true}) {
            auto d=f.draw;d.viewport[4]=reverse?0x3F800000:0;d.viewport[5]=reverse?0:0x3F800000;
            d.depthBiasBits=0xBA83126F;d.slopeBiasBits=0xBB449BA6;f.clear();f.submit(mesh,d);
            const float maxSlope=std::max(std::abs(gradient[0]),std::abs(gradient[1]))*2/16;
            f.pixels([&](UINT x,UINT y) {
                const float z=0.5f+((float(x)+0.5f)/8-1)*gradient[0]+(1-(float(y)+0.5f)/8)*gradient[1];
                return interior(x,y)?mappedDepth(z,reverse,std::bit_cast<float>(d.depthBiasBits),std::bit_cast<float>(d.slopeBiasBits),maxSlope):0;
            });
        }
    }
    // Bias clamps after clipping: a clipped primitive cannot reappear; in-range
    // fragments biased across either endpoint clamp before 20e4 conversion.
    for(float z:{-0.125f,0.0f,1.0f,1.125f}) {
        mesh=f.backend.uploadShadowMesh(quad(z),strip);f.bind(mesh);auto d=f.draw;d.depthCompare=7;
        d.depthBiasBits=bits(z<0.5f?0.125f:-0.125f);f.clear(0.5f);f.submit(mesh,d);
        f.pixels([&](UINT x,UINT y){return interior(x,y)&&z>=0&&z<=1?mappedDepth(z,true,std::bit_cast<float>(d.depthBiasBits),0,0):0.5f;});
    }
    // Restart, disconnected coverage, parity restart, and nonzero startIndex.
    std::vector<ShadowMeshVertex> separated;
    for(float center:{-0.5f,0.5f}) {auto v=quad();for(auto& p:v)p.position[0]=center+p.position[0]*0.25f;separated.insert(separated.end(),v.begin(),v.end());}
    const std::array<uint16_t,10> cuts={0xFFFF,0,1,2,3,0xFFFF,4,5,6,7};
    mesh=f.backend.uploadShadowMesh(separated,cuts);f.bind(mesh);f.clear();auto d=f.draw;d.startIndex=1;d.indexCount=9;d.cull=2;f.submit(mesh,d);
    f.pixels([](UINT x,UINT y){return interior(x,y)&&((x>=2&&x<6)||(x>=10&&x<14))?0.5f:0.0f;});
    // Reached count577 (extra strip cuts/degenerates), with exact unchanged GPU
    // index bytes and one real DrawIndexed. No CPU triangle reconstruction.
    std::vector<uint16_t> reached(577,0xFFFF);std::copy(strip.begin(),strip.end(),reached.begin());
    mesh=f.backend.uploadShadowMesh(quad(),reached);f.bind(mesh);f.clear();d=f.draw;d.indexCount=577;f.submit(mesh,d);
    f.pixels([](UINT x,UINT y){return interior(x,y)?0.5f:0.0f;});

    // Preserve zero/multiple native viewport and scissor counts too. Logical
    // scissor disabled here; enabled scissor requires the engine-owned rectangle.
    for(UINT count:{0u,2u}) {
        const std::array<D3D11_VIEWPORT,2> vp={{{2,3,4,5,0.125f,0.75f},{1,2,3,4,0,1}}};
        const std::array<D3D11_RECT,2> sc={{{2,2,4,4},{5,5,8,8}}};
        f.context->RSSetViewports(count,vp.data());f.context->RSSetScissorRects(count,sc.data());
        d=f.draw;d.scissorEnable=0;d.indexCount=577;f.clear();f.submit(mesh,d);f.pixels([](UINT,UINT){return 0.5f;});
    }
    f.backend.setViewport({2,3,7,8,0.25f,0.75f});f.backend.setScissor(f.draw.scissor);

    // Each binding hook is real and independently required. A stale buffer from
    // a prior upload cannot be silently repaired by the draw call.
    auto other=f.backend.uploadShadowMesh(quad(0.25f),strip);f.bind(mesh);
    f.backend.bindShadowMeshVertices(other);
    f.rejected([&]{f.backend.drawShadowMesh(f.color,f.depth,mesh,*f.vertex,f.commit,f.draw);},"vertex binding is stale");f.bind(mesh);
    f.backend.bindShadowMeshIndices(other);
    f.rejected([&]{f.backend.drawShadowMesh(f.color,f.depth,mesh,*f.vertex,f.commit,f.draw);},"index binding is stale");f.bind(mesh);
    f.context->IASetInputLayout(nullptr);
    f.rejected([&]{f.backend.drawShadowMesh(f.color,f.depth,mesh,*f.vertex,f.commit,f.draw);},"declaration binding is stale");f.bind(mesh);
    const auto oldCommit=f.commit;f.commit=f.backend.commitShadowDepth(*f.vertex,f.constants);
    f.rejected([&]{f.backend.drawShadowMesh(f.color,f.depth,mesh,*f.vertex,oldCommit,f.draw);},"constants differ");
    f.rejected([&]{f.backend.drawShadowMesh(f.color,f.depth,mesh,*f.vertex,{},f.draw);},"no committed constants");
    NativeBackend foreign(!hardware);auto foreignMesh=foreign.uploadShadowMesh(quad(),strip);
    f.rejected([&]{f.backend.bindShadowMeshVertices(foreignMesh);},"another device");
    f.rejected([&]{f.backend.drawShadowMesh(f.color,f.depth,foreignMesh,*f.vertex,f.commit,f.draw);},"another device");
    auto foreignDepth=foreign.createDepthTarget(16,16);
    f.rejected([&]{f.backend.drawShadowMesh(f.color,foreignDepth,mesh,*f.vertex,f.commit,f.draw);},"device");
    auto unselected=f.backend.createDepthTarget(16,16);
    f.rejected([&]{f.backend.drawShadowMesh(f.color,unselected,mesh,*f.vertex,f.commit,f.draw);},"target selection differs");
    const auto invalid=[&](auto change,const char* why){auto bad=f.draw;change(bad);f.rejected([&]{f.backend.drawShadowMesh(f.color,f.depth,mesh,*f.vertex,f.commit,bad);},why);};
    invalid([](auto& v){v.indexCount=578;},"effective index range");invalid([](auto& v){v.startIndex=UINT32_MAX;},"effective index range");
    invalid([](auto& v){v.baseVertex=-1;},"effective index range");invalid([](auto& v){v.primitiveType=3;},"primitive6");
    invalid([](auto& v){v.primitiveResetIndex=0xFFFE;},"restartFFFF");invalid([](auto& v){v.primitiveReset=0;},"restartFFFF");
    invalid([](auto& v){v.depthPolicy=ShadowMeshDepthPolicy::Unqualified;},"bias/depth policy is unqualified");
    invalid([](auto& v){v.depthBiasBits=0x7F800000;},"nonfinite");invalid([](auto& v){v.slopeBiasBits=0x7FC00000;},"nonfinite");
    invalid([](auto& v){v.slopeBiasBits=0x7F7FFFFF;},"overflows");invalid([](auto& v){v.depthCompare=8;},"depth state");
    invalid([](auto& v){v.depthEnable=2;},"depth state");invalid([](auto& v){v.stencilEnable=1;},"stencil/alpha");
    invalid([](auto& v){v.colorMask=15;},"color mask0");invalid([](auto& v){v.cull=1;},"cull0/2/6");
    invalid([](auto& v){v.fill=1;},"solid fill0");invalid([](auto& v){v.halfPixelOffset=0;},"halfpixel1");
    invalid([](auto& v){v.viewport[4]=0x3F000000;},"logical viewport");invalid([](auto& v){v.scissor[0]=2;},"retained rectangle");
    invalid([](auto& v){v.scissor[2]=17;},"scissor exceeds");
    f.rejected([&]{f.backend.drawShadowMesh(f.color,f.depth,{},*f.vertex,f.commit,f.draw);},"Missing native shadow mesh");
    f.rejected([&]{auto bad=quad();bad[0].indices[0]=64;f.backend.uploadShadowMesh(bad,strip);},"bone index");
    f.rejected([&]{auto bad=quad();bad[0].position[0]=std::numeric_limits<float>::quiet_NaN();f.backend.uploadShadowMesh(bad,strip);},"Nonfinite");
    f.rejected([&]{const std::array<uint16_t,3> bad={0,1,4};const auto badMesh=f.backend.uploadShadowMesh(quad(),bad);
        auto invalid=f.draw;invalid.startIndex=0;invalid.indexCount=3;
        f.backend.drawShadowMesh(f.color,f.depth,badMesh,*f.vertex,f.commit,invalid);},"effective index range");
    f.rejected([&]{f.backend.uploadShadowMesh({},strip);},"buffer byte or index extent");

    // Target ownership queries remain valid with unrelated UAVs, while a
    // mesh draw must reject those outputs before touching its retained state.
    {
        auto* device=NativeIm2DProbe::device(f.backend);f.bind(mesh);
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R32_UINT;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11UnorderedAccessView> output;
        hr(device->CreateTexture2D(&desc,nullptr,&texture),"Shadow UAV fixture texture");
        hr(device->CreateUnorderedAccessView(texture.Get(),nullptr,&output),"Shadow UAV fixture view");
        for(UINT slot:{1u,device->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?63u:7u}){
            auto* raw=output.Get();f.context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,slot,1,&raw,nullptr);
            f.backend.requireSelectedTargets({f.color,nullptr,nullptr,nullptr},f.depth);
            f.rejected([&]{f.backend.drawShadowMesh(f.color,f.depth,mesh,*f.vertex,f.commit,f.draw);},"output UAVs");
            ComPtr<ID3D11UnorderedAccessView> retained;f.context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,slot,1,&retained);
            need(retained==output,"Rejected shadow draw changed output UAV binding");
            ID3D11UnorderedAccessView* empty=nullptr;f.context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,slot,1,&empty,nullptr);
        }
    }

    // Actual original skinned branch, with its existing constant commit owner.
    f.constants[40][0]=1;f.commit=f.backend.commitShadowDepth(*f.vertex,f.constants);
    mesh=f.backend.uploadShadowMesh(quad(),strip);f.bind(mesh);f.clear();f.submit(mesh,f.draw);
    f.pixels([](UINT x,UINT y){return interior(x,y)?0.5f:0.0f;});
    checkDebug(NativeIm2DProbe::device(f.backend));
    std::printf("PASS shadow mesh %s: %zu checks, %llu native draws; decoded buffers, original VS, depth/bias/RNE, restart, scissor and restoration\n",
        hardware?"hardware":"WARP",checks,static_cast<unsigned long long>(f.backend.shadowMeshDrawCount()));
}
}
int main(int argc,char** argv)try {
    need(argc==2||(argc==3&&std::string(argv[2])=="--hardware"),"Supply analysis/simpsons.pe and optional --hardware");
    std::ifstream input(argv[1],std::ios::binary);const std::vector<uint8_t> image((std::istreambuf_iterator<char>(input)),{});
    need(image.size()==15466496,"Original flat image size differs");
    const auto word=[&](uint32_t address) {
        const auto* p=image.data()+address-0x82000000;
        return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
    };
    // Original SDK arithmetic pins, separate from the reference's GPU policy.
    need(word(0x821DD220)==0x41800000&&word(0x8206A018)==0x3D800000,"Original bias multiplier/inverse changed");
    need(word(0x8243AAA8)==0xC00BD220&&word(0x8243AAB4)==0xEC0D0032&&
         word(0x8243AABC)==0xD0032A50&&word(0x8243AAC0)==0xD0032A58,"Original constant bias setter changed");
    need(word(0x8243AB7C)==0xC1A1001C&&word(0x8243AB80)==0xD1A32A54&&
         word(0x8243AB84)==0xD1A32A5C,"Original unscaled slope setter changed");
    run(image,argc==3);uploadCacheReuse(argc==3);requireMeshGpuRetirements([](bool value,const char* message){need(value,message);});return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL shadow mesh after %zu checks: %s\n",checks,e.what());return 1;}
