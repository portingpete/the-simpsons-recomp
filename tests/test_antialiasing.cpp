#include "renderer/native_backend.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <thread>
#include <d3d11_1.h>

using namespace Simpsons::Graphics;
void require(bool value,const char* reason) {if(!value)throw Error(reason);}
template<class F> void rejects(F action) {
    try {action();}catch(const Error&){return;}
    throw Error("Invalid native antialiasing operation did not fail");
}
// Reuse the complete actual Get* context snapshot, including all shader stages,
// outputs, native buffers, raster/blend/depth state, viewports and scissors.
#include "header/test_engine_binding_reset.h"

namespace {
constexpr uint32_t white=0xffffffffu,black=0xc0000000u;
uint32_t packedGray(uint32_t value,uint32_t alpha=3) {return value|(value<<10)|(value<<20)|(alpha<<30);}
uint32_t pixel(const std::vector<uint8_t>& bytes,uint32_t width,uint32_t x,uint32_t y) {
    uint32_t value{};std::memcpy(&value,bytes.data()+(size_t(y)*width+x)*4,4);return value;
}
bool intermediate(uint32_t value) {const auto code=value&1023u;return code>0&&code<1023;}
void gray(uint32_t value,uint32_t expected,const char* reason) {
    for(const auto shift:{0u,10u,20u}) {
        const auto code=(value>>shift)&1023u;
        require(code+1>=expected&&code<=expected+1,reason);
    }
    require((value>>30)==3,"Antialiasing changed opaque alpha");
}
}

