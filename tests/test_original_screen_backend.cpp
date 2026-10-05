#include "renderer/native_backend.h"
#include "renderer/engine_state.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <limits>
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
}
namespace Simpsons::Graphics {
struct NativeScreenProbe {
    static ID3D11Device* device(NativeBackend& b){return b.device.Get();}
    static ID3D11DeviceContext* context(NativeBackend& b){return b.context.Get();}
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
