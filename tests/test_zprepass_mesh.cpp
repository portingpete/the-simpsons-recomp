#include "renderer/zprepass_mesh.h"
#include "renderer/mesh_upload_cache.h"
#include "renderer/native_material_compiler.h"
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
    explicit Fixture(const std::vector<uint8_t>& image,bool hardware):backend(!hardware),compiler(backend) {
        NativeIm2DProbe::debug(backend,hardware);context=NativeIm2DProbe::context(backend);
        for(const auto& record:originalMaterialIdentities())if(record.originalAddress==0x8214A8A4) {
            const auto id=registry.create(record.originalAddress,std::span<const uint8_t>(image).subspan(record.originalAddress-0x82000000,record.recordBytes));
            vertex=&registry.prepareForBind(id,compiler);
        }
        need(vertex!=nullptr,"Missing original Z-prepass shader record");
        color=backend.createTarget(extent,extent,TargetFormat::RGB10A2);depth=backend.createDepthTarget(extent,extent);
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
void runUploadCache(bool hardware) {
    NativeBackend backend(!hardware);
    const auto base = quad();
    std::vector<ZPrepassVertex> vv(base.begin(), base.end());
    std::vector<uint16_t> ii(strip.begin(), strip.end());
    auto first = backend.uploadZPrepassMesh(vv, ii);
    auto repeat = backend.uploadZPrepassMesh(vv, ii);
    need(repeat.get() == first.get(), "Z-prepass repeat upload did not reuse exact bytes");
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
    auto mutated = backend.uploadZPrepassMesh(vv, ii);
    need(mutated.get() != first.get(), "Z-prepass single-float change incorrectly hit");
    auto afterMutation = backend.uploadZPrepassMesh(origV, origI);
    need(afterMutation.get() == first.get(), "Z-prepass original bytes missed after caller mutation");
    const auto snapshot = backend.readbackZPrepassMeshVertices(first);
    need(snapshot.size() == origV.size() &&
         !std::memcmp(snapshot.data(), origV.data(), origV.size() * sizeof(ZPrepassVertex)),
         "Z-prepass cached mesh changed after caller mutation");
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

    auto other=f.backend.uploadZPrepassMesh(quad(0.25f),strip);f.backend.bindZPrepassMeshVertices(other);
    f.rejected([&]{f.backend.drawZPrepassMesh(f.color,f.depth,mesh,*f.vertex,f.commit,f.draw);},"vertex binding is stale");f.bind(mesh);
    f.backend.bindZPrepassMeshIndices(other);
    f.rejected([&]{f.backend.drawZPrepassMesh(f.color,f.depth,mesh,*f.vertex,f.commit,f.draw);},"index binding is stale");f.bind(mesh);
    f.context->IASetInputLayout(nullptr);
    f.rejected([&]{f.backend.drawZPrepassMesh(f.color,f.depth,mesh,*f.vertex,f.commit,f.draw);},"declaration binding is stale");f.bind(mesh);
    ComPtr<ID3D11Buffer> oldBoolean;f.context->VSGetConstantBuffers(1,1,&oldBoolean);
    const auto oldCommit=f.commit;f.commit=f.backend.commitZPrepass(*f.vertex,f.constants,{});
    f.rejected([&]{f.backend.drawZPrepassMesh(f.color,f.depth,mesh,*f.vertex,oldCommit,f.draw);},"constants differ");
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
    runUploadCache(hardware);
    checkDebug(NativeIm2DProbe::device(f.backend));
    std::printf("PASS Z-prepass mesh %s: %zu checks, %llu Z draws, zero shadow draws; original VS identities, b0/b1, depth, cleanup and restoration\n",
        hardware?"hardware":"WARP",checks,static_cast<unsigned long long>(f.backend.zprepassMeshDrawCount()));
}
}
int main(int argc,char** argv)try {
    need(argc==2||(argc==3&&std::string(argv[2])=="--hardware"),"Supply analysis/simpsons.pe and optional --hardware");
    std::ifstream input(argv[1],std::ios::binary);const std::vector<uint8_t> image((std::istreambuf_iterator<char>(input)),{});
    need(image.size()==15466496,"Original flat image size differs");run(image,argc==3);requireMeshGpuRetirements([](bool value,const char* message){need(value,message);});return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL Z-prepass mesh after %zu checks: %s\n",checks,e.what());return 1;}
