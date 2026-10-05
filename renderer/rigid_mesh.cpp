#include "rigid_mesh.h"
#include "common/geometry_extent.h"
#include "immutable_buffer_proof.h"
#include "mesh_upload_cache.h"
#include "r16_index_validation.h"
#include "r16_strip_chunks.h"
#include "material_resources.h"
#include "VSRigid.h"
#include "VSRigidDualTextured.h"
#include "VSRigidNormalTangent.h"
#include "VSChocolate.h"
#include "VSChocolateOpaque.h"
#include "PSChocolateOpaqueDraw.h"
#include "PSProjtexDraw.h"
#include "PSProjtexAlphaDraw.h"
#include "VSVfxRigid.h"
#include "PSRigidDraw.h"
#include "PSRigidTexturedDraw.h"
#include "PSVfxRigidDraw.h"
#include "PSRigidDualTexturedDraw.h"
#include "PSRigidDualTexturedUVDraw.h"
#include "PSRigidDualTexturedUVAlphaDraw.h"
#include "PSRigidUVDraw.h"
#include "PSRigidUVAlphaDraw.h"
#include "PSFlipbookDraw.h"
#include "PSFlipbookAlphaDraw.h"
#include "PSRigidGlossDraw.h"
#include "PSRigidMultitoneDraw.h"
#include "PSRigidNormalmapDraw.h"
#include "PSRigidGlossAlphaDraw.h"
#include "PSRigidMultitoneAlphaDraw.h"
#include "PSRigidNormalmapAlphaDraw.h"
#include "PS168F8Draw.h"
#include "PSRigidAlphaDraw.h"
#include "PSChocolateDraw.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>

