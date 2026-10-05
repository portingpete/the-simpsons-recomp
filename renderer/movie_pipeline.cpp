#include "native_backend.h"
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include "VSTextured.h"
#include "PSMovie.h"
#include "PSMoviePacked.h"

namespace Simpsons::Graphics {
namespace {
void check(HRESULT result,const char* operation) {
    if(FAILED(result)) {
        char message[180];std::snprintf(message,sizeof(message),"Native movie %s failed: 0x%08lX",operation,ULONG(result));
        throw Error(message);
    }
}
bool same(float a,float b) {return std::bit_cast<uint32_t>(a)==std::bit_cast<uint32_t>(b);}
void validateRectangle(const std::array<ScreenVertex,4>& vertices) {
    // Original primitive8 expansion, in the original bottom-left, bottom-right,
    // top-left, top-right order. VSTextured passes clip XY and UV through.
    // Endpoint copies preserve the literal 0.9 bits (movie_geometry.cpp).
    for(size_t i=0;i<vertices.size();++i) {
        const auto& v=vertices[i];
        if(!same(v.x,(i&1)?1.0f:-1.0f) || !same(v.y,(i&2)?1.0f:-1.0f) ||
           !std::isfinite(v.u) || !std::isfinite(v.v))
            throw Error("Native movie requires the original full clip-space rectangle");
    }
    const auto& a=vertices[0];const auto& b=vertices[1];const auto& c=vertices[2];const auto& d=vertices[3];
    if(!same(a.u,c.u) || !same(b.u,d.u) || !same(a.v,b.v) || !same(c.v,d.v))
        throw Error("Native movie UV edges do not complete the original rectangle");
    const bool fullU=same(a.u,0.0f)&&same(b.u,1.0f),fullV=same(a.v,0.0f)&&same(c.v,1.0f);
    const bool cropU=same(a.u,0.125f)&&same(b.u,0.875f);
    const bool cropV=same(a.v,std::bit_cast<float>(uint32_t{0x3DCCCCCD}))&&
                     same(c.v,std::bit_cast<float>(uint32_t{0x3F666666}));
    if(!((fullU&&fullV)||(cropU&&fullV)||(fullU&&cropV)))
        throw Error("Native movie UV range is not an original rectangle profile");
}
void validateSampler(const D3D11_SAMPLER_DESC& s) {
    // A single level makes the retained point/linear mip request inactive.
    // Minification/magnification are always linear in original8282E3E8.
    if((s.Filter!=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT && s.Filter!=D3D11_FILTER_MIN_MAG_MIP_LINEAR) ||
       s.MipLODBias!=0 || s.MinLOD!=0 || !std::isfinite(s.MaxLOD) || s.MaxLOD<0 ||
       s.MaxAnisotropy!=1 || s.ComparisonFunc!=D3D11_COMPARISON_NEVER)
        throw Error("Native movie sampler filter/LOD/comparison is unsupported");
    for(auto address:{s.AddressU,s.AddressV,s.AddressW})
        if(address!=D3D11_TEXTURE_ADDRESS_WRAP && address!=D3D11_TEXTURE_ADDRESS_CLAMP)
            throw Error("Native movie requires wrap/clamp addressing");
    for(float component:s.BorderColor) if(!std::isfinite(component))
        throw Error("Native movie sampler border must be finite");
}
void validateSideEffects(ID3D11DeviceContext* context,D3D_FEATURE_LEVEL level) {
    ComPtr<ID3D11Predicate> predicate;BOOL value{};context->GetPredication(&predicate,&value);
    if(predicate) throw Error("Native movie cannot execute under native predication");
    std::array<ID3D11Buffer*,D3D11_SO_BUFFER_SLOT_COUNT> streamOutput{};
    context->SOGetTargets(UINT(streamOutput.size()),streamOutput.data());bool hasStreamOutput=false;
    for(auto* buffer:streamOutput) if(buffer){hasStreamOutput=true;buffer->Release();}
    if(hasStreamOutput) throw Error("Native movie stream-output bindings are unsupported");
    std::array<ID3D11UnorderedAccessView*,64> uavs{};
    context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,level>=D3D_FEATURE_LEVEL_11_1?64:8,uavs.data());
    bool hasUav=false;for(auto* uav:uavs) if(uav){hasUav=true;uav->Release();}
    if(hasUav) throw Error("Native movie output UAV bindings are unsupported");
}
static_assert(sizeof(ScreenVertex)==16 && offsetof(ScreenVertex,u)==8);
}

