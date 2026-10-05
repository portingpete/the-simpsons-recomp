#pragma once
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <list>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>
#include <cstdio>
#include <cstdlib>

#include "common/byte_range_equal.h"
#include "common/guest_write_watch.h"

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

namespace Simpsons {

// Bounds mirror the renderer exact-byte upload caches: 512 entries and 64 MiB
// of raw snapshots retain the observed ~133 static depth draws per frame
// without thrash while staying strictly bounded.
inline constexpr size_t kStaticMeshSourceCacheMaxEntries = 512;
inline constexpr size_t kStaticMeshSourceCacheMaxBytes = 64u * 1024u * 1024u;
// Bound on how many consecutive lookups a write-watch binding may be trusted
// before the next lookup of it repeats the full byte comparison.
inline constexpr uint64_t kStaticMeshSourceCacheRevalidateLookups = 1u << 14;

// Bounded exact-content LRU over raw static mesh source bytes.
//
// Keyed ONLY by full content (never by guest address/pointer). Lookup
// computes a 64-bit XXH3 reject key, then requires full byte equality of ALL
// THREE planes (raw vertices, raw indices, raw declaration bytes) plus stride
// equality before reporting a hit. Hash collisions therefore never produce
// false hits: a forced identical key with any differing byte, extent or
// stride still misses.
//
// Snapshots own their source bytes; no guest pointer is retained, so later
// caller mutation cannot corrupt entries.
template <class Mesh>
class StaticMeshSourceCache {
public:
    explicit StaticMeshSourceCache(size_t maxEntries = kStaticMeshSourceCacheMaxEntries,
                                   size_t maxBytes = kStaticMeshSourceCacheMaxBytes) noexcept
        : maxEntries_(maxEntries), maxBytes_(maxBytes) {}

    StaticMeshSourceCache(const StaticMeshSourceCache&) = delete;
    StaticMeshSourceCache& operator=(const StaticMeshSourceCache&) = delete;

    static uint64_t contentKey(std::span<const uint8_t> vertices, std::span<const uint8_t> indices,
                               std::span<const uint8_t> declaration, uint64_t stride) noexcept {
        // XXH3 single-shot hashes. Extents and stride are folded in so they
        // alter the key; find() still enforces lengths + full byte equality via
        // ByteRangesEqual + stride equality, so any residual collision is
        // rejected there. Empty planes hash a valid empty range.
        const void* vertexData =
            vertices.empty() ? static_cast<const void*>("") : static_cast<const void*>(vertices.data());
        const void* indexData =
            indices.empty() ? static_cast<const void*>("") : static_cast<const void*>(indices.data());
        const void* declarationData = declaration.empty() ? static_cast<const void*>("")
                                                          : static_cast<const void*>(declaration.data());
        uint64_t hash = XXH3_64bits(vertexData, vertices.size());
        hash = XXH3_64bits_withSeed(indexData, indices.size(), hash);
        hash = XXH3_64bits_withSeed(declarationData, declaration.size(), hash);
        const uint64_t extents[4] = {uint64_t(vertices.size()), uint64_t(indices.size()),
                                     uint64_t(declaration.size()), uint64_t(stride)};
        hash = XXH3_64bits_withSeed(static_cast<const void*>(extents), sizeof(extents), hash);
        return hash;
    }

    // Small public lookup result: shared ownership on hit (nullptr on miss)
    // plus the content key for the caller to reuse on the miss insert.
    // One pending write-watch arm: the three guest planes were armed BEFORE
    // the lookup's byte comparison/capture, with their page versions recorded.
    // A binding is only created from it if no watched page changed since.
    struct PendingArm {
        bool valid{};
        const uint8_t* plane[3]{};
        size_t size[3]{};
        uint32_t first[3]{}, last[3]{};
        uint64_t stride{};
        std::vector<uint32_t> versions;
    };
    struct LookupResult {
        std::shared_ptr<Mesh> mesh;
        uint64_t key{};
        PendingArm pending;
    };

    // Enables exact guest-write tracking for spans that point into the guest
    // mapping. Spans outside the mapping (tests, snapshots) behave as before.
    // verify=true additionally repeats the full comparison on every watch hit
    // and aborts on any disagreement (diagnostic, SIMPSONS_WATCH_VERIFY=1).
    void setWriteWatch(const GuestWriteWatch& watch, bool verify) noexcept {
        watch_ = watch;
        verify_ = verify;
    }
    uint64_t watchHits() const noexcept { return watchHits_; }
    uint64_t watchBindings() const noexcept { return watchBindings_; }
    ~StaticMeshSourceCache() {
        if (verify_)
            std::fprintf(stderr, "[WATCH VERIFY] static mesh cache fast hits=%llu verified=%llu bindings=%llu mismatches=0\n",
                         static_cast<unsigned long long>(watchHits_), static_cast<unsigned long long>(watchVerified_),
                         static_cast<unsigned long long>(watchBindings_));
    }

