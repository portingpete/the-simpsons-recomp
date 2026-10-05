#pragma once
#include "declaration_resources.h"
#include <array>

namespace Simpsons::Graphics {

struct Im2DVertexError : std::runtime_error {using std::runtime_error::runtime_error;};

// Owned native input for the verified unlit screen-position shader branch.
// Source declaration remains the original 28-byte BE layout; this is 40 bytes.
struct Im2DVertex {
    std::array<float,4> position, color;
    std::array<float,2> uv;
};
static_assert(sizeof(Im2DVertex)==40);

// Exact complete vertex range, not the entire dynamic buffer. Accepts the
// verified Im2D declaration only, with 3..9362 vertices (triangle strip path).
// Preserves unused RHW/UV bits; rejects nonfinite active XYZ or textured UV.
// Returns owned data and retains neither source nor declaration references.
// No binding, device operation, render-state change or game draw is performed.
std::vector<Im2DVertex> decodeIm2DVertices(const DeclarationRecord&,
    std::span<const uint8_t> source, bool textured);
// Same conversion into caller-owned reusable capacity. Source/output overlap
// is rejected before resizing; after other failures output contents are unspecified.
void decodeIm2DVertices(const DeclarationRecord&, std::span<const uint8_t> source,
    bool textured, std::vector<Im2DVertex>& output);

}
