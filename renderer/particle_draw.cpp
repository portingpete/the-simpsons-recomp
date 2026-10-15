#include "particle_draw.h"
#include "VSParticle.h"
#include "VSParticleType5.h"
#include "PSParticleDraw.h"
#include "PSParticleSnapshotDraw.h"
#include "PSParticleUNormDraw.h"
#include "PSParticleProjectedUNormDraw.h"
#include "PSParticleDualUNormDraw.h"
#include "PSParticleDualProjectedUNormDraw.h"
#include "PSParticleProjectedDraw.h"
#include "PSParticleDualDraw.h"
#include "PSParticleDualProjectedDraw.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Simpsons::Graphics {
namespace {
void need(bool ok,const char* why){if(!ok)throw Error(why);}
void check(HRESULT hr,const char* why){if(FAILED(hr)){char text[180];std::snprintf(text,sizeof(text),"Native particle %s: %08lX",why,ULONG(hr));throw Error(text);}}
ComPtr<ID3D11Buffer> upload(ID3D11Device* device,const void* bytes,UINT size,UINT bind){
    D3D11_BUFFER_DESC d{};d.ByteWidth=size;d.BindFlags=bind;d.Usage=D3D11_USAGE_IMMUTABLE;
    const D3D11_SUBRESOURCE_DATA initial{bytes,0,0};ComPtr<ID3D11Buffer> result;
    check(device->CreateBuffer(&d,&initial,&result),"owned upload");return result;
}
// Direct particle draws bypass the cached material path. Restore the actual
// bindings so its retained native owners remain valid after submission.
struct Restore {
    ID3D11DeviceContext* c;
    ComPtr<ID3D11DeviceContext1> ranged;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> vb,ib;ComPtr<ID3D11InputLayout> layout;
    detail::ConstantBufferBinding vc,pc1,pc2;
    ComPtr<ID3D11ShaderResourceView> textures[4];ComPtr<ID3D11SamplerState> samplers[3];
    ComPtr<ID3D11DepthStencilState> depth;ComPtr<ID3D11BlendState> blend;ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11RenderTargetView> target;ComPtr<ID3D11DepthStencilView> dsv;
    std::array<D3D11_VIEWPORT,16> viewports{};D3D11_PRIMITIVE_TOPOLOGY topology{};DXGI_FORMAT format{};
    UINT stride{},offset{},indexOffset{},stencil{},sampleMask{},viewportCount{16};FLOAT factors[4]{};
    Restore(ID3D11DeviceContext* context,ComPtr<ID3D11DeviceContext1> rangedContext):c(context),ranged(std::move(rangedContext)){
        c->VSGetShader(&vs,nullptr,nullptr);c->PSGetShader(&ps,nullptr,nullptr);
        vc=detail::captureConstantBuffer(c,ranged.Get(),detail::ConstantBufferStage::Vertex,0);
        pc1=detail::captureConstantBuffer(c,ranged.Get(),detail::ConstantBufferStage::Pixel,1);
        pc2=detail::captureConstantBuffer(c,ranged.Get(),detail::ConstantBufferStage::Pixel,2);
        c->IAGetVertexBuffers(0,1,&vb,&stride,&offset);c->IAGetIndexBuffer(&ib,&format,&indexOffset);
        c->IAGetInputLayout(&layout);c->IAGetPrimitiveTopology(&topology);
        // Get adds one reference per non-null slot; Attach adopts it. Capture
        // the same exact ranges, including null slots, in two context calls.
        std::array<ID3D11ShaderResourceView*,4> views{};
        std::array<ID3D11SamplerState*,3> samples{};
        c->PSGetShaderResources(0,UINT(views.size()),views.data());
        c->PSGetSamplers(0,UINT(samples.size()),samples.data());
        for(size_t i=0;i<views.size();++i)textures[i].Attach(views[i]);
        for(size_t i=0;i<samples.size();++i)samplers[i].Attach(samples[i]);
        c->OMGetDepthStencilState(&depth,&stencil);c->OMGetBlendState(&blend,factors,&sampleMask);c->RSGetState(&raster);
        c->RSGetViewports(&viewportCount,viewports.data());c->OMGetRenderTargets(1,&target,&dsv);
    }
    ~Restore(){
        ID3D11ShaderResourceView* nulls[4]{};c->PSSetShaderResources(0,4,nulls);
        auto* rt=target.Get();c->OMSetRenderTargets(1,&rt,dsv.Get());
        c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);
        detail::bindConstantBuffer(c,ranged.Get(),detail::ConstantBufferStage::Vertex,0,vc);
        detail::bindConstantBuffer(c,ranged.Get(),detail::ConstantBufferStage::Pixel,1,pc1);
        detail::bindConstantBuffer(c,ranged.Get(),detail::ConstantBufferStage::Pixel,2,pc2);
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
struct ParticlePipeline {
    ComPtr<ID3D11VertexShader> vertex,type5Vertex;ComPtr<ID3D11PixelShader> pixel,projectedPixel,dualPixel,dualProjectedPixel;ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11PixelShader> snapshotPixel;
    ComPtr<ID3D11PixelShader> unormPixel,unormProjectedPixel,unormDualPixel,unormDualProjectedPixel;
    ComPtr<ID3D11DepthStencilState> snapshotDepth;
    ComPtr<ID3D11BlendState> snapshotBlend;
    ComPtr<ID3D11ShaderResourceView> packedSampled;
    ComPtr<ID3D11RenderTargetView> destinationOutput;
    ComPtr<ID3D11Buffer> indices;
    ComPtr<ID3D11Device> ownerDevice;
    detail::DirectDrawDepthStates depths;
    ComPtr<ID3D11Buffer> streamedVertices,streamedVertexConstants,streamedPixelConstants;
    UINT vertexCapacity=0,vertexConstantCapacity=0,pixelConstantCapacity=0;
    void requireDevice(ID3D11Device* device) const {
        need(ownerDevice.Get()==device,"Particle pipeline belongs to a different device");
    }
    ComPtr<ID3D11Buffer> writeStream(ID3D11Device* device,ID3D11DeviceContext* context,
        const void* bytes,UINT size,UINT bind,ComPtr<ID3D11Buffer>& buffer,UINT& capacity,UINT maximum) {
        requireDevice(device);
        need(context&&bytes&&size&&size<=maximum,"Particle streamed upload extent is invalid");
        need(bind==D3D11_BIND_VERTEX_BUFFER||bind==D3D11_BIND_CONSTANT_BUFFER,"Particle streamed upload binding is invalid");
        auto written=buffer;UINT nextCapacity=capacity;
        if(!written||capacity<size){
            // Grow for accepted packets only; constant buffer extents stay exact.
            const UINT rounded=bind==D3D11_BIND_VERTEX_BUFFER?std::bit_ceil(size):size;
            nextCapacity=rounded>maximum?maximum:rounded;
            D3D11_BUFFER_DESC bd{};bd.ByteWidth=nextCapacity;bd.BindFlags=bind;
            bd.Usage=D3D11_USAGE_DYNAMIC;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            ComPtr<ID3D11Buffer> created;
            check(device->CreateBuffer(&bd,nullptr,&created),"stream allocation");
            need(bool(created),"Particle stream allocation returned no buffer");written=std::move(created);
        }
        // DISCARD preserves earlier submitted versions; there is no application
        // cursor, published-range reuse, or NO_OVERWRITE authority.
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(written.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"stream map");
        if(!mapped.pData){context->Unmap(written.Get(),0);throw Error("Particle stream Map returned no storage");}
        std::memcpy(mapped.pData,bytes,size);context->Unmap(written.Get(),0);
        buffer=written;capacity=nextCapacity;return written;
    }
    ComPtr<ID3D11Texture2D> packed,destination;
    ComPtr<ID3D11RenderTargetView> output;ComPtr<ID3D11ShaderResourceView> sampled;
    UINT width{},height{};
    explicit ParticlePipeline(ID3D11Device* device):ownerDevice(device){
        check(device->CreateVertexShader(kVSParticle,sizeof(kVSParticle),nullptr,&vertex),"vertex shader");
        check(device->CreateVertexShader(kVSParticleType5,sizeof(kVSParticleType5),nullptr,&type5Vertex),"type-5 vertex shader");
        check(device->CreatePixelShader(kPSParticleDraw,sizeof(kPSParticleDraw),nullptr,&pixel),"pixel shader");
        check(device->CreatePixelShader(kPSParticleProjectedDraw,sizeof(kPSParticleProjectedDraw),nullptr,&projectedPixel),"projected pixel shader");
        check(device->CreatePixelShader(kPSParticleDualDraw,sizeof(kPSParticleDualDraw),nullptr,&dualPixel),"dual pixel shader");
        check(device->CreatePixelShader(kPSParticleDualProjectedDraw,sizeof(kPSParticleDualProjectedDraw),nullptr,&dualProjectedPixel),"dual/projected pixel shader");
        const D3D11_INPUT_ELEMENT_DESC elements[]={
            {"TEXCOORD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",3,DXGI_FORMAT_R32G32B32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",4,DXGI_FORMAT_R32G32B32A32_FLOAT,0,60,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",5,DXGI_FORMAT_R32_FLOAT,0,76,D3D11_INPUT_PER_VERTEX_DATA,0}};
        check(device->CreateInputLayout(elements,6,kVSParticle,sizeof(kVSParticle),&layout),"input layout");
        constexpr uint16_t corners[]={0,1,2,0,2,3};indices=upload(device,corners,sizeof(corners),D3D11_BIND_INDEX_BUFFER);
    }
    void ensureSparseSnapshots(ID3D11Device* device){
        if(snapshotPixel)return;
        ComPtr<ID3D11PixelShader> pixel,ordinary,projected,dual,dualProjected;ComPtr<ID3D11DepthStencilState> depth;ComPtr<ID3D11BlendState> blend;
        check(device->CreatePixelShader(kPSParticleSnapshotDraw,sizeof(kPSParticleSnapshotDraw),nullptr,&pixel),"quad snapshot shader");
        check(device->CreatePixelShader(kPSParticleUNormDraw,sizeof(kPSParticleUNormDraw),nullptr,&ordinary),"UNORM particle shader ordinary");
        check(device->CreatePixelShader(kPSParticleProjectedUNormDraw,sizeof(kPSParticleProjectedUNormDraw),nullptr,&projected),"UNORM particle shader projected");
        check(device->CreatePixelShader(kPSParticleDualUNormDraw,sizeof(kPSParticleDualUNormDraw),nullptr,&dual),"UNORM particle shader dual");
        check(device->CreatePixelShader(kPSParticleDualProjectedUNormDraw,sizeof(kPSParticleDualProjectedUNormDraw),nullptr,&dualProjected),"UNORM particle shader dualProjected");
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=FALSE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;dd.DepthFunc=D3D11_COMPARISON_ALWAYS;
        check(device->CreateDepthStencilState(&dd,&depth),"quad snapshot depth state");
        D3D11_BLEND_DESC bd{};bd.RenderTarget[0].BlendEnable=FALSE;
        bd.RenderTarget[0].SrcBlend=D3D11_BLEND_ONE;bd.RenderTarget[0].DestBlend=D3D11_BLEND_ZERO;bd.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;bd.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO;bd.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        check(device->CreateBlendState(&bd,&blend),"quad snapshot color state");
        // Publish the complete direct-path pack only after every creation succeeds.
        snapshotPixel=std::move(pixel);snapshotDepth=std::move(depth);snapshotBlend=std::move(blend);
        unormPixel=std::move(ordinary);unormProjectedPixel=std::move(projected);
        unormDualPixel=std::move(dual);unormDualProjectedPixel=std::move(dualProjected);
    }
    void surfaces(ID3D11Device* device,UINT w,UINT h){
        if(width==w&&height==h)return;
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.SampleDesc={1,0};
        d.Format=DXGI_FORMAT_R10G10B10A2_TYPELESS;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> p,t;ComPtr<ID3D11RenderTargetView> r,destinationRtv;ComPtr<ID3D11ShaderResourceView> s,packedSrv;
        check(device->CreateTexture2D(&d,nullptr,&p),"packed output");
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;check(device->CreateTexture2D(&d,nullptr,&t),"sampled destination");
        D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=DXGI_FORMAT_R10G10B10A2_UINT;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        check(device->CreateRenderTargetView(p.Get(),&rd,&r),"packed output view");
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R10G10B10A2_UINT;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels=1;
        check(device->CreateShaderResourceView(t.Get(),&sd,&s),"destination view");
        check(device->CreateShaderResourceView(p.Get(),&sd,&packedSrv),"packed snapshot source view");
        check(device->CreateRenderTargetView(t.Get(),&rd,&destinationRtv),"quad snapshot output view");
        packed=std::move(p);destination=std::move(t);output=std::move(r);sampled=std::move(s);
        packedSampled=std::move(packedSrv);destinationOutput=std::move(destinationRtv);width=w;height=h;
    }
};
void NativeBackend::drawParticles(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,const ParticleDraw& d){
    drawParticlesImpl(target,depth,d,true);
}
void NativeBackend::drawParticlesImpl(const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,const ParticleDraw& d,bool sparseSnapshots){
    validateSubmissionContext();validateFrontTarget(target);validateDepthCopyTarget(depth);validateTexture(d.texture);
    need(target->format==TargetFormat::RGB10A2,"Particles require original RGB10A2 color");
    need(d.vertices.size()&&!(d.vertices.size()%4)&&d.vertices.size()<=16384,"Particle quad extent is invalid");
    need(d.depthEnable<=1&&d.depthWrite<=1&&d.depthCompare<=7&&d.colorMask<=15,"Particle depth/color state is invalid");
    need(!d.stencilEnable&&!d.cull&&!d.fill&&!d.alphaToMask&&!d.clipPlaneEnable&&d.viewportEnable==1&&d.halfPixelOffset==1,
         "Particle stencil/cull/fill/clip/viewport state is unqualified");
    need(d.blendEnable<=1&&d.alphaReference<=255&&(d.blendWord==0x10106||d.blendWord==0x10186||d.blendWord==0x10706),"Particle blend/alpha state is unqualified");
    need(!d.depthBiasBits&&!d.slopeBiasBits,"Particle polygon bias is unqualified");
    need(d.scissorEnable<=1&&(d.multisampleMask==0xFFFFFFFF||d.multisampleMask==0xFFFF),"Particle scissor/sample mask is unqualified");
    need(d.viewport[0]==0&&d.viewport[1]==0&&d.viewport[2]==target->width&&d.viewport[3]==target->height&&
         ((d.viewport[4]==0&&d.viewport[5]==0x3f800000)||(d.viewport[4]==0x3f800000&&d.viewport[5]==0)),"Particle viewport is unqualified");
    need(depth->width==target->width&&depth->height==target->height,"Particle attachment extents differ");
    const bool projected=d.shadowPolicy==ParticleShadowSamplePolicy::ReferenceD24FS8DepthRRRR;
    const bool dual=d.secondaryPolicy==ParticleSecondarySamplePolicy::DualTexture1LinearRepeat;
    const bool type5=d.vertexPolicy==ParticleVertexPolicy::Type5;
    need(type5||d.vertexPolicy==ParticleVertexPolicy::Ordinary,"Particle vertex policy is unqualified");
    const auto& baseSampler=d.sampler;
    const auto baseAddress=[](D3D11_TEXTURE_ADDRESS_MODE mode){
        return mode==D3D11_TEXTURE_ADDRESS_WRAP||mode==D3D11_TEXTURE_ADDRESS_CLAMP;
    };
    need(baseSampler.Filter==D3D11_FILTER_MIN_MAG_MIP_LINEAR&&baseAddress(baseSampler.AddressU)&&
         baseAddress(baseSampler.AddressV)&&baseSampler.AddressW==D3D11_TEXTURE_ADDRESS_WRAP&&
         baseSampler.MipLODBias==0&&baseSampler.MinLOD==0&&baseSampler.MaxLOD==13&&
         baseSampler.MaxAnisotropy==1&&baseSampler.ComparisonFunc==D3D11_COMPARISON_NEVER,
         "Particles require original linear sampling and independent wrap/clamp base UV addressing");
    for(float value:baseSampler.BorderColor)need(std::isfinite(value),"Particle base sampler border is nonfinite");
    need(projected||d.shadowPolicy==ParticleShadowSamplePolicy::None,"Particle shadow sampling policy is unqualified");
    need(dual||d.secondaryPolicy==ParticleSecondarySamplePolicy::None,"Particle secondary sampling policy is unqualified");
    if(projected) {
        validateDepthCopyTarget(d.shadow);
        need(d.shadow!=depth&&d.shadow->width==1024&&d.shadow->height==1024,"Projected particle shadow owner/extent is invalid");
        const auto& s=d.shadowSampler;
        need(s.Filter==D3D11_FILTER_MIN_MAG_MIP_POINT&&s.AddressU==D3D11_TEXTURE_ADDRESS_MIRROR&&
             s.AddressV==D3D11_TEXTURE_ADDRESS_MIRROR&&s.AddressW==D3D11_TEXTURE_ADDRESS_WRAP&&
             s.MipLODBias==0&&s.MinLOD==0&&s.MaxLOD==13&&s.MaxAnisotropy==1&&
             s.ComparisonFunc==D3D11_COMPARISON_NEVER,"Projected particles require original point/mirror sampling");
        need(d.constants[25][0]>=0,"Projected particle constants disable the required shadow interpolant");
    } else need(!d.shadow,"Ordinary particles received an unexpected shadow owner");
    if(dual) {
        validateTexture(d.secondaryTexture);const auto& s=d.secondarySampler;
        need(s.Filter==D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT&&s.AddressU==D3D11_TEXTURE_ADDRESS_WRAP&&
             s.AddressV==D3D11_TEXTURE_ADDRESS_WRAP&&s.AddressW==D3D11_TEXTURE_ADDRESS_WRAP&&
             s.MipLODBias==0&&s.MinLOD==0&&s.MaxLOD==13&&s.MaxAnisotropy==1&&
             s.ComparisonFunc==D3D11_COMPARISON_NEVER,"Dual particles require original linear/linear/point repeat sampling");
    } else need(!d.secondaryTexture,"Ordinary particles received an unexpected secondary texture");
    for(const auto& row:d.constants)for(float v:row)need(std::isfinite(v),"Particle constant is nonfinite");
    for(const auto& vertex:d.vertices){const auto* values=reinterpret_cast<const float*>(&vertex);for(unsigned i=0;i<20;++i)need(std::isfinite(values[i]),"Particle vertex is nonfinite");}
    flushIm2D();requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);
    UINT viewportCount=0;context->RSGetViewports(&viewportCount,nullptr);need(viewportCount==1,"Particles require one retained viewport");
    if(d.scissorEnable){const auto retained=scissor();need(retained&&(*retained)[0]<(*retained)[2]&&(*retained)[1]<(*retained)[3]&&
        (*retained)[2]<=target->width&&(*retained)[3]<=target->height,"Particle retained scissor is invalid");}
    ComPtr<ID3D11Predicate> predicate;BOOL predicateValue{};context->GetPredication(&predicate,&predicateValue);
    need(!predicate,"Particle predicated submission is unqualified");
    ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;
    context->GSGetShader(&gs,nullptr,nullptr);context->HSGetShader(&hs,nullptr,nullptr);context->DSGetShader(&ds,nullptr,nullptr);
    need(!gs&&!hs&&!ds,"Particle submission has unrelated programmable stages");
    // These direct draws replace OM targets, so unrelated output UAVs must be
    // rejected before submission instead of being silently removed by D3D11.
    std::array<ID3D11UnorderedAccessView*,64> outputs{};
    const UINT outputCount=device->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?64u:8u;
    context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,outputCount,outputs.data());
    bool hasUnorderedOutput=false;for(auto* output:outputs)if(output){hasUnorderedOutput=true;output->Release();}
    need(!hasUnorderedOutput,"Particle submission has unrelated output UAVs");
    if(!particlePipeline)particlePipeline=std::make_shared<ParticlePipeline>(device.Get());auto& p=*particlePipeline;
    p.requireDevice(device.Get());
    p.surfaces(device.Get(),target->pixelWidth(),target->pixelHeight());
    const bool sparse=sparseSnapshots;if(sparse)p.ensureSparseSnapshots(device.Get());
    const uint32_t depthWords[]={uint32_t(d.viewport[4]!=0),0,0,0};
    const uint32_t drawWords[]={d.blendWord,std::bit_cast<uint32_t>(float(d.alphaReference)/255.0f),d.blendEnable,0};
    ComPtr<ID3D11Buffer> vertices,vc,dc,pc;
    ComPtr<ID3D11DepthStencilState> depthState;
    if(sparseSnapshots){
        vertices=p.writeStream(device.Get(),context.Get(),d.vertices.data(),UINT(d.vertices.size()*sizeof(ParticleVertex)),
            D3D11_BIND_VERTEX_BUFFER,p.streamedVertices,p.vertexCapacity,UINT(sizeof(ParticleVertex)*16384));
        vc=p.writeStream(device.Get(),context.Get(),d.constants.data(),sizeof(d.constants),
            D3D11_BIND_CONSTANT_BUFFER,p.streamedVertexConstants,p.vertexConstantCapacity,sizeof(d.constants));
        dc=sceneDepthConstantBuffer(uint32_t(d.viewport[4]!=0),0,0);
        pc=p.writeStream(device.Get(),context.Get(),drawWords,sizeof(drawWords),
            D3D11_BIND_CONSTANT_BUFFER,p.streamedPixelConstants,p.pixelConstantCapacity,sizeof(drawWords));
        check(p.depths.get(device.Get(),d.depthEnable,d.depthWrite,d.depthCompare,depthState),"depth state");
    }else{
        // Keep a real dedicated immutable upload/state-creation oracle.
        vertices=upload(device.Get(),d.vertices.data(),UINT(d.vertices.size()*sizeof(ParticleVertex)),D3D11_BIND_VERTEX_BUFFER);
        vc=upload(device.Get(),d.constants.data(),sizeof(d.constants),D3D11_BIND_CONSTANT_BUFFER);
        dc=upload(device.Get(),depthWords,sizeof(depthWords),D3D11_BIND_CONSTANT_BUFFER);
        pc=upload(device.Get(),drawWords,sizeof(drawWords),D3D11_BIND_CONSTANT_BUFFER);
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=d.depthEnable;
        dd.DepthWriteMask=d.depthWrite?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;dd.DepthFunc=D3D11_COMPARISON_FUNC(d.depthCompare+1);
        check(device->CreateDepthStencilState(&dd,&depthState),"depth state");
    }
    auto blend=sceneBlendState(0,0x10001,d.colorMask);
    auto raster=sceneRasterState(0,d.scissorEnable);ComPtr<ID3D11SamplerState> sampler;
    sampler=sceneSamplerState(d.sampler);
    ComPtr<ID3D11SamplerState> shadowSampler,secondarySampler;
    if(projected)shadowSampler=sceneSamplerState(d.shadowSampler);
    if(dual)secondarySampler=sceneSamplerState(d.secondarySampler);
    {
        Restore restore(context.Get(),constantBindingContext());
        auto* vb=vertices.Get();UINT stride=sizeof(ParticleVertex),offset=0;context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        context->IASetInputLayout(p.layout.Get());context->IASetIndexBuffer(p.indices.Get(),DXGI_FORMAT_R16_UINT,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(type5?p.type5Vertex.Get():p.vertex.Get(),nullptr,0);
        auto* realPixel=projected?(dual?p.dualProjectedPixel.Get():p.projectedPixel.Get()):
                                    (dual?p.dualPixel.Get():p.pixel.Get());
        if(sparse)realPixel=projected?(dual?p.unormDualProjectedPixel.Get():p.unormProjectedPixel.Get()):
                                        (dual?p.unormDualPixel.Get():p.unormPixel.Get());
        context->PSSetShader(realPixel,nullptr,0);
        auto* cb=vc.Get();context->VSSetConstantBuffers(0,1,&cb);cb=dc.Get();context->PSSetConstantBuffers(1,1,&cb);cb=pc.Get();context->PSSetConstantBuffers(2,1,&cb);
        auto* texture=d.texture->view.Get();context->PSSetShaderResources(0,1,&texture);auto* sample=sampler.Get();context->PSSetSamplers(0,1,&sample);
        auto* secondary=dual?d.secondaryTexture->view.Get():nullptr;context->PSSetShaderResources(3,1,&secondary);
        auto* secondarySample=secondarySampler.Get();context->PSSetSamplers(1,1,&secondarySample);
        auto* shadow=projected?d.shadow->depthView.Get():nullptr;context->PSSetShaderResources(2,1,&shadow);
        auto* shadowSample=shadowSampler.Get();context->PSSetSamplers(2,1,&shadowSample);
        const auto vp=renderViewport(target,{0,0,float(target->width),float(target->height),0,1});context->RSSetViewports(1,&vp);
        context->RSSetState(raster.Get());context->OMSetDepthStencilState(depthState.Get(),0);context->OMSetBlendState(blend.Get(),nullptr,d.multisampleMask);
        // Quads may overlap, so every particle reads the preceding particle's
        // packed output. Both triangles of a quad share the same snapshot.
        context->OMSetRenderTargets(0,nullptr,nullptr);
        if(!sparse)context->CopyResource(p.packed.Get(),target->texture.Get());
        for(UINT first=0;first<d.vertices.size();first+=4){
            ID3D11ShaderResourceView* nullView=nullptr;context->PSSetShaderResources(1,1,&nullView);
            context->OMSetRenderTargets(0,nullptr,nullptr);
            if(sparse){
                // The original VS/rasterizer cover every possible real fragment.
                // Ignore depth/alpha/write-mask rejection while recovering exact
                // codes from the typed target; both triangles share pre-quad history.
                auto* source=target->sampledView.Get();context->PSSetShaderResources(1,1,&source);
                context->PSSetShader(p.snapshotPixel.Get(),nullptr,0);
                context->OMSetDepthStencilState(p.snapshotDepth.Get(),0);context->OMSetBlendState(p.snapshotBlend.Get(),nullptr,d.multisampleMask);
                auto* snapshot=p.destinationOutput.Get();context->OMSetRenderTargets(1,&snapshot,nullptr);
                context->DrawIndexed(6,0,INT(first));
                // Unbind target SRV and destination RTV before target becomes
                // an RTV and destination an SRV; restore actual depth/write mask.
                context->PSSetShaderResources(1,1,&nullView);context->OMSetRenderTargets(0,nullptr,nullptr);
                context->PSSetShader(realPixel,nullptr,0);
                context->OMSetDepthStencilState(depthState.Get(),0);context->OMSetBlendState(blend.Get(),nullptr,d.multisampleMask);
            }else context->CopyResource(p.destination.Get(),p.packed.Get());
            auto* view=p.sampled.Get();context->PSSetShaderResources(1,1,&view);
            auto* output=sparse?target->view.Get():p.output.Get();context->OMSetRenderTargets(1,&output,depth->view.Get());
            context->DrawIndexed(6,0,INT(first));
        }
        context->OMSetRenderTargets(0,nullptr,nullptr);
        if(!sparse)context->CopyResource(target->texture.Get(),p.packed.Get());
        ++particleDraws;
        noteTemporalSceneDraw(target,depth,d.colorMask,d.depthEnable&&d.depthWrite,false);
    }
    requireOwner();
}
}
