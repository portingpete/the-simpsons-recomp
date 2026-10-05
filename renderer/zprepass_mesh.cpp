#include "zprepass_mesh.h"
#include "common/geometry_extent.h"
#include "immutable_buffer_proof.h"
#include "immutable_depth_constants.h"
#include "mesh_upload_cache.h"
#include "r16_index_validation.h"
#include "r16_strip_chunks.h"
#include "VSZPrepass.h"
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
    ID3D11Device* device = nullptr;
};
namespace {
void need(bool value,const char* message) {if(!value)throw Error(message);}
void check(HRESULT hr,const char* operation) {
    if(FAILED(hr)) {
        char message[192];std::snprintf(message,sizeof(message),"Native Z-prepass mesh %s failed: %08lX",operation,ULONG(hr));
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
}

struct NativeZPrepassMesh::State {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Buffer> vertices,indices;
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
        need(device.Get()==expected&&vertices&&indices&&layout&&pixel&&colorOff,
             "Native Z-prepass mesh is missing, retired or belongs to another device");
        if(proof.holds(vertices.Get(),indices.Get(),expected,vertexCount,indexValues.size()))return;
        for(auto* buffer:{vertices.Get(),indices.Get()}) {
            ComPtr<ID3D11Device> actual;buffer->GetDevice(&actual);D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);
            const bool vertex=buffer==vertices.Get();
            need(actual.Get()==expected&&desc.Usage==D3D11_USAGE_IMMUTABLE&&
                 desc.ByteWidth==(vertex?vertexCount*sizeof(ZPrepassVertex):indexValues.size()*sizeof(uint16_t))&&
                 desc.BindFlags==(vertex?D3D11_BIND_VERTEX_BUFFER:D3D11_BIND_INDEX_BUFFER)&&
                 !desc.CPUAccessFlags&&!desc.MiscFlags&&!desc.StructureByteStride,
                 "Native Z-prepass mesh backing differs from its immutable owner");
        }
        proof.prove(vertices.Get(),indices.Get(),expected,vertexCount,indexValues.size());
    }
    void requireBindings(ID3D11DeviceContext* context) const {
        ComPtr<ID3D11Buffer> vb,ib;UINT stride{},offset{},indexOffset{};DXGI_FORMAT format{};
        ComPtr<ID3D11InputLayout> input;
        context->IAGetVertexBuffers(0,1,&vb,&stride,&offset);context->IAGetInputLayout(&input);
        context->IAGetIndexBuffer(&ib,&format,&indexOffset);
        need(vb.Get()==vertices.Get()&&stride==sizeof(ZPrepassVertex)&&!offset,
             "Native Z-prepass mesh vertex binding is stale or has a different stride/offset");
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
    if(!zprepassMeshCache_) zprepassMeshCache_ = std::make_shared<ZPrepassMeshUploadCache>();
    auto& slot = *zprepassMeshCache_;
    if(slot.device != device.Get()) {
        slot.cache.clear();
        slot.device = device.Get();
    }
    const auto cached = slot.cache.lookup(vertexBytes, indexBytes);
    if(auto hit = cached.mesh) {
        try {
            hit->state->validate(device.Get());
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
        for(float value:v.position)need(std::isfinite(value),"Nonfinite Z-prepass mesh position");
        const auto zero=[](float value){return std::bit_cast<uint32_t>(value)==0;};
        for(float value:v.weights)need(zero(value),"Static Z-prepass requires zero unused weights");
        for(float value:v.indices)need(zero(value),"Static Z-prepass requires zero unused indices");
        for(const auto& delta:v.morph)for(float value:delta)
            need(zero(value),"Static Z-prepass requires zero unused morph inputs");
    }
    auto next=std::make_unique<NativeZPrepassMesh::State>();next->device=device;
    next->vertexCount=uint32_t(vertices.size());next->indexValues.assign(indices.begin(),indices.end());
    next->vertices=immutable(device.Get(),vertices.data(),UINT(vertices.size_bytes()),D3D11_BIND_VERTEX_BUFFER);
    next->indices=immutable(device.Get(),indices.data(),UINT(indices.size_bytes()),D3D11_BIND_INDEX_BUFFER);
    std::array<D3D11_INPUT_ELEMENT_DESC,9> elements{};
    elements[0]={"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
    elements[1]={"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0};
    elements[2]={"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,28,D3D11_INPUT_PER_VERTEX_DATA,0};
    for(UINT i=0;i<6;++i)
        elements[3+i]={"TEXCOORD",3+i,DXGI_FORMAT_R32G32B32_FLOAT,0,44+12*i,D3D11_INPUT_PER_VERTEX_DATA,0};
    check(device->CreateInputLayout(elements.data(),UINT(elements.size()),kVSZPrepass,sizeof(kVSZPrepass),&next->layout),"declaration creation");
    check(device->CreatePixelShader(kPSShadowMeshDepth,sizeof(kPSShadowMeshDepth),nullptr,&next->pixel),"depth adapter creation");
    D3D11_BLEND_DESC blend{};auto& color=blend.RenderTarget[0];color.RenderTargetWriteMask=0;
    color.SrcBlend=color.SrcBlendAlpha=D3D11_BLEND_ONE;color.DestBlend=color.DestBlendAlpha=D3D11_BLEND_ZERO;
    color.BlendOp=color.BlendOpAlpha=D3D11_BLEND_OP_ADD;
    check(device->CreateBlendState(&blend,&next->colorOff),"color-disabled state creation");
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
    auto result = std::shared_ptr<NativeZPrepassMesh>(new NativeZPrepassMesh(std::move(next)));
    // Snapshot exact bytes so later caller mutation cannot corrupt the cache.
    // Eviction drops only the cache's strong ref; live caller handles stay valid.
    slot.cache.insert(vertexBytes, indexBytes, cached.key, result);
    return result;
}
void NativeBackend::bindZPrepassMeshVertices(const std::shared_ptr<NativeZPrepassMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing native Z-prepass mesh vertex owner");mesh->state->validate(device.Get());
    flushIm2D();auto* buffer=mesh->state->vertices.Get();const UINT stride=sizeof(ZPrepassVertex),offset=0;
    context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
    ComPtr<ID3D11Buffer> actual;UINT s{},o{};context->IAGetVertexBuffers(0,1,&actual,&s,&o);
    need(actual.Get()==buffer&&s==stride&&o==offset,"D3D11 did not retain Z-prepass mesh vertices");requireOwner();
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
    return readBuffer<ZPrepassVertex>(device.Get(),context.Get(),mesh->state->vertices.Get());
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
         "Native Z-prepass mesh requires halfpixel1, MSAA request1/full mask and single-sample backing");
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
         colorDesc.SampleDesc.Count==1&&!colorDesc.SampleDesc.Quality&&depth->width==target->width&&depth->height==target->height,
         "Native Z-prepass mesh attachments have a different device, format or extent");
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
    // Reuse identical immutable buffers; never update in flight. Miss creates
    // a NEW immutable buffer with the same descriptor, published only after
    // success; eviction keeps old GPU/live refs alive via caller ComPtr.
    ComPtr<ID3D11Buffer> constants;
    if(auto* hit=m.depthConstants.find(values))constants=hit;
    else{constants=immutable(device.Get(),&values,sizeof(values),D3D11_BIND_CONSTANT_BUFFER);m.depthConstants.publish(values,constants.Get());}
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
        // R16 triangle strips consume FFFF as cut indices, including parity reset.
        forEachOriginalR16StripChunk(d.startIndex,d.indexCount,[&](uint32_t n,uint32_t first){context->DrawIndexed(n,first,d.baseVertex);});
        ++zprepassMeshDraws;
    }
    requireZPrepassShader(vertex);requireZPrepassCommit(commit);m.requireBindings(context.Get());
    requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);requireOwner();
}
}
