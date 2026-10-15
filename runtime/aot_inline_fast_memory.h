#pragma once
#include "guest_memory.h"

// These names are emitted only inside individually hash-qualified original
// functions. Keep the same hardware permission/fault path and ordered volatile
// byte/halfword/word accesses as PPCGuestPointerRead/Write; only the leaf call is inlined.
// Native validators and every other generated function keep their own helpers.
__declspec(noinline) uint8_t* PPCGuestPointerBarrier(uint8_t*,uint32_t,unsigned);

template<bool Write,unsigned Width>
[[clang::always_inline]] inline uint8_t* PPCInlineFastPointer(uint8_t* base,uint32_t address) {
    static_assert(Width==1||Width==2||Width==4);
#if defined(SIMPSONS_FAST_GUEST_MEMORY) && SIMPSONS_FAST_GUEST_MEMORY
    if((address>>12)==0x82000u) [[unlikely]] return PPCGuestPointerSlow(base,address,Width,Write);
    static constexpr uint32_t adjustment[8]={0,0,0,0,0,0,0xE0000000u,0xC0001000u};
    const uint32_t canonical=address+adjustment[address>>29];
    if constexpr(Write) {
        if(Simpsons::guestWatchBlocks[canonical>>16].load(std::memory_order_relaxed)) [[unlikely]]
            return PPCGuestPointerBarrier(base,canonical,Width);
    }
    return base+canonical;
#else
    return Write?PPC_GUEST_WRITE(base,address,Width):PPC_GUEST_READ(base,address,Width);
#endif
}

template<bool Write>
[[clang::always_inline]] inline uint8_t* PPCInlineFastWordPointer(uint8_t* base,uint32_t address) {
    return PPCInlineFastPointer<Write,4>(base,address);
}

[[clang::always_inline]] inline uint8_t PPCInlineFastLoad8(uint8_t* base,uint32_t address) {
    return *(volatile uint8_t*)PPCInlineFastPointer<false,1>(base,address);
}
[[clang::always_inline]] inline void PPCInlineFastStore8(uint8_t* base,uint32_t address,uint8_t value) {
    *(volatile uint8_t*)PPCInlineFastPointer<true,1>(base,address)=value;
}

[[clang::always_inline]] inline uint16_t PPCInlineFastLoad16(uint8_t* base,uint32_t address) {
    return __builtin_bswap16(*(volatile uint16_t*)PPCInlineFastPointer<false,2>(base,address));
}
[[clang::always_inline]] inline void PPCInlineFastStore16(uint8_t* base,uint32_t address,uint16_t value) {
    *(volatile uint16_t*)PPCInlineFastPointer<true,2>(base,address)=__builtin_bswap16(value);
}

[[clang::always_inline]] inline uint32_t PPCInlineFastLoad32(uint8_t* base,uint32_t address) {
    return __builtin_bswap32(*(volatile uint32_t*)PPCInlineFastWordPointer<false>(base,address));
}
[[clang::always_inline]] inline void PPCInlineFastStore32(uint8_t* base,uint32_t address,uint32_t value) {
    *(volatile uint32_t*)PPCInlineFastWordPointer<true>(base,address)=__builtin_bswap32(value);
}
#define PPC_FAST_INLINE_LOAD_U32(x) PPCInlineFastLoad32(base,uint32_t(x))
#define PPC_FAST_INLINE_STORE_U32(x,y) PPCInlineFastStore32(base,uint32_t(x),uint32_t(y))
#define PPC_FAST_INLINE_LOAD_U8(x) PPCInlineFastLoad8(base,uint32_t(x))
#define PPC_FAST_INLINE_STORE_U8(x,y) PPCInlineFastStore8(base,uint32_t(x),uint8_t(y))
#define PPC_FAST_INLINE_LOAD_U16(x) PPCInlineFastLoad16(base,uint32_t(x))
#define PPC_FAST_INLINE_STORE_U16(x,y) PPCInlineFastStore16(base,uint32_t(x),uint16_t(y))
