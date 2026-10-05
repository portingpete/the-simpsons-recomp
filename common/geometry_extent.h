#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Simpsons {
// Original8273B760 passes the owned vertex byte extent to82C1FAB0.
// Its82C1FB10 rlwinm stores aligned bits6..29: 03FFFFFC, rather than a
// vertex count or an R16 maximum. Upper-bit aliases remain unqualified.
inline constexpr size_t originalGeometryMaxVertexBytes = 0x03FFFFFC;
inline constexpr bool validOriginalVertexExtent(size_t bytes,uint32_t stride) noexcept {
    return stride && stride<=1020 && !(stride&3) && bytes &&
        bytes<=originalGeometryMaxVertexBytes && !(bytes&3);
}
// The original resource has an owned byte span, not an integral record count.
// A selected attribute may end before its stride's padding. Count records only
// while every byte through the highest consumed attribute end remains owned.
// Unused trailing bytes do not acquire another addressable vertex.
inline constexpr size_t originalFetchedVertexCount(size_t bytes,uint32_t stride,size_t fetchEnd) noexcept {
    return validOriginalVertexExtent(bytes,stride) && fetchEnd && fetchEnd<=stride && bytes>=fetchEnd
        ? 1+(bytes-fetchEnd)/stride : 0;
}

// Original8273B760 loads index bytes from geometry+14 independently of every
// submesh count.82C1FB80 stores that full32-bit word without masking/alignment.
// R16 fetches complete two-byte words; an unused odd final byte remains owned
// source data and cannot create another selectable index.
inline constexpr size_t originalGeometryMaxIndexBytes = (std::numeric_limits<uint32_t>::max)();
inline constexpr size_t originalFetchedIndexCount(size_t bytes) noexcept {
    return bytes>=2 && bytes<=originalGeometryMaxIndexBytes ? bytes/2 : 0;
}

// D3D11 buffer elements are bounded by2^27 and D3D11_BUFFER_DESC::ByteWidth
// is UINT. A device may impose a smaller allocation limit; CreateBuffer must
// still succeed before an upload/cache receipt can be published. This checks
// integer representability without pretending that R16 bounds owner size.
inline constexpr size_t nativeMeshMaxBufferElements = size_t{1}<<27;
inline constexpr bool validNativeMeshBufferExtent(size_t count,size_t elementBytes) noexcept {
    return count && elementBytes && count<=nativeMeshMaxBufferElements &&
        count<=(std::numeric_limits<uint32_t>::max)()/elementBytes;
}
}
