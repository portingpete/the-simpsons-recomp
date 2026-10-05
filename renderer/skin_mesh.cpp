#include "skin_mesh.h"
#include "common/geometry_extent.h"
#include "immutable_depth_constants.h"
#include "mesh_upload_cache.h"
#include "r16_index_validation.h"
#include "r16_strip_chunks.h"
#include "VSSkin.h"
#include "VSSkinDraw.h"
#include "PSSkinDraw.h"
#include "PSSkinAlphaDraw.h"
#include "renderer/skin_input.h"
#include "material_resources.h"
#include "VSSkinDual.h"
#include "PSSkinDualDraw.h"
#include "PSSkinDualAlphaDraw.h"
#include "VSSkinTextured.h"
#include "PSSkinTexturedDraw.h"
#include "PSSkinTexturedAlphaDraw.h"
#include "VSSkinGloss.h"
#include "VSSkinGlossAlpha.h"
#include "PSSkinGlossDraw.h"
#include "PSSkinGlossAlphaDraw.h"
#include "VSSkinFlipbook.h"
#include "VSSkinFlipbookAlpha.h"
#include "PSSkinFlipbookDraw.h"
#include "PSSkinFlipbookAlphaDraw.h"
#include "VSSkinDualUV.h"
#include "VSSkinDualUVAlpha.h"
#include "PSSkinDualUVDraw.h"
#include "PSSkinDualUVAlphaDraw.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>