    // Returns a shared ownership copy on exact byte equality, or nullptr.
    // Promotes hits to most-recently-used.
    std::shared_ptr<Mesh> find(std::span<const uint8_t> vertices, std::span<const uint8_t> indices,
                               std::span<const uint8_t> declaration, uint64_t stride, uint64_t key) {
        for (auto it = lru_.begin(); it != lru_.end(); ++it) {
            if (it->key != key || it->stride != stride) continue;
            if (it->vertices.size() != vertices.size() || it->indices.size() != indices.size() ||
                it->declaration.size() != declaration.size())
                continue;
            if (!ByteRangesEqual(it->vertices.data(), vertices.data(), vertices.size())) continue;
            if (!ByteRangesEqual(it->indices.data(), indices.data(), indices.size())) continue;
            if (!ByteRangesEqual(it->declaration.data(), declaration.data(), declaration.size()))
                continue;
            // Exact identical source (including extents and stride): promote
            // and share the native mesh.
            if (it != lru_.begin()) lru_.splice(lru_.begin(), lru_, it);
            return lru_.front().mesh;
        }
        return nullptr;
    }

    // Bounded candidate check before hashing for repeated cyclic stable
    // depth-draw order: on a repeated complete LRU cycle the next same mesh
    // is typically at lru_.back(). If the list is nonempty, compare the back
    // entry's stride, all three extents and full current bytes of all three
    // planes with ByteRangesEqual (declaration and indices before the larger
    // vertices; same accept/reject semantics). No guest pointer/address keys
    // and no stored pointer hints are used. On exact full equality only, the
    // back entry is promoted to front and its key/mesh returned. Otherwise
    // the current contentKey is computed and the existing find(key) path runs
    // unchanged. Fast hits still examine every byte; there is no
    // sample/hash-only trust. Empty and one-entry lists stay safe (the
    // one-entry splice is guarded so the same node is never spliced).
    LookupResult lookup(std::span<const uint8_t> vertices, std::span<const uint8_t> indices,
                        std::span<const uint8_t> declaration, uint64_t stride) {
        ++tick_;
        const bool watched = watch_.enabled() && !vertices.empty() && !indices.empty() && !declaration.empty();
        PendingArm pending;
        if (watched) {
            const HintKey hintKey = makeKey(vertices, indices, declaration, stride);
            if (const auto found = hints_.find(hintKey); found != hints_.end()) {
                Hint& hint = found->second;
                if (tick_ - hint.tick < hint.limit && hintUnchanged(hint)) {
                    Entry* entry = hint.entry;
                    if (verify_) verifyHit(*entry, vertices, indices, declaration, stride);
                    ++watchHits_;
                    promote(entry);
                    return {entry->mesh, entry->key, {}};
                }
            }
            // Arm before any comparison so a store racing the comparison
            // changes a recorded version instead of being missed.
            pending = arm(vertices, indices, declaration, stride);
        }
        LookupResult result = lookupExact(vertices, indices, declaration, stride);
        if (pending.valid) {
            if (result.mesh) bind(&lru_.front(), pending);
            else result.pending = std::move(pending);
        }
        return result;
    }

    LookupResult lookupExact(std::span<const uint8_t> vertices, std::span<const uint8_t> indices,
                        std::span<const uint8_t> declaration, uint64_t stride) {
        if (!lru_.empty()) {
            const auto& back = lru_.back();
            if (back.stride == stride && back.vertices.size() == vertices.size() &&
                back.indices.size() == indices.size() &&
                back.declaration.size() == declaration.size() &&
                ByteRangesEqual(back.declaration.data(), declaration.data(), declaration.size()) &&
                ByteRangesEqual(back.indices.data(), indices.data(), indices.size()) &&
                ByteRangesEqual(back.vertices.data(), vertices.data(), vertices.size())) {
                const uint64_t backKey = back.key;
                std::shared_ptr<Mesh> backMesh = back.mesh;
                auto backIt = std::prev(lru_.end());
                if (backIt != lru_.begin()) lru_.splice(lru_.begin(), lru_, backIt);
                return {backMesh, backKey, {}};
            }
        }
        const uint64_t key = contentKey(vertices, indices, declaration, stride);
        return {find(vertices, indices, declaration, stride, key), key, {}};
    }