struct MoviePipeline {
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel,packedPixel;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11RasterizerState> rasterizer;
    ComPtr<ID3D11BlendState> blend;
    explicit MoviePipeline(ID3D11Device* device) {
        // Shader arithmetic belongs exclusively to the separately qualified
        // original VS/PS artifacts. See tools/analyze_movie_shader.py.
        check(device->CreateVertexShader(kVSTextured,sizeof(kVSTextured),nullptr,&vertex),"vertex shader creation");
        check(device->CreatePixelShader(kPSMovie,sizeof(kPSMovie),nullptr,&pixel),"pixel shader creation");
        check(device->CreatePixelShader(kPSMoviePacked,sizeof(kPSMoviePacked),nullptr,&packedPixel),"packed pixel shader creation");
        const D3D11_INPUT_ELEMENT_DESC elements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}};
        check(device->CreateInputLayout(elements,2,kVSTextured,sizeof(kVSTextured),&layout),"input layout creation");
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        check(device->CreateRasterizerState(&rd,&rasterizer),"filled unculled rasterizer creation");
        D3D11_BLEND_DESC bd{};auto& rt=bd.RenderTarget[0];
        rt.SrcBlend=rt.SrcBlendAlpha=D3D11_BLEND_ONE;rt.DestBlend=rt.DestBlendAlpha=D3D11_BLEND_ZERO;
        rt.BlendOp=rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        check(device->CreateBlendState(&bd,&blend),"integer replace blend creation");
    }
};

