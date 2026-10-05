#include "mono_mesh.h"
#include "material_resources.h"
#include "common/geometry_extent.h"
#include "immutable_buffer_proof.h"
#include "immutable_depth_constants.h"
#include "mesh_upload_cache.h"
#include "r16_index_validation.h"
#include "r16_strip_chunks.h"
#include "VSMono.h"
#include "PSMonoDraw.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>

namespace Simpsons::Graphics {
// Backend-owned exact-content cache impl (forward-declared in
// native_backend.h). No static storage: each NativeBackend owns one slot, so
// entries never cross devices/backends.
struct MonoMeshUploadCache {
    ExactContentMeshCache<NativeMonoMesh> cache;
    ID3D11Device* device = nullptr;
};
namespace {
void need(bool value,const char* message) {if(!value)throw Error(message);}
void check(HRESULT hr,const char* operation) {
    if(FAILED(hr)) {
        char message[192];std::snprintf(message,sizeof(message),"Native mono mesh %s failed: %08lX",operation,ULONG(hr));
        throw Error(message);
    }
}
ComPtr<ID3D11Buffer> immutable(ID3D11Device* device,const void* data,UINT bytes,UINT binding) {
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=binding;
    D3D11_SUBRESOURCE_DATA input{data,0,0};ComPtr<ID3D11Buffer> result;
    check(device->CreateBuffer(&desc,&input,&result),"immutable upload");return result;
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
        // The retained original mono PS uses no class instances.
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
    need(!predicate,"Native mono mesh predication is unqualified");
    ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;
    context->GSGetShader(&gs,nullptr,nullptr);context->HSGetShader(&hs,nullptr,nullptr);context->DSGetShader(&ds,nullptr,nullptr);
    need(!gs&&!hs&&!ds,"Native mono mesh has an unrelated geometry/tessellation stage");
    std::array<ID3D11Buffer*,D3D11_SO_BUFFER_SLOT_COUNT> outputs{};context->SOGetTargets(UINT(outputs.size()),outputs.data());
    bool bound=false;for(auto* output:outputs)if(output){bound=true;output->Release();}
    need(!bound,"Native mono mesh stream output is unqualified");
}
}

struct NativeMonoMesh::State {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Buffer> vertices,indices;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11BlendState> colorOff,colorOn,alphaOff,alphaOn,alphaOnly,blendedAlphaOnly;
    std::array<ComPtr<ID3D11BlendState>,3> replacementBlend,separateAlphaBlend;
    std::array<ComPtr<ID3D11DepthStencilState>,32> depths;
    std::array<ComPtr<ID3D11RasterizerState>,6> rasterizers;
    DepthConstantsCache depthConstants;
    uint32_t vertexCount{};
    bool skinned{};
    R16Indices indexValues;
    mutable ImmutableBufferPairProof proof;
    void validate(ID3D11Device* expected) const {
        need(device.Get()==expected&&vertices&&indices&&layout&&pixel&&colorOff&&colorOn&&alphaOff&&alphaOn&&alphaOnly&&blendedAlphaOnly&&
             replacementBlend[0]&&replacementBlend[1]&&replacementBlend[2]&&
             separateAlphaBlend[0]&&separateAlphaBlend[1]&&separateAlphaBlend[2],
             "Native mono mesh is missing, retired or belongs to another device");
        if(proof.holds(vertices.Get(),indices.Get(),expected,vertexCount,indexValues.size()))return;
        for(auto* buffer:{vertices.Get(),indices.Get()}) {
            ComPtr<ID3D11Device> actual;buffer->GetDevice(&actual);D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);
            const bool vertex=buffer==vertices.Get();
            need(actual.Get()==expected&&desc.Usage==D3D11_USAGE_IMMUTABLE&&
                 desc.ByteWidth==(vertex?vertexCount*sizeof(MonoVertex):indexValues.size()*sizeof(uint16_t))&&
                 desc.BindFlags==(vertex?D3D11_BIND_VERTEX_BUFFER:D3D11_BIND_INDEX_BUFFER)&&
                 !desc.CPUAccessFlags&&!desc.MiscFlags&&!desc.StructureByteStride,
                 "Native mono mesh backing differs from its immutable owner");
        }
        proof.prove(vertices.Get(),indices.Get(),expected,vertexCount,indexValues.size());
    }
    void requireBindings(ID3D11DeviceContext* context) const {
        ComPtr<ID3D11Buffer> vb,ib;UINT stride{},offset{},indexOffset{};DXGI_FORMAT format{};
        ComPtr<ID3D11InputLayout> input;
        context->IAGetVertexBuffers(0,1,&vb,&stride,&offset);context->IAGetInputLayout(&input);
        context->IAGetIndexBuffer(&ib,&format,&indexOffset);
        need(vb.Get()==vertices.Get()&&stride==sizeof(MonoVertex)&&!offset,
             "Native mono mesh vertex binding is stale or has a different stride/offset");
        need(input.Get()==layout.Get(),"Native mono mesh declaration binding is stale");
        need(ib.Get()==indices.Get()&&format==DXGI_FORMAT_R16_UINT&&!indexOffset,
             "Native mono mesh index binding is stale or has a different format/offset");
    }
};
NativeMonoMesh::NativeMonoMesh(std::unique_ptr<State> value):state(std::move(value)) {}
NativeMonoMesh::~NativeMonoMesh()=default;
uint32_t NativeMonoMesh::vertexCount() const noexcept {return state->vertexCount;}
uint32_t NativeMonoMesh::indexCount() const noexcept {return uint32_t(state->indexValues.size());}

