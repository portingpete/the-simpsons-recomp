#pragma once
#include <cstdint>
#include <cmath>

namespace Simpsons {
// Query 0 has no authored menu; 1 is blank/temporarily disabled space.
// The remaining values carry the original Apt controller event plus 100.
constexpr int32_t nativeMenuMouseEvent(uint32_t hit,uint8_t pressed,int32_t wheel) {
    if(!hit)return -1;
    if(pressed&2)return 7;
    if((pressed&1)&&hit>=102&&hit<=113)return int32_t(hit-100);
    if(wheel) {
        if(hit==102||hit==103)return wheel>0?3:2;
        return wheel>0?4:5;
    }
    return -1;
}
struct NativeMenuPoint {double x{},y{};bool inside=false;};
inline NativeMenuPoint nativeMenuPoint(int32_t x,int32_t y,uint32_t width,uint32_t height,
                                      double stageWidth,double stageHeight,
                                      double renderAspect=16.0/9.0) {
    if(!width||!height||x<0||y<0||uint32_t(x)>=width||uint32_t(y)>=height)return {};
    if(!std::isfinite(stageWidth)||!std::isfinite(stageHeight)||!std::isfinite(renderAspect)||
       stageWidth<=0||stageHeight<=0||renderAspect<=0)return {};
    // Presentation fits the frozen render target to the client. Apt then fits
    // its original 16:9 viewport inside that scene, including on ultrawide.
    double sceneWidth=width,sceneHeight=height;
    if(sceneWidth/sceneHeight>renderAspect)sceneWidth=sceneHeight*renderAspect;
    else sceneHeight=sceneWidth/renderAspect;
    double uiWidth=sceneWidth,uiHeight=sceneHeight;
    constexpr double uiAspect=16.0/9.0;
    if(uiWidth/uiHeight>uiAspect)uiWidth=uiHeight*uiAspect;
    else uiHeight=uiWidth/uiAspect;
    const double left=(width-uiWidth)/2,top=(height-uiHeight)/2;
    if(x<left||y<top||x>=left+uiWidth||y>=top+uiHeight)return {};
    return {(x-left)*stageWidth/uiWidth,(y-top)*stageHeight/uiHeight,true};
}
}
