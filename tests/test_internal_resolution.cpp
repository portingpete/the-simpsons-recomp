#include "renderer/native_backend.h"
#include "renderer/post_filter.h"
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace Simpsons::Graphics;
namespace {
void need(bool value,const char* why){if(!value)throw Error(why);}
template<class F> void rejects(F action) {
    try {action();}catch(const Error&){return;}
    throw Error("Unsupported render-scale operation did not fail");
}
struct HiddenWindow {
    HWND handle{};
    HiddenWindow() {
        WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"SimpsonsRenderScaleProbe";
        need(RegisterClassW(&type)||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"Render-scale hidden-window registration failed");
        handle=CreateWindowExW(WS_EX_TOOLWINDOW,type.lpszClassName,L"Render-scale GPU test",WS_POPUP,-32000,-32000,1280,720,nullptr,nullptr,type.hInstance,nullptr);
        need(handle&&!IsWindowVisible(handle),"Render-scale presentation test requires a hidden owned window");
    }
    ~HiddenWindow(){if(handle)DestroyWindow(handle);}
};
}
namespace Simpsons::Graphics {
struct NativeRenderResolutionProbe {
    static void check(NativeBackend& backend,const std::shared_ptr<RenderTarget>& color,const std::shared_ptr<DepthTarget>& depth,uint32_t w,uint32_t h) {
        D3D11_TEXTURE2D_DESC c{},d{};color->texture->GetDesc(&c);depth->texture->GetDesc(&d);
        need(c.Width==w&&c.Height==h&&d.Width==w&&d.Height==h,"Scene color/depth storage stayed at display or original resolution");
        UINT count=1;D3D11_VIEWPORT viewport{};backend.context->RSGetViewports(&count,&viewport);
        need(count==1&&viewport.Width==float(w)&&viewport.Height==float(h),"Scene rasterization viewport stayed at 720p");
    }
    static void sampler(NativeBackend& backend,uint32_t anisotropy) {
        D3D11_SAMPLER_DESC original{};original.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        original.AddressU=original.AddressV=original.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        original.MaxAnisotropy=1;original.ComparisonFunc=D3D11_COMPARISON_NEVER;original.MaxLOD=D3D11_FLOAT32_MAX;
        const auto adjusted=backend.materialSampling(original);auto state=backend.sceneSamplerState(adjusted);
        D3D11_SAMPLER_DESC actual{};state->GetDesc(&actual);
        need(actual.Filter==D3D11_FILTER_ANISOTROPIC&&actual.MaxAnisotropy==anisotropy,"Material filtering did not reach a native sampler");
        original.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        need(backend.materialSampling(original).Filter==original.Filter,"Filtering changed point-sampled lookup textures");
    }
    static void scale(uint32_t percent,Antialiasing aa) {
        NativeBackend backend(true,false);backend.configureRendering(1280,720,1,aa,percent);
        const uint32_t samples=aa==Antialiasing::SSAA4x?2u:1u;
        const uint32_t baseW=(1280*percent+50)/100,baseH=(720*percent+50)/100;
        const uint32_t width=((baseW+5)/10)*samples,height=((baseH+5)/10)*samples;
        auto color=backend.createTarget(128,72,TargetFormat::RGB10A2,TargetScale::Scene);
        auto depth=backend.createDepthTarget(128,72,TargetScale::Scene);
        auto half=backend.createTarget(64,36,TargetFormat::RGB10A2,TargetScale::Scene);
        auto query=backend.createTarget(64,8,TargetFormat::RGB10A2);
        need(color->width==128&&color->height==72&&depth->width==128&&depth->height==72,"Render scale changed logical camera dimensions");
        need(half->pixelWidth()==((baseW+10)/20)*samples&&half->pixelHeight()==((baseH+10)/20)*samples,"Render scale failed to resize post resources");
        need(query->pixelWidth()==64&&query->pixelHeight()==8,"Render scale resized fixed query resources");
        need(backend.sceneExtent()==std::array<uint32_t,2>{baseW*samples,baseH*samples},"Scene extent does not include render scale and AA samples");
        need(backend.renderAspect()==16.0/9,"Render-scale integer rounding changed camera aspect");
        backend.resetEngineBindings(color,depth,true);backend.setScissor({0,0,128,72});check(backend,color,depth,width,height);
        const std::array<uint32_t,4> oddScissor{1,3,7,11};backend.setScissor(oddScissor);
        need(backend.scissor()&&*backend.scissor()==oddScissor,"Downscaled odd scissor could not retain its logical edges");
        UINT scissorCount=1;D3D11_RECT actualScissor{};backend.context->RSGetScissorRects(&scissorCount,&actualScissor);
        need(scissorCount==1&&actualScissor.left==LONG(std::floor(double(oddScissor[0])*width/128))&&
             actualScissor.right==LONG(std::ceil(double(oddScissor[2])*width/128)),"Downscaled scissor did not conservatively cover actual integer pixels");
        const D3D11_RECT changedScissor{0,0,1,1};backend.context->RSSetScissorRects(1,&changedScissor);
        need(backend.scissor()&&*backend.scissor()!=oddScissor,"Direct native scissor mutation retained a stale logical receipt");
        const D3D11_VIEWPORT fractionalViewport{1.1f,2.3f,7.7f,11.1f,0.25f,0.75f};backend.setViewport(fractionalViewport);
        const auto logicalViewport=backend.viewport();
        need(logicalViewport&&logicalViewport->TopLeftX==fractionalViewport.TopLeftX&&logicalViewport->TopLeftY==fractionalViewport.TopLeftY&&
             logicalViewport->Width==fractionalViewport.Width&&logicalViewport->Height==fractionalViewport.Height,"Scaled Float32 viewport did not retain logical coordinates");
        const D3D11_VIEWPORT changedViewport{0,0,1,1,0,1};backend.context->RSSetViewports(1,&changedViewport);
        need(backend.viewport()&&backend.viewport()->TopLeftX==0&&backend.viewport()->Width!=fractionalViewport.Width,"Direct viewport mutation retained a stale logical receipt");
        backend.resetEngineBindings(color,depth,true);backend.setScissor({0,0,128,72});
        const auto hud=backend.contentViewport(color,{0,0,128,72,0,1});
        need(std::abs(double(hud.Width)/hud.Height-16.0/9)<1e-6&&hud.TopLeftX>=-0.01f&&hud.TopLeftY>=-0.01f,"Scaled HUD lost its original centered aspect");
        backend.clearTarget(color,{1,0,0,1});
        ScreenDraw draw{};draw.vertices={ScreenVertex{-1,1,0,0},ScreenVertex{1,1,1,0},ScreenVertex{-1,-1,0,1},ScreenVertex{1,-1,1,1}};
        draw.color={0,1,0,1};draw.colorWriteMask=15;draw.blendSelector=3;backend.drawScreen(color,draw);
        PostFilterDraw post{};post.input=color;post.pixelShader=0x82152708;post.vertices={-1,1,1,1,-1,-1};post.pixelConstants[0]={1,1,1,1};
        post.viewport={0,0,64,36,0,0x3f800000};post.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        post.sampler.AddressU=post.sampler.AddressV=post.sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        post.sampler.MaxAnisotropy=1;post.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;post.sampler.MaxLOD=13;
        backend.drawPostFilter(half,post);
        const auto filtered=backend.readbackTarget(half);uint32_t last{};std::memcpy(&last,filtered.data()+filtered.size()-4,4);
        need(last==0xc00ffc00,"Scaled post filter did not cover its final physical pixel");
        const auto output=backend.resolveAntialiasing(color);
        need(output->pixelWidth()==width/samples&&output->pixelHeight()==height/samples,"AA resolve ignored the independent render scale");
        const auto pixels=backend.readbackTarget(output);
        for(const size_t pixel:{size_t(0),size_t(output->pixelWidth()-1),size_t(output->pixelWidth())*(output->pixelHeight()-1),size_t(output->pixelWidth())*output->pixelHeight()-1}) {
            uint32_t code{};std::memcpy(&code,pixels.data()+pixel*4,4);
            need(code==0xc00ffc00,"Scaled rasterization/AA left an uncovered output edge");
        }
        rejects([&]{backend.configureRendering(1280,720,1,aa,100);});
        std::printf("Verified WARP render scale %u%%, AA %u: actual color/depth/post/viewport/HUD and output edges\n",percent,unsigned(aa));
    }
    static void presentation(Antialiasing aa) {
        HiddenWindow window;NativeBackend backend(true,false);backend.configureRendering(1280,720,1,aa,67);
        auto front=backend.createTarget(1280,720,TargetFormat::RGB10A2,TargetScale::Scene);backend.clearTarget(front,{0,1,0,1});
        backend.attachWindow(window.handle,1280,720);
        auto transfer=backend.submitFrontPresentation(front);backend.waitCopy(transfer);transfer.reset();
        ComPtr<ID3D11Texture2D> source;need(SUCCEEDED(backend.swapChain->GetBuffer(0,IID_PPV_ARGS(&source))),"Scaled backbuffer query failed");
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
        need(desc.Width==858&&desc.Height==482,"Rounded 67% scene introduced unwanted presentation bars");
        RECT client{};need(GetClientRect(window.handle,&client)&&client.right==1280&&client.bottom==720,"Render scale changed the physical output window extent");
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
        ComPtr<ID3D11Texture2D> staging;need(SUCCEEDED(backend.device->CreateTexture2D(&desc,nullptr,&staging)),"Scaled presentation staging allocation failed");
        backend.context->CopyResource(staging.Get(),source.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        need(SUCCEEDED(backend.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Scaled presentation readback failed");
        for(const uint32_t y:{0u,481u})for(const uint32_t x:{0u,857u}) {
            uint32_t code{};std::memcpy(&code,static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch+size_t(x)*4,4);
            need(code==0xc00ffc00,"Render-scale presentation introduced a black border or cropped an edge");
        }
        backend.context->Unmap(staging.Get(),0);
        const auto wide=backend.presentationExtent(858,482,1280,360,1280,720);
        need(wide==std::array<uint32_t,2>{1716,482},"Scaled live presentation aspect change did not retain the base scene aspect");
        std::printf("Verified hidden-window 67%% render scale, AA %u: unchanged output client and no rounding bars\n",unsigned(aa));
    }
    static void limits() {
        need(NativeBackend::supportsRendering(3840,2160,16,Antialiasing::SSAA4x,100),"Previous 4K SSAA preset exceeded new scene budget");
        need(NativeBackend::supportsRendering(5120,1440,16,Antialiasing::SSAA4x,100),"Previous widest SSAA preset exceeded new scene budget");
        need(NativeBackend::supportsRendering(3840,2160,16,Antialiasing::Original,200),"200% 4K without SSAA must fit the previous scene budget");
        for(const auto scale:{125u,150u,200u}) {
            need(!NativeBackend::supportsRendering(3840,2160,1,Antialiasing::SSAA4x,scale),"Extreme 4K SSAA scale was accepted");
            need(!NativeBackend::supportsRendering(5120,1440,1,Antialiasing::SSAA4x,scale),"Extreme ultrawide SSAA scale was accepted");
        }
        for(const auto scale:{0u,49u,51u,68u,201u,0xffffffffu})
            need(!NativeBackend::supportsRendering(1280,720,1,Antialiasing::Original,scale),"Unsupported render-scale percentage was accepted");
        NativeBackend backend(true,false);backend.configureRendering(1280,720,1,Antialiasing::SSAA4x,200);
        rejects([&]{backend.configureRendering(3840,2160,1,Antialiasing::SSAA4x,200);});
        need(backend.sceneExtent()==std::array<uint32_t,2>{5120,2880}&&backend.renderAspect()==16.0/9,"Rejected scale mutated the prior valid configuration");
        rejects([&]{backend.createTarget(16384,720,TargetFormat::RGB10A2,TargetScale::Scene);});
        rejects([&]{backend.createDepthTarget(16384,720,TargetScale::Scene);});
        backend.configureRendering(1280,720,1,Antialiasing::Original,100);
        std::puts("Verified supported presets, invalid percentage rejection and bounded color/depth allocations before mutation");
    }
};
}
int main() {
    try {
        for(const auto height:{720u,900u,1080u,1440u,2160u}) {
            const uint32_t width=height*16/9;NativeBackend backend(true,false);
            const uint32_t anisotropy=height==720?4u:height==900?8u:16u;
            backend.configureRendering(width,height,anisotropy);
            auto color=backend.createTarget(1280,720,TargetFormat::RGB10A2,TargetScale::Scene);
            auto front=backend.createTarget(1280,720,TargetFormat::RGB10A2,TargetScale::Scene);
            auto depth=backend.createDepthTarget(1280,720,TargetScale::Scene);
            auto half=backend.createTarget(640,360,TargetFormat::RGB10A2,TargetScale::Scene);
            auto query=backend.createTarget(64,8,TargetFormat::RGB10A2);
            need(color->width==1280&&color->height==720,"Scaling changed original logical camera dimensions");
            need(half->pixelWidth()==width/2&&half->pixelHeight()==height/2,"Post target did not scale with scene resolution");
            need(query->pixelWidth()==64&&query->pixelHeight()==8,"Scaling changed a fixed corona query texture");
            backend.resetEngineBindings(color,depth,true);
            backend.setScissor({0,0,1280,720});
            NativeRenderResolutionProbe::check(backend,color,depth,width,height);
            NativeRenderResolutionProbe::sampler(backend,anisotropy);
            backend.clearTarget(color,{1,0,0,1});
            ScreenDraw draw{};draw.vertices={ScreenVertex{-1,1,0,0},ScreenVertex{1,1,1,0},ScreenVertex{-1,-1,0,1},ScreenVertex{1,-1,1,1}};
            draw.color={0,1,0,1};draw.colorWriteMask=15;draw.blendSelector=3;
            backend.drawScreen(color,draw);
            PostFilterDraw post{};post.input=color;post.pixelShader=0x82152708;post.vertices={-1,1,1,1,-1,-1};post.pixelConstants[0]={1,1,1,1};
            post.viewport={0,0,640,360,0,0x3f800000};
            post.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
            post.sampler.AddressU=post.sampler.AddressV=post.sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
            post.sampler.MaxAnisotropy=1;post.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;post.sampler.MaxLOD=13;
            backend.drawPostFilter(half,post);
            const auto filtered=backend.readbackTarget(half);uint32_t last{};
            std::memcpy(&last,filtered.data()+filtered.size()-4,4);
            need((last&0x3fffffff)==(1023u<<10),"Scaled post filter did not sample and cover its full physical target");
            backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.copyFront(color,front);
            const auto pixels=backend.readbackTarget(front);
            need(pixels.size()==size_t(width)*height*4,"Packed internal readback has the wrong size");
            for(const size_t pixel:{size_t(0),size_t(width-1),size_t(width)*(height-1),size_t(width)*height-1}) {
                uint32_t code{};std::memcpy(&code,pixels.data()+pixel*4,4);
                need((code&0x3fffffff)==(1023u<<10),"Scaled rasterization did not cover the entire internal target");
            }
            std::printf("Verified actual color/depth, scene viewport, post targets and readback at %ux%u\n",width,height);
        }
        NativeRenderResolutionProbe::limits();
        for(const auto scale:{50u,67u,75u,100u,125u,150u,200u}) {
            NativeRenderResolutionProbe::scale(scale,Antialiasing::Original);
            NativeRenderResolutionProbe::scale(scale,Antialiasing::SSAA4x);
        }
        NativeRenderResolutionProbe::scale(67,Antialiasing::FXAA);
        NativeRenderResolutionProbe::scale(67,Antialiasing::FXAAOriginal);
        NativeRenderResolutionProbe::presentation(Antialiasing::Original);
        NativeRenderResolutionProbe::presentation(Antialiasing::SSAA4x);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
