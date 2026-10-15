#include "zprepass_mesh.h"
#include "runtime/stall_profiler.h"
#include "common/geometry_extent.h"
#include "immutable_buffer_proof.h"
#include "immutable_depth_constants.h"
#include "mesh_upload_cache.h"
#include "r16_index_validation.h"
#include "r16_strip_chunks.h"
#include "VSZPrepass.h"
#include "VSZPrepassTemporal.h"
#include <d3d11_1.h>
#include <optional>
#include "PSShadowMeshDepth.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace Simpsons::Graphics {
// Backend-owned exact-content cache impl (forward-declared in
// native_backend.h). No static storage: each NativeBackend owns one slot, so
// entries never cross devices/backends.
struct ZPrepassMeshUploadCache {
    ExactContentMeshCache<NativeZPrepassMesh> cache;
    // Every static mesh uses this exact declaration and depth adapter. Pin the
    // device and immutable objects so cache misses only upload geometry.
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11VertexShader> temporalVertex;
    ComPtr<ID3D11Buffer> temporalControls;
    std::array<float,4> temporalValues{};
    bool temporalValuesValid{};
};
namespace {
struct PackedZPrepassPosition {
    std::array<float,3> position;
    PackedZPrepassPosition()=default;
    explicit PackedZPrepassPosition(const std::array<float,3>& value) noexcept {
        std::memcpy(position.data(),value.data(),sizeof(position));
    }
};
static_assert(sizeof(PackedZPrepassPosition)==12);
constexpr UINT zeroInputBytes=sizeof(ZPrepassVertex)-sizeof(PackedZPrepassPosition);
static_assert(zeroInputBytes==104);
void need(bool value,const char* message) {if(!value)throw Error(message);}
void check(HRESULT hr,const char* operation) {
    if(FAILED(hr)) {
        char message[192];std::snprintf(message,sizeof(message),"Native Z-prepass mesh %s failed: %08lX",operation,ULONG(hr));
        throw Error(message);
    }
}
ComPtr<ID3D11Buffer> immutable(ID3D11Device* device,const void* data,UINT bytes,UINT binding,const char* profile=nullptr) {
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=binding;
    D3D11_SUBRESOURCE_DATA input{data,0,0};ComPtr<ID3D11Buffer> result;
    const char* function=profile?profile:binding==D3D11_BIND_VERTEX_BUFFER?"D3D11.CreateBuffer.static.vertices":
        binding==D3D11_BIND_INDEX_BUFFER?"D3D11.CreateBuffer.static.indices":"D3D11.CreateBuffer.static.depthConstants";
    StallProfiler::Scope allocationProfile(StallProfiler::Section::Rendering,function,nullptr,reinterpret_cast<uintptr_t>(device));
    const HRESULT status=device->CreateBuffer(&desc,&input,&result);
    allocationProfile.finish();check(status,"immutable upload");return result;
}
template<class T>std::vector<T> readBuffer(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Buffer* buffer) {
    D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);const auto size=desc.ByteWidth;
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;check(device->CreateBuffer(&desc,nullptr,&staging),"readback allocation");
    std::vector<T> result(size/sizeof(T));context->CopyResource(staging.Get(),buffer);
    D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"readback map");
    if(!mapped.pData){context->Unmap(staging.Get(),0);throw Error("Native readback mapped null data");}
    if(mapped.RowPitch<size){context->Unmap(staging.Get(),0);throw Error("Native readback pitch is shorter than buffer");}
    std::memcpy(result.data(),mapped.pData,size);context->Unmap(staging.Get(),0);return result;
}
// Local to this draw: preserve actual native state, including an empty or
// multiple-viewport/scissor state. Logical camera endpoints are never rewritten.
// IA resources, original VS and VS b0/b1 are only validated, never changed here.
class ScopedDrawBindings {
    ID3D11DeviceContext* context;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> pixelConstants;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11RasterizerState> raster;
    UINT stencil{},sampleMask{};
    FLOAT blendFactors[4]{};
    D3D11_PRIMITIVE_TOPOLOGY topology{};
    std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
    std::array<D3D11_RECT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors{};
    UINT viewportCount=UINT(viewports.size()),scissorCount=UINT(scissors.size());
public:
    explicit ScopedDrawBindings(ID3D11DeviceContext* c):context(c) {
        // requireZPrepassShader already proved PS=null, so no class instances.
        context->PSGetShader(&pixel,nullptr,nullptr);context->PSGetConstantBuffers(1,1,&pixelConstants);
        context->OMGetDepthStencilState(&depth,&stencil);context->OMGetBlendState(&blend,blendFactors,&sampleMask);
        context->RSGetState(&raster);context->RSGetViewports(&viewportCount,viewports.data());
        context->RSGetScissorRects(&scissorCount,scissors.data());context->IAGetPrimitiveTopology(&topology);
    }
    ScopedDrawBindings(const ScopedDrawBindings&)=delete;
    ScopedDrawBindings& operator=(const ScopedDrawBindings&)=delete;
    ~ScopedDrawBindings() {
        context->PSSetShader(pixel.Get(),nullptr,0);auto* cb=pixelConstants.Get();context->PSSetConstantBuffers(1,1,&cb);
        context->OMSetDepthStencilState(depth.Get(),stencil);context->OMSetBlendState(blend.Get(),blendFactors,sampleMask);
        context->RSSetState(raster.Get());context->RSSetViewports(viewportCount,viewports.data());
        context->RSSetScissorRects(scissorCount,scissors.data());context->IASetPrimitiveTopology(topology);
    }
};
void requireNoAuxiliaryStages(ID3D11DeviceContext* context) {
    ComPtr<ID3D11Predicate> predicate;BOOL value{};context->GetPredication(&predicate,&value);
    need(!predicate,"Native Z-prepass mesh predication is unqualified");
    ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;
    context->GSGetShader(&gs,nullptr,nullptr);context->HSGetShader(&hs,nullptr,nullptr);context->DSGetShader(&ds,nullptr,nullptr);
    need(!gs&&!hs&&!ds,"Native Z-prepass mesh has an unrelated geometry/tessellation stage");
    std::array<ID3D11Buffer*,D3D11_SO_BUFFER_SLOT_COUNT> outputs{};context->SOGetTargets(UINT(outputs.size()),outputs.data());
    bool bound=false;for(auto* output:outputs)if(output){bound=true;output->Release();}
    need(!bound,"Native Z-prepass mesh stream output is unqualified");
}

