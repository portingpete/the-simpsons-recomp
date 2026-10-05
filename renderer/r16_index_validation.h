#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace Simpsons::Graphics {
// Owned R16 index words of an immutable mesh plus the range of their non-cut values.
// FFFF is the strip cut and never names a vertex. The bounds are computed once when the
// words are assigned, so validating a draw does not rescan its index range every frame.
class R16Indices {
    std::vector<uint16_t> words_;
    uint32_t minRaw_=0xFFFF,maxRaw_=0;
    bool anyVertex_=false;
public:
    template<class It> void assign(It first,It last) {
        words_.assign(first,last);
        minRaw_=0xFFFF;maxRaw_=0;anyVertex_=false;
        for(const uint16_t raw:words_) {
            if(raw==0xFFFF)continue;
            anyVertex_=true;minRaw_=std::min<uint32_t>(minRaw_,raw);maxRaw_=std::max<uint32_t>(maxRaw_,raw);
        }
    }
    size_t size() const noexcept {return words_.size();}
    bool empty() const noexcept {return words_.empty();}
    const uint16_t* data() const noexcept {return words_.data();}
    operator std::span<const uint16_t>() const noexcept {return {words_.data(),words_.size()};}
    // True when every non-cut word of the whole buffer is a valid vertex for this base,
    // so any subrange is too. (False only means "scan the range", not "invalid".)
    bool wholeBufferValid(uint32_t vertexCount,int32_t baseVertex) const noexcept {
        if(!anyVertex_)return true;
        return int64_t(minRaw_)+baseVertex>=0 && int64_t(maxRaw_)+baseVertex<int64_t(vertexCount);
    }
};

// Original indexed submeshes supply an independent signed base offset, start
// and count. Raw R16 words cannot be checked against vertices until that draw
// range is known. FFFF is the strip cut before the base is added.
inline bool validR16DrawRange(std::span<const uint16_t> indices,uint32_t vertexCount,
                             uint32_t startIndex,uint32_t indexCount,int32_t baseVertex) noexcept {
    const uint64_t end=uint64_t(startIndex)+indexCount;
    // Original827013A0/8244D37C transports the full unsigned selected count.
    // Counts0/1/2 on nonempty owned buffers are valid no-triangle submissions.
    // A zero count still requires a retained index/vertex owner and bounded
    // start; it does not qualify missing buffers or a start beyond the span.
    if(!vertexCount||indices.empty()||end>indices.size())return false;
    for(uint64_t at=startIndex;at<end;++at) {
        const auto raw=indices[static_cast<std::size_t>(at)];
        if(raw==0xFFFF)continue;
        const int64_t effective=int64_t(raw)+baseVertex;
        if(effective<0||uint64_t(effective)>=vertexCount)return false;
    }
    return true;
}
// Same result as the span form: the whole-buffer bounds accept without a scan whenever they
// can, and only a draw the bounds cannot vouch for scans its own range.
template<class Owned,std::enable_if_t<std::is_same_v<Owned,R16Indices>,int> = 0>
inline bool validR16DrawRange(const Owned& indices,uint32_t vertexCount,
                             uint32_t startIndex,uint32_t indexCount,int32_t baseVertex) noexcept {
    const uint64_t end=uint64_t(startIndex)+indexCount;
    if(!vertexCount||indices.empty()||end>indices.size())return false;
    if(indices.wholeBufferValid(vertexCount,baseVertex))return true;
    return validR16DrawRange(std::span<const uint16_t>(indices),vertexCount,startIndex,indexCount,baseVertex);
}
}
