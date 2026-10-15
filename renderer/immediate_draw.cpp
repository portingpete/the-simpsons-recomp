#include "immediate_draw.h"
#include "immediate_copy_region.h"
#include "temporal_scene.h"
#include "VSImmediate.h"
#include "PSImmediateDraw.h"
#include "PSImmediateDualDraw.h"
#include "PSImmediateUNormDraw.h"
#include "PSImmediateDualUNormDraw.h"
#include "PSImmediateSnapshotDraw.h"
#include "VSRadial.h"
#include "PSRadialDraw.h"
#include "PSRadialUNormDraw.h"
#include "VSImmediateProjected.h"
#include "PSImmediateProjectedDraw.h"
#include "PSImmediateProjectedDualDraw.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace Simpsons::Graphics {
namespace {
void need(bool ok,const char* why){if(!ok)throw Error(why);}
void check(HRESULT hr,const char* why){if(FAILED(hr)){char text[180];std::snprintf(text,sizeof(text),"Native immediate %s: %08lX",why,ULONG(hr));throw Error(text);}}
ComPtr<ID3D11Buffer> upload(ID3D11Device* device,const void* bytes,UINT size,UINT bind){
    D3D11_BUFFER_DESC d{};d.ByteWidth=size;d.BindFlags=bind;d.Usage=D3D11_USAGE_IMMUTABLE;
    const D3D11_SUBRESOURCE_DATA initial{bytes,0,0};ComPtr<ID3D11Buffer> result;
    check(device->CreateBuffer(&d,&initial,&result),"owned upload");return result;
}
// Direct immediate draws bypass the cached material path. Restore the actual
// bindings so its retained native owners remain valid after submission.
struct Restore {
    ID3D11DeviceContext* c;
    ComPtr<ID3D11DeviceContext1> ranged;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> vb,ib;ComPtr<ID3D11InputLayout> layout;
    detail::ConstantBufferBinding vc,projection,pc1,pc2;
    ComPtr<ID3D11ShaderResourceView> textures[4];ComPtr<ID3D11SamplerState> samplers[3];
    ComPtr<ID3D11DepthStencilState> depth;ComPtr<ID3D11BlendState> blend;ComPtr<ID3D11RasterizerState> raster;
    std::array<ComPtr<ID3D11RenderTargetView>,8> targets;ComPtr<ID3D11DepthStencilView> dsv;
    std::array<D3D11_VIEWPORT,16> viewports{};D3D11_PRIMITIVE_TOPOLOGY topology{};DXGI_FORMAT format{};
    UINT stride{},offset{},indexOffset{},stencil{},sampleMask{},viewportCount{16};FLOAT factors[4]{};
    Restore(ID3D11DeviceContext* context,ComPtr<ID3D11DeviceContext1> rangedContext):c(context),ranged(std::move(rangedContext)){
        c->VSGetShader(&vs,nullptr,nullptr);c->PSGetShader(&ps,nullptr,nullptr);
        vc=detail::captureConstantBuffer(c,ranged.Get(),detail::ConstantBufferStage::Vertex,0);
        pc1=detail::captureConstantBuffer(c,ranged.Get(),detail::ConstantBufferStage::Pixel,1);
        pc2=detail::captureConstantBuffer(c,ranged.Get(),detail::ConstantBufferStage::Pixel,2);
        projection=detail::captureConstantBuffer(c,ranged.Get(),detail::ConstantBufferStage::Vertex,3);
        c->IAGetVertexBuffers(0,1,&vb,&stride,&offset);c->IAGetIndexBuffer(&ib,&format,&indexOffset);
        c->IAGetInputLayout(&layout);c->IAGetPrimitiveTopology(&topology);
        // Each non-null Get result owns one reference, adopted without another
        // AddRef. Preserve holes and the exact slots in two context calls.
        std::array<ID3D11ShaderResourceView*,4> views{};
        std::array<ID3D11SamplerState*,3> samples{};
        c->PSGetShaderResources(0,UINT(views.size()),views.data());
        c->PSGetSamplers(0,UINT(samples.size()),samples.data());
        for(size_t i=0;i<views.size();++i)textures[i].Attach(views[i]);
        for(size_t i=0;i<samples.size();++i)samplers[i].Attach(samples[i]);
        c->OMGetDepthStencilState(&depth,&stencil);c->OMGetBlendState(&blend,factors,&sampleMask);c->RSGetState(&raster);
        c->RSGetViewports(&viewportCount,viewports.data());
        std::array<ID3D11RenderTargetView*,8> raw{};c->OMGetRenderTargets(8,raw.data(),&dsv);
        for(size_t i=0;i<raw.size();++i)targets[i].Attach(raw[i]);
    }
    ~Restore(){
        ID3D11ShaderResourceView* nulls[4]{};c->PSSetShaderResources(0,4,nulls);
        std::array<ID3D11RenderTargetView*,8> raw{};for(size_t i=0;i<raw.size();++i)raw[i]=targets[i].Get();
        c->OMSetRenderTargets(8,raw.data(),dsv.Get());
        c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);
        detail::bindConstantBuffer(c,ranged.Get(),detail::ConstantBufferStage::Vertex,0,vc);
        detail::bindConstantBuffer(c,ranged.Get(),detail::ConstantBufferStage::Pixel,1,pc1);
        detail::bindConstantBuffer(c,ranged.Get(),detail::ConstantBufferStage::Pixel,2,pc2);
        detail::bindConstantBuffer(c,ranged.Get(),detail::ConstantBufferStage::Vertex,3,projection);
        auto* b=vb.Get();c->IASetVertexBuffers(0,1,&b,&stride,&offset);c->IASetIndexBuffer(ib.Get(),format,indexOffset);
        c->IASetInputLayout(layout.Get());c->IASetPrimitiveTopology(topology);
        std::array<ID3D11ShaderResourceView*,4> views{};
        std::array<ID3D11SamplerState*,3> samples{};
        for(size_t i=0;i<views.size();++i)views[i]=textures[i].Get();
        for(size_t i=0;i<samples.size();++i)samples[i]=samplers[i].Get();
        c->PSSetShaderResources(0,UINT(views.size()),views.data());
        c->PSSetSamplers(0,UINT(samples.size()),samples.data());
        c->OMSetDepthStencilState(depth.Get(),stencil);c->OMSetBlendState(blend.Get(),factors,sampleMask);
        c->RSSetState(raster.Get());c->RSSetViewports(viewportCount,viewports.data());
    }
};
}
struct ImmediatePipeline {
    // High-water DISCARD storage is owned by this exact pipeline/device.
    // Earlier submitted draws retain their renamed storage; no range is reused
    // through NO_OVERWRITE, and no Map occurs inside the triangle passes.
    static constexpr UINT vertexStreamBytes=16384u*sizeof(ImmediateVertex);
    ComPtr<ID3D11Device> owner;
    ComPtr<ID3D11Buffer> vertexStream,quadIndices;
    UINT vertexCapacity{};
    detail::DirectDrawDepthStates depths;
    void requireDevice(ID3D11Device* device)const{need(owner.Get()==device,"Native immediate pipeline belongs to another device");}
    ComPtr<ID3D11Buffer> streamVertices(ID3D11DeviceContext* context,const void* bytes,UINT size){
        need(size&&size<=vertexStreamBytes,"Native immediate vertex packet exceeds the upload stream");
        if(size>vertexCapacity){
            D3D11_BUFFER_DESC d{};d.ByteWidth=size;d.BindFlags=D3D11_BIND_VERTEX_BUFFER;
            d.Usage=D3D11_USAGE_DYNAMIC;d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            ComPtr<ID3D11Buffer> prepared;
            check(owner->CreateBuffer(&d,nullptr,&prepared),"vertex stream allocation");
            vertexStream=std::move(prepared);vertexCapacity=size;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(vertexStream.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"vertex stream map");
        if(!mapped.pData){context->Unmap(vertexStream.Get(),0);throw Error("Native immediate vertex stream map returned no storage");}
        // The source is already owned/canonicalized. memcpy cannot throw and
        // writes without reading the driver's write-combined mapped storage.
        std::memcpy(mapped.pData,bytes,size);context->Unmap(vertexStream.Get(),0);
        return vertexStream;
    }
    ComPtr<ID3D11Buffer> streamIndices(std::span<const uint16_t> order){
        static constexpr std::array<uint16_t,6> quad{0,1,2,2,1,3};
        // Admission uses the actual finite-filtered order, never a guessed
        // primitive/vertex count. Every other order keeps its exact upload.
        if(order.size()!=quad.size()||!std::equal(order.begin(),order.end(),quad.begin()))
            return upload(owner.Get(),order.data(),UINT(order.size()*sizeof(uint16_t)),D3D11_BIND_INDEX_BUFFER);
        if(!quadIndices){auto prepared=upload(owner.Get(),quad.data(),sizeof(quad),D3D11_BIND_INDEX_BUFFER);quadIndices=std::move(prepared);}
        return quadIndices;
    }
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel,dualPixel;ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11VertexShader> radialVertex;ComPtr<ID3D11PixelShader> radialPixel;ComPtr<ID3D11InputLayout> radialLayout;
    ComPtr<ID3D11VertexShader> projectedVertex;ComPtr<ID3D11PixelShader> projectedPixel,projectedDualPixel;ComPtr<ID3D11InputLayout> projectedLayout;
    
    ComPtr<ID3D11PixelShader> directPixel,directDualPixel,directRadialPixel,snapshotPixel;
    ComPtr<ID3D11DepthStencilState> snapshotDepth;ComPtr<ID3D11BlendState> snapshotBlend;
    ComPtr<ID3D11RenderTargetView> destinationOutput;
    ComPtr<ID3D11Texture2D> packed,destination;
    ComPtr<ID3D11RenderTargetView> output;ComPtr<ID3D11ShaderResourceView> sampled;
    UINT width{},height{};
    explicit ImmediatePipeline(ID3D11Device* device):owner(device){
        check(device->CreateVertexShader(kVSImmediate,sizeof(kVSImmediate),nullptr,&vertex),"vertex shader");
        check(device->CreatePixelShader(kPSImmediateDraw,sizeof(kPSImmediateDraw),nullptr,&pixel),"pixel shader");
        check(device->CreatePixelShader(kPSImmediateDualDraw,sizeof(kPSImmediateDualDraw),nullptr,&dualPixel),"dual pixel shader");
        const D3D11_INPUT_ELEMENT_DESC elements[]={
            {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
        check(device->CreateInputLayout(elements,3,kVSImmediate,sizeof(kVSImmediate),&layout),"input layout");
        check(device->CreateVertexShader(kVSRadial,sizeof(kVSRadial),nullptr,&radialVertex),"radial vertex shader");
        check(device->CreatePixelShader(kPSRadialDraw,sizeof(kPSRadialDraw),nullptr,&radialPixel),"radial pixel shader");
        // DXBC retains the shared input signature, including unused alpha.
        check(device->CreateInputLayout(elements,3,kVSRadial,sizeof(kVSRadial),&radialLayout),"radial input layout");
        check(device->CreateVertexShader(kVSImmediateProjected,sizeof(kVSImmediateProjected),nullptr,&projectedVertex),"projected vertex shader");
        check(device->CreatePixelShader(kPSImmediateProjectedDraw,sizeof(kPSImmediateProjectedDraw),nullptr,&projectedPixel),"projected pixel shader");
        check(device->CreatePixelShader(kPSImmediateProjectedDualDraw,sizeof(kPSImmediateProjectedDualDraw),nullptr,&projectedDualPixel),"projected dual pixel shader");
        check(device->CreateInputLayout(elements,3,kVSImmediateProjected,sizeof(kVSImmediateProjected),&projectedLayout),"projected input layout");
    }
    void ensureDirectPixels(ID3D11Device* device){
        if(directPixel)return;
        ComPtr<ID3D11PixelShader> ordinary,dual,radial,snapshot;
        ComPtr<ID3D11DepthStencilState> depth;ComPtr<ID3D11BlendState> blend;
        check(device->CreatePixelShader(kPSImmediateUNormDraw,sizeof(kPSImmediateUNormDraw),nullptr,&ordinary),"direct UNORM pixel shader");
        check(device->CreatePixelShader(kPSImmediateDualUNormDraw,sizeof(kPSImmediateDualUNormDraw),nullptr,&dual),"direct dual UNORM pixel shader");
        check(device->CreatePixelShader(kPSRadialUNormDraw,sizeof(kPSRadialUNormDraw),nullptr,&radial),"direct radial UNORM pixel shader");
        check(device->CreatePixelShader(kPSImmediateSnapshotDraw,sizeof(kPSImmediateSnapshotDraw),nullptr,&snapshot),"direct UNORM snapshot shader");
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=FALSE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;dd.DepthFunc=D3D11_COMPARISON_ALWAYS;
        check(device->CreateDepthStencilState(&dd,&depth),"direct snapshot depth state");
        D3D11_BLEND_DESC bd{};bd.RenderTarget[0].BlendEnable=FALSE;
        bd.RenderTarget[0].SrcBlend=D3D11_BLEND_ONE;bd.RenderTarget[0].DestBlend=D3D11_BLEND_ZERO;bd.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;bd.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO;bd.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        check(device->CreateBlendState(&bd,&blend),"direct snapshot color state");
        // No partial pipeline publishes on any HRESULT failure.
        directPixel=std::move(ordinary);directDualPixel=std::move(dual);directRadialPixel=std::move(radial);snapshotPixel=std::move(snapshot);
        snapshotDepth=std::move(depth);snapshotBlend=std::move(blend);
    }
    void surfaces(ID3D11Device* device,UINT w,UINT h){
        if(width==w&&height==h)return;
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.SampleDesc={1,0};
        d.Format=DXGI_FORMAT_R10G10B10A2_TYPELESS;d.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> p,t;ComPtr<ID3D11RenderTargetView> r,destinationRtv;ComPtr<ID3D11ShaderResourceView> s;
        check(device->CreateTexture2D(&d,nullptr,&p),"packed output");
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;check(device->CreateTexture2D(&d,nullptr,&t),"sampled destination");
        D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=DXGI_FORMAT_R10G10B10A2_UINT;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        check(device->CreateRenderTargetView(p.Get(),&rd,&r),"packed output view");
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R10G10B10A2_UINT;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels=1;
        check(device->CreateShaderResourceView(t.Get(),&sd,&s),"destination view");
        check(device->CreateRenderTargetView(t.Get(),&rd,&destinationRtv),"direct snapshot destination view");
        destinationOutput=std::move(destinationRtv);packed=std::move(p);destination=std::move(t);output=std::move(r);sampled=std::move(s);width=w;height=h;
    }
};
void NativeBackend::drawImmediate(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,const ImmediateDraw& d){
    drawImmediateImpl(target,depth,d,true);
}
void NativeBackend::drawImmediateImpl(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,const ImmediateDraw& d,bool directUNorm){
    validateSubmissionContext();validateFrontTarget(target);validateDepthCopyTarget(depth);
    need(!d.nullTexture||(!d.texture&&!d.texture2&&!d.radial&&!d.query),"Immediate null texture profile is inconsistent");
    if(!d.radial&&!d.nullTexture)validateTexture(d.texture);
    if(d.texture2){need(!d.radial&&!d.query,"Dual immediate texture is only qualified for billboard draws");validateTexture(d.texture2);}
    if(d.query){validateFrontTarget(d.query);need(d.radial&&d.query->width==64&&d.query->height==8&&d.query->format==TargetFormat::RGB10A2,"Radial query target differs");}
    need(d.projected||!d.projectionDepth,"Immediate projection depth has no projected shader");
    if(d.projected){
        need(!d.radial&&!d.query,"Projected immediate shader combination is unqualified");
        validateDepthCopyTarget(d.projectionDepth);
        need(d.projectionDepth!=depth&&d.projectionDepth->width==1024&&d.projectionDepth->height==1024,
             "Projected immediate requires the distinct original 1024-square shadow depth");
        const auto& sampler=d.projectionSampler;
        need(sampler.Filter==D3D11_FILTER_MIN_MAG_MIP_POINT&&sampler.AddressU==D3D11_TEXTURE_ADDRESS_MIRROR&&
             sampler.AddressV==D3D11_TEXTURE_ADDRESS_MIRROR&&sampler.AddressW>=D3D11_TEXTURE_ADDRESS_WRAP&&
             sampler.AddressW<=D3D11_TEXTURE_ADDRESS_CLAMP&&sampler.MipLODBias==0&&sampler.MinLOD==0&&
             std::isfinite(sampler.MaxLOD)&&sampler.MaxLOD>=0&&sampler.MaxAnisotropy==1&&
             sampler.ComparisonFunc==D3D11_COMPARISON_NEVER,"Projected immediate shadow sampler is unqualified");
        need(std::isfinite(d.projectionConstants[4][0]),"Projected immediate c25.x is nonfinite");
        if(d.projectionConstants[4][0]>=0){
            for(unsigned i=0;i<4;++i)for(float value:d.projectionConstants[i])
                need(std::isfinite(value),"Projected immediate matrix is nonfinite");
            for(const auto& vertex:d.vertices){
                std::array<float,4> value{};
                for(unsigned i=0;i<4;++i){
                    value[i]=vertex.position[2]*d.projectionConstants[2][i]+d.projectionConstants[3][i];
                    value[i]=vertex.position[1]*d.projectionConstants[1][i]+value[i];
                    value[i]=vertex.position[0]*d.projectionConstants[0][i]+value[i];
                    need(std::isfinite(value[i]),"Projected immediate transform is nonfinite");
                }
                need(std::isnormal(value[3]),"Projected immediate homogeneous W is zero or subnormal");
                for(unsigned i=0;i<3;++i)need(std::isfinite(value[i]/value[3]),"Projected immediate divide is nonfinite");
            }
        }
    }
    need(target->format==TargetFormat::RGB10A2,"Immediates require original RGB10A2 color");
    need((d.primitive==6||d.primitive==4)&&(!d.radial||d.primitive==6)&&
         d.vertices.size()>=3&&d.vertices.size()<=16384&&
         (d.primitive==6||d.vertices.size()%3==0),"Immediate primitive or vertex extent is invalid");
    need(d.depthEnable<=1&&d.depthWrite<=1&&d.depthCompare<=7&&d.colorMask<=15,"Immediate depth/color state is invalid");
    // Original radial batch8276B750/body8276B938/cleanup8276B898 retain
    // culling. Preserve the established Xenos0/2/6 modes for that producer.
    need(!d.stencilEnable&&(d.cull==0||(d.radial&&(d.cull==2||d.cull==6)))&&!d.fill&&!d.alphaToMask&&!d.clipPlaneEnable&&d.viewportEnable==1&&d.halfPixelOffset==1,
         "Immediate stencil/cull/fill/clip/viewport state is unqualified");
    need(d.alphaTest<=1&&d.blendEnable<=1&&d.alphaReference<=255&&(d.blendWord==0x10106||d.blendWord==0x10186||d.blendWord==0x10706||d.blendWord==0x10001),"Immediate blend/alpha state is unqualified");
    const float constantBias=std::bit_cast<float>(d.depthBiasBits),slopeBias=std::bit_cast<float>(d.slopeBiasBits);
    need(std::isfinite(constantBias)&&std::isfinite(slopeBias)&&std::isfinite(slopeBias*16.0f),
         "Immediate polygon bias is nonfinite or overflows the original slope-times16 operation");
    need(d.scissorEnable<=1&&(d.multisampleMask==0xFFFFFFFF||d.multisampleMask==0xFFFF),"Immediate scissor/sample mask is unqualified");
    need(d.viewport[0]==0&&d.viewport[1]==0&&d.viewport[2]==target->width&&d.viewport[3]==target->height&&
         ((d.viewport[4]==0&&d.viewport[5]==0x3f800000)||(d.viewport[4]==0x3f800000&&d.viewport[5]==0)),"Immediate viewport is unqualified");
    need(depth->width==target->width&&depth->height==target->height,"Immediate attachment extents differ");
    for(const auto& row:d.constants)for(float v:row)need(std::isfinite(v),"Immediate constant is nonfinite");
    // Mode-zero producers such as 8277B040 and 82778F58 write only the first
    // six floats. PS821509D8 samples UV.xy; UV.zw is dead unless the dual
    // texture shader is selected. The radial shader uses all four UV lanes
    // as its color, so it retains the full validation.
    const bool unusedSecondUv=!d.radial&&!d.texture2;
    // A vertex whose position is not finite cannot be rasterized: the original GPU discards every primitive that uses
    // it, and billboard producers do emit such vertices (live gamehub: a dual-texture quad whose first vertex was all
    // quiet NaN killed the process). Those triangles are dropped here. Everything the surviving triangles consume must
    // still be finite. Radial and projected draws keep their all-vertex validation.
    const bool culling=!d.radial&&!d.projected;
    const auto positionFinite=[&](size_t index){const auto& p=d.vertices[index].position;return std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2]);};
    std::vector<uint16_t> order;
    if(d.primitive==4){for(size_t i=0;i+2<d.vertices.size();i+=3)if(!culling||(positionFinite(i)&&positionFinite(i+1)&&positionFinite(i+2)))
        for(size_t k=0;k<3;++k)order.push_back(uint16_t(i+k));}
    else for(size_t i=0;i+2<d.vertices.size();++i)if(!culling||(positionFinite(i)&&positionFinite(i+1)&&positionFinite(i+2))){
        order.push_back(uint16_t(i+(i&1)));order.push_back(uint16_t(i+!(i&1)));order.push_back(uint16_t(i+2));}
    std::vector<bool> consumed(d.vertices.size(),!culling);
    for(const auto index:order)consumed[index]=true;
    for(size_t vertexIndex=0;vertexIndex<d.vertices.size();++vertexIndex){
        if(!consumed[vertexIndex])continue;
        const auto* values=reinterpret_cast<const float*>(&d.vertices[vertexIndex]);
        for(unsigned i=0;i<(unusedSecondUv?6u:8u);++i)need(std::isfinite(values[i]),"Immediate vertex is nonfinite");}
    flushIm2D();requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);
    // This draw establishes its own qualified viewport below. Prior native
    // draws may leave zero or several viewports; Restore preserves that state.
    UINT viewportCount=0;context->RSGetViewports(&viewportCount,nullptr);
    need(viewportCount<=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE,"Immediate retained viewport count exceeds D3D11 limits");
    if(d.scissorEnable){const auto retained=scissor();need(retained&&(*retained)[0]<(*retained)[2]&&(*retained)[1]<(*retained)[3]&&
        (*retained)[2]<=target->width&&(*retained)[3]<=target->height,"Immediate retained scissor is invalid");}
    ComPtr<ID3D11Predicate> predicate;BOOL predicateValue{};context->GetPredication(&predicate,&predicateValue);
    need(!predicate,"Immediate predicated submission is unqualified");
    ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;
    context->GSGetShader(&gs,nullptr,nullptr);context->HSGetShader(&hs,nullptr,nullptr);context->DSGetShader(&ds,nullptr,nullptr);
    need(!gs&&!hs&&!ds,"Immediate submission has unrelated programmable stages");
    // These direct draws replace OM targets, so unrelated output UAVs must be
    // rejected before submission instead of being silently removed by D3D11.
    std::array<ID3D11UnorderedAccessView*,64> outputs{};
    const UINT outputCount=device->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?64u:8u;
    context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,outputCount,outputs.data());
    bool hasUnorderedOutput=false;for(auto* output:outputs)if(output){hasUnorderedOutput=true;output->Release();}
    need(!hasUnorderedOutput,"Immediate submission has unrelated output UAVs");
    if(order.empty()){++immediateDraws;requireOwner();return;} // Every triangle used a non-finite position: nothing to rasterize.
    if(!immediatePipeline)immediatePipeline=std::make_shared<ImmediatePipeline>(device.Get());auto& p=*immediatePipeline;
    p.requireDevice(device.Get());
    p.surfaces(device.Get(),target->pixelWidth(),target->pixelHeight());
    // Only a separately authenticated original Beam producer supplies these
    // native metadata fields. Unqualified world immediates still fail below.
    std::shared_ptr<TemporalGeometryDraw> temporal;
    if(d.temporalObject) {
        need(!d.radial&&!d.projected&&!d.texture2&&!d.query&&!d.nullTexture&&d.primitive==6&&d.vertices.size()==8,
             "Temporal Immediate requires the exact eight-vertex mode-zero Beam profile");
        for(const auto& vertex:d.vertices)for(float value:vertex.position)
            need(std::isfinite(value),"Temporal Immediate position is nonfinite");
        ShadowMeshDraw temporalState=d;temporalState.temporalBlendEnable=0;temporalState.temporalBlendWord=0x00010001;
        temporal=temporalGeometryDraw(target,depth,temporalState,0x821511D8,5,false);
        if(temporal) {
            std::array<TemporalRegister,13> current{};
            std::copy(d.constants.begin(),d.constants.end(),current.begin());
            for(size_t i=0;i<8;++i)current[5+i]={d.vertices[i].position[0],d.vertices[i].position[1],d.vertices[i].position[2],1};
            temporal->packedOutput(p.output.Get(),bool(d.blendEnable)||!d.depthWrite);
            temporal->prepare(context.Get(),current);
        }
    }
    if((d.colorMask&7)&&!temporal)rejectUntrackedTemporalWorldDraw(target,depth,"immediate geometry");
    const auto vp=renderViewport(target,{0,0,float(target->width),float(target->height),0,1});
    const auto region=immediateCopyRegion(d,order,target->pixelWidth(),target->pixelHeight(),bool(temporal),vp);
    if(region.left>=region.right || region.top>=region.bottom) {
        // Coverage is empty only for qualified ordinary draws.
        // Keep submission bookkeeping, but avoid uploads and native draw state.
        ++immediateDraws;
        noteTemporalSceneDraw(target,depth,d.colorMask,d.depthEnable&&d.depthWrite,false);
        requireOwner();return;
    }
    const bool direct=directUNorm&&!temporal&&!d.projected;
    if(direct)p.ensureDirectPixels(device.Get());
    const uint32_t drawWords[]={d.blendWord,std::bit_cast<uint32_t>(float(d.alphaReference)/255.0f),d.blendEnable,d.alphaTest|(d.query?2u:0u)};
    // Canonicalize only the native upload. Preserve the caller's snapshot and
    // the original ring bytes, including unwritten data from earlier batches.
    std::vector<ImmediateVertex> canonical;
    const auto* vertexData=d.vertices.data();
    if(unusedSecondUv){canonical=d.vertices;for(auto& vertex:canonical)vertex.uv[2]=vertex.uv[3]=0;vertexData=canonical.data();}
    // The private retained reference continues to allocate dedicated immutable
    // resources. Public submissions DISCARD exactly once before all passes.
    auto vertices=directUNorm?p.streamVertices(context.Get(),vertexData,UINT(d.vertices.size()*sizeof(ImmediateVertex))):
        upload(device.Get(),vertexData,UINT(d.vertices.size()*sizeof(ImmediateVertex)),D3D11_BIND_VERTEX_BUFFER);
    auto indices=directUNorm?p.streamIndices(order):upload(device.Get(),order.data(),UINT(order.size()*2),D3D11_BIND_INDEX_BUFFER);
    static_assert(sizeof(d.constants)==80 && sizeof(drawWords)==16);
    const auto vc=publishConstantSlice(ImmediateVertexBank,d.constants.data(),sizeof(d.constants));
    requireConstantSlice(ImmediateVertexBank,vc,sizeof(d.constants),"Native immediate vertex constants lack their publication");
    auto dc=sceneDepthConstantBuffer(uint32_t(d.viewport[4]!=0),d.depthBiasBits,d.slopeBiasBits);
    const auto pc=publishConstantSlice(ImmediatePixelBank,drawWords,sizeof(drawWords));
    requireConstantSlice(ImmediatePixelBank,pc,sizeof(drawWords),"Native immediate pixel constants lack their publication");
    ComPtr<ID3D11Buffer> projection;
    if(d.projected){
        auto constants=d.projectionConstants;
        if(constants[4][0]<0)for(unsigned i=0;i<4;++i)constants[i]={};
        constants[4][1]=constants[4][2]=constants[4][3]=0;
        projection=upload(device.Get(),constants.data(),sizeof(constants),D3D11_BIND_CONSTANT_BUFFER);
    }
    ComPtr<ID3D11DepthStencilState> depthState;
    if(directUNorm)check(p.depths.get(device.Get(),d.depthEnable,d.depthWrite,d.depthCompare,depthState),"depth state");
    else{
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=d.depthEnable;
        dd.DepthWriteMask=d.depthWrite?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;dd.DepthFunc=D3D11_COMPARISON_FUNC(d.depthCompare+1);
        check(device->CreateDepthStencilState(&dd,&depthState),"depth state");
    }
    auto blend=sceneBlendState(0,0x10001,d.colorMask);
    auto raster=sceneRasterState(d.cull,d.scissorEnable);ComPtr<ID3D11SamplerState> sampler;
    if(!d.nullTexture)sampler=sceneSamplerState(d.sampler);
    ComPtr<ID3D11SamplerState> querySampler;D3D11_SAMPLER_DESC q{};q.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    q.AddressU=q.AddressV=q.AddressW=D3D11_TEXTURE_ADDRESS_MIRROR;q.MaxAnisotropy=1;q.ComparisonFunc=D3D11_COMPARISON_NEVER;
    querySampler=sceneSamplerState(d.projected?d.projectionSampler:q);
    {
        Restore restore(context.Get(),constantBindingContext());
        // Preserve the exact ranged temporal slots once around the original
        // ordered triangle loop. The outer scope restores all original MRTs.
        ScopedTemporalGeometryDraw temporalRestore(context.Get(),temporal);
        auto* vb=vertices.Get();UINT stride=sizeof(ImmediateVertex),offset=0;context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        context->IASetInputLayout(d.projected?p.projectedLayout.Get():(d.radial?p.radialLayout.Get():p.layout.Get()));context->IASetIndexBuffer(indices.Get(),DXGI_FORMAT_R16_UINT,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(d.projected?p.projectedVertex.Get():(d.radial?p.radialVertex.Get():p.vertex.Get()),nullptr,0);
        context->PSSetShader(d.projected?(d.texture2?p.projectedDualPixel.Get():p.projectedPixel.Get()):
                                         (d.radial?p.radialPixel.Get():(d.texture2?p.dualPixel.Get():p.pixel.Get())),nullptr,0);
        detail::bindConstantBuffer(context.Get(),restore.ranged.Get(),detail::ConstantBufferStage::Vertex,0,vc.binding);
        auto* cb=dc.Get();context->PSSetConstantBuffers(1,1,&cb);
        detail::bindConstantBuffer(context.Get(),restore.ranged.Get(),detail::ConstantBufferStage::Pixel,2,pc.binding);
        // Prove the actual bound owner/range before any draw. An ignored or
        // malformed native setter must unwind through Restore, outside its
        // noexcept binding helper. This validation has a real per-draw cost.
        const auto boundVertex=detail::captureConstantBuffer(context.Get(),restore.ranged.Get(),detail::ConstantBufferStage::Vertex,0);
        const auto boundPixel=detail::captureConstantBuffer(context.Get(),restore.ranged.Get(),detail::ConstantBufferStage::Pixel,2);
        need(detail::constantBufferBindingMatches(boundVertex,vc.binding)&&
             detail::constantBufferBindingMatches(boundPixel,pc.binding),"Native immediate constant binding differs from its publication");
        if(d.projected){cb=projection.Get();context->VSSetConstantBuffers(3,1,&cb);}
        auto* texture=d.texture?d.texture->view.Get():nullptr;context->PSSetShaderResources(0,1,&texture);auto* sample=sampler.Get();context->PSSetSamplers(0,1,&sample);
        auto* texture2=d.texture2?d.texture2->view.Get():nullptr;context->PSSetShaderResources(1,1,&texture2);
        auto* query=d.projected?d.projectionDepth->depthView.Get():(d.query?d.query->sampledView.Get():nullptr);context->PSSetShaderResources(2,1,&query);
        sample=d.texture2?sampler.Get():querySampler.Get();context->PSSetSamplers(1,1,&sample);
        if(d.projected){sample=querySampler.Get();context->PSSetSamplers(2,1,&sample);}
        context->RSSetViewports(1,&vp);
        context->RSSetState(raster.Get());context->OMSetDepthStencilState(depthState.Get(),0);context->OMSetBlendState(blend.Get(),nullptr,d.multisampleMask);
        // Each original triangle reads preceding packed output, retaining order
        // even when a strip folds over itself or independent triangles overlap.
        const bool whole=region.left==0 && region.top==0 && region.right==target->pixelWidth() && region.bottom==target->pixelHeight();
        const auto copy=[&](ID3D11Texture2D* destination,ID3D11Texture2D* source) {
            if(whole)context->CopyResource(destination,source);
            else context->CopySubresourceRegion(destination,0,region.left,region.top,0,source,0,&region);
        };
        // Direct snapshots and retained copies preserve original triangle order.
        if(direct){
            // Snapshot the preceding real target once per ORIGINAL triangle.
            // Both snapshot and draw use the original VS/raster/scissor. The
            // snapshot has no DSV/alpha rejection and writes every UINT lane.
            for(UINT first=0;first<order.size();first+=3){
                ID3D11ShaderResourceView* nullView=nullptr;context->PSSetShaderResources(3,1,&nullView);
                context->OMSetRenderTargets(0,nullptr,nullptr);
                auto* source=target->sampledView.Get();context->PSSetShaderResources(3,1,&source);
                context->PSSetShader(p.snapshotPixel.Get(),nullptr,0);
                context->OMSetDepthStencilState(p.snapshotDepth.Get(),0);
                context->OMSetBlendState(p.snapshotBlend.Get(),nullptr,d.multisampleMask);
                auto* snapshot=p.destinationOutput.Get();context->OMSetRenderTargets(1,&snapshot,nullptr);
                context->DrawIndexed(3,first,0);
                context->PSSetShaderResources(3,1,&nullView);context->OMSetRenderTargets(0,nullptr,nullptr);
                context->PSSetShader(d.radial?p.directRadialPixel.Get():(d.texture2?p.directDualPixel.Get():p.directPixel.Get()),nullptr,0);
                context->OMSetDepthStencilState(depthState.Get(),0);
                context->OMSetBlendState(blend.Get(),nullptr,d.multisampleMask);
                auto* view=p.sampled.Get();context->PSSetShaderResources(3,1,&view);
                auto* output=target->view.Get();context->OMSetRenderTargets(1,&output,depth->view.Get());
                context->DrawIndexed(3,first,0);
            }
            context->OMSetRenderTargets(0,nullptr,nullptr);
        }else{
            // Retain the old conservative box copies as the private oracle and
            // fallback. All three transfers use the same physical coordinates;
            // outside texels in the real target are never read or overwritten.
            context->OMSetRenderTargets(0,nullptr,nullptr);copy(p.packed.Get(),target->texture.Get());
            for(UINT first=0;first<order.size();first+=3){
                ID3D11ShaderResourceView* nullView=nullptr;context->PSSetShaderResources(3,1,&nullView);
                context->OMSetRenderTargets(0,nullptr,nullptr);copy(p.destination.Get(),p.packed.Get());
                auto* view=p.sampled.Get();context->PSSetShaderResources(3,1,&view);
                if(temporal)temporal->bind(context.Get());
                else {auto* output=p.output.Get();context->OMSetRenderTargets(1,&output,depth->view.Get());}
                context->DrawIndexed(3,first,0);
            }
            context->OMSetRenderTargets(0,nullptr,nullptr);copy(target->texture.Get(),p.packed.Get());
        }
        ++immediateDraws;
        noteTemporalSceneDraw(target,depth,d.colorMask,d.depthEnable&&d.depthWrite,false,
            temporal?std::optional{temporalSceneJitter()}:std::nullopt);
    }
    requireOwner();
}
}