// Change only VS and its private control slot. Preserve the actual original
// ranged binding as well as its buffer identity; a regular SetCB restore would
// erase an original D3D11.1 range.
class ScopedTemporalZPrepass {
    ID3D11DeviceContext* context;
    ComPtr<ID3D11DeviceContext1> ranged;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11Buffer> constants;
    UINT first{},count{};
public:
    ScopedTemporalZPrepass(ID3D11DeviceContext* c,ComPtr<ID3D11DeviceContext1> r,
                          ID3D11VertexShader* replacement,ID3D11Buffer* controls):context(c),ranged(std::move(r)) {
        context->VSGetShader(&vertex,nullptr,nullptr);
        ranged->VSGetConstantBuffers1(11,1,&constants,&first,&count);
        context->VSSetShader(replacement,nullptr,0);
        context->VSSetConstantBuffers(11,1,&controls);
    }
    ScopedTemporalZPrepass(const ScopedTemporalZPrepass&)=delete;
    ScopedTemporalZPrepass& operator=(const ScopedTemporalZPrepass&)=delete;
    ~ScopedTemporalZPrepass() {
        context->VSSetShader(vertex.Get(),nullptr,0);
        auto* buffer=constants.Get();ranged->VSSetConstantBuffers1(11,1,&buffer,&first,&count);
    }
};
void prepareTemporalZPrepass(ZPrepassMeshUploadCache& slot,ID3D11Device* device,
                            ID3D11DeviceContext* context,const std::array<float,4>& values) {
    need(slot.device.Get()==device,"Temporal Z-prepass cache belongs to another device");
    need(std::isfinite(values[0])&&std::isfinite(values[1])&&values[2]>0&&values[3]>0,
         "Temporal Z-prepass sample/extent is invalid");
    if(!slot.temporalVertex)
        check(device->CreateVertexShader(kVSZPrepassTemporal,sizeof(kVSZPrepassTemporal),nullptr,&slot.temporalVertex),
              "temporal vertex adapter");
    if(!slot.temporalControls) {
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(values);desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&desc,nullptr,&slot.temporalControls),"temporal control allocation");
        slot.temporalValuesValid=false;
    }
    // One immutable-in-flight value per immediate command-stream update; no
    // per-mesh allocation or per-draw upload for an unchanged frame sample.
    if(!slot.temporalValuesValid||slot.temporalValues!=values) {
        context->UpdateSubresource(slot.temporalControls.Get(),0,nullptr,values.data(),0,0);
        slot.temporalValues=values;slot.temporalValuesValid=true;
    }
}
struct VertexBindings {
    ComPtr<ID3D11Buffer> positions,zeros;
    std::array<UINT,2> strides{},offsets{};
    explicit VertexBindings(ID3D11DeviceContext* context) {
        std::array<ID3D11Buffer*,2> buffers{};
        context->IAGetVertexBuffers(0,2,buffers.data(),strides.data(),offsets.data());
        positions.Attach(buffers[0]);zeros.Attach(buffers[1]);
    }
};
}

