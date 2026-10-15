#pragma once
#include "renderer/mesh_upload_cache.h"
#include <array>
#include <limits>

namespace Simpsons::Graphics {
struct MeshUploadCacheProbe {
    template<class Mesh>static const uint8_t* vertices(const ExactContentMeshCache<Mesh>& cache) {
        return cache.lru_.front().vertices.data();
    }
    template<class Mesh>static std::weak_ptr<const void> owner(const ExactContentMeshCache<Mesh>& cache) {
        return cache.lru_.front().vertices.owner;
    }
};
}

template<class Check>void runOwnedMeshCacheChecks(Check check) {
    using namespace Simpsons::Graphics;
    struct Mesh {unsigned id{};};
    const auto bytes=[](const std::vector<uint32_t>& v){return std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(v.data()),v.size()*sizeof(uint32_t));};
    const std::array<uint8_t,2> indices{3,7};
    const std::vector<uint32_t> expected{0x12345678,0xABCDEF01};
    auto mesh=std::make_shared<Mesh>(Mesh{1});
    ExactContentMeshCache<Mesh> cache(1,64);
    std::weak_ptr<const void> weakOwner;
    const auto key=ExactContentMeshCache<Mesh>::contentKey(bytes(expected),indices);
    {
        auto vertices=expected;
        const auto* allocation=reinterpret_cast<const uint8_t*>(vertices.data());
        const auto charged=vertices.capacity()*sizeof(uint32_t)+indices.size();
        check(cache.insertOwnedVertices(vertices,indices,key,mesh),"Owned mesh cache did not consume cacheable input");
        check(vertices.empty()&&MeshUploadCacheProbe::vertices(cache)==allocation,"Owned cache allocated/copied vertex storage");
        check(cache.residentBytes()==charged,"Owned cache did not charge retained capacity");
        weakOwner=MeshUploadCacheProbe::owner(cache);
        vertices.assign(16,0); // The moved-from vector now owns separate storage.
    }
    check(!weakOwner.expired()&&cache.find(bytes(expected),indices,key)==mesh,
          "Typed vertex ownership did not survive the caller vector");
    auto changed=expected;changed[1]^=1;
    check(!cache.find(bytes(changed),indices,key),"Owned cache accepted forced vertex hash collision");
    const std::array<uint8_t,2> changedIndices{3,8};
    check(!cache.find(bytes(expected),changedIndices,key),"Owned cache accepted forced index hash collision");
    check(!cache.find(bytes(expected).first(sizeof(uint32_t)),indices,key),"Owned cache accepted forced extent collision");
    check(cache.lookup(bytes(expected),indices).mesh==mesh,"Owned LRU lookup did not compare complete bytes");
    auto second=changed;
    const auto secondKey=ExactContentMeshCache<Mesh>::contentKey(bytes(second),indices);
    check(cache.insertOwnedVertices(second,indices,secondKey,std::make_shared<Mesh>(Mesh{2})),"Second owned cache insertion failed");
    check(weakOwner.expired()&&mesh->id==1&&cache.size()==1,"Owned eviction retained CPU allocation or invalidated live mesh");
    cache.clear();check(cache.residentBytes()==0&&cache.size()==0,"Owned cache clear kept residency");

    auto legacy=expected;
    cache.insert(bytes(legacy),indices,key,mesh);
    check(MeshUploadCacheProbe::vertices(cache)!=reinterpret_cast<const uint8_t*>(legacy.data())&&
          MeshUploadCacheProbe::owner(cache).expired(),"Legacy mesh cache retained caller storage");
    legacy[0]^=1;
    check(cache.find(bytes(expected),indices,key)==mesh,"Legacy snapshot changed after caller mutation");

    // An index span may alias a retained owned allocation that this insertion
    // evicts. Its snapshot must precede eviction and the original owner release.
    auto previousOwned=expected;
    check(cache.insertOwnedVertices(previousOwned,indices,key,mesh),"Owned alias source insertion failed");
    const auto previousOwner=MeshUploadCacheProbe::owner(cache);
    const auto* previous=MeshUploadCacheProbe::vertices(cache);
    const std::span<const uint8_t> aliasedIndices(previous,2);
    const std::array<uint8_t,2> expectedIndices{previous[0],previous[1]};
    auto aliasedVertices=changed;
    const auto aliasKey=ExactContentMeshCache<Mesh>::contentKey(bytes(aliasedVertices),aliasedIndices);
    check(cache.insertOwnedVertices(aliasedVertices,aliasedIndices,aliasKey,mesh),"Aliased-index insertion failed");
    check(previousOwner.expired()&&cache.find(bytes(changed),expectedIndices,aliasKey)==mesh,
          "Index alias was copied after its owner eviction");

    // Logical size fits, retained reserve does not: deny without consuming the
    // vector or publishing any residency.
    ExactContentMeshCache<Mesh> bounded(2,16);
    auto reserved=expected;reserved.reserve(128);
    const auto* reservedPointer=reserved.data();
    check(!bounded.insertOwnedVertices(reserved,indices,key,mesh)&&reserved.data()==reservedPointer&&reserved==expected,
          "Owned cache ignored capacity bound or consumed rejected input");
    check(bounded.residentBytes()==0&&bounded.size()==0,"Rejected owned capacity published residency");
    ExactContentMeshCache<Mesh> disabled(0,64);
    auto skipped=expected;
    check(!disabled.insertOwnedVertices(skipped,indices,key,mesh)&&skipped==expected,"Disabled cache consumed owned vertices");
    check(!cache.insertOwnedVertices(skipped,indices,key,{})&&skipped==expected,"Null mesh consumed owned vertices");

    // Index bytes can also borrow the very vertex allocation being transferred.
    auto selfAlias=expected;
    const auto vertexBytes=bytes(selfAlias);
    const auto selfKey=ExactContentMeshCache<Mesh>::contentKey(vertexBytes,vertexBytes.first(2));
    check(cache.insertOwnedVertices(selfAlias,vertexBytes.first(2),selfKey,mesh)&&selfAlias.empty(),"Self-alias transfer failed");
    const auto selfOwner=MeshUploadCacheProbe::owner(cache);
    check(cache.find(bytes(expected),bytes(expected).first(2),selfKey)==mesh,"Transferred index alias lost exact bytes");
    cache.clear();check(selfOwner.expired()&&mesh->id==1,"Clear failed to release typed owner independently of live mesh");
}