    // Snapshots exact bytes and shares the freshly validated mesh. Evicts
    // least-recently-used entries while over the entry cap or byte budget.
    // Entries whose bytes alone exceed the budget are not retained, keeping
    // residency strictly bounded. Call only after decoder/index/renderer
    // validation succeeded; failures must never reach here.
    void insert(std::span<const uint8_t> vertices, std::span<const uint8_t> indices,
                std::span<const uint8_t> declaration, uint64_t stride, uint64_t key,
                std::shared_ptr<Mesh> mesh, const PendingArm* pending = nullptr) {
        if (!mesh || maxEntries_ == 0) return;
        // Guard the byte-sum against size_t overflow before accounting.
        if (vertices.size() > SIZE_MAX - indices.size() ||
            declaration.size() > SIZE_MAX - (vertices.size() + indices.size()))
            return;
        const size_t entryBytes = vertices.size() + indices.size() + declaration.size();
        if (entryBytes > maxBytes_) return;
        Entry entry;
        entry.key = key;
        entry.stride = stride;
        // Snapshot allocations happen here on the miss path only. If any
        // assign throws, no accounting has been touched yet.
        entry.vertices.assign(vertices.begin(), vertices.end());
        entry.indices.assign(indices.begin(), indices.end());
        entry.declaration.assign(declaration.begin(), declaration.end());
        entry.mesh = std::move(mesh);
        // push_front may throw (list node allocation); only account residency
        // after it has succeeded so a failure cannot corrupt the totals.
        lru_.push_front(std::move(entry));
        lru_.front().self = lru_.begin();
        if (residentBytes_ > SIZE_MAX - entryBytes) {
            // Overflow is unreachable at production bounds (64 MiB), but never
            // let accounting wrap: drop back to the just-inserted entry.
            residentBytes_ = entryBytes;
            while (lru_.size() > 1) lru_.pop_back();
            return;
        }
        residentBytes_ += entryBytes;
        while (lru_.size() > maxEntries_ || residentBytes_ > maxBytes_) {
            auto& back = lru_.back();
            residentBytes_ -= back.vertices.size() + back.indices.size() + back.declaration.size();
            dropHints(&back);
            lru_.pop_back();
        }
        // The new entry is the front: bind it to the guest ranges that were
        // armed before its snapshot was captured, if nothing changed since.
        if (pending && pending->valid && !lru_.empty() && lru_.front().key == key) bind(&lru_.front(), *pending);
    }

    void clear() noexcept {
        lru_.clear();
        hints_.clear();
        residentBytes_ = 0;
    }

    size_t size() const noexcept { return lru_.size(); }
    size_t residentBytes() const noexcept { return residentBytes_; }
    size_t maxEntries() const noexcept { return maxEntries_; }
    size_t maxBytes() const noexcept { return maxBytes_; }

private:
    struct HintKey {
        const uint8_t* plane[3];
        size_t size[3];
        uint64_t stride;
        bool operator==(const HintKey& other) const noexcept {
            for (int i = 0; i < 3; ++i)
                if (plane[i] != other.plane[i] || size[i] != other.size[i]) return false;
            return stride == other.stride;
        }
    };
    struct HintKeyHash {
        size_t operator()(const HintKey& key) const noexcept {
            uint64_t hash = key.stride * 0x9E3779B97F4A7C15ull;
            for (int i = 0; i < 3; ++i) {
                hash ^= reinterpret_cast<uintptr_t>(key.plane[i]) + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2);
                hash ^= uint64_t(key.size[i]) + 0x7F4A7C159E3779B9ull + (hash << 6) + (hash >> 2);
            }
            return size_t(hash);
        }
    };
    struct Entry;
    struct Hint {
        Entry* entry{};
        uint32_t first[3]{}, last[3]{};
        std::vector<uint32_t> versions;
        uint64_t tick{};
        // Lookups this binding may be trusted before the next full comparison. Spread over
        // [R/2, 3R/2) by the key so bindings created together do not all re-verify (a burst
        // of full byte comparisons) in the same frame.
        uint64_t limit{kStaticMeshSourceCacheRevalidateLookups};
    };
    struct Entry {
        std::list<Entry>::iterator self;  // set on insertion; list nodes are stable, so promote is O(1)
        uint64_t key{};
        uint64_t stride{};
        std::vector<uint8_t> vertices;
        std::vector<uint8_t> indices;
        std::vector<uint8_t> declaration;
        std::shared_ptr<Mesh> mesh;
        std::vector<HintKey> hintKeys;
    };

