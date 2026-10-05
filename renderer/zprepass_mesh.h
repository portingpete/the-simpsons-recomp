#pragma once
#include "shadow_mesh.h"
#include <cstddef>

namespace Simpsons::Graphics {
// Original FETCH order, already decoded by the CPU owner. All nine attributes
// exist even in the bounded static profile, whose eight unused inputs are zero.
struct ZPrepassVertex {
    std::array<float,3> position{};
    std::array<float,4> weights{},indices{};
    std::array<std::array<float,3>,6> morph{};
};
static_assert(sizeof(ZPrepassVertex)==116 && offsetof(ZPrepassVertex,weights)==12 &&
              offsetof(ZPrepassVertex,indices)==28 && offsetof(ZPrepassVertex,morph)==44);

class NativeZPrepassMesh {
public:
    ~NativeZPrepassMesh();
    NativeZPrepassMesh(const NativeZPrepassMesh&)=delete;
    NativeZPrepassMesh& operator=(const NativeZPrepassMesh&)=delete;
    uint32_t vertexCount() const noexcept;
    uint32_t indexCount() const noexcept;
private:
    friend class NativeBackend;
    struct State;
    explicit NativeZPrepassMesh(std::unique_ptr<State>);
    std::unique_ptr<State> state;
};
}
