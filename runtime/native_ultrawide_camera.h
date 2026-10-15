#pragma once
#include <array>
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
// Native TAA camera record (Antialiasing::TAA). The jitter hook stores the
// main scene camera's unjittered view and projection for the frame being
// built; the resolve consumes them and keeps the previous frame's product.
// Row-vector convention as published by the original camera begin:
// clip = world * view * projection.
struct NativeTemporalCameraState {
    uint64_t frame=~0ull,resolvedFrame=~0ull,previousFrame=~0ull;
    uint32_t camera{},previousCamera{},sample{};
    std::array<double,16> view{},projection{},previousViewProjection{};
    std::array<double,2> jitterNdc{};
    bool currentValid=false,previousValid=false;
    uint64_t jitteredBegins{},resets{};
};
// Free camera and photo mode. The main world camera's frame is placed here
// before the original frustum rebuild, so culling and rendering both follow it.
// Photo mode also pauses gameplay updates and hides the HUD.
struct NativeFreeCameraState {
    bool active=false,photo=false,placed=false;
    uint32_t camera{};
    std::array<double,3> position{};
    double yaw{},pitch{},zoom=1,handedness=1;
    int64_t lastTicks{};
    uint16_t previousPadButtons{};
    bool thumbsReleased=true;
};
// Applies only to the original full-size perspective scene camera. The caller
// supplies the frozen renderer aspect, never a pending menu preference. Uses
// the original view-window setter. Publish before both the original frustum
// rebuild and camera begin so visibility collection and rendering agree.
// fieldOfView is a 16:9 horizontal reference relative to the stock 60-degree view,
// not an absolute replacement for every authored camera: original zoom still
// scales the perspective tangents. Zero retains the authored field of view.
bool applyNativeUltrawideCamera(const PPCContext&,uint8_t* base,double renderAspect,uint32_t fieldOfView=0);
// Once per frame for the main world camera, before its frustum rebuild.
void updateNativeFreeCamera(const PPCContext&,uint8_t* base,uint32_t camera);
}
void SimpsonsNativeUltrawideCameraBegin(PPCContext&,uint8_t* base);
void SimpsonsNativeUltrawideCameraSync(PPCContext&,uint8_t* base);
void SimpsonsNativeCameraViewWindowSource(PPCContext&,uint8_t* base);
// Original camera begin, before its projection is cached (823F0210).
void SimpsonsNativeTemporalProjection(PPCContext&,uint8_t* base);
// Original gameplay update's pause query result (82690ED0): photo mode pauses.
void SimpsonsNativePhotoPause(PPCContext&,uint8_t* base);
// Apt overlay render call (827F7488): skipped in photo mode.
bool SimpsonsNativePhotoHud(PPCContext&,uint8_t* base);
// Gameplay 2D HUD pass bracket (826D5CDC / 826D5CE0).
void SimpsonsNativeHudBegin(PPCContext&,uint8_t* base);
void SimpsonsNativeHudEnd(PPCContext&,uint8_t* base);
// Original ShakeCameraModifier offsets, before they are added (8295A3DC).
void SimpsonsNativeCameraShake(PPCContext&,uint8_t* base);