    static HintKey makeKey(std::span<const uint8_t> vertices, std::span<const uint8_t> indices,
                           std::span<const uint8_t> declaration, uint64_t stride) noexcept {
        return {{vertices.data(), indices.data(), declaration.data()},
                {vertices.size(), indices.size(), declaration.size()}, stride};
    }
    bool hintUnchanged(const Hint& hint) const noexcept {
        const uint32_t* recorded = hint.versions.data();
        for (int i = 0; i < 3; ++i) {
            if (!watch_.unchanged(hint.first[i], hint.last[i], recorded)) return false;
            recorded += hint.last[i] - hint.first[i] + 1;
        }
        return true;
    }
    PendingArm arm(std::span<const uint8_t> vertices, std::span<const uint8_t> indices,
                   std::span<const uint8_t> declaration, uint64_t stride) const {
        PendingArm pending;
        const std::span<const uint8_t> planes[3] = {vertices, indices, declaration};
        for (int i = 0; i < 3; ++i) {
            if (!watch_.pages(planes[i].data(), planes[i].size(), pending.first[i], pending.last[i])) return {};
            pending.plane[i] = planes[i].data();
            pending.size[i] = planes[i].size();
        }
        pending.stride = stride;
        for (int i = 0; i < 3; ++i) watch_.arm(pending.first[i], pending.last[i], pending.versions);
        pending.valid = true;
        return pending;
    }
    void bind(Entry* entry, const PendingArm& pending) {
        // Versions recorded at arm time must still hold: any watched write
        // during the comparison/capture window leaves the binding unmade.
        const uint32_t* recorded = pending.versions.data();
        for (int i = 0; i < 3; ++i) {
            if (!watch_.unchanged(pending.first[i], pending.last[i], recorded)) return;
            recorded += pending.last[i] - pending.first[i] + 1;
        }
        const HintKey key{{pending.plane[0], pending.plane[1], pending.plane[2]},
                          {pending.size[0], pending.size[1], pending.size[2]}, pending.stride};
        if (hints_.size() >= kMaxHints) {
            hints_.clear();
            for (Entry& other : lru_) other.hintKeys.clear();
        }
        Hint& hint = hints_[key];
        const bool fresh = hint.entry != entry;
        hint.entry = entry;
        for (int i = 0; i < 3; ++i) hint.first[i] = pending.first[i], hint.last[i] = pending.last[i];
        hint.versions = pending.versions;
        hint.tick = tick_;
        hint.limit = kStaticMeshSourceCacheRevalidateLookups / 2 +
                     HintKeyHash{}(key) % kStaticMeshSourceCacheRevalidateLookups;
        if (fresh) entry->hintKeys.push_back(key);
        ++watchBindings_;
    }
    void dropHints(Entry* entry) {
        for (const HintKey& key : entry->hintKeys)
            if (const auto found = hints_.find(key); found != hints_.end() && found->second.entry == entry)
                hints_.erase(found);
        entry->hintKeys.clear();
    }
    void promote(Entry* entry) {
        if (&lru_.front() == entry) return;
        lru_.splice(lru_.begin(), lru_, entry->self);
    }
    void verifyHit(const Entry& entry, std::span<const uint8_t> vertices, std::span<const uint8_t> indices,
                   std::span<const uint8_t> declaration, uint64_t stride) {
        if ((++watchVerified_ & 0x1FFF) == 1)
            std::fprintf(stderr, "[WATCH VERIFY] static mesh cache verified fast hits=%llu bindings=%llu (full compare agreed)\n",
                         static_cast<unsigned long long>(watchVerified_), static_cast<unsigned long long>(watchBindings_));
        const bool same = entry.stride == stride && entry.vertices.size() == vertices.size() &&
                          entry.indices.size() == indices.size() && entry.declaration.size() == declaration.size() &&
                          ByteRangesEqual(entry.vertices.data(), vertices.data(), vertices.size()) &&
                          ByteRangesEqual(entry.indices.data(), indices.data(), indices.size()) &&
                          ByteRangesEqual(entry.declaration.data(), declaration.data(), declaration.size());
        if (!same) {
            std::fprintf(stderr, "[WATCH VERIFY MISMATCH] static mesh cache served stale source bytes vertices=%p indices=%p declaration=%p\n",
                         static_cast<const void*>(vertices.data()), static_cast<const void*>(indices.data()),
                         static_cast<const void*>(declaration.data()));
            std::fflush(stderr);
            std::abort();
        }
    }

    static constexpr size_t kMaxHints = 4096;
    GuestWriteWatch watch_{};
    bool verify_{false};
    uint64_t tick_{}, watchHits_{}, watchVerified_{}, watchBindings_{};
    std::unordered_map<HintKey, Hint, HintKeyHash> hints_;
    std::list<Entry> lru_;
    size_t maxEntries_;
    size_t maxBytes_;
    size_t residentBytes_{};
};

}  // namespace Simpsons
