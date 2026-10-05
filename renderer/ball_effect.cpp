#include "ball_effect.h"
#include "VSBallEffect.h"
#include "PSBallEffectDraw.h"
#include <bit>
#include <cmath>
#include <cstdio>

namespace Simpsons::Graphics {
namespace {
void need(bool ok,const char* why){if(!ok)throw Error(why);}
void check(HRESULT hr,const char* why){if(FAILED(hr)){char text[180];std::snprintf(text,sizeof(text),"Native ball effect %s: %08lX",why,ULONG(hr));throw Error(text);}}
ComPtr<ID3D11Buffer> upload(ID3D11Device* device,const void* bytes,UINT size,UINT bind){
    D3D11_BUFFER_DESC d{};d.ByteWidth=size;d.BindFlags=bind;d.Usage=D3D11_USAGE_IMMUTABLE;
    const D3D11_SUBRESOURCE_DATA initial{bytes,0,0};ComPtr<ID3D11Buffer> result;
    check(device->CreateBuffer(&d,&initial,&result),"owned upload");return result;
}
// Keep all resource slots: binding/restoring a target must not silently erase an
// unrelated sampled view through D3D11's automatic read/write hazard handling.
struct Restore {
    ID3D11DeviceContext* c;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    std::array<ID3D11ClassInstance*,256> vsClasses{},psClasses{};
    UINT vsClassCount=256,psClassCount=256;
    ComPtr<ID3D11Buffer> vb,vc0,pc1,pc2;ComPtr<ID3D11InputLayout> layout;
    std::array<std::array<ID3D11ShaderResourceView*,128>,6> srvs{};
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11DepthStencilState> depth;ComPtr<ID3D11BlendState> blend;ComPtr<ID3D11RasterizerState> raster;
    std::array<ID3D11RenderTargetView*,8> targets{};ComPtr<ID3D11DepthStencilView> dsv;
    std::array<D3D11_VIEWPORT,16> viewports{};std::array<D3D11_RECT,16> scissors{};
    UINT viewportCount=16,scissorCount=16,stride{},offset{},stencil{},sampleMask{};
    FLOAT factors[4]{};D3D11_PRIMITIVE_TOPOLOGY topology{};
    explicit Restore(ID3D11DeviceContext* ctx):c(ctx){
        c->VSGetShader(&vs,vsClasses.data(),&vsClassCount);c->PSGetShader(&ps,psClasses.data(),&psClassCount);
        c->VSGetConstantBuffers(0,1,&vc0);c->PSGetConstantBuffers(1,1,&pc1);c->PSGetConstantBuffers(2,1,&pc2);
        c->IAGetVertexBuffers(0,1,&vb,&stride,&offset);c->IAGetInputLayout(&layout);c->IAGetPrimitiveTopology(&topology);
        c->PSGetSamplers(0,1,&sampler);
        c->VSGetShaderResources(0,128,srvs[0].data());c->PSGetShaderResources(0,128,srvs[1].data());
        c->GSGetShaderResources(0,128,srvs[2].data());c->HSGetShaderResources(0,128,srvs[3].data());
        c->DSGetShaderResources(0,128,srvs[4].data());c->CSGetShaderResources(0,128,srvs[5].data());
        c->OMGetRenderTargets(8,targets.data(),&dsv);c->OMGetDepthStencilState(&depth,&stencil);
        c->OMGetBlendState(&blend,factors,&sampleMask);c->RSGetState(&raster);
        c->RSGetViewports(&viewportCount,viewports.data());c->RSGetScissorRects(&scissorCount,scissors.data());
    }
    Restore(const Restore&)=delete;
    Restore& operator=(const Restore&)=delete;
    void clearResources(){
        std::array<ID3D11ShaderResourceView*,128> empty{};
        c->VSSetShaderResources(0,128,empty.data());c->PSSetShaderResources(0,128,empty.data());
        c->GSSetShaderResources(0,128,empty.data());c->HSSetShaderResources(0,128,empty.data());
        c->DSSetShaderResources(0,128,empty.data());c->CSSetShaderResources(0,128,empty.data());
    }
    ~Restore(){
        clearResources();c->OMSetRenderTargets(8,targets.data(),dsv.Get());
        c->VSSetShader(vs.Get(),vsClasses.data(),vsClassCount);c->PSSetShader(ps.Get(),psClasses.data(),psClassCount);
        auto* b=vc0.Get();c->VSSetConstantBuffers(0,1,&b);b=pc1.Get();c->PSSetConstantBuffers(1,1,&b);b=pc2.Get();c->PSSetConstantBuffers(2,1,&b);
        b=vb.Get();c->IASetVertexBuffers(0,1,&b,&stride,&offset);c->IASetInputLayout(layout.Get());c->IASetPrimitiveTopology(topology);
        auto* s=sampler.Get();c->PSSetSamplers(0,1,&s);
        c->VSSetShaderResources(0,128,srvs[0].data());c->PSSetShaderResources(0,128,srvs[1].data());
        c->GSSetShaderResources(0,128,srvs[2].data());c->HSSetShaderResources(0,128,srvs[3].data());
        c->DSSetShaderResources(0,128,srvs[4].data());c->CSSetShaderResources(0,128,srvs[5].data());
        c->OMSetDepthStencilState(depth.Get(),stencil);c->OMSetBlendState(blend.Get(),factors,sampleMask);
        c->RSSetState(raster.Get());c->RSSetViewports(viewportCount,viewports.data());c->RSSetScissorRects(scissorCount,scissors.data());
        for(auto* v:targets)if(v)v->Release();
        for(const auto& stage:srvs)for(auto* v:stage)if(v)v->Release();
        for(UINT i=0;i<vsClassCount;++i)vsClasses[i]->Release();
        for(UINT i=0;i<psClassCount;++i)psClasses[i]->Release();
    }
};
void noAuxiliaryStages(ID3D11DeviceContext* context,D3D_FEATURE_LEVEL level){
    ComPtr<ID3D11Predicate> predicate;BOOL value{};context->GetPredication(&predicate,&value);
    ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;
    context->GSGetShader(&gs,nullptr,nullptr);context->HSGetShader(&hs,nullptr,nullptr);context->DSGetShader(&ds,nullptr,nullptr);
    std::array<ID3D11Buffer*,4> outputs{};context->SOGetTargets(4,outputs.data());bool stream=false;
    for(auto* output:outputs)if(output){stream=true;output->Release();}
    // FL11_1 extends OM UAV binding through slot63. Every binding must be
    // checked before OMSetRenderTargets can discard an unsupported output.
    std::array<ID3D11UnorderedAccessView*,64> unordered{};
    context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,level>=D3D_FEATURE_LEVEL_11_1?64:8,unordered.data());bool uav=false;
    for(auto* output:unordered)if(output){uav=true;output->Release();}
    need(!predicate&&!gs&&!hs&&!ds&&!stream&&!uav,"Ball effect has unqualified predication/additional stages/stream output/UAVs");
}
}

