#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include "../common/guest_write_watch.h"

namespace Simpsons {
// Software TLB for the fully checked native access path (PPCCheckedGuestPointer):
// 256 direct-mapped entries of (page<<2)|access, 0 = empty. It fits in L1, where the
// 1 MiB per-page permission table does not, so the common validated access avoids
// that table entirely. An entry is only ever a copy of the permission table, and any
// store to a page's permission cell clears the entry that could hold it (below), so a
// hit proves the same fresh permission the table lookup would.
inline std::atomic<uint32_t> guestAccessTlb[256]{};

// Per-page guest permission (bit0 readable, bit1 writable; 0 = unmapped). A drop-in
// for std::atomic<uint8_t>[]: load()/store() keep their meaning, and every store also
// invalidates the matching TLB entry.
class GuestPageAccessTable {
    std::unique_ptr<std::atomic<uint8_t>[]> cells{new std::atomic<uint8_t>[0x100000]{}};
public:
    class Cell {
        std::atomic<uint8_t>& value;
        size_t page;
    public:
        Cell(std::atomic<uint8_t>& v,size_t p):value(v),page(p) {}
        uint8_t load(std::memory_order order=std::memory_order_seq_cst) const noexcept {return value.load(order);}
        operator uint8_t() const noexcept {return load();}
        Cell& operator=(uint8_t v) noexcept {store(v);return *this;}
        void store(uint8_t v,std::memory_order order=std::memory_order_seq_cst) noexcept {
            value.store(v,order);
            guestAccessTlb[page&255].store(0,std::memory_order_relaxed);
            guestWatchPermissionChanged(page);
        }
    };
    Cell operator[](size_t page) noexcept {return Cell(cells[page],page);}
    uint8_t load(size_t page,std::memory_order order=std::memory_order_seq_cst) const noexcept {return cells[page].load(order);}
};
}
