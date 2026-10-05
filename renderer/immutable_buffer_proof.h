#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

namespace Simpsons::Graphics {
// Exact proof that an immutable mesh's vertex/index buffer pair passed its full owner check
// (GetDevice plus the GetDesc profile) for one device and one element extent. A D3D11 object's
// owning device and creation descriptor never change, and the strong references here pin the
// verified objects, so their addresses cannot be reused while the proof holds; every device
// child also keeps its device alive, so the device identity cannot be reused either. While the
// same buffers, the same expected device and the same extents are presented, a later
// validation therefore needs no D3D round trip. Anything else forces the full check again.
struct ImmutableBufferPairProof {
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertices,indices;
    const ID3D11Device* device{};
    uint64_t vertexCount{},indexCount{};
    bool holds(ID3D11Buffer* v,ID3D11Buffer* i,const ID3D11Device* expected,uint64_t vertexElements,uint64_t indexElements) const noexcept {
        return v && i && expected && device==expected && vertices.Get()==v && indices.Get()==i &&
            vertexCount==vertexElements && indexCount==indexElements;
    }
    // Publish only after the full check succeeded.
    void prove(ID3D11Buffer* v,ID3D11Buffer* i,const ID3D11Device* expected,uint64_t vertexElements,uint64_t indexElements) {
        vertices=v;indices=i;device=expected;vertexCount=vertexElements;indexCount=indexElements;
    }
};
}