namespace Simpsons::Graphics {
// Per-backend exact-content caches, never cross-device. Skin separates base
// vs dual vs textured profiles so identical bytes under any profile never alias.
struct SkinMeshUploadCache {
    ExactContentMeshCache<NativeSkinMesh> base;
    ExactContentMeshCache<NativeSkinMesh> dual;
    ExactContentMeshCache<NativeSkinMesh> textured;
    // New families preserve each original VS identity and consumed layout.
    // Gloss/flipbook omit UV1 only in alpha; DualUV consumes it in both.
    std::array<ExactContentMeshCache<NativeSkinMesh>,6> variants;
};
namespace {
void need(bool ok,const char* message){if(!ok)throw Error(message);}
struct SkinMeshProfile {
    uint32_t opaqueVertex,alphaVertex,opaquePixel,alphaPixel;
    std::span<const uint8_t> layout,opaqueDraw,alphaDraw;
    uint32_t cache;bool exactPass,uv1,shadowed,material,second;
};
SkinMeshProfile meshProfile(uint32_t address) {
    if(address==0x82007C1C||address==0x82008E20)
        return {0x82007C1C,0x82008E20,0x8200A02C,0x8200A4A4,kVSSkin,kPSSkinDraw,kPSSkinAlphaDraw,0,false,false,false,false,false};
    if(address==0x8201E6DC||address==0x8201F984)
        return {0x8201E6DC,0x8201F984,0x82020B9C,0x82021344,kVSSkinDual,kPSSkinDualDraw,kPSSkinDualAlphaDraw,1,false,true,true,true,false};
    if(address==0x8201146C||address==0x820126EC)
        return {0x8201146C,0x820126EC,0x82013900,0x82013F8C,kVSSkinTextured,kPSSkinTexturedDraw,kPSSkinTexturedAlphaDraw,2,false,false,true,true,false};
    if(address==0x8202579C||address==0x820269E8) {
        const bool alpha=address==0x820269E8;
        return {0x8202579C,0x820269E8,0x82027C18,0x82028258,alpha?std::span<const uint8_t>(kVSSkinGlossAlpha):std::span<const uint8_t>(kVSSkinGloss),
            kPSSkinGlossDraw,kPSSkinGlossAlphaDraw,3+uint32_t(alpha),true,!alpha,false,true,false};
    }
    if(address==0x8203D93C||address==0x8203ED38) {
        const bool alpha=address==0x8203ED38;
        return {0x8203D93C,0x8203ED38,0x82040118,0x82040510,alpha?std::span<const uint8_t>(kVSSkinFlipbookAlpha):std::span<const uint8_t>(kVSSkinFlipbook),
            kPSSkinFlipbookDraw,kPSSkinFlipbookAlphaDraw,5+uint32_t(alpha),true,!alpha,false,true,false};
    }
    if(address==0x8204BA4C||address==0x8204CE90) {
        const bool alpha=address==0x8204CE90;
        return {0x8204BA4C,0x8204CE90,0x8204E2DC,0x8204E75C,alpha?std::span<const uint8_t>(kVSSkinDualUVAlpha):std::span<const uint8_t>(kVSSkinDualUV),
            kPSSkinDualUVDraw,kPSSkinDualUVAlphaDraw,7+uint32_t(alpha),true,true,false,true,true};
    }
    throw Error("Unqualified skin mesh shader profile");
}
bool requirePass(const SkinMeshProfile& profile,uint32_t uploaded,uint32_t vertex,uint32_t pixel) {
    const bool alpha=pixel==profile.alphaPixel;
    need(pixel==(alpha?profile.alphaPixel:profile.opaquePixel)&&vertex==(alpha?profile.alphaVertex:profile.opaqueVertex)&&
         (!profile.exactPass||uploaded==vertex),"Skin mesh layout does not match the selected shader");
    return alpha;
}
void check(HRESULT result,const char* operation) {
    if(FAILED(result)){char message[180];std::snprintf(message,sizeof(message),"Native skin mesh %s failed: %08lX",operation,ULONG(result));throw Error(message);}
}
template<class T>void finite(const T& bank) {
    for(const auto& row:bank)for(float value:row)need(std::isfinite(value),"Native skin constants contain a nonfinite value");
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
void samplerProfile(const D3D11_SAMPLER_DESC& sampler,bool base) {
    need(sampler.Filter==(base?D3D11_FILTER_MIN_MAG_MIP_LINEAR:D3D11_FILTER_MIN_MAG_MIP_POINT)&&
         sampler.AddressU==(base?D3D11_TEXTURE_ADDRESS_WRAP:D3D11_TEXTURE_ADDRESS_CLAMP)&&
         sampler.AddressV==sampler.AddressU&&sampler.AddressW==sampler.AddressU&&
         sampler.MipLODBias==0&&sampler.MinLOD==0&&sampler.MaxLOD==(base?13.0f:0.0f)&&
         sampler.MaxAnisotropy==1&&sampler.ComparisonFunc==D3D11_COMPARISON_NEVER,
         "Native dual skin requires original point/clamp/base-level depth and linear/wrap base samplers");
    for(float value:sampler.BorderColor)need(std::isfinite(value),"Nonfinite skin sampler border");
}
class ScopedSkinDrawBindings {
    ID3D11DeviceContext* context;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11RasterizerState> raster;
    std::array<ComPtr<ID3D11SamplerState>,2> samplers;
    UINT stencil{},sampleMask{};
    FLOAT factors[4]{};
    D3D11_PRIMITIVE_TOPOLOGY topology{};
    std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
    std::array<D3D11_RECT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors{};
    UINT viewportCount=UINT(viewports.size()),scissorCount=UINT(scissors.size());
public:
    explicit ScopedSkinDrawBindings(ID3D11DeviceContext* c):context(c) {
        context->VSGetShader(&vertex,nullptr,nullptr);
        context->PSGetShader(&pixel,nullptr,nullptr);context->PSGetConstantBuffers(1,1,&constants);
        context->OMGetDepthStencilState(&depth,&stencil);context->OMGetBlendState(&blend,factors,&sampleMask);
        context->RSGetState(&raster);context->RSGetViewports(&viewportCount,viewports.data());
        context->RSGetScissorRects(&scissorCount,scissors.data());context->IAGetPrimitiveTopology(&topology);
        for(UINT i=0;i<2;++i)context->PSGetSamplers(i,1,&samplers[i]);
    }
    ScopedSkinDrawBindings(const ScopedSkinDrawBindings&)=delete;
    ScopedSkinDrawBindings& operator=(const ScopedSkinDrawBindings&)=delete;
    ~ScopedSkinDrawBindings() {
        context->VSSetShader(vertex.Get(),nullptr,0);
        context->PSSetShader(pixel.Get(),nullptr,0);auto* cb=constants.Get();context->PSSetConstantBuffers(1,1,&cb);
        context->OMSetDepthStencilState(depth.Get(),stencil);context->OMSetBlendState(blend.Get(),factors,sampleMask);
        context->RSSetState(raster.Get());context->RSSetViewports(viewportCount,viewports.data());
        context->RSSetScissorRects(scissorCount,scissors.data());context->IASetPrimitiveTopology(topology);
        for(UINT i=0;i<2;++i){auto* sampler=samplers[i].Get();context->PSSetSamplers(i,1,&sampler);}
    }
};
void requireSkinDrawStages(ID3D11DeviceContext* context) {
    ComPtr<ID3D11Predicate> predicate;BOOL value{};context->GetPredication(&predicate,&value);
    need(!predicate,"Native skin mesh predication is unqualified");
    ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;
    context->GSGetShader(&gs,nullptr,nullptr);context->HSGetShader(&hs,nullptr,nullptr);context->DSGetShader(&ds,nullptr,nullptr);
    need(!gs&&!hs&&!ds,"Native skin mesh has an unrelated geometry/tessellation stage");
    std::array<ID3D11Buffer*,D3D11_SO_BUFFER_SLOT_COUNT> outputs{};context->SOGetTargets(UINT(outputs.size()),outputs.data());
    bool bound=false;for(auto* output:outputs)if(output){bound=true;output->Release();}
    need(!bound,"Native skin mesh stream output is unqualified");
}
}
struct NativeSkinMesh::State {
    const NativeBackend* owner{};DWORD thread{};ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Buffer> vertices,indices;ComPtr<ID3D11InputLayout> layout;ComPtr<ID3D11PixelShader> pixel,alphaPixel;
    ComPtr<ID3D11VertexShader> drawVertex;
    uint32_t vertexCount{},vertexAddress{};R16Indices indexValues;
    DepthConstantsCache depthConstants;
    void validate(const NativeBackend* backend,ID3D11Device* expected) const {
        need(owner==backend&&thread==GetCurrentThreadId()&&device.Get()==expected&&vertices&&indices&&layout&&pixel&&
             alphaPixel&&((vertexAddress==0x82007C1C)==bool(drawVertex)),
             "Native skin mesh is missing, stale or belongs to another backend");
    }
    void requireBindings(ID3D11DeviceContext* context) const {
        ComPtr<ID3D11Buffer> vb,ib;ComPtr<ID3D11InputLayout> input;UINT stride{},offset{},indexOffset{};DXGI_FORMAT format{};
        context->IAGetVertexBuffers(0,1,&vb,&stride,&offset);context->IAGetInputLayout(&input);
        context->IAGetIndexBuffer(&ib,&format,&indexOffset);
        need(vb.Get()==vertices.Get()&&stride==sizeof(SkinVertex)&&!offset,"Native skin mesh vertex binding is stale");
        need(input.Get()==layout.Get(),"Native skin mesh declaration binding is stale");
        need(ib.Get()==indices.Get()&&format==DXGI_FORMAT_R16_UINT&&!indexOffset,"Native skin mesh index binding is stale");
    }
};
NativeSkinMesh::NativeSkinMesh(std::unique_ptr<State> value):state(std::move(value)){}
NativeSkinMesh::~NativeSkinMesh()=default;
uint32_t NativeSkinMesh::vertexCount() const noexcept{return state->vertexCount;}
uint32_t NativeSkinMesh::indexCount() const noexcept{return uint32_t(state->indexValues.size());}
struct NativeSkinReplayConstants::State {
    const NativeBackend* owner{};DWORD thread{};ComPtr<ID3D11Device> device;bool active=true;
    SkinVertexConstants vertex{};SkinPixelConstants pixel{};
    void validate(const NativeBackend* backend,ID3D11Device* expected) const {
        need(active&&owner==backend&&thread==GetCurrentThreadId()&&device.Get()==expected,
             "Native skin replay constants are released, stale or belong to another backend");
    }
};

std::shared_ptr<NativeSkinMesh> NativeBackend::uploadSkinMesh(std::span<const SkinVertex> vertices,std::span<const uint16_t> indices,uint32_t vertexAddress) {
    validateSubmissionContext();
    const auto profile=meshProfile(vertexAddress);
    need(validNativeMeshBufferExtent(vertices.size(),sizeof(SkinVertex))&&validNativeMeshBufferExtent(indices.size(),sizeof(uint16_t)),
         "Native skin mesh exceeds buffer byte or index extent");
    // Exact decoded bytes only: never guest address, never hash-only.
    const std::span<const uint8_t> vertexBytes{reinterpret_cast<const uint8_t*>(vertices.data()), vertices.size_bytes()};
    const std::span<const uint8_t> indexBytes{reinterpret_cast<const uint8_t*>(indices.data()), indices.size_bytes()};
    const uint64_t contentKey =
        ExactContentMeshCache<NativeSkinMesh>::contentKey(vertexBytes, indexBytes);
    if(!skinMeshCache_) skinMeshCache_ = std::make_shared<SkinMeshUploadCache>();
    auto& cache = profile.cache>=3?skinMeshCache_->variants[profile.cache-3]:
        profile.cache==2?skinMeshCache_->textured:profile.cache==1?skinMeshCache_->dual:skinMeshCache_->base;
    if(auto hit = cache.find(vertexBytes, indexBytes, contentKey)) {
        // Exact immutable bytes were validated before insertion; stale
        // wrong-device resources fail here and propagate, never reupload.
        hit->state->validate(this, device.Get());
        requireOwner();
        return hit;
    }
    for(const auto& v:vertices) {
        for(float x:v.position)need(std::isfinite(x),"Nonfinite skin position");
        for(float x:v.normal)need(std::isfinite(x),"Nonfinite skin normal");
        for(float x:v.uv)need(std::isfinite(x),"Nonfinite skin UV");
        for(float x:v.uv1)need(std::isfinite(x),"Nonfinite skin UV1");
        for(float x:v.indices)need(std::isfinite(x)&&x>=0&&x<=63&&std::floor(x)==x,"Skin bone index not integer 0..63");
        for(float x:v.weights)need(std::isfinite(x),"Nonfinite skin weight");
        for(float x:v.color)need(std::isfinite(x),"Nonfinite skin color");
        for(float x:v.morph1)need(std::isfinite(x),"Nonfinite skin morph");
        for(float x:v.morph2)need(std::isfinite(x),"Nonfinite skin morph");
        for(float x:v.morph3)need(std::isfinite(x),"Nonfinite skin morph");
        for(float x:v.morph4)need(std::isfinite(x),"Nonfinite skin morph");
        for(float x:v.morph5)need(std::isfinite(x),"Nonfinite skin morph");
        for(float x:v.morph6)need(std::isfinite(x),"Nonfinite skin morph");
    }
    auto result=std::make_unique<NativeSkinMesh::State>();result->owner=this;result->thread=owner;result->device=device;
    result->vertexCount=uint32_t(vertices.size());result->vertexAddress=profile.exactPass?vertexAddress:profile.opaqueVertex;result->indexValues.assign(indices.begin(),indices.end());
    result->vertices=buffer(device.Get(),vertices.data(),UINT(vertices.size_bytes()),D3D11_BIND_VERTEX_BUFFER,D3D11_USAGE_IMMUTABLE);
    result->indices=buffer(device.Get(),indices.data(),UINT(indices.size_bytes()),D3D11_BIND_INDEX_BUFFER,D3D11_USAGE_IMMUTABLE);
    std::vector<D3D11_INPUT_ELEMENT_DESC> elements(SkinInputElements.begin(),SkinInputElements.end());
    if(profile.uv1)elements.push_back({"TEXCOORD",12,DXGI_FORMAT_R32G32_FLOAT,0,152,D3D11_INPUT_PER_VERTEX_DATA,0});
    check(device->CreateInputLayout(elements.data(),UINT(elements.size()),
        profile.layout.data(),profile.layout.size(),&result->layout),"skin input layout creation");
    check(device->CreatePixelShader(profile.opaqueDraw.data(),profile.opaqueDraw.size(),nullptr,&result->pixel),"skin depth/color adapter creation");
    check(device->CreatePixelShader(profile.alphaDraw.data(),profile.alphaDraw.size(),nullptr,&result->alphaPixel),"skin alpha depth/color adapter creation");
    if(profile.cache==0)
        check(device->CreateVertexShader(kVSSkinDraw,sizeof(kVSSkinDraw),nullptr,&result->drawVertex),"skin rim control adapter creation");
    result->validate(this,device.Get());requireOwner();
    auto shared = std::shared_ptr<NativeSkinMesh>(new NativeSkinMesh(std::move(result)));
    // Snapshot exact bytes so later caller mutation cannot corrupt the cache.
    // Eviction drops only the cache's strong ref; live caller handles stay valid.
    cache.insert(vertexBytes, indexBytes, contentKey, shared);
    return shared;
}
void NativeBackend::bindSkinMeshVertices(const std::shared_ptr<NativeSkinMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing skin mesh vertices");mesh->state->validate(this,device.Get());
    flushIm2D();auto* value=mesh->state->vertices.Get();const UINT stride=sizeof(SkinVertex),offset=0;
    context->IASetVertexBuffers(0,1,&value,&stride,&offset);
}
void NativeBackend::bindSkinMeshDeclaration(const std::shared_ptr<NativeSkinMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing skin mesh declaration");mesh->state->validate(this,device.Get());
    flushIm2D();context->IASetInputLayout(mesh->state->layout.Get());
}
void NativeBackend::bindSkinMeshIndices(const std::shared_ptr<NativeSkinMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing skin mesh indices");mesh->state->validate(this,device.Get());
    flushIm2D();context->IASetIndexBuffer(mesh->state->indices.Get(),DXGI_FORMAT_R16_UINT,0);
}
NativeSkinReplayConstants::NativeSkinReplayConstants(std::unique_ptr<State> value):state(std::move(value)){}
NativeSkinReplayConstants::~NativeSkinReplayConstants()=default;
std::shared_ptr<NativeSkinReplayConstants> NativeBackend::createSkinReplayConstants(const SkinVertexConstants& vertices,const SkinPixelConstants& pixels) {
    validateSubmissionContext();finite(vertices);finite(pixels);auto value=std::make_unique<NativeSkinReplayConstants::State>();
    value->owner=this;value->thread=owner;value->device=device;value->vertex=vertices;value->pixel=pixels;
    return std::shared_ptr<NativeSkinReplayConstants>(new NativeSkinReplayConstants(std::move(value)));
}
void NativeBackend::updateSkinReplayConstants(const std::shared_ptr<NativeSkinReplayConstants>& live,const SkinVertexConstants& vertices,const SkinPixelConstants& pixels) {
    validateSubmissionContext();need(bool(live),"Missing skin replay constants");live->state->validate(this,device.Get());finite(vertices);finite(pixels);
    live->state->vertex=vertices;live->state->pixel=pixels;
}
void NativeBackend::releaseSkinReplayConstants(const std::shared_ptr<NativeSkinReplayConstants>& live) {
    validateSubmissionContext();need(bool(live),"Missing skin replay constants");live->state->validate(this,device.Get());live->state->active=false;
}
std::vector<SkinVertex> NativeBackend::readbackSkinMeshVertices(const std::shared_ptr<NativeSkinMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing skin mesh readback owner");mesh->state->validate(this,device.Get());
    return readBuffer<SkinVertex>(device.Get(),context.Get(),mesh->state->vertices.Get());
}
std::vector<uint16_t> NativeBackend::readbackSkinMeshIndices(const std::shared_ptr<NativeSkinMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing skin index readback owner");mesh->state->validate(this,device.Get());
    return readBuffer<uint16_t>(device.Get(),context.Get(),mesh->state->indices.Get());
}
// Only value data is serialized into the native receipt. Resource/state COM
// ownership lives separately; these bytes are NOT original SDK command bytes.
struct RecordedSkinConstants {
    SkinVertexConstants vertex;
    SkinPixelConstants pixel;
    NativeRecordingMask inputMask;
};
static_assert(sizeof(DepthConstants)==16&&sizeof(RecordedSkinConstants)==5160&&std::is_trivially_copyable_v<RecordedSkinConstants>);
template<class T>void inheritSkin(T& material,const T& live,const NativeRecordingMask& mask,size_t offset) {
    // Original 826F3DC0/3F90: MSB-first bits, FOUR float4 registers per bit.
    for(size_t row=0;row<material.size();++row) {
        const size_t group=row/4;
        if(mask[offset+group/8]&(0x80u>>(group%8)))material[row]=live[row];
    }
}
struct RecordedSkinDraw {
    ComPtr<ID3D11Device> device;
    const NativeBackend* owner{};
    DWORD thread{};
    std::shared_ptr<NativeSkinMesh> mesh;
    std::shared_ptr<NativeSkinReplayConstants> live;
    std::shared_ptr<RenderTarget> colorOwner;
    std::shared_ptr<DepthTarget> depthOwner;
    std::shared_ptr<DepthTarget> shadowOwner;
    std::shared_ptr<Texture> baseOwner;
    std::shared_ptr<Texture> secondOwner;
    ComPtr<ID3D11ShaderResourceView> shadowView,baseView,secondView;
    std::array<ComPtr<ID3D11SamplerState>,2> samplers;
    ComPtr<ID3D11Buffer> vertices,indices,vertexConstants,pixelConstants,depthConstants;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> originalPixel,drawPixel;
    ComPtr<ID3D11RenderTargetView> color;
    ComPtr<ID3D11DepthStencilView> depth;
    ComPtr<ID3D11DepthStencilState> depthState;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11BlendState> blend;
    D3D11_VIEWPORT viewport{};
    D3D11_RECT scissor{};
    uint32_t indexCount{},startIndex{},sampleMask{};
    int32_t baseVertex{};
    NativeRecordingMask inputMask{};
    void record(ID3D11DeviceContext* destination) const {
        auto* target=color.Get();destination->OMSetRenderTargets(1,&target,depth.Get());
        destination->OMSetBlendState(blend.Get(),nullptr,sampleMask);destination->OMSetDepthStencilState(depthState.Get(),0);
        destination->RSSetState(raster.Get());destination->RSSetViewports(1,&viewport);destination->RSSetScissorRects(1,&scissor);
        destination->SetPredication(nullptr,FALSE);destination->SOSetTargets(0,nullptr,nullptr);
        destination->GSSetShader(nullptr,nullptr,0);destination->HSSetShader(nullptr,nullptr,0);destination->DSSetShader(nullptr,nullptr,0);
        destination->VSSetShader(vertex.Get(),nullptr,0);destination->PSSetShader(drawPixel.Get(),nullptr,0);
        ID3D11Buffer* vbanks[]={vertexConstants.Get(),nullptr};destination->VSSetConstantBuffers(0,2,vbanks);
        ID3D11Buffer* pbanks[]={pixelConstants.Get(),depthConstants.Get()};destination->PSSetConstantBuffers(0,2,pbanks);
        if(baseOwner) {
            if(shadowOwner) {
                ID3D11ShaderResourceView* views[]{shadowView.Get(),baseView.Get()};destination->PSSetShaderResources(0,2,views);
                ID3D11SamplerState* states[]{samplers[0].Get(),samplers[1].Get()};destination->PSSetSamplers(0,2,states);
            } else if(secondOwner) {
                ID3D11ShaderResourceView* views[]{baseView.Get(),secondView.Get()};destination->PSSetShaderResources(0,2,views);
                ID3D11SamplerState* states[]{samplers[0].Get(),samplers[1].Get()};destination->PSSetSamplers(0,2,states);
            } else {
                auto* view=baseView.Get();destination->PSSetShaderResources(0,1,&view);
                auto* state=samplers[0].Get();destination->PSSetSamplers(0,1,&state);
            }
        }
        std::array<ID3D11Buffer*,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> streams{};streams[0]=vertices.Get();
        std::array<UINT,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> strides{},offsets{};strides[0]=sizeof(SkinVertex);
        destination->IASetVertexBuffers(0,UINT(streams.size()),streams.data(),strides.data(),offsets.data());
        destination->IASetInputLayout(layout.Get());destination->IASetIndexBuffer(indices.Get(),DXGI_FORMAT_R16_UINT,0);
        destination->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        forEachOriginalR16StripChunk(startIndex,indexCount,[&](uint32_t n,uint32_t first){destination->DrawIndexed(n,first,baseVertex);});
    }
};
void NativeBackend::drawSkinMesh(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,
    const std::shared_ptr<NativeSkinMesh>& mesh,const CompiledMaterial& vertex,const CompiledMaterial& pixel,
    const std::shared_ptr<NativeSkinCommit>& commit,const SkinMeshDraw& d) {
    validateSubmissionContext();need(bool(mesh),"Missing native skin mesh draw owner");auto& m=*mesh->state;m.validate(this,device.Get());
    const auto profile=meshProfile(m.vertexAddress);
    const bool alpha=requirePass(profile,m.vertexAddress,vertex.originalAddress(),pixel.originalAddress());
    const bool shadowed=profile.shadowed&&!alpha,material=profile.material||alpha;
    need(shadowed==bool(d.characterShadow)&&material==bool(d.baseTexture)&&profile.second==bool(d.secondTexture),"Skin textures do not match the selected shader");
    if(shadowed) {
        need(d.shadowSamplePolicy==RigidShadowSamplePolicy::ReferenceD24FS8DepthRRRR,"Skin shadow sampling policy is unqualified");
        validateDepthCopyTarget(d.characterShadow);validateTexture(d.baseTexture);
        need(d.characterShadow->width==1024&&d.characterShadow->height==1024&&
             depth&&d.characterShadow->texture.Get()!=depth->texture.Get(),"Skin character shadow extent or output alias differs");
        samplerProfile(d.samplers[0],false);samplerProfile(d.samplers[1],true);
    } else if(material) {
        validateTexture(d.baseTexture);samplerProfile(d.samplers[0],true);
        if(profile.second){validateTexture(d.secondTexture);samplerProfile(d.samplers[1],true);}
    }
    need(d.primitiveType==6&&d.primitiveReset==1&&d.primitiveResetIndex==0xFFFF,
         "Native skin requires primitive6 R16 strip and restartFFFF");
    need(validR16DrawRange(m.indexValues,m.vertexCount,d.startIndex,d.indexCount,d.baseVertex),
         "Native skin effective index range exceeds its owned vertices or indices");
    need(d.depthPolicy==ShadowMeshDepthPolicy::Reference20e4Rne,"Native skin requires explicit Reference20e4Rne depth policy");
    need(d.depthEnable<=1&&d.depthWrite<=1&&d.depthCompare<=7,"Native skin depth state is not canonical");
    need((d.cull==0||d.cull==2||d.cull==6)&&d.fill==0&&d.colorMask<=15,"Native skin cull/fill/color mask is unqualified");
    // Original 827402B8 clears only expanded blending after an alpha draw.
    // 827401CC skips the blend setters for the next opaque draw, retaining
    // enable=1 and the same RGB/alpha equation. Shader choice is independent
    // of this inherited pipeline state.
    need((!d.blendEnable&&d.blendWord==0x00010001&&d.expandedBlend<=1)||
         (d.blendEnable==1&&d.blendWord==0x07060706&&d.expandedBlend<=1),
         "Native skin requires explicit replacement or inherited source-alpha blend state");
    need(!d.stencilEnable&&!d.alphaTest&&!d.alphaToMask&&!d.clipPlaneEnable&&d.viewportEnable==1,"Native skin stencil/alpha/clipping state is unqualified");
    need(d.scissorEnable<=1&&d.halfPixelOffset==1&&d.multisampleAntialias==1&&
         (d.multisampleMask==0xFFFFFFFF||d.multisampleMask==0x0000FFFF),"Native skin requires halfpixel1 and single-sample full-mask state");
    const float bias=std::bit_cast<float>(d.depthBiasBits),slope=std::bit_cast<float>(d.slopeBiasBits);
    need(std::isfinite(bias)&&std::isfinite(slope)&&std::isfinite(slope*16),"Native skin bias is nonfinite or overflows");
    validateFrontTarget(target);validateDepthCopyTarget(depth);
    need(depth->width==target->width&&depth->height==target->height,"Native skin attachment dimensions differ");
    const bool reverse=d.viewport[4]==0x3F800000&&d.viewport[5]==0;
    need(d.viewport[0]==0&&d.viewport[1]==0&&d.viewport[2]==target->width&&d.viewport[3]==target->height&&
         (reverse||(d.viewport[4]==0&&d.viewport[5]==0x3F800000)),"Native skin requires a full-target viewport with depth0..1 or1..0");
    if(d.scissorEnable) {
        need(d.scissor[0]<d.scissor[2]&&d.scissor[1]<d.scissor[3]&&d.scissor[2]<=target->width&&d.scissor[3]<=target->height,
             "Native skin scissor exceeds the explicit target");
        const auto retained=scissor();need(retained&&*retained==d.scissor,"Native skin scissor differs from the original retained rectangle");
    }
    const auto bindings=[&] {
        requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);requireSkinShaders(vertex,pixel);requireSkinCommit(commit);
        m.requireBindings(context.Get());requireSkinDrawStages(context.Get());
        if(shadowed) {
            requireEngineTexture(1,d.baseTexture);ComPtr<ID3D11ShaderResourceView> shadow;
            context->PSGetShaderResources(0,1,&shadow);
            need(shadow.Get()==d.characterShadow->depthView.Get(),"Skin character shadow binding differs from its owner");
        } else if(material) {
            requireEngineTexture(0,d.baseTexture);
            if(profile.second)requireEngineTexture(1,d.secondTexture);
        }
    };
    bindings();requireNoOutputUavs();
    // Reuse backend scene states instead of per-draw driver allocations.
    // All validation above precedes lookup; misses create once and publish.
    // The per-mesh depth-constant cache is retained as a fast path; the
    // backend cache covers misses across meshes for steadier frame times.
    const DepthConstants values{uint32_t(reverse),d.depthBiasBits,d.slopeBiasBits,0};
    ComPtr<ID3D11Buffer> constants;
    if(auto* hit=m.depthConstants.find(values))constants=hit;
    else {
        constants=sceneDepthConstantBuffer(uint32_t(reverse),d.depthBiasBits,d.slopeBiasBits);
        m.depthConstants.publish(values,constants.Get());
    }
    auto depthState=sceneDepthState(d.depthEnable,d.depthWrite,d.depthCompare);
    auto blend=sceneBlendState(d.blendEnable,d.blendWord,d.colorMask);
    auto raster=sceneRasterState(d.cull,d.scissorEnable);
    std::array<ComPtr<ID3D11SamplerState>,2> samplers;
    if(shadowed)for(size_t i=0;i<2;++i)samplers[i]=sceneSamplerState(i==1?materialSampling(d.samplers[i]):d.samplers[i]);
    else if(material)for(size_t i=0;i<(profile.second?2u:1u);++i)samplers[i]=sceneSamplerState(materialSampling(d.samplers[i]));
    flushIm2D();bindings();
    {
        ScopedSkinDrawBindings restore(context.Get());
        if(m.drawVertex)context->VSSetShader(m.drawVertex.Get(),nullptr,0);
        const auto viewport=renderViewport(target,{0,0,float(target->width),float(target->height),0,1});context->RSSetViewports(1,&viewport);
        const D3D11_RECT scissor=renderScissor(target,d.scissorEnable?D3D11_RECT{LONG(d.scissor[0]),LONG(d.scissor[1]),LONG(d.scissor[2]),LONG(d.scissor[3])}:
            D3D11_RECT{0,0,LONG(target->width),LONG(target->height)});
        context->RSSetScissorRects(1,&scissor);context->RSSetState(raster.Get());
        context->OMSetBlendState(blend.Get(),nullptr,d.multisampleMask);context->OMSetDepthStencilState(depthState.Get(),0);
        auto* cb=constants.Get();context->PSSetConstantBuffers(1,1,&cb);context->PSSetShader(alpha?m.alphaPixel.Get():m.pixel.Get(),nullptr,0);
        if(shadowed)for(UINT i=0;i<2;++i){auto* sampler=samplers[i].Get();context->PSSetSamplers(i,1,&sampler);}
        else if(material)for(UINT i=0;i<(profile.second?2u:1u);++i){auto* sampler=samplers[i].Get();context->PSSetSamplers(i,1,&sampler);}
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        forEachOriginalR16StripChunk(d.startIndex,d.indexCount,[&](uint32_t n,uint32_t first){context->DrawIndexed(n,first,d.baseVertex);});
        ++skinMeshDraws;
    }
    bindings();requireOwner();
}
void NativeBackend::bindSkinMeshVertices(const std::shared_ptr<NativeRecordingPayload>& payload,const std::shared_ptr<NativeSkinMesh>& mesh) {
    auto* deferred=checkedRecordingPayloadContext(payload);need(bool(mesh),"Missing skin mesh vertex owner");mesh->state->validate(this,device.Get());
    auto* value=mesh->state->vertices.Get();const UINT stride=sizeof(SkinVertex),offset=0;deferred->IASetVertexBuffers(0,1,&value,&stride,&offset);
}
void NativeBackend::bindSkinMeshDeclaration(const std::shared_ptr<NativeRecordingPayload>& payload,const std::shared_ptr<NativeSkinMesh>& mesh) {
    auto* deferred=checkedRecordingPayloadContext(payload);need(bool(mesh),"Missing skin mesh declaration owner");mesh->state->validate(this,device.Get());
    deferred->IASetInputLayout(mesh->state->layout.Get());
}
void NativeBackend::bindSkinMeshIndices(const std::shared_ptr<NativeRecordingPayload>& payload,const std::shared_ptr<NativeSkinMesh>& mesh) {
    auto* deferred=checkedRecordingPayloadContext(payload);need(bool(mesh),"Missing skin mesh index owner");mesh->state->validate(this,device.Get());
    deferred->IASetIndexBuffer(mesh->state->indices.Get(),DXGI_FORMAT_R16_UINT,0);
}
void NativeBackend::recordSkinMesh(const std::shared_ptr<NativeRecordingPayload>& payload,
    const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,
    const std::shared_ptr<NativeSkinMesh>& mesh,const CompiledMaterial& vertex,const CompiledMaterial& pixel,
    const SkinVertexConstants& materialVS,const SkinPixelConstants& materialPS,
    const std::shared_ptr<NativeSkinReplayConstants>& live,const SkinMeshDraw& d) {
    (void)checkedRecordingPayloadContext(payload);need(bool(mesh)&&bool(live),"Missing native skin mesh/replay owner");
    auto& m=*mesh->state;m.validate(this,device.Get());live->state->validate(this,device.Get());finite(materialVS);finite(materialPS);
    const auto profile=meshProfile(m.vertexAddress);
    const bool alpha=requirePass(profile,m.vertexAddress,vertex.originalAddress(),pixel.originalAddress());
    const bool shadowed=profile.shadowed&&!alpha,material=profile.material||alpha;
    need(shadowed==bool(d.characterShadow)&&material==bool(d.baseTexture)&&profile.second==bool(d.secondTexture),"Recorded skin textures do not match the selected shader");
    if(shadowed) {
        need(d.shadowSamplePolicy==RigidShadowSamplePolicy::ReferenceD24FS8DepthRRRR,"Recorded skin sampling policy is unqualified");
        validateDepthCopyTarget(d.characterShadow);validateTexture(d.baseTexture);
        need(d.characterShadow->width==1024&&d.characterShadow->height==1024&&depth&&
             d.characterShadow->texture.Get()!=depth->texture.Get(),"Recorded skin shadow extent or alias differs");
        samplerProfile(d.samplers[0],false);samplerProfile(d.samplers[1],true);
    } else if(material) {
        validateTexture(d.baseTexture);samplerProfile(d.samplers[0],true);
        if(profile.second){validateTexture(d.secondTexture);samplerProfile(d.samplers[1],true);}
    }
    const auto receipt=recordingPayloadReceipt(payload);
    need(d.primitiveType==6&&d.primitiveReset==1&&d.primitiveResetIndex==0xFFFF,
         "Native skin requires primitive6 R16 strip and restartFFFF");
    need(validR16DrawRange(m.indexValues,m.vertexCount,d.startIndex,d.indexCount,d.baseVertex),
         "Native skin effective index range exceeds its owned vertices or indices");
    need(d.depthPolicy==ShadowMeshDepthPolicy::Reference20e4Rne,"Native skin requires explicit Reference20e4Rne depth policy");
    need(d.depthEnable<=1&&d.depthWrite<=1&&d.depthCompare<=7,"Native skin depth state is not canonical");
    need((d.cull==0||d.cull==2||d.cull==6)&&d.fill==0&&d.colorMask<=15,"Native skin cull/fill/color mask is unqualified");
    // The original opaque dispatcher inherits the preceding alpha equation;
    // see the immediate path's pinned producer sites above.
    need((!d.blendEnable&&d.blendWord==0x00010001&&d.expandedBlend<=1)||
         (d.blendEnable==1&&d.blendWord==0x07060706&&d.expandedBlend<=1),
         "Native skin requires explicit replacement or inherited source-alpha blend state");
    need(!d.stencilEnable&&!d.alphaTest&&!d.alphaToMask&&!d.clipPlaneEnable&&d.viewportEnable==1,"Native skin stencil/alpha/clipping state is unqualified");
    need(d.scissorEnable<=1&&d.halfPixelOffset==1&&d.multisampleAntialias==1&&
         (d.multisampleMask==0xFFFFFFFF||d.multisampleMask==0x0000FFFF),
         "Native skin requires halfpixel1 and single-sample full-mask state");
    const float bias=std::bit_cast<float>(d.depthBiasBits),slope=std::bit_cast<float>(d.slopeBiasBits);
    need(std::isfinite(bias)&&std::isfinite(slope)&&std::isfinite(slope*16),"Native skin bias is nonfinite or overflows");
    validateFrontTarget(target);validateDepthCopyTarget(depth);
    need(depth->width==target->width&&depth->height==target->height,"Native skin attachment dimensions differ");
    const bool reverse=d.viewport[4]==0x3F800000&&d.viewport[5]==0;
    need(d.viewport[0]==0&&d.viewport[1]==0&&d.viewport[2]==target->width&&d.viewport[3]==target->height&&
         (reverse||(d.viewport[4]==0&&d.viewport[5]==0x3F800000)),"Native skin requires a full-target viewport with depth0..1 or1..0");
    if(d.scissorEnable)need(d.scissor[0]<d.scissor[2]&&d.scissor[1]<d.scissor[3]&&d.scissor[2]<=target->width&&d.scissor[3]<=target->height,
                           "Native skin scissor exceeds the explicit target");
    auto draw=std::make_shared<RecordedSkinDraw>();draw->owner=this;draw->thread=owner;draw->device=device;
    skinShaderObjects(vertex,pixel,draw->vertex,draw->originalPixel);
    if(m.drawVertex)draw->vertex=m.drawVertex;
    draw->mesh=mesh;draw->live=live;draw->colorOwner=target;draw->depthOwner=depth;
    if(shadowed) {
        draw->shadowOwner=d.characterShadow;draw->baseOwner=d.baseTexture;
        draw->shadowView=d.characterShadow->depthView;draw->baseView=d.baseTexture->view;
        for(size_t i=0;i<2;++i)draw->samplers[i]=sceneSamplerState(i==1?materialSampling(d.samplers[i]):d.samplers[i]);
    } else if(material) {
        draw->baseOwner=d.baseTexture;draw->baseView=d.baseTexture->view;
        draw->samplers[0]=sceneSamplerState(materialSampling(d.samplers[0]));
        if(profile.second){draw->secondOwner=d.secondTexture;draw->secondView=d.secondTexture->view;
            draw->samplers[1]=sceneSamplerState(materialSampling(d.samplers[1]));}
    }
    draw->vertices=m.vertices;draw->indices=m.indices;draw->layout=m.layout;draw->drawPixel=alpha?m.alphaPixel:m.pixel;
    draw->color=target->view;draw->depth=depth->view;draw->indexCount=d.indexCount;draw->startIndex=d.startIndex;draw->baseVertex=d.baseVertex;
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
    color.SrcBlend=color.SrcBlendAlpha=d.blendEnable?D3D11_BLEND_SRC_ALPHA:D3D11_BLEND_ONE;
    color.DestBlend=color.DestBlendAlpha=d.blendEnable?D3D11_BLEND_INV_SRC_ALPHA:D3D11_BLEND_ZERO;
    color.BlendOp=color.BlendOpAlpha=D3D11_BLEND_OP_ADD;check(device->CreateBlendState(&bd,&draw->blend),"replace color state creation");
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=d.cull==0?D3D11_CULL_NONE:D3D11_CULL_BACK;
    rd.FrontCounterClockwise=d.cull==2;rd.ScissorEnable=d.scissorEnable;rd.DepthClipEnable=TRUE;
    check(device->CreateRasterizerState(&rd,&draw->raster),"raster state creation");
    const RecordedSkinConstants data{materialVS,materialPS,receipt.inputMask};
    NativeRecordingPrepare prepare=[](ID3D11DeviceContext* immediate,const std::shared_ptr<void>& owner,std::span<const uint8_t> bytes) {
        const auto draw=std::static_pointer_cast<RecordedSkinDraw>(owner);
        need(bool(draw)&&bytes.size()==sizeof(RecordedSkinConstants),"Native skin recorded constant snapshot changed");
        need(immediate&&immediate->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE&&draw->thread==GetCurrentThreadId(),"Native skin preparation requires its owning immediate thread/context");
        ComPtr<ID3D11Device> device;immediate->GetDevice(&device);need(device.Get()==draw->device.Get(),"Native skin preparation belongs to another device");
        draw->live->state->validate(draw->owner,device.Get());
        RecordedSkinConstants material{};std::memcpy(&material,bytes.data(),sizeof(material));
        need(material.inputMask==draw->inputMask,"Native skin recorded inheritance mask changed");
        inheritSkin(material.vertex,draw->live->state->vertex,material.inputMask,0);
        inheritSkin(material.pixel,draw->live->state->pixel,material.inputMask,8);
        finite(material.vertex);finite(material.pixel);
        immediate->UpdateSubresource(draw->vertexConstants.Get(),0,nullptr,material.vertex.data(),0,0);
        immediate->UpdateSubresource(draw->pixelConstants.Get(),0,nullptr,material.pixel.data(),0,0);
    };
    recordRecordingDraw(payload,std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data),sizeof(data)),draw,std::move(prepare),
        [draw](ID3D11DeviceContext* deferred){draw->record(deferred);});
}
}
