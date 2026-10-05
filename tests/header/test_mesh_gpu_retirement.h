#pragma once
#include <memory>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace Simpsons::Graphics {
// TEST ONLY: retain one diagnostic COM reference to each actual input buffer.
// The normal draw/list/cache/backend scopes still perform their own cleanup.
// Once those owners have ended, the diagnostic's final Release must destroy
// the object; a nonzero result exposes retained native GPU ownership.
struct MeshGpuRetirementObservation {
    std::string family;
    std::weak_ptr<void> mesh;
    ComPtr<ID3D11Buffer> vertices,indices;
};
inline std::vector<MeshGpuRetirementObservation> meshGpuRetirementObservations;

template<class Mesh>
void observeMeshGpuRetirement(ID3D11DeviceContext* context,const std::shared_ptr<Mesh>& mesh,const char* family) {
    if(!context||!mesh)throw Error("GPU mesh retirement probe lacks its original native input owner");
    MeshGpuRetirementObservation observation;
    observation.family=family;observation.mesh=mesh;
    UINT stride{},offset{},indexOffset{};DXGI_FORMAT format{};
    context->IAGetVertexBuffers(0,1,&observation.vertices,&stride,&offset);
    context->IAGetIndexBuffer(&observation.indices,&format,&indexOffset);
    if(!observation.vertices||!observation.indices||!stride||offset||
       format!=DXGI_FORMAT_R16_UINT||indexOffset)
        throw Error("GPU mesh retirement probe lacks actual owned R16 input buffers");
    meshGpuRetirementObservations.push_back(std::move(observation));
}

template<class Require>
void requireMeshGpuRetirements(Require require) {
    require(!meshGpuRetirementObservations.empty(),"GPU mesh retirement did not observe an actual draw input");
    for(auto& observation:meshGpuRetirementObservations) {
        require(observation.mesh.expired(),"Backend destruction retained an immutable mesh owner");
        // Never touch an object after releasing this last observation reference.
        const auto vertexReferences=observation.vertices.Detach()->Release();
        const auto indexReferences=observation.indices.Detach()->Release();
        if(vertexReferences||indexReferences)
            std::fprintf(stderr,"GPU mesh retirement family=%s vertex_refs=%lu index_refs=%lu\n",
                observation.family.c_str(),vertexReferences,indexReferences);
        require(!vertexReferences&&!indexReferences,"Backend destruction retained an actual GPU vertex/index buffer");
        std::printf("AUDIT_GPU_MESH_RETIREMENT family=%s create=passed draw_use=passed backend_destructor=passed mesh_owner_expired=passed vertex_buffer_final_release=passed index_buffer_final_release=passed scope=backend_owned_immutable_buffers original_asset_retirement=separate\n",
            observation.family.c_str());
    }
    meshGpuRetirementObservations.clear();
}
}
