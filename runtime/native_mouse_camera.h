#pragma once
#include <cstdint>

struct PPCContext;
namespace Simpsons {
// Raw mouse displacement becomes an angular displacement at the original
// camera's final input consumer, after controller curves and time scaling.
inline constexpr double nativeMouseRadiansPerCount=0.0025;
}
void SimpsonsNativeMouseOrbit(PPCContext&,uint8_t* base);
void SimpsonsNativeMouseLook(PPCContext&,uint8_t* base);
void SimpsonsNativeMouseCameraDispatch(PPCContext&,uint8_t* base);
void SimpsonsNativeMouseCameraEligibility(PPCContext&,uint8_t* base);
