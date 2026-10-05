#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Simpsons {

// First-level store-barrier index: the number of armed pages in each 64 KiB block
// of the canonical guest address space (at most 16, so a byte never overflows).
// One 64 KiB table stays cache resident where the 1 MiB per-page flags do not,
// and the recompiled code only takes the exact per-page check for a store into a
// block whose count is nonzero. Maintained wherever a page flag changes state.
inline std::atomic<uint8_t> guestWatchBlocks[0x10000]{};
// Page 16*b is also counted in block b-1: an access that starts in the last bytes of
// block b-1 and straddles into an armed first page of block b is then caught by testing
// only the block of its first byte (one load per store).
inline void guestWatchBlockAdjust(uint32_t page, int delta) noexcept {
    const uint32_t block = page >> 4;
    if (delta > 0) {
        guestWatchBlocks[block].fetch_add(1, std::memory_order_seq_cst);
        if ((page & 15) == 0 && block) guestWatchBlocks[block - 1].fetch_add(1, std::memory_order_seq_cst);
    } else {
        guestWatchBlocks[block].fetch_sub(1, std::memory_order_seq_cst);
        if ((page & 15) == 0 && block) guestWatchBlocks[block - 1].fetch_sub(1, std::memory_order_seq_cst);
    }
}

// The live runtime's per-page armed flags and versions, so a change of a page's permission (which a
// validator armed for proves nothing about any more) invalidates the pages' watchers exactly like a store.
inline std::atomic<uint8_t>* guestWatchArmedTable = nullptr;
inline std::atomic<uint32_t>* guestWatchVersionTable = nullptr;
// Advanced by every permission or mapping change of any guest page (every permission-cell
// store and every watched-page invalidation), before the change becomes observable to a
// later check. A validator whose permission checks span pages it does not compare byte by
// byte can prove those permissions unchanged by an unchanged epoch.
inline std::atomic<uint64_t> guestPermissionEpoch{1};
// `page` indexes the permission table, i.e. the page of the guest address as issued; the watch tracks
// canonical pages, so physical aliases map to their canonical page first.
inline void guestWatchPermissionChanged(size_t page) noexcept {
    guestPermissionEpoch.fetch_add(1, std::memory_order_seq_cst);
    auto* armed = guestWatchArmedTable;
    if (!armed || page >= 0x100000) return;
    static constexpr uint32_t adjustment[8] = {0, 0, 0, 0, 0, 0, 0xE0000000u, 0xC0001000u};
    const uint32_t address = uint32_t(page << 12);
    const uint32_t canonical = (address + adjustment[address >> 29]) >> 12;
    if (armed[canonical].exchange(0, std::memory_order_seq_cst) == 1) {
        guestWatchBlockAdjust(canonical, -1);
        guestWatchVersionTable[canonical].fetch_add(1, std::memory_order_release);
    }
}

// Exact guest-write detection at 4 KiB canonical-page granularity.
//
// A reader that wants to prove "these guest bytes have not changed since I
// compared them" calls arm() BEFORE it reads/compares the bytes and stores the
// returned page versions with the result. Every guest store and every host
// write through Runtime::pointer(..., write=true) to an armed page clears the
// armed flag and bumps that page's version before the store lands, so a later
// unchanged() proves no write reached the pages since arm(). Mapping, unmapping
// and protection changes bump versions unconditionally. Physical aperture
// aliases share one canonical page, so a write through any alias is observed.
//
// Residual race: a different guest thread may have passed the armed check but
// not yet stored when another thread arms and compares. Callers therefore also
// re-verify every binding by full comparison on a bounded schedule.
struct GuestWriteWatch {
    const uint8_t* base = nullptr;
    std::atomic<uint8_t>* armed = nullptr;
    std::atomic<uint32_t>* version = nullptr;

    bool enabled() const noexcept { return base && armed && version; }

    // Page range of a host span inside the guest mapping.
    bool pages(const uint8_t* pointer, size_t size, uint32_t& first, uint32_t& last) const noexcept {
        if (!enabled() || !size || pointer < base) return false;
        const uint64_t offset = uint64_t(pointer - base);
        if (offset >= 0x100000000ull || size > 0x100000000ull - offset) return false;
        first = uint32_t(offset >> 12);
        last = uint32_t((offset + size - 1) >> 12);
        return true;
    }

    // Arms [first,last] and appends each page's version, in order.
    void arm(uint32_t first, uint32_t last, std::vector<uint32_t>& versions) const {
        for (uint32_t page = first; page <= last; ++page) {
            if (!armed[page].load(std::memory_order_acquire) &&
                armed[page].exchange(1, std::memory_order_seq_cst) == 0)
                guestWatchBlockAdjust(page, +1);
            versions.push_back(version[page].load(std::memory_order_acquire));
        }
    }

    // Arms one page and returns its version (read after arming, before the caller reads).
    uint32_t armPage(uint32_t page) const {
        if (!armed[page].load(std::memory_order_acquire) &&
            armed[page].exchange(1, std::memory_order_seq_cst) == 0)
            guestWatchBlockAdjust(page, +1);
        return version[page].load(std::memory_order_acquire);
    }

    // True iff every page in [first,last] still has the recorded version.
    bool unchanged(uint32_t first, uint32_t last, const uint32_t* recorded) const noexcept {
        for (uint32_t page = first; page <= last; ++page, ++recorded)
            if (version[page].load(std::memory_order_acquire) != *recorded) return false;
        return true;
    }
};

}  // namespace Simpsons
