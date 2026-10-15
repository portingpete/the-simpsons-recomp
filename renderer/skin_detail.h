#pragma once
#include "skin_mesh.h"
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <cstdint>
#include <span>
#include <vector>

namespace Simpsons::Graphics {
// Video menu "Character detail": curved PN-triangle smoothing of character
// (skin) draws, built once per uploaded mesh and draw range on the CPU.
// Corners keep the original vertices. New edge and interior vertices lie on
// a cubic patch built from position-welded normals, so neighbouring
// triangles, UV seams and submesh boundaries share identical edge curves (no
// cracks). Shading normals, UVs, colours and morph deltas interpolate
// linearly; bone influences blend and keep the four strongest.
struct SkinDetailGeometry {
    std::vector<SkinVertex> vertices;
    std::vector<uint32_t> indices; // Triangle list with the original winding.
    uint32_t sourceTriangles{};
};
// One normal per vertex: the normalized sum of the normals of every vertex at
// the same position (the vertex's own normal where they cancel).
std::vector<std::array<float,3>> skinSmoothingNormals(std::span<const SkinVertex>);
// Smooths the triangles that one original R16 strip draw (restart 0xFFFF)
// covers; the range must already be validated against the vertices.
// segments: subdivisions per edge, 2..4.
SkinDetailGeometry buildSkinDetail(std::span<const SkinVertex> vertices,std::span<const std::array<float,3>> smoothing,
    std::span<const uint16_t> indices,uint32_t startIndex,uint32_t indexCount,int32_t baseVertex,uint32_t segments);
// A mesh's smoothing input, shared with the worker. Smoothing normals are
// computed by the worker on its first job for the mesh.
struct SkinDetailSource {
    std::vector<SkinVertex> vertices;
    std::vector<uint16_t> indices;
    std::vector<std::array<float,3>> smoothing;
};
struct SkinDetailJob {
    std::shared_ptr<SkinDetailSource> source;
    uint32_t startIndex{},indexCount{},segments{};
    int32_t baseVertex{};
    SkinDetailGeometry geometry;
    std::string error;
    std::atomic<bool> done{false}; // Release-published after geometry/error.
};
// One below-normal-priority thread builds queued jobs in submission order, so
// the render thread never waits on smoothing; it draws the original range
// until the job is done.
class SkinDetailWorker {
public:
    SkinDetailWorker();
    ~SkinDetailWorker();
    SkinDetailWorker(const SkinDetailWorker&)=delete;
    SkinDetailWorker& operator=(const SkinDetailWorker&)=delete;
    void submit(std::shared_ptr<SkinDetailJob>);
    static void wait(const SkinDetailJob& job) {job.done.wait(false,std::memory_order_acquire);}
private:
    struct State;
    std::unique_ptr<State> state;
};
// Detail level 0 Off, 1 Low, 2 Medium, 3 High -> edge subdivisions.
constexpr uint32_t skinDetailSegments(uint32_t level) {return level>=1&&level<=3?level+1:1;}
}
