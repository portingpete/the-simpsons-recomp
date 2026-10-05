#include "renderer/native_backend.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <thread>

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
    }
    static void resolvePreserving(NativeBackend& backend,const std::shared_ptr<RenderTarget>& source,
                                  std::shared_ptr<RenderTarget>& result) {
        const Snapshot before(backend.context.Get());result=backend.resolveAntialiasing(source);
        require(Snapshot(backend.context.Get())==before,"Antialiasing resolve changed original engine context bindings");
        ComPtr<ID3D11Predicate> predicate;BOOL value=FALSE;backend.context->GetPredication(&predicate,&value);
        require(!predicate&&value==TRUE,"Antialiasing resolve changed native predication state");
    }
    static std::vector<uint8_t> run(Antialiasing mode) {
        constexpr uint32_t width=64,height=32;
        NativeBackend backend(true,false);backend.configureRendering(1280,720,1,mode);
        auto front=backend.createTarget(width,height,TargetFormat::RGB10A2,TargetScale::Scene);
        auto depth=backend.createDepthTarget(width,height,TargetScale::Scene);
        auto query=backend.createTarget(64,8,TargetFormat::RGB10A2);
        const bool supersampled=mode==Antialiasing::SSAA4x;
        actualExtent(front,depth,width*(supersampled?2:1),height*(supersampled?2:1));
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
        }else if(supersampled) {
            require(result!=front,"SSAA did not create a resolved output");
            // The diagonal crosses every diagonal output texel's four scene
            // samples: three white and one black. Other texels are uniform.
            for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x)
                gray(pixel(output,width,x,y),x==y?767u:x>y?1023u:0u,"SSAA did not average the four actual scene samples");
        }else {
            require(result!=front&&output!=sourceBefore,"FXAA did not filter a jagged diagonal");
            size_t softened=0;
            for(uint32_t y=2;y<height-2;++y)for(uint32_t x=2;x<width-2;++x) {
                const auto value=pixel(output,width,x,y);
                if(intermediate(value)) {
                    ++softened;const int distance=int(x)-int(y);
                    require(distance>=-3&&distance<=3,"FXAA blurred pixels away from the detected edge");
                    gray(value,value&1023u,"FXAA introduced a tint on a grayscale edge");
                }
            }
            require(softened>=16,"FXAA failed to introduce subpixel coverage along the diagonal");
            require(pixel(output,width,60,2)==white&&pixel(output,width,2,29)==black,
                    "FXAA changed flat areas away from edges");
        }
        // Resolve twice to exercise the cached output and preserved context on
        // reuse, rather than accepting only a successful initial allocation.
        std::shared_ptr<RenderTarget> again;resolvePreserving(backend,front,again);
        require(again==result&&backend.readbackTarget(again)==output,"Repeated AA resolve changed output or leaked target allocation");
        std::printf("Verified WARP AA mode %u: scene %ux%u, output %ux%u, edge coverage and native context restoration\n",
                    unsigned(mode),front->pixelWidth(),front->pixelHeight(),result->pixelWidth(),result->pixelHeight());
        return output;
    }
};
}

int main() {
    try {
        NativeAntialiasingProbe::run(Antialiasing::Original);
        const auto fxaa=NativeAntialiasingProbe::run(Antialiasing::FXAA);
        const auto combined=NativeAntialiasingProbe::run(Antialiasing::FXAAOriginal);
        require(fxaa==combined,"FXAA modes apply different final filters to identical front pixels");
        NativeAntialiasingProbe::run(Antialiasing::SSAA4x);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
