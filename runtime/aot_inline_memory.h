#pragma once
#include "guest_memory.h"

// Forced into selected original translation units after ppc_context.h has
// finished defining its normal helpers. Distinct helper names avoid differing
// inline definitions between translation units. Only the generated access
// macros change; original volatile loads/stores, byte order and checks remain.
template<class UInt>
[[clang::always_inline]] inline UInt PPCInlineLoad(uint8_t* base,uint32_t address) {
    const UInt value=*(volatile UInt*)PPCCheckedGuestPointer(base,address,sizeof(UInt),false);
    if constexpr(sizeof(UInt)==1)return value;
    else if constexpr(sizeof(UInt)==2)return __builtin_bswap16(value);
    else if constexpr(sizeof(UInt)==4)return __builtin_bswap32(value);
    else return __builtin_bswap64(value);
}
[[clang::always_inline]] inline void PPCInlineStore32(uint8_t* base,uint32_t address,uint32_t value) {
    *(volatile uint32_t*)PPCCheckedGuestPointer(base,address,4,true)=__builtin_bswap32(value);
}
[[clang::always_inline]] inline void PPCInlineStore64(uint8_t* base,uint32_t address,uint64_t value) {
    *(volatile uint64_t*)PPCCheckedGuestPointer(base,address,8,true)=__builtin_bswap64(value);
}
#undef PPC_LOAD_U8
#undef PPC_LOAD_U16
#undef PPC_LOAD_U32
#undef PPC_LOAD_U64
#undef PPC_STORE_U8
#undef PPC_STORE_U16
#undef PPC_STORE_U32
#undef PPC_STORE_U64
#define PPC_LOAD_U8(x) PPCInlineLoad<uint8_t>(base,uint32_t(x))
#define PPC_LOAD_U16(x) PPCInlineLoad<uint16_t>(base,uint32_t(x))
#define PPC_LOAD_U32(x) PPCInlineLoad<uint32_t>(base,uint32_t(x))
#define PPC_LOAD_U64(x) PPCInlineLoad<uint64_t>(base,uint32_t(x))
#define PPC_STORE_U8(x,y) (*(volatile uint8_t*)PPCCheckedGuestPointer(base,uint32_t(x),1,true)=uint8_t(y))
#define PPC_STORE_U16(x,y) (*(volatile uint16_t*)PPCCheckedGuestPointer(base,uint32_t(x),2,true)=__builtin_bswap16(uint16_t(y)))
#define PPC_STORE_U32(x,y) PPCInlineStore32(base,uint32_t(x),uint32_t(y))
#define PPC_STORE_U64(x,y) PPCInlineStore64(base,uint32_t(x),uint64_t(y))
// Direct 16-byte vector and 128-byte dcbzl accesses keep the exact same
// checked path; width 128 still takes the existing slow path inside
// PPCCheckedGuestPointer. Defined last so the ppc_context/runtime
// declarations above are unaffected; runtime.cpp defining PPCGuestPointer
// does not include this header.
#define PPCGuestPointer(base,address,width,write) PPCCheckedGuestPointer((base),(address),(width),(write))
