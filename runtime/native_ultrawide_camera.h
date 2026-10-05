#pragma once
#include <cstdint>

struct PPCContext;
namespace Simpsons {
// Runtime-owned provenance for the one main scene projection. Keep the authored
// window separate from our last published window so repeated begins and menu
// setting changes cannot scale a previously scaled result.
struct NativeCameraProjectionState {
    uint32_t camera{},frame{},color{},depth{};
    uint64_t cameraGeneration{},frameGeneration{};
    uint32_t originalWidth{},originalHeight{},appliedWidth{},appliedHeight{};
    uint32_t appliedFieldOfView{};
    double appliedAspect{};
    bool applied{},nativePublication{};
};
// Applies only to the original full-size perspective scene camera. The caller
// supplies the frozen renderer aspect, never a pending menu preference. Uses
// the original view-window setter. Publish before both the original frustum
// rebuild and camera begin so visibility collection and rendering agree.
// fieldOfView is a 16:9 horizontal reference relative to the stock 60-degree view,
// not an absolute replacement for every authored camera: original zoom still
// scales the perspective tangents. Zero retains the authored field of view.
bool applyNativeUltrawideCamera(const PPCContext&,uint8_t* base,double renderAspect,uint32_t fieldOfView=0);
}
void SimpsonsNativeUltrawideCameraBegin(PPCContext&,uint8_t* base);
void SimpsonsNativeUltrawideCameraSync(PPCContext&,uint8_t* base);
void SimpsonsNativeCameraViewWindowSource(PPCContext&,uint8_t* base);
