#pragma once
// Exact interval-stabbing index over live guest-heap allocations.
//
// "Which allocation contains this address?" is asked per draw by the producer-entry
// receipts and by row-span admission. A scan of every live allocation made that cost
// proportional to the whole heap (hundreds of thousands of blocks in a hub level), so
// 4 KiB pages each list the allocations that overlap them. Allocations of 16 MiB or
// more sit in a short side list instead of being registered in thousands of pages.
// Queries return every containing allocation; the caller keeps its own selection rule
// (the smallest extent), so overlapping or nested layouts behave exactly as a scan.
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Simpsons {
class HeapPageIndex {
public:
    struct Ref {uint32_t start,extent;};
    static constexpr uint32_t pageShift=12,largeExtent=16u<<20;
    // extent must be nonzero and start+extent must not exceed 2^32.
    void add(uint32_t start,uint32_t extent) {
        if(extent>=largeExtent){large.push_back({start,extent});return;}
        for(uint32_t page=first(start),end=last(start,extent);;++page) {
            pages[page].push_back({start,extent});
            if(page==end)break;
        }
    }
    void remove(uint32_t start,uint32_t extent) {
        const auto drop=[&](std::vector<Ref>& refs)->bool {
            for(auto& ref:refs)
                if(ref.start==start&&ref.extent==extent){ref=refs.back();refs.pop_back();return true;}
            return false;
        };
        if(extent>=largeExtent){drop(large);return;}
        for(uint32_t page=first(start),end=last(start,extent);;++page) {
            const auto found=pages.find(page);
            if(found!=pages.end()){drop(found->second);if(found->second.empty())pages.erase(found);}
            if(page==end)break;
        }
    }
    // Calls visit(Ref) for every registered allocation whose range contains the address.
    template<class Visit> void stab(uint32_t address,Visit&& visit) const {
        if(const auto found=pages.find(address>>pageShift);found!=pages.end())
            for(const auto& ref:found->second)
                if(address>=ref.start&&uint64_t(address)<uint64_t(ref.start)+ref.extent)visit(ref);
        for(const auto& ref:large)
            if(address>=ref.start&&uint64_t(address)<uint64_t(ref.start)+ref.extent)visit(ref);
    }
    size_t pageCount() const noexcept {return pages.size();}
    size_t largeCount() const noexcept {return large.size();}
private:
    static uint32_t first(uint32_t start) {return start>>pageShift;}
    static uint32_t last(uint32_t start,uint32_t extent) {return uint32_t((uint64_t(start)+extent-1)>>pageShift);}
    std::unordered_map<uint32_t,std::vector<Ref>> pages;
    std::vector<Ref> large;
};
}
