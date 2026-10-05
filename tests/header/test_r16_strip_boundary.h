#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Independent packet fixture. The preceding reset makes the single-strip
// and original two-packet windings differ after the SDK65534 boundary.
// selected start1 also leaves invalid raw values unselected on both sides.
namespace Simpsons::Graphics::Test {
constexpr uint32_t stripBoundaryCount=65536;
constexpr uint32_t stripBoundaryStart=1;
inline std::vector<uint16_t> stripBoundaryIndices() {
    std::vector<uint16_t> indices(stripBoundaryCount+2,0xFFFF);
    indices.front()=indices.back()=0xFFFE;
    for(uint16_t i=0;i<5;++i)indices[stripBoundaryStart+65531+i]=i;
    return indices;
}
template<class Quad>
auto stripBoundaryVertices(const Quad& quad) {
    using Vertex=typename Quad::value_type;
    std::vector<Vertex> vertices(5,quad.front());
    constexpr std::array<std::array<float,2>,5> xy={{{-1,1},{-1,-1},{0,1},{0,-1},{1,1}}};
    for(std::size_t i=0;i<vertices.size();++i) {
        vertices[i].position[0]=xy[i][0];vertices[i].position[1]=xy[i][1];
    }
    return vertices;
}
// Independent pixel-center geometry for ABC, BCD and DCE at the16x16 GPU
// and1280x720 original fixture sizes. No tested
// production helper or shader arithmetic is used in this coverage oracle.
constexpr bool originalStripBoundaryCovered(uint32_t x,uint32_t y,uint32_t cull,uint32_t width=16,uint32_t height=16) {
    const auto diagonal=(2ull*y+1)*width+2*(2ull*x+1)*height;
    const auto edge=2ull*width*height;
    const bool first=x<width/2&&diagonal<=edge;
    const bool second=x<width/2&&diagonal>edge;
    const bool third=x>=width/2&&diagonal<=2*edge;
    return cull==2?first:cull==6?(second||third):(first||second||third);
}
constexpr std::array<std::array<uint32_t,2>,2> originalStripBoundaryPackets={{{65534,1},{4,65533}}};
}
