#include "immediate_draw.h"
#include "VSImmediate.h"
#include "PSImmediateDraw.h"
#include "PSImmediateDualDraw.h"
#include "VSRadial.h"
#include "PSRadialDraw.h"
#include "VSImmediateProjected.h"
#include "PSImmediateProjectedDraw.h"
#include "PSImmediateProjectedDualDraw.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>

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
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> vb,ib,vc,projection,pc1,pc2;ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11ShaderResourceView> textures[4];ComPtr<ID3D11SamplerState> sampler,querySampler,projectionSampler;
    ComPtr<ID3D11DepthStencilState> depth;ComPtr<ID3D11BlendState> blend;ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11RenderTargetView> target;ComPtr<ID3D11DepthStencilView> dsv;
    std::array<D3D11_VIEWPORT,16> viewports{};D3D11_PRIMITIVE_TOPOLOGY topology{};DXGI_FORMAT format{};
    UINT stride{},offset{},indexOffset{},stencil{},sampleMask{},viewportCount{16};FLOAT factors[4]{};
    explicit Restore(ID3D11DeviceContext* context):c(context){
        c->VSGetShader(&vs,nullptr,nullptr);c->PSGetShader(&ps,nullptr,nullptr);
        c->VSGetConstantBuffers(0,1,&vc);c->PSGetConstantBuffers(1,1,&pc1);c->PSGetConstantBuffers(2,1,&pc2);
        c->VSGetConstantBuffers(3,1,&projection);
        c->IAGetVertexBuffers(0,1,&vb,&stride,&offset);c->IAGetIndexBuffer(&ib,&format,&indexOffset);
        c->IAGetInputLayout(&layout);c->IAGetPrimitiveTopology(&topology);
        for(UINT i=0;i<4;++i)c->PSGetShaderResources(i,1,textures[i].ReleaseAndGetAddressOf());
        c->PSGetSamplers(0,1,&sampler);
        c->PSGetSamplers(1,1,&querySampler);
        c->PSGetSamplers(2,1,&projectionSampler);
        c->OMGetDepthStencilState(&depth,&stencil);c->OMGetBlendState(&blend,factors,&sampleMask);c->RSGetState(&raster);
        c->RSGetViewports(&viewportCount,viewports.data());c->OMGetRenderTargets(1,&target,&dsv);
    }
    ~Restore(){
        ID3D11ShaderResourceView* nulls[4]{};c->PSSetShaderResources(0,4,nulls);
        auto* rt=target.Get();c->OMSetRenderTargets(1,&rt,dsv.Get());
        c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);
        auto* b=vc.Get();c->VSSetConstantBuffers(0,1,&b);b=pc1.Get();c->PSSetConstantBuffers(1,1,&b);b=pc2.Get();c->PSSetConstantBuffers(2,1,&b);
        b=projection.Get();c->VSSetConstantBuffers(3,1,&b);
        b=vb.Get();c->IASetVertexBuffers(0,1,&b,&stride,&offset);c->IASetIndexBuffer(ib.Get(),format,indexOffset);
        c->IASetInputLayout(layout.Get());c->IASetPrimitiveTopology(topology);
        for(UINT i=0;i<4;++i){auto* view=textures[i].Get();c->PSSetShaderResources(i,1,&view);}auto* s=sampler.Get();c->PSSetSamplers(0,1,&s);
        s=querySampler.Get();c->PSSetSamplers(1,1,&s);
        s=projectionSampler.Get();c->PSSetSamplers(2,1,&s);
        c->OMSetDepthStencilState(depth.Get(),stencil);c->OMSetBlendState(blend.Get(),factors,sampleMask);
        c->RSSetState(raster.Get());c->RSSetViewports(viewportCount,viewports.data());
    }
};
}
struct ImmediatePipeline {
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel,dualPixel;ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11VertexShader> radialVertex;ComPtr<ID3D11PixelShader> radialPixel;ComPtr<ID3D11InputLayout> radialLayout;
    ComPtr<ID3D11VertexShader> projectedVertex;ComPtr<ID3D11PixelShader> projectedPixel,projectedDualPixel;ComPtr<ID3D11InputLayout> projectedLayout;
    
