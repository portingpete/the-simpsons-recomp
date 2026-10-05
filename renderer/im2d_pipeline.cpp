#include "im2d_draw.h"
#include "im2d_layers.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <bit>
#include <d3d11_1.h>
#include "VSIm2DDraw.h"
#include "PSIm2DDrawFlat.h"
#include "PSIm2DDrawTextured.h"
#include "PSIm2DDepthFlat.h"
#include "PSIm2DDepthTextured.h"

namespace Simpsons::Graphics {
// Raster policy evidence (original/reference files remain immutable):
// 82439F00=81632948, 82439F04=51640038, 82439F08=90832948 preserve
// device+2948 except cull bits0..2. 8243B718=816329C0,
// 8243B720=5164003C, 8243B728=908329C0 preserve device+29C0 except bit0.
// 8243D074/088 store +W/2 and -H/2 viewport scales; 826D54B8 emits
// TL,BL,TR,BR for positive unrotated extents. Evidence independently checked in
// analysis/native-screen-raster-policy.json and build/im2d-upload/original logs.
// Supporting primary implementation research, not a copied runtime decoder:
// https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/registers.h
// https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/xenos.h
// https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/draw_util.cc
// https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/d3d12/pipeline_cache.cc
// D3D11 viewport, winding and subpixel rules: sections15.6,15.11,15.16 of
// https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm
// This implements a declared native raster policy. It does not prove console
// 1/16-pixel snapping/interpolation, clipping, filtering or expanded blend
// precision. The integration owner must qualify effective state separately.
namespace {
void check(HRESULT hr,const char* operation) {
    if(FAILED(hr)) {
        char reason[180];snprintf(reason,sizeof(reason),"Native Im2D %s failed: 0x%08lX",operation,ULONG(hr));
        throw Error(reason);
    }
}
struct PositionConstants {float scaleX,scaleY;float padding[2]{};};
struct DrawConstants {
    uint32_t depthReversed,reserved;
    float alphaReference;
    uint32_t alphaTest,alphaCompare,blendWord,padding[2]{};
};
static_assert(sizeof(PositionConstants)==16 && sizeof(DrawConstants)==32);
static_assert(offsetof(DrawConstants,depthReversed)==0 && offsetof(DrawConstants,alphaReference)==8 &&
              offsetof(DrawConstants,alphaTest)==12 && offsetof(DrawConstants,blendWord)==20);
static_assert(sizeof(Im2DVertex)==40 && offsetof(Im2DVertex,color)==16 && offsetof(Im2DVertex,uv)==32);

ComPtr<ID3D11Buffer> upload(ID3D11Device* device,const void* data,UINT bytes,UINT binding) {
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=binding;
    D3D11_SUBRESOURCE_DATA initial{data,0,0};ComPtr<ID3D11Buffer> buffer;
    check(device->CreateBuffer(&desc,&initial,&buffer),"owned input upload");return buffer;
}
void validateSampler(const D3D11_SAMPLER_DESC& s) {
    // Initial single-level, ordinary 2D texture profile. No inferred sampler
    // defaults, comparison sampling, anisotropy, LOD bias or mip selection.
    if((s.Filter!=D3D11_FILTER_MIN_MAG_MIP_POINT && s.Filter!=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT) ||
       s.MipLODBias!=0 || s.MinLOD!=0 || !std::isfinite(s.MaxLOD) || s.MaxLOD<0 ||
       s.MaxAnisotropy!=1 || s.ComparisonFunc!=D3D11_COMPARISON_NEVER)
        throw Error("Unsupported native Im2D sampler filter/LOD/comparison state");
    for(auto address:{s.AddressU,s.AddressV,s.AddressW})
        if(address!=D3D11_TEXTURE_ADDRESS_WRAP && address!=D3D11_TEXTURE_ADDRESS_CLAMP)
            throw Error("Native Im2D supports only wrap/clamp texture addressing");
    for(float component:s.BorderColor) if(!std::isfinite(component))
        throw Error("Nonfinite native Im2D sampler border");
}
}

struct Im2DPipeline {
    struct Scratch {
        UINT width=0,height=0;
        ComPtr<ID3D11Texture2D> output,destination;
        ComPtr<ID3D11RenderTargetView> outputView;
        ComPtr<ID3D11ShaderResourceView> destinationView;
    } scratch;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> flat,textured,depthFlat,depthTextured;
    ComPtr<ID3D11InputLayout> layout;
    // Preserve effective enable, write and comparison independently, including
    // inactive write/compare when depth testing is disabled.
    std::array<ComPtr<ID3D11DepthStencilState>,32> depths;
    std::array<ComPtr<ID3D11RasterizerState>,3> rasterizers;
    std::array<ComPtr<ID3D11BlendState>,16> writeMasks;
    template<class T>struct Constants {T value;ComPtr<ID3D11Buffer> buffer;};
    std::vector<Constants<PositionConstants>> positions;
    std::vector<Constants<DrawConstants>> draws;
    ComPtr<ID3D11Buffer> vertexRing,listIndices,stripIndices;
    static constexpr UINT ringBytes=sizeof(Im2DVertex)*16384;
    UINT vertexCursor=ringBytes;
    ComPtr<ID3D11DeviceContext1> batchContext;
    ComPtr<ID3DDeviceContextState> batchState;
    bool batchChecked=false;
    Im2DLayerStorage colorPreparation;
    bool prepareBatchState(ID3D11Device* device,ID3D11DeviceContext* context) {
        if(batchChecked)return bool(batchState);
        ComPtr<ID3D11Device1> device1;
        if(device->QueryInterface(IID_PPV_ARGS(&device1))==E_NOINTERFACE ||
           context->QueryInterface(IID_PPV_ARGS(&batchContext))==E_NOINTERFACE) {batchChecked=true;return false;}
        if(!device1 || !batchContext)throw Error("Native Im2D context-state interfaces are unavailable");
        const auto level=device->GetFeatureLevel();
        const UINT flags=(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
        check(device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&batchState),"batch context state creation");
        batchChecked=true;return true;
    }
    template<class T>ComPtr<ID3D11Buffer> constants(ID3D11Device* device,const T& value,std::vector<Constants<T>>& cache) {
        for(const auto& entry:cache)if(std::memcmp(&entry.value,&value,sizeof(value))==0)return entry.buffer;
        auto buffer=upload(device,&value,sizeof(value),D3D11_BIND_CONSTANT_BUFFER);
        if(cache.size()==64)cache.erase(cache.begin());
        cache.push_back({value,buffer});return buffer;
    }
    UINT writeVertices(ID3D11DeviceContext* context,std::span<const Im2DVertex> vertices,std::span<const UINT> triangleOrder={}) {
        const UINT bytes=UINT(vertices.size_bytes());
        if(bytes>ringBytes)throw Error("Native Im2D vertex packet exceeds the upload ring");
        if(!triangleOrder.empty()) {
            if(vertices.size()%3 || triangleOrder.size()!=vertices.size()/3)
                throw Error("Native Im2D reordered upload requires complete list triangles");
            for(UINT triangle:triangleOrder)if(triangle>=triangleOrder.size())
                throw Error("Native Im2D reordered upload exceeds the original vertex range");
        }
        const bool discard=vertexCursor+bytes>ringBytes;
        const UINT offset=discard?0:vertexCursor;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(vertexRing.Get(),0,discard?D3D11_MAP_WRITE_DISCARD:D3D11_MAP_WRITE_NO_OVERWRITE,0,&mapped),"vertex ring map");
        auto* output=static_cast<uint8_t*>(mapped.pData)+offset;
        if(triangleOrder.empty())std::memcpy(output,vertices.data(),bytes);
        else for(UINT triangle:triangleOrder) {
            std::memcpy(output,vertices.data()+3*triangle,3*sizeof(Im2DVertex));output+=3*sizeof(Im2DVertex);
        }
        context->Unmap(vertexRing.Get(),0);vertexCursor=offset+bytes;return offset;
    }
    void prepareScratch(ID3D11Device* device,UINT width,UINT height) {
        if(scratch.width==width && scratch.height==height) return;
        Scratch next;next.width=width;next.height=height;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R10G10B10A2_UINT;desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        check(device->CreateTexture2D(&desc,nullptr,&next.output),"integer output allocation");
        check(device->CreateRenderTargetView(next.output.Get(),nullptr,&next.outputView),"integer output view creation");
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        check(device->CreateTexture2D(&desc,nullptr,&next.destination),"integer destination allocation");
        check(device->CreateShaderResourceView(next.destination.Get(),nullptr,&next.destinationView),"integer destination view creation");
        // Publish only a complete pair. The immediate context orders every copy
        // and draw; earlier queued work retains old resources across a resize.
        scratch=std::move(next);
    }
    explicit Im2DPipeline(ID3D11Device* device) {
        D3D11_BUFFER_DESC ring{};ring.ByteWidth=ringBytes;ring.Usage=D3D11_USAGE_DYNAMIC;
        ring.BindFlags=D3D11_BIND_VERTEX_BUFFER;ring.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        check(device->CreateBuffer(&ring,nullptr,&vertexRing),"vertex ring allocation");
        std::vector<uint16_t> indices;indices.reserve(9362*3);
        for(uint16_t i=0;i<9360;++i)indices.push_back(i);
        listIndices=upload(device,indices.data(),UINT(indices.size()*sizeof(uint16_t)),D3D11_BIND_INDEX_BUFFER);
        indices.clear();
        for(uint16_t i=2;i<9362;++i) {
            indices.push_back(uint16_t(i-(i&1?1:2)));indices.push_back(uint16_t(i-(i&1?2:1)));indices.push_back(i);
        }
        stripIndices=upload(device,indices.data(),UINT(indices.size()*sizeof(uint16_t)),D3D11_BIND_INDEX_BUFFER);
        check(device->CreateVertexShader(kVSIm2DDraw,sizeof(kVSIm2DDraw),nullptr,&vertex),"vertex shader creation");
        check(device->CreatePixelShader(kPSIm2DDrawFlat,sizeof(kPSIm2DDrawFlat),nullptr,&flat),"flat shader creation");
        check(device->CreatePixelShader(kPSIm2DDrawTextured,sizeof(kPSIm2DDrawTextured),nullptr,&textured),"textured shader creation");
        check(device->CreatePixelShader(kPSIm2DDepthFlat,sizeof(kPSIm2DDepthFlat),nullptr,&depthFlat),"flat depth shader creation");
        check(device->CreatePixelShader(kPSIm2DDepthTextured,sizeof(kPSIm2DDepthTextured),nullptr,&depthTextured),"textured depth shader creation");
        const D3D11_INPUT_ELEMENT_DESC elements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0}};
        check(device->CreateInputLayout(elements,3,kVSIm2DDraw,sizeof(kVSIm2DDraw),&layout),"input layout creation");
        constexpr std::array comparisons={D3D11_COMPARISON_NEVER,D3D11_COMPARISON_LESS,
            D3D11_COMPARISON_EQUAL,D3D11_COMPARISON_LESS_EQUAL,D3D11_COMPARISON_GREATER,
            D3D11_COMPARISON_NOT_EQUAL,D3D11_COMPARISON_GREATER_EQUAL,D3D11_COMPARISON_ALWAYS};
        for(size_t i=0;i<depths.size();++i) {
            D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=(i&16)!=0;
            dd.DepthWriteMask=(i&8)?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;
            dd.DepthFunc=comparisons[i&7];
            dd.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};dd.BackFace=dd.FrontFace;
            check(device->CreateDepthStencilState(&dd,&depths[i]),"depth/stencil creation");
        }
        for(size_t i=0;i<rasterizers.size();++i) {
            D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.DepthClipEnable=TRUE;
            rd.CullMode=i==0?D3D11_CULL_NONE:D3D11_CULL_BACK;
            // cull2: back rejection + CCW front. cull6: back rejection + CW
            // front, conditional on the original negative viewport-Y scale.
            rd.FrontCounterClockwise=i==1;
            check(device->CreateRasterizerState(&rd,&rasterizers[i]),"rasterizer creation");
        }
        for(UINT mask=0;mask<writeMasks.size();++mask) {
            D3D11_BLEND_DESC bd{};auto& rt=bd.RenderTarget[0];
            rt.SrcBlend=rt.SrcBlendAlpha=D3D11_BLEND_ONE;rt.DestBlend=rt.DestBlendAlpha=D3D11_BLEND_ZERO;
            rt.BlendOp=rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;rt.RenderTargetWriteMask=UINT8(mask);
            check(device->CreateBlendState(&bd,&writeMasks[mask]),"integer target write-mask creation");
        }
    }
};

