// Host regression for the exact heap containment index used by allocationSpan: it must agree with
// a scan of every live allocation (the smallest containing extent) for any layout, and stay fast
// at the allocation counts of a real hub level.
#include "runtime/heap_page_index.h"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
using Simpsons::HeapPageIndex;
namespace {
void need(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
// The scan allocationSpan used before the index existed.
std::optional<std::pair<uint32_t,uint32_t>> scanSmallest(const std::map<uint32_t,uint32_t>& live,uint32_t address) {
    std::optional<std::pair<uint32_t,uint32_t>> found;
    for(const auto& [start,extent]:live)
        if(address>=start&&uint64_t(address)<uint64_t(start)+extent&&(!found||extent<found->second))found={start,extent};
    return found;
}
std::optional<std::pair<uint32_t,uint32_t>> indexSmallest(const HeapPageIndex& index,uint32_t address) {
    std::optional<std::pair<uint32_t,uint32_t>> found;
    index.stab(address,[&](const HeapPageIndex::Ref& ref){if(!found||ref.extent<found->second)found={ref.start,ref.extent};});
    return found;
}
void agree(const HeapPageIndex& index,const std::map<uint32_t,uint32_t>& live,uint32_t address,const char* why) {
    const auto expected=scanSmallest(live,address),actual=indexSmallest(index,address);
    need(expected.has_value()==actual.has_value(),why);
    if(!expected)return;
    // Equal-extent overlapping allocations tie; a scan's pick among them is only iteration order, which the
    // allocator never produces. Require the same smallest extent and a genuinely containing live allocation.
    need(actual->second==expected->second&&live.contains(actual->first)&&live.at(actual->first)==actual->second&&
         address>=actual->first&&uint64_t(address)<uint64_t(actual->first)+actual->second,why);
}
}
int main() {
    try {
        // Exact page boundaries, single bytes and the top of the address space.
        {
            HeapPageIndex index;std::map<uint32_t,uint32_t> live;
            const auto add=[&](uint32_t start,uint32_t extent){index.add(start,extent);live[start]=extent;};
            add(0x1000,0x1000);add(0x3000,1);add(0x3FFF,2);add(0xFFFFF000u,0x1000);add(0x20000,0x2001);
            for(uint32_t address:{0x0FFFu,0x1000u,0x1FFFu,0x2000u,0x2FFFu,0x3000u,0x3001u,0x3FFEu,0x3FFFu,0x4000u,0x4001u,
                                  0x1FFFFu,0x20000u,0x21FFFu,0x22000u,0x22001u,0xFFFFEFFFu,0xFFFFF000u,0xFFFFFFFFu})
                agree(index,live,address,"boundary lookup differs from a scan");
            need(indexSmallest(index,0xFFFFFFFFu).has_value()&&!indexSmallest(index,0x0FFFu).has_value(),"top-of-memory or below-first lookup wrong");
        }
        // Nested and large allocations: the innermost (smallest) containing allocation wins; removal restores the outer one.
        {
            HeapPageIndex index;std::map<uint32_t,uint32_t> live;
            const auto add=[&](uint32_t start,uint32_t extent){index.add(start,extent);live[start]=extent;};
            const auto drop=[&](uint32_t start){index.remove(start,live.at(start));live.erase(start);};
            add(0x10000000,32u<<20);add(0x10400000,0x10000);add(0x10401000,0x100);
            need(index.largeCount()==1,"a 32 MiB allocation was registered in pages");
            for(uint32_t address:{0x0FFFFFFFu,0x10000000u,0x103FFFFFu,0x10400000u,0x10400FFFu,0x10401000u,0x104010FFu,0x10401100u,0x1040FFFFu,0x10410000u,0x11FFFFFFu,0x12000000u})
                agree(index,live,address,"nested/large lookup differs from a scan");
            need(indexSmallest(index,0x10401010u)->second==0x100,"innermost allocation was not preferred");
            drop(0x10401000);agree(index,live,0x10401010u,"removal did not restore the enclosing allocation");
            need(indexSmallest(index,0x10401010u)->second==0x10000,"enclosing allocation not restored");
            drop(0x10400000);drop(0x10000000);need(!indexSmallest(index,0x10401010u).has_value()&&index.pageCount()==0&&index.largeCount()==0,
                "removing everything left stale index entries");
            index.remove(0x1234,0x10);index.remove(0x10000000,32u<<20); // Removing an absent allocation is a no-op.
        }
        // Randomized add/remove including overlapping (non-laminar) layouts: exact for ANY layout.
        {
            std::mt19937 random(0x5EED);HeapPageIndex index;std::map<uint32_t,uint32_t> live;
            const uint32_t extents[]={1,15,16,4095,4096,4097,100000,(20u<<20)};
            for(unsigned round=0;round<40;++round) {
                for(unsigned i=0;i<150;++i) {
                    const uint32_t start=0x10000000u+(random()%0x4000000u);
                    if(live.contains(start))continue;
                    const uint32_t extent=extents[random()%std::size(extents)];
                    index.add(start,extent);live[start]=extent;
                }
                for(unsigned i=0;i<60&&!live.empty();++i) {
                    auto it=live.begin();std::advance(it,random()%live.size());
                    index.remove(it->first,it->second);live.erase(it);
                }
                for(unsigned probe=0;probe<400;++probe) {
                    auto it=live.begin();if(live.empty())break;std::advance(it,random()%live.size());
                    for(const uint32_t address:{it->first-1,it->first,it->first+it->second-1,it->first+it->second,uint32_t(0x10000000u+(random()%0x5000000u))})
                        agree(index,live,address,"randomized lookup differs from a scan");
                }
            }
        }
        // Scale: a real hub level tracks hundreds of thousands of live allocations; lookups must not scan them.
        {
            HeapPageIndex index;std::map<uint32_t,uint32_t> live;std::mt19937 random(7);
            uint32_t at=0x10000000u;
            for(unsigned i=0;i<300000;++i){const uint32_t extent=64+16*(random()%250);index.add(at,extent);live[at]=extent;at+=extent+16;}
            for(unsigned i=0;i<150;++i)agree(index,live,0x10000000u+random()%(at-0x10000000u),"scale lookup differs from a scan");
            volatile uint64_t sink=0;const auto begin=std::chrono::steady_clock::now();
            for(unsigned i=0;i<400000;++i)index.stab(0x10000000u+random()%(at-0x10000000u),[&](const HeapPageIndex::Ref& ref){sink=sink+ref.extent;});
            const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
            need(seconds<2.0,"allocation containment lookups scaled with the number of live allocations");
            std::printf("scale: 300000 allocations, 400000 lookups in %.3f s\n",seconds);
        }
        std::puts("PASS heap page index: exact against a scan for boundary, nested, large, removed, overlapping and 300000-allocation layouts");
        return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
