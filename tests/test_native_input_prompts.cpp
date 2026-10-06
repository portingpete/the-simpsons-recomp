#include "renderer/native_input_prompts.h"
#include "runtime/native_control_settings.h"
#include "renderer/im2d_draw.h"
#include "renderer/native_prompt_icons.generated.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
using namespace Simpsons::Graphics;
namespace {
void need(bool value,const char* message){if(!value)throw Error(message);}
std::array<float,4> alphaBounds(NativePromptIcons::Icon icon) {
    const auto image=NativePromptIcons::lookup(icon);
    need(image.width==64&&image.height==64&&image.rgba.size()==64*64*4,"Prompt source extent changed");
    std::array<float,4> b={64,64,0,0};
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        if(!image.rgba[(y*64+x)*4+3])continue;
        b[0]=std::min(b[0],float(x));b[1]=std::min(b[1],float(y));
        b[2]=std::max(b[2],float(x+1));b[3]=std::max(b[3],float(y+1));
    }
    need(b[2]>b[0]&&b[3]>b[1],"Prompt source has no visible artwork");
    return b;
}
Im2DDraw cellDraw(const std::shared_ptr<Texture>& texture,unsigned cell) {
    Im2DDraw d{};d.texture=texture;d.primitiveType=4;
    d.rasterWidth=d.rasterHeight=64;d.blendWord=0x00010706;
    d.pixelCenterHalf=true;d.colorWriteMask=15;
    const float u=float(cell%4)/4,v=float(cell/4)/4;
    d.vertices={{{.5f,.5f,0,1},{1,1,1,1},{u,v}},
                {{64.5f,.5f,0,1},{1,1,1,1},{u+.25f,v}},
                {{.5f,64.5f,0,1},{1,1,1,1},{u,v+.25f}},
                {{64.5f,64.5f,0,1},{1,1,1,1},{u+.25f,v+.25f}}};
    d.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    d.sampler.AddressU=d.sampler.AddressV=d.sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    d.sampler.MaxAnisotropy=1;d.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
    return d;
}
Im2DDraw promptDraw(unsigned cell,float x,float y,float width,float height,bool list=false) {
    auto draw=cellDraw({},cell);draw.rasterWidth=1280;draw.rasterHeight=720;
    for(auto& v:draw.vertices) {
        v.position[0]=x+(v.position[0]-.5f)*width/64.f;
        v.position[1]=y+(v.position[1]-.5f)*height/64.f;
        v.position[2]=.25f;v.position[3]=.75f;
    }
    if(list) {
        const auto v=draw.vertices;draw.primitiveType=3;
        draw.vertices={v[0],v[1],v[2],v[2],v[1],v[3]};
    }
    return draw;
}
std::array<float,4> bounds(const Im2DDraw& draw) {
    std::array<float,4> b={draw.vertices[0].position[0],draw.vertices[0].position[1],
        draw.vertices[0].position[0],draw.vertices[0].position[1]};
    for(const auto& v:draw.vertices) {
        b[0]=std::min(b[0],v.position[0]);b[1]=std::min(b[1],v.position[1]);
        b[2]=std::max(b[2],v.position[0]);b[3]=std::max(b[3],v.position[1]);
    }
    return b;
}
bool close(float a,float b){return std::abs(a-b)<.001f;}
void unchanged(const Im2DDraw& draw) {
    auto copy=draw;
    need(enlargeInputPromptGlyphs(copy)==0,"Unrecognized prompt geometry was accepted");
    need(copy.vertices.size()==draw.vertices.size()&&
        std::memcmp(copy.vertices.data(),draw.vertices.data(),draw.vertices.size()*sizeof(Im2DVertex))==0,
        "Skipped prompt geometry changed");
}
void geometryChecks() {
    // Independently measure the supplied artwork. Space must remain a wide
    // key with a36px visible height, even when its authored aspect changes.
    const auto spaceArt=alphaBounds(NativePromptIcons::Icon::Space);
    const float spaceAspect=(spaceArt[2]-spaceArt[0])/(spaceArt[3]-spaceArt[1]);
    need(spaceAspect>1.5f&&spaceAspect<2.5f,"Space artwork lost its wide key proportions");
    const float spaceAnchor=1005.5f+40*spaceArt[0]/64;
    // The live pause menu's Select label ends at1003; retain the glyph's
    // left visible edge so increased width goes away from that label.
    auto space=promptDraw(11,1005.5f,626.5f,40,40,true);
    const auto before=space.vertices;
    for(std::size_t i=0;i<space.vertices.size();++i)space.vertices[i].color={.1f*float(i),.7f,.3f,.5f};
    const auto colors=space.vertices;
    need(enlargeInputPromptGlyphs(space)==1,"Complete list glyph was not enlarged");
    auto b=bounds(space);
    need(close(b[0],spaceAnchor)&&b[0]>1003&&close(b[2]-b[0],36*spaceAspect)&&close(b[3]-b[1],36)&&
         close((b[1]+b[3])*.5f,646.5f),"Space height, authored aspect, or label-side anchor changed");
    for(std::size_t i=0;i<space.vertices.size();++i) {
        need(space.vertices[i].color==colors[i].color&&space.vertices[i].position[2]==before[i].position[2]&&
             space.vertices[i].position[3]==before[i].position[3],"Prompt resize changed tint, fade, depth or RHW");
        need((close(space.vertices[i].uv[0],.75f+spaceArt[0]/256)||close(space.vertices[i].uv[0],.75f+spaceArt[2]/256))&&
             (close(space.vertices[i].uv[1],.5f+spaceArt[1]/256)||close(space.vertices[i].uv[1],.5f+spaceArt[3]/256)),
             "Prompt resize did not crop to Space artwork");
    }
    unchanged(space); // Cropped UVs make the transform idempotent.
    auto esc=promptDraw(7,235.5f,626.5f,40,40);
    const auto escArt=alphaBounds(NativePromptIcons::Icon::Esc);
    need(enlargeInputPromptGlyphs(esc)==1,"Complete strip glyph was not enlarged");
    b=bounds(esc);
    need(close(b[2],235.5f+40*escArt[2]/64)&&b[2]<279&&close(b[3]-b[1],44)&&b[0]<235.5f,
         "Left prompt failed to grow away from its label");
    auto inlineSpace=promptDraw(11,500,180,40,40);
    need(enlargeInputPromptGlyphs(inlineSpace)==1,"Inline prompt was not recognized");
    b=bounds(inlineSpace);
    need(b[0]>=500&&b[2]<=540&&b[1]>=180&&b[3]<=220&&close((b[2]-b[0])/(b[3]-b[1]),spaceAspect),
         "Inline prompt exceeded its text slot or stretched its artwork");
    for(unsigned cell:{5u,6u,7u,8u,9u,10u,12u,13u,14u,15u}) {
        auto footer=promptDraw(cell,235.5f,626.5f,40,40);
        need(enlargeInputPromptGlyphs(footer)==1,"Active footer prompt was not recognized");
        const auto extent=bounds(footer);
        need(close(extent[3]-extent[1],44)&&extent[2]<279,
             "Active footer prompt became too short or overlapped its label");
    }
    auto group=promptDraw(4,240,625,40,40);
    need(enlargeInputPromptGlyphs(group)==1,"WASD group was not recognized");
    b=bounds(group);need(close(b[3]-b[1],48),"WASD group did not receive its readable height");
    auto larger=promptDraw(11,1000,610,80,80);
    need(enlargeInputPromptGlyphs(larger)==1,"Large authored glyph was not recognized");
    b=bounds(larger);need(close(b[3]-b[1],std::max(36.f,80*(spaceArt[3]-spaceArt[1])/64)),"Already larger artwork was shrunk");
    auto edge=promptDraw(11,1240,626,40,40);
    need(enlargeInputPromptGlyphs(edge)==1,"Screen-edge prompt was not recognized");
    b=bounds(edge);need(b[2]<=1280&&close((b[2]-b[0])/(b[3]-b[1]),spaceAspect),"Prompt enlargement escaped the viewport");
    auto scaled=promptDraw(11,1500,939,60,60);scaled.rasterWidth=1920;scaled.rasterHeight=1080;
    need(enlargeInputPromptGlyphs(scaled)==1,"1080p prompt was not recognized");
    b=bounds(scaled);need(close(b[3]-b[1],54)&&close(b[2]-b[0],54*spaceAspect),"Prompt sizing did not scale with resolution");
    auto batch=promptDraw(7,235,626,40,40,true),right=promptDraw(11,1005,626,40,40,true);
    batch.vertices.insert(batch.vertices.end(),right.vertices.begin(),right.vertices.end());
    need(enlargeInputPromptGlyphs(batch)==2,"Batched triangle-list glyphs were not handled independently");
    unchanged(promptDraw(1,235,626,40,40)); // Empty atlas cell.
    auto clipped=promptDraw(11,1005,626,40,40);clipped.vertices[0].uv[0]+=.01f;unchanged(clipped);
    auto rotated=promptDraw(11,1005,626,40,40);rotated.vertices[0].position[0]+=3;unchanged(rotated);
    auto crossed=promptDraw(11,1005,626,40,40);std::swap(crossed.vertices[0].uv,crossed.vertices[1].uv);unchanged(crossed);
    auto invalid=promptDraw(11,1005,626,40,40);invalid.vertices[0].position[0]=std::numeric_limits<float>::quiet_NaN();unchanged(invalid);
    auto partial=promptDraw(11,1005,626,40,40,true);partial.vertices.pop_back();unchanged(partial);
    auto depth=promptDraw(11,1005,626,40,40);depth.vertices[2].position[2]=.5f;unchanged(depth);
    auto duplicate=promptDraw(11,1005,626,40,40,true);duplicate.vertices[5]=duplicate.vertices[3];unchanged(duplicate);
}
}
int main(int argc,char** argv){try{
    const bool hardware=argc==2 && std::string_view(argv[1])=="--hardware";
    need(isInputPromptAtlas("buttons",256,256),"Original buttons atlas was not recognized");
    for(auto name:{"3_controller","buttons_extra","Buttons","hud_target_center",""})
        need(!isInputPromptAtlas(name,256,256),"Unrelated game texture was selected as a prompt");
    need(!isInputPromptAtlas("buttons",128,256)&&!isInputPromptAtlas("buttons",256,128),"Wrong atlas dimensions accepted");
    geometryChecks();
    NativeBackend backend(!hardware);NativeInputPrompts prompts;
    const auto atlas=prompts.texture(backend);
    need(atlas==prompts.texture(backend),"Prompt atlas uploaded twice");
    const auto pixels=keyboardMousePromptPixels();
    need(backend.readback(atlas)==pixels,"GPU prompt upload lost RGBA/alpha bytes");
    Simpsons::NativeControlSettings controls;
    need(keyboardMousePromptLayout(controls)==defaultKeyboardMousePromptLayout(),"Default controls changed prompt identities");
    controls.bindings[uint32_t(Simpsons::ControlAction::Jump)]={'Z',0};
    controls.bindings[uint32_t(Simpsons::ControlAction::MoveForward)]={0x26,0};
    const auto customLayout=keyboardMousePromptLayout(controls);
    const auto customAtlas=prompts.texture(backend,customLayout);
    need(customAtlas!=atlas&&customAtlas==prompts.texture(backend,customLayout),"Rebound prompt cache did not update once");
    need(backend.readback(customAtlas)==keyboardMousePromptPixels(customLayout),"Rebound prompt upload lost artwork bytes");
    auto customQuad=promptDraw(11,1005,626,40,40);
    need(enlargeInputPromptGlyphs(customQuad,customLayout)==1,"Rebound physical-key glyph bounds were not recognized");
    need(prompts.texture(backend)!=customAtlas,"Returning to fixed menu prompts did not restore the atlas");
    {
        using NativePromptIcons::Icon;
        constexpr std::array keys{0x5Du,5u,6u,0x6Cu,0x7Cu,0xE2u};
        constexpr std::array expected{Icon::Menu,Icon::Question,Icon::Question,
            Icon::Question,Icon::Question,Icon::Question};
        for(size_t i=0;i<keys.size();++i) {
            auto rebound=controls;rebound.bindings[uint32_t(Simpsons::ControlAction::Jump)]={keys[i],0};
            const auto layout=keyboardMousePromptLayout(rebound);
            need(layout[11]==uint8_t(expected[i]),"Menu or unsupported physical-key prompt identity differs");
            const auto reboundPixels=keyboardMousePromptPixels(layout);
            const auto artwork=NativePromptIcons::lookup(expected[i]);
            for(size_t y=0;y<64;++y)
                need(std::equal(artwork.rgba.begin()+y*64*4,artwork.rgba.begin()+(y+1)*64*4,
                    reboundPixels.begin()+((128+y)*256+192)*4),"Rebound Menu or fallback artwork differs");
        }
    }
    // Keep an independently bound original atlas throughout replacement draws.
    // Switching the input source must never replace the engine's cached owner.
    const std::vector<uint8_t> originalPixels(256*256*4,127);
    const auto original=backend.createTexture(256,256,TextureFormat::RGBA8,originalPixels);
    backend.bindEngineTexture(0,original);
    const auto target=backend.createTarget(64,64,TargetFormat::RGB10A2);
    backend.bindTargets({target,nullptr,nullptr,nullptr},{});
    backend.setViewport({0,0,64,64,0,1});
    using NativePromptIcons::Icon;
    constexpr std::array expected={Icon::Directions,Icon::Count,Icon::Count,Icon::Count,
        Icon::Move,Icon::MouseMove,Icon::Tab,Icon::Esc,Icon::E,Icon::MouseLeft,
        Icon::MouseRight,Icon::Space,Icon::Q,Icon::R,Icon::Ctrl,Icon::Shift};
    for(unsigned cell=0;cell<16;++cell) {
        backend.clearTarget(target,{0,0,1,1});
        backend.queueIm2D(target,{},cellDraw(atlas,cell));
        const auto rendered=backend.readbackTarget(target);
        const auto icon=NativePromptIcons::lookup(expected[cell]);
        for(size_t pixel=0;pixel<64*64;++pixel) {
            uint32_t word;std::memcpy(&word,rendered.data()+4*pixel,4);
            const float alpha=icon.rgba.empty()?0:float(icon.rgba[4*pixel+3])/255;
            for(unsigned channel=0;channel<3;++channel) {
                const float source=icon.rgba.empty()?0:float(icon.rgba[4*pixel+channel])/255;
                const int want=int(std::lround((source*alpha+(channel==2?1-alpha:0))*1023));
                const int got=int((word>>(10*channel))&1023);
                need(std::abs(got-want)<=1,"Prompt UV cell or straight-alpha blend differs");
            }
        }
        backend.requireEngineTexture(0,original);
    }
    need(backend.readback(original)==originalPixels,"Prompt rendering altered original atlas");
    backend.queueIm2D(target,{},cellDraw(original,13));
    backend.flushIm2D();backend.requireEngineTexture(0,original);
    const auto menu=backend.createTarget(1280,720,TargetFormat::RGB10A2);
    backend.bindTargets({menu,nullptr,nullptr,nullptr},{});backend.setViewport({0,0,1280,720,0,1});
    backend.clearTarget(menu,{0,0,1,1});
    auto largeSpace=promptDraw(11,1005.5f,626.5f,40,40,true);largeSpace.texture=atlas;
    need(enlargeInputPromptGlyphs(largeSpace)==1,"GPU menu fixture was not enlarged");
    backend.queueIm2D(menu,{},largeSpace);
    const auto menuPixels=backend.readbackTarget(menu);
    unsigned left=1280,top=720,right=0,bottom=0;
    for(unsigned y=0;y<720;++y)for(unsigned x=0;x<1280;++x) {
        uint32_t word;std::memcpy(&word,menuPixels.data()+4*(y*1280+x),4);
        if((word&0x3FFFFFFF)==(1023u<<20))continue;
        left=std::min(left,x);right=std::max(right,x+1);top=std::min(top,y);bottom=std::max(bottom,y+1);
    }
    const auto spaceArt=alphaBounds(Icon::Space);
    const float expectedWidth=36*(spaceArt[2]-spaceArt[0])/(spaceArt[3]-spaceArt[1]);
    const float expectedLeft=1005.5f+40*spaceArt[0]/64;
    need(right-left>=unsigned(std::floor(expectedWidth))-2&&bottom-top>=34,
         "Rendered Space icon remained too small after geometry expansion");
    need(left>=unsigned(std::floor(expectedLeft))&&left>1003&&right<=unsigned(std::ceil(expectedLeft+expectedWidth))&&
         top>=627&&bottom<=665,"Rendered Space icon moved toward its label or outside its resized quad");
    backend.requireEngineTexture(0,original);
    std::printf("PASS native input prompts on %s: 16 UV cells, alpha blending, readable geometry, original binding preserved\n",hardware?"hardware":"WARP");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL native input prompts: %s\n",e.what());return 1;}}
