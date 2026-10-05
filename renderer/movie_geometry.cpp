#include "movie_geometry.h"
#include <bit>

namespace Simpsons::Graphics {
namespace {
// Literal lfs sources in the immutable image; no decimal reconversion or
// inherited guest rounding mode is involved.
constexpr float negativeOne=std::bit_cast<float>(uint32_t{0xBF800000}); // 821DD110
constexpr float zero=std::bit_cast<float>(uint32_t{0x00000000});        // 821DD0D8
constexpr float one=std::bit_cast<float>(uint32_t{0x3F800000});         // 82000BB0
constexpr float insetV0=std::bit_cast<float>(uint32_t{0x3DCCCCCD});     // 82001894
constexpr float insetV1=std::bit_cast<float>(uint32_t{0x3F666666});     // 820036E8
constexpr float insetU0=std::bit_cast<float>(uint32_t{0x3E000000});     // 8206A014
constexpr float insetU1=std::bit_cast<float>(uint32_t{0x3F600000});     // 8215D498

bool sameBits(float a,float b) {
    return std::bit_cast<uint32_t>(a)==std::bit_cast<uint32_t>(b);
}
}

std::array<MovieVertex,4> expandMovieRectangle(std::span<const MovieVertex> original) {
    if(original.size()!=3)
        throw MovieGeometryError("Movie rectangle requires exactly three original vertices");
    const auto& a=original[0];const auto& b=original[1];const auto& c=original[2];
    if(!sameBits(a.x,negativeOne) || !sameBits(a.y,negativeOne) ||
       !sameBits(b.x,one) || !sameBits(b.y,negativeOne) ||
       !sameBits(c.x,negativeOne) || !sameBits(c.y,one))
        throw MovieGeometryError("Movie rectangle position differs from the original full rectangle");
    if(!sameBits(a.u,c.u) || !sameBits(a.v,b.v))
        throw MovieGeometryError("Movie rectangle UV edges differ from the original assignments");
    const bool fullU=sameBits(a.u,zero) && sameBits(b.u,one);
    const bool fullV=sameBits(a.v,zero) && sameBits(c.v,one);
    const bool croppedU=sameBits(a.u,insetU0) && sameBits(b.u,insetU1);
    const bool croppedV=sameBits(a.v,insetV0) && sameBits(c.v,insetV1);
    if(!((fullU && fullV) || (fullU && croppedV) || (croppedU && fullV)))
        throw MovieGeometryError("Movie rectangle UV profile is not produced by the original draw");

    // Reference primitive8 expansion evaluates (b-a)+c after selecting 0123.
    // Here b.v-a.v is exactly zero, and U uses only exact binary fractions;
    // copying endpoints is bit-equivalent for every accepted input. It also
    // avoids reconstructing A as 1-B, which would change its literal bits.
    return {a,b,c,MovieVertex{b.x,c.y,b.u,c.v}};
}

MovieGeometry buildMovieGeometry(int32_t lumaWidth,uint8_t presenterByte41) {
    float u0=zero,u1=one,v0=zero,v1=one;
    if(lumaWidth<=640) { // Original cmpwi cr6,r11,640 at 8282E550.
        if(presenterByte41!=0) {v0=insetV0;v1=insetV1;}
    }else if(presenterByte41==0) {
        u0=insetU0;u1=insetU1;
    }
    MovieGeometry result{};
    result.originalVertices={MovieVertex{negativeOne,negativeOne,u0,v0},
        MovieVertex{one,negativeOne,u1,v0},MovieVertex{negativeOne,one,u0,v1}};
    result.nativeVertices=expandMovieRectangle(result.originalVertices);
    return result;
}
}