struct BallEffectPipeline {
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Texture2D> packed,destination;
    ComPtr<ID3D11RenderTargetView> output;ComPtr<ID3D11ShaderResourceView> sampled;
    UINT width{},height{};
    explicit BallEffectPipeline(ID3D11Device* device){
        check(device->CreateVertexShader(kVSBallEffect,sizeof(kVSBallEffect),nullptr,&vertex),"vertex shader");
        check(device->CreatePixelShader(kPSBallEffectDraw,sizeof(kPSBallEffectDraw),nullptr,&pixel),"pixel shader");
        const D3D11_INPUT_ELEMENT_DESC element{"TEXCOORD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        check(device->CreateInputLayout(&element,1,kVSBallEffect,sizeof(kVSBallEffect),&layout),"input layout");
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
void NativeBackend::drawBallEffect(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,const BallEffectDraw& d){
    validateSubmissionContext();validateFrontTarget(target);validateDepthCopyTarget(depth);validateTexture(d.texture);
    need(target->format==TargetFormat::RGB10A2,"Ball effect requires original RGB10A2 color");
    need(d.depthEnable<=1&&d.depthWrite<=1&&d.depthCompare<=7&&d.colorMask<=15,"Ball effect depth/color state is invalid");
    need(!d.stencilEnable&&!d.cull&&!d.fill&&!d.alphaToMask&&!d.clipPlaneEnable&&d.viewportEnable==1&&d.halfPixelOffset==1,
         "Ball effect stencil/cull/fill/clip/viewport state is unqualified");
    need(d.alphaTest<=1&&d.blendEnable<=1&&d.alphaReference<=255&&(d.blendWord==0x10106||d.blendWord==0x10186||d.blendWord==0x10706||d.blendWord==0x10001),"Ball effect blend/alpha state is unqualified");
    need(!d.depthBiasBits&&!d.slopeBiasBits,"Ball effect polygon bias is unqualified");
    need(d.scissorEnable<=1&&d.multisampleAntialias<=1&&(d.multisampleMask==0xFFFFFFFF||d.multisampleMask==0xFFFF),"Ball effect scissor/sample mask is unqualified");
    need(d.viewport[0]==0&&d.viewport[1]==0&&d.viewport[2]==target->width&&d.viewport[3]==target->height&&
         ((d.viewport[4]==0&&d.viewport[5]==0x3f800000)||(d.viewport[4]==0x3f800000&&d.viewport[5]==0)),"Ball effect viewport is unqualified");
    need(depth->width==target->width&&depth->height==target->height,"Ball effect attachment extents differ");
    if(d.scissorEnable)need(d.scissor[0]<d.scissor[2]&&d.scissor[1]<d.scissor[3]&&d.scissor[2]<=target->width&&d.scissor[3]<=target->height,"Ball effect scissor exceeds target");
    for(const auto& row:d.constants)for(float v:row)need(std::isfinite(v),"Ball effect constant is nonfinite");
    for(const auto& vertex:d.vertices)for(float v:vertex)need(std::isfinite(v),"Ball effect vertex is nonfinite");
    const auto& sm=d.sampler;
    need((sm.Filter==D3D11_FILTER_MIN_MAG_MIP_POINT||sm.Filter==D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT||sm.Filter==D3D11_FILTER_MIN_MAG_MIP_LINEAR)&&
         sm.AddressU>=1&&sm.AddressU<=4&&sm.AddressV>=1&&sm.AddressV<=4&&sm.AddressW>=1&&sm.AddressW<=4&&
         sm.MaxAnisotropy==1&&sm.ComparisonFunc==D3D11_COMPARISON_NEVER&&sm.MipLODBias==0&&std::isfinite(sm.MinLOD)&&std::isfinite(sm.MaxLOD)&&sm.MinLOD<=sm.MaxLOD,
         "Ball effect sampler is unqualified");
    for(float v:sm.BorderColor)need(std::isfinite(v),"Ball effect sampler border is nonfinite");
    flushIm2D();requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);noAuxiliaryStages(context.Get(),device->GetFeatureLevel());
    if(!ballEffectPipeline)ballEffectPipeline=std::make_shared<BallEffectPipeline>(device.Get());
    auto& p=*ballEffectPipeline;p.surfaces(device.Get(),target->pixelWidth(),target->pixelHeight());
    const uint32_t depthWords[]={uint32_t(d.viewport[4]!=0),0,0,0};
    const uint32_t drawWords[]={d.blendWord,std::bit_cast<uint32_t>(float(d.alphaReference)/255.0f),d.blendEnable,d.alphaTest};
    // Original QUADLIST perimeter order; expanding avoids mutating the retained index buffer.
    const std::array<std::array<float,4>,6> triangles{d.vertices[0],d.vertices[1],d.vertices[2],d.vertices[0],d.vertices[2],d.vertices[3]};
    auto vertices=upload(device.Get(),triangles.data(),sizeof(triangles),D3D11_BIND_VERTEX_BUFFER);
    auto vc=upload(device.Get(),d.constants.data(),sizeof(d.constants),D3D11_BIND_CONSTANT_BUFFER);
    auto dc=upload(device.Get(),depthWords,sizeof(depthWords),D3D11_BIND_CONSTANT_BUFFER);
    auto pc=upload(device.Get(),drawWords,sizeof(drawWords),D3D11_BIND_CONSTANT_BUFFER);
    ComPtr<ID3D11DepthStencilState> depthState;D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=d.depthEnable;
    dd.DepthWriteMask=d.depthWrite?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;dd.DepthFunc=D3D11_COMPARISON_FUNC(d.depthCompare+1);
    check(device->CreateDepthStencilState(&dd,&depthState),"depth state");
    ComPtr<ID3D11BlendState> blend;D3D11_BLEND_DESC bd{};bd.RenderTarget[0].BlendEnable=FALSE;
    bd.RenderTarget[0].SrcBlend=D3D11_BLEND_ONE;bd.RenderTarget[0].DestBlend=D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;bd.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO;bd.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask=UINT8(d.colorMask);
    check(device->CreateBlendState(&bd,&blend),"packed color state");
    ComPtr<ID3D11RasterizerState> raster;D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;rd.ScissorEnable=d.scissorEnable;
    check(device->CreateRasterizerState(&rd,&raster),"raster state");ComPtr<ID3D11SamplerState> sampler;
    check(device->CreateSamplerState(&sm,&sampler),"texture sampler");
    {
        Restore restore(context.Get());restore.clearResources();context->OMSetRenderTargets(0,nullptr,nullptr);
        context->CopyResource(p.packed.Get(),target->texture.Get());context->CopyResource(p.destination.Get(),target->texture.Get());
        auto* vb=vertices.Get();UINT stride=16,offset=0;context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        context->IASetInputLayout(p.layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(p.vertex.Get(),nullptr,0);context->PSSetShader(p.pixel.Get(),nullptr,0);
        auto* cb=vc.Get();context->VSSetConstantBuffers(0,1,&cb);cb=dc.Get();context->PSSetConstantBuffers(1,1,&cb);cb=pc.Get();context->PSSetConstantBuffers(2,1,&cb);
        auto* texture=d.texture->view.Get();context->PSSetShaderResources(0,1,&texture);auto* sample=sampler.Get();context->PSSetSamplers(0,1,&sample);
        auto* destination=p.sampled.Get();context->PSSetShaderResources(1,1,&destination);
        const auto vp=renderViewport(target,{0,0,float(target->width),float(target->height),0,1});context->RSSetViewports(1,&vp);
        if(d.scissorEnable){const auto sc=renderScissor(target,{LONG(d.scissor[0]),LONG(d.scissor[1]),LONG(d.scissor[2]),LONG(d.scissor[3])});context->RSSetScissorRects(1,&sc);}
        context->RSSetState(raster.Get());context->OMSetDepthStencilState(depthState.Get(),0);context->OMSetBlendState(blend.Get(),nullptr,d.multisampleMask);
        auto* output=p.output.Get();context->OMSetRenderTargets(1,&output,depth->view.Get());context->Draw(6,0);
        context->OMSetRenderTargets(0,nullptr,nullptr);context->CopyResource(target->texture.Get(),p.packed.Get());++ballEffectDraws;
    }
    requireOwner();
}
}
