#include "native_storage_screen.h"
#include "native_backend.h"
#include "native_prompt_icons.generated.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <limits>
#include <span>

namespace Simpsons::Graphics {
namespace {
using Color=std::array<uint8_t,4>;
constexpr Color yellow{255,230,0,255},white{220,235,255,255},black{0,0,0,255};
struct Glyph {int width{},height{},advance{},bearingX{},bearingY{},x{},y{};};
struct Font {
    uint32_t width{},height{},point{};
    std::array<Glyph,256> glyphs{};
    std::vector<uint8_t> rgba;
    explicit Font(const std::filesystem::path& path) {
        std::ifstream file(path,std::ios::binary|std::ios::ate);
        if(!file)throw Error("Native Save Storage menu font is missing; rebuild native assets");
        const auto length=file.tellg();
        if(length<24 || length>8*1024*1024)throw Error("Native Save Storage font fixture size is invalid");
        std::vector<uint8_t> data(static_cast<size_t>(length));file.seekg(0);
        file.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(data.size()));
        if(!file || std::memcmp(data.data(),"NSSFONT1",8))throw Error("Native Save Storage font fixture is invalid");
        auto word=[&](size_t at){uint32_t value{};std::memcpy(&value,data.data()+at,4);return value;};
        width=word(8);height=word(12);point=word(16);const auto count=word(20);
        if(!width || !height || width>2048 || height>2048 || !point || point>128 || count>256 ||
           data.size()!=24+size_t(count)*32+size_t(width)*height*4)
            throw Error("Native Save Storage font fixture layout is invalid");
        std::array<bool,256> seen{};
        for(uint32_t i=0;i<count;++i) {
            const size_t at=24+size_t(i)*32;const auto code=word(at);
            if(code>=256 || seen[code])throw Error("Native Save Storage font has duplicate character metrics");
            auto integer=[&](size_t offset){return std::bit_cast<int32_t>(word(at+offset));};
            auto& g=glyphs[code];g={integer(4),integer(8),integer(12),integer(16),integer(20),integer(24),integer(28)};
            if(g.width<=0 || g.height<=0 || g.advance<0 || g.advance>256 || g.width>256 || g.height>256 ||
               g.x<0 || g.y<0 || uint64_t(g.x)+uint32_t(g.width)>width || uint64_t(g.y)+uint32_t(g.height)>height ||
               g.bearingX<-256 || g.bearingX>256 || g.bearingY<-256 || g.bearingY>256)
                throw Error("Native Save Storage glyph lies outside its owned atlas");
            seen[code]=true;
        }
        if(!seen['?'])throw Error("Native Save Storage font lacks its fallback glyph");
        for(unsigned c=32;c<127;++c)if(!seen[c])throw Error("Native Save Storage font lacks printable ASCII");
        rgba.assign(data.begin()+24+size_t(count)*32,data.end());
    }
    const Glyph& glyph(wchar_t character) const {
        return glyphs[character<256 && glyphs[character].width?character:'?'];
    }
    float textWidth(std::wstring_view text,float size) const {
        float widthSum=0;for(auto c:text)widthSum+=float(glyph(c).advance);return widthSum*size/float(point);
    }
    float alpha(float x,float y) const {
        if(x<0 || y<0 || x>=float(width) || y>=float(height))return 0;
        const auto left=uint32_t(x),top=uint32_t(y),right=std::min(left+1,width-1),bottom=std::min(top+1,height-1);
        const float fx=x-float(left),fy=y-float(top);
        auto a=[&](uint32_t xx,uint32_t yy){return float(rgba[(size_t(yy)*width+xx)*4+3]);};
        return (a(left,top)*(1-fx)+a(right,top)*fx)*(1-fy)+(a(left,bottom)*(1-fx)+a(right,bottom)*fx)*fy;
    }
};
struct Canvas {
    uint32_t width,height;
    float scale,offsetX,offsetY;
    std::vector<uint8_t> rgba;
    Canvas(uint32_t w,uint32_t h):width(w),height(h),scale(std::min(float(w)/1280,float(h)/720)),
        offsetX((float(w)-1280*scale)*.5f),offsetY((float(h)-720*scale)*.5f),rgba(size_t(w)*h*4) {}
    void blend(int x,int y,const Color& color,float coverage=1) {
        if(x<0 || y<0 || uint32_t(x)>=width || uint32_t(y)>=height || coverage<=0)return;
        const size_t at=(size_t(y)*width+uint32_t(x))*4;
        const float source=std::clamp(coverage*float(color[3])/255,0.f,1.f);
        const float destination=float(rgba[at+3])/255;
        const float combined=source+destination*(1-source);
        if(combined<=0)return;
        for(size_t channel=0;channel<3;++channel)rgba[at+channel]=uint8_t(std::clamp(std::lround(
            (float(color[channel])*source+float(rgba[at+channel])*destination*(1-source))/combined),0l,255l));
        rgba[at+3]=uint8_t(std::clamp(std::lround(combined*255),0l,255l));
    }
    void disc(float cx,float cy,float radius,const Color& color) {
        cx=offsetX+cx*scale;cy=offsetY+cy*scale;radius*=scale;
        const int left=int(std::floor(cx-radius-1)),right=int(std::ceil(cx+radius+1));
        const int top=int(std::floor(cy-radius-1)),bottom=int(std::ceil(cy+radius+1));
        for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
            const float dx=float(x)+.5f-cx,dy=float(y)+.5f-cy;
            blend(x,y,color,std::clamp(radius+.5f-std::sqrt(dx*dx+dy*dy),0.f,1.f));
        }
    }
    void underline() {
        constexpr std::array<float,13> heights{0,1,.3f,-.7f,.1f,1.1f,.3f,-.4f,.2f,.7f,-.2f,-.8f,0};
        for(unsigned pass=0;pass<2;++pass)for(int x=470;x<=810;++x) {
            const float position=float(x-470)/340*12;const auto segment=std::min(size_t(position),size_t(11));
            const float fraction=position-float(segment),y=171+heights[segment]*(1-fraction)+heights[segment+1]*fraction;
            disc(float(x),y,pass?2.f:4.5f,pass?yellow:black);
        }
    }
    void text(const Font& font,std::wstring_view text,float x,float y,float size,const Color& color,
              bool centered=false,float outline=1.5f) {
        if(text.empty())return;
        const float fontScale=size/float(font.point),rasterScale=fontScale*scale;
        const float logicalWidth=font.textWidth(text,size);
        if(centered)x-=logicalWidth*.5f;
        int minimumY=256;for(auto c:text)if(c!=L' ')minimumY=std::min(minimumY,font.glyph(c).bearingY);
        if(minimumY==256)return;
        const float left=offsetX+x*scale,top=offsetY+y*scale-float(minimumY)*rasterScale;
        const int radius=std::max(1,int(std::ceil(outline*scale))),padding=radius+2;
        int lineHeight=0;for(auto c:text)lineHeight=std::max(lineHeight,font.glyph(c).height+font.glyph(c).bearingY-minimumY);
        const int maskWidth=int(std::ceil(logicalWidth*scale))+padding*2+int(std::ceil(6*rasterScale));
        const int maskHeight=int(std::ceil(float(lineHeight)*rasterScale))+padding*2;
        if(maskWidth<=0 || maskHeight<=0 || maskWidth>int(width)*2 || maskHeight>int(height))return;
        std::vector<uint8_t> mask(size_t(maskWidth)*size_t(maskHeight));float cursor=float(padding);
        for(auto c:text) {
            const auto& g=font.glyph(c);
            if(c!=L' ') {
                const float gx=cursor+float(g.bearingX)*rasterScale;
                // Padding is in output pixels; font bearings are in atlas pixels.
                const float correctedY=float(padding)+float(g.bearingY-minimumY)*rasterScale;
                const int x0=int(std::floor(gx)),y0=int(std::floor(correctedY));
                const int x1=int(std::ceil(gx+float(g.width)*rasterScale)),y1=int(std::ceil(correctedY+float(g.height)*rasterScale));
                for(int yy=std::max(0,y0);yy<std::min(maskHeight,y1);++yy)for(int xx=std::max(0,x0);xx<std::min(maskWidth,x1);++xx) {
                    const float sx=(float(xx)+.5f-gx)/rasterScale-.5f,sy=(float(yy)+.5f-correctedY)/rasterScale-.5f;
                    if(sx<-.5f || sy<-.5f || sx>float(g.width)-.5f || sy>float(g.height)-.5f)continue;
                    const auto alpha=uint8_t(std::clamp(std::lround(font.alpha(float(g.x)+std::clamp(sx,0.f,float(g.width-1)),
                        float(g.y)+std::clamp(sy,0.f,float(g.height-1)))),0l,255l));
                    auto& destination=mask[size_t(yy)*size_t(maskWidth)+size_t(xx)];destination=std::max(destination,alpha);
                }
            }
            cursor+=float(g.advance)*rasterScale;
        }
        const int originX=int(std::lround(left))-padding,originY=int(std::lround(top+float(minimumY)*rasterScale))-padding;
        // Draw a circular black stroke around the same alpha mask as the game
        // font; the menu's outlined captions remain legible over the blue TV.
        for(int yy=0;yy<maskHeight;++yy)for(int xx=0;xx<maskWidth;++xx) {
            uint8_t coverage=0;
            for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx) {
                if(dx*dx+dy*dy>radius*radius)continue;
                const int sx=xx+dx,sy=yy+dy;
                if(sx>=0 && sy>=0 && sx<maskWidth && sy<maskHeight)
                    coverage=std::max(coverage,mask[size_t(sy)*size_t(maskWidth)+size_t(sx)]);
            }
            if(coverage)blend(originX+xx,originY+yy,black,float(coverage)/255);
        }
        for(int yy=0;yy<maskHeight;++yy)for(int xx=0;xx<maskWidth;++xx) {
            const auto coverage=mask[size_t(yy)*size_t(maskWidth)+size_t(xx)];
            if(coverage)blend(originX+xx,originY+yy,color,float(coverage)/255);
        }
    }
    void icon(NativePromptIcons::Icon selected,float x,float y,float maximumWidth,float maximumHeight) {
        const auto source=NativePromptIcons::lookup(selected);int left=int(source.width),top=int(source.height),right=0,bottom=0;
        for(unsigned yy=0;yy<source.height;++yy)for(unsigned xx=0;xx<source.width;++xx)if(source.rgba[(size_t(yy)*source.width+xx)*4+3]) {
            left=std::min(left,int(xx));top=std::min(top,int(yy));right=std::max(right,int(xx+1));bottom=std::max(bottom,int(yy+1));
        }
        if(right<=left || bottom<=top)return;
        const float fitted=std::min(maximumWidth/float(right-left),maximumHeight/float(bottom-top));
        const float outputWidth=float(right-left)*fitted*scale,outputHeight=float(bottom-top)*fitted*scale;
        const int x0=int(std::lround(offsetX+x*scale)),y0=int(std::lround(offsetY+y*scale));
        for(int yy=0;yy<int(std::ceil(outputHeight));++yy)for(int xx=0;xx<int(std::ceil(outputWidth));++xx) {
            const auto sx=unsigned(left)+std::min(unsigned(right-left-1),unsigned(float(xx)/outputWidth*float(right-left)));
            const auto sy=unsigned(top)+std::min(unsigned(bottom-top-1),unsigned(float(yy)/outputHeight*float(bottom-top)));
            const size_t at=(size_t(sy)*source.width+sx)*4;
            blend(x0+xx,y0+yy,{source.rgba[at],source.rgba[at+1],source.rgba[at+2],source.rgba[at+3]});
        }
    }
};
std::wstring capacity(uint64_t value) {
    wchar_t output[64]{};
    if(value>=1024ull*1024*1024)std::swprintf(output,std::size(output),L"%.1f GB",double(value)/(1024.0*1024*1024));
    else std::swprintf(output,std::size(output),L"%.1f MB",double(value)/(1024.0*1024));
    return output;
}
ScreenDraw screenQuad(const std::shared_ptr<Texture>& texture) {
    ScreenDraw draw{};draw.vertices={ScreenVertex{-1,1,0,0},ScreenVertex{1,1,1,0},ScreenVertex{-1,-1,0,1},ScreenVertex{1,-1,1,1}};
    draw.color={1,1,1,1};draw.texture=texture;draw.blendSelector=1;draw.colorWriteMask=15;
    draw.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    draw.sampler.AddressU=draw.sampler.AddressV=draw.sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    draw.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;draw.sampler.MaxLOD=D3D11_FLOAT32_MAX;
    return draw;
}
}
struct NativeStorageScreen::Impl {
    HWND child;
    uint32_t width,height;
    std::unique_ptr<NativeBackend> backend;
    std::shared_ptr<RenderTarget> target;
    std::shared_ptr<Texture> background,overlay;
    Font body,title;
    StorageScreenBackground capturedBackground;
    std::optional<NativeStorageScreenState> previous;
    Impl(HWND window,uint32_t w,uint32_t h,StorageScreenBackground backdrop,const std::filesystem::path& assets)
        :child(window),width(w),height(h),body(assets/L"storage-font.dat"),title(assets/L"storage-title.dat"),capturedBackground(std::move(backdrop)) {
        if(!child || !width || !height)throw Error("Native Save Storage screen needs a child window and nonempty extent");
        if(!capturedBackground.rgba.empty()) {
            if(!capturedBackground.width || !capturedBackground.height || capturedBackground.rgba.size()!=size_t(capturedBackground.width)*capturedBackground.height*4)
                throw Error("Native Save Storage background byte extent is invalid");
        }
        attach();
    }
    void attach() {
        backend=std::make_unique<NativeBackend>(false,true);backend->attachWindow(child,width,height);
        target=backend->createTarget(width,height,TargetFormat::RGB10A2);
        overlay=backend->createWritableTexture(width,height,TextureFormat::RGBA8);
        if(!capturedBackground.rgba.empty())background=backend->createTexture(capturedBackground.width,capturedBackground.height,
            TextureFormat::RGBA8,capturedBackground.rgba);
    }
    bool render(const NativeStorageScreenState& state) {
        if(previous && *previous==state)return backend->presentFront(target);
        Canvas canvas(width,height);
        canvas.text(title,L"Save Storage",640,112,46,white,true,2.1f);canvas.underline();
        canvas.text(body,L"Saves are stored in root/saves",640,220,28,yellow,true);
        const auto player=L"Player: "+state.player;
        canvas.text(body,player,640,276,std::min(25.f,770.f/std::max(body.textWidth(player,1),1.f)),white,true);
        const bool canSave=state.storageAvailable && state.availableBytes>=state.requiredBytes;
        const auto info=state.storageAvailable?L"Available: "+capacity(state.availableBytes)+L"     Needed: "+capacity(state.requiredBytes):L"This save folder is unavailable";
        canvas.text(body,info,640,354,21,white,true);
        if(state.storageAvailable && !canSave)canvas.text(body,L"Not enough free space",640,451,18,yellow,true);
        constexpr std::array<std::wstring_view,2> rows{L"Continue",L"Continue without saving"};
        for(unsigned row=0;row<2;++row) {
            const bool selected=state.selectedRow==row;const float y=471+float(row)*56;
            auto color=selected?white:yellow;if(row==0 && !canSave)color={164,169,184,255};
            canvas.text(body,rows[row],640,y,30,color,true,1.8f);
            if(selected) {canvas.text(body,L">",364,y,30,yellow,false,1.8f);canvas.text(body,L"<",898,y,30,yellow,false,1.8f);}
        }
        if(state.controller) {
            canvas.disc(260,650,19,black);canvas.disc(260,650,16,{198,38,25,255});
            canvas.text(body,L"B",260,639,23,white,true,1);canvas.text(body,L"Cancel",291,634,30,white);
            canvas.text(body,L"Select",908,634,30,white);canvas.disc(1050,650,19,black);canvas.disc(1050,650,16,{50,157,40,255});
            canvas.text(body,L"A",1050,639,23,white,true,1);
        } else {
            canvas.icon(NativePromptIcons::Icon::Esc,232,631,42,38);canvas.text(body,L"Cancel",283,634,30,white);
            canvas.text(body,L"Select",908,634,30,white);canvas.icon(NativePromptIcons::Icon::Space,1012,634,64,35);
        }
        canvas.text(body,L"Enter / Space or A: Select     Esc or B: Cancel",640,689,17,yellow,true,1);
        backend->writeTexture(overlay,canvas.rgba);backend->clearTarget(target,{0,0,0,1});
        if(background) {
            auto draw=screenQuad(background);draw.blendSelector=3;
            // Preserve the captured frame's aspect. A 16:9 game frame shown on
            // an ultrawide output has the same black side margins as the game.
            const float fit=std::min(float(width)/float(capturedBackground.width),float(height)/float(capturedBackground.height));
            const float halfWidth=float(capturedBackground.width)*fit/float(width),halfHeight=float(capturedBackground.height)*fit/float(height);
            draw.vertices={ScreenVertex{-halfWidth,halfHeight,0,0},ScreenVertex{halfWidth,halfHeight,1,0},
                ScreenVertex{-halfWidth,-halfHeight,0,1},ScreenVertex{halfWidth,-halfHeight,1,1}};
            backend->drawScreen(target,draw);
        }
        else backend->clearTarget(target,{.14f,.30f,.72f,1}); // Renderer-only fixtures have no completed game frame.
        backend->drawScreen(target,screenQuad(overlay));previous=state;return backend->presentFront(target);
    }
};
NativeStorageScreen::NativeStorageScreen(HWND child,uint32_t width,uint32_t height,StorageScreenBackground background,
    const std::filesystem::path& assets):impl(std::make_unique<Impl>(child,width,height,std::move(background),assets)) {}
