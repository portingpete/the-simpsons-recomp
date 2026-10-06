#include "sky_mesh.h"
#include "runtime/stall_profiler.h"
#include "common/geometry_extent.h"
#include "native_material_compiler.h"
#include "mesh_upload_cache.h"
#include "r16_index_validation.h"
#include "r16_strip_chunks.h"
#include "VSSky.h"
#include "PSSkyDraw.h"
#include "PSSkyOpaqueDraw.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>

namespace Simpsons::Graphics {
// Per-backend exact-content cache, never cross-device.
struct SkyMeshUploadCache {
    ExactContentMeshCache<NativeSkyMesh> cache;
};
namespace {
void need(bool ok,const char* message){if(!ok)throw Error(message);}
void skyPair(const CompiledMaterial& vertex,const CompiledMaterial& pixel) {
    need((vertex.originalAddress()==0x82036C2C&&pixel.originalAddress()==0x820371EC)||
         (vertex.originalAddress()==0x82036F08&&pixel.originalAddress()==0x820374E8),
         "Native sky requires an exact matching original sky shader pair");
}
void check(HRESULT result,const char* operation) {
    if(FAILED(result)){char message[180];std::snprintf(message,sizeof(message),"Native sky mesh %s failed: %08lX",operation,ULONG(result));throw Error(message);}
}
ComPtr<ID3D11Buffer> buffer(ID3D11Device* device,const void* data,UINT bytes,UINT binding,D3D11_USAGE usage) {
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;desc.BindFlags=binding;desc.Usage=usage;
    const D3D11_SUBRESOURCE_DATA initial{data,0,0};ComPtr<ID3D11Buffer> result;
    check(device->CreateBuffer(&desc,data?&initial:nullptr,&result),"buffer allocation");return result;
}
}
struct DepthConstants {uint32_t reverse,constantBits,slopeBits,reserved;};
struct NativeSkyMesh::State {
    const NativeBackend* owner{};DWORD thread{};ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Buffer> vertices,indices;ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11PixelShader> depthPixel,opaqueDepthPixel;
    uint32_t vertexCount{};R16Indices indexValues;
    void validate(const NativeBackend* backend,ID3D11Device* expected) const {
        need(owner==backend&&thread==GetCurrentThreadId()&&device.Get()==expected&&vertices&&indices&&layout&&depthPixel&&opaqueDepthPixel,
             "Native sky mesh is missing, stale or belongs to another backend");
    }
    void requireBindings(ID3D11DeviceContext* context) const {
        ComPtr<ID3D11Buffer> vb,ib;ComPtr<ID3D11InputLayout> input;UINT stride{},offset{},indexOffset{};DXGI_FORMAT format{};
        context->IAGetVertexBuffers(0,1,&vb,&stride,&offset);context->IAGetInputLayout(&input);
        context->IAGetIndexBuffer(&ib,&format,&indexOffset);
        need(vb.Get()==vertices.Get()&&stride==sizeof(SkyVertex)&&!offset,"Native sky mesh vertex binding is stale");
        need(input.Get()==layout.Get(),"Native sky mesh declaration binding is stale");
        need(ib.Get()==indices.Get()&&format==DXGI_FORMAT_R16_UINT&&!indexOffset,"Native sky mesh index binding is stale");
    }
};
NativeSkyMesh::NativeSkyMesh(std::unique_ptr<State> value):state(std::move(value)){}
NativeSkyMesh::~NativeSkyMesh()=default;
uint32_t NativeSkyMesh::vertexCount() const noexcept{return state->vertexCount;}
uint32_t NativeSkyMesh::indexCount() const noexcept{return uint32_t(state->indexValues.size());}

std::shared_ptr<NativeSkyMesh> NativeBackend::uploadSkyMesh(std::span<const SkyVertex> vertices,std::span<const uint16_t> indices) {
    validateSubmissionContext();
    need(validNativeMeshBufferExtent(vertices.size(),sizeof(SkyVertex))&&validNativeMeshBufferExtent(indices.size(),sizeof(uint16_t)),
         "Native sky mesh exceeds buffer byte or index extent");
    // Exact decoded bytes only: never guest address, never hash-only.
    const std::span<const uint8_t> vertexBytes{reinterpret_cast<const uint8_t*>(vertices.data()), vertices.size_bytes()};
    const std::span<const uint8_t> indexBytes{reinterpret_cast<const uint8_t*>(indices.data()), indices.size_bytes()};
    const uint64_t contentKey =
        ExactContentMeshCache<NativeSkyMesh>::contentKey(vertexBytes, indexBytes);
    if(!skyMeshCache_) skyMeshCache_ = std::make_shared<SkyMeshUploadCache>();
    auto& slot = *skyMeshCache_;
    if(auto hit = slot.cache.find(vertexBytes, indexBytes, contentKey)) {
        // Exact immutable bytes were validated before insertion; stale
        // wrong-device resources fail here and propagate, never reupload.
        hit->state->validate(this, device.Get());
        requireOwner();
        return hit;
    }
    for(const auto& v:vertices) {
        for(float x:v.position)need(std::isfinite(x),"Nonfinite sky position");
        for(float x:v.uv)need(std::isfinite(x),"Nonfinite sky UV");
        for(float x:v.uv1)need(std::isfinite(x),"Nonfinite sky UV1");
    }
    auto result=std::make_unique<NativeSkyMesh::State>();result->owner=this;result->thread=owner;result->device=device;
    result->vertexCount=uint32_t(vertices.size());result->indexValues.assign(indices.begin(),indices.end());
    result->vertices=buffer(device.Get(),vertices.data(),UINT(vertices.size_bytes()),D3D11_BIND_VERTEX_BUFFER,D3D11_USAGE_IMMUTABLE);
    result->indices=buffer(device.Get(),indices.data(),UINT(indices.size_bytes()),D3D11_BIND_INDEX_BUFFER,D3D11_USAGE_IMMUTABLE);
    // SkyInput layout: TEXCOORD0 float3 position, TEXCOORD1 float2 uv.
    // uv1 trails in the buffer (stride 28) for faithful decode but the
    // rigidalpha VS fetches only position+uv.
    const D3D11_INPUT_ELEMENT_DESC elements[]={
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}};
    check(device->CreateInputLayout(elements,UINT(std::size(elements)),kVSSky,sizeof(kVSSky),&result->layout),"sky input layout creation");
    check(device->CreatePixelShader(kPSSkyDraw,sizeof(kPSSkyDraw),nullptr,&result->depthPixel),"sky depth adapter creation");
    check(device->CreatePixelShader(kPSSkyOpaqueDraw,sizeof(kPSSkyOpaqueDraw),nullptr,&result->opaqueDepthPixel),"opaque sky depth adapter creation");
    result->validate(this,device.Get());requireOwner();
    auto shared = std::shared_ptr<NativeSkyMesh>(new NativeSkyMesh(std::move(result)));
    // Snapshot exact bytes so later caller mutation cannot corrupt the cache.
    // Eviction drops only the cache's strong ref; live caller handles stay valid.
    slot.cache.insert(vertexBytes, indexBytes, contentKey, shared);
    return shared;
}
void NativeBackend::bindSkyMeshVertices(const std::shared_ptr<NativeSkyMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing sky mesh vertices");mesh->state->validate(this,device.Get());
    flushIm2D();auto* value=mesh->state->vertices.Get();const UINT stride=sizeof(SkyVertex),offset=0;
    context->IASetVertexBuffers(0,1,&value,&stride,&offset);
}
void NativeBackend::bindSkyMeshDeclaration(const std::shared_ptr<NativeSkyMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing sky mesh declaration");mesh->state->validate(this,device.Get());
    flushIm2D();context->IASetInputLayout(mesh->state->layout.Get());
}
void NativeBackend::bindSkyMeshIndices(const std::shared_ptr<NativeSkyMesh>& mesh) {
    validateSubmissionContext();need(bool(mesh),"Missing sky mesh indices");mesh->state->validate(this,device.Get());
    flushIm2D();context->IASetIndexBuffer(mesh->state->indices.Get(),DXGI_FORMAT_R16_UINT,0);
}
void NativeBackend::bindSkyMeshVertices(const std::shared_ptr<NativeRecordingPayload>& payload,const std::shared_ptr<NativeSkyMesh>& mesh) {
    auto* deferred=checkedRecordingPayloadContext(payload);need(bool(mesh),"Missing recorded sky vertices");mesh->state->validate(this,device.Get());
    auto* value=mesh->state->vertices.Get();const UINT stride=sizeof(SkyVertex),offset=0;deferred->IASetVertexBuffers(0,1,&value,&stride,&offset);
}
void NativeBackend::bindSkyMeshDeclaration(const std::shared_ptr<NativeRecordingPayload>& payload,const std::shared_ptr<NativeSkyMesh>& mesh) {
    auto* deferred=checkedRecordingPayloadContext(payload);need(bool(mesh),"Missing recorded sky declaration");mesh->state->validate(this,device.Get());
    deferred->IASetInputLayout(mesh->state->layout.Get());
}
void NativeBackend::bindSkyMeshIndices(const std::shared_ptr<NativeRecordingPayload>& payload,const std::shared_ptr<NativeSkyMesh>& mesh) {
    auto* deferred=checkedRecordingPayloadContext(payload);need(bool(mesh),"Missing recorded sky indices");mesh->state->validate(this,device.Get());
    deferred->IASetIndexBuffer(mesh->state->indices.Get(),DXGI_FORMAT_R16_UINT,0);
}
struct NativeSkyReplayConstants::State {
    const NativeBackend* owner{};DWORD thread{};ComPtr<ID3D11Device> device;bool active=true,ready=false;
    SkyVertexConstants vertex{};SkyPixelConstants pixel{};
    void validate(const NativeBackend* backend,ID3D11Device* expected) const {
        need(active&&owner==backend&&thread==GetCurrentThreadId()&&device.Get()==expected,"Sky replay constants are released or belong to another owner");
    }
};
NativeSkyReplayConstants::NativeSkyReplayConstants(std::unique_ptr<State> value):state(std::move(value)){}
NativeSkyReplayConstants::~NativeSkyReplayConstants()=default;
namespace {
template<class T>void skyFinite(const T& data){for(const auto& row:data)for(float x:row)need(std::isfinite(x),"Nonfinite sky recorded constant");}
template<class T>void skyInherit(T& material,const T& live,const NativeRecordingMask& mask,size_t offset) {
    for(size_t row=0;row<material.size();++row){const size_t group=row/4;
        if(mask[offset+group/8]&(0x80u>>(group%8)))material[row]=live[row];}
}
struct SkyRecordedConstants {SkyVertexConstants vertex;SkyPixelConstants pixel;NativeRecordingMask mask;};
struct SkyRecordedDraw {
    const NativeBackend* owner{};DWORD thread{};ComPtr<ID3D11Device> device;
    std::shared_ptr<NativeSkyMesh> mesh;std::shared_ptr<NativeSkyReplayConstants> live;
    std::shared_ptr<RenderTarget> color;std::shared_ptr<DepthTarget> depth;
    std::array<std::shared_ptr<Texture>,4> textures;
    std::shared_ptr<RenderTarget> lineTarget;
    ComPtr<ID3D11RenderTargetView> colorView;ComPtr<ID3D11DepthStencilView> depthView;
    std::array<ComPtr<ID3D11ShaderResourceView>,4> textureViews;
    std::array<ComPtr<ID3D11SamplerState>,4> samplers;
    ComPtr<ID3D11Buffer> vertices,indices,vc,pc,depthConstants;
    SkyVertexConstants uploadedVertex{};
    SkyPixelConstants uploadedPixel{};
    ComPtr<ID3D11InputLayout> layout;ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11DepthStencilState> depthState;ComPtr<ID3D11RasterizerState> raster;ComPtr<ID3D11BlendState> blend;
    D3D11_VIEWPORT viewport{};D3D11_RECT scissor{};uint32_t count{},start{},mask{};int32_t base{};
    NativeRecordingMask inheritance{};
    void record(ID3D11DeviceContext* deferred) const {
        auto* target=colorView.Get();deferred->OMSetRenderTargets(1,&target,depthView.Get());
        deferred->OMSetBlendState(blend.Get(),nullptr,mask);deferred->OMSetDepthStencilState(depthState.Get(),0);
        deferred->RSSetState(raster.Get());deferred->RSSetViewports(1,&viewport);deferred->RSSetScissorRects(1,&scissor);
        deferred->SetPredication(nullptr,FALSE);deferred->SOSetTargets(0,nullptr,nullptr);
        deferred->GSSetShader(nullptr,nullptr,0);deferred->HSSetShader(nullptr,nullptr,0);deferred->DSSetShader(nullptr,nullptr,0);
        deferred->VSSetShader(vertex.Get(),nullptr,0);deferred->PSSetShader(pixel.Get(),nullptr,0);
        ID3D11Buffer* v[]={vc.Get(),nullptr};ID3D11Buffer* p[]={pc.Get(),depthConstants.Get()};
        deferred->VSSetConstantBuffers(0,2,v);deferred->PSSetConstantBuffers(0,2,p);
        for(UINT i=0;i<4;++i){auto* view=textureViews[i].Get();auto* sampler=samplers[i].Get();
            deferred->PSSetShaderResources(i,1,&view);deferred->PSSetSamplers(i,1,&sampler);}
        std::array<ID3D11Buffer*,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> streams{};streams[0]=vertices.Get();
        std::array<UINT,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> strides{},offsets{};strides[0]=sizeof(SkyVertex);
        deferred->IASetVertexBuffers(0,UINT(streams.size()),streams.data(),strides.data(),offsets.data());
        deferred->IASetInputLayout(layout.Get());deferred->IASetIndexBuffer(indices.Get(),DXGI_FORMAT_R16_UINT,0);
        deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        forEachOriginalR16StripChunk(start,count,[&](uint32_t n,uint32_t first){deferred->DrawIndexed(n,first,base);});
    }
};
}
std::shared_ptr<NativeSkyReplayConstants> NativeBackend::createSkyReplayConstants() {
    validateSubmissionContext();auto value=std::make_unique<NativeSkyReplayConstants::State>();value->owner=this;value->thread=owner;value->device=device;
    return std::shared_ptr<NativeSkyReplayConstants>(new NativeSkyReplayConstants(std::move(value)));
}
void NativeBackend::updateSkyReplayConstants(const std::shared_ptr<NativeSkyReplayConstants>& live,const SkyVertexConstants& vertex,const SkyPixelConstants& pixel) {
    validateSubmissionContext();need(bool(live),"Missing sky replay owner");live->state->validate(this,device.Get());skyFinite(vertex);skyFinite(pixel);
    live->state->vertex=vertex;live->state->pixel=pixel;live->state->ready=true;
}
void NativeBackend::releaseSkyReplayConstants(const std::shared_ptr<NativeSkyReplayConstants>& live) {
    validateSubmissionContext();need(bool(live),"Missing sky replay owner");live->state->validate(this,device.Get());live->state->active=false;
}
void skyBaseSamplerProfile(const D3D11_SAMPLER_DESC& sampler) {
    need(sampler.Filter==D3D11_FILTER_MIN_MAG_MIP_LINEAR&&sampler.AddressU==D3D11_TEXTURE_ADDRESS_WRAP&&
         sampler.AddressV==D3D11_TEXTURE_ADDRESS_WRAP&&sampler.AddressW==D3D11_TEXTURE_ADDRESS_WRAP&&
         sampler.MipLODBias==0&&sampler.MinLOD==0&&sampler.MaxLOD==13&&sampler.MaxAnisotropy==1&&
         sampler.ComparisonFunc==D3D11_COMPARISON_NEVER,"Native sky requires original linear/wrap tiled sampling");
    for(float value:sampler.BorderColor)need(std::isfinite(value),"Native sky sampler border is nonfinite");
}
void skyLineSamplerProfile(const D3D11_SAMPLER_DESC& sampler) {
    need(sampler.Filter==D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR&&sampler.AddressU==D3D11_TEXTURE_ADDRESS_CLAMP&&
         sampler.AddressV==D3D11_TEXTURE_ADDRESS_CLAMP&&sampler.AddressW==D3D11_TEXTURE_ADDRESS_CLAMP&&
         sampler.MipLODBias==0&&sampler.MinLOD==0&&sampler.MaxLOD==13&&sampler.MaxAnisotropy==1&&
         sampler.ComparisonFunc==D3D11_COMPARISON_NEVER,"Native sky requires original point/clamp line sampling");
    for(float value:sampler.BorderColor)need(std::isfinite(value),"Native sky line sampler border is nonfinite");
}
class ScopedSkyDrawBindings {
    ID3D11DeviceContext* context;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11RasterizerState> raster;
    std::array<ComPtr<ID3D11SamplerState>,4> samplers;
    std::array<ComPtr<ID3D11ShaderResourceView>,4> resources;
    UINT stencil{},sampleMask{};
    FLOAT factors[4]{};
    D3D11_PRIMITIVE_TOPOLOGY topology{};
    std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
    std::array<D3D11_RECT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors{};
    UINT viewportCount=UINT(viewports.size()),scissorCount=UINT(scissors.size());
public:
    explicit ScopedSkyDrawBindings(ID3D11DeviceContext* c):context(c) {
        context->PSGetShader(&pixel,nullptr,nullptr);context->PSGetConstantBuffers(1,1,&constants);
        context->OMGetDepthStencilState(&depth,&stencil);context->OMGetBlendState(&blend,factors,&sampleMask);
        context->RSGetState(&raster);context->RSGetViewports(&viewportCount,viewports.data());
        context->RSGetScissorRects(&scissorCount,scissors.data());context->IAGetPrimitiveTopology(&topology);
        for(UINT i=0;i<samplers.size();++i){context->PSGetSamplers(i,1,&samplers[i]);context->PSGetShaderResources(i,1,&resources[i]);}
    }
    ScopedSkyDrawBindings(const ScopedSkyDrawBindings&)=delete;
    ScopedSkyDrawBindings& operator=(const ScopedSkyDrawBindings&)=delete;
    ~ScopedSkyDrawBindings() {
        context->PSSetShader(pixel.Get(),nullptr,0);auto* cb=constants.Get();context->PSSetConstantBuffers(1,1,&cb);
        context->OMSetDepthStencilState(depth.Get(),stencil);context->OMSetBlendState(blend.Get(),factors,sampleMask);
        context->RSSetState(raster.Get());context->RSSetViewports(viewportCount,viewports.data());
        context->RSSetScissorRects(scissorCount,scissors.data());context->IASetPrimitiveTopology(topology);
        for(UINT i=0;i<samplers.size();++i){auto* sampler=samplers[i].Get();auto* resource=resources[i].Get();
            context->PSSetSamplers(i,1,&sampler);context->PSSetShaderResources(i,1,&resource);}
    }
};
void requireSkyDrawStages(ID3D11DeviceContext* context) {
    ComPtr<ID3D11Predicate> predicate;BOOL value{};context->GetPredication(&predicate,&value);
    need(!predicate,"Native sky mesh predication is unqualified");
    ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;
    context->GSGetShader(&gs,nullptr,nullptr);context->HSGetShader(&hs,nullptr,nullptr);context->DSGetShader(&ds,nullptr,nullptr);
    need(!gs&&!hs&&!ds,"Native sky mesh has an unrelated geometry/tessellation stage");
    std::array<ID3D11Buffer*,D3D11_SO_BUFFER_SLOT_COUNT> outputs{};context->SOGetTargets(UINT(outputs.size()),outputs.data());
    bool bound=false;for(auto* output:outputs)if(output){bound=true;output->Release();}
    need(!bound,"Native sky mesh stream output is unqualified");
}
void NativeBackend::drawSkyMesh(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,
    const std::shared_ptr<NativeSkyMesh>& mesh,const CompiledMaterial& vertex,const CompiledMaterial& pixel,
    const std::shared_ptr<NativeSkyCommit>& commit,const SkyMeshDraw& d) {
    validateSubmissionContext();need(bool(mesh),"Missing native sky mesh draw owner");auto& m=*mesh->state;m.validate(this,device.Get());
    skyPair(vertex,pixel);
    need(d.primitiveType==6&&d.primitiveReset==1&&d.primitiveResetIndex==0xFFFF,
         "Native sky requires primitive6 R16 strip and restartFFFF");
    need(validR16DrawRange(m.indexValues,m.vertexCount,d.startIndex,d.indexCount,d.baseVertex),
         "Native sky effective index range exceeds its owned vertices or indices");
    need(d.depthPolicy==ShadowMeshDepthPolicy::Reference20e4Rne,"Native sky requires explicit Reference20e4Rne depth policy");
    need(d.depthEnable<=1&&d.depthWrite<=1&&d.depthCompare<=7,"Native sky depth state is not canonical");
    need((d.cull==0||d.cull==2||d.cull==6)&&d.fill==0&&d.colorMask<=15,"Native sky cull/fill/color mask is unqualified");
    // Original827402C4 clears expanded blending, retaining enable/factors
    // when the shared zero-bone fallback subsequently selects opaque sky.
    need((!d.blendEnable&&d.blendWord==0x00010001&&d.expandedBlend<=1)||
         (d.blendEnable==1&&d.blendWord==0x07060706&&d.expandedBlend<=1),
         "Native sky blend state is not its original replacement or inherited source-alpha tuple");
    need(!d.stencilEnable&&!d.alphaTest&&!d.alphaToMask&&!d.clipPlaneEnable&&d.viewportEnable==1,"Native sky stencil/alpha/clipping state is unqualified");
    need(d.scissorEnable<=1&&d.halfPixelOffset==1&&d.multisampleAntialias==1&&
         (d.multisampleMask==0xFFFFFFFF||d.multisampleMask==0x0000FFFF),"Native sky requires halfpixel1 and single-sample full-mask state");
    const float bias=std::bit_cast<float>(d.depthBiasBits),slope=std::bit_cast<float>(d.slopeBiasBits);
    need(std::isfinite(bias)&&std::isfinite(slope)&&std::isfinite(slope*16),"Native sky bias is nonfinite or overflows");
    validateFrontTarget(target);validateDepthCopyTarget(depth);
    need(depth->width==target->width&&depth->height==target->height,"Native sky attachment dimensions differ");
    const bool reverse=d.viewport[4]==0x3F800000&&d.viewport[5]==0;
    need(d.viewport[0]==0&&d.viewport[1]==0&&d.viewport[2]==target->width&&d.viewport[3]==target->height&&
         (reverse||(d.viewport[4]==0&&d.viewport[5]==0x3F800000)),"Native sky requires a full-target viewport with depth0..1 or1..0");
    if(d.scissorEnable) {
        need(d.scissor[0]<d.scissor[2]&&d.scissor[1]<d.scissor[3]&&d.scissor[2]<=target->width&&d.scissor[3]<=target->height,
             "Native sky scissor exceeds the explicit target");
        const auto retained=scissor();need(retained&&*retained==d.scissor,"Native sky scissor differs from the original retained rectangle");
    }
    for(uint32_t stage=0;stage<3;++stage) {
        need(bool(d.textures[stage]),"Native sky draw is missing a committed layer texture");
        validateTexture(d.textures[stage]);
        skyBaseSamplerProfile(d.samplers[stage]);
    }
    if(d.lineTarget) {
        validateFrontTarget(d.lineTarget);
        need(d.lineTarget->texture.Get()!=target->texture.Get(),"Native sky line target aliases its output");
        need(d.lineTarget->width==target->width&&d.lineTarget->height==target->height,"Native sky line target dimensions differ from its output");
    } else {
        need(bool(d.textures[3]),"Native sky draw is missing a committed line texture");
        validateTexture(d.textures[3]);
        need(d.textures[3]->texture.Get()!=target->texture.Get(),"Native sky line texture aliases its output");
    }
    skyLineSamplerProfile(d.samplers[3]);
    const auto bindings=[&] {
        requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);requireRigidShaders(vertex,pixel);requireSkyCommit(commit);
        m.requireBindings(context.Get());requireSkyDrawStages(context.Get());
        const UINT boundStages=d.lineTarget?3:4;
        for(UINT i=0;i<boundStages;++i){ComPtr<ID3D11ShaderResourceView> actual;context->PSGetShaderResources(i,1,&actual);
            need(actual.Get()==d.textures[i]->view.Get(),"Native sky sampled layer binding differs from its owner");}
    };
    bindings();requireNoOutputUavs();
    // Reuse immutable scene states instead of per-draw driver allocations.
    // All validation above precedes lookup; misses create once and publish.
    auto constants=sceneDepthConstantBuffer(uint32_t(reverse),d.depthBiasBits,d.slopeBiasBits);
    auto depthState=sceneDepthState(d.depthEnable,d.depthWrite,d.depthCompare);
    auto blend=sceneBlendState(d.blendEnable,d.blendWord,d.colorMask);
    auto raster=sceneRasterState(d.cull,d.scissorEnable);
    std::array<ComPtr<ID3D11SamplerState>,4> samplers;
    for(UINT i=0;i<4;++i)samplers[i]=sceneSamplerState(materialSampling(d.samplers[i]));
    flushIm2D();bindings();
    {
        ScopedSkyDrawBindings restore(context.Get());
        const auto viewport=renderViewport(target,{0,0,float(target->width),float(target->height),0,1});context->RSSetViewports(1,&viewport);
        const D3D11_RECT scissor=renderScissor(target,d.scissorEnable?D3D11_RECT{LONG(d.scissor[0]),LONG(d.scissor[1]),LONG(d.scissor[2]),LONG(d.scissor[3])}:
            D3D11_RECT{0,0,LONG(target->width),LONG(target->height)});
        context->RSSetScissorRects(1,&scissor);context->RSSetState(raster.Get());
        context->OMSetBlendState(blend.Get(),nullptr,d.multisampleMask);context->OMSetDepthStencilState(depthState.Get(),0);
        auto* cb=constants.Get();context->PSSetConstantBuffers(1,1,&cb);
        context->PSSetShader(pixel.originalAddress()==0x820371EC?m.opaqueDepthPixel.Get():m.depthPixel.Get(),nullptr,0);
        if(d.lineTarget){auto* line=d.lineTarget->sampledView.Get();context->PSSetShaderResources(3,1,&line);}
        for(UINT i=0;i<4;++i){auto* sampler=samplers[i].Get();context->PSSetSamplers(i,1,&sampler);}
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        forEachOriginalR16StripChunk(d.startIndex,d.indexCount,[&](uint32_t n,uint32_t first){context->DrawIndexed(n,first,d.baseVertex);});
        ++skyMeshDraws;
    }
    bindings();requireOwner();
}

