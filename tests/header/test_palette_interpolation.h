#pragma once
#include <array>
#include <cmath>
#include <cstdint>

// Palette-coordinate fixture shared by RigidMeshTests and SkinMeshTests
// paletteInterpolation. Palette PSs pack 32*floor(32*frac(2v))+floor(64*frac(u)),
// either alone into green (plain rigid, plain skin: uv0) or with the object id
// c49.z into red (every uv1 family). A value all three vertices share arrives
// exactly on Xenos; host perspective interpolation can land an ulp below it.
namespace Simpsons::Graphics::Test {
constexpr uint32_t paletteWidth=256,paletteHeight=144,paletteId=714;
inline uint32_t paletteCode(double u,double v) {
    return 32*uint32_t(std::floor(32*(2*v-std::floor(2*v))))+uint32_t(std::floor(64*(u-std::floor(u))));
}
// Grid values seen in live meshes and at palette corners, plus one cell
// centre that never depended on exact interpolation.
constexpr std::array<std::array<float,2>,7> paletteCases={{{.7f,.5f},{0,1},{1,1},{.5f,0},{0,.5f},{-.5f,3},{.320299983f,.789099991f}}};
// Screen corners and clip W (1..9): position z carries W, so the quad keeps
// its corners while interpolation is perspective.
constexpr std::array<std::array<float,3>,4> paletteCorners={{{-1,1,1},{-1,-1,4},{1,1,6},{1,-1,9}}};
// Red stores (id+code)/2046 and RGB10A2 keeps (id+code)/2, exact for the even
// sums of these cases; green stores code/1023.
inline uint32_t paletteChannel(bool red,uint32_t code){return red?(paletteId+code)/2:code;}
inline bool paletteChannelExact(bool red,uint32_t code){return !red||!((paletteId+code)&1);}
}
