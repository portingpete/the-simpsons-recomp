#pragma once
#include "shadow_mesh.h"
#include <cstddef>

namespace Simpsons::Graphics {
// Original mono FETCH order, decoded by the CPU owner; static or 64-bone skin.
struct MonoVertex {
    std::array<float,3> position{};
    std::array<float,4> weights{},indices{};
};
static_assert(sizeof(MonoVertex)==44 && offsetof(MonoVertex,weights)==12 &&
              offsetof(MonoVertex,indices)==28);

struct MonoMeshDraw : ShadowMeshDraw {
    uint32_t blendEnable=0,blendWord=0x00010001,expandedBlend=0;
};

class NativeMonoMesh {
public:
    ~NativeMonoMesh();
    NativeMonoMesh(const NativeMonoMesh&)=delete;
    NativeMonoMesh& operator=(const NativeMonoMesh&)=delete;
    uint32_t vertexCount() const noexcept;
    uint32_t indexCount() const noexcept;
private:
    friend class NativeBackend;
    struct State;
    explicit NativeMonoMesh(std::unique_ptr<State>);
    std::unique_ptr<State> state;
};
class NativeMonoReplayConstants {
public:
    ~NativeMonoReplayConstants();
    NativeMonoReplayConstants(const NativeMonoReplayConstants&)=delete;
    NativeMonoReplayConstants& operator=(const NativeMonoReplayConstants&)=delete;
private:
    friend class NativeBackend;
    struct State;
    explicit NativeMonoReplayConstants(std::unique_ptr<State>);
    std::unique_ptr<State> state;
};
}