namespace Simpsons::Graphics {
struct NativeAntialiasingProbe {
    using Snapshot=EngineBindingResetProbe::Snapshot;
    static void upload(NativeBackend& backend,const std::shared_ptr<RenderTarget>& target,
                       const std::vector<uint32_t>& values) {
        require(values.size()==size_t(target->pixelWidth())*target->pixelHeight(),"AA fixture upload extent differs");
        backend.context->UpdateSubresource(target->texture.Get(),0,nullptr,values.data(),target->pixelWidth()*4,UINT(values.size()*4));
    }
    static void actualExtent(const std::shared_ptr<RenderTarget>& color,const std::shared_ptr<DepthTarget>& depth,
                             uint32_t width,uint32_t height) {
        D3D11_TEXTURE2D_DESC c{},d{};color->texture->GetDesc(&c);depth->texture->GetDesc(&d);
        require(c.Width==width&&c.Height==height&&d.Width==width&&d.Height==height,
                "Antialiasing scene color/depth did not allocate the requested sample extent");
    }
    static void seedBindings(NativeBackend& backend,const std::shared_ptr<RenderTarget>& color,
                             const std::shared_ptr<DepthTarget>& depth) {
        ScreenDraw draw{};draw.vertices={{{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,-1,1,1}}};
        draw.color={0.25f,0.5f,0.75f,1};draw.blendSelector=3;draw.colorWriteMask=15;
        backend.drawScreen(color,draw);backend.bindTargets({color,nullptr,nullptr,nullptr},depth);
        const std::array<uint32_t,4> samples{0x102030ff,0x405060ff,0x708090ff,0xa0b0c0ff};
        const auto bytes=std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(samples.data()),sizeof(samples));
        auto texture=backend.createTexture(2,2,TextureFormat::RGBA8,bytes);backend.bindEngineTexture(0,texture);
        ComPtr<ID3D11ShaderResourceView> sampled;backend.context->PSGetShaderResources(0,1,&sampled);
        auto* view=sampled.Get();backend.context->PSSetShaderResources(120,1,&view);backend.context->VSSetShaderResources(3,1,&view);
        backend.context->GSSetShaderResources(9,1,&view);backend.context->CSSetShaderResources(20,1,&view);
        ComPtr<ID3D11Buffer> constant;backend.context->PSGetConstantBuffers(0,1,&constant);
        require(bool(constant),"AA state fixture has no native constant buffer");
        auto* buffer=constant.Get();backend.context->VSSetConstantBuffers(13,1,&buffer);backend.context->CSSetConstantBuffers(7,1,&buffer);
        const D3D11_VIEWPORT viewport{1.5f,2.5f,21,13,0.125f,0.875f};backend.context->RSSetViewports(1,&viewport);
        const D3D11_RECT scissor{2,3,19,15};backend.context->RSSetScissorRects(1,&scissor);
        backend.context->SetPredication(nullptr,TRUE);
        ComPtr<ID3D11DeviceContext1> context1;require(SUCCEEDED(backend.context.As(&context1)),"AA fixture lacks an actual Context1");
        D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
        if(SUCCEEDED(backend.device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,&options,sizeof(options)))&&options.ConstantBufferOffsetting) {
            D3D11_BUFFER_DESC desc{};desc.ByteWidth=1024;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            ComPtr<ID3D11Buffer> ranged;require(SUCCEEDED(backend.device->CreateBuffer(&desc,nullptr,&ranged)),"AA ranged binding fixture allocation failed");
            auto* actual=ranged.Get();const UINT first=16,count=16;
            context1->VSSetConstantBuffers1(11,1,&actual,&first,&count);
            context1->PSSetConstantBuffers1(12,1,&actual,&first,&count);
        }
    }
    static void resolvePreserving(NativeBackend& backend,const std::shared_ptr<RenderTarget>& source,
                                  std::shared_ptr<RenderTarget>& result) {
        ComPtr<ID3D11DeviceContext1> context1;require(SUCCEEDED(backend.context.As(&context1)),"AA fixture lacks an actual ranged-context interface");
        ComPtr<ID3D11Buffer> vsBefore,psBefore;UINT vsFirst{},vsCount{},psFirst{},psCount{};
        context1->VSGetConstantBuffers1(11,1,&vsBefore,&vsFirst,&vsCount);
        context1->PSGetConstantBuffers1(12,1,&psBefore,&psFirst,&psCount);
        const Snapshot before(backend.context.Get());result=backend.resolveAntialiasing(source);
        require(Snapshot(backend.context.Get())==before,"Antialiasing resolve changed original engine context bindings");
        ComPtr<ID3D11Buffer> vsAfter,psAfter;UINT vsFirstAfter{},vsCountAfter{},psFirstAfter{},psCountAfter{};
        context1->VSGetConstantBuffers1(11,1,&vsAfter,&vsFirstAfter,&vsCountAfter);
        context1->PSGetConstantBuffers1(12,1,&psAfter,&psFirstAfter,&psCountAfter);
        require(vsBefore==vsAfter&&psBefore==psAfter&&vsFirst==vsFirstAfter&&vsCount==vsCountAfter&&
                psFirst==psFirstAfter&&psCount==psCountAfter,"Antialiasing resolve changed original VS/PS constant-buffer ranges");
        ComPtr<ID3D11Predicate> predicate;BOOL value=FALSE;backend.context->GetPredication(&predicate,&value);
        require(!predicate&&value==TRUE,"Antialiasing resolve changed native predication state");
    }
    static std::vector<uint8_t> run(Antialiasing mode,bool software) {
        constexpr uint32_t width=64,height=32;
        NativeBackend backend(software,false);backend.configureRendering(1280,720,1,mode);
        auto front=backend.createTarget(width,height,TargetFormat::RGB10A2,TargetScale::Scene);
        auto depth=backend.createDepthTarget(width,height,TargetScale::Scene);
        auto query=backend.createTarget(64,8,TargetFormat::RGB10A2);
        actualExtent(front,depth,width,height);
        require(query->pixelWidth()==64&&query->pixelHeight()==8,"AA scaled a fixed native corona query texture");
        seedBindings(backend,front,depth);backend.clearDepthTarget(depth,0.375f,0xa7);
        const auto depthBefore=backend.readbackDepthTarget(depth);
        std::vector<uint32_t> pattern(size_t(front->pixelWidth())*front->pixelHeight());
        for(uint32_t y=0;y<front->pixelHeight();++y)
            for(uint32_t x=0;x<front->pixelWidth();++x)pattern[size_t(y)*front->pixelWidth()+x]=x>=y?white:black;
        upload(backend,front,pattern);const auto sourceBefore=backend.readbackTarget(front);
        std::shared_ptr<RenderTarget> result;resolvePreserving(backend,front,result);
        require(result&&result->width==width&&result->height==height,"AA changed original logical front dimensions");
        require(result->pixelWidth()==width&&result->pixelHeight()==height,"AA resolve output differs from selected internal resolution");
        const auto output=backend.readbackTarget(result);
        require(output.size()==size_t(width)*height*4,"AA output readback extent differs");
        require(backend.readbackTarget(front)==sourceBefore&&backend.readbackDepthTarget(depth)==depthBefore,
                "AA resolve changed the source scene color/depth");
        if(mode==Antialiasing::Original) {
            require(result==front&&output==sourceBefore,"Original AA added filtering or changed packed pixels");
        }else {
            require(result!=front&&output!=sourceBefore,"Spatial AA did not filter a jagged diagonal");
            size_t softened=0;
            for(uint32_t y=2;y<height-2;++y)for(uint32_t x=2;x<width-2;++x) {
                const auto value=pixel(output,width,x,y);
                if(intermediate(value)) {
                    ++softened;const int distance=int(x)-int(y);
                    require(distance>=-3&&distance<=3,"Spatial AA blurred pixels away from the detected edge");
                    gray(value,value&1023u,"Spatial AA introduced a tint on a grayscale edge");
                }
            }
            require(softened>=16,"Spatial AA failed to introduce subpixel coverage along the diagonal");
            require(pixel(output,width,60,2)==white&&pixel(output,width,2,29)==black,
                    "Spatial AA changed flat areas away from edges");
        }
        // Resolve twice to exercise the cached output and preserved context on
        // reuse, rather than accepting only a successful initial allocation.
        std::shared_ptr<RenderTarget> again;resolvePreserving(backend,front,again);
        require(again==result&&backend.readbackTarget(again)==output,"Repeated AA resolve changed output or leaked target allocation");
        std::printf("Verified %s AA mode %u: scene %ux%u, output %ux%u, edge coverage and native context restoration\n",
                    software?"WARP":"hardware",unsigned(mode),front->pixelWidth(),front->pixelHeight(),result->pixelWidth(),result->pixelHeight());
        return output;
    }
    static void qualityContrast(bool software) {
        constexpr uint32_t width=32,height=24;
        const auto filtered=[&](Antialiasing mode,uint32_t contrast) {
            NativeBackend backend(software,false);backend.configureRendering(1280,720,1,mode);
            auto front=backend.createTarget(width,height,TargetFormat::RGB10A2,TargetScale::Scene);
            std::vector<uint32_t> values(size_t(width)*height);
            for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x)
                values[size_t(y)*width+x]=packedGray(x>=y?64+contrast:64);
            upload(backend,front,values);
            const auto source=backend.readbackTarget(front),output=backend.readbackTarget(backend.resolveAntialiasing(front));
            require(pixel(output,width,30,2)==packedGray(64+contrast)&&pixel(output,width,2,22)==packedGray(64),
                    "FXAA quality preset changed low contrast flat areas");
            return output!=source;
        };
        require(!filtered(Antialiasing::FXAALow,40)&&filtered(Antialiasing::FXAA,40),
                "FXAA Low did not skip edges below its quality threshold");
        require(!filtered(Antialiasing::FXAA,24)&&filtered(Antialiasing::FXAAHigh,24),
                "FXAA High did not detect weaker contrast than the original preset");
        std::printf("Verified %s FXAA quality thresholds against actual weak contrast GPU edges\n",software?"WARP":"hardware");
    }
    static void sharpening(bool software) {
        constexpr uint32_t width=32,height=16;
        NativeBackend backend(software,false);backend.configureRendering(1280,720,1,Antialiasing::Original);
        auto front=backend.createTarget(width,height,TargetFormat::RGB10A2);
        auto depth=backend.createDepthTarget(width,height);
        seedBindings(backend,front,depth);
        // A deliberately blurred edge has flat endpoints and intermediate
        // shoulder pixels. Only the shoulder contrast should increase.
        constexpr std::array profile{64u,64u,80u,144u,208u,224u,224u};
        std::vector<uint32_t> values(size_t(width)*height);
        for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x)
            values[size_t(y)*width+x]=packedGray(profile[std::min(size_t(x),profile.size()-1)],y%4);
        upload(backend,front,values);const auto source=backend.readbackTarget(front);
        std::shared_ptr<RenderTarget> result;resolvePreserving(backend,front,result);
        require(result==front&&backend.readbackTarget(result)==source,"Sharpening zero did not exactly bypass the original pixels");
        rejects([&]{backend.configureSharpening(101);});
        require(backend.sharpening()==0,"Invalid sharpening strength changed the active value");
        uint32_t previous=80;
        std::shared_ptr<RenderTarget> cached;
        for(const uint32_t strength:{25u,50u,75u,100u}) {
            backend.configureSharpening(strength);require(backend.sharpening()==strength,"Sharpening getter differs from active strength");
            resolvePreserving(backend,front,result);const auto output=backend.readbackTarget(result);
            require(result!=front&&output!=source,"Nonzero sharpening did not increase shoulder contrast");
            if(cached)require(cached==result,"Live sharpening changes reallocated the presentation target");
            cached=result;
            const auto shoulder=pixel(output,width,2,7)&1023u;
            require(shoulder<previous&&shoulder>=64,"Sharpening did not progressively strengthen the edge within its local range");
            previous=shoulder;
            for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
                const auto value=pixel(output,width,x,y);
                require((value>>30)==y%4,"Sharpening changed source alpha");
                for(const auto shift:{0u,10u,20u}) {
                    const auto code=(value>>shift)&1023u;
                    require(code>=64&&code<=224,"Sharpening introduced out-of-range halos");
                }
            }
            require(pixel(output,width,0,3)==pixel(source,width,0,3)&&pixel(output,width,31,3)==pixel(source,width,31,3),
                    "Sharpening changed flat endpoint pixels");
            require(backend.readbackTarget(front)==source,"Sharpening modified the original target");
            std::shared_ptr<RenderTarget> again;resolvePreserving(backend,front,again);
            require(again==result&&backend.readbackTarget(again)==output,"Cached sharpening output changed on a repeated resolve");
        }
        backend.configureSharpening(0);resolvePreserving(backend,front,result);
        require(result==front&&backend.readbackTarget(result)==source,"Disabling sharpening did not restore exact bypass");
        std::printf("Verified %s live sharpening strengths, local contrast bounds, alpha and native context restoration\n",software?"WARP":"hardware");
    }
    static void sharpeningAfterAA(bool software) {
        constexpr uint32_t width=32,height=16;
        for(const auto mode:{Antialiasing::FXAAHigh,Antialiasing::SMAA}) {
            NativeBackend backend(software,false);backend.configureRendering(1280,720,1,mode);
            auto front=backend.createTarget(width,height,TargetFormat::RGB10A2,TargetScale::Scene);
            auto depth=backend.createDepthTarget(width,height,TargetScale::Scene);seedBindings(backend,front,depth);
            std::vector<uint32_t> values(size_t(front->pixelWidth())*front->pixelHeight());
            for(uint32_t y=0;y<front->pixelHeight();++y)for(uint32_t x=0;x<front->pixelWidth();++x)
                values[size_t(y)*front->pixelWidth()+x]=packedGray(x>=y?224:64);
            upload(backend,front,values);
            std::shared_ptr<RenderTarget> result;resolvePreserving(backend,front,result);
            const auto antialiased=backend.readbackTarget(result);
            backend.configureSharpening(50);resolvePreserving(backend,front,result);
            require(result->pixelWidth()==width&&result->pixelHeight()==height,"Sharpening after AA changed the resolve extent");
            const auto sharpened=backend.readbackTarget(result);
            NativeBackend independent(software,false);independent.configureRendering(1280,720,1);independent.configureSharpening(50);
            auto resolved=independent.createTarget(width,height,TargetFormat::RGB10A2);
            independent.context->UpdateSubresource(resolved->texture.Get(),0,nullptr,antialiased.data(),width*4,UINT(antialiased.size()));
            require(independent.readbackTarget(independent.resolveAntialiasing(resolved))==sharpened,
                    "Sharpening was not applied after the complete antialiasing resolve");
        }
        std::printf("Verified %s sharpening after FXAA and SMAA against independent resolved inputs\n",software?"WARP":"hardware");
    }
};
}

int main(int argc,char** argv) {
    try {
        const bool software=!(argc==2&&std::strcmp(argv[1],"--hardware")==0);
        NativeAntialiasingProbe::run(Antialiasing::Original,software);
        const auto fxaa=NativeAntialiasingProbe::run(Antialiasing::FXAA,software);
        const auto combined=NativeAntialiasingProbe::run(Antialiasing::FXAAOriginal,software);
        require(fxaa==combined,"FXAA modes apply different final filters to identical front pixels");
        const auto low=NativeAntialiasingProbe::run(Antialiasing::FXAALow,software);
        const auto high=NativeAntialiasingProbe::run(Antialiasing::FXAAHigh,software);
        require(low!=fxaa&&high!=fxaa&&low!=high,"FXAA quality presets produced identical diagonal filter results");
        NativeAntialiasingProbe::run(Antialiasing::SMAA,software);
        NativeAntialiasingProbe::qualityContrast(software);
        NativeAntialiasingProbe::sharpening(software);
        NativeAntialiasingProbe::sharpeningAfterAA(software);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
