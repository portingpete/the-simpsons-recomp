#include "renderer/native_backend.h"
#include "renderer/im2d_draw.h"
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Simpsons::Graphics;
namespace {
void need(bool value,const char* reason) {if(!value)throw Error(reason);}
uint32_t pixel(const std::vector<uint8_t>& bytes,uint32_t width,uint32_t x,uint32_t y) {
    uint32_t value{};std::memcpy(&value,bytes.data()+(size_t(y)*width+x)*4,4);return value;
}
bool same(const D3D11_VIEWPORT& a,const D3D11_VIEWPORT& b) {
    return a.TopLeftX==b.TopLeftX&&a.TopLeftY==b.TopLeftY&&a.Width==b.Width&&a.Height==b.Height&&a.MinDepth==b.MinDepth&&a.MaxDepth==b.MaxDepth;
}
struct HiddenWindow {
    HWND handle{};
    HiddenWindow() {
        WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"SimpsonsUltrawideProbe";
        need(RegisterClassW(&type)||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"Hidden ultrawide probe registration failed");
        handle=CreateWindowExW(WS_EX_TOOLWINDOW,type.lpszClassName,L"Ultrawide GPU test",WS_POPUP,-32000,-32000,1280,720,nullptr,nullptr,type.hInstance,nullptr);
        need(handle&&!IsWindowVisible(handle),"Ultrawide probe must be a hidden owned test window");
    }
    void resize(uint32_t width,uint32_t height) {
        need(SetWindowPos(handle,nullptr,0,0,int(width),int(height),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"Hidden probe resize failed");
    }
    ~HiddenWindow(){if(handle)DestroyWindow(handle);}
};
Im2DDraw quad(bool preserve,bool half) {
    Im2DDraw draw{};draw.primitiveType=4;draw.rasterWidth=128;draw.rasterHeight=72;draw.blendWord=0x00010001;
    draw.alphaCompare=4;draw.pixelCenterHalf=half;draw.colorWriteMask=15;draw.preserveAspect=preserve;
    draw.vertices={{{0.5f,0.5f,0,1},{0,1,0,1},{0,0}},{{128.5f,0.5f,0,1},{0,1,0,1},{1,0}},
                   {{0.5f,72.5f,0,1},{0,1,0,1},{0,1}},{{128.5f,72.5f,0,1},{0,1,0,1},{1,1}}};
    draw.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    draw.sampler.AddressU=draw.sampler.AddressV=draw.sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    draw.sampler.MaxAnisotropy=1;draw.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;return draw;
}
}

namespace Simpsons::Graphics {
struct NativeUltrawideProbe {
    static D3D11_VIEWPORT viewport(NativeBackend& backend) {
        D3D11_VIEWPORT result{};UINT count=1;backend.context->RSGetViewports(&count,&result);
        need(count==1,"Ultrawide viewport fixture missing");return result;
    }
    static void storage(const std::shared_ptr<RenderTarget>& color,const std::shared_ptr<DepthTarget>& depth,uint32_t width,uint32_t height) {
        D3D11_TEXTURE2D_DESC c{},d{};color->texture->GetDesc(&c);depth->texture->GetDesc(&d);
        need(c.Width==width&&c.Height==height&&d.Width==width&&d.Height==height,"Ultrawide scene/depth allocation kept a 16:9 extent");
    }
    static void sceneExtents() {
        for(const auto extent:{std::array<uint32_t,2>{2560,1080},{3440,1440},{3840,1600},{5120,1440}}) {
            NativeBackend backend(true,false);backend.configureRendering(extent[0],extent[1],1);
            auto color=backend.createTarget(1280,720,TargetFormat::RGB10A2,TargetScale::Scene);
            auto depth=backend.createDepthTarget(1280,720,TargetScale::Scene);storage(color,depth,extent[0],extent[1]);
            const D3D11_VIEWPORT logical{0,0,1280,720,0,1};const auto hud=backend.contentViewport(color,logical);
            const float contentWidth=float(extent[1])*16/9;
            // Physical scale uses Float32 viewport fields. At 1600px the
            // multiply after 1600/720 rounds about 0.00012px above 1600.
            need(std::abs(hud.Width-contentWidth)<0.01f&&std::abs(hud.Height-float(extent[1]))<0.01f&&
                 std::abs(hud.TopLeftX-(float(extent[0])-contentWidth)*0.5f)<0.01f&&std::abs(hud.TopLeftY)<0.01f,
                 "Original HUD content is not centered at its original aspect");
            need(std::abs(backend.renderAspect()-double(extent[0])/extent[1])<1e-12,"Frozen scene aspect differs from internal extent");
            std::printf("Verified WARP ultrawide scene/depth and HUD extent %ux%u\n",extent[0],extent[1]);
        }
        NativeBackend ssaa(true,false);ssaa.configureRendering(2560,1080,1,Antialiasing::SSAA4x);
        auto color=ssaa.createTarget(128,72,TargetFormat::RGB10A2,TargetScale::Scene);
        auto depth=ssaa.createDepthTarget(128,72,TargetScale::Scene);storage(color,depth,512,216);
        need(std::abs(ssaa.renderAspect()-double(2560)/1080)<1e-12,"SSAA changed frozen camera aspect");
    }
    static void im2d() {
        NativeBackend backend(true,false);backend.configureRendering(2560,1080,1);
        auto color=backend.createTarget(128,72,TargetFormat::RGB10A2,TargetScale::Scene);
        auto depth=backend.createDepthTarget(128,72,TargetScale::Scene);
        backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.setViewport({0,0,128,72,0,1});
        const auto original=viewport(backend);
        constexpr uint32_t red=0xc00003ff,green=0xc00ffc00;
        for(bool preserve:{false,true})for(bool half:{false,true})for(bool batch:{false,true}) {
            backend.clearTarget(color,{1,0,0,1});auto draw=quad(preserve,half);
            if(batch){backend.queueIm2D(color,depth,draw);backend.flushIm2D();}else backend.drawIm2D(color,depth,draw);
            need(same(viewport(backend),original),"Ultrawide Im2D immediate/batched draw did not restore original physical viewport");
            const auto bytes=backend.readbackTarget(color);const auto width=color->pixelWidth(),height=color->pixelHeight();
            need(pixel(bytes,width,width/2,height/2)==green,"Ultrawide Im2D content did not draw into the center");
            need(pixel(bytes,width,4,height/2)==(preserve?red:green)&&pixel(bytes,width,width-5,height/2)==(preserve?red:green),
                 "Ultrawide Apt/UI scope or world Im2D used the wrong aspect policy");
        }
        std::puts("Verified Im2D preserveAspect, both pixel centers, immediate/batch coverage and viewport restoration");
    }
    static void movie() {
        NativeBackend backend(true,false);backend.configureRendering(2560,1080,1);
        auto color=backend.createTarget(128,72,TargetFormat::RGB10A2,TargetScale::Scene);
        auto depth=backend.createDepthTarget(128,72,TargetScale::Scene);
        backend.bindTargets({color,nullptr,nullptr,nullptr},depth);backend.setViewport({0,0,128,72,0,1});backend.clearTarget(color,{1,0,0,1});
        const auto original=viewport(backend);MovieDraw draw{};
        draw.vertices={ScreenVertex{-1,-1,0,0},ScreenVertex{1,-1,1,0},ScreenVertex{-1,1,0,1},ScreenVertex{1,1,1,1}};
        draw.retainedDepthWrite=true;draw.retainedDepthCompare=6;
        for(size_t i=0;i<3;++i) {
            const uint32_t extent=i?2:4;const std::vector<uint8_t> plane(extent*extent,i?128:235);
            draw.textures[i]=backend.createTexture(extent,extent,TextureFormat::R8,plane);
            auto& s=draw.samplers[i];s.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxAnisotropy=1;s.ComparisonFunc=D3D11_COMPARISON_NEVER;s.MaxLOD=13;
        }
        backend.drawMovie(color,depth,draw);need(same(viewport(backend),original),"Ultrawide movie failed to restore full scene viewport");
        const auto bytes=backend.readbackTarget(color);const auto width=color->pixelWidth(),height=color->pixelHeight();
        need(pixel(bytes,width,4,height/2)==0xc0000000&&pixel(bytes,width,width-5,height/2)==0xc0000000,"Ultrawide movie bars were not cleared black");
        const auto center=pixel(bytes,width,width/2,height/2);
        need((center&1023)>1000&&((center>>10)&1023)>1000&&((center>>20)&1023)>1000,"Movie did not draw original YUV content inside centered bars");
        std::puts("Verified centered movie content, black bars and viewport restoration");
    }
    static std::vector<uint8_t> backbuffer(NativeBackend& backend,uint32_t width,uint32_t height) {
        ComPtr<ID3D11Texture2D> source;need(SUCCEEDED(backend.swapChain->GetBuffer(0,IID_PPV_ARGS(&source))),"Ultrawide backbuffer query failed");
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
        need(desc.Width==width&&desc.Height==height&&desc.Format==DXGI_FORMAT_R10G10B10A2_UNORM,"Padded swapchain extent/format differs");
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
        ComPtr<ID3D11Texture2D> read;need(SUCCEEDED(backend.device->CreateTexture2D(&desc,nullptr,&read)),"Padded staging allocation failed");
        backend.context->CopyResource(read.Get(),source.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        need(SUCCEEDED(backend.context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped)),"Padded staging map failed");
        std::vector<uint8_t> bytes(size_t(width)*height*4);
        for(uint32_t y=0;y<height;++y)std::memcpy(bytes.data()+size_t(y)*width*4,static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch,size_t(width)*4);
        backend.context->Unmap(read.Get(),0);return bytes;
    }
    static void presentation() {
        HiddenWindow window;NativeBackend backend(true,false);backend.configureRendering(1280,720,1);
        auto source=backend.createTarget(1280,720,TargetFormat::RGB10A2,TargetScale::Scene);
        auto front=backend.createTarget(1280,720,TargetFormat::RGB10A2,TargetScale::Scene);
        auto depth=backend.createDepthTarget(1280,720,TargetScale::Scene);
        backend.bindTargets({source,nullptr,nullptr,nullptr},depth);backend.setViewport({0,0,1280,720,0,1});
        std::vector<uint32_t> pattern(1280*720);for(uint32_t i=0;i<pattern.size();++i)pattern[i]=0xc0000000|(i&1023)|(((i*7+17)&1023)<<10)|(((i*13+91)&1023)<<20);
        backend.context->UpdateSubresource(source->texture.Get(),0,nullptr,pattern.data(),1280*4,UINT(pattern.size()*4));
        const auto before=backend.readbackTarget(source);auto copy=backend.copyFront(source,front);backend.waitCopy(copy);copy.reset();
        backend.attachWindow(window.handle,1280,720);
        auto transfer=backend.submitFrontPresentation(front);backend.waitCopy(transfer);transfer.reset();
        need(backbuffer(backend,1280,720)==before,"Initial exact internal presentation copy differs");
        window.resize(1280,360);backend.configureVideoPresentation(false,1280,360);
        transfer=backend.submitFrontPresentation(front);backend.waitCopy(transfer);transfer.reset();
        const auto wide=backbuffer(backend,2560,720);
        for(uint32_t y=0;y<720;++y) {
            need(std::memcmp(wide.data()+(size_t(y)*2560+640)*4,before.data()+size_t(y)*1280*4,1280*4)==0,"Padded presentation changed internal source pixels");
            for(uint32_t x=0;x<640;++x)need(pixel(wide,2560,x,y)==0xc0000000&&pixel(wide,2560,x+1920,y)==0xc0000000,"Padded presentation bars differ from opaque black");
        }
        need(backend.readbackTarget(source)==before&&backend.readbackTarget(front)==before&&backend.renderAspect()==16.0/9,
             "Live display aspect change altered internal source or frozen scene aspect");
        window.resize(1280,720);backend.configureVideoPresentation(false,1280,720);
        transfer=backend.submitFrontPresentation(front);backend.waitCopy(transfer);transfer.reset();
        need(backbuffer(backend,1280,720)==before,"Returning to 16:9 kept stale padded swapchain data");
        std::puts("Verified hidden-window live aspect resize, exact internal RGB10A2 copy and centered black presentation bars");
    }
};
}
int main() {
    try {NativeUltrawideProbe::sceneExtents();NativeUltrawideProbe::im2d();NativeUltrawideProbe::movie();NativeUltrawideProbe::presentation();return 0;}
    catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