NativeStorageScreen::~NativeStorageScreen()=default;
bool NativeStorageScreen::render(const NativeStorageScreenState& state) {return impl->render(state);}
void NativeStorageScreen::resize(uint32_t width,uint32_t height) {
    if(!width || !height || (impl->width==width && impl->height==height))return;
    // NativeBackend freezes its rendering source extent at attachment. A new
    // client extent therefore gets a new owned backend/swapchain after every
    // old draw retires; no old context or swapchain is reused at the new size.
    impl->backend->waitIdle();impl->target.reset();impl->overlay.reset();impl->background.reset();impl->backend.reset();
    impl->width=width;impl->height=height;
    impl->attach();
    impl->previous.reset();
}
std::array<StorageScreenRect,2> NativeStorageScreen::rowRectangles() {return {StorageScreenRect{330,458,950,505},StorageScreenRect{330,514,950,561}};}
int NativeStorageScreen::hitTest(int clientX,int clientY) const {
    const float scale=std::min(float(impl->width)/1280,float(impl->height)/720);
    const float x=(float(clientX)-(float(impl->width)-1280*scale)*.5f)/scale;
    const float y=(float(clientY)-(float(impl->height)-720*scale)*.5f)/scale;
    const auto rows=rowRectangles();
    for(unsigned row=0;row<rows.size();++row)if(x>=rows[row].left && x<rows[row].right && y>=rows[row].top && y<rows[row].bottom)return int(row);
    return -1;
}
std::vector<uint8_t> NativeStorageScreen::readbackRGBA() {
    auto result=impl->backend->readbackTarget(impl->target);
    for(size_t i=0;i<result.size();i+=4) {
        uint32_t word{};std::memcpy(&word,result.data()+i,4);
        result[i]=uint8_t(((word&1023)*255+511)/1023);
        result[i+1]=uint8_t((((word>>10)&1023)*255+511)/1023);
        result[i+2]=uint8_t((((word>>20)&1023)*255+511)/1023);result[i+3]=255;
    }
    return result;
}
uint32_t NativeStorageScreen::width() const {return impl->width;}
uint32_t NativeStorageScreen::height() const {return impl->height;}
}