namespace Simpsons::Graphics {
// Per-backend exact-content cache, never cross-device.
struct RigidMeshUploadCache {
    ExactContentMeshCache<NativeRigidMesh> cache;
};
namespace {
void need(bool ok,const char* message){if(!ok)throw Error(message);}
void check(HRESULT result,const char* operation) {
    if(FAILED(result)){char message[180];std::snprintf(message,sizeof(message),"Native rigid mesh %s failed: %08lX",operation,ULONG(result));throw Error(message);}
}
template<class T>void finite(const T& bank) {
    for(const auto& row:bank)for(float value:row)need(std::isfinite(value),"Native rigid constants contain a nonfinite value");
}
ComPtr<ID3D11Buffer> buffer(ID3D11Device* device,const void* data,UINT bytes,UINT binding,D3D11_USAGE usage) {
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;desc.BindFlags=binding;desc.Usage=usage;
    const D3D11_SUBRESOURCE_DATA initial{data,0,0};ComPtr<ID3D11Buffer> result;
    check(device->CreateBuffer(&desc,data?&initial:nullptr,&result),"buffer allocation");return result;
}
template<class T>std::vector<T> readBuffer(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Buffer* source) {
    D3D11_BUFFER_DESC desc{};source->GetDesc(&desc);const auto bytes=desc.ByteWidth;
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;check(device->CreateBuffer(&desc,nullptr,&staging),"readback allocation");
    std::vector<T> result(bytes/sizeof(T));context->CopyResource(staging.Get(),source);
    D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"readback map");
    std::memcpy(result.data(),mapped.pData,bytes);context->Unmap(staging.Get(),0);return result;
}
void samplerProfile(const D3D11_SAMPLER_DESC& sampler) {
    const bool pointClamp=sampler.Filter==D3D11_FILTER_MIN_MAG_MIP_POINT&&sampler.AddressU==D3D11_TEXTURE_ADDRESS_CLAMP&&
         sampler.AddressV==D3D11_TEXTURE_ADDRESS_CLAMP&&sampler.AddressW==D3D11_TEXTURE_ADDRESS_CLAMP;
    const bool linearWrap=sampler.Filter==D3D11_FILTER_MIN_MAG_MIP_LINEAR&&sampler.AddressU==D3D11_TEXTURE_ADDRESS_WRAP&&
         sampler.AddressV==D3D11_TEXTURE_ADDRESS_WRAP&&sampler.AddressW==D3D11_TEXTURE_ADDRESS_WRAP;
    // Retained older explicit backend profile (not the original SDK mapping).
    const bool pointMixed=sampler.Filter==D3D11_FILTER_MIN_MAG_MIP_POINT&&sampler.AddressU==D3D11_TEXTURE_ADDRESS_WRAP&&
         sampler.AddressV==D3D11_TEXTURE_ADDRESS_WRAP&&sampler.AddressW==D3D11_TEXTURE_ADDRESS_CLAMP;
    // 168F8's stage1 is unsampled, but carries either preceding linear/clamp
    // or point/mirror state. Both original mip2 states select the base level.
    const bool baseClamp=sampler.Filter==D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT&&sampler.AddressU==D3D11_TEXTURE_ADDRESS_CLAMP&&
         sampler.AddressV==D3D11_TEXTURE_ADDRESS_CLAMP&&sampler.AddressW==D3D11_TEXTURE_ADDRESS_CLAMP;
    const bool baseMirror=sampler.Filter==D3D11_FILTER_MIN_MAG_MIP_POINT&&sampler.AddressU==D3D11_TEXTURE_ADDRESS_MIRROR&&
         sampler.AddressV==D3D11_TEXTURE_ADDRESS_MIRROR&&sampler.AddressW==D3D11_TEXTURE_ADDRESS_CLAMP;
    need((((linearWrap||pointMixed)&&sampler.MaxLOD==13)||((pointClamp||baseClamp||baseMirror)&&sampler.MaxLOD==0))&&
         sampler.MipLODBias==0&&sampler.MinLOD==0&&sampler.MaxAnisotropy==1&&
         sampler.ComparisonFunc==D3D11_COMPARISON_NEVER,"Native rigid requires explicit point/clamp, linear/wrap or 168F8-mixed single-level shadow samplers");
    for(float value:sampler.BorderColor)need(std::isfinite(value),"Native rigid sampler border is nonfinite");
}
struct DepthConstants {uint32_t reverse,constantBits,slopeBits,reserved;};
void baseSamplerProfile(const D3D11_SAMPLER_DESC& sampler) {
    need(sampler.Filter==D3D11_FILTER_MIN_MAG_MIP_LINEAR&&sampler.AddressU==D3D11_TEXTURE_ADDRESS_WRAP&&
         sampler.AddressV==D3D11_TEXTURE_ADDRESS_WRAP&&sampler.AddressW==D3D11_TEXTURE_ADDRESS_WRAP&&
         sampler.MipLODBias==0&&sampler.MinLOD==0&&sampler.MaxLOD==13&&sampler.MaxAnisotropy==1&&
         sampler.ComparisonFunc==D3D11_COMPARISON_NEVER,"Native textured rigid requires original linear/wrap base sampling");
    for(float value:sampler.BorderColor)need(std::isfinite(value),"Native textured sampler border is nonfinite");
}
void projectionSamplerProfile(const D3D11_SAMPLER_DESC& sampler) {
    need(sampler.Filter==D3D11_FILTER_MIN_MAG_MIP_LINEAR&&sampler.AddressU==D3D11_TEXTURE_ADDRESS_CLAMP&&
         sampler.AddressV==D3D11_TEXTURE_ADDRESS_CLAMP&&sampler.AddressW==D3D11_TEXTURE_ADDRESS_CLAMP&&
         sampler.MipLODBias==0&&sampler.MinLOD==0&&sampler.MaxLOD==13&&sampler.MaxAnisotropy==1&&
         sampler.ComparisonFunc==D3D11_COMPARISON_NEVER,"Native projtex requires original linear/clamp projection sampling");
    for(float value:sampler.BorderColor)need(std::isfinite(value),"Native projection sampler border is nonfinite");
}
// Direct rendering changes only these transient adapter bindings. The original
// shaders, committed b0 banks, IA owners and sampled shadow SRVs stay qualified.
class ScopedRigidDrawBindings {
    ID3D11DeviceContext* context;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11RasterizerState> raster;
    std::array<ComPtr<ID3D11SamplerState>,4> samplers;
    UINT stencil{},sampleMask{};
    FLOAT factors[4]{};
    D3D11_PRIMITIVE_TOPOLOGY topology{};
    std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
    std::array<D3D11_RECT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors{};
    UINT viewportCount=UINT(viewports.size()),scissorCount=UINT(scissors.size());
public:
    explicit ScopedRigidDrawBindings(ID3D11DeviceContext* c):context(c) {
        context->PSGetShader(&pixel,nullptr,nullptr);context->PSGetConstantBuffers(1,1,&constants);
        context->OMGetDepthStencilState(&depth,&stencil);context->OMGetBlendState(&blend,factors,&sampleMask);
        context->RSGetState(&raster);context->RSGetViewports(&viewportCount,viewports.data());
        context->RSGetScissorRects(&scissorCount,scissors.data());context->IAGetPrimitiveTopology(&topology);
        for(UINT i=0;i<samplers.size();++i)context->PSGetSamplers(i,1,&samplers[i]);
    }
    ScopedRigidDrawBindings(const ScopedRigidDrawBindings&)=delete;
    ScopedRigidDrawBindings& operator=(const ScopedRigidDrawBindings&)=delete;
    ~ScopedRigidDrawBindings() {
        context->PSSetShader(pixel.Get(),nullptr,0);auto* cb=constants.Get();context->PSSetConstantBuffers(1,1,&cb);
        context->OMSetDepthStencilState(depth.Get(),stencil);context->OMSetBlendState(blend.Get(),factors,sampleMask);
        context->RSSetState(raster.Get());context->RSSetViewports(viewportCount,viewports.data());
        context->RSSetScissorRects(scissorCount,scissors.data());context->IASetPrimitiveTopology(topology);
        for(UINT i=0;i<samplers.size();++i){auto* sampler=samplers[i].Get();context->PSSetSamplers(i,1,&sampler);}
    }
};
void requireRigidDrawStages(ID3D11DeviceContext* context) {
    ComPtr<ID3D11Predicate> predicate;BOOL value{};context->GetPredication(&predicate,&value);
    need(!predicate,"Native rigid mesh predication is unqualified");
    ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;
    context->GSGetShader(&gs,nullptr,nullptr);context->HSGetShader(&hs,nullptr,nullptr);context->DSGetShader(&ds,nullptr,nullptr);
    need(!gs&&!hs&&!ds,"Native rigid mesh has an unrelated geometry/tessellation stage");
    std::array<ID3D11Buffer*,D3D11_SO_BUFFER_SLOT_COUNT> outputs{};context->SOGetTargets(UINT(outputs.size()),outputs.data());
    bool bound=false;for(auto* output:outputs)if(output){bound=true;output->Release();}
    need(!bound,"Native rigid mesh stream output is unqualified");
}
// Only value data is serialized into the native receipt. Resource/state COM
// ownership lives separately; these bytes are NOT original SDK command bytes.
struct RecordedConstants {
    RigidVertexConstants vertex;
    RigidPixelConstants pixel;
    NativeRecordingMask inputMask;
};
static_assert(sizeof(DepthConstants)==16&&sizeof(RecordedConstants)==1624&&std::is_trivially_copyable_v<RecordedConstants>);
template<class T>void inherit(T& material,const T& live,const NativeRecordingMask& mask,size_t offset) {
    // Original 826F3DC0/3F90: MSB-first bits, FOUR float4 registers per bit.
    // The remaining groups describe original registers not read by this pair.
    for(size_t row=0;row<material.size();++row) {
        const size_t group=row/4;
        if(mask[offset+group/8]&(0x80u>>(group%8)))material[row]=live[row];
    }
}
struct RecordedDraw {
    ComPtr<ID3D11Device> device;
    const NativeBackend* owner{};
    DWORD thread{};
    std::shared_ptr<NativeRigidMesh> mesh;
    std::shared_ptr<NativeRigidReplayConstants> live;
    std::shared_ptr<RenderTarget> colorOwner;
    std::shared_ptr<DepthTarget> depthOwner;
    std::array<std::shared_ptr<DepthTarget>,2> shadowOwners;
    std::array<std::shared_ptr<Texture>,3> materialOwners;
    std::array<ComPtr<ID3D11ShaderResourceView>,3> materialViews;
    std::array<ComPtr<ID3D11SamplerState>,3> materialSamplers;
    std::array<std::shared_ptr<Texture>,2> uvOwners;
    std::array<ComPtr<ID3D11ShaderResourceView>,2> uvViews;
    std::array<ComPtr<ID3D11SamplerState>,2> uvSamplers;
    std::shared_ptr<Texture> baseOwner;
    ComPtr<ID3D11ShaderResourceView> baseView;
    ComPtr<ID3D11SamplerState> baseSampler;
    std::shared_ptr<Texture> noiseOwner;
    ComPtr<ID3D11ShaderResourceView> noiseView;
    ComPtr<ID3D11SamplerState> noiseSampler;
    ComPtr<ID3D11Buffer> vertices,indices,vertexConstants,pixelConstants,depthConstants;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> originalPixel,drawPixel;
    ComPtr<ID3D11RenderTargetView> color;
    ComPtr<ID3D11DepthStencilView> depth;
    std::array<ComPtr<ID3D11ShaderResourceView>,2> shadows;
    std::array<ComPtr<ID3D11SamplerState>,2> samplers;
    ComPtr<ID3D11DepthStencilState> depthState;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11BlendState> blend;
    D3D11_VIEWPORT viewport{};
    D3D11_RECT scissor{};
    uint32_t indexCount{},startIndex{},sampleMask{},baseStage=2,originalAddress{},materialCount{},materialStage{};
    int32_t baseVertex{};
    bool chocolate{},chocolateAlpha{},uv{},uvAlpha{};
    NativeRecordingMask inputMask{};
    void record(ID3D11DeviceContext* destination) const {
        // Complete bindings on the DEFERRED context. No Get* query, no upload,
        // immediate-context operation, or callback into the CPU renderer here.
        auto* target=color.Get();destination->OMSetRenderTargets(1,&target,depth.Get());
        destination->OMSetBlendState(blend.Get(),nullptr,sampleMask);destination->OMSetDepthStencilState(depthState.Get(),0);
        destination->RSSetState(raster.Get());destination->RSSetViewports(1,&viewport);destination->RSSetScissorRects(1,&scissor);
        destination->SetPredication(nullptr,FALSE);destination->SOSetTargets(0,nullptr,nullptr);
        destination->GSSetShader(nullptr,nullptr,0);destination->HSSetShader(nullptr,nullptr,0);destination->DSSetShader(nullptr,nullptr,0);
        destination->VSSetShader(vertex.Get(),nullptr,0);destination->PSSetShader(drawPixel.Get(),nullptr,0);
        ID3D11Buffer* vbanks[]={vertexConstants.Get(),nullptr};destination->VSSetConstantBuffers(0,2,vbanks);
        ID3D11Buffer* pbanks[]={pixelConstants.Get(),depthConstants.Get()};destination->PSSetConstantBuffers(0,2,pbanks);
        if(uv) {
            ID3D11ShaderResourceView* views[]={uvAlpha?uvViews[0].Get():shadows[0].Get(),
                uvAlpha?uvViews[1].Get():uvViews[0].Get(),uvAlpha?nullptr:uvViews[1].Get()};
            destination->PSSetShaderResources(0,3,views);
            ID3D11SamplerState* states[]={uvAlpha?uvSamplers[0].Get():samplers[0].Get(),
                uvAlpha?uvSamplers[1].Get():uvSamplers[0].Get(),uvAlpha?nullptr:uvSamplers[1].Get()};
            destination->PSSetSamplers(0,3,states);
        } else if(chocolate) {
            if(!chocolateAlpha) {
                ID3D11ShaderResourceView* depths[]={shadows[0].Get(),shadows[1].Get()};destination->PSSetShaderResources(0,2,depths);
                ID3D11SamplerState* shadowStates[]={samplers[0].Get(),samplers[1].Get()};destination->PSSetSamplers(0,2,shadowStates);
            }
            ID3D11ShaderResourceView* views[]={materialViews[0].Get(),materialViews[1].Get(),materialViews[2].Get()};destination->PSSetShaderResources(materialStage,materialCount,views);
            ID3D11SamplerState* states[]={materialSamplers[0].Get(),materialSamplers[1].Get(),materialSamplers[2].Get()};destination->PSSetSamplers(materialStage,materialCount,states);
        } else {
            ID3D11ShaderResourceView* views[]={shadows[0].Get(),shadows[1].Get()};destination->PSSetShaderResources(0,2,views);
            ID3D11SamplerState* states[]={samplers[0].Get(),samplers[1].Get()};destination->PSSetSamplers(0,2,states);
            auto* base=baseView.Get();destination->PSSetShaderResources(baseStage,1,&base);
            auto* sampler=baseSampler.Get();destination->PSSetSamplers(baseStage,1,&sampler);
        }
        if(!chocolate) {
            auto* noise=noiseView.Get();destination->PSSetShaderResources(3,1,&noise);
            auto* ns=noiseSampler.Get();destination->PSSetSamplers(3,1,&ns);
        }
        std::array<ID3D11Buffer*,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> streams{};streams[0]=vertices.Get();
        std::array<UINT,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> strides{},offsets{};strides[0]=sizeof(RigidVertex);
        destination->IASetVertexBuffers(0,UINT(streams.size()),streams.data(),strides.data(),offsets.data());
        destination->IASetInputLayout(layout.Get());destination->IASetIndexBuffer(indices.Get(),DXGI_FORMAT_R16_UINT,0);
        destination->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        forEachOriginalR16StripChunk(startIndex,indexCount,[&](uint32_t n,uint32_t first){destination->DrawIndexed(n,first,baseVertex);});
    }
};
}

