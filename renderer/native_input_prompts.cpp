#include "native_input_prompts.h"
#include "im2d_draw.h"
#include "native_prompt_icons.generated.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>

namespace Simpsons::Graphics {
namespace {
using NativePromptIcons::Icon;
// Authored frontend/shared-character atlas order. Its first row has three
// empty cells. Input aliases select one representative native prompt per cell.
constexpr std::array cells={
    Icon::Directions,Icon::Count,Icon::Count,Icon::Count,
    Icon::Move,Icon::MouseMove,Icon::Tab,Icon::Esc,
    Icon::E,Icon::MouseLeft,Icon::MouseRight,Icon::Space,
    Icon::Q,Icon::R,Icon::Ctrl,Icon::Shift};
struct Bounds {float left{},top{},right{},bottom{};};
const std::array<Bounds,16>& artworkBounds() {
    static const auto bounds=[] {
        std::array<Bounds,16> result{};
        for(std::size_t cell=0;cell<cells.size();++cell) {
            const auto image=NativePromptIcons::lookup(cells[cell]);
            if(image.rgba.empty())continue;
            auto& b=result[cell];b.left=b.top=64;
            for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
                if(!image.rgba[(y*64+x)*4+3])continue;
                b.left=std::min(b.left,float(x));b.top=std::min(b.top,float(y));
                b.right=std::max(b.right,float(x+1));b.bottom=std::max(b.bottom,float(y+1));
            }
        }
        return result;
    }();
    return bounds;
}
bool nearlyEqual(float a,float b) {return std::abs(a-b)<=0.0001f;}
bool enlargeQuad(std::span<Im2DVertex> vertices,float rasterWidth,float rasterHeight,bool strip) {
    Bounds screen{vertices[0].position[0],vertices[0].position[1],vertices[0].position[0],vertices[0].position[1]};
    Bounds uv{vertices[0].uv[0],vertices[0].uv[1],vertices[0].uv[0],vertices[0].uv[1]};
    for(const auto& v:vertices) {
        for(float p:v.position)if(!std::isfinite(p))return false;
        for(float p:v.uv)if(!std::isfinite(p))return false;
        if(!nearlyEqual(v.position[2],vertices[0].position[2])||!nearlyEqual(v.position[3],vertices[0].position[3]))return false;
        screen.left=std::min(screen.left,v.position[0]);screen.right=std::max(screen.right,v.position[0]);
        screen.top=std::min(screen.top,v.position[1]);screen.bottom=std::max(screen.bottom,v.position[1]);
        uv.left=std::min(uv.left,v.uv[0]);uv.right=std::max(uv.right,v.uv[0]);
        uv.top=std::min(uv.top,v.uv[1]);uv.bottom=std::max(uv.bottom,v.uv[1]);
    }
    const float width=screen.right-screen.left,height=screen.bottom-screen.top;
    if(width<=0||height<=0||!nearlyEqual(uv.right-uv.left,.25f)||!nearlyEqual(uv.bottom-uv.top,.25f))return false;
    const float column=std::round(uv.left*4),row=std::round(uv.top*4);
    if(column<0||column>3||row<0||row>3||!nearlyEqual(uv.left,column*.25f)||!nearlyEqual(uv.top,row*.25f))return false;
    const auto cell=unsigned(row)*4+unsigned(column);
    if(cells[cell]==Icon::Count)return false;
    // Require two triangles covering exactly one axis-aligned rectangle, with
    // matching unflipped UV corners. Skip clipped, rotated or arbitrary meshes.
    std::array<unsigned,6> corners{};
    for(std::size_t i=0;i<vertices.size();++i) {
        const auto& v=vertices[i];
        const bool right=nearlyEqual(v.position[0],screen.right),bottom=nearlyEqual(v.position[1],screen.bottom);
        if((!right&&!nearlyEqual(v.position[0],screen.left))||(!bottom&&!nearlyEqual(v.position[1],screen.top))||
           !nearlyEqual(v.uv[0],right?uv.right:uv.left)||!nearlyEqual(v.uv[1],bottom?uv.bottom:uv.top))return false;
        corners[i]=(right?1u:0u)|(bottom?2u:0u);
    }
    const auto triangleMask=[&](unsigned a,unsigned b,unsigned c){return (1u<<corners[a])|(1u<<corners[b])|(1u<<corners[c]);};
    const unsigned first=triangleMask(0,1,2),second=strip?triangleMask(1,2,3):triangleMask(3,4,5);
    if(std::popcount(first)!=3||std::popcount(second)!=3||(first|second)!=15||
       ((first&second)!=9&&(first&second)!=6))return false;
    const auto b=artworkBounds()[cell];
    const float aspect=(b.right-b.left)/(b.bottom-b.top);
    const float scale=rasterHeight/720.f;
    const float centerX=(screen.left+screen.right)*.5f,centerY=(screen.top+screen.bottom)*.5f;
    const bool leftFooter=centerY>rasterHeight*.75f&&centerX<rasterWidth*.3f;
    const bool rightFooter=centerY>rasterHeight*.75f&&centerX>rasterWidth*.7f;
    float desiredHeight=(cells[cell]==Icon::Move||cells[cell]==Icon::Directions?48.f:cells[cell]==Icon::Space?36.f:44.f)*scale;
    desiredHeight=std::max(desiredHeight,height*(b.bottom-b.top)/64.f);
    float outputHeight,outputWidth,left,top;
    if(leftFooter||rightFooter) {
        // Keep the visible edge next to the label fixed, not the old cell's
        // transparent border. Clamp growth to the outside screen edge.
        const float anchor=leftFooter?screen.left+width*b.right/64.f:screen.left+width*b.left/64.f;
        const float available=leftFooter?anchor:rasterWidth-anchor;
        const float verticalAvailable=2*std::min(centerY,rasterHeight-centerY);
        if(available<=0||verticalAvailable<=0)return false;
        outputHeight=std::min({desiredHeight,available/aspect,verticalAvailable});
        outputWidth=outputHeight*aspect;
        left=leftFooter?anchor-outputWidth:anchor;
    } else {
        // An inline glyph can have text on both sides. Its complete original
        // slot is the only proven free space, so never widen that slot.
        outputHeight=std::min({desiredHeight,height,width/aspect});
        outputWidth=outputHeight*aspect;left=centerX-outputWidth*.5f;
    }
    top=centerY-outputHeight*.5f;
    for(std::size_t i=0;i<vertices.size();++i) {
        auto& v=vertices[i];const bool right=(corners[i]&1)!=0,bottom=(corners[i]&2)!=0;
        v.position[0]=left+(right?outputWidth:0);v.position[1]=top+(bottom?outputHeight:0);
        v.uv[0]=uv.left+(right?b.right:b.left)/256.f;
        v.uv[1]=uv.top+(bottom?b.bottom:b.top)/256.f;
    }
    return true;
}
}
bool isInputPromptAtlas(std::string_view name,uint32_t width,uint32_t height) {
    return name=="buttons" && width==256 && height==256;
}
std::vector<uint8_t> keyboardMousePromptPixels() {
    std::vector<uint8_t> pixels(256*256*4,0);
    for(size_t cell=0;cell<cells.size();++cell) {
        if(cells[cell]==Icon::Count)continue;
        const auto icon=NativePromptIcons::lookup(cells[cell]);
        if(icon.width!=64 || icon.height!=64 || icon.rgba.size()!=64*64*4)
            throw Error("Native input prompt icon extent differs from its atlas cell");
        for(size_t y=0;y<64;++y) {
            const size_t offset=((cell/4*64+y)*256+cell%4*64)*4;
            std::copy_n(icon.rgba.data()+y*64*4,64*4,pixels.data()+offset);
        }
    }
    return pixels;
}
std::size_t enlargeInputPromptGlyphs(Im2DDraw& draw) {
    if(!draw.rasterWidth||!draw.rasterHeight)return 0;
    std::size_t changed=0;
    if(draw.primitiveType==3&&draw.vertices.size()%6==0) {
        for(std::size_t offset=0;offset<draw.vertices.size();offset+=6)
            changed+=enlargeQuad(std::span(draw.vertices).subspan(offset,6),float(draw.rasterWidth),float(draw.rasterHeight),false);
    } else if(draw.primitiveType==4&&draw.vertices.size()==4) {
        changed=enlargeQuad(draw.vertices,float(draw.rasterWidth),float(draw.rasterHeight),true);
    }
    return changed;
}
std::shared_ptr<Texture> NativeInputPrompts::texture(NativeBackend& backend) {
    backend.validateSubmissionContext();
    if(!atlas) {
        const auto pixels=keyboardMousePromptPixels();
        atlas=backend.createTexture(256,256,TextureFormat::RGBA8,pixels);
        std::fprintf(stderr,"[NATIVE INPUT PROMPTS] Yellow keyboard/mouse atlas uploaded; readable native glyph sizing enabled\n");
    }
    backend.validateTexture(atlas);
    return atlas;
}
}