// Private device-owned immutable zero record. Its strong creation/proof refs
// pin both object and device identities; no guest data or writable backing.
struct ZPrepassZeroStream {
    const ComPtr<ID3D11Device> device;
    const ComPtr<ID3D11Buffer> buffer;
    mutable ComPtr<ID3D11Buffer> proven;
    ZPrepassZeroStream(ComPtr<ID3D11Device> d,ComPtr<ID3D11Buffer> b):device(std::move(d)),buffer(std::move(b)) {}
    void validate(ID3D11Device* expected) const {
        need(expected&&device.Get()==expected&&buffer,"Native Z-prepass zero input owner belongs to another device");
        if(proven.Get()==buffer.Get())return;
        ComPtr<ID3D11Device> actual;buffer->GetDevice(&actual);D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);
        need(actual.Get()==expected&&desc.ByteWidth==zeroInputBytes&&desc.Usage==D3D11_USAGE_IMMUTABLE&&
             desc.BindFlags==D3D11_BIND_VERTEX_BUFFER&&!desc.CPUAccessFlags&&!desc.MiscFlags&&!desc.StructureByteStride,
             "Native Z-prepass zero input backing differs from its immutable owner");
        proven=buffer;
    }
};

struct NativeZPrepassMesh::State {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Buffer> vertices,indices;
    std::shared_ptr<const ZPrepassZeroStream> zeros;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11BlendState> colorOff;
    std::array<ComPtr<ID3D11DepthStencilState>,32> depths;
    std::array<ComPtr<ID3D11RasterizerState>,6> rasterizers;
    DepthConstantsCache depthConstants;
    uint32_t vertexCount{};
    R16Indices indexValues;
    mutable ImmutableBufferPairProof proof;
    void validate(ID3D11Device* expected) const {
        need(device.Get()==expected&&vertices&&indices&&zeros&&layout&&pixel&&colorOff,
             "Native Z-prepass mesh is missing, retired or belongs to another device");
        zeros->validate(expected); // Independent bank proof always precedes pair-proof hits.
        if(proof.holds(vertices.Get(),indices.Get(),expected,vertexCount,indexValues.size()))return;
        for(auto* buffer:{vertices.Get(),indices.Get()}) {
            ComPtr<ID3D11Device> actual;buffer->GetDevice(&actual);D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);
            const bool vertex=buffer==vertices.Get();
            need(actual.Get()==expected&&desc.Usage==D3D11_USAGE_IMMUTABLE&&
                 desc.ByteWidth==(vertex?vertexCount*sizeof(PackedZPrepassPosition):indexValues.size()*sizeof(uint16_t))&&
                 desc.BindFlags==(vertex?D3D11_BIND_VERTEX_BUFFER:D3D11_BIND_INDEX_BUFFER)&&
                 !desc.CPUAccessFlags&&!desc.MiscFlags&&!desc.StructureByteStride,
                 "Native Z-prepass mesh backing differs from its immutable owner");
        }
        proof.prove(vertices.Get(),indices.Get(),expected,vertexCount,indexValues.size());
    }
    void requireBindings(ID3D11DeviceContext* context) const {
        const VertexBindings bound(context);
        ComPtr<ID3D11Buffer> ib;UINT indexOffset{};DXGI_FORMAT format{};
        ComPtr<ID3D11InputLayout> input;
        context->IAGetInputLayout(&input);
        context->IAGetIndexBuffer(&ib,&format,&indexOffset);
        need(bound.positions.Get()==vertices.Get()&&bound.strides[0]==sizeof(PackedZPrepassPosition)&&!bound.offsets[0],
             "Native Z-prepass mesh vertex binding is stale or has a different stride/offset");
        need(bound.zeros.Get()==zeros->buffer.Get()&&!bound.strides[1]&&!bound.offsets[1],
             "Native Z-prepass mesh zero input binding is stale or has a different stride/offset");
        need(input.Get()==layout.Get(),"Native Z-prepass mesh declaration binding is stale");
        need(ib.Get()==indices.Get()&&format==DXGI_FORMAT_R16_UINT&&!indexOffset,
             "Native Z-prepass mesh index binding is stale or has a different format/offset");
    }
};
NativeZPrepassMesh::NativeZPrepassMesh(std::unique_ptr<State> value):state(std::move(value)) {}
NativeZPrepassMesh::~NativeZPrepassMesh()=default;
uint32_t NativeZPrepassMesh::vertexCount() const noexcept {return state->vertexCount;}
uint32_t NativeZPrepassMesh::indexCount() const noexcept {return uint32_t(state->indexValues.size());}

