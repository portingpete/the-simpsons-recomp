#pragma once
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <list>
#include <memory>
#include <span>
#include <vector>

#include "common/byte_range_equal.h"

// Vendored XXH3 (no new package/network). XXH_INLINE_ALL keeps everything
// header-inline so no extra link unit is required.
#ifndef XXH_INLINE_ALL
#define XXH_INLINE_ALL
#endif
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include "../third_party/XenonRecomp/thirdparty/xxHash/xxhash.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace Simpsons::Graphics {

// Production bounds for the exact-content immutable mesh upload caches.
// Reached gameplay issues ~133 static depth draws per frame; 512 entries and
// 64 MiB per cache retain that working set without thrash while staying
// strictly bounded. Tests exercise eviction on small configurable instances
// instead of filling the production cache.
inline constexpr size_t kMeshUploadCacheMaxEntries = 512;
inline constexpr size_t kMeshUploadCacheMaxBytes = 64u * 1024u * 1024u;

// Bounded exact-content LRU for immutable native mesh uploads.
//
// Keyed ONLY by full content bytes (never by guest address/pointer). Lookup
// first checks the LRU back entry by full byte equality, then computes a
// 64-bit XXH3 content key for fast rejection and requires full byte equality
// of BOTH vertex and index extents before reporting a hit.
// Hash collisions therefore never produce false hits: a forced identical key
// with any differing byte or extent still misses.
//
// Per-backend/device owned: each NativeBackend owns its instances, so there
// are no static process-global entries and no cross-device hits. Eviction
// drops only the cache's strong reference; live caller handles keep their
// meshes alive. Hits perform no heap allocations: key computation, list
// traversal, byte equality and LRU splice are allocation-free; only the returned
// shared_ptr refcount is bumped. Miss insertion copies the input bytes once
// so later caller mutation cannot corrupt the snapshot.
template <class Mesh>
class ExactContentMeshCache {
public:
    explicit ExactContentMeshCache(size_t maxEntries = kMeshUploadCacheMaxEntries,
                                   size_t maxBytes = kMeshUploadCacheMaxBytes) noexcept
        : maxEntries_(maxEntries), maxBytes_(maxBytes) {}

    ExactContentMeshCache(const ExactContentMeshCache&) = delete;
    ExactContentMeshCache& operator=(const ExactContentMeshCache&) = delete;

    static uint64_t contentKey(std::span<const uint8_t> vertices,
                               std::span<const uint8_t> indices) noexcept {
        // XXH3 single-shot hashes: allocation-free and vectorized, suited to
        // tens of MiB/frame. Lengths are folded in so extent changes alter the
        // key; find() still enforces lengths + full byte equality via
        // ByteRangesEqual, so any residual collision is rejected there. Empty
        // planes hash a valid empty range.
        const void* vertexData =
            vertices.empty() ? static_cast<const void*>("") : static_cast<const void*>(vertices.data());
        const void* indexData =
            indices.empty() ? static_cast<const void*>("") : static_cast<const void*>(indices.data());
        uint64_t hash = XXH3_64bits(vertexData, vertices.size());
        hash = XXH3_64bits_withSeed(indexData, indices.size(), hash);
        const uint64_t extents[2] = {uint64_t(vertices.size()), uint64_t(indices.size())};
        hash = XXH3_64bits_withSeed(static_cast<const void*>(extents), sizeof(extents), hash);
        return hash;
    }

    // Returns a shared ownership copy on exact byte equality, or nullptr.
    // No heap allocations on either path. Promotes hits to most-recently-used.
    std::shared_ptr<Mesh> find(std::span<const uint8_t> vertices,
                               std::span<const uint8_t> indices,
                               uint64_t key) {
        for (auto it = lru_.begin(); it != lru_.end(); ++it) {
            if (it->key != key) continue;
            if (it->vertices.size() != vertices.size() ||
                it->indices.size() != indices.size())
                continue;
            if (!Simpsons::ByteRangesEqual(it->vertices.data(), vertices.data(), vertices.size()))
                continue;
            if (!Simpsons::ByteRangesEqual(it->indices.data(), indices.data(), indices.size()))
                continue;
            // Exact identical bytes (including extents): promote without
            // allocation and share GPU resources.
            if (it != lru_.begin()) lru_.splice(lru_.begin(), lru_, it);
            return lru_.front().mesh;
        }
        return nullptr;
    }

