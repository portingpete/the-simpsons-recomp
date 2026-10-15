#include "renderer/native_backend.h"
#include "renderer/engine_state.h"
#include "renderer/d3d_call_stats.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <limits>
extern "C" uint64_t gD3DDeviceCounts[64];
using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void need(bool b,const char* why){++checks;if(!b)throw Error(why);}
template<class F>void rejects(F f){try{f();}catch(const Error&){++checks;return;}throw Error("Invalid original screen request was accepted");}
template<class F>void rejectsState(F f){try{f();}catch(const EngineStateError&){++checks;return;}throw Error("Invalid screen state was accepted");}
ScreenDraw quad(){ScreenDraw d{};d.vertices={{{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,-1,1,1}}};
    d.color={1,1,1,1};d.colorWriteMask=15;d.alphaTest=true;d.alphaReference=1.0f/255;
    d.sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;d.sampler.AddressU=d.sampler.AddressV=d.sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    d.sampler.MaxAnisotropy=1;d.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;return d;}
uint32_t pixel(const std::vector<uint8_t>& bytes,size_t i=0){uint32_t value{};std::memcpy(&value,bytes.data()+i*4,4);return value;}
std::array<float,4> unpack(uint32_t p){return {float(p&1023)/1023,float((p>>10)&1023)/1023,float((p>>20)&1023)/1023,float(p>>30)/3};}
uint32_t pack(const std::array<float,4>& p){uint32_t out=0;for(uint32_t i=0;i<4;++i)out|=uint32_t(std::nearbyint(std::clamp(p[i],0.0f,1.0f)*float(i==3?3:1023)))<<(i*10);return out;}
uint32_t blended(uint32_t destination,const std::array<float,4>& source,uint32_t mode){
    const auto before=unpack(destination);auto result=source;
    for(size_t c=0;c<3;++c){const float product=source[c]*source[3];
        result[c]=mode==3?source[c]:(mode==0?product+before[c]:(mode==1?product+before[c]*(1-source[3]):before[c]-product));}
    return pack(result);
}
void image(NativeBackend& b,const std::shared_ptr<RenderTarget>& target,const std::vector<uint32_t>& expected){
    const auto bytes=b.readbackTarget(target);need(bytes.size()==expected.size()*4,"Queued screen output extent differs");
    for(size_t i=0;i<expected.size();++i)need(pixel(bytes,i)==expected[i],"Queued screen scratch changed packed output or untouched coverage");
}
bool sameBlend(const D3D11_BLEND_DESC& a,const D3D11_BLEND_DESC& b){
    if(a.AlphaToCoverageEnable!=b.AlphaToCoverageEnable||a.IndependentBlendEnable!=b.IndependentBlendEnable)return false;
    for(size_t i=0;i<8;++i){const auto& x=a.RenderTarget[i];const auto& y=b.RenderTarget[i];
        if(x.BlendEnable!=y.BlendEnable||x.SrcBlend!=y.SrcBlend||x.DestBlend!=y.DestBlend||x.BlendOp!=y.BlendOp||
           x.SrcBlendAlpha!=y.SrcBlendAlpha||x.DestBlendAlpha!=y.DestBlendAlpha||x.BlendOpAlpha!=y.BlendOpAlpha||
           x.RenderTargetWriteMask!=y.RenderTargetWriteMask)return false;}
    return true;
}
bool sameDepth(const D3D11_DEPTH_STENCIL_DESC& a,const D3D11_DEPTH_STENCIL_DESC& b){
    auto face=[](const D3D11_DEPTH_STENCILOP_DESC& x,const D3D11_DEPTH_STENCILOP_DESC& y){
        return x.StencilFailOp==y.StencilFailOp&&x.StencilDepthFailOp==y.StencilDepthFailOp&&
            x.StencilPassOp==y.StencilPassOp&&x.StencilFunc==y.StencilFunc;};
    return a.DepthEnable==b.DepthEnable&&a.DepthWriteMask==b.DepthWriteMask&&a.DepthFunc==b.DepthFunc&&
        a.StencilEnable==b.StencilEnable&&a.StencilReadMask==b.StencilReadMask&&a.StencilWriteMask==b.StencilWriteMask&&
        face(a.FrontFace,b.FrontFace)&&face(a.BackFace,b.BackFace);
}
}
namespace Simpsons::Graphics {
struct NativeScreenProbe {
    static ID3D11Device* device(NativeBackend& b){return b.device.Get();}
    static ID3D11DeviceContext* context(NativeBackend& b){return b.context.Get();}
    static void cachedStates(bool hardware){
        NativeBackend b(!hardware);auto a=b.createTarget(32,16,TargetFormat::RGB10A2),other=b.createTarget(32,16,TargetFormat::RGB10A2);
        auto z=b.createDepthTarget(32,16);b.clearDepthTarget(z,.5f,0xA5);
        auto frontA=b.createTarget(32,16,TargetFormat::RGB10A2),frontOther=b.createTarget(32,16,TargetFormat::RGB10A2);
        auto query=b.createTarget(64,8,TargetFormat::RGB10A2);b.clearTarget(query,{1,1,1,1});
        const std::array<uint8_t,4> white={255,255,255,255};auto texture=b.createTexture(1,1,TextureFormat::RGBA8,white);
        auto draw=quad();draw.alphaTest=false;draw.texture=texture;
        const std::array<D3D11_SAMPLER_DESC,2> samplers={draw.sampler,[] {
            auto s=quad().sampler;s.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
            s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;return s;}()};
        std::array<ComPtr<ID3D11SamplerState>,2> samplerReferences;
        std::array<ComPtr<ID3D11BlendState>,128> blendReferences;
        std::array<ComPtr<ID3D11DepthStencilState>,32> depthReferences;
        auto blendReference=[&](bool original,uint32_t selector,uint32_t mask){
            D3D11_BLEND_DESC d{};auto& c=d.RenderTarget[0];c.BlendEnable=!original;
            c.SrcBlend=selector==3?D3D11_BLEND_ONE:D3D11_BLEND_SRC_ALPHA;
            c.DestBlend=selector==1?D3D11_BLEND_INV_SRC_ALPHA:(selector==3?D3D11_BLEND_ZERO:D3D11_BLEND_ONE);
            c.BlendOp=selector==2?D3D11_BLEND_OP_REV_SUBTRACT:D3D11_BLEND_OP_ADD;
            c.SrcBlendAlpha=D3D11_BLEND_ONE;c.DestBlendAlpha=D3D11_BLEND_ZERO;c.BlendOpAlpha=D3D11_BLEND_OP_ADD;
            c.RenderTargetWriteMask=UINT8(mask);const size_t index=(original?64u:0u)+selector*16u+mask;
            need(SUCCEEDED(b.device->CreateBlendState(&d,&blendReferences[index])),"Independent screen blend reference creation failed");
        };
        for(uint32_t selector=0;selector<4;++selector){for(uint32_t mask=0;mask<16;++mask)blendReference(false,selector,mask);
            blendReference(true,selector,15);}
        blendReference(true,3,8);
        for(uint32_t index=0;index<32;++index){D3D11_DEPTH_STENCIL_DESC d{};d.DepthEnable=bool(index&16);
            d.DepthWriteMask=index&8?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;d.DepthFunc=D3D11_COMPARISON_FUNC((index&7)+1);
            d.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};d.BackFace=d.FrontFace;
            need(SUCCEEDED(b.device->CreateDepthStencilState(&d,&depthReferences[index])),"Independent screen depth reference creation failed");}
        for(size_t i=0;i<samplers.size();++i)need(SUCCEEDED(b.device->CreateSamplerState(&samplers[i],&samplerReferences[i])),"Independent screen sampler reference creation failed");
        D3D11_SAMPLER_DESC querySampler{};querySampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        querySampler.AddressU=querySampler.AddressV=D3D11_TEXTURE_ADDRESS_MIRROR;querySampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        querySampler.MaxAnisotropy=1;querySampler.ComparisonFunc=D3D11_COMPARISON_NEVER;ComPtr<ID3D11SamplerState> queryReference;
        need(SUCCEEDED(b.device->CreateSamplerState(&querySampler,&queryReference)),"Independent query sampler reference creation failed");
        // This isolated test explicitly enables the existing diagnostic and
        // reads device vtable slots20/21/23. Pointer identity alone is not an
        // allocation oracle: D3D11 interns equal immutable state descriptors.
        installD3DCallStats(b.context.Get(),b.device.Get());need(d3dCallStatsInstalled(),"Screen creation diagnostic was not installed");
        auto creates=[] {return std::array<uint64_t,3>{gD3DDeviceCounts[20],gD3DDeviceCounts[21],gD3DDeviceCounts[23]};};
        auto issue=[&](bool original,uint32_t selector,uint32_t mask,uint32_t depthIndex,const std::shared_ptr<RenderTarget>& target){
            draw.blendSelector=selector;draw.colorWriteMask=UINT8(mask);const size_t sample=(selector+depthIndex)&1;draw.sampler=samplers[sample];
            b.bindTargets({target,nullptr,nullptr,nullptr},z);
            if(original)b.drawOriginalScreen(target,z,draw,bool(depthIndex&8),depthIndex&7,bool(depthIndex&16));else b.drawScreen(target,draw);
            ComPtr<ID3D11BlendState> blend;ComPtr<ID3D11DepthStencilState> depth;ComPtr<ID3D11SamplerState> sampler;
            b.context->OMGetBlendState(&blend,nullptr,nullptr);b.context->OMGetDepthStencilState(&depth,nullptr);b.context->PSGetSamplers(0,1,&sampler);
            need(blend&&depth,"Screen draw lost retained blend/depth state");
            const auto& wantedBlend=blendReferences[(original?64u:0u)+selector*16u+mask];D3D11_BLEND_DESC gotBlend{},expectedBlend{};
            blend->GetDesc(&gotBlend);wantedBlend->GetDesc(&expectedBlend);
            need(blend==wantedBlend&&sameBlend(gotBlend,expectedBlend),"Cached screen blend identity/full native descriptor differs");
            const auto& wantedDepth=depthReferences[original?depthIndex:7];D3D11_DEPTH_STENCIL_DESC gotDepth{},expectedDepth{};
            depth->GetDesc(&gotDepth);wantedDepth->GetDesc(&expectedDepth);
            need(depth==wantedDepth&&sameDepth(gotDepth,expectedDepth),"Cached screen post-depth identity/full native descriptor differs");
            if(original&&draw.texture){need(bool(sampler),"Original screen lost its retained sampler");
                D3D11_SAMPLER_DESC gotSampler{},expectedSampler{};sampler->GetDesc(&gotSampler);samplerReferences[sample]->GetDesc(&expectedSampler);
                static_assert(sizeof(D3D11_SAMPLER_DESC)==52); // All fields are four-byte scalars; no padding.
                need(sampler==samplerReferences[sample]&&!std::memcmp(&gotSampler,&expectedSampler,sizeof(gotSampler)),"Cached screen sampler identity/full native descriptor differs");}
            if(!original)need(!sampler,"Ordinary screen draw retained its transient sampler");
        };
        auto exercise=[&]{
            draw.color={.31f,.73f,.23f,.37f};draw.texture=texture;draw.coronaQuery.reset();
            for(uint32_t selector=0;selector<4;++selector)for(uint32_t mask=0;mask<16;++mask)issue(false,selector,mask,0,a);
            for(uint32_t depthIndex=0;depthIndex<32;++depthIndex)issue(true,depthIndex&3,15,depthIndex,depthIndex&1?other:a);
            draw.texture.reset();draw.color={};issue(true,3,8,0,a);draw.texture=texture;draw.color={1,1,1,1};
            draw.coronaQuery=query;draw.coronaUV={.5f/64,.5f/8};issue(true,0,15,16,a);
            ComPtr<ID3D11SamplerState> bound;b.context->PSGetSamplers(1,1,&bound);need(bound==queryReference,"Cached corona sampler differs");draw.coronaQuery.reset();
            for(const auto& sampler:samplers){const auto receipt=b.bindDirectSpriteInputs(texture,sampler);b.requireScreenInputReplacement(receipt);}
        };
        const auto cold=creates();exercise();const auto warm=creates();
        need(warm[0]-cold[0]==69&&warm[1]-cold[1]==33&&warm[2]-cold[2]==3,"Screen cache warmup did not create its bounded qualified states");
        exercise();need(creates()==warm,"Warm screen repeats still requested immutable state creation");
        // Alternate actual queued draws, descriptors, destination history and
        // target owners before any readback, Flush or completion wait.
        b.clearTarget(a,{0,0,0,0});b.clearTarget(other,{0,0,0,0});std::vector<uint32_t> ea(32*16),eo(ea.size());
        for(uint32_t index=0;index<12;++index){const bool second=bool(index&1);auto& expected=second?eo:ea;
            draw.color={.07f+.03f*float(index),.41f,.23f,.37f};issue(true,index&3,15,index%32,second?other:a);
            for(auto& code:expected)code=blended(code,draw.color,index&3);}
        need(creates()==warm,"Queued cached screen draws created immutable states");
        b.bindTargets({a,nullptr,nullptr,nullptr},z);auto copyA=b.copyFront(a,frontA);
        b.bindTargets({other,nullptr,nullptr,nullptr},z);auto copyOther=b.copyFront(other,frontOther);
        std::weak_ptr<Texture> weakTexture=texture;std::weak_ptr<RenderTarget> weakTarget=a;
        b.clearBindings();draw.texture.reset();texture.reset();a.reset();other.reset();
        need(weakTexture.expired(),"Screen state cache retained a caller texture owner");need(!weakTarget.expired(),"Queued screen copy lost its source owner");
        b.waitCopy(copyOther);need(b.copyComplete(copyA),"Queued screen copies did not complete in order");image(b,frontA,ea);image(b,frontOther,eo);
        const auto depthBytes=b.readbackDepthTarget(z);for(size_t i=0;i<depthBytes.size()/8;++i){uint32_t code{};std::memcpy(&code,depthBytes.data()+i*8,4);
            need(code==0x3F000000&&depthBytes[i*8+4]==0xA5,"Cached screen draw changed defined depth/stencil bits");}
        copyA.reset();copyOther.reset();b.waitIdle();need(weakTarget.expired(),"Screen state cache retained target after copy retirement");
        // Seed stale scene-state entries from another device. Existing cache
        // device validation must retire those entries before a new screen draw.
        NativeBackend foreign(!hardware);foreign.sceneCacheDevice=b.sceneCacheDevice;
        foreign.sceneDepthStates=b.sceneDepthStates;foreign.sceneSamplers=b.sceneSamplers;
        auto ft=foreign.createTarget(32,16,TargetFormat::RGB10A2);auto fz=foreign.createDepthTarget(32,16);
        auto fresh=quad();fresh.alphaTest=false;fresh.color={.17f,.41f,.83f,.72f};fresh.blendSelector=3;
        fresh.texture=foreign.createTexture(1,1,TextureFormat::RGBA8,white);foreign.bindTargets({ft,nullptr,nullptr,nullptr},fz);
        foreign.drawOriginalScreen(ft,fz,fresh,true,6);ComPtr<ID3D11BlendState> fb;ComPtr<ID3D11DepthStencilState> fd;ComPtr<ID3D11SamplerState> fs;
        foreign.context->OMGetBlendState(&fb,nullptr,nullptr);foreign.context->OMGetDepthStencilState(&fd,nullptr);foreign.context->PSGetSamplers(0,1,&fs);
        for(ID3D11DeviceChild* state:{static_cast<ID3D11DeviceChild*>(fb.Get()),static_cast<ID3D11DeviceChild*>(fd.Get()),static_cast<ID3D11DeviceChild*>(fs.Get())}){
            ComPtr<ID3D11Device> owner;state->GetDevice(&owner);need(owner==foreign.device,"Screen reused a foreign-device immutable state");}
        const auto foreignPixels=foreign.readbackTarget(ft);for(size_t i=0;i<32*16;++i)need(pixel(foreignPixels,i)==pack(fresh.color),"Device-isolated screen cached output differs");
        foreign.waitIdle();std::printf("PASS screen cached states:69 blend/33 depth/3 sampler warmup requests, zero warm repeat requests, exact descriptors/queued pixels/device/lifetimes\n");
    }
    static void queuedScratch(bool hardware){
        NativeBackend b(!hardware);b.configureRendering(1280,720,1,Antialiasing::Original,200);
        auto a=b.createTarget(32,16,TargetFormat::RGB10A2,TargetScale::Scene);
        auto other=b.createTarget(32,16,TargetFormat::RGB10A2,TargetScale::Scene);
        auto smallerTarget=b.createTarget(16,8,TargetFormat::RGB10A2,TargetScale::Scene);
        auto fixed=b.createTarget(32,16,TargetFormat::RGB10A2);
        auto za=b.createDepthTarget(32,16,TargetScale::Scene),zs=b.createDepthTarget(16,8,TargetScale::Scene),zf=b.createDepthTarget(32,16);
        auto frontA=b.createTarget(32,16,TargetFormat::RGB10A2,TargetScale::Scene);
        auto frontOther=b.createTarget(32,16,TargetFormat::RGB10A2,TargetScale::Scene);
        auto frontSmall=b.createTarget(16,8,TargetFormat::RGB10A2,TargetScale::Scene);
        auto frontFixed=b.createTarget(32,16,TargetFormat::RGB10A2);
        auto frontFinal=b.createTarget(32,16,TargetFormat::RGB10A2,TargetScale::Scene);
        std::vector<uint32_t> ea(size_t(a->pixelWidth())*a->pixelHeight()),eo(ea.size());
        std::vector<uint32_t> es(size_t(smallerTarget->pixelWidth())*smallerTarget->pixelHeight()),ef(es.size());
        auto draw=quad();draw.alphaTest=false;draw.blendSelector=3;draw.color={0.17f,0.41f,0.83f,0.72f};
        auto issue=[&](const auto& target,const auto& depth,std::vector<uint32_t>& expected,bool left=false,
                       std::optional<std::array<float,4>> sampled={}){
            b.bindTargets({target,nullptr,nullptr,nullptr},depth);b.drawOriginalScreen(target,depth,draw,false,6,false);
            b.requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);
            const auto source=sampled.value_or(draw.color);
            for(size_t y=0;y<target->pixelHeight();++y)for(size_t x=0;x<target->pixelWidth();++x)
                if(!left||x<target->pixelWidth()/2){auto& p=expected[y*target->pixelWidth()+x];p=blended(p,source,draw.blendSelector);}
        };
        issue(a,za,ea);const auto first=b.screenScratch.texture;const auto firstView=b.screenScratch.view;
        need(first&&firstView&&b.screenScratch.desc.Width==64&&b.screenScratch.desc.Height==32&&
             b.screenScratch.desc.Format==DXGI_FORMAT_R10G10B10A2_UINT,"Screen scratch used logical dimensions or wrong packed format");
        draw.color={0.61f,0.29f,0.43f,1};issue(other,za,eo);
        need(b.screenScratch.texture==first&&b.screenScratch.view==firstView,"Same-size different target allocated another screen scratch");
        draw.vertices[1].x=draw.vertices[3].x=0;draw.color={0.31f,0.73f,0.23f,0.37f};draw.blendSelector=1;issue(a,za,ea,true);
        const std::array<uint8_t,4> rgba={17,101,239,255};auto source=b.createTexture(1,1,TextureFormat::RGBA8,rgba);
        std::weak_ptr<Texture> weakSource=source;draw=quad();draw.alphaTest=false;draw.texture=source;draw.color={1,1,1,0.37f};draw.blendSelector=0;
        issue(a,za,ea,false,std::array<float,4>{17.0f/255,101.0f/255,239.0f/255,0.37f});
        need(b.screenScratch.texture==first,"Queued textured blend replaced same-size scratch");
        draw.texture.reset();source.reset();need(weakSource.expired(),"Screen scratch retained caller source texture ownership");
        auto copyA=b.copyFront(a,frontA);const auto snapshotA=ea;
        draw.vertices[1].x=draw.vertices[3].x=0;draw.color={0.25f,0.61f,0.47f,0.29f};draw.blendSelector=2;issue(other,za,eo,true);
        auto copyOther=b.copyFront(other,frontOther);
        draw=quad();draw.alphaTest=false;draw.blendSelector=3;draw.color={0.07f,0.83f,0.19f,1};issue(smallerTarget,zs,es);
        const auto resized=b.screenScratch.texture;
        need(resized!=first&&b.screenScratch.desc.Width==32&&b.screenScratch.desc.Height==16,"Different physical extent reused incompatible scratch");
        draw.vertices[1].x=draw.vertices[3].x=0;draw.color={0.41f,0.17f,0.53f,0.23f};draw.blendSelector=0;issue(smallerTarget,zs,es,true);
        auto copySmall=b.copyFront(smallerTarget,frontSmall);
        draw=quad();draw.alphaTest=false;draw.blendSelector=3;draw.color={0.73f,0.11f,0.37f,1};issue(fixed,zf,ef);
        need(b.screenScratch.texture==resized,"Equal physical storage with different logical extent missed scratch reuse");
        auto copyFixed=b.copyFront(fixed,frontFixed);
        draw.color={0.31f,0.13f,0.23f,0.17f};draw.blendSelector=2;issue(a,za,ea);
        need(b.screenScratch.texture!=resized,"Returning to larger extent reused smaller scratch");
        // A retained scratch output in an additional OM slot must still reject
        // before copying, rebinding, or changing the completed draw count.
        ComPtr<ID3D11RenderTargetView> selected;ComPtr<ID3D11DepthStencilView> selectedDepth;
        b.context->OMGetRenderTargets(1,&selected,&selectedDepth);
        const std::array<ID3D11RenderTargetView*,2> outputs={selected.Get(),b.screenScratch.view.Get()};
        b.context->OMSetRenderTargets(2,outputs.data(),selectedDepth.Get());const auto count=b.screenDrawCount();
        rejects([&]{b.drawOriginalScreen(a,za,draw,false,6,false);});
        std::array<ID3D11RenderTargetView*,2> retained{};b.context->OMGetRenderTargets(2,retained.data(),nullptr);
        need(retained==outputs&&b.screenDrawCount()==count,"Rejected scratch output binding changed OM or draw count");
        for(auto* output:retained)if(output)output->Release();b.bindTargets({a,nullptr,nullptr,nullptr},za);
        // A same-descriptor entry belonging to another device must miss. Pin
        // both entries so pointer reuse cannot make this identity check pass.
        NativeBackend foreign(!hardware);auto ft=foreign.createTarget(64,32,TargetFormat::RGB10A2);auto fz=foreign.createDepthTarget(64,32);
        foreign.bindTargets({ft,nullptr,nullptr,nullptr},fz);auto seed=quad();seed.alphaTest=false;seed.blendSelector=3;
        foreign.drawOriginalScreen(ft,fz,seed,false,6,false);const auto foreignTexture=foreign.screenScratch.texture;
        b.screenScratch=foreign.screenScratch;draw.blendSelector=0;issue(a,za,ea);
        need(b.screenScratch.device.Get()==b.device.Get()&&b.screenScratch.texture!=foreignTexture,"Screen scratch reused a foreign-device entry");
        auto copyFinal=b.copyFront(a,frontFinal);std::weak_ptr<RenderTarget> weakTarget=a;
        b.clearBindings();a.reset();other.reset();smallerTarget.reset();fixed.reset();
        need(!weakTarget.expired(),"Queued front receipt failed to retain its source target owner");
        // No readback, Flush or completion wait occurs between the draws.
        // Waiting the last event retires every earlier scratch copy/draw/copy.
        b.waitCopy(copyFinal);need(b.copyComplete(copyA)&&b.copyComplete(copyOther)&&b.copyComplete(copySmall)&&b.copyComplete(copyFixed),"Queued screen front events completed out of order");
        image(b,frontA,snapshotA);image(b,frontOther,eo);image(b,frontSmall,es);image(b,frontFixed,ef);image(b,frontFinal,ea);
        copyA.reset();copyOther.reset();copySmall.reset();copyFixed.reset();copyFinal.reset();b.waitIdle();
        need(weakTarget.expired(),"Screen scratch retained source target after transfer retirement");
        foreign.waitIdle();
    }
    static void spriteDepth(NativeBackend& b){
        ComPtr<ID3D11DepthStencilState> state;b.context->OMGetDepthStencilState(&state,nullptr);
        D3D11_DEPTH_STENCIL_DESC desc{};state->GetDesc(&desc);
        // D3D11 canonicalizes the ignored write mask and comparison when depth
        // is disabled. DepthEnable controls both testing and writing here.
        need(!desc.DepthEnable,"Post-sprite depth remained enabled");
    }
    static void post(NativeBackend& b,bool textured){
        ComPtr<ID3D11BlendState> blend;b.context->OMGetBlendState(&blend,nullptr,nullptr);D3D11_BLEND_DESC bd{};blend->GetDesc(&bd);
        need(!bd.RenderTarget[0].BlendEnable,"Post-screen blend remained enabled");
        ComPtr<ID3D11DepthStencilState> depth;b.context->OMGetDepthStencilState(&depth,nullptr);D3D11_DEPTH_STENCIL_DESC dd{};depth->GetDesc(&dd);
        need(dd.DepthEnable&&dd.DepthWriteMask==D3D11_DEPTH_WRITE_MASK_ALL&&dd.DepthFunc==D3D11_COMPARISON_GREATER_EQUAL&&!dd.StencilEnable,"Post-screen depth differs");
        ComPtr<ID3D11Buffer> stream;UINT stride{},offset{};b.context->IAGetVertexBuffers(0,1,&stream,&stride,&offset);
        need(!stream&&!stride&&!offset,"Screen transient stream was not unbound");
        ComPtr<ID3D11ShaderResourceView> texture,destination;b.context->PSGetShaderResources(0,1,&texture);b.context->PSGetShaderResources(1,1,&destination);
        need(bool(texture)==textured&&!destination,"Screen texture retention or temporary destination cleanup differs");
        ComPtr<ID3D11Buffer> constants;b.context->PSGetConstantBuffers(0,1,&constants);D3D11_BUFFER_DESC desc{};constants->GetDesc(&desc);
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Buffer> read;
        need(SUCCEEDED(b.device->CreateBuffer(&desc,nullptr,&read)),"Constant readback allocation failed");b.context->CopyResource(read.Get(),constants.Get());
        D3D11_MAPPED_SUBRESOURCE m{};need(SUCCEEDED(b.context->Map(read.Get(),0,D3D11_MAP_READ,0,&m)),"Constant readback failed");
        uint32_t alpha{};std::memcpy(&alpha,static_cast<const uint8_t*>(m.pData)+20,4);b.context->Unmap(read.Get(),0);need(!alpha,"Post-screen alpha test remained enabled");
    }
};
}
int main(int argc,char** argv){try{
    const bool hardware=argc==2&&std::strcmp(argv[1],"--hardware")==0;NativeBackend b(!hardware);
    auto target=b.createTarget(32,16,TargetFormat::RGB10A2);auto depth=b.createDepthTarget(32,16);
    b.bindTargets({target,nullptr,nullptr,nullptr},depth);b.clearDepthTarget(depth,0,97);const auto depthBefore=b.readbackDepthTarget(depth);
    const D3D11_VIEWPORT prior{3,5,17,9,0.25f,0.75f};b.setViewport(prior);auto d=quad();
    auto submit=[&]{b.drawOriginalScreen(target,depth,d,true,6);b.requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);
        const auto after=b.viewport();need(after.has_value()&&!std::memcmp(&*after,&prior,sizeof(prior)),"Screen changed prior native viewport");};
    // The alpha value that collapses under ordinary two-bit source-alpha blending.
    b.clearTarget(target,{0,0,0,0});d.color={175.0f/255,0,0,2.0f/255};submit();
    auto bytes=b.readbackTarget(target);for(size_t i=0;i<32*16;++i){if(pixel(bytes,i)!=6)fprintf(stderr,"low-alpha pixel=%zu packed=%08X expected=00000006\n",i,pixel(bytes,i));need(pixel(bytes,i)==6,"Expanded blend quantized source alpha to two bits or changed coverage");}
    for(uint32_t mode=0;mode<4;++mode){
        b.clearTarget(target,{0,0,0,1});d.blendSelector=3;d.color={0.17f,0.41f,0.83f,0.72f};submit();
        for(uint32_t repeat=0;repeat<3;++repeat){const auto before=unpack(pixel(b.readbackTarget(target)));d.blendSelector=mode;d.color={0.31f,0.73f,0.23f,0.37f};
            auto expected=d.color;for(uint32_t c=0;c<3;++c){const float product=d.color[c]*d.color[3];
                expected[c]=mode==3?d.color[c]:(mode==0?product+before[c]:(mode==1?product+before[c]*(1-d.color[3]):before[c]-product));}
            submit();bytes=b.readbackTarget(target);const auto packed=pack(expected);
            for(size_t i=0;i<32*16;++i)need(pixel(bytes,i)==packed,"Original blend equation, saturation or per-draw destination snapshot differs");
        }
    }
    NativeScreenProbe::post(b,false);
    d.blendSelector=0;d.color={1,1,1,d.alphaReference};const auto before=b.readbackTarget(target);submit();need(b.readbackTarget(target)==before,"GREATER alpha test failed");
    const std::array<uint8_t,4> rgba={17,101,239,255};d.texture=b.createTexture(1,1,TextureFormat::RGBA8,rgba);d.color={1,1,1,1};d.blendSelector=3;submit();
    bytes=b.readbackTarget(target);const uint32_t sampled=pack({17.0f/255,101.0f/255,239.0f/255,1});
    for(size_t i=0;i<32*16;++i)need(pixel(bytes,i)==sampled,"Original textured shader/filter/component mapping differs");
    NativeScreenProbe::post(b,true);d.texture.reset();d.color={0,0,0,1};submit();NativeScreenProbe::post(b,true);
    d.vertices[1].x=d.vertices[3].x=0;d.color={1,0,0,1};submit();bytes=b.readbackTarget(target);
    for(size_t y=0;y<16;++y)for(size_t x=0;x<32;++x)need(pixel(bytes,y*32+x)==(x<16?0xC00003FFu:0xC0000000u),"Packed temporary failed to preserve uncovered pixels");
    need(b.readbackDepthTarget(depth)==depthBefore,"Screen draw changed depth or stencil pixels");
    b.drawOriginalScreen(target,depth,d,false,6,false);NativeScreenProbe::spriteDepth(b);
    need(b.readbackDepthTarget(depth)==depthBefore,"Sprite draw changed depth or stencil pixels");
    const auto alphaBefore=b.readbackTarget(target);
    d=quad();d.color={};d.alphaTest=false;d.blendSelector=3;d.colorWriteMask=8;
    b.drawOriginalScreen(target,depth,d,false,6,false);
    bytes=b.readbackTarget(target);
    for(size_t i=0;i<32*16;++i)need(pixel(bytes,i)==(pixel(alphaBefore,i)&0x3FFFFFFF),"Alpha-only screen clear changed RGB or left alpha");
    need(b.readbackDepthTarget(depth)==depthBefore,"Alpha-only screen clear changed depth/stencil");
    const auto alphaCount=b.screenDrawCount();d.color[3]=1;rejects([&]{submit();});d.color={};
    d.colorWriteMask=7;rejects([&]{submit();});d.colorWriteMask=8;d.alphaTest=true;rejects([&]{submit();});
    need(b.screenDrawCount()==alphaCount&&b.readbackTarget(target)==bytes,"Rejected alpha clear altered its target");
    d=quad();d.blendSelector=3;
    {
        auto* device=NativeScreenProbe::device(b);auto* context=NativeScreenProbe::context(b);
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R32_UINT;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11UnorderedAccessView> output;
        need(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&texture)),"Screen UAV fixture texture");
        need(SUCCEEDED(device->CreateUnorderedAccessView(texture.Get(),nullptr,&output)),"Screen UAV fixture view");
        for(UINT slot:{1u,device->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?63u:7u}){
            auto* raw=output.Get();context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,slot,1,&raw,nullptr);
            b.requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);
            const auto colorBefore=b.readbackTarget(target),zBefore=b.readbackDepthTarget(depth);const auto count=b.screenDrawCount();
            const auto viewportBefore=b.viewport();rejects([&]{submit();});const auto viewportAfter=b.viewport();
            ComPtr<ID3D11UnorderedAccessView> retained;context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,slot,1,&retained);
            need(retained==output&&b.screenDrawCount()==count,"Rejected screen draw changed UAV binding or draw count");
            need(b.readbackTarget(target)==colorBefore&&b.readbackDepthTarget(depth)==zBefore,"Rejected screen draw changed attachments");
            need(viewportBefore&&viewportAfter&&!std::memcmp(&*viewportBefore,&*viewportAfter,sizeof(D3D11_VIEWPORT)),"Rejected screen draw changed viewport");
            ID3D11UnorderedAccessView* empty=nullptr;context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,slot,1,&empty,nullptr);
        }
    }
    const auto draws=b.screenDrawCount();d.blendSelector=4;rejects([&]{submit();});d.blendSelector=3;
    d.color[0]=std::numeric_limits<float>::quiet_NaN();rejects([&]{submit();});d.color[0]=0;
    b.bindTargets({nullptr,nullptr,nullptr,nullptr},depth);rejects([&]{submit();});
    need(b.screenDrawCount()==draws,"Rejected original draw changed submission count");need(!b.presentationCount(),"Fixture draws counted as presentation");
    NativeScreenProbe::queuedScratch(hardware);
    NativeScreenProbe::cachedStates(hardware);
    auto state=EngineState::fromOriginalStartup();state.setScalar(ScalarState::HalfPixelOffset,1);state.setScalar(ScalarState::GuardBandX,0x3F800000);state.setScalar(ScalarState::GuardBandY,0x3F800000);
    state.setSampler(0,SamplerState::AddressW,2);state.setSampler(0,SamplerState::MipFilter,1);state.applyScreenQuadState(0,true);
    auto checked=state.requireOriginalScreenState(true);need(checked.expandedBlendRequested&&checked.sampler->addressW==2&&checked.sampler->mipFilter==1,"Original snapshot lost retained inactive sampler fields");
    state.setScalar(ScalarState::ColorMask0,8);state.setScalar(ScalarState::DepthWrite,0);
    state.setScalar(ScalarState::AlphaTest,0);state.setScalar(ScalarState::BlendEnable,0);
    need(state.requireOriginalAlphaClearState().colorMask==8,"Alpha clear lost its alpha-only mask");
    rejectsState([&]{state.requireOriginalScreenState(false);});
    state.setScalar(ScalarState::BlendEnable,1);rejectsState([&]{state.requireOriginalAlphaClearState();});
    printf("PASS: %zu original screen backend checks; native arithmetic fixtures, console precision parity unverified\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL after %zu checks: %s\n",checks,e.what());return 1;}}
