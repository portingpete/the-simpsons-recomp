#pragma once 

#include <cassert>
#include <cstdint>
#include <cstdlib>

#if defined(_MSC_VER)
    #define XENON_BSWAP16(x) _byteswap_ushort(x)
    #define XENON_BSWAP32(x) _byteswap_ulong(x)
    #define XENON_BSWAP64(x) _byteswap_uint64(x)
#else
    #define XENON_BSWAP16(x) __builtin_bswap16(x)
    #define XENON_BSWAP32(x) __builtin_bswap32(x)
    #define XENON_BSWAP64(x) __builtin_bswap64(x)
#endif

template<typename T>
inline T ByteSwap(T value)
{
    if constexpr (sizeof(T) == 1)
        return value;
    else if constexpr (sizeof(T) == 2)
        return static_cast<T>(XENON_BSWAP16(static_cast<uint16_t>(value)));
    else if constexpr (sizeof(T) == 4)
        return static_cast<T>(XENON_BSWAP32(static_cast<uint32_t>(value)));
    else if constexpr (sizeof(T) == 8)
        return static_cast<T>(XENON_BSWAP64(static_cast<uint64_t>(value)));

    assert(false && "Unexpected byte size.");
    return value;
}

template<typename T>
inline void ByteSwapInplace(T& value)
{
    value = ByteSwap(value);
}