struct Im2DBatch {
    std::shared_ptr<RenderTarget> target;
    std::shared_ptr<DepthTarget> depth;
    D3D11_VIEWPORT viewport{};
    Im2DDraw draw;
    std::vector<Im2DVertex> packetVertices;
    std::array<uint32_t,14> key{};
    uint64_t packets=0;
};

void NativeBackend::flushIm2D() {
    flushIm2DBufferWrites();
    if(!pendingIm2D)return;
    requireOwner();
    auto batch=std::move(pendingIm2D);
    auto& pipeline=*im2dPipeline;
    // A separate native D3D11 context-state object isolates all binding changes.
    // This is still the same immediate command stream and real DrawIndexed.
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-swapdevicecontextstate
    struct Restore {
        ID3D11DeviceContext1* context;
        ComPtr<ID3DDeviceContextState> original;
        bool swapped=false;
        ~Restore(){if(swapped){context->ClearState();context->SwapDeviceContextState(original.Get(),nullptr);}}
    } restore{pipeline.batchContext.Get()};
    pipeline.batchContext->SwapDeviceContextState(pipeline.batchState.Get(),&restore.original);
    restore.swapped=true;
    bindTargets({batch->target,nullptr,nullptr,nullptr},batch->depth);
    setViewport(batch->viewport);
    const auto before=im2dDraws;
    try {drawIm2DImpl(batch->target,batch->depth,batch->draw,false);}
    catch(...) {
        if(im2dDraws>before)im2dDraws+=batch->packets-1;
        throw;
    }
    // Count original packets only after their triangles reached DrawIndexed.
    // Physical calls are tracked independently; neither is a frame-rate proxy.
    im2dDraws+=batch->packets-1;
    // Only CPU capacity is recycled. Release every submitted resource owner;
    // the actual D3D11 stream retains its own in-flight GPU references.
    batch->target.reset();batch->depth.reset();batch->draw.texture.reset();
    batch->draw.vertices.clear();batch->packetVertices.clear();batch->packets=0;
    spareIm2D=std::move(batch);
}
void NativeBackend::queueIm2D(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,
                             const Im2DDraw& draw) {
    try {drawIm2DImpl(target,depth,draw,true);}
    catch(...) {flushIm2D();throw;}
}
void NativeBackend::drawIm2D(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,
                            const Im2DDraw& draw) {
    flushIm2D();drawIm2DImpl(target,depth,draw,false);
}
void NativeBackend::drawIm2DImpl(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,
                               const Im2DDraw& draw,bool allowBatch) {
    validateSubmissionContext();
    if(!target || !target->texture || !target->view || target->format!=TargetFormat::RGB10A2)
        throw Error("Native Im2D requires a real RGB10A2 target");
    requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);
    const auto* attachment=attachmentDescriptor(*target);const D3D11_TEXTURE2D_DESC targetDesc=attachment?*attachment:D3D11_TEXTURE2D_DESC{};
    if(!attachment || targetDesc.Format!=DXGI_FORMAT_R10G10B10A2_UNORM ||
       targetDesc.Width!=target->pixelWidth() || targetDesc.Height!=target->pixelHeight() ||
       targetDesc.ArraySize!=1 || targetDesc.MipLevels!=1 || targetDesc.SampleDesc.Count!=1 || targetDesc.SampleDesc.Quality)
        throw Error("Native Im2D target backing exceeds the single-sample packed profile");
    if(depth) {
        // Even disabled depth must be the actual selected, owned attachment.
        validateDepthCopyTarget(depth);
        if(depth->width!=target->width || depth->height!=target->height)
            throw Error("Native Im2D selected depth dimensions differ");
    }
    if(draw.primitiveType==3) {
        if(draw.vertices.size()<3 || draw.vertices.size()>9360 || draw.vertices.size()%3)
            throw Error("Native Im2D primitive3 requires 3..9360 vertices in complete triangle-list triplets");
    } else if(draw.primitiveType==4) {
        if(draw.vertices.size()<3 || draw.vertices.size()>9362)
            throw Error("Native Im2D primitive4 requires 3..9362 triangle-strip vertices");
    } else throw Error("Native Im2D supports only original primitive3 lists and primitive4 strips");
    if(!draw.rasterWidth || !draw.rasterHeight || draw.rasterWidth>16384 || draw.rasterHeight>16384)
        throw Error("Invalid original Im2D raster dimensions");
    if(draw.stencil) throw Error("Native Im2D stencil is not implemented");
    if(draw.depthCompare>7) throw Error("Invalid effective native Im2D depth comparison");
    if(draw.depthTest && !depth) throw Error("Native Im2D enabled depth requires the actual owned depth attachment");
    if(draw.cullBits!=0 && draw.cullBits!=2 && draw.cullBits!=6)
        throw Error("Native Im2D cull bits outside 0/2/6 are unqualified");
    if(draw.colorWriteMask&~15) throw Error("Invalid native Im2D color write mask");
    if(draw.expandedBlend>1) throw Error("Invalid original Im2D expanded blend request");
    switch(draw.blendWord) {
    case 0x07060706:case 0x00010001:case 0x00010706:case 0x00010106:case 0x00010186:case 0x01000100:break;
    default: {
        char reason[256];
        int n=std::snprintf(reason,sizeof(reason),
            "Unimplemented effective native Im2D blend word=%08X primitive=%u vertices=%zu expanded=%u textured=%u depth=%u/%u/%u mask=%X",
            draw.blendWord,draw.primitiveType,draw.vertices.size(),draw.expandedBlend,bool(draw.texture),
            draw.depthTest,draw.depthWrite,draw.depthCompare,draw.colorWriteMask);
        if(n<0||size_t(n)>=sizeof(reason)) throw Error("Unimplemented Im2D blend: diagnostic truncated");
        throw Error(reason);
    }
    }
    if(draw.alphaTest && (draw.alphaCompare>7 || !std::isfinite(draw.alphaReference) || draw.alphaReference<0 || draw.alphaReference>1))
        throw Error("Invalid effective native Im2D alpha test/reference/comparison");
    for(const auto& v:draw.vertices) {
        for(size_t i=0;i<3;++i) if(!std::isfinite(v.position[i])) throw Error("Nonfinite active native Im2D position");
        if(std::abs(v.position[0])>32767 || std::abs(v.position[1])>32767 || v.position[2]<0 || v.position[2]>1)
            throw Error("Native Im2D coordinate clipping outside the qualified range");
        for(float c:v.color) if(!std::isfinite(c)) throw Error("Nonfinite active native Im2D color");
        if(draw.expandedBlend)for(float c:v.color)if(c<0 || c>1)
            throw Error("Expanded native Im2D requires normalized vertex colors");
        if(draw.texture) for(float uv:v.uv) if(!std::isfinite(uv)) throw Error("Nonfinite active native Im2D UV");
        // RHW is ignored by the original VS, and flat UV is inactive.
    }
    const auto selectedViewport=viewport();
    if(!selectedViewport) throw Error("Native Im2D requires one actual selected viewport");
    const auto logicalViewport=*selectedViewport;
    const auto originalVp=renderViewport(target,logicalViewport);
    const auto vp=draw.preserveAspect?contentViewport(target,logicalViewport):originalVp;
    if(draw.depthTest && (vp.MinDepth!=0 || vp.MaxDepth!=1))
        throw Error("Native Im2D enabled depth requires the qualified native viewport depth range 0..1");
    // No viewport substitution. Native clipping handles overhanging geometry;
    // original coverage qualification remains the caller's responsibility.
    // Scissor, MSAA, depth bias and alternate clip/round modes are excluded.
    if(vp.Width<=0 || vp.Height<=0 || vp.TopLeftX<0 || vp.TopLeftY<0 || vp.TopLeftX+vp.Width>float(target->pixelWidth()) ||
       vp.TopLeftY+vp.Height>float(target->pixelHeight()))
        throw Error("Native Im2D viewport extends beyond the selected target");
    ComPtr<ID3D11Predicate> predicate;BOOL predicateValue{};context->GetPredication(&predicate,&predicateValue);
    if(predicate) throw Error("Native Im2D cannot execute under native predication");
    std::array<ID3D11Buffer*,D3D11_SO_BUFFER_SLOT_COUNT> streamOutput{};
    context->SOGetTargets(UINT(streamOutput.size()),streamOutput.data());bool hasStreamOutput=false;
    for(auto* buffer:streamOutput) if(buffer){hasStreamOutput=true;buffer->Release();}
    if(hasStreamOutput) throw Error("Native Im2D stream-output bindings are unsupported");
    std::array<ID3D11UnorderedAccessView*,64> uavs{};
    context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,featureLevel>=D3D_FEATURE_LEVEL_11_1?64:8,uavs.data());
    bool hasUav=false;for(auto* uav:uavs) if(uav){hasUav=true;uav->Release();}
    if(hasUav) throw Error("Native Im2D output UAV bindings are unsupported");
    ComPtr<ID3D11SamplerState> sampler;
    if(draw.texture) {
        validateTexture(draw.texture);validateSampler(draw.sampler);
        // BC2/BC3 use the existing immutable, owned UNORM upload and native
        // sampling. Multi-level chains are complete immutable uploads sampled
        // with hardware LOD like the original. No CPU expansion,
        // alpha-association conversion or mip inference; see
        // native-im2d-compressed-textures.md for the bounded sampling fixtures
        // and original compression-precision limits.
        if(draw.texture->format!=TextureFormat::RGBA8 &&
           draw.texture->format!=TextureFormat::BGRX8 && draw.texture->format!=TextureFormat::BC2 &&
           draw.texture->format!=TextureFormat::BC3)
            throw Error("Native Im2D supports only RGBA8/BGRX8/BC2/BC3 textures");
        // Reuse the backend scene sampler cache; validation above precedes
        // lookup. Removes one driver allocation per textured packet for
        // steadier gameplay/HUD frame times.
        sampler=sceneSamplerState(draw.sampler);
    }
    if(!im2dPipeline) im2dPipeline=std::make_shared<Im2DPipeline>(device.Get());
    auto& pipeline=*im2dPipeline;
    const size_t triangles=draw.primitiveType==3?draw.vertices.size()/3:draw.vertices.size()-2;
    // Immutable backing cannot change between acceptance and submission. A
    // writable texture still draws immediately, before its next upload. Color
    // dependencies use the same conservative ordered layers across packets.
    if(allowBatch && (!draw.texture || !draw.texture->writable) &&
       triangles<=3120 && pipeline.prepareBatchState(device.Get(),context.Get())) {
        const std::array<uint32_t,14> key={draw.rasterWidth,draw.rasterHeight,draw.blendWord,draw.expandedBlend,
            draw.alphaTest,std::bit_cast<uint32_t>(draw.alphaReference),draw.alphaCompare,draw.cullBits,
            draw.pixelCenterHalf,draw.colorWriteMask,draw.depthTest,uint32_t(draw.depthWrite)|(uint32_t(draw.reverseDepth)<<1),draw.depthCompare,draw.preserveAspect};
        const bool compatible=pendingIm2D && pendingIm2D->target->texture.Get()==target->texture.Get() &&
            (pendingIm2D->depth?pendingIm2D->depth->texture.Get():nullptr)==(depth?depth->texture.Get():nullptr) &&
            (pendingIm2D->draw.texture?pendingIm2D->draw.texture->texture.Get():nullptr)==(draw.texture?draw.texture->texture.Get():nullptr) &&
            (pendingIm2D->draw.texture?pendingIm2D->draw.texture->view.Get():nullptr)==(draw.texture?draw.texture->view.Get():nullptr) &&
            (!draw.texture || std::memcmp(&pendingIm2D->draw.sampler,&draw.sampler,sizeof(draw.sampler))==0) &&
            pendingIm2D->key==key && std::memcmp(&pendingIm2D->viewport,&logicalViewport,sizeof(logicalViewport))==0 &&
            pendingIm2D->draw.vertices.size()+triangles*3<=9360;
        if(!compatible)flushIm2D();
        if(!pendingIm2D) {
            auto next=std::move(spareIm2D);
            if(!next)next=std::make_shared<Im2DBatch>();
            // Freeze wrapper metadata as well as the underlying COM ownership.
            next->target=std::make_shared<RenderTarget>(*target);
            if(depth)next->depth=std::make_shared<DepthTarget>(*depth);
            next->viewport=logicalViewport;next->key=key;
            // Copy every scalar through the complete packet assignment while
            // retaining separate bounded capacities for its original vertices
            // and the expanded batch. Reuse must never borrow caller storage.
            next->draw.vertices.swap(next->packetVertices);
            next->draw=draw;
            if(draw.texture)next->draw.texture=std::make_shared<Texture>(*draw.texture);
            next->draw.vertices.swap(next->packetVertices);
            next->draw.primitiveType=3;next->draw.vertices.clear();next->draw.vertices.reserve(9360);
            pendingIm2D=std::move(next);
        }
        auto& batch=*pendingIm2D;
        if(draw.primitiveType==3)batch.draw.vertices.insert(batch.draw.vertices.end(),draw.vertices.begin(),draw.vertices.end());
        else for(size_t i=2;i<draw.vertices.size();++i) {
            batch.draw.vertices.push_back(draw.vertices[i-(i&1?1:2)]);
            batch.draw.vertices.push_back(draw.vertices[i-(i&1?2:1)]);
            batch.draw.vertices.push_back(draw.vertices[i]);
        }
        ++batch.packets;requireOwner();return;
    }
    if(allowBatch)flushIm2D();
    const PositionConstants position{2.0f/float(draw.rasterWidth),2.0f/float(draw.rasterHeight)};
    const DrawConstants values{uint32_t(draw.reverseDepth),0,draw.alphaTest?draw.alphaReference:0.0f,
        uint32_t(draw.alphaTest),draw.alphaTest?draw.alphaCompare:7u,draw.blendWord};
    const UINT indexCount=draw.primitiveType==3?UINT(draw.vertices.size()):UINT((draw.vertices.size()-2)*3);
    const auto& indexBuffer=draw.primitiveType==3?pipeline.listIndices:pipeline.stripIndices;
    auto positionBuffer=pipeline.constants(device.Get(),position,pipeline.positions);
    auto drawBuffer=pipeline.constants(device.Get(),values,pipeline.draws);
    // Original formats2/10 share these persistent bits. The retained expansion
    // request changes no resource identity or stored codes. Both modes use the
    // declared native float calculation, followed by quantization after EVERY
    // covered triangle; this does not assert identical console intermediates.
    const bool depthOnly=draw.blendWord==0x01000100 || draw.colorWriteMask==0;
    const bool readsDestination=!depthOnly && draw.blendWord!=0x00010001;
    // Only pixels inside the primitive's conservative screen bounds can read
    // or modify color. Copy packed bits at the same coordinates, including a
    // two-pixel margin for float transform/raster edge rounding. Overhanging
    // vertices expand to the target edge; the viewport and rasterizer still
    // perform actual clipping. Empty bounds conservatively use the full target.
    const auto colorBounds=[&](std::span<const Im2DVertex> vertices) {
        double minX=double(target->pixelWidth()),minY=double(target->pixelHeight()),maxX=0,maxY=0;
        for(const auto& v:vertices) {
            const double x=double(vp.TopLeftX)+(double(v.position[0])-0.5)*double(vp.Width)/draw.rasterWidth+(draw.pixelCenterHalf?0:0.5);
            const double y=double(vp.TopLeftY)+(double(v.position[1])-0.5)*double(vp.Height)/draw.rasterHeight+(draw.pixelCenterHalf?0:0.5);
            minX=std::min(minX,x);minY=std::min(minY,y);maxX=std::max(maxX,x);maxY=std::max(maxY,y);
        }
        D3D11_BOX box{UINT(std::clamp(std::floor(minX)-2,0.0,double(target->pixelWidth()))),
            UINT(std::clamp(std::floor(minY)-2,0.0,double(target->pixelHeight()))),0,
            UINT(std::clamp(std::ceil(maxX)+2,0.0,double(target->pixelWidth()))),
            UINT(std::clamp(std::ceil(maxY)+2,0.0,double(target->pixelHeight()))),1};
        if(box.left>=box.right || box.top>=box.bottom)box={0,0,0,target->pixelWidth(),target->pixelHeight(),1};
        return box;
    };
    auto& preparation=pipeline.colorPreparation;
    if(preparation.active)throw Error("Reentrant native Im2D color preparation");
    struct FinishPreparation {
        bool& active;
        ~FinishPreparation(){active=false;}
    } finishPreparation{preparation.active};
    preparation.active=true;preparation.beginPacket();
    D3D11_BOX packetBounds{};auto& triangleBounds=preparation.triangles;
    if(!depthOnly)packetBounds=colorBounds(draw.vertices);
    if(readsDestination) {
        triangleBounds.reserve(indexCount/3);
        for(UINT first=0;first<indexCount;first+=3) {
            const UINT i=first/3+2;
            const std::array vertices=draw.primitiveType==3?
                std::array{draw.vertices[first],draw.vertices[first+1],draw.vertices[first+2]}:
                std::array{draw.vertices[i-(i&1?1:2)],draw.vertices[i-(i&1?2:1)],draw.vertices[i]};
            triangleBounds.push_back(colorBounds(vertices));
        }
    }
    auto& colorLayers=preparation.layers;
    auto& colorOrder=preparation.order;
    if(readsDestination) {
        // A pixel can depend only on earlier triangles whose conservative
        // bounds touch its tile. Assign ordered layers from those dependencies.
        // Triangles in one layer have disjoint bounds, so they may share one
        // destination snapshot. This preserves color AND depth order at every
        // potentially covered pixel, including alpha-discarded fragments.
        preparation.prepare(target->pixelWidth(),target->pixelHeight());
    }
    // The established layer order already determines the submission order.
    // Store complete list triplets in that exact order in the separate native
    // decoded stream, so every layer has one contiguous index range. Original
    // raw GPU storage, triplet winding and each vertex's bits remain unchanged.
    const bool compactColor=readsDestination && draw.primitiveType==3 &&
        !std::is_sorted(colorOrder.begin(),colorOrder.end());
    if(!depthOnly)pipeline.prepareScratch(device.Get(),targetDesc.Width,targetDesc.Height);
    const auto& packedOutput=pipeline.scratch.output;
    const auto& destination=pipeline.scratch.destination;
    const auto& packedView=pipeline.scratch.outputView;
    const auto& destinationView=pipeline.scratch.destinationView;
    std::array<ComPtr<ID3D11ShaderResourceView>,2> savedTextures;
    context->PSGetShaderResources(0,1,&savedTextures[0]);context->PSGetShaderResources(1,1,&savedTextures[1]);
    ComPtr<ID3D11SamplerState> savedSampler;context->PSGetSamplers(0,1,&savedSampler);
    requireOwner();
    const UINT vertexOffset=pipeline.writeVertices(context.Get(),draw.vertices,
        compactColor?std::span<const UINT>(colorOrder):std::span<const UINT>{});
    // All validation and fallible preparation precede any rendering/context
    // mutation. Ring writes use previously unused bytes, or WRITE_DISCARD to
    // obtain fresh storage while D3D11 retains data used by earlier draws.
    // https://learn.microsoft.com/en-us/windows/win32/direct3d11/how-to--use-dynamic-resources
    // The pipeline is single-owner; callers may release/mutate their packet on
    // return without changing any queued vertex, constant or texture references.
    context->OMSetBlendState(pipeline.writeMasks[draw.colorWriteMask].Get(),nullptr,0xFFFFFFFF);
    const size_t depthIndex=(draw.depthTest?16u:0u)|(draw.depthWrite?8u:0u)|draw.depthCompare;
    context->OMSetDepthStencilState(pipeline.depths[depthIndex].Get(),0);
    context->RSSetState(pipeline.rasterizers[draw.cullBits==0?0:(draw.cullBits==2?1:2)].Get());
    if(!draw.pixelCenterHalf) {
        auto centered=vp;centered.TopLeftX+=0.5f;centered.TopLeftY+=0.5f;context->RSSetViewports(1,&centered);
    } else context->RSSetViewports(1,&vp);
    ID3D11Buffer* stream=pipeline.vertexRing.Get();UINT stride=sizeof(Im2DVertex),offset=vertexOffset;
    context->IASetVertexBuffers(0,1,&stream,&stride,&offset);context->IASetIndexBuffer(indexBuffer.Get(),DXGI_FORMAT_R16_UINT,0);
    context->IASetInputLayout(pipeline.layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(pipeline.vertex.Get(),nullptr,0);
    context->PSSetShader(depthOnly?(draw.texture?pipeline.depthTextured.Get():pipeline.depthFlat.Get()):
        (draw.texture?pipeline.textured.Get():pipeline.flat.Get()),nullptr,0);
    context->GSSetShader(nullptr,nullptr,0);context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);
    ID3D11Buffer* cb=positionBuffer.Get();context->VSSetConstantBuffers(0,1,&cb);
    cb=drawBuffer.Get();context->VSSetConstantBuffers(1,1,&cb);context->PSSetConstantBuffers(1,1,&cb);
    ID3D11ShaderResourceView* texture=draw.texture?draw.texture->view.Get():nullptr;
    context->PSSetShaderResources(0,1,&texture);auto* sample=sampler.Get();context->PSSetSamplers(0,1,&sample);
    ID3D11ShaderResourceView* nullView=nullptr;
    auto* output=packedView.Get();auto* sampled=destinationView.Get();
    const auto copyBounds=[&](ID3D11Texture2D* to,ID3D11Texture2D* from,const D3D11_BOX& bounds) {
        context->CopySubresourceRegion(to,0,bounds.left,bounds.top,0,from,0,&bounds);
        ++im2dColorCopies;
    };
    if(depthOnly)context->OMSetRenderTargets(0,nullptr,depth?depth->view.Get():nullptr);
    else {
        copyBounds(packedOutput.Get(),target->texture.Get(),packetBounds);
        context->OMSetRenderTargets(1,&output,depth?depth->view.Get():nullptr);
    }
    // The selected DSV stays attached to every triangle. Native depth testing
    // and writes therefore see preceding triangles' depth, just as the packed
    // color copy sees preceding color. Shader discard gates both outputs.
    bool submitted=false;
    if(!readsDestination) {
        // D3D11 depth read/modify/write preserves primitive order within a draw.
        // No shader reads destination color, so triangles need no copy barriers.
        context->DrawIndexed(indexCount,0,0);++im2dNativeDraws;++im2dDraws;
    } else for(const auto& layer:colorLayers) {
        context->PSSetShaderResources(1,1,&nullView);
        copyBounds(destination.Get(),packedOutput.Get(),layer.bounds);
        context->PSSetShaderResources(1,1,&sampled);
        if(compactColor) {
            context->DrawIndexed(3*layer.count,3*layer.first,0);++im2dNativeDraws;
            if(!submitted){++im2dDraws;submitted=true;}
        } else for(UINT at=layer.first;at<layer.first+layer.count;) {
            const UINT first=colorOrder[at++];UINT count=1;
            while(at<layer.first+layer.count && colorOrder[at]==first+count){++at;++count;}
            context->DrawIndexed(3*count,3*first,0);++im2dNativeDraws;
            if(!submitted){++im2dDraws;submitted=true;}
        }
    }
    context->PSSetShaderResources(1,1,&nullView);
    output=target->view.Get();context->OMSetRenderTargets(1,&output,depth?depth->view.Get():nullptr);
    if(!depthOnly)copyBounds(target->texture.Get(),packedOutput.Get(),packetBounds);
    context->RSSetViewports(1,&originalVp);
    for(UINT i=0;i<2;++i){auto* restored=savedTextures[i].Get();context->PSSetShaderResources(i,1,&restored);}
    sample=savedSampler.Get();context->PSSetSamplers(0,1,&sample);
    // The native service represents the temporary fixed-function shader
    // lifetime in original823F4B60/823F4970. Original cached explicit VS/PS
    // remain zero, and the actual temporary shaders are nulled after drawing.
    // GPU execution retains its references independently of these bindings.
    context->VSSetShader(nullptr,nullptr,0);context->PSSetShader(nullptr,nullptr,0);
    stream=nullptr;stride=offset=0;context->IASetVertexBuffers(0,1,&stream,&stride,&offset);
    context->IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);
    cb=nullptr;context->VSSetConstantBuffers(0,1,&cb);context->VSSetConstantBuffers(1,1,&cb);context->PSSetConstantBuffers(1,1,&cb);
    requireOwner(); // Device removal after submission remains a counted failure.
}
}
