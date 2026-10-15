#include "renderer/particle_draw.h"
#include "header/test_graphics_device.h"
#include "renderer/device_availability.h"
#include <d3d11sdklayers.h>
#include <cstdio>
#include <cstring>
#include "VSParticleUNormProbe.h"
#include "PSParticleSnapshotDraw.h"
#include "PSParticleUNormRoundTripDraw.h"
namespace Simpsons::Graphics {
struct NativeIm2DProbe {
    static ID3D11DeviceContext* context(NativeBackend& b){return b.context.Get();}
    static ID3D11Device* device(NativeBackend& b){return b.device.Get();}
    static void replaceDevice(NativeBackend& b,const NativeBackend& other){b.device=other.device;b.context=other.context;b.featureLevel=other.featureLevel;b.availability=other.availability;}
    static void fullSnapshot(NativeBackend& b,const std::shared_ptr<RenderTarget>& color,
                             const std::shared_ptr<DepthTarget>& depth,const ParticleDraw& draw){
        b.drawParticlesImpl(color,depth,draw,false);
    }
    static void debug(NativeBackend& b,bool hardware){
        if(testUsesVulkan())return; // D3D11 debug layer: a D3D11-only diagnostic.
        ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL level;
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        if(SUCCEEDED(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_DEBUG,levels,2,D3D11_SDK_VERSION,&d,&level,&c))){
            b.device=d;b.context=c;b.featureLevel=level;b.availability=std::make_shared<DeviceAvailability>(d.Get());
        }
    }
};
}
using namespace Simpsons::Graphics;
void need(bool condition,const char* message){if(!condition)throw Error(message);}
uint32_t word(const std::vector<uint8_t>& bytes,unsigned pixel){uint32_t v;std::memcpy(&v,bytes.data()+pixel*4,4);return v;}
struct ParticleSnapshotState {
    std::vector<uintptr_t> words;
    template<class T> void pointer(T* value){words.push_back(reinterpret_cast<uintptr_t>(value));if(value)value->Release();}
    void bytes(const void* value,size_t count){const auto* p=static_cast<const uint8_t*>(value);for(size_t i=0;i<count;++i)words.push_back(p[i]);}
    explicit ParticleSnapshotState(ID3D11DeviceContext* c){
        ComPtr<ID3D11DeviceContext1> ranged;c->QueryInterface(IID_PPV_ARGS(&ranged));
        ID3D11VertexShader* vs{};ID3D11PixelShader* ps{};c->VSGetShader(&vs,nullptr,nullptr);c->PSGetShader(&ps,nullptr,nullptr);pointer(vs);pointer(ps);
        for(auto stage:{detail::ConstantBufferStage::Vertex,detail::ConstantBufferStage::Pixel})for(UINT slot=0;slot<4;++slot){
            const auto binding=detail::captureConstantBuffer(c,ranged.Get(),stage,slot);
            words.push_back(reinterpret_cast<uintptr_t>(binding.buffer.Get()));words.push_back(binding.firstConstant);
            words.push_back(binding.numConstants);words.push_back(uintptr_t(binding.view));
        }
        ID3D11Buffer* vb{},*ib{};UINT stride{},offset{},indexOffset{};DXGI_FORMAT format{};
        c->IAGetVertexBuffers(0,1,&vb,&stride,&offset);c->IAGetIndexBuffer(&ib,&format,&indexOffset);pointer(vb);pointer(ib);
        words.insert(words.end(),{stride,offset,indexOffset,uintptr_t(format)});
        ID3D11InputLayout* layout{};D3D11_PRIMITIVE_TOPOLOGY topology{};c->IAGetInputLayout(&layout);c->IAGetPrimitiveTopology(&topology);pointer(layout);words.push_back(topology);
        for(UINT i=0;i<5;++i){ID3D11ShaderResourceView* view{};c->PSGetShaderResources(i,1,&view);pointer(view);}
        for(UINT i=0;i<4;++i){ID3D11SamplerState* sampler{};c->PSGetSamplers(i,1,&sampler);pointer(sampler);}
        ID3D11DepthStencilState* ds{};UINT stencil{};c->OMGetDepthStencilState(&ds,&stencil);pointer(ds);words.push_back(stencil);
        ID3D11BlendState* blend{};FLOAT factors[4]{};UINT mask{};c->OMGetBlendState(&blend,factors,&mask);pointer(blend);bytes(factors,sizeof(factors));words.push_back(mask);
        ID3D11RasterizerState* raster{};c->RSGetState(&raster);pointer(raster);
        std::array<ID3D11RenderTargetView*,8> targets{};ID3D11DepthStencilView* depth{};c->OMGetRenderTargets(8,targets.data(),&depth);
        for(auto* target:targets)pointer(target);pointer(depth);
        std::array<D3D11_VIEWPORT,16> viewports{};UINT count=16;c->RSGetViewports(&count,viewports.data());words.push_back(count);bytes(viewports.data(),count*sizeof(viewports[0]));
        std::array<D3D11_RECT,16> scissors{};count=16;c->RSGetScissorRects(&count,scissors.data());words.push_back(count);bytes(scissors.data(),count*sizeof(scissors[0]));
    }
    bool operator==(const ParticleSnapshotState& other)const{return words==other.words;}
};
// Each conversion is checked independently against literal packed-code gold.
// Thus an encode/decode error cannot cancel itself in the combined roundtrip.
void particleUNormConversionContracts(bool hardware){
    NativeBackend b(!hardware);NativeIm2DProbe::debug(b,hardware);
    constexpr UINT width=1024,height=5;
    auto source=b.createTarget(width,height,TargetFormat::RGB10A2);
    auto result=b.createTarget(width,height,TargetFormat::RGB10A2);
    auto* c=NativeIm2DProbe::context(b);auto* device=NativeIm2DProbe::device(b);
    std::vector<uint32_t> gold(size_t(width)*height);
    for(UINT x=0;x<width;++x){
        gold[x]=x|(173u<<10)|(719u<<20)|(2u<<30);
        gold[width+x]=611u|(x<<10)|(251u<<20)|(1u<<30);
        gold[2*width+x]=937u|(47u<<10)|(x<<20)|(3u<<30);
        gold[3*width+x]=413u|(831u<<10)|(97u<<20)|((x&3u)<<30);
        gold[4*width+x]=x|(((37u*x+11u)&1023u)<<10)|((1023u-x)<<20)|((x&3u)<<30);
    }
    std::vector<uint8_t> goldBytes(gold.size()*sizeof(uint32_t));
    std::memcpy(goldBytes.data(),gold.data(),goldBytes.size());
    b.bindTargets({source,nullptr,nullptr,nullptr},nullptr);
    ComPtr<ID3D11RenderTargetView> sourceRtv;c->OMGetRenderTargets(1,&sourceRtv,nullptr);
    ComPtr<ID3D11Resource> sourceResource;sourceRtv->GetResource(&sourceResource);
    ComPtr<ID3D11ShaderResourceView> sourceSrv;
    need(SUCCEEDED(device->CreateShaderResourceView(sourceResource.Get(),nullptr,&sourceSrv)),"UNORM probe source SRV creation failed");
    b.bindTargets({result,nullptr,nullptr,nullptr},nullptr);
    ComPtr<ID3D11RenderTargetView> resultRtv;c->OMGetRenderTargets(1,&resultRtv,nullptr);
    c->OMSetRenderTargets(0,nullptr,nullptr);
    c->UpdateSubresource(sourceResource.Get(),0,nullptr,gold.data(),width*4,0);
    ComPtr<ID3D11Texture2D> sourceTexture;sourceResource.As(&sourceTexture);
    D3D11_TEXTURE2D_DESC sourceDesc{};sourceTexture->GetDesc(&sourceDesc);
    need(sourceDesc.Format==DXGI_FORMAT_R10G10B10A2_UNORM&&sourceDesc.Width==width&&sourceDesc.Height==height&&
         sourceDesc.MipLevels==1&&sourceDesc.ArraySize==1&&sourceDesc.SampleDesc.Count==1,
         "UNORM probe source descriptor differs");
    D3D11_TEXTURE2D_DESC uintDesc=sourceDesc;uintDesc.Format=DXGI_FORMAT_R10G10B10A2_UINT;
    uintDesc.Usage=D3D11_USAGE_DEFAULT;uintDesc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    uintDesc.CPUAccessFlags=uintDesc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> codes;ComPtr<ID3D11RenderTargetView> codesRtv;ComPtr<ID3D11ShaderResourceView> codesSrv;
    need(SUCCEEDED(device->CreateTexture2D(&uintDesc,nullptr,&codes)),"UNORM probe UINT texture creation failed");
    need(SUCCEEDED(device->CreateRenderTargetView(codes.Get(),nullptr,&codesRtv)),"UNORM probe UINT RTV creation failed");
    need(SUCCEEDED(device->CreateShaderResourceView(codes.Get(),nullptr,&codesSrv)),"UNORM probe UINT SRV creation failed");
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> decode,encode;
    need(SUCCEEDED(device->CreateVertexShader(kVSParticleUNormProbe,sizeof(kVSParticleUNormProbe),nullptr,&vertex)),"UNORM probe VS creation failed");
    need(SUCCEEDED(device->CreatePixelShader(kPSParticleSnapshotDraw,sizeof(kPSParticleSnapshotDraw),nullptr,&decode)),"UNORM probe decode PS creation failed");
    need(SUCCEEDED(device->CreatePixelShader(kPSParticleUNormRoundTripDraw,sizeof(kPSParticleUNormRoundTripDraw),nullptr,&encode)),"UNORM probe encode PS creation failed");
    D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=FALSE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;dd.DepthFunc=D3D11_COMPARISON_ALWAYS;
    ComPtr<ID3D11DepthStencilState> depth;need(SUCCEEDED(device->CreateDepthStencilState(&dd,&depth)),"UNORM probe depth state creation failed");
    D3D11_BLEND_DESC bd{};bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    ComPtr<ID3D11BlendState> blend;need(SUCCEEDED(device->CreateBlendState(&bd,&blend)),"UNORM probe blend state creation failed");
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
    ComPtr<ID3D11RasterizerState> raster;need(SUCCEEDED(device->CreateRasterizerState(&rd,&raster)),"UNORM probe raster state creation failed");
    c->IASetInputLayout(nullptr);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(vertex.Get(),nullptr,0);c->OMSetDepthStencilState(depth.Get(),0);c->OMSetBlendState(blend.Get(),nullptr,0xFFFFFFFF);
    c->RSSetState(raster.Get());const D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};c->RSSetViewports(1,&viewport);
    c->PSSetShader(decode.Get(),nullptr,0);c->PSSetShaderResources(1,1,sourceSrv.GetAddressOf());
    c->OMSetRenderTargets(1,codesRtv.GetAddressOf(),nullptr);c->Draw(3,0);
    ID3D11ShaderResourceView* empty=nullptr;c->PSSetShaderResources(1,1,&empty);c->OMSetRenderTargets(0,nullptr,nullptr);
    auto stagingDesc=uintDesc;stagingDesc.Usage=D3D11_USAGE_STAGING;stagingDesc.BindFlags=0;stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;need(SUCCEEDED(device->CreateTexture2D(&stagingDesc,nullptr,&staging)),"UNORM probe staging creation failed");
    c->CopyResource(staging.Get(),codes.Get());std::vector<uint8_t> decoded(goldBytes.size());
    D3D11_MAPPED_SUBRESOURCE mapped{};need(SUCCEEDED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"UNORM probe staging Map failed");
    for(UINT y=0;y<height;++y)std::memcpy(decoded.data()+size_t(y)*width*4,static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch,width*4);
    c->Unmap(staging.Get(),0);
    need(decoded==goldBytes,"Typed UNORM Load did not recover independent RGB10A2 code gold");
    c->PSSetShader(encode.Get(),nullptr,0);c->PSSetShaderResources(1,1,codesSrv.GetAddressOf());
    c->OMSetRenderTargets(1,resultRtv.GetAddressOf(),nullptr);c->Draw(3,0);
    c->PSSetShaderResources(1,1,&empty);c->OMSetRenderTargets(0,nullptr,nullptr);
    need(b.readbackTarget(result)==goldBytes,"Quantized UINT codes did not encode to independent RGB10A2 gold");
    need(b.readbackTarget(source)==goldBytes,"UNORM conversion fixture changed its source codes");
    ComPtr<ID3D11InfoQueue> queue;device->QueryInterface(IID_PPV_ARGS(&queue));
    if(queue)for(UINT64 i=0;i<queue->GetNumStoredMessages();++i){SIZE_T n=0;queue->GetMessage(i,nullptr,&n);
        std::vector<uint8_t> bytes(n);auto* message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());queue->GetMessage(i,message,&n);
        if(message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"D3D11: %s\n",message->pDescription);throw Error("UNORM conversion D3D11 validation error");}}
    std::printf("Particle typed-UNORM conversion: independent decode/encode PASS, 1024 codes per RGB lane, all 4 alpha codes, 1024 mixed words\n");
}
// Compare every full descriptor, including disabled stencil fields, with the
// original zero-initialized creation rather than the different scene cache.
void particleDepthCacheContracts(ID3D11Device* device){
    detail::DirectDrawDepthStates cache;
    for(uint32_t key=0;key<32;++key){
        const uint32_t enable=(key>>4)&1,write=(key>>3)&1,compare=key&7;
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=enable;
        dd.DepthWriteMask=write?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;dd.DepthFunc=D3D11_COMPARISON_FUNC(compare+1);
        ComPtr<ID3D11DepthStencilState> original,cached,repeated;
        need(SUCCEEDED(device->CreateDepthStencilState(&dd,&original)),"Original direct depth creation failed");
        need(SUCCEEDED(cache.get(device,enable,write,compare,cached)),"Cached direct depth creation failed");
        need(SUCCEEDED(cache.get(device,enable,write,compare,repeated))&&cached.Get()==repeated.Get(),"Direct depth cache did not retain its exact tuple");
        D3D11_DEPTH_STENCIL_DESC oldDesc{},newDesc{};original->GetDesc(&oldDesc);cached->GetDesc(&newDesc);
        need(!std::memcmp(&oldDesc,&newDesc,sizeof(oldDesc)),"Direct depth cache changed the full disabled-stencil descriptor");
        ComPtr<ID3D11Device> owner;cached->GetDevice(&owner);need(owner.Get()==device,"Direct depth cache retained a foreign device");
        need(cache.get(device,2,write,compare,repeated)==E_INVALIDARG&&repeated.Get()==cached.Get(),"Invalid direct depth tuple changed a retained result");
    }
}
// Same-device oracle: force the retained whole-resource snapshot internally,
// without changing the original vertex/pixel shaders or caller's draw data.
void particleSnapshotContracts(bool hardware){
    size_t comparisons=0;
    for(const uint32_t scale:{100u,125u}){
        NativeBackend b(!hardware);NativeIm2DProbe::debug(b,hardware);
        b.configureRendering(1280,720,1,Antialiasing::Original,scale);
        constexpr UINT logicalW=32,logicalH=24;
        auto color=b.createTarget(logicalW,logicalH,TargetFormat::RGB10A2,TargetScale::Scene);
        auto depth=b.createDepthTarget(logicalW,logicalH,TargetScale::Scene);
        auto* c=NativeIm2DProbe::context(b);auto* device=NativeIm2DProbe::device(b);
        particleDepthCacheContracts(device);
        const UINT width=color->pixelWidth(),height=color->pixelHeight();
        b.bindTargets({color,nullptr,nullptr,nullptr},depth);b.setViewport({2,3,11,9,0,1});
        b.setScissor({7,5,24,19});c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        ParticleDraw base{};base.viewport={0,0,logicalW,logicalH,0,0x3F800000};
        base.depthEnable=base.depthWrite=1;base.depthCompare=6;base.viewportEnable=base.halfPixelOffset=1;
        base.colorMask=15;base.multisampleMask=0xFFFFFFFF;base.blendWord=0x10106;base.blendEnable=1;
        const uint8_t white[]={255,255,255,255};base.texture=b.createTexture(1,1,TextureFormat::RGBA8,white);
        base.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        base.sampler.AddressU=base.sampler.AddressV=base.sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        base.sampler.MaxLOD=13;base.sampler.MaxAnisotropy=1;base.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
        // Check exact mixed slots and the adjacent untouched slots throughout
        // the family, overlap and queued geometry differential cases.
        std::array<std::shared_ptr<Texture>,5> retainedTextures;
        std::array<ComPtr<ID3D11SamplerState>,4> retainedSamplers;
        for(UINT slot=0;slot<retainedTextures.size();++slot){
            retainedTextures[slot]=b.createTexture(1,1,TextureFormat::RGBA8,white);
            b.bindEngineTexture(slot,retainedTextures[slot]);
        }
        ID3D11ShaderResourceView* emptyView=nullptr;c->PSSetShaderResources(1,1,&emptyView);
        for(UINT slot=0;slot<retainedSamplers.size();++slot){
            auto desc=base.sampler;desc.AddressU=D3D11_TEXTURE_ADDRESS_MODE(D3D11_TEXTURE_ADDRESS_WRAP+slot);
            need(SUCCEEDED(device->CreateSamplerState(&desc,&retainedSamplers[slot])),"Particle retained sampler creation failed");
            auto* sample=slot==1?nullptr:retainedSamplers[slot].Get();c->PSSetSamplers(slot,1,&sample);
        }
        for(UINT row=0;row<4;++row){base.constants[row][row]=1;base.constants[4+row][row]=1;}
        base.constants[8]={1,1,1,1};base.constants[9][0]=1;base.constants[15]={255,255,255,255};
        base.constants[16]={1,1,1,63.75f};base.constants[17]={1,1,1000,0};
        base.constants[19][3]=255;base.constants[25][0]=-1;
        for(UINT quad=0;quad<3;++quad)for(UINT corner=0;corner<4;++corner){
            ParticleVertex v{};v.position={float(quad)*.25f-.25f,float(quad)*.125f-.125f,.5f,0};
            v.size={.4f,0,1};v.color={1,1,1,0};v.originalVertexId=float(corner);base.vertices.push_back(v);
        }
        auto shadow=b.createDepthTarget(1024,1024);b.clearDepthTarget(shadow,1,0);
        const uint8_t half[]={128,192,64,255};auto secondary=b.createTexture(1,1,TextureFormat::RGBA8,half);
        std::vector<uint32_t> background(size_t(width)*height);
        for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x)
            background[size_t(y)*width+x]=(32u+x*3)|((80u+y*5)<<10)|((150u+(x+y)%32)<<20)|(3u<<30);
        const auto execute=[&](const std::vector<ParticleDraw>& draws,bool full,float clearDepth){
            b.clearDepthTarget(depth,clearDepth,0x5A);
            ComPtr<ID3D11RenderTargetView> rtv;ComPtr<ID3D11DepthStencilView> dsv;c->OMGetRenderTargets(1,&rtv,&dsv);
            ComPtr<ID3D11Resource> resource;rtv->GetResource(&resource);c->OMSetRenderTargets(0,nullptr,nullptr);
            c->UpdateSubresource(resource.Get(),0,nullptr,background.data(),width*4,0);c->OMSetRenderTargets(1,rtv.GetAddressOf(),dsv.Get());
            for(const auto& draw:draws){
                const auto state=ParticleSnapshotState(c);const auto vertices=draw.vertices;const auto count=b.particleDrawCount();
                if(full)NativeIm2DProbe::fullSnapshot(b,color,depth,draw);else b.drawParticles(color,depth,draw);
                need(count+1==b.particleDrawCount(),"Particle snapshot changed submission count");
                need(ParticleSnapshotState(c)==state,"Particle snapshot changed retained native state/ranges");
                need(!std::memcmp(vertices.data(),draw.vertices.data(),vertices.size()*sizeof(ParticleVertex)),"Particle snapshot changed caller vertices");
            }
            return std::pair{b.readbackTarget(color),b.readbackDepthTarget(depth)};
        };
        const auto compare=[&](const std::vector<ParticleDraw>& draws,float clearDepth=0.f,bool unchanged=false){
            const auto sparse=execute(draws,false,clearDepth),full=execute(draws,true,clearDepth);
            need(sparse.first==full.first,"Particle quad snapshot differs from full-copy packed color");
            need(sparse.second.size()==full.second.size()&&sparse.second.size()%8==0,"Particle snapshot depth extent differs");
            for(size_t i=0;i<sparse.second.size();i+=8)
                need(!std::memcmp(sparse.second.data()+i,full.second.data()+i,5),"Particle snapshot changed defined depth/stencil");
            need(word(sparse.first,0)==background[0],"Particle snapshot corrupted outside background");
            uint32_t clearBits{};std::memcpy(&clearBits,&clearDepth,4);
            need(word(sparse.second,0)==clearBits&&sparse.second[4]==0x5A,"Particle snapshot corrupted outside depth/stencil");
            if(unchanged)need(!std::memcmp(sparse.first.data(),background.data(),sparse.first.size()),"Invisible particle changed background");
            ++comparisons;return sparse;
        };
        const auto positive=compare({base});const UINT middle=(height/2)*width+width/2;
        need(word(positive.first,middle)!=background[middle],"Particle snapshot differential has no visible positive control");
        for(auto policy:{ParticleVertexPolicy::Ordinary,ParticleVertexPolicy::Type5})
        for(bool projected:{false,true})for(bool dual:{false,true})for(UINT blend:{0x10106u,0x10186u,0x10706u}){
            auto draw=base;draw.vertexPolicy=policy;draw.blendWord=blend;
            if(policy==ParticleVertexPolicy::Type5){draw.constants[18]={1,0,0,1};
                for(auto& v:draw.vertices){v.position[3]=1;v.velocity={.5f,.125f,0,0};}}
            if(projected){draw.shadowPolicy=ParticleShadowSamplePolicy::ReferenceD24FS8DepthRRRR;draw.shadow=shadow;
                draw.shadowSampler=base.sampler;draw.shadowSampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
                draw.shadowSampler.AddressU=draw.shadowSampler.AddressV=D3D11_TEXTURE_ADDRESS_MIRROR;
                draw.constants[24]={0,0,.5f,1};draw.constants[25][0]=.4f;}
            if(dual){draw.secondaryPolicy=ParticleSecondarySamplePolicy::DualTexture1LinearRepeat;draw.secondaryTexture=secondary;
                draw.secondarySampler=base.sampler;draw.secondarySampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;}
            compare({draw,draw}); // Includes multiple overlapping quads and fresh snapshots across calls.
            auto masked=draw;masked.colorMask=5;masked.scissorEnable=1;compare({draw,masked});
            auto rejected=draw;rejected.alphaReference=255;compare({rejected},0,true);
            compare({draw},1,true); // Depth rejects every real fragment; snapshot itself must not write depth.
        }
        auto offscreen=base;for(auto& v:offscreen.vertices)v.position[0]+=8;compare({offscreen},0,true);
        auto perspective=base;perspective.constants[0][3]=.25f;compare({perspective});
        auto subpixel=base;for(auto& v:subpixel.vertices){v.position[0]+=.013f;v.position[1]-=.027f;}compare({subpixel});
        auto moving=base;for(auto& v:moving.vertices)v.position[0]+=.2f;compare({base,moving,base});
        // No readback or wait occurs between these alternating accepted draws.
        // Both geometry and VS/PS constants vary, exposing stale DISCARD storage.
        auto left=base,right=base;left.vertices.resize(4);right.vertices.resize(4);
        for(auto& v:left.vertices){v.position[0]=-.6f;v.position[1]=0;v.size[0]=.15f;}
        for(auto& v:right.vertices){v.position[0]=.6f;v.position[1]=0;v.size[0]=.15f;}
        left.constants[15]={64,96,128,255};right.constants[15]={192,160,32,255};
        right.constants[16][0]=.75f;right.blendWord=0x10706;
        const auto queued=compare({left,right,left,right});
        need(word(queued.first,(height/2)*width+width/5)!=background[(height/2)*width+width/5]&&
             word(queued.first,(height/2)*width+4*width/5)!=background[(height/2)*width+4*width/5],
             "Queued particle DISCARD controls did not render both disjoint regions");
        auto grown=right;const auto quad=right.vertices;
        for(unsigned i=0;i<16;++i)grown.vertices.insert(grown.vertices.end(),quad.begin(),quad.end());
        compare({left,grown,right,left});
        auto disabled=base;disabled.blendEnable=0;disabled.depthEnable=disabled.depthWrite=0;compare({disabled});
        ComPtr<ID3D11InfoQueue> queue;device->QueryInterface(IID_PPV_ARGS(&queue));
        if(queue)for(UINT64 i=0;i<queue->GetNumStoredMessages();++i){SIZE_T n=0;queue->GetMessage(i,nullptr,&n);
            std::vector<uint8_t> bytes(n);auto* message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());queue->GetMessage(i,message,&n);
            if(message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"D3D11: %s\n",message->pDescription);throw Error("Particle snapshot D3D11 validation error");}}
        // Existing pipeline owners must reject a new device before binding or
        // mapping any old-device buffer. All demand inputs below use the new device.
        NativeBackend replacement(!hardware);NativeIm2DProbe::replaceDevice(b,replacement);
        auto freshColor=b.createTarget(logicalW,logicalH,TargetFormat::RGB10A2,TargetScale::Scene);
        auto freshDepth=b.createDepthTarget(logicalW,logicalH,TargetScale::Scene);
        b.bindTargets({freshColor,nullptr,nullptr,nullptr},freshDepth);
        b.setViewport({2,3,11,9,0,1});
        auto freshDraw=base;freshDraw.texture=b.createTexture(1,1,TextureFormat::RGBA8,white);
        const auto freshState=ParticleSnapshotState(NativeIm2DProbe::context(b));const auto freshCount=b.particleDrawCount();bool deviceRejected=false;
        try{b.drawParticles(freshColor,freshDepth,freshDraw);}catch(const Error& e){deviceRejected=std::strcmp(e.what(),"Particle pipeline belongs to a different device")==0;}
        need(deviceRejected&&b.particleDrawCount()==freshCount&&ParticleSnapshotState(NativeIm2DProbe::context(b))==freshState,
             "Particle stream accepted a foreign pipeline device or changed its bindings");
    }
    std::printf("PASS particle sparse/full snapshot GPU differentials: %zu cases\n",comparisons);
}
int main(int argc,char** argv){try{
    const bool hardware=argc==2&&std::strcmp(argv[1],"--hardware")==0;
    particleUNormConversionContracts(hardware);
    particleSnapshotContracts(hardware);
    NativeBackend b(!hardware);NativeIm2DProbe::debug(b,hardware);
    auto color=b.createTarget(16,16,TargetFormat::RGB10A2);auto depth=b.createDepthTarget(16,16);
    b.bindTargets({color,nullptr,nullptr,nullptr},depth);b.setViewport({0,0,16,16,0,1});
    b.clearTarget(color,{0,0,0,0});b.clearDepthTarget(depth,0,0);
    ParticleDraw d{};d.viewport={0,0,16,16,0,0x3F800000};d.depthEnable=d.depthWrite=1;d.depthCompare=6;
    d.viewportEnable=d.halfPixelOffset=1;d.colorMask=15;d.multisampleMask=0xFFFFFFFF;d.blendWord=0x10106;
    const uint8_t texel[]={255,255,255,255};d.texture=b.createTexture(1,1,TextureFormat::RGBA8,texel);
    d.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;d.sampler.AddressU=d.sampler.AddressV=d.sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    d.sampler.MaxLOD=13;d.sampler.MaxAnisotropy=1;d.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
    for(unsigned row=0;row<4;++row){d.constants[row][row]=1;d.constants[4+row][row]=1;}
    d.constants[8]={1,1,1,1};d.constants[9][0]=1;d.constants[15]={255,255,255,255};
    d.constants[16]={1,1,1,63.75};d.constants[17]={1,1,1000,0};d.constants[19][3]=255;d.constants[25][0]=-1;
    for(unsigned i=0;i<4;++i){ParticleVertex v{};v.position={0,0,.5,0};v.size={.5,0,1};v.color={16.0f/255,16.0f/255,16.0f/255,0};v.originalVertexId=float(i);d.vertices.push_back(v);}
    auto* context=NativeIm2DProbe::context(b);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    b.drawParticles(color,depth,d);auto pixels=b.readbackTarget(color);auto depths=b.readbackDepthTarget(depth);
    unsigned covered=0;
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){
        const auto v=word(pixels,y*16+x);const bool inside=x>=4&&x<12&&y>=4&&y<12;
        if(v)++covered;
        if(inside){
            // Identity projection, zero age/rotation, half-unit size. Normalized byte16
            // is scaled2/255 in VS, then doubled by the PS's DOT2ADD.
            if((v&1023)!=1||((v>>10)&1023)!=1||((v>>20)&1023)!=1||(v>>30)!=3){
                std::fprintf(stderr,"unexpected color x=%u y=%u code=%08X\n",x,y,v);throw Error("Particle original shader color differs");}
            need(word(depths,2*(y*16+x))==0x3F000000,"Particle depth differs");
        }else need(!v,"Particle coverage exceeds expected quad");
    }
    need(covered==64,"Particle quad did not cover its expected square");
    b.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);
    D3D11_PRIMITIVE_TOPOLOGY topology;context->IAGetPrimitiveTopology(&topology);need(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"Particle topology leaked");
    b.drawParticles(color,depth,d);need((word(b.readbackTarget(color),8*16+8)&1023)==2,"Particle blend lost preceding output");
    auto type5=d;type5.vertexPolicy=ParticleVertexPolicy::Type5;
    type5.blendEnable=0;
    b.clearTarget(color,{0,0,0,0});
    const auto type5Before=b.particleDrawCount();b.drawParticles(color,depth,type5);
    need(b.particleDrawCount()==type5Before+1,"Type-5 particle vertex shader did not submit");
    b.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);
    b.clearTarget(color,{0,0,0,0});b.drawParticles(color,depth,d);b.drawParticles(color,depth,d);
    d.alphaReference=255;b.drawParticles(color,depth,d);need((word(b.readbackTarget(color),8*16+8)&1023)==2,"Particle alpha reject modified color");
    d.alphaReference=0;d.blendEnable=0;b.drawParticles(color,depth,d);
    need((word(b.readbackTarget(color),8*16+8)&1023)==1,"Disabled particle blending did not replace destination");
    b.drawParticles(color,depth,d);need((word(b.readbackTarget(color),8*16+8)&1023)==1,"Disabled particle blending accumulated destination");
    const uint8_t opaqueBlack[]={0,0,0,255};auto black=d;
    black.texture=b.createTexture(1,1,TextureFormat::RGBA8,opaqueBlack);black.blendEnable=1;
    b.drawParticles(color,depth,black);
    need((word(b.readbackTarget(color),8*16+8)&0x3FFFFFFF)==0x00100401,
         "Additive particle black texel replaced the destination with a dark card");
    black.blendEnable=0;b.drawParticles(color,depth,black);
    need((word(b.readbackTarget(color),8*16+8)&0x3FFFFFFF)==0,
         "Disabled particle blending did not reproduce the opaque black card");
    auto invalid=d;invalid.vertices.pop_back();const auto before=b.particleDrawCount();bool rejected=false;
    try{b.drawParticles(color,depth,invalid);}catch(const Error&){rejected=true;}
    need(rejected&&b.particleDrawCount()==before,"Invalid particle extent submitted");
    auto dual=d;dual.secondaryPolicy=ParticleSecondarySamplePolicy::DualTexture1LinearRepeat;dual.secondaryTexture=d.texture;
    dual.secondarySampler=d.sampler;dual.secondarySampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    b.bindEngineTexture(3,d.texture);ComPtr<ID3D11ShaderResourceView> savedTexture3;context->PSGetShaderResources(3,1,&savedTexture3);
    ComPtr<ID3D11SamplerState> savedSampler1;D3D11_SAMPLER_DESC sentinel1=d.sampler;
    need(SUCCEEDED(NativeIm2DProbe::device(b)->CreateSamplerState(&sentinel1,&savedSampler1)),"Dual particle sentinel sampler creation failed");
    auto* sampler1=savedSampler1.Get();context->PSSetSamplers(1,1,&sampler1);
    const auto dualState=[&] {
        ComPtr<ID3D11ShaderResourceView> texture;ComPtr<ID3D11SamplerState> sampler;
        context->PSGetShaderResources(3,1,&texture);context->PSGetSamplers(1,1,&sampler);
        need(texture==savedTexture3&&sampler==savedSampler1,"Dual particle stage3 resource/stage1 sampler leaked");
        b.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);
    };
    b.clearTarget(color,{0,0,0,0});b.drawParticles(color,depth,dual);dualState();
    const uint32_t dualWhite=word(b.readbackTarget(color),8*16+8);need(dualWhite,"Dual particle white second texture produced no output");
    const uint8_t zeroTexel[]={0,0,0,0};dual.secondaryTexture=b.createTexture(1,1,TextureFormat::RGBA8,zeroTexel);
    b.clearTarget(color,{0,0,0,0});b.drawParticles(color,depth,dual);dualState();
    need(word(b.readbackTarget(color),8*16+8)==0,"Dual particle shader did not consume the second texture");
    const auto invalidDual=[&](const ParticleDraw& value) {
        const auto count=b.particleDrawCount();const auto pixelsBefore=b.readbackTarget(color);bool failed=false;
        try{b.drawParticles(color,depth,value);}catch(const Error&){failed=true;}
        need(failed&&b.particleDrawCount()==count&&pixelsBefore==b.readbackTarget(color),"Invalid dual particle submission mutated output/count");
        dualState();
    };
    invalid=dual;invalid.secondaryTexture.reset();invalidDual(invalid);
    invalid=dual;invalid.secondarySampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;invalidDual(invalid);
    auto projected=d;projected.shadowPolicy=ParticleShadowSamplePolicy::ReferenceD24FS8DepthRRRR;
    projected.shadow=b.createDepthTarget(1024,1024);projected.shadowSampler=d.sampler;
    projected.shadowSampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    projected.shadowSampler.AddressU=projected.shadowSampler.AddressV=D3D11_TEXTURE_ADDRESS_MIRROR;
    projected.constants[24]={0,0,.5f,1};projected.constants[25][0]=.4f;
    for(auto& vertex:projected.vertices)vertex.color={1,1,1,0};
    // Retained unrelated stage2 input and sampler must survive both variants.
    b.bindEngineTexture(2,d.texture);ComPtr<ID3D11ShaderResourceView> savedTexture;context->PSGetShaderResources(2,1,&savedTexture);
    ComPtr<ID3D11SamplerState> savedSampler;D3D11_SAMPLER_DESC sentinel=d.sampler;
    need(SUCCEEDED(NativeIm2DProbe::device(b)->CreateSamplerState(&sentinel,&savedSampler)),"Particle sentinel sampler creation failed");
    auto* sampler2=savedSampler.Get();context->PSSetSamplers(2,1,&sampler2);
    const auto shadowState=[&] {
        ComPtr<ID3D11ShaderResourceView> texture;ComPtr<ID3D11SamplerState> sampler;
        context->PSGetShaderResources(2,1,&texture);context->PSGetSamplers(2,1,&sampler);
        need(texture==savedTexture&&sampler==savedSampler,"Particle stage2 resource/sampler leaked");
        b.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);
    };
    b.clearDepthTarget(projected.shadow,0,0);b.drawParticles(color,depth,projected);shadowState();
    const uint32_t lit=word(b.readbackTarget(color),8*16+8);
    b.clearDepthTarget(projected.shadow,1,0);b.drawParticles(color,depth,projected);shadowState();
    const uint32_t shaded=word(b.readbackTarget(color),8*16+8);
    need((lit&1023)==16&&(shaded&1023)==6&&(lit>>30)==3&&(shaded>>30)==3,
         "Full particle VS/projected PS failed native shadow depth sampling or ambient/alpha");
    need(word(b.readbackDepthTarget(depth),2*(8*16+8))==0x3F000000,"Projected particles changed geometric depth");
    const auto invalidShadow=[&](const ParticleDraw& value) {
        const auto count=b.particleDrawCount();const auto pixelsBefore=b.readbackTarget(color);bool failed=false;
        try{b.drawParticles(color,depth,value);}catch(const Error&){failed=true;}
        need(failed&&b.particleDrawCount()==count&&pixelsBefore==b.readbackTarget(color),"Invalid projected particle submission mutated output/count");
        shadowState();
    };
    invalid=projected;invalid.shadow.reset();invalidShadow(invalid);
    invalid=projected;invalid.shadow=depth;invalidShadow(invalid);
    invalid=projected;invalid.shadowPolicy=ParticleShadowSamplePolicy::None;invalidShadow(invalid);
    invalid=projected;invalid.shadowSampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;invalidShadow(invalid);
    invalid=projected;invalid.shadowSampler.AddressU=D3D11_TEXTURE_ADDRESS_CLAMP;invalidShadow(invalid);
    invalid=projected;invalid.constants[25][0]=-1;invalidShadow(invalid);
    // The original registry independently combines two VS with four PS. Use
    // nondegenerate streak geometry for type5 (position.w is its trail extent),
    // and distinct retained stage2/stage3 sentinels to prove state restoration.
    unsigned matrixCases=0;
    const uint8_t secondHalf[]={128,128,128,255};
    const auto halfTexture=b.createTexture(1,1,TextureFormat::RGBA8,secondHalf);
    const auto blackTexture=b.createTexture(1,1,TextureFormat::RGBA8,opaqueBlack);
    for(auto vertexPolicy:{ParticleVertexPolicy::Ordinary,ParticleVertexPolicy::Type5})
    for(bool withShadow:{false,true})for(bool withDual:{false,true}) {
        auto family=d;family.vertexPolicy=vertexPolicy;family.blendEnable=0;family.alphaReference=0;
        for(auto& vertex:family.vertices) {
            vertex.color={1,1,1,0};
            if(vertexPolicy==ParticleVertexPolicy::Type5){vertex.position[3]=1;vertex.velocity={.5f,.125f,0,0};}
        }
        if(vertexPolicy==ParticleVertexPolicy::Type5)family.constants[18]={1,0,0,1};
        if(withShadow){family.shadowPolicy=projected.shadowPolicy;family.shadow=projected.shadow;
            family.shadowSampler=projected.shadowSampler;family.constants[24]=projected.constants[24];
            family.constants[25]=projected.constants[25];}
        if(withDual){family.secondaryPolicy=dual.secondaryPolicy;family.secondaryTexture=d.texture;
            family.secondarySampler=dual.secondarySampler;}
        const auto submit=[&](const ParticleDraw& value){
            b.clearTarget(color,{0,0,0,0});const auto count=b.particleDrawCount();
            b.drawParticles(color,depth,value);need(b.particleDrawCount()==count+1,"Particle family did not submit");
            shadowState();dualState();context->IAGetPrimitiveTopology(&topology);
            need(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"Particle family topology leaked");
            return word(b.readbackTarget(color),8*16+8);
        };
        b.clearDepthTarget(projected.shadow,0,0);const auto white=submit(family);
        need((white&1023)==16&&((white>>10)&1023)==16&&((white>>20)&1023)==16&&(white>>30)==3,
             "Particle family failed original visible geometry/color/alpha");
        for(auto u:{D3D11_TEXTURE_ADDRESS_WRAP,D3D11_TEXTURE_ADDRESS_CLAMP})
        for(auto v:{D3D11_TEXTURE_ADDRESS_WRAP,D3D11_TEXTURE_ADDRESS_CLAMP}) {
            auto addressed=family;addressed.sampler.AddressU=u;addressed.sampler.AddressV=v;
            need(submit(addressed)==white,"Particle family base wrap/clamp addressing failed");
        }
        need(word(b.readbackDepthTarget(depth),2*(8*16+8))==0x3F000000,
             "Particle family changed original geometric depth");
        auto blackBase=family;blackBase.texture=blackTexture;
        need((submit(blackBase)&0x3FFFFFFF)==0,"Particle family failed to consume base texture");
        const auto rejected=[&](const ParticleDraw& value){
            const auto count=b.particleDrawCount();const auto pixelsBefore=b.readbackTarget(color);
            bool failed=false;try{b.drawParticles(color,depth,value);}catch(const Error&){failed=true;}
            need(failed&&count==b.particleDrawCount()&&pixelsBefore==b.readbackTarget(color),
                 "Invalid particle family texture owner/sampler mutated output");shadowState();dualState();
        };
        auto badBase=family;badBase.texture.reset();rejected(badBase);
        badBase=family;badBase.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;rejected(badBase);
        badBase=family;badBase.sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;rejected(badBase);
        badBase=family;badBase.sampler.MaxLOD=0;rejected(badBase);
        if(withDual){auto changed=family;changed.secondaryTexture=halfTexture;
            need((submit(changed)&1023)==8,"Particle family failed to consume secondary texture");
            changed=family;changed.secondaryTexture.reset();rejected(changed);
            changed=family;changed.secondarySampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;rejected(changed);
            if(withShadow){changed=family;changed.shadow.reset();rejected(changed);
                changed=family;changed.shadow=depth;rejected(changed);
                changed=family;changed.shadowSampler.AddressV=D3D11_TEXTURE_ADDRESS_WRAP;rejected(changed);}
        }
        if(withShadow){b.clearDepthTarget(projected.shadow,1,0);const auto shadedFamily=submit(family);
            need((shadedFamily&1023)==6&&(shadedFamily>>30)==3,
                 "Particle family failed projected RRRR sampling or changed alpha");}
        ++matrixCases;
    }
    need(matrixCases==8,"Particle family matrix coverage changed");
    b.drawParticles(color,depth,d);shadowState();
    need((word(b.readbackTarget(color),8*16+8)&1023)==1,"Ordinary particle path changed after projected draw");
    if(NativeIm2DProbe::device(b)->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1){
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=16;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=4;
        ComPtr<ID3D11Buffer> buffer;need(SUCCEEDED(NativeIm2DProbe::device(b)->CreateBuffer(&desc,nullptr,&buffer)),"Particle high UAV storage creation failed");
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{};view.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;view.Buffer.NumElements=4;
        ComPtr<ID3D11UnorderedAccessView> uav;need(SUCCEEDED(NativeIm2DProbe::device(b)->CreateUnorderedAccessView(buffer.Get(),&view,&uav)),"Particle high UAV view creation failed");
        for(UINT slot:{1u,8u,63u}){
            auto* raw=uav.Get();context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,slot,1,&raw,nullptr);
            const auto pixelsBefore=b.readbackTarget(color);const auto depthBytesBefore=b.readbackDepthTarget(depth);const auto countBefore=b.particleDrawCount();
            bool rejectedUav=false;try{b.drawParticles(color,depth,d);}catch(const Error&){rejectedUav=true;}
            need(rejectedUav&&b.particleDrawCount()==countBefore,"Particle high UAV draw was not rejected before submission");
            ComPtr<ID3D11UnorderedAccessView> retained;context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,slot,1,&retained);
            need(retained==uav&&b.readbackTarget(color)==pixelsBefore&&b.readbackDepthTarget(depth)==depthBytesBefore,"Particle rejected high UAV draw changed state or attachments");
            ID3D11UnorderedAccessView* empty=nullptr;context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,slot,1,&empty,nullptr);
        }
    }
    ComPtr<ID3D11InfoQueue> queue;NativeIm2DProbe::device(b)->QueryInterface(IID_PPV_ARGS(&queue));
    if(queue)for(UINT64 i=0;i<queue->GetNumStoredMessages();++i){SIZE_T n=0;queue->GetMessage(i,nullptr,&n);std::vector<uint8_t> bytes(n);auto* message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());queue->GetMessage(i,message,&n);
        if(message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"D3D11: %s\n",message->pDescription);throw Error("Particle D3D11 validation error");}}
    std::puts("PASS particle shader geometry/color/depth/overlap/alpha/state and invalid submission");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL particle backend: %s\n",e.what());return 1;}}