    struct LookupResult {
        std::shared_ptr<Mesh> mesh;
        uint64_t key{};
    };

    // Repeated static draws tend to cycle through the LRU in the same order,
    // making the next mesh the back entry. Compare both complete byte planes
    // and extents before trusting that entry; a miss retains the existing
    // content-key + find path. Return the key for miss insertion without a
    // second hash. Empty ranges and a one-entry LRU are safe.
    LookupResult lookup(std::span<const uint8_t> vertices,
                        std::span<const uint8_t> indices) {
        if (!lru_.empty()) {
            const auto& back = lru_.back();
            if (back.vertices.size() == vertices.size() &&
                back.indices.size() == indices.size() &&
                Simpsons::ByteRangesEqual(back.indices.data(), indices.data(), indices.size()) &&
                Simpsons::ByteRangesEqual(back.vertices.data(), vertices.data(), vertices.size())) {
                const uint64_t backKey = back.key;
                std::shared_ptr<Mesh> backMesh = back.mesh;
                auto backIt = std::prev(lru_.end());
                if (backIt != lru_.begin()) lru_.splice(lru_.begin(), lru_, backIt);
                return {backMesh, backKey};
            }
        }
        const uint64_t key = contentKey(vertices, indices);
        return {find(vertices, indices, key), key};
    }

    // Snapshots exact bytes and shares the freshly uploaded mesh. Evicts
    // least-recently-used entries while over the entry cap or byte budget.
    // Entries whose bytes alone exceed the budget are not retained, keeping
    // residency strictly bounded.
    void insert(std::span<const uint8_t> vertices, std::span<const uint8_t> indices,
                uint64_t key, std::shared_ptr<Mesh> mesh) {
        if (!mesh || maxEntries_ == 0) return;
        // Guard the byte-sum against size_t overflow before accounting.
        if (vertices.size() > SIZE_MAX - indices.size()) return;
        const size_t entryBytes = vertices.size() + indices.size();
        if (entryBytes > maxBytes_) return;
        Entry entry;
        entry.key = key;
        // Snapshot allocations happen here on the miss path only. If either
        // assign throws, no accounting has been touched yet.
        entry.vertices.assign(vertices.begin(), vertices.end());
        entry.indices.assign(indices.begin(), indices.end());
        entry.mesh = std::move(mesh);
        // push_front may throw (list node allocation); only account residency
        // after it has succeeded so a failure cannot corrupt the totals.
        lru_.push_front(std::move(entry));
        if (residentBytes_ > SIZE_MAX - entryBytes) {
            // Overflow is unreachable at production bounds (64 MiB), but never
            // let accounting wrap: drop back to the just-inserted entry.
            residentBytes_ = entryBytes;
            while (lru_.size() > 1) lru_.pop_back();
            return;
        }
        residentBytes_ += entryBytes;
        while (lru_.size() > maxEntries_ || residentBytes_ > maxBytes_) {
            const auto& back = lru_.back();
            const size_t backBytes = back.vertices.size() + back.indices.size();
            residentBytes_ -= backBytes;
            lru_.pop_back();
        }
    }

    void clear() noexcept {
        lru_.clear();
        residentBytes_ = 0;
    }

    size_t size() const noexcept { return lru_.size(); }
    size_t residentBytes() const noexcept { return residentBytes_; }
    size_t maxEntries() const noexcept { return maxEntries_; }
    size_t maxBytes() const noexcept { return maxBytes_; }

private:
    struct Entry {
        uint64_t key{};
        std::vector<uint8_t> vertices;
        std::vector<uint8_t> indices;
        std::shared_ptr<Mesh> mesh;
    };
    std::list<Entry> lru_;
    size_t maxEntries_;
    size_t maxBytes_;
    size_t residentBytes_{};
};

}  // namespace Simpsons::Graphics