std::shared_ptr<NativeZPrepassMesh> NativeBackend::uploadZPrepassMesh(
    std::span<const ZPrepassVertex> vertices,std::span<const uint16_t> indices) {
    return uploadZPrepassMeshImpl(vertices,indices,nullptr);
}
std::shared_ptr<NativeZPrepassMesh> NativeBackend::uploadZPrepassMeshOwned(
    std::vector<ZPrepassVertex>& vertices,std::span<const uint16_t> indices) {
    return uploadZPrepassMeshImpl(vertices,indices,&vertices);
}
std::shared_ptr<NativeZPrepassMesh> NativeBackend::uploadZPrepassMeshImpl(
    std::span<const ZPrepassVertex> vertices,std::span<const uint16_t> indices,
    std::vector<ZPrepassVertex>* ownedVertices) {
    validateSubmissionContext();
    need(validNativeMeshBufferExtent(vertices.size(),sizeof(ZPrepassVertex))&&validNativeMeshBufferExtent(indices.size(),sizeof(uint16_t)),
         "Native Z-prepass mesh input exceeds buffer byte or index extent");
    // Exact-content fast path: full byte equality, with a content-key fallback.
    // Never by guest address/pointer, never hash-only. Context/owner/device checks have
    // run above; the cached mesh is revalidated below before a hit returns.
    // Rejected inputs can never hit (only successful uploads are cached), so
    // any extent/bit change — including invalid bytes — falls through to the
    // normal validation+upload path. No heap allocations on hits.
    const std::span<const uint8_t> vertexBytes = vertices.empty()
        ? std::span<const uint8_t>{}
        : std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(vertices.data()),
                                   vertices.size_bytes()};
    const std::span<const uint8_t> indexBytes = indices.empty()
        ? std::span<const uint8_t>{}
        : std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(indices.data()),
                                   indices.size_bytes()};
    StallProfiler::Scope lookupProfile(StallProfiler::Section::Rendering,"Mesh.static.decodedLookup",nullptr,reinterpret_cast<uintptr_t>(device.Get()));
    if(!zprepassMeshCache_) zprepassMeshCache_ = std::make_shared<ZPrepassMeshUploadCache>();
    auto& slot = *zprepassMeshCache_;
    if(slot.device.Get() != device.Get()) {
        slot.cache.clear();
        slot.layout.Reset();slot.pixel.Reset();slot.temporalVertex.Reset();slot.temporalControls.Reset();
        slot.temporalValuesValid=false;slot.device = device;
    }
    const auto cached = slot.cache.lookup(vertexBytes, indexBytes);
    lookupProfile.finish();
    if(auto hit = cached.mesh) {
        try {
            hit->state->validate(device.Get());
            requireOwner();
            return hit;
        } catch(...) {
            // Stale entry (e.g. device swapped under test probes): drop the
            // slot and fall through to normal validation+upload.
            slot.cache.clear();
            slot.device = device;
        }
    }
    StallProfiler::Scope validationProfile(StallProfiler::Section::Rendering,"Mesh.static.validate",nullptr,reinterpret_cast<uintptr_t>(device.Get()));
    for(const auto& v:vertices) {
        for(float value:v.position)need(std::isfinite(value),"Nonfinite Z-prepass mesh position");
        const auto zero=[](float value){return std::bit_cast<uint32_t>(value)==0;};
        for(float value:v.weights)need(zero(value),"Static Z-prepass requires zero unused weights");
        for(float value:v.indices)need(zero(value),"Static Z-prepass requires zero unused indices");
        for(const auto& delta:v.morph)for(float value:delta)
            need(zero(value),"Static Z-prepass requires zero unused morph inputs");
    }
    validationProfile.finish();
    // Keep every logical input/cache byte and its original admission bound.
    // Only the immutable GPU representation changes; copy finite position bits
    // exactly, including -0. No float arithmetic or narrower precision.
    StallProfiler::Scope packProfile(StallProfiler::Section::Rendering,"Mesh.static.packPositions",nullptr,reinterpret_cast<uintptr_t>(device.Get()));
    std::vector<PackedZPrepassPosition> packed;packed.reserve(vertices.size());
    for(const auto& vertex:vertices)packed.emplace_back(vertex.position);
    packProfile.finish();
    auto next=std::make_unique<NativeZPrepassMesh::State>();next->device=device;
    next->vertexCount=uint32_t(vertices.size());
    StallProfiler::Scope indexCopyProfile(StallProfiler::Section::Rendering,"Mesh.static.indexCopy",nullptr,reinterpret_cast<uintptr_t>(device.Get()));
    next->indexValues.assign(indices.begin(),indices.end());indexCopyProfile.finish();
    next->vertices=immutable(device.Get(),packed.data(),UINT(packed.size()*sizeof(PackedZPrepassPosition)),D3D11_BIND_VERTEX_BUFFER);
    next->indices=immutable(device.Get(),indices.data(),UINT(indices.size_bytes()),D3D11_BIND_INDEX_BUFFER);
    if(!zprepassZeroStream_||zprepassZeroStream_->device.Get()!=device.Get()) {
        const std::array<uint32_t,zeroInputBytes/sizeof(uint32_t)> values{};
        auto zero=std::make_shared<ZPrepassZeroStream>(device,
            immutable(device.Get(),values.data(),zeroInputBytes,D3D11_BIND_VERTEX_BUFFER,"D3D11.CreateBuffer.static.zeroInputs"));
        zero->validate(device.Get());zprepassZeroStream_=std::move(zero);
    }
    next->zeros=zprepassZeroStream_;
    if(!slot.layout || !slot.pixel) {
        StallProfiler::Scope adapterProfile(StallProfiler::Section::Rendering,"Mesh.static.adapters",nullptr,reinterpret_cast<uintptr_t>(device.Get()));
        std::array<D3D11_INPUT_ELEMENT_DESC,9> elements{};
        elements[0]={"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        elements[1]={"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,1,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        elements[2]={"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,1,16,D3D11_INPUT_PER_VERTEX_DATA,0};
        for(UINT i=0;i<6;++i)
            elements[3+i]={"TEXCOORD",3+i,DXGI_FORMAT_R32G32B32_FLOAT,1,32+12*i,D3D11_INPUT_PER_VERTEX_DATA,0};
        ComPtr<ID3D11InputLayout> layout;ComPtr<ID3D11PixelShader> pixel;
        check(device->CreateInputLayout(elements.data(),UINT(elements.size()),kVSZPrepass,sizeof(kVSZPrepass),&layout),"declaration creation");
        check(device->CreatePixelShader(kPSShadowMeshDepth,sizeof(kPSShadowMeshDepth),nullptr,&pixel),"depth adapter creation");
        // Publish together only after both creations succeed. No native
        // bindings change, and live meshes retain their own immutable refs.
        slot.layout=std::move(layout);slot.pixel=std::move(pixel);
    }
    next->layout=slot.layout;next->pixel=slot.pixel;
    next->colorOff=sceneBlendState(0,0,0);
    // Cache active and inactive enable/write/compare independently, as Im2D does.
    for(UINT i=0;i<32;++i) {
        next->depths[i]=sceneDepthState((i&16)!=0,(i&8)!=0,i&7);
    }
    for(UINT i=0;i<6;++i) {
        // Both bias terms are computed in the PS, NOT integer D32 raster bias.
        next->rasterizers[i]=sceneRasterState(i%3==0?0u:i%3==1?2u:6u,i>=3);
    }
    next->validate(device.Get());requireOwner();
    auto result = std::shared_ptr<NativeZPrepassMesh>(new NativeZPrepassMesh(std::move(next)));
    // Snapshot exact bytes so later caller mutation cannot corrupt the cache.
    // Eviction drops only the cache's strong ref; live caller handles stay valid.
    StallProfiler::Scope cacheCopyProfile(StallProfiler::Section::Rendering,"Mesh.static.cacheCopy",nullptr,reinterpret_cast<uintptr_t>(device.Get()));
    if(ownedVertices)slot.cache.insertOwnedVertices(*ownedVertices,indexBytes,cached.key,result);
    else slot.cache.insert(vertexBytes, indexBytes, cached.key, result);
    cacheCopyProfile.finish();
    return result;
}
void NativeBackend::bindZPrepassMeshVertices(const std::shared_ptr<NativeZPrepassMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing native Z-prepass mesh vertex owner");mesh->state->validate(device.Get());
    flushIm2D();
    const std::array<ID3D11Buffer*,2> buffers{mesh->state->vertices.Get(),mesh->state->zeros->buffer.Get()};
    const std::array<UINT,2> strides{sizeof(PackedZPrepassPosition),0},offsets{};
    context->IASetVertexBuffers(0,2,buffers.data(),strides.data(),offsets.data());
    const VertexBindings actual(context.Get());
    need(actual.positions.Get()==buffers[0]&&actual.zeros.Get()==buffers[1]&&actual.strides==strides&&actual.offsets==offsets,
         "D3D11 did not retain Z-prepass mesh position and zero inputs");requireOwner();
}
void NativeBackend::bindZPrepassMeshDeclaration(const std::shared_ptr<NativeZPrepassMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing native Z-prepass mesh declaration owner");mesh->state->validate(device.Get());
    flushIm2D();context->IASetInputLayout(mesh->state->layout.Get());ComPtr<ID3D11InputLayout> actual;context->IAGetInputLayout(&actual);
    need(actual.Get()==mesh->state->layout.Get(),"D3D11 did not retain Z-prepass mesh declaration");requireOwner();
}
void NativeBackend::bindZPrepassMeshIndices(const std::shared_ptr<NativeZPrepassMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing native Z-prepass mesh index owner");mesh->state->validate(device.Get());
    flushIm2D();context->IASetIndexBuffer(mesh->state->indices.Get(),DXGI_FORMAT_R16_UINT,0);
    ComPtr<ID3D11Buffer> actual;DXGI_FORMAT format{};UINT offset{};context->IAGetIndexBuffer(&actual,&format,&offset);
    need(actual.Get()==mesh->state->indices.Get()&&format==DXGI_FORMAT_R16_UINT&&!offset,
         "D3D11 did not retain Z-prepass mesh indices");requireOwner();
}
std::vector<ZPrepassVertex> NativeBackend::readbackZPrepassMeshVertices(const std::shared_ptr<NativeZPrepassMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing native Z-prepass mesh readback owner");mesh->state->validate(device.Get());
    const auto positions=readBuffer<PackedZPrepassPosition>(device.Get(),context.Get(),mesh->state->vertices.Get());
    const auto zeros=readBuffer<uint8_t>(device.Get(),context.Get(),mesh->state->zeros->buffer.Get());
    need(positions.size()==mesh->state->vertexCount&&zeros.size()==zeroInputBytes,"Native Z-prepass GPU readback extent differs");
    std::vector<ZPrepassVertex> result(positions.size());
    for(size_t i=0;i<result.size();++i) {
        std::memcpy(result[i].position.data(),positions[i].position.data(),sizeof(PackedZPrepassPosition));
        std::memcpy(reinterpret_cast<uint8_t*>(&result[i])+sizeof(PackedZPrepassPosition),zeros.data(),zeros.size());
    }
    return result;
}
void NativeBackend::clearZPrepassAuxiliaryStream() {
    validateSubmissionContext();flushIm2D();
    ID3D11Buffer* empty=nullptr;const UINT zero=0;
    context->IASetVertexBuffers(1,1,&empty,&zero,&zero);
    ComPtr<ID3D11Buffer> actual;UINT stride{},offset{};
    context->IAGetVertexBuffers(1,1,&actual,&stride,&offset);
    need(!actual&&!stride&&!offset,"D3D11 did not clear Z-prepass auxiliary stream1");requireOwner();
}
std::vector<uint16_t> NativeBackend::readbackZPrepassMeshIndices(const std::shared_ptr<NativeZPrepassMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing native Z-prepass mesh readback owner");mesh->state->validate(device.Get());
    return readBuffer<uint16_t>(device.Get(),context.Get(),mesh->state->indices.Get());
}
void NativeBackend::drawZPrepassMesh(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,
    const std::shared_ptr<NativeZPrepassMesh>& mesh,const CompiledMaterial& vertex,
    const std::shared_ptr<NativeZPrepassCommit>& commit,const ShadowMeshDraw& d) {
    validateSubmissionContext();need(bool(mesh),"Missing native Z-prepass mesh draw owner");auto& m=*mesh->state;m.validate(device.Get());
    need(d.primitiveType==6&&d.primitiveReset==1&&d.primitiveResetIndex==0xFFFF,
         "Native Z-prepass mesh requires primitive6 R16 triangle strip and restartFFFF");
    need(validR16DrawRange(m.indexValues,m.vertexCount,d.startIndex,d.indexCount,d.baseVertex),
         "Native Z-prepass mesh effective index range exceeds its owned vertices or indices");
    need(d.depthPolicy==ShadowMeshDepthPolicy::Reference20e4Rne,
         "Native Z-prepass mesh bias/depth policy is unqualified; explicitly select audited Reference20e4Rne");
    need(d.depthEnable<=1&&d.depthWrite<=1&&d.depthCompare<=7,"Native Z-prepass mesh depth state is not canonical");
    need((d.cull==0||d.cull==2||d.cull==6)&&d.fill==0&&d.colorMask==0,
         "Native Z-prepass mesh requires cull0/2/6, solid fill0 and color mask0");
    need(!d.stencilEnable&&!d.alphaTest&&!d.alphaToMask&&!d.clipPlaneEnable&&d.viewportEnable==1,
         "Native Z-prepass mesh stencil/alpha/user clipping/viewport state is unqualified");
    need(d.scissorEnable<=1&&d.halfPixelOffset==1&&d.multisampleAntialias==1&&d.multisampleMask==0xFFFFFFFF,
         "Native Z-prepass mesh requires halfpixel1 and MSAA request1/full mask");
    const float constant=std::bit_cast<float>(d.depthBiasBits),slope=std::bit_cast<float>(d.slopeBiasBits);
    need(std::isfinite(constant)&&std::isfinite(slope)&&std::isfinite(slope*16.0f),
         "Native Z-prepass mesh bias is nonfinite or overflows the original slope-times16 operation");
    need(target&&depth,"Native Z-prepass mesh requires owned color and D24FS8 working attachments");
    DXGI_FORMAT colorFormat{};
    switch(target->format) {
    case TargetFormat::RGBA8:colorFormat=DXGI_FORMAT_R8G8B8A8_UNORM;break;
    case TargetFormat::RGB10A2:colorFormat=DXGI_FORMAT_R10G10B10A2_UNORM;break;
    case TargetFormat::RGBA16Float:colorFormat=DXGI_FORMAT_R16G16B16A16_FLOAT;break;
    case TargetFormat::RGBA32Float:colorFormat=DXGI_FORMAT_R32G32B32A32_FLOAT;break;
    default:throw Error("Unqualified Z-prepass color attachment format");
    }
    validateDepthCopyTarget(depth);
    need(target->texture&&target->view,"Missing native Z-prepass color backing");
    const auto* attachment=attachmentDescriptor(*target);const D3D11_TEXTURE2D_DESC colorDesc=attachment?*attachment:D3D11_TEXTURE2D_DESC{};
    need(attachment&&colorDesc.Format==colorFormat&&
         colorDesc.Width==target->pixelWidth()&&colorDesc.Height==target->pixelHeight()&&colorDesc.MipLevels==1&&colorDesc.ArraySize==1&&
         colorDesc.SampleDesc.Count==1&&!colorDesc.SampleDesc.Quality&&
         depth->width==target->width&&depth->height==target->height,
         "Native Z-prepass mesh attachments have a different device, format, extent or sample profile");
    const bool reverse=d.viewport[4]==0x3F800000&&d.viewport[5]==0;
    need(!d.viewport[0]&&!d.viewport[1]&&d.viewport[2]==target->width&&d.viewport[3]==target->height&&
         (reverse||(d.viewport[4]==0&&d.viewport[5]==0x3F800000)),
         "Native Z-prepass mesh requires a full-target logical viewport with depth1..0 or0..1");
    if(d.scissorEnable) {
        need(d.scissor[0]<d.scissor[2]&&d.scissor[1]<d.scissor[3]&&d.scissor[2]<=target->width&&d.scissor[3]<=target->height,
             "Native Z-prepass mesh scissor exceeds its full viewport");
        const auto retained=scissor();need(retained&&*retained==d.scissor,"Native Z-prepass mesh scissor differs from the original retained rectangle");
    }
    requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);requireNoOutputUavs();
    requireZPrepassShader(vertex);requireZPrepassCommit(commit);m.requireBindings(context.Get());requireNoAuxiliaryStages(context.Get());
    const DepthConstants values{uint32_t(reverse),d.depthBiasBits,d.slopeBiasBits,0};
    // Keep the per-mesh fast path; backend misses share exact immutable bits
    // across meshes. Neither cache updates in flight, and mesh/GPU refs keep
    // retired entries alive independently of backend cache eviction.
    ComPtr<ID3D11Buffer> constants;
    if(auto* hit=m.depthConstants.find(values))constants=hit;
    else{constants=sceneDepthConstantBuffer(values.reverse,values.constantBits,values.slopeBits);m.depthConstants.publish(values,constants.Get());}
    // Classify by actual registered Scene owners, not dimensions. frame()
    // rejects unqualified/reconstructed scenes via temporalSceneJitter(); a
    // caller retaining Scene after promotion must fail before drawing.
    ComPtr<ID3D11DeviceContext1> temporalRanged;
    ZPrepassMeshUploadCache* temporalSlot=nullptr;
    std::optional<std::array<float,2>> temporalRasterJitter;
    if(temporalSceneOwnsTargets(target,depth)) {
        const auto jitter=temporalSceneJitter();temporalRasterJitter=jitter;
        temporalRanged=constantBindingContext();
        need(bool(temporalRanged),"Temporal Z-prepass requires its original D3D11.1 context");
        need(bool(zprepassMeshCache_),"Temporal Z-prepass lost its backend cache owner");
        temporalSlot=zprepassMeshCache_.get();
        prepareTemporalZPrepass(*temporalSlot,device.Get(),context.Get(),
            {jitter[0],jitter[1],float(target->pixelWidth()),float(target->pixelHeight())});
    }
    // All rejection and allocation precede the flush or Z-prepass context mutation.
    flushIm2D();
    requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);
    requireZPrepassShader(vertex);requireZPrepassCommit(commit);m.requireBindings(context.Get());
    {
        ScopedDrawBindings restore(context.Get());
        setViewport({0,0,float(target->width),float(target->height),0,1});
        // halfpixel1 already uses native half-integer centers; no 0.5 XY shift.
        const UINT raster=(d.scissorEnable?3u:0u)+(d.cull==0?0u:(d.cull==2?1u:2u));
        context->RSSetState(m.rasterizers[raster].Get());
        context->OMSetBlendState(m.colorOff.Get(),nullptr,d.multisampleMask);
        context->OMSetDepthStencilState(m.depths[(d.depthEnable?16u:0u)|(d.depthWrite?8u:0u)|d.depthCompare].Get(),0);
        auto* cb=constants.Get();context->PSSetConstantBuffers(1,1,&cb);context->PSSetShader(m.pixel.Get(),nullptr,0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        std::optional<ScopedTemporalZPrepass> temporalRestore;
        if(temporalSlot)
            temporalRestore.emplace(context.Get(),temporalRanged,temporalSlot->temporalVertex.Get(),temporalSlot->temporalControls.Get());
        // R16 triangle strips consume FFFF as cut indices, including parity reset.
        forEachOriginalR16StripChunk(d.startIndex,d.indexCount,[&](uint32_t n,uint32_t first){context->DrawIndexed(n,first,d.baseVertex);});
        ++zprepassMeshDraws;
        noteTemporalSceneDraw(target,depth,0,d.depthEnable&&d.depthWrite,false,temporalRasterJitter);
    }
    requireZPrepassShader(vertex);requireZPrepassCommit(commit);m.requireBindings(context.Get());
    requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);requireOwner();
}
}