    ComPtr<ID3D11Texture2D> packed,destination;
    ComPtr<ID3D11RenderTargetView> output;ComPtr<ID3D11ShaderResourceView> sampled;
    UINT width{},height{};
    explicit ImmediatePipeline(ID3D11Device* device){
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
    void surfaces(ID3D11Device* device,UINT w,UINT h){
        if(width==w&&height==h)return;
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.SampleDesc.Count=1;
        d.Format=DXGI_FORMAT_R10G10B10A2_TYPELESS;d.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> p,t;ComPtr<ID3D11RenderTargetView> r;ComPtr<ID3D11ShaderResourceView> s;
        check(device->CreateTexture2D(&d,nullptr,&p),"packed output");
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE;check(device->CreateTexture2D(&d,nullptr,&t),"sampled destination");
        D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=DXGI_FORMAT_R10G10B10A2_UINT;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        check(device->CreateRenderTargetView(p.Get(),&rd,&r),"packed output view");
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R10G10B10A2_UINT;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
        check(device->CreateShaderResourceView(t.Get(),&sd,&s),"destination view");
        packed=std::move(p);destination=std::move(t);output=std::move(r);sampled=std::move(s);width=w;height=h;
    }
};
void NativeBackend::drawImmediate(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,const ImmediateDraw& d){
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
    if(!immediatePipeline)immediatePipeline=std::make_shared<ImmediatePipeline>(device.Get());auto& p=*immediatePipeline;p.surfaces(device.Get(),target->pixelWidth(),target->pixelHeight());
    const uint32_t depthWords[]={uint32_t(d.viewport[4]!=0),d.depthBiasBits,d.slopeBiasBits,0};
    const uint32_t drawWords[]={d.blendWord,std::bit_cast<uint32_t>(float(d.alphaReference)/255.0f),d.blendEnable,d.alphaTest|(d.query?2u:0u)};
    // Canonicalize only the native upload. Preserve the caller's snapshot and
    // the original ring bytes, including unwritten data from earlier batches.
    std::vector<ImmediateVertex> canonical;
    const auto* vertexData=d.vertices.data();
    if(unusedSecondUv){canonical=d.vertices;for(auto& vertex:canonical)vertex.uv[2]=vertex.uv[3]=0;vertexData=canonical.data();}
    auto vertices=upload(device.Get(),vertexData,UINT(d.vertices.size()*sizeof(ImmediateVertex)),D3D11_BIND_VERTEX_BUFFER);
    auto indices=upload(device.Get(),order.data(),UINT(order.size()*2),D3D11_BIND_INDEX_BUFFER);
    auto vc=upload(device.Get(),d.constants.data(),sizeof(d.constants),D3D11_BIND_CONSTANT_BUFFER);
    auto dc=upload(device.Get(),depthWords,sizeof(depthWords),D3D11_BIND_CONSTANT_BUFFER);
    auto pc=upload(device.Get(),drawWords,sizeof(drawWords),D3D11_BIND_CONSTANT_BUFFER);
    ComPtr<ID3D11Buffer> projection;
    if(d.projected){
        auto constants=d.projectionConstants;
        if(constants[4][0]<0)for(unsigned i=0;i<4;++i)constants[i]={};
        constants[4][1]=constants[4][2]=constants[4][3]=0;
        projection=upload(device.Get(),constants.data(),sizeof(constants),D3D11_BIND_CONSTANT_BUFFER);
    }
    ComPtr<ID3D11DepthStencilState> depthState;D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=d.depthEnable;
    dd.DepthWriteMask=d.depthWrite?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;dd.DepthFunc=D3D11_COMPARISON_FUNC(d.depthCompare+1);
    check(device->CreateDepthStencilState(&dd,&depthState),"depth state");
    ComPtr<ID3D11BlendState> blend;D3D11_BLEND_DESC bd{};bd.RenderTarget[0].BlendEnable=FALSE;
    bd.RenderTarget[0].SrcBlend=D3D11_BLEND_ONE;bd.RenderTarget[0].DestBlend=D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;bd.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO;bd.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask=UINT8(d.colorMask);
    check(device->CreateBlendState(&bd,&blend),"packed color state");
    ComPtr<ID3D11RasterizerState> raster;D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;
    rd.CullMode=d.cull?D3D11_CULL_BACK:D3D11_CULL_NONE;rd.FrontCounterClockwise=d.cull==2;
    rd.DepthClipEnable=TRUE;rd.ScissorEnable=d.scissorEnable;
    check(device->CreateRasterizerState(&rd,&raster),"raster state");ComPtr<ID3D11SamplerState> sampler;
    if(!d.nullTexture)check(device->CreateSamplerState(&d.sampler,&sampler),"texture sampler");
    ComPtr<ID3D11SamplerState> querySampler;D3D11_SAMPLER_DESC q{};q.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    q.AddressU=q.AddressV=q.AddressW=D3D11_TEXTURE_ADDRESS_MIRROR;q.MaxAnisotropy=1;q.ComparisonFunc=D3D11_COMPARISON_NEVER;
    check(device->CreateSamplerState(d.projected?&d.projectionSampler:&q,&querySampler),"query/projection sampler");
    {
        Restore restore(context.Get());
        auto* vb=vertices.Get();UINT stride=sizeof(ImmediateVertex),offset=0;context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        context->IASetInputLayout(d.projected?p.projectedLayout.Get():(d.radial?p.radialLayout.Get():p.layout.Get()));context->IASetIndexBuffer(indices.Get(),DXGI_FORMAT_R16_UINT,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(d.projected?p.projectedVertex.Get():(d.radial?p.radialVertex.Get():p.vertex.Get()),nullptr,0);
        context->PSSetShader(d.projected?(d.texture2?p.projectedDualPixel.Get():p.projectedPixel.Get()):(d.radial?p.radialPixel.Get():(d.texture2?p.dualPixel.Get():p.pixel.Get())),nullptr,0);
        auto* cb=vc.Get();context->VSSetConstantBuffers(0,1,&cb);cb=dc.Get();context->PSSetConstantBuffers(1,1,&cb);cb=pc.Get();context->PSSetConstantBuffers(2,1,&cb);
        if(d.projected){cb=projection.Get();context->VSSetConstantBuffers(3,1,&cb);}
        auto* texture=d.texture?d.texture->view.Get():nullptr;context->PSSetShaderResources(0,1,&texture);auto* sample=sampler.Get();context->PSSetSamplers(0,1,&sample);
        auto* texture2=d.texture2?d.texture2->view.Get():nullptr;context->PSSetShaderResources(1,1,&texture2);
        auto* query=d.projected?d.projectionDepth->depthView.Get():(d.query?d.query->sampledView.Get():nullptr);context->PSSetShaderResources(2,1,&query);
        sample=d.texture2?sampler.Get():querySampler.Get();context->PSSetSamplers(1,1,&sample);
        if(d.projected){sample=querySampler.Get();context->PSSetSamplers(2,1,&sample);}
        const auto vp=renderViewport(target,{0,0,float(target->width),float(target->height),0,1});context->RSSetViewports(1,&vp);
        context->RSSetState(raster.Get());context->OMSetDepthStencilState(depthState.Get(),0);context->OMSetBlendState(blend.Get(),nullptr,d.multisampleMask);
        // Each original triangle reads preceding packed output, retaining order
        // even when a strip folds over itself or independent triangles overlap.
        context->OMSetRenderTargets(0,nullptr,nullptr);context->CopyResource(p.packed.Get(),target->texture.Get());
        for(UINT first=0;first<order.size();first+=3){
            ID3D11ShaderResourceView* nullView=nullptr;context->PSSetShaderResources(3,1,&nullView);
            context->OMSetRenderTargets(0,nullptr,nullptr);context->CopyResource(p.destination.Get(),p.packed.Get());
            auto* view=p.sampled.Get();context->PSSetShaderResources(3,1,&view);auto* output=p.output.Get();context->OMSetRenderTargets(1,&output,depth->view.Get());
            context->DrawIndexed(3,first,0);
        }
        context->OMSetRenderTargets(0,nullptr,nullptr);context->CopyResource(target->texture.Get(),p.packed.Get());++immediateDraws;
    }
    requireOwner();
}
}