std::shared_ptr<NativeMonoMesh> NativeBackend::uploadMonoMesh(
    std::span<const MonoVertex> vertices,std::span<const uint16_t> indices,bool skinned) {
    validateSubmissionContext();
    need(validNativeMeshBufferExtent(vertices.size(),sizeof(MonoVertex))&&validNativeMeshBufferExtent(indices.size(),sizeof(uint16_t)),
         "Native mono mesh input exceeds buffer byte or index extent");
    // Exact-content fast path: content key + full byte equality only. Never by
    // guest address/pointer, never hash-only. Context/owner/device checks have
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
    // The same bytes can describe either profile. Preserve one shared cache
    // budget while keeping their validation and shader Boolean independent.
    const uint64_t contentKey =
        ExactContentMeshCache<NativeMonoMesh>::contentKey(vertexBytes, indexBytes) ^ (skinned?0xD973AB284E15C607ull:0);
    if(!monoMeshCache_) monoMeshCache_ = std::make_shared<MonoMeshUploadCache>();
    auto& slot = *monoMeshCache_;
    if(slot.device != device.Get()) {
        slot.cache.clear();
        slot.device = device.Get();
    }
    if(auto hit = slot.cache.find(vertexBytes, indexBytes, contentKey)) {
        try {
            hit->state->validate(device.Get());
            need(hit->state->skinned==skinned,"Cached mono mesh skinning profile differs");
            requireOwner();
            return hit;
        } catch(...) {
            // Stale entry (e.g. device swapped under test probes): drop the
            // slot and fall through to normal validation+upload.
            slot.cache.clear();
            slot.device = device.Get();
        }
    }
    for(const auto& v:vertices) {
        for(float value:v.position)need(std::isfinite(value),"Nonfinite mono mesh position");
        if(skinned) {
            for(float value:v.weights)need(std::isfinite(value),"Nonfinite mono skin weight");
            // All four palette lookups execute, even for zero-weight lanes.
            for(float value:v.indices)need(std::isfinite(value)&&value>=0&&value<=63&&std::floor(value)==value,
                "Mono skin bone index not integer 0..63");
        } else {
            const auto zero=[](float value){return std::bit_cast<uint32_t>(value)==0;};
            for(float value:v.weights)need(zero(value),"Static mono requires zero unused weights");
            for(float value:v.indices)need(zero(value),"Static mono requires zero unused indices");
        }
    }
    auto next=std::make_unique<NativeMonoMesh::State>();next->device=device;
    next->skinned=skinned;
    next->vertexCount=uint32_t(vertices.size());next->indexValues.assign(indices.begin(),indices.end());
    next->vertices=immutable(device.Get(),vertices.data(),UINT(vertices.size_bytes()),D3D11_BIND_VERTEX_BUFFER);
    next->indices=immutable(device.Get(),indices.data(),UINT(indices.size_bytes()),D3D11_BIND_INDEX_BUFFER);
    std::array<D3D11_INPUT_ELEMENT_DESC,3> elements{};
    elements[0]={"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
    elements[1]={"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0};
    elements[2]={"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,28,D3D11_INPUT_PER_VERTEX_DATA,0};
    check(device->CreateInputLayout(elements.data(),UINT(elements.size()),kVSMono,sizeof(kVSMono),&next->layout),"declaration creation");
    check(device->CreatePixelShader(kPSMonoDraw,sizeof(kPSMonoDraw),nullptr,&next->pixel),"depth adapter creation");
    D3D11_BLEND_DESC blend{};auto& color=blend.RenderTarget[0];color.RenderTargetWriteMask=0;
    color.SrcBlend=color.SrcBlendAlpha=D3D11_BLEND_ONE;color.DestBlend=color.DestBlendAlpha=D3D11_BLEND_ZERO;
    color.BlendOp=color.BlendOpAlpha=D3D11_BLEND_OP_ADD;
    check(device->CreateBlendState(&blend,&next->colorOff),"color-disabled state creation");
    color.RenderTargetWriteMask=15;
    check(device->CreateBlendState(&blend,&next->colorOn),"color-enabled state creation");
    color.RenderTargetWriteMask=8;
    check(device->CreateBlendState(&blend,&next->alphaOnly),"alpha-only state creation");
    // Original post filters can leave blending enabled with ONE/ZERO. Keep
    // that descriptor distinct from the disabled replacement state.
    color.BlendEnable=TRUE;
    constexpr std::array<UINT8,3> masks={0,8,15};
    for(size_t i=0;i<masks.size();++i) {
        color.RenderTargetWriteMask=masks[i];
        check(device->CreateBlendState(&blend,&next->replacementBlend[i]),"replacement blend state creation");
    }
    // Packed 00010706 blends RGB with source alpha but replaces alpha.
    color.SrcBlend=D3D11_BLEND_SRC_ALPHA;color.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
    for(size_t i=0;i<masks.size();++i) {
        color.RenderTargetWriteMask=masks[i];
        check(device->CreateBlendState(&blend,&next->separateAlphaBlend[i]),"separate alpha blend state creation");
    }
    // Live mono traffic also reaches the engine's audited non-separate
    // SRC_ALPHA/INV_SRC_ALPHA equation (effective word 07060706).  Preserve
    // that equation explicitly instead of relying on the mono PS currently
    // exporting alpha one.
    color.BlendEnable=TRUE;
    color.SrcBlend=color.SrcBlendAlpha=D3D11_BLEND_SRC_ALPHA;
    color.DestBlend=color.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
    color.RenderTargetWriteMask=0;
    check(device->CreateBlendState(&blend,&next->alphaOff),"alpha color-disabled state creation");
    color.RenderTargetWriteMask=15;
    check(device->CreateBlendState(&blend,&next->alphaOn),"alpha color-enabled state creation");
    color.RenderTargetWriteMask=8;
    check(device->CreateBlendState(&blend,&next->blendedAlphaOnly),"blended alpha-only state creation");
    // Cache active and inactive enable/write/compare independently, as Im2D does.
    for(UINT i=0;i<32;++i) {
        D3D11_DEPTH_STENCIL_DESC d{};d.DepthEnable=(i&16)!=0;
        d.DepthWriteMask=(i&8)?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;
        d.DepthFunc=D3D11_COMPARISON_FUNC((i&7)+1);
        d.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};d.BackFace=d.FrontFace;
        check(device->CreateDepthStencilState(&d,&next->depths[i]),"depth state creation");
    }
    for(UINT i=0;i<6;++i) {
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.DepthClipEnable=TRUE;
        r.CullMode=i%3==0?D3D11_CULL_NONE:D3D11_CULL_BACK;r.FrontCounterClockwise=i%3==1;
        r.ScissorEnable=i>=3;
        // Both bias terms are computed in the PS, NOT integer D32 raster bias.
        check(device->CreateRasterizerState(&r,&next->rasterizers[i]),"rasterizer creation");
    }
    next->validate(device.Get());requireOwner();
    auto result = std::shared_ptr<NativeMonoMesh>(new NativeMonoMesh(std::move(next)));
    // Snapshot exact bytes so later caller mutation cannot corrupt the cache.
    // Eviction drops only the cache's strong ref; live caller handles stay valid.
    slot.cache.insert(vertexBytes, indexBytes, contentKey, result);
    return result;
}
void NativeBackend::bindMonoMeshVertices(const std::shared_ptr<NativeMonoMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing native mono mesh vertex owner");mesh->state->validate(device.Get());
    flushIm2D();auto* buffer=mesh->state->vertices.Get();const UINT stride=sizeof(MonoVertex),offset=0;
    context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
    ComPtr<ID3D11Buffer> actual;UINT s{},o{};context->IAGetVertexBuffers(0,1,&actual,&s,&o);
    need(actual.Get()==buffer&&s==stride&&o==offset,"D3D11 did not retain mono mesh vertices");requireOwner();
}
void NativeBackend::bindMonoMeshDeclaration(const std::shared_ptr<NativeMonoMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing native mono mesh declaration owner");mesh->state->validate(device.Get());
    flushIm2D();context->IASetInputLayout(mesh->state->layout.Get());ComPtr<ID3D11InputLayout> actual;context->IAGetInputLayout(&actual);
    need(actual.Get()==mesh->state->layout.Get(),"D3D11 did not retain mono mesh declaration");requireOwner();
}
void NativeBackend::bindMonoMeshIndices(const std::shared_ptr<NativeMonoMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing native mono mesh index owner");mesh->state->validate(device.Get());
    flushIm2D();context->IASetIndexBuffer(mesh->state->indices.Get(),DXGI_FORMAT_R16_UINT,0);
    ComPtr<ID3D11Buffer> actual;DXGI_FORMAT format{};UINT offset{};context->IAGetIndexBuffer(&actual,&format,&offset);
    need(actual.Get()==mesh->state->indices.Get()&&format==DXGI_FORMAT_R16_UINT&&!offset,
         "D3D11 did not retain mono mesh indices");requireOwner();
}
void NativeBackend::bindMonoMeshVertices(const std::shared_ptr<NativeRecordingPayload>& payload,const std::shared_ptr<NativeMonoMesh>& mesh) {
    auto* deferred=checkedRecordingPayloadContext(payload);need(bool(mesh),"Missing recorded mono vertex owner");mesh->state->validate(device.Get());
    auto* value=mesh->state->vertices.Get();const UINT stride=sizeof(MonoVertex),offset=0;
    deferred->IASetVertexBuffers(0,1,&value,&stride,&offset);
}
void NativeBackend::bindMonoMeshDeclaration(const std::shared_ptr<NativeRecordingPayload>& payload,const std::shared_ptr<NativeMonoMesh>& mesh) {
    auto* deferred=checkedRecordingPayloadContext(payload);need(bool(mesh),"Missing recorded mono declaration owner");mesh->state->validate(device.Get());
    deferred->IASetInputLayout(mesh->state->layout.Get());
}
void NativeBackend::bindMonoMeshIndices(const std::shared_ptr<NativeRecordingPayload>& payload,const std::shared_ptr<NativeMonoMesh>& mesh) {
    auto* deferred=checkedRecordingPayloadContext(payload);need(bool(mesh),"Missing recorded mono index owner");mesh->state->validate(device.Get());
    deferred->IASetIndexBuffer(mesh->state->indices.Get(),DXGI_FORMAT_R16_UINT,0);
}
std::vector<MonoVertex> NativeBackend::readbackMonoMeshVertices(const std::shared_ptr<NativeMonoMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing native mono mesh readback owner");mesh->state->validate(device.Get());
    return readBuffer<MonoVertex>(device.Get(),context.Get(),mesh->state->vertices.Get());
}
void NativeBackend::clearMonoAuxiliaryStream() {
    validateSubmissionContext();flushIm2D();
    ID3D11Buffer* empty=nullptr;const UINT zero=0;
    context->IASetVertexBuffers(1,1,&empty,&zero,&zero);
    ComPtr<ID3D11Buffer> actual;UINT stride{},offset{};
    context->IAGetVertexBuffers(1,1,&actual,&stride,&offset);
    need(!actual&&!stride&&!offset,"D3D11 did not clear mono auxiliary stream1");requireOwner();
}
std::vector<uint16_t> NativeBackend::readbackMonoMeshIndices(const std::shared_ptr<NativeMonoMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing native mono mesh readback owner");mesh->state->validate(device.Get());
    return readBuffer<uint16_t>(device.Get(),context.Get(),mesh->state->indices.Get());
}
struct NativeMonoReplayConstants::State {
    const NativeBackend* owner{};DWORD thread{};ComPtr<ID3D11Device> device;
    bool active=true,ready=false;MonoConstants vertex{};
    void validate(const NativeBackend* expected,ID3D11Device* expectedDevice) const {
        need(active&&owner==expected&&thread==GetCurrentThreadId()&&device.Get()==expectedDevice,
             "Mono replay constants are released or belong to another owner");
    }
};
NativeMonoReplayConstants::NativeMonoReplayConstants(std::unique_ptr<State> value):state(std::move(value)){}
NativeMonoReplayConstants::~NativeMonoReplayConstants()=default;
namespace {
void monoFinite(const MonoConstants& data) {
    for(const auto& row:data)for(float x:row)need(std::isfinite(x),"Nonfinite mono recorded constant");
}
struct MonoRecordedConstants {MonoConstants vertex;MonoBooleans booleans;NativeRecordingMask inputMask;};
static_assert(sizeof(MonoRecordedConstants)==3960&&std::is_trivially_copyable_v<MonoRecordedConstants>);
struct MonoRecordedDraw {
    const NativeBackend* owner{};DWORD thread{};ComPtr<ID3D11Device> device;
    std::shared_ptr<NativeMonoMesh> mesh;std::shared_ptr<NativeMonoReplayConstants> live;
    std::shared_ptr<RenderTarget> color;std::shared_ptr<DepthTarget> depth;
    ComPtr<ID3D11RenderTargetView> colorView;ComPtr<ID3D11DepthStencilView> depthView;
    ComPtr<ID3D11Buffer> vertices,indices,constants,booleans,depthConstants;
    ComPtr<ID3D11InputLayout> layout;ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> originalPixel,drawPixel;
    ComPtr<ID3D11DepthStencilState> depthState;ComPtr<ID3D11RasterizerState> raster;ComPtr<ID3D11BlendState> blend;
    D3D11_VIEWPORT viewport{};D3D11_RECT scissor{};uint32_t count{},start{},sampleMask{};int32_t base{};
    NativeRecordingMask inputMask{};
    void record(ID3D11DeviceContext* deferred) const {
        auto* output=colorView.Get();deferred->OMSetRenderTargets(1,&output,depthView.Get());
        deferred->OMSetBlendState(blend.Get(),nullptr,sampleMask);deferred->OMSetDepthStencilState(depthState.Get(),0);
        deferred->RSSetState(raster.Get());deferred->RSSetViewports(1,&viewport);deferred->RSSetScissorRects(1,&scissor);
        deferred->SetPredication(nullptr,FALSE);deferred->SOSetTargets(0,nullptr,nullptr);
        deferred->GSSetShader(nullptr,nullptr,0);deferred->HSSetShader(nullptr,nullptr,0);deferred->DSSetShader(nullptr,nullptr,0);
        deferred->VSSetShader(vertex.Get(),nullptr,0);deferred->PSSetShader(drawPixel.Get(),nullptr,0);
        ID3D11Buffer* vs[]={constants.Get(),booleans.Get()};ID3D11Buffer* ps[]={nullptr,depthConstants.Get()};
        deferred->VSSetConstantBuffers(0,2,vs);deferred->PSSetConstantBuffers(0,2,ps);
        std::array<ID3D11Buffer*,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> streams{};streams[0]=vertices.Get();
        std::array<UINT,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> strides{},offsets{};strides[0]=sizeof(MonoVertex);
        deferred->IASetVertexBuffers(0,UINT(streams.size()),streams.data(),strides.data(),offsets.data());
        deferred->IASetInputLayout(layout.Get());deferred->IASetIndexBuffer(indices.Get(),DXGI_FORMAT_R16_UINT,0);
        deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        forEachOriginalR16StripChunk(start,count,[&](uint32_t n,uint32_t first){deferred->DrawIndexed(n,first,base);});
    }
};
}
std::shared_ptr<NativeMonoReplayConstants> NativeBackend::createMonoReplayConstants() {
    validateSubmissionContext();auto state=std::make_unique<NativeMonoReplayConstants::State>();
    state->owner=this;state->thread=owner;state->device=device;
    return std::shared_ptr<NativeMonoReplayConstants>(new NativeMonoReplayConstants(std::move(state)));
}
void NativeBackend::updateMonoReplayConstants(const std::shared_ptr<NativeMonoReplayConstants>& live,const MonoConstants& vertex) {
    validateSubmissionContext();need(bool(live),"Missing mono replay owner");live->state->validate(this,device.Get());monoFinite(vertex);
    live->state->vertex=vertex;live->state->ready=true;
}
void NativeBackend::releaseMonoReplayConstants(const std::shared_ptr<NativeMonoReplayConstants>& live) {
    validateSubmissionContext();need(bool(live),"Missing mono replay owner");live->state->validate(this,device.Get());live->state->active=false;
}
void NativeBackend::recordMonoMesh(const std::shared_ptr<NativeRecordingPayload>& payload,
    const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,
    const std::shared_ptr<NativeMonoMesh>& mesh,const CompiledMaterial& vertex,const CompiledMaterial& pixel,
    const MonoConstants& materialVS,const MonoBooleans& booleans,
    const std::shared_ptr<NativeMonoReplayConstants>& live,const MonoMeshDraw& d) {
    (void)checkedRecordingPayloadContext(payload);need(bool(mesh)&&bool(live),"Missing native mono mesh/replay owner");
    auto& m=*mesh->state;m.validate(device.Get());live->state->validate(this,device.Get());monoFinite(materialVS);
    // Original82740420 ->82701448 (LR82740624) selects this static opaque
    // pair. Other shader/skin recording producers require separate proof.
    need(vertex.originalAddress()==0x82120C04&&pixel.originalAddress()==0x82122BD4&&!m.skinned&&
         !booleans[0]&&!booleans[1]&&!booleans[2]&&!booleans[3],
         "Recorded mono requires its proven static opaque shader pair and zero Boolean bank");
    const auto receipt=recordingPayloadReceipt(payload);
    need(d.primitiveType==6&&d.primitiveReset==1&&d.primitiveResetIndex==0xFFFF,
         "Native mono mesh requires primitive6 R16 triangle strip and restartFFFF");
    need(validR16DrawRange(m.indexValues,m.vertexCount,d.startIndex,d.indexCount,d.baseVertex),
         "Native mono mesh effective index range exceeds its owned vertices or indices");
    need(d.depthPolicy==ShadowMeshDepthPolicy::Reference20e4Rne,"Recorded mono requires explicit Reference20e4Rne depth policy");
    need(d.depthEnable<=1&&d.depthWrite<=1&&d.depthCompare<=7,"Native mono mesh depth state is not canonical");
    need((d.cull==0||d.cull==2||d.cull==6)&&d.fill==0&&(d.colorMask==0||d.colorMask==8||d.colorMask==15),
         "Native mono mesh requires cull0/2/6, solid fill0 and color mask0/8/15");
    need(d.blendEnable<=1&&d.expandedBlend<=1&&
         (d.blendWord==0x00010001||(d.blendEnable==1&&(d.blendWord==0x07060706||d.blendWord==0x00010706))),
         "Recorded mono blend state is unqualified");
    need(!d.stencilEnable&&!d.alphaTest&&!d.alphaToMask&&!d.clipPlaneEnable&&d.viewportEnable==1,
         "Native mono mesh stencil/alpha/user clipping/viewport state is unqualified");
    // Original application force827246C8 selects maskFFFF; SDK8243AC40
    // stores its low16 bits, identical to the SDK defaultFFFFFFFF mask.
    // Both canonical full masks cover this validated single-sample backing.
    need(d.scissorEnable<=1&&d.halfPixelOffset==1&&d.multisampleAntialias==1&&
         (d.multisampleMask==0xFFFF||d.multisampleMask==0xFFFFFFFF),
         "Native mono mesh requires halfpixel1, MSAA request1/full mask and single-sample backing");
    const auto bias=std::bit_cast<float>(d.depthBiasBits),slope=std::bit_cast<float>(d.slopeBiasBits);
    need(std::isfinite(bias)&&std::isfinite(slope)&&std::isfinite(slope*16),"Recorded mono bias is nonfinite or overflows");
    need(target&&depth,"Native mono mesh requires owned color and D24FS8 working attachments");
    DXGI_FORMAT colorFormat{};
    switch(target->format) {
    case TargetFormat::RGBA8:colorFormat=DXGI_FORMAT_R8G8B8A8_UNORM;break;
    case TargetFormat::RGB10A2:colorFormat=DXGI_FORMAT_R10G10B10A2_UNORM;break;
    case TargetFormat::RGBA16Float:colorFormat=DXGI_FORMAT_R16G16B16A16_FLOAT;break;
    case TargetFormat::RGBA32Float:colorFormat=DXGI_FORMAT_R32G32B32A32_FLOAT;break;
    default:throw Error("Unqualified mono color attachment format");
    }
    validateDepthCopyTarget(depth);need(target->texture&&target->view,"Missing native mono color backing");
    const auto* attachment=attachmentDescriptor(*target);const D3D11_TEXTURE2D_DESC colorDesc=attachment?*attachment:D3D11_TEXTURE2D_DESC{};
    need(attachment&&colorDesc.Format==colorFormat&&colorDesc.Width==target->pixelWidth()&&
         colorDesc.Height==target->pixelHeight()&&colorDesc.MipLevels==1&&colorDesc.ArraySize==1&&colorDesc.SampleDesc.Count==1&&
         !colorDesc.SampleDesc.Quality&&depth->width==target->width&&depth->height==target->height,
         "Native mono mesh attachments have a different device, format or extent");
    const bool reverse=d.viewport[4]==0x3F800000&&d.viewport[5]==0;
    need(!d.viewport[0]&&!d.viewport[1]&&d.viewport[2]==target->width&&d.viewport[3]==target->height&&
         (reverse||(d.viewport[4]==0&&d.viewport[5]==0x3F800000)),"Native mono mesh requires a full-target logical viewport with depth1..0 or0..1");
    if(d.scissorEnable)need(d.scissor[0]<d.scissor[2]&&d.scissor[1]<d.scissor[3]&&d.scissor[2]<=target->width&&d.scissor[3]<=target->height,
                            "Native mono mesh scissor exceeds its full viewport");
    // Predictable rejection and allocations precede the first deferred command.
    // Recording never requires or changes the immediate mesh/material bindings.
    auto draw=std::make_shared<MonoRecordedDraw>();draw->owner=this;draw->thread=owner;draw->device=device;
    monoShaderObjects(vertex,pixel,draw->vertex,draw->originalPixel);draw->drawPixel=m.pixel;
    draw->mesh=mesh;draw->live=live;draw->color=target;draw->depth=depth;draw->colorView=target->view;draw->depthView=depth->view;
    draw->vertices=m.vertices;draw->indices=m.indices;draw->layout=m.layout;
    // Each draw needs distinct writable storage: replay uploads material/live
    // inheritance on the immediate context before executing its sealed list.
    D3D11_BUFFER_DESC constantsDesc{};constantsDesc.ByteWidth=sizeof(materialVS);constantsDesc.Usage=D3D11_USAGE_DEFAULT;
    constantsDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;const D3D11_SUBRESOURCE_DATA initial{materialVS.data(),0,0};
    check(device->CreateBuffer(&constantsDesc,&initial,&draw->constants),"recorded constant allocation");
    draw->booleans=immutable(device.Get(),booleans.data(),sizeof(booleans),D3D11_BIND_CONSTANT_BUFFER);
    const DepthConstants depthValues{uint32_t(reverse),d.depthBiasBits,d.slopeBiasBits,0};
    draw->depthConstants=immutable(device.Get(),&depthValues,sizeof(depthValues),D3D11_BIND_CONSTANT_BUFFER);
    draw->depthState=m.depths[(d.depthEnable?16u:0u)|(d.depthWrite?8u:0u)|d.depthCompare];
    const auto maskIndex=d.colorMask==0?0u:(d.colorMask==8?1u:2u);
    auto& alphaBlend=d.colorMask==8?(d.blendEnable?m.blendedAlphaOnly:m.alphaOnly):
        (d.blendEnable?(d.colorMask?m.alphaOn:m.alphaOff):(d.colorMask?m.colorOn:m.colorOff));
    draw->blend=d.blendEnable&&d.blendWord==0x00010001?m.replacementBlend[maskIndex]:
        (d.blendEnable&&d.blendWord==0x00010706?m.separateAlphaBlend[maskIndex]:alphaBlend);
    draw->raster=m.rasterizers[(d.scissorEnable?3u:0u)+(d.cull==0?0u:(d.cull==2?1u:2u))];
    draw->count=d.indexCount;draw->start=d.startIndex;draw->base=d.baseVertex;draw->sampleMask=d.multisampleMask;
    draw->viewport=renderViewport(target,{0,0,float(target->width),float(target->height),0,1});
    draw->scissor=renderScissor(target,d.scissorEnable?D3D11_RECT{LONG(d.scissor[0]),LONG(d.scissor[1]),LONG(d.scissor[2]),LONG(d.scissor[3])}:
                                D3D11_RECT{0,0,LONG(target->width),LONG(target->height)});
    draw->inputMask=receipt.inputMask;const MonoRecordedConstants data{materialVS,booleans,receipt.inputMask};
    NativeRecordingPrepare prepare=[](ID3D11DeviceContext* immediate,const std::shared_ptr<void>& owner,std::span<const uint8_t> bytes) {
        const auto draw=std::static_pointer_cast<MonoRecordedDraw>(owner);
        need(bool(draw)&&bytes.size()==sizeof(MonoRecordedConstants),"Mono recorded constant snapshot changed");
        need(immediate&&immediate->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE&&draw->thread==GetCurrentThreadId(),
             "Mono preparation requires its owning immediate thread/context");
        ComPtr<ID3D11Device> device;immediate->GetDevice(&device);need(device.Get()==draw->device.Get(),"Mono preparation belongs to another device");
        draw->live->state->validate(draw->owner,device.Get());need(draw->live->state->ready,"Mono replay has no completed original uploads");
        MonoRecordedConstants material{};std::memcpy(&material,bytes.data(),sizeof(material));
        need(material.inputMask==draw->inputMask&&!material.booleans[0]&&!material.booleans[1]&&!material.booleans[2]&&!material.booleans[3],
             "Mono recorded inheritance mask or static Boolean bank changed");
        // Original826F3DC0/3F90: MSB-first, four float4 rows per mask bit.
        // The original replay stages only56 VS rows. Keep the rest of the
        // captured244-row material/bone bank; the static Boolean leaves it dead.
        for(size_t row=0;row<56;++row) {
            const auto group=row/4;
            if(material.inputMask[group/8]&(0x80u>>(group%8)))material.vertex[row]=draw->live->state->vertex[row];
        }
        monoFinite(material.vertex);immediate->UpdateSubresource(draw->constants.Get(),0,nullptr,material.vertex.data(),0,0);
    };
    recordRecordingDraw(payload,std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data),sizeof(data)),draw,std::move(prepare),
        [draw](ID3D11DeviceContext* deferred){draw->record(deferred);});
}
void NativeBackend::drawMonoMesh(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,
    const std::shared_ptr<NativeMonoMesh>& mesh,const CompiledMaterial& vertex,const CompiledMaterial& pixel,
    const std::shared_ptr<NativeMonoCommit>& commit,const MonoMeshDraw& d) {
    validateSubmissionContext();need(bool(mesh),"Missing native mono mesh draw owner");auto& m=*mesh->state;m.validate(device.Get());
    need(d.primitiveType==6&&d.primitiveReset==1&&d.primitiveResetIndex==0xFFFF,
         "Native mono mesh requires primitive6 R16 triangle strip and restartFFFF");
    need(validR16DrawRange(m.indexValues,m.vertexCount,d.startIndex,d.indexCount,d.baseVertex),
         "Native mono mesh effective index range exceeds its owned vertices or indices");
    need(d.depthPolicy==ShadowMeshDepthPolicy::Reference20e4Rne,
         "Native mono mesh bias/depth policy is unqualified; explicitly select audited Reference20e4Rne");
    need(d.depthEnable<=1&&d.depthWrite<=1&&d.depthCompare<=7,"Native mono mesh depth state is not canonical");
    need((d.cull==0||d.cull==2||d.cull==6)&&d.fill==0&&(d.colorMask==0||d.colorMask==8||d.colorMask==15),
         "Native mono mesh requires cull0/2/6, solid fill0 and color mask0/8/15");
    // The original mono PS exports exact RGBA one. These replacement and
    // source-alpha equations produce the same exact endpoint with expanded
    // blending on or off; this does not qualify expanded blending for other
    // shaders or blend equations. Preserve the requested channel masks.
    if(!(d.blendEnable<=1&&d.expandedBlend<=1&&
         (d.blendWord==0x00010001||(d.blendEnable==1&&
          (d.blendWord==0x07060706||d.blendWord==0x00010706))))) {
        char message[192];std::snprintf(message,sizeof(message),
            "Native mono mesh blend state is unqualified: enable=%u word=%08X expanded=%08X mask=%X",
            d.blendEnable,d.blendWord,d.expandedBlend,d.colorMask);
        throw Error(message);
    }
    need(!d.stencilEnable&&!d.alphaTest&&!d.alphaToMask&&!d.clipPlaneEnable&&d.viewportEnable==1,
         "Native mono mesh stencil/alpha/user clipping/viewport state is unqualified");
    need(d.scissorEnable<=1&&d.halfPixelOffset==1&&d.multisampleAntialias==1&&
         (d.multisampleMask==0xFFFF||d.multisampleMask==0xFFFFFFFF),
         "Native mono mesh requires halfpixel1, MSAA request1/full mask and single-sample backing");
    const float constant=std::bit_cast<float>(d.depthBiasBits),slope=std::bit_cast<float>(d.slopeBiasBits);
    need(std::isfinite(constant)&&std::isfinite(slope)&&std::isfinite(slope*16.0f),
         "Native mono mesh bias is nonfinite or overflows the original slope-times16 operation");
    need(target&&depth,"Native mono mesh requires owned color and D24FS8 working attachments");
    DXGI_FORMAT colorFormat{};
    switch(target->format) {
    case TargetFormat::RGBA8:colorFormat=DXGI_FORMAT_R8G8B8A8_UNORM;break;
    case TargetFormat::RGB10A2:colorFormat=DXGI_FORMAT_R10G10B10A2_UNORM;break;
    case TargetFormat::RGBA16Float:colorFormat=DXGI_FORMAT_R16G16B16A16_FLOAT;break;
    case TargetFormat::RGBA32Float:colorFormat=DXGI_FORMAT_R32G32B32A32_FLOAT;break;
    default:throw Error("Unqualified mono color attachment format");
    }
    validateDepthCopyTarget(depth);
    need(target->texture&&target->view,"Missing native mono color backing");
    const auto* attachment=attachmentDescriptor(*target);const D3D11_TEXTURE2D_DESC colorDesc=attachment?*attachment:D3D11_TEXTURE2D_DESC{};
    need(attachment&&colorDesc.Format==colorFormat&&
         colorDesc.Width==target->pixelWidth()&&colorDesc.Height==target->pixelHeight()&&colorDesc.MipLevels==1&&colorDesc.ArraySize==1&&
         colorDesc.SampleDesc.Count==1&&!colorDesc.SampleDesc.Quality&&depth->width==target->width&&depth->height==target->height,
         "Native mono mesh attachments have a different device, format or extent");
    const bool reverse=d.viewport[4]==0x3F800000&&d.viewport[5]==0;
    need(!d.viewport[0]&&!d.viewport[1]&&d.viewport[2]==target->width&&d.viewport[3]==target->height&&
         (reverse||(d.viewport[4]==0&&d.viewport[5]==0x3F800000)),
         "Native mono mesh requires a full-target logical viewport with depth1..0 or0..1");
    if(d.scissorEnable) {
        need(d.scissor[0]<d.scissor[2]&&d.scissor[1]<d.scissor[3]&&d.scissor[2]<=target->width&&d.scissor[3]<=target->height,
             "Native mono mesh scissor exceeds its full viewport");
        const auto retained=scissor();need(retained&&*retained==d.scissor,"Native mono mesh scissor differs from the original retained rectangle");
    }
    requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);requireNoOutputUavs();
    requireMonoShaders(vertex,pixel);requireMonoCommit(commit,m.skinned);m.requireBindings(context.Get());requireNoAuxiliaryStages(context.Get());
    const DepthConstants values{uint32_t(reverse),d.depthBiasBits,d.slopeBiasBits,0};
    // Reuse identical immutable buffers; never update in flight. Miss creates
    // a NEW immutable buffer with the same descriptor, published only after
    // success; eviction keeps old GPU/live refs alive via caller ComPtr.
    ComPtr<ID3D11Buffer> constants;
    if(auto* hit=m.depthConstants.find(values))constants=hit;
    else{constants=immutable(device.Get(),&values,sizeof(values),D3D11_BIND_CONSTANT_BUFFER);m.depthConstants.publish(values,constants.Get());}
    // All rejection and allocation precede the flush or mono context mutation.
    flushIm2D();
    requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);
    requireMonoShaders(vertex,pixel);requireMonoCommit(commit,m.skinned);m.requireBindings(context.Get());
    {
        ScopedDrawBindings restore(context.Get());
        setViewport({0,0,float(target->width),float(target->height),0,1});
        // halfpixel1 already uses native half-integer centers; no 0.5 XY shift.
        const UINT raster=(d.scissorEnable?3u:0u)+(d.cull==0?0u:(d.cull==2?1u:2u));
        context->RSSetState(m.rasterizers[raster].Get());
        auto& blendState=d.colorMask==8?(d.blendEnable?m.blendedAlphaOnly:m.alphaOnly):
            (d.blendEnable?(d.colorMask?m.alphaOn:m.alphaOff):(d.colorMask?m.colorOn:m.colorOff));
        const auto maskIndex=d.colorMask==0?0u:(d.colorMask==8?1u:2u);
        auto* selectedBlend=d.blendEnable&&d.blendWord==0x00010001?m.replacementBlend[maskIndex].Get():
            (d.blendEnable&&d.blendWord==0x00010706?m.separateAlphaBlend[maskIndex].Get():blendState.Get());
        context->OMSetBlendState(selectedBlend,nullptr,d.multisampleMask);
        context->OMSetDepthStencilState(m.depths[(d.depthEnable?16u:0u)|(d.depthWrite?8u:0u)|d.depthCompare].Get(),0);
        auto* cb=constants.Get();context->PSSetConstantBuffers(1,1,&cb);context->PSSetShader(m.pixel.Get(),nullptr,0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        // R16 triangle strips consume FFFF as cut indices, including parity reset.
        forEachOriginalR16StripChunk(d.startIndex,d.indexCount,[&](uint32_t n,uint32_t first){context->DrawIndexed(n,first,d.baseVertex);});
        ++monoMeshDraws;
    }
    requireMonoShaders(vertex,pixel);requireMonoCommit(commit,m.skinned);m.requireBindings(context.Get());
    requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);requireOwner();
}
}