struct NativeRigidMesh::State {
    const NativeBackend* owner{};DWORD thread{};ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Buffer> vertices,indices;ComPtr<ID3D11InputLayout> layout,tangentLayout,chocolateLayout,chocolateOpaqueLayout,vfxLayout;ComPtr<ID3D11PixelShader> vfxPixel,pixel,texturedPixel,dualPixel,dualUVPixel,dualUVAlphaPixel,singleUVPixel,singleUVAlphaPixel,flipbookPixel,flipbookAlphaPixel,glossPixel,multitonePixel,normalmapPixel,glossAlphaPixel,multitoneAlphaPixel,normalmapAlphaPixel,a168Pixel,chocPixel,chocOpaquePixel,projtexPixel,projtexAlphaPixel,rigidAlphaPixel;
    uint32_t vertexCount{};R16Indices indexValues;
    mutable ImmutableBufferPairProof proof;
    void validate(const NativeBackend* backend,ID3D11Device* expected) const {
        need(owner==backend&&thread==GetCurrentThreadId()&&device.Get()==expected&&vertices&&indices&&layout&&tangentLayout&&chocolateLayout&&chocolateOpaqueLayout&&vfxLayout&&vfxPixel&&pixel&&texturedPixel&&dualPixel&&dualUVPixel&&dualUVAlphaPixel&&singleUVPixel&&singleUVAlphaPixel&&flipbookPixel&&flipbookAlphaPixel&&glossPixel&&multitonePixel&&normalmapPixel&&glossAlphaPixel&&multitoneAlphaPixel&&normalmapAlphaPixel&&a168Pixel&&chocPixel&&chocOpaquePixel&&projtexPixel&&projtexAlphaPixel&&rigidAlphaPixel,
             "Native rigid mesh is missing, stale or belongs to another backend");
        if(proof.holds(vertices.Get(),indices.Get(),expected,vertexCount,indexValues.size()))return;
        for(auto* value:{vertices.Get(),indices.Get()}) {
            D3D11_BUFFER_DESC desc{};value->GetDesc(&desc);ComPtr<ID3D11Device> actual;value->GetDevice(&actual);
            const bool vertex=value==vertices.Get();
            need(actual.Get()==expected&&desc.Usage==D3D11_USAGE_IMMUTABLE&&
                 desc.ByteWidth==(vertex?vertexCount*sizeof(RigidVertex):indexValues.size()*sizeof(uint16_t))&&
                 desc.BindFlags==(vertex?D3D11_BIND_VERTEX_BUFFER:D3D11_BIND_INDEX_BUFFER)&&
                 !desc.CPUAccessFlags&&!desc.MiscFlags&&!desc.StructureByteStride,"Native rigid immutable mesh backing differs from its owner");
        }
        proof.prove(vertices.Get(),indices.Get(),expected,vertexCount,indexValues.size());
    }
    void requireBindings(ID3D11DeviceContext* context,bool vfx=false) const {
        ComPtr<ID3D11Buffer> vb,ib;ComPtr<ID3D11InputLayout> input;UINT stride{},offset{},indexOffset{};DXGI_FORMAT format{};
        context->IAGetVertexBuffers(0,1,&vb,&stride,&offset);context->IAGetInputLayout(&input);
        context->IAGetIndexBuffer(&ib,&format,&indexOffset);
        need(vb.Get()==vertices.Get()&&stride==sizeof(RigidVertex)&&!offset,"Native rigid mesh vertex binding is stale or has a different stride/offset");
        need(input.Get()==(vfx?vfxLayout.Get():layout.Get()),"Native rigid mesh declaration binding is stale");
        need(ib.Get()==indices.Get()&&format==DXGI_FORMAT_R16_UINT&&!indexOffset,"Native rigid mesh index binding is stale or has a different format/offset");
    }
    void requireTangentBindings(ID3D11DeviceContext* context,bool chocolate,bool alpha=true) const {
        ComPtr<ID3D11Buffer> vb,ib;ComPtr<ID3D11InputLayout> input;UINT stride{},offset{},indexOffset{};DXGI_FORMAT format{};
        context->IAGetVertexBuffers(0,1,&vb,&stride,&offset);context->IAGetInputLayout(&input);
        context->IAGetIndexBuffer(&ib,&format,&indexOffset);
        need(vb.Get()==vertices.Get()&&stride==sizeof(RigidVertex)&&!offset,"Native rigid mesh vertex binding is stale or has a different stride/offset");
        need(input.Get()==(chocolate?(alpha?chocolateLayout.Get():chocolateOpaqueLayout.Get()):tangentLayout.Get()),"Native rigid tangent mesh declaration binding is stale");
        need(ib.Get()==indices.Get()&&format==DXGI_FORMAT_R16_UINT&&!indexOffset,"Native rigid mesh index binding is stale or has a different format/offset");
    }
};
struct NativeRigidReplayConstants::State {
    const NativeBackend* owner{};DWORD thread{};ComPtr<ID3D11Device> device;bool active=true;
    RigidVertexConstants vertex{};RigidPixelConstants pixel{};
    void validate(const NativeBackend* backend,ID3D11Device* expected) const {
        need(active&&owner==backend&&thread==GetCurrentThreadId()&&device.Get()==expected,
             "Native rigid replay constants are released, stale or belong to another backend");
    }
};
NativeRigidMesh::NativeRigidMesh(std::unique_ptr<State> value):state(std::move(value)){}
NativeRigidMesh::~NativeRigidMesh()=default;
uint32_t NativeRigidMesh::vertexCount() const noexcept{return state->vertexCount;}
uint32_t NativeRigidMesh::indexCount() const noexcept{return uint32_t(state->indexValues.size());}
NativeRigidReplayConstants::NativeRigidReplayConstants(std::unique_ptr<State> value):state(std::move(value)){}
NativeRigidReplayConstants::~NativeRigidReplayConstants()=default;

