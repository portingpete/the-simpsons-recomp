#pragma once
#include "runtime.h"
#include "native_window.h"

// Shared native memory contract; exceptional accesses retain the complete
// diagnostic path. This header is also used by selected original AOT chunks and by
// the validating native hooks, which need a thrown Failure (never a hardware fault)
// for an invalid guest pointer.
__declspec(noinline) uint8_t* PPCGuestPointerSlow(uint8_t*,uint32_t,unsigned,bool);
namespace Simpsons {
// Stable out-of-line exact barrier body (Runtime::noteGuestWrite).
void noteGuestWriteExact(uint32_t canonicalAddress,uint32_t width) noexcept;
}

// Exact guest-write barrier shared by every guest store path. The first byte's 64 KiB
// block carries the count of armed pages of that block and, for a block's first page,
// of the block before it, so one load covers a store that straddles a block boundary.
[[clang::always_inline]] inline void PPCGuestStoreBarrier(uint32_t canonical,unsigned width) {
    if(Simpsons::guestWatchBlocks[canonical>>16].load(std::memory_order_relaxed)) [[unlikely]]
        Simpsons::noteGuestWriteExact(canonical,width);
}

[[clang::always_inline]] inline uint8_t* PPCCheckedGuestPointer(uint8_t* base,uint32_t address,unsigned width,bool write) {
    // Single-page scalar/vector accesses dominate native validation of guest data.
    // A hit in the small software TLB proves the same permission the full table lookup
    // would (an entry is a copy of the permission cell, invalidated by every store to
    // it); a miss validates against the table and fills the entry. Cross-page and large
    // accesses, imports, opaque types and failures keep the checked implementation.
    // Cancellation is not polled here: original function entry and every wait poll it.
    if(width && width<=16 && (address&4095u)+width<=4096 && address>=0x10000 && address<0xFFD00000) [[likely]] {
        const uint32_t page=address>>12;
        const uint32_t required=write?3u:1u;
        // Eight 512-MiB apertures. Unsigned addition preserves the existing C/D
        // -0x20000000 and E/F -0x40000000+0x1000 aliases. Permissions remain those
        // of the ORIGINAL aperture's page.
        static constexpr uint32_t adjustment[8]={0,0,0,0,0,0,0xE0000000,0xC0001000};
        const uint32_t entry=Simpsons::guestAccessTlb[page&255].load(std::memory_order_relaxed);
        if((entry>>2)==page && (entry&required)==required) [[likely]] {
            const uint32_t canonical=address+adjustment[address>>29];
            if(write) PPCGuestStoreBarrier(canonical,width);
            return base+canonical;
        }
        // Route the two exceptional pages through their existing validator. Physical
        // aliases canonicalize into A/B, never either exceptional page; the single-page
        // bound above excludes straddling.
        if(page!=(Simpsons::Runtime::threadObjectType>>12) && page!=0x82000) [[likely]] {
            if(auto* runtime=Simpsons::active) [[likely]] {
                const uint8_t access=runtime->pageAccess.load(page,std::memory_order_acquire);
                if((access&required)==required) [[likely]] {
                    Simpsons::guestAccessTlb[page&255].store((page<<2)|(access&3u),std::memory_order_relaxed);
                    const uint32_t canonical=address+adjustment[address>>29];
                    if(write) PPCGuestStoreBarrier(canonical,width);
                    return base+canonical;
                }
            }
        }
    }
    return PPCGuestPointerSlow(base,address,width,write);
}
