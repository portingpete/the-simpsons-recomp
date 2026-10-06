#include "native_input_prompts.h"
#include "im2d_draw.h"
#include "native_prompt_icons.generated.h"
#include "runtime/native_control_settings.h"
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
const std::array<Bounds,16>& artworkBounds(const NativePromptLayout& layout) {
    static NativePromptLayout previous{};
    static std::array<Bounds,16> result{};
    static bool ready=false;
    if(!ready||previous!=layout) {
        result={};const auto pixels=keyboardMousePromptPixels(layout);
        for(std::size_t cell=0;cell<cells.size();++cell) {
            auto& b=result[cell];b.left=b.top=64;
            for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
                if(!pixels[((cell/4*64+y)*256+cell%4*64+x)*4+3])continue;
                b.left=std::min(b.left,float(x));b.top=std::min(b.top,float(y));
                b.right=std::max(b.right,float(x+1));b.bottom=std::max(b.bottom,float(y+1));
            }
        }
        previous=layout;ready=true;
    }
    return result;
}
bool nearlyEqual(float a,float b) {return std::abs(a-b)<=0.0001f;}
bool enlargeQuad(std::span<Im2DVertex> vertices,float rasterWidth,float rasterHeight,bool strip,const NativePromptLayout& layout) {
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
    if(layout[cell]>=uint8_t(Icon::Count))return false;
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
    const auto b=artworkBounds(layout)[cell];
    if(b.right<=b.left||b.bottom<=b.top)return false;
    const float aspect=(b.right-b.left)/(b.bottom-b.top);
    const float scale=rasterHeight/720.f;
    const float centerX=(screen.left+screen.right)*.5f,centerY=(screen.top+screen.bottom)*.5f;
    const bool leftFooter=centerY>rasterHeight*.75f&&centerX<rasterWidth*.3f;
    const bool rightFooter=centerY>rasterHeight*.75f&&centerX>rasterWidth*.7f;
    const auto selected=Icon(layout[cell]);
    float desiredHeight=(selected==Icon::Move||selected==Icon::Directions?48.f:selected==Icon::Space?36.f:44.f)*scale;
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
    return keyboardMousePromptPixels(defaultKeyboardMousePromptLayout());
}
NativePromptLayout defaultKeyboardMousePromptLayout() {
    NativePromptLayout layout{};
    for(size_t i=0;i<cells.size();++i)layout[i]=uint8_t(cells[i]);
    layout[16]=uint8_t(Icon::W);layout[17]=uint8_t(Icon::A);layout[18]=uint8_t(Icon::S);layout[19]=uint8_t(Icon::D);
    return layout;
}
namespace {
Icon keyIcon(uint32_t code) {
    constexpr std::array letters{Icon::A,Icon::B,Icon::C,Icon::D,Icon::E,Icon::F,Icon::G,Icon::H,Icon::I,Icon::J,Icon::K,Icon::L,Icon::M,
        Icon::N,Icon::O,Icon::P,Icon::Q,Icon::R,Icon::S,Icon::T,Icon::U,Icon::V,Icon::W,Icon::X,Icon::Y,Icon::Z};
    constexpr std::array digits{Icon::Zero,Icon::One,Icon::Two,Icon::Three,Icon::Four,Icon::Five,Icon::Six,Icon::Seven,Icon::Eight,Icon::Nine};
    if(code>='A'&&code<='Z')return letters[code-'A'];
    if(code>='0'&&code<='9')return digits[code-'0'];
    if(code>=0x70&&code<=0x7B)return Icon(uint8_t(Icon::F1)+code-0x70);
    if(code>=0x60&&code<=0x69)return Icon(uint8_t(Icon::Numpad0)+code-0x60);
    switch(code) {
    case 0:return Icon::Count;case 1:return Icon::MouseLeft;case 2:return Icon::MouseRight;case 4:return Icon::MouseMiddle;
    case 5:case 6:return Icon::Question;
    case 8:return Icon::Backspace;case 9:return Icon::Tab;case 13:return Icon::Enter;case 0x1B:return Icon::Esc;case 0x20:return Icon::Space;
    case 0x10:return Icon::Shift;case 0x11:return Icon::Ctrl;case 0x12:return Icon::LeftAlt;
    case 0xA0:return Icon::Shift;case 0xA1:return Icon::RightShift;case 0xA2:return Icon::Ctrl;case 0xA3:return Icon::RightCtrl;
    case 0xA4:return Icon::LeftAlt;case 0xA5:return Icon::RightAlt;
    case 0x14:return Icon::CapsLock;case 0x21:return Icon::PageUp;case 0x22:return Icon::PageDown;case 0x23:return Icon::End;case 0x24:return Icon::Home;
    case 0x25:return Icon::Left;case 0x26:return Icon::Up;case 0x27:return Icon::Right;case 0x28:return Icon::Down;
    case 0x2D:return Icon::Insert;case 0x2E:return Icon::Delete;case 0x2C:return Icon::PrintScreen;case 0x13:return Icon::Pause;
    case 0x5D:return Icon::Menu;
    case 0x90:return Icon::NumLock;case 0x91:return Icon::ScrollLock;
    case 0x6A:return Icon::NumpadMultiply;case 0x6B:return Icon::NumpadAdd;case 0x6D:return Icon::NumpadSubtract;case 0x6E:return Icon::NumpadDecimal;case 0x6F:return Icon::NumpadDivide;
    case 0xBA:return Icon::Semicolon;case 0xBB:return Icon::Equals;case 0xBC:return Icon::Comma;case 0xBD:return Icon::Minus;case 0xBE:return Icon::Period;
    case 0xBF:return Icon::Slash;case 0xC0:return Icon::Grave;case 0xDB:return Icon::LeftBracket;case 0xDC:return Icon::Backslash;case 0xDD:return Icon::RightBracket;case 0xDE:return Icon::Apostrophe;
    default:return Icon::Question;
    }
}
}
NativePromptLayout keyboardMousePromptLayout(const NativeControlSettings& settings) {
    auto layout=defaultKeyboardMousePromptLayout();
    const auto binding=[&](ControlAction action) {
        const auto& slots=settings.bindings[uint32_t(action)];
        // Keep the familiar mouse action representative when it is still bound.
        for(const auto code:slots)if(code==1||code==2||code==4)return keyIcon(code);
        return keyIcon(slots[0]?slots[0]:slots[1]);
    };
    constexpr std::array actions{ControlAction::CharacterMenu,ControlAction::Pause,ControlAction::Action,ControlAction::Attack,
        ControlAction::Special,ControlAction::Jump,ControlAction::SwitchCharacter,ControlAction::TargetLock,ControlAction::LeftTrigger,ControlAction::SpecialPower};
    for(size_t i=0;i<actions.size();++i)layout[6+i]=uint8_t(binding(actions[i]));
    constexpr std::array moves{ControlAction::MoveForward,ControlAction::MoveLeft,ControlAction::MoveBackward,ControlAction::MoveRight};
    for(size_t i=0;i<moves.size();++i)layout[16+i]=uint8_t(binding(moves[i]));
    return layout;
}
std::vector<uint8_t> keyboardMousePromptPixels(const NativePromptLayout& layout) {
    std::vector<uint8_t> pixels(256*256*4,0);
    for(size_t cell=0;cell<cells.size();++cell) {
        if(layout[cell]>=uint8_t(Icon::Count))continue;
        const auto icon=NativePromptIcons::lookup(Icon(layout[cell]));
        if(icon.width!=64 || icon.height!=64 || icon.rgba.size()!=64*64*4)
            throw Error("Native input prompt icon extent differs from its atlas cell");
        for(size_t y=0;y<64;++y) {
            const size_t offset=((cell/4*64+y)*256+cell%4*64)*4;
            std::copy_n(icon.rgba.data()+y*64*4,64*4,pixels.data()+offset);
        }
    }
    const auto defaults=defaultKeyboardMousePromptLayout();
    if(!std::equal(layout.begin()+16,layout.end(),defaults.begin()+16)) {
        // Reuse complete physical-key artwork for a directional W/A/S/D group.
        for(size_t y=0;y<64;++y)std::fill_n(pixels.data()+((64+y)*256)*4,64*4,0);
        constexpr std::array<size_t,4> xs{21,0,21,42},ys{0,32,32,32};
        for(size_t key=0;key<4;++key) {
            const auto icon=NativePromptIcons::lookup(Icon(layout[16+key]));if(icon.rgba.empty())continue;
            for(size_t y=0;y<32;++y)for(size_t x=0;x<21;++x) {
                const auto src=(y*2*64+x*64/21)*4;
                const auto dst=((64+ys[key]+y)*256+xs[key]+x)*4;
                std::copy_n(icon.rgba.data()+src,4,pixels.data()+dst);
            }
        }
    }
    return pixels;
}
std::size_t enlargeInputPromptGlyphs(Im2DDraw& draw) {
    return enlargeInputPromptGlyphs(draw,defaultKeyboardMousePromptLayout());
}
std::size_t enlargeInputPromptGlyphs(Im2DDraw& draw,const NativePromptLayout& layout) {
    if(!draw.rasterWidth||!draw.rasterHeight)return 0;
    std::size_t changed=0;
    if(draw.primitiveType==3&&draw.vertices.size()%6==0) {
        for(std::size_t offset=0;offset<draw.vertices.size();offset+=6)
            changed+=enlargeQuad(std::span(draw.vertices).subspan(offset,6),float(draw.rasterWidth),float(draw.rasterHeight),false,layout);
    } else if(draw.primitiveType==4&&draw.vertices.size()==4) {
        changed=enlargeQuad(draw.vertices,float(draw.rasterWidth),float(draw.rasterHeight),true,layout);
    }
    return changed;
}
std::shared_ptr<Texture> NativeInputPrompts::texture(NativeBackend& backend) {
    return texture(backend,defaultKeyboardMousePromptLayout());
}
std::shared_ptr<Texture> NativeInputPrompts::texture(NativeBackend& backend,const NativePromptLayout& selected) {
    backend.validateSubmissionContext();
    if(!atlas||layout!=selected) {
        const auto pixels=keyboardMousePromptPixels(selected);
        atlas=backend.createTexture(256,256,TextureFormat::RGBA8,pixels);
        layout=selected;
        std::fprintf(stderr,"[NATIVE INPUT PROMPTS] Yellow keyboard/mouse atlas uploaded; readable native glyph sizing enabled\n");
    }
    backend.validateTexture(atlas);
    return atlas;
}
}