std::shared_ptr<NativeRigidMesh> NativeBackend::uploadRigidMesh(std::span<const RigidVertex> vertices,std::span<const uint16_t> indices) {
    validateSubmissionContext();
    need(validNativeMeshBufferExtent(vertices.size(),sizeof(RigidVertex))&&validNativeMeshBufferExtent(indices.size(),sizeof(uint16_t)),
         "Native rigid mesh exceeds buffer byte or index extent");
    // Exact decoded bytes only: never guest address, never hash-only.
    const std::span<const uint8_t> vertexBytes{reinterpret_cast<const uint8_t*>(vertices.data()), vertices.size_bytes()};
    const std::span<const uint8_t> indexBytes{reinterpret_cast<const uint8_t*>(indices.data()), indices.size_bytes()};
    const uint64_t contentKey =
        ExactContentMeshCache<NativeRigidMesh>::contentKey(vertexBytes, indexBytes);
    if(!rigidMeshCache_) rigidMeshCache_ = std::make_shared<RigidMeshUploadCache>();
    auto& slot = *rigidMeshCache_;
    if(auto hit = slot.cache.find(vertexBytes, indexBytes, contentKey)) {
        // Exact immutable bytes were validated before insertion; stale
        // wrong-device resources fail here and propagate, never reupload.
        hit->state->validate(this, device.Get());
        requireOwner();
        return hit;
    }
    for(const auto& v:vertices) {
        for(float x:v.position)need(std::isfinite(x),"Nonfinite rigid position");
        // Original packed normals may be zero. The qualified PS slot15 keeps
        // their SM3 zero-product result; immutable upload preserves all bits.
        for(float x:v.normal)need(std::isfinite(x),"Nonfinite rigid normal");
        for(float x:v.color)need(std::isfinite(x),"Nonfinite rigid color");
        for(float x:v.uv)need(std::isfinite(x),"Nonfinite rigid UV");
        for(float x:v.uv1)need(std::isfinite(x),"Nonfinite rigid UV1");
        // Tangent storage is opt-in decoded; zero when unconsumed. Finite
        // check preserves existing callers and qualifies future normalmap use.
        for(float x:v.tangent)need(std::isfinite(x),"Nonfinite rigid tangent");
    }
    auto result=std::make_unique<NativeRigidMesh::State>();result->owner=this;result->thread=owner;result->device=device;
    result->vertexCount=uint32_t(vertices.size());result->indexValues.assign(indices.begin(),indices.end());
    result->vertices=buffer(device.Get(),vertices.data(),UINT(vertices.size_bytes()),D3D11_BIND_VERTEX_BUFFER,D3D11_USAGE_IMMUTABLE);
    result->indices=buffer(device.Get(),indices.data(),UINT(indices.size_bytes()),D3D11_BIND_INDEX_BUFFER,D3D11_USAGE_IMMUTABLE);
    const D3D11_INPUT_ELEMENT_DESC elements[]={
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",3,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",4,DXGI_FORMAT_R32G32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0}};
    check(device->CreateInputLayout(elements,UINT(std::size(elements)),kVSRigidDualTextured,sizeof(kVSRigidDualTextured),&result->layout),"dual rigid input layout creation");
    // The particle VS consumes three registers; use its own signature so
    // color and UV are not shifted by the lit shader's normal input.
    const D3D11_INPUT_ELEMENT_DESC vfxElements[]={elements[0],elements[2],elements[3]};
    check(device->CreateInputLayout(vfxElements,UINT(std::size(vfxElements)),kVSVfxRigid,sizeof(kVSVfxRigid),&result->vfxLayout),"VFX rigid input layout creation");
    // Tangent-consuming declaration. Appends TEXCOORD5 at byte56; the
    // first 56 bytes and the default 5-element layout above are unchanged, so
    // every existing consumer keeps its exact binding. Grounded in the
    // declaration row usage6/index0 type002A2187 and the rigid VS fetches
    // targeting r5. Bound only for shaders that explicitly consume tangent.
    const D3D11_INPUT_ELEMENT_DESC tangentElements[]={
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",3,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",4,DXGI_FORMAT_R32G32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",5,DXGI_FORMAT_R32G32B32_FLOAT,0,56,D3D11_INPUT_PER_VERTEX_DATA,0}};
    check(device->CreateInputLayout(tangentElements,UINT(std::size(tangentElements)),kVSRigidNormalTangent,sizeof(kVSRigidNormalTangent),&result->tangentLayout),"normalmap tangent input layout creation");
    // D3D input layouts encode the VS input register mapping. Chocolate has
    // the same vertex fields but a different signature from NormalTangent.
    check(device->CreateInputLayout(tangentElements,UINT(std::size(tangentElements)),kVSChocolate,sizeof(kVSChocolate),&result->chocolateLayout),"chocolate tangent input layout creation");
    check(device->CreateInputLayout(tangentElements,UINT(std::size(tangentElements)),kVSChocolateOpaque,sizeof(kVSChocolateOpaque),&result->chocolateOpaqueLayout),"chocolate opaque input layout creation");
    check(device->CreatePixelShader(kPSRigidDraw,sizeof(kPSRigidDraw),nullptr,&result->pixel),"depth/color adapter creation");
    check(device->CreatePixelShader(kPSVfxRigidDraw,sizeof(kPSVfxRigidDraw),nullptr,&result->vfxPixel),"VFX rigid depth/color adapter creation");
    check(device->CreatePixelShader(kPSRigidTexturedDraw,sizeof(kPSRigidTexturedDraw),nullptr,&result->texturedPixel),"textured depth/color adapter creation");
    check(device->CreatePixelShader(kPSRigidDualTexturedDraw,sizeof(kPSRigidDualTexturedDraw),nullptr,&result->dualPixel),"dualtextured depth/color adapter creation");
    check(device->CreatePixelShader(kPSRigidDualTexturedUVDraw,sizeof(kPSRigidDualTexturedUVDraw),nullptr,&result->dualUVPixel),"dualtextured UV depth/color adapter");
    check(device->CreatePixelShader(kPSRigidDualTexturedUVAlphaDraw,sizeof(kPSRigidDualTexturedUVAlphaDraw),nullptr,&result->dualUVAlphaPixel),"dualtextured UV alpha depth/color adapter");
    check(device->CreatePixelShader(kPSRigidUVDraw,sizeof(kPSRigidUVDraw),nullptr,&result->singleUVPixel),"single UV depth/color adapter");
    check(device->CreatePixelShader(kPSRigidUVAlphaDraw,sizeof(kPSRigidUVAlphaDraw),nullptr,&result->singleUVAlphaPixel),"single UV alpha depth/color adapter");
    check(device->CreatePixelShader(kPSFlipbookDraw,sizeof(kPSFlipbookDraw),nullptr,&result->flipbookPixel),"flipbook depth/color adapter");
    check(device->CreatePixelShader(kPSFlipbookAlphaDraw,sizeof(kPSFlipbookAlphaDraw),nullptr,&result->flipbookAlphaPixel),"flipbook alpha depth/color adapter");
    check(device->CreatePixelShader(kPSRigidGlossDraw,sizeof(kPSRigidGlossDraw),nullptr,&result->glossPixel),"gloss depth/color adapter creation");
    check(device->CreatePixelShader(kPSRigidMultitoneDraw,sizeof(kPSRigidMultitoneDraw),nullptr,&result->multitonePixel),"multitone depth/color adapter creation");
    check(device->CreatePixelShader(kPSRigidNormalmapDraw,sizeof(kPSRigidNormalmapDraw),nullptr,&result->normalmapPixel),"normalmap depth/color adapter creation");
    check(device->CreatePixelShader(kPSRigidGlossAlphaDraw,sizeof(kPSRigidGlossAlphaDraw),nullptr,&result->glossAlphaPixel),"gloss alpha depth/color adapter");
    check(device->CreatePixelShader(kPSRigidMultitoneAlphaDraw,sizeof(kPSRigidMultitoneAlphaDraw),nullptr,&result->multitoneAlphaPixel),"multitone alpha depth/color adapter");
    check(device->CreatePixelShader(kPSRigidNormalmapAlphaDraw,sizeof(kPSRigidNormalmapAlphaDraw),nullptr,&result->normalmapAlphaPixel),"normalmap alpha depth/color adapter");
    check(device->CreatePixelShader(kPS168F8Draw,sizeof(kPS168F8Draw),nullptr,&result->a168Pixel),"168F8 depth/color adapter creation");
    check(device->CreatePixelShader(kPSChocolateDraw,sizeof(kPSChocolateDraw),nullptr,&result->chocPixel),"chocolate depth/color adapter creation");
    check(device->CreatePixelShader(kPSChocolateOpaqueDraw,sizeof(kPSChocolateOpaqueDraw),nullptr,&result->chocOpaquePixel),"chocolate opaque depth/color adapter");
    check(device->CreatePixelShader(kPSProjtexDraw,sizeof(kPSProjtexDraw),nullptr,&result->projtexPixel),"projtex depth/color adapter");
    check(device->CreatePixelShader(kPSProjtexAlphaDraw,sizeof(kPSProjtexAlphaDraw),nullptr,&result->projtexAlphaPixel),"projtex alpha depth/color adapter");
    check(device->CreatePixelShader(kPSRigidAlphaDraw,sizeof(kPSRigidAlphaDraw),nullptr,&result->rigidAlphaPixel),"rigid alpha depth/color adapter");
    result->validate(this,device.Get());requireOwner();
    auto shared = std::shared_ptr<NativeRigidMesh>(new NativeRigidMesh(std::move(result)));
    // Snapshot exact bytes so later caller mutation cannot corrupt the cache.
    // Eviction drops only the cache's strong ref; live caller handles stay valid.
    slot.cache.insert(vertexBytes, indexBytes, contentKey, shared);
    return shared;
}
void NativeBackend::bindRigidMeshVertices(const std::shared_ptr<NativeRigidMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing rigid mesh vertex owner");mesh->state->validate(this,device.Get());
    flushIm2D();auto* value=mesh->state->vertices.Get();const UINT stride=sizeof(RigidVertex),offset=0;
    context->IASetVertexBuffers(0,1,&value,&stride,&offset);
    ComPtr<ID3D11Buffer> actual;UINT s{},o{};context->IAGetVertexBuffers(0,1,&actual,&s,&o);
    need(actual.Get()==value&&s==stride&&!o,"D3D11 did not retain rigid mesh vertices");requireOwner();
}
void NativeBackend::bindRigidMeshDeclaration(const std::shared_ptr<NativeRigidMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing rigid mesh declaration owner");mesh->state->validate(this,device.Get());
    flushIm2D();context->IASetInputLayout(mesh->state->layout.Get());ComPtr<ID3D11InputLayout> actual;context->IAGetInputLayout(&actual);
    need(actual.Get()==mesh->state->layout.Get(),"D3D11 did not retain rigid mesh declaration");requireOwner();
}
void NativeBackend::bindRigidNormalMeshDeclaration(const std::shared_ptr<NativeRigidMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing rigid mesh declaration owner");mesh->state->validate(this,device.Get());
    flushIm2D();context->IASetInputLayout(mesh->state->tangentLayout.Get());ComPtr<ID3D11InputLayout> actual;context->IAGetInputLayout(&actual);
    need(actual.Get()==mesh->state->tangentLayout.Get(),"D3D11 did not retain rigid normalmap mesh declaration");requireOwner();
}
void NativeBackend::bindChocolateMeshDeclaration(const std::shared_ptr<NativeRigidMesh>& mesh,bool alpha) {
    validateSubmissionContext();need(bool(mesh),"Missing chocolate mesh declaration owner");mesh->state->validate(this,device.Get());
    flushIm2D();context->IASetInputLayout((alpha?mesh->state->chocolateLayout.Get():mesh->state->chocolateOpaqueLayout.Get()));ComPtr<ID3D11InputLayout> actual;context->IAGetInputLayout(&actual);
    need(actual.Get()==(alpha?mesh->state->chocolateLayout.Get():mesh->state->chocolateOpaqueLayout.Get()),"D3D11 did not retain chocolate mesh declaration");requireOwner();
}
void NativeBackend::bindVfxRigidMeshDeclaration(const std::shared_ptr<NativeRigidMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing VFX rigid declaration owner");mesh->state->validate(this,device.Get());
    flushIm2D();context->IASetInputLayout(mesh->state->vfxLayout.Get());ComPtr<ID3D11InputLayout> actual;context->IAGetInputLayout(&actual);
    need(actual.Get()==mesh->state->vfxLayout.Get(),"D3D11 did not retain VFX rigid declaration");requireOwner();
}
void NativeBackend::bindRigidMeshIndices(const std::shared_ptr<NativeRigidMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing rigid mesh index owner");mesh->state->validate(this,device.Get());
    flushIm2D();context->IASetIndexBuffer(mesh->state->indices.Get(),DXGI_FORMAT_R16_UINT,0);
    ComPtr<ID3D11Buffer> actual;DXGI_FORMAT format{};UINT offset{};context->IAGetIndexBuffer(&actual,&format,&offset);
    need(actual.Get()==mesh->state->indices.Get()&&format==DXGI_FORMAT_R16_UINT&&!offset,"D3D11 did not retain rigid mesh indices");requireOwner();
}
void NativeBackend::bindRigidShadowDepth(uint32_t stage,const std::shared_ptr<DepthTarget>& shadow) {
    validateSubmissionContext();validateDepthCopyTarget(shadow);
    need(stage<2&&shadow->width==1024&&shadow->height==1024,"Native rigid shadow binding requires stage0/1 and a 1024-square depth owner");
    const auto validate=[&] {
        ComPtr<ID3D11DepthStencilView> output;context->OMGetRenderTargets(0,nullptr,&output);
        if(output){ComPtr<ID3D11Resource> resource;output->GetResource(&resource);
            need(resource.Get()!=shadow->texture.Get(),"Native rigid sampled shadow aliases its actual depth output");}
    };
    validate();flushIm2D();validate();auto* view=shadow->depthView.Get();context->PSSetShaderResources(stage,1,&view);
    ComPtr<ID3D11ShaderResourceView> actual;context->PSGetShaderResources(stage,1,&actual);
    need(actual.Get()==view,"D3D11 did not retain rigid shadow binding");requireOwner();
}
void NativeBackend::drawRigidMesh(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,
    const std::shared_ptr<NativeRigidMesh>& mesh,const CompiledMaterial& vertex,const CompiledMaterial& pixel,
    const std::shared_ptr<NativeRigidCommit>& commit,const RigidMeshDraw& d) {
    validateSubmissionContext();need(bool(mesh),"Missing native rigid mesh draw owner");auto& m=*mesh->state;m.validate(this,device.Get());
    need(d.primitiveType==6&&d.primitiveReset==1&&d.primitiveResetIndex==0xFFFF,
         "Native rigid requires primitive6 R16 strip and restartFFFF");
    need(validR16DrawRange(m.indexValues,m.vertexCount,d.startIndex,d.indexCount,d.baseVertex),
         "Native rigid effective index range exceeds its owned vertices or indices");
    need(d.depthPolicy==ShadowMeshDepthPolicy::Reference20e4Rne,"Native rigid requires explicit Reference20e4Rne depth policy");
    const bool vfx=pixel.originalAddress()==0x8205C100;
    const bool rigidAlpha=pixel.originalAddress()==0x8200E1BC;
    const bool a168=pixel.originalAddress()==0x82017E4C;
    const bool dualAlpha=pixel.originalAddress()==0x8202C3BC;
    const bool chocAlpha=pixel.originalAddress()==0x8205EED4;
    const bool chocOpaque=pixel.originalAddress()==0x8205E5E0,choc=chocAlpha||chocOpaque;
    const UINT materialCount=chocAlpha?3u:chocOpaque?2u:0u,materialStage=chocAlpha?0u:2u;
    const bool projtex=pixel.originalAddress()==0x82052610,projtexAlpha=pixel.originalAddress()==0x82052DF4;
    const bool uvOpaque=pixel.originalAddress()==0x820477A8;
    const bool uvAlpha=pixel.originalAddress()==0x82047F44;
    const bool singleUvOpaque=pixel.originalAddress()==0x82044068;
    const bool singleUvAlpha=pixel.originalAddress()==0x82044850;
    const bool flipbookOpaque=pixel.originalAddress()==0x8203A24C;
    const bool flipbookAlpha=pixel.originalAddress()==0x8203A644;
    const bool flipbook=flipbookOpaque||flipbookAlpha;
    const bool glossAlpha=pixel.originalAddress()==0x8201B0BC;
    const bool multitoneAlpha=pixel.originalAddress()==0x820560B8;
    const bool normalmapAlpha=pixel.originalAddress()==0x82059880;
    const bool familyAlpha=glossAlpha||multitoneAlpha||normalmapAlpha;
    const bool uv=uvOpaque||uvAlpha;
    const UINT shadowCount=uvOpaque?1u:2u;
    const UINT uvBaseStage=uvAlpha?0u:1u;
    const bool noShadowSamples=vfx||rigidAlpha||a168||dualAlpha||chocAlpha||projtexAlpha||uvAlpha||familyAlpha||singleUvAlpha||flipbook;
    const UINT baseStage=(vfx||rigidAlpha||a168||dualAlpha||familyAlpha||singleUvAlpha||flipbook||projtexAlpha)?0u:2u;
    need(noShadowSamples?(d.shadowSamplePolicy==RigidShadowSamplePolicy::NotUsed&&!d.shadows[0]&&!d.shadows[1]):
         d.shadowSamplePolicy==RigidShadowSamplePolicy::ReferenceD24FS8DepthRRRR,"Native rigid depth sampling policy is unqualified");
    need(d.depthEnable<=1&&d.depthWrite<=1&&d.depthCompare<=7,"Native rigid depth state is not canonical");
    need((d.cull==0||d.cull==2||d.cull==6)&&d.fill==0&&d.colorMask<=15,"Native rigid cull/fill/color mask is unqualified");
    // 827400F8 shares the original alpha cleanup with skin/sky: it clears
    // expanded blending only, so the following opaque draw inherits factors.
    if(!((!d.blendEnable&&d.blendWord==0x00010001&&d.expandedBlend<=1)||
         (d.blendEnable==1&&d.blendWord==0x07060706&&d.expandedBlend<=1))) {
        char message[180];std::snprintf(message,sizeof(message),"Native rigid blend is unqualified: pixel=%08X enable=%u word=%08X expanded=%u",
            pixel.originalAddress(),d.blendEnable,d.blendWord,d.expandedBlend);throw Error(message);
    }
    need(!d.stencilEnable&&!d.alphaTest&&!d.alphaToMask&&!d.clipPlaneEnable&&d.viewportEnable==1,"Native rigid stencil/alpha/clipping state is unqualified");
    need(d.scissorEnable<=1&&d.halfPixelOffset==1&&d.multisampleAntialias==1&&
         (d.multisampleMask==0xFFFFFFFF||d.multisampleMask==0x0000FFFF),"Native rigid requires halfpixel1 and single-sample full-mask state");
    const float bias=std::bit_cast<float>(d.depthBiasBits),slope=std::bit_cast<float>(d.slopeBiasBits);
    need(std::isfinite(bias)&&std::isfinite(slope)&&std::isfinite(slope*16),"Native rigid bias is nonfinite or overflows");
    validateFrontTarget(target);validateDepthCopyTarget(depth);
    need(depth->width==target->width&&depth->height==target->height,"Native rigid attachment dimensions differ");
    const bool reverse=d.viewport[4]==0x3F800000&&d.viewport[5]==0;
    need(d.viewport[0]==0&&d.viewport[1]==0&&d.viewport[2]==target->width&&d.viewport[3]==target->height&&
         (reverse||(d.viewport[4]==0&&d.viewport[5]==0x3F800000)),"Native rigid requires a full-target viewport with depth0..1 or1..0");
    if(d.scissorEnable) {
        need(d.scissor[0]<d.scissor[2]&&d.scissor[1]<d.scissor[3]&&d.scissor[2]<=target->width&&d.scissor[3]<=target->height,
             "Native rigid scissor exceeds the explicit target");
        const auto retained=scissor();need(retained&&*retained==d.scissor,"Native rigid scissor differs from the original retained rectangle");
    }
    if(uvOpaque) {
        need(!d.shadows[1],"UV rigid has only one character shadow owner");
        validateDepthCopyTarget(d.shadows[0]);samplerProfile(d.samplers[0]);
        need(d.shadows[0]->width==1024&&d.shadows[0]->height==1024&&d.shadows[0]->texture.Get()!=depth->texture.Get(),
             "UV rigid character shadow extent/output alias differs");
    } else if(!noShadowSamples){validateRigidShadowDepths(d.shadows,depth);for(const auto& sampler:d.samplers)samplerProfile(sampler);}
    const bool dual=pixel.originalAddress()==0x8202BB44;
    const bool gloss=pixel.originalAddress()==0x8201A6EC;
    const bool multitone=pixel.originalAddress()==0x82055710;
    const bool normalmap=pixel.originalAddress()==0x82058CBC;
    const bool textured=projtex||projtexAlpha||flipbook||singleUvOpaque||singleUvAlpha||familyAlpha||vfx||rigidAlpha||a168||dualAlpha||dual||gloss||multitone||normalmap||pixel.originalAddress()==0x82017658;
    if(normalmap)need(bool(d.noiseTexture),"Rigid normal texture differs from the selected shader variant");
    else need((multitone||projtex)==bool(d.noiseTexture),"Rigid noise texture differs from the selected shader variant");
    if(multitone||normalmap||projtex){validateTexture(d.noiseTexture);if(projtex)projectionSamplerProfile(d.noiseSampler);else baseSamplerProfile(d.noiseSampler);}
    need(textured==bool(d.baseTexture),"Rigid base texture differs from the selected shader variant");
    if(textured){validateTexture(d.baseTexture);baseSamplerProfile(d.baseSampler);}
    for(uint32_t stage=0;stage<3;++stage) {
        need((stage<materialCount)==bool(d.materialTextures[stage]),"Chocolate material texture differs from the selected shader variant");
        if(stage<materialCount){validateTexture(d.materialTextures[stage]);baseSamplerProfile(d.materialSamplers[stage]);}
    }
    for(uint32_t stage=0;stage<2;++stage) {
        need(uv==bool(d.uvTextures[stage]),"UV rigid material texture differs from the selected variant");
        if(uv){validateTexture(d.uvTextures[stage]);baseSamplerProfile(d.uvSamplers[stage]);}
    }
    const auto bindings=[&] {
        requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);requireRigidShaders(vertex,pixel);requireRigidCommit(commit);
        if(normalmap||choc)m.requireTangentBindings(context.Get(),choc,chocAlpha);else m.requireBindings(context.Get(),vfx);requireRigidDrawStages(context.Get());
        if(uv)for(uint32_t stage=0;stage<2;++stage)requireEngineTexture(uvBaseStage+stage,d.uvTextures[stage]);
        else if(choc)for(uint32_t stage=0;stage<materialCount;++stage)requireEngineTexture(materialStage+stage,d.materialTextures[stage]);
        else if(textured)requireEngineTexture(baseStage,d.baseTexture);
        if(multitone||normalmap||projtex)requireEngineTexture(3,d.noiseTexture);
        if(!noShadowSamples)for(UINT i=0;i<shadowCount;++i){ComPtr<ID3D11ShaderResourceView> actual;context->PSGetShaderResources(i,1,&actual);
            need(actual.Get()==d.shadows[i]->depthView.Get(),"Native rigid sampled shadow binding differs from its owner");}
    };
    bindings();requireNoOutputUavs();
    // Reuse immutable scene states instead of per-draw driver allocations.
    // All validation above precedes lookup; misses create once and publish.
    auto constants=sceneDepthConstantBuffer(uint32_t(reverse),d.depthBiasBits,d.slopeBiasBits);
    auto depthState=sceneDepthState(d.depthEnable,d.depthWrite,d.depthCompare);
    auto blend=sceneBlendState(d.blendEnable,d.blendWord,d.colorMask);
    auto raster=sceneRasterState(d.cull,d.scissorEnable);
    std::array<ComPtr<ID3D11SamplerState>,2> samplers;
    std::array<ComPtr<ID3D11SamplerState>,3> materialSamplers;
    std::array<ComPtr<ID3D11SamplerState>,2> uvSamplers;
    ComPtr<ID3D11SamplerState> baseSampler,noiseSampler;
    if(!noShadowSamples)for(UINT i=0;i<shadowCount;++i)samplers[i]=sceneSamplerState(d.samplers[i]);
    if(uv)for(UINT i=0;i<2;++i)uvSamplers[i]=sceneSamplerState(materialSampling(d.uvSamplers[i]));
    if(choc)for(UINT i=0;i<materialCount;++i)materialSamplers[i]=sceneSamplerState(materialSampling(d.materialSamplers[i]));
    if(textured)baseSampler=sceneSamplerState(materialSampling(d.baseSampler));
    if(multitone||normalmap||projtex)noiseSampler=sceneSamplerState(d.noiseSampler);
    // All qualification and allocations precede the flush and transient binds.
    flushIm2D();bindings();
    {
        ScopedRigidDrawBindings restore(context.Get());
        const auto viewport=renderViewport(target,{0,0,float(target->width),float(target->height),0,1});context->RSSetViewports(1,&viewport);
        const D3D11_RECT scissor=renderScissor(target,d.scissorEnable?D3D11_RECT{LONG(d.scissor[0]),LONG(d.scissor[1]),LONG(d.scissor[2]),LONG(d.scissor[3])}:
            D3D11_RECT{0,0,LONG(target->width),LONG(target->height)});
        context->RSSetScissorRects(1,&scissor);context->RSSetState(raster.Get());
        context->OMSetBlendState(blend.Get(),nullptr,d.multisampleMask);context->OMSetDepthStencilState(depthState.Get(),0);
        auto* cb=constants.Get();context->PSSetConstantBuffers(1,1,&cb);context->PSSetShader(flipbookOpaque?m.flipbookPixel.Get():flipbookAlpha?m.flipbookAlphaPixel.Get():singleUvOpaque?m.singleUVPixel.Get():singleUvAlpha?m.singleUVAlphaPixel.Get():uvOpaque?m.dualUVPixel.Get():uvAlpha?m.dualUVAlphaPixel.Get():vfx?m.vfxPixel.Get():rigidAlpha?m.rigidAlphaPixel.Get():chocOpaque?m.chocOpaquePixel.Get():chocAlpha?m.chocPixel.Get():projtex?m.projtexPixel.Get():projtexAlpha?m.projtexAlphaPixel.Get():(a168||dualAlpha)?m.a168Pixel.Get():glossAlpha?m.glossAlphaPixel.Get():multitoneAlpha?m.multitoneAlphaPixel.Get():normalmapAlpha?m.normalmapAlphaPixel.Get():normalmap?m.normalmapPixel.Get():multitone?m.multitonePixel.Get():gloss?m.glossPixel.Get():dual?m.dualPixel.Get():textured?m.texturedPixel.Get():m.pixel.Get(),nullptr,0);
        if(multitone||normalmap||projtex){auto* sampler=noiseSampler.Get();context->PSSetSamplers(3,1,&sampler);}
        if(!noShadowSamples)for(UINT i=0;i<shadowCount;++i){auto* sampler=samplers[i].Get();context->PSSetSamplers(i,1,&sampler);}
        if(uv){ID3D11SamplerState* states[]={uvSamplers[0].Get(),uvSamplers[1].Get()};context->PSSetSamplers(uvBaseStage,2,states);}
        else if(choc){ID3D11SamplerState* states[]={materialSamplers[0].Get(),materialSamplers[1].Get(),materialSamplers[2].Get()};context->PSSetSamplers(materialStage,materialCount,states);}
        else if(textured){auto* sampler=baseSampler.Get();context->PSSetSamplers(baseStage,1,&sampler);}
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        forEachOriginalR16StripChunk(d.startIndex,d.indexCount,[&](uint32_t n,uint32_t first){context->DrawIndexed(n,first,d.baseVertex);});
        ++rigidMeshDraws;
    }
    bindings();requireOwner();
}
void NativeBackend::bindRigidMeshVertices(const std::shared_ptr<NativeRecordingPayload>& payload,const std::shared_ptr<NativeRigidMesh>& mesh) {
    auto* deferred=checkedRecordingPayloadContext(payload);need(bool(mesh),"Missing rigid mesh vertex owner");mesh->state->validate(this,device.Get());
    auto* value=mesh->state->vertices.Get();const UINT stride=sizeof(RigidVertex),offset=0;deferred->IASetVertexBuffers(0,1,&value,&stride,&offset);
}
void NativeBackend::bindRigidMeshDeclaration(const std::shared_ptr<NativeRecordingPayload>& payload,const std::shared_ptr<NativeRigidMesh>& mesh) {
    auto* deferred=checkedRecordingPayloadContext(payload);need(bool(mesh),"Missing rigid mesh declaration owner");mesh->state->validate(this,device.Get());
    deferred->IASetInputLayout(mesh->state->layout.Get());
}
void NativeBackend::bindRigidNormalMeshDeclaration(const std::shared_ptr<NativeRecordingPayload>& payload,const std::shared_ptr<NativeRigidMesh>& mesh) {
    auto* deferred=checkedRecordingPayloadContext(payload);need(bool(mesh),"Missing rigid mesh declaration owner");mesh->state->validate(this,device.Get());
    deferred->IASetInputLayout(mesh->state->tangentLayout.Get());
}
void NativeBackend::bindChocolateMeshDeclaration(const std::shared_ptr<NativeRecordingPayload>& payload,const std::shared_ptr<NativeRigidMesh>& mesh,bool alpha) {
    auto* deferred=checkedRecordingPayloadContext(payload);need(bool(mesh),"Missing chocolate mesh declaration owner");mesh->state->validate(this,device.Get());
    deferred->IASetInputLayout((alpha?mesh->state->chocolateLayout.Get():mesh->state->chocolateOpaqueLayout.Get()));
}
void NativeBackend::bindVfxRigidMeshDeclaration(const std::shared_ptr<NativeRecordingPayload>& payload,const std::shared_ptr<NativeRigidMesh>& mesh) {
    auto* deferred=checkedRecordingPayloadContext(payload);need(bool(mesh),"Missing VFX rigid declaration owner");mesh->state->validate(this,device.Get());
    deferred->IASetInputLayout(mesh->state->vfxLayout.Get());
}
void NativeBackend::bindRigidMeshIndices(const std::shared_ptr<NativeRecordingPayload>& payload,const std::shared_ptr<NativeRigidMesh>& mesh) {
    auto* deferred=checkedRecordingPayloadContext(payload);need(bool(mesh),"Missing rigid mesh index owner");mesh->state->validate(this,device.Get());
    deferred->IASetIndexBuffer(mesh->state->indices.Get(),DXGI_FORMAT_R16_UINT,0);
}
void NativeBackend::bindRigidShadowDepth(const std::shared_ptr<NativeRecordingPayload>& payload,uint32_t stage,const std::shared_ptr<DepthTarget>& shadow) {
    auto* deferred=checkedRecordingPayloadContext(payload);validateDepthCopyTarget(shadow);
    need(stage<2&&shadow->width==1024&&shadow->height==1024,"Native rigid shadow binding requires stage0/1 and a 1024-square depth owner");
    auto* view=shadow->depthView.Get();deferred->PSSetShaderResources(stage,1,&view);
}
void NativeBackend::validateRigidShadowDepths(const std::array<std::shared_ptr<DepthTarget>,2>& shadows,const std::shared_ptr<DepthTarget>& output) const {
    validateSubmissionContext();validateDepthCopyTarget(output);
    for(const auto& shadow:shadows) {
        validateDepthCopyTarget(shadow);
        need(shadow->width==1024&&shadow->height==1024,"Native rigid shadow sampler requires 1024-square copied depth");
        need(shadow->texture.Get()!=output->texture.Get(),"Native rigid sampled shadow aliases its recorded depth output");
    }
    need(shadows[0]->texture.Get()!=shadows[1]->texture.Get(),"Native rigid shadow banks require distinct depth owners");
}
std::vector<RigidVertex> NativeBackend::readbackRigidMeshVertices(const std::shared_ptr<NativeRigidMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing rigid mesh readback owner");mesh->state->validate(this,device.Get());
    return readBuffer<RigidVertex>(device.Get(),context.Get(),mesh->state->vertices.Get());
}
std::vector<uint16_t> NativeBackend::readbackRigidMeshIndices(const std::shared_ptr<NativeRigidMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing rigid index readback owner");mesh->state->validate(this,device.Get());
    return readBuffer<uint16_t>(device.Get(),context.Get(),mesh->state->indices.Get());
}
std::shared_ptr<NativeRigidReplayConstants> NativeBackend::createRigidReplayConstants(const RigidVertexConstants& vertices,const RigidPixelConstants& pixels) {
    validateSubmissionContext();finite(vertices);finite(pixels);auto value=std::make_unique<NativeRigidReplayConstants::State>();
    value->owner=this;value->thread=owner;value->device=device;value->vertex=vertices;value->pixel=pixels;
    return std::shared_ptr<NativeRigidReplayConstants>(new NativeRigidReplayConstants(std::move(value)));
}
void NativeBackend::updateRigidReplayConstants(const std::shared_ptr<NativeRigidReplayConstants>& live,const RigidVertexConstants& vertices,const RigidPixelConstants& pixels) {
    validateSubmissionContext();need(bool(live),"Missing rigid replay constants");live->state->validate(this,device.Get());finite(vertices);finite(pixels);
    live->state->vertex=vertices;live->state->pixel=pixels;
}
void NativeBackend::releaseRigidReplayConstants(const std::shared_ptr<NativeRigidReplayConstants>& live) {
    validateSubmissionContext();need(bool(live),"Missing rigid replay constants");live->state->validate(this,device.Get());live->state->active=false;
}

