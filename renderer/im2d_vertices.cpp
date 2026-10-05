#include "im2d_vertices.h"
#include <bit>

namespace Simpsons::Graphics {
namespace {
uint32_t word(const uint8_t* p) {
    return uint32_t(p[0])<<24 | uint32_t(p[1])<<16 | uint32_t(p[2])<<8 | p[3];
}
bool finite(uint32_t bits) {return (bits&0x7F800000u)!=0x7F800000u;}
constexpr auto normalizedColor=[] {
    std::array<float,256> values{};
    for(size_t i=0;i<values.size();++i)values[i]=float(i)/255.0f;
    return values;
}(); // Compile-time nearest conversion; never inherit a guest rounding mode.
void validate(const DeclarationRecord& record) {
    constexpr std::array<uint16_t,3> offsets={0,16,20};
    constexpr std::array<uint32_t,3> types={0x001A23A6,0x00182886,0x002C23A5};
    constexpr std::array<uint8_t,3> usages={0,10,5};
    constexpr std::array<uint32_t,3> widths={16,4,8};
    const auto elements=record.elements();
    if(elements.size()!=3 || record.minimumStreamBytes()!=28)
        throw Im2DVertexError("Im2D input requires the original 28-byte declaration");
    for(size_t i=0;i<3;++i) {
        const auto& e=elements[i];
        if(e.stream || e.method || e.usageIndex || e.offset!=offsets[i] ||
           e.type!=types[i] || e.usage!=usages[i] || e.storageBytes!=widths[i])
            throw Im2DVertexError("Im2D declaration differs from the verified original layout");
        // Original stack builders do not initialize opaque byte +11. It is
        // retained by DeclarationRecord and does not select vertex semantics.
    }
}
}
std::vector<Im2DVertex> decodeIm2DVertices(const DeclarationRecord& record,
    std::span<const uint8_t> source, bool textured) {
    std::vector<Im2DVertex> output;
    decodeIm2DVertices(record,source,textured,output);
    return output;
}
void decodeIm2DVertices(const DeclarationRecord& record,
    std::span<const uint8_t> source, bool textured, std::vector<Im2DVertex>& output) {
    validate(record);
    if(source.size()<3*28 || source.size()>9362*28 || source.size()%28)
        throw Im2DVertexError("Im2D input requires 3..9362 complete 28-byte vertices");
    const auto sourceAt=reinterpret_cast<uintptr_t>(source.data());
    const auto outputAt=reinterpret_cast<uintptr_t>(output.data());
    if(output.capacity() && sourceAt<outputAt+output.capacity()*sizeof(Im2DVertex) &&
       outputAt<sourceAt+source.size())
        throw Im2DVertexError("Im2D source overlaps reusable output storage");
    output.resize(source.size()/28);
    for(size_t i=0;i<output.size();++i) {
        const auto* p=source.data()+i*28;
        auto& v=output[i];
        for(size_t c=0;c<4;++c) {
            const auto bits=word(p+c*4);
            if(c<3 && !finite(bits))throw Im2DVertexError("Nonfinite Im2D position is not qualified");
            v.position[c]=std::bit_cast<float>(bits);
        }
        // Original 826D5690..826D5708 builds 0xAARRGGBB, stored BE as A,R,G,B.
        // D3DCOLOR fetch expands unsigned channels to normalized RGBA.
        v.color={normalizedColor[p[17]],normalizedColor[p[18]],normalizedColor[p[19]],normalizedColor[p[16]]};
        for(size_t c=0;c<2;++c) {
            const auto bits=word(p+20+c*4);
            if(textured && !finite(bits))throw Im2DVertexError("Nonfinite textured Im2D UV is not qualified");
            v.uv[c]=std::bit_cast<float>(bits);
        }
    }
}
}