void NativeBackend::recordSkyMesh(const std::shared_ptr<NativeRecordingPayload>& payload,
    const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,
    const std::shared_ptr<NativeSkyMesh>& mesh,const CompiledMaterial& vertex,const CompiledMaterial& pixel,
    const SkyVertexConstants& materialVS,const SkyPixelConstants& materialPS,
    const std::shared_ptr<NativeSkyReplayConstants>& live,const SkyMeshDraw& d) {
    (void)checkedRecordingPayloadContext(payload);need(bool(mesh)&&bool(live),"Missing native sky mesh/replay owner");
    skyPair(vertex,pixel);
    auto& m=*mesh->state;m.validate(this,device.Get());live->state->validate(this,device.Get());skyFinite(materialVS);skyFinite(materialPS);
    const auto receipt=recordingPayloadReceipt(payload);
    need(d.primitiveType==6&&d.primitiveReset==1&&d.primitiveResetIndex==0xFFFF,
         "Native sky requires primitive6 R16 strip and restartFFFF");
    need(validR16DrawRange(m.indexValues,m.vertexCount,d.startIndex,d.indexCount,d.baseVertex),
         "Native sky effective index range exceeds its owned vertices or indices");
    need(d.depthPolicy==ShadowMeshDepthPolicy::Reference20e4Rne,"Native sky requires explicit Reference20e4Rne depth policy");
    need(d.depthEnable<=1&&d.depthWrite<=1&&d.depthCompare<=7,"Native sky depth state is not canonical");
    need((d.cull==0||d.cull==2||d.cull==6)&&d.fill==0&&d.colorMask<=15,"Native sky cull/fill/color mask is unqualified");
    need((!d.blendEnable&&d.blendWord==0x00010001&&d.expandedBlend<=1)||
         (d.blendEnable==1&&d.blendWord==0x07060706&&d.expandedBlend<=1),
         "Native sky blend state is not its original replacement or inherited source-alpha tuple");
    need(!d.stencilEnable&&!d.alphaTest&&!d.alphaToMask&&!d.clipPlaneEnable&&d.viewportEnable==1,"Native sky stencil/alpha/clipping state is unqualified");
    need(d.scissorEnable<=1&&d.halfPixelOffset==1&&d.multisampleAntialias==1&&
         (d.multisampleMask==0xFFFFFFFF||d.multisampleMask==0x0000FFFF),"Native sky requires halfpixel1 and single-sample full-mask state");
    const float bias=std::bit_cast<float>(d.depthBiasBits),slope=std::bit_cast<float>(d.slopeBiasBits);
    need(std::isfinite(bias)&&std::isfinite(slope)&&std::isfinite(slope*16),"Native sky bias is nonfinite or overflows");
    validateFrontTarget(target);validateDepthCopyTarget(depth);
    need(depth->width==target->width&&depth->height==target->height,"Native sky attachment dimensions differ");
    const bool reverse=d.viewport[4]==0x3F800000&&d.viewport[5]==0;
    need(d.viewport[0]==0&&d.viewport[1]==0&&d.viewport[2]==target->width&&d.viewport[3]==target->height&&
         (reverse||(d.viewport[4]==0&&d.viewport[5]==0x3F800000)),"Native sky requires a full-target viewport with depth0..1 or1..0");
    if(d.scissorEnable) {
        need(d.scissor[0]<d.scissor[2]&&d.scissor[1]<d.scissor[3]&&d.scissor[2]<=target->width&&d.scissor[3]<=target->height,
             "Native sky scissor exceeds the explicit target");
    }
    for(uint32_t stage=0;stage<3;++stage) {
        need(bool(d.textures[stage]),"Native sky draw is missing a committed layer texture");
        validateTexture(d.textures[stage]);
        need(d.textures[stage]->texture.Get()!=target->texture.Get(),"Recorded sky texture aliases its output");
        skyBaseSamplerProfile(d.samplers[stage]);
    }
    if(d.lineTarget) {
        validateFrontTarget(d.lineTarget);
        need(d.lineTarget->texture.Get()!=target->texture.Get(),"Recorded sky line target aliases its output");
        need(d.lineTarget->width==target->width&&d.lineTarget->height==target->height,"Recorded sky line target dimensions differ from its output");
    } else {
        need(bool(d.textures[3]),"Recorded sky draw is missing a committed line texture");
        validateTexture(d.textures[3]);
        need(d.textures[3]->texture.Get()!=target->texture.Get(),"Recorded sky line texture aliases its output");
    }
    skyLineSamplerProfile(d.samplers[3]);

    auto draw=std::make_shared<SkyRecordedDraw>();draw->owner=this;draw->thread=owner;draw->device=device;
    rigidShaderObjects(vertex,pixel,draw->vertex,draw->pixel);
    draw->pixel=pixel.originalAddress()==0x820371EC?m.opaqueDepthPixel:m.depthPixel;
    draw->mesh=mesh;draw->live=live;draw->color=target;draw->depth=depth;draw->textures=d.textures;draw->lineTarget=d.lineTarget;
    draw->colorView=target->view;draw->depthView=depth->view;
    for(UINT i=0;i<3;++i)draw->textureViews[i]=d.textures[i]->view;
    draw->textureViews[3]=d.lineTarget?d.lineTarget->sampledView:d.textures[3]->view;
    draw->vertices=m.vertices;draw->indices=m.indices;draw->layout=m.layout;
    draw->count=d.indexCount;draw->start=d.startIndex;draw->base=d.baseVertex;draw->mask=d.multisampleMask;draw->inheritance=receipt.inputMask;
    draw->viewport=renderViewport(target,{0,0,float(target->width),float(target->height),0,1});
    draw->scissor=renderScissor(target,d.scissorEnable?D3D11_RECT{LONG(d.scissor[0]),LONG(d.scissor[1]),LONG(d.scissor[2]),LONG(d.scissor[3])}:
        D3D11_RECT{0,0,LONG(target->width),LONG(target->height)});
    draw->vc=buffer(device.Get(),materialVS.data(),sizeof(materialVS),D3D11_BIND_CONSTANT_BUFFER,D3D11_USAGE_DEFAULT);
    draw->pc=buffer(device.Get(),materialPS.data(),sizeof(materialPS),D3D11_BIND_CONSTANT_BUFFER,D3D11_USAGE_DEFAULT);
    std::memcpy(draw->uploadedVertex.data(),materialVS.data(),sizeof(materialVS));
    std::memcpy(draw->uploadedPixel.data(),materialPS.data(),sizeof(materialPS));
    const DepthConstants values{uint32_t(reverse),d.depthBiasBits,d.slopeBiasBits,0};
    draw->depthConstants=buffer(device.Get(),&values,sizeof(values),D3D11_BIND_CONSTANT_BUFFER,D3D11_USAGE_IMMUTABLE);
    D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=d.depthEnable;dd.DepthWriteMask=d.depthWrite?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc=D3D11_COMPARISON_FUNC(d.depthCompare+1);
    dd.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};dd.BackFace=dd.FrontFace;
    check(device->CreateDepthStencilState(&dd,&draw->depthState),"direct depth state creation");
    D3D11_BLEND_DESC bd{};auto& color=bd.RenderTarget[0];color.RenderTargetWriteMask=UINT8(d.colorMask);
    color.BlendEnable=d.blendEnable;
    color.SrcBlend=color.SrcBlendAlpha=d.blendEnable?D3D11_BLEND_SRC_ALPHA:D3D11_BLEND_ONE;
    color.DestBlend=color.DestBlendAlpha=d.blendEnable?D3D11_BLEND_INV_SRC_ALPHA:D3D11_BLEND_ZERO;
    color.BlendOp=color.BlendOpAlpha=D3D11_BLEND_OP_ADD;check(device->CreateBlendState(&bd,&draw->blend),"direct replace color state creation");
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=d.cull==0?D3D11_CULL_NONE:D3D11_CULL_BACK;
    rd.FrontCounterClockwise=d.cull==2;rd.ScissorEnable=d.scissorEnable;rd.DepthClipEnable=TRUE;
    check(device->CreateRasterizerState(&rd,&draw->raster),"direct raster state creation");
    for(UINT i=0;i<4;++i)draw->samplers[i]=sceneSamplerState(materialSampling(d.samplers[i]));

    const SkyRecordedConstants data{materialVS,materialPS,receipt.inputMask};
    NativeRecordingPrepare prepare=[](ID3D11DeviceContext* immediate,const std::shared_ptr<void>& owner,std::span<const uint8_t> bytes) {
        const auto draw=std::static_pointer_cast<SkyRecordedDraw>(owner);
        need(bool(draw)&&bytes.size()==sizeof(SkyRecordedConstants),"Sky recorded constant snapshot changed");
        need(immediate&&immediate->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE&&draw->thread==GetCurrentThreadId(),"Sky preparation requires its owning immediate thread/context");
        ComPtr<ID3D11Device> device;immediate->GetDevice(&device);need(device.Get()==draw->device.Get(),"Sky preparation belongs to another device");
        draw->live->state->validate(draw->owner,device.Get());need(draw->live->state->ready,"Sky replay has no completed original uploads");
        SkyRecordedConstants material{};std::memcpy(&material,bytes.data(),sizeof(material));
        need(material.mask==draw->inheritance,"Sky recorded inheritance mask changed");
        skyInherit(material.vertex,draw->live->state->vertex,material.mask,0);
        skyInherit(material.pixel,draw->live->state->pixel,material.mask,8);
        skyFinite(material.vertex);skyFinite(material.pixel);
        if(std::memcmp(draw->uploadedVertex.data(),material.vertex.data(),sizeof(material.vertex))) {
            StallProfiler::Scope updateProfile(StallProfiler::Section::Rendering,"D3D11.UpdateSubresource.skyVertex",nullptr,reinterpret_cast<uintptr_t>(draw->vc.Get()));
            immediate->UpdateSubresource(draw->vc.Get(),0,nullptr,material.vertex.data(),0,0);
            std::memcpy(draw->uploadedVertex.data(),material.vertex.data(),sizeof(material.vertex));
        }
        if(std::memcmp(draw->uploadedPixel.data(),material.pixel.data(),sizeof(material.pixel))) {
            StallProfiler::Scope updateProfile(StallProfiler::Section::Rendering,"D3D11.UpdateSubresource.skyPixel",nullptr,reinterpret_cast<uintptr_t>(draw->pc.Get()));
            immediate->UpdateSubresource(draw->pc.Get(),0,nullptr,material.pixel.data(),0,0);
            std::memcpy(draw->uploadedPixel.data(),material.pixel.data(),sizeof(material.pixel));
        }
    };
    recordRecordingDraw(payload,std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data),sizeof(data)),draw,std::move(prepare),
        [draw](ID3D11DeviceContext* deferred){draw->record(deferred);});
}
}
