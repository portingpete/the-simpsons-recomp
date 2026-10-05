#pragma once
#include "native_backend.h"
#include "im2d_vertices.h"

namespace Simpsons::Graphics {

// Complete effective state AFTER the original pending-state commit. No guest
// pointers, SDK object addresses, borrowed vertex ranges or cache sentinels.
// This packet selects only the recovered unlit, unfogged flat / stage0*diffuse
// expressions. A null texture selects flat. Other shader branches need another
// qualification; they must not be represented by this packet.
struct Im2DDraw {
    std::vector<Im2DVertex> vertices;
    std::shared_ptr<Texture> texture;
    uint32_t primitiveType{}; // Original 3: triangle list, 3..9360 vertices in complete triplets.
                             // Original 4: triangle strip, 3..9362 vertices.
    uint32_t rasterWidth{}, rasterHeight{}; // Original VS constants, not target extent.
    uint32_t blendWord{}; // Effective device word, NOT Screen_Xenon selector:
                          // 07060706, 00010001, 00010706, 00010106, 00010186,
                          // 01000100 (ZERO/ADD/ONE for both RGB and alpha).
    // Original target0 request0/1. Both retain RGB10A2 codes; this backend uses
    // explicit float equations/per-write integer packing for either request.
    // Expanded mode requires normalized vertex colors. Console intermediate
    // precision is unproven, as in the existing native original-screen policy.
    uint32_t expandedBlend{};
    bool alphaTest{};
    float alphaReference{}; // Effective normalized reference; used before blending.
    uint32_t alphaCompare{}; // Original 0..7: NEVER, LESS, EQUAL, LEQUAL,
                             // GREATER, NOTEQUAL, GEQUAL, ALWAYS.
    uint32_t cullBits{}; // Original low three bits, supported 0/2/6 only.
    bool pixelCenterHalf{}; // Original device+29C0 bit0: 0 integer, 1 half-integer.
    uint8_t colorWriteMask{};
    bool depthTest{}, depthWrite{}, stencil{}; // Stencil remains unsupported.
    uint32_t depthCompare{}; // Retained original 0..7, same ordering as alphaCompare.
                            // Write/compare remain inactive when depthTest is false.
    bool reverseDepth{}; // Qualified original viewport: PS applies 1-Z before 20e4 conversion.
    D3D11_SAMPLER_DESC sampler{};
    bool preserveAspect=false; // Frozen at the original Apt/UI draw boundary.
};

// Native raster coverage/float filtering and round-to-even target packing are
// explicit policies, not proof of console subpixel, blend or filtering precision.
// The original VS's subtraction of 0.5 is retained. Center0 temporarily shifts
// the selected viewport origin by (+0.5,+0.5), restored before return. Center1
// leaves it unchanged. Before game integration the caller must establish solid
// fill, no scissor/bias/user clip planes, positive-X/negative-Y viewport scales,
// viewport enable, and the retained guard/round/quantization policy. These
// states are not inferred from original cache sentinels or native old bindings.
// In particular device+29C0 words4/5 request 1/16-pixel quantization; D3D11
// snaps at 1/256. Matching post-transform snapped vertices/coverage is a further
// qualification even for nominal integer CPU inputs. Fractional geometry,
// clipping and original interpolation precision remain unproven. Depth-enabled
// packets require the actual owned D32_FLOAT_S8X24 target and native viewport
// depth range 0..1. The VS preserves supplied Z; reverseDepth selects the
// caller-qualified original mapping in the PS before 20e4 round-to-nearest-even
// conversion. D32 stores that exactly representable decoded value. Original
// hardware rounding for general values and interpolation remain unproven;
// see native-im2d-depth-backend.md for the reached constant's qualification.
}