void NativeBackend::recordRigidMesh(const std::shared_ptr<NativeRecordingPayload>& payload,
    const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,
    const std::shared_ptr<NativeRigidMesh>& mesh,const CompiledMaterial& vertex,const CompiledMaterial& pixel,
    const RigidVertexConstants& materialVS,const RigidPixelConstants& materialPS,
    const std::shared_ptr<NativeRigidReplayConstants>& live,const RigidMeshDraw& d) {
    (void)checkedRecordingPayloadContext(payload);need(bool(mesh)&&bool(live),"Missing native rigid mesh/replay owner");
    auto& m=*mesh->state;m.validate(this,device.Get());live->state->validate(this,device.Get());finite(materialVS);finite(materialPS);
    const auto receipt=recordingPayloadReceipt(payload);
    need(d.primitiveType==6&&d.primitiveReset==1&&d.primitiveResetIndex==0xFFFF,
         "Native rigid requires primitive6 R16 strip and restartFFFF");
    need(validR16DrawRange(m.indexValues,m.vertexCount,d.startIndex,d.indexCount,d.baseVertex),
         "Native rigid effective index range exceeds its owned vertices or indices");
    need(d.depthPolicy==ShadowMeshDepthPolicy::Reference20e4Rne,"Native rigid requires explicit Reference20e4Rne depth policy");
    const bool vfx=pixel.originalAddress()==0x8205C100;
    const bool rigidAlpha=pixel.originalAddress()==0x8200E1BC;
    const bool a168=pixel.originalAddress()==0x82017E4C;
    const bool dualAlpha=pixel.originalAddress()==0x8202C3BC;
    const bool chocAlpha=pixel.originalAddress()==0x8205EED4;
    const bool chocOpaque=pixel.originalAddress()==0x8205E5E0,choc=chocAlpha||chocOpaque;
    const UINT materialCount=chocAlpha?3u:chocOpaque?2u:0u,materialStage=chocAlpha?0u:2u;
    const bool projtex=pixel.originalAddress()==0x82052610,projtexAlpha=pixel.originalAddress()==0x82052DF4;
    const bool uvOpaque=pixel.originalAddress()==0x820477A8;
    const bool uvAlpha=pixel.originalAddress()==0x82047F44;
    const bool singleUvOpaque=pixel.originalAddress()==0x82044068;
    const bool singleUvAlpha=pixel.originalAddress()==0x82044850;
    const bool flipbookOpaque=pixel.originalAddress()==0x8203A24C;
    const bool flipbookAlpha=pixel.originalAddress()==0x8203A644;
    const bool flipbook=flipbookOpaque||flipbookAlpha;
    const bool glossAlpha=pixel.originalAddress()==0x8201B0BC;
    const bool multitoneAlpha=pixel.originalAddress()==0x820560B8;
    const bool normalmapAlpha=pixel.originalAddress()==0x82059880;
    const bool familyAlpha=glossAlpha||multitoneAlpha||normalmapAlpha;
    const bool uv=uvOpaque||uvAlpha;
    const UINT shadowCount=uvOpaque?1u:2u;
    const bool noShadowSamples=vfx||rigidAlpha||a168||dualAlpha||chocAlpha||projtexAlpha||uvAlpha||familyAlpha||singleUvAlpha||flipbook;
    const UINT baseStage=(vfx||rigidAlpha||a168||dualAlpha||familyAlpha||singleUvAlpha||flipbook||projtexAlpha)?0u:2u;
    need(noShadowSamples?(d.shadowSamplePolicy==RigidShadowSamplePolicy::NotUsed&&!d.shadows[0]&&!d.shadows[1]):
         d.shadowSamplePolicy==RigidShadowSamplePolicy::ReferenceD24FS8DepthRRRR,"Native rigid depth sampling policy is unqualified");
    need(d.depthEnable<=1&&d.depthWrite<=1&&d.depthCompare<=7,"Native rigid depth state is not canonical");
    need((d.cull==0||d.cull==2||d.cull==6)&&d.fill==0&&d.colorMask<=15,"Native rigid cull/fill/color mask is unqualified");
    need((!d.blendEnable&&d.blendWord==0x00010001&&d.expandedBlend<=1)||
         (d.blendEnable==1&&d.blendWord==0x07060706&&d.expandedBlend<=1),"Native rigid requires explicit replacement or inherited source-alpha blend state");
    need(!d.stencilEnable&&!d.alphaTest&&!d.alphaToMask&&!d.clipPlaneEnable&&d.viewportEnable==1,"Native rigid stencil/alpha/clipping state is unqualified");
    // Original forced application state uses FFFF. Both qualified masks cover
    // sample zero of these owned single-sample targets; retain the raw value.
    need(d.scissorEnable<=1&&d.halfPixelOffset==1&&d.multisampleAntialias==1&&
         (d.multisampleMask==0xFFFFFFFF||d.multisampleMask==0x0000FFFF),
         "Native rigid requires halfpixel1 and single-sample full-mask state");
    const float bias=std::bit_cast<float>(d.depthBiasBits),slope=std::bit_cast<float>(d.slopeBiasBits);
    need(std::isfinite(bias)&&std::isfinite(slope)&&std::isfinite(slope*16),"Native rigid bias is nonfinite or overflows");
    validateFrontTarget(target);validateDepthCopyTarget(depth);
    need(depth->width==target->width&&depth->height==target->height,"Native rigid attachment dimensions differ");
    const bool reverse=d.viewport[4]==0x3F800000&&d.viewport[5]==0;
    need(d.viewport[0]==0&&d.viewport[1]==0&&d.viewport[2]==target->width&&d.viewport[3]==target->height&&
         (reverse||(d.viewport[4]==0&&d.viewport[5]==0x3F800000)),"Native rigid requires a full-target viewport with depth0..1 or1..0");
    if(d.scissorEnable)need(d.scissor[0]<d.scissor[2]&&d.scissor[1]<d.scissor[3]&&d.scissor[2]<=target->width&&d.scissor[3]<=target->height,
                           "Native rigid scissor exceeds the explicit target");
    if(uvOpaque)need(!d.shadows[1],"UV recorded rigid has only one character shadow owner");
    if(!noShadowSamples)for(size_t slot=0;slot<shadowCount;++slot) {
        validateDepthCopyTarget(d.shadows[slot]);samplerProfile(d.samplers[slot]);
        need(d.shadows[slot]->width==1024&&d.shadows[slot]->height==1024,"Native rigid shadow sampler requires 1024-square copied depth");
        need(d.shadows[slot]->texture.Get()!=depth->texture.Get(),"Native rigid sampled shadow aliases its recorded depth output");
    }
    if(!noShadowSamples&&!uvOpaque)need(d.shadows[0]->texture.Get()!=d.shadows[1]->texture.Get(),"Native rigid shadow banks require distinct depth owners");
    const bool dual=pixel.originalAddress()==0x8202BB44;
    const bool gloss=pixel.originalAddress()==0x8201A6EC;
    const bool multitone=pixel.originalAddress()==0x82055710;
    const bool normalmap=pixel.originalAddress()==0x82058CBC;
    const bool textured=projtex||projtexAlpha||flipbook||singleUvOpaque||singleUvAlpha||familyAlpha||vfx||rigidAlpha||a168||dualAlpha||dual||gloss||multitone||normalmap||pixel.originalAddress()==0x82017658;
    if(normalmap)need(bool(d.noiseTexture),"Rigid recorded normal texture differs from the shader variant");
    else need((multitone||projtex)==bool(d.noiseTexture),"Rigid recorded noise texture differs from the shader variant");
    if(multitone||normalmap||projtex){validateTexture(d.noiseTexture);if(projtex)projectionSamplerProfile(d.noiseSampler);else baseSamplerProfile(d.noiseSampler);}
    need(textured==bool(d.baseTexture),"Rigid recorded base texture differs from the shader variant");
    if(textured){validateTexture(d.baseTexture);baseSamplerProfile(d.baseSampler);}
    for(uint32_t stage=0;stage<3;++stage) {
        need((stage<materialCount)==bool(d.materialTextures[stage]),"Chocolate recorded material texture differs from the shader variant");
        if(stage<materialCount){validateTexture(d.materialTextures[stage]);baseSamplerProfile(d.materialSamplers[stage]);}
    }
    for(uint32_t stage=0;stage<2;++stage) {
        need(uv==bool(d.uvTextures[stage]),"UV recorded rigid texture differs from the selected variant");
        if(uv){validateTexture(d.uvTextures[stage]);baseSamplerProfile(d.uvSamplers[stage]);}
    }
    auto draw=std::make_shared<RecordedDraw>();draw->uv=uv;draw->uvAlpha=uvAlpha;draw->owner=this;draw->thread=owner;draw->device=device;
    rigidShaderObjects(vertex,pixel,draw->vertex,draw->originalPixel); // Validates artifacts, never current bindings.
    draw->mesh=mesh;draw->live=live;draw->colorOwner=target;draw->depthOwner=depth;draw->shadowOwners=d.shadows;draw->chocolate=choc;draw->chocolateAlpha=chocAlpha;draw->materialCount=materialCount;draw->materialStage=materialStage;
    draw->vertices=m.vertices;draw->indices=m.indices;draw->layout=vfx?m.vfxLayout:chocOpaque?m.chocolateOpaqueLayout:chocAlpha?m.chocolateLayout:normalmap?m.tangentLayout:m.layout;draw->baseStage=baseStage;draw->drawPixel=flipbookOpaque?m.flipbookPixel:flipbookAlpha?m.flipbookAlphaPixel:singleUvOpaque?m.singleUVPixel:singleUvAlpha?m.singleUVAlphaPixel:uvOpaque?m.dualUVPixel:uvAlpha?m.dualUVAlphaPixel:vfx?m.vfxPixel:rigidAlpha?m.rigidAlphaPixel:chocOpaque?m.chocOpaquePixel:chocAlpha?m.chocPixel:projtex?m.projtexPixel:projtexAlpha?m.projtexAlphaPixel:(a168||dualAlpha)?m.a168Pixel:glossAlpha?m.glossAlphaPixel:multitoneAlpha?m.multitoneAlphaPixel:normalmapAlpha?m.normalmapAlphaPixel:normalmap?m.normalmapPixel:multitone?m.multitonePixel:gloss?m.glossPixel:dual?m.dualPixel:textured?m.texturedPixel:m.pixel;
    if(multitone||normalmap||projtex){draw->noiseOwner=d.noiseTexture;draw->noiseView=d.noiseTexture->view;
        check(device->CreateSamplerState(&d.noiseSampler,&draw->noiseSampler),"recorded noise/normal sampler creation");}
    if(textured){draw->baseOwner=d.baseTexture;draw->baseView=d.baseTexture->view;
        draw->baseSampler=sceneSamplerState(materialSampling(d.baseSampler));}
    if(choc)for(uint32_t stage=0;stage<materialCount;++stage) {
        draw->materialOwners[stage]=d.materialTextures[stage];draw->materialViews[stage]=d.materialTextures[stage]->view;
        draw->materialSamplers[stage]=sceneSamplerState(materialSampling(d.materialSamplers[stage]));
    }
    if(uv)for(uint32_t stage=0;stage<2;++stage) {
        draw->uvOwners[stage]=d.uvTextures[stage];draw->uvViews[stage]=d.uvTextures[stage]->view;
        draw->uvSamplers[stage]=sceneSamplerState(materialSampling(d.uvSamplers[stage]));
    }
    draw->color=target->view;draw->depth=depth->view;draw->indexCount=d.indexCount;draw->startIndex=d.startIndex;draw->baseVertex=d.baseVertex;
    draw->originalAddress=pixel.originalAddress();
    draw->sampleMask=d.multisampleMask;draw->inputMask=receipt.inputMask;
    draw->viewport=renderViewport(target,{0,0,float(target->width),float(target->height),0,1});
    draw->scissor=renderScissor(target,d.scissorEnable?D3D11_RECT{LONG(d.scissor[0]),LONG(d.scissor[1]),LONG(d.scissor[2]),LONG(d.scissor[3])}:
                                 D3D11_RECT{0,0,LONG(target->width),LONG(target->height)});
    draw->vertexConstants=buffer(device.Get(),materialVS.data(),sizeof(materialVS),D3D11_BIND_CONSTANT_BUFFER,D3D11_USAGE_DEFAULT);
    draw->pixelConstants=buffer(device.Get(),materialPS.data(),sizeof(materialPS),D3D11_BIND_CONSTANT_BUFFER,D3D11_USAGE_DEFAULT);
    const DepthConstants depthValues{uint32_t(reverse),d.depthBiasBits,d.slopeBiasBits,0};
    draw->depthConstants=buffer(device.Get(),&depthValues,sizeof(depthValues),D3D11_BIND_CONSTANT_BUFFER,D3D11_USAGE_IMMUTABLE);
    D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=d.depthEnable;dd.DepthWriteMask=d.depthWrite?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc=D3D11_COMPARISON_FUNC(d.depthCompare+1);
    dd.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};dd.BackFace=dd.FrontFace;
    check(device->CreateDepthStencilState(&dd,&draw->depthState),"depth state creation");
    D3D11_BLEND_DESC bd{};auto& color=bd.RenderTarget[0];color.RenderTargetWriteMask=UINT8(d.colorMask);
    color.BlendEnable=d.blendEnable;
    if(d.blendWord==0x07060706){color.SrcBlend=color.SrcBlendAlpha=D3D11_BLEND_SRC_ALPHA;color.DestBlend=color.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;}
    else{color.SrcBlend=color.SrcBlendAlpha=D3D11_BLEND_ONE;color.DestBlend=color.DestBlendAlpha=D3D11_BLEND_ZERO;}
    color.BlendOp=color.BlendOpAlpha=D3D11_BLEND_OP_ADD;check(device->CreateBlendState(&bd,&draw->blend),"replace color state creation");
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=d.cull==0?D3D11_CULL_NONE:D3D11_CULL_BACK;
    rd.FrontCounterClockwise=d.cull==2;rd.ScissorEnable=d.scissorEnable;rd.DepthClipEnable=TRUE;
    check(device->CreateRasterizerState(&rd,&draw->raster),"raster state creation");
    if(!noShadowSamples)for(size_t slot=0;slot<shadowCount;++slot){draw->shadows[slot]=d.shadows[slot]->depthView;check(device->CreateSamplerState(&d.samplers[slot],&draw->samplers[slot]),"shadow sampler creation");}
    const RecordedConstants data{materialVS,materialPS,receipt.inputMask};
    // NativeRecordingPayload owns the copied bytes and draw, including both
    // shader objects. Preparation never calls this backend or original CPU code.
    NativeRecordingPrepare prepare=[](ID3D11DeviceContext* immediate,const std::shared_ptr<void>& owner,std::span<const uint8_t> bytes) {
        const auto draw=std::static_pointer_cast<RecordedDraw>(owner);
        need(bool(draw)&&bytes.size()==sizeof(RecordedConstants),"Native rigid recorded constant snapshot changed");
        need(immediate&&immediate->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE&&draw->thread==GetCurrentThreadId(),"Native rigid preparation requires its owning immediate thread/context");
        ComPtr<ID3D11Device> device;immediate->GetDevice(&device);need(device.Get()==draw->device.Get(),"Native rigid preparation belongs to another device");
        draw->live->state->validate(draw->owner,device.Get());
        RecordedConstants material{};std::memcpy(&material,bytes.data(),sizeof(material));
        need(material.inputMask==draw->inputMask,"Native rigid recorded inheritance mask changed");
        inherit(material.vertex,draw->live->state->vertex,material.inputMask,0);
        inherit(material.pixel,draw->live->state->pixel,material.inputMask,8);
        finite(material.vertex);finite(material.pixel);
        // Each draw has DISTINCT DEFAULT storage, updated only here. Recording
        // UpdateSubresource into the list would overwrite every replay's values.
        immediate->UpdateSubresource(draw->vertexConstants.Get(),0,nullptr,material.vertex.data(),0,0);
        immediate->UpdateSubresource(draw->pixelConstants.Get(),0,nullptr,material.pixel.data(),0,0);
    };
    recordRecordingDraw(payload,std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data),sizeof(data)),draw,std::move(prepare),
        [draw](ID3D11DeviceContext* deferred){draw->record(deferred);});
}
}