void NativeBackend::drawMovie(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,
                             const MovieDraw& draw) {
    flushIm2D();
    validateSubmissionContext();
    // Value/ownership snapshots only: neither guest pointers nor app/RW/stream
    // cache words enter this service. Immutable upload captures all four verts.
    const auto outputOwner=target;const auto depthOwner=depth;const MovieDraw owned=draw;
    if(movieDraws==UINT64_MAX)throw Error("Native movie draw counter exhausted");
    validateFrontTarget(outputOwner);validateDepthCopyTarget(depthOwner);
    if(depthOwner->width!=outputOwner->width || depthOwner->height!=outputOwner->height)
        throw Error("Native movie color/depth dimensions differ");
    requireSelectedTargets({outputOwner,nullptr,nullptr,nullptr},depthOwner);
    validateRectangle(owned.vertices);
    if(owned.retainedDepthCompare>7) throw Error("Native movie retained depth comparison is invalid");
    const auto selectedViewport=viewport();
    if(!selectedViewport || selectedViewport->TopLeftX!=0 || selectedViewport->TopLeftY!=0 ||
       selectedViewport->Width!=float(outputOwner->width) || selectedViewport->Height!=float(outputOwner->height) ||
       selectedViewport->MinDepth!=0 || selectedViewport->MaxDepth!=1)
        throw Error("Native movie requires the actual full target viewport with native depth zero to one");
    validateSideEffects(context.Get(),featureLevel);
    for(size_t i=0;i<owned.textures.size();++i) {
        validateTextureStorage(owned.textures[i]);validateSampler(owned.samplers[i]);
        if(owned.textures[i]->format!=TextureFormat::R8 || owned.textures[i]->levelCount()!=1)
            throw Error("Native movie requires three single-level scalar R8 planes");
        for(size_t j=0;j<i;++j) if(owned.textures[i]->texture.Get()==owned.textures[j]->texture.Get())
            throw Error("Native movie planes must have independent storage");
    }
    const auto& y=*owned.textures[0];const auto& cr=*owned.textures[1];const auto& cb=*owned.textures[2];
    if(y.width!=2*cr.width || y.height!=2*cr.height || cr.width!=cb.width || cr.height!=cb.height)
        throw Error("Native movie requires luma and two matching half-sized chroma planes");

    // All validation precedes allocation, and all fallible preparation precedes
    // any context command or publication of a new cached pipeline.
    std::array<ComPtr<ID3D11SamplerState>,3> samplers;
    for(size_t i=0;i<samplers.size();++i)
        check(device->CreateSamplerState(&owned.samplers[i],&samplers[i]),"sampler creation");
    // The original movie setter disables depth testing without replacing the
    // inherited write/compare requests. D3D11 ignores these while disabled,
    // but keep them in the requested state for the next original state change.
    D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=FALSE;
    dd.DepthWriteMask=owned.retainedDepthWrite?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc=D3D11_COMPARISON_FUNC(owned.retainedDepthCompare+1);
    dd.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};dd.BackFace=dd.FrontFace;
    ComPtr<ID3D11DepthStencilState> disabledDepth;
    check(device->CreateDepthStencilState(&dd,&disabledDepth),"disabled retained depth state creation");
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(owned.vertices);bd.Usage=D3D11_USAGE_IMMUTABLE;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    const D3D11_SUBRESOURCE_DATA initial{owned.vertices.data(),0,0};ComPtr<ID3D11Buffer> vertices;
    check(device->CreateBuffer(&bd,&initial,&vertices),"owned vertex upload");
    D3D11_TEXTURE2D_DESC packedDesc{};outputOwner->texture->GetDesc(&packedDesc);
    packedDesc.Format=DXGI_FORMAT_R10G10B10A2_UINT;packedDesc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> packed;ComPtr<ID3D11RenderTargetView> packedView;
    check(device->CreateTexture2D(&packedDesc,nullptr,&packed),"integer output allocation");
    check(device->CreateRenderTargetView(packed.Get(),nullptr,&packedView),"integer output view creation");
    auto prepared=moviePipeline?moviePipeline:std::make_shared<MoviePipeline>(device.Get());
    ComPtr<ID3D11Buffer> priorStream;UINT priorStride{},priorOffset{};
    context->IAGetVertexBuffers(0,1,&priorStream,&priorStride,&priorOffset);
    requireOwner();
    // All fallible preparation is complete. A real movie binding replaces any
    // earlier scene/screen transaction; a rejected frame above leaves it live.
    invalidateScreenReplacement();
    moviePipeline=prepared;const auto& pipeline=*prepared;

    // Same compatible packed-family bit transfer as drawOriginalScreen. The
    // integer shader helper declares native pack precision; console raster,
    // filtering and arithmetic/quantization parity remain unproven.
    const auto originalViewport=renderViewport(outputOwner,*selectedViewport);
    const auto movieViewport=contentViewport(outputOwner,*selectedViewport);
    if(movieViewport.Width!=originalViewport.Width||movieViewport.Height!=originalViewport.Height) {
        const float black[4]={0,0,0,1};context->ClearRenderTargetView(outputOwner->view.Get(),black);
    }
    context->CopyResource(packed.Get(),outputOwner->texture.Get());
    auto* output=packedView.Get();context->OMSetRenderTargets(1,&output,depthOwner->view.Get());
    context->OMSetBlendState(pipeline.blend.Get(),nullptr,0xFFFFFFFF);
    context->OMSetDepthStencilState(disabledDepth.Get(),0);
    context->RSSetState(pipeline.rasterizer.Get());
    context->RSSetViewports(1,&movieViewport);
    ID3D11Buffer* stream=vertices.Get();UINT stride=sizeof(ScreenVertex),offset=0;
    context->IASetVertexBuffers(0,1,&stream,&stride,&offset);
    context->IASetInputLayout(pipeline.layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->VSSetShader(pipeline.vertex.Get(),nullptr,0);context->PSSetShader(pipeline.packedPixel.Get(),nullptr,0);
    context->GSSetShader(nullptr,nullptr,0);context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);
    std::array<ID3D11ShaderResourceView*,3> views{};std::array<ID3D11SamplerState*,3> samples{};
    for(size_t i=0;i<views.size();++i){views[i]=owned.textures[i]->view.Get();samples[i]=samplers[i].Get();}
    context->PSSetShaderResources(0,UINT(views.size()),views.data());context->PSSetSamplers(0,UINT(samples.size()),samples.data());
    context->Draw(4,0);++movieDraws;

    // Original movie end unbinds only PS textures0..2. Retain the selected
    // float PS, VS, declaration and samplers; the integer helper is transient.
    views.fill(nullptr);context->PSSetShaderResources(0,UINT(views.size()),views.data());
    output=outputOwner->view.Get();context->OMSetRenderTargets(1,&output,depthOwner->view.Get());
    context->CopyResource(outputOwner->texture.Get(),packed.Get());
    context->RSSetViewports(1,&originalViewport);
    context->PSSetShader(pipeline.pixel.Get(),nullptr,0);
    stream=priorStream.Get();context->IASetVertexBuffers(0,1,&stream,&priorStride,&priorOffset);
    // The original viewport is restored. Scissor rectangles, index buffer,
    // other streams, constants, resource/sampler slots and predicate stay intact.
    // D3D11 retains resources for queued GPU use after these local owners die.
    requireOwner(); // A post-Draw device failure remains counted.
    completedOriginalMovieDraw=movieDraws;
}
NativeScreenReplacementReceipt NativeBackend::completedMovieReplacement(uint64_t before) const {
    requireOwner();
    if(before==UINT64_MAX||movieDraws!=before+1||completedOriginalMovieDraw!=movieDraws||!moviePipeline)
        throw Error("Native movie replacement has no single completed original frame");
    NativeScreenReplacementReceipt receipt;receipt.owner=this;receipt.draw=screenDraws;receipt.epoch=screenShaderEpoch;
    receipt.movie=movieDraws;receipt.movieQueryCount=coronaQueryDraws;receipt.vertex=0x82152880;receipt.pixel=0x82152B68;
    receipt.retainedVertex=moviePipeline->vertex;receipt.retainedPixel=moviePipeline->pixel;
    receipt.retainedMovieLayout=moviePipeline->layout;
    requireScreenReplacement(receipt);return receipt;
}
void NativeBackend::requireMovieReplacement(const NativeScreenReplacementReceipt& receipt) const {
    if(!moviePipeline||receipt.retainedVertex.Get()!=moviePipeline->vertex.Get()||
       receipt.retainedPixel.Get()!=moviePipeline->pixel.Get()||
       !receipt.retainedMovieLayout||receipt.retainedMovieLayout.Get()!=moviePipeline->layout.Get())
        throw Error("Native movie replacement lost its completed pipeline owner");
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;UINT vertexClasses=0,pixelClasses=0;
    context->VSGetShader(&vertex,nullptr,&vertexClasses);context->PSGetShader(&pixel,nullptr,&pixelClasses);
    if(vertexClasses||pixelClasses||vertex.Get()!=receipt.retainedVertex.Get()||pixel.Get()!=receipt.retainedPixel.Get())
        throw Error("Actual native movie replacement bindings differ from their completed frame");
    ComPtr<ID3D11InputLayout> layout;context->IAGetInputLayout(&layout);
    if(layout.Get()!=receipt.retainedMovieLayout.Get()&&(!completedScreenBatchRetirement||layout))
        throw Error("Actual native movie replacement layout differs from its completed frame");
}
}
