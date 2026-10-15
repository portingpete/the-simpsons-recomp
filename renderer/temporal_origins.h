#pragma once
#include <array>
#include <cstdint>
#include <memory>

namespace Simpsons::Graphics {
class RenderTarget;
class DepthTarget;
// Uniform means a successful full-component clear: it is invariant under XY
// transport. Grid records a qualified partial write on an explicit raster grid.
// Unknown never becomes known merely because a partial draw was submitted.
enum class TemporalRasterOriginKind {Unknown, Uniform, Grid};
struct TemporalRasterOrigin {
    TemporalRasterOriginKind kind=TemporalRasterOriginKind::Unknown;
    std::array<float,2> jitter{};
    uint64_t revision{};
    bool operator==(const TemporalRasterOrigin&) const=default;
    bool compatible(std::array<float,2> grid) const noexcept {
        return kind==TemporalRasterOriginKind::Uniform ||
            (kind==TemporalRasterOriginKind::Grid && jitter==grid);
    }
};
struct NativeTemporalSceneOrigins {
    // Strong actual owners prevent guest-camera/COM-address lifetime reuse.
    std::shared_ptr<RenderTarget> color;
    std::shared_ptr<DepthTarget> depth;
    // Component bits are 1=packed color, 2=depth, 4=stencil.
    std::array<TemporalRasterOrigin,3> components{};
};
}
